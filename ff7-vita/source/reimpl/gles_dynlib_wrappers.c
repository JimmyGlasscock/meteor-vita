#include "reimpl/gles_dynlib_wrappers.h"

#include "utils/ff7_boot_log.h"

#include <stddef.h>

typedef void (*PFN_glBlendColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_glCompressedTexSubImage2D)(GLenum, GLint, GLint, GLint,
                                              GLsizei, GLsizei, GLenum, GLsizei,
                                              const void *);
typedef void (*PFN_glDetachShader)(GLuint, GLuint);
typedef void (*PFN_glGetRenderbufferParameteriv)(GLenum, GLenum, GLint *);
typedef void (*PFN_glGetTexParameterfv)(GLenum, GLenum, GLfloat *);
typedef void (*PFN_glGetTexParameteriv)(GLenum, GLenum, GLint *);
typedef void (*PFN_glGetPointerv)(GLenum, void **);
typedef GLboolean (*PFN_glIsBuffer)(GLuint);
typedef void (*PFN_glSampleCoverage)(GLfloat, GLboolean);
typedef void (*PFN_glTexParameterfv)(GLenum, GLenum, const GLfloat *);
typedef void (*PFN_glValidateProgram)(GLuint);

static PFN_glBlendColor                 fn_glBlendColor;
static PFN_glCompressedTexSubImage2D    fn_glCompressedTexSubImage2D;
static PFN_glDetachShader               fn_glDetachShader;
static PFN_glGetRenderbufferParameteriv fn_glGetRenderbufferParameteriv;
static PFN_glGetTexParameterfv          fn_glGetTexParameterfv;
static PFN_glGetTexParameteriv          fn_glGetTexParameteriv;
static PFN_glGetPointerv                fn_glGetPointerv;
static PFN_glIsBuffer                   fn_glIsBuffer;
static PFN_glSampleCoverage             fn_glSampleCoverage;
static PFN_glTexParameterfv             fn_glTexParameterfv;
static PFN_glValidateProgram            fn_glValidateProgram;

static int s_logged;

#define RESOLVE(glname, field)                                                 \
    do {                                                                       \
        if (!(field))                                                          \
            (field) = (__typeof__(field))vglGetProcAddress(#glname);           \
    } while (0)

void gles_dynlib_wrappers_init(void) {
    RESOLVE(glBlendColor, fn_glBlendColor);
    RESOLVE(glCompressedTexSubImage2D, fn_glCompressedTexSubImage2D);
    RESOLVE(glDetachShader, fn_glDetachShader);
    RESOLVE(glGetRenderbufferParameteriv, fn_glGetRenderbufferParameteriv);
    RESOLVE(glGetTexParameterfv, fn_glGetTexParameterfv);
    RESOLVE(glGetTexParameteriv, fn_glGetTexParameteriv);
    RESOLVE(glGetPointerv, fn_glGetPointerv);
    RESOLVE(glIsBuffer, fn_glIsBuffer);
    RESOLVE(glSampleCoverage, fn_glSampleCoverage);
    RESOLVE(glTexParameterfv, fn_glTexParameterfv);
    RESOLVE(glValidateProgram, fn_glValidateProgram);

    if (!s_logged) {
        s_logged = 1;
        ff7_boot_log(
            "[gles] vglGetProcAddress: BlendColor=%p CompressedTexSubImage2D=%p "
            "DetachShader=%p GetRBParam=%p GetTexParamfv=%p GetTexParamiv=%p "
            "GetPointerv=%p IsBuffer=%p SampleCoverage=%p TexParameterfv=%p "
            "ValidateProgram=%p",
            (void *)fn_glBlendColor, (void *)fn_glCompressedTexSubImage2D,
            (void *)fn_glDetachShader, (void *)fn_glGetRenderbufferParameteriv,
            (void *)fn_glGetTexParameterfv, (void *)fn_glGetTexParameteriv,
            (void *)fn_glGetPointerv, (void *)fn_glIsBuffer,
            (void *)fn_glSampleCoverage, (void *)fn_glTexParameterfv,
            (void *)fn_glValidateProgram);
    }
}

void so_glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if (fn_glBlendColor)
        fn_glBlendColor(red, green, blue, alpha);
}

void so_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                   GLint yoffset, GLsizei width, GLsizei height,
                                   GLenum format, GLsizei imageSize,
                                   const void *data) {
    if (fn_glCompressedTexSubImage2D)
        fn_glCompressedTexSubImage2D(target, level, xoffset, yoffset, width,
                                     height, format, imageSize, data);
}

void so_glDetachShader(GLuint program, GLuint shader) {
    if (fn_glDetachShader)
        fn_glDetachShader(program, shader);
}

void so_glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (fn_glGetRenderbufferParameteriv)
        fn_glGetRenderbufferParameteriv(target, pname, params);
}

void so_glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
    if (fn_glGetTexParameterfv)
        fn_glGetTexParameterfv(target, pname, params);
}

void so_glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (fn_glGetTexParameteriv)
        fn_glGetTexParameteriv(target, pname, params);
}

void so_glGetPointerv(GLenum pname, void **params) {
    if (fn_glGetPointerv)
        fn_glGetPointerv(pname, params);
}

GLboolean so_glIsBuffer(GLuint buffer) {
    if (fn_glIsBuffer)
        return fn_glIsBuffer(buffer);
    return GL_FALSE;
}

void so_glSampleCoverage(GLfloat value, GLboolean invert) {
    if (fn_glSampleCoverage)
        fn_glSampleCoverage(value, invert);
}

void so_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (fn_glTexParameterfv)
        fn_glTexParameterfv(target, pname, params);
}

void so_glValidateProgram(GLuint program) {
    if (fn_glValidateProgram)
        fn_glValidateProgram(program);
}
