// settings.cpp - the Settings-new flow: T0 menu, Display (T1/T1e), Power (T2/T2p), Sound (T3), Lock
// Screen (K1a/K1b, K3a-c, K5, K6), Date & Time (D0/D0e), System (Y1-Y6) and Factory Reset.
#include <cstdio>

#include "app/modules.h"
#include "app/shell.h"
#include "app/theme.h"
#include "app/valuelist.h"
#include "app/widgets.h"
#include "ui/gfx.h"

namespace app {
namespace settings {

using hal::Button;
namespace col = ui::color;

namespace {

std::string onOff(bool b) { return b ? "ON" : "OFF"; }
std::string pct(int v) { return std::to_string(v) + "%"; }

ValueRow number(const char* label, std::vector<int> steps, std::function<int&(App&)> ref,
                std::function<std::string(int)> fmt, std::function<void(App&, int)> live = nullptr) {
  ValueRow r;
  r.label = label;
  r.kind = ValueRow::Number;
  r.steps = std::move(steps);
  r.get = [ref](App& a) { return ref(a); };
  r.set = [ref, live](App& a, int v) {
    ref(a) = v;
    if (live) live(a, v);
  };
  r.format = std::move(fmt);
  return r;
}

ValueRow toggle(const char* label, std::function<bool&(App&)> ref) {
  ValueRow r;
  r.label = label;
  r.kind = ValueRow::Toggle;
  r.value = [ref](App& a) { return onOff(ref(a)); };
  r.onOk = [ref](App& a) { ref(a) = !ref(a); };
  return r;
}

ValueRow action(const char* label, Action ok) {
  ValueRow r;
  r.label = label;
  r.onOk = std::move(ok);
  return r;
}

// ---------------- Display / Power / Sound ----------------

std::unique_ptr<Screen> makeDisplay() {
  return std::make_unique<ValueListScreen>("T1", "T1e", "DISPLAY", [](App&) {
    return std::vector<ValueRow>{
        number("BRIGHTNESS", {10, 20, 30, 40, 50, 60, 70, 80, 90, 100},
               [](App& a) -> int& { return a.settings().brightness; }, pct,
               [](App& a, int v) { a.applyBrightness(v); }),
        number("DIM AFTER", {10, 20, 30, 60, 120, 300, 0}, [](App& a) -> int& { return a.settings().dimAfterS; },
               [](int v) { return v == 0 ? std::string("NEVER") : v < 60 ? std::to_string(v) + "S" : std::to_string(v / 60) + " MIN"; }),
    };
  }, "Display");
}

std::unique_ptr<Screen> makeSleepPicker() {
  return std::make_unique<MenuScreen>("T2p", "SLEEP MODE", [](App& app) {
    std::vector<MenuItem> items;
    for (SleepMode m : {SleepMode::Deep, SleepMode::Light, SleepMode::Off, SleepMode::Never}) {
      MenuItem it;
      it.row.label = tk::upper(sleepModeName(m));
      if (app.settings().sleepMode == m) it.row.trail = tk::Trail::Check;
      it.onOk = [m](App& a) {
        a.settings().sleepMode = m;
        a.saveSettings();
        a.pop();
      };
      items.push_back(it);
    }
    return items;
  }, "Sleep mode");
}

std::unique_ptr<Screen> makePower() {
  return std::make_unique<ValueListScreen>("T2", "", "POWER", [](App&) {
    ValueRow mode = action("SLEEP MODE", [](App& a) { a.push(makeSleepPicker()); });
    mode.value = [](App& a) { return tk::upper(sleepModeName(a.settings().sleepMode)); };
    ValueRow after = number("SLEEP AFTER", {1, 2, 3, 5, 10, 15, 30}, [](App& a) -> int& { return a.settings().sleepAfterMin; },
                            [](int v) { return std::to_string(v) + " MIN"; });
    after.visible = [](App& a) { return a.settings().sleepMode != SleepMode::Never; };
    return std::vector<ValueRow>{
        mode, after,
        number("LOW BATTERY", {5, 10, 15, 20, 25, 30}, [](App& a) -> int& { return a.settings().lowBatteryPct; }, pct)};
  }, "Power");
}

std::unique_ptr<Screen> makeSound() {
  return std::make_unique<ValueListScreen>("T3", "", "SOUND", [](App&) {
    return std::vector<ValueRow>{
        toggle("BUTTON SOUND", [](App& a) -> bool& { return a.settings().buttonSound; }),
        toggle("NOTIF SOUND", [](App& a) -> bool& { return a.settings().notifySound; }),
        number("VOLUME", {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100}, [](App& a) -> int& { return a.settings().volume; }, pct),
    };
  }, "Sound");
}

// ---------------- Lock screen ----------------

void backToLockMenu(App& app) {
  int i = app.find("K1b");
  if (i < 0) i = app.find("K1a");
  if (i >= 0) app.popToDepth((size_t)i + 1);
}

std::unique_ptr<Screen> makeNewPin();

std::unique_ptr<Screen> makeConfirmPin(std::string first) {
  return std::make_unique<DigitEntryScreen>(
      "K3b", "CONFIRM PIN", Security::kPinLen,
      [first](App& app, const std::string& again) {
        if (again == first) {
          app.security().setPin(first);
          backToLockMenu(app);
          app.toast("PIN SET");
        } else {
          auto m = std::make_unique<PageScreen>("K3c", "");
          m->lines({"PINS DON'T MATCH", "ENTER A NEW PIN"}).linesY(64, 12).hint("OK=AGAIN");
          m->onOk([](App& a) { a.replace(makeNewPin()); });
          app.replace(std::move(m));
        }
      },
      [](App& app) { app.replace(makeNewPin()); });
}

std::unique_ptr<Screen> makeNewPin() {
  return std::make_unique<DigitEntryScreen>(
      "K3a", "NEW PIN", Security::kPinLen,
      [](App& app, const std::string& pin) { app.replace(makeConfirmPin(pin)); },
      [](App& app) { backToLockMenu(app); });
}

class LockMenu : public MenuScreen {
 public:
  LockMenu()
      : MenuScreen("K1a", "LOCK SCREEN", [](App& app) {
          std::vector<MenuItem> items;
          auto add = [&](const char* label, Action a) {
            MenuItem it;
            it.row.label = label;
            it.onOk = std::move(a);
            items.push_back(it);
          };
          if (!app.security().pinSet()) {
            add("SET PIN", [](App& a) { a.push(makeNewPin()); });
          } else {
            add("CHANGE PIN", [](App& a) {
              a.push(shell::makePinCheck("Kold", [](App& b) { b.replace(makeNewPin()); }, [](App& b) { b.pop(); }));
            });
            add("REMOVE PIN", [](App& a) {
              a.push(shell::makePinCheck(
                  "Krold",
                  [](App& b) {
                    b.replace(std::make_unique<DialogScreen>(
                                  "K5", std::vector<std::string>{"REMOVE PIN?"},
                                  [](App& c) {
                                    c.security().removePin();
                                    c.pop();
                                    c.toast("PIN REMOVED");
                                  }),
                              fade(150));
                  },
                  [](App& b) { b.pop(); }));
            });
          }
          add("LOCK MESSAGE", [](App& a) { a.push(makeLockMessageScreen()); });
          return items;
        }, "Lock screen") {}
  const char* code() const override { return pin_ ? "K1b" : "K1a"; }
  void onEnter(App& app) override {
    pin_ = app.security().pinSet();
    MenuScreen::onEnter(app);
  }
  void onResume(App& app) override {
    pin_ = app.security().pinSet();
    MenuScreen::onResume(app);
  }
  static std::unique_ptr<Screen> makeLockMessageScreen();

