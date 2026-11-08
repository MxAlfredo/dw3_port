#define _GNU_SOURCE 1
/*
 * Host test of the model baker (pc/shim/model_bake.c, model-import I5): the neutral source (model.json + glTF + PNG) must bake to the
 * very bytes tools/model_import.py makes, errors never crash, the cache gives the same blob, and the registry of the active mods
 * serves the baked archive as a virtual file. No game, no disc, no window.
 *
 *   modeltest <fixtures> [<parity>]    <fixtures>/<case>/ and <parity>/<case>/ hold model.json, the glTF, the PNG and expected.bin
 *   modeltest --bake <source dir> <out.bin> [--time]    bake one source (checking a re-exported model against the tool)
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL.h>

#include "shim/content.h"
#include "shim/mods.h"
#include "shim/model_bake.h"
#include "shim/model_export.h"
#include "shim/port_fs.h"
#include "shim/port_paths.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "modeltest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gRoot[600];
static int gSavedErr;
static const char *gLogPath = "/tmp/modeltest.log";

static void logBegin(void) {
    int fd;
    fflush(stderr);
    gSavedErr = dup(2);
    fd = open(gLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

static char *logEnd(void) {
    static char buf[16384];
    FILE *f;
    size_t n = 0;
    fflush(stderr);
    dup2(gSavedErr, 2);
    close(gSavedErr);
    f = fopen(gLogPath, "rb");
    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }
    buf[n] = 0;
    return buf;
}

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    long n;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = (unsigned char *)malloc((size_t)n + 1);
    if (p == NULL || fread(p, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(p);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return p;
}

static void spit(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(data, 1, len, f);
        fclose(f);
    }
}

static void spits(const char *path, const char *text) { spit(path, text, strlen(text)); }

static void sh(const char *fmt, const char *a) {
    char cmd[1500];
    snprintf(cmd, sizeof cmd, fmt, a);
    if (system(cmd)) {}
}

static double nowMs(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static int sWarnings;
static void countLog(void *ud, int warning, const char *msg) {
    (void)ud;
    (void)msg;
    if (warning) sWarnings++;
}

/* one case folder: the bake against expected.bin; 1 when it matched */
static int bakeCase(const char *dir, const char *name) {
    char path[900], err[320];
    size_t elen = 0, i;
    unsigned char *want;
    ModelBaked b;
    int ok = 0;

    snprintf(path, sizeof path, "%s/expected.bin", dir);
    want = slurp(path, &elen);
    if (want == NULL) {
        printf("modeltest: %s: no expected.bin\n", name);
        failures++;
        return 0;
    }
    if (modelBake(dir, "model.json", countLog, NULL, NULL, &b, err, sizeof err) != 0) {
        printf("modeltest: FAIL %s: rejected: %s\n", name, err);
        failures++;
    } else if (b.len != elen) {
        printf("modeltest: FAIL %s: %lu bytes, the tool's blob has %lu\n", name, (unsigned long)b.len, (unsigned long)elen);
        for (i = 0; i < b.len && i < elen && b.blob[i] == want[i]; i++) {}
        printf("modeltest:   first difference at byte %lu\n", (unsigned long)i);
        failures++;
        modelBakedFree(&b);
    } else if (memcmp(b.blob, want, elen) != 0) {
        for (i = 0; i < elen && b.blob[i] == want[i]; i++) {}
        printf("modeltest: FAIL %s: first difference at byte %lu of %lu (%02x, the tool's %02x)\n", name, (unsigned long)i, (unsigned long)elen,
               b.blob[i], want[i]);
        failures++;
        modelBakedFree(&b);
    } else {
        ok = 1;
        modelBakedFree(&b);
    }
    free(want);
    return ok;
}

static int cases(const char *root) {
    DIR *d = opendir(root);
    struct dirent *e;
    int n = 0, ok = 0;
    if (d == NULL) return 0;
    while ((e = readdir(d)) != NULL) {
        char dir[900];
        struct stat st;
        if (e->d_name[0] == '.') continue;
        snprintf(dir, sizeof dir, "%s/%s", root, e->d_name);
        if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        n++;
        ok += bakeCase(dir, e->d_name);
    }
    closedir(d);
    printf("modeltest: %s: %d of %d cases are byte-identical to tools/model_import.py\n", root, ok, n);
    return n;
}

/* ---- the cache hooks, in memory ---- */
static struct {
    uint64_t key;
    unsigned char *blob;
    size_t len;
    int gets, hits, puts;
} gMem;

static int memGet(void *ud, uint64_t key, uint8_t **blob, size_t *len) {
    (void)ud;
    gMem.gets++;
    if (gMem.blob == NULL || gMem.key != key) return 0;
    *blob = (uint8_t *)malloc(gMem.len);
    memcpy(*blob, gMem.blob, gMem.len);
    *len = gMem.len;
    gMem.hits++;
    return 1;
}

static void memPut(void *ud, uint64_t key, const uint8_t *blob, size_t len) {
    (void)ud;
    free(gMem.blob);
    gMem.blob = (unsigned char *)malloc(len);
    memcpy(gMem.blob, blob, len);
    gMem.len = len;
    gMem.key = key;
    gMem.puts++;
}

static void testCache(const char *dir) {
    ModelCacheHooks h = {memGet, memPut, NULL};
    ModelBaked a, b;
    char err[320];
    memset(&gMem, 0, sizeof gMem);
    CHECK(modelBake(dir, "model.json", NULL, NULL, &h, &a, err, sizeof err) == 0 && !a.cached && gMem.puts == 1);
    CHECK(modelBake(dir, "model.json", NULL, NULL, &h, &b, err, sizeof err) == 0 && b.cached && gMem.hits == 1 && gMem.puts == 1);
    CHECK(a.len == b.len && memcmp(a.blob, b.blob, a.len) == 0 && a.key == b.key);
    modelBakedFree(&a);
    modelBakedFree(&b);
}

