#ifndef PC_STAGE_CODES_H
#define PC_STAGE_CODES_H

/*
 * The code vocabulary of the field stages (port-close S1, docs/stages.md): the game's two small languages as symbolic JSON and back.
 *
 *   - Condition/action lists: the (code, value) u16 pairs of src/engine/game/events.c (checkCondition / applyAction), which the talks, the
 *     characters and the slots of a stage hold. One pair is one JSON array, ["flag", "1A:1F", 1] or ["item", "dw3:item/18", 1].
 *   - Event scripts: the s16 commands of FIELDSTG_runEvent (SCRIPT_* in include/field/field_map.h), ["walk", 2, 191, 400, 3].
 *
 * Both directions are lossless for any input: a code the vocabulary has no name for is ["raw", "0xHHHH", value] and an unknown script word
 * ends the script as ["end", "0xHHHH"]. A list is stored without its terminator (the 0xFFFF code, the SCRIPT_END word 0 for a script).
 *
 * Portable C, headless: no game header. The only game knowledge it can use arrives through a StageCodeEnv (the names of items and cards, the
 * rows of SPECIAL_CONDITIONS); without one the output uses plain "dw3:item/<id>" names and no special comments, and still round-trips.
 */

#include <stddef.h>
#include <stdint.h>

#include "content.h"

/* A growing text buffer (the JSON writers append to it) */
typedef struct ScBuf {
    char *b;
    size_t n, cap;
    int bad; /* out of memory: the text is incomplete */
} ScBuf;

