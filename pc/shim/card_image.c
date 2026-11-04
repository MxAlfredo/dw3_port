/* PS1 memory card images (see card_image.h) */
#include "card_image.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GME_HEADER 0xF40
#define VMP_HEADER 0x80

int cardImageNormalize(const unsigned char *in, size_t len, unsigned char *out, const char **why) {
    size_t skip;

    if (len == CARD_IMAGE_SIZE) {
        skip = 0;
    } else if (len == CARD_IMAGE_SIZE + GME_HEADER && memcmp(in, "123-456-STD", 11) == 0) {
        skip = GME_HEADER;
    } else if (len == CARD_IMAGE_SIZE + VMP_HEADER && memcmp(in, "\0PMV", 4) == 0) {
        skip = VMP_HEADER;
    } else {
        *why = "wrong size (a PS1 card is 128 KB)";
        return 0;
    }
    if (in[skip] != 'M' || in[skip + 1] != 'C') {
        *why = "not a PS1 memory card (no MC header)";
        return 0;
    }
    memcpy(out, in + skip, CARD_IMAGE_SIZE);
    return 1;
}

int cardImageRead(const char *path, unsigned char *out, const char **why) {
    static unsigned char buf[CARD_IMAGE_MAX_FILE + 1];
    FILE *f = fopen(path, "rb");
    size_t got;

    if (f == NULL) {
        *why = "the file cannot be opened";
        return 0;
    }
    got = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (got > CARD_IMAGE_MAX_FILE) {
        *why = "wrong size (a PS1 card is 128 KB)";
        return 0;
    }
    return cardImageNormalize(buf, got, out, why);
}

int cardImageHasExt(const char *name) {
    static const char *const exts[] = {".mcd", ".mcr", ".mc", ".gme", ".vmp", ".srm"};
    size_t n = strlen(name), i;

    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t e = strlen(exts[i]), k;

        if (n <= e) {
            continue;
        }
        for (k = 0; k < e && tolower((unsigned char)name[n - e + k]) == exts[i][k]; k++) {
        }
        if (k == e) {
            return 1;
        }
    }
    return 0;
}

typedef struct Cand {
    char path[PP_CAP];
    long long mtime;
} Cand;

static int newestFirst(const void *a, const void *b) {
    const Cand *x = (const Cand *)a, *y = (const Cand *)b;

    if (x->mtime != y->mtime) {
        return x->mtime < y->mtime ? 1 : -1;
    }
    return strcmp(x->path, y->path);
}

static void scanDir(const char *dir, Cand *c, int *count, int cap) {
    DIR *d = opendir(dir);
    struct dirent *e;

    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL && *count < cap) {
        char full[PP_CAP];
        struct stat st;

        if (!cardImageHasExt(e->d_name) || !portJoin(full, sizeof(full), dir, e->d_name) || stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        if (st.st_size < CARD_IMAGE_SIZE || st.st_size > CARD_IMAGE_MAX_FILE) {
            continue;
        }
        snprintf(c[*count].path, PP_CAP, "%s", full);
        c[*count].mtime = (long long)st.st_mtime;
        (*count)++;
    }
    closedir(d);
}

int cardImageFind(char out[][PP_CAP], int max) {
    static Cand cands[64];
    static char dirs[16][PP_CAP];
    char imp[PP_CAP];
    int count = 0, n, i;

    if (max <= 0) {
        return 0;
    }
    if (portPath(PP_IMPORT, imp, sizeof(imp))) {
        scanDir(imp, cands, &count, 64);
    }
    n = portPathsEmulatorDirs(dirs, 16);
    for (i = 0; i < n; i++) {
        scanDir(dirs[i], cands, &count, 64);
    }
    qsort(cands, (size_t)count, sizeof(cands[0]), newestFirst);
    if (count > max) {
        count = max;
    }
    for (i = 0; i < count; i++) {
        snprintf(out[i], PP_CAP, "%s", cands[i].path);
    }
    return count;
}
