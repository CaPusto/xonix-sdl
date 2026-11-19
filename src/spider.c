#include <stdint.h>

#include "config.h"
#include "field.h"
#include "spider.h"

/*
 * Spiders walk on LAND: the frame and everything the player has captured.
 * The player moves two cells per frame (8 px), so a spider moves one cell per
 * frame (4 px), i.e. half the player's speed as specified in the GDD.
 */
#define SPIDER_HIT_CELLS    (SPIDER_SIZE / (2 * CELL_SIZE))
#define SPIDER_SPAWN_Y   FIELD_PLAY_Y1
#define SPIDER_SPAWN_X0  (GRID_W / 4)
#define SPIDER_SPAWN_X1  (GRID_W - GRID_W / 4)

spider_t spiders[SPIDER_COUNT];

static uint8_t spider_blocked(uint16_t x, uint16_t y) {
    if(x < FIELD_PLAY_X0 || x > FIELD_PLAY_X1 ||
       y < FIELD_PLAY_Y0 || y > FIELD_PLAY_Y1) {
        return 1;
    }
    return (uint8_t)(field_get(x, y) != CELL_LAND);
}

void spider_reset(uint8_t count) {
    uint8_t i;

    for(i = 0; i < SPIDER_COUNT; i++) {
        spiders[i].active = 0;
    }
    if(count > SPIDER_COUNT) {
        count = SPIDER_COUNT;
    }

    if(count >= 1) {
        spiders[0].sx = SPIDER_SPAWN_X0;
        spiders[0].sy = SPIDER_SPAWN_Y;
        spiders[0].dx = 1;
        spiders[0].dy = -1;
        spiders[0].active = 1;
    }
    if(count >= 2) {
        spiders[1].sx = SPIDER_SPAWN_X1;
        spiders[1].sy = SPIDER_SPAWN_Y;
        spiders[1].dx = -1;
        spiders[1].dy = -1;
        spiders[1].active = 1;
    }
}

/* Diagonal step with reflection against sea and playfield edges. */
static void spider_step(spider_t* sp) {
    int8_t dx = sp->dx;
    int8_t dy = sp->dy;
    int16_t nx, ny;

    if(spider_blocked((uint16_t)(sp->sx + dx), sp->sy)) {
        dx = (int8_t)-dx;
    }
    if(spider_blocked(sp->sx, (uint16_t)(sp->sy + dy))) {
        dy = (int8_t)-dy;
    }

    nx = (int16_t)sp->sx + dx;
    ny = (int16_t)sp->sy + dy;

    if(spider_blocked((uint16_t)nx, (uint16_t)ny)) {
        dx = (int8_t)-sp->dx;
        dy = (int8_t)-sp->dy;
        nx = (int16_t)sp->sx + dx;
        ny = (int16_t)sp->sy + dy;
    }
    if(spider_blocked((uint16_t)nx, (uint16_t)ny)) {
        return;
    }

    sp->sx = (uint16_t)nx;
    sp->sy = (uint16_t)ny;
    sp->dx = dx;
    sp->dy = dy;
}

uint8_t spider_hits_player(uint16_t px, uint16_t py) {
    uint8_t i;

    for(i = 0; i < SPIDER_COUNT; i++) {
        int16_t dx, dy;

        if(!spiders[i].active) {
            continue;
        }
        dx = (int16_t)spiders[i].sx - (int16_t)px;
        dy = (int16_t)spiders[i].sy - (int16_t)py;
        /* Half the spider's width on a 4-pixel grid. */
        if(dx >= -SPIDER_HIT_CELLS && dx <= SPIDER_HIT_CELLS &&
           dy >= -SPIDER_HIT_CELLS && dy <= SPIDER_HIT_CELLS) {
            return 1;
        }
    }
    return 0;
}

uint8_t spider_update(uint16_t px, uint16_t py) {
    uint8_t i;

    if(spider_hits_player(px, py)) {
        return 1;
    }

    for(i = 0; i < SPIDER_COUNT; i++) {
        if(spiders[i].active) {
            spider_step(&spiders[i]);
        }
    }

    return spider_hits_player(px, py);
}
