// toolkit.h - the drawing pieces every screen is built from (firmware UI plan, section 4). Positions follow
// the Flipper UI Studio templates (docs/ui/flows/templates.json) and the -new mockups pixel for pixel.
//
// Colours: white = selected / main text, gray = not selected / secondary, dark = disabled. Icons in the
// asset set are white; gray and dark versions are drawn by tinting (white pixels -> the wanted colour).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/framebuffer.h"
#include "ui/gfx.h"

namespace tk {

using ui::Color;
using ui::Framebuffer;

// ---- fixed chrome ----
struct StatusInfo {
  std::string clock;  // "12:34", or "--:--" without a time
  bool bt = false;    // icon only while connected
  bool wifi = false;
  int batteryPct = -1;  // -1 = no reading
};
void statusBar(Framebuffer& fb, const StatusInfo& s);
void titleBar(Framebuffer& fb, const std::string& title);
// rule at y149 + centred text at y151 (gray)
void bottomBar(Framebuffer& fb, const std::string& text, Color c = ui::color::kGray);
// Vertical scrollbar at x126, thumb only (2 px wide). Nothing when everything fits.
void scrollbar(Framebuffer& fb, int first, int visible, int total, int16_t y = ui::kContentY,
               int16_t h = ui::kContentH);

// ---- catalog list ----
enum class Trail : uint8_t { None, Check, Lock, Dot };
struct Row {
  std::string label;
  std::string value;            // right-aligned text (NEC, 80%, ON)
  const char* icon = nullptr;   // left icon key (folder_new, plus_new) for mixed lists
  Trail trail = Trail::None;    // right-most mark
  bool lock = false;            // lock mark before the signal bars (secured network)
  int signal = -1;              // 0-4 bars, -1 = none
  bool info = false;            // a status line that can't be selected (drawn gray, skipped by the cursor)
  bool disabled = false;        // drawn dark
  bool valueWhite = false;      // value stays white on unselected rows (settings values)
  bool valueGray = false;       // value stays gray on the selected row (IR protocol)
};
// Marquee: labels that don't fit scroll on the selected row only. Offset in characters for time `ms` since
// the row was selected.
int marqueeOffset(uint32_t ms, int overflowChars);
// Draws rows [first, first+9) of the list in the content area, frame on `sel`, n/N text goes to the bottom
// bar by the caller. selMs = ms since the selection changed (marquee).
void catalogList(Framebuffer& fb, const std::vector<Row>& rows, int sel, int first, uint32_t selMs,
                 bool editing = false);
// Text in the middle of the content area (empty lists: "No remotes").
void emptyText(Framebuffer& fb, const std::string& text);

// ---- launcher cards ----
struct Card {
  std::string label;
  const char* icon;
};
// scrollPx: content offset from the top (cards 29 px apart); sel framed white with the arrow.
void launcherCards(Framebuffer& fb, const std::vector<Card>& cards, int sel, int scrollPx, uint32_t selMs);

// ---- key / value ----
struct KV {
  std::string key, value;
};
void keyValues(Framebuffer& fb, const std::vector<KV>& rows, int16_t y0 = 30, int16_t step = 14);

// ---- centred blocks ----
// Lines centred on the screen at y, 10 px apart (or `step`); line 0 white, the rest gray unless allWhite.
void centredLines(Framebuffer& fb, int16_t y, const std::vector<std::string>& lines, bool allWhite = false,
                  int16_t step = 10);
// Busy: icon scaled x2, lines, loading animation below.
void busy(Framebuffer& fb, const char* icon, const std::vector<std::string>& lines, uint32_t ms);
// Result / message: big icon (x3) and lines.
void message(Framebuffer& fb, const char* icon, const std::vector<std::string>& lines);
// Progress: bar 108x8 + counter text below
void progress(Framebuffer& fb, int16_t y, int done, int total, const std::string& counter);
void loadingAnim(Framebuffer& fb, int16_t x, int16_t y, uint32_t ms);

// ---- overlays ----
// Confirm dialog: box centred on the screen, black inside, white frame; lines white, hint gray.
void dialog(Framebuffer& fb, const std::vector<std::string>& lines, const std::string& hint);
// Small popup menu (Rename / Delete; Resume / Restart / Exit).
void popup(Framebuffer& fb, const std::vector<std::string>& items, int sel, int16_t cy = 0);
// Toast just above the bottom bar.
void toast(Framebuffer& fb, const std::string& text);
// Failsafe progress: thin bar on the bottom edge, permille 0-1000.
void failsafeBar(Framebuffer& fb, int permille);

// ---- entry ----
// One-row carousel: the selected slot boxed in the middle, two neighbours each side in gray, arrows at
// the edges. labels[i] is what slot i shows; special slots can show an icon instead (iconFor returns a
// key or nullptr).
void carousel(Framebuffer& fb, int16_t boxY, int16_t boxH, int count, int sel,
              const std::vector<std::string>& labels, const std::vector<const char*>& icons);
// Field box with the typed text and a '_' cursor; shows the end when the text is too long.
void textField(Framebuffer& fb, const std::string& text, bool cursor = true);
// Digit boxes: done ones gray with a dot, the current one white with `current`, later ones dark;
// current = 0: all empty dark boxes (the wrong-PIN page).
void digitBoxes(Framebuffer& fb, int count, int done, char current, int16_t y = 48);

// Wraps text into lines of at most maxChars, breaking at spaces.
std::vector<std::string> wrap(const std::string& text, size_t maxChars);
std::string upper(std::string s);

}  // namespace tk
