/*
 * ff7_video_player.c — SceAvPlayer-backed FMV playback.
 *
 * See ff7_video_player.h for usage notes and the ffmpeg conversion command.
 *
 * layton-vita loader/player.c — copied verbatim where the API allows:
 *   mem_alloc / mem_free (heap memalign),
 *   gpu_alloc / gpu_free (FB_ALIGNMENT 0x40000, ALIGN_MEM x2, PHYCONT_NC_RW, NULL opt,
 *   memblock name "Video Memblock", sceGxmMapMemory),
 *   video_audio_thread name "video_audio_thread", priority 0x10000100-10, stack 0x4000,
 *   SceAvPlayerInitData basePriority 175, numOutputVideoFrameBuffers 5, autoStart.
 * This binary also runs vitaGL before FMV: we call glFinish() before sceAvPlayerInit and
 * use a smaller vgl VRAM budget so PHYCONT gpu_alloc can succeed (Layton has no GL stack).
 *
 * Default chroma order matches Layton (NV21 / VU interleaved). Use
 * -DFF7_FMV_FORCE_NV12 for NV12, or -DFF7_FMV_I420 for planar I420 test builds.
 */

#include "reimpl/ff7_video_player.h"
#include "reimpl/ff7_gl_movie_tex.h"
#include "utils/ff7_boot_log.h"

#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/gxm.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/error.h>
#include <psp2common/kernel/sysmem.h>

_Static_assert(sizeof(SceAvPlayerInitData) == 0x48, "SceAvPlayerInitData size mismatch vs vitasdk");

#include <malloc.h>
#include <string.h>
#include <vitaGL.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static SceAvPlayerHandle s_player     = 0;
static int               s_avp_open   = 0; /* AddSource succeeded; handle may be negative int32 */
static int               s_active    = 0;
static uint32_t          s_total_ms  = 0;

/* layton-vita player.c: PLAYER_INACTIVE / PLAYER_ACTIVE for audio thread loop */
enum { FF7_VP_INACTIVE = 0, FF7_VP_ACTIVE = 1 };
static volatile int      s_vp_state   = FF7_VP_INACTIVE;
static SceUID            s_audio_thid = -1;

/* layton-vita video_audio_init */
static int s_ff7_audio_port = -1;

/* RGBA conversion buffer — reallocated when video dimensions change. */
static uint8_t  *s_rgba     = NULL;
static uint32_t  s_rgba_w   = 0;
static uint32_t  s_rgba_h   = 0;
static int               s_tex_need_full = 1;

/* Layton player.c: vglGetGxmTexture + sceGxmTextureInitLinear on decoder pData */
static SceGxmTexture *s_movie_gxm_tex = NULL;
static int            s_movie_gxm_ok  = 0; /* 1 = use GXM YUV path, 0 = RGBA upload */

/* Per-open debug: reset in ff7_video_open so logs stay readable per movie. */
static uint32_t          s_tex_alloc_seq   = 0;
static uint32_t          s_next_frame_poll = 0;
static uint32_t          s_next_frame_got  = 0;
static int               s_next_inact_log = 0;

/* ------------------------------------------------------------------ */
/* Memory allocators required by SceAvPlayerInit                       */
/* ------------------------------------------------------------------ */

static void *avp_alloc(void *arg, uint32_t align, uint32_t size) {
    (void)arg;
    if (align < 4) align = 4;
    return memalign(align, size);
}
static void avp_free(void *arg, void *ptr) { (void)arg; free(ptr); }

/*
 * Video frame buffers must live in contiguous PHYCONT memory mapped for GXM
 * (same as layton-vita loader/player.c gpu_alloc/gpu_free). Plain memalign
 * is not sufficient for decoder DMA surfaces.
 */
/* layton-vita player.c: FB_ALIGNMENT + ALIGN_MEM */
#define AVP_FB_ALIGNMENT 0x40000u
#define AVP_ALIGN_MEM(x, align) (((x) + ((align)-1u)) & ~((align)-1u))

