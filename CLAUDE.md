# CLAUDE.md

DIY Flipper: firmware for an ESP32 handheld (ST7735 128×160, 1-bit) plus a simulator that runs the same
firmware code headless (tests, CI, cloud) or in a window (Dear ImGui + SDL2).

**Scope: DIY Flipper only.** Nothing here relates to any other project; don't pull code, assets, file paths
or naming from elsewhere, even if another repository is attached to the session.

**Start every session by reading [docs/STATUS.md](docs/STATUS.md)** (what's done, what's next), then
[docs/DECISIONS.md](docs/DECISIONS.md) (settled questions — don't re-open them silently) and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (structure, file formats).

## Build and verify
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel && ctest --test-dir build --output-on-failure`
- Every change to firmware/ or simulator/ must keep the build free of warnings (`-Wall -Wextra`, MSVC `/W4`) and ctest green.
- To see a screen: `build/sim_headless --dump-final out.pbm <script>` writes a PBM of the last frame
  (a `dump` event in a script writes one at any time).
- GUI: configure with `-DDIYF_BUILD_GUI=ON`. It runs in this container on SDL's offscreen driver:
  `SDL_VIDEODRIVER=offscreen build-gui/sim_gui --screenshot out.bmp --frames 150` saves the whole window
  (add `--script`, `--tab`, `--import` to show a particular state).
- After pushing, check the CI run on GitHub Actions (Linux, Windows, macOS). Windows-only failures are real
  (CRLF, MSVC warnings).

## Rules
- `firmware/` must not include Arduino, OS, GUI or simulator headers. Hardware goes through `firmware/hal/hal.h`. Asset headers may include `<Arduino.h>` (host builds get `firmware/platform/host/Arduino.h`).
- Layout sizes live in `firmware/ui/gfx.h` and must match the mockup guides in Flipper UI Studio; change both together.
- Fonts, icons and the splash are drawn through `theme::` (`firmware/app/theme.h`), never `assets::` directly, so SD themes can replace them. New icon slots need a key, a size and an entry in `kIconSlots`.
- Asset headers in `firmware/assets/generated/` come from Flipper UI Studio exports. Don't hand-edit them; re-export.
- The bit order (`idx=y*w+x`, LSB-first, 1 = ink) and the `.b1i` / `.b1f` / `theme.ini` formats are shared with the studio. Don't change them on one side only.
- When a UI change is intended, update the `framebuffer_hash_equals` values in `sim/scripts/` from the runner's `fb_hash` output, and say so in the commit message. An unexpected hash change is a bug.
- New mock behaviour goes through setters on the mock (the same path a live UI control will use), never by poking firmware state from a script.
- New script events or checks: document them in the comment at the top of `simulator/core/script.h`.
- Files under `sim/` are device data: keep them byte-exact (`.gitattributes` marks them `-text`).
- Screens show problems to the user (no SD card, write failed, bad file); don't fail silently.

## Workflow
- Commit to `main` with a message that says what changed and why; CI must stay green.
- End every piece of work by updating `docs/STATUS.md` (and DECISIONS.md if a decision changed).
- User-facing replies to the owner are in Thai; code, comments and docs are in English.
