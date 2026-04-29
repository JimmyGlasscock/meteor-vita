/*
 * ff7_video_player.c — SceAvPlayer-backed FMV playback.
 *
 * See ff7_video_player.h for usage notes and the ffmpeg conversion command.
 *
 * Video frames arrive from SceAvPlayer in NV12 (Y plane + interleaved UV plane).
 * We convert them to RGBA on the CPU and upload via glTexImage2D.  For FF7's
 * movie resolutions (typically 640×360 or 854×480) this comfortably runs in
 * the budget of one render frame.
 */

#include "reimpl/ff7_video_player.h"
#include "utils/ff7_boot_log.h"

#include <psp2/avplayer.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>

#include <malloc.h>
#include <string.h>
#include <vitaGL.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static SceAvPlayerHandle s_player    = -1;
static int               s_active    = 0;
static uint32_t          s_total_ms  = 0;

/* RGBA conversion buffer — reallocated when video dimensions change. */
static uint8_t  *s_rgba     = NULL;
static uint32_t  s_rgba_w   = 0;
static uint32_t  s_rgba_h   = 0;

/* ------------------------------------------------------------------ */
/* Memory allocators required by SceAvPlayerInit                       */
/* ------------------------------------------------------------------ */

static void *avp_alloc(void *arg, uint32_t align, uint32_t size) {
    (void)arg;
    if (align < 4) align = 4;
    return memalign(align, size);
}
static void avp_free(void *arg, void *ptr) { (void)arg; free(ptr); }

/* Frame buffers are DMA-accessible; align to 64 bytes. */
static void *avp_alloc_frame(void *arg, uint32_t align, uint32_t size) {
    (void)arg;
    if (align < 64) align = 64;
    return memalign(align, size);
}
static void avp_free_frame(void *arg, void *ptr) { (void)arg; free(ptr); }

/* ------------------------------------------------------------------ */
/* NV12 → RGBA conversion (BT.601 limited-range, integer arithmetic)  */
/* ------------------------------------------------------------------ */

#define CLAMP8(x) ((unsigned)(x) > 255u ? ((x) < 0 ? 0 : 255) : (x))

/*
 * NV12 layout:
 *   bytes [0 .. stride*h)          — Y plane  (one byte per pixel)
 *   bytes [stride*h .. stride*h*3/2) — UV plane (interleaved U,V pairs,
 *                                      one pair per 2×2 pixel block)
 *
 * stride is typically (width + 15) & ~15 (aligned to 16 bytes).
 */
