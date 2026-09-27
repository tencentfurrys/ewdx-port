// ewdx_osd.cpp - on-screen gamepad overlay (v27.4). Design notes:
//
// * v27.4 GEOMETRY: the pad is sized/positioned in REAL SCREEN px (fractions
//   of the EGL drawable), NOT game px. The game-view-locked sizing made the
//   pad shrink with the game window ("too small" — owner). Controls: stick
//   base = 13% of screen height, glass buttons ~9%, in the screen's bottom
//   corners, MENU top-right. Drawn with the FULL-drawable viewport (the game
//   letterbox viewport is saved/restored around the OSD pass — otherwise
//   everything outside the game rect would be clipped).
// * NDC mapping = full drawable (2*px/dim-1, y mirrored); hit tests use the
//   SAME screen-px rects (single source of truth).
// * v27.3: occupancy is an explicit used flag per slot — SDL_FingerID 0 is
//   VALID on Android (ids start at 0; the old id-sentinel dropped every
//   first-finger press and let touch_up clear free slots).
// * v27.2 stick: RELATIVE drag (grab anchors, knob centered, delta moves it,
//   release recenters). Travel/deadzone scale with the base radius.
// * Menus: ewdx_osd_menu_seen() (batcher row-signature heartbeat) releases
//   all pad effects and hides the pad for 1 s; recompute_mask zeroes the pad
//   while a menu is visible.
// * Detail pass (owner reference photo): dark ring + glass body + gloss
//   highlight + letter labels via ewdx_text_label (cosmetic, own font size).
// * Journaling: visibility transitions + every press/release ("osd:" lines).
// * MENU button injects ESC via ewdx_osd_esc() (getkey 27 / VK 27 -> the
//   game's key_esc2 pause path). Never enters the joyg mask.
#include "ewdx_osd.h"
#include "ewdx_gles.h"
#include "ewdx_batch.h"
#include "ewdx_text.h"
#include "ewdx_input.h"
#include "ewdx_boot.h"

#include <SDL.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <string.h>

#ifndef __ANDROID__
#define EWDX_OSD_DISABLED 1
#endif

#define OSD_DISC 128

// joyg bits (mirror ewdx_input.h)
#define B_UP    EWDX_JOY_UP
#define B_DOWN  EWDX_JOY_DOWN
#define B_LEFT  EWDX_JOY_LEFT
#define B_RIGHT EWDX_JOY_RIGHT
#define B_Z     EWDX_JOY_BTN0
#define B_X     EWDX_JOY_BTN1
#define B_C     EWDX_JOY_BTN2
#define B_A     EWDX_JOY_BTN3
#define B_S     EWDX_JOY_BTN4
#define B_D     EWDX_JOY_BTN5

// --- control geometry (REAL SCREEN px; recomputed from the drawable) ---

typedef struct { int x, y, w, h; } OsdRect;

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int s_dw = 0, s_dh = 0;   // drawable snapshot (set by snap_geometry)

static int base_r(void) { return clampi(s_dh * 13 / 100, 44, 150); }
static int btn_r(void)  { return clampi(s_dh * 88 / 1000, 32, 110); }
static int mini_r(void) { return btn_r() * 55 / 100; }
static int stick_cx(void) { return s_dw * 115 / 1000; }
static int stick_cy(void) { return s_dh - base_r() * 135 / 100; }
static int dia_cx(void)   { return s_dw * 885 / 1000; }
static int dia_cy(void)   { return s_dh - btn_r() * 24 / 10; }
static int dia_d(void)    { return btn_r() * 115 / 100; }   // diamond offset
static int mini_dx(void)  { return btn_r() * 135 / 100; }
static int mini_cy(void)  { return dia_cy() - dia_d() - btn_r() - mini_r()
                                   - s_dh * 2 / 100; }
static int menu_w(void)   { return clampi(s_dh * 17 / 100, 56, 170); }
static int menu_h(void)   { return clampi(s_dh * 75 / 1000, 24, 76); }

static OsdRect centered_rect(int cx, int cy, int r) {
    OsdRect r2 = { cx - r, cy - r, r * 2, r * 2 };
    return r2;
}
static OsdRect stick_base_rect(void) {
    return centered_rect(stick_cx(), stick_cy(), base_r());
}
static OsdRect btn_rect(int dx, int dy) {  // dx/dy in {-dia_d, 0, dia_d}
    return centered_rect(dia_cx() + dx, dia_cy() + dy, btn_r());
}
static OsdRect mini_rect(int idx) {        // 0 = S, 1 = D
    return centered_rect(dia_cx() + (idx == 0 ? -mini_dx() : mini_dx()),
                         mini_cy(), mini_r());
}
static OsdRect menu_rect(void) {
    OsdRect r = { s_dw - menu_w() - 14, 14, menu_w(), menu_h() };
    return r;
}

