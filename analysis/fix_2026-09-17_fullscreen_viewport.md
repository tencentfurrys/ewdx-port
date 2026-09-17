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

## Addendum (v10): EGL surface ground truth — letterbox finally engages

v9 still showed the 640x480 corner. Boot journal explained it: SDL's
`SDL_GL_GetDrawableSize` returns the REQUESTED window size on Android
(640x480), not the real surface — so the letterbox math computed "nothing to
scale". Fix: `ewdx_surface_px()` queries EGL directly (`eglQuerySurface` on
current display/draw surface), falling back to SDL elsewhere. Journal now
prints once per geometry:

    viewport: surface 1440x720 game 640x480 -> [240,0 960x720]

(device: Adreno 660, landscape, EGL surface 1440x720 -> centered 960x720
4:3 game view). Requires linking EGL (android CMake `target_link_libraries`).

## Addendum (v11): bar-only scissor clear — DEVICE-CONFIRMED WORKING

v10 regressed to BLACK SCREEN WITH AUDIO: the journal showed the viewport
engaged correctly, but the fullscreen black clear in `ewdx_present()` ran
AFTER the frame flush and wiped the freshly drawn game view every swap
(v9 never hit it because its letterbox no-op'd skipped the clear).

Fix: clear ONLY the four bar rectangles via `glScissor` (bottom-origin Y
math), never the game subrect. Device run confirmed: full-screen, centered,
right-side up, audio + input OK (build tag
`v11-2026-09-17-bar-scissor`).

## Final state (all shipped in v11 / commits 0f8101d + e3e1b79)

- Fullscreen letterboxed present: EGL surface query -> aspect-locked subrect,
  bar-only scissor clear, SIZE_CHANGED recompute (rotation).
- Flip parity with the v7 binary: no software row flip on BMP/PNG uploads.
- Touch: stick displacement scaled into game px (deadzone stays 12 game px).
- Build: stb_image_impl.c linked (PNGs-renamed-to-.bmp), EGL linked.
- Journal evidence chain: build tag, gpu line, viewport line, per-stage boot.

## Notes / follow-ups

- Aspect mismatch: game is 4:3; on a ~19.5:9 phone there will be black bars
  left/right (pillarbox) — geometry-accurate, like the Windows window.
- The stretch-vs-letterbox choice lives entirely in
  `ewdx_apply_screen_viewport()` if a "fill screen" mode is ever wanted.
- EXCMD_REDRAW (`ewdx_extcmd.cpp`) swaps without the bar-clear; it is
  boot-time only in this script, so left as-is.

---

# Session 2 (2026-09-17, later): UI touch + the two freeze bugs (v12–v13)

## What shipped in v12 (commit 8017f02)

Full-screen gesture touch, no on-screen buttons:
- primary finger drag -> arrows (bits0-3), deadzone in game px via the
  letterbox viewport
- primary finger tap -> Z (confirm) latch, one-read consumption
- second finger -> X (cancel) while held
- host test rewritten to the gesture model (23 checks green)

## Device-side findings (v12 on hardware, via adb)

1. Menus were FROZEN: taps reached the input layer (`joyg=0x010` visible in
   the journal) but the script never advanced. Root cause chain:
   - `*label_198` (the script's input refresh) starts with
     `if ( ginfo(2) != 0 ) { return }`.
   - Our `ginfo(2)` shim hardcoded `1` -> input was NEVER read -> menus
     dead since the first build. Fixed: real HSP semantics — active-window
     id 0 when focused, -1 when backgrounded (`ewdx_input_focus()`).
   - A consume-once tap latch also starved readers: `label_198` reads
     DIGETJOYSTATE, then `getkey Z`, then `getkey2` Z-edges from the SAME
     mask in one frame; whoever read first ate the tap. Fixed: a tap now
     behaves like a ~180 ms physical key press (asserted for
     `EWDX_TAP_HOLD_MS`, then auto-released) so every reader sees it.
   - `getkey` only mapped arrows/Z/X; C/A/S/D (VK 67/65/83/68) now map to
     bits 6..9 too.
2. Boot-log mirror to Downloads always failed on Android 15
   (DatabaseUtils "failed to build unique file" — stale row conflict).
   Fixed: fall back to a fresh per-run file name when the canonical name is
   refused, plus 30 s backoff after repeated failures (logcat no longer
   spammed every journal line).
3. Verified live on device (Tcl 5033D, 960x540, via adb): after the ginfo(2)
   fix the menus NAVIGATED by tap: title -> STAGE SELECT -> difficulty ->
   stage transition ("GAME START" works).

## Where v13 stopped: #Error 7 (ARRAY_OVERFLOW) at stage load

- Full flow into the stage now works; the crash-report dialog
  ("previous run stopped at") was also dismissed live via adb tap.
- Stage load throws `#Error 7` (HSPERR_ARRAY_OVERFLOW) AFTER
  `_dmmload/_dmmvol/_dmmpan/_dmmplay` resolve (BGM loads fine).
- Prime suspect: `seplay` (start_ax_dump.hsp L3845..3860) writes
  `sound(cnt + 1)` when inserting a new SE into the last slot — one past
  the end of `sound()`. Real HSP3.5 auto-expands the array; if the pinned
  OpenHSP core throws instead, that is exactly error 7. Next step: check
  `HspVarCoreArray` / the variable-write path for expansion parity, or
  add a write-hook that grows 1-D arrays like HSP3.5 does.
- DS backslash patch verified: 13 `data\...` separators are rewritten on
  first boot (idempotent; fresh install logs `staged start.ax (... 13
  separators)`).

## Diagnostics used (for future sessions)

- OCR of adb screencaps (Windows Media OCR via PowerShell) to read the
  actual game screen.
- `uiautomator dump` to find SDL dialog buttons (OK at [453,348][509,414]
  on 960x540) and dismiss the 10 s crash-report via `input tap`.
- `input swipe x1 y x2 y 8000` + heartbeat joyg sampling to prove touch
  reaches the game on device.
- `adb install -r` while the game runs always leaves a non-clean journal
  tail -> the crash-report dialog blocks the next boot 10 s; uninstall/
  reinstall avoids it during testing.
