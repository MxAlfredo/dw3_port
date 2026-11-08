/*
 * Host test of the raw copy export (content-export E7): pc/shim/raw_export.c. A synthetic raw disc image in memory (files covered by the
 * other exporters, files nobody covers, files outside AAA/); no retail bytes. The scratch folder lives under $TMPDIR.
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/raw_export.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "rawtest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- the synthetic disc ---- */

typedef struct SynFile {
    const char *path; /* as on the disc */
    long size;
    const char *kind; /* the kind rawKindOf must name, NULL when another exporter covers it or it is not game data */
    long lba;
} SynFile;

static SynFile gFiles[] = {
    /* covered by other exporters */
    {"AAA/DAT/SOUND/BGM001/MPBGM001.BIN", 3000, NULL, 0},
    {"AAA/STR/MOVIEOPN.STR", 2352, NULL, 0},
    {"AAA/DAT/Z_STAGE/S233PACK.BIN", 4096, NULL, 0},
    {"AAA/DAT/FIGHT/MODEL/M000KOTE.BIN", 2500, NULL, 0},
    {"AAA/DAT/COUNTRY/ENG/ESCARDGM.BIN", 2100, NULL, 0},
    {"AAA/DAT/COUNTRY/SPN/ESCARDGM.BIN", 2100, NULL, 0},
    {"AAA/DAT/FIELD/SPRT/S233SPRT.BIN", 2049, NULL, 0},
    {"AAA/DAT/SCREEN/STSHOPTM.BIN", 2048, NULL, 0},
    {"AAA/DAT/SCREEN/TLOGOPAL.BIN", 2048, NULL, 0},
    {"AAA/DAT/CARD/CARDPAK0.BIN", 2048, NULL, 0},
    {"AAA/DAT/CARD/CARDDATA.BIN", 2048, NULL, 0},
    {"AAA/DAT/FIELD/STAGE/S233MASK.BIN", 2048, NULL, 0},
    {"AAA/DAT/FIELD/STAGE/S415BG01.BIN", 2048, NULL, 0},
    {"AAA/DAT/FIELD/FIELDCOM.BIN", 2048, NULL, 0},
    {"SLES_039.36", 2048, NULL, 0},
    {"SYSTEM.CNF", 100, NULL, 0},
    /* nobody covers them */
    {"AAA/DAT/FIELD/STAGE/S233TMPK.BIN", 5000, "stage-map", 0},
    {"AAA/DAT/FIELD/STAGE/S330TMPK.BIN", 2048, "stage-map", 0},
    {"AAA/DAT/FIELD/STAGE/S400TMPK.BIN", 3333, "stage-map", 0},
    {"AAA/DAT/FIELD/STAGE/SSUBDEMO.BIN", 2600, "stage-demo", 0},
    {"AAA/DAT/FIGHT/EFFECT/E000COM1.BIN", 4097, "battle-effect", 0},
    {"AAA/DAT/FIGHT/PGFGTC00.BIN", 2048, "battle-common", 0},
    {"AAA/DAT/FIGHT/F000COM3.BIN", 2050, "battle-common", 0},
    {"AAA/DAT/TRAINING/TRANIN00.BIN", 2900, "training-data", 0},
    {"AAA/DAT/SCREEN/STSHOPCS.BIN", 1544, "sprite-sheet", 0},
    {"AAA/DAT/COUNTRY/CMFONTCS.BIN", 1000, "sprite-sheet", 0},
    {"AAA/DAT/CARD/CARD_NPC.BIN", 35360, "card-opponents", 0},
    {"AAA/DAT/CARD/CARDPACK.BIN", 3160, "card-pack", 0},
    {"AAA/DAT/COUNTRY/JPN/ESCARDGM.BIN", 2100, "text-unused", 0},
    {"AAA/DAT/COUNTRY/USA/ESSYSTEM.BIN", 2300, "text-unused", 0},
    {"AAA/PRO/WSTAG233.PRO", 4500, "stage-overlay", 0},
    {"AAA/PRO/FIELDSTG.PRO", 2048, "mode-overlay", 0},
    {"AAA/PRO/SMDLDATA.PRO", 2049, "mode-overlay", 0},
    {"AAA/DAT/NONE____.BIN", 1, "filler", 0},
    {"AAA/DAT/WEIRD.BIN", 700, "other", 0},
};
#define NFILES ((int)(sizeof gFiles / sizeof gFiles[0]))

#define MAX_SECTORS 200
static uint8_t gDisc[MAX_SECTORS][2352];
static int gReads;
static long gFailLba = -1;

static uint8_t byteAt(int file, long i, int gen) { return (uint8_t)((i * 7 + file * 13 + gen * 101) & 0xFF); }

