#ifndef XONIX_AUDIO_MENU_H
#define XONIX_AUDIO_MENU_H

#include <SDL3/SDL.h>

#include "input.h"

#define AUDIO_MENU_SFX    0
#define AUDIO_MENU_MUSIC  1

/* Open the overlay with the requested slider selected. */
void audio_menu_open(uint8_t slider);
bool audio_menu_is_open(void);

/* Call once per fixed step while the overlay owns the input. */
void audio_menu_update(const InputFrame* input);

/* Draw on top of the current screen, after SDL_RenderPresent's contents. */
void audio_menu_draw(SDL_Renderer* renderer);

#endif /* XONIX_AUDIO_MENU_H */
