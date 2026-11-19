#include <math.h>
#include <string.h>

#include "audio.h"

#define SFX_VOICES      6
#define BUZZ_VOICE      (SFX_VOICES - 1)
#define BLOCK_SAMPLES   512

typedef enum {
    WAVE_SQUARE_12 = 0,
    WAVE_SQUARE_50,
    WAVE_TRIANGLE,
    WAVE_SAW,
    WAVE_NOISE
} Waveform;

typedef struct {
    uint8_t  active;
    uint8_t  waveform;
    uint8_t  buzz;
    float    phase;
    float    freq;
    float    volume;
    uint32_t position;
    uint32_t length;
} Voice;

typedef struct {
    float    freq;
    uint16_t duration_ms;
    uint16_t delay_ms;
    uint8_t  waveform;
    float    volume;
} SfxNote;

static SDL_AudioStream* audio_stream;
static SDL_Mutex* audio_lock;
static bool sfx_muted;
static bool music_muted;
static float sfx_volume = 0.75f;
static float music_volume = 0.70f;

static Voice sfx_voices[SFX_VOICES];
static Voice* buzz_voice;

static const SfxNote* sequence_notes;
static int sequence_count;
static int sequence_index;
static uint32_t sequence_delay;

static int next_sfx_voice;

static const SfxNote bounce_notes[] = {
    { 1200.0f, 60, 0, WAVE_SQUARE_12, 0.28f },
};
static const SfxNote bounce_notes_alt[] = {
    { 1500.0f, 60, 0, WAVE_SQUARE_12, 0.28f },
};
static const SfxNote capture_notes[] = {
    { 1319.0f, 80, 0, WAVE_TRIANGLE, 0.30f },
    { 1047.0f, 90, 80, WAVE_TRIANGLE, 0.26f },
};
static const SfxNote level_notes[] = {
    {  523.0f, 90,   0, WAVE_SQUARE_50, 0.22f },
    {  659.0f, 90,  90, WAVE_SQUARE_50, 0.22f },
    {  784.0f, 90, 180, WAVE_SQUARE_50, 0.22f },
    { 1047.0f, 200, 270, WAVE_SQUARE_50, 0.22f },
};
static const SfxNote life_notes[] = {
    { 440.0f, 80,   0, WAVE_SQUARE_50, 0.24f },
    { 349.0f, 80,  80, WAVE_SQUARE_50, 0.24f },
    { 262.0f, 220, 160, WAVE_SQUARE_50, 0.24f },
};
static const SfxNote over_notes[] = {
    { 330.0f, 180,   0, WAVE_TRIANGLE, 0.28f },
    { 294.0f, 180, 180, WAVE_TRIANGLE, 0.28f },
    { 262.0f, 380, 360, WAVE_TRIANGLE, 0.28f },
};

static uint32_t ms_to_samples(uint16_t ms) {
    return (uint32_t)ms * AUDIO_RATE / 1000u;
}

static void voice_trigger(Voice* voice, float freq, uint16_t duration_ms,
                          uint8_t waveform, float volume) {
    voice->active = 1;
    voice->waveform = waveform;
    voice->buzz = 0;
    voice->phase = 0.0f;
    voice->freq = freq;
    voice->volume = volume;
    voice->position = 0;
    voice->length = ms_to_samples(duration_ms);
    if(voice->length == 0) {
        voice->length = 1;
    }
}

static void play_sequence(const SfxNote* notes, int count) {
    if(audio_lock == NULL) {
        return;
    }
    SDL_LockMutex(audio_lock);
    sequence_notes = notes;
    sequence_count = count;
    sequence_index = 0;
    sequence_delay = 0;
    SDL_UnlockMutex(audio_lock);
}

static void start_buzz(void) {
    Voice* voice = &sfx_voices[BUZZ_VOICE];

    if(buzz_voice != NULL) {
        return;
    }
    voice->active = 1;
    voice->buzz = 1;
    voice->waveform = WAVE_SAW;
    voice->phase = 0.0f;
    voice->freq = 85.0f;
    voice->volume = 0.09f;
    voice->position = 0;
    voice->length = 0;
    buzz_voice = voice;
}

static void stop_voice(Voice* voice) {
    voice->active = 0;
    voice->buzz = 0;
    voice->position = 0;
    voice->length = 0;
    if(voice == buzz_voice) {
        buzz_voice = NULL;
    }
}

