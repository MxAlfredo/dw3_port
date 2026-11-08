#ifndef PC_SPRITE_EXPORT_H
#define PC_SPRITE_EXPORT_H

/*
 * The original field sprites, one folder each (readable-assets A7.5, ADR-016): every FIELD/SPRT/<NAME>.BIN of the disc but the eight partner followers.
 *
 *   <data>/sprites/<name>/<NNN>.png     one indexed PNG per frame (NNN = the entry's index in the archive, 3 digits; 4 bits, its own 16-colour palette)
 *   <data>/sprites/<name>/sprite.json   the frames, animations and sets: "sprites-sprite-v1" (the fields of sprite_data.h's "sprite-v1")
 *   <data>/sprites/manifest.json        asset-manifest-v1 for the topic; `names` maps each folder to its disc identity ("p001play": "dw3:sprite/p001play")
 *
 * <name> is the lower case of the disc file's name (FIELD/SPRT/P001PLAY.BIN -> p001play, sprFolder in sprite_data.h): the game addresses these files by that name
 * (the stage's image tables and the actors' file words name the file, not a number), so it is the stable identity; the disc has no id for them, hence the identity
 * `dw3:sprite/<name>`. The followers of the eight original partners (P003PKEN ... P010PATA) are part of the partner bundles (partners/<name>/field/) and are not
 * written here. The files are what images/field/sprt/<NAME>/ held before A7.5, byte for byte (the JSON differs in the format name only).
 *
 * A mod's mods/<mod>/sprites/<name>/<NNN>.png (or an edited data/sprites/<name>/<NNN>.png) replaces that frame through the override table of image_data.c
 * (imgSpriteOverridesBuild reads the sprites topic). A folder of a mod whose name is an original's is that sprite's frames, not a new sprite; any other name is a new
 * sprite of the mod (sprite_data.h, sprBankAdd).
 *
 * The disc side lists the disc through the same file list and sector reader as menu_export.h and writes through exp_core.h. Bump SPRITE_EXPORT_VERSION when the bytes
 * or the layout change. It is covered by pc/sprexptest.c.
 */

#include <stddef.h>
#include <stdint.h>

#include "menu_export.h"

#define SPRITE_EXPORT_VERSION 1

typedef struct SpriteExportStats {
    int sprites; /* sprite files converted */
    int frames;  /* PNGs */
    int skipped; /* 1 when everything was up to date and the disc was not read */
    int written, kept, newFiles, failed;
    long bytes;
} SpriteExportStats;

void spriteExportSetup(const char *dataDir, const MenuExportIo *io);
/* "all", or sprite names (p001play, P001PLAY) separated by commas or spaces. Synchronous. The number of sprites written or checked, 0 when up to date or when no
   name matches a sprite of the disc, or -1 when there is nothing to do (no data folder, no reader). */
int spriteExportRun(const char *spec);
void spriteExportTotals(SpriteExportStats *out);

#endif
