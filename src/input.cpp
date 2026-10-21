#include "input.h"
#include "config.h"
#include "game.h"
#include "sound.h"

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static void setWant(int d) { if (d != NONE) G.pac.want = d; }

void handleKey(SDL_Keysym k) {
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
