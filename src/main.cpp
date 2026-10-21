#include "config.h"
#include "draw.h"
#include "font.h"
#include "game.h"
#include "input.h"
#include "sound.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
Uint32 C_WALL, C_WALL_D, C_PELLET, C_POWER, C_PAC, C_BG, C_TEXT, C_DIM;
Uint32 C_GHOST[4] = {0};                // blinky, pinky, inky, clyde
Uint32 C_FRIGHT, C_FRIGHT_F, C_DOOR;

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
