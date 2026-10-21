#pragma once

#include <SDL2/SDL.h>
#include <cstdint>

// 5x7 bitmap font + text drawing
void buildFont();
void drawText(SDL_Renderer *ren, const char *s, int x, int y, int scale, Uint32 col);
int  textWidth(const char *s, int scale);
void drawTextC(SDL_Renderer *ren, const char *s, int cx, int y, int scale, Uint32 col);
