# Decisions

What was decided and why, so a later session doesn't re-open settled questions by accident. Change a
decision on purpose: update this file in the same commit and say why. The plans
([firmware UI](PLAN-firmware-ui.md), [emulator](PLAN-emulator.md)) win over older entries; locked plan items
(**[ล็อกแล้ว]**) are not repeated here.

## Product scope

- **DIY Flipper only.** No code, assets, paths or conventions from other projects.
- **Two tools:** Flipper UI Studio (claude.ai artifact) designs assets, mockups and flows; this repo builds
  them. The link is one-way: export from the Studio into `docs/ui/` and `firmware/assets/generated/`.
- The emulator is **one exe** (`sim`) that runs on Linux and Windows on the real project folder.

## Hardware (board profile: `firmware/board/board_profile.h`)

- ESP32-S3-DevKitC-1 N16R8, ST7735 1.8" RGB565, 4-button module + Power micro switch, PN532 I2C, DS3231,
  SPI SD, separate IR TX/RX, KY-006 buzzer, LiPo + TP4056 + MT3608 + toggle switch, 100k/100k divider.
- Pins: the plan's draft table, plus PN532 IRQ 41 / RST 42 (the library wants them). Reserved and refused by
  the pin test: 26-37 (flash + octal PSRAM; the plan listed 35-37 - the N16R8 uses 33-37 for PSRAM and 26-32
  for flash, so all of 26-37 is out), 0/3/45/46, 19/20, 43/44, 38/48.
- Display SPI 27 MHz (a full frame ≈ 12 ms), 30 fps cap. Button polarity is a profile setting (unknown yet).

## UI rules taken from the plans and flows

