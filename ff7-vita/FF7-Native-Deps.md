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
| `libandroid.so` | **Complete** — AAssetManager done; ANativeWindow stubs added |
| `libOpenSLES.so` | **Complete** — symbol table present; SEPlayer wired to SceAudio |

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

**Status: Partial. AAssetManager complete; ANativeWindow status unknown.**

### Implemented

| API group | Symbols | Implementation |
|-----------|---------|----------------|
| `AAssetManager` | `AAssetManager_fromJava`, `AAssetManager_open`, `AAssetManager_openDir` | `source/reimpl/asset_manager.cpp` + dynlib.c |
| `AAsset` | `AAsset_close/read/seek/getLength/getRemainingLength/openFileDescriptor/openFileDescriptor64` | Same |
| `AAssetDir` | `AAssetDir_close`, `AAssetDir_getNextFileName` | Stub (logs error, returns empty) |

### Unverified / possibly missing

`libandroid.so` also exports the `ANativeWindow_*` surface API. FF7 Android uses `GLSurfaceView` (a Java-side object) rather than `NativeActivity`, so native code likely never calls `ANativeWindow_*` directly. However, this has not been confirmed by inspection of the binary.

| Symbol group | dynlib.c entry | Risk |
|---|---|---|
| `ANativeWindow_acquire/release` | Not present | Low — Java-managed surface |
| `ANativeWindow_lock/unlockAndPost` | Not present | Low |
| `ANativeWindow_setBuffersGeometry` | Not present | Low |
| `ANativeWindow_getWidth/getHeight/getFormat` | Not present | Low |
| `ALooper_*` / `AInputQueue_*` | Not present | Very low — game uses Java input callbacks |

### Plan

