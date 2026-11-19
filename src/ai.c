#include <string.h>

#include "ai.h"
#include "config.h"
#include "field.h"

/* Shared state for the duration of one ai_reset()/ai_input() call. */
static const AiWorld* w;

#define BALL_CENTER     (BALL_SIZE / 2)
#define BALL_MIN_X      (FIELD_SEA_X0 * CELL_SIZE)
#define BALL_MAX_X      ((FIELD_SEA_X1 + 1) * CELL_SIZE - BALL_SIZE)
#define BALL_MIN_Y      (FIELD_SEA_Y0 * CELL_SIZE)
#define BALL_MAX_Y      ((FIELD_SEA_Y1 + 1) * CELL_SIZE - BALL_SIZE)

#define SAMPLE_TRAIL    0x01
#define SAMPLE_SOLID    0x02

/*
 * Attract-mode AI.  The planner treats the game as an area-maximisation
 * problem: it enumerates rectangular loops, simulates every enemy for the
 * whole drawing time, and estimates how much SEA the real capture rule would
 * turn into LAND.  A candidate is rejected if an enemy can touch the player or
 * the active trail.  No highscore is ever recorded for this path.
 */
#define AI_MIN_RECT_W     12u
#define AI_RECT_STEP      32u
#define AI_MIN_DEPTH      16u
#define AI_DEPTH_STEP     32u
#define AI_MIN_LEFT       FIELD_PLAY_X0
#define AI_MAX_RIGHT      FIELD_PLAY_X1
#define AI_MAX_DEPTH      (FIELD_PLAY_Y1 - 7u)
#define AI_RISK_HORIZON   320u
#define AI_RISK_WORDS     ((AI_RISK_HORIZON + 63u) / 64u)
#define AI_BALL_MARGIN    1u
#define AI_SPIDER_MARGIN  1u
#define AI_RISK_INTERVAL  12u
#define AI_RISK_COOLDOWN  16u
#define AI_RISK_NONE      0xFFFFu
#define AI_MAX_PATH       768u

typedef enum {
    AI_MOVE_HOME = 0,
    AI_MOVE_RIGHT,
    AI_MOVE_DOWN,
    AI_MOVE_LEFT,
    AI_MOVE_UP
} AiPhase;

typedef struct {
    int16_t x;
    int16_t y;
} AiPoint;

static uint8_t ai_running;
static AiPhase ai_phase;
static uint16_t ai_left;
static uint16_t ai_right;
static uint16_t ai_depth;
static uint8_t ai_start_at_left;
static uint8_t ai_need_edge;
static uint8_t ai_check_frames;
static uint8_t ai_cooldown;
static uint8_t ai_replan_next;

/* Enemy trajectories and per-cell arrival bits.  The bits make an exact
 * "is this cell dangerous at frame t?" query possible without re-simulating
 * every candidate.  ai_ball_last also catches a ball touching a trail cell
 * after the AI has drawn it. */
static AiPoint ai_ball_trace[AI_RISK_HORIZON][AI_BALL_MAX];
static AiSpider ai_spider_trace[AI_RISK_HORIZON][AI_SPIDER_MAX];
static uint64_t ai_ball_bits[GRID_W * GRID_H][AI_RISK_WORDS];
static uint64_t ai_spider_bits[GRID_W * GRID_H][AI_RISK_WORDS];
static uint16_t ai_ball_last[GRID_W * GRID_H];
static uint16_t ai_ball_first[GRID_W * GRID_H];

/* Scratch space used only to measure the real capture gain of a candidate.
 * The field is cached once per plan and copied with memcpy for each candidate;
 * a generation stamp avoids clearing the visited array every time. */
static uint8_t ai_field_cache[GRID_W * GRID_H];
static uint8_t ai_scratch[GRID_W * GRID_H];
static uint16_t ai_seen_stamp[GRID_W * GRID_H];
static uint16_t ai_stamp;
static uint16_t ai_sea_count;
static uint16_t ai_queue[GRID_W * GRID_H];
static uint16_t ai_path_cells[AI_MAX_PATH];


static uint16_t ai_cell_index(uint16_t x, uint16_t y) {
    return (uint16_t)(y * GRID_W + x);
}

static uint16_t ai_align_down(uint16_t value, uint16_t parity) {
    if((value & 1u) != (parity & 1u) && value != 0u) {
        value--;
    }
    return value;
}

static uint16_t ai_align_up(uint16_t value, uint16_t parity) {
    if((value & 1u) != (parity & 1u)) {
        value++;
    }
    return value;
}

static void ai_bits_set(uint64_t* bits, uint16_t frame) {
    bits[frame >> 6] |= (uint64_t)1 << (frame & 63u);
}

static uint8_t ai_bits_has(const uint64_t* bits, uint16_t frame) {
    return (uint8_t)((bits[frame >> 6] >> (frame & 63u)) & 1u);
}

