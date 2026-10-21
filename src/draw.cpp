#include "draw.h"
#include "config.h"
#include "maze.h"
#include "game.h"
#include "font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

SDL_Renderer *REN = nullptr;
float clockT = 0;   // global animation clock

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

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void draw() {
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