| Topic | Decision | Why |
|---|---|---|
| Buttons | plan §1.4 / Studio player v8 (tap on press unless the screen gives the button a hold meaning, hold 500, repeat 200 ×0.8 ≥ 40, combo drops the held button's own events, release event) | replaces the old Short/Long/Repeat rules |
| Colours | grey scale per the plan; all colours in `ui/colors.h` (`kAccent` = white until an accent is chosen); dim behind dialogs = channel × 100/256 | the mockups' exact values (white → 0x630C) |
| Invert | removed everywhere (setting, scripts, display call) | decided: no display invert |
| Screen codes | the mockup codes (M1, S2, B-M0, ...) | trace code ↔ flow node |
| Layout | measured from the mockup PNGs (status text y2, title y14, rows 14 px, KV x5..121, dialog hint 5 px under the lines ...) | "match screens/*.png in position and size" |

## Decisions made while building (plan items marked [ร่าง] and section 7)

| Item | Decision | Why |
|---|---|---|
| Lockout | 3 wrong tries → 30 s, each further lockout doubles (1, 2, 4, 8 min), cap 10 min; a correct entry resets; 3 tries again after each lockout | the plan's example; tries per round keep the "N tries left" message meaningful |
| Lockout over a reboot | `security.ini` keeps the counter and the lockout; the end time uses the DS3231 when it has time, otherwise the whole lockout is served again | the plan: a reboot must not clear it; no RTC → err on the safe side |
| Emergency code | 8 digits **40917263**, assembled from hash constants in `security.cpp` only inside the one check | the plan: scattered constants, one check |
| Emergency entry | Power held on M1, or Cancel held + OK | a second way in case Power is hard to hold while booting |
| Lock message | max 20 characters (one line), upper and lower case + space | the plan's proposal |
| Lock clock | the 8×8 font at ×3 with a narrow colon | the plan allows it; a big digit font can come later |
| Toasts / results | toast 1.2 s; "Sent" 1 s; "Saved" 1.2 s; "Connected" 1.5 s; "Wrong PIN" 1.5 s (flow timers) | the flows' numbers |
| Timeouts | IR learn 15 s, WiFi connect 15 s | the plan's "~15 s" |
| Time zone | fixed UTC+7; the DS3231 keeps local time | plan §7 |
| Firmware page joke | codename "PIE"; no joke yet | left open as allowed |
| Accent colour | none (`kAccent` = white) | plan §7 |
| Settings save | every change saved to flash on OK (numbers on OK, toggles at once, brightness live while editing) | plan [ร่าง] |
| Number rows | Brightness 10-100 % step 10; Dim after 10/20/30/60/120/300 s or Never; Sleep after 1/2/3/5/10/15/30 min; Low battery 5-30 % | readable steps with < > |
| Sleep modes | Deep = deep sleep (OK/Power wake, back to the open module); Light = light sleep; Off / Never = screen off only (Never: no idle sleep at all); Power always sleeps by the mode (Never → screen off) | the flow lists the four; Power must work |
| Deep sleep resume | back to the launcher row and the module that was open (not deeper pages), through the lock screen | serialising any page is fragile; module level matches "same screen" closely |
| Low battery | toast + beep at the Low battery setting; at ≤ 3 % save settings and switch off; apps get `onSaveRequest` below 5 % | plan 3.2 |
| Bottom bars | lists show n/N (info rows not counted); BT menus show none (mockup); pages show their button hints | mockups |
| Lists wrap | < at the top goes to the bottom and back | 2 buttons only: wrapping saves presses |
| BT advertising | starts on entering Bluetooth, stops on leaving when nothing is connected; the device name "Pie Controller"; new hosts confirm with a dialog (no passkey shown, as in the mockup) | plan [ร่าง] + mockups |
| BT keys group | "KEYS" page (code B1k): ↑ ↓ ← → PgUp PgDn Esc Enter | the flow says it exists without a node |
| BT host names | the emulator uses names; on the device a bond keeps only an address, so hosts show as addresses | NimBLE bonds store no name |
| WiFi disconnect | immediate, toast "DISCONNECTED", no dialog | plan [ร่าง] |
| NFC cards | MIFARE Classic: every block with the default key, unreadable sectors kept as `??`; other cards: UID only | plan [ร่าง] |
| NFC emulate | allowed for UID-only dumps and complete Classic dumps; otherwise E1p "needs full MIFARE authentication" | the mockup's note |
| NFC write | block 0 only on a magic card; skipped sectors reported on W5a; type must match first | the flow |
| IR storage | one file per category on the SD card | rename/delete a category = one file |
| Categories order | alphabetical (the mockup shows creation order) | files have no creation order on FAT/LittleFS |
| Games | built-in Snake (level 0), Pong (level 1), Reaction (level 2) - one per bypass level; SD packs: `sprite2d` shows the pack's scene (a minimal engine), `menu_flow` pages as standard lists/messages | the plan needs the mechanisms; a full pack engine is not specified |
| Failsafe | watched by `App` from the debounced Cancel level, so it works at every level and over the pause menu; bar from 1.5 s; G5 0.5 s | plan 3.2 |
| `onExit` budget | 200 ms, measured; the firmware is single-threaded, so it can't be cut mid-call | reported as a limit |
| Theme picker | Settings > System > Theme (code Y7) appears only when theme packs are on the SD card | themes existed before; the flow has no row for them |
| Home wallpaper | the theme's `wallpaper` (128×137) or a light-grey placeholder | the mockup's wallpaper is a placeholder sketch |
| Boot logo | cut from the "Boot Load" mockup (`tools/png_to_pic.js`) - the Studio export has no logo asset | match the mockup |
| Catalog events | `sd_removed/inserted`, `battery_low/critical`, `wifi_lost` fire although no transition uses them | they are in every flow's event list |
| Transitions | fade between pages goes through black; a fade onto a dialog cross-fades | M2 note "fade to black"; dialogs should keep the page visible |

## Emulator

| Decision | Why |
|---|---|
| Web UI served by the exe (cpp-httplib), opened as a Chrome/Edge app window | one portable file, no GUI toolkit; the same page works for `shot-ui` |
| Session file + random token, Host/Origin checks | only this machine and this page can drive it |
| Real time while a window is open, virtual time otherwise | the plan; terminal sessions repeat exactly |
| `sim press` from the terminal waits 300 ms after the release (logged as `wait 300`) | the next `sim shot` shows where the press led, not a half-finished transition |
| Taps shorter than 50 ms are held to 50 ms | a key tap inside one loop tick would never reach the firmware |
| Frame hash = FNV-1a 64 over the RGB565 pixels as 16-bit little-endian | the plan |
| `display_static_ms` check | proves a still screen isn't pushed again |
| The plan's old scripts: invert ones deleted; the rest rewritten for the colour UI with the same intent | their menu paths belonged to the 1-bit UI |
| macOS dropped from CI | allowed by the plan |

## ESP32-S3 libraries

| Part | Library | Why |
|---|---|---|
| Display | Adafruit ST7735 + GFX | simple, windowed `writePixels` for dirty rectangles |
| Flash / SD | Arduino core LittleFS / SD | built in |
| RTC | Adafruit RTClib | standard DS3231 support |
| NFC | Adafruit PN532 (I2C) | Classic auth/read/write; emulation is limited by the chip |
| IR | ESP-IDF RMT driver directly | the plan asks for RMT; own NEC/Samsung/Sony/RC5 coder |
| BLE | NimBLE-Arduino 1.4.x (HID; library versions are ranges in platformio.ini) | small, bonds with LRU drop, passkey confirm callback |
| Platform | espressif32 6.9.0 (Arduino core 2.0.17) | stable 2.x APIs (LEDC, RMT, sleep) |
