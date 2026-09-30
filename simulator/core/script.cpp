#include "core/script.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>

#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace sim {

namespace {

struct Action {
  uint32_t t;
  size_t order;  // keeps script order for equal times
  std::function<void(Simulator&, ScriptResult&)> run;
};

bool parseButton(const std::string& name, hal::Button& out) {
  std::string n = name;
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::toupper(c); });
  if (n == "OK") out = hal::Button::Ok;
  else if (n == "CANCEL" || n == "BACK") out = hal::Button::Cancel;
  else if (n == "LEFT") out = hal::Button::Left;
  else if (n == "RIGHT") out = hal::Button::Right;
  else if (n == "POWER") out = hal::Button::Power;
  else return false;
  return true;
}

// A fresh copy of the project's storage/ per run, so scripts never modify the checked-in seed data.
fs::path makeRunStorage(const fs::path& projectDir, const std::string& seed, std::string& err) {
  static std::atomic<unsigned> counter{0};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path dir = fs::temp_directory_path() /
                 ("diyf-sim-" + std::to_string(stamp) + "-" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir / "flash", ec);
  fs::create_directories(dir / "sd", ec);
  const fs::path base = projectDir / "storage";
  if (fs::exists(base)) fs::copy(base, dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) {
    err = "couldn't copy " + base.string() + ": " + ec.message();
    return {};
  }
  if (!seed.empty()) {
    const fs::path seedDir = projectDir / seed;
    if (!fs::is_directory(seedDir)) {
      err = "storage_seed folder not found: " + seedDir.string();
      return {};
    }
    fs::copy(seedDir, dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
      err = "couldn't copy seed " + seedDir.string() + ": " + ec.message();
      return {};
    }
  }
  // .gitkeep placeholders are repo plumbing, not device files
  for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); ++it)
    if (it->path().filename() == ".gitkeep") fs::remove(it->path(), ec);
  return dir;
}

std::string str(const json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

}  // namespace

