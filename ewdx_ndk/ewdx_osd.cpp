// ewdx_osd.cpp - on-screen gamepad overlay (v27). Design notes:
//
// * Geometry is computed in GAME px (640x480 space) then converted to the
//   real EGL surface through the letterbox viewport, so the pad keeps its
//   position/size relative to the game view on any device.
// * One 128x128 radial-alpha disc texture + vertex tint = every control
//   (stick base, knob, glass buttons, mini buttons, menu chip). Drawn via
//   ewdx_immediate_quad (the text path's foreign-texture route).
//   v27.1: the texture builds LAZILY on the first draw (a GL context must be
//   current — ewdx_init runs before the window/context exist, so building
//   there produced an invisible pad whose touch zones still worked: the
//   owner saw nothing but couldn't touch the menu).
// * Blend: regular alpha (the game's mode 1) for translucent glass bodies;
//   ewdx_immediate_quad draws with whatever blend is set, so the OSD sets
//   mode 1 + full-state restore around its draws (present-time: no game
//   state to disturb).
// * Hit tests mirror the drawn rects exactly (shared rect helpers — single
//   source of truth, no see/press drift).
// * Multi-touch: SDL_FingerID captured per control at down; move/up re-route
//   by id. One finger per control; OSD-owned fingers never reach the gesture
//   layer (ewdx_input asks first), so drags don't double-fire.
// * Stick: knob offset clamped to 14 game px; deadzone 6 px -> direction
//   bits; diagonal = both bits (8-way, like the keyboard arrows).
// * MENU button injects ESC via ewdx_osd_esc() (getkey 27 / EXCMD_GETKEY
//   VK 27 in ewdx_extcmd reads it) — the game's pause path (key_esc2).
//   It never enters the joyg mask (script compares joyg by equality).
// * Menu auto-hide (v27.1): every menu row in the game is a 220x28 slice of
//   buffer 8 drawn at unit scale, white, blend 1, target 4 (L29502 area,
//   title + options + gallery idle). The batcher reports that signature via
//   ewdx_osd_menu_seen(); the OSD hides for 1 s after the last sight, so
//   menu taps land on the game, not the pad (the owner: "i can't touch the
//   main menu"). Gameplay never draws that signature (beams are additive,
//   different rects), so the pad stays up in game.
#include "ewdx_osd.h"
#include "ewdx_gles.h"
#include "ewdx_batch.h"
#include "ewdx_input.h"

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
#define STICK_TRAVEL 14      // knob clamp
#define STICK_DEAD   6.0f    // deadzone (game px)
#define BTN_R        34      // glass button radius
#define BTN_D        26      // diamond offset (center distance per axis)
#define BTN_CX       (640 - 78)
#define BTN_CY       (480 - 88)
#define MINI_R       16
#define MINI_DX      40      // S/D spacing
#define MENU_W       64
#define MENU_H       24

