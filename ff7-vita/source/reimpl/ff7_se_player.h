/*
 * ff7_se_player — sound-effect playback for FF7 Vita.
 *
 * FF7 Android stores all sound effects in two files under
 * DATA_PATH/ff7_1.02/data/sound/:
 *
 *   audio.fmt — index file; maps sound-effect ID → offset + size in audio.dat
 *   audio.dat — raw PCM audio bank (16-bit little-endian, mono, 22050 Hz)
 *
 * The Java-side SEPlayer.PLAY(int id) / SETVOLUME(float v) callbacks
 * (in java.c) are forwarded here.  Playback runs on a dedicated SceAudio
 * port so it never blocks the render thread.
 */

#ifndef FF7_SE_PLAYER_H
#define FF7_SE_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Call once after DATA_PATH is known (from soloader_init_all, after the .so
 * is loaded).  Opens and parses audio.fmt / audio.dat and allocates the audio
 * output port.  Safe to call multiple times; subsequent calls are no-ops.
 */
void ff7_se_player_init(void);

/*
 * Play sound effect `id` (0-based index into audio.fmt).  Non-blocking: the
 * audio is submitted to the output port from a background thread.
 * Silently ignored if init failed or id is out of range.
 */
void ff7_se_play(int id);

/*
 * Set master SE volume.  `v` is in [0.0, 1.0]; maps to SceAudio volume scale
 * [0, SCE_AUDIO_VOLUME_0DB].
 */
void ff7_se_set_volume(float v);

#ifdef __cplusplus
}
#endif

#endif /* FF7_SE_PLAYER_H */
