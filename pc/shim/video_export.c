/* The movies as MJPEG AVI files (content-export E2a); see video_export.h */
#include "video_export.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "avi_write.h"
#include "host_mdec.h"
#include "host_xa.h"
#include "jpeg_write.h"
#include "port_log.h"
#include "text_data.h"

static const char *const kNames[] = {"MOVIED01", "MOVIED02", "MOVIED03", "MOVIED04", "MOVIED05", "MOVIED06", "MOVIED07",
                                     "MOVIED08", "MOVIEE01", "MOVIEE02", "MOVIEE03", "MOVIEE04", "MOVIEOP2", "MOVIEOPN"};
#define N_MOVIES ((int)(sizeof kNames / sizeof kNames[0]))

int videoExportCount(void) { return N_MOVIES; }

const char *videoExportName(int i) { return i >= 0 && i < N_MOVIES ? kNames[i] : NULL; }

/* ---------------------------------------------------------------- one movie */

#define RAW_SECTOR 2352
#define USER_AT 24 /* the 2048 user bytes of a Mode 2 Form 1 sector */
#define ST_USER 2048
#define ST_HEADER 32
#define ST_PAYLOAD 2016
#define ST_MAX_SECTORS 32 /* the most sectors one frame may span (host_st.c's HST_MAX_SECTORS) */
#define MAX_W 640
#define MAX_H 480
#define SPF (37800 / VIDEO_EXPORT_FPS) /* sample frames of audio per video frame: 2520 */
#define PRESCAN_SECTORS 600           /* how far into a movie the first audio sector is looked for */
#define RUN_BYTES 0x40100             /* a frame's run-level stream: command word + 0xFFFF words */

typedef struct Builder {
    const VideoExportIo *io;
    volatile int *stop;
    VideoExportStats *st;
    char *err;
    size_t errCap;
    const char *outPath;
    AviWriter *avi;
    HostMdec *mdec;
    uint8_t *rgb, *jpeg, *run, *payload, header[ST_HEADER];
    size_t jpegCap;
    int w, h;
    int havePicture; /* rgb holds a decoded picture (repeated when a frame does not decode) */
    /* the frame being assembled */
    int asmActive, asmSeen, asmSectors;
    unsigned long asmCount;
    /* the audio */
    int channels, audioFile, audioChannel;
    SndXaState xa;
    int16_t *fifo;
    size_t fifoStart, fifoFrames, fifoCap; /* the queued sample frames are fifo[fifoStart * channels ...] */
    long audioWritten;                     /* sample frames in the AVI so far */
    int16_t chunk[SPF * 2];
    int16_t pcm[SND_XA_SECTOR_SAMPLES];
    int failed;
} Builder;

static void fail(Builder *b, const char *fmt, const char *what) {
    if (!b->failed && b->err != NULL && b->errCap > 0) snprintf(b->err, b->errCap, fmt, what);
    b->failed = 1;
}

static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned long rd32(const uint8_t *p) { return (unsigned long)rd16(p) | ((unsigned long)rd16(p + 2) << 16); }

/* One chunk of sample frames from the queue (`frames` of them) into the AVI */
static void writeAudio(Builder *b, size_t frames) {
    if (b->failed || b->avi == NULL || frames == 0) return;
    memcpy(b->chunk, b->fifo + b->fifoStart * (size_t)b->channels, frames * (size_t)b->channels * sizeof(int16_t));
    if (aviAddAudio(b->avi, b->chunk, frames) != 0) {
        fail(b, "%s", "the AVI could not be written");
        return;
    }
    b->fifoStart += frames;
    b->fifoFrames -= frames;
    b->audioWritten += (long)frames;
    if (b->fifoFrames == 0) b->fifoStart = 0;
}

