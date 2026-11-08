/*
 * Host test of the partner bundles (readable-assets A3): pc/shim/partner_export.c. Synthetic sheets, pages, training files and a synthetic raw disc
 * image in memory (the bytes are made up, no retail data); the scratch folder lives under $TMPDIR.
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/exp_core.h"
#include "shim/partner_export.h"
#include "shim/png_read.h"
#include "shim/text_codec.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "partnerexptest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- builders ---- */

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

/* a TIM: mode 2 raw page (no CLUT), or mode 0 with a CLUT of cw x ch halfwords; iw x ih halfwords of image */
static void timBuild(Buf *b, int mode, const uint16_t *clut, int cw, int ch, const uint16_t *img, int iw, int ih) {
    int i;

    u32(b, 0x10);
    u32(b, (uint32_t)mode | (clut ? 8u : 0u));
    if (clut) {
        u32(b, 12 + 2 * (uint32_t)(cw * ch));
        u16(b, 0); u16(b, 480); u16(b, (unsigned)cw); u16(b, (unsigned)ch);
        for (i = 0; i < cw * ch; i++) u16(b, clut[i]);
    }
    u32(b, 12 + 2 * (uint32_t)(iw * ih));
    u16(b, 0); u16(b, 0); u16(b, (unsigned)iw); u16(b, (unsigned)ih);
    for (i = 0; i < iw * ih; i++) u16(b, img[i]);
}

/* "RLEN", the unpacked size, runs (0x80|n byte: repeat, n bytes: literal), 0 */
static void rlenBuild(Buf *b, const uint8_t *d, size_t n) {
    size_t i = 0;

    put(b, "RLEN", 4);
    u32(b, (uint32_t)n);
    while (i < n) {
        size_t run = 1;

        while (i + run < n && d[i + run] == d[i] && run < 127) run++;
        if (run >= 3) {
            u8(b, 0x80 | (unsigned)run);
            u8(b, d[i]);
            i += run;
        } else {
            size_t lit = 0;

            while (i + lit < n && lit < 127 && !(i + lit + 2 < n && d[i + lit] == d[i + lit + 1] && d[i + lit] == d[i + lit + 2])) lit++;
            if (lit == 0) lit = 1;
            u8(b, (unsigned)lit);
            put(b, d + i, lit);
            i += lit;
        }
    }
    u8(b, 0);
}

/* ---- pure: names, sheets, cells, strips ---- */

typedef struct SynFrame {
    int id, u, v, w, h, cx, cy, x, y;
} SynFrame;

/* a sheet: offsets (parts, ids, first frame...), the frame table, the parts, the ids (255 ends them), then each frame's record */
static void sheetBuild(Buf *b, const SynFrame *f, int n) {
    size_t base = b->n, po, io, i;

    u32(b, 0); u32(b, 0);
    for (i = 0; i < (size_t)n; i++) u32(b, 0);
    po = b->n - base;
    for (i = 0; i < (size_t)n; i++) { /* one part per frame */
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
        u16(b, 1); u16(b, 0); u16(b, 0xFFFF); /* one part, row 0, opaque */
        u16(b, (unsigned)i); u16(b, (unsigned)f[i].x); u16(b, (unsigned)f[i].y);
    }
}

static void testSlug(void) {
    char s[64];

    CHECK(peSlug("Agumon", s, sizeof s) && !strcmp(s, "agumon"));
    CHECK(peSlug("  Metal  Greymon!! ", s, sizeof s) && !strcmp(s, "metal-greymon"));
    CHECK(!peSlug("!!!", s, sizeof s));
    CHECK(peSlug("a-b_c", s, sizeof s) && !strcmp(s, "a-b-c"));
    CHECK(peSlug("0123456789012345678901234567890123456789012345678901234567890", s, sizeof s) && strlen(s) == 48);
}

#define PAGE_W 64
#define PAGE_H 256

static uint16_t sPageA[PAGE_W * PAGE_H], sPageB[PAGE_W * PAGE_H];

static int texel(const uint16_t *page, int x, int y) { return (page[y * PAGE_W + x / 4] >> ((x & 3) * 4)) & 15; }
static void setTexel(uint16_t *page, int x, int y, int v) {
    uint16_t *h = &page[y * PAGE_W + x / 4];

    *h = (uint16_t)((*h & ~(15 << ((x & 3) * 4))) | (v << ((x & 3) * 4)));
}