static float generate_wave(uint8_t waveform, float phase, uint32_t* noise) {
    switch(waveform) {
        case WAVE_SQUARE_12:
            return phase < 0.125f ? 1.0f : -1.0f;
        case WAVE_SQUARE_50:
            return phase < 0.5f ? 1.0f : -1.0f;
        case WAVE_TRIANGLE:
            return 1.0f - 4.0f * fabsf(phase - 0.5f);
        case WAVE_SAW:
            return 2.0f * phase - 1.0f;
        default:
            *noise = (*noise * 1664525u) + 1013904223u;
            return (float)(int16_t)(*noise >> 16) / 32768.0f;
    }
}

static float render_voice(Voice* voice, uint32_t* noise) {
    float sample;
    float envelope;
    float t;

    if(!voice->active) {
        return 0.0f;
    }

    sample = generate_wave(voice->waveform, voice->phase, noise);
    voice->phase += voice->freq / (float)AUDIO_RATE;
    while(voice->phase >= 1.0f) {
        voice->phase -= 1.0f;
    }

    if(voice->buzz) {
        /* Slow alternation between the two original buzz pitches. */
        voice->position++;
        if((voice->position & 8191u) == 0) {
            voice->freq = voice->freq > 95.0f ? 85.0f : 105.0f;
        }
        return sample * voice->volume;
    }

    t = (float)voice->position / (float)voice->length;
    if(t >= 1.0f) {
        stop_voice(voice);
        return 0.0f;
    }
    envelope = t < 0.05f ? t / 0.05f : 1.0f - (t - 0.05f) / 0.95f;
    voice->position++;
    return sample * envelope * voice->volume;
}

static void advance_sequence(void) {
    if(sequence_notes == NULL || sequence_index >= sequence_count) {
        return;
    }
    if(sequence_delay != 0) {
        sequence_delay--;
        return;
    }

    {
        const SfxNote* note = &sequence_notes[sequence_index];

        voice_trigger(&sfx_voices[next_sfx_voice], note->freq,
                      note->duration_ms, note->waveform, note->volume);
        next_sfx_voice = (next_sfx_voice + 1) % BUZZ_VOICE;
    }
    sequence_index++;
    if(sequence_index < sequence_count) {
        sequence_delay = ms_to_samples(sequence_notes[sequence_index].delay_ms);
    } else {
        sequence_notes = NULL;
    }
}

static void generate_samples(Sint16* output, int count) {
    static uint32_t noise = 0x12345678u;
    float music_buffer[BLOCK_SAMPLES];
    int offset;

    for(offset = 0; offset < count; offset += BLOCK_SAMPLES) {
        const int block = count - offset < BLOCK_SAMPLES
                          ? count - offset : BLOCK_SAMPLES;
        int i;

        /* Render even while muted so unmuting resumes the same music position. */
        music_render(music_buffer, block);
        for(i = 0; i < block; i++) {
            float sfx_sample = 0.0f;
            float sample;
            int v;

            advance_sequence();
            for(v = 0; v < SFX_VOICES; v++) {
                sfx_sample += render_voice(&sfx_voices[v], &noise);
            }

            sample = (music_muted ? 0.0f : music_buffer[i] * music_volume) +
                     (sfx_muted ? 0.0f : sfx_sample * sfx_volume);
            if(sample > 1.0f)  sample = 1.0f;
            if(sample < -1.0f) sample = -1.0f;
            output[offset + i] = (Sint16)(sample * 30000.0f);
        }
    }
}

static void audio_callback(void* userdata, SDL_AudioStream* stream,
                           int additional_amount, int total_amount) {
    Sint16 buffer[BLOCK_SAMPLES];
    int remaining;

    (void)userdata;
    (void)total_amount;

    if(audio_lock == NULL || additional_amount <= 0) {
        return;
    }

    SDL_LockMutex(audio_lock);
    remaining = additional_amount / (int)sizeof(Sint16);
    while(remaining > 0) {
        const int block = remaining < BLOCK_SAMPLES ? remaining : BLOCK_SAMPLES;

        generate_samples(buffer, block);
        if(!SDL_PutAudioStreamData(stream, buffer, block * (int)sizeof(Sint16))) {
            break;
        }
        remaining -= block;
    }
    SDL_UnlockMutex(audio_lock);
}

