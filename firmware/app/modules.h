// modules.h - entry points of the launcher's modules (one flow each: docs/ui/flows/flow-*-new.json).
#pragma once

#include <memory>

#include "app/app.h"

namespace app {

struct ModuleInfo {
  const char* label;  // launcher card text
  const char* icon;   // theme icon key
  std::unique_ptr<Screen> (*make)(App&);
};

namespace ir { std::unique_ptr<Screen> makeMenu(App& app); }
namespace nfc { std::unique_ptr<Screen> makeMenu(App& app); }
namespace games { std::unique_ptr<Screen> makeMenu(App& app); }
namespace wifi { std::unique_ptr<Screen> makeMenu(App& app); }
namespace bt { std::unique_ptr<Screen> makeMenu(App& app); }
namespace settings { std::unique_ptr<Screen> makeMenu(App& app); }

// Launcher order (firmware UI plan 2.3).
constexpr int kModuleCount = 6;
extern const ModuleInfo kModules[kModuleCount];

}  // namespace app
