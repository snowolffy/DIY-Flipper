#include "app/app.h"

#include <cstdio>

#include "app/shell.h"
#include "app/theme.h"
#include "app/toolkit.h"
#include "board/board_profile.h"
#include "ui/gfx.h"

namespace app {

static_assert(App::kFrameMs * board::kMaxFps <= 1000 + board::kMaxFps, "frame period matches the board's fps cap");

const char* sysEventName(SysEvent e) {
  switch (e) {
    case SysEvent::BootDone: return "boot_done";
    case SysEvent::LockoutOver: return "lockout_over";
    case SysEvent::IrReceived: return "ir_received";
    case SysEvent::IrLearnTimeout: return "ir_learn_timeout";
    case SysEvent::NfcCardFound: return "nfc_card_found";
    case SysEvent::NfcReadDone: return "nfc_read_done";
    case SysEvent::NfcCardLost: return "nfc_card_lost";
    case SysEvent::NfcWriteDone: return "nfc_write_done";
    case SysEvent::WifiScanDone: return "wifi_scan_done";
    case SysEvent::WifiConnected: return "wifi_connected";
    case SysEvent::WifiConnectFailed: return "wifi_connect_failed";
    case SysEvent::BleConnected: return "ble_connected";
    case SysEvent::BleDisconnected: return "ble_disconnected";
    case SysEvent::BlePairRequest: return "ble_pair_request";
    case SysEvent::PackLoaded: return "pack_loaded";
    case SysEvent::PackFailed: return "pack_failed";
    case SysEvent::AppFailsafe: return "app_failsafe";
  }
  return "?";
}

uint8_t Screen::deferMask() const {
  switch (inputMode()) {
    case InputMode::List: return bit(hal::Button::Cancel);
    case InputMode::Text: return bit(hal::Button::Ok) | bit(hal::Button::Cancel);
    case InputMode::Normal: break;
  }
  return 0;
}

App::App(hal::Hal& hal) : hal_(hal) {}
App::~App() = default;

void App::begin() {
  now_ = hal_.clock.millis();
  lastInput_ = now_;
  settings_.load(hal_.storage);
  security_.begin(hal_, now_);
  theme::useBuiltIn();
  if (!settings_.theme.empty()) {
    std::string err;
    std::vector<std::string> warnings;
    theme::load(hal_.storage, settings_.theme, err, warnings);
  }
  battery_.update(now_, hal_.battery);
  applyBrightness(settings_.brightness);
  lastWifi_ = hal_.wifi.state();
  lastBle_ = hal_.ble.state();
  if (hal_.power.wakeReason() == hal::WakeReason::DeepSleep) shell::resumeFromDeepSleep(*this);
  else push(shell::makeBootStatus());
  applyOps();
  render(true);
}

// ---------------- navigation ----------------

void App::push(std::unique_ptr<Screen> s, Anim a) { ops_.push_back({Op::Push, std::move(s), a, ""}); }
void App::pop(Anim a) { ops_.push_back({Op::Pop, nullptr, a, ""}); }
void App::replace(std::unique_ptr<Screen> s, Anim a) { ops_.push_back({Op::Replace, std::move(s), a, ""}); }
void App::popTo(const char* code, Anim a) { ops_.push_back({Op::PopTo, nullptr, a, code}); }
void App::reset(std::unique_ptr<Screen> s, Anim a) { ops_.push_back({Op::Reset, std::move(s), a, ""}); }
void App::popToDepth(size_t n, Anim a) {
  Op op{Op::PopToDepth, nullptr, a, ""};
  op.depth = n < 1 ? 1 : n;
  ops_.push_back(std::move(op));
}

int App::find(const char* code) const {
  for (size_t i = 0; i < stack_.size(); i++)
    if (std::string(code) == stack_[i]->code()) return (int)i;
  return -1;
}

void App::toast(const std::string& text, uint32_t ms) {
  toastText_ = text;
  toastUntil_ = now_ + ms;
}

void App::emit(SysEvent e) { pending_.push_back(e); }

void App::applyOps() {
  // ops queued by onEnter/onResume are applied in the same pass
  for (size_t i = 0; i < ops_.size(); i++) {
    Op op = std::move(ops_[i]);
    if (op.anim.type != Anim::Cut) startTransition(op.anim);
    input_.staleAll();
    auto removeTop = [&] {
      stack_.back()->onLeave(*this);
      stack_.pop_back();
    };
    switch (op.kind) {
      case Op::Push:
        stack_.push_back(std::move(op.screen));
        stack_.back()->onEnter(*this);
        break;
      case Op::Replace:
        if (!stack_.empty()) removeTop();
        stack_.push_back(std::move(op.screen));
        stack_.back()->onEnter(*this);
        break;
      case Op::Reset:
        while (!stack_.empty()) removeTop();
        stack_.push_back(std::move(op.screen));
        stack_.back()->onEnter(*this);
        break;
      case Op::Pop:
        if (stack_.size() <= 1) break;  // the root screen stays
        removeTop();
        stack_.back()->onResume(*this);
        break;
      case Op::PopToDepth:
        if (stack_.size() <= op.depth) break;
        while (stack_.size() > op.depth) removeTop();
        stack_.back()->onResume(*this);
        break;
      case Op::PopTo: {
        bool found = false;
        for (const auto& s : stack_) found |= op.code == s->code();
        if (!found) break;
        while (op.code != stack_.back()->code()) removeTop();
        stack_.back()->onResume(*this);
        break;
      }
    }
  }
  ops_.clear();
}

void App::dispatch(SysEvent e) {
  eventLog_.push_back(sysEventName(e));
  if (eventLog_.size() > 16) eventLog_.erase(eventLog_.begin());
  if (!stack_.empty()) stack_.back()->onSystem(*this, e);
  applyOps();
}

std::string App::menuPath() const {
  std::string path;
  for (const auto& s : stack_) {
    const std::string t = s->title();
    if (t.empty()) continue;
    if (!path.empty()) path += "/";
    path += t;
  }
  return path;
}

std::string App::screenCode() const { return stack_.empty() ? "" : stack_.back()->code(); }

// ---------------- loop ----------------

void App::tick() {
  now_ = hal_.clock.millis();
  battery_.update(now_, hal_.battery);

  if (lightWoke_) {
    // first tick after light sleep returned: the wake press is stale, the lock screen goes up
    lightWoke_ = false;
    wake();
    input_.resync();
    lockIfPin();
    applyOps();
  }
  Screen* top = stack_.empty() ? nullptr : stack_.back().get();
  input_.update(hal_.input, now_, top ? top->deferMask() : 0);
  const bool anyInput = input_.count() > 0 || input_.rawCount() > 0;

  // waking: the press that wakes the screen does nothing else
  if (screenOff_ || dimmed_) {
    if (anyInput) {
      const bool wasOff = screenOff_;
      wake();
      if (wasOff) {
        input_.staleAll();
        lockIfPin();
        applyOps();
        render(true);
        return;
      }
    }
  }
  if (anyInput) lastInput_ = now_;

  // raw levels first (bypass level 2 apps), then gestures
  if (top && top->rawInput())
    for (int i = 0; i < input_.rawCount(); i++) {
      if (input_.raw(i).button == hal::Button::Power) continue;
      top->onRaw(*this, input_.raw(i));
      applyOps();
    }
  for (int i = 0; i < input_.count(); i++) {
    const InputEvent& e = input_.event(i);
    if (e.button == hal::Button::Power && shell::powerIsGlobal(*this)) {
      handlePowerButton(e);
      continue;
    }
    if (stack_.empty()) break;
    Screen& s = *stack_.back();
    if (s.rawInput()) continue;
    if (e.gesture == Gesture::Tap && settings_.buttonSound) clickSound();
    s.onInput(*this, e);
    applyOps();
  }

  watchRadios();
  while (!pending_.empty()) {
    const SysEvent e = pending_.front();
    pending_.erase(pending_.begin());
    dispatch(e);
  }
  if (!stack_.empty()) {
    stack_.back()->onTick(*this);
    applyOps();
    while (!pending_.empty()) {
      const SysEvent e = pending_.front();
      pending_.erase(pending_.begin());
      dispatch(e);
    }
  }
  shell::checkBattery(*this);
  handleIdle(anyInput);
  render(false);
}

void App::watchRadios() {
  const hal::WifiState w = hal_.wifi.state();
  if (w != lastWifi_) {
    if (lastWifi_ == hal::WifiState::Scanning && w != hal::WifiState::Scanning) emit(SysEvent::WifiScanDone);
    if (w == hal::WifiState::Connected) emit(SysEvent::WifiConnected);
    if (lastWifi_ == hal::WifiState::Connecting && w == hal::WifiState::Failed) emit(SysEvent::WifiConnectFailed);
    lastWifi_ = w;
  }
  const hal::BleState b = hal_.ble.state();
  if (b != lastBle_) {
    if (b == hal::BleState::Connected) emit(SysEvent::BleConnected);
    if (lastBle_ == hal::BleState::Connected) emit(SysEvent::BleDisconnected);
    if (b == hal::BleState::PairingRequest) emit(SysEvent::BlePairRequest);
    lastBle_ = b;
  }
}

// ---------------- power ----------------

const char* App::powerState() const { return screenOff_ ? "screen_off" : dimmed_ ? "dim" : "awake"; }

void App::applyBrightness(int percent) {
  const int level = percent * 255 / 100;
  if (level != lastBrightness_) hal_.backlight.set((uint8_t)level);
  lastBrightness_ = level;
}

void App::saveSettings() { settings_.save(hal_.storage); }

void App::beep(uint16_t hz, uint16_t ms) {
  if (settings_.notifySound) hal_.buzzer.tone(hz, ms);
}

void App::clickSound() { hal_.buzzer.tone(4000, 8); }

void App::lockIfPin() {
  if (security_.pinSet() && !shell::isLockScreen(top())) push(shell::makeLockScreen(*this));
}

void App::wake() {
  screenOff_ = dimmed_ = false;
  lastBrightness_ = -1;
  applyBrightness(settings_.brightness);
  lastInput_ = now_;
}

void App::sleepNow() {
  dimmed_ = false;
  switch (settings_.sleepMode) {
    case SleepMode::Deep:
      shell::saveForDeepSleep(*this);
      hal_.backlight.set(0);
      hal_.power.deepSleep();  // does not return on the device; in the emulator the session restarts
      break;
    case SleepMode::Light:
      hal_.backlight.set(0);
      lastBrightness_ = 0;
      hal_.power.lightSleep();  // returns once a button woke the chip (the emulator: at once, see tick)
      lightWoke_ = true;
      break;
    case SleepMode::Off:
    case SleepMode::Never:
      // screen off only: the loop keeps running (radios stay as they are)
      hal_.backlight.set(0);
      lastBrightness_ = 0;
      screenOff_ = true;
      break;
  }
}

void App::handlePowerButton(const InputEvent& e) {
  if (e.gesture == Gesture::Tap) sleepNow();
}

void App::handleIdle(bool anyInput) {
  if (anyInput || screenOff_) return;
  if (!stack_.empty() && stack_.back()->keepAwake()) {
    lastInput_ = now_;
    return;
  }
  const uint32_t idle = now_ - lastInput_;
  if (settings_.sleepMode != SleepMode::Never && idle >= (uint32_t)settings_.sleepAfterMin * 60000u) {
    sleepNow();
    return;
  }
  if (!dimmed_ && settings_.dimAfterS > 0 && idle >= (uint32_t)settings_.dimAfterS * 1000u) {
    dimmed_ = true;
    applyBrightness(settings_.brightness / 4 > 5 ? settings_.brightness / 4 : 5);
  }
}

// ---------------- drawing ----------------

void App::startTransition(Anim a) {
  if (!from_) from_.reset(new ui::Framebuffer());
  if (!out_) out_.reset(new ui::Framebuffer());
  from_->copyFrom(shown_);
  tr_.anim = a;
  tr_.start = now_;
  tr_.active = true;
}

void App::drawStatusBar(ui::Framebuffer& fb) {
  tk::StatusInfo s;
  hal::DateTime t;
  char clock[8];
  if (hal_.rtc.now(t)) {
    unsigned h = t.hour;
    if (!settings_.clock24h) h = h % 12 == 0 ? 12 : h % 12;
    std::snprintf(clock, sizeof(clock), "%02u:%02u", h, (unsigned)t.minute);
  } else {
    std::snprintf(clock, sizeof(clock), "--:--");
  }
  s.clock = clock;
  s.bt = hal_.ble.state() == hal::BleState::Connected;
  s.wifi = hal_.wifi.state() == hal::WifiState::Connected;
  s.batteryPct = battery_.percent();
  tk::statusBar(fb, s);
}

void App::drawStack(ui::Framebuffer& fb, size_t index) {
  Screen& s = *stack_[index];
  if (s.overlay() && index > 0) {
    drawStack(fb, index - 1);
    if (s.dimBelow()) fb.dim();
  } else {
    fb.clear();
    if (s.statusBar()) drawStatusBar(fb);
  }
  s.draw(*this, fb);
}

void App::drawToast(ui::Framebuffer& fb) {
  if (toastText_.empty()) return;
  if ((int32_t)(now_ - toastUntil_) >= 0) {
    toastText_.clear();
    return;
  }
  tk::toast(fb, toastText_);
}

namespace {

uint16_t scale565(uint16_t c, uint32_t k /*0-256*/) {
  const uint32_t r = ((c >> 11) & 31) * k >> 8, g = ((c >> 5) & 63) * k >> 8, b = (c & 31) * k >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t blend565(uint16_t a, uint16_t b, uint32_t k /*0-256 towards b*/) {
  const uint32_t ra = (a >> 11) & 31, ga = (a >> 5) & 63, ba = a & 31;
  const uint32_t rb = (b >> 11) & 31, gb = (b >> 5) & 63, bb = b & 31;
  const uint32_t r = (ra * (256 - k) + rb * k) >> 8, g = (ga * (256 - k) + gb * k) >> 8,
                 bl = (ba * (256 - k) + bb * k) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

}  // namespace

void App::compose(ui::Framebuffer& out, uint32_t t) {
  const Anim& a = tr_.anim;
  const uint32_t k = a.ms ? t * 256 / a.ms : 256;  // 0..256 progress
  const uint16_t* A = from_->pixels();
  const uint16_t* B = fb_.pixels();
  uint16_t* O = out.pixels();
  using ui::kScreenH;
  using ui::kScreenW;
  if (a.type == Anim::Fade) {
    // a fade onto a dialog cross-fades (the page stays visible behind it); between pages it goes through
    // black: out first, then in
    const bool cross = !stack_.empty() && stack_.back()->overlay();
    for (int i = 0; i < ui::kPixels; i++) {
      if (cross) O[i] = blend565(A[i], B[i], k);
      else O[i] = k < 128 ? scale565(A[i], 256 - 2 * k) : scale565(B[i], 2 * (k - 128));
    }
    return;
  }
  // slide: both move; push: the new one moves in over the old one
  const bool vertical = a.dir == Anim::Up || a.dir == Anim::Down;
  const int span = vertical ? kScreenH : kScreenW;
  const int off = (int)(span * k / 256);
  for (int y = 0; y < kScreenH; y++)
    for (int x = 0; x < kScreenW; x++) {
      // position p along the axis; things move toward dir
      int p = vertical ? y : x;
      const bool neg = a.dir == Anim::Up || a.dir == Anim::Left;
      // new content enters from the side opposite to dir
      int pn = neg ? p - (span - off) : p + (span - off);  // coordinate in the new frame
      int po = neg ? p + off : p - off;                     // coordinate in the old frame
      uint16_t c;
      if (pn >= 0 && pn < span) c = vertical ? B[pn * kScreenW + x] : B[y * kScreenW + pn];
      else if (a.type == Anim::Push) c = A[y * kScreenW + x];
      else if (po >= 0 && po < span) c = vertical ? A[po * kScreenW + x] : A[y * kScreenW + po];
      else c = 0;
      O[y * kScreenW + x] = c;
    }
}

void App::render(bool force) {
  if (!force && now_ - lastRender_ < kFrameMs) return;
  lastRender_ = now_;
  if (stack_.empty()) fb_.clear();
  else drawStack(fb_, stack_.size() - 1);
  drawToast(fb_);
  tk::failsafeBar(fb_, failsafe_);

  const ui::Framebuffer* frame = &fb_;
  if (tr_.active) {
    const uint32_t t = now_ - tr_.start;
    if (t >= tr_.anim.ms) {
      tr_.active = false;
    } else {
      compose(*out_, t);
      frame = out_.get();
    }
  }
  const ui::Rect dirty = firstFrame_ ? ui::kScreenRect : ui::diffRect(shown_.pixels(), frame->pixels());
  firstFrame_ = false;
  if (dirty.empty()) return;
  shown_.copyFrom(*frame);
  hal_.display.push(shown_.pixels(), dirty.x, dirty.y, dirty.w, dirty.h);
  pushes_++;
}

}  // namespace app
