// ewdx_boot.cpp - STEP-F part 2: probe + stage + HSP boot + exec pump.
//
// Asset layout: filesDir is the CWD (chdir at boot). HSP builtins + FilePack
// use plain fopen with script-relative paths, so the APK tree (start.ax,
// data/, save.dat) is staged into filesDir first. The staged start.ax gets
// its Win32 '\\' DS separators patched to '/' (SJIS-safe rule, see below)
// because Android fopen does not accept backslashes and HSP has no hook.
//
// The VM (hsp3core static lib) runs on the SDL thread: code_execcmd() blocks
// until END/ERROR while DGREDRAW/DIGETJOYSTATE pump input + present on the
// same thread, so no extra frame handler is needed.
#include "ewdx_boot.h"
#include "ewdx_paths.h"
#include "ewdx_input.h"
#include "ewdx_log.h"
#include "ewdx_register.h"
#include "ewdx_extcmd.h"
#include "ewdx_gles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

// HSP VM core (hsp3core static lib, OpenHSP sources).
#include "hsp3.h"
#include "hsp3code.h"
#include "hsp3debug.h"

#ifdef __ANDROID__
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <SDL_system.h>
#else
#include <errno.h>
#endif

#define EWDX_BOOT_AX "start.ax"
#define EWDX_BOOT_DATA_MARK "data/.ewdx_ok"

static Hsp3 *boot_hsp = NULL;

static int boot_mkdirs(const char *dir) {
#ifdef __ANDROID__
    char tmp[1024];
    size_t i, n;
    if (dir == NULL || dir[0] == '\0') return 0;
    strncpy(tmp, dir, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    n = strlen(tmp);
    for (i = 1; i <= n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char c = tmp[i];
            tmp[i] = '\0';
            mkdir(tmp, 0755);  // EEXIST is fine
            tmp[i] = c;
        }
    }
    return 0;
#else
    (void)dir;
    return 0;
#endif
}

static void boot_mkdir_parent(const char *dst) {
    char parent[2048];
    char *sl;
    strncpy(parent, dst, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    sl = strrchr(parent, '/');
    if (sl != NULL) {
        *sl = '\0';
        boot_mkdirs(parent);
    }
}

// --- AX DS backslash patch -------------------------------------------
// HSPHED (hsp3struct.h): magic HSP3 + ints; pt_ds @ +24, max_ds @ +28.
// Rule: 0x5C -> 0x2F only when BOTH neighbors are ASCII (<0x80). A SJIS
// trail byte 0x5C always follows a lead byte (>=0x81), so it is excluded;
// a literal path separator always sits between ASCII chars. Idempotent.
static int boot_patch_ds(uint8_t *img, long len) {
    int32_t pt_ds, max_ds;
    long i, n = 0;
    if (img == NULL || len < 72) return 0;
    if (img[0] != 'H' || img[1] != 'S' || img[2] != 'P' || img[3] != '3')
        return -1;
    memcpy(&pt_ds, img + 24, 4);
    memcpy(&max_ds, img + 28, 4);
    if (pt_ds <= 0 || max_ds <= 0) return -1;
    if ((long)pt_ds + (long)max_ds > len) return -1;
    for (i = (long)pt_ds + 1; i < (long)pt_ds + (long)max_ds - 1; i++) {
        if (img[i] == 0x5C && img[i - 1] < 0x80 && img[i + 1] < 0x80) {
            img[i] = 0x2F;
            n++;
        }
    }
    return (int)n;
}

// Read filesDir file, patch DS in place when needed. Returns 1 ok,
// 0 missing/bad-magic, <0 IO error.
static int boot_patch_file(const char *dst) {
    FILE *fp = fopen(dst, "rb");
    long n;
    uint8_t *img;
    int patched;
    if (fp == NULL) return 0;
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 72) { fclose(fp); return 0; }
    img = (uint8_t *)malloc((size_t)n);
    if (img == NULL) { fclose(fp); return -1; }
    if (fread(img, 1, (size_t)n, fp) != (size_t)n) {
        free(img); fclose(fp); return -1;
    }
    fclose(fp);
    patched = boot_patch_ds(img, n);
    if (patched < 0) { free(img); return 0; }  // bad magic / insane header
    if (patched > 0) {
        fp = fopen(dst, "wb");
        if (fp == NULL) { free(img); return -1; }
        if (fwrite(img, 1, (size_t)n, fp) != (size_t)n) {
            fclose(fp); free(img); return -1;
        }
        fclose(fp);
        EWDX_LOGW("boot: patched %d path separators in '%s'", patched, dst);
    }
    free(img);
    return 1;
}

