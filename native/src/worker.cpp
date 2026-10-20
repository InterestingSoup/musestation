// SPDX-License-Identifier: GPL-3.0-or-later
#include "worker.hpp"
#include "local_app.h"
#include "platform/ps5/system.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <time.h>
#include <vector>
#ifdef MUSE_LOCAL_INFERENCE
#include "muse_inference.h"
#endif
extern "C" int sceUserServiceGetInitialUser(int *);
namespace muse {
namespace {
const char *hybrid_voice_intent(const char *utterance) {
  if (!utterance) return "unknown";
  char normalized[65]{}; size_t length = 0; bool space = false;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(utterance); *p; ++p) {
    const unsigned char c = *p;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
      if (length) space = true;
      continue;
    }
    if (c < 32 || c >= 128) return "unknown";
    if (space) {
      if (length + 1 >= sizeof normalized) return "unknown";
      normalized[length++] = ' '; space = false;
    }
    if (length + 1 >= sizeof normalized) return "unknown";
    normalized[length++] = char(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
  }
  if (length && (normalized[length - 1] == '.' || normalized[length - 1] == '!')) --length;
  if (length && normalized[length - 1] == ' ') --length;
  normalized[length] = 0;
  if (!std::strcmp(normalized, "approve") || !std::strcmp(normalized, "approve this plan") ||
      !std::strcmp(normalized, "build it")) return "approve";
  if (!std::strcmp(normalized, "add detail")) return "add_detail";
  if (!std::strcmp(normalized, "cancel")) return "cancel";
  return "unknown";
}
[[maybe_unused]] std::string status_field(const std::string &status, const std::string &tag) {
  size_t start = status.find(tag);
  if (start == std::string::npos) return "unreported";
  start += tag.size();
  size_t end = status.find(';', start);
  return status.substr(start, end == std::string::npos ? end : end - start);
}
void le16(unsigned char *p, unsigned x) {
  p[0] = x;
  p[1] = x >> 8;
}
void le32(unsigned char *p, unsigned x) {
  le16(p, x);
  le16(p + 2, x >> 16);
}
bool wav(const std::string &path, const std::vector<int16_t> &pcm) {
  unsigned char h[44]{};
  memcpy(h, "RIFF", 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  le32(h + 4, unsigned(pcm.size() * 2 + 36));
  le32(h + 16, 16);
  le16(h + 20, 1);
  le16(h + 22, 1);
  le32(h + 24, 48000);
  le32(h + 28, 96000);
  le16(h + 32, 2);
  le16(h + 34, 16);
  memcpy(h + 36, "data", 4);
  le32(h + 40, unsigned(pcm.size() * 2));
  FILE *f = fopen(path.c_str(), "wb");
  if (!f)
    return false;
  bool ok = fwrite(h, 1, 44, f) == 44 &&
            fwrite(pcm.data(), 2, pcm.size(), f) == pcm.size();
  return fclose(f) == 0 && ok;
}
bool read_pcm(const std::string &path, std::vector<int16_t> &pcm, int &rate,
              std::string &error) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) {
    error = "Cannot open phone recording.";
    return false;
  }
  std::vector<unsigned char> data(8 * 1024 * 1024 + 1);
  size_t n = fread(data.data(), 1, data.size(), f);
  bool bad = ferror(f);
  fclose(f);
  data.resize(n);
  auto u16 = [&](size_t p) {
    return unsigned(data[p]) | unsigned(data[p + 1]) << 8;
  };
  auto u32 = [&](size_t p) { return u16(p) | u16(p + 2) << 16; };
  if (bad || n < 44 || n > 8 * 1024 * 1024 || memcmp(data.data(), "RIFF", 4) ||
      memcmp(data.data() + 8, "WAVE", 4)) {
    error = "Phone audio must be a valid WAV recording.";
    return false;
  }
  size_t start = 0, length = 0;
  unsigned channels = 0, bits = 0, format = 0;
  for (size_t at = 12; at + 8 <= n;) {
    size_t len = u32(at + 4);
    if (len > n - at - 8) {
      error = "Truncated WAV chunk.";
      return false;
    }
    if (!memcmp(data.data() + at, "fmt ", 4) && len >= 16) {
      format = u16(at + 8);
      channels = u16(at + 10);
      rate = int(u32(at + 12));
      bits = u16(at + 22);
    }
    if (!memcmp(data.data() + at, "data", 4)) {
      start = at + 8;
      length = len;
    }
    at += 8 + len + (len & 1);
  }
  if (!start || format != 1 || channels != 1 || bits != 16 || rate < 8000 ||
      rate > 96000 || length % 2 || length / 2 < size_t(rate / 10) ||
      length / 2 > size_t(rate) * 60) {
    error = "Use 0.1–60 seconds of mono PCM16 WAV audio.";
    return false;
  }
  pcm.resize(length / 2);
  for (size_t i = 0; i < pcm.size(); ++i)
    pcm[i] = int16_t(u16(start + i * 2));
  return true;
}
int capture(const char *module, std::atomic<bool> &held,
            std::atomic<float> &meter, std::vector<int16_t> &pcm,
            std::string &error) {
  if (!module || !*module) {
    error = "Set the AudioIn module path in settings to use the DualSense microphone; typed requests remain available.";
    return -1;
  }
  void *lib = dlopen(module, RTLD_NOW);
  if (!lib) {
    error = "AudioIn module unavailable. Check its path in settings, or use "
            "the phone microphone.";
    return -1;
  }
  auto init = reinterpret_cast<int (*)(void)>(dlsym(lib, "sceAudioInInit"));
  auto open_in =
      reinterpret_cast<int (*)(int, int, int, unsigned, unsigned, unsigned)>(
          dlsym(lib, "sceAudioInOpen"));
  auto input =
      reinterpret_cast<int (*)(int, void *)>(dlsym(lib, "sceAudioInInput"));
  auto close_in = reinterpret_cast<int (*)(int)>(dlsym(lib, "sceAudioInClose"));
  auto silent =
      reinterpret_cast<int (*)(int)>(dlsym(lib, "sceAudioInGetSilentState"));
  int handle = -1, rc = -1, user = -1;
  if (!init || !open_in || !input || !close_in || !silent) {
    error = "AudioIn module is missing required capture functions.";
    goto done;
  }
  rc = init();
  if (rc < 0 && uint32_t(rc) != 0x8026000c && uint32_t(rc) != 0x8026000e) {
    error = "AudioIn initialization failed.";
    rc = -1;
    goto done;
  }
  if (sceUserServiceGetInitialUser(&user) < 0) {
    error = "No initial console user is signed in.";
    rc = -1;
    goto done;
  }
  handle = open_in(user, 0, 0, 256, 48000, 0);
  if (handle < 0)
    handle = open_in(user, 1, 0, 256, 48000, 0);
  if (handle < 0) {
    error = "The DualSense microphone could not be opened.";
    rc = -1;
    goto done;
  }
  pcm.reserve(48000 * 60);
  rc = 0;
  while (held.load() && pcm.size() < 48000 * 60) {
    int16_t block[256];
    int state = silent(handle);
    if (state < 0 || (state & 5)) {
      error = "Microphone is muted or unavailable. Unmute the DualSense and "
              "try again.";
      rc = -1;
      break;
    }
    if (input(handle, block) < 0) {
      error = "AudioIn capture failed.";
      rc = -1;
      break;
    }
    float peak = 0;
    for (auto s : block)
      peak = std::fmax(peak, std::fabs(float(s)) / 32768);
    meter.store(peak);
    pcm.insert(pcm.end(), block, block + 256);
  }
  if (!rc && pcm.size() < 4800) {
    error = "Hold R2 a little longer, then speak.";
    rc = -1;
  }
done:
  if (handle >= 0)
    close_in(handle);
  dlclose(lib);
  meter.store(0);
  return rc;
}
} // namespace
int Worker::phone_submit(void *context, const char *path) {
  auto *self = static_cast<Worker *>(context);
  uint64_t generation = self->phone_generation.load();
  if (!generation)
    return -1;
  Work w;
  w.type = Task::phone;
  w.generation = generation;
  w.path = path;
  return self->submit(w) ? 0 : -1;
}
bool Worker::start(const muse_config &c) {
  muse_client_init(&client, &c);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 4 * 1024 * 1024);
  int rc = pthread_create(&thread, &attr, entry, this);
  pthread_attr_destroy(&attr);
  return rc == 0;
}
bool Worker::submit(const Work &w) {
  pthread_mutex_lock(&mutex);
  bool ok = !running && !pending && !ready &&
            (w.type != Task::phone || phone_generation.load() == w.generation);
  if (ok) {
    request = w;
    if (w.type == Task::record || w.type == Task::phone) {
      request.id = context_id;
      request.revision = context_revision;
      request.text = context_text;
    }
    if (w.type == Task::local_create || w.type == Task::transcript) {
      request.id = context_id;
      request.revision = context_revision;
      request.parent_plan = context_text;
    }
    pending = true;
    pthread_cond_signal(&wake);
  }
  pthread_mutex_unlock(&mutex);
  return ok;
}
void Worker::set_context(const std::string &id, const std::string &revision,
                         const std::string &text) {
  pthread_mutex_lock(&mutex);
  context_id = id;
  context_revision = revision;
  context_text = text;
  pthread_mutex_unlock(&mutex);
}
bool Worker::busy() {
  pthread_mutex_lock(&mutex);
  bool b = running || pending || ready;
  pthread_mutex_unlock(&mutex);
  return b;
}
bool Worker::take(Result &r) {
  pthread_mutex_lock(&mutex);
  bool ok = ready;
  if (ok) {
    r = result;
    ready = false;
  }
  pthread_mutex_unlock(&mutex);
  return ok;
}
void *Worker::entry(void *p) {
  static_cast<Worker *>(p)->loop();
  return nullptr;
}
int Worker::open_projects() {
  if (projects_open) return 0;
  const std::string path = std::string(client.config.artifact_dir) + "/projects";
  projects_open = muse_local_open(&projects, path.c_str()) == 0;
  return projects_open ? 0 : -1;
}
void Worker::create_local(const Work &w, Result &r, const char *request_text,
                           const char *saved_plan) {
  if (current_job.state == MUSE_LOCAL_QUEUED && current_job.id[0]) {
    r.rc = -1; r.error = "Pause the queued local job before creating another."; return;
  }
  if (open_projects()) { r.rc = -1; r.error = projects.error; return; }
  if (!w.id.empty()) {
    muse_local_job parent{};
    if (muse_local_load(&projects, w.id.c_str(), &parent, 0) ||
        w.revision != parent.revision) {
      r.rc = -1; r.error = "The saved parent plan changed or is unavailable; reopen it before adding detail."; return;
    }
  }
  if (!request_text || !*request_text || std::strlen(request_text) > 4096) {
    r.rc = -1; r.error = "Enter a local request of 1–4096 bytes."; return;
  }
  std::string plan_text = saved_plan ? saved_plan : "";
  if (plan_text.empty()) {
#ifdef MUSE_LOCAL_INFERENCE
    char draft[4097]{}, error[512]{};
    r.rc = engine ? muse_inference_draft(static_cast<muse_inference *>(engine),
                                         request_text, draft, sizeof draft,
                                         error, sizeof error) : -1;
    if (r.rc) { r.error = *error ? error : "Initialize the bundled local model before planning."; return; }
    plan_text = draft;
#else
    r.rc = -1; r.error = "This build has no bundled local planner."; return;
#endif
  }
  std::string title = w.title;
  if (title.empty()) {
    const size_t total = std::strlen(request_text);
    size_t end = total < 80 ? total : 80;
    while (end < total && end && (static_cast<unsigned char>(request_text[end]) & 0xc0) == 0x80) --end;
    title.assign(request_text, end);
    for (char &ch : title) if (static_cast<unsigned char>(ch) < 32) ch = ' ';
  }
  r.rc = muse_local_create(&projects, title.c_str(),
                            request_text, plan_text.c_str(), w.max_attempts,
                            w.max_seconds, &current_job);
  if (r.rc) { r.error = projects.error; return; }
  local_stop.store(0);
  r.local_job = current_job;
  std::snprintf(r.plan.id, sizeof r.plan.id, "%s", current_job.id);
  std::snprintf(r.plan.revision, sizeof r.plan.revision, "%s", current_job.revision);
  std::snprintf(r.plan.text, sizeof r.plan.text, "%s", current_job.plan);
  std::snprintf(r.plan.transcript, sizeof r.plan.transcript, "%s", current_job.request);
  r.transcript = current_job.request;
}
void Worker::local_action(const Work &w, Result &r) {
  if (!muse_config_local(&client.config)) {
    r.rc = -1; r.error = "Select Local mode before working on local projects."; return;
  }
  if (open_projects()) { r.rc = -1; r.error = projects.error; return; }
  if (w.type == Task::local_create) {
    const std::string request = w.parent_plan.empty() ? w.text :
        "Revise this existing plan: " + w.parent_plan + "\nUser change: " + w.text;
    create_local(w, r, request.c_str(), w.plan.c_str()); return;
  }
  if (w.type == Task::local_list) {
    r.local_jobs.resize(MUSE_LOCAL_JOBS_MAX); size_t count = 0;
    r.rc = muse_local_list(&projects, r.local_jobs.data(), r.local_jobs.size(), &count);
    r.local_jobs.resize(count);
    if (r.rc) r.error = projects.error;
    return;
  }
  if (current_job.id[0] && current_job.state == MUSE_LOCAL_QUEUED &&
      w.id != current_job.id) {
    r.rc = -1; r.error = "Pause the queued project before selecting another."; return;
  }
  r.rc = muse_local_load(&projects, w.id.c_str(), &current_job,
                          w.type == Task::local_load);
  if (r.rc) { r.error = projects.error; return; }
  switch (w.type) {
  case Task::local_load: break;
  case Task::local_approve:
    r.rc = muse_local_approve(&projects, &current_job, w.revision.c_str());
    if (!r.rc) local_stop.store(0);
    break;
  case Task::local_resume:
    r.rc = muse_local_resume(&projects, &current_job);
    if (!r.rc) local_stop.store(0);
    break;
  case Task::local_pause:
  case Task::local_cancel:
    r.rc = muse_local_pause(&projects, &current_job, w.type == Task::local_cancel);
    local_stop.store(w.type == Task::local_cancel ? 2 : 1);
    break;
  case Task::local_step: {
    if (local_stop.load()) {
      r.rc = muse_local_pause(&projects, &current_job, local_stop.load() == 2);
      break;
    }
    const std::string prior_diagnostics = current_job.detail;
    unsigned allowance = 0;
    r.rc = muse_local_begin_step(&projects, &current_job,
        current_job.next_phase == MUSE_LOCAL_GENERATE ? 1800 : 10, &allowance);
    if (r.rc) break;
    timespec start{}; clock_gettime(CLOCK_MONOTONIC, &start);
    auto elapsed = [&]() -> uint64_t {
      timespec now{}; clock_gettime(CLOCK_MONOTONIC, &now);
      int64_t ms = (int64_t(now.tv_sec) - int64_t(start.tv_sec)) * 1000 +
                   (now.tv_nsec - start.tv_nsec) / 1000000;
      return ms > 0 ? uint64_t(ms) : 0;
    };
    auto finish = [&](muse_local_state state, muse_local_phase phase,
                      const char *source, size_t length, const std::string &detail) {
      std::string bounded = detail.substr(0, sizeof current_job.detail - 1);
      // Runtime diagnostics may include truncated/raw model bytes. Preserve an
      // ASCII diagnostic so malformed UTF-8 cannot prevent a failure checkpoint.
      for (char &ch : bounded) if (static_cast<unsigned char>(ch) >= 128) ch = '?';
      r.rc = muse_local_finish_step(&projects, &current_job, state, phase,
                                     source, length, bounded.c_str(), elapsed());
    };
    std::vector<char> source(MUSE_LOCAL_SOURCE_MAX + 1, 0);
    size_t source_size = 0;
    if (current_job.candidate_sha[0] &&
        muse_local_read_source(&projects, &current_job, 0, source.data(),
                               source.size(), &source_size)) {
      finish(MUSE_LOCAL_FAILED, current_job.next_phase, nullptr, 0, projects.error);
      break;
    }
    if (current_job.state == MUSE_LOCAL_GENERATING) {
#ifdef MUSE_LOCAL_INFERENCE
      const std::string previous(source.data(), source_size);
      source.assign(source.size(), 0);
      muse_codegen_request req{};
      req.system_prompt = muse_local_app_prompt_contract();
      req.request = current_job.request;
      req.approved_plan = current_job.plan;
      req.previous_source = previous.c_str();
      req.diagnostics = prior_diagnostics.c_str();
      req.context_tokens = 8192; req.max_output_tokens = 3072;
      req.max_duration_ms = allowance * 1000;
      char seed[9]{}; std::memcpy(seed, current_job.id, 8);
      req.seed = uint32_t(std::strtoul(seed, nullptr, 16)) ^ (0x9e3779b9u * current_job.attempts);
      char seed_detail[160]{};
      std::snprintf(seed_detail, sizeof seed_detail, "job=%s attempt=%u seed=%u", current_job.id, current_job.attempts, req.seed);
      if (muse_log_event(&journal, "info", "local_generation_seed", seed_detail)) {
        finish(MUSE_LOCAL_FAILED, MUSE_LOCAL_GENERATE, nullptr, 0,
               "Cannot persist the generation seed; work stopped before inference.");
        break;
      }
      struct Progress {
        Worker *worker;
        std::string text;
        uint32_t saved_tokens = 0;
        bool failed = false;
      } progress{this, "", 0, false};
      req.should_stop = [](void *p) -> int {
        auto *v = static_cast<Progress *>(p);
        return v->failed || v->worker->local_stop.load() != 0;
      };
      req.progress = [](void *p, const char *piece, size_t bytes, uint32_t tokens) {
        auto *v = static_cast<Progress *>(p);
        if (v->failed || bytes > MUSE_LOCAL_SOURCE_MAX - v->text.size()) { v->failed = true; return; }
        v->text.append(piece, bytes);
        if (tokens - v->saved_tokens >= 512 && !v->text.empty()) {
          v->failed = muse_local_checkpoint_source(&v->worker->projects,
              &v->worker->current_job, v->text.data(), v->text.size()) != 0;
          v->saved_tokens = tokens;
        }
      };
      req.userdata = &progress;
      muse_codegen_stats stats{}; char error[1024]{};
      int generated = engine ? muse_inference_codegen(static_cast<muse_inference *>(engine),
          &req, source.data(), source.size(), &stats, error, sizeof error) : MUSE_CODEGEN_ERROR;
      source_size = std::strlen(source.data());
      const char *candidate = source_size ? source.data() : nullptr;
      if (progress.failed)
        finish(MUSE_LOCAL_FAILED, MUSE_LOCAL_GENERATE, nullptr, 0,
               "Cannot commit partial generation checkpoint; work stopped before execution.");
      else if (local_stop.load() || generated == MUSE_CODEGEN_STOPPED)
        finish(local_stop.load() == 2 ? MUSE_LOCAL_CANCELLED : MUSE_LOCAL_PAUSED,
               MUSE_LOCAL_GENERATE, candidate, source_size,
               "Generation stopped cooperatively; partial source saved, never executed.");
      else if (generated == MUSE_CODEGEN_LIMIT && !source_size &&
               std::strstr(error, "tokenized prompt plus requested output exceeds"))
        finish(MUSE_LOCAL_FAILED, MUSE_LOCAL_GENERATE, nullptr, 0, error);
      else if (generated == MUSE_CODEGEN_COMPLETE && source_size)
        finish(MUSE_LOCAL_QUEUED, MUSE_LOCAL_VALIDATE, candidate, source_size,
               "Model finished a source candidate; isolated validation is queued.");
      else
        finish(MUSE_LOCAL_QUEUED, MUSE_LOCAL_GENERATE, candidate, source_size,
               *error ? error : "Local code generation unavailable or incomplete; repair required.");
#else
      finish(MUSE_LOCAL_FAILED, MUSE_LOCAL_GENERATE, nullptr, 0,
             "This build has no bundled code generation engine.");
#endif
    } else {
      struct ValidationStop { Worker *worker; timespec start; unsigned seconds; } stop{this, start, allowance};
      muse_local_limits limits{};
      limits.cancelled = [](void *p) -> int {
        auto *v = static_cast<ValidationStop *>(p); timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return v->worker->local_stop.load() != 0 ||
               now.tv_sec - v->start.tv_sec >= v->seconds;
      };
      limits.cancel_context = &stop;
      char seed[17]{}; std::memcpy(seed, current_job.id, 16);
      limits.seed = std::strtoull(seed, nullptr, 16);
      muse_local_validation report{}; char error[1024]{};
      int validated = muse_local_app_validate(source.data(), source_size, &limits,
                                               60, &report, error, sizeof error);
      if (local_stop.load())
        finish(local_stop.load() == 2 ? MUSE_LOCAL_CANCELLED : MUSE_LOCAL_PAUSED,
               MUSE_LOCAL_VALIDATE, nullptr, 0, "Validation stopped; resume explicitly.");
      else if (!validated) {
        std::string detail = "Source passed isolated Lua validation. Press Play explicitly to run it.";
        if (report.provably_hidden_text_commands)
          detail += " Validation also found " + std::to_string(report.provably_hidden_text_commands) +
                    " hidden text draws across its probe frames; review the display and Refine if needed.";
        finish(MUSE_LOCAL_READY, MUSE_LOCAL_VALIDATE, nullptr, 0,
               detail);
      } else
        finish(MUSE_LOCAL_QUEUED, MUSE_LOCAL_GENERATE, nullptr, 0,
               *error ? error : "Lua validation failed; repair queued.");
    }
    break;
  }
  default: r.rc = -1; r.error = "Invalid local project operation."; break;
  }
  if (r.rc && r.error.empty()) r.error = projects.error;
  r.local_job = current_job;
  if (!r.rc && current_job.state == MUSE_LOCAL_READY) {
    std::vector<char> verified(MUSE_LOCAL_SOURCE_MAX + 1); size_t length = 0;
    char path[1200]{};
    if (muse_local_read_source(&projects, &current_job, 1, verified.data(),
                               verified.size(), &length) ||
        muse_local_source_path(&projects, &current_job, 1, path, sizeof path)) {
      r.rc = -1; r.error = projects.error;
    } else r.path = path;
  }
}

