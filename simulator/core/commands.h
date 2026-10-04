// commands.h - the one way to change the emulated world. A command is a JSON object
// {"type": "...", ...fields} - exactly a script event without its t_ms - and the window, the terminal
// (`sim set ...`, `sim press ...`) and scripts all send these, so a command log turns into a script
// line for line.
//
// Commands
//   button      button: OK|CANCEL|LEFT|RIGHT|POWER, action: down | up | press (down, 80 ms, up) |
//               hold (down, duration_ms (default 800), up). press/hold advance virtual time.
//   wait        ms: advance virtual time
//   battery     percent: 0-100, or "unknown" (no ADC reading)
//   usb         plugged: true | false | "unknown"
//   rtc         time: ISO date-time | "pc" (this computer's local time), or missing: true
//   sd          present: true|false
//   storage     fail_writes: true|false
//   ir_signal   protocol: NEC|Samsung|Sony|RC5|RAW, address, command (numbers or "0x.." strings),
//               raw: [us, ...] or raw_file: path to a timings text file (relative to the project)
//   nfc_card    action: present|remove, uid, card_type, blocks: ["hex", ...], magic: bool;
//               or dump: "sd:/nfc/NAME.nfc" (the card a saved dump was read from)
//   nfc_reader  a reader taps the device while it emulates a card
//   nfc_module  present: true|false (PN532 answers or not)
//   wifi        any of: networks: [{ssid, rssi, secured}], next_connect_succeeds, latency_ms, ntp_responds,
//               drop: true (the access point goes away while connected)
//   ble_host    action: connect|disconnect, name
//   ble_bonds   full: true|false (fill the bond list with placeholder devices, or clear them)
//   power_switch on: true|false (the toggle between battery and boost converter)
//   restart     cold_boot: true|false (default true); false = wake as from deep sleep
//   import      file: path relative to the project (.c16, .b1i, .b1f or theme .zip), as Import Asset
//   dump        path: PNG of the screen, scale: 1-8 (relative to the project)
#pragma once

#include <filesystem>
#include <string>

#include "core/simulator.h"
#include "nlohmann/json.hpp"

namespace sim {

using json = nlohmann::json;

struct CmdContext {
  Simulator& sim;
  std::filesystem::path projectDir;  // relative paths in commands resolve against it
  bool virtualTime = true;           // press/hold/wait advance the clock themselves
};

struct CmdResult {
  bool ok = true;
  std::string error;
  std::string info;  // e.g. import summary
};

// Checks a command without running it (scripts validate everything before they start).
bool validateCommand(const json& cmd, std::string& err);
CmdResult runCommand(CmdContext& ctx, const json& cmd);

bool parseButton(const std::string& name, hal::Button& out);
const char* buttonName(hal::Button b);

// Everything the window and `sim state` show: menu path, screen code, every mock, power, display.
json stateJson(Simulator& s);
// One field of the state for script checks (state_equals). null when unknown.
json stateField(Simulator& s, const std::string& field);

}  // namespace sim