static void queueAudio(Builder *b, const int16_t *pcm, size_t frames) {
    size_t ch = (size_t)b->channels;
    if (b->fifoStart > 0 && b->fifoStart * 2 > b->fifoCap) { /* slide the queue back to the start of its buffer */
        memmove(b->fifo, b->fifo + b->fifoStart * ch, b->fifoFrames * ch * sizeof(int16_t));
        b->fifoStart = 0;
    }
    if (b->fifoStart + b->fifoFrames + frames > b->fifoCap) {
        size_t cap = b->fifoCap ? b->fifoCap * 2 : 16384;
        int16_t *p;
        while (cap < b->fifoStart + b->fifoFrames + frames) cap *= 2;
        p = (int16_t *)realloc(b->fifo, cap * ch * sizeof(int16_t));
        if (p == NULL) {
            fail(b, "%s", "out of memory");
            return;
        }
        b->fifo = p;
        b->fifoCap = cap;
    }
    memcpy(b->fifo + (b->fifoStart + b->fifoFrames) * ch, pcm, frames * ch * sizeof(int16_t));
    b->fifoFrames += frames;
}

/* A whole frame's payload is in b->payload: decode, encode and write it (audio first, so each picture follows its sound) */
static void frameDone(Builder *b) {
    int w = (int)rd16(b->header + 16), h = (int)rd16(b->header + 18);
    size_t len = (size_t)b->asmSectors * ST_PAYLOAD, n;
    HostMdecStats ms;
    int bad = 0;

    if (w <= 0 || h <= 0 || (w & 15) || (h & 15) || w > MAX_W || h > MAX_H) {
        bad = 1;
    } else if (b->avi == NULL) {
        AviParams p;
        memset(&p, 0, sizeof p);
        p.width = w;
        p.height = h;
        p.fps = VIDEO_EXPORT_FPS;
        p.channels = b->channels;
        p.sampleRate = 37800;
        p.aspectW = VIDEO_EXPORT_ASPECT_W; /* the stored 320x416 is shown 640x416: the PS1's pixels are twice as wide as tall */
        p.aspectH = VIDEO_EXPORT_ASPECT_H;
        b->w = w;
        b->h = h;
        b->jpegCap = jpegMaxSize(w, h);
        b->jpeg = (uint8_t *)malloc(b->jpegCap);
        if (b->jpeg == NULL) {
            fail(b, "%s", "out of memory");
            return;
        }
        b->avi = aviOpen(b->outPath, &p);
        if (b->avi == NULL) {
            fail(b, "cannot create %s", b->outPath);
            return;
        }
    } else if (w != b->w || h != b->h) {
        bad = 1; /* a frame of another size cannot go in this file */
    }
    if (!bad) {
        memset(b->payload + len, 0, 64); /* the bit reader looks a little past the data */
        if (hostMdecVlc2(b->payload, b->run, &b->mdec->vlc) != 0) b->st->badFrames++; /* the game decodes it all the same */
        if (hostMdecDecodeFrame(b->mdec, b->run, w, h, HMDEC_DEPTH_24BPP, b->rgb, &ms) != 0) {
            bad = 1;
        } else {
            b->havePicture = 1;
        }
    }
    if (b->avi == NULL) { /* the first frame is unusable: nothing to repeat yet, nothing to open the file with */
        fail(b, "%s", "the first frame of the movie cannot be decoded");
        return;
    }
    if (bad) {
        b->st->badFrames++;
        if (!b->havePicture) memset(b->rgb, 0, (size_t)b->w * (size_t)b->h * 3);
    }
    n = jpegEncodeRgb24(b->rgb, b->w, b->h, VIDEO_EXPORT_QUALITY, b->jpeg, b->jpegCap);
    if (n == 0) {
        fail(b, "%s", "a frame could not be encoded");
        return;
    }
    if (b->channels > 0) {
        long due = ((long)b->st->frames + 1) * SPF;
        while (b->fifoFrames >= SPF && b->audioWritten < due && !b->failed) writeAudio(b, SPF);
    }
    if (aviAddVideo(b->avi, b->jpeg, n) != 0) {
        fail(b, "%s", "the AVI could not be written");
        return;
    }
    b->st->frames++;
}

/* One 2048-byte user area through the frame assembly (host_st.c's rules: a frame starts at sector 0, ends after nSectors, and a
   gap or a changed frame count drops what was gathered) */
