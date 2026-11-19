#ifndef XONIX_GAME_H
#define XONIX_GAME_H

#include <SDL3/SDL.h>
#include <stdint.h>

#include "input.h"

#define GAME_EVENT_NONE         0
#define GAME_EVENT_GAME_OVER    1

void game_init(uint8_t start_level);
/* Returns GAME_EVENT_* when the caller has to change screen. */
uint8_t game_update(const InputFrame* input);
uint16_t game_score(void);
void game_draw(SDL_Renderer* renderer);
void game_shutdown(void);

/* Attract-mode controller. Call game_init() first, then game_demo_reset().
 * game_demo_input() fills an InputFrame for game_update(). */
void game_demo_reset(void);
void game_demo_input(InputFrame* input);

#endif /* XONIX_GAME_H */
