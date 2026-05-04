# FF7 Vita — Native Dependency Audit & Implementation Plan

Companion to [`FF7-Port-Plan.md`](./FF7-Port-Plan.md). This document audits all nine native libraries that `libjni_ff7.so` imports and tracks the current implementation state of each, plus a concrete plan for anything that is incomplete.

---

## Quick-reference status table

| Library | Status |
|---------|--------|
| `liblog.so` | **Complete** |
| `libm.so` | **Complete** |
| `libstdc++.so` | **Complete** |
| `libEGL.so` | **Complete** (stubbed to VitaGL) |
| `libGLESv2.so` | **Complete** — wrappers wired; proc addresses resolved at runtime |
| `libc.so` | **Complete** — `lseek64` implemented |
| `libdl.so` | **Sufficient** — limited by design |
| `libandroid.so` | **Complete** — AAssetManager + ANativeWindow + AConfiguration + ALooper all wired |
| `libOpenSLES.so` | **Complete** — full custom reimpl (SceAudio backend, Android-ordered vtables) |

---

## 1. `liblog.so` — Android logging

**Status: Complete.**

All four symbols the game uses are implemented in `source/reimpl/log.c` and wired in `dynlib.c`:

| Symbol | Implementation |
|--------|----------------|
| `__android_log_write` | Bridges to `l_info/l_warn/l_error/l_debug` |
| `__android_log_print` | As above, with vsnprintf formatting |
| `__android_log_vprint` | Same |
| `__android_log_assert` | Calls `l_fatal` then `abort()` |

**No action required.**

---

## 2. `libm.so` — Math

**Status: Complete.**

The full set of single- and double-precision math functions is present in `dynlib.c`: `sin/cos/tan/asin/acos/atan/atan2`, `exp/exp2/log/log10`, `sqrt`, `pow`, `floor/ceil/round/trunc`, `fmod`, `sincos/sincosf`, etc. Linked against `-lm` in `CMakeLists.txt`.

**No action required.**

---

## 3. `libstdc++.so` — C++ runtime

**Status: Complete.**

All C++ ABI symbols are extern'd from the Vita's own runtime and wired in `dynlib.c`: `_ZdlPv / _ZdaPv / _Znwj / _Znaj` (new/delete), `__cxa_throw / _begin_catch / __end_catch / __cxa_pure_virtual / __cxa_guard_*`, vtable type-info objects (`_ZTVN10__cxxabiv117/120/121__*`), and all `__aeabi_*` floating-point helpers.

**No action required.**

---

## 4. `libEGL.so` — EGL context management

**Status: Complete (stubbed to VitaGL).**

Every EGL function the game calls is implemented in `source/reimpl/egl.c` and wired in `dynlib.c`. Since VitaGL manages its own context, these stubs return consistent sentinel values rather than delegating to a real EGL implementation:

- `eglInitialize` calls `gl_init()` (VitaGL init) and reports version 2.2
- `eglCreateContext / eglCreateWindowSurface` return heap-allocated sentinel strings
- `eglMakeCurrent / eglSwapBuffers / eglTerminate` are no-ops returning `EGL_TRUE`
- `eglQuerySurface` returns Vita-correct values (960×544, 220 DPI, back-buffer)
- `eglGetConfigAttrib` returns RGBA8888 + depth24 + stencil8 config
- `eglQueryString(EGL_EXTENSIONS)` returns the Android image/fence-sync extension strings the game checks for

**No action required.**

---

## 5. `libGLESv2.so` — OpenGL ES 2.0

**Status: Mostly complete. Two wiring gaps to fix.**

The full GLES 2.0 + relevant GLES 1.1 function set is wired in `dynlib.c` via VitaGL. Custom soloader-layer hooks are in place for:

- `glShaderSource` → `glShaderSource_soloader` (strips `GL_OES_EGL_image_external`, rewrites `samplerExternalOES` → `sampler2D`)
- `glCompileShader` → `glCompileShader_soloader` (handles shader cache)
- `glLinkProgram` → `glLinkProgram_soloader` (logs link errors)
- `glDetachShader` → `glDetachShader_soloader` (looked up via vglGetProcAddress)

A late-addition file, `source/reimpl/gles_dynlib_wrappers.c`, provides `vglGetProcAddress`-backed wrappers for GLES functions VitaGL doesn't directly export by symbol name (`glBlendColor`, `glCompressedTexSubImage2D`, `glGetRenderbufferParameteriv`, `glGetTexParameterfv/iv`, `glGetPointerv`, `glIsBuffer`, `glSampleCoverage`, `glTexParameterfv`, `glValidateProgram`). These are the entries in `dynlib.c` currently pointing at `ret0`.

