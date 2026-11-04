#ifndef PC_PORT_FS_H
#define PC_PORT_FS_H

/*
 * The port's common file layer (ADR-005 section 6, feature port-filesystem T1).
 *
 * Every read the game makes of a disc file goes through cdRead (host_cd.c) by file number. This unit
 * knows each number's disc path (the ISO9660 directory, matched to the game's FILE_TABLE by first
 * sector, the rule of tools/file_names.py) and decides where its bytes come from, in this order:
 *
 *   1. the text layer      (text_data.c: es-419, an edited data/ file, a mod's text/ JSON)
 *   2. the image layer     (image_data.c): when it substitutes images inside the file, the file is its
 *                          own and a raw override is ignored (the substitution works on the disc
 *                          file's offsets)
 *   3. a raw override      mods/<mod>/files/<disc path without "AAA/">, the last active mod first;
 *                          the port writes and documents lower case, any case is read (ADR-010)
 *   4. a generated file     (pfsSetGenerated: the port's own bytes, built at start from the disc file and the content
 *                          mods; it may be LONGER than the disc file, which is how the content mods add rows to a table
 *                          file without a size cap; a raw override of the same number wins and the generated file is
 *                          ignored, with one log line)
 *   5. the disc
 *
 * Open formats win over a raw file of the same number; a mod that ships both gets one log line
 * (`files: <path> from mods/<mod> ignored: ...`). Raw overrides are whole files held in memory, read
 * with any offset and size, and looked at again at most once per second (mtime and size): a change
 * of size reaches the game through the wrapped getSectorCount, as for the text layer.
 *
 * The disc directory (volume descriptor, path table, every directory) is walked ONCE, by pfsDiscOpen,
 * and shared: the text layer (file sizes), the image layer (which art files exist, the stamp) and the
 * number -> path map all read it from here (feature port-filesystem T5).
 *
 * Movies and XA audio do not go through cdRead and are out of reach of this layer.
 * Host tests: pc/fstest.c.
 */

#include <stddef.h>
#include <stdint.h>

#include "text_data.h" /* TdDisc */

typedef enum PfsSource { PFS_SRC_DISC = 0, PFS_SRC_TEXT, PFS_SRC_RAW, PFS_SRC_GEN } PfsSource;

/* The open-format layers, asked in order. text: the tdOverride contract (bytes of the replacement or
   NULL; refresh asks for a look at the sources). image: nonzero when the layer substitutes images
   inside that file. Either may be NULL. */
typedef const uint8_t *(*PfsTextFn)(int file, size_t *len, int refresh);
typedef int (*PfsImageFn)(int file);
void pfsSetLayers(PfsTextFn text, PfsImageFn image);

/* ---- the disc directory, walked once ---- */

typedef struct PfsFile {
    char path[104]; /* as on the disc, "AAA/DAT/COUNTRY/ENGLISH/ES.BIN" (no ";1" version) */
    long lba;       /* first sector */
    long size;      /* exact size in bytes */
} PfsFile;

/* Walk the volume descriptor, the path table and every directory of `disc` (kept by value). Call it once,
   before tdStart and imgStart. 0 on success; -1 when it cannot be read (nothing is open then). Replaces a
   previous walk; pointers from pfsFindPath / pfsList stay valid until the next open, close or shutdown. */
int pfsDiscOpen(const TdDisc *disc);
void pfsDiscClose(void);
int pfsDiscIsOpen(void);

/* The identity of the disc: FNV hash (tdHashUpdate) of the volume descriptor, then, with withPathTable, of
   the path table too. A caller continues it with tdHashUpdate to stamp more. TD_HASH_INIT when not open. */
uint64_t pfsDiscHash(int withPathTable);

/* The file with that full path ("AAA/DAT/FIELD/FIELDCOM.BIN"), or NULL */
const PfsFile *pfsFindPath(const char *path);
/* The files whose path starts with `prefix` ("AAA/DAT/SCREEN/"; NULL or "" for all), in directory order:
   writes up to `max` pointers into out (NULL: just count) and returns how many match. */
int pfsList(const char *prefix, const PfsFile **out, int max);
/* Read `len` bytes from sector `lba` of the opened disc (malloc'ed, whole sectors read plus one spare
   byte), or NULL. */