int ewdx_boot_stage(const char *path) {
    char dst[2048];
    FILE *fp;
    long len = 0;
    uint8_t *buf;
    if (path == NULL || path[0] == '\0') return -1;
    ewdx_resolve(path, dst, sizeof(dst));
    fp = fopen(dst, "rb");
    if (fp != NULL) {
        fclose(fp);
        return 0;  // already staged
    }
    buf = ewdx_read_file(path, &len);  // falls back to APK assets
    if (buf == NULL || len <= 0) return -1;
    boot_mkdir_parent(dst);
    fp = fopen(dst, "wb");
    if (fp == NULL) {
        EWDX_LOGE("boot: cannot stage '%s' (%s)", dst, strerror(errno));
        free(buf);
        return -1;
    }
    if (fwrite(buf, 1, (size_t)len, fp) != (size_t)len) {
        EWDX_LOGE("boot: short write '%s'", dst);
        fclose(fp);
        free(buf);
        return -1;
    }
    fclose(fp);
    free(buf);
    EWDX_LOGW("boot: staged '%s' (%ld bytes)", dst, len);
    return 1;
}

int ewdx_boot_stage_ax(void) {
    char dst[2048];
    FILE *fp;
    ewdx_resolve(EWDX_BOOT_AX, dst, sizeof(dst));
    fp = fopen(dst, "rb");
    if (fp != NULL) {
        // adb-pushed dumps land here unpatched: patch in place (idempotent).
        fclose(fp);
        if (boot_patch_file(dst) <= 0)
            EWDX_LOGE("boot: '%s' present but unreadable", dst);
        return 1;
    }
    // Bundle path: copy APK asset bytes, patch, write.
    {
        long len = 0;
        uint8_t *buf = ewdx_read_file(EWDX_BOOT_AX, &len);
        int patched;
        if (buf == NULL || len <= 0) {
            EWDX_LOGE("boot: start.ax missing (push game start.ax to %s)", dst);
            return 0;
        }
        patched = boot_patch_ds(buf, len);
        if (patched < 0) {
            EWDX_LOGE("boot: start.ax asset has bad magic");
            free(buf);
            return 0;
        }
        boot_mkdir_parent(dst);
        fp = fopen(dst, "wb");
        if (fp == NULL) {
            EWDX_LOGE("boot: cannot write '%s' (%s)", dst, strerror(errno));
            free(buf);
            return 0;
        }
        if (fwrite(buf, 1, (size_t)len, fp) != (size_t)len) {
            EWDX_LOGE("boot: short write '%s'", dst);
            fclose(fp);
            free(buf);
            return 0;
        }
        fclose(fp);
        free(buf);
        EWDX_LOGW("boot: staged start.ax (%ld bytes, %d separators)",
                  len, patched);
        return 1;
    }
}

int ewdx_boot_probe(void) {
    const char *fd;
    char probe[2048];
    FILE *fp;
    ewdx_paths_init();
    fd = ewdx_files_dir();
    EWDX_LOGW("boot: filesDir='%s'", (fd != NULL) ? fd : "(null)");
    ewdx_resolve(EWDX_BOOT_AX, probe, sizeof(probe));
    fp = fopen(probe, "rb");
    if (fp != NULL) {
        long n;
        fseek(fp, 0, SEEK_END);
        n = ftell(fp);
        fclose(fp);
        fp = fopen(probe, "rb");
        if (fp != NULL) {
            char magic[4] = { 0, 0, 0, 0 };
            size_t r = fread(magic, 1, 4, fp);
            fclose(fp);
            if (r == 4 && magic[0] == 'H' && magic[1] == 'S' &&
                magic[2] == 'P' && magic[3] == '3') {
                EWDX_LOGW("boot: start.ax OK (%ld bytes, HSP3)", n);
                return 0;
            }
            EWDX_LOGE("boot: start.ax bad magic (%.4s)", magic);
            return 0;
        }
    }
    {
        long len = 0;
        uint8_t *buf = ewdx_read_file(EWDX_BOOT_AX, &len);
        if (buf != NULL) {
            int ok = (len > 4 && buf[0] == 'H' && buf[1] == 'S' &&
                      buf[2] == 'P' && buf[3] == '3');
            EWDX_LOGW("boot: start.ax in APK assets (%ld bytes, %s)",
                      len, ok ? "HSP3" : "BAD MAGIC");
            free(buf);
            return 0;
        }
    }
    EWDX_LOGE("boot: start.ax NOT FOUND (filesDir + APK assets)");
    EWDX_LOGE("boot: stage it: adb push start_ax_dump.bin <filesDir>/start.ax");
    return 0;
}

