# ewdx_ndk — EchidnaWarsDX Android translation layer (draft)

Ports the hmm.dll `DG*`/`DI*` surface used by `start_ax_dump.hsp` onto
SDL2 + OpenGL ES 2.0 inside the OpenHSP NDK runtime.

## Files

| File | Contents | Status |
|---|---|---|
| `ewdx_gles.h` | State structs (`EwdxGles`, `EwdxDrawState`, `EwdxBuffer` ×128 + vflip), blend enum 0–7, API decls | done |
| `ewdx_gles.cpp` | SDL+GLES2 init, `DGSCREEN` viewports, FBO offscreens, `DGCOLOR/CLEAR`, present+flush, `glBlendFunc` table | done (step b) |
| `ewdx_batch.h/.cpp` | Quad batcher (verbatim FUN_10001fa0 math), 256-step LUT, colorkey upload, prim/line paths, font stubs | done (step c) |
| `ewdx_dg.cpp` | Script-facing glue, exact `#func` arities, `stat`-on-failure contract | done (b + c) |
| `ewdx_hspv.h/.cpp` | Dependency-free `hspv` reader (hspda-compatible) | done (step a) |
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

- (b2) Register commands in `hsp3ext_ndk.cpp` via `code_enable_typeinfo()`/
  `BindFUNC()` (ARM64 param marshaling with `code_getdi`, not stdcall `@16`).
- (d) Audio: `dmm*` voices (OpenSL/Oboe) + ovplay `cmd_0_5/0_6/0_7` streaming
  OGG with loop points; `DI*` via `AInputQueue`; `vload/vsave` runtime hookup
  reusing this `hspv` layout; `DGFONT/DGDRAWTEXT` via SDL_ttf; `dialog/end` →
  log shim.
- NDK gate: all 5 TUs pass `aarch64-linux-android21-clang++ -fsyntax-only`
  (only benign `-Wunused-function` on glue awaiting b2).
