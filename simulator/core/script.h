// script.h - JSON mock-scripts: seed a starting state, fire timed events, check assertions. The same
// parsed script runs headless (`sim run`, on a fresh copy of storage) or inside a live session, because
// every event except the checks is a command (core/commands.h) - the same vocabulary as the terminal and
// the window's command log.
//
// {
//   "name": "boot-to-settings",
//   "initial_state": { "battery_percent": 80, "rtc": "2026-09-30T12:45:00", "sd_present": true,
//                      "storage_seed": "seeds/some-folder", "cold_boot": true },
//   "events": [
//     { "t_ms": 0,    "type": "button", "button": "OK", "action": "press" },
//     { "t_ms": 900,  "type": "assert", "check": "screen_equals", "value": "M5" }
//   ]
// }
//
// Event types: every command in core/commands.h (button press/hold become down + up at t + duration),
// plus
//   assert     check + fields:
//                menu_path_equals          value ("Home/Menu/Settings")
//                screen_equals             value (mockup code of the top screen: "M5", "S2", "B-M0")
//                framebuffer_hash_equals   value (16 hex digits, what `sim run` prints as fb_hash)
//                storage_file_exists       path ("sd:/..." or "flash:/...")
//                storage_file_missing      path
//                storage_file_contains     path, text
//                state_equals              field (dotted path into `sim state --json`, e.g. "wifi.state",
//                                          "ble.keys_sent", "buzzer.tones", "power.state"), value
//                event_fired               value (system event id, e.g. "nfc_card_found", among the
//                                          last 16 fired)
//                display_static_ms         value: no display push in the last value ms (a still screen
//                                          must not be re-sent)
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/commands.h"
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
  std::function<void(CmdContext&, std::vector<AssertResult>&)> run;
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

// A fresh copy of <project>/storage (+ seed) in a temp folder. Empty path on error.
std::filesystem::path makeRunStorage(const std::filesystem::path& projectDir, const std::string& seed,
                                     std::string& err);

// Plays a loaded script against a running session; times are relative to start().
class ScriptPlayer {
 public:
  void start(Script script, uint32_t now);
  // Runs every action due up to virtual time t, advancing the simulator to each action's time first.
  void runUntil(CmdContext& ctx, uint32_t t);
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
  std::string finalMenuPath, finalScreen, finalHash;
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
  std::filesystem::path shotFinal;   // optional PNG of the last frame
  bool keepStorage = false;          // leave the per-run storage copy on disk and print its path
};

// Headless: fresh storage copy, boot, play every action, report.
ScriptResult runScript(const std::filesystem::path& scriptPath, const RunOptions& opt);

}  // namespace sim
