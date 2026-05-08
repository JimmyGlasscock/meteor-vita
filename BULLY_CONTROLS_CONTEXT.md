# Bully Vita – Controls & Input System Context

## How the Port Works (High-Level)

This is **not** a source port. It is a **Vita loader** that:

1. Loads the official Android ARMv7 `libBully.so` into memory at `0x98000000`
2. Resolves symbols and patches hooks to replace Android/JNI calls with Vita-native equivalents
3. **Fakes a JNI environment** that the Android binary calls into each frame for gamepad state

The Android binary (`libBully.so`) thinks it is running on Android with a PS3 controller plugged in.
The entire in-game control layout (what each button does in combat, menus, driving, etc.) is defined
inside `libBully.so` — none of that logic exists in this repo's source. The loader's job is simply to
feed the `.so` accurate PS3-style gamepad data sourced from Vita hardware.

---

## Entry Point & Hardware Initialization

**File:** `loader/main.c` — `main()` starting at line 925

```c
sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);         // enables L2/R2 as analog buttons
sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panelInfoFront);  // stores panel bounds for touch-zone math
```

- `SCE_CTRL_MODE_ANALOG_WIDE` is required to read L2/R2 as distinct button bits (not available in
  standard mode).
- `panelInfoFront` (`SceTouchPanelInfo`) stores the active-area bounds (`minAaX`, `maxAaX`,
  `minAaY`, `maxAaY`) used to divide the screen into input zones.
- **Back touchscreen** is started by `sceTouchPeek` calls in `jni_patch.c` but its data is
  **never consumed** — back touch is effectively unused.

**CPU/GPU clocks** are maxed at startup:
```c
scePowerSetArmClockFrequency(444);   // CPU: 444 MHz
scePowerSetBusClockFrequency(222);
scePowerSetGpuClockFrequency(222);
scePowerSetGpuXbarClockFrequency(166);
```

---

## The JNI Input Pipeline

**File:** `loader/jni_patch.c`

Every frame, `libBully.so` calls three JNI methods through the fake `JNIEnv`. These are intercepted
by the method ID dispatch table in `CallIntMethodV` and `CallFloatMethodV`:

```
libBully.so calls GetGamepadType(port)
  → sceCtrlPeekBufferPositiveExt2() refreshes `pad` struct
  → sceTouchPeek() refreshes `touch_front` and `touch_back`
  → returns 8 (= PS3 controller type)

libBully.so calls GetGamepadButtons(port)
  → reads already-polled `pad.buttons` bitmask
  → also reads `touch_front` to synthesize L3/R3
  → returns Android-format button bitmask

libBully.so calls GetGamepadAxis(port, axis)
  → reads already-polled `pad.lx/ly/rx/ry`
  → also reads `touch_front` to synthesize L2/R2
  → returns normalized float in range [-1.0, 1.0], or 0.0 if within deadzone
```

**Critical:** `GetGamepadType` is always called first; it is the only function that actually polls
hardware. `GetGamepadButtons` and `GetGamepadAxis` operate on the stale data from that poll.

### Controller Type Reported

`GetGamepadType` always returns **`8`**, which the Android binary treats as a **PS3 controller**.
Known type constants (from comments in source):
```
0, 5, 6 → XBOX 360
4       → MogaPocket
7       → MogaPro
8       → PS3          ← what this port reports
9       → IOSExtended
10      → IOSSimple
```

---

## Button Mapping

**File:** `loader/jni_patch.c` — `GetGamepadButtons()` lines 112–159

Vita `SCE_CTRL_*` flags are converted into a single Android-format integer bitmask:

| Vita Physical Button | SCE_CTRL constant   | Android Bit | Bitmask  |
|----------------------|---------------------|-------------|----------|
| Cross (×)            | `SCE_CTRL_CROSS`    | bit 0       | `0x0001` |
| Circle (○)           | `SCE_CTRL_CIRCLE`   | bit 1       | `0x0002` |
| Square (□)           | `SCE_CTRL_SQUARE`   | bit 2       | `0x0004` |
| Triangle (△)         | `SCE_CTRL_TRIANGLE` | bit 3       | `0x0008` |
| Start                | `SCE_CTRL_START`    | bit 4       | `0x0010` |
| Select               | `SCE_CTRL_SELECT`   | bit 5       | `0x0020` |
| L1                   | `SCE_CTRL_L1`       | bit 6       | `0x0040` |
| R1                   | `SCE_CTRL_R1`       | bit 7       | `0x0080` |
| D-pad Up             | `SCE_CTRL_UP`       | bit 8       | `0x0100` |
| D-pad Down           | `SCE_CTRL_DOWN`     | bit 9       | `0x0200` |
| D-pad Left           | `SCE_CTRL_LEFT`     | bit 10      | `0x0400` |
| D-pad Right          | `SCE_CTRL_RIGHT`    | bit 11      | `0x0800` |
| L3 (left stick click)| `SCE_CTRL_L3`       | bit 12      | `0x1000` |
| R3 (right stick click)| `SCE_CTRL_R3`      | bit 13      | `0x2000` |

