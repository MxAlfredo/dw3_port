#ifndef PC_MENU_EXPORT_H
#define PC_MENU_EXPORT_H

/*
 * The menus by piece, with their real colours (readable-assets A5.2, ADR-016): one folder per screen of the game, one PNG per piece of it.
 *
 *   <data>/menus/<screen>/<piece>.png    an indexed PNG (16 colours for a 4-bit piece, 256 for an 8-bit one) in the disc's own index order, with the palette
 *                                        the game draws that piece with
 *   <data>/menus/<screen>/menu.json      "menus-menu-v1": every piece with its place on the disc and the frames that use it
 *   <data>/menus/manifest.json           the one manifest of the topic; the gym effect sets of the partner bundles (partner_export.h) live in it too
 *
 * <screen> is the disc file of the screen without its suffix, in lower case ("stmcrd" for MCARD/STMCRDTM.BIN: ADR-016 section 2.3, no English name is
 * proven). A screen is two disc files: its pages (the *TM file, raw 16-bit images the game loads side by side into VRAM, 64 halfwords per page) and its
 * sprite sheet (the *CS file). A *piece* is one rectangle of a sheet part (u, v, w, h and its texture mode) with one palette: the same rectangle drawn
 * with two CLUT rows is two pieces, and parts that name the same rectangle and palette are one piece. The palette is read where the sheet puts it
 * (imgPartPalette, image_data.h), which is not always the page of the pixels.
 *
 * Name of a piece: f<frame>p<slot>, the first frame (its id in the sheet) and slot (the position of the part in the frame; slot 0 is drawn last, on
 * top) that use it. Nothing in a path is a disc id. menu.json lists every piece, also the ones that cannot be cut ("unexported" says why), so a part of
 * the sheet is never dropped silently.
 *
 * The same layout is read from mods/<mod>/menus/<screen>/ (the packer of A5.3, menu_pack.h). The first half (cutting a screen into pieces) is pure stdlib and is
 * covered by pc/menuexptest.c; the second reads the disc through the same file list and sector reader as raw_export.h and writes through exp_core.h.
 * Bump MENU_EXPORT_VERSION when the bytes or the layout change.
 */

#include <stddef.h>
#include <stdint.h>

#include "image_data.h"
#include "sprite_data.h"

#define MENU_EXPORT_VERSION 1

typedef struct MenuStats {
    int pieces;     /* PNGs written */
    int unexported; /* pieces listed in menu.json that have no PNG (no palette, no pixels, an empty rectangle) */
    int outside;    /* of the PNGs, the ones whose rectangle reaches past the pages (those pixels are index 0) */
    int failed;     /* PNGs or files that could not be made or written */
} MenuStats;

/* "AAA/DAT/MCARD/STMCRDTM.BIN" -> "stmcrd". 0 when the path is not the pages of a screen (imgIsScreenFile and a TM file), else 1. */
int menuScreenName(const char *discPath, char *out, size_t cap);

/* Cuts one screen into pieces. `tm` is the *TM file, `cs` the *CS file (the sheet of the screen is its entry 0); `name` is the screen's folder and the two
   disc paths go into menu.json as its provenance. Files are handed to `emit` with a path relative to the screen's folder ("f000p00.png", "menu.json").
   Returns the number of PNGs emitted, -1 when the screen has no pages or no sheet (nothing is emitted) or a write failed. */
int menuScreenExport(const char *name, const char *tmDisc, const char *csDisc, const uint8_t *tm, size_t tmLen, const uint8_t *cs, size_t csLen,
                     SprEmitFn emit, void *ctx, MenuStats *st);

/* What a folder of pieces says about itself: the header of its JSON file. The menus (one folder per screen) and the stage objects (stage_obj_export.h, one
   per stage) share the cutting and the file; they differ in this and in the pages and sheet they cut from. Every string is JSON text the caller has escaped. */
typedef struct MenuPieceDoc {
    const char *format;     /* "menus-menu-v1" */
    const char *name;       /* the folder's name */
    const char *disc;       /* what goes inside the "disc" object: the provenance, e.g. "files": ["DAT/MCARD/STMCRDTM.BIN", ...] */
    const char *unexported; /* NULL for a folder with pieces; else why it has none: the file then lists no pages and no pieces (nothing is cut) */
    const char *jsonFile;   /* "menu.json" */
    const char *notes;      /* the help text of the file */
} MenuPieceDoc;

/* Cuts the pieces of `sheet` (a sprite sheet file, as menuSheetPieces reads it) out of `pages` and hands each PNG and the JSON file to `emit` (paths relative
   to the folder). The one thing menuScreenExport does for a screen, for any pair of pages and sheet. Returns the number of PNGs, or -1 when there are no pages
   or no frames (nothing is emitted) or a write failed. With doc->unexported only the JSON file is written (pages and sheet may be NULL) and 0 is returned. */
int menuPiecesExport(const ImgPages *pages, const uint8_t *sheet, size_t sheetLen, const MenuPieceDoc *doc, SprEmitFn emit, void *ctx, MenuStats *st);

/* A piece as the exporter names it (the packer of A5.3 finds each piece's PNG by this name): the rectangle and CLUT position the sheet gives, the CLUT row the
   first frame that uses it adds, and that frame (its id in the sheet) and slot. The file is menuPieceFile. */
typedef struct MenuPiece {
    ImgPart part;
    int row;
    int frame, slot;
} MenuPiece;

/* Every piece of a sheet file, in the order of menu.json (malloc'ed in *out, free it). The number of pieces, 0 when it is not a sheet, -1 when memory ran out. */
int menuSheetPieces(const uint8_t *cs, size_t csLen, MenuPiece **out);
/* "f000p00.png" */
void menuPieceFile(const MenuPiece *p, char *out, size_t cap);
/* NULL when the exporter cuts a PNG for this piece out of `pages`, else why it does not (the "unexported" text of menu.json) */
const char *menuPieceReason(const ImgPages *pages, const MenuPiece *p);

/* ---- the disc side ---- */

typedef struct MenuExportIo {
    /* the i-th file of the disc ("AAA/DAT/..." as on the disc, first sector, exact size); 1 when there is one, 0 past the last */
    int (*list)(void *ud, int i, char *path, size_t cap, long *lba, long *size);
    /* one raw 2352-byte frame of the disc image; 1 when read */
    int (*read)(void *ud, long lba, unsigned char *frame);
    void *ud;
} MenuExportIo;

typedef struct MenuExportStats {
    int screens;    /* screens cut */
    int skipped;    /* 1 when everything was up to date and the disc was not read */
    int written, kept, newFiles, failed;
    long bytes;
    int pieces, unexported, outside;
} MenuExportStats;

/* The bytes of a disc file (first sector and exact size), malloc'ed in whole sectors; NULL when it cannot be read. Shared with the stage objects. */
uint8_t *menuDiscRead(const MenuExportIo *io, long lba, long size);

void menuExportSetup(const char *dataDir, const MenuExportIo *io);
/* "all", or screen names (stmcrd, stdglb...) separated by commas or spaces. Synchronous. The number of screens cut, 0 when up to date, or -1 when there is
   nothing to do (no data folder, no reader, nothing valid). */
int menuExportRun(const char *spec);
void menuExportTotals(MenuExportStats *out);

#endif
