// framebuffer.h - the 128x160 RGB565 screen buffer every screen draws into.
// One uint16_t per pixel in the CPU's own byte order, idx = y*128 + x (the same layout Flipper UI Studio
// exports pictures in). The HAL display pushes it (only the part that changed); the simulator hashes it.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

constexpr int16_t kScreenW = 128;
constexpr int16_t kScreenH = 160;
constexpr size_t kPixels = (size_t)kScreenW * kScreenH;
constexpr size_t kFrameBytes = kPixels * 2;  // 40960

using Color = uint16_t;

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(int16_t px, int16_t py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  Rect intersect(const Rect& o) const;
  Rect unite(const Rect& o) const;  // bounding box; an empty side is ignored
  bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};
constexpr Rect kScreenRect{0, 0, kScreenW, kScreenH};

class Framebuffer {
 public:
  Framebuffer() { clear(); }

  void clear(Color c = 0);
  void set(int16_t x, int16_t y, Color c) {
    if (clip_.contains(x, y)) px_[(size_t)y * kScreenW + x] = c;
  }
  Color get(int16_t x, int16_t y) const {
    return (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) ? 0 : px_[(size_t)y * kScreenW + x];
  }
  void hline(int16_t x, int16_t y, int16_t w, Color c);
  void vline(int16_t x, int16_t y, int16_t h, Color c);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color c);
  void fillRect(const Rect& r, Color c) { fillRect(r.x, r.y, r.w, r.h, c); }
  void frameRect(int16_t x, int16_t y, int16_t w, int16_t h, Color c);
  // Multiplies every pixel inside r by ~0.39 (what Flipper UI Studio's mockups use behind dialogs).
  void dim(const Rect& r = kScreenRect);
  void copyFrom(const Framebuffer& o);

  // Drawing is clipped to this rectangle (always inside the screen).
  void setClip(const Rect& r);
  void resetClip() { clip_ = kScreenRect; }
  const Rect& clip() const { return clip_; }

  const uint16_t* pixels() const { return px_; }
  uint16_t* pixels() { return px_; }

 private:
  uint16_t px_[kPixels];
  Rect clip_ = kScreenRect;
};

// Bounding box of the pixels that differ between two frames (empty when they are the same).
Rect diffRect(const uint16_t* a, const uint16_t* b);

}  // namespace ui
