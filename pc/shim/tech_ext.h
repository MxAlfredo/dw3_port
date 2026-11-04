#ifndef PC_TECH_EXT_H
#define PC_TECH_EXT_H

/*
 * New techniques from content mods (extended techs T2/T3, ADR-006 phase 2, ADR-008).
 *
 * A mod of kind "content" declares techniques in mods/<mod>/content/techs.json (format "techs-v1"):
 *
 *   { "format": "techs-v1",
 *     "new": { "spark": { "base": 7, "mp": 3, "power": 120,
 *                         "name": { "eng": "...", "spa": "..." },
 *                         "description": { "eng": "..." },
 *                         "learnedBy": { "3": { "slot": 2, "level": 5 } } } } }
 *
 * The technique is "<mod folder>:<key>". Every valid declared technique of every active content mod gets a numeric id
 * from PC_TECH_FIRST_EXT_ID (444) up, in alphabetical order of that full name, so the same mods always give the same
 * ids; the ids are never trusted across installs: a save stores the NAME beside the game block (ext_state.h, "EXTT")
 * and the load remaps it. Its TechData is a copy of the base technique's (1..443) with the overrides, kept in the store
 * the game reaches through PC_TECH (engine/tech_access.h -> pcTechRef).
 *
 * Overrides: mp, power, accuracy, elementPower, effectChance, effectPower (whole numbers), hitCount (up to the most an
 * original technique does) and script (0..17: the archive of a fighter has 18 scripts, a higher index reads memory
 * outside it). icon, kind, element, family, effect, scriptStage, scriptEffect and scriptSound are indexes into game
 * tables: only a value some original technique already uses is accepted.
 *
 * "learnedBy" (T3): the Digimon (DigimonData.id, as a number or "dw3:digimon/<id>") that learn it, and in which skill slot (1..6, DigimonData.skills[slot])
 * at which level (1..99, DigimonData.skillLevels[slot - 1]). The game's own learning code (STFGTREP_addSkill) reads
 * those two fields, so the table is patched at load and nothing else changes. An entry is rejected when the Digimon's
 * fighter has no script slot for the technique's animation (see techExtSetFighterProbe).
 */

#include "content.h"
#include "engine/game_state.h"

/* Reads the active content mods' techs.json, assigns ids, fills the technique store, applies learnedBy and hooks the
   names (ext_state) and the text. Call after the disc is open (the compatibility check reads it), before the first
   mode. Returns the number of techniques. */
int techExtLoad(void);
/* Forgets everything and puts the Digimon table back (tests) */
void techExtReset(void);

/* The technique type of the content core (its reference export: contentReferenceText) */
const ContentType *techExtType(void);

int techExtCount(void);
int techExtIdOf(const char *fullName);   /* the id (>= 444), or -1 */
const char *techExtNameOf(int id);       /* "<mod>:<key>", or NULL when no technique has that id */
/* The text of technique `index` (0 .. count-1, id 444 + index) in a language folder name: desc 0 the name, 1 the
   description. Falls back es-419 -> spa -> eng. Never NULL for a valid index. */
const char *techExtText(int index, const char *langDir, int desc);

/* Whether the fighter of Digimon `digimonId` has script slot `slot` in its effects archive: 1 yes, 0 no, -1 unknown
   (then the learnedBy entry is accepted). The host installs a disc-backed one (pc/boot.c); tests install their own. */
/* The technique a mod names: an original (a "dw3:tech/<id>" name) or a technique of a mod ("<mod>:<key>"): its id, or -1 */
int techExtResolve(const char *name);
/* The first script slot (of `script`, and for a partner the ones its battle code can switch to) that the fighter `digimon`
   lacks in its effects archive, or -1 when it has them all (or is unknown). The check of learnedBy and of digimon.json. */
int techExtMissingSlot(int digimon, int script, int partner);
/* Does the fighter of `digimon` have script `slot` in its effects archive? 1 yes, 0 no, -1 unknown (no fighters file to look in).
   The reference export lists these as "scriptSlots". */
int techExtHasSlot(int digimon, int slot);

typedef int (*TechExtProbeFn)(int digimonId, int slot);
void techExtSetFighterProbe(TechExtProbeFn probe);

#endif
