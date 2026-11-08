/*
 * Host test of the movie export (content-export E2a): pc/shim/jpeg_write.c, avi_write.c and video_export.c. No disc, no window:
 * the disc is a synthetic raw image built here (video sectors that carry a DC-only MDEC stream, XA audio sectors of made-up
 * bytes), so no game data is involved (ADR-001).
 *
 * This program checks what C can check (frame and sample counts, the job's manifest and .new rule, skips, bad input) and writes
 * the files the independent half needs under argv[1]: pc/test_fixtures/video/check_video.py decodes every JPEG with PIL, compares
 * it with the raw picture the C side kept (PSNR), parses the AVI's RIFF structure (sizes, idx1, stream headers, interleave) and
 * compares the audio chunks with the samples the C side kept.
 *
 * Usage: videotest <scratch dir>. Exit 0: every check held. Exit 1: at least one failed (printed to stdout).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "shim/avi_read.h"
#include "shim/avi_write.h"
#include "shim/host_mdec.h"
#include "shim/host_xa.h"
#include "shim/jpeg_read.h"
#include "shim/jpeg_write.h"
#include "shim/mods.h"
#include "shim/text_data.h"
#include "shim/video_export.h"
#include "shim/video_mod.h"

#ifndef _WIN32
#include <unistd.h>
#endif

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "videotest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gDir[1024];
static FILE *gManifest;

static void path(char *out, size_t cap, const char *name) { snprintf(out, cap, "%s/%s", gDir, name); }

static int writeFile(const char *name, const void *data, size_t len) {
    char p[1300];
    FILE *f;
    path(p, sizeof p, name);
    f = fopen(p, "wb");
    if (f == NULL) return -1;
    if (len && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    return fclose(f);
}

static long fileSize(const char *p) {
    FILE *f = fopen(p, "rb");
    long n;
    if (f == NULL) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static int exists(const char *p) { return fileSize(p) >= 0; }

/* ---- synthetic pictures ---- */

/* A picture with a smooth gradient, a hard-edged coloured box and a circle, shifted by k (so the frames of a run differ) */
static void picture(uint8_t *rgb, int w, int h, int k) {
    int x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint8_t *p = rgb + ((size_t)y * w + x) * 3;
            int dx = x - (w / 2 + k), dy = y - h / 2;
            p[0] = (uint8_t)((x * 255) / (w > 1 ? w - 1 : 1));
            p[1] = (uint8_t)((y * 255) / (h > 1 ? h - 1 : 1));
            p[2] = (uint8_t)(128 + 100 * ((x + y + k) % 64 < 32 ? 1 : -1) / 2);
            if (x >= w / 8 + k && x < w / 3 + k && y >= h / 8 && y < h / 3) {
                p[0] = 220; p[1] = 30; p[2] = 40;
            }
            if (dx * dx + dy * dy < (h / 6) * (h / 6)) {
                p[0] = 20; p[1] = 200; p[2] = 60;
            }
        }
    }
}

static void jpegCase(const char *tag, int w, int h, int q, int k, double minPsnr, int gray) {
    size_t n = (size_t)w * h * 3, cap = jpegMaxSize(w, h), len;
    uint8_t *rgb = (uint8_t *)malloc(n), *out = (uint8_t *)malloc(cap ? cap : 1);
    char rgbName[64], jpgName[64];
    snprintf(rgbName, sizeof rgbName, "%s.rgb", tag);
    snprintf(jpgName, sizeof jpgName, "%s.jpg", tag);
    picture(rgb, w, h, k);
    if (gray) { /* R = G = B: no chroma, so what is measured is the DCT, the quantiser and the Huffman coding alone */
        size_t i;
        for (i = 0; i < (size_t)w * h; i++) rgb[i * 3 + 1] = rgb[i * 3 + 2] = rgb[i * 3];
    }
    CHECK(cap >= 1024);
    len = jpegEncodeRgb24(rgb, w, h, q, out, cap);
    CHECK(len > 600 && len <= cap);
    CHECK(len >= 4 && out[0] == 0xFF && out[1] == 0xD8 && out[len - 2] == 0xFF && out[len - 1] == 0xD9);
    CHECK(len == 0 || jpegEncodeRgb24(rgb, w, h, q, out, len - 1) == 0); /* too small a buffer is refused, not overrun */
    CHECK(writeFile(rgbName, rgb, n) == 0 && writeFile(jpgName, out, len) == 0);
    fprintf(gManifest, "jpeg %s %s %d %d %.1f %d\n", rgbName, jpgName, w, h, minPsnr, q);
    free(rgb);
    free(out);
}

static void testJpeg(void) {
    uint8_t px[3] = {1, 2, 3};
    uint8_t buf[4096];
    /* the floors are absolute; check_video.py also holds each one to PIL's own encoder at the same quality (within 1 dB) */
    jpegCase("j_movie", 320, 416, 85, 0, 30.0, 0);
    jpegCase("j_movie_q50", 320, 416, 50, 5, 27.0, 0);
    jpegCase("j_q100", 128, 96, 100, 3, 30.0, 0);
    jpegCase("j_odd", 33, 17, 90, 0, 22.0, 0);
    jpegCase("j_small", 16, 16, 85, 0, 21.0, 0);
    jpegCase("j_gray85", 320, 416, 85, 0, 38.0, 1);
    jpegCase("j_gray100", 128, 96, 100, 3, 46.0, 1);
    CHECK(jpegEncodeRgb24(px, 0, 1, 85, buf, sizeof buf) == 0);
    CHECK(jpegEncodeRgb24(px, 1, 1, 0, buf, sizeof buf) == 0);
    CHECK(jpegEncodeRgb24(NULL, 1, 1, 85, buf, sizeof buf) == 0);
    CHECK(jpegEncodeRgb24(px, 1, 1, 85, buf, sizeof buf) > 100); /* a one pixel picture is still a whole JPEG */
}

/* ---- the muxer ---- */

static void testAvi(void) {
    enum { W = 320, H = 416, FRAMES = 25 };
    uint8_t *rgb = (uint8_t *)malloc((size_t)W * H * 3), *jpg = (uint8_t *)malloc(jpegMaxSize(W, H));
    int16_t pcm[2520 * 2];
    FILE *ref, *audio;
    AviWriter *w;
    AviParams p;
    char fn[1300];
    int k, i, bytes = 0;
    size_t len;

    memset(&p, 0, sizeof p);
    p.width = W; p.height = H; p.fps = 15; p.channels = 2; p.sampleRate = 37800;
    path(fn, sizeof fn, "mux.avi");
    w = aviOpen(fn, &p);
    CHECK(w != NULL);
    path(fn, sizeof fn, "mux.frames.rgb");
    ref = fopen(fn, "wb");
    path(fn, sizeof fn, "mux.pcm");
    audio = fopen(fn, "wb");
    for (k = 0; w != NULL && ref != NULL && audio != NULL && k < FRAMES; k++) {
        picture(rgb, W, H, k * 3);
        len = jpegEncodeRgb24(rgb, W, H, 85, jpg, jpegMaxSize(W, H));
        for (i = 0; i < 2520; i++) {
            pcm[i * 2] = (int16_t)(8000 * ((i / 40) % 2 ? 1 : -1));
            pcm[i * 2 + 1] = (int16_t)(k * 100 - i);
        }
        CHECK(aviAddAudio(w, pcm, 2520) == 0);
        CHECK(aviAddVideo(w, jpg, len) == 0);
        fwrite(rgb, 1, (size_t)W * H * 3, ref);
        fwrite(pcm, 1, sizeof pcm, audio);
        bytes += (int)len;
    }
    CHECK(w != NULL && aviVideoFrames(w) == FRAMES && aviAudioFrames(w) == 2520L * FRAMES);
    CHECK(w != NULL && aviClose(w) == 0);
    if (ref) fclose(ref);
    if (audio) fclose(audio);
    path(fn, sizeof fn, "mux.avi");
    CHECK(fileSize(fn) > bytes);
    fprintf(gManifest, "avi mux.avi %d 2 %d %d mux.pcm mux.frames.rgb 2520\n", FRAMES, W, H);

    /* structure only: a long file of tiny chunks (idx1 at the scale of a whole movie), no audio, odd chunk sizes (padding) */
    memset(&p, 0, sizeof p);
    p.width = 64; p.height = 32; p.fps = 15;
    path(fn, sizeof fn, "long.avi");
    w = aviOpen(fn, &p);
    CHECK(w != NULL);
    for (k = 0; w != NULL && k < 3000; k++) {
        CHECK(aviAddVideo(w, jpg, (size_t)(100 + (k % 7))) == 0);
    }
    CHECK(w != NULL && aviClose(w) == 0);
    fprintf(gManifest, "avi long.avi 3000 0 64 32 - -\n");

    /* bad parameters, a path that cannot be created, an abort leaves nothing */
    p.width = 0;
    CHECK(aviOpen(fn, &p) == NULL);
    p.width = 64; p.channels = 3;
    CHECK(aviOpen(fn, &p) == NULL);
    p.channels = 0;
    path(fn, sizeof fn, "no_such_dir/x.avi");
    CHECK(aviOpen(fn, &p) == NULL);
    path(fn, sizeof fn, "abort.avi");
    w = aviOpen(fn, &p);
    CHECK(w != NULL);
    if (w != NULL) {
        aviAddVideo(w, jpg, 100);
        aviAbort(w);
    }
    CHECK(!exists(fn));
    free(rgb);
    free(jpg);
}

