/*
 * Feed PC-style fw_GetAsyncKeyState() / DirectInput joystick poll paths from
 * the Vita pad. GAK covers keyboard-style menus; IDirectInputDevice
 * GetDeviceState backs the Android "gamepad" path used in-game.
 *
 * Neither hook calls SO_CONTINUE or makes any system call. The pad state is
 * written once per frame by controls_poll() via ff7_update_pad_cache() and the
 * hooks just read the cached copy. This avoids sceCtrlPeekBufferPositiveExt2
 * being called from inside the hook (which fires during init_array, before
 * controls_init / sceCtrlSetSamplingModeExt, returning lx=ly=0 and causing
 * ff7_pad_combo to report UP+LEFT pressed during game startup).
 */

#ifndef FF7_INPUT_HOOKS_H
#define FF7_INPUT_HOOKS_H

#include <psp2/ctrl.h>

/**
 * Called once per frame by controls_poll() after sceCtrlPeekBufferPositiveExt2.
 * The hooks read from the cached copy; they never call sceCtrl themselves.
 */
void ff7_update_pad_cache(const SceCtrlData *pad);

/** Hooked from patch.c via hook_addr(). Returns Win32 GetAsyncKeyState value. */
int ff7_GetAsyncKeyState_hook(unsigned int vk);

/** Hooked from patch.c via hook_addr(). Fills DIJOYSTATE from Vita pad. */
int ff7_GetDeviceState_hook(unsigned int self, unsigned int cbData, unsigned int lpvData);

#endif
