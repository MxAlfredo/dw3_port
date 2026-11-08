/* The map objects of the field stages by piece (readable-assets A6.2): see stage_obj_export.h. */
#include "stage_obj_export.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exp_core.h"
#include "image_data.h"
#include "map_layers.h"
#include "port_log.h"

#define TAG "stage objects"
#define DISC_ROOT "AAA/"
#define STAGE_DIR "AAA/DAT/FIELD/STAGE/"

/* ---- the pieces of one stage ---- */

int stageObjNumber(const char *discPath) {
    size_t n = strlen(discPath), dir = strlen(STAGE_DIR);
    const char *f;
    int i, num = 0;

    if (n != dir + 12 || strncmp(discPath, STAGE_DIR, dir) != 0) return -1;
    f = discPath + dir;
    if (f[0] != 'S' || (strcmp(f + 4, "MASK.BIN") != 0 && strcmp(f + 4, "TMPK.BIN") != 0)) return -1;
    for (i = 1; i <= 3; i++) {
        if (!isdigit((unsigned char)f[i])) return -1;
        num = num * 10 + (f[i] - '0');
    }
    return num;
}

int stageObjSheetRef(const char *json, size_t len, int *ref) {
    static const char key[] = "\"sheetEntry\":";
    const char *p = json, *end = json + len;
    char num[16];
    size_t n = 0;

    while (p + sizeof key - 1 <= end && memcmp(p, key, sizeof key - 1) != 0) p++;
    if (p + sizeof key - 1 > end) return -1;
    for (p += sizeof key - 1; p < end && *p == ' '; p++) {
    }
    if (p < end && *p == '-') num[n++] = *p++;
    while (p < end && isdigit((unsigned char)*p) && n + 1 < sizeof num) num[n++] = *p++;
    num[n] = '\0';
    if (n == 0 || (n == 1 && num[0] == '-')) return -1;
    *ref = atoi(num);
    return 0;
}

uint8_t *stageObjSheetFile(const uint8_t *tmpk, size_t tmpkLen, int entry, size_t *outLen) {
    const uint8_t *e;
    size_t n;
    uint8_t *out;

    if (mlArchiveEntry(tmpk, tmpkLen, entry, &e, &n) != ML_OK || e == NULL || n == 0) return NULL;
    out = (uint8_t *)malloc(4 + n);
    if (out == NULL) return NULL;
    out[0] = 4; /* the sheet starts right after this word: the layout of a *CS file, whose entry 0 is the sheet */
    out[1] = out[2] = out[3] = 0;
    memcpy(out + 4, e, n);
    if (imgSheetPartCount(out, 4 + n) == 0) { /* a sheet with parts but no frames (S376, S970: a header of two words) is one too */
        free(out);
        return NULL;
    }
    *outLen = 4 + n;
    return out;
}

static const char kStageNotes[] =
    "One PNG per piece: a rectangle of the stage's object art with the palette the game draws it with (16 colours, or 256 for mode 8bit; index 0 and every "
    "colour the disc leaves as 0x0000 are transparent). The PNG keeps the disc's index order: edit its pixels and colours, keep its size. A piece is replaced "
    "by another of the same size, drawn where the game draws this one. pages: the size of each page of the stage's MASK file in halfwords (width, height); the "
    "game loads them side by side, 64 halfwords per page. u and v are texels of that space (page is where the top-left corner falls: u / 256 for 4bit, u / 128 "
    "for 8bit); clut is the palette's column x (halfwords), row and page, the frame's CLUT row included. A row of 256 or more is a row of the CLUT of the "
    "MASK file's 8-bit image (row - 256), not of the pages. frames lists where the game draws the piece: the frame (its id in the sheet), the slot (the "
    "part's position in the frame: slot 0 is drawn last, on top), at (x, y) from the object's origin and the blend of the frame (none, or how it is mixed "
    "with what is behind: average, add, subtract, quarter). disc.sheetEntry is the entry of the TMPK file that holds the sheet (the stage's setup sets it; "
    "it is not always 0). Objects that cycle their CLUT row at run time are drawn with the row their frame names: the other rows of the cycle are not "
    "listed. outside: true means the rectangle reaches past the pages (those pixels are index 0). A piece with file null cannot be cut and says why; it is "
    "listed so no part of the sheet is dropped silently. A stage with unexported at the top has no pieces and says why. Information only: regenerated from "
    "the disc.";

