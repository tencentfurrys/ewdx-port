# Session 2026-09-27 — v27.1: OSD invisible + menu tap-eating fixed
Owner report (v27): "does the virtual game pad appear then in game if so
thats ok it's just i can't touch the main menu it's just going down here"
+ LOgs.zip (93 ewdx-boot logs, ALL tag v27-2026-09-26-newlines-gamepad,
device 2400x1080 -> viewport [480,0 1440x1080], VM ready, zero errors —
the game itself is healthy).

## Two OSD bugs (both mine, both verified in the v27 code)

1. THE PAD WAS INVISIBLE. ewdx_osd_init() ran from ewdx_init() — BEFORE
   SDL_CreateWindow/GL context exist — and built the disc texture there:
   glGenTextures/glTexImage2D with NO current context = no texture (or a
   destroyed one). Every later draw_disc() early-returned on s_disc==0 →
   nothing drawn, ever. The owner never saw the pad.
2. ...BUT ITS TOUCH ZONES STILL ATE TAPS. Hit tests don't need GL, so the
   stick/diamond rects (game-px bottom corners) consumed every touch that
   landed near the bottom corners ON ANY SCREEN — including the title menu
   rows. That is the "can't touch the main menu": bottom-right menu rows
   sit exactly over the pad's diamond. (Second finger = X also fought the
   gestures there.)
3. Additionally the v27 NDC math was wrong (full-drawable mapping instead
   of the emit_quad target-dims convention), so even with a texture the
   controls would have drawn outside NDC range (0..2) — invisible again.

## Fixes (v27.1, tag v27.1-2026-09-27-osd-fix)
- ewdx_osd.cpp: disc texture builds LAZILY on the first osd_draw (GL
  context guaranteed current); ewdx_osd_init only resets state.
- NDC mapping now matches emit_quad exactly: game-px rect ->
  (px+0.5)/scr_w*2-1 (y mirrored); the GL viewport (still the game
  letterbox when the OSD draws at present time) does the scaling. The pad
  now renders locked to the game view, same pipeline as game sprites.
- MENU AUTO-HIDE (the owner's actual complaint): the batcher's
  ewdx_copy_flags reports the menu-row signature — id=8, 220x28 rect,
  unit scale, white, blend 1, target 4 (the title/options row art,
  decompile L29502/29506: DGPOS 360+t,200+cnt*45-20 + DGRECT
  220,138+c*28,220,28 + DGGCOPY 8) — via ewdx_osd_menu_seen(); the OSD
  hides for 1000 ms after the last sight. Menus redraw the signature
  every frame → pad stays hidden the whole time a menu is up; gameplay
  never matches (beams are additive, other ids) → pad appears in game.
  This ALSO answers the owner's question: yes, the pad shows up in-game.
- snap_geometry hardens the fallback path (s_dw/s_dh always set).

## Answer to the owner's question
"does the virtual game pad appear then in game" — YES: in v27 it was
supposed to (menus + game), but the texture bug made it invisible
everywhere while its touch zones still ate menu taps. In v27.1: hidden on
title/options menus (menu-row detector), visible + working in game.

## Verification
- Syntax gate green (5 TUs, zero warnings), assembleDebug green, tag
  v27.1-2026-09-27-osd-fix verified in libmain.so.
- Logs triaged: 93/93 v27 logs healthy (unpack/VM ready/viewport correct);
  no game-side regressions — the two defects were OSD-only.
- Owner test protocol:
  1. Title menu: NO pad visible; taps on GAME START..EXIT all work again.
  2. Start a run: pad fades in (stick bottom-left, diamond bottom-right,
     MENU top-right); stick moves the ship; Z attacks; MENU pauses.
  3. Pause menu (MENU button): pad hides again while the pause menu rows
     are up; ESC still registers (menu_seen fires from the menu rows, but
     the MENU button press already delivered ESC before hiding).
  4. Options screen: pad hidden; taps work.

## Shipped
- ~/Downloads/ewdx-v271-osd-fix.apk (57,431,057 B)
- apks repo: committed locally (push pending token, as v26/v26.1/v27).
