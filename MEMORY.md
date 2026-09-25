# MEMORY.md — session recall (heartbeat)

> **READ THIS FIRST if you are a new session / new agent / returning owner.**
> This file is the project heartbeat: it is updated at the END of every working
> session so any future session can resume without re-reading everything.
> Deeper context lives in `GUIDE.md` (how/why), `ewdx_ndk/README.md` (module map),
> and `analysis/session-*.md` (per-day forensic logs).
>
> Last updated: **2026-09-25 (session H.1, v25.4 point-sampler)** — v25.3
> owner logs + screenshot: POINT SAMPLING SHIPPED (owner: "way more detail
> and glossy") — D3D9's default point filter vs the port's GL_LINEAR was
> the "PC crisp / Android blurry" gap; ewdx_buffer + both DGLOADMEMORY
> paths now GL_NEAREST. v25.3 diag confirmed working (clear witness +
> episode dumps + [prim] journal fire; BMPs land on device Downloads).
> avgRGB=765 readback was a Y-MIRROR artifact in the rect readback (fixed;
> log prints glY now). Open: corner-black persists (screenshot: scene
> shows through LEFT corners, black RIGHT corners = per-quarter mask
> defect); owner says interior animation is "slap not squeeze" (PC has
> point-filtered 1px scanline wobble; port Linear blurred it — point
> filter may already improve; needs PC-truth diff). NEW TOOL:
> tools/hmm_spy — PC hmm.dll forwarding spy (x86, zig cc): logs the full
> DG* stream in port vocabulary, ring-flushes on DGGCOPY 5; install =
> rename real hmm.dll→hmm_real.dll, drop spy in, play, read hmm_spy.log.
> NEXT: owner runs PC with spy through a squeeze POV + sends Android
> v25.4 BMPs (ewdx-dump-buf{1,2,4,5,6}.bmp) → diff PC truth vs port
> stream → fix mask quarters (v25.5). Shipped
> `~/Downloads/ewdx-v254-point-sampler.apk` (tag
> v25.4-2026-09-25-point-sampler).
> (session G, v25.2-diag) — v25.1 owner test:
> menu "white stuff" RESOLVED (it was the feather sprites — supposed to be
> there; the flag fix un-broke them), 2 intestines moving (was 1). Still:
> black box on the corners of the circle. v25.2 is a DIAG build (buffer
> 2/5/6 BMP dumps into Downloads at the first POV composite + [prim] draw
> stream journal) because every script-level theory checked out on paper —
> the bug lives in runtime buffer state only a dump can show. Shipped
> `~/Downloads/ewdx-v252-maw-dump.apk`. NEW GitHub token supplied by owner,
> verified (user tencentfurrys, repo scope) + stored in ~/.git-credentials.
> (session F.3, v25) — black square GONE
> (v24.1 owner-confirmed). Remaining black + missing squirm/flash rooted
> to the PRIMITIVE PATH stub (DGADDPRIMITIVE added one degenerate
> vertex/quad → stomach wobble (part(19)==1 scanline primitives from
> buffer 6), drop-shadows and view_blur all drew nothing). v25 rewrites
> it: full DG-state snapshot per primitive + emit_quad expansion with
> flag&2 dest semantics; blend at draw time (shadow sets mode 4 after
> adding), color per-snapshot (flash recolor inside the loop).
> Shipped `~/Downloads/ewdx-v251-prim-flags.apk` (v25.1: the
> DGADDPRIMITIVE arg is the DGGCOPY flag word — centered scanlines +
> guarded menu tiling; v25 drew them flag-0 = half-row shift + the white
> row wall on menus). Token from this session is burned — owner must
> revoke (see Security notes).

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
- **v22 FIX BUILT, REBUILT & BINARY-VERIFIED (session D, not yet owner-tested):**
  `~/Downloads/ewdx-v22-decompile-anchor.apk` (56,803,945 B), tag
  `v22-2026-09-22-flag4-decompile-anchor`. The ctr_anchor branch is
  verbatim from `analysis/decomp_inner.txt` (FUN_10001fa0): scale about
  rect center, rotate, anchor (pivx−0.5, pivy−0.5). Session D rebuilt it on
  the fresh machine and ran `v21_bindiff.py` against the owner-shipped v21
  APK: `ewdx_copy_flags` 707/707 ins with exactly ONE semantic float-op
  delta (`ldr s2,[sp]`→`fmov #0.5; fsub`) = the anchor fix, nothing else;
  all 593 game assets byte-identical. Plain branch now ALSO verbatim-
  verified: session C's stale verifier test math is fixed
  (`v22_pivot_verify.py` rewritten, 144/144 + a v21-regression probe that
  asserts the old anchor MIS-matches — see
  `analysis/session-2026-09-22-v22-rebuild-verify.md`).
- NEXT: interior animation — see
  `analysis/session-2026-09-23-v241-owner-test.md` (leads: unit(19)
  drift tick, `fr = unit(12, pn)` frame advance for stom_n, p_light
  refresh/flash). Logs: current era = MuMuSharedFolder/Download root
  (41× v24.1); pre-v24.1 archived in `archive_20260922_pre_v241/`.

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
- Blend modes 3/4 RGB rows: **NOT swapped** (v20 pixel ground truth
  stands). v24 DID restructure the table: `glBlendFuncSeparate` with
  D3D-exact alpha rows + alpha-respecting mode 0 — see "Open bugs" #1.

## Open bugs (REVISED 2026-09-22 — read before "fixing" anything)

1. **POV/maw black box — FIXED in v24 (owner test pending).**
   - Still true: blend 3/4 are NOT swapped; the mask tile is white outside
     the disc, transparent inside (v20 ground truth).
   - What the v20 verdict missed: D3D9 blends ALPHA with the same color
     factors (hmm.dll never enables SEPARATEALPHABLEND). The invert mask
     (mode 3) must ALSO carve corner alpha to 0, and the final mode-0
     copy must NOT paint opaque over those corners (ref video: scene
     visible through the corners). GLES2 rejects ONE_MINUS_SRC_COLOR as
     an alpha factor → `glBlendFunc` silently dropped every alpha half →
     corners stayed A=255 → opaque black box (v23 readback: 182/182
     nontransparent=100%). GUIDE.md's mode-0 (ONE,ZERO) recovery is
     likewise corrected: D3D-verbatim 4-channel math gives a
     ONE/INVSRCALPHA alpha row — byte-identical for opaque art and the
     only reading consistent with the reference.
   - Fix (ewdx_gles.cpp, v24): BLEND_RGB_* rows unchanged + new
     BLEND_A_SRC/A_DST rows mirroring D3D's 4-channel factors, applied
     via `glBlendFuncSeparate`. Every script mode-0 draw is alpha-255, so
     no other scene can change.
   - Historical (pre-v24 verdict, superseded): "matches the reference".
     The triage doc's blend-3/4-swap hypothesis is still REFUTED by pixels:
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
| `~/Downloads/ewdx-v21-pivot-recon.apk` | v21 build (re-downloaded from the owner's Mediafire link in session D; rollback baseline) |
| `~/Downloads/ewdx-v22-decompile-anchor.apk` | **v22 build (rebuilt + verified session D)** |
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
- 2026-09-22 (session D): fresh machine restored from scratch (deps at
  pins, 593 assets re-staged from the Mediafire v21 APK,
  local.properties). Verifier plain-case math fixed (both sides were
  stale) → 144/144 + rest-placement pins + v21 regression probe. v22
  rebuilt (2m44s) and shipped; bindiff vs shipped v21 libmain.so = ONE
  semantic delta (the anchor fix), assets byte-identical. Session doc:
  `analysis/session-2026-09-22-v22-rebuild-verify.md`. NEXT: owner v22
  test (head/door/POV), then phone-parity run.
- 2026-09-22 (session E): owner confirmed v22 fixes head/door on MuMu
  (65/65 logs on the v22 tag). POV "only ~2 parts" investigated with
  swallow-aligned metrics vs the owner's reference capture: ref shows
  1-2 parts in the first ~5 s of a hold too; mumu2 matches (1→7→5 parts
  by +8 s); verdict = reference parity, no port bug. mumu1's long dark
  disc = a door/room scene, not a stuck POV. Evidence:
  `analysis/v22_pov/` (report.html + sheets), doc:
  `analysis/session-2026-09-22-v22-pov-parity.md`. [LATER CORRECTED in
  session F: verdict was measured on mis-aligned moments — the port's
  porthole was genuinely broken; see session F.]
- 2026-09-22 (session F): v23-diag owner run (screenshots + 31 log
  rotations) → prey body renders INSIDE the porthole, but corners are an
  opaque black square; `[maw]` readback 182/182 nontransparent=100%.
  Root cause: GLES2 forbids color factors as alpha factors —
  glBlendFunc silently dropped the alpha half of modes 0/3/4/5, so the
  mask's corner alpha-carve never landed and mode 0 painted the corners
  opaque. v24 fix: glBlendFuncSeparate + D3D-exact alpha rows +
  alpha-respecting mode 0 (all script mode-0 draws are alpha-255 → no
  other scene changes). Built + shipped
  `~/Downloads/ewdx-v24-maw-alpha-carve.apk` (tag
  v24-2026-09-22-maw-alpha-carve verified in libmain.so). OWNER v24
  TEST: black square STILL present → v24 was necessary (mask carve) but
  not sufficient.
- 2026-09-23 (session F.1): size forensics on the v24 screenshot (square
  ≈300 game px vs 80 px composite) + v23 journal draw scan identified the
  ZOOMED STAGING CHAIN (mouth art → buffer 6 256×256 → scaled into
  buffer 5, 287→394 px zoom animation) as the actual square source; keyed
  asset previews (`analysis/v24_blackbox/`) showed the art itself is
  clean. Real root cause: staging clears `DGCOLOR 0,0,0,256` — alpha 256
  truncates to 0 in D3D9's D3DCOLOR (transparent black), the port
  clamped it opaque. Fix: ewdx_color &0xff truncation parity. Shipped
  `~/Downloads/ewdx-v241-clear-alpha256.apk` (tag
  v24.1-2026-09-23-clear-alpha256). Logs sorted: all 126 pre-v24.1 logs
  → `MuMuSharedFolder/Download/archive_20260922_pre_v241/`. NEXT: owner
  v24.1 test — expect square gone, scene up to the ring.
- 2026-09-23 (session F.3, v25/v25.1): v24.1 owner test PASSED (black
  square gone) but POV had exactly ONE intestine moving + white row wall
  on menus → primitive path rewrite (v25) then flag-word fix (v25.1: the
  DGADDPRIMITIVE arg = DGGCOPY flag word; centered + anchor + mirror per
  primitive). Shipped `~/Downloads/ewdx-v251-prim-flags.apk`.
  OWNER v25.1 TEST: menu white stuff GONE (and it was the FEATHERS —
  supposed to be on the main menu; flag fix un-broke them), 2 intestines
  moving (was 1) — but black box on the corners of the circle PERSISTS.- 2026-09-23 (session G, v25.2-diag): pixel forensics
(`analysis/v252_evidence/`, 14 scripts committed) — corners are OPAQUE
black plate (not night scene 45,45,45), v24.1 showed scene through;
ring shrank inside the plate after the primitive rewrite; mot decode
(p0 ball / p39 ring / p17 wobble rows y=81..175) + atlas keys check out
on paper → bug is RUNTIME buffer state, not script semantics. v25.2 =
DIAG build: at the first buffer-5→scene maw copy dumps buffers
6/2/5 as BMPs (24bpp) into Downloads (`ewdx-dump-buf{2,5,6}.bmp`, one
set per run) via a new `ewdx_boot_dump_binary()` MediaStore mirror +
journals the [prim] draw stream; EWDX_MAW_DUMP define; prim cap
512→4096 (wobble-loop cap-trip reports ~2s lag). Shipped
`~/Downloads/ewdx-v252-maw-dump.apk`, pushed to apks repo (bae7aac).
Build env rebuilt on fresh RDP box: sibling checkouts re-cloned per
refs.md pins, assets+start.ax extracted from shipped v25.1 APK into
android/app/src/main/assets, local.properties written (forward-slash
sdk.dir — backslash form breaks AGP SdkLocator), debug.keystore made.
NEW GitHub token from owner verified + stored (~/.git-credentials).
NEXT: owner runs v25.2 once into the POV, sends Downloads/ewdx-boot*.log
+ ewdx-dump-buf{2,5,6}.bmp → read buf6 (staging) vs buf5 (composite)
and fix the corner-black for real (v25.3).
- 2026-09-25 (session H/H.1, v25.3-diag → v25.4): videos analyzed (see
  analysis/session-2026-09-25-v253-maw-diag.md): interior red parity EXACT;
  corner-black structural, per-quarter; Android ring smaller in plate.
  v25.3-diag shipped (hardened dump trigger, present dumps, [prim] journal
  actually coded, DGCLEAR witness, ADD-time-target prim fix). Owner logs
  confirmed diag works (93 dump lines/run); v25.4 shipped: POINT sampling
  (owner-confirmed "way more detail and glossy"), per-episode dump
  re-arming, readback Y-mirror fix (avgRGB=765 artifact), budgets 200k/40k.
  NEW: tools/hmm_spy — x86 forwarding spy for the PC game's hmm.dll
  (gen_spy_c.py + zig cc), logs DG* stream in port vocabulary, ring-flush
  on DGGCOPY 5; PC-truth-vs-port diff is the path to the mask-quarter fix.
  Both APKs: ~/Downloads/ewdx-v25{3,4}-*.apk; NOT in apks repo yet.
  NEXT: owner PC spy run + Android v25.4 BMPs → v25.5 mask-quarter fix.
