#ifndef PC_STAGE_MOD_H
#define PC_STAGE_MOD_H

/*
 * Replacing a field stage tile with a PNG (content-export E6b, the other half of stage_export.h).
 *
 * The tiles of AAA/DAT/Z_STAGE/S<nnn>PACK.BIN do not go through the file cache (the map streamer reads them with the CD reader into a
 * heap buffer), so the image layer's hook on the cache never sees them. The host copy of the stream task (pc/field/stream.c) asks
 * stageModTim for each tile it is about to put in VRAM, with the PACK's first sector, the tile number and the original TIM.
 *
 * Sources of tile <RR>_<CC> of pack s233pack (the stage s233), as for the other images (image_data.h, rules v1): the active mods from the
 * last to the first, mods/<mod>/stages/s233/tiles/<RR>_<CC>.png, then a PNG of <data>/stages/s233/tiles/ that the user edited (its hash differs
 * from the one <data>/stages/manifest.json holds, read once and again after stageModRecheck). The first one that passes the rules wins; a PNG that does not (size, colours, not an indexed PNG) is refused
 * with one log line (`images: <path> rejected: <why>`) and the next source is tried; with none the tile stays as on disc. A PNG with
 * fewer colours than the tile's CLUT only replaces the first ones; the rest of the palette stays as on disc.
 *
 * The result is kept per tile and looked at again at most once a second, so a modder can edit while the game runs. With no source
 * the game gets NULL and nothing changes.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct StageModIo {
    /* 1 when `lba` is the first sector of a stage PACK; name gets its lower-case key ("s233pack") */
    int (*pack)(void *ud, long lba, char *name, size_t cap);
    /* sector 0 of that PACK (2048 bytes); 1 when read */
    int (*header)(void *ud, long lba, uint8_t *sector);
    void *ud;
} StageModIo;

/* The data folder (<data>/stages) and how to find packs. Call again to change them; what was resolved is dropped. */
void stageModSetup(const char *dataDir, const StageModIo *io);

/* The substituted TIM of tile `frame` of the PACK at sector `lba`, whose original TIM is `tim`; NULL when the tile stays as on disc.
   The result stays valid until stageModSetup. */
const uint8_t *stageModTim(long lba, int frame, const uint8_t *tim);

/* Makes the next stageModTim look at the sources again (it does so by itself once a second; tests and tools use this) */
void stageModRecheck(void);

/* --validate-mod (U5): the stages/s<nnn>/tiles PNGs of one mod, checked as far as a file can be without the game running: tile names, an
   indexed PNG of at most 256 colors and, when the exported original is in <dataDir>/stages/s<nnn>/tiles, its size. Refusals are logged as
   `images: <path> rejected: <why>`. Returns how many PNGs were looked at. */
int stageModValidate(const char *modDir, const char *modName, const char *dataDir);

#endif