static void testCellsAndStrip(void) {
    Buf sheet = {0};
    SynFrame fr[3] = {{7, 32, 64, 32, 32, 16, 244, 2, 2}, {8, 288, 64, 32, 32, 16, 244, 0, 3}, {40, 256, 128, 44, 28, 0, 250, 0, 0}};
    PeFrame f;
    ImgTim pageA, pageB;
    Buf ta = {0}, tb = {0};
    const ImgTim *pages[2];
    uint8_t px[44 * 28], px2[32 * 32];
    uint16_t pal[16], pal2[16];
    int x, y, i;
    const uint8_t *cells[2];
    ImgPic pic;

    memset(sPageA, 0, sizeof sPageA);
    memset(sPageB, 0, sizeof sPageB);
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            setTexel(sPageA, 32 + x, 64 + y, (x + y) & 15);
            setTexel(sPageB, 32 + x, 64 + y, (x * 3 + y) & 15); /* page 1, u = 256 + 32 */
        }
    }
    for (i = 0; i < 16; i++) {
        sPageA[244 * PAGE_W + 16 + i] = (uint16_t)(i ? 0x0100 + i : 0); /* the palette lives in page 0 at halfword column 16, row 244 */
        sPageB[244 * PAGE_W + 16 + i] = 0x7FFF;                          /* a decoy: page 1 at the same place must not be read */
    }
    for (y = 0; y < 28; y++) {
        for (x = 0; x < 44; x++) setTexel(sPageB, 0 + x, 128 + y, (x + 2 * y) & 15);
    }
    sheetBuild(&sheet, fr, 3);
    CHECK(peSheetFrame(sheet.p, sheet.n, 7, &f) && f.u == 32 && f.v == 64 && f.w == 32 && f.clutX == 16 && f.clutY == 244 && f.x == 2 && f.y == 2 && f.parts == 1);
    CHECK(peSheetFrame(sheet.p, sheet.n, 40, &f) && f.w == 44 && f.h == 28 && f.u == 256);
    CHECK(!peSheetFrame(sheet.p, sheet.n, 9, &f));  /* no such frame */
    CHECK(!peSheetFrame(sheet.p, sheet.n, 255, &f)); /* the terminator is not a frame */
    CHECK(!peSheetFrame(sheet.p, 10, 7, &f));        /* truncated */
    timBuild(&ta, 2, NULL, 0, 0, sPageA, PAGE_W, PAGE_H);
    timBuild(&tb, 2, NULL, 0, 0, sPageB, PAGE_W, PAGE_H);
    CHECK(imgTimParse(ta.p, ta.n, &pageA) == 0 && imgTimParse(tb.p, tb.n, &pageB) == 0);
    pages[0] = &pageA;
    pages[1] = &pageB;

    CHECK(peSheetFrame(sheet.p, sheet.n, 7, &f) && peArtCell(pages, 2, &f, px2, pal));
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) CHECK(px2[y * 32 + x] == ((x + y) & 15));
    }
    CHECK(pal[0] == 0 && pal[5] == 0x0105);
    CHECK(peSheetFrame(sheet.p, sheet.n, 8, &f) && peArtCell(pages, 2, &f, px2, pal2));
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) CHECK(px2[y * 32 + x] == ((x * 3 + y) & 15)); /* the pixels from page 1 */
    }
    CHECK(!memcmp(pal, pal2, sizeof pal)); /* with page 0's palette */
    CHECK(peSheetFrame(sheet.p, sheet.n, 40, &f) && peArtCell(pages, 2, &f, px, pal2) && px[27 * 44 + 43] == ((43 + 54) & 15));
    CHECK(!peArtCell(pages, 1, &f, px, pal2)); /* page 1 missing */

    /* the strip: cells side by side, the palette as real colours (index 0: 0x0000 is transparent) */
    CHECK(peSheetFrame(sheet.p, sheet.n, 7, &f) && peArtCell(pages, 2, &f, px2, pal));
    cells[0] = px2;
    cells[1] = px2;
    CHECK(peStrip(cells, 2, 32, 32, pal, &pic) == 0 && pic.w == 64 && pic.h == 32 && pic.depth == 4 && pic.nColors == 16);
    CHECK(pic.px[0 * 64 + 40] == px2[0 * 32 + 8] && pic.px[5 * 64 + 32 + 3] == px2[5 * 32 + 3]);
    CHECK(pic.alpha[0] == 0 && pic.alpha[1] == 255 && pic.rgb[3 * 5] == 41 /* 0x0105: r = 5 -> (5 << 3) | (5 >> 2) */);
    imgPicFree(&pic);
    free(sheet.p);
    free(ta.p);
    free(tb.p);
}

/* ---- training sets ---- */

/* the clut of a synthetic image: rows of 16 halfwords, entry 0 transparent unless `opaque0` */
static void clutFill(uint16_t *c, int rows, int seed, int opaque0) {
    int r, i;

    for (r = 0; r < rows; r++) {
        for (i = 0; i < 16; i++) c[r * 16 + i] = (uint16_t)(i == 0 && !opaque0 ? 0 : 0x0200 + seed * 64 + r * 16 + i);
    }
}

/* One training file: 9 sets, set 5 absent (a zero count), set 8 absent without `effect`. Every present set has two images (page 0 packed with RLEN,
   page 1 plain), 3 sprites and 2 animations (the same two for animations 0 and 1). Sprite 0: one part of page 0 row 0 at (4, 6); sprite 1: two parts
   (page 0 row 1 and page 1 row 0: two palettes; the second semi-transparent); sprite 2 (sets 0..7): a part from the effect pages (column 2) and a part of
   an unknown column (7). Set 8's own sprites use its columns 2 and 3. */
