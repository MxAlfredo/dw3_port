#define _GNU_SOURCE 1
/*
 * Host test of importing the partner bundle (readable-assets A4a): pc/shim/partner_bundle.c, the colour reducer of partner_art.c and the image layer's partner
 * hooks. Synthetic sheets, page images, PNGs and mods in a scratch folder under /tmp (made-up bytes, no retail data, no disc).
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/exp_core.h"
#include "shim/image_data.h"
#include "shim/mods.h"
#include "shim/partner_art.h"
#include "shim/partner_bundle.h"
#include "shim/partner_export.h"
#include "shim/png_read.h"
#include "shim/sprite_data.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "partnerbundletest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- builders (the same shapes as partnerexptest) ---- */

typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
} Buf;

static void put(Buf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->p = (uint8_t *)realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void u8(Buf *b, unsigned v) { uint8_t c = (uint8_t)v; put(b, &c, 1); }
static void u16(Buf *b, unsigned v) { u8(b, v & 255); u8(b, v >> 8); }
static void u32(Buf *b, uint32_t v) { u16(b, v & 0xFFFF); u16(b, v >> 16); }
static void at32(Buf *b, size_t at, uint32_t v) { b->p[at] = (uint8_t)v; b->p[at + 1] = (uint8_t)(v >> 8); b->p[at + 2] = (uint8_t)(v >> 16); b->p[at + 3] = (uint8_t)(v >> 24); }
static void pad4(Buf *b) { while (b->n & 3) u8(b, 0); }

#define PAGE_W 64 /* halfwords: 256 texels */
#define PAGE_H 256

/* a raw 16-bit page TIM (mode 2, no CLUT) of PAGE_W x PAGE_H halfwords filled with `fill` */
static void pageBuild(Buf *b, uint16_t fill) {
    int i;

    u32(b, 0x10);
    u32(b, 2);
    u32(b, 12 + 2 * (uint32_t)(PAGE_W * PAGE_H));
    u16(b, 0); u16(b, 0); u16(b, PAGE_W); u16(b, PAGE_H);
    for (i = 0; i < PAGE_W * PAGE_H; i++) u16(b, fill);
}

typedef struct SynFrame {
    int id, u, v, w, h, cx, cy, x, y;
} SynFrame;

static void sheetBuild(Buf *b, const SynFrame *f, int n) {
    size_t base = b->n, po, io, i;

    u32(b, 0); u32(b, 0);
    for (i = 0; i < (size_t)n; i++) u32(b, 0);
    po = b->n - base;
    for (i = 0; i < (size_t)n; i++) {
        u16(b, (unsigned)f[i].u); u16(b, (unsigned)f[i].v); u16(b, (unsigned)f[i].w); u16(b, (unsigned)f[i].h);
        u16(b, (unsigned)f[i].cx); u16(b, (unsigned)f[i].cy); u16(b, 0);
    }
    io = b->n - base;
    for (i = 0; i < (size_t)n; i++) u8(b, (unsigned)f[i].id);
    u8(b, 255);
    pad4(b);
    at32(b, base, (uint32_t)po);
    at32(b, base + 4, (uint32_t)io);
    for (i = 0; i < (size_t)n; i++) {
        at32(b, base + 8 + 4 * i, (uint32_t)(b->n - base));
        u16(b, 1); u16(b, 0); u16(b, 0xFFFF);
        u16(b, (unsigned)i); u16(b, (unsigned)f[i].x); u16(b, (unsigned)f[i].y);
    }
}

/* a sheet file whose entry `entry` is the sheet: a table of 4 offsets from the start of the file */
static void sheetFile(Buf *b, int entry, const SynFrame *f, int n) {
    Buf s = {0};
    int i;

    for (i = 0; i < 4; i++) u32(b, 0);
    for (i = 0; i < 4; i++) {
        at32(b, (size_t)(4 * i), (uint32_t)b->n);
        if (i == entry) {
            s.n = 0;
            sheetBuild(&s, f, n);
            put(b, s.p, s.n);
        } else {
            u32(b, 0xDEADBEEF); /* not a sheet: another entry of the file */
        }
    }
    free(s.p);
}

/* the layout of partner 3 (peLayout): portrait frames 3..6, icon frame 41. Cells 0 and 1 in page 0, cells 2 and 3 in page 1; the palette in page 0 */
static const SynFrame kFrames[] = {
    {3, 0, 0, 32, 32, 0, 200, 0, 0},   {4, 32, 0, 32, 32, 0, 200, 0, 0},   {5, 256 + 0, 40, 32, 32, 0, 200, 0, 0}, {6, 256 + 32, 40, 32, 32, 0, 200, 0, 0},
    {41, 64, 100, 44, 28, 16, 201, 0, 0},
};

static int texel(const uint8_t *tim, int x, int y) {
    const uint8_t *hw = tim + 20 + 2 * ((size_t)y * PAGE_W + (size_t)(x / 4));

    return (((unsigned)hw[0] | (unsigned)hw[1] << 8) >> ((x & 3) * 4)) & 15;
}

static unsigned halfword(const uint8_t *tim, int x, int y) {
    const uint8_t *hw = tim + 20 + 2 * ((size_t)y * PAGE_W + (size_t)x);

    return (unsigned)hw[0] | (unsigned)hw[1] << 8;
}

/* an indexed image as a PngImage: every cell/pixel from fn */
static void pngMake(PngImage *p, int w, int h, int nPal, int (*fn)(int x, int y)) {
    int x, y, i;

    memset(p, 0, sizeof *p);
    p->w = w;
    p->h = h;
    p->depth = 8;
    p->nPal = nPal;
    for (i = 0; i < nPal; i++) {
        p->rgb[3 * i] = (uint8_t)(i * 255 / (nPal > 1 ? nPal - 1 : 1));
        p->rgb[3 * i + 1] = (uint8_t)(255 - i * 255 / (nPal > 1 ? nPal - 1 : 1));
        p->rgb[3 * i + 2] = (uint8_t)((i * 37) & 255);
    }
    p->nAlpha = 1;
    p->alpha[0] = 0;
    p->px = (uint8_t *)malloc((size_t)w * (size_t)h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) p->px[y * w + x] = (uint8_t)fn(x, y);
    }
}

