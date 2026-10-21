#pragma once

#include <SDL2/SDL.h>

// Rendering (draw.cpp)
extern SDL_Renderer *REN;
extern float clockT;   // global animation clock

void draw();
