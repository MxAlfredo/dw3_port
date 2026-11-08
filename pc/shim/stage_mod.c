/* Replacing a field stage tile with a PNG (content-export E6b); see stage_mod.h */
#include "stage_mod.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "image_data.h"
#include "mods.h"
#include "port_log.h"
#include "png_read.h"
#include "port_paths.h"
#include "stage_export.h"
#include "text_data.h"

typedef struct Pack {
    long lba;
    char key[24];
    int width;
    int ok; /* 0: not a pack, or its header cannot be read */
} Pack;

typedef struct Entry {
    int pack, frame;
    unsigned long sig;
    unsigned epoch;
    uint8_t *tim; /* the substituted TIM or NULL: the tile stays as on disc */
    size_t len;
} Entry;

static char sDataDir[1100];
static StageModIo sIo;
static Pack *sPacks;
static int sNPacks;
static Entry *sEntries;
static int sNEntries;
static unsigned sEpoch = 1;
static time_t sEpochAt;
static Pack *sLast;
/* the topic's manifest (<data>/stages/manifest.json: the files the exporter wrote and their hashes), read once at the first data tile and again after
   stageModSetup or stageModRecheck (the hot reload's epoch included); the parse of its ~20000 entries takes a few milliseconds */
static TdManifest sMan;
static int sManLoaded;

static void manifestDrop(void) {
    tdManifestFree(&sMan);
    sManLoaded = 0;
}

static void dropAll(void) {
    int i;
    for (i = 0; i < sNEntries; i++) free(sEntries[i].tim);
    free(sEntries);
    free(sPacks);
    manifestDrop();
    sEntries = NULL;
    sPacks = NULL;
    sNEntries = sNPacks = 0;
    sLast = NULL;
}

