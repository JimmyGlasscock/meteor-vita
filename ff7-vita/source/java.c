/*
 * java.c
 *
 * FalsoJNI tables and handlers covering everything libjni_ff7.so calls back
 * into the JVM through.
 *
 * Phase-3 inventory was produced by:
 *   - Listing exported `Java_*` natives (these are inbound and don't need
 *     handlers — they are called from main.c via so_symbol).
 *   - Scanning .rodata of libjni_ff7.so for class names, method names and JNI
 *     signatures (FindClass / GetMethodID / GetStaticMethodID inputs).
 *   - Cross-checking against baksmali output of the APK to confirm exact
 *     signatures, return types, and what each Java method actually does.
 *
 * Methods covered (23 total, all looked up by name only — FalsoJNI is not
 * class-aware):
 *   ExpansionFile.OPEN_FILE_DESCRIPTOR / CLOSE_FILE_DESCRIPTOR /
 *                  GET_START_OFFSET / GET_LENGTH
 *   MainActivity.CloudLaunch / OpenURL / postErrorMessage / postDeleteSaveFile
 *                ImageGetData / ImageGetWidth / ImageGetHeight
 *   SEPlayer.PLAY / SETVOLUME
 *   MyDecoder.START / FRAME / AFTER_FRAME / RESET / SET_POSITION /
 *             SET_TEXTURE / SET_VOLUME / GET_POSITION / GET_TOTALTIME
 *   RendererWrapper.GET_YUV_PATCH
 *
 * Most handlers log + return safe defaults so the game can keep booting; the
 * ExpansionFile family is wired to the local data tree so file-descriptor
 * based reads from native code work without mounting an actual OBB zip.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_ImplBridge.h>
#include <falso_jni/FalsoJNI_Logger.h>

#include "utils/ff7_boot_log.h"
#include "utils/path_translate.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/*
 * Pull a NUL-terminated UTF-8 view out of a jstring without involving the
 * public JNIEnv (no allocation, no copy). FalsoJNI's NewStringUTF lazily
 * builds the utf8 buffer; if it is missing for some reason (e.g. the string
 * came from NewString) we promote utf16 -> utf8 in place.
 *
 * Returns a static fallback for NULL strings so logs stay readable.
 */
static const char *jstr_cstr(jstring str) {
    if (!str) return "(null)";

    JavaString *js = (JavaString *)str;
    if (js->utf8 && js->utf8->array) {
        return (const char *)js->utf8->array;
    }
    if (js->utf16) {
        if (jstr_utf16_to_utf8(js) == JNI_TRUE && js->utf8 && js->utf8->array) {
            return (const char *)js->utf8->array;
        }
    }
    return "(unreadable)";
}

/*
 * Translate an Android-style asset path (relative, possibly with backslashes,
 * possibly OBB-rooted) into something openable from the Vita filesystem.
 * Output is written into `out` which must be at least `out_sz` bytes.
 *
 * The .so passes us a few flavours of path through this function (mostly via
 * ExpansionFile.OPEN_FILE_DESCRIPTOR / GET_LENGTH):
 *
 *   - Pure asset names      e.g. "data\fhuda.tim", "Shaders/Shader.vsh"
 *   - OBB-rooted absolutes  e.g. "/ff7_1.02/data/movies/staffroll.avi",
 *                                "/ff7_1.02/save/savefile.dat"
 *   - Already-Vita absolutes e.g. "ux0:data/ff7/foo" (defensive)
 *
 * The actual normalisation lives in utils/path_translate so that asset_manager
 * and io.c can apply the same rules.
 */
static void translate_asset_path(const char *in, char *out, size_t out_sz) {
    path_translate_data(in, out, out_sz);
}

/* ------------------------------------------------------------------ */
/* JNI method IDs                                                     */
/* ------------------------------------------------------------------ */

enum {
    /* ExpansionFile */
    MID_OPEN_FILE_DESCRIPTOR = 1,
    MID_CLOSE_FILE_DESCRIPTOR,
    MID_GET_START_OFFSET,
    MID_GET_LENGTH,

    /* MainActivity */
    MID_CLOUD_LAUNCH,
    MID_OPEN_URL,
    MID_POST_ERROR_MESSAGE,
    MID_POST_DELETE_SAVE_FILE,
    MID_IMAGE_GET_DATA,
    MID_IMAGE_GET_WIDTH,
    MID_IMAGE_GET_HEIGHT,

    /* SEPlayer */
    MID_PLAY,
    MID_SETVOLUME,

