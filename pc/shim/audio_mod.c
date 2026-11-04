/* A mod's WAV files in place of the game's samples and songs (content-export E3c/E3d); see audio_mod.h */
#include "audio_mod.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_log.h"
#include "snd_internal.h"
#include "vag_encode.h"
#include "wav_read.h"

#define AM_SLOTS 3
#define AM_REJECTS 64

static AudioModIo amIo;
static int amHaveIo;
static char amBank[AM_SLOTS][24];
static unsigned long amRejected[AM_REJECTS];
static int amRejectedCount;

void audioModSetup(const AudioModIo *io) {
    amHaveIo = io != NULL;
    if (io != NULL) amIo = *io;
    memset(amBank, 0, sizeof amBank);
    amRejectedCount = 0;
}

void audioModBankOpened(int slot) {
    if (slot < 0 || slot >= AM_SLOTS) return;
    amBank[slot][0] = '\0';
    if (amHaveIo && amIo.slotBank != NULL && !amIo.slotBank(amIo.ud, slot, amBank[slot], (int)sizeof amBank[slot])) {
        amBank[slot][0] = '\0';
    }
}

void audioModBankClosed(int slot) {
    if (slot >= 0 && slot < AM_SLOTS) amBank[slot][0] = '\0';
}

const char *audioModBankOf(int slot) {
    return slot >= 0 && slot < AM_SLOTS ? amBank[slot] : "";
}

/* --- the mod files ---------------------------------------------------------- */

static int fileExists(const char *path) {
    FILE *f = fopen(path, "rb");

    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

/* A refused file is logged once per run (a song replayed at every map change would repeat it otherwise) */
static int firstRejection(const char *path) {
    unsigned long h = 5381;
    const char *c;
    int i;

    for (c = path; *c != '\0'; c++) h = h * 33 + (unsigned char)*c;
    for (i = 0; i < amRejectedCount; i++) {
        if (amRejected[i] == h) return 0;
    }
    if (amRejectedCount < AM_REJECTS) amRejected[amRejectedCount++] = h;
    return 1;
}

static void reject(const char *mod, const char *path, const char *file, const char *why) {
    if (firstRejection(path)) plogLine("audio", "%s: %s rejected: %s\n", mod != NULL ? mod : "?", file, why[0] ? why : "unusable file");
}

int audioModVagReplacement(const char *bank, int vag, AudioModVag *out) {
    int m;

    if (!amHaveIo || amIo.modCount == NULL || bank == NULL || bank[0] == '\0' || vag < 1 || vag > 255) return 0;
    for (m = amIo.modCount(amIo.ud) - 1; m >= 0; m--) {
        const char *dir = amIo.modDir(amIo.ud, m), *mod = amIo.modName(amIo.ud, m);
        char path[1024], file[64], err[160];
        WavInfo info;
        int16_t *pcm, *mono;
        size_t frames, size = 0;
        long loopStart = -1;
        uint8_t *adpcm;

        if (dir == NULL) continue;
        snprintf(file, sizeof file, "%s/vag_%03d.wav", bank, vag);
        snprintf(path, sizeof path, "%s/audio/%s", dir, file);
        if (!fileExists(path)) continue;
        err[0] = '\0';
        pcm = wavLoad(path, &info, err, sizeof err);
        if (pcm == NULL) {
            reject(mod, path, file, err);
            continue;
        }
        mono = wavResampleMono(pcm, info.frames, info.channels, info.rate, 44100, &frames);
        free(pcm);
        if (mono == NULL) {
            reject(mod, path, file, "out of memory");
            continue;
        }
        if (info.hasLoop) {
            /* the loop's frames at 44100 Hz; the sound after the loop end is dropped */
            double k = 44100.0 / (double)info.rate;
            size_t ls = (size_t)floor((double)info.loopStart * k + 0.5);
            size_t le = (size_t)floor((double)(info.loopEnd + 1) * k + 0.5);

            if (le > frames) le = frames;
            if (le > ls) {
                loopStart = (long)ls;
                frames = le;
            }
        }
        adpcm = vagEncode(mono, frames, loopStart, &size);
        free(mono);
        if (adpcm == NULL) {
            reject(mod, path, file, "could not be encoded");
            continue;
        }
        out->adpcm = adpcm;
        out->size = (unsigned long)size;
        out->loops = loopStart >= 0;
        out->frames = (unsigned long)frames;
        PLOG("audio", PLOG_MODS, "%s: %s replaces the sample (%d Hz %s, %lu frames at 44100 Hz, %lu ADPCM bytes, %s)\n",
             mod != NULL ? mod : "?", file, info.rate, info.channels == 2 ? "stereo" : "mono", out->frames, out->size,
             out->loops ? "loops" : "plays once");
        return 1;
    }
    return 0;
}

/* =============================================================================== U7: replaced samples as PCM voices */

/* A replaced sample does not go into PsyCross's SPU RAM (a fixed 2 MB array, with a decode buffer of 512K samples behind it: both
   compile-time constants of a library the port never patches). It is decoded once, here, with the decoder the SPU path uses, and
   kept as PCM by the output (an OpenAL buffer by default); a voice that keys it on plays that buffer with the same pitch, gain and
   pan arithmetic PsyX_SPUAL applies, so the only limit left is memory. */

#define AM_PCM_VOICES 32
#define AM_PCM_STEREO_FACTOR 3.0f

static AudioModPcmBackend amPcm;
static void *amPcmCtx;
static int amPcmCustom;
static unsigned char amPcmActive[AM_PCM_VOICES];

typedef struct AmPcmAl {
    ALuint src;
    ALuint buf; /* the AL buffer it plays, 0 when none */
} AmPcmAl;
static AmPcmAl amPcmAl[AM_PCM_VOICES];

static int alPcmReady(void) {
    return alcGetCurrentContext() != NULL;
}

static int alPcmUpload(void *ctx, const short *pcm, unsigned long frames, long loopStart) {
    ALuint buf = 0;

    (void)ctx;
    if (!alPcmReady() || frames == 0 || frames > 0x7FFFFFFFUL / sizeof(short)) return 0;
    while (alGetError() != AL_NO_ERROR) {
    }
    alGenBuffers(1, &buf);
    if (alGetError() != AL_NO_ERROR || buf == 0) return 0;
    alBufferData(buf, AL_FORMAT_MONO16, pcm, (ALsizei)(frames * sizeof(short)), 44100);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &buf);
        return 0;
    }
    if (loopStart >= 0 && (unsigned long)loopStart < frames) {
        ALint pts[2];

        pts[0] = (ALint)loopStart;
        pts[1] = (ALint)frames;
        alBufferiv(buf, AL_LOOP_POINTS_SOFT, pts);
    }
    return (int)buf;
}