/* Layton player.c uses numOutputVideoFrameBuffers = 5 */
#ifndef FF7_AVP_NUM_VIDEO_BUFFERS
#define FF7_AVP_NUM_VIDEO_BUFFERS 5
#endif

static void avp_log_free_mem(const char *why, SceUID err, uint32_t size, uint32_t align) {
    SceKernelFreeMemorySizeInfo mi;
    memset(&mi, 0, sizeof(mi));
    mi.size = sizeof(mi);
    int gr = sceKernelGetFreeMemorySize(&mi);
    const char *hint = "";
    switch ((uint32_t)err) {
    case SCE_KERNEL_ERROR_INVALID_ARGUMENT:
        hint = " INVALID_ARGUMENT";
        break;
    case SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE:
        hint = " NO_FREE_PHYSICAL_PAGE";
        break;
    case SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE_CDRAM:
        hint = " NO_FREE_PHYSICAL_PAGE_CDRAM";
        break;
    default:
        break;
    }
    if (gr == 0) {
        ff7_boot_log(
            "[video] %s err=0x%08x%s size=%u align=%u | freeKiB user=%d phycont=%d cdram=%d",
            why, (unsigned)(uint32_t)err, hint, size, align,
            mi.size_user / 1024, mi.size_phycont / 1024, mi.size_cdram / 1024);
    } else {
        ff7_boot_log("[video] %s err=0x%08x size=%u align=%u | GetFreeMemFAIL ret=0x%x",
                     why, (unsigned)(uint32_t)err, size, align, (unsigned)gr);
    }
}

static void ff7_video_log_mem_snapshot(const char *tag) {
    SceKernelFreeMemorySizeInfo mi;
    memset(&mi, 0, sizeof(mi));
    mi.size = sizeof(mi);
    int gr = sceKernelGetFreeMemorySize(&mi);
    if (gr == 0) {
        ff7_boot_log("[video] mem snapshot [%s]: user=%d phycont=%d cdram=%d (bytes)",
                     tag, mi.size_user, mi.size_phycont, mi.size_cdram);
    } else {
        ff7_boot_log("[video] mem snapshot [%s]: GetFreeMemorySize ret=0x%x", tag,
                     (unsigned)gr);
    }
}

/* layton-vita loader/player.c — gpu_alloc (verbatim) */
static void *avp_alloc_texture(void *arg, uint32_t align, uint32_t size) {
    (void)arg;
    (void)++s_tex_alloc_seq;

    if (align < AVP_FB_ALIGNMENT)
        align = AVP_FB_ALIGNMENT;
    size = AVP_ALIGN_MEM(size, align);
    size = AVP_ALIGN_MEM(size, 1024u * 1024u);

    SceUID memblock = sceKernelAllocMemBlock(
        "Video Memblock",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
        (SceSize)size,
        NULL);

    if (memblock < 0) {
        avp_log_free_mem("gpu_alloc (Layton)", memblock, size, align);
        return NULL;
    }

    void *res = NULL;
    if (sceKernelGetMemBlockBase(memblock, &res) < 0 || !res) {
        ff7_boot_log("[video] gpu_alloc GetMemBlockBase failed");
        sceKernelFreeMemBlock(memblock);
        return NULL;
    }

    if (sceGxmMapMemory(
            res, (SceSize)size,
            (SceGxmMemoryAttribFlags)(SCE_GXM_MEMORY_ATTRIB_READ |
                                       SCE_GXM_MEMORY_ATTRIB_WRITE)) < 0) {
        ff7_boot_log("[video] gpu_alloc sceGxmMapMemory failed");
        sceKernelFreeMemBlock(memblock);
        return NULL;
    }

    return res;
}

static void avp_free_texture(void *arg, void *ptr) {
    (void)arg;
    if (!ptr)
        return;

    glFinish();

    SceUID memblock = sceKernelFindMemBlockByAddr(ptr, 0);
    sceGxmUnmapMemory(ptr);
    sceKernelFreeMemBlock(memblock);
}

/* ------------------------------------------------------------------ */
/* layton-vita video_audio_init + video_audio_thread                  */
/* ------------------------------------------------------------------ */

