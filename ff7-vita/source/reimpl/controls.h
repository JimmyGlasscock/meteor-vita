/*
 * Copyright (C) 2025 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  controls.h
 * @brief Implementations for the touch screen, buttons, and other controls.
 */

#ifndef SOLOADER_CONTROLS_H
#define SOLOADER_CONTROLS_H

#include <stdint.h>

/* Actions reported by controls_handler_* callbacks */
typedef enum ControlsAction {
    CONTROLS_ACTION_UP   = 0,
    CONTROLS_ACTION_DOWN = 1,
    CONTROLS_ACTION_MOVE = 2,
} ControlsAction;

/* Android key codes sent to GLESJniWrapper_onKey */
enum {
    AKEYCODE_BACK          = 4,
    AKEYCODE_DPAD_UP       = 19,
    AKEYCODE_DPAD_DOWN     = 20,
    AKEYCODE_DPAD_LEFT     = 21,
    AKEYCODE_DPAD_RIGHT    = 22,
    AKEYCODE_DPAD_CENTER   = 23,
    AKEYCODE_BUTTON_A      = 96,
    AKEYCODE_BUTTON_B      = 97,
    AKEYCODE_BUTTON_X      = 99,
    AKEYCODE_BUTTON_Y      = 100,
    AKEYCODE_BUTTON_L1     = 102,
    AKEYCODE_BUTTON_R1     = 103,
    AKEYCODE_BUTTON_START  = 108,
    AKEYCODE_BUTTON_SELECT = 109,
};

/* SCE button → Android keycode pair used in the button mapping table */
typedef struct {
    uint32_t sce_button;
    uint32_t android_button;
} ButtonMapping;

/* Callbacks implemented in main.c */
extern void controls_handler_key(int32_t keycode, ControlsAction action);
extern void controls_handler_touch(int32_t id, float x, float y, ControlsAction action);

void controls_init(void);
void controls_poll(void);

#endif // SOLOADER_CONTROLS_H
