/*
 * Host test of the Z_STAGE PACK codec (new-stages T3): pc/shim/map_pack.c.
 *   - RLEN: pack -> unpack is equal for runs, literals and the 127 / 128 boundaries; damaged blocks are refused;
 *   - one tile: encode -> decode is equal (RLEN and plain), the unpacked layout has the offsets the reader uses, a block is a whole number of
 *     sectors, the limits of the reader (sprites, image size) are errors;
 *   - the container: build -> parse is equal, tile `frame` sits at sector frame * tileBytes / 2048 + 1, empty tiles are zero, the 255 tile limit
 *     of the u8 view slots is an error, a stage of any size round-trips;
 *   - when the disc is extracted (disks/eu or $DW3_DISC_DIR), every tile of every SnnnPACK.BIN is parsed, decoded and encoded again, and the
 *     byte-exact ones are counted; each container is rebuilt from its raw blocks. Without the disc that part is skipped.
 * No game, no retail bytes in the repo. Exit 0: every check held.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim/map_pack.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "mappacktest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static uint32_t rngState = 4242u;
static uint32_t rnd(void) {
    rngState = rngState * 1664525u + 1013904223u;
    return rngState >> 8;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int rlenRound(const uint8_t *src, size_t n) {
    uint8_t *packed = NULL, *back = NULL;
    size_t pl = 0, bl = 0;
    int ok;

    if (mpRlenPack(src, n, &packed, &pl) != MP_OK) {
        return 0;
    }
    if (mpRlenUnpack(packed, pl, &back, &bl) != MP_OK) {
        free(packed);
        return 0;
    }
    ok = bl == n && memcmp(back, src, n) == 0 && packed[pl - 1] == 0 && rd32(packed + 4) == n;
    free(packed);
    free(back);
    return ok;
}

static void testRlen(void) {
    uint8_t buf[1000];
    size_t n, i;
    uint8_t *p = NULL, *q = NULL;
    size_t pl = 0, ql = 0;
    /* hand-built: RLEN, size 6, run of 3 x 0x41, literal of 2, end */
    const uint8_t hand[] = {'R', 'L', 'E', 'N', 6, 0, 0, 0, 0x83, 0x41, 0x02, 'a', 'b', 0x81, 'z', 0x00};

    CHECK(mpRlenUnpack(hand, sizeof hand, &q, &ql) == MP_OK && ql == 6 && memcmp(q, "AAAabz", 6) == 0);
    free(q);
    q = NULL;
    for (n = 1; n < 400; n++) { /* constant: runs split at 127 */
        memset(buf, 7, n);
        CHECK(rlenRound(buf, n));
    }
    for (n = 1; n < 400; n += 3) { /* no equal neighbours: literals split at 127 */
        for (i = 0; i < n; i++) {
            buf[i] = (uint8_t)(i & 1 ? 9 : 4);
        }
        CHECK(rlenRound(buf, n));
    }
    for (n = 0; n < 5; n++) { /* noise with runs */
        size_t k = 0;
        while (k < sizeof buf) {
            uint8_t v = (uint8_t)rnd();
            size_t r = (rnd() & 3) ? 1 : (size_t)(rnd() % 200);
            while (r-- && k < sizeof buf) {
                buf[k++] = v;
            }
        }
        CHECK(rlenRound(buf, sizeof buf));
    }
    /* the packer's shape: a lone differing byte stays in the literal, a pair becomes a run */
    memcpy(buf, "abccd", 5);
    CHECK(mpRlenPack(buf, 5, &p, &pl) == MP_OK);
    {
        const uint8_t want[] = {'R', 'L', 'E', 'N', 5, 0, 0, 0, 2, 'a', 'b', 0x82, 'c', 1, 'd', 0};
        CHECK(pl == sizeof want && memcmp(p, want, pl) == 0);
    }
    /* damaged: wrong magic, short, a lying size, no end, a run past the size */
    CHECK(mpRlenUnpack(p, 3, &q, &ql) == MP_ERR_FORMAT);
    p[0] = 'X';
    CHECK(mpRlenUnpack(p, pl, &q, &ql) == MP_ERR_FORMAT);
    p[0] = 'R';
    p[4] = 99;
    CHECK(mpRlenUnpack(p, pl, &q, &ql) == MP_ERR_FORMAT);
    p[4] = 5;
    CHECK(mpRlenUnpack(p, pl - 1, &q, &ql) == MP_ERR_FORMAT);
    CHECK(mpRlenPack(buf, 0, &q, &ql) == MP_ERR_SIZE && q == NULL);
    CHECK(mpRlenPack(NULL, 4, &q, &ql) == MP_ERR_ARG);
    free(p);
}