static void ff7_video_set_volume_impl(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    if (s_ff7_audio_port < 0)
        return;
    int vols[2] = { (int)(vol * 32767.0f), (int)(vol * 32767.0f) };
    sceAudioOutSetVolume(s_ff7_audio_port,
                         SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH,
                         vols);
}

void ff7_video_set_volume(float vol) {
    ff7_video_set_volume_impl(vol);
}

void ff7_video_bind_movie_texture(GLuint tex_id) {
    s_movie_gxm_tex = NULL;
    s_movie_gxm_ok  = 0;
    if (tex_id == 0)
        return;

    /* layton-vita player.c video_open (first-time texture setup) */
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    s_movie_gxm_tex = vglGetGxmTexture(GL_TEXTURE_2D);
    if (!s_movie_gxm_tex) {
        ff7_boot_log("[video] Layton bind: vglGetGxmTexture(GL_TEXTURE_2D) NULL — RGBA path");
        return;
    }

    void *old = vglGetTexDataPointer(GL_TEXTURE_2D);
    if (old)
        vglFree(old);

    s_movie_gxm_ok = 1;
    ff7_boot_log("[video] Layton bind: tex=%u 8x8 placeholder + vglFree(texData) (video_open pattern)",
                 (unsigned)tex_id);
}

static void ff7_video_audio_port_init(void) {
    if (s_ff7_audio_port >= 0)
        return;

    s_ff7_audio_port = -1;
    for (int i = 0; i < 8; i++) {
        if (sceAudioOutGetConfig(i, SCE_AUDIO_OUT_CONFIG_TYPE_LEN) >= 0) {
            s_ff7_audio_port = i;
            ff7_boot_log("[video] audio: reusing existing port id=%i (Layton-style scan)",
                         s_ff7_audio_port);
            break;
        }
    }

    if (s_ff7_audio_port < 0) {
        s_ff7_audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000,
                                                SCE_AUDIO_OUT_MODE_STEREO);
        ff7_boot_log("[video] audio: opened new MAIN port id=%i ret_if_neg=error",
                     s_ff7_audio_port);
        ff7_video_set_volume_impl(1.0f);
    }
}

static int ff7_vp_audio_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    SceAvPlayerFrameInfo frame;

    while (s_vp_state != FF7_VP_INACTIVE && sceAvPlayerIsActive(s_player)) {
        memset(&frame, 0, sizeof(frame));
        if (sceAvPlayerGetAudioData(s_player, &frame)) {
            sceAudioOutSetConfig(
                s_ff7_audio_port, 1024,
                frame.details.audio.sampleRate,
                frame.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO
                                                      : SCE_AUDIO_OUT_MODE_STEREO);
            sceAudioOutOutput(s_ff7_audio_port, frame.pData);
        } else {
            sceKernelDelayThread(1000);
        }
    }

    return sceKernelExitDeleteThread(0);
}

/* ------------------------------------------------------------------ */
/* YUV → RGBA (SceAvPlayer / Layton YVU420P2 family)                    */
/* ------------------------------------------------------------------ */

#define CLAMP8(x) ((unsigned)(x) > 255u ? ((x) < 0 ? 0 : 255) : (x))

/*
 * Layton binds frames as SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1 — chroma is
 * 2-plane 4:2:0 with V then U in each UV pair (NV21), not NV12 (U then V).
 * Many public docs still say "NV12" for AvPlayer; that mismatch yields near-
 * black or wrong-color output while the rest of GL is fine.
 *
 * SceAvPlayerFrameInfo::reserved is often the Y-plane pitch in bytes on retail
 * (multiple of 16, >= width). When it is zero or implausible we fall back to
 * width rounded up to 16.
 */

static uint32_t ffmv_y_stride_from_info(const SceAvPlayerFrameInfo *fi, uint32_t w) {
    uint32_t r = fi->reserved;
    if (r == 0 || r < w || r > 8192u || (r & 15u) != 0u)
        return (w + 15u) & ~15u;
    return r;
}