int ewdx_boot_chdir(void) {
    const char *fd = ewdx_files_dir();
#ifdef __ANDROID__
    if (fd == NULL || fd[0] == '\0') {
        EWDX_LOGE("boot: no filesDir (cannot chdir)");
        return -1;
    }
    if (chdir(fd) != 0) {
        EWDX_LOGE("boot: chdir('%s') failed (%s)", fd, strerror(errno));
        return -1;
    }
    EWDX_LOGW("boot: CWD='%s'", fd);
#else
    (void)fd;
#endif
    return 0;
}

// --- AssetManager bootstrap (device only) -----------------------------

#ifdef __ANDROID__
static AAssetManager *boot_asset_mgr(void) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act;
    jclass cls;
    jmethodID mid;
    jobject am;
    AAssetManager *mgr;
    if (env == NULL) return NULL;
    act = (jobject)SDL_AndroidGetActivity();
    if (act == NULL) return NULL;
    cls = env->GetObjectClass(act);
    if (cls == NULL) { env->DeleteLocalRef(act); return NULL; }
    mid = env->GetMethodID(cls, "getAssets",
                           "()Landroid/content/res/AssetManager;");
    if (mid == NULL) {
        env->DeleteLocalRef(cls); env->DeleteLocalRef(act); return NULL;
    }
    am = env->CallObjectMethod(act, mid);
    if (am == NULL) {
        env->DeleteLocalRef(cls); env->DeleteLocalRef(act); return NULL;
    }
    mgr = AAssetManager_fromJava(env, am);
    env->DeleteLocalRef(am);
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(act);
    return mgr;
}

// Copy one APK asset file -> filesDir-joined dst (parents created).
// Returns 1 copied, 0 asset-missing, <0 IO error.
static int boot_copy_asset(AAssetManager *mgr, const char *asset,
                           const char *dst) {
    AAsset *a = AAssetManager_open(mgr, asset, AASSET_MODE_STREAMING);
    FILE *fp;
    if (a == NULL) return 0;
    boot_mkdir_parent(dst);
    fp = fopen(dst, "wb");
    if (fp == NULL) {
        EWDX_LOGE("boot: cannot write '%s' (%s)", dst, strerror(errno));
        AAsset_close(a);
        return -1;
    }
    for (;;) {
        char chunk[32768];
        int r = AAsset_read(a, chunk, (size_t)sizeof(chunk));
        if (r < 0) {
            EWDX_LOGE("boot: asset read failed '%s'", asset);
            fclose(fp);
            AAsset_close(a);
            return -1;
        }
        if (r == 0) break;
        if (fwrite(chunk, 1, (size_t)r, fp) != (size_t)r) {
            EWDX_LOGE("boot: short write '%s'", dst);
            fclose(fp);
            AAsset_close(a);
            return -1;
        }
    }
    fclose(fp);
    AAsset_close(a);
    return 1;
}

// Recursively copy assetPath/ -> dstPath/ (both without trailing slash).
// Returns files copied, 0 when the asset path holds nothing.
static int boot_copy_tree(AAssetManager *mgr, const char *assetPath,
                          const char *dstPath) {
    AAssetDir *d = AAssetManager_openDir(mgr, assetPath);
    const char *name;
    int total = 0;
    int any = 0;
    if (d == NULL) {
        // Not listable: try as a single file.
        return boot_copy_asset(mgr, assetPath, dstPath);
    }
    while ((name = AAssetDir_getNextFileName(d)) != NULL) {
        char child_asset[1024], child_dst[2048];
        int rc;
        any = 1;
        snprintf(child_asset, sizeof(child_asset), "%s/%s", assetPath, name);
        snprintf(child_dst, sizeof(child_dst), "%s/%s", dstPath, name);
        // Directory or file? A file opens; a dir does not.
        {
            AAsset *probe =
                AAssetManager_open(mgr, child_asset, AASSET_MODE_STREAMING);
            if (probe != NULL) {
                AAsset_close(probe);
                rc = boot_copy_asset(mgr, child_asset, child_dst);
            } else {
                boot_mkdirs(child_dst);
                rc = boot_copy_tree(mgr, child_asset, child_dst);
            }
        }
        if (rc < 0) { AAssetDir_close(d); return -1; }
        total += rc;
    }
    AAssetDir_close(d);
    if (!any) {
        // Empty dir listing: may be a file after all (or missing).
        return boot_copy_asset(mgr, assetPath, dstPath);
    }
    return total;
}
#endif  // __ANDROID__

