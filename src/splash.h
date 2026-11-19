#ifndef XONIX_SPLASH_H
#define XONIX_SPLASH_H

#include <SDL3/SDL.h>
#include <stdbool.h>

/* Decode the embedded startup image and prepare the splash screen. */
bool splash_init(SDL_Renderer* renderer);
void splash_shutdown(void);

/* Advance the fades/timer. Returns true once the splash has finished. */
bool splash_update(Uint32 delta_ms, bool dismiss_requested);

void splash_draw(SDL_Renderer* renderer);

#endif /* XONIX_SPLASH_H */
