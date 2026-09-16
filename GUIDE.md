# EchidnaWarsDX → Android Port — Complete Project Guide

Reverse-engineering + port project: Windows HSP3 game
**Echidna Wars DX v1.11 (D-Gate/ASIMOFU, 2016–2017)** → Android APK.
All work below was executed and verified on 2026-09-11/12.

## 0. TL;DR status

- [x] Game binary fully reverse-engineered (PE, imports, HSP3 runtime API)
- [x] Decrypted AX (HSP3 bytecode) dumped from live process memory (764,226 B)
- [x] AX decompiled to 31,013 lines of HSP3 source (`artifacts/`)
- [x] `hspv` asset format parsed + validated against real files (ALL PASS)
- [x] All 154 runtime functions mapped; blend/scale/colorkey recovered from Ghidra
- [x] NDK translation layer (`ewdx_ndk/`) + Gradle scaffold (`android/`)
- [x] NDK `arm64` syntax gate green on all translation units
- [x] (b2) HSP command registration (`ewdx_register.cpp`, wired into
      `hsp3ext_ndk.cpp`; gate green, zero warnings)
- [x] (c) batcher hardened — LUT init, flush-before-state-change, per-target
      viewports, FUN_10001fa0 math audited line-by-line (host-verified logic)
- [x] (d) audio pipeline (`ewdx_audio`: OpenSL ES + WAV SE bank + OGG BGM
      streamer, sample-accurate loops; 57-check host test green)
- [x] (e) input (`ewdx_input`: touch stick/buttons + keys + pad -> joyg mask;
      20-check host test green), text (`ewdx_text`: SDL_ttf string cache +
      SJIS table), ovplay `cmd_0_*` EXTCMD hook (HPIDAT/typeinfo)
- [x] (f1) STEP-F part 1: `vload/vsave` live restore (name-keyed
      INT/DOUBLE/STR, 32-bit hspv writer), minimal EXTCMD/EXTSYSVAR shims
      (`ewdx_extcmd`: screen/title/cls/dialog/mouse/getkey/stick/mes/pos/font
      + ginfo/dirinfo/sysinfo), boot probe + `start.ax` APK staging
      (`ewdx_boot`), `HSP64` + zero-warning gate on all 13 TUs,
      `assembleDebug` green with `start.ax` bundled
- [x] (f2) STEP-F part 2: OpenHSP VM linked (`hsp3core`, 17 TUs) +
      SJIS `ewdx_supio` port (HSPUTF8 stays OFF) + AssetManager `data/`
      bootstrap + SJIS-safe AX `\`->`/` patch + `Hsp3::Reset` boot +
      `code_execcmd` SDL pump + `DGGCOPY` flags; `assembleDebug` green,
      `start.ax` bundled, HSP symbols verified in `libmain.so`
- [ ] On-device run to title (needs game `data/` tree + device; first
      launch unpacks `data/`, logcat `ewdx:` traces probe -> VM ready)
      2026-09-12 run: VM + plugins + audio + DG init all WORK on device
      (SM-F9660); run stopped at first `bmpload` because the APK was built
      WITHOUT `assets/data/` -> missing-file dialog -> `bload` on
      `strsize==-1` -> `#Error 12` (FILE_IO); process later killed by the
      cached-app freezer (not a native crash). Full timeline:
      `analysis/crash_2026-09-12_device_run.md`. Fixes: dialog shim
      param-buffer aliasing + optional title (`code_getds`), loud
      `no data/ in APK` boot journal. Gate re-verified green on both TUs.
      2026-09-16 runs (v2 build tag): `data/` asset-recurse fix CONFIRMED on
      device (`unpack (590 files)` + `verify OK`), VM + vsave + title green,
      then hang inside `dmmini` — `ewdx_audio_init()` self-deadlocked `au_mtx`
      (prefills call `ewdx_audio_fill` under the same non-recursive mutex).
      Fix (v3): init/OpenSL start lock-free + per-step `audio: <step> failed`
      journal. Timeline: `analysis/crash_2026-09-12_device_run.md`.

