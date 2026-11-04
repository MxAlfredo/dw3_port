/*
 * The port's config files: defaults, reference copies, mods merge. See port_config.h.
 */
#include "port_config.h"

#include "fast_transitions.h"
#include "mods.h"
#include "port_log.h"
#include "stage_banner.h"
#include "text_data.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { K_INT, K_BOXES };
#define BOX_COUNT 10 /* FIELDSTG_bannerBoxes */

typedef struct Key {
    const char *key;
    int kind;
    int def, min, max;
    size_t off;       /* where the value lives in the config struct */
    const char *note;
} Key;

typedef struct Schema {
    const char *name;
    const Key *keys;
    int count;
    void *dst;
} Schema;

static PcStageBanner gBanner = {STAGE_BANNER_FAST_TYPE_DELAY, STAGE_BANNER_FAST_HOLD_FRAMES, STAGE_BANNER_FAST_RAISE,
                                (1 << 2) | (1 << 3) | (1 << 4) | (1 << 7)};
static PcTransitions gTrans = {FAST_DOOR_FACTOR, FAST_BATTLE_TILES_PER_FRAME, FAST_BATTLE_WAIT_FRAMES, FAST_RESULTS_FACTOR,
                         FAST_MAP_TILES_PER_FRAME};

const PcStageBanner *pcStageBanner(void) { return &gBanner; }
const PcTransitions *pcTransitions(void) { return &gTrans; }

/* One table per config: key, default and valid range together. Used by Fast mode only. */
static const Key kBanner[] = {
    {"type_delay_frames", K_INT, STAGE_BANNER_FAST_TYPE_DELAY, 1, 10, offsetof(PcStageBanner, typeDelayFrames),
     "Frames per typed character of the place name. The original game uses 5."},
    {"hold_frames", K_INT, STAGE_BANNER_FAST_HOLD_FRAMES, 0, 600, offsetof(PcStageBanner, holdFrames),
     "Frames the finished name stays on screen before it closes (60 frames = 1 second). The original holds 60."},
    {"raise_px", K_INT, STAGE_BANNER_FAST_RAISE, 0, 60, offsetof(PcStageBanner, raisePx),
     "Pixels the banner bars and the two names move up so they do not cover the player."},
    {"hidden_boxes", K_BOXES, (1 << 2) | (1 << 3) | (1 << 4) | (1 << 7), 0, BOX_COUNT - 1, offsetof(PcStageBanner, hiddenBoxes),
     "Indices (0 to 9) of the banner's bar boxes that are not drawn. The default hides the vertical bars."},
};

static const Key kTrans[] = {
    {"door_factor", K_INT, FAST_DOOR_FACTOR, 1, 10, offsetof(PcTransitions, doorFactor),
     "How many times faster the field closes when you leave through a door. 1 is the original speed."},
    {"battle_tiles_per_frame", K_INT, FAST_BATTLE_TILES_PER_FRAME, 1, 30, offsetof(PcTransitions, battleTilesPerFrame),
     "Tiles of the battle entry spiral that finish every frame. The original does 1."},
    {"battle_wait_frames", K_INT, FAST_BATTLE_WAIT_FRAMES, 1, 16, offsetof(PcTransitions, battleWaitFrames),
     "Frames the battle entry waits after the last tile. The original waits 16."},
    {"results_factor", K_INT, FAST_RESULTS_FACTOR, 1, 10, offsetof(PcTransitions, resultsFactor),
     "How many times faster the battle results panels open and close. 1 is the original speed."},
    {"map_tiles_per_frame", K_INT, FAST_MAP_TILES_PER_FRAME, 1, 12, offsetof(PcTransitions, mapTilesPerFrame),
     "Map tiles in view the field unpacks every frame in Fast map loading. The view holds 12 at most; the original does 1."},
};

static const Schema kSchemas[] = {
    {"stage_banner", kBanner, (int)(sizeof kBanner / sizeof kBanner[0]), &gBanner},
    {"transitions", kTrans, (int)(sizeof kTrans / sizeof kTrans[0]), &gTrans},
};
#define SCHEMA_COUNT ((int)(sizeof kSchemas / sizeof kSchemas[0]))

static int *slot(const Schema *s, const Key *k) { return (int *)((char *)s->dst + k->off); }

static void applyDefaults(const Schema *s)
{
    int i;
    for (i = 0; i < s->count; i++) *slot(s, &s->keys[i]) = s->keys[i].def;
}

