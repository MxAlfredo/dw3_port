/*
 * Host test of the map layer codec (new-stages T2): pc/shim/map_layers.c.
 *   - synthetic layers: encode -> decode is equal, identical blocks are shared, the limits are errors;
 *   - damaged entries (truncated, flipped bytes) never crash and are refused;
 *   - the TMPK archive helpers (read entry n, build an archive) agree with each other;
 *   - when the disc is extracted (disks/eu or $DW3_DISC_DIR), every layer of every SnnnTMPK.BIN is decoded, encoded
 *     and decoded again, and the byte-exact ones are counted. Without the disc that part is skipped.
 * No game, no retail bytes in the repo. Exit 0: every check held.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim/map_layers.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "maplayertest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static uint32_t rngState = 12345u;
static uint32_t rnd(void) {
    rngState = rngState * 1664525u + 1013904223u;
    return rngState >> 8;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Number of distinct 8x8 blocks the entry stores: its pixel part is 64 bytes each */
static size_t pixelBlocks(const uint8_t *entry, size_t len) {
    return (len - rd32(entry + 20)) / 64;
}

static int roundTrip(const uint8_t *v, int w, int h, size_t *entryLen) {
    uint8_t *e = NULL;
    size_t n = 0;
    MlImage img;
    int ok;

    if (mlEncode(v, w, h, &e, &n) != ML_OK) {
        return 0;
    }
    if (mlDecode(e, n, &img) != ML_OK) {
        free(e);
        return 0;
    }
    ok = img.w == w && img.h == h && memcmp(img.v, v, (size_t)w * (size_t)h) == 0;
    if (entryLen != NULL) {
        *entryLen = n;
    }
    mlImageFree(&img);
    free(e);
    return ok;
}

static void testSynthetic(void) {
    int w = 384, h = 256, x, y, i;
    uint8_t *v = (uint8_t *)calloc((size_t)w * h, 1);
    uint8_t *e = NULL;
    size_t n = 0, n2 = 0;

    /* a flat map: the smallest entry, one block everywhere */
    CHECK(roundTrip(v, w, h, &n));
    CHECK(mlEncode(v, w, h, &e, &n) == ML_OK);
    CHECK(pixelBlocks(e, n) == 1);
    free(e);

    /* a few distinct stripes: the pixel part holds exactly the distinct blocks (all-zero, plus 3 shapes) */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            v[y * w + x] = (uint8_t)(((x >> 3) % 7 == 0) ? 4 : (((y >> 3) % 5 == 0) ? 6 : 0));
        }
    }
    CHECK(roundTrip(v, w, h, &n));
    CHECK(mlEncode(v, w, h, &e, &n) == ML_OK);
    CHECK(pixelBlocks(e, n) == 3); /* empty, column block, row block (the stripes are 8 px aligned) */
    free(e);

    /* random noise at 8 px granularity: round-trips, nothing is shared */
    for (i = 0; i < (w / 8) * (h / 8); i++) {
        int bx = (i % (w / 8)) * 8, by = (i / (w / 8)) * 8;
        uint8_t c = (uint8_t)(rnd() & 31);
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                v[(by + y) * w + bx + x] = (uint8_t)(c + (x == y));
            }
        }
    }
    CHECK(roundTrip(v, w, h, &n));

    /* the same image twice gives the same bytes; a shifted-by-one-block repeat does not grow the pixel part */
    CHECK(mlEncode(v, w, h, &e, &n) == ML_OK);
    {
        uint8_t *e2 = NULL;
        CHECK(mlEncode(v, w, h, &e2, &n2) == ML_OK);
        CHECK(n == n2 && memcmp(e, e2, n) == 0);
        free(e2);
    }
    free(e);

    /* a 1x1 cell map and a thin 255 x 1 one (the grid is u8 x u8 of 128 px cells) */
    {
        uint8_t one[128 * 128];
        uint8_t *wide = (uint8_t *)calloc((size_t)255 * 128 * 128, 1);
        memset(one, 9, sizeof one);
        CHECK(roundTrip(one, 128, 128, NULL));
        for (i = 0; i < 255; i++) {
            wide[3 * 255 * 128 + i * 128 + 5] = 7; /* the same mark in every cell */
        }
        CHECK(roundTrip(wide, 255 * 128, 128, NULL));
        free(wide);
    }
    free(v);
}

