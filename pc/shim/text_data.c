/*
 * Text data folder and runtime override layer (feature es419-live-option, L2b). See text_data.h
 * for the layout. Pure part first (no I/O), then the runtime (extraction, mods, es-419 merge).
 */
#include "text_data.h"

#include "text_codec.h"
#include "talk_box_wide.h"
#include "text_fit.h"
#include "mods.h"
#include "port_fs.h"
#include "port_log.h"
#include "port_paths.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#endif

/* ---- index map ---- */

/* The 50 text files in file table order (the disc order of their sectors), without the language
   letter and the "S": the file is <letter>S<base>.BIN. Matches tools/es419/gen_table.py. */
static const char *const BASES[TD_FILES] = {
    "AMTMAP", "ASKMAP", "CARDGM", "CARDNM", "CARDST", "CRDABM", "CRDDEK", "CRDSHP", "DGLABO",
    "DGNMET", "DIGINF", "DIGNAM", "FGTRPT", "HTLNAM", "ITMINF", "ITMNAM", "ITSHOP", "MEMCRD",
    "MFIGHT", "NAMEDT", "NAMEET", "SHPNAM", "SKLINF", "SKLNAM", "STAREA", "STATUS", "STNAME",
    "SYSTEM", "TALK00", "TALK01", "TALK02", "TALK03", "TALK04", "TALK05", "TALK06", "TALK07",
    "TALK08", "TALK09", "TRAINI", "DMG200", "DMG260", "DMG270", "DMG300", "DMG305", "DMG400",
    "DMG500", "DMG600", "DMG700", "DMG800", "DMG900",
};

static const char *const LANG_DIRS[TD_LANGS] = {"eng", "fra", "ita", "ger", "spa", "es-419"};
static const char *const DISC_DIRS[TD_LANGS] = {"ENG", "FRA", "ITA", "GER", "SPN", "SPN"};
static const char LANG_LETTERS[TD_LANGS] = {'E', 'F', 'I', 'D', 'S', 'S'};
/* the slot of each language inside its row of 7 */
static const int LANG_SLOTS[TD_LANGS] = {3, 4, 5, 6, 7, 7};

const char *tdLangDir(int lang) { return (lang >= 0 && lang < TD_LANGS) ? LANG_DIRS[lang] : NULL; }
const char *tdDiscDir(int lang) { return (lang >= 0 && lang < TD_LANGS) ? DISC_DIRS[lang] : NULL; }
const char *tdFileBase(int k) { return (k >= 0 && k < TD_FILES) ? BASES[k] : NULL; }

int tdFileName(int lang, int k, char *buf, size_t cap)
{
    int n;
    if (lang < 0 || lang >= TD_LANGS || k < 0 || k >= TD_FILES) return -1;
    n = snprintf(buf, cap, "%cS%s.BIN", LANG_LETTERS[lang], BASES[k]);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

int tdIndexOf(int lang, int k)
{
    if (lang < 0 || lang >= TD_LANGS || k < 0 || k >= TD_FILES) return -1;
    return LANG_SLOTS[lang] + 7 * k;
}

int tdFromIndex(int index, int *lang, int *k)
{
    int slot, kk, l;
    if (index < 1 || index > TD_MAX_INDEX) return 0;
    slot = (index - 1) % 7 + 1;
    kk = (index - slot) / 7;
    for (l = 0; l < TD_REAL_LANGS; l++) {
        if (LANG_SLOTS[l] == slot) {
            if (lang) *lang = l;
            if (k) *k = kk;
            return 1;
        }
    }
    return 0;
}

/* ---- hash ---- */

uint64_t tdHashUpdate(uint64_t h, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

uint64_t tdHash(const void *data, size_t len) { return tdHashUpdate(TD_HASH_INIT, data, len); }

void tdHashHex(uint64_t h, char out[17])
{
    static const char hexd[] = "0123456789abcdef";
    int i;
    for (i = 15; i >= 0; i--) {
        out[i] = hexd[h & 15];
        h >>= 4;
    }
    out[16] = 0;
}

/* ---- manifest ---- */

void tdMakeStamp(char *buf, size_t cap, const char *discHash, const char *overlayHash)
{
    snprintf(buf, cap, "disc=%s;overlay=%s;codec=%d", discHash, overlayHash, TD_CODEC_VERSION);
}

void tdManifestFree(TdManifest *m)
{
    free(m->files);
    m->files = NULL;
    m->count = 0;
}

const char *tdManifestFind(const TdManifest *m, const char *path)
{
    int i;
    for (i = 0; i < m->count; i++) {
        if (!strcmp(m->files[i].path, path)) return m->files[i].hash;
    }
    return NULL;
}

int tdManifestSet(TdManifest *m, const char *path, const char *hash)
{
    int i;
    TdManifestFile *f;
    if (strlen(path) >= sizeof f->path || strlen(hash) >= sizeof f->hash) return -1;
    for (i = 0; i < m->count; i++) {
        if (!strcmp(m->files[i].path, path)) {
            strcpy(m->files[i].hash, hash);
            return 0;
        }
    }
    f = (TdManifestFile *)realloc(m->files, (size_t)(m->count + 1) * sizeof *f);
    if (!f) return -1;
    m->files = f;
    strcpy(f[m->count].path, path);
    strcpy(f[m->count].hash, hash);
    m->count++;
    return 0;
}

static void errf(char *err, size_t size, const char *msg)
{
    if (err && size) snprintf(err, size, "%s", msg);
}

/* Next JSON string token (no escapes: the manifest only holds plain names and hashes).
   Returns 1 and advances *i, 0 at the end, -1 on a bad token. */
static int nextToken(const char *s, size_t len, size_t *i, char *out, size_t cap)
{
    size_t j;
    size_t n = 0;
    while (*i < len && s[*i] != '"') {
        char c = s[*i];
        if (!strchr("{}[]:, \t\r\n", c)) return -1; /* numbers etc. are not in the format */
        (*i)++;
    }
    if (*i >= len) return 0;
    j = *i + 1;
    while (j < len && s[j] != '"') {
        if (s[j] == '\\' || (unsigned char)s[j] < 0x20 || n + 1 >= cap) return -1;
        out[n++] = s[j++];
    }
    if (j >= len) return -1;
    out[n] = 0;
    *i = j + 1;
    return 1;
}

int tdManifestParse(const char *json, size_t len, TdManifest *m, char *err, size_t errSize)
{
    size_t i = 0;
    char tok[300], key[300];
    int r;
    memset(m, 0, sizeof *m);
    r = nextToken(json, len, &i, tok, sizeof tok);
    if (r != 1 || strcmp(tok, "stamp")) { errf(err, errSize, "manifest: expected \"stamp\" first"); goto bad; }
    r = nextToken(json, len, &i, tok, sizeof tok);
    if (r != 1 || strlen(tok) >= sizeof m->stamp) { errf(err, errSize, "manifest: bad stamp"); goto bad; }
    strcpy(m->stamp, tok);
    r = nextToken(json, len, &i, tok, sizeof tok);
    if (r != 1 || strcmp(tok, "files")) { errf(err, errSize, "manifest: expected \"files\""); goto bad; }
    for (;;) {
        r = nextToken(json, len, &i, key, sizeof key);
        if (r == 0) break;
        if (r < 0) { errf(err, errSize, "manifest: bad token"); goto bad; }
        r = nextToken(json, len, &i, tok, sizeof tok);
        if (r != 1 || tdManifestSet(m, key, tok)) { errf(err, errSize, "manifest: bad file entry"); goto bad; }
    }
    return 0;
bad:
    tdManifestFree(m);
    m->stamp[0] = 0;
    return -1;
}

char *tdManifestFormat(const TdManifest *m)
{
    size_t cap = 512 + (size_t)m->count * 120;
    char *out = (char *)malloc(cap);
    size_t n;
    int i;
    if (!out) return NULL;
    n = (size_t)snprintf(out, cap, "{\n  \"stamp\": \"%s\",\n  \"files\": {\n", m->stamp);
    for (i = 0; i < m->count; i++) {
        n += (size_t)snprintf(out + n, cap - n, "    \"%s\": \"%s\"%s\n", m->files[i].path, m->files[i].hash,
                              i + 1 < m->count ? "," : "");
    }
    snprintf(out + n, cap - n, "  }\n}\n");
    return out;
}

/* ---- decisions ---- */

TdFileState tdFileState(int exists, const char *manifestHash, const char *currentHash)
{
    if (!exists) return TD_FILE_MISSING;
    if (manifestHash && currentHash && !strcmp(manifestHash, currentHash)) return TD_FILE_PRISTINE;
    return TD_FILE_EDITED;
}

TdExtractAction tdExtractAction(int stampMatches, TdFileState state)
{
    if (state == TD_FILE_EDITED) return TD_X_WRITE_NEW;
    if (state == TD_FILE_PRISTINE && stampMatches) return TD_X_SKIP;
    return TD_X_WRITE;
}

int tdSourceOrder(int modExists, TdFileState dataState, int es419Active, TdSource out[3])
{
    int n = 0;
    if (modExists) out[n++] = TD_SRC_MOD;
    if (dataState == TD_FILE_EDITED) out[n++] = TD_SRC_DATA;
    if (es419Active) out[n++] = TD_SRC_ES419;
    return n;
}

/* ---- build ---- */

static int docTotal(const TcDoc *doc, int *partBase)
{
    int total = 0, p;
    if (!doc->isPack) return doc->lang.count;
    for (p = 0; p < doc->partCount; p++) {
        partBase[p] = total;
        if (doc->parts[p].isLang) total += doc->parts[p].lang.count;
    }
    return total;
}

static TrLayout gLayout;
static TrOptions gOpt;
static int gReflowOn;
static uint64_t gLayoutHash;
static int gTalkWidth = TBW_ORIGINAL;

void tdSetTalkBoxWidth(int px)
{
    gTalkWidth = (px >= TBW_ORIGINAL && px <= TBW_LIMIT) ? px : TBW_ORIGINAL;
}

int tdTalkBoxWidth(void) { return gTalkWidth; }

/* DW3_TALK_BOX_FLIP=1: a capture aid, the field's left-growing talk boxes are made right-growing */
int tdTalkBoxFlip(void)
{
    const char *e = getenv("DW3_TALK_BOX_FLIP");
    return e && e[0] && e[0] != '0';
}

/* DW3_TALK_BOX_LOG=1: one line per talk box created (type, position and size), for captures and checks */
void tdTalkBoxLog(int type, int x, int y, int w, int h)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("DW3_TALK_BOX_LOG");
        on = e && e[0] && e[0] != '0';
    }
    if (on) fprintf(stderr, "talkbox: type=%d x=%d y=%d w=%d h=%d\n", type, x, y, w, h);
}

