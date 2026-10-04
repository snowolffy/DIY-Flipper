# Architecture

How the pieces fit, what each folder owns and the formats they share. *Why*: [DECISIONS.md](DECISIONS.md).
What's done: [STATUS.md](STATUS.md).

## The big picture

```
 Flipper UI Studio (claude.ai artifact)              this repository
 ─────────────────────────────────────              ───────────────────────────────────────────────────────
 assets, mockups, UI flows
   │ export .h (RGB565 icons, 1bpp fonts) ───────▶ firmware/assets/generated/*.h  (built in)
   │ export flows + screens ─────────────────────▶ docs/ui/flows/  (flow-*-new.json + screens/*.png)
   │ theme pack .zip / .c16 / .b1f ──────────────▶ sim import / Files tab ──▶ sd:/system/theme/<name>/

                         firmware/  (ESP32-S3 AND the emulator)
                           app/ OS + modules ──▶ hal/hal.h  ◀── board/board_profile.h (pins, parts, speeds)
                                                    │
                     ┌──────────────────────────────┴─────────────────────────────┐
                     ▼                                                            ▼
        firmware/platform/esp32/ (PlatformIO)                        simulator/core/mocks.cpp
        Adafruit ST7735, LittleFS, SD, RTClib,                                    │
        PN532, RMT IR, NimBLE HID, WiFi                      simulator/core/commands.cpp  ◀── the one
                                                                                  │          vocabulary
                                               ┌──────────────────────────────────┼──────────────────────┐
                                               ▼                                  ▼                      ▼
                                       sim (window: web UI)            sim (terminal commands)     sim run (scripts)
```

**Firmware code never talks to hardware directly.** Everything goes through `firmware/hal/hal.h`; pins only
through `firmware/board/board_profile.h`.

## Folders

| Path | Owns |
|---|---|
| `firmware/hal/hal.h` | interfaces: Clock, Display (pushes a dirty window), Backlight, Input, Storage, Battery, Rtc, Buzzer, Power, Ir, Nfc, Wifi, Ble; the `hal::Hal` bundle |
| `firmware/board/board_profile.h` | parts, pin table, SPI clock, fps cap, I2C addresses, reserved-GPIO rules |
| `firmware/ui/` | `Framebuffer` 128×160 RGB565 (clip, dim, diff), `colors.h`, drawing + layout constants (`gfx.h`) |
| `firmware/app/app.*` | `App`: screen stack (push/pop/replace/popTo/popToDepth/reset), gestures → screens, system events, transitions, status bar, toast, idle dim/sleep, Power button, failsafe watch, the push policy |
| `firmware/app/input.*` | `InputRecognizer`: debounce, tap/hold/repeat/release, combos, stale buttons |
| `firmware/app/toolkit.*`, `widgets.*`, `valuelist.*` | drawing pieces and screen templates (list, dialog, popup, page, text input, digit entry, toast, value rows) |
| `firmware/app/shell.*` | Main-new flow: M1 boot status, M2 logo, M3 lock, M4 PIN + lockout, M5 home, M6 launcher, M7 emergency; battery, deep-sleep resume |
| `firmware/app/security.*` | PIN hash, emergency code, the shared wrong-try counter and lockout (flash) |
| `firmware/app/settings.*` | `/settings.ini` |
| `firmware/app/theme.*` | built-in assets or an SD theme pack (format 2 `.c16`, format 1 `.b1i`, `.b1f` fonts) |
| `firmware/app/app_rules.h`, `app_host.*` | rules for games/apps, the host page (G3/G4/G5), `builtin<T>()`, `validatePack()` |
| `firmware/app/modules/` | ir, nfc, games, wifi, bt, settings - one flow each |
| `firmware/platform/esp32/` | drivers + `main.cpp` (compiled by PlatformIO only) |
| `simulator/core/` | mocks, `Simulator` (virtual clock, power model), `commands` (+ state JSON), `script`, `importer` |
| `simulator/cli/` | `sim`: `server.cpp` (session), `client.cpp` (terminal), `words.cpp` (terminal words), `platform.cpp` |
| `simulator/web/` | the window: `index.html`, `app.css`, `app.js`, fonts - embedded by `cmake/embed_web.cmake` |
| `docs/ui/` | the Studio export: flows, mockup PNGs, registry, templates |

