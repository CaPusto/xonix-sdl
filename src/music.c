/* Ogg Vorbis playback for the two embedded music themes.
 *
 * The OGG files are stored as raw bytes in the executable's read-only data
 * section (see music_data.c).  stb_vorbis - the same decoder SDL3_mixer uses
 * internally - decodes them directly from RAM: no external files, no temp
 * files, no base64 decoding and no extra DLLs.
 */
#define STB_VORBIS_IMPLEMENTATION
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.h"

#include <string.h>

#include "music.h"
#include "music_data.h"
#include "audio.h"      /* AUDIO_RATE: the tracks are mixed at the stream rate */

#define MUSIC_FRAMES_PER_BLOCK  1024
#define MUSIC_FADE_SECONDS      0.15f

typedef struct {
    stb_vorbis* decoder;
    int16_t interleaved[MUSIC_FRAMES_PER_BLOCK * 2];
    int frames_available;
    int frame_pos;
    float gain;
    float target_gain;
} MusicSource;

static MusicSource sources[2];       /* [MUSIC_TRACK_MAIN - 1], [HISCORE - 1] */
static float fade_step;

static bool open_source(MusicSource* source,
                        const unsigned char* data, size_t size) {
    int error = 0;

    source->decoder = stb_vorbis_open_memory(data, (int)size, &error, NULL);
    if(source->decoder == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                    "Cannot decode embedded music (stb_vorbis error %d)",
                    error);
        return false;
    }
    return true;
}

bool music_init(void) {
    fade_step = 1.0f / ((float)AUDIO_RATE * MUSIC_FADE_SECONDS);
    memset(sources, 0, sizeof(sources));

    if(!open_source(&sources[0], XONIX_MUSIC_MAIN, XONIX_MUSIC_MAIN_SIZE)) {
        return false;
    }
    if(!open_source(&sources[1], XONIX_MUSIC_HISCORE,
                    XONIX_MUSIC_HISCORE_SIZE)) {
        music_shutdown();
        return false;
    }
    sources[0].target_gain = 1.0f;  /* main theme starts immediately */
    sources[0].gain = 1.0f;
    return true;
}

void music_shutdown(void) {
    int i;

    for(i = 0; i < 2; i++) {
        if(sources[i].decoder != NULL) {
            stb_vorbis_close(sources[i].decoder);
            sources[i].decoder = NULL;
        }
        sources[i].gain = 0.0f;
        sources[i].target_gain = 0.0f;
    }
}

void music_set_track(MusicTrack track) {
    sources[0].target_gain = (track == MUSIC_TRACK_MAIN) ? 1.0f : 0.0f;
    sources[1].target_gain = (track == MUSIC_TRACK_HISCORE) ? 1.0f : 0.0f;
}

static void advance_gain(MusicSource* source) {
    if(source->target_gain > source->gain) {
        source->gain += fade_step;
        if(source->gain > source->target_gain) {
            source->gain = source->target_gain;
        }
    } else if(source->target_gain < source->gain) {
        source->gain -= fade_step;
        if(source->gain < source->target_gain) {
            source->gain = source->target_gain;
        }
    }
}

static float next_sample(MusicSource* source) {
    float left;
    float right;

    if(source->gain <= 0.0f || source->decoder == NULL) {
        return 0.0f;
    }
    if(source->frame_pos >= source->frames_available) {
        /* stb_vorbis returns frames PER CHANNEL, not the number of shorts.
         * Dividing by the channel count here skips half of the audio and makes
         * the melody unrecognizable. */
        source->frames_available = stb_vorbis_get_samples_short_interleaved(
            source->decoder, 2, source->interleaved,
            MUSIC_FRAMES_PER_BLOCK * 2);

        if(source->frames_available == 0) {
            /* The main theme is a loop; restart it transparently. */
            if(stb_vorbis_seek_start(source->decoder)) {
                source->frames_available =
                    stb_vorbis_get_samples_short_interleaved(
                        source->decoder, 2, source->interleaved,
                        MUSIC_FRAMES_PER_BLOCK * 2);
            }
        }
        source->frame_pos = 0;
    }
    if(source->frame_pos >= source->frames_available) {
        return 0.0f;
    }

    left = (float)source->interleaved[source->frame_pos * 2] / 32768.0f;
    right = (float)source->interleaved[source->frame_pos * 2 + 1] / 32768.0f;
    source->frame_pos++;
    return (left + right) * 0.5f * source->gain;
}

void music_render(float* output, int count) {
    int i;
    int track;

    for(i = 0; i < count; i++) {
        float sample = 0.0f;

        for(track = 0; track < 2; track++) {
            advance_gain(&sources[track]);
            sample += next_sample(&sources[track]);
        }
        output[i] = sample;
    }
}
