// importer.h - Import Asset: puts a file exported from Flipper UI Studio into the simulated SD card, where
// the firmware looks for it on the real device.
//
//   theme .zip  ->  sd:/system/theme/<name>/...   (every folder in the zip that holds a theme.ini)
//   .c16 / .b1i ->  sd:/media/<file>
//   .b1f        ->  sd:/system/fonts/<file>
//
// Files are written straight into the storage folder, the way you'd copy them onto the card from a PC,
// so an ejected SD card or the "fail every write" switch doesn't block an import.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/mocks.h"

namespace sim {

struct ImportResult {
  bool ok = false;
  std::string error;                  // why nothing was imported
  std::vector<std::string> written;   // device paths, e.g. "sd:/system/theme/night/theme.ini"
  std::vector<std::string> warnings;  // imported, but the firmware will skip or ignore these parts
  std::vector<std::string> themes;    // theme folder names that were imported
};

ImportResult importAsset(const std::filesystem::path& file, MockStorage& storage);

}  // namespace sim