/* ---- source folders in a scratch place ---- */

static void copyDir(const char *from, const char *to) {
    char cmd[2000];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s' && cp '%s'/m.* '%s'/model.json '%s'/", to, to, from, from, to);
    if (system(cmd)) {}
}

static const char *bakeErr(const char *dir) {
    static char err[320];
    ModelBaked b;
    err[0] = 0;
    if (modelBake(dir, "model.json", NULL, NULL, NULL, &b, err, sizeof err) == 0) {
        modelBakedFree(&b);
        return "";
    }
    return err;
}

static void testErrors(const char *fixture) {
    char dir[900], path[900];
    unsigned char *m;
    size_t n;
    const char *e;

    snprintf(dir, sizeof dir, "%s/bad", gRoot);
    copyDir(fixture, dir);
    CHECK(bakeErr(dir)[0] == 0);
    /* a missing texture */
    snprintf(path, sizeof path, "%s/m.png", dir);
    m = slurp(path, &n);
    remove(path);
    e = bakeErr(dir);
    CHECK(strstr(e, "texture file not found") != NULL);
    spits(path, "not a png at all");
    CHECK(strstr(bakeErr(dir), "not a PNG") != NULL);
    spit(path, m, n);
    free(m);
    /* a missing buffer */
    snprintf(path, sizeof path, "%s/m.bin", dir);
    m = slurp(path, &n);
    remove(path);
    CHECK(strstr(bakeErr(dir), "buffer file not found") != NULL);
    spit(path, m, n / 2);
    CHECK(strstr(bakeErr(dir), "past the end") != NULL);
    spit(path, m, n);
    free(m);
    /* the JSON files */
    snprintf(path, sizeof path, "%s/model.json", dir);
    spits(path, "{\"format\":\"nope\"}");
    CHECK(strstr(bakeErr(dir), "format") != NULL);
    spits(path, "[1,2");
    CHECK(strstr(bakeErr(dir), "not a JSON object") != NULL);
    spits(path, "{\"format\":\"model-src-v1\",\"name\":\"x\",\"gltf\":\"../escape.gltf\"}");
    CHECK(strstr(bakeErr(dir), "no \"..\"") != NULL);
    spits(path, "{\"format\":\"model-src-v1\",\"name\":\"x\",\"gltf\":\"m.gltf\",\"scale\":1,\"fitHeight\":2,\"motions\":{\"0\":\"idle\",\"1\":\"idle\"}}");
    CHECK(strstr(bakeErr(dir), "not both") != NULL);
    spits(path, "{\"format\":\"model-src-v1\",\"name\":\"x\",\"gltf\":\"m.gltf\",\"motions\":{\"0\":\"nowhere\",\"1\":\"idle\"}}");
    CHECK(strstr(bakeErr(dir), "no animation") != NULL);
    spits(path, "{\"format\":\"model-src-v1\",\"name\":\"x\",\"gltf\":\"m.gltf\",\"motions\":{\"0\":\"idle\"}}");
    CHECK(strstr(bakeErr(dir), "slot 1 (idle) is required") != NULL);
    spits(path, "{\"format\":\"model-src-v1\",\"name\":\"x\",\"gltf\":\"m.gltf\",\"fitHeight\":1800,\"scale\":0,\"motions\":{\"0\":\"idle\",\"1\":2}}");
    CHECK(bakeErr(dir)[0] != 0);
    snprintf(path, sizeof path, "%s/m.gltf", dir);
    spits(path, "{\"asset\":{\"version\":\"1.0\"}}");
    CHECK(bakeErr(dir)[0] != 0);
    spits(path, "{");
    CHECK(strstr(bakeErr(dir), "not valid JSON") != NULL);
    sh("rm -rf '%s'", dir);
}

/* a deterministic byte-flip fuzz: whatever the sources hold, the baker returns (a blob or a reason) and never crashes */
static unsigned sRng = 12345u;
static unsigned rnd(void) {
    sRng = sRng * 1664525u + 1013904223u;
    return sRng >> 8;
}

static void testFuzz(const char *fixture, int rounds) {
    static const char *const files[4] = {"m.gltf", "m.bin", "m.png", "model.json"};
    char dir[900], path[900];
    unsigned char *orig[4];
    size_t len[4];
    int i, r, rejected = 0, baked = 0;

    snprintf(dir, sizeof dir, "%s/fuzz", gRoot);
    copyDir(fixture, dir);
    for (i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        orig[i] = slurp(path, &len[i]);
    }
    for (r = 0; r < rounds; r++) {
        int f = (int)(rnd() % 4), flips = 1 + (int)(rnd() % 6), k;
        unsigned char *copy = (unsigned char *)malloc(len[f] + 1);
        size_t cut = len[f];
        ModelBaked b;
        char err[320];
        memcpy(copy, orig[f], len[f]);
        for (k = 0; k < flips; k++) copy[rnd() % len[f]] = (unsigned char)rnd();
        if (rnd() % 8 == 0) cut = rnd() % len[f];
        snprintf(path, sizeof path, "%s/%s", dir, files[f]);
        spit(path, copy, cut);
        if (modelBake(dir, "model.json", NULL, NULL, NULL, &b, err, sizeof err) == 0) {
            baked++;
            CHECK(b.len > 0 && b.len % 8 == 0);
            modelBakedFree(&b);
        } else {
            rejected++;
            CHECK(err[0] != 0);
        }
        spit(path, orig[f], len[f]);
        free(copy);
    }
    for (i = 0; i < 4; i++) free(orig[i]);
    printf("modeltest: fuzz: %d rounds, %d baked, %d rejected, no crash\n", rounds, baked, rejected);
    sh("rm -rf '%s'", dir);
}

