# FF7 Vita — Known Issues

Open bugs and rough edges found during bring-up. Each entry has a priority, a description, the affected file(s), and enough detail to fix it later.

---

## Issue 1 — `OPEN_FILE_DESCRIPTOR` path translation bug

**Priority:** High — blocks save-file reads/writes and the New Game / Continue flow.

**Symptom (from boot log):**
```
[JNI] OPEN_FILE_DESCRIPTOR("assets/ux0:data/ff7/Documents/ff7opt.cfg") -> -1
[JNI] OPEN_FILE_DESCRIPTOR("assets/ux0:data/ff7/Documents/save01.ff7") -> -1
```
The game scans all 9 save slots and the config file via `ExpansionFile.OPEN_FILE_DESCRIPTOR`. Every call returns `-1` because `path_translate_asset` is called on a path that is already a Vita-absolute path (`ux0:data/ff7/Documents/…`), causing `assets/` to be incorrectly prepended.

**Root cause:** `source/java.c` `mh_OPEN_FILE_DESCRIPTOR` calls `path_translate_asset` unconditionally. Since the path does not start with `/`, `path_translate_asset` treats it as asset-relative and prepends the assets prefix.

**Fix:** Detect that the path is already data-directory-relative (starts with `ux0:` or contains `Documents/`) and call `path_translate_data` instead, or open the already-translated path directly.

**Affected file:** `source/java.c` — `mh_OPEN_FILE_DESCRIPTOR`

---

## Issue 2 — SE voice pool limited to 1/4 ports

**Priority:** Medium — sound effects are mono (no polyphony); not a crash blocker.

**Symptom (from boot log):**
```
[se] voice 1: sceAudioOutOpenPort failed 0x80260005
[se] voice 2: sceAudioOutOpenPort failed 0x80260005
[se] voice 3: sceAudioOutOpenPort failed 0x80260005
[se] init complete: 2858 sounds, 1/4 voices
```
`SCE_AUDIO_ERROR_PORT_FULL` (0x80260005) — the Vita's audio port pool is exhausted after the OpenSL BGM player claims its port(s). Only 1 SE voice opens successfully.

**Root cause:** `source/reimpl/ff7_se_player.c` attempts to open 4 `SCE_AUDIO_OUT_PORT_TYPE_VOICE` ports. On hardware there are limited VOICE ports available and the OpenSL BGM implementation already occupies most of them.

**Fix options (pick one):**
1. Reduce `FF7_SE_VOICES` from 4 to 2 and use `SCE_AUDIO_OUT_PORT_TYPE_BGM` for the SE voices (BGM ports are shared differently).
2. Open the first SE voice as VOICE (succeeds) and skip the rest gracefully — already happens, but 1-voice polyphony is poor.
3. Investigate whether the OpenSL implementation can give up a port or share.

**Affected file:** `source/reimpl/ff7_se_player.c`

---

## Issue 3 — `vglGetProcAddress` wrappers resolve to null

**Priority:** Low — not currently crashing, but will when those GL paths are hit.

**Symptom (from boot log):**
```
[gles] vglGetProcAddress: BlendColor=0x0 CompressedTexSubImage2D=0x0
  DetachShader=0x0 GetRBParam=0x0 GetTexParamfv=0x0 GetTexParamiv=0x0
  IsBuffer=0x0 SampleCoverage=0x0 TexParameterfv=0x0 ValidateProgram=0x0
```
Only `GetPointerv` resolves; all other `gles_dynlib_wrappers` entries come back as 0x0 from `vglGetProcAddress`.

**Root cause:** VitaGL's `vglGetProcAddress` does not export these extension/core entrypoints by their standard `gl*` string names. The wrappers in `source/reimpl/gles_dynlib_wrappers.c` are calling `vglGetProcAddress("glBlendColor")` etc., but VitaGL may use different names or export them only as direct symbols.

**Fix:** For each null wrapper, try calling the VitaGL function directly by symbol (e.g. `glBlendColor` may exist as a direct linker symbol in VitaGL even if `vglGetProcAddress` doesn't find it). Alternatively, stub them with safe no-ops in `dynlib.c` if VitaGL doesn't implement them.

**Affected file:** `source/reimpl/gles_dynlib_wrappers.c`, `source/dynlib.c`

---

*Last updated: 2026-05-04*
