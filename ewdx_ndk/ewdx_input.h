// ewdx_input.h - DI* input layer: full-screen touch gestures + keys + pad.
//
// joyg bit layout, decoded from #deffunc joystick (start_ax_dump.hsp L945):
//   bit0 UP(38) bit1 DOWN(40) bit2 LEFT(37) bit3 RIGHT(39)
//   bits4.. buttons (default bit4=Z bit5=X bit6=C bit7=A bit8=S bit9=D,
//   remappable via the script's joy() array, which we honor by reporting
//   physical button indices the script maps itself).
//
// Sources (SDLActivity owns the NDK input queue; SDL delivers the events):
//   touch ..... WHOLE screen is the gesture surface (v12 "UI touch", no
//             on-screen buttons; the game's menus are bitmask-driven and
//             never read mousex/mousey/mstat, so gestures -> mask directly):
//             primary finger drag -> bits0-3 (arrows; game-px deadzone via
//             the letterbox viewport), primary TAP (<400 ms, <18 game px)
//             -> Z latch consumed by the next buttons() read, second
//             finger (anywhere) -> X/confirm-cancel while held.
//             Landscape logical 640x480/1280x960.
//   keyboard .. arrows -> bits0-3 (checked via getkey path too), Z/X/C/A/S/D
//             (scancodes, layout-independent) -> bits4-9.
//   gamepad ... first SDL_GameController: dpad+left stick -> bits0-3,
//             A/B/X/Y/shoulders -> bits4-9, START/BACK -> bits4/5.
// Mouse is deliberately ignored (Android touch-emulated mouse would
// double-drive the gestures).
//
// Pumping: ewdx_input_poll() drains the SDL queue; the register calls it on
// every DGREDRAW (frame boundary) and every DIGETJOYSTATE (input read), so no
// dish-loop changes are needed and worst-case latency is one frame.
#ifndef __EWDX_INPUT_H
#define __EWDX_INPUT_H

#define EWDX_JOY_UP 0x001
#define EWDX_JOY_DOWN 0x002
#define EWDX_JOY_LEFT 0x004
#define EWDX_JOY_RIGHT 0x008
#define EWDX_JOY_BTN0 0x010  // Z / confirm
#define EWDX_JOY_BTN1 0x020  // X / cancel
#define EWDX_JOY_BTN2 0x040
#define EWDX_JOY_BTN3 0x080
#define EWDX_JOY_BTN4 0x100
#define EWDX_JOY_BTN5 0x200

int ewdx_input_poll(void);     // drain SDL events, refresh state (-1 ok)
int ewdx_input_buttons(void);  // current 10-bit joyg mask

#endif