static void alPcmRelease(void *ctx, int id) {
    int i;
    ALuint b = (ALuint)id;

    (void)ctx;
    for (i = 0; i < AM_PCM_VOICES; i++) {
        if (amPcmAl[i].src != 0 && amPcmAl[i].buf == b) {
            alSourceStop(amPcmAl[i].src);
            alSourcei(amPcmAl[i].src, AL_BUFFER, 0);
            amPcmAl[i].buf = 0;
        }
    }
    alDeleteBuffers(1, &b);
}

static void alPcmGain(ALuint src, short l, short r) {
    float lg = (float)l / 16384.0f, rg = (float)r / 16384.0f, pan;

    if (lg > 1.0f) lg = 1.0f;
    if (rg > 1.0f) rg = 1.0f;
    if (lg < 0.0f) lg = 0.0f;
    if (rg < 0.0f) rg = 0.0f;
    pan = (acosf(lg) + asinf(rg)) / (float)M_PI; /* PsyX_SPUAL: the average angle in [0,1] ... */
    pan = (2.0f * pan - 1.0f) * 0.5f;            /* ... to [-1,1], then a +/- 30 degree arc */
    alSource3f(src, AL_POSITION, pan * AM_PCM_STEREO_FACTOR, 0, -sqrtf(1.0f - pan * pan));
    alSourcef(src, AL_GAIN, (lg + rg) * 0.5f);
}

static void alPcmPlay(void *ctx, int voice, int id, int loops, unsigned short pitch, short l, short r) {
    AmPcmAl *v;

    (void)ctx;
    if (voice < 0 || voice >= AM_PCM_VOICES || !alPcmReady()) return;
    v = &amPcmAl[voice];
    if (v->src == 0) {
        while (alGetError() != AL_NO_ERROR) {
        }
        alGenSources(1, &v->src);
        if (alGetError() != AL_NO_ERROR || v->src == 0) {
            v->src = 0;
            return;
        }
        alSourcei(v->src, AL_SOURCE_RESAMPLER_SOFT, 2); /* the cubic resampler, as PsyX_SPUAL's voices */
        alSourcei(v->src, AL_SOURCE_RELATIVE, AL_TRUE);
    }
    alSourceStop(v->src);
    alSourcei(v->src, AL_BUFFER, 0);
    alSourcei(v->src, AL_BUFFER, (ALint)id);
    v->buf = (ALuint)id;
    alSourcei(v->src, AL_LOOPING, loops ? AL_TRUE : AL_FALSE);
    alPcmGain(v->src, l, r);
    alSourcef(v->src, AL_PITCH, (float)pitch / 4096.0f);
    alSourcePlay(v->src);
}

