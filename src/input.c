#include <string.h>

#include "input.h"

#define MAX_GAMEPADS 4
#define MAX_JOYSTICKS 4
#define AXIS_DEADZONE 8000

static SDL_Gamepad* gamepads[MAX_GAMEPADS];
static SDL_Joystick* joysticks[MAX_JOYSTICKS];
static int gamepad_count;
static int joystick_count;

static SDL_Window* text_window;
static uint8_t text_input_active;

static InputFrame current;
static char text_accum[INPUT_TEXT_MAX];
static uint8_t text_accum_len;

static uint8_t prev_confirm;
static uint8_t prev_start;
static uint8_t prev_back;
static uint8_t prev_mute;
static uint8_t prev_alt_s;
static uint8_t prev_alt_m;

static void open_gamepad(SDL_JoystickID id) {
    int i;

    if(gamepad_count >= MAX_GAMEPADS) {
        return;
    }
    for(i = 0; i < gamepad_count; i++) {
        if(SDL_GetGamepadID(gamepads[i]) == id) {
            return;
        }
    }
    gamepads[gamepad_count] = SDL_OpenGamepad(id);
    if(gamepads[gamepad_count] != NULL) {
        gamepad_count++;
    }
}

static void open_joystick(SDL_JoystickID id) {
    int i;

    if(joystick_count >= MAX_JOYSTICKS || SDL_IsGamepad(id)) {
        return;
    }
    for(i = 0; i < joystick_count; i++) {
        if(SDL_GetJoystickID(joysticks[i]) == id) {
            return;
        }
    }
    joysticks[joystick_count] = SDL_OpenJoystick(id);
    if(joysticks[joystick_count] != NULL) {
        joystick_count++;
    }
}

void input_init(SDL_Window* window) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    int i;

    text_window = window;
    text_input_active = 0;
    gamepad_count = 0;
    joystick_count = 0;
    memset(&current, 0, sizeof(current));
    memset(text_accum, 0, sizeof(text_accum));
    text_accum_len = 0;

    if(ids != NULL) {
        for(i = 0; i < count; i++) {
            open_gamepad(ids[i]);
        }
        SDL_free(ids);
    }

    ids = SDL_GetJoysticks(&count);
    if(ids != NULL) {
        for(i = 0; i < count; i++) {
            open_joystick(ids[i]);
        }
        SDL_free(ids);
    }
}

void input_shutdown(void) {
    int i;

    input_set_text_input(false);
    text_window = NULL;

    for(i = 0; i < gamepad_count; i++) {
        SDL_CloseGamepad(gamepads[i]);
    }
    for(i = 0; i < joystick_count; i++) {
        SDL_CloseJoystick(joysticks[i]);
    }
    gamepad_count = 0;
    joystick_count = 0;
}

void input_set_text_input(bool enabled) {
    if(enabled) {
        if(text_input_active || text_window == NULL) {
            return;
        }
        if(!SDL_StartTextInput(text_window)) {
            return;
        }
        text_input_active = 1;
    } else {
        if(!text_input_active) {
            return;
        }
        SDL_StopTextInput(text_window);
        text_input_active = 0;
    }

    /* Drop text queued in the previous mode so it cannot leak into the next
     * screen (e.g. a name character arriving after the table is shown). */
    memset(text_accum, 0, sizeof(text_accum));
    text_accum_len = 0;
}

