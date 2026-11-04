/* The field stage tiles as PNG and JSON (content-export E6b); see stage_export.h */
#include "stage_export.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "image_data.h"
#include "port_log.h"
#include "text_data.h"

#define RLEN_MAGIC 0x4E454C52u /* "RLEN" */
#define RAW_SECTOR 2352
#define USER_AT 24
#define USER_BYTES 2048
#define MAX_PACKS 512

static unsigned le32(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }
static int s16at(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static void setErr(char *err, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void setErr(char *err, size_t cap, const char *fmt, ...) {
    va_list ap;
    if (err == NULL || cap == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

/* ---------------------------------------------------------------- the formats */

int stagePackParse(const uint8_t *sector, size_t len, PackHeader *out, char *err, size_t errCap) {
    int n, i;

    memset(out, 0, sizeof *out);
    if (len < 16) {
        setErr(err, errCap, "the header is %zu bytes", len);
        return -1;
    }
    out->tileCount = (int)le32(sector);
    out->width = (int)le32(sector + 4);
    out->height = (int)le32(sector + 8);
    out->tileBytes = (int)le32(sector + 12);
    if (out->width <= 0 || out->height <= 0 || out->width > STAGE_MAX_TILES || out->height > STAGE_MAX_TILES ||
        out->width * out->height > STAGE_MAX_TILES || len < 16 + 2 * (size_t)(out->width * out->height)) {
        setErr(err, errCap, "the grid %dx%d does not fit the header sector", out->width, out->height);
        return -1;
    }
    if (out->tileBytes <= 0 || out->tileBytes % USER_BYTES != 0 || out->tileBytes > 0x100000) {
        setErr(err, errCap, "tileBytes %d is not a whole number of sectors", out->tileBytes);
        return -1;
    }
    n = out->width * out->height;
    for (i = 0; i < n; i++) out->sizes[i] = (uint16_t)(sector[16 + 2 * i] | sector[17 + 2 * i] << 8);
    return 0;
}

size_t stageTimLength(const uint8_t *tim, size_t max) {
    size_t at = 8, sz;
    unsigned flag;
    size_t iw, ih;

    if (max < 20 || le32(tim) != 0x10) return 0;
    flag = le32(tim + 4);
    if ((flag & ~0xBu) || (flag & 3) == 3) return 0;
    if (flag & 8) {
        if (at + 12 > max) return 0;
        sz = le32(tim + at);
        if (sz < 12 || at + sz > max) return 0;
        at += sz;
    }
    if (at + 12 > max) return 0;
    iw = (size_t)(tim[at + 8] | tim[at + 9] << 8);
    ih = (size_t)(tim[at + 10] | tim[at + 11] << 8);
    if (iw == 0 || ih == 0 || at + 12 + 2 * iw * ih > max) return 0;
    return at + 12 + 2 * iw * ih;
}

/* RLEN runs, the decompressor's own loop (src/engine/file/decompressor.c) with bounds. Returns the bytes written, or -1. */
static long rlenUnpack(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t cap) {
    size_t i = 0, o = 0;

    while (i < srcLen && src[i] != 0) {
        unsigned n = src[i] & 0x7Fu;
        if (src[i] & 0x80u) {
            if (i + 1 >= srcLen || o + n > cap) return -1;
            memset(dst + o, src[i + 1], n);
            o += n;
            i += 2;
        } else {
            if (i + 1 + n > srcLen || o + n > cap) return -1;
            memcpy(dst + o, src + i + 1, n);
            o += n;
            i += 1 + n;
        }
    }
    if (i >= srcLen) return -1; /* no end marker inside the data */
    return (long)o;
}

int stageTileParse(const uint8_t *raw, size_t rawLen, PackTile *t, char *err, size_t errCap) {
    const uint8_t *d;
    size_t len, p, off;
    int i, j;

    memset(t, 0, sizeof *t);
    if (rawLen < 8) {
        setErr(err, errCap, "the tile is %zu bytes", rawLen);
        return -1;
    }
    if (le32(raw) == RLEN_MAGIC) {
        size_t want = le32(raw + 4);
        long got;
        if (want == 0 || want > STAGE_MAX_UNPACKED) {
            setErr(err, errCap, "RLEN size %zu out of range", want);
            return -1;
        }
        t->data = (uint8_t *)malloc(want);
        if (t->data == NULL) {
            setErr(err, errCap, "out of memory");
            return -1;
        }
        got = rlenUnpack(raw + 8, rawLen - 8, t->data, want);
        if (got < 0 || (size_t)got != want) {
            setErr(err, errCap, "RLEN data does not unpack to its %zu bytes", want);
            stageTileFree(t);
            return -1;
        }
        t->len = want;
        t->rle = 1;
    } else {
        t->data = (uint8_t *)malloc(rawLen);
        if (t->data == NULL) {
            setErr(err, errCap, "out of memory");
            return -1;
        }
        memcpy(t->data, raw, rawLen);
        t->len = rawLen;
    }
    d = t->data;
    len = t->len;
    off = len >= 4 ? le32(d) : 0;
    if (off < 16 || off > len) {
        setErr(err, errCap, "the TIM offset %zu is outside the tile (%zu bytes)", off, len);
        stageTileFree(t);
        return -1;
    }
    p = 4;
    for (i = 0; i < 3; i++) {
        unsigned n;
        if (p + 4 > off) {
            setErr(err, errCap, "the sprite list of depth %d is past the TIM offset", i);
            stageTileFree(t);
            return -1;
        }
        n = le32(d + p);
        p += 4;
        if (n > 64 || p + 12 * (size_t)n > off) {
            setErr(err, errCap, "depth %d lists %u sprites, more than the tile holds", i, n);
            stageTileFree(t);
            return -1;
        }
        t->count[i] = (int)n;
        for (j = 0; j < (int)n; j++, p += 12) {
            if (j < STAGE_SPRITES) {
                t->sp[i][j] = (PackSprite){s16at(d + p), s16at(d + p + 2), s16at(d + p + 4), s16at(d + p + 6), s16at(d + p + 8), s16at(d + p + 10)};
            }
        }
    }
    t->timOff = off;
    t->timLen = stageTimLength(d + off, len - off);
    if (t->timLen == 0) {
        setErr(err, errCap, "no TIM at offset %zu", off);
        stageTileFree(t);
        return -1;
    }
    return 0;
}

void stageTileFree(PackTile *t) {
    free(t->data);
    t->data = NULL;
    t->len = 0;
}

void stageTileName(int width, int frame, char *out, size_t cap) {
    if (width <= 0) width = 1;
    snprintf(out, cap, "%02d_%02d.png", frame / width, frame % width);
}

int stagePackKey(const char *name, char *out, size_t cap) {
    size_t n = strlen(name), i;
    char b[32];

    if (n > 4 && toupper((unsigned char)name[n - 4]) == '.' && toupper((unsigned char)name[n - 3]) == 'B') n -= 4;
    if (n > 4) {
        static const char pack[] = "PACK";
        int ok = 1;
        for (i = 0; i < 4; i++) {
            if (toupper((unsigned char)name[n - 4 + i]) != pack[i]) ok = 0;
        }
        if (ok) n -= 4;
    }
    if (n != 4 || toupper((unsigned char)name[0]) != 'S') return 0;
    for (i = 1; i < 4; i++) {
        if (!isdigit((unsigned char)name[i])) return 0;
    }
    snprintf(b, sizeof b, "s%.3spack", name + 1);
    if (strlen(b) >= cap) return 0;
    strcpy(out, b);
    return 1;
}

/* ---------------------------------------------------------------- the descriptor JSON */

typedef struct Sb {
    char *p;
    size_t n, cap;
} Sb;

static void sbPrintf(Sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sbPrintf(Sb *b, const char *fmt, ...) {
    va_list ap;
    int k;

    if (b->p == NULL) return;
    for (;;) {
        va_start(ap, fmt);
        k = vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
        va_end(ap);
        if (k < 0) {
            free(b->p);
            b->p = NULL;
            return;
        }
        if (b->n + (size_t)k < b->cap) {
            b->n += (size_t)k;
            return;
        }
        {
            char *q = (char *)realloc(b->p, b->cap * 2 + (size_t)k);
            if (q == NULL) {
                free(b->p);
                b->p = NULL;
                return;
            }
            b->p = q;
            b->cap = b->cap * 2 + (size_t)k;
        }
    }
}

typedef struct TileInfo {
    int frame, size, w, h;
    PackTile t;
} TileInfo;

static void jsonTile(Sb *b, int width, const TileInfo *ti, int first) {
    char nm[24];
    int i, j;

    stageTileName(width, ti->frame, nm, sizeof nm);
    sbPrintf(b, "%s    {\"frame\": %d, \"row\": %d, \"col\": %d, \"png\": \"%s\", \"size\": %d, \"rle\": %s, \"timOffset\": %zu, "
                "\"width\": %d, \"height\": %d, \"depths\": [",
             first ? "" : ",\n", ti->frame, ti->frame / width, ti->frame % width, nm, ti->size, ti->t.rle ? "true" : "false", ti->t.timOff,
             ti->w, ti->h);
    for (i = 0; i < 3; i++) {
        sbPrintf(b, "%s[", i ? ", " : "");
        for (j = 0; j < ti->t.count[i] && j < STAGE_SPRITES; j++) {
            const PackSprite *s = &ti->t.sp[i][j];
            sbPrintf(b, "%s{\"x\": %d, \"y\": %d, \"u\": %d, \"v\": %d, \"w\": %d, \"h\": %d}", j ? ", " : "", s->x, s->y, s->u, s->v, s->w, s->h);
        }
        sbPrintf(b, "]");
    }
    sbPrintf(b, "]}");
}

/* ---------------------------------------------------------------- the job and the data folder */

static char sDataDir[1024];
static StageExportIo sIo;
static char (*sQueue)[16];
static long *sQLba, *sQSize;
static int sCount, sNext, sDone, sFailed, sBuilt;
static int sForce;
static StageExportStats sTotals;
static TdManifest sMan;
static int sManLoaded;
static char sStatus[96], sStatusOut[96];
static SDL_mutex *sMu;
static SDL_Thread *sThread;
static volatile int sStop;
static int sWorkerDone;

static void lockMu(void) {
    if (sMu != NULL) SDL_LockMutex(sMu);
}

static void unlockMu(void) {
    if (sMu != NULL) SDL_UnlockMutex(sMu);
}

static void packDir(const char *key, char *out, size_t cap) { snprintf(out, cap, "%s/z_stage/%s", sDataDir, key); }

static void manifestFree(void) {
    if (sManLoaded) tdManifestFree(&sMan);
    sManLoaded = 0;
}

/* the manifest of one pack: <data>/z_stage/<key>/manifest.json (small, so the game reads it only for the stage it enters) */
static void manifestLoad(const char *key) {
    char mp[1300], dir[1200];
    uint8_t *mt;
    size_t ml;

    manifestFree();
    memset(&sMan, 0, sizeof sMan);
    snprintf(sMan.stamp, sizeof sMan.stamp, "stage-v1");
    packDir(key, dir, sizeof dir);
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    if (tdReadFile(mp, &mt, &ml) == 0) {
        TdManifest old;
        char err[120];
        memset(&old, 0, sizeof old);
        if (tdManifestParse((const char *)mt, ml, &old, err, sizeof err) == 0) {
            tdManifestFree(&sMan);
            sMan = old;
        }
        free(mt);
    }
    sManLoaded = 1;
}

static int manifestSave(const char *key) {
    char mp[1300], dir[1200];
    char *text;
    int rc;

    packDir(key, dir, sizeof dir);
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    text = tdManifestFormat(&sMan);
    rc = (text == NULL || tdMakeDirs(dir) || tdWriteFileAtomic(mp, text, strlen(text))) ? -1 : 0;
    if (rc) plogLine("stages", "cannot write %s\n", mp);
    free(text);
    return rc;
}

static int fileExists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

/* STAGE_FILE_REV counts changes of the PNG bytes the shared encoder writes (png_write.c): rev 2 is real deflate, so a stage written
   with stored blocks (rev 1) is rebuilt once and shrinks. It lives here because stage_export.h keeps the layout version. */
#define STAGE_FILE_REV 2
static void versionHex(char *hex) { snprintf(hex, 17, "%016x", (unsigned)STAGE_EXPORT_VERSION | ((unsigned)STAGE_FILE_REV << 8)); }

/* written by this version and the descriptor is there (an edited file still counts): nothing to build */
static int upToDate(const char *key) {
    char path[1400], dir[1200], hex[17];
    const char *have;

    manifestLoad(key);
    have = tdManifestFind(&sMan, "exporter");
    versionHex(hex);
    if (have == NULL || strcmp(have, hex)) return 0;
    packDir(key, dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%s.json", dir, key);
    return fileExists(path);
}

/* One generated file under the manifest rule of ADR-005: an unedited copy is replaced, an edited one stays and the new version goes
   beside it as <file>.new. 0 when handled, -1 on a write failure. */
static int placeFile(const char *key, const char *rel, const uint8_t *bytes, size_t len, StageExportStats *st) {
    char dir[1200], path[1400], np[1450], hex[17], curHex[17] = "";
    uint8_t *cur = NULL;
    size_t curLen = 0;
    int exists = 0;
    const char *man = tdManifestFind(&sMan, rel);

    packDir(key, dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    tdHashHex(tdHash(bytes, len), hex);
    if (tdReadFile(path, &cur, &curLen) == 0) {
        exists = 1;
        tdHashHex(tdHash(cur, curLen), curHex);
        free(cur);
    }
    if (exists && !strcmp(curHex, hex)) {
        st->kept++;
        tdManifestSet(&sMan, rel, hex);
        return 0;
    }
    if (exists && tdFileState(1, man, curHex) == TD_FILE_EDITED) {
        snprintf(np, sizeof np, "%s.new", path);
        if (tdWriteFileAtomic(np, bytes, len)) { /* the folder exists: the edited file is in it */
            plogLine("stages", "cannot write %s\n", np);
            return -1;
        }
        st->newFiles++;
        plogLine("stages", "%s/%s was edited, kept; the new version is %s.new\n", key, rel, rel);
        return 0;
    }
    if (tdMakeDirs(dir) || tdWriteFileAtomic(path, bytes, len)) {
        plogLine("stages", "cannot write %s\n", path);
        return -1;
    }
    tdManifestSet(&sMan, rel, hex);
    st->written++;
    st->bytes += (long)len;
    return 0;
}

/* Reads `sectors` sectors from `lba` through the disc reader into out (sectors * 2048 bytes); 1 when read */
static int readSectors(const StageExportIo *io, long lba, int sectors, uint8_t *out) {
    uint8_t frame[RAW_SECTOR];
    int i;

    for (i = 0; i < sectors; i++) {
        if (!io->read(io->ud, lba + i, frame)) return 0;
        memcpy(out + (size_t)i * USER_BYTES, frame + USER_AT, USER_BYTES);
    }
    return 1;
}

/* One PACK to files. 0: built, -1: something failed (the stage keeps no version mark, so the next request tries again) */
static int exportPack(int index, int force, StageExportStats *total) {
    const char *name = sQueue[index];
    long lba = sQLba[index], size = sQSize[index];
    char key[24], err[200], dir[1200], rel[48], hex[17];
    uint8_t head[USER_BYTES], *buf = NULL;
    PackHeader pk;
    TileInfo *tiles = NULL;
    int nTiles = 0, i, sectorsPerTile, failed = 0;
    StageExportStats st;
    Sb js = {0};
    clock_t t0 = clock();

    memset(&st, 0, sizeof st);
    if (!stagePackKey(name, key, sizeof key)) return -1;
    if (!force && upToDate(key)) return 0;
    manifestLoad(key);
    packDir(key, dir, sizeof dir);
    if (size < USER_BYTES || !readSectors(&sIo, lba, 1, head) || stagePackParse(head, sizeof head, &pk, err, sizeof err)) {
        plogLine("stages", "%s not exported: %s\n", name, size < USER_BYTES ? "empty file" : "the header cannot be read");
        if (size >= USER_BYTES && readSectors(&sIo, lba, 1, head)) plogLine("stages", "%s: %s\n", name, err);
        return -1;
    }
    sectorsPerTile = pk.tileBytes / USER_BYTES;
    buf = (uint8_t *)malloc((size_t)pk.tileBytes);
    tiles = (TileInfo *)calloc((size_t)(pk.width * pk.height), sizeof *tiles);
    if (buf == NULL || tiles == NULL) {
        free(buf);
        free(tiles);
        plogLine("stages", "%s not exported: out of memory\n", name);
        return -1;
    }
    for (i = 0; i < pk.width * pk.height && !sStop; i++) {
        TileInfo *ti;
        ImgPic pic;
        uint8_t *png;
        size_t pngLen;
        PackTile t;
        long sector = (long)i * sectorsPerTile + 1;
        long avail = (size + USER_BYTES - 1) / USER_BYTES - sector; /* the last tile may be cut short by the end of the file */
        int n = avail < sectorsPerTile ? (int)avail : sectorsPerTile;

        if (pk.sizes[i] == 0) continue;
        st.tiles++;
        if (n <= 0 || !readSectors(&sIo, lba + sector, n, buf)) {
            plogLine("stages", "%s tile %d: cannot read sector %ld\n", name, i, sector);
            failed++;
            continue;
        }
        if (n < sectorsPerTile) memset(buf + (size_t)n * USER_BYTES, 0, (size_t)(sectorsPerTile - n) * USER_BYTES);
        if (stageTileParse(buf, (size_t)pk.tileBytes, &t, err, sizeof err)) {
            plogLine("stages", "%s tile %d: %s\n", name, i, err);
            failed++;
            continue;
        }
        if (imgDecodeTim(t.data + t.timOff, t.timLen, NULL, &pic)) {
            plogLine("stages", "%s tile %d: the TIM cannot be decoded\n", name, i);
            stageTileFree(&t);
            failed++;
            continue;
        }
        if (imgPicToPng(&pic, &png, &pngLen)) {
            plogLine("stages", "%s tile %d: the PNG cannot be built\n", name, i);
            imgPicFree(&pic);
            stageTileFree(&t);
            failed++;
            continue;
        }
        stageTileName(pk.width, i, rel, sizeof rel);
        if (placeFile(key, rel, png, pngLen, &st)) failed++;
        free(png);
        ti = &tiles[nTiles++];
        ti->frame = i;
        ti->size = pk.sizes[i];
        ti->w = pic.w;
        ti->h = pic.h;
        ti->t = t;
        ti->t.data = NULL; /* only the lists are kept */
        ti->t.len = 0;
        imgPicFree(&pic);
        stageTileFree(&t);
    }
    if (sStop) {
        free(buf);
        free(tiles);
        manifestFree();
        return -1;
    }
    /* the descriptor */
    js.cap = 4096;
    js.p = (char *)malloc(js.cap);
    sbPrintf(&js, "{\n  \"format\": \"stage-pack-v1\",\n  \"pack\": \"%s\",\n  \"width\": %d,\n  \"height\": %d,\n  \"tileBytes\": %d,\n"
                  "  \"tileCount\": %d,\n  \"tiles\": [\n",
             name, pk.width, pk.height, pk.tileBytes, nTiles);
    for (i = 0; i < nTiles; i++) jsonTile(&js, pk.width, &tiles[i], i == 0);
    sbPrintf(&js, "\n  ]\n}\n");
    snprintf(rel, sizeof rel, "%s.json", key);
    if (js.p == NULL || placeFile(key, rel, (const uint8_t *)js.p, js.n, &st)) failed++;
    free(js.p);
    free(buf);
    free(tiles);
    st.failed = failed;
    if (!failed) {
        versionHex(hex);
        tdManifestSet(&sMan, "exporter", hex);
    }
    if (manifestSave(key)) st.failed++;
    plogLine("stages", "%s: %d tiles, %d files written, %d kept, %d new beside edited, %d failed, %ld bytes, %.1f s\n", name, st.tiles, st.written,
             st.kept, st.newFiles, st.failed, st.bytes, (double)(clock() - t0) / CLOCKS_PER_SEC);
    lockMu();
    sBuilt++;
    total->tiles += st.tiles;
    total->written += st.written;
    total->kept += st.kept;
    total->newFiles += st.newFiles;
    total->failed += st.failed;
    total->bytes += st.bytes;
    unlockMu();
    return st.failed ? -1 : 0;
}

static void finishStatus(void) {
    if (sFailed) snprintf(sStatus, sizeof sStatus, "Done, %d failed", sFailed);
    else snprintf(sStatus, sizeof sStatus, "Done (%d)", sDone);
    plogLine("stages", "Done: %d stages in %s/z_stage, %d failed, %d tiles, %d files written, %ld bytes\n", sDone, sDataDir, sFailed, sTotals.tiles,
             sTotals.written, sTotals.bytes);
}

static int workerMain(void *arg) {
    int j;
    (void)arg;
    for (j = 0; j < sCount && !sStop; j++) {
        int r = exportPack(j, sForce, &sTotals);
        SDL_LockMutex(sMu);
        if (r == 0) sDone++;
        else if (!sStop) sFailed++;
        sNext = j + 1;
        if (sNext < sCount) snprintf(sStatus, sizeof sStatus, "Exporting %d/%d", sNext + 1, sCount);
        SDL_UnlockMutex(sMu);
    }
    SDL_LockMutex(sMu);
    if (j >= sCount) finishStatus();
    sWorkerDone = 1;
    SDL_UnlockMutex(sMu);
    return 0;
}

static int workerBusy(void) {
    int busy;
    if (sThread == NULL) return 0;
    lockMu();
    busy = !sWorkerDone;
    unlockMu();
    return busy;
}

/* the queue's memory; the counts stay for the status and the totals */
static void freeQueue(void) {
    free(sQueue);
    free(sQLba);
    free(sQSize);
    sQueue = NULL;
    sQLba = sQSize = NULL;
}

static void dropJob(void) {
    freeQueue();
    sCount = sNext = 0;
}

static void reap(void) {
    if (sThread == NULL) return;
    SDL_WaitThread(sThread, NULL);
    sThread = NULL;
    freeQueue();
}

void stageExportSetup(const char *dataDir, const StageExportIo *io) {
    stageExportShutdown();
    dropJob();
    manifestFree();
    sStatus[0] = '\0';
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

int stageExportRequest(const char *spec) { return stageExportRequestEx(spec, 0); }

int stageExportRequestEx(const char *spec, int force) {
    char (*q)[16];
    long *ql, *qs;
    char want[MAX_PACKS][24];
    int nWant = 0, all = 0, n = 0, i;
    const char *p;

    if (workerBusy()) return -1; /* one job at a time: a press while one runs changes nothing */
    reap();
    dropJob();
    sDone = sFailed = sBuilt = 0;
    memset(&sTotals, 0, sizeof sTotals);
    sForce = force;
    sStatus[0] = '\0';
    if (!sDataDir[0] || sIo.pack == NULL || sIo.read == NULL || spec == NULL) return -1;
    p = spec;
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, "all", 3) && (p[3] == '\0' || p[3] == ' ' || p[3] == ',' || p[3] == '\t')) {
        all = 1;
    } else {
        while (*p) {
            const char *s;
            char tok[40], key[24];
            size_t len;

            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            if (!*p) break;
            s = p;
            while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
            len = (size_t)(p - s);
            if (len >= sizeof tok) len = sizeof tok - 1;
            memcpy(tok, s, len);
            tok[len] = '\0';
            if (!stagePackKey(tok, key, sizeof key)) {
                plogLine("stages", "export_stages: \"%s\" is not a stage (use all or names like S233 separated by commas)\n", tok);
                continue;
            }
            for (i = 0; i < nWant; i++) {
                if (!strcmp(want[i], key)) break;
            }
            if (i == nWant && nWant < MAX_PACKS) strcpy(want[nWant++], key);
        }
        if (nWant == 0) return -1;
    }
    q = (char(*)[16])malloc(sizeof *q * MAX_PACKS);
    ql = (long *)malloc(sizeof(long) * MAX_PACKS);
    qs = (long *)malloc(sizeof(long) * MAX_PACKS);
    if (q == NULL || ql == NULL || qs == NULL) {
        free(q);
        free(ql);
        free(qs);
        return -1;
    }
    for (i = 0; n < MAX_PACKS; i++) {
        char name[16], key[24];
        long lba, size;
        if (!sIo.pack(sIo.ud, i, name, sizeof name, &lba, &size)) break;
        if (!stagePackKey(name, key, sizeof key)) continue;
        if (!all) {
            int k;
            for (k = 0; k < nWant; k++) {
                if (!strcmp(want[k], key)) break;
            }
            if (k == nWant) continue;
            want[k][0] = '\0'; /* found */
        }
        snprintf(q[n], sizeof q[n], "%s", name);
        ql[n] = lba;
        qs[n] = size;
        n++;
    }
    for (i = 0; !all && i < nWant; i++) {
        if (want[i][0]) plogLine("stages", "export_stages: %s is not on this disc\n", want[i]);
    }
    if (n == 0) {
        free(q);
        free(ql);
        free(qs);
        return -1;
    }
    sQueue = q;
    sQLba = ql;
    sQSize = qs;
    sCount = n;
    sNext = 0;
    snprintf(sStatus, sizeof sStatus, "Exporting 1/%d", n);
    return n;
}

int stageExportStart(const char *spec, int force) {
    int n = stageExportRequestEx(spec, force);

    if (n <= 0) return -1;
    if (sMu == NULL) sMu = SDL_CreateMutex();
    if (sMu != NULL) {
        sStop = 0;
        sWorkerDone = 0;
        sThread = SDL_CreateThread(workerMain, "dw3-stage-export", NULL);
    }
    if (sThread == NULL) {
        plogLine("stages", "the export thread cannot start: %s\n", SDL_GetError());
        dropJob();
        return -1;
    }
    return n;
}

void stageExportShutdown(void) {
    if (sThread == NULL) return;
    sStop = 1;
    reap();
    sStop = 0;
}

int stageExportActive(void) {
    if (sThread != NULL) return workerBusy();
    return sQueue != NULL && sNext < sCount;
}

void stageExportCounts(int *done, int *failed, int *total) {
    lockMu();
    if (done) *done = sDone;
    if (failed) *failed = sFailed;
    if (total) *total = sCount;
    unlockMu();
}

int stageExportBuilt(void) {
    int n;
    lockMu();
    n = sBuilt;
    unlockMu();
    return n;
}

void stageExportTotals(StageExportStats *out) {
    lockMu();
    *out = sTotals;
    unlockMu();
}

const char *stageExportStatus(void) {
    lockMu();
    snprintf(sStatusOut, sizeof sStatusOut, "%s", sStatus);
    unlockMu();
    return sStatusOut;
}

int stageExportPump(unsigned budgetMs) {
    clock_t start = clock();

    if (sThread != NULL) {
        if (workerBusy()) return 1;
        reap();
        return 0;
    }
    if (!stageExportActive()) return 0;
    do {
        if (exportPack(sNext, sForce, &sTotals) == 0) sDone++;
        else sFailed++;
        sNext++;
        if (sNext < sCount) snprintf(sStatus, sizeof sStatus, "Exporting %d/%d", sNext + 1, sCount);
    } while (sNext < sCount && (unsigned)((clock() - start) * 1000 / CLOCKS_PER_SEC) < budgetMs);
    if (sNext < sCount) return 1;
    finishStatus();
    freeQueue();
    return 0;
}
