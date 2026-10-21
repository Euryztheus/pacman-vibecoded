// Pacman — the classic arcade game, implemented from scratch in C++ / SDL2.
// Audio is synthesized at runtime.
//
//   Controls:  Arrow keys / WASD move   P pause   M mute   R restart   Esc/Q quit
//   Build:     make
//   Run:       ./pacman

#pragma once

#include <SDL2/SDL.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const int TILE    = 24;                 // pixel size of one cell
static const int COLS    = 28;                 // maze width in cells
static const int ROWS    = 31;                 // maze height in cells
static const int TUNNEL  = 14;                 // tunnel row
static const int HUD_H   = 64;                 // HUD band below the maze
static const int WIN_W   = COLS * TILE;
static const int WIN_H   = ROWS * TILE + HUD_H;
static const int FPS     = 60;
static const float DT    = 1.0f / FPS;

// Base speeds (cells per second)
static const float PAC_SPEED   = 8.0f;
static const float GHOST_SPEED = 7.4f;
static const float FRIGHT_SPEED= 5.0f;
static const float EYES_SPEED  = 14.0f;
static const float TUNNEL_SPEED= 4.0f;
static const float HOUSE_SPEED = 4.0f;

// Colours as 0xRRGGBB (defined in main.cpp, decomposed with bit shifts when drawing)
extern Uint32 C_WALL, C_WALL_D, C_PELLET, C_POWER, C_PAC, C_BG, C_TEXT, C_DIM;
extern Uint32 C_GHOST[4];                // blinky, pinky, inky, clyde
extern Uint32 C_FRIGHT, C_FRIGHT_F, C_DOOR;
