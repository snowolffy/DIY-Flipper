# Status

Last updated: 2026-10-04, branch `rework/color-ui-emulator` (CI green: Linux, Windows, ESP32-S3).
**Update this file at the end of every piece of work.**

## Where things live

| Thing | Location |
|---|---|
| Firmware + emulator | this repo; the colour rework is on branch `rework/color-ui-emulator` (not merged to `main` yet) |
| Flipper UI Studio | https://claude.ai/artifact/FAn19c48h4MqHxxMmpA4qK (not edited by this work) |
| Emulator wireframe | https://claude.ai/artifact/7nqQ71Ka3WAyEuWYiMSe7F |
| UI flows + mockups | `docs/ui/flows/` (Studio export, unpacked from the inbox zip) |
| Windows emulator | Actions → CI run → artifact `diy-flipper-sim-windows` (`sim.exe`) |
| Plans | [firmware UI](PLAN-firmware-ui.md), [emulator](PLAN-emulator.md); the report on them: [REPORT-2026-10-04.md](REPORT-2026-10-04.md) |

## Done

| Plan phase | State |
|---|---|
| A. RGB565 assets, colour framebuffer, theme format 2 (`.c16`) | done |
| B. input gestures, toolkit, widget screens, transitions | done |
| C. board profile, Buzzer/Backlight/Power HAL, push policy (diff, 30 fps), mocks, script commands/checks | done |
| D. OS shell: boot status, logo, lock + PIN + lockout (survives reboot), home, launcher, emergency menu | done |
| E. app host + `app_rules.h`: bypass 0/1/2 with build-time checks, failsafe, battery save, `validatePack()` | done |
| F. modules: Settings, WiFi, Bluetooth, IR, NFC, Games - every state of the 7 `-new` flows | done |
| tests: 96/96 flow screens reached by scripts, 22/22 system events fired, unit tests, app-rule build checks | done |
| G. `sim` exe: session server, terminal commands, window per the wireframe, shot / shot-ui | done |
| H. ESP32-S3 PlatformIO target + drivers + CI job | **compiles (RAM 16.2 %, flash 19.8 % of the app partition); never run on hardware** |
| I. ImGui window and `sim_headless` removed, CI Linux + Windows + ESP32, docs | done |

## Drivers (ESP32-S3)

Every driver below **compiles but has never run on hardware**. Bring them up in the order of
[BRINGUP.md](BRINGUP.md).

| Driver | File | Note |
|---|---|---|
| ST7735 display, backlight PWM | `platform/esp32/drivers_basic.cpp` | colour order may need `INITR_GREENTAB` |
| buttons (5) | same | polarity from the board profile |
| LittleFS, SD (FAT32) | same | SD re-detected once a second |
| DS3231, battery ADC, buzzer, light/deep sleep, power-off, restart | same | deep sleep wakes on OK/Power (ext1 any-low) |
| IR send/receive (RMT) | `platform/esp32/drivers_radio.cpp` | NEC/Samsung/Sony decoded, others RAW; RC5 send only |
| PN532 (I2C) | same | Classic default key only; emulation shows the PN532's own ID |
| WiFi + SNTP | same | |
| BLE HID (NimBLE) | same | hosts named by address; passkey confirm blocks the BLE task up to 25 s |

## Next

1. Owner: check the pin table against the real boards, then [BRINGUP.md](BRINGUP.md) step by step.
2. Merge `rework/color-ui-emulator` into `main` after a look (PR).
3. Export more assets from the Studio if wanted: a boot logo asset (now cut from the mockup), a large digit
   font for the lock clock, a theme pack in format 2 to test against the real Studio output.
4. Later: a real pack engine for SD games (only a scene viewer and menu_flow pages now), NFC emulation with a
   chip that can present any UID, BLE host names.
