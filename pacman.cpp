// Pacman — the classic arcade game, implemented from scratch in C++ / SDL2.
// Single file, no dependencies beyond libSDL2. Audio is synthesized at runtime.
//
//   Controls:  Arrow keys / WASD move   P pause   M mute   R restart   Esc/Q quit
//   Build:     g++ -O2 -o pacman pacman.cpp $(sdl2-config --cflags --libs)
//   Run:       ./pacman

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <memory>
#include <random>
#include <ctime>

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

// Colours as 0xRRGGBB (decomposed with bit shifts when drawing)
static Uint32 C_WALL, C_WALL_D, C_PELLET, C_POWER, C_PAC, C_BG, C_TEXT, C_DIM;
static Uint32 C_GHOST[4] = {0};                // blinky, pinky, inky, clyde
static Uint32 C_FRIGHT, C_FRIGHT_F, C_DOOR;

// ---------------------------------------------------------------------------
// Maze  (28 x 31).  '#' wall  '.' pellet  'o' power pellet  '-' house door
//           ' ' open, no pellet.  Row TUNNEL wraps at the edges.
// ---------------------------------------------------------------------------
static const char *MAZE[ROWS] = {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "     #.##### ## #####.#     ",
    "     #.##          ##.#     ",
    "     #.## ###--### ##.#     ",
    "######.## #      # ##.######",
    "      .   #      #   .      ",
    "######.## #      # ##.######",
    "     #.## ######## ##.#     ",
    "     #.##          ##.#     ",
    "     #.## ######## ##.#     ",
    "######.## ######## ##.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#o..##.......  .......##..o#",
    "###.##.##.########.##.##.###",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################",
};

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

static Game G;

// ---------------------------------------------------------------------------
// Audio — tiny procedural synth through the classic SDL audio callback
// ---------------------------------------------------------------------------
enum Sfx { SFX_WAKA_A, SFX_WAKA_B, SFX_POWER, SFX_EAT_GHOST, SFX_DEATH,
           SFX_START, SFX_LEVEL_CLEAR, SFX_COUNT };

struct SoundEngine {
    std::vector<int16_t> queue;      // samples to play (concatenated)
    bool    enabled = true;
    bool    opened = false;
    int     rate = 22050;
    SDL_mutex *mtx = nullptr;

    static void audioCB(void *ud, Uint8 *buf, int len) {
        SoundEngine *s = (SoundEngine *)ud;
        int n = len / 2;
        SDL_LockMutex(s->mtx);
        if (s->enabled && !s->queue.empty()) {
            int take = (int)std::min<size_t>(n, s->queue.size());
            std::memcpy(buf, s->queue.data(), (size_t)take * 2);
            s->queue.erase(s->queue.begin(), s->queue.begin() + take);
            if (take < n) std::memset(buf + (size_t)take * 2, 0, (size_t)(n - take) * 2);
        } else {
            std::memset(buf, 0, (size_t)len);
        }
        SDL_UnlockMutex(s->mtx);
    }

    void init() {
        mtx = SDL_CreateMutex();
        SDL_AudioSpec want{};
        want.freq = rate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = audioCB;
        want.userdata = this;
        if (SDL_OpenAudio(&want, nullptr) < 0) {
            enabled = false;
            return;
        }
        opened = true;
    }
    void destroy() {
        if (opened) SDL_CloseAudio();
        if (mtx) { SDL_DestroyMutex(mtx); mtx = nullptr; }
    }
    void toggle() {
        SDL_LockMutex(mtx);
        enabled = !enabled;
        if (!enabled) queue.clear();
        SDL_UnlockMutex(mtx);
    }