static OsdRect stick_base_rect(void) {
    OsdRect r = { STICK_CX - STICK_R, STICK_CY - STICK_R, STICK_R * 2, STICK_R * 2 };
    return r;
}
static OsdRect btn_rect(int dx, int dy) {  // dx/dy in {-BTN_D, 0, BTN_D}
    OsdRect r = { BTN_CX + dx - BTN_R, BTN_CY + dy - BTN_R, BTN_R * 2, BTN_R * 2 };
    return r;
}
static OsdRect mini_rect(int idx) {        // 0 = S, 1 = D
    OsdRect r = { BTN_CX - MINI_DX + idx * (MINI_DX * 2) - MINI_R,
                  BTN_CY - BTN_D - BTN_R - MINI_R - 14, MINI_R * 2, MINI_R * 2 };
    return r;
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
    float cx, cy;      // stick knob offset in game px
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

static int s_menu_mode(void) {
    return SDL_GetTicks() < s_menu_until_ms;
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

// normalized (0..1, top-left origin, full drawable) -> game px -> rect test
static int hit(OsdRect r, float nx, float ny) {
    int px = (int)(nx * (float)s_dw);
    int py = (int)(ny * (float)s_dh);
    int gx = (s_vw > 0) ? (px - s_vx) * s_scr_w / s_vw : px;
    int gy = (s_vh > 0) ? (py - s_vy) * s_scr_h / s_vh : py;
    return gx >= r.x && gx < r.x + r.w && gy >= r.y && gy < r.y + r.h;
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
    // frame (no signature) re-shows it after <=1 s.
    s_menu_until_ms = SDL_GetTicks() + 1000;
}

void ewdx_osd_set_pad_connected(int connected) {
    s_pad_connected = connected ? 1 : 0;
}

// geometry -> GL NDC corners. CRITICAL (v27.1 fix): emit_quad maps game px
// through the TARGET dims ((px+0.5)/scr_w*2-1) and the GL VIEWPORT (still the
// game letterbox rect when the OSD draws) does the scaling to the drawable.
// The first draft used full-drawable NDC — every control landed outside NDC
// range: the pad was INVISIBLE while its touch zones still worked (the owner
// saw nothing but couldn't touch the menu). Draw the OSD exactly like a game
// sprite: game-px rect -> NDC via target dims; viewport does the rest.
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

void ewdx_osd_draw(void) {
#ifdef EWDX_OSD_DISABLED
    return;
#else
    OsdRect r;
    int old_blend = ewdx.st.blend;
    int old_r = ewdx.st.r, old_g = ewdx.st.g, old_b = ewdx.st.b, old_a = ewdx.st.a;
    if (!s_inited || s_pad_connected || s_menu_mode()) return;
    snap_geometry();
    if (s_dw <= 0 || s_dh <= 0) return;
    if (s_disc == 0) s_disc = make_disc();  // v27.1: lazy (GL context live)
    if (s_disc == 0) return;
    ewdx_flush();
    ewdx_apply_blend(EWDX_BLEND_ALPHA);  // translucent glass (mode 1)
    // Stick: dark translucent base + red knob (knob offset already in game px)
    r = stick_base_rect();
    draw_disc(r, 0.10f, 0.10f, 0.12f, 0.55f);
    if (s_stick.id != 0) {
        r.x += (int)s_stick.cx;
        r.y -= (int)s_stick.cy;
    }
    draw_disc(r, 0.85f, 0.15f, 0.15f, 0.90f);
    // Diamond: blue C (left), green A (top), red Z (right), yellow X (bottom)
    r = btn_rect(-BTN_D, 0); draw_disc(r, 0.20f, 0.45f, 0.95f, 0.85f);
    r = btn_rect(0, -BTN_D); draw_disc(r, 0.15f, 0.80f, 0.25f, 0.85f);
    r = btn_rect(BTN_D, 0);  draw_disc(r, 0.95f, 0.20f, 0.20f, 0.90f);
    r = btn_rect(0, BTN_D);  draw_disc(r, 0.95f, 0.80f, 0.15f, 0.85f);
    // Mini S/D (held ones brighten via the same tint path)
    r = mini_rect(0); draw_disc(r, 0.85f, 0.85f, 0.20f, s_btns[4].held ? 0.95f : 0.70f);
    r = mini_rect(1); draw_disc(r, 0.85f, 0.85f, 0.20f, s_btns[5].held ? 0.95f : 0.70f);
    // MENU chip
    r = menu_rect();
    draw_disc(r, 0.20f, 0.20f, 0.22f, s_menu_held ? 0.90f : 0.70f);
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
        return 1;
    }
    if (s_stick.id == 0 && hit(stick_base_rect(), x, y)) {
        s_stick.id = id;
        s_stick.cx = s_stick.cy = 0.0f;
        ewdx_osd_touch_move(id, x, y);  // center-relative jump if off-center
        recompute_mask();
        return 1;
    }
    for (i = 0; i < 4; i++) {
        if (s_btns[i].id == 0 && hit(btn_rect(dxs[i], dys[i]), x, y)) {
            s_btns[i].id = id;
            s_btns[i].held = bits[i];
            recompute_mask();
            return 1;
        }
    }
    for (i = 4; i < 6; i++) {
        if (s_btns[i].id == 0 && hit(mini_rect(i - 4), x, y)) {
            s_btns[i].id = id;
            s_btns[i].held = bits[i];
            recompute_mask();
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
    // Convert the touch point to game px relative to the stick center.
    {
        int px = (int)(x * (float)s_dw);
        int py = (int)(y * (float)s_dh);
        int gx = (px - s_vx) * s_scr_w / s_vw;
        int gy = (py - s_vy) * s_scr_h / s_vh;
        cx = (float)(gx - STICK_CX);
        cy = (float)(gy - STICK_CY);
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
        consumed = 1;
    }
    if (s_stick.id == id) {
        s_stick.id = 0;
        s_stick.cx = s_stick.cy = 0.0f;
        consumed = 1;
    }
    for (i = 0; i < 6; i++) {
        if (s_btns[i].id == id) {
            s_btns[i].id = 0;
            s_btns[i].held = 0;
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
