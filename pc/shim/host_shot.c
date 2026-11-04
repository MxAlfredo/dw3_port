/*
 * The screenshot writer the two windowed harnesses share: pc/font_frame.c
 * (milestone 5) and pc/boot.c (T15). `glReadPixels` of the presented frame,
 * written as a PNG with no library: the zlib stream uses stored
 * (uncompressed) deflate blocks, which is all a screenshot needs.
 *
 * A GL read is the only reliable capture on this host: with direct rendering
 * xwd/x11grab return the window black (T13's note). The frame has to be read
 * before PsyX_EndScene swaps it -- what is in the back buffer then is what
 * the window will show.
 *
 * It is our code (pc/), not the game's, and it uses the host's allocator on
 * purpose: a screenshot must not depend on the game's heap still being sane.
 */
#include "host.h"

/* PSYX_FORCE_GLES (pc/Makefile GLES=1): the ES 3 renderer's headers; glReadPixels(RGBA, UNSIGNED_BYTE) below is valid in both */
#ifdef PSYX_FORCE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t adler32(const uint8_t *data, uint32_t size) {
    uint32_t a = 1;
    uint32_t b = 0;
    uint32_t i;

    for (i = 0; i < size; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

/* The running CRC-32 (the caller complements the initial and final values). */
static uint32_t crcUpdate(uint32_t crc, const uint8_t *data, uint32_t size) {
    uint32_t i;
    int k;

    for (i = 0; i < size; i++) {
        crc ^= data[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    return crc;
}

/* length, type, data and CRC of one chunk */
static int pngChunk(FILE *file, const char *type, const uint8_t *data, uint32_t size) {
    uint8_t header[8];
    uint8_t tail[4];
    uint32_t crc;

    header[0] = (uint8_t)(size >> 24);
    header[1] = (uint8_t)(size >> 16);
    header[2] = (uint8_t)(size >> 8);
    header[3] = (uint8_t)size;
    header[4] = (uint8_t)type[0];
    header[5] = (uint8_t)type[1];
    header[6] = (uint8_t)type[2];
    header[7] = (uint8_t)type[3];
    crc = 0xFFFFFFFFu;
    crc = crcUpdate(crc, header + 4, 4);
    crc = crcUpdate(crc, data, size);
    crc ^= 0xFFFFFFFFu;
    tail[0] = (uint8_t)(crc >> 24);
    tail[1] = (uint8_t)(crc >> 16);
    tail[2] = (uint8_t)(crc >> 8);
    tail[3] = (uint8_t)crc;

    if (fwrite(header, 1, 8, file) != 8) {
        return 0;
    }
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        return 0;
    }
    return fwrite(tail, 1, 4, file) == 4;
}

/* The zlib stream of the raw scanlines: the header, stored blocks, the adler32 */
static uint8_t *zlibStore(const uint8_t *raw, uint32_t size, uint32_t *outSize) {
    uint32_t blocks = (size + 65534u) / 65535u;
    uint32_t total = 2 + blocks * 5 + size + 4;
    uint8_t *out = malloc(total);
    uint32_t at = 0;
    uint32_t block = 0;

    if (out == NULL) {
        return NULL;
    }
    out[at++] = 0x78; /* CM=8, CINFO=7 */
    out[at++] = 0x01; /* FCHECK so that 0x7801 is a multiple of 31 */
    while (block < blocks) {
        uint32_t n = size - block * 65535u;
        uint32_t i;

        if (n > 65535u) {
            n = 65535u;
        }
        out[at++] = (block == blocks - 1) ? 0x01 : 0x00; /* BFINAL, BTYPE 0 */
        out[at++] = (uint8_t)n;
        out[at++] = (uint8_t)(n >> 8);
        out[at++] = (uint8_t)(~n);
        out[at++] = (uint8_t)(~n >> 8);
        for (i = 0; i < n; i++) {
            out[at++] = raw[block * 65535u + i];
        }
        block++;
    }
    {
        uint32_t adler = adler32(raw, size);

        out[at++] = (uint8_t)(adler >> 24);
        out[at++] = (uint8_t)(adler >> 16);
        out[at++] = (uint8_t)(adler >> 8);
        out[at++] = (uint8_t)adler;
    }
    *outSize = at;
    return out;
}

/*
 * Saves what is on screen as a PNG at `path`: RGBA pixels read from the GL
 * back buffer, top-down (GL is bottom-up), written as an 8-bit RGB PNG.
 */
void pcSaveShot(const char *path, int width, int height) {
    uint8_t *rgba;
    uint8_t *raw;
    uint8_t *idat;
    uint32_t idatSize = 0;
    uint32_t rawSize;
    FILE *file;
    uint8_t header[13];
    int x;
    int y;

    if (width <= 0 || height <= 0) {
        return;
    }

    rgba = malloc((size_t)width * height * 4);
    raw = malloc((size_t)height * (1 + width * 3));
    if (rgba == NULL || raw == NULL) {
        free(rgba);
        free(raw);
        return;
    }
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    rawSize = (uint32_t)(height * (1 + width * 3));
    memset(raw, 0, rawSize);
    for (y = 0; y < height; y++) {
        uint8_t *row = raw + (uint32_t)y * (1 + width * 3) + 1; /* filter byte 0 first */
        const uint8_t *src = rgba + (uint32_t)(height - 1 - y) * width * 4; /* GL is bottom-up */

        for (x = 0; x < width; x++) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
    }

    idat = zlibStore(raw, rawSize, &idatSize);

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "pcSaveShot: cannot write %s\n", path);
    } else if (idat != NULL) {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, file);
        header[0] = (uint8_t)(width >> 24);
        header[1] = (uint8_t)(width >> 16);
        header[2] = (uint8_t)(width >> 8);
        header[3] = (uint8_t)width;
        header[4] = (uint8_t)(height >> 24);
        header[5] = (uint8_t)(height >> 16);
        header[6] = (uint8_t)(height >> 8);
        header[7] = (uint8_t)height;
        header[8] = 8;  /* bit depth */
        header[9] = 2;  /* colour type: RGB */
        header[10] = 0; /* compression */
        header[11] = 0; /* filter */
        header[12] = 0; /* interlace */
        pngChunk(file, "IHDR", header, sizeof(header));
        pngChunk(file, "IDAT", idat, idatSize);
        pngChunk(file, "IEND", NULL, 0);
        fclose(file);
    } else {
        fclose(file);
    }

    free(idat);
    free(raw);
    free(rgba);
}

