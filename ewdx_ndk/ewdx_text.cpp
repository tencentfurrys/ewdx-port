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
    uint64_t key;   // fnv1a(utf8) mixed with size + rgba
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

int ewdx_text_draw(const char *sjis, int x, int y) {
    char utf8[1024];
    uint64_t key;
    uint32_t rgba;
    TextEntry *e;
    TTF_Font *font;
    SDL_Surface *sf, *cv;
    float cr, cg, cb, ca;
    int tgtW, tgtH;
    float nx0, ny0, nx1, ny1;

    if (sjis == NULL || sjis[0] == '\0') return -1;
    font = font_for_size(font_cur_size);
    if (font == NULL) return -1;  // degraded: menus stay non-fatal
    sjis_to_utf8(sjis, utf8, (int)sizeof(utf8));
    if (utf8[0] == '\0') return -1;

    cr = ewdx.st.r / 255.0f; cg = ewdx.st.g / 255.0f;
    cb = ewdx.st.b / 255.0f; ca = ewdx.st.a / 255.0f;
    rgba = ((uint32_t)(ewdx.st.r & 0xFF) << 24) | ((uint32_t)(ewdx.st.g & 0xFF) << 16)
         | ((uint32_t)(ewdx.st.b & 0xFF) << 8) | (uint32_t)(ewdx.st.a & 0xFF);
    key = fnv1a(utf8) ^ ((uint64_t)(uint32_t)font_cur_size << 32) ^ ((uint64_t)rgba << 1);
    e = cache_lookup(key);
    if (!e->used) {
        // render white glyphs; tint comes from the vertex color at draw
        SDL_Color white = { 255, 255, 255, 255 };
        int W, H;
        sf = TTF_RenderUTF8_Blended(font, utf8, white);
        if (sf == NULL) return -1;
        cv = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(sf);
        if (cv == NULL) return -1;
        W = cv->w; H = cv->h;
        if (W <= 0 || H <= 0 || W > 2048 || H > 512) {
            SDL_FreeSurface(cv);
            return -1;
        }
        // top-down rows (TTF surfaces are not BMP-flipped); memory RGBA order
        {
            int pitch = cv->pitch;
            uint8_t *base = (uint8_t *)cv->pixels;
            uint8_t *flat = (uint8_t *)SDL_malloc((size_t)W * (size_t)H * 4u);
            int yy;
            if (flat == NULL) { SDL_FreeSurface(cv); return -1; }
            if (cv->format->BytesPerPixel != 4) {
                SDL_free(flat); SDL_FreeSurface(cv); return -1;
            }
            for (yy = 0; yy < H; yy++) {
                uint8_t *srow = base + (size_t)yy * (size_t)pitch;
                uint8_t *drow = flat + (size_t)yy * (size_t)W * 4u;
                for (int xx = 0; xx < W; xx++) {
                    uint32_t v;
                    uint8_t r, g, b, a;
                    memcpy(&v, srow + xx * 4, 4);
                    SDL_GetRGBA(v, cv->format, &r, &g, &b, &a);
                    drow[xx * 4 + 0] = r; drow[xx * 4 + 1] = g;
                    drow[xx * 4 + 2] = b; drow[xx * 4 + 3] = a;
                }
            }
            SDL_FreeSurface(cv);
            glGenTextures(1, &e->tex);
            glBindTexture(GL_TEXTURE_2D, e->tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, flat);
            SDL_free(flat);
            e->w = W; e->h = H; e->key = key; e->used = 1; e->age = age_ctr++;
        }
    }
    tgtW = (ewdx.target == 0) ? ewdx.scr_w : ewdx.buf[ewdx.target].w;
    tgtH = (ewdx.target == 0) ? ewdx.scr_h : ewdx.buf[ewdx.target].h;
    if (tgtW <= 0) tgtW = ewdx.scr_w;
    if (tgtH <= 0) tgtH = ewdx.scr_h;
    if (tgtW <= 0 || tgtH <= 0) return -1;
    // same NDC convention as emit_quad ((X+0.5)/W*2-1) so text registers
    // against sprites drawn at the same coordinates
    nx0 = ((float)x + 0.5f) / (float)tgtW * 2.0f - 1.0f;
    ny0 = 1.0f - ((float)y + 0.5f) / (float)tgtH * 2.0f;
    nx1 = ((float)(x + e->w) + 0.5f) / (float)tgtW * 2.0f - 1.0f;
    ny1 = 1.0f - ((float)(y + e->h) + 0.5f) / (float)tgtH * 2.0f;
    ewdx_flush();  // foreign texture: drain batch first
    ewdx_immediate_quad(e->tex, nx0, ny0, nx1, ny1, 0.0f, 0.0f, 1.0f, 1.0f,
                        cr, cg, cb, ca);
    return -1;
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
