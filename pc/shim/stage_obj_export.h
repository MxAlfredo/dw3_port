#ifndef PC_STAGE_OBJ_EXPORT_H
#define PC_STAGE_OBJ_EXPORT_H

/*
 * The map objects of the field stages by piece, with their real colours (readable-assets A6.2, ADR-016): one folder per stage, one PNG per piece of it.
 *
 *   <data>/stages/s<nnn>/objects/<piece>.png    an indexed PNG (16 colours for a 4-bit piece, 256 for an 8-bit one) in the disc's own index order, with the palette
 *                                               the game draws that piece with
 *   <data>/stages/s<nnn>/objects/objects.json   "stages-objects-v1": every piece with its place on the pages and the frames that use it
 *   <data>/stages/manifest.json                 the manifest of the topic (stage_data.h writes s<nnn>/stage.json there too, stage_export.h the tiles; `exporter."stage-objects"` in its header marks this exporter)
 *
 * It is the menus' exporter (menu_export.h) with another pair of pages and sheet; a piece is the same thing and is named the same way (f<frame>p<slot>), and
 * objects.json has the fields of menu.json under another format name. A stage is three disc files and one number the game's code fixes:
 *
 *   S<nnn>MASK.BIN   the pages: TIMs the game loads into VRAM at (0x140, 0x100) side by side, 64 halfwords a page (FieldState.imageFile, file_loader.c).
 *                    imgStagePagesOpen reads them: 4-bit art as raw 16-bit pages, 8-bit art with the CLUT the archive's TIM carries.
 *   S<nnn>TMPK.BIN   an archive: one entry is the sprite sheet of the stage's objects, the others are map layers (map_layers.h). The entry is NOT always 0:
 *                    FieldState.sheetEntry = (file << 16 | entry), set by each stage's setupStage (wstag202.c: entry 1; wstag920.c: entry 3; wstag961.c: entry 4;
 *                    file_loader.c requests the file, field.c hands it to FIELDSTG_createMapObjects).
 *   <data>/stages/s<nnn>/stage.json   (stage-v1, stage_data.h) is where the port already keeps that number: its "sheetEntry". The exporter reads it from there, so the stage
 *                    data (pcExportStageData) has to be written first, which is the order the port starts in; a stage without that file is listed without pieces.
 *
 * A stage that cannot be cut (no stage data, no sheet, no MASK file) still gets an objects.json with "unexported" saying why: that is a fact about the disc,
 * not an error. Pieces are deduplicated inside a stage only (one PNG per rectangle and palette row); two stages that draw the same art have a file each.
 *
 * What it does not know: the objects a stage animates by cycling the CLUT row (StageTile.cycle == 2, map_objects.c). That table is C data of the stage's overlay
 * (FIELDSTG_state.objects), not a disc file, so only the rows the sheet's own frames use are exported; a cycled row beyond them has no PNG (the palette of a
 * piece is its CLUT row, so a cycled piece draws with the colours of the row its frame names and the game changes the row at run time).
 * Palettes of an 8-bit piece sit outside the pages, in the CLUT of the archive's TIM (row clutY - 256 of it: "row" >= 256 in objects.json); the packer writes
 * them back there (stage_obj_pack.h, A6.3).
 *
 * The same layout is read from mods/<mod>/stages/s<nnn>/objects/. The cutting is pure stdlib and is covered by pc/stageobjexptest.c; the disc side reads
 * through the same file list and sector reader as menu_export.h and writes through exp_core.h. Bump STAGE_OBJ_EXPORT_VERSION when the bytes or the layout change.
 */

#include <stddef.h>
#include <stdint.h>

#include "menu_export.h"

#define STAGE_OBJ_EXPORT_VERSION 1

/* "AAA/DAT/FIELD/STAGE/S202MASK.BIN" or ...S202TMPK.BIN -> 202. -1 for any other path. */
int stageObjNumber(const char *discPath);
/* The number after "sheetEntry" in a stage-v1 file (file << 16 | entry, as the game keeps it). 0 and *ref, or -1 when the text has none. */
int stageObjSheetRef(const char *json, size_t len, int *ref);
/* The sheetEntry of stage `stage` in the stage data of a data folder (<dataDir>/stages/s<nnn>/stage.json): 0 and *ref, or -1 when there is no such file or it names none. */
int stageObjSheetRefIn(const char *dataDir, int stage, int *ref);
/* Entry `entry` of a TMPK file as a sprite sheet file (the layout menuSheetPieces reads: a word with the offset of the sheet, then the sheet), malloc'ed;
   NULL when the entry is absent or is not a sheet. */
uint8_t *stageObjSheetFile(const uint8_t *tmpk, size_t tmpkLen, int entry, size_t *outLen);

/* Cuts the objects of stage `stage` into pieces: `mask` is its MASK file, `tmpk` its TMPK file and `entry` the entry of the sheet. maskDisc and tmpkDisc go into
   objects.json as its provenance. Files are handed to `emit` with a path relative to the stage's objects folder ("f000p00.png", "objects.json"). Returns the
   number of PNGs, or -1 when a write failed. A stage that cannot be cut writes the objects.json with its reason and returns 0; *why (may be NULL) is then that
   reason. */
int stageObjExport(int stage, const char *maskDisc, const char *tmpkDisc, const uint8_t *mask, size_t maskLen, const uint8_t *tmpk, size_t tmpkLen, int entry,
                   SprEmitFn emit, void *ctx, MenuStats *st, const char **why);
/* The objects.json of a stage the disc side could not even try (no stage data, no TMPK file ...): `why` is JSON text. 0, or -1 when the write failed. */
int stageObjUnexported(int stage, const char *why, SprEmitFn emit, void *ctx);

/* ---- the disc side ---- */

typedef struct StageObjIo {
    MenuExportIo disc; /* the file list and the sector reader */
    /* the disc path ("AAA/DAT/...") of a file number of the game's file table; 1 when it has one */
    int (*fileName)(void *ud, int file, char *path, size_t cap);
} StageObjIo;

typedef struct StageObjStats {
    int stages;     /* stages with pieces */
    int noObjects;  /* stages listed without any (unexported) */
    int skipped;    /* 1 when everything was up to date and the disc was not read */
    int written, kept, newFiles, failed;
    long bytes;
    int pieces, unexported, outside;
} StageObjStats;

void stageObjExportSetup(const char *dataDir, const StageObjIo *io);
/* "all", or stage names (s202, 202) separated by commas or spaces. Synchronous. The number of stages cut, 0 when up to date, or -1 when there is nothing to do
   (no data folder, no reader, nothing valid). */
int stageObjExportRun(const char *spec);
void stageObjExportTotals(StageObjStats *out);

#endif
