# AGENTS.md — Pacman (C++/SDL2)

A complete, self-contained Pacman clone in C++ with SDL2. No external assets:
all graphics are drawn procedurally (rects, circles, one triangle fan) and all
audio is synthesized at runtime as sine waves.

Read this file first. You should not need to open most source files to answer
questions or make small changes.

## Layout

```
Makefile            build (see below)
pacman              built binary (gitignored, root)
src/
  config.h          ALL tuning constants + palette externs
  main.cpp          entry point, SDL init, game loop, palette definitions
  game.h/.cpp       global game state (Game G), phase machine, pellets, collisions
  actor.h           Dir enum, DX/DY, Actor/Ghost/Pac structs, scatter corners, house consts
  maze.h/.cpp       MAZE grid, canStep(), moveActor() — the movement core
  ghost.h/.cpp      ghost AI (targeting, scatter/chase, house scripting)
  draw.h/.cpp       rendering (walls, pellets, pac, ghosts, HUD, overlays); REN, clockT
  font.h/.cpp       5x7 bitmap font (defined in code), drawText/textWidth/drawTextC
  input.h/.cpp      handleKey() — arrows/WASD buffer direction, P/M/R
  sound.h/.cpp      SoundEngine (SFX) — procedural synth + SDL audio callback
```

Each `.cpp` pairs 1:1 with its `.h`. Object files live in `src/` (gitignored).

## How to build & run

```sh
make        # needs g++ and SDL2 dev libs (sdl2-config must exist)
./pacman
```

Dependencies: C++ compiler + SDL2 (`sudo apt install libsdl2-dev make g++`
on Debian/Ubuntu). `make clean` removes the binary and objects.

## Architecture (important invariants)

**Global state, not OOP.** Everything mutates globals:
- `Game G` (game.h) — score/lives/level, pellets, pac, 4 ghosts, fright,
  scatter/chase schedule, phase, pause.
- `SDL_Renderer *REN` and `float clockT` (draw.h) — renderer and animation
  clock; `clockT` is accumulated in the main loop and used for all animation.
- `SoundEngine SFX` (sound.h).
- Palette: `Uint32 C_*` globals declared in config.h, *defined in main.cpp*,
  as 0xRRGGBB and decomposed with bit shifts at draw time.

**Main loop (main.cpp):** event poll → fixed-timestep update at 60 Hz via an
accumulator (`DT` from config.h) → `draw()`. Update is decoupled from frame
rate; keep it that way if you touch the loop.

**Grid movement core (maze.cpp) — the single most important mechanism.**
Actors live in cell coordinates where each grid centre is an integer point.
`moveActor(actor, speed, dt, atCentre)` substeps so that **every grid centre
is hit exactly**, calling `atCentre` there so the entity can decide
(turn / eat pellet / change target). Never replace this with free-form
floating-point movement — pellet eating, turning, and ghost AI all depend on
the actor standing exactly on a centre. Tunnel wrap is also done inside
`moveActor` (row `TUNNEL`, ±(COLS+2)).

**Maze (maze.cpp):** `MAZE` is 31 rows × 28 chars, one char per cell:
`#` wall, `.` pellet, `o` power pellet, `-` house door, ` ` open/no pellet.
Row `TUNNEL` (14) wraps at the edges (`canStep` returns true there).
If you edit the maze: keep every row exactly `COLS` (28) chars. Door chars
are at row 12, columns 13–14.

**Movement legality:** `canStep(c, r, dir, throughDoor)` — wall check plus
the door only passable with `throughDoor=true` (used by returning "Eyes"
ghosts). `moveActor` never steps into a wall; `dir` is set to `NONE` when
blocked. Pac input is buffered in `G.pac.want` and applied at the next centre
(`pacDecide`); instant 180° reversal is always allowed.

**Game phase machine (game.cpp, `Phase`):**
`Ready → Play → (Dying | LevelClear) → ... → GameOver`.
- `update()` is the single entry point; main.cpp never mutates game state.
- Death keeps the pellet layout (`startLevel(false)`); level-up rebuilds it
  (`startLevel(true)`).
- Scatter/chase schedule: `buildPhases()` fills `phaseDur`/`phaseScatter`;
  on mode change all out-of-house ghosts reverse.
- Fright: global `G.frightOn`/`frightT` + per-ghost `frightT`; combo scoring
  200·2^chain (capped). Fright duration shrinks with level (`frightBase`).
- Floating scores after eating ghosts live in `pops` (game.h), decay in
  `update()`, drawn in draw.cpp.

**Ghost AI (ghost.cpp):**
- Targets: Blinky = pac; Pinky = 4 tiles ahead; Inky = 2 tiles ahead doubled
  from Blinky; Clyde = chase if >8 tiles away, else his scatter corner.
  Implemented in `targetCell`.
- At each centre, a ghost picks the legal direction (no U-turns) minimizing
  squared distance to its target; ties keep the first option in
  UP, LEFT, DOWN, RIGHT order — keep this order if you change it, it affects
  behaviour deterministically.
- Scared ghosts walk randomly (no U-turns).
- States (`GState`): `InHouse → Leaving → Out`; eaten ghosts go
  `Eyes → Entering → Revive → Leaving`. Eye return target is the door top
  (13.5, 11.0). House consts: `HOUSE_X=13.5`, `HOUSE_Y=14` (actor.h).
- Speeds: base from config.h, scaled by `levelFactor()` (capped 1.3×);
  tunnel sections use `TUNNEL_SPEED`, frightened `FRIGHT_SPEED`, eyes
  `EYES_SPEED`.

**Rendering (draw.cpp):** `draw()` order: clear → walls → pellets → door →
ghosts → pac → floating scores → HUD → overlays (`READY!`, `GAME OVER`).
Walls are drawn as line segments per wall-cell adjacency (not filled blocks).
Pac-Man is a triangle fan via `SDL_RenderGeometry` with an animated mouth;
ghosts are circle + rect + skirt bumps. Level-clear phase hides pac and
flashes ghosts.

**Audio (sound.h):** `SoundEngine` renders each SFX to a sample buffer
(22050 Hz mono s16, `freqAt`/`durAt` define each effect) and appends it to a
queue drained by the SDL audio callback. Adding a sound = add an enum value,
a `freqAt` case, a `durAt` case. `SFX.play()` is called from the game thread.

**Input (input.cpp):** `handleKey` — arrows/WASD set `G.pac.want`;
`P` pause, `M` mute (toggles `SFX`), `R` restart via `newGame()`.
Quit is Esc/Q (handled in main.cpp).

## Conventions

- All tunable numbers live in `config.h` (tile size, speeds, FPS). Don't hard-code new constants in logic files.
- Coordinates are cell floats; pixel conversion is `(coord + 0.5) * TILE`
  (`cellPix` in draw.cpp).
- No new dependencies or asset files; stay procedural.
- Keep changes minimal; the fixed-timestep/grid-centre invariants above are
  load-bearing — if a change breaks "actor lands exactly on centres", the
  game degrades badly.

## Known quirks

- `srand(time)` in main.cpp — no persistent seed.
- Hi-score is session-only (not saved).
- Some palette globals (`C_WALL_D`, `C_FRIGHT_F`) are declared but unused
  (`(void)` casts in main.cpp); flashing white is a literal `0xFFFFFF`.
- `GState` ghosts in house are hidden during `Phase::Dying` (draw.cpp).
- History: started as one file, refactored into modules
  (commit "chore: refactor from mono-file by ai").
