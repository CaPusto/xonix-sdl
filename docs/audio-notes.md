# Audio notes

## Current implementation

- Sound effects are synthesized at runtime with SDL's audio stream. The stream
  is 44.1 kHz, mono, S16. Six voices are available; one is reserved for the
  continuous spider buzz.
- Two music themes are embedded in the executable as raw OGG bytes in
  `.rodata`. `music.c` decodes them from memory with stb_vorbis, the same
  decoder used internally by SDL3_mixer. There are no external audio files,
  temporary files, base64 payloads, or SDL3_mixer dependency.
- `main_theme_game.ogg` is the main theme. It plays on all screens except
  high-score screens. `xonix_hiscore.ogg` plays only when the result qualified
  for the top 10; otherwise high-score screens are silent. The old synthesized
  victory jingle was removed so it cannot overlap the high-score theme.
- Track changes use a 0.15 second crossfade. Both decoders keep running at
  zero gain when inactive, so switching back resumes from the current position.
- `Alt+S` / `Alt+M` open the `AUDIO` overlay with 0-100% sliders for SFX and
  music volume. Left/Right adjust, Up/Down select, and Enter/Esc close it. The
  game is frozen while the overlay is open.
- M or gamepad Y toggles hard mute for both channels. Default volumes are 75%
  for SFX and 70% for music.
- Audio continues to be generated while muted, so unmuting resumes the music
  from the current position instead of restarting it.

## Source layout

| File | Responsibility |
| --- | --- |
| `src/audio.c` | SDL audio stream, SFX voices, volume state, final mix and clipping |
| `src/audio.h` | Public audio API and `AUDIO_RATE` |
| `src/audio_menu.c` | AUDIO overlay and slider rendering |
| `src/music.c` | In-memory OGG decoding, looping, crossfade, per-track gain |
| `src/music_data.c` | Generated raw bytes of the two OGG files |
| `src/stb_vorbis.h` | Bundled public-domain Ogg Vorbis decoder |

`audio.c` calls `music_render()` for each audio block and multiplies the result
by the music volume. SFX voices are mixed separately and multiplied by the SFX
volume. The combined sample is clipped to +/-1.0 before conversion to S16.

## Regenerating embedded music

Replace the OGG files in `music/`, then run:

```sh
python3 tools/embed_music.py
```

The generator writes `src/music_data.h` and `src/music_data.c`. Rebuild after
regenerating; normal builds do not need Python.

## Implementation notes

- `stb_vorbis_get_samples_short_interleaved()` returns the number of frames per
  channel, not the number of shorts. Treating the return value as a short count
  skips every other block and makes the melody unrecognizable.
- The source OGG files are stereo at 44.1 kHz. Mixing down to the mono SDL
  stream happens in `music.c` after decoding.
- The main theme is a long loop. When a decoder reaches the end, `music.c`
  seeks back to the start and continues without a gap in the output.
- The high-score theme is short and does not need to loop during normal use;
  the same restart path is nevertheless applied to both sources.

## Possible future work

- Give jingles a priority or queue. The current sequence state is global, so a
  new short SFX can replace an earlier jingle.
- Add screen-specific variations, a tension layer near the time limit, and a
  separate fanfare for completing level 21.
- Add UI feedback for separate SFX/music mute states; currently M/Y toggles both
  together and the individual states are not shown in the HUD.