static void ai_mark_ball_footprint(int16_t px, int16_t py, uint16_t frame) {
    int16_t x0 = (int16_t)(px / CELL_SIZE);
    int16_t y0 = (int16_t)(py / CELL_SIZE);
    int16_t x1 = (int16_t)((px + BALL_SIZE - 1) / CELL_SIZE);
    int16_t y1 = (int16_t)((py + BALL_SIZE - 1) / CELL_SIZE);
    int16_t x, y;

    x0 = (int16_t)(x0 - AI_BALL_MARGIN);
    y0 = (int16_t)(y0 - AI_BALL_MARGIN);
    x1 = (int16_t)(x1 + AI_BALL_MARGIN);
    y1 = (int16_t)(y1 + AI_BALL_MARGIN);
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 >= GRID_W) x1 = GRID_W - 1;
    if(y1 >= GRID_H) y1 = GRID_H - 1;

    for(y = y0; y <= y1; y++) {
        for(x = x0; x <= x1; x++) {
            const uint16_t idx = ai_cell_index((uint16_t)x, (uint16_t)y);

            ai_bits_set(ai_ball_bits[idx], frame);
            if(frame < ai_ball_first[idx]) {
                ai_ball_first[idx] = frame;
            }
            if(frame > ai_ball_last[idx]) {
                ai_ball_last[idx] = frame;
            }
        }
    }
}

static void ai_mark_spider_hitbox(uint16_t sx, uint16_t sy, uint16_t frame) {
    const int16_t radius = SPIDER_SIZE / (2 * CELL_SIZE) + AI_SPIDER_MARGIN;
    int16_t x0 = (int16_t)sx - radius;
    int16_t y0 = (int16_t)sy - radius;
    int16_t x1 = (int16_t)sx + radius;
    int16_t y1 = (int16_t)sy + radius;
    int16_t x, y;

    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 >= GRID_W) x1 = GRID_W - 1;
    if(y1 >= GRID_H) y1 = GRID_H - 1;
    for(y = y0; y <= y1; y++) {
        for(x = x0; x <= x1; x++) {
            const uint16_t idx = ai_cell_index((uint16_t)x, (uint16_t)y);

            ai_bits_set(ai_spider_bits[idx], frame);
        }
    }
}

/* Mirror the spider's diagonal walk so the forecast matches the real one. */
static uint8_t ai_spider_blocked(uint16_t x, uint16_t y) {
    if(x < FIELD_PLAY_X0 || x > FIELD_PLAY_X1 ||
       y < FIELD_PLAY_Y0 || y > FIELD_PLAY_Y1) {
        return 1;
    }
    return (uint8_t)(field_get(x, y) != CELL_LAND);
}

static void ai_spider_step_local(AiSpider* sp) {
    int8_t dx = sp->dx;
    int8_t dy = sp->dy;
    int16_t nx, ny;

    if(ai_spider_blocked((uint16_t)(sp->sx + dx), sp->sy)) {
        dx = (int8_t)-dx;
    }
    if(ai_spider_blocked(sp->sx, (uint16_t)(sp->sy + dy))) {
        dy = (int8_t)-dy;
    }

    nx = (int16_t)sp->sx + dx;
    ny = (int16_t)sp->sy + dy;
    if(ai_spider_blocked((uint16_t)nx, (uint16_t)ny)) {
        dx = (int8_t)-sp->dx;
        dy = (int8_t)-sp->dy;
        nx = (int16_t)sp->sx + dx;
        ny = (int16_t)sp->sy + dy;
    }
    if(ai_spider_blocked((uint16_t)nx, (uint16_t)ny)) {
        return;
    }

    sp->sx = (uint16_t)nx;
    sp->sy = (uint16_t)ny;
    sp->dx = dx;
    sp->dy = dy;
}

/* Build one exact trajectory for every active enemy.  Ball reflections use
 * the same helpers and the same field as game_update(), so the forecast knows
 * about walls and previously captured areas. */