// --- state ---

static int s_inited = 0;
static int s_pad_connected = 0;
static GLuint s_disc = 0;

typedef struct {
    int used;          // v27.3: explicit occupancy — fingerId 0 is VALID
    SDL_FingerID id;
    float ax, ay;      // v27.2: anchor (grab point relative to base center, screen px)
    float cx, cy;      // knob offset in screen px (drag delta, clamped)
} StickTouch;
typedef struct {
    int used;          // v27.3: see StickTouch.used
    SDL_FingerID id;
    int held;          // bit(s) asserted while this finger is down
} BtnTouch;

static StickTouch s_stick;
static BtnTouch s_btns[6];   // Z X C A S D
static int s_menu_used = 0;
static SDL_FingerID s_menu_id = 0;
static int s_menu_held = 0;
static int s_mask = 0;
static unsigned s_menu_until_ms = 0;  // hidden until this tick (menu seen)
static int s_vis_state = -1;          // journaled visibility (transitions only)

static int s_menu_mode(void) {
    return SDL_GetTicks() < s_menu_until_ms;
}

static void osd_journal(const char *msg) {
    ewdx_boot_journal(msg);
}

static void snap_geometry(void) {
    ewdx_surface_px(&s_dw, &s_dh);
    if (s_dw <= 0 || s_dh <= 0) { s_dw = 640; s_dh = 480; }
}

static void recompute_mask(void) {
    int m = 0, i;
    if (!s_menu_mode()) {  // v27.3: a visible menu always zeroes the pad
        if (s_stick.used) {
            int dead = base_r() * 14 / 100;
            float len = sqrtf(s_stick.cx * s_stick.cx + s_stick.cy * s_stick.cy);
            if (len > (float)dead) {
                if (s_stick.cx < -(float)dead) m |= B_LEFT;
                if (s_stick.cx >  (float)dead) m |= B_RIGHT;
                if (s_stick.cy < -(float)dead) m |= B_UP;
                if (s_stick.cy >  (float)dead) m |= B_DOWN;
            }
        }
        for (i = 0; i < 6; i++) m |= s_btns[i].held;
    }
    s_mask = m;
}

// Release every control's EFFECT but keep finger ids (their touch_up still
// routes here and clears cleanly). Called when a menu is detected.
static void release_all_effect(void) {
    int i;
    if (s_stick.used) { s_stick.cx = s_stick.cy = 0.0f; }
    for (i = 0; i < 6; i++) s_btns[i].held = 0;
    s_menu_held = 0;
    recompute_mask();
}

// --- disc texture (radial alpha: hard core + AA edge + glass rim) ---

static GLuint make_disc(void) {
    unsigned char *px = (unsigned char *)SDL_malloc(OSD_DISC * OSD_DISC * 4);
    GLuint tex = 0;
    int x, y;
    if (px == NULL) return 0;
    for (y = 0; y < OSD_DISC; y++) {
        for (x = 0; x < OSD_DISC; x++) {
            float dx = (float)(x - OSD_DISC / 2) + 0.5f;
            float dy = (float)(y - OSD_DISC / 2) + 0.5f;
            float d = sqrtf(dx * dx + dy * dy);
            float a;
            unsigned char *p = px + ((size_t)y * OSD_DISC + x) * 4;
            if (d <= 52.0f)      a = 1.0f;                       // solid core
            else if (d <= 56.0f) a = 1.0f - (d - 52.0f) / 4.0f;  // AA edge
            else if (d <= 59.0f) a = 0.35f;                      // glass rim
            else if (d <= 62.0f) a = 0.35f * (1.0f - (d - 59.0f) / 3.0f);
            else                 a = 0.0f;
            p[0] = p[1] = p[2] = 255;
            p[3] = (unsigned char)(a * 255.0f + 0.5f);
        }
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OSD_DISC, OSD_DISC, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, px);
    SDL_free(px);
    return tex;
}

