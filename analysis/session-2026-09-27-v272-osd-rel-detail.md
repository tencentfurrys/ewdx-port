# Session 2026-09-27 (II) — v27.2: relative stick (touch "just going down") + detailed controls
Owner report (v27.1): "stop it's not fixed it's still not responding to my
touch just going down and can u add more detail to the controls glass
button" + a GameStop-brand controller photo as the detail reference.

## Bug: "not responding to my touch, just going down"
Root cause FOUND IN MY CODE (v27.1 ewdx_osd): the stick knob was positioned
ABSOLUTELY — knob = touch point relative to base center. Touching the lower
half of the stick base = knob jumps to full DOWN the moment the finger
lands (deadzone 6 px is tiny). "Just going down" is literally what the code
did. Any touch near the stick base forced the character downward; menu
taps near the pad could also grab the stick. (Separately: the v27.1 pad
itself may not have rendered on the owner's device at all — but the
touch-zone behavior alone reproduces the complaint exactly.)

Fix (v27.2): RELATIVE drag. Touch-down ANCHORS the grab point and centers
the knob; only the DRAG DELTA moves the knob (clamped to 16 px travel).
Landing on the lower half no longer means DOWN — you have to drag down.
Release recenters. This matches every real touch pad's behavior.

## Safety: menus release the pad
ewdx_osd_menu_seen() (fired every frame by the batcher's menu-row
signature) now also RELEASES ALL OSD EFFECTS (stick recenter, buttons
clear, MENU clear). A menu can never inherit a held direction/button from
gameplay, whatever state the pad was in. Touch slots survive (their
touch_up still clears) — no stuck finger ids.

## Observability (so "not responding" is diagnosable from logs)
- Every visibility transition journals: "osd: shown (gameplay)" /
  "osd: hidden (menu rows)" / "osd: hidden (physical pad)".
- Disc texture build journaled ("osd: disc texture built").
- Every OSD press/release journaled ("osd: press Z", "osd: stick grab
  anchor=(x,y)", "osd: stick release", "osd: pad connected/removed").
  Next time touch misbehaves, ewdx-boot*.log shows exactly what the OSD
  consumed and when.

## Detail pass (owner request, GameStop-pad reference)
draw_glass_button(): dark outer ring + colored glass body (brightens when
held) + offset white gloss highlight (physical-button sheen) + white mono
LETTER LABEL (Z/X/C/A, S/D) via a new cosmetic helper ewdx_text_label()
(own font size, does not disturb the game's font state; centered). The
stick gains a dark ring, a recessed center, a gloss dot on the knob.
MENU chip labeled "MENU". Diamond spacing widened (BTN_D 26->34) and
buttons slightly smaller (34->30) so the four glass bodies don't merge.

## Verification
- Syntax gate green (ewdx_osd/ewdx_text/ewdx_batch, zero warnings),
  assembleDebug green, tag v27.2-2026-09-27-osd-rel-detail verified in
  libmain.so.
- Owner test protocol:
  1. In game: touch the stick area WITHOUT dragging -> ship must NOT
     move. Drag up/down/left/right -> moves only while dragging.
  2. Menus: pad hidden, taps work (as v27.1).
  3. Logs: ewdx-boot*.log gains "osd:" lines — visibility transitions on
     menu entry/exit + every press/release. Send these if anything still
     feels wrong: they pinpoint whether a touch reached the OSD at all.
  4. Detail: buttons now show ring/gloss/letter like the reference pad.

## Shipped
- ~/Downloads/ewdx-v272-osd-rel-detail.apk (57,434,897 B)
- apks repo: pushed (token supplied by owner this session).
