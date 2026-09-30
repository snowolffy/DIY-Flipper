#include "core/simulator.h"

namespace sim {

Simulator::Simulator(const std::filesystem::path& storageRoot)
    : storage_(storageRoot),
      rtc_(clock_),
      hal_{clock_, display_, input_, storage_, battery_, rtc_},
      app_(hal_) {}

void Simulator::boot(bool coldBoot) {
  clock_.set(0);
  app_.begin(coldBoot);
  booted_ = true;
}

void Simulator::advanceTo(uint32_t t) {
  if (!booted_) boot();
  while (clock_.millis() < t) {
    const uint32_t next = clock_.millis() + kTickMs;
    clock_.set(next < t ? next : t);
    app_.tick();
  }
}

}  // namespace sim
