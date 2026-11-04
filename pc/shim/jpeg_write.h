#ifndef PC_JPEG_WRITE_H
#define PC_JPEG_WRITE_H

/*
 * A small baseline JPEG encoder (content-export E2a): what the video export puts in every frame of an MJPEG AVI.
 *
 * The profile is fixed and simple on purpose, so any decoder (and the port's own reader of E2b) handles it:
 *   - baseline sequential DCT (SOF0), 8 bits, three components, JFIF;
 *   - 4:2:0 (Y at 2x2, Cb and Cr at 1x1): the MDEC the discs use already stores its chroma at half resolution, so
 *     averaging RGB 2x2 again keeps what the decoder produced; 4:4:4 would only double the chroma bytes;
 *   - the standard Huffman tables of ITU T.81 Annex K (no optimised tables, no restart markers);
 *   - the Annex K quantisation tables scaled by the quality with the usual IJG rule (1..100; the export uses 85).
 * Portable C with no state outside the call (safe from any thread), no libjpeg. The DCT is float: compile with
 * -ffp-contract=off so the bytes do not depend on the compiler's fused multiply-add choice.
 */

#include <stddef.h>
#include <stdint.h>

/* The most bytes jpegEncodeRgb24 can write for an image of w x h (a worst case, never reached by real pictures) */
size_t jpegMaxSize(int w, int h);

/* rgb: w*h*3 bytes, R,G,B, rows top-down. quality 1..100. Writes the whole JPEG file (SOI to EOI) into out and returns its
   length; 0 when an argument is bad or cap is too small. w and h may be any size from 1 to 65535 (the edge is repeated). */
size_t jpegEncodeRgb24(const uint8_t *rgb, int w, int h, int quality, uint8_t *out, size_t cap);

#endif
