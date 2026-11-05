#ifndef PC_MOD_STAGES_H
#define PC_MOD_STAGES_H

/*
 * The registry of field stages that mods add (new-stages T4, ADR-012, docs/stages.md section "Escenarios nuevos").
 *
 * A mod stage is named "<mod>:<name>" and nothing else: the key is what a save and a mod's JSON hold. The port gives each registered
 * stage a SESSION MODE from MS_MODE_FIRST up, in the alphabetical order of the keys (so the same mods give the same modes on every run,
 * whatever the order they were read in), and a stage NUMBER (MS_NUMBER_BASE + the index) that stands for the stage where the stage
 * patch layer and the warp action only keep numbers. Modes and numbers are never saved.
 *
 * Order of use (the loader of T5 follows it):
 *   1. msRegister every stage of every active mod (fills the data of the MsStage it returns);
 *   2. msSeal: assigns the modes; from here a register is refused (a mode may already be in a slot or an action);
 *   3. msSetFiles for each stage in mode order (virtual files take numbers in the order they are added, port_fs.h);
 *   4. the host (pc/boot.c) answers FIELDSTG_pickStage through msByMode, and the stage patch layer resolves names through msNumberOfKey.
 *
 * This unit knows no game function: the game side (the FieldState of a stage, the generic start task) is pc/boot.c's pcModStagePick.
 * It needs port_fs.h (virtual files), ext_state.h (the visited variable) and stage_codes.h (the name rule). Host test: pc/modstagetest.c.
 */

#include <stddef.h>
#include <stdint.h>

#define MS_MODE_FIRST 0x300 /* game modes 0x300-0x3FF run FIELDSTG (overlay.c: MODE_ENTRY_POINTS[3] is FIELDSTG_start), like 0x2xx */
#define MS_MODE_COUNT 256
#define MS_MODE_LAST (MS_MODE_FIRST + MS_MODE_COUNT - 1)
#define MS_NUMBER_BASE 0xF000 /* the stage number of the stage with the mode MS_MODE_FIRST (the game's own numbers are below 1000) */
#define MS_KEY_MAX 100        /* bytes of "<mod>:<name>": the visited variable "<mod>:visited.<name>" must fit PC_EXT_VAR_NAME_MAX */

/* The game's mode of a field mode (0x2xx and 0x3xx run FIELDSTG). The port's own checks of "the field is running" use it. */
#define MS_IS_FIELD_MODE(mode) ((((mode) >> 8) & 0xFF) == 2 || (((mode) >> 8) & 0xFF) == 3)

/* Where a player lands when a stage cannot be entered (a saved game whose mod is gone): the game's WSTAG330, Central Park, mode 0x21D
   (fieldstg.c:2829) at its own start, 0x38400 x 0x3E800 in 1/256 pixels (wstag330.c:33), facing 0. */
#define MS_DEFAULT_FALLBACK_MODE 0x21D
#define MS_DEFAULT_FALLBACK_X 900
#define MS_DEFAULT_FALLBACK_Y 1000
#define MS_DEFAULT_FALLBACK_DIR 0

/* What the stage's setup tells FIELDSTG (FieldState, include/field/field_map.h), without a game header. Pointers are owned by the loader
   and stay valid for the run; NULL takes the empty table of the generic start (an end marker only), or for `images` the field's own. */
typedef struct MsStage {
    char key[MS_KEY_MAX + 1]; /* "<mod>:<name>" */
    int mode;                 /* 0 until msSeal */
    int refused;              /* msRefuse: the stage's files were not valid after the modes were given out; its mode stays taken and runs the fallback */
    int fallbackMode;         /* an original stage's game mode, 0: the default above */
    int fallbackX, fallbackY, fallbackDir; /* where to put the player there, in pixels (used when fallbackMode is set) */

    int textFile;   /* the stage's text file counted without the language (file = LANGUAGE + textFile), -1 or 0 for none */
    int packFile;   /* FieldState.mapFile: the file of the background tiles (a virtual file made by msSetFiles) */
    int layerFile;  /* the TMPK archive: entry 0 the object sheet (FieldState.sheetEntry), 1 floor, 2 triggers, 3 areas (msSetFiles) */
    int imageEntry, imageFile;
    int soundBank, music;
    int startX, startY, startDir; /* pixels and a direction 0-7 */
    int spriteColor[4];           /* r, g, b, cd; cd 0 leaves the sprites uncoloured */
    char bannerArea[MS_KEY_MAX + 1], bannerPlace[MS_KEY_MAX + 1]; /* the area name banner: names "<mod>:<text>" of the two lines, "" for none */
    int bannerAreaEntry, bannerPlaceEntry; /* their entries in the area-name (STAREA) and place-name (STNAME) text files once smLoad appended them, else 0 */

    void *objects;  /* StageTile[], up to the first margin 0 */
    void *slots;    /* StageSlot[], up to the first type 0 */
    void *battles;  /* FieldBattles * */
    void *events;   /* FieldEvent[], up to the first id -1 */
    void *actors;   /* FieldActorEntry *[], up to the first NULL */
    void *images;   /* ActorImage[]: the field image (2 entries) then the actors' */
} MsStage;

