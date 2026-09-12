// ewdx_text.h - DGFONT/DGDRAWTEXT via SDL_ttf + string-cache immediate quads.
//
// Strings in the AX dump are Shift-JIS; SDL_ttf wants UTF-8. Conversion uses
// ewdx_sjis_tab.h (304 CP932 pairs observed in the dump, validated) +
// algorithmic halfwidth-katakana/ASCII, U+FFFD fallback. Runtime strings are
// composed from the same bytes, so coverage holds.
// Font selection: the game asks for MS-Gothic/Mincho (SJIS names, unavailable
// on Android and lossy in the dump anyway). We resolve a CJK-capable system
// font instead: NotoSansCJK-Regular.ttc, DroidSansFallback.ttf,
// Roboto-Regular.ttf, NotoSans-Regular.ttf (first that opens). Sizes cached
// per request (clamped 8..72); rendered pixel size is 1:1 like GDI text.
// Draw path: 16-entry string cache (utf8 hash + size + DGCOLOR) -> GL texture
// -> immediate quad (flush-first, white glyphs tinted by vertex color).
// Always returns -1 (menus stay non-fatal if fonts are missing).
#ifndef __EWDX_TEXT_H
#define __EWDX_TEXT_H

int ewdx_text_font(const char *name, int size);    // DGFONT
int ewdx_text_draw(const char *sjis, int x, int y);  // DGDRAWTEXT (absolute px)
void ewdx_text_shutdown(void);  // called from ewdx_shutdown (before SDL_Quit)

#endif