static void nv12_to_rgba(const uint8_t *nv12,
                          uint32_t w, uint32_t h, uint32_t stride,
                          uint8_t *rgba)
{
    const uint8_t *y_plane  = nv12;
    const uint8_t *uv_plane = nv12 + stride * h;

    for (uint32_t row = 0; row < h; row++) {
        const uint8_t *y_row  = y_plane  + row * stride;
        const uint8_t *uv_row = uv_plane + (row >> 1) * stride;
        uint8_t       *out    = rgba + row * w * 4;

        for (uint32_t col = 0; col < w; col++) {
            int Y = (int)y_row[col];
            int U = (int)uv_row[col & ~1u] - 128;
            int V = (int)uv_row[col |  1u] - 128;

            /* BT.601: R=Y+1.402V  G=Y-0.344U-0.714V  B=Y+1.772U  */
            int r = Y + ((V * 1436) >> 10);
            int g = Y - ((U * 352 + V * 731) >> 10);
            int b = Y + ((U * 1815) >> 10);

            out[col * 4 + 0] = (uint8_t)CLAMP8(r);
            out[col * 4 + 1] = (uint8_t)CLAMP8(g);
            out[col * 4 + 2] = (uint8_t)CLAMP8(b);
            out[col * 4 + 3] = 255;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void ff7_video_init(void) {
    int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if (ret < 0)
        ff7_boot_log("[video] sceSysmoduleLoadModule(AVPLAYER) 0x%x (may already be loaded)", ret);
    else
        ff7_boot_log("[video] SceAvPlayer sysmodule loaded");
}

int ff7_video_open(const char *path) {
    if (s_active) ff7_video_close();

    /* Build an MP4 path from the supplied path (WebM on Android → MP4 on Vita) */
    char mp4[512];
    strncpy(mp4, path, sizeof(mp4) - 5);
    mp4[sizeof(mp4) - 5] = '\0';

    char *dot = strrchr(mp4, '.');
    if (dot)
        strcpy(dot, ".mp4");
    else
        strncat(mp4, ".mp4", sizeof(mp4) - strlen(mp4) - 1);

    ff7_boot_log("[video] opening \"%s\"", mp4);

    /* Initialise the player ------------------------------------------ */
    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));

    init.memoryReplacement.allocate        = avp_alloc;
    init.memoryReplacement.deallocate      = avp_free;
    init.memoryReplacement.allocateTexture = avp_alloc_frame;
    init.memoryReplacement.deallocateTexture = avp_free_frame;

    init.basePriority              = 0xA0;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart                 = SCE_TRUE;

    s_player = sceAvPlayerInit(&init);
    if (s_player < 0) {
        ff7_boot_log("[video] sceAvPlayerInit FAIL 0x%x", s_player);
        s_player = -1;
        return 0;
    }

    int ret = sceAvPlayerAddSource(s_player, mp4);
    if (ret < 0) {
        ff7_boot_log("[video] sceAvPlayerAddSource FAIL 0x%x for \"%s\"", ret, mp4);
        sceAvPlayerClose(s_player);
        s_player = -1;
        return 0;
    }

    /* Query duration from stream 0 ------------------------------------ */
    SceAvPlayerStreamInfo si;
    memset(&si, 0, sizeof(si));
    if (sceAvPlayerGetStreamInfo(s_player, 0, &si) == 0)
        s_total_ms = (uint32_t)si.duration;
    else
        s_total_ms = 0;

    s_active = 1;
    ff7_boot_log("[video] playback started, total=%u ms", s_total_ms);
    return 1;
}

int ff7_video_next_frame(GLuint tex_id) {
    if (!s_active || s_player < 0) return 0;

    if (!sceAvPlayerIsActive(s_player)) {
        ff7_boot_log("[video] playback complete");
        s_active = 0;
        return 0;
    }

    SceAvPlayerFrameInfo info;
    memset(&info, 0, sizeof(info));
    if (!sceAvPlayerGetVideoData(s_player, &info)) {
        /* No new frame ready yet — player is still buffering; keep going. */
        return 1;
    }

    uint32_t w      = info.details.video.width;
    uint32_t h      = info.details.video.height;
    /* NV12 stride is typically width rounded up to 16-byte alignment. */
    uint32_t stride = (w + 15u) & ~15u;

    /* (Re)allocate RGBA buffer if dimensions changed */
    if (w != s_rgba_w || h != s_rgba_h) {
        free(s_rgba);
        s_rgba   = malloc((size_t)w * h * 4);
        s_rgba_w = w;
        s_rgba_h = h;
        ff7_boot_log("[video] frame %ux%u stride=%u", w, h, stride);
    }

    if (!s_rgba) { s_active = 0; return 0; }

    nv12_to_rgba(info.pData, w, h, stride, s_rgba);

    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 (GLsizei)w, (GLsizei)h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, s_rgba);

    return 1;
}

uint32_t ff7_video_total_ms(void) {
    return s_total_ms > 0 ? s_total_ms : 5000u;
}

uint32_t ff7_video_position_ms(void) {
    if (s_player < 0 || !s_active) return 0;
    return (uint32_t)sceAvPlayerCurrentTime(s_player);
}

void ff7_video_close(void) {
    if (s_player >= 0) {
        sceAvPlayerClose(s_player);
        s_player = -1;
    }
    free(s_rgba);
    s_rgba   = NULL;
    s_rgba_w = s_rgba_h = 0;
    s_active = 0;
    s_total_ms = 0;
}
