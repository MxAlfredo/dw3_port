/*
 * Host test of the field stage tile export and replacement (content-export E6b): pc/shim/stage_export.c and stage_mod.c. Synthetic PACK files
 * on a raw disc image in memory (RLEN and plain tiles, the two TIM offsets, empty and corrupt tiles); no retail bytes. The scratch folder
 * lives under $TMPDIR.
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/image_data.h"
#include "shim/mods.h"
#include "shim/png_read.h"
#include "shim/png_write.h"
#include "shim/stage_export.h"
#include "shim/stage_mod.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "stagetest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- helpers ---- */

static void le32(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void le16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

#define TW 32 /* tile image: 8-bit, 32 px wide, 8 rows (the real ones are 128-256 by 128; the format is the same) */
#define TH 8

static unsigned palColor(int i) { return i == 0 ? 0 : (unsigned)(((i * 7) & 31) | (((i * 11) & 31) << 5) | (((i * 13) & 31) << 10)); }
static uint8_t pixelAt(int x, int y, int seed) { return (uint8_t)((x * 3 + y * 5 + seed) & 0xFF); }

/* an 8-bit TIM with its own 256x1 CLUT; returns its length */
static size_t makeTim(uint8_t *o, int seed) {
    size_t at = 0;
    int i, x, y;

    le32(o, 0x10);
    le32(o + 4, 9);
    at = 8;
    le32(o + at, 12 + 512);
    le16(o + at + 4, 0);
    le16(o + at + 6, 0xF0);
    le16(o + at + 8, 256);
    le16(o + at + 10, 1);
    for (i = 0; i < 256; i++) le16(o + at + 12 + 2 * (size_t)i, palColor(i));
    at += 12 + 512;
    le32(o + at, 12 + 2 * (TW / 2) * TH);
    le16(o + at + 4, 320);
    le16(o + at + 6, 0);
    le16(o + at + 8, TW / 2);
    le16(o + at + 10, TH);
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) o[at + 12 + (size_t)y * TW + (size_t)x] = pixelAt(x, y, seed);
    }
    return at + 12 + 2 * (TW / 2) * TH;
}

/* a tile: the sprite lists, then the TIM; n[d] sprites at depth d (record k: x = k, y = d, u = 16 * k, v = 8, w = 16, h = 8) */
static size_t makeTile(uint8_t *o, const int n[3], int seed) {
    size_t p = 4;
    int d, k;

    for (d = 0; d < 3; d++) {
        le32(o + p, (unsigned)n[d]);
        p += 4;
        for (k = 0; k < n[d]; k++, p += 12) {
            le16(o + p, (unsigned)k);
            le16(o + p + 2, (unsigned)d);
            le16(o + p + 4, 16u * (unsigned)k);
            le16(o + p + 6, 8);
            le16(o + p + 8, 16);
            le16(o + p + 10, 8);
        }
    }
    le32(o, (unsigned)p);
    return p + makeTim(o + p, seed);
}

/* "RLEN", the unpacked size, runs (repeats of 3 or more bytes, literals otherwise), 0 */
static size_t rlenPack(const uint8_t *in, size_t len, uint8_t *o) {
    size_t i = 0, w = 8;

    memcpy(o, "RLEN", 4);
    le32(o + 4, (unsigned)len);
    while (i < len) {
        size_t run = 1;
        while (i + run < len && in[i + run] == in[i] && run < 127) run++;
        if (run >= 3) {
            o[w++] = (uint8_t)(0x80 | run);
            o[w++] = in[i];
            i += run;
        } else {
            size_t n = 0;
            while (i + n < len && n < 127 && !(i + n + 2 < len && in[i + n] == in[i + n + 1] && in[i + n] == in[i + n + 2])) n++;
            if (n == 0) n = 1;
            o[w++] = (uint8_t)n;
            memcpy(o + w, in + i, n);
            w += n;
            i += n;
        }
    }
    o[w++] = 0;
    return w;
}

/* ---- the synthetic disc ---- */

#define SECTORS 200
static uint8_t gDisc[SECTORS][2352];
#define TILE_BYTES 4096
#define SPT (TILE_BYTES / 2048)
#define LBA_A 10 /* S233PACK: 3x2 tiles */
#define LBA_B 60 /* S330PACK: 1x2 tiles */

static void putSectors(long lba, const uint8_t *d, size_t len) {
    size_t i;
    for (i = 0; i < len; i += 2048) {
        size_t n = len - i < 2048 ? len - i : 2048;
        memcpy(gDisc[lba + (long)(i / 2048)] + 24, d + i, n);
    }
}

