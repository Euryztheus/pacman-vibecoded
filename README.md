# Pacman (C++ / SDL2)

> This was an experiment by Qwen 3.8 27B (Q4) for completely vibecoding a game —
> no planning doc, no assets, no framework: the whole thing (maze, ghost AI,
> rendering, even the audio) was written from scratch by an AI and then
> refactored into modules by one.

The classic arcade game, implemented from scratch in C++ with SDL2.
All graphics are drawn procedurally and all sound is synthesized at runtime —
there are **zero external asset files**.

## Features

- Full classic maze with tunnel wrap-around
- 4 ghosts with their authentic AI personalities (Blinky chases,
  Pinky ambushes, Inky flanks, Clyde wanders), plus scatter/chase phases
- Power pellets, frightened ghosts, eyes returning home, 200/400/800/1600 chain
- Ready / Dying / Level Clear / Game Over flow, lives, levels (ghosts get
  faster and fright shrinks each level)
- Procedural sound effects (waka, power-up, eat-ghost, death, jingles)
- 5x7 bitmap font drawn in code

## Build

Requires a C++ compiler and SDL2 development libraries:

```sh
# Debian/Ubuntu
sudo apt install make g++ libsdl2-dev
# Fedora
sudo dnf install make g++ SDL2-devel
# macOS (Homebrew)
brew install sdl2
```

Then:

```sh
make
```

This builds the `pacman` binary in the repo root.

## Run

```sh
./pacman
```

## Controls

| Key | Action |
|---|---|
| Arrow keys / WASD | Move |
| P | Pause |
| M | Mute |
| R | Restart |
| Esc / Q | Quit |

## Project layout

```
Makefile            build rules (make / make clean)
src/main.cpp        entry point, SDL init, game loop
src/game.*          game state, phase machine, pellets, collisions
src/maze.*          maze grid + grid-aligned movement core
src/ghost.*         ghost AI and house scripting
src/actor.h         entities (Pac, Ghost), directions, constants
src/draw.*          all rendering
src/font.*          bitmap font
src/input.*         keyboard handling
src/sound.*        procedural sound synth
src/config.h        all tunable constants
```

See [AGENTS.md](AGENTS.md) for a deeper architecture overview.
