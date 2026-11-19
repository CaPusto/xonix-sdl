#include <string.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "highscore.h"

#define HS_MAX          10
#define HS_NAME_LEN     8
#define HS_PAYLOAD_SIZE (1 + HS_MAX * (HS_NAME_LEN + 2))
#define HS_FILE_SIZE    (4 + HS_PAYLOAD_SIZE + 2)

#define HS_MAGIC        "XHS1"
#define HS_CHARSET      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
#define HS_WAIT_FRAMES  30

typedef struct {
    char name[HS_NAME_LEN + 1];
    uint16_t score;
} hs_entry_t;

typedef enum {
    HS_PHASE_NONE = 0,
    HS_PHASE_NAME,
    HS_PHASE_TABLE,
    HS_PHASE_DONE
} hs_phase_t;

static hs_entry_t table[HS_MAX];
static uint8_t table_count;
static hs_phase_t phase;
static uint8_t wait_frames;
static char name[HS_NAME_LEN + 1];
static uint8_t name_len;
static uint8_t picker;
static int highlight;
static uint16_t final_score;
static bool popup_open;
static uint8_t popup_selection;
static HighscoreAction pending_action;

static const char* const popup_items[2] = {
    "GO TO SPLASH SCREEN",
    "QUIT"
};

/* No organisation directory: Windows -> %APPDATA%\xonix-sdl\,
 * Linux -> $XDG_DATA_HOME/xonix-sdl/, macOS -> ~/Library/Application Support/xonix-sdl/. */
static char* score_path(void) {
    static char path[512];
    char* pref = SDL_GetPrefPath(NULL, "xonix-sdl");

    if(pref != NULL) {
        SDL_snprintf(path, sizeof(path), "%sHISCORE.DAT", pref);
        SDL_free(pref);
    } else {
        SDL_strlcpy(path, "HISCORE.DAT", sizeof(path));
    }
    return path;
}

/* SDL file I/O accepts UTF-8 paths on Windows; the CRT fopen() does not always
 * do so when the user profile contains non-ASCII characters. */
static SDL_IOStream* open_score_file(const char* mode) {
    SDL_IOStream* io = SDL_IOFromFile(score_path(), mode);

    if(io != NULL) {
        return io;
    }
    {
        /* SDL_GetBasePath() returns const only to discourage modification;
         * the caller still owns and must free the returned allocation. */
        char* base = (char*)SDL_GetBasePath();

        if(base != NULL) {
            char path[512];

            SDL_snprintf(path, sizeof(path), "%sHISCORE.DAT", base);
            SDL_free(base);
            io = SDL_IOFromFile(path, mode);
            if(io != NULL) {
                return io;
            }
        }
    }
    /* A missing file on first launch is normal, so only report write errors. */
    if(SDL_strcmp(mode, "rb") != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot open highscore file: %s", SDL_GetError());
    }
    return NULL;
}

static void xor_payload(uint8_t* file) {
    int i;

    for(i = 0; i < HS_PAYLOAD_SIZE; i++) {
        file[4 + i] ^= (uint8_t)(0xA5u ^ i);
    }
}

static uint16_t checksum(const uint8_t* payload) {
    uint16_t sum = 0x1357;
    int i;

    for(i = 0; i < HS_PAYLOAD_SIZE; i++) {
        sum = (uint16_t)(sum + payload[i]);
        sum = (uint16_t)((sum << 3) | (sum >> 13));
        sum ^= 0x5A5Au;
    }
    return sum;
}

static void decode_table(const uint8_t* payload) {
    int i;

    table_count = payload[0];
    if(table_count > HS_MAX) {
        table_count = 0;
        return;
    }
    memset(table, 0, sizeof(table));
    for(i = 0; i < table_count; i++) {
        const uint8_t* entry = &payload[1 + i * (HS_NAME_LEN + 2)];
        int j;

        for(j = 0; j < HS_NAME_LEN; j++) {
            table[i].name[j] = (char)entry[j];
        }
        table[i].name[HS_NAME_LEN] = '\0';
        table[i].score = (uint16_t)(entry[HS_NAME_LEN] |
                                    ((uint16_t)entry[HS_NAME_LEN + 1] << 8));
    }
}

void highscore_init(void) {
    uint8_t file[HS_FILE_SIZE];
    SDL_IOStream* io;

    memset(table, 0, sizeof(table));
    table_count = 0;
    phase = HS_PHASE_NONE;
    popup_open = false;
    popup_selection = 0;
    pending_action = HS_ACTION_NONE;

    io = open_score_file("rb");
    if(io == NULL) {
        return;
    }
    if(SDL_ReadIO(io, file, sizeof(file)) == sizeof(file) &&
       memcmp(file, HS_MAGIC, 4) == 0) {
        uint16_t stored;

        xor_payload(file);
        stored = (uint16_t)(file[4 + HS_PAYLOAD_SIZE] |
                            ((uint16_t)file[5 + HS_PAYLOAD_SIZE] << 8));
        if(stored == checksum(&file[4])) {
            decode_table(&file[4]);
        }
    }
    SDL_CloseIO(io);
}

