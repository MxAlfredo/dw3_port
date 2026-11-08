/* The packer of the stage objects by piece (readable-assets A6.3): see stage_obj_pack.h. */
#include "stage_obj_pack.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "image_data.h"
#include "menu_pack.h"
#include "mods.h"
#include "port_log.h"
#include "stage_obj_export.h"
#include "text_data.h"

#define TAG "stage objects"
#define TOPIC "stages"
#define MAX_STAGE 1000 /* stage numbers have three digits */

static unsigned char sEdited[MAX_STAGE]; /* stage number -> some layer has an edited piece for it */
static StageObjPackStats sTotals;

/* ---- the index: which stages have edited pieces ---- */

static int sameName(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

static int isPng(const char *name) {
    size_t n = strlen(name);

    return n > 4 && sameName(name + n - 4, ".png");
}

/* "s202" or "S202" -> 202, else -1 */
static int stageFolderNumber(const char *name) {
    int i, num = 0;

    if (strlen(name) != 4 || tolower((unsigned char)name[0]) != 's') return -1;
    for (i = 1; i <= 3; i++) {
        if (!isdigit((unsigned char)name[i])) return -1;
        num = num * 10 + (name[i] - '0');
    }
    return num;
}

/* the folder called `want` (any case) inside `dir`: 1 and its path */
static int subDir(const char *dir, const char *want, char *path, size_t cap) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;

    if (d == NULL) return 0;
    while (!found && (e = readdir(d)) != NULL) {
        found = sameName(e->d_name, want) && snprintf(path, cap, "%s/%s", dir, e->d_name) < (int)cap;
    }
    closedir(d);
    return found;
}

/* Calls `visit` for every PNG in the objects folder of every stage folder of `root` (root/s<nnn>/objects/). */
typedef void (*PngVisit)(void *ctx, int stage, const char *stageName, const char *objectsDir, const char *png);

static void forEachPng(const char *root, PngVisit visit, void *ctx) {
    char stageDir[1600], dir[1700];
    DIR *d = opendir(root), *o;
    struct dirent *e, *f;

    if (d == NULL) return;
    while ((e = readdir(d)) != NULL) {
        int num = stageFolderNumber(e->d_name);

        snprintf(stageDir, sizeof stageDir, "%s/%s", root, e->d_name);
        if (num < 0 || !subDir(stageDir, "objects", dir, sizeof dir) || (o = opendir(dir)) == NULL) continue;
        while ((f = readdir(o)) != NULL) {
            if (isPng(f->d_name)) visit(ctx, num, e->d_name, dir, f->d_name);
        }
        closedir(o);
    }
    closedir(d);
}

/* ---- a mod's folder: any PNG counts (whether it differs from the disc is the plan's to find out) ---- */

static void markModPng(void *ctx, int stage, const char *stageName, const char *objectsDir, const char *png) {
    (void)ctx;
    (void)stageName;
    (void)objectsDir;
    (void)png;
    sEdited[stage] = 1;
}

/* ---- the data folder: only a PNG whose hash is not the one the exporter recorded counts ----
   Reading and hashing the 8632 PNGs of the whole game costs about 29 ms at every start (measured, A6.4), against about 7 ms to stat them, so the folder is first
   compared with a signature of what it looked like the last time it was found clean: the name, size and time of each PNG. Only when that differs is every file
   hashed, and a clean result is remembered. A file edited in place changes its time or size, so it never passes for clean. */

typedef struct DataScan {
    uint64_t signature; /* the sum over the PNGs: the order they are read in does not matter */
    int files;
    int edited;         /* some PNG is the user's (only filled by the slow check) */
} DataScan;

static void sign(void *ctx, int stage, const char *stageName, const char *objectsDir, const char *png) {
    DataScan *s = (DataScan *)ctx;
    char path[1900];
    struct stat st;
    uint64_t h;

    (void)stage;
    (void)stageName;
    snprintf(path, sizeof path, "%s/%s", objectsDir, png);
    if (stat(path, &st) != 0) return;
    h = tdHash(path, strlen(path));
    h = tdHashUpdate(h, &st.st_size, sizeof st.st_size);
    h = tdHashUpdate(h, &st.st_mtime, sizeof st.st_mtime);
#if defined(__linux__) || defined(__ANDROID__)
    h = tdHashUpdate(h, &st.st_mtim.tv_nsec, sizeof st.st_mtim.tv_nsec); /* the rest of the second: two edits within one still differ */
#endif
    s->signature += h;
    s->files++;
}

static void checkHash(void *ctx, int stage, const char *stageName, const char *objectsDir, const char *png) {
    DataScan *s = (DataScan *)ctx;
    char path[1900], rel[160];
    uint8_t *bytes;
    size_t len;

    if (sEdited[stage]) return;
    snprintf(path, sizeof path, "%s/%s", objectsDir, png);
    snprintf(rel, sizeof rel, "%s/objects/%s", stageName, png);
    if (tdReadFile(path, &bytes, &len) != 0) return;
    if (menuPackDataFileEdited(TAG, TOPIC, rel, bytes, len)) {
        sEdited[stage] = 1;
        s->edited++;
    }
    free(bytes);
}

