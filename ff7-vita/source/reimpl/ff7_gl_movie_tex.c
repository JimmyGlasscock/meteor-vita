#include "reimpl/ff7_gl_movie_tex.h"

#include "utils/ff7_boot_log.h"

#include <vitaGL.h>

/* Do not include GLES2/gl2ext.h here: it conflicts with VitaGL's GL headers. */
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

void ff7_gl_upload_rgba_movie_tex(GLuint tex, int w, int h, const uint8_t *rgba,
                                  int initial_full) {
    if (tex == 0 || w <= 0 || h <= 0 || !rgba)
        return;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (initial_full) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, rgba);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                        rgba);
    }

    /*
     * Native code still follows the Android path: glBindTexture(
     * GL_TEXTURE_EXTERNAL_OES, id). We only filled GL_TEXTURE_2D before, so
     * the logo quad stayed black until the first swap after nothing drew.
     */
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
    if (initial_full) {
        glTexImage2D(GL_TEXTURE_EXTERNAL_OES, 0, GL_RGBA, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, rgba);
    } else {
        glTexSubImage2D(GL_TEXTURE_EXTERNAL_OES, 0, 0, 0, w, h, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba);
    }

    static int s_once;
    if (!s_once) {
        s_once = 1;
        ff7_boot_log("[fmv_glx] movie tex=%u %dx%d -> 2D+EXTERNAL_OES (%s)",
                      (unsigned)tex, w, h,
                      initial_full ? "full" : "sub");
    }
}
