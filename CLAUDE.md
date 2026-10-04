# CLAUDE.md

DIY Flipper: firmware for an ESP32-S3 handheld (ST7735 128×160 RGB565) plus `sim`, a one-file emulator that
runs the same `firmware/` code on a virtual clock (terminal, window, CI).

**Scope: DIY Flipper only.** Don't pull code, assets, file paths or naming from any other project, even if
another repository is attached to the session.

**Start every session by reading [docs/STATUS.md](docs/STATUS.md)**, then [docs/DECISIONS.md](docs/DECISIONS.md)
(settled questions - don't re-open them silently) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build and verify
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel && ctest --test-dir build --output-on-failure`
- Keep the build free of warnings (`-Wall -Wextra`, MSVC `/W4`) and ctest green.
- ESP32-S3: `pio run -e esp32s3` (PlatformIO). The container may not reach the PlatformIO registry; CI
  builds it on every push (job "firmware (ESP32-S3 N16R8)").

## See your own work (always, after any UI change)
The emulator is how you look at the device. From the repo root:
```
build/sim open --project /tmp/p          # use a copy: cp -r sim /tmp/p (the session writes to storage/)
build/sim wait 5400                      # boot takes ~5 s to the lock screen
build/sim press OK; build/sim press CANCEL
build/sim state                          # screen code (mockup code, e.g. T0) + menu path
build/sim shot /tmp/s.png --scale 3      # then open the PNG and look at it
build/sim shot-ui /tmp/w.png             # the whole window (Chromium is in /opt/pw-browsers)
build/sim close
```
For a screen deep in a flow it is quicker to write a script (`tools/gen_flow_scripts.py` has a step
language: `O C L R P`, `hC`, `w500`, `=CODE`, `t:TEXT`, `d:123456` ...) with `{"type":"dump","path":...}`
events, run it with `build/sim run`, and compare the PNG with the mockup in `docs/ui/flows/screens/`.

## Rules
- `firmware/` (except `platform/esp32`) must not include Arduino, OS, GUI or emulator headers. Hardware goes
  through `firmware/hal/hal.h`; pins and parts only through `firmware/board/board_profile.h`.
- Screen codes are the mockup codes (M1, S2, B-M0 ...). A new screen in a flow gets its mockup code; the
  unit test checks every flow screen is reached by a script.
- Layout numbers live in `firmware/ui/gfx.h` and `firmware/app/toolkit.cpp` and are measured from the
  mockups; colours only in `firmware/ui/colors.h`.
- Fonts, icons and the splash go through `theme::` so SD themes can replace them. Asset headers in
  `firmware/assets/generated/` come from Flipper UI Studio exports - re-export, don't hand-edit.
- App rules (bypass levels, failsafe, budgets) live in `firmware/app/app_rules.h`; built-in apps register
  with `apps::builtin<T>()`.
- Every change to the emulated world is a command (`simulator/core/commands.h`): window, terminal and
  scripts share it. New commands/checks: document them at the top of `commands.h` / `script.h` and in
  `simulator/cli/words.cpp` (the terminal words).
- Flow-walk scripts are generated: edit `tools/gen_flow_scripts.py`, run it, commit both. When a UI change
  is intended, re-lock `framebuffer_hash_equals` values there and say so in the commit message.
- Files under `sim/` are device data: byte-exact (`.gitattributes` marks them `-text`).
- Screens show problems to the user (no SD card, write failed, bad file); don't fail silently.

## Workflow
- Commit with a message that says what changed and why; CI (Linux, Windows, ESP32-S3) must stay green.
- End every piece of work by updating `docs/STATUS.md` (and DECISIONS.md if a decision changed).
- Replies to the owner are in Thai; code, comments and docs in English. On-device text is English.
