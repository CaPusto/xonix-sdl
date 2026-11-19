#ifndef XONIX_GFX_H
#define XONIX_GFX_H

#include <SDL3/SDL.h>

typedef enum {
    GFX_COLOR_BG = 0,
    GFX_COLOR_BORDER,
    GFX_COLOR_SEA,
    GFX_COLOR_LAND,
    GFX_COLOR_TRAIL,
    GFX_COLOR_PLAYER,
    GFX_COLOR_BALL,
    GFX_COLOR_SPIDER,
    GFX_COLOR_TEXT,
    GFX_COLOR_TEXT_DIM,
    GFX_COLOR_COUNT
} GfxColor;

typedef enum {
    GFX_SPRITE_PLAYER = 0,
    GFX_SPRITE_BALL,
    GFX_SPRITE_SPIDER,
    GFX_SPRITE_COUNT
} GfxSprite;

#define GFX_SPIDER_FRAMES 4

extern const SDL_Color gfx_colors[GFX_COLOR_COUNT];

bool gfx_init(SDL_Renderer* renderer);
void gfx_shutdown(void);

void gfx_clear(SDL_Renderer* renderer, GfxColor color);
void gfx_rect(SDL_Renderer* renderer, int x, int y, int w, int h, GfxColor color);
void gfx_rect_alpha(SDL_Renderer* renderer, int x, int y, int w, int h,
                    Uint8 r, Uint8 g, Uint8 b, Uint8 a);

/* One bitmap font: Oxanium Bold (SIL OFL 1.1). The alpha atlas is generated into
 * src/font_ui.h. `gfx_text_dseg*` are aliases kept for numeric call sites.
 * `scale` multiplies glyph dimensions. */
void gfx_text(SDL_Renderer* renderer, int x, int y, const char* text,
              int scale, GfxColor color);
void gfx_text_dseg(SDL_Renderer* renderer, int x, int y, const char* text,
                   int scale, GfxColor color);
int gfx_text_width(const char* text, int scale);
int gfx_text_dseg_width(const char* text, int scale);
void gfx_text_centered(SDL_Renderer* renderer, int y, const char* text,
                       int scale, GfxColor color);
void gfx_text_dseg_centered(SDL_Renderer* renderer, int y, const char* text,
                            int scale, GfxColor color);
int gfx_glyph_width(void);
int gfx_glyph_height(void);

/* Draw a sprite at its native size (PLAYER_SIZE player, BALL_SIZE ball,
 * SPIDER_SIZE spider) with its top-left corner at (x, y). */
void gfx_sprite(SDL_Renderer* renderer, GfxSprite sprite, int x, int y);

/* Animated top-down spider. `angle` is in degrees clockwise from "head up". */
void gfx_spider_draw(SDL_Renderer* renderer, int frame, double angle,
                     int x, int y);

#endif /* XONIX_GFX_H */
