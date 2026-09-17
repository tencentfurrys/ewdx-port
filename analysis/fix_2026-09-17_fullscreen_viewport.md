# Fix 2026-09-17 — game rendered in a small corner (letterboxed fullscreen)

## Symptom

v7 APK boots, VM runs, but the whole 640x480 game view draws inside a
small rectangle in one corner of the phone screen (device video:
`Downloads/2026_09_17_12_42_00.mp4`).

## Root cause

The target-0 (default framebuffer) viewport was set to the *game* size
(640x480 / 1280x960) with origin (0,0). On Windows/D3D9 the swapchain
backbuffer IS 640x480 so that is a no-op — but on Android the SDL window
drawable is the FULL device surface (e.g. 2400x1080), so the game rendered
into a 640x480 pixel subrect at the bottom-left of the display.

Every draw path (batcher quads, primitives, lines, SDL_ttf text) maps game
coords to NDC against `scr_w/scr_h` and shares one shader program, so the
only wrong piece was the target-0 `glViewport`.

## Fix (v8, fullscreen-v8.apk)

1. `ewdx_gles.cpp` — `ewdx_apply_screen_viewport()`: computes a centered,
   aspect-locked subrect (letterbox/pillarbox) mapping the whole game view
   onto the real drawable via `SDL_GL_GetDrawableSize`; stored in the new
   `ewdx.viewport[4]` (`ewdx_gles.h`). `ewdx_apply_viewport()` uses it for
   target 0. Called from `ewdx_screen()` (any DGSCREEN size change) and from
   `ewdx_input_poll()` on `SDL_WINDOWEVENT_SIZE_CHANGED` (rotation/resume).
2. `ewdx_present()` — fullscreen black clear when letterbox bars exist
   (game DGCLEAR only covers the 640x480 view; bars would keep stale pixels).
   Scissor explicitly disabled; game-view viewport restored afterwards.
3. `ewdx_input.cpp` — touch stick displacement converted to *game px*
   (`normalized x scr * viewport-scale`) so the 12 px deadzone keeps its
   meaning at any device resolution.
4. `CMakeLists.txt` (android app) — added the missing
   `thirdparty/stb_image_impl.c` TU (PNGs-renamed-to-.bmp loader); without it
   `libmain.so` failed to link (`stbi_load_from_memory` undefined).
5. `tests/shim/ewdx_gles.h` — shim struct gained `viewport[4]` to stay in
   sync with the real header (host input tests unaffected; NULL-window path
   falls back to the pre-fix behavior).

## Build (this host)

- Reference checkouts per `refs.md` pins, as siblings of `ewdx-port/`:
  `~/Documents/{SDL2@b90ac95, SDL_ttf@7f16032 (+freetype 535d299, harfbuzz
  950d232), OpenHSP@3dbb872}` (shallow clone HEADs drift; `git fetch --depth 1
  origin <sha>` + checkout to pin).
- Game assets staged from the v7 APK (unzip → `assets/data`, `start.ax`,
  `save.dat` → `android/app/src/main/assets/`).
- `android/local.properties`: `sdk.dir=C:/Android/android-sdk` (forward
  slashes; the `C\:\\...` escaped form trips AGP's SDK path validation).
- `cd android && ./gradlew assembleDebug` → 56.8 MB debug APK
  (590 asset files verified inside), copied to
  `Downloads/EchidnaWarsDX-fullscreen-v8.apk`.

## Addendum (v9): the uncommitted "flipfix" recovered from the v7 binary

The v8 rebuild above came out UPSIDE-DOWN on device: the MediaFire v7 APK
contained a fix that was never committed to GitHub (repo HEAD a156c5f = v6).
Binary forensics on the two stripped libmain.so files (v7 vs v8, all 569
exported functions compared; same-size diffs proven to be relocation noise):

- The ONLY real logic difference: the texture-upload row order in
  `ewdx_loadmemory` (BMP loop) and `ewdx_loadmemory_png`. Repo/v8 flipped rows
  (`(H-1-y)*stride`, visible as `mvn`/negate+add in the inner loop); v7 copies
  rows in FILE ORDER (no flip).
- Why: with this port's NDC mapping (`y` flipped per-vertex in emit_quad),
  texture row 0 already lands at the TOP of the screen — the extra software
  flip was the upside-down bug, not a fix. The v7 author removed it; the
  change never made it back to the repo (the APK name says it: "flipfix").
- v9 (`EchidnaWarsDX-fullscreen-v9.apk`) = repo source + v7 flip parity
  (both load paths) + the letterboxed fullscreen viewport fix from this file.
  Verified post-build: mvn-count 0 in both loaders, matching v7; build tag
  `v9-2026-09-17-flipfix+letterbox` appears in the boot journal.
- LESSON: this repo's canonical build source is the latest APK binary, not
  the git history. Before rebuilding, diff the current APK's libmain.so.

## Notes / follow-ups

- Aspect mismatch: game is 4:3; on a ~19.5:9 phone there will be black bars
  left/right (pillarbox) — geometry-accurate, like the Windows window.
- The stretch-vs-letterbox choice lives entirely in
  `ewdx_apply_screen_viewport()` if a "fill screen" mode is ever wanted.
- EXCMD_REDRAW (`ewdx_extcmd.cpp`) swaps without the bar-clear; it is
  boot-time only in this script, so left as-is.
