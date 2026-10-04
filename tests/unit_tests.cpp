// unit_tests - checks with exact expected values for the firmware logic that scripts can't pin down
// precisely, plus the board pin rules and flow coverage. No framework; prints each failure and exits
// non-zero if any.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/app_host.h"
#include "app/battery.h"
#include "app/input.h"
#include "app/security.h"
#include "app/theme.h"
#include "board/board_profile.h"
#include "core/mocks.h"
#include "core/simulator.h"
#include "nlohmann/json.hpp"
#include "ui/framebuffer.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using hal::Button;

static int failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                 \
    }                                                             \
  } while (0)

static void batteryTable() {
  CHECK(app::batteryPercentFromMv(0) == -1);
  CHECK(app::batteryPercentFromMv(4300) == 100);
  CHECK(app::batteryPercentFromMv(3800) == 50);
  CHECK(app::batteryPercentFromMv(3000) == 0);
  for (int p = 0; p <= 100; p++) CHECK(app::batteryPercentFromMv(app::batteryMvFromPercent(p)) == p);
}

// ---- input gestures (flow schema "gestures") ----
struct Rec {
  sim::MockInput in;
  app::InputRecognizer r;
  uint32_t t = 0;
  std::vector<app::InputEvent> got;
  void step(uint32_t ms, uint8_t defer = 0) {
    for (uint32_t end = t + ms; t < end;) {
      t += 10;
      r.update(in, t, defer);
      for (int i = 0; i < r.count(); i++) got.push_back(r.event(i));
    }
  }
  int count(Button b, app::Gesture g) const {
    return (int)std::count_if(got.begin(), got.end(), [&](const app::InputEvent& e) { return e.button == b && e.gesture == g; });
  }
};

static void gestures() {
  {  // tap on press when the screen has no hold meaning for the button
    Rec x;
    x.step(10);
    x.in.set(Button::Ok, true);
    x.step(10);
    CHECK(x.count(Button::Ok, app::Gesture::Tap) == 1);
    x.in.set(Button::Ok, false);
    x.step(30);
    CHECK(x.count(Button::Ok, app::Gesture::Release) == 1);
  }
  {  // deferred: tap on release before 500 ms; a long press gives hold and no tap
    Rec x;
    const uint8_t d = app::bit(Button::Cancel);
    x.step(10, d);
    x.in.set(Button::Cancel, true);
    x.step(100, d);
    CHECK(x.count(Button::Cancel, app::Gesture::Tap) == 0);
    x.in.set(Button::Cancel, false);
    x.step(30, d);
    CHECK(x.count(Button::Cancel, app::Gesture::Tap) == 1);
    x.got.clear();
    x.in.set(Button::Cancel, true);
    x.step(600, d);
    x.in.set(Button::Cancel, false);
    x.step(30, d);
    CHECK(x.count(Button::Cancel, app::Gesture::Hold) == 1);
    CHECK(x.count(Button::Cancel, app::Gesture::Tap) == 0);
  }
  {  // repeat: 500, then 160, 128, 102 ... never under 40 ms
    Rec x;
    x.step(10);
    x.in.set(Button::Right, true);
    x.step(1000);
    std::vector<uint32_t> at;
    uint32_t start = 20;
    for (const auto& e : x.got)
      if (e.gesture == app::Gesture::Repeat) at.push_back(e.durationMs);
    (void)start;
    CHECK(at.size() >= 5);
    if (at.size() >= 3) {
      CHECK(at[0] == 500);
      CHECK(at[1] == 660);
      CHECK(at[2] == 790);  // 128 ms, rounded up to the 10 ms loop
    }
    x.got.clear();
    x.step(3000);
    uint32_t prev = 0;
    bool minOk = true;
    for (const auto& e : x.got)
      if (e.gesture == app::Gesture::Repeat) {
        if (prev && e.durationMs - prev < 40) minOk = false;
        prev = e.durationMs;
      }
    CHECK(minOk);
  }
  {  // combo: hold OK, tap > : the > tap carries held=OK; OK's own tap/release are skipped
    Rec x;
    const uint8_t d = app::bit(Button::Ok);
    x.step(10, d);
    x.in.set(Button::Ok, true);
    x.step(100, d);
    x.in.set(Button::Right, true);
    x.step(30, d);
    x.in.set(Button::Right, false);
    x.step(30, d);
    x.in.set(Button::Ok, false);
    x.step(30, d);
    bool combo = false;
    for (const auto& e : x.got) combo |= e.combo(Button::Ok, Button::Right);
    CHECK(combo);
    CHECK(x.count(Button::Ok, app::Gesture::Tap) == 0);
    CHECK(x.count(Button::Ok, app::Gesture::Release) == 0);
  }
  {  // stale after a screen change: the held button sends nothing until released
    Rec x;
    x.step(10);
    x.in.set(Button::Cancel, true);
    x.step(20);
    x.r.staleAll();
    x.got.clear();
    x.step(800);
    x.in.set(Button::Cancel, false);
    x.step(30);
    CHECK(x.got.empty());
  }
  {  // debounce: a bounce inside 25 ms is ignored
    Rec x;
    x.step(10);
    x.in.set(Button::Ok, true);
    x.step(10);
    x.in.set(Button::Ok, false);
    x.step(10);
    x.in.set(Button::Ok, true);
    x.step(10);
    CHECK(x.count(Button::Ok, app::Gesture::Tap) == 1);
  }
}

