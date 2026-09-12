// ewdx_input.cpp - SDL event pump -> joyg bitmask (see header for layout).
#include "ewdx_input.h"
#include "ewdx_gles.h"  // scr_w/scr_h for touch geometry

#include <SDL.h>
#include <string.h>

#define EWDX_MAX_FINGERS 8
#define EWDX_DEADZONE_PX 12.0f
#define EWDX_AXIS_DEADZONE 8000

typedef struct {
    int used;
    SDL_FingerID id;
    float ox, oy;  // origin (down position, normalized)
    float x, y;    // current (normalized)
    int side;      // 0 = stick half, 1 = button half
    int order;     // arrival order among button-half fingers
} EwdxFinger;

static EwdxFinger fingers[EWDX_MAX_FINGERS];
static int key_mask = 0;    // keyboard-contributed bits
static int pad_mask = 0;    // gamepad-contributed bits
static SDL_GameController *pad = NULL;

static void finger_down(SDL_FingerID id, float x, float y) {
    int i, freei = -1, nbtn = 0;
    for (i = 0; i < EWDX_MAX_FINGERS; i++) {
        if (fingers[i].used && fingers[i].id == id) return;  // dup
        if (!fingers[i].used && freei < 0) freei = i;
        if (fingers[i].used && fingers[i].side == 1) nbtn++;
    }
    if (freei < 0) return;
    fingers[freei].used = 1;
    fingers[freei].id = id;
    fingers[freei].ox = fingers[freei].x = x;
    fingers[freei].oy = fingers[freei].y = y;
    fingers[freei].side = (x < 0.5f) ? 0 : 1;
    fingers[freei].order = (fingers[freei].side == 1) ? nbtn : 0;
}

static void finger_move(SDL_FingerID id, float x, float y) {
    int i;
    for (i = 0; i < EWDX_MAX_FINGERS; i++) {
        if (fingers[i].used && fingers[i].id == id) {
            fingers[i].x = x;
            fingers[i].y = y;
            return;
        }
    }
}

static void finger_up(SDL_FingerID id) {
    int i;
    for (i = 0; i < EWDX_MAX_FINGERS; i++) {
        if (fingers[i].used && fingers[i].id == id) {
            fingers[i].used = 0;
            return;
        }
    }
}

static int key_bit(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP: return EWDX_JOY_UP;
    case SDL_SCANCODE_DOWN: return EWDX_JOY_DOWN;
    case SDL_SCANCODE_LEFT: return EWDX_JOY_LEFT;
    case SDL_SCANCODE_RIGHT: return EWDX_JOY_RIGHT;
    case SDL_SCANCODE_Z: return EWDX_JOY_BTN0;
    case SDL_SCANCODE_X: return EWDX_JOY_BTN1;
    case SDL_SCANCODE_C: return EWDX_JOY_BTN2;
    case SDL_SCANCODE_A: return EWDX_JOY_BTN3;
    case SDL_SCANCODE_S: return EWDX_JOY_BTN4;
    case SDL_SCANCODE_D: return EWDX_JOY_BTN5;
    default: break;
    }
    return 0;
}

static void pad_open(void) {
    int i, n;
    if (pad != NULL) return;
    n = SDL_NumJoysticks();
    for (i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad != NULL) return;
        }
    }
}

static int pad_poll_bits(void) {
    int m = 0;
    Sint16 ax, ay;
    if (pad == NULL) return 0;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) m |= EWDX_JOY_UP;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) m |= EWDX_JOY_DOWN;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) m |= EWDX_JOY_LEFT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m |= EWDX_JOY_RIGHT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) m |= EWDX_JOY_BTN0;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) m |= EWDX_JOY_BTN1;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X)) m |= EWDX_JOY_BTN2;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y)) m |= EWDX_JOY_BTN3;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) m |= EWDX_JOY_BTN4;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m |= EWDX_JOY_BTN5;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) m |= EWDX_JOY_BTN0;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) m |= EWDX_JOY_BTN1;
    ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -EWDX_AXIS_DEADZONE) m |= EWDX_JOY_LEFT;
    if (ax > EWDX_AXIS_DEADZONE) m |= EWDX_JOY_RIGHT;
    if (ay < -EWDX_AXIS_DEADZONE) m |= EWDX_JOY_UP;
    if (ay > EWDX_AXIS_DEADZONE) m |= EWDX_JOY_DOWN;
    return m;
}

int ewdx_input_poll(void) {
    SDL_Event ev;
    pad_open();
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_FINGERDOWN:
            finger_down(ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y);
            break;
        case SDL_FINGERMOTION:
            finger_move(ev.tfinger.fingerId, ev.tfinger.x, ev.tfinger.y);
            break;
        case SDL_FINGERUP:
            finger_up(ev.tfinger.fingerId);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int b = key_bit(ev.key.keysym.scancode);
            if (b != 0) {
                if (ev.type == SDL_KEYDOWN) key_mask |= b;
                else key_mask &= ~b;
            }
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            pad_open();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            pad = NULL;  // next poll re-scans; stale handle never touched
            pad_mask = 0;
            break;
        default:
            break;
        }
    }
    pad_mask = pad_poll_bits();
    return -1;
}

int ewdx_input_buttons(void) {
    int m = key_mask | pad_mask;
    int i;
    float sw = (ewdx.scr_w > 0) ? (float)ewdx.scr_w : 640.0f;
    float sh = (ewdx.scr_h > 0) ? (float)ewdx.scr_h : 480.0f;
    for (i = 0; i < EWDX_MAX_FINGERS; i++) {
        if (!fingers[i].used) continue;
        if (fingers[i].side == 0) {
            float dx = (fingers[i].x - fingers[i].ox) * sw;
            float dy = (fingers[i].y - fingers[i].oy) * sh;
            if (dx < -EWDX_DEADZONE_PX) m |= EWDX_JOY_LEFT;
            if (dx > EWDX_DEADZONE_PX) m |= EWDX_JOY_RIGHT;
            if (dy < -EWDX_DEADZONE_PX) m |= EWDX_JOY_UP;
            if (dy > EWDX_DEADZONE_PX) m |= EWDX_JOY_DOWN;
        } else {
            // first button-half finger = Z/confirm, second = X/cancel
            if (fingers[i].order == 0) m |= EWDX_JOY_BTN0;
            else if (fingers[i].order == 1) m |= EWDX_JOY_BTN1;
        }
    }
    return m;
}

// Verified branch behavior this mask must drive (script references):
//   label_198: DIGETJOYNUM!=0 -> DIGETJOYSTATE fills joyg (we return 1)
//   #deffunc joystick: bits0-3 = arrows, bits4+ = Z/X/C/A/S/D via joy() map
//   L27300 menus: joyg==0 idle / joyg==pow2(cnt+4) single-button advance
