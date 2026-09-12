# Native build: SDL2 + SDL_ttf + ewdx translation layer (+ hsp3 core).
# Layout assumed (sibling checkouts, see ewdx-port/refs.md):
#   <root>/SDL2            (libsdl-org/SDL, branch SDL2)
#   <root>/SDL_ttf         (libsdl-org/SDL_ttf, branch SDL2 + submodules:
#                           git -C ../SDL_ttf submodule update --init)
#   <root>/OpenHSP         (onitama/OpenHSP)
#   <root>/ewdx-port/ewdx_ndk
#   <root>/ewdx-port/android  (this project)
cmake_minimum_required(VERSION 3.22)

project(ewdx LANGUAGES C CXX)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
# NOTE: paths are relative to this file (ewdx-port/android/app/src/main/cpp/).
# SDL2/OpenHSP are sibling checkouts of ewdx-port (see refs.md).
add_subdirectory("../../../../../../SDL2" SDL2_build)

# SDL_ttf (freetype/harfbuzz come from its own external/ submodules).
# HARFBUZZ stays OFF (upstream default; game text needs no shaping).
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
add_subdirectory("../../../../../../SDL_ttf" SDL_ttf_build)

add_library(ewdx STATIC
    "../../../../../ewdx_ndk/ewdx_gles.cpp"
    "../../../../../ewdx_ndk/ewdx_dg.cpp"
    "../../../../../ewdx_ndk/ewdx_hspv.cpp"
    "../../../../../ewdx_ndk/ewdx_batch.cpp"
    "../../../../../ewdx_ndk/ewdx_register.cpp"
    "../../../../../ewdx_ndk/ewdx_audio.cpp"
    "../../../../../ewdx_ndk/ewdx_input.cpp"
    "../../../../../ewdx_ndk/ewdx_text.cpp"
    "../../../../../ewdx_ndk/ewdx_ovplay.cpp"
    "../../../../../ewdx_ndk/ewdx_paths.cpp"
    "../../../../../ewdx_ndk/thirdparty/stb_vorbis.c"
)
# Third-party decoder: silence warnings, slim to pull-from-memory + s16.
set_source_files_properties(
    "../../../../../ewdx_ndk/thirdparty/stb_vorbis.c" PROPERTIES
    COMPILE_OPTIONS "-w"
    COMPILE_DEFINITIONS "STB_VORBIS_NO_PUSHDATA_API;STB_VORBIS_NO_STDIO"
)
target_include_directories(ewdx PUBLIC
    "../../../../../ewdx_ndk"
    "../../../../../../SDL_ttf"
    # OpenHSP VM headers (hsp3code.h/hspvar_core.h) for ewdx_register/ovplay.
    # The HSP runtime core itself is NOT built here yet (STEP-F); the link
    # stays red on code_*/HspVar* until the hsp3embed boot lands.
    "../../../../../../OpenHSP/src/hsp3"
)
target_link_libraries(ewdx PUBLIC SDL2 SDL2_ttf)

# JNI entry shared lib. ewdx_main.cpp currently a stub that inits SDL and the
# ewdx context; the hsp3 VM core + step (c)/(d) runtime attach here next.
add_library(main SHARED
    main/ewdx_main.cpp
)
target_link_libraries(main
    ewdx
    SDL2
    GLESv2
    OpenSLES
    log
    android
)
