#ifndef PC_FIELD_EXT_H
#define PC_FIELD_EXT_H

/*
 * New field encounters of the mods (new-stages T5b, ADR-012 question 6). The game's FIELDSTG_encounters (335 rows) and STFGTREP_rewards
 * (one row per encounter number) are arrays of two overlays, restored from the disc on every entry to their mode and sized to the disc, so a
 * mod's own battle cannot be a row of them. A mod adds rows to the port's own store instead: in mods/<mod>/tables/field_encounters.json
 * (format "field_encounters-v1") a row of "rows" whose key is a name "<mod>:<name>" (the mod's own folder, a colon and a key of a-z 0-9 - _)
 * defines a NEW encounter:
 *
 *   "rows": { "<mod>:<name>": {
 *       "enemies": [ {"enemy": <id | "dw3:digimon/<id>" | "<mod>:<key>">, "level": 1-99, "hp": 1-32767, "mp": 0-32767, "strength": 1-32767}, ... ],  (1 to 3; the first is a fighter)
 *       "ambushChance": 0-255, "kind": 0-255,                                     (optional; 0 and 1)
 *       "poison", "paralysis", "confusion", "sleep", "knockOut", "drain", "block6", "steal",
 *       "lowerAttack", "lowerDefense", "lowerSpeed", "runAway": 0 or 1,            (optional; 0)
 *       "rewards": {"digimonExp": 0-9999999, "exp": 0-999999, "money": 0-9999999}  (optional; 0) } }
 *
 * The rows get the numbers firstRow, firstRow + 1, ... in the order of the mods and of the file; stage battles, the "start-battle" action and the
 * tables name them by name or by that number. A bad row is logged (`tables: <path> row "<name>" rejected: <why>`) and does not exist; the
 * rest of the file stands. Nothing is ever written to the original rows. Portable C, no game header: the host (boot.c) turns a FeRow into the
 * game's Encounter and BattleReward.
 */

#define FE_MAX_ROWS 1024 /* the number of new rows all the mods may add (stage battles and the start-battle action take numbers up to 4095) */

typedef struct FeEnemy {
    int fighter;                       /* a battle table id */
    int level, hp, mp, strength;
} FeEnemy;

typedef struct FeRow {
    char name[96];                     /* "<mod>:<name>" */
    int mod;                           /* the index of the mod that defined it (modsNameOf) */
    FeEnemy enemy[3];                  /* fighter 0: the slot is empty */
    int ambushChance, kind;
    unsigned char blocks[12];          /* BATTLE_BLOCK_*, in the order of the table's fields */
    int digimonExp, exp, money;        /* the row of the rewards table this encounter reads */
} FeRow;

typedef struct FeHost {
    int firstRow;                                  /* the number of the first new row: the count of the original encounters */
    int (*enemyByName)(const char *name);          /* the fighter id of "dw3:digimon/<id>" or "<mod>:<key>"; negative: not an enemy */
    int (*enemyIdOk)(int id);                      /* whether a number is a fighter an encounter may field (not a partner form) */
} FeHost;

/* Forgets the rows and reads the active mods (modsInit first; the Digimon content loaded first, because the enemies are named). Returns how many rows stand. */
int fieldExtLoad(const FeHost *host);
void fieldExtReset(void);

int fieldExtCount(void);
int fieldExtFirstRow(void);
const FeRow *fieldExtRow(int i);              /* i = number - firstRow; NULL out of range */
int fieldExtNumberOf(const char *name);       /* the encounter number of a name; -1 when no row has it */
const char *fieldExtNameOf(int number);       /* the name of a number of a new row; NULL otherwise */

/* The names of the blocks, the order of FeRow.blocks (also the table's field names): "poison", ... "runAway" */
const char *fieldExtBlockName(int i);

#endif