static int cellColour(int x, int y) { (void)y; return 1 + x / 32; } /* cell c is all colour c + 1 */
static int iconColour(int x, int y) { return (x + y) % 5 == 0 ? 0 : 7; }
static int manyColours(int x, int y) { return (x / 2 + y / 2) % 40; }

/* ---- the colour reducer ---- */

static void testReduce(void) {
    PngImage in, out;
    PaArt art;
    int i, used[256] = {0};
    size_t px;

    pngMake(&in, 32, 32, 40, manyColours);
    CHECK(in.nPal == 40);
    CHECK(paFromIndexed(&in, &art, NULL, 0) == -1); /* too many for a 4-bit palette */
    CHECK(paReduce(&in, &out, 16) == 0);
    CHECK(out.nPal <= 16 && out.nPal >= 2 && out.w == 32 && out.h == 32);
    CHECK(out.nAlpha == 1 && out.alpha[0] == 0); /* transparency stays index 0 */
    for (px = 0; px < 32 * 32; px++) {
        used[out.px[px]]++;
        if (out.px[px] >= out.nPal) failures++;
    }
    /* pixels that were colour 0 (transparent) are index 0, and only those */
    for (px = 0; px < 32 * 32; px++) CHECK((in.px[px] == 0) == (out.px[px] == 0));
    CHECK(paFromIndexed(&out, &art, NULL, 0) == 0); /* the reduced image is accepted */
    paFree(&art);
    {
        int cnt = 0;

        for (i = 0; i < 256; i++) cnt += used[i] > 0;
        CHECK(cnt == out.nPal); /* no index is wasted */
    }
    pngImageFree(&out);
    /* the closest colours merge first: three colours, two near, one far, budget 2 (with transparency, 1 opaque slot... use a fresh image without transparency) */
    {
        PngImage a;
        int x;

        memset(&a, 0, sizeof a);
        a.w = 4; a.h = 1; a.depth = 8; a.nPal = 3;
        a.rgb[0] = 250; a.rgb[1] = 0; a.rgb[2] = 0;
        a.rgb[3] = 255; a.rgb[4] = 5; a.rgb[5] = 0;
        a.rgb[6] = 0; a.rgb[7] = 0; a.rgb[8] = 255;
        a.px = (uint8_t *)malloc(4);
        for (x = 0; x < 4; x++) a.px[x] = (uint8_t)(x == 3 ? 2 : x % 2);
        CHECK(paReduce(&a, &out, 2) == 0);
        CHECK(out.nPal == 2 && out.px[0] == out.px[1] && out.px[0] == out.px[2] && out.px[3] != out.px[0] && out.nAlpha == 0);
        pngImageFree(&out);
        pngImageFree(&a);
    }
    /* an image already within the limit keeps its colours */
    pngMake(&in, 128, 32, 6, cellColour);
    CHECK(paReduce(&in, &out, 16) == 0 && out.nPal == 4 && out.nAlpha == 0 && out.px[0] == out.px[1] && out.px[0] != out.px[32] && out.px[32] != out.px[64]);
    pngImageFree(&out);
    pngImageFree(&in);
}

