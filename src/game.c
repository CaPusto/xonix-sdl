#include <string.h>

#include "ai.h"

#include "audio.h"
#include "config.h"
#include "field.h"
#include "game.h"
#include "gfx.h"
#include "highscore.h"
#include "levels.h"
#include "spider.h"

#define BALL_MAX            5
#define PLAYER_SPEED        2       /* cells per frame: 8 px, twice ball speed */
#define PLAYER_START_CX     (GRID_W / 2)
#define PLAYER_START_CY     FIELD_PLAY_Y0

#define BALL_MIN_X          (FIELD_SEA_X0 * CELL_SIZE)
#define BALL_MAX_X          ((FIELD_SEA_X1 + 1) * CELL_SIZE - BALL_SIZE)
#define BALL_MIN_Y          (FIELD_SEA_Y0 * CELL_SIZE)
#define BALL_MAX_Y          ((FIELD_SEA_Y1 + 1) * CELL_SIZE - BALL_SIZE)
#define BALL_EDGE_INSET     0      /* full rectangular footprint for collision */

#define SAMPLE_TRAIL        0x01
#define SAMPLE_SOLID        0x02

#define MOVE_OK             0
#define MOVE_BLOCKED        1
#define MOVE_DIED           2

#define WAIT_BLINK_FRAMES   25
#define WAIT_ARM_FRAMES     60
#define WAIT_AUTO_FRAMES    (30u * 60u)

typedef struct {
    int16_t x;              /* top-left pixel of the sprite */
    int16_t y;
    int8_t  vx;
    int8_t  vy;
    uint8_t sub_x;          /* fractional pixel accumulator, 0..4 */
    uint8_t sub_y;
    uint8_t frac;           /* extra pixel every five frames */
    uint8_t active;
} ball_t;

static ball_t balls[BALL_MAX];
static uint8_t ball_count;

/* Initial values are only a fallback; pick_ball_spawns() randomises them on
 * every level/life reset. */
static int16_t ball_start_x[BALL_MAX] = { 200, 680, 440, 120, 820 };
static int16_t ball_start_y[BALL_MAX] = { 280, 140, 430,  90, 520 };
static const int8_t ball_start_vx[BALL_MAX] = {   1,  -1,   1,  -1,   1 };
static const int8_t ball_start_vy[BALL_MAX] = {   1,   1,  -1,  -1,   1 };

static uint8_t level;
static uint16_t score;
static uint16_t level_time;
static uint16_t time_left;
static uint8_t time_frames;
static uint8_t level_ball_speed;
static uint8_t level_spiders;

static void reset_balls(void);
static void pick_ball_spawns(void);
static void reset_level(void);
static void ai_relocate_trapped_balls(void);
static void fill_ai_world(AiWorld* world);
static void apply_level_config(uint8_t new_level);
/* Returns 1 when this life was the last one. */
static uint8_t lose_life(void);

static uint16_t player_cx;
static uint16_t player_cy;
static uint8_t player_dir;
static uint8_t turn_request;
static uint8_t drawing;
static uint8_t lives;
static uint8_t await_release;
static uint8_t game_over;
static uint8_t bounce_alt;

static uint8_t demo_running;
static uint8_t demo_mode;
static uint8_t demo_level;
static uint8_t demo_lives;
static AiWorld ai_world;

static uint8_t wait_for_key;
static uint8_t wait_armed;
static uint8_t wait_blink;
static uint8_t wait_visible;
static uint16_t wait_arm_frames;
static uint16_t wait_total;
static uint8_t pending_level;
static uint16_t last_level_score;
static uint16_t level_entry_score;
static uint8_t menu_selection;   /* 0 = CONTINUE, 1 = RESTART LEVEL */

/* Ball helpers are defined below, next to the real collision code. */
static uint8_t ball_axis_step(uint8_t* sub, uint8_t frac);
static uint8_t ball_scan_x(int16_t old_x, int16_t new_x, int16_t y);
static uint8_t ball_scan_y(int16_t old_y, int16_t new_y, int16_t x);
static int16_t clamp_ball_x(int16_t x);
static int16_t clamp_ball_y(int16_t y);

static int8_t dir_dx(uint8_t dir) {
    if(dir == DIR_LEFT)  return -1;
    if(dir == DIR_RIGHT) return 1;
    return 0;
}

static int8_t dir_dy(uint8_t dir) {
    if(dir == DIR_UP)   return -1;
    if(dir == DIR_DOWN) return 1;
    return 0;
}

static uint8_t opposite_dir(uint8_t dir) {
    switch(dir) {
        case DIR_LEFT:  return DIR_RIGHT;
        case DIR_RIGHT: return DIR_LEFT;
        case DIR_UP:    return DIR_DOWN;
        case DIR_DOWN:  return DIR_UP;
        default:        return DIR_NONE;
    }
}

