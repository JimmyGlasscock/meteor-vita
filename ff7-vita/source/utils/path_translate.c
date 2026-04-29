#include "utils/path_translate.h"

#include <stdio.h>
#include <string.h>

static const char OBB_ROOT[] = "ff7_1.02/";

static const char *strip_leading_slashes(const char *p) {
    while (*p == '/') ++p;
    return p;
}

static void normalize_backslashes(char *s) {
    for (char *p = s; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
}

static bool has_obb_root(const char *p) {
    const size_t n = sizeof(OBB_ROOT) - 1;
    return strncmp(p, OBB_ROOT, n) == 0;
}

/** Native strings that omit /ff7_1.02/ but refer to OBB files on disk. */
static bool is_bare_obb_data_path(const char *p) {
    return !strncmp(p, "data/", 5) || !strncmp(p, "save/", 5);
}

void path_translate_data(const char *in, char *out, size_t out_sz) {
    if (!in || !*in) {
        snprintf(out, out_sz, "%s", DATA_PATH);
        return;
    }

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", in);
    normalize_backslashes(tmp);

    if (strchr(tmp, ':')) {
        snprintf(out, out_sz, "%s", tmp);
        return;
    }

    const char *p = strip_leading_slashes(tmp);
    /* Bare data/... and save/... live under ff7_1.02/ on Vita (no top-level data/). */
    if (has_obb_root(p) || !is_bare_obb_data_path(p)) {
        snprintf(out, out_sz, "%s%s", DATA_PATH, p);
    } else {
        snprintf(out, out_sz, "%s%s%s", DATA_PATH, OBB_ROOT, p);
    }
}

bool path_translate_asset(const char *in, char *out, size_t out_sz) {
    if (!in || !*in) {
        snprintf(out, out_sz, "%s", DATA_PATH);
        return false;
    }

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", in);
    normalize_backslashes(tmp);

    if (strchr(tmp, ':')) {
        snprintf(out, out_sz, "%s", tmp);
        return true;
    }

    const char *p = strip_leading_slashes(tmp);

    if (has_obb_root(p)) {
        snprintf(out, out_sz, "%s%s", DATA_PATH, p);
        return true;
    }
    if (is_bare_obb_data_path(p)) {
        snprintf(out, out_sz, "%s%s%s", DATA_PATH, OBB_ROOT, p);
        return true;
    }
    snprintf(out, out_sz, "%sassets/%s", DATA_PATH, p);
    return false;
}
