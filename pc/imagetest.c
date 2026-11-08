/*
 * Host test of the menu art extraction and substitution (pc/shim/image_data.c, png_write.c, png_read.c;
 * feature menu-art-png T2 and T3). Synthetic TIMs only (no disc, no retail bytes); the scratch folder lives under
 * $TMPDIR.
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "shim/exp_core.h"
#include "shim/image_data.h"
#include "shim/mods.h"
#include "shim/png_read.h"
#include "shim/png_write.h"
#include "shim/sprite_data.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "imagetest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- the PNG reader of the tests: the port's own (png_read.c, real inflate, all filters); the exports are real deflate now ---- */

typedef struct Png {
    int w, h, depth, ctype;
    uint8_t rgb[256 * 3];
    int nPal;
    uint8_t alpha[256];
    int nAlpha;
    uint8_t *px; /* one index per pixel */
} Png;

static uint32_t crc32of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

static int pngRead(const uint8_t *d, size_t n, Png *o)
{
    PngImage im;
    char why[96];
    memset(o, 0, sizeof *o);
    if (pngReadIndexed(d, n, &im, why, sizeof why) != 0) return -1;
    o->w = im.w;
    o->h = im.h;
    o->depth = im.depth;
    o->ctype = 3;
    o->nPal = im.nPal;
    memcpy(o->rgb, im.rgb, sizeof o->rgb);
    o->nAlpha = im.nAlpha;
    memcpy(o->alpha, im.alpha, sizeof o->alpha);
    o->px = im.px;
    return 0;
}

/* ---- synthetic data builders ---- */

static void w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v & 0xFFFF); w16(p + 2, v >> 16); }

/* A TIM: bpp mode 0/1/2, a CLUT of cw x ch halfwords (cw 0: none), an image of iw x ih halfwords */
static size_t makeTim(uint8_t *out, int mode, int cw, int ch, const uint16_t *clut, int iw, int ih, const uint16_t *img)
{
    size_t at = 8, i;
    w32(out, 0x10);
    w32(out + 4, (uint32_t)mode | (cw ? 8u : 0u));
    if (cw) {
        w32(out + at, (uint32_t)(12 + 2 * cw * ch));
        w16(out + at + 4, 0);
        w16(out + at + 6, 480);
        w16(out + at + 8, (unsigned)cw);
        w16(out + at + 10, (unsigned)ch);
        for (i = 0; i < (size_t)cw * ch; i++) w16(out + at + 12 + 2 * i, clut[i]);
        at += (size_t)(12 + 2 * cw * ch);
    }
    w32(out + at, (uint32_t)(12 + 2 * iw * ih));
    w16(out + at + 4, 0);
    w16(out + at + 6, 0);
    w16(out + at + 8, (unsigned)iw);
    w16(out + at + 10, (unsigned)ih);
    for (i = 0; i < (size_t)iw * ih; i++) w16(out + at + 12 + 2 * i, img[i]);
    return at + (size_t)(12 + 2 * iw * ih);
}

/* RLEN: "RLEN", unpacked size, then runs (literal: n bytes; repeat: 0x80|n, byte), 0 ends */
static size_t rlenPack(const uint8_t *src, size_t n, uint8_t *out)
{
    size_t i = 0, o = 8;
    memcpy(out, "RLEN", 4);
    w32(out + 4, (uint32_t)n);
    while (i < n) {
        size_t run = 1;
        while (i + run < n && src[i + run] == src[i] && run < 127) run++;
        if (run >= 3) {
            out[o++] = (uint8_t)(0x80 | run);
            out[o++] = src[i];
            i += run;
        } else {
            size_t lit = 0;
            while (i + lit < n && lit < 100 && !(i + lit + 2 < n && src[i + lit] == src[i + lit + 1] && src[i + lit] == src[i + lit + 2])) lit++;
            if (lit == 0) lit = 1;
            out[o++] = (uint8_t)lit;
            memcpy(out + o, src + i, lit);
            o += lit;
            i += lit;
        }
    }
    out[o++] = 0;
    return o;
}

/* An archive: a table of offsets (relative to its start), then the entries. term: a 0 ends the table */
static size_t makeArchive(uint8_t *out, const uint8_t *const *ents, const size_t *lens, int n, int term)
{
    int tab = n + (term ? 1 : 0), i;
    size_t at = (size_t)tab * 4;
    for (i = 0; i < n; i++) {
        w32(out + 4 * i, (uint32_t)at);
        memcpy(out + at, ents[i], lens[i]);
        at += (lens[i] + 3) & ~(size_t)3;
        while (at % 4) out[at++] = 0;
    }
    if (term) w32(out + 4 * n, 0);
    return at;
}

/* a sprite sheet file: [4] then the sheet: partsOff, idsOff, one frame offset, parts */
static size_t makeSheet(uint8_t *out, const int (*parts)[7], int np)
{
    uint8_t *s = out + 4;
    int i, k;
    w32(out, 4);
    w32(s, 12);
    w32(s + 4, (uint32_t)(12 + 14 * np));
    w32(s + 8, (uint32_t)(12 + 14 * np + 4));
    for (i = 0; i < np; i++) {
        for (k = 0; k < 7; k++) w16(s + 12 + 14 * i + 2 * k, (unsigned)parts[i][k]);
    }
    s[12 + 14 * np] = 0;
    return 4 + 12 + 14 * (size_t)np + 4;
}

static uint16_t rgb555(int r, int g, int b) { return (uint16_t)(r | g << 5 | b << 10); }

static char gRoot[1024];

static char *slurp(const char *rel, size_t *n)
{
    char path[1300];
    uint8_t *d;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    if (tdReadFile(path, &d, n)) return NULL;
    return (char *)d;
}

static void spit(const char *rel, const void *d, size_t n)
{
    char path[1300], dir[1300], *sl;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "%s", path);
    sl = strrchr(dir, '/');
    if (sl) {
        *sl = 0;
        CHECK(tdMakeDirs(dir) == 0);
    }
    CHECK(tdWriteFileAtomic(path, d, n) == 0);
    imgOverridesRescan(); /* the sources are indexed once: a test that writes a source says so */
}

static void rmtree(const char *dir)
{
    char cmd[1400];
    imgOverridesRescan();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd)) {
    }
}

static int exists(const char *rel)
{
    char path[1300];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    return stat(path, &st) == 0;
}

/* ---- the cases ---- */

static void testPng(void)
{
    uint8_t px[5 * 3], *png;
    uint8_t rgb[16 * 3], alpha[2] = {0, 255};
    size_t len;
    Png p;
    int i;

    for (i = 0; i < 16; i++) {
        rgb[3 * i] = (uint8_t)(i * 16);
        rgb[3 * i + 1] = (uint8_t)i;
        rgb[3 * i + 2] = (uint8_t)(255 - i);
    }
    for (i = 0; i < 15; i++) px[i] = (uint8_t)((i * 7) & 15);
    CHECK(pngWriteIndexed(px, 5, 3, 4, rgb, 16, alpha, 2, &png, &len) == 0);
    CHECK(pngRead(png, len, &p) == 0);
    CHECK(p.w == 5 && p.h == 3 && p.depth == 4 && p.nPal == 16 && p.nAlpha == 2 && p.alpha[0] == 0);
    CHECK(memcmp(p.px, px, 15) == 0 && memcmp(p.rgb, rgb, 48) == 0);
    free(png);
    free(p.px);

    {
        /* 300x300 at 8 bits = 90300 bytes: more than one stored block */
        uint8_t *big = (uint8_t *)malloc(300 * 300), rgb8[256 * 3];
        for (i = 0; i < 300 * 300; i++) big[i] = (uint8_t)(i * 31 + i / 300);
        for (i = 0; i < 256 * 3; i++) rgb8[i] = (uint8_t)i;
        CHECK(pngWriteIndexed(big, 300, 300, 8, rgb8, 256, NULL, 0, &png, &len) == 0);
        CHECK(pngRead(png, len, &p) == 0);
        CHECK(p.w == 300 && p.h == 300 && p.depth == 8 && p.nAlpha == 0 && memcmp(p.px, big, 300 * 300) == 0);
        free(png);
        free(p.px);
        free(big);
    }
    CHECK(pngWriteIndexed(px, 5, 3, 4, rgb, 17, NULL, 0, &png, &len) != 0); /* too many colours for 4 bits */
}

static void testDecode(void)
{
    uint8_t buf[2048];
    uint16_t clut[16], img[4], raw[8];
    ImgPic pic;
    size_t n;
    ImgHints hints;
    uint16_t pal[16];
    int i;

    for (i = 0; i < 16; i++) clut[i] = (uint16_t)(i ? rgb555(i, 31 - i, i / 2) : 0);
    /* 4-bit: 2 halfwords wide = 8 pixels; low nibble first */
    img[0] = 0x4321; img[1] = 0xFEDC; img[2] = 0x0000; img[3] = 0x1111;
    n = makeTim(buf, 0, 16, 1, clut, 2, 2, img);
    CHECK(imgDecodeTim(buf, n, NULL, &pic) == 0);
    CHECK(pic.w == 8 && pic.h == 2 && pic.depth == 4 && pic.nColors == 16 && !pic.guessed);
    CHECK(pic.px[0] == 1 && pic.px[1] == 2 && pic.px[3] == 4 && pic.px[4] == 12 && pic.px[7] == 15 && pic.px[8] == 0);
    CHECK(pic.alpha[0] == 0 && pic.alpha[1] == 255);
    CHECK(pic.rgb[3] == ((1 << 3) | (1 >> 2)) && pic.rgb[4] == ((30 << 3) | (30 >> 2)));
    imgPicFree(&pic);

    /* 8-bit: 2 halfwords = 4 pixels; the CLUT has two rows and the first is the palette */
    {
        uint16_t c8[256 * 2];
        memset(c8, 0, sizeof c8);
        c8[0] = 0; c8[5] = rgb555(31, 0, 0); c8[256 + 5] = rgb555(0, 31, 0);
        raw[0] = 0x0500; raw[1] = 0x0105;
        n = makeTim(buf, 1, 256, 2, c8, 2, 1, raw);
        CHECK(imgDecodeTim(buf, n, NULL, &pic) == 0);
        CHECK(pic.w == 4 && pic.h == 1 && pic.depth == 8 && pic.nColors == 256);
        CHECK(pic.px[0] == 0 && pic.px[1] == 5 && pic.px[2] == 5 && pic.px[3] == 1);
        CHECK(pic.rgb[15] == 255 && pic.rgb[16] == 0 && pic.alpha[0] == 0 && pic.alpha[5] == 255);
        imgPicFree(&pic);
    }

    /* 16-bit raw page, 4-bit reading, with a palette given and without */
    raw[0] = 0x3210; raw[1] = 0x7654;
    n = makeTim(buf, 2, 0, 0, NULL, 2, 1, raw);
    CHECK(imgDecodeTim(buf, n, NULL, &pic) == 0);
    CHECK(pic.w == 8 && pic.depth == 4 && pic.guessed && pic.px[5] == 5 && pic.alpha[0] == 0);
    imgPicFree(&pic);
    for (i = 0; i < 16; i++) pal[i] = (uint16_t)(i ? rgb555(i, 0, 31) : 0);
    hints.pixelBits = 4; hints.pal = pal; hints.nPal = 16;
    CHECK(imgDecodeTim(buf, n, &hints, &pic) == 0 && !pic.guessed && pic.rgb[3 * 2 + 2] == 255);
    imgPicFree(&pic);
    hints.pixelBits = 8;
    CHECK(imgDecodeTim(buf, n, &hints, &pic) == 0 && pic.w == 4 && pic.depth == 8 && pic.px[3] == 0x76);
    imgPicFree(&pic);

    /* a damaged TIM is refused, not read out of bounds */
    CHECK(imgDecodeTim(buf, 10, NULL, &pic) != 0);
    CHECK(imgDecodeTim(buf, n - 4, NULL, &pic) != 0);
}

typedef struct Seen {
    int count;
    int chains[8][3];
    int depths[8];
    size_t lens[8];
} Seen;

static int onTim(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len)
{
    Seen *s = (Seen *)ctx;
    int i;
    (void)tim;
    if (s->count < 8) {
        for (i = 0; i < depth; i++) s->chains[s->count][i] = chain[i];
        s->depths[s->count] = depth;
        s->lens[s->count] = len;
    }
    s->count++;
    return 0;
}

/* a TM-shaped disc file: [4] -> a sub-archive of (an RLEN raw page, a plain 8-bit TIM) */
static size_t makeTmFile(uint8_t *out, const uint16_t *page, int pageW, int pageH)
{
    uint8_t t1[4096], t2[4096], z1[8192], sub[16384];
    uint16_t c8[256];
    uint16_t pix[2] = {0x0102, 0x0304};
    size_t n1, n2, nz, nsub;
    const uint8_t *ents[2];
    size_t lens[2];
    const uint8_t *top[1];
    size_t topLens[1];

    memset(c8, 0, sizeof c8);
    c8[1] = rgb555(1, 2, 3);
    n1 = makeTim(t1, 2, 0, 0, NULL, pageW, pageH, page);
    nz = rlenPack(t1, n1, z1);
    n2 = makeTim(t2, 1, 256, 1, c8, 2, 1, pix);
    ents[0] = z1; lens[0] = nz;
    ents[1] = t2; lens[1] = n2;
    nsub = makeArchive(sub, ents, lens, 2, 1);
    top[0] = sub; topLens[0] = nsub;
    return makeArchive(out, top, topLens, 1, 0);
}

/* A screen whose palettes sit on another page than the pixels (stmcrdtm, sttrngtm): a part names its CLUT as a position of the
   screen's image space (64 halfwords per page), so the palette is where that position falls, not on the page of the pixels. */
static size_t makeTwoPageTm(uint8_t *out)
{
    uint8_t t[4096], z[2][8192], sub[16384];
    uint16_t page[2][64 * 4];
    const uint8_t *ents[2];
    size_t lens[2], nz, nsub;
    const uint8_t *top[1];
    size_t topLens[1];
    int i, k;

    memset(page, 0, sizeof page);
    for (i = 0; i < 16; i++) {
        page[0][2 * 64 + 48 + i] = rgb555(31, i, 0); /* page 0: the palette at (48, 2), the next row at (48, 3) */
        page[0][3 * 64 + 48 + i] = rgb555(0, i, 31);
        page[1][2 * 64 + 48 + i] = rgb555(i, 31, i); /* page 1: a decoy where a reader of the pixel page would look */
        page[1][2 * 64 + 16 + i] = rgb555(7, 7, i);  /* and a real palette at column 64 + 16 of the screen's space */
    }
    page[1][0] = 0x2100;
    for (k = 0; k < 2; k++) {
        nz = rlenPack(t, makeTim(t, 2, 0, 0, NULL, 64, 4, page[k]), z[k]);
        ents[k] = z[k];
        lens[k] = nz;
    }
    nsub = makeArchive(sub, ents, lens, 2, 1);
    top[0] = sub;
    topLens[0] = nsub;
    return makeArchive(out, top, topLens, 1, 0);
}

static void testScreenPalette(void)
{
    uint8_t file[65536], sheet[512];
    const int parts[3][7] = {{256, 0, 16, 3, 48, 2, 0}, {0, 0, 16, 3, 64 + 16, 2, 0}, {0, 0, 16, 3, 48, 3, 0}};
    size_t n = makeTwoPageTm(file), sn = makeSheet(sheet, parts, 3);
    ImgPages *pg = imgPagesOpen(file, n);
    ImgPart part;
    ImgPalAt at;
    uint16_t pal[256];
    int i, ok;

    CHECK(pg != NULL && imgSheetPartCount(sheet, sn) == 3 && imgSheetPart(sheet, sn, 0, &part) == 0 && part.u == 256 && part.clutX == 48 && part.clutY == 2);
    if (!pg) return;
    /* a part of page 1 whose CLUT column 48 is page 0's: the palette of page 0, not the decoy under the pixels */
    CHECK(imgPartPalette(pg, &part, 0, pal, &at) == 0 && at.page == 0 && at.x == 48 && at.y == 2);
    for (i = 0, ok = 1; i < 16; i++) ok &= pal[i] == rgb555(31, i, 0);
    CHECK(ok);
    /* a frame's added CLUT row selects the next row */
    CHECK(imgPartPalette(pg, &part, 1, pal, &at) == 0 && at.page == 0 && at.y == 3);
    for (i = 0, ok = 1; i < 16; i++) ok &= pal[i] == rgb555(0, i, 31);
    CHECK(ok);
    /* column 64 + 16 is page 1, column 16 */
    CHECK(imgSheetPart(sheet, sn, 1, &part) == 0 && imgPartPalette(pg, &part, 0, pal, &at) == 0 && at.page == 1 && at.x == 16 && at.y == 2);
    for (i = 0, ok = 1; i < 16; i++) ok &= pal[i] == rgb555(7, 7, i);
    CHECK(ok);
    /* outside the pages: no palette */
    CHECK(imgSheetPart(sheet, sn, 2, &part) == 0 && imgPartPalette(pg, &part, 3, pal, &at) != 0); /* row 6 of 4 */
    CHECK(imgPartPalette(pg, &part, -4, pal, &at) != 0);
    part.clutX = 128;
    CHECK(imgPartPalette(pg, &part, 0, pal, &at) != 0); /* page 2 does not exist */
    imgPagesFree(pg);

    /* the page export: the PNG of page 1 carries the palette of page 0 */
    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (mkdtemp(gRoot)) {
        ImgRun run;
        Png p;
        size_t tl;
        char *txt;

        memset(&run, 0, sizeof run);
        CHECK(imgRunOpen(&run, gRoot, "aaaa", 1) == 0);
        CHECK(imgExtractFile(&run, "AAA/DAT/MCARD/STMCRDTM.BIN", file, n, sheet, sn) == 2 && run.failed == 0);
        CHECK(imgRunClose(&run) == 0);
        txt = slurp("images/mcard/stmcrdtm/00_01.png", &tl);
        CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0);
        if (txt) {
            CHECK(p.rgb[3] == 255 && p.rgb[4] == 8 && p.rgb[5] == 0 && p.rgb[6] == 255 && p.rgb[7] == 16); /* (31,1,0), (31,2,0); the decoy is (1,31,1) */
            free(p.px);
        }
        free(txt);
        rmtree(gRoot);
    }
}

