# Session 2026-09-19 — v17 diagnostics + batcher fixes

## Symptoms carried in from v16 (device video + `analysis/frames/`)

1. **Title screen: logo/credits layer drawn as a giant stretched smear**
   (`f00384_016s.png`). Menu text (buffer-3 bitmap font) renders perfectly —
   the corruption is specific to the title-logo draw path.
2. **After difficulty select: black screen with the whole `system.bmp`
   (656×519) atlas painted as one huge quad** (`f00624_026s.png`,
   `f00672_028s.png`). VM keeps running (no error in any of the 16 collected
   boot logs) — purely a rendering-state bug set.

Evidence work done this session: extracted the v16 APK assets
(`~/Documents/v16/apk/`), decoded `system.bmp` / `title1` / `title3` to PNG
and pixel-matched them against the failure frames. The 26 s garbage quad is
**buffer 1's content** (white clear + giant portrait circles = `system.bmp`
regions drawn as dio tiles) stretched by the `DGGCOPY 1, 2` present pass —
i.e. wrong texture ids/rects in `dio(20,*)`, which are filled from the STR
array `pic[]` restored by `vload` from `save.dat`.

## Fixes shipped (static, verified against the decompile)

### 1. `ewdx_batch.cpp` — `run_check` blend-group flush (real bug)
D3D groups draws by state at DrawPrimitive time. The batcher only recorded
the blend factors when a batch was *empty*, so a `DGBLENDMODE` on a
continuing texture kept the **old** factors alive for the rest of the run.
The title-logo white flash draws exactly in this pattern → v16 smear frame.
`run_check` now flushes whenever texture, blend **or** target changes and
records the *current* state for the new group.

### 2. `ewdx_batch.cpp` — `emit_quad` flag&4 anchor branch (parity insurance)
The game's in-game sprites always pass flags 7+ (bit 4 set); the port
silently ignored bit 4. Decompiled `FUN_10001fa0` else-branch verbatim:
left-center pivot `(dx, pivy)` → scale → rotate → anchor `(dx−½, pivy−½)`.
Branch A (no bit 4) and branch B differ only by a translation *under* the
scale (the `local_1c*0.5 + param_1` term) → **identical at scale 256 /
angle 0**, which is why the menus were unaffected. Now branch B is real.

### 3. vload restore — verified exact parity, **no change needed**
Audited `rc_vload_restore` against the format bible
(`OpenHSP/src/plugins/win32/hspda/Hspda.cpp`):
- INT/DOUBLE ≡ `varload_get_storage`: blind `memcpy(pv->pt, vdata, pv->size)`
  where `pv->size = esz × CountElems(master)`. Port recomputes the same
  value; vendored `HspVarCoreCountElems` (`vm/hspvar_core.cpp:421`) skips
  zero trailing dims (`if (len) k *= len`), and `HspVarCoreDim` →
  `AllocPODArray` allocates exactly that size ⇒ byte-identical copy, zero
  dims included (no silent under-copy).
- STR ≡ `varload_get_flexstorage`: `max = size/4` blocks of
  `[FX][len][bytes]`, bounded by file end. Same walk.

## v17 diagnostics (new, `EWDX_DGCOPY_JOURNAL`)

- `ewdx_batch.cpp`: `[dgcopy] id=%d f=%#x dst=(%d,%d %dx%d) src=(%d,%d)
  sc=(%.2f,%.2f) ang=%u col=(%d,%d,%d,%d)` per copy + `[loadmem] slot=%d
  WxH (bytes)` per texture load.
- `ewdx_gles.cpp`: `[gsel] target=%d (switch #%u)` on target change.
- `ewdx_register.cpp`: `vload: rest '<name>' …` (before fill) + `vload:
  done '<name>' rc=…` (after fill) per var.
- `ewdx_boot.cpp`: build tag `v17-2026-09-19-diag-dgcopy`; **throttle** for
  `[dgcopy]` lines — first 32 occurrences pass, then every 50th (per
  distinct line), so per-frame repeats don't evict the stage-load evidence
  from the 24 KB Downloads mirror. All other lines pass verbatim.
- `CMakeLists.txt`: `target_compile_definitions(ewdx PUBLIC
  EWDX_DGCOPY_JOURNAL)` — remove for a release build.

## Build environment (restored after the RDP wipe, per `refs.md`)

- `~/Documents/SDL2` @ `b90ac95`, `~/Documents/SDL_ttf` @ `7f16032`
  (+ freetype `535d299`, harfbuzz `950d232`), `~/Documents/OpenHSP` @
  `3dbb872`.
