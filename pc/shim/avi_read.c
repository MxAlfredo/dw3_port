/* An AVI 1.0 reader; see avi_read.h for what it accepts (content-export E2b) */
#include "avi_read.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Chunk {
    long off;       /* file offset of the data */
    uint32_t size;
    long firstFrame; /* audio: the sample frame this chunk starts at */
} Chunk;

struct AviReader {
    FILE *f;
    long fileSize;
    AviInfo info;
    Chunk *video;
    long nVideo, capVideo;
    Chunk *audio;
    long nAudio, capAudio;
    long lastAudio; /* the chunk the last audio read ended in: reads are mostly sequential */
};

typedef struct Streams {
    int n;
    int type[16];     /* 'v', 'a' or 0 */
    int videoOk;
    int audioOk;
    char videoCodec[5];
    int audioTag, audioBits;
} Streams;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static int setErr(char *err, size_t cap, const char *msg) {
    if (err != NULL && cap > 0) snprintf(err, cap, "%s", msg);
    return -1;
}

static int addChunk(Chunk **arr, long *n, long *cap, long off, uint32_t size, long firstFrame) {
    if (*n >= *cap) {
        long ncap = *cap ? *cap * 2 : 1024;
        Chunk *p;

        if (ncap > AVI_READ_MAX_FRAMES * 4) return -1;
        p = (Chunk *)realloc(*arr, (size_t)ncap * sizeof(Chunk));
        if (p == NULL) return -1;
        *arr = p;
        *cap = ncap;
    }
    (*arr)[*n].off = off;
    (*arr)[*n].size = size;
    (*arr)[*n].firstFrame = firstFrame;
    (*n)++;
    return 0;
}

/* The stream headers of the 'hdrl' list held in buf: strl lists, each a strh then a strf */
static int parseHdrl(AviReader *r, const uint8_t *buf, size_t len, Streams *st, long *microSec, char *err, size_t errCap) {
    size_t pos = 0;
    int curType = 0;

    while (pos + 8 <= len) {
        uint32_t size = le32(buf + pos + 4);
        const uint8_t *body = buf + pos + 8;
        size_t avail = len - pos - 8;
        size_t take = size < avail ? size : avail;

        if (memcmp(buf + pos, "LIST", 4) == 0 && take >= 4) {
            if (memcmp(body, "strl", 4) == 0) {
                if (st->n < 16) st->n++;
                curType = 0;
            }
            if (memcmp(body, "strl", 4) == 0 || memcmp(body, "hdrl", 4) == 0) { /* look through */
                pos += 12;
                continue;
            }
        } else if (memcmp(buf + pos, "avih", 4) == 0 && take >= 4) {
            *microSec = (long)le32(body);
        } else if (memcmp(buf + pos, "strh", 4) == 0 && take >= 40 && st->n > 0) {
            if (memcmp(body, "vids", 4) == 0) {
                curType = 'v';
                st->type[st->n - 1] = 'v';
                if (!st->videoOk && r->info.rate == 0) {
                    r->info.scale = (long)le32(body + 20);
                    r->info.rate = (long)le32(body + 24);
                }
            } else if (memcmp(body, "auds", 4) == 0) {
                curType = 'a';
                st->type[st->n - 1] = 'a';
            } else {
                curType = 0;
            }
        } else if (memcmp(buf + pos, "strf", 4) == 0 && st->n > 0) {
            if (curType == 'v' && !st->videoOk && take >= 40) {
                r->info.width = (int)(int32_t)le32(body + 4);
                r->info.height = (int)(int32_t)le32(body + 8);
                if (r->info.height < 0) r->info.height = -r->info.height;
                memcpy(st->videoCodec, body + 16, 4);
                st->videoCodec[4] = '\0';
                st->videoOk = st->n; /* 1-based stream number */
            } else if (curType == 'a' && !st->audioOk && take >= 16) {
                st->audioTag = (int)le16(body);
                r->info.channels = (int)le16(body + 2);
                r->info.sampleRate = (int)le32(body + 4);
                st->audioBits = (int)le16(body + 14);
                st->audioOk = st->n;
            }
        }
        pos += 8 + (size_t)size + (size & 1u);
    }
    (void)err;
    (void)errCap;
    return 0;
}

