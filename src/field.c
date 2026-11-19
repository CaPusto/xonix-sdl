#include <string.h>

#include "config.h"
#include "field.h"
#include "gfx.h"

#define CELL_COUNT      (GRID_W * GRID_H)
#define BALL_CENTER     (BALL_SIZE / 2)

static uint8_t field_map[CELL_COUNT];
static uint8_t visited[CELL_COUNT];
static uint16_t queue[CELL_COUNT];

static uint16_t captured_cells;
static uint8_t captured_percent;

static uint16_t cell_index(uint16_t x, uint16_t y) {
    return (uint16_t)(y * GRID_W + x);
}

static uint8_t is_border_cell(uint16_t x, uint16_t y) {
    return (uint8_t)(x < FIELD_PLAY_X0 || x > FIELD_PLAY_X1 ||
                     y < FIELD_PLAY_Y0 || y > FIELD_PLAY_Y1);
}

void field_reset(void) {
    uint16_t y, x;

    memset(field_map, CELL_LAND, sizeof(field_map));
    for(y = FIELD_SEA_Y0; y <= FIELD_SEA_Y1; y++) {
        for(x = FIELD_SEA_X0; x <= FIELD_SEA_X1; x++) {
            field_map[cell_index(x, y)] = CELL_SEA;
        }
    }

    captured_cells = 0;
    captured_percent = 0;
}

uint8_t field_get(uint16_t x, uint16_t y) {
    if(x >= GRID_W || y >= GRID_H) {
        return CELL_LAND;
    }
    return field_map[cell_index(x, y)];
}

void field_set(uint16_t x, uint16_t y, uint8_t state) {
    if(x >= GRID_W || y >= GRID_H) {
        return;
    }
    field_map[cell_index(x, y)] = state;
}

uint8_t field_cell_at_px(int16_t px, int16_t py) {
    if(px < 0 || py < 0) {
        return CELL_LAND;
    }
    return field_get((uint16_t)(px / CELL_SIZE), (uint16_t)(py / CELL_SIZE));
}

/* Flood the sea reachable from the given pixel through four-neighbour steps. */
static void mark_reachable(const int16_t* ball_x, const int16_t* ball_y,
                           uint8_t count) {
    uint16_t head = 0;
    uint16_t tail = 0;
    uint8_t i;

    memset(visited, 0, sizeof(visited));

    for(i = 0; i < count; i++) {
        int16_t cx = (int16_t)((ball_x[i] + BALL_CENTER) / CELL_SIZE);
        int16_t cy = (int16_t)((ball_y[i] + BALL_CENTER) / CELL_SIZE);
        uint16_t ix;

        if(cx < FIELD_SEA_X0 || cx > FIELD_SEA_X1 ||
           cy < FIELD_SEA_Y0 || cy > FIELD_SEA_Y1) {
            continue;
        }
        ix = cell_index((uint16_t)cx, (uint16_t)cy);
        if(field_map[ix] == CELL_SEA && !visited[ix]) {
            visited[ix] = 1;
            queue[tail++] = ix;
        }
    }

    while(head < tail) {
        const uint16_t ix = queue[head++];
        const uint16_t x = ix % GRID_W;
        const uint16_t y = (uint16_t)(ix / GRID_W);

        if(x > FIELD_SEA_X0) {
            const uint16_t n = ix - 1;
            if(field_map[n] == CELL_SEA && !visited[n]) {
                visited[n] = 1;
                queue[tail++] = n;
            }
        }
        if(x < FIELD_SEA_X1) {
            const uint16_t n = ix + 1;
            if(field_map[n] == CELL_SEA && !visited[n]) {
                visited[n] = 1;
                queue[tail++] = n;
            }
        }
        if(y > FIELD_SEA_Y0) {
            const uint16_t n = (uint16_t)(ix - GRID_W);
            if(field_map[n] == CELL_SEA && !visited[n]) {
                visited[n] = 1;
                queue[tail++] = n;
            }
        }
        if(y < FIELD_SEA_Y1) {
            const uint16_t n = (uint16_t)(ix + GRID_W);
            if(field_map[n] == CELL_SEA && !visited[n]) {
                visited[n] = 1;
                queue[tail++] = n;
            }
        }
    }
}

void field_capture(const int16_t* ball_x, const int16_t* ball_y, uint8_t count) {
    uint16_t x, y;

    /* The drawn line itself always becomes land. */
    for(y = 0; y < GRID_H; y++) {
        for(x = 0; x < GRID_W; x++) {
            const uint16_t ix = cell_index(x, y);
            if(field_map[ix] == CELL_TRAIL) {
                field_map[ix] = CELL_LAND;
                captured_cells++;
            }
        }
    }

    mark_reachable(ball_x, ball_y, count);

    /* Water that no ball can reach is captured. */
    for(y = FIELD_SEA_Y0; y <= FIELD_SEA_Y1; y++) {
        for(x = FIELD_SEA_X0; x <= FIELD_SEA_X1; x++) {
            const uint16_t ix = cell_index(x, y);
            if(field_map[ix] == CELL_SEA && !visited[ix]) {
                field_map[ix] = CELL_LAND;
                captured_cells++;
            }
        }
    }

    if(captured_cells > FIELD_SEA_CELLS) {
        captured_cells = FIELD_SEA_CELLS;
    }
    captured_percent = (uint8_t)(((uint32_t)captured_cells * 100u) /
                                 (uint32_t)FIELD_SEA_CELLS);
}

uint16_t field_captured_cells(void) {
    return captured_cells;
}

uint8_t field_captured_percent(void) {
    return captured_percent;
}

static GfxColor color_for_cell(uint16_t x, uint16_t y) {
    const uint8_t state = field_map[cell_index(x, y)];

    if(is_border_cell(x, y)) {
        return GFX_COLOR_BORDER;
    }
    switch(state) {
        case CELL_TRAIL: return GFX_COLOR_TRAIL;
        case CELL_LAND:  return GFX_COLOR_LAND;
        default:         return GFX_COLOR_SEA;
    }
}

void field_render(SDL_Renderer* renderer) {
    uint16_t y;

    for(y = HUD_ROWS; y < GRID_H; y++) {
        uint16_t x = 0;

        while(x < GRID_W) {
            const GfxColor color = color_for_cell(x, y);
            uint16_t end = (uint16_t)(x + 1);

            while(end < GRID_W && color_for_cell(end, y) == color) {
                end++;
            }
            gfx_rect(renderer, (int)x * CELL_SIZE, (int)y * CELL_SIZE,
                     (int)(end - x) * CELL_SIZE, CELL_SIZE, color);
            x = end;
        }
    }
}