ScriptResult runScript(const fs::path& scriptPath, const RunOptions& opt) {
  ScriptResult res;
  res.name = scriptPath.stem().string();

  json doc;
  try {
    std::ifstream f(scriptPath);
    if (!f) {
      res.error = "can't open " + scriptPath.string();
      return res;
    }
    doc = json::parse(f);
  } catch (const std::exception& e) {
    res.error = std::string("invalid JSON: ") + e.what();
    return res;
  }
  if (doc.contains("name") && doc["name"].is_string()) res.name = doc["name"];

  const json init = doc.value("initial_state", json::object());
  std::string err;
  const fs::path storageDir = makeRunStorage(opt.projectDir, str(init, "storage_seed"), err);
  if (storageDir.empty()) {
    res.error = err;
    return res;
  }

  Simulator s(storageDir);
  if (init.contains("battery_percent")) {
    const json& b = init["battery_percent"];
    s.battery().setPercent(b.is_number() ? b.get<int>() : -1);
  }
  if (init.contains("rtc")) {
    hal::DateTime t;
    if (init["rtc"].is_string() && sim::MockRtc::parse(init["rtc"], t)) s.rtc().set(t);
    else if (init["rtc"].is_null()) s.rtc().setMissing(true);
    else {
      res.error = "initial_state.rtc must be an ISO date-time or null";
      return res;
    }
  }
  if (init.contains("sd_present")) s.storage().setSdPresent(init["sd_present"].get<bool>());

  // ---- build the timeline ----
  std::vector<Action> actions;
  size_t order = 0;
  auto add = [&](uint32_t t, std::function<void(Simulator&, ScriptResult&)> fn) {
    actions.push_back({t, order++, std::move(fn)});
  };
  const json events = doc.value("events", json::array());
  for (size_t i = 0; i < events.size(); i++) {
    const json& e = events[i];
    const std::string where = "events[" + std::to_string(i) + "]";
    if (!e.contains("t_ms") || !e["t_ms"].is_number_unsigned()) {
      res.error = where + ": t_ms must be a non-negative integer";
      return res;
    }
    const uint32_t t = e["t_ms"].get<uint32_t>();
    const std::string type = str(e, "type");

    if (type == "button") {
      hal::Button b;
      if (!parseButton(str(e, "button"), b)) {
        res.error = where + ": unknown button '" + str(e, "button") + "'";
        return res;
      }
      const std::string action = e.value("action", std::string("press"));
      if (action == "press" || action == "hold") {
        const uint32_t dur = e.value("duration_ms", action == "press" ? 80u : 800u);
        add(t, [b](Simulator& sm, ScriptResult&) { sm.input().set(b, true); });
        add(t + dur, [b](Simulator& sm, ScriptResult&) { sm.input().set(b, false); });
      } else if (action == "down" || action == "up") {
        const bool down = action == "down";
        add(t, [b, down](Simulator& sm, ScriptResult&) { sm.input().set(b, down); });
      } else {
        res.error = where + ": action must be press, hold, down or up";
        return res;
      }
    } else if (type == "battery") {
      const int pct = e.contains("percent") && e["percent"].is_number() ? e["percent"].get<int>() : -1;
      add(t, [pct](Simulator& sm, ScriptResult&) { sm.battery().setPercent(pct); });
    } else if (type == "rtc") {
      if (e.value("missing", false)) {
        add(t, [](Simulator& sm, ScriptResult&) { sm.rtc().setMissing(true); });
      } else {
        hal::DateTime dt;
        if (!sim::MockRtc::parse(str(e, "time"), dt)) {
          res.error = where + ": rtc needs time (ISO date-time) or missing: true";
          return res;
        }
        add(t, [dt](Simulator& sm, ScriptResult&) { sm.rtc().set(dt); });
      }
    } else if (type == "sd") {
      const bool present = e.value("present", true);
      add(t, [present](Simulator& sm, ScriptResult&) { sm.storage().setSdPresent(present); });
    } else if (type == "storage") {
      const bool fail = e.value("fail_writes", false);
      add(t, [fail](Simulator& sm, ScriptResult&) { sm.storage().setFailWrites(fail); });
    } else if (type == "dump") {
      const fs::path p = str(e, "path");
      add(t, [p](Simulator& sm, ScriptResult&) {
        if (!sm.display().writePbm(p)) std::cerr << "couldn't write " << p << "\n";
      });
    } else if (type == "assert") {
      const std::string check = str(e, "check");
      const json spec = e;
      if (check == "menu_path_equals") {
        const std::string want = str(spec, "value");
        add(t, [t, check, want](Simulator& sm, ScriptResult& r) {
          const std::string got = sm.app().menuPath();
          r.asserts.push_back({t, check, got == want, "want \"" + want + "\", got \"" + got + "\""});
        });
      } else if (check == "framebuffer_hash_equals") {
        const std::string want = str(spec, "value");
        add(t, [t, check, want](Simulator& sm, ScriptResult& r) {
          const std::string got = sm.display().hash();
          r.asserts.push_back({t, check, got == want, "want " + want + ", got " + got});
        });
      } else if (check == "storage_file_exists" || check == "storage_file_missing" ||
                 check == "storage_file_contains") {
        hal::Volume v;
        std::string path;
        const std::string spec_path = str(spec, "path");
        if (!MockStorage::parse(spec_path, v, path)) {
          res.error = where + ": path must look like sd:/... or flash:/...";
          return res;
        }
        const std::string text = str(spec, "text");
        add(t, [t, check, v, path, spec_path, text](Simulator& sm, ScriptResult& r) {
          // checks look at the files on disk, so an ejected SD card doesn't hide what was written
          std::error_code ec;
          const bool exists = fs::exists(sm.storage().hostPath(v, path), ec);
          if (check == "storage_file_exists") {
            r.asserts.push_back({t, check, exists, spec_path + (exists ? " exists" : " not found")});
          } else if (check == "storage_file_missing") {
            r.asserts.push_back({t, check, !exists, spec_path + (exists ? " exists" : " not found")});
          } else {
            std::string body;
            std::ifstream f(sm.storage().hostPath(v, path), std::ios::binary);
            if (f) body.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            const bool ok = exists && body.find(text) != std::string::npos;
            r.asserts.push_back({t, check, ok,
                                 exists ? spec_path + (ok ? " contains " : " lacks ") + "\"" + text + "\""
                                        : spec_path + " not found"});
          }
        });
      } else if (check == "state_equals") {
        const std::string field = str(spec, "field");
        const json want = spec.value("value", json());
        if (field != "invert" && field != "battery_percent" && field != "sd_present") {
          res.error = where + ": state_equals field must be invert, battery_percent or sd_present";
          return res;
        }
        add(t, [t, check, field, want](Simulator& sm, ScriptResult& r) {
          json got;
          if (field == "invert") got = sm.app().settings().invert;
          else if (field == "battery_percent") got = sm.app().battery().percent();
          else got = sm.storage().sdPresent();
          r.asserts.push_back({t, check + " " + field, got == want, "want " + want.dump() + ", got " + got.dump()});
        });
      } else {
        res.error = where + ": unknown check '" + check + "'";
        return res;
      }
    } else {
      res.error = where + ": unknown event type '" + type + "'";
      return res;
    }
  }
  std::stable_sort(actions.begin(), actions.end(), [](const Action& a, const Action& b) { return a.t < b.t; });

  // ---- run ----
  res.loaded = true;
  s.boot(init.value("cold_boot", true));
  for (const Action& a : actions) {
    s.advanceTo(a.t);
    a.run(s, res);
  }
  // let the last input settle into a frame
  s.advanceTo(s.now() + Simulator::kTickMs);

  res.finalMenuPath = s.app().menuPath();
  res.finalHash = s.display().hash();
  res.endMs = s.now();
  if (!opt.dumpFinal.empty() && !s.display().writePbm(opt.dumpFinal))
    std::cerr << "couldn't write " << opt.dumpFinal << "\n";

  std::error_code ec;
  if (opt.keepStorage) std::cout << "  storage kept at " << storageDir.string() << "\n";
  else fs::remove_all(storageDir, ec);
  return res;
}

}  // namespace sim
