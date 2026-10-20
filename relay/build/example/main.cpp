// Relay native graphics smoke fixture, human-authored; not a Muse-generated app.
// Copyright (C) 2026 MuseStation contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <array>
#include <string>

int main() {
    using namespace hui;
    ps5::Display display;
    gfx::Renderer renderer;
    if (!display.open() || !renderer.init()) {
        sys::log("Relay graphics fixture: display initialization failed");
        sys::park();
    }
    gfx::Font font;
    std::string bytes;
    if (!save::read_file("/app0/assets/fonts/inter-regular.huifont", &bytes) || !font.load(bytes)) {
        sys::log("Relay graphics fixture: bundled font unavailable");
        sys::park();
    }
    const auto texture = renderer.batch().create_font_texture(font);
    ps5::Pad pad;
    pad.open();
    pad.set_light_bar(85, 222, 192);
    InputTracker tracker;
    std::array<PadSample, 64> samples{};
    gfx::DrawList list;
    float x = 900, y = 500;
    bool alternate = false;
    auto previous = sys::monotonic_us();
    for (;;) {
        const auto now = sys::monotonic_us();
        const float dt = std::clamp(static_cast<float>(now - previous) / 1000000.0f, 0.0f, 0.05f);
        previous = now;
        const auto count = pad.read(samples);
        const auto input = tracker.update(std::span<const PadSample>(samples.data(), count), now);
        if (input.is_pressed(Action::back)) break;
        x += (input.is_held(Action::right) - input.is_held(Action::left) + input.stick_x) * dt * 420;
        y += (input.is_held(Action::down) - input.is_held(Action::up) + input.stick_y) * dt * 420;
        x = std::clamp(x, 100.0f, 1720.0f);
        y = std::clamp(y, 310.0f, 850.0f);
        if (input.is_pressed(Action::confirm)) {
            alternate = !alternate;
            pad.rumble(0.15f, 0.08f);
        }
        pad.tick(dt);
        list.clear();
        list.rounded_rect({0, 0, 1920, 1080}, 0, gfx::Color::rgb(0x0c1826));
        list.text(font, texture, "Native graphics build fixture", 100, 140, 56, gfx::Color::rgb(0xffffff));
        list.text(font, texture, "D-pad / stick: move     Cross: change color     Circle: exit", 100, 215, 32,
                  gfx::Color::rgb(0xb9d0df));
        list.bordered_rect({80, 280, 1760, 700}, 24, gfx::Color::rgb(0x122b3d), 2, gfx::Color::rgb(0x315369));
        list.glow({x, y, 100, 100}, 24, 36, gfx::Color::rgb(0x55dec0, 0.3f));
        list.rounded_rect({x, y, 100, 100}, 24, gfx::Color::rgb(alternate ? 0xffb86b : 0x55dec0));
        renderer.begin();
        renderer.draw(list);
        renderer.present(0, display.width(), display.height());
        display.swap();
        sys::hide_splash_screen();
    }
    pad.close();
    renderer.release();
    display.close();
    sys::quit();
}
