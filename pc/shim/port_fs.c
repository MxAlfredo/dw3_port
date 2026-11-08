/*
 * The port's common file layer. See port_fs.h. Pure stdlib plus mods.h and the TdDisc of text_data.h.
 */
#include "port_fs.h"

#include "mods.h"
#include "port_log.h"
#include "port_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ---- the disc directory: walked once, shared by the text, image and file layers ---- */

typedef struct PtDir {
    char name[40];
    long extent;
    unsigned parent;
} PtDir;

static TdDisc gDisc;
static int gOpen;
static uint8_t gPvd[2048];
static uint8_t *gPt; /* the path table */
static size_t gPtSize;
static PfsFile *gFiles; /* in discovery order: path-table order of the directories, then record order */
static int gNFiles;
static int *gByLba, *gByPath; /* indexes into gFiles, sorted by sector (then path) and by path */

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

uint8_t *pfsDiscRead(long lba, size_t len)
{
    size_t n = (len + 2047) / 2048, i;
    uint8_t *b;
    if (!gOpen) return NULL;
    b = (uint8_t *)malloc(n * 2048 + 1);
    if (!b) return NULL;
    for (i = 0; i < n; i++) {
        if (!gDisc.readSector(gDisc.ctx, lba + (long)i, b + i * 2048)) { free(b); return NULL; }
    }
    return b;
}

/* "A/B/" for directory i of the path table (root: ""), at most 16 levels deep */
static int dirPath(const PtDir *dirs, int n, int i, char *out, size_t cap, int depth)
{
    size_t used;
    if (i == 0) { out[0] = 0; return 0; }
    if (depth > 16 || dirs[i].parent < 1 || dirs[i].parent > (unsigned)n) return -1;
    if (dirPath(dirs, n, (int)dirs[i].parent - 1, out, cap, depth + 1)) return -1;
    used = strlen(out);
    if (used + strlen(dirs[i].name) + 2 > cap) return -1;
    snprintf(out + used, cap - used, "%s/", dirs[i].name);
    return 0;
}

static int cmpLba(const void *a, const void *b)
{
    const PfsFile *x = &gFiles[*(const int *)a], *y = &gFiles[*(const int *)b];
    if (x->lba != y->lba) return x->lba < y->lba ? -1 : 1;
    return strcmp(x->path, y->path);
}

static int cmpPath(const void *a, const void *b)
{
    return strcmp(gFiles[*(const int *)a].path, gFiles[*(const int *)b].path);
}

void pfsDiscClose(void)
{
    free(gPt);
    free(gFiles);
    free(gByLba);
    free(gByPath);
    gPt = NULL;
    gFiles = NULL;
    gByLba = gByPath = NULL;
    gPtSize = 0;
    gNFiles = 0;
    gOpen = 0;
}