    /* MyDecoder */
    MID_START,
    MID_FRAME,
    MID_AFTER_FRAME,
    MID_RESET,
    MID_SET_POSITION,
    MID_SET_TEXTURE,
    MID_SET_VOLUME,
    MID_GET_POSITION,
    MID_GET_TOTALTIME,

    /* RendererWrapper */
    MID_GET_YUV_PATCH,
};

/* ------------------------------------------------------------------ */
/* ExpansionFile family                                               */
/*                                                                    */
/* On Android the OBB is a zip and these methods return a *real* fd to*/
/* that zip plus an offset+length to the entry inside. On Vita we     */
/* expose individual extracted files from DATA_PATH directly: open()  */
/* the file, return the raw fd; offset is always 0; length is the     */
/* file size.                                                         */
/* ------------------------------------------------------------------ */

static jint mh_open_file_descriptor(jmethodID id, va_list args) {
    (void)id;
    jstring path = va_arg(args, jstring);
    const char *raw = jstr_cstr(path);

    char full[512];
    translate_asset_path(raw, full, sizeof(full));

    int fd = open(full, O_RDONLY);
    ff7_boot_log("[JNI] OPEN_FILE_DESCRIPTOR(\"%s\") -> %d", full, fd);
    return (jint)fd;
}

static void mh_close_file_descriptor(jmethodID id, va_list args) {
    (void)id;
    jstring path = va_arg(args, jstring);
    /* We don't keep a name->fd map here: native callers either close the fd
     * directly or call this purely as a hint. Logging is enough for now. */
    ff7_boot_log("[JNI] CLOSE_FILE_DESCRIPTOR(\"%s\")", jstr_cstr(path));
}

static jlong mh_get_start_offset(jmethodID id, va_list args) {
    (void)id;
    jstring path = va_arg(args, jstring);
    /* Each asset is a standalone file on disk, so its data starts at byte 0. */
    ff7_boot_log("[JNI] GET_START_OFFSET(\"%s\") -> 0", jstr_cstr(path));
    return 0;
}

static jlong mh_get_length(jmethodID id, va_list args) {
    (void)id;
    jstring path = va_arg(args, jstring);
    const char *raw = jstr_cstr(path);

    char full[512];
    translate_asset_path(raw, full, sizeof(full));

    struct stat st;
    jlong len = -1;
    if (stat(full, &st) == 0) {
        len = (jlong)st.st_size;
    }
    ff7_boot_log("[JNI] GET_LENGTH(\"%s\") -> %lld", full, (long long)len);
    return len;
}

/* ------------------------------------------------------------------ */
/* MainActivity helpers                                               */
/* ------------------------------------------------------------------ */

static void mh_cloud_launch(jmethodID id, va_list args) {
    (void)id; (void)args;
    /* The Android version forks a "cloud" companion activity; on Vita we
     * have no such thing. Acknowledge and move on. */
    ff7_boot_log("[JNI] CloudLaunch(): ignored");
}

static void mh_open_url(jmethodID id, va_list args) {
    (void)id;
    jstring url = va_arg(args, jstring);
    ff7_boot_log("[JNI] OpenURL(\"%s\"): ignored", jstr_cstr(url));
}

static void mh_post_error_message(jmethodID id, va_list args) {
    (void)id;
    jint code = va_arg(args, jint);
    ff7_boot_log("[JNI] postErrorMessage(%d): ignored", (int)code);
}

static void mh_post_delete_save_file(jmethodID id, va_list args) {
    (void)id;
    jint slot = va_arg(args, jint);
    ff7_boot_log("[JNI] postDeleteSaveFile(%d): ignored", (int)slot);
}

static jobject mh_image_get_data(jmethodID id, va_list args) {
    (void)id;
    jstring name = va_arg(args, jstring);
    /* Returning null tells native code the image was not loadable; the catch
     * block in MainActivity.ImageGetData already maps IOException to null,
     * so the caller is required to handle it. Phase 4 will plug in real
     * decoding via libpng / stbi. */
    ff7_boot_log("[JNI] ImageGetData(\"%s\"): stub null", jstr_cstr(name));
    return NULL;
}

static jint mh_image_get_width(jmethodID id, va_list args) {
    (void)id;
    jstring name = va_arg(args, jstring);
    ff7_boot_log("[JNI] ImageGetWidth(\"%s\"): stub 0", jstr_cstr(name));
    return 0;
}

