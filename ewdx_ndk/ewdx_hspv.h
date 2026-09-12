// ewdx_hspv.h - standalone 'hspv' variable-dump reader (hspda-compatible)
// Format source: OpenHSP src/plugins/win32/hspda/Hspda.cpp
//   HSP3VARFILECODE "hspv", HSP3VARFILEVER 0x1000, FX tag 0x55AA0000
// Verified by hspv_harness.py against real game files:
//   data/map/*.map (13 vars), data/mold/*.mol (3 vars), data/mot/*.mot (5 vars)
// No HSP runtime dependency: caller supplies plain buffers. INT/DOUBLE/LABEL
// payloads are raw memcpy; STR payloads are per-element [tag,size,bytes].
#ifndef __EWDX_HSPV_H
#define __EWDX_HSPV_H

#include <stdint.h>

#define EWDX_HSPV_MAGIC 0x76707368u   // 'hspv' little-endian
#define EWDX_HSPV_VER   0x1000u
#define EWDX_HSPV_FX    0x55AA0000u

#define EWDX_VAR_LABEL  1
#define EWDX_VAR_STR    2
#define EWDX_VAR_DOUBLE 3
#define EWDX_VAR_INT    4
#define EWDX_VAR_STRUCT 5

#pragma pack(push,1)
typedef struct {           // 16 bytes
    uint32_t magic;        // 'hspv'
    uint32_t ver;          // 0x1000
    uint32_t num;          // entry count
    uint32_t pt_data;      // offset of data block (= 16 + num*64)
} EwdxHspvHead;

typedef struct {           // PVal master descriptor, 48 bytes (32-bit layout)
    int16_t  flag;         // EWDX_VAR_* (2=str 3=double 4=int)
    int16_t  mode;
    uint32_t len[5];       // array dims; len[0] unused
    uint32_t size;         // payload bytes (storage) or ptr-table bytes (flex)
    uint32_t pt, master;
    uint16_t support;
    int16_t  arraycnt;
    uint32_t offset, arraymul;
} EwdxPVal;

typedef struct {           // 64 bytes
    uint32_t name;         // data-block-relative offset of NUL var name
    uint32_t data;         // data-block-relative offset of payload
    uint32_t opt, encode;  // reserved (0 in game files)
    EwdxPVal master;
} EwdxHspvEntry;
#pragma pack(pop)

// Minimal zero-dependency loader (implemented in ewdx_hspv.cpp):
//   returns entry count, or <0 on format error.
//   name_out[i] -> NUL name; payload_out[i]/plen_out[i] -> raw payload span.
//   STR payloads: walk [u32 tag==FX][u32 size][bytes] per element (count=size/4).
int  ewdx_hspv_open(const char *path, const uint8_t **img_out, int *len_out);
int  ewdx_hspv_vars(const uint8_t *img, int len,
                    const EwdxHspvEntry **ents_out);
const char   *ewdx_hspv_name(const uint8_t *img, const EwdxHspvEntry *e);
const uint8_t *ewdx_hspv_data(const uint8_t *img, const EwdxHspvEntry *e);

#endif
