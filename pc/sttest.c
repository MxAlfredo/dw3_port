/*
 * Offline check for the port's St stream source (pc/shim/host_st.c, T38).
 *
 * It reads the opening movie from the owner's disc (never committed, ADR-001;
 * the copy under disks/eu is the extracted 2048-byte sectors), feeds its video
 * sectors through the St layer exactly as the game's movie player consumes
 * them, and proves the frame assembly is lossless:
 *
 *   1. feed frames 1..45 and check the layer assembled 45 whole frames, with
 *      frames 1..4 of the sector counts the disc gives (9, 9, 9, 8) and frame
 *      40's header (frameCount 40, 320x416, 8 sectors);
 *   2. drain frames 1..39 through StGetNext/StFreeRing and compare frame 40's
 *      assembled payload, byte for byte, with the direct concatenation of its
 *      video-sector payloads;
 *   3. decode both with the T37 decoder (shim/host_mdec.c) and require
 *      byte-identical RGB24 -- that is the proof the assembly loses nothing;
 *   4. repeat with the game's real ring size (32 sectors) and feed until the
 *      layer refuses a sector: three 9-sector frames fill the ring, the first
 *      sector of frame 4 is held back, freeing one frame lets it in (wrapping
 *      the record to offset 0), and the wrapped frame 4 decodes identically to
 *      the direct read. Nothing is overwritten while it is still owned.
 *
 * There is no window, no GPU and no PsyCross: this is the stream layer alone.
 * Exit 0: every assertion held. Exit 1: one failed (the message names it).
 *
 * Usage: sttest [file.str] [frame]
 * The defaults are disks/eu/AAA/STR/MOVIEOPN.STR and frame 40.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim/host_st.h"
#include "shim/host_mdec.h"

#define STR_SECTOR      2048
#define STR_HEADER      32
#define STR_PAYLOAD     2016
#define STR_ID          0x0160u
#define STR_VIDEO       0x8001u
#define STR_MAX_SECTORS 32
#define STR_MAX_FRAMES  2048

#define RING_SECTORS    512 /* 1 MiB: holds the 45 frames this test feeds */
#define SMALL_SECTORS   32  /* the game's own StSetRing(ring, 32) */

/* libcd's St* surface, as PsyCross's libcd.h declares it (u_long is the
   32-bit u_int here, see pc/ps1_types.h). Declared locally so this harness
   needs no PsyQ or PsyCross header. */
extern void StSetRing(unsigned int *ring_addr, unsigned int ring_size);
extern void StSetStream(unsigned int mode, unsigned int start_frame,
                        unsigned int end_frame, void (*func1)(),
                        void (*func2)());
extern void StUnSetRing(void);
extern unsigned int StFreeRing(unsigned int *base);
extern unsigned int StGetNext(unsigned int **addr, unsigned int **header);

/* 1 MiB and 64 KiB, aligned for the u_int* the SDK hands out. */
static unsigned int bigRing[RING_SECTORS * STR_SECTOR / sizeof(unsigned int)];
static unsigned int smallRing[SMALL_SECTORS * STR_SECTOR / sizeof(unsigned int)];

static int expectedNsec[STR_MAX_FRAMES + 2];

static unsigned read16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long read32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void fail(const char *what) {
    fprintf(stderr, "sttest: FAIL %s\n", what);
    exit(1);
}

