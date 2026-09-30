#include "ui/gfx.h"

#include <cstring>

#include "platform/progmem.h"

namespace ui {

void drawBits(Framebuffer& fb, int16_t x, int16_t y, const uint8_t* data, uint16_t w, uint16_t h, bool ink,
              int16_t clipRight) {
  for (uint16_t yy = 0; yy < h; yy++) {
    for (uint16_t xx = 0; xx < w; xx++) {
      if (x + xx >= clipRight) break;
      const uint32_t idx = (uint32_t)yy * w + xx;
      if (progmemByte(data + (idx >> 3)) & (1u << (idx & 7))) fb.set(x + xx, y + yy, ink);
    }
  }
}

void drawPic(Framebuffer& fb, int16_t x, int16_t y, const PicEntry& p, bool ink) {
  drawBits(fb, x, y, p.data, p.w, p.h, ink);
}

int16_t drawText(Framebuffer& fb, const FontEntry& f, int16_t x, int16_t y, const char* s, bool ink,
                 int16_t clipRight) {
  const uint16_t bytesPerGlyph = (uint16_t)((f.w * f.h + 7) / 8);
  for (; *s; s++, x += f.w) {
    if (x >= clipRight) break;
    const char* hit = std::strchr(f.charset, *s);
    if (hit) drawBits(fb, x, y, f.glyphs + (hit - f.charset) * bytesPerGlyph, f.w, f.h, ink, clipRight);
  }
  return x;
}

int16_t drawTextRight(Framebuffer& fb, const FontEntry& f, int16_t right, int16_t y, const char* s, bool ink) {
  return drawText(fb, f, right - textWidth(f, s), y, s, ink);
}

int16_t textWidth(const FontEntry& f, const char* s) { return (int16_t)(std::strlen(s) * f.w); }

}  // namespace ui
