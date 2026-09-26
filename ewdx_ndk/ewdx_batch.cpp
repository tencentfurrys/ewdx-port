// ewdx_batch.cpp - step (c) quad batcher, verbatim port of FUN_10001fa0 math
#include "ewdx_batch.h"
#include "ewdx_text.h"
#include "ewdx_boot.h"
#include <SDL.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// PNG decode for the game's PNGs-renamed-to-.bmp (title1.bmp, obj_sp.bmp);
// mirrors the BMP tail of ewdx_loadmemory (flip + black colorkey).
#define STBI_NO_STDIO
#include "thirdparty/stb_image.h"
#include "ewdx_log.h"

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
    // D3D grouped draws by state at DrawPrimitive time, so every quad drawn
    // after a DGBLENDMODE must come out with the NEW factors: flush the old
    // group, then record the CURRENT state for the new group. (The previous
    // version only recorded blend when the batch was empty, so a
    // DGBLENDMODE on a continuing texture kept the OLD factors alive —
    // verbatim FUN_10001fa0 parity bug, e.g. title-logo white-flash drew
    // with stale additive factors.)
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
                       int uflip, int vflip2, int ctr_anchor) {
    (void)tex;  // bound by the caller (run_check/batch_tex); geometry only here
    // upper clip to texture bounds (verbatim; no lower clip in orig)
    if (rx + rw > texW) rw = (float)texW - rx;
    if (ry + rh > texH) rh = (float)texH - ry;
    if (rw <= 0 || rh <= 0) return;

    float c = ewdx_cosLut[ang & 0xff];
    float s = ewdx_sinLut[ang & 0xff];
    // FUN_10001fa0 (bVar13 flags): both branches rotate around the rect
    // CENTER (pivx, pivy). v22 (see the ctr_anchor branch below +
    // analysis/session-2026-09-22-v22-head-door-pov.md): the flag&4 anchor
    // is (pivx-0.5, pivy-0.5) — verbatim decompile. The plain (&4==0)
    // branch anchors at (dx-0.5, dy-0.5) — verified numerically against
    // the decompile by analysis/v22_pivot_verify.py.
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
        float Xs, Ys;
        if (ctr_anchor) {
            // flag&4 branch, now VERBATIM from the Ghidra decompile of
            // hmm.dll FUN_10001fa0 (analysis/decomp_inner.txt): X,Y are
            // pivot-relative, SCALE about the rect center, ROTATE, anchor
            // (pivx-0.5, pivy-0.5). At identity this reduces to cx-0.5 /
            // cy-0.5 — the same placement as the plain branch — and grows
            // the part around its own center when scaled/rotated.
            // History: v19 anchored x at dx-0.5 with a +dw/2 pre-offset
            // (error dw/2*(scale-1) + rotation error = the "joints off"
            // reports); v20/v21 fixed the scale pivot but dropped the dw/2
            // anchor compensation (constant -dw/2 shift = "head off / door
            // floating" on device). This is the decompile-exact form.
            float Xsc = X * scx, Ysc = Y * scy;
            Xs = Xsc * c - Ysc * s + (pivx - K_HALF);
            Ys = Xsc * s + Ysc * c + (pivy - K_HALF);
        } else {
            // FUN_10001fa0 &4==0 branch, verbatim (decomp_inner.txt):
            // pre-add the pivot pair, scale, anchor (dx-0.5, dy-0.5) —
            // i.e. plain quads grow down-right from the TOP-LEFT corner.
            // (Verified numerically against the decompile: the original
            // port form was already exact here — do not "fix" it.)
            float Xd = Xr + pivx - dx, Yd = Yr + pivy - dy;
            Xs = Xd * scx + dx - K_HALF;
            Ys = Yd * scy + dy - K_HALF;
        }
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

#ifdef EWDX_MAW_JOURNAL
// Session E POV diagnostics: journal every DGGCOPY into/out of buffer 5 (the
// stomach/porthole composite) with color+blend, plus a pixel readback of the
// 80x80 region when the game copies buffer 5 onto the scene (id=5, flag&1).
// One gameplay run answers WHICH stage of the porthole pipeline is empty:
//   - view_mot interior draws missing / alpha tiny  -> VM-side (script state)
//   - draws present + readback black               -> render-side (blend/mask)
// Costs one glReadPixels per maw composite (rare event); journal lines are
// throttled by the boot journal itself. OFF for normal builds.
#define EWDX_MAW_BUF 5
#endif

#ifdef EWDX_MAW_DUMP
// v25.2 diag: dump buffers 2/5/6 as 24-bit BMPs into Downloads during the
// POV composite (one dump set per app run, at the first buffer-5->scene
// copy). 24bpp (RGB, no alpha) shows every black plate the game would key
// or blend; alpha truth comes from the [maw] readback journal line. BMP
// rows are bottom-up: row 0 in memory = top row of the GL texture.
static void ewdx_bmp_dump(const char *name, unsigned char *rgba, int w, int h,
                          int pitch) {
    int rowBytes = w * 3, pad = (4 - (rowBytes & 3)) & 3;
    unsigned fsz = 54 + (unsigned)(rowBytes + pad) * h;
    unsigned char *bmp = (unsigned char *)calloc(1, fsz);
    if (!bmp) return;
    unsigned char *hdr = bmp;
    hdr[0] = 'B'; hdr[1] = 'M';
    { unsigned v = fsz;             memcpy(hdr + 2, &v, 4); }
    { unsigned v = 54;              memcpy(hdr + 10, &v, 4); }
    { unsigned v = 40;              memcpy(hdr + 14, &v, 4); }
    { int v = w;                    memcpy(hdr + 18, &v, 4); }
    { int v = h;                    memcpy(hdr + 22, &v, 4); }
    { unsigned short v = 1;         memcpy(hdr + 26, &v, 2); }
    { unsigned short v = 24;        memcpy(hdr + 28, &v, 2); }
    { unsigned v = 0;               memcpy(hdr + 30, &v, 4); } // BI_RGB
    { unsigned v = (unsigned)(rowBytes + pad) * h; memcpy(hdr + 34, &v, 4); }
    for (int y = 0; y < h; y++) {
        unsigned char *dst = bmp + 54 + (unsigned)(rowBytes + pad) * y;
        // GL row 0 = BMP BOTTOM row => read the texture upside down.
        unsigned char *src = rgba + (size_t)(h - 1 - y) * pitch;
        for (int x = 0; x < w; x++) {
            dst[x * 3 + 0] = src[x * 4 + 2];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 0];
        }
    }
    ewdx_boot_dump_binary(name, bmp, fsz);
    free(bmp);
}

