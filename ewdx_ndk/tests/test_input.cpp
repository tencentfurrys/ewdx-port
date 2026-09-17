// test_input.cpp - host verification for ewdx_input (synthetic SDL events).
//
// Covers: arrow/button scancodes + release clearing, full-screen gesture
// touch (v12): drag deadzone + directions + diagonals, tap latch (confirm,
// one read), drag cancels tap, second-finger cancel, key+touch combination,
// poll idempotence. Gamepad path contributes 0 with no device attached
// (implicitly asserted by exact mask equality throughout).
//
// Build (MinGW + host SDL2 static, from the workspace root):
//   g++ -std=c++17 -I ewdx-port/ewdx_ndk/tests/shim -I ewdx-port/ewdx_ndk
//       -I SDL2/include ewdx-port/ewdx_ndk/ewdx_input.cpp
//       ewdx-port/ewdx_ndk/tests/test_input.cpp <sdlbuild>/libSDL2.a
//       -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32
//       -lsetupapi -lversion -luuid -o test_input && ./test_input
#include <stdio.h>

#define SDL_MAIN_HANDLED  // host test provides plain main(); Android uses SDL_main
#include <SDL.h>

#include "ewdx_input.h"
#include "ewdx_gles.h"  // shim: scr_w/scr_h only

EwdxGles ewdx;

// shim stubs: input only needs scr_w/scr_h; GLES helpers are no-ops here
int ewdx_surface_px(int* w, int* h) { *w = ewdx.scr_w; *h = ewdx.scr_h; return (*w > 0 && *h > 0); }
void ewdx_apply_screen_viewport(void) {}

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL %d: %s (line %d)\n", failures, #cond, __LINE__); } \
} while (0)

static void push_key(Uint32 type, SDL_Scancode sc) {
    SDL_Event ev;
    ev.type = type;
    ev.key.keysym.scancode = sc;
    SDL_PushEvent(&ev);
}

static void push_finger(Uint32 type, SDL_FingerID id, float x, float y) {
    SDL_Event ev;
    ev.type = type;
    ev.tfinger.fingerId = id;
    ev.tfinger.x = x;
    ev.tfinger.y = y;
    SDL_PushEvent(&ev);
}

int main(void) {
    ewdx.scr_w = 640;
    ewdx.scr_h = 480;
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }

    // --- arrows ---
    push_key(SDL_KEYDOWN, SDL_SCANCODE_LEFT);
    push_key(SDL_KEYDOWN, SDL_SCANCODE_UP);
    CHECK(ewdx_input_poll() == -1);
    CHECK(ewdx_input_buttons() == (EWDX_JOY_LEFT | EWDX_JOY_UP));
    push_key(SDL_KEYUP, SDL_SCANCODE_LEFT);
    CHECK(ewdx_input_poll() == -1);
    CHECK(ewdx_input_buttons() == EWDX_JOY_UP);
    push_key(SDL_KEYUP, SDL_SCANCODE_UP);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);

    // --- action keys Z/X/C/A/S/D -> bits 4..9 ---
    push_key(SDL_KEYDOWN, SDL_SCANCODE_Z);
    push_key(SDL_KEYDOWN, SDL_SCANCODE_D);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == (EWDX_JOY_BTN0 | EWDX_JOY_BTN5));
    push_key(SDL_KEYUP, SDL_SCANCODE_Z);
    push_key(SDL_KEYUP, SDL_SCANCODE_D);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);
    // unrelated key contributes nothing
    push_key(SDL_KEYDOWN, SDL_SCANCODE_Q);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);
    push_key(SDL_KEYUP, SDL_SCANCODE_Q);
    ewdx_input_poll();

    // --- gesture drag: deadzone then right (whole screen, any origin) ---
    push_finger(SDL_FINGERDOWN, 1, 0.20f, 0.50f);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);  // finger down, no deflection, no tap yet
    push_finger(SDL_FINGERMOTION, 1, 0.205f, 0.50f);  // 3.2 px < 12
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);
    push_finger(SDL_FINGERMOTION, 1, 0.30f, 0.50f);  // 64 px right
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == EWDX_JOY_RIGHT);

    // --- diagonal: up+right ---
    push_finger(SDL_FINGERMOTION, 1, 0.30f, 0.40f);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == (EWDX_JOY_RIGHT | EWDX_JOY_UP));
    // far movement on release -> NOT a tap
    push_finger(SDL_FINGERUP, 1, 0.30f, 0.40f);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);

    // --- tap: short, still -> Z latch, consumed by one read ---
    push_finger(SDL_FINGERDOWN, 2, 0.50f, 0.50f);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);  // nothing while held
    push_finger(SDL_FINGERUP, 2, 0.50f, 0.50f);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == EWDX_JOY_BTN0);  // tap = confirm
    CHECK(ewdx_input_buttons() == 0);              // consumed

    // --- second finger = cancel (X), while held ---
    push_finger(SDL_FINGERDOWN, 7, 0.80f, 0.70f);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);              // primary idle, no tap yet
    push_finger(SDL_FINGERDOWN, 9, 0.85f, 0.30f);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == EWDX_JOY_BTN1);  // second finger cancels
    push_finger(SDL_FINGERUP, 7, 0.80f, 0.70f);
    ewdx_input_poll();
    // Primary lift after a <400 ms hold IS a tap (confirm) — by design;
    // cancel ended because only one finger remains.
    CHECK(ewdx_input_buttons() == EWDX_JOY_BTN0);
    // Second finger lifts after a quick hold: never a tap — only the
    // PRIMARY finger latches Z.
    push_finger(SDL_FINGERUP, 9, 0.85f, 0.30f);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);

    // --- key + gesture combine ---
    push_key(SDL_KEYDOWN, SDL_SCANCODE_DOWN);
    push_finger(SDL_FINGERDOWN, 3, 0.40f, 0.60f);
    push_finger(SDL_FINGERMOTION, 3, 0.40f, 0.30f);  // 96 px up
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == (EWDX_JOY_DOWN | EWDX_JOY_UP));
    push_finger(SDL_FINGERUP, 3, 0.40f, 0.30f);
    push_key(SDL_KEYUP, SDL_SCANCODE_DOWN);
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);

    // --- idempotent poll with empty queue ---
    ewdx_input_poll();
    CHECK(ewdx_input_buttons() == 0);

    SDL_Quit();
    printf("%s: %d checks, %d failures\n",
           failures ? "RESULT FAIL" : "RESULT PASS", checks, failures);
    return failures ? 1 : 0;
}
