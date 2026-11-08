/*
 * Host test of the menus by piece (readable-assets A5.2): pc/shim/menu_export.c and the sheet/page readers it uses in image_data.c. A synthetic screen
 * (four 16-bit pages of 64 x 16 halfwords and a sheet with seven parts in three frames) and a synthetic raw disc in memory; no SDL, no retail bytes.
 *
 * Everything the exporter reports is checked against tables kept in this file and read back with the test's own code: where a texel lives in the pages,
 * where a palette sits, how a frame is composed. Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/exp_core.h"
#include "shim/menu_export.h"
#include "shim/png_read.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "menuexptest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- the synthetic screen ---- */

#define PAGES 4
#define PW 64 /* halfwords of a page */
#define PH 16

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
    /* palettes: entry 0 is 0x0000 (transparent) in all of them, the others are told apart by (colour, row, page) */
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

/* ---- the test's own readers ---- */

/* the index of texel column u, row v of a part of `mode` as VRAM holds the pages side by side: -1 outside them */
static int texel(int u, int v, int mode) {
    int per = mode ? 2 : 4, hx = u / per, page = hx / PW;
    unsigned h;

    if (u < 0 || v < 0 || page >= PAGES || v >= PH) return -1;
    h = gPage[page][v * PW + hx % PW];
    return mode ? (int)((h >> (8 * (u % 2))) & 255) : (int)((h >> (4 * (u % 4))) & 15);
}

/* halfword i of the palette at column x, row y of the pages' space: -1 outside */
static int palHalf(int x, int y, int i) {
    int cx = x + i;

    if (cx < 0 || y < 0 || cx / PW >= PAGES || y >= PH) return -1;
    return gPage[cx / PW][y * PW + cx % PW];
}

static void expand(int half, uint8_t rgb[3]) {
    int r = half & 31, g = (half >> 5) & 31, b = (half >> 10) & 31;

    rgb[0] = (uint8_t)((r << 3) | (r >> 2));
    rgb[1] = (uint8_t)((g << 3) | (g >> 2));
    rgb[2] = (uint8_t)((b << 3) | (b >> 2));
}

/* ---- the files of the screen ---- */

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

/* the *TM file: an archive holding a sub-archive of the four raw 16-bit pages (no RLEN) */
static size_t makeTm(uint8_t *out) {
    static uint8_t tim[PAGES][8 + 12 + 2 * PW * PH], sub[PAGES * (8 + 12 + 2 * PW * PH) + 64];
    const uint8_t *ents[PAGES];
    size_t lens[PAGES], subLen;
    const uint8_t *top[1];
    size_t topLens[1];
    int k, i;

    for (k = 0; k < PAGES; k++) {
        w32(tim[k], 0x10);
        w32(tim[k] + 4, 2);
        w32(tim[k] + 8, 12 + 2 * PW * PH);
        w16(tim[k] + 12, 0);
        w16(tim[k] + 14, 0);
        w16(tim[k] + 16, PW);
        w16(tim[k] + 18, PH);
        for (i = 0; i < PW * PH; i++) w16(tim[k] + 20 + 2 * i, gPage[k][i]);
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

/* ---- what the exporter emits ---- */

typedef struct Out {
    char name[32];
    uint8_t *data;
    size_t len;
} Out;

typedef struct Sink {
    Out f[16];
    int n;
} Sink;

static int sinkEmit(void *ctx, const char *rel, const uint8_t *data, size_t len) {
    Sink *s = (Sink *)ctx;

    if (s->n >= 16) return -1;
    snprintf(s->f[s->n].name, sizeof s->f[s->n].name, "%s", rel);
    s->f[s->n].data = (uint8_t *)malloc(len + 1);
    memcpy(s->f[s->n].data, data, len);
    s->f[s->n].data[len] = 0;
    s->f[s->n].len = len;
    s->n++;
    return 0;
}

static const Out *sinkFind(const Sink *s, const char *name) {
    int i;

    for (i = 0; i < s->n; i++) {
        if (strcmp(s->f[i].name, name) == 0) return &s->f[i];
    }
    return NULL;
}

static void sinkFree(Sink *s) {
    int i;

    for (i = 0; i < s->n; i++) free(s->f[i].data);
    s->n = 0;
}

/* one PNG against the pages: palette, depth and every pixel */
static void checkPng(const Sink *s, const char *name, const Part *p, int row) {
    const Out *o = sinkFind(s, name);
    PngImage im;
    char why[80];
    int n = p->mode ? 256 : 16, i, x, y, okPal = 1, okPix = 1;

    CHECK(o != NULL);
    if (o == NULL || pngReadIndexed(o->data, o->len, &im, why, sizeof why) != 0) {
        CHECK(!"the PNG reads");
        return;
    }
    CHECK(im.w == p->w && im.h == p->h && im.depth == (p->mode ? 8 : 4) && im.nPal == n);
    for (i = 0; i < n; i++) {
        uint8_t rgb[3];
        int half = palHalf(p->clutX, p->clutY + row, i);

        expand(half, rgb);
        if (memcmp(im.rgb + 3 * i, rgb, 3) != 0) okPal = 0;
        if ((i < im.nAlpha ? im.alpha[i] : 255) != (half == 0 ? 0 : 255)) okPal = 0;
    }
    CHECK(okPal);
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            int t = texel(p->u + x, p->v + y, p->mode);

            if (im.px[y * p->w + x] != (t < 0 ? 0 : t)) okPix = 0;
        }
    }
    CHECK(okPix);
    pngImageFree(&im);
}

