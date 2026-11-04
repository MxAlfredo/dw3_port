/* A streaming AVI 1.0 writer; see avi_write.h for the layout (content-export E2a) */
#include "avi_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct IdxEntry {
    uint32_t off, size;
    uint8_t audio;
} IdxEntry;

struct AviWriter {
    FILE *f;
    char *path;
    AviParams p;
    int failed;
    long pos;       /* bytes written so far (the file's end) */
    long moviStart; /* file position of the 'movi' fourcc */
    long vframes, aframes;
    uint32_t maxVideo, maxAudio;
    IdxEntry *idx;
    size_t nIdx, capIdx;
};

#define HDR_MAX 512

static void le16(uint8_t *b, size_t *n, unsigned v) {
    b[(*n)++] = (uint8_t)v;
    b[(*n)++] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *b, size_t *n, uint32_t v) {
    le16(b, n, v & 0xFFFF);
    le16(b, n, v >> 16);
}

static void fourcc(uint8_t *b, size_t *n, const char *s) {
    memcpy(b + *n, s, 4);
    *n += 4;
}

/* The whole header up to and including the 'movi' fourcc, with the counts known so far; its length does not depend on them.
   Returns the length. */
static size_t buildHeader(const AviWriter *w, uint8_t *b, long total, int final) {
    size_t n = 0;
    int audio = w->p.channels > 0;
    int align = w->p.channels * 2;
    int vprp = w->p.aspectW > 0 && w->p.aspectH > 0;
    uint32_t vprpSize = vprp ? 8 + 36 + 32 : 0; /* chunk header, VideoPropHeader, one VIDEO_FIELD_DESC */
    uint32_t hdrlSize = 4 + 64 + 124 + vprpSize + (audio ? 100 : 0);
    uint32_t moviSize = final ? (uint32_t)(w->pos - w->moviStart) : 4;
    uint32_t maxBuf = w->maxVideo > w->maxAudio ? w->maxVideo : w->maxAudio;
    uint32_t bps = 0;

    if (final && w->vframes > 0) bps = (uint32_t)((double)total * (double)w->p.fps / (double)w->vframes);

    fourcc(b, &n, "RIFF");
    le32(b, &n, final ? (uint32_t)(total - 8) : 0);
    fourcc(b, &n, "AVI ");
    fourcc(b, &n, "LIST");
    le32(b, &n, hdrlSize);
    fourcc(b, &n, "hdrl");

    fourcc(b, &n, "avih");
    le32(b, &n, 56);
    le32(b, &n, (uint32_t)(1000000 / w->p.fps) + ((1000000 % w->p.fps) * 2 >= (uint32_t)w->p.fps ? 1 : 0)); /* rounded: 66667 at 15 fps */
    le32(b, &n, bps);
    le32(b, &n, 0);
    le32(b, &n, 0x10 | 0x100); /* AVIF_HASINDEX | AVIF_ISINTERLEAVED */
    le32(b, &n, (uint32_t)w->vframes);
    le32(b, &n, 0);
    le32(b, &n, audio ? 2 : 1);
    le32(b, &n, maxBuf);
    le32(b, &n, (uint32_t)w->p.width);
    le32(b, &n, (uint32_t)w->p.height);
    le32(b, &n, 0);
    le32(b, &n, 0);
    le32(b, &n, 0);
    le32(b, &n, 0);

    /* the video stream: 'MJPG' */
    fourcc(b, &n, "LIST");
    le32(b, &n, 4 + 64 + 48 + vprpSize);
    fourcc(b, &n, "strl");
    fourcc(b, &n, "strh");
    le32(b, &n, 56);
    fourcc(b, &n, "vids");
    fourcc(b, &n, "MJPG");
    le32(b, &n, 0);
    le16(b, &n, 0);
    le16(b, &n, 0);
    le32(b, &n, 0);
    le32(b, &n, 1);
    le32(b, &n, (uint32_t)w->p.fps);
    le32(b, &n, 0);
    le32(b, &n, (uint32_t)w->vframes);
    le32(b, &n, w->maxVideo);
    le32(b, &n, 0xFFFFFFFFu);
    le32(b, &n, 0);
    le16(b, &n, 0);
    le16(b, &n, 0);
    le16(b, &n, (unsigned)w->p.width);
    le16(b, &n, (unsigned)w->p.height);
    fourcc(b, &n, "strf");
    le32(b, &n, 40);
    le32(b, &n, 40);
    le32(b, &n, (uint32_t)w->p.width);
    le32(b, &n, (uint32_t)w->p.height);
    le16(b, &n, 1);
    le16(b, &n, 24);
    fourcc(b, &n, "MJPG");
    le32(b, &n, (uint32_t)w->p.width * (uint32_t)w->p.height * 3);
    le32(b, &n, 0);
    le32(b, &n, 0);
    le32(b, &n, 0);
    le32(b, &n, 0);
    if (vprp) { /* OpenDML VideoPropHeader: a progressive frame whose display shape is aspectW:aspectH */
        uint32_t cw = (uint32_t)w->p.width, ch = (uint32_t)w->p.height;
        fourcc(b, &n, "vprp");
        le32(b, &n, 36 + 32);
        le32(b, &n, 0);                   /* VideoFormatToken: unknown */
        le32(b, &n, 0);                   /* VideoStandard: unknown */
        le32(b, &n, (uint32_t)w->p.fps);  /* dwVerticalRefreshRate */
        le32(b, &n, cw);                  /* dwHTotalInT */
        le32(b, &n, ch);                  /* dwVTotalInLines */
        le32(b, &n, ((uint32_t)w->p.aspectW << 16) | ((uint32_t)w->p.aspectH & 0xFFFF)); /* dwFrameAspectRatio */
        le32(b, &n, cw);                  /* dwFrameWidthInPixels */
        le32(b, &n, ch);                  /* dwFrameHeightInLines */
        le32(b, &n, 1);                   /* nbFieldPerFrame */
        le32(b, &n, ch);                  /* CompressedBMHeight */
        le32(b, &n, cw);                  /* CompressedBMWidth */
        le32(b, &n, ch);                  /* ValidBMHeight */
        le32(b, &n, cw);                  /* ValidBMWidth */
        le32(b, &n, 0);                   /* ValidBMXOffset */
        le32(b, &n, 0);                   /* ValidBMYOffset */
        le32(b, &n, 0);                   /* VideoXOffsetInT */
        le32(b, &n, 0);                   /* VideoYValidStartLine */
    }

    if (audio) {
        fourcc(b, &n, "LIST");
        le32(b, &n, 4 + 64 + 24);
        fourcc(b, &n, "strl");
        fourcc(b, &n, "strh");
        le32(b, &n, 56);
        fourcc(b, &n, "auds");
        le32(b, &n, 0);
        le32(b, &n, 0);
        le16(b, &n, 0);
        le16(b, &n, 0);
        le32(b, &n, 0);
        le32(b, &n, (uint32_t)align);
        le32(b, &n, (uint32_t)w->p.sampleRate * (uint32_t)align);
        le32(b, &n, 0);
        le32(b, &n, (uint32_t)w->aframes);
        le32(b, &n, w->maxAudio);
        le32(b, &n, 0xFFFFFFFFu);
        le32(b, &n, (uint32_t)align);
        le32(b, &n, 0);
        le32(b, &n, 0);
        fourcc(b, &n, "strf");
        le32(b, &n, 16);
        le16(b, &n, 1); /* WAVE_FORMAT_PCM */
        le16(b, &n, (unsigned)w->p.channels);
        le32(b, &n, (uint32_t)w->p.sampleRate);
        le32(b, &n, (uint32_t)w->p.sampleRate * (uint32_t)align);
        le16(b, &n, (unsigned)align);
        le16(b, &n, 16);
    }

    fourcc(b, &n, "LIST");
    le32(b, &n, moviSize);
    fourcc(b, &n, "movi");
    return n;
}

