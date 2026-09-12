// ewdx_register.h - b2 HSP command registration for the EchidnaWarsDX port.
//
// Installs name-dispatched TYPE_DLLFUNC handlers over the ~50-entry engine
// surface verified from artifacts/start_ax_dump.bin (finfo 0..61):
//   hmm.dll    DIINIT/DIEND/DIGETJOYNUM/DIGETJOYSTATE DGINIT/DGEND/DGSCREEN
//              DGCOLOR/DGCLEAR/DGREDRAW/DGGSEL/DGBUFFER/DGLOADMEMORY DGPOS/
//              DGRECT/DGSCALEANDANGLE/DGBLENDMODE/DGGCOPY DGFONT/DGDRAWTEXT
//              DGTEXTURE/DGADDPRIMITIVE/DGDRAWPRIMITIVE/DGCREATEPRIMITIVE/DGLINE
//   hspda.dll  sortval/sortget vsave_start/put/end vload_start/get/end
//   hspogg.dll dmmini/dmmbye dmmvol/dmmpan/dmmload/dmmplay/dmmstop
//   system     GetUserDefaultLCID timeBeginPeriod/timeEndPeriod/timeGetTime
//              Sleep WritePrivateProfileStringA/GetPrivateProfileStringA
//              joyGetPosEx/joyGetNumDevs/joyGetDevCapsA
//
// Dispatch is by DLL export NAME (e.g. "_DGINIT@16"), never by finfo index,
// so recompiled scripts keep working. Params are pulled with code_getdi()/
// code_gets()/code_getva()/code_getsptr() exactly per the minfo decls in the
// dump (pexinfo/nullptr/bmscr params consume no script args; the DLL-side
// pulls for DGSCREEN/dmm*/vload/vsave/sort are reproduced from call sites +
// Hspda.cpp). No libffi, no stdcall @16: clean on ARM64.
//
// Stat contract: ctx->stat receives the value the original returned, per group:
//   DG*/DI* graph+input .... -1 ok / 0 fail (script tests stat==0)
//   DIGETJOYNUM ............. 1 (virtual stick; DIGETJOYSTATE fills joyg mask)
//   sort/vload/vsave ........ hspda parity (0 ok, <0 fail; vload_get no-op=0)
//   dmm* .................... -1 ok / 0 fail (ewdx_audio SE bank + mixer)
//   GetUserDefaultLCID ...... 1033 (en-US -> englishmode=1)
//   timeBegin/EndPeriod ..... 0 (TIMERR_NOERROR); Sleep leaves stat untouched
//   WriteProfileString ...... nonzero ok / 0 fail; GetProfileString = chars copied
//   joyGetPosEx/DevCaps ..... 167 JOYERR_UNPLUGGED (no-stick branches)
//   joyGetNumDevs ........... 0 ; timeGetTime = monotonic ms (reffunc, INT)
// ovplay cmd_0_* (EXTCMD, own typeinfo via ewdx_ovplay_register): bgm api,
//   -1 ok / 0 fail; cmd_0_11 status 1/0/<0; cmd_0_254 stub stat 0 (dead path).
#ifndef __EWDX_REGISTER_H
#define __EWDX_REGISTER_H

// Resolved via the OpenHSP hsp3 include dir (same as every OpenHSP TU),
// so this header works both here and merged into the OpenHSP tree.
#include "hsp3struct.h"

#ifdef __cplusplus
extern "C" {
#endif

// Captures info->hspctx/hspexinfo and overrides info->cmdfunc + info->reffunc.
// Call AFTER the stock hsp3typeinit_dllcmd body (keeps its termfunc).
// hsp3ext_ndk.cpp wires it so hsp3eb_execstart() picks it up for TYPE_DLLFUNC.
void ewdx_register(HSP3TYPEINFO *info);

#ifdef __cplusplus
}
#endif

#endif