static void save_table(void) {
    uint8_t file[HS_FILE_SIZE];
    uint16_t sum;
    SDL_IOStream* io;
    int i;

    memset(file, 0, sizeof(file));
    memcpy(file, HS_MAGIC, 4);
    file[4] = table_count;
    for(i = 0; i < table_count; i++) {
        uint8_t* entry = &file[5 + i * (HS_NAME_LEN + 2)];
        int j;

        for(j = 0; j < HS_NAME_LEN; j++) {
            entry[j] = (uint8_t)table[i].name[j];
        }
        entry[HS_NAME_LEN] = (uint8_t)(table[i].score & 0xFFu);
        entry[HS_NAME_LEN + 1] = (uint8_t)(table[i].score >> 8);
    }

    sum = checksum(&file[4]);
    file[4 + HS_PAYLOAD_SIZE] = (uint8_t)(sum & 0xFFu);
    file[5 + HS_PAYLOAD_SIZE] = (uint8_t)(sum >> 8);
    xor_payload(file);

    io = open_score_file("wb");
    if(io == NULL) {
        return;
    }
    if(SDL_WriteIO(io, file, sizeof(file)) != sizeof(file)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot write highscore file: %s", SDL_GetError());
    }
    SDL_CloseIO(io);
}

uint16_t highscore_best(void) {
    return table_count != 0 ? table[0].score : 0;
}

bool highscore_qualified(void) {
    return (bool)(phase == HS_PHASE_NAME || highlight >= 0);
}

static bool qualifies(uint16_t score) {
    if(score == 0) {
        return false;
    }
    if(table_count < HS_MAX) {
        return true;
    }
    return score > table[HS_MAX - 1].score;
}

static int insert_score(const char* who, uint16_t score) {
    int pos;

    if(table_count < HS_MAX) {
        pos = table_count++;
    } else {
        pos = HS_MAX - 1;
    }
    while(pos > 0 && table[pos - 1].score < score) {
        table[pos] = table[pos - 1];
        pos--;
    }
    SDL_snprintf(table[pos].name, sizeof(table[pos].name), "%s", who);
    table[pos].score = score;
    return pos;
}

void highscore_begin(uint16_t score) {
    final_score = score;
    highlight = -1;
    name_len = 0;
    picker = 0;
    name[0] = '\0';
    wait_frames = HS_WAIT_FRAMES;
    popup_open = false;
    popup_selection = 0;
    pending_action = HS_ACTION_NONE;
    input_set_text_input(false);
    input_flush();

    if(qualifies(score)) {
        phase = HS_PHASE_NAME;
    } else {
        phase = HS_PHASE_TABLE;
    }
}