/* ---- the registry of the mods and the cache on disk ---- */

static void testRegistry(const char *fixture) {
    char mods[900], dir[900], path[900], want[900], user[900];
    unsigned char *exp, *got2;
    size_t elen = 0, glen = 0;
    const uint8_t *served;
    ModelSrcInfo info;
    char *log;
    int f1, f2, f3;
    double t0, tCold, tWarm;

    snprintf(user, sizeof user, "%s/user", gRoot);
    snprintf(mods, sizeof mods, "%s/mods", gRoot);
    sh("mkdir -p '%s/cache'", user);
    setenv("DW3_USER_DIR", user, 1);
    unsetenv("DW3_CACHE_DIR");
    portPathsReset();
    snprintf(dir, sizeof dir, "%s/rm", mods);
    sh("mkdir -p '%s/digimon'", dir);
    snprintf(path, sizeof path, "%s/mod.json", dir);
    spits(path, "{\"format\":\"mod-v1\",\"name\":\"rm\",\"version\":\"1.0\",\"kind\":\"content\"}");
    snprintf(dir, sizeof dir, "%s/rm/digimon/robot/model", mods);
    copyDir(fixture, dir);
    snprintf(dir, sizeof dir, "%s/rm/digimon/d012/model", mods); /* the folder of original fighter 12: reserved, its model/ is never read */
    copyDir(fixture, dir);
    snprintf(dir, sizeof dir, "%s/rm/content/models/old", mods); /* the layout of before A8.3: not read */
    copyDir(fixture, dir);
    snprintf(dir, sizeof dir, "%s/rm/digimon/bad/model", mods);
    copyDir(fixture, dir);
    snprintf(path, sizeof path, "%s/m.png", dir);
    remove(path);
    modsInit(mods, NULL);
    pfsInit(NULL, 100); /* no disc: the virtual numbers start at 100 */
    pfsClearVirtualFiles();
    modelsReset();

    snprintf(want, sizeof want, "%s/expected.bin", fixture);
    exp = slurp(want, &elen);
    CHECK(exp != NULL);
    CHECK(modelsHas("rm:robot") && modelsHas("rm:bad") && !modelsHas("rm:none") && !modelsHas("zz:robot") && !modelsHas("rm") && !modelsHas("rm:Robot") && !modelsHas("rm:d012") && !modelsHas("rm:old"));
    CHECK(modelsInfo("rm:robot", &info) && info.templateId == 4 && info.effectsId == 383 && info.distance == -1500 && info.height == INT_MIN);
    logBegin();
    t0 = nowMs();
    f1 = modelsFile("rm:robot");
    tCold = nowMs() - t0;
    f2 = modelsFile("rm:robot");
    f3 = modelsFile("rm:bad");
    log = logEnd();
    CHECK(f1 >= 100 && f1 == f2 && f3 == -1);
    CHECK(strstr(log, "[models] rm:robot: baked 5 bones") != NULL);
    CHECK(strstr(log, "[models] rm:bad rejected: texture file not found") != NULL);
    served = pfsResolve(f1, &glen, 0, NULL);
    CHECK(served != NULL && glen == elen && exp != NULL && memcmp(served, exp, elen) == 0);
    {   /* P8: the slots with a motion, read from the baked archive (the robot has "fill": all 62), and from a cache hit alike */
        uint64_t mask = 0;
        const ModelSlotNeed *need;
        int n, i, last = -1;

        CHECK(modelsSlots("rm:robot", &mask) && mask == (((uint64_t)1 << 62) - 1) && modelBlobSlots(served, glen) == mask);
        CHECK(!modelsSlots("rm:bad", &mask) && !modelsSlots("rm:none", &mask) && modelBlobSlots(NULL, 0) == 0);
        n = modelSlotNeeds(0, &need); /* an enemy's: the generic minimum of the baker's own warning (0, 1, 2, 3, 4, 10, 11, 12, 14) */
        CHECK(n == 9 && need[0].slot == 0 && need[8].slot == 14);
        n = modelSlotNeeds(1, &need); /* a partner's adds 13 (blast entrance), 20 and 32 (the attack scripts 6 and 8) */
        CHECK(n == 12);
        for (i = 0; i < n; i++) {
            CHECK(need[i].slot > last && need[i].slot < 62 && need[i].label[0] != '\0' && need[i].why[0] != '\0');
            last = need[i].slot;
        }
    }
    /* the cache file is on disk; a reload (virtual files cleared) takes the blob from it */
    snprintf(path, sizeof path, "%s/cache/models", user);
    {
        DIR *d = opendir(path);
        int files = 0;
        struct dirent *e;
        while (d != NULL && (e = readdir(d)) != NULL) if (e->d_name[0] != '.') files++;
        if (d != NULL) closedir(d);
        CHECK(files == 1);
    }
    pfsClearVirtualFiles();
    modelsReset();
    logBegin();
    t0 = nowMs();
    f1 = modelsFile("rm:robot");
    tWarm = nowMs() - t0;
    log = logEnd();
    CHECK(f1 >= 100 && strstr(log, "from the cache") != NULL);
    served = pfsResolve(f1, &glen, 0, NULL);
    CHECK(served != NULL && glen == elen && exp != NULL && memcmp(served, exp, elen) == 0);
    {
        uint64_t mask = 0;
        CHECK(modelsSlots("rm:robot", &mask) && mask == (((uint64_t)1 << 62) - 1)); /* the same from the cache */
    }
    printf("modeltest: registry: cold %.2f ms, from the cache %.2f ms\n", tCold, tWarm);
    /* a changed source is a new key: the old cache file is not used for it */
    snprintf(path, sizeof path, "%s/rm/digimon/robot/model/model.json", mods);
    got2 = slurp(path, &glen);
    if (got2 != NULL) {
        char *at = strstr((char *)got2, "1800");
        if (at != NULL) at[2] = '5';
        spit(path, got2, glen);
        free(got2);
    }
    pfsClearVirtualFiles();
    modelsReset();
    logBegin();
    f1 = modelsFile("rm:robot");
    log = logEnd();
    CHECK(f1 >= 100 && strstr(log, "from the cache") == NULL && strstr(log, "baked") != NULL);
    /* a damaged cache file is ignored and replaced */
    {
        char cdir[900];
        DIR *d;
        struct dirent *e;
        snprintf(cdir, sizeof cdir, "%s/cache/models", user);
        d = opendir(cdir);
        while (d != NULL && (e = readdir(d)) != NULL) {
            if (e->d_name[0] != '.') {
                snprintf(path, sizeof path, "%s/%s", cdir, e->d_name);
                spits(path, "DW3Mxxxxxxxxxxxx");
            }
        }
        if (d != NULL) closedir(d);
    }
    pfsClearVirtualFiles();
    modelsReset();
    logBegin();
    f1 = modelsFile("rm:robot");
    log = logEnd();
    CHECK(f1 >= 100 && strstr(log, "from the cache") == NULL && strstr(log, "baked") != NULL);
    free(exp);
    pfsClearVirtualFiles();
    modelsReset();
    unsetenv("DW3_USER_DIR");
    portPathsReset();
}


