#ifndef PC_DIGIMON_EXT_H
#define PC_DIGIMON_EXT_H

/*
 * Changes to existing Digimon from content mods (Digimon slice 1, ADR-009 sections 4, 5 and 7).
 *
 * A fighter is one entity with facets. Its name is "dw3:digimon/<id>", where <id> is the fighter id: a partner form is a
 * DigimonData id (DIGIMON_DATA, in the executable) and an enemy is a battle table id (FILE_BATTLE_TABLE). The two id spaces
 * are disjoint (248 fighters: 55 partner forms, 3 of them empty placeholders, and 193 enemies), so the id tells the facet.
 *
 * A mod of kind "content" keeps a Digimon in the folder mods/<mod>/digimon/<name>/ (ADR-016). The folder d<nnn> (the fighter id on three digits, the
 * name the data folder gives the original, d383 for Kotemon) changes original <nnn> with its digimon.json (format "digimon-digimon-v1"):
 *
 *   digimon/d383/digimon.json:
 *   { "format": "digimon-digimon-v1", "name": "d383",
 *     "partner": { "hp": 60, "stats": { "strength": 90 }, "skills": { "6": { "tech": "mymod:spark", "level": 12 } } } }
 *   digimon/d004/digimon.json:
 *   { "format": "digimon-digimon-v1", "name": "d004",
 *     "enemy":   { "stats": { "defense": 120 }, "item": "dw3:item/43", "itemChance": 1023 } }
 *
 * "name" is optional and, when there, is the folder's name; "$schema" and "notes" are ignored. A change only touches existing Digimon: no new ids, so a
 * save is not affected (nothing saved holds a value the mod made up: techniques of mods keep their own by-name chunk, ext_state.h). Nothing else of an
 * original can be replaced: its model/ in that folder is never read. Any other folder name is a new Digimon (below): its digimon.json is the record
 * ("new" of the old content/digimon.json, with "title" for the name per language) and its model/ the model it uses when the record has no "model".
 * content/digimon.json and content/models/ are not read any more; the placements ("encounters") are content/encounters.json (format "encounters-v1").
 * A new Digimon:
 * an enemy (base = an original enemy: a battle table row) or a partner form (base = an original partner form: a DigimonData row in
 * the port's store, P2). Both share one id space (471 + the position in the alphabetical list of all new Digimon) and one name
 * space (DIGNAM entry 251 + position); a new partner form's id can be saved, so it has a by-name chunk ("EXTD", ext_state.h).
 *
 * partner (a DIGIMON_DATA row): hp, mp, hpGrowth, mpGrowth, expRate, expLevel (0..255), family (a value some original
 *   Digimon has: it indexes a game table), stats {strength, defense, spirit, wisdom, speed, charisma} (0..999),
 *   resistances {"0".."6"} (0..999, the seven elements in game order), statusResists {poison, paralysis, confusion, sleep,
 *   knockOut} (0..255) and skills {"1".."6": {tech, level}}: the technique (a number, "dw3:tech/<id>" or "<mod>:<key>"; 0
 *   empties the slot) and the level (1..99) it is learned at, in DigimonData.skills[slot] and skillLevels[slot - 1].
 *   Growth: statGrowth {strength..charisma} (0..5, the column of the level-up stat gain table), resistGrowth {"0".."6"} (1..5,
 *   the row of the resistance gain tables, battle and gym) and knownLevels {"0".."4"} (1..99, the level that marks skills[i]
 *   known). Left out because they index game tables or other rows: id, nameId, pairPartner, pairTech and blastForms.
 * enemy (a BattleTableEntry): stats {attack, defense, spirit, wisdom, speed} (0..32767), resistances {"0".."6"} and
 *   statusResists {poison, paralysis, confusion, sleep, knockOut} (0..32767), family (a value some original enemy has),
 *   techs {"1".."3"} (technique; slots 2 and 3 accept 0 for none), item (an item id, "dw3:item/<id>" or "<mod>:<key>"; 0
 *   none) and itemChance (0..1023). Left out: id, nameId and the AI (actions, counter).
 *
 * Validation (ADR-009 section 5): an unknown Digimon, a facet that does not match its id, an unknown name or a value out of
 * range rejects that field or entry with a log line; nothing invalid reaches the game. A technique given to a fighter must
 * have the animation slots its effects archive provides (techExtMissingSlot), the same rule as learnedBy.
 *
 * Applying: a partner's DIGIMON_DATA row is patched in memory at load and restored by digimonExtReset. An enemy's row is
 * patched in the battle table read buffer by digimonExtPatchBattleTable, which the one post-read hook of that file calls
 * first; the battle_loot table mod is applied on top of it (so battle_loot wins for item and itemChance). When several mods
 * change the same field, the one later in the mod order wins.
 */

#include <stddef.h>
#include <stdint.h>

#include "content.h"
#include "engine/game_state.h"

/* sizeof(BattleTableEntry): boot.c asserts it, so this unit can read the table without the game headers */
#define PC_BATTLE_ROW_SIZE 70

