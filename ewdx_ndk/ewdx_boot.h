// ewdx_boot.h - STEP-F HSP boot: asset staging + start.ax load + VM exec.
//
// Boot order (ewdx_main -> SDL_main):
//   1. ewdx_boot_probe()      log filesDir + start.ax status (never fatal)
//   2. ewdx_boot_bootstrap()  AssetManager: unpack start.ax + data/ + save.dat
//                             into filesDir on first launch (device only;
//                             host/dev builds without APK assets: no-op)
//   3. ewdx_boot_stage_ax()   ensure filesDir/start.ax exists (APK fallback),
//                             patching Win32 '\\' path separators in the AX DS
//                             segment to '/' (SJIS-safe: only ASCII-flanked)
//   4. ewdx_boot_chdir()      chdir(filesDir) so HSP relative loads resolve
//   5. ewdx_boot_startup()    Hsp3::Reset("start.ax") + ewdx_register +
//                             ewdx_extcmd_register + msgfunc install
//   6. ewdx_boot_exec()       code_execcmd() to END/ERROR (blocks; pumps via
//                             msgfunc WAIT/AWAIT with SDL_Delay)
//   7. ewdx_boot_bye()        delete Hsp3 (termfunc + Dispose + code_bye)
//
// Steps 5-7 need the hsp3core static lib (OpenHSP VM) linked into libmain.
#ifndef __EWDX_BOOT_H
#define __EWDX_BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

// Ensure filesDir exists + report asset presence. Always 0 (never fatal);
// details go to logcat so first-launch triage is one adb line.
int ewdx_boot_probe(void);

// Unpack APK assets into filesDir (start.ax if newer, data/ once via
// marker, save.dat only if absent so progress is never overwritten).
// Returns staged-file count (>=0). Never fatal.
int ewdx_boot_bootstrap(void);

// Stage one file APK-assets -> filesDir if the filesDir copy is missing.
// src/dst are HSP-style relative paths ("start.ax", "data/pic/a.bmp").
// Returns 1 staged, 0 already present, <0 failed (logged).
int ewdx_boot_stage(const char *path);

// Stage start.ax specifically + patch DS backslashes (see .cpp).
// Returns 1 staged/present, 0 missing (caller shows dialog path).
int ewdx_boot_stage_ax(void);

// chdir(filesDir). Returns 0 ok, <0 failed (logged; boot continues so
// logcat shows the follow-on fopen errors with full paths).
int ewdx_boot_chdir(void);

// Hsp3 boot (steps 5). Returns 0 ok, <0 failed (dialog already shown).
int ewdx_boot_startup(void);

// Run the VM to END/ERROR. Returns the HSP endcode (0 normal).
int ewdx_boot_exec(void);

// Tear down the VM (safe to call without startup).
void ewdx_boot_bye(void);

// Main-loop tick helper: pump input (the VM thread blocks inside exec;
// DGREDRAW/DIGETJOYSTATE poll input on the same thread).
int ewdx_boot_tick(void);

#ifdef __cplusplus
}
#endif

#endif
