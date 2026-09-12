// ewdx_extcmd.h - STEP-F minimal EXTCMD/EXTSYSVAR shims for boot.
//
// The game script mixes Windows HSP3 builtins (screen/title/dialog/mes/...)
// with the hmm DG* DLL surface. The DG* surface lives in ewdx_register.cpp
// (TYPE_DLLFUNC); THIS module covers the small EXTCMD/EXTSYSVAR subset the
// script needs before/around it, so boot reaches DGINIT:
//
//   EXTCMD screen/title/cls/dialog/mouse/getkey/stick/mes/pos/font/redraw
//   EXTSYSVAR ginfo/sysinfo/dirinfo
//
// Graphics shims delegate to the ewdx GLES layer where there is one
// (screen -> ewdx_screen, title -> SDL title, cls -> clear); UI text
// (mes/pos/font) logs for now (menus render via DGDRAWTEXT in-game).
// Input shims (getkey/stick/mouse) report neutral state; the real game
// input flows via DIGETJOYSTATE -> ewdx_input (see ewdx_input.h).
//
// Wiring (part 2): boot calls ewdx_extcmd_register(ctx) after the stock
// hsp3typeinit_extcmd/extfunc, overriding cmdfunc/reffunc like ewdx_register
// does for DLLFUNC. Until then this TU compiles standalone (headers only).
#ifndef __EWDX_EXTCMD_H
#define __EWDX_EXTCMD_H

#include "hsp3struct.h"

#ifdef __cplusplus
extern "C" {
#endif

// Install minimal EXTCMD + EXTSYSVAR handlers (see .cpp for opcode map).
// Call after stock hsp3typeinit_extcmd/extfunc; overrides cmdfunc/reffunc.
void ewdx_extcmd_register(HSP3TYPEINFO *info_extcmd, HSP3TYPEINFO *info_extsysvar);

#ifdef __cplusplus
}
#endif

#endif