void ewdx_osd_init(void) {
#ifdef EWDX_OSD_DISABLED
    return;
#else
    if (s_inited) return;
    memset(&s_stick, 0, sizeof(s_stick));
    memset(s_btns, 0, sizeof(s_btns));
    // v27.1: NO texture here — no GL context exists yet during ewdx_init.
    // make_disc() runs lazily on the first draw (context is current there).
    s_inited = 1;
#endif
}

void ewdx_osd_menu_seen(void) {
    // 1000 ms cover: menu frames redraw the signature every frame while a
    // menu is up; the first gameplay frame re-shows the pad after <=1 s.
    // Also RELEASE all held effects — a menu may appear while the player
    // holds a button (pause during play); the pad must not feed bits into
    // menu input.
    s_menu_until_ms = SDL_GetTicks() + 1000;
    release_all_effect();
}

void ewdx_osd_set_pad_connected(int connected) {
    int was = s_pad_connected;
    s_pad_connected = connected ? 1 : 0;
    if (s_pad_connected != was) {
        char msg[96];
        snprintf(msg, sizeof(msg), "osd: pad %s",
                 s_pad_connected ? "connected (overlay hidden)"
                                 : "removed (overlay shown)");
        osd_journal(msg);
    }
}

// --- drawing helpers (FULL-drawable space) ---

// screen-px rect -> full-drawable NDC (2*px/dim-1, y mirrored)
static void to_ndc(OsdRect r, float *ax, float *ay, float *bx, float *by) {
    *ax = 2.0f * (float)r.x / (float)s_dw - 1.0f;
    *ay = 1.0f - 2.0f * (float)r.y / (float)s_dh;
    *bx = 2.0f * (float)(r.x + r.w) / (float)s_dw - 1.0f;
    *by = 1.0f - 2.0f * (float)(r.y + r.h) / (float)s_dh;
}

static void draw_disc(OsdRect r, float cr, float cg, float cb, float ca) {
    float ax, ay, bx, by;
    if (s_disc == 0) return;
    to_ndc(r, &ax, &ay, &bx, &by);
    ewdx_immediate_quad(s_disc, ax, ay, bx, by, 0.0f, 0.0f, 1.0f, 1.0f,
                        cr, cg, cb, ca);
}

// One detailed glass button (owner reference: real pad photo):
// dark ring -> colored glass -> white gloss highlight -> letter label.
static void draw_glass_button(int cx, int cy, int r, float cr, float cg,
                              float cb, float held, const char *label,
                              int label_size) {
    draw_disc(centered_rect(cx, cy, r + r / 10 + 2),
              0.05f, 0.05f, 0.07f, 0.85f);
    draw_disc(centered_rect(cx, cy, r), cr, cg, cb, held ? 0.97f : 0.78f);
    // gloss highlight (upper-left, like a physical button's sheen)
    draw_disc(centered_rect(cx - r / 3, cy - r / 3, r / 3 + 2),
              1.0f, 1.0f, 1.0f, held ? 0.55f : 0.35f);
    if (label != NULL) {
        int ls = clampi(r * 2 / 3, 10, 40);
        (void)label_size;
        ewdx_text_label(label, cx, cy, ls, 1.0f, 1.0f, 1.0f,
                        held ? 1.0f : 0.9f);
    }
}

