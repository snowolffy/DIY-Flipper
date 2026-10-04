// shell.h - the OS screens of the Main-new flow: boot status (M1), boot logo (M2), lock screens (M3a/b),
// PIN entry (M4a-c), home (M5), launcher (M6a/b) and the emergency menu (M7a/b); plus the hooks App calls
// for power, battery and deep sleep.
#pragma once

#include <functional>
#include <memory>

#include "app/app.h"

namespace app {
namespace shell {

std::unique_ptr<Screen> makeBootStatus();
std::unique_ptr<Screen> makeLockScreen(App& app);
std::unique_ptr<Screen> makeHome();
bool isLockScreen(const Screen* s);
// false while the boot screens handle Power themselves (emergency entry)
bool powerIsGlobal(App& app);
void checkBattery(App& app);
void saveForDeepSleep(App& app);
void resumeFromDeepSleep(App& app);
// The lockout screen (M4c) for any PIN / code entry; returns to `after` when the lockout is over.
std::unique_ptr<Screen> makeLockedOut(std::function<void(App&)> after);
// PIN entry used by Settings (change/remove) with the shared counter: ok runs on a correct PIN.
std::unique_ptr<Screen> makePinCheck(const char* code, std::function<void(App&)> ok, std::function<void(App&)> leave);
// Emergency code entry (8 digits) with the shared counter.
std::unique_ptr<Screen> makeCodeCheck(const char* code, const char* title, std::function<void(App&)> ok,
                                      std::function<void(App&)> leave);

}  // namespace shell
}  // namespace app