    // Render a whole effect into the queue (called on the game thread)
    void play(Sfx s) {
        if (!enabled || !opened) return;
        std::vector<int16_t> tmp;
        render(s, tmp);
        SDL_LockMutex(mtx);
        if (queue.size() > (size_t)rate * 2) queue.clear();  // safety cap
        queue.insert(queue.end(), tmp.begin(), tmp.end());
        SDL_UnlockMutex(mtx);
    }

private:
    static double freqAt(Sfx s, double t) {
        switch (s) {
            case SFX_WAKA_A: return 520;
            case SFX_WAKA_B: return 380;
            case SFX_POWER:  return 200 + 700 * (t / 0.35);
            case SFX_EAT_GHOST:
                if (t < 0.10) return 660;
                if (t < 0.20) return 880;
                return 1320;
            case SFX_DEATH:  return 700 - 620 * std::min(1.0, t / 1.0);
            case SFX_START:  return t < 0.12 ? 440 : t < 0.24 ? 554 : t < 0.36 ? 659 : 880;
            case SFX_LEVEL_CLEAR:
                if (t < 0.08) return 523;
                if (t < 0.16) return 659;
                if (t < 0.24) return 784;
                if (t < 0.32) return 1046;
                if (t < 0.40) return 784;
                return 1046;
            case SFX_COUNT: break;
        }
        return 440;
    }
    static double durAt(Sfx s) {
        switch (s) {
            case SFX_WAKA_A: case SFX_WAKA_B: return 0.07;
            case SFX_POWER:  return 0.35;
            case SFX_EAT_GHOST: return 0.30;
            case SFX_DEATH:  return 1.1;
            case SFX_START:  return 0.48;
            case SFX_LEVEL_CLEAR: return 0.48;
            case SFX_COUNT: break;
        }
        return 0.1;
    }
    static void render(Sfx s, std::vector<int16_t> &out) {
        double dur = durAt(s);
        int n = (int)(dur * 22050);
        out.reserve(n);
        for (int i = 0; i < n; i++) {
            double t = (double)i / 22050.0;
            double f = freqAt(s, t);
            double v = std::sin(2.0 * 3.14159265358979 * f * t);
            double env = 1.0 - (t / dur) * 0.6;
            int16_t smp = (int16_t)(v * 9000 * env);
            out.push_back(smp);
        }
    }
};
static SoundEngine SFX;

// ---------------------------------------------------------------------------
// Maze helpers
// ---------------------------------------------------------------------------
// Can an entity standing at grid centre (c,r) step towards dir?
static bool canStep(int c, int r, int d, bool throughDoor) {
    if (r == TUNNEL) return true;               // tunnel row wraps
    int nc = c + DX[d], nr = r + DY[d];
    if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) return false;
    char ch = MAZE[nr][nc];
    if (ch == '#') return false;
    if (ch == '-' && !throughDoor) return false;
    return true;
}
static int  pacCellX() { return (int)std::lround(G.pac.x); }
static int  pacCellY() { return (int)std::lround(G.pac.y); }

// ---------------------------------------------------------------------------
// Movement core — sub-stepped so every grid centre is hit exactly
// ---------------------------------------------------------------------------
using CanGo = std::function<bool(int, int, int)>;  // (c, r, dir) -> walkable?
using AtC   = std::function<void(Actor &)>;        // decision at a centre

static void moveActor(Actor &a, float speed, float dt, const AtC &atCentre) {
    const float EPS = 1e-4f;
    float rem = speed * dt;
    int guard = 0;
    while (rem > EPS && guard++ < 96) {
        bool cx = std::fabs(a.x - std::round(a.x)) < EPS;
        bool cy = std::fabs(a.y - std::round(a.y)) < EPS;
        if (cx && cy) {
            a.x = std::round(a.x); a.y = std::round(a.y);
            atCentre(a);
        }
        if (a.dir == NONE) break;      // not moving (or stopped at a wall)
        int d = a.dir;
        float dist;
        if (d == UP)        dist = a.y - std::floor(a.y);
        else if (d == DOWN) dist = std::ceil(a.y) - a.y;
        else if (d == LEFT) dist = a.x - std::floor(a.x);
        else                dist = std::ceil(a.x) - a.x;
        if (dist < EPS) dist = 1.0f;
        float step = std::min(rem, dist);
        a.x += DX[d] * step;
        a.y += DY[d] * step;
        rem -= step;
        if (std::fabs(a.y - TUNNEL) < 0.25f) {   // tunnel row wraps
            if (a.x > COLS + 0.5f) a.x -= COLS + 2.0f;
            else if (a.x < -1.5f)  a.x += COLS + 2.0f;
        }
    }
}

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

static void newGame() {
    G.score = 0; G.lives = 3; G.level = 1;
    G.soundOn = true;
    buildPhases();
    startLevel(true);
    SFX.play(SFX_START);
}

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

// Ghosts speed up slightly as levels progress
static float levelFactor() { return std::min(1.3f, 1.0f + 0.04f * (G.level - 1)); }

static int oppDir(int d) {
    if (d == UP) return DOWN;
    if (d == DOWN) return UP;
    if (d == LEFT) return RIGHT;
    if (d == RIGHT) return LEFT;
    return NONE;
}

