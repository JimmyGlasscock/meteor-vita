/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/glutil.h"

#include "utils/utils.h"
#include "utils/dialog.h"
#include "utils/ff7_boot_log.h"
#include "utils/logger.h"

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>

// Helpers for our handling of shaders
GLboolean skip_next_compile = GL_FALSE;
char next_shader_fname[256];
void load_shader(GLuint shader, const char * string, size_t length);

typedef void (*pfn_glDetachShader)(GLuint program, GLuint shader);
static pfn_glDetachShader s_glDetachShader;

// Rewrites a malloc'd shader source buffer in place so that it is acceptable
// to libshacccg / VitaGL. The only Android-specific feature FF7's shaders use
// is the GL_OES_EGL_image_external extension (samplerExternalOES), employed
// solely for FMV playback. We strip the directive and downgrade the sampler
// type to sampler2D, preserving the original buffer length by padding with
// spaces so that the GLsizei lengths the .so passes around remain valid.
static void rewrite_shader_for_vitagl(char *src, size_t len) {
    if (!src || len == 0) return;

    static const char ext_prefix[] = "#extension GL_OES_EGL_image_external";
    char *p = strstr(src, ext_prefix);
    if (p) {
        char *eol = (char *)memchr(p, '\n', (size_t)((src + len) - p));
        if (!eol) eol = src + len;
        for (char *q = p; q < eol; ++q) *q = ' ';
        ff7_boot_log("[gl] rewrite: stripped %s line", ext_prefix);
    }

    static const char target[] = "samplerExternalOES"; // 18 chars
    static const char repl[]   = "sampler2D";          // 9 chars
    const size_t tlen = sizeof(target) - 1;
    const size_t rlen = sizeof(repl)   - 1;
    int hits = 0;
    while ((p = strstr(src, target)) != NULL) {
        memcpy(p, repl, rlen);
        memset(p + rlen, ' ', tlen - rlen);
        ++hits;
    }
    if (hits) {
        ff7_boot_log("[gl] rewrite: %d samplerExternalOES -> sampler2D", hits);
    }
}

bool libshacccg_installed(void) {
    return file_exists("ur0:/data/libshacccg.suprx")
        || file_exists("ur0:/data/external/libshacccg.suprx");
}

void gl_preload() {
    if (!libshacccg_installed()) {
        fatal_error("Error: libshacccg.suprx is not installed. "
                    "Google \"ShaRKBR33D\" for quick installation.");
    }

#ifdef USE_GLSL_SHADERS
    /* POSTPONED defers work to glLinkProgram; some titles crash after the first
     * glCompileShader when paired with our hooks. GLOBAL has no ordering premise. */
    vglSetSemanticBindingMode(VGL_MODE_GLOBAL);
#endif
}

void gl_init() {
    vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
    s_glDetachShader = (pfn_glDetachShader)vglGetProcAddress("glDetachShader");
}

void gl_swap() {
    vglSwapBuffers(GL_FALSE);
}

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length) {
    ff7_boot_log("[gl] glShaderSource(shader=%u, count=%i)", (unsigned)shader, count);
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glShaderSource<%p>(shader: %i, count: %i, string: %p, length: %p)\n", __builtin_return_address(0), shader, count, string, _length);
#endif
    if (!string) {
        ff7_boot_log("[gl] glShaderSource: string array is NULL");
        l_error("<%p> Shader source string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    } else if (!*string) {
        ff7_boot_log("[gl] glShaderSource: *string is NULL");
        l_error("<%p> Shader source *string is NULL, count: %i",
                   __builtin_return_address(0), count);
        skip_next_compile = GL_TRUE;
        return;
    }

    size_t total_length = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            total_length += strlen(string[i]);
        } else {
            total_length += _length[i];
        }
    }

    char * str = malloc(total_length+1);
    size_t l = 0;

    for (int i = 0; i < count; ++i) {
        if (!_length) {
            memcpy(str + l, string[i], strlen(string[i]));
            l += strlen(string[i]);
        } else {
            memcpy(str + l, string[i], _length[i]);
            l += _length[i];
        }
    }
    str[total_length] = '\0';

    rewrite_shader_for_vitagl(str, total_length);

    ff7_boot_log("[gl] glShaderSource: total_length=%u, calling load_shader",
                 (unsigned)total_length);
    load_shader(shader, str, total_length);
    ff7_boot_log("[gl] glShaderSource: load_shader done (shader=%u)",
                 (unsigned)shader);

    free(str);
}

void glDetachShader_soloader(GLuint program, GLuint shader) {
    if (s_glDetachShader)
        s_glDetachShader(program, shader);
}

