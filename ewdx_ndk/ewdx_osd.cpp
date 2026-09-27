// ewdx_osd.cpp - on-screen gamepad overlay (v27.2). Design notes:
//
// * Geometry is computed in GAME px (640x480 space) then mapped through the
//   SAME NDC convention as emit_quad ((px+0.5)/target_w*2-1); the GL viewport
//   (still the game letterbox at present time) does the scaling. v27 drew
//   full-drawable NDC = everything outside NDC range = invisible.
// * v27.1 lesson: the disc texture must build LAZILY on the first draw (a GL
//   context must be current). ewdx_init has no context yet.
// * v27.2 stick: RELATIVE drag. The first touch anchors the knob at center;
//   only the DRAG DELTA moves it. v27.1 used absolute knob positioning, so
//   landing a thumb on the lower half of the base = instant full DOWN — the
//   owner's "just going down". Release recenters.
// * v27.2 menu safety: ewdx_osd_menu_seen() (fired by the batcher on the
//   title/options row signature) now RELEASES ALL OSD CONTROLS and hides the
//   pad for 1 s. The pad can never hold a direction or button on a menu,
//   whatever state it was in when the menu appeared.
// * v27.2 observability: visibility transitions, pad connect/remove, and
//   every OSD press/release are journaled ("osd:" lines in ewdx-boot*.log).
//   If touch input misbehaves again, the logs show exactly what the OSD
//   consumed and when.
// * v27.2 detail pass (owner request + GameStop-pad reference photo): every
//   control = dark outer ring + colored glass body + offset white gloss
//   highlight + letter label (Z/X/C/A, S/D, MENU) via ewdx_text_label.
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

// --- control geometry (GAME px, top-left origin; converted at draw time) ---

typedef struct { int x, y, w, h; } OsdRect;

#define STICK_R      46      // stick base radius (game px)
#define STICK_CX     84      // center x from left edge
#define STICK_CY     (480 - 92)
#define STICK_TRAVEL 16      // knob clamp (game px, RELATIVE drag)
#define STICK_DEAD   6.0f    // deadzone (game px)
#define BTN_R        30      // glass button radius
#define BTN_D        34      // diamond center distance per axis
#define BTN_CX       (640 - 76)
#define BTN_CY       (480 - 86)
#define MINI_R       16
#define MINI_DX      40      // S/D spacing
#define MENU_W       64
#define MENU_H       24

static OsdRect centered_rect(int cx, int cy, int r) {
    OsdRect r2 = { cx - r, cy - r, r * 2, r * 2 };
    return r2;
}
static OsdRect stick_base_rect(void) {
    return centered_rect(STICK_CX, STICK_CY, STICK_R);
}
static OsdRect btn_rect(int dx, int dy) {  // dx/dy in {-BTN_D, 0, BTN_D}
    return centered_rect(BTN_CX + dx, BTN_CY + dy, BTN_R);
}
static OsdRect mini_rect(int idx) {        // 0 = S, 1 = D
    return centered_rect(BTN_CX - MINI_DX + idx * (MINI_DX * 2),
                         BTN_CY - BTN_D - BTN_R - MINI_R - 16, MINI_R);
}
static OsdRect menu_rect(void) {
    OsdRect r = { 640 - MENU_W - 10, 10, MENU_W, MENU_H };
    return r;
}

// --- state ---

static int s_inited = 0;
static int s_pad_connected = 0;
static GLuint s_disc = 0;
static int s_scr_w = 640, s_scr_h = 480;   // snapshot at event/draw time
static int s_vx, s_vy, s_vw, s_vh;         // letterbox viewport snapshot
static int s_dw, s_dh;                     // drawable snapshot

typedef struct {
    SDL_FingerID id;   // 0 = free
    float ax, ay;      // v27.2: anchor (grab point relative to base center)
    float cx, cy;      // knob offset in game px (drag delta, clamped)
} StickTouch;
typedef struct {
    SDL_FingerID id;   // 0 = free
    int held;          // bit(s) asserted while this finger is down
} BtnTouch;

