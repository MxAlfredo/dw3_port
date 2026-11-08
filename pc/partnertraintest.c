#define _GNU_SOURCE 1
/*
 * Host test of importing a partner's gym training scene (readable-assets A4b): pc/shim/partner_train.c. A synthetic training file (made-up bytes, no retail data)
 * is exported with the exporter of A3 (peTrainEmit), read back with ptParse, rebuilt with ptBuild and compared with the original frame by frame (visible
 * pixels, colours, blend modes, animations); then the layer rules in a scratch folder under /tmp.
 *
 * `partnertraintest --dir <data>/partners` (not part of the automatic run) rebuilds every training folder of a real export and compares each frame with its own
 * PNGs.
 *
 * Exit 0: every check held.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/exp_core.h"
#include "shim/image_data.h"
#include "shim/mods.h"
#include "shim/partner_bundle.h"
#include "shim/partner_export.h"
#include "shim/partner_train.h"
#include "shim/png_read.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "partnertraintest: FAIL line %d: %s\n", __LINE__, #cond); \
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

#define PAGE_W 64
#define PAGE_H 256

static void setTexel(uint16_t *page, int x, int y, int v) {
    uint16_t *hw = &page[y * PAGE_W + x / 4];

    *hw = (uint16_t)((*hw & ~(15u << ((x & 3) * 4))) | ((unsigned)v << ((x & 3) * 4)));
}

static void timBuild(Buf *b, const uint16_t *clut, int ch, const uint16_t *img) {
    int i;

    u32(b, 0x10);
    u32(b, 8);
    u32(b, 12 + 32 * (uint32_t)ch);
    u16(b, 0); u16(b, 480); u16(b, 16); u16(b, (unsigned)ch);
    for (i = 0; i < 16 * ch; i++) u16(b, clut[i]);
    u32(b, 12 + 2 * (uint32_t)(PAGE_W * PAGE_H));
    u16(b, 768); u16(b, 0); u16(b, PAGE_W); u16(b, PAGE_H);
    for (i = 0; i < PAGE_W * PAGE_H; i++) u16(b, img[i]);
}

typedef struct SPart {
    int u, v, x, y, w, h, row, semi, abr, col;
} SPart;

typedef struct SSprite {
    int n;
    SPart p[4];
} SSprite;

/* rects of the pages (texel = f(x, y) of the rect): page 0 holds A, B, C1, C2, E; page 1 holds D */
static const SSprite kSprites[] = {
    {1, {{0, 0, 4, 6, 16, 16, 0, 0, 0, 0}}},                                                    /* 0: A */
    {2, {{16, 0, 0, 0, 20, 12, 1, 0, 0, 0}, {0, 0, 20, 3, 16, 16, 0, 1, 2, 1}}},                /* 1: B opaque, D (page 1) semi, mode 2 */
    {2, {{0, 16, 0, 0, 12, 12, 2, 0, 0, 0}, {12, 16, 14, 0, 12, 12, 2, 1, 1, 0}}},              /* 2: the same row, opaque and semi mode 1 */
    {2, {{0, 40, 10, 10, 16, 16, 3, 0, 0, 0}, {0, 0, 30, 10, 16, 16, 0, 0, 0, 2}}},             /* 3: sixteen visible colours, and a part of the effect set */
    {0, {{0}}},                                                                                  /* 4: nothing */
    {2, {{0, 0, 2, 2, 16, 16, 0, 0, 0, 0}, {16, 0, 10, 6, 20, 12, 1, 0, 0, 0}}},                /* 5: two opaque parts that overlap */
};

static void sprites(Buf *b, int set) {
    int i, j, n = (int)(sizeof kSprites / sizeof kSprites[0]);

    u16(b, 801); u16(b, 0); u16(b, (unsigned)n); u16(b, 0);
    for (i = 0; i < n; i++) {
        const SSprite *s = &kSprites[i];

        if (set == 8) {
            u32(b, i == 0 ? 1 : 0);
            if (i == 0) {
                u8(b, 0); u8(b, 0); u8(b, 4); u8(b, 6); u16(b, 0); u16(b, 2); u16(b, 16); u16(b, 16);
                u16(b, 0); u16(b, 0); u16(b, 0x1000); u16(b, 0x1000);
            }
            continue;
        }
        u32(b, (uint32_t)s->n);
        for (j = 0; j < s->n; j++) {
            const SPart *p = &s->p[j];

            u8(b, (unsigned)p->u); u8(b, (unsigned)p->v); u8(b, (unsigned)p->x); u8(b, (unsigned)p->y);
            u16(b, (unsigned)(p->row << 6) | (p->semi ? 0x8000u : 0));
            u16(b, (unsigned)p->col | (unsigned)(p->abr << 5));
            u16(b, (unsigned)p->w); u16(b, (unsigned)p->h);
            u16(b, 0); u16(b, 0); u16(b, 0x1000); u16(b, 0x1000);
        }
    }
}

