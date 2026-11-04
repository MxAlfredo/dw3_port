#ifndef PC_ITEM_EXT_H
#define PC_ITEM_EXT_H

/*
 * New items from content mods (extended-state T3, ADR-007 section 4, ADR-008).
 *
 * A mod of kind "content" declares items in mods/<mod>/content/items.json (format "items-v1"):
 *
 *   { "format": "items-v1",
 *     "new": { "test-gadget": { "base": 43, "price": 50, "sellPrice": 20,
 *                               "effect": { "amount": 300 },
 *                               "name": { "eng": "...", "spa": "..." },
 *                               "description": { "eng": "..." } } } }
 *
 * The item is "<mod folder>:<key>". Every declared item of every active content mod gets a numeric id from
 * PC_ITEM_ORIG_COUNT up, in alphabetical order of that full name, so the same set of mods always gives the same
 * ids; the ids are never saved (the save stores the name, ext_state.h). Its ItemInfo is a copy of the base item's
 * with the overrides; the game reaches it through ITEM_FUNCS, which this unit redirects only when at least one
 * item exists (no mods: the game is untouched). Names and descriptions are served by the text layer
 * (text_data.h, tdSetItemTextHook) as extra entries of the item name/description files.
 *
 * The mechanism (reading, ids, store, texts, names) is the shared core, content.h; this file is the item type: its fields,
 * its record and the redirect of ITEM_FUNCS. A mod can also name an original item as "dw3:item/<id>" wherever it gives
 * an id (base, the shops' and loot lists, item_prices).
 */

#include <stddef.h>
#include <stdint.h>

#include "content.h"
#include "engine/game_state.h"

/* Reads the active content mods' items.json, assigns ids, redirects ITEM_FUNCS and hooks the names (ext_state)
   and the text. Call after modsInit, before the first mode. Returns the number of items. */
int itemExtLoad(void);
/* Forgets everything and puts ITEM_FUNCS back (tests) */
void itemExtReset(void);

/* The item type of the content core (its reference export: contentReferenceText) */
const ContentType *itemExtType(void);

int itemExtCount(void);
int itemExtIdOf(const char *fullName);        /* the id, or -1 */
const char *itemExtNameOf(int id);            /* "<mod>:<key>", or NULL when no item has that id */
const ItemInfo *itemExtInfo(int id);          /* or NULL */
/* The text of item `index` (0 .. count-1) in a language folder name ("eng", "spa", "es-419"...): desc 0 the name, 1 the
   description. Falls back es-419 -> spa -> eng. Never NULL for a valid index. */
const char *itemExtText(int index, const char *langDir, int desc);

/* Table hooks (TmField.byName / idOk): a name in a list of ids ("<mod>:<key>" or "dw3:item/<id>"), and whether an id
   may be listed */
long long itemExtByName(const char *name);
int itemExtIdOk(long long id);
/* The mutable ItemInfo record of a new item (TmTable.extRow of item_prices), or NULL */
void *itemExtRow(long long id);
/* The own equipment record or effect of a new item of that kind (a copy of its base item's, TmTable.extRow of weapon_data,
   armor_data, accessory_data and item_effects), or NULL when the item is not one of that kind */
void *itemExtWeaponRow(long long id);
void *itemExtArmorRow(long long id);
void *itemExtAccessoryRow(long long id);
void *itemExtEffectRow(long long id);

#endif
