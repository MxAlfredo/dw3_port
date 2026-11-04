/* A mod's MJPEG AVI in place of a game movie; see video_mod.h for the design (content-export E2b) */
#include "video_mod.h"

#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "avi_read.h"
#include "jpeg_read.h"
#include "port_log.h"
#include "video_export.h" /* the 14 movie names */

#define VM_PAD_FRAMES 18900     /* silence after the end of the movie: the clock must outlast the last picture */
#define VM_MAX_CHUNK 4032       /* the most sample frames the XA ring asks for at a time */
#define VM_MAGIC0 'D'
#define VM_MAGIC1 'W'
#define VM_MAGIC2 '3'
#define VM_MAGIC3 'V'

typedef struct VmState {
    int active;
    char mod[64];
    char name[16];
    AviReader *video; /* the game thread */
    AviReader *audio; /* the XA driver's thread (under lock) */
    AviInfo info;
    long frames;
    long long rate, scale; /* the AVI's frames per second = rate / scale */
    long srcOut;           /* the AVI's audio, in sample frames at VIDEO_MOD_RATE */
    long totalOut;         /* what videoModReadAudio delivers in all */
    long endDue;           /* the sample frame at which the end record is released */
    long audioPos;
    int16_t *tmp;          /* source audio for one chunk */
    long tmpCap;           /* in shorts */
    uint8_t *jbuf;
    size_t jcap;
    uint8_t *last;         /* the last decoded picture, VIDEO_MOD_W x VIDEO_MOD_H RGB */
    long curIdx;           /* the frame `last` holds (0-based) */
    int badLogged;
    long armLba;           /* the movie's first sector */
} VmState;

static VmState vm;
static pthread_mutex_t vmLock = PTHREAD_MUTEX_INITIALIZER;
static VideoModIo vmIo;
static int vmHaveIo;
static long vmLbas[32];
static int vmLbaOk[32];
static int vmLbaKnown;

/* --- the picture ---------------------------------------------------------- */

/* Bilinear to VIDEO_MOD_W x VIDEO_MOD_H, edges clamped */
static void fitRgb(const uint8_t *src, int sw, int sh, uint8_t *dst) {
    int x, y, c;
    int x0[VIDEO_MOD_W], x1[VIDEO_MOD_W], fx[VIDEO_MOD_W];

    if (sw == VIDEO_MOD_W && sh == VIDEO_MOD_H) {
        memcpy(dst, src, (size_t)sw * (size_t)sh * 3);
        return;
    }
    for (x = 0; x < VIDEO_MOD_W; x++) {
        long long pos = (((long long)(2 * x + 1) * sw) << 15) / VIDEO_MOD_W - 32768;

        if (pos < 0) pos = 0;
        x0[x] = (int)(pos >> 16);
        if (x0[x] > sw - 1) x0[x] = sw - 1;
        x1[x] = x0[x] + 1 < sw ? x0[x] + 1 : sw - 1;
        fx[x] = (int)((pos & 0xFFFF) >> 8);
    }
    for (y = 0; y < VIDEO_MOD_H; y++) {
        long long pos = (((long long)(2 * y + 1) * sh) << 15) / VIDEO_MOD_H - 32768;
        int y0, y1, fy;
        const uint8_t *r0, *r1;
        uint8_t *o = dst + (size_t)y * VIDEO_MOD_W * 3;

        if (pos < 0) pos = 0;
        y0 = (int)(pos >> 16);
        if (y0 > sh - 1) y0 = sh - 1;
        y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
        fy = (int)((pos & 0xFFFF) >> 8);
        r0 = src + (size_t)y0 * (size_t)sw * 3;
        r1 = src + (size_t)y1 * (size_t)sw * 3;
        for (x = 0; x < VIDEO_MOD_W; x++) {
            for (c = 0; c < 3; c++) {
                int a = r0[x0[x] * 3 + c] * (256 - fx[x]) + r0[x1[x] * 3 + c] * fx[x];
                int b = r1[x0[x] * 3 + c] * (256 - fx[x]) + r1[x1[x] * 3 + c] * fx[x];

                *o++ = (uint8_t)(((a >> 8) * (256 - fy) + (b >> 8) * fy) >> 8);
            }
        }
    }
}

