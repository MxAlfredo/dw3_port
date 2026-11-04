/*
 * Host replacement for libcd's disc reader: the game's own CdReader table
 * (CD_READER, defined by src/engine/file/cd_reader.c on the PlayStation with
 * CdlReadN and callbacks) backed by a raw .bin disc image.
 *
 * This file is ours, not the game's: the PlayStation reader drives the CD
 * drive through CdControlF/CdReadyCallback and copies each sector as an
 * interrupt delivers it, none of which exists on the host. The game's file
 * cache only ever calls CD_READER.isBusy and CD_READER.read, so the host
 * reads the sectors straight from the image and completes before returning:
 * isBusy is always 0 and read leaves `done` set.
 *
 * A raw image is 2352-byte frames: 12 bytes of sync, a 4-byte header, an
 * 8-byte XA subheader, the 2048 bytes of data (what the game reads, Mode 2
 * Form 1) and the EDC/ECC. Same framing as tools/patch_disc.py and
 * tools/extract_disc.py read.
 *
 * T36 adds the other half of that framing: the movie streams' XA audio lives
 * in Mode 2 Form 2 sectors, whose audio payload (18 sound groups of 128 bytes
 * after the subheader) the file reader above never touches -- and which the
 * extracted STR copies under disks/eu/AAA do not even carry. The CD control
 * commands the movie player already sends (CdlSetloc / CdRead2 with
 * CdlModeRT / CdlPause) now drive shim/host_xa.c, whose driver reads whole raw
 * frames through pcXaReadFrame below and streams them to OpenAL.
 *
 * T38 adds the video half on the same commands: CdlSetloc also arms the St
 * stream (shim/host_st.c) at that LBA, CdRead2 with CdlModeStream starts it
 * and CdlPause stops it. The St pump reads through pcXaReadFrame too, but from
 * its own handle (cdStImage) and with its own LBA cursor, so the XA driver on
 * PsyX's vsync thread and the game thread's StGetNext never share a FILE* and
 * neither can starve the other. Each raw frame's 2048 user bytes go to the St
 * layer, which recognises video sectors by their StHEADER and skips the XA
 * ones.
 */
#include "engine/file.h"
#include "host.h"

#include "host_st.h"
#include "host_xa.h"
#include "stage_timing.h"
#include "image_data.h"
#include "port_fs.h"
#include "text_data.h"
#include "video_mod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disc_fd.h"

#define CD_FRAME_SIZE 2352
#define CD_DATA_OFFSET 24 /* sync (12) + header (4) + subheader (8) */

static FILE *cdImage;
static const char *cdImagePath;

extern s32 FILE_COUNT; /* file_table.c: how many file numbers the table has */
extern void pcFileCacheInstall(void); /* file_cache_pc.c */

/* DW3_STAGE_TIMING: the game frame the per-frame poll last saw (stage_timing.h) */
int stageTimingFrameNo;
double stageTimingT0 = -1.0;
/* DW3_STAGE_TIMING: the clock when the process started running (before main), -1 with the trace off */
double stageTimingBootMs = -1.0;

static void __attribute__((constructor)) stageTimingBoot(void) {
    if (stageTimingOn()) {
        stageTimingBootMs = stageTimingNowMs();
    }
}

/* T36: the movie streams' XA audio is read raw (whole 2352-byte frames, the
   subheader included), from a second handle of the same image so the XA
   reader on PsyX's vsync thread never races the game's own FILE* (host_cd.c's
   cdRead, which runs on the game thread). */
static FILE *cdXaImage;

/* T38: the St stream's own handle on the same image. Same reason as above: the
   video pump runs on the game thread (StGetNext) while the XA driver runs on
   the vsync thread, so they must not share a FILE*. The two readers walk the
   same LBAs independently; neither consumes from the other. */
static FILE *cdStImage;

/* A byte of a number in BCD, as a CdlLOC holds the position */
static u_char toBcd(int n) {
    return (u_char)(((n / 10) << 4) | (n % 10));
}

/*
 * LBA -> CdlLOC (libcd's CdIntToPos): the disc's 2-second, 150-sector
 * lead-in, then minute/second/sector in BCD. Still needed on the host
 * because the game's own getFilePos (file_table.c) calls it.
 */