static void ai_build_timeline(void) {
    /* Enemy state must persist across forecast frames.  Re-initialising it
     * inside the frame loop would simulate only one step over and over, so
     * the planner would think a ball sits still for the whole horizon. */
    AiBall ball_state[AI_BALL_MAX];
    AiSpider spider_state[AI_SPIDER_MAX];
    uint16_t f;
    uint16_t cell;
    uint8_t i;

    memset(ai_ball_bits, 0, sizeof(ai_ball_bits));
    memset(ai_spider_bits, 0, sizeof(ai_spider_bits));
    ai_sea_count = 0;
    for(cell = 0; cell < GRID_W * GRID_H; cell++) {
        const uint16_t x = (uint16_t)(cell % GRID_W);
        const uint16_t y = (uint16_t)(cell / GRID_W);

        ai_ball_last[cell] = AI_RISK_NONE;
        ai_ball_first[cell] = AI_RISK_NONE;
        ai_field_cache[cell] = field_get(x, y);
        if(ai_field_cache[cell] == CELL_SEA) {
            ai_sea_count++;
        }
    }

    for(i = 0; i < AI_BALL_MAX; i++) {
        ball_state[i] = w->balls[i];
    }
    for(i = 0; i < AI_SPIDER_MAX; i++) {
        spider_state[i] = w->spiders[i];
    }

    for(f = 0; f < AI_RISK_HORIZON; f++) {
        for(i = 0; i < AI_BALL_MAX; i++) {
            AiBall* sim = &ball_state[i];

            if(i >= w->ball_count || !sim->active) {
                continue;
            }

            {
                const uint8_t step_x =
                    w->physics->axis_step(&sim->sub_x, sim->frac);
                const uint8_t step_y =
                    w->physics->axis_step(&sim->sub_y, sim->frac);
                int16_t nx = (int16_t)(sim->x + sim->vx * step_x);
                int16_t ny = (int16_t)(sim->y + sim->vy * step_y);
                uint8_t sample;

                if(nx < BALL_MIN_X) {
                    nx = BALL_MIN_X;
                    sim->vx = (int8_t)-sim->vx;
                } else if(nx > BALL_MAX_X) {
                    nx = BALL_MAX_X;
                    sim->vx = (int8_t)-sim->vx;
                }
                if(ny < BALL_MIN_Y) {
                    ny = BALL_MIN_Y;
                    sim->vy = (int8_t)-sim->vy;
                } else if(ny > BALL_MAX_Y) {
                    ny = BALL_MAX_Y;
                    sim->vy = (int8_t)-sim->vy;
                }

                sample = w->physics->scan_x(sim->x, nx, sim->y);
                if(sample & SAMPLE_SOLID) {
                    sim->vx = (int8_t)-sim->vx;
                    nx = w->physics->clamp_x(
                        (int16_t)(sim->x + sim->vx * step_x));
                }
                sample = w->physics->scan_y(sim->y, ny, nx);
                if(sample & SAMPLE_SOLID) {
                    sim->vy = (int8_t)-sim->vy;
                    ny = w->physics->clamp_y(
                        (int16_t)(sim->y + sim->vy * step_y));
                }

                sim->x = nx;
                sim->y = ny;
            }

            ai_ball_trace[f][i].x = sim->x;
            ai_ball_trace[f][i].y = sim->y;
            ai_mark_ball_footprint(sim->x, sim->y, f);
        }

        for(i = 0; i < AI_SPIDER_MAX; i++) {
            AiSpider* sim = &spider_state[i];

            if(!sim->active) {
                continue;
            }
            ai_spider_step_local(sim);
            ai_spider_trace[f][i] = *sim;
            ai_mark_spider_hitbox(sim->sx, sim->sy, f);
        }
    }
}

/* `t` is one-based: the player has completed t movement steps at the end of
 * frame t-1.  `closure_t` is the one-based frame on which the current loop
 * closes and the whole trail becomes LAND; balls move before the player in
 * that frame, so a ball arriving then is still a threat, but later ones are
 * harmless.
 *
 * Two asymmetries between enemies matter here:
 *  - Balls never touch the player directly.  They only kill when their sprite
 *    overlaps a TRAIL cell.  step() paints TRAIL only on SEA cells, so on the
 *    outer frame and on any already-captured land the player is completely
 *    safe from balls, however close they pass.
 *  - Spiders hunt the player sprite itself and walk on LAND, so they remain
 *    dangerous on the frame and inside captured territory.
 */
static uint8_t ai_cell_unsafe(uint16_t x, uint16_t y, uint16_t t,
                               uint8_t draws_trail, uint16_t closure_t) {
    const uint16_t idx = ai_cell_index(x, y);
    const uint8_t cell = field_get(x, y);
    uint16_t frame;

    if(t == 0 || t > AI_RISK_HORIZON) {
        return 1;
    }
    frame = (uint16_t)(t - 1u);
    if(cell == CELL_TRAIL) {
        return 1;
    }
    /* Spiders collide with the player, not with the trail. */
    if(ai_bits_has(ai_spider_bits[idx], frame)) {
        return 1;
    }
    if(cell != CELL_SEA) {
        /* Frame or captured land: no trail is drawn here, so balls cannot
         * hurt the player.  This lets the AI finish a loop at the border
         * instead of fleeing from a harmless ball. */
        return 0;
    }
    if(ai_bits_has(ai_ball_bits[idx], frame)) {
        return 1;
    }
    /* A ball arriving later matters only while the trail is still down.
     * Once the loop closes the trail becomes LAND: ignore late arrivals. */
    if(draws_trail && closure_t != 0u &&
       ai_ball_first[idx] != AI_RISK_NONE &&
       ai_ball_first[idx] < closure_t && ai_ball_last[idx] >= frame) {
        return 1;
    }
    return 0;
}

