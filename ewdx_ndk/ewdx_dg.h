// ewdx_dg.h - script-facing DG*/DI* glue: exact arities at call sites.
//
// Every function here is consumed by ewdx_register.cpp, which pulls the
// matching script args via code_getdi()/code_gets()/code_getva() and assigns
// the return value to ctx->stat. hmm convention: -1 = ok, 0 = fail; the
// script tests `stat == 0` for the error path (DGINIT/DIINIT/DGLOADMEMORY).
#ifndef __EWDX_DG_H
#define __EWDX_DG_H

// --- lifecycle / targets (ewdx_gles.cpp) ---
int dg_init(void);                              // DGINIT
int dg_screen(int w, int h, int m, int d, int f); // DGSCREEN (d=depth32, f=flag: ignored)
int dg_buffer(int id, int w, int h);            // DGBUFFER
int dg_select(int id);                          // DGGSEL
int dg_color(int r, int g, int b, int a);       // DGCOLOR
int dg_clear(void);                             // DGCLEAR
int dg_redraw(void);                            // DGREDRAW
int dg_blend(int m);                            // DGBLENDMODE
int dg_end(void);                               // DGEND

// --- retained-state draw path (ewdx_batch.cpp) ---
int dg_pos(int x, int y);                       // DGPOS
int dg_rect(int x, int y, int w, int h);        // DGRECT
int dg_scale(int x, int y, int a);              // DGSCALEANDANGLE
int dg_copy(int id);                            // DGGCOPY (flags = 0)
int dg_copyf(int id, int flags);                // DGGCOPY with mirror/center
                                                // flags (title uses 1/2/8)
int dg_texture(int id);                         // DGTEXTURE
int dg_loadmem(const void *b, int s, int n);    // DGLOADMEMORY (b=BMP bytes, s=size, n=slot)
int dg_createprim(int n);                       // DGCREATEPRIMITIVE
int dg_addprim(unsigned flags);                 // DGADDPRIMITIVE: arg = DGGCOPY flag word
int dg_drawprim(void);                          // DGDRAWPRIMITIVE
int dg_font(const char *n, int s);              // DGFONT (SDL_ttf string cache)
int dg_drawtext(const char *s, int x, int y);   // DGDRAWTEXT (immediate quad)
int dg_line(int x1, int y1, int x2, int y2);    // DGLINE

// --- input lifecycle (hmm DI*); state lives in ewdx_input.cpp ---
int dg_diinit(void);                            // DIINIT
int dg_diend(void);                             // DIEND
// (DIGETJOYNUM/DIGETJOYSTATE are served directly by the register from
// ewdx_input_buttons(); joyg bit layout documented in ewdx_input.h)

#endif
