/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>

#include "reimpl/ff7_input_hooks.h"
#include "utils/ff7_boot_log.h"

extern so_module so_mod;

void so_patch(void) {
    uintptr_t addr;

    /* GetAsyncKeyState: temporarily disabled to test whether fn_onKey alone
     * drives title-screen navigation.  Re-enable once fn_onKey is confirmed
     * working for the title screen; GAK will be needed for in-game controls. */

    /* GetDeviceState: DEFERRED — returning DI_OK from this hook switches the
     * game into "DirectInput/gamepad mode" and silences fn_onKey + fn_onTouch,
     * breaking all input.  Use GetAsyncKeyState alone for navigation until we
     * can confirm the game supports simultaneous touch + DirectInput. */
}
