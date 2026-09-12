// ewdx_ovplay.h - ovplay.dll EXTCMD hook (bgmload/set_volume command loop).
//
// The game registers ovplay.dll via #regcmd "_hsp3cmdinit@4" (the single
// HPIDAT entry, flag TYPEFUNC) with 8 #cmd commands (0,1,3,5,6,7,11,254).
// The NDK runtime has no DLL loader, so this module replicates the stock
// Hsp3ExtAddPlugin TYPEFUNC path (OpenHSP hsp3extlib_ffi.cpp): scan the AX
// HPIDAT, allocate a typeinfo with code_gettypeinfo(-1), install our cmdfunc,
// activate with code_enable_typeinfo(). Single plugin -> deterministic index,
// identical to Windows. Called once from ewdx_register(); silently skips when
// the image carries no ovplay entry.
#ifndef __EWDX_OVPLAY_H
#define __EWDX_OVPLAY_H

#include "hsp3struct.h"

void ewdx_ovplay_register(HSPCTX *ctx);

#endif
