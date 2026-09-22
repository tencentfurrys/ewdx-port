// ewdx_register.cpp - b2 TYPE_DLLFUNC registration (name-dispatched).
//
// Provenance per entry: artifacts/start_ax_dump.bin finfo/minfo (240 finfo
// entries; libs 1=hmm.dll 2=hspda.dll 3=hspogg.dll 4=winmm 0/5=kernel32),
// call shapes mined from artifacts/start_ax_dump.hsp, hspda pulls from
// OpenHSP src/plugins/win32/hspda/Hspda.cpp, sort semantics mirrored from
// OpenHSP src/hsp3/hsp3int.cpp (0x2D/0x30). Wiring: hsp3ext_ndk.cpp calls
// ewdx_register(info) at the end of hsp3typeinit_dllcmd(), so the stock
// hsp3eb_execstart() init path (hsp3embed.cpp) picks it up for TYPE_DLLFUNC.
//
// Stat contract: ctx->stat gets the original's return value (see header).
// Deferred work is marked STEP-D (audio/input/text/live-var restore).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "hsp3code.h"
#include "hspvar_core.h"

#include "ewdx_gles.h"
#include "ewdx_batch.h"
#include "ewdx_dg.h"
#include "ewdx_hspv.h"
#include "ewdx_audio.h"
#include "ewdx_input.h"
#include "ewdx_ovplay.h"
#include "ewdx_paths.h"
#include "ewdx_log.h"
#include "ewdx_boot.h"
#include "ewdx_register.h"

static HSPCTX *rc_ctx = NULL;
static HSPEXINFO *rc_exinfo = NULL;

// ---------------------------------------------------------------- edict ---

// Internal command ids. Assigned by NAME lookup, never by finfo index.
typedef enum {
    EWDX_CMD_UNKNOWN = 0,
    // hmm graph (full impl via ewdx layer)
    EWDX_DGINIT, EWDX_DGSCREEN, EWDX_DGBUFFER, EWDX_DGGSEL, EWDX_DGCOLOR,
    EWDX_DGCLEAR, EWDX_DGREDRAW, EWDX_DGBLENDMODE, EWDX_DGEND, EWDX_DGPOS,
    EWDX_DGRECT, EWDX_DGSCALE, EWDX_DGCOPY, EWDX_DGTEXTURE, EWDX_DGLOADMEM,
    EWDX_DGCREATEPRIM, EWDX_DGADDPRIM, EWDX_DGDRAWPRIM, EWDX_DGFONT,
    EWDX_DGDRAWTEXT, EWDX_DGLINE,
    // hmm input (neutral-device stubs; STEP-D: AInputQueue)
    EWDX_DIINIT, EWDX_DIEND, EWDX_DIJOYNUM, EWDX_DIJOYSTATE,
    // hspda (sort: full impl; vload/vsave: lifecycle + stubs, STEP-D restore)
    EWDX_SORTVAL, EWDX_SORTGET,
    EWDX_VSAVESTART, EWDX_VSAVEPUT, EWDX_VSAVEEND,
    EWDX_VLOADSTART, EWDX_VLOADGET, EWDX_VLOADEND,
    // hspogg dmm (SE bank + mixer in ewdx_audio.cpp)
    EWDX_DMMINI, EWDX_DMMBYE, EWDX_DMMVOL, EWDX_DMMPAN,
    EWDX_DMMLOAD, EWDX_DMMPLAY, EWDX_DMMSTOP,
    // system shims (real impls, dependency-free)
    EWDX_LCID, EWDX_TBEGIN, EWDX_TEND, EWDX_SLEEP,
    EWDX_INIWRITE, EWDX_INIREAD,
    EWDX_JOYPOS, EWDX_JOYNUM, EWDX_JOYCAPS,
    // reffunc-only
    EWDX_TGETTIME
} EwdxCmd;

static EwdxCmd ewdx_lookup(const char *nm) {
    // DLL export names as stored in mem_mds (verified from the AX dump).
    if (nm[0] == '_') {
        if (strcmp(nm, "_DGINIT@16") == 0) return EWDX_DGINIT;
        if (strcmp(nm, "_DGSCREEN@16") == 0) return EWDX_DGSCREEN;
        if (strcmp(nm, "_DGBUFFER@16") == 0) return EWDX_DGBUFFER;
        if (strcmp(nm, "_DGGSEL@16") == 0) return EWDX_DGGSEL;
        if (strcmp(nm, "_DGCOLOR@16") == 0) return EWDX_DGCOLOR;
        if (strcmp(nm, "_DGCLEAR@16") == 0) return EWDX_DGCLEAR;
        if (strcmp(nm, "_DGREDRAW@16") == 0) return EWDX_DGREDRAW;
        if (strcmp(nm, "_DGBLENDMODE@16") == 0) return EWDX_DGBLENDMODE;
        if (strcmp(nm, "_DGEND@16") == 0) return EWDX_DGEND;
        if (strcmp(nm, "_DGPOS@16") == 0) return EWDX_DGPOS;
        if (strcmp(nm, "_DGRECT@16") == 0) return EWDX_DGRECT;
        if (strcmp(nm, "_DGSCALEANDANGLE@16") == 0) return EWDX_DGSCALE;
        if (strcmp(nm, "_DGCOPY@16") == 0) return EWDX_DGCOPY;   // unused-by-game alias kept for parity
        if (strcmp(nm, "_DGGCOPY@16") == 0) return EWDX_DGCOPY;  // real import (start.ax #func; missed -> #Error 21 at title)
        if (strcmp(nm, "_DGTEXTURE@16") == 0) return EWDX_DGTEXTURE;
        if (strcmp(nm, "_DGLOADMEMORY@16") == 0) return EWDX_DGLOADMEM;
        if (strcmp(nm, "_DGCREATEPRIMITIVE@16") == 0) return EWDX_DGCREATEPRIM;
        if (strcmp(nm, "_DGADDPRIMITIVE@16") == 0) return EWDX_DGADDPRIM;
        if (strcmp(nm, "_DGDRAWPRIMITIVE@16") == 0) return EWDX_DGDRAWPRIM;
        if (strcmp(nm, "_DGFONT@16") == 0) return EWDX_DGFONT;
        if (strcmp(nm, "_DGDRAWTEXT@16") == 0) return EWDX_DGDRAWTEXT;
        if (strcmp(nm, "_DGLINE@16") == 0) return EWDX_DGLINE;
        if (strcmp(nm, "_DIINIT@16") == 0) return EWDX_DIINIT;
        if (strcmp(nm, "_DIEND@16") == 0) return EWDX_DIEND;
        if (strcmp(nm, "_DIGETJOYNUM@16") == 0) return EWDX_DIJOYNUM;
        if (strcmp(nm, "_DIGETJOYSTATE@16") == 0) return EWDX_DIJOYSTATE;
        if (strcmp(nm, "_sortval@16") == 0) return EWDX_SORTVAL;
        if (strcmp(nm, "_sortget@16") == 0) return EWDX_SORTGET;
        if (strcmp(nm, "_vsave_start@16") == 0) return EWDX_VSAVESTART;
        if (strcmp(nm, "_vsave_put@16") == 0) return EWDX_VSAVEPUT;
        if (strcmp(nm, "_vsave_end@16") == 0) return EWDX_VSAVEEND;
        if (strcmp(nm, "_vload_start@16") == 0) return EWDX_VLOADSTART;
        if (strcmp(nm, "_vload_get@16") == 0) return EWDX_VLOADGET;
        if (strcmp(nm, "_vload_end@16") == 0) return EWDX_VLOADEND;
        if (strcmp(nm, "_dmmini@16") == 0) return EWDX_DMMINI;
        if (strcmp(nm, "_dmmbye@16") == 0) return EWDX_DMMBYE;
        if (strcmp(nm, "_dmmvol@16") == 0) return EWDX_DMMVOL;
        if (strcmp(nm, "_dmmpan@16") == 0) return EWDX_DMMPAN;
        if (strcmp(nm, "_dmmload@16") == 0) return EWDX_DMMLOAD;
        if (strcmp(nm, "_dmmplay@16") == 0) return EWDX_DMMPLAY;
        if (strcmp(nm, "_dmmstop@16") == 0) return EWDX_DMMSTOP;
        return EWDX_CMD_UNKNOWN;
    }
    if (strcmp(nm, "GetUserDefaultLCID") == 0) return EWDX_LCID;
    if (strcmp(nm, "timeBeginPeriod") == 0) return EWDX_TBEGIN;
    if (strcmp(nm, "timeEndPeriod") == 0) return EWDX_TEND;
    if (strcmp(nm, "timeGetTime") == 0) return EWDX_TGETTIME;
    if (strcmp(nm, "Sleep") == 0) return EWDX_SLEEP;
    if (strcmp(nm, "WritePrivateProfileStringA") == 0) return EWDX_INIWRITE;
    if (strcmp(nm, "GetPrivateProfileStringA") == 0) return EWDX_INIREAD;
    if (strcmp(nm, "joyGetPosEx") == 0) return EWDX_JOYPOS;
    if (strcmp(nm, "joyGetNumDevs") == 0) return EWDX_JOYNUM;
    if (strcmp(nm, "joyGetDevCapsA") == 0) return EWDX_JOYCAPS;
    return EWDX_CMD_UNKNOWN;
}

