// app.h - the firmware's OS core: a stack of screens, input gestures, system events, transitions, the
// status bar and the push policy. Runs unchanged on the ESP32-S3 and inside the emulator; everything outside
// comes in through hal::Hal.
//
// Screen codes are the mockup codes of the Flipper UI Studio flows (M1, S2, B-M0 ...), so a screen in code
// can be matched to its node in docs/ui/flows/flow-*-new.json (node id = "n_" + code with '-' -> '_').
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/battery.h"
#include "app/input.h"
#include "app/security.h"
#include "app/settings.h"
#include "hal/hal.h"
#include "ui/framebuffer.h"

namespace app {

class App;

// How a screen treats the buttons (flow schema "inputMode").
enum class InputMode : uint8_t { Normal, List, Text };

// Edge animation (flow schema "transition"). dir = where things move.
struct Anim {
  enum Type : uint8_t { Cut, Slide, Push, Fade } type = Cut;
  enum Dir : uint8_t { Left, Right, Up, Down } dir = Up;
  uint16_t ms = 0;
};
constexpr Anim kCut{};
constexpr Anim slide(Anim::Dir d, uint16_t ms = 250) { return Anim{Anim::Slide, d, ms}; }
constexpr Anim pushAnim(Anim::Dir d, uint16_t ms = 250) { return Anim{Anim::Push, d, ms}; }
constexpr Anim fade(uint16_t ms = 150) { return Anim{Anim::Fade, Anim::Up, ms}; }

// System events of the flows. Each has one place in the firmware that fires it (see app.cpp and the
// screens), and App::lastEvents() keeps the recent ones for the emulator's log.
enum class SysEvent : uint8_t {
  BootDone,
  LockoutOver,
  IrReceived,
  IrLearnTimeout,
  NfcCardFound,
  NfcReadDone,
  NfcCardLost,
  NfcWriteDone,
  WifiScanDone,
  WifiConnected,
  WifiConnectFailed,
  BleConnected,
  BleDisconnected,
  BlePairRequest,
  PackLoaded,
  PackFailed,
  AppFailsafe,
  // the flows' shared catalog (not used by any transition yet; fired so screens can react)
  SdRemoved,
  SdInserted,
  BatteryLow,
  BatteryCritical,
  WifiLost,
};
const char* sysEventName(SysEvent e);  // the flow JSON id, e.g. "nfc_card_found"

class Screen {
 public:
  virtual ~Screen() = default;
  // Mockup code ("M1", "S2", "B-M0").
  virtual const char* code() const = 0;
  // Menu path segment; empty = not part of the path (dialogs, toasts).
  virtual std::string title() const { return ""; }
  virtual InputMode inputMode() const { return InputMode::Normal; }
  // Buttons whose tap fires on release because hold/repeat means something else here. Default by mode:
  // list -> Cancel (hold = manage the row); text -> OK and Cancel; normal -> none.
  virtual uint8_t deferMask() const;
  virtual bool statusBar() const { return true; }
  // Drawn over the screen below, which is dimmed (dialogs, popups). Toasts set overlay + !dimBelow.
  virtual bool overlay() const { return false; }
  virtual bool dimBelow() const { return true; }
  // Idle dim / sleep are off while this screen is on top (games, NFC emulation).
  virtual bool keepAwake() const { return false; }
  // Raw button levels instead of gestures (apps at bypass level 2); the host still runs the failsafe.
  virtual bool rawInput() const { return false; }
  // The app host page (games): while one is in the stack, Cancel held apps::kFailsafeMs forces it out.
  virtual bool hostsApp() const { return false; }

  virtual void onEnter(App&) {}
  virtual void onResume(App&) {}  // on top again after the screen above closed
  virtual void onLeave(App&) {}   // about to be removed
  virtual void onInput(App&, const InputEvent&) {}
  virtual void onRaw(App&, const RawEvent&) {}
  virtual void onSystem(App&, SysEvent) {}
  virtual void onTick(App&) {}
  virtual void draw(App&, ui::Framebuffer&) = 0;
};

class App {
 public:
  static constexpr const char* kVersion = "0.2.0";
  static constexpr const char* kCodename = "pie";
  static constexpr uint32_t kFrameMs = 1000 / 33;     // render/push at most ~30 fps (board::kMaxFps)
  static constexpr uint32_t kToastMs = 1200;
  static constexpr int kTimeZoneHours = 7;            // fixed UTC+7: the RTC keeps local time

  explicit App(hal::Hal& hal);
  ~App();

  // Starts the OS: boot status (power-on) or straight to the lock/home screen (woken from deep sleep).
  void begin();
  // One loop iteration: input, system events, the top screen, then render + push if anything changed.
  void tick();

