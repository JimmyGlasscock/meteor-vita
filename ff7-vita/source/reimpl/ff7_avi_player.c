/*
 * ff7_avi_player — RIFF/AVI container parser + libjpeg MJPEG decoder.
 *
 * FF7 Android stores all FMV as AVI files whose video stream uses the MJPEG
 * codec (each video chunk is a self-contained JPEG image).  On Android the
 * native fw_start_movie / AVI_frame pipeline feeds those JPEGs into a
 * SurfaceTexture backed OES texture — a mechanism that does not exist on Vita.
 *
 * This module replaces that pipeline:
 *   1.  A tiny RIFF parser locates the "movi" chunk list and reads the AVI
 *       main header to learn frame dimensions / count / rate.
 *   2.  For each FRAME callback, one "00dc" (or "00db") chunk is read, decoded
 *       to RGB via libjpeg, expanded to RGBA, then handed to
 *       ff7_gl_upload_rgba_movie_tex() which uploads it to the GL texture the
 *       game already has bound for movie rendering.
 *   3.  Audio chunks ("01wb") are skipped — audio is not yet implemented.
 */

#include "reimpl/ff7_avi_player.h"
#include "reimpl/ff7_gl_movie_tex.h"
#include "utils/ff7_boot_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

/* ------------------------------------------------------------------ */
/* FOURCC helpers                                                      */
/* ------------------------------------------------------------------ */

#define FOURCC(a,b,c,d) \
    ((uint32_t)(uint8_t)(a)        | \
     ((uint32_t)(uint8_t)(b) <<  8) | \
     ((uint32_t)(uint8_t)(c) << 16) | \
     ((uint32_t)(uint8_t)(d) << 24))

#define CC_RIFF FOURCC('R','I','F','F')
#define CC_AVI  FOURCC('A','V','I',' ')
#define CC_LIST FOURCC('L','I','S','T')
#define CC_hdrl FOURCC('h','d','r','l')
#define CC_avih FOURCC('a','v','i','h')
#define CC_movi FOURCC('m','o','v','i')
#define CC_00dc FOURCC('0','0','d','c')   /* compressed video stream 0 */
#define CC_00db FOURCC('0','0','d','b')   /* uncompressed video stream 0 */
#define CC_01wb FOURCC('0','1','w','b')   /* audio stream 1 (skip) */
#define CC_idx1 FOURCC('i','d','x','1')   /* legacy index — marks end of movi */

/* ------------------------------------------------------------------ */
/* Player state — single global instance (one movie at a time)        */
/* ------------------------------------------------------------------ */

static FILE     *s_fp            = NULL;
static long      s_movi_end      = 0;      /* file offset past last movi byte */
static int       s_total_frames  = 0;      /* from AVI main header            */
static int       s_frame_idx     = 0;      /* frames decoded so far           */
static uint32_t  s_us_per_frame  = 33333;  /* microseconds per frame (~30fps) */
static int       s_vid_w         = 0;
static int       s_vid_h         = 0;
static int       s_tex_inited    = 0;      /* 0 = next upload uses glTexImage2D */

/* ------------------------------------------------------------------ */
/* I/O helpers                                                         */
/* ------------------------------------------------------------------ */

static int read_u32(uint32_t *v)
{
    unsigned char b[4];
    if (fread(b, 1, 4, s_fp) != 4) return 0;
    *v = (uint32_t)b[0]
       | ((uint32_t)b[1] <<  8)
       | ((uint32_t)b[2] << 16)
       | ((uint32_t)b[3] << 24);
    return 1;
}

static int skip_bytes(long n)
{
    return n <= 0 || fseek(s_fp, n, SEEK_CUR) == 0;
}

/* ------------------------------------------------------------------ */
/* libjpeg error handler — prevents exit() on a bad frame             */
/* ------------------------------------------------------------------ */

struct avi_jpeg_err {
    struct jpeg_error_mgr pub;
    jmp_buf               jmp;
};

static void avi_jpeg_error_exit(j_common_ptr cinfo)
{
    struct avi_jpeg_err *e = (struct avi_jpeg_err *)cinfo->err;
    longjmp(e->jmp, 1);
}