void stageModSetup(const char *dataDir, const StageModIo *io) {
    dropAll();
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

/* the pack that starts at `lba`, its header read once; NULL when it is not one */
static Pack *packFor(long lba) {
    int i;
    Pack p, *np;
    uint8_t head[2048];
    PackHeader pk;

    if (sLast != NULL && sLast->lba == lba) return sLast->ok ? sLast : NULL;
    for (i = 0; i < sNPacks; i++) {
        if (sPacks[i].lba == lba) {
            sLast = &sPacks[i];
            return sLast->ok ? sLast : NULL;
        }
    }
    memset(&p, 0, sizeof p);
    p.lba = lba;
    if (sIo.pack != NULL && sIo.header != NULL && sIo.pack(sIo.ud, lba, p.key, sizeof p.key) && sIo.header(sIo.ud, lba, head) &&
        stagePackParse(head, sizeof head, &pk, NULL, 0) == 0) {
        p.width = pk.width;
        p.ok = 1;
    }
    np = (Pack *)realloc(sPacks, (size_t)(sNPacks + 1) * sizeof *sPacks);
    if (np == NULL) return NULL;
    sPacks = np;
    sPacks[sNPacks] = p;
    sLast = &sPacks[sNPacks++];
    return sLast->ok ? sLast : NULL;
}

/* the tile as the layers and the manifest name it: stages/s233/tiles/<RR>_<CC>.png without the topic ("s233/tiles/00_00.png"); the pack key is "s233pack" */
static void tileRel(const Pack *pk, const char *name, char *rel, size_t cap) { snprintf(rel, cap, "%.4s/tiles/%s", pk->key, name); }

/* Layer s of a tile (mods.h asset layers: the mods from the last to the first, then data/): 1 and its path when the layer has the file */
static int sourcePath(int s, const Pack *pk, const char *name, char *path, size_t cap, char *label, size_t lcap) {
    AssetLayer l;
    char rel[120];

    if (!assetLayerAt(s, &l)) return 0;
    tileRel(pk, name, rel, sizeof rel);
    snprintf(label, lcap, "%s", l.mod < 0 ? "data (edited)" : l.label);
    return assetLayerFile(&l, "stages", rel, path, cap);
}

/* a number that changes when any source file of the tile (existence, mtime, size) changes */
static unsigned long sourcesSig(const Pack *pk, const char *name) {
    unsigned long h = 1469598103934665603ul;
    int s;
    char path[1400], label[100];

    for (s = 0; s < assetLayerCount(); s++) {
        struct stat st;
        unsigned long v = 0;
        if (sourcePath(s, pk, name, path, sizeof path, label, sizeof label) && stat(path, &st) == 0)
            v = (unsigned long)st.st_mtime * 31ul + (unsigned long)st.st_size + 1ul;
        h = (h ^ v) * 1099511628211ul;
    }
    return h;
}

/* The data folder's PNG counts only when the user changed it: its hash is not the one the exporter wrote */
static int dataEdited(const Pack *pk, const char *name, const uint8_t *png, size_t len) {
    char mp[1300], hex[17], rel[120];
    const char *have;

    if (!sManLoaded) {
        uint8_t *mt;
        size_t ml;

        snprintf(mp, sizeof mp, "%s/stages/manifest.json", sDataDir);
        if (tdReadFile(mp, &mt, &ml) == 0) {
            char err[120];
            if (tdManifestParse((const char *)mt, ml, &sMan, err, sizeof err) != 0) memset(&sMan, 0, sizeof sMan);
            free(mt);
        }
        sManLoaded = 1;
    }
    tileRel(pk, name, rel, sizeof rel);
    tdHashHex(tdHash(png, len), hex);
    have = sMan.files != NULL ? tdManifestFind(&sMan, rel) : NULL;
    return tdFileState(1, have, hex) == TD_FILE_EDITED;
}

static void resolveEntry(Entry *e, const Pack *pk, const char *name, const uint8_t *tim, size_t timLen) {
    int s;
    AssetLayer l;
    char path[1400], label[100];

    free(e->tim);
    e->tim = NULL;
    e->len = 0;
    for (s = 0; s < assetLayerCount() && e->tim == NULL; s++) {
        uint8_t *png, *res;
        size_t pl;
        char why[200];

        if (!sourcePath(s, pk, name, path, sizeof path, label, sizeof label) || tdReadFile(path, &png, &pl)) continue;
        if (assetLayerAt(s, &l) && l.mod < 0 && !dataEdited(pk, name, png, pl)) { /* data/: only a tile the user changed */
            free(png);
            continue;
        }
        if (imgSubstitute(tim, timLen, NULL, png, pl, &res, why, sizeof why) == 0) {
            e->tim = res;
            e->len = timLen;
            plogLine("images", "override stages/%.4s/tiles/%s from %s\n", pk->key, name, label);
        } else {
            plogLine("images", "%s rejected: %s\n", path, why);
        }
        free(png);
    }
}

static time_t nowSec(void) { return time(NULL); }

void stageModRecheck(void) {
    sEpoch++;
    manifestDrop();
}

const uint8_t *stageModTim(long lba, int frame, const uint8_t *tim) {
    Pack *pk = packFor(lba);
    Entry *e = NULL;
    char name[24];
    int i, pi;
    size_t timLen;

    if (pk == NULL || tim == NULL || frame < 0) return NULL;
    pi = (int)(pk - sPacks);
    if (modsHotReload() && nowSec() != sEpochAt) { /* opt-in (DW3_MODS_HOTRELOAD=1): a modder may edit while the game runs, look again at most once a second overall; by default the mods are read once per tile */
        sEpochAt = nowSec();
        sEpoch++;
        manifestDrop();
    }
    stageTileName(pk->width, frame, name, sizeof name);
    for (i = sNEntries - 1; i >= 0; i--) {
        if (sEntries[i].pack == pi && sEntries[i].frame == frame) {
            e = &sEntries[i];
            break;
        }
    }
    if (e != NULL && e->epoch == sEpoch) goto done;
    {
        unsigned long sig = sourcesSig(pk, name);
        if (e != NULL && e->sig == sig) {
            e->epoch = sEpoch;
            goto done;
        }
        timLen = stageTimLength(tim, STAGE_MAX_UNPACKED);
        if (timLen == 0) return NULL;
        if (e == NULL) {
            Entry *ne = (Entry *)realloc(sEntries, (size_t)(sNEntries + 1) * sizeof *sEntries);
            if (ne == NULL) return NULL;
            sEntries = ne;
            e = &sEntries[sNEntries++];
            memset(e, 0, sizeof *e);
            e->pack = pi;
            e->frame = frame;
        }
        e->sig = sig;
        e->epoch = sEpoch;
        resolveEntry(e, pk, name, tim, timLen);
    }
done:
    if (plogOn("images", PLOG_TRACE)) {
        plogLine("images", "load stages/%.4s/tiles/%s (%s)\n", pk->key, name, e->tim != NULL ? "override" : "disc");
    }
    return e->tim;
}

/* a name that is `want` in any case (ADR-010 section 3) */
static int sameName(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

/* "s233" (any case) as a stage folder: 1 */
static int isStageFolder(const char *name) {
    int i;

    if (strlen(name) != 4 || tolower((unsigned char)name[0]) != 's') return 0;
    for (i = 1; i <= 3; i++) {
        if (!isdigit((unsigned char)name[i])) return 0;
    }
    return 1;
}

/* the tiles of one stage folder of a mod; the PNGs looked at */
static int validateTiles(const char *tilesDir, const char *shown, const char *stageFolder, const char *modName, const char *dataDir) {
    DIR *pd = opendir(tilesDir);
    struct dirent *f;
    int n = 0;

    if (pd == NULL) return 0;
    while ((f = readdir(pd)) != NULL) {
        char path[1800], ref[1800], why[200];
        uint8_t *png, *refPng;
        size_t pl, rl;
        PngImage im, orig;
        int rr, cc, ok;

        if (f->d_name[0] == '.') continue;
        if (strlen(f->d_name) != 9 || sscanf(f->d_name, "%2d_%2d.png", &rr, &cc) != 2 || !isdigit((unsigned char)f->d_name[0]) ||
            !isdigit((unsigned char)f->d_name[1]) || f->d_name[2] != '_') {
            plogLine("images", "%s: stages/%s/%s/%s ignored: a tile is <RR>_<CC>.png\n", modName, stageFolder, shown, f->d_name);
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", tilesDir, f->d_name);
        n++;
        if (tdReadFile(path, &png, &pl)) {
            plogLine("images", "%s rejected: cannot read the file\n", path);
            continue;
        }
        ok = pngReadIndexed(png, pl, &im, why, sizeof why) == 0;
        free(png);
        if (!ok) {
            plogLine("images", "%s rejected: %s\n", path, why);
            continue;
        }
        if (dataDir != NULL && dataDir[0] != '\0') {
            snprintf(ref, sizeof ref, "%s/stages/%s/tiles/%s", dataDir, stageFolder, f->d_name);
            if (tdReadFile(ref, &refPng, &rl) == 0) {
                if (pngReadIndexed(refPng, rl, &orig, why, sizeof why) == 0) {
                    if (orig.w != im.w || orig.h != im.h)
                        plogLine("images", "%s rejected: size %dx%d, the tile is %dx%d\n", path, im.w, im.h, orig.w, orig.h);
                    pngImageFree(&orig);
                }
                free(refPng);
            }
        }
        pngImageFree(&im);
    }
    closedir(pd);
    return n;
}

/* --validate-mod (U5). The tile's own TIM is only at hand while the map streams, so what is checked here is what a file can be checked
   against without the game: it names a tile (stages/s<nnn>/tiles/<RR>_<CC>.png), it is an indexed PNG of at most 256 colors (the reader
   of the rules), and, when the exported original is in <dataDir>/stages/s<nnn>/tiles/, it has that PNG's size. A refusal is logged as
   `images: <path> rejected: <why>`, like the loader's. The stage folders themselves and what else they hold (objects/, stage.json) are the
   business of stageObjPackValidate and the stage patch loader. Returns how many PNGs were looked at. */
int stageModValidate(const char *modDir, const char *modName, const char *dataDir) {
    char root[1100];
    DIR *d;
    struct dirent *e;
    int n = 0;

    snprintf(root, sizeof root, "%s/stages", modDir);
    d = opendir(root);
    if (d == NULL) return 0;
    while ((e = readdir(d)) != NULL) {
        char stageDir[1400], tilesDir[1700], folder[8];
        DIR *sd;
        struct dirent *f;

        if (e->d_name[0] == '.' || !isStageFolder(e->d_name)) continue;
        snprintf(folder, sizeof folder, "s%.3s", e->d_name + 1);
        snprintf(stageDir, sizeof stageDir, "%s/%s", root, e->d_name);
        sd = opendir(stageDir);
        if (sd == NULL) continue;
        while ((f = readdir(sd)) != NULL) {
            if (!sameName(f->d_name, "tiles")) continue;
            snprintf(tilesDir, sizeof tilesDir, "%s/%s", stageDir, f->d_name);
            n += validateTiles(tilesDir, f->d_name, folder, modName, dataDir);
        }
        closedir(sd);
    }
    closedir(d);
    return n;
}
