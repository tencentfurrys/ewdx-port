# Session 2026-09-27 (IV) — v27.4: pad sized to the SCREEN (too-small fix)
Owner report (v27.3): "ok yes it work now just the gamepad is too small"
+ screenshot: gameplay runs, pad visible bottom-left/right but small within
the letterboxed game view.

## Root cause
The pad geometry was defined in GAME px (640x480 space) and inherited the
game's letterbox scale: on the owner's 2400x1080 device the game view is
1440x1080 centered with pillarboxes, so "46 game px" stick = ~103 device px
— under 10% of the screen height, half of what a thumb pad needs.

## Fix (v27.4, tag v27.4-2026-09-27-osd-screen-size)
- All control geometry now derives from the REAL EGL drawable:
  stick base = 13% of screen height (1080 -> 140 px), glass buttons = 8.8%
  (~95 px), diamond offset 1.15x button radius, minis 55% of a button,
  MENU chip top-right 17%x7.5%. Clamped 44..150 / 32..110 so tiny/huge
  screens stay sane.
- Stick travel 38% of base radius; deadzone 14% (both scale).
- Drawing switched to the FULL drawable: the game letterbox viewport is
  saved and restored around the OSD pass (it previously clipped anything
  outside the game rect — irrelevant while the pad lived inside the game
  view, essential now that controls span the screen corners).
- NDC mapping = full-drawable ((2*px/dim-1), y mirrored); hit tests use the
  same screen-px rects (single source of truth).
- All touch math moved to screen px (anchor/delta/travel).

## Kept from v27.3 (verified working by owner: "it work now")
- used-flag occupancy (fingerId 0 valid), relative stick, menu release +
  auto-hide, osd: journaling, detail pass (ring/glass/gloss/labels).

## Verification
- Syntax gate green, assembleDebug green, tag verified in libmain.so.
- Owner test protocol: pad should now fill the bottom corners like a real
  handheld (stick ~13% of screen height); drag travel feels 1:1 with the
  visible knob; menu/physical-pad hiding unchanged; logs unchanged.

## Shipped
- ~/Downloads/ewdx-v274-osd-screen-size.apk (57,436,633 B)
- apks repo + master: pushed (owner token).
