// ewdx_text.cpp - SDL_ttf text path (see header for design + provenance).
#include "ewdx_text.h"
#include "ewdx_batch.h"
#include "ewdx_gles.h"
#include "ewdx_sjis_tab.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <string.h>

#include "ewdx_log.h"

#define EWDX_TEXT_FONTS 4
#define EWDX_TEXT_CACHE 16
#define EWDX_TEXT_MIN_PT 8
#define EWDX_TEXT_MAX_PT 72

// --- Shift-JIS -> UTF-8 (game-subset table + algorithmic ranges) ---

static uint16_t sjis_lookup(uint16_t sj) {
    int lo = 0;
    int hi = (int)(sizeof(EWDX_SJIS_TAB) / sizeof(EWDX_SJIS_TAB[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (EWDX_SJIS_TAB[mid].sjis == sj) return EWDX_SJIS_TAB[mid].uni;
        if (EWDX_SJIS_TAB[mid].sjis < sj) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0xFFFD;
}

// Returns UTF-8 bytes written excluding NUL (always NUL-terminates if cap>0).
static int sjis_to_utf8(const char *src, char *dst, int cap) {
    const uint8_t *s = (const uint8_t *)src;
    int n = 0;
    if (cap <= 0) return 0;
    while (*s != '\0' && n + 4 < cap) {
        uint32_t u;
        uint8_t b = *s;
        if (b < 0x80) { u = b; s++; }
        else if (b >= 0xA1 && b <= 0xDF) { u = 0xFF61u + (uint32_t)(b - 0xA1); s++; }
        else if (((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) && s[1] != '\0') {
            u = sjis_lookup((uint16_t)(((uint16_t)b << 8) | s[1]));
            s += 2;
        } else { u = 0xFFFD; s++; }  // stray lead/trailing byte
        if (u < 0x80) {
            dst[n++] = (char)u;
        } else if (u < 0x800) {
            dst[n++] = (char)(0xC0 | (u >> 6));
            dst[n++] = (char)(0x80 | (u & 0x3F));
        } else {
            dst[n++] = (char)(0xE0 | (u >> 12));
            dst[n++] = (char)(0x80 | ((u >> 6) & 0x3F));
            dst[n++] = (char)(0x80 | (u & 0x3F));
        }
    }
    dst[n] = '\0';
    return n;
}

// Public wrapper for shims that cross the SDL/JNI boundary (dialog/title).
int ewdx_sjis_to_utf8(const char *src, char *dst, int cap) {
    return sjis_to_utf8(src, dst, cap);
}

// --- font + string cache ---

typedef struct { int size; TTF_Font *font; int age; } TextFont;
typedef struct {
    int used;
    uint64_t key;   // fnv1a(utf8 line) mixed with size + rgba
    GLuint tex;
    int w, h;
    int age;
} TextEntry;

static TextFont fonts[EWDX_TEXT_FONTS];
static TextEntry cache[EWDX_TEXT_CACHE];
static int font_cur_size = 12;
static int ttf_ok = 0;
static int age_ctr = 1;

static const char *font_candidates[] = {
    "/system/fonts/NotoSansCJK-Regular.ttc",
    "/system/fonts/DroidSansFallback.ttf",
    "/system/fonts/Roboto-Regular.ttf",
    "/system/fonts/NotoSans-Regular.ttf",
    NULL
};

static uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (uint64_t)(uint8_t)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static TTF_Font *font_for_size(int size) {
    int i, victim = 0;
    if (size < EWDX_TEXT_MIN_PT) size = EWDX_TEXT_MIN_PT;
    if (size > EWDX_TEXT_MAX_PT) size = EWDX_TEXT_MAX_PT;
    if (!ttf_ok) {
        if (TTF_Init() != 0) return NULL;
        ttf_ok = 1;
    }
    for (i = 0; i < EWDX_TEXT_FONTS; i++) {
        if (fonts[i].font != NULL && fonts[i].size == size) {
            fonts[i].age = age_ctr++;
            return fonts[i].font;
        }
    }
    for (i = 0; i < EWDX_TEXT_FONTS; i++) {
        if (fonts[i].font == NULL) { victim = i; break; }
        if (fonts[i].age < fonts[victim].age) victim = i;
    }
    if (fonts[victim].font != NULL) {
        TTF_CloseFont(fonts[victim].font);
        fonts[victim].font = NULL;
    }
    {
        const char **p = font_candidates;
        while (*p != NULL) {
            TTF_Font *f = TTF_OpenFont(*p, size);
            if (f != NULL) {
                // v26.1: GDI TextOut parity (PC DGDRAWTEXT = MS Gothic via GDI):
                // 1-bit monochrome rasterization (FT_LOAD_TARGET_MONO ->
                // FT_RENDER_MODE_MONO in SDL_ttf's Solid pipeline). Blended
                // (gray AA) text at 12px reads ~50% alpha on thin strokes and
                // turns into faint gray mush after the buffer-5 x2 upscale.
                TTF_SetFontHinting(f, TTF_HINTING_MONO);
                fonts[victim].font = f;
                fonts[victim].size = size;
                fonts[victim].age = age_ctr++;
                return f;
            }
            p++;
        }
        EWDX_LOGE("DGFONT: no system CJK font found (tried %d candidates)", 4);
    }
    return NULL;
}

static TextEntry *cache_lookup(uint64_t key) {
    int i, victim = 0;
    for (i = 0; i < EWDX_TEXT_CACHE; i++) {
        if (cache[i].used && cache[i].key == key) {
            cache[i].age = age_ctr++;
            return &cache[i];
        }
    }
    for (i = 0; i < EWDX_TEXT_CACHE; i++) {
        if (!cache[i].used) { victim = i; break; }
        if (cache[i].age < cache[victim].age) victim = i;
    }
    if (cache[victim].used) {
        glDeleteTextures(1, &cache[victim].tex);
        cache[victim].used = 0;
    }
    return &cache[victim];  // caller fills + sets used
}

int ewdx_text_font(const char *name, int size) {
    (void)name;  // MS-Gothic/Mincho unavailable; CJK system font covers glyphs
    if (size <= 0) size = 12;
    font_cur_size = size;
    return -1;
}

// Render one line (must not contain '\n') into the string cache; returns the
// entry or NULL. Binary-alpha mono glyphs (v26.1 GDI parity).
static TextEntry *text_line_entry(TTF_Font *font, const char *line, uint32_t rgba) {
    uint64_t key;
    TextEntry *e;
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *sf, *cv;
    int W, H;

    key = fnv1a(line) ^ ((uint64_t)(uint32_t)font_cur_size << 32)
        ^ ((uint64_t)rgba << 1);
    e = cache_lookup(key);
    if (e->used) return e;

    // v26.1: SOLID render = 1-bit mono glyphs (GDI TextOut parity); the
    // tint comes from the vertex color at draw. Binary alpha (0 or 255)
    // so thin strokes stay FULLY white through the x2 buffer upscale —
    // the old Blended path dimmed them to gray (owner: "very little
    // visible" on the difficulty descriptions).
    sf = TTF_RenderUTF8_Solid(font, line, white);
    if (sf == NULL) return NULL;
    cv = NULL;
    if (sf->format->BytesPerPixel != 1) {
        cv = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ABGR8888, 0);
        if (cv == NULL) { SDL_FreeSurface(sf); return NULL; }
    }
    W = sf->w; H = sf->h;
    if (W <= 0 || H <= 0 || W > 2048 || H > 512) {
        if (cv) SDL_FreeSurface(cv);
        SDL_FreeSurface(sf);
        return NULL;
    }
    // top-down rows (TTF surfaces are not BMP-flipped); memory RGBA order
    {
        int pitch = (cv != NULL) ? cv->pitch : sf->pitch;
        uint8_t *base = (uint8_t *)((cv != NULL) ? cv->pixels : sf->pixels);
        uint8_t *flat = (uint8_t *)SDL_malloc((size_t)W * (size_t)H * 4u);
        int yy;
        if (flat == NULL) {
            if (cv) SDL_FreeSurface(cv);
            SDL_FreeSurface(sf);
            return NULL;
        }
        if (cv != NULL && cv->format->BytesPerPixel != 4) {
            SDL_free(flat); SDL_FreeSurface(cv); SDL_FreeSurface(sf);
            return NULL;
        }
        for (yy = 0; yy < H; yy++) {
            uint8_t *srow = base + (size_t)yy * (size_t)pitch;
            uint8_t *drow = flat + (size_t)yy * (size_t)W * 4u;
            if (cv != NULL) {
                int xx;
                for (xx = 0; xx < W; xx++) {
                    uint32_t v;
                    uint8_t r, g, b, a;
                    memcpy(&v, srow + xx * 4, 4);
                    SDL_GetRGBA(v, cv->format, &r, &g, &b, &a);
                    drow[xx * 4 + 0] = r; drow[xx * 4 + 1] = g;
                    drow[xx * 4 + 2] = b; drow[xx * 4 + 3] = a;
                }
            } else {
                // 1-bit mono mapped to binary alpha: glyph = white/opaque
                int xx;
                for (xx = 0; xx < W; xx++) {
                    uint8_t on = srow[xx];
                    drow[xx * 4 + 0] = 255; drow[xx * 4 + 1] = 255;
                    drow[xx * 4 + 2] = 255;
                    drow[xx * 4 + 3] = (on != 0) ? 255 : 0;
                }
            }
        }
        SDL_FreeSurface(sf);
        if (cv) SDL_FreeSurface(cv);
        glGenTextures(1, &e->tex);
        glBindTexture(GL_TEXTURE_2D, e->tex);
        // v25.4 point-sampling parity applies to text too (GDI blits are
        // unfiltered; the x2 buffer upscale is the game-art NEAREST path)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, flat);
        SDL_free(flat);
        e->w = W; e->h = H; e->key = key; e->used = 1; e->age = age_ctr++;
    }
    return e;
}

int ewdx_text_draw(const char *sjis, int x, int y) {
    char utf8[1024];
    TTF_Font *font;
    float cr, cg, cb, ca;
    uint32_t rgba;
    int tgtW, tgtH;
    int line_skip;
    const char *line;
    int row = 0;

    if (sjis == NULL || sjis[0] == '\0') return -1;
    font = font_for_size(font_cur_size);
    if (font == NULL) return -1;  // degraded: menus stay non-fatal
    sjis_to_utf8(sjis, utf8, (int)sizeof(utf8));
    if (utf8[0] == '\0') return -1;

    // v27: DGDRAWTEXT strings embed real '\n' (0x0A) — the chara-select guide
    // blocks are "[Z](aerial) :\n\n\nDOWN+[X] :" style COLUMN LISTS. SDL_ttf's
    // Solid mode has no newline support, so the old single-line render sent
    // every 0x0A to .notdef (the owner's "boxes") and overprinted all columns
    // into one long strip. GDI TextOut draws per line the same way; the game
    // builds columns from \n runs. Split + per-line cache + line_skip advance.
    tgtW = (ewdx.target == 0) ? ewdx.scr_w : ewdx.buf[ewdx.target].w;
    tgtH = (ewdx.target == 0) ? ewdx.scr_h : ewdx.buf[ewdx.target].h;
    if (tgtW <= 0) tgtW = ewdx.scr_w;
    if (tgtH <= 0) tgtH = ewdx.scr_h;
    if (tgtW <= 0 || tgtH <= 0) return -1;

    cr = ewdx.st.r / 255.0f; cg = ewdx.st.g / 255.0f;
    cb = ewdx.st.b / 255.0f; ca = ewdx.st.a / 255.0f;
    rgba = ((uint32_t)(ewdx.st.r & 0xFF) << 24) | ((uint32_t)(ewdx.st.g & 0xFF) << 16)
         | ((uint32_t)(ewdx.st.b & 0xFF) << 8) | (uint32_t)(ewdx.st.a & 0xFF);
    line_skip = TTF_FontLineSkip(font);

    line = utf8;
    for (;;) {
        int len = (int)strlen(line);
        const char *nl = (const char *)memchr(line, '\n', (size_t)len);
        int this_len = (nl != NULL) ? (int)(nl - line) : len;
        char save = '\0';
        TextEntry *e;
        float nx0, ny0, nx1, ny1;

        if (nl != NULL) { save = line[this_len]; ((char *)line)[this_len] = '\0'; }
        if (this_len > 0) {  // empty line = vertical gap only
            e = text_line_entry(font, line, rgba);
            if (e != NULL) {
                // same NDC convention as emit_quad ((X+0.5)/W*2-1) so text
                // registers against sprites drawn at the same coordinates
                nx0 = ((float)x + 0.5f) / (float)tgtW * 2.0f - 1.0f;
                ny0 = 1.0f - ((float)y + row * line_skip + 0.5f) / (float)tgtH * 2.0f;
                nx1 = ((float)(x + e->w) + 0.5f) / (float)tgtW * 2.0f - 1.0f;
                ny1 = 1.0f - ((float)(y + row * line_skip + e->h) + 0.5f) / (float)tgtH * 2.0f;
                ewdx_flush();  // foreign texture: drain batch first
                ewdx_immediate_quad(e->tex, nx0, ny0, nx1, ny1,
                                    0.0f, 0.0f, 1.0f, 1.0f, cr, cg, cb, ca);
            }
        }
        if (nl != NULL) {
            ((char *)line)[this_len] = save;
            line = nl + 1;
            row++;
            if (row >= 32) break;  // script max is 5; hard cap for safety
            continue;
        }
        break;
    }
    return -1;
}

// v27.2: OSD control labels (cosmetic, never fatal). Renders one UTF-8 line
// centered at (cx,cy) with its own font size + fixed tint, without disturbing
// the game's font_cur_size or draw state beyond the immediate-quad path's
// own restore discipline (caller restores blend/color).
int ewdx_text_label(const char *utf8, int cx, int cy, int size,
                    float cr, float cg, float cb, float ca) {
    TTF_Font *font;
    TextEntry *e;
    int tgtW, tgtH, old_size;
    float nx0, ny0, nx1, ny1;

    if (utf8 == NULL || utf8[0] == '\0') return -1;
    old_size = font_cur_size;
    font_cur_size = size;
    font = font_for_size(size);
    font_cur_size = old_size;
    if (font == NULL) return -1;
    tgtW = (ewdx.target == 0) ? ewdx.scr_w : ewdx.buf[ewdx.target].w;
    tgtH = (ewdx.target == 0) ? ewdx.scr_h : ewdx.buf[ewdx.target].h;
    if (tgtW <= 0) tgtW = ewdx.scr_w;
    if (tgtH <= 0) tgtH = ewdx.scr_h;
    if (tgtW <= 0 || tgtH <= 0) return -1;
    e = text_line_entry(font, utf8, 0xFFFFFFFFu);
    if (e == NULL) return -1;
    nx0 = ((float)(cx - e->w / 2) + 0.5f) / (float)tgtW * 2.0f - 1.0f;
    ny0 = 1.0f - ((float)(cy - e->h / 2) + 0.5f) / (float)tgtH * 2.0f;
    nx1 = ((float)(cx - e->w / 2 + e->w) + 0.5f) / (float)tgtW * 2.0f - 1.0f;
    ny1 = 1.0f - ((float)(cy - e->w / 2 + e->h) + 0.5f) / (float)tgtH * 2.0f;
    ewdx_flush();
    ewdx_immediate_quad(e->tex, nx0, ny0, nx1, ny1, 0.0f, 0.0f, 1.0f, 1.0f,
                        cr, cg, cb, ca);
    return -1;
}


// v27.9: OSD label in REAL SCREEN px.
//
// ewdx_text_label() maps through the CURRENT RENDER TARGET (game space, e.g.
// 640x480). The OSD does not draw in that space -- it swaps in the FULL
// drawable viewport before drawing. Feeding it screen-px coords therefore
// divided by the wrong dimensions: chips near the origin came out at roughly
// double scale ("HIDE" spilling across the screen), and the button letters at
// ~(1130,560) mapped past +1.0 in NDC and were clipped away entirely.
//
// This variant takes the drawable dims explicitly. It also fixes a real typo
// carried by the original: ny1 computed its top edge from e->w / 2 instead of
// e->h / 2, so every glyph quad whose width differed from its height was
// vertically wrong.
int ewdx_text_label_px(const char *utf8, int cx, int cy, int size,
                       int dw, int dh,
                       float cr, float cg, float cb, float ca) {
    TTF_Font *font;
    TextEntry *e;
    int old_size;
    float nx0, ny0, nx1, ny1;

    if (utf8 == NULL || utf8[0] == '\0') return -1;
    if (dw <= 0 || dh <= 0) return -1;
    old_size = font_cur_size;
    font_cur_size = size;
    font = font_for_size(size);
    font_cur_size = old_size;
    if (font == NULL) return -1;
    e = text_line_entry(font, utf8, 0xFFFFFFFFu);
    if (e == NULL) return -1;
    nx0 = ((float)(cx - e->w / 2)) / (float)dw * 2.0f - 1.0f;
    ny0 = 1.0f - ((float)(cy - e->h / 2)) / (float)dh * 2.0f;
    nx1 = ((float)(cx - e->w / 2 + e->w)) / (float)dw * 2.0f - 1.0f;
    ny1 = 1.0f - ((float)(cy - e->h / 2 + e->h)) / (float)dh * 2.0f;
    ewdx_flush();
    ewdx_immediate_quad(e->tex, nx0, ny0, nx1, ny1, 0.0f, 0.0f, 1.0f, 1.0f,
                        cr, cg, cb, ca);
    return 0;
}

void ewdx_text_shutdown(void) {
    int i;
    for (i = 0; i < EWDX_TEXT_CACHE; i++) {
        if (cache[i].used) {
            glDeleteTextures(1, &cache[i].tex);
            cache[i].used = 0;
        }
    }
    for (i = 0; i < EWDX_TEXT_FONTS; i++) {
        if (fonts[i].font != NULL) {
            TTF_CloseFont(fonts[i].font);
            fonts[i].font = NULL;
        }
    }
    if (ttf_ok) { TTF_Quit(); ttf_ok = 0; }
}