static void alPcmVolume(void *ctx, int voice, short l, short r) {
    (void)ctx;
    if (voice >= 0 && voice < AM_PCM_VOICES && amPcmAl[voice].src != 0) alPcmGain(amPcmAl[voice].src, l, r);
}

static void alPcmPitch(void *ctx, int voice, unsigned short pitch) {
    (void)ctx;
    if (voice >= 0 && voice < AM_PCM_VOICES && amPcmAl[voice].src != 0) alSourcef(amPcmAl[voice].src, AL_PITCH, (float)pitch / 4096.0f);
}

static void alPcmStop(void *ctx, int voice) {
    (void)ctx;
    if (voice >= 0 && voice < AM_PCM_VOICES && amPcmAl[voice].src != 0) alSourceStop(amPcmAl[voice].src);
}

static const AudioModPcmBackend alPcmBackend = {alPcmUpload, alPcmRelease, alPcmPlay, alPcmVolume, alPcmPitch, alPcmStop};

static const AudioModPcmBackend *pcmBackend(void) {
    return amPcmCustom ? &amPcm : &alPcmBackend;
}

void audioModSetPcmBackend(const AudioModPcmBackend *backend, void *ctx) {
    amPcmCustom = backend != NULL;
    if (backend != NULL) amPcm = *backend;
    amPcmCtx = ctx;
    memset(amPcmActive, 0, sizeof amPcmActive);
}

int audioModPcmBuffer(const unsigned char *adpcm, unsigned long size, int loops) {
    int16_t *pcm;
    size_t frames, i;
    long loopStart = -1;
    int id;

    if (adpcm == NULL || size < VAG_BLOCK_BYTES) return 0;
    pcm = (int16_t *)malloc((size_t)(size / VAG_BLOCK_BYTES) * VAG_BLOCK_SAMPLES * sizeof(int16_t));
    if (pcm == NULL) return 0;
    frames = vagDecodeBlocks(adpcm, (size_t)size, pcm);
    if (loops) {
        /* the SPU returns to the block flagged "loop start" (bit 2), before the block that ends the sample */
        for (i = 0; i + VAG_BLOCK_BYTES <= (size_t)size; i += VAG_BLOCK_BYTES) {
            if (adpcm[i + 1] & 4) {
                loopStart = (long)(i / VAG_BLOCK_BYTES * VAG_BLOCK_SAMPLES);
                break;
            }
            if (adpcm[i + 1] & 1) break;
        }
        if (loopStart < 0) loopStart = 0;
    }
    id = pcmBackend()->upload(amPcmCtx, pcm, (unsigned long)frames, loopStart);
    free(pcm);
    return id > 0 ? id : 0;
}

void audioModPcmRelease(int buf) {
    if (buf > 0) pcmBackend()->release(amPcmCtx, buf);
}

void audioModPcmPlay(int voice, int buf, int loops, unsigned short pitch, short volL, short volR) {
    if (voice < 0 || voice >= AM_PCM_VOICES || buf <= 0) return;
    pcmBackend()->play(amPcmCtx, voice, buf, loops, pitch, volL, volR);
    amPcmActive[voice] = 1;
}

int audioModPcmActive(int voice) {
    return voice >= 0 && voice < AM_PCM_VOICES && amPcmActive[voice];
}

void audioModPcmVolume(int voice, short volL, short volR) {
    if (audioModPcmActive(voice)) pcmBackend()->volume(amPcmCtx, voice, volL, volR);
}

void audioModPcmPitch(int voice, unsigned short pitch) {
    if (audioModPcmActive(voice)) pcmBackend()->pitch(amPcmCtx, voice, pitch);
}

int audioModPcmStop(int voice) {
    if (!audioModPcmActive(voice)) return 0;
    pcmBackend()->stop(amPcmCtx, voice);
    amPcmActive[voice] = 0;
    return 1;
}

void audioModPcmStopAll(void) {
    int v;

    for (v = 0; v < AM_PCM_VOICES; v++) audioModPcmStop(v);
}

/* =============================================================================== E3d: the songs */

#define AM_TRACKS 4
#define AM_AL_BUFS 8

typedef struct AmReader {
    FILE *f;            /* streamed: the open file; NULL when the samples are in mem */
    WavInfo info;
    int16_t *mem;       /* the whole file, interleaved */
    unsigned long pos;  /* the next frame to deliver */
    int loop;
    unsigned long loopStart, loopEnd; /* the last frame of the loop */
} AmReader;

typedef struct AmTrack {
    int used;
    int serial;
    int id;             /* the sink's stream */
    int music, permille, paused, eof;
    AmReader rd;
    int16_t *buf;       /* one chunk */
} AmTrack;

