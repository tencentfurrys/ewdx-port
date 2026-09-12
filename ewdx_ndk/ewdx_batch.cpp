// ewdx_batch.cpp - step (c) quad batcher, verbatim port of FUN_10001fa0 math
#include "ewdx_batch.h"
#include "ewdx_text.h"
#include "ewdx_boot.h"
#include <SDL.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

float ewdx_sinLut[256];
float ewdx_cosLut[256];

// .rdata constants from hmm.dll
#define K_ONE   1.0f
#define K_HALF  0.5f
#define K_INV256 (1.0f / 256.0f)

// D3DX colorkey used by FUN_10001b30 (DGLOADMEMORY inner)
#define COLORKEY_R 0
#define COLORKEY_G 0
#define COLORKEY_B 0

// Batch vertex: pos2 + uv2 + col4 (matches ewdx prog attrib layout)
typedef struct { float x, y, u, v, r, g, b, a; } EwdxVert;
#define VERT_FLOATS 8

static EwdxVert batch[EWDX_BATCH_QUADS * 6];
static int batch_quads = 0;
static int batch_tex = -1;
static int batch_blend = -1;
static int batch_target = -1;

// Mirror of hmm draw-state ctx (offsets +0x13ec..0x13fc, +0x1410..0x1420)
static int m_posx = 0, m_posy = 0;
static int m_rx = 0, m_ry = 0, m_rw = 0, m_rh = 0;
static float m_scx = 256.0f, m_scy = 256.0f;
static unsigned m_ang = 0;
static int m_primTex = 0;
static GLuint whiteTex = 0;

void ewdx_lut_init(void) {
    // 256 steps per full rotation (angle idx &= 0xff in FUN_10002460)
    for (int i = 0; i < 256; i++) {
        float a = (float)i * (2.0f * 3.14159265358979f / 256.0f);
        ewdx_sinLut[i] = sinf(a);
        ewdx_cosLut[i] = cosf(a);
    }
}

int ewdx_pos(int x, int y) { m_posx = x; m_posy = y; return -1; }
int ewdx_rect(int sx, int sy, int sw, int sh) {
    m_rx = sx; m_ry = sy; m_rw = sw; m_rh = sh; return -1;
}
int ewdx_scale(int sx, int sy, int angle) {
    m_scx = (float)sx; m_scy = (float)sy;
    m_ang = ((unsigned)angle) & 0xff;   // FUN_10002460 masks to 8 bits
    return -1;
}
int ewdx_texture(int id) { m_primTex = id; return -1; }

static void bind_run(int tex) {
    // (re)bind program attribs + texture for the current run
    glUseProgram(ewdx.prog);
    glBindBuffer(GL_ARRAY_BUFFER, ewdx.vbo);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(ewdx.u_tex, 0);
    glEnableVertexAttribArray(ewdx.a_pos);
    glEnableVertexAttribArray(ewdx.a_uv);
    glEnableVertexAttribArray(ewdx.a_col);
    glVertexAttribPointer(ewdx.a_pos, 2, GL_FLOAT, GL_FALSE,
                          VERT_FLOATS * 4, (const void *)0);
    glVertexAttribPointer(ewdx.a_uv, 2, GL_FLOAT, GL_FALSE,
                          VERT_FLOATS * 4, (const void *)(2 * 4));
    glVertexAttribPointer(ewdx.a_col, 4, GL_FLOAT, GL_FALSE,
                          VERT_FLOATS * 4, (const void *)(4 * 4));
}

void ewdx_flush(void) {
    if (batch_quads == 0 || !ewdx.prog) return;
    bind_run(batch_tex);
    glBufferData(GL_ARRAY_BUFFER, batch_quads * 6 * sizeof(EwdxVert),
                 batch, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, batch_quads * 6);
    batch_quads = 0;
}

static void run_check(int tex) {
    // Flush when texture, blend or target changes (state groups like D3D).
    // Blend is applied immediately by ewdx_apply_blend; just track it.
    if (batch_quads > 0 &&
        (tex != batch_tex || ewdx.st.blend != batch_blend ||
         ewdx.target != batch_target)) {
        ewdx_flush();
    }
    if (batch_quads == 0) {
        batch_tex = tex;
        batch_blend = ewdx.st.blend;
        batch_target = ewdx.target;
    }
    if (batch_quads >= EWDX_BATCH_QUADS) ewdx_flush();
}

