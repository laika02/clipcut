#ifndef CLIPCUT_UI_BITMAP_TEXT_H
#define CLIPCUT_UI_BITMAP_TEXT_H

#include <SDL.h>

void bitmap_text_draw(
    SDL_Renderer *renderer,
    int x,
    int y,
    int scale,
    SDL_Color color,
    const char *text
);

#endif
