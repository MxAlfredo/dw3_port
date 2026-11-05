#ifndef PC_GAME_TABLES_H
#define PC_GAME_TABLES_H

/*
 * The game's item data tables for the table engine (table_mods.h, ADR-007), content-export E5a:
 *
 *   weapon_data     WEAPON_DATA, the weapons' records (item ids 92..214)
 *   armor_data      ARMOR_DATA, the armour's (item ids 215..291)
 *   accessory_data  ACCESSORY_DATA, the accessories' (item ids 292..359)
 *   item_effects    the usable items' ITEM_EFFECT_* records (what a consumable does), one row per usable item
 *
 * All four live in the executable, which nothing resets, so they are registered and patched once at boot. A row is
 * keyed by the item id, or "<mod>:<name>" for an item a content mod adds (that item has its own copy of the record,
 * made from its base item's). The item_prices table is the one with the prices (it is defined in boot.c).
 */

#include "table_mods.h"

/*
 * content-export E5b: the evolutions and the growth of the partners.
 *
 *   evolutions    STFGTREP_evolutionLists, the 44 original entries of each partner's list (key partner * 100 + entry)
 *   blast_forms   DigimonData.blastForms, what a blast turns a partner form into (key: the Digimon id)
 *   level_exp, vital_cuts, vital_random, stat_gains, resist_gains   the report's growth tables (STFGTREP_*)
 *
 * blast_forms lives in the executable (patched once, with the rest of gameTablesRegister). The other six are the report
 * overlay's data, which the "stfgtrep" scope restores on every entry: gameTablesRegisterReport registers them and
 * gameTablesApplyReport (the scope's onEnter hook) patches them. Evolutions of mod partner forms ("evolution" of
 * digimon.json) are entries 44 and up of the port's own list and are never part of the table: see gameTablesApplyReport.
 */

/* The table descriptors (the engine keeps the pointer: they outlive it) */
const TmTable *gameTableWeapons(void);
const TmTable *gameTableArmor(void);
const TmTable *gameTableAccessories(void);
/* Built from ITEMS on every call (the usable items that point at a record of their own); call it again after ITEMS changes. */
const TmTable *gameTableItemEffects(void);

const TmTable *gameTableEvolutions(void);
const TmTable *gameTableBlastForms(void);
const TmTable *gameTableLevelExp(void);
const TmTable *gameTableVitalCuts(void);
const TmTable *gameTableVitalRandom(void);
const TmTable *gameTableStatGains(void);
const TmTable *gameTableResistGains(void);

/* Registers the four item tables and blast_forms and applies the mods' patches. Call once, before the first mode (the
   tables still hold the disc's values), after itemExtLoad, the Digimon load and tmInit. */
void gameTablesRegister(void);

/* Registers the report overlay's six tables (no patch goes on yet); returns how many registered. Call once, before the
   first entry to the report, while the overlay's data still holds the disc's values. */
int gameTablesRegisterReport(void);

/* Applies the six tables' patches: call it right after the "stfgtrep" scope restored the overlay's data, on every entry.
   The port reads a partner's evolutions from a list of its own when a mod added entries (ext_state.c: the 44 originals
   copied, then the mod's); extendedList(partner, NULL) must return that list or NULL, and when evolutions has a patch its
   first 44 entries are overwritten with the patched originals, so the table patches the originals and digimon.json
   owns entries 44 and up: they never overlap and nothing is applied twice. Without a patch nothing is written. */
void gameTablesApplyReport(void *(*extendedList)(int partner, void *original));

/*
 * content-export E5c: the field's battle data (FIELDSTG.PRO's tables).
 *
 *   field_enemies     FIELDSTG_enemies, the enemy rows the encounters point at (fighter, level, hp, mp, strength; key: the row)
 *   field_encounters  FIELDSTG_encounters, the battles the field can start (key: the encounter number). A row of the game holds
 *                     three pointers into field_enemies; the table shows them as the row numbers (enemy1..enemy3) and the port
 *                     keeps a plain copy of the rows, which it writes back as pointers after every patch
 *   battle_rates      FIELDSTG_battleRates, how many steps each rate index takes off the count to the next battle
 *
 * All three are the field overlay's data, restored by the "fieldstg" scope on every entry to the field, so gameTablesApplyField
 * (the scope's onEnter hook) patches them after each restore. The per-stage battle lists (stageBattles of each WSTAG overlay)
 * are not tables yet.
 */

