// ewdx_input.cpp - SDL event pump -> joyg bitmask (see header for layout).
//
// Touch scheme v12 ("UI touch", no on-screen buttons): the WHOLE screen is
// the touch surface; gestures map onto the game's joyg bitmask. The game's
// menus are bitmask-driven (arrows move the cursor, Z confirms, X cancels;
// the script never reads mousex/mousey/mstat), so touch->mouse emulation
// would be useless — gestures are the direct equivalent:
//   primary finger drag ...... direction bits while held (deadzone in game
//                              px, scaled to the real EGL surface)
//   primary finger tap ....... confirm (Z) latched on release, if the finger
//                              stayed within EWDX_TAP_PX for < EWDX_TAP_MS;
//                              consumed by the first buttons() read
//   second finger (anywhere) . cancel (X) while held
//   further fingers .......... ignored
// Keyboard (arrows/Z/X/C/A/S/D), gamepad and gestures all OR into the same
// mask, so every UI screen (title, start, config) and the game itself are
// fully touchable with no on-screen widgets.
#include "ewdx_input.h"
#include "ewdx_gles.h"  // viewport + scr_w/scr_h for game-px conversion

#include <SDL.h>
#include <string.h>

#define EWDX_MAX_FINGERS 8
#define EWDX_DEADZONE_PX 12.0f   // drag -> direction threshold (game px)
#define EWDX_TAP_PX 18.0f        // max movement that still counts as a tap
#define EWDX_TAP_MS 400          // max hold time that still counts as a tap
#define EWDX_AXIS_DEADZONE 8000

typedef struct {
    int used;
    SDL_FingerID id;
    float ox, oy;     // origin (down position, normalized)
    float x, y;       // current (normalized)
    int order;        // 0 = primary (gesture finger), 1+ = extra fingers
    unsigned down_ms; // SDL_GetTicks() at down (tap timing)
} EwdxFinger;

static EwdxFinger fingers[EWDX_MAX_FINGERS];
static int key_mask = 0;     // keyboard-contributed bits
static int pad_mask = 0;     // gamepad-contributed bits
static int tap_btn = 0;      // latched confirm from the last tap
static unsigned tap_ms = 0;  // when the tap was latched
static SDL_GameController *pad = NULL;

static int count_fingers(void) {
    int i, n = 0;
    for (i = 0; i < EWDX_MAX_FINGERS; i++)
        if (fingers[i].used) n++;
    return n;
}

static EwdxFinger *primary(void) {
    int i;
    for (i = 0; i < EWDX_MAX_FINGERS; i++)
        if (fingers[i].used && fingers[i].order == 0) return &fingers[i];
    return NULL;
}

// Primary-finger displacement converted to GAME px: finger coords are
// normalized against the FULL screen drawable, but game-space pixels
// (scr_w x scr_h) live inside the letterbox subrect — scale by the viewport
// so the deadzone keeps its meaning on any device resolution.
static void primary_drag_px(float *dx, float *dy) {
    float sw = (ewdx.scr_w > 0) ? (float)ewdx.scr_w : 640.0f;
    float sh = (ewdx.scr_h > 0) ? (float)ewdx.scr_h : 480.0f;
    float gx = 1.0f, gy = 1.0f;
    int dw = 0, dh = 0;
    EwdxFinger *f = primary();
    *dx = *dy = 0.0f;
    if (f == NULL) return;
    ewdx_surface_px(&dw, &dh);  // EGL ground truth (SDL lies on Android)
    if (dw > 0 && dh > 0 && ewdx.viewport[2] > 0 && ewdx.viewport[3] > 0) {
        gx = (float)ewdx.viewport[2] / sw;
        gy = (float)ewdx.viewport[3] / sh;
    }
    *dx = (f->x - f->ox) * sw * gx;
    *dy = (f->y - f->oy) * sh * gy;
}

static void finger_down(SDL_FingerID id, float x, float y) {
    int i, freei = -1, n = count_fingers();
    for (i = 0; i < EWDX_MAX_FINGERS; i++) {
        if (fingers[i].used && fingers[i].id == id) return;  // dup
        if (!fingers[i].used && freei < 0) freei = i;
    }
    if (freei < 0) return;
    fingers[freei].used = 1;
    fingers[freei].id = id;
    fingers[freei].ox = fingers[freei].x = x;
    fingers[freei].oy = fingers[freei].y = y;
    fingers[freei].order = n;  // 0 for the first finger down
    fingers[freei].down_ms = SDL_GetTicks();
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
            if (fingers[i].order == 0) {
                // Tap = primary finger, barely moved, short press -> confirm.
                float dx, dy;
                unsigned held = SDL_GetTicks() - fingers[i].down_ms;
                primary_drag_px(&dx, &dy);  // finger still marked used
                if (dx * dx + dy * dy < EWDX_TAP_PX * EWDX_TAP_PX &&
                    held < EWDX_TAP_MS) {
                    tap_btn = EWDX_JOY_BTN0;
                    tap_ms = SDL_GetTicks();
                }
            }
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
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                ewdx_apply_screen_viewport();  // re-fit letterbox (rotation)
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
    float dx, dy;
    // Tap latch: expires unread after 2 s (never leaves a stuck button),
    // otherwise consumed by the first read so menus see exactly one frame.
    if (tap_btn != 0 && SDL_GetTicks() - tap_ms > 2000) tap_btn = 0;
    m |= tap_btn;
    tap_btn = 0;
    primary_drag_px(&dx, &dy);
    if (dx < -EWDX_DEADZONE_PX) m |= EWDX_JOY_LEFT;
    if (dx > EWDX_DEADZONE_PX) m |= EWDX_JOY_RIGHT;
    if (dy < -EWDX_DEADZONE_PX) m |= EWDX_JOY_UP;
    if (dy > EWDX_DEADZONE_PX) m |= EWDX_JOY_DOWN;
    if (count_fingers() >= 2) m |= EWDX_JOY_BTN1;  // second finger = cancel
    return m;
}

// Verified branch behavior this mask must drive (script references):
//   label_198: DIGETJOYNUM!=0 -> DIGETJOYSTATE fills joyg (we return 1)
//   #deffunc joystick: bits0-3 = arrows, bits4+ = Z/X/C/A/S/D via joy() map
//   L27300 menus: joyg==0 idle / joyg==pow2(cnt+4) single-button advance
//   title/start/config: same bitmask scheme -> tap=Z walks them all
