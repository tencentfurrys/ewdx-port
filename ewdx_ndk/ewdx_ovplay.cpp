// ewdx_ovplay.cpp - ovplay EXTCMD hook (see header for registration model).
//
// Command shapes from start_ax_dump.hsp (ov_load L158, bgmload L3892,
// set_volume L3884, bgmfeedout L3969):
//   cmd_0_0          () ........................ init -> audio_init
//   cmd_0_1(buf,a,b) (var,int,int) ............ decode OGG from HSP buffer
//   cmd_0_3(a,b)     (int,int) ................ play once
//   cmd_0_5(a,b,L,E,c) (int,int,[double],[double],int) loop play; L/E are
//                      double script vars truncated by code_getdi exactly like
//                      the original HspFunc_prm_getdi (626660-style truncation
//                      is faithful, not a bug)
//   cmd_0_6(a)       (int) .................... stop
//   cmd_0_7(a,vol)   (int,[double]) ........... volume in millibels
//   cmd_0_11(a)      (int) .................... status -> stat
//   cmd_0_254(a,buf) (int,var) ................ ov_save encode stub (dead:
//                      ov_save has no callers in the dump)
#include "ewdx_ovplay.h"
#include "ewdx_audio.h"
#include "ewdx_log.h"

#include "hsp3code.h"
#include "hspvar_core.h"

#include <string.h>

static HSPCTX *ov_ctx = NULL;

static int ov_stat(int v) {
    if (ov_ctx != NULL) ov_ctx->stat = v;
    return RUNMODE_RUN;
}

static int ewdx_ovplay_cmdfunc(int cmd) {
    code_next();  // mandatory advance, EXTCMD convention
    switch (cmd) {
    case 0: {  // init (DirectSound probe on Windows; always present here)
        return ov_stat(ewdx_audio_init());
    }
    case 1: {  // decode OGG from HSP memory buffer
        PVal *pv;
        APTR ap = code_getva(&pv);
        int cap = 0, rc;
        const void *ptr;
        code_getdi(0); code_getdi(0);  // start offset + chunk hint (98304)
        ptr = HspVarCorePtrAPTR(pv, ap);
        HspVarCoreGetBlockSize(pv, (PDAT *)ptr, &cap);
        if (ptr == NULL || cap <= 0) return ov_stat(0);
        rc = ewdx_bgm_open_mem(ptr, cap);
        if (rc == 0) EWDX_LOGE("ovplay: cannot decode BGM (%d bytes)", cap);
        return ov_stat(rc);
    }
    case 3: {  // play once (preview branch)
        code_getdi(0); code_getdi(0);
        return ov_stat(ewdx_bgm_play_once());
    }
    case 5: {  // loop play (0,0,looptime,loopstart,-1)
        long L, E;
        code_getdi(0); code_getdi(0);
        L = (long)code_getdi(0);
        E = (long)code_getdi(0);
        code_getdi(0);
        return ov_stat(ewdx_bgm_play(L, E));
    }
    case 6: {  // stop
        code_getdi(0);
        return ov_stat(ewdx_bgm_stop());
    }
    case 7: {  // volume in millibels (double expr, truncated like original)
        code_getdi(0);
        {
            int vol = code_getdi(-10000);
            return ov_stat(ewdx_bgm_vol(vol));
        }
    }
    case 11: {  // status -> stat
        code_getdi(0);
        return ov_stat(ewdx_bgm_status());
    }
    case 254: {  // ov_save encode stub (no callers; keep buffer untouched)
        PVal *pv;
        code_getdi(0);
        (void)code_getva(&pv);
        (void)pv;
        return ov_stat(0);
    }
    default:
        break;
    }
    throw(HSPERR_UNSUPPORTED_FUNCTION);
    return RUNMODE_RUN;
}

void ewdx_ovplay_register(HSPCTX *ctx) {
    HSPHED *hed;
    HPIDAT *org;
    int n, i;
    ov_ctx = ctx;
    if (ctx == NULL || ctx->hsphed == NULL) return;
    hed = ctx->hsphed;
    if (hed->pt_hpidat <= 0 || hed->max_hpi <= 0) return;
    org = (HPIDAT *)((char *)hed + hed->pt_hpidat);
    n = (int)(hed->max_hpi / (int)sizeof(HPIDAT));
    for (i = 0; i < n; i++) {
        const char *lib;
        if (org[i].flag != HPIDAT_FLAG_TYPEFUNC) continue;
        if (org[i].libname < 0 || org[i].libname >= hed->max_ds) continue;
        lib = &ctx->mem_mds[org[i].libname];
        if (strcmp(lib, "ovplay.dll") != 0) continue;
        {
            // mirror Hsp3ExtAddPlugin: fresh typeinfo + our cmdfunc + enable.
            // Single plugin in HPIDAT order -> same index as Windows.
            HSP3TYPEINFO *info = code_gettypeinfo(-1);
            info->cmdfunc = ewdx_ovplay_cmdfunc;
            code_enable_typeinfo(info);
        }
        return;  // one ovplay entry per image
    }
    // no ovplay entry: leave stock defaults (clean HSPERR on use)
}
