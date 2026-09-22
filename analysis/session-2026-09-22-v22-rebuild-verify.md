# Session 2026-09-22 (D) — v22 rebuilt & verified against the shipped v21 APK

Follows `session-2026-09-22-v22-head-door-pov.md` (session C, cut short).
Fresh machine: `~/Downloads` was empty (no v20/v21 APKs, no MuMu captures), the
build checkout `~/Documents/ewdx-port` was at the v22 commit but WITHOUT the
sibling dependency checkouts, `local.properties`, or the 593 staged assets.

## 1. Environment restored (all pins per refs.md / MEMORY.md)
- Re-downloaded the owner-shipped **v21 APK from Mediafire**
  (`ewdx-v21-pivot-recon.apk`, 57,411,345 B — exactly the size session B
  recorded) and staged its 593 assets into
  `android/app/src/main/assets/` (`start.ax` + `save.dat` + `data/`,
  top-level, per the staging README; first extraction was nested under
  `assets/assets/` and was re-laid).
- Sibling checkouts re-cloned at the pinned SHAs: SDL2 `b90ac950…`,
  SDL_ttf `7f16032…` (+ freetype `535d299` / harfbuzz `950d232` submodules),
  OpenHSP `3dbb8729…`. `local.properties` rewritten as
  `sdk.dir=C:/Android/android-sdk` (forward slashes — the backslash trap).
  NDK 27.3.13750724 present; JDK 17 Temurin on PATH (build green).

## 2. Session C TODO 1 closed: verifier plain-branch test math fixed
`analysis/v22_pivot_verify.py` rewritten. Both sides of the plain (flags==0)
case were stale, not just the port-side one:
- **decompile side** rotated about `dx` (missing the `+dw/2` in `local_1c`)
  instead of the pivot → model of a v19-era binary, not of `decomp_inner.txt`;
- **port side** left `dh/2` outside the scale (`Yd = Yr + pivy − dy; Ys =
  Yd·scy + dy − 0.5`) → did not match the shipped plain branch either.
Verbatim translation of `decomp_inner.txt` FUN_10001fa0 now:
plain = pivot-rotate → add pivot → subtract anchor → scale →
`(ax−0.5, ay−0.5)`; ctr_anchor = scale pivot-relative → rotate →
`(pivx−0.5, pivy−0.5)`. Result: **cases: 144, mismatches: 0**, plus two new
sanity pins: (a) both branches rest-place the rect at `(cx−0.5, cy−0.5)`;
(b) a **v21 regression probe** (anchor `dx−0.5` on the ctr branch) is asserted
to MIS-match, so the head-off bug can never pass this verifier silently.
numpy dependency dropped (was imported, never used — this box lacks it).

## 3. v22 rebuilt and binary-verified against the shipped v21
- `gradlew assembleDebug -x lint` → BUILD SUCCESSFUL (2m 44s cold, gradle 8.7
  downloaded fresh). Shipped as `~/Downloads/ewdx-v22-decompile-anchor.apk`
  (56,803,945 B; boot tag `v22-2026-09-22-flag4-decompile-anchor` and the
  `fixes: flag4 anchor=pivot…` journal line confirmed inside `libmain.so`;
  all 593 assets + start.ax + save.dat verified inside the APK).
- APK entry-by-entry CRC vs the shipped v21: all 593 game assets and
  `libc++_shared.so` **byte-identical**; only `libmain.so` (the fix),
  `libSDL2.so` (−288 B, same pinned SHA — build nondeterminism),
  `libSDL2_ttf.so` / `classes2.dex` (same size, CRC-only metadata/build-id
  deltas) differ.
- `llvm-objdump -d` both stripped `.so` files (593,592 B each; 1,202
  functions each) → normalized bindiff: **3 mismatch groups** and the
  `ewdx_copy_flags` one is 707/707 instructions with exactly ONE semantic
  float-op delta:
  v21: `ldr s2, [sp, #D]` … `fmadd s0, s0, s1, s2` (stack-loaded dx−0.5 anchor)
  v22: `fmov s2, #0.5; fsub s1, s1, s2` (computed piv−0.5 anchors, x AND y)
  everything else = branch-target reslots and register-pair allocation noise
  from that 2-instruction prologue shift. **The shipped v22 is the diagnosed
  anchor fix, nothing else.**

## 4. TODO for next session (unchanged from session C, plus one)
1. **Owner device/emulator test of v22** (MuMu fine): head attached? door
   seated? POV interior coherent? Compare side-by-side with v21 if unsure
   (v21 stays in `~/Downloads` untouched as rollback baseline).
2. If POV colors still look off after v22: extract the same moment from
   MuMu `VideoRecords/EchidnaWarsDX(5..8).mp4` — the anchor fix should cure
   the interior misalignment; anything left is a NEW finding (then suspect
   `p_light` lifetime per open-bug #1, never the blend table).
3. One real-phone run eventually (MuMu = Adreno 640 @ 960×540, GLES 3.2).
4. (new) If another fresh machine is used: this session's env-restoration
   order works — deps → assets from the previous APK → local.properties →
   build → bindiff against the last SHIPPED apk before shipping.
