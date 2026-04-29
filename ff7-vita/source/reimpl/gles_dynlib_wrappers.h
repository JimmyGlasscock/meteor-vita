/*
 * GLES entry points not exported as linkable symbols from libvitaGL.a on the
 * current SDK, but often available through vglGetProcAddress after init.
 * The Android .so imports these by name; dynlib resolves them here.
 */

#ifndef GLES_DYNLIB_WRAPPERS_H
#define GLES_DYNLIB_WRAPPERS_H

#include <vitaGL.h>

void gles_dynlib_wrappers_init(void);

void so_glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void so_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                  GLint yoffset, GLsizei width, GLsizei height,
                                  GLenum format, GLsizei imageSize,
                                  const void *data);
void so_glDetachShader(GLuint program, GLuint shader);
void so_glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params);
void so_glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
void so_glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
void so_glGetPointerv(GLenum pname, void **params);
GLboolean so_glIsBuffer(GLuint buffer);
void so_glSampleCoverage(GLfloat value, GLboolean invert);
void so_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void so_glValidateProgram(GLuint program);

#endif
