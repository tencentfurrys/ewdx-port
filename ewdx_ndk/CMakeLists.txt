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
    ewdx_boot.cpp
    ewdx_extcmd.cpp
    thirdparty/stb_vorbis.c
)

# OpenHSP VM core (authoritative wiring: android/app/src/main/cpp/CMakeLists.txt).
# Compiled with -DHSP64 -DHSP_COM_UNSUPPORTED and -include ewdx_supio_decl.h
# (no HSPLINUX/HSPNDK: HSPUTF8 must stay OFF for the SJIS script):
#   hsp3.cpp hsp3code.cpp hsp3debug.cpp hsp3int.cpp
#   hspvar_core/int/str/double/label/struct.cpp
#   stack.cpp strbuf.cpp strnote.cpp dpmread.cpp filepack.cpp hsp3utfcnv.cpp
#   ewdx_supio.cpp (supio port: SJIS utils, POSIX fs, logcat Alerts)

# 64-bit HSP VM (silences hsp3struct.h #warning; matches arm64 pointers).
target_compile_definitions(ewdx PUBLIC HSP64)

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