static StickTouch s_stick;
static BtnTouch s_btns[6];   // Z X C A S D
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
    if (ewdx.scr_w > 0) s_scr_w = ewdx.scr_w;
    if (ewdx.scr_h > 0) s_scr_h = ewdx.scr_h;
    s_vx = ewdx.viewport[0]; s_vy = ewdx.viewport[1];
    s_vw = ewdx.viewport[2]; s_vh = ewdx.viewport[3];
    if (s_vw <= 0 || s_vh <= 0) {
        s_dw = s_vw = s_scr_w; s_dh = s_vh = s_scr_h;
        s_vx = s_vy = 0;
    } else {
        ewdx_surface_px(&s_dw, &s_dh);
    }
    if (s_dw <= 0 || s_dh <= 0) { s_dw = s_vw; s_dh = s_vh; }
}

// normalized (0..1, top-left origin, full drawable) -> game px -> rect test
static int hit(OsdRect r, float nx, float ny) {
    int px = (int)(nx * (float)s_dw);
    int py = (int)(ny * (float)s_dh);
    int gx = (s_vw > 0) ? (px - s_vx) * s_scr_w / s_vw : px;
    int gy = (s_vh > 0) ? (py - s_vy) * s_scr_h / s_vh : py;
    return gx >= r.x && gx < r.x + r.w && gy >= r.y && gy < r.y + r.h;
}

static void recompute_mask(void) {
    int m = 0, i;
    if (s_stick.id != 0) {
        float len = sqrtf(s_stick.cx * s_stick.cx + s_stick.cy * s_stick.cy);
        if (len > STICK_DEAD) {
            if (s_stick.cx < -STICK_DEAD) m |= B_LEFT;
            if (s_stick.cx >  STICK_DEAD) m |= B_RIGHT;
            if (s_stick.cy < -STICK_DEAD) m |= B_UP;
            if (s_stick.cy >  STICK_DEAD) m |= B_DOWN;
        }
    }
    for (i = 0; i < 6; i++) m |= s_btns[i].held;
    s_mask = m;
}

// Release every control's EFFECT but keep finger ids (their touch_up still
// routes here and clears cleanly). Called when a menu is detected: the pad
// must never hold input while a menu is up, whatever happened in game.
static void release_all_effect(void) {
    int i;
    if (s_stick.id != 0) { s_stick.cx = s_stick.cy = 0.0f; }
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
    // menu is up, so the pad re-hides continuously; the first gameplay
    // frame (no signature) re-shows it after <=1 s. Also RELEASE all held
    // effects — a menu may appear while the player holds a button (pause
    // during play); the pad must not feed bits into menu input.
    s_menu_until_ms = SDL_GetTicks() + 1000;
    release_all_effect();
}

void ewdx_osd_set_pad_connected(int connected) {
    int was = s_pad_connected;
    s_pad_connected = connected ? 1 : 0;
    if (s_pad_connected != was) {
        char msg[96];
        snprintf(msg, sizeof(msg), "osd: pad %s",
                 s_pad_connected ? "connected (overlay hidden)" : "removed (overlay shown)");
        osd_journal(msg);
    }
}

// --- drawing helpers ---