static AmTrack amTracks[AM_TRACKS];
static int amSerial;
static pthread_mutex_t amLock = PTHREAD_MUTEX_INITIALIZER;
static const AudioModSink *amSink;
static void *amSinkCtx;
static unsigned long amMemLimit;

/* --- the OpenAL sink: one source per stream, a ring of buffers --- */

typedef struct AmAl {
    int used;
    ALuint src, bufs[AM_AL_BUFS];
    int frames[AM_AL_BUFS], busy[AM_AL_BUFS];
    int channels, rate;
    int paused;
} AmAl;

static AmAl amAl[AM_TRACKS];

static void alRecycle(AmAl *a) {
    ALint done = 0;
    int i;

    alGetSourcei(a->src, AL_BUFFERS_PROCESSED, &done);
    while (done-- > 0) {
        ALuint b = 0;

        alSourceUnqueueBuffers(a->src, 1, &b);
        for (i = 0; i < AM_AL_BUFS; i++) {
            if (a->bufs[i] == b) {
                a->busy[i] = 0;
                a->frames[i] = 0;
            }
        }
    }
}

static int alOpenStream(void *ctx, int channels, int rate) {
    int i;
    AmAl *a = NULL;

    (void)ctx;
    if (getenv("DW3_SND_MUTE") != NULL) return -1; /* like a host with no audio device: the game's own sequence plays, silent */
    sndInit();
    for (i = 0; i < AM_TRACKS; i++) {
        if (!amAl[i].used) {
            a = &amAl[i];
            break;
        }
    }
    if (a == NULL) return -1;
    while (alGetError() != AL_NO_ERROR) {
    }
    alGenSources(1, &a->src);
    if (alGetError() != AL_NO_ERROR || a->src == 0) return -1;
    alGenBuffers(AM_AL_BUFS, a->bufs);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteSources(1, &a->src);
        return -1;
    }
    memset(a->frames, 0, sizeof a->frames);
    memset(a->busy, 0, sizeof a->busy);
    a->channels = channels;
    a->rate = rate;
    a->paused = 0;
    a->used = 1;
    alSourcei(a->src, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcei(a->src, AL_LOOPING, AL_FALSE);
    alSourcef(a->src, AL_GAIN, 0.0f);
    return (int)(a - amAl);
}

static int alRoom(void *ctx, int id) {
    AmAl *a = &amAl[id];
    int i, n = 0;

    (void)ctx;
    alRecycle(a);
    for (i = 0; i < AM_AL_BUFS; i++) n += !a->busy[i];
    return n * AUDIO_MOD_CHUNK;
}

static void alQueue(void *ctx, int id, const short *pcm, int frames) {
    AmAl *a = &amAl[id];
    int i;
    ALint state;

    (void)ctx;
    for (i = 0; i < AM_AL_BUFS && a->busy[i]; i++) {
    }
    if (i == AM_AL_BUFS) return;
    alBufferData(a->bufs[i], a->channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, pcm,
                 (ALsizei)((size_t)frames * (size_t)a->channels * sizeof(short)), a->rate);
    alSourceQueueBuffers(a->src, 1, &a->bufs[i]);
    a->busy[i] = 1;
    a->frames[i] = frames;
    alGetSourcei(a->src, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING && !a->paused) alSourcePlay(a->src); /* the first chunk, or an underrun */
}

static int alQueued(void *ctx, int id) {
    AmAl *a = &amAl[id];
    int i, n = 0;

    (void)ctx;
    alRecycle(a);
    for (i = 0; i < AM_AL_BUFS; i++) n += a->busy[i] ? a->frames[i] : 0;
    return n;
}

static void alGain(void *ctx, int id, int permille) {
    (void)ctx;
    alSourcef(amAl[id].src, AL_GAIN, (float)permille / 1000.0f);
}

static void alPause(void *ctx, int id, int on) {
    AmAl *a = &amAl[id];
    ALint state;

    (void)ctx;
    a->paused = on;
    if (on) {
        alSourcePause(a->src);
    } else {
        alGetSourcei(a->src, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && alQueued(ctx, id) > 0) alSourcePlay(a->src);
    }
}

static void alCloseStream(void *ctx, int id) {
    AmAl *a = &amAl[id];
    ALint queued = 0;

    (void)ctx;
    alSourceStop(a->src);
    alGetSourcei(a->src, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        ALuint b = 0;

        alSourceUnqueueBuffers(a->src, 1, &b);
    }
    alDeleteSources(1, &a->src);
    alDeleteBuffers(AM_AL_BUFS, a->bufs);
    a->used = 0;
}