// ------------------------------------------------------- small helpers ---

static int rc_stat(int v) { rc_ctx->stat = v; return RUNMODE_RUN; }

// Remaining writable bytes of a var element (for clamping copies/fills).
static int rc_blockbytes(PVal *pv, APTR ap) {
    int n = 0;
    PDAT *p = HspVarCorePtrAPTR(pv, ap);
    HspVarCoreGetBlockSize(pv, p, &n);
    return (n < 0) ? 0 : n;
}

// Tolerant string fetch for sptr (FLEXSPTR) params: int/missing -> "".
static const char *rc_flexstr(int *is_str) {
    int t = HSPVAR_FLAG_INT;
    char *p = code_getsptr(&t);
    if (t == HSPVAR_FLAG_STR && p != NULL) {
        if (is_str != NULL) *is_str = 1;
        return p;
    }
    if (is_str != NULL) *is_str = 0;
    return "";
}

// ------------------------------------------------------------- INI db ---

// Minimal dependency-free INI upsert/read (Win32 profile-API parity for the
// game's settings + joystick cfg files). Bounded; preserves other lines.
// Section/key match is case-sensitive (Win32 is not); the game uses
// consistent case on both sides, so this is behavior-preserving here.

static char *rc_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    *e = '\0';
    return s;
}

#define EWDX_INI_MAX (65536)

static int rc_ini_read(const char *file, const char *app, const char *key,
                       char *out, int outsize) {
    FILE *fp;
    static char buf[EWDX_INI_MAX + 2];
    size_t n;
    int insec = 0;
    size_t kl;
    char rpath[1024];
    if (outsize <= 0) return 0;
    out[0] = '\0';
    if (file == NULL || file[0] == '\0') return 0;
    ewdx_resolve(file, rpath, sizeof(rpath));  // <filesDir>/*.ini on device
    file = rpath;
    fp = fopen(file, "rb");
    if (fp == NULL) return 0;
    n = fread(buf, 1, EWDX_INI_MAX, fp);
    fclose(fp);
    buf[n] = '\0';
    kl = strlen(key);
    for (char *line = buf; line != NULL;) {
        char *nl = strchr(line, '\n');
        char *t;
        if (nl != NULL) *nl = '\0';
        t = rc_trim(line);
        if (t[0] == '[') {
            char *ce = strchr(t, ']');
            if (ce != NULL) {
                *ce = '\0';
                insec = (strcmp(rc_trim(t + 1), app) == 0);
            }
        } else if (insec && strncmp(t, key, kl) == 0) {
            char *eq = t + kl;
            eq = rc_trim(eq);
            if (*eq == '=') {
                char *v = rc_trim(eq + 1);
                size_t vl = strlen(v);
                if (vl >= (size_t)outsize) vl = (size_t)(outsize - 1);
                memcpy(out, v, vl);
                out[vl] = '\0';
                return (int)vl;  // Win32 returns chars copied (excl. NUL)
            }
        }
        line = (nl == NULL) ? NULL : (nl + 1);
    }
    return 0;  // not found -> caller falls back to default (out stays "")
}

