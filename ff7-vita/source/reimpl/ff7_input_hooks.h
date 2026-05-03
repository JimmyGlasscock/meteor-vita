/*
 * Feed PC-style fw_GetAsyncKeyState() / DirectInput joystick poll paths from the
 * Vita pad. GAK covers keyboard-style menus; IDirectInputDevice GetDeviceState
 * backs the Android “gamepad” path used in-game.
 */

#ifndef FF7_INPUT_HOOKS_H
#define FF7_INPUT_HOOKS_H

#include <so_util/so_util.h>

#include <stdint.h>

void ff7_input_sync_pad_cache(void);

/** Registered from patch.c; Thumb entry, LSB set when passing to hook_addr. */
int ff7_GetAsyncKeyState_hook(unsigned int vk);

/** fw_IDirectInputDeviceA_GetDeviceState(this, cbData, lpvData) — overlay pad. */
int ff7_GetDeviceState_hook(unsigned int self, unsigned int cbData, unsigned int lpvData);

/** Set in patch.c — trampoline state for SO_CONTINUE into original GAK. */
extern so_hook ff7_gak_hook;

extern so_hook ff7_gds_hook;

#endif
