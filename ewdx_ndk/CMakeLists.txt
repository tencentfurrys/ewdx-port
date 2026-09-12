# ewdx_ndk CMake fragment (merge into OpenHSP Android build).
# Destination in OpenHSP tree: src/plugins/ndk/ewdx/
# Requires: SDL2, GLESv2. NDK toolchain file from OpenHSP setup.sh.

cmake_minimum_required(VERSION 3.10)

add_library(ewdx STATIC
    ewdx_gles.cpp
    ewdx_dg.cpp
    ewdx_hspv.cpp
)

target_include_directories(ewdx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(ewdx
    SDL2
    GLESv2
    log
)

# Asset layout expected at runtime (verified against script init logic):
#   <filesDir>/save.dat, *.ini
#   <filesDir>/data/{map,mold,mot,music,pic,se}/
# Bundle the extracted game data/ tree + save.dat accordingly.