static void trainSprites(Buf *b, int set) {
    u16(b, 801); u16(b, 0); u16(b, 3); u16(b, 0);
    u32(b, 1);
    u8(b, 0); u8(b, 0); u8(b, 4); u8(b, 6); u16(b, 0); u16(b, set == 8 ? 2 : 0); u16(b, 16); u16(b, 16);
    u16(b, 0); u16(b, 0); u16(b, 0x1000); u16(b, 0x1000);
    u32(b, 2);
    u8(b, 0); u8(b, 0); u8(b, 0); u8(b, 0); u16(b, set == 8 ? 0 : 1 << 6); u16(b, set == 8 ? 2 : 0); u16(b, 16); u16(b, 16);
    u16(b, 0); u16(b, 0); u16(b, 0x1000); u16(b, 0x1000);
    u8(b, 0); u8(b, 0); u8(b, 20); u8(b, 3); u16(b, 0x8000); u16(b, set == 8 ? 3 : 1); u16(b, 16); u16(b, 16);
    u16(b, 0); u16(b, 0); u16(b, 0x1000); u16(b, 0x1000);
    if (set == 8) {
        u32(b, 0);
        return;
    }
    u32(b, 2);
    u8(b, 0); u8(b, 0); u8(b, 8); u8(b, 8); u16(b, 0); u16(b, 2); u16(b, 16); u16(b, 16);
    u16(b, 0); u16(b, 0); u16(b, 0x1000); u16(b, 0x1000);
    u8(b, 0); u8(b, 0); u8(b, 40); u8(b, 40); u16(b, 0); u16(b, 7); u16(b, 16); u16(b, 16);
    u16(b, 0); u16(b, 0); u16(b, 0x1000); u16(b, 0x1000);
}

static uint8_t *trainBuild(size_t *len, int effect, int bigEntry0, int absent) {
    Buf f = {0};
    int s;

    for (s = 0; s < PE_SETS; s++) u32(&f, 0);
    for (s = 0; s < PE_SETS; s++) {
        size_t base = f.n, bank, anim, img0, img1;
        Buf sp = {0}, t0 = {0}, t1 = {0}, packed = {0};
        uint16_t clut0[16 * 2], clut1[16], *page0 = (uint16_t *)calloc(PAGE_W * PAGE_H, 2), *page1 = (uint16_t *)calloc(PAGE_W * PAGE_H, 2);
        int x, y, i;

        at32(&f, 4 * (size_t)s, (uint32_t)base);
        if (s == absent || (s == 8 && !effect)) {
            u32(&f, 0);
            free(page0);
            free(page1);
            continue;
        }
        for (y = 0; y < 16; y++) {
            for (x = 0; x < 16; x++) {
                setTexel(page0, x, y, 1 + (x + y) % 15);
                setTexel(page1, x, y, 1 + (x * 2 + y) % 15);
            }
        }
        clutFill(clut0, 2, s, bigEntry0 && s == 0);
        clutFill(clut1, 1, s + 20, 0);
        timBuild(&t0, 0, clut0, 16, 2, page0, PAGE_W, PAGE_H);
        timBuild(&t1, 0, clut1, 16, 1, page1, PAGE_W, PAGE_H);
        rlenBuild(&packed, t0.p, t0.n);
        trainSprites(&sp, s);
        bank = base + 48;
        anim = (bank + sp.n + 3) & ~(size_t)3;
        img0 = anim + 4 + 16;
        img1 = (img0 + packed.n + 3) & ~(size_t)3;
        u32(&f, 9);
        u32(&f, (uint32_t)bank);
        for (i = 0; i < 6; i++) u32(&f, i < 2 ? (uint32_t)anim : 0);
        u32(&f, (uint32_t)img0);
        u32(&f, (uint32_t)img1);
        u32(&f, 8);
        u32(&f, (uint32_t)(sp.n - 8));
        put(&f, sp.p, sp.n);
        pad4(&f);
        u16(&f, 0x124); u16(&f, 2);
        u16(&f, 1); u8(&f, 7); u8(&f, 0); u16(&f, 3); u16(&f, 4);
        u16(&f, 0); u8(&f, 9); u8(&f, 0); u16(&f, 0xFFFF); u16(&f, 5);
        put(&f, packed.p, packed.n);
        pad4(&f);
        put(&f, t1.p, t1.n);
        pad4(&f);
        free(sp.p); free(t0.p); free(t1.p); free(packed.p); free(page0); free(page1);
    }
    *len = f.n;
    return f.p;
}

static int collect(void *ctx, const char *rel, const uint8_t *data, size_t len);

typedef struct Got {
    char rel[64][48];
    uint8_t *data[64];
    size_t len[64];
    int n;
} Got;

static int collect(void *ctx, const char *rel, const uint8_t *data, size_t len) {
    Got *g = (Got *)ctx;

    if (g->n >= 64) return -1;
    snprintf(g->rel[g->n], sizeof g->rel[0], "%s", rel);
    g->data[g->n] = (uint8_t *)malloc(len);
    memcpy(g->data[g->n], data, len);
    g->len[g->n++] = len;
    return 0;
}

static int gotFind(const Got *g, const char *rel) {
    int i;

    for (i = 0; i < g->n; i++) {
        if (!strcmp(g->rel[i], rel)) return i;
    }
    return -1;
}

static void gotFree(Got *g) {
    int i;

    for (i = 0; i < g->n; i++) free(g->data[i]);
    memset(g, 0, sizeof *g);
}

