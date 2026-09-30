// buttons.h - turns raw button levels into UI events. Lives in firmware (not the HAL) so the simulator
// exercises the same press/hold/repeat timing the device uses.
#pragma once

#include <cstdint>
#include <vector>

#include "hal/hal.h"

namespace app {

enum class Press : uint8_t {
  Short,   // released before kHoldMs
  Long,    // still down at kHoldMs (fires once; no Short follows on release)
  Repeat,  // Left/Right only: fires every kRepeatMs after the Long, for scrolling lists
};

struct ButtonEvent {
  hal::Button button;
  Press press;
};

class Buttons {
 public:
  static constexpr uint32_t kHoldMs = 500;
  static constexpr uint32_t kRepeatMs = 120;

  // Samples every button once; call once per loop tick.
  void update(uint32_t now, const hal::Input& input, std::vector<ButtonEvent>& out);

 private:
  struct State {
    bool down = false;
    bool longSent = false;
    uint32_t downAt = 0;
    uint32_t lastRepeat = 0;
  };
  State state_[hal::kButtonCount];
};

}  // namespace app