static unsigned char *readFile(const char *path, long *size) {
    FILE *file = fopen(path, "rb");
    unsigned char *data;
    long n;

    if (file == NULL) {
        fprintf(stderr, "sttest: cannot open %s\n", path);
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

static int isVideo(const unsigned char *sector) {
    return read16(sector + 0) == STR_ID && read16(sector + 2) == STR_VIDEO;
}

/*
 * The direct read the mdectest uses: every video sector of `frame`, payloads
 * concatenated in sector order, the 32-byte StHEADER stripped. Used as the
 * reference the St layer's assembly must match.
 */
static unsigned char *directPayload(const unsigned char *file, long size,
                                    unsigned long frame, int *nsecOut) {
    const unsigned char *sector[STR_MAX_SECTORS];
    unsigned char *payload;
    size_t used = 0;
    long at;
    int nsec = 0;
    int found = 0;
    int sec;

    for (sec = 0; sec < STR_MAX_SECTORS; sec++) {
        sector[sec] = NULL;
    }
    for (at = 0; at + STR_SECTOR <= size; at += STR_SECTOR) {
        const unsigned char *s = file + at;
        int index;

        if (!isVideo(s) || read32(s + 8) != frame) {
            continue;
        }
        index = (int)read16(s + 4);
        if (index < 0 || index >= STR_MAX_SECTORS) {
            fail("direct read: sector index out of range");
        }
        if (!found) {
            nsec = (int)read16(s + 6);
            if (nsec <= 0 || nsec > STR_MAX_SECTORS) {
                fail("direct read: bad sector count");
            }
            found = 1;
        }
        sector[index] = s + STR_HEADER;
    }
    if (!found) {
        fail("direct read: frame not found");
    }
    payload = malloc((size_t)nsec * STR_PAYLOAD);
    if (payload == NULL) {
        fail("direct read: payload allocation");
    }
    for (sec = 0; sec < nsec; sec++) {
        if (sector[sec] == NULL) {
            fail("direct read: missing sector");
        }
        memcpy(payload + used, sector[sec], STR_PAYLOAD);
        used += STR_PAYLOAD;
    }
    *nsecOut = nsec;
    return payload;
}

/* Decodes one assembled payload with the T37 decoder into RGB24. */
static void decodeFrame(const unsigned char *payload, int width, int height,
                        unsigned char *rgb) {
    HostMdec mdec;
    HostMdecStats stats;
    unsigned int *stream;
    int count = (int)read16(payload);

    if (count <= 0) {
        fail("frame word count is zero");
    }
    stream = malloc(4u + (size_t)count * 4u);
    if (stream == NULL) {
        fail("run-level allocation");
    }
    hostMdecInit(&mdec);
    if (hostMdecVlc2(payload, stream, &mdec.vlc) != 0) {
        fprintf(stderr, "sttest: bad Huffman code at bit %d, %d tokens\n",
                mdec.vlc.lastErrorBit, mdec.vlc.lastTokens);
        fail("VLC decode");
    }
    if (hostMdecDecodeFrame(&mdec, stream, width, height,
                            HMDEC_DEPTH_COMMAND, rgb, &stats) != 0) {
        fail("frame decode");
    }
    if (stats.macroblocks != (width / 16) * (height / 16)
        || stats.blocks != stats.macroblocks * 6
        || stats.eobBlocks != stats.blocks || stats.errors != 0) {
        fprintf(stderr, "sttest: blocks %d/%d EOB %d errors %d\n",
                stats.blocks, stats.macroblocks * 6, stats.eobBlocks,
                stats.errors);
        fail("frame structure");
    }
    free(stream);
}

/* Takes one frame from the St layer and checks the header the game reads. */
static unsigned int *takeFrame(unsigned long frame, int nsec, int width,
                               int height) {
    unsigned int *addr = NULL;
    unsigned int *header = NULL;

    if (StGetNext(&addr, &header) != 0) {
        fprintf(stderr, "sttest: no frame ready, wanted %lu\n", frame);
        fail("StGetNext");
    }
    if (read32((const unsigned char *)header + 8) != frame) {
        fprintf(stderr, "sttest: header frameCount %lu, wanted %lu\n",
                read32((const unsigned char *)header + 8), frame);
        fail("frame order");
    }
    if (read16((const unsigned char *)header + 16) != (unsigned)width
        || read16((const unsigned char *)header + 18) != (unsigned)height) {
        fail("frame dimensions");
    }
    if (read16((const unsigned char *)header + 6) != (unsigned)nsec) {
        fprintf(stderr, "sttest: frame %lu has %u sectors, expected %d\n",
                frame, read16((const unsigned char *)header + 6), nsec);
        fail("sector count");
    }
    return addr;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1]
                                  : "disks/eu/AAA/STR/MOVIEOPN.STR";
    unsigned long wanted = (argc > 2) ? (unsigned long)atoi(argv[2]) : 40;
    const unsigned long feedFrames = 45;
    unsigned char *file;
    unsigned char *assembled;
    unsigned char *direct;
    unsigned char *rgbA;
    unsigned char *rgbD;
    unsigned int *addr;
    HostStStats stats;
    long size;
    long at;
    long fedSectors = 0;
    long skipped = 0;
    long expectedFed = 0;
    int nsecWanted;
    int width = 0;
    int height = 0;
    int f;
    int i;
    const int pixelBytes = 320 * 416 * 3;

    if (wanted < 5 || wanted > feedFrames) {
        fprintf(stderr, "sttest: frame must be 5..45 (fed frames)\n");
        exit(1);
    }
    file = readFile(path, &size);
    if (size < STR_SECTOR) {
        fail("input shorter than one sector");
    }

    /* The sector counts the disc gives, for every frame this test touches. */
    for (at = 0; at + STR_SECTOR <= size; at += STR_SECTOR) {
        const unsigned char *s = file + at;
        unsigned long frame;

        if (!isVideo(s)) {
            continue;
        }
        frame = read32(s + 8);
        if (frame < STR_MAX_FRAMES && expectedNsec[frame] == 0) {
            expectedNsec[frame] = (int)read16(s + 6);
        }
    }
    if (expectedNsec[1] != 9 || expectedNsec[2] != 9
        || expectedNsec[3] != 9 || expectedNsec[4] != 8) {
        fprintf(stderr, "sttest: first frames are %d/%d/%d/%d sectors\n",
                expectedNsec[1], expectedNsec[2], expectedNsec[3],
                expectedNsec[4]);
        fail("first-frame sector counts (disc changed?)");
    }
    for (f = 1; f <= (int)feedFrames; f++) {
        expectedFed += expectedNsec[f];
    }

    /* --- phase 1: feed 45 frames into a ring large enough for all of them - */

    StSetRing(bigRing, RING_SECTORS);
    StSetStream(1, 1, (unsigned int)-1, 0, 0);

    for (at = 0; at + STR_SECTOR <= size; at += STR_SECTOR) {
        const unsigned char *s = file + at;
        int fed;

        if (isVideo(s) && read32(s + 8) > feedFrames) {
            break;
        }
        fed = hostStFeedSector(s);
        if (isVideo(s)) {
            if (read32(s + 8) == 1 && read16(s + 4) == 0) {
                width = (int)read16(s + 16);
                height = (int)read16(s + 18);
            }
            if (fed != 1) {
                fail("a video sector was not accepted");
            }
            fedSectors++;
        } else {
            if (fed != 0) {
                fail("a non-video sector was accepted");
            }
            skipped++;
        }
    }

    hostStGetStats(&stats);
    printf("sttest: fed %ld sectors (%ld skipped), %ld frames assembled\n",
           fedSectors, skipped, stats.framesAssembled);
    if (stats.framesAssembled != (long)feedFrames) {
        fail("frame assembly count");
    }
    if (fedSectors != expectedFed) {
        fprintf(stderr, "sttest: %ld video sectors for frames 1..45, "
                "expected %ld\n", fedSectors, expectedFed);
        fail("video sector count");
    }
    if (width != 320 || height != 416) {
        fail("movie dimensions");
    }

    /* Frames 1..wanted-1 drain in order; frame wanted is the one inspected. */
    for (f = 1; f < (int)wanted; f++) {
        addr = takeFrame((unsigned long)f, expectedNsec[f], width, height);
        if (StFreeRing(addr) != 1) {
            fail("StFreeRing (drained frame)");
        }
    }
    if (expectedNsec[4] != 8) {
        fail("frame 4 sector count");
    }

    addr = takeFrame(wanted, expectedNsec[wanted], width, height);
    assembled = (unsigned char *)addr;
    nsecWanted = expectedNsec[wanted];
    if (wanted == 40 && nsecWanted != 8) {
        fprintf(stderr, "sttest: frame 40 has %d sectors, expected 8\n",
                nsecWanted);
        fail("frame 40 sector count");
    }

    direct = directPayload(file, size, wanted, &i);
    if (i != nsecWanted) {
        fail("direct sector count differs from the stream's");
    }
    if (memcmp(assembled, direct, (size_t)nsecWanted * STR_PAYLOAD) != 0) {
        fail("assembled payload differs from the direct concatenation");
    }
    printf("sttest: frame %lu: %dx%d, %d sectors, payload %ld bytes identical "
           "to the direct read\n", wanted, width, height, nsecWanted,
           (long)nsecWanted * STR_PAYLOAD);

    rgbA = malloc((size_t)pixelBytes);
    rgbD = malloc((size_t)pixelBytes);
    if (rgbA == NULL || rgbD == NULL) {
        fail("pixel allocation");
    }
    decodeFrame(assembled, width, height, rgbA);
    decodeFrame(direct, width, height, rgbD);
    if (memcmp(rgbA, rgbD, (size_t)pixelBytes) != 0) {
        fail("assembled frame decodes differently from the direct read");
    }
    printf("sttest: decoded RGB24 %d bytes identical (assembled vs direct)\n",
           pixelBytes);
    if (StFreeRing(addr) != 1) {
        fail("StFreeRing (inspected frame)");
    }

    /* --- phase 2: the game's ring, backpressure and wrap ------------------ */

    StUnSetRing();
    StSetRing(smallRing, SMALL_SECTORS);
    StSetStream(1, 1, (unsigned int)-1, 0, 0);

    at = 0;
    while (at + STR_SECTOR <= size) {
        const unsigned char *s = file + at;
        int fed;

        if (isVideo(s) && read32(s + 8) > 4) {
            break;
        }
        fed = hostStFeedSector(s);
        if (isVideo(s) && fed == 0) {
            break; /* the ring is full: this sector was held back */
        }
        at += STR_SECTOR;
    }
    hostStGetStats(&stats);
    printf("sttest: 32-sector ring: %ld frames assembled before backpressure\n",
           stats.framesAssembled);
    if (stats.framesAssembled != 3) {
        fail("backpressure: expected three frames in the game ring");
    }
    if (at + STR_SECTOR > size || !isVideo(file + at)
        || read32(file + at + 8) != 4 || read16(file + at + 4) != 0) {
        fail("backpressure: the refused sector is not frame 4 sector 0");
    }

    addr = takeFrame(1, expectedNsec[1], width, height);
    if (StFreeRing(addr) != 1) {
        fail("StFreeRing (ring frame 1)");
    }
    if (hostStFeedSector(file + at) != 1) {
        fail("the held-back sector was not accepted after a free");
    }
    at += STR_SECTOR;
    while (at + STR_SECTOR <= size) {
        const unsigned char *s = file + at;

        if (isVideo(s) && read32(s + 8) > 4) {
            break;
        }
        if (isVideo(s) && hostStFeedSector(s) != 1) {
            fail("frame 4 sector refused after the wrap");
        }
        at += STR_SECTOR;
    }

    for (f = 2; f <= 4; f++) {
        addr = takeFrame((unsigned long)f, expectedNsec[f], width, height);
        if (f < 4) {
            if (StFreeRing(addr) != 1) {
                fail("StFreeRing (ring drain)");
            }
            continue;
        }
        /* Frame 4's record wrapped to offset 0; its payload is at ring+32. */
        direct = directPayload(file, size, 4, &i);
        if (i != expectedNsec[4]) {
            fail("frame 4 direct sector count");
        }
        if (memcmp((unsigned char *)addr, direct,
                   (size_t)i * STR_PAYLOAD) != 0) {
            fail("wrapped frame 4 payload differs from the direct read");
        }
        decodeFrame((const unsigned char *)addr, width, height, rgbA);
        decodeFrame(direct, width, height, rgbD);
        if (memcmp(rgbA, rgbD, (size_t)pixelBytes) != 0) {
            fail("wrapped frame 4 decodes differently");
        }
        printf("sttest: wrapped frame 4 (%d sectors) identical to the direct "
               "read\n", i);
        if (StFreeRing(addr) != 1) {
            fail("StFreeRing (wrapped frame 4)");
        }
    }

    hostStGetStats(&stats);
    printf("sttest: ring frames assembled %ld, delivered %ld, live bytes "
           "released\n", stats.framesAssembled, stats.framesDelivered);
    if (stats.framesAssembled != 4 || stats.framesDelivered != 4) {
        fail("ring frame totals");
    }

    free(rgbD);
    free(rgbA);
    free(direct);
    free(file);
    printf("sttest: OK\n");
    return 0;
}
