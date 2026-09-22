# Session 2026-09-22 (B) — v20 fix reconstructed from binary, v21 shipped

Follows `session-2026-09-21-v20-bug-triage.md`. No Ghidra, no hmm.dll —
everything below was done with an NDK disassembler, the extracted tile, and
the web-reference capture video.

## 1. The lost v20 source: recovered by binary diff

The 09-21 triage warned: v20 exists only as `libmain.so` inside
`~/Downloads/ewdx-v20-flag4-fix.apk`; HEAD was v19-era. Rebuilding from HEAD
would have silently REGRESSED the joints fix the owner confirmed working.

Method (`analysis/v21_bindiff.py`, reusable):

1. Rebuilt HEAD with the same NDK (27.3.13750724) → `libmain_HEAD.so`
   (593,232 B vs v20's 593,200 B — 32 B apart, different but tantalizingly
   close; raw byte diff useless).
2. `llvm-objdump -d --no-show-raw-insn` both; parse into 1,201 functions
   each; normalize each instruction (branch targets → function-relative
   `f+NNN`, absolute data refs → `D`, symbol-suffixed refs → name only).
3. Walk both function lists in lockstep, resync on identical bodies.
   Result: **exactly ONE divergent function — `_Z15ewdx_copy_flagsii`**
   (DGGCOPY: `ewdx_copy_flags` + fully inlined `emit_quad`), 717 ins in
   HEAD vs 707 in v20.
4. Float-op-level diff of that function
   (`analysis/v21_funcdiff.py`, `func_HEAD.txt`/`func_v20.txt` in /tmp):
   ignoring register-allocation noise (x8↔x9 swaps, `[sp,#D]` vs
   `[x29,#-D]` slots, LUT base pointer choice), the ONE semantic delta is
   in the `ctr_anchor` (flag&4) branch:

```
HEAD (v19-era):            X4 = X + dw*0.5; Xsc = X4*scx; Ysc = Y*scy   // left-center pivot
v20 (shipped):             Xsc = X*scx;     Ysc = Y*scy                 // center pivot
(both then)                rotate (Xsc,Ysc) by LUT ang; anchor (dx-0.5, pivy-0.5)
```

   i.e. **the flag&4 scale now pivots at the rect center** — X,Y are
   already pivot-relative, so the fix is literally deleting the
   `X + dw*K_HALF` pre-offset. Applied verbatim to
   `ewdx_ndk/ewdx_batch.cpp` with full provenance comment.

5. **Verification:** rebuilt → `ewdx_copy_flags` is 707/707 instructions,
   identical modulo one stack-slot addressing mode; the fix is confirmed
   reconstructed (not approximated). v21 `libmain.so` differs from v20
   only by the new v21 additions (boot log lines, debag force, PLT
   slippage).

The v20 "pivot fix" mystery is now closed in source control where it
belongs. **Never diff-by-eye again — run `v21_bindiff.py`.**

## 2. Bug 1 (maw/POV): blend-swap hypothesis REFUTED, verdict = reference parity

Three independent pixel facts:

- The mask tile `system.bmp (242,72,40,40)` (`analysis/v21_facts.py`,
  `v21_tile_big.png`, ASCII dump in session log) is **WHITE OUTSIDE the
  disc radius, transparent INSIDE** (TL corner diagonal stair-step =
  disc edge; corners are opaque white). The 09-21 triage doc had it
  backwards ("white disc, black corners"). It is a QUARTER of the
  porthole mask; the four mirrored DGGCOPY 3/8/16/24 draws assemble the
  full 80×80 disc.
- Script sequence (decompile L24158–24186): buffer 5 = white clear →
  interior sprite (`view_mot`, alpha = decaying `p_light`) →
  `DGBLENDMODE 3` + 4× mask draws (paints OUTSIDE-disc area black,
  keeps interior) → buffer 1: white 80×80 rect → opaque `DGGCOPY 5,1`.
  With the port's CURRENT table (3 = ZERO×INVSRCCOLOR) this produces
  **a black square plate with a round window onto the interior** —
  exactly what BOTH the web reference capture AND the port show.
- `analysis/v21_dense/maw_*.jpg` (fresh 1-fps crops from the 09-19
  capture): the reference (web) edition shows the same black plate, dark
  disc, faint pink interior. t=42 s frame: no maw at all (maw only draws
  while `vore` active — explains "sometimes correct" impressions).

**Conclusion: the port's blend table is CORRECT; swapping 3/4 would have
made the maw worse (interior fully black).** The user-visible difference
("not showing much, some parts of body") is the interior alpha (`p_light`
decays −40/frame from 255 after swallow — late in the hold only ~20–30%
of the sprite shows) plus art. If v21 on device still looks emptier than
the web edition, the next lever is `p_light`/`p_light2` lifetime — NOT
`BLEND_SRC/DST`.

## 3. Bug 2 (stray lines): overlay theory refuted; likely reference parity too

- `debag_mode` (gates the `*label_232` cort/scope line overlay) is never
  assigned in the script AND the vendored VM zero-initializes every
  global (`HspVarCoreClear` at module-init — "グローバル変数を0にリセット").
  The overlay cannot appear on PC or port. (Force-0 at VM start kept in
  v21 as pure defense; it logs `debag_mode forced 0 (vid=N)`.)
- The triage doc's own evidence says the SAME thin diagonal line appears
  in the WEB capture at t≈42.8–43.6 s. Combined with all six `putline`
  call sites being intentional VFX, the default explanation is now:
  **the lines are game behavior, not a port bug.**
- Insurance shipped anyway: `EWDX_DGLINE_JOURNAL` compile-gated `[dgline]`
  logging (endpoints + color + blend per line, mirror of the v17
  `[dgcopy]` journal). If the owner reports lines the web edition does
  NOT show: flip the CMake flag, build, capture one repro, match against
  the documented call-site colors/blends.

## 4. v21 build + ship

- Build-tag `v21-2026-09-22-pivot-recon`; boot journal now also records
  `fixes: flag4 pivot=center (reconstructed from v20 binary diff); blend
  3/4 kept (verified vs web ref)`.
- APK: `~/Downloads/ewdx-v21-pivot-recon.apk` (57.4 MB; start.ax +
  save.dat + data/ staged from v20 + new libmain.so).
- Environment notes for reproducibility (see MEMORY.md build section):
  `local.properties` MUST use `C:/Android/android-sdk` (backslash form
  mangles through Java properties and kills gradle with a misleading
  "filename syntax" error); sibling checkouts belong in `~/Documents/`
  next to `ewdx-port/`; SDL_ttf submodules must be initialized.

## 5. Open items for next session

1. Owner device test of v21: joints still fixed? maw/lines compared
   against the web edition side by side?
2. If lines differ from reference → `[dgline]` journal repro run.
3. If maw interior still reads "too empty" → `p_light` lifetime audit
   (L7315/7532/7626: set 255 on swallow, decay −40/frame) — compare
   decay cadence with the web edition's visible duration.
4. Keep `~/Downloads/ewdx-v20-flag4-fix.apk` untouched as rollback.