// v25.4: POV EPISODE tracking. v25.3 dumped exactly once per app run, so the
// FIRST POV of a hours-long session owned the dumps and every later episode
// (the one the owner films/reports) had no diagnostics. An episode = POV
// activity (id-5 copies / wide buf-6 prim batches) separated by >=
// EWDX_MAW_EPISODE_GAP presents of no activity. Each episode re-arms BOTH the
// composite-time dump (buffers 6/2/5) and the present-time dump (buffers 1/4).
#define EWDX_MAW_EPISODE_GAP 120        // ~2 s of presents with no POV activity
static int mawSeen = 0;                 // active episode has fresh POV content
static unsigned maw_episode = 0;
static unsigned maw_presentTick = 0;    // bumped once per present
static unsigned maw_lastCopyTick = 0;   // last POV-activity present tick

static void ewdx_maw_dump_buffers(void) {
    static unsigned last_ep = (unsigned)-1;
    char name[64], msg[128];
    if (last_ep == maw_episode) return;
    last_ep = maw_episode;
    ewdx_boot_journal("[dump] POV composite detected: dumping buffers 6/2/5");
    ewdx_flush();
    GLint fbo = 0, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    for (int id = 6; id >= 2; id--) {
        EwdxBuffer *b = (id < EWDX_MAX_BUFFERS) ? &ewdx.buf[id] : NULL;
        if (b == NULL || !b->valid || !b->tex || !b->fbo) {
            snprintf(msg, sizeof(msg), "[dump] buf%d invalid (valid=%d tex=%u)",
                     id, b ? b->valid : -1, b ? (unsigned)b->tex : 0);
            ewdx_boot_journal(msg);
            continue;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, b->fbo);
        glViewport(0, 0, b->w, b->h);
        unsigned char *px = (unsigned char *)malloc((size_t)b->w * b->h * 4);
        if (px == NULL) continue;
        glReadPixels(0, 0, b->w, b->h, GL_RGBA, GL_UNSIGNED_BYTE, px);
        snprintf(name, sizeof(name), "ewdx-dump-buf%d.bmp", id);
        ewdx_bmp_dump(name, px, b->w, b->h, b->w * 4);
        free(px);
        snprintf(msg, sizeof(msg), "[dump] buf%d %dx%d done", id, b->w, b->h);
        ewdx_boot_journal(msg);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}
#define EWDX_MAW_DUMP_HOOK() ewdx_maw_dump_buffers()
#else
#define EWDX_MAW_DUMP_HOOK() ((void)0)
#endif  // EWDX_MAW_DUMP

#ifdef EWDX_MAW_DUMP
// Present-path scene capture: ~3 frames after the first POV trigger, dump the
// final scene FBO (buffer 1, 640x480) — the composite AFTER the copy of buffer
// 5 and after everything else the frame stacks on top. Explains the visible
// result even if the composite-time dumps race the staging.
void ewdx_maw_note_seen(void) { mawSeen = 1; }
int ewdx_maw_present_dump(void) {
    static unsigned done_ep = (unsigned)-1;
    static int pres = 0;
    maw_presentTick++;
    if (!mawSeen) return 0;
    if (maw_presentTick - maw_lastCopyTick > EWDX_MAW_EPISODE_GAP) {
        mawSeen = 0;   // episode ended without a scene capture
        return 0;
    }
    if (done_ep == maw_episode) return 0;
    if (++pres < 3) return 0;
    pres = 0;
    done_ep = maw_episode;
    mawSeen = 0;       // re-arm for the next episode
    ewdx_flush();
    GLint fbo = 0, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    // Buffers 1 (scene) and 4 (final darkened frame = what the player sees;
    // the script's end-of-frame chain is DGCLEAR(4,opaque black) +
    // DGGCOPY 1->4 with the 255-dark multiply + DGGCOPY 4->screen).
    for (int id = 1; id <= 4; id += 3) {
        EwdxBuffer *b = &ewdx.buf[id];
        if (!b->valid || !b->tex || !b->fbo) {
            char msg[64];
            snprintf(msg, sizeof(msg), "[dump] present: buf%d invalid", id);
            ewdx_boot_journal(msg);
            continue;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, b->fbo);
        glViewport(0, 0, b->w, b->h);
        unsigned char *px = (unsigned char *)malloc((size_t)b->w * b->h * 4);
        if (px) {
            glReadPixels(0, 0, b->w, b->h, GL_RGBA, GL_UNSIGNED_BYTE, px);
            char name[32], msg[96];
            snprintf(name, sizeof(name), "ewdx-dump-buf%d.bmp", id);
            ewdx_bmp_dump(name, px, b->w, b->h, b->w * 4);
            free(px);
            snprintf(msg, sizeof(msg), "[dump] buf%d %dx%d done (present)", id, b->w, b->h);
            ewdx_boot_journal(msg);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    return 1;
}
#else
void ewdx_maw_note_seen(void) { }
int ewdx_maw_present_dump(void) { return 0; }
#endif

int ewdx_copy_flags(int id, int flags) {
    static int first = 1;
    if (id < 0 || id >= EWDX_MAX_BUFFERS) return 0;
    EwdxBuffer *t = &ewdx.buf[id];
    if (!t->valid || !t->tex) return 0;  // orig draws nothing when slot empty
    float dx = (float)m_posx, dy = (float)m_posy;
    float dw = (float)m_rw, dh = (float)m_rh;
    if (flags & 1) { dx -= dw * K_HALF; dy -= dh * K_HALF; }  // centered
#ifdef EWDX_MAW_JOURNAL
    {
        static unsigned seq = 0;
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "[maw] #%u id=%d f=%#x dst=(%d,%d %dx%d) sc=(%.2f,%.2f) ang=%u col=(%d,%d,%d,%d) blend=%d tgt=%d",
                 ++seq, id, (unsigned)flags, m_posx, m_posy, m_rw, m_rh,
                 m_scx / 256.0f, m_scy / 256.0f, (unsigned)m_ang,
                 ewdx.st.r, ewdx.st.g, ewdx.st.b, ewdx.st.a,
                 ewdx.st.blend, ewdx.target);
        ewdx_boot_journal(msg);
        if (id == EWDX_MAW_BUF) {
            // v25.4 episode logic: POV activity after a gap = new episode
            // (re-arms dumps). Then fire the composite dump BEFORE the flush
            // so buffers are captured as the composite begins.
            if (maw_presentTick - maw_lastCopyTick > EWDX_MAW_EPISODE_GAP)
                maw_episode++;
            maw_lastCopyTick = maw_presentTick;
            mawSeen = 1;
            EWDX_MAW_DUMP_HOOK();
            // readback the porthole source AFTER this draw lands: force-flush,
            // bind buffer 5's FBO, read the (rx,ry,rw,rh) rect as RGBA.
            ewdx_flush();
            GLint fbo = 0, vp[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            glGetIntegerv(GL_VIEWPORT, vp);
            glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
            glViewport(0, 0, t->w, t->h);
            int rx = m_rx, ry = m_ry, rw = m_rw > 0 ? m_rw : 1, rh2 = m_rh > 0 ? m_rh : 1;
            if (rx + rw > t->w) rw = t->w - rx;
            if (ry + rh2 > t->h) rh2 = t->h - ry;
            // v25.4: glReadPixels is GL bottom-origin but (rx,ry) is the
            // script's top-origin rect — mirror Y or the readback samples the
            // WRONG rows (v25.3 logs: "avgRGB=765" = the white clear BELOW the
            // porthole, not the composite itself). The BMP dumps already
            // handle this; only this rect readback was mirrored wrong.
            int ry_gl = t->h - ry - rh2;
            if (ry_gl < 0) { rh2 += ry_gl; ry_gl = 0; }
            ry = ry_gl;
            if (rw > 0 && rh2 > 0) {
                unsigned char *px = (unsigned char *)malloc((size_t)rw * rh2 * 4);
                if (px) {
                    glReadPixels(rx, ry, rw, rh2, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    long sum = 0, npx = (long)rw * rh2, nalpha = 0;
                    for (long i = 0; i < npx * 4; i += 4) {
                        sum += px[i] + px[i + 1] + px[i + 2];
                        if (px[i + 3] > 16) nalpha++;    // GL RGBA: A is byte 3
                    }
                    long rgb = sum / npx;                 // 0..765 avg per px
                    int pct = (int)(100 * nalpha / npx);
                    snprintf(msg, sizeof(msg),
                             "[maw] buf5 readback %dx%d@(%d,%d glY=%d): avgRGB=%ld nontransparent=%d%%",
                             rw, rh2, rx, m_ry, ry, rgb, pct);
                    ewdx_boot_journal(msg);
                    free(px);
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(vp[0], vp[1], vp[2], vp[3]);
        }
    }
#endif
#ifdef EWDX_DGCOPY_JOURNAL
    // v17 diagnostics: journal every DGGCOPY the script issues. The v16
    // failure frames (system.bmp atlas drawn as one giant quad) need the
    // exact (id, dst, src-rect, scale) stream around the stage load to pin
    // whether the quad comes from the script (garbage dio/pic values) or
    // from a bad present path. Off by default; enabled per-build.
    {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "[dgcopy] id=%d f=%#x dst=(%d,%d %dx%d) src=(%d,%d) "
                 "sc=(%.2f,%.2f) ang=%u col=(%d,%d,%d,%d)",
                 id, (unsigned)flags, m_posx, m_posy, m_rw, m_rh, m_rx, m_ry,
                 m_scx / 256.0f, m_scy / 256.0f, (unsigned)m_ang,
                 ewdx.st.r, ewdx.st.g, ewdx.st.b, ewdx.st.a);
        ewdx_boot_journal(msg);
    }
#endif
    float scx = (flags & 2) ? (m_scx / (dw > 0 ? dw : 1)) : (m_scx * (1.0f / 256.0f));
    float scy = (flags & 2) ? (m_scy / (dh > 0 ? dh : 1)) : (m_scy * (1.0f / 256.0f));
    run_check((int)t->tex);
    emit_quad(t->tex, t->w, t->h, t->vflip, dx, dy, dw, dh,
              (float)m_rx, (float)m_ry, dw, dh, scx, scy, m_ang,
              ewdx.st.r / 255.0f, ewdx.st.g / 255.0f,
              ewdx.st.b / 255.0f, ewdx.st.a / 255.0f,
              (flags & 8) != 0, (flags & 0x10) != 0, (flags & 4) != 0);
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
    // Magic sniff: the game's data ships a few PNGs renamed to .bmp (title1,
    // obj_sp) and the original hmm.dll sniffs+decodes both. SDL only does BMP.
    if (size >= 8 &&
        ((const uint8_t *)bmp)[0] == 0x89 && ((const uint8_t *)bmp)[1] == 0x50 &&
        ((const uint8_t *)bmp)[2] == 0x4E && ((const uint8_t *)bmp)[3] == 0x47) {
        return ewdx_loadmemory_png(bmp, size, slot);
    }
    SDL_RWops *rw = SDL_RWFromConstMem(bmp, size);
    if (!rw) return 0;
    SDL_Surface *sf = SDL_LoadBMP_RW(rw, 1);
    if (!sf) return 0;
    SDL_Surface *cv = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(sf);
    if (!cv) return 0;
    // ABGR8888 bytes on LE = R,G,B,A order in memory. Rows are uploaded in
    // FILE ORDER (no flip): with the port's NDC mapping, texture row 0 already
    // lands at the TOP of the screen — flipping here put the whole game upside
    // down (this matches the uncommitted "flipfix" verified in the working
    // v7 binary). Then apply the black colorkey.
    int W = cv->w, H = cv->h;
    int pitch = cv->pitch;
    // Simple correct approach: build a fresh keyed buffer.
    uint8_t *out = (uint8_t *)SDL_malloc(W * H * 4);
    if (!out) { SDL_FreeSurface(cv); return 0; }
    int bpp = cv->format->BytesPerPixel;
    uint8_t *base = (uint8_t *)cv->pixels;
    for (int y = 0; y < H; y++) {
        uint8_t *srow = base + (size_t)y * pitch;
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
    // v25.4: POINT — DGLOADMEMORY textures must match D3D9's default point
    // sampling (hmm.dll sets no filters). GL_LINEAR blurred the atlas
    // uploads; see ewdx_buffer for the full note.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    SDL_free(out);
    t->w = W; t->h = H; t->valid = 1;
    t->vflip = 0;  // FUN_10001b30 clears +0x90 on load
    if (first) {
        first = 0;
        ewdx_boot_journal("first texture up");
    }
#ifdef EWDX_DGCOPY_JOURNAL
    // v17 diagnostics: texture-slot timeline (pairs with the [dgcopy] stream).
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "[loadmem] slot=%d %dx%d (%d bytes)",
                 slot, W, H, size);
        ewdx_boot_journal(msg);
    }
#endif
    return -1;
}

int ewdx_loadmemory_png(const void *png, int size, int slot) {
    int W, H, ch;
    uint8_t *px = (uint8_t *)stbi_load_from_memory(
        (const stbi_uc *)png, size, &W, &H, &ch, 4);
    if (px == NULL) {
        EWDX_LOGE("loadmem: PNG decode failed (slot %d, %d bytes): %s",
                  slot, size, stbi_failure_reason());
        return 0;
    }
    // stb decodes top-down; upload rows in decode order (no flip) — with the
    // port's NDC mapping row 0 lands at the top of the screen. Flipping here
    // put PNGs upside down (uncommitted v7 "flipfix" parity).
    uint8_t *out = (uint8_t *)SDL_malloc((size_t)W * H * 4);
    if (out == NULL) { stbi_image_free(px); return 0; }
    for (int y = 0; y < H; y++) {
        const uint8_t *srow = px + (size_t)y * W * 4;
        uint8_t *drow = out + (size_t)y * W * 4;
        for (int x = 0; x < W; x++) {
            uint8_t r = srow[x*4+0], g = srow[x*4+1], b = srow[x*4+2];
            uint8_t a = srow[x*4+3];
            if (a == 255 && r == COLORKEY_R && g == COLORKEY_G && b == COLORKEY_B)
                a = 0;  // black colorkey, same as BMP path
            drow[x*4+0] = r; drow[x*4+1] = g; drow[x*4+2] = b; drow[x*4+3] = a;
        }
    }
    stbi_image_free(px);
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
    // v25.4: POINT (second DGLOADMEMORY upload path, PNG-renamed variants)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    SDL_free(out);
    t->w = W; t->h = H; t->valid = 1;
    t->vflip = 0;
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "loadmem: PNG decoded (slot %d, %dx%d)", slot, W, H);
        ewdx_boot_journal(msg);
    }
    return -1;
}

// --- primitive path (effects + the stomach/mouth wobble) ---
// v25 rewrite. The old stub appended ONE degenerate vertex per
// DGADDPRIMITIVE (uv 0,0, no size) and fanned them — the stomach wobble
// blocks add one primitive PER 1px SCANLINE (repeat part(5,...) with
// DGRECT src row + DGSCALEANDANGLE = dest width in px + DGPOS row pos), so
// it produced nothing: the wobbled wall/intestine parts (part(19)==1,
// drawn FROM buffer 6) rendered empty = the residual black interior +
// missing squirm/flash after v24.1.
// hmm.dll semantics (D3D9): DGADDPRIMITIVE records a QUAD from the current
// DG state (D3DPT_TRIANGLEFAN, 4 verts/quad, primCount = quads); scale is
// flag&2-style (dest dims = raw DGSCALEANDANGLE px, not 8.8). We snapshot
// the state per call and expand to 6 verts at draw time via emit_quad.
typedef struct {
    int posx, posy, rx, ry, rw, rh;
    float scx, scy;   // raw 8.8 as set by DGSCALEANDANGLE
    unsigned ang;
    int r, g, b, a;
    unsigned flags;   // v25.1: the DGADDPRIMITIVE arg = DGGCOPY flag word
                      // (7 = center+rect-scale+ctr-anchor, +8 uflip, +0x10 vflip)
    int tgt;          // v25.3-diag: render target when the prim was ADDED.
                      // D3D9 AddPrimitive locks the destination at ADD time
                      // (verts are screen/clip-space by DrawPrimitive); the
                      // v25 rewrite expanded at DRAW time against the live
                      // target — a DGGSEL between ADD and DRAW mis-targeted
                      // every staged quad.
} EwdxPrim;
// wobble loops: one primitive per 1px scanline, up to 256 rows, and the
// pre-v25.1 flag-mismatch cap-trip reports (~2s of lag then fmax dump spam)
// tripped at 256. Headroom is free (512 B/prim; 2 MB at 4096).
#define EWDX_MAX_PRIMS 4096
static EwdxPrim prims[EWDX_MAX_PRIMS];
static int prim_n = 0;

int ewdx_createprim(int n) { (void)n; prim_n = 0; return -1; }

int ewdx_addprim(unsigned flags) {
    if (prim_n >= EWDX_MAX_PRIMS) return 0;
    EwdxPrim *q = &prims[prim_n++];
    q->posx = m_posx; q->posy = m_posy;
    q->rx = m_rx; q->ry = m_ry; q->rw = m_rw; q->rh = m_rh;
    q->scx = m_scx; q->scy = m_scy; q->ang = m_ang;
    q->r = ewdx.st.r; q->g = ewdx.st.g; q->b = ewdx.st.b; q->a = ewdx.st.a;
    q->flags = flags;
    q->tgt = ewdx.target;
#ifdef EWDX_MAW_JOURNAL
    // Journal ONLY the wide scanline prims (the wobble staging; menu tiles are
    // small and would spam thousands of lines per frame = the v18 I/O lag),
    // under a hard budget so a long run can never drown the journal.
    static int primj_budget = 200000;
    if (primj_budget > 0 && m_rw >= 120 && m_scx >= 120.0f) {
        primj_budget--;
        static unsigned pseq = 0;
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "[prim] +%u f=%#x dst=(%d,%d %dx%d) src=(%d,%d) sc=(%.2f,%.2f) ang=%u col=(%d,%d,%d,%d) tex=%d tgt=%d",
                 ++pseq, flags, m_posx, m_posy, m_rw, m_rh, m_rx, m_ry,
                 m_scx / 256.0f, m_scy / 256.0f, m_ang,
                 ewdx.st.r, ewdx.st.g, ewdx.st.b, ewdx.st.a, m_primTex, ewdx.target);
        ewdx_boot_journal(msg);
    }
#endif
#ifdef EWDX_MAW_DUMP
    // Secondary trigger: wide scanline prims staged into buffer 6 (the POV
    // wobble staging is ~130-190px rows; menu prims are small tiles). Same
    // episode logic as the id-5 copy: activity after a gap starts a new
    // episode and re-arms the dumps.
    if (ewdx.target == 6 && m_rw >= 120 && m_scx >= 120.0f) {
        if (maw_presentTick - maw_lastCopyTick > EWDX_MAW_EPISODE_GAP)
            maw_episode++;
        maw_lastCopyTick = maw_presentTick;
        mawSeen = 1;
        EWDX_MAW_DUMP_HOOK();
    }
#endif
    return -1;
}

int ewdx_drawprim(void) {
    if (prim_n < 1 || m_primTex < 0 || m_primTex >= EWDX_MAX_BUFFERS) { prim_n = 0; return 0; }
    EwdxBuffer *t = &ewdx.buf[m_primTex];
    if (!t->valid) { prim_n = 0; return 0; }
#ifdef EWDX_MAW_JOURNAL
    {
        static int drawj_budget = 40000;
        if (drawj_budget > 0) {
            drawj_budget--;
            static unsigned dseq = 0;
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "[prim] DRAW#%u n=%d tex=%d tgt=%d blend=%d",
                     ++dseq, prim_n, m_primTex, ewdx.target, ewdx.st.blend);
            ewdx_boot_journal(msg);
        }
    }
#endif
    int tgtW = (ewdx.target == 0) ? ewdx.scr_w : ewdx.buf[ewdx.target].w;
    int tgtH = (ewdx.target == 0) ? ewdx.scr_h : ewdx.buf[ewdx.target].h;
    (void)tgtW; (void)tgtH;  // emit_quad reads ewdx.target itself
    // v25.2 note: no source-remap clipping here. GL clips in clip space with
    // perspective-correct UV interpolation — the same visible result as the
    // D3D9 runtime clipper; a manual anchor remap would only mis-map the
    // mirrored prims (menu tiles use 7+8*flip).
    for (int i = 0; i < prim_n; i++) {
        EwdxPrim *q = &prims[i];
        float dw = (float)q->rw, dh = (float)q->rh;
        if (dw <= 0 || dh <= 0) continue;
        // v25.3-diag: D3D9 locks the destination at ADD time. Expand against
        // the prim's own snapshot target, not the live one (a DGGSEL between
        // DGADDPRIMITIVE and DGDRAWPRIMITIVE previously retargeted the whole
        // batch — the only behavioral change in this build).
        if (q->tgt != ewdx.target) ewdx_select(q->tgt);
        float dx = (float)q->posx, dy = (float)q->posy;
        // v25.5: FUN_10002550 verbatim (PC hmm.dll disassembly, session I):
        // flag&1 centering uses the DEST size -- raw DGSCALEANDANGLE px when
        // flag&2 (mov ecx,[esp+0x10]; fsub [esp+0x14] @0x2662), else
        // source*scale (fmul 8.8 @0x2654). v25 centered by the SOURCE rect
        // size, so the wobble scanlines (dest w = source + sinusoid) slid
        // BOTH edges symmetrically = the Android "slap"; the PC pins the
        // outer edge (pos - d/2, then scale about the source pivot) = the
        // "heartbeat" squeeze. emit_quad's pivx = dx + src_w/2 then
        // reproduces FUN_10002550's &4 pivot exactly.
        float dw_dst, dh_dst;
        if (q->flags & 2) {
            dw_dst = q->scx;                  // raw px, NOT /256
            dh_dst = q->scy;
        } else {
            dw_dst = dw * q->scx * (1.0f / 256.0f);
            dh_dst = dh * q->scy * (1.0f / 256.0f);
        }
        if (q->flags & 1) {  // centered: DGPOS is the DEST quad CENTER
            dx -= dw_dst * K_HALF;
            dy -= dh_dst * K_HALF;
        }
        // flag&2 dest semantics: dest dims = raw DGSCALEANDANGLE px
        float scx = (q->flags & 2) ? (q->scx / dw) : (q->scx * (1.0f / 256.0f));
        float scy = (q->flags & 2) ? (q->scy / dh) : (q->scy * (1.0f / 256.0f));
        run_check((int)t->tex);
        emit_quad(t->tex, t->w, t->h, t->vflip,
                  dx, dy, dw, dh,
                  (float)q->rx, (float)q->ry, dw, dh, scx, scy, q->ang,
                  q->r / 255.0f, q->g / 255.0f, q->b / 255.0f, q->a / 255.0f,
                  (q->flags & 8) != 0, (q->flags & 0x10) != 0,
                  (q->flags & 4) != 0);
        if (batch_quads >= EWDX_BATCH_QUADS) ewdx_flush();
    }
    ewdx_flush();
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
