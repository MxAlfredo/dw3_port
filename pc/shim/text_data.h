#ifndef PC_TEXT_DATA_H
#define PC_TEXT_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "text_reflow.h"

/*
 * Text data folder and runtime override layer (feature es419-live-option, L2b).
 *
 * The EU disc has each of the 50 text files once per language, in rows of 7 file table slots:
 *     index = slot + 7k,  slot 1 JPN, 2 USA (unused by the EU game), 3 ENG, 4 FRA, 5 ITA,
 *                         6 GER, 7 SPN  (= TEXT_FILE(): LANGUAGE + (2 + 7k) - 1, LANGUAGE 2..6)
 * k follows the disc order of the files (tdFileBase), not the alphabetical one.
 * The port uses the five languages the EU game selects (LANGUAGE 2..6) plus es-419 (the Spanish
 * files with localization/es-419 applied); JPN and USA are never read by TEXT_FILE() on this disc.
 *
 * Layout (<data> = DW3_DATA_DIR, else <user folder>/data; names in lower case, ADR-010):
 *     <data>/text/manifest.json            stamp + content hash of every generated file
 *     <data>/text/<lang>/<file>.json       generated, whole files (lang: eng fra ita ger spa es-419)
 *     <user folder>/mods/<mod>/text/<lang>/<file>.json  a mod's overrides (ADR-005), in mod order, last wins, the
 *                                          name in any case; mods win over everything else
 *
 * The first half of this unit (index map, hash, manifest, decisions, build) is pure stdlib and
 * is covered by pc/texttest.c. The second half (tdStart / tdOverride) does the file work.
 */

#define TD_CODEC_VERSION 2 /* bump when the JSON the codec writes changes (2: the L4a reflow) */

enum { TD_ENG, TD_FRA, TD_ITA, TD_GER, TD_SPA, TD_ES419, TD_LANGS };
#define TD_REAL_LANGS 5 /* the languages that exist on the disc */
#define TD_FILES 50
#define TD_MAX_INDEX (7 * (TD_FILES - 1) + 7) /* the highest text file index (SPN of k 49) */

/* ---- index map ---- */
const char *tdLangDir(int lang);   /* folder name: eng fra ita ger spa es-419 */
const char *tdDiscDir(int lang);   /* the disc folder under AAA/DAT/COUNTRY (es-419: SPN) */
const char *tdFileBase(int k);     /* "AMTMAP" */
int tdFileFromBase(const char *base); /* the inverse; -1 for an unknown name */
/* "ESAMTMAP.BIN": language letter + "S" + base (es-419 uses the Spanish names) */
int tdFileName(int lang, int k, char *buf, size_t cap);
int tdIndexOf(int lang, int k);    /* file table index; es-419 has the Spanish one */
/* The reverse: 1 and the disc language (TD_ENG..TD_SPA) and k when `index` is a text file the
   EU game reads; 0 for anything else (JPN/USA slots included) */
int tdFromIndex(int index, int *lang, int *k);

/* ---- hash (FNV-1a, 64 bit: change detection, not security) ---- */
uint64_t tdHash(const void *data, size_t len);
uint64_t tdHashUpdate(uint64_t h, const void *data, size_t len);
#define TD_HASH_INIT 0xcbf29ce484222325ull
void tdHashHex(uint64_t h, char out[17]);

/* ---- manifest ---- */
typedef struct TdManifestFile {
    char path[72];  /* "<lang>/<FILE>.json" */
    char hash[17];
} TdManifestFile;

typedef struct TdManifest {
    char stamp[256];
    int count;
    TdManifestFile *files;
} TdManifest;

/* "disc=<hash>;overlay=<hash|none>;codec=<n>" */
void tdMakeStamp(char *buf, size_t cap, const char *discHash, const char *overlayHash);
int tdManifestParse(const char *json, size_t len, TdManifest *m, char *err, size_t errSize);
char *tdManifestFormat(const TdManifest *m); /* malloc'ed */
const char *tdManifestFind(const TdManifest *m, const char *path); /* hash or NULL */
int tdManifestSet(TdManifest *m, const char *path, const char *hash);
void tdManifestFree(TdManifest *m);

/* ---- decisions ---- */
typedef enum TdFileState {
    TD_FILE_MISSING,  /* no file */
    TD_FILE_PRISTINE, /* the file is what we generated (hash == manifest) */
    TD_FILE_EDITED    /* the user's: hash differs, or the manifest does not know it */
} TdFileState;
TdFileState tdFileState(int exists, const char *manifestHash, const char *currentHash);