/* ---- menu.json read back ---- */

/* the integer after `key` in `s` (the first occurrence), `dflt` when absent */
static int jint(const char *s, const char *key, int dflt) {
    const char *p = strstr(s, key);

    return p != NULL ? atoi(p + strlen(key)) : dflt;
}

#define CANVAS_W 64
#define CANVAS_H 48
typedef uint32_t Canvas[CANVAS_W * CANVAS_H]; /* 0: nothing, else 0xFF000000 | r << 16 | g << 8 | b */

static uint32_t rgbOf(const uint8_t *rgb) { return 0xFF000000u | (uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2]; }

/* frame `f` composed straight from the tables and the pages: parts from the last to the first, each with its own palette */
static void expectedFrame(const Frame *f, Canvas c) {
    int k, x, y;

    memset(c, 0, sizeof(Canvas));
    for (k = f->n - 1; k >= 0; k--) {
        const Part *p;

        if (f->item[k].part >= 7) continue;
        p = &kParts[f->item[k].part];
        if (p->w <= 0 || p->h <= 0 || palHalf(p->clutX, p->clutY + f->row, 0) < 0) continue;
        for (y = 0; y < p->h; y++) {
            for (x = 0; x < p->w; x++) {
                int t = texel(p->u + x, p->v + y, p->mode), half;
                uint8_t rgb[3];

                if (t < 0) continue;
                half = palHalf(p->clutX, p->clutY + f->row, t);
                if (half <= 0) continue; /* transparent */
                expand(half, rgb);
                c[(f->item[k].y + y) * CANVAS_W + f->item[k].x + x] = rgbOf(rgb);
            }
        }
    }
}

/* frame `id` composed from the exported PNGs alone, placed by what menu.json says: the pieces that list the frame, the last slot first */
static void actualFrame(const Sink *s, int id, Canvas c) {
    const Out *js = sinkFind(s, "menu.json");
    int slot;

    memset(c, 0, sizeof(Canvas));
    for (slot = 9; slot >= 0; slot--) {
        const char *line = (const char *)js->data;

        while ((line = strstr(line, "\n  { \"file\": \"")) != NULL) {
            const char *end = strchr(line + 1, '\n'), *use;
            char one[4096], name[32];
            size_t ll = (size_t)(end - line);

            line++;
            if (ll >= sizeof one) continue;
            memcpy(one, line, ll - 1);
            one[ll - 1] = 0;
            if (sscanf(one, "  { \"file\": \"%31[^\"]\"", name) != 1) continue;
            for (use = strstr(one, "\"frames\": ["); use != NULL && (use = strstr(use + 1, "\"frame\": ")) != NULL;) {
                if (jint(use, "\"frame\": ", -1) == id && jint(use, "\"slot\": ", -1) == slot) {
                    const Out *o = sinkFind(s, name);
                    const char *at = strstr(use, "\"at\": [");
                    PngImage im;
                    int ax = atoi(at + 7), ay = atoi(strchr(at + 7, ',') + 1), x, y;

                    if (o == NULL || pngReadIndexed(o->data, o->len, &im, NULL, 0) != 0) {
                        CHECK(!"a listed PNG reads");
                        continue;
                    }
                    for (y = 0; y < im.h; y++) {
                        for (x = 0; x < im.w; x++) {
                            int i = im.px[y * im.w + x];

                            if (i < im.nAlpha && im.alpha[i] == 0) continue;
                            c[(ay + y) * CANVAS_W + ax + x] = rgbOf(im.rgb + 3 * i);
                        }
                    }
                    pngImageFree(&im);
                }
            }
            line = end;
        }
    }
}

