// simulator.h - owns the mocks and the firmware App, and drives the loop on a virtual clock so runs are
// deterministic: the same script gives the same frames on every machine.
#pragma once

#include <cstdint>
#include <filesystem>

#include "app/app.h"
#include "core/mocks.h"

namespace sim {

class Simulator {
 public:
  static constexpr uint32_t kTickMs = 10;  // loop period; the device runs its loop at the same rate

  // storageRoot holds flash/ and sd/ folders for this run.
  explicit Simulator(const std::filesystem::path& storageRoot);

  void boot(bool coldBoot = true);
  // Runs loop ticks until the virtual clock reaches t (ms since boot). Never goes backwards.
  void advanceTo(uint32_t t);

  uint32_t now() const { return clock_.millis(); }
  app::App& app() { return app_; }
  MockDisplay& display() { return display_; }
  MockInput& input() { return input_; }
  MockStorage& storage() { return storage_; }
  MockBattery& battery() { return battery_; }
  MockRtc& rtc() { return rtc_; }

 private:
  VirtualClock clock_;
  MockDisplay display_;
  MockInput input_;
  MockStorage storage_;
  MockBattery battery_;
  MockRtc rtc_;
  hal::Hal hal_;
  app::App app_;
  bool booted_ = false;
};

}  // namespace sim
