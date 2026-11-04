/*
 * Host test of the permission fixer (pc/shim/android_perms.c): a tree made under a strict umask ends up group-writable,
 * a second pass changes nothing, symlinks are not followed and a missing root is reported.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/android_perms.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static unsigned modeOf(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 ? (unsigned)(st.st_mode & 07777) : 0xFFFFu;
}

static void touch(const char *path) {
    FILE *f = fopen(path, "wb");

    if (f != NULL) {
        fputs("x", f);
        fclose(f);
    }
}

int main(void) {
    char root[256], p[300], outside[300];
    mode_t old = umask(027);

    snprintf(root, sizeof(root), "/tmp/dw3-permstest-%d", (int)getpid());
    snprintf(outside, sizeof(outside), "%s-outside", root);
    mkdir(root, 0750);
    snprintf(p, sizeof(p), "%s/mods", root);
    mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/mods/inner", root);
    mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/mods/inner/a.txt", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/b.txt", root);
    touch(p);
    mkdir(outside, 0700);
    snprintf(p, sizeof(p), "%s/link", root);
    CHECK(symlink(outside, p) == 0);

    CHECK((modeOf(root) & 070) == 050); /* the strict umask really made it unwritable for the group */
    CHECK(androidPermsFix(root, ANDROID_PERMS_DIR_MODE, ANDROID_PERMS_FILE_MODE) == 5);
    CHECK((modeOf(root) & 0777) == 0777); /* setgid (02000) may or may not stick, so only the permission bits are checked */
    snprintf(p, sizeof(p), "%s/mods/inner", root);
    CHECK((modeOf(p) & 0777) == 0777);
    snprintf(p, sizeof(p), "%s/mods/inner/a.txt", root);
    CHECK(modeOf(p) == 0666);
    snprintf(p, sizeof(p), "%s/b.txt", root);
    CHECK(modeOf(p) == 0666);
    CHECK(modeOf(outside) == 0700); /* the link target was not followed */
    CHECK(androidPermsFix(root, ANDROID_PERMS_DIR_MODE, ANDROID_PERMS_FILE_MODE) == 0);
    CHECK(androidPermsFix("/nonexistent/dw3-permstest", 02777, 0666) == -1);

    /* the same-mode test ignores setgid: a FUSE path that strips it must not recount every folder at each start */
    CHECK(androidPermsSame(0777, 02777) && androidPermsSame(02777, 0777) && androidPermsSame(0666, 0666));
    CHECK(!androidPermsSame(0770, 02777) && !androidPermsSame(0660, 0666));

    /* what the app creates after androidPermsUmask() is readable and writable by the shell user (log files over adb) */
    androidPermsUmask();
    snprintf(p, sizeof(p), "%s/logs", root);
    CHECK(mkdir(p, ANDROID_PERMS_DIR_MODE & 0777) == 0 && (modeOf(p) & 0777) == 0777);
    snprintf(p, sizeof(p), "%s/logs/session.log", root);
    touch(p);
    CHECK(modeOf(p) == 0666);

    umask(old);
    snprintf(p, sizeof(p), "rm -rf '%s' '%s'", root, outside);
    if (system(p) != 0) {
        fprintf(stderr, "note: could not remove %s\n", root);
    }
    if (failures == 0) {
        printf("permstest: OK\n");
        return 0;
    }
    return 1;
}
