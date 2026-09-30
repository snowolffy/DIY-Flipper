# DIY Flipper

Firmware and desktop simulator for the DIY Flipper handheld: ESP32, ST7735 128×160 in strict black and white,
five buttons (OK, Cancel, Left, Right, Power).

The firmware's UI and app code runs unchanged in two places: on the device, and inside a simulator on a
virtual clock. That lets the menus be written and tested without flashing, including in a cloud container
with no display.

Project docs: [status and next steps](docs/STATUS.md) · [decisions](docs/DECISIONS.md) ·
[architecture and file formats](docs/ARCHITECTURE.md). Rules for contributors (and Claude sessions) are in
[CLAUDE.md](CLAUDE.md).

## Layout

```
firmware/            code that runs on the device
  hal/hal.h          hardware interfaces: display, input, storage, battery, RTC, clock, IR, NFC, WiFi, BLE
  ui/                1-bit framebuffer, drawing, the locked layout sizes (gfx.h)
  app/               screen stack, List/Detail/Text-input templates, menus, IR/NFC/WiFi/BT apps, settings,
                     SD-card theme loader (theme.cpp)
  assets/            fonts, icons and splash exported from Flipper UI Studio (generated/*.h) + registry
  platform/host/     stand-in <Arduino.h> for host builds only
simulator/
  core/              mock HAL, virtual-clock simulator, JSON script runner, asset importer
  headless/          sim_headless: runs scripts, prints pass/fail (used in the cloud and CI)
  gui/               sim_gui: the windowed simulator (Dear ImGui + SDL2)
sim/                 this project's simulator data
  storage/flash/     mirrors LittleFS
  storage/sd/        mirrors the SD card: /ir /nfc /games /media /system
  seeds/             starting states scripts can copy over storage/
  fixtures/          Studio-format exports for tests (made by tools/make_fixtures.py)
  scripts/           mock-scripts (*.json)
tests/               unit tests for exact-value logic
```

The repository root is the simulator's project folder. To run two firmware builds side by side, check out
a second copy with `git worktree add ../diy-flipper-experiment <branch>` and open that folder instead.

## Build and test