// ---- board profile pin rules ----
static void pinRules() {
  std::set<int> used;
  for (int i = 0; i < board::kPinCount; i++) {
    const int g = board::kPins[i].gpio;
    if (g == board::kNoPin) continue;
    CHECK(g >= 0 && g <= board::kMaxGpio);
    for (int r : board::kReservedGpio)
      if (g == r) {
        std::printf("  pin %s uses reserved GPIO %d\n", board::kPins[i].signal, g);
        CHECK(g != r);
      }
    if (used.count(g)) std::printf("  GPIO %d used twice (%s)\n", g, board::kPins[i].signal);
    CHECK(!used.count(g));
    used.insert(g);
  }
  CHECK(board::kBtnOk >= 0 && board::kBtnOk <= board::kWakeGpioMax);
  CHECK(board::kBtnPower >= 0 && board::kBtnPower <= board::kWakeGpioMax);
  CHECK(board::kBatteryAdc >= board::kAdc1Min && board::kBatteryAdc <= board::kAdc1Max);
}

// ---- security: shared counter, doubling lockout, survives a reboot ----
static void security() {
  const fs::path root = fs::temp_directory_path() / "diyf-unit-sec";
  fs::remove_all(root);
  fs::create_directories(root / "flash");
  fs::create_directories(root / "sd");
  {
    sim::Simulator s(root);
    s.boot();
    app::Security& sec = s.app().security();
    sec.setPin("123456");
    uint32_t t = 1000;
    CHECK(sec.checkPin("000000", t) == app::Security::Result::Wrong);
    CHECK(sec.triesLeft() == 2);
    CHECK(sec.checkCode("00000000", t) == app::Security::Result::Wrong);  // same counter
    CHECK(sec.checkPin("000000", t) == app::Security::Result::LockedOut);
    CHECK(sec.lockoutLeftMs(t) == 30000);
    CHECK(sec.checkPin("123456", t + 1000) == app::Security::Result::LockedOut);  // even the right one
    t += 31000;
    for (int i = 0; i < 3; i++) sec.checkPin("000000", t);
    CHECK(sec.lockoutLeftMs(t) == 60000);  // doubled
    // a fresh firmware (reboot) reads the lockout back from flash
    app::Security again;
    again.begin(s.app().hal(), t);
    CHECK(again.lockedOut(t + 1000));
    CHECK(again.checkPin("123456", t + 61000) == app::Security::Result::Ok);
    CHECK(again.checkCode("40917263", t + 61000) == app::Security::Result::Ok);
    // the cap
    app::Security cap;
    cap.begin(s.app().hal(), 0);
    uint32_t tt = 100000;
    for (int round = 0; round < 8; round++) {
      for (int i = 0; i < 3; i++) cap.checkPin("999999", tt);
      CHECK(cap.lockoutLeftMs(tt) <= app::Security::kMaxLockoutS * 1000);
      tt += cap.lockoutLeftMs(tt) + 1;
    }
  }
  fs::remove_all(root);
}