static void feedVideo(Builder *b, const uint8_t *u) {
    int sec, nsec;
    unsigned long count;

    if (rd16(u) != 0x0160 || rd16(u + 2) != 0x8001) return;
    sec = (int)rd16(u + 4);
    nsec = (int)rd16(u + 6);
    count = rd32(u + 8);
    if (nsec <= 0 || nsec > ST_MAX_SECTORS || sec < 0 || sec >= nsec) return;
    if (!b->asmActive) {
        if (sec != 0) return;
    } else if (sec == 0 || count != b->asmCount) {
        b->asmActive = 0;
        if (sec != 0) return;
    } else if (sec != b->asmSeen) {
        return;
    }
    if (!b->asmActive) {
        memcpy(b->header, u, ST_HEADER);
        b->asmActive = 1;
        b->asmSeen = 0;
        b->asmSectors = nsec;
        b->asmCount = count;
    }
    memcpy(b->payload + (size_t)b->asmSeen * ST_PAYLOAD, u + ST_HEADER, ST_PAYLOAD);
    b->asmSeen++;
    if (b->asmSeen >= b->asmSectors) {
        b->asmActive = 0;
        frameDone(b);
    }
}

static int stopped(const Builder *b) { return b->stop != NULL && *b->stop; }

static void freeBuilder(Builder *b) {
    free(b->mdec);
    free(b->rgb);
    free(b->jpeg);
    free(b->run);
    free(b->payload);
    free(b->fifo);
}

static int buildMovie(const VideoExportIo *io, const char *name, const char *outPath, VideoExportStats *st, char *err, size_t errCap,
                      void (*progress)(void *ud, int percent), void *progressUd, volatile int *stop);

int videoExportBuild(const VideoExportIo *io, const char *name, const char *outPath, VideoExportStats *st, char *err, size_t errCap,
                     void (*progress)(void *ud, int percent), void *progressUd) {
    return buildMovie(io, name, outPath, st, err, errCap, progress, progressUd, NULL);
}

