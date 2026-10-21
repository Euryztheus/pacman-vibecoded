#include "game.h"
#include "maze.h"
#include "ghost.h"
#include "sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

Game G;
std::vector<PopUp> pops;

// ---------------------------------------------------------------------------
// Game setup
// ---------------------------------------------------------------------------
static void buildPhases() {
    G.phaseDur.clear(); G.phaseScatter.clear();
    G.phaseDur.push_back(7);  G.phaseScatter.push_back(true);
    G.phaseDur.push_back(20); G.phaseScatter.push_back(false);
    G.phaseDur.push_back(7);  G.phaseScatter.push_back(true);
    G.phaseDur.push_back(20); G.phaseScatter.push_back(false);
    G.phaseDur.push_back(5);  G.phaseScatter.push_back(true);
    G.phaseDur.push_back(20); G.phaseScatter.push_back(false);
    G.phaseDur.push_back(5);  G.phaseScatter.push_back(true);
    G.phaseDur.push_back(1e9f); G.phaseScatter.push_back(false);
}

static void resetPositions() {
    G.pac.x = 13.0f; G.pac.y = 23.0f; G.pac.dir = NONE; G.pac.want = NONE;

    Ghost &b = G.ghosts[0];
    b.x = 13.5f; b.y = 11.0f; b.dir = LEFT;  b.st = GState::Out;
    b.homeX = 13.5f; b.homeY = HOUSE_Y; b.releaseAt = 0;

    const float hx[4] = { 13.5f, 13.5f, 11.5f, 15.5f };
    const float rel[4] = { 0, 1.5f, 4.5f, 7.5f };
    for (int i = 1; i < 4; i++) {
        Ghost &g = G.ghosts[i];
        g.x = hx[i]; g.y = HOUSE_Y; g.dir = NONE; g.st = GState::InHouse;
        g.homeX = hx[i]; g.homeY = HOUSE_Y;
        g.bobT = (float)(i * 1.7);
        g.releaseAt = rel[i];
    }
    for (auto &g : G.ghosts) { g.pause = 0; g.frightT = 0; }
    G.frightOn = false; G.frightT = 0;
    G.time = 0;
    G.phaseIdx = 0; G.phaseT = 0; G.modeScatter = true;
}

static void startLevel(int rebuildPellets) {
    if (rebuildPellets) {
        int count = 0;
        G.pellet.assign(ROWS * COLS, false);
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                char ch = MAZE[r][c];
                if (ch == '.' || ch == 'o') G.pellet[r * COLS + c] = true;
            }
        for (auto p : G.pellet) if (p) count++;
        G.pelletsLeft = count;
    }
    G.dotsEatenThisLife = 0;
    resetPositions();
    G.frightBase = std::max(2.0f, 6.0f - (G.level - 1) * 0.75f);
    G.ph = Phase::Ready; G.phT = 1.6f;
}

static int oppDir(int d) {
    if (d == UP) return DOWN;
    if (d == DOWN) return UP;
    if (d == LEFT) return RIGHT;
    if (d == RIGHT) return LEFT;
    return NONE;
}

void newGame() {
    G.score = 0; G.lives = 3; G.level = 1;
    G.soundOn = true;
    buildPhases();
    startLevel(true);
    SFX.play(SFX_START);
}

// ---------------------------------------------------------------------------
// Game flow — Pac-Man movement, pellets, collisions, phase machine
// ---------------------------------------------------------------------------
static void pacDecide(Actor &a) {
    int c = (int)std::lround(a.x), r = (int)std::lround(a.y);
    if (G.pac.want != NONE && G.pac.want != a.dir) {
        int opp = (a.dir == UP) ? DOWN : (a.dir == DOWN) ? UP :
                  (a.dir == LEFT) ? RIGHT : (a.dir == RIGHT) ? LEFT : NONE;
        if (G.pac.want == opp) { a.dir = G.pac.want; return; } // instant reverse
        if (canStep(c, r, G.pac.want, false)) { a.dir = G.pac.want; return; }
    }
    if (a.dir != NONE && !canStep(c, r, a.dir, false)) a.dir = NONE;
}

