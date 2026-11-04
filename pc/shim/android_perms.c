/* Group-writable app folders for a PC's adb push (see android_perms.h). */
#define _GNU_SOURCE 1
#include "android_perms.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int androidPermsSame(mode_t have, mode_t want) {
    return (have & 0777) == (want & 0777);
}

static int fixEntry(const char *path, mode_t dirMode, mode_t fileMode, int depth) {
    struct stat st;
    int changed = 0;

    if (lstat(path, &st) != 0 || depth > 32) {
        return 0;
    }
    if (S_ISREG(st.st_mode)) {
        if (!androidPermsSame(st.st_mode, fileMode) && chmod(path, fileMode) == 0) {
            changed++;
        }
    } else if (S_ISDIR(st.st_mode)) {
        DIR *d;
        struct dirent *e;

        if (!androidPermsSame(st.st_mode, dirMode) && chmod(path, dirMode) == 0) {
            changed++;
        }
        d = opendir(path);
        if (d == NULL) {
            return changed;
        }
        while ((e = readdir(d)) != NULL) {
            char child[PATH_MAX];

            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) < (int)sizeof(child)) {
                changed += fixEntry(child, dirMode, fileMode, depth + 1);
            }
        }
        closedir(d);
    }
    return changed; /* symlinks and special files are left alone */
}

int androidPermsFix(const char *root, mode_t dirMode, mode_t fileMode) {
    struct stat st;

    if (root == NULL || stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return -1;
    }
    return fixEntry(root, dirMode, fileMode, 0);
}

void androidPermsUmask(void) {
    umask(000);
}