/* ---- the registry ---- */

void msReset(void); /* forgets every stage (a reload of the mods); the virtual files are the caller's to clear (pfsClearVirtualFiles) */

/* Registers a stage by key. Returns its zeroed data (the pointer stays valid until msReset), or NULL with the reason in why: a key that is not
   "<mod>:<name>", a key already there, a full table (MS_MODE_COUNT) or a sealed registry. */
MsStage *msRegister(const char *key, char *why, size_t whySize);

/* Assigns the modes, alphabetical by key. Returns the number of stages. A second call changes nothing. */
int msSeal(void);
int msIsSealed(void);
int msCount(void);

MsStage *msByKey(const char *key);   /* NULL when no such stage (a refused one is none) */
MsStage *msByMode(int mode);         /* NULL for a mode that is not a registered stage (and before msSeal) */
MsStage *msAt(int i);                /* in mode order (after msSeal); NULL out of range */

/* A stage whose files turned out not to be valid once the registry was sealed (new-stages T5): it stops being found by key, mode or number, but keeps its mode, so
   the others keep theirs. 1 when a stage was refused. */
int msRefuse(const char *key);

int msIsModMode(int mode);           /* MS_MODE_FIRST..MS_MODE_LAST, registered or not */
int msModeOfKey(const char *key);    /* the mode, or -1 (unknown key, or before msSeal) */
int msNumberOfKey(const char *key);  /* the stage number, or -1 */
int msNumberOfMode(int mode);        /* MS_NUMBER_BASE + the index, or -1 */
int msModeOfNumber(int number);      /* the mode of a stage number made by msNumberOfKey, or -1 */
const char *msKeyOfNumber(int number); /* the key, or NULL */

/* Where the player goes when the stage cannot be entered: the stage's own fallback or the default */
void msFallbackOf(const MsStage *s, int *mode, int *x, int *y, int *dir);

/* ---- virtual files ---- */

/* Adds the stage's two files as virtual files (port_fs.h; pfsInit must have run), TMPK archive first then PACK, and keeps their numbers in
   layerFile and packFile. The bytes are copied. 0, or -1 when a file could not be added (nothing is kept then). */
int msSetFiles(MsStage *s, const uint8_t *layers, size_t layersLen, const uint8_t *pack, size_t packLen);
/* A reference into the stage's TMPK archive, (layerFile << 16) | entry: FieldMap.setFile's and FieldState.sheetEntry's form */
int msLayerRef(const MsStage *s, int entry);

/* ---- the visited flag ---- */

/* "<mod>:visited.<name>" for the key "<mod>:<name>"; 0 when it does not fit */
int msVisitedName(const char *key, char *out, size_t cap);
/* The field of `mode` was entered: for a registered stage sets the variable and returns 1; any other mode returns 0 and does nothing */
int msMarkVisited(int mode);
int msVisited(const char *key); /* 1 when the variable is set */

/* ---- saves ("EXTS", dw3save-v1) ----
   A save never holds a session mode. The game block's fieldMode (where the menus and a loaded game return to) is replaced by the stage's
   fallback, with the fallback's position, and the chunk records the key with the player's real position:
       u32 version (1), u16 name length, the key (no NUL), s32 x, s32 y, s32 dir  (the block's own fieldPos and fieldDir, as they were)
   Little endian. Nothing is written when the block is not in a mod stage. */

/* The chunk's size (0: nothing to record). With `out` and enough `cap`, also rewrites the block `game` (gameLen bytes, GAME_SAVE_SIZE) to the
   fallback and fixes its checksum; with out NULL the block is not touched. */
unsigned long msEncodeSaved(unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap);
/* Puts the stage back in a loaded block: 1 when the key is registered (fieldMode, fieldPos and fieldDir are the stage's again, checksum fixed),
   0 when there is no chunk or the stage is gone (the block keeps the fallback it was saved with; a log line says so for a gone stage), -1 for a
   damaged chunk (the block is left as it is). */
int msRestoreSaved(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len);

#endif