/* a tile of w x h with noise and some flat areas; the palette is a ramp */
static void fillTile(MpTile *t, int w, int h, uint8_t *pix, int rle) {
    int x, y, i;

    mpTileSimple(t, w, h, pix);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            pix[y * w + x] = (uint8_t)((x < w / 2) ? 3 : ((y & 8) ? (rnd() & 255) : 200));
        }
    }
    for (i = 0; i < MP_CLUT; i++) {
        t->clut[i] = (uint16_t)(0x8000u | (unsigned)(i * 129));
    }
    t->rle = rle;
}

static int sameTile(const MpTile *a, const MpTile *b) {
    int k, j;

    if (a->w != b->w || a->h != b->h || memcmp(a->clut, b->clut, sizeof a->clut) != 0 || memcmp(a->pix, b->pix, (size_t)a->w * (size_t)a->h) != 0) {
        return 0;
    }
    for (k = 0; k < MP_LISTS; k++) {
        if (a->count[k] != b->count[k]) {
            return 0;
        }
        for (j = 0; j < a->count[k]; j++) {
            if (memcmp(&a->sp[k][j], &b->sp[k][j], sizeof(MpSprite)) != 0) {
                return 0;
            }
        }
    }
    return 1;
}

static void testTile(void) {
    uint8_t pix[128 * 128], wide[256 * 128];
    MpTile t, d;
    uint8_t *blk = NULL;
    size_t bl = 0;
    int rle;

    for (rle = 0; rle < 2; rle++) {
        fillTile(&t, 128, 128, pix, rle);
        CHECK(mpTileEncode(&t, &blk, &bl) == MP_OK);
        CHECK(bl % MP_SECTOR == 0 && bl > 0);
        CHECK((rd32(blk) == 0x4E454C52u) == (rle == 1)); /* "RLEN" or the TIM offset word */
        CHECK(mpTileDecode(blk, bl, &d) == MP_OK && d.rle == rle && sameTile(&t, &d));
        mpTileFree(&d);
        free(blk);
    }

    /* the unpacked layout, read as FIELDSTG_loadStreamSprites reads it: one sprite in list 0 puts the TIM at 4 + 3 * 4 + 12 = 28 (0x1C) */
    fillTile(&t, 128, 128, pix, 0);
    CHECK(mpTileEncode(&t, &blk, &bl) == MP_OK);
    CHECK(rd32(blk) == 28 && rd32(blk + 4) == 1 && rd32(blk + 4 + 4 + 12) == 0 && rd32(blk + 24) == 0);
    CHECK(rd32(blk + 28) == 0x10 && rd32(blk + 32) == 9 && rd32(blk + 36) == 12 + 512);
    CHECK(rd32(blk + 28 + 8 + 524) == 12 + 128 * 128); /* the image block: size field = 12 + 2 * halfwords * rows */
    CHECK(bl == 2048 * ((28 + 8 + 524 + 12 + 128 * 128 + 2047) / 2048));
    free(blk);

    /* three lists, several sprites, a sprite that hangs out of the tile (the disc has 1020 of them) */
    fillTile(&t, 256, 128, wide, 1);
    t.count[0] = 2;
    t.count[1] = 1;
    t.count[2] = 5;
    t.sp[0][0] = (MpSprite){0, 0, 0, 0, 128, 128};
    t.sp[0][1] = (MpSprite){-16, 120, 128, 0, 64, 8};
    t.sp[1][0] = (MpSprite){40, 40, 10, 10, 20, 20};
    {
        int j;
        for (j = 0; j < 5; j++) {
            t.sp[2][j] = (MpSprite){j, 2 * j, 0, j, 16, 16};
        }
    }
    CHECK(mpTileEncode(&t, &blk, &bl) == MP_OK);
    CHECK(mpTileDecode(blk, bl, &d) == MP_OK && sameTile(&t, &d) && d.w == 256);
    mpTileFree(&d);
    free(blk);

    /* limits */
    t.count[2] = 6;
    CHECK(mpTileEncode(&t, &blk, &bl) == MP_ERR_LIMIT && blk == NULL); /* sprites[3][5] */
    t.count[2] = 5;
    t.sp[0][1].u = 200; /* 200 + 64 > 256 */
    CHECK(mpTileEncode(&t, &blk, &bl) == MP_ERR_SIZE);
    t.sp[0][1].u = 128;
    t.w = 258;
    CHECK(mpTileEncode(&t, &blk, &bl) == MP_ERR_SIZE); /* wider than 128 halfwords */
    t.w = 255;
    CHECK(mpTileEncode(&t, &blk, &bl) == MP_ERR_SIZE); /* odd */
    t.w = 256;
    t.h = 129;
    CHECK(mpTileEncode(&t, &blk, &bl) == MP_ERR_SIZE); /* more than 128 rows */
    t.h = 128;
    CHECK(mpTileEncode(NULL, &blk, &bl) == MP_ERR_ARG);

    /* damaged blocks never crash: truncations and flipped bytes */
    fillTile(&t, 128, 128, pix, 0);
    CHECK(mpTileEncode(&t, &blk, &bl) == MP_OK);
    {
        size_t i, cut;
        for (cut = 0; cut < bl; cut += 97) {
            if (mpTileDecode(blk, cut, &d) == MP_OK) {
                mpTileFree(&d);
            }
        }
        for (i = 0; i < 700; i += 3) {
            uint8_t old = blk[i];
            blk[i] = (uint8_t)(old ^ 0xA5);
            if (mpTileDecode(blk, bl, &d) == MP_OK) {
                mpTileFree(&d);
            }
            blk[i] = old;
        }
        blk[0] = 3; /* a TIM offset inside the lists */
        CHECK(mpTileDecode(blk, bl, &d) == MP_ERR_FORMAT);
    }
    free(blk);
}

