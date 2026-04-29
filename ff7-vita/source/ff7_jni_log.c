/*
 * ff7_jni_log.c — redirect FalsoJNI serial output to the persistent boot log file.
 *
 * FalsoJNI_Logger uses sceClibPrintf (serial/netcat only).  By overriding the
 * weak symbol fjni_file_log_hook we also write every JNI log line to
 * DATA_PATH/debug.log so we can read them after a crash in VitaShell.
 *
 * All log levels are forwarded so we can see which JNI calls happen inside
 * onSurfaceCreated before the crash.
 */

#include "utils/ff7_boot_log.h"

/* Strong override of the weak symbol defined in FalsoJNI_Logger.c. */
void fjni_file_log_hook(const char *msg) {
    /* msg is already fully formatted — use %s to avoid re-interpreting
     * any '%' characters that might appear in shader source strings etc. */
    ff7_boot_log("%s", msg);
}
