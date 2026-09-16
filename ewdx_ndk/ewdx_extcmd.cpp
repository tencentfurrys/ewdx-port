// ewdx_extcmd.cpp - minimal EXTCMD/EXTSYSVAR shims (see header).
//
// Opcode provenance: OpenHSP src/hsp3dish/hsp3gr_dish.cpp cmdfunc_extcmd
// (button 0x00 ... dialog 0x03, mes 0x0f, title 0x10, pos 0x11, cls 0x13,
// font 0x14, color 0x18, redraw 0x1b, width 0x1c, gsel 0x1d, getkey 0x23,
// screen 0x29/0x2a/0x2b, mouse 0x2c, stick 0x34) + reffunc_function
// (ginfo 0x000, dirinfo 0x002, sysinfo 0x003). Only the subset the game
// touches is implemented; everything else throws UNSUPPORTED (loud, like
// stock) so missing coverage is found in logcat, not silently wrong.
//
// Verified call sites (artifacts/start_ax_dump.hsp):
//   screen L129, title L131/134, cls L136, mouse L139, dialog L150/154/...,
//   font L497 + mes L450..468 + pos L449/456, stick L362, getkey L363+,
//   ginfo(20/21) L129, dirinfo L212, bload L167 (INTCMD, not here).
#include "ewdx_extcmd.h"
#include "ewdx_gles.h"
#include "ewdx_batch.h"
#include "ewdx_input.h"
#include "ewdx_paths.h"
#include "ewdx_log.h"

#include "hsp3code.h"
#include "hspvar_core.h"

#include "ewdx_text.h"  // ewdx_sjis_to_utf8: SDL/JNI paths need UTF-8, not SJIS

#include <stdio.h>
#include <string.h>
#include "ewdx_boot.h"

#include <SDL.h>

// EXTCMD opcodes (hsp3gr_dish parity)
#define EXCMD_BUTTON 0x00
#define EXCMD_EXEC 0x02
#define EXCMD_DIALOG 0x03
#define EXCMD_MES 0x0f
#define EXCMD_TITLE 0x10
#define EXCMD_POS 0x11
#define EXCMD_CLS 0x13
#define EXCMD_FONT 0x14
#define EXCMD_COLOR 0x18
#define EXCMD_REDRAW 0x1b
#define EXCMD_WIDTH 0x1c
#define EXCMD_GSEL 0x1d
#define EXCMD_GETKEY 0x23
#define EXCMD_SCREEN 0x29   // +0x2a screen / 0x2b bgscr share the handler
#define EXCMD_MOUSE 0x2c
#define EXCMD_STICK 0x34

static HSPCTX *ex_ctx = NULL;
static HSPEXINFO *ex_exinfo = NULL;

// dish mes cursor (pos) -- logged; on-screen console lands later.
static int ex_mes_x = 0, ex_mes_y = 0;

static int ex_stat(int v) {
    if (ex_ctx != NULL) ex_ctx->stat = v;
    return RUNMODE_RUN;
}

static void ex_title(const char *s) {
    if (s == NULL) s = "";
    if (ewdx.win != NULL) SDL_SetWindowTitle(ewdx.win, s);
    EWDX_LOGW("title: %s", s);
}

// Boot-phase journal of EXTCMD/EXTSYSVAR use ("ex: 0x.." lines in boot.log,
// matching the 2026-09-12 device build's diagnostics). First use only per
// opcode, so it stays quiet during normal gameplay.
static void ex_journal_cmd(int cmd) {
    static unsigned char seen[256];
    char msg[32];
    if ((cmd & ~0xff) != 0 || seen[cmd & 0xff]) return;
    seen[cmd & 0xff] = 1;
    snprintf(msg, sizeof(msg), "ex: 0x%02x", cmd);
    ewdx_boot_journal(msg);
}

