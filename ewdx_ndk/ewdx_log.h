// ewdx_log.h - error piping: __android_log_print on device, stderr on host.
//
// The script's `dialog`/`end` builtins stay dish-owned (hgio_dialog device UI,
// RUNMODE_END graceful stop). OUR failure paths (missing assets, bad files)
// log here first, then report via the stat contract so the script reaches its
// own dialog/end handling instead of dying silently.
#ifndef __EWDX_LOG_H
#define __EWDX_LOG_H

#ifdef __ANDROID__
#include <android/log.h>
#define EWDX_LOGE(fmt, ...) \
    __android_log_print(ANDROID_LOG_ERROR, "ewdx", fmt, ##__VA_ARGS__)
#define EWDX_LOGW(fmt, ...) \
    __android_log_print(ANDROID_LOG_WARN, "ewdx", fmt, ##__VA_ARGS__)
#else
#include <stdio.h>
#define EWDX_LOGE(fmt, ...) fprintf(stderr, "ewdx ERROR: " fmt "\n", ##__VA_ARGS__)
#define EWDX_LOGW(fmt, ...) fprintf(stderr, "ewdx WARN: " fmt "\n", ##__VA_ARGS__)
#endif

#endif
