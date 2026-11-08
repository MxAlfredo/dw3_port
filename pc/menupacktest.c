#define _GNU_SOURCE 1
/*
 * Host test of the packer of the menus by piece (readable-assets A5.3): pc/shim/menu_pack.c, the hook it plugs into the image layer (image_data.c) and the
 * piece list of menu_export.c. A synthetic screen (four 16-bit pages of 64 x 16 halfwords and a sheet with seven parts in three frames, as menuexptest.c
 * builds it), its pieces cut by the exporter itself, and mods in a scratch folder under /tmp (made-up bytes, no retail data, no disc).
 *
 * Every check reads the pages back with the test's own code: where a texel lives, where a palette halfword sits. Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/port_log.h"

#include "shim/exp_core.h"
#include "shim/image_data.h"
#include "shim/menu_export.h"
#include "shim/menu_pack.h"
#include "shim/mods.h"
#include "shim/png_read.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "menupacktest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- the synthetic screen (the same as menuexptest.c) ---- */

#define PAGES 4
#define PW 64 /* halfwords of a page */
#define PH 16
#define TIM_BYTES (8 + 12 + 2 * PW * PH)
#define SCREEN_PATH "AAA/DAT/MCARD/STMCRDTM.BIN"

static uint16_t gPage[PAGES][PW * PH];

typedef struct Part {
    int u, v, w, h, clutX, clutY, mode;
} Part;

/* 0 and 2 name the same rectangle and CLUT; 1 sits on page 1 (palette on page 1 too); 3 crosses from page 0 into page 1 and falls off the bottom;
   4 is 8-bit with a 256-colour palette across the four pages; 5 is empty; 6 names a palette row the pages do not have */
static const Part kParts[7] = {
    {0, 0, 16, 8, 48, 14, 0}, {264, 0, 20, 6, 80, 14, 0}, {0, 0, 16, 8, 48, 14, 0}, {250, 14, 12, 4, 48, 14, 0},
    {128, 2, 8, 3, 0, 13, 1}, {0, 0, 0, 5, 48, 14, 0},   {0, 0, 8, 8, 48, 40, 0},
};

typedef struct Item {
    int part, x, y;
} Item;

typedef struct Frame {
    int id, row, rate, n;
    Item item[4];
} Frame;

static const Frame kFrames[3] = {
    {0, 0, -1, 2, {{0, 0, 0}, {1, 16, 0}}},
    {1, 1, 1, 3, {{0, 0, 0}, {2, 3, 2}, {3, 10, 10}}},
    {5, 0, 2, 4, {{4, 5, 5}, {5, 0, 0}, {6, 0, 0}, {9, 1, 1}}}, /* part 9 does not exist */
};

/* the pieces the exporter makes of it, by file name */
#define F_A "f000p00.png" /* part 0, CLUT row 14: 16x8 at (0, 0) on page 0 */
#define F_B "f000p01.png" /* part 1, 20x6 at (264, 0), palette on page 1 */
#define F_C "f001p00.png" /* part 0 again with CLUT row 15: the same rectangle as F_A, another palette */
#define F_D "f001p02.png" /* part 3, CLUT row 15 too: the same palette as F_C; reaches past the pages */
#define F_E "f005p00.png" /* part 4: 8-bit at (128, 2), 256 colours on row 13 of all four pages */
#define F_NONE "f005p02.png" /* part 6: no PNG (its palette is outside the pages) */

static uint16_t rgb555(int r, int g, int b) { return (uint16_t)(r | g << 5 | b << 10); }

static void w16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t v) {
    w16(p, v & 0xFFFF);
    w16(p + 2, v >> 16);
}

static void pagesFill(void) {
    uint32_t seed = 12345;
    int k, i, c;

    for (k = 0; k < PAGES; k++) {
        for (i = 0; i < PW * PH; i++) {
            seed = seed * 1103515245u + 12345u;
            gPage[k][i] = (uint16_t)(seed >> 8);
        }
    }
    for (c = 1; c < 16; c++) {
        gPage[0][14 * PW + 48 + c] = rgb555(c, 31 - c, 3);
        gPage[0][15 * PW + 48 + c] = rgb555(3, c, 31 - c);
        gPage[1][14 * PW + 16 + c] = rgb555(c, c, 7);
    }
    gPage[0][14 * PW + 48] = gPage[0][15 * PW + 48] = gPage[1][14 * PW + 16] = 0;
    for (k = 0; k < PAGES; k++) {
        for (c = 0; c < PW; c++) gPage[k][13 * PW + c] = (uint16_t)(k * PW + c ? rgb555((k * PW + c) & 31, (c * 3) & 31, k * 8) | 0x8000 : 0);
    }
}

