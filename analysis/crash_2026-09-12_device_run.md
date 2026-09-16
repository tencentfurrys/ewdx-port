# On-device crash analysis — 2026-09-12 run (boot logs 1–31 + bugreport q7qzcx)

Sources: `ewdx-boot.log (1..31).zip` + `bugreport-q7qzcx-BP2A.250605.031.A3-2026-09-12-22-51-07.zip`
Device: SM-F9660 ("q7q"), Mali g29p0 GLES stack, Android BP2A.250605.031.A3,
app `net.dgate.ewdx` (SDLActivity), PID 5531.

## Verdict

NOT a native crash. No tombstone, no SIGSEGV, no ANR for the game process.
The VM hit HSP error 12 (`HSPERR_FILE_IO`) on the script's own error path and
the process was later killed by Android's cached-app freezer.

## Timeline (from bugreport + persisted logcat)

- 22:45:22 — old build (`appid 10312`) uninstalled (`deletePackageX`).
- 22:46:58 — new APK installed (`appid 10313`).
- 22:47:00.414 — `Start proc 5531:net.dgate.ewdx/u0a313 for next-top-activity`.
- 22:47:00.825 — surface created 1968x2184; `SDL_main` on thread 5585.
- 22:47:00.839 → 01.624 — boot journal: run → SDL init → gpu → first present →
  window up → probe → bootstrap (NO `data/` in APK) → start.ax staged →
  chdir → VM ready → exec begin.
- 22:47:01.176 → 01.624 — script init DLLs: vload_*/vsave_* (save.dat written,
  19 vars), `ex 0x2a` (screen), `ex 0x10` (title "Echidna Wars DX ver1.11"),
  `ex 0x13` (cls), DGINIT, DIINIT, dmmini (OpenSL ready), timeBeginPeriod,
  joyGetNumDevs, DGSCREEN, DGCOLOR, DGCLEAR, DGREDRAW.
