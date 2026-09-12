// ewdx_main.cpp - SDL entry: bootstrap assets, boot the HSP VM, run it.
//
// The VM blocks in ewdx_boot_exec() until the script hits `end`/error;
// input pumping + buffer presents happen on this same thread via
// DIGETJOYSTATE/DGREDRAW, so no extra frame handler is needed.
#include <SDL.h>

#include "ewdx_boot.h"
#include "ewdx_gles.h"
#include "ewdx_log.h"

extern "C" int SDL_main(int argc, char *argv[]) {
    int rc;
    (void)argc;
    (void)argv;

    // OpenHSP's file layer uses plain relative fopen: everything anchors at
    // filesDir, so init SDL/ewdx first (SDL_Init for JNI + GL).
    if (ewdx_init() == 0) {
        EWDX_LOGE("ewdx: ewdx_init failed (no GL context)");
        return 1;
    }
    ewdx_boot_probe();
    ewdx_boot_bootstrap();  // data/ + save.dat on first launch (device)
    if (!ewdx_boot_stage_ax()) {
        EWDX_LOGE("ewdx: no start.ax (see logcat for staging help)");
        ewdx_shutdown();
        return 1;
    }
    if (ewdx_boot_chdir() != 0) {
        ewdx_shutdown();
        return 1;
    }
    if (ewdx_boot_startup() != 0) {
        EWDX_LOGE("ewdx: VM startup failed");
        ewdx_shutdown();
        return 1;
    }
    SDL_Log("ewdx: running start.ax ...");
    rc = ewdx_boot_exec();
    ewdx_boot_bye();
    ewdx_shutdown();
    SDL_Log("ewdx: exit (%d)", rc);
    return 0;
}