static void testScreen(void) {
    static uint8_t tm[PAGES * 2200 + 256], cs[512];
    size_t tl, cl;
    Sink s;
    MenuStats st;
    char *js;
    int i;

    pagesFill();
    tl = makeTm(tm);
    cl = makeCs(cs);
    memset(&s, 0, sizeof s);
    memset(&st, 0, sizeof st);
    CHECK(menuScreenExport("stmcrd", "AAA/DAT/MCARD/STMCRDTM.BIN", "AAA/DAT/MCARD/STMCRDCS.BIN", tm, tl, cs, cl, sinkEmit, &s, &st) == 5);
    CHECK(st.pieces == 5 && st.unexported == 2 && st.outside == 1 && st.failed == 0);
    /* one PNG per (rect, palette): the rectangle of part 0 with CLUT row 0 and row 1 is two, part 2 (the same rectangle and palette as part 0 in frame 1) adds none */
    CHECK(s.n == 6 && sinkFind(&s, "menu.json") != NULL);
    CHECK(sinkFind(&s, "f000p00.png") && sinkFind(&s, "f000p01.png") && sinkFind(&s, "f001p00.png") && sinkFind(&s, "f001p02.png") && sinkFind(&s, "f005p00.png"));
    CHECK(sinkFind(&s, "f001p01.png") == NULL && sinkFind(&s, "f005p01.png") == NULL && sinkFind(&s, "f005p02.png") == NULL);
    /* the palette of each is the halfwords at its CLUT position plus the frame's row (and a frame with +1 row makes a second PNG) */
    checkPng(&s, "f000p00.png", &kParts[0], 0);
    checkPng(&s, "f000p01.png", &kParts[1], 0);
    checkPng(&s, "f001p00.png", &kParts[0], 1);
    checkPng(&s, "f001p02.png", &kParts[3], 1);
    checkPng(&s, "f005p00.png", &kParts[4], 0);
    {
        PngImage a, b;
        const Out *o0 = sinkFind(&s, "f000p00.png"), *o1 = sinkFind(&s, "f001p00.png");

        CHECK(pngReadIndexed(o0->data, o0->len, &a, NULL, 0) == 0 && pngReadIndexed(o1->data, o1->len, &b, NULL, 0) == 0);
        CHECK(memcmp(a.px, b.px, (size_t)a.w * (size_t)a.h) == 0);     /* the same pixels ... */
        CHECK(memcmp(a.rgb, b.rgb, 3 * 16) != 0);                      /* ... in another palette */
        pngImageFree(&a);
        pngImageFree(&b);
    }
    /* menu.json */
    js = (char *)sinkFind(&s, "menu.json")->data;
    CHECK(strstr(js, "\"format\": \"menus-menu-v1\"") && strstr(js, "\"name\": \"stmcrd\""));
    CHECK(strstr(js, "\"files\": [\"DAT/MCARD/STMCRDTM.BIN\", \"DAT/MCARD/STMCRDCS.BIN\"]"));
    CHECK(strstr(js, "{ \"file\": \"f000p00.png\", \"page\": 0, \"u\": 0, \"v\": 0, \"w\": 16, \"h\": 8, \"mode\": \"4bit\", \"clut\": { \"x\": 48, \"row\": 14, \"page\": 0 },"));
    CHECK(strstr(js, "{ \"file\": \"f000p01.png\", \"page\": 1, \"u\": 264, \"v\": 0, \"w\": 20, \"h\": 6, \"mode\": \"4bit\", \"clut\": { \"x\": 80, \"row\": 14, \"page\": 1 },"));
    CHECK(strstr(js, "{ \"file\": \"f001p00.png\", \"page\": 0, \"u\": 0, \"v\": 0, \"w\": 16, \"h\": 8, \"mode\": \"4bit\", \"clut\": { \"x\": 48, \"row\": 15, \"page\": 0 },"));
    CHECK(strstr(js, "\"mode\": \"8bit\", \"clut\": { \"x\": 0, \"row\": 13, \"page\": 0 }"));
    CHECK(strstr(js, "{ \"frame\": 0, \"slot\": 0, \"at\": [0, 0], \"blend\": \"none\" }"));
    CHECK(strstr(js, "{ \"frame\": 1, \"slot\": 0, \"at\": [0, 0], \"blend\": \"add\" }, { \"frame\": 1, \"slot\": 1, \"at\": [3, 2], \"blend\": \"add\" }"));
    CHECK(strstr(js, "{ \"frame\": 5, \"slot\": 0, \"at\": [5, 5], \"blend\": \"subtract\" }"));
    /* the piece that reaches past the pages says so, the two that cannot be cut say why and keep their frames */
    CHECK(strstr(js, "\"file\": \"f001p02.png\", \"page\": 0, \"u\": 250, \"v\": 14, \"w\": 12, \"h\": 4, \"mode\": \"4bit\", \"clut\": { \"x\": 48, \"row\": 15, \"page\": 0 }, \"outside\": true, \"frames\""));
    CHECK(strstr(js, "\"file\": null, \"unexported\": \"empty rectangle\", \"page\": 0, \"u\": 0, \"v\": 0, \"w\": 0, \"h\": 5"));
    CHECK(strstr(js, "\"file\": null, \"unexported\": \"palette outside the pages\""));
    CHECK(strstr(js, "{ \"frame\": 5, \"slot\": 2, \"at\": [0, 0], \"blend\": \"subtract\" }"));
    CHECK(strstr(js, "\"pages\": [[64, 16], [64, 16], [64, 16], [64, 16]]"));
    /* the strong check: every frame repainted from the exported PNGs at the offsets menu.json gives is the frame composed from the pages, each part with its own palette */
    for (i = 0; i < 3; i++) {
        static Canvas want, got;
        int k, same = 1, visible = 0;

        expectedFrame(&kFrames[i], want);
        actualFrame(&s, kFrames[i].id, got);
        for (k = 0; k < CANVAS_W * CANVAS_H; k++) {
            if (want[k] != got[k]) same = 0;
            if (want[k]) visible++;
        }
        CHECK(same);
        CHECK(visible > 20); /* not an empty comparison */
    }
    sinkFree(&s);
    /* not a screen: no sheet, no pages */
    CHECK(menuScreenExport("x", "a", "b", tm, tl, tm, tl, sinkEmit, &s, &st) == -1 && s.n == 0);
    CHECK(menuScreenExport("x", "a", "b", cs, cl, cs, cl, sinkEmit, &s, &st) == -1 && s.n == 0);
}

