/*
 * Unified Android-style path translation for the FF7 port.
 *
 * The native library hands us paths from three different worlds:
 *   - APK assets: bare relative paths like "Shaders/Shader.vsh".
 *   - OBB-rooted paths: "/ff7_1.02/data/...", "ff7_1.02/ff7_en.exe", or
 *     even "//ff7_1.02/...".
 *   - Already-Vita paths: "ux0:data/ff7/...".
 *
 * We flatten all OBB content directly under DATA_PATH on the SD card, so
 * the translator's job is to coalesce these into a single Vita-friendly
 * absolute path.
 */

#ifndef SOLOADER_PATH_TRANSLATE_H
#define SOLOADER_PATH_TRANSLATE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Translate `in` into a Vita-style absolute path written into `out`.
 *
 * Rules, applied in order:
 *   1. NULL / empty input -> just DATA_PATH.
 *   2. Convert backslashes to forward slashes.
 *   3. If a Vita device prefix is already present (':'), use as-is.
 *   4. Skip every leading '/'.
 *   5. Strip a leading "ff7_1.02/" (OBB root).
 *   6. Prepend DATA_PATH.
 *
 * Note: this never prepends "assets/". Asset-bundle callers must do that
 * themselves on the OBB-prefix-miss path; see path_translate_asset.
 */
void path_translate_data(const char *in, char *out, size_t out_sz);

/**
 * Translate `in` for an APK asset lookup. Same rules as path_translate_data
 * EXCEPT that bare paths (those that don't reference the OBB or a Vita
 * device) get an additional "assets/" segment so that they resolve to the
 * APK's bundled assets tree on disk.
 *
 * @return  true if the path was treated as an OBB/absolute path, false if
 *          the "assets/" prefix was applied. Useful for callers that want
 *          to log which routing rule fired.
 */
bool path_translate_asset(const char *in, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif // SOLOADER_PATH_TRANSLATE_H
