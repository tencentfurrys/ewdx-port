// ewdx_gles.cpp - SDL2 + OpenGL ES 2.0 context, buffers, blend state
// Covers step (b): DGINIT/DGSCREEN/DGBUFFER/DGGSEL/DGCOLOR/DGCLEAR/DGREDRAW
#include "ewdx_gles.h"
#include "ewdx_batch.h"
#include "ewdx_text.h"
#include "ewdx_paths.h"
#include "ewdx_boot.h"
#include <string.h>
#ifdef __ANDROID__
#include <EGL/egl.h>
#endif

EwdxGles ewdx;

static int g_inited = 0;

static const char *VS_SRC =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_col;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "  v_uv = a_uv;\n"
    "  v_col = a_col;\n"
    "}\n";

static const char *FS_SRC =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(u_tex, v_uv) * v_col;\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}// mode -> (rgbSrc, rgbDst, aSrc, aDst) recovered from hmm.dll FUN_10001d70:
//   SetRenderState(D3DRS_SRCBLEND=0x13, X) + SetRenderState(D3DRS_DESTBLEND=0x14, Y)
//   D3DBLEND: ZERO=1 ONE=2 SRCCOLOR=3 INVSRCCOLOR=4 SRCALPHA=5 INVSRCALPHA=6
//             DESTCOLOR=9 INVDESTCOLOR=10
// D3D9 blends all four channels with color factors (INVSRCCOLOR's alpha row is
// 1-src.a); D3D9 defaults leave D3DRS_SEPARATEALPHABLENDENABLE off, and hmm.dll
// never touches SRCBLENDALPHA/DESTBLENDALPHA. GLES2 forbids color factors on
// the alpha unit, so each mode gets a separate legal alpha row (v24):
//   0 (COPY): rgb ONE/ZERO, alpha ONE/INVSRCALPHA. Alpha-respecting opaque
//     replace — the maw composite's final DGGCOPY 5,1 must NOT paint the
//     mask-carved transparent corners over the scene (ref video: scene shows
//     through the corners). Byte-identical to ONE/ZERO for opaque sources.
//   3 (SUB): rgb ZERO/INVSRCOLOR, alpha ZERO/INVSRCALPHA (D3D: 1-src.a). The
//     vore mask (4 mirrored system.bmp tiles) must carve corner ALPHA to 0 —
//     v23 evidence: glBlendFunc sent ONE_MINUS_SRC_COLOR to the alpha unit
//     (illegal in GLES2, silently ignored on MuMu's shim) so corners stayed
//     A=255 and the final copy painted the black box. Transparent (A=0)
//     mask-hole pixels leave dst alpha untouched.
//   4 (MUL): alpha row ZERO/INVSRCALPHA mirrors mode 3's D3D alpha row.
//   5 (SCR): alpha ONE/ZERO mirrors mode 0's D3D alpha row.
static const GLenum BLEND_RGB_SRC[8] = {
    GL_ONE,                // 0: SRC=ONE(2)
    GL_SRC_ALPHA,          // 1: SRC=SRCALPHA(5)
    GL_SRC_ALPHA,          // 2: SRC=SRCALPHA(5)
    GL_ZERO,               // 3: SRC=ZERO(1)
    GL_ZERO,               // 4: SRC=ZERO(1)
    GL_ONE_MINUS_DST_COLOR,// 5: SRC=INVDESTCOLOR(10)
    GL_ONE,                // 6: SRC=ONE(2)
    GL_DST_COLOR           // 7: SRC=DESTCOLOR(9)
};
static const GLenum BLEND_RGB_DST[8] = {
    GL_ZERO,                    // 0: DST=ZERO(1)
    GL_ONE_MINUS_SRC_ALPHA,     // 1: DST=INVSRCALPHA(6)
    GL_ONE,                     // 2: DST=ONE(2)
    GL_ONE_MINUS_SRC_COLOR,     // 3: DST=INVSRCCOLOR(4)
    GL_SRC_COLOR,               // 4: DST=SRCCOLOR(3)
    GL_ZERO,                    // 5: DST=ZERO(1)
    GL_ONE,                     // 6: DST=ONE(2)
    GL_ONE                      // 7: DST=ONE(2)
};
static const GLenum BLEND_A_SRC[8] = {
    GL_ONE,                     // 0: ONE's alpha row = 1 (replace)
    GL_SRC_ALPHA,               // 1: SRCALPHA's alpha row = src.a
    GL_SRC_ALPHA,               // 2: SRCALPHA's alpha row = src.a
    GL_ZERO,                    // 3: ZERO's alpha row = 0 (v24 carve fix)
    GL_ZERO,                    // 4: ZERO's alpha row = 0
    GL_ONE_MINUS_DST_ALPHA,     // 5: INVDESTCOLOR's alpha row = 1-dst.a
    GL_ONE,                     // 6: ONE's alpha row = 1
    GL_DST_ALPHA                // 7: DESTCOLOR's alpha row = dst.a
};
static const GLenum BLEND_A_DST[8] = {
    GL_ONE_MINUS_SRC_ALPHA,     // 0: INVSRCALPHA (v24: scene through carved corners)
    GL_ONE_MINUS_SRC_ALPHA,     // 1: INVSRCALPHA
    GL_ONE,                     // 2: ONE
    GL_ONE_MINUS_SRC_ALPHA,     // 3: INVSRCALPHA = 1-src.a (v24 carve fix)
    GL_SRC_ALPHA,               // 4: SRCCOLOR = src.a
    GL_ZERO,                    // 5: ZERO
    GL_ONE,                     // 6: ONE
    GL_ONE                      // 7: ONE
};

