/*
 * ff7_se_player.c — FF7 Vita sound-effect player.
 *
 * audio.fmt binary layout (little-endian):
 *
 *   uint32_t  count;              // number of sound effects
 *   struct {
 *       uint32_t offset;          // byte offset into audio.dat
 *       uint32_t size;            // byte length of PCM data
 *   } entries[count];
 *
 * audio.dat: raw PCM bank, entries accessed by [offset, offset+size).
 *
 * All PCM data: 16-bit signed little-endian, mono, 22050 Hz.
 * (These are the values documented for FF7 Android's sound bank.)
 *
 * If the actual format on-device differs, the init log will show the
 * parsed count and a sample of entry[0] so it can be adjusted.
 */

#include "reimpl/ff7_se_player.h"
#include "utils/ff7_boot_log.h"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define SE_SAMPLE_RATE   22050
#define SE_CHANNELS      1
/* SceAudio grain must be a multiple of 64; 512 samples @ 22050 Hz ≈ 23 ms */
#define SE_GRAIN         512
/* Maximum simultaneous SE voices — one SceAudio port each.            */
#define SE_MAX_VOICES    4
/* Maximum entries in audio.fmt we will allocate for.                  */
#define SE_MAX_ENTRIES   4096

/* ------------------------------------------------------------------ */
/* Audio.fmt entry                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t offset;
    uint32_t size;
} se_entry_t;

/* ------------------------------------------------------------------ */
/* Player state                                                        */
/* ------------------------------------------------------------------ */

static int         s_init       = 0;
static se_entry_t *s_entries    = NULL;
static uint32_t    s_count      = 0;
static uint8_t    *s_dat        = NULL;
static uint32_t    s_dat_size   = 0;
static int         s_volume     = SCE_AUDIO_VOLUME_0DB;

/* ------------------------------------------------------------------ */
/* Voice pool                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    SceUID   thread;
    int      port;
    uint8_t *buf;        /* copy of PCM data to play */
    uint32_t size;       /* byte length */
    int      busy;       /* 1 = playing */
} se_voice_t;

static se_voice_t s_voices[SE_MAX_VOICES];

/* ------------------------------------------------------------------ */
/* Playback thread                                                     */
/* ------------------------------------------------------------------ */