/* ---- the exporter (model_export.c, I7): files by number from a folder of <n>.bin ---- */

static char gBlobDir[900];
static uint8_t *gBlobBuf;

static const uint8_t *dirFile(void *ud, int file, size_t *len) {
    char path[1000];
    (void)ud;
    free(gBlobBuf);
    snprintf(path, sizeof path, "%s/%d.bin", gBlobDir, file);
    gBlobBuf = slurp(path, len);
    return gBlobBuf;
}

static void putLe32(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }

/* a fighters file with a partner (100) and an enemy (101) that both use entries 1 (model) and 0 (motions) of file 1 */
static void makeFighters(unsigned char *f /* 0x12C */) {
    int i;
    memset(f, 0, 0x12C);
    putLe32(f + 4, 16);
    putLe32(f + 8, 32);
    putLe32(f + 12, 0xE4);
    f[16] = 100; f[18] = 0; f[19] = 0x10;
    f[20] = 101; f[22] = 0; f[23] = 0x3A;
    for (i = 0; i < 2; i++) {
        unsigned char *r = f + (i ? 0xE4 : 32);
        putLe32(r, (1u << 16) | 1);
        putLe32(r + 4, (1u << 16) | 0);
        putLe32(r + 8, 2u << 16);
        r[0x10] = (unsigned char)(-1500 & 0xFF); r[0x11] = (unsigned char)((-1500 >> 8) & 0xFF);
        r[0x18] = (unsigned char)(321 & 0xFF); r[0x19] = (unsigned char)(321 >> 8);
    }
}

