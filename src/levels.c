#include <stdint.h>
#include <string.h>

#include "levels.h"

/* Balance table from the original Xonix GDD (unchanged). */
const level_t levels[LEVEL_COUNT] = {
    { 1, BALL_SPEED_SLOW,   0, LEVEL_TIME_STANDARD },   /*  1 */
    { 1, BALL_SPEED_MEDIUM, 0, LEVEL_TIME_STANDARD },   /*  2 */
    { 2, BALL_SPEED_SLOW,   0, LEVEL_TIME_STANDARD },   /*  3 */
    { 2, BALL_SPEED_MEDIUM, 0, LEVEL_TIME_STANDARD },   /*  4 */
    { 2, BALL_SPEED_MEDIUM, 1, LEVEL_TIME_STANDARD },   /*  5 */
    { 3, BALL_SPEED_SLOW,   1, LEVEL_TIME_STANDARD },   /*  6 */
    { 3, BALL_SPEED_MEDIUM, 1, LEVEL_TIME_STANDARD },   /*  7 */
    { 3, BALL_SPEED_FAST,   1, LEVEL_TIME_STANDARD },   /*  8 */
    { 3, BALL_SPEED_FAST,   1, 255 },                   /*  9: -15% */
    { 4, BALL_SPEED_SLOW,   1, LEVEL_TIME_STANDARD },   /* 10 */
    { 4, BALL_SPEED_MEDIUM, 1, LEVEL_TIME_STANDARD },   /* 11 */
    { 4, BALL_SPEED_MEDIUM, 2, LEVEL_TIME_STANDARD },   /* 12 */
    { 4, BALL_SPEED_FAST,   2, LEVEL_TIME_STANDARD },   /* 13 */
    { 4, BALL_SPEED_FAST,   2, 225 },                   /* 14: -25% */
    { 5, BALL_SPEED_SLOW,   2, LEVEL_TIME_STANDARD },   /* 15 */
    { 5, BALL_SPEED_MEDIUM, 2, LEVEL_TIME_STANDARD },   /* 16 */
    { 5, BALL_SPEED_MEDIUM, 2, 240 },                   /* 17: -20% */
    { 5, BALL_SPEED_FAST,   2, LEVEL_TIME_STANDARD },   /* 18 */
    { 5, BALL_SPEED_FAST,   2, 240 },                   /* 19: -20% */
    { 5, BALL_SPEED_FAST,   2, 195 },                   /* 20: -35% */
    { 5, BALL_SPEED_FAST,   1, 165 },                   /* 21: final */
};

uint8_t level_speed_frac(uint8_t speed) {
    if(speed == BALL_SPEED_MEDIUM) {
        return 2;   /* 4 + 4/5 px per frame = +20% */
    }
    if(speed == BALL_SPEED_FAST) {
        return 4;   /* 4 + 8/5 px per frame = +40% */
    }
    return 0;
}

uint16_t level_score(uint8_t percent) {
    uint16_t extra;

    if(percent < LEVEL_GOAL) {
        return 0;
    }
    if(percent > 100) {
        percent = 100;
    }

    extra = (uint16_t)((percent - LEVEL_GOAL) * 10u);

    if(percent >= 91) {
        return (uint16_t)(750u + extra * 3u);
    }
    if(percent >= 85) {
        return (uint16_t)(750u + extra * 2u);
    }
    return (uint16_t)(750u + extra);
}

static uint8_t parse_level_value(const char* text) {
    uint16_t value = 0;

    if(text == NULL) {
        return 0;
    }
    while(*text == ' ') {
        text++;
    }
    if(*text < '0' || *text > '9') {
        return 0;
    }
    while(*text >= '0' && *text <= '9') {
        value = (uint16_t)(value * 10u + (uint16_t)(*text - '0'));
        if(value > LEVEL_COUNT) {
            return 0;
        }
        text++;
    }
    if(value >= 1 && value <= LEVEL_COUNT) {
        return (uint8_t)value;
    }
    return 0;
}

uint8_t level_parse_arg(const char* arg) {
    const char* p = arg;

    if(p == NULL) {
        return 0;
    }

    /* Standard forms: "--level=N" and the value part of "--level N". */
    if(strncmp(p, "--level=", 8) == 0) {
        return parse_level_value(p + 8);
    }
    if(strcmp(p, "--level") == 0) {
        return 0;
    }

    /* Also accept a whole string such as "--level 7" for callers that pass a
     * single argument. */
    while(*p) {
        if(strncmp(p, "--level", 7) == 0) {
            const char* q = p + 7;

            while(*q == ' ' || *q == '=') {
                q++;
            }
            {
                const uint8_t value = parse_level_value(q);

                if(value != 0) {
                    return value;
                }
            }
        }
        p++;
    }

    return 0;
}

bool level_arg_given(int argc, char** argv) {
    int i;

    for(i = 1; i < argc; i++) {
        if(argv[i] != NULL &&
           (strcmp(argv[i], "--level") == 0 ||
            strncmp(argv[i], "--level=", 8) == 0)) {
            return true;
        }
    }
    return false;
}

uint8_t level_parse_args(int argc, char** argv) {
    int i;

    for(i = 1; i < argc; i++) {
        if(argv[i] == NULL) {
            continue;
        }
        if(strcmp(argv[i], "--level") == 0) {
            if(i + 1 < argc) {
                const uint8_t value = parse_level_value(argv[i + 1]);

                if(value != 0) {
                    return value;
                }
            }
            i++;
            continue;
        }
        {
            const uint8_t value = level_parse_arg(argv[i]);

            if(value != 0) {
                return value;
            }
        }
    }
    return 0;
}
