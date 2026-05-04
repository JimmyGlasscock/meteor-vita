/*
 * ff7_opensl_impl.c — SceAudio-backed OpenSL ES reimplementation.
 *
 * Design:
 *  - All SL object types (Engine, OutputMix, AudioPlayer) are heap-allocated
 *    structs whose first member is a pointer to a statically-defined vtable.
 *    Taking the address of that first member gives a valid SLObjectItf.
 *  - The vtable structs are defined here with Android NDK slot ordering.
 *    The ONLY interface whose ordering differs between Android NDK and VitaSDK
 *    is SLObjectItf_; all others (SLPlayItf_, SLVolumeItf_, etc.) match.
 *  - We define our own vtable typedef structs (vita_*_vtable_t) to avoid
 *    using the raw `struct SL*Itf_` syntax (which is opaque in C without
 *    the struct keyword).  They are binary-compatible with the SL types.
 *  - Audio output uses SceAudio, written from a per-player background thread
 *    that drains each queued buffer in FF7_OPENSL_GRAIN-sample chunks.
 */

#include "ff7_opensl_impl.h"
#include <SLES/OpenSLES_Android.h>

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include <stdlib.h>
#include <string.h>
#include <stddef.h>   /* offsetof */

#include "../utils/logger.h"

/* -------------------------------------------------------------------------
 * tunables
 * ---------------------------------------------------------------------- */
#define FF7_OPENSL_MAX_PLAYERS  8
#define FF7_OPENSL_GRAIN        512   /* samples per SceAudio output block */
#define FF7_OPENSL_MAX_VOL      SCE_AUDIO_VOLUME_0DB

/* =========================================================================
 * Vtable structs — use void* for self to avoid C struct-tag issues.
 *
 * For each interface the self pointer passed by the game is the address of
 * the `*_vtab` field inside vita_player_t (or vita_engine_t etc.).
 * We recover the parent struct via offsetof arithmetic.
 * ====================================================================== */

/* --- Standard OpenSL ES 1.0.1 SLObjectItf_ ordering.
 *
 * Both the Android NDK and the VitaSDK ship the standard ordering.
 * An earlier revision of this file had Destroy at slot 3 and GetInterface
 * at slot 9 based on incorrect information — that caused the game to call
 * Destroy() every time it tried to call GetInterface().
 *
 * Correct standard ordering (verified against both SDK headers):
 *   0 Realize  1 Resume  2 GetState  3 GetInterface  4 RegisterCallback
 *   5 AbortAsyncOperation  6 Destroy  7 SetPriority  8 GetPriority
 *   9 SetLossOfControlInterfaces
 */
typedef struct android_obj_vtable_s {
    SLresult (*Realize)(void *self, SLboolean async);           /* 0 */
    SLresult (*Resume)(void *self, SLboolean async);            /* 1 */
    SLresult (*GetState)(void *self, SLuint32 *pState);         /* 2 */
    SLresult (*GetInterface)(void *self, const SLInterfaceID, void *); /* 3 */
    SLresult (*RegisterCallback)(void *self, slObjectCallback, void *); /* 4 */
    void     (*AbortAsyncOperation)(void *self);                /* 5 */
    void     (*Destroy)(void *self);                            /* 6 */
    SLresult (*SetPriority)(void *self, SLint32, SLboolean);    /* 7 */
    SLresult (*GetPriority)(void *self, SLint32 *, SLboolean *);/* 8 */
    SLresult (*SetLossOfControlInterfaces)(void *self, SLint16, SLInterfaceID *, SLboolean); /* 9 */
} android_obj_vtable_t;

/* --- SLPlayItf (matches both Android NDK and VitaSDK slot ordering) ----- */
typedef struct vita_play_vtable_s {
    SLresult (*SetPlayState)(void *self, SLuint32 state);
    SLresult (*GetPlayState)(void *self, SLuint32 *pState);
    SLresult (*GetDuration)(void *self, SLmillisecond *pMsec);
    SLresult (*GetPosition)(void *self, SLmillisecond *pMsec);
    SLresult (*RegisterCallback)(void *self, void *cb, void *ctx);
    SLresult (*SetCallbackEventsMask)(void *self, SLuint32 flags);
    SLresult (*GetCallbackEventsMask)(void *self, SLuint32 *pFlags);
    SLresult (*SetMarkerPosition)(void *self, SLmillisecond ms);
    SLresult (*ClearMarkerPosition)(void *self);
    SLresult (*GetMarkerPosition)(void *self, SLmillisecond *pMs);
    SLresult (*SetPositionUpdatePeriod)(void *self, SLmillisecond ms);
    SLresult (*GetPositionUpdatePeriod)(void *self, SLmillisecond *pMs);
} vita_play_vtable_t;

