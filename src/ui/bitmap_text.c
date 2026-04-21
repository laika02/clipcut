#include "ui/bitmap_text.h"

#include <stdint.h>

static uint8_t glyph_row(char c, int row) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - ('a' - 'A'));
    }

    switch (c) {
        case 'A': { static const uint8_t rows[7] = {14, 17, 17, 31, 17, 17, 17}; return rows[row]; }
        case 'B': { static const uint8_t rows[7] = {30, 17, 17, 30, 17, 17, 30}; return rows[row]; }
        case 'C': { static const uint8_t rows[7] = {14, 17, 16, 16, 16, 17, 14}; return rows[row]; }
        case 'D': { static const uint8_t rows[7] = {30, 17, 17, 17, 17, 17, 30}; return rows[row]; }
        case 'E': { static const uint8_t rows[7] = {31, 16, 16, 30, 16, 16, 31}; return rows[row]; }
        case 'F': { static const uint8_t rows[7] = {31, 16, 16, 30, 16, 16, 16}; return rows[row]; }
        case 'G': { static const uint8_t rows[7] = {14, 17, 16, 16, 19, 17, 14}; return rows[row]; }
        case 'H': { static const uint8_t rows[7] = {17, 17, 17, 31, 17, 17, 17}; return rows[row]; }
        case 'I': { static const uint8_t rows[7] = {31, 4, 4, 4, 4, 4, 31}; return rows[row]; }
        case 'J': { static const uint8_t rows[7] = {31, 2, 2, 2, 2, 18, 12}; return rows[row]; }
        case 'K': { static const uint8_t rows[7] = {17, 18, 20, 24, 20, 18, 17}; return rows[row]; }
        case 'L': { static const uint8_t rows[7] = {16, 16, 16, 16, 16, 16, 31}; return rows[row]; }
        case 'M': { static const uint8_t rows[7] = {17, 27, 21, 21, 17, 17, 17}; return rows[row]; }
        case 'N': { static const uint8_t rows[7] = {17, 25, 21, 19, 17, 17, 17}; return rows[row]; }
        case 'O': { static const uint8_t rows[7] = {14, 17, 17, 17, 17, 17, 14}; return rows[row]; }
        case 'P': { static const uint8_t rows[7] = {30, 17, 17, 30, 16, 16, 16}; return rows[row]; }
        case 'Q': { static const uint8_t rows[7] = {14, 17, 17, 17, 21, 18, 13}; return rows[row]; }
        case 'R': { static const uint8_t rows[7] = {30, 17, 17, 30, 20, 18, 17}; return rows[row]; }
        case 'S': { static const uint8_t rows[7] = {15, 16, 16, 14, 1, 1, 30}; return rows[row]; }
        case 'T': { static const uint8_t rows[7] = {31, 4, 4, 4, 4, 4, 4}; return rows[row]; }
        case 'U': { static const uint8_t rows[7] = {17, 17, 17, 17, 17, 17, 14}; return rows[row]; }
        case 'V': { static const uint8_t rows[7] = {17, 17, 17, 17, 17, 10, 4}; return rows[row]; }
        case 'W': { static const uint8_t rows[7] = {17, 17, 17, 21, 21, 21, 10}; return rows[row]; }
        case 'X': { static const uint8_t rows[7] = {17, 17, 10, 4, 10, 17, 17}; return rows[row]; }
        case 'Y': { static const uint8_t rows[7] = {17, 17, 10, 4, 4, 4, 4}; return rows[row]; }
        case 'Z': { static const uint8_t rows[7] = {31, 1, 2, 4, 8, 16, 31}; return rows[row]; }
        case '0': { static const uint8_t rows[7] = {14, 17, 19, 21, 25, 17, 14}; return rows[row]; }
        case '1': { static const uint8_t rows[7] = {4, 12, 4, 4, 4, 4, 14}; return rows[row]; }
        case '2': { static const uint8_t rows[7] = {14, 17, 1, 2, 4, 8, 31}; return rows[row]; }
        case '3': { static const uint8_t rows[7] = {30, 1, 1, 14, 1, 1, 30}; return rows[row]; }
        case '4': { static const uint8_t rows[7] = {2, 6, 10, 18, 31, 2, 2}; return rows[row]; }
        case '5': { static const uint8_t rows[7] = {31, 16, 16, 30, 1, 1, 30}; return rows[row]; }
        case '6': { static const uint8_t rows[7] = {14, 16, 16, 30, 17, 17, 14}; return rows[row]; }
        case '7': { static const uint8_t rows[7] = {31, 1, 2, 4, 8, 8, 8}; return rows[row]; }
        case '8': { static const uint8_t rows[7] = {14, 17, 17, 14, 17, 17, 14}; return rows[row]; }
        case '9': { static const uint8_t rows[7] = {14, 17, 17, 15, 1, 1, 14}; return rows[row]; }
        case '/': { static const uint8_t rows[7] = {1, 2, 2, 4, 8, 8, 16}; return rows[row]; }
        case '.': { static const uint8_t rows[7] = {0, 0, 0, 0, 0, 12, 12}; return rows[row]; }
        case ':': { static const uint8_t rows[7] = {0, 12, 12, 0, 12, 12, 0}; return rows[row]; }
        case '-': { static const uint8_t rows[7] = {0, 0, 0, 31, 0, 0, 0}; return rows[row]; }
        case '_': { static const uint8_t rows[7] = {0, 0, 0, 0, 0, 0, 31}; return rows[row]; }
        case '(': { static const uint8_t rows[7] = {2, 4, 8, 8, 8, 4, 2}; return rows[row]; }
        case ')': { static const uint8_t rows[7] = {8, 4, 2, 2, 2, 4, 8}; return rows[row]; }
        case '>': { static const uint8_t rows[7] = {16, 8, 4, 2, 4, 8, 16}; return rows[row]; }
        case ' ': return 0;
        default: { static const uint8_t rows[7] = {31, 1, 2, 4, 8, 0, 8}; return rows[row]; }
    }
}

void bitmap_text_draw(
    SDL_Renderer *renderer,
    int x,
    int y,
    int scale,
    SDL_Color color,
    const char *text
) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; text[i] != '\0'; ++i) {
        const char c = text[i];
        for (int row = 0; row < 7; ++row) {
            const uint8_t bits = glyph_row(c, row);
            for (int col = 0; col < 5; ++col) {
                if ((bits & (1u << (4 - col))) == 0) {
                    continue;
                }
                const SDL_Rect pixel = {
                    .x = x + (i * (6 * scale)) + (col * scale),
                    .y = y + (row * scale),
                    .w = scale,
                    .h = scale,
                };
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
    }
}