static void testWalkAndNames(void)
{
    uint8_t file[32768];
    uint16_t page[4 * 3];
    Seen seen;
    size_t n;
    char name[128];
    int chain[2] = {0, 1};
    int i;

    for (i = 0; i < 12; i++) page[i] = (uint16_t)(0x1111 * (i & 7));
    n = makeTmFile(file, page, 4, 3);
    memset(&seen, 0, sizeof seen);
    CHECK(imgWalk(file, n, onTim, &seen) == 2);
    CHECK(seen.count == 2 && seen.depths[0] == 2 && seen.chains[0][0] == 0 && seen.chains[0][1] == 0);
    CHECK(seen.chains[1][0] == 0 && seen.chains[1][1] == 1);
    CHECK(seen.lens[0] == 20 + 12 + 24 - 8 + 8 - 20 + 20 || seen.lens[0] > 0); /* unpacked: the whole TIM */
    CHECK(seen.lens[0] == 8 + 12 + 2 * 4 * 3);
    /* noise is not an archive */
    {
        uint8_t junk[256];
        for (i = 0; i < 256; i++) junk[i] = (uint8_t)(i * 37 + 11);
        memset(&seen, 0, sizeof seen);
        CHECK(imgWalk(junk, sizeof junk, onTim, &seen) == 0);
    }

    CHECK(imgIsScreenFile("AAA/DAT/SCREEN/STSHOPTM.BIN"));
    CHECK(imgIsScreenFile("AAA/DAT/SCREEN/TLOGOUSA.BIN") && imgIsScreenFile("AAA/DAT/SCREEN/TTCOMMPK.BIN"));
    CHECK(imgIsScreenFile("AAA/DAT/SCREEN/END__ALL.BIN") && imgIsScreenFile("AAA/DAT/COUNTRY/CNTSELWN.BIN"));
    CHECK(imgIsScreenFile("AAA/DAT/DGLB/STDGLBTM.BIN") && imgIsScreenFile("AAA/DAT/TRAINING/STTRNGTM.BIN"));
    CHECK(!imgIsScreenFile("AAA/DAT/SCREEN/STSHOPCS.BIN"));      /* a sheet, not an image */
    CHECK(!imgIsScreenFile("AAA/DAT/FIELD/SPRT/P066FRTM.BIN"));  /* field sprites */
    CHECK(!imgIsScreenFile("AAA/DAT/COUNTRY/ESAMTMAP.BIN"));     /* text */
    CHECK(imgImageName("AAA/DAT/SCREEN/STSHOPTM.BIN", chain, 2, name, sizeof name) == 0);
    CHECK(strcmp(name, "screen/stshoptm/00_01.png") == 0);
    CHECK(imgImageName("AAA/DAT/SCREEN/STSHOPTM.BIN", chain, 2, name, 8) != 0);
    CHECK(imgSheetPathOf("AAA/DAT/SCREEN/STSHOPTM.BIN", name, sizeof name) && strcmp(name, "AAA/DAT/SCREEN/STSHOPCS.BIN") == 0);
    CHECK(!imgSheetPathOf("AAA/DAT/SCREEN/TLOGOUSA.BIN", name, sizeof name));
}

static void testSheet(void)
{
    uint8_t sheet[512];
    const int parts[3][7] = {{0, 0, 16, 16, 0, 2, 0}, {16, 0, 16, 16, 0, 2, 0}, {256, 0, 16, 16, 16, 1, 0}};
    size_t n = makeSheet(sheet, parts, 3);
    int mode = -1, cx = -1, cy = -1;

    CHECK(imgSheetMode(sheet, n, &mode) == 0 && mode == 0);
    CHECK(imgSheetClut(sheet, n, 0, 0, &cx, &cy) == 0 && cx == 0 && cy == 2);
    CHECK(imgSheetClut(sheet, n, 1, 0, &cx, &cy) == 0 && cx == 16 && cy == 1);
    CHECK(imgSheetClut(sheet, n, 2, 0, &cx, &cy) != 0); /* no part there */
    CHECK(imgSheetMode(sheet, 6, &mode) != 0);
}

/* a sprite sheet with frames: parts, then per frame its (part, x, y) items; fr[f][0] is the item count */
static size_t makeSheetF(uint8_t *out, const int (*parts)[7], int np, const int (*fr)[1 + 3 * 4], int nf)
{
    uint8_t *s = out + 4;
    size_t po = 8 + 4 * (size_t)nf, io = po + 14 * (size_t)np, at = io + (size_t)nf;
    int i, k, f;
    w32(out, 4);
    w32(s, (uint32_t)po);
    w32(s + 4, (uint32_t)io);
    for (i = 0; i < np; i++) {
        for (k = 0; k < 7; k++) w16(s + po + 14 * (size_t)i + 2 * (size_t)k, (unsigned)parts[i][k]);
    }
    for (f = 0; f < nf; f++) s[io + (size_t)f] = (uint8_t)f;
    at = (at + 1) & ~(size_t)1;
    for (f = 0; f < nf; f++) {
        w32(s + 8 + 4 * (size_t)f, (uint32_t)at);
        w16(s + at, (unsigned)fr[f][0]);
        w16(s + at + 2, 0);
        w16(s + at + 4, 0xFFFF);
        for (k = 0; k < fr[f][0]; k++) {
            w16(s + at + 6 + 6 * (size_t)k, (unsigned)fr[f][1 + 3 * k]);
            w16(s + at + 8 + 6 * (size_t)k, (unsigned)fr[f][2 + 3 * k]);
            w16(s + at + 10 + 6 * (size_t)k, (unsigned)fr[f][3 + 3 * k]);
        }
        at += 6 + 6 * (size_t)fr[f][0];
    }
    return 4 + at;
}

static int contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

static void testAtlas(void)
{
    uint8_t sheet[1024];
    /* u, v, w, h, clutX, clutY, mode */
    const int parts[7][7] = {
        {0, 0, 16, 8, 0, 3, 0},      /* p0 */
        {16, 8, 16, 8, 16, 3, 0},    /* p1 */
        {0, 0, 16, 8, 0, 3, 0},      /* p2: the same rectangle as p0 */
        {56, 24, 16, 16, 0, 3, 0},   /* p3: runs off the right and bottom of a 64x32 image */
        {256, 0, 16, 16, 0, 3, 0},   /* p4: page 1 */
        {8, 0, 8, 4, 0, 0, 1},       /* p5: read as 8-bit texels */
        {-4, 0, 8, 8, 0, 0, 0},      /* p6: not a rectangle on any page */
    };
    const int frames[3][13] = {
        {2, 0, 10, 20, 1, 0, 0},
        {4, 2, 5, 5, 3, 1, 1, 5, 2, 2, 4, 0, 0},
        {2, 0, 1, 1, 6, 0, 0},
    };
    size_t sn = makeSheetF(sheet, parts, 7, frames, 3), n;
    char *j;

    /* 4-bit page 0, 64x32 */
    j = imgAtlasJson("SCREEN/X/00_00.png", 64, 32, 4, 0, "SCREEN/XCS.BIN", sheet, sn, &n);
    CHECK(j && n == strlen(j));
    CHECK(contains(j, "\"format\": \"atlas-v1\"") && contains(j, "\"image\": \"00_00.png\"") && contains(j, "\"width\": 64"));
    CHECK(contains(j, "\"height\": 32") && contains(j, "\"mode\": \"4bit\"") && contains(j, "\"sheet\": \"SCREEN/XCS.BIN\""));
    /* p0 and p2 are one piece, first drawn by frame 0 at (10, 20), used by frames 0, 1 and 2 */
    CHECK(contains(j, "{ \"id\": \"f0p0\", \"frame\": 0, \"part\": 0, \"x\": 0, \"y\": 0, \"w\": 16, \"h\": 8, \"palette\": \"0,3\", \"draw_at\": [10, 20], \"frames\": [0, 1, 2] }"));
    CHECK(!contains(j, "\"id\": \"f1p2\""));
    CHECK(contains(j, "{ \"id\": \"f0p1\", \"frame\": 0, \"part\": 1, \"x\": 16, \"y\": 8, \"w\": 16, \"h\": 8, \"palette\": \"16,3\", \"draw_at\": [0, 0], \"frames\": [0] }"));
    /* p3: clipped to the image (x 56..63, y 24..31) and flagged */
    CHECK(contains(j, "\"id\": \"f1p3\"") && contains(j, "\"x\": 56, \"y\": 24, \"w\": 8, \"h\": 8, \"palette\": \"0,3\", \"draw_at\": [1, 1], \"frames\": [1], \"outside\": true"));
    /* p5: 8-bit texels 8..15 are halfwords 4..7 = PNG pixels 16..31 of a 4-bit image */
    CHECK(contains(j, "\"id\": \"f1p5\"") && contains(j, "\"x\": 16, \"y\": 0, \"w\": 16, \"h\": 4, \"palette\": \"0,0\", \"draw_at\": [2, 2], \"frames\": [1], \"read_as\": \"8bit\" }"));
    CHECK(!contains(j, "\"part\": 4,") && !contains(j, "\"part\": 6,")); /* page 1 and a negative u are not on page 0 */
    CHECK(contains(j, "\"notes\":") && contains(j, "at most 16 palette colors"));
    free(j);

    /* 4-bit page 1 (u 256..511): only p4 */
    j = imgAtlasJson("SCREEN/X/00_01.png", 64, 32, 4, 1, "SCREEN/XCS.BIN", sheet, sn, NULL);
    CHECK(contains(j, "\"id\": \"f1p4\"") && contains(j, "\"x\": 0, \"y\": 0, \"w\": 16, \"h\": 16") && !contains(j, "\"part\": 0,"));
    free(j);

    /* 8-bit page 0, 128x32: p5 is native, the 4-bit parts are converted (u / 4 halfwords, two PNG pixels each) */
    j = imgAtlasJson("SCREEN/X/00_03.png", 128, 32, 8, 0, "SCREEN/XCS.BIN", sheet, sn, NULL);
    CHECK(contains(j, "\"mode\": \"8bit\"") && contains(j, "at most 256 palette colors"));
    CHECK(contains(j, "\"id\": \"f1p5\", \"frame\": 1, \"part\": 5, \"x\": 8, \"y\": 0, \"w\": 8, \"h\": 4, \"palette\": \"0,0\", \"draw_at\": [2, 2], \"frames\": [1] }"));
    CHECK(contains(j, "\"id\": \"f0p0\", \"frame\": 0, \"part\": 0, \"x\": 0, \"y\": 0, \"w\": 8, \"h\": 8,") && contains(j, "\"read_as\": \"4bit\" }"));
    free(j);

    /* no sheet, or something that is not a sheet: an empty list and the reason */
    j = imgAtlasJson("SCREEN/X/00_00.png", 64, 32, 4, 0, "", NULL, 0, NULL);
    CHECK(contains(j, "\"sheet\": null") && contains(j, "\"pieces\": [],") && contains(j, "No sprite sheet describes this image"));
    free(j);
    j = imgAtlasJson("SCREEN/X/00_00.png", 64, 32, 4, 0, "SCREEN/XCS.BIN", (const uint8_t *)"not a sheet at all, no", 22, NULL);
    CHECK(contains(j, "\"sheet\": null") && contains(j, "\"pieces\": [],"));
    free(j);
}

static void testExtract(void)
{
    uint8_t file[32768], sheet[512], *png;
    uint16_t page[16 * 4];
    const int parts[1][7] = {{0, 0, 16, 3, 0, 3, 0}};
    size_t n, sn, len;
    ImgRun run;
    Png p;
    char *txt;
    size_t tl;
    int i;

    /* a page 16 halfwords (64 px) wide and 4 rows: rows 0..2 pixels, row 3 a palette at column 0 */
    for (i = 0; i < 16 * 4; i++) page[i] = 0;
    page[0] = 0x2100; page[1] = 0x0003; page[16] = 0x1111;
    for (i = 0; i < 16; i++) page[48 + i] = (uint16_t)(i ? rgb555(31, i, 0) : 0);
    n = makeTmFile(file, page, 16, 4);
    {
        const int fr[1][13] = {{1, 0, 4, 5}};
        sn = makeSheetF(sheet, parts, 1, fr, 1);
    }

    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        fprintf(stderr, "imagetest: cannot make a scratch folder\n");
        failures++;
        return;
    }
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "aaaa", 1) == 0);
    CHECK(!run.sameRun && !imgRunCurrent(&run));
    CHECK(imgExtractFile(&run, "AAA/DAT/SCREEN/STSHOPTM.BIN", file, n, sheet, sn) == 2);
    CHECK(run.written == 4 && run.kept == 0 && run.failed == 0); /* two PNGs and their piece maps */
    CHECK(imgRunClose(&run) == 0);
    CHECK(exists("images/manifest.json") && exists("images/screen/stshoptm/00_00.png") && exists("images/screen/stshoptm/00_01.png"));
    CHECK(exists("images/screen/stshoptm/00_00.json") && exists("images/screen/stshoptm/00_01.json"));
    txt = slurp("images/screen/stshoptm/00_00.json", &tl);
    CHECK(contains(txt, "\"sheet\": \"SCREEN/STSHOPCS.BIN\"") && contains(txt, "\"id\": \"f0p0\"") && contains(txt, "\"x\": 0, \"y\": 0, \"w\": 16, \"h\": 3"));
    free(txt);
    txt = slurp("images/manifest.json", &tl);
    CHECK(contains(txt, "screen/stshoptm/00_00.json")); /* tracked like the PNGs */
    free(txt);

    txt = slurp("images/screen/stshoptm/00_00.png", &tl);
    CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0);
    if (txt) {
        /* the raw page read as 4-bit with the palette the sheet names (column 0, row 3) */
        CHECK(p.w == 64 && p.h == 4 && p.depth == 4);
        CHECK(p.px[0] == 0 && p.px[1] == 0 && p.px[2] == 1 && p.px[3] == 2 && p.px[4] == 3 && p.px[5] == 0);
        CHECK(p.rgb[3] == 255 && p.rgb[4] == (1 << 3 | 1 >> 2) && p.nAlpha >= 1 && p.alpha[0] == 0);
        free(p.px);
    }
    free(txt);
    txt = slurp("images/screen/stshoptm/00_01.png", &tl);
    CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0 && p.depth == 8 && p.w == 4 && p.px[0] == 2 && p.px[3] == 3);
    if (txt) {
        CHECK(p.rgb[3] == (1 << 3 | 1 >> 2) && p.rgb[5] == (3 << 3 | 3 >> 2));
        free(p.px);
    }
    free(txt);

    /* second run, same header: pristine files are skipped and nothing is rewritten */
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "aaaa", 1) == 0);
    CHECK(run.sameRun && imgRunCurrent(&run));
    CHECK(imgExtractFile(&run, "AAA/DAT/SCREEN/STSHOPTM.BIN", file, n, sheet, sn) == 2);
    CHECK(run.written == 0 && run.kept == 4 && run.newFiles == 0);
    CHECK(imgRunClose(&run) == 0);

    /* a file the user edited survives a new disc; the new version lands beside it as .new */
    spit("images/screen/stshoptm/00_01.png", "edited by the user", 18);
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "bbbb", 1) == 0);
    CHECK(!run.sameRun);
    CHECK(imgExtractFile(&run, "AAA/DAT/SCREEN/STSHOPTM.BIN", file, n, sheet, sn) == 2);
    CHECK(run.newFiles == 1 && run.written == 0 && run.kept == 3); /* 00_00 and both maps are already what we would write */
    CHECK(imgRunClose(&run) == 0);
    txt = slurp("images/screen/stshoptm/00_01.png", &tl);
    CHECK(txt && tl == 18 && memcmp(txt, "edited by the user", 18) == 0);
    free(txt);
    CHECK(exists("images/screen/stshoptm/00_01.png.new"));
    txt = slurp("images/screen/stshoptm/00_01.png.new", &tl);
    CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0);
    free(txt);
    if (p.px) free(p.px);

    /* a missing file makes the folder not current even with a matching header, and is rewritten */
    {
        char path[1300];
        snprintf(path, sizeof path, "%s/images/screen/stshoptm/00_00.png", gRoot);
        CHECK(remove(path) == 0);
    }
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "bbbb", 1) == 0);
    CHECK(run.sameRun && !imgRunCurrent(&run));
    CHECK(imgExtractFile(&run, "AAA/DAT/SCREEN/STSHOPTM.BIN", file, n, sheet, sn) == 2);
    CHECK(run.written == 1 && run.kept == 2 && run.newFiles == 1); /* the edited one stays edited, the maps are current */
    CHECK(imgRunClose(&run) == 0);
    CHECK(exists("images/screen/stshoptm/00_00.png"));

    /* a manifest that is not ours is rebuilt without losing the user's files */
    spit("images/manifest.json", "{ not json", 10);
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "bbbb", 1) == 0 && !run.sameRun);
    CHECK(imgExtractFile(&run, "AAA/DAT/SCREEN/STSHOPTM.BIN", file, n, sheet, sn) == 2);
    CHECK(run.newFiles == 1 && run.written == 0 && run.kept == 3); /* no manifest: identical files are ours, a different one is the user's */
    CHECK(imgRunClose(&run) == 0);

    /* a piece map the user edited is kept, the new one lands beside it; a file with no sheet gets an empty map */
    spit("images/screen/stshoptm/00_01.json", "{\"mine\": true}", 15);
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "cccc", 1) == 0);
    CHECK(imgExtractFile(&run, "AAA/DAT/SCREEN/STSHOPTM.BIN", file, n, NULL, 0) == 2);
    CHECK(run.newFiles >= 1 && exists("images/screen/stshoptm/00_01.json.new"));
    txt = slurp("images/screen/stshoptm/00_01.json", &tl);
    CHECK(txt && tl == 15 && memcmp(txt, "{\"mine\": true}", 15) == 0);
    free(txt);
    txt = slurp("images/screen/stshoptm/00_00.json", &tl);
    CHECK(contains(txt, "\"sheet\": null") && contains(txt, "\"pieces\": [],"));
    free(txt);
    CHECK(imgRunClose(&run) == 0);

    /* a PNG of the writer is what the extraction produced: same bytes through the public path */
    {
        ImgPic pic;
        uint8_t buf[256];
        uint16_t px[1] = {0x4321};
        size_t tn = makeTim(buf, 2, 0, 0, NULL, 1, 1, px);
        CHECK(imgDecodeTim(buf, tn, NULL, &pic) == 0 && imgPicToPng(&pic, &png, &len) == 0 && len > 60);
        free(png);
        imgPicFree(&pic);
    }
    rmtree(gRoot);
}