static size_t makeArchive(uint8_t *out, const uint8_t *const *ents, const size_t *lens, int n, int term) {
    size_t at = (size_t)(n + (term ? 1 : 0)) * 4;
    int i;

    for (i = 0; i < n; i++) {
        w32(out + 4 * i, (uint32_t)at);
        memcpy(out + at, ents[i], lens[i]);
        at += lens[i];
        while (at % 4) out[at++] = 0;
    }
    if (term) w32(out + 4 * n, 0);
    return at;
}

/* the page k as a raw 16-bit TIM */
static void buildTim(int k, uint8_t *out) {
    int i;

    w32(out, 0x10);
    w32(out + 4, 2);
    w32(out + 8, 12 + 2 * PW * PH);
    w16(out + 12, 0);
    w16(out + 14, 0);
    w16(out + 16, PW);
    w16(out + 18, PH);
    for (i = 0; i < PW * PH; i++) w16(out + 20 + 2 * i, gPage[k][i]);
}

/* the *TM file: an archive holding a sub-archive of the four raw 16-bit pages (no RLEN) */
static size_t makeTm(uint8_t *out) {
    static uint8_t tim[PAGES][TIM_BYTES], sub[PAGES * TIM_BYTES + 64];
    const uint8_t *ents[PAGES];
    size_t lens[PAGES], subLen, topLens[1];
    const uint8_t *top[1];
    int k;

    for (k = 0; k < PAGES; k++) {
        buildTim(k, tim[k]);
        ents[k] = tim[k];
        lens[k] = sizeof tim[k];
    }
    subLen = makeArchive(sub, ents, lens, PAGES, 1);
    top[0] = sub;
    topLens[0] = subLen;
    return makeArchive(out, top, topLens, 1, 0);
}

/* the *CS file: [4] then the sheet (gfx.h: parts offset, ids offset, one offset per frame; parts; ids ended by 255; frames) */
static size_t makeCs(uint8_t *out) {
    const int nf = 3, np = 7;
    uint8_t *s = out + 4;
    size_t po = 8 + 4 * (size_t)nf, io = po + 14 * (size_t)np, fo = io + 4, f;
    int i, k;

    w32(out, 4);
    w32(s, (uint32_t)po);
    w32(s + 4, (uint32_t)io);
    for (i = 0; i < np; i++) {
        w16(s + po + 14 * i, (unsigned)kParts[i].u);
        w16(s + po + 14 * i + 2, (unsigned)kParts[i].v);
        w16(s + po + 14 * i + 4, (unsigned)kParts[i].w);
        w16(s + po + 14 * i + 6, (unsigned)kParts[i].h);
        w16(s + po + 14 * i + 8, (unsigned)kParts[i].clutX);
        w16(s + po + 14 * i + 10, (unsigned)kParts[i].clutY);
        w16(s + po + 14 * i + 12, (unsigned)kParts[i].mode);
    }
    for (i = 0; i < nf; i++) s[io + i] = (uint8_t)kFrames[i].id;
    s[io + nf] = 255;
    for (i = 0, f = fo; i < nf; i++) {
        w32(s + 8 + 4 * i, (uint32_t)f);
        w16(s + f, (unsigned)kFrames[i].n);
        w16(s + f + 2, (unsigned)kFrames[i].row);
        w16(s + f + 4, (unsigned)kFrames[i].rate);
        for (k = 0; k < kFrames[i].n; k++) {
            w16(s + f + 6 + 6 * k, (unsigned)kFrames[i].item[k].part);
            w16(s + f + 8 + 6 * k, (unsigned)kFrames[i].item[k].x);
            w16(s + f + 10 + 6 * k, (unsigned)kFrames[i].item[k].y);
        }
        f += 6 + 6 * (size_t)kFrames[i].n;
    }
    return 4 + f;
}

static uint8_t gTm[PAGES * 2200 + 256], gCs[512];
static size_t gTmLen, gCsLen;