// ---- image formats ----
static void imageFormats() {
  // 2x1 .c16 with its own key 0x0001: pixel 0 transparent, pixel 1 red; a real 0xF81F pixel is nudged
  std::string b = std::string("C16\x01", 4);
  auto u16 = [&](uint16_t v) { b += (char)(v & 255); b += (char)(v >> 8); };
  u16(3), u16(1), u16(1), u16(0), u16(1);
  u16(1), u16(0xF800), u16(0xF81F);
  theme::Image img;
  std::string err;
  CHECK(theme::parseC16(b, img, err));
  CHECK(img.w == 3 && img.h == 1 && img.px.size() == 3);
  if (img.px.size() == 3) {
    CHECK(img.px[0] == ui::color::kTransparent);
    CHECK(img.px[1] == 0xF800);
    CHECK(img.px[2] != ui::color::kTransparent);
  }
  CHECK(!theme::parseC16(b.substr(0, 16), img, err));  // truncated
  // .b1i ink -> white
  std::string o = std::string("B1I\x01", 4);
  auto w16 = [&](uint16_t v) { o += (char)(v & 255); o += (char)(v >> 8); };
  w16(2), w16(1), w16(1), w16(0);
  o += (char)0x01;
  CHECK(theme::parseImage(o, img, err));
  CHECK(img.px.size() == 2 && img.px[0] == ui::color::kWhite && img.px[1] == ui::color::kTransparent);
}

static void dirtyRect() {
  ui::Framebuffer a, b;
  CHECK(ui::diffRect(a.pixels(), b.pixels()).empty());
  b.set(10, 20, 0xFFFF);
  b.set(30, 25, 0x1234);
  const ui::Rect r = ui::diffRect(a.pixels(), b.pixels());
  CHECK(r.x == 10 && r.y == 20 && r.w == 21 && r.h == 6);
}

// ---- flows: every screen of docs/ui/flows/flow-*-new.json is reached by a script, every system event exists ----
static void flowCoverage(const fs::path& repo) {
  std::set<std::string> asserted, fired;
  for (const auto& e : fs::directory_iterator(repo / "sim" / "scripts")) {
    if (e.path().extension() != ".json") continue;
    std::ifstream f(e.path());
    const json doc = json::parse(f, nullptr, false);
    if (doc.is_discarded()) continue;
    for (const auto& ev : doc.value("events", json::array())) {
      if (ev.value("check", "") == "screen_equals") asserted.insert(ev.value("value", ""));
      if (ev.value("check", "") == "event_fired") fired.insert(ev.value("value", ""));
    }
  }
  std::set<std::string> known;
  for (int i = 0; i <= (int)app::SysEvent::WifiLost; i++) known.insert(app::sysEventName((app::SysEvent)i));
  int screens = 0, covered = 0;
  for (const auto& e : fs::directory_iterator(repo / "docs" / "ui" / "flows")) {
    const std::string n = e.path().filename().string();
    if (n.rfind("flow-", 0) != 0 || e.path().extension() != ".json" || n.find("-new") == std::string::npos) continue;
    std::ifstream f(e.path());
    const json doc = json::parse(f);
    for (const auto& st : doc["states"]) {
      const std::string id = st.value("id", "");
      if (st.value("kind", "") != "screen" || id == "n_launcher") continue;
      std::string code = id.substr(2);
      std::replace(code.begin(), code.end(), '_', '-');
      screens++;
      if (asserted.count(code)) covered++;
      else std::printf("  %s: screen %s (%s) is not reached by any script\n", n.c_str(), code.c_str(), st.value("title", "").c_str());
    }
    for (const auto& se : doc.value("systemEvents", json::array())) {
      const std::string ev = se.is_object() ? se.value("id", "") : se.get<std::string>();
      if (!known.count(ev)) std::printf("  %s: system event %s has no firing point in the firmware\n", n.c_str(), ev.c_str());
      CHECK(known.count(ev));
    }
  }
  std::printf("flow coverage: %d/%d screens asserted by scripts, %zu/%zu system events asserted fired\n", covered, screens,
              fired.size(), known.size());
  CHECK(covered == screens);
}

int main(int argc, char** argv) {
  const fs::path repo = argc > 1 ? fs::path(argv[1]) : fs::current_path();
  batteryTable();
  gestures();
  pinRules();
  security();
  imageFormats();
  dirtyRect();
  flowCoverage(repo);
  if (failures) std::printf("%d check(s) failed\n", failures);
  else std::printf("all unit checks passed\n");
  return failures ? 1 : 0;
}
