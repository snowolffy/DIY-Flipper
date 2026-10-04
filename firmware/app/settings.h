// settings.h - user settings, kept in flash as /settings.ini (key=value lines) so they survive reboots.
// Every change is saved at once when the user confirms it (OK), so a flat battery loses nothing.
#pragma once

#include <cstdint>
#include <string>

#include "hal/hal.h"

namespace app {

enum class SleepMode : uint8_t { Deep, Light, Off, Never };
const char* sleepModeName(SleepMode m);  // "Deep", "Light", "Off", "Never"

struct Settings {
  // Display
  int brightness = 80;  // %, 10-100 in steps of 10
  int dimAfterS = 20;   // idle seconds before the backlight dims; 0 = never
  // Power
  SleepMode sleepMode = SleepMode::Deep;
  int sleepAfterMin = 3;  // idle minutes before sleeping (hidden when Never)
  int lowBatteryPct = 15;
  // Sound
  bool buttonSound = true;
  bool notifySound = true;
  int volume = 70;  // %, kept for later: the passive buzzer has no volume control
  // Lock screen
  std::string lockMessage = "Welcome home";
  // Date & time
  bool clock24h = true;
  // Theme folder under /system/theme on the SD card, empty = built-in
  std::string theme;

  static constexpr const char* kPath = "/settings.ini";
  static constexpr size_t kLockMessageMax = 20;  // one line of the 6x8 font

  // Missing or unreadable file leaves the defaults. Unknown keys are ignored.
  void load(const hal::Storage& storage);
  bool save(hal::Storage& storage) const;
};

}  // namespace app