static void indexData(const char *root) {
    char stampPath[1500], text[32];
    DataScan now;
    uint8_t *old;
    size_t oldLen;
    int clean;

    memset(&now, 0, sizeof now);
    forEachPng(root, sign, &now);
    if (now.files == 0) return;
    snprintf(text, sizeof text, "%016llx", (unsigned long long)now.signature);
    snprintf(stampPath, sizeof stampPath, "%s/objects.stamp", root);
    if (tdReadFile(stampPath, &old, &oldLen) == 0) {
        clean = oldLen == strlen(text) && memcmp(old, text, oldLen) == 0;
        free(old);
        if (clean) return;
    }
    forEachPng(root, checkHash, &now);
    if (now.edited == 0) tdWriteFileAtomic(stampPath, text, strlen(text)); /* found clean: the next start only compares the signature */
}

static void indexLayer(const AssetLayer *layer) {
    char root[1400];

    if (layer->mod >= 0) {
        if (assetModDir(layer->mod, NULL, TOPIC, root, sizeof root)) forEachPng(root, markModPng, NULL);
    } else {
        snprintf(root, sizeof root, "%s/" TOPIC, layer->root);
        indexData(root);
    }
}

/* ---- the hooks of the image layer ---- */

/* The stage's MASK file `file` and its TMPK file `tmpk` (the hook's sheet): the plan of the pieces, when the stage has edited ones. The sheet is the TMPK entry
   that the stage data names (not the mod's objects.json). */
static int hookBegin(const char *discPath, const uint8_t *file, size_t len, const uint8_t *tmpk, size_t tmpkLen) {
    int stage = stageObjNumber(discPath), ref, rc;
    char name[8], dir[16];
    uint8_t *sheet;
    size_t sheetLen = 0;
    PackSource src = {TAG, TOPIC, name, dir, 1};

    if (stage < 0 || stage >= MAX_STAGE || !sEdited[stage] || tmpk == NULL) return 0;
    snprintf(name, sizeof name, "s%03d", stage);
    snprintf(dir, sizeof dir, "%s/objects", name);
    if (assetDataDir() == NULL || stageObjSheetRefIn(assetDataDir(), stage, &ref) != 0 || ref == 0) {
        plogLine(TAG, "%s: its pieces are ignored: no stage data (stages/s%03d/stage.json) names the entry of its TMPK file that is the sprite sheet\n", name, stage);
        return 0;
    }
    sheet = stageObjSheetFile(tmpk, tmpkLen, ref & 0xFFFF, &sheetLen);
    if (sheet == NULL) {
        plogLine(TAG, "%s: its pieces are ignored: entry %d of its TMPK file is not a sprite sheet\n", name, ref & 0xFFFF);
        return 0;
    }
    sTotals.built++;
    rc = menuPackBegin(&src, file, len, sheet, sheetLen);
    free(sheet);
    return rc;
}

static int hookPatch(const char *discPath, const int *chain, int depth, uint8_t *tim, size_t len) {
    (void)discPath;
    return menuPackPatch(chain, depth, tim, len);
}

static void hookEnd(const char *discPath) {
    (void)discPath;
    menuPackEnd();
}

void stageObjPackStart(void) {
    ImgMenuHooks h;
    int i, s;

    memset(sEdited, 0, sizeof sEdited);
    memset(&sTotals, 0, sizeof sTotals);
    for (s = 0; s < assetLayerCount(); s++) {
        AssetLayer l;

        if (assetLayerAt(s, &l)) indexLayer(&l);
    }
    for (i = 0; i < MAX_STAGE; i++) sTotals.edited += sEdited[i];
    if (sTotals.edited > 0) plogLine(TAG, "%d stage(s) have edited pieces; the others are not looked at\n", sTotals.edited);
    memset(&h, 0, sizeof h);
    h.begin = hookBegin;
    h.patch = hookPatch;
    h.end = hookEnd;
    imgSetStageHooks(&h);
}

/* ---- --validate-mod ---- */

static int isDir(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void validateStageFolder(const char *folder) {
    DIR *d = opendir(folder);
    struct dirent *e;

    if (d == NULL) return;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.' && !sameName(e->d_name, "objects") && !sameName(e->d_name, "tiles") && !sameName(e->d_name, "stage.json")) {
            plogLine(TAG, "%s/%s ignored: only objects/, tiles/ and stage.json of a stage folder are read\n", folder, e->d_name);
        }
    }
    closedir(d);
}

void stageObjPackValidate(int (*discHas)(const char *discPath)) {
    int s;

    for (s = 0; s < assetLayerCount(); s++) {
        AssetLayer l;
        char root[1400], folder[1600], disc[64];
        DIR *d;
        struct dirent *e;

        if (!assetLayerAt(s, &l) || l.mod < 0 || !assetModDir(l.mod, NULL, TOPIC, root, sizeof root) || (d = opendir(root)) == NULL) continue;
        while ((e = readdir(d)) != NULL) {
            int num = stageFolderNumber(e->d_name);

            snprintf(folder, sizeof folder, "%s/%s", root, e->d_name);
            if (e->d_name[0] == '.' || !isDir(folder)) continue;
            if (num < 0) continue; /* a new stage's folder (stage_new.c reports it), not an original's */
            snprintf(disc, sizeof disc, "AAA/DAT/FIELD/STAGE/S%03dMASK.BIN", num);
            if (!discHas(disc)) {
                plogLine(TAG, "%s ignored: the disc has no stage %03d\n", folder, num);
            } else {
                validateStageFolder(folder);
            }
        }
        closedir(d);
    }
}

void stageObjPackReset(void) {
    imgSetStageHooks(NULL);
    memset(sEdited, 0, sizeof sEdited);
    memset(&sTotals, 0, sizeof sTotals);
}

void stageObjPackTotals(StageObjPackStats *out) { *out = sTotals; }
