// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/mixer.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "muse/client_phone.h"
#include "muse/view.hpp"
#include "muse/local_player.hpp"
#include "muse/wav.hpp"
#include "muse/worker.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>
using namespace hui;
namespace {
std::string env(const char *k, const char *f) {
  const char *v = getenv(k);
  return v && *v ? v : f;
}
bool load(gfx::Renderer &r, const std::string &dir, const char *name,
          gfx::Font &font, ui::FontRef &ref) {
  std::string s;
  if (!save::read_file(dir + "/fonts/" + name, &s) || !font.load(s))
    return false;
  ref = {&font, r.batch().create_font_texture(font)};
  return true;
}
const char *fields[] = {"Your console's LAN IP", "Relay address (private LAN HTTP only)",
                        "PS5LM fallback (optional)",
                        "Speech fallback (optional)", "AudioIn module path (optional for typing)", "Relay console token (64 lowercase hex)", "ShadowMount API on this console"};
char *field(muse_config &c, int i) {
  switch (i) {
  case 0:
    return c.console_ip;
  case 1:
    return c.server_url;
  case 2:
    return c.ps5lm_url;
  case 3:
    return c.speech_url;
  case 4:
    return c.audio_in_module;
  case 5:
    return c.relay_token;
  default:
    return c.shadowmount_url;
  }
}
size_t field_cap(int i) { return i == 0 ? 128 : i == 4 ? 512 : i == 5 ? 65 : 256; }
bool read_speech(const std::string &path, std::vector<float> &out) {
  std::string bytes;
  return save::read_file(path, &bytes, 32 * 1024 * 1024) &&
         muse::decode_speech_wav(bytes, out);
}

} // namespace
int main() {
  const std::string storage = env("MUSE_DATA_DIR", "/download0/muse");
  save::ensure_directory(storage);
  const std::string config_path =
      env("MUSE_CONFIG_PATH", (storage + "/config.json").c_str());
  std::string assets = env("MUSE_ASSET_DIR", "/app0/assets");
  if (access((assets + "/fonts/inter-regular.huifont").c_str(), R_OK) &&
      !access("assets/fonts/inter-regular.huifont", R_OK))
    assets = "assets";
  muse_config cfg{};
  muse_config_defaults(&cfg);
  char config_error[256]{};
  bool configured = muse_config_load(config_path.c_str(), &cfg, config_error,
                                     sizeof config_error) == 0;
  if (!configured) {
    muse_config_defaults(&cfg);
    snprintf(cfg.log_dir, sizeof cfg.log_dir, "%s/logs", storage.c_str());
    snprintf(cfg.artifact_dir, sizeof cfg.artifact_dir, "%s/artifacts",
             storage.c_str());
    snprintf(cfg.model_dir, sizeof cfg.model_dir, "%s/inference",
             assets.c_str());
    snprintf(cfg.firmware_reported, sizeof cfg.firmware_reported, "12.40");
  }
  if (!cfg.install_helper_path[0])
    snprintf(cfg.install_helper_path, sizeof cfg.install_helper_path, "%s/install/muse-install-helper.payload", assets.c_str());
  ps5::Display display;
  if (!display.open()) {
    sys::log("Muse: display initialization failed");
    sys::park();
  }
  gfx::Renderer renderer;
  std::array<gfx::Font, 4> faces;
  ui::Fonts fonts;
  if (!renderer.init() ||
      !load(renderer, assets, "inter-regular.huifont", faces[0],
            fonts.regular) ||
      !load(renderer, assets, "inter-semibold.huifont", faces[1],
            fonts.semibold) ||
      !load(renderer, assets, "montserrat-medium.huifont", faces[2],
            fonts.display) ||
      !load(renderer, assets, "dejavu-sans-mono.huifont", faces[3],
            fonts.mono)) {
    sys::log("Muse: fonts/render initialization failed");
    sys::park();
  }
  ps5::Pad pad;
  pad.open();
  InputTracker tracker;
  audio::Mixer mixer;
  ps5::AudioOut audio_out;
  audio_out.start(mixer);
  std::vector<float> speech;
  audio::Clip speech_clip{};
  muse::View view;
  muse_gate gate{};
  muse_reset(&gate);
  muse::Worker worker;
  muse_phone *phone = nullptr;
  bool worker_started = false, exit_pending = false, quit_after_flush = false,
       session_flush = false, need_init = false;
  double poll_at = 0, install_poll_at = 0, log_retry_at = 30;
  double plan_poll_at = 0, plan_wait_started = 0;
  bool plan_active = false, plan_recovery = false;
  std::string waiting_plan;
  bool install_active = false, install_launch_queued = false;
  std::string artifact, pending_speech, pending_speech_path;
  bool plan_read = false;
  muse_config pending_config = cfg;
  std::vector<int> setup_fields;
  std::vector<muse_local_job> library;
  muse_local_job local_job{};
  muse::LocalPlayer player;
  bool local_run = false, library_pending = false, play_pending = false;
  bool refining = false;
  bool open_model_setup = false;
  bool cancel_pending = false;
  muse::Work cancelled_plan;
  const unsigned attempt_budgets[] = {4, 64, 512};
  const unsigned time_budgets[] = {7200, 86400, 604800};
  auto sync_mode = [&]() {
    view.local = muse_config_local(&cfg);
    view.provider = !strcmp(cfg.hybrid_provider, "codex") ? "Codex" : "Muse";
    view.server = cfg.server_url;
  };
  sync_mode();
  auto home_detail = [&]() {
    return view.local ? "Press X to describe an idea, or hold R2 to speak."
                      : "Hold R2 to speak, or press X to type what Muse should create.";
  };
  auto initialize = [&]() {
    save::ensure_directory(cfg.log_dir);
    save::ensure_directory(cfg.artifact_dir);
    if (!worker_started) {
      worker_started = worker.start(cfg);
      need_init = worker_started;
      plan_recovery = worker_started && !muse_config_local(&cfg);
      if (!worker_started) {
        view.screen = muse::Screen::error;
        view.detail =
            "The background worker could not start. Close and reopen MuseStation.";
      }
      if (worker_started && !muse_config_local(&cfg) && cfg.phone_port) {
        char err[256]{};
        phone = muse_phone_start(&cfg, muse::Worker::phone_submit, &worker, err,
                                 sizeof err);
        if (!phone)
          sys::log("Muse phone listener: %s", err);
      }
    }
    muse_set_voice_verified(&gate, cfg.voice_gate_verified);
    view.voice_verified = cfg.voice_gate_verified;
    sync_mode();
    view.detail=home_detail();
  };
  auto modes = [&]() {
    view.screen = muse::Screen::modes;
    view.mode_choice = muse_config_local(&cfg) ? 1 : 0;
    pending_config = cfg;
    view.error.clear();
  };
  auto setup = [&]() {
    view.screen = muse::Screen::setup;
    setup_fields = muse_config_local(&pending_config) ? std::vector<int>{4} : std::vector<int>{0,1,2,3,4,5,6};
    view.secret_edit = false;
    view.field_index = 0;
    view.step_count = int(setup_fields.size());
    view.field = fields[setup_fields[0]];
    view.edit = field(pending_config, setup_fields[0]);
    view.key = 0;
    view.error.clear();
  };
  if (configured) initialize(); else modes();
  auto fail = [&](const std::string &error) {
    install_active = false;
    plan_active = false;
    local_run = false;
    view.progress = -1;
    muse_fail(&gate);
    view.screen = muse::Screen::error;
    view.detail = error;
    view.status = view.local ? "Details stay in your local journal. Open the library to recover saved work."
                             : "Details are recorded in your journal. Pending logs retry automatically.";
  };
  auto send = [&](muse::Task t) {
    muse::Work w;
    w.type = t;
    w.generation = gate.generation;
    w.id = gate.plan_id;
    w.revision = gate.revision;
    w.text = gate.text;
    w.max_attempts = attempt_budgets[view.budget_choice];
    w.max_seconds = time_budgets[view.budget_choice];
    return worker_started && worker.submit(w);
  };
  auto local_send = [&](muse::Task task) {
    muse::Work w;
    w.type = task; w.generation = gate.generation;
    w.id = local_job.id; w.revision = local_job.revision;
    return worker_started && worker.submit(w);
  };
  auto show_plan = [&](const muse_plan &plan) {
    if (muse_set_plan(&gate, plan.id, plan.revision, plan.text)) {
      fail("The plan is invalid or stale."); return;
    }
    view.screen = muse::Screen::plan; view.plan = gate.text; view.page = 0;
    plan_active = plan_recovery = false; waiting_plan.clear();
    auto lines = ui::paragraph_lines(fonts.regular, view.plan, 32, 1090, 999);
    view.page_count = std::max(1, int((lines.lines.size()+7)/8));
    plan_read = view.page_count == 1;
    worker.set_context("", "", ""); refining=false; pending_speech = gate.text;
    pending_speech_path = plan.reply_wav; pad.rumble(.18f,.12f);
  };
  auto wait_plan = [&](const muse_plan &plan) {
    if (!plan.id[0] || gate.state != MUSE_IDLE) {
      fail("The pending plan is invalid or stale."); return;
    }
    if (!plan_active || waiting_plan != plan.id) plan_wait_started = view.seconds;
    waiting_plan = plan.id; plan_active = true; plan_recovery = false;
    plan_poll_at = view.seconds + 2;
    view.screen = muse::Screen::planning;
    view.plan.clear(); plan_read = false;
    if (plan.transcript[0]) view.transcript = plan.transcript;
    view.detail = "Muse is preparing your plan through the Opti3 relay.";
    view.status = plan.detail[0] ? std::string(plan.detail) : "Waiting for Muse. Nothing builds until you review and approve the plan.";
  };
  auto show_project = [&](const muse_local_job &job) {
    local_job = job;
    view.job_label = job.title;
    view.job_budget = "Attempts " + std::to_string(job.attempts) + "/" + std::to_string(job.max_attempts) +
      "  |  Active time " + std::to_string(job.seconds_used) + "/" + std::to_string(job.max_seconds) + " seconds";
    view.progress = -1;
    if (job.state == MUSE_LOCAL_AWAITING_APPROVAL) {
      muse_plan p{};
      snprintf(p.id,sizeof p.id,"%s",job.id); snprintf(p.revision,sizeof p.revision,"%s",job.revision);
      snprintf(p.text,sizeof p.text,"%s",job.plan); show_plan(p);
    } else {
      view.screen = muse::Screen::project;
      view.detail = job.detail;
      view.status = job.state == MUSE_LOCAL_READY ? "Validated locally. Press X to play this saved revision."
        : job.state == MUSE_LOCAL_QUEUED ? "Working locally. Square pauses after the active decode."
        : job.state == MUSE_LOCAL_PAUSED || job.state == MUSE_LOCAL_INTERRUPTED ? "Press X to resume this approved project."
        : "Saved on your console. Open the library to review another project.";
    }
  };
  auto apply_settings = [&]() {
    if (muse_config_validate(&pending_config, config_error, sizeof config_error)) {
      view.error = config_error; return;
    }
    if (worker_started) {
      if (phone) { muse_phone_stop(phone); phone = nullptr; }
      muse::Work w; w.type=muse::Task::configure; w.generation=gate.generation; w.configuration=pending_config;
      if (!worker.submit(w)) { view.error="Pause the active job before switching modes."; return; }
      view.screen=muse::Screen::working; view.detail="Applying your creation mode.";
    } else if (muse_config_save(config_path.c_str(), &pending_config, config_error, sizeof config_error)) {
      view.error=config_error;
    } else {
      cfg=pending_config; configured=true; initialize();
      if (worker_started) { view.screen=(open_model_setup || (view.local && !cfg.coding_model_path[0])) ? muse::Screen::models : muse::Screen::home; view.detail=home_detail(); open_model_setup=false; }
    }
  };
  auto last = sys::monotonic_us();
  bool first = true;
  unsigned frames = 0;
  double fps_seconds = 0;
  for (;;) {
    auto now = sys::monotonic_us();
    double elapsed = double(now - last) / 1000000.;
    float dt = std::clamp(float(elapsed), .001f, .1f);
    last = now;
    view.seconds += dt;
    frames++;
    fps_seconds += elapsed;
    if (fps_seconds > .5) {
      view.fps = float(frames / fps_seconds);
      frames = 0;
      fps_seconds = 0;
    }
    std::array<PadSample, 64> samples;
    auto n = pad.read(samples);
    auto input =
        tracker.update(std::span<const PadSample>(samples.data(), n), now);
    view.connected = input.connected;
    pad.tick(dt);
    view.busy = worker_started && worker.busy();
    view.recording = worker.recording.load();
    view.meter = worker.meter.load();
    if (view.recording &&
        (!input.is_held(Action::jump_next) || input.focus_lost))
      worker.recording.store(false);
    worker.phone_generation.store(0);
    if (worker_started) {
      muse::Result res;
      if (worker.take(res)) {
        if (!res.backend.empty()) view.model = res.backend;
        if (!res.speech_backend.empty()) view.speech = res.speech_backend;
        if (res.work.type == muse::Task::stop && quit_after_flush) {
          if (phone)
            muse_phone_stop(phone);
          worker.join();
          audio_out.stop();
          pad.close();
          display.close();
          sys::quit();
        }
        if (res.work.type == muse::Task::flush ||
            res.work.type == muse::Task::retry) {
          view.status = view.local ? "Journal saved locally." : res.rc ? "Log upload pending; journal kept on console."
                               : "Session logs uploaded.";
        } else if (res.work.type == muse::Task::plan_cancel) {
          if (res.rc < 0) view.status="Request cancelled here, but its recovery record could not be cleared. Reopening may show the old plan; it still requires approval.";
        } else if (res.work.type == muse::Task::model_download) {
          view.downloading_model=false;
          view.screen=muse::Screen::models;
          if (res.rc) { view.error=res.error; view.status="Download stopped. Previous selection retained."; }
          else if (!exit_pending) {
            pending_config=cfg;
            snprintf(pending_config.coding_model_path,sizeof pending_config.coding_model_path,"%s",res.path.c_str());
            open_model_setup=true; apply_settings();
          }
        } else if (res.work.type == muse::Task::configure) {
          if (res.configuration_applied) {
            cfg=res.work.configuration; configured=true; sync_mode();
            worker.set_context("", "", "");
            muse_reset(&gate); local_job={}; local_run=false; refining=false;
            plan_active=false; waiting_plan.clear(); plan_recovery=!view.local;
            pending_speech.clear(); view.job_budget.clear();
            if (res.rc) fail(res.error);
            else if (muse_config_save(config_path.c_str(),&cfg,config_error,sizeof config_error)) {
              fail(std::string("Mode applied, but settings could not be saved: ")+config_error);
            } else {
              view.screen=(open_model_setup || (view.local && !cfg.coding_model_path[0])) ? muse::Screen::models : muse::Screen::home;
              open_model_setup=false;
              view.detail=home_detail();
              view.status=view.local?"All Local. Your requests and journals stay on this console.":"Hybrid with Muse. Local voice, companion development.";
            }
            need_init=res.needs_init;
            if (!view.local && cfg.phone_port) {
              char e[256]{}; phone=muse_phone_start(&cfg,muse::Worker::phone_submit,&worker,e,sizeof e);
              if (!phone) view.status=e;
            }
          } else fail(res.error);
        } else if (res.work.type == muse::Task::init) {
          if (res.rc) view.model = "Local engine unavailable";
          else if (res.backend.empty()) view.model = view.local ? "Local language model + Whisper tiny.en" : "Whisper tiny.en / Muse development";
          view.status = res.rc ? "Local engine unavailable; check diagnostics."
                               : "Local voice engines ready.";
          view.model_ready=view.local && cfg.coding_model_path[0] && !res.rc && !res.coding_model_unavailable;
          if (res.rc) {
            view.detail = res.error;
            view.screen = muse::Screen::error;
          } else if (view.local && !view.model_ready) {
            view.screen=muse::Screen::models;
            view.status="Local speech is ready; select a coding model for Local lab.";
            view.error=res.coding_model_unavailable ? res.error : "";
          }
        } else if (res.work.type == muse::Task::speak) {
          if (!res.rc && res.work.generation == gate.generation &&
              !exit_pending && mixer.active_voices() == 0 &&
              read_speech(res.path, speech)) {
            speech_clip = {speech.data(), speech.size() / 2};
            mixer.play_clip(&speech_clip, {});
          } else if (res.work.generation == gate.generation && !exit_pending) {
            view.status =
                "Speech unavailable; the full plan remains on screen.";
          }
        } else if (res.work.generation == gate.generation) {
          if (res.rc < 0 && res.plan_retryable && (plan_active || res.plan.id[0])) {
            if (!plan_active) wait_plan(res.plan);
            plan_poll_at = view.seconds + 5;
            view.status = "Relay unavailable. Retrying the saved request; no approval was sent.";
          } else if (res.rc < 0)
            fail(res.error);
          else
            switch (res.work.type) {
            case muse::Task::local_create:
            case muse::Task::phone:
            case muse::Task::record:
            case muse::Task::transcript:
            case muse::Task::plan_poll:
            case muse::Task::plan_recover:
              if (res.rc == MUSE_CLIENT_NO_PENDING) break;
              if (res.rc == MUSE_CLIENT_PENDING) { wait_plan(res.plan); break; }
              if (view.local && res.local_job.id[0]) show_project(res.local_job);
              else show_plan(res.plan);
              break;
            case muse::Task::local_list:
              library=res.local_jobs; view.projects.clear();
              for (const auto &j:library) view.projects.push_back({j.title,muse_local_state_name(j.state),j.detail});
              view.selection=std::clamp(view.selection,0,std::max(0,int(library.size())-1));
              view.screen=muse::Screen::library;
              break;
            case muse::Task::local_load:
            case muse::Task::local_approve:
            case muse::Task::local_step:
            case muse::Task::local_resume:
            case muse::Task::local_pause:
            case muse::Task::local_cancel: {
              show_project(res.local_job);
              if (res.work.type==muse::Task::local_approve || res.work.type==muse::Task::local_resume) {
                muse_reset(&gate); local_run=true;
              }
              if (res.local_job.state!=MUSE_LOCAL_QUEUED) local_run=false;
              if (play_pending && res.work.type==muse::Task::local_load) {
                play_pending=false;
                std::string source,error;
                char seed[17]{}; std::memcpy(seed,res.local_job.id,16);
                if (res.local_job.state!=MUSE_LOCAL_READY || strcmp(res.local_job.approved_revision,res.local_job.revision) ||
                    !save::read_file(res.path,&source,65536) ||
                    !player.open(source,res.local_job.validated_sha,res.path+".state",std::strtoull(seed,nullptr,16),error)) {
                  fail(error.empty()?"The app must have a validated, approved source revision before playback.":error);
                } else { mixer.stop_all(); pending_speech.clear(); }
              }
              break;
            }
            case muse::Task::decision:
              if (plan_read && gate.voice_verified &&
                  muse_voice_utterance(&gate, res.work.generation,
                                       res.intent.c_str(),
                                       res.transcript.c_str()) >= 0) {
                if (gate.state == MUSE_APPROVING) {
                  if (!(view.local ? local_send(muse::Task::local_approve) : send(muse::Task::approve)))
                    fail("Approval could not be queued. Please request the "
                         "plan again.");
                } else if (gate.state == MUSE_IDLE) {
                  if (view.local && res.intent == "cancel" && local_job.state == MUSE_LOCAL_AWAITING_APPROVAL) {
                    cancelled_plan={}; cancelled_plan.type=muse::Task::local_cancel;
                    cancelled_plan.id=local_job.id; cancelled_plan.revision=local_job.revision;
                    cancelled_plan.generation=res.work.generation; cancel_pending=true;
                  } else if (!view.local && res.intent == "cancel") {
                    cancelled_plan={}; cancelled_plan.type=muse::Task::plan_cancel;
                    cancelled_plan.id=res.work.id; cancelled_plan.generation=res.work.generation;
                    cancel_pending=true;
                  }
                  if (res.intent == "add_detail") {
                    worker.set_context(res.work.id, res.work.revision,
                                       res.work.text);
                    refining=true;
                  } else {
                    worker.set_context("", "", "");
                    refining=false;
                  }
                  view.screen = muse::Screen::home;
                  view.detail = "Hold R2 to describe your next request.";
                  session_flush = true;
                } else
                  view.status = "Say approve this plan, or press X to approve.";
              } else
                view.status = "Voice intent: " + res.intent +
                              ". Use X, Square, or Circle to confirm.";
              break;
            case muse::Task::approve:
              if (muse_job(&gate, res.work.generation, res.job.c_str()) == 0) {
                view.screen = muse::Screen::working;
                view.detail = "Muse is compiling your approved plan.";
                poll_at = view.seconds;
              }
              break;
            case muse::Task::poll:
              if (!strcmp(res.status.state, "done")) {
                if (muse_build_done(&gate, res.work.generation, gate.job_id) ==
                    0) {
                  muse::Work w;
                  w.type = muse::Task::download;
                  w.generation = gate.generation;
                  w.id = gate.job_id;
                  w.manifest = res.status;
                  if (!worker.submit(w))
                    fail("Download could not be queued.");
                  else
                    view.detail = "Downloading and checking your build.";
                }
              } else if (!strcmp(res.status.state, "error"))
                fail(res.status.detail);
              else
                poll_at = view.seconds + 1.5;
              break;
            case muse::Task::download:
              if (muse_artifact_verified(&gate, res.work.generation) == 0) {
                artifact = res.path;
                const bool package = !strcmp(res.work.manifest.kind, "pkg");
                muse::Work w;
                w.type = package ? muse::Task::install_start : muse::Task::dispatch;
                w.generation = gate.generation;
                w.id = gate.job_id;
                w.path = artifact;
                w.manifest = res.work.manifest;
                if (muse_dispatch(&gate) == 0) {
                  if (!worker.submit(w))
                    fail("Your build could not be queued for installation or launch.");
                  else if (package) {
                    install_active = true;
                    install_launch_queued = false;
                    view.progress = 0;
                    view.detail = "Installing your verified homebrew package.";
                    view.status = "Keep MuseStation open until package installation completes.";
                  } else
                    view.detail = "Sending your build to the console's ELF loader.";
                }
              }
              break;
            case muse::Task::install_start:
            case muse::Task::install_poll:
            case muse::Task::launch:
              view.progress = std::clamp(res.install.progress_percent, 0, 100);
              if (res.install.running_observed) {
                install_active = false;
                view.progress = -1;
                view.screen = muse::Screen::home;
                view.detail = "Your app is available and its running title was confirmed.";
                view.status = "Ready for your next idea.";
                muse_reset(&gate);
                pad.rumble(.3f, .18f);
              } else if (res.install.completed) {
                view.detail = res.install.launch_requested
                    ? "Installation completed. Waiting for the title to start."
                    : "Installation completed. Starting your app.";
                view.status = res.install.detail;
                if (!install_launch_queued && !res.install.launch_requested) {
                  if (send(muse::Task::launch)) install_launch_queued = true;
                  else fail("Your app is ready, but launch could not be queued.");
                }
                install_poll_at = view.seconds + 1;
              } else if (res.install.installed) {
                view.detail = "The app is playable, but installation is still finishing.";
                view.status = "Keep MuseStation open. Launch waits for the complete package.";
                install_poll_at = view.seconds + 1;
              } else {
                view.detail = res.install.submitted
                    ? "Installing your approved app."
                    : "Preparing the console's package installer.";
                view.status = res.install.detail;
                install_poll_at = view.seconds + 1;
              }
              break;
            case muse::Task::dispatch:
              view.screen = muse::Screen::home;
              view.detail = "Build sent to the ELF loader. Check the console "
                            "for the launched app.";
              view.status = "Transfer complete. Runtime success is not "
                            "acknowledged by the loader.";
              muse_reset(&gate);
              pad.rumble(.3f, .18f);
              break;
            default:
              break;
            }
        }
      }
    }
    view.busy = worker_started && worker.busy();

    if (player.active()) {
      std::string error;
      if (input.is_held(Action::page_prev) && input.is_pressed(Action::page_next)) {
        const bool saved=player.save(error); player.close();
        if (!saved) fail(error); else show_project(local_job);
        muse::render(renderer, fonts, view, display.width(), display.height());
        if (!display.swap()) sys::park();
        continue;
      } else if (!player.frame(input.focus_lost || !input.connected ? 0 : dt,input,error)) {
        player.close(); fail("App stopped: " + error);
        muse::Work w; w.type=muse::Task::flush; w.generation=gate.generation; w.text="Local app runtime error: "+error;
        worker.submit(w);
      } else {
        player.render(renderer,fonts,display.width(),display.height());
        if (!display.swap()) sys::park();
        continue;
      }
    }
    if (need_init && !view.busy) {
      send(muse::Task::init);
      need_init = false;
    }
    if (cancel_pending && !view.busy) {
      if (worker.submit(cancelled_plan)) cancel_pending=false;
    } else if (exit_pending && !view.busy && !quit_after_flush) {
      if (!worker_started)
        sys::quit();
      quit_after_flush = send(muse::Task::stop);
    } else if (library_pending && !view.busy) {
      muse::Work w; w.type=muse::Task::local_list; w.generation=gate.generation;
      if (worker.submit(w)) library_pending=false;
    } else if (local_run && view.local && !view.busy && local_job.state==MUSE_LOCAL_QUEUED) {
      if (local_send(muse::Task::local_step)) {
        view.detail=local_job.next_phase==MUSE_LOCAL_GENERATE ? "Writing and repairing your app on this console." : "Checking the generated app with simulated input.";
        view.status="Square pauses. Keep MuseStation open; work checkpoints between steps.";
      }
    } else if (session_flush && !view.busy) {
      muse::Work w;
      w.type = muse::Task::flush;
      w.generation = gate.generation;
      w.text = "local workflow cancelled; server jobs may continue";
      if (worker.submit(w))
        session_flush = false;
    } else if (plan_recovery && !view.local && !view.busy) {
      if (send(muse::Task::plan_recover)) plan_recovery=false;
    } else if (plan_active && !view.busy && view.seconds >= plan_poll_at) {
      if (view.seconds - plan_wait_started >= 1800) {
        fail("Muse has not returned a plan within 30 minutes. The request ID is saved; reopen MuseStation to resume checking. No build was approved.");
      } else {
        muse::Work w; w.type=muse::Task::plan_poll; w.generation=gate.generation; w.id=waiting_plan;
        if (worker.submit(w)) plan_poll_at=view.seconds+2;
      }
    } else if (install_active && !view.busy && view.seconds >= install_poll_at) {
      if (!send(muse::Task::install_poll))
        fail("Install monitoring could not be queued. An accepted installation may continue.");
    } else if (gate.state == MUSE_BUILDING && !view.busy &&
               view.seconds >= poll_at) {
      muse::Work w;
      w.type = muse::Task::poll;
      w.generation = gate.generation;
      w.id = gate.job_id;
      worker.submit(w);
    } else if (!pending_speech.empty() && !view.busy &&
               mixer.active_voices() == 0) {
      muse::Work w;
      w.type = muse::Task::speak;
      w.generation = gate.generation;
      w.text = pending_speech;
      w.path = pending_speech_path;
      worker.submit(w);
      pending_speech.clear();
    }
    if (worker_started && !view.local && !worker.busy() && view.seconds >= log_retry_at) {
      if (send(muse::Task::retry))
        log_retry_at = view.seconds + 30;
    }
    view.busy = worker_started && worker.busy();
    if (input.is_held(Action::page_prev) &&
        input.is_pressed(Action::page_next)) {
      exit_pending = true;
      worker.model_transfer.cancel=true;
      local_run = false; worker.request_pause();
      worker.recording.store(false);
    }
    if (!exit_pending) {
    if (view.screen == muse::Screen::modes) {
      if (input.nav==Direction::left) view.mode_choice=(view.mode_choice+2)%3;
      if (input.nav==Direction::right) view.mode_choice=(view.mode_choice+1)%3;
      if (view.mode_choice==1 && input.nav==Direction::up) view.budget_choice=(view.budget_choice+2)%3;
      if (view.mode_choice==1 && input.nav==Direction::down) view.budget_choice=(view.budget_choice+1)%3;
      if (input.is_pressed(Action::confirm) && !view.busy) {
        if (view.mode_choice!=1) {
          view.connection_muse=view.mode_choice==0;
          view.screen=muse::Screen::connections;
        } else {
          pending_config=cfg;
          snprintf(pending_config.build_mode,sizeof pending_config.build_mode,"local");
          snprintf(pending_config.hybrid_provider,sizeof pending_config.hybrid_provider,"muse");
          pending_config.phone_port=0;
          open_model_setup=true;
          if (configured && !muse_config_validate(&pending_config,config_error,sizeof config_error)) apply_settings();
          else setup();
        }
      }
      if (input.is_pressed(Action::back) && configured) view.screen=muse::Screen::home;
    } else if (view.screen==muse::Screen::models || view.screen==muse::Screen::model_url) {
      if (view.downloading_model) {
        const uint64_t total=worker.model_transfer.total.load(),received=worker.model_transfer.received.load();
        view.status=total ? "Downloaded "+std::to_string(received/1048576)+" / "+std::to_string(total/1048576)+" MiB" : "Checking model metadata over HTTPS...";
        if(input.is_pressed(Action::back)) {worker.model_transfer.cancel=true;view.status="Cancelling; waiting for the active network read.";}
      } else if (!view.busy) {
        bool start=false;std::string url;
        if(view.screen==muse::Screen::models) {
          if(input.is_pressed(Action::confirm)){url=muse::recommended_model_url();start=true;}
          if(input.is_pressed(Action::west)){view.screen=muse::Screen::model_url;view.field="Hugging Face GGUF file URL";view.edit="https://huggingface.co/";view.error.clear();view.key=0;view.secret_edit=false;}
          if(input.is_pressed(Action::page_next)&&view.model_ready){view.screen=muse::Screen::home;view.detail=home_detail();}
          if(input.is_pressed(Action::back))modes();
        } else {
          const int count=int(strlen(muse::keyboard_keys()));
          if(input.nav==Direction::left)view.key=(view.key+count-1)%count;
          if(input.nav==Direction::right)view.key=(view.key+1)%count;
          if(input.nav==Direction::up)view.key=(view.key+count-12)%count;
          if(input.nav==Direction::down)view.key=(view.key+12)%count;
          if(input.is_pressed(Action::confirm)&&view.edit.size()<2048)view.edit+=muse::keyboard_keys()[view.key];
          if(input.is_pressed(Action::west)&&!view.edit.empty())view.edit.pop_back();
          if(input.is_pressed(Action::back))view.screen=muse::Screen::models;
          if(input.is_pressed(Action::page_next)){url=view.edit;start=true;}
        }
        if(start){
          std::string normalized;
          if(!muse::model_hf_url(url,normalized))view.error="Enter a public Hugging Face GGUF file link, including its filename.";
          else {
            muse::Work w;w.type=muse::Task::model_download;w.generation=gate.generation;w.text=normalized;
            worker.model_transfer.cancel=false;worker.model_transfer.received=0;worker.model_transfer.total=0;
            if(worker.submit(w)){view.screen=muse::Screen::models;view.downloading_model=true;view.error.clear();view.status="Connecting securely to Hugging Face...";}
          }
        }
      }
    } else if (view.screen==muse::Screen::connections) {
      // The endpoint is the user's Opti3 relay; this is not account pairing.
      if (view.connection_muse && (input.is_pressed(Action::confirm) || input.is_pressed(Action::west)) && !view.busy) {
        pending_config=cfg;
        snprintf(pending_config.build_mode,sizeof pending_config.build_mode,"hybrid");
        snprintf(pending_config.hybrid_provider,sizeof pending_config.hybrid_provider,"muse");
        setup();
      }
      if (input.is_pressed(Action::back)) modes();
    } else if (view.screen==muse::Screen::library) {
      if (input.nav==Direction::up) view.selection=std::max(0,view.selection-1);
      if (input.nav==Direction::down) view.selection=std::min(std::max(0,int(library.size())-1),view.selection+1);
      if (input.is_pressed(Action::confirm) && !view.busy && !library.empty()) {
        worker.set_context("","",""); refining=false;
        local_job=library[view.selection]; play_pending=false;
        if (!local_send(muse::Task::local_load)) fail("The project could not be opened.");
      }
      if (input.is_pressed(Action::back)) {
        view.screen=muse::Screen::home; muse_reset(&gate);
        worker.set_context("","",""); refining=false;
      }
      if (input.is_pressed(Action::menu) && !view.busy) modes();
    } else if (view.screen == muse::Screen::setup || view.screen==muse::Screen::request) {
      const bool typed=view.screen==muse::Screen::request;
      if(typed)view.secret_edit=false;
      const int index=typed?0:setup_fields[view.field_index];
      const size_t capacity=typed?1024:field_cap(index);
      int count = int(strlen(muse::keyboard_keys()));
      if (input.nav == Direction::left) view.key = (view.key + count - 1) % count;
      if (input.nav == Direction::right) view.key = (view.key + 1) % count;
      if (input.nav == Direction::up) view.key = (view.key + count - 12) % count;
      if (input.nav == Direction::down) view.key = (view.key + 12) % count;
      if (input.is_pressed(Action::confirm) && view.edit.size()+1<capacity)
        view.edit += muse::keyboard_keys()[view.key];
      if (input.is_pressed(Action::west) && !view.edit.empty()) view.edit.pop_back();
      if (input.is_pressed(Action::back)) {
        if (typed) {
          view.screen=muse::Screen::home; refining=false;
          worker.set_context("","",""); muse_reset(&gate);
        }
        else if (view.field_index>0) {
          --view.field_index; view.edit=field(pending_config,setup_fields[view.field_index]);
          view.field=fields[setup_fields[view.field_index]];
          view.secret_edit=setup_fields[view.field_index]==5;
        } else modes();
      }
      if (input.is_pressed(Action::page_next) && !view.busy) {
        if (typed) {
          if (view.edit.find_first_not_of(" ")==std::string::npos) view.error="Describe the app first.";
          else {
            muse::Work w; w.type=view.local ? muse::Task::local_create : muse::Task::transcript; w.generation=gate.generation;
            w.text=view.edit; w.max_attempts=attempt_budgets[view.budget_choice]; w.max_seconds=time_budgets[view.budget_choice];
            if (worker.submit(w)) { view.screen=muse::Screen::working; view.detail=view.local ? "Preparing a plan on your console." : "Sending your request to the Opti3 relay."; }
          }
        } else {
          snprintf(field(pending_config,index),field_cap(index),"%s",view.edit.c_str());
          if (view.field_index+1<int(setup_fields.size())) {
            ++view.field_index; view.edit=field(pending_config,setup_fields[view.field_index]);
            view.field=fields[setup_fields[view.field_index]];
          view.secret_edit=setup_fields[view.field_index]==5;
          } else apply_settings();
        }
      }
    } else {
      if (input.is_pressed(Action::menu) && !view.busy && !install_active && !local_run && !plan_active) modes();
      if (view.local && view.screen==muse::Screen::project && input.is_pressed(Action::west)) {
        local_run=false;
        if (view.busy) worker.request_pause();
        else if (local_job.state==MUSE_LOCAL_QUEUED) local_send(muse::Task::local_pause);
        else {
          worker.set_context(local_job.id,local_job.revision,local_job.plan);
          refining=true;
          muse_reset(&gate); view.screen=muse::Screen::request;
          view.field="How should it change?"; view.edit.clear(); view.error.clear(); view.key=0;
        }
        if (view.screen==muse::Screen::project)
          view.status="Pause requested; the current decode must finish first.";
      }
      if (view.local && view.screen==muse::Screen::project && input.is_pressed(Action::confirm) && !view.busy) {
        if (local_job.state==MUSE_LOCAL_READY) {
          play_pending=true; if (!local_send(muse::Task::local_load)) play_pending=false;
        } else if (local_job.state==MUSE_LOCAL_PAUSED || local_job.state==MUSE_LOCAL_INTERRUPTED) {
          if (!local_send(muse::Task::local_resume)) fail("Resume could not be queued.");
        }
      }
      if (view.screen==muse::Screen::home && input.is_pressed(Action::confirm) && !view.busy && configured && !plan_active) {
        muse_reset(&gate); view.screen=muse::Screen::request;
        view.field=refining?"How should it change?":""; view.edit.clear(); view.error.clear(); view.key=0;
      }
      if (input.is_pressed(Action::back)) {
        if (!view.local && gate.state != MUSE_DISPATCHED) {
          cancelled_plan={}; cancelled_plan.type=muse::Task::plan_cancel;
          cancelled_plan.generation=gate.generation; cancel_pending=true;
          plan_active=plan_recovery=false; waiting_plan.clear();
        }
        if (view.local) {
          local_run=false;
          if (view.screen==muse::Screen::plan && local_job.state==MUSE_LOCAL_AWAITING_APPROVAL) {
            cancelled_plan={}; cancelled_plan.type=muse::Task::local_cancel;
            cancelled_plan.id=local_job.id; cancelled_plan.revision=local_job.revision;
            cancelled_plan.generation=gate.generation; cancel_pending=true;
          }
          if (view.busy) worker.request_pause();
          else if (local_job.id[0] && local_job.state==MUSE_LOCAL_QUEUED) local_send(muse::Task::local_pause);
        }
        mixer.stop_all();
        worker.recording.store(false);
        pending_speech.clear();
        if (muse_input(&gate, MUSE_CIRCLE) == 0) {
          worker.set_context("", "", "");
          refining=false;
          session_flush = true;
          view.progress = -1;
          view.screen = muse::Screen::home;
          view.detail = home_detail();
          view.status = view.local ? cancel_pending ? "Plan cancelled. Its saved record stays in your library."
                                                    : "Saved projects remain in your library. Active work was asked to pause."
                                   : "Local workflow cancelled. An accepted server build may continue.";
        } else
          view.status =
              install_active ? "Keep MuseStation open; closing it may interrupt package installation."
                             : "Already sent to the loader; execution cannot be cancelled.";
      }
      if (input.is_pressed(Action::west) && gate.state == MUSE_PLAN) {
        pending_speech.clear();
        worker.set_context(gate.plan_id, gate.revision, gate.text);
        refining=true;
        muse_input(&gate, MUSE_SQUARE);
        view.screen = muse::Screen::home;
        view.detail = "Hold R2 and describe your revised request.";
      }
      if (view.screen == muse::Screen::plan) {
        if (input.nav == Direction::down || input.nav == Direction::right)
          view.page = std::min(view.page + 1, view.page_count - 1);
        if (input.nav == Direction::up || input.nav == Direction::left)
          view.page = std::max(0, view.page - 1);
        if (view.page == view.page_count - 1)
          plan_read = true;
        if (!view.busy && plan_read &&
            muse_pad(&gate, input.is_held(Action::confirm)) == 1) {
          if (view.local ? local_send(muse::Task::local_approve) : send(muse::Task::approve)) {
            view.screen = muse::Screen::working;
            view.detail = view.local ? "Saving your approval before starting local work." : "Checking the console title inventory before sending approval.";
          } else
            fail(
                "Approval could not be queued. Please request the plan again.");
        }
      }
      if (input.is_pressed(Action::jump_next) && !view.busy && configured && !plan_active && !cancel_pending &&
          (gate.state == MUSE_IDLE || gate.state == MUSE_PLAN ||
           gate.state == MUSE_ERROR)) {
        if (gate.state == MUSE_ERROR)
          muse_reset(&gate);
        mixer.stop_all();
        worker.recording.store(true);
        if (!send(gate.state == MUSE_PLAN ? muse::Task::decision
                                          : muse::Task::record))
          worker.recording.store(false);
        view.status = "Release R2 to finish speaking.";
        if (gate.state != MUSE_PLAN) {
          view.screen = muse::Screen::home;
          view.detail = "I'm listening. Describe your idea.";
        }
      }
      if (input.is_pressed(Action::north) && view.local) {
        local_run=false; library_pending=true; pending_speech.clear();
        worker.set_context("","",""); refining=false;
        worker.request_pause();
        if (!view.busy && local_job.id[0] && local_job.state==MUSE_LOCAL_QUEUED)
          local_send(muse::Task::local_pause);
        muse_reset(&gate);
        view.status="Opening your saved local projects.";
      } else if (input.is_pressed(Action::north)) {
        view.detail = phone
                          ? "Phone mic: http://" + std::string(cfg.console_ip) +
                                ":" + std::to_string(cfg.phone_port) +
                                "  Pairing code: " + muse_phone_token(phone)
                          : "Phone listener is unavailable. Check its port in "
                            "diagnostics.";
        view.status = "WAV upload works on LAN HTTP. Hold-to-talk needs a "
                      "trusted HTTPS proxy.";
      }
    }
    }
    worker.phone_generation.store(configured && !view.local && gate.state == MUSE_IDLE &&
                                          view.screen == muse::Screen::home && !plan_active && !plan_recovery && !cancel_pending &&
                                          !exit_pending
                                      ? gate.generation
                                      : 0);
    pad.set_light_bar(view.recording ? 240 : 80, view.recording ? 150 : 220,
                      view.recording ? 80 : 195);
    muse::render(renderer, fonts, view, display.width(), display.height());
    if (!display.swap()) {
      sys::log("Muse: display swap failed");
      worker.recording.store(false);
      send(muse::Task::flush);
      sys::park();
    }
    if (first) {
      sys::hide_splash_screen();
      first = false;
    }
  }
}