/* Frame `idx` (0-based) of the AVI into vm.last. 0 on success, -1 with the reason in err. A zero-byte chunk repeats the picture. */
static int loadFrame(long idx, char *err, size_t errCap) {
    long size = aviReadVideoSize(vm.video, idx);
    uint8_t *rgb = NULL;
    int w = 0, h = 0;

    if (size < 0) {
        snprintf(err, errCap, "frame %ld is outside the file", idx + 1);
        return -1;
    }
    if (size == 0) {
        if (vm.curIdx < 0) {
            snprintf(err, errCap, "the first frame is empty");
            return -1;
        }
        vm.curIdx = idx;
        return 0;
    }
    if ((size_t)size > vm.jcap) {
        uint8_t *nb = (uint8_t *)realloc(vm.jbuf, (size_t)size);

        if (nb == NULL) {
            snprintf(err, errCap, "out of memory");
            return -1;
        }
        vm.jbuf = nb;
        vm.jcap = (size_t)size;
    }
    if (aviReadVideo(vm.video, idx, vm.jbuf, vm.jcap) != size) {
        snprintf(err, errCap, "frame %ld cannot be read", idx + 1);
        return -1;
    }
    if (jpegDecodeRgb24(vm.jbuf, (size_t)size, &rgb, &w, &h, err, errCap) != 0) {
        return -1;
    }
    fitRgb(rgb, w, h, vm.last);
    free(rgb);
    vm.curIdx = idx;
    return 0;
}

/* --- open and close ------------------------------------------------------- */

static void closeReplacement(void) {
    pthread_mutex_lock(&vmLock);
    vm.active = 0;
    aviReadClose(vm.video);
    aviReadClose(vm.audio);
    vm.video = vm.audio = NULL;
    free(vm.tmp);
    free(vm.jbuf);
    free(vm.last);
    vm.tmp = NULL;
    vm.jbuf = NULL;
    vm.last = NULL;
    vm.tmpCap = 0;
    vm.jcap = 0;
    pthread_mutex_unlock(&vmLock);
}

static long long framesToSamples(long long frames) {
    return frames * VIDEO_MOD_RATE * vm.scale / vm.rate;
}

/* Opens `path` as the replacement of `name` from `mod`. 1 when it plays, 0 with the reason in err. */
static int openReplacement(const char *path, const char *mod, const char *name, char *err, size_t errCap) {
    long long fpsNum;
    long hold, videoEnd;

    memset(&vm, 0, sizeof vm);
    vm.video = aviReadOpen(path, &vm.info, err, errCap);
    if (vm.video == NULL) goto bad;
    vm.rate = vm.info.rate;
    vm.scale = vm.info.scale;
    fpsNum = vm.rate / vm.scale; /* whole frames per second, rounded down */
    if (fpsNum < 1 || vm.rate > 60 * vm.scale) {
        snprintf(err, errCap, "frame rate %.2f is outside 1..60 fps", (double)vm.rate / (double)vm.scale);
        goto bad;
    }
    vm.frames = vm.info.videoFrames;
    vm.last = (uint8_t *)malloc((size_t)VIDEO_MOD_W * VIDEO_MOD_H * 3);
    if (vm.last == NULL) {
        snprintf(err, errCap, "out of memory");
        goto bad;
    }
    vm.curIdx = -1;
    {
        char e2[160];

        if (loadFrame(0, e2, sizeof e2) != 0) {
            snprintf(err, errCap, "first frame: %s", e2);
            goto bad;
        }
    }
    if (vm.info.hasAudio) {
        vm.audio = aviReadOpen(path, NULL, err, errCap);
        if (vm.audio == NULL) goto bad;
        vm.srcOut = (long)(((long long)vm.info.audioFrames * VIDEO_MOD_RATE + vm.info.sampleRate - 1) / vm.info.sampleRate);
        vm.tmpCap = ((long)VM_MAX_CHUNK * vm.info.sampleRate / VIDEO_MOD_RATE + 4) * vm.info.channels;
        vm.tmp = (int16_t *)malloc((size_t)vm.tmpCap * sizeof(int16_t));
        if (vm.tmp == NULL) {
            snprintf(err, errCap, "out of memory");
            goto bad;
        }
    }
    videoEnd = (long)framesToSamples(vm.frames);
    hold = (long)((long long)VIDEO_MOD_RATE * VIDEO_MOD_MAX_HOLD_MS / 1000);
    vm.endDue = vm.srcOut > videoEnd ? vm.srcOut : videoEnd;
    if (vm.endDue > videoEnd + hold) vm.endDue = videoEnd + hold;
    vm.totalOut = (vm.srcOut > videoEnd ? vm.srcOut : videoEnd) + VM_PAD_FRAMES;
    snprintf(vm.mod, sizeof vm.mod, "%s", mod);
    snprintf(vm.name, sizeof vm.name, "%s", name);
    vm.active = 1;
    return 1;
bad:
    aviReadClose(vm.video);
    aviReadClose(vm.audio);
    free(vm.tmp);
    free(vm.jbuf);
    free(vm.last);
    memset(&vm, 0, sizeof vm);
    return 0;
}