static int ex_cmdfunc(int cmd) {
    if (ex_ctx == NULL) throw(HSPERR_ILLEGAL_FUNCTION);
    ex_journal_cmd(cmd);
    code_next();  // mandatory advance (EXTCMD convention)
    switch (cmd) {
    case EXCMD_DIALOG: {
        // dialog mes [, mode, title]: mode bit1 == info/error icon on Win.
        // Dish shows a message box; we log + SDL box (blocking, like Win).
        // NOTE: code_gets()/code_getds() return pointers into HSP's shared
        // temp param buffer -- copy mes BEFORE fetching the title, or the
        // title fetch overwrites it (seen on device: 'dialog: ERROR' was
        // the title clobbering the picture-missing message). Same order
        // and copy discipline as OpenHSP hsp3gr_wingui.cpp cmdfunc_dialog.
        char mesbuf[1024];
        char titlebuf[256];
        {
            char *mes = code_gets();
            snprintf(mesbuf, sizeof(mesbuf), "%s", (mes != NULL) ? mes : "");
        }
        int mode = code_getdi(0);
        {
            // title is optional on real HSP (code_getds, default "").
            char *title = code_getds("");
            snprintf(titlebuf, sizeof(titlebuf), "%s", (title != NULL) ? title : "");
        }
        EWDX_LOGE("dialog: %s", mesbuf);
        // SDL_ShowSimpleMessageBox -> JNI NewStringUTF on Android: raw SJIS
        // bytes abort ART ('illegal start byte') and kill the whole process
        // (2026-09-16 v3 run 1: dialog at ex:0x03, SIGABRT). Convert first.
        if (ewdx.win != NULL) {
            char mes_u8[2048], title_u8[512];
            ewdx_sjis_to_utf8(mesbuf, mes_u8, (int)sizeof(mes_u8));
            ewdx_sjis_to_utf8(titlebuf, title_u8, (int)sizeof(title_u8));
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,
                                     title_u8[0] ? title_u8 : "EchidnaWarsDX",
                                     mes_u8, ewdx.win);
        }
        (void)mode;
        return ex_stat(0);
    }
    case EXCMD_TITLE: {
        char *s = code_gets();
        char u8[1024];
        if (s != NULL && ewdx.win != NULL) {
            // SDL_SetWindowTitle -> JNI NewStringUTF: same SJIS abort risk.
            ewdx_sjis_to_utf8(s, u8, (int)sizeof(u8));
            SDL_SetWindowTitle(ewdx.win, u8);
        }
        ex_title(s);
        return ex_stat(0);
    }
    case EXCMD_MES: {
        // mes/print: dish text console. Log it; in-game text uses DGDRAWTEXT.
        char *s = code_gets();
        EWDX_LOGW("mes@(%d,%d): %s", ex_mes_x, ex_mes_y, s);
        return ex_stat(0);
    }
    case EXCMD_POS: {
        ex_mes_x = code_getdi(0);
        ex_mes_y = code_getdi(0);
        return ex_stat(0);
    }
    case EXCMD_FONT: {
        // font name, size [, style]: dish GDI font for mes. Keep for parity
        // logging; DGFONT (e baptized separately) owns the GL text path.
        char *name = code_gets();
        int size = code_getdi(12);
        int style = code_getdi(0);
        EWDX_LOGW("font: '%s' %d (style %d)", name, size, style);
        return ex_stat(0);
    }
    case EXCMD_CLS: {
        // cls mode: dish clears the Bmscr. Our screen IS the GLES default
        // framebuffer; clear it with the current DGCOLOR (matches DG flow).
        int mode = code_getdi(0);
        (void)mode;
        ewdx_flush();
        glClearColor(ewdx.st.r / 255.0f, ewdx.st.g / 255.0f,
                     ewdx.st.b / 255.0f, ewdx.st.a / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return ex_stat(0);
    }
    case EXCMD_COLOR: {
        int r = code_getdi(0), g = code_getdi(0), b = code_getdi(0);
        ewdx.st.r = r; ewdx.st.g = g; ewdx.st.b = b;
        return ex_stat(0);
    }
    case EXCMD_REDRAW: {
        // redraw mode: 0 = flip, 1 = enter batch. Game never calls it
        // (uses DGREDRAW); flush + present on 0, no-op on 1.
        int mode = code_getdi(0);
        if (mode == 0) {
            ewdx_flush();
            if (ewdx.win != NULL) SDL_GL_SwapWindow(ewdx.win);
        }
        return ex_stat(0);
    }
    case EXCMD_WIDTH:
    case EXCMD_GSEL: {
        // width/gsel select dish Bmscr slots -- no dish Bmscr here; the
        // game draws via DGBUFFER/DGGSEL instead. Consume args, no-op.
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        return ex_stat(0);
    }
    case EXCMD_SCREEN:
    case 0x2a:
    case 0x2b: {
        // screen/bgscr id, w, h, mode, x, y: create/resize the SDL window.
        // The ewdx layer owns the window (DGSCREEN reuses it), so route
        // straight there; x/y/mode are window-manager hints (ignored).
        int id = code_getdi(0);
        int w = code_getdi(640);
        int h = code_getdi(480);
        int mode = code_getdi(0);
        int x = code_getdi(0);
        int y = code_getdi(0);
        (void)id; (void)mode; (void)x; (void)y;
        if (w <= 0) w = 640;
        if (h <= 0) h = 480;
        if (w > 1920) w = 1920;
        if (h > 1080) h = 1080;
        // ewdx_screen needs SDL inited; DGINIT normally does that, but
        // the script calls screen FIRST (L129 < L148), so init here.
        if (ewdx.win == NULL) {
            if (ewdx_init() == 0) {
                EWDX_LOGE("screen: ewdx_init failed");
                return ex_stat(1);
            }
        }
        return ex_stat(ewdx_screen(w, h, 0));
    }
    case EXCMD_MOUSE: {
        // mouse x, y [, mode]: x<0 hides cursor (game: mouse -1,0 at boot).
        int x = code_getdi(0);
        int y = code_getdi(0);
        int mode = code_getdi(0);
        (void)y; (void)mode;
        if (x < 0) SDL_ShowCursor(SDL_DISABLE);
        else SDL_ShowCursor(SDL_ENABLE);
        return ex_stat(0);
    }
    case EXCMD_GETKEY: {
        // getkey var, code: dish polls async key state. Our polled mask
        // lives in ewdx_input; map the requested VK to joyg-ish truth.
        // Full VK table lands with the core link; for now: arrows + Z/X
        // report live, everything else 0 (jstickini sizes to 0 anyway).
        PVal *pv;
        APTR ap = code_getva(&pv);
        int vk = code_getdi(0);
        int down = 0;
        int mask = ewdx_input_buttons();
        switch (vk) {
        case 37: down = (mask & EWDX_JOY_LEFT) != 0; break;
        case 38: down = (mask & EWDX_JOY_UP) != 0; break;
        case 39: down = (mask & EWDX_JOY_RIGHT) != 0; break;
        case 40: down = (mask & EWDX_JOY_DOWN) != 0; break;
        case 90:  // Z (VK_Z; lower/upper share the code on Win)
            down = (mask & EWDX_JOY_BTN0) != 0; break;
        case 88:  // X (VK_X)
            down = (mask & EWDX_JOY_BTN1) != 0; break;
        default: down = 0; break;
        }
        code_setva(pv, ap, HSPVAR_FLAG_INT, &down);
        return ex_stat(down);
    }
    case EXCMD_STICK: {
        // stick var, mode: dish joystick poll. Game's joystick path is
        // DIGETJOYSTATE (we return 1 stick there); this fallback reports
        // the same mask so keyboard play works either way.
        PVal *pv;
        APTR ap = code_getva(&pv);
        int mode = code_getdi(0);
        int mask = ewdx_input_buttons();
        (void)mode;
        code_setva(pv, ap, HSPVAR_FLAG_INT, &mask);
        return ex_stat(mask);
    }
    case EXCMD_BUTTON:
    case EXCMD_EXEC: {
        // Config-menu UI (button L26958+, exec). No dish widgets on GLES;
        // log + no-op so boot/menus continue (menus are being re-homed
        // to touch input via ewdx_input).
        EWDX_LOGW("extcmd 0x%02x stub (config UI lands later)", cmd);
        // consume up to 4 generic args to keep the pc in sync for the
        // common arities; extra defaults are harmless.
        code_getdi(0); code_getdi(0); code_getdi(0); code_getdi(0);
        return ex_stat(0);
    }
    default:
        break;
    }
    throw(HSPERR_UNSUPPORTED_FUNCTION);
    return RUNMODE_RUN;
}