/* ---- one page image ---- */

static void testRepackPage(void) {
    PngImage cells, icon;
    PaArt portrait, ic, three;
    Buf sheet = {0}, page0 = {0}, page1 = {0};
    PbArt art[8];
    int n0, n1, x, y, c, i, bad = 0;

    pngMake(&cells, 128, 32, 6, cellColour);
    pngMake(&icon, 44, 28, 8, iconColour);
    CHECK(paFromIndexed(&cells, &portrait, NULL, 0) == 0 && portrait.cells == 4);
    CHECK(paIconFromIndexed(&icon, &ic, NULL, 0) == 0);
    pngMake(&cells, 96, 32, 6, cellColour);
    CHECK(paFromIndexed(&cells, &three, NULL, 0) == 0 && three.cells == 3);
    sheetBuild(&sheet, kFrames, 5);
    pageBuild(&page0, 0xAAAA);
    pageBuild(&page1, 0xAAAA);
    memset(art, 0, sizeof art);
    art[3].portrait = &portrait;
    art[3].icon = &ic;

    n0 = pbRepackPage(page0.p, page0.n, 0, sheet.p, sheet.n, art);
    n1 = pbRepackPage(page1.p, page1.n, 1, sheet.p, sheet.n, art);
    CHECK(n0 > 0 && n1 > 0);
    /* page 0: cells 0 and 1 and the icon, the palette in the row the frames name (row 200 of page 0: 16 halfwords from column 0), the icon's at (16, 201) */
    for (c = 0; c < 2; c++) {
        for (y = 0; y < 32; y++) {
            for (x = 0; x < 32; x++) CHECK(texel(page0.p, c * 32 + x, y) == c + 1);
        }
    }
    CHECK(texel(page0.p, 64, 100) == 0 && texel(page0.p, 65, 100) == 7); /* the icon's first pixels: (0,0) is transparent, (1,0) is colour 7 */
    for (y = 0; y < 28; y++) {
        for (x = 0; x < 44; x++) {
            if (texel(page0.p, 64 + x, 100 + y) != ((x + y) % 5 == 0 ? 0 : 7)) bad++;
        }
    }
    CHECK(bad == 0);
    for (i = 0; i < 16; i++) {
        CHECK(halfword(page0.p, i, 200) == portrait.pal[i]);
        CHECK(halfword(page0.p, 16 + i, 201) == ic.pal[i]);
    }
    CHECK(portrait.pal[1] != 0 && portrait.pal[2] != portrait.pal[1]); /* a real palette, not zeros */
    /* around them nothing changed */
    CHECK(texel(page0.p, 96, 0) == 10 && texel(page0.p, 63, 33) == 10 && texel(page0.p, 0, 32) == 10 && halfword(page0.p, 0, 199) == 0xAAAA &&
          halfword(page0.p, 16, 200) == 0xAAAA);
    /* page 1: cells 2 and 3 at u 256 and 288 -> texels 0..31 and 32..63 of page 1, rows 40..71; no palette there */
    for (c = 0; c < 2; c++) {
        for (y = 0; y < 32; y++) {
            for (x = 0; x < 32; x++) CHECK(texel(page1.p, c * 32 + x, 40 + y) == c + 3);
        }
    }
    CHECK(halfword(page1.p, 0, 200) == 0xAAAA && texel(page1.p, 0, 0) == 10 && texel(page1.p, 70, 40) == 10);
    /* a portrait with another number of cells than the original's, or a page that is not 16-bit, or another page, writes nothing */
    art[3].portrait = &three;
    art[3].icon = NULL;
    {
        Buf p = {0};

        pageBuild(&p, 0xAAAA);
        CHECK(pbRepackPage(p.p, p.n, 0, sheet.p, sheet.n, art) == 0 && halfword(p.p, 0, 200) == 0xAAAA && texel(p.p, 0, 0) == 10);
        art[3].portrait = &portrait;
        CHECK(pbRepackPage(p.p, p.n, 2, sheet.p, sheet.n, art) == 0 && texel(p.p, 0, 0) == 10);
        p.p[4] = 0; /* mode 0, not a raw page */
        CHECK(pbRepackPage(p.p, p.n, 0, sheet.p, sheet.n, art) == 0);
        free(p.p);
    }
    memset(art, 0, sizeof art);
    CHECK(pbRepackPage(page0.p, page0.n, 0, sheet.p, sheet.n, art) == 0);
    pngImageFree(&cells);
    pngImageFree(&icon);
    paFree(&portrait);
    paFree(&ic);
    paFree(&three);
    free(sheet.p);
    free(page0.p);
    free(page1.p);
}

