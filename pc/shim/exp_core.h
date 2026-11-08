/* The write policy and the manifest every exporter of the data folder shares (ADR-005, ADR-016): stages, raw, audio, models, tables,
   images and text. Pure stdlib plus text_data.h (hash, manifest file, atomic write) and port_log.h. */
#ifndef EXP_CORE_H
#define EXP_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "text_data.h"

/* what the sink did, per file: bytes counts the files it wrote (not the .new copies) */
typedef struct ExpStats {
    int written, kept, newFiles;
    long bytes;
} ExpStats;

/* The disc this run reads: the hash of its volume descriptor and path table (the `disc` of every manifest header), as 16 hex digits, or "" when no disc is
   open (the config manifest is read before the disc is chosen: it does not depend on one). expDiscOverride(hex) makes it answer `hex` instead (tests);
   NULL goes back to the open disc. */
void expDiscHex(char out[17]);
void expDiscOverride(const char *hex);

/* One topic's manifest file, loaded once and updated in place (stage, raw, audio, models, tables, content). `tag` is the log tag. */
typedef struct ExpManifest {
    TdManifest m;
    char path[1300];
    const char *tag;
    int loaded, dirty;
} ExpManifest;

/* A manifest file read into `out`: 1 when it parsed, 0 when it is missing or unreadable (logged; `out` is then empty). */
int expManRead(const char *tag, const char *path, TdManifest *out);
/* Writes a manifest file, making its folder. 0 or -1 and a log line. */
int expManWrite(const char *tag, const char *path, const TdManifest *m);
/* Adds to `next` what `old` knew and `next` does not: the files a run did not regenerate are still ours, and so are the names. */
void expManCarry(TdManifest *next, const TdManifest *old);

/* A fresh header for `topic` on the current disc: no versions, no files. */
void expManInit(TdManifest *m, const char *topic);
/* The loading rule of expManOpen for a manifest the caller keeps itself: `out` is the manifest read, or a fresh header for `topic`. Returns 1 when the
   header changed on loading (another disc), so it is worth writing. */
int expManLoad(const char *tag, const char *path, const char *topic, TdManifest *out);
/* Reads `path`. A missing manifest and one that is not asset-manifest-v1 (an old one, a damaged one) start an empty header for `topic`; a manifest of
   another disc keeps its files (so an edited one is still the user's) but loses its versions, so everything is exported again. With no disc open
   (expDiscHex gives "") the header is left as it is. */
void expManOpen(ExpManifest *man, const char *tag, const char *path, const char *topic);
void expManFree(ExpManifest *man);
/* Writes the file when loaded (and, with onlyIfDirty, changed). 0 when written or nothing to do, -1 and a log line on failure. */
int expManSave(ExpManifest *man, int onlyIfDirty);
/* Records key=hash; the manifest is dirty only when that changes it. */
void expManSet(ExpManifest *man, const char *key, const char *hash);

/* The exporter's version mark: header `exporter`[key] = version, where key names the exporter or one unit of it (a bank, a fighter). */
int expManIsCurrent(const ExpManifest *man, const char *key, unsigned version);
void expManMark(ExpManifest *man, const char *key, unsigned version);

int expFileExists(const char *path);

/* Where one run puts files and what it records. `old` is what earlier runs wrote (the files' owners), `next` what this run records:
   the same manifest for one updated in place (expSinkInPlace), or a fresh one that rebuilds the manifest of a run with a header to match (images,
   text), where `sameRun` says the earlier run had the same header (disc, exporter version, inputs) and an unedited file is not regenerated. */
typedef struct ExpSink {
    const char *tag; /* log tag */
    const char *dir; /* the folder the relative paths hang from */
    const TdManifest *old;
    TdManifest *next;
    int *dirty;      /* set when `next` changes (may be NULL) */
    int sameRun;
    ExpStats *stats; /* may be NULL */
} ExpSink;

void expSinkInPlace(ExpSink *sink, const char *tag, const char *dir, ExpManifest *man, ExpStats *stats);

/* One generated file <dir>/<rel> under the manifest rule of ADR-005: a copy that already holds these bytes is kept, an edited one
   (not the one the manifest says we wrote) stays and the new version goes beside it as <rel>.new, anything else is written atomically
   (the folders are made). 0 when handled, -1 on a failure (logged); the caller counts failures. */
int expPut(const ExpSink *sink, const char *rel, const void *bytes, size_t len);

#endif