static void testExport(const char *robot, const char *xparity) {
    char src[900], dir[900], path[1000], err[320], *log;
    ModelBaked b;
    ModelExportIo io;
    ModelExportOut o;
    unsigned char fighters[0x12C];
    int ids[8], n, d1, f1, f2, i;
    size_t len = 0;
    unsigned char *edited;

    snprintf(gBlobDir, sizeof gBlobDir, "%s/blobs", gRoot);
    sh("mkdir -p '%s'", gBlobDir);
    snprintf(src, sizeof src, "%s", robot);
    CHECK(modelBake(src, "model.json", NULL, NULL, NULL, &b, err, sizeof err) == 0);
    snprintf(path, sizeof path, "%s/1.bin", gBlobDir);
    spit(path, b.blob, b.len);
    makeFighters(fighters);
    snprintf(path, sizeof path, "%s/%d.bin", gBlobDir, MODEL_EXPORT_FILE_FIGHTERS);
    spit(path, fighters, sizeof fighters);
    io.file = dirFile;
    io.ud = NULL;
    n = modelExportIds(&io, ids, 8);
    CHECK(n == 2 && ids[0] == 100 && ids[1] == 101);
    CHECK(modelExportBuild(&io, 100, MODEL_EXPORT_UNIT_SCALE, &o, err, sizeof err) == 0);
    CHECK(o.bones == b.bones + 0 || o.bones > 1);
    CHECK(o.gltfLen > 1000 && o.binLen > 100 && o.pngLen > 60 && o.anims > 0 && o.tris > 0);
    CHECK(o.json != NULL && strstr((char *)o.json, "\"scale\": 1000") != NULL && strstr((char *)o.json, "\"distance\": -1500") != NULL);
    modelExportFree(&o);
    CHECK(modelExportBuild(&io, 999, MODEL_EXPORT_UNIT_SCALE, &o, err, sizeof err) != 0 && strstr(err, "no fighter") != NULL);
    /* export, then bake the exported source back: the same bones and polygons (the scale is undone by model.json) */
    snprintf(dir, sizeof dir, "%s/data", gRoot);
    modelExportSetup(dir, &io);
    sh("mkdir -p '%s/data/models'", gRoot); /* the folder of before A8.3 */
    logBegin();
    CHECK(modelExportRequest("100, 101, 7, nonsense") == 2);
    log = logEnd();
    CHECK(strstr(log, "models/ is the old layout of the models and is not read any more") != NULL);
    CHECK(modelExportActive());
    while (modelExportPump(1000)) {}
    {
        size_t ml = 0;
        char mp[900];
        unsigned char *mf;
        snprintf(mp, sizeof mp, "%s/data/digimon/manifest.json", gRoot);
        mf = slurp(mp, &ml);
        CHECK(mf != NULL && strstr((char *)mf, "\"d100\": \"dw3:digimon/100\"") != NULL && strstr((char *)mf, "\"d101\": \"dw3:digimon/101\"") != NULL);
        free(mf);
        snprintf(mp, sizeof mp, "%s/data/models/100", gRoot);
        CHECK(access(mp, F_OK) != 0);
    }
    modelExportCounts(&d1, &f1, &f2);
    CHECK(d1 == 2 && f1 == 0 && f2 == 2 && strstr(modelExportStatus(), "Done (2)") != NULL);
    snprintf(dir, sizeof dir, "%s/data/digimon/d100/model", gRoot);
    {
        ModelBaked b2;
        CHECK(modelBake(dir, "model.json", NULL, NULL, NULL, &b2, err, sizeof err) == 0);
        if (b2.blob != NULL) {
            CHECK(b2.bones == b.bones);
            CHECK(b2.polys > 0);
            modelBakedFree(&b2);
        } else printf("modeltest: re-bake failed: %s\n", err);
    }
    if (xparity != NULL) {
        sh("rm -rf '%s'", xparity);
        sh("mkdir -p '%s'", xparity);
        snprintf(path, sizeof path, "cp -r '%s' '%%s/blobs'", gBlobDir);
        sh(path, xparity);
        snprintf(path, sizeof path, "cp -r '%s/data/digimon' '%%s/c'", gRoot);
        sh(path, xparity);
    }
    /* idempotent: a fighter written by this exporter version with its four files in place is skipped without building it */
    CHECK(modelExportRequest("100, 101") == 2);
    while (modelExportPump(1000)) {}
    modelExportCounts(&d1, &f1, &f2);
    CHECK(d1 == 2 && f1 == 0 && modelExportBuilt() == 0);
    snprintf(path, sizeof path, "%s/data/digimon/d101/model/model.png", gRoot);
    CHECK(remove(path) == 0);
    CHECK(modelExportRequest("100, 101") == 2);
    while (modelExportPump(1000)) {}
    CHECK(modelExportBuilt() == 1 && access(path, F_OK) == 0);
    CHECK(modelExportRequestEx("100, 101", 1) == 2);
    while (modelExportPump(1000)) {}
    CHECK(modelExportBuilt() == 2);
    CHECK(modelExportRun("100, 101") == 2 && modelExportBuilt() == 0); /* the settings.ini key: synchronous, all up to date */
    /* a manifest from before the version existed: rebuilt once, then skipped again */
    snprintf(path, sizeof path, "%s/data/digimon/manifest.json", gRoot);
    sh("sed -i 's#\"\\(d10[01]\\)\": [0-9][0-9]*#\"\\1\": 0#g' '%s'", path);
    snprintf(dir, sizeof dir, "%s/data", gRoot);
    modelExportSetup(dir, &io);
    CHECK(modelExportRequest("100, 101") == 2);
    while (modelExportPump(1000)) {}
    CHECK(modelExportBuilt() == 2);
    CHECK(modelExportRequest("100, 101") == 2);
    while (modelExportPump(1000)) {}
    CHECK(modelExportBuilt() == 0);
    /* the manifest rule: an edited file stays and the fresh one goes beside it as .new; an unedited one is replaced silently */
    snprintf(path, sizeof path, "%s/data/digimon/d100/model/model.json", gRoot);
    edited = slurp(path, &len);
    spits(path, "{\"edited\": true}\n");
    snprintf(path, sizeof path, "%s/data/digimon/d100/model/model.bin", gRoot);
    spits(path, "junk");
    CHECK(modelExportRequestEx("100", 1) == 1);
    logBegin();
    while (modelExportPump(1000)) {}
    log = logEnd();
    CHECK(strstr(log, "model.json was edited, kept") != NULL);
    snprintf(path, sizeof path, "%s/data/digimon/d100/model/model.json.new", gRoot);
    {
        size_t l2 = 0;
        unsigned char *nw = slurp(path, &l2);
        CHECK(nw != NULL && edited != NULL && l2 == len && memcmp(nw, edited, len) == 0);
        free(nw);
    }
    snprintf(path, sizeof path, "%s/data/digimon/d100/model/model.json", gRoot);
    {
        size_t l2 = 0;
        unsigned char *kept = slurp(path, &l2);
        CHECK(kept != NULL && strstr((char *)kept, "\"edited\"") != NULL);
        free(kept);
    }
    snprintf(path, sizeof path, "%s/data/digimon/d100/model/model.bin", gRoot);
    {
        size_t l2 = 0;
        unsigned char *bin = slurp(path, &l2);
        /* "junk" was not in the manifest, so it counts as the user's too: kept, with a .new */
        CHECK(bin != NULL && l2 == 4);
        free(bin);
    }
    free(edited);
    snprintf(path, sizeof path, "%s/data/digimon/manifest.json", gRoot);
    CHECK(access(path, F_OK) == 0);
    /* the menu's job on a worker thread: same files and bytes as the synchronous job, a second request while it runs is refused,
       and quitting in the middle stops it cleanly */
    snprintf(dir, sizeof dir, "%s/data2", gRoot);
    modelExportSetup(dir, &io);
    CHECK(modelExportStart("100, 101", 1) == 2);
    CHECK(modelExportActive());
    CHECK(modelExportStart("100", 1) == -1 && modelExportRequestEx("100", 1) == -1 && modelExportOne(100) == -1);
    {
        int spins = 0;
        while (modelExportPump(1)) {
            SDL_Delay(1);
            CHECK(++spins < 20000);
        }
    }
    modelExportCounts(&d1, &f1, &f2);
    CHECK(!modelExportActive() && d1 == 2 && f1 == 0 && f2 == 2 && strstr(modelExportStatus(), "Done (2)") != NULL);
    for (i = 0; i < 8; i++) {
        static const char *const names[4] = {"model.gltf", "model.bin", "model.png", "model.json"};
        char a[1000], b2[1000];
        size_t la = 0, lb = 0;
        unsigned char *ba, *bb;
        if (i < 4 && (i % 4 == 1 || i % 4 == 3)) continue; /* fighter 100's .bin and .json were edited by the rule test above */
        snprintf(a, sizeof a, "%s/data/digimon/d%d/model/%s", gRoot, 100 + i / 4, names[i % 4]);
        snprintf(b2, sizeof b2, "%s/data2/digimon/d%d/model/%s", gRoot, 100 + i / 4, names[i % 4]);
        ba = slurp(a, &la);
        bb = slurp(b2, &lb);
        CHECK(ba != NULL && bb != NULL && la == lb && memcmp(ba, bb, la) == 0);
        free(ba);
        free(bb);
    }
    snprintf(path, sizeof path, "%s/data2/digimon/manifest.json", gRoot);
    CHECK(access(path, F_OK) == 0);
    CHECK(modelExportStart("100", 1) == 1); /* a finished job is reaped by the next request */
    modelExportShutdown();
    CHECK(!modelExportActive());
    modelExportShutdown();
    modelExportSetup("", NULL);
    CHECK(modelExportRequest("all") == -1);
    for (i = 0; i < 0; i++) {}
    modelBakedFree(&b);
}