## 1. Environment (build host)

| Tool | Version / path |
|---|---|
| OS | Windows x64 |
| Android SDK | `C:\Android\android-sdk` (platforms 34–37, build-tools ≤37.0.0) |
| Android NDK | `27.3.13750724` (`ndk/27.3.13750724`) |
| CMake / Ninja / Gradle | system installs (`cmake.exe`, `ninja.exe`, `gradle.exe`) |
| JDK (Ghidra) | Microsoft OpenJDK 21.0.12 (`JAVA_HOME` must point at 21+, NOT 17) |
| Ghidra | 11.3.2 (`C:\tools\Ghidra\ghidra_11.3.2_PUBLIC`), projects in `Default Project/ghidra_projects/` (`.rep` DBs NOT backed up here — see §10) |
| Python | 3.12 (all `tools/*.py` harnesses) |
| .NET SDK | 8.0.425 (deHSP build) |

Reference checkouts (NOT in this repo — re-clone via `refs.md`):
`onitama/OpenHSP`, `libsdl-org/SDL` (branch `SDL2`), `SoulMelody/deHSP`.

## 2. Target identification

- `EchidnaWarsDX.exe`: 32-bit x86, MSVC 7.10 (VS2003), timestamp 2013-03-01,
  entry `0x0042188e`, base `0x00400000`. NOT .NET (CLR header RVA = 0;
  the `mscoree.dll` string is an error message, not an import).
- Engine: **HSP3** (`OnionSoftware.hsp`, `hspwnd`, `HSPERROR` strings).
- Runtime DLLs (all x86, loaded at fixed bases under WOW64):
  `hmm.dll` 608 KB @`0x10000000` (114 exports: DD 26 / DG 24 / DI 14 / DM 6 /
  DS 33 / HM 10), `hspda.dll` 57 KB, `hspogg.dll` 236 KB, `ovplay.dll` 131 KB.
- `hmm.dll` imports `d3d9.dll` (`Direct3DCreate9`) + `DDRAW.dll` → D3D9 renderer.

## 3. RE process log (reproducible)

1. Listed `Downloads/` → `Echidna_Wars_DX_V1.11_ENG-JAP.zip` (49 MB) → extracted
   to temp; found game tree (`data/{map,mold,mot,music,pic,se}/`, exe + 4 DLLs).
2. PE analysis (LIEF + manual IAT walk): full import table resolved
   (`GetVersionExA`@`0x42C130`, `GetModuleHandleA`@`0x42C138` at entry, …).
3. Ghidra: imported exe + `hmm.dll` + `hspda.dll` into `HSP3Runtime` project.
4. Cloned OpenHSP → recovered full HSP3 VM architecture (see §5).
5. Built deHSP → exe reports **encrypted** (`HSP3Crypt` is a no-op dummy in
   open source; real cipher is closed-source) → went dynamic (step 6).
6. **Dynamic dump** (`tools/hsp_stage4_dump.py`): launched game, scanned heap
   for `HSP3` magic with structural validation
   (`pt_cs + max_cs == pt_ds`), dumped `allsize` bytes → `start_ax_dump.bin`.
7. deHSP on the dump → `start_ax_dump.hsp` (814 KB, 31,013 lines). Decompile OK.
8. Asset cross-checks (`tools/verify_*.py`) → §8. Ghidra deep-dives → §7.

Re-run dynamic dump: `python tools/hsp_stage4_dump.py` (game must launch;
kills it after). Then:
`dotnet deHSP <bin> -o <dir> -d Dictionary.csv` (deHSP build notes in refs).

## 4. Artifact inventory (this repo)

