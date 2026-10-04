// input.h - turns raw button levels into the gestures the UI flows are written in (Flipper UI Studio
// flow schema, "gestures"):
//   tap      on press; or on release (before kHoldMs) when the screen gives the same button a meaning on
//            hold/repeat, or holds it for a combo - the screen says which buttons via deferMask
//   hold     once, kHoldMs after the press
//   repeat   at kHoldMs, then again every interval, the interval shrinking x0.8 down to 40 ms
//   release  on release, any duration
// combo: an event whose `held` is set fired while that other button was already down; the held button's
// own tap, hold, repeat and release are then skipped.
// After a screen change every button still down is "stale": it sends nothing more until released, so the
// Cancel tap that opened a screen can't also hold-delete on it.
#pragma once

#include <cstdint>

#include "hal/hal.h"

namespace app {

constexpr uint32_t kHoldMs = 500;
constexpr uint32_t kRepeatStartMs = 200;
constexpr uint32_t kRepeatMinMs = 40;
constexpr uint32_t kRepeatAccelPct = 80;
constexpr uint32_t kDebounceMs = 25;

enum class Gesture : uint8_t { Tap, Hold, Repeat, Release };

constexpr uint8_t buttonBit(hal::Button b) { return (uint8_t)(1u << static_cast<int>(b)); }

struct InputEvent {
  hal::Button button;
  Gesture gesture;
  int8_t held = -1;         // the button held down first, for a combo; -1 = none
  uint32_t durationMs = 0;  // how long the button has been down
  uint16_t repeat = 0;      // 1 for the first repeat

  bool is(hal::Button b, Gesture g) const { return button == b && gesture == g && held < 0; }
  bool tap(hal::Button b) const { return is(b, Gesture::Tap); }
  bool hold(hal::Button b) const { return is(b, Gesture::Hold); }
  bool release(hal::Button b) const { return is(b, Gesture::Release); }
  // tap or repeat: list-style stepping
  bool step(hal::Button b) const {
    return button == b && held < 0 && (gesture == Gesture::Tap || gesture == Gesture::Repeat);
  }
  bool combo(hal::Button h, hal::Button b) const {
    return held == static_cast<int8_t>(h) && button == b && (gesture == Gesture::Tap || gesture == Gesture::Repeat);
  }
};

// Raw level change, for apps that take raw input (bypass level 2).
struct RawEvent {
  hal::Button button;
  bool down;
  uint32_t atMs;
};

class InputRecognizer {
 public:
  static constexpr int kMaxEvents = 16;

  // Reads the levels once. deferMask: buttons whose tap waits for release on the current screen.
  void update(const hal::Input& in, uint32_t now, uint8_t deferMask);
  // What the last update() produced, in order.
  int count() const { return n_; }
  const InputEvent& event(int i) const { return ev_[i]; }
  int rawCount() const { return rawN_; }
  const RawEvent& raw(int i) const { return raw_[i]; }

  // Marks every button that is down as stale (screen changed).
  void staleAll();
  // The next update takes the levels as they are and treats buttons already down as stale (after waking
  // from light sleep: the press that woke the chip must not act).
  void resync() { first_ = true; }
  bool isDown(hal::Button b) const { return st_[static_cast<int>(b)].down; }
  // ms the button has been down (0 when up); stale or not.
  uint32_t downFor(hal::Button b, uint32_t now) const {
    const S& s = st_[static_cast<int>(b)];
    return s.down ? now - s.downAt : 0;
  }

 private:
  struct S {
    bool down = false;  // debounced level
    uint32_t edgeAt = 0;
    uint32_t downAt = 0;
    bool stale = false;
    bool deferred = false;   // tap waits for release
    bool holdSent = false;
    bool comboUsed = false;  // a combo fired while this was held: its own events are skipped
    int8_t held = -1;        // the other button this press is a combo with
    uint32_t nextRepeat = 0, interval = 0;
    uint16_t repeats = 0;
  };
  void emit(const InputEvent& e) {
    if (n_ < kMaxEvents) ev_[n_++] = e;
  }

  S st_[hal::kButtonCount];
  InputEvent ev_[kMaxEvents];
  RawEvent raw_[hal::kButtonCount * 2];
  int n_ = 0, rawN_ = 0;
  bool first_ = true;
};

}  // namespace app
