#ifndef PC_VIDEO_EXPORT_H
#define PC_VIDEO_EXPORT_H

/*
 * The game's movies as open files (content-export E2a, ADR-005 / ADR-010): each of the 14 FMVs (AAA/STR/<NAME>.STR) is read from
 * the raw disc image and written as
 *
 *   <data>/video/<NAME>.avi    MJPEG video (the frame size of the stream, 320x416, 15 frames per second) and 16-bit PCM audio
 *                              (the XA stream, 37800 Hz, mono or stereo as its sectors say), see avi_write.h / jpeg_write.h
 *
 * so a modder can open it in any editor or player and, later (E2b), play an edited one in place of the original.
 *
 * It reuses the port's own decoders (host_mdec.c for the picture, host_xa.c for the sound) but none of the playback state: the
 * frames are assembled here from the sectors, and the disc is read through the reader the caller hands in (boot.c gives it its
 * own file handle), so a movie that is playing is never disturbed and the job needs nothing of the game's St or XA layers.
 *
 * Sync: the first audio sample is at time zero and video frame k at k / 15 s, the usual AVI reading. (The game's own player
 * holds frame N until N * 2520 samples have played, one frame period later than that; the files do not copy that delay.) The
 * audio is cut into the 2520 sample frames of one video frame (37800 / 15) and each frame's audio goes in just before its picture.
 *
 * On demand only (the 14 files are a few hundred MB): the System tab's "Export videos" row and the [data] export_videos key of
 * settings.ini ("all" or movie names separated by commas). The menu's job runs on a worker thread (videoExportStart); the
 * settings.ini key at start runs synchronously. Every file follows the rule of the other data/ families: a manifest of what the
 * port wrote, an edited file stays and the new one goes beside it as <file>.new. The manifest also holds, per movie, the exporter
 * version that wrote it: a request skips a movie of the current version whose file exists (an edited file counts); the menu row
 * forces a rebuild of all. Bump VIDEO_EXPORT_VERSION when the output bytes change. Compile with -ffp-contract=off.
 */

#include <stddef.h>
#include <stdint.h>

#define VIDEO_EXPORT_VERSION 2 /* 2: the AVIs declare their 20:13 display aspect (vprp) */
#define VIDEO_EXPORT_ASPECT_W 20
#define VIDEO_EXPORT_ASPECT_H 13
#define VIDEO_EXPORT_QUALITY 85
#define VIDEO_EXPORT_FPS 15

/* Where the movies are and how to read the disc. read is only called by the thread that runs the job. */
typedef struct VideoExportIo {
    /* the first sector and the sector count of the movie called `name` ("MOVIED01"); 1 when it is on the disc */
    int (*movie)(void *ud, const char *name, long *lba, long *sectors);
    /* one raw 2352-byte frame of the disc image; 1 when read */
    int (*read)(void *ud, long lba, unsigned char *frame);
    void *ud;
} VideoExportIo;

typedef struct VideoExportStats {
    int width, height;
    int frames;       /* video frames written */
    int badFrames;    /* frames that did not decode (the previous picture was repeated) */
    int channels;     /* 0: the movie has no audio */
    long audioFrames; /* sample frames written */
    long bytes;       /* size of the AVI */
} VideoExportStats;

/* The 14 movie names, in disc order, and how many */
int videoExportCount(void);
const char *videoExportName(int i);

/* One movie to a file, now, no manifest. 0, or -1 with the reason in err. progress (may be NULL) is called with the percentage 0..100. */
int videoExportBuild(const VideoExportIo *io, const char *name, const char *outPath, VideoExportStats *st, char *err, size_t errCap,
                     void (*progress)(void *ud, int percent), void *progressUd);

/* ---- the job: where the files go and which movies ---- */

/* The data folder (<data>/video/...) and the disc reader. Call again to change them; an unfinished job is dropped */
void videoExportSetup(const char *dataDir, const VideoExportIo *io);
/* Queues "all" or movie names separated by commas/spaces (any case, ".STR"/".avi" optional). The queued count, or -1 when there is
   nothing to do (no data folder, no reader, nothing valid, a job is running). */
int videoExportRequest(const char *spec);
/* The same; force != 0 rebuilds every queued movie even when it is up to date */
int videoExportRequestEx(const char *spec, int force);
/* How many movies of the last job were really built (the rest were skipped as up to date) */
int videoExportBuilt(void);
/* The settings.ini key: videoExportRequest, then builds the queue now, on this thread. The queued count, or -1. */
int videoExportRun(const char *spec);
/* The menu's job: videoExportRequestEx, then a worker thread builds and writes the queue. Then call videoExportPump every frame */
int videoExportStart(const char *spec, int force);
/* Stops a running worker between two movies (a movie in progress is dropped, its .part file removed) and joins it; atexit-safe */
void videoExportShutdown(void);
/* Exports queued movies for about budgetMs milliseconds (at least one movie) when there is no worker; with one it only joins it at
   the end. 1 while work remains, 0 when the job is over. */
int videoExportPump(unsigned budgetMs);
int videoExportActive(void);
/* done and failed so far, how many were queued */
void videoExportCounts(int *done, int *failed, int *total);
/* The row's value text: "" before any job, "Exporting 3/14", "Done (14)", "Done, 2 failed" */
const char *videoExportStatus(void);

#endif