/* L4d: the glyph scale of the one-line fields is OFF (owner's decision); DW3_TEXT_FIT=scale turns it on
   (a capture aid for future experiments) */
int tdTextFitOn(void)
{
    static int on = -1;
    if (on < 0)
        on = tfScaleMode(getenv("DW3_TEXT_FIT"));
    return on;
}

/* L4c: DW3_TEXT_FIT_GRID=<px> widens the left item-grid columns' budget (a capture aid) */
int tdTextFitGrid(void)
{
    static int px = -1;
    if (px < 0) {
        const char *e = getenv("DW3_TEXT_FIT_GRID");
        px = e ? atoi(e) : 0;
        if (px < 0) px = 0;
    }
    return px;
}

/* L4c: DW3_TEXT_FIT_SEED=1 puts some of the longest-named items in the bag when the pause menu's item
   grid opens (a capture aid: the mode: harness starts the pause menu with an empty bag) */
int tdTextFitSeed(void)
{
    const char *e = getenv("DW3_TEXT_FIT_SEED");
    return e && e[0] && e[0] != '0';
}

/* L4c: DW3_TEXT_FIT_LOG=1: one line each time a fitted field's width or scale changes */
void tdTextFitLog(int screen, int x, int y, int w, int budget, int scale)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("DW3_TEXT_FIT_LOG");
        on = e && e[0] && e[0] != '0';
    }
    if (on) fprintf(stderr, "textfit: screen=%d x=%d y=%d w=%d budget=%d scale=%d%s\n", screen, x, y, w, budget, scale,
                    scale < 0x1000 ? " SCALED" : "");
}

static int readAll(const char *path, uint8_t **data, size_t *len);

static void talkWidthFromEnvOrFile(void)
{
    const char *env = getenv("DW3_TALK_BOX_WIDTH");
    int w = 0;
    if (env && env[0]) {
        w = tbwParseWidth(env);
        if (!w) fprintf(stderr, "text: DW3_TALK_BOX_WIDTH=%s not understood (original or 139..280)\n", env);
    } else {
        char path[1100];
        uint8_t *d;
        size_t n;
        if (portPath(PP_SETTINGS, path, (int)sizeof path) && readAll(path, &d, &n) == 0) {
            w = tbwScanSettings((const char *)d, n);
            free(d);
        }
    }
    tdSetTalkBoxWidth(w ? w : TBW_DEFAULT);
    if (gTalkWidth != TBW_ORIGINAL) fprintf(stderr, "text: talk box width %d px\n", gTalkWidth);
}

void tdReflowOff(void)
{
    trLayoutFree(&gLayout);
    gReflowOn = 0;
    gLayoutHash = 0;
}

int tdReflowActive(void) { return gReflowOn; }

