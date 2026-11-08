/*
 * Host test of the stage objects by piece (readable-assets A6.2): pc/shim/stage_obj_export.c on the menus' cutting (menu_export.c) and the page and sheet readers of
 * image_data.c. A synthetic stage: a MASK file (an 8-bit TIM with a CLUT of three rows, then two raw 16-bit pages, one with the 4-bit art and one with its
 * palettes), a TMPK file whose sprite sheet is NOT entry 0, and a synthetic raw disc in memory; no SDL, no retail bytes.
 *
 * Everything the exporter reports is checked against tables kept in this file and read back with the test's own code: where a texel lives in the pages, where a
 * palette sits (a 4-bit one in the pages, an 8-bit one in the TIM's CLUT), how a frame is composed. The strong check repaints every frame from the exported PNGs
 * alone and compares it with the frame composed from the pages, each part with its own palette. Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/exp_core.h"
#include "shim/png_read.h"
#include "shim/stage_obj_export.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "stageobjexptest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- the synthetic stage ---- */

#define PW 64 /* halfwords of a page */
#define PH 16
#define CLUT_ROWS 3

static uint16_t gPage0[PW * PH]; /* the 8-bit image: 128 texels wide */
static uint16_t gPage1[PW * PH]; /* the 4-bit art, with a decoy palette under it */
static uint16_t gPage2[PW * PH]; /* the palettes of the 4-bit art */
static uint16_t gClut[CLUT_ROWS][256];

typedef struct Part {
    int u, v, w, h, clutX, clutY, mode;
} Part;

/* 0 is 4-bit on page 1 (its palette is on page 2, column 16, row 5); 1 is 8-bit on page 0 (CLUT row 1 of the TIM: clutY 257); 2 names part 0's rectangle and
   palette again; 3 is 4-bit with a palette row the pages do not have */
static const Part kParts[4] = {
    {256, 0, 16, 8, 128 + 16, 5, 0},
    {0, 2, 20, 6, 0, 257, 1},
    {256, 0, 16, 8, 128 + 16, 5, 0},
    {256, 8, 8, 4, 128 + 16, 40, 0},
};

typedef struct Item {
    int part, x, y;
} Item;

typedef struct Frame {
    int id, row, rate, n;
    Item item[4];
} Frame;

