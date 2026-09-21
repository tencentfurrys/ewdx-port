# Session 2026-09-21 — v20 bug triage (POV black maw + stray lines)

User-reported on the v20 build (`v20-2026-09-20-flag4-pivot-fix`, APK =
`Downloads/ewdx-v20-flag4-fix.apk`), with capture video
(`2026_09_19_23_36_54.mp4`, 47.3 s, 480×854@28 fps screen recording of Chrome
playing `t-api.farmce.com`) and 96 boot logs (`20.zip`). NOTE: the video is
of the **web** (`t-api.farmce.com`) edition, used by the user as the reference
for how the maw/POV view should look; the Android APK logs are the device
evidence. This session = triage + evidence only; **no code changed yet.**

## IMPORTANT repo-state finding: v20 fixes are NOT committed

- Repo HEAD (`4411364`) is the v19-era commit. `git status` clean.
- The v20 APK's `libmain.so` reports `build v20-2026-09-20-flag4-pivot-fix`.
- Grep for `flag4|pivot|v20` in the repo tree: only the OLD flag&4 work from
  v17 (`ewdx_batch.cpp` emit_quad ctr_anchor branch) matches; no v20 marker.
- **A one-day-newer build exists only as a binary.** Before ANY future code
  change: diff v20's `libmain.so` behavior against HEAD or risk regressing
  the v20 fix. (Blender/disasm of the batcher path, or ask the owner to
  push the v20 source.)

## Evidence inventory (this folder)

- `frames_2026-09-21/` — 24 evenly spaced frames + `contact_sheet.jpg` from
  the capture video (timestamps burned in, 0.0 s → 47.3 s).
- Video fact sheet: zero scene cuts, avg motion 1.1/255, peak 21 — static
  phone, screen recording.

## Bug 1 — maw/POV renders as a BLACK BOX (primary)

### What the video shows

- t≈0–16 s: a large axis-aligned BLACK SQUARE with a ring-bordered dark disc
  inside; a faint pink sprite is visible deep inside the disc. The square's
  edges are razor-sharp (a quad, not a radial fade), the disc edge has a
  bright ring (the circle art in `system.bmp`).
- t≈21–44 s: the SAME stage renders CORRECTLY at times (searchlight, moon,
  full scene) — the bug is STATE-DEPENDENT/INTERMITTENT, not asset corruption.
- t≈46.5 s: CHARACTER SELECT (user quit to menu).

### Script mechanism (decompile, `artifacts/start_ax_dump.hsp`)

The vore/maw interior view is composed in BUFFER 5 (320×240):

1. `DGGSEL 5` + white `DGCLEAR` (L24144)
2. interior sprite drawn (`view_mot stom_n, 5`)
3. `DGBLENDMODE 3` + `DGRECT 242, 72, 40, 40` + `DGGCOPY 3` — the shading
   tile from `system.bmp` (L24164–24179), drawn 4× mirrored (offsets
   0,0 / 40,0 / 0,40 / 40,40 via `DGGCOPY 3, 8/16/24`)
4. back to buffer 1: white opaque 80×80 rect, `DGGCOPY 5, 1` scaled in
   (`stomach == 1` branch, L24177–24180)

The tile at `system.bmp` (242,72)-(282,112) was extracted this session
(`analysis/frames_2026-09-21/` pipeline, `system_tiles.html`): it is a
**quarter-disc mask — WHITE disc, BLACK corners** (diagonal stair-step
edge). Drawn 4× mirrored with the correct blend it shades the area OUTSIDE
the disc radius-40; the disc interior keeps buffer-5's white + interior
sprite. The player sees the maw interior THROUGH the disc.

### Root-cause hypothesis (strong)

**DGBLENDMODE 3/4 are swapped in the port** (`ewdx_gles.cpp` BLEND_SRC/
BLEND_DST):

- Port today: mode 3 = `(GL_ZERO, GL_ONE_MINUS_SRC_COLOR)` (invert-multiply),
  mode 4 = `(GL_ZERO, GL_SRC_COLOR)` (multiply).
- D3D9 enum order (D3DBLEND): ZERO=1 ONE=2 **SRCCOLOR=3 INVSRCCOLOR=4**
  SRCALPHA=5 INVSRCALPHA=6 … — if hmm.dll's switch is a plain jump table on
  the mode value (typical MSVC), mode 3 = `dst × src` (multiply) and
  mode 4 = `dst × (1−src)` (invert-multiply).
- With the swapped mapping, step 3 above inverts buffer 5: the WHITE disc →
  black; BLACK corners → white-outside… but then step 4 draws the white
  80×80 rect first and `DGGCOPY 5,1` composites the inverted result —
  matching the observed BLACK box + dark disc. The ring edge survives
  because the circle art's bright rim stays relatively bright under
  inversion of its neighborhood.
- Supporting evidence: mode 3 is also used for the boss-name glow
  (L26470: `DGBLENDMODE 3` + alpha-120 color, additive-looking intent) and
  the face-panel shade (L24671: `DGBLENDMODE 3` with `fb_r/g/b` at
  alpha 64 — reads like a multiply/darken, NOT an invert).
  Mode 4 uses: shadow pass (L3041 `sd` drop-shadow prims) and go-sign
  arrows (L25858+) — both make sense as invert-multiply/multiply depending
  on art; the maw tile is the discriminating case because we KNOW the tile
  pixels (white disc must NOT darken).

### Why the maw is sometimes fine later in the video