static uint8_t ai_add_path_cell(uint16_t x, uint16_t y, uint16_t t,
                                 uint16_t closure_t, uint16_t* path_count) {
    const uint8_t unsafe = ai_cell_unsafe(x, y, t, 1, closure_t);

    if(*path_count < AI_MAX_PATH) {
        ai_path_cells[(*path_count)++] = ai_cell_index(x, y);
    }
    return unsafe;
}

static uint16_t ai_estimate_gain(uint16_t closure_t, uint16_t path_count) {
    uint16_t head = 0;
    uint16_t tail = 0;
    uint16_t cell;
    uint16_t frame;
    uint8_t i;

    memcpy(ai_scratch, ai_field_cache, sizeof(ai_scratch));
    ai_stamp++;
    if(ai_stamp == 0u) {
        for(cell = 0; cell < GRID_W * GRID_H; cell++) {
            ai_seen_stamp[cell] = 0;
        }
        ai_stamp = 1u;
    }

    for(cell = 0; cell < path_count; cell++) {
        ai_scratch[ai_path_cells[cell]] = CELL_LAND;
    }

    frame = closure_t == 0u ? 0u : (uint16_t)(closure_t - 1u);
    for(i = 0; i < w->ball_count; i++) {
        int16_t cx, cy;
        uint16_t idx;

        if(!w->balls[i].active) {
            continue;
        }
        cx = (int16_t)((ai_ball_trace[frame][i].x + BALL_SIZE / 2) /
                       CELL_SIZE);
        cy = (int16_t)((ai_ball_trace[frame][i].y + BALL_SIZE / 2) /
                       CELL_SIZE);
        if(cx < FIELD_SEA_X0 || cx > FIELD_SEA_X1 ||
           cy < FIELD_SEA_Y0 || cy > FIELD_SEA_Y1) {
            continue;
        }
        idx = ai_cell_index((uint16_t)cx, (uint16_t)cy);
        if(ai_scratch[idx] == CELL_SEA && ai_seen_stamp[idx] != ai_stamp) {
            ai_seen_stamp[idx] = ai_stamp;
            ai_queue[tail++] = idx;
        }
    }

    while(head < tail) {
        const uint16_t idx = ai_queue[head++];
        const uint16_t cx = (uint16_t)(idx % GRID_W);
        const uint16_t cy = (uint16_t)(idx / GRID_W);
        uint16_t n;

        if(cx > FIELD_SEA_X0) {
            n = (uint16_t)(idx - 1u);
            if(ai_scratch[n] == CELL_SEA && ai_seen_stamp[n] != ai_stamp) {
                ai_seen_stamp[n] = ai_stamp;
                ai_queue[tail++] = n;
            }
        }
        if(cx < FIELD_SEA_X1) {
            n = (uint16_t)(idx + 1u);
            if(ai_scratch[n] == CELL_SEA && ai_seen_stamp[n] != ai_stamp) {
                ai_seen_stamp[n] = ai_stamp;
                ai_queue[tail++] = n;
            }
        }
        if(cy > FIELD_SEA_Y0) {
            n = (uint16_t)(idx - GRID_W);
            if(ai_scratch[n] == CELL_SEA && ai_seen_stamp[n] != ai_stamp) {
                ai_seen_stamp[n] = ai_stamp;
                ai_queue[tail++] = n;
            }
        }
        if(cy < FIELD_SEA_Y1) {
            n = (uint16_t)(idx + GRID_W);
            if(ai_scratch[n] == CELL_SEA && ai_seen_stamp[n] != ai_stamp) {
                ai_seen_stamp[n] = ai_stamp;
                ai_queue[tail++] = n;
            }
        }
    }

    /* Every SEA cell not reached by a ball becomes LAND at capture time.
     * `unreachable` also contains the freshly drawn trail cells: they were SEA
     * in the cached field but are LAND in the scratch copy, so they are missing
     * from `tail`.  That is exactly the gain of this one capture and must not
     * be reduced by field_captured_cells(), which counts cells taken earlier. */
    if(ai_sea_count <= tail) {
        return 0u;
    }
    return (uint16_t)(ai_sea_count - tail);
}

/* Frames needed to cross a horizontal or vertical leg at two cells/frame. */
static uint16_t ai_leg_frames(uint16_t a, uint16_t b) {
    return (uint16_t)(((a > b ? a - b : b - a) + 1u) / 2u);
}

/* Walk the whole candidate path.  This both validates safety and records the
 * trail cells needed for the capture estimate. */
