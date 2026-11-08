/*
 * Host test of the port's table patch engine (pc/shim/table_mods.c, ADR-007). Runs in a scratch folder under $TMPDIR; no disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed (printed to stderr).
 */
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "engine/game_state.h"
#include "engine/digimon_access.h"
#include "shim/content.h"
#include "shim/ext_state.h"
#include "shim/game_tables.h"
#include "shim/mods.h"
#include "shim/port_fs.h"
#include "shim/table_mods.h"
#include "shim/port_version.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "tabletest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gRoot[1024];
static char gLogPath[1100];
static int gSavedErr = -1;

static void put(const char *rel, const char *text)
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
        fputs(text, f);
        fclose(f);
    }
}

static char *slurp(const char *rel)
{
    char path[1300];
    uint8_t *d;
    size_t n;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    if (tdReadFile(path, &d, &n)) return NULL;
    return (char *)d;
}

static void wipe(void)
{
    char cmd[1300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'/*", gRoot);
    if (system(cmd)) {}
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


/* A synthetic table: 5 rows, keys 10..14, three fields of different types and ranges */
typedef struct Row {
    int a;             /* s32 0..1000 */
    unsigned short b;  /* u16 0..500 */
    signed char c;     /* s8 -10..10 */
} Row;

static Row gRows[5];
static const TmField kFields[] = {
    {"a", TM_S32, offsetof(Row, a), 0, 1000, "first"},
    {"b", TM_U16, offsetof(Row, b), 0, 500, "second"},
    {"c", TM_S8, offsetof(Row, c), -10, 10, "third (\"x\")"},
};
static const TmTable kTable = {"synth", gRows, sizeof(Row), 5, 10, "the \"key\"", "the moment", kFields, 3};

static void reset(void)
{
    int i;
    for (i = 0; i < 5; i++) {
        gRows[i].a = 100 + i;
        gRows[i].b = (unsigned short)(200 + i);
        gRows[i].c = (signed char)(i - 2);
    }
}

static char *start(const char *order, int withData)
{
    char mods[1300], data[1300];
    snprintf(mods, sizeof mods, "%s/mods", gRoot);
    snprintf(data, sizeof data, "%s/data", gRoot);
    reset();
    logBegin();
    modsInit(mods, order);
    tmInit(withData ? data : NULL);
    CHECK(tmRegister(&kTable) == 0);
    return logEnd();
}

static void mod(const char *name, const char *patch)
{
    char rel[200];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    put(rel, "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    if (patch) {
        snprintf(rel, sizeof rel, "mods/%s/tables/synth.json", name);
        put(rel, patch);
    }
}

#define F "\"format\":\"synth-v1\""

static char *apply(int *changed)
{
    int n;
    logBegin();
    n = tmApply("synth");
    if (changed) *changed = n;
    return logEnd();
}

static void testNoMods(void)
{
    char *log;
    int n;
    wipe();
    mod("a", NULL);
    log = start(NULL, 0);
    free(log);
    CHECK(tmPatchCount("synth") == 0);
    gRows[1].a = 7; /* proves the table is not rewritten when nothing patches it */
    log = apply(&n);
    CHECK(n == 0);
    CHECK(gRows[1].a == 7);
    CHECK(strstr(log, "patched") == NULL);
    free(log);
    CHECK(tmApply("nope") == -1);
}

static void testOpsAndPrecedence(void)
{
    char *log;
    int n;
    wipe();
    mod("a", "{" F ",\"rows\":{\"*\":{\"a\":\"*2\",\"b\":\"+5\"},\"11\":{\"a\":50},\"12\":{\"c\":\"-3\"}}}");
    log = start(NULL, 0);
    free(log);
    CHECK(tmPatchCount("synth") == 1);
    log = apply(&n);
    CHECK(gRows[0].a == 200 && gRows[0].b == 205);    /* wildcard on every row */
    CHECK(gRows[1].a == 50 && gRows[1].b == 206);     /* its own key beats "*" on the field it names only */
    CHECK(gRows[2].a == 204 && gRows[2].c == -3);     /* -3 applied to 0 */
    CHECK(gRows[4].a == 208 && gRows[4].c == 2);      /* untouched field stays */
    CHECK(n == 5);
    CHECK(strstr(log, "[tables] synth patched by mods/a (5 rows)\n") != NULL);
    free(log);
}

static void testClampAndRound(void)
{
    char *log;
    wipe();
    mod("a", "{" F ",\"rows\":{\"10\":{\"a\":\"*1000\",\"b\":\"-9999\",\"c\":\"*0.5\"},\"11\":{\"a\":\"*0.5\"},\"12\":{\"c\":\"+100\"}}}");
    free(start(NULL, 0));
    log = apply(NULL);
    CHECK(gRows[0].a == 1000);   /* clamped to the field max */
    CHECK(gRows[0].b == 0);      /* clamped to the min */
    CHECK(gRows[0].c == -1);     /* -2 * 0.5 = -1 */
    CHECK(gRows[1].a == 51);     /* 101 * 0.5 = 50.5 rounds to 51 */
    CHECK(gRows[2].c == 10);
    free(log);
}

static void testRejections(void)
{
    char *log;
    int n;
    wipe();
    mod("a", "{" F ",\"rows\":{\"10\":{\"a\":5000,\"zzz\":1,\"b\":true,\"c\":\"hello\"},\"99\":{\"a\":1},\"abc\":{\"a\":1},\"11\":5,\"12\":{\"a\":7}}}");
    log = start(NULL, 0);
    CHECK(strstr(log, "row \"10\" field \"a\" rejected: 5000 is outside 0 to 1000") != NULL);
    CHECK(strstr(log, "row \"10\" field \"zzz\" rejected: unknown field") != NULL);
    CHECK(strstr(log, "row \"10\" field \"b\" rejected: not a number or an operation") != NULL);
    CHECK(strstr(log, "row \"10\" field \"c\" rejected: a text value must be an operation") != NULL);
    CHECK(strstr(log, "row \"99\" rejected: no row with that key") != NULL);
    CHECK(strstr(log, "row \"abc\" rejected: no row with that key") != NULL);
    CHECK(strstr(log, "row \"11\" rejected: not an object of fields") != NULL);
    free(log);
    log = apply(&n);
    CHECK(gRows[0].a == 100 && gRows[0].b == 200 && gRows[0].c == -2); /* every field of row 10 was refused */
    CHECK(gRows[2].a == 7);                                           /* the rest of the patch applied */
    CHECK(n == 1);
    free(log);

    /* whole-file rejections: the table stays original and the next mod still applies */
    wipe();
    mod("a", "{" F ",\"rows\":{\"10\":{\"a\":1},}}");          /* trailing comma */
    mod("b", "{\"format\":\"other-v1\",\"rows\":{}}");          /* wrong format */
    mod("c", "{\"rows\":{}}");                                 /* no format */
    mod("d", "{" F ",\"rows\":{\"10\":{\"a\":1},\"10\":{\"a\":2}}}"); /* duplicate row */
    mod("e", "{" F ",\"rows\":{\"10\":{\"a\":1,\"a\":2}}}");   /* duplicate field */
    mod("f", "{" F ",\"rows\":{\"13\":{\"a\":9}}} x");         /* trailing garbage */
    mod("g", "{" F ",\"rows\":{\"14\":{\"a\":9}}}");           /* good */
    log = start(NULL, 0);
    CHECK(tmPatchCount("synth") == 1);
    CHECK(strstr(log, "a/tables/synth.json rejected:") != NULL);
    CHECK(strstr(log, "b/tables/synth.json rejected: unknown format \"other-v1\"") != NULL);
    CHECK(strstr(log, "c/tables/synth.json rejected: no \"format\"") != NULL);
    CHECK(strstr(log, "d/tables/synth.json rejected: a row key is listed twice") != NULL);
    CHECK(strstr(log, "e/tables/synth.json rejected: a field is listed twice") != NULL);
    CHECK(strstr(log, "f/tables/synth.json rejected: text after the object") != NULL);
    free(log);
    free(apply(NULL));
    CHECK(gRows[0].a == 100 && gRows[3].a == 103 && gRows[4].a == 9);
}

static void testModOrder(void)
{
    char *log;
    wipe();
    mod("a", "{" F ",\"rows\":{\"*\":{\"a\":\"*2\"}}}");
    mod("b", "{" F ",\"rows\":{\"*\":{\"a\":\"*2\"},\"10\":{\"b\":77}}}");
    mod("c", "{" F ",\"rows\":{\"10\":{\"b\":88}}}");
    free(start("a, b, c", 0));
    log = apply(NULL);
    CHECK(gRows[0].a == 400 && gRows[1].a == 404);  /* two doublings give x4 */
    CHECK(gRows[0].b == 88);                         /* a later replacement wins */
    CHECK(strstr(log, "patched by mods/a, mods/b, mods/c") != NULL);
    free(log);
    free(start("c, b", 0));
    free(apply(NULL));
    CHECK(gRows[0].a == 200 && gRows[0].b == 77);    /* only the listed mods, in the listed order */
}

static void testIdempotent(void)
{
    int n1, n2;
    wipe();
    mod("a", "{" F ",\"rows\":{\"*\":{\"a\":\"*2\"},\"12\":{\"b\":\"+1\"}}}");
    free(start(NULL, 0));
    free(apply(&n1));
    CHECK(gRows[0].a == 200 && gRows[2].b == 203);
    free(apply(&n2));
    CHECK(gRows[0].a == 200 && gRows[2].b == 203 && n1 == n2); /* a second application never doubles again */
    gRows[3].a = 1; /* the host's own restore (or any drift) is undone too */
    free(apply(NULL));
    CHECK(gRows[3].a == 206);
}

static void testExport(void)
{
    char *ref, *man, *log;
    wipe();
    mod("a", NULL);
    log = start(NULL, 1);
    free(log);
    ref = slurp("data/tables/synth.json");
    CHECK(ref != NULL);
    if (ref) {
        CHECK(strstr(ref, "\"format\": \"synth-v1\"") != NULL);
        CHECK(strstr(ref, "\"10\": { \"a\": 100, \"b\": 200, \"c\": -2 }") != NULL);
        CHECK(strstr(ref, "\"14\": { \"a\": 104, \"b\": 204, \"c\": 2 }") != NULL);
        CHECK(strstr(ref, "Valid: 0 to 1000.") != NULL);
        CHECK(strstr(ref, "\"key\": \"the \\\"key\\\"\"") != NULL); /* notes are escaped JSON strings */
        free(ref);
    }
    man = slurp("data/tables/manifest.json");
    CHECK(man != NULL && strstr(man, "synth.json") != NULL);
    free(man);

    /* the reference is also a valid patch: same values, nothing changes in the game */
    {
        char dst[1300], src[1300];
        uint8_t *d;
        size_t n;
        snprintf(src, sizeof src, "%s/data/tables/synth.json", gRoot);
        CHECK(tdReadFile(src, &d, &n) == 0);
        snprintf(dst, sizeof dst, "%s/mods/a/tables/synth.json", gRoot);
        snprintf(src, sizeof src, "%s/mods/a/tables", gRoot);
        tdMakeDirs(src);
        tdWriteFileAtomic(dst, d, n);
        free(d);
        free(start(NULL, 1));
        CHECK(tmPatchCount("synth") == 1);
        free(apply(NULL));
        CHECK(gRows[0].a == 100 && gRows[4].c == 2);
    }

    /* an edited copy is kept; the new version goes to .new */
    put("data/tables/synth.json", "{\"mine\": true}\n");
    log = start(NULL, 1);
    ref = slurp("data/tables/synth.json");
    CHECK(ref && strstr(ref, "mine") != NULL);
    free(ref);
    ref = slurp("data/tables/synth.json.new");
    CHECK(ref && strstr(ref, "synth-v1") != NULL);
    free(ref);
    CHECK(strstr(log, "was edited, kept") != NULL);
    free(log);
    /* editing the reference changed nothing in the game */
    free(apply(NULL));
    CHECK(gRows[0].a == 100);
}

/* content-core C2: a generated file of another family (data/content/...) under the same manifest rule */
static void testExportFile(void)
{
    char *ref, *man, *log;
    wipe();
    mod("a", NULL);
    free(start(NULL, 1));
    tmExportFile("content", "items.json", "{\"v\": 1}\n");
    ref = slurp("data/content/items.json");
    CHECK(ref != NULL && strstr(ref, "\"v\": 1") != NULL);
    free(ref);
    man = slurp("data/content/manifest.json");
    CHECK(man != NULL && strstr(man, "\"topic\": \"content\"") != NULL && strstr(man, "items.json") != NULL);
    free(man);
    /* a changed version replaces an unedited copy */
    tmExportFile("content", "items.json", "{\"v\": 2}\n");
    ref = slurp("data/content/items.json");
    CHECK(ref != NULL && strstr(ref, "\"v\": 2") != NULL);
    free(ref);
    CHECK(slurp("data/content/items.json.new") == NULL);
    /* an edited one stays; the new version goes beside it */
    put("data/content/items.json", "{\"mine\": true}\n");
    logBegin();
    tmExportFile("content", "items.json", "{\"v\": 3}\n");
    log = logEnd();
    ref = slurp("data/content/items.json");
    CHECK(ref != NULL && strstr(ref, "mine") != NULL);
    free(ref);
    ref = slurp("data/content/items.json.new");
    CHECK(ref != NULL && strstr(ref, "\"v\": 3") != NULL);
    free(ref);
    CHECK(strstr(log, "[content] items.json was edited, kept; the new version is items.json.new") != NULL);
    free(log);
    /* the tables' own folder is untouched, and no data folder means nothing is written */
    free(start(NULL, 0));
    tmExportFile("content", "techs.json", "{}\n");
    CHECK(slurp("data/content/techs.json") == NULL);
}

static void testBadDescriptor(void)
{
    TmTable t = kTable;
    TmField bad[] = {{"a", TM_U8, 0, 0, 1000, "range beyond the type"}};
    wipe();
    mod("a", NULL);
    free(start(NULL, 0));
    t.name = "bad";
    t.fields = bad;
    t.nfields = 1;
    CHECK(tmRegister(&t) == -1);
    CHECK(tmRegister(&kTable) == -1); /* registered twice */
}


/* A file-backed table (ADR-007 section 6, the battle table): records with an s16 id first, in file order,
   a record with id 0 ends the table and the bytes after it belong to the file, not to the table */
typedef struct Rec {
    short id;
    short item;
    short chance;
    short pad;
} Rec;

static const TmField kRecFields[] = {
    {"item", TM_S16, offsetof(Rec, item), 0, 403, "the item"},
    {"chance", TM_S16, offsetof(Rec, chance), 0, 1023, "the chance"},
};
static const TmTable kRecTable = {"synthfile", NULL, sizeof(Rec), 0, 0, "the record id", "when the file is read", kRecFields, 2, 1, offsetof(Rec, id), TM_S16};
static const Rec kDisc[6] = {{5, 10, 100, 7}, {9, 20, 200, 7}, {3, 30, 300, 7}, {12, 0, 0, 7}, {0, 0, 0, 0}, {5, 99, 99, 99}};

static void recMod(const char *name, const char *patch)
{
    char rel[200];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    put(rel, "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    snprintf(rel, sizeof rel, "mods/%s/tables/synthfile.json", name);
    put(rel, patch);
}

static char *startFile(int withData, int *rc)
{
    char mods[1300], data[1300];
    snprintf(mods, sizeof mods, "%s/mods", gRoot);
    snprintf(data, sizeof data, "%s/data", gRoot);
    logBegin();
    modsInit(mods, NULL);
    tmInit(withData ? data : NULL);
    *rc = tmRegisterFile(&kRecTable, kDisc, sizeof kDisc);
    return logEnd();
}

#define FF "\"format\":\"synthfile-v1\""

static void testFileBacked(void)
{
    char *log, *ref;
    int rc, n;
    Rec buf[6], buf2[6], over[6];

    wipe();
    recMod("a", "{" FF ",\"rows\":{\"*\":{\"chance\":\"*2\"},\"9\":{\"item\":77},\"3\":{\"chance\":5},\"44\":{\"item\":1}}}");
    log = startFile(1, &rc);
    CHECK(rc == 0);
    CHECK(strstr(log, "row \"44\" rejected: no row with that key") != NULL);
    CHECK(strstr(log, "row \"5\"") == NULL);
    free(log);
    CHECK(tmPatchCount("synthfile") == 1);
    CHECK(tmApply("synthfile") == -1);          /* a file-backed table is never applied in memory */
    CHECK(tmPatchBuffer("synth", buf, sizeof buf) == -1 && tmPatchBuffer("nope", buf, sizeof buf) == -1);

    /* the reference lists the rows of the file in file order, up to the terminator, with the id as key */
    ref = slurp("data/tables/synthfile.json");
    CHECK(ref != NULL);
    if (ref) {
        CHECK(strstr(ref, "\"format\": \"synthfile-v1\"") != NULL);
        CHECK(strstr(ref, "\"5\": { \"item\": 10, \"chance\": 100 }") != NULL);
        CHECK(strstr(ref, "\"12\": { \"item\": 0, \"chance\": 0 }") != NULL);
        CHECK(strstr(ref, "\"99\"") == NULL && strstr(ref, "\"0\":") == NULL); /* past the terminator: not a row */
        CHECK(strstr(ref, "\"5\": { \"item\": 99") == NULL);
        free(ref);
    }
    CHECK(tmOriginalRow("synthfile", 9) != NULL && ((const Rec *)tmOriginalRow("synthfile", 9))->chance == 200);
    CHECK(tmOriginalRow("synthfile", 44) == NULL && tmOriginalRow("nope", 9) == NULL);

    /* each read starts from the source bytes: two fresh copies patch to the same result */
    memcpy(buf, kDisc, sizeof buf);
    log = NULL;
    logBegin();
    n = tmPatchBuffer("synthfile", buf, sizeof buf);
    log = logEnd();
    CHECK(n == 3);                                            /* ids 5, 9, 3 changed; 12 stayed 0 * 2 */
    CHECK(buf[0].chance == 200 && buf[0].item == 10);         /* "*" on the chance */
    CHECK(buf[1].item == 77 && buf[1].chance == 400);         /* its own key replaces only the field it names */
    CHECK(buf[2].chance == 5);                                /* own key beats "*" on the field it names */
    CHECK(buf[3].chance == 0 && buf[3].pad == 7 && buf[0].pad == 7 && buf[0].id == 5); /* other fields untouched */
    CHECK(memcmp(&buf[4], &kDisc[4], sizeof(Rec)) == 0 && memcmp(&buf[5], &kDisc[5], sizeof(Rec)) == 0); /* after the terminator */
    CHECK(strstr(log, "[tables] synthfile patched by mods/a (3 rows)\n") != NULL);
    free(log);
    memcpy(buf2, kDisc, sizeof buf2);
    CHECK(tmPatchBuffer("synthfile", buf2, sizeof buf2) == 3 && memcmp(buf, buf2, sizeof buf) == 0);
    CHECK(((const Rec *)tmOriginalRow("synthfile", 9))->chance == 200); /* the snapshot never changes */
    {
        char who[64];
        CHECK(tmWho("synthfile", who, sizeof who) == 1 && strcmp(who, "mods/a") == 0);
        CHECK(tmWho("synthfile", who, 5) == 1 && strcmp(who, "mods") == 0); /* cut, never overrun */
    }

    /* a raw override of the same file (a different order and values) comes first; the patch goes on top */
    memset(over, 0, sizeof over);
    over[0] = (Rec){9, 21, 50, 1};
    over[1] = (Rec){8, 22, 60, 1};   /* an id the disc does not have: not in the reference, not patched by key */
    over[2] = (Rec){5, 23, 70, 1};
    n = tmPatchBuffer("synthfile", over, sizeof over);
    CHECK(n == 3);
    CHECK(over[0].id == 9 && over[0].item == 77 && over[0].chance == 100); /* 50 * 2 on the override's value */
    CHECK(over[1].id == 8 && over[1].item == 22 && over[1].chance == 120); /* "*" reaches rows the disc does not have; no key 8 of its own */
    CHECK(over[2].id == 5 && over[2].item == 23 && over[2].chance == 140); /* 70 * 2 */
    /* a buffer too short for a whole record or with no rows is left alone */
    memset(over, 0, sizeof over);
    CHECK(tmPatchBuffer("synthfile", over, sizeof over) == 0 && over[0].id == 0);
    CHECK(tmPatchBuffer("synthfile", over, 3) == 0);
}

static void testFileBackedNoPatch(void)
{
    int rc;
    Rec buf[6];
    wipe();
    mod("a", NULL);
    free(startFile(0, &rc));
    CHECK(rc == 0 && tmPatchCount("synthfile") == 0);
    memcpy(buf, kDisc, sizeof buf);
    CHECK(tmPatchBuffer("synthfile", buf, sizeof buf) == 0 && memcmp(buf, kDisc, sizeof buf) == 0);
    {
        TmTable t = kRecTable;
        t.keyFromRow = 0; /* a file-backed table needs its key in the row */
        free(startFile(0, &rc));
        t.name = "other";
        CHECK(tmRegisterFile(&t, kDisc, sizeof kDisc) == -1);
        t = kRecTable;
        t.name = "other";
        t.keyOff = 7; /* a key that does not fit the row */
        CHECK(tmRegisterFile(&t, kDisc, sizeof kDisc) == -1);
        CHECK(tmRegisterFile(&kRecTable, kDisc, 0) == -1);
    }
}

static void testOriginalRowMemory(void)
{
    char who[64];
    wipe();
    mod("a", "{" F ",\"rows\":{\"11\":{\"a\":1}}}");
    mod("b", "{" F ",\"rows\":{\"12\":{\"a\":2}}}");
    free(start(NULL, 0));
    free(apply(NULL));
    CHECK(gRows[1].a == 1);
    CHECK(((const Row *)tmOriginalRow("synth", 11))->a == 101);  /* the original, not the patched value */
    CHECK(tmOriginalRow("synth", 9) == NULL && tmOriginalRow("synth", 15) == NULL);
    CHECK(tmWho("synth", who, sizeof who) == 2 && strcmp(who, "mods/a, mods/b") == 0);
}


/* The post-read hook of the file layer (port_fs.h): the reader calls pfsPostRead after the bytes are in the
   buffer, whatever their source; here a stub hook patches the table in the buffer the way boot.c does */
static int gHookCalls, gHookFile, gHookOffset, gHookSectors;

static void stubHook(int file, int offset, int sectors, uint8_t *dst, PfsSource src)
{
    gHookCalls++;
    gHookFile = file;
    gHookOffset = offset;
    gHookSectors = sectors;
    if (file == 463 && offset == 0 && src != PFS_SRC_TEXT) tmPatchBuffer("synthfile", dst, (size_t)sectors * 2048);
}

static void testPostReadHook(void)
{
    static uint8_t sector[2048];
    int rc;
    Rec *r = (Rec *)sector;

    wipe();
    recMod("a", "{" FF ",\"rows\":{\"9\":{\"chance\":\"+1\"}}}");
    free(startFile(0, &rc));
    CHECK(rc == 0);
    pfsPostRead(463, 0, 1, sector, PFS_SRC_DISC); /* no hook installed: nothing happens */
    CHECK(gHookCalls == 0);
    pfsSetPostRead(stubHook);
    /* the disc's bytes: patched in the buffer, not in the source */
    memset(sector, 0, sizeof sector);
    memcpy(sector, kDisc, sizeof kDisc);
    pfsPostRead(463, 0, 1, sector, PFS_SRC_DISC);
    CHECK(gHookCalls == 1 && gHookFile == 463 && gHookOffset == 0 && gHookSectors == 1);
    CHECK(r[1].id == 9 && r[1].chance == 201 && kDisc[1].chance == 200);
    /* a raw override's bytes first, the patch on top; reading again from the source gives the same result */
    memset(sector, 0, sizeof sector);
    r[0] = (Rec){9, 5, 40, 0};
    pfsPostRead(463, 0, 1, sector, PFS_SRC_RAW);
    CHECK(r[0].item == 5 && r[0].chance == 41);
    memset(sector, 0, sizeof sector);
    r[0] = (Rec){9, 5, 40, 0};
    pfsPostRead(463, 0, 1, sector, PFS_SRC_RAW);
    CHECK(r[0].chance == 41);
    /* other files and partial reads are the hook's own business; a read of zero sectors is not announced */
    pfsPostRead(1, 0, 1, sector, PFS_SRC_DISC);
    CHECK(gHookFile == 1);
    pfsPostRead(463, 0, 0, sector, PFS_SRC_DISC);
    CHECK(gHookFile == 1);
    pfsSetPostRead(NULL);
    gHookCalls = 0;
    pfsPostRead(463, 0, 1, sector, PFS_SRC_DISC);
    CHECK(gHookCalls == 0);
}

/* ---- item_prices-like table: a record with a pointer, two u16 fields and two bytes (ItemInfo) ---- */

typedef struct Info {
    void *data;
    unsigned short price;
    unsigned short sell;
    unsigned char kind, type;
} Info;

static Info gInfo[4];
static int gInfoTarget[4];
static const TmField kInfoFields[] = {
    {"price", TM_U16, offsetof(Info, price), 0, 65535, "buy"},
    {"sellPrice", TM_U16, offsetof(Info, sell), 0, 65535, "sell"},
};
static const TmTable kInfoTable = {"prices", gInfo, sizeof(Info), 4, 1, "the id", "when", kInfoFields, 2};

static void testPriceTable(void)
{
    int i, n1, n2;

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    put("mods/a/tables/prices.json",
        "{\"format\":\"prices-v1\",\"rows\":{\"*\":{\"price\":\"*0.5\"},\"3\":{\"sellPrice\":7}}}");
    for (i = 0; i < 4; i++) {
        gInfo[i].data = &gInfoTarget[i];
        gInfo[i].price = (unsigned short)(100 + 10 * i);
        gInfo[i].sell = (unsigned short)(50 + i);
        gInfo[i].kind = (unsigned char)(i + 1);
        gInfo[i].type = (unsigned char)(i + 2);
    }
    logBegin();
    {
        char m[1300];
        snprintf(m, sizeof m, "%s/mods", gRoot);
        modsInit(m, NULL);
    }
    tmInit(NULL);
    CHECK(tmRegister(&kInfoTable) == 0);
    free(logEnd());
    CHECK(tmPatchCount("prices") == 1);
    n1 = tmApply("prices");
    CHECK(n1 == 4);
    CHECK(gInfo[0].price == 50 && gInfo[1].price == 55 && gInfo[3].price == 65); /* 100,110,130 halved, rounded */
    CHECK(gInfo[2].sell == 7 && gInfo[2].price == 60); /* id 3 is row 2: its own sell price, the halved buy price */
    CHECK(gInfo[1].sell == 51);
    for (i = 0; i < 4; i++) {
        CHECK(gInfo[i].data == &gInfoTarget[i]); /* the record pointer and the bytes are never touched */
        CHECK(gInfo[i].kind == i + 1 && gInfo[i].type == i + 2);
    }
    n2 = tmApply("prices");
    CHECK(n2 == n1 && gInfo[1].price == 55 && gInfo[0].price == 50); /* a second application never halves again */
    CHECK(((const Info *)tmOriginalRow("prices", 1))->price == 100);
    CHECK(tmOriginalRow("prices", 0) == NULL && tmOriginalRow("prices", 5) == NULL);
}

/* ---- list fields: rows of { count, pointer to ids ended by 0 } (the shops) ---- */

typedef struct Shop {
    int count;
    short *items;
} Shop;

static short gStockA[] = {11, 12, 13, 0};
static short gStockB[] = {21, 0};
static short gStockC[] = {31, 32, 0};
static Shop gShops[3];
static const TmField kShopFields[] = {
    {"items", TM_S16, offsetof(Shop, items), 1, 400, "ids", 6, offsetof(Shop, count)},
};
static const TmTable kShopTable = {"shops", gShops, sizeof(Shop), 3, 0, "the shop", "when", kShopFields, 1};

static void shopReset(void)
{
    gShops[0].count = 3;
    gShops[0].items = gStockA;
    gShops[1].count = 1;
    gShops[1].items = gStockB;
    gShops[2].count = 2;
    gShops[2].items = gStockC;
}

static char *shopStart(int withData, const char *a, const char *b)
{
    char m[1300], d[1300];

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    put("mods/b/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    if (a) put("mods/a/tables/shops.json", a);
    if (b) put("mods/b/tables/shops.json", b);
    snprintf(m, sizeof m, "%s/mods", gRoot);
    snprintf(d, sizeof d, "%s/data", gRoot);
    shopReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(withData ? d : NULL);
    CHECK(tmRegister(&kShopTable) == 0);
    return logEnd();
}

static char *shopApply(int *n)
{
    int r;

    logBegin();
    r = tmApply("shops");
    if (n) *n = r;
    return logEnd();
}

#define SF "\"format\":\"shops-v1\""

static int shopIs(int row, int count, const short *want)
{
    int i;

    if (gShops[row].count != count) return 0;
    for (i = 0; i < count; i++) {
        if (gShops[row].items[i] != want[i]) return 0;
    }
    return gShops[row].items[count] == 0; /* the terminator the game's lists end with */
}

static void testListFields(void)
{
    static const short r3[] = {5, 6, 7};
    static const short r6[] = {1, 2, 3, 4, 5, 6};
    static const short add1[] = {11, 12, 13, 99};
    char *log;
    int n;

    /* replace, with the terminator; the original arrays stay as they were */
    free(shopStart(0, "{" SF ",\"rows\":{\"1\":{\"items\":[5,6,7]}}}", NULL));
    log = shopApply(&n);
    CHECK(n == 1);
    CHECK(shopIs(1, 3, r3));
    CHECK(gStockB[0] == 21 && gStockB[1] == 0);
    CHECK(shopIs(0, 3, (const short[]){11, 12, 13}));
    CHECK(strstr(log, "[tables] shops patched by mods/a (1 rows)\n") != NULL);
    free(log);
    free(shopApply(&n)); /* idempotent */
    CHECK(n == 1 && shopIs(1, 3, r3));
    gShops[1].count = 9; /* the host's restore (or drift) is undone, the pointer too */
    gShops[1].items = gStockC;
    free(shopApply(NULL));
    CHECK(shopIs(1, 3, r3));
    CHECK(((const Shop *)tmOriginalRow("shops", 1))->items == gStockB);

    /* the exact capacity is allowed; one more is rejected, the shop stays as it was */
    log = shopStart(0, "{" SF ",\"rows\":{\"1\":{\"items\":[1,2,3,4,5,6]},\"2\":{\"items\":[1,2,3,4,5,6,7]}}}", NULL);
    CHECK(strstr(log, "row \"2\" field \"items\" rejected: more than the list can hold (more than 6 items)") != NULL);
    free(log);
    free(shopApply(&n));
    CHECK(shopIs(1, 6, r6));
    CHECK(gShops[2].items == gStockC && gShops[2].count == 2);

    /* 0 is the terminator, so it is not an id; out of range, fractions and text are rejected whole */
    log = shopStart(0, "{" SF ",\"rows\":{\"0\":{\"items\":[1,0,3]},\"1\":{\"items\":[1,401]},\"2\":{\"items\":[1.5]}}}", NULL);
    CHECK(strstr(log, "row \"0\" field \"items\" rejected") != NULL);
    CHECK(strstr(log, "row \"1\" field \"items\" rejected") != NULL);
    CHECK(strstr(log, "row \"2\" field \"items\" rejected") != NULL);
    free(log);
    free(shopApply(&n));
    CHECK(n == 0 && gShops[0].items == gStockA && gShops[1].items == gStockB && gShops[2].items == gStockC);
    log = shopStart(0, "{" SF ",\"rows\":{\"0\":{\"items\":7},\"1\":{\"items\":\"*2\"},\"2\":{\"items\":[\"x\"]}}}", NULL);
    CHECK(strstr(log, "row \"0\" field \"items\" rejected") != NULL);
    CHECK(strstr(log, "row \"1\" field \"items\" rejected") != NULL);
    CHECK(strstr(log, "row \"2\" field \"items\" rejected") != NULL);
    free(log);
    free(shopApply(&n));
    CHECK(n == 0);

    /* add and remove: composable, an id already there is not repeated, remove goes first */
    free(shopStart(0, "{" SF ",\"rows\":{\"0\":{\"items\":{\"add\":[12,99]}},\"2\":{\"items\":{\"remove\":[31],\"add\":[31,40]}}}}",
                   "{" SF ",\"rows\":{\"0\":{\"items\":{\"remove\":[11]}}}}"));
    log = shopApply(&n);
    CHECK(shopIs(0, 3, (const short[]){12, 13, 99}));
    CHECK(shopIs(2, 3, (const short[]){32, 31, 40}));
    CHECK(n == 2);
    CHECK(strstr(log, "patched by mods/a, mods/b") != NULL);
    free(log);
    (void)add1;

    /* "*" is every row; a row that names the field wins; a later mod's replace wins over the earlier */
    free(shopStart(0, "{" SF ",\"rows\":{\"*\":{\"items\":{\"add\":[9]}},\"1\":{\"items\":[2]}}}", "{" SF ",\"rows\":{\"2\":{\"items\":[3,4]}}}"));
    free(shopApply(&n));
    CHECK(shopIs(0, 4, (const short[]){11, 12, 13, 9}));
    CHECK(shopIs(1, 1, (const short[]){2}));
    CHECK(shopIs(2, 2, (const short[]){3, 4}));

    /* an add that overflows is rejected whole at application; a remove that empties the list too */
    free(shopStart(0, "{" SF ",\"rows\":{\"0\":{\"items\":{\"add\":[1,2,3,4]}},\"1\":{\"items\":{\"remove\":[21]}}}}", NULL));
    log = shopApply(&n);
    CHECK(n == 0 && gShops[0].items == gStockA && gShops[1].items == gStockB);
    CHECK(strstr(log, "more than 6 items") != NULL);
    CHECK(strstr(log, "no items left") != NULL);
    free(log);

    /* the reference export lists the original ids and the limits */
    {
        char *ref;

        free(shopStart(1, NULL, NULL));
        ref = slurp("data/tables/shops.json");
        CHECK(ref != NULL);
        if (ref) {
            CHECK(strstr(ref, "\"0\": { \"items\": [11, 12, 13] }") != NULL);
            CHECK(strstr(ref, "\"1\": { \"items\": [21] }") != NULL);
            CHECK(strstr(ref, "up to 6") != NULL);
            free(ref);
        }
    }

    /* descriptors: a list that cannot fit, a min of 0 (the terminator), or a list in a file-backed table */
    {
        static const TmField bad0[] = {{"items", TM_S16, offsetof(Shop, items), 0, 400, "x", 6, offsetof(Shop, count)}};
        static const TmField bad1[] = {{"items", TM_S16, offsetof(Shop, items), 1, 400, "x", 2, offsetof(Shop, count)}};
        static const TmField bad2[] = {{"items", TM_S16, 12, 1, 400, "x", 6, offsetof(Shop, count)}};
        TmTable t = kShopTable;

        shopReset();
        tmInit(NULL);
        t.name = "bad0";
        t.fields = bad0;
        CHECK(tmRegister(&t) == -1);
        t.name = "bad1"; /* row 0 holds 3 ids, more than listMax 2 */
        t.fields = bad1;
        CHECK(tmRegister(&t) == -1);
        t.name = "bad2"; /* the pointer does not fit the row */
        t.fields = bad2;
        CHECK(tmRegister(&t) == -1);
    }
}

/* ---- a list field whose ids can be names (the shops' items of content mods) ---- */

static long long namedId(const char *name)
{
    return strcmp(name, "m:gadget") == 0 ? 500 : strcmp(name, "m:other") == 0 ? 501 : -1;
}

static int namedOk(long long id)
{
    return id <= 400 || id == 500; /* 501 is "registered" by name only in the mod's dreams: not listed */
}

static void testNamedIds(void)
{
    static const TmField f[] = {
        {"items", TM_S16, offsetof(Shop, items), 1, 32767, "ids", 6, offsetof(Shop, count), namedId, namedOk},
    };
    TmTable t = kShopTable;
    char *log;
    int n;

    t.fields = f;
    t.name = "shops";
    /* a name, a registered number and a plain one are all accepted, in order */
    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    put("mods/a/tables/shops.json", "{" SF ",\"rows\":{\"1\":{\"items\":[5,\"m:gadget\",500]},"
        "\"0\":{\"items\":{\"add\":[\"m:gadget\"]}}}}");
    {
        char m[1300];
        snprintf(m, sizeof m, "%s/mods", gRoot);
        shopReset();
        modsInit(m, NULL);
        tmInit(NULL);
    }
    CHECK(tmRegister(&t) == 0);
    log = shopApply(&n);
    free(log);
    CHECK(n == 2);
    CHECK(shopIs(0, 4, (const short[]){11, 12, 13, 500}));
    CHECK(shopIs(1, 3, (const short[]){5, 500, 500}));

    /* an unknown name, an id nobody defines and a number past the range are rejected whole, the shop stays */
    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    put("mods/a/tables/shops.json", "{" SF ",\"rows\":{\"0\":{\"items\":[\"m:nope\"]},\"1\":{\"items\":[501]},"
        "\"2\":{\"items\":[40000]}}}");
    {
        char m[1300];
        snprintf(m, sizeof m, "%s/mods", gRoot);
        shopReset();
        logBegin();
        modsInit(m, NULL);
        tmInit(NULL);
        CHECK(tmRegister(&t) == 0);
        log = logEnd();
    }
    CHECK(strstr(log, "row \"0\" field \"items\" rejected: no installed mod defines that name") != NULL);
    CHECK(strstr(log, "row \"1\" field \"items\" rejected: no installed mod defines that id") != NULL);
    CHECK(strstr(log, "row \"2\" field \"items\" rejected") != NULL);
    free(log);
    free(shopApply(&n));
    CHECK(n == 0 && gShops[0].items == gStockA);
}


/* ---- game_tables.c (E5a): the equipment and item effect tables over stubbed game data ---- */

ItemInfo ITEMS[402];
WeaponData WEAPON_DATA[123];
ArmorData ARMOR_DATA[77];
AccessoryData ACCESSORY_DATA[68];
static ItemEffect gEff[4];
static WeaponData gExtWeapon;
static ItemEffect gExtEffect;

/* what card_ext.c gives the tables (U8c): "m:card" is a mod card (id 317), "m:other" another (318); the ids 315-316 are no card */
long long cardExtByName(const char *name)
{
    if (!strcmp(name, "m:card")) return 317;
    if (!strcmp(name, "m:other")) return 318;
    if (!strncmp(name, "dw3:card/", 9)) return atoi(name + 9);
    return -1;
}

int cardExtIdOk(long long id)
{
    return (id >= 1 && id <= 314) || id == 317 || id == 318;
}

/* what item_ext.c gives the tables: "m:gun" is a mod weapon (id 500), "m:potion" a mod usable item (501) */
long long itemExtByName(const char *name)
{
    if (!strcmp(name, "m:gun")) return 500;
    if (!strcmp(name, "m:potion")) return 501;
    if (!strncmp(name, "dw3:item/", 9)) return atoi(name + 9);
    return -1;
}
void *itemExtWeaponRow(long long id) { return id == 500 ? (void *)&gExtWeapon : NULL; }
void *itemExtArmorRow(long long id) { (void)id; return NULL; }
void *itemExtAccessoryRow(long long id) { (void)id; return NULL; }
void *itemExtEffectRow(long long id) { return id == 501 ? (void *)&gExtEffect : NULL; }

static void growReset(void);

static void gtReset(void)
{
    int i;

    growReset();
    memset(ITEMS, 0, sizeof ITEMS);
    memset(WEAPON_DATA, 0, sizeof WEAPON_DATA);
    memset(ARMOR_DATA, 0, sizeof ARMOR_DATA);
    memset(ACCESSORY_DATA, 0, sizeof ACCESSORY_DATA);
    for (i = 0; i < 123; i++) {
        WEAPON_DATA[i].kind = EQUIP_KIND_EITHER_HAND;
        WEAPON_DATA[i].atk = (u16)(10 + i);
        WEAPON_DATA[i].amounts[0] = (u16)(i % 7);
        WEAPON_DATA[i].stats[0] = (u8)(i % 15);
    }
    for (i = 0; i < 77; i++) {
        ARMOR_DATA[i].kind = EQUIP_KIND_BODY;
        ARMOR_DATA[i].def = (s16)(5 + i);
        ARMOR_DATA[i].evasion = (u8)(i % 9);
    }
    for (i = 0; i < 68; i++) {
        ACCESSORY_DATA[i].kind = EQUIP_KIND_ACCESSORY;
        ACCESSORY_DATA[i].amount = (u16)(i + 1);
        ACCESSORY_DATA[i].stat = (u8)(i % 8);
    }
    gEff[0].flags = 3; gEff[0].kind = 1; gEff[0].amount = 500;
    gEff[1].flags = 1; gEff[1].kind = 2; gEff[1].amount = 10;
    gEff[2].flags = 2; gEff[2].kind = 0; gEff[2].amount = 32;
    gEff[3].flags = 4; gEff[3].kind = 0; gEff[3].amount = 0;
    /* usable items 43, 44, 45 and 46 (46 shares 45's record) and 394 */
    ITEMS[42].kind = ITEMS[43].kind = ITEMS[44].kind = ITEMS[45].kind = ITEMS[393].kind = ITEM_KIND_USABLE;
    ITEMS[42].data.effect = &gEff[0];
    ITEMS[43].data.effect = &gEff[1];
    ITEMS[44].data.effect = &gEff[2];
    ITEMS[45].data.effect = &gEff[2];
    ITEMS[393].data.effect = &gEff[3];
    ITEMS[0].kind = ITEM_KIND_KEY; /* a key item: no record, no row */
    memset(&gExtWeapon, 0, sizeof gExtWeapon);
    gExtWeapon.atk = 40;
    memset(&gExtEffect, 0, sizeof gExtEffect);
    gExtEffect.amount = 50;
}

static char *gtStart(int withData, const char *weapons, const char *armor, const char *acc, const char *effects)
{
    char m[1300], d[1300];

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    if (weapons) put("mods/a/tables/weapon_data.json", weapons);
    if (armor) put("mods/a/tables/armor_data.json", armor);
    if (acc) put("mods/a/tables/accessory_data.json", acc);
    if (effects) put("mods/a/tables/item_effects.json", effects);
    snprintf(m, sizeof m, "%s/mods", gRoot);
    snprintf(d, sizeof d, "%s/data", gRoot);
    gtReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(withData ? d : NULL);
    gameTablesRegister();
    gameTablesRegisterReport();
    return logEnd();
}

static void testGameTables(void)
{
    static WeaponData w0[123];
    static ItemEffect e0[4];
    char *log, *ref;
    int n;

    /* no mods: the four tables are registered and the game's data is never written */
    gtReset();
    memcpy(w0, WEAPON_DATA, sizeof w0);
    memcpy(e0, gEff, sizeof e0);
    free(gtStart(1, NULL, NULL, NULL, NULL));
    CHECK(tmPatchCount("weapon_data") == 0 && tmPatchCount("item_effects") == 0);
    CHECK(tmApply("weapon_data") == 0 && tmApply("armor_data") == 0 && tmApply("accessory_data") == 0 && tmApply("item_effects") == 0);
    CHECK(!memcmp(w0, WEAPON_DATA, sizeof w0) && !memcmp(e0, gEff, sizeof e0));
    /* the layout of each record: the registrations need the offsets and sizes of the game's structs */
    CHECK(sizeof(WeaponData) == 0x14 && sizeof(ArmorData) == 0x14 && sizeof(AccessoryData) == 0xC && sizeof(ItemEffect) == 4);
    CHECK(gameTableWeapons()->rows == 123 && gameTableWeapons()->keyBase == 92 && gameTableWeapons()->nfields == 14);
    CHECK(gameTableArmor()->rows == 77 && gameTableArmor()->keyBase == 215 && gameTableArmor()->nfields == 10);
    CHECK(gameTableAccessories()->rows == 68 && gameTableAccessories()->keyBase == 292 && gameTableAccessories()->nfields == 6);
    CHECK(gameTableItemEffects()->rows == 4 && gameTableItemEffects()->keys[3] == 394 && gameTableItemEffects()->nfields == 3);

    /* the reference exports: every row under its item id, nothing else */
    ref = slurp("data/tables/weapon_data.json");
    CHECK(ref && strstr(ref, "\"92\": {") && strstr(ref, "\"214\": {") && !strstr(ref, "\"215\": {") && !strstr(ref, "\"91\": {"));
    CHECK(ref && strstr(ref, "\"atk\": 10, ") && strstr(ref, "\"atk\": 132, "));
    CHECK(ref && strstr(ref, "Valid: 0 to 999."));
    free(ref);
    ref = slurp("data/tables/armor_data.json");
    CHECK(ref && strstr(ref, "\"215\": {") && strstr(ref, "\"291\": {") && !strstr(ref, "\"292\": {"));
    free(ref);
    ref = slurp("data/tables/accessory_data.json");
    CHECK(ref && strstr(ref, "\"292\": {") && strstr(ref, "\"359\": {") && !strstr(ref, "\"360\": {"));
    free(ref);
    ref = slurp("data/tables/item_effects.json");
    CHECK(ref && strstr(ref, "\"43\": { \"flags\": 3, \"kind\": 1, \"amount\": 500 }"));
    CHECK(ref && strstr(ref, "\"45\": {") && !strstr(ref, "\"46\": {") && !strstr(ref, "\"1\": {") && strstr(ref, "\"394\": {"));
    free(ref);

    /* weapons: a patch by item id, a range refused, an operation, a mod weapon, a key that is not a weapon */
    log = gtStart(0,
        "{\"format\":\"weapon_data-v1\",\"rows\":{\"92\":{\"atk\":500,\"amount1\":-5},\"93\":{\"atk\":1000,\"stat1\":15},"
        "\"94\":{\"atk\":\"*2\"},\"m:gun\":{\"atk\":77},\"215\":{\"atk\":1}}}",
        NULL, NULL, NULL);
    CHECK(strstr(log, "row \"93\" field \"atk\" rejected") && strstr(log, "row \"93\" field \"stat1\" rejected"));
    CHECK(strstr(log, "row \"215\" rejected: no row with that key"));
    free(log);
    n = tmApply("weapon_data");
    CHECK(n == 2); /* the mod weapon is outside the table's rows */
    CHECK(WEAPON_DATA[0].atk == 500 && (s16)WEAPON_DATA[0].amounts[0] == -5);
    CHECK(WEAPON_DATA[1].atk == 11 && WEAPON_DATA[1].stats[0] == 1); /* 93: both fields refused */
    CHECK(WEAPON_DATA[2].atk == 24 && gExtWeapon.atk == 77);
    CHECK(WEAPON_DATA[3].atk == 13 && WEAPON_DATA[0].kind == EQUIP_KIND_EITHER_HAND); /* the rest is untouched */
    CHECK(tmApply("weapon_data") == 2 && WEAPON_DATA[2].atk == 24); /* applying again never doubles it */
    CHECK(((const WeaponData *)tmOriginalRow("weapon_data", 94))->atk == 12);

    /* armour: signed defense, evasion past a byte refused */
    free(gtStart(0, NULL, "{\"format\":\"armor_data-v1\",\"rows\":{\"216\":{\"def\":-20,\"evasion\":300},\"217\":{\"evasion\":\"+4\"}}}", NULL,
        NULL));
    CHECK(tmApply("armor_data") == 2);
    CHECK(ARMOR_DATA[1].def == -20 && ARMOR_DATA[1].evasion == 1 && ARMOR_DATA[2].evasion == 6);

    /* accessories: kind 7 is a weapon kind, the row keeps its values (the whole row, the other fields too) */
    log = gtStart(0, NULL, NULL,
        "{\"format\":\"accessory_data-v1\",\"rows\":{\"292\":{\"kind\":7,\"amount\":9},\"293\":{\"amount\":50,\"stat\":19},"
        "\"294\":{\"stat\":22}}}", NULL);
    CHECK(strstr(log, "row \"294\" field \"stat\" rejected"));
    free(log);
    logBegin();
    n = tmApply("accessory_data");
    log = logEnd();
    CHECK(strstr(log, "accessory_data row 292 rejected: kind 7"));
    free(log);
    CHECK(n == 1 && ACCESSORY_DATA[0].kind == EQUIP_KIND_ACCESSORY && ACCESSORY_DATA[0].amount == 1);
    CHECK(ACCESSORY_DATA[1].amount == 50 && ACCESSORY_DATA[1].stat == 19 && ACCESSORY_DATA[2].stat == 2);

    /* effects: separate records written through their rows; a stat boost of 0 refused whole; a mod item and "dw3:item/<id>" */
    log = gtStart(0, NULL, NULL, NULL,
        "{\"format\":\"item_effects-v1\",\"rows\":{\"dw3:item/43\":{\"amount\":9999},\"44\":{\"amount\":0,\"flags\":3},\"394\":{\"amount\":\"+30000\"},"
        "\"m:potion\":{\"amount\":123},\"46\":{\"amount\":1},\"45\":{\"flags\":3,\"kind\":18}}}");
    CHECK(strstr(log, "row \"46\" rejected: no row with that key"));
    CHECK(strstr(log, "row \"45\" field \"kind\" rejected"));
    CHECK(strstr(log, "a field is listed twice") == NULL);
    free(log);
    logBegin();
    n = tmApply("item_effects");
    log = logEnd();
    CHECK(strstr(log, "item_effects row 44 rejected: a stat boost"));
    free(log);
    CHECK(gEff[0].amount == 9999 && gEff[0].flags == 3 && gEff[0].kind == 1);
    CHECK(gEff[1].amount == 10 && gEff[1].flags == 1); /* kind 2 with amount 0: back to its original */
    CHECK(gEff[2].flags == 3 && gEff[2].amount == 32 && ITEMS[45].data.effect == &gEff[2]); /* item 45 and the shared 46 */
    CHECK(gEff[3].amount == 20000); /* "+30000" stops at the range */
    CHECK(gExtEffect.amount == 123);
    CHECK(n == 3 && tmApply("item_effects") == 3 && gEff[3].amount == 20000 && gEff[0].amount == 9999);
    CHECK(ITEMS[42].data.effect == &gEff[0] && ITEMS[393].data.effect == &gEff[3]); /* the pointers are never touched */
}

/* ---- game_tables.c (E5b): evolutions, blast forms and the growth tables, over stubbed game data ---- */

PcEvolution STFGTREP_evolutionLists[8][44];
s32 STFGTREP_levelExp[4];
s32 STFGTREP_vitalCuts[4];
s32 STFGTREP_vitalRandom[9];
s32 STFGTREP_statGains[6][9];
s32 STFGTREP_resistGains[8];
static DigimonData gDig[400];
static DigimonData gDigScratch;
static PcDigiLinks gLinks[400]; /* the side store of ext_state.c (u16 blast forms and pair partner per row), stubbed */
static int gDigRows = 57;
static int gDigRowsWant = 57; /* what growReset gives the table: a test with more rows sets it */
static PcEvolution gEvoCopy[45]; /* the port's extended list of partner 2: the 44 originals and one entry a mod added */

DigimonData *pcDigimonRef(int row) { return row >= 0 && row < gDigRows ? &gDig[row] : &gDigScratch; }
int pcDigimonRowCount(void) { return gDigRows; }
PcDigiLinks *pcDigimonLinks(int row) { return row >= 0 && row < gDigRows ? &gLinks[row] : NULL; }
void pcDigimonLinksInit(void)
{
    int r, k;

    memset(gLinks, 0, sizeof gLinks);
    for (r = 0; r < 55; r++) {
        gLinks[r].id = gDig[r].id;
        for (k = 0; k < 5; k++) gLinks[r].blast[k] = gDig[r].blastForms[k];
        gLinks[r].pair = gDig[r].pairPartner;
        gLinks[r].set = 1;
    }
}

/* what digimon_ext.c gives the tables: "m:form" is a new partner form at row 56 (row + 1 = 57), "dw3:digimon/<id>" an original id */
long long digimonExtRowRef(const char *name)
{
    int r, id;

    if (!strcmp(name, "m:form")) return 57;
    if (strncmp(name, "dw3:digimon/", 12)) return -1;
    id = atoi(name + 12);
    for (r = 0; r < 52; r++) {
        if (gDig[r].id == id) return r + 1;
    }
    return -1;
}

/* partner 2 has an extended list (the copy), the others do not */
static void *growCopyOf(int partner, void *original)
{
    return partner == 2 ? (void *)gEvoCopy : original;
}

static void growReset(void)
{
    int p, i, t, c;

    for (p = 0; p < 8; p++) {
        for (i = 0; i < 44; i++) {
            PcEvolution *e = &STFGTREP_evolutionLists[p][i];

            e->digimon = (p * 100 + i) % 50 + 1;
            e->needs[0].digimon = (s16)(i % 3 == 0 ? 7 : 0);
            e->needs[0].level = (s16)(20 + i % 10);
            e->needs[1].digimon = 0;
            e->needs[1].level = 1;
            e->stat = (s16)(i % 15);
            e->value = (s16)(10 + i);
        }
    }
    memcpy(gEvoCopy, STFGTREP_evolutionLists[2], sizeof STFGTREP_evolutionLists[2]);
    memset(&gEvoCopy[44], 0, sizeof gEvoCopy[44]);
    gEvoCopy[44].digimon = 57;
    gEvoCopy[44].value = 3;
    for (i = 0; i < 4; i++) {
        STFGTREP_levelExp[i] = i == 0 ? 0 : i == 1 ? 50 : i == 2 ? 800 : 3000;
        STFGTREP_vitalCuts[i] = i * 5;
    }
    for (i = 0; i < 9; i++) STFGTREP_vitalRandom[i] = i - 4;
    for (t = 0; t < 6; t++) {
        for (c = 0; c < 9; c++) STFGTREP_statGains[t][c] = t + c;
    }
    for (i = 0; i < 8; i++) STFGTREP_resistGains[i] = i / 3;
    memset(gDig, 0, sizeof gDig);
    gDigRows = gDigRowsWant;
    for (i = 0; i < 57; i++) {
        int k;

        gDig[i].id = (u16)(i < 52 ? 10 + i : i == 55 ? 471 : i == 56 ? 472 : 0);
        gDig[i].hp = (u8)(100 + i);
        for (k = 0; i != 11 && k < 5; k++) gDig[i].blastForms[k] = (u8)((i + k) % 52 + 1); /* row 11 (id 21) never blasts: all 0 */
    }
}

static char *growStart(int withData, const char *const *names, const char *const *texts, int n)
{
    char m[1300], d[1300];
    int i;

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    for (i = 0; i < n; i++) {
        char rel[200];

        snprintf(rel, sizeof rel, "mods/a/tables/%s.json", names[i]);
        put(rel, texts[i]);
    }
    snprintf(m, sizeof m, "%s/mods", gRoot);
    snprintf(d, sizeof d, "%s/data", gRoot);
    gtReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(withData ? d : NULL);
    gameTablesRegister();
    gameTablesRegisterReport();
    return logEnd();
}

static void testGrowthTables(void)
{
    static PcEvolution e0[8][44], copy0[45];
    static s32 lx0[4], st0[6][9];
    static DigimonData d0[57];
    char *log, *ref;
    int n;
    const char *names[6];
    const char *texts[6];

    /* no mods: registered, never written, the extended copy never touched */
    growReset();
    memcpy(e0, STFGTREP_evolutionLists, sizeof e0);
    memcpy(copy0, gEvoCopy, sizeof copy0);
    memcpy(lx0, STFGTREP_levelExp, sizeof lx0);
    memcpy(st0, STFGTREP_statGains, sizeof st0);
    memcpy(d0, gDig, sizeof d0);
    free(growStart(1, NULL, NULL, 0));
    gEvoCopy[10].value = 999; /* a sentinel: with no patch the sync leaves the copy alone */
    copy0[10].value = 999;
    gameTablesApplyReport(growCopyOf);
    CHECK(!memcmp(e0, STFGTREP_evolutionLists, sizeof e0) && !memcmp(copy0, gEvoCopy, sizeof copy0));
    CHECK(!memcmp(lx0, STFGTREP_levelExp, sizeof lx0) && !memcmp(st0, STFGTREP_statGains, sizeof st0) && !memcmp(d0, gDig, sizeof d0));
    CHECK(tmPatchCount("evolutions") == 0 && tmPatchCount("blast_forms") == 0 && tmPatchCount("level_exp") == 0);

    /* the layout and keys: the struct sizes the offsets rely on, the key of each row */
    CHECK(sizeof(PcEvolution) == 16 && offsetof(DigimonData, blastForms) == 0x50 && offsetof(DigimonData, id) == 0);
    CHECK(gameTableEvolutions()->rows == 352 && gameTableEvolutions()->nfields == 7 && gameTableEvolutions()->keys[0] == 0);
    CHECK(gameTableEvolutions()->keys[43] == 43 && gameTableEvolutions()->keys[44] == 100 && gameTableEvolutions()->keys[351] == 743);
    CHECK(gameTableBlastForms()->rows == 52 && gameTableBlastForms()->nfields == 5 && gameTableBlastForms()->keys[0] == 10 && gameTableBlastForms()->keys[51] == 61);
    CHECK(gameTableLevelExp()->rows == 4 && gameTableLevelExp()->keys[0] == 1 && gameTableLevelExp()->keys[3] == 40);
    CHECK(gameTableVitalCuts()->rows == 4 && gameTableVitalCuts()->keys[2] == 20);
    CHECK(gameTableVitalRandom()->rows == 9 && gameTableVitalRandom()->keyBase == 0);
    CHECK(gameTableStatGains()->rows == 6 && gameTableStatGains()->nfields == 9 && gameTableStatGains()->keys[5] == 80);
    CHECK(gameTableResistGains()->rows == 8 && gameTableResistGains()->keyBase == 0);

    /* the reference exports */
    ref = slurp("data/tables/evolutions.json");
    CHECK(ref && strstr(ref, "\"0\": {") && strstr(ref, "\"743\": {") && !strstr(ref, "\"44\": {") && strstr(ref, "\"need1Level\""));
    CHECK(ref && strstr(ref, "\"203\": { \"digimon\": "));
    free(ref);
    ref = slurp("data/tables/blast_forms.json");
    CHECK(ref && strstr(ref, "\"10\": { \"blast1\": 1, \"blast2\": 2,") && strstr(ref, "\"61\": {") && !strstr(ref, "\"62\": {"));
    free(ref);
    ref = slurp("data/tables/level_exp.json");
    CHECK(ref && strstr(ref, "\"1\": { \"exp\": 0 }") && strstr(ref, "\"5\": { \"exp\": 50 }") && strstr(ref, "\"40\": { \"exp\": 3000 }"));
    free(ref);
    ref = slurp("data/tables/stat_gains.json");
    CHECK(ref && strstr(ref, "\"80\": { \"gain0\": 5, ") && !strstr(ref, "\"100\": {"));
    free(ref);
    ref = slurp("data/tables/resist_gains.json");
    CHECK(ref && strstr(ref, "\"7\": { \"gain\": 2 }") && !strstr(ref, "\"8\": {"));
    free(ref);

    /* evolutions: a patch by partner * 100 + entry, names for the Digimon, ranges, a row of a mod (not addressable), operations */
    names[0] = "evolutions";
    texts[0] = "{\"format\":\"evolutions-v1\",\"rows\":{\"203\":{\"need1Level\":55,\"value\":400,\"digimon\":\"m:form\",\"need2\":\"dw3:digimon/12\"},"
               "\"204\":{\"digimon\":9999,\"stat\":15,\"need1\":58,\"need2Level\":100},\"206\":{\"digimon\":54,\"need2\":53,\"need1\":56},\"205\":{\"value\":\"*2\",\"need1\":\"dw3:digimon/10\"},"
               "\"244\":{\"value\":5},\"7\":{\"value\":1}}}";
    log = growStart(0, names, texts, 1);
    CHECK(strstr(log, "row \"204\" field \"digimon\" rejected") && strstr(log, "row \"204\" field \"stat\" rejected"));
    CHECK(strstr(log, "row \"204\" field \"need1\" rejected") && strstr(log, "row \"204\" field \"need2Level\" rejected"));
    CHECK(strstr(log, "row \"244\" rejected: no row with that key"));
    /* rows 53-55 of the Digimon table are placeholders: a form to learn or to need cannot be one */
    CHECK(strstr(log, "row \"206\" field \"digimon\" rejected") && strstr(log, "row \"206\" field \"need2\" rejected"));
    free(log);
    gEvoCopy[10].value = 999;
    n = tmApply("evolutions");
    CHECK(n == 4); /* 203, 205, 7 and 206 (need1 56 is a mod's form: accepted); every other field of 204 was refused */
    CHECK(STFGTREP_evolutionLists[2][6].digimon == (206 % 50) + 1 && STFGTREP_evolutionLists[2][6].needs[0].digimon == 56);
    gameTablesApplyReport(growCopyOf);
    CHECK(STFGTREP_evolutionLists[2][3].needs[0].level == 55 && STFGTREP_evolutionLists[2][3].value == 400);
    CHECK(STFGTREP_evolutionLists[2][3].digimon == 57 && STFGTREP_evolutionLists[2][3].needs[1].digimon == 3);
    CHECK(STFGTREP_evolutionLists[2][4].digimon == (204 % 50) + 1 && STFGTREP_evolutionLists[2][4].stat == 4); /* all four refused */
    CHECK(STFGTREP_evolutionLists[2][5].value == 2 * (10 + 5) && STFGTREP_evolutionLists[2][5].needs[0].digimon == 1);
    CHECK(STFGTREP_evolutionLists[0][7].value == 1 && STFGTREP_evolutionLists[0][8].value == 18); /* the rest is untouched */
    /* the port's extended list: the original 44 entries follow the patched array, the mod's entry stays where it is */
    CHECK(!memcmp(gEvoCopy, STFGTREP_evolutionLists[2], sizeof STFGTREP_evolutionLists[2]));
    CHECK(gEvoCopy[44].digimon == 57 && gEvoCopy[44].value == 3 && gEvoCopy[3].value == 400);
    /* applying again never adds up (the "*2" row), and the copy still agrees */
    gameTablesApplyReport(growCopyOf);
    CHECK(STFGTREP_evolutionLists[2][5].value == 30 && !memcmp(gEvoCopy, STFGTREP_evolutionLists[2], sizeof STFGTREP_evolutionLists[2]));
    CHECK(gEvoCopy[44].value == 3);
    CHECK(((const PcEvolution *)tmOriginalRow("evolutions", 205))->value == 15);

    /* the growth tables, patched by level / index */
    names[0] = "level_exp";
    texts[0] = "{\"format\":\"level_exp-v1\",\"rows\":{\"5\":{\"exp\":123456},\"40\":{\"exp\":2000000000},\"20\":{\"exp\":\"*2\"},\"2\":{\"exp\":1}}}";
    names[1] = "vital_cuts";
    texts[1] = "{\"format\":\"vital_cuts-v1\",\"rows\":{\"1\":{\"cut\":-1},\"40\":{\"cut\":7}}}";
    names[2] = "vital_random";
    texts[2] = "{\"format\":\"vital_random-v1\",\"rows\":{\"0\":{\"value\":-300},\"8\":{\"value\":400},\"9\":{\"value\":1}}}";
    names[3] = "stat_gains";
    texts[3] = "{\"format\":\"stat_gains-v1\",\"rows\":{\"20\":{\"gain4\":7,\"gain3\":-1,\"gain8\":1000},\"80\":{\"gain0\":\"+3\"},\"3\":{\"gain0\":1}}}";
    names[4] = "resist_gains";
    texts[4] = "{\"format\":\"resist_gains-v1\",\"rows\":{\"3\":{\"gain\":5},\"7\":{\"gain\":-1},\"8\":{\"gain\":1}}}";
    log = growStart(0, names, texts, 5);
    CHECK(strstr(log, "level_exp.json row \"40\" field \"exp\" rejected") && strstr(log, "level_exp.json row \"2\" rejected: no row with that key"));
    CHECK(strstr(log, "vital_cuts.json row \"1\" field \"cut\" rejected"));
    CHECK(strstr(log, "vital_random.json row \"0\" field \"value\" rejected") && strstr(log, "vital_random.json row \"8\" field \"value\" rejected"));
    CHECK(strstr(log, "vital_random.json row \"9\" rejected: no row with that key"));
    CHECK(strstr(log, "stat_gains.json row \"20\" field \"gain3\" rejected") && strstr(log, "stat_gains.json row \"20\" field \"gain8\" rejected"));
    CHECK(strstr(log, "stat_gains.json row \"3\" rejected: no row with that key"));
    CHECK(strstr(log, "resist_gains.json row \"7\" field \"gain\" rejected") && strstr(log, "resist_gains.json row \"8\" rejected"));
    free(log);
    gameTablesApplyReport(growCopyOf);
    gameTablesApplyReport(growCopyOf);
    CHECK(STFGTREP_levelExp[0] == 0 && STFGTREP_levelExp[1] == 123456 && STFGTREP_levelExp[2] == 1600 && STFGTREP_levelExp[3] == 3000);
    CHECK(STFGTREP_vitalCuts[3] == 7 && STFGTREP_vitalCuts[1] == 5 && STFGTREP_vitalCuts[2] == 10);
    CHECK(STFGTREP_vitalRandom[0] == -4 && STFGTREP_vitalRandom[8] == 4);
    CHECK(STFGTREP_statGains[2][4] == 7 && STFGTREP_statGains[2][3] == 5 && STFGTREP_statGains[2][8] == 10);
    CHECK(STFGTREP_statGains[5][0] == 8 && STFGTREP_statGains[4][0] == 4);
    CHECK(STFGTREP_resistGains[3] == 5 && STFGTREP_resistGains[7] == 2 && STFGTREP_resistGains[2] == 0);

    /* blast forms: by Digimon id (not row), names, a u8 cap; the rest of the row, the original values of other rows, kept */
    names[0] = "blast_forms";
    texts[0] = "{\"format\":\"blast_forms-v1\",\"rows\":{\"13\":{\"blast1\":2,\"blast2\":\"m:form\",\"blast3\":300,\"blast4\":58,"
               "\"blast5\":\"dw3:digimon/20\"},\"3\":{\"blast1\":1},\"471\":{\"blast1\":1},"
               "\"14\":{\"blast2\":0,\"blast3\":54},\"21\":{\"blast1\":5},\"22\":{\"blast1\":0}}}";
    log = growStart(0, names, texts, 1);
    CHECK(strstr(log, "row \"13\" field \"blast3\" rejected") && strstr(log, "row \"13\" field \"blast4\" rejected"));
    CHECK(strstr(log, "row \"3\" rejected: no row with that key") && strstr(log, "row \"471\" rejected: no row with that key"));
    /* a blast of 0 on a row that has blast forms is `PC_DIGIMON(-1)` for the game: the whole row is refused (id 14, row 4; id 22 too);
       a placeholder row (54) is refused as a field; a row that never blasts (id 21) may take a form and keep its 0s */
    CHECK(strstr(log, "blast_forms row 14 rejected") && strstr(log, "blast_forms row 22 rejected") && strstr(log, "row \"14\" field \"blast3\" rejected"));
    free(log);
    CHECK(tmApply("blast_forms") == 2);
    CHECK(gLinks[3].blast[0] == 2 && gLinks[3].blast[1] == 57 && gLinks[3].blast[2] == (3 + 2) % 52 + 1);
    CHECK(gLinks[3].blast[3] == (3 + 3) % 52 + 1 && gLinks[3].blast[4] == 11 && gDig[3].hp == 103 && gLinks[4].blast[0] == 5);
    CHECK(gLinks[4].blast[1] == (4 + 1) % 52 + 1 && gLinks[4].blast[2] == (4 + 2) % 52 + 1 && gLinks[12].blast[0] == 13);
    CHECK(gLinks[11].blast[0] == 5 && gLinks[11].blast[1] == 0);
    CHECK(tmApply("blast_forms") == 2 && gLinks[3].blast[1] == 57 && gDig[3].hp == 103);
    /* the pair partner and the id of a row are not table fields and never change */
    CHECK(gLinks[3].id == 13 && gLinks[3].pair == 0 && gLinks[3].set == 1);

    /* U9: a Digimon table with 360 rows: a blast form may be a row past a byte (a name or a number), the numbers that are not
       rows are still refused, and the u8 of the row is not the value the game reads */
    gDigRowsWant = 360;
    growReset();
    gDig[300].id = 700;
    texts[0] = "{\"format\":\"blast_forms-v1\",\"rows\":{\"13\":{\"blast1\":300,\"blast2\":360,\"blast3\":361,\"blast4\":4096,\"blast5\":54},"
               "\"14\":{\"blast1\":256,\"blast2\":255}}}";
    log = growStart(0, names, texts, 1);
    CHECK(strstr(log, "row \"13\" field \"blast3\" rejected") && strstr(log, "row \"13\" field \"blast4\" rejected") &&
          strstr(log, "row \"13\" field \"blast5\" rejected") && !strstr(log, "field \"blast1\" rejected") &&
          !strstr(log, "field \"blast2\" rejected") && !strstr(log, "row \"14\" field"));
    free(log);

    CHECK(gLinks[3].blast[0] == 300 && gLinks[3].blast[1] == 360 && gLinks[3].blast[2] == (3 + 2) % 52 + 1);
    CHECK(gLinks[4].blast[0] == 256 && gLinks[4].blast[1] == 255 && gLinks[4].blast[2] == (4 + 2) % 52 + 1);
    CHECK(gDig[3].blastForms[0] == (3 + 0) % 52 + 1); /* the disc's bytes are never written */
    CHECK(tmApply("blast_forms") == 2 && gLinks[3].blast[0] == 300);
    gDigRowsWant = 57;
    growReset();
}

/* ---- game_tables.c (E5c): the field's enemies, encounters and battle rates, over stubbed game data ---- */

BattleEnemy FIELDSTG_enemies[523];
GameFieldEncounter FIELDSTG_encounters[335];
s32 FIELDSTG_battleRates[6];

/* what digimon_ext.c gives the tables: enemies are 100-299 and "m:boss" (600); partner forms are 10-61 and "m:form" (472) */
int digimonExtNameId(int id)
{
    return (id >= 10 && id <= 61) || (id >= 100 && id <= 299) || id == 600 || id == 472 ? id : -1;
}
int digimonExtIsPartner(int id) { return id == 472; }
int digimonExtNewId(const char *name)
{
    return !strcmp(name, "m:boss") ? 600 : !strcmp(name, "m:form") ? 472 : -1;
}
const ContentType *digimonExtType(void) { return NULL; }
int contentOriginalId(const ContentType *t, const char *name)
{
    (void)t;
    return !strncmp(name, "dw3:digimon/", 12) ? atoi(name + 12) : -1;
}

static const char *fakeFighterName(int id)
{
    static char buf[40];

    snprintf(buf, sizeof buf, "Foe %d", id);
    return buf;
}

static void fieldReset(void)
{
    static const s32 rates[6] = {2000, 3, 4, 6, 9, 18};
    int i, k;

    memset(FIELDSTG_enemies, 0, sizeof FIELDSTG_enemies);
    for (i = 1; i < 523; i++) {
        FIELDSTG_enemies[i].fighter = 100 + i % 100;
        FIELDSTG_enemies[i].level = (s16)(1 + i % 99);
        FIELDSTG_enemies[i].hp = (s16)(100 + i);
        FIELDSTG_enemies[i].mp = (s16)(i % 50);
        FIELDSTG_enemies[i].strength = 16;
    }
    for (i = 0; i < 335; i++) {
        GameFieldEncounter *g = &FIELDSTG_encounters[i];

        memset(g, 0, sizeof *g);
        g->enemies[0] = &FIELDSTG_enemies[i == 0 ? 0 : i % 522 + 1];
        g->enemies[1] = &FIELDSTG_enemies[i == 0 || i % 3 == 0 ? 0 : (i + 1) % 522 + 1];
        g->enemies[2] = &FIELDSTG_enemies[0];
        g->ambushChance = (u8)(i % 2 ? 127 : 0);
        g->kind = (u8)(i == 0 ? 0 : 1 + i % 5);
        for (k = 0; k < 12; k++) g->blocks[k] = (u8)((i + k) % 2);
    }
    memcpy(FIELDSTG_battleRates, rates, sizeof rates);
}

static char *fieldStart(int withData, const char *enemies, const char *encounters, const char *rates)
{
    char m[1300], d[1300];

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    if (enemies) put("mods/a/tables/field_enemies.json", enemies);
    if (encounters) put("mods/a/tables/field_encounters.json", encounters);
    if (rates) put("mods/a/tables/battle_rates.json", rates);
    snprintf(m, sizeof m, "%s/mods", gRoot);
    snprintf(d, sizeof d, "%s/data", gRoot);
    fieldReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(withData ? d : NULL);
    gameTablesSetFighterNames(fakeFighterName);
    CHECK(gameTablesRegisterField() == 3);
    return logEnd();
}

static void testFieldTables(void)
{
    static BattleEnemy en0[523];
    static GameFieldEncounter enc0[335];
    s32 rates0[6];
    char *log, *ref;
    int i;

    /* no mods: registered, the game's memory is never written, the layout the offsets rely on */
    free(fieldStart(1, NULL, NULL, NULL));
    memcpy(en0, FIELDSTG_enemies, sizeof en0);
    memcpy(enc0, FIELDSTG_encounters, sizeof enc0);
    memcpy(rates0, FIELDSTG_battleRates, sizeof rates0);
    gameTablesApplyField();
    CHECK(!memcmp(en0, FIELDSTG_enemies, sizeof en0) && !memcmp(enc0, FIELDSTG_encounters, sizeof enc0));
    CHECK(!memcmp(rates0, FIELDSTG_battleRates, sizeof rates0));
    CHECK(tmPatchCount("field_enemies") == 0 && tmPatchCount("field_encounters") == 0 && tmPatchCount("battle_rates") == 0);
    CHECK(sizeof(BattleEnemy) == 12 && offsetof(BattleEnemy, level) == 4 && offsetof(BattleEnemy, strength) == 10);
    CHECK(gameTableFieldEnemies()->rows == 523 && gameTableFieldEnemies()->nfields == 5);
    CHECK(gameTableFieldEncounters()->rows == 335 && gameTableFieldEncounters()->nfields == 17);
    CHECK(gameTableBattleRates()->rows == 6 && gameTableBattleRates()->nfields == 1);

    /* the reference exports: every row under its number, the Digimon names in "labels" (outside the rows) */
    ref = slurp("data/tables/field_enemies.json");
    CHECK(ref && strstr(ref, "\"522\": {") && !strstr(ref, "\"523\": {") && strstr(ref, "\"fighter\": 101, \"level\": 2, \"hp\": 101"));
    CHECK(ref && strstr(ref, "\"labels\": {") && strstr(ref, "\"1\": \"Foe 101\"") && !strstr(ref, "\"0\": \"Foe"));
    CHECK(ref && strstr(ref, "Valid: 1 to 99.") && strstr(ref, "Valid: 0 to 32767."));
    free(ref);
    ref = slurp("data/tables/field_encounters.json");
    CHECK(ref && strstr(ref, "\"334\": {") && !strstr(ref, "\"335\": {") && strstr(ref, "\"enemy1\": 2, \"enemy2\": 3, \"enemy3\": 0"));
    CHECK(ref && strstr(ref, "\"2\": \"Foe 103, Foe 104\"") && strstr(ref, "\"3\": \"Foe 104\""));
    free(ref);
    ref = slurp("data/tables/battle_rates.json");
    CHECK(ref && strstr(ref, "\"0\": { \"rate\": 2000 }") && strstr(ref, "\"5\": { \"rate\": 18 }") && !strstr(ref, "\"labels\""));
    free(ref);

    /* enemies: by number, by name, an operation, a partner form or an unknown id refused, a row that would be emptied and row 0 refused whole */
    log = fieldStart(0,
        "{\"format\":\"field_enemies-v1\",\"rows\":{\"1\":{\"fighter\":150,\"level\":5,\"hp\":\"*2\"},\"2\":{\"fighter\":\"dw3:digimon/200\"},"
        "\"3\":{\"fighter\":\"m:boss\",\"mp\":0},\"4\":{\"fighter\":\"m:form\"},\"5\":{\"fighter\":30},\"6\":{\"fighter\":999},"
        "\"7\":{\"level\":100,\"hp\":0,\"strength\":0},\"8\":{\"fighter\":0},\"0\":{\"level\":5},\"523\":{\"level\":5},\"9\":{\"strength\":\"+16\"},\"10\":{\"strength\":501}}}",
        NULL, NULL);
    CHECK(strstr(log, "row \"4\" field \"fighter\" rejected") && strstr(log, "row \"5\" field \"fighter\" rejected"));
    CHECK(strstr(log, "row \"6\" field \"fighter\" rejected") && strstr(log, "row \"523\" rejected: no row with that key"));
    CHECK(strstr(log, "row \"7\" field \"level\" rejected") && strstr(log, "row \"7\" field \"hp\" rejected") &&
          strstr(log, "row \"7\" field \"strength\" rejected"));
    free(log);
    logBegin();
    gameTablesApplyField();
    log = logEnd();
    CHECK(strstr(log, "field_enemies row 8 rejected: an enemy cannot be emptied"));
    CHECK(strstr(log, "field_enemies row 0 rejected: row 0 is the empty slot"));
    free(log);
    CHECK(FIELDSTG_enemies[1].fighter == 150 && FIELDSTG_enemies[1].level == 5 && FIELDSTG_enemies[1].hp == 202);
    CHECK(FIELDSTG_enemies[2].fighter == 200 && FIELDSTG_enemies[3].fighter == 600 && FIELDSTG_enemies[3].mp == 0);
    CHECK(FIELDSTG_enemies[4].fighter == 104 && FIELDSTG_enemies[5].fighter == 105 && FIELDSTG_enemies[6].fighter == 106);
    CHECK(FIELDSTG_enemies[7].level == 8 && FIELDSTG_enemies[7].hp == 107 && FIELDSTG_enemies[7].strength == 16);
    CHECK(FIELDSTG_enemies[8].fighter == 108 && FIELDSTG_enemies[0].fighter == 0 && FIELDSTG_enemies[0].level == 0);
    CHECK(FIELDSTG_enemies[9].strength == 32 && FIELDSTG_enemies[10].fighter == 110 && FIELDSTG_enemies[10].strength == 16); /* 501: over the cap */
    CHECK(!memcmp(enc0, FIELDSTG_encounters, sizeof enc0) && !memcmp(rates0, FIELDSTG_battleRates, sizeof rates0)); /* untouched */
    /* the overlay is restored on the next entry and the patch goes on again: the same result, never added up */
    fieldReset();
    gameTablesApplyField();
    CHECK(FIELDSTG_enemies[1].hp == 202 && FIELDSTG_enemies[9].strength == 32 && FIELDSTG_enemies[3].fighter == 600);

    /* encounters: row numbers, the pointers written back, a range and a flag refused, the empty first enemy and encounter 0 refused whole */
    log = fieldStart(0, NULL,
        "{\"format\":\"field_encounters-v1\",\"rows\":{\"1\":{\"enemy1\":5,\"enemy2\":6,\"ambushChance\":50,\"kind\":3,\"runAway\":1,\"poison\":0},"
        "\"2\":{\"enemy1\":0,\"kind\":4},\"3\":{\"enemy1\":523,\"enemy3\":7},\"4\":{\"poison\":2,\"steal\":\"+1\"},\"0\":{\"kind\":4},"
        "\"335\":{\"kind\":1}}}",
        NULL);
    CHECK(strstr(log, "row \"3\" field \"enemy1\" rejected") && strstr(log, "row \"4\" field \"poison\" rejected"));
    CHECK(strstr(log, "row \"335\" rejected: no row with that key"));
    free(log);
    logBegin();
    gameTablesApplyField();
    log = logEnd();
    CHECK(strstr(log, "field_encounters row 2 rejected: the first enemy cannot be empty"));
    CHECK(strstr(log, "field_encounters row 0 rejected: encounter 0 is the empty one"));
    free(log);
    CHECK(FIELDSTG_encounters[1].enemies[0] == &FIELDSTG_enemies[5] && FIELDSTG_encounters[1].enemies[1] == &FIELDSTG_enemies[6]);
    CHECK(FIELDSTG_encounters[1].enemies[2] == &FIELDSTG_enemies[0] && FIELDSTG_encounters[1].ambushChance == 50 && FIELDSTG_encounters[1].kind == 3);
    CHECK(FIELDSTG_encounters[1].blocks[11] == 1 && FIELDSTG_encounters[1].blocks[0] == 0 && FIELDSTG_encounters[1].blocks[3] == 0);
    CHECK(FIELDSTG_encounters[2].enemies[0] == &FIELDSTG_enemies[3] && FIELDSTG_encounters[2].kind == 3); /* the whole row stays as it was */
    CHECK(FIELDSTG_encounters[3].enemies[0] == &FIELDSTG_enemies[4] && FIELDSTG_encounters[3].enemies[2] == &FIELDSTG_enemies[7]);
    CHECK(FIELDSTG_encounters[4].blocks[5] == 1 && FIELDSTG_encounters[4].blocks[7] == 1 && FIELDSTG_encounters[0].kind == 0); /* "+1" on a flag stops at 1 */
    for (i = 5; i < 335; i++) {
        CHECK(!memcmp(&FIELDSTG_encounters[i], &enc0[i], sizeof enc0[i]));
    }
    CHECK(!memcmp(en0, FIELDSTG_enemies, sizeof en0)); /* the enemy rows are not this table's */
    fieldReset();
    gameTablesApplyField();
    CHECK(FIELDSTG_encounters[1].enemies[1] == &FIELDSTG_enemies[6] && FIELDSTG_encounters[1].kind == 3);

    /* enemies and encounters together: the encounter points at the patched row */
    log = fieldStart(0, "{\"format\":\"field_enemies-v1\",\"rows\":{\"5\":{\"fighter\":250,\"hp\":999}}}",
        "{\"format\":\"field_encounters-v1\",\"rows\":{\"1\":{\"enemy1\":5}}}", NULL);
    free(log);
    gameTablesApplyField();
    CHECK(FIELDSTG_encounters[1].enemies[0]->fighter == 250 && FIELDSTG_encounters[1].enemies[0]->hp == 999);
    CHECK(FIELDSTG_encounters[4].enemies[0] == &FIELDSTG_enemies[5] && FIELDSTG_encounters[4].enemies[0]->fighter == 250); /* the row is shared */

    /* a row keyed by a name ("<mod>:<name>") is a new encounter of the mods' (field_ext.c), not a row of this table: skipped without a line, the rest of the file applies */
    log = fieldStart(0, NULL, "{\"format\":\"field_encounters-v1\",\"rows\":{\"m:new\":{\"enemies\":[]},\"1\":{\"kind\":2},\"*:x\":{\"kind\":9}}}", NULL);
    CHECK(!strstr(log, "m:new") && strstr(log, "\"*:x\"") == NULL);
    free(log);
    gameTablesApplyField();
    CHECK(FIELDSTG_encounters[1].kind == 2);
    CHECK(gameTablesEncounterCount() == 335);

    /* battle rates: a number, an operation that stops at the range, a negative and an over-range value refused */
    log = fieldStart(0, NULL, NULL,
        "{\"format\":\"battle_rates-v1\",\"rows\":{\"1\":{\"rate\":5},\"2\":{\"rate\":-1},\"3\":{\"rate\":2001},\"0\":{\"rate\":\"*2\"},\"4\":{\"rate\":0},"
        "\"6\":{\"rate\":1}}}");
    CHECK(strstr(log, "row \"2\" field \"rate\" rejected") && strstr(log, "row \"3\" field \"rate\" rejected"));
    CHECK(strstr(log, "row \"6\" rejected: no row with that key"));
    free(log);
    gameTablesApplyField();
    CHECK(FIELDSTG_battleRates[0] == 2000 && FIELDSTG_battleRates[1] == 5 && FIELDSTG_battleRates[2] == 4);
    CHECK(FIELDSTG_battleRates[3] == 6 && FIELDSTG_battleRates[4] == 0 && FIELDSTG_battleRates[5] == 18);
    CHECK(!memcmp(en0, FIELDSTG_enemies, sizeof en0) && !memcmp(enc0, FIELDSTG_encounters, sizeof enc0));
}

/* ---- game_tables.c (E5d): the card shop's prices, stock and packs and the card battle's effects, prizes and default deck ---- */

GameCardPrice STCRDSHP_prices[45];
GameCardStock STCRDSHP_stocks[25];
GameCardPack STCRDSHP_packs[36];
GameCardEffect CARDGAME_cardEffects[60];
s16 CARDGAME_prizeItems[36];
u16 CARDGAME_defaultDeck[80];
static short gShopCards[24][8];
static int gPackSlots[210][16];

int itemExtIdOk(long long id) { return (id >= 1 && id <= 402) || id == 500; }

static const char *fakeCardName(int id)
{
    static char buf[40];

    snprintf(buf, sizeof buf, "Card %d", id);
    return buf;
}

static const char *fakeItemName(int id)
{
    static char buf[40];

    snprintf(buf, sizeof buf, "Item %d", id);
    return buf;
}

static void cardReset(void)
{
    int i, k;

    memset(STCRDSHP_prices, 0, sizeof STCRDSHP_prices);
    for (i = 0; i < 44; i++) {
        STCRDSHP_prices[i].card = (short)(10 + 5 * i);
        STCRDSHP_prices[i].price = (short)(100 * (i + 1));
    }
    STCRDSHP_prices[44].price = 1;
    memset(gShopCards, 0, sizeof gShopCards);
    memset(STCRDSHP_stocks, 0, sizeof STCRDSHP_stocks);
    for (i = 0; i < 24; i++) {
        for (k = 0; k < 6; k++) gShopCards[i][k] = (short)(1 + (i * 6 + k) % 300);
        STCRDSHP_stocks[i].shop = i < 19 ? 49 + i : 51 + i;
        STCRDSHP_stocks[i].cards = gShopCards[i];
    }
    STCRDSHP_stocks[24].shop = -1;
    memset(STCRDSHP_packs, 0, sizeof STCRDSHP_packs);
    for (i = 0; i < 35; i++) {
        STCRDSHP_packs[i].item = i == 0 ? 91 : 360 + i;
        for (k = 0; k < 6; k++) {
            int c;

            for (c = 0; c < 16; c++) gPackSlots[i * 6 + k][c] = 1 + ((i * 6 + k) * 16 + c) % 314;
            STCRDSHP_packs[i].slots[k] = gPackSlots[i * 6 + k];
        }
    }
    memset(CARDGAME_cardEffects, 0, sizeof CARDGAME_cardEffects);
    for (i = 0; i < 60; i++) {
        GameCardEffect *e = &CARDGAME_cardEffects[i];

        e->condition = (unsigned char)(0x70 + i % 20);
        e->playCondition = (unsigned char)(i % 11);
        e->message = (unsigned char)(i % 2 ? 0x2F : 0);
        e->target = (unsigned char)(i % 10);
        for (k = 0; k < 5; k++) e->script[k] = (unsigned char)(1 + (i + k) % 174);
    }
    for (i = 0; i < 36; i++) CARDGAME_prizeItems[i] = (short)(91 + i);
    for (i = 0; i < 80; i++) CARDGAME_defaultDeck[i] = (u16)((i * 7) % 314);
}

/* a patch row that fills every step of a card's script with `v` (the script then has no 0) */
static void fullScript(char *out, size_t cap, int v)
{
    int k;
    size_t n = 0;

    for (k = 1; k <= 40; k++) n += (size_t)snprintf(out + n, cap - n, "%s\"step%d\":%d", k > 1 ? "," : "", k, v);
}

static char *cardStart(int withData, const char *prices, const char *stock, const char *packs, const char *effects, const char *prizes, const char *deck)
{
    char m[1300], d[1300];

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    if (prices) put("mods/a/tables/card_prices.json", prices);
    if (stock) put("mods/a/tables/card_shop_stock.json", stock);
    if (packs) put("mods/a/tables/card_pack_slots.json", packs);
    if (effects) put("mods/a/tables/card_effects.json", effects);
    if (prizes) put("mods/a/tables/prize_items.json", prizes);
    if (deck) put("mods/a/tables/default_deck.json", deck);
    snprintf(m, sizeof m, "%s/mods", gRoot);
    snprintf(d, sizeof d, "%s/data", gRoot);
    cardReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(withData ? d : NULL);
    gameTablesSetCardNames(fakeCardName);
    gameTablesSetItemNames(fakeItemName);
    CHECK(gameTablesRegisterCardShop() == 3);
    CHECK(gameTablesRegisterCardGame() == 3);
    return logEnd();
}

/* modding-ux U8a: card_opponents, a positional file-backed table (CARD_NPC.BIN: 170 rows of 0xD0 bytes, no key, the last sector padded) */
#define OPP_ROWS 170
#define OPP_BYTES (OPP_ROWS * 0xD0)
#define OPP_FILE (18 * 2048)

static unsigned char gOppDisc[OPP_FILE];

static unsigned char *oppRow(unsigned char *buf, int r)
{
    return buf + (size_t)r * 0xD0;
}

static void oppSet16(unsigned char *row, size_t off, int v)
{
    row[off] = (unsigned char)(v & 255);
    row[off + 1] = (unsigned char)((v >> 8) & 255);
}

static void oppFill(void)
{
    int r, i;

    memset(gOppDisc, 0, sizeof gOppDisc);
    for (r = 0; r < OPP_ROWS; r++) {
        unsigned char *row = oppRow(gOppDisc, r);

        for (i = 0; i < 40; i++) {
            oppSet16(row, (size_t)i * 4, ((r * 3 + i) % 314 + 1) | (i % 5 == 0 ? 0x8000 : 0));
            row[i * 4 + 2] = (unsigned char)(i < 36 ? 1 + i / 6 : 7);
            row[i * 4 + 3] = (unsigned char)(1 + i % 5);
        }
        for (i = 0; i < 4; i++) oppSet16(row, 0xA0 + (size_t)i * 2, (r + i) % 314 + 1);
        row[0xC8] = (unsigned char)(4 + r % 80);
        row[0xCC] = (unsigned char)(r % 36);
    }
}

static char *oppStart(int withData, const char *patch, int *rc)
{
    char m[1300], d[1300];

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    if (patch) put("mods/a/tables/card_opponents.json", patch);
    snprintf(m, sizeof m, "%s/mods", gRoot);
    snprintf(d, sizeof d, "%s/data", gRoot);
    oppFill();
    logBegin();
    modsInit(m, NULL);
    tmInit(withData ? d : NULL);
    *rc = gameTablesRegisterCardOpponents(gOppDisc, sizeof gOppDisc);
    return logEnd();
}

static void testCardOpponents(void)
{
    static unsigned char buf[OPP_FILE], buf2[OPP_FILE];
    char *log, *ref, *big = (char *)malloc(16000);
    size_t n;
    int rc, i;

    CHECK(sizeof(GameCardOpponent) == 0xD0 && offsetof(GameCardOpponent, counterCards) == 0xA0 && offsetof(GameCardOpponent, level) == 0xC8 &&
          offsetof(GameCardOpponent, prize) == 0xCC);

    /* no mods: registered with 170 rows and 182 fields, nothing is written, the reference holds the file's values */
    free(oppStart(1, NULL, &rc));
    CHECK(rc == 0 && tmTableCount() == 1);
    memcpy(buf, gOppDisc, sizeof buf);
    CHECK(gameTablesPatchCardOpponents(buf, sizeof buf) == 0 && !memcmp(buf, gOppDisc, sizeof buf));
    CHECK(tmPatchBuffer("card_opponents", buf, sizeof buf) == 0 && tmPatchCount("card_opponents") == 0);
    ref = slurp("data/tables/card_opponents.json");
    CHECK(ref && strstr(ref, "\"format\": \"card_opponents-v1\"") && strstr(ref, "\"0\": { \"card1\": 32769, \"flagged1\": 1, \"group1\": 1, \"kind1\": 1,") == NULL);
    CHECK(ref && strstr(ref, "\"0\": { \"card1\": 1, \"flagged1\": 1, \"group1\": 1, \"kind1\": 1, \"card2\": 2, \"flagged2\": 0,"));
    CHECK(ref && strstr(ref, "\"counter1\": 1, \"counter2\": 2, \"counter3\": 3, \"counter4\": 4, \"counter5\": 0,") && strstr(ref, "\"level\": 4, \"prize\": 0 },"));
    CHECK(ref && strstr(ref, "\"169\": { \"card1\": 194,") && !strstr(ref, "\"170\": {") && !strstr(ref, "\"labels\""));
    CHECK(ref && strstr(ref, "Valid: 1 to 1023.") && strstr(ref, "Valid: 0 to 35.") && strstr(ref, "Valid: 1 to 7."));
    free(ref);

    /* the patch: by index, the card id and the flag bit apart, ranges, a row without a key, whole rows refused */
    n = (size_t)snprintf(big, 16000,
        "{\"format\":\"card_opponents-v1\",\"rows\":{"
        "\"0\":{\"card1\":7,\"group2\":3,\"kind40\":7,\"counter1\":9,\"level\":50,\"prize\":35},"
        "\"1\":{\"flagged1\":1,\"card2\":\"+5\",\"flagged3\":0},"
        "\"2\":{\"flagged1\":0},"
        "\"169\":{\"level\":99},"
        "\"3\":{\"card1\":315,\"card2\":0,\"level\":100,\"prize\":36,\"group1\":8,\"kind1\":8,\"counter1\":315,\"flagged1\":2},"
        "\"170\":{\"level\":1},\"-1\":{\"level\":1},"
        "\"4\":{\"level\":\"*2\"},"
        "\"5\":{");
    for (i = 5; i <= 13; i++) n += (size_t)snprintf(big + n, 16000 - n, "%s\"counter%d\":1", i > 5 ? "," : "", i);
    n += (size_t)snprintf(big + n, 16000 - n, ",\"counter1\":1,\"counter2\":1,\"counter3\":1,\"counter4\":1,\"level\":77},\"6\":{");
    for (i = 1; i <= 40; i++) n += (size_t)snprintf(big + n, 16000 - n, "%s\"group%d\":7", i > 1 ? "," : "", i);
    n += (size_t)snprintf(big + n, 16000 - n, ",\"level\":88},\"7\":{\"counter1\":0,\"counter2\":0,\"level\":66},\"8\":{\"counter1\":1,\"counter2\":1,\"counter3\":1,\"counter4\":1,"
        "\"counter5\":1,\"counter6\":1,\"counter7\":1,\"counter8\":1,\"counter9\":1,\"counter10\":1,\"counter11\":1,\"counter12\":1,\"level\":55}}}");
    log = oppStart(0, big, &rc);
    CHECK(rc == 0 && tmPatchCount("card_opponents") == 1);
    CHECK(strstr(log, "row \"3\" field \"card1\" rejected") && strstr(log, "row \"3\" field \"card2\" rejected") && strstr(log, "row \"3\" field \"level\" rejected"));
    CHECK(strstr(log, "row \"3\" field \"prize\" rejected") && strstr(log, "row \"3\" field \"group1\" rejected") && strstr(log, "row \"3\" field \"kind1\" rejected"));
    CHECK(strstr(log, "row \"3\" field \"counter1\" rejected") && strstr(log, "row \"3\" field \"flagged1\" rejected"));
    CHECK(strstr(log, "row \"170\" rejected: no row with that key") && strstr(log, "row \"-1\" rejected: no row with that key"));
    free(log);
    memcpy(buf, gOppDisc, sizeof buf);
    logBegin();
    rc = gameTablesPatchCardOpponents(buf, sizeof buf);
    log = logEnd();
    CHECK(strstr(log, "card_opponents row 5 rejected: its counter list has no 0 among the first 13 places"));
    CHECK(strstr(log, "card_opponents row 6 rejected: all its deck is in group 7"));
    CHECK(!strstr(log, "card_opponents row 7 rejected") && !strstr(log, "card_opponents row 8 rejected"));
    CHECK(strstr(log, "card_opponents patched by mods/a (") != NULL);
    free(log);
    /* rows 0, 1, 2, 4, 7, 8, 169 changed; 3 (every field refused), 5 and 6 (whole row refused) are as the file had them */
    CHECK(rc == 7 && !memcmp(oppRow(buf, 3), oppRow(gOppDisc, 3), 0xD0));
    {
        unsigned char *r0 = oppRow(buf, 0), *o0 = oppRow(gOppDisc, 0);

        CHECK(r0[0] == 7 && r0[1] == 0x80); /* the card id changed, the flag the disc set stays */
        CHECK(r0[4] == o0[4] && r0[5] == o0[5]);
        CHECK(r0[4 + 2] == 3 && r0[39 * 4 + 3] == 7 && r0[0xA0] == 9 && r0[0xA1] == 0 && r0[0xC8] == 50 && r0[0xCC] == 35 && r0[0xCD] == 0);
        CHECK(r0[2] == o0[2] && r0[3] == o0[3] && r0[0xA2] == o0[0xA2]);
    }
    {
        unsigned char *r1 = oppRow(buf, 1), *o1 = oppRow(gOppDisc, 1);

        /* row 1 place 1 had the flag; place 3 had none; "flagged1": 1 keeps it, "flagged3": 0 keeps none */
        CHECK(r1[0] == o1[0] && r1[1] == o1[1] && (r1[1] & 0x80));
        CHECK(r1[4] == (unsigned char)(o1[4] + 5) && r1[5] == o1[5]);
        CHECK(r1[8] == o1[8] && r1[9] == o1[9]);
    }
    CHECK(oppRow(buf, 2)[1] == (oppRow(gOppDisc, 2)[1] & 0x7F) && oppRow(buf, 2)[0] == oppRow(gOppDisc, 2)[0]); /* the flag cleared, the id kept */
    CHECK(oppRow(buf, 169)[0xC8] == 99);
    CHECK(oppRow(buf, 4)[0xC8] == 16);
    CHECK(!memcmp(oppRow(buf, 5), oppRow(gOppDisc, 5), 0xD0) && !memcmp(oppRow(buf, 6), oppRow(gOppDisc, 6), 0xD0));
    CHECK(oppRow(buf, 7)[0xA0] == 0 && oppRow(buf, 7)[0xC8] == 66 && oppRow(buf, 8)[0xA0 + 22] == 1 && oppRow(buf, 8)[0xA0 + 24] == 0 && oppRow(buf, 8)[0xC8] == 55);
    CHECK(!memcmp(oppRow(buf, 10), oppRow(gOppDisc, 10), 0xD0 * 159)); /* the untouched rows, down to 168 */
    CHECK(!memcmp(buf + OPP_BYTES, gOppDisc + OPP_BYTES, OPP_FILE - OPP_BYTES)); /* the padding of the last sector */
    /* applying again to a fresh read gives the same bytes, and to an already patched one does not add up */
    memcpy(buf2, gOppDisc, sizeof buf2);
    CHECK(gameTablesPatchCardOpponents(buf2, sizeof buf2) == rc && !memcmp(buf, buf2, sizeof buf));
    memcpy(buf2, gOppDisc, sizeof buf2);
    gameTablesPatchCardOpponents(buf2, sizeof buf2);
    CHECK(!memcmp(buf, buf2, sizeof buf));
    CHECK(tmApply("card_opponents") == -1); /* a file's table is never applied in memory */

    /* U8c: a card a mod added, by name or id, in the deck places and the counter list; a name nobody defines and the ids 315-316 are refused (0 ends counters) */
    log = oppStart(0,
        "{\"format\":\"card_opponents-v1\",\"rows\":{"
        "\"0\":{\"card1\":\"m:card\",\"card2\":\"m:bad\",\"card3\":316,\"card4\":318,\"card5\":\"dw3:card/9\",\"counter1\":\"m:other\",\"counter2\":316,"
        "\"counter3\":\"m:bad\",\"counter4\":0},"
        "\"1\":{\"card1\":\"+3\",\"counter1\":\"-1\",\"counter2\":\"+300\"}}}", &rc);
    CHECK(rc == 0 && tmPatchCount("card_opponents") == 1);
    CHECK(strstr(log, "row \"0\" field \"card2\" rejected: no installed mod defines \"m:bad\""));
    CHECK(strstr(log, "row \"0\" field \"card3\" rejected") && strstr(log, "row \"0\" field \"counter2\" rejected") && strstr(log, "row \"0\" field \"counter3\" rejected"));
    CHECK(!strstr(log, "row \"0\" field \"card1\" rejected") && !strstr(log, "row \"0\" field \"card4\" rejected") && !strstr(log, "row \"0\" field \"counter4\" rejected"));
    free(log);
    memcpy(buf, gOppDisc, sizeof buf);
    CHECK(gameTablesPatchCardOpponents(buf, sizeof buf) == 2);
    {
        unsigned char *r0 = oppRow(buf, 0), *o0 = oppRow(gOppDisc, 0);

        CHECK(r0[0] == (317 & 255) && (r0[1] & 0x0F) == (317 >> 8) && (r0[1] & 0x80) == (o0[1] & 0x80)); /* the id in the low 12 bits, the flag kept */
        CHECK(r0[4] == o0[4] && r0[8] == o0[8]);           /* card2 and card3 refused: unchanged */
        CHECK(r0[12] == (318 & 255) && (r0[13] & 0x0F) == 1 && r0[16] == 9);
        CHECK(r0[0xA0] == (318 & 255) && r0[0xA1] == 1 && r0[0xA2] == o0[0xA2] && r0[0xA4] == o0[0xA4] && r0[0xA6] == 0 && r0[0xA7] == 0);
    }
    free(big);
}

static void testCardTables(void)
{
    static GameCardPrice price0[45];
    static GameCardStock stock0[25];
    static int slots0[210][16];
    static GameCardEffect eff0[60];
    static short prize0[36];
    static u16 deck0[80];
    static short stockCards0[24][8];
    char *log, *ref, big[2400], patch[3000];
    int i;

    /* no mods: registered, the game's memory is never written, and the layouts the offsets rely on */
    free(cardStart(1, NULL, NULL, NULL, NULL, NULL, NULL));
    memcpy(price0, STCRDSHP_prices, sizeof price0);
    memcpy(stock0, STCRDSHP_stocks, sizeof stock0);
    memcpy(slots0, gPackSlots, sizeof slots0);
    memcpy(eff0, CARDGAME_cardEffects, sizeof eff0);
    memcpy(prize0, CARDGAME_prizeItems, sizeof prize0);
    memcpy(deck0, CARDGAME_defaultDeck, sizeof deck0);
    memcpy(stockCards0, gShopCards, sizeof stockCards0);
    gameTablesApplyCardShop();
    gameTablesApplyCardGame();
    CHECK(!memcmp(price0, STCRDSHP_prices, sizeof price0) && !memcmp(stock0, STCRDSHP_stocks, sizeof stock0) && !memcmp(slots0, gPackSlots, sizeof slots0));
    CHECK(!memcmp(eff0, CARDGAME_cardEffects, sizeof eff0) && !memcmp(prize0, CARDGAME_prizeItems, sizeof prize0) && !memcmp(deck0, CARDGAME_defaultDeck, sizeof deck0));
    CHECK(tmPatchCount("card_prices") == 0 && tmPatchCount("card_shop_stock") == 0 && tmPatchCount("card_pack_slots") == 0);
    CHECK(tmPatchCount("card_effects") == 0 && tmPatchCount("prize_items") == 0 && tmPatchCount("default_deck") == 0);
    CHECK(sizeof(GameCardEffect) == 44 && offsetof(GameCardEffect, script) == 4 && sizeof(GameCardPrice) == 4);
    CHECK(gameTableCardPrices()->rows == 44 && gameTableCardShopStock()->rows == 24 && gameTableCardPackSlots()->rows == 210);
    CHECK(gameTableCardEffects()->rows == 60 && gameTableCardEffects()->nfields == 44 && gameTablePrizeItems()->rows == 36 && gameTableDefaultDeck()->rows == 40);
    CHECK(gameTableCardPackSlots()->nfields == 16);

    /* the reference exports: keys, labels and the ranges */
    ref = slurp("data/tables/card_prices.json");
    CHECK(ref && strstr(ref, "\"10\": { \"price\": 100 }") && strstr(ref, "\"225\": { \"price\": 4400 }") && !strstr(ref, "\"230\": {"));
    CHECK(ref && strstr(ref, "\"labels\": {") && strstr(ref, "\"10\": \"Card 10\"") && strstr(ref, "Valid: 0 to 32767."));
    free(ref);
    ref = slurp("data/tables/card_shop_stock.json");
    CHECK(ref && strstr(ref, "\"49\": { \"cards\": [1, 2, 3, 4, 5, 6] }") && strstr(ref, "\"74\": {") && !strstr(ref, "\"75\": {"));
    CHECK(ref && strstr(ref, "\"49\": \"Card 1, Card 2, Card 3, Card 4, Card 5, Card 6\"") && strstr(ref, "A list of 1 up to 29 numbers, each valid from 1 to 1023"));
    free(ref);
    ref = slurp("data/tables/card_pack_slots.json");
    CHECK(ref && strstr(ref, "\"910\": { \"card1\": 1, \"card2\": 2") && strstr(ref, "\"3615\": {") && strstr(ref, "\"3945\": {") && !strstr(ref, "\"3946\": {"));
    CHECK(ref && strstr(ref, "\"910\": \"Item 91 slot 1\"") && strstr(ref, "\"3612\": \"Item 361 slot 3\""));
    free(ref);
    ref = slurp("data/tables/card_effects.json");
    CHECK(ref && strstr(ref, "\"60\": {") && !strstr(ref, "\"61\": {") && strstr(ref, "\"step40\": 0") && strstr(ref, "\"1\": \"Card 1\""));
    CHECK(ref && strstr(ref, "\"condition\": 112, \"playCondition\": 0, \"message\": 0, \"target\": 0, \"step1\": 1, \"step2\": 2"));
    free(ref);
    ref = slurp("data/tables/prize_items.json");
    CHECK(ref && strstr(ref, "\"0\": { \"item\": 91 }") && strstr(ref, "\"35\": { \"item\": 126 }") && strstr(ref, "\"0\": \"Item 91\""));
    free(ref);
    ref = slurp("data/tables/default_deck.json");
    CHECK(ref && strstr(ref, "\"39\": { \"card\": 273 }") && !strstr(ref, "\"40\": {") && strstr(ref, "\"1\": \"Card 8\"") && strstr(ref, "Valid: 0 to 1022."));
    free(ref);

    /* prices: a number, an operation, the range and a card with no row refused (the key is the card id, there is no `card` field) */
    log = cardStart(0,
        "{\"format\":\"card_prices-v1\",\"rows\":{\"10\":{\"price\":5},\"15\":{\"price\":\"*2\"},\"20\":{\"price\":32768},\"25\":{\"price\":-1},"
        "\"11\":{\"price\":3},\"30\":{\"card\":99},\"35\":{\"price\":0}}}",
        NULL, NULL, NULL, NULL, NULL);
    CHECK(strstr(log, "row \"20\" field \"price\" rejected") && strstr(log, "row \"25\" field \"price\" rejected"));
    CHECK(strstr(log, "row \"11\" rejected: no row with that key") && strstr(log, "row \"30\" field \"card\" rejected"));
    free(log);
    CHECK(tmApply("card_prices") == 3);
    CHECK(STCRDSHP_prices[0].price == 5 && STCRDSHP_prices[1].price == 400 && STCRDSHP_prices[2].price == 300 && STCRDSHP_prices[3].price == 400);
    CHECK(STCRDSHP_prices[5].price == 0 && STCRDSHP_prices[4].card == 30 && STCRDSHP_prices[44].card == 0 && STCRDSHP_prices[44].price == 1);
    cardReset(); /* the overlay restored */
    gameTablesApplyCardShop();
    CHECK(STCRDSHP_prices[0].price == 5 && STCRDSHP_prices[1].price == 400 && STCRDSHP_prices[5].price == 0);
    CHECK(!memcmp(stock0, STCRDSHP_stocks, sizeof stock0) && !memcmp(slots0, gPackSlots, sizeof slots0));

    /* stock: a list replaces, add/remove edit; an id out of range, an empty list and a list that is too long are refused; the buffer is zeroed past the 0 */
    for (i = 0, big[0] = '\0'; i < 30; i++) snprintf(big + strlen(big), sizeof big - strlen(big), "%s%d", i ? "," : "", 1 + i);
    snprintf(patch, sizeof patch,
        "{\"format\":\"card_shop_stock-v1\",\"rows\":{\"49\":{\"cards\":[7,8,9,10,11,12,13]},\"50\":{\"cards\":{\"add\":[200],\"remove\":[7]}},"
        "\"51\":{\"cards\":[315]},\"52\":{\"cards\":[]},\"53\":{\"cards\":[%s]},\"54\":{\"cards\":[0]},\"55\":{\"cards\":[3]},\"56\":{\"cards\":[]},"
        "\"68\":{\"cards\":[1]},\"75\":{\"cards\":[1]},\"70\":{\"cards\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29]}}}",
        big);
    log = cardStart(0, NULL, patch, NULL, NULL, NULL, NULL);
    CHECK(strstr(log, "row \"51\" field \"cards\" rejected") && strstr(log, "row \"53\" field \"cards\" rejected"));
    CHECK(strstr(log, "row \"54\" field \"cards\" rejected") && strstr(log, "row \"75\" rejected: no row with that key"));
    free(log);
    gameTablesApplyCardShop();
    CHECK(STCRDSHP_stocks[0].cards != gShopCards[0] && STCRDSHP_stocks[0].cards[0] == 7 && STCRDSHP_stocks[0].cards[6] == 13 && STCRDSHP_stocks[0].cards[7] == 0);
    CHECK(STCRDSHP_stocks[0].cards[8] == 0 && STCRDSHP_stocks[0].cards[29] == 0); /* a partial page reads these: empty, never stale memory */
    CHECK(STCRDSHP_stocks[1].cards[0] == 8 && STCRDSHP_stocks[1].cards[1] == 1 + (1 * 6 + 2) % 300 && STCRDSHP_stocks[1].cards[5] == 200 && STCRDSHP_stocks[1].cards[6] == 0);
    CHECK(STCRDSHP_stocks[2].cards == gShopCards[2] && STCRDSHP_stocks[3].cards == gShopCards[3] && STCRDSHP_stocks[4].cards == gShopCards[4]);
    CHECK(STCRDSHP_stocks[5].cards[0] == 1 + (5 * 6) % 300 && STCRDSHP_stocks[18].cards[0] == 1 + (18 * 6) % 300); /* shop 54 "[0]" refused, shop 68 has no row */
    CHECK(STCRDSHP_stocks[19].shop == 70 && STCRDSHP_stocks[19].cards[28] == 29 && STCRDSHP_stocks[19].cards[29] == 0);
    CHECK(!memcmp(gShopCards, stockCards0, sizeof gShopCards)); /* the game's own lists are never written */
    cardReset();
    gameTablesApplyCardShop();
    CHECK(STCRDSHP_stocks[0].cards[6] == 13 && STCRDSHP_stocks[1].cards[5] == 200 && STCRDSHP_stocks[19].cards[28] == 29); /* applying again: the same */

    /* pack slots: written through the packs' pointers, a card out of range refused, a key with no row refused */
    log = cardStart(0, NULL, NULL,
        "{\"format\":\"card_pack_slots-v1\",\"rows\":{\"910\":{\"card1\":5,\"card16\":314},\"3615\":{\"card3\":0,\"card4\":315,\"card2\":\"+1\"},"
        "\"3616\":{\"card1\":1},\"900\":{\"card1\":1},\"3610\":{\"card2\":\"*2\"},\"3945\":{\"card16\":2}}}",
        NULL, NULL, NULL);
    CHECK(strstr(log, "row \"3615\" field \"card3\" rejected") && strstr(log, "row \"3615\" field \"card4\" rejected"));
    CHECK(strstr(log, "row \"3616\" rejected: no row with that key") && strstr(log, "row \"900\" rejected: no row with that key"));
    free(log);
    gameTablesApplyCardShop();
    CHECK(gPackSlots[0][0] == 5 && gPackSlots[0][15] == 314 && gPackSlots[0][1] == slots0[0][1]);
    CHECK(gPackSlots[6][1] == slots0[6][1] * 2 && gPackSlots[6][0] == slots0[6][0]);
    CHECK(gPackSlots[11][1] == slots0[11][1] + 1 && gPackSlots[11][2] == slots0[11][2] && gPackSlots[11][3] == slots0[11][3]);
    CHECK(gPackSlots[209][15] == 2);
    for (i = 12; i < 209; i++) CHECK(!memcmp(gPackSlots[i], slots0[i], sizeof gPackSlots[i]));
    CHECK(!memcmp(price0, STCRDSHP_prices, sizeof price0) && !memcmp(eff0, CARDGAME_cardEffects, sizeof eff0));

    /* effects: head fields and steps; a script with no 0 refuses the whole row; message, target and step ranges */
    snprintf(big, sizeof big, "{\"format\":\"card_effects-v1\",\"rows\":{\"1\":{\"condition\":128,\"step2\":5,\"playCondition\":3,\"message\":58,\"target\":4},"
        "\"2\":{");
    fullScript(big + strlen(big), sizeof big - strlen(big), 9);
    snprintf(big + strlen(big), sizeof big - strlen(big), ",\"target\":3},\"3\":{\"message\":167,\"target\":10,\"step1\":175,\"step3\":0},\"61\":{\"target\":1},"
        "\"0\":{\"target\":1},\"4\":{\"step5\":\"+3\",\"step6\":\"*2\"}}}");
    log = cardStart(0, NULL, NULL, NULL, big, NULL, NULL);
    CHECK(strstr(log, "row \"3\" field \"message\" rejected") && strstr(log, "row \"3\" field \"target\" rejected") && strstr(log, "row \"3\" field \"step1\" rejected"));
    CHECK(strstr(log, "row \"61\" rejected: no row with that key") && strstr(log, "row \"0\" rejected: no row with that key"));
    free(log);
    logBegin();
    gameTablesApplyCardGame();
    log = logEnd();
    CHECK(strstr(log, "card_effects row 2 rejected: its script has no 0 step to end it"));
    free(log);
    CHECK(CARDGAME_cardEffects[0].condition == 128 && CARDGAME_cardEffects[0].script[1] == 5 && CARDGAME_cardEffects[0].playCondition == 3);
    CHECK(CARDGAME_cardEffects[0].message == 58 && CARDGAME_cardEffects[0].target == 4 && CARDGAME_cardEffects[0].script[0] == eff0[0].script[0]);
    CHECK(!memcmp(&CARDGAME_cardEffects[1], &eff0[1], sizeof eff0[1])); /* the unterminated script: the row keeps what it had, target 3 too */
    CHECK(CARDGAME_cardEffects[2].script[2] == 0 && CARDGAME_cardEffects[2].script[0] == eff0[2].script[0] && CARDGAME_cardEffects[2].message == eff0[2].message);
    CHECK(CARDGAME_cardEffects[3].script[4] == eff0[3].script[4] + 3 && CARDGAME_cardEffects[3].script[5] == 0);
    for (i = 4; i < 60; i++) CHECK(!memcmp(&CARDGAME_cardEffects[i], &eff0[i], sizeof eff0[i]));
    cardReset();
    gameTablesApplyCardGame();
    CHECK(CARDGAME_cardEffects[0].script[1] == 5 && !memcmp(&CARDGAME_cardEffects[1], &eff0[1], sizeof eff0[1]));

    /* prizes: an item id or a mod item by name, 0 and an unknown name refused; the deck: card id minus one, 0-313 */
    log = cardStart(0, NULL, NULL, NULL, NULL,
        "{\"format\":\"prize_items-v1\",\"rows\":{\"0\":{\"item\":95},\"1\":{\"item\":\"m:gun\"},\"2\":{\"item\":0},\"3\":{\"item\":\"m:nothing\"},"
        "\"4\":{\"item\":403},\"5\":{\"item\":\"+10\"},\"36\":{\"item\":5}}}",
        "{\"format\":\"default_deck-v1\",\"rows\":{\"0\":{\"card\":313},\"1\":{\"card\":314},\"2\":{\"card\":\"+1\"},\"3\":{\"card\":-1},\"39\":{\"card\":0},\"40\":{\"card\":1}}}");
    CHECK(strstr(log, "row \"2\" field \"item\" rejected") && strstr(log, "row \"3\" field \"item\" rejected") && strstr(log, "row \"4\" field \"item\" rejected"));
    CHECK(strstr(log, "row \"36\" rejected: no row with that key") && strstr(log, "row \"1\" field \"card\" rejected") && strstr(log, "row \"3\" field \"card\" rejected"));
    CHECK(strstr(log, "row \"40\" rejected: no row with that key"));
    free(log);
    gameTablesApplyCardGame();
    CHECK(CARDGAME_prizeItems[0] == 95 && CARDGAME_prizeItems[1] == 500 && CARDGAME_prizeItems[2] == prize0[2] && CARDGAME_prizeItems[3] == prize0[3]);
    CHECK(CARDGAME_prizeItems[4] == prize0[4] && CARDGAME_prizeItems[5] == prize0[5] + 10 && CARDGAME_prizeItems[35] == prize0[35]); /* a "+k" is kept as it is: 96+10 is item 106 */
    CHECK(CARDGAME_defaultDeck[0] == 313 && CARDGAME_defaultDeck[1] == deck0[1] && CARDGAME_defaultDeck[2] == deck0[2] + 1 && CARDGAME_defaultDeck[3] == deck0[3]);
    CHECK(CARDGAME_defaultDeck[39] == 0 && CARDGAME_defaultDeck[40] == deck0[40] && CARDGAME_defaultDeck[79] == deck0[79]); /* the second 40 are not the table's */
    CHECK(!memcmp(eff0, CARDGAME_cardEffects, sizeof eff0));

    /* U8c: a card a mod added (m:card = 317, m:other = 318) by name or id in the stock, the pack slots and the deck; a name nobody defines and 315-316 are
       refused; a price row for a mod card does not exist (its price is its card.json's) */
    log = cardStart(0,
        "{\"format\":\"card_prices-v1\",\"rows\":{\"10\":{\"price\":7},\"317\":{\"price\":9},\"m:card\":{\"price\":9}}}",
        "{\"format\":\"card_shop_stock-v1\",\"rows\":{\"49\":{\"cards\":[\"m:card\",\"dw3:card/7\",318,12]},\"50\":{\"cards\":[\"m:bad\",3]},"
        "\"51\":{\"cards\":[3,316]},\"52\":{\"cards\":{\"add\":[\"m:other\"]}}}}",
        "{\"format\":\"card_pack_slots-v1\",\"rows\":{\"910\":{\"card1\":\"m:card\",\"card2\":\"m:bad\",\"card3\":316,\"card4\":318,\"card5\":\"+317\"}}}",
        NULL, NULL,
        "{\"format\":\"default_deck-v1\",\"rows\":{\"0\":{\"card\":\"m:card\"},\"1\":{\"card\":\"m:bad\"},\"2\":{\"card\":316},\"3\":{\"card\":315},"
        "\"4\":{\"card\":\"dw3:card/9\"},\"5\":{\"card\":1023}}}");
    CHECK(strstr(log, "row \"317\" rejected: no row with that key") && strstr(log, "row \"m:card\" rejected: no row with that key"));
    CHECK(strstr(log, "row \"50\" field \"cards\" rejected") && strstr(log, "row \"51\" field \"cards\" rejected"));
    CHECK(strstr(log, "row \"910\" field \"card2\" rejected: no installed mod defines \"m:bad\"") && strstr(log, "row \"910\" field \"card3\" rejected"));
    CHECK(strstr(log, "row \"1\" field \"card\" rejected: no installed mod defines \"m:bad\"") && strstr(log, "row \"3\" field \"card\" rejected"));
    CHECK(strstr(log, "row \"5\" field \"card\" rejected"));
    CHECK(!strstr(log, "row \"49\" field") && !strstr(log, "row \"52\" field"));
    free(log);
    gameTablesApplyCardShop();
    CHECK(STCRDSHP_stocks[0].cards[0] == 317 && STCRDSHP_stocks[0].cards[1] == 7 && STCRDSHP_stocks[0].cards[2] == 318 && STCRDSHP_stocks[0].cards[3] == 12 &&
          STCRDSHP_stocks[0].cards[4] == 0);
    CHECK(STCRDSHP_stocks[1].cards == gShopCards[1] && STCRDSHP_stocks[2].cards == gShopCards[2]); /* refused lists leave the shop as it was */
    CHECK(STCRDSHP_stocks[3].cards[6] == 318 && STCRDSHP_stocks[3].cards[0] == gShopCards[3][0]); /* shop 52: the disc's list plus the mod's card */
    CHECK(gPackSlots[0][0] == 317 && gPackSlots[0][1] == slots0[0][1] && gPackSlots[0][2] == slots0[0][2] && gPackSlots[0][3] == 318);
    CHECK(STCRDSHP_prices[0].price == 7);
    gameTablesApplyCardGame();
    CHECK(CARDGAME_defaultDeck[0] == 316 && CARDGAME_defaultDeck[1] == deck0[1] && CARDGAME_defaultDeck[2] == 316 + 0 && CARDGAME_defaultDeck[3] == deck0[3]);
    CHECK(CARDGAME_defaultDeck[4] == 8 && CARDGAME_defaultDeck[5] == deck0[5]);
}

/* ---- game_tables.c (E5e): the gym's tables and the inns ---- */

GameTrainEntry STGTRAIN_gymTrainings[14][16];
GameTrainInfo STGTRAIN_trainings[25];
GameTrainGain STGTRAIN_statGains[12];
GameTrainGain STGTRAIN_statLosses[3];
GameTrainGain STGTRAIN_resistGainsLow[9];
GameTrainGain STGTRAIN_resistGainsMid[9];
GameTrainGain STGTRAIN_resistGainsHigh[9];
GameTrainGain STGTRAIN_maxHpMpGains[9];
s32 STGTRAIN_intensityCosts[3];
GameInn INNS[22];

static const char *fakeTrainingName(int id)
{
    static char buf[40];

    snprintf(buf, sizeof buf, "Training text %d", id);
    return buf;
}

static const char *fakeInnName(int id)
{
    static char buf[40];

    snprintf(buf, sizeof buf, "Inn text %d", id);
    return buf;
}

/* the disc's shapes with made-up values: five stat trainings, five resistance trainings (the first with max MP), the rest empty */
static void gymReset(void)
{
    int i, k;

    memset(STGTRAIN_gymTrainings, 0, sizeof STGTRAIN_gymTrainings);
    for (i = 0; i < 14; i++) {
        for (k = 0; k < 5; k++) {
            STGTRAIN_gymTrainings[i][k].id = k + 1;
            STGTRAIN_gymTrainings[i][k].stat = (short)(k + 1);
            STGTRAIN_gymTrainings[i][8 + k].id = 6 + k;
            STGTRAIN_gymTrainings[i][8 + k].stat = (short)(8 + k);
            STGTRAIN_gymTrainings[i][8 + k].other = (short)(k == 0 ? 16 : k);
        }
    }
    for (i = 0; i < 25; i++) {
        STGTRAIN_trainings[i].name = 19 + i;
        STGTRAIN_trainings[i].desc = 43 + i;
    }
    for (i = 0; i < 12; i++) {
        STGTRAIN_statGains[i].base = i % 5 + 1;
        STGTRAIN_statGains[i].range = i % 3;
    }
    for (i = 0; i < 9; i++) {
        STGTRAIN_resistGainsLow[i].base = i + 1;
        STGTRAIN_resistGainsLow[i].range = i % 3;
        STGTRAIN_resistGainsMid[i].base = 10 + i;
        STGTRAIN_resistGainsMid[i].range = 1 + i % 2;
        STGTRAIN_resistGainsHigh[i].base = 20 + i;
        STGTRAIN_resistGainsHigh[i].range = 0;
        STGTRAIN_maxHpMpGains[i].base = 3 * i + 1;
        STGTRAIN_maxHpMpGains[i].range = i % 2;
    }
    STGTRAIN_statLosses[0].base = 1; STGTRAIN_statLosses[0].range = 0;
    STGTRAIN_statLosses[1].base = 2; STGTRAIN_statLosses[1].range = 3;
    STGTRAIN_statLosses[2].base = 4; STGTRAIN_statLosses[2].range = 3;
    STGTRAIN_intensityCosts[0] = 1;
    STGTRAIN_intensityCosts[1] = 5;
    STGTRAIN_intensityCosts[2] = 10;
}

static void innsReset(void)
{
    int i;

    memset(INNS, 0, sizeof INNS);
    for (i = 0; i < 21; i++) {
        INNS[i].mode = 522 + 7 * i;
        INNS[i].string = (short)(i + 1);
        INNS[i].price = (short)(8 + 4 * i);
    }
    INNS[21].string = 1;
    INNS[21].price = 1;
}

/* the patches of the seven tables (NULL: none), in the order trainings, stat gains, stat losses, resist gains, hp/mp gains, costs, inns */
static char *gymStart(int withData, const char *const *p)
{
    static const char *const names[7] = {"gym_trainings", "gym_stat_gains", "gym_stat_losses", "gym_resist_gains", "gym_hp_mp_gains", "gym_intensity_costs", "inns"};
    char m[1300], d[1300], rel[100];
    int i;

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    for (i = 0; i < 7; i++) {
        if (p != NULL && p[i] != NULL) {
            snprintf(rel, sizeof rel, "mods/a/tables/%s.json", names[i]);
            put(rel, p[i]);
        }
    }
    snprintf(m, sizeof m, "%s/mods", gRoot);
    snprintf(d, sizeof d, "%s/data", gRoot);
    gymReset();
    innsReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(withData ? d : NULL);
    gameTablesSetTrainingNames(fakeTrainingName);
    gameTablesSetInnNames(fakeInnName);
    CHECK(gameTablesRegisterGym() == 6);
    CHECK(gameTablesRegisterInns() == 1);
    return logEnd();
}

static void testGymTables(void)
{
    static GameTrainEntry train0[14][16];
    static GameTrainGain stat0[12], loss0[3], low0[9], mid0[9], high0[9], hpmp0[9];
    static s32 cost0[3];
    static GameInn inns0[22];
    const char *p[7] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    char *log, *ref;
    int i;

    /* no mods: registered, nothing written; the layouts the offsets rely on */
    free(gymStart(1, NULL));
    memcpy(train0, STGTRAIN_gymTrainings, sizeof train0);
    memcpy(stat0, STGTRAIN_statGains, sizeof stat0);
    memcpy(loss0, STGTRAIN_statLosses, sizeof loss0);
    memcpy(low0, STGTRAIN_resistGainsLow, sizeof low0);
    memcpy(mid0, STGTRAIN_resistGainsMid, sizeof mid0);
    memcpy(high0, STGTRAIN_resistGainsHigh, sizeof high0);
    memcpy(hpmp0, STGTRAIN_maxHpMpGains, sizeof hpmp0);
    memcpy(cost0, STGTRAIN_intensityCosts, sizeof cost0);
    memcpy(inns0, INNS, sizeof inns0);
    gameTablesApplyGym();
    CHECK(!memcmp(train0, STGTRAIN_gymTrainings, sizeof train0) && !memcmp(stat0, STGTRAIN_statGains, sizeof stat0) && !memcmp(loss0, STGTRAIN_statLosses, sizeof loss0));
    CHECK(!memcmp(low0, STGTRAIN_resistGainsLow, sizeof low0) && !memcmp(mid0, STGTRAIN_resistGainsMid, sizeof mid0) && !memcmp(high0, STGTRAIN_resistGainsHigh, sizeof high0));
    CHECK(!memcmp(hpmp0, STGTRAIN_maxHpMpGains, sizeof hpmp0) && !memcmp(cost0, STGTRAIN_intensityCosts, sizeof cost0) && !memcmp(inns0, INNS, sizeof inns0));
    CHECK(tmPatchCount("gym_trainings") == 0 && tmPatchCount("gym_stat_gains") == 0 && tmPatchCount("gym_resist_gains") == 0 && tmPatchCount("inns") == 0);
    CHECK(sizeof(GameTrainEntry) == 8 && sizeof(GameTrainGain) == 8 && sizeof(GameTrainInfo) == 24 && sizeof(GameInn) == 8);
    CHECK(gameTableGymTrainings()->rows == 224 && gameTableGymStatGains()->rows == 12 && gameTableGymStatLosses()->rows == 3);
    CHECK(gameTableGymResistGains()->rows == 27 && gameTableGymHpMpGains()->rows == 9 && gameTableGymIntensityCosts()->rows == 3 && gameTableInns()->rows == 21);

    /* the reference exports: keys, labels and ranges */
    ref = slurp("data/tables/gym_trainings.json");
    CHECK(ref && strstr(ref, "\"0\": { \"id\": 1, \"stat\": 1, \"other\": 0 }") && strstr(ref, "\"108\": { \"id\": 6, \"stat\": 8, \"other\": 16 }"));
    CHECK(ref && strstr(ref, "\"1315\": {") && !strstr(ref, "\"1316\": {") && !strstr(ref, "\"1400\": {") && strstr(ref, "\"100\": \"Training text 20\"") && strstr(ref, "Valid: 0 to 24."));
    free(ref);
    ref = slurp("data/tables/gym_stat_gains.json");
    CHECK(ref && strstr(ref, "\"0\": { \"base\": 1, \"range\": 0 }") && strstr(ref, "\"11\": { \"base\": 2, \"range\": 2 }") && !strstr(ref, "\"12\": {"));
    CHECK(ref && strstr(ref, "Valid: 0 to 31768.") && strstr(ref, "Valid: 0 to 31769."));
    free(ref);
    ref = slurp("data/tables/gym_resist_gains.json");
    CHECK(ref && strstr(ref, "\"0\": { \"base\": 1, \"range\": 0 }") && strstr(ref, "\"10\": { \"base\": 10, \"range\": 1 }") && strstr(ref, "\"28\": { \"base\": 28, \"range\": 0 }"));
    CHECK(ref && !strstr(ref, "\"9\": {") && !strstr(ref, "\"19\": {") && !strstr(ref, "\"29\": {"));
    free(ref);
    ref = slurp("data/tables/gym_hp_mp_gains.json");
    CHECK(ref && strstr(ref, "\"8\": { \"base\": 25, \"range\": 0 }") && strstr(ref, "Valid: 0 to 22768."));
    free(ref);
    ref = slurp("data/tables/gym_stat_losses.json");
    CHECK(ref && strstr(ref, "\"2\": { \"base\": 4, \"range\": 3 }") && !strstr(ref, "\"3\": {") && strstr(ref, "Valid: 0 to 32767."));
    free(ref);
    ref = slurp("data/tables/gym_intensity_costs.json");
    CHECK(ref && strstr(ref, "\"1\": { \"cost\": 5 }") && strstr(ref, "Valid: 0 to 32767."));
    free(ref);
    ref = slurp("data/tables/inns.json");
    CHECK(ref && strstr(ref, "\"522\": { \"price\": 8, \"string\": 1 }") && strstr(ref, "\"662\": { \"price\": 88, \"string\": 21 }") && !strstr(ref, "\"669\": {"));
    CHECK(ref && strstr(ref, "\"522\": \"Inn text 1\"") && strstr(ref, "Valid: 0 to 21.") && strstr(ref, "Valid: 0 to 32767."));
    free(ref);

    /* trainings: whole rows go back when the result would not do (no stat, a stat between, an unnamed other), single fields when out of range */
    p[0] = "{\"format\":\"gym_trainings-v1\",\"rows\":{"
           "\"100\":{\"id\":3,\"stat\":5},"                  /* fine */
           "\"108\":{\"other\":3},"                          /* fine: max MP -> speed lowered */
           "\"109\":{\"other\":7},"                          /* refused: other 7 names nothing */
           "\"110\":{\"stat\":7,\"other\":4},"               /* refused whole: stat 7 (the other change goes back too) */
           "\"101\":{\"stat\":0},"                           /* refused: a training with no stat */
           "\"105\":{\"id\":7,\"stat\":3},"                  /* a new training in an empty slot */
           "\"106\":{\"id\":9},"                             /* refused: an id with no stat */
           "\"111\":{\"id\":25},\"112\":{\"stat\":15},\"113\":{\"other\":17},\"114\":{\"stat\":-1},"
           "\"102\":{\"id\":0},"                             /* fine: an empty slot is never read */
           "\"1600\":{\"id\":1},\"1400\":{\"id\":1},\"-1\":{\"id\":1},"
           "\"203\":{\"id\":\"+2\",\"stat\":\"+1\"}}}";
    log = gymStart(0, p);
    CHECK(strstr(log, "row \"111\" field \"id\" rejected") && strstr(log, "row \"112\" field \"stat\" rejected") && strstr(log, "row \"113\" field \"other\" rejected"));
    CHECK(strstr(log, "row \"114\" field \"stat\" rejected") && strstr(log, "row \"1600\" rejected: no row with that key") && strstr(log, "row \"1400\" rejected: no row with that key"));
    free(log);
    logBegin();
    gameTablesApplyGym();
    log = logEnd();
    CHECK(strstr(log, "gym_trainings row 109 rejected: its other stat is not 1-5, 15 or 16") && strstr(log, "gym_trainings row 110 rejected: its stat is not a battle stat"));
    CHECK(strstr(log, "gym_trainings row 101 rejected: its stat is not") && strstr(log, "gym_trainings row 106 rejected: its stat is not"));
    free(log);
    CHECK(STGTRAIN_gymTrainings[1][0].id == 3 && STGTRAIN_gymTrainings[1][0].stat == 5 && STGTRAIN_gymTrainings[1][0].other == 0);
    CHECK(STGTRAIN_gymTrainings[1][8].other == 3 && STGTRAIN_gymTrainings[1][8].id == 6 && STGTRAIN_gymTrainings[1][8].stat == 8);
    CHECK(!memcmp(&STGTRAIN_gymTrainings[1][9], &train0[1][9], sizeof train0[1][9])); /* other 7 refused */
    CHECK(!memcmp(&STGTRAIN_gymTrainings[1][10], &train0[1][10], sizeof train0[1][10])); /* stat 7 + other 4: the whole row */
    CHECK(!memcmp(&STGTRAIN_gymTrainings[1][1], &train0[1][1], sizeof train0[1][1]));
    CHECK(STGTRAIN_gymTrainings[1][5].id == 7 && STGTRAIN_gymTrainings[1][5].stat == 3 && !memcmp(&STGTRAIN_gymTrainings[1][6], &train0[1][6], sizeof train0[1][6]));
    CHECK(STGTRAIN_gymTrainings[1][2].id == 0 && STGTRAIN_gymTrainings[1][2].stat == 3); /* the id 0 row keeps its stat: never read */
    CHECK(STGTRAIN_gymTrainings[2][3].id == train0[2][3].id + 2 && STGTRAIN_gymTrainings[2][3].stat == train0[2][3].stat + 1);
    CHECK(!memcmp(&STGTRAIN_gymTrainings[1][11], &train0[1][11], sizeof train0[1][11]) && !memcmp(STGTRAIN_gymTrainings[0], train0[0], sizeof train0[0]));
    CHECK(!memcmp(STGTRAIN_gymTrainings[13], train0[13], sizeof train0[13]));
    /* the scope restores the overlay on the next entry: applying again lands on the same rows */
    gymReset();
    gameTablesApplyGym();
    CHECK(STGTRAIN_gymTrainings[1][0].id == 3 && STGTRAIN_gymTrainings[1][8].other == 3 && STGTRAIN_gymTrainings[2][3].id == train0[2][3].id + 2);
    CHECK(!memcmp(&STGTRAIN_gymTrainings[1][10], &train0[1][10], sizeof train0[1][10]));

    /* gains: a number, an operation, the s16 limits (a row whose largest result would wrap is refused whole), a negative one */
    p[0] = NULL;
    p[1] = "{\"format\":\"gym_stat_gains-v1\",\"rows\":{\"0\":{\"base\":5,\"range\":0},\"1\":{\"base\":31768,\"range\":1},\"2\":{\"base\":31768,\"range\":2},"
           "\"3\":{\"base\":-1},\"4\":{\"base\":31769},\"5\":{\"range\":31770},\"6\":{\"base\":\"*2\"},\"7\":{\"range\":31769},\"12\":{\"base\":1}}}";
    p[2] = "{\"format\":\"gym_stat_losses-v1\",\"rows\":{\"0\":{\"base\":32767,\"range\":1},\"1\":{\"base\":32767,\"range\":2},\"2\":{\"range\":-1}}}";
    p[3] = "{\"format\":\"gym_resist_gains-v1\",\"rows\":{\"0\":{\"base\":7},\"10\":{\"base\":8,\"range\":4},\"20\":{\"range\":31769},\"28\":{\"base\":31768,\"range\":2},"
           "\"9\":{\"base\":1},\"29\":{\"base\":1},\"18\":{\"base\":0,\"range\":0}}}";
    p[4] = "{\"format\":\"gym_hp_mp_gains-v1\",\"rows\":{\"0\":{\"base\":22768,\"range\":1},\"1\":{\"base\":22768,\"range\":2},\"2\":{\"base\":22769},\"3\":{\"base\":\"+1\"}}}";
    p[5] = "{\"format\":\"gym_intensity_costs-v1\",\"rows\":{\"0\":{\"cost\":0},\"1\":{\"cost\":32768},\"2\":{\"cost\":-1},\"3\":{\"cost\":1},\"1000\":{\"cost\":3}}}";
    log = gymStart(0, p);
    CHECK(strstr(log, "row \"3\" field \"base\" rejected") && strstr(log, "row \"4\" field \"base\" rejected") && strstr(log, "row \"5\" field \"range\" rejected"));
    CHECK(strstr(log, "row \"12\" rejected: no row with that key") && strstr(log, "row \"9\" rejected: no row with that key") && strstr(log, "row \"29\" rejected: no row with that key"));
    CHECK(strstr(log, "row \"2\" field \"range\" rejected") && strstr(log, "row \"1\" field \"cost\" rejected") && strstr(log, "row \"2\" field \"cost\" rejected"));
    CHECK(strstr(log, "row \"2\" field \"base\" rejected") && strstr(log, "row \"1000\" rejected: no row with that key"));
    free(log);
    logBegin();
    gameTablesApplyGym();
    log = logEnd();
    CHECK(strstr(log, "gym_stat_gains row 2 rejected: the largest result") && strstr(log, "gym_stat_losses row 1 rejected: the largest result"));
    CHECK(strstr(log, "gym_resist_gains row 28 rejected: the largest result") && strstr(log, "gym_hp_mp_gains row 1 rejected: the largest result"));
    CHECK(strstr(log, "gym_stat_gains row 7 rejected: the largest result"));
    free(log);
    CHECK(STGTRAIN_statGains[0].base == 5 && STGTRAIN_statGains[0].range == 0 && STGTRAIN_statGains[1].base == 31768 && STGTRAIN_statGains[1].range == 1);
    CHECK(!memcmp(&STGTRAIN_statGains[2], &stat0[2], sizeof stat0[2]) && !memcmp(&STGTRAIN_statGains[3], &stat0[3], sizeof stat0[3]) && !memcmp(&STGTRAIN_statGains[4], &stat0[4], sizeof stat0[4]));
    CHECK(STGTRAIN_statGains[6].base == stat0[6].base * 2);
    CHECK(STGTRAIN_statLosses[0].base == 32767 && STGTRAIN_statLosses[0].range == 1 && !memcmp(&STGTRAIN_statLosses[1], &loss0[1], sizeof loss0[1]));
    CHECK(!memcmp(&STGTRAIN_statLosses[2], &loss0[2], sizeof loss0[2]));
    CHECK(STGTRAIN_resistGainsLow[0].base == 7 && STGTRAIN_resistGainsLow[0].range == low0[0].range && STGTRAIN_resistGainsMid[0].base == 8 && STGTRAIN_resistGainsMid[0].range == 4);
    CHECK(!memcmp(&STGTRAIN_resistGainsHigh[0], &high0[0], sizeof high0[0]) && !memcmp(&STGTRAIN_resistGainsHigh[8], &high0[8], sizeof high0[8])); /* keys 20 and 28: refused */
    CHECK(STGTRAIN_resistGainsMid[8].base == 0 && STGTRAIN_resistGainsMid[8].range == 0 && !memcmp(&STGTRAIN_resistGainsMid[1], &mid0[1], sizeof mid0[1]));
    for (i = 2; i < 9; i++) CHECK(!memcmp(&STGTRAIN_resistGainsLow[i], &low0[i], sizeof low0[i]));
    CHECK(STGTRAIN_maxHpMpGains[0].base == 22768 && STGTRAIN_maxHpMpGains[0].range == 1 && !memcmp(&STGTRAIN_maxHpMpGains[1], &hpmp0[1], sizeof hpmp0[1]));
    CHECK(!memcmp(&STGTRAIN_maxHpMpGains[2], &hpmp0[2], sizeof hpmp0[2]) && STGTRAIN_maxHpMpGains[3].base == hpmp0[3].base + 1);
    CHECK(STGTRAIN_intensityCosts[0] == 0 && STGTRAIN_intensityCosts[1] == 5 && STGTRAIN_intensityCosts[2] == 10);
    CHECK(!memcmp(STGTRAIN_gymTrainings, train0, sizeof train0) && !memcmp(inns0, INNS, sizeof inns0));
    gymReset();
    gameTablesApplyGym();
    CHECK(STGTRAIN_statGains[0].base == 5 && STGTRAIN_resistGainsMid[0].base == 8 && STGTRAIN_intensityCosts[0] == 0 && STGTRAIN_statGains[6].base == stat0[6].base * 2);

    /* inns: patched once when registered (the executable's data), the key is the game mode and `mode` is not a field */
    for (i = 0; i < 7; i++) p[i] = NULL;
    p[6] = "{\"format\":\"inns-v1\",\"rows\":{\"522\":{\"price\":0},\"529\":{\"price\":32768},\"536\":{\"string\":22},\"543\":{\"string\":21,\"price\":\"*2\"},"
           "\"550\":{\"mode\":5},\"551\":{\"price\":3},\"0\":{\"price\":3},\"557\":{\"price\":-1},\"564\":{\"string\":\"+1\",\"price\":\"+10\"}}}";
    log = gymStart(0, p);
    CHECK(strstr(log, "row \"529\" field \"price\" rejected") && strstr(log, "row \"536\" field \"string\" rejected") && strstr(log, "row \"550\" field \"mode\" rejected"));
    CHECK(strstr(log, "row \"551\" rejected: no row with that key") && strstr(log, "row \"0\" rejected: no row with that key") && strstr(log, "row \"557\" field \"price\" rejected"));
    free(log);
    CHECK(INNS[0].mode == 522 && INNS[0].price == 0 && INNS[0].string == 1);
    CHECK(!memcmp(&INNS[1], &inns0[1], sizeof inns0[1]) && !memcmp(&INNS[2], &inns0[2], sizeof inns0[2])); /* price 32768 and string 22 refused */
    CHECK(INNS[3].price == inns0[3].price * 2 && INNS[3].string == 21 && INNS[3].mode == inns0[3].mode);
    CHECK(!memcmp(&INNS[4], &inns0[4], sizeof inns0[4]) && !memcmp(&INNS[5], &inns0[5], sizeof inns0[5])); /* `mode` is no field; price -1 refused */
    CHECK(INNS[6].string == inns0[6].string + 1 && INNS[6].price == inns0[6].price + 10 && INNS[6].mode == inns0[6].mode);
    for (i = 7; i < 21; i++) CHECK(!memcmp(&INNS[i], &inns0[i], sizeof inns0[i]));
    CHECK(INNS[21].mode == 0 && INNS[21].string == 1 && INNS[21].price == 1); /* the end row is never touched */
    CHECK(!memcmp(STGTRAIN_gymTrainings, train0, sizeof train0) && !memcmp(STGTRAIN_statGains, stat0, sizeof stat0));
}


/* ---- game_tables.c (S3): the event system's condition tables and the special conditions a mod adds ---- */

/* made-up rows: the shapes of the real ones (partner and money and progress-range rows, the end row 0xFF), none of the disc's values */
SpecialCondition SPECIAL_CONDITIONS[] = {
    {0x00, 0x10, 0x03}, {0x01, 0x11, 0x02}, {0x02, 0x12, 0x01}, {0x03, 0x20, 0x04}, {0x04, 0x21, 0x03}, {0x05, 0x22, 0x05},
    {0x06, 0x30, 0x02}, {0x07, 0x40, 0x03}, {0x08, 0x50, 0x00}, {0x09, 0x00, 0x00}, {0xFF, 0, 0},
};
s32 MONEY_REQUIRED[10];
s32 MONEY_GAINS[8];
s32 MONEY_LOSSES[10];
u8 PROGRESS_RANGES[34][2];
s32 PARTY_STAT_THRESHOLDS[15];

static void eventsReset(void)
{
    static const SpecialCondition rows[] = {
        {0x00, 0x10, 0x03}, {0x01, 0x11, 0x02}, {0x02, 0x12, 0x01}, {0x03, 0x20, 0x04}, {0x04, 0x21, 0x03}, {0x05, 0x22, 0x05},
        {0x06, 0x30, 0x02}, {0x07, 0x40, 0x03}, {0x08, 0x50, 0x00}, {0x09, 0x00, 0x00}, {0xFF, 0, 0},
    };
    int i;

    memcpy(SPECIAL_CONDITIONS, rows, sizeof rows);
    for (i = 0; i < 10; i++) {
        MONEY_REQUIRED[i] = 100 * (i + 1);
        MONEY_LOSSES[i] = 50 * (i + 1);
    }
    for (i = 0; i < 8; i++) MONEY_GAINS[i] = 10 * (i + 1);
    for (i = 0; i < 34; i++) {
        PROGRESS_RANGES[i][0] = (u8)i;
        PROGRESS_RANGES[i][1] = (u8)(i + 5);
    }
    PROGRESS_RANGES[33][0] = 0;
    PROGRESS_RANGES[33][1] = 0;
    for (i = 0; i < 15; i++) PARTY_STAT_THRESHOLDS[i] = 30 * (i + 1);
}

static int findPartner(const char *name)
{
    if (!strcmp(name, "m:bee")) return 8;
    if (!strcmp(name, "m:cat")) return 9;
    if (!strncmp(name, "dw3:partner/", 12)) return atoi(name + 12);
    return -1;
}

static int tenPartners(void)
{
    return 10;
}

/* the patches of the six tables in the order special_conditions, money_required, money_gains, money_losses, progress_ranges, party_stat_thresholds;
   a second mod "b" with the same table files in q (NULL: none) */
static char *eventsStart(int withData, const char *const *p, const char *const *q)
{
    static const char *const names[6] = {"special_conditions", "money_required", "money_gains", "money_losses", "progress_ranges", "party_stat_thresholds"};
    char m[1300], d[1300], rel[100];
    int i;

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    put("mods/b/mod.json", "{\"format\":\"mod-v1\",\"name\":\"y\",\"version\":\"1\"}");
    for (i = 0; i < 6; i++) {
        if (p != NULL && p[i] != NULL) {
            snprintf(rel, sizeof rel, "mods/a/tables/%s.json", names[i]);
            put(rel, p[i]);
        }
        if (q != NULL && q[i] != NULL) {
            snprintf(rel, sizeof rel, "mods/b/tables/%s.json", names[i]);
            put(rel, q[i]);
        }
    }
    snprintf(m, sizeof m, "%s/mods", gRoot);
    snprintf(d, sizeof d, "%s/data", gRoot);
    eventsReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(withData ? d : NULL);
    CHECK(gameTablesRegisterEvents() == 6);
    return logEnd();
}

static void testEventTables(void)
{
    static SpecialCondition sp0[11];
    static s32 req0[10], gain0[8], loss0[10], thr0[15];
    static u8 range0[34][2];
    const char *p[6] = {NULL, NULL, NULL, NULL, NULL, NULL}, *q[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    unsigned char check = 0, arg = 0;
    char *log, *ref;
    int i, c, a;

    /* no mods: registered, nothing written; the rows are counted up to the end row; the reference export has the keys and the labels */
    free(eventsStart(1, NULL, NULL));
    memcpy(sp0, SPECIAL_CONDITIONS, sizeof sp0);
    memcpy(req0, MONEY_REQUIRED, sizeof req0);
    memcpy(gain0, MONEY_GAINS, sizeof gain0);
    memcpy(loss0, MONEY_LOSSES, sizeof loss0);
    memcpy(range0, PROGRESS_RANGES, sizeof range0);
    memcpy(thr0, PARTY_STAT_THRESHOLDS, sizeof thr0);
    CHECK(gameTableSpecialConditions()->rows == 10 && gameTableMoneyRequired()->rows == 10 && gameTableMoneyGains()->rows == 8);
    CHECK(gameTableMoneyLosses()->rows == 10 && gameTableProgressRanges()->rows == 34 && gameTablePartyStatThresholds()->rows == 15);
    CHECK(tmPatchCount("special_conditions") == 0 && tmPatchCount("progress_ranges") == 0);
    ref = slurp("data/tables/special_conditions.json");
    CHECK(ref != NULL && strstr(ref, "\"format\": \"special_conditions-v1\"") && strstr(ref, "\"primitive\"") && strstr(ref, "\"op\"") && strstr(ref, "\"arg\""));
    CHECK(ref != NULL && strstr(ref, "\"partner, op 0, arg 3\"") && strstr(ref, "\"money, op 2, arg 5\"")); /* the labels of the original rows */
    free(ref);
    ref = slurp("data/tables/progress_ranges.json");
    CHECK(ref != NULL && strstr(ref, "\"min\"") && strstr(ref, "\"max\""));
    free(ref);
    CHECK(gameSpecialIdOf("a:greeting") == -1 && gameSpecialNameOf(0x100) == NULL);
    CHECK(gameSpecialRow(3, &c, &a) == 1 && c == 0x20 && a == 4 && gameSpecialRow(0x7F, &c, &a) == 0 && gameSpecialRow(0x100, &c, &a) == 0);

    /* patches: the two nibbles of the check byte are fields; whole-row checks refuse rows the game would index out of its arrays */
    p[0] = "{\"format\":\"special_conditions-v1\",\"rows\":{"
           "\"3\":{\"arg\":9},"                                   /* money op 0, index 9: fine */
           "\"4\":{\"arg\":8},"                                   /* money op 1 reads money_gains[8]: refused */
           "\"1\":{\"op\":3,\"arg\":7},"                          /* partner op 3 (unlock partner 7): fine */
           "\"2\":{\"arg\":8},"                                   /* partner op 2 compares with a party member: any arg */
           "\"0\":{\"op\":1,\"arg\":8},"                          /* partner op 1 reads partner 8: refused */
           "\"6\":{\"arg\":34},"                                  /* progress range 34: refused */
           "\"7\":{\"primitive\":4,\"op\":1},"                    /* flag count op 1: fine */
           "\"8\":{\"op\":6},"                                    /* actor icon ignores op: fine */
           "\"9\":{\"primitive\":6},"                             /* primitive 6: the field's range */
           "\"255\":{\"arg\":1},\"77\":{\"arg\":1}}}";            /* no such rows */
    p[1] = "{\"format\":\"money_required-v1\",\"rows\":{\"0\":{\"amount\":0},\"1\":{\"amount\":9999999},\"2\":{\"amount\":10000000},\"3\":{\"amount\":\"*3\"},\"10\":{\"amount\":1}}}";
    p[2] = "{\"format\":\"money_gains-v1\",\"rows\":{\"7\":{\"amount\":\"+5\"},\"0\":{\"amount\":-1}}}";
    p[3] = "{\"format\":\"money_losses-v1\",\"rows\":{\"*\":{\"amount\":1}}}";
    p[4] = "{\"format\":\"progress_ranges-v1\",\"rows\":{\"0\":{\"min\":3,\"max\":9},\"1\":{\"min\":20},\"2\":{\"max\":1},\"33\":{\"min\":1,\"max\":99}}}";
    p[5] = "{\"format\":\"party_stat_thresholds-v1\",\"rows\":{\"0\":{\"threshold\":1},\"14\":{\"threshold\":100000},\"1\":{\"threshold\":\"+9\"}}}";
    log = eventsStart(0, p, NULL);
    CHECK(strstr(log, "special_conditions row 4 rejected: this money op reads the amount") && strstr(log, "special_conditions row 0 rejected: this partner op reads partner `arg`"));
    CHECK(strstr(log, "special_conditions row 6 rejected: a progress range row's `arg`") && strstr(log, "row \"9\" field \"primitive\" rejected"));
    CHECK(strstr(log, "row \"255\" rejected: no row with that key") && strstr(log, "row \"77\" rejected: no row with that key"));
    CHECK(strstr(log, "money_required.json row \"2\" field \"amount\" rejected") && strstr(log, "money_required.json row \"10\" rejected: no row with that key"));
    CHECK(strstr(log, "money_gains.json row \"0\" field \"amount\" rejected"));
    CHECK(strstr(log, "progress_ranges row 1 rejected: its min is above its max") && strstr(log, "progress_ranges row 2 rejected: its min is above its max"));
    CHECK(strstr(log, "party_stat_thresholds.json row \"14\" field \"threshold\" rejected"));
    free(log);
    CHECK(SPECIAL_CONDITIONS[3].check == 0x20 && SPECIAL_CONDITIONS[3].arg == 9);
    CHECK(!memcmp(&SPECIAL_CONDITIONS[4], &sp0[4], sizeof sp0[4]) && !memcmp(&SPECIAL_CONDITIONS[0], &sp0[0], sizeof sp0[0]) && !memcmp(&SPECIAL_CONDITIONS[6], &sp0[6], sizeof sp0[6]));
    CHECK(SPECIAL_CONDITIONS[1].check == 0x13 && SPECIAL_CONDITIONS[1].arg == 7 && SPECIAL_CONDITIONS[2].check == 0x12 && SPECIAL_CONDITIONS[2].arg == 8);
    CHECK(SPECIAL_CONDITIONS[7].check == 0x41 && SPECIAL_CONDITIONS[8].check == 0x56 && !memcmp(&SPECIAL_CONDITIONS[9], &sp0[9], sizeof sp0[9]));
    CHECK(SPECIAL_CONDITIONS[10].id == 0xFF && SPECIAL_CONDITIONS[2].id == 2 && SPECIAL_CONDITIONS[1].id == 1); /* the end row and every id stay */
    CHECK(MONEY_REQUIRED[0] == 0 && MONEY_REQUIRED[1] == 9999999 && MONEY_REQUIRED[2] == req0[2] && MONEY_REQUIRED[3] == req0[3] * 3 && MONEY_REQUIRED[4] == req0[4]);
    CHECK(MONEY_GAINS[7] == gain0[7] + 5 && MONEY_GAINS[0] == gain0[0]);
    for (i = 0; i < 10; i++) CHECK(MONEY_LOSSES[i] == 1);
    CHECK(PROGRESS_RANGES[0][0] == 3 && PROGRESS_RANGES[0][1] == 9);
    CHECK(PROGRESS_RANGES[1][0] == range0[1][0] && PROGRESS_RANGES[1][1] == range0[1][1] && PROGRESS_RANGES[2][0] == range0[2][0] && PROGRESS_RANGES[2][1] == range0[2][1]);
    CHECK(PROGRESS_RANGES[33][0] == 1 && PROGRESS_RANGES[33][1] == 99);
    CHECK(PARTY_STAT_THRESHOLDS[0] == 1 && PARTY_STAT_THRESHOLDS[1] == thr0[1] + 9 && PARTY_STAT_THRESHOLDS[14] == thr0[14]);

    /* a mod's own conditions: a row keyed by a name makes a new id (0x100 up), valid only with a primitive, an op and an arg the game can run */
    memset(p, 0, sizeof p);
    p[0] = "{\"format\":\"special_conditions-v1\",\"rows\":{"
           "\"a:greeting\":{\"primitive\":1,\"op\":3,\"arg\":2},"      /* unlock partner 2 */
           "\"a:half\":{\"primitive\":1},"                            /* no op: op 15 is invalid */
           "\"a:rich\":{\"primitive\":2,\"op\":0,\"arg\":9},"         /* has money_required[9] */
           "\"a:oops\":{\"primitive\":2,\"op\":1,\"arg\":8},"         /* money_gains[8] does not exist */
           "\"a:icon\":{\"primitive\":5,\"op\":0},"                   /* actor icon: no arg */
           "\"BAD\":{\"primitive\":1},\"a:Bad\":{\"primitive\":1,\"op\":0},\"a:\":{\"op\":0}}}";
    q[0] = "{\"format\":\"special_conditions-v1\",\"rows\":{"
           "\"a:rich\":{\"arg\":3},"                                  /* the later mod changes the arg of a row the first made */
           "\"b:own\":{\"primitive\":3,\"op\":0,\"arg\":33},"          /* progress range 33 */
           "\"3\":{\"arg\":1}}}";                                     /* and an original row */
    log = eventsStart(0, p, q);
    CHECK(strstr(log, "special_conditions row a:half rejected: a partner row's op is 0-9"));
    CHECK(strstr(log, "special_conditions row a:oops rejected: this money op reads the amount"));
    CHECK(strstr(log, "row \"BAD\" rejected: no row with that key") && strstr(log, "row \"a:Bad\" rejected: no installed mod defines that name"));
    CHECK(strstr(log, "row \"a:\" rejected: no installed mod defines that name"));
    free(log);
    CHECK(gameSpecialIdOf("a:greeting") == 0x100 && gameSpecialIdOf("a:rich") == 0x102 && gameSpecialIdOf("a:icon") == 0x104 && gameSpecialIdOf("b:own") == 0x105);
    CHECK(gameSpecialIdOf("a:half") == -1 && gameSpecialIdOf("a:oops") == -1 && gameSpecialIdOf("a:nothing") == -1 && gameSpecialIdOf(NULL) == -1);
    CHECK(gameSpecialNameOf(0x100) != NULL && !strcmp(gameSpecialNameOf(0x102), "a:rich") && gameSpecialNameOf(0x106) == NULL && gameSpecialNameOf(0x50) == NULL);
    CHECK(pcSpecialExtRow(0x100, &check, &arg) == 1 && check == 0x13 && arg == 2);
    CHECK(pcSpecialExtRow(0x102, &check, &arg) == 1 && check == 0x20 && arg == 3); /* the last mod's arg */
    CHECK(pcSpecialExtRow(0x104, &check, &arg) == 1 && check == 0x50);
    CHECK(pcSpecialExtRow(0x105, &check, &arg) == 1 && check == 0x30 && arg == 33);
    CHECK(pcSpecialExtRow(0x101, &check, &arg) == 0 && pcSpecialExtRow(0x103, &check, &arg) == 0 && pcSpecialExtRow(0x106, &check, &arg) == 0 && pcSpecialExtRow(0x7, &check, &arg) == 0);
    CHECK(gameSpecialRow(0x100, &c, &a) == 1 && c == 0x13 && a == 2 && gameSpecialRow(0x101, &c, &a) == 0);
    CHECK(SPECIAL_CONDITIONS[3].check == 0x20 && SPECIAL_CONDITIONS[3].arg == 1); /* the original row of the same patch */
    CHECK(!memcmp(&SPECIAL_CONDITIONS[0], &sp0[0], sizeof sp0[0]) && SPECIAL_CONDITIONS[10].id == 0xFF); /* the original array never grows */

    /* registering again forgets the names (no stale rows after a mod change) and an unpatched run has none */
    free(eventsStart(0, NULL, NULL));
    CHECK(gameSpecialIdOf("a:greeting") == -1 && gameSpecialNameOf(0x100) == NULL);
    CHECK(!memcmp(SPECIAL_CONDITIONS, sp0, sizeof sp0) && !memcmp(MONEY_REQUIRED, req0, sizeof req0) && !memcmp(PROGRESS_RANGES, range0, sizeof range0));

    /* partners a mod adds (P4): the partner ops read partner `arg`, which must exist; the host says how many there are */
    memset(p, 0, sizeof p);
    p[0] = "{\"format\":\"special_conditions-v1\",\"rows\":{"
           "\"0\":{\"op\":1,\"arg\":9},"                           /* partner 9 exists (ten partners): fine */
           "\"1\":{\"op\":3,\"arg\":10}}}";                        /* partner 10 does not: refused */
    gameTablesSetPartnerCount(tenPartners);
    log = eventsStart(0, p, NULL);
    CHECK(SPECIAL_CONDITIONS[0].check == 0x11 && SPECIAL_CONDITIONS[0].arg == 9);
    CHECK(strstr(log, "special_conditions row 1 rejected: this partner op reads partner `arg`") && !strstr(log, "special_conditions row 0 rejected"));
    free(log);
    gameTablesSetPartnerCount(NULL);
    log = eventsStart(0, p, NULL);
    CHECK(strstr(log, "special_conditions row 0 rejected: this partner op reads partner `arg`")); /* without the hook: the original eight */
    free(log);
    free(eventsStart(0, NULL, NULL));

    /* P11: a partner op names its partner (`arg` as "<mod>:<key>", "dw3:partner/<n>"): resolved when the table loads, the numbers keep working */
    memset(p, 0, sizeof p);
    p[0] = "{\"format\":\"special_conditions-v1\",\"rows\":{"
           "\"m:unlock-bee\":{\"primitive\":1,\"op\":3,\"arg\":\"m:bee\"},"      /* UNLOCK partner 8 by name */
           "\"m:bee-locked\":{\"primitive\":1,\"op\":6,\"arg\":\"m:bee\"},"      /* "still locked" */
           "\"m:unlock-orig\":{\"primitive\":1,\"op\":3,\"arg\":\"dw3:partner/4\"},"
           "\"m:unlock-num\":{\"primitive\":1,\"op\":3,\"arg\":9},"
           "\"m:unlock-ghost\":{\"primitive\":1,\"op\":3,\"arg\":\"m:ghost\"},"
           "\"1\":{\"arg\":\"m:cat\"}}}";                                            /* an original row can be pointed at a name too */
    gameTablesSetPartnerCount(tenPartners);
    gameTablesSetPartnerFind(findPartner);
    log = eventsStart(0, p, NULL);
    CHECK(pcSpecialExtRow(0x100, &check, &arg) == 1 && check == 0x13 && arg == 8);
    CHECK(pcSpecialExtRow(0x101, &check, &arg) == 1 && check == 0x16 && arg == 8);
    CHECK(pcSpecialExtRow(0x102, &check, &arg) == 1 && check == 0x13 && arg == 4);
    CHECK(pcSpecialExtRow(0x103, &check, &arg) == 1 && check == 0x13 && arg == 9);
    CHECK(SPECIAL_CONDITIONS[1].arg == 9 && SPECIAL_CONDITIONS[1].check == sp0[1].check);
    CHECK(strstr(log, "row \"m:unlock-ghost\" field \"arg\" rejected: no installed mod defines \"m:ghost\""));
    CHECK(!strstr(log, "m:bee") && !strstr(log, "dw3:partner/4"));
    free(log);
    gameTablesSetPartnerFind(NULL); /* no registry: a name resolves to nothing, the row refuses it */
    log = eventsStart(0, p, NULL);
    CHECK(strstr(log, "row \"m:unlock-bee\" field \"arg\" rejected: no installed mod defines \"m:bee\""));
    free(log);

    /* a rejected `arg` never runs as a substitute: a mod's row with an unknown name is no row (it would unlock partner 0), an original row keeps its arg */
    memset(p, 0, sizeof p);
    p[0] = "{\"format\":\"special_conditions-v1\",\"rows\":{"
           "\"a:unlockghost\":{\"primitive\":1,\"op\":3,\"arg\":\"a:nobody\"},"
           "\"a:fine\":{\"primitive\":1,\"op\":3,\"arg\":2},"
           "\"1\":{\"arg\":\"a:nobody\"}}}";
    log = eventsStart(0, p, NULL);
    CHECK(gameSpecialIdOf("a:unlockghost") == -1 && gameSpecialIdOf("a:fine") != -1);
    CHECK(pcSpecialExtRow(0x100, &check, &arg) == 0 && pcSpecialExtRow(0x101, &check, &arg) == 1 && arg == 2);
    CHECK(SPECIAL_CONDITIONS[1].arg == sp0[1].arg && SPECIAL_CONDITIONS[1].check == sp0[1].check);
    CHECK(strstr(log, "row \"a:unlockghost\" field \"arg\" rejected: no installed mod defines \"a:nobody\"") && strstr(log, "row \"1\" field \"arg\" rejected: no installed mod defines \"a:nobody\""));
    free(log);
    gameTablesSetPartnerCount(NULL);
    free(eventsStart(0, NULL, NULL));
    CHECK(!memcmp(SPECIAL_CONDITIONS, sp0, sizeof sp0)); /* the original numeric rows are as the disc has them */

    /* up to 256 new rows, the rest refused (the id is 9 bits and the game's own ids stop below 0xFF) */
    {
        static char big[256 * 80 + 400];
        size_t n = 0;

        n += (size_t)snprintf(big + n, sizeof big - n, "{\"format\":\"special_conditions-v1\",\"rows\":{");
        for (i = 0; i < 258; i++) {
            n += (size_t)snprintf(big + n, sizeof big - n, "%s\"a:r%d\":{\"primitive\":5,\"op\":0}", i ? "," : "", i);
        }
        snprintf(big + n, sizeof big - n, "}}");
        memset(p, 0, sizeof p);
        p[0] = big;
        log = eventsStart(0, p, NULL);
        CHECK(strstr(log, "row \"a:r256\" rejected: no installed mod defines that name") && strstr(log, "row \"a:r257\" rejected"));
        free(log);
        CHECK(gameSpecialIdOf("a:r0") == 0x100 && gameSpecialIdOf("a:r255") == 0x1FF && gameSpecialIdOf("a:r256") == -1);
    }
    free(eventsStart(0, NULL, NULL));
}


/* ---- the engine has no fixed caps (U1): many tables, long lists ---- */

typedef struct Big {
    int count;
    int *items;
} Big;

#define BIG_N 600
static int gBigItems[BIG_N + 1];
static Big gBig[1];
static const TmField kBigFields[] = {
    {"items", TM_S32, offsetof(Big, items), 1, 100000, "ids", 1000, offsetof(Big, count)},
};
static const TmTable kBigTable = {"big", gBig, sizeof(Big), 1, 0, "the row", "when", kBigFields, 1};

static char *idsJson(int n)
{
    char *b = (char *)malloc((size_t)n * 8 + 8), *p = b;
    int i;
    *p++ = '[';
    for (i = 0; i < n; i++) p += sprintf(p, "%s%d", i ? "," : "", i + 1);
    *p++ = ']';
    *p = 0;
    return b;
}

static void bigReset(void)
{
    int i;
    for (i = 0; i < BIG_N; i++) gBigItems[i] = i + 1;
    gBigItems[BIG_N] = 0;
    gBig[0].count = BIG_N;
    gBig[0].items = gBigItems;
}

static void testNoCaps(void)
{
    static char names[100][16];
    char m[1300], *log, *ids, *text;
    int i, ok = 0, n;

    /* 100 tables: all registered, none dropped (the registry used to hold 32) */
    wipe();
    snprintf(m, sizeof m, "%s/mods", gRoot);
    reset();
    logBegin();
    modsInit(m, NULL);
    tmInit(NULL);
    for (i = 0; i < 100; i++) {
        TmTable t = kTable;
        snprintf(names[i], sizeof names[i], "many%d", i);
        t.name = names[i];
        ok += tmRegister(&t) == 0;
    }
    log = logEnd();
    CHECK(ok == 100);
    CHECK(tmOriginalRow("many0", 10) != NULL && tmOriginalRow("many99", 10) != NULL);
    CHECK(tmApply("many99") == 0);
    free(log);

    /* a list longer than the old cap of 256: registered, patched, and an excess rejected whole with a log line */
    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    ids = idsJson(700);
    text = (char *)malloc(strlen(ids) + 100);
    sprintf(text, "{\"format\":\"big-v1\",\"rows\":{\"0\":{\"items\":%s}}}", ids);
    put("mods/a/tables/big.json", text);
    free(text);
    free(ids);
    bigReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(NULL);
    CHECK(tmRegister(&kBigTable) == 0);
    log = logEnd();
    free(log);
    log = (logBegin(), tmApply("big"), logEnd());
    free(log);
    CHECK(gBig[0].count == 700 && gBig[0].items[699] == 700 && gBig[0].items[700] == 0);
    n = gBig[0].count;

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    ids = idsJson(1001);
    text = (char *)malloc(strlen(ids) + 100);
    sprintf(text, "{\"format\":\"big-v1\",\"rows\":{\"0\":{\"items\":%s}}}", ids);
    put("mods/a/tables/big.json", text);
    free(text);
    free(ids);
    bigReset();
    logBegin();
    modsInit(m, NULL);
    tmInit(NULL);
    CHECK(tmRegister(&kBigTable) == 0);
    log = logEnd();
    free((logBegin(), tmApply("big"), logEnd()));
    CHECK(strstr(log, "rejected: more than the list can hold (more than 1000 items)") != NULL);
    CHECK(gBig[0].count == BIG_N && gBig[0].items == gBigItems); /* the row keeps what it held */
    free(log);
    (void)n;

    /* a descriptor that no engine could honour is refused with a log line, never silently */
    {
        static const TmField huge[] = {{"items", TM_S32, offsetof(Big, items), 1, 100000, "ids", 100000000, offsetof(Big, count)}};
        TmTable t = kBigTable;
        t.name = "huge";
        t.fields = huge;
        bigReset();
        logBegin();
        tmInit(NULL);
        CHECK(tmRegister(&t) == -1);
        log = logEnd();
        CHECK(strstr(log, "table \"huge\" not registered") != NULL);
        free(log);
    }
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    char tpl[1100];
    snprintf(tpl, sizeof tpl, "%s/tabletest-XXXXXX", (tmp && tmp[0]) ? tmp : "/tmp");
    if (!mkdtemp(tpl)) {
        fprintf(stderr, "tabletest: cannot make a scratch folder in %s\n", tpl);
        return 2;
    }
    snprintf(gRoot, sizeof gRoot, "%s", tpl);
    snprintf(gLogPath, sizeof gLogPath, "%s/.log", gRoot);
    testNoMods();
    testOpsAndPrecedence();
    testClampAndRound();
    testRejections();
    testModOrder();
    testIdempotent();
    testExport();
    testExportFile();
    testBadDescriptor();
    testFileBacked();
    testFileBackedNoPatch();
    testOriginalRowMemory();
    testPostReadHook();
    testPriceTable();
    testListFields();
    testNamedIds();
    testGameTables();
    testGrowthTables();
    testFieldTables();
    testCardTables();
    testCardOpponents();
    testGymTables();
    testEventTables();
    testNoCaps();
    {
        char cmd[1200];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
        if (system(cmd)) {}
    }
    if (failures) {
        printf("tabletest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("tabletest: OK\n");
    return 0;
}