static jint mh_image_get_height(jmethodID id, va_list args) {
    (void)id;
    jstring name = va_arg(args, jstring);
    ff7_boot_log("[JNI] ImageGetHeight(\"%s\"): stub 0", jstr_cstr(name));
    return 0;
}

/* ------------------------------------------------------------------ */
/* SEPlayer (sound effects)                                           */
/* ------------------------------------------------------------------ */

static void mh_se_play(jmethodID id, va_list args) {
    (void)id;
    jint se_id = va_arg(args, jint);
    ff7_boot_log("[JNI] SEPlayer.PLAY(%d): stub", (int)se_id);
}

static void mh_se_setvolume(jmethodID id, va_list args) {
    (void)id;
    jfloat v = (jfloat)va_arg(args, double);
    ff7_boot_log("[JNI] SEPlayer.SETVOLUME(%f): stub", (double)v);
}

/* ------------------------------------------------------------------ */
/* MyDecoder (movie playback)                                         */
/*                                                                    */
/* We have no movie pipeline yet, so START reports failure (return 0) */
/* and the rest are inert.                                            */
/* ------------------------------------------------------------------ */

static jint mh_dec_start(jmethodID id, va_list args) {
    (void)id;
    jstring path = va_arg(args, jstring);
    ff7_boot_log("[JNI] MyDecoder.START(\"%s\"): stub 0 (skip movie)",
                 jstr_cstr(path));
    return 0;
}

static jint mh_dec_frame(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 0;
}

static void mh_dec_after_frame(jmethodID id, va_list args) {
    (void)id; (void)args;
}

static void mh_dec_reset(jmethodID id, va_list args) {
    (void)id; (void)args;
    ff7_boot_log("[JNI] MyDecoder.RESET()");
}

static void mh_dec_set_position(jmethodID id, va_list args) {
    (void)id;
    jint pos = va_arg(args, jint);
    ff7_boot_log("[JNI] MyDecoder.SET_POSITION(%d)", (int)pos);
}

static void mh_dec_set_texture(jmethodID id, va_list args) {
    (void)id;
    jint tex = va_arg(args, jint);
    ff7_boot_log("[JNI] MyDecoder.SET_TEXTURE(%d)", (int)tex);
}

static void mh_dec_set_volume(jmethodID id, va_list args) {
    (void)id;
    jfloat v = (jfloat)va_arg(args, double);
    ff7_boot_log("[JNI] MyDecoder.SET_VOLUME(%f)", (double)v);
}

static jint mh_dec_get_position(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 0;
}

static jint mh_dec_get_totaltime(jmethodID id, va_list args) {
    (void)id; (void)args;
    return 0;
}

/* ------------------------------------------------------------------ */
/* RendererWrapper                                                    */
/* ------------------------------------------------------------------ */

static jboolean mh_get_yuv_patch(jmethodID id, va_list args) {
    (void)id; (void)args;
    /* GLES2 on Vita lacks the EXT_paletted_texture / YUV-patch extension this
     * checks for. Pretend we don't have it; the native side has a fallback. */
    return JNI_FALSE;
}

/* ------------------------------------------------------------------ */
/* Tables                                                             */
/* ------------------------------------------------------------------ */

