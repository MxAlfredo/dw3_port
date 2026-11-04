/*
 * dw3save-v1 container: see save_file.h.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
/* not <windows.h>: the port's Windows build already defines u_long & co. itself (win/bsd_types.h) */
__declspec(dllimport) int __stdcall MoveFileExA(const char *from, const char *to, unsigned long flags);
#define MOVEFILE_REPLACE_EXISTING 0x1
#define MOVEFILE_WRITE_THROUGH 0x8
#else
#include <unistd.h>
#endif

#include "port_log.h"
#include "save_file.h"

#define HEADER_SIZE 24
#define MAGIC "DW3SAVE"

static uint32_t crcTable[256];
static int crcReady;

uint32_t saveCrc32(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;

    if (!crcReady) {
        uint32_t n, k;

        for (n = 0; n < 256; n++) {
            uint32_t c = n;

            for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTable[n] = c;
        }
        crcReady = 1;
    }
    for (i = 0; i < len; i++) crc = crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int saveFileAdd(SaveFile *f, const char tag[4], const void *data, size_t size) {
    if (f->count >= SAVEFILE_MAX_SECTIONS) {
        plogLine("saves", "section %.4s dropped: the save already holds %d sections (the most a file takes)\n", tag, SAVEFILE_MAX_SECTIONS);
        return 0;
    }
    memcpy(f->sec[f->count].tag, tag, 4);
    f->sec[f->count].size = (uint32_t)size;
    f->sec[f->count].data = data;
    f->count++;
    return 1;
}

const SaveSection *saveFileFind(const SaveFile *f, const char tag[4]) {
    int i;

    for (i = 0; i < f->count; i++) {
        if (memcmp(f->sec[i].tag, tag, 4) == 0) return &f->sec[i];
    }
    return NULL;
}

size_t saveFileEncode(const SaveFile *f, uint8_t *out, size_t cap) {
    size_t total = HEADER_SIZE + 4;
    size_t pos;
    int i;

    for (i = 0; i < f->count; i++) total += 8 + f->sec[i].size;
    if (out == NULL || cap < total) return total;
    memset(out, 0, 8);
    memcpy(out, MAGIC, 7);
    put32(out + 8, SAVEFILE_FORMAT_VERSION);
    put32(out + 12, f->portVersion != 0 ? f->portVersion : SAVEFILE_PORT_VERSION);
    put32(out + 16, (uint32_t)f->count);
    put32(out + 20, (uint32_t)total);
    pos = HEADER_SIZE;
    for (i = 0; i < f->count; i++) {
        memcpy(out + pos, f->sec[i].tag, 4);
        put32(out + pos + 4, f->sec[i].size);
        if (f->sec[i].size != 0) memcpy(out + pos + 8, f->sec[i].data, f->sec[i].size);
        pos += 8 + f->sec[i].size;
    }
    put32(out + pos, saveCrc32(out, pos));
    return total;
}

int saveFileDecode(const uint8_t *buf, size_t len, SaveFile *out) {
    uint32_t count, total, version;
    size_t pos;
    uint32_t i;

    memset(out, 0, sizeof(*out));
    if (len < HEADER_SIZE + 4) return SAVEFILE_ERR_SHORT;
    if (memcmp(buf, MAGIC, 8) != 0) return SAVEFILE_ERR_MAGIC;
    version = get32(buf + 8);
    if (version == 0 || version > SAVEFILE_FORMAT_VERSION) return SAVEFILE_ERR_VERSION;
    total = get32(buf + 20);
    if (total != len) return SAVEFILE_ERR_SHORT;
    if (saveCrc32(buf, len - 4) != get32(buf + len - 4)) return SAVEFILE_ERR_CRC;
    count = get32(buf + 16);
    if (count > SAVEFILE_MAX_SECTIONS) return SAVEFILE_ERR_FORMAT;
    out->formatVersion = version;
    out->portVersion = get32(buf + 12);
    pos = HEADER_SIZE;
    for (i = 0; i < count; i++) {
        uint32_t size;

        if (pos + 8 > len - 4) return SAVEFILE_ERR_FORMAT;
        size = get32(buf + pos + 4);
        if (size > len - 4 - (pos + 8)) return SAVEFILE_ERR_FORMAT;
        memcpy(out->sec[i].tag, buf + pos, 4);
        out->sec[i].size = size;
        out->sec[i].data = buf + pos + 8;
        pos += 8 + size;
    }
    if (pos != len - 4) return SAVEFILE_ERR_FORMAT;
    out->count = (int)count;
    return SAVEFILE_OK;
}

const char *saveFileErrorName(int err) {
    switch (err) {
    case SAVEFILE_OK: return "ok";
    case SAVEFILE_ERR_MISSING: return "missing";
    case SAVEFILE_ERR_SHORT: return "cut short";
    case SAVEFILE_ERR_MAGIC: return "not a dw3save";
    case SAVEFILE_ERR_VERSION: return "newer format";
    case SAVEFILE_ERR_CRC: return "checksum mismatch";
    case SAVEFILE_ERR_FORMAT: return "bad section table";
    case SAVEFILE_ERR_IO: return "cannot write";
    }
    return "?";
}

/* Whole file into a malloc'd buffer; NULL when it cannot be read */
static uint8_t *slurp(const char *path, size_t *len) {
    FILE *fp = fopen(path, "rb");
    long size;
    uint8_t *buf;

    if (fp == NULL) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 || size > 64 * 1024 * 1024 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    *len = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    return buf;
}

/* rename() that replaces an existing destination on every host */
static int replaceFile(const char *from, const char *to) {
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(from, to);
#endif
}

static int fileUsable(const char *path) {
    size_t len = 0;
    uint8_t *buf = slurp(path, &len);
    SaveFile tmp;
    int err;

    if (buf == NULL) return 0;
    err = saveFileDecode(buf, len, &tmp);
    free(buf);
    return err == SAVEFILE_OK;
}

int saveFileWrite(const char *path, const SaveFile *f) {
    char tmp[1100], bak[1100];
    size_t need = saveFileEncode(f, NULL, 0);
    uint8_t *buf = malloc(need);
    FILE *fp;
    int ok;

    if (buf == NULL) return SAVEFILE_ERR_IO;
    saveFileEncode(f, buf, need);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    snprintf(bak, sizeof(bak), "%s.bak", path);
    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        free(buf);
        plogLine("saves", "cannot write %s: %s\n", tmp, strerror(errno));
        return SAVEFILE_ERR_IO;
    }
    ok = fwrite(buf, 1, need, fp) == need;
    ok = (fflush(fp) == 0) && ok;
#ifdef _WIN32
    ok = (_commit(_fileno(fp)) == 0) && ok;
#else
    ok = (fsync(fileno(fp)) == 0) && ok;
#endif
    ok = (fclose(fp) == 0) && ok;
    free(buf);
    if (!ok) {
        remove(tmp);
        plogLine("saves", "cannot write %s: %s\n", tmp, strerror(errno));
        return SAVEFILE_ERR_IO;
    }
    /* the previous file becomes the fallback only while it is itself good */
    if (fileUsable(path)) {
        if (replaceFile(path, bak) != 0) plogLine("saves", "cannot keep %s as %s: %s\n", path, bak, strerror(errno));
    }
    if (replaceFile(tmp, path) != 0) {
        plogLine("saves", "cannot move %s to %s: %s\n", tmp, path, strerror(errno));
        remove(tmp);
        return SAVEFILE_ERR_IO;
    }
    return SAVEFILE_OK;
}

