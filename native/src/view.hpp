// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gfx/renderer.hpp"
#include "ui/fonts.hpp"
#include <string>
#include <vector>
namespace muse {
enum class Screen { home, plan, planning, working, error, setup, diagnostics, modes, request, library, project, connections, models, model_url };
struct ProjectRow { std::string title, state, detail; };
struct View {
  Screen screen = Screen::home;
  std::string headline = "Make something worth playing.";
  std::string detail = "Hold R2 and describe what Muse should create.";
  std::string transcript, plan, status = "Ready when you are.",
                                server = "Not configured",
                                model = "Loading local engines", speech = "Loading voice";
  std::string edit, field, error;
  bool secret_edit = false;
  std::string provider = "Muse", job_label, job_budget;
  std::vector<ProjectRow> projects;
  int key = 0, field_index = 0, step_count = 5, page = 0, page_count = 1, progress = -1,
      selection = 0, mode_choice = 0, budget_choice = 0;
  bool recording = false, busy = false, voice_verified = false,
       connected = false, reduced_motion = false, local = false,
       connection_muse = true, model_ready = false, downloading_model = false;
  float seconds = 0, fps = 0, meter = 0;
};
void render(hui::gfx::Renderer &, const hui::ui::Fonts &, const View &,
            int width, int height, unsigned framebuffer = 0);
const char *keyboard_keys();
} // namespace muse
