/*
 * ff7_avi_player — lightweight RIFF/AVI MJPEG player for PS Vita.
 *
 * Opens an AVI file containing MJPEG-encoded video, decodes frames with
 * libjpeg one at a time, and uploads each decoded RGBA frame to a GL texture
 * via ff7_gl_upload_rgba_movie_tex().
 *
 * Usage:
 *   ff7_avi_open(path)          — open file, parse header; returns 1 on success
 *   ff7_avi_next_frame(tex)     — decode + upload next frame; returns 1 if more
 *                                 frames remain, 0 when done / on error
 *   ff7_avi_close()             — close file, reset all state
 *   ff7_avi_total_ms()          — total movie duration in milliseconds
 *   ff7_avi_position_ms()       — current playback position in milliseconds
 *   ff7_avi_frame_count()       — total number of video frames in header
 */

#ifndef FF7_AVI_PLAYER_H
#define FF7_AVI_PLAYER_H

#include <stdint.h>
#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open an AVI file and prepare for sequential frame decode.
 * Returns 1 on success, 0 on failure. */
int ff7_avi_open(const char *path);

/* Decode and upload the next video frame to `tex`.
 * Returns 1 while more frames remain, 0 when the movie is finished or an
 * unrecoverable error occurs. */
int ff7_avi_next_frame(GLuint tex);

/* Close the file and reset all internal state. Safe to call multiple times. */
void ff7_avi_close(void);

/* Total movie duration derived from the AVI header (milliseconds). */
uint32_t ff7_avi_total_ms(void);

/* Current playback position (milliseconds, based on frames decoded so far). */
uint32_t ff7_avi_position_ms(void);

/* Frame count from the AVI main header (may be 0 for some encoders). */
int ff7_avi_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FF7_AVI_PLAYER_H */
