#ifndef PC_EXT_STATE_H
#define PC_EXT_STATE_H

/*
 * The port's extended game state (ADR-008, mechanism M2): the per-id arrays the original keeps
 * inside GAME (items, equippedItems) with room for ids beyond the original range.
 *
 *   ids 0 .. PC_ITEM_ORIG_COUNT-1   GAME.itemsCard / GAME.equippedItemsCard (the original arrays)
 *   ids PC_ITEM_ORIG_COUNT .. PC_EXT_ITEM_LIMIT-1
 *                                   the extension store: grows on demand, starts zeroed
 *   anything else (negative, >= limit)
 *                                   a scratch slot that always reads 0; a write is lost. Logged once on
 *                                   the [state] channel. Nothing ever writes past the original array.
 *
 * The code reaches these through PC_ITEM_COUNT / PC_ITEM_EQUIPPED (engine/item_access.h).
 */

/* the original arrays' length (GameState.items[0x193]) */
#define PC_ITEM_ORIG_COUNT 0x193

/* Safety limit, not a design cap: the item ids the game stores are s16, and a mod needs ~1000 at most
   (ADR-008 proposes +1024). 0x8000 ids cost 64 KiB for both stores, so it only stops a runaway id. */
#define PC_EXT_ITEM_LIMIT 0x8000

/* ---- techniques (extended techs T2) ----
   Technique n is TECHS[n - 1] (PC_TECH(n - 1)). Ids 1..PC_TECH_ORIG_COUNT are the original table; the ids above it
   (first PC_TECH_FIRST_EXT_ID) are the ones content mods add, kept in a store the registry (shim/tech_ext.c) hands
   over. A technique id is saved in 13 bits (PartnerEntry.skills & SKILL_ID), so the last usable id is
   PC_EXT_TECH_LAST_ID. */
#define PC_TECH_ORIG_COUNT 443
#define PC_TECH_FIRST_EXT_ID (PC_TECH_ORIG_COUNT + 1)
#define PC_EXT_TECH_LAST_ID 0x1FFF

/* The registry's table: rows[i] is technique id PC_TECH_FIRST_EXT_ID + i (index PC_TECH_ORIG_COUNT + i). The array
   stays owned by the caller; (NULL, 0) forgets it. */
struct TechData;
void pcExtStateSetTechStore(struct TechData *rows, int count);
/* The base technique id (1..PC_TECH_ORIG_COUNT) each store row was copied from ("based on"); same indexing as the
   store, caller-owned, (NULL, 0) forgets it. pcTechIsFieldHeal (engine/tech_access.h) reads it. */
void pcExtStateSetTechBases(const int *bases, int count);

/* The base item id (an original one, or another mod item) each new item was copied from ("based on"); row i is item
   id PC_ITEM_ORIG_COUNT + i. Caller-owned, (NULL, 0) forgets it. pcItemBase (engine/item_access.h) reads it. */
void pcExtStateSetItemBases(const int *bases, int count);

/* ---- Digimon rows (extended Digimon P1) ----
   PC_DIGIMON(row) (engine/digimon_access.h) reaches DIGIMON_DATA through pcDigimonRef: rows 0..54 are the original
   table, rows from 55 up a growable store, anything else a scratch row (reads zeros, writes dropped, one [state] line).
   A row is not a Digimon id; findDigimon maps an id to its row and also searches the extension rows.
   Row-index spaces: pairPartner and blastForms (DigimonData, the disc's layout) keep row + 1 in a u8, so they reach row 254 at
   most (U9 maps how to lift that); Evolution.digimon is an s32 and Evolution.needs[].digimon an s16, so the evolution lists
   already reach any row. PC_DIGIMON_ROW_LIMIT is the safety limit of the store itself. */
#define PC_DIGIMON_ROW_LIMIT 4096

/* Adds `count` zeroed rows after the current last one. Returns the index of the first added row (the current
   pcDigimonRowCount() when count <= 0), or -1 when the limit is passed or memory ran out (nothing is added).
   The store is one array: pointers from pcDigimonRef / GET_DIGIMON to extension rows are invalid after a call, so
   extend at load time, before the game holds any. */
int pcDigimonExtendRows(int count);