static void testLimits(void) {
    uint8_t *e = NULL;
    size_t n = 0;
    uint8_t small[128 * 128];
    int cx, cy;
    int w = 17 * 128, h = 16 * 128; /* 272 cells, every one different: more than the 256 a u8 grid value can name */
    uint8_t *v = (uint8_t *)calloc((size_t)w * h, 1);

    memset(small, 0, sizeof small);
    CHECK(mlEncode(small, 100, 128, &e, &n) == ML_ERR_SIZE); /* not a multiple of 128 */
    CHECK(mlEncode(small, 128, 0, &e, &n) == ML_ERR_SIZE);
    CHECK(mlEncode(small, 0, 128, &e, &n) == ML_ERR_SIZE);
    CHECK(mlEncode(small, 256 * 128, 128, &e, &n) == ML_ERR_SIZE); /* 256 cells wide: the grid width is a u8 */
    CHECK(mlEncode(NULL, 128, 128, &e, &n) == ML_ERR_ARG);
    CHECK(e == NULL);

    for (cy = 0; cy < 16; cy++) {
        for (cx = 0; cx < 17; cx++) {
            int id = cy * 17 + cx;
            v[(cy * 128) * w + cx * 128] = (uint8_t)(id & 255);
            v[(cy * 128) * w + cx * 128 + 8] = (uint8_t)(id >> 8);
        }
    }
    CHECK(mlEncode(v, w, h, &e, &n) == ML_ERR_TOO_MANY);
    CHECK(e == NULL);
    /* 256 different cells still fit */
    /* 15 x 16 = 240 different cells still fit (the empty quadrant is a 64 px node of its own, so 256 would not) */
    {
        int w2 = 15 * 128;
        uint8_t *v2 = (uint8_t *)calloc((size_t)w2 * h, 1);
        for (cy = 0; cy < 16; cy++) {
            for (cx = 0; cx < 15; cx++) {
                int id = cy * 15 + cx;
                v2[(cy * 128) * w2 + cx * 128] = (uint8_t)(id & 255);
                v2[(cy * 128) * w2 + cx * 128 + 8] = (uint8_t)(id >> 8);
            }
        }
        CHECK(roundTrip(v2, w2, h, NULL));
        free(v2);
    }
    free(v);
    CHECK(strlen(mlErrorText(ML_ERR_TOO_MANY)) > 0 && strlen(mlErrorText(12345)) > 0);
}

static void testDamaged(void) {
    int w = 256, h = 256, x, y;
    uint8_t *v = (uint8_t *)calloc((size_t)w * h, 1);
    uint8_t *e = NULL;
    size_t n = 0, cut, i;
    MlImage img;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            v[y * w + x] = (uint8_t)(((x >> 3) ^ (y >> 4)) & 7);
        }
    }
    CHECK(mlEncode(v, w, h, &e, &n) == ML_OK);
    /* every truncation either decodes to something self-consistent or is refused; none may crash */
    for (cut = 0; cut < n; cut += 7) {
        if (mlDecode(e, cut, &img) == ML_OK) {
            mlImageFree(&img);
        }
    }
    CHECK(mlDecode(e, 0, &img) != ML_OK);
    CHECK(mlDecode(e, 20, &img) != ML_OK);
    CHECK(mlDecode(NULL, 100, &img) != ML_OK);
    /* flipped bytes: an index past its table is refused, never read */
    for (i = 0; i < n; i += 3) {
        uint8_t old = e[i];
        e[i] = (uint8_t)(old ^ 0xFF);
        if (mlDecode(e, n, &img) == ML_OK) {
            mlImageFree(&img);
        }
        e[i] = old;
    }
    /* one pointed-out case: a grid cell naming a node that does not exist */
    e[rd32(e) + 2] = 255;
    CHECK(mlDecode(e, n, &img) == ML_ERR_FORMAT);
    free(e);
    free(v);
}

