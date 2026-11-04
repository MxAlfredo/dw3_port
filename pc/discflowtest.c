/*
 * Host test of the Android disc start's pure decisions (shim/disc_flow.c).
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>

#include "shim/disc_fd.h"
#include "shim/disc_flow.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* the disc read through a descriptor: a /proc/self/fd/<n> path gives a FILE with its own position, never a re-open of the path */
static void testFd(void) {
    char tmpl[] = "build/discfd-XXXXXX", path[64];
    unsigned char data[5000], got[8];
    int fd, n, i;
    FILE *a, *b;

    CHECK(dfdParse("/proc/self/fd/7", &n) && n == 7);
    CHECK(dfdParse("/proc/self/fd/123", &n) && n == 123);
    CHECK(!dfdParse("/proc/self/fd/", &n));
    CHECK(!dfdParse("/proc/self/fd/7x", &n));
    CHECK(!dfdParse("/proc/self/fd/-1", &n));
    CHECK(!dfdParse("/tmp/x.bin", &n));
    CHECK(!dfdParse(NULL, &n));
    CHECK(dfdOpen("/nonexistent/disc.bin") == NULL);

    for (i = 0; i < (int)sizeof(data); i++) {
        data[i] = (unsigned char)(i * 7 + i / 256);
    }
    fd = mkstemp(tmpl);
    CHECK(fd >= 0);
    if (fd < 0) {
        return;
    }
    CHECK(write(fd, data, sizeof(data)) == (ssize_t)sizeof(data));
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    a = dfdOpen(path);
    b = dfdOpen(path);
    CHECK(a != NULL && b != NULL);
    if (a != NULL && b != NULL) {
        /* two FILEs, two positions, one descriptor */
        CHECK(fseek(a, 4000, SEEK_SET) == 0 && fread(got, 1, 8, a) == 8 && memcmp(got, data + 4000, 8) == 0);
        CHECK(fseek(b, 16, SEEK_SET) == 0 && fread(got, 1, 8, b) == 8 && memcmp(got, data + 16, 8) == 0);
        CHECK(fread(got, 1, 8, a) == 8 && memcmp(got, data + 4008, 8) == 0);
        CHECK(fseek(a, -8, SEEK_END) == 0 && fread(got, 1, 8, a) == 8 && memcmp(got, data + sizeof(data) - 8, 8) == 0);
        CHECK(fread(got, 1, 8, a) == 0); /* the end */
        CHECK(fseek(a, 100, SEEK_SET) == 0 && fseek(a, 10, SEEK_CUR) == 0 && fread(got, 1, 8, a) == 8 && memcmp(got, data + 110, 8) == 0);
        fclose(a);
        /* closing one FILE leaves the other and the original descriptor alive */
        CHECK(fseek(b, 2352, SEEK_SET) == 0 && fread(got, 1, 8, b) == 8 && memcmp(got, data + 2352, 8) == 0);
        fclose(b);
        CHECK(lseek(fd, 0, SEEK_CUR) >= 0);
    }
    close(fd);
    unlink(tmpl);
}

int main(void) {
    char buf[600];
    int i;

    testFd();

    /* start order: remembered, then the app folder, then ask */
    CHECK(dfStartSource(1, 1) == DF_FROM_REMEMBERED);
    CHECK(dfStartSource(1, 0) == DF_FROM_REMEMBERED);
    CHECK(dfStartSource(0, 1) == DF_FROM_APP_FOLDER);
    CHECK(dfStartSource(0, 0) == DF_ASK);

    /* settings values */
    CHECK(dfIsContentUri("content://com.android.externalstorage.documents/document/primary%3ADownload%2Fgame.bin"));
    CHECK(!dfIsContentUri("/storage/emulated/0/Android/data/x/files/game.bin"));
    CHECK(!dfIsContentUri("game.bin"));
    CHECK(!dfIsContentUri(""));
    CHECK(!dfIsContentUri(NULL));

    /* .cue detection: the display name decides, the URI is the fallback */
    CHECK(dfPickIsCue("content://x/document/1234", "Game (Europe).CUE"));
    CHECK(dfPickIsCue("content://x/document/primary%3ADownload%2Fgame.cue", NULL));
    CHECK(dfPickIsCue("content://x/document/primary%3ADownload%2Fgame.cue", ""));
    CHECK(!dfPickIsCue("content://x/document/primary%3ADownload%2Fgame.cue", "game.bin")); /* the name wins */
    CHECK(!dfPickIsCue("content://x/document/primary%3ADownload%2Fgame.bin", ""));
    CHECK(!dfPickIsCue("content://x/document/1234", "game.iso"));
    CHECK(!dfPickIsCue(NULL, NULL));

    /* the retry loop */
    CHECK(dfNext(DF_EXPLAIN, DFE_CHOOSE) == DF_PICKING);
    CHECK(dfNext(DF_EXPLAIN, DFE_QUIT) == DF_QUIT);
    CHECK(dfNext(DF_PICKING, DFE_PICKED) == DF_DONE);
    CHECK(dfNext(DF_PICKING, DFE_CANCEL) == DF_CANCELED);
    CHECK(dfNext(DF_PICKING, DFE_BAD) == DF_REJECTED);
    CHECK(dfNext(DF_CANCELED, DFE_CHOOSE) == DF_PICKING);
    CHECK(dfNext(DF_CANCELED, DFE_QUIT) == DF_QUIT);
    CHECK(dfNext(DF_REJECTED, DFE_CHOOSE) == DF_PICKING);
    CHECK(dfNext(DF_REJECTED, DFE_QUIT) == DF_QUIT);
    /* a stray event never moves a phase that cannot take it (no busy loop) */
    CHECK(dfNext(DF_CANCELED, DFE_CANCEL) == DF_CANCELED);
    CHECK(dfNext(DF_DONE, DFE_CHOOSE) == DF_DONE);
    CHECK(dfNext(DF_QUIT, DFE_CHOOSE) == DF_QUIT);
    CHECK(dfNext(DF_PICKING, DFE_CHOOSE) == DF_PICKING);
    /* repeated cancels always end in a message, never in a loop that skips it */
    {
        DfPhase p = DF_EXPLAIN;

        p = dfNext(p, DFE_CHOOSE);
        for (i = 0; i < 5; i++) {
            p = dfNext(p, DFE_CANCEL);
            CHECK(p == DF_CANCELED);
            p = dfNext(p, DFE_CHOOSE);
            CHECK(p == DF_PICKING);
        }
    }

    /* messages: both languages, mention the .bin and Downloads */
    CHECK(strstr(dfMessage(DFM_EXPLAIN, 1, buf, sizeof(buf)), ".bin") != NULL);
    CHECK(strstr(buf, "Descargas") != NULL);
    CHECK(strstr(dfMessage(DFM_EXPLAIN, 0, buf, sizeof(buf)), "Downloads") != NULL);
    CHECK(strstr(dfMessage(DFM_CUE, 1, buf, sizeof(buf)), ".bin") != NULL);
    CHECK(strstr(dfMessage(DFM_CUE, 0, buf, sizeof(buf)), ".cue") != NULL);
    CHECK(dfMessage(DFM_CANCELED, 1, buf, sizeof(buf))[0] != '\0');
    CHECK(dfMessage(DFM_UNREADABLE, 0, buf, sizeof(buf))[0] != '\0');
    CHECK(strlen(dfMessage(DFM_EXPLAIN, 1, buf, 20)) < 20); /* truncates, never overruns */

    if (failures != 0) {
        fprintf(stderr, "discflowtest: %d failure(s)\n", failures);
        return 1;
    }
    printf("discflowtest: OK\n");
    return 0;
}
