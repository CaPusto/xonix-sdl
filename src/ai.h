#ifndef XONIX_AI_H
#define XONIX_AI_H

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

/* Snapshot limits. They mirror BALL_MAX in game.c and SPIDER_COUNT in
 * spider.h; the planner only ever needs these fixed-size arrays. */
#define AI_BALL_MAX     5
#define AI_SPIDER_MAX   2

/* Read-only copy of an enemy ball. */
typedef struct {
    int16_t x;              /* top-left pixel of the sprite */
    int16_t y;
    int8_t  vx;
    int8_t  vy;
    uint8_t sub_x;
    uint8_t sub_y;
    uint8_t frac;
    uint8_t active;
} AiBall;

/* Read-only copy of a spider. */
typedef struct {
    uint16_t sx;
    uint16_t sy;
    int8_t dx;
    int8_t dy;
    uint8_t active;
} AiSpider;

/* Ball motion/collision helpers supplied by the game. Sharing them keeps the
 * planner's forecast byte-for-byte identical to the real simulation. */
typedef struct {
    uint8_t (*axis_step)(uint8_t* sub, uint8_t frac);
    uint8_t (*scan_x)(int16_t old_x, int16_t new_x, int16_t y);
    uint8_t (*scan_y)(int16_t old_y, int16_t new_y, int16_t x);
    int16_t (*clamp_x)(int16_t x);
    int16_t (*clamp_y)(int16_t y);
} AiBallPhysics;

/* Everything the attract-mode planner is allowed to observe about the game.
 * The arrays are snapshots, not live game state. */
typedef struct {
    uint16_t player_cx;
    uint16_t player_cy;
    uint8_t player_dir;
    uint8_t ball_count;
    AiBall balls[AI_BALL_MAX];
    AiSpider spiders[AI_SPIDER_MAX];
    uint8_t level;
    uint8_t lives;
    uint8_t wait_for_key;
    uint8_t await_release;
    const AiBallPhysics* physics;
} AiWorld;

/* Start/stop the attract-mode planner. */
void ai_shutdown(void);

/* Reset planner state and pick a first contour from the given world snapshot. */
void ai_reset(const AiWorld* world);

/* Fill `input` with the next attract-mode action for this fixed step. */
void ai_input(InputFrame* input, const AiWorld* world);

#endif /* XONIX_AI_H */
