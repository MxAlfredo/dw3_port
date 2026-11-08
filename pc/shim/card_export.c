/* The original cards, one folder each (readable-assets A7.4): see card_export.h. */
#include "card_export.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exp_core.h"
#include "image_data.h"
#include "port_log.h"

#define TAG "cards"
#define CARD_ID_MAX 320 /* five files of 64 */

static char sDataDir[1024];
static MenuExportIo sIo;
static CardExportStats sTotals;

void cardExportSetup(const char *dataDir, const MenuExportIo *io) {
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

void cardExportTotals(CardExportStats *out) { *out = sTotals; }

/* "all", or card numbers (12, c012, C012) separated by commas or spaces: want[id] for id 1..CARD_ID_MAX; returns how many cards are named */
static int parseSpec(const char *spec, char *want) {
    char tok[32];
    int named = 0, id;
    const char *s = spec;

    memset(want, 0, CARD_ID_MAX + 1);
    while (*s) {
        size_t len = 0;

        while (*s == ',' || *s == ' ') s++;
        while (*s && *s != ',' && *s != ' ' && len + 1 < sizeof tok) tok[len++] = (char)tolower((unsigned char)*s++);
        tok[len] = '\0';
        if (len == 0) break;
        if (strcmp(tok, "all") == 0) {
            memset(want + 1, 1, CARD_ID_MAX);
            continue;
        }
        id = atoi(tok[0] == 'c' ? tok + 1 : tok);
        if (id >= 1 && id <= CARD_ID_MAX) want[id] = 1;
        else plogLine(TAG, "export_cards: \"%s\" is not a card (use all, or numbers like c012 separated by commas)\n", tok);
    }
    for (id = 1; id <= CARD_ID_MAX; id++) named += want[id];
    return named;
}

typedef struct Job {
    const ExpSink *sink;
    ExpManifest *man;
    const char *discPath;
    const char *want;
    int failed, cards;
} Job;

static const char *noRoot(const char *discPath) { return strncmp(discPath, "AAA/", 4) == 0 ? discPath + 4 : discPath; }

static int onFace(void *ud, int id, const uint8_t *png, size_t len) {
    Job *j = (Job *)ud;
    char folder[8], rel[40], json[640], ident[64], file[80];
    int n;

    if (!j->want[id]) return 0;
    if (imgCardFolder(id, folder, sizeof folder)) return -1;
    snprintf(rel, sizeof rel, "%s/face.png", folder);
    snprintf(ident, sizeof ident, "dw3:card/%d", id);
    snprintf(file, sizeof file, "%s", noRoot(j->discPath));
    for (n = 0; file[n]; n++) file[n] = (char)tolower((unsigned char)file[n]);
    if (expPut(j->sink, rel, png, len)) return -1;
    n = snprintf(json, sizeof json,
                 "{\n  \"format\": \"cards-card-v1\",\n  \"name\": \"%s\",\n  \"id\": \"%s\",\n  \"number\": %d,\n  \"disc\": { \"file\": \"%s\", \"index\": %d },\n"
                 "  \"face\": \"face.png\",\n  \"notes\": \"Information only: regenerated from the disc. face.png is the card's picture; edit it, keep it 32x32 with at most 256 colours. "
                 "number is the game's card number (card file * 64 + index + 1).\"\n}\n",
                 folder, ident, id, file, (id - 1) % 64);
    snprintf(rel, sizeof rel, "%s/card.json", folder);
    if (n <= 0 || n >= (int)sizeof json || expPut(j->sink, rel, json, (size_t)n)) return -1;
    if (tdKvSet(&j->man->m.names, folder, ident)) return -1;
    j->cards++;
    return 0;
}

static const char kExporterKey[] = "cards";

/* a run of the current version whose files are all there: nothing to build */
static int upToDate(const ExpManifest *man, const char *dir) {
    char path[1400];
    int i;

    if (!expManIsCurrent(man, kExporterKey, CARD_EXPORT_VERSION) || man->m.count == 0) return 0;
    for (i = 0; i < man->m.count; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, man->m.files[i].path);
        if (!expFileExists(path)) return 0;
    }
    return 1;
}

int cardExportRun(const char *spec) {
    ExpManifest man;
    ExpStats es;
    ExpSink sink;
    Job job;
    char want[CARD_ID_MAX + 1], dir[1100], mp[1200], path[112];
    long lba, size;
    int i, rc = -1, manLoaded = 0, wantAll;

    memset(&sTotals, 0, sizeof sTotals);
    if (!sDataDir[0] || sIo.list == NULL || sIo.read == NULL || spec == NULL) return -1;
    if (parseSpec(spec, want) == 0) return -1;
    wantAll = strstr(spec, "all") != NULL;
    snprintf(dir, sizeof dir, "%s/cards", sDataDir);
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    expManOpen(&man, TAG, mp, "cards");
    manLoaded = 1;
    if (upToDate(&man, dir)) {
        sTotals.skipped = 1;
        rc = 0;
        goto done;
    }
    memset(&es, 0, sizeof es);
    expSinkInPlace(&sink, TAG, dir, &man, &es);
    memset(&job, 0, sizeof job);
    job.sink = &sink;
    job.man = &man;
    job.want = want;
    for (i = 0; sIo.list(sIo.ud, i, path, sizeof path, &lba, &size); i++) {
        uint8_t *bytes;
        int n;

        if (!imgIsCardFile(path)) continue;
        bytes = menuDiscRead(&sIo, lba, size);
        if (bytes == NULL) {
            plogLine(TAG, "cannot read %s from the disc\n", path);
            job.failed++;
            continue;
        }
        job.discPath = path;
        n = imgCardFaces(path, bytes, (size_t)size, onFace, &job);
        free(bytes);
        if (n < 0) {
            plogLine(TAG, "cannot convert a card of %s\n", path);
            job.failed++;
        }
    }
    sTotals.cards = job.cards;
    sTotals.failed = job.failed;
    sTotals.written = es.written;
    sTotals.kept = es.kept;
    sTotals.newFiles = es.newFiles;
    sTotals.bytes = es.bytes;
    if (sTotals.failed == 0 && wantAll) expManMark(&man, kExporterKey, CARD_EXPORT_VERSION);
    if (expManSave(&man, 0)) sTotals.failed++;
    plogLine(TAG, "Done: %d cards in %s/cards, %d written, %d kept, %d new beside edited, %d failed, %ld bytes\n", sTotals.cards, sDataDir, sTotals.written, sTotals.kept,
             sTotals.newFiles, sTotals.failed, sTotals.bytes);
    rc = sTotals.cards;
done:
    if (manLoaded) expManFree(&man);
    return rc;
}