/* --- SLVolumeItf (same slot order in Android NDK and VitaSDK) ----------- */
typedef struct vita_vol_vtable_s {
    SLresult (*SetVolumeLevel)(void *self, SLmillibel level);
    SLresult (*GetVolumeLevel)(void *self, SLmillibel *pLevel);
    SLresult (*GetMaxVolumeLevel)(void *self, SLmillibel *pMax);
    SLresult (*SetMute)(void *self, SLboolean mute);
    SLresult (*GetMute)(void *self, SLboolean *pMute);
    SLresult (*EnableStereoPosition)(void *self, SLboolean enable);
    SLresult (*IsEnabledStereoPosition)(void *self, SLboolean *pEnable);
    SLresult (*SetStereoPosition)(void *self, SLpermille pos);
    SLresult (*GetStereoPosition)(void *self, SLpermille *pPos);
} vita_vol_vtable_t;

/* --- SLAndroidSimpleBufferQueueItf --------------------------------------- */
typedef struct vita_bq_vtable_s {
    SLresult (*Enqueue)(void *self, const void *pBuf, SLuint32 size);
    SLresult (*Clear)(void *self);
    SLresult (*GetState)(void *self, SLAndroidSimpleBufferQueueState *pState);
    SLresult (*RegisterCallback)(void *self, slAndroidSimpleBufferQueueCallback, void *ctx);
} vita_bq_vtable_t;

/* --- SLEngineItf (same slot order in Android NDK and VitaSDK) ----------- */
typedef struct vita_eng_vtable_s {
    SLresult (*CreateLEDDevice)(void *s, void *p, SLuint32 id, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*CreateVibraDevice)(void *s, void *p, SLuint32 id, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*CreateAudioPlayer)(void *s, SLObjectItf *pp, SLDataSource *src, SLDataSink *snk, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*CreateAudioRecorder)(void *s, void *p, void *src, void *snk, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*CreateMidiPlayer)(void *s, void *p, void *ms, void *bs, void *ao, void *vi, void *le, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*CreateListener)(void *s, void *p, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*Create3DGroup)(void *s, void *p, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*CreateOutputMix)(void *s, SLObjectItf *pp, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*CreateMetadataExtractor)(void *s, void *p, void *ds, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*CreateExtensionObject)(void *s, void *p, void *params, SLuint32 id, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req);
    SLresult (*QueryNumSupportedInterfaces)(void *s, SLuint32 objectId, SLuint32 *pNum);
    SLresult (*QuerySupportedInterfaces)(void *s, SLuint32 objectId, SLuint32 idx, SLInterfaceID *pIid);
    SLresult (*QueryNumSupportedExtensions)(void *s, SLuint32 *pNum);
    SLresult (*QuerySupportedExtension)(void *s, SLuint32 idx, SLchar *pName, SLint16 *pLen);
    SLresult (*IsExtensionSupported)(void *s, const SLchar *name, SLboolean *pSupported);
} vita_eng_vtable_t;

/* =========================================================================
 * Forward declarations
 * ====================================================================== */
typedef struct vita_engine_s  vita_engine_t;
typedef struct vita_outmix_s  vita_outmix_t;
typedef struct vita_player_s  vita_player_t;

/* =========================================================================
 * Buffer queue — double-buffered ring
 * ====================================================================== */
#define PLAYER_QUEUE_DEPTH 4

typedef struct {
    void    *data;
    SLuint32 size;
} sl_buf_entry_t;

/* =========================================================================
 * vita_player_t — backed by one SceAudio output port + a render thread
 * ====================================================================== */
struct vita_player_s {
    /* vtable pointers — MUST come first in this exact order */
    const android_obj_vtable_t  *obj_vtab;   /* SLObjectItf    */
    const vita_play_vtable_t    *play_vtab;  /* SLPlayItf      */
    const vita_bq_vtable_t      *bq_vtab;   /* SLAndroidSimpleBufferQueueItf */
    const vita_vol_vtable_t     *vol_vtab;   /* SLVolumeItf    */

    /* audio format (from SLDataFormat_PCM on creation) */
    SLuint32 sample_rate_mHz;
    SLuint32 channels;
    SLuint32 bits_per_sample;

