# Session 2026-09-25 (H) — PC vs Android video comparison → v25.3-diag

Owner supplied: `archive.zip` (69 boot logs, all tag `v25.2-2026-09-23-maw-dump`),
`PCversion.mp4` (2:40, 854x480), `androidversions.mp4` (1:43, 854x480). Report:
"still has black corners". Tooling: ffmpeg via imageio-ffmpeg (no system ffmpeg
on this box), Python cv2/numpy/pillow user-site. Scripts:
`analysis/v26_timeline.py`, `v26_scan.py`, `v26_disc.py`, `v26_ascii.py`,
`v26_rgb.py`, `v26_ring.py`, `v26_angles.py`, `v26_hough.py`.

## Log forensics (the v25.2 diag build never fired its dump)
- All 69 logs carry build tag v25.2. `[dump]` lines: **0**. `[prim]` lines: **0**.
- The `[maw]` journal shows ONLY ids 0/4/7/8/9 (menu/gallery/stage traffic).
  **Zero id=5 copies and zero buffer-6 traffic in any captured run.** The
  trigger (`id == EWDX_MAW_BUF` inside the copy journal) therefore never ran in
  ANY captured session — these are all menu/stage sessions, not POV runs.
- `DGGSEL 0 FAILED (valid=0)` per frame = `DGGSEL 0` failing the `valid` check
  (slot 0 has no DGBUFFER) and falling into the journal branch; harmless
  (id!=0 guard returns success) but noisy — journaled from ewdx_select.
- Late-log loop: id=9 (textbox 466x106, blend 2 alpha=0), id=8 (menu rows
  220x28), id=7 fullscreen (blend 0), id=4 f=0x2 fullscreen (present chain).
- **v25.2 was never actually POV-tested with dumps** — the protocol output the
  build was named for never existed. Fix shipped in v25.3 (below).

## Video forensics (Hough disc R~91-121px, PC/Android 854x480, game box 640x400)
PC POV: t≈123.5-154 s. Android POV: t≈69.5-99 s (multiple grabs).
1. **The interior red parity is nearly EXACT.** Radial center RGB: PC
   (133,72,89) vs Android (134,73,90). Interior composition inside the disc:
   red 15-19%, darkred 20-26%, black 1-5%, mid 41-56% on BOTH. The stomach
   wall/prey rendering, p_light interior brightness, and wobble content are
   visually equivalent at the pixel-sample level. "Intestines move" = both
   videos show animated interior; the interior is NOT the bug anymore.
2. **The corner-black is REAL and structural:** disc bounding-square corner RGB
   on PC = scene colors (e.g. (134,136,119) wall, (200,239,128) grass);
   on Android = (2,2,2)/(7,7,4) pure black in 3 of 4 corners every sampled
   frame (t=71/75/79/83/87 s). One Android corner stays scene-colored —
   a PER-QUARTER defect, consistent with the 4-quarter mask composite
   (DGCOPY 3 x4 at 0,0/40,0 flags 0/8/16/24).
3. **The Android ring sits SMALLER inside its plate than PC's:** red-cluster
   extent / ring-ridge ratio med PC 0.83-0.91 vs Android 0.35-0.42; and the
   Android Hough diameter is constant 182px vs PC's varying 190-243px.
   PC: ring fills the plate (corners show through); Android: fat black frame
   around a smaller ring. Matches session G's "ring shrank inside the plate".
4. Both videos' gradation: dark scene -> plate appears -> interior shows ->
   plate leaves. Port compresses/loses the plate-exit transition.

## Root-cause candidates ranked after this session
1. **Mask quarter geometry (leading).** The four mask copies (DGCOPY 3 at
   (0,0) f=0 / (40,0) f=8 / (0,40) f=16 / (40,40) f=24) each carry a mirror
   bit. If even one quarter lands wrong (mirrored/shifted), its corner paints
   opaque plate ONTO the composite (blend 3 rgb ZERO/INVSRCCOLOR). Per-corner
   defect pattern supports a per-quarter bug. v22 fixed flag&4 anchoring; the
   40px quarter copies here use flags WITHOUT bit 4 (0/8/16/24), so they run
   the plain branch: pivot=(dx-0.5,dy-0.5)-anchored at 1:1 scale... needs the
   FUN_10001fa0 plain-branch math re-audited for mirror+quarter cases.
2. **Buffer-3 mask texture state.** The mask must be WHITE outside the disc,
   transparent inside, alpha=255 in the plate. If buffer 3 ever gets cleared
   with the wrong color/alpha, corners go black. The v25.3 [dump] clear
   witness will show every DGCLEAR hitting buffer 3.
3. **Draw-order/blend poisoning of the composite after view_mot.** Between
   `DGCLEAR 5` + `view_mot` and the 4 mask copies, `draw_mot_b`/prim batches
   (wobble) run under blend-1 with a=255 white; if any wobble quad spills
   outside its scanline rect, it can stamp black over the plate area INSIDE
   buf5 — visible as corner black in buf5 dumps (v25.3 dumps buf6+buf5 at the
   same instant to check).
4. ~~Blend alpha rows~~ (v24 solved), ~~clear truncation~~ (v24.1 solved),
   ~~script semantics~~ (verified on paper twice).

## v25.3-diag changes (this session; tag v25.3-2026-09-25-maw-diag)
- **Dump trigger hardened** (ewdx_batch.cpp): fires on ANY id-5 copy (before
  the flush), plus a secondary trigger — wide scanline prim batch staged into
  buffer 6 (m_rw>=120 && m_scx>=120, the wobble signature). v25.2's hook only
  ran inside a journal branch that never executed in captured sessions.