/* Semi-planar 4:2:0: Y plane then interleaved chroma (same stride as Y). */
static void ffmv_semi420_to_rgba(const uint8_t *buf, uint32_t w, uint32_t h,
                                 uint32_t stride, int nv21, uint8_t *rgba) {
    const uint8_t *y_plane  = buf;
    const uint8_t *uv_plane = buf + stride * h;

    for (uint32_t row = 0; row < h; row++) {
        const uint8_t *y_row  = y_plane + row * stride;
        const uint8_t *uv_row = uv_plane + (row >> 1) * stride;
        uint8_t       *out    = rgba + row * w * 4;

        for (uint32_t col = 0; col < w; col++) {
            int Y = (int)y_row[col];
            int U, V;
            if (nv21) {
                /* NV21 / YVU420P2: V,U,V,U,... (Layton path) */
                V = (int)uv_row[col & ~1u] - 128;
                U = (int)uv_row[col | 1u] - 128;
            } else {
                /* NV12: U,V,U,V,... */
                U = (int)uv_row[col & ~1u] - 128;
                V = (int)uv_row[col | 1u] - 128;
            }

            /* BT.601 limited-range-ish (same fast integer kernel as before). */
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

/*
 * Planar I420: Y, then U, then V. Chroma row bytes often luma_stride/2 on Vita.
 * Build with -DFF7_FMV_I420=1 if semi-planar still looks wrong for your encodes.
 */
#ifdef FF7_FMV_I420
static void ffmv_i420_to_rgba(const uint8_t *buf, uint32_t w, uint32_t h,
                              uint32_t y_stride, uint8_t *rgba) {
    uint32_t c_stride = y_stride >> 1;
    if (c_stride < ((w + 1u) >> 1))
        c_stride = ((w + 1u) >> 1);

    const uint8_t *yp = buf;
    const uint8_t *up = yp + y_stride * h;
    const uint8_t *vp = up + c_stride * (h >> 1);

    for (uint32_t row = 0; row < h; row++) {
        const uint8_t *y_row = yp + row * y_stride;
        const uint8_t *u_row = up + (row >> 1) * c_stride;
        const uint8_t *v_row = vp + (row >> 1) * c_stride;
        uint8_t       *out   = rgba + row * w * 4;

        for (uint32_t col = 0; col < w; col++) {
            int Y = (int)y_row[col];
            int U = (int)u_row[col >> 1] - 128;
            int V = (int)v_row[col >> 1] - 128;

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
#endif

#ifndef FF7_FMV_FORCE_NV12
#define FF7_FMV_DEFAULT_NV21 1
#else
#define FF7_FMV_DEFAULT_NV21 0
#endif

static int s_ffmv_layout_logged;

static void ffmv_boot_log_first_frame(uint32_t w, uint32_t h, uint32_t reserved,
                                      uint32_t y_stride) {
    if (s_ffmv_layout_logged)
        return;
    s_ffmv_layout_logged = 1;
#ifdef FF7_FMV_I420
    ff7_boot_log("[video] first VideoData: %ux%u reserved=%u y_stride=%u mode=I420",
                 (unsigned)w, (unsigned)h, (unsigned)reserved, (unsigned)y_stride);
#else
    ff7_boot_log("[video] first VideoData: %ux%u reserved=%u y_stride=%u mode=%s",
                 (unsigned)w, (unsigned)h, (unsigned)reserved, (unsigned)y_stride,
                 FF7_FMV_DEFAULT_NV21 ? "NV21(YVU)" : "NV12(YUV)");
#endif
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/* AvPlayer returns opaque handles; do not use (handle < 0) as failure — see ff7_video_open. */
extern int sceAvPlayerPostInit(void);

void ff7_video_init(void) {
    int r;

    r = sceSysmoduleLoadModule(SCE_SYSMODULE_MP4);
    if (r < 0)
        ff7_boot_log("[video] sceSysmoduleLoadModule(MP4) 0x%x", r);

    r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC);
    if (r < 0)
        ff7_boot_log("[video] sceSysmoduleLoadModule(AVCDEC) 0x%x", r);

    r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if (r < 0)
        ff7_boot_log("[video] sceSysmoduleLoadModule(AVPLAYER) 0x%x (may already be loaded)", r);
    else
        ff7_boot_log("[video] SceAvPlayer sysmodule loaded");

    r = sceAvPlayerPostInit();
    if (r < 0)
        ff7_boot_log("[video] sceAvPlayerPostInit 0x%x (ignored if unused on this FW)", r);
}

static int avplayer_handle_is_documented_error(SceAvPlayerHandle h) {
    switch ((uint32_t)h) {
    case 0x806A0001u: /* SCE_AVPLAYER_ERROR_INVALID_PARAM */
    case 0x806A0003u: /* SCE_AVPLAYER_ERROR_OUT_OF_MEMORY */
    case 0u:
        return 1;
    default:
        return 0;
    }
}

int ff7_video_open(const char *path) {
    if (s_avp_open)
        ff7_video_close();

    s_tex_alloc_seq   = 0;
    s_next_frame_poll = 0;
    s_next_frame_got  = 0;
    s_next_inact_log  = 0;
    s_ffmv_layout_logged = 0;

    /* Build an MP4 path from the supplied path (WebM on Android → MP4 on Vita) */
    char mp4[512];
    strncpy(mp4, path, sizeof(mp4) - 5);
    mp4[sizeof(mp4) - 5] = '\0';

    char *dot = strrchr(mp4, '.');
    if (dot)
        strcpy(dot, ".mp4");
    else
        strncat(mp4, ".mp4", sizeof(mp4) - strlen(mp4) - 1);

    ff7_boot_log("[video] open: path=\"%s\"", mp4);
    ff7_video_log_mem_snapshot("before AvPlayerInit");
    /* Layton runs no GL before movie; drain pending GPU work before PHYCONT allocs. */
    glFinish();

    /* Initialise the player ------------------------------------------ */
    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));

    init.memoryReplacement.objectPointer     = NULL;
    init.memoryReplacement.allocate          = avp_alloc;
    init.memoryReplacement.deallocate        = avp_free;
    init.memoryReplacement.allocateTexture   = avp_alloc_texture;
    init.memoryReplacement.deallocateTexture = avp_free_texture;

    /* layton-vita player.c: numOutputVideoFrameBuffers = 5 */
    init.basePriority               = 175;
    init.numOutputVideoFrameBuffers = FF7_AVP_NUM_VIDEO_BUFFERS;
    init.autoStart                  = SCE_TRUE;

    ff7_boot_log(
        "[video] SceAvPlayerInitData: basePriority=%u numOutputVideoFrameBuffers=%i autoStart=%i "
        "(Layton player.c: 175, 5, 1)",
        (unsigned)init.basePriority, (int)init.numOutputVideoFrameBuffers,
        (int)init.autoStart);

    s_player = sceAvPlayerInit(&init);
    ff7_boot_log("[video] sceAvPlayerInit -> 0x%08x (opaque handle; do not treat as signed error)",
                 (unsigned)s_player);

    if (avplayer_handle_is_documented_error(s_player)) {
        ff7_boot_log("[video] sceAvPlayerInit returned documented fatal error — aborting open");
        return 0;
    }

    ff7_video_log_mem_snapshot("after AvPlayerInit");

    int ret = sceAvPlayerAddSource(s_player, mp4);
    ff7_boot_log("[video] sceAvPlayerAddSource(\"%s\") -> %i", mp4, ret);
    if (ret < 0) {
        ff7_boot_log("[video] AddSource failed — closing player");
        sceAvPlayerClose(s_player);
        s_player = 0;
        return 0;
    }

    s_avp_open = 1;

    /* layton-vita: drain audio in a side thread or video often never advances. */
    ff7_video_audio_port_init();
    s_vp_state = FF7_VP_ACTIVE;
    s_audio_thid = sceKernelCreateThread(
        "video_audio_thread", ff7_vp_audio_thread,
        0x10000100 - 10, 0x4000, 0, 0, NULL);
    if (s_audio_thid < 0) {
        ff7_boot_log("[video] sceKernelCreateThread(audio) uid=0x%08x FAIL",
                     (unsigned)(uint32_t)s_audio_thid);
        s_vp_state = FF7_VP_INACTIVE;
        sceAvPlayerClose(s_player);
        s_avp_open = 0;
        s_player   = 0;
        return 0;
    }
    sceKernelStartThread(s_audio_thid, 0, NULL);
    ff7_boot_log("[video] audio drain thread started uid=0x%08x (Layton: same priority stack)",
                 (unsigned)(uint32_t)s_audio_thid);

    /* layton-vita player.c: probe streams (Layton does not call GetStreamInfo in video_open). */
    s_total_ms = 0;
    for (int sid = 0; sid < 8; sid++) {
        SceAvPlayerStreamInfo st;
        memset(&st, 0, sizeof(st));
        int gr = sceAvPlayerGetStreamInfo(s_player, sid, &st);
        if (gr != 0)
            continue;
        ff7_boot_log("[video] GetStreamInfo(%i): type=%u duration=%llu ms startTime=%llu",
                      sid, (unsigned)st.type, (unsigned long long)st.duration,
                      (unsigned long long)st.startTime);
        if (st.type == SCE_AVPLAYER_VIDEO) {
            ff7_boot_log("[video]   VIDEO %ux%u aspect=%f",
                         (unsigned)st.details.video.width, (unsigned)st.details.video.height,
                         (double)st.details.video.aspectRatio);
            if (s_total_ms == 0 && st.duration != 0)
                s_total_ms = (uint32_t)st.duration;
        }
    }
    if (s_total_ms == 0)
        ff7_boot_log("[video] GetStreamInfo: no video duration found (using stub total in ff7_video_total_ms)");

    s_active         = 1;
    s_tex_need_full  = 1;
    ff7_boot_log("[video] open OK: IsActive=%i CurrentTime=%llu ms", (int)sceAvPlayerIsActive(s_player),
                 (unsigned long long)sceAvPlayerCurrentTime(s_player));
    ff7_video_log_mem_snapshot("after open complete");
    return 1;
}

int ff7_video_next_frame(GLuint tex_id) {
    if (!s_active || !s_avp_open) {
        if (s_next_inact_log < 4) {
            ff7_boot_log("[video] next_frame: inactive (active=%i avp_open=%i) call#%i -> 0",
                         s_active, s_avp_open, s_next_inact_log);
            s_next_inact_log++;
        }
        return 0;
    }

    if (!sceAvPlayerIsActive(s_player)) {
        ff7_boot_log("[video] next_frame: sceAvPlayerIsActive=0 playback ended -> return 0");
        s_active = 0;
        return 0;
    }

    SceAvPlayerFrameInfo info;
    memset(&info, 0, sizeof(info));
    if (!sceAvPlayerGetVideoData(s_player, &info)) {
        s_next_frame_poll++;
        if (s_next_frame_poll <= 5u || (s_next_frame_poll % 120u) == 0u)
            ff7_boot_log(
                "[video] next_frame: GetVideoData=false (no new YUV frame yet) poll#%u time=%llu",
                (unsigned)s_next_frame_poll,
                (unsigned long long)sceAvPlayerCurrentTime(s_player));
        return 1;
    }

    s_next_frame_poll = 0;
    s_next_frame_got++;

    if (!info.pData) {
        ff7_boot_log("[video] next_frame: GetVideoData=TRUE but pData=NULL (bug or EOS?) time=%llu",
                     (unsigned long long)info.timeStamp);
        return 1;
    }

    uint32_t w           = info.details.video.width;
    uint32_t h           = info.details.video.height;
    uint32_t y_stride    = ffmv_y_stride_from_info(&info, w);
    uint32_t stride_guess = (w + 15u) & ~15u;

    if (s_next_frame_got <= 5u || (s_next_frame_got % 30u) == 0u)
        ff7_boot_log(
            "[video] next_frame: VIDEO #%u pData=%p ts=%llu ms wxh=%ux%u reserved=%u y_stride=%u tex=%u",
            (unsigned)s_next_frame_got, info.pData,
            (unsigned long long)info.timeStamp, (unsigned)w, (unsigned)h,
            (unsigned)info.reserved, (unsigned)y_stride, (unsigned)tex_id);

    ffmv_boot_log_first_frame(w, h, (uint32_t)info.reserved, y_stride);

    glBindTexture(GL_TEXTURE_2D, tex_id);

    if (s_movie_gxm_ok && s_movie_gxm_tex) {
        /* layton-vita player.c video_get_frame — sceGxmTextureInitLinear on frame.pData */
        int r;
        if (y_stride != w)
            r = sceGxmTextureInitLinearStrided(
                s_movie_gxm_tex, info.pData, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
                w, h, y_stride);
        else
            r = sceGxmTextureInitLinear(
                s_movie_gxm_tex, info.pData, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
                w, h, 0);
        if (r < 0) {
            ff7_boot_log("[video] sceGxmTextureInit* ret=0x%x — RGBA fallback", (unsigned)r);
            s_movie_gxm_ok = 0;
            goto movie_rgba_upload;
        }
        sceGxmTextureSetMinFilter(s_movie_gxm_tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetMagFilter(s_movie_gxm_tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        s_tex_need_full = 0;
        return 1;
    }

movie_rgba_upload:
    /* (Re)allocate RGBA buffer if dimensions changed */
    if (w != s_rgba_w || h != s_rgba_h) {
        free(s_rgba);
        s_rgba   = malloc((size_t)w * h * 4);
        s_rgba_w = w;
        s_rgba_h = h;
        s_tex_need_full = 1;
        ff7_boot_log("[video] RGBA staging realloc %ux%u y_stride=%u (w+15=%u) rgba_bytes=%zu",
                     w, h, y_stride, stride_guess, (size_t)w * h * 4u);
    }

    if (!s_rgba) {
        ff7_boot_log("[video] next_frame: malloc(%zu) for RGBA failed — stop", (size_t)w * h * 4u);
        s_active = 0;
        return 0;
    }

#ifdef FF7_FMV_I420
    ffmv_i420_to_rgba(info.pData, w, h, y_stride, s_rgba);
#else
    ffmv_semi420_to_rgba(info.pData, w, h, y_stride, FF7_FMV_DEFAULT_NV21, s_rgba);
#endif

    ff7_gl_upload_rgba_movie_tex(tex_id, (int)w, (int)h, s_rgba, s_tex_need_full);
    s_tex_need_full = 0;

    return 1;
}

uint32_t ff7_video_total_ms(void) {
    return s_total_ms > 0 ? s_total_ms : 5000u;
}

uint32_t ff7_video_position_ms(void) {
    if (!s_avp_open || !s_active) return 0;
    return (uint32_t)sceAvPlayerCurrentTime(s_player);
}

void ff7_video_close(void) {
    ff7_boot_log("[video] close: begin (avp_open=%i active=%i)", s_avp_open, s_active);
    uint64_t tpos = 0;
    if (s_avp_open && s_player)
        tpos = sceAvPlayerCurrentTime(s_player);
    ff7_boot_log("[video] close: last CurrentTime=%llu ms", (unsigned long long)tpos);

    s_movie_gxm_tex = NULL;
    s_movie_gxm_ok  = 0;

    s_vp_state = FF7_VP_INACTIVE;

    if (s_audio_thid >= 0) {
        int st = 0;
        sceKernelWaitThreadEnd(s_audio_thid, &st, NULL);
        ff7_boot_log("[video] close: audio thread joined exit=0x%x", (unsigned)st);
        s_audio_thid = -1;
    }

    if (s_avp_open) {
        sceAvPlayerStop(s_player);
        sceAvPlayerClose(s_player);
        s_avp_open = 0;
        s_player   = 0;
        ff7_boot_log("[video] close: sceAvPlayerStop+Close done");
    }
    free(s_rgba);
    s_rgba   = NULL;
    s_rgba_w = s_rgba_h = 0;
    s_active = 0;
    s_total_ms = 0;
    s_tex_need_full = 1;
    ff7_video_log_mem_snapshot("after video close");
    ff7_boot_log("[video] close: end");
}