static void testArchive(void) {
    uint8_t a[128 * 128], b[256 * 128];
    uint8_t sheet[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint8_t *ea = NULL, *eb = NULL, *arc = NULL;
    size_t na = 0, nb = 0, narc = 0, sz = 0;
    const uint8_t *layers[2], *p = NULL;
    size_t lens[2];
    MlImage img;

    memset(a, 3, sizeof a);
    memset(b, 0, sizeof b);
    b[5000] = 22;
    CHECK(mlEncode(a, 128, 128, &ea, &na) == ML_OK);
    CHECK(mlEncode(b, 256, 128, &eb, &nb) == ML_OK);
    layers[0] = ea;
    layers[1] = eb;
    lens[0] = na;
    lens[1] = nb;

    CHECK(mlArchiveBuild(sheet, sizeof sheet, layers, lens, 2, &arc, &narc) == ML_OK);
    CHECK(mlArchiveCount(arc, narc) == 3);
    CHECK(mlArchiveEntry(arc, narc, 0, &p, &sz) == ML_OK && sz >= sizeof sheet && memcmp(p, sheet, sizeof sheet) == 0);
    CHECK(mlArchiveEntry(arc, narc, 1, &p, &sz) == ML_OK && sz >= na && memcmp(p, ea, na) == 0);
    CHECK(mlDecode(p, sz, &img) == ML_OK && img.w == 128 && img.v[0] == 3);
    mlImageFree(&img);
    CHECK(mlArchiveEntry(arc, narc, 2, &p, &sz) == ML_OK && mlDecode(p, sz, &img) == ML_OK && img.w == 256 && img.v[5000] == 22);
    mlImageFree(&img);
    CHECK(mlArchiveEntry(arc, narc, 3, &p, &sz) == ML_ERR_ARG);
    CHECK(mlArchiveEntry(arc, narc, -1, &p, &sz) == ML_ERR_ARG);
    CHECK(mlArchiveCount(arc, 3) < 0); /* truncated table */
    free(arc);
    arc = NULL;

    /* no sheet: entry 0 is empty */
    CHECK(mlArchiveBuild(NULL, 0, layers, lens, 1, &arc, &narc) == ML_OK);
    CHECK(mlArchiveCount(arc, narc) == 2);
    CHECK(mlArchiveEntry(arc, narc, 0, &p, &sz) == ML_OK && sz == 0);
    CHECK(mlArchiveEntry(arc, narc, 1, &p, &sz) == ML_OK && sz == na);
    free(arc);
    free(ea);
    free(eb);
}

/* ---- the disc sweep ---- */

static uint8_t *readAll(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    uint8_t *d;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (d == NULL || fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return d;
}

static void sweepDisc(void) {
    const char *env = getenv("DW3_DISC_DIR");
    const char *roots[3];
    char path[512];
    int r, num;
    int unbuildable = 0, archivesDiffer = 0, absent = 0, noLayerFiles = 0, files = 0, archivesExact = 0, layersOk = 0, layersBad = 0, notLayers = 0, exact = 0, roundOk = 0;
    const char *root = NULL;

    roots[0] = env;
    roots[1] = "../disks/eu";
    roots[2] = "disks/eu";
    for (r = 0; r < 3 && root == NULL; r++) {
        FILE *f;
        if (roots[r] == NULL) {
            continue;
        }
        snprintf(path, sizeof path, "%s/AAA/DAT/FIELD/STAGE/S345TMPK.BIN", roots[r]);
        f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
            root = roots[r];
        }
    }
    if (root == NULL) {
        printf("maplayertest: disc sweep skipped (no extracted disc)\n");
        return;
    }
    for (num = 0; num < 1000; num++) {
        size_t len = 0, nEnt, i;
        uint8_t *d;
        int count;
        const uint8_t **slotPtr;
        size_t *slotLen;
        uint8_t **owned;

        snprintf(path, sizeof path, "%s/AAA/DAT/FIELD/STAGE/S%03dTMPK.BIN", root, num);
        d = readAll(path, &len);
        if (d == NULL) {
            continue;
        }
        files++;
        count = mlArchiveCount(d, len);
        CHECK(count >= 1);
        if (count < 2) { /* a sheet only: no layers */
            if (count == 1) {
                noLayerFiles++;
            }
            free(d);
            continue;
        }
        nEnt = (size_t)count;
        slotPtr = (const uint8_t **)calloc(nEnt, sizeof *slotPtr);
        slotLen = (size_t *)calloc(nEnt, sizeof *slotLen);
        owned = (uint8_t **)calloc(nEnt, sizeof *owned);
        for (i = 0; i < nEnt; i++) {
            const uint8_t *e;
            size_t el;
            MlImage a, b;
            uint8_t *enc = NULL;
            size_t encLen = 0;

            CHECK(mlArchiveEntry(d, len, (int)i, &e, &el) == ML_OK);
            slotPtr[i] = e;
            slotLen[i] = el;
            if (e == NULL) { /* an absent entry */
                absent++;
                continue;
            }
            if (i == 0) { /* the sprite sheet: copied as is */
                continue;
            }
            if (mlDecode(e, el, &a) != ML_OK) { /* not a layer: copied as is */
                if (++notLayers <= 12) {
                    printf("maplayertest: S%03d entry %d (%u bytes): not a layer\n", num, (int)i, (unsigned)el);
                }
                continue;
            }
            if (mlEncode(a.v, a.w, a.h, &enc, &encLen) != ML_OK) {
                layersBad++;
                printf("maplayertest: S%03d entry %d: encode refused\n", num, (int)i);
                mlImageFree(&a);
                continue;
            }
            if (mlDecode(enc, encLen, &b) == ML_OK && a.w == b.w && a.h == b.h && memcmp(a.v, b.v, (size_t)a.w * a.h) == 0) {
                roundOk++;
            } else {
                layersBad++;
                printf("maplayertest: S%03d entry %d: round trip differs\n", num, (int)i);
            }
            layersOk++;
            if (encLen == el && memcmp(enc, e, el) == 0) {
                exact++;
            }
            owned[i] = enc;
            slotPtr[i] = enc;
            slotLen[i] = encLen;
            mlImageFree(&a);
            mlImageFree(&b);
        }
        /* the whole archive rebuilt from its entries (layers re-encoded, anything else copied): the same bytes? */
        if (slotPtr[0] == NULL) {
            unbuildable++; /* leading absent slots: the builder always puts entry 0 first */
        } else {
            uint8_t *arc = NULL;
            size_t narc = 0;
            if (mlArchiveBuild(slotPtr[0], slotLen[0], slotPtr + 1, slotLen + 1, (int)nEnt - 1, &arc, &narc) == ML_OK) {
                if (narc == len && memcmp(arc, d, len) == 0) {
                    archivesExact++;
                } else if (++archivesDiffer <= 3) {
                    printf("maplayertest: S%03d: archive rebuilt differs (%u vs %u bytes)\n", num, (unsigned)narc, (unsigned)len);
                }
                free(arc);
            }
        }
        for (i = 0; i < nEnt; i++) {
            free(owned[i]);
        }
        free(owned);
        free(slotPtr);
        free(slotLen);
        free(d);
    }
    printf("maplayertest: disc sweep: %d files, %d layers decoded, %d round-trip equal, %d byte-exact, %d entries not layers, "
           "%d archives byte-exact (%d not rebuildable: leading absent slots), "
           "%d files with no layer, %d absent entries\n",
           files, layersOk, roundOk, exact, notLayers, archivesExact, unbuildable, noLayerFiles, absent);
    CHECK(files > 0);
    CHECK(layersBad == 0);
    CHECK(roundOk == layersOk);
}

int main(void) {
    testSynthetic();
    testLimits();
    testDamaged();
    testArchive();
    sweepDisc();
    if (failures != 0) {
        printf("maplayertest: %d FAILED\n", failures);
        return 1;
    }
    printf("maplayertest: all checks held\n");
    return 0;
}