static uint8_t next_cell(uint8_t dir, uint16_t* x, uint16_t* y) {
    const int16_t nx = (int16_t)player_cx + dir_dx(dir);
    const int16_t ny = (int16_t)player_cy + dir_dy(dir);

    if(nx < FIELD_PLAY_X0 || nx > FIELD_PLAY_X1 ||
       ny < FIELD_PLAY_Y0 || ny > FIELD_PLAY_Y1) {
        return 0;
    }
    *x = (uint16_t)nx;
    *y = (uint16_t)ny;
    return 1;
}

static void apply_level_config(uint8_t new_level) {
    const level_t* cfg;

    level = new_level;
    cfg = &levels[level - 1];
    ball_count = cfg->balls;
    if(ball_count > BALL_MAX) {
        ball_count = BALL_MAX;
    }
    level_ball_speed = cfg->speed;
    level_spiders = cfg->spiders;
    if(level_spiders > SPIDER_COUNT) {
        level_spiders = SPIDER_COUNT;
    }
    level_time = cfg->time;
}

static uint8_t player_on_frame(void) {
    return (uint8_t)(player_cx <= FIELD_PLAY_X0 + 1 ||
                     player_cx >= FIELD_PLAY_X1 - 1 ||
                     player_cy <= FIELD_PLAY_Y0 + 1 ||
                     player_cy >= FIELD_PLAY_Y1 - 1);
}

static uint8_t can_turn(uint8_t dir) {
    uint16_t x, y;

    if(!next_cell(dir, &x, &y)) {
        return 0;
    }
    return (uint8_t)(field_get(x, y) != CELL_TRAIL);
}

static uint8_t step(uint8_t dir) {
    uint16_t nx, ny;
    uint8_t state;

    if(!next_cell(dir, &nx, &ny)) {
        return MOVE_BLOCKED;
    }

    state = field_get(nx, ny);
    if(state == CELL_TRAIL) {
        return MOVE_DIED;
    }

    player_cx = nx;
    player_cy = ny;
    player_dir = dir;

    if(state == CELL_SEA) {
        field_set(nx, ny, CELL_TRAIL);
        drawing = 1;
    } else if(drawing) {
        int16_t bx[BALL_MAX];
        int16_t by[BALL_MAX];
        uint8_t i;

        for(i = 0; i < ball_count; i++) {
            bx[i] = balls[i].x;
            by[i] = balls[i].y;
        }
        field_capture(bx, by, ball_count);
        audio_capture();
        drawing = 0;
    }

    return MOVE_OK;
}

static uint8_t update_player(const InputFrame* input) {
    uint8_t want = DIR_NONE;
    uint8_t i;

    if(await_release) {
        if(input->dir == DIR_NONE) {
            await_release = 0;
        }
    } else {
        want = input->dir_edge;
    }

    if(want != DIR_NONE) {
        if(player_dir == DIR_NONE) {
            uint8_t result = step(want);

            if(result == MOVE_DIED) {
                return 1;
            }
            return 0;
        }

        if(want != player_dir) {
            if(want == opposite_dir(player_dir)) {
                if(player_on_frame() && can_turn(want)) {
                    turn_request = want;
                }
            } else if(can_turn(want)) {
                turn_request = want;
            }
        }
    }

    if(player_dir == DIR_NONE) {
        return 0;
    }

    for(i = 0; i < PLAYER_SPEED; i++) {
        uint8_t dir = player_dir;
        uint8_t result;

        if(turn_request != DIR_NONE) {
            dir = turn_request;
            turn_request = DIR_NONE;
        }
        result = step(dir);
        if(result == MOVE_DIED) {
            return 1;
        }
        if(result == MOVE_BLOCKED) {
            player_dir = DIR_NONE;
            turn_request = DIR_NONE;
            break;
        }
    }

    return 0;
}

static void reset_ball(uint8_t index) {
    balls[index].x = ball_start_x[index];
    balls[index].y = ball_start_y[index];
    balls[index].vx = ball_start_vx[index];
    balls[index].vy = ball_start_vy[index];
    balls[index].sub_x = 0;
    balls[index].sub_y = 0;
    balls[index].frac = level_speed_frac(level_ball_speed);
    balls[index].active = 1;
}

static void reset_balls(void) {
    uint8_t i;

    pick_ball_spawns();
    for(i = 0; i < BALL_MAX; i++) {
        if(i < ball_count) {
            reset_ball(i);
        } else {
            balls[i].active = 0;
        }
    }
}

/*
 * Check every grid cell overlapped by the ball's swept footprint along one
 * axis. Probing only two rows/columns could miss a one-cell-thick land line
 * and let the ball escape a captured area.
 */
