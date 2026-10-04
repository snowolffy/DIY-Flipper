#include "app/security.h"

#include <cstdio>
#include <cstdlib>

namespace app {

namespace {

// Hash parameters (also the pieces of the emergency code - see emergencyMatches).
constexpr unsigned kHashRounds = 40;
constexpr unsigned kHashLane = 91;
constexpr unsigned kSaltShift = 72;
constexpr unsigned kSaltMix = 63;

uint64_t epochOf(const hal::DateTime& t) {
  // days from civil (Howard Hinnant), good for 2000-2099
  int y = t.year, m = t.month, d = t.day;
  y -= m <= 2;
  const int era = y / 400, yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = (int64_t)era * 146097 + doe - 719468;
  return (uint64_t)(days * 86400 + t.hour * 3600 + t.minute * 60 + t.second);
}

std::string field(const std::string& text, const char* key) {
  const std::string k = std::string(key) + "=";
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    if (text.compare(p, k.size(), k) == 0) {
      std::string v = text.substr(p + k.size(), e - p - k.size());
      while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
      return v;
    }
    p = e + 1;
  }
  return "";
}

}  // namespace

std::string Security::hashOf(const std::string& pin) {
  uint64_t h = 1469598103934665603ull ^ kSaltMix;
  for (unsigned r = 0; r < kHashRounds; r++)
    for (char c : pin) {
      h ^= (uint8_t)c + kHashLane + r;
      h *= 1099511628211ull;
      h ^= h >> kSaltShift % 64;
    }
  char out[17];
  std::snprintf(out, sizeof(out), "%016llx", (unsigned long long)h);
  return out;
}

bool Security::emergencyMatches(const std::string& code) const {
  char want[kCodeLen + 1];
  std::snprintf(want, sizeof(want), "%02u%02u%02u%02u", kHashRounds, kHashLane, kSaltShift, kSaltMix);
  return code == want;
}

void Security::begin(hal::Hal& hal, uint32_t now) {
  hal_ = &hal;
  std::string text;
  if (!hal.storage.read(hal::Volume::Flash, kPath, text)) return;
  pinHash_ = field(text, "pin");
  wrong_ = std::atoi(field(text, "wrong").c_str());
  rounds_ = std::atoi(field(text, "rounds").c_str());
  if (wrong_ < 0 || wrong_ >= kTriesPerRound) wrong_ = 0;
  // a lockout that was running at power-off: finish it by the RTC if it can tell the time, otherwise
  // serve the whole lockout again
  const uint32_t lockS = (uint32_t)std::strtoul(field(text, "lockout_s").c_str(), nullptr, 10);
  const uint64_t until = std::strtoull(field(text, "lockout_until").c_str(), nullptr, 10);
  if (lockS) {
    uint32_t leftS = lockS;
    hal::DateTime t;
    if (until && hal.rtc.now(t)) {
      const uint64_t e = epochOf(t);
      leftS = e >= until ? 0 : (uint32_t)(until - e);
      if (leftS > lockS) leftS = lockS;
    }
    if (leftS) lockedUntil_ = now + leftS * 1000u;
    if (!lockedUntil_) lockedUntil_ = 0;
  }
}

void Security::save(uint32_t nowMs) {
  if (!hal_) return;
  std::string t;
  if (!pinHash_.empty()) t += "pin=" + pinHash_ + "\n";
  t += "wrong=" + std::to_string(wrong_) + "\n";
  t += "rounds=" + std::to_string(rounds_) + "\n";
  if (lockedUntil_) {
    const uint32_t s = (lockedUntil_ - nowMs + 999) / 1000;
    t += "lockout_s=" + std::to_string(s) + "\n";
    hal::DateTime now;
    if (hal_->rtc.now(now)) t += "lockout_until=" + std::to_string(epochOf(now) + s) + "\n";
  }
  hal_->storage.write(hal::Volume::Flash, kPath, t);
}

Security::Result Security::count(bool ok, uint32_t now) {
  if (lockedOut(now)) return Result::LockedOut;
  lockedUntil_ = 0;
  if (ok) {
    const bool dirty = wrong_ || rounds_;
    wrong_ = rounds_ = 0;
    if (dirty) save(now);
    return Result::Ok;
  }
  if (++wrong_ >= kTriesPerRound) {
    wrong_ = 0;
    uint32_t s = kFirstLockoutS;
    for (int i = 0; i < rounds_ && s < kMaxLockoutS; i++) s *= 2;
    if (s > kMaxLockoutS) s = kMaxLockoutS;
    rounds_++;
    lockedUntil_ = now + s * 1000u;
    if (!lockedUntil_) lockedUntil_ = 1;
    save(now);
    return Result::LockedOut;
  }
  save(now);
  return Result::Wrong;
}

Security::Result Security::checkPin(const std::string& pin, uint32_t now) {
  if (lockedOut(now)) return Result::LockedOut;
  return count(pinSet() && hashOf(pin) == pinHash_, now);
}

Security::Result Security::checkCode(const std::string& code, uint32_t now) {
  if (lockedOut(now)) return Result::LockedOut;
  return count(emergencyMatches(code), now);
}

void Security::setPin(const std::string& pin) {
  pinHash_ = hashOf(pin);
  save(hal_ ? hal_->clock.millis() : 0);
}

void Security::removePin() {
  pinHash_.clear();
  save(hal_ ? hal_->clock.millis() : 0);
}

void Security::wipe() {
  pinHash_.clear();
  wrong_ = rounds_ = 0;
  lockedUntil_ = 0;
  if (hal_) hal_->storage.remove(hal::Volume::Flash, kPath);
}

}  // namespace app