/* ---- field sprites: archive of frames + animations + set table (feature field-sprites-png T2) ---- */

/* A frame: a 4-bit TIM of iw x ih halfwords with a 16-colour palette whose colour i is (i, 0, 31 - i) */
static size_t makeFrame(uint8_t *out, int iw, int ih, uint16_t seed)
{
    uint16_t clut[16], img[64];
    int i;
    for (i = 0; i < 16; i++) clut[i] = i ? rgb555(i, 0, 31 - i) : 0;
    for (i = 0; i < iw * ih; i++) img[i] = (uint16_t)(seed + 0x4444 * (unsigned)(i & 1) + (unsigned)(i >> 1) * 0x1111);
    return makeTim(out, 0, 16, 1, clut, iw, ih, img);
}

/* animation steps (time, frame, x, y) then the end word: 0 loops, -1 plays once */
static size_t makeAnimData(uint8_t *out, const int *steps, int n, int endWord)
{
    int i;
    for (i = 0; i < 4 * n; i++) w32(out + 4 * (size_t)i, (uint32_t)steps[i]);
    w32(out + 16 * (size_t)n, (uint32_t)endWord);
    return 16 * (size_t)n + 4;
}

static size_t makeSpriteFile(uint8_t *out, int withSets)
{
    uint8_t f0[512], f1[512], a2[256], a3[256], f4[512], sets[64];
    const uint8_t *ents[6];
    size_t lens[6];
    const int loop[8] = {5, 0, -4, -10, 7, 1, -3, -9};
    const int once[4] = {3, 1, -2, -8};
    int i;

    lens[0] = makeFrame(f0, 2, 3, 0x3210);
    lens[1] = makeFrame(f1, 1, 2, 0x0101);
    lens[2] = makeAnimData(a2, loop, 2, 0);
    lens[3] = makeAnimData(a3, once, 1, -1);
    lens[4] = makeFrame(f4, 1, 1, 0x0002);
    memset(sets, 0, sizeof sets);
    if (withSets) {
        const int recs[2][6] = {{1, 2, 2, 3, 3, 3}, {4, 3, 3, 3, 3, 3}};
        for (i = 0; i < 12; i++) w16(sets + 2 * i, (unsigned)recs[i / 6][i % 6]);
        lens[5] = 36; /* two records and the zero one that ends the table */
    } else {
        memset(sets, 0xA5, 20);
        lens[5] = 20;
    }
    ents[0] = f0; ents[1] = f1; ents[2] = a2; ents[3] = a3; ents[4] = f4; ents[5] = sets;
    return makeArchive(out, ents, lens, 6, 0);
}

/* the exporter's way of writing a sprite (sprite_export.c, covered by sprexptest): frames and sprite.json through the manifest rules into sprites/<name>/ of the data folder */
typedef struct SprWrite {
    const ExpSink *sink;
    char folder[40];
    int failed;
} SprWrite;

static int sprWriteEmit(void *ctx, const char *rel, const uint8_t *data, size_t len)
{
    SprWrite *w = (SprWrite *)ctx;
    char path[120];
    snprintf(path, sizeof path, "%s/%s", w->folder, rel);
    if (expPut(w->sink, path, data, len)) w->failed++;
    return 0;
}

/* writes the sprite of `discPath` under <dataDir>/sprites/ with its manifest; returns the number of PNGs */
static int writeSprite(const char *dataDir, const char *discPath, const uint8_t *file, size_t len, SprStats *st)
{
    ExpManifest man;
    ExpStats es;
    ExpSink sink;
    SprWrite w;
    char dir[1200], mp[1300];
    int n;

    snprintf(dir, sizeof dir, "%s/sprites", dataDir);
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    expManOpen(&man, "test", mp, "sprites");
    memset(&es, 0, sizeof es);
    expSinkInPlace(&sink, "test", dir, &man, &es);
    memset(&w, 0, sizeof w);
    w.sink = &sink;
    CHECK(sprFolder(discPath, w.folder, sizeof w.folder) == 0);
    n = sprExtractAs("sprites-sprite-v1", sprWriteEmit, &w, discPath, file, len, st);
    CHECK(w.failed == 0 && expManSave(&man, 0) == 0);
    expManFree(&man);
    return n;
}

static void testSprites(void)
{
    uint8_t file[4096];
    size_t n, tl, jl;
    SprStats st;
    Png p;
    char *txt, name[160];
    char *json;
    int i;

    CHECK(sprIsSpriteFile("AAA/DAT/FIELD/SPRT/P001PLAY.BIN"));
    CHECK(!sprIsSpriteFile("AAA/DAT/FIELD/FIELDCOM.BIN") && !sprIsSpriteFile("AAA/DAT/FIELD/SPRT/P001PLAY.TXT") &&
          !sprIsSpriteFile("AAA/DAT/FIELD/SPRT/X/P001PLAY.BIN") && !sprIsSpriteFile("AAA/DAT/SCREEN/STSHOPTM.BIN"));
    CHECK(sprName("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", 4, NULL, name, sizeof name) == 0 && strcmp(name, "sprites/p001play/004.png") == 0);
    CHECK(sprName("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", 0, "sprite.json", name, sizeof name) == 0 && strcmp(name, "sprites/p001play/sprite.json") == 0);
    CHECK(sprName("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", 4, NULL, name, 8) != 0 && sprName("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", -1, NULL, name, sizeof name) != 0);
    CHECK(!imgIsScreenFile("AAA/DAT/FIELD/SPRT/P001PLAY.BIN"));

    n = makeSpriteFile(file, 1);
    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        fprintf(stderr, "imagetest: cannot make a scratch folder\n");
        failures++;
        return;
    }
    memset(&st, 0, sizeof st);
    CHECK(writeSprite(gRoot, "AAA/DAT/FIELD/SPRT/P001PLAY.BIN", file, n, &st) == 3);
    CHECK(st.files == 1 && st.frames == 3 && st.unused == 1 && st.anims == 2 && st.sets == 2 && st.failed == 0);
    CHECK(exists("sprites/p001play/000.png") && exists("sprites/p001play/001.png") &&
          exists("sprites/p001play/004.png") && exists("sprites/p001play/sprite.json"));
    CHECK(!exists("sprites/p001play/002.png") && !exists("sprites/p001play/005.png")); /* animations and the set table are not images */
    CHECK(!exists("images/field/sprt/p001play/000.png"));

    txt = slurp("sprites/p001play/000.png", &tl);
    CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0);
    if (txt) {
        CHECK(p.w == 8 && p.h == 3 && p.depth == 4);
        for (i = 0; i < 4; i++) CHECK(p.px[i] == i); /* 0x3210: index 0, 1, 2, 3 */
        for (i = 4; i < 8; i++) CHECK(p.px[i] == i); /* 0x7654 */
        CHECK(p.alpha[0] == 0 && p.rgb[3 * 5] == (5 << 3 | 5 >> 2) && p.rgb[3 * 5 + 2] == (26 << 3 | 26 >> 2)); /* its own palette */
        free(p.px);
    }
    free(txt);
    txt = slurp("sprites/p001play/004.png", &tl);
    CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0 && p.w == 4 && p.h == 1);
    if (txt) free(p.px);
    free(txt);

    txt = slurp("sprites/p001play/sprite.json", &tl);
    CHECK(contains(txt, "\"format\": \"sprites-sprite-v1\"") && contains(txt, "\"source\": \"DAT/FIELD/SPRT/P001PLAY.BIN\"") == 0);
    CHECK(contains(txt, "\"source\": \"FIELD/SPRT/P001PLAY.BIN\"") && contains(txt, "\"set_table\": 5"));
    CHECK(contains(txt, "{ \"id\": 0, \"image\": \"000.png\", \"w\": 8, \"h\": 3 }"));
    CHECK(contains(txt, "{ \"id\": 1, \"image\": \"001.png\", \"w\": 4, \"h\": 2 }"));
    CHECK(contains(txt, "{ \"id\": 4, \"image\": \"004.png\", \"w\": 4, \"h\": 1, \"used\": false }"));
    CHECK(contains(txt, "{ \"id\": 2, \"loop\": true, \"ticks\": 12, \"steps\": [{ \"frame\": 0, \"time\": 5, \"x\": -4, \"y\": -10 }, "
                        "{ \"frame\": 1, \"time\": 7, \"x\": -3, \"y\": -9 }] }"));
    CHECK(contains(txt, "{ \"id\": 3, \"loop\": false, \"ticks\": 3, \"steps\": [{ \"frame\": 1, \"time\": 3, \"x\": -2, \"y\": -8 }] }"));
    CHECK(contains(txt, "{ \"id\": 1, \"name\": \"STAND\", \"animations\": [2, 2, 3, 3, 3] }"));
    CHECK(contains(txt, "{ \"id\": 4, \"name\": \"WALK\", \"animations\": [3, 3, 3, 3, 3] }"));
    CHECK(contains(txt, "{ \"dir\": 5, \"slot\": 3, \"flip\": true") && contains(txt, "{ \"dir\": 0, \"slot\": 0, \"flip\": false"));
    free(txt);
    txt = slurp("sprites/manifest.json", &tl);
    CHECK(contains(txt, "p001play/sprite.json") && contains(txt, "p001play/004.png") && !contains(txt, "field/sprt"));
    free(txt);

    /* the same text through sprJson (the partners' "sprite-v1"), without writing */
    memset(&st, 0, sizeof st);
    json = sprJson("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", file, n, &jl, &st);
    CHECK(json && jl == strlen(json) && st.frames == 3 && contains(json, "\"format\": \"sprite-v1\""));
    free(json);

    /* no set table in the last entry: every TIM is a frame no animation uses, and the map says so */
    n = makeSpriteFile(file, 0);
    memset(&st, 0, sizeof st);
    json = sprJson("AAA/DAT/FIELD/SPRT/P002PLAY.BIN", file, n, &jl, &st);
    CHECK(json && st.frames == 3 && st.unused == 3 && st.anims == 0 && st.sets == 0 && st.failed == 0);
    CHECK(contains(json, "\"set_table\": -1") && contains(json, "\"animations\": [],"));
    free(json);

    /* an animation without an end marker is reported, its steps kept */
    {
        uint8_t bad[1024];
        const uint8_t *ents[3];
        size_t lens[3];
        uint8_t f[512], a[16], sets[24];
        lens[0] = makeFrame(f, 1, 1, 0);
        w32(a, 5); w32(a + 4, 0); w32(a + 8, 0); w32(a + 12, 0);
        lens[1] = 16;
        memset(sets, 0, sizeof sets);
        for (i = 0; i < 6; i++) w16(sets + 2 * i, (unsigned)(i ? 1 : 9));
        lens[2] = 24;
        ents[0] = f; ents[1] = a; ents[2] = sets;
        n = makeArchive(bad, ents, lens, 3, 0);
        memset(&st, 0, sizeof st);
        json = sprJson("AAA/DAT/FIELD/SPRT/P003BAD0.BIN", bad, n, &jl, &st);
        CHECK(json && st.failed == 1 && contains(json, "\"error\": \"no end marker\"") && contains(json, "{ \"id\": 9, \"animations\": [1, 1, 1, 1, 1] }"));
        free(json);
    }
    /* not an archive at all */
    {
        uint8_t junk[64];
        for (i = 0; i < 64; i++) junk[i] = (uint8_t)(i * 37 + 11);
        memset(&st, 0, sizeof st);
        CHECK(sprJson("AAA/DAT/FIELD/SPRT/P004JUNK.BIN", junk, sizeof junk, &jl, &st) == NULL && st.failed == 1 && st.files == 0);
    }
    rmtree(gRoot);
}

/* FIELDCOM.BIN: entries 0 and 1 are the sheets of the images in the archives of entries 2 and 3; returns its size */
static size_t makeFieldComFile(uint8_t *file)
{
    uint8_t sh0[512], sh1[512], page[512], in2[1024], in3[1024];
    uint16_t pg[16 * 4];
    const int parts0[1][7] = {{0, 0, 16, 3, 0, 3, 0}};
    const int parts1[1][7] = {{16, 0, 16, 3, 0, 3, 0}};
    const int fr[1][13] = {{1, 0, 4, 5}};
    const uint8_t *ents[4], *inner[1];
    size_t lens[4], il[1], s0, s1, pn;
    int i;

    for (i = 0; i < 16 * 4; i++) pg[i] = 0;
    pg[0] = 0x2100;
    for (i = 0; i < 16; i++) pg[48 + i] = (uint16_t)(i ? rgb555(31, i, 0) : 0);
    s0 = makeSheetF(sh0, parts0, 1, fr, 1);
    s1 = makeSheetF(sh1, parts1, 1, fr, 1);
    pn = makeTim(page, 2, 0, 0, NULL, 16, 4, pg);
    inner[0] = page; il[0] = pn;
    lens[2] = makeArchive(in2, inner, il, 1, 1);
    lens[3] = makeArchive(in3, inner, il, 1, 1);
    ents[0] = sh0 + 4; lens[0] = s0 - 4; /* the sheets start at their own first word, without the sheet-file wrapper */
    ents[1] = sh1 + 4; lens[1] = s1 - 4;
    ents[2] = in2; ents[3] = in3;
    return makeArchive(file, ents, lens, 4, 0);
}

static void testFieldCom(void)
{
    uint8_t file[8192];
    size_t n, tl;
    ImgRun run;
    Png p;
    char *txt;

    n = makeFieldComFile(file);

    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        fprintf(stderr, "imagetest: cannot make a scratch folder\n");
        failures++;
        return;
    }
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "aaaa", 3) == 0);
    CHECK(imgExtractFile(&run, "AAA/DAT/FIELD/FIELDCOM.BIN", file, n, NULL, 0) == 2);
    CHECK(run.written == 4 && run.failed == 0);
    CHECK(imgRunClose(&run) == 0);
    CHECK(exists("images/field/fieldcom/02_00.png") && exists("images/field/fieldcom/03_00.png"));
    txt = slurp("images/field/fieldcom/02_00.json", &tl);
    CHECK(contains(txt, "\"sheet\": \"FIELD/FIELDCOM.BIN (entry 0)\"") && contains(txt, "\"x\": 0, \"y\": 0, \"w\": 16, \"h\": 3"));
    free(txt);
    txt = slurp("images/field/fieldcom/03_00.json", &tl);
    CHECK(contains(txt, "\"sheet\": \"FIELD/FIELDCOM.BIN (entry 1)\"") && contains(txt, "\"x\": 16, \"y\": 0, \"w\": 16, \"h\": 3"));
    free(txt);
    txt = slurp("images/field/fieldcom/03_00.png", &tl);
    CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0 && p.w == 64 && p.h == 4 && p.depth == 4);
    if (txt) {
        CHECK(p.rgb[3] == 255 && p.rgb[4] == (1 << 3 | 1 >> 2)); /* the palette the sheet names (column 0, row 3) */
        free(p.px);
    }
    free(txt);
    rmtree(gRoot);
}

/* ---- T3: reading PNGs, substituting images, mod order ---- */

/* PNGs made by Python's zlib at level 9 (real deflate): kPng8 is 40x30, 8-bit, filters 0..4 by row, one
   dynamic-Huffman block, 11 palette entries (i*17, i*29, i*43 mod 256), tRNS [0]; pixel ((x*x+3*y)>>2)%11.
   kPng4 is 33x5, 4-bit, a fixed-Huffman block, 16 entries, tRNS [0,0]; pixel (x*7+y*3)%16. */
