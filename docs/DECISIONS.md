# Decisions

What was decided, and why, so a later session doesn't re-open settled questions by accident. Change a
decision on purpose: update this file in the same commit and say why.

## Product scope

- **DIY Flipper only.** This repo, the simulator and Flipper UI Studio serve the DIY Flipper handheld and
  nothing else. Don't bring in code, assets, paths or conventions from other projects.
- **Two tools:** the pixel/asset designer stays a Claude artifact ("Flipper UI Studio"); the emulator is this
  native app. The designer is not ported into the app. The link between them is one-way and manual: export
  from the studio, then either build it in (asset headers) or Import Asset into the simulator.

## Hardware assumptions (confirm before writing real drivers)

- Board: ESP32 (it has both WiFi and BLE).
- Display: ST7735 128×160, used strictly 1-bit black/white.
- RTC: DS3231. NFC: PN532. Five buttons: OK, Cancel, Left, Right, Power.
- Battery: single-cell LiPo read through an ADC divider; percentage from a lookup table
  (`firmware/app/battery.h`) with an 8-sample moving average.

## UI (locked; mockups in the studio use the same numbers)

| Topic | Decision | Why |
|---|---|---|
| Tone | Flipper Zero–style pixel art, utilitarian layout | 1-bit art reads as intentional; the device is a tool |
| Fonts | Large 8×8 (menus, titles, input), Small 6×8 (status bar, values) | 8 px is the smallest comfortable size on a 1.8" panel (~0.22 mm/px); 6 px fits 21 chars per row |
| Small font face | Silkscreen, caps only, glyphs centred in the 6 px cell | proportional glyphs left-aligned made "WIFI" read "WI FI" |
| Icons | status 8×8 (battery 10×8), menu 12×12 | fit a 12 px status bar and 16 px list rows |
| Status bar | 12 px, on List/Detail/Text-input, off on Canvas/Game | games and animations need all 160 px |
| Status bar left slot | clock on the home menu (`--:--` without time), otherwise the section name | tells you where you are; Detail pages show their parent section |
| Status bar right slot | battery; WiFi/BT icons only while that radio is on | agreed before this repo existed |
| List | 16 px rows, 9 visible, icon at x=2, label at x=18 (13 large chars), scrollbar only on overflow | "Bluetooth Remote" is 16 chars: it scrolls on the selected row instead of shrinking every label |
| Detail | label left, value right, value wraps to the next row | every Detail screen is key/value data |
| Navigation | Left = up, Right = down (wraps), OK = open, Cancel = back | only five buttons |
| Text input | single-row carousel; hold OK = done, Back = delete | decided before the plan |
| Boot splash | static image, cold boot only | 2.5 KB, instant; animation costs flash and boot time |
| Codename | `pic.h` | nod to the old project's asset files |
| Personalisation | Invert is a device setting; themes swap artwork only | layout-changing themes would multiply the UI code |

## The dev-tool plan's open questions

| Question | Answer |
|---|---|
| Is `firmware/` the repo checkout or a copy/symlink? | The project folder **is** the repo root; simulator data lives in `sim/`. No symlinks (Windows needs Developer Mode and git mangles them there). Two builds side by side: `git worktree`. |
| Record a live session as a script? | Deferred to v1.1. |
| Where do imported assets land? | theme `.zip` → `sd:/system/theme/<name>/`, `.b1i` → `sd:/media/`, `.b1f` → `sd:/system/fonts/`. Icons only arrive inside themes. |

## Technical

| Decision | Why |
|---|---|
| Own 1-bit framebuffer + drawing (`firmware/ui`), not Adafruit_GFX | the core must build without Arduino; Adafruit's bitmap bit order differs from ours |
| C++17, CMake, warnings as a quality gate (`-Wall -Wextra`, MSVC `/W4`) on our targets only | builds on GCC/Clang/MSVC; fetched code isn't ours to fix |
| Virtual clock with 10 ms ticks | same script → same frames on every OS; screen hashes can be pinned |
| Regression by framebuffer hash (FNV-1a 64 of the shown pixels) | cheap, exact; update deliberately when a UI change is intended |
| Mocks change only through setters shared by GUI and scripts | a live session and a script exercise the same code |
| Dear ImGui 1.91.5 + SDL2 2.30.9 via FetchContent, behind `DIYF_BUILD_GUI` | headless builds stay fast and offline |
| Static MSVC runtime, static SDL2 | `sim_gui.exe` runs with no installer or DLLs |
| Vendored `nlohmann/json` and `miniz` | no network needed for headless builds |
| `sim/**` marked `-text` in `.gitattributes` | device files must reach the simulator byte-for-byte on Windows (a CRLF bug in `settings.ini` was caught by CI) |
| Settings and theme parsers trim CR/space | files edited on a PC are CRLF |
| Import writes straight to the storage folder, ignoring the SD-present and fail-writes switches | it models copying onto the card from a PC |
| Theme loader skips a bad file and keeps the built-in one | a broken theme can't make the device unusable |
| Radios reset on restart; storage, bonds, cards in the field don't | those live outside the device |
| Sim_gui "Run here" restarts the device first by default | scripts assume their own start state; without it presses land on the wrong screen |
| Pushing straight to `main` | single-developer repo; CI on every push is the gate |
