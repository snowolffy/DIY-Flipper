#include "app/settings.h"

#include <cstdlib>
#include <string>

namespace app {

const char* sleepModeName(SleepMode m) {
  switch (m) {
    case SleepMode::Deep: return "Deep";
    case SleepMode::Light: return "Light";
    case SleepMode::Off: return "Off";
    case SleepMode::Never: return "Never";
  }
  return "?";
}

namespace {

// tolerate files edited on a PC: CRLF line endings, spaces around keys and values
std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
  return s.substr(b, e - b);
}

int clampInt(const std::string& v, int lo, int hi, int def) {
  if (v.empty()) return def;
  char* end = nullptr;
  const long n = std::strtol(v.c_str(), &end, 10);
  if (*end) return def;
  return n < lo ? lo : n > hi ? hi : (int)n;
}

}  // namespace

void Settings::load(const hal::Storage& storage) {
  std::string text;
  if (!storage.read(hal::Volume::Flash, kPath, text)) return;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string v = trim(line.substr(eq + 1));
    if (key == "brightness") brightness = clampInt(v, 10, 100, brightness);
    else if (key == "dim_after_s") dimAfterS = clampInt(v, 0, 600, dimAfterS);
    else if (key == "sleep_mode") {
      for (SleepMode m : {SleepMode::Deep, SleepMode::Light, SleepMode::Off, SleepMode::Never})
        if (v == sleepModeName(m)) sleepMode = m;
    } else if (key == "sleep_after_min") sleepAfterMin = clampInt(v, 1, 60, sleepAfterMin);
    else if (key == "low_battery_pct") lowBatteryPct = clampInt(v, 5, 50, lowBatteryPct);
    else if (key == "button_sound") buttonSound = v == "1";
    else if (key == "notify_sound") notifySound = v == "1";
    else if (key == "volume") volume = clampInt(v, 0, 100, volume);
    else if (key == "lock_message") lockMessage = v.substr(0, kLockMessageMax);
    else if (key == "clock_24h") clock24h = v == "1";
    else if (key == "theme") theme = v;
  }
}

bool Settings::save(hal::Storage& storage) const {
  std::string t;
  t += "brightness=" + std::to_string(brightness) + "\n";
  t += "dim_after_s=" + std::to_string(dimAfterS) + "\n";
  t += std::string("sleep_mode=") + sleepModeName(sleepMode) + "\n";
  t += "sleep_after_min=" + std::to_string(sleepAfterMin) + "\n";
  t += "low_battery_pct=" + std::to_string(lowBatteryPct) + "\n";
  t += std::string("button_sound=") + (buttonSound ? "1" : "0") + "\n";
  t += std::string("notify_sound=") + (notifySound ? "1" : "0") + "\n";
  t += "volume=" + std::to_string(volume) + "\n";
  t += "lock_message=" + lockMessage + "\n";
  t += std::string("clock_24h=") + (clock24h ? "1" : "0") + "\n";
  if (!theme.empty()) t += "theme=" + theme + "\n";
  return storage.write(hal::Volume::Flash, kPath, t);
}

}  // namespace app
