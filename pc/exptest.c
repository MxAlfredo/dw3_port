/*
 * Host test of the shared export write policy (B1): pc/shim/exp_core.c. A scratch folder under $TMPDIR, no disc, no retail bytes.
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/exp_core.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "exptest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gDir[1024];

static void path(char *out, size_t cap, const char *rel) { snprintf(out, cap, "%s/%s", gDir, rel); }

/* the file's text, "" when it is not there */
static void slurp(const char *rel, char *out, size_t cap) {
    char p[1400];
    uint8_t *d;
    size_t n;

    path(p, sizeof p, rel);
    out[0] = '\0';
    if (tdReadFile(p, &d, &n) == 0) {
        snprintf(out, cap, "%.*s", (int)n, (const char *)d);
        free(d);
    }
}

static void spit(const char *rel, const char *text) {
    char p[1400], dir[1400], *sl;

    path(p, sizeof p, rel);
    snprintf(dir, sizeof dir, "%s", p);
    sl = strrchr(dir, '/');
    if (sl) {
        *sl = '\0';
        CHECK(tdMakeDirs(dir) == 0);
    }
    CHECK(tdWriteFileAtomic(p, text, strlen(text)) == 0);
}

static int put(ExpSink *s, const char *rel, const char *text) { return expPut(s, rel, text, strlen(text)); }

static void testPolicy(void) {
    ExpManifest man;
    ExpSink sink;
    ExpStats st;
    char mp[1400], got[64];

    memset(&st, 0, sizeof st);
    path(mp, sizeof mp, "manifest.json");
    expManOpen(&man, "exptest", mp, "t");
    expSinkInPlace(&sink, "exptest", gDir, &man, &st);

    /* new: written (with its folder), recorded, counted */
    CHECK(put(&sink, "a/one.txt", "v1") == 0);
    slurp("a/one.txt", got, sizeof got);
    CHECK(!strcmp(got, "v1"));
    CHECK(tdManifestFind(&man.m, "a/one.txt") != NULL);
    CHECK(st.written == 1 && st.kept == 0 && st.newFiles == 0 && st.bytes == 2);
    CHECK(man.dirty == 1);

    /* identical: kept, nothing rewritten */
    man.dirty = 0;
    CHECK(put(&sink, "a/one.txt", "v1") == 0);
    CHECK(st.written == 1 && st.kept == 1);
    CHECK(man.dirty == 0);

    /* ours and unedited, new bytes: replaced */
    CHECK(put(&sink, "a/one.txt", "v2") == 0);
    slurp("a/one.txt", got, sizeof got);
    CHECK(!strcmp(got, "v2"));
    CHECK(st.written == 2 && man.dirty == 1);

    /* edited by the user: stays, the new version goes beside it as .new, the manifest keeps our hash */
    {
        char before[17];
        snprintf(before, sizeof before, "%s", tdManifestFind(&man.m, "a/one.txt"));
        spit("a/one.txt", "mine");
        CHECK(put(&sink, "a/one.txt", "v3") == 0);
        slurp("a/one.txt", got, sizeof got);
        CHECK(!strcmp(got, "mine"));
        slurp("a/one.txt.new", got, sizeof got);
        CHECK(!strcmp(got, "v3"));
        CHECK(st.newFiles == 1 && st.written == 2);
        CHECK(!strcmp(tdManifestFind(&man.m, "a/one.txt"), before));
    }

    /* a file nobody recorded counts as the user's */
    spit("b.txt", "stranger");
    CHECK(put(&sink, "b.txt", "ours") == 0);
    slurp("b.txt", got, sizeof got);
    CHECK(!strcmp(got, "stranger"));
    slurp("b.txt.new", got, sizeof got);
    CHECK(!strcmp(got, "ours"));
    CHECK(tdManifestFind(&man.m, "b.txt") == NULL);

    /* the user's edit that equals the new bytes: it is ours again */
    CHECK(put(&sink, "b.txt", "stranger") == 0);
    CHECK(tdManifestFind(&man.m, "b.txt") != NULL);

    /* the manifest file: only when changed, and it reads back */
    CHECK(expManSave(&man, 1) == 0 && man.dirty == 0);
    {
        ExpManifest again;
        expManOpen(&again, "exptest", mp, "t");
        CHECK(!strcmp(again.m.topic, "t") && again.m.count == man.m.count);
        CHECK(tdManifestFind(&again.m, "a/one.txt") != NULL);
        expManFree(&again);
    }
    expManFree(&man);
}

static void testRebuild(void) {
    ExpManifest oldMan;
    TdManifest next;
    ExpSink sink;
    ExpStats st;
    char mp[1400], got[64];

    memset(&st, 0, sizeof st);
    memset(&next, 0, sizeof next);
    path(mp, sizeof mp, "stamped.json");
    expManOpen(&oldMan, "exptest", mp, "s");
    sink.tag = "exptest";
    sink.dir = gDir;
    sink.old = &oldMan.m;
    sink.next = &next;
    sink.dirty = NULL;
    sink.sameRun = 0;
    sink.stats = &st;

    CHECK(put(&sink, "c.txt", "c1") == 0);
    CHECK(tdManifestFind(&oldMan.m, "c.txt") == NULL && tdManifestFind(&next, "c.txt") != NULL);
    tdManifestSet(&oldMan.m, "c.txt", tdManifestFind(&next, "c.txt"));

    /* the same inputs and an unedited file: skipped, whatever the new bytes are */
    sink.sameRun = 1;
    CHECK(put(&sink, "c.txt", "c2") == 0);
    slurp("c.txt", got, sizeof got);
    CHECK(!strcmp(got, "c1") && st.kept == 1);

    /* other inputs: regenerated */
    sink.sameRun = 0;
    CHECK(put(&sink, "c.txt", "c2") == 0);
    slurp("c.txt", got, sizeof got);
    CHECK(!strcmp(got, "c2"));

    tdManifestFree(&next);
    expManFree(&oldMan);
}

