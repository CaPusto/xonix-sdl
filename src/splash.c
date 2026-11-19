/* GCC reports a false positive in unused 16-bit PNG transparency code. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "config.h"
#include "splash.h"
#include "splash_image.h"

#define SPLASH_FADE_IN_MS   1000u
#define SPLASH_FADE_OUT_MS  800u
#define SPLASH_TOTAL_MS     25000u

typedef enum {
    SPLASH_PHASE_FADE_IN = 0,
    SPLASH_PHASE_HOLD,
    SPLASH_PHASE_FADE_OUT,
    SPLASH_PHASE_DONE
} SplashPhase;

static SDL_Texture* splash_texture;
static SplashPhase phase = SPLASH_PHASE_DONE;
static Uint32 elapsed_ms;
static Uint32 phase_ms;
static Uint8 alpha;
static Uint8 fade_from_alpha;

static Uint8 fade_alpha(Uint8 from, Uint8 to, Uint32 ms, Uint32 duration) {
    Uint32 t;

    if(ms >= duration) {
        return to;
    }
    t = (ms * 256u) / duration;
    return (Uint8)(from + ((int)to - (int)from) * (int)t / 256);
}

static void start_fade_out(void) {
    fade_from_alpha = alpha;
    phase_ms = 0;
    phase = SPLASH_PHASE_FADE_OUT;
}

bool splash_init(SDL_Renderer* renderer) {
    int width;
    int height;
    int channels;
    unsigned char* pixels;
    SDL_Surface* surface;

    splash_shutdown();

    pixels = stbi_load_from_memory(splash_png, SPLASH_PNG_LEN, &width,
                                   &height, &channels, 4);
    if(pixels == NULL) {
        return false;
    }

    /* stb_image returns bytes in R,G,B,A order. SDL_PIXELFORMAT_RGBA32 is the
     * alias for exactly that byte layout on the current platform (unlike
     * RGBA8888, whose in-memory order differs on little-endian systems). */
    surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                    pixels, width * 4);
    if(surface == NULL) {
        stbi_image_free(pixels);
        return false;
    }

    splash_texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    stbi_image_free(pixels);
    if(splash_texture == NULL) {
        return false;
    }

    SDL_SetTextureBlendMode(splash_texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(splash_texture, SDL_SCALEMODE_NEAREST);

    phase = SPLASH_PHASE_FADE_IN;
    elapsed_ms = 0;
    phase_ms = 0;
    alpha = 0;
    fade_from_alpha = 0;
    return true;
}

void splash_shutdown(void) {
    SDL_DestroyTexture(splash_texture);
    splash_texture = NULL;
    phase = SPLASH_PHASE_DONE;
    elapsed_ms = 0;
    phase_ms = 0;
    alpha = 0;
    fade_from_alpha = 0;
}

bool splash_update(Uint32 delta_ms, bool dismiss_requested) {
    if(splash_texture == NULL || phase == SPLASH_PHASE_DONE) {
        return true;
    }

    elapsed_ms += delta_ms;
    if(dismiss_requested && phase != SPLASH_PHASE_FADE_OUT) {
        start_fade_out();
    }

    switch(phase) {
        case SPLASH_PHASE_FADE_IN:
            phase_ms += delta_ms;
            if(phase_ms >= SPLASH_FADE_IN_MS) {
                alpha = 255;
                phase = SPLASH_PHASE_HOLD;
                phase_ms = 0;
            } else {
                alpha = fade_alpha(0, 255, phase_ms, SPLASH_FADE_IN_MS);
            }
            if(elapsed_ms >= SPLASH_TOTAL_MS) {
                start_fade_out();
            }
            break;

        case SPLASH_PHASE_HOLD:
            phase_ms += delta_ms;
            if(elapsed_ms >= SPLASH_TOTAL_MS) {
                start_fade_out();
            }
            break;

        case SPLASH_PHASE_FADE_OUT:
            phase_ms += delta_ms;
            alpha = fade_alpha(fade_from_alpha, 0, phase_ms,
                               SPLASH_FADE_OUT_MS);
            if(phase_ms >= SPLASH_FADE_OUT_MS) {
                alpha = 0;
                phase = SPLASH_PHASE_DONE;
                return true;
            }
            break;

        case SPLASH_PHASE_DONE:
        default:
            return true;
    }

    return false;
}

void splash_draw(SDL_Renderer* renderer) {
    SDL_FRect dst = { 0.0f, 0.0f, (float)SCREEN_W, (float)SCREEN_H };

    if(splash_texture == NULL || phase == SPLASH_PHASE_DONE) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetTextureAlphaMod(splash_texture, alpha);
    SDL_RenderTexture(renderer, splash_texture, NULL, &dst);
    SDL_SetTextureAlphaMod(splash_texture, 255);
}