int tdReflowConfigure(const char *layoutJson, size_t len, const TrOptions *metrics)
{
    char err[200];
    tdReflowOff();
    if (trLayoutParse(layoutJson, len, &gLayout, err, sizeof err)) {
        fprintf(stderr, "text: layout.json rejected (%s); no reflow\n", err);
        return -1;
    }
    if (metrics) {
        gOpt = *metrics;
    } else if (!trEngineMetrics(&gOpt)) {
        fprintf(stderr, "text: the game's glyph tables are not linked; no reflow\n");
        trLayoutFree(&gLayout);
        return -1;
    }
    gLayoutHash = tdHash(layoutJson, len);
    if (gTalkWidth > TBW_ORIGINAL) { /* L4b: the talk-box budget is the player's choice */
        int f;
        for (f = 0; f < gLayout.count; f++)
            if (gLayout.files[f].tightWidth > 0 && gTalkWidth > gLayout.files[f].tightWidth) gLayout.files[f].tightBudget = gTalkWidth;
        gLayoutHash = tdHashUpdate(gLayoutHash, &gTalkWidth, sizeof gTalkWidth);
    }
    gReflowOn = 1;
    return 0;
}

int tdBuild(const uint8_t *base, size_t baseLen, const char *json, size_t jsonLen, int requireFull,
            uint8_t **out, size_t *outLen, char *err, size_t errSize)
{
    return tdBuildFile(NULL, NULL, base, baseLen, json, jsonLen, requireFull, out, outLen, err, errSize);
}

int tdBuildFile(const char *fileBase, const char *label, const uint8_t *base, size_t baseLen, const char *json,
                size_t jsonLen, int requireFull, uint8_t **out, size_t *outLen, char *err, size_t errSize)
{
    TcDoc doc;
    TcJson j;
    int *partBase = NULL;
    unsigned char *seen = NULL;
    int total, i, rc = -1;

    memset(&j, 0, sizeof j);
    if (tcDocParse(base, baseLen, &doc, err, errSize)) return -1;
    if (tcJsonParse(json, jsonLen, &j, err, errSize)) goto done;
    partBase = (int *)calloc((size_t)(doc.isPack ? doc.partCount : 1) + 1, sizeof *partBase);
    total = partBase ? docTotal(&doc, partBase) : 0;
    if (!partBase) { errf(err, errSize, "out of memory"); goto done; }
    if (requireFull) {
        if (j.count != total) {
            if (err && errSize) snprintf(err, errSize, "entry count %d differs from the disc's %d", j.count, total);
            goto done;
        }
        seen = (unsigned char *)calloc((size_t)total + 1, 1);
        if (!seen) { errf(err, errSize, "out of memory"); goto done; }
        for (i = 0; i < j.count; i++) {
            const TcJsonString *s = &j.strings[i];
            TcLang *l = tcDocLang(&doc, s->part);
            int at;
            if (!l) {
                if (err && errSize) snprintf(err, errSize, "part=%d index=%d: part does not fit this file", s->part, s->index);
                goto done;
            }
            if (s->index < 0 || s->index >= l->count) {
                if (err && errSize) snprintf(err, errSize, "part=%d index=%d: index out of range (0..%d)", s->part, s->index, l->count - 1);
                goto done;
            }
            at = (doc.isPack ? partBase[s->part] : 0) + s->index;
            if (seen[at]) {
                if (err && errSize) snprintf(err, errSize, "part=%d index=%d: listed twice", s->part, s->index);
                goto done;
            }
            seen[at] = 1;
        }
    }
    if (tcApplyJson(&doc, &j, err, errSize)) goto done;
    if (gReflowOn && fileBase) {
        const TrFileRule *rule = trLayoutFind(&gLayout, fileBase);
        if (rule) {
            TcDoc orig;
            int rr;
            if (tcDocParse(base, baseLen, &orig, err, errSize)) goto done;
            rr = trReflowDoc(&doc, &orig, &gOpt, rule, label ? label : fileBase, NULL, err, errSize);
            tcDocFree(&orig);
            if (rr) goto done;
        }
    }
    *out = tcDocBuild(&doc, outLen);
    if (!*out) { errf(err, errSize, "out of memory"); goto done; }
    rc = 0;
done:
    free(seen);
    free(partBase);
    tcJsonFree(&j);
    tcDocFree(&doc);
    return rc;
}

/* ======================================================================================== */
/* ---- runtime ---- */

static int gVariant = TD_VARIANT_SPA;
void tdSetVariant(int variant) { gVariant = variant == TD_VARIANT_ES419 ? TD_VARIANT_ES419 : TD_VARIANT_SPA; }
int tdGetVariant(void) { return gVariant; }

static TdDisc gDisc;
static int gHaveDisc;
static char gData[1024];
static int gHaveData;

void tdSetDataDir(const char *dataDir)
{
    gHaveData = dataDir && dataDir[0] && strlen(dataDir) < sizeof gData;
    if (gHaveData) snprintf(gData, sizeof gData, "%s", dataDir);
}
static char gL10n[1024];
static int gHaveL10n;
static TdManifest gManifest;
static long gSize[TD_MAX_INDEX + 1]; /* the exact byte size of each text file, -1 unknown */

int tdEs419Available(void) { return gHaveDisc && gHaveL10n; }

/* The text channel at trace (DW3_TEXT_TRACE=1 is the shortcut): which source served each text file */
int tdTraceEnabled(void)
{
    return plogOn("text", PLOG_TRACE);
}

void tdLogServed(int index, const char *source, const void *data, size_t len)
{
    char hex[17];
    tdHashHex(tdHash(data, len), hex);
    plogLine("text", "served idx=%d src=%s bytes=%zu hash=%s\n", index, source, len, hex);
}

static double nowSeconds(void)
{
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static int readAll(const char *path, uint8_t **data, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *b;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return -1; }
    b = (uint8_t *)malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return -1; }
    fclose(f);
    b[n] = 0; /* a NUL after the data, for the JSON readers */
    *data = b;
    *len = (size_t)n;
    return 0;
}

static int mkdirs(const char *path)
{
    return portMkdirs(path) ? 0 : -1;
}

static int writeAtomic(const char *path, const void *data, size_t len)
{
    char tmp[1100];
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, len, f) != len) { fclose(f); remove(tmp); return -1; }
    if (fclose(f)) { remove(tmp); return -1; }
    if (rename(tmp, path)) {
#ifdef _WIN32
        remove(path);
        if (rename(tmp, path) == 0) return 0;
#endif
        remove(tmp);
        return -1;
    }
    return 0;
}

int tdReadFile(const char *path, uint8_t **data, size_t *len) { return readAll(path, data, len); }
int tdMakeDirs(const char *path) { return mkdirs(path); }
int tdWriteFileAtomic(const char *path, const void *data, size_t len) { return writeAtomic(path, data, len); }

static long mtimeOf(const char *path, int *exists)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        *exists = 0;
        return 0;
    }
    *exists = 1;
    return (long)st.st_mtime;
}

static int isDir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ---- the disc: exact file sizes from the ISO9660 directories (walked once by port_fs) ---- */

/* Fill gSize for every text file: find AAA/DAT/COUNTRY/<LANG>/<name> in the disc directory of port_fs and
   check each extent against the file table. */