int ewdx_boot_bootstrap(void) {
#ifdef __ANDROID__
    AAssetManager *mgr;
    char dst[2048];
    FILE *fp;
    int staged = 0;
    mgr = boot_asset_mgr();
    if (mgr == NULL) {
        EWDX_LOGW("boot: no AssetManager (bootstrap skipped)");
        return 0;
    }
    // data/ tree: once (marker), ~100 MB.
    ewdx_resolve(EWDX_BOOT_DATA_MARK, dst, sizeof(dst));
    fp = fopen(dst, "rb");
    if (fp != NULL) {
        fclose(fp);
        EWDX_LOGW("boot: data/ already staged");
    } else {
        char dstdir[2048];
        int rc;
        ewdx_resolve("data", dstdir, sizeof(dstdir));
        boot_mkdirs(dstdir);
        EWDX_LOGW("boot: unpacking data/ from APK (first launch) ...");
        rc = boot_copy_tree(mgr, "data", dstdir);
        if (rc < 0) {
            EWDX_LOGE("boot: data/ unpack failed (continuing)");
        } else if (rc == 0) {
            EWDX_LOGW("boot: no data/ in APK (dev build? push via adb)");
        } else {
            fp = fopen(dst, "wb");
            if (fp != NULL) {
                fwrite("1\n", 1, 2, fp);
                fclose(fp);
            }
            EWDX_LOGW("boot: data/ unpacked (%d files)", rc);
            staged += rc;
        }
    }
    // save.dat: only when absent (never overwrite progress).
    ewdx_resolve("save.dat", dst, sizeof(dst));
    fp = fopen(dst, "rb");
    if (fp != NULL) {
        fclose(fp);
    } else {
        int rc = boot_copy_asset(mgr, "save.dat", dst);
        if (rc > 0) {
            EWDX_LOGW("boot: staged default save.dat");
            staged += rc;
        }
    }
    return staged;
#else
    return 0;  // host: files are used in place
#endif
}

// --- HSP VM boot + pump ------------------------------------------------

static int ewdx_dllctrl_stub(int cmd) {
    (void)cmd;
    code_next();  // mandatory advance (DLLCTRL convention mirrors DLLFUNC)
    throw(HSPERR_UNSUPPORTED_FUNCTION);
    return RUNMODE_RUN;
}

static int ewdx_term_noop(int opt) {
    (void)opt;
    return 0;
}

// hsp3utfcnv back-link (tv-folder queries): answer filesDir.
char *hsp3ext_getdir(int id) {
    static char dirbuf[1024];
    const char *fd = ewdx_files_dir();
    (void)id;
    if (fd != NULL && fd[0] != '\0') {
        strncpy(dirbuf, fd, sizeof(dirbuf) - 1);
        dirbuf[sizeof(dirbuf) - 1] = '\0';
    } else {
        dirbuf[0] = '.';
        dirbuf[1] = '\0';
    }
    return dirbuf;
}

#define EWDX_AWAIT_SLICE 16

static void ewdx_msgfunc(HSPCTX *hspctx) {
    int tick;
    while (1) {
        switch (hspctx->runmode) {
        case RUNMODE_STOP:
            throw HSPERR_NONE;
        case RUNMODE_WAIT:
            // waitcount is in 10ms units (hsp3cl parity).
            if (hspctx->waitcount > 0)
                SDL_Delay((Uint32)hspctx->waitcount * 10u);
            hspctx->runmode = RUNMODE_RUN;
            break;
        case RUNMODE_AWAIT:
            tick = (int)SDL_GetTicks();
            if (code_exec_await(tick) != RUNMODE_RUN) {
                int maxtick = hspctx->waittick - tick;
                ewdx_input_poll();  // keep touch state fresh during waits
                if (maxtick >= EWDX_AWAIT_SLICE) {
                    if (maxtick > EWDX_AWAIT_SLICE) maxtick = EWDX_AWAIT_SLICE;
                    SDL_Delay((Uint32)maxtick);
                } else if (maxtick > 0) {
                    SDL_Delay((Uint32)maxtick);
                } else {
                    SDL_Delay(1);
                }
            } else {
                hspctx->lasttick = (int)SDL_GetTicks();
                hspctx->runmode = RUNMODE_RUN;
            }
            break;
        case RUNMODE_END:
            throw HSPERR_NONE;
        case RUNMODE_RETURN:
            throw HSPERR_RETURN_WITHOUT_GOSUB;
        case RUNMODE_ASSERT:
            hspctx->runmode = RUNMODE_STOP;
            break;
        case RUNMODE_LOGMES:
            EWDX_LOGW("logmes: %s", hspctx->stmp);
            hspctx->runmode = RUNMODE_RUN;
            return;
        default:
            return;
        }
    }
}

