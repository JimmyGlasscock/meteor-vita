# FF7 Android → PS Vita Port Plan

Companion to [`FF7-Port-Context.md`](./FF7-Port-Context.md). This document is the agreed phased plan after the project builds and `libjni_ff7.so` plus OBB/data are in place under `DATA_PATH`.

---

## Current codebase snapshot

The `ff7-vita/` tree is based on [soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate). Practically everything below is still boilerplate unless noted:

| Area | Location | Status |
|------|----------|--------|
| JNI bindings | `ff7-vita/source/java.c` | Empty method/field tables — must be filled from the game |
| Main loop | `ff7-vita/source/main.c` | Stub: `JNI_OnLoad` then empty `while (1) { gl_swap(); }` |
| Patches / hooks | `ff7-vita/source/patch.c` | Empty — add game-specific redirects |
| Imports | `ff7-vita/source/dynlib.c` | Large shared stub set — extend as unresolved symbols appear |
| EGL / GL | `ff7-vita/source/reimpl/egl.c`, `utils/glutil.c` | Partial stubs; ties to VitaGL |
| Assets | `ff7-vita/source/reimpl/asset_manager.*` | Must mirror Android paths under `DATA_PATH` |
| Init | `ff7-vita/source/utils/init.c` | kubridge check, `.so` load, FalsoJNI, controls init |

---

## Prerequisites on the Vita

- **kubridge** kernel plugin (required for RWX / loader behavior).
- **libshacccg.suprx** (shader compiler). `gl_preload()` in `glutil.c` exits if missing; install via ShaRKBR33D or equivalent.
- Game data at **`DATA_PATH`** (see `CMakeLists.txt`: default `ux0:data/ff7/`), including **`SO_PATH`** → `libjni_ff7.so` and unpacked OBB/layout the Android build expects.

---

## Phase 1 — Device and data layout

**Goal:** Reproducible installs with no missing-component dialogs.

- Confirm kubridge loads before the homebrew runs.
- Confirm ShaRKBR33G / libshacccg is present.
- Document the exact **`ux0:data/ff7/`** (or configured `DATA_PATH`) tree: `.so`, OBB extraction paths, any extra files.

**Done when:** Every deploy uses the same layout; loader does not stop on kubridge / shaCCG / missing `.so`.

---

## Phase 2 — First run: loader and relocations

**Goal:** Binary loads, relocates, resolves imports, `JNI_OnLoad` completes.

- Build **Debug** (`CMAKE_BUILD_TYPE=Debug`), deploy, capture logs (`DEBUG_SOLOADER`).
- For each crash or linker error: add symbols or stubs in **`dynlib.c`** / related `reimpl/*` (libc, pthread, zlib, EGL/GLES, OpenSL ES, etc.).
- Confirm **`JNI_OnLoad`** returns without aborting.

**Done when:** Process survives load + relocate + resolve + `JNI_OnLoad` (graphics may still be blank).

---

## Phase 3 — JNI surface

**Goal:** Java-facing entry points the game expects are implemented in FalsoJNI.

- Inventory JNI from APK + `.so`: smali, `strings`, Ghidra — `RegisterNatives`, classes, method names/signatures.
- Populate **`source/java.c`**: `nameToMethodId`, `methodsVoid`, `methodsObject`, etc.
- Implement handlers that call into the loaded `.so` or return safe placeholders until behavior is pinned down.

**Done when:** No rampant JNI/FalsoJNI missing-method failures; native init paths that lived on Java start executing.

---

## Phase 4 — Filesystem and assets

**Goal:** All game data opens from paths the binary expects.

- Trace **AAssetManager**, absolute paths, OBB-relative paths.
- Align **FIOS** / `DATA_PATH` layout and **`reimpl/asset_manager`** with that tree.
- Use **`patch.c`** only for hardcoded Android paths that cannot be fixed by layout alone.

**Done when:** Asset reads succeed (logs, first textures/audio, or equivalent proof).

---

## Phase 5 — Graphics and main loop

**Goal:** Real EGL/GLES usage matches VitaGL; game drives frames correctly.

- Extend **`reimpl/egl.c`** and GLES handling as demanded by the `.so` (configs, queries, surfaces).
- Replace the **`main.c`** placeholder loop with the real driver: exported **`native*`** / frame functions the Java layer used to invoke — not an infinite `gl_swap()` alone.

**Done when:** First frame or measurable GL activity; loop runs without immediate crash.

---

## Phase 6 — Audio and input

**Goal:** Sound and controls reach the game.

- Match **OpenSL ES** (and related) imports in **`dynlib.c`** / reimpls to what `libjni_ff7.so` uses.
- Wire **`controls_handler_*`** in `main.c` / **`reimpl/controls.c`** to JNI or direct native symbols.

**Done when:** Audio works or is safely stubbed without blocking; input is recognized in-game.

---

## Phase 7 — Stability, performance, polish

**Goal:** Playable sessions.

- Tune **heap** (`_newlib_heap_size_user`, `sceLibcHeapSize`), **clocks** in `init.c`, **shader** options in `CMakeLists.txt` (`SHADER_FORMAT`, `DUMP_COMPILED_SHADERS`).
- Profile memory, long-run crashes; optional LiveArea **configurator** path (`settings.c` — still sample settings in boilerplate).

**Done when:** Acceptable performance and reliability for real play.

---

## Day-to-day iteration loop

1. Build  
2. Deploy to Vita  
3. Run with logging  
4. Fix the **first** failure (symbol, JNI, path, EGL, audio)  
5. Repeat  

Phases can overlap, but avoid deep JNI work before **Phase 2** proves load + `JNI_OnLoad`.

---

## Success milestones (cross-check)

Aligns with [`FF7-Port-Context.md`](./FF7-Port-Context.md) §8:

- `.so` loads without immediate crash  
- No unresolved critical symbols  
- Rendering path initializes  
- First frame (even wrong)  
- Game loop runs  
- Input recognized  
- Playable state  

---

## Open areas (ongoing)

- GLES2 completeness via VitaGL  
- Android API coverage gaps  
- Memory limits on Vita  
- Input feel and mappings  

---

*Last updated from port planning discussion (repo: meteor-vita / ff7-vita).*