static const uint8_t kPng8[447] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x1E, 0x08, 0x03, 0x00, 0x00, 0x00, 0x69, 0x03, 0xAC,
    0xEF, 0x00, 0x00, 0x00, 0x21, 0x50, 0x4C, 0x54, 0x45, 0x00, 0x00, 0x00, 0x11, 0x1D, 0x2B, 0x22,
    0x3A, 0x56, 0x33, 0x57, 0x81, 0x44, 0x74, 0xAC, 0x55, 0x91, 0xD7, 0x66, 0xAE, 0x02, 0x77, 0xCB,
    0x2D, 0x88, 0xE8, 0x58, 0x99, 0x05, 0x83, 0xAA, 0x22, 0xAE, 0x4C, 0xDD, 0x06, 0x28, 0x00, 0x00,
    0x00, 0x01, 0x74, 0x52, 0x4E, 0x53, 0x00, 0x40, 0xE6, 0xD8, 0x66, 0x00, 0x00, 0x01, 0x4C, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xDA, 0xB5, 0x93, 0x49, 0x8E, 0xC4, 0x20, 0x0C, 0x45, 0x3D, 0x63, 0xB8,
    0xFF, 0x59, 0xD3, 0x99, 0x80, 0x86, 0x4A, 0x0D, 0x9D, 0x50, 0x8B, 0x6C, 0x1A, 0x84, 0x84, 0x9E,
    0xC0, 0xFC, 0x8F, 0x6D, 0x00, 0x40, 0x12, 0x8B, 0xA8, 0x91, 0x9D, 0xA3, 0x62, 0x34, 0x21, 0x84,
    0x91, 0x22, 0x20, 0x10, 0x32, 0x2D, 0xAC, 0x8B, 0xAD, 0x61, 0xCB, 0xA1, 0xE4, 0x5A, 0xA0, 0x0E,
    0x94, 0xFA, 0xC9, 0x3B, 0x93, 0x09, 0x11, 0x89, 0x7E, 0x88, 0x67, 0xE6, 0xB2, 0x48, 0x5D, 0x15,
    0xFA, 0xC3, 0x03, 0x15, 0x7C, 0xC0, 0xC7, 0x98, 0xEC, 0xB9, 0x79, 0x1D, 0xFD, 0x4B, 0x81, 0x45,
    0x2C, 0x24, 0x54, 0x27, 0x43, 0x23, 0x57, 0x4C, 0xC1, 0x44, 0x46, 0x8A, 0x02, 0xFD, 0x91, 0x99,
    0x65, 0xD1, 0xD5, 0xB6, 0xB0, 0x67, 0x2F, 0xA5, 0x56, 0x18, 0x28, 0x1D, 0xE1, 0xF1, 0x33, 0xA6,
    0x2F, 0xBB, 0xB6, 0x67, 0xE9, 0xB2, 0xF7, 0x7E, 0x9B, 0x8B, 0x74, 0xDD, 0x55, 0x5F, 0x66, 0x4E,
    0x54, 0xBE, 0xFD, 0x45, 0x6D, 0x73, 0xA0, 0x21, 0x78, 0x04, 0x52, 0x47, 0x4D, 0x92, 0x14, 0x5D,
    0x09, 0xA2, 0x87, 0x91, 0xA2, 0xB7, 0x67, 0xDA, 0x7F, 0xF1, 0x22, 0xAB, 0x6E, 0xB6, 0x87, 0x9C,
    0x4B, 0x3C, 0xCC, 0x9C, 0x29, 0x5D, 0x44, 0x7F, 0x4C, 0x5C, 0x28, 0x2B, 0xC2, 0x8E, 0x84, 0x33,
    0x71, 0x96, 0x2C, 0x65, 0x15, 0xA8, 0xA0, 0x5D, 0xD3, 0x85, 0xB6, 0xCC, 0xB4, 0xAB, 0x17, 0x2F,
    0x53, 0x1A, 0x29, 0xDC, 0x2E, 0x33, 0x7C, 0xE4, 0x80, 0x67, 0x39, 0x64, 0xEF, 0x9E, 0x3F, 0x99,
    0xF9, 0x4B, 0xE9, 0x1D, 0x7B, 0x7A, 0xAE, 0xF3, 0x7C, 0x53, 0xA6, 0x7E, 0xB7, 0x7F, 0x45, 0x6E,
    0x49, 0x28, 0x4F, 0x37, 0x80, 0x03, 0xBD, 0x5F, 0x66, 0x22, 0x6A, 0x9E, 0x48, 0x23, 0x05, 0x0C,
    0x14, 0x95, 0x92, 0x9B, 0xCA, 0x48, 0x51, 0x7A, 0x77, 0xCC, 0x24, 0xBC, 0xCA, 0xA6, 0xBB, 0xE5,
    0xBD, 0xF8, 0xAB, 0x67, 0x4E, 0xF4, 0x7E, 0xCF, 0xDC, 0x2E, 0xB3, 0x6E, 0x66, 0xA2, 0x53, 0xAA,
    0xE3, 0x61, 0xE6, 0x4C, 0x21, 0xB8, 0x27, 0x60, 0x8D, 0x68, 0x49, 0x93, 0x61, 0x54, 0x86, 0xE4,
    0x3E, 0xD2, 0x7F, 0x28, 0x33, 0x6B, 0x84, 0x5A, 0xAF, 0x37, 0xD5, 0x4D, 0x77, 0x5D, 0xB4, 0x45,
    0xDB, 0x0C, 0x07, 0x2A, 0x5F, 0xF2, 0xDB, 0xEB, 0x6C, 0xA0, 0xBF, 0x0F, 0x3D, 0xD8, 0x7A, 0x03,
    0xAD, 0xF4, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};
static const uint8_t kPng4[195] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x05, 0x04, 0x03, 0x00, 0x00, 0x00, 0x39, 0xF7, 0x98,
    0x7C, 0x00, 0x00, 0x00, 0x30, 0x50, 0x4C, 0x54, 0x45, 0x00, 0x00, 0x00, 0x11, 0x1D, 0x2B, 0x22,
    0x3A, 0x56, 0x33, 0x57, 0x81, 0x44, 0x74, 0xAC, 0x55, 0x91, 0xD7, 0x66, 0xAE, 0x02, 0x77, 0xCB,
    0x2D, 0x88, 0xE8, 0x58, 0x99, 0x05, 0x83, 0xAA, 0x22, 0xAE, 0xBB, 0x3F, 0xD9, 0xCC, 0x5C, 0x04,
    0xDD, 0x79, 0x2F, 0xEE, 0x96, 0x5A, 0xFF, 0xB3, 0x85, 0x47, 0x42, 0xCB, 0x6C, 0x00, 0x00, 0x00,
    0x02, 0x74, 0x52, 0x4E, 0x53, 0x00, 0x00, 0x76, 0x93, 0xCD, 0x38, 0x00, 0x00, 0x00, 0x40, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x60, 0x7F, 0x7A, 0x78, 0x61, 0x7F, 0xAE, 0xB7, 0x26, 0x8C,
    0x66, 0x60, 0xB4, 0xBA, 0x07, 0x02, 0xEF, 0xEE, 0xDD, 0x83, 0xD2, 0x57, 0x98, 0x8C, 0xC1, 0x40,
    0xD9, 0x18, 0x46, 0x1B, 0x30, 0x47, 0x09, 0x72, 0x72, 0x76, 0x76, 0x72, 0x32, 0x32, 0x42, 0x69,
    0x76, 0x16, 0xE3, 0x7B, 0x82, 0x82, 0x82, 0xC6, 0xF7, 0xEE, 0x09, 0x42, 0x69, 0x3E, 0x00, 0xFA,
    0x06, 0x22, 0x2D, 0xF6, 0xEC, 0xDE, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82,
};

static void testPngRead(void)
{
    PngImage p;
    char why[160];
    uint8_t bad[sizeof kPng8];
    int x, y, ok;

    CHECK(pngReadIndexed(kPng8, sizeof kPng8, &p, why, sizeof why) == 0);
    CHECK(p.w == 40 && p.h == 30 && p.depth == 8 && p.nPal == 11 && p.nAlpha == 1 && p.alpha[0] == 0);
    CHECK(p.rgb[3 * 3] == 51 && p.rgb[3 * 3 + 1] == 87 && p.rgb[3 * 3 + 2] == 129);
    for (ok = 1, y = 0; y < 30; y++) {
        for (x = 0; x < 40; x++) ok &= p.px[y * 40 + x] == ((x * x + 3 * y) >> 2) % 11;
    }
    CHECK(ok);
    pngImageFree(&p);
    CHECK(pngReadIndexed(kPng4, sizeof kPng4, &p, why, sizeof why) == 0);
    CHECK(p.w == 33 && p.h == 5 && p.depth == 4 && p.nPal == 16 && p.nAlpha == 2);
    for (ok = 1, y = 0; y < 5; y++) {
        for (x = 0; x < 33; x++) ok &= p.px[y * 33 + x] == (x * 7 + y * 3) % 16;
    }
    CHECK(ok);
    pngImageFree(&p);

    /* corruption: a flipped byte in the compressed data (CRC), a cut file, a wrong signature */
    memcpy(bad, kPng8, sizeof bad);
    bad[100] ^= 0x40;
    CHECK(pngReadIndexed(bad, sizeof bad, &p, why, sizeof why) != 0 && strstr(why, "CRC"));
    CHECK(pngReadIndexed(kPng8, sizeof kPng8 - 20, &p, why, sizeof why) != 0);
    memcpy(bad, kPng8, sizeof bad);
    bad[1] = 'X';
    CHECK(pngReadIndexed(bad, sizeof bad, &p, why, sizeof why) != 0 && strstr(why, "not a PNG"));
    CHECK(pngReadIndexed(kPng8, 5, &p, why, sizeof why) != 0);
    /* an inflate stream that points before its start / overruns the output */
    {
        uint8_t out[8];
        size_t got = 0;
        static const uint8_t back[] = {0x63, 0x00, 0x03, 0x00}; /* fixed block: literal 0 then a match with no history */
        CHECK(pngInflate(kPng8 + 100, 3, out, sizeof out, &got) != 0 || got <= sizeof out);
        CHECK(pngInflate(back, sizeof back, out, sizeof out, &got) != 0 || got <= sizeof out);
    }
}

/* A PNG with a stored-deflate image; raw holds the filter bytes and the scanlines */
static size_t mkPng(uint8_t *out, int w, int h, int depth, int ctype, int interlace, const uint8_t *plte, int nPal,
                    const uint8_t *trns, int nTrns, const uint8_t *raw, size_t rawLen)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    size_t at = 8, i;
    uint8_t z[70000], hdr[13];
    size_t zn = 0;
    uint32_t a1 = 1, a2 = 0;
    struct {
        const char *t;
        const uint8_t *d;
        size_t n;
    } ch[5];
    int nc = 0, k;
    for (i = 0; i < rawLen; i++) {
        a1 = (a1 + raw[i]) % 65521u;
        a2 = (a2 + a1) % 65521u;
    }
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    z[zn++] = 1;
    z[zn++] = (uint8_t)rawLen;
    z[zn++] = (uint8_t)(rawLen >> 8);
    z[zn++] = (uint8_t)~rawLen;
    z[zn++] = (uint8_t)(~rawLen >> 8);
    memcpy(z + zn, raw, rawLen);
    zn += rawLen;
    z[zn++] = (uint8_t)(a2 >> 8);
    z[zn++] = (uint8_t)a2;
    z[zn++] = (uint8_t)(a1 >> 8);
    z[zn++] = (uint8_t)a1;
    hdr[0] = 0; hdr[1] = 0; hdr[2] = (uint8_t)(w >> 8); hdr[3] = (uint8_t)w;
    hdr[4] = 0; hdr[5] = 0; hdr[6] = (uint8_t)(h >> 8); hdr[7] = (uint8_t)h;
    hdr[8] = (uint8_t)depth; hdr[9] = (uint8_t)ctype; hdr[10] = 0; hdr[11] = 0; hdr[12] = (uint8_t)interlace;
    memcpy(out, sig, 8);
    ch[nc].t = "IHDR"; ch[nc].d = hdr; ch[nc++].n = 13;
    if (nPal) { ch[nc].t = "PLTE"; ch[nc].d = plte; ch[nc++].n = (size_t)nPal * 3; }
    if (nTrns) { ch[nc].t = "tRNS"; ch[nc].d = trns; ch[nc++].n = (size_t)nTrns; }
    ch[nc].t = "IDAT"; ch[nc].d = z; ch[nc++].n = zn;
    ch[nc].t = "IEND"; ch[nc].d = z; ch[nc++].n = 0;
    for (k = 0; k < nc; k++) {
        uint8_t tmp[70100];
        uint32_t c;
        out[at] = (uint8_t)(ch[k].n >> 24); out[at + 1] = (uint8_t)(ch[k].n >> 16);
        out[at + 2] = (uint8_t)(ch[k].n >> 8); out[at + 3] = (uint8_t)ch[k].n;
        memcpy(tmp, ch[k].t, 4);
        memcpy(tmp + 4, ch[k].d, ch[k].n);
        memcpy(out + at + 4, tmp, 4 + ch[k].n);
        c = crc32of(tmp, 4 + ch[k].n);
        at += 8 + ch[k].n;
        out[at] = (uint8_t)(c >> 24); out[at + 1] = (uint8_t)(c >> 16); out[at + 2] = (uint8_t)(c >> 8); out[at + 3] = (uint8_t)c;
        at += 4;
    }
    return at;
}

/* The page of the substitution tests: 16 halfwords (64 px) by 4 rows; row 3 holds a palette at column 0 */
#define PG_W 16
#define PG_H 4
static void makePage(uint16_t *page)
{
    int i;
    memset(page, 0, sizeof(uint16_t) * PG_W * PG_H);
    page[0] = 0x2100;
    page[1] = 0x0003;
    page[PG_W] = 0x1111;
    for (i = 0; i < 16; i++) page[3 * PG_W + i] = (uint16_t)(i ? rgb555(31, i, 0) : 0);
    page[3 * PG_W + 4] = (uint16_t)(0x8000 | rgb555(10, 0, 0));
}

static void pageHints(ImgHints *h, uint16_t *pal4, const uint16_t *page)
{
    int i;
    memset(h, 0, sizeof *h);
    for (i = 0; i < 16; i++) pal4[i] = page[3 * PG_W + i];
    h->pixelBits = 4;
    h->pal = pal4;
    h->nPal = 16;
    h->palAt = 1;
    h->palX = 0;
    h->palY = 3;
}

/* the PNG of a decoded picture with a few changes, through the writer */
static size_t picPng(const ImgPic *p, uint8_t **png)
{
    size_t n;
    CHECK(imgPicToPng(p, png, &n) == 0);
    return n;
}

static void testSubstitute(void)
{
    uint8_t tim[1024], *out = NULL, *png, buf[70000];
    uint16_t page[PG_W * PG_H], pal4[16];
    ImgHints h;
    ImgPic pic;
    char why[200];
    size_t n, pn;
    int i;

    makePage(page);
    pageHints(&h, pal4, page);
    n = makeTim(tim, 2, 0, 0, NULL, PG_W, PG_H, page);
    CHECK(imgDecodeTim(tim, n, &h, &pic) == 0);

    /* round trip: the PNG of the extraction put back gives the very same TIM */
    pn = picPng(&pic, &png);
    CHECK(imgSubstitute(tim, n, &h, png, pn, &out, why, sizeof why) == 0 && out && memcmp(out, tim, n) == 0);
    free(out);
    free(png);

    /* a pixel index edit: pixel 2 of row 0 goes from 1 to 5, nothing else moves */
    pic.px[2] = 5;
    pn = picPng(&pic, &png);
    CHECK(imgSubstitute(tim, n, &h, png, pn, &out, why, sizeof why) == 0 && out);
    if (out) {
        uint8_t want[1024];
        memcpy(want, tim, n);
        w16(want + 20, 0x2500);
        CHECK(memcmp(out, want, n) == 0);
    }
    free(out);
    free(png);
    pic.px[2] = 1;

    /* a palette edit: colour 1 recoloured, 2 made transparent, 3 opaque black, 4 recoloured keeping its STP */
    pic.rgb[3] = 8; pic.rgb[4] = 16; pic.rgb[5] = 24;
    pic.alpha[2] = 0;
    pic.rgb[9] = pic.rgb[10] = pic.rgb[11] = 0;
    pic.rgb[12] = 248; pic.rgb[13] = 0; pic.rgb[14] = 0;
    pn = picPng(&pic, &png);
    CHECK(imgSubstitute(tim, n, &h, png, pn, &out, why, sizeof why) == 0 && out);
    if (out) {
        const uint8_t *row3 = out + 20 + 2 * 3 * PG_W;
        CHECK(row3[2] == (0x0C41 & 255) && row3[3] == (0x0C41 >> 8));  /* 1 | 2<<5 | 3<<10 */
        CHECK(row3[4] == 0 && row3[5] == 0);                           /* transparent */
        CHECK(row3[6] == 0 && row3[7] == 0x80);                        /* opaque black: 0x8000 */
        CHECK(row3[8] == 31 && row3[9] == 0x80);                       /* 31 with the STP the disc entry had */
        CHECK(memcmp(out + 20, tim + 20, 2 * 3 * PG_W) == 0);         /* the pixels did not change */
        CHECK(row3[10] == (rgb555(31, 5, 0) & 255));                   /* the other entries stay */
    }
    free(out);
    free(png);
    imgPicFree(&pic);

    /* refusals, each with its reason */
    {
        uint8_t px[64 * 3], pl[17 * 3];
        int k;
        memset(px, 0, sizeof px);
        for (k = 0; k < 17 * 3; k++) pl[k] = (uint8_t)k;
        CHECK(pngWriteIndexed(px, 63, 4, 4, pl, 16, NULL, 0, &png, &pn) == 0);
        CHECK(imgSubstitute(tim, n, &h, png, pn, &out, why, sizeof why) != 0 && strstr(why, "63x4") && strstr(why, "64x4") && !out);
        free(png);
        CHECK(pngWriteIndexed(px, 64, 4, 8, pl, 17, NULL, 0, &png, &pn) == 0);
        CHECK(imgSubstitute(tim, n, &h, png, pn, &out, why, sizeof why) != 0 && strstr(why, "17 colors"));
        free(png);
        px[5] = 16;
        CHECK(pngWriteIndexed(px, 64, 4, 8, pl, 16, NULL, 0, &png, &pn) == 0);
        CHECK(imgSubstitute(tim, n, &h, png, pn, &out, why, sizeof why) != 0 && strstr(why, "index 16"));
        free(png);
        /* a depth-8 PNG with 16 colours is fine for a 4-bit page */
        px[5] = 2;
        CHECK(pngWriteIndexed(px, 64, 4, 8, pl, 16, NULL, 0, &png, &pn) == 0);
        CHECK(imgSubstitute(tim, n, &h, png, pn, &out, why, sizeof why) == 0 && out);
        free(out);
        free(png);
    }
    {
        uint8_t raw[1 + 64 * 3], pl[3] = {0, 0, 0};
        memset(raw, 0, sizeof raw);
        pn = mkPng(buf, 64, 1, 8, 2, 0, NULL, 0, NULL, 0, raw, sizeof raw);
        CHECK(imgSubstitute(tim, n, &h, buf, pn, &out, why, sizeof why) != 0 && strstr(why, "truecolor"));
        memset(raw, 0, sizeof raw);
        pn = mkPng(buf, 64, 4, 8, 3, 1, pl, 1, NULL, 0, raw, 4 * 65);
        CHECK(imgSubstitute(tim, n, &h, buf, pn, &out, why, sizeof why) != 0 && strstr(why, "interlaced"));
        buf[40] ^= 1;
        CHECK(imgSubstitute(tim, n, &h, buf, pn, &out, why, sizeof why) != 0);
        CHECK(imgSubstitute(tim, n, &h, (const uint8_t *)"junk", 4, &out, why, sizeof why) != 0 && !out);
    }

    /* a plain 8-bit TIM: its CLUT row 0 is the palette, the image holds two halfwords of indices */
    {
        uint16_t c8[256], pix[2] = {0x0102, 0x0304};
        uint8_t t8[2048], px[4] = {9, 5, 5, 1}, pl[256 * 3];
        ImgHints h8;
        memset(c8, 0, sizeof c8);
        c8[1] = rgb555(1, 2, 3);
        n = makeTim(t8, 1, 256, 1, c8, 2, 1, pix);
        memset(&h8, 0, sizeof h8);
        for (i = 0; i < 256 * 3; i++) pl[i] = (uint8_t)(i % 251);
        pl[3] = 248; pl[4] = 8; pl[5] = 0;  /* colour 1 -> r 31, g 1, b 0 */
        CHECK(pngWriteIndexed(px, 4, 1, 8, pl, 256, NULL, 0, &png, &pn) == 0);
        CHECK(imgSubstitute(t8, n, &h8, png, pn, &out, why, sizeof why) == 0 && out);
        if (out) {
            CHECK(out[8 + 12 + 2] == (rgb555(31, 1, 0) & 255) && out[8 + 12 + 3] == (rgb555(31, 1, 0) >> 8));
            CHECK(out[8 + 12 + 512 + 12 + 0] == 9 && out[8 + 12 + 512 + 12 + 1] == 5 && out[8 + 12 + 512 + 12 + 3] == 1);
        }
        free(out);
        free(png);
    }
    /* a raw 8-bit page borrows its palette from another TIM: the indices change, the PLTE is ignored */
    {
        uint16_t pg[4] = {0x0102, 0x0304, 0, 0}, pal8[256];
        uint8_t t[256], px[4] = {7, 7, 3, 4}, pl[3 * 256];
        ImgHints h8;
        memset(pal8, 0, sizeof pal8);
        memset(&h8, 0, sizeof h8);
        h8.pixelBits = 8;
        h8.pal = pal8;
        h8.nPal = 256;
        memset(pl, 77, sizeof pl);
        n = makeTim(t, 2, 0, 0, NULL, 2, 1, pg);
        CHECK(pngWriteIndexed(px, 4, 1, 8, pl, 256, NULL, 0, &png, &pn) == 0);
        CHECK(imgSubstitute(t, n, &h8, png, pn, &out, why, sizeof why) == 0 && out);
        if (out) CHECK(out[20] == 7 && out[21] == 7 && out[22] == 3 && out[23] == 4 && memcmp(out, t, 20) == 0);
        free(out);
        free(png);
    }
}

