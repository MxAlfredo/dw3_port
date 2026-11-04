/*
 * The port's own CD-XA (streamed audio) decoder and stream driver (T36). See
 * shim/host_xa.h for the frame layout and the injectable source/sink design.
 *
 * Why this exists (ADR-002): PsyCross decodes VAG that already sits in SPU
 * RAM and has no XA support at all (its README lists XA as TODO) and no libsnd
 * serial API; PsyX_SPUAL's voices play only resident VAG. A movie's audio is a
 * 37800 Hz XA stream the drive would decode and send straight to the mixer,
 * never through SPU RAM -- so it is this port's own code, and it plays through
 * its own OpenAL source (not the 24-voice SPU pool, which must not be stolen
 * from the SEP/music side).
 *
 * The three halves:
 *   - sndXaClassify / sndXaDecodeAudio: pure functions on one raw 2352-byte
 *     frame. The decode follows the CD-XA spec: each 128-byte group is a
 *     16-byte header plus 28 four-byte words, four 28-sample blocks, and each
 *     data byte carries block b's left (low nibble) and right (high nibble).
 *     The predictor history is per channel and persists across blocks, groups
 *     and sectors for the whole stream. pc/sndtest.c pins the exact layout,
 *     order and history continuity.
 *   - the stream driver: reads sectors from the injected source, skips
 *     everything that is not audio of the selected (file, channel), and
 *     hands the sink one PAL frame of audio per sndXaTick. One sector is one
 *     ring item (2016 stereo frames or 4032 mono frames). This is what
 *     pc/sndtest.c drives with a fake source and a recording sink (no
 *     OpenAL).
 *   - the OpenAL streamer: a rotating pool of AL buffers on one AL source,
 *     installed by host_snd.c (sndXaUseOpenAl) at sound init.
 *
 * T36 simplifications, on purpose, so the milestone lands instead of stalling:
 *   - audio only: no MDEC, no video decode, no StGetNext frames. The game's
 *     movie task runs exactly as before and never sees a picture; the XA
 *     stream is a side effect of the CdControl/CdRead2 surface it already
 *     drives (host_cd.c).
 *   - 37800 Hz assumed for every stream (the discs' movie audio is 37.8 kHz,
 *     4-bit; the coding-info rate/bits fields are parsed but not re-routed).
 *   - mono/stereo is taken at face value from the first sector's coding info;
 *     a mid-stream change is ignored (the real streams never change).
 *   - the file/channel the game implies is unknown (the movie path never calls
 *     CdlSetfilter), so the first audio sector's (file, channel) is locked in
 *     and logged under SND_DEBUG.
 *   - the stream's end is the source running out or SND_XA_SKIP_MAX
 *     consecutive non-matching sectors (the discs carry 7 video sectors
 *     between audio ones; the game itself never stops the CD on the host
 *     because it never sees a video frame to find the last one).
 *   - no SPU/CD volume or SsSetSerialVol routing: the stream plays at the
 *     AL gain below, which is only the player's master and movie sliders (O4,
 *     snd_vol.c).
 *   - a tick is one PAL frame (756 frames of 37.8 kHz audio), driven from
 *     PsyX's vsync thread by host_snd.c's hook, like T35's SEP tick. T40 makes
 *     it demand-driven: the sink reports how much room it has and the tick
 *     decodes only while there is room (bounded per call), so the queue stays
 *     full even when the game's main loop is blocked by the video pacer. The
 *     same tick also counts the playhead the pacer waits on (sndXaPosition).
 *   - an optional SND_XA_DUMP=<path> writes the delivered PCM as a canonical
 *     16-bit WAV (stereo when the stream is stereo) for objective checks. It
 *     is inert by default and the path is the caller's (build/ during
 *     verification); no decoded audio is ever committed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <time.h>

#include <AL/al.h>

#include "host_xa.h"
#include "snd_internal.h"

#define SND_XA_RATE 37800

/* The coefficients are 6-bit fixed point: pos/64 and neg/64. Filter 0..3. */
static const int xaPos[4] = {0, 60, 115, 98};
static const int xaNeg[4] = {0, 0, -52, -55};

static int xaDebugSet;

/* The injected source and sink (host_cd.c sets the source, host_snd.c or
   pc/sndtest.c sets the sink). */
static const SndXaSink *xaSink;
static void *xaSinkCtx;
static SndXaReadFn xaRead;
static void *xaReadCtx;

