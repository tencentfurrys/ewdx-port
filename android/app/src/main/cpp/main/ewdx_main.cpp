// ewdx_main.cpp - JNI entry stub. Grows into the runtime host in steps (b2)/(c)/(d).
// SDL2 Android entry: SDLActivity loads libmain.so and calls SDL_main().
#include <SDL.h>

extern "C" int SDL_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    // Step (b2): ewdx_init() + asset bootstrap (assets -> filesDir),
    // then hand off to the hsp3 VM running start_ax_dump-derived script.
    // Step (c)/(d): quad batcher, audio, input attach here.
    SDL_Log("ewdx: entry stub OK (runtime lands in b2/c/d)");
    return 0;
}