static uint8_t ball_scan_cells(int16_t x0, int16_t x1, int16_t y0, int16_t y1) {
    uint8_t result = 0;
    int16_t cy, cx;

    for(cy = y0; cy <= y1; cy++) {
        for(cx = x0; cx <= x1; cx++) {
            const uint8_t cell = field_get((uint16_t)cx, (uint16_t)cy);

            if(cell == CELL_TRAIL) {
                result |= SAMPLE_TRAIL;
            } else if(cell != CELL_SEA) {
                result |= SAMPLE_SOLID;
            }
        }
    }
    return result;
}

static uint8_t ball_scan_x(int16_t old_x, int16_t new_x, int16_t y) {
    const int16_t x0 = (int16_t)(((old_x < new_x ? old_x : new_x) +
                                  BALL_EDGE_INSET) / CELL_SIZE);
    const int16_t x1 = (int16_t)(((old_x > new_x ? old_x : new_x) +
                                  BALL_SIZE - BALL_EDGE_INSET - 1) / CELL_SIZE);
    const int16_t y0 = (int16_t)((y + BALL_EDGE_INSET) / CELL_SIZE);
    const int16_t y1 = (int16_t)((y + BALL_SIZE - BALL_EDGE_INSET - 1) /
                                 CELL_SIZE);

    return ball_scan_cells(x0, x1, y0, y1);
}

static uint8_t ball_scan_y(int16_t old_y, int16_t new_y, int16_t x) {
    const int16_t y0 = (int16_t)(((old_y < new_y ? old_y : new_y) +
                                  BALL_EDGE_INSET) / CELL_SIZE);
    const int16_t y1 = (int16_t)(((old_y > new_y ? old_y : new_y) +
                                  BALL_SIZE - BALL_EDGE_INSET - 1) / CELL_SIZE);
    const int16_t x0 = (int16_t)((x + BALL_EDGE_INSET) / CELL_SIZE);
    const int16_t x1 = (int16_t)((x + BALL_SIZE - BALL_EDGE_INSET - 1) /
                                 CELL_SIZE);

    return ball_scan_cells(x0, x1, y0, y1);
}

static int16_t clamp_ball_x(int16_t x) {
    if(x < BALL_MIN_X) return BALL_MIN_X;
    if(x > BALL_MAX_X) return BALL_MAX_X;
    return x;
}

static int16_t clamp_ball_y(int16_t y) {
    if(y < BALL_MIN_Y) return BALL_MIN_Y;
    if(y > BALL_MAX_Y) return BALL_MAX_Y;
    return y;
}

/* Slow 4 px/frame (one cell); medium 4.8; fast 5.6 at 60 fps. */
static uint8_t ball_axis_step(uint8_t* sub, uint8_t frac) {
    uint8_t extra = 0;

    *sub = (uint8_t)(*sub + (uint8_t)(frac * 2u));
    while(*sub >= 5) {
        *sub = (uint8_t)(*sub - 5);
        extra++;
    }
    return (uint8_t)(CELL_SIZE + extra);
}

static uint8_t update_balls(void) {
    uint8_t hit = 0;
    uint8_t bounced = 0;
    uint8_t i;

    for(i = 0; i < BALL_MAX; i++) {
        ball_t* ball = &balls[i];
        int16_t nx, ny;
        uint8_t step_x, step_y;

        if(!ball->active) {
            continue;
        }

        step_x = ball_axis_step(&ball->sub_x, ball->frac);
        step_y = ball_axis_step(&ball->sub_y, ball->frac);
        nx = (int16_t)(ball->x + ball->vx * step_x);
        ny = (int16_t)(ball->y + ball->vy * step_y);

        if(nx < BALL_MIN_X) {
            nx = BALL_MIN_X;
            ball->vx = (int8_t)-ball->vx;
            bounced = 1;
        } else if(nx > BALL_MAX_X) {
            nx = BALL_MAX_X;
            ball->vx = (int8_t)-ball->vx;
            bounced = 1;
        }
        if(ny < BALL_MIN_Y) {
            ny = BALL_MIN_Y;
            ball->vy = (int8_t)-ball->vy;
            bounced = 1;
        } else if(ny > BALL_MAX_Y) {
            ny = BALL_MAX_Y;
            ball->vy = (int8_t)-ball->vy;
            bounced = 1;
        }

        /* Sweep the whole footprint on each axis. Sparse probe rows/columns
         * let a ball slip through a land line that is only one cell thick. */
        {
            const uint8_t sample = ball_scan_x(ball->x, nx, ball->y);

            if(sample & SAMPLE_TRAIL) {
                hit = 1;
            } else if(sample & SAMPLE_SOLID) {
                ball->vx = (int8_t)-ball->vx;
                nx = clamp_ball_x((int16_t)(ball->x + ball->vx * step_x));
                bounced = 1;
            }
        }
        {
            const uint8_t sample = ball_scan_y(ball->y, ny, nx);

            if(sample & SAMPLE_TRAIL) {
                hit = 1;
            } else if(sample & SAMPLE_SOLID) {
                ball->vy = (int8_t)-ball->vy;
                ny = clamp_ball_y((int16_t)(ball->y + ball->vy * step_y));
                bounced = 1;
            }
        }

        ball->x = nx;
        ball->y = ny;
    }

    if(bounced) {
        audio_bounce(bounce_alt);
        bounce_alt = (uint8_t)(bounce_alt ^ 1);
    }
    return hit;
}

