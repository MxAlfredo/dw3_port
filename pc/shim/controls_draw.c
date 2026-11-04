/*
 * The options menu's drawing and per-frame glue (feature controls-config, C3
 * and O1; layout in docs/design/options-menu.md). The model and the input are in controls_screen.c; this file puts them on
 * the window with the game's own look:
 *
 *   - Text: the game's font. FONT.styles[n].glyphs is the glyph table the game's
 *     text window (text_window.c, textWindowDraw) walks; the font's TIMs are in
 *     VRAM from loadFont (page column 0x140, glyph.page * 64 further right) and
 *     each glyph is one textured quad with the same UV, CLUT and texture page
 *     the text window computes. Not a game task and no game state is touched.
 *     The text files address glyphs by a byte code (digits 4-13, A-Z 14-39,
 *     a-z 40-65; the table entry is glyphs[code - 4]); '.' and '-' are
 *     icon-table entries (the 0x01 nn sequences); other characters draw as '?'.
 *   - Frame and cursor (O5b): the game's own window art, read from the user's disc at
 *     run time (see "The window art"): the panel of its sprite sheet (FILE_MENU_SPRITES)
 *     nine-sliced to the window, the cursor glyphs of its font, and a short open animation
 *     like the field menu's. If the disc data cannot be read, flat and gradient polygons
 *     in the field menu's colours (navy panel, light blue border) stand in.
 *
 *   - Button icons (C4): the Kenney prompt atlas (prompt_icons.h) lives in a VRAM
 *     area of its own, drawn 1:1 as 14x14 textured quads (see "The icon atlas").
 *
 * The primitives go into this file's own ordering table, drawn with its own
 * DRAWENV after the game's layers (the same way host_battle.c draws its moves).
 */
#include <stdio.h>

#include "engine/game.h"

#include "controls_screen.h"
#include "touch_menu.h"
#include "touch_sdl.h"
#include "options_system.h"
#include "host_video.h"
#include "input_device.h"
#include "menu_frame.h"
#include "options_video.h"
#include "prompt_icons.h"



#define SCREEN_W 320
#define SCREEN_H 240
#define FONT_PAGE_X 0x140 /* where loadFont puts the font's images (TextWindow.texX) */

#define MAX_GLYPHS 768
#define MAX_SHAPES 96
#define MAX_ICONS 40

/* The menus' font: style 1. Styles 2 and 3 are the battle/HUD sheets, whose
   CLUT rows are not the menu's white/blue/yellow ones. */
#define STYLE 1

static POLY_FT4 glyphPrims[MAX_GLYPHS];
static POLY_G4 gradPrims[MAX_SHAPES];
static POLY_F4 flatPrims[MAX_SHAPES];
static POLY_F3 triPrims[12];
static POLY_FT4 iconPrims[MAX_ICONS];
static POLY_FT4 panelPrims[9 * 8]; /* the slices of the window art's panel: the window and the tabs' boxes */
static POLY_FT4 cursorPrims[2];
static void *order[MAX_GLYPHS + 2 * MAX_SHAPES + 24]; /* in drawing order */
static int orderCount, glyphCount, gradCount, flatCount, triCount, iconCount, panelCount, cursorCount;
static OT_TAG ot[2];
static DRAWENV env;

static void push(void *prim) {
    if (orderCount < (int)(sizeof(order) / sizeof(order[0]))) {
        order[orderCount++] = prim;
    }
}

static void flat(int x, int y, int w, int h, int r, int g, int b) {
    POLY_F4 *p;

    if (flatCount >= MAX_SHAPES) {
        return;
    }
    p = &flatPrims[flatCount++];
    SetPolyF4(p);
    setRGB0(p, r, g, b);
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    push(p);
}

/* Vertical gradient from (r0,g0,b0) at the top to (r1,g1,b1) at the bottom */
static void gradient(int x, int y, int w, int h, int r0, int g0, int b0, int r1, int g1, int b1) {
    POLY_G4 *p;

    if (gradCount >= MAX_SHAPES) {
        return;
    }
    p = &gradPrims[gradCount++];
    SetPolyG4(p);
    setRGB0(p, r0, g0, b0);
    setRGB1(p, r0, g0, b0);
    setRGB2(p, r1, g1, b1);
    setRGB3(p, r1, g1, b1);
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    push(p);
}

/* The menu window: light border with cut corners over a navy gradient */
static void panel(int x, int y, int w, int h) {
    flat(x + 2, y, w - 4, 1, 0xB8, 0xD0, 0xFF);
    flat(x + 2, y + h - 1, w - 4, 1, 0xB8, 0xD0, 0xFF);
    flat(x, y + 2, 1, h - 4, 0xB8, 0xD0, 0xFF);
    flat(x + w - 1, y + 2, 1, h - 4, 0xB8, 0xD0, 0xFF);
    flat(x + 1, y + 1, 1, 1, 0xB8, 0xD0, 0xFF);
    flat(x + w - 2, y + 1, 1, 1, 0xB8, 0xD0, 0xFF);
    flat(x + 1, y + h - 2, 1, 1, 0xB8, 0xD0, 0xFF);
    flat(x + w - 2, y + h - 2, 1, 1, 0xB8, 0xD0, 0xFF);
    gradient(x + 1, y + 2, w - 2, h - 4, 0x14, 0x20, 0x60, 0x08, 0x10, 0x38);
    gradient(x + 2, y + 1, w - 4, 1, 0x14, 0x20, 0x60, 0x14, 0x20, 0x60);
    gradient(x + 2, y + h - 2, w - 4, 1, 0x08, 0x10, 0x38, 0x08, 0x10, 0x38);
}

/* A small triangle: dir 1 points right, -1 points left */
static void arrow(int x, int y, int dir) {
    POLY_F3 *p;

    if (triCount >= (int)(sizeof(triPrims) / sizeof(triPrims[0]))) {
        return;
    }
    p = &triPrims[triCount++];
    setPolyF3(p);
    setRGB0(p, 0xFF, 0xFF, 0xFF);
    if (dir > 0) {
        setXY3(p, x, y, x, y + 8, x + 6, y + 4);
    } else {
        setXY3(p, x + 6, y, x + 6, y + 8, x, y + 4);
    }
    push(p);
}

