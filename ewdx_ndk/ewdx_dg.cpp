// ewdx_dg.cpp - HSP command glue: script-facing DG*/DI* commands -> ewdx layer.
//
// HSP-side signatures verified against the live AX dump
// (artifacts/start_ax_dump.bin finfo/minfo: 240 entries, all "@16" names are
// STRUCTPRM_SUBID_OLDDLL, plain Win32 names are SUBID_DLL):
//   DGINIT(bmscr,i,i,i) DGSCREEN(pexinfo: pulls w,h,mode,depth,flag)
//   DGCOLOR/DGBUFFER/DGGSEL/DGBLENDMODE/DGGCOPY/DGTEXTURE/DGCREATEPRIMITIVE/
//   DGPOS/DGRECT/DGSCALEANDANGLE/DGLINE(int x4, trailing defaults 0)
//   DGCLEAR/DGREDRAW/DGEND(int x4, always defaulted) DGFONT/DGDRAWTEXT(bmscr,str,i,i)
//   DGLOADMEMORY/DIGETJOYSTATE(pvar,i,i,i) DIINIT(bmscr,i,i,i)
//
// All functions are called from ewdx_register.cpp (never by address), so they
// carry normal C++ linkage; the ARM64/stdcall mismatch is sidestepped because
// the register pulls script args via code_getdi()/code_getva() like hsp3dish.
#include "ewdx_dg.h"
#include "ewdx_gles.h"
#include "ewdx_batch.h"
#include "ewdx_boot.h"

#include <string.h>

// --- lifecycle / targets ---

int dg_init(void)                                 { return ewdx_init(); }
int dg_screen(int w, int h, int m, int d, int f)  { (void)d; (void)f; return ewdx_screen(w, h, m); }
int dg_buffer(int id, int w, int h)               { return ewdx_buffer(id, w, h); }
int dg_select(int id)                             { return ewdx_select(id); }
int dg_color(int r, int g, int b, int a)          { return ewdx_color(r, g, b, a); }
int dg_clear(void)                                { return ewdx_clear(); }
int dg_redraw(void)                               { int rc = ewdx_present(); ewdx_boot_frame(); return rc; }
int dg_blend(int m)                               { return ewdx_apply_blend(m); }
int dg_end(void)                                  { return ewdx_shutdown(); }

// --- retained-state draw path ---

int dg_pos(int x, int y)                          { return ewdx_pos(x, y); }
int dg_rect(int x, int y, int w, int h)           { return ewdx_rect(x, y, w, h); }
int dg_scale(int x, int y, int a)                 { return ewdx_scale(x, y, a); }
int dg_copy(int id)                               { return ewdx_copy(id); }
int dg_copyf(int id, int flags)                   { return ewdx_copy_flags(id, flags); }
int dg_texture(int id)                            { return ewdx_texture(id); }
int dg_loadmem(const void *b, int s, int n)       { return ewdx_loadmemory(b, s, n); }
int dg_createprim(int n)                          { return ewdx_createprim(n); }
int dg_addprim(void)                              { return ewdx_addprim(); }
int dg_drawprim(void)                             { return ewdx_drawprim(); }
int dg_font(const char *n, int s)                 { return ewdx_font(n, s); }
int dg_drawtext(const char *s, int x, int y)      { return ewdx_drawtext(s, x, y); }
int dg_line(int x1, int y1, int x2, int y2)       { return ewdx_line(x1, y1, x2, y2); }

// --- input lifecycle (state in ewdx_input.cpp; mask via register) ---

int dg_diinit(void)                               { return -1; }
int dg_diend(void)                                { return -1; }

// Verified stat semantics preserved by the register (ctx->stat = return):
//   DGLOADMEMORY 0 on failure -> script shows dialog + end.
//   DGINIT/DIINIT 0 -> "DirectGraphics/DirectInput ..." dialogs.
//   DIGETJOYNUM 1 -> DIGETJOYSTATE fills joyg bitmask (label_198 branch).
