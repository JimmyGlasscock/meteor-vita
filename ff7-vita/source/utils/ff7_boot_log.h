/*
 * Append-only boot log at DATA_PATH/debug.log (survives after C2 crashes if I/O flushes).
 */

#ifndef FF7_BOOT_LOG_H
#define FF7_BOOT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void ff7_boot_log_open(void);
void ff7_boot_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif
