#include "app/shell.h"

#include <cstdio>
#include <cstring>

#include "app/app_rules.h"
#include "app/modules.h"
#include "app/theme.h"
#include "app/toolkit.h"
#include "app/widgets.h"
#include "assets/assets.h"
#include "board/board_profile.h"
#include "ui/gfx.h"

namespace app {

const ModuleInfo kModules[kModuleCount] = {
    {"IR", "ir", ir::makeMenu},
    {"NFC", "nfc", nfc::makeMenu},
    {"GAMES", "games", games::makeMenu},
    {"WIFI SETUP", "wifi_setup", wifi::makeMenu},
    {"BLUETOOTH REMOTE", "bluetooth_remote", bt::makeMenu},
    {"SETTINGS", "settings", settings::makeMenu},
};

namespace shell {

using hal::Button;
namespace col = ui::color;

namespace {

constexpr uint32_t kPostLineMs = 120;   // M1: one check line every 120 ms
constexpr uint32_t kLogoMinMs = 3000;   // M2: shown at least this long
constexpr uint32_t kWrongPinMs = 1500;  // M4b

// Big digits for clocks: the 8x8 font scaled, the colon drawn narrow so "12:34" fits at x3.
void bigTime(ui::Framebuffer& fb, int16_t y, const std::string& t, int scale, ui::Color c) {
  const FontEntry& f = theme::large();
  const int16_t cell = (int16_t)(f.w * scale);
  const int16_t colonW = (int16_t)(4 * scale);
  int16_t w = 0;
  for (char ch : t) w = (int16_t)(w + (ch == ':' ? colonW : cell));
  int16_t x = ui::centerIn(0, ui::kScreenW, w);
  for (char ch : t) {
    const char s[2] = {ch, 0};
    if (ch == ':') {
      ui::drawText(fb, f, (int16_t)(x - 2 * scale), y, s, c, scale);
      x = (int16_t)(x + colonW);
    } else {
      ui::drawText(fb, f, x, y, s, c, scale);
      x = (int16_t)(x + cell);
    }
  }
}

std::string clockText(App& app) {
  hal::DateTime t;
  char buf[8];
  if (!app.localTime(t)) return "--:--";
  unsigned h = t.hour;
  if (!app.settings().clock24h) h = h % 12 == 0 ? 12 : h % 12;
  std::snprintf(buf, sizeof(buf), "%02u:%02u", h, (unsigned)t.minute);
  return buf;
}

std::unique_ptr<Screen> makeBootLogo();
std::unique_ptr<Screen> makeEmergencyMenu();
std::unique_ptr<Screen> makeLauncher(int sel);

// Pops the lock screen (and the PIN pages above it). At boot the lock screen is the root: go home.
void unlock(App& app) {
  int i = app.find("M3b");
  if (i < 0) i = app.find("M3a");
  if (i <= 0) app.reset(makeHome(), fade(150));
  else app.popToDepth((size_t)i, fade(150));
}

// ---------------- M1 boot status ----------------

class BootStatus : public Screen {
 public:
  const char* code() const override { return "M1"; }
  std::string title() const override { return "Boot"; }
  bool statusBar() const override { return false; }
  uint8_t deferMask() const override { return 0; }
  void onEnter(App& app) override {
    at_ = app.now();
    hal::Hal& h = app.hal();
    char buf[32];
    lines_.clear();
    auto row = [&](const char* label, const char* status) {
      std::string s = label;
      s += ' ';
      while (s.size() < 20 - std::strlen(status) - 1) s += '.';
      s += ' ';
      s += status;
      lines_.push_back(s);
    };
    row("CPU ESP32-S3", "OK");
    std::snprintf(buf, sizeof(buf), "FLASH %uMB", (unsigned)(board::kFlashBytes >> 20));
    row(buf, h.storage.present(hal::Volume::Flash) ? "OK" : "FAIL");
    row("DISPLAY", "OK");
    row("BUTTONS", "OK");
    row("SD CARD", h.storage.present(hal::Volume::Sd) ? "OK" : "NONE");
    hal::DateTime t;
    row("RTC DS3231", h.rtc.now(t) ? "OK" : "FAIL");
    const int pct = app.battery().percent();
    if (pct < 0) row("BATTERY", "??");
    else {
      std::snprintf(buf, sizeof(buf), "BATTERY %d%%", pct);
      row(buf, pct > 5 ? "OK" : "LOW");
    }
  }
  void onInput(App& app, const InputEvent& e) override {
    // only the emergency entry: hold Power (or hold Cancel + tap OK)
    if (e.hold(Button::Power) || e.combo(Button::Cancel, Button::Ok)) app.replace(makeEmergencyMenu());
  }
  void onTick(App& app) override {
    if (!done_ && app.now() - at_ >= kPostLineMs * (lines_.size() + 3)) {
      done_ = true;
      app.emit(SysEvent::BootDone);
    }
  }
  void onSystem(App& app, SysEvent e) override {
    if (e == SysEvent::BootDone) app.replace(makeBootLogo());
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    const FontEntry& f = theme::small();
    char ver[24];
    std::snprintf(ver, sizeof(ver), "V%.3s  (C) 2026", App::kVersion);
    ui::drawText(fb, f, 4, 6, "PIE CONTROLLER BIOS", col::kWhite);
    ui::drawText(fb, f, 4, 16, ver, col::kWhite);
    const size_t shown = (app.now() - at_) / kPostLineMs;
    for (size_t i = 0; i < lines_.size() && i < shown; i++)
      ui::drawText(fb, f, 4, (int16_t)(36 + 10 * i), lines_[i].c_str(), col::kGray);
    if (shown > lines_.size()) ui::drawText(fb, f, 4, 116, "STARTING...", col::kWhite);
  }

