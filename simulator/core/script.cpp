#include "core/script.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>

#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace sim {

namespace {

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

std::string str(const json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// 4, "4", "0x04" all read as 4.
uint32_t num(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end()) return 0;
  if (it->is_number_unsigned() || it->is_number_integer()) return it->get<uint32_t>();
  if (it->is_string()) return (uint32_t)std::strtoul(it->get<std::string>().c_str(), nullptr, 0);
  return 0;
}

const char* wifiStateName(hal::WifiState s) {
  switch (s) {
    case hal::WifiState::Off: return "off";
    case hal::WifiState::Idle: return "idle";
    case hal::WifiState::Scanning: return "scanning";
    case hal::WifiState::Connecting: return "connecting";
    case hal::WifiState::Connected: return "connected";
    case hal::WifiState::Failed: return "failed";
  }
  return "?";
}

const char* bleStateName(hal::BleState s) {
  switch (s) {
    case hal::BleState::Off: return "off";
    case hal::BleState::Advertising: return "advertising";
    case hal::BleState::PairingRequest: return "pairing_request";
    case hal::BleState::Connected: return "connected";
    case hal::BleState::BondListFull: return "bond_list_full";
  }
  return "?";
}

const char* kStateFields[] = {"invert",     "battery_percent", "sd_present", "ir_listening", "ir_sent_count",
                              "nfc_polling", "wifi_state",     "wifi_ssid",  "wifi_password", "ble_state",
                              "ble_host",   "ble_keys_sent"};

json stateField(Simulator& s, const std::string& f) {
  if (f == "invert") return s.app().settings().invert;
  if (f == "battery_percent") return s.app().battery().percent();
  if (f == "sd_present") return s.storage().sdPresent();
  if (f == "ir_listening") return s.ir().listening();
  if (f == "ir_sent_count") return s.ir().sentCount();
  if (f == "nfc_polling") return s.nfc().polling();
  if (f == "wifi_state") return wifiStateName(s.wifi().state());
  if (f == "wifi_ssid") return s.wifi().connectedSsid();
  if (f == "wifi_password") return s.wifi().lastPassword();
  if (f == "ble_state") return bleStateName(s.ble().state());
  if (f == "ble_host") return s.ble().hostName();
  if (f == "ble_keys_sent") return s.ble().keysSent();
  return nullptr;
}

using Run = std::function<void(Simulator&, std::vector<AssertResult>&)>;

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