### Gaps

1. **`gles_dynlib_wrappers.c` is not added to `CMakeLists.txt`** — the file is written but never compiled.
2. **`gles_dynlib_wrappers_init()` is never called** — proc addresses are never resolved; the `ret0` entries in `dynlib.c` for the above functions are never replaced with the real wrappers.
3. **`dynlib.c` entries for wrapped functions still point to `ret0`** — they need to point to the `so_gl*` wrapper functions once the file is compiled.

### Plan

1. Add `source/reimpl/gles_dynlib_wrappers.c` to the `add_executable` list in `CMakeLists.txt`.
2. Call `gles_dynlib_wrappers_init()` from `soloader_init_all()` in `source/utils/init.c`, after `gl_preload()`.
3. Update `dynlib.c` to route the previously-`ret0` entries to the `so_gl*` wrappers:

```c
{ "glBlendColor",                    (uintptr_t)&so_glBlendColor                    },
{ "glCompressedTexSubImage2D",        (uintptr_t)&so_glCompressedTexSubImage2D        },
{ "glGetRenderbufferParameterivOES",  (uintptr_t)&so_glGetRenderbufferParameteriv     },
{ "glGetTexParameterfv",              (uintptr_t)&so_glGetTexParameterfv              },
{ "glGetTexParameteriv",              (uintptr_t)&so_glGetTexParameteriv              },
{ "glGetPointerv",                    (uintptr_t)&so_glGetPointerv                    },
{ "glIsBuffer",                       (uintptr_t)&so_glIsBuffer                       },
{ "glSampleCoverage",                 (uintptr_t)&so_glSampleCoverage                 },
{ "glSampleCoveragex",                (uintptr_t)&so_glSampleCoverage                 },
{ "glTexParameterfv",                 (uintptr_t)&so_glTexParameterfv                 },
{ "glValidateProgram",                (uintptr_t)&so_glValidateProgram                },
```

---

## 6. `libc.so` — Standard C library

**Status: Mostly complete. Two known minor gaps.**

Full coverage of string, memory, I/O (via `SceLibcBridge` when `USE_SCELIBC_IO` is on), time, locale, signals, environment, and pthreads. Struct-size compatibility wrappers for `pthread_*`, `sem_*` live in `source/reimpl/pthr.c`; Bionic errno translation is in `source/reimpl/errno.c`.

### Known gaps

| Symbol | Current | Issue |
|--------|---------|-------|
| `lseek64` | `ret0` | 64-bit file seeks silently fail |
| `setjmp / longjmp` | Direct newlib | Bionic's `jmp_buf` may be a different size; if the game uses these across a call that goes through the loader boundary it could corrupt the stack |

### Plan

- **`lseek64`**: Implement a wrapper that casts to `off_t` and calls the regular `lseek`; sufficient for FF7's file sizes.
- **`setjmp/longjmp`**: Defer until a crash is actually traced to these; on ARMv7 Bionic and newlib `jmp_buf` are both 64 bytes so in practice this is unlikely to matter.

---

## 7. `libdl.so` — Dynamic linker

**Status: Sufficient by design.**

