/*
 * Host test of a partner's own 2D art (Betamon pack R4, shim/partner_art.c): an indexed PNG of 32x32 cells becomes the 4-bit texels
 * and 16-colour palette the menus' sprite drawer reads, the animation walks the cells back and forth as the original's do, and a
 * one-frame sheet points the drawer at one cell of a VRAM slot. No game, no disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim/partner_art.h"
#include "shim/png_read.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "partnerarttest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* an in-memory PNG: cells of 32x32 whose pixel (x, y) of cell c is (x + y + c) & 15, palette entry i = (i*16, 255 - i*16, i), index 0 transparent */
static void makePng(PngImage *p, int cells, int h)
{
    int x, y, i;

    memset(p, 0, sizeof *p);
    p->w = 32 * cells;
    p->h = h;
    p->depth = 4;
    p->nPal = 16;
    for (i = 0; i < 16; i++) {
        p->rgb[3 * i] = (uint8_t)(i * 16);
        p->rgb[3 * i + 1] = (uint8_t)(255 - i * 16);
        p->rgb[3 * i + 2] = (uint8_t)i;
    }
    p->nAlpha = 1;
    p->alpha[0] = 0;
    p->px = (uint8_t *)malloc((size_t)p->w * (size_t)p->h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < p->w; x++) p->px[y * p->w + x] = (uint8_t)((x % 32 + y + x / 32) & 15);
    }
}

