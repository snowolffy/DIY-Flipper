// theme.h - the fonts, icons and boot splash the UI draws with: the built-in set, or a theme pack loaded
// from the SD card (<kRoot>/<name>/, the layout Flipper UI Studio's Theme pack export writes).
//
// Layout sizes are fixed in firmware, so a theme only swaps artwork. A file whose size doesn't fit its slot
// is skipped with a warning and the built-in asset stays; the rest of the theme still loads.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hal/hal.h"
#include "ui/gfx.h"

namespace theme {

constexpr const char* kRoot = "/system/theme";

const FontEntry& large();  // 8x8
const FontEntry& small();  // 6x8
const PicEntry& splash();  // 128x160
// Keys: battery (10x8), wifi, bluetooth (8x8), ir, nfc, games, wifi_setup, bluetooth_remote, settings (12x12).
// An unknown key gives an empty 0x0 picture.
const PicEntry& icon(const char* key);

// Folder name of the active theme, empty while the built-in set is in use.
const std::string& activeName();
// Theme folders on the SD card, sorted.
std::vector<std::string> available(const hal::Storage& storage);

// Reads <kRoot>/<name>/theme.ini and the files it lists. False (and nothing changes) when the folder or
// theme.ini can't be read; true otherwise, with per-file problems in warnings.
bool load(const hal::Storage& storage, const std::string& name, std::string& err, std::vector<std::string>& warnings);
// Same checks as load() without making the theme active.
bool validate(const hal::Storage& storage, const std::string& name, std::string& err,
              std::vector<std::string>& warnings);
void useBuiltIn();

// ---- file formats (see THEME_FORMAT_SPEC in Flipper UI Studio) ----
struct Image {
  uint16_t w = 0, h = 0, frames = 0, delayMs = 0;
  std::vector<uint8_t> data;  // frames back to back, each (w*h+7)/8 bytes
};
struct Font {
  uint8_t w = 0, h = 0;
  std::string charset;
  std::vector<uint8_t> glyphs;
};
bool parseB1i(const std::string& bytes, Image& out, std::string& err);
bool parseB1f(const std::string& bytes, Font& out, std::string& err);

}  // namespace theme
