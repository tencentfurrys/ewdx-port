// ewdx_paths.cpp - filesDir storage + asset fallback (see header).
#include "ewdx_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#ifdef __ANDROID__
#include <SDL_system.h>
#endif

#define EWDX_READ_MAX (32L << 20)

static char files_dir[1024] = { 0 };

void ewdx_paths_init(void) {
#ifdef __ANDROID__
    const char *p = SDL_AndroidGetInternalStoragePath();
    if (p != NULL && p[0] != '\0') {
        strncpy(files_dir, p, sizeof(files_dir) - 1);
        files_dir[sizeof(files_dir) - 1] = '\0';
    }
#else
    files_dir[0] = '\0';  // host: resolve relative to working directory
#endif
}

const char *ewdx_files_dir(void) {
    return files_dir;
}

void ewdx_resolve(const char *hsp_path, char *out, size_t cap) {
    char norm[1024];
    size_t i, n;
    int absolute = 0;
    if (cap == 0) return;
    if (hsp_path == NULL) hsp_path = "";
    n = strlen(hsp_path);
    if (n >= sizeof(norm)) n = sizeof(norm) - 1;
    for (i = 0; i < n; i++) {
        char c = hsp_path[i];
        norm[i] = (c == '\\') ? '/' : c;
    }
    norm[n] = '\0';
    // absolute: leading '/' or drive-letter ':' within the first 3 chars
    if (norm[0] == '/') absolute = 1;
    {
        size_t k;
        for (k = 0; k < n && k < 3; k++) {
            if (norm[k] == ':') { absolute = 1; break; }
        }
    }
    if (absolute || files_dir[0] == '\0') {
        strncpy(out, norm, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    {
        int w = snprintf(out, cap, "%s/%s", files_dir, norm);
        if (w < 0 || (size_t)w >= cap) out[cap - 1] = '\0';
    }
}

uint8_t *ewdx_read_file(const char *hsp_path, long *len_out) {
    char path[2048];
    FILE *fp;
    long n;
    uint8_t *buf;
    if (len_out != NULL) *len_out = 0;
    if (hsp_path == NULL) return NULL;
    ewdx_resolve(hsp_path, path, sizeof(path));
    fp = fopen(path, "rb");
    if (fp != NULL) {
        fseek(fp, 0, SEEK_END);
        n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (n <= 0 || n > EWDX_READ_MAX) { fclose(fp); return NULL; }
        buf = (uint8_t *)malloc((size_t)n);
        if (buf == NULL) { fclose(fp); return NULL; }
        if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
            free(buf); fclose(fp); return NULL;
        }
        fclose(fp);
        if (len_out != NULL) *len_out = n;
        return buf;
    }
    // APK-asset fallback (Android SDL intercepts relative paths here).
    {
        char rel[1024];
        size_t i, m;
        SDL_RWops *rw;
        Sint64 sz;
        m = strlen(hsp_path);
        if (m >= sizeof(rel)) m = sizeof(rel) - 1;
        for (i = 0; i < m; i++) {
            rel[i] = (hsp_path[i] == '\\') ? '/' : hsp_path[i];
        }
        rel[m] = '\0';
        rw = SDL_RWFromFile(rel, "rb");
        if (rw == NULL) return NULL;
        sz = SDL_RWsize(rw);
        if (sz <= 0 || sz > EWDX_READ_MAX) { SDL_RWclose(rw); return NULL; }
        buf = (uint8_t *)malloc((size_t)sz);
        if (buf == NULL) { SDL_RWclose(rw); return NULL; }
        if (SDL_RWread(rw, buf, 1, (size_t)sz) != (size_t)sz) {
            free(buf); SDL_RWclose(rw); return NULL;
        }
        SDL_RWclose(rw);
        if (len_out != NULL) *len_out = (long)sz;
        return buf;
    }
}
