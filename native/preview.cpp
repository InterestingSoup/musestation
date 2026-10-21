// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/save_file.hpp"
#include "gfx/canvas.hpp"
#include "muse/view.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <utility>
#include <vector>
namespace hui::sys {
int64_t monotonic_us() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return int64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}
void log(const char *f, ...) {
  va_list a;
  va_start(a, f);
  vfprintf(stderr, f, a);
  fputc('\n', stderr);
  va_end(a);
}
} // namespace hui::sys
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  auto display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                       EGL_DEFAULT_DISPLAY, nullptr);
  EGLint major, minor;
  if (!eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API))
    return 3;
  EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                         4,
                         EGL_CONTEXT_MINOR_VERSION,
                         5,
                         EGL_CONTEXT_OPENGL_PROFILE_MASK,
                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                         EGL_NONE};
  auto ctx =
      eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
  if (ctx == EGL_NO_CONTEXT ||
      !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
    return 4;
  hui::gfx::Renderer r;
  if (!r.init())
    return 5;
  std::array<hui::gfx::Font, 4> font;
  hui::ui::Fonts f;
  hui::ui::FontRef *refs[] = {&f.regular, &f.semibold, &f.display, &f.mono};
  const char *names[] = {"inter-regular.huifont", "inter-semibold.huifont",
                         "montserrat-medium.huifont",
                         "dejavu-sans-mono.huifont"};
  for (int i = 0; i < 4; ++i) {
    std::string data;
    if (!hui::save::read_file(std::string(argv[1]) + "/fonts/" + names[i],
                              &data) ||
        !font[i].load(data))
      return 6;
    *refs[i] = {&font[i], r.batch().create_font_texture(font[i])};
  }
  GLuint texture, buffer;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1920, 1080, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glGenFramebuffers(1, &buffer);
  glBindFramebuffer(GL_FRAMEBUFFER, buffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    return 7;
  // render() presents to framebuffer0; redirect its final output using an
  // explicit framebuffer parameter in the shared view renderer.
  struct Scenario { const char *name; muse::View view; };
  std::vector<Scenario> scenarios;
  auto base = []() {
    muse::View v;
    v.connected=true; v.fps=60; v.seconds=4; v.local=true; v.provider="All Local";
    v.server="http://192.0.2.10:8000";
    v.model="Qwen2.5-Coder 1.5B / Vulkan requested";
    v.speech="Qwen neural voice";
    v.plan="Create a falling-block puzzle with a clear neon board and score display. "
           "Use the D-pad to move and Cross to rotate, with Circle restarting the game. "
           "The app plays locally inside MuseStation after validation and your Run command.";
    return v;
  };
  auto add = [&](const char *name, muse::View v) { scenarios.push_back({name,std::move(v)}); };
  // Keep prior numbered preview paths usable while adding descriptive coverage.
  auto v=base(); add("screen-0",v);
  v=base(); v.screen=muse::Screen::plan;
  v.job_budget="Attempts 0/512  |  Active time 0/604800 seconds"; add("screen-1",v);
  v=base(); v.local=true; v.screen=muse::Screen::models;
  v.status="Speech ready. No coding model downloaded.";
  add("optional-model-setup",v);
  v.downloading_model=true; v.status="Downloaded 512 / 1065 MiB";
  add("optional-model-downloading",v);
  v=base(); v.local=true; v.screen=muse::Screen::model_url;
  v.field="Hugging Face GGUF file URL"; v.edit="https://huggingface.co/Qwen/";
  add("optional-model-url",v);
  v=base(); v.screen=muse::Screen::setup; v.field="AudioIn module path";
  v.step_count=1; v.field_index=0; add("screen-2",v);
  v=base(); v.screen=muse::Screen::working; v.local=false; v.provider="Muse";
  v.busy=true; v.progress=62; v.detail="Installing your approved app.";
  v.status="Package verified. Keep MuseStation open until installation completes."; add("screen-3",v);
  v=base(); v.screen=muse::Screen::modes; v.mode_choice=1; v.budget_choice=0; add("mode-local",v);
  v=base(); v.local=false; v.provider="Muse"; v.screen=muse::Screen::modes; v.mode_choice=0; add("mode-hybrid",v);
  v=base(); v.local=false; v.provider="Muse"; v.screen=muse::Screen::modes; v.mode_choice=2; add("mode-providers",v);
  v=base(); v.screen=muse::Screen::library; add("library-empty",v);
  v.projects={
    {"Neon Blocks", "ready", "Validated locally. Your saved revision is ready to play."},
    {"Kitchen Timer", "paused", "Generation paused. Partial source is saved; resume explicitly."},
    {"A long project title for a two-player paddle game with a bright scoreboard", "interrupted", "Previous execution was interrupted; reserved step time remains charged."},
    {"Starfield Explorer", "awaiting approval", "Review the saved plan before approving local work."},
    {"Pocket Calculator", "failed", "The attempt budget is exhausted. Saved candidates remain available."},
    {"Rhythm Lights", "cancelled", "Cancelled explicitly; saved revisions retained."},
    {"Daily Counter", "ready", "Validated source. Press X to open the project."}};
  v.selection=2; add("library-projects",v); v.selection=6; add("library-page-2",v);
  v=base(); v.screen=muse::Screen::project; v.job_label="Neon Blocks";
  v.job_budget="Attempts 2/4  |  Active time 182/7200 seconds";
  v.detail="Source passed isolated Lua validation. Press Play explicitly to run it.";
  v.status="Validated locally. Press X to play this saved revision."; add("project-ready",v);
  v=base(); v.screen=muse::Screen::project; v.job_label="Kitchen Timer";
  v.job_budget="Attempts 12/512  |  Active time 1347/604800 seconds";
  v.detail="Generation stopped cooperatively. Partial source is saved and has not been run.";
  v.status="Press X to resume this approved project."; add("project-paused",v);
  v=base(); v.local=false; v.provider="Muse"; v.screen=muse::Screen::connections; add("connections-muse",v);
  v.connection_muse=false; add("connections-providers",v);
  v=base(); v.local=false; v.provider="Muse"; v.screen=muse::Screen::planning;
  v.model="Muse / Whisper: CPU"; v.speech="Qwen neural voice";
  v.detail="Muse is preparing your plan through the Opti3 relay.";
  v.status="Waiting for Muse. Nothing builds until you review and approve the plan.";
  add("plan-waiting",v);
  v=base(); v.local=false; v.provider="Muse"; v.screen=muse::Screen::setup;
  v.field="AudioIn module path (optional for typing)"; v.step_count=5; v.field_index=4;
  add("setup-hybrid-optional-mic",v);
  v=base(); v.screen=muse::Screen::request; v.edit="make a paddle game with a score";
  v.key=17; add("type-request",v);
  v=base(); v.screen=muse::Screen::request; v.field="Refine your saved app";
  v.edit="keep the controls and add a high-score display"; v.key=28; add("refine-request",v);
  v=base(); v.screen=muse::Screen::plan; v.local=false; v.provider="Muse";
  v.plan="Build a falling-block puzzle game with crisp neon graphics, DualSense controls, "
         "and a score display. Include a next-piece preview, pause menu, and increasing speed. "
         "Muse will compile a native homebrew package for installation after approval.";
  add("plan-hybrid",v);
  for (const auto &scenario : scenarios) {
    const auto &v=scenario.view;
    muse::render(r, f, v, 1920, 1080, buffer);
    glBindFramebuffer(GL_FRAMEBUFFER, buffer);
    std::vector<unsigned char> pixels(1920 * 1080 * 4);
    glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::string path =
        std::string(argv[2]) + "/" + scenario.name + ".ppm";
    FILE *out = fopen(path.c_str(), "wb");
    if (!out)
      return 8;
    fprintf(out, "P6\n1920 1080\n255\n");
    for (int y = 1079; y >= 0; --y)
      for (int x = 0; x < 1920; ++x)
        fwrite(pixels.data() + (y * 1920 + x) * 4, 1, 3, out);
    fclose(out);
  }
}