static uint32_t rd32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

/* The display aspect (OpenDML vprp): the writer declares it in the video strl without moving the stored pixels, and every
   size and idx1 offset stays right; a writer with no aspect writes no vprp. */
static void testAviAspect(void) {
    enum { FR = 5 };
    AviParams p;
    AviWriter *w;
    char fn[1300];
    uint8_t jpg[300];
    uint8_t *f;
    long n;
    int pass, k;

    memset(jpg, 0xAB, sizeof jpg);
    for (pass = 0; pass < 2; pass++) {
        size_t pos, hdrlEnd, strl, vprp = 0, moviAt = 0, idx = 0;
        int nIdx = 0;

        memset(&p, 0, sizeof p);
        p.width = 320; p.height = 416; p.fps = 15; p.channels = pass ? 0 : 1; p.sampleRate = 22050;
        if (pass == 0) { p.aspectW = 20; p.aspectH = 13; }
        path(fn, sizeof fn, "aspect.avi");
        w = aviOpen(fn, &p);
        CHECK(w != NULL);
        for (k = 0; w != NULL && k < FR; k++) {
            int16_t pcm[100] = {0};
            if (pass == 0) CHECK(aviAddAudio(w, pcm, 100) == 0);
            CHECK(aviAddVideo(w, jpg, (size_t)(101 + k)) == 0);
        }
        CHECK(w != NULL && aviClose(w) == 0);
        n = fileSize(fn);
        f = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
        if (n <= 0 || f == NULL) { CHECK(0); free(f); continue; }
        { FILE *fp = fopen(fn, "rb"); CHECK(fp != NULL && fread(f, 1, (size_t)n, fp) == (size_t)n); if (fp) fclose(fp); }
        CHECK(memcmp(f, "RIFF", 4) == 0 && rd32(f + 4) == (uint32_t)(n - 8) && memcmp(f + 8, "AVI ", 4) == 0);
        CHECK(memcmp(f + 12, "LIST", 4) == 0 && memcmp(f + 20, "hdrl", 4) == 0);
        hdrlEnd = 20 + rd32(f + 16);
        CHECK(hdrlEnd < (size_t)n && memcmp(f + hdrlEnd, "LIST", 4) == 0 && memcmp(f + hdrlEnd + 8, "movi", 4) == 0);
        /* the first strl is the video stream; walk it chunk by chunk */
        strl = 24 + 8 + rd32(f + 28); /* after avih */
        CHECK(memcmp(f + strl, "LIST", 4) == 0 && memcmp(f + strl + 8, "strl", 4) == 0);
        {
            size_t end = strl + 8 + rd32(f + strl + 4);
            CHECK(end <= hdrlEnd);
            for (pos = strl + 12; pos + 8 <= end; pos += 8 + rd32(f + pos + 4) + (rd32(f + pos + 4) & 1)) {
                if (memcmp(f + pos, "vprp", 4) == 0) vprp = pos;
            }
            CHECK(pos == end); /* the chunks tile the strl exactly */
        }
        if (pass == 0) {
            const uint8_t *v;
            CHECK(vprp != 0);
            if (vprp != 0) {
                v = f + vprp + 8;
                CHECK(rd32(f + vprp + 4) == 36 + 32);
                CHECK(rd32(v + 20) == ((20u << 16) | 13u)); /* dwFrameAspectRatio */
                CHECK(rd32(v + 24) == 320 && rd32(v + 28) == 416); /* the stored frame */
                CHECK(rd32(v + 32) == 1);                           /* one field: progressive */
            }
        } else {
            CHECK(vprp == 0);
        }
        /* idx1: after the movi list, every offset (from the 'movi' fourcc) lands on the chunk it names */
        moviAt = hdrlEnd + 8;
        {
            size_t moviEnd = hdrlEnd + 8 + rd32(f + hdrlEnd + 4);
            CHECK(memcmp(f + moviEnd, "idx1", 4) == 0 && moviEnd + 8 + rd32(f + moviEnd + 4) == (size_t)n);
            idx = moviEnd + 8;
            nIdx = (int)(rd32(f + moviEnd + 4) / 16);
        }
        CHECK(nIdx == (pass == 0 ? 2 * FR : FR));
        for (k = 0; k < nIdx; k++) {
            const uint8_t *e = f + idx + (size_t)k * 16;
            size_t at = moviAt + rd32(e + 8);
            CHECK(at + 8 <= (size_t)n && memcmp(f + at, e, 4) == 0 && rd32(f + at + 4) == rd32(e + 12));
        }
        free(f);
    }
}

/* ---- the synthetic disc ---- */

#define SECTOR 2352

typedef struct SynDisc {
    uint8_t *data;
    long sectors, cap;
    long failAt; /* a read at or past this lba fails (-1: never) */
} SynDisc;

static uint8_t *newSector(SynDisc *d) {
    if (d->sectors == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 1024;
        d->data = (uint8_t *)realloc(d->data, (size_t)d->cap * SECTOR);
    }
    memset(d->data + (size_t)d->sectors * SECTOR, 0, SECTOR);
    return d->data + (size_t)d->sectors++ * SECTOR;
}

static void rawHead(uint8_t *s, long lba, int file, int chan, int submode, int coding) {
    int i;
    long t = lba + 150;
    s[0] = 0;
    for (i = 1; i < 11; i++) s[i] = 0xFF;
    s[11] = 0;
    s[12] = (uint8_t)((t / 4500 / 10) << 4 | (t / 4500 % 10));
    s[13] = (uint8_t)((t / 75 % 60 / 10) << 4 | (t / 75 % 60 % 10));
    s[14] = (uint8_t)((t % 75 / 10) << 4 | (t % 75 % 10));
    s[15] = 2;
    s[16] = s[20] = (uint8_t)file;
    s[17] = s[21] = (uint8_t)chan;
    s[18] = s[22] = (uint8_t)submode;
    s[19] = s[23] = (uint8_t)coding;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned long v) { put16(p, (unsigned)(v & 0xFFFF)); put16(p + 2, (unsigned)(v >> 16)); }

/* The Huffman data of a DC-only frame: per block the 10-bit DC and the EOB code "10"; the 16-bit words are written MSB first */
typedef struct Bits { uint8_t *p; long n; } Bits;

static void putBits(Bits *b, unsigned v, int n) {
    while (n-- > 0) {
        long word = b->n >> 4;
        int sub = (int)(b->n & 15);
        unsigned cur = (unsigned)b->p[word * 2] | ((unsigned)b->p[word * 2 + 1] << 8);
        if ((v >> n) & 1) cur |= 1u << (15 - sub);
        b->p[word * 2] = (uint8_t)cur;
        b->p[word * 2 + 1] = (uint8_t)(cur >> 8);
        b->n++;
    }
}

static int dcOf(int m, int b, int k) {
    switch (b) {
    case 0: return ((m * 13 + k * 5) % 140) - 70;       /* Cr */
    case 1: return ((m * 29 + k * 3) % 140) - 70;       /* Cb */
    default: return ((m * 37 + b * 61 + k * 9) % 380) - 190; /* Y */
    }
}

/* One frame's payload: u16 words, quant, version 2, then the Huffman bits. Returns its length in bytes (a whole number of words). */
static size_t mdecFrame(uint8_t *out, int w, int h, int k) {
    int mbs = (w / 16) * (h / 16), m, b;
    Bits bits;
    size_t bytes;
    memset(out, 0, 8 + (size_t)mbs * 6 * 2 * 2 + 16);
    bits.p = out + 8;
    bits.n = 0;
    for (m = 0; m < mbs; m++) {
        for (b = 0; b < 6; b++) {
            putBits(&bits, (unsigned)dcOf(m, b, k) & 0x3FFu, 10);
            putBits(&bits, 2, 2); /* EOB */
        }
    }
    bytes = 8 + (size_t)((bits.n + 15) / 16) * 2;
    bytes = (bytes + 3) & ~(size_t)3;
    put16(out, (unsigned)mbs * 6); /* run-level words after the command word: 2 tokens per block */
    put16(out + 2, 0x3800);
    put16(out + 4, 1);
    put16(out + 6, 2);
    return bytes;
}

