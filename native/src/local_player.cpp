// SPDX-License-Identifier: GPL-3.0-or-later
#include "local_player.hpp"
#include "core/save_file.hpp"
extern "C" {
#include "client_sha256.h"
}
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace muse {
namespace {
bool write_state(const std::string &path, const std::string &data, std::string &error) {
  const std::string temporary = path + ".tmp";
  int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  if (fd < 0) { error = "Could not open app save: " + std::string(strerror(errno)); return false; }
  size_t used = 0;
  while (used < data.size()) {
    ssize_t n = write(fd, data.data() + used, data.size() - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    used += size_t(n);
  }
  bool ok = used == data.size() && fsync(fd) == 0;
  if (::close(fd)) ok = false;
  if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok) {
    error = "Could not commit app save; the previous save was retained.";
    unlink(temporary.c_str());
  } else {
    const auto slash = path.find_last_of('/');
    const std::string parent = slash == std::string::npos ? "." : slash ? path.substr(0, slash) : "/";
    int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    int rc = directory < 0 ? -1 : fsync(directory);
    int sync_error = errno;
    if (directory >= 0) ::close(directory);
    if (rc && sync_error != EINVAL && sync_error != ENOTSUP) {
      error = "App state was written, but its directory could not be synchronized.";
      return false;
    }
  }
  return ok;
}
}
LocalPlayer::~LocalPlayer() { close(); }
void LocalPlayer::close() {
  if (app) muse_local_app_destroy(app);
  app = nullptr; draw = nullptr; saved_state.clear(); state_path.clear(); save_after = 15;
}
bool LocalPlayer::open(const std::string &source, const std::string &expected_sha,
                       const std::string &path, uint64_t seed, std::string &error) {
  close();
  char digest[65]{};
  muse_sha256_hex(source.data(), source.size(), digest);
  if (expected_sha.size() != 64 || expected_sha != digest) {
    error = "The app source no longer matches its validated revision."; return false;
  }
  muse_local_limits limits{};
  limits.seed = seed;
  char detail[512]{};
  if (muse_local_app_create(source.data(), source.size(), &limits, &app, detail, sizeof detail)) {
    error = detail; return false;
  }
  state_path = path;
  std::string data;
  if (access(path.c_str(), F_OK) == 0) {
    if (!hui::save::read_file(path, &data, MUSE_LOCAL_STATE_MAX + 32)) {
      error = "The saved app state is unreadable."; close(); return false;
    }
    auto decoded = hui::save::decode(hui::save::Kind::game, data);
    if (!decoded.ok || decoded.version != 1 || decoded.payload.size() > MUSE_LOCAL_STATE_MAX ||
        muse_local_app_set_state(app, decoded.payload.data(), decoded.payload.size())) {
      error = "The saved app state failed its integrity check."; close(); return false;
    }
    saved_state = decoded.payload;
  }
  return true;
}
bool LocalPlayer::save(std::string &error) {
  if (!app) return true;
  size_t n = 0;
  const char *value = muse_local_app_get_state(app, &n);
  if (!value || n > MUSE_LOCAL_STATE_MAX) { error = "Invalid app save data."; return false; }
  std::string current(value, n);
  if (current == saved_state) return true;
  if (!write_state(state_path, hui::save::encode(hui::save::Kind::game, 1, current), error)) return false;
  saved_state = current;
  return true;
}
bool LocalPlayer::frame(double dt, const hui::InputFrame &input, std::string &error) {
  if (!app) return false;
  const hui::Action actions[] = {hui::Action::up, hui::Action::down, hui::Action::left,
    hui::Action::right, hui::Action::confirm, hui::Action::back, hui::Action::west,
    hui::Action::north, hui::Action::page_prev, hui::Action::page_next,
    hui::Action::jump_prev, hui::Action::jump_next, hui::Action::menu};
  muse_local_input buttons{};
  if (input.connected && !input.focus_lost) {
    for (unsigned i = 0; i < MUSE_LOCAL_BUTTON_COUNT; ++i) {
      if (input.is_held(actions[i])) buttons.down |= 1u << i;
      if (input.is_pressed(actions[i])) buttons.pressed |= 1u << i;
    }
    buttons.lx=input.stick_x; buttons.ly=input.stick_y;
    buttons.rx=input.stick2_x; buttons.ry=input.stick2_y;
  }
  char detail[512]{};
  if (muse_local_app_frame(app, dt, &buttons, &draw, detail, sizeof detail)) {
    error = detail; return false;
  }
  save_after -= dt;
  if (save_after <= 0) {
    save_after = 15;
    if (!save(error)) return false;
  }
  return true;
}
void LocalPlayer::render(hui::gfx::Renderer &renderer, const hui::ui::Fonts &fonts,
                         int width, int height, unsigned framebuffer) {
  hui::gfx::DrawList list;
  list.rounded_rect({0, 0, 1280, 720}, 0, hui::gfx::Color::rgb(0x070d16));
  if (draw) for (size_t i=0; i<draw->count; ++i) {
    const auto &c=draw->commands[i];
    hui::gfx::Color color{c.r/255.f, c.g/255.f, c.b/255.f, c.a/255.f};
    switch(c.type) {
    case MUSE_LOCAL_CLEAR: list.rounded_rect({0,0,1280,720}, 0, color); break;
    case MUSE_LOCAL_RECT: list.rounded_rect({c.x,c.y,c.w,c.h}, 0, color); break;
    case MUSE_LOCAL_CIRCLE_DRAW: list.circle(c.x,c.y,c.radius,color); break;
    case MUSE_LOCAL_LINE: list.line(c.x,c.y,c.w,c.h,c.thickness,color); break;
    case MUSE_LOCAL_TEXT: hui::ui::text(list,fonts.regular,c.text,c.x,c.y,c.font_size,color); break;
    }
  }
  list.rounded_rect({936, 682, 328, 28}, 8, hui::gfx::Color::rgb(0x070d16,.85f));
  hui::ui::text(list,fonts.regular,"MuseStation  /  L1 + R1 returns",950,702,16,hui::gfx::Color::rgb(0xcbd8e8));
  renderer.begin(); renderer.draw(list); renderer.present(framebuffer,width,height,1280,720);
}
}
