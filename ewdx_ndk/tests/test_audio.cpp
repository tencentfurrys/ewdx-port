// test_audio.cpp - host verification for ewdx_audio (NULL backend).
//
// Covers: WAV parse (16-bit stereo / 8-bit mono / corrupt), SE mix level +
// pan law, voice restart determinism, BGM loop-wrap sample accuracy,
// end-anchor (loop_end<0), play-once tail silence, status/pos accounting,
// off-rate resample ratio. The OGG decode path itself needs a real .ogg
// (game data, not in repo); the streamer above it is fully exercised via WAV.
//
// Build (MinGW, from the workspace root; backslashes shown, adapt as needed):
//   gcc -c ewdx-port/ewdx_ndk/thirdparty/stb_vorbis.c -o stb.o
//   g++ -std=c++17 -DEWDX_AUDIO_NULL_BACKEND -I ewdx-port/ewdx_ndk ...
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ewdx_audio.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL %d: %s (line %d)\n", failures, #cond, __LINE__); } \
} while (0)

// --- minimal WAV builder (PCM, interleaved) ---

typedef struct { uint8_t *mem; int len; } Blob;

static Blob make_wav(int bits, int ch, int rate, int nframes,
                     int (*fn)(int frame, int c)) {
    int bytes = bits / 8;
    int dlen = nframes * ch * bytes;
    int len = 44 + dlen;
    uint8_t *m = (uint8_t *)malloc((size_t)len);
    Blob b = { m, len };
    int i, c;
    memcpy(m, "RIFF", 4);
    m[4] = (uint8_t)((len - 8) & 0xff); m[5] = (uint8_t)(((len - 8) >> 8) & 0xff);
    m[6] = (uint8_t)(((len - 8) >> 16) & 0xff); m[7] = (uint8_t)(((len - 8) >> 24) & 0xff);
    memcpy(m + 8, "WAVEfmt ", 8);
    m[16] = 16; m[17] = 0; m[18] = 0; m[19] = 0;
    m[20] = 1; m[21] = 0;  // PCM
    m[22] = (uint8_t)ch; m[23] = 0;
    m[24] = (uint8_t)(rate & 0xff); m[25] = (uint8_t)((rate >> 8) & 0xff);
    m[26] = (uint8_t)((rate >> 16) & 0xff); m[27] = (uint8_t)((rate >> 24) & 0xff);
    {
        int br = rate * ch * bytes;
        m[28] = (uint8_t)(br & 0xff); m[29] = (uint8_t)((br >> 8) & 0xff);
        m[30] = (uint8_t)((br >> 16) & 0xff); m[31] = (uint8_t)((br >> 24) & 0xff);
    }
    m[32] = (uint8_t)(ch * bytes); m[33] = 0;
    m[34] = (uint8_t)bits; m[35] = 0;
    memcpy(m + 36, "data", 4);
    m[40] = (uint8_t)(dlen & 0xff); m[41] = (uint8_t)((dlen >> 8) & 0xff);
    m[42] = (uint8_t)((dlen >> 16) & 0xff); m[43] = (uint8_t)((dlen >> 24) & 0xff);
    for (i = 0; i < nframes; i++) {
        for (c = 0; c < ch; c++) {
            int v = fn(i, c);
            uint8_t *p = m + 44 + (i * ch + c) * bytes;
            if (bits == 8) {
                if (v < -32768) v = -32768;
                if (v > 32767) v = 32767;
                p[0] = (uint8_t)((v >> 8) + 128);
            } else {
                uint16_t u = (uint16_t)(int16_t)v;
                p[0] = (uint8_t)(u & 0xff);
                p[1] = (uint8_t)((u >> 8) & 0xff);
            }
        }
    }
    return b;
}

static int g_ramp(int f, int c) { (void)c; return f; }  // 0,1,2,... (exact)
static int g_dc(int f, int c) { (void)f; (void)c; return 10000; }
static int g_sine(int f, int c) {
    (void)c;
    return (int)(30000.0 * sin(2.0 * 3.14159265358979 * 440.0 * f / 44100.0));
}
static int g_sine22(int f, int c) {
    (void)c;
    return (int)(30000.0 * sin(2.0 * 3.14159265358979 * 440.0 * f / 22050.0));
}

static int peak_abs(const int16_t *buf, int frames, int c) {
    int pk = 0, i;
    for (i = 0; i < frames; i++) {
        int v = buf[i * 2 + c];
        if (v < 0) v = -v;
        if (v > pk) pk = v;
    }
    return pk;
}

