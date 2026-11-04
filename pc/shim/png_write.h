#ifndef PC_PNG_WRITE_H
#define PC_PNG_WRITE_H

#include <stddef.h>
#include <stdint.h>

/*
 * A minimal PNG encoder for indexed images (menu art extraction, feature menu-art-png T2).
 *
 * No dependency: our own deflate (LZ77 with hash chains, lazy matching; per block the smallest of stored,
 * fixed and dynamic Huffman) and PNG scanline filters 0..4 chosen per row. A 256x256 4-bit page was 32 KB
 * stored and is a few KB now. The output is deterministic, which the manifest hash needs, and every
 * PNG reader accepts it (checked against Pillow). The reader (png_read.c) reads old stored files and these.
 */

/* One 8-bit index per pixel in `px` (w*h bytes, row by row). depth is the PNG bit depth: 4 (indices
   0..15) or 8. rgb holds nColors triplets; alpha (may be NULL) holds nAlpha leading tRNS entries
   (0 = transparent), the rest are opaque. On success 0 and *out is malloc'ed with *len bytes. */
int pngWriteIndexed(const uint8_t *px, int w, int h, int depth, const uint8_t *rgb, int nColors,
                    const uint8_t *alpha, int nAlpha, uint8_t **out, size_t *len);

#endif