uint8_t *pfsDiscRead(long lba, size_t len);

/* Walk the disc's directories and map file numbers 0..fileCount-1 to their paths. disc->locate gives
   a number's first sector. 0 on success; -1 when the directory cannot be read (the layer then only
   dispatches the text layer). Uses the walk of pfsDiscOpen (opening it when not yet open). Safe to call again (replaces the map). */
int pfsInit(const TdDisc *disc, int fileCount);
void pfsShutdown(void);

/* Disc path of a file number without the "AAA/" folder ("DAT/FIELD/STAGE/S330PACK.BIN"), or NULL */
const char *pfsName(int file);
int pfsFileCount(void); /* numbers that have a path */
/* File number of a disc path (with or without "AAA/"), or -1 */
int pfsNumberOf(const char *path);

/* The one dispatch point. Returns the bytes that replace the file (text layer or raw override) and
   their length, or NULL for the disc. *src says which (src may be NULL). refresh: look at the
   sources again if they were last seen over a second ago; cdRead passes 0 so a read uses the same
   size its sector count announced. */
const uint8_t *pfsResolve(int file, size_t *len, int refresh, PfsSource *src);

/* Serve `sectors` sectors of `file` starting at sector `offset` from memory into dst when pfsResolve has a
   replacement: the bytes, then zeros up to whole sectors (the disc's last sector is padded too). Returns 1 when
   served (dst filled), 0 when the caller must read the disc. *src as in pfsResolve. */
int pfsReadSectors(int file, int offset, int sectors, uint8_t *dst, PfsSource *src);

/* Serves `bytes` (copied; whole-file, any length) for file number `file` in place of the disc file, from the generated layer
   (see the order at the top). The length reaches the game through the wrapped getSectorCount like any replacement, so the
   file cache allocates its buffer to fit. bytes NULL removes it. */
void pfsSetGenerated(int file, const uint8_t *bytes, size_t len);

/* ---- virtual files (model-import I3, ADR-011) ----
   Files that are not on the disc, with numbers from the end of the game's file table up (`fileCount` of pfsInit; 2382 for
   the EU disc) to 65535 (a reference is (file << 16) | entry, so the number is a u16). They are served by the generated layer
   from a copy of the bytes and reach the game by number like any file: exists, sector count, the file cache, a FighterInfo's
   (file << 16) | entry. host_cd.c wraps the table's functions for them. Nothing here names a file: the numbers are an
   implementation detail, a caller keeps its own name -> number map (the content registry does).
   Numbering is deterministic: the k-th file added since the last pfsClearVirtualFiles is `base + k`, so the same load order
   gives the same numbers. Add them in a fixed order (mods in their load order, then each mod's files by key) and clear
   them before a reload (pfsClearVirtualFiles); pfsShutdown and a new pfsInit with another base release them too. */
int pfsVirtualBase(void);                              /* the first virtual number (0 until pfsInit has a file count) */
int pfsAddVirtualFile(const void *bytes, size_t len);  /* a copy of the bytes served under a new number; -1: empty, no base or the table is full */
int pfsIsVirtual(int file);                            /* 1 when `file` is a number added and not cleared */
int pfsVirtualCount(void);
void pfsClearVirtualFiles(void);                       /* release them all; the next one gets the base again */

/* The post-read hook (ADR-007 section 6, the battle table): called by the reader after the bytes of a
   read are in dst, whatever their source (the disc, the text layer or a raw override), with the file,
   the first sector, the sector count and the source. It may patch dst in place (never grow it). Either
   the hook or the reader may be absent; hook NULL removes it. Precedence follows: the source's bytes
   first (a raw override replaces the disc file), the hook's patch on top. */
typedef void (*PfsPostReadFn)(int file, int offset, int sectors, uint8_t *dst, PfsSource src);
void pfsSetPostRead(PfsPostReadFn fn);
void pfsPostRead(int file, int offset, int sectors, uint8_t *dst, PfsSource src);

/* Log level trace on the files channel (or DW3_FS_TRACE=1): one line per read
   (`[files] read <n> <path> off=<sectors> sectors=<n> src=<...>`, port_log.h) */
int pfsTraceEnabled(void);
void pfsTrace(int file, int offset, int sectors, PfsSource src);

#endif