int main(void) {
    // --- init / ready ---
    CHECK(ewdx_audio_init() == -1);
    CHECK(ewdx_audio_ready() == 1);

    // --- WAV parse: bad magic fails ---
    {
        uint8_t junk[64];
        memset(junk, 0, sizeof(junk));
        CHECK(ewdx_se_load_mem(junk, (int)sizeof(junk), 0) == 0);
        CHECK(ewdx_se_play(0) == 0);  // nothing loaded on slot 0
    }

    // --- SE mix: full-scale DC at unity ---
    {
        Blob w = make_wav(16, 2, 44100, 256, g_dc);
        int16_t out[256 * 2];
        CHECK(ewdx_se_load_mem(w.mem, w.len, 3) == -1);
        CHECK(ewdx_se_play(3) == -1);
        ewdx_audio_fill(out, 256);
        CHECK(peak_abs(out, 256, 0) == 10000);
        CHECK(peak_abs(out, 256, 1) == 10000);
        free(w.mem);
    }

    // --- file-backed load (fopen branch of ewdx_read_file) ---
    {
        Blob w = make_wav(16, 2, 44100, 256, g_dc);
        int16_t out[256 * 2];
        FILE *fp = fopen("ewdx_test_tmp.wav", "wb");
        CHECK(fp != NULL);
        if (fp != NULL) {
            CHECK(fwrite(w.mem, 1, (size_t)w.len, fp) == (size_t)w.len);
            fclose(fp);
            CHECK(ewdx_se_load("ewdx_test_tmp.wav", 6) == -1);
            CHECK(ewdx_se_play(6) == -1);
            ewdx_audio_fill(out, 256);
            CHECK(peak_abs(out, 256, 0) == 10000);
            remove("ewdx_test_tmp.wav");
        }
        CHECK(ewdx_se_load("ewdx_no_such_file.wav", 6) == 0);  // missing -> 0
        free(w.mem);
    }

    // --- level: -6000 mB = x0.001 ---
    {
        int16_t out[256 * 2];
        CHECK(ewdx_se_vol(3, -6000) == -1);
        CHECK(ewdx_se_play(3) == -1);
        ewdx_audio_fill(out, 256);
        CHECK(peak_abs(out, 256, 0) == 10);  // 10000 * 10^(-3)
        CHECK(ewdx_se_vol(3, 0) == -1);      // restore unity for later tests
    }

    // --- pan: hard left kills right, keeps left ---
    {
        int16_t out[256 * 2];
        CHECK(ewdx_se_pan(3, -10000) == -1);
        CHECK(ewdx_se_play(3) == -1);
        ewdx_audio_fill(out, 256);
        CHECK(peak_abs(out, 256, 0) == 10000);
        CHECK(peak_abs(out, 256, 1) == 0);
        CHECK(ewdx_se_pan(3, 10000) == -1);
        CHECK(ewdx_se_play(3) == -1);
        ewdx_audio_fill(out, 256);
        CHECK(peak_abs(out, 256, 0) == 0);
        CHECK(peak_abs(out, 256, 1) == 10000);
        CHECK(ewdx_se_pan(3, 0) == -1);
    }

    // --- restart determinism + stop ---
    {
        int16_t a[128 * 2], b[128 * 2];
        CHECK(ewdx_se_play(3) == -1);
        ewdx_audio_fill(a, 128);
        CHECK(ewdx_se_play(3) == -1);  // retrigger restarts
        ewdx_audio_fill(b, 128);
        CHECK(memcmp(a, b, sizeof(a)) == 0);
        CHECK(ewdx_se_stop(3) == -1);
        ewdx_audio_fill(a, 128);
        CHECK(peak_abs(a, 128, 0) == 0);  // voice done + stopped => silence
    }

    // --- 16-bit stereo 44100 passthrough peak ---
    {
        Blob w = make_wav(16, 2, 44100, 4410, g_sine);  // 0.1 s 440 Hz
        int16_t out[256 * 2];
        CHECK(ewdx_se_load_mem(w.mem, w.len, 5) == -1);
        CHECK(ewdx_se_play(5) == -1);
        ewdx_audio_fill(out, 256);
        CHECK(peak_abs(out, 256, 0) > 29000 && peak_abs(out, 256, 0) <= 30000);
        CHECK(peak_abs(out, 256, 1) > 29000 && peak_abs(out, 256, 1) <= 30000);
        CHECK(ewdx_se_stop(5) == -1);
        free(w.mem);
    }

    // --- 8-bit mono 22050 resample ratio + content ---
    {
        Blob w = make_wav(8, 1, 22050, 2205, g_sine22);  // 0.1 s
        int16_t out[256 * 2];
        CHECK(ewdx_se_load_mem(w.mem, w.len, 4) == -1);
        CHECK(ewdx_se_play(4) == -1);
        ewdx_audio_fill(out, 256);
        // 8-bit quantization: floor-asymmetric ((v>>8)+128), so the negative
        // peak can reach -30208; positive peak stays <= 30000>>8<<8 = 29952.
        CHECK(peak_abs(out, 256, 0) > 29000 && peak_abs(out, 256, 0) <= 30300);
        CHECK(peak_abs(out, 256, 1) > 29000);  // mono duplicated
        free(w.mem);
    }

    // --- BGM loop sample accuracy (ramp: frame i holds value i) ---
    {
        Blob w = make_wav(16, 2, 44100, 100, g_ramp);
        int16_t out[200 * 2];
        int16_t drain[64 * 2];
        int i, ok = 1;
        ewdx_se_stop_all();  // slot-4 sine still has frames left; drain it
        ewdx_audio_fill(drain, 64);
        (void)drain;
        CHECK(ewdx_bgm_open_mem(w.mem, w.len) == -1);
        CHECK(ewdx_bgm_status() == 0);
        CHECK(ewdx_bgm_play(50, 80) == -1);  // region [30,80)
        ewdx_audio_fill(out, 200);
        for (i = 0; i < 80; i++) {
            if (out[i * 2] != i || out[i * 2 + 1] != i) { ok = 0; break; }
        }
        CHECK(ok);
        ok = 1;
        for (i = 80; i < 200; i++) {  // wraps: 30..79 repeating
            int expect = 30 + ((i - 80) % 50);
            if (out[i * 2] != expect || out[i * 2 + 1] != expect) { ok = 0; break; }
        }
        CHECK(ok);
        CHECK(ewdx_bgm_pos() == 200);
        CHECK(ewdx_bgm_status() == 1);
        free(w.mem);
    }

    // --- end anchor (loop_end<0) + play-once tail ---
    {
        Blob w = make_wav(16, 2, 44100, 100, g_ramp);
        int16_t out[160 * 2];
        int i, ok = 1;
        CHECK(ewdx_bgm_open_mem(w.mem, w.len) == -1);
        CHECK(ewdx_bgm_play(30, -1) == -1);  // last 30 frames loop
        ewdx_audio_fill(out, 160);
        for (i = 100; i < 160; i++) {
            int expect = 70 + ((i - 100) % 30);
            if (out[i * 2] != expect) { ok = 0; break; }
        }
        CHECK(ok);
        CHECK(ewdx_bgm_stop() == -1);
        CHECK(ewdx_bgm_status() == 0);
        CHECK(ewdx_bgm_play_once() == -1);
        ewdx_audio_fill(out, 160);
        ok = 1;
        for (i = 0; i < 100; i++) {
            if (out[i * 2] != i) { ok = 0; break; }
        }
        CHECK(ok);
        ok = 1;
        for (i = 100; i < 160; i++) {  // tail must be silence, not garbage
            if (out[i * 2] != 0 || out[i * 2 + 1] != 0) { ok = 0; break; }
        }
        CHECK(ok);
        CHECK(ewdx_bgm_status() == 0);  // ended
        free(w.mem);
    }

    // --- BGM vol + no-track status ---
    {
        Blob w = make_wav(16, 2, 44100, 64, g_dc);
        int16_t out[64 * 2];
        CHECK(ewdx_bgm_open_mem(w.mem, w.len) == -1);
        CHECK(ewdx_bgm_vol(-2000) == -1);  // x0.1
        CHECK(ewdx_bgm_play_once() == -1);
        ewdx_audio_fill(out, 64);
        CHECK(peak_abs(out, 64, 0) == 1000);
        CHECK(ewdx_bgm_vol(0) == -1);
        free(w.mem);
    }

    ewdx_audio_shutdown();
    CHECK(ewdx_audio_ready() == 0);

    printf("%s: %d checks, %d failures\n",
           failures ? "RESULT FAIL" : "RESULT PASS", checks, failures);
    return failures ? 1 : 0;
}