typedef enum TdExtractAction {
    TD_X_SKIP,      /* nothing to do */
    TD_X_WRITE,     /* write the file and record its hash */
    TD_X_WRITE_NEW  /* the user's file stays; write <FILE>.json.new beside it */
} TdExtractAction;
TdExtractAction tdExtractAction(int stampMatches, TdFileState state);

typedef enum TdSource {
    TD_SRC_DISC,
    TD_SRC_MOD,
    TD_SRC_DATA,
    TD_SRC_ES419
} TdSource;
/* The sources to try, in priority order (the disc is implicit after the last): mods, then an
   edited data file, then the es-419 merge. Returns how many were written to out[3]. */
int tdSourceOrder(int modExists, TdFileState dataState, int es419Active, TdSource out[3]);

/* ---- build ---- */

/* Apply `json` onto the lang file/pack `base`. requireFull (a user file): the JSON must list
   exactly the base's entries (same count, each entry once, all in range), else it is refused.
   Overlays pass 0. On success *out is the rebuilt file (malloc'ed). On failure -1 and err names
   the part/index or the byte position. */
int tdBuild(const uint8_t *base, size_t baseLen, const char *json, size_t jsonLen, int requireFull,
            uint8_t **out, size_t *outLen, char *err, size_t errSize);

/* The same, plus the automatic reflow (L4a) of the entries that differ from `base`: `fileBase` is the
   file's name without letters ("TALK03"), looked up in the layout; `label` names it in the log.
   Without a configured layout (or a rule for the file) it is tdBuild. */
int tdBuildFile(const char *fileBase, const char *label, const uint8_t *base, size_t baseLen, const char *json,
                size_t jsonLen, int requireFull, uint8_t **out, size_t *outLen, char *err, size_t errSize);

/* The budgets of localization/layout.json. `metrics` NULL: the game's own glyph tables. Returns 0, or
   -1 (the reflow stays off: bad layout, no metrics). tdStart calls it with the file next to the overlay
   folder; DW3_TEXT_REFLOW=0 keeps it off. */
int tdReflowConfigure(const char *layoutJson, size_t len, const TrOptions *metrics);
void tdReflowOff(void);

/* L4b: the widest talk box the player chose ([display] talk_box_width): 139 (retail, the default) up
   to 280. The reflow budget of the talk-box entries follows it (the stamp too), so the wrap and the
   box agree. A value under 139 is ignored. tdStart takes it from DW3_TALK_BOX_WIDTH, else from the
   settings file, before it configures the reflow. */
void tdSetTalkBoxWidth(int px);
int tdTalkBoxWidth(void);
int tdReflowActive(void);

/* ---- file helpers shared with the port's config and mods layers (pure stdlib) ---- */

/* Whole file into a malloc'ed buffer with a NUL after the data; 0 on success */
int tdReadFile(const char *path, uint8_t **data, size_t *len);
/* mkdir -p; 0 on success */
int tdMakeDirs(const char *path);
/* Write <path>.tmp then rename over <path>; 0 on success */
int tdWriteFileAtomic(const char *path, const void *data, size_t len);

/* The mod files of one text file (language `lang`, file `k`) that exist, the winning one first: the active
   mods (mods.h) from the last to the first (the file name matched without regard to case). *mix is the sum of their mtimes. tdStart's data folder, or tdSetDataDir, names <data>. */
#define TD_MOD_FILES_MAX 33
int tdModFiles(int lang, int k, char paths[][1200], long *mix);
void tdSetDataDir(const char *dataDir); /* tests: what tdStart does with its dataDir argument */

/* ---- runtime ---- */

enum { TD_VARIANT_SPA = 0, TD_VARIANT_ES419 = 1 };
void tdSetVariant(int variant);
int tdGetVariant(void);
/* 1 when the es-419 variant can be served: the disc is open and sized, and the overlay folder exists
   (after tdStart; 0 before it) */
int tdEs419Available(void);

/* How the runtime reaches the disc */
typedef struct TdDisc {
    void *ctx;
    int (*readSector)(void *ctx, long lba, uint8_t *out2048);        /* 1 on success */
    int (*locate)(void *ctx, int index, long *lba, int *sectors);   /* 1 on success */
} TdDisc;

/* Resolve the data folder and the extraction: checks the stamp, extracts when needed (log with
   timings on stderr), loads the manifest. dataDir / l10nDir may be NULL (no folder: the layer
   still serves es-419 from memory when l10nDir is set). Fail soft: returns the number of files
   written, 0 when nothing was needed, -1 when something failed (the game runs from the disc). */
int tdStart(const TdDisc *disc, const char *dataDir, const char *l10nDir);

