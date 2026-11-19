#ifndef XONIX_HIGHSCORE_H
#define XONIX_HIGHSCORE_H

#include <SDL3/SDL.h>
#include <stdint.h>

#include "input.h"

void highscore_init(void);

typedef enum {
    HS_ACTION_NONE = 0,
    HS_ACTION_SPLASH,
    HS_ACTION_QUIT
} HighscoreAction;

/* Start the end-of-game flow (name entry when the score qualifies). */
void highscore_begin(uint16_t score);
void highscore_update(const InputFrame* input);
void highscore_draw(SDL_Renderer* renderer);
bool highscore_done(void);

/* Records-screen popup state. The caller performs the selected action. */
/* True when the current end-of-game run made the top 10. */
bool highscore_qualified(void);

bool highscore_popup_open(void);
uint8_t highscore_popup_selection(void);
HighscoreAction highscore_take_action(void);

uint16_t highscore_best(void);

#endif /* XONIX_HIGHSCORE_H */