/* a file of two sets: set 0 (the partner's) and set 8 (the effect's): two images each (page 0 plain, page 1 plain) */
static uint8_t *synthFile(size_t *len) {
    Buf f = {0};
    int s, k;

    for (s = 0; s < 9; s++) u32(&f, 0);
    for (k = 0; k < 2; k++) {
        int set = k == 0 ? 0 : 8;
        size_t base = f.n, bank, anim, img0, img1;
        Buf sp = {0}, t0 = {0}, t1 = {0};
        uint16_t clut0[16 * 4], clut1[16], *page0 = (uint16_t *)calloc(PAGE_W * PAGE_H, 2), *page1 = (uint16_t *)calloc(PAGE_W * PAGE_H, 2);
        int x, y, i, r;

        at32(&f, 4 * (size_t)set, (uint32_t)base);
        for (r = 0; r < 4; r++) {
            for (i = 0; i < 16; i++) clut0[r * 16 + i] = (uint16_t)(i == 0 && r != 3 ? 0 : 0x0200 + (set + 1) * 0x40 + r * 16 + i);
        }
        for (i = 0; i < 16; i++) clut1[i] = (uint16_t)(i == 0 ? 0 : 0x4000 + (set + 1) * 32 + i);
        for (i = 0; i < (int)(sizeof kSprites / sizeof kSprites[0]); i++) {
            int j;

            for (j = 0; j < kSprites[i].n; j++) {
                const SPart *p = &kSprites[i].p[j];
                uint16_t *pg = p->col == 1 ? page1 : page0;

                if (p->col == 2) continue; /* the effect set's page: set 8 fills its own rect below */
                for (y = 0; y < p->h; y++) {
                    for (x = 0; x < p->w; x++) setTexel(pg, p->u + x, p->v + y, (x + y + i) % 16 == 5 ? 0 : 1 + (x * 3 + y + p->row * 5) % 15);
                }
            }
        }
        if (set == 8) {
            for (y = 0; y < 16; y++) {
                for (x = 0; x < 16; x++) setTexel(page0, x, y, 1 + (x + y) % 15);
            }
        }
        timBuild(&t0, clut0, 4, page0);
        timBuild(&t1, clut1, 1, page1);
        sprites(&sp, set);
        bank = base + 48;
        anim = (bank + sp.n + 3) & ~(size_t)3;
        img0 = anim + 4 + 8 * 3 + 4 + 8;
        img1 = (img0 + t0.n + 3) & ~(size_t)3;
        u32(&f, 9);
        u32(&f, (uint32_t)bank);
        u32(&f, (uint32_t)anim);
        u32(&f, 0);
        u32(&f, (uint32_t)(anim + 4 + 8 * 3));
        for (i = 0; i < 3; i++) u32(&f, 0);
        u32(&f, (uint32_t)img0);
        u32(&f, (uint32_t)img1);
        u32(&f, 8);
        u32(&f, (uint32_t)(sp.n - 8));
        put(&f, sp.p, sp.n);
        pad4(&f);
        u16(&f, 0x124); u16(&f, 3);
        u16(&f, 1); u8(&f, 7); u8(&f, 0); u16(&f, 3); u16(&f, 4);
        u16(&f, 0); u8(&f, 9); u8(&f, 0); u16(&f, 0xFFFF); u16(&f, 5);
        u16(&f, 3); u8(&f, 5); u8(&f, 0); u16(&f, 0); u16(&f, 0);
        u16(&f, 0x124); u16(&f, 1);
        u16(&f, 5); u8(&f, 12); u8(&f, 0); u16(&f, 7); u16(&f, 8);
        put(&f, t0.p, t0.n);
        pad4(&f);
        put(&f, t1.p, t1.n);
        pad4(&f);
        free(sp.p); free(t0.p); free(t1.p); free(page0); free(page1);
    }
    *len = f.n;
    return f.p;
}

/* ---- the exported folder in memory ---- */

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

static int gotRead(void *ud, const char *name, uint8_t **bytes, size_t *len) {
    const Got *g = (const Got *)ud;
    int i = gotFind(g, name);

    if (i < 0) return -1;
    *bytes = (uint8_t *)malloc(g->len[i]);
    memcpy(*bytes, g->data[i], g->len[i]);
    *len = g->len[i];
    return 0;
}

/* ---- comparing two frames: the visible pixels with their colour and blend, wherever they are ---- */

static int visibleAt(const PeTrainFrame *f, int ax, int ay, unsigned *rgb, int *blend) {
    int x = ax - f->ox, y = ay - f->oy, idx;

    if (f->pic.px == NULL || x < 0 || y < 0 || x >= f->pic.w || y >= f->pic.h) return 0;
    idx = f->pic.px[y * f->pic.w + x];
    if (f->pic.alpha[idx] == 0) return 0;
    *rgb = (unsigned)f->pic.rgb[3 * idx] << 16 | (unsigned)f->pic.rgb[3 * idx + 1] << 8 | f->pic.rgb[3 * idx + 2];
    *blend = f->slotBlend[(idx - f->base) / 16];
    return 1;
}

static int sameFrames(const PeTrainFrame *a, const PeTrainFrame *b, int *visible) {
    int x, y, bad = 0;

    *visible = 0;
    for (y = -60; y < 300; y++) {
        for (x = -60; x < 300; x++) {
            unsigned ra = 0, rb = 0;
            int ba = 0, bb = 0, va = visibleAt(a, x, y, &ra, &ba), vb = visibleAt(b, x, y, &rb, &bb);

            *visible += va;
            if (va != vb || (va && (ra != rb || ba != bb))) bad++;
        }
    }
    return bad == 0;
}

/* ---- the round trip ---- */

