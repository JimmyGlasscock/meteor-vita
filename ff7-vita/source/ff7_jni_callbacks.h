/*
 * FalsoJNI method handlers for libjni_ff7.so (Phase 3).
 * Method keys are "jni/class/path/methodName" (see FalsoJNI GetMethodID change).
 */

#ifndef FF7_JNI_CALLBACKS_H
#define FF7_JNI_CALLBACKS_H

#include <falso_jni/FalsoJNI_ImplBridge.h>

void ff7_cb_md_setTexture(jmethodID id, va_list args);
jint ff7_cb_md_start(jmethodID id, va_list args);
jint ff7_cb_md_getTotalTime(jmethodID id, va_list args);
jint ff7_cb_md_frame(jmethodID id, va_list args);
void ff7_cb_md_afterFrame(jmethodID id, va_list args);
void ff7_cb_md_reset(jmethodID id, va_list args);
jint ff7_cb_md_getPosition(jmethodID id, va_list args);
void ff7_cb_md_setPosition(jmethodID id, va_list args);
void ff7_cb_md_setVolume(jmethodID id, va_list args);

void ff7_cb_exp_closeFd(jmethodID id, va_list args);
jlong ff7_cb_exp_getLength(jmethodID id, va_list args);
jlong ff7_cb_exp_getStartOffset(jmethodID id, va_list args);
jint ff7_cb_exp_openFd(jmethodID id, va_list args);

void ff7_cb_ma_cloudLaunch(jmethodID id, va_list args);
jobject ff7_cb_ma_imageGetData(jmethodID id, va_list args);
jint ff7_cb_ma_imageGetHeight(jmethodID id, va_list args);
jint ff7_cb_ma_imageGetWidth(jmethodID id, va_list args);
void ff7_cb_ma_openUrl(jmethodID id, va_list args);
void ff7_cb_ma_postDeleteSave(jmethodID id, va_list args);
void ff7_cb_ma_postError(jmethodID id, va_list args);

jobject ff7_cb_rw_getYuvPatch(jmethodID id, va_list args);

void ff7_cb_se_setVolume(jmethodID id, va_list args);
void ff7_cb_se_ui_play(jmethodID id, va_list args);

#endif
