/* Installs the APK's es-419 overlay into the user folder (see l10n_install.h). */
#include "l10n_install.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_paths.h"

int l10nIndexStamp(const char *text, char *out, int cap) {
    const char *p = text;

    while (p != NULL && *p != '\0') {
        const char *e = strchr(p, '\n');
        size_t n = e != NULL ? (size_t)(e - p) : strlen(p);

        while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ')) {
            n--;
        }
        if (n > 6 && strncmp(p, "stamp ", 6) == 0) {
            return snprintf(out, (size_t)cap, "%.*s", (int)(n - 6), p + 6) < cap;
        }
        p = e != NULL ? e + 1 : NULL;
    }
    return 0;
}

static int safeRel(const char *s) {
    if (s[0] == '\0' || s[0] == '/' || s[0] == '\\') {
        return 0;
    }
    return strstr(s, "..") == NULL && strchr(s, ':') == NULL;
}

int l10nIndexNext(const char **cursor, char *rel, int cap, long *size) {
    const char *p = *cursor;

    while (p != NULL && *p != '\0') {
        const char *e = strchr(p, '\n');
        size_t n = e != NULL ? (size_t)(e - p) : strlen(p);
        char line[512], *end;
        long sz;

        memcpy(line, p, n < sizeof(line) ? n : sizeof(line) - 1);
        line[n < sizeof(line) ? n : sizeof(line) - 1] = '\0';
        p = e != NULL ? e + 1 : NULL;
        while (line[0] != '\0' && (line[strlen(line) - 1] == '\r' || line[strlen(line) - 1] == ' ')) {
            line[strlen(line) - 1] = '\0';
        }
        sz = strtol(line, &end, 10);
        if (end == line || *end != ' ' || sz < 0 || !safeRel(end + 1) || (int)strlen(end + 1) >= cap) {
            continue;
        }
        strcpy(rel, end + 1);
        *size = sz;
        *cursor = p != NULL ? p : "";
        return 1;
    }
    *cursor = "";
    return 0;
}

static int readText(L10nRead read, const char *name, char **text) {
    uint8_t *d = NULL;
    size_t n = 0;

    if (read(name, &d, &n) != 0) {
        return 0;
    }
    *text = (char *)realloc(d, n + 1);
    if (*text == NULL) {
        return 0;
    }
    (*text)[n] = '\0';
    return 1;
}

static int writeFile(const char *path, const uint8_t *d, size_t n) {
    FILE *f;
    int ok;

    portMkdirsFor(path);
    f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    ok = fwrite(d, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static int fileThere(const char *path, long size) {
    FILE *f = fopen(path, "rb");
    long n;

    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n >= 0 && size >= 0 && (n > 0 || size == 0);
}

int l10nInstall(const char *srcPrefix, const char *base, L10nRead read) {
    char name[1024], dst[1024], rel[256], stamp[128], have[128];
    char *index = NULL;
    const char *cur;
    long size;
    int current, written = 0;

    snprintf(name, sizeof(name), "%sindex.txt", srcPrefix);
    if (!readText(read, name, &index)) {
        return 0;
    }
    if (!l10nIndexStamp(index, stamp, sizeof(stamp))) {
        free(index);
        return -1;
    }
    snprintf(dst, sizeof(dst), "%slocalization/.stamp", base);
    have[0] = '\0';
    {
        FILE *f = fopen(dst, "rb");

        if (f != NULL) {
            size_t n = fread(have, 1, sizeof(have) - 1, f);

            have[n] = '\0';
            fclose(f);
        }
    }
    current = strcmp(have, stamp) == 0;
    cur = index;
    while (l10nIndexNext(&cur, rel, sizeof(rel), &size)) {
        uint8_t *d = NULL;
        size_t n = 0;

        snprintf(dst, sizeof(dst), "%slocalization/%s", base, rel);
        if (current && fileThere(dst, size)) {
            continue;
        }
        snprintf(name, sizeof(name), "%s%s", srcPrefix, rel);
        if (read(name, &d, &n) != 0 || !writeFile(dst, d, n)) {
            fprintf(stderr, "l10n: could not install %s\n", rel);
            free(d);
            free(index);
            return -1;
        }
        free(d);
        written++;
    }
    free(index);
    if (written > 0 || !current) {
        snprintf(dst, sizeof(dst), "%slocalization/.stamp", base);
        if (!writeFile(dst, (const uint8_t *)stamp, strlen(stamp))) {
            return -1;
        }
    }
    return written;
}
