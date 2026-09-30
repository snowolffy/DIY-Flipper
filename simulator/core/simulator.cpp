#include "core/simulator.h"

namespace sim {

Simulator::Simulator(const std::filesystem::path& storageRoot)
    : storage_(storageRoot),
      rtc_(clock_),
      wifi_(clock_),
      hal_{clock_, display_, input_, storage_, battery_, rtc_, ir_, nfc_, wifi_, ble_},
      app_(std::make_unique<app::App>(hal_)) {}

void Simulator::boot(bool coldBoot) {
  clock_.set(0);
  app_->begin(coldBoot);
  booted_ = true;
}

void Simulator::restart(bool coldBoot) {
  ir_.setListening(false);
  nfc_.setPolling(false);
  wifi_.disconnect();
  ble_.stop();
  app_ = std::make_unique<app::App>(hal_);
  app_->begin(coldBoot);
  booted_ = true;
}

void Simulator::advanceTo(uint32_t t) {
  if (!booted_) boot();
  while (clock_.millis() < t) {
    const uint32_t next = clock_.millis() + kTickMs;
    clock_.set(next < t ? next : t);
    wifi_.tick();
    app_->tick();
  }
}

}  // namespace sim
