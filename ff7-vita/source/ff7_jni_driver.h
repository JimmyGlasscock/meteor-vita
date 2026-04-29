#ifndef FF7_JNI_DRIVER_H
#define FF7_JNI_DRIVER_H

#include <stdint.h>

#include <reimpl/controls.h>

/** Path / asset / language before GL (matches typical Android activity order). */
void ff7_jni_bootstrap_pre_gl(void);

/** One swap after vglInitExtended; GLESJniWrapper surface JNI runs on first draw frame. */
void ff7_jni_bootstrap_post_gl(void);

void ff7_jni_render_one_frame(void);

void ff7_jni_on_key(int32_t keycode, ControlsAction action);
void ff7_jni_on_touch(int32_t id, float x, float y, ControlsAction action);

#endif