static void putHeader(long lba, int count, int w, int h, const uint16_t *sizes) {
    uint8_t s[2048];
    int i;
    memset(s, 0, sizeof s);
    le32(s, (unsigned)count);
    le32(s + 4, (unsigned)w);
    le32(s + 8, (unsigned)h);
    le32(s + 12, TILE_BYTES);
    for (i = 0; i < w * h; i++) le16(s + 16 + 2 * (size_t)i, sizes[i]);
    putSectors(lba, s, 2048);
}

static void buildDisc(void) {
    uint8_t tile[TILE_BYTES], packed[TILE_BYTES + 64];
    static const int n0[3] = {1, 0, 2}, n1[3] = {0, 1, 0}, n3[3] = {2, 0, 0}, nBig[3] = {2, 2, 2};
    uint16_t sizesA[6] = {200, 150, 0, 220, 100, 0}, sizesB[2] = {120, 90};
    size_t len;

    memset(gDisc, 0, sizeof gDisc);
    putHeader(LBA_A, 4, 3, 2, sizesA);
    /* frame 0: plain, offset 4 + 3*4 + 3*12 = 52 */
    memset(tile, 0, sizeof tile);
    len = makeTile(tile, n0, 1);
    putSectors(LBA_A + 1, tile, TILE_BYTES);
    (void)len;
    /* frame 1: RLEN, one sprite */
    memset(tile, 0, sizeof tile);
    len = makeTile(tile, n1, 2);
    memset(packed, 0, sizeof packed);
    putSectors(LBA_A + 1 + SPT, packed, 0);
    len = rlenPack(tile, len, packed);
    putSectors(LBA_A + 1 + SPT, packed, len);
    /* frame 3: plain with two sprites at depth 0 (offset 4 + 12 + 24 = 40 = 0x28) */
    memset(tile, 0, sizeof tile);
    makeTile(tile, n3, 4);
    putSectors(LBA_A + 1 + 3 * SPT, tile, TILE_BYTES);
    /* frame 4: RLEN, the busiest lists */
    memset(tile, 0, sizeof tile);
    len = makeTile(tile, nBig, 5);
    len = rlenPack(tile, len, packed);
    putSectors(LBA_A + 1 + 4 * SPT, packed, len);
    /* the second pack: tile 0 fine, tile 1 an RLEN that does not unpack to its size */
    putHeader(LBA_B, 2, 1, 2, sizesB);
    memset(tile, 0, sizeof tile);
    len = makeTile(tile, n1, 7);
    len = rlenPack(tile, len, packed);
    putSectors(LBA_B + 1, packed, len);
    memset(tile, 0, sizeof tile);
    len = makeTile(tile, n1, 8);
    len = rlenPack(tile, len, packed);
    le32(packed + 4, (unsigned)len + 50); /* a size the runs do not reach */
    putSectors(LBA_B + 1 + SPT, packed, len);
}

static int ioPack(void *ud, int i, char *name, size_t cap, long *lba, long *size) {
    (void)ud;
    if (i == 0) {
        snprintf(name, cap, "S233PACK");
        *lba = LBA_A;
        *size = (1 + 6 * SPT) * 2048;
        return 1;
    }
    if (i == 1) {
        snprintf(name, cap, "S330PACK");
        *lba = LBA_B;
        *size = (1 + 2 * SPT) * 2048;
        return 1;
    }
    return 0;
}

static int gReads;

static int ioRead(void *ud, long lba, unsigned char *frame) {
    (void)ud;
    if (lba < 0 || lba >= SECTORS) return 0;
    gReads++;
    memcpy(frame, gDisc[lba], 2352);
    return 1;
}

static StageExportIo gIo = {ioPack, ioRead, NULL};

/* ---- scratch folder ---- */

static char gRoot[1024];

static void spit(const char *rel, const void *d, size_t n) {
    char path[1300], dir[1300], *sl;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "%s", path);
    sl = strrchr(dir, '/');
    if (sl) {
        *sl = 0;
        CHECK(tdMakeDirs(dir) == 0);
    }
    CHECK(tdWriteFileAtomic(path, d, n) == 0);
}

static uint8_t *slurp(const char *rel, size_t *n) {
    char path[1300];
    uint8_t *d;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    if (tdReadFile(path, &d, n)) return NULL;
    return d;
}

static int exists(const char *rel) {
    char path[1300];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    return stat(path, &st) == 0;
}

