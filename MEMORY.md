# MEMORY.md — session recall (heartbeat)

> **READ THIS FIRST if you are a new session / new agent / returning owner.**
> This file is the project heartbeat: it is updated at the END of every working
> session so any future session can resume without re-reading everything.
> Deeper context lives in `GUIDE.md` (how/why), `ewdx_ndk/README.md` (module map),
> and `analysis/session-*.md` (per-day forensic logs).
>
> Last updated: **2026-09-22 (session C)** — v21 device-tested, head/door bug
> diagnosed, v22 fix built (pending owner test).

## What this project is (30 seconds)

Reverse-engineered Android port of **Echidna Wars DX v1.11** (Windows HSP3 game)
→ arm64 APK: SDL2 + GLES2 + OpenSL ES, OpenHSP VM running the decrypted,
decompiled `start.ax` (764 KB bytecode / 31,013 lines in `artifacts/`).
No game assets in the repo (rights + size). Build needs sibling checkouts
per `refs.md`.

## Current state (2026-09-22, session C)

- **v21 device test result (MuMu emulator): the flag&4 anchor was STILL
  wrong** — head drawn ~half-a-part left, door floating (rotation turns
  the shift into a vertical offset), POV interior parts misaligned
  ("weird colors"). Cause: v20/v21 anchored the ctr_anchor branch at
  (dx−0.5, pivy−0.5) instead of the decompile's (pivx−0.5, pivy−0.5);
  the missing +dw/2 is a constant left-shift on every flag&4 part.
- **v22 FIX BUILT (not yet owner-tested):**
  `~/Downloads/ewdx-v22-decompile-anchor.apk`, tag
  `v22-2026-09-22-flag4-decompile-anchor`. The ctr_anchor branch is now
  verbatim from `analysis/decomp_inner.txt` (FUN_10001fa0): scale about
  rect center, rotate, anchor (pivx−0.5, pivy−0.5). Plain branch
  confirmed already-exact (a wrong edit to it was caught by the numerical
  verifier `analysis/v22_pivot_verify.py` and reverted).
- NEXT OWNER ACTION: install v22, check head/door/POV. v21 logs and the
  4 MuMu videos are the baseline evidence (`analysis/v21_mumu/`,
  `analysis/session-2026-09-22-v22-head-door-pov.md`).

- **The lost v20 source is RECONSTRUCTED and now lives in the repo.** The v20
  "flag4 pivot fix" was recovered by normalized instruction diff of HEAD vs the
  shipped v20 `libmain.so` (`analysis/v21_bindiff.py`): exactly ONE function
  differed — `ewdx_copy_flags` (DGGCOPY path with `emit_quad` inlined). In the
  flag&4 (ctr_anchor) branch, v20 scales around the rect CENTER
  (`Xsc = X*scx` — X is already pivot-relative) instead of HEAD's left-center
  pre-offset (`X4 = X + dw/2`). Applied to `ewdx_ndk/ewdx_batch.cpp`;
  verification rebuild matches v20 at 707/707 instructions with only one
  benign stack-slot addressing-mode difference. **Owner-confirmed v20 fixed
  the joints bug — this restores it in source so it can never regress again.**
- **v21 APK built and shipped:** `~/Downloads/ewdx-v21-pivot-recon.apk`
  (build tag `v21-2026-09-22-pivot-recon`, 57.4 MB). Contents over v20:
  reconstructed pivot fix (identical code), `debag_mode` force-0 at VM start,
  compile-gated `[dgline]` journal (off by default), boot journal lines
  recording the fix provenance.
- Blend modes 3/4: **NOT swapped — do not change them.** See "Open bugs" #1.

## Open bugs (REVISED 2026-09-22 — read before "fixing" anything)