/* frame 1 adds a CLUT row: the same two rectangles are two more pieces, with the palette one row further */
static const Frame kFrames[3] = {
    {0, 0, -1, 2, {{0, 0, 0}, {1, 16, 0}}},
    {1, 1, 1, 3, {{0, 0, 0}, {2, 3, 2}, {1, 10, 10}}},
    {4, 0, 2, 1, {{3, 0, 0}}},
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

static void stageFill(void) {
    uint32_t seed = 777;
    int i, c, r;

    for (i = 0; i < PW * PH; i++) {
        seed = seed * 1103515245u + 12345u;
        gPage0[i] = (uint16_t)(seed >> 8);
        seed = seed * 1103515245u + 12345u;
        gPage1[i] = (uint16_t)(seed >> 8);
    }
    memset(gPage2, 0, sizeof gPage2);
    for (r = 0; r < CLUT_ROWS; r++) {
        for (c = 0; c < 256; c++) gClut[r][c] = c ? rgb555(c & 31, 9 + r, r * 5 + 1) : 0;
    }
    for (c = 1; c < 16; c++) {
        gPage1[5 * PW + 16 + c] = rgb555(c, 31, c); /* the decoy: where a reader that takes the palette from the page of the pixels would look */
        gPage2[5 * PW + 16 + c] = rgb555(c, 31 - c, 3);
        gPage2[6 * PW + 16 + c] = rgb555(3, c, 31 - c); /* the frame's row +1 */
    }
    gPage1[5 * PW + 16] = gPage2[5 * PW + 16] = gPage2[6 * PW + 16] = 0;
}

/* ---- the test's own readers ---- */

/* the index of texel column u, row v of a part of `mode` in the pages (page 0, 1, 2 side by side): -1 outside them */
static int texel(int u, int v, int mode) {
    const uint16_t *pg[3] = {gPage0, gPage1, gPage2};
    int per = mode ? 2 : 4, hx = u / per, page = hx / PW;
    unsigned h;

    if (u < 0 || v < 0 || page >= 3 || v >= PH) return -1;
    h = pg[page][v * PW + hx % PW];
    return mode ? (int)((h >> (8 * (u % 2))) & 255) : (int)((h >> (4 * (u % 4))) & 15);
}

/* halfword i of the palette of `p` seen from a frame that adds `row`: a 4-bit one is in the pages' space, an 8-bit one is row clutY - 256 of the TIM's CLUT; -1 when
   the palette is not there */
static int palHalf(const Part *p, int row, int i) {
    const uint16_t *pg[3] = {gPage0, gPage1, gPage2};
    int y = p->clutY + row, cx = p->clutX + i;

    if (p->mode) return y >= 256 && y - 256 < CLUT_ROWS && cx < 256 ? gClut[y - 256][cx] : -1;
    if (y < 0 || y >= PH || cx / PW >= 3) return -1;
    return pg[cx / PW][y * PW + cx % PW];
}

static void expand(int half, uint8_t rgb[3]) {
    int r = half & 31, g = (half >> 5) & 31, b = (half >> 10) & 31;

    rgb[0] = (uint8_t)((r << 3) | (r >> 2));
    rgb[1] = (uint8_t)((g << 3) | (g >> 2));
    rgb[2] = (uint8_t)((b << 3) | (b >> 2));
}

/* ---- the files of the stage ---- */

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

/* the MASK file: an archive of the TIMs the game loads, the 8-bit one (with its CLUT) first, then two raw pages */
static size_t makeMask(uint8_t *out) {
    static uint8_t t[3][4096];
    size_t len[3], at = 4 * 3;
    int k;

    len[0] = timBlock(t[0], 1, CLUT_ROWS, &gClut[0][0], gPage0);
    len[1] = timBlock(t[1], 2, 0, NULL, gPage1);
    len[2] = timBlock(t[2], 2, 0, NULL, gPage2);
    for (k = 0; k < 3; k++) {
        w32(out + 4 * k, (uint32_t)at);
        memcpy(out + at, t[k], len[k]);
        at += len[k];
        while (at % 4) out[at++] = 0;
    }
    return at;
}

/* the sprite sheet (gfx.h: parts offset, ids offset, one offset per frame; parts; ids ended by 255; frames), alone, without a leading word */
static size_t makeSheet(uint8_t *s) {
    const int nf = 3, np = 4;
    size_t po = 8 + 4 * (size_t)nf, io = po + 14 * (size_t)np, fo = io + 4, f;
    int i, k;

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
    return f;
}

/* the TMPK file: a table of entry offsets (0 = absent) up to the sheet's entry and then the sheet; entry 0 is, when there is one before the sheet, a map layer
   that is not a sheet (S202: entry 0 is not a sheet, entry 1 is; S920: entries 0 to 2 are absent, entry 3 is) */
static size_t makeTmpk(uint8_t *out, int sheetEntry, int junkFirst) {
    static uint8_t sheet[512];
    size_t sl = makeSheet(sheet), at = 4 * ((size_t)sheetEntry + 1);
    int i;

    for (i = 0; i < sheetEntry; i++) w32(out + 4 * i, 0);
    if (junkFirst) {
        w32(out, (uint32_t)at);
        memset(out + at, 0, 8);
        at += 8;
    }
    w32(out + 4 * sheetEntry, (uint32_t)at);
    memcpy(out + at, sheet, sl);
    return at + sl;
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

/* one PNG against the pages and CLUT: palette, depth and every pixel */
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
        int half = palHalf(p, row, i);

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

static int jint(const char *s, const char *key, int dflt) {
    const char *p = strstr(s, key);

    return p != NULL ? atoi(p + strlen(key)) : dflt;
}

#define CANVAS_W 64
#define CANVAS_H 48
typedef uint32_t Canvas[CANVAS_W * CANVAS_H]; /* 0: nothing, else 0xFF000000 | r << 16 | g << 8 | b */

static uint32_t rgbOf(const uint8_t *rgb) { return 0xFF000000u | (uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2]; }

/* frame `f` composed straight from the tables, the pages and the CLUT: parts from the last to the first, each with its own palette */
static void expectedFrame(const Frame *f, Canvas c) {
    int k, x, y;

    memset(c, 0, sizeof(Canvas));
    for (k = f->n - 1; k >= 0; k--) {
        const Part *p = &kParts[f->item[k].part];

        if (palHalf(p, f->row, 0) < 0) continue;
        for (y = 0; y < p->h; y++) {
            for (x = 0; x < p->w; x++) {
                int t = texel(p->u + x, p->v + y, p->mode), half;
                uint8_t rgb[3];

                if (t < 0) continue;
                half = palHalf(p, f->row, t);
                if (half <= 0) continue; /* transparent */
                expand(half, rgb);
                c[(f->item[k].y + y) * CANVAS_W + f->item[k].x + x] = rgbOf(rgb);
            }
        }
    }
}

/* frame `id` composed from the exported PNGs alone, placed by what objects.json says: the pieces that list the frame, the last slot first */
static void actualFrame(const Sink *s, int id, Canvas c) {
    const Out *js = sinkFind(s, "objects.json");
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

/* ---- the cutting of one stage ---- */

static void testStage(void) {
    static uint8_t mask[16384], tmpk[1024];
    size_t ml, tl;
    Sink s;
    MenuStats st;
    const char *why = NULL;
    char *js;
    int i;

    stageFill();
    ml = makeMask(mask);
    tl = makeTmpk(tmpk, 1, 1); /* the sheet is entry 1; entry 0 is not a sheet */
    memset(&s, 0, sizeof s);
    memset(&st, 0, sizeof st);
    CHECK(stageObjExport(202, "AAA/DAT/FIELD/STAGE/S202MASK.BIN", "AAA/DAT/FIELD/STAGE/S202TMPK.BIN", mask, ml, tmpk, tl, 1, sinkEmit, &s, &st, &why) == 4);
    CHECK(why == NULL);
    /* part 3's palette row is not in the pages: listed, no PNG */
    CHECK(st.pieces == 4 && st.unexported == 1 && st.outside == 0 && st.failed == 0);
    /* one PNG per (rect, palette): parts 0 and 2 are one piece per CLUT row, and each frame's row makes its own */
    CHECK(s.n == 5 && sinkFind(&s, "objects.json") != NULL);
    CHECK(sinkFind(&s, "f000p00.png") && sinkFind(&s, "f000p01.png") && sinkFind(&s, "f001p00.png") && sinkFind(&s, "f001p02.png") && sinkFind(&s, "f004p00.png") == NULL);
    CHECK(sinkFind(&s, "f001p01.png") == NULL); /* part 2 is part 0 again */
    if (s.n != 5 || sinkFind(&s, "f000p00.png") == NULL || sinkFind(&s, "f001p00.png") == NULL) { /* nothing more to read */
        sinkFree(&s);
        return;
    }
    checkPng(&s, "f000p00.png", &kParts[0], 0);
    checkPng(&s, "f000p01.png", &kParts[1], 0); /* 8-bit: CLUT row 1 of the TIM, not a page */
    checkPng(&s, "f001p00.png", &kParts[0], 1); /* 4-bit with the frame's row +1 */
    checkPng(&s, "f001p02.png", &kParts[1], 1); /* 8-bit with the frame's row +1: CLUT row 2 */
    {
        PngImage a, b;
        const Out *o0 = sinkFind(&s, "f000p00.png"), *o1 = sinkFind(&s, "f001p00.png");

        CHECK(pngReadIndexed(o0->data, o0->len, &a, NULL, 0) == 0 && pngReadIndexed(o1->data, o1->len, &b, NULL, 0) == 0);
        CHECK(memcmp(a.px, b.px, (size_t)a.w * (size_t)a.h) == 0); /* the same pixels ... */
        CHECK(memcmp(a.rgb, b.rgb, 3 * 16) != 0);                  /* ... in another palette */
        pngImageFree(&a);
        pngImageFree(&b);
    }
    js = (char *)sinkFind(&s, "objects.json")->data;
    CHECK(strstr(js, "\"format\": \"stages-objects-v1\"") && strstr(js, "\"name\": \"s202\""));
    CHECK(strstr(js, "\"disc\": { \"files\": [\"DAT/FIELD/STAGE/S202MASK.BIN\", \"DAT/FIELD/STAGE/S202TMPK.BIN\"], \"sheetEntry\": 1 }"));
    CHECK(strstr(js, "{ \"file\": \"f000p00.png\", \"page\": 1, \"u\": 256, \"v\": 0, \"w\": 16, \"h\": 8, \"mode\": \"4bit\", \"clut\": { \"x\": 144, \"row\": 5, \"page\": 2 },"));
    CHECK(strstr(js, "{ \"file\": \"f000p01.png\", \"page\": 0, \"u\": 0, \"v\": 2, \"w\": 20, \"h\": 6, \"mode\": \"8bit\", \"clut\": { \"x\": 0, \"row\": 257, \"page\": 0 },"));
    CHECK(strstr(js, "{ \"file\": \"f001p02.png\", \"page\": 0, \"u\": 0, \"v\": 2, \"w\": 20, \"h\": 6, \"mode\": \"8bit\", \"clut\": { \"x\": 0, \"row\": 258, \"page\": 0 },"));
    CHECK(strstr(js, "{ \"frame\": 1, \"slot\": 0, \"at\": [0, 0], \"blend\": \"add\" }, { \"frame\": 1, \"slot\": 1, \"at\": [3, 2], \"blend\": \"add\" }"));
    CHECK(strstr(js, "\"file\": null, \"unexported\": \"palette outside the pages\""));
    CHECK(strstr(js, "{ \"frame\": 4, \"slot\": 0, \"at\": [0, 0], \"blend\": \"subtract\" }"));
    CHECK(strstr(js, "\"pages\": [[64, 16], [64, 16], [64, 16]]"));
    /* the strong check: every frame repainted from the PNGs at the offsets objects.json gives is the frame composed from the pages and the CLUT */
    for (i = 0; i < 2; i++) { /* frame 4 has its only part without a palette: nothing to paint */
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

    /* entry 0 is not the sheet here: asking for it lists the stage without pieces, with the reason (a fact about the disc, not an error) */
    memset(&st, 0, sizeof st);
    CHECK(stageObjExport(202, "AAA/DAT/FIELD/STAGE/S202MASK.BIN", "AAA/DAT/FIELD/STAGE/S202TMPK.BIN", mask, ml, tmpk, tl, 0, sinkEmit, &s, &st, &why) == 0);
    CHECK(why != NULL && s.n == 1 && st.pieces == 0 && st.failed == 0);
    if (sinkFind(&s, "objects.json") == NULL) return;
    js = (char *)sinkFind(&s, "objects.json")->data;
    CHECK(strstr(js, "\"unexported\": \"the entry of the TMPK file that should hold the sprite sheet is absent or is not one\"") && strstr(js, "\"pieces\": []"));
    sinkFree(&s);
    /* an entry past the table, and a MASK file that is not one */
    CHECK(stageObjExport(202, "a", "b", mask, ml, tmpk, tl, 7, sinkEmit, &s, &st, &why) == 0 && why != NULL);
    sinkFree(&s);
    CHECK(stageObjExport(202, "a", "b", tmpk, tl, tmpk, tl, 1, sinkEmit, &s, &st, &why) == 0 && why != NULL && strstr((char *)sinkFind(&s, "objects.json")->data, "no image"));
    sinkFree(&s);
}

/* a sheet of the two-word kind (parts and ids, no frames: S376, S970): the entry is a sheet, and nothing in it is drawn */
static void testFramelessSheet(void) {
    static uint8_t mask[16384], tmpk[256];
    size_t ml, at = 8;
    Sink s;
    MenuStats st;
    const char *why = NULL;

    memset(&s, 0, sizeof s);
    memset(&st, 0, sizeof st);
    stageFill();
    ml = makeMask(mask);
    w32(tmpk, (uint32_t)at);
    w32(tmpk + at, 8);      /* parts at 8 */
    w32(tmpk + at + 4, 22); /* ids at 8 + one part */
    w16(tmpk + at + 8, 256);
    w16(tmpk + at + 10, 0);
    w16(tmpk + at + 12, 16);
    w16(tmpk + at + 14, 8);
    w16(tmpk + at + 16, 144);
    w16(tmpk + at + 18, 5);
    w16(tmpk + at + 20, 0);
    tmpk[at + 22] = 255;
    CHECK(stageObjExport(376, "AAA/DAT/FIELD/STAGE/S376MASK.BIN", "AAA/DAT/FIELD/STAGE/S376TMPK.BIN", mask, ml, tmpk, at + 24, 0, sinkEmit, &s, &st, &why) == 0);
    CHECK(why != NULL && strstr(why, "parts but no frames") && s.n == 1 && st.pieces == 0 && st.failed == 0);
    if (s.n == 1) CHECK(strstr((char *)s.f[0].data, "\"unexported\": \"the sprite sheet has parts but no frames") && strstr((char *)s.f[0].data, "\"pieces\": []"));
    sinkFree(&s);
}

static void testSheetFile(void) {
    static uint8_t tmpk[1024];
    uint8_t *sh;
    size_t tl, n = 0;

    /* S920's shape: entries 0 to 2 absent, the sheet in entry 3 */
    tl = makeTmpk(tmpk, 3, 0);
    CHECK((sh = stageObjSheetFile(tmpk, tl, 3, &n)) != NULL && n > 8);
    free(sh);
    CHECK(stageObjSheetFile(tmpk, tl, 0, &n) == NULL && stageObjSheetFile(tmpk, tl, 2, &n) == NULL); /* absent */
    CHECK(stageObjSheetFile(tmpk, tl, 4, &n) == NULL && stageObjSheetFile(tmpk, tl, -1, &n) == NULL);
    /* S202's shape: a first entry that is not a sheet */
    tl = makeTmpk(tmpk, 1, 1);
    CHECK(stageObjSheetFile(tmpk, tl, 0, &n) == NULL);
    CHECK((sh = stageObjSheetFile(tmpk, tl, 1, &n)) != NULL);
    free(sh);
}

static void testNames(void) {
    int ref = 0;
    static const char json[] = "{\n  \"mapFile\": 824, \"sheetEntry\": 54067201, \"imageEntry\": 0, \"imageFile\": 823,\n}";

    CHECK(stageObjNumber("AAA/DAT/FIELD/STAGE/S202MASK.BIN") == 202 && stageObjNumber("AAA/DAT/FIELD/STAGE/S920TMPK.BIN") == 920);
    CHECK(stageObjNumber("AAA/DAT/FIELD/STAGE/S202PACK.BIN") == -1 && stageObjNumber("AAA/DAT/FIELD/STAGE/S415BG01.BIN") == -1);
    CHECK(stageObjNumber("AAA/DAT/Z_STAGE/S202MASK.BIN") == -1 && stageObjNumber("AAA/DAT/FIELD/STAGE/SABCMASK.BIN") == -1 && stageObjNumber("") == -1);
    CHECK(stageObjSheetRef(json, strlen(json), &ref) == 0 && ref == 54067201);
    CHECK(stageObjSheetRef("{\"sheetEntry\": 0}", 17, &ref) == 0 && ref == 0);
    CHECK(stageObjSheetRef("{\"sheetEntry\":-5}", 17, &ref) == 0 && ref == -5);
    CHECK(stageObjSheetRef("{\"mapFile\": 1}", 14, &ref) != 0 && stageObjSheetRef("{\"sheetEntry\": x}", 17, &ref) != 0);
    CHECK(stageObjSheetRef("{\"sheetEntry\": 12", 12, &ref) != 0); /* the number is past the end it was given */
}

/* ---- the disc side ---- */

typedef struct DFile {
    char path[112];
    long lba;
    size_t size;
} DFile;

static DFile gDisc[16];
static int gDiscN;
static uint8_t (*gSect)[2352];
static long gSectN, gNextLba = 4;
static char gData[1024];
static int gReads;

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
    gReads++;
    memcpy(frame, gSect[lba], 2352);
    return 1;
}

/* the game's file numbers of this synthetic disc: 825 is S202TMPK, 2218 is S920TMPK, 900 has a name and no file */
static int ioFileName(void *ud, int file, char *path, size_t cap) {
    const char *name = file == 825 ? "S202TMPK" : file == 2218 ? "S920TMPK" : file == 900 ? "S555TMPK" : NULL;

    (void)ud;
    if (name == NULL) return 0;
    snprintf(path, cap, "AAA/DAT/FIELD/STAGE/%s.BIN", name);
    return 1;
}

static int exists(const char *rel) {
    char p[1400];

    snprintf(p, sizeof p, "%s/%s", gData, rel);
    return access(p, F_OK) == 0;
}

static void writeStageData(int stage, int sheetEntry) {
    char p[1400], text[200], dir[1300];

    snprintf(dir, sizeof dir, "%s/stages/s%03d", gData, stage);
    CHECK(tdMakeDirs(dir) == 0);
    snprintf(p, sizeof p, "%s/stage.json", dir);
    snprintf(text, sizeof text, "{\n \"format\": \"stage-v1\",\n \"stage\": %d,\n \"mapFile\": 1, \"sheetEntry\": %d, \"imageEntry\": 0, \"imageFile\": 2,\n}\n", stage, sheetEntry);
    CHECK(tdWriteFileAtomic(p, text, strlen(text)) == 0);
}

static char *slurpFile(const char *rel) {
    char p[1400];
    uint8_t *d = NULL;
    size_t n;

    snprintf(p, sizeof p, "%s/%s", gData, rel);
    if (tdReadFile(p, &d, &n) != 0) return NULL;
    d = (uint8_t *)realloc(d, n + 1);
    d[n] = 0;
    return (char *)d;
}

static void testRun(void) {
    static uint8_t mask[16384], tmpk1[1024], tmpk3[1024];
    StageObjIo io = {{ioList, ioRead, NULL}, ioFileName};
    StageObjStats st;
    ExpManifest man;
    char p[1400], rm[1500], *txt;
    uint8_t one[64] = {0};
    size_t ml, t1, t3;

    stageFill();
    ml = makeMask(mask);
    t1 = makeTmpk(tmpk1, 1, 1);
    t3 = makeTmpk(tmpk3, 3, 0);
    discAdd("AAA/DAT/FIELD/STAGE/S202MASK.BIN", mask, ml);
    discAdd("AAA/DAT/FIELD/STAGE/S202TMPK.BIN", tmpk1, t1); /* the sheet is entry 1 */
    discAdd("AAA/DAT/FIELD/STAGE/S920MASK.BIN", mask, ml);
    discAdd("AAA/DAT/FIELD/STAGE/S920TMPK.BIN", tmpk3, t3); /* the sheet is entry 3, entries 0 to 2 are absent */
    discAdd("AAA/DAT/FIELD/STAGE/S300MASK.BIN", mask, ml);  /* its stage data sets no sheet */
    discAdd("AAA/DAT/FIELD/STAGE/S301MASK.BIN", mask, ml);  /* no stage data at all */
    discAdd("AAA/DAT/FIELD/STAGE/S555MASK.BIN", mask, ml);  /* its sheet file is not on the disc */
    discAdd("AAA/DAT/FIELD/STAGE/S400TMPK.BIN", tmpk1, t1); /* no MASK file */
    discAdd("AAA/DAT/FIELD/STAGE/S202PACK.BIN", mask, ml);  /* not a stage's objects */

    snprintf(gData, sizeof gData, "%s/stageobjexptest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(gData) != NULL);
    /* stages/manifest.json is the topic's one manifest: the stage data's entries stay */
    snprintf(p, sizeof p, "%s/stages/manifest.json", gData);
    writeStageData(202, 825 << 16 | 1);
    writeStageData(920, 2218 << 16 | 3);
    writeStageData(300, 0);
    writeStageData(555, 900 << 16);
    writeStageData(400, 825 << 16 | 1);
    expManOpen(&man, "test", p, "stages");
    expManSet(&man, "s202/stage.json", "0123456789abcdef");
    expManMark(&man, "stage-data", 3);
    CHECK(expManSave(&man, 0) == 0);
    expManFree(&man);

    stageObjExportSetup(gData, &io);
    CHECK(stageObjExportRun("nonsense") == -1);
    CHECK(stageObjExportRun("s920") == 1); /* one stage by name; its files only */
    CHECK(exists("stages/s920/objects/objects.json") && exists("stages/s920/objects/f000p00.png") && !exists("stages/s202/objects/objects.json"));
    CHECK(stageObjExportRun("202, s300") == 2);
    CHECK(exists("stages/s202/objects/f001p02.png") && exists("stages/s300/objects/objects.json") && !exists("stages/s300/objects/f000p00.png"));
    CHECK(stageObjExportRun("all") == 6); /* S202 and S920 have objects; S300, S301, S400 and S555 are listed with the reason; S202PACK is nothing */
    stageObjExportTotals(&st);
    CHECK(st.stages == 2 && st.noObjects == 4 && st.failed == 0 && st.pieces == 8 && st.unexported == 2 && st.outside == 0);
    /* the pieces of S202 (sheet in entry 1) and of S920 (entry 3) are the same pieces: the sheet is what differs in the TMPK files */
    snprintf(p, sizeof p, "%s/stages/s920/objects/f001p02.png", gData);
    CHECK(exists("stages/s920/objects/f001p02.png"));
    /* the reasons, one per cause */
    txt = slurpFile("stages/s300/objects/objects.json");
    CHECK(txt != NULL && strstr(txt, "\"unexported\": \"the stage sets no sprite sheet (sheetEntry 0)\"") && strstr(txt, "\"format\": \"stages-objects-v1\"") && strstr(txt, "\"name\": \"s300\""));
    free(txt);
    txt = slurpFile("stages/s301/objects/objects.json");
    CHECK(txt != NULL && strstr(txt, "no stage data file"));
    free(txt);
    txt = slurpFile("stages/s555/objects/objects.json");
    CHECK(txt != NULL && strstr(txt, "\"unexported\": \"the sheet's file is not on the disc\""));
    free(txt);
    txt = slurpFile("stages/s400/objects/objects.json");
    CHECK(txt != NULL && strstr(txt, "\"unexported\": \"the stage has no MASK file on the disc\""));
    free(txt);
    txt = slurpFile("stages/s202/objects/objects.json");
    CHECK(txt != NULL && strstr(txt, "\"sheetEntry\": 1 }"));
    free(txt);
    txt = slurpFile("stages/s920/objects/objects.json");
    CHECK(txt != NULL && strstr(txt, "\"sheetEntry\": 3 }"));
    free(txt);
    snprintf(p, sizeof p, "%s/stages/manifest.json", gData);
    expManOpen(&man, "test", p, "stages");
    CHECK(tdManifestFind(&man.m, "s202/stage.json") != NULL && tdKvFind(&man.m.exporter, "stage-data") != NULL); /* the stage data's entries and version are kept */
    CHECK(expManIsCurrent(&man, "stage-objects", STAGE_OBJ_EXPORT_VERSION));
    CHECK(tdManifestFind(&man.m, "s202/objects/objects.json") != NULL && tdManifestFind(&man.m, "s202/objects/f000p00.png") != NULL);
    CHECK(tdManifestFind(&man.m, "s202/objects/f001p01.png") == NULL); /* only what was written */
    expManFree(&man);
    /* up to date: the disc is not read */
    gReads = 0;
    CHECK(stageObjExportRun("all") == 0);
    stageObjExportTotals(&st);
    CHECK(st.skipped == 1 && gReads == 0);
    /* a file that went missing is made again */
    snprintf(p, sizeof p, "%s/stages/s202/objects/f000p00.png", gData);
    CHECK(remove(p) == 0);
    CHECK(stageObjExportRun("all") == 6 && exists("stages/s202/objects/f000p00.png"));
    /* an edited piece stays, and the new version goes beside it */
    snprintf(p, sizeof p, "%s/stages/s202/objects/f000p01.png", gData);
    CHECK(tdWriteFileAtomic(p, one, sizeof one) == 0);
    snprintf(p, sizeof p, "%s/stages/manifest.json", gData);
    expManOpen(&man, "test", p, "stages");
    tdKvSet(&man.m.exporter, "stage-objects", "0"); /* a version that is not the current one: the run happens again */
    CHECK(expManSave(&man, 0) == 0);
    expManFree(&man);
    CHECK(stageObjExportRun("all") == 6);
    CHECK(exists("stages/s202/objects/f000p01.png.new"));
    snprintf(rm, sizeof rm, "rm -rf '%s'", gData);
    if (system(rm)) {
    }
    free(gSect);
    gSect = NULL;
}

int main(void) {
    testNames();
    testSheetFile();
    testFramelessSheet();
    testStage();
    testRun();
    if (failures) {
        fprintf(stdout, "stageobjexptest: %d FAILED\n", failures);
        return 1;
    }
    fprintf(stdout, "stageobjexptest: all checks passed\n");
    return 0;
}
