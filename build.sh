#!/bin/sh
# Universal build driver for xonix-sdl.
#
#   ./build.sh                 # Linux, Release
#   ./build.sh linux           # same as above
#   ./build.sh windows         # Windows x86_64 via MinGW-w64 (cross or MSYS2)
#   ./build.sh macos           # macOS (must run on macOS)
#   ./build.sh all             # every target supported by this host
#
# Options:
#   --clean          remove the target's build and output directories first
#   --debug          build Debug instead of Release
#   --fetch-sdl      force CMake's bundled SDL3 download path
#   -j, --jobs N     parallel build jobs (default: CPU count)
#   -h, --help       show this help
#
# Outputs:
#   dist/linux/xonix-sdl
#   dist/windows/xonix-sdl.exe  (+ SDL3.dll when SDL3 is a shared library)
#   dist/macos/xonix-sdl
#
# Environment overrides:
#   TOOLCHAIN_FILE   CMake toolchain file to use for the Windows target
#   CMAKE_GENERATOR  CMake generator (e.g. Ninja)
#   MINGW_PREFIX     MSYS2 MinGW prefix, default /mingw64

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET=linux
BUILD_TYPE=Release
CLEAN=0
FETCH_SDL=0
DRY_RUN=0
TOOLCHAIN=${TOOLCHAIN_FILE:-}
JOBS=${JOBS:-}

usage() {
    sed -n '2,/^set -eu$/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        linux|windows|macos|all)
            TARGET=$1
            ;;
        --clean|-c)
            CLEAN=1
            ;;
        --debug|-d)
            BUILD_TYPE=Debug
            ;;
        --fetch-sdl)
            FETCH_SDL=1
            ;;
        --dry-run)
            DRY_RUN=1
            ;;
        -j|--jobs)
            shift
            [ "$#" -gt 0 ] || { echo "build.sh: missing value for $1" >&2; exit 2; }
            JOBS=$1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "build.sh: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [ -z "$JOBS" ]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS=$(nproc)
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 2)
    else
        JOBS=2
    fi
fi

print_cmd() {
    printf '%s' "$1"
    shift
    for arg in "$@"; do
        printf " '%s'" "$arg"
    done
    printf '\n'
}

run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        print_cmd "$@"
        return 0
    fi
    "$@"
}

windows_supported() {
    [ -n "$TOOLCHAIN" ] && return 0
    command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 && return 0
    case $(uname -s) in
        MINGW*|MSYS*) return 0 ;;
    esac
    return 1
}

build_target() {
    bt=$1
    build_dir="$ROOT/build/$bt"
    out_dir="$ROOT/dist/$bt"

    case "$bt" in
        macos)
            if [ "$(uname -s)" != Darwin ]; then
                return 2
            fi
            ;;
        windows)
            if ! windows_supported; then
                return 2
            fi
            ;;
    esac

    set -- -S "$ROOT" -B "$build_dir" \
           -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
           -DXONIX_SDL_OUTPUT_DIR="$out_dir"

    if [ -n "${CMAKE_GENERATOR:-}" ]; then
        set -- -G "$CMAKE_GENERATOR" "$@"
    fi
    if [ "$FETCH_SDL" -eq 1 ]; then
        set -- "$@" -DXONIX_SDL_FETCH_SDL3=ON
    fi

    case "$bt" in
        windows)
            if [ -z "$TOOLCHAIN" ] && \
               command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
                TOOLCHAIN="$ROOT/cmake/mingw-w64-x86_64.cmake"
            fi
            if [ -n "$TOOLCHAIN" ]; then
                set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
            else
                # Native MSYS2/MinGW SDL3 prefix.
                win_prefix=${MINGW_PREFIX:-/mingw64}
                if [ -d "$win_prefix" ]; then
                    set -- "$@" "-DCMAKE_PREFIX_PATH=$win_prefix"
                fi
            fi
            # A cross-built SDL3 installed under /usr/<triplet> is found via
            # CMAKE_FIND_ROOT_PATH, but its pkg-config prefix still helps when
            # SDL3Config.cmake lives in a non-standard location.
            if command -v x86_64-w64-mingw32-pkg-config >/dev/null 2>&1 && \
               x86_64-w64-mingw32-pkg-config --exists sdl3 2>/dev/null; then
                sdl_prefix=$(x86_64-w64-mingw32-pkg-config --variable=prefix sdl3)
                set -- "$@" "-DCMAKE_PREFIX_PATH=$sdl_prefix"
            fi
            ;;
        macos)
            # Homebrew keeps SDL3 outside the default CMake search path.
            if command -v brew >/dev/null 2>&1; then
                brew_prefix=$(brew --prefix sdl3 2>/dev/null || true)
                if [ -n "$brew_prefix" ]; then
                    set -- "$@" "-DCMAKE_PREFIX_PATH=$brew_prefix"
                fi
            fi
            ;;
    esac

    echo "==> $bt ($BUILD_TYPE) -> $out_dir"

    if [ "$CLEAN" -eq 1 ]; then
        if [ "$DRY_RUN" -eq 1 ]; then
            print_cmd rm -rf "$build_dir" "$out_dir"
        else
            rm -rf "$build_dir" "$out_dir"
        fi
    fi

    run cmake "$@"
    run cmake --build "$build_dir" --parallel "$JOBS"

    case "$bt" in
        windows) echo "    exe: $out_dir/xonix-sdl.exe" ;;
        *)       echo "    exe: $out_dir/xonix-sdl" ;;
    esac
}

if [ "$TARGET" = all ]; then
    failed=0
    for t in linux windows macos; do
        if build_target "$t"; then
            :
        else
            rc=$?
            if [ "$rc" -eq 2 ]; then
                echo "==> skipping $t: unsupported on this host"
            else
                failed=1
            fi
        fi
    done
    exit "$failed"
else
    if build_target "$TARGET"; then
        exit 0
    else
        rc=$?
        if [ "$rc" -eq 2 ]; then
            case "$TARGET" in
                windows)
                    echo "build.sh: MinGW-w64 not found." >&2
                    echo "Install it (e.g. 'sudo apt install mingw-w64') or set TOOLCHAIN_FILE." >&2
                    ;;
                macos)
                    echo "build.sh: macOS target must be built on macOS." >&2
                    ;;
            esac
            exit 1
        fi
        exit "$rc"
    fi
fi