int ewdx_init(void) {
    if (g_inited && ewdx.win != NULL) return -1;  // idempotent: screen (L129)
    memset(&ewdx, 0, sizeof(ewdx));               // runs before DGINIT (L148)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) return 0;
    g_inited = 1;
    ewdx_paths_init();  // cache filesDir for save.dat/ini/data resolution
    ewdx_lut_init();  // 256-step sin/cos (FUN_10002460 angle units); batcher needs it
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    ewdx.st.scalex = ewdx.st.scaley = 256;
    ewdx.st.a = 255;
    ewdx.st.blend = EWDX_BLEND_ALPHA;
    return -1;  // HSP convention: -1 ok / 0 fail (stat-ready; see ewdx_dg.h)
}

// Viewport always matches the CURRENT render target (screen or FBO size).
// emit_quad maps NDC against these same dims, so the two must agree.
void ewdx_apply_viewport(void) {
    int w = (ewdx.target == 0) ? ewdx.scr_w : ewdx.buf[ewdx.target].w;
    int h = (ewdx.target == 0) ? ewdx.scr_h : ewdx.buf[ewdx.target].h;
    if (w <= 0) w = (ewdx.scr_w > 0) ? ewdx.scr_w : 1;
    if (h <= 0) h = (ewdx.scr_h > 0) ? ewdx.scr_h : 1;
    // Target 0 (Android): the window drawable is the full device screen while
    // game coords are scr_w x scr_h (e.g. 640x480). Draw into a centered
    // aspect-locked subrect so the whole game view fills the screen; NDC is
    // viewport-relative, so nothing else needs to change. Without this the
    // game rendered into a scr-sized corner of the display.
    if (ewdx.target == 0 && ewdx.viewport[2] > 0 && ewdx.viewport[3] > 0) {
        glViewport(ewdx.viewport[0], ewdx.viewport[1],
                   ewdx.viewport[2], ewdx.viewport[3]);
        return;
    }
    glViewport(0, 0, w, h);
}

// Real GL drawable size in pixels. CRITICAL on Android: SDL reports the
// REQUESTED window size (640x480) there, not the actual surface — the GL
// drawable is the full-screen EGL surface. Querying EGL directly is ground
// truth; without it the letterbox math saw 640x480, did nothing, and the
// game rendered into a 640x480 bottom-left corner of the display.
void ewdx_surface_px(int *dw, int *dh) {
#ifdef __ANDROID__
    EGLDisplay d = eglGetCurrentDisplay();
    EGLSurface s = eglGetCurrentSurface(EGL_DRAW);
    if (d != EGL_NO_DISPLAY && s != EGL_NO_SURFACE &&
        eglQuerySurface(d, s, EGL_WIDTH, dw) &&
        eglQuerySurface(d, s, EGL_HEIGHT, dh) && *dw > 0 && *dh > 0) {
        return;
    }
#endif
    SDL_GL_GetDrawableSize(ewdx.win, dw, dh);
}

// Recompute the target-0 letterbox subrect against the real drawable size:
// whole game view scaled to fit, centered, aspect locked.
void ewdx_apply_screen_viewport(void) {
    int dw = 0, dh = 0;
    ewdx_surface_px(&dw, &dh);
    if (dw <= 0 || dh <= 0) {
        ewdx.viewport[0] = 0; ewdx.viewport[1] = 0;
        ewdx.viewport[2] = 0; ewdx.viewport[3] = 0;
        return;
    }
    int gw = (ewdx.scr_w > 0) ? ewdx.scr_w : 640;
    int gh = (ewdx.scr_h > 0) ? ewdx.scr_h : 480;
    if (dw * gh < dh * gw) {           // drawable taller: pillarbox sides
        ewdx.viewport[2] = dw;
        ewdx.viewport[3] = dw * gh / gw;
        ewdx.viewport[0] = 0;
        ewdx.viewport[1] = (dh - ewdx.viewport[3]) / 2;
    } else {                           // drawable wider (or exact): letterbox
        ewdx.viewport[2] = dh * gw / gh;
        ewdx.viewport[3] = dh;
        ewdx.viewport[0] = (dw - ewdx.viewport[2]) / 2;
        ewdx.viewport[1] = 0;
    }
    {
        // One-shot per geometry: numeric proof in the boot journal.
        static int last_w = -1, last_h = -1;
        if (ewdx.viewport[2] != last_w || ewdx.viewport[3] != last_h) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "viewport: surface %dx%d game %dx%d -> [%d,%d %dx%d]",
                     dw, dh, gw, gh, ewdx.viewport[0], ewdx.viewport[1],
                     ewdx.viewport[2], ewdx.viewport[3]);
            ewdx_boot_journal(msg);
            last_w = ewdx.viewport[2];
            last_h = ewdx.viewport[3];
        }
    }
    ewdx_apply_viewport();
}