/* Walks the chunks of the movi list between [pos, end) */
static void walkMovi(AviReader *r, long pos, long end, const Streams *st, int depth) {
    uint8_t h[12];

    while (pos + 8 <= end) {
        uint32_t size;
        long data;

        if (fseek(r->f, pos, SEEK_SET) != 0 || fread(h, 1, 8, r->f) != 8) return;
        size = le32(h + 4);
        data = pos + 8;
        if (memcmp(h, "LIST", 4) == 0) {
            if (size < 4 || fread(h + 8, 1, 4, r->f) != 4) return;
            if (depth < 4 && memcmp(h + 8, "rec ", 4) == 0) {
                long inner = data + 4 + (long)size - 4;

                walkMovi(r, data + 4, inner < end ? inner : end, st, depth + 1);
            }
        } else {
            int s0 = h[0], s1 = h[1];
            int a = (s0 >= '0' && s0 <= '9') ? s0 - '0' : (s0 >= 'a' && s0 <= 'f') ? s0 - 'a' + 10 : (s0 >= 'A' && s0 <= 'F') ? s0 - 'A' + 10 : -1;
            int b = (s1 >= '0' && s1 <= '9') ? s1 - '0' : (s1 >= 'a' && s1 <= 'f') ? s1 - 'a' + 10 : (s1 >= 'A' && s1 <= 'F') ? s1 - 'A' + 10 : -1;
            int stream = (a >= 0 && b >= 0) ? a * 16 + b + 1 : -1;

            if (data + (long)size > r->fileSize) size = (uint32_t)(r->fileSize - data > 0 ? r->fileSize - data : 0); /* cut short */
            if (stream > 0 && stream == st->videoOk && h[2] == 'd' && (h[3] == 'c' || h[3] == 'b')) {
                if (r->nVideo < AVI_READ_MAX_FRAMES) addChunk(&r->video, &r->nVideo, &r->capVideo, data, size, 0);
            } else if (stream > 0 && stream == st->audioOk && h[2] == 'w' && h[3] == 'b' && r->info.channels > 0) {
                long fb = (long)r->info.channels * 2;
                long first = r->nAudio ? r->audio[r->nAudio - 1].firstFrame + (long)(r->audio[r->nAudio - 1].size / (uint32_t)fb) : 0;

                addChunk(&r->audio, &r->nAudio, &r->capAudio, data, size, first);
            }
        }
        pos = data + (long)size + (long)(size & 1u);
    }
}

void aviReadClose(AviReader *r) {
    if (r == NULL) return;
    if (r->f != NULL) fclose(r->f);
    free(r->video);
    free(r->audio);
    free(r);
}

AviReader *aviReadOpen(const char *path, AviInfo *info, char *err, size_t errCap) {
    AviReader *r;
    Streams st;
    uint8_t h[12];
    long pos, microSec = 0;
    int haveHdrl = 0, haveMovi = 0;
    long moviEnd = 0, moviStart = 0;

    if (err != NULL && errCap > 0) err[0] = '\0';
    if (path == NULL) {
        setErr(err, errCap, "no path");
        return NULL;
    }
    r = (AviReader *)calloc(1, sizeof *r);
    if (r == NULL) {
        setErr(err, errCap, "out of memory");
        return NULL;
    }
    memset(&st, 0, sizeof st);
    r->f = fopen(path, "rb");
    if (r->f == NULL) {
        setErr(err, errCap, "cannot open the file");
        goto bad;
    }
    if (fseek(r->f, 0, SEEK_END) != 0 || (r->fileSize = ftell(r->f)) < 0) {
        setErr(err, errCap, "cannot read the file");
        goto bad;
    }
    if (r->fileSize >= 0x7FFFFFFFL) {
        setErr(err, errCap, "file larger than 2 GB");
        goto bad;
    }
    if (r->fileSize < 12 || fseek(r->f, 0, SEEK_SET) != 0 || fread(h, 1, 12, r->f) != 12 || memcmp(h, "RIFF", 4) != 0 ||
        memcmp(h + 8, "AVI ", 4) != 0) {
        setErr(err, errCap, "not an AVI file (no RIFF AVI header)");
        goto bad;
    }
    pos = 12;
    while (pos + 12 <= r->fileSize && !(haveHdrl && haveMovi)) {
        uint32_t size;

        if (fseek(r->f, pos, SEEK_SET) != 0 || fread(h, 1, 12, r->f) != 12) break;
        size = le32(h + 4);
        if (memcmp(h, "LIST", 4) == 0 && memcmp(h + 8, "hdrl", 4) == 0 && !haveHdrl) {
            long n = (long)size - 4;
            uint8_t *buf;

            if (n < 0 || n > (1L << 20) || pos + 12 + n > r->fileSize) {
                setErr(err, errCap, "damaged AVI header list");
                goto bad;
            }
            buf = (uint8_t *)malloc((size_t)n + 1);
            if (buf == NULL || fread(buf, 1, (size_t)n, r->f) != (size_t)n) {
                free(buf);
                setErr(err, errCap, "damaged AVI header list");
                goto bad;
            }
            parseHdrl(r, buf, (size_t)n, &st, &microSec, err, errCap);
            free(buf);
            haveHdrl = 1;
        } else if (memcmp(h, "LIST", 4) == 0 && memcmp(h + 8, "movi", 4) == 0 && !haveMovi) {
            haveMovi = 1;
            moviStart = pos + 12;
            moviEnd = pos + 8 + (long)size;
            if (moviEnd > r->fileSize || moviEnd < moviStart) moviEnd = r->fileSize; /* a wrong size, or a file cut short */
        }
        pos += 8 + (long)size + (long)(size & 1u);
    }
    if (!haveHdrl) {
        setErr(err, errCap, "no AVI header list");
        goto bad;
    }
    if (!haveMovi) {
        setErr(err, errCap, "no movie data in the AVI");
        goto bad;
    }
    if (!st.videoOk) {
        setErr(err, errCap, "the AVI has no video stream");
        goto bad;
    }
    if (strcmp(st.videoCodec, "MJPG") && strcmp(st.videoCodec, "mjpg") && strcmp(st.videoCodec, "AVI1") && strcmp(st.videoCodec, "avi1") &&
        strcmp(st.videoCodec, "JPEG") && strcmp(st.videoCodec, "jpeg")) {
        char m[96];

        snprintf(m, sizeof m, "video codec '%.4s' is not MJPEG (encode with -c:v mjpeg)", st.videoCodec);
        setErr(err, errCap, m);
        goto bad;
    }
    if (st.audioOk) {
        if ((st.audioTag != 1 && st.audioTag != 0xFFFE) || st.audioBits != 16) {
            char m[96];

            snprintf(m, sizeof m, "audio is not 16-bit PCM (format %d, %d bits; encode with -c:a pcm_s16le)", st.audioTag, st.audioBits);
            setErr(err, errCap, m);
            goto bad;
        }
        if (r->info.channels < 1 || r->info.channels > 2) {
            setErr(err, errCap, "audio must be mono or stereo");
            goto bad;
        }
        if (r->info.sampleRate < 1000 || r->info.sampleRate > AVI_READ_MAX_RATE) {
            setErr(err, errCap, "audio sample rate out of range");
            goto bad;
        }
        r->info.hasAudio = 1;
    } else {
        r->info.channels = 0;
        r->info.sampleRate = 0;
    }
    if (r->info.rate <= 0 || r->info.scale <= 0) { /* the stream header gave no rate: the main header's frame period */
        if (microSec > 0) {
            r->info.rate = 1000000L;
            r->info.scale = microSec;
        } else {
            setErr(err, errCap, "the AVI has no frame rate");
            goto bad;
        }
    }
    walkMovi(r, moviStart, moviEnd, &st, 0);
    r->info.videoFrames = r->nVideo;
    if (r->nVideo < 1) {
        setErr(err, errCap, "the AVI has no video frames");
        goto bad;
    }
    if (r->info.hasAudio && r->nAudio > 0) {
        const Chunk *c = &r->audio[r->nAudio - 1];

        r->info.audioFrames = c->firstFrame + (long)(c->size / (uint32_t)(r->info.channels * 2));
    }
    if (info != NULL) *info = r->info;
    return r;
bad:
    aviReadClose(r);
    return NULL;
}

