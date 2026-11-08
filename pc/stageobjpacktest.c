/*
 * Host test of the packer of the stage objects by piece (readable-assets A6.3): pc/shim/stage_obj_pack.c on the menus' packer (menu_pack.c) and the stage hooks of
 * the image layer (image_data.c). A synthetic stage: a MASK file (an 8-bit TIM with a one-row CLUT, a raw 16-bit page with 4-bit art and its palette, a second
 * 8-bit TIM whose two-row CLUT covers the first one's), a TMPK file whose sprite sheet is entry 1, its pieces cut by the exporter itself, and mods and a data
 * folder in a scratch folder under /tmp (made-up bytes, no retail data, no disc).
 *
 * Every check builds the MASK file the way the game does (imgOverridesBuild, the overridden TIMs spliced in) and compares it with the original byte by byte, so
 * "exactly that texel" and "exactly that halfword of that TIM" are checked on the file's bytes. Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/exp_core.h"
#include "shim/image_data.h"
#include "shim/menu_pack.h"
#include "shim/mods.h"
#include "shim/png_read.h"
#include "shim/stage_obj_export.h"
#include "shim/stage_obj_pack.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "stageobjpacktest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- the synthetic stage ---- */

#define PW 64 /* halfwords of a page */
#define PH 16
#define STAGE 202
#define MASK_PATH "AAA/DAT/FIELD/STAGE/S202MASK.BIN"
#define MASK_OTHER "AAA/DAT/FIELD/STAGE/S203MASK.BIN" /* the same bytes, a stage nobody edits */
#define TMPK_PATH "AAA/DAT/FIELD/STAGE/S202TMPK.BIN"
#define SHEET_ENTRY 1

static uint16_t gPage0[PW * PH]; /* the first 8-bit TIM's image */
static uint16_t gPage1[PW * PH]; /* the 4-bit art, with its palette (column 16, row 5) */
static uint16_t gPage2[PW * PH]; /* the second 8-bit TIM's image */
static uint16_t gClutA[1][256];  /* the CLUT of the first TIM: one row, which the second one covers */
static uint16_t gClutB[2][256];  /* the CLUT of the second TIM: the game keeps it (the last wins) */

typedef struct Part {
    int u, v, w, h, clutX, clutY, mode;
} Part;

/* 0: 4-bit on page 1, its palette on page 1 too; 1: 8-bit on page 0, 256 colours from row 0 of the CLUT area (clutY 256) */
static const Part kParts[2] = {
    {256, 0, 16, 8, 64 + 16, 5, 0},
    {0, 2, 20, 6, 0, 256, 1},
};
#define F_4BIT "f000p00.png"
#define F_8BIT "f000p01.png"
#define F_4BIT_ROW1 "f001p00.png" /* part 0 again with CLUT row 6: the same rectangle as F_4BIT */

static uint16_t rgb555(int r, int g, int b) { return (uint16_t)(r | g << 5 | b << 10); }

static void w16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t v) {
    w16(p, v & 0xFFFF);
    w16(p + 2, v >> 16);
}
static unsigned r16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

static void stageFill(void) {
    uint32_t seed = 4242;
    int i, c;

    for (i = 0; i < PW * PH; i++) {
        seed = seed * 1103515245u + 12345u;
        gPage0[i] = (uint16_t)(seed >> 8);
        seed = seed * 1103515245u + 12345u;
        gPage1[i] = (uint16_t)(seed >> 8);
        seed = seed * 1103515245u + 12345u;
        gPage2[i] = (uint16_t)(seed >> 8);
    }
    for (c = 0; c < 256; c++) {
        gClutA[0][c] = c ? (uint16_t)(rgb555(c & 31, 3, 20) | 0x8000) : 0; /* what a wrong write would land on: different from the second TIM's */
        gClutB[0][c] = c ? (uint16_t)(rgb555(c & 31, 9, 1) | (c % 2 ? 0x8000 : 0)) : 0; /* the game's: some colours have the STP bit */
        gClutB[1][c] = c ? rgb555(1, c & 31, 9) : 0;
    }
    for (c = 1; c < 16; c++) {
        gPage1[5 * PW + 16 + c] = (uint16_t)(rgb555(c, 31 - c, 3) | (c == 3 ? 0x8000 : 0));
        gPage1[6 * PW + 16 + c] = rgb555(3, c, 31 - c);
    }
    gPage1[5 * PW + 16] = gPage1[6 * PW + 16] = 0;
}