static void draw_hud(SDL_Renderer* renderer) {
    char value[16];
    uint8_t i;

    /* Single centred HUD row: lives, level, time and capture progress. */
    for(i = 0; i < FIELD_LIVES_MAX; i++) {
        gfx_sprite(renderer, GFX_SPRITE_PLAYER, 62 + (int)i * 20, 16);
        if(i >= lives) {
            gfx_rect(renderer, 62 + (int)i * 20, 16, PLAYER_SIZE, PLAYER_SIZE,
                     GFX_COLOR_BG);
        }
    }

    gfx_text(renderer, 181, 13, "LEVEL", 1, GFX_COLOR_TEXT_DIM);
    SDL_snprintf(value, sizeof(value), "%02u", (unsigned)level);
    gfx_text_dseg(renderer, 315, 13, value, 1, GFX_COLOR_TEXT);

    gfx_text(renderer, 383, 13, "TIME", 1, GFX_COLOR_TEXT_DIM);
    SDL_snprintf(value, sizeof(value), "%02u:%02u",
                 (unsigned)(time_left / 60u), (unsigned)(time_left % 60u));
    gfx_text_dseg(renderer, 495, 13, value, 1, GFX_COLOR_TEXT);

    gfx_text(renderer, 629, 13, "CAPT", 1, GFX_COLOR_TEXT_DIM);
    SDL_snprintf(value, sizeof(value), "%03u",
                 (unsigned)field_captured_percent());
    gfx_text_dseg(renderer, 741, 13, value, 1, GFX_COLOR_TRAIL);
    gfx_text(renderer, 831, 13, "PCT", 1, GFX_COLOR_TRAIL);
}

/* `value_y` is the top of the large number. The extra seven pixels compensate
 * for the font's ink bounds: aligning the glyph cells would leave the visible
 * label text noticeably below the number. */
#define MENU_LABEL_INK_OFFSET 7

static void draw_menu_stat(SDL_Renderer* renderer, int value_y,
                           const char* label, const char* value,
                           GfxColor value_color) {
    const int gap = 16;
    const int label_h = gfx_glyph_height();
    const int value_h = label_h * 2;
    const int label_y = value_y + value_h - label_h - MENU_LABEL_INK_OFFSET;
    const int label_w = gfx_text_width(label, 1);
    const int value_w = gfx_text_dseg_width(value, 2);
    const int x = (SCREEN_W - (label_w + gap + value_w)) / 2;

    gfx_text(renderer, x, label_y, label, 1, GFX_COLOR_TEXT_DIM);
    gfx_text_dseg(renderer, x + label_w + gap, value_y, value, 2,
                  value_color);
}

static void draw_menu_item(SDL_Renderer* renderer, int y, const char* label,
                           uint8_t selected) {
    const int w = 400;
    const int h = 44;
    const int x = (SCREEN_W - w) / 2;
    const GfxColor edge = selected ? GFX_COLOR_PLAYER : GFX_COLOR_TEXT_DIM;

    gfx_rect_alpha(renderer, x, y, w, h, 0, 0, 0, 180);
    gfx_rect(renderer, x, y, w, 2, edge);
    gfx_rect(renderer, x, y + h - 2, w, 2, edge);
    gfx_rect(renderer, x, y, 2, h, edge);
    gfx_rect(renderer, x + w - 2, y, 2, h, edge);
    gfx_text(renderer, x + (w - gfx_text_width(label, 1)) / 2,
             y + (h - gfx_glyph_height()) / 2, label, 1,
             selected ? GFX_COLOR_PLAYER : GFX_COLOR_TEXT);
}

