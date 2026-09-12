# Native build: SDL2 + ewdx translation layer (+ hsp3 core in step b2/c).
# Layout assumed (sibling checkouts, see README):
#   <root>/SDL2        (libsdl-org/SDL, branch SDL2 -- already cloned)
#   <root>/OpenHSP     (onitama/OpenHSP -- already cloned)
#   <root>/Default Project/ewdx_ndk
#   <root>/Default Project/android  (this project)
cmake_minimum_required(VERSION 3.22)

project(ewdx LANGUAGES C CXX)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
add_subdirectory("../../../../SDL2" SDL2_build)

add_library(ewdx STATIC
    "../../../ewdx_ndk/ewdx_gles.cpp"
    "../../../ewdx_ndk/ewdx_dg.cpp"
    "../../../ewdx_ndk/ewdx_hspv.cpp"
    "../../../ewdx_ndk/ewdx_batch.cpp"
)
target_include_directories(ewdx PUBLIC
    "../../../ewdx_ndk"
)
target_link_libraries(ewdx PUBLIC SDL2)

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
