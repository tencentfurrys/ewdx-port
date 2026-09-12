// ewdx_hspv.cpp - dependency-free 'hspv' reader (see ewdx_hspv.h)
#include "ewdx_hspv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ewdx_hspv_open(const char *path, const uint8_t **img_out, int *len_out) {
    FILE *fp = fopen(path, "rb");
    long n;
    uint8_t *img;
    const EwdxHspvHead *h;
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < (long)sizeof(EwdxHspvHead)) { fclose(fp); return -2; }
    img = (uint8_t *)malloc(n);
    if (!img) { fclose(fp); return -3; }
    if (fread(img, 1, n, fp) != (size_t)n) { free(img); fclose(fp); return -4; }
    fclose(fp);
    h = (const EwdxHspvHead *)img;
    if (h->magic != EWDX_HSPV_MAGIC || h->ver != EWDX_HSPV_VER) { free(img); return -5; }
    if (h->pt_data != 16u + h->num * 64u) { free(img); return -6; }
    if ((long)h->pt_data > n) { free(img); return -7; }
    *img_out = img;
    *len_out = (int)n;
    return (int)h->num;
}

int ewdx_hspv_vars(const uint8_t *img, int len,
                   const EwdxHspvEntry **ents_out) {
    const EwdxHspvHead *h = (const EwdxHspvHead *)img;
    (void)len;
    *ents_out = (const EwdxHspvEntry *)(img + sizeof(EwdxHspvHead));
    return (int)h->num;
}

const char *ewdx_hspv_name(const uint8_t *img, const EwdxHspvEntry *e) {
    const EwdxHspvHead *h = (const EwdxHspvHead *)img;
    return (const char *)(img + h->pt_data + e->name);
}

const uint8_t *ewdx_hspv_data(const uint8_t *img, const EwdxHspvEntry *e) {
    const EwdxHspvHead *h = (const EwdxHspvHead *)img;
    return img + h->pt_data + e->data;
}