CdlLOC *CdIntToPos(int i, CdlLOC *p) {
    i += 150;
    p->minute = toBcd(i / (60 * 75));
    p->second = toBcd(i / 75 % 60);
    p->sector = toBcd(i % 75);
    p->track = 0;
    return p;
}

/* The host reader completes every read before read returns: never busy */
static s32 cdIsBusy(void) {
    return 0;
}

/*
 * Reads a file's sectors from the image into buffer. size 0 means all of the
 * file, as the game's readFile reads it; offset is in sectors. The bytes go
 * where the game expects: the data of each frame, 2048 at a time.
 */
static void cdRead(s32 file, s32 offset, s32 size, void *buffer, s32 *done) {
    s32 sectors = (size != 0) ? size : FILE_TABLE.getSectorCount(file);
    s32 first = FILE_TABLE.getSector(file) + offset;
    u8 *dst = buffer;
    s32 i;
    int served;
    PfsSource src = PFS_SRC_DISC;
    double t0;

    if (cdImage == NULL) {
        fprintf(stderr, "host cd: no image open\n");
        exit(1);
    }
    /* One dispatch point (port_fs.h): a file with a replacement (text layer: mods, an edited data
       file, es-419; or a raw mods/<mod>/files/ file) comes from memory, zero-padded to whole sectors
       like the disc's last sector. No refresh here: the sector count the cache allocated
       (getSectorCount, below) must stay the one this read uses. */
    t0 = stageTimingOn() ? stageTimingNowMs() : 0.0;
    served = pfsReadSectors(file, offset, sectors, dst, &src);
    if (pfsTraceEnabled()) {
        pfsTrace(file, offset, sectors, src);
    }
    if (served) {
        pfsPostRead(file, offset, sectors, dst, src);
        if (src == PFS_SRC_TEXT && tdTraceEnabled()) {
            size_t overLen = 0;
            const u8 *over = pfsResolve(file, &overLen, 0, NULL);

            if (over != NULL) {
                tdLogServed(file, "override", over, overLen);
            }
        }
        if (stageTimingOn()) {
            stageTimingLog("read file=%d off=%d sectors=%d bytes=%d src=override ms=%.3f", (int)file,
                           (int)offset, (int)sectors, (int)(sectors * CD_SECTOR_SIZE),
                           stageTimingNowMs() - t0);
        }
        if (done != NULL) {
            *done = 1;
        }
        return;
    }
    for (i = 0; i < sectors; i++) {
        long at = (long)(first + i) * CD_FRAME_SIZE + CD_DATA_OFFSET;
        if (fseek(cdImage, at, SEEK_SET) != 0
            || fread(dst + i * CD_SECTOR_SIZE, 1, CD_SECTOR_SIZE, cdImage) != CD_SECTOR_SIZE) {
            fprintf(stderr, "host cd: sector %d not readable from %s\n", first + i, cdImagePath);
            exit(1);
        }
    }
    pfsPostRead(file, offset, sectors, dst, PFS_SRC_DISC);
    if (stageTimingOn()) {
        stageTimingLog("read file=%d off=%d sectors=%d bytes=%d src=disc ms=%.3f", (int)file, (int)offset,
                       (int)sectors, (int)(sectors * CD_SECTOR_SIZE), stageTimingNowMs() - t0);
    }
    if (tdTraceEnabled()) {
        int lang, k;

        if (tdFromIndex(file, &lang, &k)) {
            tdLogServed(file, "disc", buffer, (size_t)sectors * CD_SECTOR_SIZE);
        }
    }
    if (done != NULL) {
        *done = 1;
    }
}

/*
 * L2b: the text override layer. FILE_TABLE's leaf functions are saved and getSectorCount is
 * wrapped, so a file served from memory (mods, an edited data file, es-419) reports its own
 * sector count to the file cache; every other file answers with the original. The variant
 * (Spanish or es-419) is set by tdSetVariant; DW3_TEXT_VARIANT=es419 selects it at boot.
 */
static s32 (*origExists)(s32 file);
static s32 (*origGetSector)(s32 file);
static s32 (*origGetSectorCount)(s32 file);
static void (*origGetPos)(s32 file, s32 offset, void *pos);

