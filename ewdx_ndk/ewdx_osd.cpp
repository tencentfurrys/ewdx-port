// ewdx_osd.cpp - on-screen gamepad overlay (v27.7). Design notes:
//
// * v27.9 CHIPS: two skins (0 JEWEL default, 1 GLASS; FLAT removed)
//   cycled by a top-left chip, plus a HIDE/SHOW chip that drops the
//   stick and buttons. Both sit top-LEFT, opposite MENU, clear of the
//   stick and diamond. HIDE/SHOW and MENU stay live and drawn while the
//   pad is hidden; hidden pad controls fall through to the game rather
//   than swallowing taps on invisible rects, and assert no joyg bits.
//   Skin choice is in-memory only -- it resets to JEWEL on relaunch.
//
// * v27.5 LOOK: the flat tinted disc + hard white blob read as stickers.
//   Replaced with real shading -- see the texture generators below for
//   why the gloss must be a separate ADDITIVE pass (the shader multiplies
//   texture by vertex colour, so a multiply pass can never out-shine the
//   button's own tint). Also clamps the controls inside the drawable;
//   the stick was hanging off the left edge.
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
#define OSD_TEX  256   // v27.5 shaded control textures

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
static int stick_cx(void) {
    int r = base_r(), x = s_dw * 115 / 1000, lo = r + r / 6 + 10;
    return x < lo ? lo : x;              // v27.5: was clipped off-screen left
}
static int stick_cy(void) {
    int r = base_r(), y = s_dh - r * 135 / 100, hi = s_dh - r - r / 6 - 10;
    return y > hi ? hi : y;
}
static int dia_cx(void) {
    int x = s_dw * 885 / 1000;
    int hi = s_dw - (btn_r() * 215 / 100) - 10;   // diamond half-span + margin
    return x > hi ? hi : x;
}
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
// v27.7: the two new chips sit top-LEFT so they are on the opposite side
// from MENU and clear of the stick and the button diamond.
static OsdRect hide_rect(void) {
    OsdRect r = { 14, 14, menu_w(), menu_h() };
    return r;
}
static OsdRect style_rect(void) {
    OsdRect r = { 14 + menu_w() + 12, 14, menu_w(), menu_h() };
    return r;
}
// v30.1: third chip -- FIT/FULL. Left of MENU, right of the skin chip.
static OsdRect fill_rect(void) {
    OsdRect r = { 14 + (menu_w() + 12) * 2, 14, menu_w(), menu_h() };
    return r;
}

// --- state ---

static int s_inited = 0;
static int s_pad_connected = 0;
static GLuint s_disc  = 0;
static GLuint s_glass = 0;   // v27.5 convex dome body (RGB shades the tint)
static GLuint s_spec  = 0;   // v27.5 additive gloss: hotspot + bounced rim
static GLuint s_dish  = 0;   // v27.5 concave dish (stick base + knob top)
static GLuint s_jewel = 0;   // v27.6 TRANSLUCENT body (variable opacity)
static GLuint s_caus  = 0;   // v27.6 internal scatter, drawn in button hue

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
// v27.9 skin + visibility. s_style: 0 JEWEL (default), 1 GLASS.
// The old FLAT (v27.4) skin was dropped at the owner!s request.
static int s_style = 0;
static int s_hidden = 0;
static int s_hide_used = 0;   static SDL_FingerID s_hide_id = 0;
static int s_style_used = 0;  static SDL_FingerID s_style_id = 0;
// v30.1: FIT/FULL chip state (screen fill = stretch-to-drawable).
static int s_fill_used = 0;   static SDL_FingerID s_fill_id = 0;

