// framebuffer.h - the 128x160 1-bit screen buffer every screen draws into.
// Same bit order as every asset Flipper UI Studio exports: idx = y*128 + x, byte = idx/8, bit = idx%8
// (LSB-first), 1 = ink. The HAL display pushes it whole; the simulator hashes it for tests.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

constexpr int16_t kScreenW = 128;
constexpr int16_t kScreenH = 160;
constexpr size_t kFrameBytes = (size_t)kScreenW * kScreenH / 8;  // 2560

class Framebuffer {
 public:
  Framebuffer() { clear(); }

  void clear();
  void set(int16_t x, int16_t y, bool ink);
  bool get(int16_t x, int16_t y) const;
  void hline(int16_t x, int16_t y, int16_t w, bool ink);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool ink);
  void frameRect(int16_t x, int16_t y, int16_t w, int16_t h, bool ink);

  const uint8_t* data() const { return buf_; }

 private:
  uint8_t buf_[kFrameBytes];
};

}  // namespace ui
