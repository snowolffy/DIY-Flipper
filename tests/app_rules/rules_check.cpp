// rules_check.cpp - compiled three ways by CTest (see CMakeLists.txt): the control build must succeed, the two
// broken apps must fail to compile, because builtin<T>() static_asserts the bypass rules of app_rules.h.
#include "app/app_host.h"

namespace {

#if defined(RULES_LEVEL1_WITHOUT_PAUSE)
// "pong" is level 1 in kBypassList but has no onPauseRequest()
struct App1 : apps::CanvasApp {
  static constexpr const char* kId = "pong";
  void draw(apps::Host&, ui::Framebuffer&) override {}
};
#elif defined(RULES_LEVEL2_WITHOUT_RAW)
// "reaction" is level 2 but has no onRawInput()
struct App1 : apps::CanvasApp {
  static constexpr const char* kId = "reaction";
  void draw(apps::Host&, ui::Framebuffer&) override {}
};
#else
struct App1 : apps::CanvasApp {
  static constexpr const char* kId = "pong";
  void draw(apps::Host&, ui::Framebuffer&) override {}
  void onPauseRequest(apps::Host&) {}
};
#endif

}  // namespace

int main() {
  const apps::AppEntry e = apps::builtin<App1>("TEST");
  return e.level == apps::Bypass::CustomPause || e.level == apps::Bypass::RawInput ? 0 : 1;
}
