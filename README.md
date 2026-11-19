# xonix-sdl

A cross-platform Xonix game written in C11 with SDL3. It is based on the game
rules of [zeal-xonix](https://github.com/CaPusto/zeal-xonix); the original
Zeal 8-bit Computer specifics (tile system, Z80, ZOS/ZVB SDK) were removed.
The game uses a logical 960x600 resolution with letterboxed, nearest-neighbour
scaling. The internal grid is 240x150 cells of 4x4 pixels, with a 48-pixel HUD
at the top.

## Features

- 21 levels with the original GDD balance: 1-5 balls, 0-2 spiders, three ball
  speeds, timers, five lives, a 75% territory goal, and bonus multipliers at
  85% and 91%.
- Classic Xonix capture rules: a drawn line becomes land, and every water area
  unreachable by the balls is captured.
- Swept-area collision for balls, so they cannot pass through a one-cell-thick
  line.
- Keyboard, SDL gamepad, and raw joystick support (axes, hat, buttons).
- Procedurally synthesized sound effects through an SDL audio stream. Two music
  themes, `main_theme_game.ogg` and `xonix_hiscore.ogg`, are embedded as raw
  bytes and decoded from RAM with stb_vorbis. No external audio files,
  temporary files, base64 blobs, or SDL3_mixer are needed.
- The main theme plays on every screen except high-score screens. The high-score
  theme plays only when the result qualifies for the top 10; otherwise those
  screens are silent. The old synthesized victory jingle is not used. Track
  changes use a 0.15 second crossfade.
- `Alt+S` and `Alt+M` open the `AUDIO` overlay with separate SFX and music
  volume sliders. Left/Right adjust volume, Up/Down select a slider, and
  Enter/Esc close the overlay. The game is frozen while the overlay is open.
- Single-line 48-pixel HUD with lives, level, time, and territory percentage.
  Score, high score, and the mute hint were removed from the play field; the
  level score is shown in the between-level menu.
- All text is rendered with the bundled Oxanium Bold glyph atlas. The font is
  embedded in the binary, so no system font or SDL3_ttf is required.
- Between levels, a translucent menu shows the score earned on the completed
  level (`LEVEL SCORE`) and offers `CONTINUE` / `RESTART LEVEL`.
- A fade-in/fade-out splash screen uses the embedded
  `1980sRetroGamingRoomNostalgia960x600.png` image. It is dismissed by any key
  or gamepad button, or automatically after 25 seconds.
- Enemy balls are 24x24 pixels with a procedural pseudo-3D look. The player ball
  is a matching 16x16 yellow sphere. The 24x24 spider has eight jointed legs,
  a chitin body, four eyes, four animation frames, and rotates toward its
  movement direction.
- Full 32-bit RGBA rendering with per-frame gradients instead of a 16-color
  palette. `gfx_colors` are named code colors, not hardware palette entries.
- Top-10 high scores are saved to `HISCORE.DAT` through `SDL_GetPrefPath`:
  `~/.local/share/xonix-sdl/` on Linux,
  `~/Library/Application Support/xonix-sdl/` on macOS, and
  `%APPDATA%\xonix-sdl\` on Windows.
- After 12 seconds without input on the title screen, an attract demo starts.
  Its AI planner predicts ball and spider positions, picks a safer contour, and
  closes it early if an enemy approaches the trail. The demo runs for up to 30
  seconds, returns to the title screen, and never writes to the high-score
  table.
- After losing all lives, a `GAME OVER` screen shows the final score. Any key
  opens name entry if the result qualified for the top 10, followed by the
  high-score table.
- On the high-score screen, any key opens a popup with `GO TO SPLASH SCREEN`
  and `QUIT`. Use the arrow keys to select and Enter/A to confirm.
- The starting level can be set with `--level 7` or `--level=7`; the splash is
  skipped in that case.

## Controls

| Action | Keyboard | Gamepad / joystick |
| --- | --- | --- |
| Move | Arrow keys or WASD | D-pad or left stick / axes 0-1 |
| Select a name character | Left / Right | Left / Right |
| Type a character | Type text | A |
| Delete a character | Backspace | B |
| Confirm / continue | Enter / Space | Start / A |
| Mute all sound | M | Y |
| Open SFX volume menu | Alt+S | -- |
| Open music volume menu | Alt+M | -- |
| In the `AUDIO` menu: volume / selection / close | Left/Right, Up/Down, Enter or Esc | -- |
| Quit | Close the window, or `QUIT` in the high-score popup | -- |
| Toggle fullscreen | F11 | -- |

Esc quits from the title screen only when the `AUDIO` overlay is not open.

## Build

Requirements: a C11 compiler, CMake 3.20 or newer, and SDL3 3.2 or newer.
Python 3 is only needed to regenerate embedded assets; it is not required for
a normal build.

### One-command build

```sh
./build.sh                 # Linux, Release (default)
./build.sh linux           # same as above
./build.sh windows         # Windows x86_64 via MinGW-w64
./build.sh macos           # macOS (must run on macOS, SDL3 from Homebrew)
./build.sh all             # every target supported by this host

./build.sh linux --debug --clean -j 8
./build.sh windows --fetch-sdl
./build.sh --help
```

Outputs are kept in separate directories:

```
dist/linux/xonix-sdl
dist/windows/xonix-sdl.exe   # SDL3.dll is copied next to it
dist/macos/xonix-sdl
```

- **Linux**: install `cmake` and `libsdl3-dev` (or `sdl3-devel`).
- **Windows**: on Linux, install `mingw-w64` (`sudo apt install mingw-w64`).
  On Windows, run the script from an MSYS2 MinGW shell (`/mingw64`). If SDL3
  for MinGW is not found, CMake downloads and builds it automatically; the
  first build takes longer.
- **macOS**: install Homebrew with `sdl3` and `cmake`. The script sets
  `CMAKE_PREFIX_PATH` automatically.

Script options: `--clean`, `--debug`, `--fetch-sdl`, `--dry-run`, `-j N` /
`--jobs N`, and `-h`. Environment variables: `TOOLCHAIN_FILE` (custom Windows
toolchain), `CMAKE_GENERATOR` (for example `Ninja`), `MINGW_PREFIX` (default
`/mingw64`), and `JOBS`.

### Manual CMake build

CMake writes the executable to `bin/` in the source tree.

#### Linux

```sh
sudo dnf install sdl3-devel cmake gcc   # or: sudo apt install libsdl3-dev cmake gcc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./bin/xonix-sdl
```

#### macOS (Homebrew)

```sh
brew install sdl3 cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix sdl3)"
cmake --build build -j
./bin/xonix-sdl
```

#### Windows (vcpkg)

```powershell
vcpkg install sdl3:x64-windows
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
.\bin\xonix-sdl.exe
```

If SDL3 is not installed, CMake can download and build it:

```sh
cmake -S . -B build -DXONIX_SDL_FETCH_SDL3=ON
cmake --build build -j
```

## Embedded assets

The font atlas, splash image, Windows icon, and both OGG themes are generated
into C headers or source files and checked into the repository. Normal builds
do not need Python, and the game does not look for these assets at runtime.

After replacing a source asset, regenerate the corresponding file and rebuild:

```sh
python3 tools/generate_font.py        # requires Pillow
python3 tools/generate_splash.py
python3 tools/generate_icon.py        # requires Pillow
python3 tools/embed_music.py
./build.sh all
```

## Project structure

| Path | Purpose |
| --- | --- |
| `src/main.c` | SDL initialization, fixed-step 60 Hz game loop, screen handling |
| `src/ai.c`, `src/ai.h` | Attract-demo planner: contour search, enemy prediction, path choice |
| `src/field.*` | Field map, flood-fill territory capture, rendering |
| `src/game.*` | Player, balls, levels, timers, lives, HUD |
| `src/spider.*` | Spiders moving on captured land |
| `src/levels.*` | 21-level balance table, scoring, argument parsing |
| `src/input.*` | Keyboard, gamepad, joystick, text input |
| `src/audio.*` | Audio stream mixer, procedural effects, volume state, track switching |
| `src/audio_menu.*` | AUDIO overlay with volume sliders |
| `src/music.*` | In-memory OGG decoding, crossfade, looping |
| `src/music_data.*` | Generated raw OGG bytes in `.rodata` |
| `src/stb_vorbis.h` | Public-domain stb_vorbis v1.24 decoder |
| `src/gfx.*` | Font, sprites, pseudo-3D balls, animated spider, drawing primitives |
| `src/font_ui.h` | Generated Oxanium Bold alpha atlas |
| `src/splash.*` | Splash fade, 25-second timer, rendering |
| `src/splash_image.h` | Generated embedded splash PNG bytes |
| `src/stb_image.h` | Public-domain stb_image v2.32 decoder |
| `src/highscore.*` | High-score table and name/result screens |
| `src/config.h` | 960x600 screen and grid geometry |
| `assets/` | Source splash image, Windows icon, Oxanium font and license |
| `music/` | Source OGG files for the two music themes |
| `tools/` | Asset generators |
| `cmake/` | MinGW toolchain and Windows icon resource template |
| `docs/audio-notes.md` | Audio implementation notes |

## License

MIT. See `LICENSE`. Based on the game logic of
[CaPusto/zeal-xonix](https://github.com/CaPusto/zeal-xonix) (MIT, Copyright
(c) 2026 Eugene).

Oxanium font: Copyright (c) 2019 The Oxanium Project Authors, SIL Open Font
License 1.1. The full license text is in `assets/fonts/Oxanium-OFL.txt`.

stb_image (v2.32) and stb_vorbis (v1.24) by Sean Barrett and contributors are
released into the public domain (MIT-compatible).