static int rc_ini_write(const char *file, const char *app, const char *key,
                        const char *val) {
    FILE *fp;
    static char buf[EWDX_INI_MAX + 2];
    static char out[EWDX_INI_MAX + 512];
    size_t n = 0;
    int done = 0, insec = 0, havsec = 0;
    size_t o = 0;
    size_t kl, need;
    char rpath[1024];
    if (file == NULL || file[0] == '\0' || app == NULL || key == NULL || val == NULL) return 0;
    ewdx_resolve(file, rpath, sizeof(rpath));  // <filesDir>/*.ini on device
    file = rpath;
    fp = fopen(file, "rb");
    if (fp != NULL) {
        n = fread(buf, 1, EWDX_INI_MAX, fp);
        fclose(fp);
        buf[n] = '\0';
    } else {
        buf[0] = '\0';
    }
    kl = strlen(key);
    for (char *line = buf;;) {
        char *nl = strchr(line, '\n');
        int last = (nl == NULL);
        char tmp[1024];
        size_t ll;
        char *t;
        if (last && line[0] == '\0') break;  // ignore trailing empty segment
        if (!last) *nl = '\0';
        ll = strlen(line);
        if (ll >= sizeof(tmp)) ll = sizeof(tmp) - 1;
        memcpy(tmp, line, ll);
        tmp[ll] = '\0';
        t = rc_trim(tmp);
        if (t[0] == '[') {
            if (insec && !done) {
                // key absent in its section: append before section ends
                need = kl + strlen(val) + 4;
                if (o + need >= sizeof(out)) return 0;
                o += (size_t)snprintf(out + o, sizeof(out) - o, "%s=%s\n", key, val);
                done = 1;
            }
            {
                char *ce = strchr(t, ']');
                if (ce != NULL) {
                    *ce = '\0';
                    insec = (strcmp(rc_trim(t + 1), app) == 0);
                    if (insec) havsec = 1;
                } else {
                    insec = 0;
                }
            }
        } else if (insec && !done && strncmp(t, key, kl) == 0) {
            char *eq = rc_trim(t + kl);
            if (*eq == '=') {
                // replace this line
                need = kl + strlen(val) + 4;
                if (o + need >= sizeof(out)) return 0;
                o += (size_t)snprintf(out + o, sizeof(out) - o, "%s=%s\n", key, val);
                done = 1;
                line = last ? NULL : (nl + 1);
                if (line != NULL && *line == '\0' && last) break;
                if (last) break;
                continue;
            }
        }
        {
            size_t wl = strlen(line);
            if (o + wl + 2 >= sizeof(out)) return 0;
            memcpy(out + o, line, wl);
            o += wl;
            out[o++] = '\n';
        }
        if (last) break;
        line = nl + 1;
    }
    if (!done) {
        if (!havsec) {
            need = strlen(app) + 4;
            if (o + need >= sizeof(out)) return 0;
            o += (size_t)snprintf(out + o, sizeof(out) - o, "[%s]\n", app);
        }
        need = kl + strlen(val) + 4;
        if (o + need >= sizeof(out)) return 0;
        o += (size_t)snprintf(out + o, sizeof(out) - o, "%s=%s\n", key, val);
    }
    fp = fopen(file, "wb");
    if (fp == NULL) return 0;
    n = fwrite(out, 1, o, fp);
    fclose(fp);
    return (n == o) ? 1 : 0;
}

// ------------------------------------------------------- sort mirror ---
// Mirrors OpenHSP hsp3int.cpp 0x2D sortval / 0x30 sortget (int + double).

typedef struct { double dkey; int ikey; int info; int isdbl; } EwdxSortEnt;
static std::vector<EwdxSortEnt> rc_sortidx;

static bool rc_sort_lt_asc(const EwdxSortEnt &a, const EwdxSortEnt &b) {
    if (a.isdbl || b.isdbl) {
        if (a.dkey != b.dkey) return a.dkey < b.dkey;
        return a.info < b.info;
    }
    if (a.ikey != b.ikey) return a.ikey < b.ikey;
    return a.info < b.info;
}
static bool rc_sort_lt_desc(const EwdxSortEnt &a, const EwdxSortEnt &b) {
    if (a.isdbl || b.isdbl) {
        if (a.dkey != b.dkey) return a.dkey > b.dkey;
        return a.info < b.info;
    }
    if (a.ikey != b.ikey) return a.ikey > b.ikey;
    return a.info < b.info;
}

// ------------------------------------------------- vload/vsave session ---
// Live restore/extract keyed by var NAME (Hspda.cpp parity: varload_get
// matches hei->HspFunc_varname(varid) against the hspv entry names).
// File layout is the 32-bit x86 hspv image (see ewdx_hspv.h); host PVal may
// be 64-bit, so payloads are converted, never struct-copied. Covers
// INT/DOUBLE (raw storage) + STR (per-element [FX,size,bytes]); LABEL and
// STRUCT restore as no-op success (not used by save.dat/map/mot flows).

typedef struct { const unsigned char *img; int len; int active; } EwdxVLoad;
static EwdxVLoad rc_vload;

typedef struct { int active; } EwdxVSave;
static EwdxVSave rc_vsave;
static char rc_vsave_path[1024];  // resolved <filesDir>/... target for STEP-F
static std::vector<int> rc_vsave_ids;  // varids from vsave_put (drained at end)

// Find an hspv entry by var name in the open vload image.
static const EwdxHspvEntry *rc_vload_find(const char *name) {
    const EwdxHspvEntry *ents = NULL;
    int n, i;
    if (!rc_vload.active || rc_vload.img == NULL || name == NULL) return NULL;
    n = ewdx_hspv_vars(rc_vload.img, rc_vload.len, &ents);
    if (n <= 0 || ents == NULL) return NULL;
    for (i = 0; i < n; i++) {
        const char *en = ewdx_hspv_name(rc_vload.img, &ents[i]);
        if (en != NULL && strcmp(en, name) == 0) return &ents[i];
    }
    return NULL;
}

