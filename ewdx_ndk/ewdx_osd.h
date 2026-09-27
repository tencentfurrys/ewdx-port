// ewdx_osd.h - on-screen gamepad overlay (v27): touch controls drawn over the
// game at present time, hit-tested by the input layer.
//
// Layout (user spec): red analog-style joystick bottom-left; glass buttons in
// the classic 4-color diamond bottom-right (red=Z right, yellow=X bottom,
// blue=C left, green=A top) + S/D mini buttons above + MENU (ESC) top-right.
// joyg bits only (0..9) — never contaminates the game's equality comparisons.
//
// Touch routing is by SDL_FingerID: each control is grabbed by exactly one
// finger; move/up events re-route by id, so multi-touch (stick + buttons at
// once) works and OSD-owned fingers never reach the gesture layer.
// Auto-hides while a physical SDL gamepad is connected. Non-Android builds
// compile to no-ops (EWDX_OSD_DISABLED).
#ifndef __EWDX_OSD_H
#define __EWDX_OSD_H

#include <SDL.h>

// Called once after the GL context exists (builds the disc texture).
void ewdx_osd_init(void);
// Draw the overlay (ewdx_present calls this right before the swap).
void ewdx_osd_draw(void);
// Physical gamepad presence (input layer reports; OSD hides when true).
void ewdx_osd_set_pad_connected(int connected);

// Touch routing (SDL normalized coords, top-left origin). Each returns 1 if
// the event was consumed by the OSD (input layer then skips gesture handling
// for that finger id). Buttons ignore move; the stick uses it.
int  ewdx_osd_touch_down(SDL_FingerID id, float x, float y);
int  ewdx_osd_touch_move(SDL_FingerID id, float x, float y);
int  ewdx_osd_touch_up(SDL_FingerID id, float x, float y);

// Currently held OSD buttons in joyg bits (0..9), OR-combinable.
int  ewdx_osd_buttons(void);
// MENU button held (drives getkey ESC / VK 27; never enters joyg).
int  ewdx_osd_esc(void);

void ewdx_osd_shutdown(void);

#endif
