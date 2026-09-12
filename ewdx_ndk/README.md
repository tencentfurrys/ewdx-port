# ewdx_ndk — EchidnaWarsDX Android translation layer (draft)

Ports the hmm.dll `DG*`/`DI*` surface used by `start_ax_dump.hsp` onto
SDL2 + OpenGL ES 2.0 inside the OpenHSP NDK runtime.

## Files

| File | Contents | Status |
|---|---|---|
| `ewdx_gles.h` | State structs (`EwdxGles`, `EwdxDrawState`, `EwdxBuffer` ×128 + vflip), blend enum 0–7, API decls | done |
| `ewdx_gles.cpp` | SDL+GLES2 init, per-target viewports, FBO offscreens, `DGCOLOR/CLEAR`, present+flush, `glBlendFunc` table, flush-before-state-change ordering | done (step b + c harden) |
| `ewdx_batch.h/.cpp` | Quad batcher (verbatim FUN_10001fa0 math, audited L89-222), 256-step LUT, colorkey upload, prim/line paths, `ewdx_immediate_quad` for text | done (step c) |
| `ewdx_dg.h/.cpp` | Script-facing glue, exact `#func` arities, `stat`-on-failure contract, `DI*` lifecycle | done (b + c) |
| `ewdx_input.h/.cpp` | SDL pump (touch stick + keys + gamepad) -> joyg 10-bit mask (bit0-3 dpad, bits4+ buttons per `#deffunc joystick`) | done (input) |
| `ewdx_text.h/.cpp` | SDL_ttf string cache (16-entry LRU) + immediate quads; SJIS->UTF8 via generated table; CJK system-font resolve | done (text) |
| `ewdx_sjis_tab.h` | GENERATED: 304 CP932 pairs observed in the AX DS segment | done (text) |
| `ewdx_ovplay.h/.cpp` | ovplay.dll EXTCMD hook: HPIDAT scan + `code_gettypeinfo(-1)` typeinfo, `cmd_0_*` -> bgm api | done (ovplay hook) |
| `tests/test_input.cpp` (+`shim/`) | Host verification (20 checks: keys/touch-zones/order/combine) | done (input) |
| `ewdx_audio.h/.cpp` | dmm* SE bank (WAV PCM) + software mixer + OGG BGM streamer w/ sample-accurate loop points, OpenSL ES backend, NULL backend for host tests | done (step d) |
| `tests/test_audio.cpp` | Host verification (57 checks: parse/mix/pan/loop/status) | done (step d) |
| `thirdparty/stb_vorbis.c` | Vendored OGG decoder v1.22 (public domain; slimmed `NO_PUSHDATA/NO_STDIO`, `-w`) | done (step d) |
| `ewdx_register.h/.cpp` | b2 name-dispatched `TYPE_DLLFUNC` cmdfunc/reffunc (~50-entry surface: hmm/hspda/hspogg/system), per-group `stat` contract; `vload/vsave` live restore (name-keyed INT/DOUBLE/STR, 32-bit hspv I/O) | done (step b2 + STEP-F) |
| `ewdx_hspv.h/.cpp` | Dependency-free `hspv` reader (hspda-compatible) | done (step a) |
| `ewdx_boot.h/.cpp` | STEP-F: filesDir probe + AssetManager `data/`/`save.dat` bootstrap + `start.ax` APK->filesDir staging with SJIS-safe DS `\`->`/` patch + chdir + `Hsp3::Reset` boot + SDL `msgfunc`/exec pump + `hsp3ext_getdir` | done (STEP-F part 2) |
| `ewdx_extcmd.h/.cpp` | STEP-F minimal EXTCMD/EXTSYSVAR shims (screen/title/cls/dialog/mouse/getkey/stick/mes/pos/font + ginfo/dirinfo/sysinfo; opcodes per hsp3gr_dish) | done (STEP-F part 1+2) |
| `ewdx_supio_decl.h` / `ewdx_supio.cpp` | supio port (SJIS string utils, POSIX fs, logcat Alerts; force-included for hsp3core so HSPUTF8 stays OFF) | done (STEP-F part 2) |
| `CMakeLists.txt` | Static-lib fragment for the NDK build | done |

## Evidence backing this draft

- **hspv format**: `src/plugins/win32/hspda/Hspda.cpp` (`HSP3VARFILECODE "hspv"`,
  `HSP3VARFILEVER 0x1000`, FX tag `0x55AA0000`). Harness `hspv_harness.py`
  parses real `st00.map`/`st11.map`/`player.mol`/`mirea1.mot` with zero errors
  (13/13/3/5 vars, names + dims + payloads all consistent).
- **Blend modes**: Ghidra `HSP3Runtime` project, `hmm.dll` `FUN_10001d70` —
  vtable+`0xE4` = `IDirect3DDevice9::SetRenderState`,
  `0x13`=SRCBLEND / `0x14`=DESTBLEND. Script uses modes 0–4.
- **Call shapes**: first call-sites in `start_ax_dump.hsp`
  (`DGSCREEN` L487, `DGBUFFER` L500–505, `DGCOLOR` L493, `DGRECT` L2080,
  `DGSCALEANDANGLE` L2070, `DGGCOPY` L2142, `DGLOADMEMORY` L2022 …).
- **Asset paths**: `dir_c==0` → `data\<sub>\`+name+ext for all six loaders;
  no `chdir`; images via `bload`→`DGLOADMEMORY`; BGM loop points per track
  (e.g. `1.ogg` loopstart (3:04.61)→14.21s @44.1kHz).

## Open items (next steps)

- (b2) ~~Register commands in `hsp3ext_ndk.cpp`~~ DONE:
  `ewdx_register()` overrides `cmdfunc`/`reffunc` at the end of
  `hsp3typeinit_dllcmd()` (OpenHSP `src/hsp3/ndk/hsp3ext_ndk.cpp`, 3-line
  hunk), so stock `hsp3eb_execstart()` picks it up for `TYPE_DLLFUNC`.
  Name-dispatched (`_DGINIT@16` etc., never finfo index); ARM64-safe
  `code_getdi/gets/getva/getsptr` pulls per the dump's minfo decls; full
  `sortval/get` (hsp3int parity) + dependency-free INI/time/LCID/joystick
  shims; `vload/vsave` live restore (STEP-F: name-keyed INT/DOUBLE/STR via
  `code_getdebug_varname/varid` + 32-bit hspv writer, `save.dat` round-trip);
  `dmm*` full SE bank + mixer (step d).
- (d/e) Final stubs DONE:
  - Input: `ewdx_input` pumps SDL (touch virtual-stick + Z/X zones, arrows +
    Z/X/C/A/S/D scancodes, first gamepad); `DIGETJOYNUM`=1 so the script
    takes the `DIGETJOYSTATE` branch and `joyg` carries the 10-bit mask
    (`#deffunc joystick` layout: bits0-3 UDRL, bits4+ buttons). Pumped on
    `DGREDRAW` + every input read. Host test 20/20 (synthetic events).
  - Text: `ewdx_text` renders via SDL_ttf into a 16-entry string LRU cache
    (utf8 hash + size + DGCOLOR) and draws immediate quads through the
    shared shader (flush-first). Fonts resolve from Android system CJK
    fonts; MS-Gothic/Mincho names are documented-unavailable.
  - ovplay hook: `ewdx_ovplay` replicates `Hsp3ExtAddPlugin` TYPEFUNC path
    (HPIDAT scan -> `code_gettypeinfo(-1)` -> `code_enable_typeinfo`);
    `cmd_0_0/1/3/5/6/7/11` drive `ewdx_bgm_*` (double args truncated via
    `code_getdi`, matching the original); `cmd_0_254` stub (dead path).