There is **no button remapping** layer. Vita buttons map 1:1 to their PS3 equivalents in the bitmask.
What those PS3 buttons do in-game is hardcoded in `libBully.so`.

---

## Analog Stick Mapping

**File:** `loader/jni_patch.c` — `GetGamepadAxis()` lines 161–208

Axis indices reported to the game:

| Axis Index | Source           | Raw Range | Formula                              |
|------------|------------------|-----------|--------------------------------------|
| 0          | Left stick X     | 0–255     | `(pad.lx - 128.0f) / 128.0f`        |
| 1          | Left stick Y     | 0–255     | `(pad.ly - 128.0f) / 128.0f`        |
| 2          | Right stick X    | 0–255     | `(pad.rx - 128.0f) / 128.0f`        |
| 3          | Right stick Y    | 0–255     | `(pad.ry - 128.0f) / 128.0f`        |
| 4          | L2 trigger       | binary    | `1.0f` if pressed, else touch/0.0f   |
| 5          | R2 trigger       | binary    | `1.0f` if pressed, else touch/0.0f   |

### Deadzone

A single global deadzone threshold is applied to **all axes** after computing the value:

```c
if (fabsf(val) > 0.25f)
    return val;
return 0.0f;
```

- Threshold: **±0.25** in normalized [-1.0, 1.0] space
- Applied uniformly — no per-axis tuning, no circular deadzone, no sensitivity curve
- This means roughly the inner 25% of each stick's travel is dead

---

## L2 / R2 Trigger Handling

**File:** `loader/jni_patch.c` — `GetGamepadAxis()` lines 177–201

L2 and R2 are exposed to the game as **analog axes (4 and 5)**, not as button bits. They output
`1.0f` when triggered, `0.0f` when not (binary — no pressure sensitivity).

### Priority order for L2 (axis 4) and R2 (axis 5):
1. **Physical L2/R2 buttons** (from `pad.buttons & SCE_CTRL_L2/R2`) → `val = 1.0f`, short-circuits
2. **Front touchscreen upper-half** → `val = 1.0f` if a touch falls in the correct zone (see below)
3. **Deadzone check** → if `fabsf(val) <= 0.25f`, returns `0.0f` (L2/R2 always return exactly 0 or
   1, so they always pass this check)

---

## Front Touchscreen Input Zones

**Files:** `loader/jni_patch.c`, `loader/config.h`

The front touchscreen is divided into four quadrants using the panel's reported active area bounds
(`panelInfoFront`) plus a horizontal margin (`TOUCH_X_MARGIN = 100` raw units from `config.h`).

```
Screen divided at:
  Vertical midpoint:   (minAaY + maxAaY) / 2
  Horizontal midpoint: (minAaX + maxAaX) / 2
  Left margin edge:    minAaX + TOUCH_X_MARGIN  (= minAaX + 100)
  Right margin edge:   maxAaX - TOUCH_X_MARGIN  (= maxAaX - 100)
```

### Top half of screen (y < vertical midpoint) — L2 / R2:

```
┌────────────────────────────────────────────────────────┐
│  [margin]  ←  TOP-LEFT zone  →  [h-mid]  ←  TOP-RIGHT zone  →  [margin]  │
│            triggers L2 (axis 4)           triggers R2 (axis 5)            │
└────────────────────────────────────────────────────────┘
```

- Touch in **top-left zone** (x >= `minAaX + 100` AND x < horizontal midpoint): → **L2** (`axis 4 = 1.0f`)
- Touch in **top-right zone** (x >= horizontal midpoint AND x < `maxAaX - 100`): → **R2** (`axis 5 = 1.0f`)
- Touches within the 100-unit margins on either side are **ignored**

### Bottom half of screen (y >= vertical midpoint) — L3 / R3:

```
┌────────────────────────────────────────────────────────┐
│  [margin]  ←  BOT-LEFT zone  →  [h-mid]  ←  BOT-RIGHT zone  →  [margin]  │
│            triggers L3 (bit 12)           triggers R3 (bit 13)            │
└────────────────────────────────────────────────────────┘
```

- Touch in **bottom-left zone** (x >= `minAaX + 100` AND x < horizontal midpoint): → **L3** (`mask |= 0x1000`)
- Touch in **bottom-right zone** (x >= horizontal midpoint AND x < `maxAaX - 100`): → **R3** (`mask |= 0x2000`)
- Both physical L3/R3 (stick clicks) **and** touch-zone L3/R3 can fire simultaneously — they set the same bits

