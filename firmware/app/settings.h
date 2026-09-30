// settings.h - user settings, kept in flash as /settings.ini (key=value lines) so they survive reboots.
#pragma once

#include <string>

#include "hal/hal.h"

namespace app {

struct Settings {
  bool invert = false;  // Settings > Invert display: white background, black ink
  std::string theme;    // Settings > Theme: folder under /system/theme on the SD card, empty = built-in

  static constexpr const char* kPath = "/settings.ini";

  // Missing or unreadable file leaves the defaults. Unknown keys are ignored.
  void load(const hal::Storage& storage);
  bool save(hal::Storage& storage) const;
};

}  // namespace app
