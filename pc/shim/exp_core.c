/* The write policy and the manifest every exporter of the data folder shares; see exp_core.h */
#include "exp_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_fs.h"
#include "port_log.h"

int expFileExists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

/* makes the folder that holds `path`; 0 when it is there */
static int makeParent(const char *path) {
    char dir[1500], *slash;

    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash == NULL) return 0;
    *slash = '\0';
    return tdMakeDirs(dir);
}

/* ---- the manifest ---- */

static char sDiscOverride[17];
static int sDiscOverridden;

void expDiscOverride(const char *hex) {
    sDiscOverridden = hex != NULL;
    snprintf(sDiscOverride, sizeof sDiscOverride, "%s", hex != NULL ? hex : "");
}

void expDiscHex(char out[17]) {
    if (sDiscOverridden) snprintf(out, 17, "%s", sDiscOverride);
    else if (pfsDiscIsOpen()) tdHashHex(pfsDiscHash(1), out);
    else out[0] = 0;
}

void expManInit(TdManifest *m, const char *topic) {
    memset(m, 0, sizeof *m);
    snprintf(m->topic, sizeof m->topic, "%s", topic);
    expDiscHex(m->disc);
}

int expManRead(const char *tag, const char *path, TdManifest *out) {
    uint8_t *mt;
    size_t ml;
    char err[120];

    memset(out, 0, sizeof *out);
    if (tdReadFile(path, &mt, &ml) != 0) return 0;
    if (tdManifestParse((const char *)mt, ml, out, err, sizeof err) != 0) {
        plogLine(tag, "manifest is not asset-manifest-v1 (%s); regenerating (edited files are kept)\n", err);
        tdManifestFree(out);
        memset(out, 0, sizeof *out);
        free(mt);
        return 0;
    }
    free(mt);
    return 1;
}

int expManWrite(const char *tag, const char *path, const TdManifest *m) {
    char *text = tdManifestFormat(m);
    int rc = (text == NULL || makeParent(path) || tdWriteFileAtomic(path, text, strlen(text))) ? -1 : 0;

    if (rc) plogLine(tag, "cannot write %s\n", path);
    free(text);
    return rc;
}

void expManCarry(TdManifest *next, const TdManifest *old) {
    int i;

    for (i = 0; i < old->count; i++) {
        if (!tdManifestFind(next, old->files[i].path)) tdManifestSet(next, old->files[i].path, old->files[i].hash);
    }
    for (i = 0; i < old->names.count; i++) {
        if (!tdKvFind(&next->names, old->names.items[i].key)) tdKvSet(&next->names, old->names.items[i].key, old->names.items[i].val);
    }
}

int expManLoad(const char *tag, const char *path, const char *topic, TdManifest *out) {
    char disc[17];

    if (!expManRead(tag, path, out)) {
        expManInit(out, topic);
        return 0;
    }
    snprintf(out->topic, sizeof out->topic, "%s", topic);
    expDiscHex(disc);
    if (disc[0] == 0 || strcmp(out->disc, disc) == 0) return 0;
    tdKvFree(&out->exporter); /* another disc: the files stay the manifest's, the versions do not count */
    snprintf(out->disc, sizeof out->disc, "%s", disc);
    return 1;
}

void expManOpen(ExpManifest *man, const char *tag, const char *path, const char *topic) {
    memset(man, 0, sizeof *man);
    man->tag = tag;
    snprintf(man->path, sizeof man->path, "%s", path);
    man->dirty = expManLoad(tag, path, topic, &man->m);
    man->loaded = 1;
}

void expManFree(ExpManifest *man) {
    if (man->loaded) tdManifestFree(&man->m);
    man->loaded = 0;
    man->dirty = 0;
}

int expManSave(ExpManifest *man, int onlyIfDirty) {
    int rc;

    if (!man->loaded || (onlyIfDirty && !man->dirty)) return 0;
    rc = expManWrite(man->tag, man->path, &man->m);
    if (rc == 0) man->dirty = 0;
    return rc;
}

