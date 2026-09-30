// script.h - JSON mock-scripts: seed a starting state, fire timed events, check assertions. The same
// parsed script runs headless (runScript, on a fresh copy of storage) or inside the live window
// (ScriptPlayer, on the running simulator), because every event goes through the mocks' own setters.
//
// {
//   "name": "boot-to-settings",
//   "initial_state": { "battery_percent": 80, "rtc": "2026-09-30T12:45:00", "sd_present": true,
//                      "storage_seed": "seeds/some-folder", "cold_boot": true },
//   "events": [
//     { "t_ms": 0,    "type": "button", "button": "OK", "action": "press" },
//     { "t_ms": 900,  "type": "assert", "check": "menu_path_equals", "value": "Main" }
//   ]
// }
//
// Event types
//   button     button: OK|CANCEL|LEFT|RIGHT|POWER, action: press (80 ms tap) | hold (duration_ms, default 800)
//              | down | up
//   battery    percent: 0-100, or "unknown" for no ADC reading
//   rtc        time: ISO date-time, or missing: true
//   sd         present: true|false
//   storage    fail_writes: true|false
//   ir_signal  protocol: NEC|Samsung|Sony|RC5|RAW, address, command (numbers or "0x.." strings), raw: [us, ...]
//   nfc_card   action: present|remove, uid, card_type, blocks: ["hex", ...]
//   wifi       any of: networks: [{ssid, rssi, secured}], next_connect_succeeds: bool, latency_ms
//   ble_host   action: connect|disconnect, name
//   ble_bonds  full: true|false (fill the bond list with placeholder devices, or clear them)
//   import     file: path relative to the project folder (.b1i, .b1f or theme .zip), as Import Asset does
//   restart    cold_boot: true|false (default true) - power-cycle the device
//   dump       path: where to write the current screen as a .pbm
//   assert     check + fields:
//                menu_path_equals          value
//                framebuffer_hash_equals   value (16 hex digits, what the runner prints as fb_hash)
//                storage_file_exists       path ("sd:/..." or "flash:/...")
//                storage_file_missing      path
//                storage_file_contains     path, text
//                state_equals              field, value. Fields: invert, battery_percent, sd_present,
//                                          ir_listening, ir_sent_count, nfc_polling, wifi_state, wifi_ssid,
//                                          wifi_password, ble_state, ble_host, ble_keys_sent, theme
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/simulator.h"

namespace sim {

struct AssertResult {
  uint32_t tMs;
  std::string check;
  bool pass;
  std::string detail;
};

struct InitialState {
  std::optional<int> batteryPercent;  // -1 = no reading
  std::optional<hal::DateTime> rtc;
  bool rtcMissing = false;
  std::optional<bool> sdPresent;
  std::string storageSeed;  // folder relative to the project, copied over storage/
  bool coldBoot = true;
};

struct ScriptAction {
  uint32_t t;
  std::function<void(Simulator&, std::vector<AssertResult>&)> run;
};

struct Script {
  std::string name;
  InitialState init;
  std::vector<ScriptAction> actions;  // sorted by t, script order kept for equal times
  uint32_t lastT() const { return actions.empty() ? 0 : actions.back().t; }
};

// False with a message in err if the file can't be read or has an unknown event/check.
bool loadScript(const std::filesystem::path& path, Script& out, std::string& err);

// Battery, RTC and SD settings (not the seed or cold_boot, which depend on how the script is run).
void applyInitialState(const InitialState& init, Simulator& s);

// Copies <project>/<seed> over storageRoot. Empty seed is a no-op.
bool copySeed(const std::filesystem::path& projectDir, const std::string& seed,
              const std::filesystem::path& storageRoot, std::string& err);

// Plays a loaded script against a simulator that is already running; times are relative to start().
class ScriptPlayer {
 public:
  void start(Script script, uint32_t now);
  // Runs every action due up to virtual time t, advancing the simulator to each action's time first.
  void runUntil(Simulator& s, uint32_t t);
  bool active() const { return active_; }
  bool finished() const { return active_ && next_ >= script_.actions.size(); }
  const Script& script() const { return script_; }
  const std::vector<AssertResult>& results() const { return results_; }
  void stop() { active_ = false; }

 private:
  Script script_;
  std::vector<AssertResult> results_;
  size_t next_ = 0;
  uint32_t start_ = 0;
  bool active_ = false;
};

struct ScriptResult {
  std::string name;
  bool loaded = false;
  std::string error;  // parse / setup problem; the script didn't run
  std::vector<AssertResult> asserts;
  std::string finalMenuPath;
  std::string finalHash;
  uint32_t endMs = 0;

  bool passed() const {
    if (!loaded) return false;
    for (const auto& a : asserts)
      if (!a.pass) return false;
    return true;
  }
};

struct RunOptions {
  std::filesystem::path projectDir;  // holds storage/ (copied per run) and seed folders
  std::filesystem::path dumpFinal;   // optional .pbm of the last frame
  bool keepStorage = false;          // leave the per-run storage copy on disk and print its path
};

// Headless: fresh storage copy, boot, play every action, report.
ScriptResult runScript(const std::filesystem::path& scriptPath, const RunOptions& opt);

}  // namespace sim