static void testTraining(void) {
    size_t len;
    uint8_t *file = trainBuild(&len, 1, 0, 5);
    PeTrain tr;
    PeTrainFrame fr;
    Got got;
    PeTrainStats st;
    PngImage png;
    char why[80];
    int i;

    CHECK(peTrainOpen(&tr, file, len) == 8); /* set 5 is absent */
    CHECK(tr.set[0].present && !tr.set[5].present && tr.set[8].present && tr.set[0].sprites == 3 && tr.set[0].nImages == 2);

    /* sprite 0: one part, one palette: a 4-bit image with that palette; the part's own x, y are the origin */
    CHECK(peTrainFrame(&tr, 0, 0, &fr) && fr.pic.px != NULL && fr.pic.w == 16 && fr.pic.h == 16 && fr.ox == 4 && fr.oy == 6 && fr.pic.depth == 4);
    CHECK(fr.pic.px[0] == 1 && fr.pic.px[1] == 2 && fr.pic.px[16] == 2); /* img0: 1 + (x + y) % 15 */
    CHECK(fr.pic.alpha[0] == 0 && fr.pic.alpha[1] == 255 && fr.pic.nColors == 16);
    peTrainFrameFree(&fr);

    /* sprite 1: two parts on two palettes: 8 bit, the palettes at 16..31 and 32..47 after the transparent index 0 is kept apart (entry 0 is 0x0000) */
    CHECK(peTrainFrame(&tr, 0, 1, &fr) && fr.pic.px != NULL && fr.pic.depth == 8 && fr.pic.nColors == 32 && fr.semi == 1 && fr.parts == 2);
    CHECK(fr.ox == 0 && fr.oy == 0 && fr.pic.w == 36 && fr.pic.h == 19);
    CHECK(fr.pic.px[0] == 1 && fr.pic.px[3 * fr.pic.w + 20] == 16 + 1); /* page 0 row 1 is slot 0, page 1 row 0 is slot 1 (indices 16..31) */
    CHECK(fr.pic.alpha[0] == 0 && fr.pic.alpha[16] == 0 && fr.pic.alpha[17] == 255);
    peTrainFrameFree(&fr);

    /* sprite 2 of set 0: a part from the effect set's page (column 2), a part of an unknown column (7): missing */
    CHECK(peTrainFrame(&tr, 0, 2, &fr) && fr.parts == 2 && fr.shared == 1 && fr.missing == 1 && fr.pic.px != NULL);
    peTrainFrameFree(&fr);
    /* the effect set draws from its own pages: column 2 and 3 */
    CHECK(peTrainFrame(&tr, 8, 0, &fr) && fr.pic.px != NULL && fr.shared == 0 && fr.missing == 0);
    peTrainFrameFree(&fr);
    CHECK(!peTrainFrame(&tr, 0, 3, &fr) && !peTrainFrame(&tr, 5, 0, &fr) && !peTrainFrame(&tr, 9, 0, &fr));

    memset(&got, 0, sizeof got);
    memset(&st, 0, sizeof st);
    CHECK(peTrainEmit(&tr, 0, "partners-training-v1", "tranin00", "dw3:partner/0", "DAT/TRAINING/TRANIN00.BIN", collect, &got, &st) == 0);
    CHECK(st.frames == 3 && st.failed == 0 && st.missing == 1);
    CHECK(gotFind(&got, "000.png") >= 0 && gotFind(&got, "001.png") >= 0 && gotFind(&got, "002.png") >= 0 && gotFind(&got, "training.json") >= 0);
    i = gotFind(&got, "001.png");
    CHECK(i >= 0 && pngReadIndexed(got.data[i], got.len[i], &png, why, sizeof why) == 0 && png.w == 36 && png.h == 19 && png.nPal == 32);
    pngImageFree(&png);
    i = gotFind(&got, "training.json");
    CHECK(i >= 0 && strstr((char *)got.data[i], "\"format\": \"partners-training-v1\"") && strstr((char *)got.data[i], "\"id\": \"dw3:partner/0\"") &&
          strstr((char *)got.data[i], "\"origin\": [4, 6]") && strstr((char *)got.data[i], "{ \"frame\": 1, \"time\": 7, \"x\": 3, \"y\": 4 }"));
    CHECK(i >= 0 && strstr((char *)got.data[i], "\"time\": 9, \"x\": -1, \"y\": 5") != NULL);
    CHECK(peTrainEmit(&tr, 5, "x", "n", NULL, "f", collect, &got, &st) == -1); /* absent set */
    gotFree(&got);
    peTrainClose(&tr);

    /* a visible colour at entry 0 of the first palette: index 0 stays the transparent background, the palette moves up by one */
    free(file);
    file = trainBuild(&len, 1, 1, 5);
    CHECK(peTrainOpen(&tr, file, len) == 8);
    CHECK(peTrainFrame(&tr, 0, 0, &fr) && fr.pic.nColors == 17 && fr.pic.depth == 8 && fr.pic.alpha[0] == 0 && fr.pic.alpha[1] == 255 && fr.pic.px[0] == 1 + 1);
    peTrainFrameFree(&fr);
    peTrainClose(&tr);
    free(file);

    /* no effect set: the partner's part of column 2 cannot be drawn */
    file = trainBuild(&len, 0, 0, 5);
    CHECK(peTrainOpen(&tr, file, len) == 7 && !tr.set[8].present);
    CHECK(peTrainFrame(&tr, 0, 2, &fr) && fr.shared == 0 && fr.missing == 2 && fr.pic.px == NULL);
    peTrainFrameFree(&fr);
    peTrainClose(&tr);
    CHECK(peTrainOpen(&tr, file, 20) == 0); /* truncated: nothing */
    peTrainClose(&tr);
    free(file);
}

/* ---- the synthetic disc ---- */

typedef struct DFile {
    char path[112];
    uint8_t *data;
    size_t size;
    long lba;
} DFile;

static DFile gDisc[200];
static int gDiscN;
static long gNextLba = 100;
static uint8_t (*gSect)[2352];
static long gSectN;
static int gReads;