static int discSizes(void)
{
    int l, k, i, ok = 0;

    for (i = 0; i <= TD_MAX_INDEX; i++) gSize[i] = -1;
    if (!pfsDiscIsOpen()) return -1;
    for (l = 0; l < TD_REAL_LANGS; l++) {
        for (k = 0; k < TD_FILES; k++) {
            char want[32], path[96];
            const PfsFile *f;
            long lba = 0;
            int sectors = 0, idx = tdIndexOf(l, k);

            tdFileName(l, k, want, sizeof want);
            snprintf(path, sizeof path, "AAA/DAT/COUNTRY/%s/%s", DISC_DIRS[l], want);
            f = pfsFindPath(path);
            if (f && gDisc.locate(gDisc.ctx, idx, &lba, &sectors) && lba == f->lba) {
                gSize[idx] = f->size;
                ok++;
            }
        }
    }
    return ok == TD_REAL_LANGS * TD_FILES ? 0 : -1;
}

/* A text file's exact bytes (malloc'ed) */
static int discLoad(int index, uint8_t **data, size_t *len)
{
    long lba = 0, size;
    int sectors = 0;
    uint8_t *b;
    if (!gHaveDisc || index < 1 || index > TD_MAX_INDEX || gSize[index] < 0) return -1;
    if (!gDisc.locate(gDisc.ctx, index, &lba, &sectors)) return -1;
    size = gSize[index];
    b = pfsDiscRead(lba, (size_t)size);
    if (!b) return -1;
    *data = b;
    *len = (size_t)size;
    return 0;
}

/* ---- paths ---- */

/* ADR-010 section 3: everything the port writes is lower case (the mods may spell it any way: they are read through
   portPathsResolveCI) */
