# Session 2026-09-26 (I) — PC squeeze ground truth captured (spy works)

Owner request: use the PC debug path (spy on hmm.dll) to capture the squeeze
"heartbeat" animation, because the Android port shows it as a side-to-side
"slap" while the PC pulses like a heartbeat.

## What was built and fixed

- `tools/hmm_spy/hmm.dll` **rebuilt from `gen_spy_c.py`** — two fixes, now in
  the generator:
  1. **All 114 exports are stdcall(4 ints) with decorated names.** The old
     generator emitted `DGCLEAR/DGDRAWPRIMITIVE/DGREDRAW` as bare `(void)`
     exports; the AX binds every hmm call as `_NAME@16` with 4 int pushes,
     so those three never resolved → the game died with HSP #Error 38 on the
     first `DGCLEAR`. (Evidence: the `#func` lines in `start_ax_dump.hsp`.)
  2. **`.def` file pins the decorated export names.** zig/LLVM strips the
     leading `_` on `@16` exports (`DGCLEAR@16` instead of `_DGCLEAR@16`);
     `hmm_spy.def` maps `real@16 = Cname@16` so all 114 keep their
     underscore. Verified with `objdump -p` (114 hits for `_[A-Z0-9]+@16`).
- Build command that works on this box (no zig needed, but same pipeline):
  `python -m ziglang cc -target x86-windows-gnu -shared -O2 -o hmm.dll hmm_spy.c hmm_spy.def`
- `tools/pc_run/launch.py` + `tools/pc_run/gamectl.py`: detached game
  launcher (survives tool timeouts; DETACHED_PROCESS) and window controller
  (move/focus/grab/tap via SendInput; PrintWindow returns black on this D3D9
  window — use `grab` = PIL ImageGrab of the window rect instead).
- Game copy: `game_pc/` = the Downloads v1.11 tree with `hmm.dll` (spy) +
  `hmm_real.dll` (original renamed). Downloads folder untouched.

## Driving the game to the squeeze (gallery path, no gameplay needed)

- Sticky Keys popup (5×Shift) kept interrupting: disabled via
  `SystemParametersInfoW(SPI_SETSTICKYKEYS)` and killed
  `EaseOfAccessDialog.exe` (PID was stuck on-screen; needed admin).
- Title → GALLERY (Down×3 + Z) → monster select → DELTA (Left×6 from Honey
  Bee) → Z → motion list → SWALLOW (Down×3).
- The POV porthole needs `vore>0 & stom_n!=0 & stomach==1 & v_zoom_f==1`.
  Gallery sets `vore=pn` on entry; `stom_n` comes from `putstom` inside
  `*label_234` — **which returns immediately when `gl_play==0`**. My earlier
  toolbar Z press had hit index 0 = PAUSE (play/pause) and froze playback,
  so the composite never fired. Toolbar LEFT×12 + Z unpaused; then
  `DGGCOPY id=5 flags=0x1` started firing every frame (77 hits immediately).
- Toolbar map (gls_sel 0..12): 0 play/pause, 1 prev-motion(set+load),
  2 next-motion(set+load), 3 motion list nav, 4 playspeed, 5 chara,
  6 cloth_lv, 7 gl_state, 8 flip, 9 `v_zoom_f` (POV zoom), 10 `stomach`
  (porthole), 11 bgm, 12 pause-menu. v_zoom+stomach were already ON from
  the earlier session state.

## PC ground truth (spy log, DELTA swallow, playback 0.5x)

One full POV frame (in stream order, `game_pc/hmm_spy.log`):

1. `DGGSEL 6; DGCOLOR 0,0,0,256; DGCLEAR` — wobble staging clear
2. `DGTEXTURE 2` (player atlas), blend 1, white
3. `repeat part(5,...)` rows: `DGRECT rx, ry+row, 118, 1` /
   `DGSCALEANDANGLE 118+l_size_x, 1, 0` / `DGPOS (118+amp)/2+l_size_x/2, row+1`
   / `DGADDPRIMITIVE 7` — row 0 is 2px tall (`1+(cnt==0)`)
4. `DGDRAWPRIMITIVE`, `DGGSEL 5`, `DGRECT 0,0,134,94`
5. interior parts (id=7/34 copies into t=5) ...
6. `DGGSEL 5` mask quarters: `DGRECT 242,72,40,40` + `DGCOPY id=3`
   flags 0/8/0x10/0x18 at (0,0)/(40,0)/(0,40)/(40,40), blend 3
7. `DGGSEL 1` + `DGCOPY id=5 flags=0x1` (the composite onto the scene)
8. `[pc-dump] FLUSH ep=...` (spy ring flush per composite)

Row-0 (`y=1`) staging samples over time (dest_w = 118+l_size_x, pos_x =
(118+amp)/2+l_size_x/2 → **left edge fixed at 8**):

- 104 @60, 103 @60 (x13), 105 @61 (x8), 119 @67 (x8), 118 @67 (x8),
  104 @60 (x8), 103 @60 (x8), 105 @61 ...
- i.e. **the width quantizes to integers and holds for ~8-13 frames at
  0.5x playback, then steps** — the visible "heartbeat": the wall
  contracts/expands in place. Left edge never moves (pos_x only follows
  the width via the center formula: 60→61→67 are (w+amp)/2 for w=104,105,119
  with amp≈16/18/34 → the row stays centered on a FIXED axis).
- Same numbers appear in the port's own `[prim]` journals (session H:
  `dst=(67,1 119x1)`), so **the AX stream is identical on both sides**.