/* Translucent modal menu shown after a level is captured. */
static void draw_level_menu(SDL_Renderer* renderer) {
    const int panel_x = 170;
    const int panel_y = 120;
    const int panel_w = 620;
    const int panel_h = 390;
    char value[16];

    gfx_rect_alpha(renderer, 0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 150);
    gfx_rect_alpha(renderer, panel_x, panel_y, panel_w, panel_h,
                   0, 16, 40, 220);
    gfx_rect(renderer, panel_x, panel_y, panel_w, 2, GFX_COLOR_PLAYER);
    gfx_rect(renderer, panel_x, panel_y + panel_h - 2, panel_w, 2,
             GFX_COLOR_PLAYER);
    gfx_rect(renderer, panel_x, panel_y, 2, panel_h, GFX_COLOR_PLAYER);
    gfx_rect(renderer, panel_x + panel_w - 2, panel_y, 2, panel_h,
             GFX_COLOR_PLAYER);

    gfx_text_centered(renderer, 145, "LEVEL COMPLETE", 2, GFX_COLOR_PLAYER);

    SDL_snprintf(value, sizeof(value), "%02u", (unsigned)level);
    draw_menu_stat(renderer, 206, "LEVEL", value, GFX_COLOR_TEXT);

    SDL_snprintf(value, sizeof(value), "%04u", (unsigned)last_level_score);
    draw_menu_stat(renderer, 266, "LEVEL SCORE", value, GFX_COLOR_PLAYER);

    gfx_rect(renderer, panel_x + 30, 325, panel_w - 60, 2,
             GFX_COLOR_TEXT_DIM);
    draw_menu_item(renderer, 345, "CONTINUE", menu_selection == 0);
    draw_menu_item(renderer, 405, "RESTART LEVEL", menu_selection == 1);

    if(wait_visible) {
        gfx_text_centered(renderer, 470, "UP DOWN SELECT   ENTER OK", 1,
                          GFX_COLOR_TEXT_DIM);
    }
}

static void reset_level(void) {
    audio_stop_gameplay();
    if(level_spiders != 0) {
        audio_buzz_start();
    }

    field_reset();

    player_cx = PLAYER_START_CX;
    player_cy = PLAYER_START_CY;
    player_dir = DIR_NONE;
    turn_request = DIR_NONE;
    drawing = 0;

    time_left = level_time;
    time_frames = 0;

    reset_balls();
    spider_reset(level_spiders);
}

/* Attract-mode life loss keeps the captured land so the demo can keep growing
 * a level instead of restarting the field from scratch every few seconds. */
static void demo_soft_reset(void) {
    uint16_t x, y;

    for(y = HUD_ROWS; y < GRID_H; y++) {
        for(x = 0; x < GRID_W; x++) {
            if(field_get(x, y) == CELL_TRAIL) {
                field_set(x, y, CELL_LAND);
            }
        }
    }

    audio_stop_gameplay();
    if(level_spiders != 0) {
        audio_buzz_start();
    }
    player_cx = PLAYER_START_CX;
    player_cy = PLAYER_START_CY;
    player_dir = DIR_NONE;
    turn_request = DIR_NONE;
    drawing = 0;
    time_left = level_time;
    time_frames = 0;
    reset_balls();
    spider_reset(level_spiders);
    ai_relocate_trapped_balls();
    fill_ai_world(&ai_world);
    ai_reset(&ai_world);
    await_release = 1;
}

static uint8_t lose_life(void) {
    if(demo_mode) {
        demo_soft_reset();
        return 0;
    }

    if(lives > 0) {
        lives--;
    }

    if(lives == 0) {
        audio_stop_gameplay();
        audio_game_over();
        game_over = 1;
        return 1;
    }

    audio_life_lost();
    reset_level();
    await_release = 1;
    return 0;
}

static uint8_t handle_life_loss(void) {
    return lose_life() ? GAME_EVENT_GAME_OVER : GAME_EVENT_NONE;
}

void game_shutdown(void) {
    demo_running = 0;
    demo_mode = 0;
    ai_shutdown();
    audio_stop_gameplay();
}

void game_init(uint8_t start_level) {
    demo_running = 0;
    demo_mode = 0;
    ai_shutdown();
    lives = FIELD_LIVES_MAX;
    score = 0;
    level_entry_score = 0;
    game_over = 0;
    await_release = 0;
    wait_for_key = 0;
    wait_armed = 0;
    wait_arm_frames = 0;
    wait_total = 0;
    menu_selection = 0;
    turn_request = DIR_NONE;

    if(start_level < 1) {
        start_level = 1;
    }
    if(start_level > LEVEL_COUNT) {
        start_level = LEVEL_COUNT;
    }

    apply_level_config(start_level);
    reset_level();
}

uint16_t game_score(void) {
    return score;
}


/* Bridge to the attract-mode planner.  The planner sees a snapshot and a set
 * of ball-physics callbacks, so ai.c never touches the game's private state. */
