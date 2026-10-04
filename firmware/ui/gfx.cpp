#include "ui/gfx.h"

#include <cctype>
#include <cstring>

#include "platform/progmem.h"

namespace ui {

void drawMask(Framebuffer& fb, int16_t x, int16_t y, const uint8_t* data, uint16_t w, uint16_t h, Color c,
              int scale) {
  for (uint16_t yy = 0; yy < h; yy++)
    for (uint16_t xx = 0; xx < w; xx++) {
      const uint32_t idx = (uint32_t)yy * w + xx;
      if (!(progmemByte(data + (idx >> 3)) & (1u << (idx & 7)))) continue;
      if (scale == 1) fb.set((int16_t)(x + xx), (int16_t)(y + yy), c);
      else fb.fillRect((int16_t)(x + xx * scale), (int16_t)(y + yy * scale), (int16_t)scale, (int16_t)scale, c);
    }
}

namespace {

void blit(Framebuffer& fb, int16_t x, int16_t y, const uint16_t* data, uint16_t w, uint16_t h, bool tint,
          Color white, int scale) {
  if (!data) return;
  for (uint16_t yy = 0; yy < h; yy++)
    for (uint16_t xx = 0; xx < w; xx++) {
      Color c = progmemWord(data + (uint32_t)yy * w + xx);
      if (c == color::kTransparent) continue;
      if (tint && c == color::kWhite) c = white;
      if (scale == 1) fb.set((int16_t)(x + xx), (int16_t)(y + yy), c);
      else fb.fillRect((int16_t)(x + xx * scale), (int16_t)(y + yy * scale), (int16_t)scale, (int16_t)scale, c);
    }
}

const char* glyphFor(const FontEntry& f, char ch) {
  if (ch == 0) return nullptr;
  const char* hit = std::strchr(f.charset, ch);
  if (!hit && std::islower((unsigned char)ch)) hit = std::strchr(f.charset, std::toupper((unsigned char)ch));
  return hit;
}

}  // namespace

void drawPic(Framebuffer& fb, int16_t x, int16_t y, const PicEntry& p, int scale) {
  blit(fb, x, y, p.data, p.w, p.h, false, 0, scale);
}

void drawPicTinted(Framebuffer& fb, int16_t x, int16_t y, const PicEntry& p, Color white, int scale) {
  blit(fb, x, y, p.data, p.w, p.h, true, white, scale);
}

void drawGifFrame(Framebuffer& fb, int16_t x, int16_t y, const GifEntry& g, int frame) {
  if (g.frameCount <= 0) return;
  blit(fb, x, y, g.frames[frame % g.frameCount], g.w, g.h, false, 0, 1);
}

int16_t drawText(Framebuffer& fb, const FontEntry& f, int16_t x, int16_t y, const char* s, Color c, int scale) {
  const uint16_t bytesPerGlyph = (uint16_t)((f.w * f.h + 7) / 8);
  for (; *s; s++, x = (int16_t)(x + f.w * scale)) {
    const char* hit = glyphFor(f, *s);
    if (hit) drawMask(fb, x, y, f.glyphs + (hit - f.charset) * bytesPerGlyph, f.w, f.h, c, scale);
  }
  return x;
}

int16_t textWidth(const FontEntry& f, const char* s, int scale) {
  return (int16_t)(std::strlen(s) * f.w * scale);
}

void drawTextCentered(Framebuffer& fb, const FontEntry& f, int16_t y, const char* s, Color c, int16_t x0,
                      int16_t w, int scale) {
  drawText(fb, f, centerIn(x0, w, textWidth(f, s, scale)), y, s, c, scale);
}

void drawTextRight(Framebuffer& fb, const FontEntry& f, int16_t right, int16_t y, const char* s, Color c) {
  drawText(fb, f, (int16_t)(right - textWidth(f, s)), y, s, c);
}

}  // namespace ui
