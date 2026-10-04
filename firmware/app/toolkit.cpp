#include "app/toolkit.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "app/theme.h"
#include "assets/assets.h"

namespace tk {

using namespace ui;
namespace col = ui::color;

namespace {

const FontEntry& F() { return theme::small(); }

int16_t textW(const std::string& s) { return textWidth(F(), s.c_str()); }

void text(Framebuffer& fb, int16_t x, int16_t y, const std::string& s, Color c) {
  drawText(fb, F(), x, y, s.c_str(), c);
}

void centred(Framebuffer& fb, int16_t y, const std::string& s, Color c, int16_t x0 = 0, int16_t w = kScreenW) {
  drawTextCentered(fb, F(), y, s.c_str(), c, x0, w);
}

void right(Framebuffer& fb, int16_t r, int16_t y, const std::string& s, Color c) {
  drawText(fb, F(), (int16_t)(r - textW(s)), y, s.c_str(), c);
}

void icon(Framebuffer& fb, int16_t x, int16_t y, const char* key, Color c, int scale = 1) {
  if (!key) return;
  const PicEntry& p = theme::icon(key);
  if (c == col::kWhite) drawPic(fb, x, y, p, scale);
  else drawPicTinted(fb, x, y, p, c, scale);
}

// signal bars: 4 bars, 1 px wide, 2 px apart, heights 2/4/6/8, bottom aligned; level bars lit
constexpr int16_t kBarsW = 7;
void bars(Framebuffer& fb, int16_t x, int16_t bottom, int level, Color on) {
  for (int i = 0; i < 4; i++) {
    const int16_t h = (int16_t)(2 + 2 * i);
    fb.vline((int16_t)(x + 2 * i), (int16_t)(bottom - h + 1), h, i < level ? on : col::kDark);
  }
}

}  // namespace

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

std::vector<std::string> wrap(const std::string& t, size_t maxChars) {
  std::vector<std::string> out;
  std::string line;
  size_t p = 0;
  while (p <= t.size()) {
    size_t sp = t.find(' ', p);
    if (sp == std::string::npos) sp = t.size();
    std::string word = t.substr(p, sp - p);
    while (word.size() > maxChars) {  // a word longer than a line is cut
      if (!line.empty()) out.push_back(line), line.clear();
      out.push_back(word.substr(0, maxChars));
      word = word.substr(maxChars);
    }
    if (line.empty()) line = word;
    else if (line.size() + 1 + word.size() <= maxChars) line += " " + word;
    else out.push_back(line), line = word;
    p = sp + 1;
  }
  if (!line.empty()) out.push_back(line);
  return out;
}

// ---------------- chrome ----------------

void statusBar(Framebuffer& fb, const StatusInfo& s) {
  fb.fillRect(0, 0, kScreenW, 10, col::kBlack);
  fb.fillRect(0, 10, kScreenW, 2, col::kWhite);
  text(fb, kStatusClockX, kStatusTextY, s.clock, col::kWhite);
  if (s.bt) icon(fb, kStatusBtX, 1, "bluetooth", col::kWhite);
  if (s.wifi) icon(fb, kStatusWifiX, 1, "wifi", col::kWhite);
  const std::string pct = s.batteryPct < 0 ? "?" : std::to_string(s.batteryPct) + "%";
  right(fb, kStatusPctX + 24, kStatusTextY, pct, col::kWhite);
  const PicEntry& bat = theme::icon("battery");
  drawPic(fb, kStatusBatX, 1, bat);
  // fill the icon's inner cells by charge (the icon is drawn full)
  if (s.batteryPct >= 0 && bat.w == 10) {
    const int full = 6;  // inner width of the built-in icon
    const int lit = (s.batteryPct * full + 99) / 100;
    if (lit < full) fb.fillRect((int16_t)(kStatusBatX + 2 + lit), 3, (int16_t)(full - lit), 4, col::kBlack);
  }
}

void titleBar(Framebuffer& fb, const std::string& title) {
  fb.fillRect(0, kTitleY, kScreenW, 10, col::kBlack);
  centred(fb, kTitleTextY, title, col::kWhite);
  fb.hline(0, kTitleRuleY, kScreenW, col::kWhite);
}

void bottomBar(Framebuffer& fb, const std::string& t, Color c) {
  fb.fillRect(0, kBottomRuleY, kScreenW, kScreenH - kBottomRuleY, col::kBlack);
  fb.hline(0, kBottomRuleY, kScreenW, col::kWhite);
  centred(fb, kBottomTextY, t, c);
}

void scrollbar(Framebuffer& fb, int first, int visible, int total, int16_t y, int16_t h) {
  if (total <= visible || total <= 0) return;
  int16_t th = (int16_t)(h * visible / total);
  if (th < 4) th = 4;
  int16_t ty = (int16_t)(y + (int)h * first / total);
  if (first + visible >= total) ty = (int16_t)(y + h - th);
  fb.fillRect(kScrollbarX, ty, kScrollbarW, th, col::kWhite);
}

// ---------------- list ----------------

int marqueeOffset(uint32_t ms, int overflow) {
  // pause, one character per step, pause at the end, restart
  constexpr uint32_t kPause = 1000, kStep = 250;
  if (overflow <= 0) return 0;
  const uint32_t cycle = kPause + overflow * kStep + kPause;
  const uint32_t t = ms % cycle;
  if (t < kPause) return 0;
  const int off = (int)((t - kPause) / kStep);
  return off > overflow ? overflow : off;
}

void catalogList(Framebuffer& fb, const std::vector<Row>& rows, int sel, int first, uint32_t selMs, bool editing) {
  const int total = (int)rows.size();
  const bool scrolls = total > kRowsVisible;
  const int16_t frameW = 125;
  for (int i = 0; i < kRowsVisible && first + i < total; i++) {
    const Row& r = rows[first + i];
    const int16_t y = (int16_t)(kContentY + i * kRowH);
    const bool on = first + i == sel;
    const Color c = r.disabled ? col::kDark : on ? col::kWhite : col::kGray;
    if (on) fb.frameRect(0, y, frameW, kRowH, col::kWhite);
    // right side, right to left from x121: check/dot, bars, lock, value
    int16_t rx = 121;
    if (r.trail == Trail::Check) rx = (int16_t)(rx - 8), icon(fb, rx, (int16_t)(y + 3), "check_new", c), rx -= 3;
    else if (r.trail == Trail::Dot) rx = (int16_t)(rx - 6), icon(fb, rx, (int16_t)(y + 4), "status_dot_new", c), rx -= 3;
    if (r.signal >= 0) rx = (int16_t)(rx - kBarsW), bars(fb, rx, (int16_t)(y + 10), r.signal, c), rx -= 3;
    if (r.lock) rx = (int16_t)(rx - 8), icon(fb, rx, (int16_t)(y + 3), "lock_new", c), rx -= 3;
    if (!r.value.empty()) {
      std::string v = r.value;
      if (editing && on) v = "< " + v + " >";
      right(fb, rx, (int16_t)(y + kRowTextDy), v, r.disabled ? c : r.valueWhite ? col::kWhite : r.valueGray ? col::kGray : c);
      rx = (int16_t)(rx - textW(v) - 6);
    }
    int16_t lx = kRowTextX;
    if (r.icon) {
      icon(fb, 4, (int16_t)(y + 3), r.icon, c);
      lx = 18;
    }
    // label, clipped before the right side; marquee on the selected row
    const int maxChars = (rx + 1 - lx) / 6;
    std::string label = r.label;
    if ((int)label.size() > maxChars && maxChars > 0) {
      const int off = on ? marqueeOffset(selMs, (int)label.size() - maxChars) : 0;
      label = label.substr((size_t)off, (size_t)maxChars);
    }
    text(fb, lx, (int16_t)(y + kRowTextDy), label, c);
  }
  if (scrolls) scrollbar(fb, first, kRowsVisible, total);
}

void emptyText(Framebuffer& fb, const std::string& t) { centred(fb, 82, t, col::kGray); }

// ---------------- launcher ----------------

void launcherCards(Framebuffer& fb, const std::vector<Card>& cards, int sel, int scrollPx, uint32_t selMs) {
  fb.setClip(Rect{0, kContentY, kScreenW, kContentH});
  for (int i = 0; i < (int)cards.size(); i++) {
    const int16_t top = (int16_t)(kCardTop + i * kCardStep - scrollPx);
    if (top > kBottomRuleY || top + kCardH < kContentY) continue;
    const bool on = i == sel;
    const Color c = on ? col::kWhite : col::kGray;
    fb.frameRect(0, top, kCardW, kCardH, c);
    icon(fb, kCardIconX, (int16_t)(top + kCardIconDy), cards[i].icon, c);
    // label: up to the arrow on the selected card, marquee when too long
    const int maxChars = on ? (111 - kCardTextX) / 6 : (kCardW - 2 - kCardTextX) / 6;
    std::string label = cards[i].label;
    if ((int)label.size() > maxChars) {
      const int off = on ? marqueeOffset(selMs, (int)label.size() - maxChars) : 0;
      label = label.substr((size_t)off, (size_t)maxChars);
    }
    text(fb, kCardTextX, (int16_t)(top + kCardTextDy), label, c);
    if (on) icon(fb, 111, (int16_t)(top + 5), "arrow1", col::kWhite, 2);
  }
  fb.resetClip();
}

// ---------------- key/value ----------------

void keyValues(Framebuffer& fb, const std::vector<KV>& rows, int16_t y0, int16_t step) {
  for (size_t i = 0; i < rows.size(); i++) {
    const int16_t y = (int16_t)(y0 + i * step);
    text(fb, 5, y, rows[i].key, col::kGray);
    right(fb, 121, y, rows[i].value, col::kWhite);
  }
}

// ---------------- centred blocks ----------------

void centredLines(Framebuffer& fb, int16_t y, const std::vector<std::string>& lines, bool allWhite, int16_t step) {
  for (size_t i = 0; i < lines.size(); i++)
    centred(fb, (int16_t)(y + i * step), lines[i], i == 0 || allWhite ? col::kWhite : col::kGray);
}

void loadingAnim(Framebuffer& fb, int16_t x, int16_t y, uint32_t ms) {
  const GifEntry& g = theme::loading();
  const int frame = g.frameDelayMs ? (int)((ms / g.frameDelayMs) % (uint32_t)g.frameCount) : 0;
  drawGifFrame(fb, x, y, g, frame);
}

void busy(Framebuffer& fb, const char* ic, const std::vector<std::string>& lines, uint32_t ms) {
  if (ic) icon(fb, centerIn(0, kScreenW, (int16_t)(theme::icon(ic).w * 2)), 40, ic, col::kWhite, 2);
  centredLines(fb, 70, lines, false, 11);
  loadingAnim(fb, 58, 110, ms);
}

void message(Framebuffer& fb, const char* ic, const std::vector<std::string>& lines) {
  if (ic) icon(fb, 52, 52, ic, col::kWhite, 3);
  centredLines(fb, ic ? 88 : 75, lines, false, 12);
}

void progress(Framebuffer& fb, int16_t y, int done, int total, const std::string& counter) {
  fb.frameRect(10, y, 108, 10, col::kWhite);
  const int16_t w = total > 0 ? (int16_t)(104 * done / total) : 0;
  fb.fillRect(12, (int16_t)(y + 2), w, 6, col::kWhite);
  centred(fb, (int16_t)(y + 18), counter, col::kGray);
}

// ---------------- overlays ----------------

void dialog(Framebuffer& fb, const std::vector<std::string>& lines, const std::string& hint) {
  const int16_t h = (int16_t)(10 * lines.size() + 26 - (hint.empty() ? 14 : 0));
  const int16_t y = centerIn(0, kScreenH, h);
  fb.fillRect(6, y, 116, h, col::kBlack);
  fb.frameRect(6, y, 116, h, col::kWhite);
  for (size_t i = 0; i < lines.size(); i++) centred(fb, (int16_t)(y + 7 + 10 * i), lines[i], col::kWhite, 6, 116);
  if (!hint.empty()) centred(fb, (int16_t)(y + 12 + 10 * lines.size()), hint, col::kGray, 6, 116);
}

void popup(Framebuffer& fb, const std::vector<std::string>& items, int sel, int16_t cy) {
  size_t maxLen = 0;
  for (const auto& s : items) maxLen = std::max(maxLen, s.size());
  const int16_t w = (int16_t)(maxLen * 6 + 24);
  const int16_t h = (int16_t)(items.size() * 14 + 4);
  const int16_t x = centerIn(0, kScreenW, w);
  const int16_t y = cy ? (int16_t)(cy - h / 2) : (int16_t)(centerIn(0, kScreenH, h) + 4);
  fb.fillRect(x, y, w, h, col::kBlack);
  fb.frameRect(x, y, w, h, col::kWhite);
  for (size_t i = 0; i < items.size(); i++) {
    const int16_t ry = (int16_t)(y + 2 + 14 * i);
    const bool on = (int)i == sel;
    if (on) fb.frameRect((int16_t)(x + 2), ry, (int16_t)(w - 4), 14, col::kWhite);
    text(fb, (int16_t)(x + 9), (int16_t)(ry + 3), items[i], on ? col::kWhite : col::kGray);
  }
}

void toast(Framebuffer& fb, const std::string& t) {
  const int16_t w = (int16_t)(textW(t) + 16);
  const int16_t x = centerIn(0, kScreenW, w);
  fb.fillRect(x, 128, w, 14, col::kBlack);
  fb.frameRect(x, 128, w, 14, col::kWhite);
  centred(fb, 131, t, col::kWhite, x, w);
}

void failsafeBar(Framebuffer& fb, int permille) {
  if (permille <= 0) return;
  fb.fillRect(0, kScreenH - 2, (int16_t)(kScreenW * std::min(permille, 1000) / 1000), 2, col::kWhite);
}

// ---------------- entry ----------------

void carousel(Framebuffer& fb, int16_t boxY, int16_t boxH, int count, int sel,
              const std::vector<std::string>& labels, const std::vector<const char*>& icons) {
  if (count <= 0) return;
  const int16_t cx = 64;
  fb.frameRect((int16_t)(cx - 12), boxY, 24, boxH, col::kWhite);
  static const int16_t kOff[] = {-50, -26, 0, 26, 50};
  for (int k = 0; k < 5; k++) {
    const int idx = ((sel + k - 2) % count + count) % count;
    const Color c = k == 2 ? col::kWhite : col::kGray;
    const int16_t x = (int16_t)(cx + kOff[k]);
    const char* ic = idx < (int)icons.size() ? icons[idx] : nullptr;
    if (ic) {
      const PicEntry& p = theme::icon(ic);
      icon(fb, centerIn((int16_t)(x - 12), 24, (int16_t)p.w), centerIn(boxY, boxH, (int16_t)p.h), ic, c);
    } else if (idx < (int)labels.size()) {
      centred(fb, centerIn(boxY, boxH, 8), labels[idx], c, (int16_t)(x - 12), 24);
    }
  }
  // arrows: 4x7 triangles at the edges, centred on the box
  const int16_t ay = (int16_t)(centerIn(boxY, boxH, 7));
  for (int i = 0; i < 4; i++) {
    fb.vline((int16_t)(1 + i), (int16_t)(ay + 3 - i), (int16_t)(1 + 2 * i), col::kGray);
    fb.vline((int16_t)(126 - i), (int16_t)(ay + 3 - i), (int16_t)(1 + 2 * i), col::kGray);
  }
}

void textField(Framebuffer& fb, const std::string& t, bool cursor) {
  fb.frameRect(6, 34, 116, 18, col::kWhite);
  constexpr size_t kMax = 17;
  std::string s = cursor ? t + "_" : t;
  if (s.size() > kMax) s = s.substr(s.size() - kMax);
  text(fb, 11, 39, s, col::kWhite);
}

void digitBoxes(Framebuffer& fb, int count, int done, char current, int16_t y) {
  const int16_t bw = count > 6 ? 12 : 14, gap = count > 6 ? 2 : 4;
  const int16_t total = (int16_t)(count * bw + (count - 1) * gap);
  const int16_t x0 = centerIn(0, kScreenW, total);
  for (int i = 0; i < count; i++) {
    const int16_t x = (int16_t)(x0 + i * (bw + gap));
    if (i < done) {
      fb.frameRect(x, y, bw, 16, col::kGray);
      fb.fillRect(centerIn(x, bw, 4), centerIn(y, 16, 4), 4, 4, col::kWhite);
    } else if (current == 0) {  // no entry going on (wrong PIN page): every box empty and dark
      fb.frameRect(x, y, bw, 16, col::kDark);
    } else if (i == done) {
      fb.frameRect(x, y, bw, 16, col::kWhite);
      const char s[2] = {current, 0};
      centred(fb, centerIn(y, 16, 8), s, col::kWhite, x, bw);
    } else {
      fb.frameRect(x, y, bw, 16, col::kDark);
    }
  }
}

}  // namespace tk