 private:
  bool pin_ = false;
};

std::unique_ptr<Screen> LockMenu::makeLockMessageScreen() {
  auto t = std::make_unique<TextInputScreen>(
      "K6", "MESSAGE", std::string(TextInputScreen::kPasswordChars) + " ", "", Settings::kLockMessageMax,
      [](App& app, const std::string& text) {
        app.settings().lockMessage = text;
        app.saveSettings();
        app.pop();
      },
      [](App& app) { app.pop(); });
  t->allowEmpty();
  return t;
}

// ---------------- Date & time ----------------

int daysIn(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : d[m - 1];
}

class DateTimeScreen : public ListScreen {
 public:
  DateTimeScreen() : ListScreen("D0", "DATE & TIME", "Date & time") {}
  const char* code() const override { return editing_ ? "D0e" : "D0"; }
  uint8_t deferMask() const override { return editing_ ? 0 : ListScreen::deferMask(); }
  void onTick(App& app) override {
    if (!editing_ && app.now() - lastReload_ >= 1000) {
      lastReload_ = app.now();
      reload(app);
    }
  }

 protected:
  std::vector<tk::Row> rows(App& app) override {
    hal::DateTime t;
    const bool ok = editing_ ? (t = edit_, true) : app.localTime(t);
    char time[8], date[12];
    if (ok) {
      unsigned h = t.hour;
      if (!app.settings().clock24h && !editing_) h = h % 12 == 0 ? 12 : h % 12;
      std::snprintf(time, sizeof(time), "%02u:%02u", h, (unsigned)t.minute);
      std::snprintf(date, sizeof(date), "%02u/%02u/%04u", (unsigned)t.day, (unsigned)t.month, (unsigned)t.year);
    } else {
      std::snprintf(time, sizeof(time), "--:--");
      std::snprintf(date, sizeof(date), "--/--/----");
    }
    std::vector<tk::Row> r(3);
    r[0].label = "TIME", r[0].value = time;
    r[1].label = "DATE", r[1].value = date;
    r[2].label = "24-HOUR", r[2].value = onOff(app.settings().clock24h);
    for (auto& x : r) x.valueWhite = true;
    return r;
  }
  void ok(App& app, int i) override {
    if (i == 2) {
      app.settings().clock24h = !app.settings().clock24h;
      app.saveSettings();
      reload(app);
      return;
    }
    if (!app.localTime(edit_)) edit_ = hal::DateTime{2026, 1, 1, 12, 0, 0};
    editing_ = true;
    row_ = i;
    field_ = 0;
    reload(app);
  }
  bool editing() const override { return false; }  // drawn by draw() below, not the "< v >" style
  void onInput(App& app, const InputEvent& e) override {
    if (!editing_) {
      ListScreen::onInput(app, e);
      return;
    }
    const int fields = row_ == 0 ? 2 : 3;
    if (e.step(Button::Left) || e.step(Button::Right)) {
      const int d = e.button == Button::Left ? -1 : 1;
      hal::DateTime& t = edit_;
      if (row_ == 0) {
        if (field_ == 0) t.hour = (uint8_t)((t.hour + 24 + d) % 24);
        else t.minute = (uint8_t)((t.minute + 60 + d) % 60);
      } else if (field_ == 0) {
        const int n = daysIn(t.year, t.month);
        t.day = (uint8_t)((t.day - 1 + n + d) % n + 1);
      } else if (field_ == 1) {
        t.month = (uint8_t)((t.month - 1 + 12 + d) % 12 + 1);
        if (t.day > daysIn(t.year, t.month)) t.day = (uint8_t)daysIn(t.year, t.month);
      } else {
        t.year = (uint16_t)(t.year + d < 2024 ? 2099 : t.year + d > 2099 ? 2024 : t.year + d);
        if (t.day > daysIn(t.year, t.month)) t.day = (uint8_t)daysIn(t.year, t.month);
      }
      reload(app);
    } else if (e.tap(Button::Ok)) {
      if (++field_ >= fields) {
        edit_.second = 0;
        app.hal().rtc.set(edit_);  // DS3231 keeps local time
        editing_ = false;
        app.toast("SAVED");
      }
      reload(app);
    } else if (e.tap(Button::Cancel)) {
      editing_ = false;
      reload(app);
    }
  }
  void draw(App& app, ui::Framebuffer& fb) override {
    ListScreen::draw(app, fb);
    if (!editing_) return;
    // box around the field being edited: HH or MM / DD, MM or YYYY of the value text
    const std::string v = rowsShown()[row_].value;
    const int16_t x0 = (int16_t)(121 - 6 * (int)v.size());
    static const int kStart[2][3] = {{0, 3, 0}, {0, 3, 6}};
    static const int kLen[2][3] = {{2, 2, 0}, {2, 2, 4}};
    const int16_t y = (int16_t)(ui::kContentY + row_ * ui::kRowH + 1);
    fb.frameRect((int16_t)(x0 + 6 * kStart[row_][field_] - 3), y, (int16_t)(6 * kLen[row_][field_] + 3), 12, col::kWhite);
  }