/* original rows + added rows (the number of rows PC_DIGIMON reaches) */
int pcDigimonRowCount(void);

/* forgets the added rows. pcExtStateReset does NOT: the rows belong to the mods loaded at boot (digimonExtReset), and loading
   a save calls pcExtStateReset */
void pcDigimonResetRows(void);

/* The DIGNAM entry (the string of the Digimon's name) of a Digimon row. DigimonData.nameId is a u8 and DIGNAM has 251 entries
   on the disc, so a new partner form (P2) cannot name itself through it: its row keeps its base's nameId (a harmless fallback
   for a reader that does not know better) and the real entry is kept here, beside the extension row. For any other
   DigimonData (an original row, a copy) it is d->nameId. Every place that shows a partner form's name goes through
   PC_DIGIMON_NAME (engine/digimon_access.h), which calls pcDigimonNameId (declared there; defined in ext_state.c). */
/* Sets the DIGNAM entry of extension row `row` (>= 55); other rows are ignored */
void pcDigimonSetNameId(int row, int nameId);

/* ---- row references wider than a byte (U9) ----
   DigimonData.pairPartner and blastForms are u8 in the disc's layout (row + 1, so row 254 at most). The port keeps them again in 16
   bits per row, beside the row: a row whose `set` is 1 reads these, any other row (a copy, a scratch row, a row nobody set) reads
   its own u8, so without mods the game reads exactly what the disc says. PC_DIGIMON_BLAST / PC_DIGIMON_PAIR (engine/digimon_access.h)
   reach them through pcDigimonBlast / pcDigimonPair. The first 12 bytes (id .. blast) are the "blast_forms" table's row. */
typedef struct PcDigiLinks {
    unsigned short id;       /* the row's Digimon id (the table key) */
    unsigned short blast[5]; /* blastForms: row + 1 of the form a blast turns the partner into, by tier */
    unsigned short pair;     /* pairPartner: row + 1 of its pair-technique partner, 0 for none */
    unsigned short set;      /* 1: blast and pair are the values to read */
} PcDigiLinks;

/* the links of row `row` (original rows 0..54 and the added rows), NULL outside the table */
PcDigiLinks *pcDigimonLinks(int row);
/* (re)builds the links of the original rows from DIGIMON_DATA_CARD's bytes and marks them set; call once the table is loaded */
void pcDigimonLinksInit(void);
/* Sets the links of added row `row` (>= 55) and marks it set; false when the row does not exist */
int pcDigimonSetLinks(int row, const unsigned short blast[5], unsigned short pair);

struct DigimonData;
/* the value the game reads: row + 1 of the blast form of `tier` (0..4) / of the pair partner of the row `d` points to */
int pcDigimonBlast(const struct DigimonData *d, int tier);
int pcDigimonPair(const struct DigimonData *d);
/* what findPairTech compares a pair partner with: row + 1 of `d` for an added row, d->nameId for an original one */
int pcDigimonRefOf(const struct DigimonData *d);

/* The Digimon id of the first new Digimon (new enemies and new partner forms share one id space: above every original id) */
#define PC_DIGIMON_FIRST_NEW_ID 471

/* the extension's current size in ids (PC_ITEM_ORIG_COUNT until something above it is touched) */
int pcExtItemCapacity(void);

/* forget the extension (new game, tests) */
void pcExtStateReset(void);

/*
 * Saving the extension (dw3save-v1, shim/save_file.h, section "EXTI"). The file never stores a numeric
 * id above the original range: it stores the item's NAME ("<mod>:<name>"), which the content registry
 * (T3) maps. Until it exists nothing names those ids, so there is nothing to write; the hooks are how
 * T3 plugs in (and how the tests do).
 *
 *   nameOf(id)   the stable name of an extension item, or NULL when the id has none
 *   idOf(name)   the id the current mods give that name, or -1 when no installed mod defines it
 */
typedef const char *(*PcExtItemNameFn)(int id);
typedef int (*PcExtItemIdFn)(const char *name);
void pcExtStateSetNameHooks(PcExtItemNameFn nameOf, PcExtItemIdFn idOf);

/* The extension as an "EXTI" chunk: returns its size, and writes it only when `cap` is enough. Ids with
   a count but no name are left out (logged on [saves]). */