static void testRoundTrip(void) {
    size_t len;
    uint8_t *file = synthFile(&len);
    PeTrain t1, t2;
    PeTrainStats st;
    Got got;
    PtSource src;
    PtSet set;
    char why[200];
    int i, nSprites = (int)(sizeof kSprites / sizeof kSprites[0]);

    CHECK(peTrainOpen(&t1, file, len) == 2 && t1.set[0].present && t1.set[8].present && t1.set[0].sprites == nSprites);
    memset(&got, 0, sizeof got);
    memset(&st, 0, sizeof st);
    CHECK(peTrainEmit(&t1, 0, "partners-training-v1", "punch", "dw3:partner/0", "DAT/TRAINING/TRANIN00.BIN", collect, &got, &st) == 0);
    CHECK(st.failed == 0);
    i = gotFind(&got, "training.json");
    CHECK(i >= 0 && strstr((char *)got.data[i], "\"blend\": [{ \"indices\": [") != NULL && strstr((char *)got.data[i], "\"mode\": 2 }") != NULL);
    CHECK(i >= 0 && strstr((char *)got.data[i], "\"paletteBase\": 1") != NULL && strstr((char *)got.data[i], "\"paletteBase\": 0") != NULL);

    CHECK(ptParse(gotRead, &got, 1, &src, why, sizeof why) == 0);
    CHECK(src.nFrames == nSprites && src.frame[4].hasImage == 0 && src.frame[0].hasImage == 1 && src.anim[0].n == 3 && src.anim[2].n == 1 && src.anim[1].n == 0);
    CHECK(ptBuild(&src, &set, why, sizeof why) == 0);
    if (failures) fprintf(stdout, "  why: %s\n", why);
    ptSourceFree(&src);
    CHECK(set.nImages >= 1 && set.nImages <= 2 && set.sprites == nSprites && set.rows[0] >= 1 && set.rows[0] <= 16 && set.bankOffset == 8);
    fprintf(stdout, "partnertraintest: rebuilt %d sprites, %d parts, %d + %d palette rows, %zu bytes\n", set.sprites, set.parts, set.rows[0], set.rows[1], set.len);

    CHECK(peTrainOpen(&t2, set.file, set.len) == 1 && t2.set[0].present && t2.set[0].sprites == nSprites);
    for (i = 0; i < nSprites; i++) {
        PeTrainFrame a, b;
        int vis = 0;

        CHECK(peTrainFrame(&t1, 0, i, &a) && peTrainFrame(&t2, 0, i, &b));
        CHECK(sameFrames(&a, &b, &vis));
        CHECK(i == 4 ? vis == 0 : vis > 100);
        CHECK(b.missing == 0 && b.shared == 0);
        peTrainFrameFree(&a);
        peTrainFrameFree(&b);
    }
    /* the animations come back byte for byte: same sprite, time and placement (nothing needed a shift) */
    {
        const uint8_t *a1 = t1.file + t1.set[0].anim[0], *a2 = t2.file + t2.set[0].anim[0];

        CHECK(t2.set[0].anim[0] != 0 && t2.set[0].anim[1] == 0 && t2.set[0].anim[2] != 0 && t2.set[0].anim[3] == 0 && t2.set[0].anim[4] == 0);
        CHECK(!memcmp(a1, a2, 4 + 8 * 3));
        CHECK(!memcmp(t1.file + t1.set[0].anim[2], t2.file + t2.set[0].anim[2], 4 + 8));
    }
    /* the second blend slot of sprite 1 and the shared row of sprite 2 keep their modes */
    {
        PeTrainFrame b;
        int k, modes = 0;

        CHECK(peTrainFrame(&t2, 0, 2, &b) && b.nSlots >= 2);
        for (k = 0; k < b.nSlots; k++) modes |= 1 << b.slotBlend[k];
        CHECK((modes & 1) && (modes & 4)); /* an opaque palette and one drawn with mode 1 (slotBlend 2 -> bit 2) */
        peTrainFrameFree(&b);
        CHECK(peTrainFrame(&t2, 0, 1, &b) && b.nSlots >= 2);
        for (k = 0, modes = 0; k < b.nSlots; k++) modes |= 1 << b.slotBlend[k];
        CHECK((modes & 1) && (modes & 8)); /* mode 2 (slotBlend 3) */
        peTrainFrameFree(&b);
    }
    peTrainClose(&t2);
    /* the TIMs are what the game's loader reads: 4-bit with a CLUT of 16 x rows, an image of 64 halfwords */
    {
        ImgTim tim;

        CHECK(imgTimParse(set.file + set.image[0], set.len - set.image[0], &tim) == 0 && tim.mode == 0 && tim.cw == 16 && tim.ch == set.rows[0] && tim.iw == 64 && tim.ih >= 1 &&
              tim.ih <= 256);
    }
    CHECK((set.bank & 3) == 0 && (set.anim[0] & 3) == 0 && (set.image[0] & 3) == 0);
    {
        PcTrainView v;

        ptView(&set, &v);
        CHECK(v.bank == set.file + set.bank && v.bankOffset == 8 && v.anim[1] == NULL && v.anim[0] != NULL && v.nImages == set.nImages && v.image[0] == set.file + set.image[0]);
    }
    ptSetFree(&set);
    peTrainClose(&t1);
    gotFree(&got);
    free(file);
}

/* ---- the limits ---- */

