#ifndef PC_STAGE_NEW_H
#define PC_STAGE_NEW_H

/*
 * The loader of the field stages that mods add (new-stages T5, ADR-012, docs/stages.md section 13).
 *
 * A mod's mods/<mod>/new-stages/<name>/ holds a stage-new-v1: stage.json (the header and the sections of the stage patch layer, "add" only) and PNGs of the map
 * (indexed, one pixel per pixel of the map): background.png cut into 128 px tiles (a Z_STAGE PACK, map_pack.h), floor.png, triggers.png and areas.png (the three
 * layers of the TMPK, map_layers.h). The stage is "<mod>:<name>", the folder name being the name.
 *
 * Order of use (pc/boot.c pcInstallStageMods; each step needs the one before it):
 *   1. nsScan(host): reads the header and builds the TMPK and the PACK in memory for every stage folder; a stage that is not valid is refused here whole
 *      ("[stages] <path> rejected: <reason>") and is never registered; the others are msRegister'ed (mod_stages.h);
 *   2. msSeal();
 *   3. smLoad(host) with host->newCheck = nsCheck and host->stageRefused = nsRefuse: reads the sections (the stage patch layer's own parser), asks nsCheck whether
 *      the map and the sections agree, and refuses a stage whole when they do not;
 *   4. nsInstall(): the virtual files of the stages that stood (msSetFiles) and what the field's setup reads (MsStage).
 *
 * Portable C. It reads the PNGs with png_read.h; the game side stays in pc/boot.c (pcModStageSetup).
 */

#include <stddef.h>

#include "stage_mods.h"

#define NS_DIR "new-stages"
#define NS_FILE "stage.json"
#define NS_CELL 128         /* the size of a map tile, and the unit the map's size must be a multiple of */
#define NS_MAX_TILES 255    /* MP_MAX_TILES: the streamer keeps a tile in a u8, 0xFF means none */

/* Step 1. Returns how many stages it registered. Forgets what an earlier call kept. */
int nsScan(const SmHost *host);

/* Step 3's callbacks (SmHost.newCheck, SmHost.stageRefused). nsCheck: 0 when the stage's trigger and area cells name only slots and battle areas its sections define. */
int nsCheck(const char *key, const SmNewInfo *info, char *why, size_t whySize);
void nsRefuse(const char *key);

/* Step 4. Returns how many stages it installed. Frees what the scan kept. */
int nsInstall(void);

/* Forgets everything (a reload, or the end of a test). */
void nsReset(void);

/* How many stages the scan kept (before a check refused any) */
int nsPending(void);

#endif