int ewdx_boot_startup(void) {
    HSPCTX *ctx;
    HSP3TYPEINFO *t;
    char axname[] = "start.ax";
    char moddir[1024];
    if (boot_hsp != NULL) return 0;  // idempotent
    boot_hsp = new Hsp3();
    boot_hsp->SetFileName(axname);  // CWD == filesDir (ewdx_boot_chdir)
    if (boot_hsp->Reset(0)) {
        EWDX_LOGE("boot: Hsp3::Reset failed (start.ax unreadable?)");
        delete boot_hsp;
        boot_hsp = NULL;
        return -1;
    }
    ctx = &boot_hsp->hspctx;
    strncpy(moddir, ewdx_files_dir(), sizeof(moddir) - 1);
    moddir[sizeof(moddir) - 1] = '\0';
    boot_hsp->SetCommandLinePrm((char *)"");
    boot_hsp->SetModuleFilePrm(moddir[0] ? moddir : (char *)".");
    ctx->msgfunc = ewdx_msgfunc;
    ctx->hspstat |= 16;
    // DLLCTRL: no COM/newcom on Android; clean error instead of NULL call.
    t = code_gettypeinfo(TYPE_DLLCTRL);
    t->cmdfunc = ewdx_dllctrl_stub;
    t->termfunc = ewdx_term_noop;
    // DLLFUNC: the game surface (hmm/hspda/hspogg/system shims).
    t = code_gettypeinfo(TYPE_DLLFUNC);
    t->termfunc = ewdx_term_noop;
    ewdx_register(t);  // overrides cmdfunc/reffunc + installs ovplay hook
    // EXTCMD/EXTSYSVAR: window/dialog/input/text shims.
    {
        HSP3TYPEINFO *te = code_gettypeinfo(TYPE_EXTCMD);
        HSP3TYPEINFO *tf = code_gettypeinfo(TYPE_EXTSYSVAR);
        te->termfunc = ewdx_term_noop;
        ewdx_extcmd_register(te, tf);
    }
    EWDX_LOGW("boot: HSP VM ready (vars=%d)", ctx->hsphed->max_val);
    return 0;
}

int ewdx_boot_exec(void) {
    int runmode;
    int endcode;
    if (boot_hsp == NULL) return -1;
    runmode = code_execcmd();
    if (runmode == RUNMODE_ERROR) {
        char errmsg[1024];
        HSPERROR err = code_geterror();
        int ln = code_getdebug_line();
        const char *msg = hspd_geterror(err);
        const char *fname = code_getdebug_name();
        if (ln < 0) {
            snprintf(errmsg, sizeof(errmsg), "#Error %d --> %s", (int)err,
                     msg ? msg : "?");
        } else {
            snprintf(errmsg, sizeof(errmsg), "#Error %d in line %d (%s) --> %s",
                     (int)err, ln, fname ? fname : "?", msg ? msg : "?");
        }
        EWDX_LOGE("boot: %s", errmsg);
        if (ewdx.win != NULL) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "EchidnaWarsDX",
                                     errmsg, ewdx.win);
        }
        return -1;
    }
    endcode = boot_hsp->hspctx.endcode;
    EWDX_LOGW("boot: VM ended (endcode=%d)", endcode);
    return endcode;
}

void ewdx_boot_bye(void) {
    if (boot_hsp != NULL) {
        delete boot_hsp;  // termfunc + Dispose + code_bye
        boot_hsp = NULL;
    }
}

int ewdx_boot_tick(void) {
    ewdx_input_poll();
    return 0;
}
