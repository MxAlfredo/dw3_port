#ifndef PC_JPEG_READ_H
#define PC_JPEG_READ_H

/*
 * A small baseline JPEG decoder (content-export E2b): what a mod's replacement movie (an MJPEG AVI, avi_read.h) is decoded with.
 *
 * It accepts what jpeg_write.c writes and what a modder's ffmpeg/PIL/libjpeg writes for an MJPEG video:
 *   - baseline or extended sequential Huffman DCT (SOF0, SOF1), 8 bits, one scan with every component (the usual file);
 *   - one component (grey) or three (YCbCr, or RGB when an Adobe marker says transform 0 or the components are named R, G, B);
 *   - any sampling the components divide evenly (4:4:4, 4:2:2, 4:2:0, 4:4:0, 4:1:1, ...); chroma is repeated, not interpolated;
 *   - quantisation tables of 8 or 16 bits, restart intervals (DRI, RSTn), JFIF or none;
 *   - Huffman tables that are absent: the Annex K standard tables stand in, as AVI1 MJPEG expects (an AVI1 frame has no DHT).
 * Rejected, with the reason in err: progressive, arithmetic or lossless frames, 12-bit samples, four components, scans that split
 * the components, odd sampling ratios, pictures over JPEG_MAX_PIXELS. A damaged file never reads outside the data it is given.
 * Portable C with no state outside the call besides the IDCT table (computed once, always the same values); no libjpeg.
 * The IDCT is float: compile with -ffp-contract=off so the pixels do not depend on the compiler's fused multiply-add choice.
 */

#include <stddef.h>
#include <stdint.h>

/* The most pixels a frame may have (4096 x 4096) */
#define JPEG_MAX_PIXELS (4096L * 4096L)

/* Reads the frame header only. 0 and the size and the component count (1 or 3), or -1 with the reason in err (may be NULL). */
int jpegReadInfo(const uint8_t *data, size_t len, int *w, int *h, int *components, char *err, size_t errCap);

/* Decodes the whole picture to *rgb (malloc'ed, w*h*3 bytes, R,G,B, rows top-down; the caller frees it). 0, or -1 with the
   reason in err (may be NULL) and nothing allocated. */
int jpegDecodeRgb24(const uint8_t *data, size_t len, uint8_t **rgb, int *w, int *h, char *err, size_t errCap);

#endif
