// security.h - the PIN, the emergency code and the shared wrong-try counter.
//
// One counter covers every place a PIN or the emergency code is typed (lock screen, change/remove PIN,
// emergency menu, factory reset). kTriesPerRound wrong tries start a lockout; each further lockout lasts twice
// as long as the one before (30 s, 1 min, 2 min ... capped at 10 min) until a correct entry clears it. The
// counter and the lockout live in flash (/security.ini), so a reboot doesn't reset them.
#pragma once

#include <cstdint>
#include <string>

#include "hal/hal.h"

namespace app {

class Security {
 public:
  static constexpr int kPinLen = 6;
  static constexpr int kCodeLen = 8;
  static constexpr int kTriesPerRound = 3;
  static constexpr uint32_t kFirstLockoutS = 30;
  static constexpr uint32_t kMaxLockoutS = 600;
  static constexpr const char* kPath = "/security.ini";

  enum class Result : uint8_t { Ok, Wrong, LockedOut };

  void begin(hal::Hal& hal, uint32_t now);

  bool pinSet() const { return !pinHash_.empty(); }
  // Checks a PIN against the stored one. Wrong answers count; the third starts a lockout.
  Result checkPin(const std::string& pin, uint32_t now);
  // Same for the 8-digit emergency code.
  Result checkCode(const std::string& code, uint32_t now);
  void setPin(const std::string& pin);
  void removePin();
  // Factory reset "Everything": PIN and counters gone.
  void wipe();

  int triesLeft() const { return kTriesPerRound - wrong_; }
  bool lockedOut(uint32_t now) const { return lockedUntil_ != 0 && (int32_t)(lockedUntil_ - now) > 0; }
  uint32_t lockoutLeftMs(uint32_t now) const { return lockedOut(now) ? lockedUntil_ - now : 0; }
  int rounds() const { return rounds_; }

 private:
  Result count(bool ok, uint32_t now);
  void save(uint32_t now);
  static std::string hashOf(const std::string& pin);
  bool emergencyMatches(const std::string& code) const;

  hal::Hal* hal_ = nullptr;
  std::string pinHash_;
  int wrong_ = 0;   // wrong tries in this round
  int rounds_ = 0;  // lockouts so far
  uint32_t lockedUntil_ = 0;  // millis; 0 = not locked
};

}  // namespace app