static const AiBallPhysics ai_physics = {
    ball_axis_step, ball_scan_x, ball_scan_y, clamp_ball_x, clamp_ball_y
};

static void fill_ai_world(AiWorld* world) {
    uint8_t i;

    world->player_cx = player_cx;
    world->player_cy = player_cy;
    world->player_dir = player_dir;
    world->ball_count = ball_count;
    world->level = level;
    world->lives = lives;
    world->wait_for_key = wait_for_key;
    world->await_release = await_release;
    world->physics = &ai_physics;

    for(i = 0; i < AI_BALL_MAX; i++) {
        world->balls[i].x = balls[i].x;
        world->balls[i].y = balls[i].y;
        world->balls[i].vx = balls[i].vx;
        world->balls[i].vy = balls[i].vy;
        world->balls[i].sub_x = balls[i].sub_x;
        world->balls[i].sub_y = balls[i].sub_y;
        world->balls[i].frac = balls[i].frac;
        world->balls[i].active = balls[i].active;
    }
    for(i = 0; i < AI_SPIDER_MAX; i++) {
        world->spiders[i].sx = spiders[i].sx;
        world->spiders[i].sy = spiders[i].sy;
        world->spiders[i].dx = spiders[i].dx;
        world->spiders[i].dy = spiders[i].dy;
        world->spiders[i].active = spiders[i].active;
    }
}

/* A ball must never be respawned inside the land kept by a demo soft reset. */
static uint8_t ai_spawn_clear(int16_t x, int16_t y) {
    const int16_t x0 = (int16_t)(x / CELL_SIZE);
    const int16_t y0 = (int16_t)(y / CELL_SIZE);
    const int16_t x1 = (int16_t)((x + BALL_SIZE - 1) / CELL_SIZE);
    const int16_t y1 = (int16_t)((y + BALL_SIZE - 1) / CELL_SIZE);
    int16_t cx, cy;

    for(cy = y0; cy <= y1; cy++) {
        for(cx = x0; cx <= x1; cx++) {
            if(field_get((uint16_t)cx, (uint16_t)cy) != CELL_SEA) {
                return 0;
            }
        }
    }
    return 1;
}

static uint8_t ranges_overlap(int16_t a0, int16_t a1,
                              int16_t b0, int16_t b1) {
    return (uint8_t)(a0 <= b1 && b0 <= a1);
}

static uint8_t ai_spawn_overlaps(int16_t x, int16_t y,
                                 const int16_t* placed_x,
                                 const int16_t* placed_y, uint8_t count) {
    const int16_t x0 = (int16_t)(x / CELL_SIZE) - 1;
    const int16_t x1 = (int16_t)((x + BALL_SIZE - 1) / CELL_SIZE) + 1;
    const int16_t y0 = (int16_t)(y / CELL_SIZE) - 1;
    const int16_t y1 = (int16_t)((y + BALL_SIZE - 1) / CELL_SIZE) + 1;
    uint8_t i;

    if(ranges_overlap(x0, x1, (int16_t)player_cx - 2,
                      (int16_t)player_cx + 2) &&
       ranges_overlap(y0, y1, (int16_t)player_cy - 2,
                      (int16_t)player_cy + 2)) {
        return 1;
    }
    for(i = 0; i < count; i++) {
        if(ranges_overlap(x0, x1, (int16_t)(placed_x[i] / CELL_SIZE) - 1,
                          (int16_t)((placed_x[i] + BALL_SIZE - 1) /
                                    CELL_SIZE) + 1) &&
           ranges_overlap(y0, y1, (int16_t)(placed_y[i] / CELL_SIZE) - 1,
                          (int16_t)((placed_y[i] + BALL_SIZE - 1) /
                                    CELL_SIZE) + 1)) {
            return 1;
        }
    }
    return 0;
}