 private:
  bool editing_ = false;
  int row_ = 0, field_ = 0;
  hal::DateTime edit_;
  uint32_t lastReload_ = 0;
};

// ---------------- System ----------------

std::string size(uint64_t bytes) {
  char b[16];
  const double mb = (double)bytes / (1024.0 * 1024.0);
  if (mb < 10) std::snprintf(b, sizeof(b), "%.1f", mb);
  else std::snprintf(b, sizeof(b), "%.0f", mb);
  return b;
}

std::unique_ptr<Screen> makeStorage() {
  auto p = std::make_unique<PageScreen>("Y2", "STORAGE", "Storage");
  p->kv([](App& app) {
    std::vector<tk::KV> rows;
    uint64_t used, total;
    for (auto v : {hal::Volume::Flash, hal::Volume::Sd}) {
      const char* name = v == hal::Volume::Flash ? "FLASH" : "SD";
      if (app.hal().storage.usage(v, used, total)) rows.push_back({name, size(used) + "/" + size(total) + "M"});
      else rows.push_back({name, v == hal::Volume::Sd ? "NO CARD" : "ERROR"});
    }
    return rows;
  });
  p->onCancel([](App& a) { a.pop(); });
  return p;
}

std::unique_ptr<Screen> makeFirmware() {
  auto p = std::make_unique<PageScreen>("Y6", "FIRMWARE", "Firmware");
  p->kv([](App&) {
    return std::vector<tk::KV>{{"VERSION", App::kVersion}, {"CODENAME", tk::upper(App::kCodename)}};
  });
  // the joke slot: hold OK for a while on this page
  p->custom([](App& app, ui::Framebuffer& fb) {
    (void)app;
    (void)fb;
  });
  p->onCancel([](App& a) { a.pop(); });
  return p;
}

enum class Scope { SettingsOnly, Radios, Everything };

void factoryReset(App& app, Scope s) {
  hal::Hal& h = app.hal();
  h.storage.remove(hal::Volume::Flash, Settings::kPath);
  if (s != Scope::SettingsOnly) {
    h.storage.remove(hal::Volume::Flash, "/wifi.ini");
    for (const std::string& b : h.ble.bonds()) h.ble.forget(b);
    h.wifi.disconnect();
  }
  if (s == Scope::Everything) app.security().wipe();
  // files on the SD card are kept
  h.power.restart();
}

std::unique_ptr<Screen> makeResetScope() {
  return std::make_unique<MenuScreen>("Y3", "RESET", [](App&) {
    std::vector<MenuItem> items;
    struct S { const char* label; Scope scope; std::vector<std::string> lines; };
    const S scopes[] = {
        {"SETTINGS ONLY", Scope::SettingsOnly, {"RESET SETTINGS?"}},
        {"SETTINGS+WIFI/BT", Scope::Radios, {"RESET SETTINGS,", "WIFI AND BT?"}},
        {"EVERYTHING", Scope::Everything, {"RESET EVERYTHING?", "PIN WILL BE", "REMOVED"}},
    };
    for (const S& s : scopes) {
      MenuItem it;
      it.row.label = s.label;
      const Scope scope = s.scope;
      const std::vector<std::string> lines = s.lines;
      it.onOk = [scope, lines](App& a) {
        auto leave = [](App& b) { b.popTo("Y3"); };
        if (a.security().lockedOut(a.now())) {
          a.push(shell::makeLockedOut(leave));
          return;
        }
        a.push(shell::makeCodeCheck("Y4", "ENTER CODE",
                                    [scope, lines](App& b) {
                                      b.replace(std::make_unique<DialogScreen>(
                                                    "Y5", lines, [scope](App& c) { factoryReset(c, scope); },
                                                    [](App& c) { c.pop(); }),
                                                fade(150));
                                    },
                                    leave));
      };
      items.push_back(it);
    }
    return items;
  }, "Reset");
}

std::unique_ptr<Screen> makeSystem() {
  return std::make_unique<MenuScreen>("Y1", "SYSTEM", [](App&) {
    std::vector<MenuItem> items(3);
    items[0].row.label = "STORAGE", items[0].onOk = [](App& a) { a.push(makeStorage()); };
    items[1].row.label = "FACTORY RESET", items[1].onOk = [](App& a) { a.push(makeResetScope()); };
    items[2].row.label = "FIRMWARE", items[2].onOk = [](App& a) { a.push(makeFirmware()); };
    return items;
  }, "System");
}

}  // namespace

std::unique_ptr<Screen> makeMenu(App&) {
  return std::make_unique<MenuScreen>("T0", "SETTINGS", [](App&) {
    std::vector<MenuItem> items(6);
    items[0].row.label = "DISPLAY", items[0].onOk = [](App& a) { a.push(makeDisplay()); };
    items[1].row.label = "POWER", items[1].onOk = [](App& a) { a.push(makePower()); };
    items[2].row.label = "SOUND", items[2].onOk = [](App& a) { a.push(makeSound()); };
    items[3].row.label = "LOCK SCREEN", items[3].onOk = [](App& a) { a.push(std::make_unique<LockMenu>()); };
    items[4].row.label = "DATE & TIME", items[4].onOk = [](App& a) { a.push(std::make_unique<DateTimeScreen>()); };
    items[5].row.label = "SYSTEM", items[5].onOk = [](App& a) { a.push(makeSystem()); };
    return items;
  }, "Settings");
}

}  // namespace settings
}  // namespace app