/* modeltest --export <blob folder of <n>.bin> <fighter> <out folder> [--time]: the exporter on a disc's files dumped by the tool */
static int exportCli(int argc, char **argv) {
    ModelExportIo io;
    ModelExportOut o;
    char err[320], path[1100];
    double t0 = nowMs();
    snprintf(gBlobDir, sizeof gBlobDir, "%s", argv[2]);
    io.file = dirFile;
    io.ud = NULL;
    if (modelExportBuild(&io, atoi(argv[3]), MODEL_EXPORT_UNIT_SCALE, &o, err, sizeof err) != 0) {
        fprintf(stderr, "not exported: %s\n", err);
        return 1;
    }
    if (argc >= 6) printf("exported in %.1f ms: %d bones, %d triangles, %d animations\n", nowMs() - t0, o.bones, o.tris, o.anims);
    sh("mkdir -p '%s'", argv[4]);
    snprintf(path, sizeof path, "%s/model.gltf", argv[4]); spit(path, o.gltf, o.gltfLen);
    snprintf(path, sizeof path, "%s/model.bin", argv[4]); spit(path, o.bin, o.binLen);
    snprintf(path, sizeof path, "%s/model.png", argv[4]); spit(path, o.png, o.pngLen);
    snprintf(path, sizeof path, "%s/model.json", argv[4]); spit(path, o.json, o.jsonLen);
    modelExportFree(&o);
    return 0;
}

/* ---- "from": a model.json that starts from an original fighter (model-polish P3) ---- */

static int gOrigCalls;

static int origBuild(void *ud, int fighterId, ModelExportOut *out, char *err, size_t errCap) {
    ModelExportIo io;
    (void)ud;
    gOrigCalls++;
    io.file = dirFile;
    io.ud = NULL;
    return modelExportBuild(&io, fighterId, MODEL_EXPORT_UNIT_SCALE, out, err, errCap);
}

static void putFile(const char *dir, const char *name, const void *data, size_t len) {
    char path[1000];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    spit(path, data, len);
}

static void putJson(const char *dir, const char *text) { putFile(dir, "model.json", text, strlen(text)); }

static int bakeWith(const char *dir, const ModelOriginals *orig, const ModelCacheHooks *h, ModelBaked *b, char *err, size_t cap) {
    return modelBakeFrom(dir, "model.json", NULL, NULL, h, orig, b, err, cap);
}

static int sameBlob(const ModelBaked *a, const ModelBaked *b) {
    return a->blob != NULL && b->blob != NULL && a->len == b->len && memcmp(a->blob, b->blob, a->len) == 0;
}