NameToMethodID nameToMethodId[] = {
    /* ExpansionFile */
    { MID_OPEN_FILE_DESCRIPTOR,  "OPEN_FILE_DESCRIPTOR",  METHOD_TYPE_INT    },
    { MID_CLOSE_FILE_DESCRIPTOR, "CLOSE_FILE_DESCRIPTOR", METHOD_TYPE_VOID   },
    { MID_GET_START_OFFSET,      "GET_START_OFFSET",      METHOD_TYPE_LONG   },
    { MID_GET_LENGTH,            "GET_LENGTH",            METHOD_TYPE_LONG   },

    /* MainActivity */
    { MID_CLOUD_LAUNCH,          "CloudLaunch",           METHOD_TYPE_VOID   },
    { MID_OPEN_URL,              "OpenURL",               METHOD_TYPE_VOID   },
    { MID_POST_ERROR_MESSAGE,    "postErrorMessage",      METHOD_TYPE_VOID   },
    { MID_POST_DELETE_SAVE_FILE, "postDeleteSaveFile",    METHOD_TYPE_VOID   },
    { MID_IMAGE_GET_DATA,        "ImageGetData",          METHOD_TYPE_OBJECT },
    { MID_IMAGE_GET_WIDTH,       "ImageGetWidth",         METHOD_TYPE_INT    },
    { MID_IMAGE_GET_HEIGHT,      "ImageGetHeight",        METHOD_TYPE_INT    },

    /* SEPlayer */
    { MID_PLAY,                  "PLAY",                  METHOD_TYPE_VOID   },
    { MID_SETVOLUME,             "SETVOLUME",             METHOD_TYPE_VOID   },

    /* MyDecoder */
    { MID_START,                 "START",                 METHOD_TYPE_INT    },
    { MID_FRAME,                 "FRAME",                 METHOD_TYPE_INT    },
    { MID_AFTER_FRAME,           "AFTER_FRAME",           METHOD_TYPE_VOID   },
    { MID_RESET,                 "RESET",                 METHOD_TYPE_VOID   },
    { MID_SET_POSITION,          "SET_POSITION",          METHOD_TYPE_VOID   },
    { MID_SET_TEXTURE,           "SET_TEXTURE",           METHOD_TYPE_VOID   },
    { MID_SET_VOLUME,            "SET_VOLUME",            METHOD_TYPE_VOID   },
    { MID_GET_POSITION,          "GET_POSITION",          METHOD_TYPE_INT    },
    { MID_GET_TOTALTIME,         "GET_TOTALTIME",         METHOD_TYPE_INT    },

    /* RendererWrapper */
    { MID_GET_YUV_PATCH,         "GET_YUV_PATCH",         METHOD_TYPE_BOOLEAN },
};

MethodsBoolean methodsBoolean[] = {
    { MID_GET_YUV_PATCH, mh_get_yuv_patch },
};

MethodsByte    methodsByte[]    = {};
MethodsChar    methodsChar[]    = {};
MethodsDouble  methodsDouble[]  = {};
MethodsFloat   methodsFloat[]   = {};

MethodsInt methodsInt[] = {
    { MID_OPEN_FILE_DESCRIPTOR,  mh_open_file_descriptor },
    { MID_IMAGE_GET_WIDTH,       mh_image_get_width      },
    { MID_IMAGE_GET_HEIGHT,      mh_image_get_height     },
    { MID_START,                 mh_dec_start            },
    { MID_FRAME,                 mh_dec_frame            },
    { MID_GET_POSITION,          mh_dec_get_position     },
    { MID_GET_TOTALTIME,         mh_dec_get_totaltime    },
};

MethodsLong methodsLong[] = {
    { MID_GET_START_OFFSET, mh_get_start_offset },
    { MID_GET_LENGTH,       mh_get_length       },
};

MethodsObject methodsObject[] = {
    { MID_IMAGE_GET_DATA, mh_image_get_data },
};

MethodsShort methodsShort[] = {};

MethodsVoid methodsVoid[] = {
    { MID_CLOSE_FILE_DESCRIPTOR, mh_close_file_descriptor },
    { MID_CLOUD_LAUNCH,          mh_cloud_launch          },
    { MID_OPEN_URL,              mh_open_url              },
    { MID_POST_ERROR_MESSAGE,    mh_post_error_message    },
    { MID_POST_DELETE_SAVE_FILE, mh_post_delete_save_file },
    { MID_PLAY,                  mh_se_play               },
    { MID_SETVOLUME,             mh_se_setvolume          },
    { MID_AFTER_FRAME,           mh_dec_after_frame       },
    { MID_RESET,                 mh_dec_reset             },
    { MID_SET_POSITION,          mh_dec_set_position      },
    { MID_SET_TEXTURE,           mh_dec_set_texture       },
    { MID_SET_VOLUME,            mh_dec_set_volume        },
};

/* ------------------------------------------------------------------ */
/* JNI fields                                                         */
/* ------------------------------------------------------------------ */

/* Context.WINDOW_SERVICE — Android system service constant. */
char WINDOW_SERVICE[] = "window";

/* Build.VERSION.SDK_INT — claim KitKat so platform-version checks behave. */
const int SDK_INT = 19;

NameToFieldID nameToFieldId[] = {
    { 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
    { 1, "SDK_INT",        FIELD_TYPE_INT    },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte    fieldsByte[]    = {};
FieldsChar    fieldsChar[]    = {};
FieldsDouble  fieldsDouble[]  = {};
FieldsFloat   fieldsFloat[]   = {};

FieldsInt fieldsInt[] = {
    { 1, SDK_INT },
};

FieldsObject fieldsObject[] = {
    { 0, WINDOW_SERVICE },
};

FieldsLong  fieldsLong[]  = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
