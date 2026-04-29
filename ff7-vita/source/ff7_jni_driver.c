/*
 * ff7_jni_driver.c — Phase 5: JNI-level GL surface and frame driver.
 *
 * Implements the bootstrap and per-frame calls that drive the game via
 * its GLESJniWrapper native exports.  Called from main.c.
 *
 * Bootstrap order:
 *   ff7_jni_bootstrap_pre_gl()   — set data path / asset manager / language
 *   gl_init()                    — VitaGL init (in main.c)
 *   gles_dynlib_wrappers_init()  — resolve late GL proc addresses (in main.c)
 *   ff7_jni_bootstrap_post_gl()  — one warm swap + prepare surface flag
 *   loop: controls_poll(); ff7_jni_render_one_frame();
 */

#include "ff7_jni_driver.h"

#include <string.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "utils/ff7_boot_log.h"
#include "utils/glutil.h"

extern so_module so_mod;

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/ff7/"
#endif

#define FF7_GLES_CLASS "jp/co/d4e/materialg/GLESJniWrapper"

static void (*fn_setDataPath)(JNIEnv *, jclass, jstring);
static void (*fn_setAssetManager)(JNIEnv *, jclass, jobject);
static void (*fn_setLang)(JNIEnv *, jclass, jint);
static void (*fn_onSurfaceCreated)(JNIEnv *, jclass, jboolean, jboolean);
static void (*fn_onSurfaceChanged)(JNIEnv *, jclass, jint, jint);
static void (*fn_onDrawFrame)(JNIEnv *, jclass);
static void (*fn_onPause)(JNIEnv *, jclass);
static void (*fn_onResume)(JNIEnv *, jclass);
static void (*fn_onKey)(JNIEnv *, jclass, jint, jint);
static jint (*fn_onKeyBack)(JNIEnv *, jclass);
static void (*fn_onTouchBegan)(JNIEnv *, jclass, jint, jfloat, jfloat);
static void (*fn_onTouchMoved)(JNIEnv *, jclass, jint, jfloat, jfloat);
static void (*fn_onTouchEnded)(JNIEnv *, jclass, jint, jfloat, jfloat);

static jclass s_gles_class;
/** Defer GLESJniWrapper surface JNI until after at least one swap (VitaGL / loadShaders stability). */
static int s_need_first_surface = 1;

static int resolve_gles_fn(void **dst, const char *sym) {
    void *p = (void *)so_symbol(&so_mod, sym);
    if (!p) {
        ff7_boot_log("[ff7-jni] GLES symbol missing: %s", sym);
        return -1;
    }
    *dst = p;
    return 0;
}

