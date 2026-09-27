# Session 2026-09-26 (III) — v27: newline text (chara-select garble) + on-screen gamepad
Owner report (v26.1): "ok that's fix but now there's this stuff look at image
and also u can add game pad I want red joystick and glass button like yell
blue red green form u know like a controller."

## Bug: chara-select guide garble (boxes + overprint strip)
Screenshot: CHARACTER SELECT shows `⊠⊠ orDodge ⊠⊠⊠⊠...` — a long horizontal
strip of .notdef boxes overprinting colored fragments, overlapping the
portrait; bottom shows "Sachiho Ohgami : ⊠⊠A wolf-girl martial artist..."

Root cause (script-verified): the guide strings embed REAL '\n' (0x0A) bytes.
*label_225 (chara select, L25622..) draws each character's guide as FOUR
draw_guide blocks per screen half:
- "[Z](aerial) :\n\n\nDOWN+[X] :" — move-name column
- "\n    Ariel Jump\n\n\n    V. Ring" — description column
- "[C] : V. Arrow\n" + "\n   Lv.1 > 3Way\n   Lv.2 > 5Way..." — weapon column
- "Sachiho Ohgami : \nA wolf-girl martial artist...\nWith her partner..." — bio
GDI TextOut honors \n as end-of-line (that is how the PC lays these columns
out). SDL_ttf Solid mode has NO newline support: the old path rendered the
whole string as ONE line — every 0x0A became .notdef (the boxes), and all
columns overprinted into one long strip. The slc_list_t slide animation
(±8px/step, 0..20) is the script's own; the strip was just unreadable.

Fix (ewdx_text.cpp): split on '\n', render each line through the existing
mono/NEAREST cache (per-line key = fnv1a(line)^size^rgba — full-string keys
are gone), advance y by TTF_FontLineSkip per row. Empty lines = vertical gap
only (the "\n\n\n" runs make the column rhythm). Hard cap 32 rows (script
max is 5). The cache now holds ~5 line entries per guide block instead of 1
huge strip — cache pressure is fine (16 entries, hot lines stay resident).

## Feature: on-screen gamepad (ewdx_osd.cpp/.h, new module)
Owner spec: "red joystick and glass button like yell blue red green form u
know like a controller". Layout (game-px space, scales with the letterbox):
- Joystick bottom-left: 46px base (dark translucent) + red knob, analog drag
  clamped to 14px travel, 6px deadzone, 8-way (diagonals = both bits).
- Buttons bottom-right in the classic diamond: red=Z right, yellow=X bottom,
  blue=C left, green=A top (34px glass discs), + S/D mini-buttons above.
- MENU chip top-right -> ESC (ewdx_osd_esc -> getkey 27/VK 27 in
  ewdx_extcmd -> the game's key_esc2 pause path). Never enters joyg (the
  script compares joyg by equality — contamination would break menus).
Design:
- One 128x128 radial-alpha disc texture (solid core + AA edge + glass rim)
  + per-control vertex tint, drawn with ewdx_immediate_quad (foreign-texture
  route) at present time — AFTER the letterbox bar clear, BEFORE the swap.
  Game draw-state (blend/color) restored around the draws.
- Hit tests share the exact drawn rects (single source of truth).
- Touch routing by SDL_FingerID (ewdx_input asks the OSD first; OSD-owned
  fingers never reach the gesture layer — no double-drive). One finger per
  control; stick + buttons simultaneously; multi-touch safe.
- Auto-hides while a physical SDL gamepad is connected
  (ewdx_osd_set_pad_connected from pad_open/poll + CONTROLLERDEVICEREMOVED).
- joyg bits OR into ewdx_input_buttons() (held-key contract, same as taps:
  visible to DIGETJOYSTATE + getkey + getkey2 in one frame).
- Non-Android builds compile to no-ops (EWDX_OSD_DISABLED) — host tests and
  the PC spy build are untouched.

## Also
- ewdx_osd_init() from ewdx_init (after GL attributes; disc texture builds
  lazily safe), ewdx_osd_shutdown() from ewdx_shutdown.
- CMakeLists (ewdx_ndk + android cpp) gain ewdx_osd.cpp.
- Build tag v27-2026-09-26-newlines-gamepad (verified in libmain.so).

## Verification
- Syntax gate green (aarch64 clang++ -fsyntax-only -DHSP64 -Wall -Wextra,
  zero warnings): ewdx_text/ewdx_osd/ewdx_input/ewdx_gles/ewdx_extcmd/
  ewdx_batch.
- assembleDebug green. APK 57,430,633 B, 590 data files + start.ax + save.dat.
- Owner test protocol:
  1. Character select: guide blocks render as COLUMNS (move names / weapon
     levels / bio) — no boxes, no overprint; bio at the 10px font size.
  2. Difficulty screen unchanged (still crisp mono).
  3. On-screen pad visible bottom corners; stick moves the cursor; buttons
     Z/X/C/A/S/D work on menus and in game; MENU pauses (ESC).
  4. Plug a USB/BT controller -> OSD hides; unplug -> OSD returns.
  5. Tap gestures still work on the untouched screen area (gestures and pad
     coexist: pad owns its rects only).

## Shipped
- ~/Downloads/ewdx-v27-newlines-gamepad.apk
- apks repo: committed locally (no token on this box — push pending, same
  as v26/v26.1).
