/*
 * fw_GetAsyncKeyState + IDirectInputDevice::GetDeviceState hooks for FF7
 * Android / PC-input stack on Vita.
 */

#include "reimpl/ff7_input_hooks.h"

#include <kubridge.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h>

#include <stdint.h>
#include <stddef.h>

so_hook ff7_gak_hook;
so_hook ff7_gds_hook;

/*
 * Win32 virtual-key codes — menus poll these via fw_GetAsyncKeyState.
 * (Values from wingdi / WinUser.h.)
 */
#define FF7_VK_LEFT     0x25
#define FF7_VK_UP       0x26
#define FF7_VK_RIGHT    0x27
#define FF7_VK_DOWN     0x28
#define FF7_VK_RETURN   0x0D
#define FF7_VK_ESCAPE   0x1B
#define FF7_VK_SPACE    0x20
#define FF7_VK_PRIOR    0x21 /* Page up */
#define FF7_VK_NEXT     0x22 /* Page down */
#define FF7_VK_NUMPAD4  0x64
#define FF7_VK_NUMPAD8  0x68
#define FF7_VK_NUMPAD6  0x66
#define FF7_VK_NUMPAD2  0x62
#define FF7_VK_F1       0x70
#define FF7_VK_F2       0x71
#define FF7_VK_X        0x58
#define FF7_VK_Y        0x59

static SceCtrlData s_ff7_pad;

void ff7_input_sync_pad_cache(void) {
    sceCtrlPeekBufferPositiveExt2(0, &s_ff7_pad, 1);
}

static uint32_t ff7_pad_combo(const SceCtrlData *p) {
    uint32_t b = p->buttons;
    /* Same thresholds as controls.c stick → D-pad */
    if (p->ly < 80)
        b |= SCE_CTRL_UP;
    if (p->ly > 170)
        b |= SCE_CTRL_DOWN;
    if (p->lx < 80)
        b |= SCE_CTRL_LEFT;
    if (p->lx > 170)
        b |= SCE_CTRL_RIGHT;
    return b;
}

/*
 * Returns Windows GetAsyncKeyState-style value: high bit set if key is down.
 * When the Vita pad is not asserting this VK, defer to the original function so
 * touch + JNI onKey updates to internal keyboard state still work.
 */
int ff7_GetAsyncKeyState_hook(unsigned int vk) {
    ff7_input_sync_pad_cache();
    const uint32_t b = ff7_pad_combo(&s_ff7_pad);
    int from_pad = 0;

    switch (vk) {
    case FF7_VK_LEFT:
    case FF7_VK_NUMPAD4:
        from_pad = (b & SCE_CTRL_LEFT) != 0;
        break;
    case FF7_VK_UP:
    case FF7_VK_NUMPAD8:
        from_pad = (b & SCE_CTRL_UP) != 0;
        break;
    case FF7_VK_RIGHT:
    case FF7_VK_NUMPAD6:
        from_pad = (b & SCE_CTRL_RIGHT) != 0;
        break;
    case FF7_VK_DOWN:
    case FF7_VK_NUMPAD2:
        from_pad = (b & SCE_CTRL_DOWN) != 0;
        break;
    case FF7_VK_RETURN:
    case FF7_VK_SPACE:
        from_pad = (b & SCE_CTRL_CROSS) != 0;
        break;
    case FF7_VK_ESCAPE:
        from_pad = (b & SCE_CTRL_CIRCLE) != 0;
        break;
    case FF7_VK_X:
        from_pad = (b & SCE_CTRL_SQUARE) != 0;
        break;
    case FF7_VK_Y:
        from_pad = (b & SCE_CTRL_TRIANGLE) != 0;
        break;
    case FF7_VK_PRIOR:
        from_pad = (b & SCE_CTRL_L1) != 0;
        break;
    case FF7_VK_NEXT:
        from_pad = (b & SCE_CTRL_R1) != 0;
        break;
    case FF7_VK_F1:
        from_pad = (b & SCE_CTRL_START) != 0;
        break;
    case FF7_VK_F2:
        from_pad = (b & SCE_CTRL_SELECT) != 0;
        break;
    default:
        return SO_CONTINUE(int, ff7_gak_hook, vk);
    }

    if (from_pad)
        return (int)(0x8001);
    return SO_CONTINUE(int, ff7_gak_hook, vk);
}

