// ewdx_batch.h - step (c) quad batcher + texture upload + prim/text stubs
//
// Provenance (Ghidra HSP3Runtime, hmm.dll):
//   FUN_10001fa0  DGGCOPY inner: 4-vert TRIANGLEFAN, FVF XYZRHW|DIFFUSE|TEX1
//                 (stride 0x1c), rotate-about-center via 256-entry sin/cos LUT,
//                 8.8-fixed scale (/256), 0.5 half-pixel offsets, UV clip+flip.
//   FUN_10002460  DGSCALEANDANGLE inner: scale stored float, angle &= 0xff.
//   FUN_10002b50  DGTEXTURE inner: deferred texture-id store.
//   FUN_10001b30  DGLOADMEMORY inner: D3DXCreateTextureFromFileInMemoryEx,
//                 forced A8R8G8B8, ColorKey = 0xff000000 (opaque BLACK).
// On-disk constants (.rdata): K_ONE=1.0, K_HALF=0.5, K_INV256=1/256.
#ifndef __EWDX_BATCH_H
#define __EWDX_BATCH_H

#include "ewdx_gles.h"

#define EWDX_BATCH_QUADS 1024

// 256-step rotation LUT (angle idx 0..255 = full circle). Game passes 0.
extern float ewdx_sinLut[256];
extern float ewdx_cosLut[256];
void ewdx_lut_init(void);

// Draw-state setters (mirror hmm ctx +0x13ec..0x1420)
int ewdx_pos(int x, int y);                    // DGPOS
int ewdx_rect(int sx, int sy, int sw, int sh); // DGRECT (src x,y,w,h)
int ewdx_scale(int sx, int sy, int angle);     // DGSCALEANDANGLE (8.8 fixed, 8-bit angle)
int ewdx_texture(int id);                      // DGTEXTURE (prim-path bind)

// Execute (flushes batch on texture/blend/target change)
int ewdx_copy(int id);                         // DGGCOPY (flags reserved = 0, as called)
int ewdx_copy_flags(int id, int flags);        // bit1=center bit2=abs-scale bit4+=pivot
                                               // bit8=u-flip bit0x10=v-flip

// Upload with D3DX colorkey parity: RGB(0,0,0) -> alpha 0. Returns -1 ok / 0 fail
// (matches hmm stat convention: stat==0 triggers the script error path).
int ewdx_loadmemory(const void *bmp, int size, int slot); // DGLOADMEMORY

// Primitive path (best-effort; 13 ADD/DRAW pairs in script, effects only)
int ewdx_createprim(int n);                    // DGCREATEPRIMITIVE
int ewdx_addprim(void);                        // DGADDPRIMITIVE (uses pos/color/tex state)
int ewdx_drawprim(void);                       // DGDRAWPRIMITIVE (immediate flush)

// Text (needs SDL_ttf - deferred; stubs keep menus non-fatal)
int ewdx_font(const char *name, int size);     // DGFONT
int ewdx_drawtext(const char *s, int x, int y);// DGDRAWTEXT

int ewdx_line(int x1, int y1, int x2, int y2); // DGLINE (1px perp quad)

void ewdx_flush(void);  // flush pending quads (called by present + state changes)

#endif