static void discAdd(const char *path, uint8_t *data, size_t size) {
    DFile *f = &gDisc[gDiscN++];
    long sectors = (long)((size + 2047) / 2048), i;

    snprintf(f->path, sizeof f->path, "%s", path);
    f->data = data;
    f->size = size;
    f->lba = gNextLba;
    gNextLba += sectors + 1;
    if (gNextLba > gSectN) {
        gSect = (uint8_t(*)[2352])realloc(gSect, (size_t)gNextLba * 2352);
        memset(gSect + gSectN, 0, (size_t)(gNextLba - gSectN) * 2352);
        gSectN = gNextLba;
    }
    for (i = 0; i < sectors; i++) {
        size_t n = size - (size_t)i * 2048 < 2048 ? size - (size_t)i * 2048 : 2048;

        memcpy(gSect[f->lba + i] + 24, data + (size_t)i * 2048, n);
    }
}

static int ioList(void *ud, int i, char *path, size_t cap, long *lba, long *size) {
    (void)ud;
    if (i < 0 || i >= gDiscN) return 0;
    snprintf(path, cap, "%s", gDisc[i].path);
    *lba = gDisc[i].lba;
    *size = (long)gDisc[i].size;
    return 1;
}

static int ioRead(void *ud, long lba, unsigned char *frame) {
    (void)ud;
    if (lba < 0 || lba >= gSectN) return 0;
    gReads++;
    memcpy(frame, gSect[lba], 2352);
    return 1;
}

/* a lang file (u32 count, offsets, strings in the game's charmap) of n names */
static uint8_t *langBuild(const char *const *names, int n, size_t *len) {
    Buf b = {0};
    int i;
    size_t at = 4 + 4 * (size_t)n;
    uint8_t *enc[64];
    size_t encLen[64];
    char err[80];

    for (i = 0; i < n; i++) {
        if (tcEncode(names[i], &enc[i], &encLen[i], err, sizeof err) != 0) {
            fprintf(stdout, "partnerexptest: cannot encode %s: %s\n", names[i], err);
            exit(1);
        }
    }
    u32(&b, (uint32_t)n);
    for (i = 0; i < n; i++) {
        u32(&b, (uint32_t)at);
        at += (encLen[i] + 1 + 3) & ~(size_t)3;
    }
    for (i = 0; i < n; i++) {
        put(&b, enc[i], encLen[i]);
        u8(&b, 0);
        pad4(&b);
        free(enc[i]);
    }
    *len = b.n;
    return b.p;
}

