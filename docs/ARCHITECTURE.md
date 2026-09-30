# Architecture

How the pieces fit, what each folder owns, and the file formats they share. For *why* things are this way,
see [DECISIONS.md](DECISIONS.md); for what's done and what's next, [STATUS.md](STATUS.md).

## The big picture

```
 Flipper UI Studio (claude.ai artifact)          this repository
 ───────────────────────────────────            ─────────────────────────────────────────────────────
 draw pictures, icons, fonts, mockups
   │ export .h  ───────────────────────────────▶ firmware/assets/generated/*.h  (built into firmware)
   │ export theme .zip / .b1i / .b1f ──────────▶ simulator Import Asset ──▶ sim/storage/sd/...

                                                  firmware/  (runs on the ESP32 AND in the simulator)
                                                    app/  UI + apps ──▶ hal/hal.h interfaces
                                                                              │
                                     ┌────────────────────────────────────────┴───────────────┐
                                     ▼                                                        ▼
                         ESP32 drivers (not written yet)                 simulator/core/mocks.cpp
                                                                                              │
                                                             ┌────────────────────────────────┤
                                                             ▼                                ▼
                                                   simulator/headless/                 simulator/gui/
                                                   sim_headless (CI, cloud)            sim_gui (ImGui + SDL2)
```

The rule that makes this work: **firmware code never talks to hardware directly**. Everything goes through
`firmware/hal/hal.h`, so the same UI code runs against mocks (tests, the window) and later against real
drivers.

## Folders

| Path | Owns | Must not |
|---|---|---|
| `firmware/hal/hal.h` | interfaces: Clock, Display, Input, Storage, Battery, Rtc, Ir, Nfc, Wifi, Ble; the `hal::Hal` bundle | include anything but the C++ standard library |
| `firmware/ui/` | `Framebuffer` (128×160, 1-bit), drawing (`gfx.h`), **the locked layout constants** | know about apps or hardware |
| `firmware/app/` | `App` (screen stack, loop tick, status bar), templates (`screens.*`), radio apps (`radio_apps.cpp`), `buttons`, `battery`, `settings`, `theme` loader, `minijson` | include Arduino, OS, GUI or simulator headers |
| `firmware/assets/` | built-in fonts/icons/splash (`generated/*.h`, exported from the studio) and the registry `assets.cpp` | be hand-edited (re-export instead) |
| `firmware/platform/` | `progmem.h`, and `host/Arduino.h` (stand-in used only by host builds) | — |
| `simulator/core/` | mocks, `Simulator` (virtual clock), script loader/player, asset importer | — |
| `simulator/headless/` | `sim_headless` CLI | — |
| `simulator/gui/` | `sim_gui` window: device view, Mock control tabs | — |
| `simulator/third_party/` | vendored `nlohmann/json.hpp` 3.11.3 and `miniz` 3.0.2 (MIT) | be modified |
| `sim/` | this repo's simulator project folder: `storage/`, `scripts/`, `seeds/`, `fixtures/` | — |
| `tests/unit_tests.cpp` | exact-value checks (battery table, buttons, RTC dates, settings, JSON, file formats, importer) | — |
| `tools/make_fixtures.py` | builds `sim/fixtures/` from the built-in asset headers | — |

## Firmware runtime

- `App::begin(coldBoot)`: load `flash:/settings.ini`, load the saved theme (falls back to built-in), push
  `SplashScreen` (cold boot) or the main menu.
- `App::tick()` runs every 10 ms: read the battery (every 1 s, 8-sample average), turn raw button levels into
  events (`Buttons`: Short < 500 ms, Long at 500 ms, Repeat every 120 ms for Left/Right), give events and a
  tick to the top screen, redraw the whole frame, push it to `hal::Display` with the invert flag.
- Screen stack changes (`push`, `pop`, `replaceTop`) are queued and applied after the current event, so a
  screen may close itself. `onResume` runs when a screen is on top again (lists reload from their source).
- `menuPath()` joins the stack titles (`Main/Settings/Theme`); scripts assert on it.
- Buttons: Left moves **up** a list, Right moves **down**, OK opens, Cancel goes back. Five buttons only.

### Screen templates (`firmware/app/screens.h`)

| Template | Class | Notes |
|---|---|---|
| List | `ListScreen` | items: label, icon key, action, optional right-aligned value; static items or a `Source` rebuilt on enter/resume; marquee for long selected labels; scrollbar only on overflow |
| Detail | `DetailScreen` | title row + label/value rows from a function; value wraps to the next row if it doesn't fit |
| Text input | `TextInputScreen` | single-row character carousel; Left/Right pick, OK add, hold OK done, Back delete |
| Canvas | (none yet) | full 160 px, no status bar |
| Notice | `NoticeScreen` | title + wrapped paragraph; OK/Back closes |

