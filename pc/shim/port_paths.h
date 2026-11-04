/*
 * The one module that knows where the player's files live (ADR-010 section 4). Every other module asks it for a
 * path and keeps no copy of its own, never reads HOME / XDG_* / APPDATA and never writes a separator by hand.
 *
 * The user folder (base), in the order portPathsBase() tries:
 *     $DW3_USER_DIR           a harness's scratch folder (the whole tree moves with it)
 *     portable mode           a portable.txt next to the executable: the executable's folder
 *     the platform's folder   SDL_GetPrefPath("dw3port", "dw3") (shim/port_paths_sdl.c: the single place with a
 *                             per-platform branch; on Android it is the app's external files folder)
 *
 *     <base>/settings.ini  saves/<profile>/  import/  mods/  data/  logs/  screenshots/  cache/
 *
 * Each of those has an environment override that points at the item itself (DW3_SETTINGS is the settings FILE, the
 * others are folders): DW3_SETTINGS, DW3_SAVES_DIR (the profile folder), DW3_IMPORT_DIR, DW3_MODS_DIR, DW3_DATA_DIR,
 * DW3_LOGS_DIR, DW3_SCREENSHOTS_DIR, DW3_CACHE_DIR; DW3_MEMCARD (one PS1 card to import, instead of the import/
 * folder) and DW3_L10N_DIR (the es-419 overlay). Results never end in a separator.
 *
 * Pure standard C (the platform part is a weak hook that port_paths_sdl.c overrides), so the host tests link it.
 */
#ifndef PORT_PATHS_H
#define PORT_PATHS_H

#include <stddef.h>

#define PP_CAP 1024 /* the size every path buffer of this module uses */

typedef enum PortDir {
    PP_SETTINGS = 0, /* the settings FILE <base>/settings.ini */
    PP_SAVES,        /* the profile folder <base>/saves/default */
    PP_IMPORT,       /* <base>/import */
    PP_MODS,         /* <base>/mods */
    PP_DATA,         /* <base>/data */
    PP_LOGS,         /* <base>/logs */
    PP_SCREENSHOTS,  /* <base>/screenshots */
    PP_CACHE,        /* <base>/cache */
    PP_DIR_COUNT
} PortDir;

#define PP_DEFAULT_PROFILE "default"

/* ---- pure choices (the tests drive these with explicit inputs) ---- */

/* Portable mode keeps the files in the executable's folder, otherwise the platform's. Both end in a separator.
   0 when there is none. */
int portPathsChoosePrefDir(const char *exeDir, int portable, const char *pref, char *out, int cap);

/* The path of one item under `base` (NULL or "": no base) unless `override` is set. No trailing separator. 0 if neither
   exists or it does not fit. */
int portPathsResolveWith(PortDir d, const char *base, const char *override, char *out, int cap);

/* Android's user folder: the app's external files folder (what a PC reaches over USB as Android/data/<id>/files) when
   there is one, else the internal one. Ends in a separator. 0 when neither exists or it does not fit. */
int portPathsAndroidBase(const char *external, const char *internal, char *out, int cap);

/* The environment variable that overrides an item */
const char *portPathsEnvName(PortDir d);

/* ---- the live answers ---- */

/* The user folder, with a trailing separator; NULL when there is none */
const char *portPathsBase(void);
int portPathsIsPortable(void);
/* The executable's folder (trailing separator); NULL when unknown */
const char *portPathsExeDir(void);

int portPath(PortDir d, char *out, int cap);                                   /* 1 when resolved */
int portPathIn(PortDir d, const char *leaf, char *out, int cap);              /* <item>/<leaf> */
int portPathsViewDir(char *out, int cap);                                     /* <saves>/view */
int portPathsL10nCandidate(int i, const char *env, const char *exeDir, const char *userDir, char *out, int cap); /* pure: place i to look */
int portPathsL10n(char *out, int cap);                                        /* the es-419 overlay folder, 0 when none */

/* The PS1 cards to import, in order (full paths): $DW3_MEMCARD alone when it is set (it need not exist yet),
   else every *.mcd of import/ by name, case-insensitively. Returns how many (at most `max`). */
int portPathsImportCards(char out[][PP_CAP], int max);
/* Folders where emulators keep their PS1 cards (DuckStation, PCSX, ePSXe, Mednafen, RetroArch), the ones that exist on this machine,
   for the in-game card import. $DW3_EMU_DIRS (a list separated by ':' or ';') replaces the built-in guesses (harnesses, and a
   way to point at an emulator the guesses miss). Returns how many (at most `max`). */
int portPathsEmulatorDirs(char out[][PP_CAP], int max);
/* The "already imported" record: imported.txt next to the first card (so a harness's card keeps the record in its own
   folder), else in import/ */
int portPathsImportMarker(char *out, int cap);

/* Creates the user folder's tree that the port always has: saves/<profile>, import, mods, data, logs (and the
   settings folder). Returns 1 when every folder is there. */
int portPathsEnsureTree(void);

/* Forgets the cached base (the tests' hook, after changing the environment) */
void portPathsReset(void);

/* ---- file helpers ---- */

int portJoin(char *out, size_t cap, const char *dir, const char *leaf); /* dir/leaf, one separator; 1 when it fits */
int portMkdirs(const char *dir);                                       /* mkdir -p, either separator; 1 when it exists afterwards */
int portMkdirsFor(const char *file);                                   /* mkdir -p of the file's folder */
int portIsDir(const char *path);
int portIsFile(const char *path);

/* root/rel with `rel` ('/'-separated) matched case-insensitively component by component (an exact match wins):
   1 and the real path in `out` when the file exists, else 0. This is how mods and data are read, so a mod written
   with upper-case disc names on Windows still works on Linux and Android. */
int portPathsResolveCI(const char *root, const char *rel, char *out, size_t cap);

/* The platform hook (weak here): the executable's folder and the platform's per-user folder, both with a trailing
   separator. prefCap 0 means "do not ask for the per-user folder". Returns 1 when `pref` is usable. port_paths_sdl.c
   defines it. */
int portPathsPlatform(char *exeDir, int exeCap, char *pref, int prefCap);

#endif
