#include "maze.h"
#include "game.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Maze  (28 x 31).  '#' wall  '.' pellet  'o' power pellet  '-' house door
//           ' ' open, no pellet.  Row TUNNEL wraps at the edges.
// ---------------------------------------------------------------------------
const char *MAZE[ROWS] = {
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
// Maze helpers
// ---------------------------------------------------------------------------
// Can an entity standing at grid centre (c,r) step towards dir?
bool canStep(int c, int r, int d, bool throughDoor) {
    if (r == TUNNEL) return true;               // tunnel row wraps
    int nc = c + DX[d], nr = r + DY[d];
    if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) return false;
    char ch = MAZE[nr][nc];
    if (ch == '#') return false;
    if (ch == '-' && !throughDoor) return false;
    return true;
}
int  pacCellX() { return (int)std::lround(G.pac.x); }
int  pacCellY() { return (int)std::lround(G.pac.y); }

// ---------------------------------------------------------------------------
// Movement core — sub-stepped so every grid centre is hit exactly
// ---------------------------------------------------------------------------
void moveActor(Actor &a, float speed, float dt, const AtC &atCentre) {
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