Needs CMake 3.16+ and a C++17 compiler (GCC, Clang or MSVC).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure      # unit tests + every script in sim/scripts
```

Run scripts directly:

```sh
build/sim_headless sim/scripts                           # all of them
build/sim_headless sim/scripts/boot-to-settings.json     # one
build/sim_headless --dump-final screen.pbm sim/scripts/boot-to-settings.json
```

Each run works on a temporary copy of `sim/storage`, so scripts never change the checked-in files. Add
`--keep-storage` to keep the copy and print where it is.

## The simulator window

**Windows, no build needed:** open the latest run of the CI workflow on GitHub (Actions tab), download the
`diy-flipper-simulator-windows` artifact, unzip, and run `sim_gui.exe`. It needs no installer or DLLs. Run it
from the repository root to use `sim/storage`; anywhere else it creates a `sim/` folder next to itself.

**Build it yourself** (SDL2 and Dear ImGui are downloaded by CMake; on Linux install `libx11-dev libxext-dev`
first):

```sh
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Release -DDIYF_BUILD_GUI=ON
cmake --build build-gui --config Release --parallel
build-gui/sim_gui            # or build-gui/Release/sim_gui.exe with Visual Studio
```

| Key | Button |
|---|---|
| Left / Up | LEFT (moves up a list) |
| Right / Down | RIGHT (moves down a list) |
| Enter, Z | OK |
| Esc, Backspace, X | CANCEL (back) |
| P | POWER |
| F12 | save the screen as a BMP in `sim/screenshots/` |

The on-screen buttons work too; holding one acts like holding the key. Unlike scripts, the window uses
`sim/storage` directly, so what the firmware saves stays on disk.

The Mock control panel has one tab per mock:

| Tab | Controls |
|---|---|
| Project | the open project folder, recent folders, Import asset |
| Power & storage | battery charge or "no reading", RTC missing / sync to PC time, SD card in or out, failing writes |
| IR | press a remote button (protocol, address, command); list of signals the device sent |
| NFC | place a card (UID, type, block count, or a dump saved on the SD card) and take it away |
| WiFi | networks in range (name, signal, lock), whether the next connect succeeds, scan/connect latency |
| Bluetooth | a host connects or disconnects, bonded hosts, bond list full, keys the host received |
| Scripts | play any script in `sim/scripts` inside the window, with live pass/fail |

Options: `--project DIR` opens a project folder (default: the last one you had open, else `./sim`),
`--script FILE.json` plays a script as soon as the window opens, `--import FILE` imports an export at start,
`--tab NAME` opens a tab (`project`, `power`, `ir`, `nfc`, `wifi`, `bluetooth`, `scripts`). "Restart the device first" (on by default) power-cycles
the device before a script, so it replays exactly as it does headless.

## Mock-scripts

```json
{
  "name": "boot-to-settings",
  "initial_state": { "battery_percent": 80, "rtc": "2026-09-30T12:45:00" },
  "events": [
    { "t_ms": 1700, "type": "button", "button": "RIGHT" },
    { "t_ms": 2500, "type": "assert", "check": "menu_path_equals", "value": "Main/Settings" }
  ]
}
```

The full list of event types and checks is at the top of `simulator/core/script.h`. The simulator ticks every
10 ms of virtual time, so a script gives the same frames on every machine. `fb_hash` in the output is what a
`framebuffer_hash_equals` check compares against.

## Projects and Import Asset

A project folder holds `storage/`, `scripts/`, `seeds/` and `fixtures/`; this repository's `sim/` is one.
In the window, open another one from the Project tab (path or recent list) or by dropping the folder onto the
window. Missing `storage/` folders are created. Recent folders are remembered per user.

Exports from Flipper UI Studio go onto the open project's SD card the way you'd copy them from a PC, so an
ejected card or failing writes don't block them:

| File | Lands in |
|---|---|
| theme pack `.zip` | `sd:/system/theme/<name>/` (every folder in the zip that holds a `theme.ini`) |
| `.b1i` picture | `sd:/media/` |
| `.b1f` font | `sd:/system/fonts/` |

Drop the file on the window or use Project > Import asset. Each file is checked the way the firmware will
check it; zip entries that try to leave the theme folder are skipped. On the device, pick the theme in
Settings > Theme. Files that don't fit their slot (fonts must be 8×8 / 6×8, the splash 128×160, menu icons
12×12, status icons 8×8, battery 10×8) keep the built-in version, and the device says which.

## Assets

Pictures, icons, fonts and theme packs are drawn in
[Flipper UI Studio](https://claude.ai/artifact/FAn19c48h4MqHxxMmpA4qK). To build an asset into the firmware,
export a header there, save it into `firmware/assets/generated/`, include it in `firmware/assets/assets.cpp`
and add its entry to the table the studio's install steps name. To try one without rebuilding, import the
`.zip` / `.b1i` / `.b1f` into the simulator (above).

Asset data and the framebuffer use the same bit order: `idx = y*w + x`, `byte = idx/8`, `bit = idx%8`,
LSB-first, 1 = ink.

## UI rules (locked)

- Status bar (12 px) on List, Detail and Text-input screens; Canvas/Game screens get the full 160 px.
  Left slot: clock on the home menu (`--:--` without RTC time), otherwise the current section's name.
- List: 16 px rows, 9 visible, 12×12 icon, label at x=18. Left/Right move the selection, OK opens, Cancel
  goes back. Labels longer than the row scroll on the selected row. Scrollbar only when the list overflows.
- Detail: title row, then label/value rows; a value that doesn't fit beside its label takes the next row.
- Fonts: large 8×8 (menus, titles), small 6×8 (status bar, values).
- Settings > Invert swaps ink and paper at output; it is saved in `flash:/settings.ini`.

## Status

Done:
- firmware core: main menu, Settings (invert, date & time, firmware info), boot splash
- apps: IR (learn, save to `sd:/ir/uncategorized/`, send), NFC (read, save to `sd:/nfc/`, view dumps),
  WiFi Setup (scan, password on the character carousel, connect, disconnect), Bluetooth Remote
  (pair with passkey, media keys, bond list full)
- mocks for display, input, storage, battery, RTC, IR, NFC, WiFi and BLE; headless runner; windowed
  simulator with a control tab per mock and live script playback
- themes: Settings > Theme loads a pack from `sd:/system/theme/`, remembered across reboots; Import Asset
  and project folders with a recent list in the window
- CI on Linux, Windows and macOS with a portable Windows build

Not simulated yet: NFC write/emulate, IR raw capture from the GUI, NTP time from WiFi.

Next: the ESP32 target (real drivers behind the same HAL), and recording a live session as a script
(deferred to v1.1 in the plan).