static const char *style_name(void) {
    return s_style == 0 ? "JEWEL" : "GLASS";
}
static const char *fill_name(void) {
    return ewdx_get_screen_fill() ? "FULL" : "FIT";
}

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
    if (!s_menu_mode() && !s_hidden) {  // menu OR user-hide zeroes the pad
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


// --- v27.5 shaded control textures -------------------------------------
//
// The fragment shader is  texture2D(tex) * vertexColor  (pure multiply), so a
// texture can only DARKEN the tint it is drawn with. That is the whole reason
// the old flat-disc + white-blob buttons read as stickers: a single uniform
// disc tinted flat has no curvature, and a multiply pass can never put a
// highlight ABOVE the body colour. So the look is split in two:
//   s_glass  RGB = dome shading 0.30..1.00, multiplied into the button colour
//   s_spec   white, drawn ADDITIVELY on top for the parts that must out-shine
//            the body (the hotspot and the bounced rim)
// Light is fixed upper-left, matching the reference photo.

static const float LX = -0.52f, LY = -0.58f, LZ = 0.63f;

static GLuint upload_rgba(unsigned char *px) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OSD_TEX, OSD_TEX, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, px);
    return tex;
}

// Convex moulded button face. The curvature exponent keeps the middle broad
// and flat and rolls the normal over hard near the rim, which is how an
// injection-moulded pad button actually catches light (a plain hemisphere
// looks like a marble instead).
static GLuint make_glass(void) {
    unsigned char *px = (unsigned char *)SDL_malloc(OSD_TEX * OSD_TEX * 4);
    const float R = 104.0f;   // 104/128 == s_disc's 52/64 core:
                              // all control textures share one scale
    int x, y;
    GLuint t;
    if (px == NULL) return 0;
    for (y = 0; y < OSD_TEX; y++) {
        for (x = 0; x < OSD_TEX; x++) {
            float dx = (float)x - (float)(OSD_TEX / 2) + 0.5f;
            float dy = (float)y - (float)(OSD_TEX / 2) + 0.5f;
            float d  = sqrtf(dx * dx + dy * dy);
            float u  = d / R;
            unsigned char *p = px + ((size_t)y * OSD_TEX + x) * 4;
            float a, nr, nx, ny, nz, diff, shade;
            if (u >= 1.06f) { p[0] = p[1] = p[2] = 0; p[3] = 0; continue; }
            a = (u <= 1.0f) ? 1.0f : 1.0f - (u - 1.0f) / 0.06f;
            if (a < 0.0f) a = 0.0f;
            nr = powf(u, 2.2f);                       // flat face, fast rim roll
            nx = (d > 0.001f) ? (dx / d) * nr : 0.0f;
            ny = (d > 0.001f) ? (dy / d) * nr : 0.0f;
            nz = sqrtf(fmaxf(0.0f, 1.0f - nr * nr));
            diff = nx * LX + ny * LY + nz * LZ;
            if (diff < 0.0f) diff = 0.0f;
            shade = 0.30f + 0.78f * diff;             // ambient + lambert
            if (u > 0.86f) {                          // rim lip, reads as bevel
                float tt = (u - 0.86f) / 0.14f;
                shade *= 1.0f - 0.55f * tt * tt;
            }
            if (shade > 1.0f) shade = 1.0f;
            if (shade < 0.0f) shade = 0.0f;
            p[0] = p[1] = p[2] = (unsigned char)(shade * 255.0f + 0.5f);
            p[3] = (unsigned char)(a * 255.0f + 0.5f);
        }
    }
    t = upload_rgba(px);
    SDL_free(px);
    return t;
}