int main(void)
{
    PngImage p;
    PaArt art;
    PaSheet sh;
    char why[200];
    int i;

    /* the conversion: texels packed four to a halfword, the first in the low nibble, cells side by side */
    makePng(&p, 3, 32);
    CHECK(paFromIndexed(&p, &art, why, sizeof why) == 0);
    CHECK(art.cells == 3);
    CHECK(art.pal[0] == 0); /* index 0 is transparent */
    CHECK(art.pal[1] == (uint16_t)((16 >> 3) | (239 >> 3) << 5 | (1 >> 3) << 10));
    CHECK(art.pal[15] == (uint16_t)((240 >> 3) | (15 >> 3) << 5 | (15 >> 3) << 10));
    /* row 0, cell 0: texels 0,1,2,3 -> 0x3210; cell 1 starts at halfword 8: texels (x + 1) & 15 = 1,2,3,4 -> 0x4321 */
    CHECK(art.img[0] == 0x3210);
    CHECK(art.img[8] == 0x4321);
    /* row 5, cell 2, halfword 1 of the cell (texels x = 4..7): (x + 5 + 2) & 15 = 11,12,13,14 */
    CHECK(art.img[5 * 24 + 16 + 1] == 0xEDCB);
    paFree(&art);

    /* an opaque black stays opaque (0 would read as transparent) */
    p.rgb[3 * 2] = p.rgb[3 * 2 + 1] = p.rgb[3 * 2 + 2] = 0;
    CHECK(paFromIndexed(&p, &art, why, sizeof why) == 0 && art.pal[2] == 0x8000);
    paFree(&art);
    free(p.px);

    /* rules: 32 rows, whole cells, 1..PA_CELLS_MAX of them, at most 16 colours */
    makePng(&p, 2, 31);
    CHECK(paFromIndexed(&p, &art, why, sizeof why) != 0 && strstr(why, "32") != NULL);
    free(p.px);
    makePng(&p, 2, 32);
    p.w = 63;
    CHECK(paFromIndexed(&p, &art, why, sizeof why) != 0);
    p.w = 64;
    p.nPal = 17;
    CHECK(paFromIndexed(&p, &art, why, sizeof why) != 0 && strstr(why, "16") != NULL);
    free(p.px);
    makePng(&p, PA_CELLS_MAX + 1, 32);
    CHECK(paFromIndexed(&p, &art, why, sizeof why) != 0);
    free(p.px);
    makePng(&p, 1, 32);
    p.px[7] = 16; /* an index past the palette */
    CHECK(paFromIndexed(&p, &art, why, sizeof why) != 0);
    free(p.px);
    CHECK(paFromPng((const uint8_t *)"not a png", 9, &art, why, sizeof why) != 0);

    /* the animation: 0 1 .. k-1 k-2 .. 1 like the original's 0 1 2 1 and 7 8 9 10 9 8 */
    CHECK(paSteps(0) == 0 && paSteps(1) == 1 && paSteps(2) == 2 && paSteps(3) == 4 && paSteps(4) == 6 && paSteps(8) == 14);
    {
        static const int want4[] = {0, 1, 2, 3, 2, 1};
        static const int want3[] = {0, 1, 2, 1};

        for (i = 0; i < 6; i++) CHECK(paCellAt(4, i) == want4[i]);
        for (i = 0; i < 4; i++) CHECK(paCellAt(3, i) == want3[i]);
        CHECK(paCellAt(1, 0) == 0 && paCellAt(2, 0) == 0 && paCellAt(2, 1) == 1);
        CHECK(paCellAt(3, 4) == 0 && paCellAt(3, -1) == 0); /* out of range: the first cell */
    }

    /* the step after a step: the next cell of the walk, back to 0 at its end (what pcPartnerStepNext gives a partner with art) */
    {
        int st = 0, n;

        for (n = 0; n < 12; n++) st = paNext(4, st); /* 6 steps: twice round */
        CHECK(st == 0 && paNext(4, 0) == 1 && paNext(4, 4) == 5 && paNext(4, 5) == 0);
        CHECK(paNext(1, 0) == 0 && paNext(2, 0) == 1 && paNext(2, 1) == 0 && paNext(0, 0) == 0);
    }

    /* the sheet: one frame, one part, the cell's texels in the slot's page and the slot's palette relative to the drawer's texture position */
    paSheetBuild(&sh, 2, 64, 96, -128, 224, 2, 3);
    CHECK(sh.offsets[0] == (int)offsetof(PaSheet, part) && sh.offsets[1] == (int)offsetof(PaSheet, ids) && sh.offsets[2] == (int)offsetof(PaSheet, frame));
    CHECK(sh.ids[0] == 0);
    CHECK(sh.frame[0] == 1 && sh.frame[1] == 0 && sh.frame[2] == -1 && sh.frame[3] == 0 && sh.frame[4] == 2 && sh.frame[5] == 3);
    CHECK(sh.part.u == 64 + 2 * 32 && sh.part.v == 96 && sh.part.w == 32 && sh.part.h == 32);
    CHECK(sh.part.clutX == -128 && sh.part.clutY == 224 && sh.part.mode == 0);

    /* the memory card icon: exactly 44x28, at most 16 colours, index 0 transparent; the texels pack the same way (11 halfwords a row) */
    {
        PngImage ic;
        PaArt ia;
        int x, y;

        memset(&ic, 0, sizeof ic);
        ic.w = PA_ICON_W;
        ic.h = PA_ICON_H;
        ic.depth = 4;
        ic.nPal = 3;
        ic.rgb[3] = 255;
        ic.rgb[7] = 255;
        ic.nAlpha = 1;
        ic.alpha[0] = 0;
        ic.px = (uint8_t *)malloc((size_t)ic.w * (size_t)ic.h);
        for (y = 0; y < ic.h; y++) {
            for (x = 0; x < ic.w; x++) ic.px[y * ic.w + x] = (uint8_t)((x + y) % 3);
        }
        CHECK(paIconFromIndexed(&ic, &ia, why, sizeof why) == 0);
        CHECK(ia.cells == 1 && ia.w == 44 && ia.h == 28 && ia.rowHw == 11);
        CHECK(ia.pal[0] == 0 && ia.pal[1] == (uint16_t)(31) && ia.pal[2] == (uint16_t)(31 << 5));
        CHECK(ia.img[0] == 0x0210); /* texels 0,1,2,0 -> 0,1,2,0 in nibbles: 0x0210 */
        CHECK(ia.img[11 * 3 + 2] == (uint16_t)(((5 + 0) % 3) | (((5 + 1) % 3) << 4) | (((5 + 2) % 3) << 8) | (((5 + 3) % 3) << 12))); /* row 3, texels 8..11 */
        paFree(&ia);
        ic.w = 32;
        CHECK(paIconFromIndexed(&ic, &ia, why, sizeof why) != 0); /* a portrait cell is not an icon */
        ic.w = 44;
        ic.h = 32;
        CHECK(paIconFromIndexed(&ic, &ia, why, sizeof why) != 0);
        ic.h = 28;
        ic.nPal = 17;
        CHECK(paIconFromIndexed(&ic, &ia, why, sizeof why) != 0);
        ic.nPal = 3;
        ic.px[5] = 3; /* past the palette */
        CHECK(paIconFromIndexed(&ic, &ia, why, sizeof why) != 0);
        free(ic.px);
        CHECK(paIconFromPng((const uint8_t *)"not a png", 9, &ia, why, sizeof why) != 0);
        paSheetBuildSize(&sh, 0, PA_ICON_W, PA_ICON_H, 64, 32, -64, 224, 1, 2);
        CHECK(sh.part.u == 64 && sh.part.v == 32 && sh.part.w == 44 && sh.part.h == 28 && sh.frame[4] == 1 && sh.frame[5] == 2);
    }

    if (failures) {
        fprintf(stdout, "partnerarttest: %d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stdout, "partnerarttest: ok\n");
    return 0;
}