### Themes (`firmware/app/theme.*`)

All drawing gets fonts, icons and the splash from `theme::large()`, `theme::small()`, `theme::icon(key)`,
`theme::splash()`: the built-in asset unless a loaded theme replaces it. Icon slots and required sizes:

| Key | Size | Where |
|---|---|---|
| `battery` | 10×8 | status bar (fill = 4 columns at x 2–5, rows 3–4, drawn by firmware) |
| `wifi`, `bluetooth` | 8×8 | status bar, only while that radio is on |
| `ir`, `nfc`, `games`, `wifi_setup`, `bluetooth_remote`, `settings` | 12×12 | main menu |
| fonts | large 8×8, small 6×8 | everywhere |
| splash | 128×160 | cold boot |

A file of the wrong size is skipped with a warning and the built-in asset stays.

## Simulator runtime

- `Simulator` owns every mock plus the `App`, and a `VirtualClock`. `advanceTo(t)` runs 10 ms ticks
  (`MockWifi::tick()` then `App::tick()`), so a run is deterministic on every machine.
- `restart(coldBoot)` power-cycles the device: new `App`, radios off; storage, cards in the field, bonds and
  scripted networks stay.
- Mocks expose setters; **the GUI and scripts call the same setters**.
- `ScriptPlayer` plays a parsed script against a running simulator (the window's Scripts tab);
  `runScript` does the same headless on a temporary copy of `storage/` (+ seed).
- `importAsset` writes straight into the storage folder (like copying onto the card from a PC).
- `sim_gui` keyboard/mouse only write a button when its state changes, so a playing script's presses survive.

## Storage layout (device paths)

| Path | What | Written by |
|---|---|---|
| `flash:/settings.ini` | `invert=0/1`, `theme=<folder>` (omitted when built-in) | Settings |
| `sd:/ir/uncategorized/<name>.json` | saved IR remote | IR > Learn (`new_remote`, `new_remote_2`, ...) |
| `sd:/nfc/<uid>.json` | NFC dump | NFC > Read card |
| `sd:/system/theme/<name>/` | theme pack | Import Asset / copy from PC |
| `sd:/media/*.b1i` | pictures | Import Asset |
| `sd:/system/fonts/*.b1f` | fonts | Import Asset |
| `sd:/games/` | reserved | — |

In the simulator these are folders: `<project>/storage/flash/...` and `<project>/storage/sd/...`.

## File formats (shared with Flipper UI Studio — keep in sync)

**Bit order, everywhere** (framebuffer, asset headers, `.b1i`, `.b1f`): `idx = y*w + x`, `byte = idx/8`,
`bit = idx%8` (LSB-first), 1 = ink. This is *not* Adafruit `drawBitmap()` order.

`.b1i` image/animation (little-endian):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `"B1I"` + version `0x01` |
| 4 | 2 | width |
| 6 | 2 | height |
| 8 | 2 | frame count |
| 10 | 2 | frame delay ms (0 for stills) |
| 12 | … | frames, each `ceil(w*h/8)` bytes |

`.b1f` fixed-cell font:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `"B1F"` + version `0x01` |
| 4 | 1 | glyph width |
| 5 | 1 | glyph height |
| 6 | 2 | glyph count N |
| 8 | N | character code of each glyph |
| 8+N | … | glyphs, each `ceil(w*h/8)` bytes |

`theme.ini` (inside `<root>/<name>/`):

```ini
[theme]
name=Night
font_large=fonts/large.b1f
font_small=fonts/small.b1f
splash=splash.b1i

[icons]
settings=icons/settings.b1i
```

Paths are relative to the theme folder and may not contain `..`. Unknown icon keys are ignored.

Asset headers (`firmware/assets/generated/`): the studio's Export tab format — `PIC_<IDENT>[]`, and for fonts
`<IDENT>_W/_H/_COUNT/_CHARSET/_GLYPHS`. Register them in `firmware/assets/assets.cpp`.

IR remote JSON: `{"name", "protocol", "address": "0x04", "command": "0x08"}` or `"raw": ["us", ...]` for RAW.
NFC dump JSON: `{"uid", "type", "blocks": ["hex", ...]}`. Both are written and read by `minijson` (flat
objects only).

## Mock-scripts

The full event and check vocabulary is documented at the top of `simulator/core/script.h`; that comment is
the source of truth. Scripts live in `<project>/scripts/`, and `import` paths are relative to the project.