static void put(AviWriter *w, const void *data, size_t len) {
    if (w->failed) return;
    if (len && fwrite(data, 1, len, w->f) != len) {
        w->failed = 1;
        return;
    }
    w->pos += (long)len;
}

static int addChunk(AviWriter *w, const char *id, const void *data, size_t len, int audio) {
    uint8_t head[8];
    size_t n = 0, padded = len + (len & 1);

    if (w == NULL || w->failed) return -1;
    if (len > 0x7FFFFFFFu || w->pos + 8 + (long)padded + (long)(w->nIdx + 1) * 16 + 4096 > AVI_MAX_BYTES) {
        w->failed = 1;
        return -1;
    }
    if (w->nIdx == w->capIdx) {
        size_t cap = w->capIdx ? w->capIdx * 2 : 4096;
        IdxEntry *p = (IdxEntry *)realloc(w->idx, cap * sizeof *p);
        if (p == NULL) {
            w->failed = 1;
            return -1;
        }
        w->idx = p;
        w->capIdx = cap;
    }
    w->idx[w->nIdx].off = (uint32_t)(w->pos - w->moviStart);
    w->idx[w->nIdx].size = (uint32_t)len;
    w->idx[w->nIdx].audio = (uint8_t)audio;
    w->nIdx++;
    fourcc(head, &n, id);
    le32(head, &n, (uint32_t)len);
    put(w, head, 8);
    put(w, data, len);
    if (len & 1) put(w, "", 1);
    return w->failed ? -1 : 0;
}