static const char *noRoot(const char *discPath) { return strncmp(discPath, DISC_ROOT, 4) == 0 ? discPath + 4 : discPath; }

static void stageName(int stage, char *out, size_t cap) { snprintf(out, cap, "s%03d", stage); }

int stageObjExport(int stage, const char *maskDisc, const char *tmpkDisc, const uint8_t *mask, size_t maskLen, const uint8_t *tmpk, size_t tmpkLen, int entry,
                   SprEmitFn emit, void *ctx, MenuStats *st, const char **why) {
    char name[8], disc[320];
    MenuPieceDoc doc = {"stages-objects-v1", name, disc, NULL, "objects.json", kStageNotes};
    ImgPages *pages;
    uint8_t *sheet;
    size_t sheetLen = 0;
    int rc;

    stageName(stage, name, sizeof name);
    snprintf(disc, sizeof disc, "\"files\": [\"%s\", \"%s\"], \"sheetEntry\": %d", noRoot(maskDisc), noRoot(tmpkDisc), entry);
    pages = imgStagePagesOpen(mask, maskLen);
    sheet = stageObjSheetFile(tmpk, tmpkLen, entry, &sheetLen);
    if (pages == NULL) doc.unexported = "the MASK file has no image";
    else if (sheet == NULL) doc.unexported = "the entry of the TMPK file that should hold the sprite sheet is absent or is not one";
    else if (imgSheetFrameCount(sheet, sheetLen) == 0) doc.unexported = "the sprite sheet has parts but no frames, so nothing is drawn from it";
    if (why != NULL) *why = doc.unexported;
    rc = menuPiecesExport(pages, sheet, sheetLen, &doc, emit, ctx, st);
    imgPagesFree(pages);
    free(sheet);
    return rc;
}

int stageObjUnexported(int stage, const char *why, SprEmitFn emit, void *ctx) {
    char name[8];
    MenuPieceDoc doc = {"stages-objects-v1", name, "\"files\": []", why, "objects.json", kStageNotes};

    stageName(stage, name, sizeof name);
    return menuPiecesExport(NULL, NULL, 0, &doc, emit, ctx, NULL) < 0 ? -1 : 0;
}

/* ---- the disc side ---- */

typedef struct DiscFile {
    char path[112];
    long lba, size;
} DiscFile;

static char sDataDir[1024];
static StageObjIo sIo;
static StageObjStats sTotals;

void stageObjExportSetup(const char *dataDir, const StageObjIo *io) {
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

void stageObjExportTotals(StageObjStats *out) { *out = sTotals; }

static const DiscFile *findFile(const DiscFile *f, int n, const char *path) {
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(f[i].path, path) == 0) return &f[i];
    }
    return NULL;
}

