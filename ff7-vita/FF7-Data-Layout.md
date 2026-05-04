# Phase 1 — Vita setup and `ux0:data/ff7/` layout

Use this checklist so every install matches what **`ff7-vita`** expects. Defaults come from [`ff7-vita/CMakeLists.txt`](ff7-vita/CMakeLists.txt) (`DATA_PATH`, `SO_PATH`).

---

## 1. System requirements (before first launch)

| Requirement | Why |
|-------------|-----|
| **[kubridge](https://github.com/bythos14/kubridge)** kernel plugin loaded | So-loader needs RWX / loader behavior; [`init.c`](ff7-vita/source/utils/init.c) fatals if the module is not loaded. |
| **libshacccg.suprx** (e.g. via ShaRKBR33D) | VitaGL shader compilation; [`gl_preload()`](ff7-vita/source/utils/glutil.c) exits if neither `ur0:/data/libshacccg.suprx` nor `ur0:/data/external/libshacccg.suprx` exists. |

Confirm both on the device before debugging "black screen" or immediate exit.

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

---

## 3. Folder layout on the Vita

Copy via FTP/USB/VitaShell into **`ux0:data/ff7/`**:

```
ux0:data/ff7/
├── libjni_ff7.so              ← ARMv7 library from the Android APK (required)
├── ff7_1.02/                  ← OBB contents, keeping the OBB root (required)
│   └── data/
│       ├── sound/
│       │   ├── audio.fmt
│       │   └── audio.dat
│       ├── music/
│       │   └── music.idx
│       └── movies/
│           └── *.mp4
├── assets/                    ← APK assets/ mirror (required for AAssetManager)
│   └── Shaders/
│       ├── Shader.fsh
│       ├── Shader.vsh
│       ├── Shader_old.vsh
│       └── Shader_yuv.fsh
├── Documents/                 ← writable: in-game saves + APP.LOG (auto-created on first run)
├── glsl/                      ← shader cache (auto-created on first run)
└── gxp/                       ← shader cache (auto-created on first run)
```

### 3.1 Native library

- **`libjni_ff7.so`**: Extract from the FF7 Android APK (`lib/armeabi-v7a/` or equivalent). Name must match **`SO_PATH`** (default filename above).

### 3.2 OBB data (`ExpansionFile` / native fd)

Android ships the bulk of FF7 in an **OBB** (`.obb`) zip whose internal root is **`ff7_1.02/`**. The on-disk layout preserves this root so native code paths resolve correctly without any extra stripping.

[`translate_asset_path()`](ff7-vita/source/java.c) uses [`path_translate_data()`](ff7-vita/source/utils/path_translate.c), which keeps the `ff7_1.02/` prefix:

| Native path the `.so` requests | Vita path it resolves to |
|---|---|
| `/ff7_1.02/data/movies/staffroll.mp4` | `ux0:data/ff7/ff7_1.02/data/movies/staffroll.mp4` |
| `/ff7_1.02/data/sound/audio.fmt` | `ux0:data/ff7/ff7_1.02/data/sound/audio.fmt` |
| `/ff7_1.02/data/music/music.idx` | `ux0:data/ff7/ff7_1.02/data/music/music.idx` |
| `data\fhuda.tim` (bare, from native I/O) | `ux0:data/ff7/ff7_1.02/data/fhuda.tim` |
| `save\savefile.dat` (bare) | `ux0:data/ff7/ff7_1.02/save/savefile.dat` |

There is **no** top-level `ux0:data/ff7/data/` folder in the canonical layout; everything under the OBB lives in `ff7_1.02/`. The optional **`ff7_1.02/save/`** directory only appears if the OBB ships read-only seed data (or after the game creates saves under `Documents/` — runtime saves use `Documents/`, not this tree).

**Setup:** Open the FF7 OBB zip (`main.*.jp.co.d4e.materialg.obb`) and extract the **entire `ff7_1.02/` directory** into **`ux0:data/ff7/`**, so that `ux0:data/ff7/ff7_1.02/data/` exists. Preserve all subdirectories.

Key subdirectories inside `ff7_1.02/data/`:

| Subdirectory | Contents |
|---|---|
| `sound/` | `audio.fmt`, `audio.dat` — PCM sound effect bank |
| `music/` | `music.idx` (and associated music data) |
| `movies/` | `*.mp4` cutscene files |

### 3.3 Writable saves and log (`Documents/`)

The `.so` builds save / log paths from the dataPath the Java side hands it via `setDataPath(...)`. The native format strings include `"%s/Documents/%s"` and a literal `"Documents/APP.LOG"`, so it expects **`<dataPath>/Documents/`** to exist and be writable.

**`soloader_verify_data_layout()`** in [`init.c`](ff7-vita/source/utils/init.c) calls `sceIoMkdir` for `Documents/` at startup, so it is created automatically on the first run. No manual step required.

### 3.4 Shader caches (`glsl/`, `gxp/`)

These directories are created automatically by `soloader_verify_data_layout()` at startup and are used for shader cache files generated at runtime. They do not need to be pre-populated.

### 3.5 APK assets (shaders, button PNGs)

Bare APK-asset paths loaded via `AAssetManager` are routed through the `assets/` subdirectory by [`path_translate_asset()`](ff7-vita/source/utils/path_translate.h):

| APK asset path | Vita path it resolves to |
|---|---|
| `Shaders/Shader.fsh` | `ux0:data/ff7/assets/Shaders/Shader.fsh` |
| `Shaders/Shader.vsh` | `ux0:data/ff7/assets/Shaders/Shader.vsh` |
| `button_jp_en.png` | `ux0:data/ff7/assets/button_jp_en.png` |

**Setup:** Unzip the APK and copy its `assets/` tree into **`ux0:data/ff7/assets/`**, preserving subfolders. The minimum required set is the `Shaders/` directory (four `.fsh`/`.vsh` files). Button PNGs are optional for initial bring-up.

---

## 4. Runtime verification (implemented)

On startup, **`soloader_verify_data_layout()`** in [`ff7-vita/source/utils/init.c`](ff7-vita/source/utils/init.c) runs **after** the kubridge check and **before** loading `SO_PATH`:

1. **libshacccg.suprx** — same paths as [`libshacccg_installed()`](ff7-vita/source/utils/glutil.c); fails fast with a dialog.
2. **Always-on log** — `sceClibPrintf` lines with `DATA_PATH` and `SO_PATH` (visible over serial/net logging).
3. **`DATA_PATH`** — must exist and be a directory (`is_dir`).
4. **`DATA_PATH/ff7_1.02/data/`** — must exist (confirms OBB was extracted at the right level).
5. **`DATA_PATH/assets/`** — must exist (shaders are loaded at startup via AAssetManager).
6. **`Documents/`, `glsl/`, `gxp/`** — created via `sceIoMkdir` if not already present.

Then: **`SO_PATH`** file exists → load native library → … → **`gl_preload()`**.

---

## 5. Phase 1 completion checklist

Use this on each fresh install:

- [ ] kubridge installed and loaded
- [ ] libshacccg present (`ur0:/data/libshacccg.suprx` or external path above)
- [ ] **`ux0:data/ff7/libjni_ff7.so`** exists and matches **SO_PATH**
- [ ] **`ux0:data/ff7/ff7_1.02/data/sound/`** contains `audio.fmt` and `audio.dat`
- [ ] **`ux0:data/ff7/ff7_1.02/data/music/`** contains `music.idx`
- [ ] **`ux0:data/ff7/ff7_1.02/data/movies/`** contains `*.mp4` cutscenes
- [ ] **`ux0:data/ff7/assets/Shaders/`** populated from APK `assets/Shaders/` (`.fsh` / `.vsh` files)
- [ ] **`ux0:data/ff7/Documents/`** exists (auto-created on first run, or create manually)
- [ ] Same layout documented or scripted so teammates/devices match

**Phase 1 done when:** Launch passes **kubridge**, **ShaRKBR33D/shaCCG**, **DATA_PATH / ff7_1.02 layout**, and **missing `.so`** checks — i.e. you reach the next failure (typically loader/JNI), not configuration dialogs from [`init.c`](ff7-vita/source/utils/init.c) / [`gl_preload()`](ff7-vita/source/utils/glutil.c).

---

## 6. Related docs

- [FF7-Port-Plan.md](FF7-Port-Plan.md) — full phased plan
- [FF7-Port-Context.md](FF7-Port-Context.md) — technical background
