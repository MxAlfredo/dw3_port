#ifndef PC_STAGE_EXPORT_H
#define PC_STAGE_EXPORT_H

/*
 * The field stage tiles as open files (content-export E6b, ADR-005 / ADR-010). AAA/DAT/Z_STAGE/S<nnn>PACK.BIN is the map art of a field
 * stage, streamed tile by tile while the player walks (src/field/field_mode/map_streamer.c, stream.c). Each PACK is written to
 *
 *   <data>/stages/s233/tiles/<RR>_<CC>.png   one 8-bit indexed PNG per tile (its own 256 colour CLUT), RR = row, CC = column, 128 px high
 *   <data>/stages/s233/tiles/tiles.json      "stages-tiles-v1": the tile grid and, per tile, how it is stored and what the game draws from it
 *   <data>/stages/manifest.json              what the port wrote (hash of every file, exporter version "stage-tiles/s233"), the one manifest of the topic
 *                                            (the stage data and the objects write to it too)
 *
 * The folder was data/z_stage/s233pack/ until readable-assets A7.2 (with its own manifest.json); nothing reads it any more and the exporter leaves it alone,
 * with one log line when it is there.
 *
 * Disc format (evidence in docs/formats.md):
 *   sector 0   s32 tileCount, s32 width, s32 height (in 128 px tiles), s32 tileBytes (the room of a tile), u16 tileSizes[width*height]
 *              (0 = no tile). Tile `frame` = row * width + col lives at sector frame * tileBytes / 2048 + 1.
 *   a tile     RLEN packed ("RLEN", the unpacked size, runs 0x80|n+byte / n+bytes, 0 ends) or plain; unpacked: word 0 = the offset of the
 *              TIM, then three sprite lists, one per depth (s32 count, then count records of s16 x, y, u, v, w, h; the game draws at most
 *              five), then at that offset an 8-bit TIM, 128 rows high.
 *
 * The PNG carries the pixels and the palette; the JSON carries the rest, so an edited PNG replaces the tile through the host hook
 * (pc/field/stream.c, stage_mod.c: mods/<mod>/stages/s233/tiles/<RR>_<CC>.png, same rules as the other images).
 *
 * On demand only (about 12000 files): the System tab's "Export stages" row and the [data] export_stages key of settings.ini ("all" or
 * stage names like S233 separated by commas). The menu's job runs on a worker thread (stageExportStart); the settings.ini key at start
 * runs synchronously. An edited file stays and the new one goes beside it as <file>.new. The manifest header also holds the exporter version
 * that wrote the stage: a request skips a stage of the current version whose tiles.json exists; the menu row forces a rebuild.
 * Bump STAGE_EXPORT_VERSION when the output bytes change.
 */

#include <stddef.h>
#include <stdint.h>

#define STAGE_EXPORT_VERSION 2
#define STAGE_MAX_TILES 1016   /* the tile table has to fit sector 0: (2048 - 16) / 2 */
#define STAGE_SPRITES 5        /* the records of a depth the game reads */
#define STAGE_MAX_UNPACKED 0x100000

typedef struct PackSprite {
    int x, y, u, v, w, h;
} PackSprite;

typedef struct PackHeader {
    int tileCount, width, height, tileBytes;
    uint16_t sizes[STAGE_MAX_TILES];
} PackHeader;

/* Sector 0 of a PACK (2048 bytes). 0, or -1 with the reason in err. */
int stagePackParse(const uint8_t *sector, size_t len, PackHeader *out, char *err, size_t errCap);

typedef struct PackTile {
    int rle;                              /* 1: RLEN packed, 0: plain */
    size_t timOff, timLen;                /* the TIM inside `data` */
    int count[3];                         /* records per depth, as the tile says (the game uses the first STAGE_SPRITES) */
    PackSprite sp[3][STAGE_SPRITES];
    uint8_t *data;                        /* the unpacked tile, malloc'ed */
    size_t len;
} PackTile;

/* One tile as read from the disc (`raw`, up to tileBytes). 0, or -1 with the reason in err. */
int stageTileParse(const uint8_t *raw, size_t rawLen, PackTile *t, char *err, size_t errCap);
void stageTileFree(PackTile *t);

/* The bytes a TIM takes (header, CLUT block, image block); 0 when it is not a TIM or does not fit in `max` */
size_t stageTimLength(const uint8_t *tim, size_t max);

/* "05_02.png" (row, column) for tile `frame` of a PACK `width` tiles wide; the folder is the stage's tiles/ ("s233/tiles") */
void stageTileName(int width, int frame, char *out, size_t cap);
/* "S233PACK", "s233", "S233PACK.BIN" -> "s233pack" (lower case, 0 when it is not a stage name) */
int stagePackKey(const char *name, char *out, size_t cap);

/* ---- the job: where the files go and which stages ---- */

/* Where the stages are and how to read the disc. Only called by the thread that runs the job. */
typedef struct StageExportIo {
    /* the i-th PACK of the disc ("S233PACK", its first sector and size in bytes); 1 when there is one, 0 past the last */
    int (*pack)(void *ud, int i, char *name, size_t cap, long *lba, long *size);
    /* one raw 2352-byte frame of the disc image; 1 when read */
    int (*read)(void *ud, long lba, unsigned char *frame);
    void *ud;
} StageExportIo;

typedef struct StageExportStats {
    int tiles;      /* tiles with data */
    int written;    /* files written (PNG and JSON) */
    int kept;       /* files already exactly as they would be written */
    int newFiles;   /* edited files that stayed, the new version went beside them as .new */
    int failed;     /* tiles or files that could not be exported */
    long bytes;     /* bytes of the files written */
} StageExportStats;

void stageExportSetup(const char *dataDir, const StageExportIo *io);
/* Queues "all" or stage names separated by commas/spaces. The queued count, or -1 when there is nothing to do (no data folder, no
   reader, nothing valid, a job is running). */
int stageExportRequest(const char *spec);
int stageExportRequestEx(const char *spec, int force);
/* How many stages of the last job were really built (the rest were skipped as up to date) */
int stageExportBuilt(void);
/* totals of the last job */
void stageExportTotals(StageExportStats *out);
/* The settings.ini key: stageExportRequest, then builds the queue now, on this thread. The queued count, or -1. */
int stageExportRun(const char *spec);
/* The menu's job: stageExportRequestEx, then a worker thread builds the queue. Then call stageExportPump every frame */
int stageExportStart(const char *spec, int force);
/* Stops a running worker between two tiles and joins it; atexit-safe */
void stageExportShutdown(void);
/* Exports queued stages for about budgetMs milliseconds (at least one) when there is no worker; with one it only joins it at the end.
   1 while work remains, 0 when the job is over. */
int stageExportPump(unsigned budgetMs);
int stageExportActive(void);
void stageExportCounts(int *done, int *failed, int *total);
/* The row's value text: "" before any job, "Exporting 3/237", "Done (237)", "Done, 2 failed" */
const char *stageExportStatus(void);

#endif
