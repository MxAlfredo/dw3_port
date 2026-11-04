/*
 * The VRAM side of the game's text button icons (feature controls-config, C5); the plan
 * and the pixels are text_icons.c, the reasoning is in text_icons.h.
 *
 * Once per frame, before the layers are drawn (gfx_engine.c hostDrawFrame), the four
 * 12 x 12 cells of the face-button icons and their CLUT in the font sheet are brought to
 * what the plan says: the player's device family, or the game's own texels. PsyCross
 * keeps a CPU copy of VRAM (`vram`) and draws from it, so reading and writing it here is
 * what the sprites the game's text window emitted will sample.
 *
 * The font is the game's: loadFont writes it at boot and again after a movie (movie.c).
 * So the cells are never trusted: when they are not what this unit last wrote, what is
 * there is taken as the game's own (and saved, to restore it later) and the plan is
 * applied again. While a movie frame is in VRAM nothing is read or written, and neither
 * happens until the CLUT looks like a loaded font.
 */
#include <stdio.h>

#include "engine/game.h"

#include "host_video.h"
#include "input_device.h"
#include "port_log.h"
#include "text_icons.h"

extern unsigned short vram[]; /* PsyX_render.cpp: the CPU copy, 1024 x 512 halfwords */
#define VRAM_W 1024

#define CELL_WORDS (TI_WORDS_PER_ROW * TI_SIZE)

typedef struct Region {
    unsigned short px[TI_BUTTONS][CELL_WORDS];
    unsigned short clut[TI_CLUT_ROWS][16];
} Region;

static Region ours;  /* what this unit last wrote */
static Region theirs; /* the game's own, as last seen */
static int applied;   /* the cells hold `ours` */
static int builtFor[TI_BUTTONS] = {-1, -1, -1, -1};
static int builtValid;
static int spriteCount, lastReported;

/* engine/game.h declares its own memcpy: no <string.h> here, plain loops */
static void copyWords(unsigned short *dst, const unsigned short *src, int n) {
    while (n-- > 0) {
        *dst++ = *src++;
    }
}

static int sameWords(const unsigned short *a, const unsigned short *b, int n) {
    while (n-- > 0) {
        if (*a++ != *b++) {
            return 0;
        }
    }
    return 1;
}

static unsigned short *cellAt(int slot, int row) {
    const TextIconSite *s = &TEXT_ICON_SITES[slot];

    return &vram[(s->v + row) * VRAM_W + TI_FONT_X + s->page * 64 + s->u / 4];
}

static void readRegion(Region *r) {
    int i, y;

    for (i = 0; i < TI_BUTTONS; i++) {
        for (y = 0; y < TI_SIZE; y++) {
            copyWords(&r->px[i][y * TI_WORDS_PER_ROW], cellAt(i, y), TI_WORDS_PER_ROW);
        }
    }
    for (y = 0; y < TI_CLUT_ROWS; y++) {
        copyWords(r->clut[y], &vram[(TI_CLUT_Y + y) * VRAM_W + TI_CLUT_X], 16);
    }
}

static void writeRegion(const Region *r) {
    RECT rect;
    int i, y;

    for (i = 0; i < TI_BUTTONS; i++) {
        setRECT(&rect, TI_FONT_X + TEXT_ICON_SITES[i].page * 64 + TEXT_ICON_SITES[i].u / 4, TEXT_ICON_SITES[i].v,
                TI_WORDS_PER_ROW, TI_SIZE);
        LoadImage(&rect, (u_long *)r->px[i]);
    }
    for (y = 0; y < TI_CLUT_ROWS; y++) {
        setRECT(&rect, TI_CLUT_X, TI_CLUT_Y + y, 16, 1);
        LoadImage(&rect, (u_long *)r->clut[y]);
    }
}

/* A loaded font leaves a CLUT with a transparent first entry and several colours */
static int fontLooksLoaded(const Region *r) {
    int i, used = 0;

    for (i = 1; i < 16; i++) {
        used += r->clut[0][i] != 0;
    }
    return r->clut[0][0] == 0 && used >= 3;
}

static void build(const int cells[TI_BUTTONS]) {
    TextIconSet set;
    int i, y;

    for (i = 0; builtValid && i < TI_BUTTONS && builtFor[i] == cells[i]; i++) {
    }
    if (builtValid && i == TI_BUTTONS) {
        return;
    }
    textIconBuild(cells, &set);
    for (i = 0; i < TI_BUTTONS; i++) {
        textIconPack(set.px[i], ours.px[i]);
    }
    for (y = 0; y < TI_CLUT_ROWS; y++) {
        copyWords(ours.clut[y], set.clut, 16);
    }
    for (i = 0; i < TI_BUTTONS; i++) {
        builtFor[i] = cells[i];
    }
    builtValid = 1;
}

void textIconsNoteSprite(unsigned short clut) {
    if ((clut & 0x3F) == (TI_CLUT_X >> 4) && (clut >> 6) >= TI_CLUT_Y && (clut >> 6) < TI_CLUT_Y + TI_CLUT_ROWS) {
        spriteCount++;
    }
}

void textIconsFrame(void) {
    static int frame;
    Region cur;
    InputCfg live;
    int cells[TI_BUTTONS];
    int restore;

    /* the sprites of the frame before: a line when the count of face-button icons on screen changes */
    if (spriteCount != lastReported) {
        PLOG("text", PLOG_TRACE, "texticons: frame %d: %d face-button icon sprite(s) drawn\n", frame, spriteCount);
        lastReported = spriteCount;
    }
    spriteCount = 0;
    frame++;
    if (hostVideoActive()) {
        return;
    }
    readRegion(&cur);
    if (!applied || !sameWords(&cur.px[0][0], &ours.px[0][0], (int)(sizeof(cur) / sizeof(unsigned short)))) {
        /* not what this unit wrote: the game's own (loaded or reloaded) */
        theirs = cur;
        applied = 0;
        if (!fontLooksLoaded(&theirs)) {
            return;
        }
    }
    inputCfgCurrent(&live);
    restore = textIconPlan(inputDeviceCurrent(), inputIconSetting(), inputDeviceFamily(), &live, cells);
    if (restore) {
        if (applied) {
            writeRegion(&theirs);
            applied = 0;
        }
        return;
    }
    build(cells);
    if (!applied || !sameWords(&cur.px[0][0], &ours.px[0][0], (int)(sizeof(cur) / sizeof(unsigned short)))) {
        writeRegion(&ours);
    }
    applied = 1;
}
