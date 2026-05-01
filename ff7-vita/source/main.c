#include "utils/init.h"
#include "utils/dialog.h"
#include "utils/ff7_boot_log.h"
#include "utils/glutil.h"

#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "reimpl/controls.h"
#include "reimpl/ff7_video_player.h"

#include <stdint.h>
#include <stdio.h>

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;

// JNI native entry-point signatures, mirroring jp.co.d4e.materialg.GLESJniWrapper.
typedef void (*fn_v_v)   (JNIEnv*, jclass);
typedef jint (*fn_i_v)   (JNIEnv*, jclass);
typedef void (*fn_v_i)   (JNIEnv*, jclass, jint);
typedef void (*fn_v_f)   (JNIEnv*, jclass, jfloat);
typedef void (*fn_v_o)   (JNIEnv*, jclass, jobject);
typedef void (*fn_v_s)   (JNIEnv*, jclass, jstring);
typedef void (*fn_v_zz)  (JNIEnv*, jclass, jboolean, jboolean);
typedef void (*fn_v_ii)  (JNIEnv*, jclass, jint, jint);
typedef void (*fn_v_iz)  (JNIEnv*, jclass, jint, jboolean);
typedef void (*fn_v_iff) (JNIEnv*, jclass, jint, jfloat, jfloat);

static fn_v_i   fn_setLang;
static fn_v_f   fn_setBatteryLevel;
static fn_v_s   fn_setDataPath;
static fn_v_o   fn_setAssetManager;
static fn_v_zz  fn_onSurfaceCreated;
static fn_v_ii  fn_onSurfaceChanged;
static fn_v_v   fn_onResume;
static fn_v_v   fn_onPause;
static fn_v_v   fn_onDrawFrame;
static fn_v_iz  fn_onKey;
static fn_i_v   fn_onKeyBack;
static fn_v_iff fn_onTouchBegan;
static fn_v_iff fn_onTouchEnded;
static fn_v_iff fn_onTouchMoved;
static fn_v_v   fn_callUpdateTitlemenu;
static fn_v_v   fn_updateSaveDataFile;

static uintptr_t resolve_native(const char* short_name) {
    char full[160];
    snprintf(full, sizeof(full),
             "Java_jp_co_d4e_materialg_GLESJniWrapper_%s", short_name);
    uintptr_t sym = so_symbol(&so_mod, full);
    if (!sym) {
        ff7_boot_log("symbol %s not found", full);
        fatal_error("Required native symbol %s missing.", full);
    }
    return sym;
}

static void resolve_all_natives(void) {
    fn_setLang             = (fn_v_i)   resolve_native("setLang");
    fn_setBatteryLevel     = (fn_v_f)   resolve_native("setBatteryLevel");
    fn_setDataPath         = (fn_v_s)   resolve_native("setDataPath");
    fn_setAssetManager     = (fn_v_o)   resolve_native("setAssetManager");
    fn_onSurfaceCreated    = (fn_v_zz)  resolve_native("onSurfaceCreated");
    fn_onSurfaceChanged    = (fn_v_ii)  resolve_native("onSurfaceChanged");
    fn_onResume            = (fn_v_v)   resolve_native("onResume");
    fn_onPause             = (fn_v_v)   resolve_native("onPause");
    fn_onDrawFrame         = (fn_v_v)   resolve_native("onDrawFrame");
    fn_onKey               = (fn_v_iz)  resolve_native("onKey");
    fn_onKeyBack           = (fn_i_v)   resolve_native("onKeyBack");
    fn_onTouchBegan        = (fn_v_iff) resolve_native("onTouchBegan");
    fn_onTouchEnded        = (fn_v_iff) resolve_native("onTouchEnded");
    fn_onTouchMoved        = (fn_v_iff) resolve_native("onTouchMoved");
    fn_callUpdateTitlemenu = (fn_v_v)   resolve_native("callUpdateTitlemenu");
    fn_updateSaveDataFile  = (fn_v_v)   resolve_native("updateSaveDataFile");
}

