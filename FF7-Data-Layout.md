# Phase 1 — Vita setup and `ux0:data/ff7/` layout

Use this checklist so every install matches what **`ff7-vita`** expects. Defaults come from [`ff7-vita/CMakeLists.txt`](ff7-vita/CMakeLists.txt) (`DATA_PATH`, `SO_PATH`).

---

## 1. System requirements (before first launch)

| Requirement | Why |
|-------------|-----|
| **[kubridge](https://github.com/bythos14/kubridge)** kernel plugin loaded | So-loader needs RWX / loader behavior; [`init.c`](ff7-vita/source/utils/init.c) fatals if the module is not loaded. |
| **libshacccg.suprx** (e.g. via ShaRKBR33D) | VitaGL shader compilation; [`gl_preload()`](ff7-vita/source/utils/glutil.c) exits if neither `ur0:/data/libshacccg.suprx` nor `ur0:/data/external/libshacccg.suprx` exists. |

Confirm both on the device before debugging “black screen” or immediate exit.

---

## 2. Canonical data directory

| CMake setting | Default value |
|----------------|---------------|
| `DATA_PATH` | `ux0:data/ff7/` (must include trailing slash in CMake; Vita path is **`ux0:data/ff7/`**) |
| `SO_PATH` | `${DATA_PATH}libjni_ff7.so` → **`ux0:data/ff7/libjni_ff7.so`** |

Override at configure time if needed:

```bash
cmake -S ff7-vita -B build -DDATA_PATH=ux0:data/ff7/ -DSO_PATH=ux0:data/ff7/libjni_ff7.so
```

Keep **`DATA_PATH` and trailing-slash convention** consistent with [`asset_manager.cpp`](ff7-vita/source/reimpl/asset_manager.cpp) (string concatenation).

---

## 3. Folder layout on the Vita

Copy via FTP/USB/VitaShell into **`ux0:data/ff7/`**:

```
ux0:data/ff7/
├── libjni_ff7.so          ← ARMv7 library from the Android APK (required)
├── assets/                ← APK `assets/` mirror (required for AAssetManager)
│   ├── Shaders/           ← Shader.fsh / Shader.vsh / Shader_old.vsh / Shader_yuv.fsh
│   └── button_*.png       ← per-language D-pad / button overlays
├── data/                  ← OBB `ff7_1.02/data/` extracted (movies, music, .tim, .dat …) [required]
├── save/                  ← OBB `ff7_1.02/save/` extracted [optional: only if the OBB ships one]
├── Documents/             ← writable: in-game saves + APP.LOG; create empty folder before first run
├── glsl/                  ← optional; created/used when shader dump/cache uses GLSL
├── cg/                    ← optional; CG shader cache
├── gxp/                   ← optional; GXP shader cache
├── debug.log              ← optional; appended each run by `ff7_boot_log` (bring-up)
└── config.txt             ← optional; port settings (see settings.c)
```

### 3.1 Native library

- **`libjni_ff7.so`**: Extract from the FF7 Android APK (`lib/armeabi-v7a/` or equivalent). Name must match **`SO_PATH`** (default filename above).

### 3.2 APK assets (`AAssetManager`)

[`AAssetManager_open`](ff7-vita/source/reimpl/asset_manager.cpp) resolves:

`DATA_PATH` + `"assets/"` + `filename`

So anything the game loads via the asset manager must live under **`ux0:data/ff7/assets/`**, with **`filename`** matching the path string the game passes in (same as paths under the APK **`assets/`** directory).

**Setup:** Unzip the APK and copy its `assets/` tree into **`ux0:data/ff7/assets/`**, preserving subfolders. The full set the game touches is small:

- `Shaders/Shader.fsh`, `Shaders/Shader.vsh`, `Shaders/Shader_old.vsh`, `Shaders/Shader_yuv.fsh`
- `button_jp_en.png`, `button_de.png`, `button_es.png`, `button_fr.png`

### 3.3 Expansion OBB (`ExpansionFile` / native fd)

Android ships the bulk of FF7 in an **OBB** (`.obb`) zip whose internal root is **`ff7_1.02/`**. The native code never opens those files directly — it asks the Java side for an `AssetFileDescriptor` via `ExpansionFile.OPEN_FILE_DESCRIPTOR(name)` (handled in [`ff7-vita/source/java.c`](ff7-vita/source/java.c)). The names look like:

```
/ff7_1.02/data/movies/staffroll.avi
/ff7_1.02/data/music_2/...
/ff7_1.02/save/savefile.dat
data\fhuda.tim          ← a few callers omit the OBB root
data\stage57.dat
```

[`translate_asset_path()`](ff7-vita/source/java.c) normalises all of these so the on-disk layout is plain and unnested:

| Native path the `.so` requests | Vita path it resolves to |
|---|---|
| `/ff7_1.02/data/movies/X` | `ux0:data/ff7/data/movies/X` |
| `/ff7_1.02/save/X` | `ux0:data/ff7/save/X` |
| `data\fhuda.tim` | `ux0:data/ff7/data/fhuda.tim` |
| `Shaders/Shader.fsh` | `ux0:data/ff7/Shaders/Shader.fsh` (rare; usually via assets) |

**Setup:** Open the FF7 OBB zip (`main.*.jp.co.d4e.materialg.obb`) and extract **the contents of its `ff7_1.02/data/` directory** directly into **`ux0:data/ff7/data/`** — do *not* keep the `ff7_1.02/` parent. Backslash-named entries (e.g. `data\fhuda.tim`) land alongside their forward-slash siblings under `data/`. If the OBB also ships an `ff7_1.02/save/` tree (read-only seed data such as NG+ clears), extract it to `ux0:data/ff7/save/`; otherwise skip it. No other top-level OBB directories are referenced by the native code.

### 3.4 Writable saves and log (`Documents/`)

The `.so` builds save / log paths from the dataPath the Java side hands it via `setDataPath(...)`. The native format strings include `"%s/Documents/%s"` and a literal `"Documents/APP.LOG"`, so it expects **`<dataPath>/Documents/`** to exist and be writable.

[`ff7-vita/source/main.c`](ff7-vita/source/main.c) (Phase 5) calls `setDataPath("ux0:data/ff7")`, which makes the writable directory:

```
ux0:data/ff7/Documents/    ← create this directory yourself, or it will be auto-created on first save
```

If it is missing, the boot log will show `[io] fopen("...Documents/APP.LOG", "a") -> MISSING` and saves will silently fail.

### 3.5 Generated / optional paths

| Path | Purpose |
|------|---------|
| `DATA_PATH/glsl/`, `cg/`, `gxp/` | Shader caches when dumping/compiling ([`glutil.c`](ff7-vita/source/utils/glutil.c)); may appear after first runs. |
| `DATA_PATH/config.txt` | [`settings.c`](ff7-vita/source/utils/settings.c) key/value text (boilerplate sample keys unless you change them). |

---

## 4. Runtime verification (implemented)

On startup, **`soloader_verify_data_layout()`** in [`ff7-vita/source/utils/init.c`](ff7-vita/source/utils/init.c) runs **after** the kubridge check and **before** loading `SO_PATH`:

1. **libshacccg.suprx** — same paths as [`libshacccg_installed()`](ff7-vita/source/utils/glutil.c) / [`gl_preload()`](ff7-vita/source/utils/glutil.c); fails fast with a dialog (no need to load the `.so` first).
2. **Always-on log** — `sceClibPrintf` lines with `DATA_PATH` and `SO_PATH` (visible over serial/net logging).
3. **`DATA_PATH`** — must exist and be a directory (`is_dir`).
4. **`DATA_PATH/assets/`** — must exist as a directory (APK `assets/` mirror for `AAssetManager_open`).

Then the existing checks continue: **`SO_PATH`** file exists → load native library → … → **`gl_preload()`** (still asserts shaCCG for callers that skip early init paths).

---

## 5. Phase 1 completion checklist

Use this on each fresh install:

- [ ] kubridge installed and loaded  
- [ ] libshacccg present (`ur0:/data/libshacccg.suprx` or external path above)  
- [ ] **`ux0:data/ff7/libjni_ff7.so`** exists and matches **SO_PATH**  
- [ ] **`ux0:data/ff7/assets/`** populated from APK **`assets/`** (Shaders + button_*.png)  
- [ ] **`ux0:data/ff7/data/`** populated from OBB **`ff7_1.02/data/`** (movies, music, *.tim, *.dat) — required  
- [ ] **`ux0:data/ff7/save/`** populated from OBB **`ff7_1.02/save/`** — optional, only if OBB ships one  
- [ ] **`ux0:data/ff7/Documents/`** exists as an empty writable folder (for in-game saves + APP.LOG)  
- [ ] Same layout documented or scripted so teammates/devices match  

**Phase 1 done when:** Launch passes **kubridge**, **ShaRKBR33D/shaCCG**, **DATA_PATH / assets layout**, and **missing `.so`** checks—i.e. you reach whatever failure comes next (typically loader/JNI), not configuration dialogs from [`init.c`](ff7-vita/source/utils/init.c) / [`gl_preload()`](ff7-vita/source/utils/glutil.c).

---

## 6. Related docs

- [FF7-Port-Plan.md](FF7-Port-Plan.md) — full phased plan  
- [FF7-Port-Context.md](FF7-Port-Context.md) — technical background  