// Restore one live var from its hspv entry. Returns 0 ok / <0 fail.
static int rc_vload_restore(PVal *pv, int varid) {
    char *name;
    const EwdxHspvEntry *e;
    const uint8_t *payload;
    int flag;
    if (pv == NULL || varid < 0) return -1;
    name = code_getdebug_varname(varid);
    if (name == NULL || name[0] == '\0') return -1;
    e = rc_vload_find(name);
    if (e == NULL) {
        EWDX_LOGW("vload: no entry for '%s' (keeps live value)", name);
        return 0;  // missing entry is not fatal (fresh save slots)
    }
    payload = ewdx_hspv_data(rc_vload.img, e);
    if (payload == NULL) return -1;
    flag = (int)e->master.flag;
    if (flag != HSPVAR_FLAG_INT && flag != HSPVAR_FLAG_DOUBLE &&
        flag != HSPVAR_FLAG_STR) {
        EWDX_LOGW("vload: '%s' type %d unsupported (keeps live)", name, flag);
        return 0;
    }
    // Hspda.cpp varload_getvar parity: struct-copy the MASTER PVal (true
    // dims incl. zero trailing lens + support bits), realloc, copy payload.
    // The old shim rebuilt 1-D Dim(v,flag,len1,1,1,1), which flattened
    // multi-dim shapes (m_obj 17x3, t_part 24x59x77, ...) -> truncated INT
    // payloads and OOB STR row reads (Error 7 at stage load).
    {
        PVal master;
        memset(&master, 0, sizeof(master));
        master.flag = (short)flag;
        master.mode = (short)e->master.mode;
        master.len[0] = 1;
        master.len[1] = e->master.len[1];
        master.len[2] = e->master.len[2];
        master.len[3] = e->master.len[3];
        master.len[4] = e->master.len[4];
        master.size = (int)e->master.size;
        master.support = e->master.support;
        EWDX_LOGW("vload: rest '%s' flag=%d len=[%u,%u,%u,%u] size=%u sup=%#x",
                  name, flag, master.len[1], master.len[2], master.len[3],
                  master.len[4], master.size, master.support);
        // File-end bound for payload walks (truncated file guard).
        const uint8_t *fend = rc_vload.img + rc_vload.len;
        if (payload + e->master.size > fend) return -1;
        if (flag == HSPVAR_FLAG_STR) {
            // STR blocks: master.size is the POINTER-TABLE size (elems*4),
            // NOT the payload bytes; the payload is `size/4` self-describing
            // blocks [u32 FX][u32 len][bytes]. v15 bounded the walk by
            // master.size and read the NEXT entry's data -> tag mismatch ->
            // the var was left re-dimmed to a bogus shape (device ERR7
            // core:range on m_dio0_w, v15 logs). So: validate the whole walk
            // against the file end FIRST, then dim to the master's true dims
            // (zeros preserved; strict-path reads need the exact shape, e.g.
            // m_dio0_w [1,64]) and only then fill.
            uint32_t blocks = master.size / 4u;
            const uint8_t *p = payload;
            const uint8_t *q;
            uint32_t k, tag, bsz;
            for (k = 0, q = p; k < blocks; k++) {
                if (q + 8 > fend) return -1;
                memcpy(&tag, q, 4);
                if (tag != EWDX_HSPV_FX) return -1;
                memcpy(&bsz, q + 4, 4);
                q += 8 + bsz;
                if (q > fend) return -1;
            }
            HspVarCoreClear(pv, HSPVAR_FLAG_STR);
            HspVarCoreDim(pv, HSPVAR_FLAG_STR,
                          (int)master.len[1], (int)master.len[2],
                          (int)master.len[3], (int)master.len[4]);
            for (k = 0, q = p; k < blocks; k++) {
                memcpy(&bsz, q + 4, 4);
                q += 8;
                char *tmp = (char *)malloc((size_t)bsz + 1u);
                if (tmp == NULL) return -1;
                memcpy(tmp, q, bsz);
                tmp[bsz] = '\0';
                code_setva(pv, (APTR)k, HSPVAR_FLAG_STR, tmp);
                free(tmp);
                q += bsz;
            }
            return 0;
        }
        // INT/DOUBLE: fixed storage. Keep dims verbatim (0s stay 0s so the
        // VM's FLEXARRAY auto-expand keeps working on later writes), then
        // memcpy the whole stored payload.
        {
            int esz = (flag == HSPVAR_FLAG_INT) ? 4 : 8;
            uint32_t want = (uint32_t)(esz * (int)HspVarCoreCountElems(&master));
            if ((uint32_t)e->master.size < want) return -1;
            HspVarCoreClear(pv, flag);
            HspVarCoreDim(pv, flag,
                          (int)master.len[1], (int)master.len[2],
                          (int)master.len[3], (int)master.len[4]);
            void *dst = HspVarCorePtrAPTR(pv, 0);
            int cap = 0;
            HspVarCoreGetBlockSize(pv, (PDAT *)dst, &cap);
            if (cap < (int)want || dst == NULL) return -1;
            memcpy(dst, payload, want);
            return 0;
        }
    }
}

// Serialize the accumulated vsave_put varids to an hspv file (32-bit layout
// so Windows + Android images stay interchangeable). Returns 0 ok / <0 fail.
static int rc_vsave_write(const char *path) {
    FILE *fp;
    uint32_t n, i;
    uint32_t pt_data;
    // First pass: gather per-var blobs to size the data block.
    typedef struct { const char *name; int varid; uint32_t nsize; uint32_t dsize; } VSaveEnt;
    std::vector<VSaveEnt> list;
    if (path == NULL || path[0] == '\0') return -1;
    if (rc_ctx == NULL) return -1;
    for (i = 0; i < (uint32_t)rc_vsave_ids.size(); i++) {
        int varid = rc_vsave_ids[i];
        PVal *pv;
        char *name;
        VSaveEnt ve;
        if (varid < 0 || varid >= rc_ctx->hsphed->max_val) continue;
        pv = &rc_ctx->mem_var[varid];
        name = code_getdebug_varname(varid);
        if (name == NULL || name[0] == '\0') continue;
        if (pv->flag != HSPVAR_FLAG_INT && pv->flag != HSPVAR_FLAG_DOUBLE &&
            pv->flag != HSPVAR_FLAG_STR)
            continue;  // LABEL/STRUCT not used by saves; skip loudly below
        ve.name = name;
        ve.varid = varid;
        ve.nsize = (uint32_t)(strlen(name) + 1);
        if (pv->flag == HSPVAR_FLAG_STR) {
            int ne = HspVarCoreCountElems(pv);
            int k;
            uint32_t ds = 0;
            if (ne <= 0) ne = 1;
            for (k = 0; k < ne; k++) {
                PDAT *pd = HspVarCorePtrAPTR(pv, (APTR)k);
                int bsz = 0;
                HspVarCoreGetBlockSize(pv, pd, &bsz);
                if (bsz < 0) bsz = 0;
                ds += 8u + (uint32_t)bsz;
            }
            ve.dsize = ds;
        } else {
            int cap = 0;
            void *ptr = HspVarCorePtrAPTR(pv, 0);
            HspVarCoreGetBlockSize(pv, (PDAT *)ptr, &cap);
            if (cap < 0) cap = 0;
            ve.dsize = (uint32_t)cap;
        }
        list.push_back(ve);
    }
    n = (uint32_t)list.size();
    pt_data = 16u + n * 64u;
    fp = fopen(path, "wb");
    if (fp == NULL) {
        EWDX_LOGE("vsave: cannot write '%s'", path);
        return -1;
    }
    {
        // header
        uint32_t magic = EWDX_HSPV_MAGIC, ver = EWDX_HSPV_VER;
        fwrite(&magic, 4, 1, fp);
        fwrite(&ver, 4, 1, fp);
        fwrite(&n, 4, 1, fp);
        fwrite(&pt_data, 4, 1, fp);
    }
    {
        // entries + data block assembled in memory first
        std::vector<uint8_t> names;
        std::vector<uint8_t> payloads;
        uint32_t j;
        for (j = 0; j < n; j++) names.insert(names.end(), list[j].nsize, 0);
        {
            uint32_t at = 0;
            for (j = 0; j < n; j++) {
                memcpy(&names[at], list[j].name, list[j].nsize);
                at += list[j].nsize;
            }
        }
        for (j = 0; j < n; j++) {
            PVal *pv = &rc_ctx->mem_var[list[j].varid];
            if (pv->flag == HSPVAR_FLAG_STR) {
                int ne = HspVarCoreCountElems(pv);
                int k;
                if (ne <= 0) ne = 1;
                for (k = 0; k < ne; k++) {
                    PDAT *pd = HspVarCorePtrAPTR(pv, (APTR)k);
                    int bsz = 0;
                    void *blk = NULL;
                    uint32_t fx = EWDX_HSPV_FX;
                    uint32_t sz;
                    // fetch block bytes (GetBlockSize returns ptr+size)
                    blk = HspVarCoreGetBlockSize(pv, pd, &bsz);
                    if (bsz < 0) bsz = 0;
                    // NOTE: GetBlockSize returns the block ptr for STR and
                    // sets size; pd already points at the element.
                    {
                        uint8_t *bytes = (uint8_t *)HspVarCorePtrAPTR(pv, (APTR)k);
                        (void)blk;
                        sz = (uint32_t)bsz;
                        payloads.insert(payloads.end(), (uint8_t *)&fx, (uint8_t *)&fx + 4);
                        payloads.insert(payloads.end(), (uint8_t *)&sz, (uint8_t *)&sz + 4);
                        if (sz > 0) payloads.insert(payloads.end(), bytes, bytes + sz);
                    }
                }
            } else {
                int cap = 0;
                uint8_t *ptr = (uint8_t *)HspVarCorePtrAPTR(pv, 0);
                HspVarCoreGetBlockSize(pv, (PDAT *)ptr, &cap);
                if (cap < 0) cap = 0;
                if (cap > 0) payloads.insert(payloads.end(), ptr, ptr + cap);
            }
        }
        // emit entries with data-relative offsets
        {
            uint32_t noff = 0, poff = (uint32_t)names.size();
            for (j = 0; j < n; j++) {
                PVal *pv = &rc_ctx->mem_var[list[j].varid];
                uint32_t opt = 0, enc = 0;
                int16_t flag = (int16_t)pv->flag;
                int16_t mode = (int16_t)pv->mode;
                uint32_t len[5];
                uint32_t size;
                uint32_t pt = 0, master = 0;
                uint16_t support = pv->support;
                int16_t arraycnt = pv->arraycnt;
                uint32_t offset = 0, arraymul = 0;
                int q;
                uint32_t my_nsize = list[j].nsize;
                // payload size for THIS var = slice of payloads stream:
                // recompute deterministically (same order as above).
                uint32_t my_dsize;
                if (pv->flag == HSPVAR_FLAG_STR) {
                    int ne = HspVarCoreCountElems(pv);
                    int k;
                    my_dsize = 0;
                    if (ne <= 0) ne = 1;
                    for (k = 0; k < ne; k++) {
                        PDAT *pd = HspVarCorePtrAPTR(pv, (APTR)k);
                        int bsz = 0;
                        HspVarCoreGetBlockSize(pv, pd, &bsz);
                        if (bsz < 0) bsz = 0;
                        my_dsize += 8u + (uint32_t)bsz;
                    }
                } else {
                    int cap = 0;
                    void *ptr = HspVarCorePtrAPTR(pv, 0);
                    HspVarCoreGetBlockSize(pv, (PDAT *)ptr, &cap);
                    if (cap < 0) cap = 0;
                    my_dsize = (uint32_t)cap;
                }
                for (q = 0; q < 5; q++) len[q] = (uint32_t)((q < 5) ? pv->len[q] : 0);
                // STR storage: size field = payload bytes (flex stream)
                size = my_dsize;
                if (pv->flag != HSPVAR_FLAG_STR) {
                    // fixed storage: size = raw bytes
                    int cap = 0;
                    void *ptr = HspVarCorePtrAPTR(pv, 0);
                    HspVarCoreGetBlockSize(pv, (PDAT *)ptr, &cap);
                    if (cap < 0) cap = 0;
                    size = (uint32_t)cap;
                }
                fwrite(&noff, 4, 1, fp);
                fwrite(&poff, 4, 1, fp);
                fwrite(&opt, 4, 1, fp);
                fwrite(&enc, 4, 1, fp);
                fwrite(&flag, 2, 1, fp);
                fwrite(&mode, 2, 1, fp);
                fwrite(len, 4, 5, fp);
                fwrite(&size, 4, 1, fp);
                fwrite(&pt, 4, 1, fp);
                fwrite(&master, 4, 1, fp);
                fwrite(&support, 2, 1, fp);
                fwrite(&arraycnt, 2, 1, fp);
                fwrite(&offset, 4, 1, fp);
                fwrite(&arraymul, 4, 1, fp);
                noff += my_nsize;
                poff += my_dsize;
                (void)my_nsize;
            }
        }
        if (!names.empty()) fwrite(&names[0], 1, names.size(), fp);
        if (!payloads.empty()) fwrite(&payloads[0], 1, payloads.size(), fp);
    }
    fclose(fp);
    EWDX_LOGW("vsave: wrote %u vars -> '%s'", n, path);
    return 0;
}