static void rmtree(const char *dir) {
    char cmd[1400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd)) {
    }
}

static int scratch(void) {
    snprintf(gRoot, sizeof gRoot, "%s/stagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        failures++;
        return 0;
    }
    return 1;
}

static void dataDir(char *out, size_t cap) { snprintf(out, cap, "%s/data", gRoot); }

/* ---- the formats ---- */

static void testFormats(void) {
    uint8_t sec[2048], tile[TILE_BYTES], packed[TILE_BYTES + 64], tim[1024];
    static const int n0[3] = {1, 0, 2}, nb[3] = {1, 1, 1};
    PackHeader pk;
    PackTile t;
    char err[200], key[24];
    size_t len, rl;
    int i;

    /* the pack header */
    memcpy(sec, gDisc[LBA_A] + 24, 2048);
    CHECK(stagePackParse(sec, 2048, &pk, err, sizeof err) == 0);
    CHECK(pk.tileCount == 4 && pk.width == 3 && pk.height == 2 && pk.tileBytes == TILE_BYTES);
    CHECK(pk.sizes[0] == 200 && pk.sizes[1] == 150 && pk.sizes[2] == 0 && pk.sizes[3] == 220 && pk.sizes[5] == 0);
    le32(sec + 4, 2000); /* a grid that does not fit the header sector */
    CHECK(stagePackParse(sec, 2048, &pk, err, sizeof err) == -1 && err[0] != '\0');
    memcpy(sec, gDisc[LBA_A] + 24, 2048);
    le32(sec + 12, 3000); /* not a whole number of sectors */
    CHECK(stagePackParse(sec, 2048, &pk, err, sizeof err) == -1 && strstr(err, "sector") != NULL);
    le32(sec + 12, 4096);
    le32(sec + 8, 0);
    CHECK(stagePackParse(sec, 2048, &pk, err, sizeof err) == -1);
    CHECK(stagePackParse(sec, 10, &pk, err, sizeof err) == -1);

    /* TIM length */
    len = makeTim(tim, 3);
    CHECK(stageTimLength(tim, len) == len && stageTimLength(tim, len + 100) == len);
    CHECK(stageTimLength(tim, len - 1) == 0);
    CHECK(stageTimLength(tim, 10) == 0);
    tim[0] = 0x11;
    CHECK(stageTimLength(tim, len) == 0);

    /* a plain tile and its TIM offset (4 + 3*4 + 3*12 = 52) */
    memset(tile, 0, sizeof tile);
    len = makeTile(tile, n0, 1);
    CHECK(stageTileParse(tile, TILE_BYTES, &t, err, sizeof err) == 0);
    CHECK(!t.rle && t.timOff == 4 + 12 + 36 && t.count[0] == 1 && t.count[1] == 0 && t.count[2] == 2);
    CHECK(t.timLen == len - t.timOff);
    CHECK(t.sp[2][1].x == 1 && t.sp[2][1].y == 2 && t.sp[2][1].u == 16 && t.sp[2][1].v == 8 && t.sp[2][1].w == 16 && t.sp[2][1].h == 8);
    stageTileFree(&t);
    /* the 0x28 variant: two sprites at depth 0 only */
    {
        static const int n3[3] = {2, 0, 0};
        memset(tile, 0, sizeof tile);
        makeTile(tile, n3, 4);
        CHECK(stageTileParse(tile, TILE_BYTES, &t, err, sizeof err) == 0 && t.timOff == 0x28 && t.count[0] == 2);
        stageTileFree(&t);
    }
    /* an RLEN tile gives the same bytes as the plain one */
    memset(tile, 0, sizeof tile);
    len = makeTile(tile, nb, 9);
    rl = rlenPack(tile, len, packed);
    CHECK(stageTileParse(packed, sizeof packed, &t, err, sizeof err) == 0 && t.rle && t.len == len && memcmp(t.data, tile, len) == 0);
    stageTileFree(&t);
    /* the same stream cut before its end marker, a wrong declared size, a bad offset, no TIM, a tiny tile */
    CHECK(stageTileParse(packed, rl - 1, &t, err, sizeof err) == -1 && err[0] != '\0');
    le32(packed + 4, (unsigned)len + 1);
    CHECK(stageTileParse(packed, sizeof packed, &t, err, sizeof err) == -1 && strstr(err, "RLEN") != NULL);
    le32(packed + 4, 0);
    CHECK(stageTileParse(packed, sizeof packed, &t, err, sizeof err) == -1);
    le32(packed + 4, STAGE_MAX_UNPACKED + 1);
    CHECK(stageTileParse(packed, sizeof packed, &t, err, sizeof err) == -1);
    le32(tile, 4000);
    CHECK(stageTileParse(tile, TILE_BYTES, &t, err, sizeof err) == -1 && strstr(err, "offset") != NULL);
    le32(tile, 8);
    CHECK(stageTileParse(tile, TILE_BYTES, &t, err, sizeof err) == -1);
    memset(tile, 0, sizeof tile);
    makeTile(tile, nb, 1);
    le32(tile + 4, 99); /* depth 0 claims 99 sprites */
    CHECK(stageTileParse(tile, TILE_BYTES, &t, err, sizeof err) == -1 && strstr(err, "sprites") != NULL);
    memset(tile, 0, sizeof tile);
    makeTile(tile, nb, 1);
    tile[tile[0] | tile[1] << 8] = 0x42; /* the TIM magic is gone */
    CHECK(stageTileParse(tile, TILE_BYTES, &t, err, sizeof err) == -1 && strstr(err, "TIM") != NULL);
    CHECK(stageTileParse(tile, 4, &t, err, sizeof err) == -1);

    /* names */
    stageTileName(3, 4, err, sizeof err);
    CHECK(!strcmp(err, "01_01.png"));
    stageTileName(3, 0, err, sizeof err);
    CHECK(!strcmp(err, "00_00.png"));
    stageTileName(12, 25, err, sizeof err);
    CHECK(!strcmp(err, "02_01.png"));
    for (i = 0; i < 6; i++) {
        static const char *const good[] = {"S233PACK", "s233", "S233PACK.BIN", "s233pack.bin", "S233.BIN", "S000"};
        CHECK(stagePackKey(good[i], key, sizeof key) == 1);
        if (i < 5) CHECK(!strcmp(key, "s233pack"));
    }
    CHECK(!stagePackKey("S23", key, sizeof key) && !stagePackKey("T233", key, sizeof key) && !stagePackKey("S2334", key, sizeof key) &&
          !stagePackKey("S23x", key, sizeof key) && !stagePackKey("PACK", key, sizeof key) && !stagePackKey("", key, sizeof key));
}