 private:
  uint32_t at_ = 0;
  bool done_ = false;
  std::vector<std::string> lines_;
};

// ---------------- M2 boot logo ----------------

class BootLogo : public Screen {
 public:
  const char* code() const override { return "M2"; }
  std::string title() const override { return "Boot"; }
  bool statusBar() const override { return false; }
  void onEnter(App& app) override { at_ = app.now(); }
  void onTick(App& app) override {
    if (!left_ && app.now() - at_ >= kLogoMinMs) {
      left_ = true;
      app.replace(makeLockScreen(app), fade(400));
    }
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    if (const PicEntry* s = theme::splash()) {
      ui::drawPic(fb, 0, 0, *s);
      return;
    }
    ui::drawPic(fb, 26, 56, assets::kBootLogo);
    tk::loadingAnim(fb, 58, 119, app.now() - at_);
  }

 private:
  uint32_t at_ = 0;
  bool left_ = false;
};

// ---------------- M3 lock screen / M4 PIN ----------------

std::unique_ptr<Screen> makePinEntry();

class LockScreen : public Screen {
 public:
  explicit LockScreen(bool pin) : pin_(pin) {}
  const char* code() const override { return pin_ ? "M3b" : "M3a"; }
  std::string title() const override { return "Lock"; }
  bool statusBar() const override { return false; }
  void onInput(App& app, const InputEvent& e) override {
    if (!e.tap(Button::Ok)) return;
    if (pin_) {
      if (app.security().lockedOut(app.now())) app.push(makeLockedOut([](App& a) { a.pop(); }));
      else app.push(makePinEntry());
    } else {
      unlock(app);
    }
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    // battery where the status bar has it
    const int pct = app.battery().percent();
    const std::string p = pct < 0 ? "?" : std::to_string(pct) + "%";
    ui::drawTextRight(fb, theme::small(), ui::kStatusPctX + 24, ui::kStatusTextY, p.c_str(), col::kWhite);
    ui::drawPic(fb, ui::kStatusBatX, 1, theme::icon("battery"));
    bigTime(fb, 28, clockText(app), 3, col::kWhite);
    ui::drawTextCentered(fb, theme::small(), 66, tk::upper(app.settings().lockMessage).c_str(), col::kGray);
    if (pin_) ui::drawPic(fb, 56, 128, theme::icon("lock_new"), 2);
  }

