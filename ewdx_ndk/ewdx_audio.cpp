// ewdx_audio.cpp - step (d) audio pipeline.
//
// Provenance: dmm*/ovplay call shapes + loop tables mined from
// artifacts/start_ax_dump.hsp (seplay L3814, bgmload L3892, set_volume L3884);
// millibel volume + +-10000 pan conventions from those call sites.
// Backend: OpenSL ES buffer-queue player (44.1 kHz stereo s16). The mixer +
// streamer are platform-free (C++ <mutex>); EWDX_AUDIO_NULL_BACKEND builds
// them without <SLES/*> so host tests drive ewdx_audio_fill() directly.
//
// Sample-accurate looping WITHOUT decoder seek (stb_vorbis has none):
// playback always starts at sample 0 and pulls sequentially; the first pass
// through [loop_b, loop_e) is cached to RAM, and the wrap jumps to cache[0].
// cache[0] IS sample loop_b by construction -> the wrap is exact.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <mutex>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "thirdparty/stb_vorbis.c"

#include "ewdx_audio.h"
#include "ewdx_log.h"
#include "ewdx_paths.h"
#include "ewdx_log.h"

#ifndef EWDX_AUDIO_NULL_BACKEND
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#endif

#define EWDX_SE_MAX_SLOTS 256
#define EWDX_VOICES 24
#define EWDX_BQ_FRAMES 2048
#define EWDX_FILL_SLICE 512

// ------------------------------------------------------------- helpers ---

static float au_mb_gain(int mb) {
    if (mb <= -10000) return 0.0f;
    if (mb > 600) mb = 600;
    return powf(10.0f, (float)mb / 2000.0f);
}

static void au_pan_gains(int pan, float *gl, float *gr) {
    // Linear balance (DirectSound pan law): center passes both at unity,
    // extremes silence the far side. (NOT equal-power: the original does
    // not attenuate centered sounds, and seplay centers often.)
    float t = (float)pan / 10000.0f;
    if (t < -1.0f) t = -1.0f;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) { *gl = 1.0f; *gr = 1.0f + t; }
    else { *gl = 1.0f - t; *gr = 1.0f; }
}

static int16_t au_clamp_s16(float v) {
    long v2 = lrintf(v);
    if (v2 > 32767) return 32767;
    if (v2 < -32768) return -32768;
    return (int16_t)v2;
}

// Linear resample helper state (32.32 source position).
typedef struct { int64_t pos; } AuRs;

// ---------------------------------------------------------- WAV decode ---
// RIFF/WAVE PCM 8/16-bit mono/stereo, any sane rate -> stereo 44100 s16.
// Returns malloc'd frames (4 bytes each) or NULL.