int pfsDiscOpen(const TdDisc *disc)
{
    PtDir *dirs = NULL;
    int nDirs = 0, capDirs = 0, capFiles = 0, i, rc = -1;
    size_t at = 0;

    pfsDiscClose();
    if (!disc || !disc->readSector) return -1;
    gDisc = *disc;
    gOpen = 1;
    if (!gDisc.readSector(gDisc.ctx, 16, gPvd) || memcmp(gPvd + 1, "CD001", 5)) goto out;
    gPtSize = le32(gPvd + 132);
    if (gPtSize == 0 || gPtSize > (1u << 20)) goto out;
    gPt = pfsDiscRead((long)le32(gPvd + 140), gPtSize);
    if (!gPt) goto out;
    while (at + 8 <= gPtSize && gPt[at]) {
        unsigned idLen = gPt[at];
        PtDir *e;
        if (nDirs == capDirs) {
            PtDir *g = (PtDir *)realloc(dirs, (size_t)(capDirs ? capDirs * 2 : 64) * sizeof *dirs);
            if (!g) goto out;
            dirs = g;
            capDirs = capDirs ? capDirs * 2 : 64;
        }
        e = &dirs[nDirs++];
        e->extent = (long)le32(gPt + at + 2);
        e->parent = le16(gPt + at + 6);
        memset(e->name, 0, sizeof e->name);
        memcpy(e->name, gPt + at + 8, idLen < sizeof e->name - 1 ? idLen : sizeof e->name - 1);
        at += 8 + idLen + (idLen & 1);
    }
    for (i = 0; i < nDirs; i++) {
        char prefix[200];
        uint8_t first[2048], *d;
        size_t dsize, off = 0;

        if (dirPath(dirs, nDirs, i, prefix, sizeof prefix, 0)) continue;
        if (!gDisc.readSector(gDisc.ctx, dirs[i].extent, first)) continue;
        dsize = le32(first + 10);
        d = pfsDiscRead(dirs[i].extent, dsize);
        if (!d) continue;
        while (off < dsize) {
            unsigned rl = d[off];
            if (rl == 0) {
                off = (off / 2048 + 1) * 2048;
                continue;
            }
            if (off + rl > dsize || rl < 34) break;
            if (!(d[off + 25] & 2)) { /* a file, not a directory */
                unsigned nl = d[off + 32];
                char name[60], full[260];
                size_t m = 0;
                const char *p;
                while (m < nl && m < sizeof name - 1 && d[off + 33 + m] != ';') {
                    name[m] = (char)d[off + 33 + m];
                    m++;
                }
                name[m] = 0;
                snprintf(full, sizeof full, "%s%s", prefix, name);
                p = strncmp(full, "AAA/", 4) ? full : full + 4;
                if (m > 0 && strlen(p) < 100) { /* the name without "AAA/" fits 100; with it, the path field */
                    if (gNFiles == capFiles) {
                        PfsFile *g = (PfsFile *)realloc(gFiles, (size_t)(capFiles ? capFiles * 2 : 256) * sizeof *gFiles);
                        if (!g) { free(d); goto out; }
                        gFiles = g;
                        capFiles = capFiles ? capFiles * 2 : 256;
                    }
                    snprintf(gFiles[gNFiles].path, sizeof gFiles->path, "%.103s", full);
                    gFiles[gNFiles].lba = (long)le32(d + off + 2);
                    gFiles[gNFiles].size = (long)le32(d + off + 10);
                    gNFiles++;
                }
            }
            off += rl;
        }
        free(d);
    }
    gByLba = (int *)malloc((size_t)(gNFiles ? gNFiles : 1) * sizeof *gByLba);
    gByPath = (int *)malloc((size_t)(gNFiles ? gNFiles : 1) * sizeof *gByPath);
    if (!gByLba || !gByPath) goto out;
    for (i = 0; i < gNFiles; i++) gByLba[i] = gByPath[i] = i;
    qsort(gByLba, (size_t)gNFiles, sizeof *gByLba, cmpLba);
    qsort(gByPath, (size_t)gNFiles, sizeof *gByPath, cmpPath);
    rc = 0;
out:
    free(dirs);
    if (rc) pfsDiscClose();
    return rc;
}

int pfsDiscIsOpen(void) { return gOpen; }

uint64_t pfsDiscHash(int withPathTable)
{
    uint64_t h = TD_HASH_INIT;
    if (!gOpen) return h;
    h = tdHashUpdate(h, gPvd, sizeof gPvd);
    if (withPathTable) h = tdHashUpdate(h, gPt, gPtSize);
    return h;
}

