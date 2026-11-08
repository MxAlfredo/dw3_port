#ifndef PC_STAGE_DATA_H
#define PC_STAGE_DATA_H

/*
 * The stage data model and the stage-v1 files (port-close S2, docs/stages.md).
 *
 * A field stage is mostly data the game's own setupStage() hands to FIELDSTG (FieldState): the start, the music, the characters with their
 * conditions and talks, the exits and the events. SdStage is that data in memory, in one portable shape, apart from the rendering and the
 * game structs: this file knows no game header, so the same model is read from the game's tables (pc/boot.c, original content first), from
 * JSON files (stage-v1, what the reference export writes and what a mod patch will name) and written back, headless. The online project later
 * reuses it as it is.
 *
 * <data>/stages/s<nnn>/stage.json is the reference copy of stage WSTAG<nnn> in the user's own data folder: it is made from the player's disc, so it
 * is never committed (ADR-001). Same manifest and ".new" rule as the other data/ families (ADR-005); the manifest also holds the exporter
 * version, and a folder of the current version with every file present is skipped. Bump SD_EXPORT_VERSION when the output bytes change.
 *
 * Lists of codes (conditions, actions) are stored as u16 words without terminator; a list pointer that is NULL in the game (no conditions,
 * the last talk, no actions) is a list that is not set here, which is not the same as an empty set one.
 */

#include <stddef.h>
#include <stdint.h>

#include "stage_codes.h"

#define SD_EXPORT_VERSION 4
#define SD_FORMAT "stage-v1"

typedef struct SdList {
    uint16_t *w; /* (code, value) pairs, no terminator; NULL when empty */
    int n;       /* words (twice the pairs) */
    int set;     /* 0: the game's pointer is NULL */
} SdList;

typedef struct SdTalk {
    SdList cond;    /* not set: the last talk, which always holds */
    SdList actions; /* applied when the talk ends */
    int entry;      /* its text's entry in the stage's text file */
    char *preview;  /* the text in the player's language, or NULL: read only, nothing reads it back */
} SdTalk;

typedef struct SdActor {
    int character; /* Actor.key1: 2 the player, 3 the partner, the others the game's characters */
    int image;     /* its ActorImage, counted from 2 */
    int x, y, dir; /* pixels; direction 0-7 */
    SdList cond;   /* not set: always there */
    SdTalk *talks;
    int ntalks;
} SdActor;

typedef struct SdSlot {
    SdList cond; /* up to two pairs */
    int type;    /* SLOT_* */
    int arg;
    int x, y, dir;
    int hideAnim, place, placeArg;
    /* port-only (S8): a trigger area in map pixels that sets the slot off wherever the map's own trigger layer has no cell; the game's slots have none.
       facing is the direction the player must face (0-7), -1 for none. */
    int hasArea;
    int ax, ay, aw, ah, facing;
} SdSlot;

typedef struct SdPreview {
    int entry;
    char *text;
} SdPreview;

typedef struct SdEvent {
    int id;
    int hasText;
    int textFile; /* FieldEvent.text >> 16: the file counted without the language (file = LANGUAGE + textFile) */
    int textEntry;
    int hasScript;
    int16_t *script; /* the commands of FIELDSTG_runEvent, the end word included */
    int nscript;
    int hasStart; /* a C task starts it (no script): the model cannot hold it */
    int hasEnd;   /* a C function runs when it ends */
    int isNew;    /* port-only (S8): added by a stage patch (the events of the game come first, in its order, and keep their start and end functions) */
    SdPreview *previews; /* the lines of its script's talks, in the player's language */
    int npreviews;
} SdEvent;

/* The stage's battles (FieldState.battles, S10): per area of the map (the value of a cell of its area layer, 1 to 4 in the game's own lists, the fourth being
   the one events start), a rate (an index of FIELDSTG_battleRates) and the eight battles one of which is picked at random. A stage with no battles
   (present 0) has none; the model holds up to SD_AREAS_MAX areas, a port extension: the game's list has four. */