/* records key=hash in `m`; 1 when that changed it */
static int record(TdManifest *m, const char *key, const char *hash) {
    const char *have = tdManifestFind(m, key);

    if (have != NULL && !strcmp(have, hash)) return 0;
    return tdManifestSet(m, key, hash) == 0;
}

void expManSet(ExpManifest *man, const char *key, const char *hash) {
    if (record(&man->m, key, hash)) man->dirty = 1;
}

int expManIsCurrent(const ExpManifest *man, const char *key, unsigned version) {
    char dec[16];
    const char *have = tdKvFind(&man->m.exporter, key);

    snprintf(dec, sizeof dec, "%u", version);
    return have != NULL && !strcmp(have, dec);
}

void expManMark(ExpManifest *man, const char *key, unsigned version) {
    char dec[16];

    if (expManIsCurrent(man, key, version)) return;
    snprintf(dec, sizeof dec, "%u", version);
    if (tdKvSet(&man->m.exporter, key, dec) == 0) man->dirty = 1;
}

/* ---- the sink ---- */

void expSinkInPlace(ExpSink *sink, const char *tag, const char *dir, ExpManifest *man, ExpStats *stats) {
    sink->tag = tag;
    sink->dir = dir;
    sink->old = &man->m;
    sink->next = &man->m;
    sink->dirty = &man->dirty;
    sink->sameRun = 0;
    sink->stats = stats;
}

static void note(const ExpSink *s, const char *rel, const char *hash) {
    if (record(s->next, rel, hash) && s->dirty != NULL) *s->dirty = 1;
}

int expPut(const ExpSink *s, const char *rel, const void *bytes, size_t len) {
    char path[1500], np[1510], hex[17], curHex[17] = "";
    uint8_t *cur = NULL;
    size_t curLen = 0;
    int exists = 0;
    const char *man = tdManifestFind(s->old, rel);
    TdFileState state;
    TdExtractAction act;

    if (snprintf(path, sizeof path, "%s/%s", s->dir, rel) >= (int)sizeof path) {
        plogLine(s->tag, "path too long: %s\n", rel);
        return -1;
    }
    tdHashHex(tdHash(bytes, len), hex);
    if (tdReadFile(path, &cur, &curLen) == 0) {
        exists = 1;
        tdHashHex(tdHash(cur, curLen), curHex);
        free(cur);
    }
    state = tdFileState(exists, man, exists ? curHex : NULL);
    act = tdExtractAction(s->sameRun, state);
    if (act == TD_X_SKIP) { /* the earlier run came from the same inputs and the file is still its own */
        if (s->stats != NULL) s->stats->kept++;
        note(s, rel, man);
        return 0;
    }
    if (exists && !strcmp(curHex, hex)) { /* already exactly what we would write: it is ours */
        if (s->stats != NULL) s->stats->kept++;
        note(s, rel, hex);
        return 0;
    }
    if (makeParent(path)) {
        plogLine(s->tag, "cannot create the folder of %s\n", path);
        return -1;
    }
    if (act == TD_X_WRITE_NEW) { /* the user's copy stays; the new version goes beside it */
        int rc = 0;

        snprintf(np, sizeof np, "%s.new", path);
        if (tdWriteFileAtomic(np, bytes, len)) {
            plogLine(s->tag, "cannot write %s\n", np);
            rc = -1;
        } else {
            if (s->stats != NULL) s->stats->newFiles++;
            plogLine(s->tag, "%s was edited, kept; the new version is %s.new\n", rel, rel);
        }
        if (man) note(s, rel, man);
        return rc;
    }
    if (tdWriteFileAtomic(path, bytes, len)) {
        plogLine(s->tag, "cannot write %s\n", path);
        return -1;
    }
    if (s->stats != NULL) {
        s->stats->written++;
        s->stats->bytes += (long)len;
    }
    note(s, rel, hex);
    return 0;
}