| Path | Contents |
|---|---|
| `artifacts/start_ax_dump.bin` | Decrypted HSP3 AX, 764,226 B (`HSP3` v3.01, 833 vars) |
| `artifacts/start_ax_dump.hsp` | Decompiled source, 31,013 lines, 190 `#deffunc`, 55 labels, 50 `#func` binds |
| `ewdx_ndk/` | NDK translation layer (gles/batch/dg/hspv + CMake fragment + README) |
| `android/` | Gradle scaffold (settings/root/app gradle, manifest, native CMake, SDL entry stub, asset map) |
| `tools/` | All RE harnesses: memdump stages, AX/header/LINFO scanners, call-site miners, asset verifiers, `hspv_harness.py` |
| `analysis/` | Raw Ghidra decompile outputs + `DecompOne.java` (multi-target + FUN autofollow) + `HSP3CrossRef.java` |
| `GUIDE.md` | This file. `refs.md`: clone URLs + SHAs + SDK/NDK pins. |

## 5. HSP3 VM + AX format (from OpenHSP source)

- AX magic `HSP3`, header 72 B: ver/max_val/allsize/pt+max for CS, DS, OT,
  dinfo, linfo, finfo, minfo (+ finfo2/hpidat/sr/exopt).
- Instructions are 16-bit words: `[EXFLG_3|EXFLG_2|EXFLG_1|EXFLG_0 | TYPE(12b)]`
  + 16- or 32-bit value → 4–6 B per instruction.
- TYPEs: 0 MARK, 1 VAR, 2 STRING, 3 DNUM, 4 INUM, 5 STRUCT, 6 XLABEL, 7 LABEL,
  8 INTCMD, 9 EXTCMD, 10 EXTSYSVAR, 11 CMPCMD(`if`), 12 MODCMD, 13 INTFUNC,
  14 SYSVAR, 15 PROGCMD, 16 DLLFUNC, 17 DLLCTRL.
- Full opcode tables (INTCMD 0x00–0x30, INTFUNC int/str/double, PROGCMD,
  SYSVAR, EXTCMD Win32 ~80, DLLCTRL) were recovered — see session notes in
  `analysis/` and OpenHSP `src/hsp3/hsp3int.cpp`, `hsp3code.cpp`.
- Plugin calls: AX HPIDAT/LINFO names the DLL (`hmm.dll` @0x72 in DS, …),
  FINFO STRUCTDAT resolves `GetProcAddress`; x86 stdcall `@16` (ARM64 port
  must marshal via `code_getdi`, NOT varargs — see ewdx README b2 note).
- EXE embedding: `HSPHED~~` option block (DPM offset string, `hsp_sum`,
  `hsp_dec` deckey); ~756 KB appended past PE sections = encrypted pack.
  Runtime decrypts to heap (see §3 step 6 for the capture method).

## 6. DG\* bindings (exact `#func` lines + observed call shapes)

```
DGINIT | DGSCREEN w,h,mode,32,0 (640x480 / 1280x960, mode 1 then 0)
DGBUFFER id,w,h (1:640x480 2:256x256 3:512x512 4:640x480 5:320x240 6:256x256)
DGGSEL id | DGCOLOR r,g,b,a | DGCLEAR | DGPOS x,y
DGRECT sx,sy,sw,sh (e.g. 8x8 font cells) | DGSCALEANDANGLE 256,256,0
DGBLENDMODE m | DGGCOPY id | DGTEXTURE id | DGCREATEPRIMITIVE n
DGADDPRIMITIVE | DGDRAWPRIMITIVE | DGFONT name,12 | DGDRAWTEXT s,320,240
DGLINE x1,y1,x2,y2 | DGLOADMEMORY buf,size,slot | DGREDRAW | DGEND
DIINIT/DIEND | DIGETJOYNUM | DIGETJOYSTATE joyg,0
dmmload/play/stop/vol/pan (SE slots) | ovplay cmd_0_5/0_6/0_7 (BGM stream+loops)
vload_start/get/end + vsave_* (hspda) | sortval/sortget | winmm/kernel32 time+ini APIs
```

## 7. Ghidra findings (hmm.dll, project `HSP3Runtime`)