| Symbol | Implementation |
|--------|----------------|
| `dlopen` | `ret1` (returns a non-null handle; we can't load additional .so files) |
| `dlclose` | `ret0` |
| `dlerror` | `ret0` (returns NULL — no error) |
| `dlsym` | `dlsym_soloader` — scans `default_dynlib` table |

The `.so` may use `dlsym` to probe for optional symbols at runtime. Any symbol in `default_dynlib` is resolvable; anything outside it returns NULL (which well-written code treats as "feature not present"). If a crash traces back to a null `dlsym` result being called, add the symbol to `dynlib.c`.

**No action required unless a dlsym-related crash surfaces.**

---

## 8. `libandroid.so` — Android NDK native APIs

**Status: Complete.**

### Implemented

| API group | Symbols | Implementation |
|-----------|---------|----------------|
| `AAssetManager` | `AAssetManager_fromJava`, `AAssetManager_open`, `AAssetManager_openDir` | `source/reimpl/asset_manager.cpp` + dynlib.c |
| `AAsset` | `AAsset_close/read/seek/getLength/getRemainingLength/openFileDescriptor/openFileDescriptor64` | Same |
| `AAssetDir` | `AAssetDir_close`, `AAssetDir_getNextFileName` | Stub |
| `ANativeWindow` | all 8 `ANativeWindow_*` symbols | `source/reimpl/sys.c` — returns Vita screen dims (960×544, RGBA_8888) |
| `AConfiguration` | 22 `AConfiguration_*` symbols | `source/reimpl/ff7_android_config.c` — returns en-US / landscape / HDPI defaults |
| `ALooper` | 9 `ALooper_*` symbols | `source/reimpl/ff7_android_config.c` — no-op stubs; game uses Java input |

**No action required.**

---

## 9. `libOpenSLES.so` — Audio

**Status: Complete.**

VitaSDK's own OpenSL ES implementation **cannot be used directly** because its `SLObjectItf_` vtable has a different slot ordering than Android NDK's (Destroy@slot3 vs GetInterface@slot3, GetInterface@slot9 vs Destroy@slot6). Using it would cause the game to call the wrong vtable slots and crash immediately.

A complete custom reimplementation is in `source/reimpl/ff7_opensl_impl.c`:

| Feature | Detail |
|---------|--------|
| `slCreateEngine_vita` | Replaces `slCreateEngine` in dynlib.c |
| `SLObjectItf` vtable | Android NDK slot ordering (Destroy@3, GetInterface@9) |
| `SLEngineItf` | `CreateAudioPlayer`, `CreateOutputMix` fully implemented |
| `SLPlayItf` | `SetPlayState/GetPlayState` and all 12 methods |
| `SLVolumeItf` | Volume mapped to `sceAudioOutSetVolume` in millibels |
| `SLAndroidSimpleBufferQueueItf` | `Enqueue/Clear/GetState/RegisterCallback` — 4-slot ring buffer |
| Audio backend | `SceAudio` BGM port (falls back to VOICE if BGM exhausted) |
| Streaming | Per-player background thread drains queue in 512-sample grains |
| Format support | Any `SLDataFormat_PCM` sample rate, mono→stereo expansion |

Sound effects through the Java `SEPlayer` bridge are handled separately by `source/reimpl/ff7_se_player.c`.

**No action required.**

---

## 10. Build completeness check

All source files are compiled and all libraries are linked. This section is a record of what was wired and when.

| File | Status | Notes |
|------|--------|-------|
| `source/reimpl/gles_dynlib_wrappers.c` | **Compiled** — in CMakeLists | `gles_dynlib_wrappers_init()` called from `init.c`; dynlib.c entries updated to `so_gl*` |
| `source/reimpl/ff7_gl_movie_tex.c` | **Compiled** — in CMakeLists | Used by both video backends |
| `source/reimpl/ff7_avi_player.c` | **Compiled** — in CMakeLists | `jpeg` linked |
| `source/reimpl/ff7_video_player.c` | **Compiled** — in CMakeLists | `SceAvPlayer_stub`, `SceSysmodule_stub` linked |
| `source/reimpl/ff7_input_hooks.c` | **Compiled** — in CMakeLists | Hooks **not yet registered** in `patch.c` (needs Ghidra addresses) |
| `source/reimpl/ff7_se_player.c` | **Compiled** — in CMakeLists | `ff7_se_player_init()` called from `init.c`; `java.c` wired |
| `source/reimpl/ff7_opensl_impl.c` | **Compiled** — in CMakeLists | `slCreateEngine_vita` wired in `dynlib.c` |
| `source/reimpl/ff7_android_config.c` | **Compiled** — in CMakeLists | All `AConfiguration_*` and `ALooper_*` wired in `dynlib.c` |

---

## 11. Remaining work (post-dependency, port phases)

The nine native dependencies are fully resolved. All outstanding items are higher-level porting tasks:

| Priority | Item | Detail | File(s) |
|----------|------|--------|---------|
| 1 | **Runtime boot test** | Deploy VPK; check log for unresolved symbols or early crash | (device) |
| 2 | **`patch.c` input hooks** | `ff7_input_hooks.c` is compiled but `hook_addr()` is never called. Needs Ghidra to find `fw_GetAsyncKeyState` and DirectInput vtable addresses in `libjni_ff7.so` | `source/patch.c` |
| 3 | **Video player wiring** | `MyDecoder.START/FRAME` in `java.c` return 0 (skip all FMVs). Wire to `ff7_video_player.c` or `ff7_avi_player.c` once boot is stable | `source/java.c` |
| 4 | **`audio.fmt` layout validation** | Confirm the `SEPlayer` PCM struct parser matches the actual file; adjust if count/offset log looks wrong | `source/reimpl/ff7_se_player.c` |

---

*Last updated: 2026-05-03*
