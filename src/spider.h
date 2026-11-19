#ifndef XONIX_SPIDER_H
#define XONIX_SPIDER_H

#include <stdint.h>

#define SPIDER_COUNT 2

typedef struct {
    uint16_t sx;            /* logical cell X */
    uint16_t sy;            /* logical cell Y */
    int8_t   dx;            /* -1 or +1 */
    int8_t   dy;            /* -1 or +1 */
    uint8_t  active;
} spider_t;

extern spider_t spiders[SPIDER_COUNT];

void spider_reset(uint8_t count);
/* Returns 1 when a spider touches the player's cell. */
uint8_t spider_update(uint16_t px, uint16_t py);
uint8_t spider_hits_player(uint16_t px, uint16_t py);

#endif /* XONIX_SPIDER_H */