const PfsFile *pfsFindPath(const char *path)
{
    int lo = 0, hi = gNFiles;
    while (lo < hi) {
        int mid = (lo + hi) / 2, c = strcmp(gFiles[gByPath[mid]].path, path);
        if (c == 0) return &gFiles[gByPath[mid]];
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

int pfsList(const char *prefix, const PfsFile **out, int max)
{
    size_t pl = prefix ? strlen(prefix) : 0;
    int i, n = 0;
    for (i = 0; i < gNFiles; i++) {
        if (strncmp(gFiles[i].path, prefix ? prefix : "", pl)) continue;
        if (out && n < max) out[n] = &gFiles[i];
        n++;
    }
    return n;
}


/* ---- the map and the per-file state ---- */

typedef struct RawSlot {
    int init;
    double checked;
    int mod;         /* index of the mod the bytes come from, -1: none */
    long mtime;
    long size;
    uint8_t *bytes;
    size_t len;
    int loggedMod;   /* the mod whose override was announced, -1 */
    int loggedClash; /* the precedence note was printed */
} RawSlot;

static char (*gNames)[100]; /* per number; "" when unmapped */
static RawSlot *gSlots;
static int gCount, gMapped;
static PfsTextFn gText;
static PfsImageFn gImage;

/* the generated layer: whole files held in memory (the content mods' battle table and fighters file, any other file a mod
   adds to). A growable list: it is searched linearly, and holds a handful of entries. */
typedef struct GenFile {
    int file;
    uint8_t *bytes;
    size_t len;
    int loggedClash;
} GenFile;
static GenFile *gGen;
static int gGenN, gGenCap;

void pfsSetGenerated(int file, const uint8_t *bytes, size_t len)
{
    int i, free_ = -1;

    for (i = 0; i < gGenN; i++) {
        if (gGen[i].bytes && gGen[i].file == file) break;
        if (!gGen[i].bytes && free_ < 0) free_ = i;
    }
    if (i == gGenN) {
        if (!bytes || len == 0) return;
        if (free_ >= 0) {
            i = free_;
        } else {
            if (gGenN == gGenCap) {
                int cap = gGenCap ? gGenCap * 2 : 8;
                GenFile *grown = (GenFile *)realloc(gGen, (size_t)cap * sizeof *gGen);
                if (!grown) return;
                gGen = grown;
                gGenCap = cap;
            }
            i = gGenN++;
            gGen[i].bytes = NULL;
        }
    }
    free(gGen[i].bytes);
    gGen[i].bytes = NULL;
    gGen[i].len = 0;
    gGen[i].loggedClash = 0;
    gGen[i].file = file;
    if (!bytes || len == 0) return;
    gGen[i].bytes = (uint8_t *)malloc(len);
    if (!gGen[i].bytes) return;
    memcpy(gGen[i].bytes, bytes, len);
    gGen[i].len = len;
}

static int genSlot(int file)
{
    int i;
    for (i = 0; i < gGenN; i++) if (gGen[i].bytes && gGen[i].file == file) return i;
    return -1;
}

/* virtual files: numbers gVirtBase.. up, one array slot per number (an O(1) lookup: the game asks for the size and the bytes of
   a file several times) */
#define VIRT_LAST 0xFFFF /* the file of (file << 16) | entry is a u16 */
typedef struct VirtFile {
    uint8_t *bytes;
    size_t len;
} VirtFile;
static VirtFile *gVirt;
static int gVirtN, gVirtCap, gVirtBase;

int pfsVirtualBase(void) { return gVirtBase; }
int pfsVirtualCount(void) { return gVirtN; }

int pfsIsVirtual(int file)
{
    return gVirtBase > 0 && file >= gVirtBase && file - gVirtBase < gVirtN;
}

int pfsAddVirtualFile(const void *bytes, size_t len)
{
    uint8_t *copy;

    if (!bytes || len == 0 || gVirtBase <= 0 || gVirtBase + gVirtN > VIRT_LAST) return -1;
    if (gVirtN == gVirtCap) {
        int cap = gVirtCap ? gVirtCap * 2 : 16;
        VirtFile *grown = (VirtFile *)realloc(gVirt, (size_t)cap * sizeof *gVirt);
        if (!grown) return -1;
        gVirt = grown;
        gVirtCap = cap;
    }
    copy = (uint8_t *)malloc(len);
    if (!copy) return -1;
    memcpy(copy, bytes, len);
    gVirt[gVirtN].bytes = copy;
    gVirt[gVirtN].len = len;
    return gVirtBase + gVirtN++;
}

void pfsClearVirtualFiles(void)
{
    int i;
    for (i = 0; i < gVirtN; i++) free(gVirt[i].bytes);
    free(gVirt);
    gVirt = NULL;
    gVirtN = gVirtCap = 0;
}

void pfsSetLayers(PfsTextFn text, PfsImageFn image)
{
    gText = text;
    gImage = image;
}

static void freeMap(void)
{
    int i;
    for (i = 0; i < gCount; i++) free(gSlots[i].bytes);
    free(gSlots);
    free(gNames);
    gSlots = NULL;
    gNames = NULL;
    gCount = gMapped = 0;
}

void pfsShutdown(void)
{
    int i;
    for (i = 0; i < gGenN; i++) free(gGen[i].bytes);
    free(gGen);
    gGen = NULL;
    gGenN = gGenCap = 0;
    pfsClearVirtualFiles();
    gVirtBase = 0;
    freeMap();
    pfsDiscClose();
}

int pfsInit(const TdDisc *disc, int fileCount)
{
    int i;

    freeMap();
    if (fileCount > 0 && fileCount != gVirtBase) {
        pfsClearVirtualFiles(); /* the numbers were relative to another base */
        gVirtBase = fileCount;
    }
    if (!disc || !disc->readSector || !disc->locate || fileCount <= 0) return -1;
    if (!pfsDiscIsOpen() && pfsDiscOpen(disc)) return -1;
    gNames = (char (*)[100])calloc((size_t)fileCount, sizeof *gNames);
    gSlots = (RawSlot *)calloc((size_t)fileCount, sizeof *gSlots);
    if (!gNames || !gSlots) {
        freeMap();
        return -1;
    }
    gCount = fileCount;
    for (i = 0; i < fileCount; i++) {
        long lba = 0;
        int sectors = 0, lo, hi, mid, first, k, same = 1;
        const PfsFile *f0;

        gSlots[i].mod = gSlots[i].loggedMod = -1;
        if (!disc->locate(disc->ctx, i, &lba, &sectors) || lba <= 0 || sectors <= 0) continue;
        lo = 0;
        hi = gNFiles;
        while (lo < hi) { /* first record at lba */
            mid = (lo + hi) / 2;
            if (gFiles[gByLba[mid]].lba < lba) lo = mid + 1; else hi = mid;
        }
        first = lo;
        if (first >= gNFiles || gFiles[gByLba[first]].lba != lba) continue;
        f0 = &gFiles[gByLba[first]];
        for (k = first + 1; k < gNFiles && gFiles[gByLba[k]].lba == lba; k++) {
            if (strcmp(gFiles[gByLba[k]].path, f0->path)) same = 0;
        }
        if (!same) continue; /* several files start there: not a name we can trust */
        snprintf(gNames[i], sizeof gNames[0], "%.99s", strncmp(f0->path, "AAA/", 4) ? f0->path : f0->path + 4);
        gMapped++;
    }
    return 0;
}

int pfsNumberOf(const char *path)
{
    int i;
    if (!path) return -1;
    if (!strncmp(path, "AAA/", 4)) path += 4;
    for (i = 0; i < gCount; i++) {
        if (gNames[i][0] && !strcmp(gNames[i], path)) return i;
    }
    return -1;
}

const char *pfsName(int file)
{
    return (file >= 0 && file < gCount && gNames[file][0]) ? gNames[file] : NULL;
}

int pfsFileCount(void) { return gMapped; }

static double nowSeconds(void)
{
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static int readWhole(const char *path, long size, uint8_t **out)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    if (!f) return -1;
    b = (uint8_t *)malloc((size_t)size + 1);
    if (!b || fread(b, 1, (size_t)size, f) != (size_t)size) { free(b); fclose(f); return -1; }
    fclose(f);
    *out = b;
    return 0;
}

/* Look at the mods' files/ for this number and (re)load the bytes when the winner or its mtime/size changed */
static void rawScan(RawSlot *s, int file)
{
    const char *name = pfsName(file);
    int i, winner = -1;
    long mt = 0, sz = 0;
    char path[1400];

    s->checked = nowSeconds();
    s->init = 1;
    if (!name) return;
    for (i = 0; i < modsCount(); i++) { /* the mods only; the disc is the base (mods.h asset layers) */
        AssetLayer layer;
        struct stat st;
        /* the disc's names are upper case; a mod may spell them any way (ADR-010 section 3) */
        if (!assetLayerAt(i, &layer) || !assetLayerFile(&layer, "files", name, path, sizeof path)) continue;
        if (stat(path, &st) != 0) continue;
        if (st.st_size <= 0) {
            plogLine("files", "%s rejected: empty file\n", path);
            continue;
        }
        if (s->bytes && s->mod == layer.mod && s->mtime == (long)st.st_mtime && s->size == (long)st.st_size) return; /* unchanged */
        {
            uint8_t *b = NULL;
            if (readWhole(path, (long)st.st_size, &b)) {
                plogLine("files", "%s rejected: cannot read it\n", path);
                continue;
            }
            free(s->bytes);
            s->bytes = b;
            s->len = (size_t)st.st_size;
            s->mod = layer.mod;
            s->mtime = (long)st.st_mtime;
            s->size = (long)st.st_size;
            winner = layer.mod;
            mt = s->mtime;
            sz = s->size;
            break;
        }
    }
    (void)mt;
    (void)sz;
    if (winner < 0) { /* no usable file now: a removed override returns the file to the disc */
        free(s->bytes);
        s->bytes = NULL;
        s->len = 0;
        s->mod = -1;
        return;
    }
    if (s->loggedMod != winner) {
        s->loggedMod = winner;
        plogLine("files", "override %s from mods/%s\n", name, modsNameOf(winner));
    }
}

static RawSlot *rawSlot(int file, int refresh)
{
    RawSlot *s;
    if (file < 0 || file >= gCount || modsCount() == 0) return NULL;
    s = &gSlots[file];
    if (!s->init || (refresh && nowSeconds() - s->checked >= 1.0)) rawScan(s, file);
    return s->bytes ? s : NULL;
}

const uint8_t *pfsResolve(int file, size_t *len, int refresh, PfsSource *src)
{
    const uint8_t *t;
    RawSlot *raw;

    if (src) *src = PFS_SRC_DISC;
    refresh = refresh && modsHotReload(); /* the mods are read once (mods.h); only the opt-in hot reload looks again */
    if (pfsIsVirtual(file)) { /* not on the disc: nothing above it can replace it */
        const VirtFile *v = &gVirt[file - gVirtBase];
        if (len) *len = v->len;
        if (src) *src = PFS_SRC_GEN;
        return v->bytes;
    }
    t = gText ? gText(file, len, refresh) : NULL;
    raw = rawSlot(file, refresh);
    if (t) {
        if (raw && !raw->loggedClash) {
            raw->loggedClash = 1;
            plogLine("files", "%s from mods/%s ignored: the text layer handles this file\n", pfsName(file) ? pfsName(file) : "?", modsNameOf(raw->mod));
        }
        if (src) *src = PFS_SRC_TEXT;
        return t;
    }
    if (!raw) {
        int g = genSlot(file);
        if (g < 0) return NULL;
        if (len) *len = gGen[g].len;
        if (src) *src = PFS_SRC_GEN;
        return gGen[g].bytes;
    }
    if (gImage && gImage(file)) {
        if (!raw->loggedClash) {
            raw->loggedClash = 1;
            plogLine("files", "%s from mods/%s ignored: the image layer substitutes images in this file\n", pfsName(file) ? pfsName(file) : "?", modsNameOf(raw->mod));
        }
        return NULL;
    }
    {
        int g = genSlot(file);
        if (g >= 0 && !gGen[g].loggedClash) {
            gGen[g].loggedClash = 1;
            plogLine("files", "%s from mods/%s replaces the file, so the content mods' additions to it are not served\n",
                     pfsName(file) ? pfsName(file) : "?", modsNameOf(raw->mod));
        }
    }
    if (len) *len = raw->len;
    if (src) *src = PFS_SRC_RAW;
    return raw->bytes;
}

int pfsReadSectors(int file, int offset, int sectors, uint8_t *dst, PfsSource *src)
{
    size_t len = 0, from = (size_t)offset * 2048, want = (size_t)sectors * 2048, n;
    PfsSource s = PFS_SRC_DISC;
    const uint8_t *over = pfsResolve(file, &len, 0, &s);

    if (src) *src = s;
    if (!over) return 0;
    n = (from < len) ? len - from : 0;
    if (n > want) n = want;
    memcpy(dst, over + from, n);
    memset(dst + n, 0, want - n);
    return 1;
}

static PfsPostReadFn gPost;

void pfsSetPostRead(PfsPostReadFn fn) { gPost = fn; }

void pfsPostRead(int file, int offset, int sectors, uint8_t *dst, PfsSource src)
{
    if (gPost && dst && sectors > 0) gPost(file, offset, sectors, dst, src);
}

int pfsTraceEnabled(void)
{
    return plogOn("files", PLOG_TRACE);
}

void pfsTrace(int file, int offset, int sectors, PfsSource src)
{
    const char *name = pfsName(file);
    plogLine("files", "read %d %s off=%d sectors=%d src=%s\n", file, name ? name : "?", offset, sectors,
            src == PFS_SRC_TEXT ? "text" : src == PFS_SRC_RAW ? "raw" : src == PFS_SRC_GEN ? "generated" : "disc");
}