static void writeText(const char *p, const char *text) {
    FILE *f = fopen(p, "wb");

    CHECK(f != NULL);
    if (f == NULL) return;
    fputs(text, f);
    fclose(f);
}

static void testVersion(void) {
    ExpManifest man, again;
    char mp[1400], disc[17], text[600];

    path(mp, sizeof mp, "ver/manifest.json");
    expManOpen(&man, "exptest", mp, "v");
    expDiscHex(disc);
    CHECK(!strcmp(man.m.topic, "v") && !strcmp(man.m.disc, disc) && man.m.exporter.count == 0);
    CHECK(!expManIsCurrent(&man, "x", 3));
    expManMark(&man, "x", 3);
    CHECK(man.dirty && expManIsCurrent(&man, "x", 3) && !expManIsCurrent(&man, "x", 4));
    CHECK(expManSave(&man, 1) == 0); /* makes its folder */
    CHECK(expFileExists(mp));
    expManMark(&man, "x", 3);
    CHECK(!man.dirty); /* the same version again changes nothing */
    expManFree(&man);

    /* the version is in the header, as a number, and not among the files */
    expManOpen(&again, "exptest", mp, "v");
    CHECK(expManIsCurrent(&again, "x", 3) && !again.dirty && again.m.count == 0 && !strcmp(tdKvFind(&again.m.exporter, "x"), "3"));
    expManFree(&again);

    /* another disc: the files stay the manifest's (an edited one is still the user's), the versions do not count */
    snprintf(text, sizeof text,
             "{\n  \"format\": \"asset-manifest-v1\",\n  \"topic\": \"v\",\n  \"exporter\": {\"x\": 3},\n  \"disc\": \"not-this-disc\",\n  \"names\": {},\n"
             "  \"files\": {\"f.txt\": \"0123456789abcdef\"}\n}\n");
    writeText(mp, text);
    expDiscOverride("this-disc");
    expManOpen(&again, "exptest", mp, "v");
    CHECK(!expManIsCurrent(&again, "x", 3) && again.dirty && !strcmp(again.m.disc, "this-disc"));
    CHECK(!strcmp(tdManifestFind(&again.m, "f.txt"), "0123456789abcdef"));
    expManFree(&again);
    writeText(mp, text);
    expDiscOverride(NULL); /* no disc open: the manifest is left as it is */
    expManOpen(&again, "exptest", mp, "v");
    CHECK(expManIsCurrent(&again, "x", 3) && !again.dirty && !strcmp(again.m.disc, "not-this-disc"));
    expManFree(&again);

    /* an old manifest (stamp + files) is not this format: it counts as absent, nothing of it is kept */
    writeText(mp, "{\n  \"stamp\": \"v-v1\",\n  \"files\": {\n    \"f.txt\": \"0123456789abcdef\",\n    \"x/exporter\": \"0000000000000003\"\n  }\n}\n");
    expManOpen(&again, "exptest", mp, "v");
    CHECK(!strcmp(again.m.topic, "v") && again.m.count == 0 && again.m.exporter.count == 0 && !expManIsCurrent(&again, "x", 3));
    expManFree(&again);

    /* names and files a run did not regenerate are carried; versions are not */
    {
        TdManifest old, next;

        expManInit(&old, "v");
        expManInit(&next, "v");
        CHECK(tdKvSet(&old.names, "agumon", "dw3:partner/0") == 0 && tdManifestSet(&old, "a.png", "0123456789abcdef") == 0 && tdKvSet(&old.exporter, "x", "1") == 0);
        expManCarry(&next, &old);
        CHECK(!strcmp(tdKvFind(&next.names, "agumon"), "dw3:partner/0"));
        CHECK(!strcmp(tdManifestFind(&next, "a.png"), "0123456789abcdef"));
        CHECK(next.exporter.count == 0);
        tdManifestFree(&old);
        tdManifestFree(&next);
    }
}

int main(void) {
    const char *tmp = getenv("TMPDIR");
    char cmd[1200];

    snprintf(gDir, sizeof gDir, "%s/exptest-%d", tmp != NULL && tmp[0] ? tmp : "/tmp", (int)getpid());
    CHECK(tdMakeDirs(gDir) == 0);
    testPolicy();
    testRebuild();
    testVersion();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", gDir);
    if (system(cmd) != 0) fprintf(stdout, "exptest: could not remove %s\n", gDir);
    if (failures) {
        fprintf(stdout, "exptest: %d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stdout, "exptest: ok\n");
    return 0;
}
