#ifndef PC_STAGE_MODS_H
#define PC_STAGE_MODS_H

/*
 * The stage patch layer (port-close S4) and the mod flag codes (S5), docs/stages.md section 7.
 *
 * A mod changes a field stage with mods/<mod>/stages/<nnn>.json (stage-patch-v1): per stage, the characters (actors) it patches by index
 * or key, adds (with a key other patches can name) or removes, and the talks it replaces or puts before the existing ones. The patches
 * go through the same model as the original (stage_data.h): the original is what the stage's own setup produced -- read at run time, so a
 * stage whose setup depends on the story progress is merged against what it gave THIS time -- and the mods' patches follow in load
 * order (the last one wins, a conflict is logged). The result is plain data; pc/boot.c turns it back into the arrays the field reads.
 *
 * This unit knows no game header. It reads the active mods through mods.h and keeps the parsed patches for the run.
 *
 * Mod flags: a patch's conditions and actions may name variables of mods ("mod-flag", "mod-var", "set-var", stage_codes.h). The game's
 * interpreter (src/engine/game/events.c) knows only numbers, so each op a patch uses gets a code in the port-reserved groups
 * (SC_MOD_GROUP_FIRST..LAST) that indexes a table here; pcModCheck / pcModApply are what events.c calls for those codes. The variables
 * themselves live in ext_state by name (pcExtVarGet / pcExtVarSet) and are saved in the "EXTV" section.
 */

#include "stage_data.h"

#define SM_FORMAT "stage-patch-v1"

/* A sprite a mod brings (S9): what the host built from mods/<mod>/sprites/<name>/ */
typedef struct SmSprite {
    int entry;  /* the set table's entry in the sprites' file */
    int width;  /* the character's width for the field's collision, in pixels */
    int w, h;   /* the largest frame it plays, in pixels: the VRAM slot it needs */
} SmSprite;

typedef struct SmHost {
    const StageCodeEnv *env; /* the item/card names of the content mods (modOp and modOpInfo are filled by smLoad); NULL: plain */
    int (*stageExists)(int number); /* whether a stage number is one of the build's (NULL: every number is) */
    /* named texts (S6): the entries file k of the text layer has before the appended ones (-1: unknown, the named talks of that stage are skipped) and a call
       when the entries appended to file k changed, so the text layer serves it again. NULL: no named texts. */
    int (*textFirst)(int k);
    void (*textChanged)(int k);
    /* the game mode a stage number runs in (an exit's destination, S8), 0 when it has none; NULL: the stage patch's "stage" members are refused */
    int (*stageMode)(int number);
    /* whether a row of the field encounters exists (S10); NULL: any number from 1 to 4095 */
    int (*encounterOk)(int row);
    /* S9: whether the game has a character with that id and a sprite file for it (a patch's "character"); NULL: any number */
    int (*characterOk)(int character);
    /* S9: builds the sprite of a folder (mods/<mod>/sprites/<name>, label "<mod>:<name>"). 0 and *out, or -1 with the reason in why. spritesDone runs after
       the last one of a load (the host closes its file). NULL spriteAdd: the mods' sprites are ignored. */
    int (*spriteAdd)(const char *dir, const char *label, SmSprite *out, char *why, size_t whySize);
    void (*spritesDone)(void);
} SmHost;

/* What one smApplyActors did */
typedef struct SmReport {
    int patched;   /* actors with at least one field or talk change */
    int added;
    int removed;
    int skipped;   /* patches, adds and removes that could not apply (a missing index at this progress, a removed target, an image the stage has not): logged */
    int conflicts; /* fields two mods both set (the later mod's value stands): logged */
    int texts;     /* named texts of the stage that got an entry of their own */
    int slotsPatched, slotsAdded, slotsRemoved;       /* S8 */
    int eventsPatched, eventsAdded;                   /* S8 */
    int battlesPatched, areasAdded;                   /* S10 */
} SmReport;

/* Reads mods/<mod>/stages/<nnn>.json of every active mod (modsCount/modsDirOf, in order). A file that is not valid is refused whole, with
   "[stages] <path> rejected: <reason>" on the log. Forgets what an earlier call loaded. Returns the number of files kept. */
int smLoad(const SmHost *host);
void smUnload(void); /* forgets the patches and the table of mod ops */
int smPatchCount(void);      /* files kept */
int smHasPatches(int stage); /* 1 when a kept file patches that stage */

/* Merges every patch of `stage` into s->actors (replaced by a new array: original order, the added ones last, the removed ones gone) and returns 1 when
   something changed; 0 when the stage has no patch (s is untouched, so the caller keeps the game's own arrays). s->nimages is how many actor
   images the stage has from index 2 (an image outside 2..nimages+1 is refused). `rep` may be NULL. */
int smApplyActors(int stage, SdStage *s, SmReport *rep);

/* Everything a stage's patches change besides the characters: slots (S8: patch, add, remove), events (S8: patch, add), battles (S10), then the characters
   as smApplyActors does. Each of those lists in s is replaced by a new one (slots and events keep the original order, the added ones last; a removed slot
   stays in its place as a slot that does nothing, type SM_SLOT_OFF, because the map's cells name slots by index), and the named texts of the events get
   their entries. Returns 1 when something changed, 0 when the stage has no patch (s is untouched). */
int smApplyStage(int stage, SdStage *s, SmReport *rep);
#define SM_SLOT_OFF 15 /* the type of a slot a patch removed: the field's trigger lookup passes it by */