/* ---- the follower sprites of the originals ---- */

/* a sprite archive of one 4-bit frame (a 4x4 image, its own 16-colour CLUT) and a last entry that is not a set table: entry 0 is the frame */
static uint8_t *spriteFile(size_t *len) {
    Buf f = {0}, t = {0};
    int i;

    u32(&t, 0x10);
    u32(&t, 8); /* 4-bit with a CLUT */
    u32(&t, 12 + 32);
    u16(&t, 0); u16(&t, 480); u16(&t, 16); u16(&t, 1);
    for (i = 0; i < 16; i++) u16(&t, i ? 0x0300 + i : 0);
    u32(&t, 12 + 8);
    u16(&t, 0); u16(&t, 0); u16(&t, 1); u16(&t, 4);
    for (i = 0; i < 4; i++) u16(&t, 0x4321);
    u32(&f, 12);
    u32(&f, (uint32_t)(12 + t.n));
    u32(&f, 0);
    put(&f, t.p, t.n);
    u32(&f, 0);
    u32(&f, 0);
    free(t.p);
    *len = f.n;
    return f.p;
}

static void testNames(void) {
    CHECK(sprPartnerOf("AAA/DAT/FIELD/SPRT/P003PKEN.BIN") == 0 && sprPartnerOf("AAA/DAT/FIELD/SPRT/P010PATA.BIN") == 7);
    CHECK(sprPartnerOf("sprites/p006agum/004.png") == 3 && sprPartnerOf("sprites/p006agum") == 3);
    CHECK(sprPartnerOf("sprites/p001play/004.png") == -1 && sprPartnerOf("sprites/p0060agum/004.png") == -1 && sprPartnerOf("screen/namedttm/00_00.png") == -1 &&
          sprPartnerOf(NULL) == -1);
    CHECK(!strcmp(sprPartnerFile(0), "P003PKEN") && !strcmp(sprPartnerFile(7), "P010PATA") && sprPartnerFile(8) == NULL);
    CHECK(!strcmp(peLayout(3)->fieldFile, sprPartnerFile(3)) && !strcmp(peLayout(7)->fieldFile, "P010PATA"));
}

/* ---- the layers ---- */

static char gRoot[600];
static int kChain00[2] = {0, 0};

static void mk(const char *rel) {
    char cmd[1000];

    snprintf(cmd, sizeof cmd, "mkdir -p '%s/%s'", gRoot, rel);
    if (system(cmd)) {}
}