/* ---- the export ---- */

typedef struct Png {
    PngImage p;
    int ok;
} Png;

static Png readPng(const char *rel) {
    Png r;
    size_t n;
    uint8_t *d = slurp(rel, &n);
    char why[100];
    memset(&r, 0, sizeof r);
    if (d) {
        r.ok = pngReadIndexed(d, n, &r.p, why, sizeof why) == 0;
        free(d);
    }
    return r;
}

static void testExport(void) {
    char dd[1100], rel[160];
    StageExportStats tot;
    int done, failed, total, guard;
    Png pg;
    size_t n, i;
    uint8_t *js;

    CHECK(scratch());
    dataDir(dd, sizeof dd);
    stageExportSetup(dd, &gIo);
    CHECK(stageExportRequest("nonsense") == -1);
    CHECK(stageExportRequest("S999") == -1); /* a stage name that is not on this disc */
    CHECK(stageExportRequest("all") == 2);
    guard = 0;
    while (stageExportPump(1000) && guard++ < 1000) {
    }
    stageExportCounts(&done, &failed, &total);
    CHECK(total == 2 && done == 1 && failed == 1); /* S330 has a corrupt tile */
    CHECK(strstr(stageExportStatus(), "failed") != NULL);
    stageExportTotals(&tot);
    CHECK(tot.tiles == 4 + 2 && tot.failed == 1);

    /* S233: 4 tiles with data, 2 empty (no file) */
    CHECK(exists("data/z_stage/s233pack/00_00.png") && exists("data/z_stage/s233pack/00_01.png") && !exists("data/z_stage/s233pack/00_02.png") &&
          exists("data/z_stage/s233pack/01_00.png") && exists("data/z_stage/s233pack/01_01.png") && !exists("data/z_stage/s233pack/01_02.png"));
    CHECK(exists("data/z_stage/s233pack/s233pack.json") && exists("data/z_stage/s233pack/manifest.json"));
    /* the PNG round trip: pixels and palette are the TIM's */
    pg = readPng("data/z_stage/s233pack/00_00.png");
    CHECK(pg.ok && pg.p.w == TW && pg.p.h == TH && pg.p.depth == 8 && pg.p.nPal == 256);
    if (pg.ok) {
        int x, y, bad = 0;
        for (y = 0; y < TH; y++) {
            for (x = 0; x < TW; x++) {
                if (pg.p.px[y * TW + x] != pixelAt(x, y, 1)) bad++;
            }
        }
        CHECK(bad == 0);
        for (i = 1; i < 256; i++) {
            unsigned c = palColor((int)i), r5 = c & 31, g5 = (c >> 5) & 31, b5 = (c >> 10) & 31;
            CHECK(pg.p.rgb[3 * i] == (uint8_t)((r5 << 3) | (r5 >> 2)) && pg.p.rgb[3 * i + 1] == (uint8_t)((g5 << 3) | (g5 >> 2)) &&
                  pg.p.rgb[3 * i + 2] == (uint8_t)((b5 << 3) | (b5 >> 2)));
        }
        CHECK(pg.p.nAlpha >= 1 && pg.p.alpha[0] == 0); /* colour 0 is transparent */
        pngImageFree(&pg.p);
    }
    pg = readPng("data/z_stage/s233pack/01_01.png"); /* frame 4, RLEN, seed 5 */
    CHECK(pg.ok && pg.p.px[1 * TW + 2] == pixelAt(2, 1, 5));
    if (pg.ok) pngImageFree(&pg.p);
    /* the descriptor */
    js = slurp("data/z_stage/s233pack/s233pack.json", &n);
    CHECK(js != NULL);
    if (js) {
        const char *j = (const char *)js;
        CHECK(strstr(j, "\"format\": \"stage-pack-v1\"") && strstr(j, "\"pack\": \"S233PACK\"") && strstr(j, "\"width\": 3") &&
              strstr(j, "\"height\": 2") && strstr(j, "\"tileBytes\": 4096") && strstr(j, "\"tileCount\": 4"));
        CHECK(strstr(j, "\"frame\": 0, \"row\": 0, \"col\": 0, \"png\": \"00_00.png\", \"size\": 200, \"rle\": false, \"timOffset\": 52"));
        CHECK(strstr(j, "\"frame\": 1, \"row\": 0, \"col\": 1, \"png\": \"00_01.png\", \"size\": 150, \"rle\": true, \"timOffset\": 28"));
        CHECK(strstr(j, "\"frame\": 3, \"row\": 1, \"col\": 0, \"png\": \"01_00.png\", \"size\": 220, \"rle\": false, \"timOffset\": 40"));
        CHECK(strstr(j, "\"depths\": [[{\"x\": 0, \"y\": 0, \"u\": 0, \"v\": 8, \"w\": 16, \"h\": 8}], [], [{\"x\": 0, \"y\": 2, \"u\": 0, \"v\": 8, \"w\": 16, \"h\": 8}, "
                        "{\"x\": 1, \"y\": 2, \"u\": 16, \"v\": 8, \"w\": 16, \"h\": 8}]]"));
        CHECK(strstr(j, "\"width\": 32, \"height\": 8"));
        free(js);
    }
    /* S330: its good tile is there, the corrupt one is not, and the pack has no version mark */
    CHECK(exists("data/z_stage/s330pack/00_00.png") && !exists("data/z_stage/s330pack/01_00.png"));
    js = slurp("data/z_stage/s330pack/manifest.json", &n);
    CHECK(js != NULL && strstr((const char *)js, "exporter") == NULL);
    free(js);
    js = slurp("data/z_stage/s233pack/manifest.json", &n);
    CHECK(js != NULL && strstr((const char *)js, "\"exporter\"") != NULL && strstr((const char *)js, "\"00_00.png\"") != NULL &&
          strstr((const char *)js, "\"s233pack.json\"") != NULL);
    free(js);

    /* a second request: S233 is up to date (skipped), S330 is tried again */
    gReads = 0;
    CHECK(stageExportRequest("all") == 2);
    guard = 0;
    while (stageExportPump(1000) && guard++ < 1000) {
    }
    CHECK(stageExportBuilt() == 1); /* only S330 */
    CHECK(gReads > 0 && gReads < 4 * 2 + 8); /* S233 was not read again (its tiles are 2 sectors each) */
    /* name forms */
    CHECK(stageExportRequest("s233pack.bin, S233, nonsense, S330") == 2); /* a repeat and a bad name are dropped */
    guard = 0;
    while (stageExportPump(1000) && guard++ < 1000) {
    }
    CHECK(stageExportRequest("S330") == 1);
    guard = 0;
    while (stageExportPump(1000) && guard++ < 1000) {
    }
    /* force: rebuild everything, nothing changes on disk */
    CHECK(stageExportRequestEx("S233", 1) == 1);
    while (stageExportPump(1000)) {
    }
    CHECK(stageExportBuilt() == 1);
    stageExportTotals(&tot);
    CHECK(tot.written == 0 && tot.kept == 5 && tot.newFiles == 0); /* 4 PNG + the JSON, byte for byte the same */

    /* an edited PNG stays and the new version goes beside it; an unedited one is replaced when it changes */
    {
        uint8_t edited[64];
        memset(edited, 7, sizeof edited);
        spit("data/z_stage/s233pack/00_01.png", edited, sizeof edited);
        CHECK(stageExportRequestEx("S233", 1) == 1);
        while (stageExportPump(1000)) {
        }
        stageExportTotals(&tot);
        CHECK(tot.newFiles == 1 && exists("data/z_stage/s233pack/00_01.png.new"));
        js = slurp("data/z_stage/s233pack/00_01.png", &n);
        CHECK(js != NULL && n == 64 && js[0] == 7);
        free(js);
        /* a skipped request (up to date) leaves the edited file alone and writes nothing */
        CHECK(stageExportRequest("S233") == 1);
        while (stageExportPump(1000)) {
        }
        CHECK(stageExportBuilt() == 0);
    }
    /* an unedited file whose content is stale is replaced: change the manifest-known file by hand-writing the old export's own bytes */
    snprintf(rel, sizeof rel, "data/z_stage/s233pack/00_00.png");
    {
        size_t pl;
        uint8_t *cur = slurp(rel, &pl);
        char path[1300], mp[1300];
        uint8_t *mt;
        size_t ml;
        TdManifest m;
        char hex[17], err[100];
        CHECK(cur != NULL);
        cur[pl - 1] ^= 1;
        spit(rel, cur, pl);
        /* the manifest says this changed file is what the exporter wrote */
        snprintf(mp, sizeof mp, "%s/data/z_stage/s233pack/manifest.json", gRoot);
        snprintf(path, sizeof path, "%s/%s", gRoot, rel);
        memset(&m, 0, sizeof m);
        CHECK(tdReadFile(mp, &mt, &ml) == 0 && tdManifestParse((const char *)mt, ml, &m, err, sizeof err) == 0);
        free(mt);
        tdHashHex(tdHash(cur, pl), hex);
        tdManifestSet(&m, "00_00.png", hex);
        {
            char *text = tdManifestFormat(&m);
            CHECK(text != NULL && tdWriteFileAtomic(mp, text, strlen(text)) == 0);
            free(text);
        }
        tdManifestFree(&m);
        free(cur);
        CHECK(stageExportRequestEx("S233", 1) == 1);
        while (stageExportPump(1000)) {
        }
        stageExportTotals(&tot);
        CHECK(tot.written == 1 && !exists("data/z_stage/s233pack/00_00.png.new")); /* replaced */
        pg = readPng(rel);
        CHECK(pg.ok);
        if (pg.ok) pngImageFree(&pg.p);
    }

    /* the worker thread: start, then pump until it is joined */
    CHECK(stageExportStart("S233", 1) == 1);
    guard = 0;
    while (stageExportActive() && guard++ < 100000) {
        stageExportPump(1);
        usleep(1000);
    }
    CHECK(!stageExportActive() && stageExportPump(1) == 0 && strstr(stageExportStatus(), "Done") != NULL);
    CHECK(stageExportBuilt() == 1);
    /* nothing to do without a data folder or a reader */
    stageExportSetup(NULL, &gIo);
    CHECK(stageExportRequest("all") == -1);
    stageExportSetup(dd, NULL);
    CHECK(stageExportRequest("all") == -1);
    stageExportSetup(NULL, NULL);
    rmtree(gRoot);
}