    if (type == "button") {
      hal::Button b;
      if (!parseButton(str(e, "button"), b)) {
        err = where + ": unknown button '" + str(e, "button") + "'";
        return false;
      }
      const std::string action = e.value("action", std::string("press"));
      if (action == "press" || action == "hold") {
        const uint32_t dur = e.value("duration_ms", action == "press" ? 80u : 800u);
        add(t, [b](Simulator& s, std::vector<AssertResult>&) { s.input().set(b, true); });
        add(t + dur, [b](Simulator& s, std::vector<AssertResult>&) { s.input().set(b, false); });
      } else if (action == "down" || action == "up") {
        const bool down = action == "down";
        add(t, [b, down](Simulator& s, std::vector<AssertResult>&) { s.input().set(b, down); });
      } else {
        err = where + ": action must be press, hold, down or up";
        return false;
      }
    } else if (type == "battery") {
      const int pct = e.contains("percent") && e["percent"].is_number() ? e["percent"].get<int>() : -1;
      add(t, [pct](Simulator& s, std::vector<AssertResult>&) { s.battery().setPercent(pct); });
    } else if (type == "rtc") {
      if (e.value("missing", false)) {
        add(t, [](Simulator& s, std::vector<AssertResult>&) { s.rtc().setMissing(true); });
      } else {
        hal::DateTime dt;
        if (!MockRtc::parse(str(e, "time"), dt)) {
          err = where + ": rtc needs time (ISO date-time) or missing: true";
          return false;
        }
        add(t, [dt](Simulator& s, std::vector<AssertResult>&) { s.rtc().set(dt); });
      }
    } else if (type == "sd") {
      const bool present = e.value("present", true);
      add(t, [present](Simulator& s, std::vector<AssertResult>&) { s.storage().setSdPresent(present); });
    } else if (type == "storage") {
      const bool fail = e.value("fail_writes", false);
      add(t, [fail](Simulator& s, std::vector<AssertResult>&) { s.storage().setFailWrites(fail); });
    } else if (type == "ir_signal") {
      hal::IrSignal sig;
      sig.protocol = str(e, "protocol");
      if (sig.protocol.empty()) {
        err = where + ": ir_signal needs a protocol";
        return false;
      }
      sig.address = num(e, "address");
      sig.command = num(e, "command");
      if (e.contains("raw") && e["raw"].is_array())
        for (const json& v : e["raw"]) sig.raw.push_back(v.get<uint16_t>());
      add(t, [sig](Simulator& s, std::vector<AssertResult>&) { s.ir().inject(sig); });
    } else if (type == "nfc_card") {
      const std::string action = e.value("action", std::string("present"));
      if (action == "remove") {
        add(t, [](Simulator& s, std::vector<AssertResult>&) { s.nfc().remove(); });
      } else if (action == "present") {
        hal::NfcCard c;
        c.uid = str(e, "uid");
        c.type = e.value("card_type", std::string("NTAG215"));
        if (e.contains("blocks") && e["blocks"].is_array())
          for (const json& v : e["blocks"]) c.blocks.push_back(v.get<std::string>());
        if (c.uid.empty()) {
          err = where + ": nfc_card present needs a uid";
          return false;
        }
        add(t, [c](Simulator& s, std::vector<AssertResult>&) { s.nfc().place(c); });
      } else {
        err = where + ": nfc_card action must be present or remove";
        return false;
      }
    } else if (type == "wifi") {
      std::optional<std::vector<hal::WifiNetwork>> nets;
      if (e.contains("networks")) {
        nets.emplace();
        for (const json& n : e["networks"])
          nets->push_back({n.value("ssid", std::string()), n.value("rssi", -60), n.value("secured", true)});
      }
      std::optional<bool> ok;
      if (e.contains("next_connect_succeeds")) ok = e["next_connect_succeeds"].get<bool>();
      std::optional<uint32_t> latency;
      if (e.contains("latency_ms")) latency = e["latency_ms"].get<uint32_t>();
      add(t, [nets, ok, latency](Simulator& s, std::vector<AssertResult>&) {
        if (nets) s.wifi().setNetworks(*nets);
        if (ok) s.wifi().setNextConnectSucceeds(*ok);
        if (latency) s.wifi().setLatencyMs(*latency);
      });
    } else if (type == "ble_host") {
      const std::string action = e.value("action", std::string("connect"));
      const std::string name = e.value("name", std::string("Phone"));
      if (action == "connect") add(t, [name](Simulator& s, std::vector<AssertResult>&) { s.ble().hostConnect(name); });
      else if (action == "disconnect") add(t, [](Simulator& s, std::vector<AssertResult>&) { s.ble().hostDisconnect(); });
      else {
        err = where + ": ble_host action must be connect or disconnect";
        return false;
      }
    } else if (type == "ble_bonds") {
      const bool full = e.value("full", true);
      add(t, [full](Simulator& s, std::vector<AssertResult>&) { s.ble().setBondListFull(full); });
    } else if (type == "dump") {
      const fs::path p = str(e, "path");
      add(t, [p](Simulator& s, std::vector<AssertResult>&) {
        if (!s.display().writePbm(p)) std::cerr << "couldn't write " << p << "\n";
      });
    } else if (type == "assert") {
      const std::string check = str(e, "check");
      if (check == "menu_path_equals") {
        const std::string want = str(e, "value");
        add(t, [t, check, want](Simulator& s, std::vector<AssertResult>& r) {
          const std::string got = s.app().menuPath();
          r.push_back({t, check, got == want, "want \"" + want + "\", got \"" + got + "\""});
        });
      } else if (check == "framebuffer_hash_equals") {
        const std::string want = str(e, "value");
        add(t, [t, check, want](Simulator& s, std::vector<AssertResult>& r) {
          const std::string got = s.display().hash();
          r.push_back({t, check, got == want, "want " + want + ", got " + got});
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
        add(t, [t, check, v, path, spec, text](Simulator& s, std::vector<AssertResult>& r) {
          // checks look at the files on disk, so an ejected SD card doesn't hide what was written
          std::error_code ec;
          const fs::path host = s.storage().hostPath(v, path);
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
        const json want = e.value("value", json());
        if (std::none_of(std::begin(kStateFields), std::end(kStateFields),
                         [&](const char* f) { return field == f; })) {
          err = where + ": unknown state_equals field '" + field + "'";
          return false;
        }
        add(t, [t, check, field, want](Simulator& s, std::vector<AssertResult>& r) {
          const json got = stateField(s, field);
          r.push_back({t, check + " " + field, got == want, "want " + want.dump() + ", got " + got.dump()});
        });
      } else {
        err = where + ": unknown check '" + check + "'";
        return false;
      }
    } else {
      err = where + ": unknown event type '" + type + "'";
      return false;
    }
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

void ScriptPlayer::runUntil(Simulator& s, uint32_t t) {
  if (!active_) return;
  while (next_ < script_.actions.size() && start_ + script_.actions[next_].t <= t) {
    s.advanceTo(start_ + script_.actions[next_].t);
    script_.actions[next_].run(s, results_);
    next_++;
  }
}

// ---------------- headless ----------------

namespace {

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

}  // namespace

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
    s.boot(script.init.coldBoot);
    ScriptPlayer player;
    player.start(std::move(script), 0);
    player.runUntil(s, player.script().lastT());
    res.asserts = player.results();
    // let the last input settle into a frame
    s.advanceTo(s.now() + Simulator::kTickMs);

    res.finalMenuPath = s.app().menuPath();
    res.finalHash = s.display().hash();
    res.endMs = s.now();
    if (!opt.dumpFinal.empty() && !s.display().writePbm(opt.dumpFinal))
      std::cerr << "couldn't write " << opt.dumpFinal << "\n";
  }

  std::error_code ec;
  if (opt.keepStorage) std::cout << "  storage kept at " << storageDir.string() << "\n";
  else fs::remove_all(storageDir, ec);
  return res;
}

}  // namespace sim