void ewdx_osd_draw(void) {
#ifdef EWDX_OSD_DISABLED
    return;
#else
    OsdRect r;
    int old_blend = ewdx.st.blend;
    int old_r = ewdx.st.r, old_g = ewdx.st.g, old_b = ewdx.st.b, old_a = ewdx.st.a;
    GLint old_vp[4];
    int hidden_menu, vis;
    if (!s_inited) return;
    hidden_menu = s_menu_mode();
    vis = s_pad_connected ? 2 : (hidden_menu ? 1 : 0);
    if (vis != s_vis_state) {
        osd_journal(vis == 0 ? "osd: shown (gameplay)"
                    : vis == 1 ? "osd: hidden (menu rows)"
                    : "osd: hidden (physical pad)");
        s_vis_state = vis;
    }
    if (vis != 0) return;
    snap_geometry();
    if (s_disc == 0) {
        s_disc = make_disc();  // v27.1: lazy (GL context live here)
        osd_journal("osd: disc texture built");
    }
    if (s_disc == 0) return;
    ewdx_flush();
    // v27.4: draw across the FULL drawable (the game letterbox viewport would
    // clip the screen-corner controls). Save -> fullscreen -> restore.
    glGetIntegerv(GL_VIEWPORT, old_vp);
    glViewport(0, 0, s_dw, s_dh);
    ewdx_apply_blend(EWDX_BLEND_ALPHA);  // translucent glass (mode 1)
    // Stick: dark ring + base + recessed center + red knob (RELATIVE delta)
    draw_disc(centered_rect(stick_cx(), stick_cy(), base_r() + base_r() / 10 + 2),
              0.05f, 0.05f, 0.07f, 0.85f);
    draw_disc(stick_base_rect(), 0.10f, 0.10f, 0.12f, 0.55f);
    draw_disc(centered_rect(stick_cx(), stick_cy(), base_r() / 2),
              0.16f, 0.16f, 0.18f, 0.45f);
    {
        int kx = stick_cx(), ky = stick_cy();
        if (s_stick.used) {
            kx += (int)s_stick.cx;
            ky -= (int)s_stick.cy;
        }
        draw_glass_button(kx, ky, base_r() * 11 / 20,
                          0.85f, 0.15f, 0.15f, s_stick.used, NULL, 0);
        draw_disc(centered_rect(kx, ky - base_r() / 8, base_r() / 8),
                  1.0f, 1.0f, 1.0f, 0.40f);
    }
    // Diamond: blue C (left), green A (top), red Z (right), yellow X (bottom)
    draw_glass_button(dia_cx() - dia_d(), dia_cy(), btn_r(),
                      0.20f, 0.45f, 0.95f, s_btns[2].held != 0, "C", 0);
    draw_glass_button(dia_cx(), dia_cy() - dia_d(), btn_r(),
                      0.15f, 0.80f, 0.25f, s_btns[3].held != 0, "A", 0);
    draw_glass_button(dia_cx() + dia_d(), dia_cy(), btn_r(),
                      0.95f, 0.20f, 0.20f, s_btns[0].held != 0, "Z", 0);
    draw_glass_button(dia_cx(), dia_cy() + dia_d(), btn_r(),
                      0.95f, 0.80f, 0.15f, s_btns[1].held != 0, "X", 0);
    // Mini S/D
    draw_glass_button(dia_cx() - mini_dx(), mini_cy(), mini_r(),
                      0.85f, 0.85f, 0.20f, s_btns[4].held != 0, "S", 0);
    draw_glass_button(dia_cx() + mini_dx(), mini_cy(), mini_r(),
                      0.85f, 0.85f, 0.20f, s_btns[5].held != 0, "D", 0);
    // MENU chip (dark pill + label)
    r = menu_rect();
    draw_disc(r, 0.20f, 0.20f, 0.22f, s_menu_held ? 0.92f : 0.70f);
    ewdx_text_label("MENU", r.x + r.w / 2, r.y + r.h / 2,
                    clampi(r.h * 2 / 3, 10, 32), 1.0f, 1.0f, 1.0f, 0.95f);
    // Restore game viewport + state exactly (present-time hygiene)
    glViewport(old_vp[0], old_vp[1], old_vp[2], old_vp[3]);
    ewdx_apply_blend(old_blend);
    ewdx_color(old_r, old_g, old_b, old_a);
#endif
}

// --- touch routing (input layer asks first; 1 = consumed by the OSD) ---

