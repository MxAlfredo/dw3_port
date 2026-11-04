/*
 * Host test of the port's common file layer (pc/shim/port_fs.c, feature port-filesystem T1): the number to
 * path map from a synthetic ISO9660 directory, the override order across two mods, offset reads, a size
 * change, and the precedence of the text and image layers (stubs) over a raw file. Scratch under $TMPDIR;
 * no disc, no window, no game data.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/mods.h"
#include "shim/port_fs.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "fstest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- a synthetic disc: 2048-byte sectors, the layout of a tiny ISO9660 ---- */

#define SECTORS 128
static uint8_t gDisc[SECTORS][2048];

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* a directory record at *at of sector `sec`; returns its length */
static size_t record(uint8_t *at, uint32_t extent, uint32_t size, int isDir, const char *name)
{
    size_t nl = strlen(name), len = 33 + nl;
    if (len & 1) len++;
    memset(at, 0, len);
    at[0] = (uint8_t)len;
    put32(at + 2, extent);
    put32(at + 10, size);
    at[25] = isDir ? 2 : 0;
    at[32] = (uint8_t)nl;
    memcpy(at + 33, name, nl);
    return len;
}

static size_t ptEntry(uint8_t *at, uint32_t extent, unsigned parent, const char *name)
{
    size_t nl = strlen(name);
    memset(at, 0, 8 + nl + 1);
    at[0] = (uint8_t)nl;
    put32(at + 2, extent);
    at[6] = (uint8_t)parent;
    memcpy(at + 8, name, nl);
    return 8 + nl + (nl & 1);
}

/*
 *  18 root (empty)   19 AAA: TOP.BIN lba 30    20 DAT: STAGE dir    21 STAGE: S1 lba 40, S2 lba 50,
 *  S3 and S4 both lba 60 (ambiguous: no name)
 */
static void buildDisc(void)
{
    uint8_t *pt = gDisc[17], *d;
    size_t at = 0, o;

    memset(gDisc, 0, sizeof gDisc);
    gDisc[16][0] = 1;
    memcpy(gDisc[16] + 1, "CD001", 5);
    at += ptEntry(pt + at, 18, 1, "\x01");
    pt[8] = 0;
    at += ptEntry(pt + at, 19, 1, "AAA");
    at += ptEntry(pt + at, 20, 2, "DAT");
    at += ptEntry(pt + at, 21, 3, "STAGE");
    put32(gDisc[16] + 132, (uint32_t)at);
    put32(gDisc[16] + 140, 17);
    put32(gDisc[18] + 10, 2048);
    d = gDisc[19];
    o = record(d, 19, 2048, 1, "\x01");
    o += record(d + o, 30, 100, 0, "TOP.BIN;1");
    o += record(d + o, 20, 2048, 1, "DAT");
    put32(gDisc[19] + 10, 2048);
    d = gDisc[20];
    o = record(d, 20, 2048, 1, "\x01");
    record(d + o, 21, 2048, 1, "STAGE");
    put32(gDisc[20] + 10, 2048);
    d = gDisc[21];
    o = record(d, 21, 2048, 1, "\x01");
    o += record(d + o, 40, 4096, 0, "S1.BIN;1");
    o += record(d + o, 50, 2048, 0, "S2.BIN;1");
    o += record(d + o, 60, 2048, 0, "S3.BIN;1");
    o += record(d + o, 60, 2048, 0, "S4.BIN;1");
    put32(gDisc[21] + 10, 2048);
}

static int dSector(void *ctx, long lba, uint8_t *out)
{
    (void)ctx;
    if (lba < 0 || lba >= SECTORS) return 0;
    memcpy(out, gDisc[lba], 2048);
    return 1;
}

/* file numbers: 0 unused, 1 S1, 2 S2, 3 ambiguous, 4 TOP, 5 points at nothing */
static int dLocate(void *ctx, int index, long *lba, int *sectors)
{
    static const long L[6] = {0, 40, 50, 60, 30, 99};
    static const int N[6] = {0, 2, 1, 1, 1, 1};
    (void)ctx;
    if (index < 0 || index > 5) return 0;
    *lba = L[index];
    *sectors = N[index];
    return 1;
}

