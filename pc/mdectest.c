/*
 * Offline check for the port's own MDEC decoder (pc/shim/host_mdec.c, T37).
 *
 * It takes one .STR file from the owner's disc (never committed, ADR-001),
 * gathers one frame's video sectors, decodes the frame and writes it as a PNG
 * next to this binary. There is no window, no GPU and no PsyCross: this is the
 * decoder alone, so a failure is the decoder's.
 *
 * Structural assertions, on the real stream:
 *   - the run-level pass accepts every Huffman code (hostMdecVlc2 returns 0);
 *   - the macroblock count is (width/16) * (height/16);
 *   - every 8x8 block is closed by its 0xFE00 end code;
 *   - the frame's word count covers the blocks (count >= 6*macroblocks) and the
 *     decoder consumes no more tokens than the stream holds (never overflows);
 *   - the frame decoder reports no truncation and no bad token.
 *
 * Exit 0: every assertion held. Exit 1: one failed (the message names it).
 *
 * Usage: mdectest [file.str] [frame] [out.png]
 * The default input is the EU disc's opening movie, frame 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "shim/host_mdec.h"

#define STR_SECTOR      2048
#define STR_PAYLOAD     2016
#define STR_PAYLOAD_AT  32
#define STR_ID          0x0160u
#define STR_VIDEO       0x8001u
#define STR_MAX_SECTORS 512

/* host_shot.c, which has no public header of its own (its other entry point
   is declared in host.h, outside this task's edit surface). */
void pcSaveRgb24(const char *path, int width, int height, const void *rgb);

static unsigned read16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long read32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void fail(const char *what) {
    fprintf(stderr, "mdectest: FAIL %s\n", what);
    exit(1);
}

static unsigned char *readFile(const char *path, long *size) {
    FILE *file = fopen(path, "rb");
    unsigned char *data;
    long n;

    if (file == NULL) {
        fprintf(stderr, "mdectest: cannot open %s\n", path);
        exit(1);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fail("seek to the end");
    }
    n = ftell(file);
    if (n <= 0) {
        fail("empty input");
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fail("seek to the start");
    }
    data = malloc((size_t)n);
    if (data == NULL) {
        fail("input allocation");
    }
    if (fread(data, 1, (size_t)n, file) != (size_t)n) {
        fail("short read");
    }
    fclose(file);
    *size = n;
    return data;
}

