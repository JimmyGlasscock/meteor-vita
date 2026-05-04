/*
 * Copyright (C) 2025 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/controls.h"
#include "reimpl/ff7_input_hooks.h"

#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>

/*
 * Raw analog stick thresholds for D-pad emulation, matching the approach used
 * in the FF3/FF4 Vita ports (frangarcj/ff3_vita, Rinnegatamante/ff4_vita).
 * Stick values are 0–255 with center at 128.  Anything below LOW or above HIGH
 * fires the corresponding D-pad key event.  These values (~80/170) give roughly
 * a 37 % deadzone from center on either side.
 */
#define ANALOG_DPAD_LOW  80
#define ANALOG_DPAD_HIGH 170

void controls_init(void) {
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);
    /* sceMotionStartSampling is required before sceCtrlPeekBufferPositiveExt2
     * in ANALOG_WIDE mode — without it some firmware versions don't populate
     * the analog fields, leaving lx/ly at 0 and triggering spurious D-pad
     * events on the first poll frame. */
    sceMotionStartSampling();
}

static void poll_touch(void);
static void poll_pad(void);

void controls_poll(void) {
    poll_touch();
    poll_pad();
}

/* ------------------------------------------------------------------ */
/* Touch                                                               */
/* ------------------------------------------------------------------ */

static SceTouchData touch;
static SceTouchData touch_old;

static void poll_touch(void) {
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

    for (int i = 0; i < touch.reportNum; i++) {
        float x = (float)touch.report[i].x * 960.f / 1920.0f;
        float y = (float)touch.report[i].y * 544.f / 1088.0f;

        int finger_down = 0;
        for (int j = 0; j < touch_old.reportNum; j++) {
            if (touch.report[i].id == touch_old.report[j].id) {
                finger_down = 1;
                break;
            }
        }

        if (!finger_down)
            controls_handler_touch(touch.report[i].id, x, y, CONTROLS_ACTION_DOWN);
        else
            controls_handler_touch(touch.report[i].id, x, y, CONTROLS_ACTION_MOVE);
    }

    for (int i = 0; i < touch_old.reportNum; i++) {
        int finger_up = 1;
        for (int j = 0; j < touch.reportNum; j++) {
            if (touch.report[j].id == touch_old.report[i].id) {
                finger_up = 0;
                break;
            }
        }
        if (finger_up) {
            float x = (float)touch_old.report[i].x * 960.f / 1920.0f;
            float y = (float)touch_old.report[i].y * 544.f / 1088.0f;
            controls_handler_touch(touch_old.report[i].id, x, y, CONTROLS_ACTION_UP);
        }
    }

    sceClibMemcpy(&touch_old, &touch, sizeof(touch));
}

/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */

/*
 * Map Vita buttons to Win32 VK codes, NOT Android AKEYCODE values.
 *
 * fn_onKey writes its keycode argument directly into the game's internal
 * keyboard-state buffer at index [keycode].  fw_GetAsyncKeyState(vk) reads
 * from the same buffer at index [vk].  Using AKEYCODE values (e.g. 19 for
 * DPAD_UP) writes slot 19 while the game reads slot 38 (VK_UP = 0x26) —
 * a complete mismatch.  Sending VK codes lets the original (unhooked)
 * fw_GetAsyncKeyState find the input we wrote.
 */
static const ButtonMapping mapping[] = {
    /* D-pad: Win32 arrow keys */
    { SCE_CTRL_UP,       0x26 },   /* VK_UP     */
    { SCE_CTRL_DOWN,     0x28 },   /* VK_DOWN   */
    { SCE_CTRL_LEFT,     0x25 },   /* VK_LEFT   */
    { SCE_CTRL_RIGHT,    0x27 },   /* VK_RIGHT  */
    /* Face buttons */
    { SCE_CTRL_CROSS,    0x0D },   /* VK_RETURN — confirm */
    { SCE_CTRL_CIRCLE,   0x1B },   /* VK_ESCAPE — cancel  */
    { SCE_CTRL_SQUARE,   0x58 },   /* VK_X      — special */
    { SCE_CTRL_TRIANGLE, 0x59 },   /* VK_Y      — menu    */
    /* Shoulder / system */
    { SCE_CTRL_L1,       0x21 },   /* VK_PRIOR  — page up / L1  */
    { SCE_CTRL_R1,       0x22 },   /* VK_NEXT   — page dn / R1  */
    { SCE_CTRL_START,    0x70 },   /* VK_F1     — menu / start  */
    { SCE_CTRL_SELECT,   0x71 },   /* VK_F2     — select        */
};