// Emit one textured quad, replicating FUN_10001fa0 with flags=0.
// Audited against analysis/decomp_inner.txt (FUN_10001fa0 L89-222):
//   guard id<0x80 + tex!=0 (L89-92) ......... ewdx_copy_flags early-out
//   src-rect upper clip vs tex bounds (L99-105, no lower clip) ... rw/rh clamp
//   scale = s*INV256, or s/rect when flag&2 (L106-113) ... scx/scy at call site
//   center flag&1 (L115-118) ................ dx/dy adjust at call site
//   UV normalize + V-flip on ctx+0x90 (L119-128) ... u0/v0/u1/v1 (+vflip)
//   u/v mirror bits 8/0x10 (L130-139) ....... uflip/vflip2
//   per-vert: sub pivot, LUT rotate (0xeec=sin,0xfec=cos, idx=0x13fc&0xff),
//     add pivot, sub dest, scale, add dest-0.5 (L151-178) ... Xr/Yr/Xd/Yd/Xs/Ys
//   DrawPrimitive(6,2)=TRIANGLEFAN quad .... our 2-triangle expansion
// Only the final NDC map is ours (D3D->GL): (X+0.5)/W*2-1, which reproduces
// the D3D half-pixel convention by construction. 8.8 scale (256=1.0x, game
// passes 256/512) and 8-bit angle come straight from FUN_10002460 (L45-52).
static void emit_quad(GLuint tex, int texW, int texH, int vflip,
                      float dx, float dy, float dw, float dh,
                      float rx, float ry, float rw, float rh,
                      float scx, float scy, unsigned ang,
                      float cr, float cg, float cb, float ca,
                       int uflip, int vflip2) {
    (void)tex;  // bound by the caller (run_check/batch_tex); geometry only here
    // upper clip to texture bounds (verbatim; no lower clip in orig)
    if (rx + rw > texW) rw = (float)texW - rx;
    if (ry + rh > texH) rh = (float)texH - ry;
    if (rw <= 0 || rh <= 0) return;

    float c = ewdx_cosLut[ang & 0xff];
    float s = ewdx_sinLut[ang & 0xff];
    float pivx = dx + dw * K_HALF;
    float pivy = dy + dh * K_HALF;

    float cx[4] = { dx, dx + dw, dx + dw, dx };
    float cy[4] = { dy, dy, dy + dh, dy + dh };
    float du[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    float dv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

    int tgtW = (ewdx.target == 0) ? ewdx.scr_w : ewdx.buf[ewdx.target].w;
    int tgtH = (ewdx.target == 0) ? ewdx.scr_h : ewdx.buf[ewdx.target].h;
    if (tgtW <= 0) tgtW = ewdx.scr_w;
    if (tgtH <= 0) tgtH = ewdx.scr_h;

    float u0 = rx / texW, v0 = ry / texH;
    float u1 = (rx + rw) / texW, v1 = (ry + rh) / texH;
    if (vflip) { v0 = K_ONE - v0; v1 = K_ONE - v1; }

    EwdxVert *v = &batch[batch_quads * 6];
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    for (int k = 0; k < 6; k++) {
        int i = idx[k];
        float X = cx[i] - pivx, Y = cy[i] - pivy;
        float Xr = X * c - Y * s, Yr = X * s + Y * c;
        float Xd = Xr + pivx - dx, Yd = Yr + pivy - dy;
        float Xs = Xd * scx + dx - K_HALF;
        float Ys = Yd * scy + dy - K_HALF;
        float uu = (du[i] == 0.0f) ? u0 : u1;
        float vv = (dv[i] == 0.0f) ? v0 : v1;
        if (uflip) uu = u0 + u1 - uu;
        if (vflip2) vv = v0 + v1 - vv;
        v[k].x = (Xs + K_HALF) / tgtW * 2.0f - K_ONE;
        v[k].y = K_ONE - (Ys + K_HALF) / tgtH * 2.0f;
        v[k].u = uu; v[k].v = vv;
        v[k].r = cr; v[k].g = cg; v[k].b = cb; v[k].a = ca;
    }
    batch_quads++;
}

int ewdx_copy_flags(int id, int flags) {
    static int first = 1;
    if (id < 0 || id >= EWDX_MAX_BUFFERS) return 0;
    EwdxBuffer *t = &ewdx.buf[id];
    if (!t->valid || !t->tex) return 0;  // orig draws nothing when slot empty
    float dx = (float)m_posx, dy = (float)m_posy;
    float dw = (float)m_rw, dh = (float)m_rh;
    if (flags & 1) { dx -= dw * K_HALF; dy -= dh * K_HALF; }  // centered
    float scx = (flags & 2) ? (m_scx / (dw > 0 ? dw : 1)) : (m_scx * (1.0f / 256.0f));
    float scy = (flags & 2) ? (m_scy / (dh > 0 ? dh : 1)) : (m_scy * (1.0f / 256.0f));
    run_check((int)t->tex);
    emit_quad(t->tex, t->w, t->h, t->vflip, dx, dy, dw, dh,
              (float)m_rx, (float)m_ry, dw, dh, scx, scy, m_ang,
              ewdx.st.r / 255.0f, ewdx.st.g / 255.0f,
              ewdx.st.b / 255.0f, ewdx.st.a / 255.0f,
              (flags & 8) != 0, (flags & 0x10) != 0);
    if (first) {
        first = 0;
        ewdx_boot_journal("first copy queued");
    }
    if (batch_quads >= EWDX_BATCH_QUADS) ewdx_flush();
    return -1;
}

int ewdx_copy(int id) { return ewdx_copy_flags(id, 0); }  // game always passes flags=0

int ewdx_loadmemory(const void *bmp, int size, int slot) {
    // D3DX parity: force 32-bit RGBA + colorkey opaque-black -> alpha 0.
    static int first = 1;
    if (!bmp || size <= 0 || slot < 0 || slot >= EWDX_MAX_BUFFERS) return 0;
    ewdx_flush();  // queued quads may sample this slot's old pixels
    SDL_RWops *rw = SDL_RWFromConstMem(bmp, size);
    if (!rw) return 0;
    SDL_Surface *sf = SDL_LoadBMP_RW(rw, 1);
    if (!sf) return 0;
    SDL_Surface *cv = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(sf);
    if (!cv) return 0;
    // ABGR8888 bytes on LE = R,G,B,A order in memory. Flip rows (GL v=0 is
    // bottom; D3DX BMP load presents v=0 as top) + apply colorkey.
    int W = cv->w, H = cv->h;
    int pitch = cv->pitch;
    // Simple correct approach: build a fresh flipped+keyed buffer.
    uint8_t *out = (uint8_t *)SDL_malloc(W * H * 4);
    if (!out) { SDL_FreeSurface(cv); return 0; }
    int bpp = cv->format->BytesPerPixel;
    uint8_t *base = (uint8_t *)cv->pixels;
    for (int y = 0; y < H; y++) {
        uint8_t *srow = base + (H - 1 - y) * pitch;
        uint8_t *drow = out + y * W * 4;
        for (int x = 0; x < W; x++) {
            uint8_t r, g, b, a;
            if (bpp == 4) {
                uint32_t v;
                memcpy(&v, srow + x * 4, 4);
                SDL_GetRGBA(v, cv->format, &r, &g, &b, &a);
            } else {  // 24-bit (all game BMPs)
                uint8_t *p = srow + x * 3;
                b = p[0]; g = p[1]; r = p[2]; a = 255;
            }
            if (r == COLORKEY_R && g == COLORKEY_G && b == COLORKEY_B) a = 0;
            drow[x*4+0] = r; drow[x*4+1] = g; drow[x*4+2] = b; drow[x*4+3] = a;
        }
    }
    SDL_FreeSurface(cv);
    EwdxBuffer *t = &ewdx.buf[slot];
    if (t->valid && t->tex) {
        if (t->w != W || t->h != H) {
            glDeleteTextures(1, &t->tex);
            t->tex = 0;
        }
        if (t->fbo) { glDeleteFramebuffers(1, &t->fbo); t->fbo = 0; }
    }
    if (!t->tex) glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    SDL_free(out);
    t->w = W; t->h = H; t->valid = 1;
    t->vflip = 0;  // FUN_10001b30 clears +0x90 on load
    if (first) {
        char msg[96];
        first = 0;
        snprintf(msg, sizeof(msg), "first texture up (slot %d, %dx%d)", slot, W, H);
        ewdx_boot_journal(msg);
    }
    return -1;
}

// --- primitive path (effects only; immediate draw, same program) ---
static EwdxVert prims[64];
static int prim_n = 0;

int ewdx_createprim(int n) { (void)n; prim_n = 0; return -1; }

int ewdx_addprim(void) {
    if (prim_n >= 64) return 0;
    EwdxVert *v = &prims[prim_n++];
    int tgtW = (ewdx.target == 0) ? ewdx.scr_w : ewdx.buf[ewdx.target].w;
    int tgtH = (ewdx.target == 0) ? ewdx.scr_h : ewdx.buf[ewdx.target].h;
    v->x = ((float)m_posx) / tgtW * 2.0f - K_ONE;
    v->y = K_ONE - ((float)m_posy) / tgtH * 2.0f;
    v->u = 0.0f; v->v = 0.0f;
    v->r = ewdx.st.r / 255.0f; v->g = ewdx.st.g / 255.0f;
    v->b = ewdx.st.b / 255.0f; v->a = ewdx.st.a / 255.0f;
    return -1;
}

int ewdx_drawprim(void) {
    if (prim_n < 3 || m_primTex < 0 || m_primTex >= EWDX_MAX_BUFFERS) { prim_n = 0; return 0; }
    EwdxBuffer *t = &ewdx.buf[m_primTex];
    if (!t->valid) { prim_n = 0; return 0; }
    ewdx_flush();
    bind_run((int)t->tex);
    glBufferData(GL_ARRAY_BUFFER, prim_n * sizeof(EwdxVert), prims, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, prim_n);  // orig uses fan (D3DPT_TRIANGLEFAN=6)
    prim_n = 0;
    return -1;
}

void ewdx_immediate_quad(unsigned int tex,
                         float nx0, float ny0, float nx1, float ny1,
                         float u0, float v0, float u1, float v1,
                         float cr, float cg, float cb, float ca) {
    // Same program/attrib layout as the batcher; bypasses the queue.
    EwdxVert v[6];
    bind_run((GLuint)tex);
    v[0].x = nx0; v[0].y = ny0; v[0].u = u0; v[0].v = v0;
    v[1].x = nx1; v[1].y = ny0; v[1].u = u1; v[1].v = v0;
    v[2].x = nx1; v[2].y = ny1; v[2].u = u1; v[2].v = v1;
    v[3].x = nx0; v[3].y = ny0; v[3].u = u0; v[3].v = v0;
    v[4].x = nx1; v[4].y = ny1; v[4].u = u1; v[4].v = v1;
    v[5].x = nx0; v[5].y = ny1; v[5].u = u0; v[5].v = v1;
    {
        int k;
        for (k = 0; k < 6; k++) {
            v[k].r = cr; v[k].g = cg; v[k].b = cb; v[k].a = ca;
        }
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int ewdx_font(const char *name, int size) {
    return ewdx_text_font(name, size);
}

int ewdx_drawtext(const char *s, int x, int y) {
    return ewdx_text_draw(s, x, y);
}

int ewdx_line(int x1, int y1, int x2, int y2) {
    // 1px perpendicular quad through the shared batcher (white texture).
    if (!whiteTex) {
        uint8_t wht[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &whiteTex);
        glBindTexture(GL_TEXTURE_2D, whiteTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, wht);
    }
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return -1;
    float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;
    int tgtW = (ewdx.target == 0) ? ewdx.scr_w : ewdx.buf[ewdx.target].w;
    int tgtH = (ewdx.target == 0) ? ewdx.scr_h : ewdx.buf[ewdx.target].h;
    float px[4] = { x1 + nx, x2 + nx, x2 - nx, x1 - nx };
    float py[4] = { y1 + ny, y2 + ny, y2 - ny, y1 - ny };
    run_check((int)whiteTex);
    EwdxVert *v = &batch[batch_quads * 6];
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    float cr = ewdx.st.r / 255.0f, cg = ewdx.st.g / 255.0f;
    float cb = ewdx.st.b / 255.0f, ca = ewdx.st.a / 255.0f;
    for (int k = 0; k < 6; k++) {
        int i = idx[k];
        v[k].x = px[i] / tgtW * 2.0f - K_ONE;
        v[k].y = K_ONE - py[i] / tgtH * 2.0f;
        v[k].u = 0.0f; v[k].v = 0.0f;
        v[k].r = cr; v[k].g = cg; v[k].b = cb; v[k].a = ca;
    }
    batch_quads++;
    if (batch_quads >= EWDX_BATCH_QUADS) ewdx_flush();
    return -1;
}
