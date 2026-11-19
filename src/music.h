#ifndef XONIX_MUSIC_H
#define XONIX_MUSIC_H

#include <SDL3/SDL.h>

typedef enum {
    MUSIC_TRACK_NONE = 0,
    MUSIC_TRACK_MAIN,
    MUSIC_TRACK_HISCORE
} MusicTrack;

/* Decode the embedded OGG files once at startup. */
bool music_init(void);
void music_shutdown(void);

/* Switch themes with a short crossfade. Safe to call from any thread. */
void music_set_track(MusicTrack track);

/* Render `count` mono float samples of the currently audible theme(s). */
void music_render(float* output, int count);

#endif /* XONIX_MUSIC_H */