The maw only draws while `vore` is active. The correct-looking 21–44 s
segment is a different room/phase without the maw open (searchlight scene).
Intermittency = gameplay state, not a race.

### To CONFIRM before shipping a fix (cheap)

- Ghidra: decompile hmm.dll's `DGBLENDMODE` export (thunk → inner switch on
  mode) and READ the 8 jump targets' SetRenderState arg pairs. (Ghidra
  project `.rep` was not backed up; re-import hmm.dll per GUIDE §1.)
- Or host-test: render the extracted tile with both mappings over a known
  gradient and compare with a PC screenshot of the same scene.

## Bug 2 — stray line to the player ("lines that appear to player to enemies")

### What the video shows

- t≈42.8–43.6 s: a single thin dark DIAGONAL line crossing the right half
  of the scene, endpoint near the player, other end off toward the
  scenery/power-line area. 4× zoom saved (`line_zoom.html` pipeline).
- NOT the debug overlay: `*label_232` (cort collision lines + scope boxes,
  L28182) is gated by `debag_mode` (L24195) which is **never assigned in the
  whole 31k-line script** (single occurrence = the `if`) — and the staged
  `save.dat` does not contain it (parsed: only 19 legit vars: bgm/se/joy/
  guide/highscore/cleardif/open/wmode/wmode_w/v_zoom_f/free_mode/nude_on/
  guivib_on/gamename/englishmode/legacymode/stomach/cleared/clearlevel).
  HSP zero-inits globals, so on PC this overlay is dead code.
- The `string()`-array line system (`putline` L4058, drawn via `DGLINE` in
  the L24140 loop and `drawline` L3044) has 6 live call sites — ALL are
  intentional gameplay VFX:
  - L11242/11245/11248: beam/tether colors (190,255,120 / 190,230,255 /
    255,230,150) alpha 255, blend 2 — attack beams to `bex,bey`
  - L11518: (5,107,233) blue tether, blend 2
  - L15473: (255,230,240) alpha 200, blend 3 — boss grab tether to player
  - L15619: (255,220,240) alpha `255−20*rnd(8)` blend 1 — grab streaks

### Hypotheses (in order)

- H1: the L15473/L15619 grab tethers draw even when they shouldn't
  (stuck `vore`/`stom_n` state after the v20 flag4 change?) — the observed
  line is thin and scene-colored, consistent with an alpha-200 light line
  over dark BG.
- H2: `DGLINE` quad path bug — `ewdx_line` builds a 0.5px perpendicular
  quad; if `run_check` groups it with a stale blend/color from previous
  quads (line queue shares the batch), a line intended as a soft VFX could
  render as a hard dark line. Note `drawline`'s L24140 loop sets
  `DGCOLOR`+`DGBLENDMODE` per line, so state is right IF the batcher flushes
  on color change — **it does not** (color is per-vertex, fine) but blend is
  per-run (also flushed, fine). Lower probability.
- H3: it is the shadow pass (`DGBLENDMODE 4` prims, L3041) mis-projected by
  the v20 pivot change — would explain "line to enemies" if the sd-shadow
  quads degenerate into a streak when `sdsize<=6` clamps.
- Discriminator: v17 diag build (`EWDX_DGCOPY_JOURNAL`) logs every
  `[dgcopy]` + `[gsel]` but NOT `DGLINE`. Extend the journal to `[dgline]`
  (endpoints + color + blend) and capture one repro.

## Device log facts (v20, 96 runs in 20.zip)

- Zero `#Error`, zero native crashes; heartbeats `alive f=<frame> line=-1`
  up to f=7920. PNG loads every run: `loadmem: PNG decoded (slot 7, 640x480)`
  (title) + `(slot 22, 256x256)`. Viewport `[540,0 1440x1080]` on the
  2520×1080 Xclipse 950 device. Both bugs are RENDERING-ONLY.
- One `ewdx-boot.log.txt` was EMPTY (0 bytes) — truncated mirror copy, ignore.

## Tooling notes for the next session

- Build env RESTORED on this machine: SDK `C:\Android\android-sdk`, NDK
  27.3.13750724 (+28.2/29.0 also present), JDK 17 (gradle wrapper OK;
  GUIDE says JDK 21 for Ghidra only). Missing: sibling checkouts
  (OpenHSP `3dbb8729`, SDL2 `b90ac95`, SDL_ttf `7f16032` — clone per
  refs.md) and the Ghidra project.
- v20 APK extracted to `/tmp/ewdx_v20/` this session (libmain.so +
  assets/save.dat + data/pic/system.bmp).
- The blend tables in the shipped v20 binary were located at
  `libmain.so+0x14a24` (SRC) / `+0x14a44` (DST) and MATCH the repo table
  byte-for-byte — no hidden v20 change in the blend constants.

## Suggested fix plan (NOT executed)

1. Re-derive modes 3/4 from hmm.dll (Ghidra re-import; jump table at the
   DGBLENDMODE export). If swapped → fix `BLEND_SRC/DST` entries 3/4 in
   `ewdx_gles.cpp`, tag `v21-blend34`, ship.
2. Add `[dgline]` journal (throttled like v17) to catch the stray-line
   draw parameters on device; compare against putline call sites.
3. Audit `debag_mode` anyway: force `debag_mode = 0` at VM start (defensive;
   free) so the cort/scope debug overlay can NEVER appear even if some
   future vload/save path touches it.
4. Long shot: also audit `p_light` lifetime for the maw interior parts
   (interior draws at alpha `p_light`; if 0 the interior is invisible →
   black disc even with correct blend).