#define SD_AREAS_MAX 16
typedef struct SdAreaBattle {
    int encounter; /* a row of FIELDSTG_encounters */
    int stage;     /* the fight stage (BATTLE_SETUP.stage) */
    int music;     /* BATTLE_SETUP.music */
} SdAreaBattle;

typedef struct SdArea {
    int rate;
    SdAreaBattle b[8];
} SdArea;

typedef struct SdBattles {
    int present;
    int place, serial; /* FieldBattles.id (GAME.place) and the number nothing reads: kept so the export tells which list the setup chose */
    int nareas;
    SdArea areas[SD_AREAS_MAX];
} SdBattles;

/* Port-only (fishing and hidden spots, new-stages T9): what a gauge game cell or a hidden spot's prize gives. A list of weighted results, one picked at random
   (sdCatchPick). `set` 0: not given, the game's own result stands (an event battle of the fourth area). A set list with no results gives nothing. */
enum { SD_CATCH_NOTHING, SD_CATCH_BATTLE, SD_CATCH_ITEM };
typedef struct SdCatch {
    int kind;      /* SD_CATCH_* */
    int weight;    /* 1 to SD_CATCH_WEIGHT_MAX: its share of the list */
    int encounter; /* SD_CATCH_BATTLE: a row of FIELDSTG_encounters, the fight stage (BATTLE_SETUP.stage) and the music (BATTLE_SETUP.music) */
    int stage;
    int music;
    int item;      /* SD_CATCH_ITEM: an item id, one copy */
} SdCatch;
#define SD_CATCH_WEIGHT_MAX 1000
#define SD_CATCH_MAX 16 /* results in one list */
typedef struct SdCatchList {
    SdCatch *v;
    int n;
    int set;
} SdCatchList;

/* The results of one gauge slot: its cells 1 and 2 (cell[0], cell[1]; 0 is a miss, and the rows of the game have no 3). `slot` is its index in SdStage.slots. */
typedef struct SdFishSlot {
    int slot;
    SdCatchList cell[2];
} SdFishSlot;

/* A hidden spot a patch adds (a map object of animation 0xFF): where the player searches, in map pixels */
typedef struct SdSpot {
    int x, y;
} SdSpot;

typedef struct SdImage {
    int x, y, imageX, imageY, u, v, clutX, clutY;
} SdImage;

/* S9: an actor whose `image` is SD_IMAGE_AUTO wants an ActorImage of its own. smApplyStage gives each distinct character of those one extra slot, numbered
   from SD_IMAGE_EXTRA_FIRST (the game's own actor images stop at 19: FieldImages.actors[20]) and listed in SdStage.extra; pc/boot.c finds VRAM for them. */
#define SD_IMAGE_AUTO (-1)
#define SD_IMAGE_EXTRA_FIRST 20

typedef struct SdStage {
    int number;       /* WSTAG<number> */
    char name[16];    /* "WSTAG380" */
    int modes[4];     /* the game modes that run it */
    int nmodes;
    int textFile;     /* the stage's text file counted without the language (file = LANGUAGE + textFile), -1 for none */
    int mapFile, sheetEntry, imageEntry, imageFile;
    int soundBank, music;
    int startX, startY, startDir; /* as the game keeps them (pixels * 256) */
    int spriteColor[4];
    int nobjects;     /* map objects (StageTile) */
    SdImage *images;  /* the ActorImages from index 2 on, as many as the actors use */
    int nimages;
    int *extra;       /* S9: the character each extra actor image (SD_IMAGE_EXTRA_FIRST + k) is for; only smApplyStage fills it */
    int nextra;
    SdActor *actors;
    int nactors;
    SdSlot *slots;
    int nslots;
    SdEvent *events;
    int nevents;
    SdBattles battles;
    /* port-only (T9): fishing results per gauge slot and a default for the others, the hidden spots a patch adds and the results of their prize
       (prize[0] the common one, the game's event battle 3, prize[1] the rare one, battle 6) */
    SdFishSlot *fishing;
    int nfishing;
    SdCatchList fishDefault[2];
    SdSpot *spots;
    int nspots;
    SdCatchList prize[2];
    char **functions; /* what the stage's C code defines besides its setup and start */
    int nfunctions;
    char **notes;     /* what the stage does that this model does not hold */
    int nnotes;
} SdStage;

