# Fix 2026-09-18 — vload restore shape parity (Error 7 at stage load)

## Symptom (device evidence)

- Video (Downloads, 64 s): D-Gate splash → title → GAME START → DIFFICULTY →
  CHARACTER SELECT → "Now Loading" → `#Error 7 → Internal Error(7)` dialog → exit.
- `chacktarslect.zip` → `ewdx-boot-67.log (1/2).txt` (v13 build): VM ready, menus
  playable (two distinct SEs played fine: `dmm*` group fired once, journal dedupes
  first-use-only), heartbeats to `f=1440`, then `#Error 7` (~52-54 s in,
  = character-select confirm → stage load "Now Loading").
- Error 7 = `HSPVAR_ERROR_ARRAYOVER` (`#Error 7 (ARRAY_OVERFLOW)`).

## Root cause

`rc_vload_restore()` (`ewdx_ndk/ewdx_register.cpp`) rebuilt every restored
variable as 1-D: `HspVarCoreDim(pv, flag, len1, 1, 1, 1)` with
`nelem = len[1]` only. The real files store full multi-dim flex arrays:

- `data/map/*.map` (13 vars): `m_obj` INT 17×3, `m_obj_w` STR 2×(len2=0),
  `m_dio0` INT 20×2, `m_sw` INT 10×1, `m_rp` INT 2×0 ...
- `data/mold/*.mol` (62 files): `t_obj` INT 6×0, `t_obj_w` STR 7×0,
  `t_cort` INT 8×N (N = cort rows, flex)
- `data/mot/*.mot` (138 files): `t_frame` INT 6×F, `t_part` INT 24×59×F,
  `t_part_w` STR F×0, `t_scope` INT 46×F (+ a `pic` STR entry the port
  restores into the live `pic` array)

Consequences of the flat rebuild:

1. INT payloads truncated to `len[1]` elements (`m_obj` got 68 of 204 bytes)
   and dim2+ lost — `repeat length2(m_obj)` loops in the stage-init
   `#deffunc` (start_ax_dump.hsp ~L1853-1909) read/iterate wrong shapes.
2. STR vars restored as `len1` blocks while the payload walks `size/4`
   blocks — slot-count mismatch vs the rows the script then reads.