void videoModSetup(const VideoModIo *io) {
    videoModStop();
    vmHaveIo = io != NULL;
    if (io != NULL) vmIo = *io;
    vmLbaKnown = 0;
}

void videoModStop(void) {
    if (vm.active || vm.video != NULL) {
        closeReplacement();
        memset(&vm, 0, sizeof vm);
    }
}

int videoModActive(void) {
    return vm.active;
}

const char *videoModCurrentMod(void) {
    return vm.active ? vm.mod : "";
}

const char *videoModCurrentName(void) {
    return vm.active ? vm.name : "";
}

long videoModFrames(void) {
    return vm.active ? vm.frames : 0;
}

static int fileExists(const char *path) {
    FILE *f = fopen(path, "rb");

    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

int videoModArm(long lba) {
    int i, m, count;
    const char *name = NULL;
    char lower[16];

    videoModStop();
    if (!vmHaveIo || vmIo.movieLba == NULL || vmIo.modCount == NULL) return 0;
    count = videoExportCount();
    if (count > 32) count = 32;
    if (!vmLbaKnown) {
        for (i = 0; i < count; i++) vmLbaOk[i] = vmIo.movieLba(vmIo.ud, videoExportName(i), &vmLbas[i]);
        vmLbaKnown = 1;
    }
    for (i = 0; i < count; i++) {
        if (vmLbaOk[i] && vmLbas[i] == lba) {
            name = videoExportName(i);
            break;
        }
    }
    if (name == NULL) return 0;
    for (i = 0; name[i] != '\0' && i < 15; i++) lower[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    lower[i] = '\0';
    for (m = vmIo.modCount(vmIo.ud) - 1; m >= 0; m--) {
        const char *dir = vmIo.modDir(vmIo.ud, m);
        const char *mod = vmIo.modName(vmIo.ud, m);
        char path[1024];
        char err[200];
        int k, found = 0;

        if (dir == NULL) continue;
        for (k = 0; k < 3 && !found; k++) {
            if (k == 0) snprintf(path, sizeof path, "%s/video/%s.avi", dir, name);
            else if (k == 1) snprintf(path, sizeof path, "%s/video/%s.AVI", dir, name);
            else snprintf(path, sizeof path, "%s/video/%s.avi", dir, lower);
            found = fileExists(path);
        }
        if (!found) continue;
        err[0] = '\0';
        if (openReplacement(path, mod != NULL ? mod : "?", name, err, sizeof err)) {
            vm.armLba = lba;
            PLOG("video", PLOG_MODS, "%s: %s.avi replaces the movie (%ld frames, %dx%d, %.2f fps, %s)\n", vm.mod, name, vm.frames,
                 vm.info.width, vm.info.height, (double)vm.rate / (double)vm.scale,
                 vm.info.hasAudio ? "with audio" : "no audio");
            return 1;
        }
        plogLine("video", "%s: %s.avi rejected: %s\n", mod != NULL ? mod : "?", name, err[0] ? err : "unusable file");
    }
    return 0;
}

/* --- the St layer's side -------------------------------------------------- */

static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char *p, unsigned long v) {
    put16(p, (unsigned)(v & 0xFFFF));
    put16(p + 2, (unsigned)((v >> 16) & 0xFFFF));
}

/* Sector k of the replacement (k = lba - the movie's first sector): the record of frame k + 1; sector `frames` is the end record */
int videoModReadSector(void *ctx, long lba, unsigned char *sector) {
    long k;
    int isEnd;
    (void)ctx;
    if (!vm.active || sector == NULL) return 0;
    k = lba - vm.armLba;
    if (k < 0 || k > vm.frames) return 0;
    isEnd = (k == vm.frames);
    memset(sector, 0, 2048);
    put16(sector + 0, 0x0160); /* the St layer's id and type (host_st.h) */
    put16(sector + 2, 0x8001);
    put16(sector + 4, 0); /* secCount */
    put16(sector + 6, 1); /* nSectors: the whole frame is this one record */
    put32(sector + 8, isEnd ? 0xFFFFFFFFul : 0ul); /* frameCount: only the end record reaches the movie's end frame */
    put32(sector + 12, 2016);
    put16(sector + 16, VIDEO_MOD_W);
    put16(sector + 18, VIDEO_MOD_H);
    sector[32] = VM_MAGIC0;
    sector[33] = VM_MAGIC1;
    sector[34] = VM_MAGIC2;
    sector[35] = VM_MAGIC3;
    put32(sector + 36, (unsigned long)(isEnd ? vm.frames : k + 1));
    put32(sector + 40, isEnd ? 1ul : 0ul);
    return 1;
}

int videoModRecord(const void *payload, long *frameNo, long *due, int *isEnd) {
    const unsigned char *p = (const unsigned char *)payload;
    long no;
    int end;

    if (p == NULL || p[0] != VM_MAGIC0 || p[1] != VM_MAGIC1 || p[2] != VM_MAGIC2 || p[3] != VM_MAGIC3) return 0;
    no = (long)((unsigned long)p[4] | ((unsigned long)p[5] << 8) | ((unsigned long)p[6] << 16) | ((unsigned long)p[7] << 24));
    end = p[8] != 0;
    if (frameNo != NULL) *frameNo = no;
    if (isEnd != NULL) *isEnd = end;
    if (due != NULL) {
        *due = 0;
        if (vm.active) *due = end ? vm.endDue : (no > 1 ? (long)framesToSamples(no - 1) : 0);
    }
    return 1;
}

int videoModFrame15(long frameNo, unsigned short *out) {
    long idx = frameNo - 1;
    const uint8_t *s;
    long i;

    if (!vm.active || out == NULL || idx < 0 || idx >= vm.frames) return 0;
    if (idx != vm.curIdx) {
        char err[200];

        if (loadFrame(idx, err, sizeof err) != 0 && !vm.badLogged) {
            vm.badLogged = 1;
            plogLine("video", "%s: %s.avi frame %ld is damaged (%s); the previous picture stays\n", vm.mod, vm.name, idx + 1, err);
        }
        vm.curIdx = idx; /* a failed frame is not tried twice */
    }
    s = vm.last;
    for (i = 0; i < (long)VIDEO_MOD_W * VIDEO_MOD_H; i++, s += 3) {
        out[i] = (unsigned short)(((s[2] >> 3) << 10) | ((s[1] >> 3) << 5) | (s[0] >> 3));
    }
    return 1;
}

/* --- the XA driver's side ------------------------------------------------- */

int videoModReadAudio(void *ctx, short *pcm, int frames) {
    long n, srcStart, srcEnd, cnt, got, j;
    int ch, c;
    (void)ctx;

    pthread_mutex_lock(&vmLock);
    if (!vm.active || pcm == NULL || frames <= 0 || vm.audioPos >= vm.totalOut) {
        pthread_mutex_unlock(&vmLock);
        return 0;
    }
    n = frames > VM_MAX_CHUNK ? VM_MAX_CHUNK : frames;
    if (n > vm.totalOut - vm.audioPos) n = vm.totalOut - vm.audioPos;
    memset(pcm, 0, (size_t)n * 2 * sizeof(short));
    if (vm.audio != NULL && vm.audioPos < vm.srcOut) {
        long long R = vm.info.sampleRate;

        ch = vm.info.channels;
        srcStart = (long)(((long long)vm.audioPos * R) / VIDEO_MOD_RATE);
        srcEnd = (long)(((long long)(vm.audioPos + n - 1) * R) / VIDEO_MOD_RATE) + 2;
        cnt = srcEnd - srcStart;
        if (cnt * ch > vm.tmpCap) cnt = vm.tmpCap / ch;
        got = aviReadAudio(vm.audio, srcStart, cnt, vm.tmp);
        if (got < cnt) memset(vm.tmp + got * ch, 0, (size_t)((cnt - got) * ch) * sizeof(int16_t));
        for (j = 0; j < n; j++) {
            long long sp = (long long)(vm.audioPos + j) * R;
            long si = (long)(sp / VIDEO_MOD_RATE) - srcStart;
            long long fr = sp % VIDEO_MOD_RATE;
            long long v[2];

            if (si + 1 >= cnt) break;
            for (c = 0; c < ch; c++) {
                long long s0 = vm.tmp[si * ch + c], s1 = vm.tmp[(si + 1) * ch + c];

                v[c] = (s0 * (VIDEO_MOD_RATE - fr) + s1 * fr) / VIDEO_MOD_RATE;
            }
            pcm[j * 2] = (short)v[0];
            pcm[j * 2 + 1] = (short)v[ch == 2 ? 1 : 0];
        }
    }
    vm.audioPos += n;
    pthread_mutex_unlock(&vmLock);
    return (int)n;
}

static const VideoModHooks vmHooks = {videoModArm, videoModStop, videoModReadSector, videoModReadAudio};

const VideoModHooks *videoModHooks(void) {
    return &vmHooks;
}

/* --validate-mod (U5): every video/<NAME>.avi of one mod through openReplacement, the check videoModArm makes; the file name must be one of
   the disc's movies. Refusals are the usual `[video] <mod>: <NAME>.avi rejected: <why>` line. Returns how many files were looked at. */
int videoModValidate(const char *modDir, const char *modName) {
    char dir[1100];
    DIR *d;
    struct dirent *e;
    int n = 0;

    snprintf(dir, sizeof dir, "%s/video", modDir);
    d = opendir(dir);
    if (d == NULL) return 0;
    videoModStop();
    while ((e = readdir(d)) != NULL) {
        char stem[64], path[1400], err[200];
        size_t len = strlen(e->d_name), i;
        int known = 0;
        if (e->d_name[0] == '.') continue;
        if (len < 5 || len - 4 >= sizeof stem || strcasecmp(e->d_name + len - 4, ".avi")) {
            plogLine("video", "%s: %s ignored: only <NAME>.avi files are read\n", modName, e->d_name);
            continue;
        }
        memcpy(stem, e->d_name, len - 4);
        stem[len - 4] = '\0';
        for (i = 0; i < (size_t)videoExportCount(); i++) known |= strcasecmp(stem, videoExportName((int)i)) == 0;
        if (!known) {
            plogLine("video", "%s: %s rejected: no movie of the disc is called %s\n", modName, e->d_name, stem);
            continue;
        }
        n++;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        err[0] = '\0';
        if (openReplacement(path, modName, stem, err, sizeof err)) videoModStop();
        else plogLine("video", "%s: %s rejected: %s\n", modName, e->d_name, err[0] ? err : "unusable file");
    }
    closedir(d);
    return n;
}
