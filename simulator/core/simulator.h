// simulator.h - owns the mocks and the firmware App, and drives the loop on a virtual clock so runs are
// deterministic: the same script gives the same frames on every machine.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "app/app.h"
#include "core/mocks.h"

namespace sim {

class Simulator {
 public:
  static constexpr uint32_t kTickMs = 10;  // loop period; the device runs its loop at the same rate

  // storageRoot holds flash/ and sd/ folders for this run.
  explicit Simulator(const std::filesystem::path& storageRoot);

  void boot(bool coldBoot = true);
  // Power-cycles the device at the current virtual time: fresh firmware state, radios off, settings
  // reloaded from storage. Cards in the field, bonds and scripted networks stay (they're outside the device).
  void restart(bool coldBoot);
  // Runs loop ticks until the virtual clock reaches t (ms since boot). Never goes backwards.
  void advanceTo(uint32_t t);

  uint32_t now() const { return clock_.millis(); }
  app::App& app() { return *app_; }
  MockDisplay& display() { return display_; }
  MockInput& input() { return input_; }
  MockStorage& storage() { return storage_; }
  MockBattery& battery() { return battery_; }
  MockRtc& rtc() { return rtc_; }
  MockIr& ir() { return ir_; }
  MockNfc& nfc() { return nfc_; }
  MockWifi& wifi() { return wifi_; }
  MockBle& ble() { return ble_; }

 private:
  VirtualClock clock_;
  MockDisplay display_;
  MockInput input_;
  MockStorage storage_;
  MockBattery battery_;
  MockRtc rtc_;
  MockIr ir_;
  MockNfc nfc_;
  MockWifi wifi_;
  MockBle ble_;
  hal::Hal hal_;
  std::unique_ptr<app::App> app_;
  bool booted_ = false;
};

}  // namespace sim