/* ---- the files of the layers ---- */

static char gRoot[128];

static void putBytes(const char *rel, const uint8_t *d, size_t n) {
    char path[1000], dir[1000], *slash;
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "mkdir -p '%s'", path);
    slash = strrchr(dir, '/');
    *slash = '\'';
    slash[1] = 0;
    if (system(dir)) {
    }
    f = fopen(path, "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

static void modJson(const char *mod) {
    char rel[200], text[200];

    snprintf(rel, sizeof rel, "mods/%s/mod.json", mod);
    snprintf(text, sizeof text, "{\"format\":\"mod-v1\",\"name\":\"%s\",\"version\":\"1.0\"}", mod);
    putBytes(rel, (const uint8_t *)text, strlen(text));
}

typedef struct Out {
    char name[32];
    uint8_t *data;
    size_t len;
} Out;

static Out gOut[16];
static int gOutN;

static int sinkEmit(void *ctx, const char *rel, const uint8_t *data, size_t len) {
    (void)ctx;
    snprintf(gOut[gOutN].name, sizeof gOut[gOutN].name, "%s", rel);
    gOut[gOutN].data = (uint8_t *)malloc(len + 1);
    memcpy(gOut[gOutN].data, data, len);
    gOut[gOutN].len = len;
    gOutN++;
    return 0;
}

static const Out *pieceOut(const char *name) {
    int i;

    for (i = 0; i < gOutN; i++) {
        if (strcmp(gOut[i].name, name) == 0) return &gOut[i];
    }
    return NULL;
}

/* the PNG of a piece as the exporter wrote it, read */
static int readPiece(const char *name, PngImage *p) {
    const Out *o = pieceOut(name);

    return o != NULL && pngReadIndexed(o->data, o->len, p, NULL, 0) == 0;
}

/* a PngImage as a file of the layer `where` ("data", "mods/m1") */
static void putPng(const char *where, const char *name, const PngImage *p) {
    ImgPic pic;
    uint8_t *png;
    size_t len;
    char rel[200];
    int i;

    memset(&pic, 0, sizeof pic);
    pic.w = p->w;
    pic.h = p->h;
    pic.depth = p->nPal > 16 ? 8 : 4;
    pic.px = p->px;
    pic.nColors = p->nPal;
    memcpy(pic.rgb, p->rgb, sizeof pic.rgb);
    for (i = 0; i < 256; i++) pic.alpha[i] = i < p->nAlpha ? p->alpha[i] : 255;
    if (imgPicToPng(&pic, &png, &len) != 0) {
        CHECK(!"a PNG is made");
        return;
    }
    snprintf(rel, sizeof rel, "%s/menus/stmcrd/%s", where, name);
    putBytes(rel, png, len);
    free(png);
}

/* menuPackTotals counts from the start of the run: a case reads what it added */
static MenuPackStats gBase;

static MenuPackStats since(void) {
    MenuPackStats now;

    menuPackTotals(&now);
    now.pieces -= gBase.pieces;
    now.equal -= gBase.equal;
    now.rejected -= gBase.rejected;
    now.reduced -= gBase.reduced;
    now.conflicts -= gBase.conflicts;
    return now;
}

static void mark(void) { menuPackTotals(&gBase); }

/* the layers as they are before a case: the exporter's files in data/ (the manifest knows their hash), no mod; then `mods` mods (m1 and m2) */
static void layers(int mods) {
    ExpManifest man;
    char cmd[700], path[900];
    int i;

    snprintf(cmd, sizeof cmd, "rm -rf '%s/data' '%s/mods'", gRoot, gRoot);
    if (system(cmd)) {
    }
    snprintf(path, sizeof path, "%s/data/menus/manifest.json", gRoot);
    expManOpen(&man, "test", path, "menus");
    for (i = 0; i < gOutN; i++) {
        char rel[200], file[900], hex[17];

        snprintf(rel, sizeof rel, "data/menus/stmcrd/%s", gOut[i].name);
        putBytes(rel, gOut[i].data, gOut[i].len);
        snprintf(file, sizeof file, "stmcrd/%s", gOut[i].name);
        tdHashHex(tdHash(gOut[i].data, gOut[i].len), hex);
        expManSet(&man, file, hex);
    }
    expManSave(&man, 0);
    expManFree(&man);
    snprintf(path, sizeof path, "%s/mods", gRoot);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
    if (system(cmd)) {
    }
    if (mods >= 1) modJson("m1");
    if (mods >= 2) modJson("m2");
    snprintf(path, sizeof path, "%s/data", gRoot);
    assetSetData(path);
    snprintf(path, sizeof path, "%s/mods", gRoot);
    modsInit(path, NULL);
    menuPackStart();
    mark();
}

/* ---- the pages, before and after ---- */

static uint8_t gTim[PAGES][TIM_BYTES]; /* the disc's page images */
static uint8_t gNew[PAGES][TIM_BYTES]; /* after the hooks */

static unsigned half(const uint8_t tim[TIM_BYTES], int x, int y) {
    const uint8_t *p = tim + 20 + 2 * (y * PW + x);

    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

/* the index of texel column u, row v (mode 0: 4-bit, 1: 8-bit) of the pages `t` */
static int texelOf(uint8_t t[PAGES][TIM_BYTES], int u, int v, int mode) {
    int per = mode ? 2 : 4, hx = u / per;
    unsigned h = half(t[hx / PW], hx % PW, v);

    return mode ? (int)((h >> (8 * (u % 2))) & 255) : (int)((h >> (4 * (u % 4))) & 15);
}

/* what the hooks make of the disc's pages: 1 when the screen has anything to write; gNew holds the pages; *changed is how many pages the patch reported changed */
static int pack(int *changed) {
    const ImgMenuHooks *h = imgGetMenuHooks();
    int k, begin, c = 0;

    memcpy(gNew, gTim, sizeof gNew);
    begin = h->begin != NULL && h->begin(SCREEN_PATH, gTm, gTmLen, gCs, gCsLen);
    for (k = 0; k < PAGES && begin; k++) {
        int chain[2] = {0, k};

        if (h->patch(SCREEN_PATH, chain, 2, gNew[k], TIM_BYTES)) c++;
    }
    if (h->end != NULL) h->end(SCREEN_PATH);
    if (changed) *changed = c;
    return begin;
}

/* how many halfwords of the pages differ from the disc's (the texels and palette entries live in them) */
static int diffHalfwords(void) {
    int k, i, n = 0;

    for (k = 0; k < PAGES; k++) {
        for (i = 0; i < PW * PH; i++) {
            if (half(gTim[k], i % PW, i / PW) != half(gNew[k], i % PW, i / PW)) n++;
        }
        CHECK(memcmp(gTim[k], gNew[k], 20) == 0); /* the headers are never touched */
    }
    return n;
}

static int identical(void) { return memcmp(gTim, gNew, sizeof gTim) == 0; }

/* ---- the cases ---- */

/* an exported piece with one texel and/or colour changed, written into the layer `where` */
static void editPiece(const char *where, const char *name, int px, int py, int newIdx, int colour, unsigned rgb) {
    PngImage p;

    CHECK(readPiece(name, &p));
    if (px >= 0) p.px[py * p.w + px] = (uint8_t)newIdx;
    if (colour >= 0) {
        p.rgb[3 * colour] = (uint8_t)(rgb >> 16);
        p.rgb[3 * colour + 1] = (uint8_t)(rgb >> 8);
        p.rgb[3 * colour + 2] = (uint8_t)rgb;
        if (colour < p.nAlpha) p.alpha[colour] = 255;
    }
    putPng(where, name, &p);
    pngImageFree(&p);
}

static void testNothing(void) {
    MenuPackStats st;
    int k;

    /* no mod, and the data folder as the exporter left it: nothing counts */
    layers(0);
    CHECK(!pack(NULL) && identical());
    /* a mod that copies the whole export: every piece is read and equals the disc, so there is nothing to write */
    layers(1);
    for (k = 0; k < gOutN; k++) {
        char rel[200];

        if (strcmp(gOut[k].name, "menu.json") == 0) continue;
        snprintf(rel, sizeof rel, "mods/m1/menus/stmcrd/%s", gOut[k].name);
        putBytes(rel, gOut[k].data, gOut[k].len);
    }
    menuPackStart();
    CHECK(!pack(NULL) && identical());
    st = since();
    CHECK(st.pieces == 5 && st.equal == 5 && st.rejected == 0);
    /* the same copy forced through the write path: the TIM comes out byte for byte the disc's, no byte reported changed */
    menuPackWriteUnchanged(1);
    {
        int changed = -1;

        CHECK(pack(&changed) == 1 && changed == 0 && identical());
    }
    menuPackWriteUnchanged(0);
}

static void testPixel(void) {
    int u, v, was;

    layers(1);
    editPiece("mods/m1", F_A, 3, 2, 9, -1, 0); /* texel (3, 2) of the 16x8 piece at (0, 0) */
    was = texelOf(gTim, 3, 2, 0);
    CHECK(was != 9);
    CHECK(pack(NULL) == 1);
    CHECK(diffHalfwords() == 1); /* exactly one halfword of the pages changed ... */
    CHECK(texelOf(gNew, 3, 2, 0) == 9);
    for (v = 0; v < 8; v++) {
        for (u = 0; u < 16; u++) {
            if (u != 3 || v != 2) CHECK(texelOf(gNew, u, v, 0) == texelOf(gTim, u, v, 0)); /* ... and it is that texel's */
        }
    }
    /* the other piece of the same rectangle (F_C, row 15) was not touched: its palette is where it was */
    CHECK(half(gNew[0], 48 + 4, 15) == half(gTim[0], 48 + 4, 15) && half(gNew[0], 48 + 4, 14) == half(gTim[0], 48 + 4, 14));
}

static void testColour(void) {
    unsigned stp = half(gTim[0], 5, 13) & 0x8000u;
    int changed = 0;

    layers(1);
    /* F_A: colour 7 of its palette (page 0, column 55, row 14, no STP on the disc) becomes (200, 100, 50) */
    editPiece("mods/m1", F_A, -1, 0, 0, 7, 0xC86432);
    CHECK(pack(&changed) == 1 && changed == 1);
    CHECK(diffHalfwords() == 1);
    CHECK(half(gNew[0], 48 + 7, 14) == (unsigned)(200 >> 3 | (100 >> 3) << 5 | (50 >> 3) << 10) && half(gTim[0], 48 + 7, 14) != half(gNew[0], 48 + 7, 14));
    /* F_E: colour 5 of its 8-bit palette (column 5, row 13: the disc has the STP bit there) and colour 6 turned opaque black */
    CHECK(stp == 0x8000u);
    layers(1);
    {
        PngImage p;

        CHECK(readPiece(F_E, &p));
        p.rgb[3 * 5] = 0x10;
        p.rgb[3 * 5 + 1] = 0x20;
        p.rgb[3 * 5 + 2] = 0x30;
        p.rgb[3 * 6] = p.rgb[3 * 6 + 1] = p.rgb[3 * 6 + 2] = 0;
        p.alpha[6] = 255; /* opaque black */
        putPng("mods/m1", F_E, &p);
        pngImageFree(&p);
    }
    menuPackStart();
    CHECK(pack(NULL) == 1);
    CHECK(diffHalfwords() == 2);
    CHECK(half(gNew[0], 5, 13) == ((unsigned)(0x10 >> 3 | (0x20 >> 3) << 5 | (0x30 >> 3) << 10) | stp)); /* the new colour, the disc's STP kept */
    CHECK(half(gNew[0], 6, 13) == 0x8000u);                                                              /* opaque black stays drawn: 0x8000 */
    CHECK(half(gNew[0], 0, 13) == 0 && half(gTim[0], 0, 13) == 0);                                       /* a transparent entry stays 0x0000 */
}

static void testPaletteOtherPage(void) {
    PngImage p;
    unsigned h130 = half(gTim[2], 2, 13), h200 = half(gTim[3], 8, 13);

    layers(1);
    CHECK(readPiece(F_E, &p));
    p.rgb[3 * 130] = 255;
    p.rgb[3 * 130 + 1] = 0;
    p.rgb[3 * 130 + 2] = 0;
    p.rgb[3 * 200] = 0;
    p.rgb[3 * 200 + 1] = 0;
    p.rgb[3 * 200 + 2] = 248;
    putPng("mods/m1", F_E, &p);
    pngImageFree(&p);
    CHECK(pack(NULL) == 1);
    /* the pixels of F_E are on page 1 and its palette runs across the four pages: colour 130 is column 2 of page 2, colour 200 column 8 of page 3 */
    CHECK(diffHalfwords() == 2);
    CHECK(half(gNew[2], 2, 13) == (0x1Fu | (h130 & 0x8000u)) && half(gNew[3], 8, 13) == (0x1Fu << 10 | (h200 & 0x8000u)));
    CHECK(memcmp(gNew[1], gTim[1], TIM_BYTES) == 0 && memcmp(gNew[0], gTim[0], TIM_BYTES) == 0);
}

static void testWrongSize(void) {
    PngImage p;
    MenuPackStats st;

    layers(1);
    CHECK(readPiece(F_A, &p));
    {
        PngImage wide = p;

        wide.w = 17;
        wide.px = (uint8_t *)calloc((size_t)wide.w * (size_t)wide.h, 1);
        wide.px[5] = 3;
        putPng("mods/m1", F_A, &wide);
        free(wide.px);
    }
    pngImageFree(&p);
    menuPackStart();
    CHECK(!pack(NULL) && identical()); /* refused: the disc's piece stays */
    st = since();
    CHECK(st.rejected == 1);
    /* the next layer is tried: the data folder's edited file goes in */
    layers(1);
    {
        PngImage wide;

        CHECK(readPiece(F_A, &p));
        wide = p;
        wide.w = 17;
        wide.px = (uint8_t *)calloc((size_t)wide.w * (size_t)wide.h, 1);
        putPng("mods/m1", F_A, &wide);
        free(wide.px);
        p.px[1 * p.w + 1] = (uint8_t)((p.px[1 * p.w + 1] + 1) & 15);
        putPng("data", F_A, &p);
        pngImageFree(&p);
    }
    menuPackStart();
    CHECK(pack(NULL) == 1 && diffHalfwords() == 1);
    CHECK(texelOf(gNew, 1, 1, 0) != texelOf(gTim, 1, 1, 0));
    /* a PNG for a piece the exporter does not cut (F_NONE: palette outside the pages): ignored */
    layers(1);
    CHECK(readPiece(F_A, &p));
    putPng("mods/m1", F_NONE, &p);
    pngImageFree(&p);
    menuPackStart();
    CHECK(!pack(NULL) && identical());
}

/* texel (3, 2) of F_A in the pages after the hooks, for the layer cases */
static int texelA(void) { return texelOf(gNew, 3, 2, 0); }

static void testLayerOrder(void) {
    AssetLayer l;

    layers(2);
    CHECK(assetLayerAt(0, &l) && strstr(l.label, "m2") != NULL); /* the later mod wins */
    editPiece("data", F_A, 3, 2, 5, -1, 0);
    CHECK(pack(NULL) == 1 && texelA() == 5); /* the user's edited file in data/ counts */
    editPiece("mods/m1", F_A, 3, 2, 6, -1, 0);
    menuPackStart();
    CHECK(pack(NULL) == 1 && texelA() == 6); /* a mod over data/ */
    editPiece("mods/m2", F_A, 3, 2, 7, -1, 0);
    menuPackStart();
    CHECK(pack(NULL) == 1 && texelA() == 7); /* the later mod over the earlier one */
    CHECK(diffHalfwords() == 1);
    layers(2); /* only the earlier mod and data/ */
    editPiece("data", F_A, 3, 2, 5, -1, 0);
    editPiece("mods/m1", F_A, 3, 2, 6, -1, 0);
    CHECK(pack(NULL) == 1 && texelA() == 6);
    /* the data folder's file that is the exporter's own (its hash is the manifest's) is not an edit */
    layers(0);
    CHECK(!pack(NULL) && identical());
}

static void testSharedPieces(void) {
    MenuPackStats st;

    /* F_A and F_C are the same rectangle with two palettes: they share the indices. Disagreeing edits: the later layer wins */
    layers(2);
    editPiece("mods/m1", F_A, 3, 2, 5, -1, 0);
    editPiece("mods/m2", F_C, 3, 2, 6, -1, 0);
    CHECK(pack(NULL) == 1 && texelA() == 6 && diffHalfwords() == 1);
    st = since();
    CHECK(st.conflicts == 1);
    layers(2);
    editPiece("mods/m2", F_A, 3, 2, 5, -1, 0);
    editPiece("mods/m1", F_C, 3, 2, 6, -1, 0);
    CHECK(pack(NULL) == 1 && texelA() == 5);
    /* in one layer, the first piece (menu.json order) wins */
    layers(1);
    editPiece("mods/m1", F_C, 3, 2, 6, -1, 0);
    editPiece("mods/m1", F_A, 3, 2, 5, -1, 0);
    CHECK(pack(NULL) == 1 && texelA() == 5);
    /* an edit of one and an unedited copy of the other (a mod that carries the whole export) do not disagree */
    layers(1);
    {
        PngImage c;

        CHECK(readPiece(F_C, &c));
        putPng("mods/m1", F_C, &c);
        pngImageFree(&c);
    }
    editPiece("mods/m1", F_A, 3, 2, 5, -1, 0);
    menuPackStart();
    CHECK(pack(NULL) == 1 && texelA() == 5);
    st = since();
    CHECK(st.conflicts == 0);
    /* F_C and F_D share the palette at (48, 15): disagreeing colours, the later layer wins */
    layers(2);
    editPiece("mods/m1", F_C, -1, 0, 0, 3, 0xF80000);
    editPiece("mods/m2", F_D, -1, 0, 0, 3, 0x0000F8);
    CHECK(pack(NULL) == 1 && diffHalfwords() == 1 && half(gNew[0], 48 + 3, 15) == (0x1Fu << 10));
    /* the same palette edited the same way by both: no conflict */
    layers(2);
    editPiece("mods/m1", F_C, -1, 0, 0, 3, 0xF80000);
    editPiece("mods/m2", F_D, -1, 0, 0, 3, 0xF80000);
    menuPackStart();
    CHECK(pack(NULL) == 1 && half(gNew[0], 48 + 3, 15) == 0x1Fu);
    st = since();
    CHECK(st.conflicts == 0);
}

static void testReduce(void) {
    PngImage p;
    MenuPackStats st;
    int i, x, y, maxIdx = 0, changed;

    layers(1);
    CHECK(readPiece(F_A, &p));
    p.nPal = 20; /* 20 colours: more than a 4-bit piece holds */
    p.nAlpha = 20;
    for (i = 0; i < 20; i++) {
        p.rgb[3 * i] = (uint8_t)(i * 12);
        p.rgb[3 * i + 1] = (uint8_t)(255 - i * 12);
        p.rgb[3 * i + 2] = (uint8_t)(i * 5);
        p.alpha[i] = 255;
    }
    for (i = 0; i < p.w * p.h; i++) p.px[i] = (uint8_t)(i % 20);
    putPng("mods/m1", F_A, &p);
    pngImageFree(&p);
    menuPackStart();
    CHECK(pack(NULL) == 1);
    st = since();
    CHECK(st.reduced == 1 && st.rejected == 0);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 16; x++) {
            if (texelOf(gNew, x, y, 0) > maxIdx) maxIdx = texelOf(gNew, x, y, 0);
        }
    }
    CHECK(maxIdx > 0 && maxIdx < 16);
    changed = diffHalfwords();
    CHECK(changed > 0 && changed <= 4 * 8 + 16); /* its 4 x 8 halfwords and its palette */
}