- SDK `C:\Android\android-sdk`, NDK 27.3.13750724, gradle wrapper 8.7, JDK 17.
- Game data staged from the v16 APK extraction into
  `android/app/src/main/assets/` (590 data files + `start.ax` + `save.dat`;
  the repo carries none of it).

## Gate results

- NDK syntax gate: all 12 ewdx TUs `-Wall -Wextra` **zero warnings**
  (journal define both on and off).
- `gradlew assembleDebug` green → 55 MB APK with all 590 data files.
- `libmain.so` strings verified: tag + `[dgcopy]` + `[loadmem]` + `[gsel]`
  + `vload: done` all present.
- Shipped: `Downloads/ewdx-v17-diag.apk`.

## On-device repro protocol (next run)

1. Install APK; **clear app data first** (fresh `save.dat` staging).
2. Launch → title (watch the logo layer) → GAME START → difficulty select →
   stay in-game ~5 s → quit.
3. Collect `Downloads/ewdx-boot.log` (auto-mirrored) + `logcat -s ewdx:`.
4. In the log, correlate: `vload: done pic rc=0` → `[loadmem] slot=3 …656x519`
   → `[gsel] target=1` → the first `[dgcopy]` lines after the stage `gsel`.
   If the giant quad is a *script* draw (dio/pic garbage), its dst/src/scale
   tuple will be visibly wrong there; if it appears only in the present pass,
   the bug is in `DGGCOPY 1, 2` handling — either way the stream pins it.

## Postscript — v17 run results + the v18 fix (same day)

The user ran `ewdx-v17-diag.apk` on device and supplied `v17logss.zip` + a
capture video. Results:

- **The v16 rendering bugs are gone.** Title, difficulty select, character
  select, intro movie, and the in-game stage all render correctly; the
  atlas-quad/black-screen and the title smear no longer reproduce (the
  blend-flush + flag&4 fixes did their job).
- **New, precise failure**: crash right after character select, before the
  first full in-game frame. Log shows the HUD life-row + HP gauge drawing
  correctly, then ~30 identical digit draws `id=3 src=(24,40) 10x10` at
  10 px steps (the digit '0' repeated), then `#Error 19` =
  `HSPERR_DIVIDED_BY_ZERO`.

**Root cause (proven chain):** the score HUD calls
`tex_s2 …, score, 1` with `score == 0` at stage start →
`keta(0)` = `int(logf(0)/logf(10)) + 1` → ARM64 casts −inf to INT_MIN →
negative count → HSP `repeat` maps any negative count to ETRLOOP (eternal) →
`pow10(cnt+1)` wraps to 0 at cnt=31 (10³² ≡ 0 mod 2³²) →
`prm \ pow10(…) / pow10(…)` divides by zero → Error 19. The Windows runtime
survives `keta(0)`; our ARM64 build does not.

**Fix (v18, `vm/hsp3int.cpp` vendored):** clamp `logf` inputs ≤0 to 1.0
(`log(1)==0` ⇒ `keta(0)==1`; score "0" renders as one digit, PC behavior).
All other script `logf` call sites (seplay/set_volume dB math) are guarded
by `>0` checks and never see ≤0. CMake now compiles the vendored
`vm/hsp3int.cpp` instead of the OpenHSP one; build tag
`v18-2026-09-19-logf-clamp-keta0`; shipped as `Downloads/ewdx-v18-fix.apk`.

PC-version comparison (user offer): not required for this fix — the device
log + decompile pinned it — but having the original Windows game around is
useful for future behavior diffs (per refs.md it's also the RE source).

## Postscript 2 — v18 run results + v19 perf build (same day)

- **v18 crash fix verified**: gameplay reached and no `#Error` in any of the
  collected v18 logs.
- **User-reported lag**: self-inflicted — the v17/v18 diagnostics journal
  writes every `[dgcopy]` line with fopen/fclose + a JNI MediaStore flush,
  and in-game sprite coords are unique per frame so the occurrence throttle
  never trips (v18 log: ~12.6k zero-size quads alone, all journaled).
  **v19** (`v19-2026-09-19-nojournal-perf`, `Downloads/ewdx-v19-perf.apk`)
  compiles the journal OUT (CMake flag stays available) and doubles the
  Downloads mirror to 64 KB.
- **Open item — night-stage light masks**: the "JOURNEY INTO THE NIGHT"
  stage multiplies a 60×60 light circle (buffer 8) fullscreen-black over the
  scene per light source (`id=8 f=0x2 sc=(2.50,1.88) col=(0,0,0,255)`).
  User-reported black patches / animation glitches need a short journaled
  repro run (diag build with `EWDX_DGCOPY_JOURNAL` re-enabled) captured at
  the moment the glitch shows, to pin blend vs. geometry.