static void pacUpdate(float dt) {
    auto atC = [&](Actor &a) {
        pacDecide(a);
        int c = (int)std::lround(a.x), r = (int)std::lround(a.y);
        if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return;
        if (!G.pellet[r * COLS + c]) return;
        G.pellet[r * COLS + c] = false;
        G.pelletsLeft--;
        G.dotsEatenThisLife++;
        if (MAZE[r][c] == 'o') {
            G.score += 50;
            G.frightOn = true;
            G.frightT = G.frightBase;
            G.frightCombo = 0;
            for (auto &g : G.ghosts)
                if (g.st == GState::Out) { g.frightT = G.frightBase; g.dir = oppDir(g.dir); }
            SFX.play(SFX_POWER);
        } else {
            G.score += 10;
            SFX.play((G.dotsEatenThisLife & 1) ? SFX_WAKA_A : SFX_WAKA_B);
        }
        if (G.score > G.hi) G.hi = G.score;
    };
    moveActor(G.pac, PAC_SPEED, dt, atC);
}

static void checkCollisions() {
    for (auto &g : G.ghosts) {
        if (g.st != GState::Out) continue;
        float dx = g.x - G.pac.x, dy = g.y - G.pac.y;
        if (dx * dx + dy * dy < 0.45f * 0.45f) {
            if (g.frightT > 0) {
                int pts = 200 * (1 << std::min(3, G.frightCombo));
                G.score += pts;
                G.frightCombo = std::min(4, G.frightCombo + 1);
                pops.push_back({ g.x, g.y, pts, 1.0f });
                g.frightT = 0;
                g.st = GState::Eyes;
                if (G.score > G.hi) G.hi = G.score;
                SFX.play(SFX_EAT_GHOST);
            } else {
                G.lives--;
                G.ph = Phase::Dying;
                G.phT = 2.4f;
                SFX.play(SFX_DEATH);
                return;
            }
        }
    }
}

void update(float dt) {
    if (G.paused) return;
    for (auto it = pops.begin(); it != pops.end(); ) {
        it->t -= dt * 0.8f;
        if (it->t <= 0) it = pops.erase(it);
        else it++;
    }
    switch (G.ph) {
        case Phase::Ready:
            G.phT -= dt;
            if (G.phT <= 0) G.ph = Phase::Play;
            break;
        case Phase::Play: {
            G.time += dt;
            // scatter / chase schedule
            G.phaseT += dt;
            if (G.phaseT >= G.phaseDur[G.phaseIdx]) {
                G.phaseT = 0;
                G.phaseIdx = (G.phaseIdx + 1) % (int)G.phaseDur.size();
                if (G.phaseScatter[G.phaseIdx] != G.modeScatter) {
                    G.modeScatter = G.phaseScatter[G.phaseIdx];
                    for (auto &g : G.ghosts)
                        if (g.st == GState::Out) g.dir = oppDir(g.dir);
                }
            }
            pacUpdate(dt);
            for (auto &g : G.ghosts) updateGhost(g, dt);
            if (G.frightOn) {
                G.frightT -= dt;
                if (G.frightT <= 0) {
                    G.frightOn = false;
                    G.frightCombo = 0;
                    for (auto &g : G.ghosts)
                        if (g.st == GState::Out) g.frightT = 0;
                }
            }
            if (G.ph == Phase::Play) checkCollisions();
            if (G.ph == Phase::Play && G.pelletsLeft <= 0) {
                G.ph = Phase::LevelClear;
                G.phT = 2.2f;
                SFX.play(SFX_LEVEL_CLEAR);
            }
            break;
        }
        case Phase::Dying:
            G.phT -= dt;
            if (G.phT <= 0) {
                if (G.lives <= 0) G.ph = Phase::GameOver;
                else startLevel(false);
            }
            break;
        case Phase::LevelClear:
            G.phT -= dt;
            if (G.phT <= 0) { G.level++; startLevel(true); }
            break;
        case Phase::GameOver:
            break;
    }
}
