/*
 * FMV / MyDecoder JNI callbacks.
 *
 * On Android the game uses a SurfaceTexture-backed OES texture fed by the
 * native fw_start_movie / AVI_frame pipeline.  That mechanism does not exist
 * on Vita, so we implement movie playback ourselves via ff7_avi_player:
 *
 *   START  → translate the Android path to a Vita path, open the AVI file
 *   FRAME  → decode one MJPEG frame and upload it to the game's GL texture
 *   RESET  → close the AVI file; GET_POSITION then returns 0 to exit the
 *             game's "wait-for-reset" polling loop
 *
 * The original native symbols (fw_start_movie, AVI_frame, …) are still
 * resolved for diagnostic purposes but are NOT called — they require an
 * initialised OpenSL ES audio engine which is not available on Vita.
 */

#include "ff7_jni_callbacks.h"
#include "reimpl/ff7_video_player.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#include <vitaGL.h>
#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "utils/ff7_boot_log.h"
#include "utils/path_translate.h"

extern so_module so_mod;

/* ------------------------------------------------------------------ */
/* Cached AVI/FW symbol table — resolved once, used forever           */
/* ------------------------------------------------------------------ */

static struct {
    void  (*fw_start)(void);            /* fw_start_movie()          */
    void  (*avi_frame)(int);            /* AVI_frame(int)            */
    void  (*avi_after)(void);           /* staff_AVI_afterRenderingOES() */
    void  (*avi_reset)(void);           /* AVI_reset()               */
    jint  (*avi_get_pos)(void);         /* AVI_getFrameCounter()     */
    void  (*avi_set_pos)(int);          /* AVI_setFrameCounter(int)  */
    void  (*avi_set_vol)(int);          /* AVI_setVolume(int)        */
    jint  (*avi_total_time)(void);      /* AVI_getTotalTime()        */
} s_avi;

/* GL texture ID passed via SET_TEXTURE — the target the decoder writes into. */
static GLuint s_movie_tex = 0;

static int s_avi_resolved = 0;

/* Resolve once; log which symbols are missing so the log only shows it once. */
static void resolve_avi_symbols(void) {
    if (s_avi_resolved) return;
    s_avi_resolved = 1;

    ff7_video_init();   /* load SceAvPlayer sysmodule */

    ff7_boot_log_section("AVI symbol resolution");

#define RESOLVE_AVI(field, mangled)                                         \
    do {                                                                    \
        s_avi.field = (__typeof__(s_avi.field))so_symbol(&so_mod, mangled); \
        ff7_boot_log("  %-20s %s", mangled,                                \
                     s_avi.field ? "OK" : "MISSING");                      \
    } while (0)

    RESOLVE_AVI(fw_start,      "_Z14fw_start_moviev");
    RESOLVE_AVI(avi_frame,     "_Z9AVI_framei");
    RESOLVE_AVI(avi_after,     "staff_AVI_afterRenderingOES");
    RESOLVE_AVI(avi_reset,     "_Z9AVI_resetv");
    RESOLVE_AVI(avi_get_pos,   "_Z19AVI_getFrameCounterv");
    RESOLVE_AVI(avi_set_pos,   "_Z19AVI_setFrameCounteri");
    RESOLVE_AVI(avi_set_vol,   "_Z13AVI_setVolumei");
    RESOLVE_AVI(avi_total_time,"_Z16AVI_getTotalTimev");

#undef RESOLVE_AVI
}

/* ------------------------------------------------------------------ */
/* MyDecoder callbacks                                                */
/* ------------------------------------------------------------------ */

void ff7_cb_md_setTexture(jmethodID id, va_list args) {
    (void)id;
    jint tex = va_arg(args, jint);
    s_movie_tex = (GLuint)tex;
    ff7_boot_log("[movie] SET_TEXTURE tex=%d", (int)tex);
}

