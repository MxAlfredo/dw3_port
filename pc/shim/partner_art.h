#ifndef PC_PARTNER_ART_H
#define PC_PARTNER_ART_H

/*
 * A partner's own 2D art (Betamon pack R4). Every screen that shows a partner draws a 32x32, 4-bit frame of its sprite sheet (docs/formats.md,
 * 4.12): the portrait. A partner a mod adds brings one indexed PNG, its cells of 32x32 side by side (up to PA_CELLS_MAX, at most 16 colours, index 0
 * transparent); the animation walks the cells back and forth like the original's (0 1 2 1, 7 8 9 10 9 8). This file is the pure part: the PNG
 * becomes the texels and the palette a VRAM slot takes, and a one-frame sheet points the sprite drawer at one cell of the slot
 * (partner_art_vram.c uploads them and draws).
 */

#include <stddef.h>
#include <stdint.h>

#include "png_read.h"

#define PA_CELL 32       /* a cell is 32x32 pixels, as every portrait of the game */
#define PA_CELLS_MAX 8   /* cells of one art: 256 pixels, one texture page row */
#define PA_CELL_HW 8     /* halfwords of a cell's row: 32 texels of 4 bits */
#define PA_ICON_W 44     /* the memory card's icon of a partner: 44x28 (docs/formats.md 4.12) */
#define PA_ICON_H 28

typedef struct PaArt {
    int cells;
    int w, h;         /* one cell's size in pixels: 32x32 for a portrait, 44x28 for an icon (one cell) */
    int rowHw;        /* halfwords of a row of the whole image: cells * w / 4 */
    uint16_t pal[16]; /* the CLUT, 15-bit PlayStation colours; index 0 is transparent (0), an opaque black is 0x8000 */
    uint16_t *img;    /* h rows of rowHw halfwords (a portrait: PA_CELL rows of cells * PA_CELL_HW): texel x of a row in nibble x & 3 of halfword x / 4 */
} PaArt;

/* An indexed image with more colours than a 4-bit palette holds (readable-assets A4a): the colours it really uses are merged, the closest pair (weighted by pixels)
   first, until `maxColours` are left (16 for a portrait or icon); transparent entries (tRNS alpha 0) become the one index 0. *out is a new image (free it with
   pngImageFree) that paFromIndexed accepts. 0 on success; -1 for an image that has no pixels or whose indices pass its palette. The caller logs the warning (it
   knows the file). An image already within the limit comes out renumbered (transparent first) but the same. */
int paReduce(const PngImage *in, PngImage *out, int maxColours);

/* Converts an indexed PNG (pngReadIndexed). 0 on success; else -1 and a reason in `why` (NULL allowed) */
int paFromIndexed(const PngImage *p, PaArt *out, char *why, size_t whySize);
int paFromPng(const uint8_t *data, size_t len, PaArt *out, char *why, size_t whySize);
/* The memory card icon: one 44x28 image under the same rules (at most 16 colours, index 0 transparent) */
int paIconFromIndexed(const PngImage *p, PaArt *out, char *why, size_t whySize);
int paIconFromPng(const uint8_t *data, size_t len, PaArt *out, char *why, size_t whySize);
/* paFromPng / paIconFromPng for a file that may have too many colours (A4a): more than 16 are merged with paReduce and *reducedFrom is how many it had (0: it
   fitted). The cells and size rules are the same. */
int paFromPngFit(const uint8_t *data, size_t len, int icon, PaArt *out, int *reducedFrom, char *why, size_t whySize);
void paFree(PaArt *a);

/* The animation of an art of `cells` cells: its length in steps and the cell of a step (the first cell outside 0..steps-1) */
int paSteps(int cells);
int paCellAt(int cells, int step);
int paNext(int cells, int step); /* the step after `step`: step + 1, or 0 at the end of the walk */

/* A sheet of the layout spriteDrawerDraw reads (include/engine/gfx.h, SpritePart): frame 0 is one part, the cell `cell` of a slot whose texels
   start at (u, v) of its texture page, its palette at (clutX, clutY) relative to the drawer's texture position, drawn at the offset (x, y) */
typedef struct PaPart {
    int16_t u, v, w, h, clutX, clutY, mode;
} PaPart;
typedef struct PaSheet {
    int32_t offsets[3]; /* to the parts, to the frame ids, to frame 0 */
    uint8_t ids[4];
    int16_t frame[6]; /* parts, CLUT row, opacity (-1), then the part's index, x, y */
    PaPart part;
} PaSheet;
void paSheetBuild(PaSheet *s, int cell, int u, int v, int clutX, int clutY, int x, int y);
/* the same for a cell of cw x ch pixels (an icon): cell `cell` starts at u + cell * cw */
void paSheetBuildSize(PaSheet *s, int cell, int cw, int ch, int u, int v, int clutX, int clutY, int x, int y);

/* The VRAM side (partner_art_vram.c, the host only): reads the PNG `file` as the art of new partner `partner` (`name` for the log) and gives it a slot of
   the band; 0 on success. pcPartnerArtsReset forgets them all. */
int pcPartnerArtLoad(int partner, const char *file, const char *name);
void pcPartnerArtsReset(void);
int pcPartnerIconLoad(int partner, const char *file, const char *name);

#endif