int ewdx_screen(int w, int h, int mode) {
    // mode: game passes 1 then 0 (probe then set); both map to windowed on Android.
    // w/h observed: 640x480 or 1280x960 (wmode_w flag).
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    (void)mode;
    ewdx_flush();  // queued quads were mapped against the old size
    if (!ewdx.win) {
        ewdx.win = SDL_CreateWindow("EchidnaWarsDX",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        if (!ewdx.win) return 0;
        ewdx.ctx = SDL_GL_CreateContext(ewdx.win);
        if (!ewdx.ctx) return 0;
        GLuint vs = compile_shader(GL_VERTEX_SHADER, VS_SRC);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FS_SRC);
        ewdx.prog = glCreateProgram();
        glAttachShader(ewdx.prog, vs);
        glAttachShader(ewdx.prog, fs);
        glLinkProgram(ewdx.prog);
        glUseProgram(ewdx.prog);
        ewdx.a_pos = glGetAttribLocation(ewdx.prog, "a_pos");
        ewdx.a_uv = glGetAttribLocation(ewdx.prog, "a_uv");
        ewdx.a_col = glGetAttribLocation(ewdx.prog, "a_col");
        ewdx.u_tex = glGetUniformLocation(ewdx.prog, "u_tex");
        glGenBuffers(1, &ewdx.vbo);
        glEnable(GL_BLEND);
        ewdx_apply_blend(EWDX_BLEND_ALPHA);
    } else {
        SDL_SetWindowSize(ewdx.win, w, h);
    }
    ewdx.scr_w = w;
    ewdx.scr_h = h;
    ewdx.viewport[0] = 0; ewdx.viewport[1] = 0;
    ewdx.viewport[2] = 0; ewdx.viewport[3] = 0;
    ewdx_apply_screen_viewport();  // target-0 letterbox + viewport for new size
    return -1;
}

static void ewdx_drop_buffer(EwdxBuffer *b) {
    if (b->tex) glDeleteTextures(1, &b->tex);
    if (b->fbo) glDeleteFramebuffers(1, &b->fbo);
    b->tex = 0; b->fbo = 0; b->valid = 0;
}

int ewdx_buffer(int id, int w, int h) {
    // Game allocates 6 fixed offscreens at boot (1:640x480 2:256x256 3:512x512
    // 4:640x480 5:320x240 6:256x256). Back them with FBO+RGBA texture.
    if (id < 0 || id >= EWDX_MAX_BUFFERS) return 0;
    ewdx_flush();  // queued quads may sample the texture/FBO being replaced
    EwdxBuffer *b = &ewdx.buf[id];
    if (b->valid) ewdx_drop_buffer(b);
    glGenTextures(1, &b->tex);
    glBindTexture(GL_TEXTURE_2D, b->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &b->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, b->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b->tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, ewdx.buf[ewdx.target].fbo);
    b->w = w; b->h = h; b->valid = 1;
    b->vflip = 1;  // render-target sampling flips V (mirrors ctx+0x90 set path)
    return -1;
}

int ewdx_select(int id) {
    if (id < 0 || id >= EWDX_MAX_BUFFERS || !ewdx.buf[id].valid) {
#ifdef EWDX_MAW_JOURNAL
        // Session E/F: a failed DGGSEL used to vanish silently — if buffer 5
        // was ever invalid at vore time, the whole porthole composite would
        // land on the WRONG target and nobody would know. Log it.
        {
            char msg[96];
            snprintf(msg, sizeof(msg), "[maw] DGGSEL %d FAILED (valid=%d)",
                     id, (id >= 0 && id < EWDX_MAX_BUFFERS) ? ewdx.buf[id].valid : -1);
            ewdx_boot_journal(msg);
        }
#endif
        if (id != 0) return 0;
    }
    if (id != ewdx.target) ewdx_flush();  // queued quads belong to the old FBO
    ewdx.target = id;
#ifdef EWDX_DGCOPY_JOURNAL
    // v17 diagnostics: journal target switches (rare events, no throttle).
    {
        static int lastj = -1;
        static unsigned nsw = 0;
        if (id != lastj) {
            char msg[80];
            lastj = id;
            nsw++;
            snprintf(msg, sizeof(msg), "[gsel] target=%d (switch #%u)", id, nsw);
            ewdx_boot_journal(msg);
        }
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, ewdx.buf[id].fbo); // slot 0 fbo==0 (default)
    ewdx_apply_viewport();
    return -1;
}