    /* SceAudio port */
    int sce_port;                /* <0 = not open */

    /* play state */
    SLuint32  play_state;        /* SL_PLAYSTATE_* */
    int       volume_db;         /* millibels, 0 = max */

    /* buffer queue */
    sl_buf_entry_t  queue[PLAYER_QUEUE_DEPTH];
    int             q_head;
    int             q_tail;
    int             q_count;
    SceUID          q_mutex;
    SceUID          q_sema;

    slAndroidSimpleBufferQueueCallback bq_callback;
    void                              *bq_callback_ctx;

    /* playback thread */
    SceUID  thread_id;
    int     thread_running;
};

/* =========================================================================
 * vita_outmix_t
 * ====================================================================== */
struct vita_outmix_s {
    const android_obj_vtable_t *obj_vtab;
};

/* =========================================================================
 * vita_engine_t
 * ====================================================================== */
struct vita_engine_s {
    const android_obj_vtable_t *obj_vtab;
    const vita_eng_vtable_t    *eng_vtab;
};

/* =========================================================================
 * Utility
 * ====================================================================== */
static int sl_uuid_eq(const SLInterfaceID a, const SLInterfaceID b)
{
    if (a == b) return 1;
    return (a->time_low == b->time_low &&
            a->time_mid == b->time_mid &&
            a->time_hi_and_version == b->time_hi_and_version &&
            a->clock_seq == b->clock_seq &&
            memcmp(a->node, b->node, 6) == 0);
}

/* =========================================================================
 * Common SLObjectItf_ vtable helpers (shared, non-type-specific)
 * ====================================================================== */
static SLresult common_Realize(void *self, SLboolean async)                         { (void)self; (void)async; return SL_RESULT_SUCCESS; }
static SLresult common_Resume(void *self, SLboolean async)                          { (void)self; (void)async; return SL_RESULT_SUCCESS; }
static SLresult common_GetState(void *self, SLuint32 *pState)                       { (void)self; if (pState) *pState = SL_OBJECT_STATE_REALIZED; return SL_RESULT_SUCCESS; }
static SLresult common_SetPriority(void *self, SLint32 p, SLboolean pr)             { (void)self; (void)p; (void)pr; return SL_RESULT_SUCCESS; }
static SLresult common_GetPriority(void *self, SLint32 *p, SLboolean *pr)           { (void)self; if (p) *p = 0; if (pr) *pr = SL_BOOLEAN_FALSE; return SL_RESULT_SUCCESS; }
static SLresult common_SetLossOfControl(void *self, SLint16 n, SLInterfaceID *ids, SLboolean en) { (void)self; (void)n; (void)ids; (void)en; return SL_RESULT_SUCCESS; }
static SLresult common_RegisterCallback(void *self, slObjectCallback cb, void *ctx) { (void)self; (void)cb; (void)ctx; return SL_RESULT_SUCCESS; }
static void     common_AbortAsync(void *self)                                       { (void)self; }

/* =========================================================================
 * SLPlayItf_ implementation
 * ====================================================================== */
static vita_player_t *player_from_play(void *self) {
    return (vita_player_t *)((char *)self - offsetof(vita_player_t, play_vtab));
}

static SLresult play_SetPlayState(void *self, SLuint32 state) {
    player_from_play(self)->play_state = state;
    return SL_RESULT_SUCCESS;
}
static SLresult play_GetPlayState(void *self, SLuint32 *pState) {
    if (pState) *pState = player_from_play(self)->play_state;
    return SL_RESULT_SUCCESS;
}
static SLresult play_GetDuration(void *self, SLmillisecond *pMs)   { (void)self; if (pMs) *pMs = SL_TIME_UNKNOWN; return SL_RESULT_SUCCESS; }
static SLresult play_GetPosition(void *self, SLmillisecond *pMs)   { (void)self; if (pMs) *pMs = 0; return SL_RESULT_SUCCESS; }
static SLresult play_RegisterCallback(void *self, void *cb, void *ctx) { (void)self; (void)cb; (void)ctx; return SL_RESULT_SUCCESS; }
static SLresult play_SetCallbackEventsMask(void *self, SLuint32 f) { (void)self; (void)f; return SL_RESULT_SUCCESS; }
static SLresult play_GetCallbackEventsMask(void *self, SLuint32 *f){ (void)self; if (f) *f = 0; return SL_RESULT_SUCCESS; }
static SLresult play_SetMarkerPos(void *self, SLmillisecond ms)    { (void)self; (void)ms; return SL_RESULT_SUCCESS; }
static SLresult play_ClearMarkerPos(void *self)                     { (void)self; return SL_RESULT_SUCCESS; }
static SLresult play_GetMarkerPos(void *self, SLmillisecond *ms)   { (void)self; if (ms) *ms = 0; return SL_RESULT_SUCCESS; }
static SLresult play_SetPosUpdatePeriod(void *self, SLmillisecond ms){ (void)self; (void)ms; return SL_RESULT_SUCCESS; }
static SLresult play_GetPosUpdatePeriod(void *self, SLmillisecond *ms){ (void)self; if (ms) *ms = 0; return SL_RESULT_SUCCESS; }