static const char *const kNames[] = {"", "Alpha", "Beta Two", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta", "Iota"};
static const char *const kKeys[PE_PARTNERS] = {"alpha", "beta-two", "gamma", "delta", "epsilon", "zeta", "eta", "theta"};

/* the gym's text: made-up training names at the entries the decomp's STGTRAIN_trainings tables name (19..23, 29..35); the first is repeated and the last empty */
static const char *const kTrainNames[12] = {"Anvil Lift", "Stone Hold", "Rope Climb", "Book Stack", "Hoop Jump", "Sun Walk",
                                            "Rain Walk", "Snow Walk", "Gale Walk", "Bolt Catch", "Anvil  Lift!", ""};
static const char *const kTrainDirs[12] = {"anvil-lift", "stone-hold", "rope-climb", "book-stack", "hoop-jump", "sun-walk",
                                           "rain-walk", "snow-walk", "gale-walk", "bolt-catch", "anvil-lift-2", "training-12"};

static uint8_t *trainTextBuild(size_t *len) {
    const char *names[36];
    int i;

    for (i = 0; i < 36; i++) names[i] = "";
    for (i = 0; i < 12; i++) names[peTrainNameEntry(i)] = kTrainNames[i];
    return langBuild(names, 36, len);
}

/* one screen: a CS file with the sheet at its entry, a TM file whose pages sit in a sub-archive (chain 0, k) */
static int gTweak = -1; /* the screen whose frame 3 gets one pixel changed */

static void screenFiles(const char *sheetPath, const char *pagesPath, int entry, int pixelPage, int index) {
    Buf cs = {0}, tm = {0}, a = {0}, b = {0}, sub = {0};
    SynFrame fr[37];
    int f, i, x, y, n = 0;
    size_t tableBytes = entry == 0 ? 4 : 16;
    uint16_t pageA[PAGE_W * PAGE_H], pageB[PAGE_W * PAGE_H];
    uint16_t *px = pixelPage ? pageB : pageA;

    memset(pageA, 0, sizeof pageA);
    memset(pageB, 0, sizeof pageB);
    /* 29 frames of 32x32 on `pixelPage`, 8 groups' palettes in page 0 rows 244.. (the same in every screen) */
    for (f = 0; f < 29; f++) {
        int p = f < 3 ? 5 : f < 7 ? 3 : f < 11 ? 0 : f < 14 ? 2 : f < 17 ? 1 : f < 21 ? 6 : f < 25 ? 7 : 4;
        int u = (f % 8) * 32, v = 16 + (f / 8) * 32;

        fr[n].id = f; fr[n].u = u + (pixelPage ? 256 : 0); fr[n].v = v; fr[n].w = 32; fr[n].h = 32; fr[n].cx = 16 * (p % 4); fr[n].cy = 244 + p / 4; fr[n].x = f % 3; fr[n].y = f % 5;
        n++;
        for (y = 0; y < 32; y++) {
            for (x = 0; x < 32; x++) setTexel(px, u + x, v + y, (x + y + f) & 15);
        }
    }
    if (index == gTweak) setTexel(px, 3 * 32 % 256 /* frame 3: u = 96 */, 16, 15 - ((0 + 0 + 3) & 15));
    for (f = 0; f < 8; f++) {
        fr[n].id = 40 + f; fr[n].u = (pixelPage ? 256 : 0) + (f % 4) * 48; fr[n].v = 160 + (f / 4) * 32; fr[n].w = 44; fr[n].h = 28; fr[n].cx = 16 * (f % 4); fr[n].cy = 246 + f / 4; fr[n].x = 0; fr[n].y = 0;
        n++;
        for (y = 0; y < 28; y++) {
            for (x = 0; x < 44; x++) setTexel(px, fr[n - 1].u % 256 + x, 160 + (f / 4) * 32 + y, (x * 2 + y + f) & 15);
        }
    }
    for (f = 0; f < 8; f++) { /* the palettes, in page 0: row 244 + p / 4, column 16 * (p % 4) */
        for (i = 0; i < 16; i++) pageA[(244 + f / 4) * PAGE_W + 16 * (f % 4) + i] = (uint16_t)(i ? 0x0400 + f * 32 + i : 0);
    }
    for (f = 0; f < 8; f++) { /* the icons' palettes: rows 246/247 (the same columns): distinct from the portraits' */
        for (i = 0; i < 16; i++) pageA[(246 + f / 4) * PAGE_W + 16 * (f % 4) + i] = (uint16_t)(i ? 0x0200 + f * 32 + i : 0);
    }
    /* a decoy: the same place in page 1 */
    for (i = 0; i < 16; i++) pageB[244 * PAGE_W + i] = 0x7FFF;
    {
        Buf sheet = {0};

        sheetBuild(&sheet, fr, n);
        for (i = 0; i < (int)tableBytes / 4; i++) u32(&cs, entry == 0 ? 4 : (i == 0 ? 16 : i == 3 ? 16 : 0));
        put(&cs, sheet.p, sheet.n);
        free(sheet.p);
    }
    timBuild(&a, 2, NULL, 0, 0, pageA, PAGE_W, PAGE_H);
    timBuild(&b, 2, NULL, 0, 0, pageB, PAGE_W, PAGE_H);
    u32(&sub, 12); /* a sub-archive of 2 entries: [12, 12 + len(a), 0?]. The table is 3 words: the last is the end */
    u32(&sub, (uint32_t)(12 + a.n));
    u32(&sub, 0);
    put(&sub, a.p, a.n);
    put(&sub, b.p, b.n);
    u32(&tm, 4);
    put(&tm, sub.p, sub.n);
    discAdd(sheetPath, cs.p, cs.n);
    discAdd(pagesPath, tm.p, tm.n);
    free(a.p); free(b.p); free(sub.p);
}

/* a sprite archive with one 4-bit frame and no set table: "frames only" (sprite_data.c) */
static uint8_t *spriteFile(size_t *len, int seed) {
    Buf f = {0}, t = {0};
    uint16_t clut[16], img[4];
    int i;

    for (i = 0; i < 16; i++) clut[i] = (uint16_t)(i ? 0x0300 + seed * 16 + i : 0);
    for (i = 0; i < 4; i++) img[i] = (uint16_t)(0x4321 + seed);
    timBuild(&t, 0, clut, 16, 1, img, 1, 4);
    u32(&f, 12);
    u32(&f, (uint32_t)(12 + t.n));
    u32(&f, 0);
    put(&f, t.p, t.n);
    u32(&f, 0); /* the last entry: not a set table */
    u32(&f, 0);
    free(t.p);
    *len = f.n;
    return f.p;
}

static const char *const kScreenFiles[7][2] = {
    {"AAA/DAT/SCREEN/NAMEDTCS.BIN", "AAA/DAT/SCREEN/NAMEDTTM.BIN"}, {"AAA/DAT/NAMEENT/STDGNMCS.BIN", "AAA/DAT/NAMEENT/STDGNMTM.BIN"},
    {"AAA/DAT/DGLB/STDGLBCS.BIN", "AAA/DAT/DGLB/STDGLBTM.BIN"},     {"AAA/DAT/SCREEN/STSTATCS.BIN", "AAA/DAT/SCREEN/STSTATTM.BIN"},
    {"AAA/DAT/TRAINING/STTRNGCS.BIN", "AAA/DAT/TRAINING/STTRNGTM.BIN"}, {"AAA/DAT/SCREEN/STFRPTCS.BIN", "AAA/DAT/SCREEN/STFRPTTM.BIN"},
    {"AAA/DAT/MCARD/STMCRDCS.BIN", "AAA/DAT/MCARD/STMCRDTM.BIN"},
};

static void testTrainFolders(void) {
    char names[PE_TRAININGS][64];
    size_t len;
    uint8_t *d = trainTextBuild(&len);
    int i;

    peTrainFolders(d, len, names);
    for (i = 0; i < PE_TRAININGS; i++) CHECK(!strcmp(names[i], kTrainDirs[i]));
    free(d);
    peTrainFolders(NULL, 0, names); /* a text that cannot be read: the row number, never a disc id */
    CHECK(!strcmp(names[0], "training-1") && !strcmp(names[11], "training-12"));
    CHECK(peTrainNameEntry(0) == 19 && peTrainNameEntry(4) == 23 && peTrainNameEntry(5) == 29 && peTrainNameEntry(11) == 35 && peTrainNameEntry(12) == -1);
    CHECK(peScreenOfPages("AAA/DAT/SCREEN/NAMEDTTM.BIN", NULL) == 0 && peScreenOfPages("AAA/DAT/SCREEN/STSHOPTM.BIN", NULL) == -1);
    {
        int entry = -1;

        CHECK(peScreenOfPages("AAA/DAT/TRAINING/STTRNGTM.BIN", &entry) == 4 && entry == 3);
    }
}

static void discBuild(void) {
    int s, p, t;
    size_t len;
    uint8_t *d;

    gDiscN = 0;
    gNextLba = 100;
    d = langBuild(kNames, 10, &len);
    discAdd("AAA/DAT/COUNTRY/ENG/ESDIGNAM.BIN", d, len);
    d = trainTextBuild(&len);
    discAdd("AAA/DAT/COUNTRY/ENG/ESTRAINI.BIN", d, len);
    for (s = 0; s < 7; s++) screenFiles(kScreenFiles[s][0], kScreenFiles[s][1], s == 4 ? 3 : 0, s == 6, s);
    for (p = 0; p < PE_PARTNERS; p++) {
        char path[96];

        snprintf(path, sizeof path, "AAA/DAT/FIELD/SPRT/%s.BIN", peLayout(p)->fieldFile);
        d = spriteFile(&len, p);
        discAdd(path, d, len);
    }
    for (t = 0; t < PE_TRAININGS; t++) {
        char path[96];

        snprintf(path, sizeof path, "AAA/DAT/TRAINING/%s.BIN", peTrainFile(t));
        d = trainBuild(&len, t != 2 && t != 4, 0, -1); /* like the real ones: tranin02 and tranin04 have no effect */
        discAdd(path, d, len);
    }
    discAdd("AAA/DAT/SCREEN/STSHOPTM.BIN", (uint8_t *)calloc(1, 2048), 2048);
}

static void discFree(void) {
    int i;

    for (i = 0; i < gDiscN; i++) free(gDisc[i].data);
    gDiscN = 0;
    free(gSect);
    gSect = NULL;
    gSectN = 0;
}

static char gData[1024];

static int exists(const char *rel) {
    char p[1400];

    snprintf(p, sizeof p, "%s/%s", gData, rel);
    return access(p, F_OK) == 0;
}

static int readRel(const char *rel, uint8_t **d, size_t *n) {
    char p[1400];

    snprintf(p, sizeof p, "%s/%s", gData, rel);
    return tdReadFile(p, d, n);
}

static int hashRel(const char *rel, char hex[17]) {
    uint8_t *d;
    size_t n;

    if (readRel(rel, &d, &n)) return -1;
    tdHashHex(tdHash(d, n), hex);
    free(d);
    return 0;
}

static void testRun(void) {
    PartnerExportIo io = {ioList, ioRead, NULL};
    PartnerExportStats st;
    uint8_t *d;
    size_t n;
    PngImage png;
    char why[80], h1[17], h2[17], rel[200], cmd[2200];
    int p, i, reads;

    discBuild();
    partnerExportSetup(gData, &io);
    CHECK(partnerExportRun("nonsense") == -1);
    CHECK(partnerExportRun("beta-two") == 1); /* one partner by name; its files only */
    CHECK(exists("partners/beta-two/portrait.png") && !exists("partners/alpha/portrait.png"));
    CHECK(partnerExportRun("all") == 8);
    partnerExportTotals(&st);
    CHECK(st.partners == 8 && st.failed == 0 && st.mismatches == 0 && !st.skipped);
    for (p = 0; p < PE_PARTNERS; p++) {
        const PeLayout *lay = peLayout(p);

        snprintf(rel, sizeof rel, "partners/%s/portrait.png", kKeys[p]);
        CHECK(readRel(rel, &d, &n) == 0 && pngReadIndexed(d, n, &png, why, sizeof why) == 0 && png.w == 32 * lay->portraitCells && png.h == 32 && png.nPal == 16);
        if (readRel(rel, &d, &n) == 0) free(d);
        /* cell c of the strip is frame portraitFirst + c of the sheet: pixel (x + y + f) & 15 at (x, y) */
        for (i = 0; i < lay->portraitCells; i++) {
            int f = lay->portraitFirst + i;

            CHECK(png.px[1 * png.w + 32 * i + 2] == ((2 + 1 + f) & 15) && png.px[31 * png.w + 32 * i + 31] == ((31 + 31 + f) & 15));
        }
        CHECK(png.alpha[0] == 0 || png.nAlpha >= 1);
        CHECK(png.nAlpha >= 1 && png.alpha[0] == 0 && png.rgb[3 * 3 + 1] != 0 + 255); /* colour 3 is the group's own: not the decoy 0x7FFF (white) */
        pngImageFree(&png);
        snprintf(rel, sizeof rel, "partners/%s/icon.png", kKeys[p]);
        CHECK(readRel(rel, &d, &n) == 0 && pngReadIndexed(d, n, &png, why, sizeof why) == 0 && png.w == 44 && png.h == 28);
        free(d);
        CHECK(png.px[27 * 44 + 43] == ((43 * 2 + 27 + (lay->iconFrame - 40)) & 15)); /* the icon of frame iconFrame, pixels of page 1 */
        pngImageFree(&png);
        snprintf(rel, sizeof rel, "partners/%s/field/000.png", kKeys[p]);
        CHECK(exists(rel));
        snprintf(rel, sizeof rel, "partners/%s/field/sprite.json", kKeys[p]);
        CHECK(exists(rel));
        for (i = 0; i < PE_TRAININGS; i++) {
            snprintf(rel, sizeof rel, "partners/%s/training/%s/training.json", kKeys[p], kTrainDirs[i]);
            CHECK(exists(rel));
            snprintf(rel, sizeof rel, "partners/%s/training/%s/000.png", kKeys[p], kTrainDirs[i]);
            CHECK(exists(rel));
        }
        snprintf(rel, sizeof rel, "partners/%s/partner.json", kKeys[p]);
        CHECK(readRel(rel, &d, &n) == 0);
        {
            char want[96];
            int ok;

            ok = readRel(rel, &d, &n) == 0;
            snprintf(want, sizeof want, "\"id\": \"dw3:partner/%d\"", p);
            CHECK(ok && strstr((char *)d, "\"format\": \"partners-partner-v1\"") && strstr((char *)d, want) && strstr((char *)d, kKeys[p]) &&
                  strstr((char *)d, "\"firstForm\": \"dw3:digimon/") && strstr((char *)d, "\"cells\": "));
            if (ok) free(d);
        }
    }
    /* the effects the partners share: once, by training, under menus/ */
    CHECK(exists("menus/gym/effects/anvil-lift/000.png") && exists("menus/gym/effects/anvil-lift/training.json") && !exists("menus/gym/effects/rope-climb/000.png") &&
          !exists("menus/gym/effects/hoop-jump/training.json") && exists("menus/gym/effects/training-12/000.png"));
    /* the disc's file names are not paths: they live in training.json only */
    CHECK(!exists("partners/alpha/training/tranin00/training.json") && !exists("menus/gym/effects/tranin00/000.png"));
    {
        char *tj = NULL;
        size_t tn;

        CHECK(readRel("partners/alpha/training/anvil-lift/training.json", (uint8_t **)&tj, &tn) == 0);
        if (tj != NULL) {
            CHECK(strstr(tj, "TRANIN00.BIN") != NULL && strstr(tj, "\"name\": \"anvil-lift\"") != NULL);
            free(tj);
        }
    }
    CHECK(exists("partners/manifest.json") && exists("menus/manifest.json"));
    /* no path of a disc id: no numbers in the folder names but the training files' own */
    CHECK(!exists("partners/alpha/p003pken") && !exists("partners/p003pken"));

    /* idempotent: a second run reads nothing and changes nothing; a deleted file brings the work back with the same bytes */
    CHECK(hashRel("partners/alpha/portrait.png", h1) == 0);
    reads = gReads;
    CHECK(partnerExportRun("all") == 0);
    partnerExportTotals(&st);
    CHECK(st.skipped == 1 && gReads - reads <= 2); /* only the names file is read (to resolve the spec) */
    snprintf(rel, sizeof rel, "%s/partners/alpha/portrait.png", gData);
    CHECK(unlink(rel) == 0);
    CHECK(partnerExportRun("all") == 8);
    partnerExportTotals(&st);
    CHECK(st.failed == 0 && st.written == 1 && st.kept > 100 && !st.skipped);
    CHECK(hashRel("partners/alpha/portrait.png", h2) == 0 && !strcmp(h1, h2));
    /* an edited file stays, the new one goes beside it */
    snprintf(rel, sizeof rel, "partners/alpha/icon.png");
    {
        char p2[1400];

        snprintf(p2, sizeof p2, "%s/%s", gData, rel);
        CHECK(tdWriteFileAtomic(p2, "edited", 6) == 0);
        snprintf(p2, sizeof p2, "%s/partners/alpha/field/000.png", gData);
        CHECK(unlink(p2) == 0);
    }
    CHECK(partnerExportRun("all") == 8);
    partnerExportTotals(&st);
    CHECK(st.newFiles == 1 && st.written == 1 && exists("partners/alpha/icon.png.new"));
    discFree();

    /* a screen whose portraits are not the others': counted and logged, the bundle still comes from the first screen */
    gTweak = 5;
    discBuild();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/partners' '%s/menus'", gData, gData);
    CHECK(system(cmd) == 0);
    CHECK(partnerExportRun("all") == 8);
    partnerExportTotals(&st);
    CHECK(st.mismatches == 1 && st.failed == 0);
    gTweak = -1;
    discFree();
    /* the disc is not the EU one (no portrait sheets): nothing is written */
    gDiscN = 0;
    gNextLba = 100;
    {
        size_t len;
        uint8_t *d2 = langBuild(kNames, 10, &len);

        discAdd("AAA/DAT/COUNTRY/ENG/ESDIGNAM.BIN", d2, len);
    }
    snprintf(cmd, sizeof cmd, "rm -rf '%s/partners' '%s/menus'", gData, gData);
    CHECK(system(cmd) == 0);
    CHECK(partnerExportRun("all") == -1 && !exists("partners/alpha/portrait.png"));
    discFree();
}

int main(void) {
    const char *tmp = getenv("TMPDIR");
    char dir[1024];

    snprintf(dir, sizeof dir, "%s/partnerexptest-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (mkdtemp(dir) == NULL) {
        fprintf(stdout, "partnerexptest: cannot make a scratch folder\n");
        return 1;
    }
    snprintf(gData, sizeof gData, "%s/data", dir);
    CHECK(tdMakeDirs(gData) == 0);
    testSlug();
    testCellsAndStrip();
    testTrainFolders();
    testTraining();
    testRun();
    {
        char cmd[1100];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
        if (system(cmd) != 0) fprintf(stdout, "partnerexptest: could not remove %s\n", dir);
    }
    if (failures) {
        fprintf(stdout, "partnerexptest: %d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stdout, "partnerexptest: OK\n");
    return 0;
}