jint ff7_cb_md_start(jmethodID id, va_list args) {
    (void)id;
    jstring path = va_arg(args, jstring);
    JNIEnv *env = &jni;
    const char *path_cstr = path ? (*env)->GetStringUTFChars(env, path, NULL) : NULL;

    resolve_avi_symbols();

    /* Translate the Android-relative path (e.g. "/ff7_1.02/data/movies/eidoslogo.avi")
     * to the Vita path ("ux0:data/ff7/ff7_1.02/data/movies/eidoslogo.avi"). */
    char vita_path[512] = {0};
    if (path_cstr)
        path_translate_data(path_cstr, vita_path, sizeof(vita_path));

    ff7_boot_log("[movie] START \"%s\"", vita_path[0] ? vita_path : "(null)");

    int ok = vita_path[0] ? ff7_video_open(vita_path) : 0;
    if (!ok)
        ff7_boot_log("[movie] START: video open failed — movie will be skipped");

    if (path_cstr)
        (*env)->ReleaseStringUTFChars(env, path, (char *)path_cstr);

    return 1;
}

jint ff7_cb_md_getTotalTime(jmethodID id, va_list args) {
    (void)id; (void)args;
    return (jint)ff7_video_total_ms();
}

jint ff7_cb_md_frame(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jint);   /* frame_n — we advance sequentially */

    int more = ff7_video_next_frame(s_movie_tex);

    /* Log the first frame of each movie so we can confirm rendering started */
    static int s_log_next = 1;
    if (s_log_next) {
        s_log_next = 0;
        ff7_boot_log("[movie] FRAME: first frame decoded, more=%d tex=%u",
                     more, (unsigned)s_movie_tex);
    }
    if (!more) {
        s_log_next = 1;   /* reset so next movie logs its first frame */
        ff7_boot_log("[movie] FRAME: movie finished");
    }

    return (jint)more;   /* 1 = still playing, 0 = done */
}

void ff7_cb_md_afterFrame(jmethodID id, va_list args) {
    (void)id; (void)args;
    /* Nothing to do: ff7_avi_next_frame already uploaded the frame to GL.
     * The native staff_AVI_afterRenderingOES (SurfaceTexture.updateTexImage)
     * is specific to Android and not called here. */
}

void ff7_cb_md_reset(jmethodID id, va_list args) {
    (void)id; (void)args;
    /* Close the AVI file.  After this ff7_avi_position_ms() returns 0, which
     * lets the game's "wait until GET_POSITION == 0" loop exit immediately. */
    ff7_video_close();
    ff7_boot_log("[movie] RESET: video closed");
}

jint ff7_cb_md_getPosition(jmethodID id, va_list args) {
    (void)id; (void)args;
    return (jint)ff7_video_position_ms();
}

void ff7_cb_md_setPosition(jmethodID id, va_list args) {
    (void)id;
    jint pos = va_arg(args, jint);
    (void)pos;
    /* Seeking within an MJPEG AVI is not implemented — we play sequentially. */
}

void ff7_cb_md_setVolume(jmethodID id, va_list args) {
    (void)id;
    double vol_d = va_arg(args, double);
    jfloat vol = (jfloat)vol_d;

    /* The game's audio fade-in multiplies volume by ~1.27 each tick, starting
     * at 1.0 (full volume). Without clamping the value grows without bound,
     * eventually producing INT_MAX when cast to int, which crashes AVI_setVolume.
     * We treat anything >= 1.0 as full volume.
     *
     * Scale: AVI_setVolume appears to use 0-100 (integer percent), not the
     * MIDI-style 0-127 we originally used.  Passing 127 crashes the function. */
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;

    int vol_i = (int)(vol * 100.0f);
    if (vol_i > 100) vol_i = 100;

    /* Only log on change — the game calls this every frame during fade-in. */
    static int s_last_vol_i = -1;
    if (vol_i != s_last_vol_i) {
        ff7_boot_log("[movie] SET_VOLUME %.2f (raw %d/100)", (double)vol, vol_i);
        s_last_vol_i = vol_i;
    }

    /* AVI_setVolume crashes regardless of value — the OpenSL ES audio engine it
     * talks to is never initialized on Vita (stubs only).  Skip the call; movie
     * audio volume stays at whatever the decoder defaults to. */
    (void)s_avi.avi_set_vol;
}

/* ------------------------------------------------------------------ */
/* ExpansionFile fd tracking table                                    */
/* ------------------------------------------------------------------ */

#define EXP_FD_TABLE_SIZE 32
static struct {
    char path[512];
    int  fd;
    int  in_use;
} s_exp_fd_table[EXP_FD_TABLE_SIZE];

