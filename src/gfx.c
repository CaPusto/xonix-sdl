#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "font_ui.h"
#include "gfx.h"

const SDL_Color gfx_colors[GFX_COLOR_COUNT] = {
    {   0,   0,   0, 255 },  /* BG        */
    { 100,  60,   0, 255 },  /* BORDER    */
    {   6,  18,  52, 255 },  /* SEA       */
    {   0, 110,  60, 255 },  /* LAND      */
    { 255, 160,   0, 255 },  /* TRAIL     */
    { 255, 210,   0, 255 },  /* PLAYER    */
    { 235,  50,  45, 255 },  /* BALL      */
    { 245, 245, 245, 255 },  /* SPIDER    */
    { 255, 255, 255, 255 },  /* TEXT      */
    { 160, 160, 170, 255 },  /* TEXT_DIM  */
};

/* Player and ball are generated as shaded spheres; the spider uses its own
 * animated 24x24 procedural frames. */
static const int sprite_sizes[GFX_SPRITE_COUNT] = {
    PLAYER_SIZE,
    BALL_SIZE,
    SPIDER_SIZE,
};

static SDL_Texture* font_ui_texture;
static SDL_Texture* sprite_textures[GFX_SPRITE_COUNT];
static SDL_Texture* spider_textures[GFX_SPIDER_FRAMES];

static uint32_t mapped_pixel(SDL_Surface* surface, Uint8 r, Uint8 g,
                             Uint8 b, Uint8 a) {
    return SDL_MapRGBA(SDL_GetPixelFormatDetails(surface->format),
                       SDL_GetSurfacePalette(surface), r, g, b, a);
}

