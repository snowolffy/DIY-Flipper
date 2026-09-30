# CLAUDE.md

## Build and verify
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel && ctest --test-dir build --output-on-failure`
- Every change to firmware/ or simulator/ must keep the build free of warnings (`-Wall -Wextra`, MSVC `/W4`) and ctest green.
- To see a screen: `build/sim_headless --dump-final out.pbm <script>` writes a PBM of the last frame.
- GUI: configure with `-DDIYF_BUILD_GUI=ON`. It runs in this container on SDL's offscreen driver:
  `SDL_VIDEODRIVER=offscreen build-gui/sim_gui --screenshot out.bmp --frames 150` saves the whole window.

## Rules
- `firmware/` must not include Arduino, OS, GUI or simulator headers. Hardware goes through `firmware/hal/hal.h`. Asset headers may include `<Arduino.h>` (host builds get `firmware/platform/host/Arduino.h`).
- Layout sizes live in `firmware/ui/gfx.h` and must match the mockup guides in Flipper UI Studio; change both together.
- Asset headers in `firmware/assets/generated/` come from Flipper UI Studio exports. Don't hand-edit them; re-export.
- When a UI change is intended, update the `framebuffer_hash_equals` values in `sim/scripts/` from the runner's `fb_hash` output, and say so in the commit message.
- New mock behaviour goes through setters on the mock (the same path a live UI control will use), never by poking firmware state from a script.
