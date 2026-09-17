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

# Android 15+ devices use 16 KB memory pages and refuse to load 4 KB-aligned
# .so files (instant death before any app code runs, zero logs). Force 16 KB
# alignment for EVERYTHING here, including the SDL2/SDL_ttf subbuilds below
# (directory-scoped: inherited by add_subdirectory targets added after this).
add_link_options("-Wl,-z,max-page-size=16384")

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

# OpenHSP HSP3 VM core (no HSPLINUX/HSPNDK: HSPUTF8 must stay OFF for SJIS).
# supio is provided by ewdx (ewdx_supio.cpp); declarations are force-included
# (-include) because OpenHSP's own supio.h is empty without a platform macro.
set(OPENHSP_HSP3_DIR "../../../../../../OpenHSP/src/hsp3")
set(EWDX_DIR "../../../../../ewdx_ndk")
add_library(hsp3core STATIC
    "${OPENHSP_HSP3_DIR}/hsp3.cpp"
    "${OPENHSP_HSP3_DIR}/hsp3code.cpp"
    "${OPENHSP_HSP3_DIR}/hsp3debug.cpp"
    "${OPENHSP_HSP3_DIR}/hsp3int.cpp"
    "${OPENHSP_HSP3_DIR}/hspvar_core.cpp"
    "${OPENHSP_HSP3_DIR}/hspvar_int.cpp"
    "${OPENHSP_HSP3_DIR}/hspvar_str.cpp"
    "${OPENHSP_HSP3_DIR}/hspvar_double.cpp"
    "${OPENHSP_HSP3_DIR}/hspvar_label.cpp"
    "${OPENHSP_HSP3_DIR}/hspvar_struct.cpp"
    "${OPENHSP_HSP3_DIR}/stack.cpp"
    "${OPENHSP_HSP3_DIR}/strbuf.cpp"
    "${OPENHSP_HSP3_DIR}/strnote.cpp"
    "${OPENHSP_HSP3_DIR}/dpmread.cpp"
    "${OPENHSP_HSP3_DIR}/filepack.cpp"
    "${OPENHSP_HSP3_DIR}/hsp3crypt.cpp"
    "${OPENHSP_HSP3_DIR}/hsp3utfcnv.cpp"
    "${EWDX_DIR}/ewdx_supio.cpp"
)
target_include_directories(hsp3core PUBLIC
    "${OPENHSP_HSP3_DIR}"
    "${EWDX_DIR}"
    "../../../../../../SDL2/include"
)
target_compile_definitions(hsp3core PUBLIC HSP64 HSP_COM_UNSUPPORTED)
target_compile_options(hsp3core PRIVATE
    "-include${CMAKE_CURRENT_SOURCE_DIR}/${EWDX_DIR}/ewdx_supio_decl.h"
)
# strnote.cpp carries its own file-static strstr2: skip that one declaration.
set_source_files_properties("${OPENHSP_HSP3_DIR}/strnote.cpp" PROPERTIES
    COMPILE_DEFINITIONS EWDX_SUPIO_SKIP_STRSTR2
)

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
    "../../../../../ewdx_ndk/ewdx_boot.cpp"
    "../../../../../ewdx_ndk/ewdx_extcmd.cpp"
    "../../../../../ewdx_ndk/thirdparty/stb_vorbis.c"
    "../../../../../ewdx_ndk/thirdparty/stb_image_impl.c"
)
# Third-party decoders: silence warnings, slim to pull-from-memory only.
set_source_files_properties(
    "../../../../../ewdx_ndk/thirdparty/stb_vorbis.c" PROPERTIES
    COMPILE_OPTIONS "-w"
    COMPILE_DEFINITIONS "STB_VORBIS_NO_PUSHDATA_API;STB_VORBIS_NO_STDIO"
)
set_source_files_properties(
    "../../../../../ewdx_ndk/thirdparty/stb_image_impl.c" PROPERTIES
    COMPILE_OPTIONS "-w"
)
# 64-bit HSP VM (silences hsp3struct.h #warning; matches arm64 pointers).
target_compile_definitions(ewdx PUBLIC HSP64)
target_include_directories(ewdx PUBLIC
    "../../../../../ewdx_ndk"
    "../../../../../../SDL_ttf"
    # OpenHSP VM headers (hsp3code.h/hspvar_core.h) for register/ovplay/boot.
    # STEP-F part 1: headers only (probe + DLLFUNC/EXTCMD shims compile);
    # part 2 links the hsp3 core (hsp3code/var/stack/...) into libmain.
    "../../../../../../OpenHSP/src/hsp3"
)
target_link_libraries(ewdx PUBLIC SDL2 SDL2_ttf hsp3core)

# JNI entry shared lib. ewdx_main.cpp probes assets + inits the ewdx context;
# the hsp3 VM core exec loop attaches here in STEP-F part 2.
add_library(main SHARED
    main/ewdx_main.cpp
)
target_link_libraries(main
    ewdx
    SDL2
    GLESv2
    EGL
    OpenSLES
    log
    android
)
