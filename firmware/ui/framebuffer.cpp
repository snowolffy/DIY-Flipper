#include "ui/framebuffer.h"

#include <cstring>

namespace ui {

void Framebuffer::clear() { std::memset(buf_, 0, sizeof(buf_)); }

void Framebuffer::set(int16_t x, int16_t y, bool ink) {
  if (x < 0 || x >= kScreenW || y < 0 || y >= kScreenH) return;
  const uint32_t idx = (uint32_t)y * kScreenW + x;
  const uint8_t mask = (uint8_t)(1u << (idx & 7));
  if (ink) buf_[idx >> 3] |= mask;
  else buf_[idx >> 3] &= (uint8_t)~mask;
}

bool Framebuffer::get(int16_t x, int16_t y) const {
  if (x < 0 || x >= kScreenW || y < 0 || y >= kScreenH) return false;
  const uint32_t idx = (uint32_t)y * kScreenW + x;
  return (buf_[idx >> 3] >> (idx & 7)) & 1;
}

void Framebuffer::hline(int16_t x, int16_t y, int16_t w, bool ink) {
  if (y < 0 || y >= kScreenH) return;
  int16_t x0 = x < 0 ? 0 : x;
  int16_t x1 = x + w > kScreenW ? kScreenW : x + w;
  for (int16_t i = x0; i < x1; i++) set(i, y, ink);
}

void Framebuffer::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool ink) {
  for (int16_t j = 0; j < h; j++) hline(x, y + j, w, ink);
}

void Framebuffer::frameRect(int16_t x, int16_t y, int16_t w, int16_t h, bool ink) {
  if (w <= 0 || h <= 0) return;
  hline(x, y, w, ink);
  hline(x, y + h - 1, w, ink);
  for (int16_t j = 1; j < h - 1; j++) {
    set(x, y + j, ink);
    set(x + w - 1, y + j, ink);
  }
}

}  // namespace ui