// normalized (0..1, top-left origin, full drawable) -> screen px -> rect test
static int hit(OsdRect r, float nx, float ny) {
    int px = (int)(nx * (float)s_dw);
    int py = (int)(ny * (float)s_dh);
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

int ewdx_osd_touch_down(SDL_FingerID id, float x, float y) {
#ifdef EWDX_OSD_DISABLED
    (void)id; (void)x; (void)y;
    return 0;
#else
    int i;
    OsdRect r;
    static const int bits[6] = { B_Z, B_X, B_C, B_A, B_S, B_D };
    static const char *names[6] = { "Z", "X", "C", "A", "S", "D" };
    static const int dxs[4] = { 1, 0, -1, 0 };
    static const int dys[4] = { 0, 1, 0, -1 };
    // v27.3: id==0 is a VALID finger id (Android pointer ids start at 0).
    // The old "id == 0 -> reject" guard silently dropped the first finger's
    // press everywhere; occupancy is tracked by the explicit used flags now.
    if (!s_inited || s_pad_connected || s_menu_mode()) return 0;
    snap_geometry();
    // Already owned? (defensive: dup downs)
    if ((s_stick.used && s_stick.id == id) ||
        (s_menu_used && s_menu_id == id)) return 1;
    for (i = 0; i < 6; i++) if (s_btns[i].used && s_btns[i].id == id) return 1;
    // MENU first (top-right; must not fall through to anything else).
    r = menu_rect();
    if (hit(r, x, y)) {
        s_menu_used = 1;
        s_menu_id = id;
        s_menu_held = 1;
        osd_journal("osd: press MENU (ESC)");
        return 1;
    }
    if (!s_stick.used && hit(stick_base_rect(), x, y)) {
        // v27.2 RELATIVE stick: anchor at the grab point; the knob starts
        // centered and only the DRAG moves it (screen-px space now).
        int px = (int)(x * (float)s_dw);
        int py = (int)(y * (float)s_dh);
        s_stick.used = 1;
        s_stick.id = id;
        s_stick.ax = (float)(px - stick_cx());
        s_stick.ay = (float)(py - stick_cy());
        s_stick.cx = s_stick.cy = 0.0f;
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "osd: stick grab id=%llu r=%d",
                     (unsigned long long)id, base_r());
            osd_journal(msg);
        }
        recompute_mask();
        return 1;
    }
    for (i = 0; i < 4; i++) {
        if (!s_btns[i].used &&
            hit(btn_rect(dxs[i] * dia_d(), dys[i] * dia_d()), x, y)) {
            s_btns[i].used = 1;
            s_btns[i].id = id;
            s_btns[i].held = bits[i];
            recompute_mask();
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "osd: press %s", names[i]);
                osd_journal(msg);
            }
            return 1;
        }
    }
    for (i = 4; i < 6; i++) {
        if (!s_btns[i].used && hit(mini_rect(i - 4), x, y)) {
            s_btns[i].used = 1;
            s_btns[i].id = id;
            s_btns[i].held = bits[i];
            recompute_mask();
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "osd: press %s", names[i]);
                osd_journal(msg);
            }
            return 1;
        }
    }
    return 0;
#endif
}

int ewdx_osd_touch_move(SDL_FingerID id, float x, float y) {
#ifdef EWDX_OSD_DISABLED
    (void)id; (void)x; (void)y;
    return 0;
#else
    float cx, cy, len;
    int travel;
    if (!s_inited || !s_stick.used || s_stick.id != id) return 0;
    snap_geometry();
    // v27.2: knob = (touch - base center) - anchor, clamped to travel
    // (screen-px space; travel scales with the base radius).
    {
        int px = (int)(x * (float)s_dw);
        int py = (int)(y * (float)s_dh);
        cx = (float)(px - stick_cx()) - s_stick.ax;
        cy = (float)(py - stick_cy()) - s_stick.ay;
    }
    travel = base_r() * 38 / 100;
    len = sqrtf(cx * cx + cy * cy);
    if (len > (float)travel) {
        cx = cx * (float)travel / len;
        cy = cy * (float)travel / len;
    }
    s_stick.cx = cx; s_stick.cy = cy;
    recompute_mask();
    return 1;
#endif
}

int ewdx_osd_touch_up(SDL_FingerID id, float x, float y) {
#ifdef EWDX_OSD_DISABLED
    (void)id; (void)x; (void)y;
    return 0;
#else
    int i, consumed = 0;
    if (!s_inited) return 0;
    // v27.3: every match requires used && id — a release for finger 0 must
    // never clear free slots (the old id-sentinel matching spammed bogus
    // releases and let a stuck gesture finger hold DOWN forever).
    if (s_menu_used && s_menu_id == id) {
        s_menu_used = 0;
        s_menu_id = 0;
        s_menu_held = 0;
        osd_journal("osd: release MENU");
        consumed = 1;
    }
    if (s_stick.used && s_stick.id == id) {
        s_stick.used = 0;
        s_stick.id = 0;
        s_stick.cx = s_stick.cy = 0.0f;
        osd_journal("osd: stick release");
        consumed = 1;
    }
    for (i = 0; i < 6; i++) {
        if (s_btns[i].used && s_btns[i].id == id) {
            s_btns[i].used = 0;
            s_btns[i].id = 0;
            s_btns[i].held = 0;
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "osd: release btn%d", i);
                osd_journal(msg);
            }
            consumed = 1;
        }
    }
    (void)x; (void)y;
    recompute_mask();
    return consumed;
#endif
}

int ewdx_osd_buttons(void) {
    return s_mask;
}

int ewdx_osd_esc(void) {
    return s_menu_held;
}

void ewdx_osd_shutdown(void) {
    if (s_disc) { glDeleteTextures(1, &s_disc); s_disc = 0; }
    s_inited = 0;
}