static int buildMovie(const VideoExportIo *io, const char *name, const char *outPath, VideoExportStats *st, char *err, size_t errCap,
                      void (*progress)(void *ud, int percent), void *progressUd, volatile int *stop) {
    Builder *b;
    VideoExportStats local;
    long lba = 0, sectors = 0, i;
    uint8_t raw[RAW_SECTOR];
    int percent = -1, ok;

    if (st == NULL) st = &local;
    memset(st, 0, sizeof *st);
    if (err != NULL && errCap > 0) err[0] = '\0';
    if (io == NULL || io->movie == NULL || io->read == NULL || name == NULL || outPath == NULL) {
        if (err != NULL && errCap > 0) snprintf(err, errCap, "no disc reader");
        return -1;
    }
    if (!io->movie(io->ud, name, &lba, &sectors) || sectors <= 0) {
        if (err != NULL && errCap > 0) snprintf(err, errCap, "%s is not on the disc", name);
        return -1;
    }
    b = (Builder *)calloc(1, sizeof *b);
    if (b == NULL) return -1;
    b->io = io;
    b->stop = stop;
    b->st = st;
    b->err = err;
    b->errCap = errCap;
    b->outPath = outPath;
    b->mdec = (HostMdec *)malloc(sizeof *b->mdec);
    b->rgb = (uint8_t *)malloc((size_t)MAX_W * MAX_H * 3);
    b->run = (uint8_t *)malloc(RUN_BYTES);
    b->payload = (uint8_t *)malloc((size_t)ST_MAX_SECTORS * ST_PAYLOAD + 64);
    if (b->mdec == NULL || b->rgb == NULL || b->run == NULL || b->payload == NULL) {
        freeBuilder(b);
        free(b);
        if (err != NULL && errCap > 0) snprintf(err, errCap, "out of memory");
        return -1;
    }
    hostMdecInit(b->mdec);
    sndXaResetState(&b->xa);

    /* the sound: the first audio sector says its (file, channel) and whether it is mono or stereo, like the game's own player locks
       onto the first one it sees; a movie with none has no audio stream */
    for (i = 0; i < sectors && i < PRESCAN_SECTORS; i++) {
        SndXaSector sec;
        if (!io->read(io->ud, lba + i, raw)) break;
        if (sndXaClassify(raw, RAW_SECTOR, &sec) && sec.kind == SND_XA_KIND_AUDIO) {
            if ((sec.coding & (SND_XA_CODING_RATE | SND_XA_CODING_BITS)) != 0) {
                fail(b, "%s", "the audio is not 37800 Hz 4-bit XA");
            } else {
                b->channels = (sec.coding & SND_XA_CODING_STEREO) ? 2 : 1;
                b->audioFile = sec.file;
                b->audioChannel = sec.channel;
            }
            break;
        }
    }
    st->channels = b->channels;

    if (progress != NULL) progress(progressUd, 0);
    for (i = 0; i < sectors && !b->failed; i++) {
        SndXaSector sec;
        int pc;

        if (stopped(b)) {
            fail(b, "%s", "stopped");
            break;
        }
        if (!io->read(io->ud, lba + i, raw)) {
            char msg[128];
            snprintf(msg, sizeof msg, "sector %ld of %s cannot be read", lba + i, name);
            fail(b, "%s", msg);
            break;
        }
        if (sndXaClassify(raw, RAW_SECTOR, &sec) && sec.kind == SND_XA_KIND_AUDIO) {
            int n;
            if (b->channels > 0 && sec.file == b->audioFile && sec.channel == b->audioChannel &&
                ((sec.coding & SND_XA_CODING_STEREO) ? 2 : 1) == b->channels) {
                n = sndXaDecodeAudio(raw, RAW_SECTOR, (short *)b->pcm, SND_XA_SECTOR_SAMPLES, &b->xa);
                if (n > 0) queueAudio(b, b->pcm, (size_t)n / (size_t)b->channels);
            }
        } else {
            feedVideo(b, raw + USER_AT);
        }
        pc = (int)((i + 1) * 100 / sectors);
        if (pc != percent) {
            percent = pc;
            if (progress != NULL) progress(progressUd, pc);
        }
    }
    if (!b->failed && b->avi == NULL) fail(b, "%s has no video frames", name);
    if (!b->failed && b->channels > 0) {
        while (b->fifoFrames > 0 && !b->failed) writeAudio(b, b->fifoFrames > SPF ? SPF : b->fifoFrames); /* the tail of the sound */
    }
    if (b->avi != NULL) {
        if (b->failed) {
            aviAbort(b->avi);
        } else if (aviClose(b->avi) != 0) {
            b->failed = 1;
            if (err != NULL && errCap > 0) snprintf(err, errCap, "the AVI could not be finished");
            remove(outPath);
        } else {
            FILE *f = fopen(outPath, "rb");
            if (f != NULL) {
                fseek(f, 0, SEEK_END);
                st->bytes = ftell(f);
                fclose(f);
            }
        }
        b->avi = NULL;
    }
    st->width = b->w;
    st->height = b->h;
    st->audioFrames = b->audioWritten;
    ok = !b->failed;
    freeBuilder(b);
    free(b);
    return ok ? 0 : -1;
}

/* ---------------------------------------------------------------- the job and the data folder */

static char sDataDir[1024];
static VideoExportIo sIo;
static int *sQueue;
static int sCount, sNext, sDone, sFailed, sBuilt;
static int sForce;
static TdManifest sMan;
static int sManLoaded, sManDirty;
static char sStatus[96], sStatusOut[96];
static SDL_mutex *sMu;
static SDL_Thread *sThread;
static volatile int sStop;
static int sWorkerDone;

static void lockMu(void) {
    if (sMu != NULL) SDL_LockMutex(sMu);
}

static void unlockMu(void) {
    if (sMu != NULL) SDL_UnlockMutex(sMu);
}

static void manifestPath(char *out, size_t cap) { snprintf(out, cap, "%s/video/manifest.json", sDataDir); }