 private:
  bool pin_;
};

void onPinResult(App& app, Security::Result r);

std::unique_ptr<Screen> makePinEntry() {
  return std::make_unique<DigitEntryScreen>(
      "M4a", "ENTER PIN", Security::kPinLen,
      [](App& app, const std::string& pin) { onPinResult(app, app.security().checkPin(pin, app.now())); },
      [](App& app) { app.pop(); });
}

std::unique_ptr<Screen> makeWrongPin(const char* code, const char* title, std::function<void(App&)> again) {
  auto s = std::make_unique<PageScreen>(code, "");
  s->noStatus().noBottom();
  const int left = 0;
  (void)left;
  s->custom([title](App& app, ui::Framebuffer& fb) {
    ui::drawTextCentered(fb, theme::small(), 22, title, col::kWhite);
    tk::digitBoxes(fb, std::strcmp(title, "EMERGENCY CODE") == 0 ? Security::kCodeLen : Security::kPinLen, 0, ' ');
    // the box drawn "current" is empty here: redraw it as an empty dark box
    ui::drawTextCentered(fb, theme::small(), 88, "WRONG PIN", col::kWhite);
    const int n = app.security().triesLeft();
    const std::string t = std::to_string(n) + (n == 1 ? " TRY LEFT" : " TRIES LEFT");
    ui::drawTextCentered(fb, theme::small(), 100, t.c_str(), col::kGray);
  });
  s->timer(kWrongPinMs, std::move(again));
  return s;
}

void onPinResult(App& app, Security::Result r) {
  switch (r) {
    case Security::Result::Ok:
      unlock(app);
      break;
    case Security::Result::Wrong:
      app.replace(makeWrongPin("M4b", "ENTER PIN", [](App& a) { a.replace(makePinEntry()); }));
      break;
    case Security::Result::LockedOut:
      app.replace(makeLockedOut([](App& a) { a.pop(); }));
      break;
  }
}

// ---------------- M5 home ----------------

class Home : public Screen {
 public:
  const char* code() const override { return "M5"; }
  std::string title() const override { return "Home"; }
  uint8_t deferMask() const override { return 0; }
  void onInput(App& app, const InputEvent& e) override {
    if (e.tap(Button::Cancel)) app.push(makeLauncher(app.launcherIndex), slide(Anim::Up, 250));
  }
  void draw(App&, ui::Framebuffer& fb) override {
    // wallpaper: the theme's, or the built-in placeholder (light grey with a cross)
    if (const PicEntry* w = theme::wallpaper()) {
      ui::drawPic(fb, 0, ui::kStatusH, *w);
    } else {
      fb.fillRect(0, ui::kStatusH, ui::kScreenW, ui::kBottomRuleY - ui::kStatusH, col::kLight);
    }
    fb.fillRect(0, ui::kBottomRuleY, ui::kScreenW, ui::kScreenH - ui::kBottomRuleY, col::kBlack);
    fb.hline(0, ui::kBottomRuleY, ui::kScreenW, col::kWhite);
    ui::drawTextCentered(fb, theme::small(), ui::kBottomTextY, "MENU", col::kWhite);
    for (int i = 0; i < 3; i++) {  // up arrows each side: 6x3 triangles
      fb.hline((int16_t)(5 - i), (int16_t)(153 + i), (int16_t)(2 + 2 * i), col::kWhite);
      fb.hline((int16_t)(123 - i), (int16_t)(153 + i), (int16_t)(2 + 2 * i), col::kWhite);
    }
  }
};

// ---------------- M6 launcher ----------------

class Launcher : public Screen {
 public:
  explicit Launcher(int sel) : sel_(sel) {}
  // M6b is the same page scrolled to its end
  const char* code() const override { return first_ + 4 >= kModuleCount ? "M6b" : "M6a"; }
  std::string title() const override { return "Menu"; }
  InputMode inputMode() const override { return InputMode::List; }
  uint8_t deferMask() const override { return 0; }
  void onEnter(App& app) override { select(app, sel_); }
  void onResume(App& app) override {
    app.moduleOpen = false;
    select(app, sel_);
  }
  void onInput(App& app, const InputEvent& e) override {
    if (e.step(Button::Left)) select(app, (sel_ + kModuleCount - 1) % kModuleCount);
    else if (e.step(Button::Right)) select(app, (sel_ + 1) % kModuleCount);
    else if (e.tap(Button::Ok)) {
      app.launcherIndex = sel_;
      app.moduleOpen = true;
      app.push(kModules[sel_].make(app));
    } else if (e.tap(Button::Cancel)) {
      app.launcherIndex = sel_;
      app.pop(slide(Anim::Down, 250));
    }
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    tk::titleBar(fb, "MENU");
    std::vector<tk::Card> cards;
    for (const auto& m : kModules) cards.push_back({m.label, m.icon});
    tk::launcherCards(fb, cards, sel_, first_ * ui::kCardStep, app.now() - selAt_);
    tk::scrollbar(fb, first_, 4, kModuleCount);
    tk::bottomBar(fb, kModules[sel_].label);
  }

