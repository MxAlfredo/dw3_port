/*
 * Host harness: unpacks an RLEN file through the game's own decompressor.
 *
 *     rlen_tool <packed.rlen> <out.bin>
 *
 * It creates the game's Decompressor task (createDecompressor), points it at
 * the packed file and calls its run method (decompressorRun: decompressorSet-
 * Data, decompressorAllocBuffer, decompressorStep). The unpacked bytes come
 * from the task's buffer and go to <out.bin>. Nothing here reimplements the
 * RLEN logic: the bytes live in src/engine/file/decompressor.c.
 */
#include "engine/file.h"

#include <stdio.h>
#include <stdlib.h>

static u8 *readWholeFile(const char *path, long *outSize) {
    FILE *f = fopen(path, "rb");
    long size;
    u8 *buf;

    if (f == NULL) {
        perror(path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: cannot determine its size\n", path);
        fclose(f);
        return NULL;
    }
    buf = malloc(size > 0 ? (size_t)size : 1);
    if (buf == NULL) {
        fprintf(stderr, "%s: out of memory (%ld bytes)\n", path, size);
        fclose(f);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "%s: short read\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *outSize = size;
    return buf;
}

int main(int argc, char **argv) {
    long size;
    u8 *packed;
    Decompressor *task;
    void *unpacked;
    FILE *f;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <packed.rlen> <out.bin>\n", argv[0]);
        return 2;
    }

    packed = readWholeFile(argv[1], &size);
    if (packed == NULL) {
        return 1;
    }
    if (size < 8) {
        fprintf(stderr, "%s: %ld bytes, too small to be RLEN\n", argv[1], size);
        return 1;
    }

    task = createDecompressor();
    unpacked = task->run(task, (s32 *)packed);
    if (!task->compressed) {
        fprintf(stderr, "%s: not RLEN data\n", argv[1]);
        return 1;
    }

    f = fopen(argv[2], "wb");
    if (f == NULL) {
        perror(argv[2]);
        return 1;
    }
    if (task->size > 0 && fwrite(unpacked, 1, (size_t)task->size, f) != (size_t)task->size) {
        fprintf(stderr, "%s: write failed\n", argv[2]);
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("%s: %ld packed -> %d bytes unpacked\n", argv[1], size, task->size);
    return 0;
}
