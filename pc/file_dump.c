/*
 * Host harness: resolves a file index through the game's own file layer and
 * writes the file's sectors out.
 *
 *     file_dump <disc.bin> <file index> <out.bin>
 *
 * The index is resolved by the game's code, not by a table here:
 * FILE_TABLE.getSector/getSectorCount (src/engine/file/file_table.c) give its
 * sectors, and FILE_CACHE.load (src/engine/file/file_cache.c) reads it through
 * CD_READER, the host replacement for libcd (pc/shim/host_cd.c). What comes
 * out is the file as the game holds it in the heap: sectorCount * 2048 bytes,
 * every sector's 2048 data bytes, the last sector's padding included.
 *
 * pc/check_file.py compares this against an independent read of the same
 * index from the same image.
 */
#include "engine/file.h"
#include "shim/host.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    s32 file;
    s32 sector;
    s32 sectors;
    void *data;
    FILE *out;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <disc.bin> <file index> <out.bin>\n", argv[0]);
        return 2;
    }

    pcGfxInit();
    pcCdReaderInit(argv[1]);

    file = atoi(argv[2]);
    if (!FILE_TABLE.exists(file)) {
        fprintf(stderr, "%s: no file %d on the disc\n", argv[1], file);
        return 1;
    }
    sector = FILE_TABLE.getSector(file);
    sectors = FILE_TABLE.getSectorCount(file);

    /* The game's own path: the file cache queues the file and reads it through
       CD_READER, then hands back the heap block it filled. */
    data = FILE_CACHE.load(file);
    if (data == NULL) {
        fprintf(stderr, "file %d: the game's cache returned no data\n", file);
        return 1;
    }

    out = fopen(argv[3], "wb");
    if (out == NULL) {
        perror(argv[3]);
        return 1;
    }
    if (fwrite(data, 1, (size_t)sectors * CD_SECTOR_SIZE, out)
        != (size_t)sectors * CD_SECTOR_SIZE) {
        fprintf(stderr, "%s: write failed\n", argv[3]);
        fclose(out);
        return 1;
    }
    fclose(out);

    printf("file %d: sector %d, %d sectors (%d bytes)\n",
           file, sector, sectors, sectors * CD_SECTOR_SIZE);
    return 0;
}