static void touchMod(const char *name)
{
    char rel[200];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    spit(rel, "{\n  \"format\": \"mod-v1\",\n  \"name\": \"t\",\n  \"version\": \"1.0.0\"\n}\n", 61);
}

static void putMadRaw(const char *rel, const ImgPic *pic, int idx, int val)
{
    ImgPic c = *pic;
    uint8_t *png, *px = (uint8_t *)malloc((size_t)c.w * c.h);
    size_t pn;
    char path[1400], dir[1400], *sl;
    memcpy(px, pic->px, (size_t)c.w * c.h);
    px[idx] = (uint8_t)val;
    c.px = px;
    CHECK(imgPicToPng(&c, &png, &pn) == 0);
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "%s", path);
    sl = strrchr(dir, '/');
    *sl = 0;
    CHECK(tdMakeDirs(dir) == 0);
    CHECK(tdWriteFileAtomic(path, png, pn) == 0);
    free(png);
    free(px);
}

/* writes a source and tells the index (the sources are indexed once; see imgOverridesRescan) */
static void putMad(const char *rel, const ImgPic *pic, int idx, int val)
{
    imgOverridesRescan();
    putMadRaw(rel, pic, idx, val);
}

static const ImgOverride *byChain(const ImgOverrides *o, int a, int b)
{
    int i;
    for (i = 0; i < o->n; i++) {
        if (o->v[i].depth == 2 && o->v[i].chain[0] == a && o->v[i].chain[1] == b) return &o->v[i];
    }
    return NULL;
}

static void testOverrides(void)
{
    uint8_t file[32768], sheet[512];
    uint16_t page[PG_W * PG_H];
    const int parts[1][7] = {{0, 0, 16, 3, 0, 3, 0}};
    size_t n, sn;
    ImgRun run;
    ImgOverrides ov;
    ImgPic pic;
    ImgHints h;
    uint16_t pal4[16];
    uint8_t tim[1024], *png;
    size_t tn, pn;
    const ImgOverride *a;
    char dataDir[1100], modsDir[1100];
    long m0, m1;

    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        failures++;
        return;
    }
    snprintf(dataDir, sizeof dataDir, "%s/data", gRoot);
    assetSetData(dataDir);
    snprintf(modsDir, sizeof modsDir, "%s/mods", gRoot);
    makePage(page);
    n = makeTmFile(file, page, PG_W, PG_H);
    sn = makeSheet(sheet, parts, 1);
    pageHints(&h, pal4, page);
    tn = makeTim(tim, 2, 0, 0, NULL, PG_W, PG_H, page);
    CHECK(imgDecodeTim(tim, tn, &h, &pic) == 0);

    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, dataDir, "aaaa", 1) == 0);
    CHECK(imgExtractFile(&run, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 2);
    CHECK(imgRunClose(&run) == 0);

    {
        char path[1300];
        struct utimbuf old;
        old.actime = old.modtime = 1000000000;
        snprintf(path, sizeof path, "%s/images/screen/tlogousa/00_00.png", dataDir);
        CHECK(utime(path, &old) == 0);
    }
    /* A5.5: a screen's TM pages are edited by menus/ pieces only. The same PNG that a loose picture (TLOGO) takes from images/ is not read for
       the TM file, in the data folder or in a mod; the per-page entry stays (it carries the patched TIM of the menu packer). */
    CHECK(imgIsMenuScreen("AAA/DAT/SCREEN/STSHOPTM.BIN") && imgIsMenuScreen("AAA/DAT/MCARD/STMCRDTM.BIN"));
    CHECK(!imgIsMenuScreen("AAA/DAT/SCREEN/TLOGOUSA.BIN") && !imgIsMenuScreen("AAA/DAT/SCREEN/TTCOMMPK.BIN") && !imgIsMenuScreen("AAA/DAT/CARD/CARDDATA.BIN"));
    CHECK(!imgIsMenuScreen("AAA/DAT/SCREEN/STWMP0TM.BIN") && !imgIsMenuScreen("AAA/DAT/SCREEN/STWMP1TM.BIN")); /* raw 8-bit pages: no piece has a PNG */
    touchMod("a");
    CHECK(modsInit(modsDir, NULL) == 1);
    putMad("data/images/screen/stshoptm/00_00.png", &pic, 2, 6);
    putMad("mods/a/images/SCREEN/STSHOPTM/00_00.png", &pic, 2, 7);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/STSHOPTM.BIN", file, n, sheet, sn) == 0);
    CHECK(ov.n == 2 && ov.applied == 0 && byChain(&ov, 0, 0) && !byChain(&ov, 0, 0)->tim && byChain(&ov, 0, 0)->off > 0);
    imgOverridesFree(&ov);
    putMad("mods/a/images/SCREEN/STWMP0TM/00_00.png", &pic, 2, 7);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/STWMP0TM.BIN", file, n, sheet, sn) == 1); /* the exception keeps its images/ path */
    CHECK(byChain(&ov, 0, 0) && byChain(&ov, 0, 0)->tim != NULL);
    imgOverridesFree(&ov);
    rmtree(modsDir);
    CHECK(modsInit(modsDir, NULL) == 0);

    /* nothing edited, no mods: nothing substituted */
    CHECK(modsInit(modsDir, NULL) == 0);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 0);
    CHECK(ov.n == 2 && ov.applied == 0 && byChain(&ov, 0, 0) && !byChain(&ov, 0, 0)->tim);
    CHECK(byChain(&ov, 0, 1) && byChain(&ov, 0, 0)->off > 0);
    m0 = imgOverridesMix(&ov);
    imgOverridesFree(&ov);

    /* the user edited data/images/...: it applies; a pristine copy of another image does not. The
       extracted files get an old mtime so the edit below is seen even within the same second. */
    putMad("data/images/screen/tlogousa/00_00.png", &pic, 2, 6);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 1);
    a = byChain(&ov, 0, 0);
    CHECK(a && a->tim && a->len == tn && a->tim[20 + 1] == 0x26 /* 0x2600: pixel 2 is 6 */ && !byChain(&ov, 0, 1)->tim);
    CHECK(imgOverrideAt(&ov, a->off) == a && imgOverrideAt(&ov, 1) == NULL);
    m1 = imgOverridesMix(&ov);
    CHECK(m1 != m0);
    imgOverridesFree(&ov);

    /* mods win over data, the last mod over the first, a refused file falls to the next source */
    touchMod("a");
    touchMod("b");
    CHECK(modsInit(modsDir, NULL) == 2);
    putMad("mods/a/images/SCREEN/TLOGOUSA/00_00.png", &pic, 2, 7);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 1);
    CHECK(byChain(&ov, 0, 0)->tim[21] == 0x27);
    imgOverridesFree(&ov);
    spit("mods/b/images/SCREEN/TLOGOUSA/00_00.png", "not a png at all", 16);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 1);
    CHECK(byChain(&ov, 0, 0)->tim[21] == 0x27); /* b refused, a wins over the edited data */
    imgOverridesFree(&ov);
    putMad("mods/b/images/SCREEN/TLOGOUSA/00_00.png", &pic, 2, 8);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 1);
    CHECK(byChain(&ov, 0, 0)->tim[21] == 0x28);
    imgOverridesFree(&ov);
    /* an image of the 8-bit TIM through a mod */
    {
        ImgPic p8;
        ImgHints h8;
        uint16_t c8[256], pix[2] = {0x0102, 0x0304};
        uint8_t t8[2048];
        size_t n8;
        memset(c8, 0, sizeof c8);
        c8[1] = rgb555(1, 2, 3);
        n8 = makeTim(t8, 1, 256, 1, c8, 2, 1, pix);
        memset(&h8, 0, sizeof h8);
        CHECK(imgDecodeTim(t8, n8, &h8, &p8) == 0);
        putMad("mods/a/images/SCREEN/TLOGOUSA/00_01.png", &p8, 0, 9);
        memset(&ov, 0, sizeof ov);
        CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 2);
        CHECK(byChain(&ov, 0, 1)->tim && byChain(&ov, 0, 1)->tim[8 + 12 + 512 + 12] == 9);
        imgOverridesFree(&ov);
        /* the mods are read once: an image that appears in a mod while the game runs is not looked for (no probing of the
           folders per use); the rescan, which a restart and the opt-in hot reload do, sees it */
        putMadRaw("mods/b/images/SCREEN/TLOGOUSA/00_01.png", &p8, 0, 10);
        memset(&ov, 0, sizeof ov);
        CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 2);
        CHECK(byChain(&ov, 0, 1)->tim && byChain(&ov, 0, 1)->tim[8 + 12 + 512 + 12] == 9); /* still a's */
        imgOverridesFree(&ov);
        imgOverridesRescan();
        memset(&ov, 0, sizeof ov);
        CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 2);
        CHECK(byChain(&ov, 0, 1)->tim && byChain(&ov, 0, 1)->tim[8 + 12 + 512 + 12] == 10); /* b, the later mod, wins now */
        imgOverridesFree(&ov);
        imgPicFree(&p8);
    }
    /* no data folder: the mods alone */
    assetSetData(NULL);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/SCREEN/TLOGOUSA.BIN", file, n, sheet, sn) == 2);
    imgOverridesFree(&ov);
    CHECK(imgOverrideTim(1, 0, &tn) == NULL); /* no disc was given to imgStart: no runtime override */
    free(png = NULL);
    (void)pn;
    imgPicFree(&pic);
    modsInit(NULL, NULL);
    rmtree(gRoot);
}

/* ---- content-export E4: the card art (CARDPAK0-4) ---- */

#define CARD_STRIDE 0x62C
#define CARD_COUNT 3

/* CARDPAK-shaped file: CARD_COUNT cards of a 12-byte header and a 32x32 8-bit TIM with its own 256-colour CLUT.
   Card c has pixel (x, y) = (x + y + c) & 255 and colour i = (i + c) & 31 | 1. */
static size_t makeCardPak(uint8_t *out)
{
    uint16_t clut[256], img[16 * 32];
    uint8_t tim[0x700];
    int c, i, x, y;
    for (c = 0; c < CARD_COUNT; c++) {
        uint8_t *card = out + (size_t)c * CARD_STRIDE;
        size_t tn;
        for (i = 0; i < 256; i++) clut[i] = (uint16_t)(rgb555((i + c) & 31, (i * 3) & 31, c * 7) | 1u << 15);
        clut[0] = 0;
        for (y = 0; y < 32; y++) {
            for (x = 0; x < 16; x++) img[y * 16 + x] = (uint16_t)(((x * 2 + y + c) & 255) | ((x * 2 + 1 + y + c) & 255) << 8);
        }
        memset(card, 0, 12);
        card[0] = (uint8_t)(c + 1);
        card[3] = 4;
        tn = makeTim(tim, 1, 256, 1, clut, 16, 32, img);
        CHECK(tn == 0x620);
        memcpy(card + 12, tim, tn);
    }
    return (size_t)CARD_COUNT * CARD_STRIDE;
}

/* imgCardFaces' callback: keeps the PNG of card `id` (1-based within the test's file) */
typedef struct CardFaces {
    int n, ids[CARD_COUNT + 1];
    uint8_t *png[CARD_COUNT + 1];
    size_t len[CARD_COUNT + 1];
} CardFaces;

static int keepFace(void *ud, int id, const uint8_t *png, size_t len)
{
    CardFaces *f = (CardFaces *)ud;
    if (f->n > CARD_COUNT) return -1;
    f->ids[f->n] = id;
    f->png[f->n] = (uint8_t *)malloc(len);
    memcpy(f->png[f->n], png, len);
    f->len[f->n++] = len;
    return 0;
}

static void testCardPak(void)
{
    uint8_t file[CARD_COUNT * CARD_STRIDE];
    size_t n = makeCardPak(file), tl;
    ImgRun run;
    ImgOverrides ov;
    ImgPic pic;
    ImgHints h;
    Png p;
    char *txt;
    int i;
    CardFaces faces;
    char folder[8];

    CHECK(imgIsCardFile("AAA/DAT/CARD/CARDPAK0.BIN") && imgIsCardFile("AAA/DAT/CARD/CARDPAK4.BIN"));
    CHECK(!imgIsCardFile("AAA/DAT/CARD/CARDPAK5.BIN") && !imgIsCardFile("AAA/DAT/CARD/CARD_NPC.BIN") && !imgIsCardFile("AAA/DAT/SCREEN/CARDPAK0.BIN"));
    /* the game's card number: file A, image i is A * 64 + i + 1 (cardDrawerSetCard) */
    CHECK(imgCardId("AAA/DAT/CARD/CARDPAK0.BIN", 0) == 1 && imgCardId("AAA/DAT/CARD/CARDPAK0.BIN", 63) == 64 && imgCardId("AAA/DAT/CARD/CARDPAK1.BIN", 0) == 65);
    CHECK(imgCardId("AAA/DAT/CARD/CARDPAK4.BIN", 59) == 316 && imgCardId("AAA/DAT/CARD/CARDPAK0.BIN", 64) == 0 && imgCardId("AAA/DAT/CARD/CARDPAK5.BIN", 0) == 0);
    CHECK(imgCardFolder(12, folder, sizeof folder) == 0 && !strcmp(folder, "c012") && imgCardFolder(316, folder, sizeof folder) == 0 && !strcmp(folder, "c316"));
    CHECK(imgCardFolder(0, folder, sizeof folder) != 0 && imgCardFolder(1000, folder, sizeof folder) != 0 && imgCardFolder(5, folder, 4) != 0);
    CHECK(imgCardIdOf("c012") == 12 && imgCardIdOf("c316") == 316 && imgCardIdOf("c12") == 0 && imgCardIdOf("x012") == 0 && imgCardIdOf("c01a") == 0 && imgCardIdOf("sample") == 0);
    memset(&faces, 0, sizeof faces);
    CHECK(imgCardFaces("AAA/DAT/CARD/CARDPAK2.BIN", file, n, keepFace, &faces) == CARD_COUNT && faces.n == CARD_COUNT);
    for (i = 0; i < faces.n; i++) CHECK(faces.ids[i] == 128 + i + 1);
    CHECK(imgCardFaces("AAA/DAT/SCREEN/CARDPAK0.BIN", file, n, keepFace, &faces) == -1);
    for (i = 0; i < faces.n; i++) free(faces.png[i]);

    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        fprintf(stderr, "imagetest: cannot make a scratch folder\n");
        failures++;
        return;
    }
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "aaaa", 4) == 0);
    CHECK(imgExtractFile(&run, "AAA/DAT/CARD/CARDPAK0.BIN", file, n, NULL, 0) == 0); /* the faces are not images/'s any more: card_export.c writes them to cards/ */
    CHECK(run.written == 0 && run.failed == 0);
    CHECK(imgRunClose(&run) == 0);
    CHECK(!exists("images/card/cardpak0/00.png") && !exists("images/card"));
    memset(&faces, 0, sizeof faces);
    CHECK(imgCardFaces("AAA/DAT/CARD/CARDPAK0.BIN", file, n, keepFace, &faces) == CARD_COUNT && faces.n == CARD_COUNT);
    txt = faces.n == CARD_COUNT ? (char *)faces.png[2] : NULL;
    tl = faces.n == CARD_COUNT ? faces.len[2] : 0;
    CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0 && p.w == 32 && p.h == 32 && p.depth == 8 && p.nPal == 256);
    if (txt) {
        CHECK(p.px[0] == 2 && p.px[33] == 4 && p.px[31 * 32 + 31] == (31 + 31 + 2));
        CHECK(p.rgb[3 * 5] == (uint8_t)(((5 + 2) & 31) << 3 | ((5 + 2) & 31) >> 2)); /* palette entry 5 of card 2 */
        CHECK(p.nAlpha >= 1 && p.alpha[0] == 0);
        free(p.px);
    }
    for (i = 0; i < faces.n; i++) free(faces.png[i]);

    /* a mod's PNG replaces one card picture: the TIM the game would load has its pixel and colour */
    touchMod("a");
    {
        char modsDir[1300];
        snprintf(modsDir, sizeof modsDir, "%s/mods", gRoot);
        CHECK(modsInit(modsDir, NULL) == 1);
    }
    memset(&h, 0, sizeof h);
    CHECK(imgDecodeTim(file + 12 + CARD_STRIDE, 0x620, &h, &pic) == 0);
    putMad("mods/a/cards/c002/face.png", &pic, 0, 9); /* card 2 is image 1 of CARDPAK0 */
    imgPicFree(&pic);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/CARD/CARDPAK0.BIN", file, n, NULL, 0) == 1);
    CHECK(ov.n == CARD_COUNT);
    for (i = 0; i < ov.n; i++) CHECK(ov.v[i].off == (size_t)12 + (size_t)i * CARD_STRIDE && ov.v[i].depth == 1);
    CHECK(imgOverrideAt(&ov, 12 + CARD_STRIDE) && imgOverrideAt(&ov, 12 + CARD_STRIDE)->tim);
    CHECK(imgOverrideAt(&ov, 12)->tim == NULL);
    if (imgOverrideAt(&ov, 12 + CARD_STRIDE)->tim) {
        const uint8_t *t = imgOverrideAt(&ov, 12 + CARD_STRIDE)->tim;
        CHECK(imgOverrideAt(&ov, 12 + CARD_STRIDE)->len == 0x620);
        CHECK(t[8 + 12 + 512 + 12] == 9 && t[8 + 12 + 512 + 12 + 1] == file[12 + CARD_STRIDE + 8 + 12 + 512 + 12 + 1]);
        CHECK(!memcmp(t, file + 12 + CARD_STRIDE, 8 + 12 + 512 + 12)); /* header, CLUT and image block header as on disc */
    }
    imgOverridesFree(&ov);

    /* the old images/card/cardpak<A>/<ii>.png route is gone; an edited data/cards/c003/face.png counts, a pristine one (its hash in cards/manifest.json) does not */
    memset(&h, 0, sizeof h);
    CHECK(imgDecodeTim(file + 12 + 2 * CARD_STRIDE, 0x620, &h, &pic) == 0);
    putMad("mods/a/images/card/cardpak0/00.png", &pic, 0, 7);
    {
        char dataDir[1300], mp[1400], hex[17];
        uint8_t *bytes;
        size_t bl;
        TdManifest man;

        snprintf(dataDir, sizeof dataDir, "%s/data", gRoot);
        assetSetData(dataDir);
        putMadRaw("data/cards/c003/face.png", &pic, 1, 5);
        memset(&ov, 0, sizeof ov);
        CHECK(imgOverridesBuild(&ov, "AAA/DAT/CARD/CARDPAK0.BIN", file, n, NULL, 0) == 2); /* the mod's c002 and the edited data c003, not images/card/.../00.png */
        CHECK(imgOverrideAt(&ov, 12)->tim == NULL && imgOverrideAt(&ov, 12 + 2 * CARD_STRIDE)->tim != NULL);
        imgOverridesFree(&ov);
        snprintf(mp, sizeof mp, "%s/data/cards/c003/face.png", gRoot);
        CHECK(tdReadFile(mp, &bytes, &bl) == 0);
        tdHashHex(tdHash(bytes, bl), hex);
        free(bytes);
        expManInit(&man, "cards");
        CHECK(tdManifestSet(&man, "c003/face.png", hex) == 0);
        snprintf(mp, sizeof mp, "%s/data/cards/manifest.json", gRoot);
        CHECK(expManWrite("imagetest", mp, &man) == 0);
        tdManifestFree(&man);
        CHECK(imgOverridesBuild(&ov, "AAA/DAT/CARD/CARDPAK0.BIN", file, n, NULL, 0) == 1);
        CHECK(imgOverrideAt(&ov, 12 + 2 * CARD_STRIDE)->tim == NULL);
        imgOverridesFree(&ov);
        assetSetData(NULL);
    }
    imgPicFree(&pic);
    modsInit(NULL, NULL);
    rmtree(gRoot);
}