## The loop (every 10 ms)

1. Battery sample (1 s), buttons → gestures (`deferMask` from the top screen).
2. Waking (screen off / light sleep): the waking press does nothing else; the lock screen goes up if a PIN is set.
3. Power is handled by `App` (sleep by the Sleep mode setting) except on the boot pages.
4. Raw levels to a level-2 app, gestures to the top screen; the stack changes after each event.
5. Failsafe watch (Cancel held 3 s while an app is hosted), radio state edges → system events.
6. Top screen `onTick`, then events it fired, battery checks, idle dim/sleep.
7. Render at most every 33 ms (30 fps cap, `board::kMaxFps`): draw the stack (overlays over the dimmed
   screen below), toast, failsafe bar; compose a running transition; **push only the rectangle that differs
   from what the panel shows** - a still screen sends nothing.

Screens have **mockup codes** (`code()`): `n_<code with - → _>` is the node id in the flow JSON.

## Input gestures (flow schema)

tap on press, or on release (before 500 ms) when the screen's `deferMask` says the button has a hold/repeat
meaning (lists: Cancel; text: OK + Cancel); hold once at 500 ms; repeat at 500 ms then ×0.8 down to 40 ms;
release always; a press while another button is down is a combo (`held` set) and the held button's own
events are dropped; buttons still down after a screen change are stale until released; debounce 25 ms.

## Transitions

`cut`; `slide`/`push` (dir = where things move) and `fade` (through black between pages, a cross-fade onto a
dialog) composed per frame from a snapshot of the old frame and the live new one.

## Emulator session

`sim serve` owns one `Simulator` on `<project>/storage`. HTTP on 127.0.0.1 (port in
`<project>/.sim-session.json` with a random token; Host and Origin checked). While a window polls (header
`X-Sim-Window`) the device runs in real time; otherwise only commands move time. Buttons tapped faster than a
loop tick are held 50 ms. A `sim gui` session ends 4 s after its window stops polling.

API: `GET /api/state[?full=0]`, `GET /api/frame` (RGB565 LE + backlight byte), `GET /api/screen.png?scale=`,
`POST /api/cmd` (a command object), `POST /api/words` (terminal words), `/api/pause`, `/api/files`,
`/api/import?name=`, `/api/reset-storage`, `/api/open-folder`, `/api/keys`, `/api/script/run|stop|run-all`,
`/api/record`, `/api/save-log`, `/api/close`.

## File formats

| File | Format |
|---|---|
| asset headers | RGB565 `uint16_t`, `idx = y*w + x`, `0xF81F` transparent; fonts 1bpp LSB-first, colour chosen when drawn |
| `.c16` | `"C16" 01`, u16 w, h, frames, delay ms, key colour; frames of w*h RGB565 LE (Studio THEME_FORMAT_SPEC) |
| `.b1i` / `.b1f` | format-1 images (ink → white) / 1bpp fonts, as before |
| `theme.ini` | `[theme] format=2 name= font_large= font_small= splash= wallpaper=` and `[icons] key=path` |
| `flash:/settings.ini` | `key=value`: brightness, dim_after_s, sleep_mode, sleep_after_min, low_battery_pct, button_sound, notify_sound, volume, lock_message, clock_24h, theme |
| `flash:/security.ini` | pin (salted hash), wrong, rounds, lockout_s, lockout_until (RTC epoch) |
| `flash:/wifi.ini` | `ssid<TAB>password` per line (plain text, as the plan says) |
| `flash:/ble_seen.ini` | `host<TAB>HH:MM` last connected |
| `sd:/ir/<CATEGORY>.ir` | `NAME<TAB>PROTOCOL<TAB>0xADDR<TAB>0xCMD<TAB>raw,us,...` |
| `sd:/nfc/<NAME>.nfc` | `type=`, `uid=`, one `block=` per block (`??..` = unreadable) |
| `sd:/games/<dir>/manifest.ini` | `name= type=canvas|screens engine=sprite2d|menu_flow scene=` / `page=kind|TITLE|...`; `bind=`, `bypass=` refused |
| `sd:/games/save/<id>.ini` | an app's `key=value` saves |
| scripts | `{name, initial_state, events:[{t_ms, type, ...}]}` - events are commands or `assert` checks (`simulator/core/script.h`) |
