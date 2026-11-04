#ifndef PC_PNG_READ_H
#define PC_PNG_READ_H

#include <stddef.h>
#include <stdint.h>

/*
 * A small PNG reader for indexed images (menu art substitution, feature menu-art-png T3).
 *
 * No dependency: it carries its own inflate (stored, fixed and dynamic Huffman blocks), so PNGs saved
 * by any editor (real zlib compression) are accepted. Only what the game's art needs: colour type 3
 * (palette), bit depth 1, 2, 4 or 8, not interlaced, filters 0..4, PLTE and tRNS. Anything else is
 * refused with a reason; a corrupt file (bad signature, chunk CRC, zlib header, Adler-32, short data)
 * never crashes.
 */

typedef struct PngImage {
    int w, h;
    int depth;            /* the file's bit depth */
    int nPal;             /* PLTE entries */
    uint8_t rgb[256 * 3];
    int nAlpha;           /* tRNS entries (the rest are opaque) */
    uint8_t alpha[256];
    uint8_t *px;          /* w*h indices, one per byte, malloc'ed */
} PngImage;

/* 0 on success; else -1 with a short reason in why (NULL allowed) */
int pngReadIndexed(const uint8_t *data, size_t len, PngImage *out, char *why, size_t whySize);
void pngImageFree(PngImage *p);

/* Raw deflate (no zlib wrapper): fills out[0..cap), *outLen is what was produced. 0 on success;
   -1 when the stream is corrupt or does not fit in cap. */
int pngInflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *outLen);

#endif