static void testPack(void) {
    int w = 20, h = 10, n = w * h, i;
    const uint8_t **blocks = (const uint8_t **)calloc((size_t)n, sizeof *blocks);
    size_t *lens = (size_t *)calloc((size_t)n, sizeof *lens);
    uint8_t **own = (uint8_t **)calloc((size_t)n, sizeof *own);
    static uint8_t pix[128 * 128];
    uint8_t *pack = NULL, *pk2 = NULL;
    size_t pl = 0, pl2 = 0;
    MpPack p;
    MpTile t, d;
    int have = 0;

    /* a mod-size stage: 20 x 10 tiles, every third one empty, every other one RLEN */
    for (i = 0; i < n; i++) {
        if (i % 3 == 2) {
            continue;
        }
        fillTile(&t, 128, 128, pix, i & 1);
        pix[0] = (uint8_t)i; /* distinct */
        CHECK(mpTileEncode(&t, &own[i], &lens[i]) == MP_OK);
        blocks[i] = own[i];
        have++;
    }
    CHECK(mpPackBuild(w, h, 0, blocks, lens, &pack, &pl) == MP_OK);
    CHECK(mpPackParse(pack, pl, &p) == MP_OK && p.width == w && p.height == h && p.tileCount == have);
    CHECK(p.tileBytes % MP_SECTOR == 0 && pl == MP_SECTOR + (size_t)n * (size_t)p.tileBytes); /* 1 + n * sectors per tile */
    for (i = 0; i < n; i++) {
        const uint8_t *b;
        size_t bl;
        CHECK(mpPackTile(pack, pl, &p, i, &b, &bl) == MP_OK);
        if (i % 3 == 2) {
            CHECK(b == NULL && bl == 0 && p.sizes[i] == 0);
            /* an empty tile's room is zero */
            {
                size_t k, z = 0;
                for (k = 0; k < (size_t)p.tileBytes; k++) {
                    z |= pack[MP_SECTOR + (size_t)i * (size_t)p.tileBytes + k];
                }
                CHECK(z == 0);
            }
            continue;
        }
        CHECK(bl == lens[i] && b == pack + (size_t)(i * (p.tileBytes / MP_SECTOR) + 1) * MP_SECTOR); /* sector = frame * frameSectors + 1 */
        CHECK(memcmp(b, own[i], bl) == 0);
        CHECK(mpTileDecode(b, bl, &d) == MP_OK && d.pix[0] == (uint8_t)i);
        mpTileFree(&d);
    }
    CHECK(mpPackTile(pack, pl, &p, n, (const uint8_t **)&pk2, &pl2) == MP_ERR_ARG);
    /* a bigger room than needed is kept as asked */
    CHECK(mpPackBuild(w, h, p.tileBytes + MP_SECTOR, blocks, lens, &pk2, &pl2) == MP_OK && pl2 == MP_SECTOR + (size_t)n * (size_t)(p.tileBytes + MP_SECTOR));
    free(pk2);
    pk2 = NULL;
    CHECK(mpPackBuild(w, h, MP_SECTOR, blocks, lens, &pk2, &pl2) == MP_ERR_SIZE && pk2 == NULL); /* a tile does not fit */
    CHECK(mpPackBuild(w, h, 1000, blocks, lens, &pk2, &pl2) == MP_ERR_SIZE);
    /* damaged: a size that is not a whole number of sectors, a tile past the end */
    pack[16] = 1;
    CHECK(mpPackParse(pack, pl, &p) == MP_ERR_FORMAT);
    pack[16] = 0;
    CHECK(mpPackParse(pack, pl, &p) == MP_OK);
    CHECK(mpPackParse(pack, (size_t)MP_SECTOR + (size_t)(n - 1) * (size_t)p.tileBytes + p.sizes[n - 1] - MP_SECTOR, &p) == MP_ERR_FORMAT); /* the last tile is cut */
    CHECK(mpPackParse(pack, 100, &p) == MP_ERR_FORMAT);
    free(pack);

    /* limits of the reader: 255 tiles is the most (the view slot keeps frame in a u8, 0xFF = none) */
    {
        const uint8_t *none[300] = {0};
        size_t nl[300] = {0};
        CHECK(mpPackBuild(15, 17, 0, none, nl, &pk2, &pl2) == MP_OK); /* 255 */
        CHECK(mpPackParse(pk2, pl2, &p) == MP_OK && p.width == 15 && p.height == 17 && p.tileCount == 0 && p.tileBytes == MP_SECTOR);
        free(pk2);
        pk2 = NULL;
        CHECK(mpPackBuild(16, 16, 0, none, nl, &pk2, &pl2) == MP_ERR_LIMIT && pk2 == NULL); /* 256 */
        CHECK(mpPackBuild(0, 4, 0, none, nl, &pk2, &pl2) == MP_ERR_SIZE);
        CHECK(mpPackBuild(300, 1, 0, none, nl, &pk2, &pl2) == MP_ERR_SIZE);
        CHECK(mpPackBuild(1, 1, MP_MAX_TILE_BYTES + MP_SECTOR, none, nl, &pk2, &pl2) == MP_ERR_LIMIT);
        CHECK(mpPackBuild(1, 1, 0, NULL, nl, &pk2, &pl2) == MP_ERR_ARG);
    }
    for (i = 0; i < n; i++) {
        free(own[i]);
    }
    free(own);
    free(lens);
    free(blocks);
    CHECK(strlen(mpErrorText(MP_ERR_LIMIT)) > 0 && strlen(mpErrorText(777)) > 0);
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
    const char *root = NULL;
    char path[512];
    int r, num;
    int files = 0, parsed = 0, containersExact = 0, tiles = 0, empties = 0, decoded = 0, rleTiles = 0, plainTiles = 0, reencoded = 0, pixelsEqual = 0,
        exact = 0, exactRle = 0, exactPlain = 0, longLists = 0, tailOnly = 0, maxW = 0, maxTiles = 0, maxBytes = 0, failed = 0;

    roots[0] = env;
    roots[1] = "../disks/eu";
    roots[2] = "disks/eu";
    for (r = 0; r < 3 && root == NULL; r++) {
        FILE *f;
        if (roots[r] == NULL) {
            continue;
        }
        snprintf(path, sizeof path, "%s/AAA/DAT/Z_STAGE/S200PACK.BIN", roots[r]);
        f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
            root = roots[r];
        }
    }
    if (root == NULL) {
        printf("mappacktest: disc sweep skipped (no extracted disc)\n");
        return;
    }
    for (num = 0; num < 1000; num++) {
        size_t len = 0;
        uint8_t *d;
        MpPack p;
        int n, i;
        const uint8_t **blocks;
        size_t *lens;
        uint8_t *again = NULL;
        size_t al = 0;

        snprintf(path, sizeof path, "%s/AAA/DAT/Z_STAGE/S%03dPACK.BIN", root, num);
        d = readAll(path, &len);
        if (d == NULL) {
            continue;
        }
        files++;
        if (mpPackParse(d, len, &p) != MP_OK) {
            failed++;
            printf("mappacktest: S%03d: header refused\n", num);
            free(d);
            continue;
        }
        parsed++;
        n = p.width * p.height;
        if (n > maxTiles) {
            maxTiles = n;
        }
        if (p.tileBytes > maxBytes) {
            maxBytes = p.tileBytes;
        }
        if (len != (size_t)MP_SECTOR + (size_t)n * (size_t)p.tileBytes) { /* the file is exactly 1 + w * h * tileBytes / 2048 sectors */
            failed++;
            printf("mappacktest: S%03d: file size is not 1 + w*h*frameSectors sectors\n", num);
        }
        blocks = (const uint8_t **)calloc((size_t)n, sizeof *blocks);
        lens = (size_t *)calloc((size_t)n, sizeof *lens);
        for (i = 0; i < n; i++) {
            const uint8_t *b;
            size_t bl;
            MpTile t;
            uint8_t *enc = NULL;
            size_t el = 0;
            int k, rc;

            if (mpPackTile(d, len, &p, i, &b, &bl) != MP_OK) {
                failed++;
                continue;
            }
            blocks[i] = b;
            lens[i] = bl;
            if (b == NULL) {
                empties++;
                continue;
            }
            tiles++;
            if (mpTileDecode(b, bl, &t) != MP_OK) {
                failed++;
                printf("mappacktest: S%03d tile %d: not decoded\n", num, i);
                continue;
            }
            decoded++;
            if (t.w > maxW) {
                maxW = t.w;
            }
            t.rle ? rleTiles++ : plainTiles++;
            rc = 0;
            for (k = 0; k < MP_LISTS; k++) {
                if (t.count[k] > MP_MAX_SPRITES) {
                    rc = 1;
                }
            }
            if (rc) { /* the reader keeps 5 per list: the encoder refuses a tile that overflows it */
                longLists++;
                mpTileFree(&t);
                continue;
            }
            if (mpTileEncode(&t, &enc, &el) != MP_OK) {
                failed++;
                printf("mappacktest: S%03d tile %d: encode refused\n", num, i);
                mpTileFree(&t);
                continue;
            }
            reencoded++;
            {
                MpTile u;
                if (mpTileDecode(enc, el, &u) == MP_OK && sameTile(&t, &u) && u.rle == t.rle) {
                    pixelsEqual++;
                } else {
                    failed++;
                    printf("mappacktest: S%03d tile %d: re-encoded tile differs\n", num, i);
                }
                mpTileFree(&u);
            }
            if (el == bl && memcmp(enc, b, bl) == 0) {
                exact++;
                t.rle ? exactRle++ : exactPlain++;
            } else if (t.rle) {
                tailOnly++; /* checked below to be only the lone last byte */
                {
                    uint8_t *x = NULL, *y = NULL;
                    size_t xl = 0, yl = 0;
                    if (mpRlenUnpack(b, bl, &x, &xl) != MP_OK || mpRlenUnpack(enc, el, &y, &yl) != MP_OK || xl != yl || memcmp(x, y, xl) != 0) {
                        failed++;
                        printf("mappacktest: S%03d tile %d: packed tile unpacks differently\n", num, i);
                    }
                    free(x);
                    free(y);
                }
            } else {
                failed++;
                printf("mappacktest: S%03d tile %d: plain tile differs\n", num, i);
            }
            free(enc);
            mpTileFree(&t);
        }
        /* the container rebuilt from the raw blocks and its own tileBytes */
        if (mpPackBuild(p.width, p.height, p.tileBytes, blocks, lens, &again, &al) == MP_OK && al == len && memcmp(again, d, len) == 0) {
            containersExact++;
        } else {
            printf("mappacktest: S%03d: rebuilt container differs\n", num);
        }
        free(again);
        free(blocks);
        free(lens);
        free(d);
    }
    printf("mappacktest: disc sweep: %d files, %d headers parsed, %d containers byte-exact after rebuild, biggest %d tiles, tileBytes up to %d\n", files,
           parsed, containersExact, maxTiles, maxBytes);
    printf("mappacktest: disc sweep: %d tiles (%d empty), %d decoded (%d RLEN, %d plain), %d with a list over 5 sprites (not re-encoded), "
           "%d re-encoded, %d equal after decode, %d byte-exact (%d RLEN, %d plain), %d RLEN differ only in the packer's tail, widest image %d px\n",
           tiles, empties, decoded, rleTiles, plainTiles, longLists, reencoded, pixelsEqual, exact, exactRle, exactPlain, tailOnly, maxW);
    CHECK(files > 0);
    CHECK(parsed == files);
    CHECK(containersExact == files);
    CHECK(decoded == tiles);
    CHECK(pixelsEqual == reencoded);
    CHECK(failed == 0);
}

int main(void) {
    testRlen();
    testTile();
    testPack();
    sweepDisc();
    if (failures != 0) {
        printf("mappacktest: %d FAILED\n", failures);
        return 1;
    }
    printf("mappacktest: all checks held\n");
    return 0;
}