static void srcInit(PtSource *s, int frames) {
    memset(s, 0, sizeof *s);
    s->nFrames = frames;
    s->frame = (PtFrame *)calloc((size_t)frames, sizeof *s->frame);
}

static void frameFill(PtFrame *f, int w, int h, int ncol, int seed) {
    int i;

    f->hasImage = 1;
    f->paletteBase = -1;
    f->png.w = w;
    f->png.h = h;
    f->png.depth = 8;
    f->png.nPal = ncol + 1;
    f->png.nAlpha = 1;
    f->png.alpha[0] = 0;
    f->png.px = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
    for (i = 0; i < ncol; i++) {
        f->png.rgb[3 * (i + 1)] = (uint8_t)(8 * ((seed + i) % 31 + 1));
        f->png.rgb[3 * (i + 1) + 1] = (uint8_t)(8 * ((seed / 31 + i * 3) % 31 + 1));
        f->png.rgb[3 * (i + 1) + 2] = (uint8_t)(8 * ((seed * 13 + i * 5) % 31 + 1));
    }
    for (i = 0; i < w * h; i++) f->png.px[i] = (uint8_t)(1 + i % ncol);
}

static void testLimits(void) {
    PtSource src;
    PtSet set;
    char why[300];
    PeTrain t;
    PeTrainFrame fr;
    int i;

    /* a frame whose origin is negative: it shifts, and its animation steps shift back; nothing is lost to the byte */
    srcInit(&src, 2);
    frameFill(&src.frame[0], 20, 10, 5, 1);
    src.frame[0].ox = -30;
    src.frame[0].oy = -3;
    frameFill(&src.frame[1], 20, 10, 5, 2);
    src.frame[1].ox = 100;
    src.frame[1].oy = 7;
    src.anim[0].n = 2;
    src.anim[0].step = (PtStep *)calloc(2, sizeof(PtStep));
    src.anim[0].step[0].frame = 0;
    src.anim[0].step[0].time = 4;
    src.anim[0].step[0].x = 200;
    src.anim[0].step[0].y = 90;
    src.anim[0].step[1].frame = 1;
    src.anim[0].step[1].time = 5;
    src.anim[0].step[1].x = 10;
    src.anim[0].step[1].y = 20;
    CHECK(ptBuild(&src, &set, why, sizeof why) == 0);
    CHECK(peTrainOpen(&t, set.file, set.len) == 1);
    {
        const uint8_t *a = t.file + t.set[0].anim[0];
        int s0x = (int16_t)(a[8] | a[9] << 8), s0y = (int16_t)(a[10] | a[11] << 8), s1x = (int16_t)(a[16] | a[17] << 8);

        CHECK(s0x == 200 + -30 && s0y == 90 + -3); /* ox, oy added: the part sits at 0, 0 */
        CHECK(s1x == 10);
        CHECK(peTrainFrame(&t, 0, 0, &fr) && fr.ox == 0 && fr.oy == 0 && fr.pic.w == 20 && fr.pic.h == 10);
        peTrainFrameFree(&fr);
        CHECK(peTrainFrame(&t, 0, 1, &fr) && fr.ox == 100 && fr.oy == 7);
        peTrainFrameFree(&fr);
    }
    peTrainClose(&t);
    ptSetFree(&set);
    /* an animation that shows a frame that is not there */
    src.anim[0].step[1].frame = 5;
    CHECK(ptBuild(&src, &set, why, sizeof why) == -1 && strstr(why, "frame 5") != NULL);
    ptSourceFree(&src);

    /* a part that would start past byte 255 moves back (the frame spans less than 256 pixels from its first part to its last) */
    srcInit(&src, 1);
    frameFill(&src.frame[0], 40, 8, 15, 3);
    src.frame[0].ox = 300;
    CHECK(ptBuild(&src, &set, why, sizeof why) == 0 && peTrainOpen(&t, set.file, set.len) == 1 && peTrainFrame(&t, 0, 0, &fr) && fr.ox == 255);
    peTrainFrameFree(&fr);
    peTrainClose(&t);
    ptSetFree(&set);
    ptSourceFree(&src);

    /* more distinct palettes than two pages hold (2 x 16 rows) */
    srcInit(&src, 40);
    for (i = 0; i < 40; i++) {
        frameFill(&src.frame[i], 12, 12, 15, 1 + i * 37);
        src.frame[i].png.px[0] = 1;
    }
    CHECK(ptBuild(&src, &set, why, sizeof why) == -1 && strstr(why, "do not fit") != NULL);
    ptSourceFree(&src);

    /* identical frames are stored once: 40 frames of one picture are one part's pixels and one palette row */
    srcInit(&src, 40);
    for (i = 0; i < 40; i++) frameFill(&src.frame[i], 12, 12, 15, 5);
    CHECK(ptBuild(&src, &set, why, sizeof why) == 0 && set.parts == 40 && set.rows[0] == 1 && set.rows[1] == 0 && set.nImages == 1);
    ptSetFree(&set);
    ptSourceFree(&src);

    /* 255 colours: 17 rows' worth of parts per frame, still two pages' worth; an index past the palette is refused */
    srcInit(&src, 1);
    frameFill(&src.frame[0], 40, 40, 255, 9);
    CHECK(ptBuild(&src, &set, why, sizeof why) == 0 && set.parts >= 17 && set.rows[0] + set.rows[1] >= 17);
    ptSetFree(&set);
    src.frame[0].png.nPal = 100;
    CHECK(ptBuild(&src, &set, why, sizeof why) == -1 && strstr(why, "palette") != NULL);
    ptSourceFree(&src);

    /* 20 frames of a 100 x 100 picture that differs only in a 16 x 16 corner: whole parts (20 x 10000 pixels) do not fit two pages, tiles of the sprite's own grid
       store what the frames share once, and the frames come back exactly */
    srcInit(&src, 20);
    for (i = 0; i < 20; i++) {
        PtFrame *f = &src.frame[i];
        int x, y;

        frameFill(f, 100, 100, 15, 4);
        f->ox = 40;
        f->oy = 20;
        for (y = 0; y < 100; y++) {
            for (x = 0; x < 100; x++) f->png.px[y * 100 + x] = (uint8_t)(1 + (x * 7 + y * 3 + (y < 16 && x < 16 ? i : 0)) % 15);
        }
    }
    CHECK(ptBuild(&src, &set, why, sizeof why) == 0 && set.parts > 20);
    if (set.file != NULL) {
        CHECK(peTrainOpen(&t, set.file, set.len) == 1);
        for (i = 0; i < 20; i++) {
            PeTrainFrame b;

            CHECK(peTrainFrame(&t, 0, i, &b) && b.pic.px != NULL && b.ox == 40 && b.oy == 20 && b.pic.w == 100 && b.pic.h == 100);
            peTrainFrameFree(&b);
        }
        peTrainClose(&t);
        ptSetFree(&set);
    }
    ptSourceFree(&src);
}