void Worker::loop() {
  // Journal opening can flush Hybrid logs. Keep it inside the serialized mailbox
  // so a first task selecting Local takes effect before any old-spool activity.
  bool opened = false, boot_logged = false, installer_recovered = false;
  for (;;) {
    pthread_mutex_lock(&mutex);
    while (!pending)
      pthread_cond_wait(&wake, &mutex);
    Work w = request;
    pending = false;
    running = true;
    pthread_mutex_unlock(&mutex);
    Result r;
    r.work = w;
    client.error[0] = 0;
    client.http_status = 0;
    if (w.type == Task::configure) {
      char error[256]{};
      if (muse_config_validate(&w.configuration, error, sizeof error)) {
        r.rc = -1; r.error = error;
      } else if (installer || (current_job.id[0] && current_job.state == MUSE_LOCAL_QUEUED)) {
        r.rc = -1; r.error = "Finish installation or pause local work before changing modes.";
      } else {
        const bool reload = muse_config_local(&client.config) != muse_config_local(&w.configuration) ||
            std::strcmp(client.config.model_dir, w.configuration.model_dir) ||
            std::strcmp(client.config.artifact_dir, w.configuration.artifact_dir) ||
            std::strcmp(client.config.inference_backend, w.configuration.inference_backend) ||
            std::strcmp(client.config.speech_mode, w.configuration.speech_mode);
        const bool new_store = std::strcmp(client.config.artifact_dir, w.configuration.artifact_dir);
        /* Incoming Local disables old-spool uploads before close/open can flush. */
        if (muse_config_local(&w.configuration)) std::strcpy(client.config.build_mode, "local");
        if (opened) muse_log_close(&journal);
        muse_client_init(&client, &w.configuration);
        r.configuration_applied = true;
        installer_recovered = false;
        opened = muse_log_open(&journal, &client) == 0;
        if (!opened) { r.rc = -1; r.error = "Mode applied, but its journal cannot be opened. Check writable storage."; }
        if (new_store && projects_open) {
          muse_local_close(&projects); projects_open = false; current_job = {};
        }
#ifdef MUSE_LOCAL_INFERENCE
        if (reload && engine) { muse_inference_destroy(static_cast<muse_inference *>(engine)); engine = nullptr; }
#else
        (void)reload;
#endif
        approved_generation = downloaded_generation = 0;
        approved_job.clear(); downloaded_path.clear();
        r.needs_init = engine == nullptr;
      }
    } else {
    if (!opened)
      opened = muse_log_open(&journal, &client) == 0;
    if (opened && !boot_logged) {
      boot_logged = muse_log_event(&journal, "info", "boot", "MuseStation worker started; hardware validation pending") == 0;
    }
    if (w.type != Task::stop && (!opened || muse_log_event(&journal, "info", "operation_begin",
                                  std::to_string(int(w.type)).c_str()))) {
      r.rc = -1;
      r.error = muse_config_local(&client.config)
          ? "Cannot persist the private journal. Check writable local storage."
          : (client.http_status == 401 || client.http_status == 403)
              ? "Relay console key rejected; update it in setup."
              : "Cannot persist or flush the session journal. Check log storage and the Muse server.";
      if (installer)
        r.error += " Close and reopen Muse before another installation; an "
                   "OS-accepted installation may continue.";
      if (w.type == Task::phone)
        unlink(w.path.c_str());
      if (w.type == Task::record || w.type == Task::decision)
        recording.store(false);
    } else if (w.type != Task::stop && !muse_config_local(&client.config) &&
               !installer_recovered && muse_install_recover_logs(&client)) {
      r.rc = -1;
      r.error = "Previous installer logs must reach the relay before new Hybrid work. " + std::string(client.error);
      if (w.type == Task::phone) unlink(w.path.c_str());
      if (w.type == Task::record || w.type == Task::decision) recording.store(false);
    } else {
      if (w.type != Task::stop && !muse_config_local(&client.config)) installer_recovered = true;
      switch (w.type) {
      case Task::init: {
        if (engine) break;
#ifdef MUSE_LOCAL_INFERENCE
        const std::string base = client.config.model_dir;
        const std::string llm = base + "/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
                          whisper = base + "/ggml-tiny.en.bin";
        const std::string neural = base + "/Qwen3-TTS-12Hz-0.6B-Base.Q4_K_M.gguf",
                          projector = base + "/Qwen3-TTS-12Hz-0.6B-Base.mmproj-Q8_0.gguf";
        auto policy = !std::strcmp(client.config.inference_backend, "cpu")
                          ? MUSE_BACKEND_CPU
                          : !std::strcmp(client.config.inference_backend, "vulkan_required")
                                ? MUSE_BACKEND_VULKAN_REQUIRED : MUSE_BACKEND_AUTO;
        const bool neural_enabled = std::strcmp(client.config.speech_mode, "espeak");
        muse_inference_config cfg{muse_config_local(&client.config) ? llm.c_str() : nullptr,
                                  whisper.c_str(), base.c_str(),
                                  "en", 2, policy, client.config.artifact_dir,
                                  neural_enabled ? neural.c_str() : nullptr,
                                  neural_enabled ? projector.c_str() : nullptr};
        char err[512]{};
        engine = muse_inference_create(&cfg, err, sizeof err);
        if (!engine) {
          r.rc = -1;
          r.error = err;
        }
#else
        r.rc = -1;
        r.error = "This build has no linked local inference engine. Configure "
                  "the console PS5LM and speech endpoints.";
#endif
        break;
      }
      case Task::record:
      case Task::phone:
      case Task::decision: {
        std::vector<int16_t> pcm;
        int sample_rate = 48000;
        std::string path =
            w.type == Task::phone
                ? w.path
                : std::string(client.config.artifact_dir) + "/request.wav";
        if (w.type == Task::phone && muse_config_local(&client.config)) {
          r.rc = -1; r.error = "Phone uploads are disabled in Local mode.";
          unlink(path.c_str()); break;
        }
        if (w.type == Task::phone) {
          r.rc = read_pcm(path, pcm, sample_rate, r.error) ? 0 : -1;
          if (r.rc)
            unlink(path.c_str());
        } else {
          r.rc = capture(client.config.audio_in_module, recording, meter, pcm,
                         r.error);
          recording.store(false);
          if (!r.rc && !wav(path, pcm)) {
            r.rc = -1;
            r.error = "Cannot persist microphone recording.";
          }
        }
        if (r.rc)
          break;
        muse_log_event(&journal, "info", "voice_submit",
                       w.type == Task::decision ? "approval utterance captured"
                                                : "request captured");
        char transcript[4097]{}, draft[4097]{};
#ifdef MUSE_LOCAL_INFERENCE
        if (engine) {
          char err[512]{};
          r.rc = muse_inference_transcribe(
              static_cast<muse_inference *>(engine), pcm.data(), pcm.size(),
              sample_rate, transcript, sizeof transcript, err, sizeof err);
          if (r.rc)
            r.error = err;
        } else
#endif
          r.rc = muse_client_transcribe(&client, path.c_str(), transcript);
        if (r.rc) {
          if (!muse_config_local(&client.config) && w.type != Task::decision)
            r.error = "Transcription unavailable. Configure a speech fallback or type the request; the relay accepts transcripts only.";
          unlink(path.c_str());
          break;
        }
        r.transcript = transcript;
        if (w.type == Task::decision) {
          if (!muse_config_local(&client.config)) {
            r.intent = hybrid_voice_intent(transcript);
            r.rc = 0;
          } else {
#ifdef MUSE_LOCAL_INFERENCE
          if (engine) {
            muse_local_intent intent = MUSE_INTENT_UNKNOWN;
            char err[512]{};
            r.rc = muse_inference_classify(
                static_cast<muse_inference *>(engine), transcript,
                w.text.c_str(), &intent, err, sizeof err);
            if (r.rc)
              r.error = err;
            else
              r.intent = intent == MUSE_INTENT_APPROVE  ? "approve"
                         : intent == MUSE_INTENT_DETAIL ? "add_detail"
                         : intent == MUSE_INTENT_CANCEL ? "cancel"
                                                        : "unknown";
          } else
#endif
          {
            char intent[16]{};
            r.rc = muse_client_classify(&client, transcript, intent);
            r.intent = intent;
          }
          }
        } else {
          std::string draft_request =
              w.text.empty() ? std::string(transcript)
                             : "Revise this existing plan: " + w.text +
                                   "\nUser change: " + transcript;
          if (muse_config_local(&client.config)) {
#ifdef MUSE_LOCAL_INFERENCE
          if (engine) {
            char err[512]{};
            if (muse_inference_draft(static_cast<muse_inference *>(engine),
                                     draft_request.c_str(), draft, sizeof draft,
                                     err, sizeof err))
              muse_log_event(&journal, "warn", "local_draft_unavailable", err);
          } else
#endif
            muse_client_draft(&client, draft_request.c_str(), draft);
            create_local(w, r, draft_request.c_str(), draft);
            unlink(path.c_str());
            break;
          }
          r.rc = muse_client_transcript_context(&client, transcript, draft,
                                                w.id.c_str(),
                                                w.revision.c_str(), &r.plan);

        }
        unlink(path.c_str());
        break;
      }
      case Task::transcript:
        muse_log_event(&journal, "info", "voice_submit", "typed request submitted");
        r.transcript = w.text;
        r.rc = muse_client_transcript_context(&client, w.text.c_str(), "",
                    w.id.c_str(), w.revision.c_str(), &r.plan);
        break;
      case Task::plan_poll:
        r.rc = muse_client_plan_resume(&client, w.id.c_str(), &r.plan);
        r.plan_retryable = r.rc < 0 && (client.http_status == 0 || client.http_status >= 500);
        break;
      case Task::plan_recover: {
        muse_plan pending{};
        r.rc = muse_client_pending_load(&client, &pending);
        if (r.rc == MUSE_CLIENT_PENDING) {
          r.plan = pending;
          r.rc = muse_client_plan_resume(&client, pending.id, &r.plan);
          r.plan_retryable = r.rc < 0 && (client.http_status == 0 || client.http_status >= 500);
          if (r.rc >= 0) std::strcpy(r.plan.transcript, pending.transcript);
        }
        break;
      }
      case Task::plan_cancel:
        r.rc = muse_client_pending_clear(&client, w.id.c_str());
        break;
      case Task::approve: {
        muse_log_event(&journal, "info", "approve", w.id.c_str());
        char job[129]{};
        r.rc =
            muse_client_approve(&client, w.id.c_str(), w.revision.c_str(), job);
        r.job = job;
        if (!r.rc) {
          if (muse_client_pending_clear(&client, w.id.c_str()))
            muse_log_event(&journal, "warn", "plan_recovery_cleanup", client.error);
          approved_generation = w.generation;
          approved_job = job;
          downloaded_generation = 0;
          downloaded_path.clear();
        }
        break;
      }
      case Task::poll:
        r.rc = muse_client_status(&client, w.id.c_str(), &r.status);
        break;
      case Task::download: {
        if (w.generation != approved_generation || w.id != approved_job) {
          r.rc = -1;
          r.error = "Download is not bound to this worker's approved build.";
          break;
        }
        muse_log_event(&journal, "info", "download", w.id.c_str());
        char path[1024]{};
        r.rc = muse_client_artifact(&client, w.id.c_str(), &w.manifest, path,
                                    sizeof path);
        r.path = path;
        if (!r.rc) {
          downloaded_generation = w.generation;
          downloaded_path = path;
          downloaded_manifest = w.manifest;
        }
        break;
      }
      case Task::install_start: {
        if (installer || w.generation != downloaded_generation ||
            w.id != approved_job || w.path != downloaded_path ||
            std::strcmp(downloaded_manifest.kind, "pkg") ||
            std::memcmp(&w.manifest, &downloaded_manifest, sizeof(w.manifest))) {
          r.rc = -1;
          r.error = "Installation requires this session's approved, verified package.";
          break;
        }
        muse_log_event(&journal, "info", "install_begin", downloaded_manifest.title_id);
        installer = muse_install_begin(&client, downloaded_path.c_str(),
                                        &downloaded_manifest, &r.install);
        r.rc = installer ? 0 : -1;
        break;
      }
      case Task::install_poll:
      case Task::launch:
        if (!installer || w.generation != downloaded_generation) {
          r.rc = -1;
          r.error = "No matching approved installation session is active.";
        } else {
          r.rc = w.type == Task::launch
                     ? muse_install_launch(installer, &r.install)
                     : muse_install_poll(installer, &r.install);
          if (r.rc)
            r.error = client.error;
          muse_log_event(&journal, r.rc ? "error" : "info",
                         w.type == Task::launch ? "launch_result" : "install_status",
                         r.install.detail);
          if (r.rc || r.install.running_observed) {
            muse_install_close(installer);
            installer = nullptr;
          }
        }
        break;
      case Task::dispatch:
        if (w.generation != downloaded_generation || w.path != downloaded_path ||
            std::strcmp(downloaded_manifest.kind, "elf")) {
          r.rc = -1;
          r.error = "ELF dispatch requires this session's approved, verified artifact.";
          break;
        }
        muse_log_event(&journal, "info", "launch", w.path.c_str());
        muse_log_flush(&journal);
        r.rc = muse_client_dispatch(&client, w.path.c_str());
        if (r.rc)
          r.error = client.error;
        muse_log_event(
            &journal, "info", "session_end",
            r.rc ? "loader dispatch failed"
                 : "ELF sent; execution requires console observation");
        break;
      case Task::speak: {
        r.path = std::string(client.config.artifact_dir) + "/reply.wav";
#ifdef MUSE_LOCAL_INFERENCE
        char err[512]{};
        r.rc = engine
                   ? muse_inference_speak(static_cast<muse_inference *>(engine),
                                          w.text.c_str(), r.path.c_str(), err,
                                          sizeof err)
                   : -1;
        if (r.rc)
          r.error = err;
#else
        r.rc = -1;
        r.error = "Local speech synthesis is unavailable in this build.";
#endif
        if (r.rc && !muse_config_local(&client.config) && !w.path.empty()) {
          muse_log_event(&journal, "warn", "speech_fallback",
                         "Fetching Muse-provided reply WAV");
          unsigned char *bytes = nullptr;
          size_t length = 0;
          r.rc = muse_client_http(&client, client.config.server_url, "GET",
                                  w.path.c_str(), "audio/wav", nullptr, 0,
                                  8 * 1024 * 1024, &bytes, &length);
          if (!r.rc) {
            FILE *f = fopen(r.path.c_str(), "wb");
            bool ok = f && fwrite(bytes, 1, length, f) == length;
            if (f && fclose(f))
              ok = false;
            if (!ok) {
              r.rc = -1;
              r.error = "Cannot save server reply WAV.";
            } else {
              std::vector<int16_t> check;
              int rate = 0;
              if (!read_pcm(r.path, check, rate, r.error))
                r.rc = -1;
              else {
                r.error.clear();
                r.speech_backend = "Muse server WAV fallback";
              }
            }
          } else
            r.error = client.error;
          free(bytes);
        }
        break;
      }
      case Task::configure: break; // handled before any journal/network operation
      case Task::local_create:
      case Task::local_load:
      case Task::local_list:
      case Task::local_approve:
      case Task::local_step:
      case Task::local_resume:
      case Task::local_pause:
      case Task::local_cancel:
        local_action(w, r);
        break;
      case Task::retry:
        r.rc = muse_log_flush(&journal);
        if (!r.rc)
          r.rc = muse_install_recover_logs(&client);
        break;
      case Task::flush:
        muse_log_event(&journal, "info", "session_end", w.text.c_str());
        r.rc = muse_log_flush(&journal);
        break;
      case Task::stop:
        if (projects_open) {
          if (current_job.id[0] && current_job.state == MUSE_LOCAL_QUEUED)
            muse_local_pause(&projects, &current_job, 0);
          muse_local_close(&projects); projects_open = false;
        }
        if (installer) {
          muse_install_close(installer);
          installer = nullptr;
          muse_log_event(&journal, "warn", "install_monitor_closed",
                         "OS-accepted installation is not cancelled.");
        }
#ifdef MUSE_LOCAL_INFERENCE
        if (engine) {
          muse_inference_destroy(static_cast<muse_inference *>(engine));
          engine = nullptr;
        }
#endif
        muse_log_event(&journal, "info", "session_end", "user closed app");
        muse_log_close(&journal);
        break;
      }
    }
    }
#ifdef MUSE_LOCAL_INFERENCE
    if (engine && (w.type == Task::init || w.type == Task::record ||
                   w.type == Task::phone || w.type == Task::decision ||
                   w.type == Task::speak || w.type == Task::local_create ||
                   w.type == Task::local_step)) {
      char backend[2048]{};
      if (!muse_inference_status(static_cast<muse_inference *>(engine), backend,
                                 sizeof backend)) {
        r.backend = muse_config_local(&client.config)
            ? "Qwen: " + status_field(backend, "; Qwen: ") +
              "; Whisper: " + status_field(backend, "; Whisper: ")
            : "Muse / Whisper: " + status_field(backend, "; Whisper: ");
        if (w.type == Task::speak && r.speech_backend.empty())
          r.speech_backend = status_field(backend, "; TTS: ");
        muse_log_event(&journal, "info", "inference_backend", backend);
      }
    }
#endif
    if (w.type == Task::speak && r.speech_backend.empty())
      r.speech_backend = !r.rc && !w.path.empty() ? "Muse server WAV fallback"
                                                : "Speech unavailable";
    r.http_status = client.http_status;
    if (r.rc < 0) {
      if (r.error.empty())
        r.error = client.error[0] ? client.error : "Operation failed";
      if (opened && w.type != Task::configure)
        muse_log_event(&journal, "error", "failure", r.error.c_str());
    }
    if (r.rc >= 0 && r.rc != MUSE_CLIENT_NO_PENDING &&
        (w.type == Task::record || w.type == Task::phone || w.type == Task::transcript ||
         w.type == Task::plan_poll || w.type == Task::plan_recover) &&
        r.plan.id[0]) {
      muse_log_event(&journal, "info", r.rc == MUSE_CLIENT_PENDING ? "plan_wait" : "plan_fetch", r.plan.id);
    }
    pthread_mutex_lock(&mutex);
    result = r;
    running = false;
    ready = true;
    pthread_mutex_unlock(&mutex);
    if (w.type == Task::stop) {
      muse_log_close(&journal);
      return;
    }
  }
}
} // namespace muse