static void testFrom(const char *robot) {
    ModelOriginals orig = {origBuild, NULL};
    ModelCacheHooks h = {memGet, memPut, NULL};
    ModelExportOut o;
    ModelBaked full, only, own, ownFull, yaw, yawFull, mot, other, hit;
    ModelSrcInfo info;
    char root[900], dFull[900], dOnly[900], dOwn[900], dOwnFull[900], dTmp[900], path[1000], err[320];
    unsigned char *png;
    size_t pngLen = 0;

    snprintf(root, sizeof root, "%s/from", gRoot);
    snprintf(dFull, sizeof dFull, "%s/full", root);
    snprintf(dOnly, sizeof dOnly, "%s/only", root);
    snprintf(dOwn, sizeof dOwn, "%s/own", root);
    snprintf(dOwnFull, sizeof dOwnFull, "%s/ownfull", root);
    snprintf(dTmp, sizeof dTmp, "%s/tmp", root);
    sh("mkdir -p '%s'", dFull);
    sh("mkdir -p '%s'", dOnly);
    sh("mkdir -p '%s'", dOwn);
    sh("mkdir -p '%s'", dOwnFull);
    sh("mkdir -p '%s'", dTmp);
    /* the exported folder in full, and a folder with only model.json that names it */
    CHECK(origBuild(NULL, 100, &o, err, sizeof err) == 0);
    putFile(dFull, "model.gltf", o.gltf, o.gltfLen);
    putFile(dFull, "model.bin", o.bin, o.binLen);
    putFile(dFull, "model.png", o.png, o.pngLen);
    putFile(dFull, "model.json", o.json, o.jsonLen);
    modelExportFree(&o);
    putJson(dOnly, "{\"from\": \"dw3:model/100\"}\n");
    CHECK(modelBake(dFull, "model.json", NULL, NULL, NULL, &full, err, sizeof err) == 0);
    gOrigCalls = 0;
    CHECK(bakeWith(dOnly, &orig, NULL, &only, err, sizeof err) == 0 && gOrigCalls == 1);
    CHECK(sameBlob(&full, &only));
    /* the model.png of the mod wins (the robot's PNG is another picture; the glTF still names model.png) */
    snprintf(path, sizeof path, "%s/m.png", robot);
    png = slurp(path, &pngLen);
    CHECK(png != NULL);
    putJson(dOwn, "{\"from\": \"dw3:model/100\"}\n");
    putFile(dOwn, "model.png", png, pngLen);
    {
        char cmd[2000];
        snprintf(cmd, sizeof cmd, "cp '%s'/* '%s'/", dFull, dOwnFull);
        if (system(cmd)) {}
    }
    putFile(dOwnFull, "model.png", png, pngLen);
    free(png);
    CHECK(bakeWith(dOwn, &orig, NULL, &own, err, sizeof err) == 0);
    CHECK(modelBake(dOwnFull, "model.json", NULL, NULL, NULL, &ownFull, err, sizeof err) == 0);
    CHECK(sameBlob(&own, &ownFull) && !sameBlob(&own, &only));
    /* the keys of the mod's model.json win over the original's */
    putJson(dTmp, "{\"from\": \"dw3:model/100\", \"yaw\": 90}\n");
    CHECK(bakeWith(dTmp, &orig, NULL, &yaw, err, sizeof err) == 0 && !sameBlob(&yaw, &only));
    snprintf(path, sizeof path, "%s/yawfull", root);
    {
        char cmd[2000];
        snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", path, path);
        if (system(cmd)) {}
        snprintf(cmd, sizeof cmd, "cp '%s'/* '%s'/ && sed -i 's#\"scale\": 1000,#\"scale\": 1000,\\n  \"yaw\": 90,#' '%s/model.json'", dFull, path, path);
        if (system(cmd)) {}
    }
    CHECK(modelBake(path, "model.json", NULL, NULL, NULL, &yawFull, err, sizeof err) == 0 && sameBlob(&yaw, &yawFull));
    /* info and template/effects: the mod's keys over the original's, the rest from the original (shallow: "info" is replaced whole) */
    putJson(dTmp, "{\"from\": \"dw3:model/100\", \"template\": \"dw3:digimon/4\", \"info\": {\"distance\": -777}}\n");
    CHECK(modelSourceInfoFrom(dTmp, "model.json", &orig, &info, err, sizeof err) == 0);
    CHECK(info.templateId == 4 && info.effectsId == 100 && info.distance == -777 && info.height == INT_MIN);
    putJson(dTmp, "{\"from\": \"dw3:model/100\"}\n");
    CHECK(modelSourceInfoFrom(dTmp, "model.json", &orig, &info, err, sizeof err) == 0);
    CHECK(info.templateId == 100 && info.effectsId == 100 && info.distance == -1500);
    /* "motions" merges per slot: a slot set to what the original has changes nothing, the others stay */
    putJson(dTmp, "{\"from\": \"dw3:model/100\", \"motions\": {\"0\": \"motion_00\"}}\n");
    CHECK(bakeWith(dTmp, &orig, NULL, &mot, err, sizeof err) == 0 && sameBlob(&mot, &only));
    /* bad "from": a reason each; and no reader for the originals */
    putJson(dTmp, "{\"from\": \"dw3:model/999\"}\n");
    CHECK(bakeWith(dTmp, &orig, NULL, &other, err, sizeof err) != 0 && strstr(err, "no fighter") != NULL);
    putJson(dTmp, "{\"from\": \"dw3:digimon/100\"}\n");
    CHECK(bakeWith(dTmp, &orig, NULL, &other, err, sizeof err) != 0 && strstr(err, "dw3:model/") != NULL);
    putJson(dTmp, "{\"from\": \"dw3:model/x\"}\n");
    CHECK(bakeWith(dTmp, &orig, NULL, &other, err, sizeof err) != 0 && strstr(err, "dw3:model/") != NULL);
    putJson(dTmp, "{\"from\": 100}\n");
    CHECK(bakeWith(dTmp, &orig, NULL, &other, err, sizeof err) != 0 && strstr(err, "\"from\"") != NULL);
    CHECK(bakeWith(dOnly, NULL, NULL, &other, err, sizeof err) != 0 && strstr(err, "disc") != NULL);
    CHECK(modelBake(dOnly, "model.json", NULL, NULL, NULL, &other, err, sizeof err) != 0 && strstr(err, "disc") != NULL);
    /* the cache: the key follows the original's id and the mod's files, and a hit does not build the export */
    memset(&gMem, 0, sizeof gMem);
    gOrigCalls = 0;
    CHECK(bakeWith(dOnly, &orig, &h, &only, err, sizeof err) == 0 && !only.cached && gMem.puts == 1 && gOrigCalls == 1);
    CHECK(bakeWith(dOnly, &orig, &h, &hit, err, sizeof err) == 0 && hit.cached && gOrigCalls == 1 && hit.key == only.key && sameBlob(&hit, &full));
    modelBakedFree(&hit);
    putJson(dTmp, "{\"from\": \"dw3:model/101\"}\n");
    CHECK(bakeWith(dTmp, &orig, &h, &other, err, sizeof err) == 0 && other.key != only.key);
    modelBakedFree(&other);
    CHECK(bakeWith(dOwn, &orig, &h, &other, err, sizeof err) == 0 && other.key != only.key && !other.cached);
    modelBakedFree(&other);
    gOrigCalls = 0;
    CHECK(bakeWith(dOwn, &orig, &h, &other, err, sizeof err) == 0 && other.cached && gOrigCalls == 0);
    modelBakedFree(&other);
    /* the registry: "rt:kot" is a folder with only model.json; rejected with a reason without originals, baked with them */
    {
        char user[900], mods[900], dir[900], *log;
        int f;
        snprintf(user, sizeof user, "%s/ruser", root);
        snprintf(mods, sizeof mods, "%s/rmods", root);
        sh("mkdir -p '%s/cache'", user);
        setenv("DW3_USER_DIR", user, 1);
        unsetenv("DW3_CACHE_DIR");
        portPathsReset();
        snprintf(dir, sizeof dir, "%s/rt", mods);
        sh("mkdir -p '%s/digimon/kot/model'", dir);
        snprintf(path, sizeof path, "%s/mod.json", dir);
        spits(path, "{\"format\":\"mod-v1\",\"name\":\"rt\",\"version\":\"1.0\",\"kind\":\"content\"}");
        snprintf(dir, sizeof dir, "%s/rt/digimon/kot/model", mods);
        putJson(dir, "{\"from\": \"dw3:model/100\", \"template\": \"dw3:digimon/4\"}\n");
        modsInit(mods, NULL);
        pfsInit(NULL, 100);
        pfsClearVirtualFiles();
        modelsReset();
        modelsSetOriginals(NULL);
        logBegin();
        f = modelsFile("rt:kot");
        log = logEnd();
        CHECK(f == -1 && strstr(log, "[models] rt:kot rejected: \"from\": the original models need the disc") != NULL);
        modelsSetOriginals(&orig);
        modelsReset();
        gOrigCalls = 0;
        CHECK(modelsInfo("rt:kot", &info) && info.templateId == 4 && info.effectsId == 100 && info.distance == -1500);
        f = modelsFile("rt:kot");
        CHECK(f >= 100 && gOrigCalls == 2); /* the info and the first bake each build it once; a later start finds the blob in the cache */
        pfsClearVirtualFiles();
        modelsReset();
        gOrigCalls = 0;
        logBegin();
        f = modelsFile("rt:kot");
        log = logEnd();
        CHECK(f >= 100 && gOrigCalls == 0 && strstr(log, "from the cache") != NULL);
        pfsClearVirtualFiles();
        modelsReset();
        modelsSetOriginals(NULL);
        unsetenv("DW3_USER_DIR");
        portPathsReset();
    }
    modelBakedFree(&full);
    modelBakedFree(&only);
    modelBakedFree(&own);
    modelBakedFree(&ownFull);
    modelBakedFree(&yaw);
    modelBakedFree(&yawFull);
    modelBakedFree(&mot);
}

