// ewdx_gles.cpp - SDL2 + OpenGL ES 2.0 context, buffers, blend state
// Covers step (b): DGINIT/DGSCREEN/DGBUFFER/DGGSEL/DGCOLOR/DGCLEAR/DGREDRAW
#include "ewdx_gles.h"
#include "ewdx_batch.h"
#include "ewdx_text.h"
#include "ewdx_paths.h"
#include <string.h>

EwdxGles ewdx;

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
}

// mode -> (src, dst) recovered from hmm.dll FUN_10001d70:
//   SetRenderState(D3DRS_SRCBLEND=0x13, X) + SetRenderState(D3DRS_DESTBLEND=0x14, Y)
//   D3DBLEND: ZERO=1 ONE=2 SRCCOLOR=3 INVSRCCOLOR=4 SRCALPHA=5 INVSRCALPHA=6
//             DESTCOLOR=9 INVDESTCOLOR=10
static const GLenum BLEND_SRC[8] = {
    GL_ONE,                // 0: SRC=ONE(2)
    GL_SRC_ALPHA,          // 1: SRC=SRCALPHA(5)
    GL_SRC_ALPHA,          // 2: SRC=SRCALPHA(5)
    GL_ZERO,               // 3: SRC=ZERO(1)
    GL_ZERO,               // 4: SRC=ZERO(1)
    GL_ONE_MINUS_DST_COLOR,// 5: SRC=INVDESTCOLOR(10)
    GL_ONE,                // 6: SRC=ONE(2)
    GL_DST_COLOR           // 7: SRC=DESTCOLOR(9)
};
static const GLenum BLEND_DST[8] = {
    GL_ZERO,                    // 0: DST=ZERO(1)
    GL_ONE_MINUS_SRC_ALPHA,     // 1: DST=INVSRCALPHA(6)
    GL_ONE,                     // 2: DST=ONE(2)
    GL_ONE_MINUS_SRC_COLOR,     // 3: DST=INVSRCCOLOR(4)
    GL_SRC_COLOR,               // 4: DST=SRCCOLOR(3)
    GL_ZERO,                    // 5: DST=ZERO(1)
    GL_ONE,                     // 6: DST=ONE(2)
    GL_ONE                       // 7: DST=ONE(2)
};

int ewdx_init(void) {
    memset(&ewdx, 0, sizeof(ewdx));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) return 0;
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
    glViewport(0, 0, w, h);
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
    ewdx_apply_viewport();  // viewport follows the current target, not always screen
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
        if (id != 0) return 0;
    }
    if (id != ewdx.target) ewdx_flush();  // queued quads belong to the old FBO
    ewdx.target = id;
    glBindFramebuffer(GL_FRAMEBUFFER, ewdx.buf[id].fbo); // slot 0 fbo==0 (default)
    ewdx_apply_viewport();
    return -1;
}

int ewdx_color(int r, int g, int b, int a) {
    ewdx.st.r = r; ewdx.st.g = g; ewdx.st.b = b; ewdx.st.a = a;
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
    ewdx_flush();  // step (c): drain quad batcher before swap
    if (ewdx.target != 0) ewdx_select(0);
    SDL_GL_SwapWindow(ewdx.win);
    return -1;
}

int ewdx_apply_blend(int mode) {
    if (mode < 0 || mode > 7) mode = EWDX_BLEND_ALPHA;
    if (mode != ewdx.st.blend) ewdx_flush();  // pending quads use the old factors
    ewdx.st.blend = mode;
    glBlendFunc(BLEND_SRC[mode], BLEND_DST[mode]);
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
    SDL_Quit();
    return -1;
}