static void testNames(void) {
    char n[64];

    CHECK(menuScreenName("AAA/DAT/MCARD/STMCRDTM.BIN", n, sizeof n) && strcmp(n, "stmcrd") == 0);
    CHECK(menuScreenName("AAA/DAT/COUNTRY/CMFONTTM.BIN", n, sizeof n) && strcmp(n, "cmfont") == 0);
    CHECK(menuScreenName("AAA/DAT/SCREEN/STWMP0TM.BIN", n, sizeof n) && strcmp(n, "stwmp0") == 0);
    CHECK(!menuScreenName("AAA/DAT/SCREEN/STSHOPCS.BIN", n, sizeof n));   /* the sheet is not the pages */
    CHECK(!menuScreenName("AAA/DAT/SCREEN/TLOGOUSA.BIN", n, sizeof n));   /* a loose picture: its palette is in itself */
    CHECK(!menuScreenName("AAA/DAT/FIELD/SPRT/P066FRTM.BIN", n, sizeof n));
    CHECK(!menuScreenName("AAA/DAT/MCARD/STMCRDTM.BIN", n, 4));            /* does not fit */
}

/* ---- the disc side ---- */

typedef struct DFile {
    char path[112];
    long lba;
    size_t size;
    uint8_t *data;
} DFile;

static DFile gDisc[8];
static int gDiscN;
static uint8_t (*gSect)[2352];
static long gSectN, gNextLba = 4;
static char gData[1024];

static void discAdd(const char *path, const uint8_t *data, size_t size) {
    DFile *f = &gDisc[gDiscN++];
    long sectors = (long)((size + 2047) / 2048), i;

    snprintf(f->path, sizeof f->path, "%s", path);
    f->lba = gNextLba;
    f->size = size;
    gNextLba += sectors;
    gSect = (uint8_t(*)[2352])realloc(gSect, (size_t)gNextLba * 2352);
    memset(gSect + gSectN, 0, (size_t)(gNextLba - gSectN) * 2352);
    gSectN = gNextLba;
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
    memcpy(frame, gSect[lba], 2352);
    return 1;
}

static int exists(const char *rel) {
    char p[1400];

    snprintf(p, sizeof p, "%s/%s", gData, rel);
    return access(p, F_OK) == 0;
}

