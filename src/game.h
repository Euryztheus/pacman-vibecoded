#pragma once

#include <vector>

#include "actor.h"

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------
enum class Phase { Ready, Play, Dying, LevelClear, GameOver };

struct Game {
    std::vector<bool> pellet;        // ROWS*COLS
    int  pelletsLeft = 0;
    Pac  pac;
    Ghost ghosts[4];

    int  score = 0, hi = 0, lives = 3, level = 1;
    int  dotsEatenThisLife = 0;

    bool frightOn = false;
    float frightT = 0;               // seconds of fright left
    float frightBase = 6.0f;         // fright duration, shrinks with level
    int  frightCombo = 0;            // chain counter for eating ghosts

    // scatter / chase phase schedule
    std::vector<float> phaseDur;     // seconds
    std::vector<bool>  phaseScatter; // true = scatter
    int   phaseIdx = 0;
    float phaseT = 0;
    bool  modeScatter = true;        // current mode

    Phase  ph = Phase::Ready;
    float  phT = 0;
    bool   paused = false;
    float  time = 0;                 // time within current life (for releases)
    bool   soundOn = true;
};

extern Game G;

struct PopUp { float x, y; int pts; float t; };
extern std::vector<PopUp> pops;   // floating scores after eating a ghost

// Game flow (game.cpp)
void newGame();
void update(float dt);
