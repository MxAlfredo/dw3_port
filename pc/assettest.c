/*
 * Host test of the asset layers (B4): pc/shim/mods.c assetResolve and friends. A scratch folder under $TMPDIR, no disc.
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "shim/mods.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "assettest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gRoot[1024];

static void spit(const char *rel, const char *text) {
    char p[1400], dir[1400], *sl;

    snprintf(p, sizeof p, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "%s", p);
    sl = strrchr(dir, '/');
    if (sl) {
        *sl = '\0';
        CHECK(tdMakeDirs(dir) == 0);
    }
    CHECK(tdWriteFileAtomic(p, text, strlen(text)) == 0);
}

static void mkMod(const char *name) {
    char rel[200];

    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    spit(rel, "{\"format\": \"mod-v1\"}\n");
}

/* the text of the file the resolver picked, "" when none; *who gets the label */
static void pick(const char *topic, const char *rel, char *text, char *who) {
    char path[1400];
    AssetLayer from;
    uint8_t *d;
    size_t n;

    text[0] = who[0] = '\0';
    if (!assetResolve(topic, rel, path, sizeof path, &from)) return;
    snprintf(who, 80, "%s", from.label);
    if (tdReadFile(path, &d, &n) == 0) {
        snprintf(text, 40, "%.*s", (int)(n < 39 ? n : 39), (const char *)d);
        free(d);
    }
}

static void testLayers(void) {
    char modsRoot[1100], dataDir[1100], text[40], who[80], path[1400];
    AssetLayer l;

    snprintf(modsRoot, sizeof modsRoot, "%s/mods", gRoot);
    snprintf(dataDir, sizeof dataDir, "%s/data", gRoot);
    mkMod("a");
    mkMod("b");
    mkMod("c");
    spit("data/t/both.txt", "data");
    spit("mods/a/t/both.txt", "a");
    spit("mods/b/t/both.txt", "b");
    spit("data/t/only-data.txt", "data");
    spit("mods/a/t/only-a.txt", "a");
    spit("mods/a/U/Cased.TXT", "a-cased");
    spit("data/t/sub/deep.txt", "data-deep");
    spit("mods/c/t/dir.txt/x", "not a file"); /* a folder is not a file */

    /* no data folder yet: the mods only */
    CHECK(modsInit(modsRoot, NULL) == 3);
    CHECK(assetDataDir() == NULL && assetLayerCount() == 3);
    pick("t", "only-data.txt", text, who);
    CHECK(who[0] == '\0');
    assetSetData(dataDir);
    CHECK(strcmp(assetDataDir(), dataDir) == 0 && assetLayerCount() == 4);

    /* order: the last mod first, the data folder last */
    CHECK(assetLayerAt(0, &l) && l.mod == 2 && strcmp(l.label, "mods/c") == 0);
    CHECK(assetLayerAt(1, &l) && l.mod == 1 && strcmp(l.label, "mods/b") == 0 && strcmp(l.root, modsDirOf(1)) == 0);
    CHECK(assetLayerAt(2, &l) && l.mod == 0);
    CHECK(assetLayerAt(3, &l) && l.mod == -1 && strcmp(l.label, "data") == 0 && strcmp(l.root, dataDir) == 0);
    CHECK(!assetLayerAt(4, &l) && !assetLayerAt(-1, &l));

    /* who wins */
    pick("t", "both.txt", text, who);
    CHECK(strcmp(text, "b") == 0 && strcmp(who, "mods/b") == 0); /* c has none; b is the last that has it */
    pick("t", "only-a.txt", text, who);
    CHECK(strcmp(text, "a") == 0 && strcmp(who, "mods/a") == 0);
    pick("t", "only-data.txt", text, who);
    CHECK(strcmp(text, "data") == 0 && strcmp(who, "data") == 0);
    pick("t", "sub/deep.txt", text, who);
    CHECK(strcmp(text, "data-deep") == 0);
    pick("u", "cased.txt", text, who); /* the mod spells it any way */
    CHECK(strcmp(text, "a-cased") == 0 && strcmp(who, "mods/a") == 0);
    pick("t", "missing.txt", text, who);
    CHECK(who[0] == '\0');
    pick("other", "both.txt", text, who); /* the topic is part of the path */
    CHECK(who[0] == '\0');
    pick("t", "dir.txt", text, who);
    CHECK(who[0] == '\0');
    CHECK(assetResolve("t", "both.txt", path, sizeof path, NULL));

    /* a single layer's file */
    assetLayerAt(3, &l);
    CHECK(assetLayerFile(&l, "t", "both.txt", path, sizeof path) && strstr(path, "/data/t/both.txt") != NULL);
    assetLayerAt(0, &l);
    CHECK(!assetLayerFile(&l, "t", "both.txt", path, sizeof path));

    /* one mod's file or folder, by index in the load order (a, b, c): the merging sites walk the mods in that order */
    CHECK(assetModFile(0, "t", "both.txt", path, sizeof path) && strstr(path, "/mods/a/t/both.txt") != NULL);
    CHECK(assetModFile(1, "t", "both.txt", path, sizeof path) && strstr(path, "/mods/b/") != NULL);
    CHECK(!assetModFile(2, "t", "both.txt", path, sizeof path) && !assetModFile(3, "t", "both.txt", path, sizeof path));
    CHECK(assetModFile(0, NULL, "t/only-a.txt", path, sizeof path)); /* no topic: the path inside the mod */
    CHECK(assetModFile(0, "U", "CASED.txt", path, sizeof path));
    CHECK(assetModDir(0, NULL, "t", path, sizeof path) && !assetModFile(0, NULL, "t", path, sizeof path));
    CHECK(!assetModDir(0, NULL, "t/only-a.txt", path, sizeof path) && !assetModDir(-1, NULL, "t", path, sizeof path));

    /* a disabled mod takes no part: the next layer shows through */
    CHECK(modsInit(modsRoot, "-b") == 2);
    CHECK(assetLayerCount() == 3);
    pick("t", "both.txt", text, who);
    CHECK(strcmp(text, "a") == 0 && strcmp(who, "mods/a") == 0);
    /* an order sets which wins */
    CHECK(modsInit(modsRoot, "b, a") == 2);
    pick("t", "both.txt", text, who);
    CHECK(strcmp(text, "a") == 0); /* the last listed wins */
    CHECK(modsInit(modsRoot, "a, b") == 2);
    pick("t", "both.txt", text, who);
    CHECK(strcmp(text, "b") == 0);

    /* no data folder again: nothing past the mods */
    assetSetData(NULL);
    CHECK(assetDataDir() == NULL && assetLayerCount() == 2);
    pick("t", "only-data.txt", text, who);
    CHECK(who[0] == '\0');
}

int main(void) {
    char tmpl[] = "/tmp/assettestXXXXXX";
    char cmd[700];

    if (!mkdtemp(tmpl)) return 2;
    snprintf(gRoot, sizeof gRoot, "%s", tmpl);
    testLayers();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
    if (system(cmd)) {}
    if (failures) {
        printf("assettest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("assettest: ok\n");
    return 0;
}
