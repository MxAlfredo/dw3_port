#ifndef PC_MOD_TOOLS_H
#define PC_MOD_TOOLS_H

/*
 * The tools that make modding easy (feature modding-ux, U5): command-line flags that run without the game.
 *
 *   --list-mods                      what is installed: version, kind, enabled or disabled (and why), load position
 *   --validate-mod <name|path>       run the real parsers over one mod without starting the game; one line per problem
 *   --validate-mods                  the same for every installed mod
 *   --new-mod <name> [--kind <k>]    create <pref>/mods/<name>/ with a mod.json, a README.txt and empty folders
 *
 * Nothing here has parsers of its own for the mod's data: --validate-* runs the loaders the game runs (mod.json and the order in
 * mods.c, the table patches in table_mods.c, the content files, the image/audio/video/stage checks) with the log captured, and turns
 * the log lines that report a refusal into problems. What this unit adds on its own is what a loader silently skips: a folder or a
 * file name the game never looks at, an unknown key of mod.json, a table or content file that names nothing, a raw file the disc does
 * not have. A layer that cannot be checked without a running game is reported as "not checked", never guessed.
 *
 * Pure stdlib plus mods.h, table_mods.h and the file helpers of text_data.h, so configtest runs it on synthetic mods; the boot binary
 * supplies the game side through ModToolsLayers.
 */

#include <stdio.h>

typedef enum ModToolsKind { MT_NONE = 0, MT_LIST, MT_NEW, MT_VALIDATE, MT_VALIDATE_ALL } ModToolsKind;

typedef struct ModToolsCmd {
    int kind;         /* ModToolsKind */
    char name[300];   /* --validate-mod target (a mod name or a folder path) or --new-mod name */
    char kinds[96];   /* --kind value, "" for the default */
    char err[200];    /* non-empty: the command line is wrong (a flag without its value, --kind without --new-mod) */
} ModToolsCmd;

/* Takes the tool flags out of argv (any position, the rest keeps its order). 1 when one was found; cmd->err says what is wrong. */
int modToolsTakeArgs(int *argc, char **argv, ModToolsCmd *cmd);

/* --validate-mod <path>: a folder path gives the mods folder (its parent) and the mod's name; a plain name gives root "" (the
   preferences' mods folder). Returns 1 when `target` is a path. */
int modToolsSplitTarget(const char *target, char *root, size_t rootCap, char *name, size_t nameCap);

/* --list-mods: prints the report of the last modsInit. Returns 0. */
int modToolsList(const char *root, FILE *out);

/* --new-mod: creates <root>/<name>/. kinds: "" (the default, assets), or a list ("content", "tables,images", "assets") of: assets, content
   and the folder names config tables text images audio video files. Never touches an existing folder. Returns 0, or 1 with the reason
   printed on `err`. */
int modToolsNew(const char *root, const char *name, const char *kinds, FILE *out, FILE *err);

/* The files the template creates, for the test that keeps pc/examples/mods/_template/ in step: name 0..n-1, its text
   (@NAME@ and @KIND@ stand for the mod's name and kind). NULL past the end. */
const char *modToolsTemplateFile(int i, const char **text);

/* ---- validation ---- */

typedef struct ModToolsLayers {
    /* Runs the game's own loaders over the active mods (tables registered and applied, images, audio, video, stages...). Logs go to
       stderr, which is captured. NULL: nothing beyond the mods layer. */
    void (*run)(void *ud);
    /* 1 when the disc has the file ("AAA/DAT/Z_STAGE/S330PACK.BIN", any case). NULL: the files/ folder is not checked. */
    int (*discHas)(void *ud, const char *discPath);
    /* The folders `run` checks, comma separated ("config,content,tables,images,audio,video,stages"). A folder a mod has that is not in
       the list is reported as not checked. */
    const char *covered;
    void *ud;
    /* What a sprites/<name> folder is on this disc (A7.5): 0 a new sprite (no original has that name), 1 an original sprite (its NNN.png frames replace the
       original's), 2 the follower of an original partner (partners/<name>/field/ holds it, not sprites/). NULL: every folder is taken as a new sprite. */
    int (*spriteKind)(void *ud, const char *folder);
    /* 1 when partners/<name> is an original partner's folder on this disc (the slug of its English name): its partner.json is never read (A8.2). NULL: none is. */
    int (*partnerOriginal)(void *ud, const char *name);
    /* 1 when `name` is the folder name of one of the gym's twelve trainings on this disc (partners/<key>/training/<name>/, the slug of the training's English name). NULL: every
       name is taken as one. */
    int (*partnerTraining)(void *ud, const char *name);
    /* 1 when data/partners/<key>/ holds a file the user changed (its hash is not the one partners/manifest.json recorded): that folder is a layer of the partner's art
       like a mod's (A8.4). NULL: none is. */
    int (*partnerDataEdited)(void *ud, const char *key);
} ModToolsLayers;

/* Start capturing stderr (before modsInit and the loaders); modToolsValidate stops it. A second call does nothing. */
void modToolsCaptureBegin(void);

/* Stops a capture and throws the text away (--list-mods: the scan's log lines are not the listing) */
void modToolsCaptureEnd(void);

/* Validates the mods the last modsInit saw (all, or only `target`: a name, as modToolsSplitTarget gave it), after running
   layers->run, and prints the result to `out`. Returns 0 (no errors), 1 (errors) or 2 (no such mod). */
int modToolsValidate(const char *target, const ModToolsLayers *layers, FILE *out);

#endif
