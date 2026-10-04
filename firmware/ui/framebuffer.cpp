#include "ui/framebuffer.h"

#include <cstring>

#include "ui/colors.h"

namespace ui {

Rect Rect::intersect(const Rect& o) const {
  const int16_t x0 = x > o.x ? x : o.x, y0 = y > o.y ? y : o.y;
  const int16_t x1 = (x + w) < (o.x + o.w) ? (int16_t)(x + w) : (int16_t)(o.x + o.w);
  const int16_t y1 = (y + h) < (o.y + o.h) ? (int16_t)(y + h) : (int16_t)(o.y + o.h);
  if (x1 <= x0 || y1 <= y0) return Rect{};
  return Rect{x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}

Rect Rect::unite(const Rect& o) const {
  if (empty()) return o;
  if (o.empty()) return *this;
  const int16_t x0 = x < o.x ? x : o.x, y0 = y < o.y ? y : o.y;
  const int16_t x1 = (x + w) > (o.x + o.w) ? (int16_t)(x + w) : (int16_t)(o.x + o.w);
  const int16_t y1 = (y + h) > (o.y + o.h) ? (int16_t)(y + h) : (int16_t)(o.y + o.h);
  return Rect{x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}

void Framebuffer::clear(Color c) {
  for (size_t i = 0; i < kPixels; i++) px_[i] = c;
}

void Framebuffer::copyFrom(const Framebuffer& o) { std::memcpy(px_, o.px_, sizeof(px_)); }

void Framebuffer::setClip(const Rect& r) { clip_ = r.intersect(kScreenRect); }

void Framebuffer::hline(int16_t x, int16_t y, int16_t w, Color c) {
  if (y < clip_.y || y >= clip_.y + clip_.h) return;
  int16_t x0 = x < clip_.x ? clip_.x : x;
  const int16_t x1 = (x + w) > (clip_.x + clip_.w) ? (int16_t)(clip_.x + clip_.w) : (int16_t)(x + w);
  uint16_t* row = px_ + (size_t)y * kScreenW;
  for (; x0 < x1; x0++) row[x0] = c;
}

void Framebuffer::vline(int16_t x, int16_t y, int16_t h, Color c) {
  for (int16_t j = 0; j < h; j++) set(x, (int16_t)(y + j), c);
}

void Framebuffer::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color c) {
  for (int16_t j = 0; j < h; j++) hline(x, (int16_t)(y + j), w, c);
}

void Framebuffer::frameRect(int16_t x, int16_t y, int16_t w, int16_t h, Color c) {
  if (w <= 0 || h <= 0) return;
  hline(x, y, w, c);
  hline(x, (int16_t)(y + h - 1), w, c);
  vline(x, (int16_t)(y + 1), (int16_t)(h - 2), c);
  vline((int16_t)(x + w - 1), (int16_t)(y + 1), (int16_t)(h - 2), c);
}

void Framebuffer::dim(const Rect& r) {
  const Rect a = r.intersect(clip_);
  for (int16_t y = a.y; y < a.y + a.h; y++)
    for (int16_t x = a.x; x < a.x + a.w; x++) {
      uint16_t& p = px_[(size_t)y * kScreenW + x];
      p = color::dimmed(p);
    }
}

Rect diffRect(const uint16_t* a, const uint16_t* b) {
  int16_t x0 = kScreenW, y0 = kScreenH, x1 = -1, y1 = -1;
  for (int16_t y = 0; y < kScreenH; y++) {
    const uint16_t* ra = a + (size_t)y * kScreenW;
    const uint16_t* rb = b + (size_t)y * kScreenW;
    if (std::memcmp(ra, rb, kScreenW * 2) == 0) continue;
    for (int16_t x = 0; x < kScreenW; x++) {
      if (ra[x] == rb[x]) continue;
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
    }
    if (y < y0) y0 = y;
    y1 = y;
  }
  if (x1 < 0) return Rect{};
  return Rect{x0, y0, (int16_t)(x1 - x0 + 1), (int16_t)(y1 - y0 + 1)};
}

}  // namespace ui