static float clampf(float value, float low, float high) {
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

/* ------------------------------------------------------------------------- *
 * Animated spider: a top-down chitin body with eight jointed legs. Four
 * frames alternate the leg positions, and the game rotates the sprite toward
 * the spider's movement direction.
 * ------------------------------------------------------------------------- */

static void spider_set(SDL_Surface* surface, int x, int y,
                       Uint8 r, Uint8 g, Uint8 b) {
    if(x < 0 || y < 0 || x >= SPIDER_SIZE || y >= SPIDER_SIZE) {
        return;
    }
    ((Uint32*)((Uint8*)surface->pixels + (size_t)y * surface->pitch))[x] =
        mapped_pixel(surface, r, g, b, 255);
}

static void spider_disc(SDL_Surface* surface, int cx, int cy, int radius,
                        Uint8 r, Uint8 g, Uint8 b) {
    int dy, dx;

    for(dy = -radius; dy <= radius; dy++) {
        for(dx = -radius; dx <= radius; dx++) {
            if(dx * dx + dy * dy <= radius * radius) {
                spider_set(surface, cx + dx, cy + dy, r, g, b);
            }
        }
    }
}

static void spider_line(SDL_Surface* surface, int x0, int y0,
                        int x1, int y1, int radius,
                        Uint8 r, Uint8 g, Uint8 b) {
    const int dx = abs(x1 - x0);
    const int dy = abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for(;;) {
        spider_disc(surface, x0, y0, radius, r, g, b);
        if(x0 == x1 && y0 == y1) {
            break;
        }
        {
            const int e2 = 2 * err;

            if(e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if(e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

static void spider_ellipse(SDL_Surface* surface, int cx, int cy,
                           int rx, int ry, Uint8 r, Uint8 g, Uint8 b) {
    int y, x;

    for(y = cy - ry; y <= cy + ry; y++) {
        for(x = cx - rx; x <= cx + rx; x++) {
            const int dx = x - cx;
            const int dy = y - cy;

            if(dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry) {
                spider_set(surface, x, y, r, g, b);
            }
        }
    }
}

static void draw_spider_frame(SDL_Surface* surface, int frame) {
    /* { knee dy, foot dy } per leg; alternating frames create a walking gait. */
    static const int8_t gait[GFX_SPIDER_FRAMES][4][2] = {
        { { -1, -2 }, {  1,  2 }, { -1,  2 }, {  1, -2 } },
        { {  0,  0 }, {  0,  0 }, {  0,  0 }, {  0,  0 } },
        { {  1,  2 }, { -1, -2 }, {  1, -2 }, { -1,  2 } },
        { {  0,  1 }, {  0, -1 }, {  0, -1 }, {  0,  1 } },
    };
    static const int base_x[4]  = { 15, 16, 16, 15 };
    static const int base_y[4]  = {  8, 10, 13, 16 };
    static const int knee_x[4]  = { 19, 21, 21, 19 };
    static const int knee_y[4]  = {  5,  8, 14, 19 };
    static const int foot_x[4]  = { 22, 23, 23, 21 };
    static const int foot_y[4]  = {  2,  6, 16, 22 };
    int side, leg;

    for(side = 0; side < 2; side++) {
        for(leg = 0; leg < 4; leg++) {
            int bx = base_x[leg];
            int by = base_y[leg];
            int kx = knee_x[leg];
            int ky = knee_y[leg] + gait[frame][leg][0];
            int fx = foot_x[leg];
            int fy = foot_y[leg] + gait[frame][leg][1];

            if(side == 1) {
                bx = SPIDER_SIZE - 1 - bx;
                kx = SPIDER_SIZE - 1 - kx;
                fx = SPIDER_SIZE - 1 - fx;
            }

            spider_line(surface, bx, by, kx, ky, 1, 42, 26, 24);
            spider_line(surface, kx, ky, fx, fy, 1, 42, 26, 24);
            spider_line(surface, bx, by, kx, ky, 0, 122, 84, 68);
            spider_line(surface, kx, ky, fx, fy, 0, 122, 84, 68);
            spider_disc(surface, kx, ky, 1, 96, 64, 52);
        }
    }

    /* Abdomen and head, both shaded from the upper left. A tiny vertical bob
     * makes the neutral frames part of the walk cycle too. */
    {
        const int bob = frame == 1 ? -1 : (frame == 3 ? 1 : 0);

        spider_ellipse(surface, 12, 15 + bob, 5, 5, 34, 22, 22);
        spider_ellipse(surface, 11, 14 + bob, 3, 3, 78, 48, 44);
        spider_ellipse(surface, 12, 17 + bob, 3, 1, 128, 82, 66);
        spider_ellipse(surface, 12, 11 + bob, 2, 1, 40, 26, 24);
        spider_ellipse(surface, 12, 8 + bob, 3, 3, 56, 34, 30);
        spider_ellipse(surface, 11, 7 + bob, 2, 2, 116, 78, 62);

        /* Four eyes and two small fangs make the orientation readable. */
        spider_disc(surface, 10, 6 + bob, 1, 255, 58, 48);
        spider_disc(surface, 14, 6 + bob, 1, 255, 58, 48);
        spider_set(surface, 10, 5 + bob, 255, 226, 220);
        spider_set(surface, 14, 5 + bob, 255, 226, 220);
        spider_set(surface,  9, 8 + bob, 255, 145, 60);
        spider_set(surface, 15, 8 + bob, 255, 145, 60);
        spider_set(surface, 10, 4 + bob, 214, 208, 190);
        spider_set(surface, 14, 4 + bob, 214, 208, 190);
        spider_set(surface, 10, 3 + bob, 150, 145, 135);
        spider_set(surface, 14, 3 + bob, 150, 145, 135);
    }
}

static bool make_spider_frames(SDL_Renderer* renderer) {
    int frame;

    for(frame = 0; frame < GFX_SPIDER_FRAMES; frame++) {
        SDL_Surface* surface = SDL_CreateSurface(SPIDER_SIZE, SPIDER_SIZE,
                                                 SDL_PIXELFORMAT_RGBA8888);

        if(surface == NULL) {
            return false;
        }
        if(!SDL_FillSurfaceRect(surface, NULL,
                                mapped_pixel(surface, 0, 0, 0, 0))) {
            SDL_DestroySurface(surface);
            return false;
        }
        if(!SDL_LockSurface(surface)) {
            SDL_DestroySurface(surface);
            return false;
        }
        draw_spider_frame(surface, frame);
        SDL_UnlockSurface(surface);

        spider_textures[frame] = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if(spider_textures[frame] == NULL) {
            return false;
        }
        SDL_SetTextureScaleMode(spider_textures[frame], SDL_SCALEMODE_NEAREST);
    }
    return true;
}

/* Procedural shaded sphere used by both the red ball and the yellow player:
 * dark rim, soft light from top-left and a small specular highlight. Colours
 * are per-pixel RGBA, so the gradient uses far more than 16 shades. */
static bool make_sphere(SDL_Renderer* renderer, GfxSprite sprite, int size,
                        float base_r, float base_g, float base_b,
                        float spec_r, float spec_g, float spec_b) {
    const float center = (size - 1) * 0.5f;
    const float radius = size * 0.5f - 0.8f;
    SDL_Surface* surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA8888);
    int y, x;

    if(surface == NULL) {
        return false;
    }
    if(!SDL_LockSurface(surface)) {
        SDL_DestroySurface(surface);
        return false;
    }

    for(y = 0; y < size; y++) {
        for(x = 0; x < size; x++) {
            const float dx = (float)x - center;
            const float dy = (float)y - center;
            const float dist = sqrtf(dx * dx + dy * dy);
            Uint32 color;

            if(dist > radius + 0.5f) {
                color = mapped_pixel(surface, 0, 0, 0, 0);
            } else {
                const float alpha = clampf((radius + 0.5f - dist) * 255.0f,
                                           0.0f, 255.0f);
                const float nx = dx / radius;
                const float ny = dy / radius;
                const float light = clampf((-0.55f * nx - 0.75f * ny + 1.0f) *
                                           0.5f, 0.0f, 1.0f);
                const float lum = 0.42f + 0.58f * light;
                const float rim = dist > radius * 0.82f ? 0.58f : 1.0f;
                const float hx = (float)x - (center - radius * 0.30f);
                const float hy = (float)y - (center - radius * 0.35f);
                const float spec = expf(-(hx * hx + hy * hy) /
                                         (2.0f * radius * radius * 0.035f));
                const Uint8 r = (Uint8)clampf((base_r * lum +
                                               spec_r * spec) * rim,
                                              0.0f, 255.0f);
                const Uint8 g = (Uint8)clampf((base_g * lum +
                                               spec_g * spec) * rim,
                                              0.0f, 255.0f);
                const Uint8 b = (Uint8)clampf((base_b * lum +
                                               spec_b * spec) * rim,
                                              0.0f, 255.0f);

                color = mapped_pixel(surface, r, g, b, (Uint8)alpha);
            }
            ((Uint32*)((Uint8*)surface->pixels +
                       (size_t)y * surface->pitch))[x] = color;
        }
    }
    SDL_UnlockSurface(surface);

    sprite_textures[sprite] = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if(sprite_textures[sprite] == NULL) {
        return false;
    }
    SDL_SetTextureScaleMode(sprite_textures[sprite], SDL_SCALEMODE_NEAREST);
    return true;
}

static bool make_font(SDL_Renderer* renderer,
                      const unsigned char* alpha, int glyph_count,
                      int glyph_w, int glyph_h, SDL_Texture** output) {
    const int width = glyph_w * glyph_count;
    SDL_Surface* surface = SDL_CreateSurface(width, glyph_h,
                                             SDL_PIXELFORMAT_RGBA8888);
    int y, x;

    if(surface == NULL) {
        return false;
    }
    if(!SDL_LockSurface(surface)) {
        SDL_DestroySurface(surface);
        return false;
    }

    for(y = 0; y < glyph_h; y++) {
        Uint32* row = (Uint32*)((Uint8*)surface->pixels +
                                (size_t)y * surface->pitch);
        for(x = 0; x < width; x++) {
            const size_t index = (size_t)y * (size_t)width + (size_t)x;
            row[x] = mapped_pixel(surface, 255, 255, 255, alpha[index]);
        }
    }
    SDL_UnlockSurface(surface);

    *output = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if(*output == NULL) {
        return false;
    }
    SDL_SetTextureScaleMode(*output, SDL_SCALEMODE_NEAREST);
    return true;
}

bool gfx_init(SDL_Renderer* renderer) {
    int g;

    if(!make_font(renderer, font_ui_alpha, FONT_UI_GLYPH_COUNT,
                  FONT_UI_GLYPH_W, FONT_UI_GLYPH_H, &font_ui_texture) ||
       !make_spider_frames(renderer)) {
        gfx_shutdown();
        return false;
    }
    for(g = 0; g < GFX_SPRITE_COUNT; g++) {
        bool ok;

        if(g == GFX_SPRITE_PLAYER) {
            ok = make_sphere(renderer, GFX_SPRITE_PLAYER, PLAYER_SIZE,
                             240.0f, 190.0f, 20.0f,
                             204.0f, 196.0f, 128.0f);
        } else if(g == GFX_SPRITE_BALL) {
            ok = make_sphere(renderer, GFX_SPRITE_BALL, BALL_SIZE,
                             235.0f, 35.0f, 30.0f,
                             204.0f, 152.0f, 119.0f);
        } else {
            continue;   /* spider uses the animation frames */
        }
        if(!ok) {
            gfx_shutdown();
            return false;
        }
    }
    return true;
}

void gfx_shutdown(void) {
    int i;

    for(i = 0; i < GFX_SPRITE_COUNT; i++) {
        SDL_DestroyTexture(sprite_textures[i]);
        sprite_textures[i] = NULL;
    }
    for(i = 0; i < GFX_SPIDER_FRAMES; i++) {
        SDL_DestroyTexture(spider_textures[i]);
        spider_textures[i] = NULL;
    }
    SDL_DestroyTexture(font_ui_texture);
    font_ui_texture = NULL;
}

void gfx_clear(SDL_Renderer* renderer, GfxColor color) {
    const SDL_Color c = gfx_colors[color];
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderClear(renderer);
}

void gfx_rect(SDL_Renderer* renderer, int x, int y, int w, int h,
              GfxColor color) {
    const SDL_Color c = gfx_colors[color];
    SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };

    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &rect);
}

/* Filled rectangle with an explicit alpha channel, used by the level menu. */
void gfx_rect_alpha(SDL_Renderer* renderer, int x, int y, int w, int h,
                    Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_BlendMode previous = SDL_BLENDMODE_NONE;
    SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };

    SDL_GetRenderDrawBlendMode(renderer, &previous);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawBlendMode(renderer, previous);
}

static int glyph_index(char c, int first, int count) {
    unsigned char code = (unsigned char)c;

    if(code < first || code >= first + count) {
        code = ' ';
    }
    return (int)(code - first);
}

static void draw_font_text(SDL_Renderer* renderer, SDL_Texture* texture,
                           int first, int count, int glyph_w, int glyph_h,
                           int x, int y, const char* text, int scale,
                           GfxColor color) {
    const SDL_Color c = gfx_colors[color];
    int column = 0;

    if(text == NULL) {
        return;
    }

    SDL_SetTextureColorMod(texture, c.r, c.g, c.b);
    while(*text) {
        const int index = glyph_index(*text++, first, count);
        SDL_FRect src = { (float)(index * glyph_w), 0.0f,
                          (float)glyph_w, (float)glyph_h };
        SDL_FRect dst = { (float)(x + column * glyph_w * scale), (float)y,
                          (float)(glyph_w * scale), (float)(glyph_h * scale) };

        SDL_RenderTexture(renderer, texture, &src, &dst);
        column++;
    }
}

int gfx_text_width(const char* text, int scale) {
    if(text == NULL) {
        return 0;
    }
    return (int)strlen(text) * FONT_UI_GLYPH_W * scale;
}

/* Oxanium Bold: one strict, geometric face for words and numbers alike. */
void gfx_text(SDL_Renderer* renderer, int x, int y, const char* text,
              int scale, GfxColor color) {
    draw_font_text(renderer, font_ui_texture, FONT_UI_FIRST,
                   FONT_UI_GLYPH_COUNT, FONT_UI_GLYPH_W, FONT_UI_GLYPH_H,
                   x, y, text, scale, color);
}

/* Kept as aliases so numeric call sites need no special font handling. */
int gfx_text_dseg_width(const char* text, int scale) {
    return gfx_text_width(text, scale);
}

void gfx_text_dseg(SDL_Renderer* renderer, int x, int y, const char* text,
                   int scale, GfxColor color) {
    gfx_text(renderer, x, y, text, scale, color);
}

void gfx_text_centered(SDL_Renderer* renderer, int y, const char* text,
                       int scale, GfxColor color) {
    gfx_text(renderer, (SCREEN_W - gfx_text_width(text, scale)) / 2,
             y, text, scale, color);
}

void gfx_text_dseg_centered(SDL_Renderer* renderer, int y, const char* text,
                            int scale, GfxColor color) {
    gfx_text_centered(renderer, y, text, scale, color);
}

int gfx_glyph_width(void) {
    return FONT_UI_GLYPH_W;
}

int gfx_glyph_height(void) {
    return FONT_UI_GLYPH_H;
}

void gfx_sprite(SDL_Renderer* renderer, GfxSprite sprite, int x, int y) {
    const int size = sprite_sizes[sprite];
    SDL_FRect dst = { (float)x, (float)y, (float)size, (float)size };

    if(sprite == GFX_SPRITE_SPIDER) {
        gfx_spider_draw(renderer, 0, 0.0, x, y);
        return;
    }
    SDL_RenderTexture(renderer, sprite_textures[sprite], NULL, &dst);
}

void gfx_spider_draw(SDL_Renderer* renderer, int frame, double angle,
                     int x, int y) {
    SDL_FRect dst = { (float)x, (float)y,
                      (float)SPIDER_SIZE, (float)SPIDER_SIZE };

    if(frame < 0) {
        frame = 0;
    }
    frame %= GFX_SPIDER_FRAMES;
    SDL_RenderTextureRotated(renderer, spider_textures[frame], NULL, &dst,
                             angle, NULL, SDL_FLIP_NONE);
}
