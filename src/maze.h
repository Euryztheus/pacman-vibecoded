#pragma once

#include <functional>

#include "actor.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Maze  (28 x 31).  '#' wall  '.' pellet  'o' power pellet  '-' house door
//           ' ' open, no pellet.  Row TUNNEL wraps at the edges.
// Defined in maze.cpp.
// ---------------------------------------------------------------------------
extern const char *MAZE[ROWS];

// ---------------------------------------------------------------------------
// Maze helpers + movement core (maze.cpp)
// ---------------------------------------------------------------------------
bool canStep(int c, int r, int d, bool throughDoor);
int  pacCellX();
int  pacCellY();

using CanGo = std::function<bool(int, int, int)>;  // (c, r, dir) -> walkable?
using AtC   = std::function<void(Actor &)>;        // decision at a centre

// Sub-stepped grid-aligned movement: every grid centre is hit exactly.
void moveActor(Actor &a, float speed, float dt, const AtC &atCentre);