/* The picture the decoder makes of frame k, as RGB24 (the reference the JPEG is compared with) */
static void refFrame(uint8_t *rgb, int w, int h, int k) {
    static uint8_t pay[1 << 18], run[0x40100];
    HostMdec mdec;
    HostMdecStats st;
    hostMdecInit(&mdec);
    mdecFrame(pay, w, h, k);
    CHECK(hostMdecVlc2(pay, run, &mdec.vlc) == 0);
    CHECK(hostMdecDecodeFrame(&mdec, run, w, h, HMDEC_DEPTH_24BPP, rgb, &st) == 0 && st.errors == 0);
}

static void videoSectors(SynDisc *d, int w, int h, int k, int file) {
    static uint8_t pay[1 << 18];
    size_t len = mdecFrame(pay, w, h, k), at = 0;
    int nsec = (int)((len + 2015) / 2016), s;
    for (s = 0; s < nsec; s++) {
        long lba = d->sectors;
        uint8_t *sec = newSector(d), *u = sec + 24;
        size_t n = len - at < 2016 ? len - at : 2016;
        rawHead(sec, lba, file, 1, 0x08, 0);
        put16(u + 0, 0x0160);
        put16(u + 2, 0x8001);
        put16(u + 4, (unsigned)s);
        put16(u + 6, (unsigned)nsec);
        put32(u + 8, (unsigned long)k + 1);
        put32(u + 12, (unsigned long)len);
        put16(u + 16, (unsigned)w);
        put16(u + 18, (unsigned)h);
        memcpy(u + 32, pay + at, n);
        at += n;
    }
}

static uint32_t gSeed = 12345;
static uint32_t rnd(void) { gSeed = gSeed * 1664525u + 1013904223u; return gSeed >> 8; }

static void audioSector(SynDisc *d, int file, int chan, int stereo, FILE *refPcm) {
    long lba = d->sectors;
    uint8_t *sec = newSector(d);
    SndXaState st;
    static short pcm[SND_XA_SECTOR_SAMPLES];
    int g, i, n;
    static SndXaState keep[8];
    rawHead(sec, lba, file, chan, 0x64, stereo ? 0x01 : 0x00);
    for (g = 0; g < SND_XA_GROUPS; g++) {
        uint8_t *grp = sec + 24 + g * 128;
        for (i = 4; i < 12; i++) grp[i] = (uint8_t)(0x0A + (rnd() & 3) + ((rnd() & 1) << 4)); /* shift 10..13, filter 0/1 */
        for (i = 16; i < 128; i++) grp[i] = (uint8_t)rnd();
    }
    (void)st;
    n = sndXaDecodeAudio(sec, SECTOR, pcm, SND_XA_SECTOR_SAMPLES, &keep[file & 7]);
    CHECK(n == SND_XA_SECTOR_SAMPLES);
    if (refPcm != NULL) fwrite(pcm, 2, (size_t)n, refPcm);
}

/* A movie of `frames` pictures, audio kept just ahead of the pictures (an interleave like the discs'): frameSamples per picture */
static void makeMovie(SynDisc *d, int file, int frames, int stereo, int audio, const char *name, long *lba, long *sectors) {
    char fn[64];
    FILE *pcm = NULL, *frm;
    long have = 0, per = stereo ? SND_XA_STEREO_FRAMES : SND_XA_MONO_FRAMES;
    uint8_t *rgb = (uint8_t *)malloc(320 * 416 * 3);
    int k;
    snprintf(fn, sizeof fn, "%s.frames.rgb", name);
    {
        char p[1300];
        path(p, sizeof p, fn);
        frm = fopen(p, "wb");
        if (audio) {
            snprintf(fn, sizeof fn, "%s.pcm", name);
            path(p, sizeof p, fn);
            pcm = fopen(p, "wb");
        }
    }
    *lba = d->sectors;
    for (k = 0; k < frames; k++) {
        videoSectors(d, 320, 416, k, file);
        refFrame(rgb, 320, 416, k);
        if (frm != NULL) fwrite(rgb, 1, 320 * 416 * 3, frm);
        while (audio && have < (long)(k + 1) * 2520) {
            audioSector(d, file, 1, stereo, pcm);
            have += per;
        }
    }
    *sectors = d->sectors - *lba;
    if (frm != NULL) fclose(frm);
    if (pcm != NULL) fclose(pcm);
    free(rgb);
}

typedef struct SynMovie { const char *name; long lba, sectors; } SynMovie;
static SynDisc gDisc;
static SynMovie gMovies[3];

static int ioMovie(void *ud, const char *name, long *lba, long *sectors) {
    int i;
    (void)ud;
    for (i = 0; i < 3; i++) {
        if (!strcmp(name, gMovies[i].name)) {
            *lba = gMovies[i].lba;
            *sectors = gMovies[i].sectors;
            return 1;
        }
    }
    return 0;
}

static int ioRead(void *ud, long lba, unsigned char *frame) {
    SynDisc *d = (SynDisc *)ud;
    if (lba < 0 || lba >= d->sectors || (d->failAt >= 0 && lba >= d->failAt)) return 0;
    memcpy(frame, d->data + (size_t)lba * SECTOR, SECTOR);
    return 1;
}

static void buildDisc(void) {
    static const int frames[3] = {60, 45, 30};
    static const char *const names[3] = {"MOVIED01", "MOVIED02", "MOVIEE01"};
    int i;
    newSector(&gDisc); /* a lead-in sector so lba 0 is not a movie */
    gDisc.failAt = -1;
    for (i = 0; i < 3; i++) {
        gMovies[i].name = names[i];
        makeMovie(&gDisc, i + 1, frames[i], i == 0, i != 2, names[i], &gMovies[i].lba, &gMovies[i].sectors);
    }
}

static VideoExportIo gIo = {ioMovie, ioRead, &gDisc};

static void testBuild(void) {
    VideoExportStats st;
    char err[256], fn[1300];
    int i;
    static const int frames[3] = {60, 45, 30};

    for (i = 0; i < 3; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s.avi", gMovies[i].name);
        path(fn, sizeof fn, name);
        memset(&st, 0, sizeof st);
        err[0] = '\0';
        CHECK(videoExportBuild(&gIo, gMovies[i].name, fn, &st, err, sizeof err, NULL, NULL) == 0);
        CHECK(st.width == 320 && st.height == 416 && st.frames == frames[i] && st.badFrames == 0);
        CHECK(st.channels == (i == 2 ? 0 : (i == 0 ? 2 : 1)));
        CHECK(i == 2 ? st.audioFrames == 0 : st.audioFrames >= 2520L * frames[i] && st.audioFrames < 2520L * frames[i] + 4032);
        CHECK(st.bytes == fileSize(fn) && st.bytes > 10000);
        {
            char pcm[96];
            snprintf(pcm, sizeof pcm, "%s.pcm", gMovies[i].name);
            fprintf(gManifest, "avi %s %d %d 320 416 %s %s.frames.rgb 2520\n", name, st.frames, st.channels,
                    st.channels ? pcm : "-", gMovies[i].name);
        }
    }
    path(fn, sizeof fn, "none.avi");
    CHECK(videoExportBuild(&gIo, "NOSUCH", fn, &st, err, sizeof err, NULL, NULL) == -1 && err[0] != '\0' && !exists(fn));
    /* a read that fails halfway: an error, and no file behind */
    gDisc.failAt = gMovies[0].lba + gMovies[0].sectors / 2;
    err[0] = '\0';
    CHECK(videoExportBuild(&gIo, "MOVIED01", fn, &st, err, sizeof err, NULL, NULL) == -1 && err[0] != '\0');
    CHECK(!exists(fn));
    gDisc.failAt = -1;
}

static int pctLast = -1, pctCalls;
static void onProgress(void *ud, int percent) {
    (void)ud;
    CHECK(percent >= pctLast && percent >= 0 && percent <= 100);
    pctLast = percent;
    pctCalls++;
}

static void testProgress(void) {
    VideoExportStats st;
    char err[256], fn[1300];
    path(fn, sizeof fn, "progress.avi");
    CHECK(videoExportBuild(&gIo, "MOVIEE01", fn, &st, err, sizeof err, onProgress, NULL) == 0);
    CHECK(pctCalls >= 2 && pctLast == 100);
}

/* ---- the job ---- */

