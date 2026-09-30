#include "app/buttons.h"

namespace app {

void Buttons::update(uint32_t now, const hal::Input& input, std::vector<ButtonEvent>& out) {
  for (int i = 0; i < hal::kButtonCount; i++) {
    const auto b = static_cast<hal::Button>(i);
    const bool down = input.isDown(b);
    State& s = state_[i];
    const bool repeats = b == hal::Button::Left || b == hal::Button::Right;

    if (down && !s.down) {
      s.down = true;
      s.longSent = false;
      s.downAt = now;
    } else if (down && s.down) {
      if (!s.longSent && now - s.downAt >= kHoldMs) {
        s.longSent = true;
        s.lastRepeat = now;
        out.push_back({b, Press::Long});
      } else if (s.longSent && repeats && now - s.lastRepeat >= kRepeatMs) {
        s.lastRepeat = now;
        out.push_back({b, Press::Repeat});
      }
    } else if (!down && s.down) {
      s.down = false;
      if (!s.longSent) out.push_back({b, Press::Short});
    }
  }
}

}  // namespace app