unsigned long pcExtStateEncode(unsigned char *out, unsigned long cap);
/* Replaces the extension with a chunk's content (an empty/NULL chunk just clears it). Items whose name
   no installed mod defines are dropped with a log line. Returns the number dropped, or -1 for a damaged
   chunk (the extension is then empty). */
int pcExtStateDecode(const unsigned char *in, unsigned long len);

/*
 * Saving techniques (dw3save-v1 section "EXTT"). The game block keeps technique ids inside the partners' entries
 * (PartnerEntry.skills, 13-bit id plus flags); an id above the original range depends on the installed mods, so the
 * file also stores, for each such id found in the block, its NAME. Chunk: u32 version (1), u32 count, then per entry
 * u16 saved id, u16 name length, the name bytes (no NUL). Little endian.
 *
 *   nameOf(id)   the stable name of a new technique ("<mod>:<key>"), or NULL
 *   idOf(name)   the id the installed mods give it now, or -1
 */
typedef const char *(*PcExtTechNameFn)(int id);
typedef int (*PcExtTechIdFn)(const char *name);
void pcExtStateSetTechHooks(PcExtTechNameFn nameOf, PcExtTechIdFn idOf);

/* The names of the new techniques that `game` (a saved GameState block, `gameLen` bytes) holds: the chunk's size, and
   writes it only when `cap` is enough. An id with no name is not recorded (logged on [saves]). */
unsigned long pcExtStateEncodeTechs(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap);
/* Rewrites the new technique ids of a loaded block from the ids the chunk's names had when saved to the ids they have
   now (the chunk is NULL/empty for a save without any). A technique whose mod is not installed is removed from its
   skill slot (the slot reads empty), with a [saves] line. The block's checksum byte is recomputed when anything
   changed. Returns the number removed, or -1 when the chunk is damaged (then every new id in the block is removed). */
int pcExtStateRemapTechs(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len);

/*
 * Saving new partner forms (dw3save-v1 section "EXTD"). The game block keeps Digimon ids in the partners' entries
 * (PartnerEntry.id), their picked forms (PartnerStats.slots) and the battle form (Partner.battleDigivolve); an id from
 * PC_DIGIMON_FIRST_NEW_ID up depends on the installed mods, so the file also stores, for each such id found in the block,
 * its NAME and the original form it was based on. Chunk: u32 version (1), u32 count, then per entry u16 saved id,
 * u16 base id (an original Digimon id), u16 name length, the name bytes (no NUL). Little endian.
 *
 *   nameOf(id)   the stable name of a new partner form ("<mod>:<key>"), or NULL
 *   idOf(name)   the id the installed mods give it now, or -1
 *   baseOf(id)   the original Digimon id the form was based on, or -1
 */
typedef const char *(*PcExtDigNameFn)(int id);
typedef int (*PcExtDigIdFn)(const char *name);
typedef int (*PcExtDigBaseFn)(int id);
void pcExtStateSetDigimonHooks(PcExtDigNameFn nameOf, PcExtDigIdFn idOf, PcExtDigBaseFn baseOf);

/* The names of the new forms `game` holds: the chunk's size, written only when `cap` is enough. An id with no name is not
   recorded (logged on [saves]). */
unsigned long pcExtStateEncodeDigimon(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap);
/* Rewrites the new form ids of a loaded block from the ids their names had when saved to the ids they have now (the chunk is
   NULL/empty for a save without any). A form whose mod is not installed is dropped, by this rule: an entry that held it
   becomes the original form it was based on (keeping its level, experience and skills) unless the partner already has an
   entry of that form, in which case the entry is emptied; a picked form or battle form that named it becomes that original
   form when the partner has it, else nothing (0). With a damaged or missing chunk no base is known: entries are emptied
   and picks cleared. The checksum byte is recomputed when anything changed. Returns the number of forms dropped, or -1
   when the chunk is damaged. */
int pcExtStateRemapDigimon(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len);

