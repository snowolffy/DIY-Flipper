// mocks.h - HAL implementations for the simulator. Each one exposes setters that live UI controls and
// script events both call, so the two paths are the same code.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "hal/hal.h"
#include "ui/framebuffer.h"

namespace sim {

class VirtualClock : public hal::Clock {
 public:
  uint32_t millis() const override { return now_; }
  void set(uint32_t now) { now_ = now; }

 private:
  uint32_t now_ = 0;
};

class MockDisplay : public hal::Display {
 public:
  void push(const uint8_t* frame, bool invert) override;

  // What the panel shows: ink bits, flipped when invert is on. 1 = lit pixel.
  bool lit(int16_t x, int16_t y) const;
  bool invert() const { return invert_; }
  const uint8_t* frame() const { return frame_; }
  uint64_t pushes() const { return pushes_; }
  // FNV-1a 64 over the shown pixels, as 16 hex digits. Stable across platforms.
  std::string hash() const;
  bool writePbm(const std::filesystem::path& path) const;

 private:
  uint8_t frame_[ui::kFrameBytes] = {};
  bool invert_ = false;
  uint64_t pushes_ = 0;
};

class MockInput : public hal::Input {
 public:
  bool isDown(hal::Button b) const override { return down_[static_cast<int>(b)]; }
  void set(hal::Button b, bool down) { down_[static_cast<int>(b)] = down; }

 private:
  bool down_[hal::kButtonCount] = {};
};

// Flash and SD as real folders: <root>/flash and <root>/sd.
class MockStorage : public hal::Storage {
 public:
  explicit MockStorage(std::filesystem::path root) : root_(std::move(root)) {}

  bool present(hal::Volume v) const override;
  bool exists(hal::Volume v, const std::string& path) const override;
  bool read(hal::Volume v, const std::string& path, std::string& out) const override;
  bool write(hal::Volume v, const std::string& path, const std::string& data) override;

  void setSdPresent(bool present) { sdPresent_ = present; }
  void setFailWrites(bool fail) { failWrites_ = fail; }
  bool sdPresent() const { return sdPresent_; }

  // "sd:/ir/tv.json" or "flash:/settings.ini" -> volume + path. False for anything else.
  static bool parse(const std::string& spec, hal::Volume& v, std::string& path);
  std::filesystem::path hostPath(hal::Volume v, const std::string& path) const;

 private:
  std::filesystem::path root_;
  bool sdPresent_ = true;
  bool failWrites_ = false;
};

class MockBattery : public hal::Battery {
 public:
  uint16_t readMillivolts() override { return mv_; }
  // percent < 0 means "no usable reading" (ADC returns 0).
  void setPercent(int percent);
  void setMillivolts(uint16_t mv) { mv_ = mv; }

 private:
  uint16_t mv_ = 4000;
};

class MockRtc : public hal::Rtc {
 public:
  explicit MockRtc(const VirtualClock& clock) : clock_(clock) {}
  bool now(hal::DateTime& out) override;
  // Sets the wall time as of the current virtual millis; it then advances with the clock.
  void set(const hal::DateTime& t);
  void setMissing(bool missing) { missing_ = missing; }
  static bool parse(const std::string& iso, hal::DateTime& out);  // "2026-09-30T12:45:00"

 private:
  const VirtualClock& clock_;
  int64_t baseEpoch_ = 1790771100;  // 2026-09-30 12:25:00 UTC
  uint32_t baseMillis_ = 0;
  bool missing_ = false;
};

}  // namespace sim
