#include "core/script.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace sim {

namespace {

std::string str(const json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

using Run = std::function<void(CmdContext&, std::vector<AssertResult>&)>;

}  // namespace

bool loadScript(const fs::path& path, Script& out, std::string& err) {
  out = Script{};
  out.name = path.stem().string();
  json doc;
  try {
    std::ifstream f(path);
    if (!f) {
      err = "can't open " + path.string();
      return false;
    }
    doc = json::parse(f);
  } catch (const std::exception& e) {
    err = std::string("invalid JSON: ") + e.what();
    return false;
  }
  if (doc.contains("name") && doc["name"].is_string()) out.name = doc["name"];

  // ---- initial state ----
  const json init = doc.value("initial_state", json::object());
  if (init.contains("battery_percent")) {
    const json& b = init["battery_percent"];
    out.init.batteryPercent = b.is_number() ? b.get<int>() : -1;
  }
  if (init.contains("rtc")) {
    hal::DateTime t;
    if (init["rtc"].is_string() && MockRtc::parse(init["rtc"], t)) out.init.rtc = t;
    else if (init["rtc"].is_null()) out.init.rtcMissing = true;
    else {
      err = "initial_state.rtc must be an ISO date-time or null";
      return false;
    }
  }
  if (init.contains("sd_present")) out.init.sdPresent = init["sd_present"].get<bool>();
  out.init.storageSeed = str(init, "storage_seed");
  out.init.coldBoot = init.value("cold_boot", true);

  // ---- timeline ----
  struct Timed {
    uint32_t t;
    size_t order;
    Run run;
  };
  std::vector<Timed> timed;
  auto add = [&](uint32_t t, Run fn) { timed.push_back({t, timed.size(), std::move(fn)}); };
  auto command = [&](uint32_t t, json cmd) {
    add(t, [t, cmd](CmdContext& ctx, std::vector<AssertResult>& r) {
      const CmdResult res = runCommand(ctx, cmd);
      if (!res.ok) r.push_back({t, cmd.value("type", std::string()), false, res.error});
      else if (cmd.value("type", std::string()) == "import") r.push_back({t, "import " + cmd.value("file", std::string()), true, res.info});
    });
  };

  const json events = doc.value("events", json::array());
  for (size_t i = 0; i < events.size(); i++) {
    const json& e = events[i];
    const std::string where = "events[" + std::to_string(i) + "]";
    if (!e.contains("t_ms") || !e["t_ms"].is_number_unsigned()) {
      err = where + ": t_ms must be a non-negative integer";
      return false;
    }
    const uint32_t t = e["t_ms"].get<uint32_t>();
    const std::string type = str(e, "type");
    json cmd = e;
    cmd.erase("t_ms");

    if (type == "assert") {
      const std::string check = str(e, "check");
      const std::string want = str(e, "value");
      if (check == "menu_path_equals") {
        add(t, [t, check, want](CmdContext& c, std::vector<AssertResult>& r) {
          const std::string got = c.sim.app().menuPath();
          r.push_back({t, check, got == want, "want \"" + want + "\", got \"" + got + "\""});
        });
      } else if (check == "screen_equals") {
        add(t, [t, check, want](CmdContext& c, std::vector<AssertResult>& r) {
          const std::string got = c.sim.app().screenCode();
          r.push_back({t, check, got == want, "want " + want + ", got " + got + " (" + c.sim.app().menuPath() + ")"});
        });
      } else if (check == "framebuffer_hash_equals") {
        add(t, [t, check, want](CmdContext& c, std::vector<AssertResult>& r) {
          const std::string got = c.sim.display().hash();
          r.push_back({t, check, got == want, "want " + want + ", got " + got});
        });
      } else if (check == "event_fired") {
        add(t, [t, check, want](CmdContext& c, std::vector<AssertResult>& r) {
          const auto& ev = c.sim.app().lastEvents();
          const bool ok = std::find(ev.begin(), ev.end(), want) != ev.end();
          r.push_back({t, check + " " + want, ok, ok ? "fired" : "not among the last events"});
        });
      } else if (check == "display_static_ms") {
        const uint32_t ms = e.value("value", 1000u);
        add(t, [t, check, ms](CmdContext& c, std::vector<AssertResult>& r) {
          const uint32_t since = c.sim.now() - c.sim.display().lastPushAt();
          r.push_back({t, check, since >= ms, "last push " + std::to_string(since) + " ms ago"});
        });
      } else if (check == "storage_file_exists" || check == "storage_file_missing" ||
                 check == "storage_file_contains") {
        hal::Volume v;
        std::string path;
        const std::string spec = str(e, "path");
        if (!MockStorage::parse(spec, v, path)) {
          err = where + ": path must look like sd:/... or flash:/...";
          return false;
        }
        const std::string text = str(e, "text");
        add(t, [t, check, v, path, spec, text](CmdContext& c, std::vector<AssertResult>& r) {
          // checks look at the files on disk, so an ejected SD card doesn't hide what was written
          std::error_code ec;
          const fs::path host = c.sim.storage().hostPath(v, path);
          const bool exists = fs::exists(host, ec);
          if (check == "storage_file_exists") {
            r.push_back({t, check, exists, spec + (exists ? " exists" : " not found")});
          } else if (check == "storage_file_missing") {
            r.push_back({t, check, !exists, spec + (exists ? " exists" : " not found")});
          } else {
            std::string body;
            std::ifstream f(host, std::ios::binary);
            if (f) body.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            const bool ok = exists && body.find(text) != std::string::npos;
            r.push_back({t, check, ok,
                         exists ? spec + (ok ? " contains " : " lacks ") + "\"" + text + "\"" : spec + " not found"});
          }
        });
      } else if (check == "state_equals") {
        const std::string field = str(e, "field");
        const json wantJ = e.value("value", json());
        add(t, [t, check, field, wantJ](CmdContext& c, std::vector<AssertResult>& r) {
          const json got = stateField(c.sim, field);
          r.push_back({t, check + " " + field, got == wantJ, "want " + wantJ.dump() + ", got " + got.dump()});
        });
      } else {
        err = where + ": unknown check '" + check + "'";
        return false;
      }
      continue;
    }
    if (!validateCommand(cmd, err)) {
      err = where + ": " + err;
      return false;
    }
    if (type == "button") {
      const std::string action = e.value("action", std::string("press"));
      if (action == "press" || action == "hold") {
        const uint32_t dur = e.value("duration_ms", action == "press" ? 80u : 800u);
        json down = cmd, up = cmd;
        down["action"] = "down";
        up["action"] = "up";
        down.erase("duration_ms");
        up.erase("duration_ms");
        command(t, down);
        command(t + dur, up);
        continue;
      }
    }
    command(t, cmd);
  }
  std::stable_sort(timed.begin(), timed.end(), [](const Timed& a, const Timed& b) { return a.t < b.t; });
  for (Timed& x : timed) out.actions.push_back({x.t, std::move(x.run)});
  return true;
}

void applyInitialState(const InitialState& init, Simulator& s) {
  if (init.batteryPercent) s.battery().setPercent(*init.batteryPercent);
  if (init.rtc) s.rtc().set(*init.rtc);
  if (init.rtcMissing) s.rtc().setMissing(true);
  if (init.sdPresent) s.storage().setSdPresent(*init.sdPresent);
}

bool copySeed(const fs::path& projectDir, const std::string& seed, const fs::path& storageRoot, std::string& err) {
  if (seed.empty()) return true;
  const fs::path seedDir = projectDir / seed;
  if (!fs::is_directory(seedDir)) {
    err = "storage_seed folder not found: " + seedDir.string();
    return false;
  }
  std::error_code ec;
  fs::copy(seedDir, storageRoot, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) {
    err = "couldn't copy seed " + seedDir.string() + ": " + ec.message();
    return false;
  }
  return true;
}

// ---------------- live player ----------------

void ScriptPlayer::start(Script script, uint32_t now) {
  script_ = std::move(script);
  results_.clear();
  next_ = 0;
  start_ = now;
  active_ = true;
}

void ScriptPlayer::runUntil(CmdContext& ctx, uint32_t t) {
  if (!active_) return;
  while (next_ < script_.actions.size() && start_ + script_.actions[next_].t <= t) {
    ctx.sim.advanceTo(start_ + script_.actions[next_].t);
    script_.actions[next_].run(ctx, results_);
    next_++;
  }
}

// ---------------- headless ----------------

// A fresh copy of the project's storage/ per run, so scripts never modify the checked-in seed data.
fs::path makeRunStorage(const fs::path& projectDir, const std::string& seed, std::string& err) {
  static std::atomic<unsigned> counter{0};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path dir = fs::temp_directory_path() / ("diyf-sim-" + std::to_string(stamp) + "-" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir / "flash", ec);
  fs::create_directories(dir / "sd", ec);
  const fs::path base = projectDir / "storage";
  if (fs::exists(base)) fs::copy(base, dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) {
    err = "couldn't copy " + base.string() + ": " + ec.message();
    return {};
  }
  if (!copySeed(projectDir, seed, dir, err)) return {};
  // .gitkeep placeholders are repo plumbing, not device files
  for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); ++it)
    if (it->path().filename() == ".gitkeep") fs::remove(it->path(), ec);
  return dir;
}