/*
 * Partner entries beyond the original 44 (extended Digimon P3, ADR-008 M3). A partner's first PARTNER_ENTRY_COUNT entries stay
 * in the saved GAME block exactly as in the original; the rest are kept per partner here, up to PC_PARTNER_ENTRY_MAX entries in
 * all (engine/partner_access.h: PC_PARTNER_ENTRY, PC_PARTNER_ENTRY_LIMIT). pcExtStateReset (a save load, a new game) forgets them.
 *
 * Their dw3save section is "EXTP": it carries each stored entry with the NAMES of a new Digimon form or technique it holds (the
 * hooks of the EXTD and EXTT chunks), so it does not depend on the ids of the run that saved it. A save with no stored entry
 * writes no section. On load a form or technique whose mod is gone is dropped (the entry) or emptied (the skill slot), with a
 * [saves] line.
 */
/* the chunk's size, written only when `cap` is enough; 0 when no entry is stored */
unsigned long pcExtStateEncodeEntries(unsigned char *out, unsigned long cap);
/* replaces the stored entries by the chunk's (NULL/empty: none). Returns the entries dropped, or -1 when the chunk is damaged
   (then none are kept) */
int pcExtStateDecodeEntries(const unsigned char *in, unsigned long len);

/*
 * Evolution lists (extended Digimon P3). STFGTREP_learnDigimon reads a partner's list through PC_EVOLUTION_LIST (battle/report.h):
 * the original 44 entries and then the ones added here. pcEvolutionExtend appends `entry` to the partner's list (the original list is
 * copied on the first addition) and returns its index, or -1. pcEvolutionReset forgets every
 * addition (digimonExtReset: they belong to the mods loaded at boot, not to a save).
 */
#define PC_EVOLUTION_ORIG_COUNT 44
typedef struct PcEvolution { /* the layout of Evolution (battle/report.h), which a shim cannot include; boot.c asserts it */
    int digimon; /* DIGIMON_DATA row + 1 */
    struct {
        short digimon; /* row + 1, 0 for none */
        short level;
    } needs[2];
    short stat;
    short value;
} PcEvolution;
void *pcEvolutionList(int partner, void *original); /* the list STFGTREP_learnDigimon reads: the partner's extended list, or `original` */
void pcEvolutionSetOriginal(const void *lists); /* the original lists, PcEvolution [PARTNER_COUNT][44] (STFGTREP_evolutionLists) */
int pcEvolutionExtend(int partner, const PcEvolution *entry);
int pcEvolutionCount(int partner);
void pcEvolutionReset(void);

/*
 * Cards from content mods (U8b, ADR-009). Card ids 0 .. PC_CARD_ORIG_COUNT-1 (317) are GAME.cards / GAME.cardsSeen; the ids the
 * content registry gives new cards (shim/card_ext.c: from PC_CARD_ORIG_COUNT up, alphabetical order of "<mod>:<key>") keep their
 * owned count and seen flag in a store here, read through PC_CARD_COUNT / PC_CARD_SEEN (cardgame/card_access.h); an id outside
 * both is a scratch slot (reads 0, writes dropped, one [state] line). pcExtStateReset forgets the store.
 *
 * Their dw3save section is "EXTK": u32 version (1), u32 count, then per entry u16 saved id, u16 name length, the name bytes (no NUL),
 * s8 count, u8 seen. An entry is written for every new card with a count or a seen flag and for every new id in a saved deck, so a
 * deck of the game block can be remapped (pcExtStateRemapCards). Little endian. Nothing is written without a new card.
 */
typedef const char *(*PcExtCardNameFn)(int id);
typedef int (*PcExtCardIdFn)(const char *name);
void pcExtStateSetCardHooks(PcExtCardNameFn nameOf, PcExtCardIdFn idOf);
/* how many new cards the registry has (the album's size grows by this); 0 forgets */
void pcExtStateSetCardCount(int count);
/* the original starter deck (40 ids): the card a removed mod's card becomes in a loaded deck; NULL: card 1 */
void pcExtStateSetCardFallback(const int *deck);

/*
 * Field battle results (the `start-battle` action). The game keeps no record of how an event battle went: a won one goes through the battle report back to
 * the field, a fled one straight back, a lost one to the title screen. The port stores, per encounter, once the player is back on the field:
 *   dw3:battle-played/<key>   1 once the battle started by `start-battle` was fought (won or fled)
 *   dw3:battle-won/<key>      1 once it was won (a later flight does not clear it)
 * <key> is the mod's encounter name with ":" turned into "/" ("training-field/duel-trio") or the row of field_encounters in lower-case hex. Random area
 * battles record nothing. pcFieldBattleBegin is called when the battle starts and pcFieldBattleEnd(won) when the field comes back from it.
 */