static uint8_t ai_build_candidate(uint16_t start_x, uint16_t left,
                                    uint16_t right, uint16_t depth,
                                    uint8_t start_at_left,
                                    uint16_t* gain, uint16_t* frames,
                                    uint16_t* risk) {
    uint16_t path_count = 0;
    uint16_t t = 0;
    uint16_t x = start_x;
    uint16_t y = FIELD_PLAY_Y0;
    const uint16_t side = start_at_left ? left : right;
    const uint16_t other = start_at_left ? right : left;
    const int8_t approach = start_at_left ? -1 : 1;
    const int8_t bottom = start_at_left ? 1 : -1;
    /* The trail survives until the player steps back onto LAND at the top
     * frame.  A ball arriving after the loop closes cannot hit anything. */
    const uint16_t closure_t = (uint16_t)(ai_leg_frames(start_x, side) +
                                ai_leg_frames(FIELD_PLAY_Y0, depth) +
                                ai_leg_frames(side, other) +
                                ai_leg_frames(FIELD_PLAY_Y0, depth));

    *risk = 0;
    *gain = 0;
    *frames = 0;

    /* update_player() takes two one-cell steps per frame.  Both cells become
     * trail, so the planner must check and record both of them. */
    while(x != side) {
        const uint16_t mid = (uint16_t)((int16_t)x + approach);

        x = (uint16_t)((int16_t)x + approach * 2);
        t++;
        if(t > AI_RISK_HORIZON) {
            return 1;
        }
        *risk += ai_cell_unsafe(mid, FIELD_PLAY_Y0, t, 0, closure_t);
        *risk += ai_cell_unsafe(x, FIELD_PLAY_Y0, t, 0, closure_t);
    }

    while(y < depth) {
        const uint16_t mid = (uint16_t)(y + 1u);

        y = (uint16_t)(y + 2u);
        t++;
        if(t > AI_RISK_HORIZON) {
            return 1;
        }
        *risk += ai_add_path_cell(side, mid, t, closure_t, &path_count);
        *risk += ai_add_path_cell(side, y, t, closure_t, &path_count);
    }

    while(x != other) {
        const uint16_t mid = (uint16_t)((int16_t)x + bottom);

        x = (uint16_t)((int16_t)x + bottom * 2);
        t++;
        if(t > AI_RISK_HORIZON) {
            return 1;
        }
        *risk += ai_add_path_cell(mid, depth, t, closure_t, &path_count);
        *risk += ai_add_path_cell(x, depth, t, closure_t, &path_count);
    }

    while(y > FIELD_PLAY_Y0) {
        const uint16_t mid = (uint16_t)(y - 1u);

        y = (uint16_t)(y - 2u);
        t++;
        if(t > AI_RISK_HORIZON) {
            return 1;
        }
        *risk += ai_add_path_cell(other, mid, t, closure_t, &path_count);
        if(y == FIELD_PLAY_Y0) {
            *risk += ai_cell_unsafe(other, y, t, 0, closure_t);
        } else {
            *risk += ai_add_path_cell(other, y, t, closure_t, &path_count);
        }
    }

    *frames = t;
    if(*risk == 0u) {
        *gain = ai_estimate_gain(t, path_count);
    }
    return 0;
}

static void ai_consider_candidate(uint16_t start_x, uint16_t left,
                                    uint16_t right, uint16_t depth,
                                    uint8_t start_at_left,
                                    uint32_t* best_safe,
                                    uint16_t* best_left,
                                    uint16_t* best_right,
                                    uint16_t* best_depth,
                                    uint8_t* best_start_at_left,
                                    uint16_t* best_unsafe_risk,
                                    uint16_t* unsafe_left,
                                    uint16_t* unsafe_right,
                                    uint16_t* unsafe_depth,
                                    uint8_t* unsafe_start_at_left) {
    uint16_t gain;
    uint16_t frames;
    uint16_t risk;
    uint32_t score;

    if(right <= left || depth <= FIELD_PLAY_Y0) {
        return;
    }
    if(ai_build_candidate(start_x, left, right, depth, start_at_left,
                            &gain, &frames, &risk)) {
        return;
    }

    if(risk == 0u && gain == 0u) {
        /* A safe but empty loop is a last resort: it keeps the player moving
         * without helping the capture goal, so remember it only as a fallback
         * and keep looking for a loop that actually takes territory. */
        if(*best_safe == 0u &&
           (*best_unsafe_risk == AI_RISK_NONE || *best_unsafe_risk != 0u)) {
            *best_unsafe_risk = 0u;
            *unsafe_left = left;
            *unsafe_right = right;
            *unsafe_depth = depth;
            *unsafe_start_at_left = start_at_left;
        }
        return;
    }

    if(risk == 0u) {
        /* Gain dominates; the small tie-break prefers a quicker loop, which
         * leaves less time for future enemies to interfere. */
        score = (uint32_t)gain * 256u +
                (uint32_t)(AI_MAX_PATH - (frames < AI_MAX_PATH
                                                        ? frames
                                                        : AI_MAX_PATH));
        if(score > *best_safe) {
            *best_safe = score;
            *best_left = left;
            *best_right = right;
            *best_depth = depth;
            *best_start_at_left = start_at_left;
        }
    } else if(risk < *best_unsafe_risk) {
        *best_unsafe_risk = risk;
        *unsafe_left = left;
        *unsafe_right = right;
        *unsafe_depth = depth;
        *unsafe_start_at_left = start_at_left;
    }
}