// ---------------------------------------------------------------------------
// Ghost update (house scripting + grid movement)
// ---------------------------------------------------------------------------
static void updateGhost(Ghost &g, float dt) {
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

// ---------------------------------------------------------------------------
// Game flow — Pac-Man movement, pellets, collisions, phase machine
// ---------------------------------------------------------------------------
struct PopUp { float x, y; int pts; float t; };
static std::vector<PopUp> pops;   // floating scores after eating a ghost

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

static void update(float dt) {
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

// ---------------------------------------------------------------------------
// 5x7 bitmap font
// ---------------------------------------------------------------------------
static uint8_t FONT[128][7];
static void buildFont() {
    auto set = [](char ch, uint8_t r0, uint8_t r1, uint8_t r2, uint8_t r3,
                  uint8_t r4, uint8_t r5, uint8_t r6) {
        int i = (unsigned char)ch;
        FONT[i][0] = r0; FONT[i][1] = r1; FONT[i][2] = r2; FONT[i][3] = r3;
        FONT[i][4] = r4; FONT[i][5] = r5; FONT[i][6] = r6;
    };
    set('A', 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11);
    set('B', 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E);
    set('C', 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E);
    set('D', 0x1C,0x12,0x11,0x11,0x11,0x12,0x1C);
    set('E', 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F);
    set('F', 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10);
    set('G', 0x0E,0x11,0x10,0x17,0x11,0x11,0x0F);
    set('H', 0x11,0x11,0x11,0x1F,0x11,0x11,0x11);
    set('I', 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E);
    set('J', 0x07,0x02,0x02,0x02,0x02,0x12,0x0C);
    set('K', 0x12,0x12,0x14,0x18,0x14,0x12,0x12);
    set('L', 0x10,0x10,0x10,0x10,0x10,0x10,0x1F);
    set('M', 0x11,0x1B,0x15,0x15,0x11,0x11,0x11);
    set('N', 0x11,0x19,0x15,0x13,0x11,0x11,0x11);
    set('O', 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E);
    set('P', 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10);
    set('Q', 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D);
    set('R', 0x1C,0x12,0x12,0x1C,0x14,0x12,0x11);
    set('S', 0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E);
    set('T', 0x1F,0x04,0x04,0x04,0x04,0x04,0x04);
    set('U', 0x11,0x11,0x11,0x11,0x11,0x12,0x0C);
    set('V', 0x11,0x11,0x11,0x11,0x11,0x0A,0x04);
    set('W', 0x11,0x11,0x11,0x15,0x15,0x15,0x0A);
    set('X', 0x11,0x0A,0x04,0x02,0x04,0x0A,0x11);
    set('Y', 0x11,0x11,0x0A,0x04,0x04,0x04,0x04);
    set('Z', 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F);
    set('0', 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E);
    set('1', 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E);
    set('2', 0x0E,0x11,0x01,0x06,0x08,0x10,0x1F);
    set('3', 0x1F,0x02,0x04,0x02,0x01,0x11,0x0E);
    set('4', 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02);
    set('5', 0x1F,0x10,0x1F,0x01,0x01,0x11,0x0E);
    set('6', 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E);
    set('7', 0x1F,0x01,0x02,0x04,0x08,0x08,0x08);
    set('8', 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E);
    set('9', 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C);
    set('!', 0x04,0x04,0x04,0x04,0x04,0x00,0x04);
    set('.', 0x00,0x00,0x00,0x00,0x00,0x00,0x04);
    set(':', 0x00,0x04,0x04,0x00,0x04,0x04,0x00);
    set('-', 0x00,0x00,0x00,0x0E,0x00,0x00,0x00);
    set('x', 0x00,0x00,0x0A,0x04,0x02,0x04,0x0A);
}

static void drawText(SDL_Renderer *ren, const char *s, int x, int y, int scale, Uint32 col) {
    SDL_SetRenderDrawColor(ren, (col >> 16) & 255, (col >> 8) & 255, col & 255, 255);
    for (const char *p = s; *p; p++) {
        if (*p == ' ') { x += 6 * scale; continue; }
        int i = (unsigned char)*p;
        if (i < 128)
            for (int row = 0; row < 7; row++) {
                uint8_t bits = FONT[i][row];
                for (int bit = 4; bit >= 0; bit--)
                    if (bits & (1 << bit)) {
                        SDL_Rect r{ x + (4 - bit) * scale, y + row * scale, scale, scale };
                        SDL_RenderFillRect(ren, &r);
                    }
            }
        x += 6 * scale;
    }
}
static int textWidth(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }
static void drawTextC(SDL_Renderer *ren, const char *s, int cx, int y, int scale, Uint32 col) {
    drawText(ren, s, cx - textWidth(s, scale) / 2, y, scale, col);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static SDL_Renderer *REN = nullptr;
static float clockT = 0;   // global animation clock

static void cellPix(float cx, float cy, float &px, float &py) {
    px = (cx + 0.5f) * TILE;
    py = (cy + 0.5f) * TILE;
}

static void drawCircle(SDL_Renderer *ren, int cx, int cy, int r, Uint32 col) {
    SDL_SetRenderDrawColor(ren, (col >> 16) & 255, (col >> 8) & 255, col & 255, 255);
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) {
                SDL_Rect q{ cx + x, cy + y, 1, 1 };
                SDL_RenderFillRect(ren, &q);
            }
}

static void drawWalls(SDL_Renderer *ren) {
    const int bar = 3;  // half-thickness of a wall line
    auto wcol = [&](SDL_Rect &r) {
        SDL_SetRenderDrawColor(REN, (C_WALL >> 16) & 255, (C_WALL >> 8) & 255, C_WALL & 255, 255);
        SDL_RenderFillRect(REN, &r);
    };
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (MAZE[r][c] != '#') continue;
            int x = c * TILE, y = r * TILE;
            bool u = r > 0 && MAZE[r - 1][c] == '#';
            bool d = r + 1 < ROWS && MAZE[r + 1][c] == '#';
            bool l = c > 0 && MAZE[r][c - 1] == '#';
            bool ri = c + 1 < COLS && MAZE[r][c + 1] == '#';
            if (!u && !d && !l && !ri) {           // isolated block
                SDL_Rect q{ x + TILE / 2 - bar, y + TILE / 2 - bar, bar * 2, bar * 2 };
                wcol(q);
                continue;
            }
            if (u) { SDL_Rect q{ x + TILE / 2 - bar, y, bar * 2, bar }; wcol(q); }
            if (d) { SDL_Rect q{ x + TILE / 2 - bar, y + TILE - bar, bar * 2, bar }; wcol(q); }
            if (l) { SDL_Rect q{ x, y + TILE / 2 - bar, bar, bar * 2 }; wcol(q); }
            if (ri) { SDL_Rect q{ x + TILE - bar, y + TILE / 2 - bar, bar, bar * 2 }; wcol(q); }
        }
    }
}