static const vita_play_vtable_t g_play_vtab = {
    play_SetPlayState, play_GetPlayState,
    play_GetDuration,  play_GetPosition,
    play_RegisterCallback, play_SetCallbackEventsMask, play_GetCallbackEventsMask,
    play_SetMarkerPos, play_ClearMarkerPos, play_GetMarkerPos,
    play_SetPosUpdatePeriod, play_GetPosUpdatePeriod,
};

/* =========================================================================
 * SLVolumeItf_ implementation
 * ====================================================================== */
#define SL_VOL_MIN ((SLmillibel)-9600)
#define SL_VOL_MAX ((SLmillibel)0)

static int sl_vol_to_sce(SLmillibel mb) {
    if (mb <= SL_VOL_MIN) return 0;
    if (mb >= SL_VOL_MAX) return FF7_OPENSL_MAX_VOL;
    return (int)(((float)(mb - SL_VOL_MIN) / (float)(-SL_VOL_MIN)) * (float)FF7_OPENSL_MAX_VOL);
}

static vita_player_t *player_from_vol(void *self) {
    return (vita_player_t *)((char *)self - offsetof(vita_player_t, vol_vtab));
}

static SLresult vol_SetVolumeLevel(void *self, SLmillibel level) {
    vita_player_t *p = player_from_vol(self);
    p->volume_db = (int)level;
    if (p->sce_port >= 0) {
        int vols[2] = { sl_vol_to_sce(level), sl_vol_to_sce(level) };
        sceAudioOutSetVolume(p->sce_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
    }
    return SL_RESULT_SUCCESS;
}
static SLresult vol_GetVolumeLevel(void *self, SLmillibel *pLevel) {
    if (pLevel) *pLevel = (SLmillibel)player_from_vol(self)->volume_db;
    return SL_RESULT_SUCCESS;
}
static SLresult vol_GetMaxVolumeLevel(void *self, SLmillibel *pMax) { (void)self; if (pMax) *pMax = SL_VOL_MAX; return SL_RESULT_SUCCESS; }
static SLresult vol_SetMute(void *self, SLboolean mute) {
    vita_player_t *p = player_from_vol(self);
    if (p->sce_port >= 0) {
        int v = mute ? 0 : sl_vol_to_sce((SLmillibel)p->volume_db);
        int vols[2] = { v, v };
        sceAudioOutSetVolume(p->sce_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
    }
    return SL_RESULT_SUCCESS;
}
static SLresult vol_GetMute(void *self, SLboolean *m)             { (void)self; if (m) *m = SL_BOOLEAN_FALSE; return SL_RESULT_SUCCESS; }
static SLresult vol_EnableStereoPos(void *self, SLboolean e)      { (void)self; (void)e; return SL_RESULT_SUCCESS; }
static SLresult vol_IsEnabledStereoPos(void *self, SLboolean *e)  { (void)self; if (e) *e = SL_BOOLEAN_FALSE; return SL_RESULT_SUCCESS; }
static SLresult vol_SetStereoPos(void *self, SLpermille pos)      { (void)self; (void)pos; return SL_RESULT_SUCCESS; }
static SLresult vol_GetStereoPos(void *self, SLpermille *pos)     { (void)self; if (pos) *pos = 0; return SL_RESULT_SUCCESS; }

static const vita_vol_vtable_t g_vol_vtab = {
    vol_SetVolumeLevel, vol_GetVolumeLevel, vol_GetMaxVolumeLevel,
    vol_SetMute, vol_GetMute,
    vol_EnableStereoPos, vol_IsEnabledStereoPos,
    vol_SetStereoPos, vol_GetStereoPos,
};

/* =========================================================================
 * SLAndroidSimpleBufferQueueItf_ implementation
 * ====================================================================== */
static vita_player_t *player_from_bq(void *self) {
    return (vita_player_t *)((char *)self - offsetof(vita_player_t, bq_vtab));
}

static SLresult bq_Enqueue(void *self, const void *pBuffer, SLuint32 size) {
    vita_player_t *p = player_from_bq(self);
    sceKernelLockMutex(p->q_mutex, 1, NULL);
    if (p->q_count >= PLAYER_QUEUE_DEPTH) {
        sceKernelUnlockMutex(p->q_mutex, 1);
        l_warn("[opensl] bq_Enqueue: queue full, dropping buffer");
        return SL_RESULT_BUFFER_INSUFFICIENT;
    }
    void *buf = malloc(size);
    if (!buf) { sceKernelUnlockMutex(p->q_mutex, 1); return SL_RESULT_MEMORY_FAILURE; }
    memcpy(buf, pBuffer, size);
    p->queue[p->q_tail].data = buf;
    p->queue[p->q_tail].size = size;
    p->q_tail = (p->q_tail + 1) % PLAYER_QUEUE_DEPTH;
    p->q_count++;
    sceKernelUnlockMutex(p->q_mutex, 1);
    sceKernelSignalSema(p->q_sema, 1);
    return SL_RESULT_SUCCESS;
}

static SLresult bq_Clear(void *self) {
    vita_player_t *p = player_from_bq(self);
    sceKernelLockMutex(p->q_mutex, 1, NULL);
    while (p->q_count > 0) {
        free(p->queue[p->q_head].data);
        p->queue[p->q_head].data = NULL;
        p->q_head = (p->q_head + 1) % PLAYER_QUEUE_DEPTH;
        p->q_count--;
    }
    p->q_head = p->q_tail = 0;
    sceKernelUnlockMutex(p->q_mutex, 1);
    return SL_RESULT_SUCCESS;
}

static SLresult bq_GetState(void *self, SLAndroidSimpleBufferQueueState *pState) {
    vita_player_t *p = player_from_bq(self);
    if (pState) {
        sceKernelLockMutex(p->q_mutex, 1, NULL);
        pState->count = (SLuint32)p->q_count;
        pState->index = (SLuint32)p->q_head;
        sceKernelUnlockMutex(p->q_mutex, 1);
    }
    return SL_RESULT_SUCCESS;
}

static SLresult bq_RegisterCallback(void *self,
                                     slAndroidSimpleBufferQueueCallback cb,
                                     void *ctx) {
    vita_player_t *p = player_from_bq(self);
    p->bq_callback     = cb;
    p->bq_callback_ctx = ctx;
    return SL_RESULT_SUCCESS;
}

static const vita_bq_vtable_t g_bq_vtab = {
    bq_Enqueue, bq_Clear, bq_GetState, bq_RegisterCallback,
};

/* =========================================================================
 * Playback thread — drains buffer queue, submits to SceAudio
 * ====================================================================== */
static int player_thread(SceSize args, void *argp)
{
    vita_player_t *p       = *(vita_player_t **)argp;
    int16_t  grain_buf[FF7_OPENSL_GRAIN * 2];   /* always stereo for SceAudio */
    int      channels      = (int)p->channels;
    uint32_t grain_samples = (uint32_t)(FF7_OPENSL_GRAIN * channels);

    while (p->thread_running) {
        sceKernelWaitSema(p->q_sema, 1, NULL);
        if (!p->thread_running) break;
        if (p->play_state != SL_PLAYSTATE_PLAYING) continue;

        sceKernelLockMutex(p->q_mutex, 1, NULL);
        if (p->q_count == 0) {
            sceKernelUnlockMutex(p->q_mutex, 1);
            continue;
        }
        sl_buf_entry_t entry = p->queue[p->q_head];
        p->queue[p->q_head].data = NULL;
        p->q_head = (p->q_head + 1) % PLAYER_QUEUE_DEPTH;
        p->q_count--;
        sceKernelUnlockMutex(p->q_mutex, 1);

        const int16_t *src     = (const int16_t *)entry.data;
        uint32_t total_smp     = entry.size / sizeof(int16_t);
        uint32_t consumed      = 0;

        while (consumed < total_smp && p->thread_running) {
            uint32_t avail = total_smp - consumed;
            uint32_t chunk = avail < grain_samples ? avail : grain_samples;

            if (channels == 1) {
                uint32_t i;
                for (i = 0; i < chunk; i++) {
                    grain_buf[i * 2]     = src[consumed + i];
                    grain_buf[i * 2 + 1] = src[consumed + i];
                }
                for (; i < (uint32_t)FF7_OPENSL_GRAIN; i++) {
                    grain_buf[i * 2]     = 0;
                    grain_buf[i * 2 + 1] = 0;
                }
                sceAudioOutOutput(p->sce_port, grain_buf);
            } else {
                if (chunk < grain_samples) {
                    memcpy(grain_buf, src + consumed, chunk * sizeof(int16_t));
                    memset(grain_buf + chunk, 0, (grain_samples - chunk) * sizeof(int16_t));
                    sceAudioOutOutput(p->sce_port, grain_buf);
                } else {
                    sceAudioOutOutput(p->sce_port, (void *)(src + consumed));
                }
            }
            consumed += chunk;
        }

        free(entry.data);

        if (p->bq_callback)
            p->bq_callback((SLAndroidSimpleBufferQueueItf)&p->bq_vtab,
                           p->bq_callback_ctx);
    }

    sceKernelExitThread(0);
    return 0;
}

/* =========================================================================
 * Player SLObjectItf_ GetInterface / Destroy
 * ====================================================================== */
static SLresult player_GetInterface(void *self, const SLInterfaceID iid, void *pInterface)
{
    vita_player_t *p  = (vita_player_t *)self;
    void         **out = (void **)pInterface;
    if (!out) return SL_RESULT_PARAMETER_INVALID;

    if (sl_uuid_eq(iid, SL_IID_PLAY)) { *out = (void *)&p->play_vtab; return SL_RESULT_SUCCESS; }
    if (sl_uuid_eq(iid, SL_IID_ANDROIDSIMPLEBUFFERQUEUE) ||
        sl_uuid_eq(iid, SL_IID_BUFFERQUEUE)) { *out = (void *)&p->bq_vtab; return SL_RESULT_SUCCESS; }
    if (sl_uuid_eq(iid, SL_IID_VOLUME)) { *out = (void *)&p->vol_vtab; return SL_RESULT_SUCCESS; }

    l_warn("[opensl] player GetInterface: unsupported IID {%08x}", iid->time_low);
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

static void player_Destroy(void *self)
{
    vita_player_t *p = (vita_player_t *)self;
    p->thread_running = 0;
    sceKernelSignalSema(p->q_sema, 1);
    if (p->thread_id > 0) {
        sceKernelWaitThreadEnd(p->thread_id, NULL, NULL);
        sceKernelDeleteThread(p->thread_id);
    }
    bq_Clear((void *)&p->bq_vtab);
    if (p->sce_port >= 0) sceAudioOutReleasePort(p->sce_port);
    sceKernelDeleteSema(p->q_sema);
    sceKernelDeleteMutex(p->q_mutex);
    free(p);
}

static const android_obj_vtable_t g_player_obj_vtab = {
    common_Realize, common_Resume, common_GetState,
    player_GetInterface,              /* slot 3 — standard */
    common_RegisterCallback, common_AbortAsync,
    player_Destroy,                   /* slot 6 — standard */
    common_SetPriority, common_GetPriority, common_SetLossOfControl,
};

/* =========================================================================
 * OutputMix SLObjectItf_ GetInterface / Destroy
 * ====================================================================== */
static SLresult outmix_GetInterface(void *self, const SLInterfaceID iid, void *pInterface)
{
    (void)self; (void)pInterface;
    l_warn("[opensl] OutputMix GetInterface: unsupported IID {%08x}", iid->time_low);
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

static void outmix_Destroy(void *self) { free((vita_outmix_t *)self); }

static const android_obj_vtable_t g_outmix_obj_vtab = {
    common_Realize, common_Resume, common_GetState,
    outmix_GetInterface,              /* slot 3 — standard */
    common_RegisterCallback, common_AbortAsync,
    outmix_Destroy,                   /* slot 6 — standard */
    common_SetPriority, common_GetPriority, common_SetLossOfControl,
};

/* =========================================================================
 * SLEngineItf_ implementation
 * ====================================================================== */
static SLresult engine_CreateAudioPlayer(void *self,
                                          SLObjectItf    *pPlayer,
                                          SLDataSource   *pAudioSrc,
                                          SLDataSink     *pAudioSnk,
                                          SLuint32        numInterfaces,
                                          const SLInterfaceID *pInterfaceIds,
                                          const SLboolean     *pInterfaceRequired)
{
    (void)self; (void)pAudioSnk; (void)numInterfaces;
    (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pPlayer || !pAudioSrc) return SL_RESULT_PARAMETER_INVALID;

    SLuint32 sample_rate_mHz = 44100000;
    SLuint32 channels        = 2;
    SLuint32 bps             = 16;

    if (pAudioSrc->pFormat) {
        SLuint32 fmt_type = *(SLuint32 *)pAudioSrc->pFormat;
        if (fmt_type == SL_DATAFORMAT_PCM) {
            SLDataFormat_PCM *pcm = (SLDataFormat_PCM *)pAudioSrc->pFormat;
            sample_rate_mHz = pcm->samplesPerSec;
            channels        = pcm->numChannels;
            bps             = pcm->bitsPerSample;
        }
    }
    if (bps != 16 && bps != 8) { l_warn("[opensl] CreateAudioPlayer: bps=%u unsupported, using 16", bps); bps = 16; }

    int sample_rate_hz = (int)(sample_rate_mHz / 1000);
    int sce_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                       FF7_OPENSL_GRAIN,
                                       sample_rate_hz,
                                       SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (sce_port < 0) {
        l_warn("[opensl] BGM port failed (%08x), trying VOICE", sce_port);
        sce_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE,
                                       FF7_OPENSL_GRAIN,
                                       sample_rate_hz,
                                       SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    }

    vita_player_t *p = (vita_player_t *)calloc(1, sizeof(vita_player_t));
    if (!p) return SL_RESULT_MEMORY_FAILURE;

    p->obj_vtab        = &g_player_obj_vtab;
    p->play_vtab       = &g_play_vtab;
    p->bq_vtab         = &g_bq_vtab;
    p->vol_vtab        = &g_vol_vtab;
    p->sample_rate_mHz = sample_rate_mHz;
    p->channels        = channels;
    p->bits_per_sample = bps;
    p->sce_port        = sce_port;
    p->play_state      = SL_PLAYSTATE_STOPPED;
    p->volume_db       = (int)SL_VOL_MAX;

    p->q_mutex = sceKernelCreateMutex("sl_bq_mutex", 0, 0, NULL);
    p->q_sema  = sceKernelCreateSema("sl_bq_sema",  0, 0, PLAYER_QUEUE_DEPTH, NULL);

    if (sce_port >= 0) {
        int vols[2] = { FF7_OPENSL_MAX_VOL, FF7_OPENSL_MAX_VOL };
        sceAudioOutSetVolume(sce_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
    }

    p->thread_running = 1;
    p->thread_id = sceKernelCreateThread("sl_player",
                                          player_thread,
                                          0x10000100,
                                          0x10000,
                                          0, 0, NULL);
    if (p->thread_id > 0)
        sceKernelStartThread(p->thread_id, sizeof(vita_player_t *), &p);
    else
        l_warn("[opensl] CreateAudioPlayer: thread failed: %08x", p->thread_id);

    l_debug("[opensl] CreateAudioPlayer: port=%d rate=%d ch=%u bps=%u",
            sce_port, sample_rate_hz, channels, bps);

    *pPlayer = (SLObjectItf)&p->obj_vtab;
    return SL_RESULT_SUCCESS;
}

static SLresult engine_CreateOutputMix(void *self,
                                        SLObjectItf *pMix,
                                        SLuint32 numInterfaces,
                                        const SLInterfaceID *pInterfaceIds,
                                        const SLboolean     *pInterfaceRequired)
{
    (void)self; (void)numInterfaces; (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pMix) return SL_RESULT_PARAMETER_INVALID;
    vita_outmix_t *m = (vita_outmix_t *)calloc(1, sizeof(vita_outmix_t));
    if (!m) return SL_RESULT_MEMORY_FAILURE;
    m->obj_vtab = &g_outmix_obj_vtab;
    *pMix = (SLObjectItf)&m->obj_vtab;
    return SL_RESULT_SUCCESS;
}

static SLresult engine_stub_any(void *s, ...) { (void)s; return SL_RESULT_FEATURE_UNSUPPORTED; }
static SLresult engine_QueryNumIface(void *s, SLuint32 id, SLuint32 *n)    { (void)s; (void)id; if (n) *n = 0; return SL_RESULT_SUCCESS; }
static SLresult engine_QueryIface(void *s, SLuint32 id, SLuint32 idx, SLInterfaceID *iid) { (void)s; (void)id; (void)idx; (void)iid; return SL_RESULT_FEATURE_UNSUPPORTED; }
static SLresult engine_QueryNumExt(void *s, SLuint32 *n)                   { (void)s; if (n) *n = 0; return SL_RESULT_SUCCESS; }
static SLresult engine_QueryExt(void *s, SLuint32 i, SLchar *n, SLint16 *l){ (void)s; (void)i; (void)n; (void)l; return SL_RESULT_FEATURE_UNSUPPORTED; }
static SLresult engine_IsExtSupported(void *s, const SLchar *n, SLboolean *sup) { (void)s; (void)n; if (sup) *sup = SL_BOOLEAN_FALSE; return SL_RESULT_SUCCESS; }

static const vita_eng_vtable_t g_engine_vtab = {
    (SLresult (*)(void*,void*,SLuint32,SLuint32,const SLInterfaceID*,const SLboolean*))engine_stub_any, /* CreateLEDDevice   */
    (SLresult (*)(void*,void*,SLuint32,SLuint32,const SLInterfaceID*,const SLboolean*))engine_stub_any, /* CreateVibraDevice */
    engine_CreateAudioPlayer,
    (SLresult (*)(void*,void*,void*,void*,SLuint32,const SLInterfaceID*,const SLboolean*))engine_stub_any, /* CreateAudioRecorder */
    (SLresult (*)(void*,void*,void*,void*,void*,void*,void*,SLuint32,const SLInterfaceID*,const SLboolean*))engine_stub_any, /* CreateMidiPlayer */
    (SLresult (*)(void*,void*,SLuint32,const SLInterfaceID*,const SLboolean*))engine_stub_any, /* CreateListener */
    (SLresult (*)(void*,void*,SLuint32,const SLInterfaceID*,const SLboolean*))engine_stub_any, /* Create3DGroup */
    engine_CreateOutputMix,
    (SLresult (*)(void*,void*,void*,SLuint32,const SLInterfaceID*,const SLboolean*))engine_stub_any, /* CreateMetadataExtractor */
    (SLresult (*)(void*,void*,void*,SLuint32,SLuint32,const SLInterfaceID*,const SLboolean*))engine_stub_any, /* CreateExtensionObject */
    engine_QueryNumIface,
    engine_QueryIface,
    engine_QueryNumExt,
    engine_QueryExt,
    engine_IsExtSupported,
};

/* =========================================================================
 * Engine SLObjectItf_ GetInterface / Destroy
 * ====================================================================== */
static SLresult engine_GetInterface(void *self, const SLInterfaceID iid, void *pInterface)
{
    vita_engine_t *e  = (vita_engine_t *)self;
    void         **out = (void **)pInterface;
    if (!out) return SL_RESULT_PARAMETER_INVALID;

    if (sl_uuid_eq(iid, SL_IID_ENGINE)) {
        *out = (void *)&e->eng_vtab;
        return SL_RESULT_SUCCESS;
    }
    l_warn("[opensl] engine GetInterface: unsupported IID {%08x}", iid->time_low);
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

static void engine_Destroy(void *self) { free((vita_engine_t *)self); }

static const android_obj_vtable_t g_engine_obj_vtab = {
    common_Realize, common_Resume, common_GetState,
    engine_GetInterface,              /* slot 3 — standard */
    common_RegisterCallback, common_AbortAsync,
    engine_Destroy,                   /* slot 6 — standard */
    common_SetPriority, common_GetPriority, common_SetLossOfControl,
};

/* =========================================================================
 * Public entry point
 * ====================================================================== */
SLresult slCreateEngine_vita(SLObjectItf *pEngine,
                              SLuint32 numOptions,
                              const SLEngineOption *pEngineOptions,
                              SLuint32 numInterfaces,
                              const SLInterfaceID *pInterfaceIds,
                              const SLboolean *pInterfaceRequired)
{
    (void)numOptions; (void)pEngineOptions;
    (void)numInterfaces; (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pEngine) return SL_RESULT_PARAMETER_INVALID;

    vita_engine_t *e = (vita_engine_t *)calloc(1, sizeof(vita_engine_t));
    if (!e) return SL_RESULT_MEMORY_FAILURE;

    e->obj_vtab = &g_engine_obj_vtab;
    e->eng_vtab = &g_engine_vtab;
    *pEngine    = (SLObjectItf)&e->obj_vtab;

    l_debug("[opensl] slCreateEngine_vita: engine=%p", (void *)e);
    return SL_RESULT_SUCCESS;
}