/* ---- mod sprites (S9) ----
   mods/<mod>/sprites/<name>/ (sprite.json + PNGs) is a character of the port: smLoad asks the host to build each folder and gives it the character id
   SM_SPRITE_CHAR_BASE + k (the order of loading), which a patch names with "sprite": "<mod>:<name>" (the bare name is the file's own mod's). An actor of
   one is an actor whose `image` is SD_IMAGE_AUTO (it needs its own VRAM slot): smApplyStage numbers the slots and lists their characters in SdStage.extra. */
#define SM_SPRITE_CHAR_BASE 0x4000 /* FieldActorEntry.id is an s16: ids up to 0x7FFF */
#define SM_SPRITES_MAX 0x3FFF
int smSpriteCount(void);
const char *smSpriteName(int character); /* "<mod>:<name>" of a mod sprite's character id, else NULL */
int smSpriteInfo(int character, SmSprite *out); /* 1 and the info for a mod sprite's character id */

/* ---- mod events (S8) ----
   A stage patch adds an event under a name "<mod>:<name>"; it gets an id from SM_EVENT_BASE up (smEventId, the same name is the same id for the whole run),
   which the slots' "event", the run-event action and the stage's events table use. */
#define SM_EVENT_TEXT_STAGE 0xFFFF /* the entry of a mod event's `text`: its talks name entries of the stage's own text file (not a block of the game's) */
#define SM_EVENT_BASE 30000
#define SM_EVENTS_MAX 4096
int smEventId(const char *name);   /* the id of a name (made on first use); -1 for a name that is not valid or when the table is full */
const char *smEventName(int id);   /* the name of an id the table made, else NULL */

/* ---- named texts (S6) ----
   A talk of a stage patch may say `"text": "<mod>:<name>"` instead of an `entry`. The mods' strings live in mods/<mod>/text/<lang>/stages.json
   (stage-text-v1: {"format": "stage-text-v1", "texts": {"<name>": "the line"}}, one folder per language like the other text files); smLoad reads them
   before the patches, and a name no mod defines (with at least an English text) rejects the patch file. A language that lacks a name shows the English
   one (es-419 tries Spanish first). When a stage with named talks starts (smApplyActors), the names it uses become entries appended after its text file's
   own (the host's textFirst says where they start) and the talk's entry is the number of its own; the text layer's hook (smTextCount / smTextOf)
   hands the strings to text_data.c, which appends them to every language's copy of the file and fits them to the talk box. */
#define SM_TEXT_BASE 0x10000 /* a talk's entry at or above this is a named text of the registry (entry - SM_TEXT_BASE) until the stage resolves it */
int smTextCount(int k);                                   /* entries appended to text file k (tdFileBase's number), 0 when none */
const char *smTextOf(int k, int i, const char *langDir);   /* appended entry i of file k in a language folder ("eng", "spa", "es-419"...); never NULL */
int smTextNames(void);                                    /* the names loaded (all mods) */

/* ---- mod flag codes ---- */

/* the code of an op (stage_codes.h SC_MOD_*), made on demand and the same for the same op; -1 when the table is full or the name is not valid */
int smOpCode(int kind, const char *name, int cmp, int n);
/* what a code names; 1 when the table made it */
int smOpInfo(unsigned code, int *kind, char *name, size_t cap, int *cmp, int *n);
/* a StageCodeEnv that is `base` (may be NULL) with modOp/modOpInfo set to this table */
void smCodeEnv(StageCodeEnv *out, const StageCodeEnv *base);

/* ---- the port's actions (S7, stage_codes.h) ----
   The same kind of table for the actions beyond the PS1 vocabulary (give-money, take-money, set-progress, warp, sound): an op is a kind and up to
   SC_ACT_ARGS numbers, the same op is the same code, and applying one calls the runner the host installed (pc/boot.c has the game's side: GAME.money,
   GAME.progress, FIELDSTG_leaveField, playSound). Without a runner an action does nothing. As conditions they always hold. */
int smActCode(int kind, const int *args);              /* -1 when the table is full or the kind is not one */
int smActInfo(unsigned code, int *kind, int *args);    /* 1 when the table made the code; args holds SC_ACT_ARGS numbers */
void smSetActRunner(void (*run)(int kind, const int *args));

/* A request to change what the field is doing (the warp, start-battle and run-event actions): the runner posts it, the field's own running frame takes it and
   asks the game for it (FIELDSTG_leaveField, FIELDSTG_startBattle, FIELDSTG_startEvent), so a talk's actions never change the stage from inside the talk.
   One request is kept (a later one replaces it). */
enum { SM_REQ_LEAVE = 0, SM_REQ_BATTLE = 1, SM_REQ_EVENT = 2 };
typedef struct {
    int kind;
    int mode;   /* leave: the game mode to leave for; battle: the encounter; event: the event id */
    int arg;    /* leave: -1 for a plain stage change; battle: the fight stage */
    int x, y;   /* leave: where to appear, in map pixels (the game's exits use << 8 of them); battle: x is its music */
    int dir;    /* leave: the direction to face */
} SmRequest;
void smPostRequest(const SmRequest *l);
int smTakeRequest(SmRequest *out); /* 1 and the request when one waits (and clears it) */

/* what src/engine/game/events.c calls for the codes of the reserved groups (the flags' and the actions'): checkCondition(code, value) and applyAction(code, value) */
int pcModCheck(unsigned short code, unsigned short value);
void pcModApply(int code, int value);

#endif