#define SND_LOG(...)                                                           \
    do {                                                                       \
        if (xaDebugSet) {                                                      \
            fputs("snd: ", stderr);                                            \
            fprintf(stderr, __VA_ARGS__);                                      \
        }                                                                      \
    } while (0)

/* --- the optional WAV dump (SND_XA_DUMP) --------------------------------- */

/* A canonical 44-byte 16-bit PCM WAV for the delivered samples. Off unless
   SND_XA_DUMP names a path; the file is the caller's, never part of the repo. */
static FILE *xaDumpFp;
static long xaDumpBytes;
static int xaDumpChannels;
static int xaDumpRegistered;

static void xaPut16(unsigned char *at, unsigned long v) {
    at[0] = (unsigned char)(v & 0xFF);
    at[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void xaPut32(unsigned char *at, unsigned long v) {
    at[0] = (unsigned char)(v & 0xFF);
    at[1] = (unsigned char)((v >> 8) & 0xFF);
    at[2] = (unsigned char)((v >> 16) & 0xFF);
    at[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void xaDumpHeader(unsigned char *h) {
    unsigned long data = (unsigned long)xaDumpBytes;
    int channels = xaDumpChannels ? xaDumpChannels : 2;

    memcpy(h + 0, "RIFF", 4);
    xaPut32(h + 4, 36 + data);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    xaPut32(h + 16, 16);
    xaPut16(h + 20, 1);                                   /* PCM          */
    xaPut16(h + 22, (unsigned long)channels);
    xaPut32(h + 24, SND_XA_RATE);
    xaPut32(h + 28, (unsigned long)SND_XA_RATE * channels * 2);
    xaPut16(h + 32, (unsigned long)channels * 2);
    xaPut16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    xaPut32(h + 40, data);
}

static void xaDumpClose(void) {
    unsigned char h[44];

    if (xaDumpFp == NULL) {
        return;
    }
    xaDumpHeader(h);
    if (fseek(xaDumpFp, 0, SEEK_SET) == 0) {
        fwrite(h, 1, sizeof h, xaDumpFp);
    }
    fclose(xaDumpFp);
    xaDumpFp = NULL;
}

static void xaDumpOpen(void) {
    const char *path = getenv("SND_XA_DUMP");
    unsigned char zero[44];

    if (path == NULL || path[0] == '\0') {
        return;
    }
    xaDumpClose();
    xaDumpFp = fopen(path, "wb");
    if (xaDumpFp == NULL) {
        SND_LOG("xa dump: cannot open %s\n", path);
        return;
    }
    xaDumpBytes = 0;
    xaDumpChannels = 0;
    memset(zero, 0, sizeof zero);
    fwrite(zero, 1, sizeof zero, xaDumpFp); /* patched when the dump closes */
    if (!xaDumpRegistered) {
        atexit(xaDumpClose);
        xaDumpRegistered = 1;
    }
    SND_LOG("xa dump %s\n", path);
}

static void xaDumpWrite(const short *pcm, int samples, int channels) {
    if (xaDumpFp == NULL || samples <= 0) {
        return;
    }
    if (xaDumpChannels == 0) {
        xaDumpChannels = channels;
    } else if (channels != xaDumpChannels) {
        /* T36 simplification: one dump file carries one stream's channel
           layout; a mid-file change is dropped rather than mislabelled. */
        return;
    }
    fwrite(pcm, sizeof(short), (size_t)samples, xaDumpFp);
    xaDumpBytes += (long)samples * (long)sizeof(short);
}

/* --- classification and decode (pure, pc/sndtest.c drives them) ---------- */

/* The raw frame's sync: 00 + 10 * FF + 00 */
static int xaSyncOk(const unsigned char *frame) {
    int i;

    if (frame[0] != 0x00 || frame[11] != 0x00) {
        return 0;
    }
    for (i = 1; i < 11; i++) {
        if (frame[i] != 0xFF) {
            return 0;
        }
    }
    return 1;
}

int sndXaClassify(const unsigned char *frame, unsigned long len, SndXaSector *out) {
    const unsigned char *sub;

    if (frame == NULL || out == NULL) {
        return 0;
    }
    /* a whole Form 2 sector: sync + header + subheader + 18 whole sound
       groups + the EDC. A short read is a truncated sector and is refused
       before a single payload byte is touched. */
    if (len < (unsigned long)SND_XA_FRAME_SIZE) {
        return 0;
    }
    if (!xaSyncOk(frame)) {
        return 0;
    }
    sub = frame + SND_XA_SUBHEADER_AT;
    /* the subheader is stored twice; a mismatch is a corrupt frame */
    if (memcmp(sub, sub + 4, 4) != 0) {
        return 0;
    }
    out->file = sub[0];
    out->channel = sub[1];
    out->submode = sub[2];
    out->coding = sub[3];
    if ((out->submode & SND_XA_SUBMODE_AUDIO) != 0 &&
        (out->submode & SND_XA_SUBMODE_FORM2) != 0) {
        out->kind = SND_XA_KIND_AUDIO;
    } else if ((out->submode & (SND_XA_SUBMODE_VIDEO | SND_XA_SUBMODE_DATA)) != 0) {
        out->kind = SND_XA_KIND_VIDEO;
    } else {
        out->kind = SND_XA_KIND_NONE;
    }
    return 1;
}

/*
 * One nibble through the spec's predictor. `param` is the block's left/right
 * (or mono first/second) parameter byte: bits 0..3 are the shift field, bits
 * 4..5 the filter. Field values 13..15 behave like 9. The fixed-point
 * predictor is divided by 64 with a floor, matching the arithmetic shift the
 * hardware uses; +32 is the rounding term.
 */
static int xaDiv64(int v) {
    return (v >= 0) ? (v / 64) : -(((-v) + 63) / 64);
}

static int xaNibble(int nibble, SndXaChannel *ch, int param) {
    int field = param & 0x0F;
    int filter = (param >> 4) & 0x03;
    int shift;
    int t;
    int s;

    if (field > 12) {
        field = 9;
    }
    shift = 12 - field;
    t = (nibble & 0x08) ? (nibble - 16) : nibble;
    s = t * (1 << shift)
        + xaDiv64(ch->old * xaPos[filter] + ch->older * xaNeg[filter] + 32);
    if (s > 32767) {
        s = 32767;
    } else if (s < -32768) {
        s = -32768;
    }
    ch->older = ch->old;
    ch->old = s;
    return s;
}

void sndXaResetState(SndXaState *state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof *state);
}

int sndXaDecodeAudio(const unsigned char *frame, unsigned long len,
                     short *pcm, int maxSamples, SndXaState *state) {
    SndXaSector sec;
    int g, b, j, n = 0;

    if (pcm == NULL || state == NULL ||
        maxSamples < SND_XA_SECTOR_SAMPLES) {
        return -1;
    }
    if (!sndXaClassify(frame, len, &sec) || sec.kind != SND_XA_KIND_AUDIO) {
        return -1;
    }

    for (g = 0; g < SND_XA_GROUPS; g++) {
        const unsigned char *grp = frame + SND_XA_PAYLOAD_AT
                                   + g * SND_XA_GROUP_SIZE;

        for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
            int leftParam = grp[4 + b * 2];
            int rightParam = grp[5 + b * 2];

            if ((sec.coding & SND_XA_CODING_STEREO) != 0) {
                /* one L/R frame: low nibble left, high nibble right */
                for (j = 0; j < SND_XA_GROUP_WORDS; j++) {
                    int byte = grp[SND_XA_GROUP_HEADER + j * 4 + b];

                    pcm[n++] = (short)xaNibble(byte & 0x0F,
                                               &state->channel[0], leftParam);
                    pcm[n++] = (short)xaNibble((byte >> 4) & 0x0F,
                                               &state->channel[1], rightParam);
                }
            } else {
                /* mono: block b's 28 first-half samples, then its 28
                   second-half samples, each with its own parameter and
                   history */
                for (j = 0; j < SND_XA_GROUP_WORDS; j++) {
                    int byte = grp[SND_XA_GROUP_HEADER + j * 4 + b];

                    pcm[n++] = (short)xaNibble(byte & 0x0F,
                                               &state->channel[0], leftParam);
                }
                for (j = 0; j < SND_XA_GROUP_WORDS; j++) {
                    int byte = grp[SND_XA_GROUP_HEADER + j * 4 + b];

                    pcm[n++] = (short)xaNibble((byte >> 4) & 0x0F,
                                               &state->channel[1], rightParam);
                }
            }
        }
    }
    return n;
}

/* --- the OpenAL streaming sink (the real sink; host_snd.c installs it) --- */

#define SND_XA_AL_BUFS 8

static ALuint xaAlSource;
static ALuint xaAlBufs[SND_XA_AL_BUFS];
static int xaAlUsed[SND_XA_AL_BUFS];
static int xaAlFrames[SND_XA_AL_BUFS]; /* sample frames in that buffer */
static int xaAlReady;
static int xaAlFailed;
static ALenum xaAlFormat;
static long xaAlDropped;

static int xaAlOpen(int channels) {
    int i;
    ALenum err;

    /* DW3_SND_MUTE: behave exactly like a host with no audio device (the
       failure path the sink callbacks already have, with its pinned real-time
       T36 semantics): the movie keeps playing, no OpenAL source is created
       and no stream is born. */
    if (xaAlFailed == 0 && getenv("DW3_SND_MUTE") != NULL) {
        xaAlFailed = 1;
        SND_LOG("xa openal: muted (DW3_SND_MUTE), acting as no device\n");
        return 0;
    }

    /* A pending error from another AL user would make the checks below lie;
       the error state is global, so clear it before asking for our objects. */
    while (alGetError() != AL_NO_ERROR) {
    }
    alGenSources(1, &xaAlSource);
    err = alGetError();
    if (err != AL_NO_ERROR || xaAlSource == 0) {
        xaAlFailed = 1;
        SND_LOG("xa openal: no source (al error 0x%x, no audio device?)\n",
                (unsigned)err);
        return 0;
    }
    alGenBuffers(SND_XA_AL_BUFS, xaAlBufs);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        xaAlFailed = 1;
        SND_LOG("xa openal: no buffers (al error 0x%x)\n", (unsigned)err);
        return 0;
    }
    for (i = 0; i < SND_XA_AL_BUFS; i++) {
        xaAlUsed[i] = 0;
        xaAlFrames[i] = 0;
    }
    xaAlFormat = (channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    alSourcef(xaAlSource, AL_PITCH, 1.0f);
    alSourcef(xaAlSource, AL_GAIN, (float)sndUserGainPermille(SND_VOL_MOVIE) / 1000.0f); /* O4 */
    alSourcei(xaAlSource, AL_LOOPING, AL_FALSE);
    xaAlReady = 1;
    SND_LOG("xa openal source=%u %d ch %d Hz\n", (unsigned)xaAlSource,
            channels, SND_XA_RATE);
    return 1;
}

/* Recycle the buffers the source is done with. Called from the sample path and
   from the capacity/queued queries, always under the driver's mutex. */
static void xaAlRecycle(void) {
    ALint processed;
    int i;

    if (!xaAlReady) {
        return;
    }
    alGetSourcei(xaAlSource, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
        ALuint done = 0;

        alSourceUnqueueBuffers(xaAlSource, 1, &done);
        for (i = 0; i < SND_XA_AL_BUFS; i++) {
            if (xaAlBufs[i] == done) {
                xaAlUsed[i] = 0;
                xaAlFrames[i] = 0;
            }
        }
    }
}

static void xaAlSamples(void *ctx, const short *pcm, int samples, int channels) {
    ALint state;
    ALuint buf;
    int i, freeIdx = -1;

    (void)ctx;
    if (xaAlFailed) {
        return;
    }
    if (!xaAlReady && !xaAlOpen(channels)) {
        return;
    }
    /* O4: the movie slider and the master, at every chunk (a tick away at most) */
    alSourcef(xaAlSource, AL_GAIN, (float)sndUserGainPermille(SND_VOL_MOVIE) / 1000.0f);
    /* recycle the buffers the source is done with */
    xaAlRecycle();
    for (i = 0; i < SND_XA_AL_BUFS; i++) {
        if (!xaAlUsed[i]) {
            freeIdx = i;
            break;
        }
    }
    if (freeIdx < 0) {
        /* T36 simplification: the queue is full, the chunk is dropped (T40's
           demand-driven tick only feeds when the sink reports room, so this is
           the safety valve of the fixed-tick path, not the normal case). */
        xaAlDropped++;
        if (xaAlDropped == 1) {
            SND_LOG("xa openal: queue full, dropping chunks (ch=%d frames=%d)\n",
                    channels,
                    samples / (channels > 0 ? channels : 1));
        }
        return;
    }
    buf = xaAlBufs[freeIdx];
    /* pcm is interleaved: samples = frames * channels. A whole sector is
       2016 frames * 2 ch * 2 bytes = 8064 bytes (stereo), and a tick hands
       it over in <= 756-frame slices, so one buffer is samples * 2 bytes. */
    alBufferData(buf, xaAlFormat, pcm, (ALsizei)(samples * (int)sizeof(short)),
                 SND_XA_RATE);
    alSourceQueueBuffers(xaAlSource, 1, &buf);
    xaAlUsed[freeIdx] = 1;
    xaAlFrames[freeIdx] = (channels > 0) ? samples / channels : 0;
    alGetSourcei(xaAlSource, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        alSourcePlay(xaAlSource);
    }
}

/*
 * T40, the sink's capacity: free buffers, in sample frames. Each buffer holds
 * one chunk (SND_XA_TICK_FRAMES). Before the source exists one chunk is
 * reported, because that first chunk is what opens it (the channel count comes
 * with the samples, not with the query). A failed source reports -1: the
 * driver then keeps T36's fixed tick and its produced-frames clock, so a host
 * with no audio device still plays the movie at real time instead of stalling
 * the pacer.
 */
static int xaAlSpace(void *ctx) {
    int i, freeBufs = 0;

    (void)ctx;
    if (xaAlFailed) {
        return -1;
    }
    if (!xaAlReady) {
        return SND_XA_TICK_FRAMES;
    }
    xaAlRecycle();
    for (i = 0; i < SND_XA_AL_BUFS; i++) {
        if (!xaAlUsed[i]) {
            freeBufs++;
        }
    }
    return freeBufs * SND_XA_TICK_FRAMES;
}

/* Frames accepted but not played yet, or -1 when the sink cannot tell. */
static int xaAlQueued(void *ctx) {
    long frames = 0;
    int i;

    (void)ctx;
    if (xaAlFailed) {
        return -1;
    }
    if (!xaAlReady) {
        return 0;
    }
    xaAlRecycle();
    for (i = 0; i < SND_XA_AL_BUFS; i++) {
        if (xaAlUsed[i]) {
            frames += xaAlFrames[i];
        }
    }
    return (int)frames;
}

static void xaAlStop(void) {
    ALint queued;

    if (!xaAlReady) {
        return;
    }
    alSourceStop(xaAlSource);
    alGetSourcei(xaAlSource, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        ALuint b = 0;

        alSourceUnqueueBuffers(xaAlSource, 1, &b);
    }
    memset(xaAlUsed, 0, sizeof(xaAlUsed));
    memset(xaAlFrames, 0, sizeof(xaAlFrames));
}

static const SndXaSink xaAlSink = { xaAlSamples, xaAlSpace, xaAlQueued };

void sndXaUseOpenAl(void) {
    xaDebugSet = (getenv("SND_DEBUG") != NULL);
    xaSink = &xaAlSink;
    xaSinkCtx = NULL;
}

/* --- the stream driver --------------------------------------------------- */

/* The ring holds one sector's whole decode: 4032 interleaved samples for
   stereo (2016 frames) or 4032 mono frames. Counts and positions are in
   frames, never in interleaved samples. */
static short xaRing[SND_XA_SECTOR_SAMPLES];
static int xaRingPos;
static int xaRingCount;
static int xaRingChannels;

/* Predictor history, shared by every sector of the current stream. */
static SndXaState xaState;

/* T40: the driver is pumped from PsyX's vsync thread (host_snd.c) and, while
   the movie's video pacer waits, from the game's own thread (host_press.c).
   Everything that touches the ring, the OpenAL source or the sink takes this
   mutex; the sink's own queries never take it again. */
static pthread_mutex_t xaLock = PTHREAD_MUTEX_INITIALIZER;

static long xaNextLba;
static int xaActive;
static int xaEnded;
static int xaFile;
static int xaChannel;
static int xaSkipRun;

/* T40: sample frames handed to the sink since sndXaStart, the clock the video
   pacer reads (minus what the sink still holds). Counted in frames, never in
   interleaved samples. */
static long xaFramesProduced;

/* The stream's start on the monotonic clock, set by sndXaStart: the playhead a
   sink without capacity reporting follows (xaPumpLocked). */
static struct timespec xaStartTime;

static double xaWallMs(void) {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - xaStartTime.tv_sec) * 1000.0
           + (now.tv_nsec - xaStartTime.tv_nsec) / 1000000.0;
}
static long xaUnderruns;
static int xaWasEmpty;

/* diagnostics for the SND_DEBUG log and for nothing else */
static long xaSectors;
static long xaDecoded;
static long xaSkipped;
static long xaOutSamples;
static long xaPeak;
static long xaSumAbs;
static long xaTicks;

/* T36 simplification: how many consecutive non-matching sectors mean the
   stream is over (the discs carry 7 video sectors between audio ones). */
#define SND_XA_SKIP_MAX 64

void sndXaSetSink(const SndXaSink *sink, void *ctx) {
    xaDebugSet = (getenv("SND_DEBUG") != NULL);
    xaSink = sink;
    xaSinkCtx = ctx;
}

void sndXaSetSource(SndXaReadFn read, void *ctx) {
    xaDebugSet = (getenv("SND_DEBUG") != NULL);
    xaRead = read;
    xaReadCtx = ctx;
}

/* E2b: the PCM source that stands in for the disc (host_xa.h), none by default */
static SndXaPcmFn xaPcm;
static void *xaPcmCtx;
static int xaPcmChannels;

void sndXaSetPcm(int channels, SndXaPcmFn read, void *ctx) {
    pthread_mutex_lock(&xaLock);
    xaPcm = (read != NULL && (channels == 1 || channels == 2)) ? read : NULL;
    xaPcmCtx = ctx;
    xaPcmChannels = channels;
    pthread_mutex_unlock(&xaLock);
}

void sndXaArm(long lba) {
    pthread_mutex_lock(&xaLock);
    xaNextLba = lba;
    xaFramesProduced = 0;
    xaUnderruns = 0;
    xaWasEmpty = 0;
    pthread_mutex_unlock(&xaLock);
    SND_LOG("xa arm lba=%ld\n", lba);
}

/*
 * Reads sectors until the ring holds one whole sector's decode, or the stream
 * ends. One sector is 2016 stereo frames (4032 interleaved samples) or 4032
 * mono frames. Video/data sectors and audio of another (file, channel) are
 * skipped. Returns 1 with the ring filled, 0 otherwise.
 */
static int xaFillRing(void) {
    unsigned char frame[SND_XA_FRAME_SIZE];

    if (xaPcm != NULL) {
        int n;

        if (xaEnded) {
            return 0;
        }
        n = xaPcm(xaPcmCtx, xaRing, SND_XA_SECTOR_SAMPLES / xaPcmChannels);
        if (n <= 0) {
            xaEnded = 1;
            SND_LOG("xa end (pcm source) after %ld frames\n", xaFramesProduced);
            return 0;
        }
        xaRingChannels = xaPcmChannels;
        xaRingPos = 0;
        xaRingCount = n;
        xaDecoded++;
        return 1;
    }

    while (!xaEnded) {
        SndXaSector sec;
        int n;

        if (xaRead == NULL || !xaRead(xaReadCtx, xaNextLba, frame)) {
            xaEnded = 1;
            SND_LOG("xa end (source) at lba %ld\n", xaNextLba);
            break;
        }
        xaNextLba++;
        xaSectors++;

        if (!sndXaClassify(frame, SND_XA_FRAME_SIZE, &sec) ||
            sec.kind != SND_XA_KIND_AUDIO) {
            if (++xaSkipRun > SND_XA_SKIP_MAX) {
                xaEnded = 1;
                SND_LOG("xa end (%ld sectors without audio) at lba %ld\n",
                        xaSkipRun, xaNextLba);
                break;
            }
            xaSkipped++;
            continue;
        }
        if (xaFile < 0) {
            /* T36 simplification: no CdlSetfilter in the movie path, so the
               first audio sector names the (file, channel) to play. */
            xaFile = sec.file;
            xaChannel = sec.channel;
            SND_LOG("xa select file=%d channel=%d coding=%02x\n",
                    xaFile, xaChannel, sec.coding);
        }
        if (sec.file != xaFile || sec.channel != xaChannel) {
            if (++xaSkipRun > SND_XA_SKIP_MAX) {
                xaEnded = 1;
                break;
            }
            xaSkipped++;
            continue;
        }
        xaSkipRun = 0;

        n = sndXaDecodeAudio(frame, SND_XA_FRAME_SIZE, xaRing,
                             SND_XA_SECTOR_SAMPLES, &xaState);
        if (n < 0) {
            continue;
        }
        xaDecoded++;
        xaRingChannels = (sec.coding & SND_XA_CODING_STEREO) ? 2 : 1;
        xaRingPos = 0;
        xaRingCount = n / xaRingChannels;
        if ((sec.submode & SND_XA_SUBMODE_EOF) != 0) {
            xaEnded = 1;
        }
        return 1;
    }
    return 0;
}

/*
 * One sink handoff: takes up to `need` frames from the ring (a whole sector at
 * a time when it runs dry) and hands them over in a single call. Returns the
 * frames delivered, 0 when the stream is over. One handoff is one sink buffer,
 * which is why the demand-driven tick asks for room before each of these.
 */
static int xaFeedOnce(int need) {
    int avail, take, samples, i;
    const short *src;

    if (xaRingCount <= 0) {
        if (!xaFillRing()) {
            return 0;
        }
    }
    avail = xaRingCount;
    take = (avail < need) ? avail : need;
    if (take <= 0) {
        return 0;
    }
    samples = take * xaRingChannels;
    src = xaRing + xaRingPos * xaRingChannels;
    for (i = 0; i < samples; i++) {
        int a = src[i];

        if (a < 0) {
            a = -a;
        }
        if (a > xaPeak) {
            xaPeak = a;
        }
        xaSumAbs += a;
    }
    if (xaSink != NULL && xaSink->samples != NULL) {
        xaSink->samples(xaSinkCtx, src, samples, xaRingChannels);
    }
    xaDumpWrite(src, samples, xaRingChannels);
    xaRingPos += take;
    xaRingCount -= take;
    xaOutSamples += samples;
    xaFramesProduced += take;
    return take;
}

/*
 * Decodes and hands the sink up to `need` sample frames: from the ring, and
 * from fresh sectors when it runs dry. Returns the frames delivered (0 when
 * the stream is over). The chunking is T36's: a chunk never crosses a sector
 * boundary in the ring, which is what pc/sndtest.c's expected chunk sizes pin
 * (one fixed tick may therefore hand over several chunks, e.g. 252 + 504).
 */
static int xaFeed(int need) {
    int done = 0;

    while (done < need) {
        int take = xaFeedOnce(need - done);

        if (take <= 0) {
            break;
        }
        done += take;
    }
    return done;
}

/*
 * T40: keep the sink full instead of delivering one fixed PAL frame per tick.
 * The sink reports how much room it has and the tick decodes while that room
 * lasts, up to SND_XA_PUMP_CHUNKS chunks per call: the queue stays fed whether
 * the main loop runs at 50 Hz or is blocked by the video pacer, and no caller
 * spends unbounded time here. A sink that cannot report capacity (pc/sndtest.c
 * fake) keeps T36's one-chunk-per-tick path, which is what its expectations
 * measure.
 */
static void xaPumpLocked(void) {
    int chunks = 0;

    if (xaSink != NULL && xaSink->space != NULL) {
        int space = xaSink->space(xaSinkCtx);

        if (space < 0) {
            /* the sink exists but cannot report capacity (a failed OpenAL
               source, or DW3_SND_MUTE): nothing drains at playback speed, so
               the wall clock does. The video pacer ticks every millisecond
               while it waits; a chunk per call there ran movies at decode
               speed. Feed only the chunks real time has made due. */
            long due = (long)(xaWallMs() * SND_XA_RATE / 1000);

            while (chunks < SND_XA_PUMP_CHUNKS
                   && xaFramesProduced + SND_XA_TICK_FRAMES <= due) {
                if (xaFeedOnce(SND_XA_TICK_FRAMES) <= 0) {
                    break;
                }
                chunks++;
            }
            return;
        }
        if (xaSink->queued != NULL) {
            int queued = xaSink->queued(xaSinkCtx);

            if (queued == 0 && xaFramesProduced > 0 && !xaEnded) {
                if (!xaWasEmpty) {
                    xaUnderruns++;
                    xaWasEmpty = 1;
                }
            } else if (queued > 0) {
                xaWasEmpty = 0;
            }
        }
        while (chunks < SND_XA_PUMP_CHUNKS) {
            if (space < SND_XA_TICK_FRAMES) {
                break;
            }
            /* one handoff is one sink buffer, so the space asked for above is
               exactly what this consumes */
            if (xaFeedOnce(SND_XA_TICK_FRAMES) <= 0) {
                break;
            }
            chunks++;
            space = xaSink->space(xaSinkCtx);
        }
    } else {
        xaFeed(SND_XA_TICK_FRAMES);
    }
}

/* The sink's unplayed frames, or -1 when it cannot tell. The caller holds the
   lock; the sink's queries never take it. */
static int xaQueuedLocked(void) {
    if (xaSink == NULL || xaSink->queued == NULL) {
        return -1;
    }
    return xaSink->queued(xaSinkCtx);
}

void sndXaStart(void) {
    pthread_mutex_lock(&xaLock);
    xaDebugSet = (getenv("SND_DEBUG") != NULL) || xaDebugSet;
    xaRingPos = 0;
    xaRingCount = 0;
    xaRingChannels = 1;
    sndXaResetState(&xaState);
    xaFile = -1;
    xaChannel = -1;
    xaSkipRun = 0;
    xaEnded = 0;
    xaActive = 1;
    xaFramesProduced = 0;
    xaUnderruns = 0;
    xaWasEmpty = 0;
    xaSectors = xaDecoded = xaSkipped = 0;
    xaOutSamples = xaPeak = xaSumAbs = 0;
    xaTicks = 0;
    clock_gettime(CLOCK_MONOTONIC, &xaStartTime);
    xaDumpOpen();
    SND_LOG("xa start lba=%ld\n", xaNextLba);
    pthread_mutex_unlock(&xaLock);
}

void sndXaStop(void) {
    pthread_mutex_lock(&xaLock);
    /* the clock is 0 between streams: a movie presented after this one must
       not wait on the previous stream's position */
    xaFramesProduced = 0;
    xaUnderruns = 0;
    xaWasEmpty = 0;
    if (!xaActive) {
        pthread_mutex_unlock(&xaLock);
        return;
    }
    xaActive = 0;
    xaEnded = 1;
    xaAlStop();
    SND_LOG("xa stop sectors=%ld decoded=%ld video=%ld samples=%ld\n",
            xaSectors, xaDecoded, xaSkipped, xaOutSamples);
    pthread_mutex_unlock(&xaLock);
}

void sndXaTick(void) {
    pthread_mutex_lock(&xaLock);
    if (!xaActive) {
        pthread_mutex_unlock(&xaLock);
        return;
    }
    xaPumpLocked();
    if (xaEnded && xaRingCount <= 0) {
        xaActive = 0;
        SND_LOG("xa done sectors=%ld decoded=%ld video=%ld samples=%ld "
                "peak=%ld mean=%ld dropped=%ld underruns=%ld\n",
                xaSectors, xaDecoded, xaSkipped, xaOutSamples, xaPeak,
                xaOutSamples ? xaSumAbs / xaOutSamples : 0, xaAlDropped,
                xaUnderruns);
    } else if ((++xaTicks % 64) == 0) {
        /* T36 simplification: a live stream may outlast the harness run, so
           the counters are also reported periodically (every ~1.3 s of
           audio). SND_DEBUG only. */
        SND_LOG("xa progress ticks=%ld sectors=%ld decoded=%ld video=%ld "
                "samples=%ld pos=%ld queued=%d underruns=%ld peak=%ld "
                "mean=%ld dropped=%ld\n",
                xaTicks, xaSectors, xaDecoded, xaSkipped, xaOutSamples,
                xaFramesProduced, xaQueuedLocked(), xaUnderruns, xaPeak,
                xaOutSamples ? xaSumAbs / xaOutSamples : 0, xaAlDropped);
    }
    pthread_mutex_unlock(&xaLock);
}

long sndXaPosition(void) {
    long pos;
    int queued;

    pthread_mutex_lock(&xaLock);
    pos = xaFramesProduced;
    queued = xaQueuedLocked();
    if (queued >= 0) {
        pos -= queued;
    }
    if (pos < 0) {
        pos = 0;
    }
    pthread_mutex_unlock(&xaLock);
    return pos;
}

int sndXaQueuedFrames(void) {
    int queued;

    pthread_mutex_lock(&xaLock);
    queued = xaQueuedLocked();
    pthread_mutex_unlock(&xaLock);
    return (queued < 0) ? 0 : queued;
}

long sndXaUnderruns(void) {
    long underruns;

    pthread_mutex_lock(&xaLock);
    underruns = xaUnderruns;
    pthread_mutex_unlock(&xaLock);
    return underruns;
}

int sndXaActive(void) {
    return xaActive;
}
