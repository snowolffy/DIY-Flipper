// app_rules.h - the rules every game / custom program runs under (firmware UI plan, section 3), in one
// place. The host (app_host.h) enforces them; nothing an app does can switch them off.
//
// Bypass levels
//   0 Default       built-in games and every SD pack: the host owns Cancel hold (pause menu Resume /
//                   Restart / Exit); a short Cancel reaches the app on release.
//   1 Custom pause  compiled-in canvas apps listed below: Cancel hold calls onPauseRequest(); the app draws
//                   its own pause and calls host.exit().
//   2 Raw input     compiled-in canvas apps listed below: raw down/up of OK, Cancel, <, > with times through
//                   onRawInput(); nothing is intercepted.
// Always, at every level: Cancel held kFailsafeMs forces the app out (onExit gets kExitBudgetMs); Power
// sleeps and locks; battery below kSaveBelowPct asks the app to save and warns; idle dim/sleep are off.
#pragma once

#include <cstdint>

namespace apps {

enum class Bypass : uint8_t { Default = 0, CustomPause = 1, RawInput = 2 };
enum class AppType : uint8_t { Canvas, Screens };

constexpr uint32_t kPauseHoldMs = 500;      // Cancel hold that opens the pause menu (level 0)
constexpr uint32_t kFailsafeMs = 3000;      // Cancel held this long: forced exit, every level
constexpr uint32_t kFailsafeBarFromMs = 1500;  // the failsafe bar shows from here
constexpr uint32_t kExitBudgetMs = 200;     // onExit() longer than this is reported as cut short
constexpr uint32_t kFailsafeExitMs = 500;   // G5 "Exiting..." page
constexpr int kSaveBelowPct = 5;            // onSaveRequest + full-screen warning
constexpr int kPowerOffPct = 3;             // the OS switches off at this level

// Reserved buttons: apps never see Power; at level 0/1 Cancel hold belongs to the host.
constexpr bool kPowerReserved = true;

// Resource budget for SD packs (checked by validatePack).
constexpr int kMaxSpriteW = 128, kMaxSpriteH = 160;
constexpr int kMaxPackAssets = 64;
constexpr uint32_t kPackPsramBudget = 2u * 1024 * 1024;  // bytes of decoded RGB565 across all assets

struct BypassEntry {
  const char* appId;
  Bypass level;
};
// Only compiled-in canvas apps may be listed; an app not listed is level 0.
constexpr BypassEntry kBypassList[] = {
    {"pong", Bypass::CustomPause},
    {"reaction", Bypass::RawInput},
};

constexpr bool sameId(const char* a, const char* b) {
  while (*a && *a == *b) a++, b++;
  return *a == *b;
}
constexpr Bypass bypassOf(const char* id) {
  for (const auto& e : kBypassList)
    if (sameId(e.appId, id)) return e.level;
  return Bypass::Default;
}

}  // namespace apps