void sdCatchListFree(SdCatchList *l);
/* The result of the list that r (any number) picks by weight: NULL when the list is empty. Nothing is read of `set`. */
const SdCatch *sdCatchPick(const SdCatchList *l, unsigned r);
void sdStageFree(SdStage *s); /* frees what it owns and zeroes it */
void sdActorFree(SdActor *a); /* frees one actor's lists and talks and zeroes it (S9: the host drops an actor that has no slot) */
int sdStageAddNote(SdStage *s, const char *note);
int sdStageAddFunction(SdStage *s, const char *name);

/* The stage as a stage-v1 document (malloc'ed text), or NULL when memory ran out. */
char *sdStageToJson(const StageCodeEnv *env, const SdStage *s);
/* A stage-v1 document into *out (zeroed first). 0, or -1 with the reason in err. */
int sdStageFromJson(const StageCodeEnv *env, const char *text, size_t len, SdStage *out, char *err, size_t errCap);
/* Whether two stages hold the same data (the previews and the name are data too) */
int sdStageEqual(const SdStage *a, const SdStage *b);

/* ---- building a stage: pc/boot.c reads the game's own tables into the model with these ---- */

/* Each add appends a zeroed record and returns it (valid until the next add of that kind); NULL when memory ran out */
SdActor *sdStageAddActor(SdStage *s);
SdSlot *sdStageAddSlot(SdStage *s);
SdEvent *sdStageAddEvent(SdStage *s);
SdTalk *sdActorAddTalk(SdActor *a);
SdImage *sdStageAddImage(SdStage *s);
/* A list as the game keeps it: `src` ends with the 0xFFFF code (at most maxPairs pairs are read); NULL src leaves it not set */
int sdListFromCodes(SdList *l, const uint16_t *src, int maxPairs);
/* The event's script, read up to its end word (scScriptLength) */
int sdEventSetScript(SdEvent *e, const int16_t *script);
int sdEventAddPreview(SdEvent *e, int entry, const char *text);
int sdTalkSetPreview(SdTalk *t, const char *text);
/* What the stage holds, for the export's log: code lists, their pairs, scripts and their words */
typedef struct SdCounts {
    long lists, pairs, scripts, words, talks;
} SdCounts;
void sdStageCount(const SdStage *s, SdCounts *c);

/* ---- the stages of the build ---- */

/* One field stage overlay of the build, generated by pc/Makefile (build/stage_setups.c) from the stage objects: the entry points of its
   setupStage and startStage (NULL when it has none: WSTAG260 is only the story events' scripts) and the C functions it defines, comma
   separated. The table ends with number 0. Game-linked builds only (pc/boot). */
typedef struct SdSetup {
    const char *name; /* "WSTAG380" */
    int number;
    void (*setup)(void);
    void *(*start)(void *owner);
    const char *functions;
} SdSetup;

extern const SdSetup SD_SETUPS[];

/* ---- the reference export ---- */

typedef struct SdExportStats {
    int written, kept, newFiles, failed;
} SdExportStats;

/* The data folder (NULL: no export). Loads the stages manifest. */
void sdExportSetup(const char *dataDir);
/* 1 when the folder has the current exporter version and every file `s<numbers[i]>/stage.json` */
int sdExportUpToDate(const int *numbers, int count);
/* Writes <data>/stages/s<nnn>/stage.json under the manifest rule. 0, or -1 on a failure. */
int sdExportPut(int number, const char *text, SdExportStats *st);
/* Stamps the exporter version (when nothing failed) and saves the manifest. */
void sdExportEnd(int ok);

#endif