/* Apply one parsed file onto the schema's values, key by key. Returns -1 when the whole file is
   refused (format), else 0. Bad keys are logged and skipped. */
static int applyFile(const Schema *s, const char *path, const PjVal *v, int n)
{
    char want[48];
    const PjVal *f = pjFind(v, n, "format");
    int i, j;

    snprintf(want, sizeof want, "%s-v1", s->name);
    if (!f || f->kind != PJ_STR) {
        plogLine("config", "%s rejected: no \"format\" (expected \"%s\")\n", path, want);
        return -1;
    }
    if (strcmp(f->str, want)) {
        plogLine("config", "%s rejected: unknown format \"%s\" (expected \"%s\")\n", path, f->str, want);
        return -1;
    }
    for (i = 0; i < n; i++) {
        const Key *k = NULL;
        if (!strcmp(v[i].key, "format") || !strcmp(v[i].key, "notes")) continue;
        for (j = 0; j < s->count; j++) {
            if (!strcmp(s->keys[j].key, v[i].key)) k = &s->keys[j];
        }
        if (!k) {
            plogLine("config", "%s key \"%s\" rejected: unknown key\n", path, v[i].key);
            continue;
        }
        if (k->kind == K_INT) {
            if (v[i].kind != PJ_NUM) {
                plogLine("config", "%s key \"%s\" rejected: not a number\n", path, k->key);
            } else if (v[i].num != (double)(int)v[i].num) {
                plogLine("config", "%s key \"%s\" rejected: not a whole number\n", path, k->key);
            } else if ((int)v[i].num < k->min || (int)v[i].num > k->max) {
                plogLine("config", "%s key \"%s\" rejected: %d is outside %d to %d\n", path, k->key, (int)v[i].num, k->min, k->max);
            } else {
                *slot(s, k) = (int)v[i].num;
            }
        } else { /* K_BOXES: an array of indices */
            int mask = 0, bad = 0;
            if (v[i].kind != PJ_ARR) {
                plogLine("config", "%s key \"%s\" rejected: not a list\n", path, k->key);
                continue;
            }
            for (j = 0; j < v[i].arrCount; j++) {
                if (v[i].arr[j] < k->min || v[i].arr[j] > k->max) bad = v[i].arr[j] + 1000000;
                else mask |= 1 << v[i].arr[j];
            }
            if (bad) plogLine("config", "%s key \"%s\" rejected: index %d is outside %d to %d\n", path, k->key, bad - 1000000, k->min, k->max);
            else *slot(s, k) = mask;
        }
    }
    return 0;
}

/* Read and apply one file. Returns 1 when it contributed, 0 when it was refused or missing */
static int loadFile(const Schema *s, const char *path, int logMissing)
{
    uint8_t *data;
    size_t len;
    PjVal v[PJ_MAX_KEYS];
    char err[160];
    int n;

    if (tdReadFile(path, &data, &len)) {
        if (logMissing) plogLine("config", "%s rejected: cannot read it\n", path);
        return 0;
    }
    n = pjParseFlat((const char *)data, (unsigned long)len, v, PJ_MAX_KEYS, err, sizeof err);
    free(data);
    if (n < 0) {
        plogLine("config", "%s rejected: %s\n", path, err);
        return 0;
    }
    return applyFile(s, path, v, n) == 0;
}

static int fileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* The reference copy: the defaults and a "notes" object, deterministic text (its hash is what the
   manifest records). malloc'ed. */
static char *referenceText(const Schema *s)
{
    size_t cap = 4096;
    char *b = (char *)malloc(cap);
    size_t n = 0;
    int i, j;

    if (!b) return NULL;
#define PUT(...) do { n += (size_t)snprintf(b + n, cap - n, __VA_ARGS__); if (n + 1 >= cap) { free(b); return NULL; } } while (0)
    PUT("{\n  \"format\": \"%s-v1\",\n", s->name);
    for (i = 0; i < s->count; i++) {
        const Key *k = &s->keys[i];
        if (k->kind == K_INT) {
            PUT("  \"%s\": %d,\n", k->key, k->def);
        } else {
            int first = 1;
            PUT("  \"%s\": [", k->key);
            for (j = 0; j <= k->max; j++) {
                if (k->def & (1 << j)) {
                    PUT("%s%d", first ? "" : ", ", j);
                    first = 0;
                }
            }
            PUT("],\n");
        }
    }
    PUT("  \"notes\": {\n");
    for (i = 0; i < s->count; i++) {
        const Key *k = &s->keys[i];
        PUT("    \"%s\": \"%s Valid: %d to %d.\"%s\n", k->key, k->note, k->min, k->max, i + 1 < s->count ? "," : "");
    }
    PUT("  }\n}\n");
#undef PUT
    return b;
}

