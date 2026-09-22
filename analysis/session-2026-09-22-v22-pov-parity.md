# Session 2026-09-22 (E) — v22 device verdict + POV interior comparison

> **CORRECTION (same session, after owner pushback):** the "parity" verdict
> below was measured on MIS-ALIGNED moments (the auto-detector latched onto
> countdown screens and a door scene). With eyes on matched contact sheets:
> the reference maw sphere shows the prey INSIDE (pink/red parts clearly
> visible); the port draws the same porthole as a flat black disc + white
> ring with NOTHING inside. The POV interior bug is REAL and still open.
> Also: the reference capture is a CLOUD PHONE run, not a "web edition".
> Session F continues with `~/Downloads/ewdx-v23-diag-maw.apk`
> (EWDX_MAW_JOURNAL: buffer-5 draw journal + 80×80 pixel readback).

Follows `session-2026-09-22-v22-rebuild-verify.md` (session D). Owner tested
v22 in MuMu (logs: 65/65 runs `build v22-2026-09-22-flag4-decompile-anchor`,
zero errors) and reported: **head attached, door seated — the flag&4 anchor
fix is CONFIRMED on device.** Remaining report: "POV animation is the only
thing still not showing, just like 2 parts."Evidence this session:
- `~/Documents/MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1..2).mp4`
  (960×540@30, v22) vs `~/Downloads/2026_09_22_17_00_09.mp4` (854×480@33,
  the owner-supplied CLOUD PHONE reference capture — NOT a web edition). Tools: `analysis/v22_pov_compare.py` + `v22_pov_align.py`
  (swallow-aligned), images + `report.html` in `analysis/v22_pov/`.

## Verdict: POV interior on v22 = reference parity

Phase-aligned at each video's swallow (t0), disc-region metrics:

| video | t0    | +2s dark/parts | +6s dark/parts/sat | longest hold |
|-------|-------|----------------|--------------------|--------------|
| ref   | 7.3s  | 0.60 / 1       | 0.45 / 2 / sat 142 | ~10.3s (then a 2nd hold w/ 6–7 parts) |
| mumu2 | 1.7s  | 1.00 / 0       | 0.47 / 1 / sat 100 | ~5.5s, parts 1→7→5 |
| mumu1 | 6.9s  | 0.80 / 0       | 0.79 / 0 / sat 32  | ~36s — NOT a POV hold, see below |

- The reference itself shows only **1–2 parts in the first ~5 s of a hold**
  (interior alpha `p_light` ramps the visibility) and more parts later
  (6–7 in its second hold). mumu2 shows the same shape: 0–1 early, 5–7 by
  +6–8 s. The owner's "just like 2 parts" matches the early-hold phase that
  the reference ALSO has — the port is not missing parts relative to the
  original.
- The interior is *dark* in both (bright fraction 0.3–0.7 %, sat 100–142):
  same `p_light`-driven look. "Weird colors" from v21 is gone — that was
  the misaligned interior parts (anchor bug), now seated.

## mumu1's 36 s "black disc" is a different scene, not a stuck POV

Full-frame dump at t=25 s (`v22_pov/mumu1_full_t025.jpg`): a bright room
(mean luminance 68) with a large X/door structure and a dark circular
object in the wall — the tracker followed that object, not a maw POV.
A 36 s hold would also exceed every hold in the reference (~10 s max).
If the owner intended that scene AS the POV, flag it and we dig again;
otherwise no bug.

## What would still be port-side (both currently refuted or parity)

- Blend table: untouched and verbatim vs FUN_10001d70 (mode 2 =
  SRCALPHA×ONE for the interior pass, mode 3 = invert-multiply for the
  plate). Do not swap (open-bug #1 stands).
- `p_light` curve: L7315 set 255 on swallow, decay −40/tick (L7626).
  The visible-duration differences between holds are script state, same
  bytecode as the reference. If the owner wants a MORE visible interior
  than the original, that would be a deliberate deviation (e.g. slower
  decay or higher floor) — needs an explicit owner request, not a "fix".

## Session log additions

- v22 device-confirmed (head/door). POV interior parity established with
  numbers; evidence in `analysis/v22_pov/` (+ `report.html`).
- Tooling: cv2/numpy/pillow reinstalled user-site on the fresh machine.
