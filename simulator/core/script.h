// script.h - JSON mock-scripts: seed a starting state, fire timed events, check assertions.
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
//   button   button: OK|CANCEL|LEFT|RIGHT|POWER, action: press (80 ms tap) | hold (duration_ms, default 800)
//            | down | up
//   battery  percent: 0-100, or "unknown" for no ADC reading
//   rtc      time: ISO date-time, or missing: true
//   sd       present: true|false
//   storage  fail_writes: true|false
//   dump     path: where to write the current screen as a .pbm (relative to the working directory)
//   assert   check + fields:
//              menu_path_equals          value
//              framebuffer_hash_equals   value (16 hex digits, what the runner prints as fb_hash)
//              storage_file_exists       path ("sd:/..." or "flash:/...")
//              storage_file_missing      path
//              storage_file_contains     path, text
//              state_equals              field (invert | battery_percent | sd_present), value
#pragma once

#include <filesystem>
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

ScriptResult runScript(const std::filesystem::path& scriptPath, const RunOptions& opt);

}  // namespace sim