static const AudioModSink amAlSink = {alOpenStream, alRoom, alQueue, alQueued, alGain, alPause, alCloseStream};

void audioModSetSink(const AudioModSink *sink, void *ctx) {
    amSink = sink;
    amSinkCtx = ctx;
}

void audioModSetMemLimit(unsigned long bytes) {
    amMemLimit = bytes;
}

static const AudioModSink *sinkNow(void) {
    return amSink != NULL ? amSink : &amAlSink;
}

/* --- the reader --- */

static void rdClose(AmReader *rd) {
    if (rd->f != NULL) fclose(rd->f);
    free(rd->mem);
    memset(rd, 0, sizeof *rd);
}

/* Up to n frames from the reader's position, wrapping at the loop's end; fewer only at the end of a file that does not loop */
static unsigned long rdRead(AmReader *rd, int16_t *out, unsigned long n) {
    unsigned long got = 0;
    int ch = rd->info.channels;

    while (got < n) {
        unsigned long limit = rd->loop ? rd->loopEnd + 1 : rd->info.frames, take;

        if (rd->pos >= limit) {
            if (!rd->loop) break;
            rd->pos = rd->loopStart;
        }
        take = n - got < limit - rd->pos ? n - got : limit - rd->pos;
        if (rd->mem != NULL) {
            memcpy(out + got * (unsigned long)ch, rd->mem + rd->pos * (unsigned long)ch, (size_t)take * (size_t)ch * sizeof(int16_t));
        } else if (wavReadFrames(rd->f, &rd->info, rd->pos, out + got * (unsigned long)ch, take) != take) {
            rd->info.frames = rd->pos; /* the file got cut under us: this is its end */
            rd->loop = 0;
            break;
        }
        rd->pos += take;
        got += take;
        if (rd->loop && rd->pos > rd->loopEnd) rd->pos = rd->loopStart;
    }
    return got;
}

/* --- the sidecar --- */

/* {"loopStart": n, "loopEnd": n}: strict JSON for those two numbers (other keys with a number, string or boolean are skipped). 0 and *err empty
   when it parses (*has says which of the two keys it holds), else -1 with the reason */
static int sidecarParse(const char *t, size_t len, double *start, double *end, int *hasStart, int *hasEnd, char *err, size_t cap) {
    size_t i = 0;

#define SKIP while (i < len && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) i++
    *hasStart = *hasEnd = 0;
    SKIP;
    if (i >= len || t[i] != '{') {
        snprintf(err, cap, "not a JSON object");
        return -1;
    }
    i++;
    SKIP;
    if (i < len && t[i] == '}') {
        i++;
    } else {
        for (;;) {
            char key[48];
            size_t k = 0;
            char *endp;
            double v;

            SKIP;
            if (i >= len || t[i] != '"') {
                snprintf(err, cap, "expected a key");
                return -1;
            }
            i++;
            while (i < len && t[i] != '"' && k + 1 < sizeof key) key[k++] = t[i++];
            key[k] = '\0';
            if (i >= len || t[i] != '"') {
                snprintf(err, cap, "bad key");
                return -1;
            }
            i++;
            SKIP;
            if (i >= len || t[i] != ':') {
                snprintf(err, cap, "expected ':'");
                return -1;
            }
            i++;
            SKIP;
            if (i < len && t[i] == '"') {
                i++;
                while (i < len && t[i] != '"') i++;
                if (i >= len) {
                    snprintf(err, cap, "unterminated string");
                    return -1;
                }
                i++;
            } else if (len - i >= 4 && (memcmp(t + i, "true", 4) == 0 || memcmp(t + i, "null", 4) == 0)) {
                i += 4;
            } else if (len - i >= 5 && memcmp(t + i, "false", 5) == 0) {
                i += 5;
            } else {
                char tmp[40];
                size_t m = 0;

                while (i + m < len && m + 1 < sizeof tmp && (strchr("+-.eE0123456789", t[i + m]) != NULL)) {
                    tmp[m] = t[i + m];
                    m++;
                }
                tmp[m] = '\0';
                v = strtod(tmp, &endp);
                if (m == 0 || *endp != '\0') {
                    snprintf(err, cap, "bad value of \"%s\"", key);
                    return -1;
                }
                i += m;
                if (strcmp(key, "loopStart") == 0) {
                    *start = v;
                    *hasStart = 1;
                } else if (strcmp(key, "loopEnd") == 0) {
                    *end = v;
                    *hasEnd = 1;
                }
            }
            SKIP;
            if (i < len && t[i] == ',') {
                i++;
                continue;
            }
            if (i < len && t[i] == '}') {
                i++;
                break;
            }
            snprintf(err, cap, "expected ',' or '}'");
            return -1;
        }
    }
    SKIP;
    if (i != len) {
        snprintf(err, cap, "text after the object");
        return -1;
    }
    return 0;
#undef SKIP
}