- **Present-path scene dump:** ~3 frames after the trigger, dumps buffers 1
  (scene) and 4 (final darkened frame) as BMPs — the visible end state,
  independent of composite-time races.
- **[prim] journal actually exists now** (v25.2 documented it but never wrote
  it): wide prims only (rw>=120), budget 20000 add + 4000 draw lines.
- **[dump] clear witness** (ewdx_gles.cpp ewdx_clear): every DGCLEAR journals
  target/size/rgba/state-target before flushing. Catches candidate 2 and the
  "clear color after DGGSEL" ordering.
- **st.target stamp** on DGCOLOR (which target was live when the color was
  set) — disambiguates DGCOLOR->DGGSEL->DGCLEAR chains.
- **Behavioral fix (the only one): DGDRAWPRIMITIVE now expands each prim
  against its ADD-time target** (EwdxPrim.tgt snapshot + ewdx_select(q->tgt)
  in the draw loop). D3D9 locks the destination at ADD time; the v25 rewrite
  expanded at draw time against the live target, so a DGGSEL between
  DGADDPRIMITIVE and DGDRAWPRIMITIVE mis-targeted the whole batch. This is a
  real correctness gap for any script that stages prims then switches back
  (draw_side/draw_sub patterns in the decompile do exactly this around the
  POV staging).
- EWDX_MAX_PRIMS stays 4096. Journal budgets keep I/O bounded (v18 lag).

## Environment (fresh RDP box, rebuilt this session)
- ~/Documents/{ewdx-port,SDL2@b90ac95,SDL_ttf@7f16032(+freetype 535d299,
  harfbuzz 950d232),OpenHSP@3dbb872} per refs.md; ewdx-port unshallowed.
- Assets: 590 data files + start.ax + save.dat extracted from the shipped
  v25.2 APK into android/app/src/main/assets.
- local.properties: sdk.dir=C:/Android/android-sdk (forward slashes);
  NDK 27.3.13750724; debug.keystore generated; JDK 17 on PATH worked for AGP.
- Build: gradlew assembleDebug -x lint = 1m58s. Syntax gate green (diag ON
  and OFF). Evidence: analysis/v26_video/pc_vs_android_pov.png.

## Shipped
- `~/Downloads/ewdx-v253-maw-diag.apk` (57,420,489 B, tag verified in
  libmain.so). NOT yet pushed to the apks repo (no token on this box —
  ~/.git-credentials is fresh; owner token needed for push).

## Session H.1 (same day) — owner "Lastest V" logs → v25.4
Owner report: PC is "more of a squeeze", Android "just slap" + less detail
(PC uses point filter), possible missing intestines. 16 fresh log rotations
(v25.3 tag): [dump] clear witness WORKS (12-23/run); POV composites present
(2-4 per log, seq #995k-1.04M = hours-long sessions).
### Findings
1. **v25.3 dumps fired ONCE per run** (first POV of the session); all owner
   logs are from later — the BMPs exist on the device Downloads from the
   first POV only. Fixed in v25.4 with EPISODE re-arming.
2. **`avgRGB=765` readback was a Y-MIRROR ARTIFACT**: glReadPixels is
   bottom-origin, (rx,ry) top-origin → the rect read sampled the white clear
   BELOW the porthole. Fixed (ry mirrored; log now prints both y and glY).
3. **Journal budgets exhausted** (prim=0 after 20000 adds early in the run)
   → bumped 10x (200k adds / 40k draws).
4. POV block verified against decompile (clear5-white → interior parts
   (id=25 ball, id=7 16x16..32x32 angled parts, id=6 wobble row 138x94 into
   t=5) → 4 mask quarters (id=3 f=0/8/16/24, blend 3) → id=5 f=1 80x80 into
   t=1 blend 0). Structure matches; content needs the BMPs.
### v25.4 changes (tag v25.4-2026-09-25-point-sampler)
- **POINT sampling parity** (owner-confirmed visual gap): D3D9 default
  sampler is POINT and hmm.dll sets no filters — the port's GL_LINEAR
  blurred every buffer/atlas upscale ("less detail", mushy scanlines).
  ewdx_buffer + both DGLOADMEMORY upload paths now GL_NEAREST. Text cache
  stays LINEAR (own font rendering, not game art).
- **POV episode dumps**: episode = POV activity separated by >120 presents
  without it; every episode re-arms composite dumps (6/2/5) + present dumps
  (1/4). The POV the owner tests ALWAYS produces fresh BMPs now.
- Readback Y-mirror fix + glY in log. Budgets 200k/40k.
- Shipped `~/Downloads/ewdx-v254-point-sampler.apk` (57,420,801 B, tag
  verified). Owner: run the POV ~5 s, quit, send ewdx-boot*.log +
  ewdx-dump-buf{1,2,4,5,6}.bmp (5 BMPs per episode; only the tested episode
  writes them).

## Owner test protocol (v25.3-diag)
1. Install, start a vore with the big porthole, hold ~5 s, quit.
2. Send: Downloads/ewdx-boot*.log + ewdx-dump-buf{1,2,4,5,6}.bmp.
Reading guide:
- [dump] clear lines: any DGCLEAR on t=3 or t=5 with alpha!=0 or wrong rgb?
- [prim] +N lines: staging rows hitting tgt=6 with expected (x,y,w)?
- buf6 vs buf5: is the staging art clean and the composite already black?
- buf5 vs buf1: does the black survive the copy (composite bug) or appear
  later (scene chain)?
- buf4 vs buf1: does the darkening chain add black corners?