int main() {
    ff7_boot_log_open();
    ff7_boot_log("main() entered");

    soloader_init_all();

    ff7_video_init();

    ff7_boot_log("resolving JNI_OnLoad");
    int (* JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    if (!JNI_OnLoad) {
        ff7_boot_log("JNI_OnLoad symbol not found");
        fatal_error("JNI_OnLoad was not exported by the loaded .so.");
    }

    ff7_boot_log("calling JNI_OnLoad");
    int jni_rc = JNI_OnLoad(&jvm);
    ff7_boot_log("JNI_OnLoad returned: 0x%x", jni_rc);

    ff7_boot_log("resolving native entry points");
    resolve_all_natives();
    ff7_boot_log("native entry points resolved");

    // Mirror Android MainActivity.onCreate ordering. Lang map (Locale.getLanguage):
    // en=0, fr=1, de=2, es=3, ja=4. Default to English.
    ff7_boot_log("setLang(0)");
    fn_setLang(&jni, NULL, 0);

    // setBatteryLevel takes a float in [0..1] on Android; report full charge.
    ff7_boot_log("setBatteryLevel(1.0)");
    fn_setBatteryLevel(&jni, NULL, 1.0f);

    // The .so saves DATA_PATH and appends ./Documents internally for save files.
    ff7_boot_log("setDataPath(\"" DATA_PATH "\")");
    jstring js_data_path = jni->NewStringUTF(&jni, DATA_PATH);
    fn_setDataPath(&jni, NULL, js_data_path);

    // setAssetManager just hands an opaque jobject through to AAssetManager_fromJava,
    // which in our reimpl ignores the cookie. Pass any non-null sentinel.
    ff7_boot_log("setAssetManager(<sentinel>)");
    fn_setAssetManager(&jni, NULL, (jobject)(uintptr_t)1);

    ff7_boot_log("calling gl_init");
    gl_init();
    ff7_boot_log("gl_init returned");

    // RendererWrapper.onSurfaceChanged() invokes the native onSurfaceChanged BEFORE
    // onSurfaceCreated; replicate that order. Vita is fixed 960x544 landscape.
    ff7_boot_log("onSurfaceChanged(960, 544)");
    fn_onSurfaceChanged(&jni, NULL, 960, 544);

    // YUV_patch_20160713 + ATOM_patch_20161018 are workarounds for specific Samsung
    // devices; both default to false on every other Android handset, including ours.
    ff7_boot_log("onSurfaceCreated(false, false)");
    fn_onSurfaceCreated(&jni, NULL, JNI_FALSE, JNI_FALSE);

    ff7_boot_log("onResume()");
    fn_onResume(&jni, NULL);

    ff7_boot_log("entering frame loop");
    uint32_t frame = 0;
    for (;;) {
        controls_poll();
        fn_onDrawFrame(&jni, NULL);
        gl_swap();
        if (((frame++) & 0x7F) == 0) {
            ff7_boot_log("frame %u", frame);
        }
    }

    sceKernelExitDeleteThread(0);
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    if (!fn_onKey) return;
    if (action == CONTROLS_ACTION_DOWN) {
        fn_onKey(&jni, NULL, keycode, JNI_TRUE);
    } else if (action == CONTROLS_ACTION_UP) {
        fn_onKey(&jni, NULL, keycode, JNI_FALSE);
    }
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    switch (action) {
        case CONTROLS_ACTION_DOWN:
            if (fn_onTouchBegan) fn_onTouchBegan(&jni, NULL, id, x, y);
            break;
        case CONTROLS_ACTION_MOVE:
            if (fn_onTouchMoved) fn_onTouchMoved(&jni, NULL, id, x, y);
            break;
        case CONTROLS_ACTION_UP:
            if (fn_onTouchEnded) fn_onTouchEnded(&jni, NULL, id, x, y);
            break;
    }
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    // The Android binary has no native analog-stick entry point; sticks are not used.
    (void)which; (void)x; (void)y; (void)action;
}
