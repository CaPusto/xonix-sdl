#ifndef XONIX_LEVELS_H
#define XONIX_LEVELS_H

#include <stdbool.h>
#include <stdint.h>

#define LEVEL_COUNT         21
#define LEVEL_GOAL          75
#define LEVEL_TIME_STANDARD 300

/* Ball speed grades from the original game design document. */
#define BALL_SPEED_SLOW     0
#define BALL_SPEED_MEDIUM   1
#define BALL_SPEED_FAST     2

typedef struct {
    uint8_t  balls;     /* number of balls, 1..5 */
    uint8_t  speed;     /* BALL_SPEED_* */
    uint8_t  spiders;   /* number of spiders, 0..2 */
    uint16_t time;      /* level time limit, seconds */
} level_t;

extern const level_t levels[LEVEL_COUNT];

/* Ball grades: 4.0 / 4.8 / 5.6 px per frame at 60 fps (one cell is 4 px). */
uint8_t level_speed_frac(uint8_t speed);

/* Score awarded for finishing a level with the given captured percentage. */
uint16_t level_score(uint8_t percent);

/* Parse "--level=N" or the value part of "--level N". 0 means "not given". */
uint8_t level_parse_arg(const char* arg);

/* True when any --level form is present on the command line. */
bool level_arg_given(int argc, char** argv);

/* Parse a whole argv array, including "--level N". 0 means no valid level. */
uint8_t level_parse_args(int argc, char** argv);

#endif /* XONIX_LEVELS_H */
