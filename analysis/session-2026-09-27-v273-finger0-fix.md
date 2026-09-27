# Session 2026-09-27 (III) — v27.3: fingerId-0 sentinel bug (the pad never saw a press)
Owner report (v27.2): "still not working here logs in archive zip and screen
shots" — archive.zip (34 logs) + Wlog.zip (31) + 9 screenshots.

## The screenshots first (GOOD news, two things verified working)
- The pad RENDERS: visible in-game (red stick bottom-left, detailed glass
  diamond bottom-right with letters) and HIDDEN on the title menu (menu
  auto-hide works). The v27.1 visibility fixes are confirmed on device.
- Title menu now shows the bottom-right guide text at its correct place.

## The logs (the smoking gun)
All runs are v27.2. Journal lines:
- arch set: "osd: shown (gameplay)" + "osd: disc texture built" (pad works),
  one "osd: hidden (menu rows)" (title).
- Wlog set: **25-26x "osd: release MENU" / "release btn0..btn5" but ZERO
  "osd: press ..." and ZERO "osd: stick grab"**.

Releases with no presses = the press path never ran while the release path
matched everything. Root cause: **SDL_FingerID 0 is a VALID finger id on
Android** (pointer ids start at 0), and the OSD used `id == 0` as the
free-slot sentinel AND rejected `id == 0` presses outright. So:
1. touch_down with fingerId 0 (the FIRST finger — every first touch!) was
   rejected by the `id == 0` guard → the pad never registered a press →
   "not responding to my touch".
2. touch_up with fingerId 0 matched every free (0-filled) slot → the bogus
   "release" spam in the logs.
3. The consumed FINGERUP also never reached the gesture layer → its finger
   stayed down forever → whatever the last gesture direction was (DOWN)
   was held indefinitely = the persistent "just going down".
One sentinel mistake explains all three owner complaints across v27/v27.1/
v27.2 — and the osd: journaling added in v27.2 is exactly what exposed it.

## Fix (v27.3, tag v27.3-2026-09-27-finger0-fix)
- All OSD slots (stick, 6 buttons, MENU) track occupancy with an explicit
  `used` flag; SDL_FingerID 0 is accepted like any other id.
- touch_down: dup-check and slot-grab all use used&&id matching.
- touch_up: a release only clears a slot that is `used && id` — finger-0
  releases can no longer clear free slots.
- recompute_mask: a visible menu always zeroes the pad (belt-and-braces on
  top of menu_seen's release).
- Journal lines now include the finger id ("osd: stick grab id=0 ...").

## Verification
- Syntax gate green, assembleDebug green, tag verified in libmain.so.
- Owner test protocol:
  1. Tap the pad's Z button in game: "osd: press Z" must appear in the
     log and the ship must attack. (Previously: nothing at all.)
  2. Stick: touch without drag = no movement; drag = 8-way movement.
  3. After ANY touch anywhere, the ship must never keep sliding after
     release (the stuck-finger DOWN is gone).
  4. Menu taps work (v27.1 behavior intact).

## Shipped
- ~/Downloads/ewdx-v273-finger0-fix.apk (57,435,313 B)
- apks repo + master: pushed (owner token).