void scPrintf(ScBuf *B, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void scPutEsc(ScBuf *B, const char *s); /* s as the inside of a JSON string (UTF-8 passes through) */
void scFree(ScBuf *B);

/* What the vocabulary can use of the game; every member may be NULL */
typedef struct StageCodeEnv {
    /* kind "item" or "card": the name of id (a registry name, "dw3:item/18" or "<mod>:<key>"); 1 when there is one */
    int (*nameOf)(void *ud, const char *kind, int id, char *out, size_t cap);
    /* kind "item" or "card": the id of a registry name that is not "dw3:<kind>/<id>"; -1 when unknown */
    int (*resolve)(void *ud, const char *kind, const char *name);
    /* SPECIAL_CONDITIONS row of an id: its check byte (primitive << 4 | op) and argument; 1 when the row exists */
    int (*specialRow)(void *ud, int id, int *check, int *arg);
    void *ud;
    /* mod flags and counters (S5): the code of a named op ("mod-flag", "mod-var", "set-var"), made on demand (SC_MOD_*, SC_CMP_*; cmp and n
       only for SC_MOD_VAR); -1 when it cannot be made (the table is full). The code is in the groups SC_MOD_GROUP_FIRST..LAST. */
    int (*modOp)(void *ud, int kind, const char *name, int cmp, int n);
    /* what a code in those groups names (name into `name`, cap bytes); 1 when the code is one this table made */
    int (*modOpInfo)(void *ud, unsigned code, int *kind, char *name, size_t cap, int *cmp, int *n);
    /* the special conditions a mod adds (S3): the name "<mod>:<name>" of an id 0x100-0x1FF (NULL when it is not one), and `resolve` with kind
       "special" gives the id of a name (-1 when no mod defines it) */
    const char *(*specialName)(void *ud, int id);
    /* port actions (S7): the code of an action made on demand (SC_ACT_*; args are the numbers the action takes, see below); -1 when it cannot be made (the
       table is full). The code is in the groups SC_ACT_GROUP_FIRST..LAST. */
    int (*actOp)(void *ud, int kind, const int *args);
    /* what a code in those groups is (kind, args[SC_ACT_ARGS]); 1 when the code is one this table made */
    int (*actOpInfo)(void *ud, unsigned code, int *kind, int *args);
    /* whether a stage number is one of the build's (NULL: every number is), for the warp action */
    int (*stageExists)(void *ud, int stage);
    /* mod events (S8): the id the port gives a name "<mod>:<name>" (made on demand; -1 when it is not a valid name or the table is full), and the name of
       an id it made (NULL when it is not one), for the run-event action */
    int (*eventId)(void *ud, const char *name);
    const char *(*eventName)(void *ud, int id);
    /* mod stages (new-stages T4, shim/mod_stages.h): the stage number the port gives a name "<mod>:<name>" (-1 when no such stage is registered), and the
       name of a number it gave (NULL when it is not one), for the warp action. NULL: names are not accepted, numbers print as numbers. */
    int (*stageByName)(void *ud, const char *name);
    const char *(*stageName)(void *ud, int number);
    /* new field encounters (new-stages T5b): the number of a row a mod added, by name "<mod>:<name>" (-1: none), and the name of such a number (NULL: not
       one), for the start-battle action. NULL encounterByName: names are not accepted, numbers print as numbers. */
    int (*encounterByName)(void *ud, const char *name);
    const char *(*encounterName)(void *ud, int number);
} StageCodeEnv;

/* ---- the codes the port adds for mods (S5) ----
   The groups 0x62..0x68 of events.c's `code >> 8 & 0xFE` are unused by the game; the port reserves them (`#ifdef DW3_PC` in checkCondition and
   applyAction). The low 9 bits of a code, plus 512 per group above the first, index a table of ops the port builds from the stage patches
   (shim/stage_mods.c): an op names a variable ("<mod>:<name>", kept in ext_state by name) and what to do with it.
     mod-flag  condition: the variable is non-zero (value 1) or zero (value 0); action: set it to 1 or 0
     mod-var   condition: the variable <cmp> n holds (value 1) or does not (value 0); action: nothing
     set-var   action: the variable becomes `value`; condition: always holds
     ask       action: ["ask", "<mod>:<name>"], when the talk's line has been said the field shows the game's own Yes/No panel and the variable becomes 1 (yes) or 2 (no);
               the speaker's talks are then chosen again (so the next talk can read the answer); condition: always holds */
#define SC_MOD_GROUP_FIRST 0x62
#define SC_MOD_GROUP_LAST 0x68
#define SC_MOD_OPS_MAX 2048 /* 4 groups x 512 */
enum { SC_MOD_FLAG = 1, SC_MOD_VAR = 2, SC_MOD_SET = 3, SC_MOD_ASK = 4 };
enum { SC_CMP_EQ, SC_CMP_NE, SC_CMP_GE, SC_CMP_GT, SC_CMP_LE, SC_CMP_LT, SC_CMP_COUNT };
const char *scCmpName(int cmp);     /* "==", "!=", ">=", ">", "<=", "<"; NULL when out of range */
int scCmpByName(const char *name);  /* -1 when unknown */
/* "<mod>:<name>": the mod's folder name (letters, digits, . _ -), a colon and a lowercase name (a-z 0-9 _ - /); at most 120 bytes */
int scModNameValid(const char *name);

/* ---- the port's own actions (S7) ----
   Actions the PS1 vocabulary did not have, each a call to an engine function that already exists. They live in the reserved groups after the mod flags'
   (0x6A and 0x6C: the game uses none of them), 512 ops a group, in a table the patch layer builds like the flags' (shim/stage_mods.c): the code of an
   op is 0x6A00 + 512 per group + its index, and `pcModApply` runs it through the host's runner. As a condition each of them always holds (the pair's
   value is ignored; compiling writes 1).
     give-money    ["give-money", amount]       GAME.money rises by amount, up to 9 999 999 (as the money rows of SPECIAL_CONDITIONS)
     take-money    ["take-money", amount]       GAME.money falls by amount, down to 0
     set-progress  ["set-progress", n]          GAME.progress becomes n (0-99), the story's position
     warp          ["warp", stage, x, y, dir]   leaves the field for that stage and appears at (x, y) facing dir 0-7 (FIELDSTG_leaveField, as the exits do)
     sound         ["sound", "<name>"]          plays one of the game's shared sounds (scSoundName)
     start-battle  ["start-battle", encounter, stage, music]
                                                starts the field battle `encounter` (FIELDSTG_encounters) in the fight stage `stage` with `music`, the way the
                                                stages' own event battles do (FIELDSTG_startBattle)
     run-event     ["run-event", "<mod>:<name>"]  starts the event a stage patch added under that name (or a number: an event id of the stage's own table) */
#define SC_ACT_GROUP_FIRST 0x6A
#define SC_ACT_GROUP_LAST 0x6C
#define SC_ACT_OPS_MAX 1024 /* 2 groups x 512 */
#define SC_ACT_ARGS 4
#define SC_MONEY_MAX 9999999
#define SC_PROGRESS_MAX 99
enum { SC_ACT_GIVE_MONEY = 1, SC_ACT_TAKE_MONEY = 2, SC_ACT_SET_PROGRESS = 3, SC_ACT_WARP = 4, SC_ACT_SOUND = 5, SC_ACT_START_BATTLE = 6, SC_ACT_RUN_EVENT = 7, SC_ACT_KINDS = 8 };
const char *scActName(int kind); /* "give-money" ...; NULL when out of range */
int scActByName(const char *name); /* 0 when unknown */
#define SC_SOUND_COUNT 12
const char *scSoundName(int i);        /* "select", "cancel", ...: the shared sounds of include/engine/sound.h, in the order the host's table has them */
int scSoundByName(const char *name);   /* -1 when unknown */

/* ---- condition/action lists ---- */

/* The (code, value) pairs of `words` (n u16, even, no terminator) as a JSON array: [["flag","1A:1F",1], ...]. 0, or -1 (odd n or no memory). */
int scListToJson(const StageCodeEnv *env, const uint16_t *words, int n, ScBuf *out);
/* The JSON array `list` as words (malloc'ed, *n of them; NULL for an empty list). 0, or -1 with the reason in err. */
int scListFromJson(const StageCodeEnv *env, const ExtJson *list, uint16_t **words, int *n, char *err, size_t errCap);
/* Copies a list that ends with the 0xFFFF code (at most maxPairs pairs are read; a longer one is cut there). *n is the u16 count without the
   terminator; *ended is 1 when the terminator was found. NULL for an empty list. */
uint16_t *scListCopy(const uint16_t *src, int maxPairs, int *n, int *ended);

/* ---- event scripts ---- */

/* The script at `words` (s16, `avail` of them readable; -1 when unbounded) up to and including its end word: the words read, 0 on none. The
   end word is the first whose kind (word >> 8) is not 1, 2, 3 or 6 -- FIELDSTG_runEvent ends the event on kind 0 and on any other. */
int scScriptLength(const int16_t *words, int avail);
/* Where the talk commands (kind 2: word, box, entry, id, kind) start in a script of n words, in order (the first `max` of them); returns how many there are */
int scScriptTalks(const int16_t *words, int n, int *offsets, int max);
/* A script (n words, ending with its end word) as a JSON array: [["place",2,10,20], ..., ["end"]]. 0, or -1. */
int scScriptToJson(const int16_t *words, int n, ScBuf *out);
/* The JSON array as words (malloc'ed, *n of them, the end word included). 0, or -1 with the reason in err. */
int scScriptFromJson(const ExtJson *list, int16_t **words, int *n, char *err, size_t errCap);

/* The name of a slot type (SLOT_EXIT 1 ... SLOT_LAUNCH_OUT 14), or NULL */
const char *scSlotTypeName(int type);
int scSlotTypeByName(const char *name); /* 0 when unknown */

#endif