- STEP-F part 2 DONE (link + package): `hsp3core` static lib (17 TUs:
  OpenHSP hsp3/hsp3code/hsp3debug/hsp3int/hspvar x5/stack/strbuf/strnote/
  dpmread/filepack/hsp3crypt/hsp3utfcnv + `ewdx_supio.cpp`) links into
  `libmain.so`; `SDL_main` bootstraps assets, `chdir`s to filesDir,
  `Hsp3::Reset("start.ax")`, installs `ewdx_register` (DLLFUNC + ovplay
  hook) + `ewdx_extcmd_register` (EXTCMD/EXTSYSVAR) + SDL msgfunc, and
  `code_execcmd()` runs the script to END/ERROR. `DGGCOPY` now forwards
  its flags arg (title uses 1/2/8); `ewdx_init` is idempotent (script
  calls `screen` before `DGINIT`).
- Device verification (needs game `data/` + device): install the APK,
  first launch unpacks `data/` + stages patched `start.ax`, logcat
  `ewdx:` shows probe -> `HSP VM ready` -> title (`label_238`).
- NDK gate: all ewdx TUs (13 C++ + `stb_vorbis.c`) pass
  `aarch64-linux-android21-clang(++) -fsyntax-only` (`-DHSP64`, zero
  warnings under `-Wall -Wextra`; third-party TU under `-w`); the 17-TU
  `hsp3core` (OpenHSP VM + `ewdx_supio.cpp`, `-DHSP_COM_UNSUPPORTED`,
  `-include ewdx_supio_decl.h`) compiles with only pre-existing upstream
  `-Wwritable-strings`/`-Wunsequenced` warnings; wired `hsp3ext_ndk.cpp`
  passes with zero new warnings (4 pre-existing `-Wwritable-strings` in
  stock code, verified pristine-vs-wired).
- Host tests (MinGW): `tests/test_audio.cpp` — 57 checks green (NULL
  backend); `tests/test_input.cpp` — 20 checks green (host SDL2 static +
  `tests/shim/ewdx_gles.h`, production code unmodified).