---

## Hooks & Stubs Affecting Input

**File:** `loader/main.c` — `patch_game()` lines 376–401

### ProcessEvents

The Android binary calls `ProcessEvents(bool)` each frame to pump its event queue (touch events,
system events). On the Vita this is **stubbed**:

```c
int ProcessEvents(void) {
    movie_draw_frame();  // only handles video playback frames
    return 0;            // returning 1 would signal exit
}
```

- The `// TODO: implement touch here` comment indicates this is a known gap
- Because `ProcessEvents` is stubbed, **the Android touch event pipeline is completely bypassed**
- All gameplay input flows exclusively through the `GetGamepadType/Buttons/Axis` JNI path

### TouchSense (Haptics)

All haptic feedback functions from the Android `TouchSense` SDK are stubbed to no-ops:

```c
_ZN10TouchSenseC2Ev                        → TouchSense__TouchSense() (returns `this`, no-op constructor)
_ZN10TouchSense20stopContinuousEffectEv    → ret0
_ZN10TouchSense14stopAllEffectsEv          → ret0
_ZN10TouchSense28startContinuousBuiltinEffectEiiii → ret0
_ZN10TouchSense25playBuiltinEffectInternalEii      → ret0
_ZN10TouchSense17playBuiltinEffectEiiii            → ret0
```

No vibration/haptics of any kind.

---

## JNI Method Dispatch Table

**File:** `loader/jni_patch.c` — `name_to_method_ids[]` lines 46–67, dispatch at lines 253–315

The fake `JNIEnv` routes method calls by integer ID. Input-relevant IDs:

| Method Name        | Enum ID              | Dispatch function       | Returns |
|--------------------|----------------------|-------------------------|---------|
| `GetGamepadType`   | `GET_GAMEPAD_TYPE`   | `CallIntMethodV`        | `int`   |
| `GetGamepadButtons`| `GET_GAMEPAD_BUTTONS`| `CallIntMethodV`        | `int`   |
| `GetGamepadAxis`   | `GET_GAMEPAD_AXIS`   | `CallFloatMethodV`      | `float` |

`GetMethodID` resolves method names to these integer IDs by linear scan of `name_to_method_ids[]`.
The `fake_env` vtable wires JNI function-pointer slots to the correct `Call*MethodV` dispatchers:

```c
*(uintptr_t *)(fake_env + 0xC8) = (uintptr_t)CallIntMethodV;
*(uintptr_t *)(fake_env + 0xE0) = (uintptr_t)CallFloatMethodV;
```

---

## Configuration Constants

**File:** `loader/config.h`

| Constant             | Value                         | Purpose                                      |
|----------------------|-------------------------------|----------------------------------------------|
| `TOUCH_X_MARGIN`     | `100`                         | Horizontal dead-margin for touch input zones |
| `SCREEN_W`           | `960`                         | Vita screen width (reported to game)         |
| `SCREEN_H`           | `544`                         | Vita screen height (reported to game)        |
| `CONFIG_PATH`        | `"ux0:data/Bully/config.txt"` | Defined but **never read** by loader source  |
| `DATA_PATH`          | `"ux0:data/Bully"`            | Root path for game data                      |
| `SO_PATH`            | `"ux0:data/Bully/libBully.so"`| Path to the Android shared library           |

`CONFIG_PATH` is a dead constant — no loader code reads it. There is currently no file-based control
configuration or remapping system.

---

## What Is NOT In This Repo

The following control behaviors are defined inside `libBully.so` (Android binary) and are not
accessible from this codebase:

- What each button/axis does in-game (punch, run, aim, camera, menus, etc.)
- Any in-game control settings / sensitivity sliders
- Controller dead-zone or sensitivity processing beyond the 0.25 loader deadzone
- Any PS3-vs-Xbox control scheme branching logic

---

## Key Files Summary

| File                    | Role in Input System                                                  |
|-------------------------|-----------------------------------------------------------------------|
| `loader/jni_patch.c`    | **Core** — polls hardware, translates to Android bitmask/axes, dispatches JNI |
| `loader/main.c`         | Hardware init, `ProcessEvents` stub, `TouchSense` stubs, `patch_game()` hooks |
| `loader/config.h`       | `TOUCH_X_MARGIN` (100), screen dimensions                             |
| `loader/main.h`         | Declares `panelInfoFront` (touch panel bounds used in zone math)      |
| `loader/dialog.c`       | Restores `SCE_CTRL_MODE_ANALOG_WIDE` after IME keyboard dialogs       |