All exports are thunks `FUN_*(…,&DAT_10092cc8,…)`; return `-1` ok / `0` fail
(HSP `stat` convention: `stat==0` = error path in script).
- `FUN_10001d70` (blend): vtable+`0xE4` = `SetRenderState`,
  `0x13`=SRCBLEND/`0x14`=DESTBLEND. Modes →
  0:(ONE,ZERO) 1:(SRCALPHA,INVSRCALPHA) 2:(SRCALPHA,ONE) 3:(ZERO,INVSRCCOLOR)
  4:(ZERO,SRCCOLOR) 5:(INVDESTCOLOR,ZERO) 6:(ONE,ONE) 7:(DESTCOLOR,ONE).
  Script uses 0–4. → `glBlendFunc` table in `ewdx_gles.cpp`.
- `FUN_10002460` (scale/angle): scale float, **angle &= 0xff** (256-step LUT).
- `FUN_10001fa0` (copy): 4-vert TRIANGLEFAN, stride 0x1c (XYZRHW|DIFFUSE|TEX1),
  rotate-about-center via LUT @ctx+`0xeec`/`0xfec`, scale÷256, 0.5 offsets,
  UV upper-clip + per-buffer V-flip + mirror bits, id<0x80 guard.
  NO alpha-test/stage-state calls in the draw path.
- `FUN_10001b30` (load): `D3DXCreateTextureFromFileInMemoryEx`, forced
  `A8R8G8B8`, **ColorKey `0xff000000` (black = transparent)** → CPU colorkey
  in `ewdx_batch.cpp`. `.rdata` floats: 1.0 / 0.5 / 1/256 confirmed on disk.
- `FUN_100012c0` (init): `Direct3DCreate9(D3D_SDK_VERSION)` + HWND store.
- Buffer descriptor stride `0x1c`: +0x78 tex, +0x7c surface, +0x80 w, +0x84 h,
  +0x90 flip flag. Raw decompiles in `analysis/decomp_*.txt`.

## 8. Assets (source: Downloads zip — NOT in this repo)

100.4 MB: mot 138 / mold 62 / map 21 / pic 106 / se 253 / music 10 + `save.dat`.
Script path logic (`dir_c==0` branch, all six loaders): `"data\<sub>\"+name+ext`,
no `chdir`; images via `bload`→`DGLOADMEMORY` (memory upload — APK-friendly);
mot/mol/map via `vload` (`hspv` dumps); SE via `dmmload`, BGM via `ov_load`
with per-track loop points (e.g. `1.ogg` (3:04.61)→14.21 s @44.1 kHz).
Verification: 33/33 map dio pic refs → `.bmp`, 44/44 mot pic refs → `.bmp`,
78/62/32/115 cross-pool refs resolve; 29 `t_obj_w` orphans are internal mold
labels, never paths. APK staging map: `android/app/src/main/assets/README.md`.

## 9. Port architecture (`ewdx_ndk/` + `android/`)

Single id space 0–127 (orig bound); `DGBUFFER` ids own FBOs (`vflip=1`),
`DGLOADMEMORY` ids are textures (`vflip=0`). Retained-state struct
(pos/rect/8.8-scale/8-bit-angle/color/blend) → run-grouped quad batcher
(1024 quads, flush on tex/blend/target change + present), verbatim vertex math
incl. D3D −0.5 with NDC `(X+0.5)/W·2−1`. NDK gate: all TUs pass
`aarch64-linux-android21-clang++ -fsyntax-only` (only benign unused-glue
warnings pending b2). APK link gated on: b2 registration, SDL2 `.so` build,
(d) audio/input/text.

## 10. Deliberately NOT backed up here

- Raw game zip/exe/DLLs/`data/` tree (rights + size; source = Downloads zip).
- `OpenHSP`/`SDL2`/`deHSP` working clones (see `refs.md` for URLs + SHAs).
- Ghidra `.rep` DBs (~25 MB binaries; decompile outputs kept in `analysis/`).
- Android SDK/NDK, JDK, Ghidra installs (see §1 paths/versions to reinstall).