/* Keep <data>/config/<name>.json as the reference copy: write it when missing, refresh it while it is
   still the port's own (hash in the manifest), never touch an edited one. Returns 1 when the file on
   disk is the user's (a source), 0 otherwise. */
static int referenceCopy(const Schema *s, const char *dataDir, TdManifest *man, int *manDirty)
{
    char path[1200], rel[96], hex[17];
    char *want = referenceText(s);
    uint8_t *cur = NULL;
    size_t curLen = 0;

    snprintf(path, sizeof path, "%s/config/%s.json", dataDir, s->name);
    snprintf(rel, sizeof rel, "%s.json", s->name);
    if (!want) return 0;
    if (tdReadFile(path, &cur, &curLen) == 0) {
        TdFileState st;
        tdHashHex(tdHash(cur, curLen), hex);
        st = tdFileState(1, tdManifestFind(man, rel), hex);
        free(cur);
        if (st == TD_FILE_EDITED) {
            free(want);
            return 1;
        }
        /* pristine: refresh it only when this port's defaults differ from what is on disk */
        {
            char wantHex[17];
            tdHashHex(tdHash(want, strlen(want)), wantHex);
            if (!strcmp(wantHex, hex)) { free(want); return 0; }
        }
    }
    {
        char dir[1200], hex2[17];
        snprintf(dir, sizeof dir, "%s/config", dataDir);
        if (tdMakeDirs(dir) || tdWriteFileAtomic(path, want, strlen(want))) {
            plogLine("config", "cannot write the reference copy %s\n", path);
        } else {
            tdHashHex(tdHash(want, strlen(want)), hex2);
            tdManifestSet(man, rel, hex2);
            *manDirty = 1;
        }
    }
    free(want);
    return 0;
}

void pcConfigLoad(const char *dataDir)
{
    TdManifest man;
    char mpath[1200];
    int manDirty = 0, si, mi;

    memset(&man, 0, sizeof man);
    snprintf(man.stamp, sizeof man.stamp, "config-v1");
    if (dataDir && dataDir[0]) {
        uint8_t *mt;
        size_t ml;
        snprintf(mpath, sizeof mpath, "%s/config/manifest.json", dataDir);
        if (tdReadFile(mpath, &mt, &ml) == 0) {
            TdManifest old;
            char err[120];
            memset(&old, 0, sizeof old);
            if (tdManifestParse((const char *)mt, ml, &old, err, sizeof err) == 0) {
                tdManifestFree(&man);
                man = old;
            } else {
                plogLine("config", "%s rejected: %s (its copies count as edited)\n", mpath, err);
            }
            free(mt);
        }
    }
    for (si = 0; si < SCHEMA_COUNT; si++) {
        const Schema *s = &kSchemas[si];
        char sources[400], path[1200];
        int len;

        applyDefaults(s);
        len = snprintf(sources, sizeof sources, "default");
        if (dataDir && dataDir[0]) {
            if (referenceCopy(s, dataDir, &man, &manDirty)) {
                snprintf(path, sizeof path, "%s/config/%s.json", dataDir, s->name);
                if (loadFile(s, path, 1)) len += snprintf(sources + len, sizeof sources - (size_t)len, "+data");
            }
        }
        for (mi = 0; mi < modsCount(); mi++) {
            snprintf(path, sizeof path, "%s/config/%s.json", modsDirOf(mi), s->name);
            if (fileExists(path) && loadFile(s, path, 1) && (size_t)len < sizeof sources - 80) {
                len += snprintf(sources + len, sizeof sources - (size_t)len, "+mods/%s", modsNameOf(mi));
            }
        }
        plogLine("config", "%s from %s\n", s->name, sources);
    }
    if (manDirty && dataDir && dataDir[0]) {
        char *text = tdManifestFormat(&man);
        if (!text || tdWriteFileAtomic(mpath, text, strlen(text))) plogLine("config", "cannot write %s\n", mpath);
        free(text);
    }
    tdManifestFree(&man);
}
