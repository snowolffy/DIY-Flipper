// unit_tests - small checks for firmware logic with exact expected values. No framework; prints each
// failure and exits non-zero if any.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app/battery.h"
#include "app/buttons.h"
#include "app/settings.h"
#include "core/mocks.h"

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static void batteryTable() {
  CHECK(app::batteryPercentFromMv(0) == -1);
  CHECK(app::batteryPercentFromMv(4300) == 100);
  CHECK(app::batteryPercentFromMv(4200) == 100);
  CHECK(app::batteryPercentFromMv(3800) == 50);
  CHECK(app::batteryPercentFromMv(3775) == 45);
  CHECK(app::batteryPercentFromMv(3000) == 0);
  // the mock's percent -> mV must read back as the same percent through the firmware table
  for (int p = 0; p <= 100; p++) CHECK(app::batteryPercentFromMv(app::batteryMvFromPercent(p)) == p);
}

static void batteryAveraging() {
  sim::MockBattery bat;
  app::BatteryMonitor mon;
  bat.setMillivolts(4000);
  mon.update(0, bat);
  CHECK(mon.percent() == 80);
  bat.setMillivolts(3600);
  mon.update(500, bat);  // too soon, no new sample
  CHECK(mon.percent() == 80);
  mon.update(1000, bat);  // average of 4000 and 3600 = 3800
  CHECK(mon.millivolts() == 3800);
  CHECK(mon.percent() == 50);
  bat.setMillivolts(0);
  mon.update(2000, bat);
  CHECK(mon.percent() == -1);
}

static void buttons() {
  sim::MockInput in;
  app::Buttons b;
  std::vector<app::ButtonEvent> ev;

  in.set(hal::Button::Ok, true);
  b.update(0, in, ev);
  in.set(hal::Button::Ok, false);
  b.update(80, in, ev);
  CHECK(ev.size() == 1 && ev[0].button == hal::Button::Ok && ev[0].press == app::Press::Short);

  ev.clear();
  in.set(hal::Button::Ok, true);
  b.update(1000, in, ev);
  b.update(1499, in, ev);
  CHECK(ev.empty());
  b.update(1500, in, ev);
  CHECK(ev.size() == 1 && ev[0].press == app::Press::Long);
  in.set(hal::Button::Ok, false);
  b.update(1600, in, ev);
  CHECK(ev.size() == 1);  // no Short after a Long

  // Right repeats while held, OK doesn't
  ev.clear();
  in.set(hal::Button::Right, true);
  b.update(2000, in, ev);
  b.update(2500, in, ev);  // Long
  b.update(2620, in, ev);  // Repeat
  b.update(2740, in, ev);  // Repeat
  CHECK(ev.size() == 3 && ev[1].press == app::Press::Repeat && ev[2].press == app::Press::Repeat);
}

static void rtc() {
  sim::VirtualClock clock;
  sim::MockRtc r(clock);
  hal::DateTime t;
  CHECK(sim::MockRtc::parse("2024-02-28T23:59:30", t));
  r.set(t);
  clock.set(45000);  // +45 s crosses into the leap day
  hal::DateTime n;
  CHECK(r.now(n));
  CHECK(n.year == 2024 && n.month == 2 && n.day == 29 && n.hour == 0 && n.minute == 0 && n.second == 15);
  CHECK(!sim::MockRtc::parse("2024-13-01", t));
  r.setMissing(true);
  CHECK(!r.now(n));
}

static void settingsFile() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "diyf-unit-settings";
  fs::remove_all(dir);
  sim::MockStorage st(dir);
  app::Settings s;
  s.load(st);  // no file yet: defaults
  CHECK(!s.invert);
  s.invert = true;
  CHECK(s.save(st));
  app::Settings loaded;
  loaded.load(st);
  CHECK(loaded.invert);
  st.setFailWrites(true);
  CHECK(!s.save(st));
  st.setFailWrites(false);
  // a file saved by a Windows editor
  CHECK(st.write(hal::Volume::Flash, app::Settings::kPath, "invert = 1\r\n"));
  app::Settings crlf;
  crlf.load(st);
  CHECK(crlf.invert);
  fs::remove_all(dir);
}

int main() {
  batteryTable();
  batteryAveraging();
  buttons();
  rtc();
  settingsFile();
  if (failures == 0) std::printf("unit_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}
