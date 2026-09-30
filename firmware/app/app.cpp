#include "app/app.h"

#include <cstdio>

#include "app/screens.h"
#include "assets/assets.h"
#include "ui/gfx.h"

namespace app {

App::App(hal::Hal& hal) : hal_(hal) {}

void App::begin(bool coldBoot) {
  now_ = hal_.clock.millis();
  settings_.load(hal_.storage);
  battery_.update(now_, hal_.battery);
  if (coldBoot) push(std::make_unique<SplashScreen>());
  else push(makeMainMenu());
  applyStackOps();
  render();
}

void App::tick() {
  now_ = hal_.clock.millis();
  battery_.update(now_, hal_.battery);

  events_.clear();
  buttons_.update(now_, hal_.input, events_);
  for (const ButtonEvent& e : events_) {
    if (stack_.empty()) break;
    stack_.back()->onEvent(*this, e);
    applyStackOps();
  }
  if (!stack_.empty()) {
    stack_.back()->onTick(*this);
    applyStackOps();
  }
  render();
}

void App::push(std::unique_ptr<Screen> screen) { ops_.push_back({StackOp::Push, std::move(screen)}); }
void App::pop() { ops_.push_back({StackOp::Pop, nullptr}); }
void App::replaceTop(std::unique_ptr<Screen> screen) { ops_.push_back({StackOp::Replace, std::move(screen)}); }

void App::applyStackOps() {
  // ops queued by onEnter are applied in the same pass
  for (size_t i = 0; i < ops_.size(); i++) {
    StackOp op = std::move(ops_[i]);
    if (op.kind == StackOp::Pop || op.kind == StackOp::Replace) {
      // the root screen is never popped; Replace on it swaps it
      if (op.kind == StackOp::Pop && stack_.size() <= 1) continue;
      if (!stack_.empty()) stack_.pop_back();
    }
    if (op.kind == StackOp::Push || op.kind == StackOp::Replace) {
      stack_.push_back(std::move(op.screen));
      stack_.back()->onEnter(*this);
    }
  }
  ops_.clear();
}

std::string App::menuPath() const {
  std::string path;
  for (const auto& s : stack_) {
    if (!path.empty()) path += "/";
    path += s->title();
  }
  return path;
}

void App::render() {
  fb_.clear();
  if (!stack_.empty()) {
    Screen& top = *stack_.back();
    top.draw(*this, fb_);
    if (top.hasStatusBar()) drawStatusBar(top);
  }
  hal_.display.push(fb_.data(), settings_.invert);
}

void App::drawStatusBar(const Screen& top) {
  using namespace ui;
  fb_.fillRect(0, 0, kScreenW, kStatusH - 1, false);
  fb_.hline(0, kStatusH - 1, kScreenW, true);

  // left slot: clock on the home menu (dashes while the RTC has no time), otherwise the title of the
  // nearest screen without its own title row, so a Detail page shows the section it belongs to
  const Screen* named = &top;
  for (size_t i = stack_.size(); i-- > 0;) {
    named = stack_[i].get();
    if (!named->hasTitleRow()) break;
  }
  char left[24];
  if (isRoot(named)) {
    hal::DateTime t;
    if (rtcTime(t)) std::snprintf(left, sizeof(left), "%02u:%02u", (unsigned)t.hour, (unsigned)t.minute);
    else std::snprintf(left, sizeof(left), "--:--");
  } else {
    std::snprintf(left, sizeof(left), "%s", named->title());
  }

  // right slot: battery; WiFi/BT icons join it to the left once those radios exist and are on
  const PicEntry& bat = assets::kIconBattery;
  const int16_t batX = kScreenW - kPad - bat.w;
  drawPic(fb_, batX, 2, bat);
  // the icon's inner fill is 4 columns (x 2-5, rows 3-4); show the charge in quarters
  fb_.fillRect(batX + 2, 2 + 3, 4, 2, false);
  const int pct = battery_.percent();
  if (pct < 0) {
    drawText(fb_, assets::kFontSmall, batX - assets::kFontSmall.w - 1, 2, "?");
  } else {
    const int cols = pct == 0 ? 0 : (pct + 24) / 25;
    fb_.fillRect(batX + 2, 2 + 3, (int16_t)cols, 2, true);
  }

  drawText(fb_, assets::kFontSmall, kPad, 2, left, true, batX - 4);
}

}  // namespace app
