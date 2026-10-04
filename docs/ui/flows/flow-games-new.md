# Flow: Games-new

## States

### Games Menu  `[screen, list input, start]`

Screen: **Games G1 Games Menu-new** (Mockup 128×160, id `pic114`)

![Games G1 Games Menu-new](screens/games-g1-games-menu-new_pic114.png)

> Built-in first, then SD packs. No SD -> toast "SD not available"

### Launcher (back)  `[screen, list input]`

Screen: **Main M6a Launcher-new** (Mockup 128×160, id `pic75`)

![Main M6a Launcher-new](screens/main-m6a-launcher-new_pic75.png)

> Leave this flow: back to the Launcher

### Loading pack  `[screen, normal input]`

Screen: **Games G2 Loading Pack-new** (Mockup 128×160, id `pic115`)

![Games G2 Loading Pack-new](screens/games-g2-loading-pack-new_pic115.png)

### Load failed  `[screen, normal input]`

Screen: **Games G2f Load Failed-new** (Mockup 128×160, id `pic116`)

![Games G2f Load Failed-new](screens/games-g2f-load-failed-new_pic116.png)

### Game (canvas)  `[screen, normal input]`

Screen: **Games G3 Game Canvas-new** (Mockup 128×160, id `pic117`)

![Games G3 Game Canvas-new](screens/games-g3-game-canvas-new_pic117.png)

> Full screen, no status bar. Idle dim/sleep off. Level 0: host owns Cancel hold. Levels 1/2 (bypass list) draw their own pause.

### Pause menu (host)  `[screen, list input]`

Screen: **Games G4 Pause Menu-new** (Mockup 128×160, id `pic118`)

![Games G4 Pause Menu-new](screens/games-g4-pause-menu-new_pic118.png)

### Failsafe exit  `[screen, normal input]`

Screen: **Games G5 Failsafe Exit-new** (Mockup 128×160, id `pic119`)

![Games G5 Failsafe Exit-new](screens/games-g5-failsafe-exit-new_pic119.png)

> Works at every level; progress bar from 1.5 s

## Transitions

- Games Menu —OK · tap→ Game (canvas) · "built-in game"
- Games Menu —OK · tap→ Loading pack · "SD pack"
- Games Menu —Cancel · tap→ Launcher (back)
- Loading pack —⚡ Game pack loaded→ Game (canvas)
- Loading pack —⚡ Game pack failed→ Load failed
- Load failed —OK · tap→ Games Menu
- Game (canvas) —hold Cancel→ Pause menu (host) (fade, 150 ms) · "level 0 only"
- Game (canvas) —⚡ App failsafe exit (Cancel held 3 s)→ Failsafe exit
- Pause menu (host) —OK · tap→ Game (canvas) · "Resume"
- Pause menu (host) —OK · tap→ Game (canvas) · "Restart"
- Pause menu (host) —OK · tap→ Games Menu · "Exit (game saves in onExit)"
- Pause menu (host) —Cancel · tap→ Game (canvas) · "Resume"
- Failsafe exit —⏱ 0.5 s→ Games Menu

## System events used

- `pack_loaded`: Game pack loaded
- `pack_failed`: Game pack failed
- `app_failsafe`: App failsafe exit (Cancel held 3 s)