/* The loop a sidecar names, applied to rd: 1 when it did. A broken sidecar is logged (once) and ignored. */
static int sidecarLoop(AmReader *rd, const char *mod, const char *jsonPath, const char *jsonFile) {
    FILE *f = fopen(jsonPath, "rb");
    char text[512], err[96];
    size_t n;
    double s0 = 0, e0 = 0;
    int hs, he;

    if (f == NULL) return 0;
    n = fread(text, 1, sizeof text, f);
    fclose(f);
    err[0] = '\0';
    if (n == sizeof text) {
        snprintf(err, sizeof err, "too long");
    } else if (sidecarParse(text, n, &s0, &e0, &hs, &he, err, sizeof err) == 0) {
        if (!hs || !he) {
            snprintf(err, sizeof err, "needs \"loopStart\" and \"loopEnd\"");
        } else if (s0 < 0 || e0 < s0 || s0 != floor(s0) || e0 != floor(e0) || s0 >= (double)rd->info.frames) {
            snprintf(err, sizeof err, "loop %.0f..%.0f is not inside the %lu frames of the WAV", s0, e0, rd->info.frames);
        } else {
            rd->loop = 1;
            rd->loopStart = (unsigned long)s0;
            rd->loopEnd = e0 >= (double)rd->info.frames ? rd->info.frames - 1 : (unsigned long)e0;
            return 1;
        }
    }
    reject(mod, jsonPath, jsonFile, err);
    return 0;
}

/* --- the tracks --- */

static AmTrack *trackOf(int handle) {
    int i;

    if (handle <= 0) return NULL;
    for (i = 0; i < AM_TRACKS; i++) {
        if (amTracks[i].used && amTracks[i].serial == handle) return &amTracks[i];
    }
    return NULL;
}

static void trackFree(AmTrack *t) {
    if (t->id >= 0) sinkNow()->close(amSinkCtx, t->id);
    rdClose(&t->rd);
    free(t->buf);
    memset(t, 0, sizeof *t);
}

static int gainOf(const AmTrack *t) {
    return (int)((long)t->permille * sndUserGainPermille(t->music ? SND_VOL_MUSIC : SND_VOL_EFFECTS) / 1000);
}

/* Fills the output while it has room; an end of file waits for the queued audio to play out */
static void trackPump(AmTrack *t) {
    const AudioModSink *sk = sinkNow();

    if (t->paused) return;
    while (!t->eof && sk->room(amSinkCtx, t->id) >= AUDIO_MOD_CHUNK) {
        unsigned long n = rdRead(&t->rd, t->buf, AUDIO_MOD_CHUNK);

        if (n > 0) sk->queue(amSinkCtx, t->id, t->buf, (int)n);
        if (n < AUDIO_MOD_CHUNK) t->eof = 1;
    }
    sk->setGain(amSinkCtx, t->id, gainOf(t));
    if (t->eof && sk->queued(amSinkCtx, t->id) <= 0) {
        PLOG("audio", PLOG_TRACE, "track %d played to its end\n", t->serial);
        trackFree(t);
    }
}

