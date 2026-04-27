#include "utils/init.h"
#include "utils/dialog.h"
#include "utils/ff7_boot_log.h"
#include "utils/glutil.h"

#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "reimpl/controls.h"

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;

int main() {
    ff7_boot_log_open();
    ff7_boot_log("main() entered");

    soloader_init_all();

    ff7_boot_log("resolving JNI_OnLoad");
    int (* JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    if (!JNI_OnLoad) {
        ff7_boot_log("JNI_OnLoad symbol not found");
        fatal_error("JNI_OnLoad was not exported by the loaded .so.");
    }

    ff7_boot_log("calling JNI_OnLoad");
    JNI_OnLoad(&jvm);
    ff7_boot_log("JNI_OnLoad returned");

    ff7_boot_log("calling gl_init");
    gl_init();
    ff7_boot_log("gl_init returned, entering render loop");

    while (1) {
        // ... render call
        gl_swap();
    }

    sceKernelExitDeleteThread(0);
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    // Call into the .so here
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    // Call into the .so here
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    // Call into the .so here
}
