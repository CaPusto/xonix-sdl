#include <stdio.h>
#include <string.h>

#include <stddef.h>

#include <SDL3/SDL.h>

#include "audio.h"
#include "audio_menu.h"
#include "config.h"
#include "game.h"
#include "gfx.h"
#include "highscore.h"
#include "input.h"
#include "levels.h"
#include "splash.h"

typedef enum {
    SCREEN_SPLASH = 0,
    SCREEN_TITLE,
    SCREEN_GAME,
    SCREEN_GAME_OVER,
    SCREEN_HIGHSCORE,
    SCREEN_DEMO
} Screen;

#ifndef TITLE_DEMO_DELAY_MS
#define TITLE_DEMO_DELAY_MS 12000u
#endif
#ifndef DEMO_DURATION_MS
#define DEMO_DURATION_MS    30000u
#endif

static Uint32 title_idle_ms;
static Uint32 demo_elapsed_ms;
static InputFrame demo_input;

static void draw_title(SDL_Renderer* renderer, uint8_t start_level,
                       Uint32 demo_delay_ms) {
    char text[64];
    const bool show_prompt = (SDL_GetTicks() / 500u) % 2u == 0;

    gfx_clear(renderer, GFX_COLOR_BG);
    gfx_text_centered(renderer, 90, "XONIX SDL", 3, GFX_COLOR_PLAYER);
    gfx_text_centered(renderer, 190, "CAPTURE 75 PERCENT OF THE SEA", 1,
                      GFX_COLOR_TEXT);
    gfx_text_centered(renderer, 270, "ARROWS OR WASD TO MOVE", 1,
                      GFX_COLOR_TEXT_DIM);
    gfx_text_centered(renderer, 300, "JOYSTICK OR ENTER TO START", 1,
                      GFX_COLOR_TEXT_DIM);

    if(show_prompt) {
        gfx_text_centered(renderer, 380, "! PRESS ANY KEY !", 1,
                          GFX_COLOR_TEXT);
    }

    SDL_snprintf(text, sizeof(text), "BEST %05u   START LEVEL %02u",
                 (unsigned)highscore_best(), (unsigned)start_level);
    gfx_text_centered(renderer, 450, text, 1, GFX_COLOR_TEXT_DIM);

    SDL_snprintf(text, sizeof(text), "DEMO IN %2u S",
                 (unsigned)((demo_delay_ms + 999u) / 1000u));
    gfx_text_centered(renderer, 500, text, 1, GFX_COLOR_TEXT_DIM);
}

static void draw_demo_overlay(SDL_Renderer* renderer) {
    gfx_rect_alpha(renderer, 0, SCREEN_H - 34, SCREEN_W, 34,
                   0, 0, 0, 180);
    gfx_text_centered(renderer, SCREEN_H - 27,
                      "DEMO - PRESS ANY KEY TO RETURN", 1,
                      GFX_COLOR_TEXT_DIM);
}

static void draw_game_over(SDL_Renderer* renderer, uint16_t score,
                           Uint64 entered_ticks) {
    const Uint32 elapsed = (Uint32)(SDL_GetTicks() - entered_ticks);
    const bool prompt_visible = (elapsed / 500u) % 2u == 0;
    char score_text[16];
    int i;

    gfx_clear(renderer, GFX_COLOR_BG);

    /* Dark red scanlines and a heavy frame make this screen unmistakably an
     * ending, while the sea/land colours stay out of the palette. */
    for(i = 0; i < SCREEN_H; i += 24) {
        gfx_rect_alpha(renderer, 0, i, SCREEN_W, 2, 92, 16, 18, 90);
    }
    gfx_rect_alpha(renderer, 0, 0, SCREEN_W, 10, 150, 28, 24, 255);
    gfx_rect_alpha(renderer, 0, SCREEN_H - 10, SCREEN_W, 10,
                   150, 28, 24, 255);
    gfx_rect(renderer, 122, 116, 716, 368, GFX_COLOR_BALL);
    gfx_rect(renderer, 128, 122, 704, 356, GFX_COLOR_BG);
    gfx_rect_alpha(renderer, 128, 122, 704, 356, 42, 6, 12, 230);

    gfx_text_centered(renderer, 176, "GAME OVER", 3, GFX_COLOR_BALL);
    gfx_rect(renderer, 250, 264, 460, 2, GFX_COLOR_TRAIL);
    gfx_text_centered(renderer, 286, "ALL LIVES LOST", 1,
                      GFX_COLOR_TEXT_DIM);
    gfx_text_centered(renderer, 340, "FINAL SCORE", 1,
                      GFX_COLOR_TEXT_DIM);
    SDL_snprintf(score_text, sizeof(score_text), "%05u", (unsigned)score);
    gfx_text_dseg_centered(renderer, 378, score_text, 3, GFX_COLOR_PLAYER);

    if(prompt_visible) {
        gfx_text_centered(renderer, 508, "PRESS ANY KEY", 1,
                          GFX_COLOR_TEXT);
    }
}

