#ifndef XONIX_INPUT_H
#define XONIX_INPUT_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define DIR_NONE        0
#define DIR_LEFT        1
#define DIR_RIGHT       2
#define DIR_UP          3
#define DIR_DOWN        4

#define INPUT_TEXT_MAX  16

typedef struct {
    uint8_t dir;        /* currently held direction */
    uint8_t dir_edge;   /* direction pressed since the previous frame */
    uint8_t confirm;    /* primary action pressed this frame (A / Space) */
    uint8_t start;      /* start/accept pressed this frame (Start / Enter) */
    uint8_t back;       /* back/cancel pressed this frame */
    uint8_t any;        /* any game action pressed this frame */
    uint8_t any_key;    /* any key/button down event this frame (filled by main) */
    uint8_t mute;       /* mute toggle pressed this frame */
    uint8_t audio_menu_sfx;   /* Alt+S: open the SFX slider */
    uint8_t audio_menu_music; /* Alt+M: open the music slider */
    uint8_t text_len;   /* bytes in text (UTF-8 from SDL text input) */
    char text[INPUT_TEXT_MAX];
} InputFrame;

/* The window is needed to start/stop SDL text input. */
void input_init(SDL_Window* window);
void input_shutdown(void);

/* Enable SDL text input while the player types a highscore name. While it is
 * active, letter keys feed the text buffer instead of game actions. */
void input_set_text_input(bool enabled);

/* Forward SDL events for hot-plug and text input. */
void input_handle_event(const SDL_Event* event);

/* Sample keyboard + controllers once per frame after all events are read. */
void input_update(void);
const InputFrame* input_frame(void);

/* Forget held keys/buttons and pending text (used after blocking screens). */
void input_flush(void);

#endif /* XONIX_INPUT_H */