- 22:47:01.644 → 01.688 — DGFONT ("ＭＳ ゴシック" 12), `ex 0x14`,
  DGCREATEPRIMITIVE, **DGBUFFER 1,640,480** (`ex 0x03` = first DGBUFFER of the
  script's buffer-alloc block, start.ax line ~500).
- 22:47:01.702 — journal `ex: 0x03` = EXTCMD **0x03 = `dialog`** — the script's
  FIRST dialog: `bmpload "system", 3` → `exist "data\pic\system.bmp"` fails →
  `dialog "画像「system」が見つかりません", 1, "ERROR"` (start.ax L2014-2018).
- 22:47:01.710 — `E ewdx: dialog: ERROR` (message text clobbered by title —
  see bug #1 below). SDL message box grabs focus (VRI[ERROR] window in log).
- 22:47:05.995 — dialog dismissed → `E ewdx: boot: #Error 12 --> Internal
  Error(12)` = `HSPERR_FILE_IO`: next statement after the dialog is
  `sdim opbuf, strsize` with `strsize == -1` → `bload` throws FILE_IO
  (start.ax L2019-2020). VM stops with error.
- 22:47:22 — user leaves the app (`userLeaving=true` → pause/stop).
- 22:48:32.929 — cached-app freezer: `Unable to freeze binder for 5531` →
  `am_kill [0,5531,net.dgate.ewdx,900,Unable to freeze binder interface]`
  → `am_proc_died`. (Side note: native thread was gone after `return -1`
  from SDL_main but the process lingered — either a lingering OpenSL/binder
  thread or SDL native-run JNI teardown.)

The boot-log progression (files 1→31) is the journal growing line-by-line as
each fix/hook was added; it matches the on-device sequence exactly, ending at
`_DGREDRAW@16` because `ex:` EXTCMD lines were only added later.

## Root cause

The APK was built WITHOUT the game `data/` tree
(`boot: no data/ in APK (dev build? push via adb)`). The script kept running
(no `chdir` in dir_c==0 branch), so the first picture load
(`bmpload "system", 3` right after the DGBUFFER block, start.ax L505) hit the
missing-file dialog, then `bload` on `strsize == -1` raised HSP `#Error 12`
(FILE_IO). Everything before that (VM, plugins, audio, DG renders) worked.

## Secondary bug found (shim)

`ewdx_extcmd.cpp` EXCMD_DIALOG fetched `mes` and `title` via `code_gets()`
and kept the raw pointers. Both return pointers into HSP's shared temp param
buffer, so the title fetch overwrote the message before logging — the log
showed `dialog: ERROR` (the title) instead of
`画像「system」が見つかりません`. OpenHSP's `cmdfunc_dialog`
(hsp3gr_wingui.cpp) copies the message into a local buffer first, and reads
the title via `code_getds("")` (optional). Fixed accordingly + mesbuf/titlebuf.

## Boot-shim fix

`ewdx_boot.cpp` now journals `no data/ in APK (assets missing!)` through
`ewdx_boot_journal` (visible in the boot log the user collects) in addition
to the logcat E line, so the next missing-assets run is self-diagnosing.

## Rebuilt APK (2026-09-16)

`EchidnaWarsDX-FULL11-err12fix.apk` built from the user's `err5fix` APK
(data assets complete: 590 files + start.ax + save.dat) with a fresh
`libmain.so` recompiled from GitHub HEAD + the two fixes above (+ first-use
`dll:`/`ex:` journal parity + `audio: opensl unavailable` log line; device
.so otherwise matched HEAD strings). Pipeline: NDK 27.3 `-O2 -fPIC`
(17-TU hsp3core + 12 ewdx TUs + stb_vorbis + main) → link against the
APK's prebuilt libSDL2/libSDL2_ttf/libc++_shared → python-zip swap (so kept
STORED for extractNativeLibs=false, META-INF dropped) → zipalign 4 →
apksigner debug-keystore (V3). Verified: signature valid, alignment ok,
590 assets, new strings in shipped .so.
NOTE: signature differs from any previous install — uninstall the old
`net.dgate.ewdx` first (back up `/data/data/net.dgate.ewdx/files/save.dat`
if wanted: `adb exec-out run-as net.dgate.ewdx cat files/save.dat > save.dat`).

## 2026-09-16 session: root cause #2 (asset enumeration) + build tag

Second device run (bugreport 19:10): 3 attempts, all `Unable to freeze
binder` kills after user leaves, no native crash. CRITICAL FINDING: all
three runs still executed the OLD err5fix binary (old log strings:
`no data/ in APK (dev build? push via adb)` + clobbered `dialog: ERROR`);
the err12fix v1 build was never actually exercised (video 14:08 predates
it; installs at 19:03:26 + 19:04:29 were err5fix re-installs).

NEW ROOT CAUSE found in err5fix regardless: `boot_copy_tree` relies on
`AAssetDir_getNextFileName`, which lists FILES ONLY — subdirectories are
invisible to the NDK asset API. `assets/data/` contains ONLY subdirs
(map/mold/mot/music/pic/se), so `openDir("data")` enumerated empty →
unpack staged 0 files on EVERY build, even with all 590 assets inside the
APK. Explains why err5fix (data bundled) still hit the missing-picture
dialog.

Fix (v2): bootstrap walks the explicit subdir list (per assets/README.md),
per-pool staging counts logged, six pools verified on-disk (21/62/138/10/
106/253) before the "already staged" marker is written (mismatch → retry
next launch), plus `build v2-...` tag journaled after `=== run ===` so
future logs self-identify their binary.

## Next steps

1. Bundle `assets/data/{map,mold,mot,music,pic,se}` per
   `android/app/src/main/assets/README.md` (~100.4 MB) or adb-push the tree
   into `/data/data/net.dgate.ewdx/files/data/` for dev builds.
2. Rebuild, reinstall (fresh appid each install — journal path is per-filesDir,
   so it survives), relaunch: expect the journal to continue past
   `bmpload "system"` through picture staging into menu BGM (ovplay).
3. Watch for the same dialog path on other assets (se/music/mot) — every
   loader has an exist+dialog guard that will mask the real failure as
   `#Error 12`; the loud journal lines now make the first failure obvious.

## Run 2026-09-16 19:48 (v2 device test) — root cause #3: audio self-deadlock

Journal: `build v2` tag confirmed, `data/ unpacked (590 files)` + `verify OK`,
`vsave: wrote 19 vars`, `title: Echidna Wars DX ver1.11`, then
`_DGINIT/_DIINIT/_dmmini` … nothing. No dialog, no error — VM hung forever on
the VM thread inside `dmmini`; user saw a black screen with no sound; cached-app
freezer killed the process after the user gave up (~5 min).

Cause: `ewdx_audio_init()` held `au_mtx` across `au_sl_start()`, whose buffer
prefills call `ewdx_audio_fill()` → locks the same non-recursive mutex on the
same thread. First build where this executes: v2 is the first where `dmmini`
actually runs (earlier runs died at `data/` staging before the audio init).
The 09-12 "success" run used the unpushed device build, so HEAD's audio path
had never been exercised on-device before this test.

Fix (v3): init resets state lock-free, OpenSL start runs without the lock, and
`au_ready` is set under the lock afterward. All 13 OpenSL failure sites now log
`audio: <step> failed (0x%x)` into the boot journal so a silent-fail audio path
self-diagnoses. Buffer prefills happen outside the lock; `au_sl_idx = 1` is set
before `SetPlayState(PLAYING)` so the first callback refills the buffer that
was just consumed. Build tag: `build v3-2026-09-16-audio-deadlock`.

Expected after v3 install: journal continues past `_dmmini` into `tmset`
(`timeBeginPeriod`) and the draw chain (`DGSCREEN/DGCLEAR/DGREDRAW` loop) —
picture, menu, and audio all live.

## Runs 2026-09-16 20:36 + 20:36:38 (v3 device test) — root causes #4 + #5

Run 1 journal: v3 tag, data/ verified, vload/vsave green, ex 0x2a/0x10/0x13,
DGINIT/DIINIT green, then `_dmmini@16` -> OpenSL TrackPlayerBase created (the
v3 deadlock fix WORKS, init completes in ~11 ms) -> `ex: 0x03` dialog fired ->
`dialog: DirectSound\x82̏\x89...` (REAL SJIS message — v1 dialog fix works) ->
**FATAL: JNI DETECTED ERROR: input is not valid Modified UTF-8 (0x82)** in
NewStringUTF from nativeRunMain -> ART abort -> SIGABRT -> process death.
(Explains journal truncation after `ex: 0x2a` in the exported file: the crash
context line is only flushed on next start, and the abort kills mid-line.)

Two chained causes:
1. (#4) ovplay `cmd_0_0` (DirectSound probe) returned `ewdx_audio_init()`'s
   internal -1=ok as stat; script reads `if (stat)` as FAILURE -> bogus
   "DirectSoundの初期化に失敗しました" dialog on a healthy audio system.
2. (#5) dialog shim passed raw SJIS bytes to SDL_ShowSimpleMessageBox, which
   on Android calls JNI NewStringUTF — Modified-UTF8-invalid bytes abort ART
   and kill the whole process (any SJIS dialog = instant death).

Run 2 (auto-relaunch 32 s later): journal confirms crash-context works —
`previous run stopped at: SIGNAL 6 at stage: ex: 0x03` — then stalled in GL
(window/context) init ~30 s after the abnormal restart; secondary to the abort.

Fix (v4): `cmd_0_0` always reports stat 0 (audio init runs; its failure mode
is silent-safe by design), and dialog/title shims convert SJIS->UTF-8 via the
shared `ewdx_sjis_to_utf8` (ewdx_text table) before any SDL/JNI boundary.
Build tag: `build v4-2026-09-16-sjis-dialog`.

## Run 2026-09-16 21:00 (v4 device test) — root cause #6: PNGs renamed to .bmp

v4 fixes confirmed on device: NO bogus DirectSound dialog, NO SIGABRT — clean
progression through DGINIT/DIINIT/dmmini/timeBeginPeriod/joyGetNumDevs,
DGSCREEN/DGCOLOR/DGCLEAR/DGREDRAW/DGFONT (MS-Gothic request logged), DGCREATEPRIMITIVE,
DGBUFFER, DGLOADMEMORY slot 3 (system.bmp 656x519) OK — then
`DGLOADMEMORY failed (slot 7, 227883 bytes)` on `bmpload "title1", 7`.
Game script showed its own English-mode error dialogs (picture: title1 not
found + the canned "anti-virus software" hint) and CLEAN EXIT (endcode=0).
Three runs, all identical/deterministic.

The "anti-virus" dialog is the ORIGINAL GAME's message (start_ax_dump.hsp
english-mode branch), not a device AV problem: the Windows release shipped
data files that AV tools habitually quarantined, so the author added that hint
to the picture-load error path. On the port it fired because the texture
decode failed, not because of AV.

Actual cause: `title1.bmp` (and `obj_sp.bmp`) are PNG files RENAMED to .bmp
(magic `89 50 4E 47`; the rest of data/pic is genuine BMP: 104/106). The
original hmm.dll sniffs magic bytes and decodes both formats; the port's
DGLOADMEMORY used SDL_LoadBMP_RW only. Fix (v5): magic-byte sniff in
ewdx_loadmemory -> stb_image PNG path (vendored thirdparty/stb_image_impl.c,
memory-only, 4ch) sharing the flip + black-colorkey texture tail; journal
logs `loadmem: PNG decoded (slot n, WxH)`. Build tag
`build v5-2026-09-16-png-bmp`.

## Run 2026-09-16 21:25 (v5 device test) — root cause #7 + full asset sweep

v5 PNG fix CONFIRMED on device: `loadmem: PNG decoded (slot 7, 640x480)`.
Run then progressed into real title compositing: Sleep/timeGetTime, DGGSEL,
DGSCALEANDANGLE, DGPOS, DGRECT, DGBLENDMODE, DGGCOPY -> **#Error 21**
(HSPERR_UNSUPPORTED_FUNCTION) thrown inside the DGGCOPY dispatch. VM error
dialog (38 s), then clean exit. Run 2 (relaunch) confirmed stable boot with
data/ already staged.

Cause: start.ax declares `#func DGGCOPY "_DGGCOPY@16"` (double G) but the
lookup table registered only `"_DGCOPY@16"` — a name the game never imports.
Sweep of ALL 49 #func/#cfunc imports vs the lookup found exactly this one miss.

Asset magic-byte sweep (user request) — all clean:
- map 21x .map / mold 62x .mol / mot 138x .mot: all `hspv` magic, uniform.
- music 10x .ogg: all real Vorbis (stb_vorbis OK).
- se 253x .wav: all PCM (fmt=1); rates 44.1k/22.05k/11.025k/8k —
  au_wav_to_mix already resamples (fast path only for 44.1k).
- pic: 104 BMP + 2 PNG-renamed (title1, obj_sp) — v5 handles.

Fix (v6): `_DGGCOPY@16` registered (alias `_DGCOPY@16` kept); lookup-miss
fallback now logs + journals the exact missing command name before throwing
UNSUPPORTED. Build tag `build v6-2026-09-16-dggcopy`.
