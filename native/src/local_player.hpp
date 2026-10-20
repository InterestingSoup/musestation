// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/input.hpp"
#include "gfx/renderer.hpp"
#include "ui/fonts.hpp"
#include "local_app.h"
#include <string>
namespace muse {
class LocalPlayer {
public:
  ~LocalPlayer();
  bool open(const std::string &source, const std::string &expected_sha,
            const std::string &state_path, uint64_t seed, std::string &error);
  bool frame(double dt, const hui::InputFrame &, std::string &error);
  void render(hui::gfx::Renderer &, const hui::ui::Fonts &, int width, int height,
              unsigned framebuffer = 0);
  bool save(std::string &error);
  void close();
  bool active() const { return app != nullptr; }
private:
  muse_local_app *app = nullptr;
  const muse_local_draw_list *draw = nullptr;
  std::string state_path, saved_state;
  double save_after = 15;
};
}
