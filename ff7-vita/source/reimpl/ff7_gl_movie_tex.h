/*
 * FMV / MyDecoder: upload decoded or stub RGBA into the GL targets Android uses
 * for MediaCodec (GL_TEXTURE_EXTERNAL_OES) as well as GL_TEXTURE_2D.
 */

#ifndef FF7_GL_MOVIE_TEX_H
#define FF7_GL_MOVIE_TEX_H

#include <stdint.h>
#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void ff7_gl_upload_rgba_movie_tex(GLuint tex, int w, int h, const uint8_t *rgba,
                                 int initial_full);

#ifdef __cplusplus
}
#endif

#endif