/* ---- training.json ---- */

typedef struct Mem {
    const char *name[4];
    const char *text[4];
    int n;
    uint8_t *png;
    size_t pngLen;
} Mem;

static int memRead(void *ud, const char *name, uint8_t **bytes, size_t *len) {
    Mem *m = (Mem *)ud;
    int i;

    for (i = 0; i < m->n; i++) {
        if (!strcmp(m->name[i], name)) {
            *len = strlen(m->text[i]);
            *bytes = (uint8_t *)malloc(*len + 1);
            memcpy(*bytes, m->text[i], *len + 1);
            return 0;
        }
    }
    if (m->png != NULL && !strcmp(name, "000.png")) {
        *bytes = (uint8_t *)malloc(m->pngLen);
        memcpy(*bytes, m->png, m->pngLen);
        *len = m->pngLen;
        return 0;
    }
    return -1;
}

static void testParse(void) {
    PtSource src;
    char why[200];
    Mem m;
    ImgPic pic;

    memset(&m, 0, sizeof m);
    memset(&pic, 0, sizeof pic);
    pic.w = 8;
    pic.h = 8;
    pic.depth = 4;
    pic.nColors = 16;
    pic.px = (uint8_t *)calloc(64, 1);
    pic.px[9] = 1;
    pic.alpha[1] = 255;
    pic.rgb[3] = 200;
    CHECK(imgPicToPng(&pic, &m.png, &m.pngLen) == 0);
    free(pic.px);
    m.n = 1;
    m.name[0] = "training.json";

    m.text[0] = "{\"format\":\"partners-training-v1\",\"frames\":[{\"id\":0,\"image\":\"000.png\",\"origin\":[3,4]},{\"id\":2,\"image\":null}],"
                "\"animations\":[{\"id\":1,\"steps\":[{\"frame\":2,\"time\":3,\"x\":-4,\"y\":5}]}]}";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == 0 && src.nFrames == 3 && src.frame[0].hasImage && src.frame[0].ox == 3 && src.frame[0].oy == 4 &&
          src.frame[0].paletteBase == -1 && !src.frame[1].hasImage && !src.frame[2].hasImage && src.anim[1].n == 1 && src.anim[1].step[0].x == -4);
    ptSourceFree(&src);
    /* decode 0 reads the files but fills no pixels */
    CHECK(ptParse(memRead, &m, 0, &src, why, sizeof why) == 0 && src.frame[0].hasImage && src.frame[0].png.px == NULL);
    ptSourceFree(&src);

    m.text[0] = "{\"format\":\"partners-training-v1\",\"frames\":[{\"id\":0,\"image\":\"000.png\",\"paletteBase\":1}]}";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == 0 && src.frame[0].paletteBase == 1);
    ptSourceFree(&src);
    m.text[0] = "{\"format\":\"partners-training-v1\",\"frames\":[{\"id\":0,\"image\":\"000.png\",\"paletteBase\":2}]}";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == -1 && strstr(why, "paletteBase") != NULL);
    m.text[0] = "{\"format\":\"partners-training-v1\",\"frames\":[{\"id\":0,\"image\":\"missing.png\"}]}";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == -1 && strstr(why, "missing.png") != NULL);
    m.text[0] = "{\"format\":\"partners-training-v1\",\"frames\":[{\"id\":0,\"image\":\"../000.png\"}]}";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == -1 && strstr(why, "plain file name") != NULL);
    m.text[0] = "{\"format\":\"menus-gym-effect-v1\",\"frames\":[]}";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == -1 && strstr(why, "format") != NULL);
    m.text[0] = "{\"format\":\"partners-training-v1\",\"frames\":[{\"id\":0,\"image\":null}],\"animations\":[{\"id\":0,\"steps\":[{\"frame\":1,\"time\":1,\"x\":0,\"y\":0}]}]}";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == -1 && strstr(why, "animation 0 step 0") != NULL);
    m.text[0] = "{\"format\":\"partners-training-v1\",\"frames\":[{\"id\":0,\"image\":null,\"blend\":[{\"indices\":[1],\"mode\":0}]}]}";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == -1 && strstr(why, "blend") != NULL);
    m.text[0] = "not json";
    CHECK(ptParse(memRead, &m, 1, &src, why, sizeof why) == -1);
    free(m.png);
}