## VERDICT (probe-confirmed): the prim centering was the bug — v25.5 fixes it

A temporary spy build with a vertex-ring probe (read the real ctx prim ring
at ctx RVA 0x92cc8, ring base/idx at +0x1480/+0x1484, 6 verts x 0x1c after
each ADD) captured the REAL D3D9 quad for a wobble row:

```
[pc-maw] DGRECT 0 101 34 2
[pc-maw] DGSCALEANDANGLE 32 2 0        (raw dest w = 32 < src 34!)
[pc-maw] DGPOS 17 1
[pc-prim] DGADDPRIMITIVE flags=0x7
[pc-q] rect=(0,101 34x2) pos=(17.0,1.0) raw=(32.0,2.0) ang=0
       | L1.50 R33.50 T-0.50 B-0.50 | u 0.0000->0.1328 v 0.3945
```

L = 17 - 32/2 + 0.5 = 1.5, R = 17 + 32/2 + 0.5 = 33.5 — **the real DLL
centers flag&1 prims by the DEST size** (raw DGSCALEANDANGLE px when flag&2,
source*scale/256 otherwise), with the +0.5 D3D half-pixel at the edges.
The port's v25 ewdx_drawprim centered by the SOURCE rect size (dw), which
for the wobble rows (dest = src + sinusoid) slides BOTH edges by l/2
symmetrically = the Android "slap"; centering by dest keeps the outer edge
pinned = the PC "heartbeat" squeeze.

**v25.5 fix applied** to `ewdx_ndk/ewdx_batch.cpp` ewdx_drawprim:
dw_dst/dh_dst per FUN_10002550, centering subtracts dw_dst*K_HALF, scale
about the source pivot via emit_quad's pivx (matches the &4 pivot path).
Syntax gate green (aarch64-linux-android21-clang++ -fsyntax-only, SDL2 pin
re-cloned at ~/Documents/SDL per refs.md).

Also confirmed from the probe: ctx stores DGPOS as floats at 0x13ec/0x13f0
and raw scale at 0x13f4/0x13f8 (the [pc-q] fields read 0x41860000=17.0f),
and angle &0xff at 0x13fc.

NOTE: during probe runs the game hit HSP #Error 38 twice (once at gallery
entry with sampling 1-in-97, once with per-line flush + 1-in-7). Both times
the last log line was a mid-wobble-staging ADD; the probe reads/writes
nothing (pure reads of ctx+ring), so the likeliest cause is timing (the
per-line flush on the hot path) — the lean ring-flush spy (this final
build) ran the full swallow + POV WITHOUT the error in earlier sessions.
If it recurs with the lean spy, revisit; the probe has served its purpose.

## Static math parity (re-derived from binaries, not memory)

- `FUN_10001fa0` decompile (Ghidra, in `analysis/decomp_inner.txt`) + the
  live `FUN_10002550` (AddPrimitive vertex writer) disassembly both give:
  - flags&1: dest rect is centered: `x0 = pos - w_dest/2` (w_dest =
    src_w·scx when &2, else src_w·scx/256); with &2 the pos formula in the
    script makes left edge = (amp+l_size_x)/2 - (118+l_size_x)/2 = 8 FIXED.
  - &4 = ctr_anchor (rotate about rect center), &8/&0x10 = u/v flips.
  - Row 0 is 2px tall (rh = 1+(row==0)), matching the port.
- Port `ewdx_drawprim` (flag&2 dest dims + centering + emit_quad plain
  branch) matches all of the above numerically.
- Conclusion: **staging and expansion are bit-parity-correct on paper**;
  the "slap" is therefore NOT a per-quad math bug in the port's prim path.

## Remaining divergence hypothesis (what to fix/test on Android, v25.5)

1. **Temporal resolution of the wobble**: PC shows integer-quantized widths
   held for N frames (heart-beat steps). If the port advances `unit(12,pn)`
   frames or `l_phase` accumulation slightly differently (e.g. 60Hz vs
   30Hz tick, or playspeed applied twice), the rows would still match
   per-frame but the perceived motion becomes a fast slide instead of
   discrete pulses. Compare `[prim]` dest_w sequences frame-by-frame from
   an Android v25.4 log against the PC sequence above (104,103,105,119...).
2. **`fr = unit(12, pn)` frame advance during POV**: `p_light`/`fr` drive
   which sin() phase the rows use; any drift there changes the waveform.
3. If Android `[prim]` logs show fractional or non-stepping dest_w, the
   bug is in how the port stores DGSCALEANDANGLE (float vs int truncation).

## Artifacts (this machine)

- `game_pc/hmm_spy.log` — 500MB+ full stream (DELTA swallow + POV, hours)
- `/tmp/spy_tail.log` — last 20MB slice used for the analysis above
- `analysis/pc_squeeze.png` — screenshot of the live PC squeeze POV
- `analysis/pc_game_3.png` etc — navigation screenshots
- Spy log also proves: gallery idle traffic is id=5 fullscreen copies with
  flags=0 (NOT composites), and `DGREDRAW/DGEND` never appear per-frame
  (present chain is DGREDRAW only at frame end, [pc] journal).

## Next session checklist

1. Run Android v25.4 into the same DELTA swallow POV; capture ewdx-boot log.
2. Extract the `[prim]` dest_w/pos sequence; diff against the PC waveform
   (104,103,105,119,118 @ fixed left edge 8).
3. Fix the temporal divergence (likely playspeed/tick), rebuild v25.5,
   ship APK to Downloads.
