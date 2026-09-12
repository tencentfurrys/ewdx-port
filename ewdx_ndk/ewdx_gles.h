// ewdx_gles.h - EchidnaWarsDX NDK runtime: GLES2/SDL2 state
// Target home in OpenHSP tree: src/hsp3dish/ndk/ewdx/
// Drafted from: hmm.dll export table (114 fns) + decompiled game call sites
// Blend values verified against hmm.dll FUN_10001d70 (SetRenderState dispatcher)
#ifndef __EWDX_GLES_H
#define __EWDX_GLES_H

#include <SDL.h>
#include <GLES2/gl2.h>

// Single id space 0..127 like hmm (slot valid iff id<0x80; bmpload uses 7+).
// DGBUFFER ids additionally own an FBO; DGLOADMEMORY ids are textures only.
#define EWDX_MAX_BUFFERS 128

// Blend modes 0..7 as dispatched by hmm.dll FUN_10001d70.
// Game script uses 0..4 (+ per-frame values from .mot part(13,*) data).
typedef enum {
    EWDX_BLEND_COPY = 0,   // SRC=ONE, DST=ZERO        (opaque)
    EWDX_BLEND_ALPHA = 1,  // SRC=SRCALPHA, DST=INVSRCALPHA (normal alpha)
    EWDX_BLEND_ADD = 2,    // SRC=SRCALPHA, DST=ONE     (additive)
    EWDX_BLEND_SUB = 3,    // SRC=ZERO, DST=INVSRCCOLOR (darken)
    EWDX_BLEND_MUL = 4,    // SRC=ZERO, DST=SRCCOLOR    (multiply)
    EWDX_BLEND_SCR = 5,    // SRC=INVDESTCOLOR, DST=ZERO
    EWDX_BLEND_ADD2 = 6,   // SRC=ONE, DST=ONE
    EWDX_BLEND_DSTC = 7    // SRC=DESTCOLOR, DST=ONE
} EWDX_BLEND;

typedef struct {
    GLuint fbo;     // nonzero only for DGBUFFER targets (0 = plain texture/default)
    GLuint tex;
    int w, h;       // logical dims (clip + UV normalize use these; no POT pad in GLES2)
    int valid;
    int vflip;      // 1 = sample with V flip (FBO-backed, mirrors ctx+0x90 flag)
} EwdxBuffer;

// Deferred draw state = DG* retained state (DGPOS/DGRECT/DGCOLOR/... set, DGGCOPY executes)
typedef struct {
    int x, y;                 // DGPOS dest position
    int sx, sy, sw, sh;       // DGRECT source rect
    int scalex, scaley;       // DGSCALEANDANGLE, 8.8 fixed (256 = 1.0)
    int angle;                // DGSCALEANDANGLE, hmm angle units
    int r, g, b, a;           // DGCOLOR 0..255
    int blend;                // DGBLENDMODE 0..7
} EwdxDrawState;

typedef struct {
    SDL_Window *win;
    SDL_GLContext ctx;
    int scr_w, scr_h;
    EwdxBuffer buf[EWDX_MAX_BUFFERS];
    int target;               // DGGSEL current render target
    EwdxDrawState st;         // current draw state
    GLuint prog;              // textured-quad program
    GLuint vbo;
    GLint a_pos, a_uv, a_col;
    GLint u_tex;
} EwdxGles;

extern EwdxGles ewdx;

// Lifecycle / commands implemented in ewdx_gles.cpp + ewdx_dg.cpp
int ewdx_init(void);                          // DGINIT
int ewdx_screen(int w, int h, int mode);      // DGSCREEN (depth always 32, flag ignored)
int ewdx_buffer(int id, int w, int h);        // DGBUFFER
int ewdx_select(int id);                      // DGGSEL
int ewdx_color(int r, int g, int b, int a);   // DGCOLOR
int ewdx_clear(void);                         // DGCLEAR
int ewdx_present(void);                       // DGREDRAW
int ewdx_apply_blend(int mode);               // DGBLENDMODE -> glBlendFunc
int ewdx_shutdown(void);                      // DGEND

#endif