static int cmpInt(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* the disc's files (malloc'ed in *files) and the numbers of the stages that have a MASK or a TMPK file, ascending (malloc'ed in *stages). -1 when memory ran out. */
static int discStages(DiscFile **files, int *nFiles, int **stages, int *nStages) {
    int cap = 4096, n = 0, ns = 0, i, k;
    DiscFile *all = (DiscFile *)malloc(sizeof *all * (size_t)cap);
    int *st;

    *files = NULL;
    *stages = NULL;
    *nFiles = *nStages = 0;
    if (all == NULL) return -1;
    for (i = 0; i < cap; i++) {
        DiscFile *x = &all[n];

        if (!sIo.disc.list(sIo.disc.ud, i, x->path, sizeof x->path, &x->lba, &x->size)) break;
        n++;
    }
    st = (int *)malloc(sizeof *st * (size_t)(n ? n : 1));
    if (st == NULL) {
        free(all);
        return -1;
    }
    for (i = 0; i < n; i++) {
        int num = stageObjNumber(all[i].path);

        if (num >= 0) st[ns++] = num;
    }
    qsort(st, (size_t)ns, sizeof *st, cmpInt);
    for (i = k = 0; i < ns; i++) {
        if (k == 0 || st[k - 1] != st[i]) st[k++] = st[i];
    }
    *files = all;
    *nFiles = n;
    *stages = st;
    *nStages = k;
    return 0;
}

/* "all", or stage names (s202, S202, 202) separated by commas or spaces: sets want[] and returns how many stages are wanted */
static int parseSpec(const char *spec, const int *stages, int n, char *want) {
    char tok[32];
    int named = 0, i;
    const char *s = spec;

    memset(want, 0, (size_t)n);
    while (*s) {
        size_t len = 0;
        int num;

        while (*s == ',' || *s == ' ') s++;
        while (*s && *s != ',' && *s != ' ' && len + 1 < sizeof tok) tok[len++] = (char)tolower((unsigned char)*s++);
        tok[len] = '\0';
        if (len == 0) break;
        if (strcmp(tok, "all") == 0) {
            memset(want, 1, (size_t)n);
            continue;
        }
        num = atoi(tok[0] == 's' ? tok + 1 : tok);
        for (i = 0; i < n && stages[i] != num; i++) {
        }
        if (i < n) want[i] = 1;
        else plogLine(TAG, "export_stage_objects: \"%s\" is not a stage of the disc (use all, or names like s202 separated by commas)\n", tok);
    }
    for (i = 0; i < n; i++) named += want[i];
    return named;
}

typedef struct Emit {
    const ExpSink *sink;
    char prefix[64];
    int failed;
} Emit;

static int emitFn(void *ctx, const char *rel, const uint8_t *data, size_t len) {
    Emit *e = (Emit *)ctx;
    char path[160];

    snprintf(path, sizeof path, "%s/%s", e->prefix, rel);
    if (expPut(e->sink, path, data, len)) {
        e->failed++;
        return -1;
    }
    return 0;
}

static const char kExporterKey[] = "stage-objects";

/* a run of the current version whose objects files are all there (stage_data.h keeps its s<nnn>/stage.json in the same manifest: not ours to check): nothing to build */
static int upToDate(const ExpManifest *man, const char *dir) {
    char path[1400];
    int i;

    if (!expManIsCurrent(man, kExporterKey, STAGE_OBJ_EXPORT_VERSION)) return 0;
    for (i = 0; i < man->m.count; i++) {
        if (strstr(man->m.files[i].path, "/objects/") == NULL) continue;
        snprintf(path, sizeof path, "%s/%s", dir, man->m.files[i].path);
        if (!expFileExists(path)) return 0;
    }
    return 1;
}

int stageObjSheetRefIn(const char *dataDir, int stage, int *ref) {
    char path[1200];
    uint8_t *text;
    size_t len;
    int rc;

    snprintf(path, sizeof path, "%s/stages/s%03d/stage.json", dataDir, stage);
    if (tdReadFile(path, &text, &len) != 0) return -1;
    rc = stageObjSheetRef((const char *)text, len, ref);
    free(text);
    return rc;
}

/* Cuts one stage from the disc. *why is NULL when it has pieces; else the reason it has none (a string that lives on). Returns -1 when something failed. */
static int exportStage(int stage, const DiscFile *all, int nAll, Emit *em, MenuStats *ms, const char **why) {
    char maskPath[112], tmpkPath[112];
    const DiscFile *mask, *tmpk;
    uint8_t *maskBytes, *tmpkBytes;
    int ref, rc;

    *why = NULL;
    snprintf(maskPath, sizeof maskPath, STAGE_DIR "S%03dMASK.BIN", stage);
    mask = findFile(all, nAll, maskPath);
    if (mask == NULL) *why = "the stage has no MASK file on the disc";
    else if (stageObjSheetRefIn(sDataDir, stage, &ref) != 0) *why = "no stage data file (stages/s<nnn>/stage.json) says which entry of the TMPK file is the sprite sheet";
    else if (ref == 0) *why = "the stage sets no sprite sheet (sheetEntry 0)";
    else if (sIo.fileName == NULL || !sIo.fileName(sIo.disc.ud, ref >> 16, tmpkPath, sizeof tmpkPath)) *why = "the sheet's file number is not in the disc's file table";
    else if ((tmpk = findFile(all, nAll, tmpkPath)) == NULL) *why = "the sheet's file is not on the disc";
    if (*why != NULL) return stageObjUnexported(stage, *why, emitFn, em);
    maskBytes = menuDiscRead(&sIo.disc, mask->lba, mask->size);
    tmpkBytes = menuDiscRead(&sIo.disc, tmpk->lba, tmpk->size);
    if (maskBytes == NULL || tmpkBytes == NULL) {
        free(maskBytes);
        free(tmpkBytes);
        return -1;
    }
    rc = stageObjExport(stage, mask->path, tmpk->path, maskBytes, (size_t)mask->size, tmpkBytes, (size_t)tmpk->size, ref & 0xFFFF, emitFn, em, ms, why);
    free(maskBytes);
    free(tmpkBytes);
    return rc;
}

int stageObjExportRun(const char *spec) {
    DiscFile *all = NULL;
    int *stages = NULL, nAll = 0, nStages = 0, i, nWant, rc = -1, wantAll, manLoaded = 0;
    char *want = NULL;
    ExpManifest man;
    ExpStats es;
    ExpSink sink;
    Emit em;
    MenuStats ms;
    char dir[1100], mp[1200];

    memset(&sTotals, 0, sizeof sTotals);
    memset(&ms, 0, sizeof ms);
    if (!sDataDir[0] || sIo.disc.list == NULL || sIo.disc.read == NULL || spec == NULL) return -1;
    if (discStages(&all, &nAll, &stages, &nStages) != 0 || nStages == 0) goto done;
    want = (char *)malloc((size_t)nStages);
    if (want == NULL) goto done;
    nWant = parseSpec(spec, stages, nStages, want);
    if (nWant == 0) goto done;
    wantAll = strstr(spec, "all") != NULL;
    snprintf(dir, sizeof dir, "%s/stages", sDataDir);
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    expManOpen(&man, TAG, mp, "stages");
    manLoaded = 1;
    if (upToDate(&man, dir)) {
        sTotals.skipped = 1;
        rc = 0;
        goto done;
    }
    memset(&es, 0, sizeof es);
    expSinkInPlace(&sink, TAG, dir, &man, &es);
    memset(&em, 0, sizeof em);
    em.sink = &sink;
    for (i = 0; i < nStages; i++) {
        const char *why = NULL;
        int n;

        if (!want[i]) continue;
        snprintf(em.prefix, sizeof em.prefix, "s%03d/objects", stages[i]);
        n = exportStage(stages[i], all, nAll, &em, &ms, &why);
        if (n < 0) {
            plogLine(TAG, "cannot cut the objects of stage %d into pieces\n", stages[i]);
            ms.failed++;
        } else if (why != NULL) {
            plogLine(TAG, "stage %d has no objects: %s\n", stages[i], why);
            sTotals.noObjects++;
        } else {
            sTotals.stages++;
        }
    }
    sTotals.failed = ms.failed;
    sTotals.written = es.written;
    sTotals.kept = es.kept;
    sTotals.newFiles = es.newFiles;
    sTotals.bytes = es.bytes;
    sTotals.pieces = ms.pieces;
    sTotals.unexported = ms.unexported;
    sTotals.outside = ms.outside;
    if (sTotals.failed == 0 && wantAll) expManMark(&man, kExporterKey, STAGE_OBJ_EXPORT_VERSION);
    if (expManSave(&man, 0)) sTotals.failed++;
    plogLine(TAG,
             "Done: %d stages in %s/stages with %d pieces (%d with pixels past their pages), %d pieces listed without a PNG, %d stages without objects, %d written, "
             "%d kept, %d new beside edited, %d failed, %ld bytes\n",
             sTotals.stages, sDataDir, sTotals.pieces, sTotals.outside, sTotals.unexported, sTotals.noObjects, sTotals.written, sTotals.kept, sTotals.newFiles,
             sTotals.failed, sTotals.bytes);
    rc = sTotals.stages + sTotals.noObjects;
done:
    if (manLoaded) expManFree(&man);
    free(want);
    free(all);
    free(stages);
    return rc;
}
