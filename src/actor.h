#pragma once

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------
enum Dir { UP = 0, LEFT = 1, DOWN = 2, RIGHT = 3, NONE = -1 };
static const int DX[4] = {  0, -1,  0, +1 };
static const int DY[4] = { -1,  0, +1,  0 };

enum class GState { InHouse, Leaving, Out, Eyes, Entering, Revive };

struct Actor {
    float x = 0, y = 0;
    int   dir = NONE;
};

struct Ghost : Actor {
    int      id = 0;                 // 0 blinky, 1 pinky, 2 inky, 3 clyde
    GState   st = GState::InHouse;
    float    homeX = 0, homeY = 0;   // resting cell inside the house
    float    bobT  = 0;              // bobbing phase
    float    pause = 0;              // generic pause timer
    float    releaseAt = 0;          // level-time when this ghost may leave
    float    frightT = 0;            // personal fright timer (0 = not scared)
};

struct Pac : Actor {
    int want = NONE;                 // buffered input direction
};

// Scatter corners (one per ghost)
static const float SCATTER[4][2] = { {25, 0}, {2, 0}, {25, 30}, {2, 30} };

// House geometry (cell coords)
static const float HOUSE_X = 13.5f;  // centre column of the door
static const float HOUSE_Y = 14.0f;  // resting row inside the house