  // ---- navigation (applied after the current event, so a screen may close itself) ----
  void push(std::unique_ptr<Screen> s, Anim a = kCut);
  void pop(Anim a = kCut);
  void replace(std::unique_ptr<Screen> s, Anim a = kCut);  // swaps the top screen
  // Pops until the top screen has this code (stays if none has it).
  void popTo(const char* code, Anim a = kCut);
  // Pops until n screens are left (n >= 1).
  void popToDepth(size_t n, Anim a = kCut);
  // Index of the lowest screen with this code, -1 if none.
  int find(const char* code) const;
  // Clears the stack and starts over with s.
  void reset(std::unique_ptr<Screen> s, Anim a = kCut);
  void toast(const std::string& text, uint32_t ms = kToastMs);
  // Fires a system event at the top screen (after the current one finishes).
  void emit(SysEvent e);

  // ---- state ----
  uint32_t now() const { return now_; }
  hal::Hal& hal() { return hal_; }
  Settings& settings() { return settings_; }
  Security& security() { return security_; }
  const BatteryMonitor& battery() const { return battery_; }
  const ui::Framebuffer& framebuffer() const { return shown_; }
  bool localTime(hal::DateTime& out) { return hal_.rtc.now(out); }
  void saveSettings();
  // Applies the brightness setting to the backlight (Settings shows changes live).
  void applyBrightness(int percent);
  // Plays a short beep if key sounds are on.
  void beep(uint16_t hz = 2000, uint16_t ms = 30);
  void clickSound();

  std::string menuPath() const;
  std::string screenCode() const;  // code of the top screen
  const Screen* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }
  size_t depth() const { return stack_.size(); }
  const std::vector<std::string>& lastEvents() const { return eventLog_; }
  uint64_t pushes() const { return pushes_; }
  // "awake", "dim", "screen_off", "light_sleep" (only seen while the light sleep call is in progress)
  const char* powerState() const;
  bool screenOff() const { return screenOff_; }

  // ---- sleep / lock ----
  // Sleeps now (Power button, idle timeout): by the Sleep mode setting.
  void sleepNow();
  // Shows the lock screen on top if a PIN is set.
  void lockIfPin();
  // Launcher row last opened, kept across deep sleep (Power::retained) so waking returns to that module.
  int launcherIndex = 0;
  bool moduleOpen = false;

  // Failsafe progress of the app host (0 = hidden), drawn as a thin bar at the bottom edge.
  void setFailsafeProgress(int permille) { failsafe_ = permille; }

 private:
  struct Op {
    enum Kind : uint8_t { Push, Pop, Replace, PopTo, PopToDepth, Reset } kind;
    std::unique_ptr<Screen> screen;
    Anim anim;
    std::string code;
    size_t depth = 0;
  };
  struct Transition {
    Anim anim;
    uint32_t start = 0;
    bool active = false;
  };

  void applyOps();
  void startTransition(Anim a);
  void watchRadios();
  void handleIdle(bool anyInput);
  void handlePowerButton(const InputEvent& e);
  void checkFailsafe();
  void wake();
  void render(bool force);
  void drawStack(ui::Framebuffer& fb, size_t index);
  void drawStatusBar(ui::Framebuffer& fb);
  void drawToast(ui::Framebuffer& fb);
  void compose(ui::Framebuffer& out, uint32_t t);
  void dispatch(SysEvent e);

  hal::Hal& hal_;
  ui::Framebuffer fb_;     // the current screen, freshly drawn
  ui::Framebuffer shown_;  // what was last pushed to the panel
  std::unique_ptr<ui::Framebuffer> from_;  // frame at the start of a transition
  std::unique_ptr<ui::Framebuffer> out_;   // composed frame during a transition
  InputRecognizer input_;
  BatteryMonitor battery_;
  Settings settings_;
  Security security_;
  std::vector<std::unique_ptr<Screen>> stack_;
  std::vector<Op> ops_;
  std::vector<SysEvent> pending_;
  std::vector<std::string> eventLog_;
  Transition tr_;
  std::string toastText_;
  uint32_t toastUntil_ = 0;
  uint32_t now_ = 0, lastRender_ = 0, lastInput_ = 0;
  uint64_t pushes_ = 0;
  bool firstFrame_ = true;
  bool dimmed_ = false, screenOff_ = false, lightWoke_ = false;
  int failsafe_ = 0;
  bool failsafeFired_ = false;
  int lastBrightness_ = -1;
  hal::WifiState lastWifi_ = hal::WifiState::Off;
  hal::BleState lastBle_ = hal::BleState::Off;
  bool lowBatteryWarned_ = false;
  bool sdPresent_ = true;
  int lastPct_ = -1;
};

}  // namespace app
