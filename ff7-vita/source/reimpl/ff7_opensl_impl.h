/*
 * ff7_opensl_impl.h — Android-compatible OpenSL ES reimplementation for FF7 Vita.
 *
 * VitaSDK's own OpenSL ES has a DIFFERENT SLObjectItf_ vtable slot ordering
 * than Android NDK's:
 *
 *   Slot  Android NDK           VitaSDK
 *    3    Destroy               GetInterface  ← fatal mismatch
 *    9    GetInterface          (not at slot 9)
 *
 * The game's .so was compiled with Android NDK headers and dispatches through
 * Android's slot ordering.  Using VitaSDK's slCreateEngine would cause
 * incorrect functions to be called, resulting in immediate crashes.
 *
 * This module provides a complete custom implementation:
 *   - Vtables with Android NDK slot ordering
 *   - SceAudio as the audio output backend
 *   - Full SLAndroidSimpleBufferQueueItf support with per-buffer callbacks
 *   - Up to FF7_OPENSL_MAX_PLAYERS simultaneous audio players
 */

#ifndef FF7_OPENSL_IMPL_H
#define FF7_OPENSL_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <SLES/OpenSLES.h>

/*
 * Drop-in replacement for slCreateEngine.
 * Maps to "slCreateEngine" in dynlib.c.
 */
SLresult slCreateEngine_vita(SLObjectItf *pEngine,
                              SLuint32 numOptions,
                              const SLEngineOption *pEngineOptions,
                              SLuint32 numInterfaces,
                              const SLInterfaceID *pInterfaceIds,
                              const SLboolean *pInterfaceRequired);

#ifdef __cplusplus
}
#endif

#endif /* FF7_OPENSL_IMPL_H */
