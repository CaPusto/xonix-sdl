cmake_minimum_required(VERSION 3.20)

project(xonix-sdl
    VERSION 1.0.0
    DESCRIPTION "Cross-platform SDL3 remake of Xonix"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(XONIX_SDL_FETCH_SDL3
       "Download and build SDL3 with FetchContent when it is not installed"
       OFF)

# ---------------------------------------------------------------------------
# SDL3: use an installed copy (vcpkg / Homebrew / system package), or fetch it.
# ---------------------------------------------------------------------------
if(NOT XONIX_SDL_FETCH_SDL3)
    find_package(SDL3 3.2 QUIET CONFIG)
endif()

if(NOT TARGET SDL3::SDL3)
    message(STATUS "SDL3 was not found; fetching SDL3 from GitHub")
    include(FetchContent)

    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(sdl3)
endif()

# ---------------------------------------------------------------------------
# Game
# ---------------------------------------------------------------------------
add_executable(xonix-sdl
    src/main.c
    src/ai.c
    src/ai.h
    src/audio.c
    src/audio_menu.c
    src/audio_menu.h
    src/config.h
    src/font_ui.h
    src/field.c
    src/field.h
    src/game.c
    src/game.h
    src/gfx.c
    src/gfx.h
    src/highscore.c
    src/highscore.h
    src/input.c
    src/input.h
    src/levels.c
    src/levels.h
    src/music.c
    src/music.h
    src/music_data.c
    src/music_data.h
    src/spider.c
    src/spider.h
    src/splash.c
    src/splash.h
    src/splash_image.h
    src/stb_image.h
    src/stb_vorbis.h
)

set(XONIX_SDL_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
    "Directory for the built executable")

set_target_properties(xonix-sdl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XONIX_SDL_OUTPUT_DIR}")

# Keep a single output directory for multi-config generators as well.
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(xonix-sdl PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${XONIX_SDL_OUTPUT_DIR}")
endforeach()

target_include_directories(xonix-sdl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(xonix-sdl PRIVATE SDL3::SDL3)

# ---------------------------------------------------------------------------
# Windows: GUI subsystem (no console window) and an embedded application icon.
# ---------------------------------------------------------------------------
if(WIN32)
    set_target_properties(xonix-sdl PROPERTIES WIN32_EXECUTABLE TRUE)

    enable_language(RC)
    set(XONIX_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/xonix.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xonix-icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/xonix-icon.rc"
        @ONLY)
    target_sources(xonix-sdl PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/xonix-icon.rc")
endif()

# powf()/fabsf() are used by the sound synthesis code.
if(NOT WIN32)
    target_link_libraries(xonix-sdl PRIVATE m)
endif()

if(MSVC)
    target_compile_options(xonix-sdl PRIVATE /W4)
else()
    target_compile_options(xonix-sdl PRIVATE -Wall -Wextra -Wpedantic)
endif()

# A shared SDL build needs its DLL next to the executable on Windows.
if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET xonix-sdl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3-shared>
                $<TARGET_FILE_DIR:xonix-sdl>
        COMMENT "Copying SDL3 DLL next to xonix-sdl.exe")
endif()

include(GNUInstallDirs)
install(TARGETS xonix-sdl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
