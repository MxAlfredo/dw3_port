/*
 * The options menu's window art (feature controls-config, O5b): the pure side. The frame and the cursor of the
 * game's own menus are drawn from its sprite sheet (FILE_MENU_SPRITES) and its font images (FILE_FONT), read from
 * the user's disc at run time; this file only parses those bytes, lays the pieces out and times the animations.
 * It has no game, PsyQ or SDL dependency, so inputtest drives it with synthetic data (no retail bytes anywhere
 * in the repo). The VRAM side is in controls_draw.c; layout and evidence in docs/design/options-menu.md.
 */
#ifndef MENU_FRAME_H
#define MENU_FRAME_H

#include <stddef.h>

#define MF_ONE 4096        /* 1.0 in the game's fixed point (ONE) */
#define MF_OPEN_FRAMES 10  /* the field menu's panel duration (openFieldMenu) */

/* The open animation: the scale (0..MF_ONE) of the panel `frame` presented frames after the menu opened (1 is the
   first drawn frame); it grows by ONE / 10 a frame like updatePanel and stays at ONE */
int mfOpenLevel(int frame);

/* The cursor's glyph (0..3, CURSOR_FRAMES' 0x8185..0x8188) `tick` presented frames in: it rests on glyph 0 for 32
   frames, then swings 1, 2, 3, 2 for 6 frames each (updateCursor's idleDelay 0x20 and frameDelay 6) */
int mfCursorFrame(int tick);

/* How far (pixels, outward from the value) a cycle row's arrows are on `tick`: the cursor's own swing, so they
   rest and swing in step with it (O5d) */
int mfArrowOffset(int tick);

/* The tab strip: n boxes in a row, each its text's width plus `pad` on both sides and `gap` between boxes, centred in
   `avail`. When that is too wide the gap and then the padding shrink (to 0 at least). x[i] and w[i] are box i's
   left edge (from the strip's left) and width; returns the strip's width */
int mfTabLayout(const int *textW, int n, int avail, int pad, int gap, int *x, int *w);

/* The tab carousel: when the n tabs do not fit in `avail` at full `pad` and `gap`, the run of tabs around `current`
   that fits in `avail` less `arrowW` on each side (the arrows that mark the hidden tabs), grown one tab at a time
   to the right and then the left so the current tab stays near the middle. *first is the run's first tab; returns
   how many (all n, from 0, when they fit; at least the current one) */
int mfTabWindow(const int *textW, int n, int current, int avail, int pad, int gap, int arrowW, int *first);

/* One piece of a nine-slice: where it goes (relative to the panel's top left) and which rectangle of the
   source image it shows */
typedef struct MfSlice {
    int dx, dy, dw, dh;
    int su, sv, sw, sh;
} MfSlice;

/* A w x h panel from a sw x sh source whose margins are ml, mr, mt, mb: the corners keep their size, the edges
   and the middle stretch. A panel narrower (or lower) than its margins shrinks them in proportion. Fills `out`
   (9 slices, none empty) and returns the count, 0 for an empty panel */
int mfSlice9(int w, int h, int sw, int sh, int ml, int mr, int mt, int mb, MfSlice out[9]);

/* A part of the sprite sheet: a rectangle of the 4-bit texture (u in texels from the font page's left edge) and
   the CLUT it uses (relative to the texture's position, as SpritePart) */
typedef struct MfPart {
    int u, v, w, h, clutX, clutY;
} MfPart;

/* The sheet is entry 0 of the file (a table of offsets); a frame is a list of parts. Reads the `which`th part of
   frame `frame`, or the one of the largest area when which < 0. 1 on success; 0 when the bytes are not a sheet
   (every offset is checked against `size`) */
int mfSheetPart(const unsigned char *file, long size, int frame, int which, MfPart *out);

/* An image of the font archive as the game uploads it: w x h halfwords at the page of its entry */
typedef struct MfImage {
    int w, h;
    const unsigned char *px; /* w * h little-endian halfwords */
} MfImage;

/* "RLEN" data (a run-length encoding, see the game's timLoaderLoadArchive) unpacked from src to dst; returns the
   bytes written, or -1 when it would not fit or is cut short */
long mfRlenUnpack(const unsigned char *src, long srcSize, unsigned char *dst, long cap);

/* Image `index` of the archive that is entry 0 of the file: a TIM without a CLUT, packed or not. A packed one is
   unpacked into scratch (cap bytes). 1 on success, 0 when it is not there or not valid */
int mfArchiveImage(const unsigned char *file, long size, int index, unsigned char *scratch, long cap, MfImage *out);

/* Copies the w x h halfwords at (x, y) of the image to dst (row stride in halfwords); 0 when the rectangle is
   not inside the image */
int mfImageCopy(const MfImage *im, int x, int y, int w, int h, unsigned short *dst, int stride);

/* What the menu borrows from VRAM keeps the game's own halfwords in `backup` while `ours` is on screen. A write of
   the game's into the area shows as a halfword that is neither (cur != ours): it becomes the backup, so closing
   the menu gives it back. Returns how many halfwords changed hands. */
int mfLeaseSync(unsigned short *backup, const unsigned short *ours, const unsigned short *cur, int n);

#endif
