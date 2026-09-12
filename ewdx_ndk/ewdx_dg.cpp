// ewdx_dg.cpp - HSP command glue: script-facing DG*/DI* commands -> ewdx_gles.cpp
// Registration pattern follows OpenHSP hsp3dish (HSP3TYPEINFO cmdfunc table).
// HSP-side signatures reproduced from start_ax_dump.hsp #func lines so the
// decompiled script recompiles unchanged under hspcmp targeting this plugin.
//
//   DGINIT                          -> ewdx_init()
//   DGSCREEN w,h,mode,32,0          -> ewdx_screen(w,h,mode)
//   DGBUFFER id,w,h                 -> ewdx_buffer(id,w,h)
//   DGGSEL id                       -> ewdx_select(id)
//   DGCOLOR r,g,b,a                 -> ewdx_color(r,g,b,a)
//   DGCLEAR                         -> ewdx_clear()
//   DGREDRAW                        -> ewdx_present()
//   DGBLENDMODE m                   -> ewdx_apply_blend(m)
//   DGEND                           -> ewdx_shutdown()
//
// Step (c) commands (quad batcher: DGPOS/DGRECT/DGSCALEANDANGLE/DGGCOPY,
// DGTEXTURE/DGCREATEPRIMITIVE/DGADDPRIMITIVE/DGDRAWPRIMITIVE,
// DGFONT/DGDRAWTEXT/DGLINE/DGLOADMEMORY) attach to EwdxGles.vbo/prog here.
#include "ewdx_gles.h"
#include "ewdx_batch.h"

// --- thin wrappers with the exact arities observed at call sites ---

static int dg_init(void)                       { return ewdx_init(); }
static int dg_screen(int w,int h,int m,int,int) { return ewdx_screen(w,h,m); }
static int dg_buffer(int id,int w,int h)       { return ewdx_buffer(id,w,h); }
static int dg_select(int id)                   { return ewdx_select(id); }
static int dg_color(int r,int g,int b,int a)   { return ewdx_color(r,g,b,a); }
static int dg_clear(void)                      { return ewdx_clear(); }
static int dg_redraw(void)                     { return ewdx_present(); }
static int dg_blend(int m)                     { return ewdx_apply_blend(m); }
static int dg_end(void)                        { return ewdx_shutdown(); }

// --- step (c) wrappers: DGPOS/DGRECT/DGSCALEANDANGLE/DGGCOPY batcher ---
static int dg_pos(int x, int y)                { return ewdx_pos(x, y); }
static int dg_rect(int x, int y, int w, int h) { return ewdx_rect(x, y, w, h); }
static int dg_scale(int x, int y, int a)       { return ewdx_scale(x, y, a); }
static int dg_copy(int id)                     { return ewdx_copy(id); }
static int dg_texture(int id)                  { return ewdx_texture(id); }
static int dg_loadmem(const void *b, int s, int n) { return ewdx_loadmemory(b, s, n); }
static int dg_createprim(int n)                { return ewdx_createprim(n); }
static int dg_addprim(void)                    { return ewdx_addprim(); }
static int dg_drawprim(void)                   { return ewdx_drawprim(); }
static int dg_font(const char *n, int s)       { return ewdx_font(n, s); }
static int dg_drawtext(const char *s, int x, int y) { return ewdx_drawtext(s, x, y); }
static int dg_line(int x1, int y1, int x2, int y2) { return ewdx_line(x1, y1, x2, y2); }

// HSP plugin entry table (names must match the #func names in script):
//   "DGINIT" "DGSCREEN" "DGBUFFER" "DGGSEL" "DGCOLOR" "DGCLEAR" "DGREDRAW"
//   "DGBLENDMODE" "DGEND"
//   "DGPOS" "DGRECT" "DGSCALEANDANGLE" "DGGCOPY" "DGTEXTURE" "DGLOADMEMORY"
//   "DGCREATEPRIMITIVE" "DGADDPRIMITIVE" "DGDRAWPRIMITIVE"
//   "DGFONT" "DGDRAWTEXT" "DGLINE"
// TODO(step b2): register via hsp3ext_ndk.cpp using
//   code_enable_typeinfo() + BindFUNC(), mirroring how hmm.dll's
//   HPIDAT/finfo table binds "_DGINIT@16" etc. on Windows.
//   Calling convention on ARM64 differs from x86 stdcall @16; the HSP
//   bytecode passes plain ints so the glue reads params with
//   code_getdi()/code_getva() like hsp3dish does (see hsp3gr_dish.cpp),
//   NOT via native varargs.

// Verified stat semantics to preserve:
//   DGLOADMEMORY sets stat==0 on failure -> script shows dialog + end.
//   Our port must set the equivalent stat so missing-asset errors surface
//   identically (shimmed to __android_log_print, not a Win32 dialog).