1. **POV/maw "black box + faint pink" — VERDICT: matches the reference.**
   The triage doc's blend-3/4-swap hypothesis is REFUTED by pixels:
   - Ground truth tile (`analysis/v21_facts.py`, `v21_tile_big.png`):
     `system.bmp (242,72,40,40)` is **WHITE OUTSIDE the disc radius,
     transparent INSIDE** (a porthole mask) — the triage doc had the
     orientation backwards ("white disc, black corners"). It is a
     quarter; 4× mirrored copies make the full 80×80 disc.
   - Therefore mode 3 = invert-multiply (port table today) paints the
     plate OUTSIDE the disc black and keeps the disc interior — the black
     80×80 square with a round window is the game's INTENDED maw art.
     Multiply (the "swap") would black out the interior entirely.
   - The 09-19 capture — which per the triage doc is the **web reference
     edition** — shows the SAME black-plate + dark-interior maw
     (`analysis/v21_dense/maw_*.jpg`). The user's complaint
     "not showing much, then some parts of body" is about how LITTLE of
     the prey is visible inside — that is `p_light`-driven interior alpha
     (decays −40/frame after a swallow) plus the game's art, not a blend bug.
   - If the owner still reports a difference vs the web edition on v21,
     next lever is `p_light`/`p_light2` lifetime, NOT the blend table.
2. **Stray black lines player↔enemies — reduced priority; likely reference
   parity.** `debag_mode` overlay theory REFUTED: the vendored VM
   zero-initializes globals (`HspVarCoreClear` on every global —
   "グローバル変数を0にリセット"), so the overlay is dead on PC and port
   alike; the force-0 at boot is kept as pure defense. NOTE the triage doc
   itself observed the SAME stray line in the web capture at t≈42.8 s —
   strong hint the line is in the original too. The `[dgline]` journal is
   now in the tree (`EWDX_DGLINE_JOURNAL` compile flag, mirror of the v17
   `[dgcopy]` one) — if the owner sees lines the web edition does NOT show,
   build once with `-DEWDX_DGLINE_JOURNAL` via a one-line CMakeLists edit,
   capture one repro, match colors/blends against the documented putline
   call sites (beam 190,255,120 / 190,230,255 / 255,230,150 blend 2;
   boss tether 255,230,240 blend 3; grab streaks 255,220,240 blend 1).
3. Done defensively: `debag_mode` forced 0 at VM start (log line
   `debag_mode forced 0 (vid=N)` in every v21 boot log).

## Evidence locations (on THIS machine, NOT in repo)

| File | What |
|---|---|
| `~/Downloads/2026_09_19_23_36_54.mp4` | 47.3 s capture of the WEB reference edition (maw ~0–16 s, stray line ~42.8–43.6 s) |
| `~/Downloads/ewdx-v20-flag4-fix.apk` | The v20 binary (basis of the pivot reconstruction) |
| `~/Downloads/ewdx-v21-pivot-recon.apk` | **NEW v21 build (this session)** |
| `~/Downloads/20.zip` | 96 v20 boot logs |
| `analysis/v21_dense/` | maw crops from the web capture (ground truth) |
| `analysis/v21_tile_big.png` | the porthole mask tile, magnified |
| `analysis/v21_bindiff.py` / `v21_funcdiff.py` | reusable stripped-binary differ (HEAD/v20/v21 disasm -> normalized per-function diff) |
| `/tmp/ewdx_v20/` | extracted v20 APK (re-extract if tmp wiped: `unzip ~/Downloads/ewdx-v20-flag4-fix.apk`) |
| `/tmp/dis_HEAD.txt`, `dis_v20.txt`, `dis_v21.txt`, `libmain_*.so` | disassembly corpus for the differ |

Video tooling: ffmpeg NOT installed. Use Python `opencv-python-headless`
+ `numpy` + `pillow` (installed 09-22, user site).

## Build environment status (2026-09-22, verified working)

- Android SDK `C:\Android\android-sdk`, NDK **27.3.13750724** (project pin).
- **`android/local.properties` MUST use forward slashes:**
  `sdk.dir=C:/Android/android-sdk` — a backslash version
  (`C\:\Android\...`) silently mangles to `C:Androidandroid-sdk` and gradle
  dies with "The filename, directory name, or volume label syntax is
  incorrect" at the SdkLocation listener (cost half a session to find).
- Sibling checkouts RESTORED as **`~/Documents/{SDL2,SDL_ttf,OpenHSP}`**
  (must be siblings of `ewdx-port/` — the CMake relative paths count on it;
  a clone into `~/` will fail CMake with "not an existing directory").
  Pins per `refs.md`; SDL_ttf needs `git submodule update --init`
  (freetype + harfbuzz vendored).
