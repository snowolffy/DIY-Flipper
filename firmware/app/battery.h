// battery.h - LiPo voltage to percentage: a lookup table plus a moving average, so one noisy ADC read
// doesn't make the status bar icon flicker.
#pragma once

#include <cstdint>

#include "hal/hal.h"

namespace app {

struct BatteryPoint {
  uint16_t mv;
  uint8_t percent;
};

// Single-cell LiPo under light load, highest voltage first. Shared with the simulator's mock so a
// "set battery to 40%" there produces the ADC value this table reads back as 40%.
constexpr BatteryPoint kBatteryTable[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50},
    {3750, 40},  {3700, 30}, {3650, 20}, {3600, 10}, {3500, 5}, {3300, 0},
};
constexpr int kBatteryTableSize = sizeof(kBatteryTable) / sizeof(kBatteryTable[0]);

// Linear interpolation between table points; clamps outside the table. Returns -1 for 0 mV (no reading).
int batteryPercentFromMv(uint16_t mv);
uint16_t batteryMvFromPercent(int percent);

class BatteryMonitor {
 public:
  static constexpr uint32_t kSampleMs = 1000;
  static constexpr int kSamples = 8;

  // Reads the ADC every kSampleMs (first call reads immediately).
  void update(uint32_t now, hal::Battery& battery);

  // 0-100, or -1 while there is no usable reading.
  int percent() const { return percent_; }
  uint16_t millivolts() const { return avgMv_; }

 private:
  uint16_t samples_[kSamples] = {};
  int count_ = 0;
  int next_ = 0;
  bool started_ = false;
  uint32_t lastSample_ = 0;
  uint16_t avgMv_ = 0;
  int percent_ = -1;
};

}  // namespace app