static int se_play_thread(SceSize args, void *argp) {
    int voice_idx = *(int *)argp;
    se_voice_t *v = &s_voices[voice_idx];

    int16_t *pcm     = (int16_t *)v->buf;
    uint32_t samples = v->size / sizeof(int16_t);
    uint32_t pos     = 0;

    /* Silence buffer for padding the last grain */
    static int16_t silence[SE_GRAIN];

    while (pos < samples) {
        uint32_t remaining = samples - pos;
        uint32_t grain     = (remaining >= SE_GRAIN) ? SE_GRAIN : remaining;
        int16_t *src       = pcm + pos;

        if (grain < SE_GRAIN) {
            /* Pad last chunk with silence so SceAudio gets a full grain */
            int16_t tmp[SE_GRAIN];
            sceClibMemcpy(tmp, src, grain * sizeof(int16_t));
            sceClibMemset(tmp + grain, 0, (SE_GRAIN - grain) * sizeof(int16_t));
            sceAudioOutOutput(v->port, tmp);
        } else {
            sceAudioOutOutput(v->port, src);
        }

        pos += grain;
    }

    free(v->buf);
    v->buf  = NULL;
    v->busy = 0;
    sceKernelExitDeleteThread(0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void ff7_se_player_init(void) {
    if (s_init) return;

    /* ----- Open and parse audio.fmt ----- */
    char fmt_path[512];
    snprintf(fmt_path, sizeof(fmt_path),
             DATA_PATH "ff7_1.02/data/sound/audio.fmt");

    FILE *f = fopen(fmt_path, "rb");
    if (!f) {
        ff7_boot_log("[se] audio.fmt not found at %s — SE disabled", fmt_path);
        s_init = 1; /* mark init done so we don't retry every frame */
        return;
    }

    uint32_t count = 0;
    if (fread(&count, 4, 1, f) != 1 || count == 0 || count > SE_MAX_ENTRIES) {
        ff7_boot_log("[se] audio.fmt: bad count %u", (unsigned)count);
        fclose(f);
        s_init = 1;
        return;
    }

    s_entries = (se_entry_t *)malloc(count * sizeof(se_entry_t));
    if (!s_entries) {
        ff7_boot_log("[se] OOM allocating %u entries", (unsigned)count);
        fclose(f);
        s_init = 1;
        return;
    }

    if (fread(s_entries, sizeof(se_entry_t), count, f) != count) {
        ff7_boot_log("[se] audio.fmt: truncated reading %u entries", (unsigned)count);
        free(s_entries); s_entries = NULL;
        fclose(f);
        s_init = 1;
        return;
    }
    fclose(f);
    s_count = count;

    ff7_boot_log("[se] audio.fmt: %u entries; entry[0] offset=%u size=%u",
                 (unsigned)count,
                 (unsigned)s_entries[0].offset,
                 (unsigned)s_entries[0].size);

    /* ----- Load audio.dat into memory ----- */
    char dat_path[512];
    snprintf(dat_path, sizeof(dat_path),
             DATA_PATH "ff7_1.02/data/sound/audio.dat");

    FILE *d = fopen(dat_path, "rb");
    if (!d) {
        ff7_boot_log("[se] audio.dat not found at %s — SE disabled", dat_path);
        free(s_entries); s_entries = NULL; s_count = 0;
        s_init = 1;
        return;
    }

    fseek(d, 0, SEEK_END);
    long dat_sz = ftell(d);
    fseek(d, 0, SEEK_SET);

    if (dat_sz <= 0) {
        ff7_boot_log("[se] audio.dat is empty");
        fclose(d);
        free(s_entries); s_entries = NULL; s_count = 0;
        s_init = 1;
        return;
    }

    s_dat = (uint8_t *)malloc((size_t)dat_sz);
    if (!s_dat) {
        ff7_boot_log("[se] OOM loading audio.dat (%ld bytes)", dat_sz);
        fclose(d);
        free(s_entries); s_entries = NULL; s_count = 0;
        s_init = 1;
        return;
    }

    if (fread(s_dat, 1, (size_t)dat_sz, d) != (size_t)dat_sz) {
        ff7_boot_log("[se] audio.dat: short read");
        fclose(d);
        free(s_dat); s_dat = NULL;
        free(s_entries); s_entries = NULL; s_count = 0;
        s_init = 1;
        return;
    }
    fclose(d);
    s_dat_size = (uint32_t)dat_sz;
    ff7_boot_log("[se] audio.dat loaded: %u bytes", s_dat_size);

    /* ----- Open SceAudio ports for each voice ----- */
    int ports_ok = 0;
    for (int i = 0; i < SE_MAX_VOICES; i++) {
        s_voices[i].port = sceAudioOutOpenPort(
            SCE_AUDIO_OUT_PORT_TYPE_VOICE, SE_GRAIN,
            SE_SAMPLE_RATE, SCE_AUDIO_OUT_MODE_MONO);
        if (s_voices[i].port < 0) {
            ff7_boot_log("[se] voice %d: sceAudioOutOpenPort failed 0x%x",
                         i, s_voices[i].port);
            s_voices[i].port = -1;
        } else {
            int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
            sceAudioOutSetVolume(s_voices[i].port,
                                 SCE_AUDIO_VOLUME_FLAG_L_CH |
                                 SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
            ports_ok++;
        }
        s_voices[i].busy   = 0;
        s_voices[i].buf    = NULL;
        s_voices[i].thread = -1;
    }

    ff7_boot_log("[se] init complete: %u sounds, %d/%d voices",
                 s_count, ports_ok, SE_MAX_VOICES);
    s_init = 1;
}

void ff7_se_play(int id) {
    if (!s_init) ff7_se_player_init();
    if (!s_entries || !s_dat || id < 0 || (uint32_t)id >= s_count) return;

    se_entry_t *e = &s_entries[id];
    if (e->offset + e->size > s_dat_size || e->size == 0) {
        ff7_boot_log("[se] play(%d): out-of-range offset=%u size=%u",
                     id, (unsigned)e->offset, (unsigned)e->size);
        return;
    }

    /* Find a free voice */
    int vi = -1;
    for (int i = 0; i < SE_MAX_VOICES; i++) {
        if (!s_voices[i].busy && s_voices[i].port >= 0) {
            vi = i;
            break;
        }
    }
    if (vi < 0) return; /* all voices busy — drop this sound */

    /* Copy PCM data so the thread owns it */
    uint8_t *buf = (uint8_t *)malloc(e->size);
    if (!buf) return;
    sceClibMemcpy(buf, s_dat + e->offset, e->size);

    s_voices[vi].buf  = buf;
    s_voices[vi].size = e->size;
    s_voices[vi].busy = 1;

    /* Apply current volume */
    int vol = (int)((float)s_volume);
    int vols[2] = { vol, vol };
    sceAudioOutSetVolume(s_voices[vi].port,
                         SCE_AUDIO_VOLUME_FLAG_L_CH |
                         SCE_AUDIO_VOLUME_FLAG_R_CH, vols);

    /* Spawn playback thread */
    static int s_thread_n = 0;
    char tname[32];
    snprintf(tname, sizeof(tname), "ff7_se_%d", s_thread_n++);

    SceUID tid = sceKernelCreateThread(tname, se_play_thread,
                                       0xC0, 0x4000, 0, 0, NULL);
    if (tid < 0) {
        ff7_boot_log("[se] failed to create play thread: 0x%x", tid);
        free(buf);
        s_voices[vi].buf  = NULL;
        s_voices[vi].busy = 0;
        return;
    }

    s_voices[vi].thread = tid;
    sceKernelStartThread(tid, sizeof(vi), &vi);
}

void ff7_se_set_volume(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    s_volume = (int)(v * (float)SCE_AUDIO_VOLUME_0DB);

    if (!s_init || !s_entries) return;

    int vols[2] = { s_volume, s_volume };
    for (int i = 0; i < SE_MAX_VOICES; i++) {
        if (s_voices[i].port >= 0) {
            sceAudioOutSetVolume(s_voices[i].port,
                                 SCE_AUDIO_VOLUME_FLAG_L_CH |
                                 SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
        }
    }
}