static int ff7_resolve_gles_jni(void) {
    int err = 0;
    err |= resolve_gles_fn((void **)&fn_setDataPath,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_setDataPath");
    err |= resolve_gles_fn((void **)&fn_setAssetManager,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_setAssetManager");
    err |= resolve_gles_fn((void **)&fn_setLang,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_setLang");
    err |= resolve_gles_fn((void **)&fn_onSurfaceCreated,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onSurfaceCreated");
    err |= resolve_gles_fn((void **)&fn_onSurfaceChanged,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onSurfaceChanged");
    err |= resolve_gles_fn((void **)&fn_onDrawFrame,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onDrawFrame");
    err |= resolve_gles_fn((void **)&fn_onPause,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onPause");
    err |= resolve_gles_fn((void **)&fn_onResume,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onResume");
    err |= resolve_gles_fn((void **)&fn_onKey,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onKey");
    err |= resolve_gles_fn((void **)&fn_onKeyBack,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onKeyBack");
    err |= resolve_gles_fn((void **)&fn_onTouchBegan,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onTouchBegan");
    err |= resolve_gles_fn((void **)&fn_onTouchMoved,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onTouchMoved");
    err |= resolve_gles_fn((void **)&fn_onTouchEnded,
                             "Java_jp_co_d4e_materialg_GLESJniWrapper_onTouchEnded");
    return err;
}

static void ff7_jni_resolve_or_log(void) {
    int rc = ff7_resolve_gles_jni();
    ff7_boot_log("[ff7-jni] symbol resolution: setDataPath=%p setAssetManager=%p setLang=%p",
                 (void*)fn_setDataPath, (void*)fn_setAssetManager, (void*)fn_setLang);
    ff7_boot_log("[ff7-jni] symbol resolution: onSurfaceCreated=%p onSurfaceChanged=%p onDrawFrame=%p",
                 (void*)fn_onSurfaceCreated, (void*)fn_onSurfaceChanged, (void*)fn_onDrawFrame);
    ff7_boot_log("[ff7-jni] symbol resolution: onKey=%p onTouchBegan=%p onTouchMoved=%p onTouchEnded=%p",
                 (void*)fn_onKey, (void*)fn_onTouchBegan, (void*)fn_onTouchMoved, (void*)fn_onTouchEnded);
    if (rc != 0)
        ff7_boot_log("[ff7-jni] WARNING: some GLESJniWrapper symbols are missing (see above)");
}

void ff7_jni_bootstrap_pre_gl(void) {
    ff7_boot_log("[ff7-jni] bootstrap pre-gl: start");
    ff7_jni_resolve_or_log();

    JNIEnv *env = &jni;
    ff7_boot_log("[ff7-jni] FindClass(%s)", FF7_GLES_CLASS);
    s_gles_class = (*env)->FindClass(env, FF7_GLES_CLASS);
    if (!s_gles_class) {
        ff7_boot_log("[ff7-jni] FindClass failed");
        return;
    }

    ff7_boot_log("[ff7-jni] NewStringUTF(DATA_PATH)");
    jstring jpath = (*env)->NewStringUTF(env, DATA_PATH);
    if (!jpath) {
        ff7_boot_log("[ff7-jni] NewStringUTF failed");
        return;
    }

    if (fn_setDataPath) {
        ff7_boot_log("[ff7-jni] setDataPath");
        fn_setDataPath(env, s_gles_class, jpath);
    }

    if (fn_setAssetManager) {
        ff7_boot_log("[ff7-jni] setAssetManager");
        fn_setAssetManager(env, s_gles_class, (jobject)(uintptr_t)1);
    }

    if (fn_setLang) {
        ff7_boot_log("[ff7-jni] setLang(1)");
        fn_setLang(env, s_gles_class, 1);
    }

    ff7_boot_log("[ff7-jni] bootstrap pre-gl: done");
}

void ff7_jni_bootstrap_post_gl(void) {
    ff7_boot_log("[ff7-jni] bootstrap post-gl: start (surface JNI deferred to 1st frame)");

    if (!s_gles_class) {
        ff7_boot_log("[ff7-jni] post-gl: no GLES class (pre-gl failed)");
        return;
    }

    /* One swap after vglInitExtended so the display path is live before loadShaders. */
    gl_swap();
    s_need_first_surface = 1;

    ff7_boot_log("[ff7-jni] bootstrap post-gl: done");
}

void ff7_jni_render_one_frame(void) {
    JNIEnv *env = &jni;
    const jint w = 960, h = 544;

    if (s_need_first_surface && s_gles_class) {
        ff7_boot_log("[ff7-jni] first frame: clearing screen");
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ff7_boot_log("[ff7-jni] first frame: glClear done");

        /* Correct Android GLSurfaceView.Renderer order:
         * onSurfaceCreated → onSurfaceChanged → [loop] onDrawFrame */
        if (fn_onSurfaceCreated) {
            /* Two boolean flags: yuvPatch and atomPatch (Samsung workarounds).
             * Always false on non-Samsung hardware. */
            ff7_boot_log("[ff7-jni] calling onSurfaceCreated(false, false)");
            ff7_boot_log("[ff7-jni] env=0x%x s_gles_class=0x%x",
                         (unsigned)(uintptr_t)env,
                         (unsigned)(uintptr_t)s_gles_class);
            fn_onSurfaceCreated(env, s_gles_class, JNI_FALSE, JNI_FALSE);
            ff7_boot_log("[ff7-jni] onSurfaceCreated returned");
        }
        if (fn_onSurfaceChanged) {
            ff7_boot_log("[ff7-jni] calling onSurfaceChanged(%d,%d)", (int)w, (int)h);
            fn_onSurfaceChanged(env, s_gles_class, w, h);
            ff7_boot_log("[ff7-jni] onSurfaceChanged returned");
        }
        s_need_first_surface = 0;
        ff7_boot_log("[ff7-jni] first frame: surface JNI done");
    }

    if (s_gles_class && fn_onDrawFrame) {
        static int draw_frame_count = 0;
        draw_frame_count++;
        if (draw_frame_count <= 3)
            ff7_boot_log("[ff7-jni] onDrawFrame #%d calling", draw_frame_count);
        else
            ff7_boot_log_once("[ff7-jni] onDrawFrame #>3 calling");
        fn_onDrawFrame(env, s_gles_class);
        if (draw_frame_count <= 3)
            ff7_boot_log("[ff7-jni] onDrawFrame #%d returned", draw_frame_count);
        else
            ff7_boot_log_once("[ff7-jni] onDrawFrame #>3 returned");
    }
    gl_swap();
}

static int android_key_action(ControlsAction a) {
    if (a == CONTROLS_ACTION_DOWN)
        return 0;
    if (a == CONTROLS_ACTION_UP)
        return 1;
    return 1;
}

void ff7_jni_on_key(int32_t keycode, ControlsAction action) {
    JNIEnv *env = &jni;
    if (!s_gles_class || !fn_onKey)
        return;
    fn_onKey(env, s_gles_class, (jint)keycode, (jint)android_key_action(action));
}

void ff7_jni_on_touch(int32_t id, float x, float y, ControlsAction action) {
    JNIEnv *env = &jni;
    if (!s_gles_class)
        return;
    jfloat jx = (jfloat)x, jy = (jfloat)y;
    if (action == CONTROLS_ACTION_DOWN && fn_onTouchBegan)
        fn_onTouchBegan(env, s_gles_class, (jint)id, jx, jy);
    else if (action == CONTROLS_ACTION_MOVE && fn_onTouchMoved)
        fn_onTouchMoved(env, s_gles_class, (jint)id, jx, jy);
    else if (action == CONTROLS_ACTION_UP && fn_onTouchEnded)
        fn_onTouchEnded(env, s_gles_class, (jint)id, jx, jy);
}