static char *slurp(const char *rel, size_t *len) {
    char p[1300];
    uint8_t *d;
    size_t n;
    path(p, sizeof p, rel);
    if (tdReadFile(p, &d, &n)) return NULL;
    if (len) *len = n;
    return (char *)d;
}

static void drain(void) {
    int guard = 0;
    while (videoExportPump(1000) && guard++ < 100000) {
    }
}

static void testJob(void) {
    char dir[1300], f1[1500], f2[1500], text[64];
    int done, failed, total;
    char *man;
    FILE *f;

    path(dir, sizeof dir, "data");
    videoExportSetup(dir, &gIo);
    CHECK(videoExportRequest("movied01, nonsense, MOVIEE01.STR, MOVIED01.avi") == 2); /* the bad name and the repeat are dropped */
    drain();
    videoExportCounts(&done, &failed, &total);
    CHECK(done == 2 && failed == 0 && total == 2 && videoExportBuilt() == 2);
    CHECK(strstr(videoExportStatus(), "Done (2)") != NULL);
    snprintf(f1, sizeof f1, "%s/video/MOVIED01.avi", dir);
    snprintf(f2, sizeof f2, "%s/video/MOVIEE01.avi", dir);
    CHECK(exists(f1) && exists(f2));
    snprintf(f1, sizeof f1, "%s/video/MOVIED01.avi.part", dir);
    CHECK(!exists(f1)); /* the partial file is gone */
    man = slurp("data/video/manifest.json", NULL);
    CHECK(man != NULL && strstr(man, "MOVIED01.avi") != NULL && strstr(man, "\"MOVIED01\": ") != NULL && strstr(man, "MOVIED01/exporter") == NULL);
    free(man);

    /* the same request again: up to date, nothing built */
    CHECK(videoExportRequest("all") == 3); /* the three movies this disc has */
    drain();
    CHECK(videoExportBuilt() == 1 && strstr(videoExportStatus(), "Done (3)") != NULL); /* only MOVIED02 was missing */
    CHECK(videoExportRequest("all") == 3);
    drain();
    CHECK(videoExportBuilt() == 0);

    /* an edited file stays, the new one goes beside it as .new; unedited ones are replaced in place */
    snprintf(f1, sizeof f1, "%s/video/MOVIED01.avi", dir);
    f = fopen(f1, "ab");
    CHECK(f != NULL);
    if (f) {
        fputs("edited", f);
        fclose(f);
    }
    {
        long sizeBefore = fileSize(f1);
        CHECK(videoExportRequestEx("MOVIED01, MOVIED02", 1) == 2);
        drain();
        CHECK(videoExportBuilt() == 2);
        snprintf(f2, sizeof f2, "%s/video/MOVIED01.avi.new", dir);
        CHECK(exists(f2) && fileSize(f1) == sizeBefore);
        snprintf(f2, sizeof f2, "%s/video/MOVIED02.avi.new", dir);
        CHECK(!exists(f2));
    }

    /* an older exporter version in the manifest: the movie is built again */
    man = slurp("data/video/manifest.json", NULL);
    CHECK(man != NULL);
    if (man != NULL) {
        char *at = strstr(man, "\"MOVIEE01\": ");
        CHECK(at != NULL);
        if (at != NULL) {
            char *q = at + strlen("\"MOVIEE01\": ");
            CHECK(*q == '2' && (q[1] < '0' || q[1] > '9'));
            if (*q == '2') {
                *q = '1'; /* version 1 */
                {
                    char mp[1300];
                    path(mp, sizeof mp, "data/video/manifest.json");
                    CHECK(tdWriteFileAtomic(mp, man, strlen(man)) == 0);
                }
            }
        }
        free(man);
        videoExportSetup(dir, &gIo); /* the manifest is read again */
        CHECK(videoExportRequest("all") == 3);
        drain();
        CHECK(videoExportBuilt() == 1);
    }

    /* no data folder / no reader / nothing valid */
    videoExportSetup(NULL, &gIo);
    CHECK(videoExportRequest("all") == -1);
    videoExportSetup(dir, NULL);
    CHECK(videoExportRequest("all") == -1);
    videoExportSetup(dir, &gIo);
    CHECK(videoExportRequest("nonsense") == -1);
    CHECK(videoExportRequest(NULL) == -1);

    /* a failing disc: the movie fails, the others go on, no .part is left */
    path(dir, sizeof dir, "data2");
    videoExportSetup(dir, &gIo);
    gDisc.failAt = gMovies[1].lba + gMovies[1].sectors / 2;
    CHECK(videoExportRequest("all") == 3);
    drain();
    videoExportCounts(&done, &failed, &total);
    CHECK(done == 1 && failed == 2 && total == 3 && strstr(videoExportStatus(), "failed") != NULL); /* MOVIED01 is before the break */
    gDisc.failAt = -1;
    snprintf(f1, sizeof f1, "%s/video/MOVIED02.avi.part", dir);
    CHECK(!exists(f1));
    snprintf(f1, sizeof f1, "%s/video/MOVIED02.avi", dir);
    CHECK(!exists(f1));

    /* the worker thread job (the menu row): all three rebuilt, the status goes through "Exporting" to "Done" */
    path(dir, sizeof dir, "data3");
    videoExportSetup(dir, &gIo);
    CHECK(videoExportStart("all", 1) == 3);
    CHECK(videoExportActive());
    CHECK(videoExportStart("all", 1) == -1); /* one job at a time */
    {
        int guard = 0, sawExporting = 0;
        while (videoExportPump(1) && guard++ < 60000) {
            SDL_Delay(1); /* the worker is busy for seconds: do not spin */
            snprintf(text, sizeof text, "%s", videoExportStatus());
            if (strstr(text, "Exporting") != NULL) sawExporting = 1;
        }
        CHECK(sawExporting);
    }
    CHECK(!videoExportActive() && videoExportBuilt() == 3 && strstr(videoExportStatus(), "Done (3)") != NULL);
    snprintf(f1, sizeof f1, "%s/video/MOVIED01.avi", dir);
    CHECK(exists(f1));
    /* a shutdown in the middle of a job joins the worker and leaves no partial file */
    path(dir, sizeof dir, "data4");
    videoExportSetup(dir, &gIo);
    CHECK(videoExportStart("all", 1) == 3);
    videoExportShutdown();
    CHECK(!videoExportActive());
    snprintf(f1, sizeof f1, "%s/video/MOVIED01.avi.part", dir);
    CHECK(!exists(f1));
    videoExportSetup(NULL, NULL);
}


/* ---- E2b: the readers and the movie replacement ---- */

static double psnrOf(const uint8_t *a, const uint8_t *b, size_t n) {
    double err = 0.0;
    size_t i;
    for (i = 0; i < n; i++) {
        double d = (double)a[i] - (double)b[i];
        err += d * d;
    }
    return err == 0.0 ? 99.0 : 10.0 * log10(255.0 * 255.0 * (double)n / err);
}

