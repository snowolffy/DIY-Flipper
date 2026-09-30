// hal.h - hardware abstraction layer. The firmware's UI and app code talks only to these interfaces;
// the ESP32 build implements them with real drivers, the simulator with mocks. Nothing here may include
// Arduino, GUI or OS headers.
#pragma once

#include <cstdint>
#include <string>

namespace hal {

enum class Button : uint8_t { Ok, Cancel, Left, Right, Power };
constexpr int kButtonCount = 5;

struct DateTime {
  uint16_t year = 2000;
  uint8_t month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

// Milliseconds since boot. Wraps after ~49 days; compare with subtraction, never with <.
class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t millis() const = 0;
};

// Takes a whole 128x160 1-bit frame (2560 bytes, idx=y*128+x, byte=idx/8, bit=idx%8 LSB-first, 1 = ink).
// invert swaps ink/paper at output time; the frame itself never changes.
class Display {
 public:
  virtual ~Display() = default;
  virtual void push(const uint8_t* frame, bool invert) = 0;
};

// Raw button levels. Press/hold/repeat detection is firmware logic (app/buttons.h), so it gets tested too.
class Input {
 public:
  virtual ~Input() = default;
  virtual bool isDown(Button b) const = 0;
};

enum class Volume : uint8_t { Flash, Sd };

// Paths are absolute within a volume, e.g. "/settings.ini" or "/ir/tv.json".
class Storage {
 public:
  virtual ~Storage() = default;
  virtual bool present(Volume v) const = 0;
  virtual bool exists(Volume v, const std::string& path) const = 0;
  virtual bool read(Volume v, const std::string& path, std::string& out) const = 0;
  virtual bool write(Volume v, const std::string& path, const std::string& data) = 0;
};

// Battery voltage from the ADC divider, already scaled to cell millivolts. 0 means no usable reading.
class Battery {
 public:
  virtual ~Battery() = default;
  virtual uint16_t readMillivolts() = 0;
};

// DS3231. now() returns false when the RTC is missing or has lost power.
class Rtc {
 public:
  virtual ~Rtc() = default;
  virtual bool now(DateTime& out) = 0;
};

struct Hal {
  Clock& clock;
  Display& display;
  Input& input;
  Storage& storage;
  Battery& battery;
  Rtc& rtc;
};

}  // namespace hal