/* writes file `f` with the pattern of generation `gen`; the rest of its last sector is 0xEE (the copy must stop at the exact size) */
static void putFile(int f, int gen) {
    long sectors = (gFiles[f].size + 2047) / 2048, i;
    for (i = 0; i < sectors * 2048; i++) {
        gDisc[gFiles[f].lba + i / 2048][24 + i % 2048] = i < gFiles[f].size ? byteAt(f, i, gen) : 0xEE;
    }
}

static void buildDisc(void) {
    long next = 20;
    int f;
    memset(gDisc, 0x55, sizeof gDisc);
    for (f = 0; f < NFILES; f++) {
        gFiles[f].lba = next;
        next += (gFiles[f].size + 2047) / 2048;
        putFile(f, 0);
    }
}

static int fileIndex(const char *path) {
    int f;
    for (f = 0; f < NFILES; f++) {
        if (strcmp(gFiles[f].path, path) == 0) return f;
    }
    return -1;
}

static int ioList(void *ud, int i, char *path, size_t cap, long *lba, long *size) {
    (void)ud;
    if (i < 0 || i >= NFILES) return 0;
    snprintf(path, cap, "%s", gFiles[i].path);
    *lba = gFiles[i].lba;
    *size = gFiles[i].size;
    return 1;
}

static int ioRead(void *ud, long lba, unsigned char *frame) {
    (void)ud;
    if (lba < 0 || lba >= MAX_SECTORS || lba == gFailLba) return 0;
    gReads++;
    memcpy(frame, gDisc[lba], 2352);
    return 1;
}

static const RawExportIo gIo = {ioList, ioRead, NULL};

/* ---- scratch folder ---- */

static char gRoot[1024];

static void dataDir(char *out, size_t cap) { snprintf(out, cap, "%s/data", gRoot); }

static int exists(const char *rel) {
    char path[1400];
    struct stat st;
    snprintf(path, sizeof path, "%s/data/raw/%s", gRoot, rel);
    return stat(path, &st) == 0;
}

static uint8_t *slurp(const char *rel, size_t *n) {
    char path[1400];
    uint8_t *d;
    snprintf(path, sizeof path, "%s/data/raw/%s", gRoot, rel);
    if (tdReadFile(path, &d, n)) return NULL;
    return d;
}

static void spit(const char *rel, const void *d, size_t n) {
    char path[1400], dir[1400], *sl;
    snprintf(path, sizeof path, "%s/data/raw/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "%s", path);
    sl = strrchr(dir, '/');
    if (sl) {
        *sl = 0;
        CHECK(tdMakeDirs(dir) == 0);
    }
    CHECK(tdWriteFileAtomic(path, d, n) == 0);
}

static void removeFile(const char *rel) {
    char path[1400];
    snprintf(path, sizeof path, "%s/data/raw/%s", gRoot, rel);
    CHECK(unlink(path) == 0);
}

static void rmtree(const char *dir) {
    char cmd[1400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd)) {
    }
}

static int scratch(void) {
    snprintf(gRoot, sizeof gRoot, "%s/rawtest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        failures++;
        return 0;
    }
    return 1;
}

/* the raw copy of disc file f is there and holds generation `gen` byte for byte (the exact size, no sector padding) */
static int sameAsDisc(const char *rel, int f, int gen, const char *suffix) {
    char name[160];
    size_t n = 0, i;
    uint8_t *d;
    int ok;

    snprintf(name, sizeof name, "%s%s", rel, suffix);
    d = slurp(name, &n);
    if (d == NULL) return 0;
    ok = n == (size_t)gFiles[f].size;
    for (i = 0; ok && i < n; i++) ok = d[i] == byteAt(f, (long)i, gen);
    free(d);
    return ok;
}

static int countExpected(void) {
    int f, n = 0;
    for (f = 0; f < NFILES; f++) n += gFiles[f].kind != NULL;
    return n;
}

static int countKinds(void) {
    int f, g, n = 0;
    for (f = 0; f < NFILES; f++) {
        int seen = 0;
        if (gFiles[f].kind == NULL) continue;
        for (g = 0; g < f; g++) seen |= gFiles[g].kind != NULL && strcmp(gFiles[g].kind, gFiles[f].kind) == 0;
        n += !seen;
    }
    return n;
}

/* ---- the kinds ---- */