static int16_t *au_wav_to_mix(const uint8_t *mem, long len, long *out_frames) {
    uint32_t rate = 0;
    int ch = 0, bits = 0;
    const uint8_t *data = NULL;
    long dlen = 0;
    long pos;
    long src_frames, dst_frames, i;
    int64_t step;
    AuRs rs;
    int16_t *out;

    *out_frames = 0;
    if (mem == NULL || len < 44) return NULL;
    if (memcmp(mem, "RIFF", 4) != 0 || memcmp(mem + 8, "WAVE", 4) != 0) return NULL;
    pos = 12;
    while (pos + 8 <= len) {
        uint32_t ck = (uint32_t)mem[pos]
                    | ((uint32_t)mem[pos + 1] << 8)
                    | ((uint32_t)mem[pos + 2] << 16)
                    | ((uint32_t)mem[pos + 3] << 24);
        uint32_t sz = (uint32_t)mem[pos + 4]
                    | ((uint32_t)mem[pos + 5] << 8)
                    | ((uint32_t)mem[pos + 6] << 16)
                    | ((uint32_t)mem[pos + 7] << 24);
        long body = pos + 8;
        if (body < 0 || sz > (uint32_t)(len - body)) break;  // truncated
        if (ck == 0x20746d66u) {  // "fmt "
            uint16_t fmt, bch, bb;
            uint32_t br;
            if (sz < 16) return NULL;
            fmt = (uint16_t)(mem[body] | (mem[body + 1] << 8));
            bch = (uint16_t)(mem[body + 2] | (mem[body + 3] << 8));
            br = (uint32_t)mem[body + 4] | ((uint32_t)mem[body + 5] << 8)
               | ((uint32_t)mem[body + 6] << 16) | ((uint32_t)mem[body + 7] << 24);
            bb = (uint16_t)(mem[body + 14] | (mem[body + 15] << 8));
            if (fmt != 1) return NULL;  // PCM only
            if (bch < 1 || bch > 2) return NULL;
            if (bb != 8 && bb != 16) return NULL;
            if (br < 4000 || br > 192000) return NULL;
            ch = bch; bits = bb; rate = br;
        } else if (ck == 0x61746164u) {  // "data"
            data = mem + body;
            dlen = (long)sz;
        }
        pos = body + (long)((sz + 1) & ~1u);  // chunks are word-padded
    }
    if (ch == 0 || data == NULL || dlen <= 0) return NULL;

    src_frames = dlen / (ch * (bits / 8));
    if (src_frames <= 0 || src_frames > (1L << 26)) return NULL;
    if (rate == EWDX_AUDIO_RATE) {
        // bit-exact fast path (also keeps stream totals exact for loop math)
        long i;
        int c;
        out = (int16_t *)malloc((size_t)src_frames * 4u);
        if (out == NULL) return NULL;
        for (i = 0; i < src_frames; i++) {
            for (c = 0; c < 2; c++) {
                int sc = (ch == 1) ? 0 : c;
                if (bits == 8) {
                    const uint8_t *p = data + ((size_t)i * (size_t)ch + (size_t)sc);
                    out[i * 2 + c] = (int16_t)(((int)*p - 128) << 8);
                } else {
                    const uint8_t *p = data + (((size_t)i * (size_t)ch + (size_t)sc) * 2);
                    out[i * 2 + c] =
                        (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
                }
            }
        }
        *out_frames = src_frames;
        return out;
    }
    dst_frames = (long)(((int64_t)src_frames * EWDX_AUDIO_RATE) / (int64_t)rate) + 1;
    out = (int16_t *)malloc((size_t)dst_frames * 4u);
    if (out == NULL) return NULL;
    step = ((int64_t)rate << 32) / EWDX_AUDIO_RATE;
    rs.pos = 0;
    for (i = 0; i < dst_frames; i++) {
        long idx = (long)(rs.pos >> 32);
        uint32_t fr = (uint32_t)(rs.pos & 0xffffffffu);
        float fl = (float)fr / 4294967296.0f;
        int c;
        for (c = 0; c < 2; c++) {
            int sc = (ch == 1) ? 0 : c;
            float s0, s1, s;
            // fetch sample (frame o*, channel sc) from interleaved data:
            long o0 = idx, o1 = idx + 1;
            const uint8_t *p0, *p1;
            if (o0 < 0) o0 = 0;
            if (o0 >= src_frames) o0 = src_frames - 1;
            if (o1 < 0) o1 = 0;
            if (o1 >= src_frames) o1 = src_frames - 1;
            if (bits == 8) {
                p0 = data + ((size_t)o0 * (size_t)ch + (size_t)sc);
                p1 = data + ((size_t)o1 * (size_t)ch + (size_t)sc);
                s0 = (float)(((int)*p0 - 128) << 8);
                s1 = (float)(((int)*p1 - 128) << 8);
            } else {
                p0 = data + (((size_t)o0 * (size_t)ch + (size_t)sc) * 2);
                p1 = data + (((size_t)o1 * (size_t)ch + (size_t)sc) * 2);
                s0 = (float)(int16_t)((uint16_t)p0[0] | ((uint16_t)p0[1] << 8));
                s1 = (float)(int16_t)((uint16_t)p1[0] | ((uint16_t)p1[1] << 8));
            }
            s = s0 + (s1 - s0) * fl;
            out[i * 2 + c] = au_clamp_s16(s);
        }
        rs.pos += step;
    }
    *out_frames = dst_frames;
    return out;
}

// ------------------------------------------------------------ SE bank ---

typedef struct {
    int used;
    int16_t *pcm;      // stereo 44100 s16, owned
    long nframes;
    float vol;         // absolute level gain (from dmmvol millibels)
    int pan;           // -10000..10000 (from dmmpan)
    float gl, gr;      // combined = vol x equal-power pan
} AuSlot;

typedef struct {
    int active;
    int16_t *pcm;
    long nframes;
    long pos;
    float gl, gr;
    int slot;
    uint32_t gen;
} AuVoice;

static std::vector<AuSlot> au_slots;
static AuVoice au_voices[EWDX_VOICES];
static uint32_t au_gen = 1;
static float au_bgm_gl = 1.0f, au_bgm_gr = 1.0f;

static void au_slot_apply(AuSlot *s) {
    float pl, pr;
    au_pan_gains(s->pan, &pl, &pr);
    s->gl = s->vol * pl;
    s->gr = s->vol * pr;
}

static AuSlot *au_slot_get(int slot, int create);

static void au_slot_push_live(int slot) {
    size_t i;
    AuSlot *s = au_slot_get(slot, 0);
    if (s == NULL) return;
    for (i = 0; i < (size_t)EWDX_VOICES; i++) {
        if (au_voices[i].active && au_voices[i].slot == slot) {
            au_voices[i].gl = s->gl;
            au_voices[i].gr = s->gr;
        }
    }
}

static AuSlot *au_slot_get(int slot, int create) {
    if (slot < 0 || slot >= EWDX_SE_MAX_SLOTS) return NULL;
    if ((int)au_slots.size() <= slot) {
        size_t i, old;
        if (!create) return NULL;
        old = au_slots.size();
        au_slots.resize((size_t)slot + 1);
        for (i = old; i < au_slots.size(); i++) {
            au_slots[i].used = 0;
            au_slots[i].pcm = NULL;
            au_slots[i].nframes = 0;
            au_slots[i].vol = 1.0f;
            au_slots[i].pan = 0;
            au_slots[i].gl = 1.0f;
            au_slots[i].gr = 1.0f;
        }
    }
    return &au_slots[(size_t)slot];
}

// ------------------------------------------------------ BGM streamer ---

typedef struct {
    void *ctx;
    // pull up to `frames` stereo-44100 s16 frames; returns frames read (0=EOS)
    long (*read)(void *ctx, int16_t *out, long frames);
    void (*restart)(void *ctx);  // back to sample 0
    void (*close)(void *ctx);
    long total;  // total frames, always known for our sources
} AuBgmSrc;

typedef struct {
    stb_vorbis *dec;
    uint8_t *mem;   // owned file copy
    long mem_len;
    int channels;   // native (1 or 2)
    int rate;       // native
    // native-frame window for interpolation (absolute indices):
    int16_t blk[512 * 2];
    long blk_have;  // valid native frames in blk
    long blk_abs;   // absolute native index of blk[0]
    int eos;        // decoder exhausted
    int64_t rs;     // 32.32 absolute output position in NATIVE frames
} AuVorbisCtx;

static long au_vorbis_pull_native(AuVorbisCtx *vc, int16_t *out, long frames) {
    long got = 0;
    while (got < frames) {
        int want = (int)(frames - got);
        int n;
        if (want > 2048) want = 2048;
        n = stb_vorbis_get_samples_short_interleaved(vc->dec, vc->channels,
                                                    out + got * vc->channels,
                                                    want * vc->channels);
        if (n <= 0) break;
        got += n;
    }
    return got;
}

// Fetch one native frame's channel (clamped; pulls decoder forward as needed).
static int16_t au_vorbis_native(AuVorbisCtx *vc, long idx, int ch) {
    for (;;) {
        if (idx >= 0 && idx < vc->blk_abs + vc->blk_have) {
            long at = idx - vc->blk_abs;
            if (vc->channels == 2) return vc->blk[at * 2 + ch];
            return vc->blk[at];  // mono expands to both
        }
        if (vc->eos) {
            // past EOS: hold last decoded frame (streamer stops at total anyway)
            if (vc->blk_have <= 0) return 0;
            if (vc->channels == 2) return vc->blk[(vc->blk_have - 1) * 2 + ch];
            return vc->blk[vc->blk_have - 1];
        }
        {
            // slide window: keep last frame for continuity, refill after it
            long keep = 0;
            int16_t tail[2] = { 0, 0 };
            long n;
            if (vc->blk_have > 0) {
                keep = 1;
                tail[0] = vc->blk[(vc->blk_have - 1) * vc->channels];
                tail[1] = (vc->channels == 2)
                        ? vc->blk[(vc->blk_have - 1) * 2 + 1] : tail[0];
            }
            vc->blk_abs += vc->blk_have - keep;
            vc->blk[0] = tail[0];
            if (vc->channels == 2) vc->blk[1] = tail[1];
            n = au_vorbis_pull_native(vc, vc->blk + keep * vc->channels,
                                      512 - keep);
            vc->blk_have = keep + n;
            if (n <= 0) vc->eos = 1;
        }
    }
}

static long au_vorbis_read(void *ctx, int16_t *out, long frames) {
    AuVorbisCtx *vc = (AuVorbisCtx *)ctx;
    int64_t step = ((int64_t)vc->rate << 32) / EWDX_AUDIO_RATE;
    long i;
    for (i = 0; i < frames; i++) {
        long idx = (long)(vc->rs >> 32);
        float fl = (float)(uint32_t)(vc->rs & 0xffffffffu) / 4294967296.0f;
        int16_t a0 = au_vorbis_native(vc, idx, 0);
        int16_t a1 = au_vorbis_native(vc, idx + 1, 0);
        int16_t b0 = au_vorbis_native(vc, idx, 1);
        int16_t b1 = au_vorbis_native(vc, idx + 1, 1);
        out[i * 2] = au_clamp_s16((float)a0 + ((float)a1 - (float)a0) * fl);
        out[i * 2 + 1] = au_clamp_s16((float)b0 + ((float)b1 - (float)b0) * fl);
        vc->rs += step;
    }
    return frames;
    // NOTE: EOS is enforced by the streamer via total, not here; the hold-last
    // above only materializes if a file misreports its length.
}

static void au_vorbis_restart(void *ctx) {
    AuVorbisCtx *vc = (AuVorbisCtx *)ctx;
    int err = 0;
    if (vc->dec != NULL) stb_vorbis_close(vc->dec);
    // stb_vorbis has no seek: reopen from our memory copy (header parse only)
    vc->dec = stb_vorbis_open_memory(vc->mem, (int)vc->mem_len, &err, NULL);
    vc->rs = 0;
    vc->blk_have = 0;
    vc->blk_abs = 0;
    vc->eos = 0;
}

static void au_vorbis_close(void *ctx) {
    AuVorbisCtx *vc = (AuVorbisCtx *)ctx;
    if (vc->dec != NULL) stb_vorbis_close(vc->dec);
    free(vc->mem);
    free(vc);
}

// mem-PCM source (WAV BGM + host tests): pre-normalized stereo 44100.
typedef struct {
    int16_t *pcm;  // owned
    long nframes;
    long pos;
} AuMemCtx;

static long au_mem_read(void *ctx, int16_t *out, long frames) {
    AuMemCtx *mc = (AuMemCtx *)ctx;
    long avail = mc->nframes - mc->pos;
    if (avail <= 0) return 0;
    if (avail > frames) avail = frames;
    memcpy(out, mc->pcm + mc->pos * 2, (size_t)avail * 4u);
    mc->pos += avail;
    return avail;
}

static void au_mem_restart(void *ctx) { ((AuMemCtx *)ctx)->pos = 0; }

static void au_mem_close(void *ctx) {
    free(((AuMemCtx *)ctx)->pcm);
    free(ctx);
}

typedef struct {
    int has_track;
    int active;      // linear streaming phase (audible)
    int looping;     // wrapped: output cycles cache
    long cursor;     // absolute rendered frames
    long loop_b, loop_e, loop_len;
    int use_loop;
    AuBgmSrc src;
    int src_open;
    int16_t *cache;  // loop region [b,e), exactly loop_len frames
    long cache_have;
    long cache_pos;
} AuBgm;

static AuBgm au_bgm;
static std::mutex au_mtx;

static void au_bgm_close_src(void) {
    if (au_bgm.src_open && au_bgm.src.close != NULL) {
        au_bgm.src.close(au_bgm.src.ctx);
    }
    au_bgm.src.ctx = NULL;
    au_bgm.src_open = 0;
    free(au_bgm.cache);
    au_bgm.cache = NULL;
    au_bgm.cache_have = 0;
}

static void au_bgm_begin(long loop_len, long loop_end) {
    long total;
    au_bgm.active = 0;
    au_bgm.looping = 0;
    au_bgm.cursor = 0;
    au_bgm.cache_pos = 0;
    au_bgm.cache_have = 0;
    free(au_bgm.cache);
    au_bgm.cache = NULL;
    if (!au_bgm.has_track || !au_bgm.src_open) return;
    au_bgm.src.restart(au_bgm.src.ctx);
    total = au_bgm.src.total;
    au_bgm.use_loop = 0;
    if (loop_len > 0) {
        long e = (loop_end < 0) ? total : loop_end;
        long b;
        if (e > total) e = total;
        b = e - loop_len;
        if (b < 0) b = 0;
        if (e > b) {
            au_bgm.loop_b = b;
            au_bgm.loop_e = e;
            au_bgm.loop_len = e - b;
            au_bgm.cache = (int16_t *)malloc((size_t)au_bgm.loop_len * 4u);
            if (au_bgm.cache != NULL) {
                au_bgm.use_loop = 1;
            } else {
                au_bgm.loop_len = 0;
            }
        }
    }
    au_bgm.active = 1;
}

// Pull mixed (gain-free) BGM frames; returns frames written (pads end).
static long au_bgm_pull(int16_t *out, long frames) {
    long done = 0;
    if (!au_bgm.active) return 0;
    while (done < frames) {
        if (au_bgm.looping) {
            long avail = au_bgm.cache_have - au_bgm.cache_pos;
            long want = frames - done;
            if (avail <= 0) {  // cache exhausted unexpectedly: stop cleanly
                au_bgm.active = 0;
                au_bgm.looping = 0;
                break;
            }
            if (want > avail) want = avail;
            memcpy(out + done * 2, au_bgm.cache + au_bgm.cache_pos * 2,
                   (size_t)want * 4u);
            au_bgm.cache_pos += want;
            if (au_bgm.cache_pos >= au_bgm.cache_have) au_bgm.cache_pos = 0;
            done += want;
            au_bgm.cursor += want;
        } else {
            int16_t tmp[EWDX_FILL_SLICE * 2];
            long want = frames - done;
            long n, k;
            if (want > EWDX_FILL_SLICE) want = EWDX_FILL_SLICE;
            if (au_bgm.use_loop && au_bgm.cursor >= au_bgm.loop_e) {
                // exact wrap: cache[0] == sample loop_b by construction
                if (au_bgm.cache_have >= au_bgm.loop_len && au_bgm.loop_len > 0) {
                    au_bgm.looping = 1;
                    au_bgm.cache_pos = 0;
                    continue;
                }
                au_bgm.active = 0;  // incomplete cache: stop, never glitch
                break;
            }
            if (au_bgm.use_loop) {
                // never emit past E in the linear pass; the wrap above takes over
                long cap = au_bgm.loop_e - au_bgm.cursor;
                if (want > cap) want = cap;
            }
            if (want <= 0) { au_bgm.active = 0; break; }  // safety
            n = au_bgm.src.read(au_bgm.src.ctx, tmp, want);
            if (n <= 0) {  // EOS in linear phase (no-loop play, or E==total edge
                           // already handled by the wrap above): end
                au_bgm.active = 0;
                break;
            }
            // cache the loop region on first pass
            if (au_bgm.use_loop && au_bgm.cache != NULL) {
                for (k = 0; k < n; k++) {
                    long at = au_bgm.cursor + k;
                    if (at >= au_bgm.loop_b && at < au_bgm.loop_e) {
                        long ci = at - au_bgm.loop_b;
                        au_bgm.cache[ci * 2] = tmp[k * 2];
                        au_bgm.cache[ci * 2 + 1] = tmp[k * 2 + 1];
                        if (ci + 1 > au_bgm.cache_have) au_bgm.cache_have = ci + 1;
                    }
                }
            }
            memcpy(out + done * 2, tmp, (size_t)n * 4u);
            done += n;
            au_bgm.cursor += n;
        }
    }
    return done;
}

// ------------------------------------------------------ mixer core ---

void ewdx_audio_fill(int16_t *out, int frames) {
    float acc[EWDX_FILL_SLICE * 2];
    long done = 0;
    int v;
    if (out == NULL || frames <= 0) return;
    std::lock_guard<std::mutex> lk(au_mtx);
    while (done < frames) {
        long n = frames - done;
        long i;
        int16_t btmp[EWDX_FILL_SLICE * 2];
        long bn;
        if (n > EWDX_FILL_SLICE) n = EWDX_FILL_SLICE;
        for (i = 0; i < n * 2; i++) acc[i] = 0.0f;
        for (v = 0; v < EWDX_VOICES; v++) {
            AuVoice *vc = &au_voices[v];
            if (!vc->active) continue;
            for (i = 0; i < n; i++) {
                long p = vc->pos + i;
                if (p >= vc->nframes) break;
                acc[i * 2] += (float)vc->pcm[p * 2] * vc->gl;
                acc[i * 2 + 1] += (float)vc->pcm[p * 2 + 1] * vc->gr;
            }
            vc->pos += i;
            if (vc->pos >= vc->nframes) vc->active = 0;
        }
        bn = au_bgm_pull(btmp, n);
        for (i = 0; i < bn; i++) {
            acc[i * 2] += (float)btmp[i * 2] * au_bgm_gl;
            acc[i * 2 + 1] += (float)btmp[i * 2 + 1] * au_bgm_gr;
        }
        for (i = 0; i < n; i++) {
            out[(done + i) * 2] = au_clamp_s16(acc[i * 2]);
            out[(done + i) * 2 + 1] = au_clamp_s16(acc[i * 2 + 1]);
        }
        done += n;
    }
}

// ------------------------------------------------------- public API ---

static int au_ready = 0;

#ifndef EWDX_AUDIO_NULL_BACKEND
// --- OpenSL ES backend ---
static SLObjectItf au_sl_eng = NULL, au_sl_mix = NULL, au_sl_play = NULL;
static SLAndroidSimpleBufferQueueItf au_sl_bq = NULL;
static int16_t au_sl_buf[2][EWDX_BQ_FRAMES * 2];
static int au_sl_idx = 0;
static int au_sl_running = 0;

static void au_sl_cb(SLAndroidSimpleBufferQueueItf bq, void *ctx) {
    (void)ctx;
    au_sl_idx ^= 1;
    ewdx_audio_fill(au_sl_buf[au_sl_idx], EWDX_BQ_FRAMES);
    (*bq)->Enqueue(bq, au_sl_buf[au_sl_idx],
                   (SLuint32)(EWDX_BQ_FRAMES * 2u * sizeof(int16_t)));
}

static void au_sl_teardown(void) {
    if (au_sl_play != NULL) {
        SLPlayItf pi = NULL;
        (*au_sl_play)->GetInterface(au_sl_play, SL_IID_PLAY, &pi);
        if (pi != NULL) (*pi)->SetPlayState(pi, SL_PLAYSTATE_STOPPED);
        (*au_sl_play)->Destroy(au_sl_play);
    }
    if (au_sl_mix != NULL) (*au_sl_mix)->Destroy(au_sl_mix);
    if (au_sl_eng != NULL) (*au_sl_eng)->Destroy(au_sl_eng);
    au_sl_play = NULL;
    au_sl_mix = NULL;
    au_sl_eng = NULL;
    au_sl_bq = NULL;
    au_sl_running = 0;
}

#define AU_FAIL(tag) do { EWDX_LOGW("audio: %s failed (0x%x)", tag, (int)r); au_sl_teardown(); return 0; } while (0)

static int au_sl_start(void) {
    SLresult r;
    SLEngineItf eng;
    SLPlayItf play;
    SLDataLocator_AndroidSimpleBufferQueue loc_bq;
    SLDataFormat_PCM fmt;
    SLDataSource src;
    SLDataLocator_OutputMix loc_mix;
    SLDataSink sink;
    const SLInterfaceID ids[1] = { SL_IID_BUFFERQUEUE };
    const SLboolean req[1] = { SL_BOOLEAN_TRUE };

    r = slCreateEngine(&au_sl_eng, 0, NULL, 0, NULL, NULL);
    if (r != SL_RESULT_SUCCESS) AU_FAIL("engine-create");
    r = (*au_sl_eng)->Realize(au_sl_eng, SL_BOOLEAN_FALSE);
    if (r != SL_RESULT_SUCCESS) AU_FAIL("engine-realize");
    r = (*au_sl_eng)->GetInterface(au_sl_eng, SL_IID_ENGINE, &eng);
    if (r != SL_RESULT_SUCCESS) AU_FAIL("engine-iface");
    r = (*eng)->CreateOutputMix(eng, &au_sl_mix, 0, NULL, NULL);
    if (r != SL_RESULT_SUCCESS) AU_FAIL("mix-create");
    r = (*au_sl_mix)->Realize(au_sl_mix, SL_BOOLEAN_FALSE);
    if (r != SL_RESULT_SUCCESS) AU_FAIL("mix-realize");

    loc_bq.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
    loc_bq.numBuffers = 2;
    fmt.formatType = SL_DATAFORMAT_PCM;
    fmt.numChannels = 2;
    fmt.samplesPerSec = SL_SAMPLINGRATE_44_1;
    fmt.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    fmt.containerSize = 16;
    fmt.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    fmt.endianness = SL_BYTEORDER_LITTLEENDIAN;
    src.pLocator = &loc_bq;
    src.pFormat = &fmt;
    loc_mix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
    loc_mix.outputMix = au_sl_mix;
    sink.pLocator = &loc_mix;
    sink.pFormat = NULL;

    r = (*eng)->CreateAudioPlayer(eng, &au_sl_play, &src, &sink, 1, ids, req);
    if (r != SL_RESULT_SUCCESS) AU_FAIL("player-create");
    r = (*au_sl_play)->Realize(au_sl_play, SL_BOOLEAN_FALSE);
    if (r != SL_RESULT_SUCCESS) AU_FAIL("player-realize");
    r = (*au_sl_play)->GetInterface(au_sl_play, SL_IID_PLAY, &play);
    if (r != SL_RESULT_SUCCESS) AU_FAIL("play-iface");
    r = (*au_sl_play)->GetInterface(au_sl_play, SL_IID_BUFFERQUEUE, &au_sl_bq);
    if (r != SL_RESULT_SUCCESS) { EWDX_LOGW("audio: bq iface failed 0x%x", r); au_sl_teardown(); return 0; }
    r = (*au_sl_bq)->RegisterCallback(au_sl_bq, au_sl_cb, NULL);
    if (r != SL_RESULT_SUCCESS) { EWDX_LOGW("audio: regcb failed 0x%x", r); au_sl_teardown(); return 0; }

    // Fill+enqueue OUTSIDE au_mtx: ewdx_audio_fill locks au_mtx, and our caller
    // (ewdx_audio_init) already holds it -> self-deadlock hung dmmini forever
    // (2026-09-16 device run: black screen, no sound, journal stopped at _dmmini).
    au_sl_idx = 0;
    ewdx_audio_fill(au_sl_buf[0], EWDX_BQ_FRAMES);
    ewdx_audio_fill(au_sl_buf[1], EWDX_BQ_FRAMES);
    r = (*au_sl_bq)->Enqueue(au_sl_bq, au_sl_buf[0],
                             (SLuint32)(EWDX_BQ_FRAMES * 2u * sizeof(int16_t)));
    if (r != SL_RESULT_SUCCESS) { EWDX_LOGW("audio: enq0 failed 0x%x", r); au_sl_teardown(); return 0; }
    r = (*au_sl_bq)->Enqueue(au_sl_bq, au_sl_buf[1],
                             (SLuint32)(EWDX_BQ_FRAMES * 2u * sizeof(int16_t)));
    if (r != SL_RESULT_SUCCESS) { EWDX_LOGW("audio: enq1 failed 0x%x", r); au_sl_teardown(); return 0; }
    au_sl_idx = 1;  // first cb flips to 0 = the buffer just consumed
    r = (*play)->SetPlayState(play, SL_PLAYSTATE_PLAYING);
    if (r != SL_RESULT_SUCCESS) { EWDX_LOGW("audio: playstate failed 0x%x", r); au_sl_teardown(); return 0; }
    au_sl_running = 1;
    return 1;
}
#endif  // !EWDX_AUDIO_NULL_BACKEND

int ewdx_audio_init(void) {
    int v;
    int started;
    if (au_ready) return -1;
    // NOTE: do NOT hold au_mtx across au_sl_start(): the buffer prefills call
    // ewdx_audio_fill() which locks au_mtx -> self-deadlock (2026-09-16 hang).
    for (v = 0; v < EWDX_VOICES; v++) {
        au_voices[v].active = 0;
        au_voices[v].pcm = NULL;
    }
    au_bgm.has_track = 0;
    au_bgm.active = 0;
#ifndef EWDX_AUDIO_NULL_BACKEND
    started = au_sl_start();
    {
        std::lock_guard<std::mutex> lk(au_mtx);
        if (!started) {
            // No output device: keep mixer alive so gameplay stays silent-safe.
            // dmm stat still reports ok (script has no audio-fail path).
            EWDX_LOGW("audio: opensl unavailable (silent-safe)");
        }
        au_ready = 1;
    }
#else
    {
        std::lock_guard<std::mutex> lk(au_mtx);
        au_ready = 1;
    }
#endif
    return -1;
}

void ewdx_audio_shutdown(void) {
    size_t i;
    std::lock_guard<std::mutex> lk(au_mtx);
#ifndef EWDX_AUDIO_NULL_BACKEND
    au_sl_teardown();
#endif
    for (i = 0; i < au_slots.size(); i++) {
        free(au_slots[i].pcm);
        au_slots[i].pcm = NULL;
        au_slots[i].used = 0;
    }
    for (i = 0; i < (size_t)EWDX_VOICES; i++) au_voices[i].active = 0;
    au_bgm_close_src();
    au_bgm.has_track = 0;
    au_bgm.active = 0;
    au_ready = 0;
}

int ewdx_audio_ready(void) { return au_ready; }

// --- SE ---

int ewdx_se_load_mem(const void *wav, int len, int slot) {
    AuSlot *s;
    int16_t *pcm;
    long nframes;
    std::lock_guard<std::mutex> lk(au_mtx);
    if (wav == NULL || len <= 0) return 0;
    s = au_slot_get(slot, 1);
    if (s == NULL) return 0;
    pcm = au_wav_to_mix((const uint8_t *)wav, (long)len, &nframes);
    if (pcm == NULL || nframes <= 0) { free(pcm); return 0; }
    free(s->pcm);
    s->pcm = pcm;
    s->nframes = nframes;
    s->used = 1;
    s->vol = 1.0f;
    s->pan = 0;
    au_slot_apply(s);
    return -1;
}

int ewdx_se_load(const char *path, int slot) {
    long n = 0;
    uint8_t *buf;
    int rc;
    if (path == NULL) return 0;
    // filesDir first (bootstrapped data + saves), APK assets as fallback
    buf = ewdx_read_file(path, &n);
    if (buf == NULL || n <= 0) {
        EWDX_LOGE("dmmload: cannot open '%s'", path);
        return 0;
    }
    rc = ewdx_se_load_mem(buf, (int)n, slot);
    if (rc == 0) EWDX_LOGE("dmmload: bad WAV '%s' (%ld bytes)", path, n);
    free(buf);
    return rc;
}

int ewdx_se_vol(int slot, int millibel) {
    std::lock_guard<std::mutex> lk(au_mtx);
    AuSlot *s = au_slot_get(slot, 0);
    if (s == NULL || !s->used) return -1;  // unknown slot: Bearer silent ok
    s->vol = au_mb_gain(millibel);
    au_slot_apply(s);
    au_slot_push_live(slot);
    return -1;
}

int ewdx_se_pan(int slot, int pan) {
    std::lock_guard<std::mutex> lk(au_mtx);
    AuSlot *s = au_slot_get(slot, 0);
    if (s == NULL || !s->used) return -1;
    s->pan = pan;
    au_slot_apply(s);
    au_slot_push_live(slot);
    return -1;
}

int ewdx_se_play(int slot) {
    AuSlot *s;
    AuVoice *best = NULL;
    size_t i;
    std::lock_guard<std::mutex> lk(au_mtx);
    s = au_slot_get(slot, 0);
    if (s == NULL || !s->used || s->pcm == NULL) return 0;
    // retrigger: the original restarts the slot's voice, never layers it
    for (i = 0; i < (size_t)EWDX_VOICES; i++) {
        if (au_voices[i].active && au_voices[i].slot == slot) au_voices[i].active = 0;
    }
    for (i = 0; i < (size_t)EWDX_VOICES; i++) {
        if (!au_voices[i].active) { best = &au_voices[i]; break; }
        if (best == NULL || au_voices[i].gen < best->gen) best = &au_voices[i];
    }
    best->active = 1;
    best->pcm = s->pcm;
    best->nframes = s->nframes;
    best->pos = 0;
    best->gl = s->gl;
    best->gr = s->gr;
    best->slot = slot;
    best->gen = au_gen++;
    return -1;
}

int ewdx_se_stop(int slot) {
    size_t i;
    std::lock_guard<std::mutex> lk(au_mtx);
    for (i = 0; i < (size_t)EWDX_VOICES; i++) {
        if (au_voices[i].active && au_voices[i].slot == slot) au_voices[i].active = 0;
    }
    return -1;
}

void ewdx_se_stop_all(void) {
    size_t i;
    std::lock_guard<std::mutex> lk(au_mtx);
    for (i = 0; i < (size_t)EWDX_VOICES; i++) au_voices[i].active = 0;
}

// --- BGM ---

static int au_bgm_open_locked(const uint8_t *data, long len) {
    // probe OGG first, then WAV (game ships .ogg; tests use .wav)
    au_bgm_close_src();
    au_bgm.has_track = 0;
    if (data == NULL || len <= 0) return 0;
    if (len > 4 && memcmp(data, "OggS", 4) == 0) {
        AuVorbisCtx *vc = (AuVorbisCtx *)calloc(1, sizeof(AuVorbisCtx));
        stb_vorbis_info inf;
        int err = 0;
        if (vc == NULL) return 0;
        vc->mem_len = len;
        vc->mem = (uint8_t *)malloc((size_t)len);
        if (vc->mem == NULL) { free(vc); return 0; }
        memcpy(vc->mem, data, (size_t)len);
        vc->dec = stb_vorbis_open_memory(vc->mem, (int)len, &err, NULL);
        if (vc->dec == NULL) {
            free(vc->mem); free(vc); return 0;
        }
        inf = stb_vorbis_get_info(vc->dec);
        if (inf.channels < 1 || inf.channels > 2 || inf.sample_rate == 0) {
            stb_vorbis_close(vc->dec);
            free(vc->mem); free(vc); return 0;
        }
        vc->channels = inf.channels;
        vc->rate = (int)inf.sample_rate;
        au_bgm.src.ctx = vc;
        au_bgm.src.read = au_vorbis_read;
        au_bgm.src.restart = au_vorbis_restart;
        au_bgm.src.close = au_vorbis_close;
        au_bgm.src.total = (long)stb_vorbis_stream_length_in_samples(vc->dec);
        au_bgm.src_open = 1;
        au_bgm.has_track = 1;
        return -1;
    }
    {
        // WAV / raw PCM fallback (also the host-test path)
        AuMemCtx *mc = (AuMemCtx *)calloc(1, sizeof(AuMemCtx));
        if (mc == NULL) return 0;
        mc->pcm = au_wav_to_mix(data, len, &mc->nframes);
        if (mc->pcm == NULL || mc->nframes <= 0) {
            free(mc->pcm); free(mc); return 0;
        }
        au_bgm.src.ctx = mc;
        au_bgm.src.read = au_mem_read;
        au_bgm.src.restart = au_mem_restart;
        au_bgm.src.close = au_mem_close;
        au_bgm.src.total = mc->nframes;
        au_bgm.src_open = 1;
        au_bgm.has_track = 1;
        return -1;
    }
}

int ewdx_bgm_open_mem(const void *data, int len) {
    int rc;
    std::lock_guard<std::mutex> lk(au_mtx);
    rc = au_bgm_open_locked((const uint8_t *)data, (long)len);
    au_bgm.active = 0;
    au_bgm.looping = 0;
    au_bgm.cursor = 0;
    return rc;
}

int ewdx_bgm_play(long loop_len, long loop_end) {
    std::lock_guard<std::mutex> lk(au_mtx);
    if (!au_bgm.has_track || !au_bgm.src_open) return 0;
    au_bgm_begin(loop_len, loop_end);
    return au_bgm.active ? -1 : 0;
}

int ewdx_bgm_play_once(void) {
    std::lock_guard<std::mutex> lk(au_mtx);
    if (!au_bgm.has_track || !au_bgm.src_open) return 0;
    au_bgm_begin(0, -1);
    return au_bgm.active ? -1 : 0;
}

int ewdx_bgm_stop(void) {
    std::lock_guard<std::mutex> lk(au_mtx);
    au_bgm.active = 0;
    au_bgm.looping = 0;
    return -1;
}

int ewdx_bgm_vol(int millibel) {
    float g = au_mb_gain(millibel);
    std::lock_guard<std::mutex> lk(au_mtx);
    au_bgm_gl = g;
    au_bgm_gr = g;
    return -1;
}

int ewdx_bgm_status(void) {
    std::lock_guard<std::mutex> lk(au_mtx);
    if (!au_bgm.has_track) return -1;
    return au_bgm.active ? 1 : 0;
}

long ewdx_bgm_pos(void) {
    std::lock_guard<std::mutex> lk(au_mtx);
    return au_bgm.cursor;
}