static void ai_go_home(void) {
    ai_left = (uint16_t)AI_MIN_LEFT;
    ai_right = ai_left;
    ai_start_at_left = 1;
    ai_phase = AI_MOVE_HOME;
    ai_need_edge = 1;
}

static void ai_choose_plan(void) {
    const uint16_t start_x = w->player_cx;
    const uint16_t min_left = ai_align_up((uint16_t)AI_MIN_LEFT, start_x);
    const uint16_t max_right = ai_align_down((uint16_t)AI_MAX_RIGHT,
                                               start_x);
    uint16_t left, right, depth;
    uint16_t best_left = 0;
    uint16_t best_right = 0;
    uint16_t best_depth = 0;
    uint16_t unsafe_left = 0;
    uint16_t unsafe_right = 0;
    uint16_t unsafe_depth = 0;
    uint16_t unsafe_risk = AI_RISK_NONE;
    uint8_t best_start_at_left = 1;
    uint8_t unsafe_start_at_left = 1;
    uint8_t start_at_left;
    uint32_t best_safe = 0;

    ai_build_timeline();

    for(start_at_left = 0; start_at_left <= 1; start_at_left++) {
        if(start_at_left) {
            if(start_x <= min_left + 2u) {
                continue;
            }
            for(left = min_left;
                left <= start_x - 2u &&
                (uint16_t)(left + AI_MIN_RECT_W) <= max_right;
                left = (uint16_t)(left + AI_RECT_STEP)) {
                const uint16_t first_right = ai_align_up(
                    (uint16_t)(left + AI_MIN_RECT_W), start_x);

                /* Always try the exact right frame too.  Stepping by
                 * AI_RECT_STEP from start_x rarely lands on it, which made
                 * the AI turn away just before the right border. */
                for(right = first_right;; right = (uint16_t)(right + AI_RECT_STEP)) {
                    const uint16_t r = right > max_right ? max_right : right;

                    for(depth = (uint16_t)(FIELD_PLAY_Y0 + AI_MIN_DEPTH);
                        depth <= (uint16_t)AI_MAX_DEPTH;
                        depth = (uint16_t)(depth + AI_DEPTH_STEP)) {
                        ai_consider_candidate(
                            start_x, left, r, depth, start_at_left,
                            &best_safe, &best_left, &best_right, &best_depth,
                            &best_start_at_left, &unsafe_risk,
                            &unsafe_left, &unsafe_right, &unsafe_depth,
                            &unsafe_start_at_left);
                    }
                    if(r == max_right) {
                        break;
                    }
                }
            }
        } else {
            const uint16_t first_right = (uint16_t)(start_x + 2u);

            if(first_right > max_right) {
                continue;
            }
            /* Include the exact right frame even when it is not on the
             * AI_RECT_STEP sequence. */
            for(right = first_right;; right = (uint16_t)(right + AI_RECT_STEP)) {
                const uint16_t r = right > max_right ? max_right : right;

                for(left = min_left;
                    left + AI_MIN_RECT_W <= r;
                    left = (uint16_t)(left + AI_RECT_STEP)) {
                    for(depth = (uint16_t)(FIELD_PLAY_Y0 + AI_MIN_DEPTH);
                        depth <= (uint16_t)AI_MAX_DEPTH;
                        depth = (uint16_t)(depth + AI_DEPTH_STEP)) {
                        ai_consider_candidate(
                            start_x, left, r, depth, start_at_left,
                            &best_safe, &best_left, &best_right, &best_depth,
                            &best_start_at_left, &unsafe_risk,
                            &unsafe_left, &unsafe_right, &unsafe_depth,
                            &unsafe_start_at_left);
                    }
                }
                if(r == max_right) {
                    break;
                }
            }
        }
    }

    if(best_safe != 0u) {
        ai_left = best_left;
        ai_right = best_right;
        ai_depth = best_depth;
        ai_start_at_left = best_start_at_left;
    } else if(unsafe_risk != AI_RISK_NONE) {
        ai_left = unsafe_left;
        ai_right = unsafe_right;
        ai_depth = unsafe_depth;
        ai_start_at_left = unsafe_start_at_left;
    } else {
        ai_go_home();
        return;
    }

    ai_phase = AI_MOVE_HOME;
    ai_need_edge = 1;
    ai_check_frames = 0;
}

/* Frames left until the active loop closes, measured from the player's
 * current cell.  Used to discard ball threats that materialise only after
 * the trail has already turned into land. */
