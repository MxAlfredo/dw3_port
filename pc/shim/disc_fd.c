/* T7b: reading the disc through a file descriptor (see disc_fd.h). */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "disc_fd.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

int dfdParse(const char *path, int *fd) {
    static const char prefix[] = "/proc/self/fd/";
    const char *p;
    long v = 0;

    if (path == NULL || strncmp(path, prefix, sizeof(prefix) - 1) != 0) {
        return 0;
    }
    p = path + sizeof(prefix) - 1;
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    for (; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p) || v > 100000) {
            return 0;
        }
        v = v * 10 + (*p - '0');
    }
    *fd = (int)v;
    return 1;
}

#if defined(__linux__)
typedef struct {
    int fd;
    long long pos;
} FdCookie;

static int fdReadAt(FdCookie *k, char *buf, size_t n) {
    ssize_t got = pread(k->fd, buf, n, (off_t)k->pos);

    if (got > 0) {
        k->pos += got;
    }
    return (int)got;
}

static long long fdSeekTo(FdCookie *k, long long offset, int whence) {
    long long base;

    if (whence == SEEK_SET) {
        base = 0;
    } else if (whence == SEEK_CUR) {
        base = k->pos;
    } else if (whence == SEEK_END) {
        struct stat st;

        if (fstat(k->fd, &st) != 0) {
            return -1;
        }
        base = (long long)st.st_size;
    } else {
        return -1;
    }
    if (base + offset < 0) {
        return -1;
    }
    k->pos = base + offset;
    return k->pos;
}

static int fdCloseCookie(void *c) {
    FdCookie *k = (FdCookie *)c;

    close(k->fd);
    free(k);
    return 0;
}

#ifdef __ANDROID__
/* bionic has funopen, not fopencookie */
static int fdReadF(void *c, char *buf, int n) {
    return fdReadAt((FdCookie *)c, buf, (size_t)n);
}

static fpos_t fdSeekF(void *c, fpos_t offset, int whence) {
    return (fpos_t)fdSeekTo((FdCookie *)c, (long long)offset, whence);
}
#else
static ssize_t fdReadG(void *c, char *buf, size_t n) {
    return fdReadAt((FdCookie *)c, buf, n);
}

static int fdSeekG(void *c, off64_t *offset, int whence) {
    long long r = fdSeekTo((FdCookie *)c, (long long)*offset, whence);

    if (r < 0) {
        return -1;
    }
    *offset = (off64_t)r;
    return 0;
}
#endif

FILE *dfdOpen(const char *path) {
    int fd;
    FdCookie *k;
    FILE *f;

    if (!dfdParse(path, &fd)) {
        return fopen(path, "rb");
    }
    k = (FdCookie *)malloc(sizeof(*k));
    if (k == NULL) {
        return NULL;
    }
    k->fd = dup(fd);
    k->pos = 0;
    if (k->fd < 0) {
        free(k);
        return NULL;
    }
#ifdef __ANDROID__
    f = funopen(k, fdReadF, NULL, fdSeekF, fdCloseCookie);
#else
    {
        cookie_io_functions_t io;

        memset(&io, 0, sizeof(io));
        io.read = fdReadG;
        io.seek = fdSeekG;
        io.close = fdCloseCookie;
        f = fopencookie(k, "rb", io);
    }
#endif
    if (f == NULL) {
        close(k->fd);
        free(k);
    }
    return f;
}
#else
FILE *dfdOpen(const char *path) {
    return fopen(path, "rb");
}
#endif
