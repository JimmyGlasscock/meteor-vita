/*
 * ff7_video_player.h — SceAvPlayer-backed video playback for FF7 FMV cutscenes.
 *
 * The Android port ships FMV as WebM/VP8 files.  The PS Vita hardware decoder
 * (SceAvPlayer) only handles H.264/MP4, so the WebM files must be converted
 * once with ffmpeg before copying them to the Vita:
 *
 *   cd ux0:data/ff7/ff7_1.02/data/movies/
 *   for f in *.webm; do
 *     ffmpeg -i "$f" -c:v libx264 -preset fast -crf 22 \
 *            -c:a aac -b:a 128k "${f%.webm}.mp4"
 *   done
 *
 * Our implementation automatically appends ".mp4" instead of ".webm" when
 * opening a movie, so no path changes are required in the game code.
 *
 * Interface mirrors the old ff7_avi_player so ff7_jni_callbacks.c only
 * needs trivial renames.
 */

#pragma once

#include <stdint.h>
#include <vitaGL.h>

/*
 * Load the SceAvPlayer sysmodule and prepare internal state.
 * Call once at startup (before any ff7_video_* calls).
 */
void ff7_video_init(void);

/*
 * Open a video file for playback.  `path` is the Vita-native filesystem path
 * (already translated from Android).  The extension is replaced with ".mp4"
 * automatically.
 *
 * Returns 1 on success, 0 on failure.
 */
int ff7_video_open(const char *path);

/*
 * Decode the next video frame and upload it to `tex_id` (a GL texture).
 *
 * Returns 1 while the video is still playing, 0 when playback is complete or
 * no video is open.
 */
int ff7_video_next_frame(GLuint tex_id);

/* Total playback duration in milliseconds (0 if unknown). */
uint32_t ff7_video_total_ms(void);

/* Current playback position in milliseconds. */
uint32_t ff7_video_position_ms(void);

/* Stop playback and free all resources. */
void ff7_video_close(void);