static uint8_t *slurpAbs(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb");
    uint8_t *d;
    long n;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (uint8_t *)malloc((size_t)n + 1);
    if (d == NULL || fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (len != NULL) *len = (size_t)n;
    return d;
}

static uint8_t *slurpIn(const char *dir, const char *name, size_t *len) {
    char p[1400];
    snprintf(p, sizeof p, "%s/%s/%s", gDir, dir, name);
    return slurpAbs(p, len);
}

static int copyTo(const char *dstRel, const void *data, size_t len) {
    char p[1400], d[1400];
    char *slash;
    snprintf(p, sizeof p, "%s/%s", gDir, dstRel);
    snprintf(d, sizeof d, "%s", p);
    slash = strrchr(d, '/');
    if (slash != NULL) {
        *slash = '\0';
        tdMakeDirs(d);
    }
    {
        FILE *f = fopen(p, "wb");
        if (f == NULL) return -1;
        if (len && fwrite(data, 1, len, f) != len) {
            fclose(f);
            return -1;
        }
        return fclose(f);
    }
}

/* the standard output of the jpeg writer with its DHT segments cut out (what an AVI1 frame is) */
static size_t stripDht(const uint8_t *in, size_t len, uint8_t *out) {
    size_t pos = 2, n = 2;
    out[0] = in[0];
    out[1] = in[1];
    while (pos + 4 <= len && in[pos] == 0xFF) {
        size_t seg = ((size_t)in[pos + 2] << 8) | in[pos + 3];
        if (in[pos + 1] == 0xDA) {
            memcpy(out + n, in + pos, len - pos);
            return n + (len - pos);
        }
        if (in[pos + 1] != 0xC4) {
            memcpy(out + n, in + pos, seg + 2);
            n += seg + 2;
        }
        pos += seg + 2;
    }
    return 0;
}

static void testJpegRead(void) {
    static const struct { int w, h, q, gray; double floor; } cases[] = {
        {320, 416, 85, 0, 30.0}, {128, 96, 100, 0, 30.0}, {33, 17, 90, 0, 22.0}, {16, 16, 85, 0, 21.0}, {1, 1, 85, 0, 0.0}, {320, 416, 85, 1, 38.0},
    };
    size_t i;
    uint8_t *rgb, *jpg, *dec, *nodht;
    char err[200];
    int w, h, comps;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int cw = cases[i].w, ch = cases[i].h;
        size_t n = (size_t)cw * ch * 3, len;
        size_t k;
        rgb = (uint8_t *)malloc(n);
        jpg = (uint8_t *)malloc(jpegMaxSize(cw, ch));
        nodht = (uint8_t *)malloc(jpegMaxSize(cw, ch));
        picture(rgb, cw, ch, 2);
        if (cases[i].gray) {
            for (k = 0; k < (size_t)cw * ch; k++) rgb[k * 3 + 1] = rgb[k * 3 + 2] = rgb[k * 3];
        }
        len = jpegEncodeRgb24(rgb, cw, ch, cases[i].q, jpg, jpegMaxSize(cw, ch));
        CHECK(len > 0);
        dec = NULL;
        w = h = 0;
        err[0] = '\0';
        CHECK(jpegDecodeRgb24(jpg, len, &dec, &w, &h, err, sizeof err) == 0 && dec != NULL && w == cw && h == ch);
        if (dec != NULL) {
            CHECK(psnrOf(rgb, dec, n) >= cases[i].floor);
            if (cw > 1) {
                /* the same file without its DHT segments decodes to the same pixels (Annex K tables stand in) */
                size_t nl = stripDht(jpg, len, nodht);
                uint8_t *dec2 = NULL;
                CHECK(nl > 0 && nl < len);
                CHECK(jpegDecodeRgb24(nodht, nl, &dec2, &w, &h, NULL, 0) == 0 && dec2 != NULL && memcmp(dec, dec2, n) == 0);
                free(dec2);
            }
        }
        free(dec);
        CHECK(jpegReadInfo(jpg, len, &w, &h, &comps, err, sizeof err) == 0 && w == cw && h == ch && comps == (cases[i].gray ? 3 : 3));
        free(rgb);
        free(jpg);
        free(nodht);
    }

    /* the fixtures PIL's libjpeg wrote (gen_fixtures.py): 4:4:4, 4:2:2, 4:2:0, grey, restart markers, no DHT, and a refusal */
    {
        char idx[1400];
        FILE *f;
        char line[256];
        int seen = 0;
        snprintf(idx, sizeof idx, "%s/fixtures/index.txt", gDir);
        f = fopen(idx, "r");
        if (f == NULL) {
            printf("videotest: no fixtures (python3 with PIL not found): the PIL decode checks were skipped\n");
            return;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            char kind[16], name[64], rest[160];
            int fw, fh;
            double floor;
            if (sscanf(line, "%15s %63s %159[^\n]", kind, name, rest) < 2) continue;
            if (!strcmp(kind, "ok") && sscanf(rest, "%d %d %lf", &fw, &fh, &floor) == 3) {
                char jn[80], rn[80];
                size_t jl = 0, rl = 0;
                uint8_t *jp, *ref, *out = NULL;
                snprintf(jn, sizeof jn, "fixtures/%s.jpg", name);
                snprintf(rn, sizeof rn, "fixtures/%s.rgb", name);
                jp = slurpIn(".", jn, &jl);
                ref = slurpIn(".", rn, &rl);
                CHECK(jp != NULL && ref != NULL && rl == (size_t)fw * fh * 3);
                if (jp != NULL && ref != NULL) {
                    err[0] = '\0';
                    w = h = 0;
                    if (jpegDecodeRgb24(jp, jl, &out, &w, &h, err, sizeof err) != 0) {
                        printf("videotest: fixture %s: %s\n", name, err);
                        failures++;
                    } else {
                        double p = psnrOf(ref, out, rl);
                        CHECK(w == fw && h == fh);
                        if (p < floor) {
                            printf("videotest: fixture %s: PSNR %.2f below %.1f\n", name, p, floor);
                            failures++;
                        }
                    }
                    free(out);
                }
                free(jp);
                free(ref);
                seen++;
            } else if (!strcmp(kind, "reject")) {
                char jn[80];
                size_t jl = 0;
                uint8_t *jp;
                snprintf(jn, sizeof jn, "fixtures/%s.jpg", name);
                jp = slurpIn(".", jn, &jl);
                CHECK(jp != NULL);
                if (jp != NULL) {
                    err[0] = '\0';
                    CHECK(jpegDecodeRgb24(jp, jl, &dec, &w, &h, err, sizeof err) == -1 && strstr(err, rest) != NULL && dec == NULL);
                    CHECK(jpegReadInfo(jp, jl, &w, &h, &comps, err, sizeof err) == -1 && strstr(err, rest) != NULL);
                }
                free(jp);
                seen++;
            }
        }
        fclose(f);
        CHECK(seen >= 10);
    }
}

/* data that is not a picture is refused with a reason, and a damaged picture never reads past what it was given */
static void testJpegBad(void) {
    uint8_t *rgb = (uint8_t *)malloc(64 * 48 * 3), *jpg = (uint8_t *)malloc(jpegMaxSize(64, 48)), *dec = NULL, *mut;
    size_t len, i;
    char err[200];
    int w, h, k;
    static const uint8_t soiOnly[] = {0xFF, 0xD8, 0xFF, 0xD9};

    picture(rgb, 64, 48, 1);
    len = jpegEncodeRgb24(rgb, 64, 48, 85, jpg, jpegMaxSize(64, 48));
    CHECK(len > 0);
    err[0] = '\0';
    CHECK(jpegDecodeRgb24(NULL, 0, &dec, &w, &h, err, sizeof err) == -1 && err[0] != '\0' && dec == NULL);
    CHECK(jpegDecodeRgb24(jpg, 0, &dec, &w, &h, err, sizeof err) == -1 && dec == NULL);
    CHECK(jpegDecodeRgb24(jpg, 1, &dec, &w, &h, err, sizeof err) == -1);
    CHECK(jpegDecodeRgb24(soiOnly, sizeof soiOnly, &dec, &w, &h, err, sizeof err) == -1 && strstr(err, "no scan") != NULL);
    CHECK(jpegDecodeRgb24((const uint8_t *)"GIF89a not a jpeg at all", 24, &dec, &w, &h, err, sizeof err) == -1 && strstr(err, "SOI") != NULL);
    CHECK(jpegDecodeRgb24(jpg, len, NULL, &w, &h, err, sizeof err) == -1);
    /* cut anywhere: an error or a picture, never a crash; cut inside the scan it is an error */
    for (i = 0; i < len; i += 7) {
        dec = NULL;
        if (jpegDecodeRgb24(jpg, i, &dec, &w, &h, NULL, 0) == 0) free(dec);
    }
    CHECK(jpegDecodeRgb24(jpg, len * 8 / 10, &dec, &w, &h, err, sizeof err) == -1 && strstr(err, "damaged") != NULL);
    /* a picture that is too large for the limit */
    mut = (uint8_t *)malloc(len);
    memcpy(mut, jpg, len);
    for (i = 2; i + 8 < len; i++) {
        if (mut[i] == 0xFF && mut[i + 1] == 0xC0) {
            mut[i + 5] = 0xFF; mut[i + 6] = 0xFF; mut[i + 7] = 0xFF; mut[i + 8] = 0xFF; /* 65535 x 65535 */
            break;
        }
    }
    CHECK(jpegDecodeRgb24(mut, len, &dec, &w, &h, err, sizeof err) == -1 && strstr(err, "too large") != NULL && dec == NULL);
    /* the fuzz: random bytes flipped, and the file truncated, 4000 times; under ASan/UBSan (make videotest-asan) a bad read shows */
    for (k = 0; k < 4000; k++) {
        int flips = 1 + (int)(rnd() % 4), f;
        size_t cut = len;
        memcpy(mut, jpg, len);
        for (f = 0; f < flips; f++) mut[rnd() % len] = (uint8_t)rnd();
        if (k % 5 == 0) cut = 1 + rnd() % len;
        dec = NULL;
        if (jpegDecodeRgb24(mut, cut, &dec, &w, &h, NULL, 0) == 0) {
            CHECK(dec != NULL && w > 0 && h > 0);
            free(dec);
        }
    }
    free(rgb);
    free(jpg);
    free(mut);
}

static int stderrSaved = -1;