/* The bytes to serve for file `index` in place of the disc, or NULL for the disc. `refresh`:
   re-check the source files' mtime (throttled to once a second). Valid until the next refresh. */
const uint8_t *tdOverride(int index, size_t *len, int refresh);

/* Extra item texts (extended-state T3): the item name file (ITMNAM) and the description file (ITMINF) are indexed by
   item id, so the ids the port gives new items (403 up, item_ext.c) need entries there. When count() > 0 every served
   copy of those two files (any language, with or without a mod's text) gets entries 403 .. 403+count()-1 appended
   (padding below 403 with empty strings); text(i, langDir, desc) is the string of new item i (desc 0 name, 1
   description) in a language folder ("eng", "spa", "es-419"...). NULL, NULL removes the hook. */
/* The same for cards (U8b): CARDNM (name) and CARDST (effect text) get entries from id 317 (PC_CARD_ORIG_COUNT) up */
void tdSetCardTextHook(int (*count)(void), const char *(*text)(int index, const char *langDir, int desc));
void tdSetItemTextHook(int (*count)(void), const char *(*text)(int index, const char *langDir, int desc));

/* The same for techniques (extended techs T2): the technique name file (SKLNAM) and description file (SKLINF) are indexed by
   technique id, so the ids the port gives new techniques (444 up, tech_ext.c) get entries there (SKLINF has fewer than 444
   entries on the disc: the gap is padded with empty strings). The description of a new technique is fitted to one
   line (280 px) like an item's. NULL, NULL removes the hook. */
void tdSetTechTextHook(int (*count)(void), const char *(*text)(int index, const char *langDir, int desc));

/* The same for the names of new Digimon (digimon_ext.c): the Digimon name file (DIGNAM) is indexed by nameId, and the port gives
   a new enemy or new partner form the entry 251 + its index (251 entries on the disc, in every language), so entries
   251 .. 251+count()-1 are appended. A partner form's entry does not fit its u8 nameId: the game reads it through
   PC_DIGIMON_NAME (engine/digimon_access.h). The description file (DIGINF, shown by the lab for a form; the layout rule keeps
   its own line breaks) gets the same entries. NULL, NULL removes the hook. */
void tdSetDigimonTextHook(int (*count)(void), const char *(*text)(int index, const char *langDir, int desc));

/* Named texts for the stage talks (port-close S6, stage_mods.c): the talk files of the field stages (TALKnn) are indexed by entry, and a stage patch's
   `"text": "<mod>:<name>"` needs an entry of its own, so every copy of a talk file served (any language, with or without a mod's text) gets
   count(k) entries appended after the file's original ones; text(k, i, langDir) is the string of appended entry i of file k (readable syntax, as
   a mod's text files hold it) in a language folder ("eng", "spa", "es-419"...). The entries are fitted to the talk boxes with the same reflow as a mod's
   own text. k is the file number of tdFileBase (the 50 files of the disc). NULL, NULL removes the hook. */
void tdSetStageTextHook(int (*count)(int k), const char *(*text)(int k, int i, const char *langDir));
/* The k of a stage's text file as SdStage.textFile counts it (the file without the language: file = LANGUAGE + textFile); -1 when it is not a
   text file of the disc */
int tdStageTextK(int textFile);
/* The entries file k has on the disc (the number a stage's appended entries start at); -1 when it cannot be read or is a pack */
int tdStageTextFirst(int k);
/* The pure half of that (texttest): `file` is a lang file of `len` bytes; `count` entries are appended after its first `first` (a file with fewer is padded with
   empty entries, one with more is refused, as is a pack); text(ud, i) is appended entry i in the readable syntax, reflowed to fileBase's layout rule (the
   talk box) when there is one. On success *out is the new file (malloc'ed). */
int tdAppendTexts(const uint8_t *file, size_t len, const char *fileBase, int first, int count, const char *(*text)(void *ud, int i), void *ud,
                  uint8_t **out, size_t *outLen, char *err, size_t errSize);
/* The copies of file k that were served are rebuilt on the next read (the hook's count changed) */
void tdStageTextInvalidate(int k);

/* Fits a one-line text to maxWidth px with the reflow metrics (tdReflowConfigure): copies it to out when it fits (returns
   0); else the longest prefix cut at a word, or at a character when no word fits, plus "..." (returns 1). -1: no metrics
   (reflow not configured) or out too small; out is then untouched. The description of a new item is served through it
   with the ITMINF budget of layout.json (280 px). */
int tdFitLine(const char *text, int maxWidth, char *out, size_t cap);

/* DW3_TEXT_TRACE=1: log what every text file read was served from */
int tdTraceEnabled(void);
void tdLogServed(int index, const char *source, const void *data, size_t len);

#endif
