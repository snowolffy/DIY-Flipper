// gfx.h - asset types (the shapes Flipper UI Studio's exports and registry entries use), the screen layout
// shared with the Studio's templates, and drawing helpers.
//
// Pictures/icons/animation frames are RGB565, one uint16_t per pixel, idx = y*w + x; color::kTransparent
// (0xF81F) is not drawn. Fonts are 1bpp masks (idx = y*w + x, byte = idx/8, bit = idx%8, LSB-first) and the
// caller picks the color.
#pragma once

#include <cstdint>

#include "ui/colors.h"
#include "ui/framebuffer.h"

// ---------- asset tables (same field order as the Studio's ui_assets.h) ----------
struct PicEntry {  // pictures, icons, mockups
  const char* name;
  const uint16_t* data;  // w*h RGB565 values
  uint16_t w, h;
};

struct GifEntry {  // multi-frame animations
  const char* name;
  const uint16_t* const* frames;
  int frameCount;
  unsigned long frameDelayMs;
  uint16_t w, h;
};

struct FontEntry {  // fixed-cell bitmap fonts; glyph n = glyphs + n * ((w*h+7)/8)
  const char* name;
  const uint8_t* glyphs;
  const char* charset;  // charset[n] is the character glyph n draws
  uint8_t w, h;
  uint16_t count;
};

namespace ui {

// ---------- layout (Flipper UI Studio templates "Catalog List-new", "Launcher-new", ...) ----------
// Status bar: rows 0-9 content, a 2 px white rule at y 10-11. Clock at x1; BT icon x75, WiFi x83 (only while
// connected); battery text "100%" at x91; battery icon at x117.
constexpr int16_t kStatusH = 12;
constexpr int16_t kStatusTextY = 2;
constexpr int16_t kStatusClockX = 1;
constexpr int16_t kStatusBtX = 75;
constexpr int16_t kStatusWifiX = 83;
constexpr int16_t kStatusPctX = 91;
constexpr int16_t kStatusBatX = 117;
// Title bar: y 12-21, text centered, rule at y 22.
constexpr int16_t kTitleY = 12;
constexpr int16_t kTitleTextY = 14;
constexpr int16_t kTitleRuleY = 22;
// Content: y 23-148 (126 px).
constexpr int16_t kContentY = 23;
constexpr int16_t kContentH = 126;
// Bottom bar: rule at y 149, text at y 151.
constexpr int16_t kBottomRuleY = 149;
constexpr int16_t kBottomTextY = 151;
// Catalog list: 14 px rows (9 fit), frame around the selected row, text at x5, y+3 in the row.
constexpr int16_t kRowH = 14;
constexpr int16_t kRowTextX = 5;
constexpr int16_t kRowTextDy = 3;
constexpr int16_t kRowsVisible = kContentH / kRowH;  // 9
// Launcher cards: 126x26, 29 px apart from y 25; icon 12 px at (x8, top+7), text at (x25, top+9).
constexpr int16_t kCardW = 126, kCardH = 26, kCardStep = 29, kCardTop = 25;
constexpr int16_t kCardIconX = 8, kCardIconDy = 7, kCardTextX = 25, kCardTextDy = 9;
// Scrollbar: 2 px at x126.
constexpr int16_t kScrollbarX = 126, kScrollbarW = 2;

// Centering rule shared with the Studio: an odd leftover pixel goes right/down, the item sits left/up.
constexpr int16_t centerIn(int16_t start, int16_t span, int16_t size) {
  return (int16_t)(start + ((span - size) >= 0 ? (span - size) / 2 : -((1 - (span - size)) / 2)));
}

// ---------- drawing ----------
// Draws the set bits of a w*h mask in color c, scaled by `scale`.
void drawMask(Framebuffer& fb, int16_t x, int16_t y, const uint8_t* data, uint16_t w, uint16_t h, Color c,
              int scale = 1);
// RGB565 picture; transparent pixels are skipped.
void drawPic(Framebuffer& fb, int16_t x, int16_t y, const PicEntry& p, int scale = 1);
// Same, with every white pixel drawn in `white` instead (grey icons for unselected rows, dim when disabled).
void drawPicTinted(Framebuffer& fb, int16_t x, int16_t y, const PicEntry& p, Color white, int scale = 1);
void drawGifFrame(Framebuffer& fb, int16_t x, int16_t y, const GifEntry& g, int frame);

// Returns the x after the last character. Characters missing from the font advance but draw nothing;
// a lowercase letter missing from the font uses its uppercase glyph.
int16_t drawText(Framebuffer& fb, const FontEntry& f, int16_t x, int16_t y, const char* s, Color c, int scale = 1);
int16_t textWidth(const FontEntry& f, const char* s, int scale = 1);
// Centered in [x0, x0 + w) with the shared centering rule.
void drawTextCentered(Framebuffer& fb, const FontEntry& f, int16_t y, const char* s, Color c, int16_t x0 = 0,
                      int16_t w = kScreenW, int scale = 1);
void drawTextRight(Framebuffer& fb, const FontEntry& f, int16_t right, int16_t y, const char* s, Color c);

}  // namespace ui