/* The structure of an Encounter as game_tables.c reads it (boot.c asserts it matches the game's Encounter) */
typedef struct GameFieldEncounter {
    struct BattleEnemy *enemies[3];
    unsigned char ambushChance;
    unsigned char kind;
    unsigned char blocks[12];
} GameFieldEncounter;

const TmTable *gameTableFieldEnemies(void);
const TmTable *gameTableFieldEncounters(void); /* rebuilt from FIELDSTG_encounters on every call: call it while the game holds the disc's rows */
const TmTable *gameTableBattleRates(void);

/* The English name of a fighter id (a Digimon of the DIGNAM file) for the reference export; NULL: none. The text may be overwritten by
   the next call. Set it before gameTablesRegisterField; without it the reference has no labels. */
void gameTablesSetFighterNames(const char *(*nameOf)(int id));

/* Registers the three tables (the export is written; no patch goes on yet) and returns how many registered. Call once, before the first
   entry to the field, while the overlay's data still holds the disc's values, after the Digimon load (the enemy names resolve). */
int gameTablesRegisterField(void);

/* Applies the three tables' patches: call it right after the "fieldstg" scope restored the overlay's data, on every entry. Without a
   patch nothing is written. Enemy rows first, then the encounters (which are checked against them), then the rates. */
void gameTablesApplyField(void);

/*
 * content-export E5d: the card game's data (the card shop, the packs and the card battle).
 *
 *   card_prices      STCRDSHP_prices, what the card shop charges for the 44 cards it prices (key: the card id; a card with no row costs 1)
 *   card_shop_stock  STCRDSHP_stocks, the cards each of the 24 card shops sells (key: the shop number; a list of card ids)
 *   card_pack_slots  STCRDSHP_packs, the 16 cards each slot of a booster pack draws from (35 packs x 6 slots; key: pack item id * 10 + slot)
 *   card_effects     CARDGAME_cardEffects, the conditions, target and step script of the first 60 cards (key: the card id)
 *   prize_items      CARDGAME_prizeItems, the item an opponent gives for a win (key: CardOpponent.prize, 0-35)
 *   default_deck     CARDGAME_defaultDeck, the deck a player with an empty deck slot plays (key: the position, 0-39)
 *
 * The first three are STCRDSHP's data and the last three CARDGAME's: two overlays that their scopes restore on every entry, so the
 * hooks (gameTablesApplyCardShop / gameTablesApplyCardGame, the scopes' onEnter) patch them after each restore. The opponents' decks
 * (CARD_NPC.BIN) are a disc file and not a table yet.
 */

/* The structures of the game as game_tables.c reads them (boot.c asserts they match the game's CardPrice, CardShopStock, CardPack, CardEffect) */
typedef struct GameCardPrice {
    short card;
    short price;
} GameCardPrice;

typedef struct GameCardStock {
    int shop;
    int count;
    short *cards; /* 0-terminated */
} GameCardStock;

typedef struct GameCardPack {
    int item;
    int *slots[6]; /* each 16 card ids */
} GameCardPack;

typedef struct GameCardEffect {
    unsigned char condition, playCondition, message, target;
    unsigned char script[40];
} GameCardEffect;

const TmTable *gameTableCardPrices(void);
const TmTable *gameTableCardShopStock(void);
const TmTable *gameTableCardPackSlots(void); /* built from STCRDSHP_packs on every call: call it while the overlay holds the disc's values */
/* CardOpponent (battle.h): a row of CARD_NPC.BIN, 0xD0 bytes */
typedef struct GameCardOpponent {
    struct {
        short card;            /* the id in the low 12 bits (1-314), bit 15 a flag */
        unsigned char group, kind;
    } cards[40];
    short counterCards[20];
    unsigned char level, pad[3];
    int prize;
} GameCardOpponent;