AviWriter *aviOpen(const char *path, const AviParams *p) {
    AviWriter *w;
    uint8_t hdr[HDR_MAX];
    size_t n;

    if (path == NULL || p == NULL || p->width < 1 || p->height < 1 || p->fps < 1 || p->fps > 1000 || p->channels < 0 || p->channels > 2 ||
        (p->channels > 0 && p->sampleRate < 1)) {
        return NULL;
    }
    w = (AviWriter *)calloc(1, sizeof *w);
    if (w == NULL) return NULL;
    w->p = *p;
    w->path = (char *)malloc(strlen(path) + 1);
    if (w->path == NULL) {
        free(w);
        return NULL;
    }
    strcpy(w->path, path);
    w->f = fopen(path, "wb");
    if (w->f == NULL) {
        free(w->path);
        free(w);
        return NULL;
    }
    n = buildHeader(w, hdr, 0, 0);
    w->moviStart = (long)n - 4;
    put(w, hdr, n);
    if (w->failed) {
        aviAbort(w);
        return NULL;
    }
    return w;
}

int aviAddVideo(AviWriter *w, const uint8_t *jpeg, size_t len) {
    int r;
    if (w == NULL || jpeg == NULL || len == 0) return -1;
    r = addChunk(w, "00dc", jpeg, len, 0);
    if (r == 0) {
        w->vframes++;
        if ((uint32_t)len > w->maxVideo) w->maxVideo = (uint32_t)len;
    }
    return r;
}

int aviAddAudio(AviWriter *w, const int16_t *pcm, size_t frames) {
    size_t i, n, count;
    uint8_t *tmp = NULL;
    int r;

    if (w == NULL || w->p.channels == 0 || pcm == NULL || frames == 0) return -1;
    count = frames * (size_t)w->p.channels;
    n = count * 2;
    tmp = (uint8_t *)calloc(n, 1);
    if (tmp == NULL) {
        w->failed = 1;
        return -1;
    }
    for (i = 0; i < count; i++) { /* little-endian on disk whatever the host is */
        tmp[i * 2] = (uint8_t)((uint16_t)pcm[i] & 0xFF);
        tmp[i * 2 + 1] = (uint8_t)((uint16_t)pcm[i] >> 8);
    }
    r = addChunk(w, "01wb", tmp, n, 1);
    free(tmp);
    if (r == 0) {
        w->aframes += (long)frames;
        if ((uint32_t)n > w->maxAudio) w->maxAudio = (uint32_t)n;
    }
    return r;
}

static void release(AviWriter *w) {
    if (w->f != NULL) fclose(w->f);
    free(w->idx);
    free(w->path);
    free(w);
}

int aviClose(AviWriter *w) {
    uint8_t hdr[HDR_MAX], head[8];
    size_t i, n = 0;
    int bad;

    if (w == NULL) return -1;
    if (!w->failed) {
        uint8_t *ix = (uint8_t *)malloc(w->nIdx ? w->nIdx * 16 : 1);
        if (ix == NULL) {
            w->failed = 1;
        } else {
            for (i = 0; i < w->nIdx; i++) {
                size_t m = i * 16;
                fourcc(ix, &m, w->idx[i].audio ? "01wb" : "00dc");
                le32(ix, &m, 0x10); /* AVIIF_KEYFRAME */
                le32(ix, &m, w->idx[i].off);
                le32(ix, &m, w->idx[i].size);
            }
            fourcc(head, &n, "idx1");
            le32(head, &n, (uint32_t)(w->nIdx * 16));
            {
                long moviEnd = w->pos;
                put(w, head, 8);
                put(w, ix, w->nIdx * 16);
                free(ix);
                if (!w->failed) {
                    long total = w->pos;
                    long keep = w->pos;
                    w->pos = moviEnd; /* the movi list ends where the index starts */
                    n = buildHeader(w, hdr, total, 1);
                    w->pos = keep;
                    if (fseek(w->f, 0, SEEK_SET) != 0 || fwrite(hdr, 1, n, w->f) != n) w->failed = 1;
                }
            }
        }
    }
    if (!w->failed && fflush(w->f) != 0) w->failed = 1;
    bad = w->failed;
    {
        FILE *f = w->f;
        w->f = NULL;
        if (f != NULL && fclose(f) != 0) bad = 1;
    }
    release(w);
    return bad ? -1 : 0;
}

void aviAbort(AviWriter *w) {
    char *path;
    if (w == NULL) return;
    path = (char *)malloc(strlen(w->path) + 1);
    if (path != NULL) strcpy(path, w->path);
    if (w->f != NULL) {
        fclose(w->f);
        w->f = NULL;
    }
    if (path != NULL) {
        remove(path);
        free(path);
    }
    release(w);
}

long aviVideoFrames(const AviWriter *w) { return w != NULL ? w->vframes : 0; }
long aviAudioFrames(const AviWriter *w) { return w != NULL ? w->aframes : 0; }
long aviBytes(const AviWriter *w) { return w != NULL ? w->pos : 0; }