static int ex_reffunc_ivalue = 0;

static void *ex_reffunc(int *type_res, int arg) {
    int fn = arg & 0xff;
    int is_func = (arg & 0x100) != 0;
    if (ex_ctx == NULL || ex_exinfo == NULL) throw(HSPERR_ILLEGAL_FUNCTION);
    if (!is_func) {
        // Plain system vars (cnt/stat/strsize/... are INTFUNC, not here).
        throw(HSPERR_SYNTAX);
    }
    if (*ex_exinfo->nptype != TYPE_MARK || *ex_exinfo->npval != '(')
        throw(HSPERR_INVALID_FUNCPARAM);
    code_next();
    switch (fn) {
    case 0x000: {  // ginfo(n)
        int n = code_getdi(0);
        int v = 0;
        int *type = ex_exinfo->nptype;
        int *val = ex_exinfo->npval;
        switch (n) {
        case 12: case 26: v = (ewdx.scr_w > 0) ? ewdx.scr_w : 640; break;
        case 13: case 27: v = (ewdx.scr_h > 0) ? ewdx.scr_h : 480; break;
        case 20: {  // desktop w
            SDL_DisplayMode dm;
            v = 1280;
            if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0) v = dm.w;
            break;
        }
        case 21: {  // desktop h
            SDL_DisplayMode dm;
            v = 720;
            if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.h > 0) v = dm.h;
            break;
        }
        case 2: v = 1; break;   // active window id
        case 3: v = 0; break;   // current window
        default: v = 0; break;
        }
        if (*type != TYPE_MARK || *val != ')') throw(HSPERR_INVALID_FUNCPARAM);
        code_next();
        *type_res = HSPVAR_FLAG_INT;
        ex_reffunc_ivalue = v;
        return &ex_reffunc_ivalue;
    }
    case 0x002: {  // dirinfo(n) -> str
        int n = code_getdi(0);
        int *type = ex_exinfo->nptype;
        int *val = ex_exinfo->npval;
        char *out = ex_ctx->stmp;
        if (*type != TYPE_MARK || *val != ')') throw(HSPERR_INVALID_FUNCPARAM);
        code_next();
        if (n == 0) {
            const char *fd = ewdx_files_dir();
            strncpy(out, (fd != NULL && fd[0]) ? fd : ".", HSPCTX_REFSTR_MAX - 1);
            out[HSPCTX_REFSTR_MAX - 1] = '\0';
        } else if (n == 1) {
            strncpy(out, ".", HSPCTX_REFSTR_MAX - 1);
            out[HSPCTX_REFSTR_MAX - 1] = '\0';
        } else {
            out[0] = '\0';
        }
        *type_res = HSPVAR_FLAG_STR;
        return out;
    }
    case 0x003: {  // sysinfo(n) -> int/str
        int n = code_getdi(0);
        int *type = ex_exinfo->nptype;
        int *val = ex_exinfo->npval;
        char *out = ex_ctx->stmp;
        int flag = HSPVAR_FLAG_INT;
        if (*type != TYPE_MARK || *val != ')') throw(HSPERR_INVALID_FUNCPARAM);
        code_next();
        if (n == 0) {
            strncpy(out, "Android", HSPCTX_REFSTR_MAX - 1);
            out[HSPCTX_REFSTR_MAX - 1] = '\0';
            flag = HSPVAR_FLAG_STR;
        } else {
            ex_reffunc_ivalue = 0;
            *type_res = HSPVAR_FLAG_INT;
            return &ex_reffunc_ivalue;
        }
        *type_res = flag;
        return out;
    }
    default:
        break;
    }
    throw(HSPERR_UNSUPPORTED_FUNCTION);
    return NULL;
}

extern "C" void ewdx_extcmd_register(HSP3TYPEINFO *info_extcmd, HSP3TYPEINFO *info_extsysvar) {
    if (info_extcmd != NULL) {
        ex_ctx = info_extcmd->hspctx;
        ex_exinfo = info_extcmd->hspexinfo;
        info_extcmd->cmdfunc = ex_cmdfunc;
    }
    if (info_extsysvar != NULL) {
        if (ex_ctx == NULL) ex_ctx = info_extsysvar->hspctx;
        if (ex_exinfo == NULL) ex_exinfo = info_extsysvar->hspexinfo;
        info_extsysvar->reffunc = ex_reffunc;
    }
}