/* the whole path: the image layer builds the pages of the file, the hook rewrites the one that has the edit, and nothing else counts as overridden */
static void testImageLayer(void) {
    ImgOverrides o;
    int i, n;
    const ImgOverride *ov = NULL;

    layers(1);
    n = imgOverridesBuild(&o, SCREEN_PATH, gTm, gTmLen, gCs, gCsLen);
    CHECK(n == 0);
    imgOverridesFree(&o);
    editPiece("mods/m1", F_A, 3, 2, 9, -1, 0);
    menuPackStart();
    n = imgOverridesBuild(&o, SCREEN_PATH, gTm, gTmLen, gCs, gCsLen);
    CHECK(n == 1); /* one page image overridden: the one with the pixel */
    for (i = 0; i < o.n; i++) {
        if (o.v[i].tim != NULL) ov = &o.v[i];
    }
    CHECK(ov != NULL && ov->len >= TIM_BYTES);
    if (ov != NULL) {
        int hx = 3 / 4, sh = 4 * (3 % 4);

        CHECK(((half(ov->tim, hx, 2) >> sh) & 15) == 9);
    }
    imgOverridesFree(&o);
    /* a mod without the hook running: nothing */
    menuPackReset();
    n = imgOverridesBuild(&o, SCREEN_PATH, gTm, gTmLen, gCs, gCsLen);
    CHECK(n == 0);
    imgOverridesFree(&o);
}

