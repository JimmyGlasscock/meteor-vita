/*
 * Unified Android-style path translation for the FF7 port.
 *
 * The native library hands us paths from three different worlds:
 *   - APK assets: bare relative paths like "Shaders/Shader.vsh".
 *   - OBB-rooted paths: "/ff7_1.02/data/...", "ff7_1.02/ff7_en.exe", or
 *     even "//ff7_1.02/...".
 *   - Already-Vita paths: "ux0:data/ff7/...".
 *
 * On-disk layout keeps OBB content under ff7_1.02/. Examples:
 *   /ff7_1.02/data/movies/foo.mp4  →  ux0:data/ff7/ff7_1.02/data/movies/foo.mp4
 *   data\fhuda.tim                 →  ux0:data/ff7/ff7_1.02/data/fhuda.tim
 *   save\seed.dat                  →  ux0:data/ff7/ff7_1.02/save/seed.dat
 *   Shaders/Shader.fsh (AAsset)    →  ux0:data/ff7/assets/Shaders/Shader.fsh
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
 *   4. Strip every leading '/'.
 *   5. Prepend DATA_PATH. If the remainder starts with ff7_1.02/, keep it;
 *      if it starts with data/ or save/ alone, insert ff7_1.02/ before it.
 */
void path_translate_data(const char *in, char *out, size_t out_sz);

/**
 * Translate `in` for an AAssetManager-style lookup.
 * OBB-rooted paths and bare data/ or save/ resolve like path_translate_data.
 * All other bare paths get DATA_PATH + "assets/" + path.
 *
 * @return  true if resolved under ff7_1.02/ (explicit or bare data/save);
 *          false if the "assets/" prefix was used or path was already absolute.
 */
bool path_translate_asset(const char *in, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif // SOLOADER_PATH_TRANSLATE_H