const TmTable *gameTableCardEffects(void);
const TmTable *gameTablePrizeItems(void);
const TmTable *gameTableDefaultDeck(void);

/* The English name of a card id (CARDNM) and of an item id (ITMNAM) for the reference exports; NULL: none. The text may be overwritten by the
   next call. Set them before the registration; without them the references have no labels. */
void gameTablesSetCardNames(const char *(*nameOf)(int id));
void gameTablesSetItemNames(const char *(*nameOf)(int id));

/* Registers the card shop's three tables (the export is written, no patch goes on yet); returns how many registered. Call once, before the first
   entry to the shop, while the overlay's data still holds the disc's values. */
int gameTablesRegisterCardShop(void);
/* Applies the three tables' patches: call it right after the "stcrdshp" scope restored the overlay's data, on every entry. Without a patch
   nothing is written. */
void gameTablesApplyCardShop(void);
/* modding-ux U8a: card_opponents, the 170 rows of CARD_NPC.BIN (a disc file, not memory): register it with the file's disc bytes, and patch the buffer
   each time the game reads the file (the post-read hook calls gameTablesPatchCardOpponents). Returns 0 / -1, and the patch returns the rows changed. */
int gameTablesRegisterCardOpponents(const void *file, size_t len);
int gameTablesPatchCardOpponents(void *buf, size_t len);
/* The same for the card battle's three tables and the "cardgame" scope. */
int gameTablesRegisterCardGame(void);
void gameTablesApplyCardGame(void);

/*
 * content-export E5e: the gym (STGTRAIN.PRO's tables) and the inns.
 *
 *   gym_trainings       STGTRAIN_gymTrainings, the trainings each gym offers (14 x 16 rows; key: gym index * 100 + slot)
 *   gym_stat_gains      STGTRAIN_statGains, what a training adds to a battle stat (12 rows)
 *   gym_stat_losses     STGTRAIN_statLosses, what a resistance training may take off a battle stat (3 rows, one per intensity)
 *   gym_resist_gains    STGTRAIN_resistGainsLow/Mid/High, what a training adds to a resistance (3 tables x 9 rows; key: table * 10 + slot)
 *   gym_hp_mp_gains     STGTRAIN_maxHpMpGains, what a training adds to the maximum HP or MP (9 rows)
 *   gym_intensity_costs STGTRAIN_intensityCosts, the training points each intensity costs (3 rows)
 *   inns                INNS, the price and the name of each inn (21 rows; key: the game mode of the inn)
 *
 * The gym's six tables are the overlay's data (restored by the "stgtrain" scope on every entry), so gameTablesApplyGym (the scope's
 * onEnter hook) patches them after each restore. INNS belongs to the executable, which nothing resets: gameTablesRegisterInns patches it
 * once. The tables that pick the resistance table of a stat (STGTRAIN_resistGainTables, pointers) are not exposed.
 */

/* The structures of the game as game_tables.c reads them (INNS is defined in inn.c and the rest in the gym's files; no header declares them as
   these, and boot.c asserts the gym's match TrainEntry/TrainGain/TrainInfo) */
typedef struct GameTrainEntry {
    int id;
    short stat;
    short other;
} GameTrainEntry;

typedef struct GameTrainGain {
    int base;
    int range;
} GameTrainGain;

typedef struct GameTrainInfo {
    int name, desc;
    int icons[4];
} GameTrainInfo;

typedef struct GameInn {
    int mode;
    short string;
    short price;
} GameInn;

const TmTable *gameTableGymTrainings(void);
const TmTable *gameTableGymStatGains(void);
const TmTable *gameTableGymStatLosses(void);
const TmTable *gameTableGymResistGains(void);
const TmTable *gameTableGymHpMpGains(void);
const TmTable *gameTableGymIntensityCosts(void);
const TmTable *gameTableInns(void);

/* The English name of a training (an entry of TRAINI) and of an inn (an entry of HTLNAM) for the reference exports; NULL: none. The text may be
   overwritten by the next call. Set them before the registration; without them the references have no labels. */
