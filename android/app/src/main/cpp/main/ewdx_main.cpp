// ewdx_main.cpp - SDL entry: bootstrap assets, boot the HSP VM, run it.
//
// The VM blocks in ewdx_boot_exec() until the script hits `end`/error;
// input pumping + buffer presents happen on this same thread via
// DIGETJOYSTATE/DGREDRAW, so no extra frame handler is needed.
//
// Crash visibility: every stage is journaled to filesDir/boot.log AND
// mirrored to Downloads/ewdx-boot.log (any file manager can open it).
// Failures are shown for 10 s (photographable) instead of vanishing.
#include <SDL.h>

#include <exception>

#include "ewdx_boot.h"
#include "ewdx_gles.h"
#include "ewdx_log.h"

#define EWDX_FAIL_HOLD_MS 10000

static void ewdx_fail_hold(const char *title, const char *body) {
    EWDX_LOGE("ewdx: %s", body);
    ewdx_boot_journal(body);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, body, NULL);
    SDL_Delay(EWDX_FAIL_HOLD_MS);  // stays up so it can be read/photographed
}

extern "C" int SDL_main(int argc, char *argv[]) {
    int rc = 1;
    (void)argc;
    (void)argv;

    // OpenHSP's file layer uses plain relative fopen: everything anchors at
    // filesDir, so init SDL/ewdx first (SDL_Init for JNI + GL).
    if (ewdx_init() == 0) {
        ewdx_fail_hold("EchidnaWarsDX", "ewdx_init failed (no GL context)");
        return 1;
    }
    ewdx_boot_crash_ui_init();  // signal handlers + previous-crash report
    ewdx_boot_journal("SDL init ok");
    // Early live surface: first-launch unpacking must not look dead.
    ewdx_boot_early_window();
    ewdx_boot_journal("window up");
    try {
        ewdx_boot_probe();
        ewdx_boot_journal("probe done");
        ewdx_boot_bootstrap();  // data/ + save.dat on first launch (device)
        ewdx_boot_journal("bootstrap done");
        if (!ewdx_boot_stage_ax()) {
            ewdx_fail_hold("EchidnaWarsDX",
                           "no start.ax (reinstall the full APK)");
            ewdx_shutdown();
            return 1;
        }
        ewdx_boot_journal("start.ax staged");
        if (ewdx_boot_chdir() != 0) {
            ewdx_fail_hold("EchidnaWarsDX", "chdir(filesDir) failed");
            ewdx_shutdown();
            return 1;
        }
        ewdx_boot_journal("chdir ok");
        if (ewdx_boot_startup() != 0) {
            ewdx_fail_hold("EchidnaWarsDX",
                           "VM startup failed (start.ax unreadable?)");
            ewdx_shutdown();
            return 1;
        }
        SDL_Log("ewdx: running start.ax ...");
        ewdx_boot_journal("exec begin");
        rc = ewdx_boot_exec();
        ewdx_boot_journal("exec end");
    } catch (const std::exception &e) {
        char msg[512];
        snprintf(msg, sizeof(msg), "C++ exception: %s", e.what());
        ewdx_fail_hold("EchidnaWarsDX", msg);
        rc = 1;
    } catch (...) {
        ewdx_fail_hold("EchidnaWarsDX", "unknown native exception");
        rc = 1;
    }
    ewdx_boot_bye();
    ewdx_boot_crash_clean();  // marks this run clean for the next launch
    ewdx_shutdown();
    SDL_Log("ewdx: exit (%d)", rc);
    return 0;
}