// ------------------------------------------------------------ dmm bank ---
// Lifecycle only (dmmini/dmmbye). Voice mix/streaming is STEP-D (OpenSL ES
// bank mixer). Slot semantics observed: SE slots via dmmload(file,slot),
// dmmvol(slot,level) dmmlevel ~= -10000..0, dmmpan(slot,pan).

static int rc_dmm_ok = 0;

// ============================================================ handlers ===

static int rc_dg_4i(EwdxCmd id, int a, int b, int c, int d) {
    switch (id) {
    case EWDX_DGCOLOR: return dg_color(a, b, c, d);
    case EWDX_DGBUFFER: return dg_buffer(a, b, c);  // d reserved, always 0
    case EWDX_DGGSEL: return dg_select(a);
    case EWDX_DGBLENDMODE: return dg_blend(a);
    case EWDX_DGTEXTURE: return dg_texture(a);
    case EWDX_DGCREATEPRIM: return dg_createprim(a);
    case EWDX_DGPOS: return dg_pos(a, b);
    case EWDX_DGRECT: return dg_rect(a, b, c, d);
    case EWDX_DGSCALE: return dg_scale(a, b, c);  // d reserved, always 0
    case EWDX_DGLINE: {
#ifdef EWDX_DGLINE_JOURNAL
        // v21 diagnostics (mirror of EWDX_DGCOPY_JOURNAL): log every DGLINE
        // with endpoints + current color/blend so a stray-line repro can be
        // matched against the putline call sites (beam colors 190,255,120 /
        // 190,230,255 / 255,230,150 blend 2; boss tether 255,230,240 blend 3;
        // grab streaks 255,220,240 blend 1; cort overlay blend 1).
        int rc = dg_line(a, b, c, d);
        {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "[dgline] (%d,%d)-(%d,%d) col=(%d,%d,%d,%d) blend=%d",
                     a, b, c, d, ewdx.st.r, ewdx.st.g, ewdx.st.b, ewdx.st.a,
                     ewdx.st.blend);
            ewdx_boot_journal(msg);
        }
        return rc;
#else
        return dg_line(a, b, c, d);  // tail-call (byte-parity with v20 path)
#endif
    }
    default: break;
    }
    return 0;
}

// Boot-phase journal: first use of each DLL command goes to filesDir/boot.log
// (same journal the crash UI reads). Reproduces the per-new-call "dll: <name>"
// lines of the 2026-09-12 device build without flooding during normal play:
// names are stable DS pointers, so a pointer table deduplicates by name.
static void rc_journal_dll(const char *nm) {
    static const char *seen[128];
    static int seen_n = 0;
    char msg[160];
    int i;
    if (nm == NULL || nm[0] == '\0') return;
    for (i = 0; i < seen_n; i++) {
        if (seen[i] == nm) return;
    }
    if (seen_n < (int)(sizeof(seen) / sizeof(seen[0]))) seen[seen_n++] = nm;
    snprintf(msg, sizeof(msg), "dll: %s", nm);
    ewdx_boot_journal(msg);
}