static size_t timBlock(uint8_t *out, int mode, int clutRows, const uint16_t *clut, const uint16_t *img) {
    uint8_t *p = out;
    int i;

    w32(p, 0x10);
    w32(p + 4, (uint32_t)mode | (clut != NULL ? 8u : 0u));
    p += 8;
    if (clut != NULL) {
        w32(p, 12 + 2 * 256 * (uint32_t)clutRows);
        w16(p + 4, 0);
        w16(p + 6, 256);
        w16(p + 8, 256);
        w16(p + 10, (unsigned)clutRows);
        for (i = 0; i < 256 * clutRows; i++) w16(p + 12 + 2 * i, clut[i]);
        p += 12 + 2 * 256 * (size_t)clutRows;
    }
    w32(p, 12 + 2 * PW * PH);
    w16(p + 4, 0);
    w16(p + 6, 0);
    w16(p + 8, PW);
    w16(p + 10, PH);
    for (i = 0; i < PW * PH; i++) w16(p + 12 + 2 * i, img[i]);
    return (size_t)(p - out) + 12 + 2 * PW * PH;
}

static uint8_t gMask[3 * 4096];
static size_t gMaskLen;
static size_t gTimAt[3]; /* where each TIM starts in the MASK file: the image layer reports its overrides by offset */

static size_t makeMask(uint8_t *out) {
    static uint8_t t[3][4096];
    size_t len[3], at = 4 * 3;
    int k;

    len[0] = timBlock(t[0], 1, 1, &gClutA[0][0], gPage0);
    len[1] = timBlock(t[1], 2, 0, NULL, gPage1);
    len[2] = timBlock(t[2], 1, 2, &gClutB[0][0], gPage2);
    for (k = 0; k < 3; k++) {
        w32(out + 4 * k, (uint32_t)at);
        gTimAt[k] = at;
        memcpy(out + at, t[k], len[k]);
        at += len[k];
        while (at % 4) out[at++] = 0;
    }
    return at;
}

/* the sprite sheet (gfx.h: parts offset, ids offset, one offset per frame; parts; ids ended by 255; frames): frame 0 draws both parts, frame 1 draws part 0 again
   with the next CLUT row, so its piece shares the first one's rectangle */
static size_t makeSheet(uint8_t *s) {
    size_t po = 8 + 4 * 2, io = po + 14 * 2, f0 = io + 4, f1 = f0 + 6 + 6 * 2;
    int i;

    w32(s, (uint32_t)po);
    w32(s + 4, (uint32_t)io);
    for (i = 0; i < 2; i++) {
        w16(s + po + 14 * i, (unsigned)kParts[i].u);
        w16(s + po + 14 * i + 2, (unsigned)kParts[i].v);
        w16(s + po + 14 * i + 4, (unsigned)kParts[i].w);
        w16(s + po + 14 * i + 6, (unsigned)kParts[i].h);
        w16(s + po + 14 * i + 8, (unsigned)kParts[i].clutX);
        w16(s + po + 14 * i + 10, (unsigned)kParts[i].clutY);
        w16(s + po + 14 * i + 12, (unsigned)kParts[i].mode);
    }
    s[io] = 0;
    s[io + 1] = 1;
    s[io + 2] = 255;
    w32(s + 8, (uint32_t)f0);
    w32(s + 12, (uint32_t)f1);
    w16(s + f0, 2); /* frame 0: row 0, two parts */
    w16(s + f0 + 2, 0);
    w16(s + f0 + 4, 0xFFFF);
    w16(s + f0 + 6, 0);
    w16(s + f0 + 8, 0);
    w16(s + f0 + 10, 0);
    w16(s + f0 + 12, 1);
    w16(s + f0 + 14, 16);
    w16(s + f0 + 16, 0);
    w16(s + f1, 1); /* frame 1: row 1, part 0 */
    w16(s + f1 + 2, 1);
    w16(s + f1 + 4, 0xFFFF);
    w16(s + f1 + 6, 0);
    w16(s + f1 + 8, 0);
    w16(s + f1 + 10, 0);
    return f1 + 12;
}