/* ---- the layers ---- */

static char gRoot[600];

static void mk(const char *rel) {
    char cmd[1000];

    snprintf(cmd, sizeof cmd, "mkdir -p '%s/%s'", gRoot, rel);
    if (system(cmd)) {}
}

static void putBytes(const char *rel, const uint8_t *d, size_t n) {
    char path[1300], dir[1300], *slash;
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

static void putFolder(const char *prefix, const Got *g) {
    int i;

    for (i = 0; i < g->n; i++) {
        char rel[300];

        snprintf(rel, sizeof rel, "%s/%s", prefix, g->rel[i]);
        putBytes(rel, g->data[i], g->len[i]);
    }
}

static void manifestFolder(const char *dir, const char *keyPrefix, const Got *g) {
    ExpManifest man;
    char path[900];
    int i;

    snprintf(path, sizeof path, "%s/%s/manifest.json", gRoot, dir);
    expManOpen(&man, "test", path, "partners");
    for (i = 0; i < g->n; i++) {
        char rel[300], hex[17];

        snprintf(rel, sizeof rel, "%s/%s", keyPrefix, g->rel[i]);
        tdHashHex(tdHash(g->data[i], g->len[i]), hex);
        expManSet(&man, rel, hex);
    }
    expManSave(&man, 0);
    expManFree(&man);
}

static int baseOf(int partner) { return partner == 9 ? 8 : 3; } /* partner 8 stands in for 3, 9 for the mod's own 8 (a chain) */

static void modJson(const char *mod) {
    char rel[200], text[200];

    snprintf(rel, sizeof rel, "mods/%s/mod.json", mod);
    snprintf(text, sizeof text, "{\"format\":\"mod-v1\",\"name\":\"%s\",\"version\":\"1.0\"}", mod);
    putBytes(rel, (const uint8_t *)text, strlen(text));
}

/* what stderr got between logBegin and logEnd (the validator reads the loaders' lines from there): `out` gets up to cap-1 bytes */
static int sSavedErr;

static void logBegin(const char *path) {
    int fd;

    fflush(stderr);
    sSavedErr = dup(2);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

static void logEnd(const char *path, char *out, size_t cap) {
    FILE *f;
    size_t n = 0;

    fflush(stderr);
    dup2(sSavedErr, 2);
    close(sSavedErr);
    f = fopen(path, "rb");
    if (f != NULL) {
        n = fread(out, 1, cap - 1, f);
        fclose(f);
    }
    out[n] = '\0';
}

static void testLayers(const Got *got) {
    char root[700], data[700], names[PT_TRAININGS][64];
    char keys[8][64];
    PcTrainView v;
    int disc = -1, i;

    memset(names, 0, sizeof names);
    snprintf(names[0], sizeof names[0], "punch");
    snprintf(names[3], sizeof names[3], "study");
    ptSetNames(names);
    ptSetBase(baseOf);
    memset(keys, 0, sizeof keys);
    for (i = 0; i < 8; i++) snprintf(keys[i], sizeof keys[i], "name%d", i);
    snprintf(keys[3], sizeof keys[3], "agumon");
    pbReset();
    pbSetKeys(keys);
    snprintf(data, sizeof data, "%s/data", gRoot);
    snprintf(root, sizeof root, "%s/mods", gRoot);
    mk("data/partners");
    mk("mods");
    assetSetData(data);
    modsInit(root, NULL);
    ptReset(0);
    ptSetNames(names);

    CHECK(ptTrainingNamed("punch") && ptTrainingNamed("Study") && !ptTrainingNamed("nosuch") && !ptTrainingNamed("") && !ptTrainingNamed(NULL));
    CHECK(ptTrainingOfRow(0) == 0 && ptTrainingOfRow(1) == 0 && ptTrainingOfRow(12) == 11 && ptTrainingOfRow(13) == 0 && ptTrainingOfRow(24) == 11);

    /* the exporter's folder of an original, untouched: nothing to build (the manifest vouches for every file) */
    putFolder("data/partners/agumon/training/punch", got);
    manifestFolder("data/partners", "agumon/training/punch", got);
    CHECK(ptStartOriginals() == 0);
    CHECK(!pcTrainSetFor(1, 3, &v, &disc) && disc == 3);

    /* one file changed: the folder counts, rows 1 and 13 (the same training) get the set, another training does not */
    {
        int k = gotFind(got, "000.png");
        uint8_t *bad;

        CHECK(k >= 0);
        bad = (uint8_t *)malloc(got->len[k]);
        memcpy(bad, got->data[k], got->len[k]);
        bad[got->len[k] - 20] ^= 0x01; /* not a valid PNG any more: the folder is refused, not half used */
        putBytes("data/partners/agumon/training/punch/000.png", bad, got->len[k]);
        CHECK(ptStartOriginals() == 0);
        CHECK(!pcTrainSetFor(1, 3, &v, &disc) && disc == 3);
        free(bad);
        /* edited and valid: a training.json that only differs by a trailing newline is the user's file */
        k = gotFind(got, "training.json");
        {
            size_t n = got->len[k] + 1;
            uint8_t *t = (uint8_t *)malloc(n);
            int png = gotFind(got, "000.png");

            memcpy(t, got->data[k], got->len[k]);
            t[n - 1] = '\n';
            putBytes("data/partners/agumon/training/punch/training.json", t, n);
            putBytes("data/partners/agumon/training/punch/000.png", got->data[png], got->len[png]);
            free(t);
        }
        CHECK(ptStartOriginals() == 1);
        CHECK(pcTrainSetFor(1, 3, &v, &disc) == 1 && v.bank != NULL && v.nImages >= 1);
        CHECK(pcTrainSetFor(13, 3, &v, &disc) == 1);
        CHECK(!pcTrainSetFor(4, 3, &v, &disc) && disc == 3);
        CHECK(!pcTrainSetFor(1, 2, &v, &disc) && disc == 2); /* another original keeps the disc's */
    }

    /* a mod's partner: its own folder, else its base's set, whatever that is */
    modJson("m1");
    putFolder("mods/m1/partners/duo/training/punch", got);
    modsInit(root, NULL);
    ptSetNames(names);
    CHECK(ptLoadMod(8, "duo") == 1);
    CHECK(pcTrainSetFor(1, 8, &v, &disc) == 1 && v.nImages >= 1);
    CHECK(pcTrainSetFor(25 - 12, 8, &v, &disc) == 1);
    CHECK(!pcTrainSetFor(4, 8, &v, &disc) && disc == 3);    /* no `study` folder: the base's (the original partner 3) */
    CHECK(ptLoadMod(9, "trio") == 0);
    CHECK(pcTrainSetFor(1, 9, &v, &disc) == 1);               /* partner 9 stands in for 8: the set of partner 8 */
    CHECK(!pcTrainSetFor(4, 9, &v, &disc) && disc == 3);      /* ... and for 3 beyond it */
    /* a training.json that does not read: the line names the file (its path, which `--validate-mod` shortens to partners/<key>/training/<name>/training.json) */
    {
        char log[4096], logPath[800];

        modJson("m2");
        putBytes("mods/m2/partners/broken/training/punch/training.json", (const uint8_t *)"{ broken", 8);
        modsInit(root, NULL);
        ptSetNames(names);
        snprintf(logPath, sizeof logPath, "%s/log.txt", gRoot);
        logBegin(logPath);
        CHECK(ptLoadMod(11, "broken") == 0);
        logEnd(logPath, log, sizeof log);
        CHECK(strstr(log, "/mods/m2/partners/broken/training/punch/training.json rejected: ") != NULL);
        CHECK(strstr(log, " mods/m2/broken/") == NULL);
    }
    /* a partner without a folder shows its base's set, and an original's edited set counts as its base's too */
    CHECK(ptLoadMod(10, "none") == 0 && pcTrainSetFor(1, 10, &v, &disc) == 1 && !pcTrainSetFor(4, 10, &v, &disc) && disc == 3);
    ptReset(8);
    CHECK(pcTrainSetFor(1, 8, &v, &disc) == 1 && !pcTrainSetFor(4, 8, &v, &disc) && disc == 3);
    CHECK(pcTrainSetFor(1, 3, &v, &disc) == 1);               /* the original's edited set stays */
    ptReset(0);
    CHECK(!pcTrainSetFor(1, 3, &v, &disc));
    pbReset();
}

/* ---- a real exported bundle (not part of the automatic run) ---- */

typedef struct Dir {
    char path[1200];
} Dir;

static int dirRead(void *ud, const char *name, uint8_t **bytes, size_t *len) {
    char path[1500];

    snprintf(path, sizeof path, "%s/%s", ((Dir *)ud)->path, name);
    return tdReadFile(path, bytes, len);
}

/* the first visible pixel of a PNG frame in the sprite's coordinates (row by row) */
static void firstVisible(const PtFrame *f, int *fx, int *fy) {
    int x, y;

    *fx = *fy = 1 << 30;
    for (y = 0; y < f->png.h && f->hasImage; y++) {
        for (x = 0; x < f->png.w; x++) {
            int idx = f->png.px[y * f->png.w + x];

            if (idx < f->png.nAlpha && f->png.alpha[idx] == 0) continue;
            *fx = f->ox + x;
            *fy = f->oy + y;
            return;
        }
    }
}

static void firstVisibleRebuilt(const PeTrainFrame *fr, int *fx, int *fy) {
    int x, y;

    *fx = *fy = 1 << 30;
    for (y = fr->oy; fr->pic.px != NULL && y < fr->oy + fr->pic.h; y++) {
        for (x = fr->ox; x < fr->ox + fr->pic.w; x++) {
            unsigned c = 0;
            int b = 0;

            if (visibleAt(fr, x, y, &c, &b)) {
                *fx = x;
                *fy = y;
                return;
            }
        }
    }
}

static int compareFolder(const char *path, int *sprites, int *parts, int *rows, int *pixels) {
    Dir d;
    PtSource src;
    PtSet set;
    PeTrain t;
    char why[320];
    int i, bad = 0;

    snprintf(d.path, sizeof d.path, "%s", path);
    if (ptParse(dirRead, &d, 1, &src, why, sizeof why) != 0) {
        fprintf(stdout, "  %s: %s\n", path, why);
        return -1;
    }
    if (ptBuild(&src, &set, why, sizeof why) != 0) {
        fprintf(stdout, "  %s: %s\n", path, why);
        ptSourceFree(&src);
        return -1;
    }
    *sprites += set.sprites;
    *parts += set.parts;
    *rows += set.rows[0] + set.rows[1];
    if (peTrainOpen(&t, set.file, set.len) != 1) bad++;
    for (i = 0; i < src.nFrames && !bad; i++) {
        PeTrainFrame fr;
        const PtFrame *f = &src.frame[i];
        int x, y, sx, sy, rx, ry, dx, dy;

        if (!peTrainFrame(&t, 0, i, &fr)) {
            bad++;
            break;
        }
        /* the rebuilt sprite may sit shifted in the part bytes (the animation takes the shift off): compare from each one's first visible pixel */
        firstVisible(f, &sx, &sy);
        firstVisibleRebuilt(&fr, &rx, &ry);
        dx = sx == 1 << 30 || rx == 1 << 30 ? 0 : rx - sx;
        dy = sx == 1 << 30 || rx == 1 << 30 ? 0 : ry - sy;
        for (y = 0; y < f->png.h && f->hasImage; y++) {
            for (x = 0; x < f->png.w; x++) {
                int idx = f->png.px[y * f->png.w + x], vis = !(idx < f->png.nAlpha && f->png.alpha[idx] == 0), k, bl = 0, gb = 0, gv;
                unsigned rgb = (unsigned)(f->png.rgb[3 * idx] & 0xF8) << 16 | (unsigned)(f->png.rgb[3 * idx + 1] & 0xF8) << 8 | (f->png.rgb[3 * idx + 2] & 0xF8), got = 0;

                gv = visibleAt(&fr, f->ox + x + dx, f->oy + y + dy, &got, &gb);
                for (k = 0; k < f->nBlend; k++) {
                    if (idx >= f->blend[k].lo && idx <= f->blend[k].hi) bl = 1 + f->blend[k].mode;
                }
                *pixels += vis;
                got = (got & 0xF80000) | (got & 0x00F800) | (got & 0x0000F8);
                if (gv != vis || (vis && (got != rgb || gb != bl))) {
                    if (bad == 0) fprintf(stdout, "  %s frame %d (%d,%d): visible %d/%d colour %06x/%06x blend %d/%d\n", path, i, x, y, vis, gv, rgb, got, bl, gb);
                    bad++;
                }
            }
        }
        peTrainFrameFree(&fr);
    }
    peTrainClose(&t);
    ptSetFree(&set);
    ptSourceFree(&src);
    return bad ? -1 : 0;
}

static int dirMode(const char *root) {
    DIR *pd = opendir(root);
    struct dirent *pe;
    int folders = 0, failed = 0, sprites = 0, parts = 0, rows = 0, pixels = 0;

    if (pd == NULL) {
        fprintf(stdout, "partnertraintest: cannot open %s\n", root);
        return 1;
    }
    while ((pe = readdir(pd)) != NULL) {
        char tdir[1300];
        DIR *td;
        struct dirent *te;

        if (pe->d_name[0] == '.') continue;
        snprintf(tdir, sizeof tdir, "%s/%s/training", root, pe->d_name);
        td = opendir(tdir);
        if (td == NULL) continue;
        while ((te = readdir(td)) != NULL) {
            char f[1700];

            if (te->d_name[0] == '.') continue;
            snprintf(f, sizeof f, "%s/%s", tdir, te->d_name);
            folders++;
            if (compareFolder(f, &sprites, &parts, &rows, &pixels) != 0) failed++;
        }
        closedir(td);
    }
    closedir(pd);
    fprintf(stdout, "partnertraintest --dir: %d folders, %d failed, %d sprites, %d parts, %d palette rows, %d visible pixels compared\n", folders, failed, sprites, parts, rows, pixels);
    return failed != 0 || folders == 0;
}

int main(int argc, char **argv) {
    char tmp[] = "/tmp/partnertraintestXXXXXX";
    size_t len;
    uint8_t *file;
    PeTrain t1;
    Got got;
    PeTrainStats st;

    if (argc == 3 && !strcmp(argv[1], "--dir")) return dirMode(argv[2]);
    if (mkdtemp(tmp) == NULL) {
        fprintf(stdout, "partnertraintest: no temp folder\n");
        return 1;
    }
    snprintf(gRoot, sizeof gRoot, "%s", tmp);
    testRoundTrip();
    testLimits();
    testParse();
    file = synthFile(&len);
    memset(&got, 0, sizeof got);
    memset(&st, 0, sizeof st);
    if (peTrainOpen(&t1, file, len) == 2) peTrainEmit(&t1, 0, "partners-training-v1", "punch", "dw3:partner/0", "DAT/TRAINING/TRANIN00.BIN", collect, &got, &st);
    testLayers(&got);
    peTrainClose(&t1);
    gotFree(&got);
    free(file);
    {
        char cmd[700];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
        if (system(cmd)) {}
    }
    if (failures) {
        fprintf(stdout, "partnertraintest: %d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stdout, "partnertraintest: OK\n");
    return 0;
}