void gameTablesSetTrainingNames(const char *(*nameOf)(int id));
void gameTablesSetInnNames(const char *(*nameOf)(int id));

/* Registers the gym's six tables (the export is written, no patch goes on yet); returns how many registered. Call once, before the first entry
   to the gym, while the overlay's data still holds the disc's values. */
int gameTablesRegisterGym(void);
/* Applies the six tables' patches: call it right after the "stgtrain" scope restored the overlay's data, on every entry. Without a patch nothing
   is written. */
void gameTablesApplyGym(void);
/* Registers INNS and patches it (once: the executable's data is never reset); returns 1 when registered. Call once at boot. */
int gameTablesRegisterInns(void);

/*
 * port-close S3: the event system's condition tables (src/engine/game/events.c), the numbers the stages' talks and events test.
 *
 *   special_conditions     SPECIAL_CONDITIONS, the rows `["special", ...]` names (key: the row id; a row is `primitive` (0 item set, 1 partner,
 *                          2 money, 3 progress range, 4 flag count, 5 actor icon), `op` and `arg`)
 *   money_required         MONEY_REQUIRED, what the "has money" rows ask for (key: the index, 0-9)
 *   money_gains            MONEY_GAINS, what the "give money" rows add (0-7)
 *   money_losses           MONEY_LOSSES, what the "take money" rows remove (0-9)
 *   progress_ranges        PROGRESS_RANGES, the story progress ranges the "in range" rows test (0-33; fields min and max)
 *   party_stat_thresholds  PARTY_STAT_THRESHOLDS, what `["party-stat", n, ...]` compares the party's stat total with (0-14)
 *
 * They are the executable's data, which nothing resets: gameTablesRegisterEvents patches them once at boot.
 *
 * A mod can also ADD rows to special_conditions: a row keyed by "<mod>:<name>" in its tables/special_conditions.json makes a new condition
 * that a stage patch names (`["special", "<mod>:<name>", 1]`, stage_codes.c). The rows live in the port (ids 0x100 and up, up to 256, kept
 * outside SPECIAL_CONDITIONS); events.c's checkSpecialCondition asks pcSpecialExtRow for those ids. A new row starts invalid (primitive 15):
 * it must give `primitive` and `op` (and `arg` where the primitive reads it) and pass the same check as the original rows, or the name is
 * not defined.
 */
const TmTable *gameTableSpecialConditions(void); /* its row count is that of SPECIAL_CONDITIONS when called */
const TmTable *gameTableMoneyRequired(void);
const TmTable *gameTableMoneyGains(void);
const TmTable *gameTableMoneyLosses(void);
const TmTable *gameTableProgressRanges(void);
const TmTable *gameTablePartyStatThresholds(void);

/* Registers the six tables and patches them (once: the executable's data is never reset); returns how many registered. Call once at boot,
   before the stage patches are loaded (their `special` names resolve to the rows this reads). */
int gameTablesRegisterEvents(void);

/* The id (0x100 and up) of a mod's special condition, or -1 when the name defines no valid row */
int gameSpecialIdOf(const char *name);
/* The name of a mod's special condition id; NULL for any other id */
const char *gameSpecialNameOf(int id);
/* The row of any special condition id (an original or a mod's): 1 when there is one */
int gameSpecialRow(int id, int *check, int *arg);
/* What events.c's checkSpecialCondition calls for the ids of mod rows (0x100 and up): 1 and the row when the id is a valid one */
int pcSpecialExtRow(int id, unsigned char *check, unsigned char *arg);

/* How many rows FIELDSTG_encounters has (the field battles a stage patch may name: rows 1 to this - 1) */
int gameTablesEncounterCount(void);

/* What an encounter may field (new-stages T5b, shim/field_ext.h's FeHost): the battle table id of "dw3:digimon/<id>" or "<mod>:<key>" (negative: not an
   enemy), and whether a number is a fighter an encounter may field (an original enemy or a mod's new one, never a partner form) */
int gameEnemyByName(const char *name);
int gameEnemyIdOk(int id);

#endif
