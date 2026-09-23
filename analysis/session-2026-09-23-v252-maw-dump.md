# Session 2026-09-23 (G) — v25.1 test + corner-black forensics → v25.2 diag

Owner report on v25.1 (`ewdx-v251-prim-flags.apk`), with 46 fresh boot logs
(all tag `v25.1-2026-09-23-prim-flags`, zero error lines, clean run) and a
136-frame video (`EchidnaWarsDX(1).mp4`, 960×540@30) in MuMu shared folder.

## Owner test results (v25.1)

1. **Menu "white stuff" GONE — and it was the FEATHERS.** The white shapes
   on the main menu are the feather sprites that are SUPPOSED to be there;
   the v25.1 flag fix un-broke them (v25 drew them shifted/spilled as the
   white row wall). Closed.
2. **2 intestines moving** (was 1 in v25) — the per-primitive center/anchor
   fix positioned the wobble set. Partial win, more work remains.
3. **Black box on the corners of the circle PERSISTS** — this session's
   target.

## Forensics (analysis/v252_evidence/, 14 scripts committed)

- Video contact sheets: POV interior works (red wall, prey, wobble shifts
  across frames) — the bug is strictly the corners AROUND the ellipse.
- Corner color census: v24.1 corners showed scene colors through; v25.1
  corners are opaque black/grey (the plate), while the web reference's
  "corners" are just the dark night scene (45,45,45).
- Reference vs port crops: the ORIGINAL also has a black plate behind the
  porthole, but its ring fills the plate ~100%; v24.1 was close (0.97),
  v25.1's ring shrank inside the same plate → fat black corners.
- stom1.mot decoded (hspv format, t_part[24][41][98]): p0 = red meat ball
  (stom_s1 rect 160,0 80×80), p39 = ring outline (0,0 80×80), p35 = white
  glow, p17 = the wobbled scanline part sampling stom_s1 rows y=81..175 —
  the "intestines". Everything aligns in an 80×80 box.
- Atlas render (stom_s1.bmp / stom1.bmp): all stomach layers are opaque
  squares with black corners — the corner carve is the MASK pass's job,
  which worked in v24.1 and came back after the primitive rewrite.
- Decompile cross-check (FUN_10001fa0): v25.1's per-primitive math
  (centered + rect-scale + ctr-anchor) is faithful. Every script-level
  theory checks out on paper → the bug lives in RUNTIME buffer state.

## v25.2: diag build (no behavioral fix claimed)

Pixel forensics is exhausted; the proven move from the v23 era is buffer
dumps. New infra:

- `ewdx_boot_dump_binary()` (ewdx_boot.cpp/h): one-shot binary blob →
  filesDir + Downloads/MediaStore (reuses the journal's writer).
- `EWDX_MAW_DUMP` (ewdx_batch.cpp): at the first buffer-5→scene maw copy,
  force-flush, then dump buffers 6/2/5 as 24bpp BMPs
  (`ewdx-dump-buf{6,2,5}.bmp`, one set per run) + `[dump]` journal lines.
- `[maw]`/`[prim]` journal ON via EWDX_MAW_JOURNAL (prim draw stream:
  id/flags/dst/src/scale/color per primitive batch).
- EWDX_MAX_PRIMS 512→4096: the wobble loop adds one primitive per 1px
  scanline (up to 256 rows); the v25-era cap-trip reports (~2s lag) were
  real headroom failures.
- Build tag `v25.2-2026-09-23-maw-dump` (verified in the binary).
- Deliberately NOT changed: emit_quad math, blend state, clear state,
  prim flag semantics (all verified correct on paper this session; a
  source-remap clip guard was written and reverted — GL clip space already
  produces the D3D9-clipper result with perspective-correct UVs).

## Environment rebuild (fresh RDP box)

- Sibling checkouts re-cloned to refs.md pins: SDL b90ac95 (renamed to
  SDL2 — CMake expects `<root>/SDL2`), SDL_ttf 7f16032 (+ harfbuzz
  950d232 submodule), OpenHSP 3dbb872.
- Game assets + start.ax + save.dat extracted from the shipped v25.1 APK
  into `android/app/src/main/assets` (593 files, 103 MB).
- `local.properties`: `sdk.dir=C:/Android/android-sdk` — the backslash
  form breaks AGP's SdkLocator ("filename, directory name, or volume
  label syntax is incorrect" at config time).
- debug.keystore generated (fresh box); zipalign+apksigner via
  build-tools 37.0.0 (`.bat` required under bash).
- Owner supplied a NEW GitHub token; verified (login tencentfurrys, full
  scopes, both repos writable), stored in `~/.git-credentials` (Windows:
  C:\Users\RDP\.git-credentials), credential.helper=store, git identity
  set. Checkpoint push `edeff43..ef26d43` (v25.2 forensics scripts).

## Shipped

- `~/Downloads/ewdx-v252-maw-dump.apk` (53,066,043 B, signed V3, zipal-
  igned), pushed to the apks repo (bae7aac, branch main).

## Owner test protocol (one run is enough)

1. Install v25.2, start a vore with the big porthole, hold the POV ~5 s,
   quit. (Dumps fire at the FIRST POV composite of the run; restart the
   app to re-dump.)
2. Send: `Downloads/ewdx-boot*.log` + `Downloads/ewdx-dump-buf2.bmp`,
   `ewdx-dump-buf5.bmp`, `ewdx-dump-buf6.bmp`.

Reading guide for the dumps (next session):
- buf6 (staging): expect red wall + wobble rows inside an 80×80-ish box;
  if it's black/garbage → the wobble primitives or their texture state.
- buf2 (art): whatever lands here gets STAGED into buf6 by draw_spart.
- buf5 (composite): the carved porthole as it will appear on screen.
- Cross-check [prim] lines: ids, flags, scale, target — any prim batch
  drawing to the wrong target or with alpha≠255 explains plate black.