/* the TMPK file: entry 0 is a map layer that is not a sheet, entry 1 is the sheet (S202's layout) */
static uint8_t gTmpk[1024];
static size_t gTmpkLen;

static size_t makeTmpk(uint8_t *out) {
    static uint8_t sheet[512];
    size_t sl = makeSheet(sheet), at = 4 * ((size_t)SHEET_ENTRY + 1);

    w32(out, (uint32_t)at);
    memset(out + at, 0, 8);
    at += 8;
    w32(out + 4 * SHEET_ENTRY, (uint32_t)at);
    memcpy(out + at, sheet, sl);
    return at + sl;
}

/* ---- the pieces the exporter makes, and the files of the layers ---- */

typedef struct Out {
    char name[32];
    uint8_t *data;
    size_t len;
} Out;

static Out gOut[8];
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

static char gRoot[128];

static void putBytes(const char *rel, const uint8_t *d, size_t n) {
    char path[1000], cmd[1100], *slash;
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
    slash = strrchr(cmd, '/');
    slash[0] = '\'';
    slash[1] = 0;
    if (system(cmd)) {
    }
    f = fopen(path, "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

/* a PngImage as the piece `name` of stage 202 in the layer `where` ("data", "mods/m1") */
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
    snprintf(rel, sizeof rel, "%s/stages/s%03d/objects/%s", where, STAGE, name);
    putBytes(rel, png, len);
    free(png);
}

static void modJson(const char *mod) {
    char rel[200], text[200];

    snprintf(rel, sizeof rel, "mods/%s/mod.json", mod);
    snprintf(text, sizeof text, "{\"format\":\"mod-v1\",\"name\":\"%s\",\"version\":\"1.0\"}", mod);
    putBytes(rel, (const uint8_t *)text, strlen(text));
}

/* an exported piece with one texel and/or one colour changed, written into the layer `where` */
static void editPiece(const char *where, const char *name, int px, int py, int newIdx, int colour, unsigned rgb) {
    PngImage p;
    const Out *o = pieceOut(name);

    CHECK(o != NULL && pngReadIndexed(o->data, o->len, &p, NULL, 0) == 0);
    if (o == NULL) return;
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

/* the layers as they are before a case: the exporter's files in data/ with their manifest and the stage data that names the sheet, `mods` mods (m1, m2), and the
   index of the stages with edited pieces (the case writes its edits first, then calls start()) */
static void layers(int mods) {
    ExpManifest man;
    char cmd[700], path[900], json[80];
    int i;

    snprintf(cmd, sizeof cmd, "rm -rf '%s/data' '%s/mods'", gRoot, gRoot);
    if (system(cmd)) {
    }
    snprintf(path, sizeof path, "%s/data/stages/manifest.json", gRoot);
    expManOpen(&man, "test", path, "stages");
    for (i = 0; i < gOutN; i++) {
        char rel[440], file[440], hex[17];

        snprintf(rel, sizeof rel, "data/stages/s%03d/objects/%s", STAGE, gOut[i].name);
        putBytes(rel, gOut[i].data, gOut[i].len);
        snprintf(file, sizeof file, "s%03d/objects/%s", STAGE, gOut[i].name);
        tdHashHex(tdHash(gOut[i].data, gOut[i].len), hex);
        expManSet(&man, file, hex);
    }
    expManSave(&man, 0);
    expManFree(&man);
    snprintf(path, sizeof path, "data/stages/s%03d/stage.json", STAGE);
    snprintf(json, sizeof json, "{\"sheetEntry\": %d}", 3 << 16 | SHEET_ENTRY); /* the TMPK file is file 3 of the game's table */
    putBytes(path, (const uint8_t *)json, strlen(json));
    snprintf(path, sizeof path, "data/stages/s%03d/stage.json", STAGE + 1); /* the stage nobody edits has stage data too: only the index keeps it from being built */
    putBytes(path, (const uint8_t *)json, strlen(json));
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/mods'", gRoot);
    if (system(cmd)) {
    }
    if (mods >= 1) modJson("m1");
    if (mods >= 2) modJson("m2");
}

static void start(void) {
    char path[900];

    snprintf(path, sizeof path, "%s/data", gRoot);
    assetSetData(path);
    snprintf(path, sizeof path, "%s/mods", gRoot);
    modsInit(path, NULL);
    menuPackStart();
    stageObjPackStart();
}

/* ---- the MASK file the game gets ---- */

static uint8_t gNew[sizeof gMask];

/* the image layer builds the file's overrides; the overridden TIMs go where they were. Returns how many images were overridden. */
static int build(const char *discPath) {
    ImgOverrides o;
    int i, n;

    memcpy(gNew, gMask, gMaskLen);
    n = imgOverridesBuild(&o, discPath, gMask, gMaskLen, gTmpk, gTmpkLen);
    for (i = 0; i < o.n; i++) {
        if (o.v[i].tim != NULL) memcpy(gNew + o.v[i].off, o.v[i].tim, o.v[i].len);
    }
    imgOverridesFree(&o);
    return n;
}

static int identical(void) { return memcmp(gMask, gNew, gMaskLen) == 0; }

/* the bytes that differ from the original: how many, and the first one's offset */
static int diffBytes(size_t *first) {
    size_t i;
    int n = 0;

    for (i = 0; i < gMaskLen; i++) {
        if (gMask[i] != gNew[i] && n++ == 0 && first) *first = i;
    }
    return n;
}

/* where, in the MASK file, halfword (col, row) of the image of TIM k and entry (col, row) of its CLUT are (TIM layout: 8 header bytes, then the CLUT block of 12 +
   2 * 256 * rows, then the image block of 12 + the pixels) */
static size_t imageHalf(int k, int col, int row) {
    int clutRows = k == 0 ? 1 : k == 2 ? 2 : 0;
    size_t base = gTimAt[k] + 8 + (clutRows ? 12 + 2 * 256 * (size_t)clutRows : 0) + 12;

    return base + 2 * ((size_t)row * PW + (size_t)col);
}

static size_t clutHalf(int k, int col, int row) { return gTimAt[k] + 8 + 12 + 2 * ((size_t)row * 256 + (size_t)col); }

static unsigned expect555(unsigned rgb, unsigned discHalf) {
    return (unsigned)((rgb >> 16 & 255) >> 3 | (rgb >> 8 & 255) >> 3 << 5 | (rgb & 255) >> 3 << 10) | (discHalf & 0x8000u);
}

/* ---- the cases ---- */

static void testNothing(void) {
    StageObjPackStats st;
    MenuPackStats ms, ms0;

    /* no mod, and the data folder as the exporter left it: no stage has edits, nothing is built */
    layers(0);
    start();
    menuPackTotals(&ms0);
    stageObjPackTotals(&st);
    CHECK(st.edited == 0);
    CHECK(build(MASK_PATH) == 0 && identical());
    stageObjPackTotals(&st);
    menuPackTotals(&ms);
    CHECK(st.built == 0 && ms.screens == ms0.screens);
    /* a mod that copies the whole export: the stage is built, every piece equals the disc and the file is byte for byte the disc's */
    layers(1);
    {
        int i;

        for (i = 0; i < gOutN; i++) {
            char rel[440];

            if (strstr(gOut[i].name, ".png") == NULL) continue;
            snprintf(rel, sizeof rel, "mods/m1/stages/s%03d/objects/%s", STAGE, gOut[i].name);
            putBytes(rel, gOut[i].data, gOut[i].len);
        }
    }
    start();
    menuPackTotals(&ms0);
    CHECK(build(MASK_PATH) == 0 && identical());
    stageObjPackTotals(&st);
    menuPackTotals(&ms);
    CHECK(st.edited == 1 && st.built == 1 && ms.pieces - ms0.pieces == 3 && ms.equal - ms0.equal == 3 && ms.rejected == 0);
}

static void testPixel(void) {
    size_t at = 0;
    unsigned was, now;

    layers(1);
    editPiece("mods/m1", F_4BIT, 3, 2, 9, -1, 0); /* texel (3, 2) of the 16x8 piece at (256, 0): page 1, column 0, row 2, nibble 3 */
    start();
    CHECK(build(MASK_PATH) == 1); /* one image overridden: the page with the texel */
    CHECK(diffBytes(&at) == 1 || diffBytes(&at) == 2); /* a nibble changes within one halfword: one or two of its bytes */
    CHECK(at == imageHalf(1, 0, 2) || at == imageHalf(1, 0, 2) + 1);
    was = r16(gMask + imageHalf(1, 0, 2));
    now = r16(gNew + imageHalf(1, 0, 2));
    CHECK(((was >> 12) & 15) != 9 && ((now >> 12) & 15) == 9 && (was & 0x0FFF) == (now & 0x0FFF));
}

static void testColour(void) {
    size_t at = 0;
    unsigned was;

    /* the 8-bit piece: colour 5 (the disc's STP bit is set there) becomes (200, 100, 50). The game keeps the second TIM's CLUT, so that is where it goes */
    layers(1);
    editPiece("mods/m1", F_8BIT, -1, 0, 0, 5, 0xC86432);
    start();
    CHECK(build(MASK_PATH) == 1);
    CHECK(diffBytes(&at) == 2 && (at == clutHalf(2, 5, 0) || at == clutHalf(2, 5, 0) + 1));
    was = r16(gMask + clutHalf(2, 5, 0));
    CHECK((was & 0x8000u) != 0);
    CHECK(r16(gNew + clutHalf(2, 5, 0)) == expect555(0xC86432, was)); /* the STP bit stays */
    CHECK(memcmp(gMask + gTimAt[0], gNew + gTimAt[0], gTimAt[1] - gTimAt[0]) == 0); /* the first TIM, whose CLUT the second one covers, is untouched */
    /* colour 6 (no STP on the disc) and colour 7 turned opaque black (a colour that is 0x0000 stays the transparent one: black is 0x8000) */
    layers(1);
    editPiece("mods/m1", F_8BIT, -1, 0, 0, 6, 0x102030);
    start();
    CHECK(build(MASK_PATH) == 1 && diffBytes(&at) >= 1);
    CHECK(r16(gNew + clutHalf(2, 6, 0)) == expect555(0x102030, r16(gMask + clutHalf(2, 6, 0))));
    CHECK((r16(gMask + clutHalf(2, 6, 0)) & 0x8000u) == 0 && (r16(gNew + clutHalf(2, 6, 0)) & 0x8000u) == 0);
    /* a 4-bit palette goes into the pages: colour 3 of the piece's palette (column 19 of page 1, row 5; the disc has STP there) */
    layers(1);
    editPiece("mods/m1", F_4BIT, -1, 0, 0, 3, 0x0A0B0C);
    start();
    CHECK(build(MASK_PATH) == 1 && diffBytes(&at) == 2);
    was = r16(gMask + imageHalf(1, 16 + 3, 5));
    CHECK((was & 0x8000u) != 0 && r16(gNew + imageHalf(1, 16 + 3, 5)) == expect555(0x0A0B0C, was));
}

static void testWrongSize(void) {
    PngImage p;
    const Out *o = pieceOut(F_4BIT);
    MenuPackStats ms;

    layers(1);
    CHECK(o != NULL && pngReadIndexed(o->data, o->len, &p, NULL, 0) == 0);
    p.w--; /* 15 x 8 where the piece is 16 x 8: the indices no longer fill it */
    putPng("mods/m1", F_4BIT, &p);
    pngImageFree(&p);
    start();
    menuPackTotals(&ms);
    {
        int before = ms.rejected;

        CHECK(build(MASK_PATH) == 0 && identical());
        menuPackTotals(&ms);
        CHECK(ms.rejected == before + 1);
    }
}

static void testLayerOrder(void) {
    MenuPackStats ms;
    int before, was;
    unsigned now;

    /* two pieces that share a rectangle (the same part with two CLUT rows) are edited in two mods, differently: the later mod (m2) wins, and it is one conflict */
    layers(2);
    was = (int)((r16(gMask + imageHalf(1, 0, 2)) >> 12) & 15);
    editPiece("mods/m1", F_4BIT, 3, 2, (was + 1) & 15, -1, 0);
    editPiece("mods/m2", F_4BIT_ROW1, 3, 2, (was + 2) & 15, -1, 0);
    start();
    menuPackTotals(&ms);
    before = ms.conflicts;
    CHECK(build(MASK_PATH) == 1);
    now = r16(gNew + imageHalf(1, 0, 2));
    CHECK((int)((now >> 12) & 15) == ((was + 2) & 15));
    menuPackTotals(&ms);
    CHECK(ms.conflicts == before + 1);
    /* the same two edits the other way round (m1 edits the row 1 piece, m2 the first): still the later mod's */
    layers(2);
    editPiece("mods/m1", F_4BIT_ROW1, 3, 2, (was + 1) & 15, -1, 0);
    editPiece("mods/m2", F_4BIT, 3, 2, (was + 2) & 15, -1, 0);
    start();
    CHECK(build(MASK_PATH) == 1);
    CHECK((int)((r16(gNew + imageHalf(1, 0, 2)) >> 12) & 15) == ((was + 2) & 15));
}

/* a stage nobody edits is not built: the hook says no before it plans anything, whatever the other stage has */
static void testUntouchedStage(void) {
    StageObjPackStats st;
    MenuPackStats ms;
    int built, screens;

    layers(1);
    editPiece("mods/m1", F_4BIT, 3, 2, 9, -1, 0);
    start();
    stageObjPackTotals(&st);
    CHECK(st.edited == 1);
    menuPackTotals(&ms);
    screens = ms.screens;
    CHECK(build(MASK_PATH) == 1);
    stageObjPackTotals(&st);
    menuPackTotals(&ms);
    built = st.built;
    CHECK(built == 1 && ms.screens == screens + 1);
    screens = ms.screens;
    CHECK(build(MASK_OTHER) == 0 && identical()); /* S203 has no edited piece */
    stageObjPackTotals(&st);
    menuPackTotals(&ms);
    CHECK(st.built == built && ms.screens == screens);
    /* the hooks are what says no: begin for an unedited stage returns 0 and reads nothing */
    CHECK(imgGetStageHooks()->begin(MASK_OTHER, gMask, gMaskLen, gTmpk, gTmpkLen) == 0);
    stageObjPackTotals(&st);
    CHECK(st.built == built);
    /* without the hooks nothing is applied at all */
    stageObjPackReset();
    CHECK(build(MASK_PATH) == 0 && identical());
}

/* the data folder's own files: the exporter's count for nothing; one the user edited in place counts, even right after the folder was found clean */
static void testDataFolder(void) {
    StageObjPackStats st;
    PngImage p;
    const Out *o = pieceOut(F_4BIT);
    char path[900];
    int was;

    layers(0);
    start();
    stageObjPackTotals(&st);
    CHECK(st.edited == 0);
    snprintf(path, sizeof path, "%s/data/stages/objects.stamp", gRoot);
    CHECK(access(path, F_OK) == 0); /* found clean: remembered, so the next start only compares sizes and times */
    start();
    stageObjPackTotals(&st);
    CHECK(st.edited == 0 && build(MASK_PATH) == 0 && identical());
    was = (int)((r16(gMask + imageHalf(1, 0, 2)) >> 12) & 15);
    CHECK(o != NULL && pngReadIndexed(o->data, o->len, &p, NULL, 0) == 0);
    p.px[2 * p.w + 3] = (uint8_t)((was + 3) & 15);
    putPng("data", F_4BIT, &p);
    pngImageFree(&p);
    start();
    stageObjPackTotals(&st);
    CHECK(st.edited == 1 && build(MASK_PATH) == 1);
    CHECK((int)((r16(gNew + imageHalf(1, 0, 2)) >> 12) & 15) == ((was + 3) & 15) && diffBytes(NULL) <= 2);
}

/* --validate-mod: the plan of a stage reports a PNG of the wrong size, a file no piece is called and a piece two mods ship (the menus' validator, menu_pack.h), and
   stageObjPackValidate reports the folders of stages/ that are not read: not s<nnn>, a stage the disc does not have, something besides objects/. The log is the
   test's stderr, taken to a file. */
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

static int discHasStage(const char *discPath) { return strcmp(discPath, MASK_PATH) == 0 || strcmp(discPath, MASK_OTHER) == 0; }

static void testValidate(void) {
    PngImage p, wide;
    const Out *o = pieceOut(F_8BIT);
    ImgOverrides ov0;
    char log[8192];

    layers(2);
    editPiece("mods/m1", F_4BIT, 3, 2, 9, -1, 0); /* a good piece, in both mods: the later one wins */
    editPiece("mods/m2", F_4BIT, 4, 2, 8, -1, 0);
    CHECK(o != NULL && pngReadIndexed(o->data, o->len, &p, NULL, 0) == 0);
    wide = p;
    wide.w = p.w + 1; /* the piece is 20 wide */
    wide.px = (uint8_t *)calloc((size_t)wide.w * (size_t)wide.h, 1);
    putPng("mods/m1", F_8BIT, &wide);
    free(wide.px);
    putPng("mods/m1", "f999p99.png", &p); /* no piece of the sheet is called that */
    pngImageFree(&p);
    putBytes("mods/m1/stages/s999/objects/f000p00.png", o->data, o->len);  /* the disc has no stage 999 */
    putBytes("mods/m1/stages/junk/objects/f000p00.png", o->data, o->len);  /* not a stage folder */
    putBytes("mods/m1/stages/s203/pieces/f000p00.png", o->data, o->len);   /* only objects/ is read */
    putBytes("mods/m1/stages/s202/stage.json", (const uint8_t *)"{}", 2);  /* a stage patch is read by another loader: not reported */
    putBytes("mods/m1/stages/s202/tiles/00_00.png", o->data, o->len);      /* the tiles are checked by stage_mod: not reported here */
    start();
    menuPackValidating(0);
    logStart();
    CHECK(build(MASK_PATH) == 1);
    logStop(log, sizeof log);
    CHECK(strstr(log, "f000p01.png rejected: size 21x6, the piece is 20x6") != NULL);
    CHECK(strstr(log, "ignored") == NULL); /* the reports of the validator are off at play time */
    menuPackValidating(1);
    start();
    logStart();
    CHECK(build(MASK_PATH) == 1);
    stageObjPackValidate(discHasStage);
    logStop(log, sizeof log);
    menuPackValidating(0);
    CHECK(strstr(log, "mods/m1/stages/s202/objects/f000p01.png rejected: size 21x6, the piece is 20x6") != NULL);
    CHECK(strstr(log, "mods/m1/stages/s202/objects/f999p99.png ignored: no piece of s202 is called that") != NULL);
    CHECK(strstr(log, "mods/m1/stages/s202/objects/f000p00.png ignored: the piece is also in mods/m2, which loads later and wins") != NULL);
    CHECK(strstr(log, "mods/m2/stages/s202/objects/f000p00.png ignored") == NULL); /* the winner is not reported */
    CHECK(strstr(log, "mods/m1/stages/junk") == NULL); /* not s<nnn>: a new stage's folder, the new-stage loader's business, no line here */
    CHECK(strstr(log, "mods/m1/stages/s999 ignored: the disc has no stage 999") != NULL);
    CHECK(strstr(log, "mods/m1/stages/s203/pieces ignored: only objects/, tiles/ and stage.json of a stage folder are read") != NULL);
    CHECK(strstr(log, "s202/stage.json") == NULL && strstr(log, "s202/tiles") == NULL); /* what the other loaders read is not reported here */

    /* a stage whose sheet has parts and no frames (S376 and ten more) has no pieces: what a mod puts in its objects/ is reported as no piece's */
    layers(1);
    putBytes("mods/m1/stages/s202/objects/f000p00.png", o->data, o->len);
    {
        uint8_t tmpk[128], sheet[32];
        size_t at = 4 * ((size_t)SHEET_ENTRY + 1);

        memset(sheet, 0, sizeof sheet);
        w32(sheet, 8);
        w32(sheet + 4, 8 + 14); /* the header of two words (the parts, the ids), one part, no frames */
        w32(tmpk, (uint32_t)at);
        memset(tmpk + at, 0, 8);
        at += 8;
        w32(tmpk + 4 * SHEET_ENTRY, (uint32_t)at);
        memcpy(tmpk + at, sheet, 8 + 14 + 4);
        start();
        menuPackValidating(1);
        logStart();
        CHECK(imgOverridesBuild(&ov0, MASK_PATH, gMask, gMaskLen, tmpk, at + 8 + 14 + 4) == 0);
        logStop(log, sizeof log);
        menuPackValidating(0);
        imgOverridesFree(&ov0);
    }
    CHECK(strstr(log, "mods/m1/stages/s202/objects/f000p00.png ignored: no piece of s202 is called that") != NULL);
}

int main(void) {
    char tmp[] = "/tmp/stageobjpacktestXXXXXX";
    MenuStats ms;
    const char *why = NULL;

    if (mkdtemp(tmp) == NULL) {
        fprintf(stdout, "stageobjpacktest: no scratch folder\n");
        return 1;
    }
    snprintf(gRoot, sizeof gRoot, "%s", tmp);
    stageFill();
    gMaskLen = makeMask(gMask);
    gTmpkLen = makeTmpk(gTmpk);
    memset(&ms, 0, sizeof ms);
    CHECK(stageObjExport(STAGE, MASK_PATH, TMPK_PATH, gMask, gMaskLen, gTmpk, gTmpkLen, SHEET_ENTRY, sinkEmit, NULL, &ms, &why) == 3 && why == NULL);
    CHECK(pieceOut(F_4BIT) != NULL && pieceOut(F_8BIT) != NULL && pieceOut(F_4BIT_ROW1) != NULL);

    testNothing();
    testPixel();
    testColour();
    testWrongSize();
    testLayerOrder();
    testUntouchedStage();
    testDataFolder();
    testValidate();

    {
        char cmd[700];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp);
        if (system(cmd)) {
        }
    }
    if (failures) {
        fprintf(stdout, "stageobjpacktest: %d FAILED\n", failures);
        return 1;
    }
    fprintf(stdout, "stageobjpacktest: all checks passed\n");
    return 0;
}
