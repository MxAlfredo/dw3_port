/* The original field sprites, one folder each (readable-assets A7.5): see sprite_export.h. */
#include "sprite_export.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exp_core.h"
#include "port_log.h"
#include "sprite_data.h"

#define TAG "sprites"

static char sDataDir[1024];
static MenuExportIo sIo;
static SpriteExportStats sTotals;

void spriteExportSetup(const char *dataDir, const MenuExportIo *io) {
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

void spriteExportTotals(SpriteExportStats *out) { *out = sTotals; }

/* does `spec` ("all" or names separated by commas or spaces) name `folder`? */
static int wants(const char *spec, const char *folder) {
    const char *s = spec;

    while (*s) {
        char tok[64];
        size_t len = 0;

        while (*s == ',' || *s == ' ') s++;
        while (*s && *s != ',' && *s != ' ' && len + 1 < sizeof tok) tok[len++] = (char)tolower((unsigned char)*s++);
        tok[len] = '\0';
        if (len == 0) break;
        if (strcmp(tok, "all") == 0 || strcmp(tok, folder) == 0) return 1;
    }
    return 0;
}

typedef struct Job {
    const ExpSink *sink;
    ExpManifest *man;
    char folder[40];
    int failed, frames;
} Job;

static int onEmit(void *ud, const char *rel, const uint8_t *data, size_t len) {
    Job *j = (Job *)ud;
    char path[96];
    size_t n = strlen(rel);

    if (snprintf(path, sizeof path, "%s/%s", j->folder, rel) >= (int)sizeof path) return -1;
    if (n > 4 && strcmp(rel + n - 4, ".png") == 0) j->frames++;
    return expPut(j->sink, path, data, len);
}

static const char kExporterKey[] = "sprites";

/* a run of the current version whose files are all there: nothing to build */
static int upToDate(const ExpManifest *man, const char *dir) {
    char path[1400];
    int i;

    if (!expManIsCurrent(man, kExporterKey, SPRITE_EXPORT_VERSION) || man->m.count == 0) return 0;
    for (i = 0; i < man->m.count; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, man->m.files[i].path);
        if (!expFileExists(path)) return 0;
    }
    return 1;
}

int spriteExportRun(const char *spec) {
    ExpManifest man;
    ExpStats es;
    ExpSink sink;
    Job job;
    SprStats ss;
    char dir[1100], mp[1200], path[112];
    long lba, size;
    int i, rc = -1, manLoaded = 0, wantAll, sprites = 0;

    memset(&sTotals, 0, sizeof sTotals);
    if (!sDataDir[0] || sIo.list == NULL || sIo.read == NULL || spec == NULL) return -1;
    wantAll = strstr(spec, "all") != NULL;
    snprintf(dir, sizeof dir, "%s/sprites", sDataDir);
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    expManOpen(&man, TAG, mp, "sprites");
    manLoaded = 1;
    if (upToDate(&man, dir)) {
        sTotals.skipped = 1;
        rc = 0;
        goto done;
    }
    memset(&es, 0, sizeof es);
    memset(&ss, 0, sizeof ss);
    expSinkInPlace(&sink, TAG, dir, &man, &es);
    memset(&job, 0, sizeof job);
    job.sink = &sink;
    job.man = &man;
    for (i = 0; sIo.list(sIo.ud, i, path, sizeof path, &lba, &size); i++) {
        uint8_t *bytes;
        char ident[64];

        if (!sprIsSpriteFile(path) || sprPartnerOf(path) >= 0 || sprFolder(path, job.folder, sizeof job.folder) || !wants(spec, job.folder)) continue;
        bytes = menuDiscRead(&sIo, lba, size);
        if (bytes == NULL) {
            plogLine(TAG, "cannot read %s from the disc\n", path);
            job.failed++;
            continue;
        }
        sprExtractAs("sprites-sprite-v1", onEmit, &job, path, bytes, (size_t)size, &ss);
        free(bytes);
        snprintf(ident, sizeof ident, "dw3:sprite/%s", job.folder);
        if (tdKvSet(&man.m.names, job.folder, ident)) job.failed++;
        sprites++;
    }
    sTotals.sprites = sprites;
    sTotals.frames = job.frames;
    sTotals.failed = job.failed + ss.failed;
    sTotals.written = es.written;
    sTotals.kept = es.kept;
    sTotals.newFiles = es.newFiles;
    sTotals.bytes = es.bytes;
    if (sprites == 0 && !wantAll) {
        plogLine(TAG, "export_sprites: \"%s\" names no sprite of the disc (use all, or names like p001play separated by commas)\n", spec);
        rc = 0;
        goto done;
    }
    if (sTotals.failed == 0 && wantAll) expManMark(&man, kExporterKey, SPRITE_EXPORT_VERSION);
    if (expManSave(&man, 0)) sTotals.failed++;
    plogLine(TAG, "Done: %d sprites (%d frames) in %s/sprites, %d written, %d kept, %d new beside edited, %d failed, %ld bytes\n", sTotals.sprites, sTotals.frames, sDataDir,
             sTotals.written, sTotals.kept, sTotals.newFiles, sTotals.failed, sTotals.bytes);
    rc = sprites;
done:
    if (manLoaded) expManFree(&man);
    return rc;
}
