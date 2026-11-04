/*
 * Offline check of the path layer (pc/shim/port_paths.c, ADR-010): the user folder's resolution (platform folder,
 * portable mode, DW3_USER_DIR), every item and its environment override, the import/ rule, the tree creation and
 * the case-insensitive lookup. The platform hook is replaced by a scratch one; everything is written under
 * $TMPDIR (or /tmp).
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/port_paths.h"

static char gExe[PP_CAP], gPref[PP_CAP];
static int failures;

/* the host test's platform: a scratch executable folder and a scratch per-user folder */
int portPathsPlatform(char *exeDir, int exeCap, char *pref, int prefCap) {
    snprintf(exeDir, (size_t)exeCap, "%s", gExe);
    if (prefCap > 0) {
        snprintf(pref, (size_t)prefCap, "%s", gPref);
        return 1;
    }
    return 0;
}

static void expect(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static void touch(const char *path) {
    FILE *f;

    portMkdirsFor(path);
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("x", f);
        fclose(f);
    }
}

static void clearEnv(void) {
    int d;

    for (d = 0; d < PP_DIR_COUNT; d++) {
        unsetenv(portPathsEnvName((PortDir)d));
    }
    unsetenv("DW3_USER_DIR");
    unsetenv("DW3_MEMCARD");
    unsetenv("DW3_L10N_DIR");
    portPathsReset();
}

