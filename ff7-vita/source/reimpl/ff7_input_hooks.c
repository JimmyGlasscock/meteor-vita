/*
 * fw_GetAsyncKeyState + IDirectInputDevice::GetDeviceState hooks for FF7
 * Android / PC-input stack on Vita.
 *
 * Both hooks fully own the input response and never call SO_CONTINUE into the
 * original function. The Android-side originals read from an internal keyboard
 * buffer fed by JNI onKey events — irrelevant on Vita. Bypassing them avoids
 * the un-patch / flush / re-patch cycle that SO_CONTINUE requires, which was
 * causing a crash when the game's input system first fired.
 */

#include "reimpl/ff7_input_hooks.h"
#include "utils/ff7_boot_log.h"

#include <psp2/kernel/clib.h>

#include <stdint.h>
#include <stddef.h>

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

/* Pre-initialised to centered analog so ff7_pad_combo never sees lx/ly=0
 * before the first controls_poll() frame updates this via ff7_update_pad_cache. */
static SceCtrlData s_ff7_pad = { .lx = 128, .ly = 128, .rx = 128, .ry = 128 };

/* Combine physical buttons with left-stick D-pad emulation into a single mask. */
static uint32_t ff7_pad_combo(const SceCtrlData *p) {
    uint32_t b = p->buttons;
    if (p->ly < 80)  b |= SCE_CTRL_UP;
    if (p->ly > 170) b |= SCE_CTRL_DOWN;
    if (p->lx < 80)  b |= SCE_CTRL_LEFT;
    if (p->lx > 170) b |= SCE_CTRL_RIGHT;
    return b;
}

/*
 * Windows GetAsyncKeyState edge-detection.
 *
 * The game uses bit 0 of the return value as "key was newly pressed since the
 * last call".  If we always return 0x8001 for held keys the game sees a new
 * press every call (potentially many times per frame), causing repeated or
 * conflicting inputs.
 *
 * s_gak_edge: one bit per virtual key we handle (indexed by VK code & 0x7F).
 *   Bit SET   → key has transitioned down but the first GAK call hasn't fired yet.
 *   Bit CLEAR → key is held (bit 0 already consumed) or released.
 * s_gak_prev: tracks the previous "buttons active" bitmask so we can detect edges.
 */
#define GAK_TABLE_BITS 128
static uint32_t s_gak_edge[GAK_TABLE_BITS / 32];  /* 4 x uint32_t = 128 bits */
static uint32_t s_gak_prev_combo = 0;

/* Map a VK code to the SCE_CTRL_* bit it corresponds to (0 = unmapped). */
static uint32_t vk_to_sce(unsigned int vk) {
    switch (vk) {
    case FF7_VK_LEFT:
    case FF7_VK_NUMPAD4:  return SCE_CTRL_LEFT;
    case FF7_VK_UP:
    case FF7_VK_NUMPAD8:  return SCE_CTRL_UP;
    case FF7_VK_RIGHT:
    case FF7_VK_NUMPAD6:  return SCE_CTRL_RIGHT;
    case FF7_VK_DOWN:
    case FF7_VK_NUMPAD2:  return SCE_CTRL_DOWN;
    case FF7_VK_RETURN:
    case FF7_VK_SPACE:    return SCE_CTRL_CROSS;
    case FF7_VK_ESCAPE:   return SCE_CTRL_CIRCLE;
    case FF7_VK_X:        return SCE_CTRL_SQUARE;
    case FF7_VK_Y:        return SCE_CTRL_TRIANGLE;
    case FF7_VK_PRIOR:    return SCE_CTRL_L1;
    case FF7_VK_NEXT:     return SCE_CTRL_R1;
    case FF7_VK_F1:       return SCE_CTRL_START;
    case FF7_VK_F2:       return SCE_CTRL_SELECT;
    default:              return 0;
    }
}

static void gak_edge_set(unsigned int vk) {
    unsigned idx = vk & (GAK_TABLE_BITS - 1);
    s_gak_edge[idx >> 5] |= (1u << (idx & 31));
}
static int gak_edge_consume(unsigned int vk) {
    unsigned idx = vk & (GAK_TABLE_BITS - 1);
    uint32_t bit = 1u << (idx & 31);
    if (s_gak_edge[idx >> 5] & bit) {
        s_gak_edge[idx >> 5] &= ~bit;
        return 1;
    }
    return 0;
}