void pcFieldBattleBegin(const char *key);
void pcFieldBattleEnd(int won);
/* the chunk's size (0: nothing to record), written only when `cap` is enough; `game` is the saved GameState block */
unsigned long pcExtStateEncodeCards(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap);
/* replaces the store by the chunk's content (NULL/empty: none); cards whose mod is gone are dropped with a [saves] line. Returns the
   number dropped, or -1 for a damaged chunk (the store is then empty) */
int pcExtStateDecodeCards(const unsigned char *in, unsigned long len);
/* rewrites the new card ids of the decks of a loaded block from the ids the chunk's names had when saved to the ids they have now; a
   card whose mod is not installed becomes the fallback card of its slot, with a [saves] line. Checksum recomputed when changed.
   Returns the number replaced, or -1 when the chunk is damaged (then every new id in a deck is replaced) */
int pcExtStateRemapCards(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len);

/*
 * Named variables of content mods (port-close S5): the flags and counters a stage patch's conditions and actions use
 * ("mod-flag", "mod-var", "set-var", shim/stage_mods.h). A variable is an integer kept by NAME ("<mod>:<name>"), 0 until
 * something sets it; a flag is a variable that holds 0 or 1. The names are the save's key, so nothing depends on which mods are
 * installed or in which order: a name no installed mod uses is simply never read.
 *
 * Their dw3save section is "EXTV": u32 version (1), u32 count, then per entry u16 name length, the name bytes (no NUL), s32
 * value. Little endian. Only non-zero values are written (zero is the same as never set), and nothing is written when there are
 * none. On a load EVERY name in the chunk is kept, installed or not: disabling a mod and enabling it again keeps its progress.
 * pcExtStateReset (a save load) and a new game (newGame calls pcExtVarsReset itself) forget them.
 */
#define PC_EXT_VAR_MAX 4096     /* names one save holds; the rest are refused (a safety limit, not a design cap) */
#define PC_EXT_VAR_NAME_MAX 120 /* bytes of a name */
/* the value of `name`, 0 when it was never set */
int pcExtVarGet(const char *name);
/* sets `name` (any value; 0 is the same as unset). 0, or -1 when the name is empty/too long or the store is full (logged on [state]) */
int pcExtVarSet(const char *name, int value);
/* how many names hold a non-zero value, and the i-th of them (insertion order); NULL when out of range */
int pcExtVarCount(void);
const char *pcExtVarNameAt(int i);
void pcExtVarsReset(void);

/*
 * Card battle results (new-stages fix 2026-11-04). The game's own record of a card battle is two TEMPORARY flags, 00:11 (one just ended) and 00:10 (the
 * player won it), that it clears when the next field stage starts (events.c updateModeFlags, GameState.clearTempFlags), so a talk has to read them before
 * the player leaves the stage. The port also keeps one named variable pair per opponent, set the moment the battle ends and kept in the save (EXTV):
 *   dw3:card-played/<opponent>   1 once the player has fought that opponent
 *   dw3:card-won/<opponent>      1 once the player has won it (a later loss does not clear it)
 * <opponent> is the opponent of the `card-battle` action in lower-case hex without leading zeros ("13" for "13:0"). A mod reads them with `mod-flag`.
 * pcCardBattleBegin is called when a field action starts the battle (the opponent is not known afterwards: the mode has changed) and pcCardBattleEnd when
 * the field comes back from the card game, with whether the player won. An end without a begin (a harness that opens the card mode directly) records nothing.
 */
void pcCardBattleBegin(int opponent);
void pcCardBattleEnd(int won);
/* the chunk's size (0: nothing to record), written only when `cap` is enough */
unsigned long pcExtStateEncodeVars(unsigned char *out, unsigned long cap);
/* replaces the store by the chunk's content (NULL/empty: none), unknown names included. Returns the number of entries kept, or -1 for
   a damaged chunk (the store is then empty) */
int pcExtStateDecodeVars(const unsigned char *in, unsigned long len);

#endif
