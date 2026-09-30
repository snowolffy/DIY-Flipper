# Status

Last updated: 2026-09-30, at commit `4081a00` (CI green on Linux, Windows, macOS).
**Update this file at the end of every piece of work** (what changed, CI state, what's next).

## Where things live

| Thing | Location |
|---|---|
| Firmware + simulator | this repo, branch `main` |
| Flipper UI Studio (asset designer) | https://claude.ai/artifact/FAn19c48h4MqHxxMmpA4qK |
| Its firmware-path settings | studio database doc `settings/firmware`: folder `firmware/assets/generated/`, registry `firmware/assets/assets.cpp`, theme root `/system/theme/` |
| Windows build | Actions → latest CI run → artifact `diy-flipper-simulator-windows` (`sim_gui.exe`, `sim_headless.exe`) |
| Plans this work follows | "DIY Flipper — UI Customization & Design Tool Plan" and "DIY Flipper Dev Tool — Plan Document" (given in chat, not in the repo); decisions from both are in [DECISIONS.md](DECISIONS.md) |

## Dev-tool plan progress

| Step | What | State | Commit |
|---|---|---|---|
| 1 | core library: HAL, mocks for display, input, storage, battery, RTC | done | `51f8c9d` |
| 2 | headless runner + first scenario (boot → main menu → Settings) | done | `51f8c9d` |
| 3 | native window: framebuffer, keyboard → buttons | done | `4df4b18` |
| — | fix: CRLF `settings.ini` | done | `8679d01` |
| 4 | IR, NFC, WiFi, BLE mocks + live panels; scripts in the live window | done | `e0dfe2a` |
| 5 | project folders + recent list | done | `4081a00` |
| 6 | Import Asset (`.b1i` / `.b1f` / theme `.zip`) | done, plus firmware theme loader | `4081a00` |
| 7 | record a live session as a script | deferred to v1.1 | — |

## What the firmware does today

- Boot splash (cold boot, 1.5 s or any key) → main menu: IR, NFC, Games, WiFi Setup, Bluetooth Remote, Settings.
- IR: Learn (listen → show protocol/address/command → save `sd:/ir/uncategorized/new_remote*.json`), list saved
  remotes, send.
- NFC: Read card (show type/UID/blocks → save `sd:/nfc/<uid>.json`), list and view dumps.
- WiFi Setup: scan, password via the carousel, connect, failure screen, disconnect.
- Bluetooth Remote: advertise, passkey pairing, media keys (hold Left/Right = volume), bond-list-full screen.
- Settings: Invert, Date & time, Firmware (version 0.1.0, codename `pic.h`), Theme (built-in or any pack on SD).
- Errors are shown, not swallowed: no SD card, write failures, bad theme files.
- Games: placeholder ("Not built yet").

## Tests

- `ctest`: `unit_tests` + one test per script in `sim/scripts/` (10 scripts) + `gui_smoke` when the GUI is built on Linux.
- Pinned screen hashes: `boot-to-settings` (Settings screen `2e55045db62ee172`), `theme-import` (themed splash
  `bdbe2bb0b6a1dc06`).
- CI: `.github/workflows/ci.yml`, headless and GUI jobs on ubuntu/windows/macos; GUI job uploads the Windows build.

## Known gaps

- No ESP32 target yet: no real drivers, no PlatformIO/Arduino project.
- Not simulated: NFC write/emulate, IR raw capture from the GUI, NTP time over WiFi, sleep/power.
- No picture viewer for `sd:/media/*.b1i`; Games has no games.
- Small font `&` looks like `$` (fix the glyph in the studio and re-export `font_small.h`).
- WiFi passwords are not saved; there is no saved-networks list.
- Invert changed while writes fail is not saved and no message says so.

## Next, in the order I'd do them

1. ESP32 target: PlatformIO project under `firmware/platform/esp32/` implementing `hal::*` with real drivers
   (ST7735 push of the 1-bit frame, buttons, SD + LittleFS, ADC, DS3231, IR, PN532, WiFi, BLE HID). Confirm the
   hardware assumptions in DECISIONS.md first.
2. Games (Canvas template) and a `/media` picture viewer.
3. Saved WiFi networks; NTP → RTC.
4. v1.1: record a live session as a script.

## Resuming in a new session

1. The repo's `CLAUDE.md` loads automatically; read this file, then DECISIONS.md and ARCHITECTURE.md.
2. Build and run the tests (commands in CLAUDE.md) before changing anything; they should be green.
3. Check the latest CI run on GitHub Actions.
4. If the task touches assets or layout, open Flipper UI Studio too: both sides must agree.