static void testKinds(void) {
    int f, n = 0, i, j;
    const RawKind *k = rawKinds(&n);
    char rel[100];

    CHECK(k != NULL && n >= 12);
    for (i = 0; i < n; i++) {
        CHECK(k[i].id[0] && k[i].pattern[0] && k[i].know[0] && k[i].usedBy[0] && k[i].replace[0]);
        CHECK(!strcmp(k[i].works, "yes") || !strcmp(k[i].works, "no") || !strcmp(k[i].works, "unverified"));
        for (j = 0; j < i; j++) CHECK(strcmp(k[i].id, k[j].id) != 0);
    }
    for (f = 0; f < NFILES; f++) {
        const RawKind *got = rawKindOf(gFiles[f].path);
        if (gFiles[f].kind == NULL) {
            CHECK(got == NULL);
        } else {
            CHECK(got != NULL && !strcmp(got->id, gFiles[f].kind));
        }
    }
    CHECK(rawKindOf(NULL) == NULL);
    CHECK(rawRelPath("AAA/DAT/FIELD/STAGE/S233TMPK.BIN", rel, sizeof rel) && !strcmp(rel, "dat/field/stage/s233tmpk.bin"));
    CHECK(!rawRelPath("SLES_039.36", rel, sizeof rel));
    CHECK(!rawRelPath("AAA/", rel, sizeof rel));
    CHECK(!rawRelPath("AAA/DAT/FIELD/STAGE/S233TMPK.BIN", rel, 10)); /* does not fit */
}

/* ---- the export ---- */