/* what is written to stderr until captureEnd (the log lines of the port), or "" where that is not possible */
static void captureStart(void) {
#ifndef _WIN32
    char p[1400];
    fflush(stderr);
    stderrSaved = dup(2);
    snprintf(p, sizeof p, "%s/stderr.txt", gDir);
    if (freopen(p, "w", stderr) == NULL) stderrSaved = -1;
#endif
}

static void captureEnd(char *out, size_t cap) {
    out[0] = '\0';
#ifndef _WIN32
    if (stderrSaved >= 0) {
        char p[1400];
        FILE *f;
        size_t n;
        fflush(stderr);
        dup2(stderrSaved, 2);
        close(stderrSaved);
        stderrSaved = -1;
        clearerr(stderr);
        snprintf(p, sizeof p, "%s/stderr.txt", gDir);
        f = fopen(p, "r");
        if (f != NULL) {
            n = fread(out, 1, cap - 1, f);
            out[n] = '\0';
            fclose(f);
        }
    }
#else
    (void)cap;
#endif
}

static void testAviRead(void) {
    char err[200], fn[1300];
    AviInfo info;
    AviReader *r;
    long i;
    size_t pcmLen = 0, ln;
    uint8_t *pcm, *ref, *fr;
    int16_t *got;

    /* what the E2a writer wrote (the synthetic disc's MOVIED01: 60 frames, stereo 37800) */
    path(fn, sizeof fn, "MOVIED01.avi");
    r = aviReadOpen(fn, &info, err, sizeof err);
    CHECK(r != NULL);
    if (r != NULL) {
        pcm = slurpIn(".", "MOVIED01.pcm", &pcmLen);
        ref = slurpIn(".", "MOVIED01.frames.rgb", &ln);
        CHECK(info.videoFrames == 60 && info.width == 320 && info.height == 416 && info.rate == 15 && info.scale == 1);
        CHECK(info.hasAudio && info.channels == 2 && info.sampleRate == 37800 && info.audioFrames >= 2520L * 60);
        CHECK(aviReadVideoSize(r, 0) > 500 && aviReadVideoSize(r, 59) > 500 && aviReadVideoSize(r, 60) == -1 && aviReadVideoSize(r, -1) == -1);
        fr = (uint8_t *)malloc(1 << 20);
        for (i = 0; i < 60 && ref != NULL; i += 7) {
            long n = aviReadVideo(r, i, fr, 1 << 20);
            uint8_t *dec = NULL;
            int w, h;
            CHECK(n == aviReadVideoSize(r, i) && aviReadVideo(r, i, fr, 10) == -1);
            CHECK(jpegDecodeRgb24(fr, (size_t)n, &dec, &w, &h, NULL, 0) == 0 && w == 320 && h == 416);
            if (dec != NULL) {
                CHECK(psnrOf(ref + (size_t)i * 320 * 416 * 3, dec, (size_t)320 * 416 * 3) >= 28.0);
                free(dec);
            }
        }
        free(fr);
        /* the audio, from any start and across chunk edges, equals the samples the exporter was given */
        got = (int16_t *)malloc(5000 * 4);
        if (pcm != NULL) {
            static const long starts[] = {0, 1, 2519, 2520, 7000, 100000};
            for (i = 0; i < 6; i++) {
                long n = aviReadAudio(r, starts[i], 5000, got);
                long want = info.audioFrames - starts[i] < 5000 ? info.audioFrames - starts[i] : 5000;
                CHECK(n == want);
                CHECK(n <= 0 || (size_t)(starts[i] + n) * 4 > pcmLen || memcmp(got, pcm + (size_t)starts[i] * 4, (size_t)n * 4) == 0);
            }
            CHECK(aviReadAudio(r, info.audioFrames, 10, got) == 0 && aviReadAudio(r, -1, 10, got) == 0);
        }
        free(got);
        free(pcm);
        free(ref);
        aviReadClose(r);
    }
    path(fn, sizeof fn, "MOVIEE01.avi"); /* no audio */
    r = aviReadOpen(fn, &info, err, sizeof err);
    CHECK(r != NULL && info.hasAudio == 0 && info.channels == 0 && info.videoFrames == 30);
    CHECK(r == NULL || aviReadAudio(r, 0, 10, (int16_t[20]){0}) == 0);
    aviReadClose(r);

    /* the ffmpeg layout (gen_fixtures.py): JUNK, idx1, a zero-byte frame, mono 22050 audio at 25 fps */
    snprintf(fn, sizeof fn, "%s/fixtures/hand.avi", gDir);
    r = aviReadOpen(fn, &info, err, sizeof err);
    if (r == NULL && !exists(fn)) {
        return; /* no fixtures (no PIL): the rest needs them */
    }
    CHECK(r != NULL);
    if (r != NULL) {
        CHECK(info.videoFrames == 12 && info.width == 160 && info.height == 120 && info.rate == 25 && info.scale == 1);
        CHECK(info.hasAudio && info.channels == 1 && info.sampleRate == 22050 && info.audioFrames == 10584);
        CHECK(aviReadVideoSize(r, 5) == 0 && aviReadVideo(r, 5, (uint8_t[4]){0}, 4) == 0);
        pcm = slurpIn("fixtures", "hand.pcm", &pcmLen);
        got = (int16_t *)malloc(10584 * 2);
        CHECK(pcm != NULL && aviReadAudio(r, 0, 10584, got) == 10584 && memcmp(got, pcm, pcmLen) == 0);
        free(got);
        free(pcm);
        aviReadClose(r);
    }
    /* no idx1, a wrong RIFF size, stereo 44100 Hz */
    snprintf(fn, sizeof fn, "%s/fixtures/hand44.avi", gDir);
    r = aviReadOpen(fn, &info, err, sizeof err);
    CHECK(r != NULL && info.videoFrames == 8 && info.channels == 2 && info.sampleRate == 44100 && info.audioFrames == 23520 &&
          info.rate == 15);
    aviReadClose(r);
    /* no audio, no DHT in the frames */
    snprintf(fn, sizeof fn, "%s/fixtures/hand_silent.avi", gDir);
    r = aviReadOpen(fn, &info, err, sizeof err);
    CHECK(r != NULL && info.videoFrames == 5 && !info.hasAudio);
    aviReadClose(r);

    /* refusals, each with its reason */
    {
        size_t len = 0;
        uint8_t *good = slurpIn("fixtures", "hand.avi", &len), *bad;
        size_t at;
        static const struct { const char *from, *to; const char *why; } patches[] = {
            {"MJPG", "XVID", "not MJPEG"},
        };
        CHECK(good != NULL);
        if (good == NULL) return;
        bad = (uint8_t *)malloc(len);
        memcpy(bad, good, len);
        memcpy(bad, "RIFX", 4);
        CHECK(copyTo("bad/riffx.avi", bad, len) == 0);
        snprintf(fn, sizeof fn, "%s/bad/riffx.avi", gDir);
        CHECK(aviReadOpen(fn, &info, err, sizeof err) == NULL && strstr(err, "not an AVI") != NULL);
        memcpy(bad, good, len);
        for (at = 0; at + 4 < len; at++) {
            if (!memcmp(bad + at, patches[0].from, 4)) {
                memcpy(bad + at, patches[0].to, 4); /* the stream header's handler and the format's compression */
            }
        }
        CHECK(copyTo("bad/xvid.avi", bad, len) == 0);
        snprintf(fn, sizeof fn, "%s/bad/xvid.avi", gDir);
        CHECK(aviReadOpen(fn, &info, err, sizeof err) == NULL && strstr(err, patches[0].why) != NULL);
        CHECK(copyTo("bad/empty.avi", "", 0) == 0);
        snprintf(fn, sizeof fn, "%s/bad/empty.avi", gDir);
        CHECK(aviReadOpen(fn, &info, err, sizeof err) == NULL && err[0] != '\0');
        snprintf(fn, sizeof fn, "%s/bad/missing.avi", gDir);
        CHECK(aviReadOpen(fn, &info, err, sizeof err) == NULL && strstr(err, "cannot open") != NULL);
        /* audio that is not 16-bit PCM: the wBitsPerSample of the first audio stream (0x10 0x00 at the end of its WAVEFORMATEX) */
        memcpy(bad, good, len);
        for (at = 0; at + 24 < len; at++) {
            if (!memcmp(bad + at, "strf", 4) && bad[at + 8] == 1 && bad[at + 9] == 0 && bad[at + 10] == 1 && bad[at + 11] == 0 &&
                bad[at + 22] == 16) {
                bad[at + 22] = 8; /* 8-bit */
                break;
            }
        }
        CHECK(copyTo("bad/pcm8.avi", bad, len) == 0);
        snprintf(fn, sizeof fn, "%s/bad/pcm8.avi", gDir);
        CHECK(aviReadOpen(fn, &info, err, sizeof err) == NULL && strstr(err, "16-bit PCM") != NULL);
        /* cut short: the chunks before the cut are kept (a movie that stops early) */
        CHECK(copyTo("bad/cut.avi", good, len * 6 / 10) == 0);
        snprintf(fn, sizeof fn, "%s/bad/cut.avi", gDir);
        r = aviReadOpen(fn, &info, err, sizeof err);
        CHECK(r != NULL && info.videoFrames > 3 && info.videoFrames < 12);
        aviReadClose(r);
        /* the fuzz: random bytes flipped in the whole file, 600 times; every frame and all the audio are read when it opens */
        for (i = 0; i < 600; i++) {
            int flips = 1 + (int)(rnd() % 6), f;
            memcpy(bad, good, len);
            for (f = 0; f < flips; f++) bad[rnd() % len] = (uint8_t)rnd();
            CHECK(copyTo("bad/fuzz.avi", bad, (i % 4 == 0) ? 1 + rnd() % len : len) == 0);
            snprintf(fn, sizeof fn, "%s/bad/fuzz.avi", gDir);
            r = aviReadOpen(fn, &info, NULL, 0);
            if (r != NULL) {
                long k;
                uint8_t *buf = (uint8_t *)malloc(1 << 16);
                int16_t a[4096];
                for (k = 0; k < info.videoFrames && k < 40; k++) {
                    long n = aviReadVideo(r, k, buf, 1 << 16);
                    uint8_t *dec = NULL;
                    int w, h;
                    if (n > 0 && jpegDecodeRgb24(buf, (size_t)n, &dec, &w, &h, NULL, 0) == 0) free(dec);
                }
                for (k = 0; k < info.audioFrames && k < 20000; k += 1000) aviReadAudio(r, k, 1000 < 4096 ? 1000 : 1, a);
                free(buf);
                aviReadClose(r);
            }
        }
        free(bad);
        free(good);
    }
}

