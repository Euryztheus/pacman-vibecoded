#include "ghost.h"
#include "maze.h"
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Ghost AI
// ---------------------------------------------------------------------------
static float targetCell(const Ghost &g, float &tx, float &ty) {
    int px = pacCellX(), py = pacCellY();
    switch (g.id) {
        case 0: // Blinky: straight at Pac-Man
            tx = px; ty = py;
            break;
        case 1: { // Pinky: 4 tiles ahead of Pac-Man
            tx = px + DX[G.pac.dir == NONE ? LEFT : G.pac.dir] * 4;
            ty = py + DY[G.pac.dir == NONE ? LEFT : G.pac.dir] * 4;
            break;
        }
        case 2: { // Inky: double the vector from Blinky to 2 tiles ahead
            Ghost &b = G.ghosts[0];
            float ax = px + DX[G.pac.dir == NONE ? LEFT : G.pac.dir] * 2;
            float ay = py + DY[G.pac.dir == NONE ? LEFT : G.pac.dir] * 2;
            tx = ax + (ax - b.x);
            ty = ay + (ay - b.y);
            break;
        }
        case 3: { // Clyde: chase when far, retreat when close
            float dx = g.x - px, dy = g.y - py;
            if (dx * dx + dy * dy > 64) { tx = px; ty = py; }
            else { tx = SCATTER[3][0]; ty = SCATTER[3][1]; }
            break;
        }
    }
    return 0;
}

static bool normCanGo(int c, int r, int d)  { return canStep(c, r, d, false); }


static void ghostDecide(Ghost &g, int c, int r) {
    if (g.st == GState::Eyes) {
        if (r == 11 && (c == 13 || c == 14)) {
            // Reached the top of the door — descend into the house
            g.st = GState::Entering; g.dir = DOWN; g.x = HOUSE_X;
            return;
        }
        // Head for the door; prefer shortest path
        float best = 1e9; int bd = g.dir;
        for (int d : {UP, LEFT, DOWN, RIGHT}) {
            if (d == NONE) continue;
            int opp = (d == UP) ? DOWN : (d == DOWN) ? UP : (d == LEFT) ? RIGHT : LEFT;
            if (d == opp && g.dir != NONE) continue;
            if (!canStep(c, r, d, true)) continue;
            float dx = (c + DX[d]) - 13.5f, dy = (r + DY[d]) - 11.0f;
            float v = dx * dx + dy * dy;
            if (v < best) { best = v; bd = d; }
        }
        g.dir = bd;
        return;
    }

    bool scatter = G.modeScatter;
    float tx = pacCellX(), ty = pacCellY();
    if (scatter) { tx = SCATTER[g.id][0]; ty = SCATTER[g.id][1]; }
    else targetCell(g, tx, ty);

    if (g.frightT > 0) {
        // Scared: random walk (no U-turns)
        int opts[4], n = 0;
        for (int d : {UP, LEFT, DOWN, RIGHT}) {
            int opp = (d == UP) ? DOWN : (d == DOWN) ? UP : (d == LEFT) ? RIGHT : LEFT;
            if (g.dir != NONE && d == opp) continue;
            if (!normCanGo(c, r, d)) continue;
            opts[n++] = d;
        }
        if (n) g.dir = opts[std::rand() % n];
        return;
    }

    float best = 1e9; int bd = g.dir;
    for (int d : {UP, LEFT, DOWN, RIGHT}) {
        int opp = (d == UP) ? DOWN : (d == DOWN) ? UP : (d == LEFT) ? RIGHT : LEFT;
        if (g.dir != NONE && d == opp) continue;
        if (!normCanGo(c, r, d)) continue;
        float dx = (c + DX[d]) - tx, dy = (r + DY[d]) - ty;
        float v = dx * dx + dy * dy;
        if (v < best) { best = v; bd = d; }   // ties keep first (UP,LEFT,DOWN,RIGHT)
    }
    g.dir = bd;
}

// Ghosts speed up slightly as levels progress
static float levelFactor() { return std::min(1.3f, 1.0f + 0.04f * (G.level - 1)); }

// ---------------------------------------------------------------------------
// Ghost update (house scripting + grid movement)
// ---------------------------------------------------------------------------
void updateGhost(Ghost &g, float dt) {
    // Personal fright expiry
    if (g.frightT > 0) {
        g.frightT -= dt;
        if (g.frightT <= 0 && g.st == GState::Out) g.frightT = 0;
    }

    switch (g.st) {
        case GState::InHouse: {
            g.bobT += dt;
            g.y = g.homeY + 0.25f * std::sin(g.bobT * 4.0f);
            if (G.time >= g.releaseAt) { g.st = GState::Leaving; g.dir = NONE; }
            break;
        }
        case GState::Leaving: {
            float v = HOUSE_SPEED * dt;
            if (std::fabs(g.x - HOUSE_X) > 1e-3f) {
                // slide to the door column first
                float dx = HOUSE_X - g.x;
                g.x += std::copysign(std::min(v, std::fabs(dx)), dx);
            } else {
                g.x = HOUSE_X;
                g.y -= v;
                if (g.y <= 11.0f) {
                    g.y = 11.0f;
                    g.dir = (g.id == 0) ? LEFT : ((g.id & 1) ? LEFT : RIGHT);
                    g.st = GState::Out;
                }
            }
            break;
        }
        case GState::Entering: {
            float v = HOUSE_SPEED * dt;
            g.y += v;
            if (g.y >= HOUSE_Y) {
                g.y = HOUSE_Y;
                g.st = GState::Revive;
                g.pause = 0.4f;
            }
            break;
        }
        case GState::Revive: {
            g.pause -= dt;
            g.y = g.homeY + 0.25f * std::sin(g.bobT * 4.0f);
            g.bobT += dt;
            if (g.pause <= 0) {
                g.st = GState::Leaving;
                g.dir = NONE;
                g.x = g.homeX;
            }
            break;
        }
        case GState::Eyes: {
            auto atC = [&](Actor &a) {
                ghostDecide(g, (int)std::lround(a.x), (int)std::lround(a.y));
            };
            moveActor(g, EYES_SPEED, dt, atC);
            break;
        }
        case GState::Out:
        default: {
            float sp;
            if (g.frightT > 0) sp = FRIGHT_SPEED;
            else if (std::fabs(g.y - TUNNEL) < 0.25f &&
                     (g.x < 1.5f || g.x > COLS - 1.5f)) sp = TUNNEL_SPEED;
            else sp = GHOST_SPEED;
            sp *= levelFactor();
            auto atC = [&](Actor &a) {
                ghostDecide(g, (int)std::lround(a.x), (int)std::lround(a.y));
            };
            moveActor(g, sp, dt, atC);
            break;
        }
    }
}
