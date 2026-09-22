# MEMORY.md — session recall (heartbeat)

> **READ THIS FIRST if you are a new session / new agent / returning owner.**
> This file is the project heartbeat: it is updated at the END of every working
> session so any future session can resume without re-reading everything.
> Deeper context lives in `GUIDE.md` (how/why), `ewdx_ndk/README.md` (module map),
> and `analysis/session-*.md` (per-day forensic logs).
>
> Last updated: **2026-09-22** (session start; owner's phone dying — this file
> written early so nothing is lost).

## What this project is (30 seconds)

Reverse-engineered Android port of **Echidna Wars DX v1.11** (Windows HSP3 game)
→ arm64 APK: SDL2 + GLES2 + OpenSL ES, OpenHSP VM running the decrypted,
decompiled `start.ax` (764 KB bytecode / 31,013 lines in `artifacts/`).
No game assets in the repo (rights + size). Build needs sibling checkouts
per `refs.md`.

## Current state (2026-09-22)

- Repo HEAD at recall time: `07a998c` "docs: v20 bug triage" (v19-era code).
- **The v20 source was NEVER committed.** The only v20 artifact is the binary
  `Downloads/ewdx-v20-flag4-fix.apk` (build tag `v20-2026-09-20-flag4-pivot-fix`).
  Before ANY source change: diff v20's `libmain.so` against a HEAD build or risk
  regressing the v20 fix. (Known v20 binary facts: blend tables at
  `libmain.so+0x14a24`/`+0x14a44` match the repo byte-for-byte — verified 09-21.)
- **Owner-confirmed (2026-09-22): v20 FIXED the "joints" bug** — player/enemy
  sprites no longer look off/weird (that was the flag&4 pivot fix). Do not undo.
- Shipping v19 code + v20-only binary divergence is the top repo-hygiene risk.

## Open bugs (in owner's words + mapped to repo analysis)

1. **POV/maw animation wrong** — "it's not showing much, then some parts of
   body and black". This is **Bug 1** of `analysis/session-2026-09-21-v20-bug-triage.md`:
   - Mechanism: vore/maw interior composed in buffer 5, masked by the
     quarter-disc tile `system.bmp (242,72,40,40)` drawn 4× mirrored with
     `DGBLENDMODE 3` (decompile L24144–24180).
   - **Strong hypothesis: port's DGBLENDMODE modes 3/4 are SWAPPED**
     (`BLEND_SRC/DST[3]/[4]` in `ewdx_ndk/ewdx_gles.cpp`). D3D9 enum order
     SRCCOLOR=3/INVSRCCOLOR=4 suggests a plain jump table gives
     3=multiply, 4=invert — opposite of the repo table today.
   - Also audit `p_light` lifetime (maw interior draws at alpha `p_light`;
     if 0 → interior invisible → black disc even with correct blend).
   - Confirm before shipping: Ghidra re-import hmm.dll → DGBLENDMODE export →
     read the 8 jump targets' SetRenderState pairs. Ghidra project was lost
     in the 09-18 RDP wipe; must re-import (hmm.dll is in the game zip, NOT
     in the repo — ask owner).
2. **Stray black lines player↔enemies** — **Bug 2** of the same triage doc.
   All 6 `putline`/`DGLINE` call sites in the script are intentional VFX
   (beam/tether colors documented there). Hypotheses H1 stuck grab-tether
   state, H2 DGLINE quad blend grouping, H3 shadow-prims degenerate.
   Plan: add a `[dgline]` journal (throttled like v17's `[dgcopy]`, see
   `ewdx_boot.cpp` throttle + `EWDX_DGCOPY_JOURNAL` CMake flag pattern) and
   capture one repro run.