int saveFileRead(const char *path, SaveFile *out) {
    char bak[1100];
    size_t len = 0;
    uint8_t *buf;
    int err;

    memset(out, 0, sizeof(*out));
    buf = slurp(path, &len);
    if (buf != NULL) {
        err = saveFileDecode(buf, len, out);
        if (err == SAVEFILE_OK) {
            out->owned = buf;
            return SAVEFILE_OK;
        }
        free(buf);
    } else {
        err = SAVEFILE_ERR_MISSING;
    }
    snprintf(bak, sizeof(bak), "%s.bak", path);
    buf = slurp(bak, &len);
    if (buf != NULL) {
        int err2 = saveFileDecode(buf, len, out);

        if (err2 == SAVEFILE_OK) {
            out->owned = buf;
            out->usedBackup = 1;
            plogLine("saves", "%s is unusable (%s); using the previous copy %s\n", path, saveFileErrorName(err), bak);
            return SAVEFILE_OK;
        }
        free(buf);
    }
    memset(out, 0, sizeof(*out));
    if (err != SAVEFILE_ERR_MISSING) plogLine("saves", "%s is unusable (%s) and has no good previous copy\n", path, saveFileErrorName(err));
    return err;
}

void saveFileFree(SaveFile *f) {
    free(f->owned);
    memset(f, 0, sizeof(*f));
}