static void drawDoor(SDL_Renderer *ren) {
    SDL_Rect r{ 13 * TILE, 12 * TILE + TILE / 2 - 2, TILE * 2, 4 };
    SDL_SetRenderDrawColor(REN, (C_DOOR >> 16) & 255, (C_DOOR >> 8) & 255, C_DOOR & 255, 255);
    SDL_RenderFillRect(REN, &r);
}

static void drawPellets(SDL_Renderer *ren) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (!G.pellet[r * COLS + c]) continue;
            bool power = (MAZE[r][c] == 'o');
            float px, py;
            cellPix((float)c, (float)r, px, py);
            Uint32 col = power ? C_POWER : C_PELLET;
            int rad = power ? (int)(6 + 2 * std::sin(clockT * 6.0f)) : 3;
            drawCircle(ren, (int)px, (int)py, rad, col);
        }
}

static void drawPac(SDL_Renderer *ren, float x, float y, int dir, float chomp, float scale, Uint32 col) {
    float px, py;
    cellPix(x, y, px, py);
    float R = TILE * 0.42f * scale;
    if (R < 0.5f) return;
    float a0 = 0;
    switch (dir) { case UP: a0 = -1.5708f; break; case LEFT: a0 = 3.14159f; break; case DOWN: a0 = 1.5708f; break; }
    float m = 0.35f + 1.05f * (0.5f + 0.5f * std::sin(chomp));  // mouth half-angle
    SDL_Color fc = { (Uint8)((col >> 16) & 255), (Uint8)((col >> 8) & 255),
                     (Uint8)(col & 255), 255 };
    const int N = 48;
    std::vector<SDL_Vertex> v; v.reserve(N + 2);
    std::vector<int> idx; idx.reserve(3 * N);
    v.push_back({ {px, py}, fc, {0, 0} });               // v0 = centre
    for (int i = 0; i <= N; i++) {
        float a = a0 + m + (2.0f * 3.14159f - 2.0f * m) * (float)i / N;
        v.push_back({ {px + R * std::cos(a), py + R * std::sin(a)}, fc, {0, 0} });
    }
    for (int i = 1; i <= N; i++) {                       // triangle fan from centre
        idx.push_back(0); idx.push_back(i); idx.push_back(i + 1);
    }
    SDL_RenderGeometry(ren, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

static void drawGhost(SDL_Renderer *ren, float x, float y, int id, GState st, float frightT) {
    if (st == GState::Eyes) {
        // just the eyes heading home
        float px, py;
        cellPix(x, y, px, py);
        drawCircle(ren, (int)px - 4, (int)py - 2, 4, C_BG);
        drawCircle(ren, (int)px + 4, (int)py - 2, 4, C_BG);
        drawCircle(ren, (int)px - 3, (int)py - 2, 2, 0x2222FF);
        drawCircle(ren, (int)px + 5, (int)py - 2, 2, 0x2222FF);
        return;
    }
    float px, py;
    cellPix(x, y, px, py);
    float R = TILE * 0.42f;
    bool scared = (st == GState::Out && frightT > 0);
    bool flashing = scared && frightT < 2.0f && (int)(clockT * 3.0f) % 2 == 0;
    Uint32 body = scared ? (flashing ? 0xFFFFFF : C_FRIGHT) : C_GHOST[id];
    // head dome
    drawCircle(ren, (int)px, (int)py - 1, (int)R, body);
    // body
    SDL_SetRenderDrawColor(REN, (body >> 16) & 255, (body >> 8) & 255, body & 255, 255);
    SDL_Rect b{ (int)(px - R), (int)py - 1, (int)(2 * R), (int)(R + 3) };
    SDL_RenderFillRect(REN, &b);
    // skirt: three bumps
    int feet[3] = { (int)(px - R) + 2, (int)(px + 1), (int)(px + R) - 4 };
    for (int i = 0; i < 3; i++) drawCircle(ren, feet[i], (int)py + (int)(R + 2), 3, body);
    // face
    if (scared) {
        Uint32 fc = flashing ? 0xFF5555 : 0xFFCCAA;
        for (int i = -1; i <= 1; i++)
            drawCircle(ren, (int)px + i * 5, (int)py - 2, 2, fc);
        // wavy mouth
        SDL_SetRenderDrawColor(REN, (fc >> 16) & 255, (fc >> 8) & 255, fc & 255, 255);
        for (int i = -3; i <= 3; i++)
            if ((i % 2) == ((int)(clockT * 4) % 2)) {
                SDL_Rect m{ (int)px + i * 2, (int)py + 5, 2, 2 };
                SDL_RenderFillRect(REN, &m);
            }
    } else {
        drawCircle(ren, (int)px - 4, (int)py - 3, 4, C_BG);
        drawCircle(ren, (int)px + 4, (int)py - 3, 4, C_BG);
        drawCircle(ren, (int)px - 3, (int)py - 2, 2, 0x2222FF);
        drawCircle(ren, (int)px + 5, (int)py - 2, 2, 0x2222FF);
    }
}

static void drawHUD(SDL_Renderer *ren) {
    int y0 = ROWS * TILE + 8;
    char buf[64];
    SDL_SetRenderDrawBlendMode(REN, SDL_BLENDMODE_BLEND);
    snprintf(buf, sizeof buf, "SCORE %d", G.score);
    drawText(ren, buf, 16, y0, 2, C_TEXT);
    snprintf(buf, sizeof buf, "HI %d", G.hi);
    drawTextC(ren, buf, WIN_W / 2, y0, 2, C_TEXT);
    snprintf(buf, sizeof buf, "LEVEL %d", G.level);
    drawText(ren, buf, WIN_W - 16 - textWidth(buf, 2), y0, 2, C_TEXT);
    // lives as small pac icons in the HUD band
    for (int i = 0; i < G.lives - 1; i++) {
        float cx = (16.0f + i * 30.0f) / TILE - 0.5f;
        float cy = (ROWS * TILE + 36.0f) / TILE - 0.5f;
        drawPac(ren, cx, cy, RIGHT, clockT * 8, 0.7f, C_PAC);
    }
    if (G.paused) drawTextC(ren, "PAUSED", WIN_W / 2, y0 + 26, 2, C_DIM);
}

static void draw() {
    SDL_SetRenderDrawColor(REN, (C_BG >> 16) & 255, (C_BG >> 8) & 255, C_BG & 255, 255);
    SDL_RenderClear(REN);
    drawWalls(REN);
    drawPellets(REN);
    drawDoor(REN);

    if (G.ph != Phase::LevelClear) {
        for (auto &g : G.ghosts) {
            if (g.st == GState::InHouse || g.st == GState::Revive || g.st == GState::Leaving) {
                if (G.ph != Phase::Dying)
                    drawGhost(REN, g.x, g.y, g.id, g.st, g.frightT);
            } else {
                drawGhost(REN, g.x, g.y, g.id, g.st, g.frightT);
            }
        }
        if (G.ph != Phase::Dying || G.phT > 1.2f)
            drawPac(REN, G.pac.x, G.pac.y, G.pac.dir, clockT * 9,
                    G.ph == Phase::Dying ? std::max(0.0f, (G.phT - 1.2f) / 1.2f) : 1.0f, C_PAC);
        else {
            // death flash: pac spinning out
            float k = 1.0f - (G.phT - 1.2f) / 1.2f;
            drawPac(REN, G.pac.x, G.pac.y - k * 0.5f, UP, clockT * 30, std::max(0.05f, 1.0f - k), C_PAC);
        }
    } else {
        // level clear: ghosts flash, no pac
        if ((int)(clockT * 4.0f) % 2 == 0)
            for (auto &g : G.ghosts) drawGhost(REN, g.x, g.y, g.id, g.st, g.frightT);
    }

    // floating scores
    for (auto &p : pops) {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", p.pts);
        float px, py;
        cellPix(p.x, p.y, px, py);
        drawText(REN, buf, (int)px - 8, (int)py - 12 - (int)((1.0f - p.t) * 14), 1, C_POWER);
    }

    drawHUD(REN);

    // overlays
    if (G.ph == Phase::Ready)
        drawTextC(REN, "READY!", WIN_W / 2, ROWS * TILE / 2 + 8, 2, C_POWER);
    else if (G.ph == Phase::GameOver) {
        drawTextC(REN, "GAME OVER", WIN_W / 2, ROWS * TILE / 2 - 20, 3, 0xFF5555);
        drawTextC(REN, "PRESS R TO RESTART", WIN_W / 2, ROWS * TILE / 2 + 24, 1, C_DIM);
    }
    SDL_RenderPresent(REN);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static void setWant(int d) { if (d != NONE) G.pac.want = d; }

static void handleKey(SDL_Keysym k) {
    switch (k.sym) {
        case SDLK_UP:    case SDLK_w: setWant(UP);    break;
        case SDLK_DOWN:  case SDLK_s: setWant(DOWN);  break;
        case SDLK_LEFT:  case SDLK_a: setWant(LEFT);  break;
        case SDLK_RIGHT: case SDLK_d: setWant(RIGHT); break;
        case SDLK_p: G.paused = !G.paused; break;
        case SDLK_m: SFX.toggle(); G.soundOn = SFX.enabled; break;
        case SDLK_r: newGame(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    srand((unsigned)time(nullptr));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("PAC-MAN", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }

    REN = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!REN) REN = SDL_CreateRenderer(win, -1, 0);
    if (!REN) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }

    // palette (0xRRGGBB; decomposed with bit shifts when drawing)
    C_WALL     = 0x4444EE;
    C_WALL_D   = 0x222288;
    C_PELLET   = 0xFFD890;
    C_POWER    = 0xFFE0A0;
    C_PAC      = 0xFFE000;
    C_BG       = 0x000000;
    C_TEXT     = 0xFFFFFF;
    C_DIM      = 0x999999;
    C_GHOST[0] = 0xFF0000;
    C_GHOST[1] = 0xFFB0F0;
    C_GHOST[2] = 0x80FFFF;
    C_GHOST[3] = 0xFFB852;
    C_FRIGHT   = 0x2121F0;
    C_FRIGHT_F = 0xFFFFFF;
    C_DOOR     = 0xFFB0C0;
    (void)C_WALL_D; (void)C_FRIGHT_F;

    buildFont();
    SFX.init();
    newGame();

    bool running = true;
    Uint32 last = SDL_GetTicks();
    float acc = 0;
    SDL_Event e;
    while (running) {
        // events
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = false;
                else handleKey(e.key.keysym);
            }
        }
        Uint32 now = SDL_GetTicks();
        float frameDt = std::min(0.1f, (float)(now - last) / 1000.0f);
        last = now;
        clockT += frameDt;
        acc += frameDt;
        while (acc >= DT) { update(DT); acc -= DT; }

        draw();
        SDL_Delay(0);  // vsync present handles pacing
    }

    SFX.destroy();
    SDL_DestroyRenderer(REN);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