int ewdx_color(int r, int g, int b, int a) {
    // v24.1: D3D9 packs DGCOLOR args into a 32-bit D3DCOLOR (RGBA bytes), so an
    // alpha of 256 TRUNCATES to 0. The game's staging/menu clears are
    // "DGCOLOR 0,0,0,256" (15 sites) — born-transparent black on D3D — but the
    // port stored 256 and glClearColor clamped it to an OPAQUE 1.0: every
    // staging buffer (mouth-art buffer 6, menu buffer 5) shipped an opaque
    // black square through the zoom chain = the vore POV black box that
    // survived v23/v24. Mirror the hardware truncation here.
    ewdx.st.r = r & 0xff; ewdx.st.g = g & 0xff; ewdx.st.b = b & 0xff;
    ewdx.st.a = a & 0xff;
    return -1;
}

int ewdx_clear(void) {
    // D3D Clear is immediate: earlier queued draws (command order) must land first.
    ewdx_flush();
    glClearColor(ewdx.st.r / 255.0f, ewdx.st.g / 255.0f,
                 ewdx.st.b / 255.0f, ewdx.st.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return -1;
}

int ewdx_present(void) {
    static int first = 1;
    ewdx_flush();  // step (c): drain quad batcher before swap
    if (ewdx.target != 0) ewdx_select(0);
    // Letterbox bars are NOT cleared by the game (its DGCLEAR only covers the
    // 640x480 view). Clear ONLY the bar rects: a fullscreen clear here would
    // erase the frame just flushed into the game-view subrect (the v10
    // black-screen bug). Scissor rects share the viewport's bottom-origin Y.
    if (ewdx.viewport[2] > 0 && ewdx.viewport[3] > 0) {
        int dw = 0, dh = 0;
        ewdx_surface_px(&dw, &dh);
        int vx = ewdx.viewport[0], vy = ewdx.viewport[1];
        int vw = ewdx.viewport[2], vh = ewdx.viewport[3];
        if (vx > 0 || vx + vw < dw || vy > 0 || vy + vh < dh) {
            glEnable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            if (vx > 0) {       // left bar
                glScissor(0, 0, vx, dh);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            if (vx + vw < dw) { // right bar
                glScissor(vx + vw, 0, dw - vx - vw, dh);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            if (vy > 0) {       // bottom bar (GL origin)
                glScissor(0, 0, dw, vy);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            if (vy + vh < dh) { // top bar
                glScissor(0, vy + vh, dw, dh - vy - vh);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glDisable(GL_SCISSOR_TEST);
        }
    }
    SDL_GL_SwapWindow(ewdx.win);
    if (first) { first = 0; ewdx_boot_journal("first present"); }
    return -1;
}

int ewdx_apply_blend(int mode) {
    if (mode < 0 || mode > 7) mode = EWDX_BLEND_ALPHA;
    if (mode != ewdx.st.blend) ewdx_flush();  // pending quads use the old factors
    ewdx.st.blend = mode;
    // v24: separate alpha factors — D3D9 blends RGB+alpha with the same
    // color-factor rows (no SEPARATEALPHABLEND in hmm.dll), and GLES2 rejects
    // color factors on the alpha unit. glBlendFunc(BLEND_*[3]) silently broke
    // the alpha half of modes 0/3/4/5 (maw black-box bug).
    glBlendFuncSeparate(BLEND_RGB_SRC[mode], BLEND_RGB_DST[mode],
                        BLEND_A_SRC[mode], BLEND_A_DST[mode]);
    return -1;
}

int ewdx_shutdown(void) {
    ewdx_text_shutdown();  // frees string-cache textures + closes fonts (GL alive)
    for (int i = 1; i < EWDX_MAX_BUFFERS; i++) {
        if (ewdx.buf[i].valid) {
            glDeleteTextures(1, &ewdx.buf[i].tex);
            glDeleteFramebuffers(1, &ewdx.buf[i].fbo);
        }
    }
    if (ewdx.ctx) SDL_GL_DeleteContext(ewdx.ctx);
    if (ewdx.win) SDL_DestroyWindow(ewdx.win);
    memset(&ewdx, 0, sizeof(ewdx));
    g_inited = 0;
    SDL_Quit();
    return -1;
}
