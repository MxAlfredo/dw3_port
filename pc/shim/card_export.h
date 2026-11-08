#ifndef PC_CARD_EXPORT_H
#define PC_CARD_EXPORT_H

/*
 * The original cards, one folder each (readable-assets A7.4, ADR-016): the face of every card of CARDPAK0-4.
 *
 *   <data>/cards/c<nnn>/face.png    the card's 32x32 picture, an indexed PNG (256 colours, index 0 transparent) in the disc's own index order
 *   <data>/cards/c<nnn>/card.json   "cards-card-v1": the card's number, its disc identity and where its picture is on the disc (information only)
 *   <data>/cards/manifest.json      asset-manifest-v1 for the topic; `names` maps each folder to its disc identity ("c012": "dw3:card/12")
 *
 * <nnn> is the game's own card number: the card drawer (src/engine/gfx/card_drawer.c, cardDrawerSetCard) takes an id, loads file CARD_IMAGE_FILES[(id - 1) >> 6]
 * and picks image (id - 1) & 63 of it, so image `i` of CARDPAK<A> is card A * 64 + i + 1 (imgCardId, image_data.h). The numbers are the same ones `dw3:card/<id>`
 * and a card's folder name in mods use (ADR-009). The PNG is the one the images exporter wrote to images/card/cardpak<A>/<ii>.png before A7.4, byte for byte.
 *
 * A mod's mods/<mod>/cards/c<nnn>/face.png, or an edited data/cards/c<nnn>/face.png, replaces that card's picture through the override table of image_data.c
 * (imgOverridesBuild reads the cards topic for a CARDPAK file). A folder of a mod that is not c<nnn> is a new card of that mod (cards/<key>/card.json, card_ext.h); the card.json written here is descriptive and never read.
 *
 * The disc side lists the disc through the same file list and sector reader as menu_export.h and writes through exp_core.h. Bump CARD_EXPORT_VERSION when the bytes
 * or the layout change. It is covered by pc/cardexptest.c.
 */

#include <stddef.h>
#include <stdint.h>

#include "menu_export.h"

#define CARD_EXPORT_VERSION 1

typedef struct CardExportStats {
    int cards;   /* cards with a face */
    int skipped; /* 1 when everything was up to date and the disc was not read */
    int written, kept, newFiles, failed;
    long bytes;
} CardExportStats;

void cardExportSetup(const char *dataDir, const MenuExportIo *io);
/* "all", or card numbers (12, c012) separated by commas or spaces. Synchronous. The number of cards written or checked, 0 when up to date, or -1 when there is
   nothing to do (no data folder, no reader, nothing valid). */
int cardExportRun(const char *spec);
void cardExportTotals(CardExportStats *out);

#endif