/* A small triangle pointing up (dir -1) or down (1): a list has more rows that way */
static void arrowV(int x, int y, int dir) {
    POLY_F3 *p;

    if (triCount >= (int)(sizeof(triPrims) / sizeof(triPrims[0]))) {
        return;
    }
    p = &triPrims[triCount++];
    setPolyF3(p);
    setRGB0(p, 0xFF, 0xFF, 0xFF);
    if (dir > 0) {
        setXY3(p, x, y, x + 8, y, x + 4, y + 6);
    } else {
        setXY3(p, x, y + 6, x + 8, y + 6, x + 4, y);
    }
    push(p);
}

/* The glyph of an ASCII character, NULL for a space. Letters and digits are
   glyphs[byte - 4] (processTextChar), '?' and '!' are the font's own; '.' and
   '-' are icon-table entries (the 0x01 nn sequences of the text files). */
static const Glyph *glyphFor(const TextStyle *st, char c) {
    if (c == ' ') {
        return NULL;
    }
    if (c >= '0' && c <= '9') {
        return &st->glyphs[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return &st->glyphs[10 + (c - 'A')];
    }
    if (c >= 'a' && c <= 'z') {
        return &st->glyphs[36 + (c - 'a')];
    }
    switch (c) {
    case '.':
        return &st->icons[4];
    case '-':
        return &st->icons[12];
    case '!':
        return &st->glyphs[227];
    default:
        return &st->glyphs[226]; /* '?' */
    }
}

static int spaceWidth(int style) {
    (void)style;
    return 5;
}

/* Width in pixels of the game's own advance for the text */
static int textWidth(int style, const char *text) {
    const TextStyle *st = &FONT.styles[style];
    int w = 0;

    for (; *text != '\0'; text++) {
        const Glyph *g = glyphFor(st, *text);

        w += g == NULL ? spaceWidth(style) : g->advance + g->dx;
    }
    return w;
}

/* One glyph at x, y (the line's top left); returns the advance */
static int drawGlyph(const TextStyle *st, const Glyph *g, int x, int y, int palette) {
    POLY_FT4 *p;
    int gx, gy;

    if (g->page == 0xFF) {
        g = st->glyphs; /* a missing glyph: the font's glyph 0, as the game does */
    }
    if (glyphCount >= MAX_GLYPHS) {
        return 0;
    }
    p = &glyphPrims[glyphCount++];
    gx = x + g->dx;
    gy = y + g->dy;
    SetPolyFT4(p);
    setRGB0(p, 0x80, 0x80, 0x80);
    setXY4(p, gx, gy, gx + g->w, gy, gx, gy + g->h, gx + g->w, gy + g->h);
    setUV4(p, g->u, g->v, g->u + g->w - 1, g->v, g->u, g->v + g->h - 1, g->u + g->w - 1, g->v + g->h - 1);
    p->tpage = GetTPage(0, 1, FONT_PAGE_X + (g->page << 6), 0);
    p->clut = GetClut(FONT_PAGE_X + g->clutX, palette + g->clutY);
    push(p);
    return g->advance + g->dx;
}

/* Draws like textWindowDraw with an opaque window: x, y is the line's top left */
static void drawText(int style, int x, int y, const char *text, int palette) {
    const TextStyle *st = &FONT.styles[style];

    for (; *text != '\0'; text++) {
        const Glyph *g = glyphFor(st, *text);

        x += g == NULL ? spaceWidth(style) : drawGlyph(st, g, x, y, palette);
    }
}

static void drawCentered(int style, int cx, int y, const char *text, int palette) {
    drawText(style, cx - textWidth(style, text) / 2, y, text, palette);
}

/* the game's headers declare their own string and number functions: no <stdlib.h> or <string.h> here */
extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern int memcmp(const void *a, const void *b, unsigned long n);
extern void *memset(void *s, int c, unsigned long n);
extern char *getenv(const char *name);

/* ---- Borrowed VRAM: the icon atlas (C4) and the window art (O5b) ----
 *
 * The menu needs two small blocks of VRAM that are its own while it is open:
 *
 *   - the icon atlas: PI_ATLAS_W x PI_ATLAS_H halfwords (56 x 126) at (ICON_VRAM_X, ICON_VRAM_Y),
 *     15-bit direct colour, no CLUT;
 *   - the window art: ART_W x ART_H halfwords (16 x 64) at (ART_X, ART_Y): the panel's texels, the
 *     cursor's glyphs and their two CLUT rows (see "The window art").
 *
 * No part of VRAM is free in every scene: the stage textures of a field fill everything right of
 * x = 640 (the VRAM dump of warp:330 has the stage's tiles on both blocks), so the blocks are borrowed,
 * not owned. A lease keeps the game's own halfwords in a backup while ours are on screen, tells a write
 * of the game's into the area (a halfword that is neither ours nor in the backup) and keeps it in the
 * backup, puts ours back when something overwrote them, and returns the backup when the menu closes
 * (or a movie starts). PsyCross keeps a CPU copy of VRAM (`vram`): the leases read it, never the GPU.
 * Evidence and layout in docs/design/options-menu.md.
 */
extern unsigned short vram[]; /* PsyX_render.cpp: the CPU copy, 1024 x 512 halfwords */

#define VRAM_W 1024
#define ICON_VRAM_X 960
#define ICON_VRAM_Y 0
#define ART_X 1008 /* 16 halfwords: GetClut needs a multiple of 16 */
#define ART_Y 128
#define ART_W 16
#define ART_H 64

typedef struct Lease {
    const char *name;
    int x, y, w, h;
    const unsigned short *ours;
    unsigned short *backup, *scratch; /* w * h halfwords each */
    int held;
} Lease;

static unsigned short atlasBackup[PI_ATLAS_W * PI_ATLAS_H], atlasScratch[PI_ATLAS_W * PI_ATLAS_H];
static unsigned short artOurs[ART_W * ART_H], artBackup[ART_W * ART_H], artScratch[ART_W * ART_H];
static Lease leases[2] = {
    {"icon atlas", ICON_VRAM_X, ICON_VRAM_Y, PI_ATLAS_W, PI_ATLAS_H, PROMPT_ATLAS, atlasBackup, atlasScratch, 0},
    {"window art", ART_X, ART_Y, ART_W, ART_H, artOurs, artBackup, artScratch, 0},
};

/* The area as VRAM holds it now, w * h halfwords */
static void leaseRead(const Lease *l, unsigned short *dst) {
    int y;

    for (y = 0; y < l->h; y++) {
        memcpy(dst + y * l->w, &vram[(l->y + y) * VRAM_W + l->x], (size_t)l->w * sizeof(unsigned short));
    }
}

static void leaseUpload(const Lease *l, const unsigned short *px) {
    RECT rect;

    setRECT(&rect, l->x, l->y, l->w, l->h);
    LoadImage(&rect, (u_long *)px);
    DrawSync(0);
}

/* Once per frame the menu draws: takes the area on the first call, then keeps ours on screen */
static void leaseEnsure(Lease *l) {
    int n = l->w * l->h, i, used = 0;

    leaseRead(l, l->scratch);
    if (!l->held) {
        memcpy(l->backup, l->scratch, (size_t)n * sizeof(unsigned short));
        for (i = 0; i < n; i++) {
            used += l->scratch[i] != 0;
        }
        fprintf(stderr, "menu: %s borrows VRAM (%d,%d) %dx%d, %d halfwords of the game's kept for the close\n", l->name,
                l->x, l->y, l->w, l->h, used);
        l->held = 1;
    } else {
        mfLeaseSync(l->backup, l->ours, l->scratch, n);
    }
    if (memcmp(l->scratch, l->ours, (size_t)n * sizeof(unsigned short)) != 0) {
        leaseUpload(l, l->ours);
    }
}

/* Gives the game's halfwords back (whatever it wrote meanwhile stays its own) */
static void leaseRelease(Lease *l) {
    if (l->held) {
        leaseRead(l, l->scratch);
        mfLeaseSync(l->backup, l->ours, l->scratch, l->w * l->h);
        leaseUpload(l, l->backup);
        l->held = 0;
    }
}

static void leasesRelease(void) {
    leaseRelease(&leases[0]);
    leaseRelease(&leases[1]);
}

/* One atlas cell at x, y (top left), 1:1 */
static void drawIcon(int cell, int x, int y) {
    POLY_FT4 *p;
    int u, v, baseX = ICON_VRAM_X & ~63, baseY = ICON_VRAM_Y >= 256 ? 256 : 0;

    if (iconCount >= MAX_ICONS || !promptIconUV(cell, &u, &v)) {
        return;
    }
    p = &iconPrims[iconCount++];
    SetPolyFT4(p);
    setRGB0(p, 0x80, 0x80, 0x80);
    setXY4(p, x, y, x + PI_CELL, y, x, y + PI_CELL, x + PI_CELL, y + PI_CELL);
    u += ICON_VRAM_X - baseX;
    v += ICON_VRAM_Y - baseY;
    setUV4(p, u, v, u + PI_CELL - 1, v, u, v + PI_CELL - 1, u + PI_CELL - 1, v + PI_CELL - 1);
    p->tpage = GetTPage(2, 0, baseX, baseY);
    p->clut = 0;
    push(p);
}

/* ---- The window art (O5b) ----
 *
 * The game draws its window panels with SpriteDrawer from the sheet FILE_MENU_SPRITES (src/engine/menu/field_menu.c)
 * and its cursor as a glyph of the font (src/engine/text/cursor.c: the text codes 0x8185..0x8188). Both pictures
 * are texels of the FILE_FONT archive, which loadFont uploads to VRAM at x = 0x140 (page k of the archive at
 * 0x140 + 64 k, as 4-bit texels in 16-bit words); only the sheet's frame table is a cached file that may not be
 * resident. So, once, the first time the menu opens, both files are read from the disc through the game's own
 * file table and CD reader (into private memory: FILE_CACHE and the game's heap are not touched), and what the
 * menu draws is copied into artOurs: the panel (the one part of frame 0x16, a 44 x 44 rounded window with its
 * shadow), the four cursor glyphs and their two CLUT rows. Nothing of it is in the repo or in VRAM before that.
 *
 * artOurs, 16 x 64 halfwords (the block at ART_X, ART_Y, borrowed through a lease):
 *   rows 0..43   the panel's texels, from column 0
 *   rows 44..59  the cursor glyphs, 4 columns each
 *   row 60       the panel's CLUT (16 colours)        row 61  the cursor's CLUT
 * Drawn as 4-bit textured quads of the page at ART_PAGE_X; if anything fails the polygon frame stays.
 */
#define ART_PAGE_X (ART_X & ~63)
#define ART_PANEL_FRAME 0x16   /* a plain rounded panel; the party pages of the field menu draw it too */
#define ART_CURSOR_CODE 0x8185 /* CURSOR_FRAMES: glyph k is the code 0x8185 + k */
#define ART_CURSOR_ROW 44
#define ART_PANEL_CLUT_ROW 60
#define ART_CURSOR_CLUT_ROW 61
#define FONT_PAGE_HW 64 /* halfwords a font page is apart from the next */
#define ART_SCRATCH 0xA800 /* the game's buffer for an unpacked font page (timLoaderSetBufferSize) */

static int artState; /* 0 not tried yet, 1 ready, -1 failed: the polygon frame */
static MfPart artPanel;
static int artPanelU;
static struct {
    int u, v, w, h, dx, dy;
} artGlyph[4];

/* A file of the disc in private memory, NULL when it is not there */
static unsigned char *readDiscFile(int file, long *size) {
    unsigned char *buf;
    s32 sectors, done = 0;

    if (!FILE_TABLE.exists(file) || (sectors = FILE_TABLE.getSectorCount(file)) <= 0) {
        return NULL;
    }
    buf = malloc((size_t)sectors * CD_SECTOR_SIZE);
    if (buf != NULL) {
        CD_READER.read(file, 0, 0, buf, &done);
        *size = (long)sectors * CD_SECTOR_SIZE;
    }
    return buf;
}

/* w x h halfwords of the font pages at (hx, y), hx counted from 0x140, into artOurs at (col, row) */
static int artFetch(const unsigned char *font, long fontSize, unsigned char *scratch, int hx, int y, int w, int h, int col,
                    int row) {
    MfImage im;
    int local = hx % FONT_PAGE_HW;

    return col + w <= ART_W && row + h <= ART_H && local + w <= FONT_PAGE_HW &&
           mfArchiveImage(font, fontSize, hx / FONT_PAGE_HW, scratch, ART_SCRATCH, &im) &&
           mfImageCopy(&im, local, y, w, h, &artOurs[row * ART_W + col], ART_W);
}

static int artBuild(const unsigned char *sheet, long sheetSize, const unsigned char *font, long fontSize,
                    unsigned char *scratch) {
    const TextStyle *st = &FONT.styles[STYLE];
    const Glyph *first = NULL;
    int k, pw;

    if (!mfSheetPart(sheet, sheetSize, ART_PANEL_FRAME, -1, &artPanel) || artPanel.w < 16 || artPanel.h < 16 ||
        artPanel.h > ART_CURSOR_ROW || artPanel.u < 0 || artPanel.v < 0) {
        return 0;
    }
    pw = (artPanel.u % 4 + artPanel.w + 3) / 4;
    artPanelU = (ART_X - ART_PAGE_X) * 4 + artPanel.u % 4;
    if (!artFetch(font, fontSize, scratch, artPanel.u / 4, artPanel.v, pw, artPanel.h, 0, 0) ||
        !artFetch(font, fontSize, scratch, artPanel.clutX, artPanel.clutY, 16, 1, 0, ART_PANEL_CLUT_ROW)) {
        return 0;
    }
    for (k = 0; k < 4; k++) {
        unsigned char code[4];
        const Glyph *g;
        int c, hw;

        code[0] = (unsigned char)(((ART_CURSOR_CODE + k) >> 8) & 0xFF);
        code[1] = (unsigned char)((ART_CURSOR_CODE + k) & 0xFF);
        code[2] = code[3] = 0;
        c = FONT.decode(code, 1, st); /* the way the text window reads it: kind << 8 | icon number */
        if ((c >> 8) != 1 || (c & 0xFF) < 1 || (c & 0xFF) > st->iconCount) {
            return 0;
        }
        g = &st->icons[(c & 0xFF) - 1];
        hw = (g->u % 4 + g->w + 3) / 4;
        if (g->page == 0xFF || hw > 4 || g->h > ART_H - ART_CURSOR_ROW - 2 || (first != NULL && (g->clutX != first->clutX || g->clutY != first->clutY))) {
            return 0;
        }
        first = first != NULL ? first : g;
        if (!artFetch(font, fontSize, scratch, g->page * FONT_PAGE_HW + g->u / 4, g->v, hw, g->h, 4 * k, ART_CURSOR_ROW)) {
            return 0;
        }
        artGlyph[k].u = (ART_X - ART_PAGE_X + 4 * k) * 4 + g->u % 4;
        artGlyph[k].v = ART_Y + ART_CURSOR_ROW;
        artGlyph[k].w = g->w;
        artGlyph[k].h = g->h;
        artGlyph[k].dx = g->dx;
        artGlyph[k].dy = g->dy;
    }
    return artFetch(font, fontSize, scratch, first->clutX, first->clutY + PALETTE_WHITE, 16, 1, 0, ART_CURSOR_CLUT_ROW);
}

/* Whether the game's file cache holds the sheet right now (only to tell in the log; the art never uses it) */
static int sheetResident(void) {
    int i;

    for (i = 0; i < FILE_CACHE_SLOTS; i++) {
        if (FILE_CACHE.slots[i].file == FILE_MENU_SPRITES && FILE_CACHE.slots[i].state == FILE_LOADED) {
            return 1;
        }
    }
    return 0;
}

/* 1 when the window art is ready; the first call reads the disc (a few ms: two small files).
   Harness only: DW3_MENU_ART=0 forces the polygon frame (the fallback); DW3_MENU_ART=evict drops the sheet from
   the game's file cache first, as its heap would, to show the art does not depend on it being resident */
static int artReady(void) {
    if (artState == 0) {
        long sheetSize = 0, fontSize = 0;
        const char *off = getenv("DW3_MENU_ART");
        int resident;
        unsigned char *sheet;
        unsigned char *font;
        unsigned char *scratch;

        if (off != NULL && off[0] == 'e') {
            FILE_CACHE.free(FILE_MENU_SPRITES);
        }
        resident = sheetResident();
        sheet = off != NULL && off[0] == '0' ? NULL : readDiscFile(FILE_MENU_SPRITES, &sheetSize);
        font = sheet == NULL ? NULL : readDiscFile(FILE_FONT, &fontSize);
        scratch = malloc(ART_SCRATCH);

        artState = sheet != NULL && font != NULL && scratch != NULL && artBuild(sheet, sheetSize, font, fontSize, scratch) ? 1 : -1;
        if (artState < 0) {
            memset(artOurs, 0, sizeof(artOurs));
            fprintf(stderr, "menu: the window art could not be read from the disc, the polygon frame stays\n");
        } else {
            fprintf(stderr, "menu: window art read from the disc (the sheet is %s the game's file cache)\n",
                    resident ? "in" : "NOT in");
        }
        free(sheet);
        free(font);
        free(scratch);
    }
    return artState > 0;
}

static void artQuad(POLY_FT4 *p, int x, int y, int w, int h, int u, int v, int sw, int sh, int clutRow) {
    SetPolyFT4(p);
    setRGB0(p, 0x80, 0x80, 0x80);
    setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
    setUV4(p, u, v, u + sw - 1, v, u, v + sh - 1, u + sw - 1, v + sh - 1);
    p->tpage = GetTPage(0, 0, ART_PAGE_X, 0);
    p->clut = GetClut(ART_X, ART_Y + clutRow);
    push(p);
}

/* The window: the game's panel nine-sliced (its border and shadow keep their size) or the polygon frame */
static void drawWindow(int x, int y, int w, int h) {
    MfSlice sl[9];
    int i, n;

    if (artState > 0) {
        n = mfSlice9(w, h, artPanel.w, artPanel.h, 4, 6, 4, 6, sl);
        for (i = 0; i < n && panelCount < (int)(sizeof(panelPrims) / sizeof(panelPrims[0])); i++) {
            artQuad(&panelPrims[panelCount++], x + sl[i].dx, y + sl[i].dy, sl[i].dw, sl[i].dh, artPanelU + sl[i].su,
                    ART_Y + sl[i].sv, sl[i].sw, sl[i].sh, ART_PANEL_CLUT_ROW);
        }
    } else if (w >= 8) {
        panel(x, y, w, h);
    }
}

/* The selection cursor at the text line's origin x, y: the game's glyph on its swing, or a triangle */
static void drawCursor(int x, int y, int tick) {
    int f = mfCursorFrame(tick);

    if (artState > 0 && cursorCount < (int)(sizeof(cursorPrims) / sizeof(cursorPrims[0]))) {
        artQuad(&cursorPrims[cursorCount++], x + artGlyph[f].dx, y + artGlyph[f].dy, artGlyph[f].w, artGlyph[f].h, artGlyph[f].u,
                artGlyph[f].v, artGlyph[f].w, artGlyph[f].h, ART_CURSOR_CLUT_ROW);
    } else if (artState <= 0) {
        arrow(x + 1, y + 2, 1);
    }
}

/* ---- Prompts: an icon of the device in use, or its name when there is none ---- */

/* The text and the atlas cell (PI_NONE for none) of an action on the keyboard or on a
   pad of the icon family: see promptIconAction */
static int promptFor(int kb, int family, int act, const char **text) {
    return promptIconAction(kb, family, act, text);
}

/* A prompt at x, y (the text line's top left); returns its advance */
static int drawPrompt(int kb, int act, int x, int y, int palette) {
    const char *text;
    int cell = promptFor(kb, inputIconFamily(), act, &text);

    if (cell != PI_NONE) {
        drawIcon(cell, x, y - 1);
        return PI_CELL;
    }
    drawText(STYLE, x, y, text, palette);
    return textWidth(STYLE, text);
}

static int promptWidth(int kb, int act) {
    const char *text;

    return promptFor(kb, inputIconFamily(), act, &text) != PI_NONE ? PI_CELL : textWidth(STYLE, text);
}

/* A help line is a list of items: a prompt (act >= 0) or a text (act == -1) */
typedef struct HelpItem {
    int act;
    const char *text;
} HelpItem;

static int helpWidth(const HelpItem *items, int n, int kb) {
    int i, w = 0;

    for (i = 0; i < n; i++) {
        w += items[i].act >= 0 ? promptWidth(kb, items[i].act) : textWidth(STYLE, items[i].text);
    }
    return w;
}

static void drawHelp(const HelpItem *items, int n, int kb, int y) {
    int i, x = SCREEN_W / 2 - helpWidth(items, n, kb) / 2;

    for (i = 0; i < n; i++) {
        if (items[i].act >= 0) {
            x += drawPrompt(kb, items[i].act, x, y, PALETTE_GREY);
        } else {
            drawText(STYLE, x, y, items[i].text, PALETTE_GREY);
            x += textWidth(STYLE, items[i].text);
        }
    }
}

#define PANEL_X 16
#define PANEL_Y 2
#define PANEL_W 288
#define PANEL_H 236
#define TAB_Y 5
#define RULE_Y 20
#define DEVICE_Y 24
#define ROW_Y0_CONTROLS 40 /* below the device line */
#define ROW_Y0_OTHER 28
#define ROW_STEP 12
#define ROW_GAP 2 /* before "Reset to defaults" */
#define FOOT_RULE_Y 204
#define DESC_Y 207
#define HELP_Y 221
#define VALUE_X (PANEL_X + 140)
#define BAR_W 96

static int rowY(int tab, int row) {
    if (tab == CS_TAB_CONTROLS) {
        return ROW_Y0_CONTROLS + row * ROW_STEP + (row >= CS_CTL_ROW_RESET ? ROW_GAP : 0);
    }
    return ROW_Y0_OTHER + row * ROW_STEP;
}

/* The tab strip: each tab is a small window of the game's own panel art (or the polygon frame), the active one lit
   like the focused row with its name in yellow and a bar under it, the others dim. When the tabs do not all fit at
   full size the strip is a carousel (mfTabWindow): the run around the current tab that fits, with an arrow on each
   side that hides tabs; the keys that switch tabs are in the help line */
#define TAB_BOX_Y 3
#define TAB_BOX_H 17
#define TAB_PAD 5
#define TAB_GAP 3
#define TAB_ARROW_W 9 /* room for one carousel arrow (6 wide) and its margin */

static TmLayout tapLayout; /* where this frame put the tabs and rows, for the touch layer (touch_menu.h) */

static void drawTabs(int tab) {
    int tw[CS_TAB_COUNT], bx[CS_TAB_COUNT], bw[CS_TAB_COUNT], i, avail, left, first, count;

    avail = PANEL_W - 16;
    for (i = 0; i < CS_TAB_COUNT; i++) {
        tw[i] = textWidth(STYLE, csTabName(i));
    }
    count = mfTabWindow(tw, CS_TAB_COUNT, tab, avail, TAB_PAD, TAB_GAP, TAB_ARROW_W, &first);
    left = PANEL_X + 8;
    if (count < CS_TAB_COUNT) {
        if (first > 0) {
            arrow(left + 1, TAB_BOX_Y + 5, -1);
        }
        if (first + count < CS_TAB_COUNT) {
            arrow(left + avail - 7, TAB_BOX_Y + 5, 1);
        }
        left += TAB_ARROW_W;
        avail -= 2 * TAB_ARROW_W;
    }
    mfTabLayout(tw + first, count, avail, TAB_PAD, TAB_GAP, bx + first, bw + first);
    flat(PANEL_X + 10, RULE_Y, PANEL_W - 20, 1, 0x60, 0x80, 0xC0);
    for (i = first; i < first + count && tapLayout.tabCount < TM_MAX_TABS; i++) {
        tapLayout.tabs[tapLayout.tabCount].tab = i;
        tapLayout.tabs[tapLayout.tabCount].x = left + bx[i];
        tapLayout.tabs[tapLayout.tabCount].w = bw[i];
        tapLayout.tabCount++;
    }
    for (i = first; i < first + count; i++) {
        int x = left + bx[i], on = i == tab;

        drawWindow(x, TAB_BOX_Y, bw[i], TAB_BOX_H);
        if (on) {
            gradient(x + 3, TAB_BOX_Y + 3, bw[i] - 6, TAB_BOX_H - 6, 0x30, 0x50, 0xA0, 0x18, 0x28, 0x68);
            flat(x + 4, TAB_BOX_Y + TAB_BOX_H - 4, bw[i] - 8, 1, 0xF8, 0xE0, 0x50);
        }
        drawText(STYLE, x + (bw[i] - tw[i]) / 2, TAB_Y, csTabName(i), on ? PALETTE_YELLOW : PALETTE_GREY);
    }
}

/* The icon of a Controller tab binding in the icon family; PI_NONE on the keyboard tab,
   for an axis or an unbound slot */
static int bindingIcon(int row) {
    InputCfg cur;
    int *code;

    if (csDevice() != INPUT_DEV_PAD) {
        return PI_NONE;
    }
    inputCfgCurrent(&cur);
    code = inputCfgSlot(&cur, INPUT_DEV_PAD, csRowSlot(row));
    return code != NULL && *code >= 0 && !(*code & CONTROLLER_MAP_FLAG_AXIS) ? promptIconPad(inputIconFamily(), *code) : PI_NONE;
}

/* The left and right arrows of a value that Left and Right change, swinging outward like the cursor does */
static void cycleArrows(int xl, int xr, int y, int tick) {
    int off = mfArrowOffset(tick);

    arrow(xl - off, y, -1);
    arrow(xr + off, y, 1);
}

/* The value column of a widget row: toggle and cycle text with arrows when focused, slider bar and number */
static void drawValue(int tab, int row, int y, int sel, int tick) {
    char value[48];
    int kind = csRowKind(tab, row), pal = sel ? PALETTE_YELLOW : PALETTE_BLUE;
    int frac = csRowFraction(tab, row);

    csRowValue(tab, csDevice(), row, value, sizeof(value));
    if (frac >= 0) {
        flat(VALUE_X, y + 4, BAR_W, 5, 0x30, 0x40, 0x80);
        flat(VALUE_X + 1, y + 5, BAR_W - 2, 3, 0x0C, 0x14, 0x30);
        if (frac > 0) {
            flat(VALUE_X + 1, y + 5, (BAR_W - 2) * frac / 1000, 3, sel ? 0xF8 : 0x90, sel ? 0xE0 : 0xB0, sel ? 0x50 : 0xFF);
        }
        drawText(STYLE, VALUE_X + BAR_W + 8, y, value, pal);
        if (sel) {
            cycleArrows(VALUE_X - 9, VALUE_X + BAR_W + 8 + textWidth(STYLE, value) + 3, y + 2, tick);
        }
    } else if (kind == CS_ROW_TOGGLE || kind == CS_ROW_CYCLE) {
        int x = VALUE_X + (sel ? 10 : 0);

        drawText(STYLE, x, y, value, kind == CS_ROW_TOGGLE && value[1] == 'f' && !sel ? PALETTE_GREY : pal);
        if (sel) {
            cycleArrows(VALUE_X, x + textWidth(STYLE, value) + 4, y + 2, tick);
        }
    } else if (kind == CS_ROW_ACTION && value[0] != '\0') {
        drawText(STYLE, VALUE_X, y, value, PALETTE_YELLOW); /* an action waiting for its second Confirm */
    } else if (kind == CS_ROW_BINDING) {
        int cell = csWaitingRow() == row ? PI_NONE : bindingIcon(row);

        if (cell != PI_NONE) {
            /* the controller's button as its family draws it, the SDL name after it */
            drawIcon(cell, VALUE_X, y - 1);
            drawText(STYLE, VALUE_X + PI_CELL + 4, y, value, pal);
        } else {
            drawText(STYLE, VALUE_X, y, value, csWaitingRow() == row ? PALETTE_YELLOW : pal);
        }
    }
}

/* The foot line for the focused row's kind, in prompts of the device in use */
static void drawHelpLine(int tab, int row, int kb, int y) {
    static const HelpItem tabs[] = {{PI_ACT_TAB_PREV, NULL}, {PI_ACT_TAB_NEXT, NULL}, {-1, " Tab  "}};
    HelpItem line[16];
    int n = 0, kind = csRowKind(tab, row), i;

    for (i = 0; i < 3; i++) {
        line[n++] = tabs[i];
    }
    if (kind == CS_ROW_TOGGLE || kind == CS_ROW_CYCLE || kind == CS_ROW_SLIDER) {
        line[n++] = (HelpItem){PI_ACT_LEFT, NULL};
        line[n++] = (HelpItem){PI_ACT_RIGHT, NULL};
        line[n++] = (HelpItem){-1, " Change  "};
    } else if (kind == CS_ROW_BINDING || kind == CS_ROW_ACTION) {
        line[n++] = (HelpItem){PI_ACT_OK, NULL};
        line[n++] = (HelpItem){-1, kind == CS_ROW_BINDING ? " Bind  " : " Select  "};
    }
    line[n++] = (HelpItem){PI_ACT_BACK, NULL};
    line[n++] = (HelpItem){-1, " Back"};
    if (kind != CS_ROW_TOGGLE && kind != CS_ROW_CYCLE && kind != CS_ROW_SLIDER) {
        line[n++] = (HelpItem){-1, "  "};
        line[n++] = (HelpItem){PI_ACT_CLOSE, NULL};
        line[n++] = (HelpItem){-1, " Close"};
    }
    drawHelp(line, n, kb, y);
}

static int openTick; /* presented frames since the menu opened: the open animation and the cursor's swing */

static void resetBuild(void) {
    orderCount = glyphCount = gradCount = flatCount = triCount = iconCount = panelCount = cursorCount = 0;
}

static void buildScreen(void) {
    char label[48], desc[64];
    int tab = csTab(), row, top, last, cursor = csCursor(), kb = csDevice() == INPUT_DEV_KEYBOARD;
    int pkb = inputDeviceCurrent() == INPUT_DEV_KEYBOARD; /* prompts follow the last device used */
    int count = csTabRowCount(tab);
    int level = mfOpenLevel(++openTick);

    leaseEnsure(&leases[0]);
    if (artReady()) {
        leaseEnsure(&leases[1]);
    }

    resetBuild();
    if (level < MF_ONE) {
        /* opening: the panel grows from its middle, like the field menu's panels (updatePanel), then the contents */
        int w = PANEL_W * level / MF_ONE;

        drawWindow(PANEL_X + (PANEL_W - w) / 2, PANEL_Y, w, PANEL_H);
        touchMenuLayout(NULL); /* not tappable while it opens */
        return;
    }
    memset(&tapLayout, 0, sizeof(tapLayout));
    tapLayout.frameW = SCREEN_W;
    tapLayout.frameH = SCREEN_H;
    tapLayout.panelX = PANEL_X;
    tapLayout.panelY = PANEL_Y;
    tapLayout.panelW = PANEL_W;
    tapLayout.panelH = PANEL_H;
    tapLayout.tabY = TAB_BOX_Y;
    tapLayout.tabH = TAB_BOX_H;
    tapLayout.rowX = PANEL_X + 10;
    tapLayout.rowW = PANEL_W - 20;
    tapLayout.valueX = VALUE_X - 10;
    tapLayout.barX = VALUE_X;
    tapLayout.barW = BAR_W;
    tapLayout.rowStep = ROW_STEP;
    drawWindow(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    drawTabs(tab);

    if (tab == CS_TAB_CONTROLS) {
        int w1 = textWidth(STYLE, "Keyboard"), w2 = textWidth(STYLE, "Controller");
        int x = SCREEN_W / 2 - (w1 + 24 + w2) / 2;

        drawText(STYLE, x, DEVICE_Y, "Keyboard", kb ? PALETTE_YELLOW : PALETTE_GREY);
        drawText(STYLE, x + w1 + 24, DEVICE_Y, "Controller", kb ? PALETTE_GREY : PALETTE_YELLOW);
        cycleArrows(x - 14, x + w1 + 24 + w2 + 8, DEVICE_Y + 2, openTick);
        flat(PANEL_X + 10, 38, PANEL_W - 20, 1, 0x60, 0x80, 0xC0);
        tapLayout.deviceOn = 1;
        tapLayout.deviceY = DEVICE_Y - 1;
        tapLayout.deviceH = ROW_STEP;
    }
    if (count == 0) {
        drawCentered(STYLE, SCREEN_W / 2, 100, "Coming soon", PALETTE_GREY);
    }
    top = csTabWindowTop(tab);
    last = tab == CS_TAB_MODS && count - top > CS_MAX_WIDGETS ? top + CS_MAX_WIDGETS : count;
    for (row = top; row < last; row++) {
        int y = rowY(tab, row - top), sel = row == cursor;

        if (tapLayout.rowCount < TM_MAX_ROWS) {
            TmRow *tr = &tapLayout.rows[tapLayout.rowCount++];

            tr->index = row;
            tr->y = y - 1;
            tr->h = ROW_STEP;
            tr->slider = csRowFraction(tab, row) >= 0;
        }

        if (sel) {
            gradient(PANEL_X + 10, y - 1, PANEL_W - 20, ROW_STEP, 0x30, 0x50, 0xA0, 0x18, 0x28, 0x68);
            drawCursor(PANEL_X + 13, y, openTick);
        }
        csRowLabel(tab, row, label, sizeof(label));
        drawText(STYLE, PANEL_X + 28, y, label, sel ? PALETTE_YELLOW : PALETTE_WHITE);
        drawValue(tab, row, y, sel, openTick);
    }
    if (top > 0) {
        arrowV(PANEL_X + PANEL_W - 22, ROW_Y0_OTHER + 1, -1);
    }
    if (last < count) {
        arrowV(PANEL_X + PANEL_W - 22, ROW_Y0_OTHER + (CS_MAX_WIDGETS - 1) * ROW_STEP + 2, 1);
    }
    /* the foot: the description of the focused row (or the last result), then the keys */
    flat(PANEL_X + 10, FOOT_RULE_Y, PANEL_W - 20, 1, 0x60, 0x80, 0xC0);
    if (csMessage()[0] != '\0') {
        drawCentered(STYLE, SCREEN_W / 2, DESC_Y, csMessage(), PALETTE_RED);
    } else if (count > 0 && csRowDesc(tab, cursor, desc, sizeof(desc)) > 0) {
        drawCentered(STYLE, SCREEN_W / 2, DESC_Y, desc, PALETTE_BLUE);
    }
    drawHelpLine(tab, cursor, pkb, HELP_Y);
    touchMenuLayout(&tapLayout);
}

/* The FPS counter (Display tab, O2): a small box in the top right corner. Not drawn
   under the menu, whose tab strip is there. */
static void buildFps(int fps) {
    char text[16];
    int w, x;

    snprintf(text, sizeof(text), "%d", fps);
    w = textWidth(STYLE, text);
    x = SCREEN_W - 8 - w;
    flat(x - 4, 3, w + 8, 14, 0x08, 0x10, 0x30);
    drawText(STYLE, x, 4, text, PALETTE_WHITE);
}

void controlsScreenDraw(void) {
    int i, fps = optionsFpsFrame(); /* counts every presented frame, shown or not */


    if (hostVideoActive()) {
        touchMenuLayout(NULL);
        leasesRelease(); /* a movie writes VRAM as it likes */
        openTick = 0;
        return;
    }
    if (csIsOpen()) {
        buildScreen();
    } else {
        touchMenuLayout(NULL);
        leasesRelease(); /* the game gets its halfwords back */
        openTick = 0;
        if (fps < 0) {
            return;
        }
        resetBuild();
        buildFps(fps);
    }
    SetDefDrawEnv(&env, 0, 0, SCREEN_W, SCREEN_H);
    env.dfe = 1;
    env.isbg = 0;
    PutDrawEnv(&env);
    ClearOTagR((u_long *)ot, 2);
    /* addPrim puts a primitive at the head of the list: add the last drawn first */
    for (i = orderCount - 1; i >= 0; i--) {
        addPrim(&ot[1], order[i]);
    }
    DrawOTag((u_long *)&ot[1]);
}

/* Once per frame, from pcBootPadFrame: the screen cannot be shown over a movie */
static void settingsLog(const char *msg) {
    fprintf(stderr, "settings: %s\n", msg);
}

/* The sounds of the game's own menus (evidence: docs/design/options-menu.md, Sonidos): the field menu's panels
   open and close with SOUND_MENU_OPEN / SOUND_MENU_CLOSE (startPanel), its cursor moves with SOUND_CURSOR,
   Cross is SOUND_SELECT and Triangle SOUND_MENU_CANCEL (field_menu.c); the language menu's sideways moves are
   SOUND_MENU_MOVE (country_select.c). They go through SOUND.playSound like every other effect, so the effects
   volume (O4) applies. */
static int frameNo; /* presented frames the screen has been pumped for: the log's clock */

static void menuSound(int event) {
    static int dbg = -1;
    static const char *const NAMES[] = {"?", "open", "close", "move", "confirm", "cancel", "tab", "value"};
    int id = 0;

    switch (event) {
    case CS_SND_OPEN: id = SOUND_MENU_OPEN; break;
    case CS_SND_CLOSE: id = SOUND_MENU_CLOSE; break;
    case CS_SND_MOVE: id = SOUND_CURSOR; break;
    case CS_SND_CONFIRM: id = SOUND_SELECT; break;
    case CS_SND_CANCEL: id = SOUND_MENU_CANCEL; break;
    case CS_SND_TAB:
    case CS_SND_VALUE: id = SOUND_MENU_MOVE; break;
    default: break;
    }
    if (dbg < 0) {
        dbg = getenv("SND_DEBUG") != NULL;
    }
    if (dbg && event >= CS_SND_OPEN && event <= CS_SND_VALUE) {
        fprintf(stderr, "options: frame %d sound %s -> SOUND.playSound(0x%X)\n", frameNo, NAMES[event], id);
    }
    if (id != 0) {
        SOUND.playSound(id);
    }
}

/* DW3_OPTIONS_SCRIPT (harness only): once the screen is open, plays a list of menu actions, one every 6 frames,
   so a run proves the sounds and the repeat without timed key presses. Tokens, comma separated: u d l r (move),
   c (confirm), b (cancel), n p (next / previous tab), `h<u|d|l|r>:<frames>` (hold that direction that long, then
   let go), `w:<frames>` (wait). */
static const char *scriptAt;
static int scriptWait, scriptHold, scriptHeld;

static void scriptFrame(void) {
    static int inited;
    const char *t;

    if (!inited) {
        inited = 1;
        scriptAt = getenv("DW3_OPTIONS_SCRIPT");
    }
    if (scriptAt == NULL || !csIsOpen()) {
        return;
    }
    if (scriptHeld != 0) {
        if (--scriptHold <= 0) {
            csQueueHold(scriptHeld, 0);
            scriptHeld = 0;
            scriptWait = 6;
        }
        return;
    }
    if (scriptWait > 0) {
        scriptWait--;
        return;
    }
    t = scriptAt;
    if (*t == '\0') {
        return;
    }
    scriptWait = 6;
    if (t[0] == 'h' && t[1] != '\0' && t[2] == ':') {
        scriptHeld = t[1] == 'u' ? CS_ACT_UP : t[1] == 'd' ? CS_ACT_DOWN : t[1] == 'l' ? CS_ACT_LEFT : CS_ACT_RIGHT;
        scriptHold = atoi((u8 *)(t + 3));
        csQueueHold(scriptHeld, 1);
    } else if (t[0] == 'w' && t[1] == ':') {
        scriptWait = atoi((u8 *)(t + 2));
    } else {
        csQueueAction(t[0] == 'u' ? CS_ACT_UP : t[0] == 'd' ? CS_ACT_DOWN : t[0] == 'l' ? CS_ACT_LEFT
                      : t[0] == 'r' ? CS_ACT_RIGHT : t[0] == 'c' ? CS_ACT_CONFIRM : t[0] == 'b' ? CS_ACT_CANCEL
                      : t[0] == 'n' ? CS_ACT_TAB_NEXT : CS_ACT_TAB_PREV);
    }
    while (*scriptAt != ',' && *scriptAt != '\0') {
        scriptAt++;
    }
    if (*scriptAt == ',') {
        scriptAt++;
    }
}

void controlsScreenFrame(void) {
    static int reported;

    csSetSoundHook(menuSound);
    frameNo++;
    scriptFrame();
    csInit();
    inputDeviceInit();
    if (!reported) {
        reported = 1;
        csSettingsReportUnknown(settingsLog); /* every row was added by now */
    }
    csSetBlocked(hostVideoActive());
    csPump();
    optionsSystemFrame(); /* the model export job, a slice per frame */
}

/* The harness tokens: `options` (alias `controls`) opens the menu, `options:<tab>` also
   picks a tab (0 Controls .. 4 Audio), `options:<tab>.<row>` moves the cursor,
   a `k` / `p` suffix shows the keyboard / controller device and `w` starts the
   row's capture. `controls:<row>` is the Controls tab's row */
void controlsScreenHarness(int tab, int row, int device, int capture) {
    csSetSoundHook(menuSound);
    if (!csIsOpen()) {
        csOpen();
    }
    if (tab >= 0) {
        csSetTab(tab);
    }
    if (device >= 0) {
        csSetDevice(device);
        inputDeviceSet(device, -1); /* no events headless: the prompts follow the shown device */
    }
    if (row >= 0) {
        csSetCursor(row);
    }
    if (capture) {
        csAct(CS_ACT_CONFIRM);
    }
}
