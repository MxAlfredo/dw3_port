#ifndef PC_MODS_H
#define PC_MODS_H

/*
 * Mods and the small JSON reader of the port's data files (ADR-005, feature stage-banner T2).
 *
 * A mod is a folder <pref>/mods/<mod>/ with a valid mod.json ("format": "mod-v1"). The port never
 * writes in mods/. The active mods and their order: `order = a, b` in the [mods] section of
 * settings.ini (only the listed mods are active; the last one wins); without it, every valid mod in
 * alphabetical order. `disabled = a, b` turns mods off (the in-game Mods tab writes it): they are skipped even when listed in
 * `order`, and new mods stay on. A mod that is listed but missing or invalid, or a folder with a bad mod.json,
 * is logged (`mods: <path> rejected: <reason>`) and skipped; the game never stops for a mod.
 *
 * mod.json (mod-v1, grows compatibly, ADR-006 section 5): "format" (required), "name", "version"
 * (digits and dots, "1.2"), "kind" ("assets", the default, or "content"), "port" (the port version it
 * needs, ">=0.4"), "requires" (a list of "<folder name>" or "<folder name>>=1.2"). Other keys are ignored.
 * The active list is then put in dependency order (a dependency loads before what needs it, the rest keeps
 * the settings/alphabetical order) and a mod whose port is too old, whose dependency is missing, disabled or
 * too old, or that is in a dependency cycle, is disabled with a log line (`mods: <name> disabled: <why>`).
 */

/* most mods one run holds; the scan logs `rejected: more than N mods` for each one past it */
#define MODS_MAX 256

/* Scan `root` (NULL: no mods) and fix the active list. `order` is the raw text after `order =` or
   NULL; an entry written "-name" is a mod turned off (inputCfgReadModsOrder
   joins [mods] disabled into the text that way), and a text of only "-name" entries keeps every other mod on in
   alphabetical order. Returns how many mods are active. Logs one summary line on stderr. */
int modsInit(const char *root, const char *order);

int modsCount(void);
/* Mods are read ONCE, at startup; a file edited, added or removed while the game runs is seen after a restart. A modder who wants the
   old live look (every art, tile and file source re-checked about once a second, which costs a lot of file probing) sets
   DW3_MODS_HOTRELOAD=1. Off by default. modsSetHotReload is for the tests (-1: back to the environment). */
int modsHotReload(void);
void modsSetHotReload(int on);
const char *modsNameOf(int i);  /* folder name of the i-th active mod, in order (last wins) */
const char *modsDirOf(int i);   /* its path, no trailing slash */
const char *modsVersionOf(int i); /* manifest "version" ("0" when absent) */
const char *modsKindOf(int i);    /* "assets" (default) or "content" */

/* ---- the report of the last modsInit (--list-mods, --validate-mod) ---- */
enum { MODS_REP_ACTIVE, MODS_REP_DISABLED, MODS_REP_REJECTED, MODS_REP_NOT_LISTED, MODS_REP_MISSING };
typedef struct ModsReport {
    char name[64];     /* the folder name */
    char dir[1100];    /* its path ("" for MODS_REP_MISSING) */
    char version[24];  /* "0" when absent or the mod was rejected */
    char kind[8];
    int status;        /* MODS_REP_*: active, disabled (reason), rejected (bad mod.json: reason), valid but not in the order, listed but absent */
    int position;      /* 1-based place in the load order (the last one wins) for MODS_REP_ACTIVE, else 0 */
    char reason[200];
    int byUser;        /* 1 when the player turned it off with [mods] disabled (status MODS_REP_DISABLED) */
} ModsReport;
/* Every folder the scan found (sorted by name, rejected ones included) and every name the order lists that is absent */
int modsReportCount(void);
const ModsReport *modsReportAt(int i);

/* Compare two versions (digits and dots, a "-suffix" ignored): -1, 0 or 1 */
int modsVersionCompare(const char *a, const char *b);

/* ---- strict JSON reader for flat objects ---- */

enum { PJ_NUM, PJ_BOOL, PJ_STR, PJ_ARR, PJ_OBJ, PJ_SARR };
/* members per object and numbers per array; one more is a parse error ("too many keys", "array too long"), logged by the caller */
#define PJ_MAX_KEYS 64
#define PJ_MAX_ARR 64

typedef struct PjVal {
    char key[40];
    int kind;
    double num;         /* PJ_NUM, PJ_BOOL (0 or 1) */
    char str[256];      /* PJ_STR; PJ_SARR: the strings of a list of strings, one per line (arrCount of them) */
    int arr[PJ_MAX_ARR];/* PJ_ARR: whole numbers only */
    int arrCount;
} PjVal;

/* One object of numbers, booleans, strings, arrays of whole numbers and (skipped) nested objects of
   scalars. Strict: no trailing commas, no duplicate keys, nothing after the object. Returns how many
   members were stored in out[] (at most `max`), or -1 with a reason in err. A PJ_OBJ member keeps no
   content (the "notes" of the config files). */
int pjParseFlat(const char *text, unsigned long len, PjVal *out, int max, char *err, unsigned long errSize);
const PjVal *pjFind(const PjVal *vals, int n, const char *key);

#endif