const AviInfo *aviReadInfo(const AviReader *r) {
    return r != NULL ? &r->info : NULL;
}

long aviReadVideoSize(const AviReader *r, long index) {
    if (r == NULL || index < 0 || index >= r->nVideo) return -1;
    return (long)r->video[index].size;
}

long aviReadVideo(AviReader *r, long index, uint8_t *buf, size_t cap) {
    const Chunk *c;

    if (r == NULL || index < 0 || index >= r->nVideo) return -1;
    c = &r->video[index];
    if (c->size > cap) return -1;
    if (c->size == 0) return 0;
    if (fseek(r->f, c->off, SEEK_SET) != 0 || fread(buf, 1, c->size, r->f) != c->size) return -1;
    return (long)c->size;
}

long aviReadAudio(AviReader *r, long start, long frames, int16_t *out) {
    long done = 0, lo, hi, ch, fb;

    if (r == NULL || !r->info.hasAudio || r->nAudio == 0 || start < 0 || frames <= 0 || out == NULL) return 0;
    ch = r->info.channels;
    fb = ch * 2;
    /* the chunk that holds `start`: the last read's chunk or the next, else a binary search */
    lo = 0;
    hi = r->nAudio - 1;
    if (r->lastAudio >= 0 && r->lastAudio < r->nAudio && r->audio[r->lastAudio].firstFrame <= start) lo = r->lastAudio;
    while (lo < hi) {
        long mid = (lo + hi + 1) / 2;

        if (r->audio[mid].firstFrame <= start) lo = mid;
        else hi = mid - 1;
    }
    while (done < frames && lo < r->nAudio) {
        const Chunk *c = &r->audio[lo];
        long chunkFrames = (long)(c->size / (uint32_t)fb);
        long from = start + done - c->firstFrame; /* inside the chunk */
        long n;
        uint8_t *tmp;
        long i;

        if (from < 0) break;
        if (from >= chunkFrames) {
            lo++;
            continue;
        }
        n = chunkFrames - from;
        if (n > frames - done) n = frames - done;
        tmp = (uint8_t *)out + (size_t)done * (size_t)fb; /* read in place, then fix the byte order */
        if (fseek(r->f, c->off + from * fb, SEEK_SET) != 0 || fread(tmp, 1, (size_t)(n * fb), r->f) != (size_t)(n * fb)) break;
        for (i = 0; i < n * ch; i++) {
            const uint8_t *p = tmp + i * 2;
            int16_t v = (int16_t)(uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));

            out[done * ch + i] = v;
        }
        done += n;
        r->lastAudio = lo;
    }
    return done;
}