static int trackStartLocked(const char *bank, int pkg, int rec, int loopForever, int music, int permille) {
    int m;

    if (!amHaveIo || amIo.modCount == NULL || bank == NULL || bank[0] == '\0' || pkg < 0 || rec < 0) return 0;
    for (m = amIo.modCount(amIo.ud) - 1; m >= 0; m--) {
        const char *dir = amIo.modDir(amIo.ud, m), *mod = amIo.modName(amIo.ud, m);
        char path[1024], file[96], jpath[1024], jfile[96], err[200];
        const char *shown = NULL;
        AmReader rd;
        AmTrack *t = NULL;
        FILE *f;
        int k, i, loopSrc = 0, ch, h;
        unsigned long bytes;
        const AudioModSink *sk;

        if (dir == NULL) continue;
        for (k = 0; k < 2 && shown == NULL; k++) {
            if (k == 0) snprintf(file, sizeof file, "%s/seq_%d_%02d.wav", bank, pkg, rec);
            else snprintf(file, sizeof file, "%s/seq_%d_%d.wav", bank, pkg, rec);
            snprintf(path, sizeof path, "%s/audio/%s", dir, file);
            if (fileExists(path)) shown = file;
        }
        if (shown == NULL) continue;
        f = fopen(path, "rb");
        memset(&rd, 0, sizeof rd);
        err[0] = '\0';
        if (f == NULL || wavInfoRead(f, &rd.info, err, sizeof err) != 0) {
            if (f != NULL) fclose(f);
            reject(mod, path, file, err[0] ? err : "cannot open the file");
            continue;
        }
        ch = rd.info.channels;
        bytes = rd.info.frames * (unsigned long)ch * 2UL;
        rd.f = f;
        if (bytes <= (amMemLimit != 0 ? amMemLimit : AUDIO_MOD_MEM_BYTES)) {
            rd.mem = (int16_t *)malloc((size_t)bytes);
            if (rd.mem == NULL || wavReadFrames(f, &rd.info, 0, rd.mem, rd.info.frames) != rd.info.frames) {
                rdClose(&rd);
                reject(mod, path, file, "the data chunk is cut or does not fit in memory");
                continue;
            }
            fclose(f);
            rd.f = NULL;
        }
        if (rd.info.hasLoop) {
            rd.loop = 1;
            rd.loopStart = rd.info.loopStart;
            rd.loopEnd = rd.info.loopEnd;
            loopSrc = 1;
        } else {
            snprintf(jfile, sizeof jfile, "%.*s.json", (int)strlen(file) - 4, file);
            snprintf(jpath, sizeof jpath, "%s/audio/%s", dir, jfile);
            if (sidecarLoop(&rd, mod, jpath, jfile)) loopSrc = 2;
        }
        if (!rd.loop && loopForever) {
            rd.loop = 1;
            rd.loopStart = 0;
            rd.loopEnd = rd.info.frames - 1;
            loopSrc = 3;
        }
        for (i = 0; i < AM_TRACKS; i++) {
            if (!amTracks[i].used) {
                t = &amTracks[i];
                break;
            }
        }
        sk = sinkNow();
        if (t == NULL) {
            rdClose(&rd);
            PLOG("audio", PLOG_MODS, "%s: no free song track, the game's sequence plays\n", bank);
            return 0;
        }
        memset(t, 0, sizeof *t);
        t->id = sk->open(amSinkCtx, ch, rd.info.rate);
        t->buf = (int16_t *)malloc((size_t)AUDIO_MOD_CHUNK * (size_t)ch * sizeof(int16_t));
        if (t->id < 0 || t->buf == NULL) {
            if (t->id >= 0) sk->close(amSinkCtx, t->id);
            free(t->buf);
            rdClose(&rd);
            memset(t, 0, sizeof *t);
            PLOG("audio", PLOG_MODS, "%s: %s has no audio output, the game's sequence plays\n", mod != NULL ? mod : "?", file);
            return 0; /* no output: the game's own sequence plays */
        }
        t->used = 1;
        t->serial = ++amSerial;
        t->music = music;
        t->permille = permille < 0 ? 0 : (permille > 1000 ? 1000 : permille);
        t->rd = rd;
        PLOG("audio", PLOG_MODS, "%s: %s replaces the sequence (%d Hz %s, %lu frames, %s, %s)\n", mod != NULL ? mod : "?", file,
             rd.info.rate, ch == 2 ? "stereo" : "mono", rd.info.frames, rd.f != NULL ? "streamed" : "in memory",
             loopSrc == 0 ? "plays once"
                          : (loopSrc == 3 ? "loops the whole file"
                                          : (loopSrc == 1 ? "loops from the smpl chunk" : "loops from the json")));
        if (rd.loop) PLOG("audio", PLOG_MODS, "%s: loop frames %lu..%lu\n", file, rd.loopStart, rd.loopEnd);
        h = t->serial;
        trackPump(t);
        return h;
    }
    return 0;
}

int audioModTrackStart(const char *bank, int pkg, int rec, int loopForever, int music, int permille) {
    int h;

    pthread_mutex_lock(&amLock);
    h = trackStartLocked(bank, pkg, rec, loopForever, music, permille);
    pthread_mutex_unlock(&amLock);
    return h;
}

void audioModTrackLevel(int handle, int permille) {
    AmTrack *t;

    pthread_mutex_lock(&amLock);
    t = trackOf(handle);
    if (t != NULL) {
        t->permille = permille < 0 ? 0 : (permille > 1000 ? 1000 : permille);
        sinkNow()->setGain(amSinkCtx, t->id, gainOf(t));
    }
    pthread_mutex_unlock(&amLock);
}

void audioModTrackPause(int handle, int on) {
    AmTrack *t;

    pthread_mutex_lock(&amLock);
    t = trackOf(handle);
    if (t != NULL && t->paused != (on != 0)) {
        t->paused = on != 0;
        sinkNow()->pause(amSinkCtx, t->id, t->paused);
    }
    pthread_mutex_unlock(&amLock);
}

