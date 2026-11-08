#ifndef PC_CARD_EXT_H
#define PC_CARD_EXT_H

/*
 * New cards from content mods (U8b, ADR-009; one folder per card since A8.1, ADR-016). A mod of kind "content" declares a card as the folder
 * mods/<mod>/cards/<key>/, holding card.json (format "cards-card-v1") and, optionally, face.png:
 *
 *   { "format": "cards-card-v1", "name": "test-card",
 *     "base": 120, "ap": 30, "hp": 25, "price": 120, "kind": 16, "color": 2, "points": 1, "level": 3, "rank": 150, "comboCard": 0,
 *     "effect": { "like": 12, "message": 5, "script": [3, 7] },
 *     "title": { "eng": "...", "spa": "..." }, "description": { "eng": "..." } }
 *
 * "name" is optional and, when there, is the folder's name (the ADR-016 header); "title" is the card's name per language. A folder without a
 * card.json is no card. The same file name, cards-card-v1, is what the exporter writes for an original card (card_export.c): that one is
 * descriptive and is never read. content/cards.json is not read any more.
 *
 * The card is "<mod folder>:<key>". Its art is face.png beside its card.json (indexed, 32x32, at most 256 colours); without it the card
 * wears its base's art. A folder named "c" and three digits is an original card's (cards/c<nnn>/face.png replaces card <nnn>'s art, image_data.c): it
 * is never a new card, its card.json is not read and the loader skips it (cardExtKeyIsOriginal). Every card of every active content mod gets an id
 * from PC_CARD_ORIG_COUNT (317) up, in the alphabetical order of its full name; ids are never saved (the save stores the name, ext_state.h "EXTK").
 *
 * Its image record (CardImage: the stats and the TIM) is a copy of the base card's with the overrides; the game reaches it through
 * cardDrawerSetCard (pcCardImage) and its effect through CARDGAME_getEffectField (pcCardEffect). The names and descriptions are extra
 * entries of CARDNM and CARDST (text_data.h, tdSetCardTextHook). The owned counts and seen flags are PC_CARD_COUNT / PC_CARD_SEEN
 * (cardgame/card_access.h, ext_state.h). The mechanism (reading, ids, texts, names) is the shared core, content.h.
 *
 * "price" (0..32767, default 1) is what the card shop charges (STCRDSHP_getPrice -> pcCardPrice). A mod sells the card, puts it in packs, an
 * opponent's deck or the default deck with its tables, naming it "<mod>:<key>" (game_tables.c: card_shop_stock, card_pack_slots, card_opponents,
 * default_deck).
 *
 * A mod can name an original card as "dw3:card/<id>" wherever it gives a card id (base, "like").
 */

#include <stddef.h>
#include <stdint.h>

#include "content.h"

/* The 0x62C bytes of an original card's image record (12-byte header and its 32x32 8-bit TIM) for card `id` (1..316), and the 44 bytes
   of its effect (CardEffect) when it has one (cards 1..60). Set by the host (boot.c reads CARDPAK from the disc, tests give fixtures);
   without them no card can be declared. Return 1 when `out` was filled. */
typedef struct CardExtBase {
    int (*image)(int id, unsigned char *out);
    int (*effect)(int id, unsigned char *out);
} CardExtBase;
void cardExtSetBase(const CardExtBase *base);

#define CARD_EXT_IMAGE_SIZE 0x62C
#define CARD_EXT_EFFECT_SIZE 44

/* Reads the active content mods' cards/<key>/card.json, assigns ids, hooks names, texts and the count. Returns the number of cards. */
int cardExtLoad(void);
void cardExtReset(void);

const ContentType *cardExtType(void);

int cardExtCount(void);
/* 1 when a card key is "c" and three digits: the folder name of an original card, never the key of a new one (its art is read by image_data.c) */
int cardExtKeyIsOriginal(const char *key);
int cardExtIdOf(const char *fullName);     /* the id, or -1 */
const char *cardExtNameOf(int id);         /* "<mod>:<key>", or NULL */
const char *cardExtText(int index, const char *langDir, int desc);
/* Table hooks: a name in a list of card ids ("<mod>:<key>" or "dw3:card/<id>"), and whether an id may be listed */
long long cardExtByName(const char *name);
int cardExtIdOk(long long id);
/* The image bytes and effect bytes of new card `id` (tests), or NULL */
const unsigned char *cardExtImageBytes(int id);
const unsigned char *cardExtEffectBytes(int id);

#endif
