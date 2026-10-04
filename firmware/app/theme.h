// theme.h - the fonts, icons, loading animation and boot splash the UI draws with: the built-in set, or a
// theme pack loaded from the SD card (<kRoot>/<name>/, the layout Flipper UI Studio's Theme pack export writes).
//
// Layout sizes are fixed in firmware, so a theme only swaps artwork. A file whose size doesn't fit its slot
// is skipped with a warning and the built-in asset stays; the rest of the theme still loads.
// Images: .c16 (theme format 2, RGB565) or the older 1-bit .b1i (drawn white). Fonts: .b1f (1bpp mask).
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
// The theme's 128x160 boot splash, or nullptr (the boot logo screen then draws the built-in logo).
const PicEntry* splash();
// The theme's 128x137 home screen wallpaper (y 12-148), or nullptr (built-in placeholder).
const PicEntry* wallpaper();
// Keys (Flipper UI Studio names the theme's icon files after the asset): battery (10x8), wifi, bluetooth
// (8x8), ir, nfc, games, wifi_setup, bluetooth_remote, settings (12x12), arrow1, check_new, lock_new (8x8),
// pie1 (24x24), folder_new (10x8), plus_new (7x7), backspace_new (11x9), status_dot_new (6x6).
// An unknown key gives an empty 0x0 picture.
const PicEntry& icon(const char* key);
const GifEntry& loading();  // 12x12 animation, key loading_gif1

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
  std::vector<uint16_t> px;  // frames back to back, RGB565, transparent = ui::color::kTransparent
};
struct Font {
  uint8_t w = 0, h = 0;
  std::string charset;
  std::vector<uint8_t> glyphs;
};
bool parseC16(const std::string& bytes, Image& out, std::string& err);
bool parseB1i(const std::string& bytes, Image& out, std::string& err);  // ink -> white, paper -> transparent
bool parseImage(const std::string& bytes, Image& out, std::string& err);  // either, by magic
bool parseB1f(const std::string& bytes, Font& out, std::string& err);

}  // namespace theme