static uint8_t ai_find_spawn(int16_t pref_x, int16_t pref_y,
                             const int16_t* placed_x,
                             const int16_t* placed_y, uint8_t count,
                             int16_t* out_x, int16_t* out_y) {
    const int16_t pref_cx = (int16_t)((pref_x + BALL_SIZE / 2) / CELL_SIZE);
    const int16_t pref_cy = (int16_t)((pref_y + BALL_SIZE / 2) / CELL_SIZE);
    const uint8_t cells = (uint8_t)(BALL_SIZE / CELL_SIZE);
    uint8_t ignore_overlap;

    /* Prefer a vacant spot; if the sea is crowded, overlap is still better
     * than trapping a ball on land. */
    for(ignore_overlap = 0; ignore_overlap <= 1; ignore_overlap++) {
        uint32_t best = UINT32_MAX;
        uint8_t found = 0;
        uint16_t cy, cx;

        for(cy = FIELD_SEA_Y0; cy + cells - 1 <= FIELD_SEA_Y1; cy++) {
            for(cx = FIELD_SEA_X0; cx + cells - 1 <= FIELD_SEA_X1; cx++) {
                const int16_t ox = (int16_t)(cx * CELL_SIZE);
                const int16_t oy = (int16_t)(cy * CELL_SIZE);
                const int32_t dx = (int32_t)cx - pref_cx;
                const int32_t dy = (int32_t)cy - pref_cy;
                const uint32_t dist = (uint32_t)(dx * dx + dy * dy);

                if(!ai_spawn_clear(ox, oy)) {
                    continue;
                }
                if(!ignore_overlap &&
                   ai_spawn_overlaps(ox, oy, placed_x, placed_y, count)) {
                    continue;
                }
                if(dist < best) {
                    best = dist;
                    *out_x = ox;
                    *out_y = oy;
                    found = 1;
                }
            }
        }
        if(found) {
            return 1;
        }
    }
    return 0;
}

/* Pick a fresh random layout for the enemy balls whenever a level or a life
 * starts.  Spots are cell-aligned, lie fully inside the open sea, and avoid
 * the player and the balls already placed.  The arrays are updated in place,
 * so ai_relocate_trapped_balls() uses exactly the same layout after a demo
 * soft reset. */
static void pick_ball_spawns(void) {
    const int16_t span_x = (int16_t)(FIELD_SEA_X1 - FIELD_SEA_X0 + 1 -
                                     BALL_SIZE / CELL_SIZE);
    const int16_t span_y = (int16_t)(FIELD_SEA_Y1 - FIELD_SEA_Y0 + 1 -
                                     BALL_SIZE / CELL_SIZE);
    uint8_t i;

    for(i = 0; i < BALL_MAX; i++) {
        int16_t chosen_x = ball_start_x[i];
        int16_t chosen_y = ball_start_y[i];
        uint8_t attempt;

        for(attempt = 0; attempt < 64; attempt++) {
            const int16_t cx = (int16_t)(FIELD_SEA_X0 + SDL_rand(span_x));
            const int16_t cy = (int16_t)(FIELD_SEA_Y0 + SDL_rand(span_y));
            const int16_t ox = (int16_t)(cx * CELL_SIZE);
            const int16_t oy = (int16_t)(cy * CELL_SIZE);

            if(!ai_spawn_clear(ox, oy) ||
               ai_spawn_overlaps(ox, oy, ball_start_x, ball_start_y, i)) {
                continue;
            }
            chosen_x = ox;
            chosen_y = oy;
            break;
        }
        ball_start_x[i] = chosen_x;
        ball_start_y[i] = chosen_y;
    }
}

static void ai_relocate_trapped_balls(void) {
    int16_t placed_x[BALL_MAX];
    int16_t placed_y[BALL_MAX];
    uint8_t placed = 0;
    uint8_t need_fresh_field = 0;
    uint8_t i;

    for(i = 0; i < BALL_MAX; i++) {
        int16_t nx = ball_start_x[i];
        int16_t ny = ball_start_y[i];

        if(!balls[i].active) {
            continue;
        }
        if(ai_spawn_clear(ball_start_x[i], ball_start_y[i]) &&
           !ai_spawn_overlaps(ball_start_x[i], ball_start_y[i],
                              placed_x, placed_y, placed)) {
            nx = ball_start_x[i];
            ny = ball_start_y[i];
        } else if(!ai_find_spawn(ball_start_x[i], ball_start_y[i],
                                 placed_x, placed_y, placed, &nx, &ny)) {
            need_fresh_field = 1;
            break;
        }
        reset_ball(i);
        balls[i].x = nx;
        balls[i].y = ny;
        placed_x[placed] = nx;
        placed_y[placed] = ny;
        placed++;
    }

    if(need_fresh_field) {
        /* No 24x24 patch of sea is left: restart the field instead of
         * leaving enemies stuck on green land. */
        field_reset();
        reset_balls();
    }
}

void game_demo_reset(void) {
    demo_running = 1;
    demo_mode = 1;
    demo_level = level;
    demo_lives = lives;
    fill_ai_world(&ai_world);
    ai_reset(&ai_world);
}

void game_demo_input(InputFrame* input) {
    if(input == NULL) {
        return;
    }
    memset(input, 0, sizeof(*input));
    if(!demo_running) {
        return;
    }

    fill_ai_world(&ai_world);

    /* A lost life or an auto-continued level restarts the field, so the
     * rectangle plan has to start over from the new player position. */
    if(ai_world.level != demo_level) {
        demo_level = ai_world.level;
        demo_lives = ai_world.lives;
        ai_reset(&ai_world);
    } else if(ai_world.lives != demo_lives) {
        if(ai_world.lives < demo_lives) {
            lives = demo_lives;
        }
        demo_lives = lives;
        fill_ai_world(&ai_world);
        ai_reset(&ai_world);
    }

    ai_input(input, &ai_world);
}