static int rc_cmdfunc_dllcmd(int cmd) {
    HSPHED *hed;
    STRUCTDAT *st;
    const char *nm;
    int maxf;
    EwdxCmd id;

    if (rc_ctx == NULL) throw(HSPERR_ILLEGAL_FUNCTION);
    if (cmd < 0) throw(HSPERR_SYNTAX);
    hed = rc_ctx->hsphed;
    maxf = (hed == NULL) ? 0 : (int)(hed->max_finfo / (int)sizeof(STRUCTDAT));
    if (cmd >= maxf) throw(HSPERR_SYNTAX);
    st = &rc_ctx->mem_finfo[cmd];
    if (st->nameidx < 0 || (hed != NULL && st->nameidx >= hed->max_ds)) throw(HSPERR_SYNTAX);
    nm = &rc_ctx->mem_mds[st->nameidx];
    id = ewdx_lookup(nm);
    rc_journal_dll(nm);

    code_next();  // mandatory: advance past the command before pulling args

    switch (id) {
    // --- hmm graph: int x4 decls (trailing params default 0, dropped) ---
    case EWDX_DGCOLOR:
    case EWDX_DGBUFFER:
    case EWDX_DGGSEL:
    case EWDX_DGBLENDMODE:
    case EWDX_DGTEXTURE:
    case EWDX_DGCREATEPRIM:
    case EWDX_DGPOS:
    case EWDX_DGRECT:
    case EWDX_DGSCALE:
    case EWDX_DGLINE: {
        int a = code_getdi(0), b = code_getdi(0);
        int c = code_getdi(0), d = code_getdi(0);
        return rc_stat(rc_dg_4i(id, a, b, c, d));
    }
    case EWDX_DGCOPY: {  // (id, flags): title passes 1/2/8 (center, scale, flip)
        int a = code_getdi(0), b = code_getdi(0);
        code_getdi(0); code_getdi(0);
        return rc_stat(dg_copyf(a, b));
    }
    case EWDX_DGCLEAR: {
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        return rc_stat(dg_clear());
    }
    case EWDX_DGREDRAW: {
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        ewdx_input_poll();  // frame boundary: drain input for next frame
        return rc_stat(dg_redraw());
    }
    case EWDX_DGEND: {
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        return rc_stat(dg_end());
    }
    case EWDX_DGINIT: {  // (bmscr auto) + int x3, bare at call sites
        code_getdi(0); code_getdi(0); code_getdi(0);
        return rc_stat(dg_init());
    }
    case EWDX_DGSCREEN: {  // pexinfo: pulls w,h,mode,depth,flag (L487)
        int w = code_getdi(0), h = code_getdi(0), m = code_getdi(0);
        int dep = code_getdi(0); code_getdi(0);
        (void)dep;  // depth always 32; flag ignored (windowed on Android)
        return rc_stat(dg_screen(w, h, m, 32, 0));
    }
    case EWDX_DGLOADMEM: {  // (pvar,i,i,i): bload buf -> texture slot
        PVal *pv;
        APTR ap = code_getva(&pv);
        int size = code_getdi(0), slot = code_getdi(0);
        int cap, rc;
        const void *ptr;
        code_getdi(0);
        ptr = HspVarCorePtrAPTR(pv, ap);
        cap = rc_blockbytes(pv, ap);
        if (ptr == NULL || size <= 0 || (cap > 0 && size > cap)) return rc_stat(0);
        rc = dg_loadmem(ptr, size, slot);
        if (rc == 0) EWDX_LOGE("DGLOADMEMORY failed (slot %d, %d bytes)", slot, size);
        return rc_stat(rc);
    }
    case EWDX_DGFONT: {  // (bmscr auto,str,i,i)
        char *s = code_gets();
        int sz = code_getdi(12);
        code_getdi(0);
        return rc_stat(dg_font(s, sz));
    }
    case EWDX_DGDRAWTEXT: {  // (bmscr auto,str,i,i)
        char *s = code_gets();
        int x = code_getdi(0), y = code_getdi(0);
        code_getdi(0);
        return rc_stat(dg_drawtext(s, x, y));
    }
    case EWDX_DGADDPRIM: {
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        return rc_stat(dg_addprim());
    }
    case EWDX_DGDRAWPRIM: {
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        return rc_stat(dg_drawprim());
    }
    // --- hmm input ---
    case EWDX_DIINIT: {
        code_getdi(0); code_getdi(0); code_getdi(0);
        return rc_stat(dg_diinit());
    }
    case EWDX_DIEND: {
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        return rc_stat(dg_diend());
    }
    case EWDX_DIJOYNUM: {
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        // 1 virtual stick (touch dpad + keys + pad); script then runs
        // DIGETJOYSTATE instead of the joyg=0 keyboard fallback (label_198)
        return rc_stat(1);
    }
    case EWDX_DIJOYSTATE: {  // (pvar,i,i,i): joyg bitmask (see ewdx_input.h)
        PVal *pv;
        APTR ap = code_getva(&pv);
        int mask;
        code_getdi(0); code_getdi(0); code_getdi(0);
        ewdx_input_poll();  // fresh sample at the read site
        mask = ewdx_input_buttons();
        code_setva(pv, ap, HSPVAR_FLAG_INT, &mask);
        return rc_stat(-1);
    }
    // --- hspda sort (full impl, hsp3int parity) ---
    case EWDX_SORTVAL: {  // pexinfo: pulls (var, order)
        PVal *pv;
        APTR ap = code_getva(&pv);
        int order = code_getdi(0);
        int n, k;
        (void)ap;
        n = pv->len[1];
        if (n <= 0) throw(HSPERR_ILLEGAL_FUNCTION);
        rc_sortidx.clear();
        rc_sortidx.reserve((size_t)n);
        if (pv->flag == HSPVAR_FLAG_DOUBLE) {
            double *dp = (double *)pv->pt;
            for (k = 0; k < n; k++) {
                EwdxSortEnt e; e.dkey = dp[k]; e.ikey = 0; e.info = k; e.isdbl = 1;
                rc_sortidx.push_back(e);
            }
        } else if (pv->flag == HSPVAR_FLAG_INT) {
            int *ip = (int *)pv->pt;
            for (k = 0; k < n; k++) {
                EwdxSortEnt e; e.dkey = 0.0; e.ikey = ip[k]; e.info = k; e.isdbl = 0;
                rc_sortidx.push_back(e);
            }
        } else {
            throw(HSPERR_ILLEGAL_FUNCTION);
        }
        if (order == 0) std::sort(rc_sortidx.begin(), rc_sortidx.end(), rc_sort_lt_asc);
        else std::sort(rc_sortidx.begin(), rc_sortidx.end(), rc_sort_lt_desc);
        if (pv->flag == HSPVAR_FLAG_DOUBLE) {
            for (k = 0; k < n; k++) {
                double v = rc_sortidx[(size_t)k].dkey;
                code_setva(pv, k, HSPVAR_FLAG_DOUBLE, &v);
            }
        } else {
            int *ip = (int *)pv->pt;
            for (k = 0; k < n; k++) ip[k] = rc_sortidx[(size_t)k].ikey;
        }
        return rc_stat(0);
    }
    case EWDX_SORTGET: {  // pexinfo: pulls (var, n) -> original index
        PVal *pv;
        APTR ap = code_getva(&pv);
        int n = code_getdi(0);
        int res = 0;
        if (rc_sortidx.empty()) throw(HSPERR_ILLEGAL_FUNCTION);
        if (n >= 0 && n < (int)rc_sortidx.size()) res = rc_sortidx[(size_t)n].info;
        code_setva(pv, ap, HSPVAR_FLAG_INT, &res);
        return rc_stat(0);
    }
    // --- hspda vload/vsave (Hspda.cpp pull parity; live restore) ---
    case EWDX_VSAVESTART: {
        rc_vsave.active = 1;
        rc_vsave_ids.clear();
        return rc_stat(0);
    }
    case EWDX_VSAVEPUT: {
        PVal *pv;
        (void)code_getva(&pv);
        if (!rc_vsave.active) return rc_stat(-2);
        // Record the varid; values are serialized at vsave_end (same tick,
        // no intervening writes in label_035, so end-time read is exact).
        {
            int varid = code_getdebug_varid(pv);
            if (varid < 0) return rc_stat(-2);
            rc_vsave_ids.push_back(varid);
        }
        return rc_stat(0);
    }
    case EWDX_VSAVEEND: {
        char *fn = code_gets();
        int rc;
        ewdx_resolve(fn, rc_vsave_path, sizeof(rc_vsave_path));
        rc = rc_vsave_write(rc_vsave_path);
        rc_vsave.active = 0;
        rc_vsave_ids.clear();
        return rc_stat(rc);
    }
    case EWDX_VLOADSTART: {
        char fnbuf[1024];
        char *fn = code_gets();
        const unsigned char *img = NULL;
        int len = 0, nv = 0;
        strncpy(fnbuf, fn, sizeof(fnbuf) - 1);
        fnbuf[sizeof(fnbuf) - 1] = '\0';
        if (rc_vload.active && rc_vload.img != NULL) { free((void *)rc_vload.img); }
        rc_vload.img = NULL; rc_vload.len = 0; rc_vload.active = 0;
        // "save.dat" -> <filesDir>/save.dat so progress persists across boots
        ewdx_resolve(fnbuf, fnbuf, sizeof(fnbuf));
        nv = ewdx_hspv_open(fnbuf, &img, &len);  // validates magic/ver/pt_data
        if (nv < 0) {
            EWDX_LOGE("vload: cannot open '%s' (%d)", fnbuf, nv);
            return rc_stat(-1);
        }
        rc_vload.img = img; rc_vload.len = len; rc_vload.active = 1;
        return rc_stat(0);
    }
    case EWDX_VLOADGET: {
        PVal *pv;
        int varid, rc;
        (void)code_getva(&pv);
        if (!rc_vload.active) return rc_stat(-1);
        varid = code_getdebug_varid(pv);
        rc = rc_vload_restore(pv, varid);
        // v17 diagnostics: completion line per var (the per-var 'vload: rest'
        // line prints before the fill; this one confirms the fill outcome).
        {
            char name[64];
            char *vn = code_getdebug_varname(varid);
            snprintf(name, sizeof(name), "%s", vn ? vn : "?");
            EWDX_LOGW("vload: done '%s' rc=%d", name, rc);
        }
        return rc_stat(rc);
    }
    case EWDX_VLOADEND: {
        if (rc_vload.active && rc_vload.img != NULL) free((void *)rc_vload.img);
        rc_vload.img = NULL; rc_vload.len = 0; rc_vload.active = 0;
        return rc_stat(0);
    }
    // --- hspogg dmm (SE bank + mixer in ewdx_audio.cpp) ---
    case EWDX_DMMINI: {
        int ok;
        code_getdi(0); code_getdi(0); code_getdi(0);
        ok = ewdx_audio_init();
        rc_dmm_ok = (ok != 0) ? 1 : 0;
        return rc_stat(ok);
    }
    case EWDX_DMMBYE: {  // onexit CLEANUP entry; also ends audio
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        rc_dmm_ok = 0;
        ewdx_audio_shutdown();
        if (rc_vload.active && rc_vload.img != NULL) free((void *)rc_vload.img);
        rc_vload.img = NULL; rc_vload.len = 0; rc_vload.active = 0;
        rc_vsave.active = 0;
        rc_vsave_ids.clear();
        return rc_stat(-1);
    }
    case EWDX_DMMVOL: {  // pexinfo: pulls (slot, level in millibels)
        int slot = code_getdi(0);
        int lv = code_getdi(0);
        if (!rc_dmm_ok) return rc_stat(-1);
        return rc_stat(ewdx_se_vol(slot, lv));
    }
    case EWDX_DMMPAN: {  // pexinfo: pulls (slot, pan +-10000)
        int slot = code_getdi(0);
        int pan = code_getdi(0);
        if (!rc_dmm_ok) return rc_stat(-1);
        return rc_stat(ewdx_se_pan(slot, pan));
    }
    case EWDX_DMMLOAD: {  // pexinfo: pulls (file, slot)
        char fnbuf[1024];
        char *fn = code_gets();
        int slot;
        int rc;
        strncpy(fnbuf, fn, sizeof(fnbuf) - 1);
        fnbuf[sizeof(fnbuf) - 1] = '\0';
        slot = code_getdi(0);
        if (!rc_dmm_ok) return rc_stat(0);
        rc = ewdx_se_load(fnbuf, slot);
        return rc_stat(rc);
    }
    case EWDX_DMMPLAY: {  // pexinfo: pulls (slot)
        int slot = code_getdi(0);
        if (!rc_dmm_ok) return rc_stat(0);
        return rc_stat(ewdx_se_play(slot));
    }
    case EWDX_DMMSTOP: {
        int slot = code_getdi(0);
        if (!rc_dmm_ok) return rc_stat(-1);
        return rc_stat(ewdx_se_stop(slot));
    }
    // --- system shims ---
    case EWDX_LCID: {
        // en-US -> script takes the englishmode=1 branch (L82).
        // STEP-D: derive from the Android locale.
        return rc_stat(1033);
    }
    case EWDX_TBEGIN:
    case EWDX_TEND: {
        code_getdi(0);
        return rc_stat(0);  // TIMERR_NOERROR parity; no-op on Android
    }
    case EWDX_SLEEP: {  // Win32 Sleep is void -> stat untouched (parity)
        int ms = code_getdi(0);
        if (ms > 0) {
            struct timespec ts;
            ts.tv_sec = ms / 1000;
            ts.tv_nsec = (long)(ms % 1000) * 1000000L;
            nanosleep(&ts, NULL);
        }
        return RUNMODE_RUN;
    }
    case EWDX_INIWRITE: {  // (sptr x4): copy each out before the next pull
        char a0[256], k0[256], v0[1024], f0[1024];
        const char *p;
        p = rc_flexstr(NULL);
        strncpy(a0, p, sizeof(a0) - 1); a0[sizeof(a0) - 1] = '\0';
        p = rc_flexstr(NULL);
        strncpy(k0, p, sizeof(k0) - 1); k0[sizeof(k0) - 1] = '\0';
        p = rc_flexstr(NULL);
        strncpy(v0, p, sizeof(v0) - 1); v0[sizeof(v0) - 1] = '\0';
        p = rc_flexstr(NULL);
        strncpy(f0, p, sizeof(f0) - 1); f0[sizeof(f0) - 1] = '\0';
        return rc_stat(rc_ini_write(f0, a0, k0, v0));
    }
    case EWDX_INIREAD: {  // (sptr,sptr,sptr,pvar,int,sptr)
        const char *app = rc_flexstr(NULL);
        char a0[256], k0[256], d0[1024], f0[1024];
        PVal *pv;
        APTR ap;
        int size, cap, got;
        char *dst;
        strncpy(a0, app, sizeof(a0) - 1); a0[sizeof(a0) - 1] = '\0';
        {
            const char *key = rc_flexstr(NULL);
            strncpy(k0, key, sizeof(k0) - 1); k0[sizeof(k0) - 1] = '\0';
        }
        {
            const char *def = rc_flexstr(NULL);
            strncpy(d0, def, sizeof(d0) - 1); d0[sizeof(d0) - 1] = '\0';
        }
        ap = code_getva(&pv);
        size = code_getdi(0);
        {
            const char *file = rc_flexstr(NULL);
            strncpy(f0, file, sizeof(f0) - 1); f0[sizeof(f0) - 1] = '\0';
        }
        if (pv->flag != HSPVAR_FLAG_STR) throw(HSPERR_TYPE_MISMATCH);
        if (size <= 0) return rc_stat(0);
        dst = (char *)HspVarCorePtrAPTR(pv, ap);
        cap = rc_blockbytes(pv, ap);
        if (cap <= 0 || dst == NULL) return rc_stat(0);
        {
            // read into a scratch buffer first (dst may alias file content)
            char tmp[4096];
            int room = size;
            if (room > (int)sizeof(tmp)) room = (int)sizeof(tmp);
            if (room > cap) room = cap;
            got = rc_ini_read(f0, a0, k0, tmp, room);
            if (got <= 0) {
                size_t dl = strlen(d0);
                if (dl >= (size_t)room) dl = (size_t)(room - 1);
                memcpy(tmp, d0, dl);
                tmp[dl] = '\0';
                got = (int)dl;
            }
            memcpy(dst, tmp, (size_t)got + 1);
            return rc_stat(got);
        }
    }
    case EWDX_JOYPOS: {  // (int, pvar): no stick -> zero + UNPLUGGED
        int joyid = code_getdi(0);
        PVal *pv;
        APTR ap = code_getva(&pv);
        int cap;
        (void)joyid;
        cap = rc_blockbytes(pv, ap);
        if (cap > 52) cap = 52;  // JOYINFOEX sizeof
        memset(HspVarCorePtrAPTR(pv, ap), 0, (size_t)(cap > 0 ? cap : 0));
        return rc_stat(167);  // JOYERR_UNPLUGGED: callers take no-stick path
    }
    case EWDX_JOYNUM: {
        return rc_stat(0);  // jstickini sizes all tables to 0 -> keyboard
    }
    case EWDX_JOYCAPS: {
        code_getdi(0); code_getdi(0); code_getdi(0);
        return rc_stat(167);  // JOYERR_UNPLUGGED parity (callers test stat)
    }
    default:
        break;
    }
    {
        // Name the missing command loudly: a bare #Error 21 gives no clue.
        char msg[160];
        snprintf(msg, sizeof(msg), "UNSUPPORTED dll command (check lookup): %s", nm);
        EWDX_LOGE("%s", msg);
        ewdx_boot_journal(msg);
    }
    throw(HSPERR_UNSUPPORTED_FUNCTION);
    return RUNMODE_RUN;
}

