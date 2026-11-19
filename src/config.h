#ifndef XONIX_CONFIG_H
#define XONIX_CONFIG_H

#include <stdint.h>

/*
 * Logical video mode.  The game always renders into a 960x600 framebuffer;
 * SDL scales it to the window (integer-friendly, nearest-neighbour filtering).
 *
 * Like the old Zeal version, a cell is 4x4 pixels.  On the 960x600 screen this
 * gives a 240x150 cell grid.  The top twelve cell rows (48 pixels) are reserved
 * for the single-row HUD.
 */
#define SCREEN_W            960
#define SCREEN_H            600

#define CELL_SIZE           4
#define GRID_W              (SCREEN_W / CELL_SIZE)   /* 240 */
#define GRID_H              (SCREEN_H / CELL_SIZE)   /* 150 */
#define HUD_ROWS            12                      /* 48 pixels */

/* Playable frame and the inner sea, in cell coordinates. */
#define FIELD_PLAY_X0       2
#define FIELD_PLAY_X1       (GRID_W - 3)             /* 237 */
#define FIELD_PLAY_Y0       (HUD_ROWS + 1)           /*  13 */
#define FIELD_PLAY_Y1       (GRID_H - 2)             /* 148 */

#define FIELD_SEA_X0        4
#define FIELD_SEA_X1        (GRID_W - 5)             /* 235 */
#define FIELD_SEA_Y0        (HUD_ROWS + 3)           /*  15 */
#define FIELD_SEA_Y1        (GRID_H - 4)             /* 146 */

#define FIELD_SEA_CELLS     ((uint16_t)((FIELD_SEA_X1 - FIELD_SEA_X0 + 1) * \
                                        (FIELD_SEA_Y1 - FIELD_SEA_Y0 + 1)))

#define SPIDER_SIZE         24     /* animated spider sprite, same as a ball */
#define PLAYER_SIZE         16     /* player sphere: 1x the original 16 px ball */
#define BALL_SIZE           24     /* enemy sphere: 1.5x the original 16 px ball */
#define FIELD_LIVES_MAX     5
#define FIXED_STEP_MS       (1000 / 60)

#endif /* XONIX_CONFIG_H */
