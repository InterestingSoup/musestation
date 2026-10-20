// SPDX-License-Identifier: GPL-3.0-or-later
#include "view.hpp"
#include <cmath>
#include <cstdio>
using namespace hui;
namespace muse {
namespace {
const gfx::Color white = gfx::Color::rgb(0xf0f6fa),
                 muted = gfx::Color::rgb(0xa1b4c5),
                 mint = gfx::Color::rgb(0x68f2cd),
                 panel = gfx::Color::rgb(0x111f30, .94f);
void label(gfx::DrawList &d, const ui::FontRef &f, const std::string &s,
           float x, float y, float size = 24, gfx::Color c = muted) {
  ui::text(d, f, s, x, y, size, c);
}
void card(gfx::DrawList &d, gfx::Rect r) {
  d.shadow(r, 28, 45, gfx::Color::rgb(0x01070d, .35f));
  d.rounded_rect(r, 28, panel);
}
void pill(gfx::DrawList &d, const ui::FontRef &f, const std::string &s, float x,
          float y, float w, gfx::Color c = mint) {
  d.rounded_rect({x, y, w, 42}, 21, c.with_alpha(.12f));
  label(d, f, s, x + 18, y + 29, 20, c);
}
} // namespace
const char *keyboard_keys() {
  return "1234567890qwertyuiopasdfghjkl:zxcvbnm./-_ "
         "@[]ABCDEFGHIJKLMNOPQRSTUVWXYZ";
}
void render(gfx::Renderer &r, const ui::Fonts &f, const View &v, int width,
            int height, unsigned framebuffer) {
  gfx::DrawList d;
  gfx::BackdropSpec b;
  b.mode = gfx::BackdropMode::aurora;
  b.colors[0] = gfx::Color::rgb(0x040b14);
  b.colors[1] = gfx::Color::rgb(0x091725);
  b.colors[2] = gfx::Color::rgb(0x123b47);
  b.colors[3] = gfx::Color::rgb(0x18263f);
  b.time = v.reduced_motion ? 0 : v.seconds * .15f;
  r.begin();
  r.backdrop(b);
  d.rounded_rect({96, 64, 48, 48}, 16, mint);
  d.line(108, 99, 108, 79, 3, gfx::Color::rgb(0x102d30));
  d.line(108, 79, 120, 91, 3, gfx::Color::rgb(0x102d30));
  d.line(120, 91, 132, 79, 3, gfx::Color::rgb(0x102d30));
  d.line(132, 79, 132, 99, 3, gfx::Color::rgb(0x102d30));
  label(d, f.semibold, "MuseStation", 160, 103, 40, white);
  label(d, f.regular, "YOUR CONSOLE. YOUR IDEAS.", 470, 98, 19, muted);
  pill(d, f.regular, v.local ? "All Local  /  Offline" : "Hybrid  /  " + v.provider, 1454, 70, 370);
  const bool request = v.screen == Screen::request;
  const bool setup = v.screen == Screen::setup || request;
  label(d, f.semibold,
        request                           ? "DESCRIBE YOUR APP"
        : setup                           ? "MAKE YOURSELF AT HOME"
        : v.screen == Screen::modes       ? "CHOOSE HOW TO CREATE"
        : v.screen == Screen::library     ? "YOUR LOCAL PROJECTS"
        : v.screen == Screen::connections ? "CONNECT A DEVELOPMENT PARTNER"
        : v.screen == Screen::plan        ? "YOUR BUILD PLAN"
        : v.screen == Screen::planning    ? "YOUR REQUEST IS WITH MUSE"
        : v.screen == Screen::diagnostics ? "CONSOLE HEALTH"
                                          : "YOUR PERSONAL CREATION SPACE",
        96, 190, 22, mint);
  std::string title =
      request                           ? v.field.empty() ? "What should we make?" : v.field
      : setup                           ? v.field
      : v.screen == Screen::modes       ? "Your idea. Muse builds it."
      : v.screen == Screen::library     ? "Your ideas, kept here."
      : v.screen == Screen::connections ? v.connection_muse ? "Connect with Muse." : "More ways to create."
      : v.screen == Screen::project     ? v.job_label
      : v.screen == Screen::plan        ? "A plan before we play."
      : v.screen == Screen::planning    ? "Waiting for Muse."
      : v.screen == Screen::working     ? "Bringing your idea to life."
      : v.screen == Screen::error       ? "Let's get this working."
      : v.screen == Screen::diagnostics ? "Everything in view."
                                        : "Make something\nworth playing.";
  ui::paragraph(d, f.display, title, 96, 282, 66, 1690, 77, white, 2);
  if (setup) {
    label(d, f.regular,
          request ? "D-pad selects a key. X types it. R1 prepares a plan."
          : "Step " + std::to_string(v.field_index + 1) +
              " of " + std::to_string(v.step_count) + "  /  D-pad selects a key. X types it. R1 continues.",
          96, 350, 27, muted);
    card(d, {96, 390, 1728, 95});
    const char *empty_value = !request && (v.step_count==1 || v.field_index==4) ? "Optional: leave blank to use typed requests"
                              : (v.field_index == 2 || v.field_index == 3)
                                  ? "Leave blank to use the bundled engines"
                                  : v.secret_edit ? "Enter your relay console token; it stays hidden" : "Enter a value";
    std::string visible_edit=v.secret_edit ? std::string(v.edit.size(),'*') : v.edit;
    if (visible_edit.size()>80) {
      size_t start=visible_edit.size()-80;
      while (start<visible_edit.size() && (static_cast<unsigned char>(visible_edit[start])&0xc0)==0x80) ++start;
      visible_edit="... "+visible_edit.substr(start);
    }
    label(d, f.mono,
          f.mono.font->fit(v.edit.empty() ? empty_value : visible_edit, 30, 1600),
          126, 451, 30, v.edit.empty() ? muted : white);
    const char *keys = keyboard_keys();
    for (int i = 0; keys[i]; ++i) {
      float x = 96 + float(i % 12) * 143, y = 520 + float(i / 12) * 67;
      bool focus = i == v.key;
      d.rounded_rect({x, y, 129, 54}, 13, focus ? mint : panel);
      label(d, f.mono, std::string(1, keys[i]), x + 51, y + 37, 27,
            focus ? gfx::Color::rgb(0x102d30) : white);
    }
    label(d, f.regular, v.error, 96, 963, 24, gfx::Color::rgb(0xffb388));
  } else if (v.screen == Screen::modes) {
    const char *names[] = {"Hybrid with Muse", "Local lab", "Other providers"};
    const char *details[] = {
      "Speak on your PS5. Muse plans and develops a native app. Review the plan before approving a build.",
      "Experimental offline creation. A small model builds apps that play inside MuseStation. Existing projects stay here.",
      "Bring your preferred development provider in a future release. Muse is the first integration."};
    for (int i=0; i<3; ++i) {
      float x=96+float(i)*584;
      card(d,{x,380,560,438});
      if (i==v.mode_choice) d.rounded_rect({x,380,560,7},3,mint);
      label(d,f.semibold,names[i],x+30,445,32,i==v.mode_choice?mint:white);
      ui::paragraph(d,f.regular,details[i],x+30,506,27,500,39,muted,6);
      label(d,f.regular,i==v.mode_choice?(i==0?"X  Set up Opti3 relay":i==1?"X  Open Local lab":"X  View provider status"):"D-pad to select",x+30,772,23,white);
    }
    const char *budgets[]={"4 attempts / 2 active hours", "64 attempts / 24 active hours", "512 attempts / 7 active days"};
    if (v.mode_choice==1) {
      label(d,f.semibold,std::string("Local budget: ")+budgets[v.budget_choice]+"  /  Up or Down changes it",96,870,25,mint);
      ui::paragraph(d,f.regular,"Keep MuseStation open and the console awake. Jobs stop at their limits; saved work can be resumed from your library.",96,918,24,1728,34,muted,2);
    } else {
      label(d,f.semibold,"Local transcription and speech. Muse handles development.",96,870,25,mint);
      ui::paragraph(d,f.regular,"Your Opti3 relay exchanges requests and plans with Muse through a shared folder. Configure its LAN address to begin.",96,918,24,1728,34,muted,2);
    }
  } else if (v.screen == Screen::library) {
    if (v.projects.empty()) {
      card(d,{96,390,1728,440});
      ui::paragraph(d,f.regular,"Your local projects will appear here. Press Circle, then X to describe your first app.",132,470,32,1600,46,white,4);
    } else {
      int first=(v.selection/6)*6;
      for (int i=first; i<int(v.projects.size()) && i<first+6; ++i) {
        float y=362+float(i-first)*94;
        d.rounded_rect({96,y,1728,80},18,i==v.selection?mint.with_alpha(.18f):panel);
        label(d,f.semibold,f.semibold.font->fit(v.projects[i].title,28,950),124,y+36,28,white);
        label(d,f.regular,f.regular.font->fit(v.projects[i].detail,20,1100),124,y+65,20,muted);
        label(d,f.mono,v.projects[i].state,1400,y+46,24,i==v.selection?mint:muted);
      }
      label(d,f.mono,"Page "+std::to_string(v.selection/6+1)+" / "+std::to_string((v.projects.size()+5)/6)+
            "  |  "+std::to_string(v.projects.size())+" projects",96,965,22,muted);
    }
  } else if (v.screen == Screen::connections) {
    card(d,{96,380,1728,500});
    label(d,f.semibold,v.connection_muse?"OPTI3 BUILD SERVER / MUSE RELAY":"PROVIDER CONNECTIONS",132,440,26,mint);
    ui::paragraph(d,f.regular,v.connection_muse
      ?"Opti3 receives your transcript and exchanges a request with Muse through your configured shared folder. The plan appears here when Muse returns it. You review and approve before the server builds."
      :"Muse is the first development provider. OpenAI and other providers are planned; each will use its supported account or key connection.",132,502,30,1610,44,white,5);
    ui::paragraph(d,f.regular,v.connection_muse
      ?"Press X to configure the relay's LAN address. Muse must be able to read and write the shared exchange folder. This flow does not use Bluetooth pairing, an SDK token, or a linked-account status."
      :"This build cannot connect those accounts or send them requests. Your existing Local lab projects remain available without an account.",132,724,27,1610,39,muted,4);
  } else if (v.screen == Screen::plan) {
    card(d, {96, 340, 1180, 527});
    label(d, f.semibold, v.local ? "LOCAL PLAN  /  YOUR APPROVAL" : "MUSE-REVIEWED PLAN", 132, 395, 20, mint);
    auto lines = ui::paragraph_lines(f.regular, v.plan, 32, 1090, 999);
    int begin = v.page * 8;
    for (int i = begin; i < int(lines.lines.size()) && i < begin + 8; ++i)
      label(d, f.regular, lines.lines[i], 132, 460 + float(i - begin) * 45, 32,
            white);
    label(d, f.regular,
          "Page " + std::to_string(v.page + 1) + " / " +
              std::to_string(v.page_count) + "   |   D-pad to read",
          132, 833, 22, muted);
    card(d, {1300, 340, 524, 527});
    label(d, f.semibold, "YOU'RE IN CONTROL", 1336, 402, 24, white);
    ui::paragraph(
        d, f.regular,
        v.voice_verified
            ? "Say approve, add detail, or cancel. Buttons are always "
              "available."
            : "Review every page, then press X to approve. Voice decisions "
              "stay advisory until verified on your console.",
        1336, 460, 28, 450, 40, muted, 7);
    pill(d, f.semibold, "X   Approve build", 1336, 708, 452);
    label(d, f.regular, "Square  Add detail", 1336, 801, 25, white);
    if (v.local && !v.job_budget.empty())
      label(d, f.mono, f.mono.font->fit(v.job_budget, 23, 1728), 96, 918, 23, muted);
  } else {
    card(d, {96, 421, 1100, 452});
    card(d, {1220, 421, 604, 452});
    const bool err = v.screen == Screen::error;
    label(d, f.semibold,
          v.recording ? "LISTENING TO YOU"
          : v.busy    ? "WORK IN PROGRESS"
          : err       ? "ACTION NEEDED"
          : v.screen == Screen::project ? "SAVED PROJECT"
                      : "START WITH AN IDEA",
          132, 479, 22, v.recording ? gfx::Color::rgb(0xffb388) : mint);
    ui::paragraph(d, f.regular, v.detail, 132, 545, 32, 1015, 46, white, 4);
    if (v.progress >= 0) {
      const float fraction = std::fmax(0.f, std::fmin(1.f, float(v.progress) / 100.f));
      d.rounded_rect({136, 744, 1010, 14}, 7, mint.with_alpha(.12f));
      if (fraction > 0) d.rounded_rect({136, 744, 1010 * fraction, 14}, 7, mint);
      label(d, f.mono, std::to_string(v.progress) + "%", 136, 797, 24, white);
    } else for (int i = 0; i < 59; ++i) {
      float phase = float(i) * .41f;
      float a =
          v.recording
              ? 15 + 70 * v.meter *
                         (.35f +
                          .65f * std::fabs(std::sin(v.seconds * 7 + phase)))
              : 8 + 14 * std::fabs(std::sin(
                             phase + (v.reduced_motion ? 0 : v.seconds)));
      d.rounded_rect({136 + float(i) * 17, 760 - a / 2, 5, a}, 2.5f,
                     (v.recording ? gfx::Color::rgb(0xffbb8c) : mint)
                         .with_alpha(i % 4 ? .7f : 1));
    }
    label(d, f.regular, f.regular.font->fit(v.status, 24, 1015), 132, 832, 24, muted);
    label(d, f.semibold, "ON YOUR PLAYSTATION", 1256, 479, 22, white);
    label(d, f.regular, "Intelligence", 1256, 542, 23, muted);
    label(d, f.semibold, f.semibold.font->fit(v.model, 24, 530), 1256, 583, 24, white);
    label(d, f.regular, f.regular.font->fit(v.speech, 21, 530), 1256, 620, 21, muted);
    d.line(1256, 642, 1788, 642, 1, muted.with_alpha(.2f));
    label(d, f.regular, v.local ? "Creation mode" : "Muse server", 1256, 680, 23, muted);
    label(d, f.mono, f.mono.font->fit(v.local ? "Offline / no account" : v.server, 24, 530), 1256, 716, 24, white);
    d.line(1256, 751, 1788, 751, 1, muted.with_alpha(.2f));
    label(d, f.regular,
          v.connected ? "Controller connected" : "Connect your DualSense", 1256,
          785, 24, v.connected ? mint : muted);
    label(d, f.regular, v.local ? "Logs stay on this console" : "Logs upload automatically", 1256, 829, 23, muted);
    if (!v.job_budget.empty()) label(d,f.mono,f.mono.font->fit(v.job_budget,21,1015),132,897,21,muted);
  }
  d.line(96, 995, 1824, 995, 1, muted.with_alpha(.25f));
  label(d, f.regular,
        setup ? "X  Type     Square  Delete     R1  Continue     Circle  Back"
        : v.screen == Screen::modes ? v.mode_choice==1 ? "Left / Right  Mode     Up / Down  Budget     X  Select     Circle  Back" : "Left / Right  Mode     X  Select     Circle  Back"
        : v.screen == Screen::library ? "D-pad  Choose     X  Open project     Circle  Back"
        : v.screen == Screen::connections ? v.connection_muse ? "X / Square  Configure relay     Circle  Back" : "Circle  Back     Provider connection unavailable"
        : v.screen == Screen::project ? "X  Play / Resume   |   Square  Pause / Refine   |   Triangle  Library   |   Circle  Back"
        : v.screen == Screen::plan ? "X  Approve     Square  Add detail     "
                                     "Circle  Cancel     Hold R2  Speak"
        : v.screen == Screen::planning ? "Circle  Cancel request     L1 + R1  Close and keep request for later"
        : v.local ? "X  Type idea     R2  Speak     Triangle  Library     Options  Modes     L1 + R1  Close"
                  : "X  Type idea     R2  Speak     Circle  Cancel     Options  Modes     L1 + R1  Close",
        96, 1040, 24, white);
  if (!setup) {
    char fps[40];
    std::snprintf(fps, sizeof fps, "%.0f fps", double(v.fps));
    label(d, f.mono, fps, 1700, 1040, 22, muted);
  }
  r.draw(d);
  r.present(framebuffer, width, height);
}
} // namespace muse