3. Trailing dims forced to 1 instead of 0 — `code_arrayint2`'s flex
   auto-expand check (`len[next]==0`, support `FLEXARRAY` bit 0x8 — present:
   INT=0x9, STR=0xa in all files) was disabled for grow-on-write patterns
   (`seplay`'s `sound(cnt+1)`, `copy2_a` writing `mold(cnt, mold_num)`,
   `frame(cnt, ct2, mo_num)` with growing mo_num, `npic`'s `pic` growth).

Format authority: OpenHSP `src/plugins/win32/hspda/Hspda.cpp` `varload_getvar()`
does `*pv = *pv2` (struct-copy of the master PVal incl. true lens + support),
then realloc + payload copy; STR flexstorage walk is `max = pv->size /
sizeof(char*)` blocks of `[FX tag][size][bytes]` — payload-driven, not
len-driven. Verified against all 21 `.map` / 62 `.mol` / 138 `.mot` files
in the v13 APK (parse with the corrected 16+64-entry layout: flag@16,
len[0..4]@20, size@40, support@52).

Note: the stock pinned VM (OpenHSP 3dbb872) auto-expands most of these writes
on paper; the shipped v13 binary evidently diverges from the pinned sources
(the repo's own v9 "LESSON" warns about this). v14 therefore rebuilds from the
pinned sources AND makes the restore shape exact, so no growth is ever needed
at stage load.

## The fix (v14, tag `v14-2026-09-18-vload-shape-parity`)

`ewdx_ndk/ewdx_register.cpp` — `rc_vload_restore()`:

- Rebuilds the PVal with the MASTER's true dims verbatim (zeros stay zeros;
  no more `1,1,1` padding) and `support` from the file.
- INT/DOUBLE: memcopies the whole stored payload
  (`esz × HspVarCoreCountElems(master)` bytes; bounds-checked vs `master.size`).
- STR: block count from `master.size / 4` (bible parity), per-block
  `[FX][size][bytes]` walk into `Dim(pv, STR, blocks, 0, 0, 0)`.
- Journals every restore: `vload: '<name>' flag=N len=[l1,l2,l3,l4] size=N sup=0xN`
  — next crash log pins the exact var/shape in question.

`ewdx_ndk/ewdx_boot.cpp`: build tag bump.

## Build/ship

- Toolchain: SDK `C:\Android\android-sdk`, NDK 27.3.13750724, gradle 9.7.1,
  sibling checkouts per refs.md (OpenHSP 3dbb872, SDL2 b90ac95, SDL_ttf 7f16032).
- `android/local.properties` re-added (properties-escaped `sdk.dir`).
- Assets staged from the v13 APK (593 files: start.ax, save.dat, data/).
- `gradle assembleDebug -x lint` → green (1m49s after warm NDK cache).
- Shipped: `Downloads/EchidnaWarsDX-v14.apk` (56,801,145 bytes).
- Host syntax gate: NDK clang++ `-fsyntax-only` on the modified TU → clean.

## v15 addendum (2026-09-18, after v14 device run)

v14 (`ewdx-boot-56.log`, Newl9g.zip) still died with `#Error 7` at the same
spot — the restore-shape divergence was real but not the trigger. v15
(`v15-2026-09-18-err7-forensics`) vendors `hsp3code.cpp` + `hspvar_core.cpp`
(pinned OpenHSP 3dbb872, verified `git rev-parse` == refs.md) with a journal
hook at every array-overflow throw (`ewdx_vm_overflow_journal`): the variable
name, dims, index, and support bits are written into `ctx->refstr` before the
throw, and `ewdx_boot_exec` journals it on error 7. Next crash log will read:

    #Error 7 --> Internal Error(7)
    #Error 7 ... | ERR7 <site> var=<name> id=N flag=N len=[..] idx=N sup=0xN

CMake now compiles `${EWDX_DIR}/vm/{hsp3code,hspvar_core}.cpp` instead of the
upstream ones (marked "ewdx:" blocks; re-sync = copy pinned file + re-apply).

## v16 addendum — ROOT CAUSE CONFIRMED BY v15 FORENSICS

The v15 ERR7 hook fired on device (Newv15.zip, all runs identical):

    ERR7 core:range var=m_dio0_w id=348 flag=2 len=[64,0,0,0] idx=0 sup=0xa

Decoded: `m_dio0_w` (STR) was 1-D `[64,0,0,0]` at crash time; the script's
`npic(m_dio0_w(0, cty))` reads dim2 on the STRICT path (`code_checkarray` ->
`HspVarCoreArray` — used for RHS/array arguments; it never auto-expands), and
`len[2]==0` throws for ANY dim2 index, even 0. The files store `[1,64]`.

Who made it `[64,0,0,0]`: the v14/v15 STR restore itself.
- `master.size` for STR vars is the POINTER-TABLE size (elems*4), not
  payload bytes (256 = 4*64 for ending/st11). The v15 walk bounded the
  payload by `master.size` -> ran into the NEXT entry's data -> FX tag
  mismatch -> `return -1` AFTER `HspVarCoreDim` had already reshaped the
  var to the bogus 1-D blocks count.
- m_obj_w corrupted the same way (it restores before m_dio0_w; the dio0
  loop just throws first).

Also settled by device evidence: strict-path reads need exact restored
shapes (matches Windows hspda behavior); expand-on-write only covers the
LHS (`code_getva` -> `code_checkarray2`) path — the menus' `sound(cnt+1)=`
writes expanded fine, so FLEXARRAY works on device.

v16 fix (`v16-2026-09-18-str-payload-span`): STR restore now (1) validates
the whole block walk against the FILE end first (payload is self-describing:
`size/4` blocks of [FX][len][bytes]), (2) dims to the master's true dims
(zeros preserved), (3) only then fills — a failed restore can no longer
leave a half-rebuilt shape. INT/DOUBLE path unchanged from v14.

## Verification checklist (next device run)

1. Journal now prints one `vload: 'm_*'` line per map var — confirm shapes
   match the table above (e.g. `m_obj len=[17,3,0,0] size=204`).
2. Character select → stage load should pass "Now Loading" without Error 7.
3. If it still trips, the new journal line names the exact variable + shape;
   compare against its `.map`/`.mol`/`.mot` entry.
