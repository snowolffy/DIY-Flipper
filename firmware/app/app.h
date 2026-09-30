// app.h - the firmware's UI core: a stack of screens, the loop tick, and the status bar. Runs unchanged on
// the ESP32 and inside the simulator; everything outside comes in through hal::Hal.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/battery.h"
#include "app/buttons.h"
#include "app/settings.h"
#include "hal/hal.h"
#include "ui/framebuffer.h"

namespace app {

class App;

class Screen {
 public:
  virtual ~Screen() = default;
  // Shown in the status bar on sub-screens and used for the menu path ("Main/Settings").
  virtual const char* title() const = 0;
  virtual bool hasStatusBar() const { return true; }
  // Screens with their own title row (Detail) leave the status bar to name the section they sit in.
  virtual bool hasTitleRow() const { return false; }
  virtual void onEnter(App&) {}
  virtual void onEvent(App&, const ButtonEvent&) {}
  virtual void onTick(App&) {}
  virtual void draw(App&, ui::Framebuffer&) = 0;
};

class App {
 public:
  static constexpr const char* kVersion = "0.1.0";
  static constexpr const char* kCodename = "pic.h";

  explicit App(hal::Hal& hal);

  // coldBoot shows the boot splash first; waking from deep sleep goes straight to the main menu.
  void begin(bool coldBoot);
  // One loop iteration: read buttons and battery, run the top screen, redraw, push to the display.
  void tick();

  // Screen stack changes are queued and applied after the current event/tick, so a screen may pop itself.
  void push(std::unique_ptr<Screen> screen);
  void pop();
  void replaceTop(std::unique_ptr<Screen> screen);

  std::string menuPath() const;
  uint32_t now() const { return now_; }
  hal::Hal& hal() { return hal_; }
  Settings& settings() { return settings_; }
  const BatteryMonitor& battery() const { return battery_; }
  const ui::Framebuffer& framebuffer() const { return fb_; }
  bool rtcTime(hal::DateTime& out) { return hal_.rtc.now(out); }
  bool isRoot(const Screen* s) const { return !stack_.empty() && stack_.front().get() == s; }

 private:
  struct StackOp {
    enum Kind { Push, Pop, Replace } kind;
    std::unique_ptr<Screen> screen;
  };

  void applyStackOps();
  void render();
  void drawStatusBar(const Screen& top);

  hal::Hal& hal_;
  ui::Framebuffer fb_;
  Buttons buttons_;
  BatteryMonitor battery_;
  Settings settings_;
  std::vector<std::unique_ptr<Screen>> stack_;
  std::vector<StackOp> ops_;
  std::vector<ButtonEvent> events_;
  uint32_t now_ = 0;
};

}  // namespace app