uint8_t game_update(const InputFrame* input) {
    if(wait_for_key) {
        uint8_t activate = 0;

        if(wait_armed) {
            if(input->dir_edge == DIR_UP || input->dir_edge == DIR_LEFT) {
                menu_selection = 0;
            } else if(input->dir_edge == DIR_DOWN ||
                      input->dir_edge == DIR_RIGHT) {
                menu_selection = 1;
            }
            if(input->confirm || input->start) {
                activate = 1;
            }
        } else if(!input->any) {
            wait_armed = 1;
        } else if(++wait_arm_frames >= WAIT_ARM_FRAMES) {
            wait_armed = 1;
        }

        if(!activate && ++wait_total >= WAIT_AUTO_FRAMES) {
            activate = 1;
        }

        if(activate) {
            wait_for_key = 0;
            if(menu_selection == 0) {
                level_entry_score = score;
                apply_level_config(pending_level);
            } else {
                /* Undo this level's bonus so restarting cannot farm points. */
                score = level_entry_score;
                apply_level_config(level);
            }
            reset_level();
            await_release = 1;
            return GAME_EVENT_NONE;
        }

        if(++wait_blink >= WAIT_BLINK_FRAMES) {
            wait_blink = 0;
            wait_visible = (uint8_t)(wait_visible ^ 1);
        }
        return GAME_EVENT_NONE;
    }

    /* One real second per 60 fixed steps. */
    if(time_left != 0 && ++time_frames >= 60) {
        time_frames = 0;
        time_left--;
        if(time_left == 0) {
            return handle_life_loss();
        }
    }

    if(update_balls()) {
        return handle_life_loss();
    }

    if(update_player(input)) {
        return handle_life_loss();
    }

    if(field_captured_percent() >= LEVEL_GOAL) {
        last_level_score = level_score(field_captured_percent());
        score = (uint16_t)(score + last_level_score);
        audio_level_complete();
        pending_level = level >= LEVEL_COUNT ? 1 : (uint8_t)(level + 1);
        wait_for_key = 1;
        wait_armed = 0;
        wait_blink = 0;
        wait_visible = 1;
        wait_arm_frames = 0;
        wait_total = 0;
        menu_selection = 0;
        return GAME_EVENT_NONE;
    }

    if(spider_update(player_cx, player_cy)) {
        return handle_life_loss();
    }

    return GAME_EVENT_NONE;
}

/* The spider art faces up; movement is always diagonal on the cell grid. */
static double spider_facing(const spider_t* sp) {
    if(sp->dx > 0) {
        return sp->dy < 0 ? 45.0 : 135.0;
    }
    return sp->dy < 0 ? -45.0 : 225.0;
}

void game_draw(SDL_Renderer* renderer) {
    uint8_t i;
    int player_x;
    int player_y;
    const int anim_tick = (int)(SDL_GetTicks() / 110u);

    gfx_clear(renderer, GFX_COLOR_BG);
    field_render(renderer);

    for(i = 0; i < BALL_MAX; i++) {
        if(balls[i].active) {
            gfx_sprite(renderer, GFX_SPRITE_BALL, balls[i].x, balls[i].y);
        }
    }
    for(i = 0; i < SPIDER_COUNT; i++) {
        if(spiders[i].active) {
            gfx_spider_draw(renderer,
                            (anim_tick + (int)i) % GFX_SPIDER_FRAMES,
                            spider_facing(&spiders[i]),
                            (int)spiders[i].sx * CELL_SIZE + CELL_SIZE / 2 -
                                SPIDER_SIZE / 2,
                            (int)spiders[i].sy * CELL_SIZE + CELL_SIZE / 2 -
                                SPIDER_SIZE / 2);
        }
    }
    player_x = (int)player_cx * CELL_SIZE + CELL_SIZE / 2 - PLAYER_SIZE / 2;
    player_y = (int)player_cy * CELL_SIZE + CELL_SIZE / 2 - PLAYER_SIZE / 2;
    if(player_y < HUD_ROWS * CELL_SIZE) {
        player_y = HUD_ROWS * CELL_SIZE;
    }
    if(player_y > SCREEN_H - PLAYER_SIZE) {
        player_y = SCREEN_H - PLAYER_SIZE;
    }
    gfx_sprite(renderer, GFX_SPRITE_PLAYER, player_x, player_y);

    draw_hud(renderer);
    if(wait_for_key) {
        draw_level_menu(renderer);
    }
}