// Additive gloss. Two features, both taken off the reference photo:
//  * a soft elliptical hotspot in the upper-left quadrant (the sheen)
//  * a crescent of bounced light hugging the INNER lower-right edge -- this
//    is the cue that actually sells translucent plastic; without it a shaded
//    dome still reads as opaque rubber.
static GLuint make_spec(void) {
    unsigned char *px = (unsigned char *)SDL_malloc(OSD_TEX * OSD_TEX * 4);
    const float R = 104.0f;   // 104/128 == s_disc's 52/64 core:
                              // all control textures share one scale
    int x, y;
    GLuint t;
    if (px == NULL) return 0;
    for (y = 0; y < OSD_TEX; y++) {
        for (x = 0; x < OSD_TEX; x++) {
            float dx = (float)x - (float)(OSD_TEX / 2) + 0.5f;
            float dy = (float)y - (float)(OSD_TEX / 2) + 0.5f;
            float d  = sqrtf(dx * dx + dy * dy);
            float u  = d / R;
            unsigned char *p = px + ((size_t)y * OSD_TEX + x) * 4;
            float hot = 0.0f, rim = 0.0f, a;
            p[0] = p[1] = p[2] = 255;
            if (u >= 1.0f) { p[3] = 0; continue; }
            {
                float hx = (dx + 0.34f * R) / (0.32f * R);
                float hy = (dy + 0.40f * R) / (0.24f * R);
                float hd = sqrtf(hx * hx + hy * hy);
                if (hd < 1.0f) {
                    hot = 1.0f - hd;
                    hot = hot * hot * (3.0f - 2.0f * hot);   // smooth falloff
                    hot *= 0.95f;
                }
            }
            if (u > 0.78f && u < 0.99f) {
                float tt   = (u - 0.78f) / 0.21f;
                float band = sinf(tt * 3.14159265f);
                float dirn = (d > 0.001f)
                           ? (dx / d) * 0.55f + (dy / d) * 0.83f : 0.0f;
                if (dirn > 0.0f) rim = band * dirn * dirn * 0.42f;
            }
            a = hot + rim;
            if (u > 0.94f) a *= 1.0f - (u - 0.94f) / 0.06f;  // no spill past rim
            if (a < 0.0f) a = 0.0f;
            if (a > 1.0f) a = 1.0f;
            p[3] = (unsigned char)(a * 255.0f + 0.5f);
        }
    }
    t = upload_rgba(px);
    SDL_free(px);
    return t;
}

// v27.6 translucent body. Two things separate see-through plastic from a
// painted disc, and both are about optical path length:
//   * OPACITY VARIES. Straight down the middle of the dome you look through
//     thin material and the scene behind shows; at the rim the sightline runs
//     the long way through the plastic, so it goes saturated and opaque.
//     Constant alpha is what made the old buttons read as stickers.
//   * The body's own brightness peaks LOWER-RIGHT of centre -- light enters
//     the lit upper-left face, refracts, and pools against the far inner
//     wall. Putting that pool opposite the specular is what makes a button
//     look lit from within rather than lit from outside.
static GLuint make_jewel(void) {
    unsigned char *px = (unsigned char *)SDL_malloc(OSD_TEX * OSD_TEX * 4);
    const float R = 104.0f;
    int x, y;
    GLuint t;
    if (px == NULL) return 0;
    for (y = 0; y < OSD_TEX; y++) {
        for (x = 0; x < OSD_TEX; x++) {
            float dx = (float)x - (float)(OSD_TEX / 2) + 0.5f;
            float dy = (float)y - (float)(OSD_TEX / 2) + 0.5f;
            float d  = sqrtf(dx * dx + dy * dy);
            float u  = d / R;
            unsigned char *p = px + ((size_t)y * OSD_TEX + x) * 4;
            float a, nr, nx, ny, nz, lam, opac, tgx, tgy, trans, rd, rgb;
            if (u >= 1.06f) { p[0] = p[1] = p[2] = 0; p[3] = 0; continue; }
            a = (u <= 1.0f) ? 1.0f : 1.0f - (u - 1.0f) / 0.06f;
            if (a < 0.0f) a = 0.0f;
            nr = powf(u, 2.2f);
            nx = (d > 0.001f) ? (dx / d) * nr : 0.0f;
            ny = (d > 0.001f) ? (dy / d) * nr : 0.0f;
            nz = sqrtf(fmaxf(0.0f, 1.0f - nr * nr));
            lam = nx * LX + ny * LY + nz * LZ;
            if (lam < 0.0f) lam = 0.0f;
            opac  = 0.40f + 0.60f * powf(u, 1.8f);      // see-through middle
            tgx   = (dx - 0.20f * R) / (0.62f * R);
            tgy   = (dy - 0.24f * R) / (0.62f * R);
            trans = expf(-(tgx * tgx + tgy * tgy) * 1.35f);
            rgb   = 0.26f + 0.52f * lam + 0.46f * trans;
            rd    = (u - 0.80f) / 0.20f;                // saturated dark rim
            if (rd < 0.0f) rd = 0.0f;
            if (rd > 1.0f) rd = 1.0f;
            rgb *= 1.0f - 0.62f * rd * rd;
            if (rgb > 1.0f) rgb = 1.0f;
            if (rgb < 0.0f) rgb = 0.0f;
            if (opac > 1.0f) opac = 1.0f;
            p[0] = p[1] = p[2] = (unsigned char)(rgb * 255.0f + 0.5f);
            p[3] = (unsigned char)(a * opac * 255.0f + 0.5f);
        }
    }
    t = upload_rgba(px);
    SDL_free(px);
    return t;
}