static void testRun(void) {
    static uint8_t tm[PAGES * 2200 + 256], cs[512];
    MenuExportIo io = {ioList, ioRead, NULL};
    MenuExportStats st;
    ExpManifest man;
    char p[1400], rm[1500];
    uint8_t one[64] = {0};
    size_t tl, cl;

    tl = makeTm(tm);
    cl = makeCs(cs);
    discAdd("AAA/DAT/MCARD/STMCRDCS.BIN", cs, cl);
    discAdd("AAA/DAT/MCARD/STMCRDTM.BIN", tm, tl);
    discAdd("AAA/DAT/SCREEN/STSHOPTM.BIN", tm, tl); /* pages without their sheet: left out and logged */
    discAdd("AAA/DAT/SCREEN/TLOGOUSA.BIN", tm, tl); /* a loose picture: not a screen */
    discAdd("AAA/DAT/SCREEN/STFRPTCS.BIN", cs, cl);
    discAdd("AAA/DAT/SCREEN/STFRPTTM.BIN", tm, tl); /* a second screen */

    snprintf(gData, sizeof gData, "%s/menuexptest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(gData) != NULL);
    /* menus/manifest.json is the topic's one manifest: what the partner bundles recorded in it (a gym effect set) stays */
    snprintf(p, sizeof p, "%s/menus/manifest.json", gData);
    expManOpen(&man, "test", p, "menus");
    expManSet(&man, "gym/effects/punch/001.png", "0123456789abcdef");
    expManMark(&man, "partners", 2);
    CHECK(expManSave(&man, 0) == 0);
    expManFree(&man);
    snprintf(p, sizeof p, "%s/menus/gym/effects/punch", gData);
    CHECK(tdMakeDirs(p) == 0);
    snprintf(p, sizeof p, "%s/menus/gym/effects/punch/001.png", gData);
    CHECK(tdWriteFileAtomic(p, one, sizeof one) == 0);

    menuExportSetup(gData, &io);
    CHECK(menuExportRun("nonsense") == -1);
    CHECK(menuExportRun("stfrpt") == 1); /* one screen by name; its files only */
    CHECK(exists("menus/stfrpt/menu.json") && exists("menus/stfrpt/f000p00.png") && !exists("menus/stmcrd/menu.json"));
    CHECK(menuExportRun("all") == 2); /* both screens with a sheet; the third has none */
    menuExportTotals(&st);
    CHECK(st.screens == 2 && st.failed == 0 && st.pieces == 10 && st.unexported == 4 && st.outside == 2);
    CHECK(exists("menus/stmcrd/menu.json") && exists("menus/stmcrd/f001p00.png") && !exists("menus/stshop/menu.json") && !exists("menus/tlogousa/menu.json"));
    snprintf(p, sizeof p, "%s/menus/manifest.json", gData);
    expManOpen(&man, "test", p, "menus");
    CHECK(tdManifestFind(&man.m, "gym/effects/punch/001.png") != NULL);                    /* the partners' entries are kept */
    CHECK(expManIsCurrent(&man, "partners", 2) && expManIsCurrent(&man, "menus", MENU_EXPORT_VERSION) && tdManifestFind(&man.m, "exporter/menus") == NULL);
    CHECK(tdManifestFind(&man.m, "stmcrd/menu.json") != NULL && tdManifestFind(&man.m, "stmcrd/f000p00.png") != NULL);
    CHECK(tdManifestFind(&man.m, "stmcrd/f001p01.png") == NULL);                           /* only what was written */
    expManFree(&man);
    /* up to date: the disc is not read */
    CHECK(menuExportRun("all") == 0);
    menuExportTotals(&st);
    CHECK(st.skipped == 1);
    /* a file that went missing is made again */
    snprintf(p, sizeof p, "%s/menus/stmcrd/f000p00.png", gData);
    CHECK(remove(p) == 0);
    CHECK(menuExportRun("all") == 2 && exists("menus/stmcrd/f000p00.png"));
    snprintf(rm, sizeof rm, "rm -rf '%s'", gData);
    if (system(rm)) {
    }
    free(gSect);
    gSect = NULL;
}

int main(void) {
    testNames();
    testScreen();
    testRun();
    if (failures) {
        fprintf(stdout, "menuexptest: %d FAILED\n", failures);
        return 1;
    }
    fprintf(stdout, "menuexptest: all checks passed\n");
    return 0;
}