void audioModTrackStop(int handle) {
    AmTrack *t;

    pthread_mutex_lock(&amLock);
    t = trackOf(handle);
    if (t != NULL) {
        PLOG("audio", PLOG_TRACE, "track %d stopped\n", t->serial);
        trackFree(t);
    }
    pthread_mutex_unlock(&amLock);
}

int audioModTrackActive(int handle) {
    int on;

    pthread_mutex_lock(&amLock);
    on = trackOf(handle) != NULL;
    pthread_mutex_unlock(&amLock);
    return on;
}

void audioModPump(void) {
    int i;

    pthread_mutex_lock(&amLock);
    for (i = 0; i < AM_TRACKS; i++) {
        if (amTracks[i].used) trackPump(&amTracks[i]);
    }
    pthread_mutex_unlock(&amLock);
}

/* --- --validate-mod (U5) ----------------------------------------------------- */

static int vOneCount(void *ud) { (void)ud; return 1; }
static const char *vOneName(void *ud, int i) { (void)i; return ((const char *const *)ud)[0]; }
static const char *vOneDir(void *ud, int i) { (void)i; return ((const char *const *)ud)[1]; }
static int vSinkOpen(void *c, int ch, int rate) { (void)c; (void)ch; (void)rate; return 1; }
static int vSinkRoom(void *c, int id) { (void)c; (void)id; return 0; }
static void vSinkQueue(void *c, int id, const short *p, int n) { (void)c; (void)id; (void)p; (void)n; }
static int vSinkQueued(void *c, int id) { (void)c; (void)id; return 0; }
static void vSinkGain(void *c, int id, int pm) { (void)c; (void)id; (void)pm; }
static void vSinkPause(void *c, int id, int on) { (void)c; (void)id; (void)on; }
static void vSinkClose(void *c, int id) { (void)c; (void)id; }

/* Every audio file of one mod through the same loaders the game runs (audioModVagReplacement, audioModTrackStart), the mod alone in
   the list so a good file of a later mod never hides a bad one. A refusal is the usual `[audio] <mod>: <file> rejected: <why>` line.
   Returns how many files were looked at. */
int audioModValidate(const char *modDir, const char *modName) {
    AudioModIo saved = amIo, one;
    int savedHave = amHaveIo, n = 0;
    const char *ud[2];
    char root[1100];
    DIR *d;
    struct dirent *e;
    static const AudioModSink nullSink = {vSinkOpen, vSinkRoom, vSinkQueue, vSinkQueued, vSinkGain, vSinkPause, vSinkClose};

    snprintf(root, sizeof root, "%s/audio", modDir);
    d = opendir(root);
    if (d == NULL) return 0;
    ud[0] = modName;
    ud[1] = modDir;
    memset(&one, 0, sizeof one);
    one.modCount = vOneCount;
    one.modName = vOneName;
    one.modDir = vOneDir;
    one.ud = (void *)ud;
    amIo = one;
    amHaveIo = 1;
    audioModSetSink(&nullSink, NULL);
    while ((e = readdir(d)) != NULL) {
        char bankDir[1400];
        DIR *bd;
        struct dirent *f;
        if (e->d_name[0] == '.' || strlen(e->d_name) >= 24) continue;
        snprintf(bankDir, sizeof bankDir, "%s/%s", root, e->d_name);
        bd = opendir(bankDir);
        if (bd == NULL) continue;
        while ((f = readdir(bd)) != NULL) {
            int a = 0, b = 0, used = 0;
            const char *name = f->d_name;
            size_t len = strlen(name);
            if (name[0] == '.' || len < 5 || strcmp(name + len - 4, ".wav")) {
                if (len > 5 && !strcmp(name + len - 5, ".json")) continue; /* a loop sidecar, read with its song */
                if (name[0] != '.') plogLine("audio", "%s: %s/%s ignored: only vag_NNN.wav and seq_<package>_<record>.wav are read\n", modName, e->d_name, name);
                continue;
            }
            if (sscanf(name, "vag_%d.wav", &a) == 1 && strlen(name) == 11) {
                AudioModVag v;
                n++;
                if (audioModVagReplacement(e->d_name, a, &v)) free(v.adpcm);
                used = 1;
            } else if (sscanf(name, "seq_%d_%d.wav", &a, &b) == 2) {
                int h;
                n++;
                h = audioModTrackStart(e->d_name, a, b, 0, 1, 1000);
                if (h > 0) audioModTrackStop(h);
                used = 1;
            }
            if (!used) plogLine("audio", "%s: %s/%s ignored: only vag_NNN.wav and seq_<package>_<record>.wav are read\n", modName, e->d_name, name);
        }
        closedir(bd);
    }
    closedir(d);
    audioModSetSink(NULL, NULL);
    amIo = saved;
    amHaveIo = savedHave;
    return n;
}