static void manifestLoad(void) {
    char mp[1200];
    uint8_t *mt;
    size_t ml;

    if (sManLoaded) return;
    memset(&sMan, 0, sizeof sMan);
    snprintf(sMan.stamp, sizeof sMan.stamp, "video-v1");
    manifestPath(mp, sizeof mp);
    if (tdReadFile(mp, &mt, &ml) == 0) {
        TdManifest old;
        char err[120];
        memset(&old, 0, sizeof old);
        if (tdManifestParse((const char *)mt, ml, &old, err, sizeof err) == 0) {
            tdManifestFree(&sMan);
            sMan = old;
        }
        free(mt);
    }
    sManLoaded = 1;
    sManDirty = 0;
}

static void manifestSave(void) {
    char mp[1200], dir[1200];
    char *text;

    if (!sManLoaded || !sManDirty) return;
    snprintf(dir, sizeof dir, "%s/video", sDataDir);
    manifestPath(mp, sizeof mp);
    text = tdManifestFormat(&sMan);
    if (text == NULL || tdMakeDirs(dir) || tdWriteFileAtomic(mp, text, strlen(text))) plogLine("video", "cannot write %s\n", mp);
    else sManDirty = 0;
    free(text);
}

static int fileExists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

/* FNV hash of a whole file, read in pieces (the files are tens of MB); 0 when it cannot be read */
static int hashFile(const char *path, char hex[17]) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    uint64_t h = TD_HASH_INIT;
    size_t n;

    if (f == NULL) return 0;
    buf = (uint8_t *)malloc(1 << 16);
    if (buf == NULL) {
        fclose(f);
        return 0;
    }
    while ((n = fread(buf, 1, 1 << 16, f)) > 0) h = tdHashUpdate(h, buf, n);
    free(buf);
    fclose(f);
    tdHashHex(h, hex);
    return 1;
}

static void versionKey(const char *name, char *rel, size_t cap) { snprintf(rel, cap, "%s/exporter", name); }

static void versionHex(char *hex) { snprintf(hex, 17, "%016x", (unsigned)VIDEO_EXPORT_VERSION); }

/* written by this version and the file is there (an edited file still counts): nothing to build */
static int upToDate(const char *name) {
    char rel[96], path[1300], hex[17];
    const char *have;

    manifestLoad();
    versionKey(name, rel, sizeof rel);
    have = tdManifestFind(&sMan, rel);
    versionHex(hex);
    if (have == NULL || strcmp(have, hex)) return 0;
    snprintf(path, sizeof path, "%s/video/%s.avi", sDataDir, name);
    return fileExists(path);
}

/* The finished <name>.avi.part under the manifest rule of ADR-005: an unedited copy is replaced, an edited one stays and the new
   version goes beside it as <name>.avi.new */
static int place(const char *name, const char *part) {
    char rel[96], path[1300], np[1400], hex[17], curHex[17];
    int edited = 0;

    snprintf(rel, sizeof rel, "%s.avi", name);
    snprintf(path, sizeof path, "%s/video/%s", sDataDir, rel);
    if (!hashFile(part, hex)) {
        remove(part);
        return -1;
    }
    if (fileExists(path) && hashFile(path, curHex)) {
        if (!strcmp(hex, curHex)) {
            remove(part);
            if (!tdManifestFind(&sMan, rel)) {
                tdManifestSet(&sMan, rel, hex);
                sManDirty = 1;
            }
            return 0;
        }
        edited = tdFileState(1, tdManifestFind(&sMan, rel), curHex) == TD_FILE_EDITED;
    }
    if (edited) {
        snprintf(np, sizeof np, "%s.new", path);
        remove(np);
        if (rename(part, np) != 0) {
            plogLine("video", "cannot write %s\n", np);
            remove(part);
            return -1;
        }
        plogLine("video", "%s was edited, kept; the new version is %s.new\n", rel, rel);
        return 0;
    }
    remove(path); /* rename does not replace an existing file on Windows */
    if (rename(part, path) != 0) {
        plogLine("video", "cannot write %s\n", path);
        remove(part);
        return -1;
    }
    tdManifestSet(&sMan, rel, hex);
    sManDirty = 1;
    return 0;
}