static void append_char(char c) {
    if(c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if(((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') &&
       name_len < HS_NAME_LEN) {
        name[name_len++] = c;
        name[name_len] = '\0';
    }
}

void highscore_update(const InputFrame* input) {
    if(phase == HS_PHASE_NONE || input == NULL) {
        return;
    }
    if(wait_frames != 0) {
        wait_frames--;
        return;
    }

    if(phase == HS_PHASE_NAME) {
        int i;

        /* Only turn text input on after the initial blocking delay, so keys
         * pressed on the game-over screen cannot become part of the name. */
        input_set_text_input(true);

        for(i = 0; i < input->text_len && name_len < HS_NAME_LEN; i++) {
            append_char(input->text[i]);
        }

        if(input->dir_edge == DIR_LEFT) {
            picker = (uint8_t)((picker + strlen(HS_CHARSET) - 1) %
                               strlen(HS_CHARSET));
        } else if(input->dir_edge == DIR_RIGHT) {
            picker = (uint8_t)((picker + 1) % strlen(HS_CHARSET));
        }

        if(input->back && name_len != 0) {
            name_len--;
            name[name_len] = '\0';
        }
        if(input->confirm) {
            append_char(HS_CHARSET[picker]);
        }
        if(input->start) {
            if(name_len == 0) {
                memcpy(name, "PLAYER", 7);
                name_len = 6;
            }
            highlight = insert_score(name, final_score);
            save_table();
            input_set_text_input(false);
            phase = HS_PHASE_TABLE;
            wait_frames = HS_WAIT_FRAMES;
        }
        return;
    }

    if(phase != HS_PHASE_TABLE) {
        return;
    }

    /* On the records screen any key opens the popup. The key that opens it is
     * consumed by the edge logic; do not flush here, otherwise a held key would
     * create a fresh edge on the next frame and select an item immediately. */
    if(!popup_open) {
        if(input->any_key || input->any) {
            popup_open = true;
            popup_selection = 0;
        }
        return;
    }

    if(input->dir_edge == DIR_UP || input->dir_edge == DIR_LEFT) {
        popup_selection = 0;
    } else if(input->dir_edge == DIR_DOWN ||
              input->dir_edge == DIR_RIGHT) {
        popup_selection = 1;
    }

    if(input->back) {
        popup_open = false;
    } else if(input->confirm || input->start) {
        pending_action = popup_selection == 0 ? HS_ACTION_SPLASH
                                              : HS_ACTION_QUIT;
        popup_open = false;
    }
}

void highscore_draw(SDL_Renderer* renderer) {
    if(phase == HS_PHASE_NONE || phase == HS_PHASE_DONE) {
        return;
    }

    if(phase == HS_PHASE_NAME) {
        const int glyph_w = gfx_glyph_width();
        const int glyph_h = gfx_glyph_height();
        char line[HS_NAME_LEN + 1];
        int i;
        int x;

        gfx_clear(renderer, GFX_COLOR_BG);
        gfx_text_centered(renderer, 80, "NEW HIGH SCORE", 2, GFX_COLOR_TEXT);
        gfx_text_centered(renderer, 190, "SCORE", 1, GFX_COLOR_TEXT_DIM);
        {
            char score_text[16];
            SDL_snprintf(score_text, sizeof(score_text), "%05u",
                         (unsigned)final_score);
            gfx_text_dseg_centered(renderer, 225, score_text, 3,
                                   GFX_COLOR_PLAYER);
        }

        for(i = 0; i < HS_NAME_LEN; i++) {
            line[i] = (uint8_t)i < name_len ? name[i] : ' ';
        }
        line[HS_NAME_LEN] = '\0';
        gfx_text_centered(renderer, 350, line, 2, GFX_COLOR_TEXT);

        x = (SCREEN_W - (int)strlen(HS_CHARSET) * glyph_w) / 2;
        gfx_text(renderer, x, 420, HS_CHARSET, 1, GFX_COLOR_TEXT_DIM);
        gfx_rect(renderer, x + (int)picker * glyph_w, 420, glyph_w, glyph_h,
                 GFX_COLOR_BG);
        {
            char selected[2] = { HS_CHARSET[picker], '\0' };
            gfx_text(renderer, x + (int)picker * glyph_w, 420, selected, 1,
                     GFX_COLOR_PLAYER);
        }
        gfx_text_centered(renderer, 520, "LR SELECT A TYPE B DEL ENTER OK", 1,
                          GFX_COLOR_TEXT_DIM);
        return;
    }

    {
        int i;

        gfx_clear(renderer, GFX_COLOR_BG);
        gfx_text_centered(renderer, 80, "HIGH SCORES", 2, GFX_COLOR_TEXT);

        for(i = 0; i < table_count; i++) {
            char row[32];
            int column = i / 5;
            int row_in_column = i % 5;
            int x = column == 0 ? 100 : 520;
            int y = 170 + row_in_column * 36;

            SDL_snprintf(row, sizeof(row), "%02u %-8s %05u",
                         (unsigned)(i + 1), table[i].name,
                         (unsigned)table[i].score);
            gfx_text(renderer, x, y, row, 1,
                     i == highlight ? GFX_COLOR_PLAYER : GFX_COLOR_TEXT);
        }
        if(table_count == 0) {
            gfx_text_centered(renderer, 260, "NO SCORES YET", 1,
                              GFX_COLOR_TEXT_DIM);
        }
        if(!popup_open) {
            gfx_text_centered(renderer, 540, "PRESS ANY KEY", 1,
                              GFX_COLOR_TEXT_DIM);
        }

        if(popup_open) {
            const int w = 640;
            const int h = 180;
            const int x = (SCREEN_W - w) / 2;
            const int y = 350;
            int i;

            gfx_rect_alpha(renderer, 0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 130);
            gfx_rect(renderer, x, y, w, h, GFX_COLOR_PLAYER);
            gfx_rect(renderer, x + 2, y + 2, w - 4, h - 4, GFX_COLOR_BG);
            gfx_rect_alpha(renderer, x + 2, y + 2, w - 4, h - 4,
                           32, 10, 28, 230);
            gfx_text_centered(renderer, y + 16, "SELECT ACTION", 1,
                              GFX_COLOR_TEXT_DIM);

            for(i = 0; i < 2; i++) {
                const int iy = y + 62 + i * 44;
                const bool selected = (uint8_t)i == popup_selection;

                if(selected) {
                    gfx_rect_alpha(renderer, x + 24, iy - 4, w - 48, 36,
                                   255, 210, 0, 45);
                    gfx_rect(renderer, x + 24, iy - 4, 3, 36,
                             GFX_COLOR_PLAYER);
                }
                gfx_text_centered(renderer, iy, popup_items[i], 1,
                                  selected ? GFX_COLOR_PLAYER
                                           : GFX_COLOR_TEXT);
            }
            gfx_text_centered(renderer, y + h - 28,
                              "UP DOWN SELECT   ENTER OK", 1,
                              GFX_COLOR_TEXT_DIM);
        }
    }
}

bool highscore_done(void) {
    return phase == HS_PHASE_DONE || phase == HS_PHASE_NONE;
}

bool highscore_popup_open(void) {
    return popup_open;
}

uint8_t highscore_popup_selection(void) {
    return popup_selection;
}

HighscoreAction highscore_take_action(void) {
    const HighscoreAction action = pending_action;

    pending_action = HS_ACTION_NONE;
    return action;
}