/* CARDDATA.BIN: [0] and [1] archives of one page each, [2] and [3] the sheets that describe them (scene.c loads the pages and
   sprite.draw uses the sheets 2 and 3); the table has no end marker. Page 0 and 1 hold two palettes, at columns 0 and 16 of row 3. */
static void testCardData(void)
{
    uint8_t file[8192], sh2[512], sh3[512], page[1024], in0[1200], in1[1200];
    uint16_t pg[32 * 4];
    const int parts2[1][7] = {{0, 0, 16, 3, 0, 3, 0}};
    const int parts3[1][7] = {{0, 0, 16, 3, 16, 3, 0}};
    const int fr[1][13] = {{1, 0, 4, 5}};
    const uint8_t *ents[4], *inner[1];
    size_t lens[4], il[1], s2, s3, pn, n, tl;
    ImgRun run;
    char *txt;
    int i;

    for (i = 0; i < 32 * 4; i++) pg[i] = 0;
    pg[0] = 0x2100;
    for (i = 0; i < 16; i++) {
        pg[96 + i] = (uint16_t)(i ? rgb555(31, i, 0) : 0);
        pg[96 + 16 + i] = (uint16_t)(i ? rgb555(i, 31, 0) : 0);
    }
    s2 = makeSheetF(sh2, parts2, 1, fr, 1);
    s3 = makeSheetF(sh3, parts3, 1, fr, 1);
    pn = makeTim(page, 2, 0, 0, NULL, 32, 4, pg);
    inner[0] = page; il[0] = pn;
    lens[0] = makeArchive(in0, inner, il, 1, 1);
    lens[1] = makeArchive(in1, inner, il, 1, 1);
    ents[0] = in0; ents[1] = in1;
    ents[2] = sh2 + 4; lens[2] = s2 - 4;
    ents[3] = sh3 + 4; lens[3] = s3 - 4;
    n = makeArchive(file, ents, lens, 4, 0);

    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        fprintf(stderr, "imagetest: cannot make a scratch folder\n");
        failures++;
        return;
    }
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "aaaa", 4) == 0);
    CHECK(imgExtractFile(&run, "AAA/DAT/CARD/CARDDATA.BIN", file, n, NULL, 0) == 2);
    CHECK(run.written == 4 && run.failed == 0);
    CHECK(imgRunClose(&run) == 0);
    for (i = 0; i < 2; i++) {
        char rel[64];
        Png p;
        snprintf(rel, sizeof rel, "images/card/carddata/%02d_00.json", i);
        txt = slurp(rel, &tl);
        CHECK(contains(txt, i ? "\"sheet\": \"CARD/CARDDATA.BIN (entry 3)\"" : "\"sheet\": \"CARD/CARDDATA.BIN (entry 2)\""));
        free(txt);
        snprintf(rel, sizeof rel, "images/card/carddata/%02d_00.png", i);
        txt = slurp(rel, &tl);
        CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0 && p.w == 128 && p.h == 4 && p.depth == 4);
        if (txt) { /* the palette each sheet names */
            CHECK(i == 0 ? (p.rgb[3] == 255 && p.rgb[4] == (1 << 3 | 1 >> 2)) : (p.rgb[3] == (1 << 3 | 1 >> 2) && p.rgb[4] == 255));
            free(p.px);
        }
        free(txt);
    }
    rmtree(gRoot);
}

/* FIELD/STAGE/S<nnn>MASK.BIN (content-export E6a). The pages load side by side, 64 halfwords each; the sheet (entry 0 of S<nnn>TMPK.BIN,
   the file starts with its entry table so the sheet sits at 8) places palettes relative to that area: row < 256 of the pages, row >= 256 the
   CLUT of the 8-bit TIM. Page 0: raw page of 4-bit art with two palettes at row 3 (columns 0 and 16); page 1: raw page of 4-bit art whose
   palette is the one in page 0; page 2: an 8-bit TIM with a 2-row CLUT; page 3: raw page of 8-bit art whose palette is CLUT row 1. */
static void stagePal(uint16_t *pg, int col, int which)
{
    int i;
    for (i = 0; i < 16; i++) pg[64 * 3 + col + i] = (uint16_t)(i ? (which ? rgb555(3, i, 31) : rgb555(i, 3, 31)) : 0);
}

static size_t makeStageMask(uint8_t *out, size_t *lens, uint8_t *p0)
{
    uint16_t c8[512], pg0[64 * 4], pg1[64 * 4], pg3[64 * 4];
    uint8_t t1[4096], t2[4096], t3[4096], z1[8192], z3[8192];
    uint16_t pix[2] = {0x0102, 0x0304};
    size_t n0, n1, n2, n3, nz1, nz3;
    const uint8_t *ents[4];
    int i;

    memset(c8, 0, sizeof c8);
    for (i = 0; i < 256; i++) c8[256 + i] = (uint16_t)(i ? rgb555(i & 31, 9, 1) : 0);
    memset(pg0, 0, sizeof pg0);
    memset(pg1, 0, sizeof pg1);
    memset(pg3, 0, sizeof pg3);
    pg0[0] = 0x2100; /* 4-bit indices 0, 0, 1, 2 */
    pg1[0] = 0x4321; /* 1, 2, 3, 4 */
    pg3[0] = 0x0403; /* 8-bit indices 3, 4 */
    stagePal(pg0, 0, 0);
    stagePal(pg0, 16, 1);
    n0 = makeTim(p0, 2, 0, 0, NULL, 64, 4, pg0); /* page 0 stays raw: no compression, the test reads its offsets */
    n1 = makeTim(t1, 2, 0, 0, NULL, 64, 4, pg1);
    n2 = makeTim(t2, 1, 256, 2, c8, 2, 1, pix);
    n3 = makeTim(t3, 2, 0, 0, NULL, 64, 4, pg3);
    nz1 = rlenPack(t1, n1, z1);
    nz3 = rlenPack(t3, n3, z3);
    ents[0] = p0; lens[0] = n0;
    ents[1] = z1; lens[1] = nz1;
    ents[2] = t2; lens[2] = n2;
    ents[3] = z3; lens[3] = nz3;
    return makeArchive(out, ents, lens, 4, 1);
}

static void testStageMask(void)
{
    uint8_t mask[16384], p0[4096], sh[512], tmpk[1024], dummy[8] = {0}, bg[4096];
    /* parts: page 0 palette 1 of page 0 (column 16), page 1 the palette of page 0 column 0, page 3 (8-bit texels 384..) CLUT row 1 */
    const int parts[3][7] = {{0, 0, 16, 16, 16, 3, 0}, {256, 0, 32, 16, 0, 3, 0}, {384, 0, 16, 16, 0, 257, 1}};
    const uint8_t *ents[2];
    size_t lens[2], mlens[4], mn, sn = makeSheet(sh, parts, 3) - 4, tn, bn;
    ImgRun run;
    ImgOverrides ov;
    ImgPic pic;
    ImgHints h;
    char name[64], sp[96];
    int chain[1] = {1};

    mn = makeStageMask(mask, mlens, p0);
    CHECK(imgIsStageMask("AAA/DAT/FIELD/STAGE/S233MASK.BIN") && imgIsStageBg("AAA/DAT/FIELD/STAGE/S415BG02.BIN"));
    CHECK(!imgIsStageMask("AAA/DAT/FIELD/STAGE/S233TMPK.BIN") && !imgIsStageMask("AAA/DAT/FIELD/STAGE/SDIGDEMO.BIN") &&
          !imgIsStageMask("AAA/DAT/FIELD/STAGE/S23MASK.BIN") && !imgIsStageBg("AAA/DAT/FIELD/STAGE/S415BG1.BIN") &&
          !imgIsStageBg("AAA/DAT/FIELD/S415BG01.BIN") && !imgIsStageMask("AAA/DAT/SCREEN/S233MASK.BIN"));
    CHECK(imgSheetPathOf("AAA/DAT/FIELD/STAGE/S233MASK.BIN", sp, sizeof sp) && !strcmp(sp, "AAA/DAT/FIELD/STAGE/S233TMPK.BIN"));
    CHECK(!imgSheetPathOf("AAA/DAT/FIELD/STAGE/S415BG01.BIN", sp, sizeof sp));
    CHECK(imgImageName("AAA/DAT/FIELD/STAGE/S233MASK.BIN", chain, 1, name, sizeof name) == 0 && !strcmp(name, "field/stage/s233mask/01.png"));

    ents[0] = sh + 4; lens[0] = sn;
    ents[1] = dummy; lens[1] = sizeof dummy;
    tn = makeArchive(tmpk, ents, lens, 2, 0);

    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        fprintf(stderr, "imagetest: cannot make a scratch folder\n");
        failures++;
        return;
    }
    /* a BG file: its images, no piece map */
    {
        uint16_t px[2] = {0x0201, 0x0403};
        uint8_t t[256];
        const uint8_t *be[1];
        size_t bl[1], tl2 = makeTim(t, 2, 0, 0, NULL, 2, 1, px);
        be[0] = t;
        bl[0] = tl2;
        bn = makeArchive(bg, be, bl, 1, 1);
    }
    memset(&run, 0, sizeof run);
    CHECK(imgRunOpen(&run, gRoot, "aaaa", 5) == 0);
    CHECK(imgExtractFile(&run, "AAA/DAT/FIELD/STAGE/S415BG01.BIN", bg, bn, NULL, 0) == 1);
    CHECK(run.written == 1 && run.failed == 0);
    imgRunClose(&run);
    CHECK(exists("images/field/stage/s415bg01/00.png") && !exists("images/field/stage/s415bg01/00.json"));

    /* the objects of a stage are edited only by piece (A6.4): a PNG a mod leaves in images/ for a MASK page is not read, and the page still has its
       (empty) entry, the carrier of what the stage hook writes into it; a BG file keeps its PNGs */
    touchMod("a");
    {
        char modsDir[1300];
        snprintf(modsDir, sizeof modsDir, "%s/mods", gRoot);
        CHECK(modsInit(modsDir, NULL) == 1);
    }
    {
        uint16_t pg0[64 * 4], clut[256], px2[2] = {0x0201, 0x0403}, px1[1] = {0};
        uint8_t t8[2048], t16[256], tim0[4096];
        const uint8_t *be[2];
        size_t bl[2], n0;
        int i;
        memset(pg0, 0, sizeof pg0);
        pg0[0] = 0x2100;
        stagePal(pg0, 0, 0);
        stagePal(pg0, 16, 1);
        memset(&h, 0, sizeof h);
        h.pixelBits = 4;
        h.pal = pg0 + 64 * 3 + 16;
        h.nPal = 16;
        h.palAt = 1;
        h.palX = 16;
        h.palY = 3;
        n0 = makeTim(tim0, 2, 0, 0, NULL, 64, 4, pg0);
        CHECK(imgDecodeTim(tim0, n0, &h, &pic) == 0);
        putMad("mods/a/images/field/stage/s233mask/00.png", &pic, 0, 9);
        imgPicFree(&pic);
        memset(&ov, 0, sizeof ov);
        CHECK(imgOverridesBuild(&ov, "AAA/DAT/FIELD/STAGE/S233MASK.BIN", mask, mn, tmpk, tn) == 0);
        CHECK(ov.n == 4 && ov.v[0].off == 20 && ov.v[1].depth == 1 && ov.v[1].chain[0] == 1);
        for (i = 0; i < ov.n; i++) CHECK(ov.v[i].tim == NULL && ov.v[i].rel[0] == 0); /* nothing to watch in images/ either */
        imgOverridesFree(&ov);

        /* a BG file: an 8-bit TIM, then a raw page with its palette; its PNG still goes in */
        for (i = 0; i < 256; i++) clut[i] = (uint16_t)(i * 3);
        be[0] = t8;
        bl[0] = makeTim(t8, 1, 256, 1, clut, 1, 1, px1);
        be[1] = t16;
        bl[1] = makeTim(t16, 2, 0, 0, NULL, 2, 1, px2);
        bn = makeArchive(bg, be, bl, 2, 1);
        memset(&h, 0, sizeof h);
        h.pixelBits = 8;
        h.pal = clut;
        h.nPal = 256;
        CHECK(imgDecodeTim(t16, bl[1], &h, &pic) == 0);
        putMad("mods/a/images/field/stage/s415bg01/01.png", &pic, 0, 9);
        imgPicFree(&pic);
        memset(&ov, 0, sizeof ov);
        CHECK(imgOverridesBuild(&ov, "AAA/DAT/FIELD/STAGE/S415BG01.BIN", bg, bn, NULL, 0) == 1);
        CHECK(imgOverrideAt(&ov, ov.v[1].off) && imgOverrideAt(&ov, ov.v[1].off)->tim != NULL);
        imgOverridesFree(&ov);
    }
    modsInit(NULL, NULL);
    rmtree(gRoot);
}

/* ---- field-sprites-png T3: sprite frames and FIELDCOM through the overrides ---- */

/* images/<srcRel> of the scratch folder copied to <dstRel> with palette entry palIdx set (palIdx < 0: untouched) and pixel
   pxIdx set to pxVal (pxIdx < 0: untouched) */
static void editPng(const char *srcRel, const char *dstRel, int palIdx, int r, int g, int b, int pxIdx, int pxVal)
{
    size_t tl, pl;
    char *txt = slurp(srcRel, &tl);
    Png p;
    uint8_t *png;
    CHECK(txt && pngRead((uint8_t *)txt, tl, &p) == 0);
    if (!txt) return;
    if (palIdx >= 0) {
        p.rgb[3 * palIdx] = (uint8_t)r;
        p.rgb[3 * palIdx + 1] = (uint8_t)g;
        p.rgb[3 * palIdx + 2] = (uint8_t)b;
    }
    if (pxIdx >= 0) p.px[pxIdx] = (uint8_t)pxVal;
    CHECK(pngWriteIndexed(p.px, p.w, p.h, p.depth, p.rgb, p.nPal, p.alpha, p.nAlpha, &png, &pl) == 0);
    spit(dstRel, png, pl);
    free(png);
    free(p.px);
    free(txt);
}

