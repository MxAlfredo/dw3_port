#ifndef PC_RAW_EXPORT_H
#define PC_RAW_EXPORT_H

/*
 * The disc files no other exporter covers, as raw copies (content-export E7, ADR-005 / ADR-010). Text, images, models, video, audio,
 * the tables and the field stage tiles each have an exporter that writes open files; what is left (the field collision maps, the battle
 * effects, the training files, the stage demos, the sprite sheets, the card data nobody reads yet, the overlays, ...) is copied byte for
 * byte to
 *
 *   <data>/raw/<disc path without AAA/, lower case>     e.g. raw/dat/field/stage/s233tmpk.bin
 *   <data>/raw/README.txt                                what this folder is (generated, English)
 *   <data>/raw/raw.json                                  "raw-v1": the kinds (what we know, who reads them, how to replace them) and every file
 *   <data>/raw/manifest.json                             what the port wrote (hash of every file, exporter version per kind), the rule of
 *                                                        the other data/ families
 *
 * A raw copy is not read by the game from data/. To change one, copy it to mods/<mod>/files/<same path> (port_fs.h: the raw override);
 * raw.json says per kind whether the game reads the file that way. The covered families are decided by the same predicates the other
 * exporters use (image_data.h, sprite_data.h), so a new exporter takes its files out of here by changing one place (rawKindOf).
 *
 * About 15 MB for the EU disc, so it runs at start, before the window opens, and not from a menu: the [data] export_raw key of settings.ini
 * ("all" when absent, "off", or kinds separated by commas). An edited file stays and the new one goes beside it as <file>.new. The manifest
 * holds the exporter version per kind: a kind of the current version whose files are all there is skipped without reading the disc.
 * Bump RAW_EXPORT_VERSION when the bytes or the kinds change.
 */

#include <stddef.h>
#include <stdint.h>

#define RAW_EXPORT_VERSION 1

typedef struct RawKind {
    const char *id;       /* "stage-map": what the key of settings.ini and raw.json call it */
    const char *pattern;  /* where its files are, in words */
    const char *know;     /* what we know about the format */
    const char *usedBy;   /* who reads it in the game */
    const char *replace;  /* how to replace it in a mod, and whether the port honours it */
    const char *works;    /* "yes" (the game reads it through the file layer), "no" (it does not) or "unverified" */
} RawKind;

/* The kinds, in a fixed order; *n is how many */
const RawKind *rawKinds(int *n);
/* The kind of a disc path ("AAA/DAT/FIELD/STAGE/S233TMPK.BIN"), or NULL when another exporter covers the file or it is not game data
   (outside AAA/: the executable and the disc's own files). */
const RawKind *rawKindOf(const char *discPath);
/* "AAA/DAT/FIELD/STAGE/S233TMPK.BIN" -> "dat/field/stage/s233tmpk.bin" (0: not under AAA/ or too long) */
int rawRelPath(const char *discPath, char *out, size_t cap);

/* Where the disc is and how to read it. Only called by the thread that runs the job. */
typedef struct RawExportIo {
    /* the i-th file of the disc ("AAA/DAT/..." as on the disc, first sector, exact size); 1 when there is one, 0 past the last */
    int (*list)(void *ud, int i, char *path, size_t cap, long *lba, long *size);
    /* one raw 2352-byte frame of the disc image; 1 when read */
    int (*read)(void *ud, long lba, unsigned char *frame);
    void *ud;
} RawExportIo;

typedef struct RawExportStats {
    int kinds;       /* kinds requested that have files on the disc */
    int skipped;     /* of those, how many were up to date (no disc read) */
    int files;       /* files looked at in the kinds that were built */
    int written;     /* files written */
    int kept;        /* files already exactly as they would be written */
    int newFiles;    /* edited files that stayed, the new version went beside them as .new */
    int failed;      /* files that could not be copied */
    long bytes;      /* bytes of the files written */
} RawExportStats;

void rawExportSetup(const char *dataDir, const RawExportIo *io);
/* Copies "all" or the kinds named in spec (separated by commas or spaces). Synchronous. The number of kinds with files, or -1 when
   there is nothing to do (no data folder, no reader, nothing valid). */
int rawExportRun(const char *spec);
void rawExportTotals(RawExportStats *out);

#endif