void audio_init(void) {
    SDL_AudioSpec spec;

    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = AUDIO_RATE;

    audio_lock = SDL_CreateMutex();
    next_sfx_voice = 0;
    sfx_muted = false;
    music_muted = false;
    sfx_volume = 0.75f;
    music_volume = 0.70f;

    if(!music_init()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                    "Embedded music could not be decoded; playing silently");
    }

    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                             &spec, audio_callback, NULL);
    if(audio_stream == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                    "Audio unavailable: %s (the game will run silently)",
                    SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(audio_stream);
}

void audio_shutdown(void) {
    if(audio_stream != NULL) {
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
    }
    if(audio_lock != NULL) {
        SDL_DestroyMutex(audio_lock);
        audio_lock = NULL;
    }
    memset(sfx_voices, 0, sizeof(sfx_voices));
    buzz_voice = NULL;
    sequence_notes = NULL;
    music_shutdown();
}

void audio_toggle_mute(void) {
    const bool mute_all = !audio_muted();

    if(audio_lock == NULL) {
        return;
    }
    SDL_LockMutex(audio_lock);
    sfx_muted = mute_all;
    music_muted = mute_all;
    SDL_UnlockMutex(audio_lock);
}

bool audio_muted(void) {
    return sfx_muted && music_muted;
}

void audio_toggle_sfx(void) {
    if(audio_lock == NULL) {
        return;
    }
    SDL_LockMutex(audio_lock);
    sfx_muted = !sfx_muted;
    SDL_UnlockMutex(audio_lock);
}

void audio_toggle_music(void) {
    if(audio_lock == NULL) {
        return;
    }
    SDL_LockMutex(audio_lock);
    music_muted = !music_muted;
    SDL_UnlockMutex(audio_lock);
}

bool audio_sfx_muted(void) {
    return sfx_muted;
}

bool audio_music_muted(void) {
    return music_muted;
}

void audio_set_sfx_volume(uint8_t value) {
    if(value > 100) {
        value = 100;
    }
    if(audio_lock == NULL) {
        sfx_volume = (float)value / 100.0f;
        return;
    }
    SDL_LockMutex(audio_lock);
    sfx_volume = (float)value / 100.0f;
    SDL_UnlockMutex(audio_lock);
}

uint8_t audio_sfx_volume(void) {
    return (uint8_t)(sfx_volume * 100.0f + 0.5f);
}

void audio_set_music_volume(uint8_t value) {
    if(value > 100) {
        value = 100;
    }
    if(audio_lock == NULL) {
        music_volume = (float)value / 100.0f;
        return;
    }
    SDL_LockMutex(audio_lock);
    music_volume = (float)value / 100.0f;
    SDL_UnlockMutex(audio_lock);
}

uint8_t audio_music_volume(void) {
    return (uint8_t)(music_volume * 100.0f + 0.5f);
}

void audio_set_music_track(MusicTrack track) {
    if(audio_lock == NULL) {
        return;
    }
    SDL_LockMutex(audio_lock);
    music_set_track(track);
    SDL_UnlockMutex(audio_lock);
}

void audio_bounce(uint8_t alternate) {
    play_sequence(alternate ? bounce_notes_alt : bounce_notes, 1);
}

void audio_capture(void) {
    play_sequence(capture_notes, 2);
}

void audio_buzz_start(void) {
    if(audio_lock == NULL) {
        return;
    }
    SDL_LockMutex(audio_lock);
    start_buzz();
    SDL_UnlockMutex(audio_lock);
}

void audio_buzz_stop(void) {
    if(audio_lock == NULL || buzz_voice == NULL) {
        return;
    }
    SDL_LockMutex(audio_lock);
    if(buzz_voice != NULL) {
        stop_voice(buzz_voice);
    }
    SDL_UnlockMutex(audio_lock);
}

void audio_stop_gameplay(void) {
    int i;

    if(audio_lock == NULL) {
        return;
    }
    SDL_LockMutex(audio_lock);
    for(i = 0; i < BUZZ_VOICE; i++) {
        stop_voice(&sfx_voices[i]);
    }
    if(buzz_voice != NULL) {
        stop_voice(buzz_voice);
    }
    sequence_notes = NULL;
    sequence_index = 0;
    sequence_delay = 0;
    SDL_UnlockMutex(audio_lock);
}

void audio_level_complete(void) {
    play_sequence(level_notes, (int)(sizeof(level_notes) / sizeof(level_notes[0])));
}

void audio_life_lost(void) {
    play_sequence(life_notes, (int)(sizeof(life_notes) / sizeof(life_notes[0])));
}

void audio_game_over(void) {
    play_sequence(over_notes, (int)(sizeof(over_notes) / sizeof(over_notes[0])));
}
