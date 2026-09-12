// ewdx_audio.h - step (d) audio pipeline: dmm* SE bank + OGG BGM streamer.
//
// Backend: OpenSL ES (buffer-queue player, 44100 Hz stereo s16; software
// voice mixer). The mixer core has no platform deps (C++ <mutex> only), so
// host tests drive ewdx_audio_fill() directly with EWDX_AUDIO_NULL_BACKEND.
//
// ovplay (hspogg) command mapping, mined from start_ax_dump.hsp L158-178 and
// L3884-3966 (bgmload/set_volume). ovplay.dll is a #regcmd EXTCMD plugin; its
// dish-level hook is a follow-up, and it drives THIS api:
//   cmd_0_0          init .................... ewdx_audio_init()
//   cmd_0_1(buf,0,98304) decode OGG from mem . ewdx_bgm_open_mem()
//   cmd_0_3(0,0)      play once .............. ewdx_bgm_play_once()
//   cmd_0_5(0,0,looptime,loopstart,-1)
//                     loop play .............. ewdx_bgm_play(looptime,loopstart)
//   cmd_0_6(0)        stop ................... ewdx_bgm_stop()
//   cmd_0_7(0,mb)     volume (millibels) ..... ewdx_bgm_vol(mb)
//   cmd_0_11(p)       status -> stat ......... ewdx_bgm_status()
//   cmd_0_254         ov_save (encode path; not used by gameplay)
//
// Loop semantics (verified per track, e.g. 1.ogg total 3:04.61 loop 14.21s):
//   loop_len L, loop_end E (samples; E<0 anchors at stream end).
//   L<=0 -> play once. Otherwise the loop region is [E-L, E); on reaching E
//   playback wraps to E-L sample-accurately (verified by host test).
//   2.ogg (L=28.19s, E=-1) loops its last 28.19 s; 0/5/6/8/9.ogg (L=0) once.
//
// dmm (hspogg SE) mapping (seplay/sestop, L3814-3882):
//   dmmini/dmmini->ok . ewdx_audio_init(); dmmbye -> ewdx_audio_shutdown()
//   dmmload(file,slot)  ewdx_se_load()   (WAV PCM 8/16-bit mono/stereo)
//   dmmvol(slot,mb)     ewdx_se_vol()    (mb ~= -10000..0 millibels)
//   dmmpan(slot,pan)    ewdx_se_pan()    (pan +-10000, equal-power)
//   dmmplay/dmmstop     ewdx_se_play()/ewdx_se_stop()
//
// Stat contract (consumed by ewdx_register.cpp): -1 ok / 0 fail everywhere
// except b2-documented specials. Audio failure degrades to silence; the
// script's exist/strsize gates already guard missing files, so no dialogs.
#ifndef __EWDX_AUDIO_H
#define __EWDX_AUDIO_H

#include <stdint.h>

#define EWDX_AUDIO_RATE 44100
#define EWDX_AUDIO_CH 2

// --- lifecycle (-1 ok / 0 fail) ---
int ewdx_audio_init(void);
void ewdx_audio_shutdown(void);
int ewdx_audio_ready(void);  // 1 after successful init, else 0

// --- SE bank (dmm*) ---
int ewdx_se_load(const char *path, int slot);          // WAV file
int ewdx_se_load_mem(const void *wav, int len, int slot);  // (also for tests)
int ewdx_se_vol(int slot, int millibel);
int ewdx_se_pan(int slot, int pan);                    // -10000..10000
int ewdx_se_play(int slot);                            // (re)trigger
int ewdx_se_stop(int slot);
void ewdx_se_stop_all(void);

// --- BGM streamer (ovplay) ---
int ewdx_bgm_open_mem(const void *data, int len);  // copies; OGG or WAV
int ewdx_bgm_play(long loop_len, long loop_end);   // samples; end<0 => EOS
int ewdx_bgm_play_once(void);
int ewdx_bgm_stop(void);
int ewdx_bgm_vol(int millibel);
int ewdx_bgm_status(void);  // 1 playing, 0 stopped/idle, <0 no track open
long ewdx_bgm_pos(void);    // absolute sample cursor (rendered frames)

// --- mixer core: render `frames` stereo s16 frames into out ---
// Called by the OpenSL callback; host tests call it directly.
void ewdx_audio_fill(int16_t *out, int frames);

#endif