/* 0: built (or already up to date), -1: failed */
static int exportOne(int index, int force) {
    const char *name = kNames[index];
    char dir[1300], part[1400], err[256], rel[96], hex[17];
    VideoExportStats st;
    clock_t t0 = clock();

    manifestLoad();
    if (!force && upToDate(name)) return 0;
    snprintf(dir, sizeof dir, "%s/video", sDataDir);
    if (tdMakeDirs(dir)) {
        plogLine("video", "cannot create %s\n", dir);
        return -1;
    }
    snprintf(part, sizeof part, "%s/%s.avi.part", dir, name);
    if (buildMovie(&sIo, name, part, &st, err, sizeof err, NULL, NULL, &sStop) != 0) {
        remove(part);
        plogLine("video", "%s not exported: %s\n", name, err);
        return -1;
    }
    if (place(name, part) != 0) return -1;
    versionKey(name, rel, sizeof rel);
    versionHex(hex);
    if (tdManifestSet(&sMan, rel, hex) == 0) sManDirty = 1;
    manifestSave();
    lockMu();
    sBuilt++;
    unlockMu();
    plogLine("video", "%s: %d frames, %dx%d, %s, %ld bytes, %.1f s\n", name, st.frames, st.width, st.height,
             st.channels == 2 ? "stereo" : (st.channels == 1 ? "mono" : "no audio"), st.bytes, (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 0;
}

static void finishStatus(void) {
    if (sFailed) snprintf(sStatus, sizeof sStatus, "Done, %d failed", sFailed);
    else snprintf(sStatus, sizeof sStatus, "Done (%d)", sDone);
    plogLine("video", "Done: %d movies in %s/video, %d failed\n", sDone, sDataDir, sFailed);
}

static int workerMain(void *arg) {
    int j;
    (void)arg;
    for (j = 0; j < sCount && !sStop; j++) {
        int r = exportOne(sQueue[j], sForce);
        SDL_LockMutex(sMu);
        if (r == 0) sDone++;
        else if (!sStop) sFailed++;
        sNext = j + 1;
        if (sNext < sCount) snprintf(sStatus, sizeof sStatus, "Exporting %d/%d", sNext + 1, sCount);
        SDL_UnlockMutex(sMu);
    }
    manifestSave();
    SDL_LockMutex(sMu);
    if (j >= sCount) finishStatus();
    sWorkerDone = 1;
    SDL_UnlockMutex(sMu);
    return 0;
}

static int workerBusy(void) {
    int busy;
    if (sThread == NULL) return 0;
    lockMu();
    busy = !sWorkerDone;
    unlockMu();
    return busy;
}

static void reap(void) {
    if (sThread == NULL) return;
    SDL_WaitThread(sThread, NULL);
    sThread = NULL;
    free(sQueue);
    sQueue = NULL;
}

static void dropJob(void) {
    free(sQueue);
    sQueue = NULL;
    sCount = sNext = 0;
}

void videoExportSetup(const char *dataDir, const VideoExportIo *io) {
    videoExportShutdown();
    dropJob();
    if (sManLoaded) tdManifestFree(&sMan);
    sManLoaded = 0;
    sStatus[0] = '\0';
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

static int nameEq(const char *p, size_t n, const char *name) {
    size_t i;
    if (n != strlen(name)) return 0;
    for (i = 0; i < n; i++) {
        if (toupper((unsigned char)p[i]) != name[i]) return 0;
    }
    return 1;
}

static int onDisc(int index) {
    long lba, sectors;
    return sIo.movie != NULL && sIo.movie(sIo.ud, kNames[index], &lba, &sectors) && sectors > 0;
}

int videoExportRequest(const char *spec) { return videoExportRequestEx(spec, 0); }

int videoExportRequestEx(const char *spec, int force) {
    int *q, n = 0, i;
    const char *p;

    if (workerBusy()) return -1; /* one job at a time: a press while one runs changes nothing */
    reap();
    dropJob();
    sDone = sFailed = sBuilt = 0;
    sForce = force;
    sStatus[0] = '\0';
    if (!sDataDir[0] || sIo.movie == NULL || sIo.read == NULL || spec == NULL) return -1;
    q = (int *)malloc(sizeof(int) * (size_t)N_MOVIES);
    if (q == NULL) return -1;
    p = spec;
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, "all", 3) && (p[3] == '\0' || p[3] == ' ' || p[3] == ',' || p[3] == '\t')) {
        for (i = 0; i < N_MOVIES; i++) {
            if (onDisc(i)) q[n++] = i;
        }
    } else {
        while (*p) {
            const char *s;
            size_t len;
            int k, j;

            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            if (!*p) break;
            s = p;
            while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
            len = (size_t)(p - s);
            if (len > 4 && (nameEq(s + len - 4, 4, ".STR") || nameEq(s + len - 4, 4, ".AVI"))) len -= 4;
            for (k = 0; k < N_MOVIES; k++) {
                if (nameEq(s, len, kNames[k])) break;
            }
            if (k == N_MOVIES) {
                plogLine("video", "export_videos: \"%.*s\" is not a movie (use all or names like MOVIED01 separated by commas)\n", (int)(p - s), s);
                continue;
            }
            if (!onDisc(k)) {
                plogLine("video", "export_videos: %s is not on this disc\n", kNames[k]);
                continue;
            }
            for (j = 0; j < n; j++) {
                if (q[j] == k) break;
            }
            if (j == n) q[n++] = k;
        }
    }
    if (n == 0) {
        free(q);
        return -1;
    }
    sQueue = q;
    sCount = n;
    sNext = 0;
    snprintf(sStatus, sizeof sStatus, "Exporting 1/%d", n);
    return n;
}

int videoExportStart(const char *spec, int force) {
    int n = videoExportRequestEx(spec, force);

    if (n <= 0) return -1;
    if (sMu == NULL) sMu = SDL_CreateMutex();
    if (sMu != NULL) {
        sStop = 0;
        sWorkerDone = 0;
        sThread = SDL_CreateThread(workerMain, "dw3-video-export", NULL);
    }
    if (sThread == NULL) {
        plogLine("video", "the export thread cannot start: %s\n", SDL_GetError());
        dropJob();
        return -1;
    }
    return n;
}

void videoExportShutdown(void) {
    if (sThread == NULL) return;
    sStop = 1;
    reap();
    sStop = 0;
}

int videoExportActive(void) {
    if (sThread != NULL) return workerBusy();
    return sQueue != NULL && sNext < sCount;
}

void videoExportCounts(int *done, int *failed, int *total) {
    lockMu();
    if (done) *done = sDone;
    if (failed) *failed = sFailed;
    if (total) *total = sCount;
    unlockMu();
}

int videoExportBuilt(void) {
    int n;
    lockMu();
    n = sBuilt;
    unlockMu();
    return n;
}

const char *videoExportStatus(void) {
    lockMu();
    snprintf(sStatusOut, sizeof sStatusOut, "%s", sStatus);
    unlockMu();
    return sStatusOut;
}

int videoExportPump(unsigned budgetMs) {
    clock_t start = clock();

    if (sThread != NULL) {
        if (workerBusy()) return 1;
        reap();
        return 0;
    }
    if (!videoExportActive()) return 0;
    do {
        if (exportOne(sQueue[sNext], sForce) == 0) sDone++;
        else sFailed++;
        sNext++;
        if (sNext < sCount) snprintf(sStatus, sizeof sStatus, "Exporting %d/%d", sNext + 1, sCount);
    } while (sNext < sCount && (unsigned)((clock() - start) * 1000 / CLOCKS_PER_SEC) < budgetMs);
    if (sNext < sCount) return 1;
    manifestSave();
    finishStatus();
    free(sQueue);
    sQueue = NULL;
    return 0;
}