static void testSpriteOverrides(void)
{
    static const char *disc = "AAA/DAT/FIELD/SPRT/P001PLAY.BIN";
    uint8_t file[4096], fc[8192], big[20000], px17[8 * 3], pl17[17 * 3];
    const uint8_t *bents[200];
    size_t blens[200], n, nfc, tl, tn, i;
    SprEntry ent[8];
    SprStats st;
    ImgOverrides ov;
    const ImgOverride *a, *b2;
    char dataDir[1100], modsDir[1100], rel[160], *txt;
    long m0, m1;
    int k;

    n = makeSpriteFile(file, 1);
    nfc = makeFieldComFile(fc);
    snprintf(gRoot, sizeof gRoot, "%s/imagetest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        failures++;
        return;
    }
    snprintf(dataDir, sizeof dataDir, "%s/data", gRoot);
    assetSetData(dataDir);
    snprintf(modsDir, sizeof modsDir, "%s/mods", gRoot);
    memset(&st, 0, sizeof st);
    CHECK(writeSprite(dataDir, disc, file, n, &st) == 3);
    {
        ImgRun run;
        memset(&run, 0, sizeof run);
        CHECK(imgRunOpen(&run, dataDir, "aaaa", 3) == 0);
        CHECK(imgExtractFile(&run, "AAA/DAT/FIELD/FIELDCOM.BIN", fc, nfc, NULL, 0) == 2);
        CHECK(imgRunClose(&run) == 0);
    }
    {
        char path[1300];
        struct utimbuf old;
        old.actime = old.modtime = 1000000000; /* so that the edit below shows in the mtime even within the same second */
        snprintf(path, sizeof path, "%s/sprites/p001play/001.png", dataDir);
        CHECK(utime(path, &old) == 0);
    }
    CHECK(sprTable(file, n, ent, 8) == 6 && ent[0].off > 0 && ent[5].len == 36);
    CHECK(modsInit(modsDir, NULL) == 0);

    /* nothing edited, no mods: the frames have a source (the extractor's own) but none counts, and nothing is read yet */
    memset(&ov, 0, sizeof ov);
    CHECK(imgSpriteOverridesBuild(&ov, disc, file, n) == 3);
    CHECK(ov.n == 3 && ov.applied == 0);
    a = imgOverrideGet(&ov, ent[0].off);
    CHECK(a && a->chain[0] == 0 && a->depth == 1 && !a->tim && strcmp(a->rel, "sprites/p001play/000.png") == 0);
    CHECK(imgOverrideGet(&ov, ent[4].off) && imgOverrideGet(&ov, ent[4].off)->chain[0] == 4);
    CHECK(imgOverrideGet(&ov, ent[2].off) == NULL && imgOverrideGet(&ov, ent[5].off) == NULL && imgOverrideGet(&ov, 1) == NULL); /* animations, set table */
    CHECK(ov.applied == 0);
    m0 = imgOverridesMix(&ov);
    imgOverridesFree(&ov);

    /* the user edited data/sprites/... it applies (frame 1), a pristine one (frame 0) does not */
    editPng("data/sprites/p001play/001.png", "data/sprites/p001play/001.png", 3, 9, 9, 9, 1, 2);
    memset(&ov, 0, sizeof ov);
    CHECK(imgSpriteOverridesBuild(&ov, disc, file, n) == 3 && ov.applied == 0);
    a = imgOverrideGet(&ov, ent[1].off);
    CHECK(a && a->tim && a->len == ent[1].len && ov.applied == 1);
    CHECK(a->tim[20 + 6] == (9 >> 3 | (9 >> 3) << 5) && a->tim[20 + 7] == (9 >> 3) << 2); /* palette entry 3: (1,1,1) */
    CHECK(!imgOverrideGet(&ov, ent[0].off)->tim);
    CHECK(imgOverridesMix(&ov) != m0);
    imgOverridesFree(&ov);

    /* a mod with an unchanged copy of the extracted PNG gives back the very same TIM bytes */
    touchMod("a");
    CHECK(modsInit(modsDir, NULL) == 1);
    txt = slurp("data/sprites/p001play/000.png", &tl);
    CHECK(txt != NULL);
    spit("mods/a/sprites/p001play/000.png", txt, tl);
    free(txt);
    memset(&ov, 0, sizeof ov);
    CHECK(imgSpriteOverridesBuild(&ov, disc, file, n) == 3 && ov.applied == 0); /* lazy: no PNG read yet */
    a = imgOverrideGet(&ov, ent[0].off);
    CHECK(a && a->tim && a->len == ent[0].len && memcmp(a->tim, file + ent[0].off, ent[0].len) == 0 && ov.applied == 1);
    b2 = imgOverrideGet(&ov, ent[0].off);
    CHECK(b2 == a && b2->tim == a->tim && ov.applied == 1); /* kept, not decoded again */
    imgOverridesFree(&ov);

    /* a palette edit and a pixel edit */
    editPng("data/sprites/p001play/000.png", "mods/a/sprites/p001play/000.png", 5, 255, 0, 0, 2, 6);
    memset(&ov, 0, sizeof ov);
    CHECK(imgSpriteOverridesBuild(&ov, disc, file, n) == 3);
    a = imgOverrideGet(&ov, ent[0].off);
    CHECK(a && a->tim && a->tim[30] == 0x1F && a->tim[31] == 0x00); /* CLUT entry 5: red */
    CHECK(a && a->tim[64] == 0x10 && a->tim[65] == 0x36);           /* pixel 2: 0x3210 -> 0x3610 */
    CHECK(a && memcmp(a->tim, file + ent[0].off, 20) == 0 && memcmp(a->tim + 32, file + ent[0].off + 32, 32) == 0);
    imgOverridesFree(&ov);

    /* rejections: the next source is used, nothing crashes. 17 colours (b/000) falls to a; not a PNG (b/001) falls to the
       edited data file; a PNG of another size (b/004) leaves the disc image */
    touchMod("b");
    CHECK(modsInit(modsDir, NULL) == 2);
    for (k = 0; k < 8 * 3; k++) px17[k] = (uint8_t)(k % 17);
    for (k = 0; k < 17 * 3; k++) pl17[k] = (uint8_t)(k * 5);
    {
        uint8_t *png;
        size_t pl;
        uint8_t ix[24];
        for (k = 0; k < 24; k++) ix[k] = (uint8_t)(px17[k] % 16);
        CHECK(pngWriteIndexed(ix, 8, 3, 8, pl17, 17, NULL, 0, &png, &pl) == 0);
        spit("mods/b/sprites/p001play/000.png", png, pl);
        free(png);
    }
    spit("mods/b/sprites/p001play/001.png", "not a png at all", 16);
    spit("mods/b/sprites/p001play/004.png", kPng4, sizeof kPng4);
    memset(&ov, 0, sizeof ov);
    CHECK(imgSpriteOverridesBuild(&ov, disc, file, n) == 3);
    a = imgOverrideGet(&ov, ent[0].off);
    CHECK(a && a->tim && a->tim[30] == 0x1F && ov.applied == 1);       /* a's red palette */
    a = imgOverrideGet(&ov, ent[1].off);
    CHECK(a && a->tim && a->tim[20 + 6] == (9 >> 3 | (9 >> 3) << 5));  /* the edited data file */
    a = imgOverrideGet(&ov, ent[4].off);
    CHECK(a && !a->tim && ov.applied == 2);
    imgOverridesFree(&ov);

    /* the last mod wins */
    editPng("data/sprites/p001play/000.png", "mods/b/sprites/p001play/000.png", 5, 0, 255, 0, -1, 0);
    memset(&ov, 0, sizeof ov);
    CHECK(imgSpriteOverridesBuild(&ov, disc, file, n) == 3);
    a = imgOverrideGet(&ov, ent[0].off);
    CHECK(a && a->tim && a->tim[30] == 0xE0 && a->tim[31] == 0x03);
    imgOverridesFree(&ov);

    /* a whole sprite of 200 frames replaced by a mod: lookups are by offset, nothing is decoded twice */
    {
        uint8_t fr[128];
        size_t fl = makeFrame(fr, 1, 1, 0x0001);
        ImgPic pic;
        CHECK(imgDecodeTim(fr, fl, NULL, &pic) == 0);
        for (i = 0; i < 200; i++) {
            static uint8_t frames[200][128];
            blens[i] = makeFrame(frames[i], 1, 1, (uint16_t)(0x0001 + i));
            bents[i] = frames[i];
        }
        tn = makeArchive(big, bents, blens, 200, 0);
        CHECK(tn > 0 && tn < sizeof big);
        modsInit(NULL, NULL);
        touchMod("c");
        CHECK(modsInit(modsDir, NULL) == 3);
        for (i = 0; i < 200; i++) {
            pic.px[0] = (uint8_t)(i % 16);
            snprintf(rel, sizeof rel, "mods/c/sprites/p009big0/%03d.png", (int)i);
            {
                uint8_t *png;
                size_t pl;
                CHECK(imgPicToPng(&pic, &png, &pl) == 0);
                spit(rel, png, pl);
                snprintf(rel, sizeof rel, "mods/c/images/field/sprt/p008old0/%03d.png", (int)i); /* the route of before A7.5 */
                spit(rel, png, pl);
                free(png);
            }
        }
        imgPicFree(&pic);
        memset(&ov, 0, sizeof ov);
        {
            SprEntry be[256];
            CHECK(sprTable(big, tn, be, 256) == 200);
            CHECK(imgSpriteOverridesBuild(&ov, "AAA/DAT/FIELD/SPRT/P009BIG0.BIN", big, tn) == 200 && ov.n == 200 && ov.applied == 0);
            for (k = 0; k < 3; k++) {
                for (i = 0; i < 200; i++) {
                    const ImgOverride *e = imgOverrideGet(&ov, be[i].off);
                    CHECK(e && e->chain[0] == (int)i && e->tim && (e->tim[64] & 15) == (int)(i % 16));
                }
                CHECK(ov.applied == 200); /* the first pass decoded them, the others only look */
            }
            CHECK(imgOverrideGet(&ov, be[199].off + 1) == NULL);
        }
        imgOverridesFree(&ov);
        /* images/field/sprt/<name>/ is not read any more: the same 200 PNGs there give no source, and the data folder's own are never an edit */
        memset(&ov, 0, sizeof ov);
        CHECK(imgSpriteOverridesBuild(&ov, "AAA/DAT/FIELD/SPRT/P008OLD0.BIN", big, tn) == 0 && ov.applied == 0);
        imgOverridesFree(&ov);
    }

    /* FIELDCOM.BIN: a mod's PNG of its second image changes the palette row the sheet of entry 1 names */
    {
        char cmd[1400];
        snprintf(cmd, sizeof cmd, "rm -rf '%s/mods/b' '%s/mods/c' '%s/mods/a/images'", gRoot, gRoot, gRoot);
        if (system(cmd)) {
        }
    }
    CHECK(modsInit(modsDir, NULL) == 1);
    editPng("data/images/field/fieldcom/03_00.png", "mods/a/images/FIELD/FIELDCOM/03_00.png", 1, 0, 0, 255, -1, 0);
    memset(&ov, 0, sizeof ov);
    CHECK(imgOverridesBuild(&ov, "AAA/DAT/FIELD/FIELDCOM.BIN", fc, nfc, NULL, 0) == 1);
    {
        const ImgOverride *c3 = byChain(&ov, 3, 0), *c2 = byChain(&ov, 2, 0);
        CHECK(c3 && c3->tim && c2 && !c2->tim && c3->off > 0);
        CHECK(c3 && c3->tim && c3->tim[118] == 0x00 && c3->tim[119] == 0x7C); /* page row 3, entry 1: blue */
        CHECK(c3 && imgOverrideGet(&ov, c3->off) == c3 && imgOverrideGet(&ov, c2->off) == c2);
    }
    imgOverridesFree(&ov);
    CHECK(imgOverrideTim(1, 0, &tl) == NULL);
    modsInit(NULL, NULL);
    assetSetData(NULL);
    rmtree(gRoot);
}


/* ---- real deflate in the writer (U3) ---- */

/* an old file as the stored-block writer made it: filter 0, stored blocks, Adler-32 */
static size_t storedPng(const uint8_t *px, int w, int h, const uint8_t *rgb, int nColors, uint8_t *out)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    size_t rawLen = (size_t)(w + 1) * h, n = 0, at = 0, zn = 0, i;
    uint8_t *raw = (uint8_t *)malloc(rawLen), *z = (uint8_t *)malloc(rawLen + 64);
    uint32_t a1 = 1, a2 = 0;
    uint8_t ihdr[13] = {0, 0, 0, 0, 0, 0, 0, 0, 8, 3, 0, 0, 0};
    int y;
#define PUT32(buf, v) do { (buf)[0] = (uint8_t)((v) >> 24); (buf)[1] = (uint8_t)((v) >> 16); (buf)[2] = (uint8_t)((v) >> 8); (buf)[3] = (uint8_t)(v); } while (0)
#define CHUNK(type, d, len) do { \
        uint32_t crc_ = 0xFFFFFFFFu, l_ = (uint32_t)(len); \
        uint8_t tmp_[4]; \
        PUT32(tmp_, l_); memcpy(out + n, tmp_, 4); n += 4; \
        memcpy(out + n, type, 4); n += 4; memcpy(out + n, d, l_); n += l_; \
        crc_ = crc32of(out + n - l_ - 4, l_ + 4); PUT32(tmp_, crc_); memcpy(out + n, tmp_, 4); n += 4; \
    } while (0)
    for (y = 0; y < h; y++) {
        raw[(size_t)y * (w + 1)] = 0;
        memcpy(raw + (size_t)y * (w + 1) + 1, px + (size_t)y * w, (size_t)w);
    }
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    do {
        size_t k = rawLen - at > 65535 ? 65535 : rawLen - at;
        z[zn++] = (uint8_t)(at + k >= rawLen);
        z[zn++] = (uint8_t)k; z[zn++] = (uint8_t)(k >> 8); z[zn++] = (uint8_t)~k; z[zn++] = (uint8_t)(~k >> 8);
        memcpy(z + zn, raw + at, k);
        zn += k;
        at += k;
    } while (at < rawLen);
    for (i = 0; i < rawLen; i++) {
        a1 = (a1 + raw[i]) % 65521u;
        a2 = (a2 + a1) % 65521u;
    }
    PUT32(z + zn, (a2 << 16) | a1);
    zn += 4;
    memcpy(out, sig, 8);
    n = 8;
    PUT32(ihdr, (uint32_t)w);
    PUT32(ihdr + 4, (uint32_t)h);
    CHUNK("IHDR", ihdr, 13);
    CHUNK("PLTE", rgb, (size_t)nColors * 3);
    CHUNK("IDAT", z, zn);
    CHUNK("IEND", z, 0);
#undef CHUNK
#undef PUT32
    free(raw);
    free(z);
    return n;
}

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void testDeflate(void)
{
    uint8_t rgb[256 * 3], *px, *png = NULL, *old, *png2;
    size_t len = 0, oldLen, len2;
    int i, k, x, y;
    Png p;

    for (i = 0; i < 256 * 3; i++) rgb[i] = (uint8_t)(i * 7);
    /* a typical stage tile: 256x256, a few flat regions, a gradient and some noise */
    px = (uint8_t *)malloc(256 * 256);
    for (y = 0; y < 256; y++) {
        for (x = 0; x < 256; x++) {
            uint8_t v = (uint8_t)(((x / 32) + (y / 32)) & 7);
            if (y > 200) v = (uint8_t)(x >> 2);
            if ((rnd() & 63) == 0) v = (uint8_t)rnd();
            px[y * 256 + x] = v;
        }
    }
    old = (uint8_t *)malloc(256 * 257 + 4096);
    oldLen = storedPng(px, 256, 256, rgb, 256, old);
    CHECK(pngWriteIndexed(px, 256, 256, 8, rgb, 256, NULL, 0, &png, &len) == 0);
    CHECK(len < oldLen / 2); /* it was stored: 65 KB */
    printf("  deflate: 256x256 tile %zu bytes stored, %zu compressed\n", oldLen, len);
    CHECK(pngRead(png, len, &p) == 0 && p.w == 256 && p.h == 256 && memcmp(p.px, px, 256 * 256) == 0);
    free(p.px);
    /* determinism: the manifest hash needs the same bytes every time */
    CHECK(pngWriteIndexed(px, 256, 256, 8, rgb, 256, NULL, 0, &png2, &len2) == 0 && len2 == len && !memcmp(png, png2, len));
    free(png2);
    /* the old stored file still reads, to the same pixels */
    CHECK(pngRead(old, oldLen, &p) == 0 && memcmp(p.px, px, 256 * 256) == 0);
    free(p.px);
    free(png);
    free(old);
    free(px);

    /* shapes: 1x1, a row, a column, 4-bit odd widths, all-equal, all-different, noise, long runs (the 258 match
       limit and the 32 KB window), and a size past one 32768-token block */
    for (k = 0; k < 12; k++) {
        static const int W[12] = {1, 300, 1, 7, 63, 64, 500, 513, 40, 600, 700, 1000};
        static const int H[12] = {1, 1, 300, 5, 9, 64, 500, 100, 40, 300, 700, 120};
        int w = W[k], h = H[k], depth = (k == 3 || k == 4 || k == 5) ? 4 : 8, nc = depth == 4 ? 16 : 256;
        px = (uint8_t *)malloc((size_t)w * h);
        for (i = 0; i < w * h; i++) {
            switch (k % 4) {
            case 0: px[i] = 9 % nc; break;
            case 1: px[i] = (uint8_t)(rnd() % nc); break;
            case 2: px[i] = (uint8_t)((i / 97 + i % 3) % nc); break;
            default: px[i] = (uint8_t)(((i % 1000) < 700 ? 3 : rnd()) % nc); break;
            }
        }
        png = NULL;
        CHECK(pngWriteIndexed(px, w, h, depth, rgb, nc, NULL, 0, &png, &len) == 0);
        CHECK(pngRead(png, len, &p) == 0 && p.w == w && p.h == h && p.depth == depth && memcmp(p.px, px, (size_t)w * h) == 0);
        if (k == 0 || k == 4 || k == 8) {
            size_t stored = ((size_t)(depth == 4 ? (w + 1) / 2 : w) + 1) * h;
            CHECK(len < stored + 8 + 25 + 12 + (size_t)nc * 3 + 12 + 12 + 40); /* PNG framing and zlib framing, no more: never much bigger than stored */
        }
        free(p.px);
        free(png);
        free(px);
    }
}


/* ---- S9: the sprites a mod brings (sprite_data.c: sprBank*, sprMaxFrame, the VRAM band) ---- */

