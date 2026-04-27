/*
 * File log for bring-up: read ux0:data/ff7/debug.log in VitaShell after a crash.
 */

#include "ff7_boot_log.h"

#include "utils/utils.h"

#include <psp2/kernel/clib.h>

#include <stdarg.h>
#include <stdio.h>

static FILE *g_ff7_boot_fp;

void ff7_boot_log_open(void) {
    if (g_ff7_boot_fp) {
        fclose(g_ff7_boot_fp);
        g_ff7_boot_fp = NULL;
    }

    g_ff7_boot_fp = fopen(DATA_PATH "debug.log", "a");
    if (!g_ff7_boot_fp)
        return;

    uint64_t ms = current_timestamp_ms();
    fprintf(g_ff7_boot_fp, "\n======== ff7-vita boot (ts=%llu ms) ========\n",
            (unsigned long long)ms);
    fflush(g_ff7_boot_fp);
}

void ff7_boot_log(const char *fmt, ...) {
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    sceClibPrintf("[ff7-vita] %s\n", buf);

    if (g_ff7_boot_fp) {
        fprintf(g_ff7_boot_fp, "%s\n", buf);
        fflush(g_ff7_boot_fp);
    }
}
