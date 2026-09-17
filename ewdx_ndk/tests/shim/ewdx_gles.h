// Test-only shim: satisfies ewdx_input.cpp's use of ewdx.scr_w/scr_h without
// linking GLES. Used ONLY by tests (shim dir precedes ewdx_ndk in -I order).
#ifndef __EWDX_GLES_H
#define __EWDX_GLES_H

typedef struct {
    int scr_w, scr_h;
    int viewport[4];  // target-0 letterbox (unused by input tests; kept in sync)
} EwdxGles;

extern EwdxGles ewdx;

#endif
