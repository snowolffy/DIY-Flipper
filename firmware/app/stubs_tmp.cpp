// temporary: module entry points until each module is written
#include "app/modules.h"
#include "app/widgets.h"
namespace app {
namespace {
std::unique_ptr<Screen> stub(const char* code, const char* t) {
  return std::make_unique<MenuScreen>(code, t, [](App&) { return std::vector<MenuItem>{}; });
}
}
namespace ir { std::unique_ptr<Screen> makeMenu(App&) { return stub("I0", "IR"); } }
namespace nfc { std::unique_ptr<Screen> makeMenu(App&) { return stub("N0", "NFC"); } }
namespace games { std::unique_ptr<Screen> makeMenu(App&) { return stub("G1", "GAMES"); } }
namespace bt { std::unique_ptr<Screen> makeMenu(App&) { return stub("B-M0", "BLUETOOTH"); } }
}
