/* A WAV file from a mod: headers, samples and the mixdown/resampler (content-export E3c/E3d); see wav_read.h */
#include "wav_read.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define WAV_RATE_MIN 1000
#define WAV_RATE_MAX 384000

static void setErr(char *err, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void setErr(char *err, size_t cap, const char *fmt, ...) {
    va_list ap;

    if (err == NULL || cap == 0 || err[0] != '\0') return;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

static unsigned long le32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned le16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

int wavInfoRead(FILE *f, WavInfo *info, char *err, size_t errCap) {
    unsigned char hdr[12], ck[8], fmt[40];
    long fileLen, pos;
    int haveFmt = 0, haveData = 0;
    unsigned tag = 0, bits = 0;
    unsigned long dataBytes = 0;

    if (err != NULL && errCap > 0) err[0] = '\0';
    memset(info, 0, sizeof *info);
    if (fseek(f, 0, SEEK_END) != 0 || (fileLen = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        setErr(err, errCap, "unreadable file");
        return -1;
    }
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        setErr(err, errCap, "not a WAV file (no RIFF WAVE header)");
        return -1;
    }
    pos = 12;
    while (pos + 8 <= fileLen) {
        unsigned long size;
        long body;

        if (fseek(f, pos, SEEK_SET) != 0 || fread(ck, 1, 8, f) != 8) break;
        size = le32(ck + 4);
        body = pos + 8;
        if (memcmp(ck, "fmt ", 4) == 0 && !haveFmt) {
            size_t want = size < sizeof fmt ? size : sizeof fmt;

            if (size < 16 || fread(fmt, 1, want, f) != want) {
                setErr(err, errCap, "damaged fmt chunk");
                return -1;
            }
            tag = le16(fmt);
            info->channels = (int)le16(fmt + 2);
            info->rate = (int)le32(fmt + 4);
            bits = le16(fmt + 14);
            if (tag == 0xFFFE && size >= 40) {
                tag = le16(fmt + 24); /* the first two bytes of the sub-format GUID */
            }
            haveFmt = 1;
        } else if (memcmp(ck, "data", 4) == 0 && !haveData) {
            long avail = fileLen - body;

            if (avail < 0) avail = 0;
            if (size > (unsigned long)avail) size = (unsigned long)avail; /* cut file, or a streamed size of 0xFFFFFFFF */
            dataBytes = size;
            info->dataOfs = body;
            haveData = 1;
        } else if (memcmp(ck, "smpl", 4) == 0 && !info->hasSmpl && size >= 36) {
            unsigned char sm[60];
            size_t want = size < sizeof sm ? size : sizeof sm;

            if (fread(sm, 1, want, f) == want) {
                info->hasSmpl = 1;
                info->rootNote = (int)le32(sm + 12);
                if (le32(sm + 28) >= 1 && want >= 60) {
                    info->hasLoop = 1;
                    info->loopStart = le32(sm + 44);
                    info->loopEnd = le32(sm + 48);
                }
            }
        }
        pos = body + (long)size + (long)(size & 1);
        if (size > 0x7FFFFFFFu || pos < body) break;
    }
    if (!haveFmt) {
        setErr(err, errCap, "no fmt chunk");
        return -1;
    }
    if (tag != 1 || bits != 16) {
        setErr(err, errCap, "not 16-bit PCM (format %u, %u bits)", tag, bits);
        return -1;
    }
    if (info->channels < 1 || info->channels > 2) {
        setErr(err, errCap, info->channels > 2 ? "more than 2 channels (%d)" : "no channels (%d)", info->channels);
        return -1;
    }
    if (info->rate < WAV_RATE_MIN || info->rate > WAV_RATE_MAX) {
        setErr(err, errCap, "sample rate %d Hz out of range (%d..%d)", info->rate, WAV_RATE_MIN, WAV_RATE_MAX);
        return -1;
    }
    if (!haveData) {
        setErr(err, errCap, "no data chunk");
        return -1;
    }
    info->frames = dataBytes / (2u * (unsigned)info->channels);
    if (info->frames == 0) {
        setErr(err, errCap, "no samples");
        return -1;
    }
    if (info->hasLoop && (info->loopStart >= info->frames || info->loopEnd < info->loopStart)) {
        info->hasLoop = 0; /* a loop outside the data is no loop */
    } else if (info->hasLoop && info->loopEnd >= info->frames) {
        info->loopEnd = info->frames - 1;
    }
    return 0;
}

size_t wavReadFrames(FILE *f, const WavInfo *info, unsigned long first, int16_t *out, size_t n) {
    unsigned char buf[4096];
    size_t done = 0, perChunk = sizeof buf / (2u * (size_t)info->channels);

    if (first >= info->frames) return 0;
    if (n > info->frames - first) n = info->frames - first;
    if (n == 0 || fseek(f, info->dataOfs + (long)first * 2L * info->channels, SEEK_SET) != 0) return 0;
    while (done < n) {
        size_t want = n - done < perChunk ? n - done : perChunk, got, i;

        got = fread(buf, 2u * (size_t)info->channels, want, f);
        for (i = 0; i < got * (size_t)info->channels; i++) {
            out[done * (size_t)info->channels + i] = (int16_t)le16(buf + i * 2);
        }
        done += got;
        if (got < want) break;
    }
    return done;
}

int16_t *wavLoad(const char *path, WavInfo *info, char *err, size_t errCap) {
    FILE *f = fopen(path, "rb");
    int16_t *pcm;
    size_t got;

    if (err != NULL && errCap > 0) err[0] = '\0';
    if (f == NULL) {
        setErr(err, errCap, "cannot open the file");
        return NULL;
    }
    if (wavInfoRead(f, info, err, errCap) != 0) {
        fclose(f);
        return NULL;
    }
    if (info->frames > 0x20000000ul / ((unsigned long)info->channels * 2ul)) {
        setErr(err, errCap, "too long (%lu frames)", info->frames);
        fclose(f);
        return NULL;
    }
    pcm = (int16_t *)malloc((size_t)info->frames * (size_t)info->channels * sizeof(int16_t));
    if (pcm == NULL) {
        setErr(err, errCap, "out of memory");
        fclose(f);
        return NULL;
    }
    got = wavReadFrames(f, info, 0, pcm, info->frames);
    fclose(f);
    if (got != info->frames) {
        setErr(err, errCap, "the data chunk is cut");
        free(pcm);
        return NULL;
    }
    return pcm;
}

static double mixAt(const int16_t *pcm, size_t i, int channels) {
    return channels == 2 ? ((double)pcm[i * 2] + (double)pcm[i * 2 + 1]) * 0.5 : (double)pcm[i];
}

static int16_t clip16(double v) {
    long r = lround(v);

    return (int16_t)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
}

int16_t *wavResampleMono(const int16_t *pcm, size_t frames, int channels, int from, int to, size_t *outFrames) {
    size_t n, i;
    int16_t *out;
    double ratio;

    if (pcm == NULL || frames == 0 || channels < 1 || channels > 2 || from <= 0 || to <= 0 || outFrames == NULL) return NULL;
    n = (size_t)(((unsigned long long)frames * (unsigned)to + (unsigned)from / 2) / (unsigned)from);
    if (n == 0) n = 1;
    if (n > 0x40000000u / 2) return NULL;
    out = (int16_t *)malloc(n * sizeof(int16_t));
    if (out == NULL) return NULL;
    ratio = (double)from / (double)to;
    for (i = 0; i < n; i++) {
        if (from == to) {
            out[i] = clip16(mixAt(pcm, i, channels));
        } else if (from < to) {
            double t = (double)i * ratio;
            size_t i0 = (size_t)t, i1 = i0 + 1 < frames ? i0 + 1 : frames - 1;
            double fr = t - (double)i0;

            if (i0 >= frames) i0 = i1 = frames - 1;
            out[i] = clip16(mixAt(pcm, i0, channels) * (1.0 - fr) + mixAt(pcm, i1, channels) * fr);
        } else {
            double lo = (double)i * ratio, hi = lo + ratio, acc = 0, w = 0;
            size_t k = (size_t)lo;

            for (; (double)k < hi && k < frames; k++) {
                double a = (double)k > lo ? (double)k : lo, b = (double)(k + 1) < hi ? (double)(k + 1) : hi;

                if (b > a) {
                    acc += mixAt(pcm, k, channels) * (b - a);
                    w += b - a;
                }
            }
            out[i] = w > 0 ? clip16(acc / w) : out[i > 0 ? i - 1 : 0];
        }
    }
    *outFrames = n;
    return out;
}