/* ------------------------------------------------------------------ */
/* Left-stick → D-pad emulation                                       */
/*                                                                     */
/* FF3/FF4 Vita ports pass raw stick values directly to the engine as  */
/* a D-pad bitmask.  FF7 uses key events instead, so we track which   */
/* virtual directions are currently "held" and fire DOWN/UP edges.    */
/*                                                                     */
/* The physical D-pad has priority: if the real button is already held */
/* in a direction we suppress the analog event for that direction to   */
/* avoid injecting a duplicate DOWN with no matching UP.              */
/* ------------------------------------------------------------------ */

static uint8_t adpad_up    = 0;
static uint8_t adpad_down  = 0;
static uint8_t adpad_left  = 0;
static uint8_t adpad_right = 0;

static void poll_analog_dpad(uint8_t lx, uint8_t ly, uint32_t buttons) {
    uint8_t want_up    = (ly < ANALOG_DPAD_LOW);
    uint8_t want_down  = (ly > ANALOG_DPAD_HIGH);
    uint8_t want_left  = (lx < ANALOG_DPAD_LOW);
    uint8_t want_right = (lx > ANALOG_DPAD_HIGH);

    /* Suppress analog direction if the physical D-pad button is held */
    if (buttons & SCE_CTRL_UP)    want_up    = 0;
    if (buttons & SCE_CTRL_DOWN)  want_down  = 0;
    if (buttons & SCE_CTRL_LEFT)  want_left  = 0;
    if (buttons & SCE_CTRL_RIGHT) want_right = 0;

#define EDGE(dir, code) \
    if ( want_##dir && !adpad_##dir) controls_handler_key(code, CONTROLS_ACTION_DOWN); \
    if (!want_##dir &&  adpad_##dir) controls_handler_key(code, CONTROLS_ACTION_UP);   \
    adpad_##dir = want_##dir

    EDGE(up,    0x26);   /* VK_UP    */
    EDGE(down,  0x28);   /* VK_DOWN  */
    EDGE(left,  0x25);   /* VK_LEFT  */
    EDGE(right, 0x27);   /* VK_RIGHT */

#undef EDGE
}

/* ------------------------------------------------------------------ */
/* pad poll                                                            */
/* ------------------------------------------------------------------ */

static uint32_t old_buttons = 0;

static void poll_pad(void) {
    SceCtrlData pad;
    sceClibMemset(&pad, 0, sizeof(pad));
    /* Pre-set analog axes to center (128) so a failed read doesn't produce
     * stray D-pad events from lx/ly == 0. */
    pad.lx = pad.ly = pad.rx = pad.ry = 128;
    sceCtrlPeekBufferPositiveExt2(0, &pad, 1);

    uint32_t current  = pad.buttons;
    uint32_t pressed  = current & ~old_buttons;
    uint32_t released = ~current & old_buttons;
    old_buttons = current;

    for (int i = 0; i < (int)(sizeof(mapping) / sizeof(mapping[0])); i++) {
        if (pressed  & mapping[i].sce_button)
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_DOWN);
        if (released & mapping[i].sce_button)
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_UP);
    }

    poll_analog_dpad(pad.lx, pad.ly, current);

    /* Share the freshly-read pad state with the input hooks so they never
     * call sceCtrl themselves (would fire during init_array before controls_init). */
    ff7_update_pad_cache(&pad);
}