typedef struct MemPngs {
    const char *name[4];
    uint8_t *png[4];
    size_t len[4];
} MemPngs;

static int memPng(void *ctx, const char *name, uint8_t **data, size_t *len)
{
    MemPngs *m = (MemPngs *)ctx;
    int i;
    for (i = 0; i < 4; i++) {
        if (m->name[i] && !strcmp(m->name[i], name)) {
            *data = (uint8_t *)malloc(m->len[i]);
            memcpy(*data, m->png[i], m->len[i]);
            *len = m->len[i];
            return 0;
        }
    }
    return -1;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

static void testModSprites(void)
{
    static const char good[] =
        "{\"format\":\"sprite-v1\",\"actor_width\":24,"
        " \"frames\":[{\"id\":10,\"image\":\"a.png\"},{\"id\":11,\"image\":\"b.png\"}],"
        " \"animations\":[{\"id\":20,\"loop\":true,\"steps\":[{\"frame\":10,\"time\":9,\"x\":-14,\"y\":-30},{\"frame\":11,\"time\":7,\"x\":-14,\"y\":-31}]},"
        "                 {\"id\":21,\"loop\":false,\"steps\":[{\"frame\":11,\"time\":5,\"x\":-14,\"y\":-31}]}],"
        " \"sets\":[{\"id\":1,\"animations\":[20,20,21,21,21]},{\"id\":4,\"animations\":[20]}]}";
    uint8_t px1[30 * 40], px2[26 * 36], *png1, *png2, *bank;
    uint8_t rgb[16 * 3];
    uint8_t alpha[1] = {0};
    size_t l1, l2, bl = 0;
    MemPngs m;
    SprBank *b = sprBankNew();
    SprInfo in, in2;
    char why[200];
    const uint8_t *e;
    int i;

    for (i = 0; i < 16; i++) {
        rgb[3 * i] = (uint8_t)(i * 16);
        rgb[3 * i + 1] = (uint8_t)(255 - i * 16);
        rgb[3 * i + 2] = 40;
    }
    for (i = 0; i < 30 * 40; i++) px1[i] = (uint8_t)(i % 16);
    for (i = 0; i < 26 * 36; i++) px2[i] = (uint8_t)((i / 3) % 16);
    CHECK(pngWriteIndexed(px1, 30, 40, 4, rgb, 16, alpha, 1, &png1, &l1) == 0);
    CHECK(pngWriteIndexed(px2, 26, 36, 4, rgb, 16, alpha, 1, &png2, &l2) == 0);
    memset(&m, 0, sizeof m);
    m.name[0] = "a.png";
    m.png[0] = png1;
    m.len[0] = l1;
    m.name[1] = "b.png";
    m.png[1] = png2;
    m.len[1] = l2;

    why[0] = 0;
    CHECK(sprBankAdd(b, good, strlen(good), memPng, &m, &in, why, sizeof why) == 0);
    /* frames, animations, set table: 2 + 2 + 1 entries; the width pads to whole halfwords, the slot is the largest frame an animation uses */
    CHECK(sprBankEntries(b) == 5 && in.entry == 4 && in.w == 32 && in.h == 40 && in.width == 24 && in.frames == 2 && in.anims == 2 && in.sets == 2);
    bank = sprBankBuild(b, &bl);
    CHECK(bank != NULL && rd32(bank) == 20);
    if (bank) {
        /* entry 0: a 4-bit TIM with a 16-colour CLUT; index 0 transparent; the pixels are the PNG's */
        e = bank + rd32(bank);
        CHECK(rd32(e) == 0x10 && rd32(e + 4) == 8 && rd16(e + 8 + 8) == 16 && rd16(e + 8 + 10) == 1);
        CHECK(rd16(e + 20) == 0); /* the tRNS entry is the transparent colour */
        CHECK(rd16(e + 8 + 44 + 8) == 8 && rd16(e + 8 + 44 + 10) == 40); /* 30 pixels padded to 32: 8 halfwords */
        CHECK((rd16(e + 8 + 44 + 12) & 0xFFFF) == (unsigned)(px1[0] | px1[1] << 4 | px1[2] << 8 | px1[3] << 12));
        /* entry 2: the animation, steps of (time, frame entry, x, y) and a 0 word that loops; entry 3 ends on -1 */
        e = bank + rd32(bank + 8);
        CHECK(rd32(e) == 9 && rd32(e + 4) == 0 && (int32_t)rd32(e + 8) == -14 && (int32_t)rd32(e + 12) == -30);
        CHECK(rd32(e + 16) == 7 && rd32(e + 20) == 1 && rd32(e + 32) == 0);
        e = bank + rd32(bank + 12);
        CHECK(rd32(e) == 5 && rd32(e + 4) == 1 && rd32(e + 16) == 0xFFFFFFFFu);
        /* the set table: (id, five animation entries), the single-animation set repeats it, and a zero record ends it */
        e = bank + rd32(bank + 16);
        CHECK(rd16(e) == 1 && rd16(e + 2) == 2 && rd16(e + 4) == 2 && rd16(e + 6) == 3 && rd16(e + 10) == 3);
        CHECK(rd16(e + 12) == 4 && rd16(e + 14) == 2 && rd16(e + 22) == 2 && rd16(e + 24) == 0);
    }
    /* a second sprite goes after the first: its entries are the bank's, so the file is shared */
    CHECK(sprBankAdd(b, good, strlen(good), memPng, &m, &in2, why, sizeof why) == 0 && in2.entry == 9 && sprBankEntries(b) == 10);
    free(bank);
    /* the engine's own reader (the disc's sprite files) agrees: the largest used frame from the set table */
    bank = sprBankBuild(b, &bl);
    {
        int fw = 0, fh = 0;
        CHECK(bank && sprMaxFrame(bank, bl, in.entry, &fw, &fh) == 0 && fw == 32 && fh == 40);
    }
    free(bank);

    /* refusals, each leaves the bank as it was */
    {
        static const char *const bad[] = {
            "{\"format\":\"sprite-v2\",\"frames\":[],\"animations\":[],\"sets\":[]}",
            "{\"format\":\"sprite-v1\",\"frames\":[{\"id\":1,\"image\":\"a.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":9,\"time\":1,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[2]}]}",
            "{\"format\":\"sprite-v1\",\"frames\":[{\"id\":1,\"image\":\"a.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":1,\"time\":0,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[2]}]}",
            "{\"format\":\"sprite-v1\",\"frames\":[{\"id\":1,\"image\":\"a.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":1,\"time\":1,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[7]}]}",
            "{\"format\":\"sprite-v1\",\"frames\":[{\"id\":1,\"image\":\"a.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":1,\"time\":1,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[2,2]}]}",
            "{\"format\":\"sprite-v1\",\"frames\":[{\"id\":1,\"image\":\"../a.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":1,\"time\":1,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[2]}]}",
            "{\"format\":\"sprite-v1\",\"frames\":[{\"id\":1,\"image\":\"zz.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":1,\"time\":1,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[2]}]}",
            "{\"format\":\"sprite-v1\",\"frames\":[{\"id\":1,\"image\":\"a.png\"},{\"id\":1,\"image\":\"b.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":1,\"time\":1,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[2]}]}",
            "{\"format\":\"sprite-v1\",\"actor_width\":4,\"frames\":[{\"id\":1,\"image\":\"a.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":1,\"time\":1,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[2]}]}",
        };
        int n = sprBankEntries(b);
        for (i = 0; i < (int)(sizeof bad / sizeof *bad); i++) {
            why[0] = 0;
            CHECK(sprBankAdd(b, bad[i], strlen(bad[i]), memPng, &m, &in, why, sizeof why) != 0 && why[0] != 0);
            CHECK(sprBankEntries(b) == n);
        }
    }
    {
        /* 17 colours, and a frame wider than a texture page */
        uint8_t rgb17[17 * 3] = {0}, px17[4 * 4], pxw[260 * 2], *p17, *pw;
        size_t l17, lw;
        static const char one[] =
            "{\"format\":\"sprite-v1\",\"frames\":[{\"id\":1,\"image\":\"a.png\"}],\"animations\":[{\"id\":2,\"steps\":[{\"frame\":1,\"time\":1,\"x\":0,\"y\":0}]}],\"sets\":[{\"id\":1,\"animations\":[2]}]}";
        MemPngs m2;

        for (i = 0; i < 16; i++) px17[i] = (uint8_t)i;
        px17[15] = 16;
        for (i = 0; i < 17; i++) rgb17[3 * i] = (uint8_t)i;
        CHECK(pngWriteIndexed(px17, 4, 4, 8, rgb17, 17, NULL, 0, &p17, &l17) == 0);
        memset(pxw, 1, sizeof pxw);
        CHECK(pngWriteIndexed(pxw, 260, 2, 4, rgb, 16, NULL, 0, &pw, &lw) == 0);
        memset(&m2, 0, sizeof m2);
        m2.name[0] = "a.png";
        m2.png[0] = p17;
        m2.len[0] = l17;
        CHECK(sprBankAdd(b, one, strlen(one), memPng, &m2, &in, why, sizeof why) != 0 && strstr(why, "16"));
        m2.png[0] = pw;
        m2.len[0] = lw;
        CHECK(sprBankAdd(b, one, strlen(one), memPng, &m2, &in, why, sizeof why) != 0 && strstr(why, "260x2"));
        free(p17);
        free(pw);
    }
    sprBankFree(b);
    free(png1);
    free(png2);

    /* the VRAM band: slots stay inside one texture page and apart, palettes fill rows of 20 */
    {
        SprVram v;
        SprSlot s[400];
        int n = 0, j, ok = 1;

        sprVramReset(&v);
        while (n < 400 && sprVramAlloc(&v, 32, 48, &s[n]) == 0) n++;
        /* five pages of 64 halfwords hold eight 32-pixel slots a row and four rows of 48 */
        CHECK(n == SPR_BAND_PAGES * 8 * (SPR_BAND_ROWS / 48));
        for (i = 0; i < n; i++) {
            int hw = 8;
            if (s[i].x % 64 || s[i].imageX < s[i].x || s[i].imageX + hw > s[i].x + 64 || s[i].u != (s[i].imageX - s[i].x) * 4 ||
                s[i].imageY < SPR_BAND_Y || s[i].imageY + 48 > SPR_BAND_Y + SPR_BAND_ROWS || s[i].y != SPR_BAND_Y || s[i].v != s[i].imageY - SPR_BAND_Y || s[i].u + 32 > 256 ||
                s[i].clutX % 16 || s[i].clutY < SPR_CLUT_Y || s[i].clutY >= SPR_CLUT_Y + SPR_CLUT_ROWS)
                ok = 0;
            for (j = 0; j < i && ok; j++) {
                if (s[i].imageX < s[j].imageX + 8 && s[j].imageX < s[i].imageX + 8 && s[i].imageY < s[j].imageY + 48 && s[j].imageY < s[i].imageY + 48) ok = 0;
                if (s[i].clutX == s[j].clutX && s[i].clutY == s[j].clutY) ok = 0;
            }
        }
        CHECK(ok);
        CHECK(sprVramAlloc(&v, 32, 48, &s[0]) != 0);
        sprVramReset(&v);
        CHECK(sprVramAlloc(&v, 256, 224, &s[0]) == 0 && s[0].u == 0 && s[0].imageY == SPR_BAND_Y);
        CHECK(sprVramAlloc(&v, 257, 8, &s[1]) != 0 && sprVramAlloc(&v, 8, 225, &s[1]) != 0 && sprVramAlloc(&v, 0, 8, &s[1]) != 0);
        sprVramReset(&v);
        /* different sizes share shelves when the heights are close, and a failed allocation takes nothing */
        CHECK(sprVramAlloc(&v, 32, 48, &s[0]) == 0 && sprVramAlloc(&v, 28, 44, &s[1]) == 0 && s[1].imageY == s[0].imageY && s[1].imageX == s[0].imageX + 8);
        CHECK(sprVramCapacity(&v, 32, 48) == n - 2);
        for (i = 0; i < SPR_CLUTS_MAX; i++) {
            sprVramReset(&v);
            v.cluts = i;
            if (sprVramAlloc(&v, 8, 8, &s[0]) != 0) break;
        }
        CHECK(i == SPR_CLUTS_MAX);
    }
}

/* A stage's object palettes by piece (A6.1). The 8-bit TIM is entry 0 here, so the pages shift (page 0 is its image) and its 3-row CLUT is
   at row 256 of the sheet's space: an 8-bit part at clutY 257 is CLUT row 1, and a frame that adds a row reads CLUT row 2. Page 1 holds
   4-bit art with a DECOY palette under it, the part's real palette is on page 2 (column 128 + 48), row 2, and the frame row +1 is row 3. */
static int palIs(const uint16_t *pal, int n, uint16_t (*want)(int))
{
    int i, ok = 1;
    for (i = 0; i < n; i++) ok &= pal[i] == want(i);
    return ok;
}
static uint16_t wantReal4(int i) { return rgb555(31, i, 2); }
static uint16_t wantReal4Row(int i) { return rgb555(2, i, 31); }
static uint16_t wantClut1(int i) { return i ? rgb555(i & 31, 9, 1) : 0; }
static uint16_t wantClut2(int i) { return i ? rgb555(1, i & 31, 7) : 0; }

static void testStagePartPalette(void)
{
    uint16_t c8[3 * 256], img0[64 * 4], pg1[64 * 4], pg2[64 * 4], pg3[64 * 4], pal[256];
    uint8_t t0[8192], t1[4096], t2[4096], t3[4096], z1[8192], z2[8192], z3[8192], mask[32768];
    const uint8_t *me[4];
    size_t ml[4], mn, n0, n1, n2, n3;
    ImgPages *pg;
    ImgPart part;
    ImgPalAt at;
    int i;

    memset(c8, 0, sizeof c8);
    for (i = 0; i < 256; i++) {
        c8[256 + i] = wantClut1(i);
        c8[512 + i] = wantClut2(i);
        c8[i] = rgb555(0, 0, 31); /* CLUT row 0: what a reader that forgets the -256 would get */
    }
    memset(img0, 0, sizeof img0);
    memset(pg1, 0, sizeof pg1);
    memset(pg2, 0, sizeof pg2);
    memset(pg3, 0, sizeof pg3);
    for (i = 0; i < 16; i++) {
        pg1[2 * 64 + 48 + i] = rgb555(i, 31, i); /* the decoy: under the pixels of page 1, where the part's column 48 would read the page of the pixels */
        pg2[2 * 64 + 48 + i] = wantReal4(i);
        pg2[3 * 64 + 48 + i] = wantReal4Row(i);
    }
    pg1[0] = 0x4321;
    pg3[0] = 0x0403;
    n0 = makeTim(t0, 1, 256, 3, c8, 64, 4, img0);
    n1 = makeTim(t1, 2, 0, 0, NULL, 64, 4, pg1);
    n2 = makeTim(t2, 2, 0, 0, NULL, 64, 4, pg2);
    n3 = makeTim(t3, 2, 0, 0, NULL, 64, 4, pg3);
    me[0] = t0; ml[0] = n0;
    me[1] = z1; ml[1] = rlenPack(t1, n1, z1);
    me[2] = z2; ml[2] = rlenPack(t2, n2, z2);
    me[3] = z3; ml[3] = rlenPack(t3, n3, z3);
    mn = makeArchive(mask, me, ml, 4, 1);

    pg = imgStagePagesOpen(mask, mn);
    CHECK(pg != NULL && imgPagesCount(pg) == 4);
    if (!pg) return;
    memset(&part, 0, sizeof part);
    /* 4-bit: the palette is on page 2, not under the pixels of page 1 */
    part.u = 256; part.w = 16; part.h = 4; part.clutX = 128 + 48; part.clutY = 2; part.mode = 0;
    CHECK(imgPartPalette(pg, &part, 0, pal, &at) == 0 && at.page == 2 && at.x == 48 && at.y == 2 && palIs(pal, 16, wantReal4));
    /* the frame's CLUT row +1 */
    CHECK(imgPartPalette(pg, &part, 1, pal, &at) == 0 && at.y == 3 && palIs(pal, 16, wantReal4Row));
    /* 8-bit: clutY 256 + r is CLUT row r of the archive's TIM, and the frame's row adds to it */
    part.mode = 1; part.clutX = 0; part.clutY = 257;
    CHECK(imgPartPalette(pg, &part, 0, pal, &at) == 0 && at.page == -1 && palIs(pal, 256, wantClut1));
    CHECK(imgPartPalette(pg, &part, 1, pal, &at) == 0 && palIs(pal, 256, wantClut2));
    CHECK(imgPartPalette(pg, &part, -1, pal, &at) == 0 && pal[1] == rgb555(0, 0, 31)); /* row 256 is CLUT row 0 */
    CHECK(imgPartPalette(pg, &part, -2, pal, &at) != 0); /* below the CLUT rows the loader wrote */
    CHECK(imgPartPalette(pg, &part, 2, pal, &at) != 0);  /* CLUT row 3 of 3 */
    part.clutX = 1;
    CHECK(imgPartPalette(pg, &part, 0, pal, &at) != 0);  /* 256 colours from column 1: past the CLUT's 256 halfwords */
    part.clutX = 0; part.clutY = 100;
    CHECK(imgPartPalette(pg, &part, 0, pal, &at) != 0);  /* an 8-bit part below row 256 is not in the CLUTs (and not in a page: no guess) */
    /* a 4-bit part past the pages' rows: VRAM below them, not data */
    part.mode = 0; part.clutX = 128 + 48; part.clutY = 2;
    CHECK(imgPartPalette(pg, &part, 3, pal, &at) != 0);
    imgPagesFree(pg);
}

int main(void)
{
    testPngRead();
    testDeflate();
    testSubstitute();
    testPng();
    testDecode();
    testWalkAndNames();
    testSheet();
    testScreenPalette();
    testAtlas();
    testExtract();
    testSprites();
    testFieldCom();
    testOverrides();
    testSpriteOverrides();
    testCardPak();
    testCardData();
    testStageMask();
    testStagePartPalette();
    testModSprites();
    if (failures) {
        printf("imagetest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("imagetest: OK\n");
    return 0;
}