static uint8_t parse_start_level(int argc, char** argv) {
    const uint8_t level = level_parse_args(argc, argv);

    return level != 0 ? level : 1;
}

static void start_demo(void) {
    const Sint32 pick = SDL_rand(LEVEL_COUNT);

    game_init((uint8_t)(pick + 1));
    audio_set_music_track(MUSIC_TRACK_MAIN);
    game_demo_reset();
    demo_elapsed_ms = 0;
    SDL_memset(&demo_input, 0, sizeof(demo_input));
}

static void stop_demo(void) {
    game_shutdown();
    title_idle_ms = 0;
}

int main(int argc, char** argv) {
    const uint8_t start_level = parse_start_level(argc, argv);
    const bool skip_splash = level_arg_given(argc, argv);
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;
    Screen screen = SCREEN_TITLE;
    Uint64 last_ticks;
    Uint64 game_over_ticks = 0;
    uint16_t game_over_score = 0;
    Uint32 accumulator = 0;
    bool splash_dismiss = false;
    bool pending_any_key = false;
    bool running = true;

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD |
                 SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_srand(SDL_GetTicksNS());

    window = SDL_CreateWindow("Xonix SDL", SCREEN_W, SCREEN_H,
                              SDL_WINDOW_RESIZABLE);
    if(window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, NULL);
    if(renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderLogicalPresentation(renderer, SCREEN_W, SCREEN_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_HideCursor();

    if(!gfx_init(renderer)) {
        fprintf(stderr, "gfx_init failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if(skip_splash) {
        game_init(start_level);
        screen = SCREEN_GAME;
    } else if(splash_init(renderer)) {
        screen = SCREEN_SPLASH;
    } else {
        fprintf(stderr, "splash_init failed: %s\n", SDL_GetError());
    }

    audio_init();
    input_init(window);
    highscore_init();
    last_ticks = SDL_GetTicks();

    while(running) {
        SDL_Event event;
        InputFrame step_input;
        const InputFrame* input;
        Uint64 now;
        Uint32 delta;

        while(SDL_PollEvent(&event)) {
            if(event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if(event.type == SDL_EVENT_KEY_DOWN) {
                const bool menu_open = audio_menu_is_open();

                /* The AUDIO overlay owns the input: a key used to close it
                 * must not also dismiss the splash or start the game. */
                if(!menu_open) {
                    splash_dismiss = true;
                    pending_any_key = true;
                }
                if(event.key.scancode == SDL_SCANCODE_F11) {
                    const bool fullscreen =
                        (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(window,
                                            fullscreen ? (SDL_WindowFlags)0
                                                       : SDL_WINDOW_FULLSCREEN);
                } else if(event.key.scancode == SDL_SCANCODE_ESCAPE &&
                          screen == SCREEN_TITLE && !menu_open) {
                    running = false;
                } else {
                    input_handle_event(&event);
                }
            } else if(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                      event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
                if(!audio_menu_is_open()) {
                    splash_dismiss = true;
                    pending_any_key = true;
                }
                input_handle_event(&event);
            } else if(event.type == SDL_EVENT_TEXT_INPUT) {
                /* Alt+S / Alt+M can produce text on some layouts. The toggles
                 * are handled from the keyboard state, so ignore that text. */
                if((SDL_GetModState() & SDL_KMOD_ALT) == 0) {
                    input_handle_event(&event);
                }
            } else {
                input_handle_event(&event);
            }
        }
        if(!running) {
            break;
        }

        input_update();
        input = input_frame();
        if(input->audio_menu_sfx) {
            audio_menu_open(AUDIO_MENU_SFX);
        }
        if(input->audio_menu_music) {
            audio_menu_open(AUDIO_MENU_MUSIC);
        }
        if(input->mute) {
            audio_toggle_mute();
        }

        now = SDL_GetTicks();
        delta = (Uint32)(now - last_ticks);
        last_ticks = now;
        /* Keep a slow renderer from making attract-mode time lag wall time.
         * The splash fade still uses the old 100 ms cap so it cannot skip. */
        if(delta > 1000u) {
            delta = 1000u;
        }

        if(screen == SCREEN_SPLASH) {
            accumulator = 0;
            if(splash_update(delta > 100u ? 100u : delta, splash_dismiss)) {
                screen = SCREEN_TITLE;
                splash_dismiss = false;
                title_idle_ms = 0;
                /* No input_flush() here: the fade-out consumes the dismiss
                 * edge. Flushing would recreate it for a key/button that is
                 * still held and start the game from the title screen. */
            }
        } else {
            accumulator += delta;
        }

        step_input = *input;
        step_input.any_key = (uint8_t)(pending_any_key || input->any);
        while(accumulator >= FIXED_STEP_MS) {
            accumulator -= FIXED_STEP_MS;

            /* The overlay owns the input (and freezes the game) while open. */
            if(audio_menu_is_open()) {
                audio_menu_update(&step_input);
            } else
            switch(screen) {
                case SCREEN_TITLE:
                    if(step_input.any) {
                        game_init(start_level);
                        audio_set_music_track(MUSIC_TRACK_MAIN);
                        screen = SCREEN_GAME;
                        title_idle_ms = 0;
                    } else if(step_input.any_key) {
                        /* Unmapped keys still postpone the attract demo. */
                        title_idle_ms = 0;
                    } else {
                        title_idle_ms += FIXED_STEP_MS;
                        if(title_idle_ms >= TITLE_DEMO_DELAY_MS) {
                            start_demo();
                            screen = SCREEN_DEMO;
                        }
                    }
                    break;

                case SCREEN_GAME:
                    if(game_update(&step_input) == GAME_EVENT_GAME_OVER) {
                        game_over_score = game_score();
                        game_shutdown();
                        game_over_ticks = SDL_GetTicks();
                        screen = SCREEN_GAME_OVER;
                    }
                    break;

                case SCREEN_GAME_OVER:
                    if(step_input.any_key || step_input.any) {
                        highscore_begin(game_over_score);
                        audio_set_music_track(highscore_qualified()
                                              ? MUSIC_TRACK_HISCORE
                                              : MUSIC_TRACK_NONE);
                        screen = SCREEN_HIGHSCORE;
                    }
                    break;

                case SCREEN_HIGHSCORE: {
                    HighscoreAction action;

                    highscore_update(&step_input);
                    action = highscore_take_action();
                    if(action == HS_ACTION_SPLASH) {
                        audio_set_music_track(MUSIC_TRACK_MAIN);
                        if(splash_init(renderer)) {
                            screen = SCREEN_SPLASH;
                            splash_dismiss = false;
                            pending_any_key = false;
                        } else {
                            screen = SCREEN_TITLE;
                            title_idle_ms = 0;
                        }
                    } else if(action == HS_ACTION_QUIT) {
                        running = false;
                    }
                    break;
                }

                case SCREEN_DEMO:
                    if(step_input.any_key || step_input.any) {
                        stop_demo();
                        screen = SCREEN_TITLE;
                        break;
                    }
                    demo_elapsed_ms += FIXED_STEP_MS;
                    if(demo_elapsed_ms >= DEMO_DURATION_MS) {
                        stop_demo();
                        screen = SCREEN_TITLE;
                        break;
                    }
                    game_demo_input(&demo_input);
                    if(game_update(&demo_input) == GAME_EVENT_GAME_OVER) {
                        stop_demo();
                        screen = SCREEN_TITLE;
                    }
                    break;

                default:
                    break;
            }

            /* Only the first fixed step of a frame may consume input edges. */
            step_input.dir_edge = 0;
            step_input.confirm = 0;
            step_input.start = 0;
            step_input.back = 0;
            step_input.mute = 0;
            step_input.audio_menu_sfx = 0;
            step_input.audio_menu_music = 0;
            step_input.any = 0;
            step_input.any_key = 0;
            step_input.text_len = 0;
            /* The event is consumed only when a fixed step actually saw it. */
            pending_any_key = false;
            if(!running) {
                break;
            }
        }
        if(!running) {
            break;
        }

        switch(screen) {
            case SCREEN_SPLASH:
                splash_draw(renderer);
                break;
            case SCREEN_TITLE:
                draw_title(renderer, start_level,
                           TITLE_DEMO_DELAY_MS > title_idle_ms
                               ? TITLE_DEMO_DELAY_MS - title_idle_ms
                               : 0);
                break;
            case SCREEN_GAME:
                game_draw(renderer);
                break;
            case SCREEN_DEMO:
                game_draw(renderer);
                draw_demo_overlay(renderer);
                break;
            case SCREEN_GAME_OVER:
                draw_game_over(renderer, game_over_score, game_over_ticks);
                break;
            case SCREEN_HIGHSCORE:
                highscore_draw(renderer);
                break;
            default:
                break;
        }
        audio_menu_draw(renderer);
        SDL_RenderPresent(renderer);
    }

    game_shutdown();
    splash_shutdown();
    input_shutdown();
    audio_shutdown();
    gfx_shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
