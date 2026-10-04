# DIY Flipper

Firmware and emulator for the DIY Flipper ("Pie Controller") handheld: ESP32-S3 N16R8, ST7735 128×160
**RGB565** display, five buttons (OK, Cancel, <, >, Power), IR, NFC (PN532), WiFi, Bluetooth LE, DS3231 RTC,
SD card, LiPo battery.

The firmware's code under `firmware/` runs unchanged in two places: on the ESP32-S3 (PlatformIO) and inside
`sim`, a single-file emulator on a virtual clock. The UI is built from the Flipper UI Studio flows and
mockups in `docs/ui/` and matches them pixel for pixel.

Docs: [status](docs/STATUS.md) · [decisions](docs/DECISIONS.md) · [architecture & formats](docs/ARCHITECTURE.md)
· [hardware bring-up](docs/BRINGUP.md) · plans: [firmware UI](docs/PLAN-firmware-ui.md),
[emulator](docs/PLAN-emulator.md). Rules for contributors and Claude sessions: [CLAUDE.md](CLAUDE.md).

## Layout

```
firmware/                 code that runs on the device (and in the emulator)
  hal/hal.h               hardware interfaces: display, backlight, input, storage, battery, RTC, buzzer,
                          power, IR, NFC, WiFi, BLE
  board/board_profile.h   the one place parts, pins, bus speeds and the fps cap are written down
  ui/                     RGB565 framebuffer, colours, drawing, the layout constants (gfx.h)
  app/                    the OS: App (screen stack, gestures, events, transitions, push policy), input,
                          toolkit + widget screens, shell (boot/lock/PIN/home/launcher/emergency), security,
                          settings, theme loader, app host + app_rules.h, modules/ (IR, NFC, Games, WiFi,
                          Bluetooth, Settings)
  assets/                 fonts/icons/animation exported from Flipper UI Studio (generated/*.h) + registry
  platform/esp32/         ESP32-S3 drivers and main.cpp (PlatformIO only)
  platform/host/          stand-in <Arduino.h> for host builds
simulator/
  core/                   mocks, virtual-clock Simulator, commands (the one vocabulary), scripts, importer
  cli/                    sim: session server, terminal commands, script runner
  web/                    the emulator window (embedded into sim at build time)
sim/                      the emulator's project folder: storage/ (flash + SD as real folders), seeds/,
                          fixtures/, scripts/
docs/ui/flows/            the UI flows (flow-*-new.json) and their mockups (screens/*.png)
tools/                    gen_flow_scripts.py (flow-walk scripts), png_to_pic.js
tests/                    unit tests, app-rule build checks, terminal session test
platformio.ini            the ESP32-S3 build
```

## Build and test (emulator)

CMake 3.21+ and a C++17 compiler (GCC, Clang or MSVC).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`ctest` runs the unit tests, every script in `sim/scripts` (each flow walked screen by screen, 96/96 flow
screens covered), the app-rule build checks (an app that breaks the bypass rules must not compile) and the
terminal session path.

## Use the emulator

One exe, `build/sim` (`sim.exe` on Windows, the CI artifact `diy-flipper-sim-windows`). It works on the
project folder (`./sim` by default, `--project DIR` for another).

```sh
sim gui                        # the window (Chrome/Edge app window, else the default browser)
sim open                       # a session without a window (virtual time: moves only when told)
sim press OK                   # buttons: OK CANCEL LEFT RIGHT POWER; hold BTN [ms]; down/up for combos
sim wait 500
sim state                      # screen code, menu path, every mock, power, display hash (--json)
sim shot screen.png --scale 3  # the device screen
sim shot-ui window.png         # the whole window (needs Chrome/Chromium/Edge)
sim set battery 20             # every mock: sim set --help
sim import theme.zip           # Import Asset (.c16 .b1i .b1f theme .zip)
sim close
sim run sim/scripts            # scripts headless, each on a fresh copy of storage
```

The window and the terminal drive the same session through the same commands; the window's command log
shows each click in terminal words and can be saved as a script. Keys: Z = OK, X = Cancel, ←/→, P = Power
(Edit keys to change); right-click a button to keep it held.

## Build the firmware (ESP32-S3)

```sh
pip install platformio
pio run -e esp32s3              # build; CI does this on every push and prints flash/RAM use
pio run -e esp32s3 -t upload    # flash
```

The drivers compile but have not run on hardware yet: follow [docs/BRINGUP.md](docs/BRINGUP.md).

## Third-party code

nlohmann/json (MIT), miniz 3.0.2 (MIT), cpp-httplib 0.18.3 (MIT) in `simulator/third_party`; IBM Plex Sans
and Mono (SIL OFL 1.1) in `simulator/web/fonts`. The ESP32 build pulls Adafruit GFX/ST7735/RTClib/PN532 and
NimBLE-Arduino through PlatformIO.