static int rc_reffunc_ivalue = 0;

// Only DLLFUNC-function in the dump is timeGetTime (finfo 46, ot=4).
// Framing mirrors the stock reffunc_dllcmd: '(' ... ')' with zero args.
static void *rc_reffunc_dllcmd(int *type_res, int arg) {
    HSPHED *hed;
    STRUCTDAT *st;
    const char *nm;
    int maxf;
    int *type, *val;
    struct timespec ts;

    if (rc_ctx == NULL || rc_exinfo == NULL) throw(HSPERR_ILLEGAL_FUNCTION);
    if (arg < 0) throw(HSPERR_SYNTAX);
    hed = rc_ctx->hsphed;
    maxf = (hed == NULL) ? 0 : (int)(hed->max_finfo / (int)sizeof(STRUCTDAT));
    if (arg >= maxf) throw(HSPERR_SYNTAX);
    st = &rc_ctx->mem_finfo[arg];
    if (st->nameidx < 0 || (hed != NULL && st->nameidx >= hed->max_ds)) throw(HSPERR_SYNTAX);
    nm = &rc_ctx->mem_mds[st->nameidx];
    if (ewdx_lookup(nm) != EWDX_TGETTIME) throw(HSPERR_SYNTAX);
    rc_journal_dll(nm);

    type = rc_exinfo->nptype;
    val = rc_exinfo->npval;
    if (*type != TYPE_MARK) throw(HSPERR_INVALID_FUNCPARAM);
    if (*val != '(') throw(HSPERR_INVALID_FUNCPARAM);
    code_next();

    *type_res = HSPVAR_FLAG_INT;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    rc_reffunc_ivalue = (int)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));

    if (*type != TYPE_MARK) throw(HSPERR_INVALID_FUNCPARAM);
    if (*val != ')') throw(HSPERR_INVALID_FUNCPARAM);
    code_next();

    return &rc_reffunc_ivalue;
}

extern "C" void ewdx_register(HSP3TYPEINFO *info) {
    rc_ctx = info->hspctx;
    rc_exinfo = info->hspexinfo;
    rc_vload.img = NULL; rc_vload.len = 0; rc_vload.active = 0;
    rc_vsave.active = 0;
    rc_vsave_ids.clear();
    rc_dmm_ok = 0;
    rc_sortidx.clear();
    info->cmdfunc = rc_cmdfunc_dllcmd;
    info->reffunc = rc_reffunc_dllcmd;
    // ovplay EXTCMD hook (own typeinfo via HPIDAT; single plugin, same index)
    ewdx_ovplay_register(info->hspctx);
}