/* ---- the replacement itself ---- */


static int vmLbaOf(void *ud, const char *name, long *lba) {
    (void)ud;
    if (!strcmp(name, "MOVIED01")) { *lba = 1000; return 1; }
    if (!strcmp(name, "MOVIED02")) { *lba = 2000; return 1; }
    return 0;
}

static int pixelNear(unsigned short v, int r, int g, int b, int tol) {
    int pr = (v & 31) << 3, pg = ((v >> 5) & 31) << 3, pb = ((v >> 10) & 31) << 3;
    return abs(pr - r) <= tol && abs(pg - g) <= tol && abs(pb - b) <= tol;
}

static long drainAudio(short *all, long cap) {
    long total = 0, n;
    static short buf[4032];
    while ((n = videoModReadAudio(NULL, buf, 2016)) > 0) {
        if (all != NULL && total + n <= cap) memcpy(all + total * 2, buf, (size_t)n * 4);
        total += n;
    }
    return total;
}

static void testVideoMod(void) {
    VideoModIo io;
    char fn[1300], log[4096];
    size_t len = 0;
    uint8_t *hand = slurpIn("fixtures", "hand.avi", &len);
    static unsigned short pic[VIDEO_MOD_W * VIDEO_MOD_H];
    unsigned char sec[2048];
    short *all;
    long total, i;
    int k;
    FILE *cf;
    char cline[64];
    int colors[12][3];

    {
        char root[1300];
        static const char json[] = "{\"format\": \"mod-v1\"}\n"; /* modlow, then modhigh: the last wins */

        CHECK(copyTo("vmods/modlow/mod.json", json, sizeof json - 1) == 0 && copyTo("vmods/modhigh/mod.json", json, sizeof json - 1) == 0);
        snprintf(root, sizeof root, "%s/vmods", gDir);
        CHECK(modsInit(root, "modlow, modhigh") == 2);
    }
    io.movieLba = vmLbaOf; io.ud = NULL;

    /* without a setup, or with no file: the disc plays */
    CHECK(videoModArm(1000) == 0 && !videoModActive());
    videoModSetup(&io);
    CHECK(videoModArm(1000) == 0 && videoModArm(77) == 0 && videoModArm(2000) == 0 && !videoModActive());
    CHECK(videoModReadSector(NULL, 1000, sec) == 0 && videoModReadAudio(NULL, (short[8]){0}, 4) == 0);
    CHECK(videoModFrame15(1, pic) == 0);

    if (hand == NULL) { /* no fixtures: the AVI side is covered by the E2a file below */
        char p2[1300];
        size_t l2 = 0;
        uint8_t *own;
        snprintf(p2, sizeof p2, "%s/MOVIED01.avi", gDir);
        own = slurpAbs(p2, &l2);
        CHECK(own != NULL);
        if (own != NULL) {
            CHECK(copyTo("vmods/modlow/video/MOVIED01.avi", own, l2) == 0);
            CHECK(videoModArm(1000) == 1 && videoModFrames() == 60);
            CHECK(videoModFrame15(1, pic) == 1 && videoModFrame15(61, pic) == 0);
            videoModStop();
        }
        free(own);
        videoModSetup(NULL);
        return;
    }

    /* a broken file in the mod that wins is refused with its reason on the log, and the mod below it plays */
    CHECK(copyTo("vmods/modlow/video/MOVIED01.avi", hand, len) == 0);
    CHECK(copyTo("vmods/modhigh/video/MOVIED01.avi", "this is not an avi file at all, not even a riff", 48) == 0);
    captureStart();
    k = videoModArm(1000);
    captureEnd(log, sizeof log);
    CHECK(k == 1 && videoModActive() && !strcmp(videoModCurrentMod(), "modlow") && !strcmp(videoModCurrentName(), "MOVIED01"));
#ifndef _WIN32
    CHECK(strstr(log, "[video] modhigh: MOVIED01.avi rejected: not an AVI") != NULL);
#endif
    CHECK(videoModFrames() == 12);

    /* the records the St layer gets: 12 frames and the end, then nothing */
    for (k = 0; k <= 12; k++) {
        long no = 0, due = -1;
        int end = -1;
        CHECK(videoModReadSector(NULL, 1000 + k, sec) == 1);
        CHECK(sec[0] == 0x60 && sec[1] == 0x01 && sec[2] == 0x01 && sec[3] == 0x80 && sec[4] == 0 && sec[6] == 1); /* id, type, sec 0 of 1 */
        CHECK(sec[16] == 0x40 && sec[17] == 0x01 && sec[18] == 0xA0 && sec[19] == 0x01); /* 320 x 416 */
        CHECK(videoModRecord(sec + 32, &no, &due, &end) == 1);
        if (k < 12) {
            CHECK(sec[8] == 0 && sec[9] == 0 && sec[10] == 0 && sec[11] == 0); /* frameCount 0: never the original's end frame */
            CHECK(no == k + 1 && end == 0 && due == 1512L * k);               /* 25 fps: 37800 / 25 = 1512 sample frames */
        } else {
            CHECK(sec[8] == 0xFF && sec[9] == 0xFF && sec[10] == 0xFF && sec[11] == 0xFF); /* the end the game honours */
            CHECK(no == 12 && end == 1 && due == 12 * 1512L);
        }
    }
    CHECK(videoModReadSector(NULL, 1013, sec) == 0 && videoModReadSector(NULL, 999, sec) == 0);
    {
        unsigned char junk[16] = {0};
        CHECK(videoModRecord(junk, NULL, NULL, NULL) == 0 && videoModRecord(NULL, NULL, NULL, NULL) == 0);
    }

    /* the pictures: a solid colour per frame, scaled to 320x416; the zero-byte frame (6) repeats frame 5 */
    snprintf(fn, sizeof fn, "%s/fixtures/hand.colors", gDir);
    cf = fopen(fn, "r");
    CHECK(cf != NULL);
    for (k = 0; cf != NULL && k < 12 && fgets(cline, sizeof cline, cf); k++) sscanf(cline, "%d %d %d", &colors[k][0], &colors[k][1], &colors[k][2]);
    if (cf != NULL) fclose(cf);
    for (k = 1; k <= 12; k++) {
        CHECK(videoModFrame15(k, pic) == 1);
        CHECK(pixelNear(pic[0], colors[k - 1][0], colors[k - 1][1], colors[k - 1][2], 14));
        CHECK(pixelNear(pic[(VIDEO_MOD_H / 2) * VIDEO_MOD_W + VIDEO_MOD_W / 2], colors[k - 1][0], colors[k - 1][1], colors[k - 1][2], 14));
        CHECK(pixelNear(pic[VIDEO_MOD_H * VIDEO_MOD_W - 1], colors[k - 1][0], colors[k - 1][1], colors[k - 1][2], 14));
    }
    CHECK(videoModFrame15(0, pic) == 0 && videoModFrame15(13, pic) == 0);
    CHECK(videoModFrame15(3, pic) == 1 && videoModFrame15(2, pic) == 1 && videoModFrame15(2, pic) == 1); /* out of order and repeated */
    CHECK(pixelNear(pic[1000], colors[1][0], colors[1][1], colors[1][2], 14));

    /* the audio: 22050 Hz mono resampled to 37800 Hz stereo, then silence for the padding; ends at totalOut */
    all = (short *)malloc(60000 * 4);
    total = drainAudio(all, 60000);
    CHECK(total == 12 * 1512 + 18900);
    for (i = 0; i < 12 * 1512 - 3 && total >= 12 * 1512; i++) {
        double want = 12000.0 * sin(2.0 * 3.14159265358979323846 * 440.0 * (double)i / 37800.0);
        if (fabs((double)all[i * 2] - want) > 450.0 || all[i * 2] != all[i * 2 + 1]) {
            CHECK(0);
            break;
        }
    }
    for (i = 12 * 1512 + 2; i < total && total > 12 * 1512; i++) {
        if (all[i * 2] != 0 || all[i * 2 + 1] != 0) {
            CHECK(0);
            break;
        }
    }
    free(all);
    CHECK(videoModReadAudio(NULL, (short[8]){0}, 4) == 0);
    videoModStop();
    CHECK(!videoModActive() && videoModReadSector(NULL, 1000, sec) == 0 && videoModRecord(sec, NULL, NULL, NULL) == 0);

    /* the last active mod that has the file wins: stereo 44100 Hz, 15 fps, small pictures (scaled), no idx1 */
    {
        size_t l2 = 0;
        uint8_t *h44 = slurpIn("fixtures", "hand44.avi", &l2);
        CHECK(h44 != NULL && copyTo("vmods/modhigh/video/MOVIED01.avi", h44, l2) == 0);
        free(h44);
    }
    CHECK(videoModArm(1000) == 1 && !strcmp(videoModCurrentMod(), "modhigh") && videoModFrames() == 8);
    CHECK(videoModFrame15(1, pic) == 1 && videoModFrame15(8, pic) == 1);
    {
        long no, due;
        int end;
        CHECK(videoModReadSector(NULL, 1003, sec) == 1 && videoModRecord(sec + 32, &no, &due, &end) == 1 && no == 4 && due == 3 * 2520L);
        CHECK(videoModReadSector(NULL, 1008, sec) == 1 && videoModRecord(sec + 32, &no, &due, &end) == 1 && end == 1 && due == 8 * 2520L);
    }
    all = (short *)malloc(80000 * 4);
    total = drainAudio(all, 80000);
    CHECK(total == 8 * 2520 + 18900);
    for (i = 0; i < 8 * 2520 - 3 && total >= 8 * 2520; i++) {
        double l = 9000.0 * sin(2.0 * 3.14159265358979323846 * 300.0 * (double)i / 37800.0);
        double r = -9000.0 * sin(2.0 * 3.14159265358979323846 * 500.0 * (double)i / 37800.0);
        if (fabs((double)all[i * 2] - l) > 450.0 || fabs((double)all[i * 2 + 1] - r) > 450.0) {
            CHECK(0);
            break;
        }
    }
    free(all);
    videoModStop();

    /* no audio at all: silence for the length of the pictures and the padding */
    {
        size_t l2 = 0;
        uint8_t *hs = slurpIn("fixtures", "hand_silent.avi", &l2);
        CHECK(hs != NULL && copyTo("vmods/modhigh/video/MOVIED01.avi", hs, l2) == 0);
        free(hs);
    }
    CHECK(videoModArm(1000) == 1 && videoModFrames() == 5);
    all = (short *)malloc(40000 * 4);
    total = drainAudio(all, 40000);
    CHECK(total == 5 * 2520 + 18900);
    for (i = 0; i < total && i < 40000; i++) {
        if (all[i * 2] != 0 || all[i * 2 + 1] != 0) {
            CHECK(0);
            break;
        }
    }
    free(all);
    videoModStop();

    /* a frame that does not decode later in the file repeats the picture before it, with one log line; the first one is refused */
    {
        size_t l2 = 0, at;
        uint8_t *dmg = slurpIn("fixtures", "hand_silent.avi", &l2), *hs_orig = NULL;
        int seen = 0;
        CHECK(dmg != NULL);
        if (dmg != NULL) {
            hs_orig = (uint8_t *)malloc(l2);
            memcpy(hs_orig, dmg, l2);
            /* the third frame's scan data: find the third SOS and blank what follows it */
            for (at = 0; at + 2 < l2; at++) {
                if (dmg[at] == 0xFF && dmg[at + 1] == 0xDA && ++seen == 3) {
                    memset(dmg + at + 14, 0xFF, 64);
                    break;
                }
            }
            CHECK(copyTo("vmods/modhigh/video/MOVIED01.avi", dmg, l2) == 0);
            CHECK(videoModArm(1000) == 1);
            captureStart();
            CHECK(videoModFrame15(2, pic) == 1);
            CHECK(videoModFrame15(3, pic) == 1);
            CHECK(videoModFrame15(4, pic) == 1);
            captureEnd(log, sizeof log);
#ifndef _WIN32
            CHECK(strstr(log, "[video] modhigh: MOVIED01.avi frame 3 is damaged") != NULL);
#endif
            videoModStop();
            /* the first frame damaged: the file is refused, and the mod below plays */
            memcpy(dmg, hs_orig, l2);
            for (at = 0, seen = 0; at + 2 < l2; at++) {
                if (dmg[at] == 0xFF && dmg[at + 1] == 0xDA && ++seen == 1) {
                    memset(dmg + at + 14, 0xFF, 64);
                    break;
                }
            }
            CHECK(copyTo("vmods/modhigh/video/MOVIED01.avi", dmg, l2) == 0);
            captureStart();
            k = videoModArm(1000);
            captureEnd(log, sizeof log);
            CHECK(k == 1 && !strcmp(videoModCurrentMod(), "modlow"));
#ifndef _WIN32
            CHECK(strstr(log, "[video] modhigh: MOVIED01.avi rejected: first frame:") != NULL);
#endif
            videoModStop();
            /* a frame rate outside 1..60 fps is refused too (the stream header's rate field set to 200) */
            memcpy(dmg, hs_orig, l2);
            for (at = 0; at + 28 < l2; at++) {
                if (!memcmp(dmg + at, "vids", 4)) {
                    dmg[at + 24] = 200; dmg[at + 25] = dmg[at + 26] = dmg[at + 27] = 0;
                    dmg[at + 20] = 1; dmg[at + 21] = dmg[at + 22] = dmg[at + 23] = 0;
                    break;
                }
            }
            CHECK(copyTo("vmods/modhigh/video/MOVIED01.avi", dmg, l2) == 0);
            captureStart();
            k = videoModArm(1000);
            captureEnd(log, sizeof log);
            CHECK(k == 1 && !strcmp(videoModCurrentMod(), "modlow"));
#ifndef _WIN32
            CHECK(strstr(log, "[video] modhigh: MOVIED01.avi rejected: frame rate 200.00 is outside 1..60 fps") != NULL);
#endif
            videoModStop();
            /* nothing usable anywhere: the disc plays */
            CHECK(copyTo("vmods/modlow/video/MOVIED01.avi", "garbage", 7) == 0);
            CHECK(videoModArm(1000) == 0 && !videoModActive());
        }
        free(dmg);
        free(hs_orig);
    }
    videoModSetup(NULL);
    free(hand);
}

int main(int argc, char **argv) {
    char mp[1300];
    if (argc < 2) {
        fprintf(stderr, "usage: videotest <scratch dir>\n");
        return 2;
    }
    snprintf(gDir, sizeof gDir, "%s", argv[1]);
    if (tdMakeDirs(gDir)) {
        fprintf(stderr, "videotest: cannot create %s\n", gDir);
        return 2;
    }
    path(mp, sizeof mp, "manifest.txt");
    gManifest = fopen(mp, "wb");
    if (gManifest == NULL) return 2;
    CHECK(videoExportCount() == 14 && !strcmp(videoExportName(0), "MOVIED01") && !strcmp(videoExportName(13), "MOVIEOPN"));
    testJpeg();
    testAvi();
    testAviAspect();
    buildDisc();
    testBuild();
    testProgress();
    testJob();
    testJpegRead();
    testJpegBad();
    testAviRead();
    testVideoMod();
    fclose(gManifest);
    free(gDisc.data);
    if (failures) {
        printf("videotest: %d failures\n", failures);
        return 1;
    }
    printf("videotest: OK (C side)\n");
    return 0;
}