ScriptResult runScript(const fs::path& scriptPath, const RunOptions& opt) {
  ScriptResult res;
  Script script;
  if (!loadScript(scriptPath, script, res.error)) {
    res.name = scriptPath.stem().string();
    return res;
  }
  res.name = script.name;

  const fs::path storageDir = makeRunStorage(opt.projectDir, script.init.storageSeed, res.error);
  if (storageDir.empty()) return res;

  {
    Simulator s(storageDir);
    applyInitialState(script.init, s);
    res.loaded = true;
    s.boot();
    if (!script.init.coldBoot) s.restart(false);
    CmdContext ctx{s, opt.projectDir, false};
    ScriptPlayer player;
    player.start(std::move(script), 0);
    player.runUntil(ctx, player.script().lastT());
    res.asserts = player.results();
    // let the last input settle into a frame
    s.advanceTo(s.now() + Simulator::kTickMs);

    res.finalMenuPath = s.app().menuPath();
    res.finalScreen = s.app().screenCode();
    res.finalHash = s.display().hash();
    res.endMs = s.now();
    if (!opt.shotFinal.empty() && !s.display().writePng(opt.shotFinal, 1))
      std::cerr << "couldn't write " << opt.shotFinal << "\n";
  }

  std::error_code ec;
  if (opt.keepStorage) std::cout << "  storage kept at " << storageDir.string() << "\n";
  else fs::remove_all(storageDir, ec);
  return res;
}

}  // namespace sim
