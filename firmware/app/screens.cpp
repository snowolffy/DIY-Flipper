#include "app/screens.h"

#include <cstdio>
#include <cstring>

#include "assets/assets.h"

namespace app {

using ui::Framebuffer;

namespace {

bool isStep(const ButtonEvent& e) {
  return e.press == Press::Short || e.press == Press::Long || e.press == Press::Repeat;
}

}  // namespace

// ---------------- Splash ----------------

void SplashScreen::onEnter(App& app) { shownAt_ = app.now(); }

void SplashScreen::onEvent(App& app, const ButtonEvent& e) {
  if (!done_ && e.press == Press::Short && e.button != hal::Button::Power) {
    done_ = true;
    app.replaceTop(makeMainMenu());
  }
}

void SplashScreen::onTick(App& app) {
  if (!done_ && app.now() - shownAt_ >= kShowMs) {
    done_ = true;
    app.replaceTop(makeMainMenu());
  }
}

void SplashScreen::draw(App&, Framebuffer& fb) { ui::drawPic(fb, 0, 0, assets::kBootSplash); }

// ---------------- List ----------------

void ListScreen::onEnter(App& app) {
  selectedAt_ = app.now();
  reload(app);
}

void ListScreen::onResume(App& app) { reload(app); }

void ListScreen::reload(App& app) {
  if (!source_) return;
  items_ = source_(app);
  if (sel_ >= (int)items_.size()) sel_ = items_.empty() ? 0 : (int)items_.size() - 1;
  if (first_ > sel_) first_ = sel_;
}

void ListScreen::select(App& app, int index) {
  const int n = (int)items_.size();
  if (n == 0) return;
  sel_ = (index % n + n) % n;  // wraps both ways
  if (sel_ < first_) first_ = sel_;
  if (sel_ >= first_ + ui::kListVisible) first_ = sel_ - ui::kListVisible + 1;
  selectedAt_ = app.now();
}

void ListScreen::onEvent(App& app, const ButtonEvent& e) {
  // five buttons, no up/down: Left moves up the list, Right moves down
  if (e.button == hal::Button::Left && isStep(e)) select(app, sel_ - 1);
  else if (e.button == hal::Button::Right && isStep(e)) select(app, sel_ + 1);
  else if (e.button == hal::Button::Ok && e.press == Press::Short) {
    if (sel_ < (int)items_.size() && items_[sel_].onOk) items_[sel_].onOk(app);
  } else if (e.button == hal::Button::Cancel && e.press == Press::Short) {
    app.pop();
  }
}

int ListScreen::marqueeOffset(uint32_t now, int overflowChars) const {
  const uint32_t scroll = (uint32_t)overflowChars * kMarqueeStepMs;
  const uint32_t cycle = kMarqueePauseMs * 2 + scroll;
  const uint32_t t = (now - selectedAt_) % cycle;
  if (t < kMarqueePauseMs) return 0;
  if (t < kMarqueePauseMs + scroll) return (int)((t - kMarqueePauseMs) / kMarqueeStepMs);
  return overflowChars;
}

void ListScreen::draw(App& app, Framebuffer& fb) {
  using namespace ui;
  const FontEntry& large = assets::kFontLarge;
  const FontEntry& small = assets::kFontSmall;
  const int n = (int)items_.size();
  const bool overflow = n > kListVisible;
  const int16_t right = overflow ? kScreenW - kScrollbarW : kScreenW;

  for (int r = 0; r < kListVisible && first_ + r < n; r++) {
    const int i = first_ + r;
    const Item& it = items_[i];
    const int16_t y = kStatusH + r * kListRowH;
    const bool on = i == sel_;
    if (on) fb.fillRect(0, y, right - 1, kListRowH, true);
    const bool ink = !on;
    if (it.icon) drawPic(fb, kPad, y + (kListRowH - kListIcon) / 2, *it.icon, ink);

    int16_t labelRight = right - 2;
    if (it.value) {
      const std::string v = it.value(app);
      drawTextRight(fb, small, right - 2 - kPad, y + 4, v.c_str(), ink);
      labelRight = right - 2 - kPad - textWidth(small, v.c_str()) - 4;
    }
    const int fits = (labelRight - kListTextX) / large.w;
    const int overflowChars = (int)it.label.size() - fits;
    const int offset = on && overflowChars > 0 ? marqueeOffset(app.now(), overflowChars) : 0;
    drawText(fb, large, kListTextX, y + 4, it.label.c_str() + offset, ink, labelRight);
  }

  if (overflow) {
    const int16_t trackY = kStatusH + 1;
    const int16_t trackH = kScreenH - trackY;
    for (int16_t y = trackY; y < kScreenH; y += 2) fb.set(kScreenW - 2, y, true);
    int16_t thumbH = (int16_t)(trackH * kListVisible / n);
    if (thumbH < 6) thumbH = 6;
    const int16_t thumbY = (int16_t)(trackY + (trackH - thumbH) * first_ / (n - kListVisible));
    fb.fillRect(kScreenW - kScrollbarW, thumbY, kScrollbarW, thumbH, true);
  }
}

// ---------------- Detail ----------------

void DetailScreen::onEvent(App& app, const ButtonEvent& e) {
  if ((e.button == hal::Button::Cancel || e.button == hal::Button::Ok) && e.press == Press::Short) app.pop();
}

void DetailScreen::draw(App& app, Framebuffer& fb) {
  using namespace ui;
  const FontEntry& large = assets::kFontLarge;
  const FontEntry& small = assets::kFontSmall;
  drawText(fb, large, kPad, kStatusH + 2, title_.c_str());
  fb.hline(0, kStatusH + kTitleH - 1, kScreenW, true);

  const int perRow = (kScreenW - kPad * 2) / small.w;
  int row = 0;
  for (const auto& kv : rows_(app)) {
    const int16_t y0 = kStatusH + kTitleH + row * kDetailRowH + 1;
    if (y0 + small.h > kScreenH) break;
    drawText(fb, small, kPad, y0, kv.first.c_str());
    if ((int)(kv.first.size() + 1 + kv.second.size()) > perRow) row++;
    const int16_t y1 = kStatusH + kTitleH + row * kDetailRowH + 1;
    if (y1 + small.h > kScreenH) break;
    drawTextRight(fb, small, kScreenW - kPad, y1, kv.second.c_str());
    row++;
  }
}

// ---------------- Text input ----------------

const char* const TextInputScreen::kCharset =
    "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%&*-_.,:;?/+=()' ";

void TextInputScreen::onEvent(App& app, const ButtonEvent& e) {
  const int n = (int)std::strlen(kCharset);
  if (e.button == hal::Button::Left && isStep(e)) pick_ = (pick_ - 1 + n) % n;
  else if (e.button == hal::Button::Right && isStep(e)) pick_ = (pick_ + 1) % n;
  else if (e.button == hal::Button::Ok && e.press == Press::Short) {
    if (text_.size() < kMaxLen) text_ += kCharset[pick_];
  } else if (e.button == hal::Button::Ok && e.press == Press::Long) {
    if (onDone_) onDone_(app, text_);
  } else if (e.button == hal::Button::Cancel && e.press == Press::Short) {
    if (text_.empty()) app.pop();
    else text_.pop_back();
  } else if (e.button == hal::Button::Cancel && e.press == Press::Long) {
    app.pop();
  }
}

void TextInputScreen::draw(App& app, Framebuffer& fb) {
  using namespace ui;
  (void)app;
  const FontEntry& large = assets::kFontLarge;
  const FontEntry& small = assets::kFontSmall;
  const int16_t y0 = kStatusH;
  drawText(fb, large, kPad, y0 + 2, title_.c_str());
  fb.hline(0, y0 + kTitleH - 1, kScreenW, true);
  drawText(fb, small, kPad, y0 + kTitleH + 2, prompt_.c_str());

  const int16_t fy = y0 + kTitleH + 10;
  fb.frameRect(kPad, fy, kScreenW - kPad * 2, kInputBoxH, true);
  // show the tail when the text is wider than the field
  const size_t fits = (size_t)((kScreenW - kPad * 2 - 8) / large.w);
  const std::string shown = text_.size() > fits ? text_.substr(text_.size() - fits) : text_;
  const int16_t end = drawText(fb, large, kPad + 3, fy + 4, shown.c_str());
  fb.fillRect(end + 1, fy + 3, 1, 10, true);

  char count[12];
  std::snprintf(count, sizeof(count), "%u/%u", (unsigned)text_.size(), (unsigned)kMaxLen);
  drawTextRight(fb, small, kScreenW - kPad, fy + kInputBoxH + 2, count);
  int16_t hy = fy + kInputBoxH + 14;
  drawText(fb, small, kPad, hy, "< > PICK   OK ADD");
  drawText(fb, small, kPad, hy + 10, "HOLD OK  DONE");
  drawText(fb, small, kPad, hy + 20, "BACK  DELETE");

  // carousel: the picked character in the middle, inverted
  const int16_t cy = kScreenH - kCarouselH - kPad;
  fb.hline(0, cy - 1, kScreenW, true);
  const int n = (int)std::strlen(kCharset);
  const int16_t cw = 10;
  const int16_t mid = kScreenW / 2 - cw / 2;
  for (int k = -6; k <= 6; k++) {
    const int16_t x = mid + k * cw;
    if (x + cw <= 0 || x >= kScreenW) continue;
    const char c = kCharset[((pick_ + k) % n + n) % n];
    const bool on = k == 0;
    if (on) fb.fillRect(x, cy + 2, cw, kCarouselH - 4, true);
    const char s[2] = {c == ' ' ? '_' : c, 0};
    drawText(fb, large, x + 1, cy + 5, s, !on);
  }
}

// ---------------- menus ----------------

namespace {

std::unique_ptr<Screen> comingSoon(const std::string& title) {
  return std::make_unique<DetailScreen>(title, [](App&) {
    return DetailScreen::Rows{{"Status", "Not built yet"}};
  });
}

std::string twoDigits(unsigned v) {
  char b[4];
  std::snprintf(b, sizeof(b), "%02u", v % 100);
  return b;
}

std::unique_ptr<Screen> makeFirmwareInfo() {
  return std::make_unique<DetailScreen>("Firmware", [](App& app) {
    const int pct = app.battery().percent();
    std::string battery = "No reading";
    if (pct >= 0) {
      char b[24];
      std::snprintf(b, sizeof(b), "%d%% %u.%02uV", pct, app.battery().millivolts() / 1000,
                    (app.battery().millivolts() % 1000) / 10);
      battery = b;
    }
    return DetailScreen::Rows{
        {"Version", App::kVersion},
        {"Codename", App::kCodename},
        {"Board", "ESP32"},
        {"Display", "ST7735 128x160"},
        {"SD card", app.hal().storage.present(hal::Volume::Sd) ? "Ready" : "Missing"},
        {"Battery", battery},
    };
  });
}

std::unique_ptr<Screen> makeDateTime() {
  return std::make_unique<DetailScreen>("Date & time", [](App& app) {
    hal::DateTime t;
    if (!app.rtcTime(t)) return DetailScreen::Rows{{"RTC", "Missing"}, {"Time", "Unknown"}};
    return DetailScreen::Rows{
        {"Date", std::to_string(t.year) + "-" + twoDigits(t.month) + "-" + twoDigits(t.day)},
        {"Time", twoDigits(t.hour) + ":" + twoDigits(t.minute) + ":" + twoDigits(t.second)},
        {"Source", "DS3231"},
    };
  });
}

}  // namespace

std::unique_ptr<Screen> makeMainMenu() {
  std::vector<ListScreen::Item> items = {
      {"IR", &assets::kIconIr, [](App& app) { app.push(makeIrMenu()); }, nullptr},
      {"NFC", &assets::kIconNfc, [](App& app) { app.push(makeNfcMenu()); }, nullptr},
      {"Games", &assets::kIconGames, [](App& app) { app.push(comingSoon("Games")); }, nullptr},
      {"WiFi Setup", &assets::kIconWifiSetup, [](App& app) { app.push(makeWifiMenu()); }, nullptr},
      {"Bluetooth Remote", &assets::kIconBluetoothRemote, [](App& app) { app.push(makeBluetoothRemote()); },
       nullptr},
      {"Settings", &assets::kIconSettings, [](App& app) { app.push(makeSettingsMenu()); }, nullptr},
  };
  return std::make_unique<ListScreen>("Main", std::move(items));
}

std::unique_ptr<Screen> makeSettingsMenu() {
  std::vector<ListScreen::Item> items = {
      {"Invert",
       nullptr,
       [](App& app) {
         app.settings().invert = !app.settings().invert;
         app.settings().save(app.hal().storage);
       },
       [](App& app) { return std::string(app.settings().invert ? "On" : "Off"); }},
      {"Date & time", nullptr, [](App& app) { app.push(makeDateTime()); }, nullptr},
      {"Firmware", nullptr, [](App& app) { app.push(makeFirmwareInfo()); }, nullptr},
  };
  return std::make_unique<ListScreen>("Settings", std::move(items));
}

}  // namespace app