 private:
  void select(App& app, int i) {
    if (i != sel_) selAt_ = app.now();
    sel_ = i;
    if (sel_ < first_) first_ = sel_;
    if (sel_ > first_ + 3) first_ = sel_ - 3;
  }
  int sel_, first_ = 0;
  uint32_t selAt_ = 0;
};

std::unique_ptr<Screen> makeLauncher(int sel) { return std::make_unique<Launcher>(sel); }
std::unique_ptr<Screen> makeBootLogo() { return std::make_unique<BootLogo>(); }

// ---------------- M7 emergency ----------------

std::unique_ptr<Screen> makeEmergencyMenu() {
  auto m = std::make_unique<MenuScreen>("M7a", "EMERGENCY", [](App&) {
    std::vector<MenuItem> items;
    MenuItem reset;
    reset.row.label = "RESET PIN";
    reset.onOk = [](App& app) {
      if (app.security().lockedOut(app.now())) {
        app.push(makeLockedOut([](App& a) { a.replace(makeLockScreen(a)); }));
        return;
      }
      app.push(makeCodeCheck(
          "M7b", "EMERGENCY CODE",
          [](App& a) {
            a.security().removePin();
            a.reset(makeBootLogo());
            a.toast("PIN REMOVED");
          },
          [](App& a) { a.pop(); }));
    };
    items.push_back(reset);
    return items;
  }, "Emergency");
  m->onBack([](App& app) { app.reset(makeBootLogo()); });
  m->bottomText([](App&) { return std::string(); });
  return m;
}

}  // namespace

// ---------------- public ----------------

std::unique_ptr<Screen> makeBootStatus() { return std::make_unique<BootStatus>(); }
std::unique_ptr<Screen> makeHome() { return std::make_unique<Home>(); }

std::unique_ptr<Screen> makeLockScreen(App& app) {
  return std::make_unique<LockScreen>(app.security().pinSet());
}

bool isLockScreen(const Screen* s) {
  if (!s) return false;
  const std::string c = s->code();
  return c == "M3a" || c == "M3b" || c == "M4a" || c == "M4b" || c == "M4c";
}

bool powerIsGlobal(App& app) {
  const std::string c = app.screenCode();
  return c != "M1" && c != "M2" && c != "M7a" && c != "M7b";
}

std::unique_ptr<Screen> makeLockedOut(std::function<void(App&)> after) {
  auto s = std::make_unique<PageScreen>("M4c", "");
  s->noStatus().noBottom();
  s->custom([](App& app, ui::Framebuffer& fb) {
    ui::drawTextCentered(fb, theme::small(), 30, "LOCKED", col::kWhite);
    ui::drawTextCentered(fb, theme::small(), 56, "TRY AGAIN IN", col::kGray);
    const uint32_t left = (app.security().lockoutLeftMs(app.now()) + 999) / 1000;
    char t[8];
    std::snprintf(t, sizeof(t), "%02u:%02u", (unsigned)(left / 60 % 100), (unsigned)(left % 60));
    bigTime(fb, 72, t, 2, col::kWhite);
    ui::drawPicTinted(fb, 56, 112, theme::icon("lock_new"), col::kGray, 2);
  });
  bool* fired = new bool(false);
  s->tick([fired](App& app, PageScreen&) {
    if (!*fired && !app.security().lockedOut(app.now())) {
      *fired = true;
      app.emit(SysEvent::LockoutOver);
    }
  });
  std::shared_ptr<bool> owner(fired);
  s->system([after, owner](App& app, SysEvent e) {
    if (e == SysEvent::LockoutOver) after(app);
  });
  return s;
}

std::unique_ptr<Screen> makePinCheck(const char* code, std::function<void(App&)> ok, std::function<void(App&)> leave) {
  std::string c = code;
  return std::make_unique<DigitEntryScreen>(
      code, "ENTER PIN", Security::kPinLen,
      [c, ok, leave](App& app, const std::string& pin) {
        switch (app.security().checkPin(pin, app.now())) {
          case Security::Result::Ok: ok(app); break;
          case Security::Result::Wrong:
            app.toast("WRONG PIN  " + std::to_string(app.security().triesLeft()) + " LEFT");
            break;
          case Security::Result::LockedOut: app.replace(makeLockedOut(leave)); break;
        }
      },
      leave);
}

std::unique_ptr<Screen> makeCodeCheck(const char* code, const char* title, std::function<void(App&)> ok,
                                      std::function<void(App&)> leave) {
  return std::make_unique<DigitEntryScreen>(
      code, title, Security::kCodeLen,
      [ok, leave](App& app, const std::string& c) {
        switch (app.security().checkCode(c, app.now())) {
          case Security::Result::Ok: ok(app); break;
          case Security::Result::Wrong:
            app.toast("WRONG CODE  " + std::to_string(app.security().triesLeft()) + " LEFT");
            break;
          case Security::Result::LockedOut: app.replace(makeLockedOut(leave)); break;
        }
      },
      leave);
}

void checkBattery(App& app) {
  const int pct = app.battery().percent();
  if (pct < 0) return;
  static bool warned = false;
  if (pct <= apps::kPowerOffPct) {
    // flat: save what can be saved and switch off (the toggle switch cuts the battery for real)
    app.saveSettings();
    app.hal().power.powerOff();
    return;
  }
  if (pct <= app.settings().lowBatteryPct && !warned) {
    warned = true;
    app.toast("LOW BATTERY " + std::to_string(pct) + "%", 2000);
    app.beep(1000, 200);
  }
  if (pct > app.settings().lowBatteryPct + 2) warned = false;
}

void saveForDeepSleep(App& app) {
  // what survives deep sleep (RTC memory): launcher row and whether a module was open
  std::string& r = app.hal().power.retained();
  r = std::to_string(app.launcherIndex) + (app.moduleOpen ? ",1" : ",0");
}

void resumeFromDeepSleep(App& app) {
  const std::string r = app.hal().power.retained();
  int sel = 0, open = 0;
  std::sscanf(r.c_str(), "%d,%d", &sel, &open);
  if (sel < 0 || sel >= kModuleCount) sel = 0;
  app.launcherIndex = sel;
  app.reset(makeHome());
  if (open) {
    app.push(makeLauncher(sel));
    app.moduleOpen = true;
    app.push(kModules[sel].make(app));
  }
  app.lockIfPin();
}

}  // namespace shell
}  // namespace app