// Internal scatter, drawn ADDITIVELY in the BUTTON's own hue (not white) so
// the colour deepens from inside instead of washing toward grey.
static GLuint make_caustic(void) {
    unsigned char *px = (unsigned char *)SDL_malloc(OSD_TEX * OSD_TEX * 4);
    const float R = 104.0f;
    int x, y;
    GLuint t;
    if (px == NULL) return 0;
    for (y = 0; y < OSD_TEX; y++) {
        for (x = 0; x < OSD_TEX; x++) {
            float dx = (float)x - (float)(OSD_TEX / 2) + 0.5f;
            float dy = (float)y - (float)(OSD_TEX / 2) + 0.5f;
            float d  = sqrtf(dx * dx + dy * dy);
            float u  = d / R;
            unsigned char *p = px + ((size_t)y * OSD_TEX + x) * 4;
            float cgx, cgy, c;
            p[0] = p[1] = p[2] = 255;
            if (u >= 1.0f) { p[3] = 0; continue; }
            cgx = (dx - 0.16f * R) / (0.50f * R);
            cgy = (dy - 0.22f * R) / (0.50f * R);
            c = expf(-(cgx * cgx + cgy * cgy) * 1.6f) * 0.85f;
            if (u > 0.86f) c *= 1.0f - (u - 0.86f) / 0.14f;
            if (c < 0.0f) c = 0.0f;
            if (c > 1.0f) c = 1.0f;
            p[3] = (unsigned char)(c * 255.0f + 0.5f);
        }
    }
    t = upload_rgba(px);
    SDL_free(px);
    return t;
}