/* ---- the replacement ---- */

static int modPack(void *ud, long lba, char *name, size_t cap) {
    (void)ud;
    if (lba == LBA_A) {
        snprintf(name, cap, "s233pack");
        return 1;
    }
    if (lba == LBA_B) {
        snprintf(name, cap, "s330pack");
        return 1;
    }
    return 0;
}

static int modHeader(void *ud, long lba, uint8_t *sector) {
    (void)ud;
    if (lba < 0 || lba >= SECTORS) return 0;
    memcpy(sector, gDisc[lba] + 24, 2048);
    return 1;
}

static void touchMod(const char *name) {
    char rel[200];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    spit(rel, "{\n  \"format\": \"mod-v1\",\n  \"name\": \"t\",\n  \"version\": \"1.0.0\"\n}\n", 61);
}

/* a PNG of the tile (seed) with palette entry `pal` set to (r, g, b); nPal colours; the size is (w, h) */
static void putTilePng(const char *rel, int seed, int nPal, int pal, int r, int g, int b, int w, int h) {
    uint8_t px[64 * 64], rgb[256 * 3], alpha[256], *png;
    size_t pl;
    int i, x, y;

    for (i = 0; i < 256; i++) {
        unsigned c = palColor(i);
        rgb[3 * i] = (uint8_t)((c & 31) << 3);
        rgb[3 * i + 1] = (uint8_t)(((c >> 5) & 31) << 3);
        rgb[3 * i + 2] = (uint8_t)(((c >> 10) & 31) << 3);
        alpha[i] = i ? 255 : 0;
    }
    if (pal >= 0) {
        rgb[3 * pal] = (uint8_t)r;
        rgb[3 * pal + 1] = (uint8_t)g;
        rgb[3 * pal + 2] = (uint8_t)b;
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) px[y * w + x] = (uint8_t)(pixelAt(x, y, seed) % (unsigned)nPal);
    }
    CHECK(pngWriteIndexed(px, w, h, 8, rgb, nPal, alpha, 1, &png, &pl) == 0);
    spit(rel, png, pl);
    free(png);
}

