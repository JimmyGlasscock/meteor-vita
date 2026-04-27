#include "utils/path_translate.h"

#include <stdio.h>
#include <string.h>

static const char OBB_ROOT[] = "ff7_1.02/";

static const char *strip_leading_and_obb(const char *p, bool *out_was_obb) {
    while (*p == '/') ++p;

    const size_t obb_len = sizeof(OBB_ROOT) - 1;
    if (strncmp(p, OBB_ROOT, obb_len) == 0) {
        if (out_was_obb) *out_was_obb = true;
        return p + obb_len;
    }
    if (out_was_obb) *out_was_obb = false;
    return p;
}

static void normalize_backslashes(char *s) {
    for (char *p = s; *p; ++p) {
        if (*p == '\\') *p = '/';
    }
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

    const char *p = strip_leading_and_obb(tmp, NULL);
    snprintf(out, out_sz, "%s%s", DATA_PATH, p);
}

bool path_translate_asset(const char *in, char *out, size_t out_sz) {
    if (!in || !*in) {
        snprintf(out, out_sz, "%sassets/", DATA_PATH);
        return false;
    }

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", in);
    normalize_backslashes(tmp);

    if (strchr(tmp, ':')) {
        snprintf(out, out_sz, "%s", tmp);
        return true;
    }

    bool was_obb = false;
    const char *p = strip_leading_and_obb(tmp, &was_obb);

    if (was_obb || p != tmp) {
        // Either an explicit /ff7_1.02/ root or had stripped slashes that
        // suggest an absolute Android path. Route to the flat data root.
        snprintf(out, out_sz, "%s%s", DATA_PATH, p);
        return true;
    }

    // Genuine bare APK-asset path; route through the assets/ tree.
    snprintf(out, out_sz, "%sassets/%s", DATA_PATH, p);
    return false;
}
