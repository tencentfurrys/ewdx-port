# Session handoff — 2026-09-18 (RDP wipe incoming)

Everything on this machine will be deleted. This file + git history + the GitHub
release `v16` are the only survivors. Read this before doing anything.

## State of the port

| Build | Status | What it fixed |
|-------|--------|---------------|
| v13 | repo HEAD before this session | baseline |
| v14 | vload restore true-shape parity | theory fix (not the crash) |
| v15 | ERR7 forensics hooks vendored into VM | **named the culprit**: `ERR7 core:range var=m_dio0_w id=348 flag=2 len=[64,0,0,0] idx=0 sup=0xa` |
| v16 | STR restore block-walk fixed (validate-first, true dims, file-end bounds) | **Error 7 at stage load is GONE** |

v16 device video proves: title -> menus -> character select -> Now Loading ->
**in-game renders** (no more `#Error 7`). All APKs, log zips, and the video are
release assets on tag `v16`:
https://github.com/tencentfurrys/ewdx-port/releases/tag/v16

## The NEXT bug (v16 video, evidence in `analysis/frames/`)

1. `f00384_016s.png` — TITLE MENU: menu items (GAME START / STAGE SELECT / ...)
   render perfectly, but the background title logo is **garbled / mirrored glyph
   spam**. Font strip from the atlas is being drawn wrong.
2. `f00432_018s.png` — DIFFICULTY SELECT renders **perfectly** (icons + font +
   `HIGHSCORE = 14929` — savedata restore works, so vload/vsave are healthy).
3. `f00528_022s.png` — stage start: player sprite centered on black, no map BG.
4. `f00624_026s.png` / `f00672_028s.png` — **the entire texture atlas dumped raw
   as one huge quad** (portrait circles, effect sprites, the font strip at the
   bottom — all visible) with player + UI buttons drawn on top.

### Working hypotheses (verify, don't trust)

- H1 (atlas dump): a DG draw call in the stage path passes a texture with an
  unset/zeroed src rect and the shim falls back to the whole texture. Prime
  suspect: `dg_copy`/`dg_addprim` state machine in `ewdx_ndk/ewdx_dg.cpp` —
  rect/UV state not initialized per prim, or `DGCOPY` flags (1/2/8 = center/
  scale/flip) mapping wrong. See `EWDX_DGCOPY` case in `ewdx_ndk/ewdx_register.cpp`.
- H2 (mirrored glyphs): V-coordinate flip inconsistency. GLES origin is
  bottom-left; DG/DIB is top-left. Some load paths (`dg_loadmem` vs `dg_texture`)
  flip and others don't. Difficulty select (SDL_ttf path) is unaffected; the
  atlas-textured logo is. Check flip handling in `ewdx_ndk/ewdx_gles.cpp`.
- H3 (no map BG): stage BG texture never loads on device (asset path shim?) so
  its draw falls back to a different bound texture. Journal asset loads
  (`picload`/`dmmload`/`dg_loadmem` failures) — failures are silent today.

## Rebuilding from scratch (this machine is gone)

- Toolchain was at `C:\Android\android-sdk` (SDK + NDK 27.3.13750724 + cmake +
  ninja). `android/local.properties` needs `sdk.dir=C\:\\Android\\android-sdk`.
- Sibling reference checkouts were at `C:\Users\runneradmin\Documents\`:
  `OpenHSP`, `SDL2`, `SDL_ttf` — clone the exact SHAs pinned in `refs.md`.
- The VM files `ewdx_ndk/vm/hsp3code.cpp` + `hspvar_core.cpp` are VENDORED now
  (with ERR7 hooks) and `android/app/src/main/cpp/CMakeLists.txt` points at
  them — they do NOT come from the sibling OpenHSP anymore.
- Build: `cd android && gradlew.bat assembleDebug` (~2 min warm, ~10 min cold).
- Assets are staged from the v13 APK into
  `android/app/src/main/assets/` — this dir IS committed? Check
  `git ls-files android/app/src/main/assets | head`. If empty, re-extract from
  release asset `EchidnaWarsDX-v13.apk` (res/assets inside the APK).
- Boot journal mirrors to device Downloads as `ewdx-boot-*.log.txt` — the ERR7
  hook names var/id/len/idx/support for any future array overflow.

## Security note

The GitHub PAT used for this push was pasted into chat and is burned. Revoke it
(GitHub -> Settings -> Developer settings -> Tokens) and issue a fresh one.