static void exp_fd_track(const char *path, int fd) {
    for (int i = 0; i < EXP_FD_TABLE_SIZE; i++) {
        if (!s_exp_fd_table[i].in_use) {
            strncpy(s_exp_fd_table[i].path, path, sizeof(s_exp_fd_table[i].path) - 1);
            s_exp_fd_table[i].path[sizeof(s_exp_fd_table[i].path) - 1] = '\0';
            s_exp_fd_table[i].fd     = fd;
            s_exp_fd_table[i].in_use = 1;
            return;
        }
    }
    ff7_boot_log("[exp] fd table full, cannot track fd=%d for \"%s\"", fd, path);
}

static int exp_fd_find_and_close(const char *path) {
    /* Close the most-recently opened fd for this path */
    for (int i = EXP_FD_TABLE_SIZE - 1; i >= 0; i--) {
        if (s_exp_fd_table[i].in_use &&
            strcmp(s_exp_fd_table[i].path, path) == 0) {
            int fd = s_exp_fd_table[i].fd;
            close(fd);
            s_exp_fd_table[i].in_use = 0;
            return fd;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* ExpansionFile callbacks                                            */
/* ------------------------------------------------------------------ */

void ff7_cb_exp_closeFd(jmethodID id, va_list args) {
    (void)id;
    jstring jpath = va_arg(args, jstring);
    JNIEnv *env = &jni;
    if (!jpath) return;
    const char *rel = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!rel) return;

    char full[512];
    path_translate_data(rel, full, sizeof(full));

    int fd = exp_fd_find_and_close(full);
    if (fd >= 0)
        ff7_boot_log("[exp] CLOSE_FILE_DESCRIPTOR \"%s\" -> closed fd=%d", full, fd);
    else
        ff7_boot_log("[exp] CLOSE_FILE_DESCRIPTOR \"%s\" -> no tracked fd found", full);

    (*env)->ReleaseStringUTFChars(env, jpath, (char *)rel);
}

jlong ff7_cb_exp_getLength(jmethodID id, va_list args) {
    (void)id;
    jstring jpath = va_arg(args, jstring);
    JNIEnv *env = &jni;
    if (!jpath) return -1;
    const char *rel = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!rel) return -1;

    char full[512];
    path_translate_data(rel, full, sizeof(full));

    struct stat st;
    jlong len = -1;
    if (stat(full, &st) == 0)
        len = (jlong)st.st_size;
    else
        ff7_boot_log("[exp] GET_LENGTH FAIL \"%s\"", full);

    (*env)->ReleaseStringUTFChars(env, jpath, (char *)rel);
    return len;
}

jlong ff7_cb_exp_getStartOffset(jmethodID id, va_list args) {
    (void)id; (void)args;
    return (jlong)0;
}

jint ff7_cb_exp_openFd(jmethodID id, va_list args) {
    (void)id;
    jstring jpath = va_arg(args, jstring);
    JNIEnv *env = &jni;
    if (!jpath) {
        ff7_boot_log("[exp] OPEN_FILE_DESCRIPTOR: jpath is NULL");
        return -1;
    }
    const char *rel = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!rel) {
        ff7_boot_log("[exp] OPEN_FILE_DESCRIPTOR: GetStringUTFChars failed");
        return -1;
    }

    ff7_boot_log("[exp] OPEN_FILE_DESCRIPTOR rel=\"%s\"", rel);

    char full[512];
    path_translate_data(rel, full, sizeof(full));

    int fd = open(full, O_RDONLY);
    if (fd < 0) {
        ff7_boot_log("[exp] OPEN_FILE_DESCRIPTOR FAIL \"%s\" (errno=%d)", full, errno);
    } else {
        ff7_boot_log("[exp] OPEN_FILE_DESCRIPTOR \"%s\" -> fd=%d", full, fd);
        exp_fd_track(full, fd);
    }

    (*env)->ReleaseStringUTFChars(env, jpath, (char *)rel);
    return (jint)fd;
}

void ff7_cb_ma_cloudLaunch(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

jobject ff7_cb_ma_imageGetData(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return NULL;
}

jint ff7_cb_ma_imageGetHeight(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return 0;
}

jint ff7_cb_ma_imageGetWidth(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return 0;
}

void ff7_cb_ma_openUrl(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

void ff7_cb_ma_postDeleteSave(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

void ff7_cb_ma_postError(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

jobject ff7_cb_rw_getYuvPatch(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return NULL;
}

void ff7_cb_se_setVolume(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

void ff7_cb_se_ui_play(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}