/* The output goes next to the binary (pc/build, git-ignored), not the cwd. */
static void outputPath(char *out, size_t size, const char *argv0, int frame,
                       const char *given) {
    const char *slash;

    if (given != NULL) {
        snprintf(out, size, "%s", given);
        return;
    }
    slash = strrchr(argv0, '/');
    if (slash != NULL) {
        snprintf(out, size, "%.*s/mdec_frame%d.png", (int)(slash - argv0),
                 argv0, frame);
    } else {
        snprintf(out, size, "mdec_frame%d.png", frame);
    }
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1]
                                  : "disks/eu/AAA/STR/MOVIEOPN.STR";
    int frame = (argc > 2) ? atoi(argv[2]) : 1;
    const char *givenOut = (argc > 3) ? argv[3] : NULL;
    unsigned char *file;
    unsigned char *payload;
    const unsigned char *sector[STR_MAX_SECTORS];
    unsigned char *stream;
    unsigned char *rgb;
    char outPath[1024];
    long fileSize;
    long at;
    int width;
    int height;
    int nsec = 0;
    int frameSize = 0;
    int found = 0;
    int sec;
    int count;
    size_t payloadSize;
    size_t streamSize;
    HostMdec mdec;
    HostMdecStats stats;
    struct stat info;

    file = readFile(path, &fileSize);
    if (fileSize < STR_SECTOR) {
        fail("input shorter than one sector");
    }

    width = (int)read16(file + 16);
    height = (int)read16(file + 18);
    if (width <= 0 || height <= 0 || (width & 15) != 0 || (height & 15) != 0) {
        fprintf(stderr, "mdectest: bad frame size %dx%d\n", width, height);
        fail("frame dimensions");
    }

    at = 0;
    while (at + STR_SECTOR <= fileSize) {
        const unsigned char *s = file + at;

        if (read16(s + 0) == STR_ID && read16(s + 2) == STR_VIDEO &&
            (int)read32(s + 8) == frame) {
            int index = (int)read16(s + 4);

            if (index >= 0 && index < STR_MAX_SECTORS) {
                if (!found) {
                    nsec = (int)read16(s + 6);
                    frameSize = (int)read32(s + 12);
                }
                sector[index] = s + STR_PAYLOAD_AT;
                found = 1;
            }
        }
        at += STR_SECTOR;
    }

    if (!found) {
        fprintf(stderr, "mdectest: %s has no video for frame %d\n", path, frame);
        fail("frame lookup");
    }
    if (nsec <= 0 || nsec > STR_MAX_SECTORS) {
        fprintf(stderr, "mdectest: bad sector count %d\n", nsec);
        fail("sector count");
    }

    /* Compact the frame's payloads in sector order: only the first one carries
       the 8-byte MDEC header, the Huffman data runs across all of them. */
    payloadSize = (size_t)nsec * STR_PAYLOAD + 64;
    payload = calloc(payloadSize, 1);
    if (payload == NULL) {
        fail("payload allocation");
    }
    {
        size_t used = 0;

        for (sec = 0; sec < nsec; sec++) {
            if (sector[sec] != NULL) {
                memcpy(payload + used, sector[sec], STR_PAYLOAD);
                used += STR_PAYLOAD;
            }
        }
        if (used < 8) {
            fail("frame payload too short for the MDEC header");
        }
    }

    count = (int)read16(payload + 0);
    if (count <= 0) {
        fail("frame word count is zero");
    }
    streamSize = 4u + (size_t)count * 4u;
    stream = malloc(streamSize);
    if (stream == NULL) {
        fail("run-level allocation");
    }

    hostMdecInit(&mdec);
    if (hostMdecVlc2(payload, stream, &mdec.vlc) != 0) {
        fprintf(stderr, "mdectest: bad Huffman code at bit %d, %d tokens\n",
                mdec.vlc.lastErrorBit, mdec.vlc.lastTokens);
        fail("VLC decode");
    }
    if (mdec.vlc.lastTokens <= 0 || mdec.vlc.lastTokens > count * 2) {
        fprintf(stderr, "mdectest: %d tokens for a %d-word frame\n",
                mdec.vlc.lastTokens, count);
        fail("run-level token count");
    }

    rgb = malloc((size_t)width * height * 3);
    if (rgb == NULL) {
        fail("pixel allocation");
    }
    if (hostMdecDecodeFrame(&mdec, stream, width, height,
                            HMDEC_DEPTH_COMMAND, rgb, &stats) != 0) {
        fail("frame decode");
    }

    if (stats.macroblocks != (width / 16) * (height / 16)) {
        fprintf(stderr, "mdectest: %d macroblocks for %dx%d\n",
                stats.macroblocks, width, height);
        fail("macroblock count");
    }
    if (stats.blocks != stats.macroblocks * 6) {
        fprintf(stderr, "mdectest: %d blocks for %d macroblocks\n",
                stats.blocks, stats.macroblocks);
        fail("block count");
    }
    if (stats.eobBlocks != stats.blocks) {
        fprintf(stderr, "mdectest: %d of %d blocks closed by EOB\n",
                stats.eobBlocks, stats.blocks);
        fail("end-of-block codes");
    }
    if (stats.errors != 0) {
        fprintf(stderr, "mdectest: %d truncated or bad blocks\n", stats.errors);
        fail("run-level stream");
    }
    /* count is in 32-bit words, two tokens each; a block needs at least its DC
       and EOB, so the frame cannot hold fewer words than blocks. */
    if (count < stats.blocks || stats.tokens > count * 2) {
        fprintf(stderr, "mdectest: count %d, %d blocks, %d tokens\n",
                count, stats.blocks, stats.tokens);
        fail("word count bounds");
    }

    outputPath(outPath, sizeof(outPath), argv[0], frame, givenOut);
    pcSaveRgb24(outPath, width, height, rgb);

    printf("mdectest: %s frame %d: %dx%d, %d sectors, frameSize %d, "
           "count %d (q %d, version %d)\n",
           path, frame, width, height, found ? nsec : 0, frameSize, count,
           (int)(read16(payload + 4) & 0x3Fu), (int)read16(payload + 6));
    printf("mdectest: macroblocks %d, blocks %d, EOB %d, errors %d, "
           "tokens %d\n",
           stats.macroblocks, stats.blocks, stats.eobBlocks, stats.errors,
           stats.tokens);
    if (stat(outPath, &info) == 0) {
        printf("mdectest: wrote %s (%ld bytes)\n", outPath, (long)info.st_size);
    } else {
        printf("mdectest: wrote %s\n", outPath);
    }

    free(rgb);
    free(stream);
    free(payload);
    free(file);
    return 0;
}
