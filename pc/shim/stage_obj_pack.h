#ifndef PC_STAGE_OBJ_PACK_H
#define PC_STAGE_OBJ_PACK_H

/*
 * The packer of the stage objects by piece (readable-assets A6.3, ADR-016 section 6c): the stages/s<nnn>/objects/<piece>.png files of the asset layers go
 * back into the stage's MASK file when the game loads it, so a modder edits one piece of one object and the game shows it. stage_obj_export.h writes the
 * pieces; this file reads them. The reading, the planning and the writing are the menus' (menu_pack.h): a stage is another pair of pages (its MASK file's TIMs)
 * and sheet (the entry of its TMPK file that the stage data names).
 *
 * Geometry: the original's, as for the menus. The rectangle, texture mode and CLUT of a piece come from the sprite sheet on the disc that the stage's
 * sheetEntry (data/stages/s<nnn>/stage.json) names, never from the mod's objects.json. 4-bit colours are written into the pages at the CLUT position; 8-bit colours
 * into the CLUT of the TIM that the game keeps at that colour (menu_pack.h, "Stages"). The STP bit of the disc stays and 0x0000 stays transparent.
 *
 * Cost: a stage nobody edits costs nothing. At startup the layers are looked at once and the stages that have an edited piece are remembered (a mod's
 * stages/s<nnn>/objects/ with a PNG; the data folder's PNG whose hash is not the manifest's). A stage outside that set is not planned at all: the image layer's
 * hook says no at once. Only a stage in it reads its stage data and its TMPK sheet, and builds its plan the first time the game loads its MASK file; the plan
 * is dropped after the file is built.
 *
 * Two pieces that disagree about a rectangle or a CLUT colour: the later layer wins, with one log line naming the stage. This is the only way to edit
 * the objects of a stage: the pages of its MASK file are not exported to images/ and a PNG left there for one is not read (A6.4). The BG files of the stages are
 * not objects and stay on images/.
 * Covered by pc/stageobjpacktest.c (synthetic) and the disc round trip of the A6.3 notes.
 */

typedef struct StageObjPackStats {
    int edited; /* stages that have an edited piece in some layer (the startup index) */
    int built;  /* of those, the ones whose plan was built (the game loaded their MASK file) */
} StageObjPackStats;

/* Indexes the stages that have edited pieces and sets the stage hooks of the image layer. Call once at startup, after the layers and the data folder are set
   (mods.h) and the stage objects are exported. Without the hooks (or with no layer) nothing changes. */
void stageObjPackStart(void);
/* Removes the hooks and forgets the index (tests, shutdown) */
void stageObjPackReset(void);
/* --validate-mod: logs (as "<mod folder>/stages/<name> ignored: <why>") what a mod's stages/ folder holds that nothing reads: a folder that is not s<nnn>, a stage
   `discHas` says the disc lacks (it is asked for the stage's MASK file path), and anything in a stage folder but objects/, tiles/ and stage.json. The files of objects/ are the plans'
   (menu_pack.h, menuPackValidating), which this does not repeat; tiles/ is stageModValidate's and stage.json the stage patch loader's. */
void stageObjPackValidate(int (*discHas)(const char *discPath));
void stageObjPackTotals(StageObjPackStats *out);

#endif