int main(int argc, char **argv) {
    if (argc >= 5 && strcmp(argv[1], "--export") == 0) return exportCli(argc, argv);
    if (argc >= 4 && strcmp(argv[1], "--bake") == 0) {
        ModelBaked b;
        char err[320];
        double t0 = nowMs();
        int rc = modelBake(argv[2], "model.json", NULL, NULL, NULL, &b, err, sizeof err);
        if (rc != 0) {
            fprintf(stderr, "rejected: %s\n", err);
            return 1;
        }
        if (argc >= 5) printf("baked %lu bytes in %.1f ms: %d bones, %d polygons, %d palettes, rms %.1f\n", (unsigned long)b.len, nowMs() - t0, b.bones, b.polys, b.palettes, b.rms);
        spit(argv[3], b.blob, b.len);
        modelBakedFree(&b);
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: modeltest <fixtures> [<parity>]\n");
        return 2;
    }
    snprintf(gRoot, sizeof gRoot, "/tmp/modeltest-%d", (int)getpid());
    sh("rm -rf '%s'", gRoot);
    sh("mkdir -p '%s'", gRoot);
    CHECK(cases(argv[1]) > 0);
    if (argc >= 3) CHECK(cases(argv[2]) > 0);
    {
        char robot[900];
        snprintf(robot, sizeof robot, "%s/robot", argv[1]);
        testCache(robot);
        testErrors(robot);
        testFuzz(robot, getenv("MODELTEST_FUZZ") != NULL ? atoi(getenv("MODELTEST_FUZZ")) : 400);
        testRegistry(robot);
        testExport(robot, argc >= 4 ? argv[3] : NULL);
        testFrom(robot);
    }
    sh("rm -rf '%s'", gRoot);
    if (failures) {
        printf("modeltest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("modeltest: OK\n");
    return 0;
}