/* The original battle table (the bytes of FILE_BATTLE_TABLE: BattleTableEntry records up to an id of 0), copied: the enemy
   ids, their names and the reference come from it. Call before digimonExtLoad; without it enemy changes are rejected. */
void digimonExtSetEnemyTable(const unsigned char *file, size_t len);

/* The original fighters file (FILE_FIGHTERS bytes), copied: a new enemy shares the model of an original enemy fighter, so its
   entry is built from that fighter's. Call before digimonExtLoad; without it new enemies are rejected. */
void digimonExtSetFighters(const unsigned char *file, size_t len);

/* Reads the active content mods' digimon/ folders and content/encounters.json, validates, patches DIGIMON_DATA and keeps the enemy patches. Call after
   techExtLoad and itemExtLoad (names resolve) and after the fighter probe is set. Returns the fighters changed. */
int digimonExtLoad(void);
/* Puts DIGIMON_DATA back and forgets everything (tests, reload) */
void digimonExtReset(void);

/* 1 when digimon/<key>/ is original fighter <nnn>'s folder (d<nnn>: "d" and three digits): a change of that fighter, never a new Digimon */
int digimonExtKeyIsOriginal(const char *key);

/* The Digimon type of the content core (reference export: contentReferenceText); ids 3..470 */
const ContentType *digimonExtType(void);

/* DIGNAM entry of fighter `id` (a partner's or an enemy's nameId), or -1 when no such fighter */
int digimonExtNameId(int id);

/* The DIGIMON_DATA row + 1 a name stands for (the value the evolution lists and blastForms keep): an original partner form
   ("dw3:digimon/<id>") or a new partner form ("<mod>:<key>"); -1 for anything else (an enemy, a name no mod defines). */
long long digimonExtRowRef(const char *name);

/* ---- new Digimon (D3): the digimon/<key>/digimon.json records ---- */

int digimonExtNewCount(void);                /* new enemies and new partner forms together */
int digimonExtNewId(const char *fullName);   /* "<mod>:<key>" -> the id (>= 471), or -1 */
const char *digimonExtNewName(int id);       /* the "<mod>:<key>" of a new Digimon id, or NULL */
int digimonExtIsPartner(int id);             /* 1: `id` is a new partner form (P2), 0: an enemy or no new Digimon */
int digimonExtBaseOf(int id);                /* the original Digimon id a new Digimon was based on, or -1 */

/* The file numbers a "model" object may name by string: name -> the number of a file (a disc file or a virtual one, port_fs.h),
   or -1 when there is none. The default reads "<mod>:<path inside the mod's folder>" as a raw model archive and serves it as a
   virtual file (the same name, the same number until the next load). The model registry of model-import I5 ("dw3:model/<id>",
   "<mod>:<key>") sets its own before the fighters file is built; NULL restores the default. Not reset by digimonExtReset. */
void digimonExtSetFileResolver(int (*resolve)(const char *name));

/* The bytes of a disc file (malloc'd, the caller frees; NULL: not readable), to look at the motions a model takes by reference
   ("motions": "dw3:digimon/<id>" or {"file", "entry"}). With it, a slot the battle code plays that the archive leaves empty is a warning
   line, and "fill" (a slot 0..61 of that archive) makes the empty slots play it (a copy of the file as a virtual file). The same reader gives
   the effects archives of "scripts" ({"<slot>": {"from": <original fighter>, "slot": <its slot>}}): the form's effects file is copied with those
   slots replaced by the fighters' scripts, converted to the form's side when the fighter is of the other (script_mirror.h). NULL: no check. */
void digimonExtSetDiscReader(unsigned char *(*read)(int file, size_t *len));

/* The battle table and the fighters file with the new enemies added: malloc'd, whole sectors long (longer than the disc's
   file when the rows no longer fit), the original bytes untouched; NULL when there is nothing to add. The host serves them
   in place of the disc files (pfsSetGenerated); the post-read hook then patches the former as before. */
unsigned char *digimonExtBuildBattleTable(size_t *len);
unsigned char *digimonExtBuildFighters(size_t *len);

/* ---- placing enemies in encounters (D4): "encounters" of content/encounters.json ---- */

int digimonExtPlaceCount(void);
void digimonExtPlaceTarget(int i, int *encounter, int *slot);   /* the encounter's index and the slot 0..2 of placement i */
/* The BattleEnemy row of placement i (lives until digimonExtReset), the values it leaves out taken from `orig`, the row the
   slot has now (may be NULL); NULL when a needed value is missing. The host points Encounter.enemies[slot] at it. */
BattleEnemy *digimonExtPlaceRow(int i, const BattleEnemy *orig);

/* Applies the enemy patches to a battle table read buffer (BattleTableEntry records). Cheap when none. */
void digimonExtPatchBattleTable(uint8_t *buf, size_t len);
int digimonExtEnemyPatchCount(void);

#endif