void input_handle_event(const SDL_Event* event) {
    if(event == NULL) {
        return;
    }

    switch(event->type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            open_gamepad(event->gdevice.which);
            break;
        case SDL_EVENT_JOYSTICK_ADDED:
            open_joystick(event->jdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED: {
            int i;

            for(i = 0; i < gamepad_count; i++) {
                if(SDL_GetGamepadID(gamepads[i]) == event->gdevice.which) {
                    SDL_CloseGamepad(gamepads[i]);
                    gamepads[i] = gamepads[gamepad_count - 1];
                    gamepad_count--;
                    break;
                }
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_REMOVED: {
            int i;

            for(i = 0; i < joystick_count; i++) {
                if(SDL_GetJoystickID(joysticks[i]) == event->jdevice.which) {
                    SDL_CloseJoystick(joysticks[i]);
                    joysticks[i] = joysticks[joystick_count - 1];
                    joystick_count--;
                    break;
                }
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            const char* text = event->text.text;

            if(text != NULL) {
                while(*text && text_accum_len + 1 < INPUT_TEXT_MAX) {
                    /* Space is the picker's confirm key. Keep SDL from adding
                     * a second character on top of the selected one. */
                    if(text_input_active && *text == ' ') {
                        text++;
                        continue;
                    }
                    text_accum[text_accum_len++] = *text++;
                }
            }
            break;
        }
        default:
            break;
    }
}

static uint8_t direction_from_axes(float x, float y) {
    if(x < -0.45f) return DIR_LEFT;
    if(x >  0.45f) return DIR_RIGHT;
    if(y < -0.45f) return DIR_UP;
    if(y >  0.45f) return DIR_DOWN;
    return DIR_NONE;
}

static uint8_t merge_direction(uint8_t a, uint8_t b) {
    return a != DIR_NONE ? a : b;
}

void input_update(void) {
    const bool* keys = SDL_GetKeyboardState(NULL);
    uint8_t dir = DIR_NONE;
    uint8_t confirm = 0;
    uint8_t start = 0;
    uint8_t back = 0;
    uint8_t mute = 0;
    uint8_t alt_s = 0;
    uint8_t alt_m = 0;
    bool alt_held = false;
    int i;

    if(keys != NULL) {
        const bool letters_blocked = (bool)text_input_active;

        alt_held = (keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT]);

        /* Letters are typed text while a highscore name is being entered, and
         * audio toggles while Alt is held. Arrow keys always keep working. */
        if(keys[SDL_SCANCODE_LEFT] ||
           (!letters_blocked && !alt_held && keys[SDL_SCANCODE_A])) {
            dir = DIR_LEFT;
        } else if(keys[SDL_SCANCODE_RIGHT] ||
                  (!letters_blocked && !alt_held && keys[SDL_SCANCODE_D])) {
            dir = DIR_RIGHT;
        } else if(keys[SDL_SCANCODE_UP] ||
                  (!letters_blocked && !alt_held && keys[SDL_SCANCODE_W])) {
            dir = DIR_UP;
        } else if(keys[SDL_SCANCODE_DOWN] ||
                  (!letters_blocked && !alt_held && keys[SDL_SCANCODE_S])) {
            dir = DIR_DOWN;
        }

        confirm = (uint8_t)keys[SDL_SCANCODE_SPACE];
        start = (uint8_t)(keys[SDL_SCANCODE_RETURN] ||
                          keys[SDL_SCANCODE_KP_ENTER]);
        back = (uint8_t)(keys[SDL_SCANCODE_ESCAPE] ||
                         keys[SDL_SCANCODE_BACKSPACE]);
        mute = (uint8_t)(!letters_blocked && !alt_held &&
                         keys[SDL_SCANCODE_M]);

        if(alt_held) {
            alt_s = (uint8_t)keys[SDL_SCANCODE_S];
            alt_m = (uint8_t)keys[SDL_SCANCODE_M];
        }
    }

    for(i = 0; i < gamepad_count; i++) {
        SDL_Gamepad* pad = gamepads[i];
        Sint16 ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        Sint16 ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        uint8_t d = DIR_NONE;

        if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            d = DIR_LEFT;
        } else if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            d = DIR_RIGHT;
        } else if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
            d = DIR_UP;
        } else if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
            d = DIR_DOWN;
        } else {
            d = direction_from_axes((float)ax / 32768.0f,
                                    (float)ay / 32768.0f);
        }

        dir = merge_direction(dir, d);
        if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) {
            confirm = 1;
        }
        if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START)) {
            start = 1;
        }
        if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) ||
           SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK)) {
            back = 1;
        }
        if(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH)) {
            mute = 1;
        }
    }

    /* Raw joysticks without a gamepad mapping (hats, axes 0/1, buttons). */
    for(i = 0; i < joystick_count; i++) {
        SDL_Joystick* joy = joysticks[i];
        Sint16 ax = SDL_GetJoystickAxis(joy, 0);
        Sint16 ay = SDL_GetJoystickAxis(joy, 1);
        Uint8 hat = SDL_GetJoystickHat(joy, 0);
        uint8_t d = DIR_NONE;

        if(ax < -AXIS_DEADZONE)      d = DIR_LEFT;
        else if(ax > AXIS_DEADZONE)  d = DIR_RIGHT;
        else if(ay < -AXIS_DEADZONE) d = DIR_UP;
        else if(ay > AXIS_DEADZONE)  d = DIR_DOWN;

        if(hat & SDL_HAT_LEFT)       d = DIR_LEFT;
        else if(hat & SDL_HAT_RIGHT) d = DIR_RIGHT;
        else if(hat & SDL_HAT_UP)    d = DIR_UP;
        else if(hat & SDL_HAT_DOWN)  d = DIR_DOWN;

        dir = merge_direction(dir, d);
        if(SDL_GetJoystickButton(joy, 0)) {
            confirm = 1;
        }
        if(SDL_GetJoystickButton(joy, 9)) {
            start = 1;
        }
        if(SDL_GetJoystickButton(joy, 1)) {
            back = 1;
        }
    }

    current.dir_edge = (uint8_t)((dir & ~current.dir) |
                                 ((confirm & ~prev_confirm) ? dir : 0));
    current.dir = dir;

    current.confirm = (uint8_t)(confirm && !prev_confirm);
    current.start = (uint8_t)(start && !prev_start);
    current.back = (uint8_t)(back && !prev_back);
    current.mute = (uint8_t)(mute && !prev_mute);
    current.audio_menu_sfx = (uint8_t)(alt_s && !prev_alt_s);
    current.audio_menu_music = (uint8_t)(alt_m && !prev_alt_m);
    current.any = (uint8_t)(current.confirm || current.start || current.back ||
                            current.dir_edge != 0 || text_accum_len != 0);

    prev_confirm = confirm;
    prev_start = start;
    prev_back = back;
    prev_mute = mute;
    prev_alt_s = alt_s;
    prev_alt_m = alt_m;

    current.text_len = text_accum_len;
    memcpy(current.text, text_accum, text_accum_len);
    text_accum_len = 0;
    memset(text_accum, 0, sizeof(text_accum));
}

const InputFrame* input_frame(void) {
    return &current;
}

void input_flush(void) {
    memset(&current, 0, sizeof(current));
    memset(text_accum, 0, sizeof(text_accum));
    text_accum_len = 0;
    prev_confirm = 0;
    prev_start = 0;
    prev_back = 0;
    prev_mute = 0;
    prev_alt_s = 0;
    prev_alt_m = 0;
}