static uint16_t ai_remaining_frames(void) {
    const uint16_t side = ai_start_at_left ? ai_left : ai_right;
    const uint16_t other = ai_start_at_left ? ai_right : ai_left;

    switch(ai_phase) {
        case AI_MOVE_HOME:
            return (uint16_t)(ai_leg_frames(w->player_cx, side) +
                              ai_leg_frames(FIELD_PLAY_Y0, ai_depth) +
                              ai_leg_frames(side, other) +
                              ai_leg_frames(FIELD_PLAY_Y0, ai_depth));
        case AI_MOVE_DOWN:
            return (uint16_t)(ai_leg_frames(w->player_cy, ai_depth) +
                              ai_leg_frames(side, other) +
                              ai_leg_frames(FIELD_PLAY_Y0, ai_depth));
        case AI_MOVE_RIGHT:
            return (uint16_t)(ai_leg_frames(w->player_cx, ai_right) +
                              ai_leg_frames(w->player_cy, FIELD_PLAY_Y0));
        case AI_MOVE_LEFT:
            return (uint16_t)(ai_leg_frames(w->player_cx, ai_left) +
                              ai_leg_frames(w->player_cy, FIELD_PLAY_Y0));
        case AI_MOVE_UP:
            return ai_leg_frames(w->player_cy, FIELD_PLAY_Y0);
        default:
            return 0;
    }
}

/* Check only the part of the active loop that has not been drawn yet.
 * `*safe_frames` receives how many frames of this leg are still safe, so the
 * caller can keep drawing right up to the threat instead of turning away at
 * the first sign of one. */
static uint8_t ai_path_threatened(uint16_t* safe_frames) {
    uint16_t t = 1;
    uint16_t x, y;
    const uint16_t side = ai_start_at_left ? ai_left : ai_right;
    const uint16_t other = ai_start_at_left ? ai_right : ai_left;
    const uint16_t closure_t = ai_remaining_frames();

    *safe_frames = 0;
    switch(ai_phase) {
        case AI_MOVE_HOME:
            x = w->player_cx;
            while(x != side) {
                const int8_t dir = ai_start_at_left ? -1 : 1;
                const uint16_t mid = (uint16_t)((int16_t)x + dir);

                x = (uint16_t)((int16_t)x + dir * 2);
                if(ai_cell_unsafe(mid, FIELD_PLAY_Y0, t, 0, closure_t) ||
                   ai_cell_unsafe(x, FIELD_PLAY_Y0, t, 0, closure_t)) {
                    *safe_frames = (uint16_t)(t - 1u);
                    return 1;
                }
                t++;
            }
            break;

        case AI_MOVE_DOWN:
            for(y = w->player_cy; y < ai_depth; y = (uint16_t)(y + 2u)) {
                if(ai_cell_unsafe(side, (uint16_t)(y + 1u), t, 1, closure_t) ||
                   ai_cell_unsafe(side, (uint16_t)(y + 2u), t, 1, closure_t)) {
                    *safe_frames = (uint16_t)(t - 1u);
                    return 1;
                }
                t++;
            }
            break;

        case AI_MOVE_RIGHT:
            for(x = w->player_cx; x < ai_right; x = (uint16_t)(x + 2u)) {
                if(ai_cell_unsafe((uint16_t)(x + 1u), w->player_cy, t, 1,
                                  closure_t) ||
                   ai_cell_unsafe((uint16_t)(x + 2u), w->player_cy, t, 1,
                                  closure_t)) {
                    *safe_frames = (uint16_t)(t - 1u);
                    return 1;
                }
                t++;
            }
            break;

        case AI_MOVE_LEFT:
            for(x = w->player_cx; x > ai_left; x = (uint16_t)(x - 2u)) {
                if(ai_cell_unsafe((uint16_t)(x - 1u), w->player_cy, t, 1,
                                  closure_t) ||
                   ai_cell_unsafe((uint16_t)(x - 2u), w->player_cy, t, 1,
                                  closure_t)) {
                    *safe_frames = (uint16_t)(t - 1u);
                    return 1;
                }
                t++;
            }
            break;

        case AI_MOVE_UP:
            y = w->player_cy;
            while(y > FIELD_PLAY_Y0) {
                const uint16_t mid = (uint16_t)(y - 1u);

                y = (uint16_t)(y - 2u);
                if(ai_cell_unsafe(other, mid, t, 1, closure_t)) {
                    *safe_frames = (uint16_t)(t - 1u);
                    return 1;
                }
                if(y == FIELD_PLAY_Y0) {
                    if(ai_cell_unsafe(other, y, t, 0, closure_t)) {
                        *safe_frames = (uint16_t)(t - 1u);
                        return 1;
                    }
                } else if(ai_cell_unsafe(other, y, t, 1, closure_t)) {
                    *safe_frames = (uint16_t)(t - 1u);
                    return 1;
                }
                t++;
            }
            break;

        default:
            break;
    }
    /* No threat on this leg; every frame of it is safe. */
    *safe_frames = (uint16_t)(t - 1u);
    return 0;
}