/* ------------------------------------------------------------------ */
/* MJPEG -> RGBA decode                                               */
/* ------------------------------------------------------------------ */

/*
 * Decode one MJPEG frame (a raw JPEG blob) to a freshly malloc'd RGBA buffer.
 * Sets *out_w / *out_h to the decoded dimensions.
 * Returns NULL on any error (caller should skip the frame).
 */
static uint8_t *decode_mjpeg(const uint8_t *data, uint32_t size,
                              int *out_w, int *out_h)
{
    struct jpeg_decompress_struct cinfo;
    struct avi_jpeg_err jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = avi_jpeg_error_exit;

    if (setjmp(jerr.jmp)) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)data, (unsigned long)size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int w = (int)cinfo.output_width;
    int h = (int)cinfo.output_height;

    uint8_t *rgb = malloc((size_t)w * (size_t)h * 3);
    if (!rgb) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    while ((int)cinfo.output_scanline < h) {
        JSAMPROW row_ptr = (JSAMPROW)(rgb + (size_t)cinfo.output_scanline * (size_t)w * 3);
        jpeg_read_scanlines(&cinfo, &row_ptr, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    /* Expand RGB -> RGBA (game shader expects 4-channel texture) */
    uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
    if (!rgba) {
        free(rgb);
        return NULL;
    }

    const uint8_t *src = rgb;
    uint8_t *dst = rgba;
    for (int i = 0; i < w * h; i++, src += 3, dst += 4) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 0xFF;
    }
    free(rgb);

    *out_w = w;
    *out_h = h;
    return rgba;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void ff7_avi_close(void)
{
    if (s_fp) { fclose(s_fp); s_fp = NULL; }
    s_movi_end     = 0;
    s_total_frames = 0;
    s_frame_idx    = 0;
    s_us_per_frame = 33333;
    s_vid_w        = 0;
    s_vid_h        = 0;
    s_tex_inited   = 0;
}

int ff7_avi_open(const char *path)
{
    ff7_avi_close();

    s_fp = fopen(path, "rb");
    if (!s_fp) {
        ff7_boot_log("[avi] open FAIL: %s", path);
        return 0;
    }

    /* ---- Validate RIFF/AVI signature ---- */
    uint32_t tag, file_size, type;
    if (!read_u32(&tag)      || tag != CC_RIFF) goto fail;
    if (!read_u32(&file_size))                  goto fail;
    if (!read_u32(&type)     || type != CC_AVI) goto fail;

    long file_end   = (long)file_size + 8;
    int  found_movi = 0;

    /* ---- Scan top-level chunks for hdrl and movi ---- */
    while (!found_movi && ftell(s_fp) < file_end) {
        uint32_t chunk_tag, chunk_size;
        if (!read_u32(&chunk_tag) || !read_u32(&chunk_size)) break;

        long chunk_data_start = ftell(s_fp);

        if (chunk_tag == CC_LIST) {
            uint32_t list_type;
            if (!read_u32(&list_type)) break;

            if (list_type == CC_hdrl) {
                /* Parse the AVI main header (avih) to get dimensions / fps */
                long hdrl_end = chunk_data_start + (long)chunk_size;

                while (ftell(s_fp) < hdrl_end) {
                    uint32_t t, s;
                    if (!read_u32(&t) || !read_u32(&s)) break;

                    if (t == CC_avih && s >= 40) {
                        uint32_t us_per_frame, dummy, dummy2, dummy3;
                        uint32_t total_frames, dummy4, dummy5, dummy6, w, h;

                        read_u32(&us_per_frame);
                        read_u32(&dummy);  read_u32(&dummy2); read_u32(&dummy3);
                        read_u32(&total_frames);
                        read_u32(&dummy4); read_u32(&dummy5); read_u32(&dummy6);
                        read_u32(&w);      read_u32(&h);

                        s_us_per_frame = us_per_frame ? us_per_frame : 33333;
                        s_total_frames = (int)total_frames;
                        s_vid_w        = (int)w;
                        s_vid_h        = (int)h;

                        long rest = (long)s - 40;
                        if (rest > 0) skip_bytes(rest + (s & 1));
                    } else {
                        skip_bytes((long)s + (s & 1));
                    }
                }
                fseek(s_fp, hdrl_end, SEEK_SET);

            } else if (list_type == CC_movi) {
                /* File pointer is now at the first chunk inside movi */
                s_movi_end  = chunk_data_start + (long)chunk_size;
                found_movi  = 1;
                /* Leave position here — ff7_avi_next_frame reads from here */

            } else {
                /* Some other LIST (e.g. INFO) — skip its contents */
                skip_bytes((long)chunk_size - 4);
            }
        } else {
            /* Non-LIST chunk (JUNK padding etc.) — skip */
            skip_bytes((long)chunk_size + (chunk_size & 1));
        }
    }

    if (!found_movi) {
        ff7_boot_log("[avi] no movi LIST in %s", path);
        goto fail;
    }

    ff7_boot_log("[avi] %s | %dx%d %d frames %.2f fps",
                 path, s_vid_w, s_vid_h, s_total_frames,
                 s_us_per_frame > 0 ? 1000000.0 / (double)s_us_per_frame : 0.0);
    return 1;

fail:
    fclose(s_fp);
    s_fp = NULL;
    return 0;
}

int ff7_avi_frame_count(void) { return s_total_frames; }

uint32_t ff7_avi_total_ms(void)
{
    return (uint32_t)(((uint64_t)s_total_frames * s_us_per_frame) / 1000);
}

uint32_t ff7_avi_position_ms(void)
{
    return (uint32_t)(((uint64_t)s_frame_idx * s_us_per_frame) / 1000);
}

/*
 * Decode and display the next video frame.
 *
 * Scans forward in the movi chunk for the next "00dc"/"00db" video chunk,
 * skipping audio and padding.  Decodes it with libjpeg and uploads the RGBA
 * result to `tex`.
 *
 * Returns:
 *   1  — frame rendered; more frames likely remain
 *   0  — end of movie (or unrecoverable I/O error); caller should stop
 */
int ff7_avi_next_frame(GLuint tex)
{
    if (!s_fp) return 0;

    for (;;) {
        if (ftell(s_fp) >= s_movi_end) return 0;   /* past end of movi */

        uint32_t chunk_tag, chunk_size;
        if (!read_u32(&chunk_tag) || !read_u32(&chunk_size)) return 0;

        if (chunk_tag == CC_00dc || chunk_tag == CC_00db) {
            /* ---- Video frame ---- */
            uint8_t *jpeg_buf = malloc(chunk_size);
            if (!jpeg_buf) {
                skip_bytes((long)chunk_size + (chunk_size & 1));
                s_frame_idx++;
                return 1;   /* memory pressure — skip but stay alive */
            }

            if (fread(jpeg_buf, 1, chunk_size, s_fp) != chunk_size) {
                free(jpeg_buf);
                return 0;
            }
            if (chunk_size & 1) skip_bytes(1);   /* word-align */

            int w = 0, h = 0;
            uint8_t *rgba = decode_mjpeg(jpeg_buf, chunk_size, &w, &h);
            free(jpeg_buf);

            if (!rgba) {
                /* Bad frame — log once per movie and skip */
                static int s_bad_frames = 0;
                if (s_bad_frames++ < 3)
                    ff7_boot_log("[avi] JPEG decode fail, frame %d", s_frame_idx);
                s_frame_idx++;
                return 1;
            }

            ff7_gl_upload_rgba_movie_tex(tex, w, h, rgba, !s_tex_inited);
            s_tex_inited = 1;
            free(rgba);

            s_frame_idx++;

            /* Check frame count if the header reported one */
            if (s_total_frames > 0 && s_frame_idx >= s_total_frames)
                return 0;

            return 1;

        } else if (chunk_tag == CC_LIST) {
            /* "rec " sub-list — descend (don't skip, just read past the type) */
            uint32_t list_type;
            if (!read_u32(&list_type)) return 0;
            /* Continue reading from inside this list; s_movi_end still bounds us */

        } else if (chunk_tag == CC_idx1) {
            /* Legacy index chunk — always appears after all video data */
            return 0;

        } else {
            /* Audio ("01wb") or unknown — skip */
            skip_bytes((long)chunk_size + (chunk_size & 1));
        }
    }
}
