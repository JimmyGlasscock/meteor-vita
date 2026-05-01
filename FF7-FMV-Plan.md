# FF7 Vita – FMV Movie Playback Implementation Plan

## Summary

Almost everything needed for FMV playback is **already written**. The work is entirely
about wiring code that exists into the build and replacing stub handlers in `java.c`
with calls to the real implementations.

---

## How the Android App Drives Movies

The game's Java layer calls into the native library through the `MyDecoder` JNI class.
The full per-movie lifecycle is:

| Call | What it does |
|------|-------------|
| `SET_TEXTURE(texId)` | Passes the OpenGL texture ID the decoder should write frames into |
| `START(path)` | Starts playback; path is e.g. `/ff7_1.02/data/movies/eidoslogo.webm`; returns 1 on success |
| `FRAME(n)` | Called once per render frame; decode + upload one video frame; returns 1=playing, 0=done |
| `AFTER_FRAME()` | On Android calls `SurfaceTexture.updateTexImage()` — no Vita equivalent needed |
| `GET_POSITION()` | Returns current playback position in ms; game polls until this is 0 after RESET |
| `GET_TOTALTIME()` | Returns total duration in ms |
| `SET_VOLUME(v)` | Float 0..1 volume control |
| `RESET()` | Stop playback and free resources; thereafter GET_POSITION must return 0 |

On Android, `START` hands the path to the native `fw_start_movie` / `AVI_frame`
pipeline which feeds MJPEG frames into a `SurfaceTexture`-backed OES texture — a
mechanism that does not exist on Vita.

---

## What Is Already Written

| File | What it does | Status |
|------|-------------|--------|
| `source/reimpl/ff7_video_player.c` | SceAvPlayer-backed MP4 player: open, per-frame NV12→RGBA decode, upload to GL, close | Written; **not compiled** |
| `source/reimpl/ff7_video_player.h` | Public API for the above | Written |
| `source/reimpl/ff7_gl_movie_tex.c` | Uploads RGBA pixel data to both `GL_TEXTURE_2D` and `GL_TEXTURE_EXTERNAL_OES` (the game uses EXTERNAL_OES) | Written; **not compiled** |
| `source/reimpl/ff7_gl_movie_tex.h` | Public API for the above | Written |
| `source/ff7_jni_callbacks.c` | Complete JNI handlers for every MyDecoder method — bridges FalsoJNI to `ff7_video_player` | Written; **not compiled** |
| `source/ff7_jni_callbacks.h` | Declarations for the above | Written |
| `source/ff7_jni_log.c` | Forwards FalsoJNI log output to the persistent debug log file | Written; **not compiled** |

---

## What Is Broken / Missing

### 1. `java.c` MyDecoder handlers are stubs

`java.c` contains placeholder implementations for all MyDecoder methods that either
return 0 or do nothing:

- `mh_dec_start` → logs "stub 0 (skip movie)" and returns 0
- `mh_dec_frame` → returns 0 immediately
- `mh_dec_set_texture` → logs but stores nothing
- `mh_dec_get_position` → returns 0
- `mh_dec_get_totaltime` → returns 0
- etc.

The real implementations are sitting in `ff7_jni_callbacks.c` but are never called.

### 2. Missing source files in `CMakeLists.txt`

None of the video-related files appear in the `add_executable(...)` source list.
The following need to be added:

```
source/reimpl/ff7_video_player.c
source/reimpl/ff7_gl_movie_tex.c
source/ff7_jni_callbacks.c
source/ff7_jni_log.c
```

`ff7_avi_player.c` (the older MJPEG/libjpeg path) does **not** need to be added —
`ff7_video_player.c` (SceAvPlayer) supersedes it.

`ff7_jni_driver.c` does **not** need to be added — `main.c` implements its own
equivalent and does not call any `ff7_jni_driver` functions.

### 3. `SceAvPlayer_stub` not linked

`SceAvPlayer` is Vita's hardware-accelerated H.264 decoder. The stub library must be
added to `target_link_libraries` in `CMakeLists.txt`.

### 4. `ff7_video_init()` not called at startup

`ff7_video_init()` calls `sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER)` to load the
decoder sysmodule. It is currently never called. It needs to be called once at startup
in `main.c`, after `soloader_init_all()`.

---

## Step-by-Step Implementation

### Step 1 — `CMakeLists.txt`: add source files

In the `add_executable(${CMAKE_PROJECT_NAME} ...)` block, add:

```cmake
source/reimpl/ff7_video_player.c
source/reimpl/ff7_gl_movie_tex.c
source/ff7_jni_callbacks.c
source/ff7_jni_log.c
```

### Step 2 — `CMakeLists.txt`: link `SceAvPlayer_stub`

Add `SceAvPlayer_stub` to the `target_link_libraries` list (alongside the other `Sce*_stub` entries at the bottom).

### Step 3 — `java.c`: replace stub MyDecoder handlers with calls to real callbacks

Include the callbacks header at the top of `java.c`:

```c
#include "ff7_jni_callbacks.h"
```

