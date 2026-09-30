// unit_tests - small checks for firmware logic with exact expected values. No framework; prints each
// failure and exits non-zero if any.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/battery.h"
#include "app/buttons.h"
#include "app/minijson.h"
#include "app/settings.h"
#include "app/theme.h"
#include "core/importer.h"
#include "core/mocks.h"
#include "miniz/miniz.h"

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

static void miniJson() {
  const std::string j = "{\n  \"name\": \"tv \\\"lounge\\\"\",\n  \"address\": \"0x04\",\n  \"n\": 12,\n"
                        "  \"blocks\": [\"AA\", \"BB\"]\n}";
  CHECK(minijson::field(j, "name") == "tv \"lounge\"");
  CHECK(minijson::field(j, "address") == "0x04");
  CHECK(minijson::field(j, "n") == "12");
  CHECK(minijson::field(j, "missing").empty());
  const auto b = minijson::stringArray(j, "blocks");
  CHECK(b.size() == 2 && b[0] == "AA" && b[1] == "BB");
  CHECK(minijson::escape("a\"b") == "a\\\"b");
}

static void storageList() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "diyf-unit-list";
  fs::remove_all(dir);
  sim::MockStorage st(dir);
  std::vector<std::string> names;
  CHECK(!st.list(hal::Volume::Sd, "/ir", names));
  CHECK(st.write(hal::Volume::Sd, "/ir/b.json", "{}"));
  CHECK(st.write(hal::Volume::Sd, "/ir/a.json", "{}"));
  CHECK(st.list(hal::Volume::Sd, "/ir", names) && names.size() == 2 && names[0] == "a.json");
  st.setSdPresent(false);
  CHECK(!st.list(hal::Volume::Sd, "/ir", names));
  fs::remove_all(dir);
}

static void themeFiles() {
  std::string err;
  theme::Image img;
  CHECK(!theme::parseB1i("B1I", img, err));
  const std::string ok = std::string("B1I\x01", 4) + std::string("\x02\x00\x02\x00\x01\x00\x00\x00", 8) + "\x0F";
  CHECK(theme::parseB1i(ok, img, err) && img.w == 2 && img.h == 2 && img.data.size() == 1);
  CHECK(!theme::parseB1i(std::string("B1I\x02", 4) + ok.substr(4), img, err));  // unknown version
  theme::Font font;
  CHECK(!theme::parseB1f(std::string("B1F\x01\x06\x08\x05\x00", 8) + "ab", font, err));  // truncated
}

static void importer() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "diyf-unit-import";
  fs::remove_all(dir);
  fs::create_directories(dir);
  sim::MockStorage st(dir / "storage");

  // a zip that tries to write outside the theme folder
  const fs::path evil = dir / "evil.zip";
  mz_zip_archive z{};
  CHECK(mz_zip_writer_init_file(&z, evil.string().c_str(), 0));
  const char ini[] = "[theme]\nname=x\n";
  mz_zip_writer_add_mem(&z, "t/theme.ini", ini, sizeof(ini) - 1, MZ_DEFAULT_COMPRESSION);
  mz_zip_writer_add_mem(&z, "t/../../../escaped.txt", "x", 1, MZ_DEFAULT_COMPRESSION);
  mz_zip_writer_finalize_archive(&z);
  mz_zip_writer_end(&z);
  const sim::ImportResult r = sim::importAsset(evil, st);
  CHECK(r.ok && r.themes.size() == 1 && r.themes[0] == "t");
  CHECK(!fs::exists(dir / "escaped.txt") && !fs::exists(dir / "storage" / "escaped.txt"));
  CHECK(!r.warnings.empty());

  // no theme.ini at all
  const fs::path empty = dir / "empty.zip";
  mz_zip_archive z2{};
  CHECK(mz_zip_writer_init_file(&z2, empty.string().c_str(), 0));
  mz_zip_writer_add_mem(&z2, "readme.txt", "hi", 2, MZ_DEFAULT_COMPRESSION);
  mz_zip_writer_finalize_archive(&z2);
  mz_zip_writer_end(&z2);
  CHECK(!sim::importAsset(empty, st).ok);

  // wrong extension, and a .b1i that isn't one
  CHECK(!sim::importAsset(dir / "picture.png", st).ok);
  { std::ofstream(dir / "bad.b1i") << "hello"; }
  CHECK(!sim::importAsset(dir / "bad.b1i", st).ok);
  CHECK(!fs::exists(dir / "storage" / "sd" / "media" / "bad.b1i"));
  fs::remove_all(dir);
}

int main() {
  themeFiles();
  importer();
  miniJson();
  storageList();
  batteryTable();
  batteryAveraging();
  buttons();
  rtc();
  settingsFile();
  if (failures == 0) std::printf("unit_tests: all passed\n");
  return failures == 0 ? 0 : 1;
}