static void putBytes(const char *rel, const uint8_t *d, size_t n) {
    char path[1000], dir[600], *slash;
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    *slash = 0;
    snprintf(path, sizeof path, "mkdir -p '%s'", dir);
    if (system(path)) {}
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    f = fopen(path, "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

/* a PNG of cells (cw*n x ch) in colour `base + cell` (a palette of `nPal` colours) with one pixel set to `dot` when dot >= 0 */
static int gDot, gBase;
static int dotColour(int x, int y) { return x == 0 && y == 0 && gDot >= 0 ? gDot : gBase + x / 32; }
static int spreadColour(int x, int y) { return 1 + (x + y) % (gBase - 1); } /* colours 1 .. gBase - 1 (gBase is the palette size here) */

static void pngFile(const char *rel, int w, int h, int nPal, int base, int dot) {
    ImgPic pic;
    uint8_t *png;
    size_t len;
    PngImage p;
    int i;

    gDot = dot;
    gBase = base < 0 ? nPal : base;
    pngMake(&p, w, h, nPal, base < 0 ? spreadColour : dotColour);
    memset(&pic, 0, sizeof pic);
    pic.w = w;
    pic.h = h;
    pic.depth = nPal > 16 ? 8 : 4;
    pic.nColors = nPal;
    pic.px = p.px;
    for (i = 0; i < nPal; i++) {
        pic.rgb[3 * i] = p.rgb[3 * i];
        pic.rgb[3 * i + 1] = p.rgb[3 * i + 1];
        pic.rgb[3 * i + 2] = p.rgb[3 * i + 2];
        pic.alpha[i] = i == 0 ? 0 : 255;
    }
    if (imgPicToPng(&pic, &png, &len) == 0) {
        putBytes(rel, png, len);
        free(png);
    }
    free(p.px);
}

/* partners/manifest.json as the exporter writes it (exp_core), with the hash of each named file as it is now */
static void manifestOf(const char *a, const char *b, const char *c) {
    ExpManifest man;
    const char *rels[3];
    char path[900];
    int i;

    rels[0] = a;
    rels[1] = b;
    rels[2] = c;
    snprintf(path, sizeof path, "%s/data/partners/manifest.json", gRoot);
    expManOpen(&man, "test", path, "partners");
    for (i = 0; i < 3; i++) {
        uint8_t *bytes;
        size_t len;
        char file[900], hex[17];

        snprintf(file, sizeof file, "%s/data/partners/%s", gRoot, rels[i]);
        if (tdReadFile(file, &bytes, &len) != 0) continue;
        tdHashHex(tdHash(bytes, len), hex);
        free(bytes);
        expManSet(&man, rels[i], hex);
    }
    expManSave(&man, 0);
    expManFree(&man);
}

static void modJson(const char *mod) {
    char rel[200], text[200];

    snprintf(rel, sizeof rel, "mods/%s/mod.json", mod);
    snprintf(text, sizeof text, "{\"format\":\"mod-v1\",\"name\":\"%s\",\"version\":\"1.0\"}", mod);
    putBytes(rel, (const uint8_t *)text, strlen(text));
}

/* the seven screens' page files and one synthetic sheet file each (entry 3 for the gym, 0 for the others) */
static const char *const kPages[7] = {"AAA/DAT/SCREEN/NAMEDTTM.BIN", "AAA/DAT/NAMEENT/STDGNMTM.BIN", "AAA/DAT/DGLB/STDGLBTM.BIN", "AAA/DAT/SCREEN/STSTATTM.BIN",
                                      "AAA/DAT/TRAINING/STTRNGTM.BIN", "AAA/DAT/SCREEN/STFRPTTM.BIN", "AAA/DAT/MCARD/STMCRDTM.BIN"};

/* what the hooks do to the page images of one screen: 0 when the file is not wanted, else how many pages changed (page images 0 and 1 of the file) */
static int runScreen(int s, Buf pages[2], int *palettes) {
    const ImgPartnerHooks *h = imgGetPartnerHooks();
    Buf sheet = {0};
    int chain[2] = {0, 0}, changed = 0, k;

    if (h == NULL || h->begin == NULL || !h->begin(kPages[s])) return 0;
    sheetFile(&sheet, s == 4 ? 3 : 0, kFrames, 5);
    for (k = 0; k < 2; k++) {
        chain[1] = k;
        if (h->patch(kPages[s], chain, 2, pages[k].p, pages[k].n, sheet.p, sheet.n)) changed++;
    }
    *palettes = (int)halfword(pages[0].p, 1, 200);
    free(sheet.p);
    return changed;
}

static void testLayers(void) {
    char root[700], data[700];
    char keys[8][64];
    const ImgPartnerHooks *h;
    Buf pages[7][2];
    int s, i, n, pal = 0, ok = 1;
    AssetLayer layer;
    char path[1500];

    memset(keys, 0, sizeof keys);
    memset(pages, 0, sizeof pages);
    for (i = 0; i < 8; i++) snprintf(keys[i], sizeof keys[i], "name%d", i);
    snprintf(keys[3], sizeof keys[3], "agumon");
    snprintf(data, sizeof data, "%s/data", gRoot);
    snprintf(root, sizeof root, "%s/mods", gRoot);
    mk("data/partners/agumon");
    mk("mods");
    pbReset();
    pbSetKeys(keys);
    assetSetData(data);
    modsInit(root, NULL);

    /* the exporter's files, untouched: the manifest knows their hash, nothing counts as edited */
    pngFile("data/partners/agumon/portrait.png", 128, 32, 6, 1, -1);
    pngFile("data/partners/agumon/icon.png", 44, 28, 4, 1, -1);
    putBytes("data/partners/agumon/field/004.png", (const uint8_t *)"frame", 5);
    manifestOf("agumon/portrait.png", "agumon/icon.png", "agumon/field/004.png");
    n = pbStart();
    h = imgGetPartnerHooks();
    CHECK(n == 0 && h != NULL && h->begin != NULL && h->patch != NULL && h->fieldFile != NULL);
    CHECK(!h->begin(kPages[0])); /* nothing edited: the pages are left alone */
    putBytes("data/partners/agumon/partner.json", (const uint8_t *)"{}", 2); /* the exporter's information file: not the manifest's, and never read */
    CHECK(!pbDataFolderEdited("agumon") && !pbDataFolderEdited("nobody"));
    CHECK(assetLayerAt(assetLayerCount() - 1, &layer) && layer.mod < 0);
    CHECK(!h->fieldFile(3, &layer, "004.png", path, sizeof path)); /* an unedited export is not a replacement */
    CHECK(!h->fieldFile(2, &layer, "004.png", path, sizeof path)); /* another partner has no such file */
    CHECK(pbOriginalKey(3) != NULL && !strcmp(pbOriginalKey(3), "agumon") && pbOriginalKey(8) == NULL);

    /* the user changes one pixel of the portrait (and the field frame): both count, the untouched icon does not */
    pngFile("data/partners/agumon/portrait.png", 128, 32, 6, 1, 3);
    putBytes("data/partners/agumon/field/004.png", (const uint8_t *)"frame*", 6);
    n = pbStart();
    CHECK(n == 1);
    CHECK(pbDataFolderEdited("agumon")); /* a changed file anywhere in the folder: the folder is a layer of the art */
    CHECK(h->fieldFile(3, &layer, "004.png", path, sizeof path) && strstr(path, "agumon/field/004.png") != NULL);
    for (s = 0; s < 7; s++) {
        pageBuild(&pages[s][0], 0xAAAA);
        pageBuild(&pages[s][1], 0xAAAA);
        CHECK(runScreen(s, pages[s], &pal) == 2); /* both page images of every one of the seven screens changed */
        CHECK(texel(pages[s][0].p, 0, 0) == 3);    /* the edited pixel is in the first cell */
        CHECK(texel(pages[s][0].p, 5, 5) == 1 && texel(pages[s][0].p, 33, 5) == 2 && texel(pages[s][1].p, 5, 45) == 3 && texel(pages[s][1].p, 37, 45) == 4);
        CHECK(texel(pages[s][0].p, 64, 100) == 10 && halfword(pages[s][0].p, 16, 201) == 0xAAAA); /* the icon was not edited: untouched */
        for (i = 0; i < 16; i++) {
            if (halfword(pages[s][0].p, i, 200) != halfword(pages[0][0].p, i, 200)) ok = 0;
        }
    }
    CHECK(ok && halfword(pages[0][0].p, 1, 200) != 0xAAAA && halfword(pages[0][0].p, 1, 200) != 0);  /* the same palette rows in all seven sheets */
    CHECK(!h->begin("AAA/DAT/SCREEN/STSHOPTM.BIN") && !h->patch(kPages[0], kChain00, 2, pages[0][0].p, pages[0][0].n, NULL, 0)); /* another screen file: ignored */
    for (s = 0; s < 7; s++) {
        free(pages[s][0].p);
        free(pages[s][1].p);
    }

    /* a mod's file counts as it is, the last mod wins; a bad one (the wrong number of cells) is refused and the next layer is used */
    modJson("m1");
    modJson("m2");
    pngFile("mods/m1/partners/agumon/portrait.png", 128, 32, 6, 2, -1); /* cells in colours 2.. */
    pngFile("mods/m2/partners/agumon/portrait.png", 96, 32, 6, 1, -1);  /* 3 cells: refused (it has 4) */
    pngFile("mods/m2/partners/agumon/icon.png", 44, 28, 20, -1, -1);    /* 19 colours in use: reduced to 16 with a warning */
    modsInit(root, NULL);
    n = pbStart();
    CHECK(n == 1);
    {
        Buf p[2] = {{0}, {0}};
        int changed;

        pageBuild(&p[0], 0xAAAA);
        pageBuild(&p[1], 0xAAAA);
        changed = runScreen(6, p, &pal);
        CHECK(changed == 2);
        CHECK(texel(p[0].p, 0, 0) == 2 && texel(p[0].p, 33, 5) == 3); /* m1's cells (first colour 2); the data folder's dot is gone (m1 wins) */
        CHECK(texel(p[0].p, 64, 100) != 10);                          /* the icon of m2 went in: it had 20 colours and now 16 or fewer */
        for (i = 0; i < 16; i++) CHECK(halfword(p[0].p, 16 + i, 201) != 0xAAAA);
        free(p[0].p);
        free(p[1].p);
    }
    CHECK(assetLayerAt(0, &layer) && layer.mod >= 0);
    putBytes("mods/m2/partners/agumon/field/004.png", (const uint8_t *)"mod frame", 9);
    CHECK(h->fieldFile(3, &layer, "004.png", path, sizeof path) && strstr(path, "mods/m2/partners/agumon/field/004.png") != NULL);
    /* the sprite file of the original: its frame comes from partners/<name>/field/ and no longer from sprites/ */
    {
        size_t len;
        uint8_t *file = spriteFile(&len);
        ImgOverrides o;
        const ImgOverride *ov;
        ImgPic pic;
        uint8_t *png;
        size_t pl;

        memset(&pic, 0, sizeof pic);
        pic.w = 4;
        pic.h = 4;
        pic.depth = 4;
        pic.nColors = 2;
        pic.px = (uint8_t *)calloc(16, 1);
        pic.px[5] = 1;
        pic.rgb[3] = 200;
        pic.alpha[0] = 0;
        pic.alpha[1] = 255;
        CHECK(imgPicToPng(&pic, &png, &pl) == 0);
        putBytes("mods/m2/partners/agumon/field/000.png", png, pl);
        putBytes("mods/m1/sprites/p006agum/000.png", png, pl); /* a sprites/ folder of the follower: not read (partners/<name>/field/ holds it) */
        free(png);
        free(pic.px);
        CHECK(imgSpriteOverridesBuild(&o, "AAA/DAT/FIELD/SPRT/P006AGUM.BIN", file, len) == 1);
        ov = o.n > 0 ? imgOverrideGet(&o, o.v[0].off) : NULL;
        CHECK(ov != NULL && ov->tim != NULL); /* the bundle's frame went in */
        imgOverridesFree(&o);
        /* another sprite file keeps the old place, the follower of another partner has no bundle file for this frame */
        CHECK(imgSpriteOverridesBuild(&o, "AAA/DAT/FIELD/SPRT/P003PKEN.BIN", file, len) == 0);
        imgOverridesFree(&o);
        pbReset();
        CHECK(imgSpriteOverridesBuild(&o, "AAA/DAT/FIELD/SPRT/P006AGUM.BIN", file, len) == 0); /* no hook, no keys: neither place is read */
        imgOverridesFree(&o);
        free(file);
    }
    pbReset();
    CHECK(imgGetPartnerHooks()->begin == NULL && pbOriginalKey(3) == NULL);
}

int main(void) {
    char tmp[] = "/tmp/partnerbundletestXXXXXX";

    if (mkdtemp(tmp) == NULL) {
        fprintf(stdout, "partnerbundletest: no temp folder\n");
        return 1;
    }
    snprintf(gRoot, sizeof gRoot, "%s", tmp);
    testNames();
    testReduce();
    testRepackPage();
    testLayers();
    {
        char cmd[700];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
        if (system(cmd)) {}
    }
    if (failures) {
        fprintf(stdout, "partnerbundletest: %d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stdout, "partnerbundletest: OK\n");
    return 0;
}