static void ai_reset_state(const AiWorld* world) {
    if(world == NULL) {
        return;
    }
    w = world;
    ai_running = 1;
    ai_check_frames = 0;
    ai_cooldown = 0;
    ai_replan_next = 1;
    ai_choose_plan();
}
void ai_shutdown(void) {
    ai_running = 0;
    w = NULL;
}

void ai_reset(const AiWorld* world) {
    ai_reset_state(world);
}

void ai_input(InputFrame* input, const AiWorld* world) {
    uint8_t want = DIR_NONE;
    uint16_t safe_frames;
    int step;

    if(input == NULL) {
        return;
    }
    memset(input, 0, sizeof(*input));
    if(world == NULL) {
        return;
    }
    w = world;
    if(!ai_running) {
        return;
    }

    /* The level-complete menu is dismissed automatically. `confirm` is
     * otherwise ignored by game_update(), so normal play is unaffected. */
    if(w->wait_for_key) {
        input->confirm = 1;
        input->start = 1;
        input->any = 1;
        return;
    }
    if(w->await_release) {
        return;
    }

    /* The first input frame moves a single cell because player_dir starts as
     * DIR_NONE.  Replan once afterwards so every candidate keeps the player's
     * two-cell parity. */
    if(ai_replan_next && w->player_dir != DIR_NONE) {
        ai_replan_next = 0;
        ai_choose_plan();
    }

    if(ai_cooldown != 0) {
        ai_cooldown--;
    }
    if(++ai_check_frames >= AI_RISK_INTERVAL) {
        ai_check_frames = 0;
        if(ai_cooldown == 0) {
            ai_build_timeline();
            if(ai_path_threatened(&safe_frames)) {
                switch(ai_phase) {
                    case AI_MOVE_HOME:
                        /* No trail exists yet, so a fresh plan is free. */
                        ai_choose_plan();
                        ai_cooldown = AI_RISK_COOLDOWN;
                        break;

                    case AI_MOVE_DOWN: {
                        /* Keep drawing until the last safe row, then let the
                         * normal phase transition close the loop.  Turning
                         * away at the first distant threat throws away land
                         * that was perfectly safe to capture. */
                        const uint16_t limit = ai_depth;

                        ai_depth = (uint16_t)(w->player_cy + safe_frames * 2u);
                        if(ai_depth > limit) {
                            ai_depth = limit;
                        }
                        ai_cooldown = 0;   /* keep watching while we advance */
                        break;
                    }

                    case AI_MOVE_RIGHT: {
                        const uint16_t limit = ai_right;

                        ai_right = (uint16_t)(w->player_cx +
                                              safe_frames * 2u);
                        if(ai_right > limit) {
                            ai_right = limit;
                        }
                        ai_cooldown = 0;
                        break;
                    }

                    case AI_MOVE_LEFT: {
                        const uint16_t limit = ai_left;

                        ai_left = (uint16_t)(w->player_cx -
                                             safe_frames * 2u);
                        if(ai_left < limit) {
                            ai_left = limit;
                        }
                        ai_cooldown = 0;
                        break;
                    }

                    default:
                        /* The closing ascent already ends the loop: finish
                         * it, since a captured trail cannot be hit. */
                        break;
                }
            }
        }
    }

    for(step = 0; step < 2; step++) {
        switch(ai_phase) {
            case AI_MOVE_HOME:
                want = ai_start_at_left ? DIR_LEFT : DIR_RIGHT;
                if((ai_start_at_left && w->player_cx <= ai_left) ||
                   (!ai_start_at_left && w->player_cx >= ai_right)) {
                    ai_phase = AI_MOVE_DOWN;
                    ai_need_edge = 1;
                    continue;
                }
                break;

            case AI_MOVE_RIGHT:
                want = DIR_RIGHT;
                if(w->player_cx >= ai_right) {
                    ai_phase = AI_MOVE_UP;
                    ai_need_edge = 1;
                    continue;
                }
                break;

            case AI_MOVE_DOWN:
                want = DIR_DOWN;
                if(w->player_cy >= ai_depth) {
                    ai_phase = ai_start_at_left ? AI_MOVE_RIGHT
                                                : AI_MOVE_LEFT;
                    ai_need_edge = 1;
                    continue;
                }
                break;

            case AI_MOVE_LEFT:
                want = DIR_LEFT;
                if(w->player_cx <= ai_left) {
                    ai_phase = AI_MOVE_UP;
                    ai_need_edge = 1;
                    continue;
                }
                break;

            case AI_MOVE_UP:
                want = DIR_UP;
                if(w->player_cy <= FIELD_PLAY_Y0) {
                    ai_choose_plan();
                    continue;
                }
                break;

            default:
                want = DIR_NONE;
                break;
        }
        break;
    }

    input->dir = want;
    if(ai_need_edge) {
        if(want != w->player_dir) {
            input->dir_edge = want;
        }
        ai_need_edge = 0;
    }
}

