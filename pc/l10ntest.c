/*
 * Host test of the overlay installer: on Android the es-419 overlay
 * ships inside the APK as assets and is copied into the user folder at start. The test stands in for the APK with a
 * scratch folder (the reader is a plain file read), and checks the index parser, the first install, the "nothing to do"
 * second run, the refresh when the stamp changes and that files the installer did not write are left alone.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/l10n_install.h"
#include "shim/port_paths.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gSrc[512];

static int readFile(const char *path, uint8_t **data, size_t *len) {
    FILE *f = fopen(path, "rb");
    long n;

    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = (uint8_t *)malloc((size_t)n + 1);
    *len = fread(*data, 1, (size_t)n, f);
    fclose(f);
    return 0;
}

static int assetRead(const char *name, uint8_t **data, size_t *len) {
    return readFile(name, data, len); /* the installer passes srcPrefix + the file's name */
}

static void put(const char *path, const char *text) {
    FILE *f;

    portMkdirsFor(path);
    f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

static int has(const char *path, const char *text) {
    uint8_t *d = NULL;
    size_t n = 0;
    int ok = readFile(path, &d, &n) == 0 && n == strlen(text) && memcmp(d, text, n) == 0;

    free(d);
    return ok;
}

int main(void) {
    char root[400], base[512], p[1024], stamp[80], rel[128];
    const char *cur;
    long size;
    const char *tmp = getenv("TMPDIR");

    /* the index: "stamp <text>", then "<size> <relative path>" lines; anything else is skipped */
    {
        const char *idx = "stamp abc123\n5 layout.json\n\nbogus\n7 es-419/SSTALK00.json\r\n";

        CHECK(l10nIndexStamp(idx, stamp, sizeof(stamp)) && strcmp(stamp, "abc123") == 0);
        CHECK(!l10nIndexStamp("5 layout.json\n", stamp, sizeof(stamp)));
        cur = idx;
        CHECK(l10nIndexNext(&cur, rel, sizeof(rel), &size) && size == 5 && strcmp(rel, "layout.json") == 0);
        CHECK(l10nIndexNext(&cur, rel, sizeof(rel), &size) && size == 7 && strcmp(rel, "es-419/SSTALK00.json") == 0);
        CHECK(!l10nIndexNext(&cur, rel, sizeof(rel), &size));
        cur = "9 ../escape.json\n3 /abs.json\n1 ok.json\n";
        CHECK(l10nIndexNext(&cur, rel, sizeof(rel), &size) && strcmp(rel, "ok.json") == 0); /* unsafe paths are never installed */
    }

    snprintf(root, sizeof(root), "%s/l10ntest.%d", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", (int)getpid());
    snprintf(gSrc, sizeof(gSrc), "%s/apk/localization/", root);
    snprintf(base, sizeof(base), "%s/user/", root);
    snprintf(p, sizeof(p), "%sindex.txt", gSrc);
    put(p, "stamp v1\n5 layout.json\n7 es-419/SSTALK00.json\n");
    snprintf(p, sizeof(p), "%slayout.json", gSrc);
    put(p, "{\"a\"}");
    snprintf(p, sizeof(p), "%ses-419/SSTALK00.json", gSrc);
    put(p, "[\"one\"]\n");

    /* no assets (a desktop build, or an APK without the overlay): nothing happens */
    CHECK(l10nInstall("/nonexistent/", base, assetRead) == 0);

    /* first install */
    CHECK(l10nInstall(gSrc, base, assetRead) == 2);
    snprintf(p, sizeof(p), "%slocalization/layout.json", base);
    CHECK(has(p, "{\"a\"}"));
    snprintf(p, sizeof(p), "%slocalization/es-419/SSTALK00.json", base);
    CHECK(has(p, "[\"one\"]\n"));

    /* same stamp and every file there: nothing to copy, and a file the player edited is not overwritten */
    put(p, "[\"mine\"]\n");
    CHECK(l10nInstall(gSrc, base, assetRead) == 0);
    CHECK(has(p, "[\"mine\"]\n"));

    /* a file that went missing is restored even with the same stamp */
    remove(p);
    CHECK(l10nInstall(gSrc, base, assetRead) == 1);
    CHECK(has(p, "[\"one\"]\n"));

    /* a new app version (new stamp) refreshes the files the installer wrote and leaves the others alone */
    snprintf(p, sizeof(p), "%sindex.txt", gSrc);
    put(p, "stamp v2\n5 layout.json\n8 es-419/SSTALK00.json\n");
    snprintf(p, sizeof(p), "%ses-419/SSTALK00.json", gSrc);
    put(p, "[\"two\"]\n");
    snprintf(p, sizeof(p), "%slocalization/es-419/extra.json", base);
    put(p, "[]");
    CHECK(l10nInstall(gSrc, base, assetRead) == 2);
    snprintf(p, sizeof(p), "%slocalization/es-419/SSTALK00.json", base);
    CHECK(has(p, "[\"two\"]\n"));
    snprintf(p, sizeof(p), "%slocalization/es-419/extra.json", base);
    CHECK(has(p, "[]"));
    CHECK(l10nInstall(gSrc, base, assetRead) == 0);

    /* a listed file that the assets lack: failure, and the stamp is not written (so the next start tries again) */
    snprintf(p, sizeof(p), "%sindex.txt", gSrc);
    put(p, "stamp v3\n5 layout.json\n8 es-419/MISSING.json\n");
    CHECK(l10nInstall(gSrc, base, assetRead) < 0);
    snprintf(p, sizeof(p), "%slocalization/.stamp", base);
    CHECK(has(p, "v2"));

    snprintf(p, sizeof(p), "rm -rf '%s'", root);
    if (system(p) != 0) {
        fprintf(stderr, "note: could not remove %s\n", root);
    }
    if (failures == 0) {
        printf("l10ntest: OK\n");
        return 0;
    }
    return 1;
}
