#ifndef XONIX_FIELD_H
#define XONIX_FIELD_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* Cell states of the logical 4x4-pixel grid. */
#define CELL_SEA        0
#define CELL_LAND       1
#define CELL_TRAIL      2

/* Reset to the initial frame and empty sea, and clear the capture counter. */
void field_reset(void);

/* Cell access. Coordinates outside the grid are solid LAND. */
uint8_t field_get(uint16_t x, uint16_t y);
void field_set(uint16_t x, uint16_t y, uint8_t state);
uint8_t field_cell_at_px(int16_t px, int16_t py);

/*
 * Close the current trail: TRAIL becomes LAND, then the sea unreachable from
 * any ball becomes LAND (the classic Xonix capture rule).  Ball coordinates
 * are the top-left pixel of a BALL_SIZE x BALL_SIZE sprite.
 */
void field_capture(const int16_t* ball_x, const int16_t* ball_y, uint8_t count);

uint16_t field_captured_cells(void);
uint8_t field_captured_percent(void);

/* Draw the whole playfield. The HUD area is left untouched. */
void field_render(SDL_Renderer* renderer);

#endif /* XONIX_FIELD_H */