int main(void) {
    char root[512], p[PP_CAP], q[PP_CAP], cards[8][PP_CAP];
    const char *tmp = getenv("TMPDIR");
    int n;

    snprintf(root, sizeof(root), "%s/pathstest.%d", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", (int)getpid());
    snprintf(gExe, sizeof(gExe), "%s/app/", root);
    snprintf(gPref, sizeof(gPref), "%s/share/dw3port/dw3/", root);
    portMkdirs(gExe);
    clearEnv();

    /* pure choice: portable wins, else the platform's, none when neither */
    expect(portPathsChoosePrefDir("/app/", 1, "/home/u/dw3/", p, sizeof(p)) && strcmp(p, "/app/") == 0, "choose: portable -> executable folder");
    expect(portPathsChoosePrefDir("/app/", 0, "/home/u/dw3/", p, sizeof(p)) && strcmp(p, "/home/u/dw3/") == 0, "choose: platform folder");
    expect(!portPathsChoosePrefDir("/app/", 0, NULL, p, sizeof(p)), "choose: nothing at all");
    expect(portPathsChoosePrefDir(NULL, 1, "/pref/", p, sizeof(p)) && strcmp(p, "/pref/") == 0, "choose: portable without an executable folder");

    /* Android: the app's external files folder when it has one, else the internal one; always ends in a separator */
    expect(portPathsAndroidBase("/storage/emulated/0/Android/data/x/files", "/data/user/0/x/files", p, sizeof(p))
               && strcmp(p, "/storage/emulated/0/Android/data/x/files/") == 0, "android base: external storage");
    expect(portPathsAndroidBase("/ext/files/", "/int/files", p, sizeof(p)) && strcmp(p, "/ext/files/") == 0, "android base: separator kept");
    expect(portPathsAndroidBase(NULL, "/int/files", p, sizeof(p)) && strcmp(p, "/int/files/") == 0, "android base: internal when no external");
    expect(portPathsAndroidBase("", "/int/files", p, sizeof(p)) && strcmp(p, "/int/files/") == 0, "android base: empty external");
    expect(!portPathsAndroidBase(NULL, NULL, p, sizeof(p)) && !portPathsAndroidBase("", "", p, sizeof(p)), "android base: none at all");
    expect(!portPathsAndroidBase("/a/very/long/folder", NULL, p, 8), "android base: does not fit");

    /* the es-419 overlay folder: the order of the places to look (Android adds the user folder's localization/) */
    expect(portPathsL10nCandidate(0, "/env/l10n", "/app/", "/b/", p, sizeof(p)) && strcmp(p, "/env/l10n") == 0, "l10n: environment first");
    expect(portPathsL10nCandidate(0, NULL, "/app/", "/b/", p, sizeof(p)) && strcmp(p, "localization/es-419") == 0, "l10n: working folder");
    expect(portPathsL10nCandidate(1, NULL, "/app/", "/b/", p, sizeof(p)) && strcmp(p, "/app/localization/es-419") == 0, "l10n: executable folder");
    expect(portPathsL10nCandidate(2, NULL, "/app/", "/b/", p, sizeof(p)) && strcmp(p, "/b/localization/es-419") == 0, "l10n: user folder (Android)");
    expect(!portPathsL10nCandidate(2, NULL, "/app/", NULL, p, sizeof(p)) && !portPathsL10nCandidate(1, NULL, NULL, NULL, p, sizeof(p)),
           "l10n: no user folder / no executable folder");
    expect(!portPathsL10nCandidate(3, NULL, "/app/", "/b/", p, sizeof(p)), "l10n: no fourth place");

    /* pure resolution of each item under a base, with and without its override */
    expect(portPathsResolveWith(PP_SETTINGS, "/b/", NULL, p, sizeof(p)) && strcmp(p, "/b/settings.ini") == 0, "item: settings.ini");
    expect(portPathsResolveWith(PP_SAVES, "/b/", NULL, p, sizeof(p)) && strcmp(p, "/b/saves/default") == 0, "item: saves/default");
    expect(portPathsResolveWith(PP_IMPORT, "/b", NULL, p, sizeof(p)) && strcmp(p, "/b/import") == 0, "item: import (base without a separator)");
    expect(portPathsResolveWith(PP_MODS, "/b/", NULL, p, sizeof(p)) && strcmp(p, "/b/mods") == 0, "item: mods");
    expect(portPathsResolveWith(PP_DATA, "/b/", NULL, p, sizeof(p)) && strcmp(p, "/b/data") == 0, "item: data");
    expect(portPathsResolveWith(PP_LOGS, "/b/", NULL, p, sizeof(p)) && strcmp(p, "/b/logs") == 0, "item: logs");
    expect(portPathsResolveWith(PP_SCREENSHOTS, "/b/", NULL, p, sizeof(p)) && strcmp(p, "/b/screenshots") == 0, "item: screenshots");
    expect(portPathsResolveWith(PP_CACHE, "/b/", NULL, p, sizeof(p)) && strcmp(p, "/b/cache") == 0, "item: cache");
    expect(portPathsResolveWith(PP_DATA, "/b/", "/x/data//", p, sizeof(p)) && strcmp(p, "/x/data") == 0, "override: wins and loses the trailing separator");
    expect(portPathsResolveWith(PP_SETTINGS, "/b/", "/x/my.ini", p, sizeof(p)) && strcmp(p, "/x/my.ini") == 0, "override: the settings file as given");
    expect(portPathsResolveWith(PP_DATA, NULL, "/x/d", p, sizeof(p)) && strcmp(p, "/x/d") == 0, "override: works without a base");
    expect(!portPathsResolveWith(PP_DATA, NULL, NULL, p, sizeof(p)), "item: no base and no override -> none");
    expect(!portPathsResolveWith(PP_DATA, "/very/long/", NULL, p, 5), "item: too small a buffer is refused");
    expect(strcmp(portPathsEnvName(PP_SAVES), "DW3_SAVES_DIR") == 0 && strcmp(portPathsEnvName(PP_SETTINGS), "DW3_SETTINGS") == 0, "env names");

    /* live: the platform folder, then portable.txt, then DW3_USER_DIR */
    clearEnv();
    expect(portPathsBase() != NULL && strcmp(portPathsBase(), gPref) == 0 && !portPathsIsPortable(), "live: the platform folder by default");
    expect(portPath(PP_SAVES, p, sizeof(p)) && strstr(p, "/share/dw3port/dw3/saves/default") != NULL, "live: saves under it");
    expect(portPathsViewDir(p, sizeof(p)) && strstr(p, "/saves/default/view") != NULL, "live: view/ inside the profile folder");
    expect(portPathIn(PP_SAVES, "slot0.dw3save", p, sizeof(p)) && strstr(p, "/saves/default/slot0.dw3save") != NULL, "live: a file of the profile");
    snprintf(p, sizeof(p), "%sportable.txt", gExe);
    touch(p);
    portPathsReset();
    expect(portPathsIsPortable() && strcmp(portPathsBase(), gExe) == 0, "live: portable.txt -> next to the executable");
    expect(portPath(PP_MODS, q, sizeof(q)) && strcmp(q, "") != 0 && strstr(q, "/app/mods") != NULL, "live: mods next to the executable");
    remove(p);
    snprintf(p, sizeof(p), "%s/scratch-user", root);
    setenv("DW3_USER_DIR", p, 1);
    portPathsReset();
    expect(portPathsBase() != NULL && strncmp(portPathsBase(), p, strlen(p)) == 0 && portPathsBase()[strlen(p)] == '/', "live: DW3_USER_DIR moves the whole tree");
    expect(portPath(PP_IMPORT, q, sizeof(q)) && strstr(q, "/scratch-user/import") != NULL, "live: import under DW3_USER_DIR");
    setenv("DW3_DATA_DIR", "/elsewhere/data/", 1);
    expect(portPath(PP_DATA, q, sizeof(q)) && strcmp(q, "/elsewhere/data") == 0, "live: DW3_DATA_DIR beats the base");

    /* the tree */
    unsetenv("DW3_DATA_DIR");
    expect(portPathsEnsureTree(), "tree: created");
    expect(portPath(PP_SAVES, q, sizeof(q)) && portIsDir(q), "tree: saves/default");
    expect(portPath(PP_IMPORT, q, sizeof(q)) && portIsDir(q), "tree: import");
    expect(portPath(PP_MODS, q, sizeof(q)) && portIsDir(q), "tree: mods");
    expect(portPath(PP_DATA, q, sizeof(q)) && portIsDir(q), "tree: data");
    expect(portPath(PP_LOGS, q, sizeof(q)) && portIsDir(q), "tree: logs");
    expect(portPathsEnsureTree(), "tree: a second call is harmless");

    /* import/: every *.mcd by name, any case; others ignored; DW3_MEMCARD replaces the folder */
    portPathIn(PP_IMPORT, "b.mcd", p, sizeof(p));
    touch(p);
    portPathIn(PP_IMPORT, "A.MCD", p, sizeof(p));
    touch(p);
    portPathIn(PP_IMPORT, "notes.txt", p, sizeof(p));
    touch(p);
    portPathIn(PP_IMPORT, "imported.txt", p, sizeof(p));
    touch(p);
    n = portPathsImportCards(cards, 8);
    expect(n == 2, "import: only the .mcd files count");
    expect(n == 2 && strstr(cards[0], "/import/A.MCD") != NULL && strstr(cards[1], "/import/b.mcd") != NULL, "import: sorted by name without regard to case");
    expect(portPathsImportCards(cards, 1) == 1, "import: capped by max");
    expect(portPathsImportMarker(p, sizeof(p)) && strstr(p, "/import/imported.txt") != NULL, "import: the record lives in import/");
    setenv("DW3_MEMCARD", "/h/scratch/one.mcd", 1);
    n = portPathsImportCards(cards, 8);
    expect(n == 1 && strcmp(cards[0], "/h/scratch/one.mcd") == 0, "import: DW3_MEMCARD is the only card");
    expect(portPathsImportMarker(p, sizeof(p)) && strcmp(p, "/h/scratch/imported.txt") == 0, "import: its record sits next to it");
    unsetenv("DW3_MEMCARD");

    /* case-insensitive lookup: exact first, then each component by name */
    snprintf(q, sizeof(q), "%s/ci", root);
    snprintf(p, sizeof(p), "%s/ci/IMAGES/Field/ABC/Img_01.PNG", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/ci2/images/exact.png", root);
    touch(p);
    snprintf(cards[0], PP_CAP, "%s/ci2", root);
    expect(portPathsResolveCI(cards[0], "images/exact.png", p, sizeof(p)) && strstr(p, "/ci2/images/exact.png") != NULL, "ci: the exact path");
    expect(portPathsResolveCI(q, "images/field/abc/img_01.png", p, sizeof(p)) && strstr(p, "/ci/IMAGES/Field/ABC/Img_01.PNG") != NULL,
           "ci: a mod written with upper-case names");
    expect(portPathsResolveCI(q, "IMAGES/FIELD/ABC/IMG_01.PNG", p, sizeof(p)), "ci: the other way round");
    expect(!portPathsResolveCI(q, "images/field/abc/nope.png", p, sizeof(p)), "ci: a missing file stays missing");
    expect(!portPathsResolveCI(q, "images/field/zzz/img_01.png", p, sizeof(p)), "ci: a missing folder stays missing");
    expect(!portPathsResolveCI("/does/not/exist", "a/b.png", p, sizeof(p)), "ci: a missing root");

    /* mkdir -p with either separator, and for a file's folder */
    snprintf(p, sizeof(p), "%s/m1\\m2/m3", root);
    expect(portMkdirs(p), "mkdirs: mixed separators");
    snprintf(p, sizeof(p), "%s/f1/f2/file.txt", root);
    expect(portMkdirsFor(p) && !portIsFile(p), "mkdirsFor: the folder of a file, no file made");
    snprintf(p, sizeof(p), "%s/f1/f2", root);
    expect(portIsDir(p), "mkdirsFor: it exists");

    clearEnv();
    snprintf(p, sizeof(p), "rm -rf '%s'", root);
    if (system(p) != 0) {
        fprintf(stderr, "pathstest: cannot remove %s\n", root);
    }
    if (failures) {
        fprintf(stderr, "pathstest: %d failure(s)\n", failures);
        return 1;
    }
    printf("pathstest: OK\n");
    return 0;
}
