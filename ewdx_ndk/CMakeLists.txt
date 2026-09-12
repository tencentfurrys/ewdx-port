# ewdx_ndk CMake fragment (merge into OpenHSP Android build).
# Destination in OpenHSP tree: src/plugins/ndk/ewdx/
# Requires: SDL2, GLESv2. NDK toolchain file from OpenHSP setup.sh.

cmake_minimum_required(VERSION 3.10)

add_library(ewdx STATIC
    ewdx_gles.cpp
    ewdx_dg.cpp
    ewdx_hspv.cpp
    ewdx_batch.cpp
    ewdx_register.cpp
    ewdx_audio.cpp
    ewdx_input.cpp
    ewdx_text.cpp
    ewdx_ovplay.cpp
    ewdx_paths.cpp
    thirdparty/stb_vorbis.c
)

# Third-party decoder: silence warnings, slim to pull-from-memory + s16.
set_source_files_properties(thirdparty/stb_vorbis.c PROPERTIES
    COMPILE_OPTIONS "-w"
    COMPILE_DEFINITIONS "STB_VORBIS_NO_PUSHDATA_API;STB_VORBIS_NO_STDIO"
)

target_include_directories(ewdx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
    # + SDL_ttf root (for SDL_ttf.h): the consuming build adds it --
    # see android/app/src/main/cpp/CMakeLists.txt.
)

target_link_libraries(ewdx
    SDL2
    GLESv2
    OpenSLES
    log
)

# Asset layout expected at runtime (verified against script init logic):
#   <filesDir>/save.dat, *.ini
#   <filesDir>/data/{map,mold,mot,music,pic,se}/
# Bundle the extracted game data/ tree + save.dat accordingly.