static void testExport(void) {
    char dd[1100];
    RawExportStats t;
    size_t n = 0;
    uint8_t *d;
    int f, reads;
    TdManifest man;
    char err[120], *js;
    const char *stageMapPath = "dat/field/stage/s233tmpk.bin";

    CHECK(scratch());
    dataDir(dd, sizeof dd);

    /* nothing to do: no setup, no valid kind */
    rawExportSetup(NULL, NULL);
    CHECK(rawExportRun("all") == -1);
    rawExportSetup(dd, &gIo);
    CHECK(rawExportRun("nonsense") == -1);
    CHECK(rawExportRun("") == -1);
    CHECK(!exists("README.txt") && !exists("manifest.json"));

    /* everything: only what no other exporter covers, byte for byte */
    CHECK(rawExportRun("all") == countKinds());
    rawExportTotals(&t);
    CHECK(t.kinds == countKinds() && t.skipped == 0 && t.files == countExpected() && t.failed == 0 && t.newFiles == 0);
    CHECK(t.written == countExpected() + 2); /* the files, README.txt and raw.json */
    for (f = 0; f < NFILES; f++) {
        char rel[100];
        CHECK(rawRelPath(gFiles[f].path, rel, sizeof rel) || gFiles[f].kind == NULL);
        if (gFiles[f].kind != NULL) {
            CHECK(sameAsDisc(rel, f, 0, ""));
        } else if (rawRelPath(gFiles[f].path, rel, sizeof rel)) {
            CHECK(!exists(rel)); /* covered: not copied */
        }
    }
    CHECK(!exists("sles_039.36") && !exists("system.cnf") && !exists("../sles_039.36"));
    CHECK(exists("README.txt") && exists("raw.json") && exists("manifest.json"));

    /* the notes */
    d = slurp("README.txt", &n);
    CHECK(d != NULL && n > 200 && strstr((char *)d, "mods/<your mod>/files/") != NULL && strstr((char *)d, "stage-map") != NULL);
    free(d);
    js = (char *)slurp("raw.json", &n);
    CHECK(js != NULL && strstr(js, "\"schema\": \"raw-v1\"") != NULL && strstr(js, "\"id\": \"stage-map\", \"files\": 3") != NULL &&
          strstr(js, "\"works\": \"yes\"") != NULL && strstr(js, "\"path\": \"dat/field/stage/s233tmpk.bin\", \"size\": 5000, \"kind\": \"stage-map\"") != NULL);
    CHECK(js != NULL && strstr(js, "mpbgm001") == NULL); /* the covered files are not listed */
    free(js);
    d = slurp("manifest.json", &n);
    CHECK(d != NULL && tdManifestParse((char *)d, n, &man, err, sizeof err) == 0 && !strcmp(man.topic, "raw") && man.exporter.count >= 1 && man.exporter.count == countKinds());
    CHECK(tdKvFind(&man.exporter, "stage-map") != NULL && tdManifestFind(&man, "kind/stage-map") == NULL && tdManifestFind(&man, stageMapPath) != NULL);
    tdManifestFree(&man);
    free(d);

    /* a second run: every kind is up to date, the disc is not read */
    reads = gReads;
    CHECK(rawExportRun("all") == countKinds());
    rawExportTotals(&t);
    CHECK(t.skipped == countKinds() && t.files == 0 && t.written == 0 && t.kept == 2 && gReads == reads);

    /* an edited file stays and the new version goes beside it; an unedited one is replaced; a missing one comes back.
       (a missing file makes its kind be looked at again) */
    {
        uint8_t mine[7] = {1, 2, 3, 4, 5, 6, 7};
        int a = fileIndex("AAA/DAT/FIELD/STAGE/S233TMPK.BIN"), b = fileIndex("AAA/DAT/FIELD/STAGE/S330TMPK.BIN"),
            c = fileIndex("AAA/DAT/FIELD/STAGE/S400TMPK.BIN");

        spit(stageMapPath, mine, sizeof mine);
        removeFile("dat/field/stage/s400tmpk.bin");
        putFile(a, 1); /* the disc's bytes changed (as if the exporter's input did) */
        putFile(b, 1);
        CHECK(rawExportRun("stage-map") == 1);
        rawExportTotals(&t);
        CHECK(t.skipped == 0 && t.files == 3 && t.newFiles == 1 && t.written == 2 && t.kept == 2 && t.failed == 0);
        d = slurp(stageMapPath, &n);
        CHECK(d != NULL && n == sizeof mine && !memcmp(d, mine, n)); /* the user's file is untouched */
        free(d);
        CHECK(sameAsDisc(stageMapPath, a, 1, ".new"));
        CHECK(sameAsDisc("dat/field/stage/s330tmpk.bin", b, 1, "")); /* unedited: replaced */
        CHECK(sameAsDisc("dat/field/stage/s400tmpk.bin", c, 0, "")); /* missing: back */
        CHECK(!exists("dat/field/stage/s330tmpk.bin.new"));
    }

    /* an older exporter version: the kind is built again, the rest stays up to date */
    {
        char mp[1300], *text;
        TdManifest m2;
        size_t ml;
        uint8_t *mt;

        snprintf(mp, sizeof mp, "%s/raw/manifest.json", dd);
        CHECK(tdReadFile(mp, &mt, &ml) == 0 && tdManifestParse((char *)mt, ml, &m2, err, sizeof err) == 0);
        free(mt);
        CHECK(tdKvSet(&m2.exporter, "battle-effect", "0") == 0);
        text = tdManifestFormat(&m2);
        CHECK(text != NULL && tdWriteFileAtomic(mp, text, strlen(text)) == 0);
        free(text);
        tdManifestFree(&m2);
        reads = gReads;
        CHECK(rawExportRun("all") == countKinds());
        rawExportTotals(&t);
        CHECK(t.skipped == countKinds() - 1 && t.files == 1 && t.kept == 1 + 2 && gReads > reads);
    }
    rmtree(gRoot);

    /* kinds by name: any case, unknown names ignored, the rest untouched */
    CHECK(scratch());
    dataDir(dd, sizeof dd);
    rawExportSetup(dd, &gIo);
    CHECK(rawExportRun("Stage-Map, bogus battle-effect") == 2);
    CHECK(exists("dat/field/stage/s233tmpk.bin") && exists("dat/fight/effect/e000com1.bin") && !exists("dat/training/tranin00.bin") &&
          !exists("pro/wstag233.pro") && exists("README.txt"));
    js = (char *)slurp("raw.json", &n);
    CHECK(js != NULL && strstr(js, "\"id\": \"training-data\"") != NULL); /* the notes describe the whole disc */
    free(js);
    CHECK(rawExportRun("filler") == 1 && exists("dat/none____.bin"));
    rmtree(gRoot);

    /* a sector that cannot be read: that file fails, the kind keeps no version mark and the next run builds it */
    CHECK(scratch());
    dataDir(dd, sizeof dd);
    rawExportSetup(dd, &gIo);
    gFailLba = gFiles[fileIndex("AAA/DAT/TRAINING/TRANIN00.BIN")].lba + 1;
    CHECK(rawExportRun("training-data, filler") == 2);
    rawExportTotals(&t);
    CHECK(t.failed == 1 && !exists("dat/training/tranin00.bin") && exists("dat/none____.bin"));
    gFailLba = -1;
    CHECK(rawExportRun("training-data, filler") == 2);
    rawExportTotals(&t);
    CHECK(t.failed == 0 && t.skipped == 1 && t.files == 1 && sameAsDisc("dat/training/tranin00.bin", fileIndex("AAA/DAT/TRAINING/TRANIN00.BIN"), 0, ""));
    rmtree(gRoot);

    /* a disc without any file of the kinds asked for */
    CHECK(scratch());
    dataDir(dd, sizeof dd);
    rawExportSetup(dd, &gIo);
    {
        static const RawExportIo none = {NULL, NULL, NULL};
        rawExportSetup(dd, &none);
        CHECK(rawExportRun("all") == -1);
    }
    rawExportSetup(NULL, NULL);
    rmtree(gRoot);
}

int main(void) {
    buildDisc();
    testKinds();
    testExport();
    if (failures) {
        printf("rawtest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("rawtest: OK\n");
    return 0;
}