/* Virtual files (port_fs.h, model-import I3): the numbers from FILE_COUNT up are not in the game's table (FILE_SECTORS has
   FILE_COUNT entries), so every leaf of FILE_TABLE answers for them here and never reaches the table. Their bytes come from
   the generated layer; there is no disc sector, so the first sector is 0 and the position is the start of the disc. */
static int cdIsVirtualNumber(s32 file) {
    return file >= FILE_COUNT;
}

static s32 cdTextSectorCount(s32 file) {
    size_t len = 0;

    if (pfsResolve(file, &len, 1, NULL) != NULL) {
        return (s32)((len + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE);
    }
    return cdIsVirtualNumber(file) ? 0 : origGetSectorCount(file);
}

static s32 cdExists(s32 file) {
    return cdIsVirtualNumber(file) ? pfsIsVirtual(file) : origExists(file);
}

static s32 cdGetSector(s32 file) {
    return cdIsVirtualNumber(file) ? 0 : origGetSector(file);
}

static void cdGetPos(s32 file, s32 offset, void *pos) {
    if (cdIsVirtualNumber(file)) {
        CdIntToPos(offset, (CdlLOC *)pos);
        return;
    }
    origGetPos(file, offset, pos);
}

/* The image layer's claim on a file: it substitutes images inside it (port_fs.h precedence) */
static int cdImageClaims(int file) {
    if (cdIsVirtualNumber(file) || !FILE_TABLE.exists(file)) {
        return 0;
    }
    return imgFileOverridden(origGetSector(file));
}

static int cdTextSector(void *ctx, long lba, uint8_t *out) {
    (void)ctx;
    if (cdImage == NULL || fseek(cdImage, lba * CD_FRAME_SIZE + CD_DATA_OFFSET, SEEK_SET) != 0) {
        return 0;
    }
    return fread(out, 1, CD_SECTOR_SIZE, cdImage) == CD_SECTOR_SIZE;
}

static int cdTextLocate(void *ctx, int index, long *lba, int *sectors) {
    (void)ctx;
    if (cdIsVirtualNumber(index) || !FILE_TABLE.exists(index)) {
        return 0;
    }
    *lba = origGetSector(index);
    *sectors = origGetSectorCount(index);
    return 1;
}

/* The disc's own bytes of a file number, whatever the layers above would serve (malloc'ed, *len bytes:
   the whole sectors), or NULL. For the reference exports of the data tables (table_mods.h). */
uint8_t *pcCdReadDiscFile(int file, size_t *len) {
    s32 sectors, first, i;
    uint8_t *b;

    if (cdImage == NULL || origGetSector == NULL || origGetSectorCount == NULL || file < 0 || cdIsVirtualNumber(file) ||
        !FILE_TABLE.exists(file)) {
        return NULL;
    }
    sectors = origGetSectorCount(file);
    first = origGetSector(file);
    if (sectors <= 0 || first <= 0) {
        return NULL;
    }
    b = (uint8_t *)malloc((size_t)sectors * CD_SECTOR_SIZE);
    if (b == NULL) {
        return NULL;
    }
    for (i = 0; i < sectors; i++) {
        long at = (long)(first + i) * CD_FRAME_SIZE + CD_DATA_OFFSET;
        if (fseek(cdImage, at, SEEK_SET) != 0 || fread(b + (size_t)i * CD_SECTOR_SIZE, 1, CD_SECTOR_SIZE, cdImage) != CD_SECTOR_SIZE) {
            free(b);
            return NULL;
        }
    }
    if (len != NULL) {
        *len = (size_t)sectors * CD_SECTOR_SIZE;
    }
    return b;
}

/* Called by boot once the disc is open. dataDir/l10nDir may be NULL. */
void pcCdTextInit(const char *dataDir, const char *l10nDir) {
    TdDisc disc;
    const char *variant = getenv("DW3_TEXT_VARIANT");

    if (cdImage == NULL || origGetSectorCount != NULL) {
        return;
    }
    origExists = FILE_TABLE.exists;
    origGetSector = FILE_TABLE.getSector;
    origGetSectorCount = FILE_TABLE.getSectorCount;
    origGetPos = FILE_TABLE.getPos;
    FILE_TABLE.exists = cdExists;
    FILE_TABLE.getSector = cdGetSector;
    FILE_TABLE.getSectorCount = cdTextSectorCount;
    FILE_TABLE.getPos = cdGetPos;
    if (variant != NULL && variant[0] != '\0') {
        if (strcmp(variant, "es419") == 0) {
            tdSetVariant(TD_VARIANT_ES419);
        } else if (strcmp(variant, "spa") != 0) {
            fprintf(stderr, "text: DW3_TEXT_VARIANT=%s not understood (spa or es419), using spa\n", variant);
        }
    }
    disc.ctx = NULL;
    disc.readSector = cdTextSector;
    disc.locate = cdTextLocate;
    pfsDiscOpen(&disc); /* the disc directory, walked once for the text, image and file layers; fails soft */
    tdStart(&disc, dataDir, l10nDir);
    if (dataDir != NULL) {
        imgStart(&disc, dataDir); /* menu art as PNG (menu-art-png T2); fail soft */
    }
    pfsSetLayers(tdOverride, cdImageClaims);
    pfsInit(&disc, (int)FILE_COUNT); /* the number -> disc path map for the raw overrides, from that walk; fails soft */
    fprintf(stderr, "text: variant %s\n", tdGetVariant() == TD_VARIANT_ES419 ? "es419" : "spa");
}

/* The game's CD reader table, with the host's two methods */
CdReader CD_READER = {
    CD_READ_IDLE, 0, 0, 0, NULL, NULL, {0}, 0, 0, NULL, 0,
    cdIsBusy,
    cdRead,
};

/*
 * T36: one whole raw frame for the XA stream driver. Same framing the file
 * reader skips past; here the sync/header/subheader are exactly what the
 * decoder needs. 0 on any short read (the stream's end as far as the driver
 * is concerned).
 */
static int pcXaReadFrame(void *ctx, long lba, unsigned char *frame) {
    long at = lba * CD_FRAME_SIZE;

    if (ctx == NULL) {
        return 0;
    }
    if (fseek((FILE *)ctx, at, SEEK_SET) != 0) {
        return 0;
    }
    return fread(frame, 1, CD_FRAME_SIZE, (FILE *)ctx) == CD_FRAME_SIZE;
}

/*
 * T38: one 2048-byte sector for the St layer (shim/host_st.c), read through the
 * same raw-frame reader the XA side uses but from cdStImage. The 2048 bytes a
 * Mode 2 frame carries are at CD_DATA_OFFSET; for a Form 2 (XA) sector those
 * bytes are audio payload and do not look like a StHEADER, so the St layer
 * skips them the same way the game's stream does.
 */
static int cdReadStSector(void *ctx, long lba, unsigned char *sector2048) {
    unsigned char frame[CD_FRAME_SIZE];
    int i;

    if (!pcXaReadFrame(ctx, lba, frame)) {
        return 0;
    }
    for (i = 0; i < CD_SECTOR_SIZE; i++) {
        sector2048[i] = frame[CD_DATA_OFFSET + i];
    }
    return 1;
}

/* 0 on success; on failure it reports the file and returns -1 (W2: boot.c decides what to do) */
int pcCdReaderOpen(const char *image) {
    cdImagePath = image;
    cdImage = dfdOpen(image);
    if (cdImage == NULL) {
        perror(image);
        return -1;
    }
    cdXaImage = dfdOpen(image);
    if (cdXaImage == NULL) {
        perror(image);
        return -1;
    }
    sndXaSetSource(pcXaReadFrame, cdXaImage);
    cdStImage = dfdOpen(image);
    if (cdStImage == NULL) {
        perror(image);
        return -1;
    }
    hostStSetSource(cdReadStSector, cdStImage);
    pcFileCacheInstall(); /* the cache's per-frame update, port-side (file_cache_pc.c) */
    return 0;
}

/* The path of the disc image that was opened (NULL before pcCdReaderOpen): the movie export reads the raw sectors through a handle
   of its own (boot.c), so a movie that is playing is never disturbed */
const char *pcCdDiscPath(void) {
    return cdImagePath;
}

/* The command line's behavior, unchanged: a disc that does not open ends the run */
void pcCdReaderInit(const char *image) {
    if (pcCdReaderOpen(image) != 0) {
        exit(1);
    }
}

/*
 * The drive's control commands (libcd), which the game's main runs at boot:
 * CdInit, CdSetDebug, then a loop that waits for CdlSetmode and a CdlPause.
 * On the host there is no drive to command, so these answer "done" at once:
 * a real CdControl would send the command to the drive and return 0 until it
 * completed, and main's `while (CdControl(...) == 0)` would spin forever.
 *
 * Nothing else of libcd is needed: the reads go through CD_READER above, and
 * the game's own cd_reader.c (which drives the drive with CdControlF and
 * callbacks) is the PS1 half of that table.
 */
int CdInit(void) {
    return 1;
}

int CdSetDebug(int level) {
    (void)level;
    return 0;
}

/* The game's movie player names a stream by MSF (movie.c's STDWTITL_readStream
   passes the file table's CdlLOC to CdlSetloc): libcd's CdPosToInt, the
   disc's 150-sector lead-in already included. */
static long cdPosToLba(const u_char *p) {
    long minute = btoi(p[0]);
    long second = btoi(p[1]);
    long sector = btoi(p[2]);

    return (minute * 60 + second) * 75 + sector - 150;
}

/*
 * E2b: a mod's AVI in place of a movie (shim/video_mod.h). CdlSetloc names the movie by its first sector; when the replacement
 * recognises it, the St layer's sector source and the XA driver's PCM source point at the replacement, otherwise at the disc.
 * Installed by boot (pcCdSetVideoMod); without it nothing differs from the disc-only path.
 */
static const VideoModHooks *cdMovieHooks;
static int cdMovieReplaced; /* the sources point at the replacement now */

void pcCdSetVideoMod(const VideoModHooks *hooks) {
    cdMovieHooks = hooks;
}

static void cdSelectMovieSource(long lba) {
    if (cdMovieHooks != NULL && cdMovieHooks->arm(lba)) {
        hostStSetSource(cdMovieHooks->readSector, NULL);
        sndXaSetPcm(2, cdMovieHooks->readAudio, NULL);
        cdMovieReplaced = 1;
    } else if (cdMovieReplaced) {
        hostStSetSource(cdReadStSector, cdStImage);
        sndXaSetPcm(0, NULL, NULL);
        cdMovieReplaced = 0;
    }
}

/*
 * T36/T38: the movie player's CD commands carry both streams. CdlSetloc names
 * the stream's first sector and arms both LBA cursors (XA audio and the St
 * video pump), CdlPause stops both; CdlSetmode is recorded by CdRead2's own
 * mode word instead. Everything answers 1 at once (the host has no drive and
 * main's `while (CdControl(...) == 0)` spins otherwise).
 */
int CdControl(u_char com, u_char *param, u_char *result) {
    (void)result;
    if (com == CdlSetloc && param != NULL) {
        long lba = cdPosToLba(param);

        sndXaArm(lba);
        hostStArm(lba);
        cdSelectMovieSource(lba);
    } else if (com == CdlPause) {
        sndXaStop();
        hostStStop();
        if (cdMovieHooks != NULL) {
            cdMovieHooks->stop();
        }
    }
    return 1;
}

int CdControlB(u_char com, u_char *param, u_char *result) {
    return CdControl(com, param, result);
}

/*
 * CdRead2 (T18/T36/T38): the streaming read the title's movie player starts
 * (movie.c's STDWTITL_readStream). On the PlayStation it arms the drive for a
 * streamed sector read; the host's reader is the synchronous CD_READER above
 * and never streams. The mode word selects the halves: CdlModeRT (ADPCM on)
 * starts the XA stream driver, CdlModeStream starts the St video pump, both at
 * the LBA CdlSetloc last recorded. They read the same sectors from their own
 * handles, so audio and video advance independently.
 */
int CdRead2(long mode) {
    if ((mode & CdlModeRT) != 0) {
        sndXaStart();
    }
    if ((mode & CdlModeStream) != 0) {
        hostStStart();
    }
    return 1;
}
