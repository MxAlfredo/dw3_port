#ifndef PC_MENU_PACK_H
#define PC_MENU_PACK_H

/*
 * The packer of the pieces (readable-assets A5.3, ADR-016 section 6b): the menus/<screen>/<piece>.png files of the asset layers go back into the
 * screen's pages, so a modder edits one piece and the game shows it. menu_export.h writes the pieces; this file reads them. It is written for any pair of
 * pages and sprite sheet: the menus are one user of it (the hooks below) and the stage objects another (stage_obj_pack.h, A6.3), which differ only in the
 * folder the pieces are read from (PackSource) and in where an 8-bit palette lives (see "Stages").
 *
 * Geometry: the original's (owner decision 2026-11-07). A piece goes back to its own rectangle of the page space (u, v, w, h and its texture mode) and its
 * own palette goes to its CLUT position (clut x and absolute row of menu.json), which may be on another page than the pixels. Nothing moves: the game asks for
 * every texture at a fixed place of VRAM. The places come from the screen's sprite sheet on the disc (the bytes menu.json was made from), not from the
 * mod's menu.json, so a stale or hand-edited menu.json cannot move a piece. A piece the exporter lists with file null has no PNG and is left as it is.
 *
 * Layers (mods.h, B4): for each piece the first layer that has a usable file wins: the mods from the last to the first, then the data folder, whose file only
 * counts when menus/manifest.json says the user edited it (its hash is not the exporter's). A PNG of another size than the piece is refused with a log line
 * and the next layer is tried. An indexed PNG within 16 colours (256 for an 8-bit piece) uses its own indices; one with more colours is reduced (partner_art.h
 * paReduce) with a log line. Colour 0x0000 of the disc stays transparent, and each palette halfword keeps its STP bit from the disc.
 *
 * What is written: only what differs from the disc. A piece whose indices and colours equal the disc's changes nothing (so a mod that copies the whole
 * export is a no-op), and the pixels and the palette of a piece are judged apart. Pieces that share a rectangle (drawn with two CLUT rows) share its indices;
 * pieces that share a CLUT position share the palette. Two edited pieces that disagree about a shared rectangle or palette: the one of the layer that
 * wins (the later mod) is written, with one log line (inside one layer, the first piece in menu.json order). Rectangles that only partly overlap are
 * written from the losing layer to the winning one, so the winner shows.
 *
 * Stages (A6.3): the pages are the TIMs of a stage's MASK file (any TIM's image is a page, imgStagePagesOpen) and the sheet comes from the stage's TMPK. A
 * 4-bit palette is written into the pages at its CLUT position, as for a menu. An 8-bit piece's palette is not in the pages: it goes into the CLUT of the TIM
 * that the game keeps at that colour, the last TIM of the archive whose CLUT covers it (image_data.h imgPagesAltOwner), row - IMG_ALT_CLUT_ROW of it.
 *
 * The files are read once, when the screen's pages are first built (the image layer builds a file the first time the game asks for it), and kept: no polling.
 * The module is a hook of the image layer (image_data.h ImgMenuHooks); it is applied after the partner art and over a page of images/.
 * --validate-mod walks every screen through imgValidateMods (image_data.h), so the plan of each one is built and its refusals are logged, as at the first use.
 * Covered by pc/menupacktest.c (synthetic) and the disc round trip of the A5.3 notes.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct MenuPackStats {
    int screens;               /* screens (or stages) whose pieces were looked at */
    int pieces;                /* PNGs that were read and accepted */
    int equal;                 /* of those, pieces equal to the disc (nothing to write) */
    int rejected;              /* PNGs refused (size, colours, unreadable) */
    int reduced;               /* PNGs whose colours were reduced */
    int conflicts;             /* shared rectangles or palettes the edited pieces disagreed about */
    long texelsWritten, halfwordsWritten; /* what the patches wrote into the pages (also where the value was the disc's already) */
    long texelsChanged, halfwordsChanged; /* of those, the ones whose value really changed */
} MenuPackStats;

/* Where a source's pieces are. `topic` and `dir` locate them in the asset layers (<layer>/<topic>/<dir>/<piece>.png; the data folder's manifest is
   <topic>/manifest.json); `name` is what the log lines call the source; `tag` is their log tag. `stage`: the pages are a stage's MASK file. */
typedef struct PackSource {
    const char *tag, *topic, *name, *dir;
    int stage;
} PackSource;

/* The three steps of a source's packer, for the image layer's hooks (image_data.h ImgMenuHooks): plan the pieces of one file (`file`: the pages; `sheet`: its
   sprite sheet file) and 1 when some piece differs from the disc; patch each TIM of the file in walk order (a writable copy, as the hook gets it; 1 when a
   byte changed); end the file. */
int menuPackBegin(const PackSource *src, const uint8_t *file, size_t len, const uint8_t *sheet, size_t sheetLen);
int menuPackPatch(const int *chain, int depth, uint8_t *tim, size_t len);
void menuPackEnd(void);
/* Is a data-folder file the user's: its hash is not the one <topic>/manifest.json recorded (or it is not there). The manifest is read once per topic. */
int menuPackDataFileEdited(const char *tag, const char *topic, const char *rel, const uint8_t *bytes, size_t len);

/* Sets the hooks of the image layer and forgets what was read. Call once at startup, after the layers and the data folder are set (mods.h). */
void menuPackStart(void);
/* Removes the hooks (tests, shutdown) */
void menuPackReset(void);
void menuPackTotals(MenuPackStats *out);
/* Tests only: also write the pieces that equal the disc's, to exercise the write path with a copy of the export. Off by default. */
void menuPackWriteUnchanged(int on);

/* --validate-mod: the screens' plans also report what the mods ship that no piece takes (a file no piece is called, a piece two mods ship), as log lines
   the validator files under the mod. The rejected PNGs (path, size got and size of the piece) are reported with or without it. Off by default. */
void menuPackValidating(int on);

#endif