// Concave recess: the dome's normal flipped inward, so the lit band lands on
// the FAR (lower-right) inner wall instead of the near one. Used for the
// stick base and again, smaller, for the dished top of the thumbstick.
static GLuint make_dish(void) {
    unsigned char *px = (unsigned char *)SDL_malloc(OSD_TEX * OSD_TEX * 4);
    const float R = 104.0f;   // 104/128 == s_disc's 52/64 core:
                              // all control textures share one scale
    int x, y;
    GLuint t;
    if (px == NULL) return 0;
    for (y = 0; y < OSD_TEX; y++) {
        for (x = 0; x < OSD_TEX; x++) {
            float dx = (float)x - (float)(OSD_TEX / 2) + 0.5f;
            float dy = (float)y - (float)(OSD_TEX / 2) + 0.5f;
            float d  = sqrtf(dx * dx + dy * dy);
            float u  = d / R;
            unsigned char *p = px + ((size_t)y * OSD_TEX + x) * 4;
            float a, nr, nx, ny, nz, diff, shade;
            if (u >= 1.04f) { p[0] = p[1] = p[2] = 0; p[3] = 0; continue; }
            a = (u <= 1.0f) ? 1.0f : 1.0f - (u - 1.0f) / 0.04f;
            if (a < 0.0f) a = 0.0f;
            nr = powf(u, 1.6f);
            nx = (d > 0.001f) ? -(dx / d) * nr : 0.0f;   // inward-facing
            ny = (d > 0.001f) ? -(dy / d) * nr : 0.0f;
            nz = sqrtf(fmaxf(0.0f, 1.0f - nr * nr));
            diff = nx * LX + ny * LY + nz * LZ;
            if (diff < 0.0f) diff = 0.0f;
            shade = 0.22f + 0.70f * diff;
            if (u > 0.93f) {                     // outer lip catches the light
                float tt = (u - 0.93f) / 0.07f;
                shade += 0.35f * tt * (1.0f - u);
            }
            if (shade > 1.0f) shade = 1.0f;
            if (shade < 0.0f) shade = 0.0f;
            p[0] = p[1] = p[2] = (unsigned char)(shade * 255.0f + 0.5f);
            p[3] = (unsigned char)(a * 255.0f + 0.5f);
        }
    }
    t = upload_rgba(px);
    SDL_free(px);
    return t;
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

static void draw_tex(GLuint tex, OsdRect r, float cr, float cg, float cb,
                     float ca) {
    float ax, ay, bx, by;
    if (tex == 0) return;
    to_ndc(r, &ax, &ay, &bx, &by);
    ewdx_immediate_quad(tex, ax, ay, bx, by, 0.0f, 0.0f, 1.0f, 1.0f,
                        cr, cg, cb, ca);
}

static void draw_disc(OsdRect r, float cr, float cg, float cb, float ca) {
    draw_tex(s_disc, r, cr, cg, cb, ca);
}

// One pad button in the currently selected skin:
//   0 JEWEL (default) translucent body + hue-tinted inner scatter + gloss
//   1 GLASS            opaque shaded dome + white gloss
//   2 FLAT             the original v27.4 tinted disc + hard blob
static void draw_glass_button(int cx, int cy, int r, float cr, float cg,
                              float cb, int held, const char *label,
                              int label_size) {
    int sink = held ? (r / 14 + 1) : 0;
    int by = cy + sink;
    (void)label_size;
    draw_disc(centered_rect(cx, cy + r / 8 + 2, r + r / 5),
              0.0f, 0.0f, 0.0f, held ? 0.22f : 0.42f);
    draw_disc(centered_rect(cx, by, r + r / 6 + 2), 0.04f, 0.04f, 0.06f, 0.92f);
    if (s_style == 0) {
        draw_tex(s_jewel, centered_rect(cx, by, r), cr, cg, cb,
                 held ? 1.0f : 0.96f);
        ewdx_apply_blend(EWDX_BLEND_ADD);
        draw_tex(s_caus, centered_rect(cx, by, r), cr, cg, cb,
                 held ? 0.95f : 0.78f);
        draw_tex(s_spec, centered_rect(cx, by, r), 1.0f, 1.0f, 1.0f,
                 held ? 0.95f : 0.72f);
        ewdx_apply_blend(EWDX_BLEND_ALPHA);
    } else {
        draw_tex(s_glass, centered_rect(cx, by, r), cr, cg, cb,
                 held ? 1.0f : 0.93f);
        ewdx_apply_blend(EWDX_BLEND_ADD);
        draw_tex(s_spec, centered_rect(cx, by, r), 1.0f, 1.0f, 1.0f,
                 held ? 0.90f : 0.62f);
        ewdx_apply_blend(EWDX_BLEND_ALPHA);
    }
    if (label != NULL) {
        // v27.9: screen-px mapping. These letters used to map through game
        // space and land off-screen, which is why the buttons were blank.
        // Sized to the button so the glyph sits inside the moulded face.
        int ls = clampi(r * 62 / 100, 10, 44);
        ewdx_text_label_px(label, cx + 1, by + 2, ls, s_dw, s_dh,
                           0.0f, 0.0f, 0.0f, 0.55f);
        ewdx_text_label_px(label, cx, by, ls, s_dw, s_dh, 1.0f, 1.0f, 1.0f,
                           held ? 1.0f : 0.95f);
    }
}

// Small top-row chip (MENU / HIDE / SKIN).
static void draw_chip(OsdRect rc, const char *label, int lit) {
    OsdRect sh = rc;
    int ls;
    sh.y += 3;
    draw_disc(sh, 0.0f, 0.0f, 0.0f, 0.40f);
    draw_tex(s_glass, rc, 0.26f, 0.26f, 0.30f, lit ? 0.98f : 0.86f);
    ewdx_apply_blend(EWDX_BLEND_ADD);
    draw_tex(s_spec, rc, 1.0f, 1.0f, 1.0f, lit ? 0.50f : 0.34f);
    ewdx_apply_blend(EWDX_BLEND_ALPHA);
    // v27.9: screen-px mapping, and sized to FIT. The old 2/3-of-height size
    // was already too wide for the chip before the space bug doubled it.
    ls = clampi(rc.h * 42 / 100, 10, 26);
    ewdx_text_label_px(label, rc.x + rc.w / 2 + 1, rc.y + rc.h / 2 + 2, ls,
                       s_dw, s_dh, 0.0f, 0.0f, 0.0f, 0.50f);
    ewdx_text_label_px(label, rc.x + rc.w / 2, rc.y + rc.h / 2, ls,
                       s_dw, s_dh, 1.0f, 1.0f, 1.0f, 0.96f);
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
        s_disc  = make_disc();   // v27.1: lazy (GL context live here)
        s_glass = make_glass();  // v27.5
        s_spec  = make_spec();
        s_dish  = make_dish();
        s_jewel = make_jewel();    // v27.6
        s_caus  = make_caustic();
        osd_journal("osd: glass control textures built");
    }
    if (s_disc == 0 || s_glass == 0 || s_spec == 0 || s_dish == 0 ||
        s_jewel == 0 || s_caus == 0) return;
    ewdx_flush();
    // v27.4: draw across the FULL drawable (the game letterbox viewport would
    // clip the screen-corner controls). Save -> fullscreen -> restore.
    glGetIntegerv(GL_VIEWPORT, old_vp);
    glViewport(0, 0, s_dw, s_dh);
    ewdx_apply_blend(EWDX_BLEND_ALPHA);  // translucent glass (mode 1)
    if (!s_hidden) {
        // Stick: dished base + black rubber knob with the red top.
        {
            int bx = stick_cx(), byc = stick_cy(), R = base_r();
            int kx = bx, ky = byc, kr = R * 11 / 20;
            draw_disc(centered_rect(bx, byc + R / 12, R + R / 8),
                      0.0f, 0.0f, 0.0f, 0.45f);
            draw_disc(centered_rect(bx, byc, R + R / 12 + 2),
                      0.04f, 0.04f, 0.06f, 0.90f);
            draw_tex(s_dish, centered_rect(bx, byc, R), 0.30f, 0.30f, 0.34f, 0.82f);
            if (s_stick.used) { kx += (int)s_stick.cx; ky -= (int)s_stick.cy; }
            draw_disc(centered_rect(kx, ky + kr / 7, kr + kr / 7),
                      0.0f, 0.0f, 0.0f, 0.50f);
            draw_tex(s_glass, centered_rect(kx, ky, kr), 0.15f, 0.15f, 0.18f, 0.99f);
            draw_tex(s_dish, centered_rect(kx, ky, kr * 74 / 100),
                     0.82f, 0.11f, 0.11f, 0.97f);
            ewdx_apply_blend(EWDX_BLEND_ADD);
            draw_tex(s_spec, centered_rect(kx, ky, kr), 1.0f, 1.0f, 1.0f,
                     s_stick.used ? 0.40f : 0.28f);
            ewdx_apply_blend(EWDX_BLEND_ALPHA);
        }
        // Diamond: blue C (left), green A (top), red Z (right), yellow X (bottom)
        draw_glass_button(dia_cx() - dia_d(), dia_cy(), btn_r(),
                          0.20f, 0.45f, 0.95f, s_btns[2].held != 0, "C", 0);
        draw_glass_button(dia_cx(), dia_cy() - dia_d(), btn_r(),
                          0.15f, 0.80f, 0.25f, s_btns[3].held != 0, "A", 0);
        draw_glass_button(dia_cx() + dia_d(), dia_cy(), btn_r(),
                          0.95f, 0.20f, 0.20f, s_btns[0].held != 0, "Z", 0);
        draw_glass_button(dia_cx(), dia_cy() + dia_d(), btn_r(),
                          0.98f, 0.72f, 0.10f, s_btns[1].held != 0, "X", 0);
        draw_glass_button(dia_cx() - mini_dx(), mini_cy(), mini_r(),
                          0.90f, 0.88f, 0.25f, s_btns[4].held != 0, "S", 0);
        draw_glass_button(dia_cx() + mini_dx(), mini_cy(), mini_r(),
                          0.90f, 0.88f, 0.25f, s_btns[5].held != 0, "D", 0);
    }
    // Top row. These stay up even while the pad is hidden -- otherwise SHOW
    // could never be pressed again.
    r = menu_rect();
    draw_chip(r, "MENU", s_menu_held);
    draw_chip(hide_rect(), s_hidden ? "SHOW" : "HIDE", s_hide_used);
    draw_chip(style_rect(), style_name(), s_style_used);
    draw_chip(fill_rect(), fill_name(), s_fill_used);
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
        (s_menu_used && s_menu_id == id) ||
        (s_hide_used && s_hide_id == id) ||
        (s_style_used && s_style_id == id) ||
        (s_fill_used && s_fill_id == id)) return 1;
    for (i = 0; i < 6; i++) if (s_btns[i].used && s_btns[i].id == id) return 1;
    // v27.7: the top-left chips are tested FIRST and stay live while the
    // pad is hidden -- otherwise SHOW could never be pressed again.
    // Both act on the DOWN edge and then hold their finger slot, so a
    // press-and-hold toggles once instead of cycling every frame.
    if (!s_hide_used && hit(hide_rect(), x, y)) {
        s_hide_used = 1;
        s_hide_id = id;
        s_hidden = s_hidden ? 0 : 1;
        release_all_effect();
        osd_journal(s_hidden ? "osd: pad hidden (user)"
                             : "osd: pad shown (user)");
        return 1;
    }
    if (!s_style_used && hit(style_rect(), x, y)) {
        char msg[64];
        s_style_used = 1;
        s_style_id = id;
        s_style = (s_style + 1) % 2;
        snprintf(msg, sizeof(msg), "osd: skin -> %s", style_name());
        osd_journal(msg);
        return 1;
    }
    if (!s_fill_used && hit(fill_rect(), x, y)) {
        char msg[64];
        s_fill_used = 1;
        s_fill_id = id;
        ewdx_set_screen_fill(!ewdx_get_screen_fill());
        // Re-fit immediately so the change is visible on this very frame.
        ewdx_apply_screen_viewport();
        snprintf(msg, sizeof(msg), "osd: screen %s", fill_name());
        osd_journal(msg);
        return 1;
    }
    // MENU (top-right) also stays live while hidden.
    r = menu_rect();
    if (hit(r, x, y)) {
        s_menu_used = 1;
        s_menu_id = id;
        s_menu_held = 1;
        osd_journal("osd: press MENU (ESC)");
        return 1;
    }
    // Pad controls are inert while hidden: fall through so the taps reach
    // the game instead of being swallowed by invisible hit rects.
    if (s_hidden) return 0;
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
    if (s_hide_used && s_hide_id == id) {
        s_hide_used = 0; s_hide_id = 0; consumed = 1;
    }
    if (s_style_used && s_style_id == id) {
        s_style_used = 0; s_style_id = 0; consumed = 1;
    }
    if (s_fill_used && s_fill_id == id) {
        s_fill_used = 0; s_fill_id = 0; consumed = 1;
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
    if (s_disc)  { glDeleteTextures(1, &s_disc);  s_disc  = 0; }
    if (s_glass) { glDeleteTextures(1, &s_glass); s_glass = 0; }
    if (s_spec)  { glDeleteTextures(1, &s_spec);  s_spec  = 0; }
    if (s_dish)  { glDeleteTextures(1, &s_dish);  s_dish  = 0; }
    if (s_jewel) { glDeleteTextures(1, &s_jewel); s_jewel = 0; }
    if (s_caus)  { glDeleteTextures(1, &s_caus);  s_caus  = 0; }
    s_inited = 0;
}