/* --validate-mod: the plan of a screen reports a PNG of the wrong size (with both sizes), a file no piece is called and a piece two mods ship; with the validator
   off only the refusal is logged. The log is the test's stderr, taken to a file. */
static char gLogPath[1000];
static int gSavedErr = -1;

static void logStart(void) {
    snprintf(gLogPath, sizeof gLogPath, "%s/validate.log", gRoot);
    fflush(stderr);
    gSavedErr = dup(2);
    if (freopen(gLogPath, "wb", stderr) == NULL) failures++;
}

static void logStop(char *out, size_t cap) {
    FILE *f;
    size_t n = 0;

    fflush(stderr);
    dup2(gSavedErr, 2);
    close(gSavedErr);
    clearerr(stderr);
    if ((f = fopen(gLogPath, "rb")) != NULL) {
        n = fread(out, 1, cap - 1, f);
        fclose(f);
    }
    out[n] = '\0';
}

static void testValidate(void) {
    PngImage p, wide;
    char log[8192];

    layers(2);
    editPiece("mods/m1", F_A, 3, 2, 9, -1, 0);        /* a good piece, in both mods: the later one wins */
    editPiece("mods/m2", F_A, 4, 2, 8, -1, 0);
    CHECK(readPiece(F_B, &p));
    wide = p;
    wide.w = p.w + 1;                                 /* the piece is 20 wide */
    wide.px = (uint8_t *)calloc((size_t)wide.w * (size_t)wide.h, 1);
    putPng("mods/m1", F_B, &wide);
    free(wide.px);
    putPng("mods/m1", "f999p99.png", &p);             /* no piece of the sheet is called that */
    pngImageFree(&p);
    menuPackStart();
    menuPackValidating(0);
    logStart();
    CHECK(pack(NULL) == 1);
    logStop(log, sizeof log);
    CHECK(strstr(log, "f000p01.png rejected: size 21x6, the piece is 20x6") != NULL);
    CHECK(strstr(log, "ignored") == NULL);            /* the reports of the validator are off at play time */
    menuPackStart();
    menuPackValidating(1);
    logStart();
    CHECK(pack(NULL) == 1);
    logStop(log, sizeof log);
    menuPackValidating(0);
    CHECK(strstr(log, "mods/m1/menus/stmcrd/f000p01.png rejected: size 21x6, the piece is 20x6") != NULL);
    CHECK(strstr(log, "mods/m1/menus/stmcrd/f999p99.png ignored: no piece of stmcrd is called that") != NULL);
    CHECK(strstr(log, "mods/m1/menus/stmcrd/f000p00.png ignored: the piece is also in mods/m2, which loads later and wins") != NULL);
    CHECK(strstr(log, "mods/m2/menus/stmcrd/f000p00.png ignored") == NULL);       /* the winner is not reported */
    CHECK(strstr(log, "menu.json ignored") == NULL);
}

