# Session 2026-09-22 (C) — v21 device test: head-off/door-float diagnosed, v22 fix built

Follows `session-2026-09-22-v21-binary-reconstruction.md`. Owner tested v21 in
**MuMu emulator** (not a phone): 4 capture videos in
`~/Documents/MuMuSharedFolder/VideoRecords/EchidnaWarsDX(5..8).mp4` (960×540@30),
~160 fresh boot logs in `MuMuSharedFolder/Download/` (all confirm
`build v21-2026-09-22-pivot-recon`, `debag_mode forced 0 (vid=661)`, zero
errors — rendering-only issues).

## Owner symptoms (videos watched frame-by-frame, `analysis/v21_mumu/`)

1. **"Her head is off"** — vid8 t≈33–47 s: the hair dome is drawn shifted
   LEFT ~half its own width, detached from the body. Limbs fine (small
   parts → small error). "at least not all limbs are distorted, just the
   head" — matches a per-part error proportional to part width.
2. **"The door is way off and floating"** — same scene: the paw-print door
   quad floats above/next to the building; a rotated door sprite lands
   vertically when its translation error is rotated with it.
3. **"POV animation shows more but looks weird colors"** — the maw interior
   (same flag&4 path, drawn into buffer 5) has shifted parts; "weird
   colors" is consistent with misaligned interior art + the correct dark
   plate art, NOT a blend regression (boot log line confirms blend kept).

## Root cause (this time with ground truth in the repo)

The repo's own Ghidra dump `analysis/decomp_inner.txt` (FUN_10001fa0) shows
the REAL flag&4 branch. Algebra with the vertex setup (`+dw/2+dx` on every
x before the subtract) gives, for pivot-relative X,Y:

- scale about the rect CENTER, rotate, anchor **(pivx−0.5, pivy−0.5)**
  → at identity: `cx−0.5 / cy−0.5` (perfect placement).
- **v19 (old HEAD):** anchor x at `dx−0.5` + `X+dw/2` pre-offset → error
  `dw/2·(scale−1)` + rotation error → the v17–v19 "joints off" reports.
- **v20/v21 (binary reconstruction, shipped this morning):** scale pivot
  fixed but anchor left at `(dx−0.5, pivy−0.5)` → **constant `−dw/2` shift
  on every flag&4 part** → head (wide) flies left, door lands floating,
  small limbs barely move. EXACTLY the new reports. My 09-22B
  "verification" missed it because v20 and my build were identical — the
  reconstruction faithfully preserved v20's remaining bug.
- The plain (flag&4==0) branch anchors `(dx−0.5, dy−0.5)` (fVar10/fVar5 in
  the decompile); the port's plain branch was ALREADY verbatim-exact — an
  intermediate edit to it was caught by the numerical verifier and
  reverted before shipping.

## The fix (v22, `ewdx_ndk/ewdx_batch.cpp` ctr_anchor branch)

```
Xs = (X*scx)*c − (Y*scy)*s + (pivx − 0.5);
Ys = (X*scx)*s + (Y*scy)*c + (pivy − 0.5);
```

Numerical cross-check: `analysis/v22_pivot_verify.py` ports BOTH sides of
`decomp_inner.txt` (decompile + new emit_quad) and compares all 4 vertices
across flags {0,4} × scales {128..512} × angles {0..255} × 3 rects. The
ctr-anchor branch is exactly identity-equivalent at rest and
center-growing when scaled/rotated. (The plain-branch cases in the
current script still carry a stale test-side translation — the shipped
plain code path is the untouched, decompile-exact port form; see TODO
below.)

## Shipped

- **`~/Downloads/ewdx-v22-decompile-anchor.apk`** (57.4 MB, tag
  `v22-2026-09-22-flag4-decompile-anchor`, built 07:01 from this source).
- Boot journal records `fixes: flag4 anchor=pivot (verbatim decompile
  FUN_10001fa0; fixes head-off / door-shift); blend 3/4 kept`.

## TODO (next session, cut short by owner request)

1. Fix the verifier's plain-branch test math (stale decompile-side
   translation in `v22_pivot_verify.py` cases flags==0) and re-run to a
   clean 144/144 — the SHIPPED plain code is unchanged from the
   previously-verified form; only the test is stale.
2. Owner test of v22: head attached? door seated? POV interior coherent?
3. If POV colors still look off after v22, extract the same moment from
   vid(5..8) and compare interior part positions — the shift fix should
   also cure it; anything left would be a new, separate finding.
4. MuMu note: emulator = Adreno 640 @ 960×540, GLES 3.2 — logs healthy;
   device parity still worth one phone run eventually.

### Frame evidence (committed)

- `analysis/v21_mumu/vid(8)/` + `vid8_head/t0{33..47}` — head shift,
  floating door, full scene otherwise correct.
- `analysis/v21_mumu/vid5_sweep|vid6_sweep|vid7_sweep/` — other runs.
- `analysis/v21_mumu_frames.py` / `v21_mumu_dense.py` — extraction tools.
