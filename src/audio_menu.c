#include <stdio.h>

#include "audio.h"
#include "audio_menu.h"
#include "config.h"
#include "gfx.h"

/* Every glyph is FONT_UI_GLYPH_W (22) px wide at scale 1, so the hints have
 * to be short and split into lines: one long line would not fit even on the
 * whole 960 px screen. All coordinates below are relative to the panel. */
#define PANEL_W     640
#define PANEL_H     300
#define LABEL_X     28
#define BAR_X       170
#define BAR_W       300
#define BAR_H       22
#define VALUE_X     492
#define TITLE_Y     18
#define ROW_Y       78
#define ROW_STEP    58
#define HINT_Y      200
#define HINT_STEP   28

static bool menu_open;
static uint8_t selected;
static uint16_t repeat_delay;

static uint8_t current_volume(uint8_t slider) {
    return slider == AUDIO_MENU_SFX ? audio_sfx_volume()
                                    : audio_music_volume();
}

static void adjust_volume(int delta) {
    const int value = (int)current_volume(selected) + delta;

    if(value < 0) {
        if(selected == AUDIO_MENU_SFX) audio_set_sfx_volume(0);
        else audio_set_music_volume(0);
    } else if(value > 100) {
        if(selected == AUDIO_MENU_SFX) audio_set_sfx_volume(100);
        else audio_set_music_volume(100);
    } else if(selected == AUDIO_MENU_SFX) {
        audio_set_sfx_volume((uint8_t)value);
    } else {
        audio_set_music_volume((uint8_t)value);
    }
}

void audio_menu_open(uint8_t slider) {
    menu_open = true;
    selected = slider > AUDIO_MENU_MUSIC ? AUDIO_MENU_SFX : slider;
    repeat_delay = 0;
}

bool audio_menu_is_open(void) {
    return menu_open;
}

void audio_menu_update(const InputFrame* input) {
    if(!menu_open || input == NULL) {
        return;
    }

    if(input->dir_edge == DIR_UP || input->dir_edge == DIR_DOWN) {
        selected ^= 1;
        repeat_delay = 0;
    }

    if(input->dir == DIR_LEFT || input->dir == DIR_RIGHT) {
        const int step = input->dir == DIR_RIGHT ? 2 : -2;

        /* One step on the key edge, then auto-repeat for fast scanning. */
        if(input->dir_edge == input->dir) {
            adjust_volume(step);
            repeat_delay = 12;
        } else if(repeat_delay != 0) {
            repeat_delay--;
        } else {
            adjust_volume(step);
            repeat_delay = 3;
        }
    } else {
        repeat_delay = 0;
    }

    if(input->back || input->confirm || input->start) {
        menu_open = false;
    }
}

static void draw_panel_text(SDL_Renderer* renderer, int px, int y,
                            const char* text, int scale, GfxColor color) {
    const int width = gfx_text_width(text, scale);

    gfx_text(renderer, px + (PANEL_W - width) / 2, y, text, scale, color);
}

void audio_menu_draw(SDL_Renderer* renderer) {
    const int px = (SCREEN_W - PANEL_W) / 2;
    const int py = (SCREEN_H - PANEL_H) / 2;
    char text[16];
    int row;

    if(!menu_open) {
        return;
    }

    gfx_rect_alpha(renderer, 0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 170);
    gfx_rect(renderer, px, py, PANEL_W, PANEL_H, GFX_COLOR_BORDER);
    gfx_rect(renderer, px + 4, py + 4, PANEL_W - 8, PANEL_H - 8,
             GFX_COLOR_BG);

    draw_panel_text(renderer, px, py + TITLE_Y, "AUDIO", 2,
                    GFX_COLOR_PLAYER);

    for(row = 0; row <= AUDIO_MENU_MUSIC; row++) {
        const uint8_t value = current_volume((uint8_t)row);
        const int y = py + ROW_Y + row * ROW_STEP;
        const GfxColor color = row == selected ? GFX_COLOR_PLAYER
                                               : GFX_COLOR_TEXT_DIM;

        gfx_text(renderer, px + LABEL_X, y,
                 row == AUDIO_MENU_SFX ? "SFX" : "MUSIC", 1, color);

        gfx_rect(renderer, px + BAR_X, y + 2, BAR_W, BAR_H,
                 GFX_COLOR_TEXT_DIM);
        if(value != 0) {
            gfx_rect(renderer, px + BAR_X + 2, y + 4,
                     (BAR_W - 4) * value / 100, BAR_H - 4,
                     row == selected ? GFX_COLOR_TRAIL : GFX_COLOR_LAND);
        }
        SDL_snprintf(text, sizeof(text), "%3u%%", (unsigned)value);
        gfx_text(renderer, px + VALUE_X, y, text, 1, color);
    }

    draw_panel_text(renderer, px, py + HINT_Y, "LEFT/RIGHT ADJUST VOLUME", 1,
                    GFX_COLOR_TEXT_DIM);
    draw_panel_text(renderer, px, py + HINT_Y + HINT_STEP,
                    "UP/DOWN SELECT SLIDER", 1, GFX_COLOR_TEXT_DIM);
    draw_panel_text(renderer, px, py + HINT_Y + 2 * HINT_STEP,
                    "ENTER OR ESC CLOSE", 1, GFX_COLOR_TEXT_DIM);
}