static void lowerCase(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void dataPath(char *buf, size_t cap, const char *sub, int lang, int k, const char *ext)
{
    char name[32];
    tdFileName(lang, k, name, sizeof name);
    name[strlen(name) - 4] = 0; /* ".BIN" */
    lowerCase(name);
    snprintf(buf, cap, "%s/%s/%s/%s%s", gData, sub, tdLangDir(lang), name, ext);
}

#define MAX_MOD_FILES TD_MOD_FILES_MAX

/* The mod files of one text file that exist, the winning one first: the mods from the last to the
   first (ADR-005). *mix is the sum of their mtimes, so a change in any of them is noticed. Returns how many. */
int tdModFiles(int lang, int k, char paths[][1200], long *mix)
{
    char name[32], rel[100];
    int n = 0, i, exists;
    long m, sum = 0;

    tdFileName(lang, k, name, sizeof name);
    name[strlen(name) - 4] = 0;
    lowerCase(name);
    snprintf(rel, sizeof rel, "text/%s/%s.json", tdLangDir(lang), name);
    for (i = modsCount() - 1; i >= 0; i--) {
        if (!portPathsResolveCI(modsDirOf(i), rel, paths[n], 1200)) continue;
        m = mtimeOf(paths[n], &exists);
        if (exists) {
            sum += m;
            n++;
        }
    }
    *mix = sum;
    return n;
}

static int overlayLoad(int k, uint8_t **data, size_t *len)
{
    char name[32], path[1200];
    if (!gHaveL10n) return -1;
    tdFileName(TD_SPA, k, name, sizeof name);
    name[strlen(name) - 4] = 0;
    snprintf(path, sizeof path, "%s/%s.json", gL10n, name);
    return readAll(path, data, len);
}

/* ---- extraction ---- */

static void discIdentity(char out[17])
{
    uint64_t h = pfsDiscHash(0); /* the volume descriptor */
    int l;
    for (l = 0; l < TD_REAL_LANGS; l++) {
        uint8_t *d;
        size_t n;
        if (discLoad(tdIndexOf(l, 0), &d, &n) == 0) {
            h = tdHashUpdate(h, d, n);
            free(d);
        }
    }
    tdHashHex(h, out);
}

static void overlayIdentity(char out[17])
{
    uint64_t h = TD_HASH_INIT;
    int k, any = 0;
    if (!gHaveL10n) {
        snprintf(out, 17, "none");
        return;
    }
    for (k = 0; k < TD_FILES; k++) {
        uint8_t *d;
        size_t n;
        if (overlayLoad(k, &d, &n) == 0) {
            h = tdHashUpdate(h, tdFileBase(k), strlen(tdFileBase(k)));
            h = tdHashUpdate(h, d, n);
            free(d);
            any = 1;
        }
    }
    if (gReflowOn) { /* the budgets change what the generated es-419 files hold */
        h = tdHashUpdate(h, "reflow", 6);
        h = tdHashUpdate(h, &gLayoutHash, sizeof gLayoutHash);
    }
    if (!any) snprintf(out, 17, "none");
    else tdHashHex(h, out);
}

typedef struct Extract {
    int written, kept, newFiles, failed;
    int stampMatches;
    TdManifest next; /* the manifest we are building */
} Extract;

/* Handle one generated file: decide, write, record */
static void emit(Extract *x, int lang, int k, const char *fileName, const char *json)
{
    char rel[100], path[1200], hex[17], curHex[17];
    uint8_t *cur = NULL;
    size_t curLen = 0, jl = strlen(json);
    int exists = 0;
    const char *man;
    TdFileState st;
    TdExtractAction act;
    char dir[1200];

    snprintf(rel, sizeof rel, "%s/%.*s.json", tdLangDir(lang), (int)strlen(fileName) - 4, fileName);
    lowerCase(rel);
    snprintf(path, sizeof path, "%s/text/%s", gData, rel);
    tdHashHex(tdHash(json, jl), hex);
    if (readAll(path, &cur, &curLen) == 0) {
        exists = 1;
        tdHashHex(tdHash(cur, curLen), curHex);
        free(cur);
    } else {
        curHex[0] = 0;
    }
    man = tdManifestFind(&gManifest, rel);
    st = tdFileState(exists, man, exists ? curHex : NULL);
    act = tdExtractAction(x->stampMatches, st);
    (void)k;
    if (act == TD_X_SKIP) {
        x->kept++;
        tdManifestSet(&x->next, rel, man);
        return;
    }
    snprintf(dir, sizeof dir, "%s/text/%s", gData, tdLangDir(lang));
    if (mkdirs(dir)) {
        fprintf(stderr, "text: cannot create %s\n", dir);
        x->failed++;
        return;
    }
    if (act == TD_X_WRITE_NEW) {
        char np[1210];
        snprintf(np, sizeof np, "%s.new", path);
        if (writeAtomic(np, json, jl)) x->failed++;
        else {
            x->newFiles++;
            fprintf(stderr, "text: %s was edited, kept; the new version is %s.new\n", rel, rel);
        }
        if (man) tdManifestSet(&x->next, rel, man);
        return;
    }
    if (exists && !strcmp(curHex, hex)) {
        x->kept++; /* already what we would write */
    } else if (writeAtomic(path, json, jl)) {
        fprintf(stderr, "text: cannot write %s\n", path);
        x->failed++;
        return;
    } else {
        x->written++;
    }
    tdManifestSet(&x->next, rel, hex);
}

static int extractAll(Extract *x)
{
    int l, k;
    for (k = 0; k < TD_FILES; k++) {
        TcDoc spa, spaBase;
        int haveSpa = 0, haveBase = 0;
        memset(&spa, 0, sizeof spa);
        memset(&spaBase, 0, sizeof spaBase);
        for (l = 0; l < TD_REAL_LANGS; l++) {
            uint8_t *d;
            size_t n;
            TcDoc doc;
            char name[32], err[200];
            char *json;
            tdFileName(l, k, name, sizeof name);
            if (discLoad(tdIndexOf(l, k), &d, &n)) {
                fprintf(stderr, "text: cannot read %s from the disc\n", name);
                x->failed++;
                continue;
            }
            if (tcDocParse(d, n, &doc, err, sizeof err)) {
                fprintf(stderr, "text: %s is not a lang file: %s\n", name, err);
                free(d);
                x->failed++;
                continue;
            }
            if (l == TD_SPA && gReflowOn) {
                /* the untouched Spanish: the reflow leaves its text alone and classes the boxes by it */
                haveBase = tcDocParse(d, n, &spaBase, err, sizeof err) == 0;
            }
            free(d);
            json = tcDocToJson(&doc, name);
            if (json) {
                emit(x, l, k, name, json);
                free(json);
            } else {
                x->failed++;
            }
            if (l == TD_SPA) {
                spa = doc; /* keep it for the es-419 merge below */
                haveSpa = 1;
            } else {
                tcDocFree(&doc);
            }
        }
        if (haveSpa) {
            uint8_t *od;
            size_t on;
            char name[32], err[200];
            if (overlayLoad(k, &od, &on) == 0) {
                TcJson oj;
                if (tcJsonParse((const char *)od, on, &oj, err, sizeof err) == 0) {
                    if (tcApplyJson(&spa, &oj, err, sizeof err) == 0) {
                        char *json;
                        tdFileName(TD_ES419, k, name, sizeof name);
                        if (gReflowOn && haveBase) {
                            const TrFileRule *rule = trLayoutFind(&gLayout, tdFileBase(k));
                            char label[64];
                            snprintf(label, sizeof label, "%s/%s", tdLangDir(TD_ES419), name);
                            if (rule && trReflowDoc(&spa, &spaBase, &gOpt, rule, label, NULL, err, sizeof err)) {
                                fprintf(stderr, "text: reflow of %s failed (%s); written as merged\n", label, err);
                            }
                        }
                        json = tcDocToJson(&spa, name);
                        if (json) {
                            emit(x, TD_ES419, k, name, json);
                            free(json);
                        } else {
                            x->failed++;
                        }
                    } else {
                        fprintf(stderr, "text: overlay %s does not apply: %s\n", tdFileBase(k), err);
                        x->failed++;
                    }
                    tcJsonFree(&oj);
                } else {
                    fprintf(stderr, "text: overlay %s is not valid: %s\n", tdFileBase(k), err);
                    x->failed++;
                }
                free(od);
            }
            tcDocFree(&spa);
        }
        if (haveBase) tcDocFree(&spaBase);
    }
    return 0;
}

int tdStart(const TdDisc *disc, const char *dataDir, const char *l10nDir)
{
    double t0 = nowSeconds();
    char discHex[17], ovHex[17], stamp[256], path[1200];
    uint8_t *mtxt = NULL;
    size_t mlen = 0;
    int haveOld = 0, stampOk, result = 0;
    Extract x;

    memset(&gManifest, 0, sizeof gManifest);
    gHaveData = gHaveL10n = gHaveDisc = 0;
    if (disc) {
        gDisc = *disc;
        gHaveDisc = 1;
    }
    if (dataDir && dataDir[0] && strlen(dataDir) < sizeof gData) {
        snprintf(gData, sizeof gData, "%s", dataDir);
        gHaveData = 1;
    }
    if (l10nDir && l10nDir[0] && strlen(l10nDir) < sizeof gL10n && isDir(l10nDir)) {
        snprintf(gL10n, sizeof gL10n, "%s", l10nDir);
        gHaveL10n = 1;
    } else if (l10nDir && l10nDir[0]) {
        fprintf(stderr, "text: overlay folder %s not found, es-419 is not available\n", l10nDir);
    }
    tdReflowOff();
    talkWidthFromEnvOrFile();
    if (gHaveL10n) {
        const char *off = getenv("DW3_TEXT_REFLOW");
        if (off && off[0] == '0') {
            fprintf(stderr, "text: reflow off (DW3_TEXT_REFLOW=0)\n");
        } else {
            char lp[1300];
            uint8_t *lj;
            size_t ll;
            snprintf(lp, sizeof lp, "%s/../layout.json", gL10n);
            if (readAll(lp, &lj, &ll) == 0) {
                if (tdReflowConfigure((const char *)lj, ll, NULL) == 0) {
                    fprintf(stderr, "text: reflow budgets from %s (%d file rules)\n", lp, gLayout.count);
                }
                free(lj);
            } else {
                fprintf(stderr, "text: no layout.json beside the overlay; no reflow\n");
            }
        }
    }
    if (!gHaveDisc) return -1;
    if (discSizes()) {
        fprintf(stderr, "text: could not size the text files from the disc directories; text data off\n");
        gHaveData = 0;
        gHaveL10n = 0;
        return -1;
    }
    if (!gHaveData) return 0;

    discIdentity(discHex);
    overlayIdentity(ovHex);
    tdMakeStamp(stamp, sizeof stamp, discHex, ovHex);

    snprintf(path, sizeof path, "%s/text/manifest.json", gData);
    if (readAll(path, &mtxt, &mlen) == 0) {
        char err[120];
        if (tdManifestParse((const char *)mtxt, mlen, &gManifest, err, sizeof err) == 0) haveOld = 1;
        else fprintf(stderr, "text: manifest unreadable (%s); regenerating (edited files are kept)\n", err);
        free(mtxt);
    }
    snprintf(path, sizeof path, "%s/text", gData);
    stampOk = haveOld && !strcmp(gManifest.stamp, stamp) && isDir(path);
    if (stampOk) {
        fprintf(stderr, "text: data folder %s is current (stamp ok), %d files, %.3f s\n", gData, gManifest.count,
                nowSeconds() - t0);
        return 0;
    }

    memset(&x, 0, sizeof x);
    x.stampMatches = haveOld && !strcmp(gManifest.stamp, stamp);
    snprintf(x.next.stamp, sizeof x.next.stamp, "%s", stamp);
    extractAll(&x);
    {
        /* what this run did not regenerate (es-419 without the overlay folder, a file that failed)
           keeps its old entry: its file on disk is still ours, not the user's */
        int i;
        for (i = 0; i < gManifest.count; i++) {
            if (!tdManifestFind(&x.next, gManifest.files[i].path)) {
                tdManifestSet(&x.next, gManifest.files[i].path, gManifest.files[i].hash);
            }
        }
    }
    {
        char *text = tdManifestFormat(&x.next);
        char dir[1200];
        snprintf(dir, sizeof dir, "%s/text", gData);
        if (!text || mkdirs(dir) || (snprintf(path, sizeof path, "%s/text/manifest.json", gData), writeAtomic(path, text, strlen(text)))) {
            fprintf(stderr, "text: cannot write the manifest in %s\n", dir);
            x.failed++;
        }
        free(text);
    }
    tdManifestFree(&gManifest);
    gManifest = x.next;
    result = x.failed ? -1 : x.written;
    fprintf(stderr, "text: extracted %d files (%d kept, %d new beside edited, %d failed) into %s/text in %.3f s\n",
            x.written, x.kept, x.newFiles, x.failed, gData, nowSeconds() - t0);
    return result;
}

/* ---- serving ---- */

typedef struct Slot {
    int init;
    int variant;
    long modM, dataM;
    int modExists, dataExists;
    double checked;
    uint8_t *bytes;
    size_t len;
} Slot;

static Slot gSlots[TD_MAX_INDEX + 1];

/* Build the file from `source`; 0 on success with bytes in the slot */
static int tryBuild(Slot *s, int index, int lang, int k, TdSource src, const char *modFile)
{
    char path[1200], err[300], name[32];
    uint8_t *base = NULL, *js = NULL, *out = NULL;
    size_t baseLen = 0, jl = 0, outLen = 0;
    int rc, full = (src != TD_SRC_ES419);

    tdFileName(lang, k, name, sizeof name);
    if (discLoad(index, &base, &baseLen)) {
        fprintf(stderr, "text: %s: cannot read the disc file; serving the disc\n", name);
        return -1;
    }
    if (src == TD_SRC_MOD) snprintf(path, sizeof path, "%s", modFile);
    else if (src == TD_SRC_DATA) dataPath(path, sizeof path, "text", lang, k, ".json");
    else { /* the overlay, applied on the Spanish disc file */
        if (overlayLoad(k, &js, &jl)) {
            fprintf(stderr, "text: %s: overlay file missing; serving the Spanish disc file\n", name);
            free(base);
            return -1;
        }
        path[0] = 0;
    }
    if (full && readAll(path, &js, &jl)) {
        fprintf(stderr, "text: %s: cannot read %s; serving the next source\n", name, path);
        free(base);
        return -1;
    }
    {
        char label[64];
        snprintf(label, sizeof label, "%s/%s", tdLangDir(lang), name);
        rc = tdBuildFile(tdFileBase(k), label, base, baseLen, (const char *)js, jl, full, &out, &outLen, err, sizeof err);
    }
    free(base);
    free(js);
    if (rc) {
        fprintf(stderr, "text: %s rejected (%s): %s\n", name, src == TD_SRC_ES419 ? "overlay" : path, err);
        return -1;
    }
    free(s->bytes);
    s->bytes = out;
    s->len = outLen;
    {
        char hex[17];
        tdHashHex(tdHash(out, outLen), hex);
        PLOG("text", PLOG_TRACE, "idx=%d %s served from %s (%zu bytes, hash %s)\n", index, name,
             src == TD_SRC_MOD ? "mods" : src == TD_SRC_DATA ? "data (edited)" : "es-419 merge", outLen, hex);
    }
    return 0;
}

/* the extra entries a registry appends to a name file and a description file (see tdSetItemTextHook, tdSetTechTextHook) */
typedef struct ExtText {
    int (*count)(void);
    const char *(*text)(int index, const char *langDir, int desc);
    int first;            /* the first id of a new entry: entries below it are padded with empty strings */
    const char *nameBase; /* the text file indexed by id: names */
    const char *infoBase; /* and descriptions (NULL: the file has no description file to extend) */
    const char *what;     /* for the log */
    int fitIgnoresRule;   /* fit the description to the box even when layout.json marks the file noreflow */
} ExtText;

enum { EXT_TEXT_ITEMS, EXT_TEXT_TECHS, EXT_TEXT_DIGIMON, EXT_TEXT_CARDS, EXT_TEXT_KINDS };
static ExtText gExtText[EXT_TEXT_KINDS] = {
    {NULL, NULL, 403, "ITMNAM", "ITMINF", "item", 0}, /* PC_ITEM_ORIG_COUNT */
    {NULL, NULL, 444, "SKLNAM", "SKLINF", "tech", 1}, /* PC_TECH_FIRST_EXT_ID */
    {NULL, NULL, 251, "DIGNAM", "DIGINF", "digimon", 0}, /* the entries of DIGNAM (and DIGINF, the lab's description of a form) on the disc: the names of new enemies and new partner forms */
    {NULL, NULL, 317, "CARDNM", "CARDST", "card", 0}, /* PC_CARD_ORIG_COUNT: CARDNM entry = card id, CARDST the effect text of a card */
};

void tdSetItemTextHook(int (*count)(void), const char *(*text)(int index, const char *langDir, int desc))
{
    gExtText[EXT_TEXT_ITEMS].count = count;
    gExtText[EXT_TEXT_ITEMS].text = text;
}

void tdSetTechTextHook(int (*count)(void), const char *(*text)(int index, const char *langDir, int desc))
{
    gExtText[EXT_TEXT_TECHS].count = count;
    gExtText[EXT_TEXT_TECHS].text = text;
}

void tdSetCardTextHook(int (*count)(void), const char *(*text)(int index, const char *langDir, int desc))
{
    gExtText[EXT_TEXT_CARDS].count = count;
    gExtText[EXT_TEXT_CARDS].text = text;
}

void tdSetDigimonTextHook(int (*count)(void), const char *(*text)(int index, const char *langDir, int desc))
{
    gExtText[EXT_TEXT_DIGIMON].count = count;
    gExtText[EXT_TEXT_DIGIMON].text = text;
}

static int fitWidth(const char *text)
{
    char *raw = tcUglify(text);
    uint8_t *enc = NULL;
    size_t el = 0;
    int w = -1;

    if (raw && tcEncode(raw, &enc, &el, NULL, 0) == 0) w = trMaxLine(&gOpt, enc, el);
    free(raw);
    free(enc);
    return w;
}

int tdFitLine(const char *text, int maxWidth, char *out, size_t cap)
{
    size_t n, k;
    int pass;

    if (!text || !out || cap < 5 || !gOpt.width || maxWidth <= 0) return -1;
    n = strlen(text);
    if (n < cap && fitWidth(text) <= maxWidth) {
        memcpy(out, text, n + 1);
        return 0;
    }
    /* the longest prefix that, with "...", fits: at a space first (a whole word), else at any character */
    for (pass = 0; pass < 2; pass++) {
        for (k = n < cap - 4 ? n : cap - 4; k > 0; k--) {
            size_t e = k;
            if ((unsigned char)text[k] >= 0x80 && ((unsigned char)text[k] & 0xC0) == 0x80) continue; /* inside a character */
            if (pass == 0 && text[k] != ' ') continue;
            while (e > 0 && text[e - 1] == ' ') e--;
            if (e == 0) continue;
            memcpy(out, text, e);
            memcpy(out + e, "...", 4);
            if (fitWidth(out) <= maxWidth) return 1;
        }
    }
    snprintf(out, cap, "...");
    return 1;
}

/* The name and description files of items (ITMNAM/ITMINF) and techniques (SKLNAM/SKLINF) get the entries of the new
   ones appended (see tdSetItemTextHook, tdSetTechTextHook). `s` holds the file built so far, or nothing (the disc's
   file). */
static void extendExtText(Slot *s, int index, int lang, int k)
{
    const char *base = tdFileBase(k);
    int desc = 0, n, i, total, first, h;
    const ExtText *x = NULL;
    uint8_t *disc = NULL, *out;
    size_t dl = 0, ol;
    TcDoc doc;
    TcLang *l;
    char err[200];

    if (!base) return;
    for (h = 0; h < EXT_TEXT_KINDS && !x; h++) {
        if (!strcmp(base, gExtText[h].nameBase)) { x = &gExtText[h]; desc = 0; }
        else if (gExtText[h].infoBase && !strcmp(base, gExtText[h].infoBase)) { x = &gExtText[h]; desc = 1; }
    }
    if (!x || !x->count || !x->text || (n = x->count()) <= 0) return;
    first = x->first;
    if (!s->bytes && discLoad(index, &disc, &dl)) return;
    if (tcDocParse(s->bytes ? s->bytes : disc, s->bytes ? s->len : dl, &doc, err, sizeof err)) {
        free(disc);
        return;
    }
    free(disc);
    l = &doc.lang;
    total = first + n;
    if (doc.isPack || l->count > first) {
        tcDocFree(&doc);
        return;
    }
    {
        TcEntry *grown = (TcEntry *)realloc(l->entries, sizeof(TcEntry) * (size_t)total);
        if (!grown) {
            tcDocFree(&doc);
            return;
        }
        memset(grown + l->count, 0, sizeof(TcEntry) * (size_t)(total - l->count));
        l->entries = grown;
        i = l->count;
        l->count = total;
    }
    for (; i < total; i++) {
        const char *text = i < first ? "" : x->text(i - first, tdLangDir(lang), desc);
        char fitted[512];
        char *raw;
        int bad;

        /* the description box is one line (layout.json, ITMINF; SKLINF has the same kind of box): a longer one is cut at
           a word with "...", on purpose, instead of running off the window; the log names the entry so its author can
           shorten it */
        if (desc == 1 && gReflowOn && text && text[0] && i >= first) {
            const TrFileRule *rule = trLayoutFind(&gLayout, base);
            if (!rule || !rule->noreflow || x->fitIgnoresRule) {
                int budget = rule && rule->maxWidth > 0 ? rule->maxWidth : 280;
                if (tdFitLine(text, budget, fitted, sizeof fitted) == 1) {
                    fprintf(stderr, "text: %s %s %d (%s) description is wider than the box (%d px); shown as \"%s\"\n", base,
                            x->what, i, tdLangDir(lang), budget, fitted);
                    text = fitted;
                }
            }
        }
        raw = tcUglify(text ? text : "");
        bad = !raw || tcSetText(l, i, raw, err, sizeof err);
        free(raw);
        if (bad) {
            fprintf(stderr, "text: %s entry %d: %s; left as \"?\"\n", base, i, err);
            tcSetText(l, i, "?", err, sizeof err);
        }
    }
    out = tcDocBuild(&doc, &ol);
    tcDocFree(&doc);
    if (!out) return;
    free(s->bytes);
    s->bytes = out;
    s->len = ol;
}

/* ---- port-close S6: named texts for the stage talks (see tdSetStageTextHook) ---- */

static int (*gStageTextCount)(int k);
static const char *(*gStageTextText)(int k, int i, const char *langDir);
static int gStageFirst[TD_FILES]; /* the disc's entry count of file k plus one; 0 unknown */

void tdSetStageTextHook(int (*count)(int k), const char *(*text)(int k, int i, const char *langDir))
{
    gStageTextCount = count;
    gStageTextText = text;
}

int tdStageTextK(int textFile)
{
    int lang, k;
    if (textFile < 0 || !tdFromIndex(2 + textFile, &lang, &k) || lang != TD_ENG) return -1; /* LANGUAGE 2 is English: index 2 + n is its slot */
    return k;
}

static int discDocCount(int index)
{
    uint8_t *disc = NULL;
    size_t dl = 0;
    TcDoc doc;
    char err[200];
    int n;
    if (discLoad(index, &disc, &dl)) return -1;
    if (tcDocParse(disc, dl, &doc, err, sizeof err)) {
        free(disc);
        return -1;
    }
    free(disc);
    n = doc.isPack ? -1 : doc.lang.count;
    tcDocFree(&doc);
    return n;
}

int tdStageTextFirst(int k)
{
    if (k < 0 || k >= TD_FILES) return -1;
    if (!gStageFirst[k]) {
        int n = discDocCount(tdIndexOf(TD_ENG, k));
        if (n < 0) return -1;
        gStageFirst[k] = n + 1;
    }
    return gStageFirst[k] - 1;
}

void tdStageTextInvalidate(int k)
{
    int lang;
    if (k < 0 || k >= TD_FILES) return;
    for (lang = 0; lang < TD_LANGS; lang++) {
        int index = tdIndexOf(lang, k);
        if (index >= 1 && index <= TD_MAX_INDEX) gSlots[index].init = 0;
    }
}

/* The pure half: `file` (a lang file of `len` bytes) with `count` entries appended after its first `first` (the entries below `first` it lacks are
   padded empty); text(ud, i) is appended entry i in the readable syntax. The new entries are reflowed when the layout has a rule for fileBase: each
   counts as written by a mod, with the talk-box budget. Fails (-1, err) on a pack, a file that already has more than `first` entries, or no memory. */
int tdAppendTexts(const uint8_t *file, size_t len, const char *fileBase, int first, int count, const char *(*text)(void *ud, int i), void *ud,
                  uint8_t **out, size_t *outLen, char *err, size_t errSize)
{
    TcDoc doc, orig;
    TcLang *l;
    int i, total = first + count, haveOrig = 0, rc = -1;

    if (tcDocParse(file, len, &doc, err, errSize)) return -1;
    l = &doc.lang;
    if (doc.isPack || l->count > first) {
        if (err && errSize) {
            if (doc.isPack) snprintf(err, errSize, "a pack cannot be extended");
            else snprintf(err, errSize, "the file has %d entries, more than the %d it should start from", l->count, first);
        }
        tcDocFree(&doc);
        return -1;
    }
    if (gReflowOn && fileBase && trLayoutFind(&gLayout, fileBase) && tcDocParse(file, len, &orig, err, errSize) == 0) haveOrig = 1;
    {
        TcEntry *grown = (TcEntry *)realloc(l->entries, sizeof(TcEntry) * (size_t)total);
        if (!grown) {
            errf(err, errSize, "out of memory");
            goto done;
        }
        memset(grown + l->count, 0, sizeof(TcEntry) * (size_t)(total - l->count));
        l->entries = grown;
        i = l->count;
        l->count = total;
    }
    if (haveOrig) { /* the original's side of the reflow: the new entries are empty there */
        TcEntry *grown = (TcEntry *)realloc(orig.lang.entries, sizeof(TcEntry) * (size_t)total);
        if (!grown) {
            errf(err, errSize, "out of memory");
            goto done;
        }
        memset(grown + orig.lang.count, 0, sizeof(TcEntry) * (size_t)(total - orig.lang.count));
        orig.lang.entries = grown;
        orig.lang.count = total;
    }
    for (; i < total; i++) {
        const char *t = i < first ? "" : text(ud, i - first);
        char *raw = tcUglify(t ? t : "");
        int bad = !raw || tcSetText(l, i, raw, err, errSize);

        free(raw);
        if (bad) {
            fprintf(stderr, "text: appended entry %d: %s; left as \"?\"\n", i, err ? err : "cannot encode");
            tcSetText(l, i, "?", err, errSize);
        }
    }
    if (haveOrig) {
        const TrFileRule *rule = trLayoutFind(&gLayout, fileBase);
        if (rule && trReflowDoc(&doc, &orig, &gOpt, rule, fileBase, NULL, err, errSize)) {
            fprintf(stderr, "text: %s appended entries: reflow failed (%s); kept as written\n", fileBase, err);
        }
    }
    *out = tcDocBuild(&doc, outLen);
    if (!*out) {
        errf(err, errSize, "out of memory");
        goto done;
    }
    rc = 0;
done:
    if (haveOrig) tcDocFree(&orig);
    tcDocFree(&doc);
    return rc;
}

typedef struct StageTextCtx {
    int k;
    const char *langDir;
} StageTextCtx;

static const char *stageTextAt(void *ud, int i)
{
    const StageTextCtx *c = (const StageTextCtx *)ud;
    return gStageTextText(c->k, i, c->langDir);
}

/* The appended entries of a stage's talk file (the hook above); `s` holds the file built so far, or nothing (the disc's file). A language
   whose file has more entries than the English one cannot be extended (its entry numbers would not agree): it is left alone, with a line. */
static void extendStageText(Slot *s, int index, int lang, int k)
{
    const char *base = tdFileBase(k);
    int n, first;
    uint8_t *disc = NULL, *out = NULL;
    size_t dl = 0, ol = 0;
    StageTextCtx ctx;
    char err[200];

    if (!base || !gStageTextCount || !gStageTextText || (n = gStageTextCount(k)) <= 0) return;
    first = tdStageTextFirst(k);
    if (first < 0) return;
    if (!s->bytes && discLoad(index, &disc, &dl)) return;
    ctx.k = k;
    ctx.langDir = tdLangDir(lang);
    if (tdAppendTexts(s->bytes ? s->bytes : disc, s->bytes ? s->len : dl, base, first, n, stageTextAt, &ctx, &out, &ol, err, sizeof err)) {
        fprintf(stderr, "text: %s (%s): the named stage texts are not added: %s\n", base, ctx.langDir, err);
        free(disc);
        return;
    }
    free(disc);
    free(s->bytes);
    s->bytes = out;
    s->len = ol;
}

static void rebuildBase(Slot *s, int index, int lang, int k, int es419);

static void rebuild(Slot *s, int index, int lang, int k, int es419)
{
    rebuildBase(s, index, lang, k, es419);
    extendExtText(s, index, lang, k);
    extendStageText(s, index, lang, k);
}

static void rebuildBase(Slot *s, int index, int lang, int k, int es419)
{
    char dp[1200], mp[MAX_MOD_FILES][1200];
    TdSource order[3];
    int n, i, nmods;
    TdFileState st = TD_FILE_MISSING;

    free(s->bytes);
    s->bytes = NULL;
    s->len = 0;
    nmods = tdModFiles(lang, k, mp, &s->modM);
    s->modExists = nmods > 0;
    if (gHaveData) {
        dataPath(dp, sizeof dp, "text", lang, k, ".json");
        s->dataM = mtimeOf(dp, &s->dataExists);
        if (s->dataExists) {
            uint8_t *cur;
            size_t cl;
            if (readAll(dp, &cur, &cl) == 0) {
                char hex[17], rel[100];
                char name[32];
                tdFileName(lang, k, name, sizeof name);
                snprintf(rel, sizeof rel, "%s/%.*s.json", tdLangDir(lang), (int)strlen(name) - 4, name);
                lowerCase(rel);
                tdHashHex(tdHash(cur, cl), hex);
                st = tdFileState(1, tdManifestFind(&gManifest, rel), hex);
                free(cur);
            } else {
                st = TD_FILE_EDITED; /* exists but unreadable: let the build report it */
            }
        }
    } else {
        s->dataExists = 0;
        s->dataM = 0;
    }
    n = tdSourceOrder(s->modExists, st, es419 && gHaveL10n, order);
    for (i = 0; i < n; i++) {
        if (order[i] == TD_SRC_MOD) {
            int j;
            for (j = 0; j < nmods; j++) {
                if (tryBuild(s, index, lang, k, TD_SRC_MOD, mp[j]) == 0) return;
            }
        } else if (tryBuild(s, index, lang, k, order[i], NULL) == 0) {
            return;
        }
    }
}

const uint8_t *tdOverride(int index, size_t *len, int refresh)
{
    int lang, k, es419;
    Slot *s;
    double now;

    if (!gHaveDisc || !tdFromIndex(index, &lang, &k)) return NULL;
    es419 = (lang == TD_SPA && gVariant == TD_VARIANT_ES419);
    if (es419) lang = TD_ES419;
    if (!gHaveData && modsCount() == 0 && !(es419 && gHaveL10n)) return NULL;
    s = &gSlots[index];
    now = nowSeconds();
    if (s->init && s->variant == gVariant && (!refresh || now - s->checked < 1.0)) {
        goto have;
    }
    if (s->init && s->variant == gVariant) {
        /* due for a look: did a source file change? */
        char dp[1200], mp[MAX_MOD_FILES][1200];
        int de = 0, me;
        long dm = 0, mm = 0;
        if (gHaveData) {
            dataPath(dp, sizeof dp, "text", lang, k, ".json");
            dm = mtimeOf(dp, &de);
        }
        me = tdModFiles(lang, k, mp, &mm) > 0;
        s->checked = now;
        if (de == s->dataExists && me == s->modExists && dm == s->dataM && mm == s->modM) goto have;
    }
    rebuild(s, index, lang, k, es419);
    s->init = 1;
    s->variant = gVariant;
    s->checked = now;
have:
    if (!s->bytes) return NULL;
    if (len) *len = s->len;
    return s->bytes;
}