static unsigned clutAt(const uint8_t *tim, int i) { return (unsigned)(tim[8 + 12 + 2 * i] | tim[8 + 12 + 2 * i + 1] << 8); }

static void testMod(void) {
    uint8_t tim1[1024], tim0[1024];
    size_t len1, len0;
    char dd[1100], modsRoot[1100], rel[200];
    StageModIo io = {modPack, modHeader, NULL};
    const uint8_t *r;
    int i;

    CHECK(scratch());
    dataDir(dd, sizeof dd);
    snprintf(modsRoot, sizeof modsRoot, "%s/mods", gRoot);
    len0 = makeTim(tim0, 1); /* tile 0 of S233 */
    len1 = makeTim(tim1, 2); /* tile 1 */
    (void)len1;

    /* no mods, nothing exported: every tile stays */
    modsInit(NULL, NULL);
    stageModSetup(dd, &io);
    CHECK(stageModTim(LBA_A, 0, tim0) == NULL && stageModTim(LBA_A, 1, tim1) == NULL);
    CHECK(stageModTim(999, 0, tim0) == NULL);    /* not a pack */
    CHECK(stageModTim(LBA_A, 0, NULL) == NULL && stageModTim(LBA_A, -1, tim0) == NULL);

    /* a mod: tile 0 with palette entry 5 recoloured, the tile 1 PNG of the wrong size */
    touchMod("a");
    putTilePng("mods/a/z_stage/s233pack/00_00.png", 1, 256, 5, 255, 0, 0, TW, TH);
    putTilePng("mods/a/z_stage/s233pack/00_01.png", 2, 256, 5, 255, 0, 0, 16, 8);
    CHECK(modsInit(modsRoot, NULL) == 1);
    stageModSetup(dd, &io);
    r = stageModTim(LBA_A, 0, tim0);
    CHECK(r != NULL && r != tim0);
    if (r) {
        CHECK(clutAt(r, 5) == 0x001F);                                   /* red */
        CHECK(clutAt(r, 4) == clutAt(tim0, 4) && clutAt(r, 6) == clutAt(tim0, 6) && clutAt(r, 255) == clutAt(tim0, 255));
        CHECK(memcmp(r + (8 + 12 + 512) + 12, tim0 + (8 + 12 + 512) + 12, 2 * (TW / 2) * TH) == 0); /* pixels as on disc */
        CHECK(stageTimLength(r, 4096) == len0);
        CHECK(memcmp(r, tim0, 8 + 12) == 0);
    }
    CHECK(stageModTim(LBA_A, 1, tim1) == NULL); /* rejected: 16x8 against 32x8 */
    CHECK(stageModTim(LBA_A, 3, tim1) == NULL); /* no PNG for it */
    CHECK(stageModTim(LBA_B, 0, tim0) == NULL); /* the other pack has no files */
    CHECK(stageModTim(LBA_A, 0, tim0) == r);    /* kept: the same answer, the same buffer */

    /* the pixels change when the PNG's do (a pixel index) */
    {
        uint8_t px[TW * TH], rgb[256 * 3], alpha[256], *png;
        size_t pl;
        for (i = 0; i < 256; i++) {
            unsigned c = palColor(i);
            rgb[3 * i] = (uint8_t)((c & 31) << 3);
            rgb[3 * i + 1] = (uint8_t)(((c >> 5) & 31) << 3);
            rgb[3 * i + 2] = (uint8_t)(((c >> 10) & 31) << 3);
            alpha[i] = i ? 255 : 0;
        }
        for (i = 0; i < TW * TH; i++) px[i] = pixelAt(i % TW, i / TW, 2);
        px[1] = 77;
        CHECK(pngWriteIndexed(px, TW, TH, 8, rgb, 256, alpha, 1, &png, &pl) == 0);
        spit("mods/a/z_stage/s233pack/00_01.png", png, pl);
        free(png);
        stageModRecheck();
        r = stageModTim(LBA_A, 1, tim1); /* the file changed: looked at again */
        CHECK(r != NULL && r[8 + 12 + 512 + 12 + 1] == 77 && tim1[8 + 12 + 512 + 12 + 1] != 77);
    }

    /* last mod wins; a refused file of the last one falls to the one before */
    touchMod("b");
    putTilePng("mods/b/z_stage/s233pack/00_00.png", 1, 256, 5, 0, 255, 0, TW, TH);
    putTilePng("mods/b/z_stage/s233pack/00_01.png", 2, 256, 5, 0, 0, 255, 64, 8); /* wrong size */
    CHECK(modsInit(modsRoot, NULL) == 2);
    stageModSetup(dd, &io);
    r = stageModTim(LBA_A, 0, tim0);
    CHECK(r != NULL && clutAt(r, 5) == 0x03E0); /* b's green */
    r = stageModTim(LBA_A, 1, tim1);
    CHECK(r != NULL && r[8 + 12 + 512 + 12 + 1] == 77); /* b refused (64x8), a's applies */

    /* fewer colours than the CLUT: the first ones are replaced, the rest stay */
    putTilePng("mods/b/z_stage/s233pack/00_00.png", 1, 4, 2, 255, 255, 0, TW, TH);
    stageModRecheck();
    r = stageModTim(LBA_A, 0, tim0);
    CHECK(r != NULL && clutAt(r, 2) == 0x03FF && clutAt(r, 3) == clutAt(tim0, 3) && clutAt(r, 4) == clutAt(tim0, 4) &&
          clutAt(r, 100) == clutAt(tim0, 100));
    /* a mod whose folder is spelled in capitals still works (ADR-010) */
    touchMod("e");
    putTilePng("mods/e/Z_STAGE/S330PACK/00_00.PNG", 7, 256, 9, 1, 2, 3, TW, TH);
    CHECK(modsInit(modsRoot, NULL) == 3);
    stageModSetup(dd, &io);
    r = stageModTim(LBA_B, 0, tim0);
    CHECK(r != NULL);

    /* the data folder: the exporter's own PNG is not a replacement, a user-edited one is */
    modsInit(NULL, NULL);
    stageModSetup(dd, &io);
    stageExportSetup(dd, &gIo);
    CHECK(stageExportRequest("S233") == 1);
    while (stageExportPump(1000)) {
    }
    stageModRecheck();
    CHECK(stageModTim(LBA_A, 0, tim0) == NULL && stageModTim(LBA_A, 1, tim1) == NULL); /* exported, unedited */
    snprintf(rel, sizeof rel, "data/z_stage/s233pack/00_00.png");
    putTilePng(rel, 1, 256, 8, 9, 99, 200, TW, TH);
    stageModRecheck();
    r = stageModTim(LBA_A, 0, tim0);
    CHECK(r != NULL && clutAt(r, 8) == (unsigned)((9 >> 3) | ((99 >> 3) << 5) | ((200 >> 3) << 10)));
    CHECK(stageModTim(LBA_A, 1, tim1) == NULL);
    /* a mod beats the edited data file */
    touchMod("c");
    putTilePng("mods/c/z_stage/s233pack/00_00.png", 1, 256, 8, 255, 0, 0, TW, TH);
    CHECK(modsInit(modsRoot, "c") == 1);
    stageModSetup(dd, &io);
    r = stageModTim(LBA_A, 0, tim0);
    CHECK(r != NULL && clutAt(r, 8) == 0x001F);
    stageExportSetup(NULL, NULL);
    stageModSetup(NULL, NULL);
    modsInit(NULL, NULL);
    CHECK(stageModTim(LBA_A, 0, tim0) == NULL);
    rmtree(gRoot);
}

int main(void) {
    buildDisc();
    testFormats();
    testExport();
    testMod();
    if (failures) {
        printf("stagetest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("stagetest: OK\n");
    return 0;
}
