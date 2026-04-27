# PS Vita Port – Final Fantasy VII (Android)

## Project Context File

---

## 1. Objective

Port the Android version of Final Fantasy VII to PlayStation Vita using so-loader, enabling native execution of ARMv7 Android binaries on Vita hardware.

---

## 2. Known Facts

* The PlayStation Vita uses an ARMv7 CPU architecture.

* The Android version of Final Fantasy VII includes native ARMv7 `.so` libraries.

* The game uses OpenGL ES 2.0 (GLES2) for rendering.

* so-loader is an ELF loader that enables execution of Android native libraries on Vita by providing a minimal Android-like runtime environment.

* so-loader requires manual implementation or stubbing of missing Android APIs.

* A boilerplate implementation exists:
  https://github.com/v-atamanenko/soloader-boilerplate

* Other Android games (including Final Fantasy III and IV) have been ported to Vita using this method.

---

## 3. Required Inputs

### 3.1 Game Files

* Android APK file
* Extracted native libraries (`.so` files)
* Game asset files (OBB or internal data)

### 3.2 Tooling

* APK extraction tools (e.g., apktool, unzip)
* Native binary analysis tools (e.g., Ghidra, IDA)
* PS Vita development environment (VitaSDK)
* so-loader boilerplate project

---

## 4. Core Technical Areas

### 4.1 Native Library Execution

* Load ARMv7 `.so` files using so-loader
* Identify entry points (e.g., `JNI_OnLoad`)
* Ensure proper linking and symbol resolution

### 4.2 Android API Surface

* Provide or stub required Android APIs used by the game:

  * File I/O (AAssetManager or equivalents)
  * Logging
  * Threading (pthreads compatibility)
  * Timing functions
  * JNI interfaces

### 4.3 Graphics

* The game uses GLES2
* Vita uses a different graphics API (sceGxm)
* A translation or wrapper layer is required to map GLES2 calls to Vita-compatible rendering

### 4.4 Audio

* Android audio APIs (e.g., OpenSL ES) may be used
* Replacement or translation to Vita audio APIs is required

### 4.5 Input

* Android touch input must be mapped to:

  * Vita touchscreen
  * Vita physical controls

### 4.6 Filesystem

* Android asset paths must be mapped to Vita filesystem paths
* Game must be able to load all required assets correctly

---

## 5. Constraints

* No access to source code of the game
* Must rely on reverse engineering and runtime behavior
* Limited system memory on Vita
* Differences between Android runtime and Vita environment

---

## 6. Initial Tasks

### Task 1: Extract Game Data

* Extract APK contents
* Locate `.so` libraries
* Locate and organize asset files

### Task 2: Identify Main Library

* Determine primary native library used by the game
* Inspect exported symbols
* Locate initialization functions

### Task 3: Integrate with so-loader

* Add target `.so` to boilerplate project
* Attempt initial load
* Capture runtime logs and errors

### Task 4: Resolve Missing Symbols

* Identify undefined symbols during load/runtime
* Implement or stub required functions incrementally

### Task 5: Initialize Rendering

* Ensure graphics context initializes
* Confirm GLES2 calls are being made

### Task 6: Validate Execution Flow

* Confirm main loop execution
* Prevent immediate crashes

---

## 7. Iterative Development Loop

1. Build
2. Deploy to Vita (or run in emulator)
3. Execute
4. Capture logs
5. Identify failure point
6. Patch or implement missing functionality
7. Repeat

---

## 8. Success Milestones

* `.so` loads without immediate crash
* No unresolved critical symbols
* Rendering context initializes
* First frame is drawn (even if incorrect)
* Game loop runs continuously
* Input is recognized
* Game becomes playable

---

## 9. Open Work Areas

* GLES2 → Vita graphics translation completeness
* Android API coverage gaps
* Performance optimization
* Memory usage handling
* Input system refinement

---

## 10. Notes

* All compatibility issues must be resolved manually
* Progress depends on incremental resolution of runtime errors
* Logging and debugging are critical throughout the process

---