void glLinkProgram_soloader(GLuint program) {
    ff7_boot_log("[gl] glLinkProgram(program=%u): begin", (unsigned)program);
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    ff7_boot_log("[gl] glLinkProgram(program=%u): status=%i",
                 (unsigned)program, (int)status);

    if (!status) {
        char info[1024];
        GLsizei n = 0;
        info[0] = '\0';
        glGetProgramInfoLog(program, sizeof(info) - 1, &n, info);
        info[sizeof(info) - 1] = '\0';
        ff7_boot_log("[gl] glLinkProgram info: %s", info);
    }
}

void glCompileShader_soloader(GLuint shader) {
    ff7_boot_log("[gl] glCompileShader(shader=%u, skip=%i)",
                 (unsigned)shader, (int)skip_next_compile);
#ifdef DEBUG_OPENGL
    sceClibPrintf("[gl_dbg] glCompileShader<%p>(shader: %i)\n", __builtin_return_address(0), shader);
#endif

#ifndef USE_GXP_SHADERS
    if (!skip_next_compile) {
        glCompileShader(shader);
        ff7_boot_log("[gl] glCompileShader: shacccg done (shader=%u)",
                     (unsigned)shader);
#ifdef DUMP_COMPILED_SHADERS
        void *bin = vglMalloc(32 * 1024);
        GLsizei len = 0;
        vglGetShaderBinary(shader, 32 * 1024, &len, bin);
        ff7_boot_log("[gl] glCompileShader: vglGetShaderBinary len=%i path=%s",
                     (int)len, next_shader_fname);
        if (len > 0) {
            file_save(next_shader_fname, bin, len);
        }
        vglFree(bin);
#endif
    }
    skip_next_compile = GL_FALSE;
#endif
}

#if defined(USE_GLSL_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);
    ff7_boot_log("[gl] load_shader: shader=%u sha=%s len=%u",
                 (unsigned)shader, sha_name, (unsigned)length);

    if (file_exists(gxp_path)) {
        ff7_boot_log("[gl] load_shader: cached gxp found, glShaderBinary");
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else {
        ff7_boot_log("[gl] load_shader: no cache, glShaderSource then compile");
        glShaderSource(shader, 1, &string, &length);
        strcpy(next_shader_fname, gxp_path);
    }

    free(sha_name);
}
#elif defined(USE_GLSL_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    glShaderSource(shader, 1, &string, &length);
}
#elif defined(USE_CG_SHADERS) && defined(DUMP_COMPILED_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char gxp_path[256];
    char cg_path[256];
    snprintf(gxp_path, sizeof(gxp_path), DATA_PATH"gxp/%s.gxp", sha_name);
    snprintf(cg_path, sizeof(cg_path), DATA_PATH"cg/%s.cg", sha_name);

    if (file_exists(gxp_path)) {
        uint8_t *buffer;
        size_t size;

        file_load(gxp_path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
        skip_next_compile = GL_TRUE;
    } else if (file_exists(cg_path)) {
        char *buffer;
        size_t size;

        file_load(cg_path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);
        strcpy(next_shader_fname, gxp_path);

        free(buffer);
        skip_next_compile = GL_FALSE;
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }

        skip_next_compile = GL_FALSE;
    }

    free(sha_name);
}
#elif defined(USE_CG_SHADERS) || defined(USE_GXP_SHADERS)
void load_shader(GLuint shader, const char * string, size_t length) {
    char* sha_name = str_sha1sum(string, length);

    char path[256];
#ifdef USE_CG_SHADERS
    snprintf(path, sizeof(path), DATA_PATH"cg/%s.cg", sha_name);
#else
    snprintf(path, sizeof(path), DATA_PATH"gxp/%s.gxp", sha_name);
#endif

    if (file_exists(path)) {
#ifdef USE_CG_SHADERS
        char *buffer;
        size_t size;

        file_load(path, (uint8_t **) &buffer, &size);

        glShaderSource(shader, 1, &string, &size);

        free(buffer);
#else
        uint8_t *buffer;
        size_t size;

        file_load(path, &buffer, &size);

        glShaderBinary(1, &shader, 0, buffer, (int32_t) size);

        free(buffer);
#endif
    } else {
        l_warn("Encountered an untranslated shader %s, saving GLSL "
               "and using a dummy shader.", sha_name);

        char glsl_path[256];
        snprintf(glsl_path, sizeof(glsl_path), DATA_PATH"glsl/%s.glsl", sha_name);
        file_mkpath(glsl_path, 0777);
        file_save(glsl_path, (const uint8_t *) string, length);

        if (strstr(string, "gl_FragColor")) {
            const char *dummy_shader = "float4 main() { return float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        } else {
            const char *dummy_shader = "void main(float4 out gl_Position : POSITION ) { gl_Position = float4(1.0,1.0,1.0,1.0); }";
            int32_t dummy_shader_len = (int32_t) strlen(dummy_shader);
            glShaderSource(shader, 1, &dummy_shader, &dummy_shader_len);
        }
    }

    free(sha_name);
}
#else
#error "Define one of (USE_GLSL_SHADERS, USE_CG_SHADERS, USE_GXP_SHADERS)"
#endif
