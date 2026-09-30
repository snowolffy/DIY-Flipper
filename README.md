# DIY Flipper

Firmware and desktop simulator for the DIY Flipper handheld: ESP32, ST7735 128×160 in strict black and white,
five buttons (OK, Cancel, Left, Right, Power).

The firmware's UI and app code runs unchanged in two places: on the device, and inside a simulator on a
virtual clock. That lets the menus be written and tested without flashing, including in a cloud container
with no display.

## Layout

```
firmware/            code that runs on the device
  hal/hal.h          hardware interfaces: display, input, storage, battery, RTC, clock
  ui/                1-bit framebuffer, drawing, the locked layout sizes (gfx.h)
  app/               screen stack, List/Detail templates, menus, buttons, battery, settings
  assets/            fonts, icons and splash exported from Flipper UI Studio (generated/*.h) + registry
  platform/host/     stand-in <Arduino.h> for host builds only
simulator/
  core/              mock HAL, virtual-clock simulator, JSON script runner
  headless/          sim_headless: runs scripts, prints pass/fail (used in the cloud and CI)
  gui/               sim_gui: the windowed simulator (Dear ImGui + SDL2)
sim/                 this project's simulator data
  storage/flash/     mirrors LittleFS
  storage/sd/        mirrors the SD card: /ir /nfc /games /media /system
  seeds/             starting states scripts can copy over storage/
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

The on-screen buttons work too; holding one acts like holding the key. The Mock control panel sets battery
charge or "no reading", removes the RTC or syncs it to the PC clock, ejects the SD card, and makes every
write fail. Unlike scripts, the window uses `sim/storage` directly, so settings you change stay on disk.

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

## Assets

Pictures, icons, fonts and theme packs are drawn in
[Flipper UI Studio](https://claude.ai/artifact/FAn19c48h4MqHxxMmpA4qK). Export a header there, save it into
`firmware/assets/generated/`, include it in `firmware/assets/assets.cpp` and add its entry to the table the
studio's install steps name. Theme packs go on the SD card under `/system/theme/<name>/`.

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

Done: firmware core with main menu, Settings (invert, date & time, firmware info), boot splash; mocks for
display, input, storage, battery and RTC; headless runner; windowed simulator with live mock controls;
CI on Linux, Windows and macOS with a portable Windows build.

Next, in order: IR, NFC, WiFi and BLE mocks with live controls (and loading a script into the live
window) · recent-folders list · Import Asset (`.b1i` / `.b1f` / theme `.zip` into `sim/storage`) · ESP32 target.