3. Defensive fix queued (free): force `debag_mode = 0` at VM start — the
   script never assigns it; its debug overlay is dead code on PC and must
   never wake up (triage doc suggestion #3).

## Evidence locations (on THIS machine, NOT in repo)

| File | What |
|---|---|
| `~/Downloads/2026_09_19_23_36_54.mp4` | 47.3 s v20 capture (maw black box ~0–16 s, correct scene ~21–44 s, stray line ~42.8–43.6 s) |
| `~/Downloads/ewdx-v20-flag4-fix.apk` | The v20 binary (extract: `unzip`; check tag: strings-grep `build v20` in `lib/arm64-v8a/libmain.so`) |
| `~/Downloads/20.zip` | 96 v20 boot logs |
| `ewdx-port/analysis/frames_2026-09-21/` | 24 extracted video frames + contact sheet (already in repo) |

Video analysis tooling: ffmpeg NOT installed on this machine. Use Python
`opencv-python-headless` + `numpy` + `pillow` instead (pip install was
interrupted mid-run on 09-22 — re-run:
`python3 -m pip install --user opencv-python-headless numpy pillow`).
Reference frame-extraction pipeline from the 09-21 session lives in
`analysis/frames_2026-09-21/` naming + the triage doc.

## Build environment status (2026-09-22)

- Android SDK `C:\Android\android-sdk`, NDK **27.3.13750724** present
  (also 28.2 / 29.0; the project pins 27.3). llvm-objdump available at
  `ndk/27.3.13750724/toolchains/llvm/prebuilt/windows-x86_64/bin/` for
  binary forensics (that's how the 09-21 blend-table check was done).
- Ghidra: **not installed** (needed for the DGBLENDMODE jump-table confirmation).
- Sibling checkouts (required by `android/app/src/main/cpp/CMakeLists.txt`):
  **MISSING** — clone per `refs.md`:
  OpenHSP `3dbb872`, SDL2 `b90ac95` (branch SDL2), SDL_ttf `7f16032`
  (+ submodules freetype `535d299`, harfbuzz `950d232`) as siblings of `ewdx-port/`.
- Python 3.12 (hostedtoolcache). MinGW g++ exists for host tests.
- Game assets: stage from the v20 APK into
  `android/app/src/main/assets/` (593 files: start.ax, save.dat, data/).
- Build: `cd android && gradlew.bat assembleDebug -x lint` (~2 min warm).
  Host syntax gate: NDK clang++ `-fsyntax-only` per TU, `-DHSP64`, `-Wall -Wextra`.

## Worked-example fixes to reuse (do not rediscover)

- Audio self-deadlock pattern → `ewdx_audio.cpp` init comments (never hold
  `au_mtx` across calls that re-lock it).
- SJIS at any SDL/JNI boundary aborts ART (SIGABRT) → always
  `ewdx_sjis_to_utf8()` first (`ewdx_extcmd.cpp` dialog/title).
- vload STR restore: `master.size` is the POINTER-TABLE size (elems*4), the
  payload is `size/4` self-describing blocks — validate against file end
  BEFORE re-dimming (`ewdx_register.cpp` `rc_vload_restore`).
- `ginfo(2)` must be 0 when focused / −1 when not, or the script's
  `*label_198` guard skips ALL input reads (menus freeze).
- Tap input must behave like a ~180 ms physical key, never consume-once
  (three same-frame readers: DIGETJOYSTATE, getkey, getkey2).
- Diagnostics journaling costs fopen/fclose + MediaStore flush per line →
  compile OUT for gameplay builds (the v18 lag), throttle when on
  (`EWDX_JOURNAL_THROTTLE_*` in `ewdx_boot.cpp`).
- LESSON (from v9): the shipped APK binary can be ahead of git. Diff
  `libmain.so` before rebuilding.

## Security notes

- 2026-09-22: a GitHub PAT was pasted into chat by the owner to enable the
  memory push. **It is burned — revoke it** (GitHub → Settings → Developer
  settings → Tokens) once this push lands, and issue a fresh one. This is the
  same incident class as the 2026-09-18 handoff (`analysis/session-handoff-2026-09-18.md`).
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
  Next: frame extraction from the 09-19 video, v20 APK tag/blend verification,
  hmm.dll DGBLENDMODE confirmation, then v21 (blend 3/4 + [dgline] journal +
  debag_mode=0).
