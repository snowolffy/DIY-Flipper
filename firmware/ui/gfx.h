// gfx.h - asset types, the locked layout, and drawing helpers. The layout numbers match the mockup guides
// in Flipper UI Studio; change them there and here together.
#pragma once

#include <cstdint>

#include "ui/framebuffer.h"

// ---------- asset tables (the shapes Flipper UI Studio's Export tab writes entries for) ----------
struct PicEntry {  // pictures, icons, mockups
  const char* name;
  const uint8_t* data;
  uint16_t w, h;
};

struct GifEntry {  // multi-frame animations
  const char* name;
  const uint8_t* const* frames;
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

// ---------- layout ----------
constexpr int16_t kPad = 2;

// Status bar on List, Detail and Text-input screens; Canvas/Game screens get the full 160 px.
// Rows 0-10 content, row 11 the rule.
constexpr int16_t kStatusH = 12;

// List: 16 px rows, 12x12 icon at x=2, label at x=18, selected row inverted. Longer labels scroll on the
// selected row and are clipped elsewhere. Scrollbar only when the list overflows.
constexpr int16_t kListRowH = 16;
constexpr int16_t kListIcon = 12;
constexpr int16_t kListTextX = 18;
constexpr int16_t kScrollbarW = 3;
constexpr int16_t kListVisible = (kScreenH - kStatusH) / kListRowH;                     // 9
constexpr int16_t kListLabelChars = (kScreenW - kListTextX - kScrollbarW - 1) / 8;      // 13

// Detail: 12 px title row (large font) then 10 px label/value rows (small font). A value that doesn't fit
// beside its label takes the whole next row.
constexpr int16_t kTitleH = 12;
constexpr int16_t kDetailRowH = 10;

// Text input: title row, 16 px field, 18 px character carousel at the bottom.
constexpr int16_t kInputBoxH = 16;
constexpr int16_t kCarouselH = 18;

// ---------- drawing ----------
// Draws the set bits of a w*h block (asset bit order) as `ink`; clear bits are left untouched.
// Anything at or right of clipRight is skipped.
void drawBits(Framebuffer& fb, int16_t x, int16_t y, const uint8_t* data, uint16_t w, uint16_t h, bool ink,
              int16_t clipRight = kScreenW);
void drawPic(Framebuffer& fb, int16_t x, int16_t y, const PicEntry& p, bool ink = true);

// Returns the x after the last character. Characters missing from the font advance but draw nothing.
int16_t drawText(Framebuffer& fb, const FontEntry& f, int16_t x, int16_t y, const char* s, bool ink = true,
                 int16_t clipRight = kScreenW);
int16_t drawTextRight(Framebuffer& fb, const FontEntry& f, int16_t right, int16_t y, const char* s, bool ink = true);
int16_t textWidth(const FontEntry& f, const char* s);
// Word-wraps s into lines of `width` pixels starting at (x, y), lineH apart. Returns the y below the last line.
int16_t drawWrapped(Framebuffer& fb, const FontEntry& f, int16_t x, int16_t y, int16_t width, int16_t lineH,
                    const char* s, bool ink = true);

}  // namespace ui
