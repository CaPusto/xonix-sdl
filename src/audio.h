#ifndef XONIX_AUDIO_H
#define XONIX_AUDIO_H

#include <SDL3/SDL.h>

#include "music.h"

#define AUDIO_RATE      44100

/* Sound effects are generated procedurally; the two music themes are OGG
 * files embedded in the executable and decoded by music.c. */
void audio_init(void);
void audio_shutdown(void);

/* Mute toggles: M / gamepad Y mutes both, Alt+S / Alt+M open the slider menu. */
void audio_toggle_mute(void);
bool audio_muted(void);             /* true only when both are muted */

void audio_toggle_sfx(void);
bool audio_sfx_muted(void);
void audio_toggle_music(void);
bool audio_music_muted(void);

/* Volume sliders, 0..100 percent. */
void audio_set_sfx_volume(uint8_t value);
uint8_t audio_sfx_volume(void);
void audio_set_music_volume(uint8_t value);
uint8_t audio_music_volume(void);

/* Main theme everywhere except the highscore screens, where the record theme
 * plays only if the current score qualified. */
void audio_set_music_track(MusicTrack track);

/* Stop gameplay effects (including the continuous spider buzz). */
void audio_stop_gameplay(void);

/* Gameplay effects. */
void audio_bounce(uint8_t alternate);
void audio_capture(void);
void audio_buzz_start(void);
void audio_buzz_stop(void);

/* Jingles. */
void audio_level_complete(void);
void audio_life_lost(void);
void audio_game_over(void);

#endif /* XONIX_AUDIO_H */
