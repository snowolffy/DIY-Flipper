#include "app/battery.h"

namespace app {

int batteryPercentFromMv(uint16_t mv) {
  if (mv == 0) return -1;
  if (mv >= kBatteryTable[0].mv) return kBatteryTable[0].percent;
  for (int i = 1; i < kBatteryTableSize; i++) {
    const BatteryPoint& hi = kBatteryTable[i - 1];
    const BatteryPoint& lo = kBatteryTable[i];
    if (mv >= lo.mv) {
      return lo.percent + (int)(mv - lo.mv) * (hi.percent - lo.percent) / (hi.mv - lo.mv);
    }
  }
  return 0;
}

uint16_t batteryMvFromPercent(int percent) {
  if (percent < 0) return 0;
  if (percent >= kBatteryTable[0].percent) return kBatteryTable[0].mv;
  for (int i = 1; i < kBatteryTableSize; i++) {
    const BatteryPoint& hi = kBatteryTable[i - 1];
    const BatteryPoint& lo = kBatteryTable[i];
    if (percent >= lo.percent) {
      // round up so batteryPercentFromMv(batteryMvFromPercent(p)) == p despite integer division
      const int span = hi.percent - lo.percent;
      return (uint16_t)(lo.mv + ((percent - lo.percent) * (hi.mv - lo.mv) + span - 1) / span);
    }
  }
  return kBatteryTable[kBatteryTableSize - 1].mv;
}

void BatteryMonitor::update(uint32_t now, hal::Battery& battery) {
  if (started_ && now - lastSample_ < kSampleMs) return;
  started_ = true;
  lastSample_ = now;

  const uint16_t mv = battery.readMillivolts();
  if (mv == 0) {
    // a lost reading clears the history so a reconnected pack isn't averaged with stale values
    count_ = 0;
    next_ = 0;
    avgMv_ = 0;
    percent_ = -1;
    return;
  }
  samples_[next_] = mv;
  next_ = (next_ + 1) % kSamples;
  if (count_ < kSamples) count_++;
  uint32_t sum = 0;
  for (int i = 0; i < count_; i++) sum += samples_[i];
  avgMv_ = (uint16_t)(sum / count_);
  percent_ = batteryPercentFromMv(avgMv_);
}

}  // namespace app