/* Win32 DIJOYSTATE — same field order as DirectInput 8 SDK. */
typedef struct {
    int32_t lX;
    int32_t lY;
    int32_t lZ;
    int32_t lRx;
    int32_t lRy;
    int32_t lRz;
    int32_t rglSlider[2];
    uint32_t rgdwPOV[4];
    uint8_t rgbButtons[32];
} ff7_dijoystate_t;

enum { ff7_axis_full = 10000 };

/* DIJOYSTATE / DIJOYSTATE2 share this prefix; rgbButtons may be 32 or 128 bytes. */
enum { ff7_dijoy_rgb_off = 48 };

static void ff7_overlay_dijoystate(void *pv, unsigned cb) {
    if (!pv || cb < ff7_dijoy_rgb_off + 4u)
        return;
    ff7_dijoystate_t *st = (ff7_dijoystate_t *)pv;
    uint32_t b = ff7_pad_combo(&s_ff7_pad);
    int x = 0, y = 0;
    if (b & SCE_CTRL_LEFT)
        x -= ff7_axis_full;
    if (b & SCE_CTRL_RIGHT)
        x += ff7_axis_full;
    if (b & SCE_CTRL_UP)
        y -= ff7_axis_full;
    if (b & SCE_CTRL_DOWN)
        y += ff7_axis_full;
    if (x == 0 && y == 0) {
        int ax = (int)s_ff7_pad.lx - 128;
        int ay = (int)s_ff7_pad.ly - 128;
        if (ax < -24 || ax > 24 || ay < -24 || ay > 24) {
            x = (ax * ff7_axis_full) / 128;
            y = (ay * ff7_axis_full) / 128;
        }
    }
    st->lX = x;
    st->lY = y;
    st->lZ = 0;
    st->lRx = st->lRy = st->lRz = 0;
    st->rglSlider[0] = st->rglSlider[1] = 0;
    uint32_t pov = 0xFFFFFFFFu;
    if (b & SCE_CTRL_UP)
        pov = 0u;
    else if (b & SCE_CTRL_RIGHT)
        pov = 9000u;
    else if (b & SCE_CTRL_DOWN)
        pov = 18000u;
    else if (b & SCE_CTRL_LEFT)
        pov = 27000u;
    st->rgdwPOV[0] = pov;
    st->rgdwPOV[1] = st->rgdwPOV[2] = st->rgdwPOV[3] = 0xFFFFFFFFu;
    size_t nbtn = (size_t)cb - ff7_dijoy_rgb_off;
    if (nbtn > 128u)
        nbtn = 128u;
    uint8_t *rgb = (uint8_t *)pv + ff7_dijoy_rgb_off;
    sceClibMemset(rgb, 0, nbtn);
#define FF7_BTN(i, cond)                                                                          \
    do {                                                                                          \
        if ((size_t)(i) < nbtn)                                                                   \
            rgb[(i)] = (uint8_t)((cond) ? 0x80u : 0u);                                           \
    } while (0)
    FF7_BTN(0, b & SCE_CTRL_CROSS);
    FF7_BTN(1, b & SCE_CTRL_CIRCLE);
    FF7_BTN(2, b & SCE_CTRL_SQUARE);
    FF7_BTN(3, b & SCE_CTRL_TRIANGLE);
    FF7_BTN(4, b & SCE_CTRL_L1);
    FF7_BTN(5, b & SCE_CTRL_R1);
    FF7_BTN(6, b & SCE_CTRL_L2);
    FF7_BTN(7, b & SCE_CTRL_R2);
    FF7_BTN(8, b & SCE_CTRL_START);
    FF7_BTN(9, b & SCE_CTRL_SELECT);
#undef FF7_BTN
}

int ff7_GetDeviceState_hook(unsigned int self, unsigned int cbData, unsigned int lpvData) {
    ff7_input_sync_pad_cache();
    int ret = SO_CONTINUE(int, ff7_gds_hook, self, cbData, lpvData);
    ff7_overlay_dijoystate((void *)(uintptr_t)lpvData, cbData);
    return ret;
}