- Build: `cd android && gradlew.bat assembleDebug -x lint` (~1.5 min warm,
  ~90 s cold with gradle 8.7 download). APK lands in
  `app/build/outputs/apk/debug/app-debug.apk`.
- Game assets: staged 593 files from the v20 APK into
  `android/app/src/main/assets/` (start.ax + save.dat + data/) — already
  in place; if wiped, re-stage from an APK extract.
- Ghidra: still not installed (only needed if blend/emit questions return).
- Python 3.12 + cv2/numpy/PIL OK.

## Worked-example fixes to reuse (do not rediscover)

- **Stripped-binary source recovery (this session's trick):** build HEAD
  with the same NDK, `llvm-objdump -d --no-show-raw-insn` both `.so` files,
  run `analysis/v21_bindiff.py` — one differing function = the missing
  source delta. Register-noise filter: focus on `ldr/str s0..s31`,
  `fmov #const`, `fmadd/fnmsub`, and dropped/added float ops; ignore
  x8/x9 register-pair swaps and `[sp]` vs `[x29]` slot choices.
- Audio self-deadlock pattern → `ewdx_audio.cpp` init comments.
- SJIS at any SDL/JNI boundary aborts ART (SIGABRT) → `ewdx_sjis_to_utf8()`.
- vload STR restore: `master.size` is the POINTER-TABLE size (elems*4) —
  validate against file end BEFORE re-dimming (`rc_vload_restore`).
- `ginfo(2)` focused/-1 contract or `*label_198` skips all input.
- Tap input = ~180 ms physical key, never consume-once.
- Journaling costs fopen/fclose + MediaStore flush per line → compile OUT
  for gameplay builds (`EWDX_DGCOPY_JOURNAL` / `EWDX_DGLINE_JOURNAL`).
- LESSON (v9 + v20): the shipped APK binary can be ahead of git. Diff
  `libmain.so` before rebuilding.

## Security notes

- 2026-09-22: a GitHub PAT was pasted into chat by the owner to enable the
  memory push. **It is burned — revoke it** (GitHub → Settings → Developer
  settings → Tokens) and issue a fresh one. Same incident class as the
  2026-09-18 handoff (`analysis/session-handoff-2026-09-18.md`).
- The token is deliberately NOT stored in this file or anywhere in the repo.

## Heartbeat protocol (for future sessions)

1. Read this file top to bottom before touching code.
2. Update "Current state" + "Open bugs" as facts change.
3. Append a dated line below when a session ends, even if nothing shipped.
4. Commit + push this file with every session-end (owner has mandated the
   repo as the only durable memory).

### Session log
- 2026-09-22 (session A): repo fully re-read (all sources/docs/tools/vm diffs).
  MEMORY.md created + pushed as the recall heartbeat. No code changes yet.
- 2026-09-22 (session B): v20 pivot fix reconstructed from the v20 binary
  (normalized disasm diff, ONE function: `ewdx_copy_flags` ctr_anchor branch
  scale pivot = rect center). Blend-3/4-swap hypothesis REFUTED by pixel
  ground truth (mask tile is white-outside-disc; web reference shows the
  same black-plate maw). debag_mode overlay theory refuted (VM zero-inits).
  v21 = reconstructed fix + debag_mode force-0 + `[dgline]` journal hook.
  APK shipped to `~/Downloads/ewdx-v21-pivot-recon.apk`.
- 2026-09-22 (session C): v21 tested in MuMu — head-off + door-floating
  reported with video evidence. Root cause: reconstruction preserved
  v20's REMAINING anchor bug ((dx−0.5, pivy−0.5) instead of decompile's
  (pivx−0.5, pivy−0.5); −dw/2 constant shift). v22 fix built from
  decomp_inner.txt verbatim; plain branch verified already-exact via
  v22_pivot_verify.py. APK: ~/Downloads/ewdx-v22-decompile-anchor.apk.
  NEXT: owner v22 test; finish verifier plain-case test math; then POV
  color re-check on v22.