Then replace every stub `mh_dec_*` function body to forward to the corresponding
`ff7_cb_md_*` function:

| Stub in `java.c` | Forward to |
|-----------------|-----------|
| `mh_dec_set_texture` (void) | `ff7_cb_md_setTexture(id, args)` |
| `mh_dec_start` (jint) | `return ff7_cb_md_start(id, args)` |
| `mh_dec_frame` (jint) | `return ff7_cb_md_frame(id, args)` |
| `mh_dec_after_frame` (void) | `ff7_cb_md_afterFrame(id, args)` |
| `mh_dec_reset` (void) | `ff7_cb_md_reset(id, args)` |
| `mh_dec_set_position` (void) | `ff7_cb_md_setPosition(id, args)` |
| `mh_dec_set_volume` (void) | `ff7_cb_md_setVolume(id, args)` |
| `mh_dec_get_position` (jint) | `return ff7_cb_md_getPosition(id, args)` |
| `mh_dec_get_totaltime` (jint) | `return ff7_cb_md_getTotalTime(id, args)` |

### Step 4 — `main.c`: call `ff7_video_init()` at startup

Add `#include "reimpl/ff7_video_player.h"` at the top of `main.c`, then call
`ff7_video_init()` immediately after `soloader_init_all()` (before `JNI_OnLoad`).

---

## How the Path Translation Works

`ff7_jni_callbacks.c` `ff7_cb_md_start()` already handles the path correctly:

1. FalsoJNI delivers the Android path as a `jstring`, e.g.
   `/ff7_1.02/data/movies/eidoslogo.webm`
2. `path_translate_data()` converts it to the Vita path:
   `ux0:data/ff7/ff7_1.02/data/movies/eidoslogo.webm`
3. `ff7_video_open()` strips the extension and appends `.mp4`:
   `ux0:data/ff7/ff7_1.02/data/movies/eidoslogo.mp4`
4. SceAvPlayer opens the MP4 file from that path.

The MP4 files are already present at `ux0:data/ff7/ff7_1.02/data/movies/` as
confirmed. No additional path work is required.

---

## Known Risks and Notes

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **SceAvPlayer audio** | SceAvPlayer plays AAC audio internally; game's `SET_VOLUME` float is converted but `AVI_setVolume` (OpenSL ES) is intentionally skipped. Volume control will not work. | Acceptable for "just works" goal. Audio will play at default level. |
| **Native symbol resolution** | `ff7_jni_callbacks.c` resolves `fw_start_movie`, `AVI_frame`, etc. from the `.so` using `so_symbol`. If any are missing they are logged but ignored — the callbacks never call them. | No crash risk. |
| **`extern so_module so_mod`** | `ff7_jni_callbacks.c` references this symbol defined in `main.c`. | Both are in the same final binary; the linker resolves it at link time. No action needed. |
| **MP4 decode performance** | SceAvPlayer runs the hardware H.264 decoder; NV12→RGBA conversion is CPU-based. For typical FF7 movie resolutions (≤640×360) this is well within one frame budget. | Monitor first few cutscenes. If a movie is 854×480 or larger, consider downscaling during conversion. |
| **`ff7_avi_player.c` conflicts** | The AVI player is not compiled, so `libjpeg` is not needed and there are no symbol conflicts. | Do not add `ff7_avi_player.c` to CMakeLists.txt. |

---

## Files Changed (Summary)

| File | Change |
|------|--------|
| `ff7-vita/CMakeLists.txt` | Add 4 source files; add `SceAvPlayer_stub` to link libraries |
| `ff7-vita/source/main.c` | Add `#include`, call `ff7_video_init()` after `soloader_init_all()` |
| `ff7-vita/source/java.c` | Add `#include "ff7_jni_callbacks.h"`; replace 9 stub `mh_dec_*` bodies with one-line forwards |

Everything else (`ff7_video_player.c`, `ff7_gl_movie_tex.c`, `ff7_jni_callbacks.c`,
`ff7_jni_log.c`) is already correct as written — it just needs to be compiled.

---

## Post-Implementation Verification

After building and deploying:

1. **Eidos logo movie** (`eidoslogo.mp4`) plays immediately on boot — this is the first
   movie triggered by the game and the easiest to confirm.
2. Boot log should show:
   - `[video] SceAvPlayer sysmodule loaded`
   - `[movie] SET_TEXTURE tex=<N>`
   - `[movie] START "ux0:data/ff7/ff7_1.02/data/movies/eidoslogo.mp4"`
   - `[video] playback started, total=<ms> ms`
   - `[movie] FRAME: first frame decoded, more=1 tex=<N>`
   - `[movie] FRAME: movie finished`
   - `[movie] RESET: video closed`
3. If the movie file opens but the screen stays black, check the
   `GL_TEXTURE_EXTERNAL_OES` bind path in `ff7_gl_movie_tex.c` — VitaGL must support
   the `GL_OES_EGL_image_external` extension for that target. If it does not, the
   fallback is to bind only `GL_TEXTURE_2D` and patch the game's shader to use
   `sampler2D` instead of `samplerExternalOES`.