int main(void) {
    char tmp[] = "/tmp/menupacktestXXXXXX";
    MenuExportStats unused;
    MenuStats st;
    int k;

    if (mkdtemp(tmp) == NULL) {
        fprintf(stdout, "menupacktest: no scratch folder\n");
        return 1;
    }
    snprintf(gRoot, sizeof gRoot, "%s", tmp);
    (void)unused;
    pagesFill();
    gTmLen = makeTm(gTm);
    gCsLen = makeCs(gCs);
    for (k = 0; k < PAGES; k++) buildTim(k, gTim[k]);
    memset(&st, 0, sizeof st);
    CHECK(menuScreenExport("stmcrd", SCREEN_PATH, "AAA/DAT/MCARD/STMCRDCS.BIN", gTm, gTmLen, gCs, gCsLen, sinkEmit, NULL, &st) == 5 && gOutN == 6);

    testNothing();
    testPixel();
    testColour();
    testPaletteOtherPage();
    testWrongSize();
    testLayerOrder();
    testSharedPieces();
    testReduce();
    testImageLayer();
    testValidate();

    {
        char cmd[700];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp);
        if (system(cmd)) {
        }
    }
    if (failures) {
        fprintf(stdout, "menupacktest: %d FAILED\n", failures);
        return 1;
    }
    fprintf(stdout, "menupacktest: all checks passed\n");
    return 0;
}