void ff7_update_pad_cache(const SceCtrlData *pad) {
    s_ff7_pad = *pad;

    /* Detect newly-pressed buttons and set their edge bits so the next
     * GetAsyncKeyState call for that VK returns bit 0 set exactly once. */
    uint32_t cur = ff7_pad_combo(pad);
    uint32_t newly_down = cur & ~s_gak_prev_combo;
    s_gak_prev_combo = cur;

    /* Walk all VK codes we handle and mark edges for newly-pressed ones. */
    static const unsigned int vk_list[] = {
        FF7_VK_LEFT, FF7_VK_UP, FF7_VK_RIGHT, FF7_VK_DOWN,
        FF7_VK_NUMPAD4, FF7_VK_NUMPAD8, FF7_VK_NUMPAD6, FF7_VK_NUMPAD2,
        FF7_VK_RETURN, FF7_VK_SPACE, FF7_VK_ESCAPE,
        FF7_VK_X, FF7_VK_Y, FF7_VK_PRIOR, FF7_VK_NEXT,
        FF7_VK_F1, FF7_VK_F2,
    };
    for (int i = 0; i < (int)(sizeof(vk_list) / sizeof(vk_list[0])); i++) {
        uint32_t sce = vk_to_sce(vk_list[i]);
        if (sce && (newly_down & sce))
            gak_edge_set(vk_list[i]);
    }
}

/*
 * Windows GetAsyncKeyState semantics:
 *   high bit (0x8000) → key is currently down
 *   low  bit (0x0001) → key was newly pressed since the previous GAK call
 *                        (consumed once per press; re-armed when key releases)
 *
 * Returning 0x8001 on every call for a held key causes the game to treat every
 * frame as a fresh key-down event, producing erratic repeated input.  We track
 * the edge in s_gak_edge[], set by ff7_update_pad_cache() when the button
 * transitions down, and clear it here on the first call that sees it set.
 */
/* Log each unique VK code the game asks about once so we know it's being called. */
static uint32_t s_gak_seen_vk[4]; /* 128 bits, one per VK 0..127 */
static void gak_log_once(unsigned int vk) {
    unsigned idx = vk & 0x7Fu;
    uint32_t bit = 1u << (idx & 31u);
    if (!(s_gak_seen_vk[idx >> 5] & bit)) {
        s_gak_seen_vk[idx >> 5] |= bit;
        ff7_boot_log("[gak] first poll: vk=0x%02x", vk);
    }
}

int ff7_GetAsyncKeyState_hook(unsigned int vk) {
    gak_log_once(vk);

    uint32_t sce = vk_to_sce(vk);
    if (!sce)
        return 0;

    const uint32_t b = ff7_pad_combo(&s_ff7_pad);
    if (!(b & sce))
        return 0;           /* key is up */

    int newly = gak_edge_consume(vk);
    return newly ? 0x8001 : 0x8000;
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

static volatile int s_gds_call_count = 0;

int ff7_GetDeviceState_hook(unsigned int self, unsigned int cbData, unsigned int lpvData) {
    (void)self;
    /* Log only the first few calls so we can see what cbData / lpvData look like
     * without flooding the log. Remove once stable. */
    if (s_gds_call_count < 5) {
        ff7_boot_log("[gds] call #%d: cbData=%u lpvData=0x%08x", s_gds_call_count, cbData, lpvData);
        s_gds_call_count++;
    }
    /* Only write when cbData matches a known DIJOYSTATE (80 bytes) or
     * DIJOYSTATE2 (176 bytes) size.  Any other size means the call is an
     * init-time probe or has an uninitialised buffer pointer — skip it. */
    if (cbData != 80u && cbData != 176u)
        return 0; /* DI_OK — empty state */
    ff7_overlay_dijoystate((void *)(uintptr_t)lpvData, cbData);
    return 0; /* DI_OK */
}
