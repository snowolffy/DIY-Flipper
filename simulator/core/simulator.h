// simulator.h - owns the mocks and the firmware App, and drives the loop on a virtual clock so runs are
// deterministic: the same commands give the same frames on every machine.
//
// Power is modelled like the board: deep sleep stops the firmware until OK or Power is pressed, then it
// starts again with wakeReason() == DeepSleep (retained bytes kept); the power switch Off cuts everything
// and On cold-boots.
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

  void boot();
  // Power-cycles the device at the current virtual time: fresh firmware state, radios off, settings reloaded
  // from storage. Cards in the field, bonds and scripted networks stay (they are outside the device).
  // cold = false wakes it as from deep sleep.
  void restart(bool cold = true);
  // Runs loop ticks until the virtual clock reaches t (ms since the session started). Never goes backwards.
  void advanceTo(uint32_t t);
  void advanceBy(uint32_t ms) { advanceTo(now() + ms); }
  void setButton(hal::Button b, bool down);
  void setPowerSwitch(bool on);

  uint32_t now() const { return clock_.millis(); }
  bool running() const { return power_.state() == PowerState::Awake || power_.state() == PowerState::LightSleep; }
  app::App& app() { return *app_; }
  MockDisplay& display() { return display_; }
  MockBacklight& backlight() { return backlight_; }
  MockInput& input() { return input_; }
  MockStorage& storage() { return storage_; }
  MockBattery& battery() { return battery_; }
  MockRtc& rtc() { return rtc_; }
  MockBuzzer& buzzer() { return buzzer_; }
  MockPower& power() { return power_; }
  MockIr& ir() { return ir_; }
  MockNfc& nfc() { return nfc_; }
  MockWifi& wifi() { return wifi_; }
  MockBle& ble() { return ble_; }

 private:
  void start(hal::WakeReason why);

  VirtualClock clock_;
  MockDisplay display_;
  MockBacklight backlight_;
  MockInput input_;
  MockStorage storage_;
  MockBattery battery_;
  MockRtc rtc_;
  MockBuzzer buzzer_;
  MockPower power_;
  MockIr ir_;
  MockNfc nfc_;
  MockWifi wifi_;
  MockBle ble_;
  hal::Hal hal_;
  std::unique_ptr<app::App> app_;
  bool booted_ = false;
};

}  // namespace sim
