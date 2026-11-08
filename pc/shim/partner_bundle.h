#ifndef PC_PARTNER_BUNDLE_H
#define PC_PARTNER_BUNDLE_H

/*
 * Importing the partner bundle (readable-assets A4a, ADR-016 section 6): partners/<key>/ holds a partner's 2D art, and the port reads it back through the
 * asset layers of mods.h (the mods from the last to the first, then data/): `portrait.png`, `icon.png` and `field/` (sprite-v1). A file that is missing falls
 * back to the base asset (ADR-016 section 3).
 *
 *   A partner a mod adds ("<mod>:<key>" of partners/<key>/partner.json) finds its bundle by <key>: partner_ext.c hands portrait.png and icon.png to the VRAM band
 *   (partner_art_vram.c) and names field/ as its follower character (stage_mods.c registers every partners/<key>/field/ of the mods as the sprite "partner:<key>").
 *
 *   An original partner is edited by replacing a file of data/partners/<name>/ (a file the user changed: one whose hash differs from partners/manifest.json) or
 *   by a mod's partners/<name>/. <name> is the slug of its English name (DIGNAM[p + 1], partner_export.h). At startup, once, the art is converted (more than 16
 *   colours are merged, with a warning) and kept; while the overrides of each of the seven screens' page files are built (image_data.c, the hook below) the
 *   pixels of its cells and the palette rows are rewritten in the page images, so the starter team page, the name page, the lab, the field menu, the gym, the
 *   battle report and the memory card all show the same edited art: the cells go where the sheet puts frame `first + cell` and the palette in every row the
 *   frames name (the same palette in all seven sheets). The follower sprite of an original is read from partners/<name>/field/ by the image layer's hook.
 */

#include <stddef.h>
#include <stdint.h>

#include "partner_art.h"

/* The English-name folders of the eight originals ("agumon"...). Set by pbStart from the disc, or by a test; NULL while unknown. */
void pbSetKeys(char key[8][64]);
const char *pbOriginalKey(int partner);

/* The pixels of the portrait cells and icons of the partners with an `art` (NULL entry: unchanged) written into one page image of a screen's page file:
   `tim` is the unpacked TIM of page `page` (chain index of the image in the file), `sheet` the screen's sprite sheet from the entry the screen uses
   (peScreenOfPages). Rewrites the 4-bit pixels of every cell whose frame lives in this page and, in page 0, the 16 palette halfwords of every frame of the
   partner. Returns how many cells and palettes it wrote (0: nothing to do here). Pure: no globals, no disc. */
typedef struct PbArt {
    const PaArt *portrait; /* PA_CELL x PA_CELL cells of the strip, exactly the original's count (peLayout) */
    const PaArt *icon;     /* 44x28 */
} PbArt;
int pbRepackPage(uint8_t *tim, size_t len, int page, const uint8_t *sheet, size_t sheetLen, const PbArt art[8]);

/* Whether a file of data/partners/<rel> (rel: "<key>/training/<name>/003.png") is the user's: its hash is not the one partners/manifest.json recorded, or the
   manifest does not know it (partner_train.c uses it for the training folders of the originals). Needs the data folder set (assetSetData). */
int pbDataFileEdited(const char *rel, const uint8_t *bytes, size_t len);
/* 1 when data/partners/<key>/ has a file the user changed (pbDataFileEdited over the whole folder; its partner.json is information only and does not count): the folder is
   then a layer of that partner's art like a mod's (--validate-mod warns when a mod shares it) */
int pbDataFolderEdited(const char *key);

/* Reads and converts the edited originals' art (portrait.png, icon.png) through the asset layers and hooks the image layer (imgSetPartnerHooks): the page
   patch above and the follower sprites of partners/<name>/field/. Needs the disc open (port_fs) for the names. Returns how many originals have an edited
   portrait or icon. Safe to call again (a reload). */
int pbStart(void);
/* Forgets everything pbStart read and unhooks (tests) */
void pbReset(void);

#endif