1. **Verify** with `objdump -d libjni_ff7.so | grep ANativeWindow` (or Ghidra's import list) whether any `ANativeWindow_*` symbols appear. If none → confirmed not needed.
2. If any are present: add stubs in a new `source/reimpl/android_native_window.c`:

```c
// Minimal stubs — return success; Vita's window is managed by VitaGL.
int ANativeWindow_acquire(void *win)               { return 0; }
void ANativeWindow_release(void *win)              {}
int ANativeWindow_getWidth(void *win)              { return 960; }
int ANativeWindow_getHeight(void *win)             { return 544; }
int ANativeWindow_setBuffersGeometry(void *win, int w, int h, int fmt) { return 0; }
```

---

## 9. `libOpenSLES.so` — Audio

**Status: Partial. Symbol table complete; audio path not tested.**

All `SL_IID_*` interface UUID constants and `slCreateEngine` are mapped in `dynlib.c`. The library is linked via `-lOpenSLES` in `CMakeLists.txt`. If `libjni_ff7.so` uses the OpenSL ES C API directly (engine → output mix → player → buffer queue), those calls will reach VitaSDK's own OpenSL ES implementation through the symbol table entries.

### What is still stubbed

The JNI-side audio helpers in `java.c` are stubs:
- `SEPlayer.PLAY(int id)` — logs and returns
- `SEPlayer.SETVOLUME(float v)` — logs and returns

If the game's sound effect engine is implemented as Java calling into native C++ via JNI (i.e., native code creates OpenSL ES objects on first `PLAY` call), the stubs will silently suppress all sound.

If the game's audio is entirely native (OpenSL ES engine init happens in `JNI_OnLoad` or `onSurfaceCreated`, with `SEPlayer.PLAY` just triggering playback of an already-loaded buffer), audio may work without touching those stubs.

### Plan

**After the game reaches a running frame (Phase 5 complete):**

1. Check the boot log for any OpenSL ES failure codes. `slCreateEngine` returning `SL_RESULT_SUCCESS` is the first gate.
2. Use Ghidra to trace what `Java_..._GLESJniWrapper_onSurfaceCreated` does — does it call `slCreateEngine`? Does it create a buffer-queue player? If yes, audio is all-native and may already work.
3. If audio is silent after the game loads, add `ff7_boot_log` calls to the `SEPlayer` JNI handlers and confirm whether `PLAY` is ever invoked.
4. If `PLAY` is invoked but silent: implement a minimal Vita audio thread in a new `source/reimpl/ff7_se_player.c`:
   - On init: load `audio.fmt` + `audio.dat` from `DATA_PATH/ff7_1.02/data/sound/` into memory.
   - On `PLAY(id)`: look up the PCM entry by ID, submit it to `sceAudio*` or an OpenSL ES buffer queue.
   - On `SETVOLUME(v)`: pass to the volume interface.

---

## 10. Files written but not yet compiled into the build

Several source files exist in `source/reimpl/` that are **not listed in `CMakeLists.txt`** and therefore not compiled. Each needs to be added along with any missing linked libraries.

| File | Purpose | Missing from CMake | Extra libs needed |
|------|---------|--------------------|-------------------|
| `source/reimpl/gles_dynlib_wrappers.c` | `vglGetProcAddress` wrappers for rarely-exported GL funcs | Yes | None |
| `source/reimpl/ff7_gl_movie_tex.c` | Uploads RGBA frame to GL texture (used by both video players) | Yes | None |
| `source/reimpl/ff7_avi_player.c` | RIFF/AVI + MJPEG decoder for FMV playback | Yes | `jpeg` |
| `source/reimpl/ff7_video_player.c` | `SceAvPlayer` MP4 decoder for FMV playback | Yes | `SceAvPlayer_stub`, `SceSysmodule_stub` |
| `source/reimpl/ff7_input_hooks.c` | `fw_GetAsyncKeyState` + DirectInput `GetDeviceState` hooks | Yes | None |

### Plan — add to `CMakeLists.txt`

```cmake
# In add_executable(...):
source/reimpl/gles_dynlib_wrappers.c
source/reimpl/ff7_gl_movie_tex.c
source/reimpl/ff7_avi_player.c
source/reimpl/ff7_video_player.c
source/reimpl/ff7_input_hooks.c

# In target_link_libraries(...):
jpeg
SceAvPlayer_stub
SceSysmodule_stub
```

### Plan — wire the hooks in `patch.c`

`ff7_input_hooks.c` is written but `patch.c` never calls `hook_addr()`. Once the `.so` is loaded and the symbol addresses for `fw_GetAsyncKeyState` and the DirectInput vtable's `GetDeviceState` slot are found via Ghidra:

```c
#include "reimpl/ff7_input_hooks.h"

void so_patch(void) {
    // Replace fw_GetAsyncKeyState with our pad-aware version.
    // Address must be found from libjni_ff7.so symbol table or Ghidra.
    uintptr_t gak_addr = so_symbol(&so_mod, "fw_GetAsyncKeyState");
    if (gak_addr) {
        ff7_gak_hook = hook_addr(gak_addr, (uintptr_t)&ff7_GetAsyncKeyState_hook);
    }

    // Replace IDirectInputDevice::GetDeviceState (vtable slot 9 or by symbol).
    uintptr_t gds_addr = so_symbol(&so_mod, "_ZN...GetDeviceState...");
    if (gds_addr) {
        ff7_gds_hook = hook_addr(gds_addr, (uintptr_t)&ff7_GetDeviceState_hook);
    }
}
```

### Plan — wire the video player from `java.c`

`MyDecoder.START` currently returns 0 to skip all FMVs. Once the video files are confirmed playable:

1. Choose one backend (recommend `ff7_video_player` using `SceAvPlayer` for MP4, or `ff7_avi_player` for the original AVI/MJPEG files if MP4 conversion is not desired).
2. Wire `mh_dec_start` to call `ff7_video_open(path)` (path translation applies).
3. Wire `mh_dec_frame` to call `ff7_avi_next_frame(tex)` or `ff7_video_next_frame(tex)`.
4. Wire `mh_dec_reset / mh_dec_set_position / mh_dec_get_position / mh_dec_get_totaltime` to the corresponding player API.
5. Call `ff7_video_init()` (loads `SceAvPlayer` sysmodule) from `soloader_init_all()` if using the SceAvPlayer backend.

---

## Summary of outstanding actions (priority order)

All native dependency gaps have been resolved. Remaining work is runtime validation:

| Priority | Action | File(s) |
|----------|--------|---------|
| 1 | Deploy to device; check boot log for unresolved imports | (runtime) |
| 2 | Verify `audio.fmt` count + `entry[0]` log matches actual file layout | (runtime log) |
| 3 | If `audio.fmt` format differs: adjust `ff7_se_player.c` struct parser | `source/reimpl/ff7_se_player.c` |
| 4 | Confirm no `ANativeWindow_*` calls appear in boot log | (runtime log) |
| 5 | Wire `patch.c` input hooks once symbol addresses known from Ghidra | `source/patch.c` |
| 6 | Wire video player into `java.c` `MyDecoder` handlers (Phase 6) | `source/java.c` |

---

*Last updated: 2026-05-03*
