#ifndef PC_PARTNER_EXT_H
#define PC_PARTNER_EXT_H

/*
 * New partners and starter teams from content mods (Betamon pack P4, ADR-013, ADR-014, ADR-015).
 *
 * A mod of kind "content" declares a partner as the folder mods/<mod>/partners/<key>/, whose partner.json (format "partners-partner-v1", ADR-016) is the
 * registry record, next to the art the folder already holds (portrait.png, icon.png, field/, training/); teams stay in mods/<mod>/content/teams.json
 * (format "teams-v1"):
 *
 *   partners/betamon/partner.json:
 *   { "format": "partners-partner-v1", "name": "betamon",
 *     "base": "dw3:partner/3",
 *     "firstForm": "betamon-mod:betamon",
 *     "title": { "eng": "Betamon", "es-419": "Betamon" },
 *     "description": { "eng": "A river digimon.\nStrong against fire." },
 *     "equip": { "like": "dw3:partner/3", "allow": ["dw3:item/120"], "deny": ["dw3:item/100"] },
 *     "equipSet": { "items": ["dw3:item/243", "dw3:item/269", "dw3:item/98", "dw3:item/284"],
 *                   "bonuses": [4, 4, 4, 4, 4, 20] },
 *     "evolution": [ { "digimon": "betamon-mod:seadramon",
 *                      "needs": [ { "digimon": "betamon-mod:betamon", "level": 20 } ],
 *                      "stat": "level", "value": 30 } ] }
 *
 *   content/teams.json:
 *   { "format": "teams-v1",
 *     "new": { "river": { "partners": ["dw3:partner/0", "betamon-mod:betamon", "dw3:partner/7"],
 *                         "name": { "eng": "River pack" }, "description": { "eng": "..." } } } }
 *
 * "name" is optional and, when there, is the folder's name (the ADR-016 header, as cards do since A8.1); "title" is the partner's name per language;
 * "$schema" and "notes" are ignored. A folder without a partner.json is no partner (it may be only art). The same file name and format,
 * partners-partner-v1, is what the exporter writes in partners/<name>/ of the data folder for an original partner (partner_export.c): that one is descriptive
 * and is never read, and a mod folder named like an original partner (the slug of its English name, pbOriginalKey) only replaces art, so the loader skips it
 * (isOriginalKey). content/partners.json is not read any more, and no file ever patched an original partner: "base" names one only for art and equipment.
 *
 * The partner is "<mod folder>:<key>". Its index in the game (the partner number PC_PARTNER takes) is 8 plus its place in the
 * alphabetical order of every declared partner's full name, so the same mods always give the same indices; an index is never saved
 * (the save keeps names, ADR-014). The registry (pcPartnerRegister, ext_state.c) is filled here, before any save is read.
 *
 *   base       an original partner (0..7, or "dw3:partner/<n>"): its 2D art stands in for the new partner until it has its own
 *              (P6/P7), and its bit of every item's `partners` mask is the partner's equipment eligibility unless "equip" says more
 *   (art, icon, field sprite)  not fields: a partner's own 2D art is its bundle partners/<key>/ (<key> is the part of the name after the colon) in the
 *              mod, through the asset layers (mods.h, ADR-016 section 6, partner_bundle.h): portrait.png (32x32 cells side by side, at most 16 colours, index 0
 *              transparent; more colours are reduced with a warning), icon.png (44x28, likewise) and field/ (sprite-v1: its character when it follows the
 *              player, sets 1, 5, 6; docs/formats.md 4.12). A file that is missing falls back to the base's
 *   firstForm  the Digimon form the partner starts as: an original partner form ("dw3:digimon/<id>") or a new one ("<mod>:<key>" of
 *              digimon/<key>/digimon.json). Its stats, growth and name come from that form's row; the partner has no entry for it until a battle report
 *              (the game's partners work the same way: the first form is the base row, the evolution list starts after it)
 *   name       the partner's name, per language (eng obligatory): what the starter screen shows and the name the new game gives the partner
 *   description  the starter screen's text for the partner (a few lines); optional
 *   equip      like (an original partner whose item mask it reads, default base), allow / deny (items it can or cannot wear whatever the mask
 *              says; deny wins)
 *   equipSet   four items worn together (their ids, as EQUIP_SETS) and the six stat bonuses wearing them adds (as EQUIP_SET_BONUSES)
 *   evolution  the forms it can learn after a battle: the Evolution entries of its list (include/battle/report.h) with the Digimon by name:
 *              digimon (the form it learns), needs (up to 2 of {digimon, level}), stat (1..14 or a stat name) and value
 *
 * A team is "<mod folder>:<key>" too; its number (what GAME.partySet keeps in memory and `DW3_STARTER_SET` takes, by number or by name) is 3 plus its place in the alphabetical
 * order. "partners" are exactly three partners by name (originals allowed, none twice); a team with a name no mod defines is rejected whole.
 * Teams may leave "base" out; with an original team ("dw3:team/<0..2>") as base its members are the defaults. The ids of the partners come out
 * of the registry, so the teams load after the partners.
 *
 * Texts (name, description) are extra entries of the registration file NAMEET (TEXT_ONLINE, text_data.h tdSetPartnerTextHook): partner k (0 for the
 * first new one) has entries 31 + 2k and 32 + 2k, team j entries 31 + 2P + 2j and 32 + 2P + 2j, P being the number of new partners; after them
 * team j's label on the starter page's left column (D, E, ... AA) is entry 31 + 2P + 2T + j, T being the number of new teams.
 *
 * The mechanism (reading, validating, ids, texts) is the shared core, content.h.
 */

#include "content.h"

/* What the registry needs from the rest of the port, set by the host (boot.c) before partnerExtLoad: tests give stubs. */
typedef struct PartnerExtHooks {
    long long (*formRef)(const char *name); /* DIGIMON_DATA row + 1 of a partner form by name ("dw3:digimon/<id>", "<mod>:<key>"), -1 for none */
    long long (*itemId)(const char *name);  /* the id of an item by name ("dw3:item/<id>", "<mod>:<key>"), -1 for none */
    int (*artLoad)(int partner, const char *file, const char *name); /* reads the PNG of a partner's own art (partner_art.h); 0 on success, NULL: art is ignored */
    void (*artReset)(void); /* forgets every art (a reload) */
    int (*iconLoad)(int partner, const char *file, const char *name); /* reads the PNG of its memory card icon (44x28); 0 on success, NULL: icons are ignored */
    int (*trainLoad)(int partner, const char *key); /* reads partners/<key>/training/ of the partner (A4b, partner_train.h); NULL: the gym shows the base's */
    int (*isOriginalKey)(const char *key); /* 1 when partners/<key>/ is an original partner's folder (pbOriginalKey): not a new partner; NULL: none is */
} PartnerExtHooks;
void partnerExtSetHooks(const PartnerExtHooks *hooks);

/* Reads the active content mods' partners/<key>/partner.json and content/teams.json, registers the partners, fills the starter team table and hooks the texts.
   Call after digimonExtLoad (the forms exist) and itemExtLoad, before any save is read. Returns the number of new partners. */
int partnerExtLoad(void);
/* Back to the original eight partners and three teams (tests, a reload) */
void partnerExtReset(void);

const ContentType *partnerExtType(void);
const ContentType *teamExtType(void);

int partnerExtCount(void); /* new partners */
int teamExtCount(void);    /* new teams */

#endif
