#include "core/simulator.h"

namespace sim {

Simulator::Simulator(const std::filesystem::path& storageRoot)
    : storage_(storageRoot),
      rtc_(clock_),
      buzzer_(clock_),
      wifi_(clock_),
      hal_{clock_, display_, backlight_, input_, storage_, battery_, rtc_, buzzer_, power_, ir_, nfc_, wifi_, ble_} {
  display_.setClock(&clock_);
}

void Simulator::start(hal::WakeReason why) {
  // the chip resets: radios and peripherals go back to their power-on state
  ir_.setListening(false);
  nfc_.setPolling(false);
  nfc_.stopEmulation();
  wifi_.disconnect();
  ble_.stop();
  buzzer_.stop();
  power_.setState(PowerState::Awake);
  power_.setWakeReason(why);
  app_ = std::make_unique<app::App>(hal_);
  app_->begin();
  booted_ = true;
}

void Simulator::boot() {
  clock_.set(0);
  start(hal::WakeReason::PowerOn);
}

void Simulator::restart(bool cold) {
  if (cold) power_.clearRetained();
  power_.setSwitch(true);
  start(cold ? hal::WakeReason::PowerOn : hal::WakeReason::DeepSleep);
}

void Simulator::setButton(hal::Button b, bool down) {
  input_.set(b, down);
  if (!down) return;
  // a wake button press: OK or Power (the RTC GPIOs of the board profile)
  const bool wakeKey = b == hal::Button::Ok || b == hal::Button::Power;
  if (power_.state() == PowerState::DeepSleep && wakeKey) start(hal::WakeReason::DeepSleep);
  else if (power_.state() == PowerState::LightSleep && wakeKey) power_.setState(PowerState::Awake);
}

void Simulator::setPowerSwitch(bool on) {
  if (on == power_.switchOn()) return;
  power_.setSwitch(on);
  if (!on) {
    power_.setState(PowerState::Off);
    power_.clearRetained();
    std::fill(const_cast<uint16_t*>(display_.frame()), const_cast<uint16_t*>(display_.frame()) + ui::kPixels, 0);
    backlight_.set(0);
  } else {
    start(hal::WakeReason::PowerOn);
  }
}

void Simulator::advanceTo(uint32_t t) {
  if (!booted_) boot();
  while (clock_.millis() < t) {
    const uint32_t next = clock_.millis() + kTickMs;
    clock_.set(next < t ? next : t);
    wifi_.tick();
    // asleep or off: the firmware doesn't run (light sleep wakes by setButton)
    if (power_.state() == PowerState::Awake) app_->tick();
  }
}

}  // namespace sim