/* ---- scratch helpers ---- */

static char gRoot[1100], gLogPath[1200];
static int gSavedErr = -1;

static void put(const char *rel, const void *data, size_t n)
{
    char path[1300], dir[1300], *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        tdMakeDirs(dir);
    }
    f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, n, f);
        fclose(f);
    }
}

static void remove_(const char *rel)
{
    char path[1300];
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    unlink(path);
}

static void logBegin(void)
{
    fflush(stderr);
    gSavedErr = dup(2);
    int fd = open(gLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

static char *logEnd(void)
{
    uint8_t *d;
    size_t n;
    fflush(stderr);
    dup2(gSavedErr, 2);
    close(gSavedErr);
    if (tdReadFile(gLogPath, &d, &n)) return strdup("");
    return (char *)d;
}

static int count(const char *hay, const char *needle)
{
    int n = 0;
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

static void mod(const char *name)
{
    char rel[200];
    const char *json = "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}";
    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    put(rel, json, strlen(json));
}

static void fill(const char *rel, int c, size_t n)
{
    uint8_t *b = (uint8_t *)malloc(n);
    memset(b, c, n);
    put(rel, b, n);
    free(b);
}

static const TdDisc kDisc = {NULL, dSector, dLocate};

static void start(const char *order)
{
    char mods[1300];
    snprintf(mods, sizeof mods, "%s/mods", gRoot);
    modsInit(mods, order);
    pfsInit(&kDisc, 6);
}

static void wipe(void)
{
    char cmd[1300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'/mods", gRoot);
    if (system(cmd)) {}
}

/* ---- the layer stubs ---- */

static const uint8_t kText[] = "TEXT-LAYER-BYTES";
static int gTextFile = -1, gImageFile = -1;

static const uint8_t *stubText(int file, size_t *len, int refresh)
{
    (void)refresh;
    if (file != gTextFile) return NULL;
    if (len) *len = sizeof kText - 1;
    return kText;
}

static int stubImage(int file) { return file == gImageFile; }

/* ---- the tests ---- */

static void testMap(void)
{
    wipe();
    pfsSetLayers(NULL, NULL);
    start(NULL);
    CHECK(pfsName(1) && !strcmp(pfsName(1), "DAT/STAGE/S1.BIN"));
    CHECK(pfsName(2) && !strcmp(pfsName(2), "DAT/STAGE/S2.BIN"));
    CHECK(pfsName(4) && !strcmp(pfsName(4), "TOP.BIN"));
    CHECK(pfsName(0) == NULL);  /* unused */
    CHECK(pfsName(3) == NULL);  /* two files start there */
    CHECK(pfsName(5) == NULL);  /* nothing starts there */
    CHECK(pfsName(99) == NULL && pfsName(-1) == NULL);
    CHECK(pfsFileCount() == 3);
}

/* The shared directory (T5): lookup by path, listing by prefix, number by path, a stable identity */
static void testDirectory(void)
{
    const PfsFile *v[8], *f;
    uint64_t h0, h1;
    uint8_t save;

    CHECK(pfsDiscOpen(&kDisc) == 0 && pfsDiscIsOpen());
    f = pfsFindPath("AAA/DAT/STAGE/S1.BIN");
    CHECK(f && f->lba == 40 && f->size == 4096);
    f = pfsFindPath("AAA/TOP.BIN");
    CHECK(f && f->lba == 30 && f->size == 100);
    CHECK(pfsFindPath("AAA/DAT/STAGE/NOPE.BIN") == NULL && pfsFindPath("DAT/STAGE/S1.BIN") == NULL);
    CHECK(pfsList("AAA/DAT/STAGE/", NULL, 0) == 4);
    CHECK(pfsList("AAA/DAT/STAGE/", v, 8) == 4 && !strcmp(v[0]->path, "AAA/DAT/STAGE/S1.BIN") && !strcmp(v[3]->path, "AAA/DAT/STAGE/S4.BIN"));
    CHECK(pfsList("AAA/DAT/STAGE/", v, 2) == 4 && v[1]->lba == 50); /* the count is the full count, only max are written */
    CHECK(pfsList("AAA/", NULL, 0) == 5 && pfsList(NULL, NULL, 0) == 5 && pfsList("ZZZ/", NULL, 0) == 0);
    pfsInit(&kDisc, 6); /* reuses the open walk */
    CHECK(pfsNumberOf("DAT/STAGE/S2.BIN") == 2 && pfsNumberOf("AAA/TOP.BIN") == 4 && pfsNumberOf("AAA/DAT/STAGE/S3.BIN") == -1);
    h0 = pfsDiscHash(0);
    h1 = pfsDiscHash(1);
    CHECK(h0 == pfsDiscHash(0) && h1 == pfsDiscHash(1) && h0 != h1 && h0 != TD_HASH_INIT);
    CHECK(pfsDiscOpen(&kDisc) == 0 && pfsDiscHash(0) == h0 && pfsDiscHash(1) == h1); /* the same disc, the same identity */
    pfsDiscClose();
    CHECK(!pfsDiscIsOpen() && pfsFindPath("AAA/TOP.BIN") == NULL && pfsList(NULL, NULL, 0) == 0);
    save = gDisc[17][20]; /* a directory name changes: the path table identity moves, the descriptor's does not */
    gDisc[17][20] = 'Q';
    CHECK(pfsDiscOpen(&kDisc) == 0 && pfsDiscHash(0) == h0 && pfsDiscHash(1) != h1);
    gDisc[17][20] = save;
    memset(gDisc[16] + 1, 0, 5); /* not an ISO9660 disc */
    CHECK(pfsDiscOpen(&kDisc) != 0 && !pfsDiscIsOpen());
    buildDisc();
}

static void testOrder(void)
{
    uint8_t buf[4096];
    size_t len = 0;
    PfsSource src;
    char *log;

    wipe();
    mod("a");
    mod("b");
    fill("mods/a/files/DAT/STAGE/S1.BIN", 'a', 3000);
    fill("mods/b/files/dat/stage/s1.bin", 'b', 3000); /* mod b is written in lower case (ADR-010), mod a in the disc's upper case */
    fill("mods/b/files/dat/stage/s2.bin", 'B', 100); /* ADR-010: written in lower case, the disc says upper case */
    pfsSetLayers(NULL, NULL);

    logBegin();
    start(NULL); /* alphabetical: the last one, b, wins */
    CHECK(pfsResolve(1, &len, 1, &src) != NULL && src == PFS_SRC_RAW && len == 3000);
    CHECK(pfsResolve(1, &len, 1, &src)[0] == 'b');
    CHECK(pfsResolve(2, &len, 1, &src)[0] == 'B' && len == 100);
    CHECK(pfsResolve(4, &len, 1, &src) == NULL && src == PFS_SRC_DISC);
    log = logEnd();
    CHECK(count(log, "[files] override DAT/STAGE/S1.BIN from mods/b\n") == 1); /* once, not per read */
    CHECK(count(log, "[files] override DAT/STAGE/S2.BIN from mods/b\n") == 1);
    free(log);

    start("b, a"); /* the order from settings.ini: a wins S1, b still owns S2 */
    CHECK(pfsResolve(1, &len, 1, &src)[0] == 'a');
    CHECK(pfsResolve(2, &len, 1, &src)[0] == 'B');

    /* offset reads: whole sectors from the override, zero padded after its last byte */
    start(NULL);
    memset(buf, 0x55, sizeof buf);
    CHECK(pfsReadSectors(1, 1, 1, buf, &src) == 1 && src == PFS_SRC_RAW);
    CHECK(buf[0] == 'b' && buf[951] == 'b' && buf[952] == 0 && buf[2047] == 0); /* 3000 - 2048 = 952 bytes left */
    memset(buf, 0x55, sizeof buf);
    CHECK(pfsReadSectors(1, 0, 2, buf, NULL) == 1);
    CHECK(buf[0] == 'b' && buf[2999] == 'b' && buf[3000] == 0 && buf[4095] == 0);
    memset(buf, 0x55, sizeof buf);
    CHECK(pfsReadSectors(1, 3, 1, buf, NULL) == 1 && buf[0] == 0 && buf[2047] == 0); /* past the end: zeros */
    CHECK(pfsReadSectors(4, 0, 1, buf, &src) == 0 && src == PFS_SRC_DISC);          /* no override: the disc */
}

static void testChange(void)
{
    size_t len = 0;
    PfsSource src;

    wipe();
    mod("a");
    fill("mods/a/files/DAT/STAGE/S1.BIN", 'x', 3000);
    pfsSetLayers(NULL, NULL);
    start(NULL);
    modsSetHotReload(1); /* DW3_MODS_HOTRELOAD=1: the live look of a modder */
    CHECK(pfsResolve(1, &len, 1, &src) != NULL && len == 3000);
    fill("mods/a/files/DAT/STAGE/S1.BIN", 'y', 5000); /* the modder saves a bigger file */
    CHECK(pfsResolve(1, &len, 1, &src) != NULL && len == 3000); /* looked at less than a second ago */
    sleep(1);
    CHECK(pfsResolve(1, &len, 0, &src) != NULL && len == 3000); /* a read (refresh 0) never re-looks */
    CHECK(pfsResolve(1, &len, 1, &src) != NULL && len == 5000 && pfsResolve(1, &len, 1, &src)[0] == 'y');
    remove_("mods/a/files/DAT/STAGE/S1.BIN"); /* removed: back to the disc */
    sleep(1);
    CHECK(pfsResolve(1, &len, 1, &src) == NULL && src == PFS_SRC_DISC);
    fill("mods/a/files/DAT/STAGE/S1.BIN", 0, 0); /* empty files are refused, not served */
    sleep(1);
    {
        char *log;
        logBegin();
        CHECK(pfsResolve(1, &len, 1, &src) == NULL);
        log = logEnd();
        CHECK(strstr(log, "rejected: empty file") != NULL);
        free(log);
    }
    /* the default (hot reload off): the mods are read once, a file that changes is not looked at while the game runs */
    modsSetHotReload(0);
    fill("mods/a/files/DAT/STAGE/S1.BIN", 'q', 2500);
    sleep(1);
    CHECK(pfsResolve(1, &len, 1, &src) == NULL && src == PFS_SRC_DISC); /* even asked to refresh, a second later */
    modsSetHotReload(1);
    CHECK(pfsResolve(1, &len, 1, &src) != NULL && len == 2500);
    modsSetHotReload(-1);
}

static void testPrecedence(void)
{
    size_t len = 0;
    PfsSource src;
    char *log;
    const uint8_t *p;

    wipe();
    mod("a");
    fill("mods/a/files/DAT/STAGE/S1.BIN", 'r', 3000);
    fill("mods/a/files/DAT/STAGE/S2.BIN", 'r', 3000);
    pfsSetLayers(stubText, stubImage);
    gTextFile = 2;
    gImageFile = 1;

    logBegin();
    start(NULL);
    p = pfsResolve(2, &len, 1, &src); /* the text layer handles file 2: it wins over the raw file */
    CHECK(p == kText && src == PFS_SRC_TEXT && len == sizeof kText - 1);
    CHECK(pfsResolve(2, &len, 1, &src) == kText);
    p = pfsResolve(1, &len, 1, &src); /* the image layer substitutes in file 1: the raw file is ignored */
    CHECK(p == NULL && src == PFS_SRC_DISC);
    log = logEnd();
    CHECK(count(log, "[files] DAT/STAGE/S2.BIN from mods/a ignored: the text layer handles this file\n") == 1);
    CHECK(count(log, "[files] DAT/STAGE/S1.BIN from mods/a ignored: the image layer") == 1);
    free(log);

    gImageFile = -1; /* no image substitution: the raw file serves */
    CHECK(pfsResolve(1, &len, 1, &src) != NULL && src == PFS_SRC_RAW);
    gTextFile = -1;
    CHECK(pfsResolve(2, &len, 1, &src) != NULL && src == PFS_SRC_RAW);
    pfsSetLayers(NULL, NULL);
}

/* the generated layer: a whole file the port builds (longer than the disc's), below the text layer and any raw override */
static void testGenerated(void)
{
    uint8_t big[5000], buf[6144];
    size_t len = 0;
    PfsSource src;
    char *log;
    int i;

    wipe();
    mod("g");
    pfsSetLayers(NULL, NULL);
    for (i = 0; i < (int)sizeof big; i++) big[i] = (uint8_t)(i * 7 + 1);
    start(NULL);
    CHECK(pfsResolve(2, &len, 1, &src) == NULL && src == PFS_SRC_DISC); /* nothing generated yet */
    pfsSetGenerated(2, big, sizeof big); /* file 2 is 100 bytes on the disc; the port serves 5000 */
    big[0] = 0xEE; /* the layer copied them */
    CHECK(pfsResolve(2, &len, 1, &src) != NULL && src == PFS_SRC_GEN && len == 5000);
    CHECK(pfsResolve(2, &len, 1, &src)[0] == 1 && pfsResolve(2, &len, 1, &src)[4999] == (uint8_t)(4999 * 7 + 1));
    CHECK(pfsResolve(3, &len, 1, &src) == NULL && src == PFS_SRC_DISC); /* other files untouched */
    memset(buf, 0x55, sizeof buf);
    CHECK(pfsReadSectors(2, 0, 3, buf, &src) == 1 && src == PFS_SRC_GEN);
    CHECK(buf[0] == 1 && buf[4999] == (uint8_t)(4999 * 7 + 1) && buf[5000] == 0 && buf[6143] == 0); /* zero padded to sectors */
    CHECK(pfsReadSectors(2, 2, 1, buf, NULL) == 1 && buf[0] == big[4096] && buf[903] == big[4999] && buf[904] == 0);

    /* a raw override of the same file wins, with one log line */
    fill("mods/g/files/DAT/STAGE/S2.BIN", 'r', 300);
    start(NULL);
    pfsSetGenerated(2, big, sizeof big);
    logBegin();
    CHECK(pfsResolve(2, &len, 1, &src) != NULL && src == PFS_SRC_RAW && len == 300);
    CHECK(pfsResolve(2, &len, 1, &src)[0] == 'r');
    log = logEnd();
    CHECK(count(log, "[files] DAT/STAGE/S2.BIN from mods/g replaces the file, so the content mods' additions to it are not served\n") == 1);
    free(log);

    /* a text layer wins too; removing the generated file returns the disc */
    pfsSetGenerated(2, NULL, 0);
    wipe();
    mod("g");
    start(NULL);
    CHECK(pfsResolve(2, &len, 1, &src) == NULL && src == PFS_SRC_DISC);
    pfsSetGenerated(2, big, sizeof big);
    CHECK(pfsResolve(2, &len, 1, &src) != NULL && src == PFS_SRC_GEN);
    pfsSetGenerated(2, big, 0); /* an empty replacement removes it */
    CHECK(pfsResolve(2, &len, 1, &src) == NULL && src == PFS_SRC_DISC);
}

/* Virtual files (model-import I3): numbers from the table's end up (pfsInit's fileCount), served from memory, deterministic
   numbering per load, released together, and the generated layer is no longer capped at a few files */
static void testVirtual(void)
{
    uint8_t a[5000], b[3000], buf[6144];
    size_t len = 0;
    PfsSource src;
    int i, n0, n1, last = 0, count = 0;

    wipe();
    mod("v");
    fill("mods/v/files/DAT/STAGE/S1.BIN", 'r', 300); /* a mod is active: the raw layer is on for the real numbers */
    pfsSetLayers(NULL, NULL);
    for (i = 0; i < (int)sizeof a; i++) a[i] = (uint8_t)(i * 5 + 3);
    memset(b, 0x42, sizeof b);
    start(NULL);
    CHECK(pfsVirtualBase() == 6 && pfsVirtualCount() == 0 && !pfsIsVirtual(6));
    n0 = pfsAddVirtualFile(a, sizeof a);
    n1 = pfsAddVirtualFile(b, sizeof b);
    CHECK(n0 == 6 && n1 == 7 && pfsVirtualCount() == 2);
    CHECK(pfsIsVirtual(6) && pfsIsVirtual(7) && !pfsIsVirtual(8) && !pfsIsVirtual(5) && !pfsIsVirtual(-1));
    a[0] = 0xEE; /* copied */
    CHECK(pfsResolve(n0, &len, 1, &src) != NULL && src == PFS_SRC_GEN && len == 5000 && pfsResolve(n0, &len, 0, NULL)[0] == 3);
    CHECK(pfsResolve(n1, &len, 0, &src) != NULL && src == PFS_SRC_GEN && len == 3000);
    memset(buf, 0x55, sizeof buf);
    CHECK(pfsReadSectors(n0, 2, 1, buf, &src) == 1 && src == PFS_SRC_GEN && buf[0] == (uint8_t)(4096 * 5 + 3) && buf[903] == (uint8_t)(4999 * 5 + 3) && buf[904] == 0);
    CHECK(pfsReadSectors(n1, 0, 2, buf, NULL) == 1 && buf[2999] == 0x42 && buf[3000] == 0 && buf[4095] == 0);
    CHECK(pfsResolve(1, &len, 1, &src) != NULL && src == PFS_SRC_RAW && len == 300); /* real numbers untouched */
    CHECK(pfsResolve(8, &len, 1, &src) == NULL && src == PFS_SRC_DISC);              /* the next number is free */
    CHECK(pfsAddVirtualFile(NULL, 10) == -1 && pfsAddVirtualFile(a, 0) == -1 && pfsVirtualCount() == 2); /* nothing to serve: no number */

    /* a reload: everything released, the numbers start again from the base, so the same load order gives the same numbers */
    pfsClearVirtualFiles();
    CHECK(pfsVirtualCount() == 0 && pfsResolve(n0, &len, 1, &src) == NULL && !pfsIsVirtual(n0));
    CHECK(pfsAddVirtualFile(b, sizeof b) == 6 && pfsResolve(6, &len, 1, NULL) != NULL && len == 3000);
    pfsClearVirtualFiles();

    /* the number is a u16 in (file << 16) | entry: the table ends at 65535 */
    for (;;) {
        int n = pfsAddVirtualFile("x", 1);
        if (n < 0) break;
        last = n;
        count++;
    }
    CHECK(last == 0xFFFF && count == 0x10000 - 6 && pfsVirtualCount() == count);
    CHECK(pfsResolve(0xFFFF, &len, 0, NULL) != NULL && len == 1);
    pfsClearVirtualFiles();

    /* the generated layer holds more than 8 files (it was a fixed table) */
    for (i = 0; i < 40; i++) pfsSetGenerated(100 + i, b, 100 + i);
    for (i = 0; i < 40; i++) CHECK(pfsResolve(100 + i, &len, 0, &src) != NULL && src == PFS_SRC_GEN && len == (size_t)(100 + i));
    for (i = 0; i < 40; i++) pfsSetGenerated(100 + i, NULL, 0);
    CHECK(pfsResolve(100, &len, 0, NULL) == NULL && pfsResolve(139, &len, 0, NULL) == NULL);
    wipe();
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    char tpl[1100];
    snprintf(tpl, sizeof tpl, "%s/fstest-XXXXXX", (tmp && tmp[0]) ? tmp : "/tmp");
    if (!mkdtemp(tpl)) {
        fprintf(stderr, "fstest: cannot make a scratch folder in %s\n", tpl);
        return 2;
    }
    snprintf(gRoot, sizeof gRoot, "%s", tpl);
    snprintf(gLogPath, sizeof gLogPath, "%s/.log", gRoot);
    buildDisc();
    testMap();
    testDirectory();
    testOrder();
    testChange();
    testPrecedence();
    testGenerated();
    testVirtual();
    pfsShutdown();
    {
        char cmd[1200];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
        if (system(cmd)) {}
    }
    if (failures) {
        printf("fstest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("fstest: OK\n");
    return 0;
}