// game-px rect -> NDC via the emit_quad convention ((px+0.5)/target*2-1);
// the GL viewport (game letterbox, still active at present) scales it.
static void to_ndc(OsdRect r, float *ax, float *ay, float *bx, float *by) {
    *ax = ((float)r.x + 0.5f) / (float)s_scr_w * 2.0f - 1.0f;
    *ay = 1.0f - ((float)r.y + 0.5f) / (float)s_scr_h * 2.0f;
    *bx = ((float)(r.x + r.w) + 0.5f) / (float)s_scr_w * 2.0f - 1.0f;
    *by = 1.0f - ((float)(r.y + r.h) + 0.5f) / (float)s_scr_h * 2.0f;
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
    OsdRect rr = centered_rect(cx, cy, r);
    draw_disc(centered_rect(cx, cy, r + 3), 0.05f, 0.05f, 0.07f, 0.85f);
    draw_disc(rr, cr, cg, cb, held ? 0.97f : 0.78f);
    // gloss highlight (upper-left, like a physical button's sheen)
    draw_disc(centered_rect(cx - r / 3, cy - r / 3, r / 3 + 2),
              1.0f, 1.0f, 1.0f, held ? 0.55f : 0.35f);
    if (label != NULL) {
        ewdx_text_label(label, cx, cy, label_size, 1.0f, 1.0f, 1.0f,
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
    if (s_dw <= 0 || s_dh <= 0) return;
    if (s_disc == 0) {
        s_disc = make_disc();  // v27.1: lazy (GL context live here)
        osd_journal("osd: disc texture built");
    }
    if (s_disc == 0) return;
    ewdx_flush();
    ewdx_apply_blend(EWDX_BLEND_ALPHA);  // translucent glass (mode 1)
    // Stick: dark ring + base + red knob (offset = RELATIVE drag delta)
    draw_disc(centered_rect(STICK_CX, STICK_CY, STICK_R + 3),
              0.05f, 0.05f, 0.07f, 0.85f);
    draw_disc(stick_base_rect(), 0.10f, 0.10f, 0.12f, 0.55f);
    draw_disc(centered_rect(STICK_CX, STICK_CY, STICK_R / 2),
              0.16f, 0.16f, 0.18f, 0.45f);
    {
        int kx = STICK_CX, ky = STICK_CY;
        if (s_stick.id != 0) {
            kx += (int)s_stick.cx;
            ky -= (int)s_stick.cy;
        }
        draw_glass_button(kx, ky, (STICK_R * 11) / 20,
                          0.85f, 0.15f, 0.15f, s_stick.id != 0, NULL, 0);
        draw_disc(centered_rect(kx, ky - 6, 6), 1.0f, 1.0f, 1.0f, 0.40f);
    }
    // Diamond: blue C (left), green A (top), red Z (right), yellow X (bottom)
    draw_glass_button(BTN_CX - BTN_D, BTN_CY, BTN_R,
                      0.20f, 0.45f, 0.95f, s_btns[2].held != 0, "C", 20);
    draw_glass_button(BTN_CX, BTN_CY - BTN_D, BTN_R,
                      0.15f, 0.80f, 0.25f, s_btns[3].held != 0, "A", 20);
    draw_glass_button(BTN_CX + BTN_D, BTN_CY, BTN_R,
                      0.95f, 0.20f, 0.20f, s_btns[0].held != 0, "Z", 20);
    draw_glass_button(BTN_CX, BTN_CY + BTN_D, BTN_R,
                      0.95f, 0.80f, 0.15f, s_btns[1].held != 0, "X", 20);
    // Mini S/D
    draw_glass_button(BTN_CX - MINI_DX, BTN_CY - BTN_D - BTN_R - MINI_R - 16,
                      MINI_R, 0.85f, 0.85f, 0.20f, s_btns[4].held != 0, "S", 12);
    draw_glass_button(BTN_CX + MINI_DX, BTN_CY - BTN_D - BTN_R - MINI_R - 16,
                      MINI_R, 0.85f, 0.85f, 0.20f, s_btns[5].held != 0, "D", 12);
    // MENU chip (dark pill + label)
    r = menu_rect();
    draw_disc(r, 0.20f, 0.20f, 0.22f, s_menu_held ? 0.92f : 0.70f);
    ewdx_text_label("MENU", 640 - 10 - MENU_W / 2, 10 + MENU_H / 2, 12,
                    1.0f, 1.0f, 1.0f, 0.95f);
    // Restore game state exactly (present-time hygiene)
    ewdx_apply_blend(old_blend);
    ewdx_color(old_r, old_g, old_b, old_a);
#endif
}

// --- touch routing (input layer asks first; 1 = consumed by the OSD) ---

int ewdx_osd_touch_down(SDL_FingerID id, float x, float y) {
#ifdef EWDX_OSD_DISABLED
    (void)id; (void)x; (void)y;
    return 0;
#else
    int i;
    OsdRect r;
    static const int bits[6] = { B_Z, B_X, B_C, B_A, B_S, B_D };
    static const char *names[6] = { "Z", "X", "C", "A", "S", "D" };
    static const int dxs[4] = { BTN_D, 0, -BTN_D, 0 };
    static const int dys[4] = { 0, BTN_D, 0, -BTN_D };
    if (!s_inited || s_pad_connected || id == 0 || s_menu_mode()) return 0;
    snap_geometry();
    if (s_dw <= 0 || s_dh <= 0) return 0;
    // Already owned? (defensive: dup downs)
    if (s_stick.id == id || s_menu_id == id) return 1;
    for (i = 0; i < 6; i++) if (s_btns[i].id == id) return 1;
    // MENU first (top-right; must not fall through to anything else).
    r = menu_rect();
    if (hit(r, x, y)) {
        s_menu_id = id;
        s_menu_held = 1;
        osd_journal("osd: press MENU (ESC)");
        return 1;
    }
    if (s_stick.id == 0 && hit(stick_base_rect(), x, y)) {
        // v27.2 RELATIVE stick: anchor at the grab point; the knob starts
        // centered and only the DRAG moves it. Touching the lower half no
        // longer means instant DOWN.
        int px = (int)(x * (float)s_dw);
        int py = (int)(y * (float)s_dh);
        int gx = (px - s_vx) * s_scr_w / s_vw;
        int gy = (py - s_vy) * s_scr_h / s_vh;
        s_stick.id = id;
        s_stick.ax = (float)(gx - STICK_CX);
        s_stick.ay = (float)(gy - STICK_CY);
        s_stick.cx = s_stick.cy = 0.0f;
        {
            char msg[96];
            snprintf(msg, sizeof(msg), "osd: stick grab anchor=(%.0f,%.0f)",
                     s_stick.ax, s_stick.ay);
            osd_journal(msg);
        }
        recompute_mask();
        return 1;
    }
    for (i = 0; i < 4; i++) {
        if (s_btns[i].id == 0 && hit(btn_rect(dxs[i], dys[i]), x, y)) {
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
        if (s_btns[i].id == 0 && hit(mini_rect(i - 4), x, y)) {
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
    if (!s_inited || s_stick.id != id || s_stick.id == 0) return 0;
    if (s_vw <= 0 || s_vh <= 0) { snap_geometry(); }
    // v27.2: knob = (touch - base center) - anchor, clamped to travel.
    {
        int px = (int)(x * (float)s_dw);
        int py = (int)(y * (float)s_dh);
        int gx = (px - s_vx) * s_scr_w / s_vw;
        int gy = (py - s_vy) * s_scr_h / s_vh;
        cx = (float)(gx - STICK_CX) - s_stick.ax;
        cy = (float)(gy - STICK_CY) - s_stick.ay;
    }
    len = sqrtf(cx * cx + cy * cy);
    if (len > (float)STICK_TRAVEL) {
        cx = cx * (float)STICK_TRAVEL / len;
        cy = cy * (float)STICK_TRAVEL / len;
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
    if (s_menu_id == id) {
        s_menu_id = 0;
        s_menu_held = 0;
        osd_journal("osd: release MENU");
        consumed = 1;
    }
    if (s_stick.id == id) {
        s_stick.id = 0;
        s_stick.cx = s_stick.cy = 0.0f;
        osd_journal("osd: stick release");
        consumed = 1;
    }
    for (i = 0; i < 6; i++) {
        if (s_btns[i].id == id) {
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
