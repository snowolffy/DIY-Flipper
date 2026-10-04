#include "app/input.h"

namespace app {

void InputRecognizer::staleAll() {
  for (S& s : st_)
    if (s.down) s.stale = true;
}

void InputRecognizer::update(const hal::Input& in, uint32_t now, uint8_t deferMask) {
  n_ = rawN_ = 0;
  for (int i = 0; i < hal::kButtonCount; i++) {
    S& s = st_[i];
    const hal::Button b = static_cast<hal::Button>(i);
    const bool lvl = in.isDown(b);
    if (first_) {
      // a button already down at boot is stale until released
      s.down = s.stale = lvl;
      s.edgeAt = s.downAt = now;
      continue;
    }
    // debounce: an edge is taken at once, then the level is ignored for kDebounceMs (contact bounce)
    if (lvl != s.down && now - s.edgeAt >= kDebounceMs) {
      s.edgeAt = now;
      s.down = lvl;
      if (rawN_ < (int)(sizeof(raw_) / sizeof(raw_[0]))) raw_[rawN_++] = RawEvent{b, lvl, now};
      if (lvl) {
        s.downAt = now;
        if (s.stale) continue;
        s.holdSent = s.comboUsed = false;
        s.repeats = 0;
        s.interval = kRepeatStartMs;
        s.nextRepeat = now + kHoldMs;
        // combo: the earliest other live button still down is the held one
        s.held = -1;
        for (int j = 0; j < hal::kButtonCount; j++) {
          const S& o = st_[j];
          if (j == i || !o.down || o.stale || o.comboUsed) continue;
          if (s.held < 0 || (int32_t)(o.downAt - st_[s.held].downAt) < 0) s.held = (int8_t)j;
        }
        if (s.held >= 0) st_[s.held].comboUsed = true;
        s.deferred = s.held < 0 && (deferMask & bit(b));
        if (!s.deferred) emit(InputEvent{b, Gesture::Tap, s.held, 0, 0});
      } else {
        const uint32_t dur = now - s.downAt;
        if (s.stale) {
          s.stale = false;
          continue;
        }
        if (s.comboUsed) continue;
        if (s.deferred && !s.holdSent) emit(InputEvent{b, Gesture::Tap, -1, dur, 0});
        emit(InputEvent{b, Gesture::Release, s.held, dur, 0});
      }
      continue;
    }
    if (!s.down || s.stale || s.comboUsed) continue;
    const uint32_t dur = now - s.downAt;
    if (!s.holdSent && dur >= kHoldMs) {
      s.holdSent = true;
      emit(InputEvent{b, Gesture::Hold, s.held, dur, 0});
    }
    if ((int32_t)(now - s.nextRepeat) >= 0) {
      emit(InputEvent{b, Gesture::Repeat, s.held, dur, ++s.repeats});
      s.interval = s.interval * kRepeatAccelPct / 100;
      if (s.interval < kRepeatMinMs) s.interval = kRepeatMinMs;
      s.nextRepeat = now + s.interval;
    }
  }
  first_ = false;
}

}  // namespace app