/*
 * Saves an RGB24 image (top-down, 3 bytes per pixel) as a PNG. The GL capture
 * above reads the back buffer; this one takes pixels the caller already has,
 * which is what the offline MDEC harness needs (pc/mdectest.c): it decodes a
 * frame with no window and still leaves the owner a file to look at.
 */
void pcSaveRgb24(const char *path, int width, int height, const void *rgb) {
    const uint8_t *pixels = rgb;
    uint8_t *raw;
    uint8_t *idat;
    uint32_t idatSize = 0;
    uint32_t rawSize;
    FILE *file;
    uint8_t header[13];
    int x;
    int y;

    if (width <= 0 || height <= 0 || rgb == NULL) {
        return;
    }

    rawSize = (uint32_t)(height * (1 + width * 3));
    raw = malloc(rawSize);
    if (raw == NULL) {
        return;
    }
    for (y = 0; y < height; y++) {
        uint8_t *row = raw + (uint32_t)y * (1 + width * 3);
        const uint8_t *src = pixels + (uint32_t)y * width * 3;

        row[0] = 0; /* filter: none */
        for (x = 0; x < width * 3; x++) {
            row[1 + x] = src[x];
        }
    }

    idat = zlibStore(raw, rawSize, &idatSize);
    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "pcSaveRgb24: cannot write %s\n", path);
    } else if (idat == NULL) {
        fclose(file);
    } else {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, file);
        header[0] = (uint8_t)(width >> 24);
        header[1] = (uint8_t)(width >> 16);
        header[2] = (uint8_t)(width >> 8);
        header[3] = (uint8_t)width;
        header[4] = (uint8_t)(height >> 24);
        header[5] = (uint8_t)(height >> 16);
        header[6] = (uint8_t)(height >> 8);
        header[7] = (uint8_t)height;
        header[8] = 8;  /* bit depth */
        header[9] = 2;  /* colour type: RGB */
        header[10] = 0; /* compression */
        header[11] = 0; /* filter */
        header[12] = 0; /* interlace */
        pngChunk(file, "IHDR", header, sizeof(header));
        pngChunk(file, "IDAT", idat, idatSize);
        pngChunk(file, "IEND", NULL, 0);
        fclose(file);
    }

    free(idat);
    free(raw);
}
