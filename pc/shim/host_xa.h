/*
 * The port's own CD-XA (streamed audio) decoder and stream driver, behind the
 * CD control surface the game's movie player already uses (shim/host_cd.c).
 * ADR-002: PsyCross decodes VAG but has no XA support at all and no libsnd
 * serial API, so the movie streams' audio is this port's own code.
 *
 * T36 scope: audio only. The frames are read straight from the owner's raw
 * disc image (pc/shim/host_cd.c owns the handle and installs the reader), so
 * the extracted STR copies under disks/eu/AAA -- form 1 payload only -- are
 * never used for audio.
 *
 * Raw frame (2352 bytes, the same framing tools/extract_disc.py reads):
 *
 *     0x000  12  sync   00 ff ff ff ff ff ff ff ff ff ff 00
 *     0x00C   4  header minute/second/sector BCD + mode (2 = Mode 2)
 *     0x010   8  subheader file, channel, submode, coding info + a copy
 *     0x018 2304 payload 18 sound groups of 128 bytes (the last 20 of the
 *                       2324-byte Form 2 payload are padding)
 *     0x92C   4  EDC
 *
 * An audio sector is submode AUDIO (0x04) | FORM2 (0x20); everything else is
 * video/data and is skipped. A 128-byte sound group is a 16-byte header plus
 * 28 four-byte words: four 28-sample blocks, each with a left/right parameter
 * pair (stereo; the low/high nibble of a data byte) or a first/second half
 * (mono). A stereo sector therefore carries 2016 L/R frames and a mono sector
 * 4032 frames. The predictor history is per channel and persists across
 * blocks, groups and sectors for the life of the stream. The exact decode is
 * documented at sndXaDecodeAudio in host_xa.c.
 *
 * The driver is engine-agnostic on purpose: the sector source and the PCM
 * sink are both injected, so pc/sndtest.c drives the whole stream with a fake
 * source and a recording sink and never opens OpenAL. host_cd.c installs the
 * disc reader as the source; host_snd.c installs the OpenAL streamer as the
 * sink (sndXaUseOpenAl) and pumps sndXaTick once a frame.
 *
 * T40: the driver is also the movie's clock. sndXaPosition counts the sample
 * frames the stream has played since sndXaStart (37800 per second), which is
 * the time base the video pacer uses (shim/host_press.c): the movies are 15
 * fps, so video frame N is due at sample frame N * 2520. The pump is
 * demand-driven when the sink reports its free/queued capacity (the OpenAL sink
 * below does), so a stalled main loop -- the video pacer blocks it for up to a
 * frame at a time -- does not stall the audio; a sink that cannot report it
 * (pc/sndtest.c's recording sink) keeps the fixed one-PAL-frame-per-tick
 * behaviour the unit tests pin. sndXaTick may be called from more than one
 * thread (PsyCross's vsync thread and the game's own, from the pacer), so the
 * driver serialises it with a mutex.
 */
#ifndef PC_SHIM_HOST_XA_H
#define PC_SHIM_HOST_XA_H

/* --- the raw frame and the CD-XA subheader fields ------------------------ */

#define SND_XA_FRAME_SIZE   2352
#define SND_XA_SUBHEADER_AT 16
#define SND_XA_PAYLOAD_AT   24
#define SND_XA_PAYLOAD_SIZE 2304  /* 18 groups * 128; the Form 2 payload adds 20 pad + EDC */

#define SND_XA_SUBMODE_EOR   0x01
#define SND_XA_SUBMODE_VIDEO 0x02
#define SND_XA_SUBMODE_AUDIO 0x04
#define SND_XA_SUBMODE_DATA  0x08
#define SND_XA_SUBMODE_TRIG  0x10
#define SND_XA_SUBMODE_FORM2 0x20
#define SND_XA_SUBMODE_RT    0x40
#define SND_XA_SUBMODE_EOF   0x80

/* coding info bits */
#define SND_XA_CODING_STEREO 0x01 /* set: 2 channels inside each sector          */
#define SND_XA_CODING_RATE   0x0C /* 00 = 37.8 kHz, 01 = 18.9 kHz (bits 2..3)   */
#define SND_XA_CODING_BITS   0x30 /* 00 = 4-bit, 01 = 8-bit (bits 4..5)         */

/* --- the sound group layout ---------------------------------------------- */

#define SND_XA_GROUP_SIZE     128
#define SND_XA_GROUP_HEADER   16
#define SND_XA_GROUPS         18
#define SND_XA_GROUP_BLOCKS   4
#define SND_XA_BLOCK_SAMPLES  28
#define SND_XA_GROUP_WORDS    28
#define SND_XA_SECTOR_SAMPLES \
    (SND_XA_GROUPS * SND_XA_GROUP_BLOCKS * SND_XA_BLOCK_SAMPLES * 2) /* 4032 */
#define SND_XA_STEREO_FRAMES \
    (SND_XA_SECTOR_SAMPLES / 2)                                  /* 2016 */
#define SND_XA_MONO_FRAMES     SND_XA_SECTOR_SAMPLES                 /* 4032 */

/* One PAL frame of 37.8 kHz audio: the chunk the driver hands the sink at a
   time, and the size of one OpenAL buffer. A sink that reports capacity is
   filled with as many of these chunks as it has room for; a sink that does not
   gets exactly one per sndXaTick (T36's fixed behaviour, which pc/sndtest.c
   pins). */
#define SND_XA_TICK_FRAMES  756

/* The most chunks one sndXaTick decodes when the sink reports capacity: 4 * 20
   ms = 80 ms per call, so a stalled caller never spends unbounded time here and
   the 8-buffer queue still refills in two ticks. */
#define SND_XA_PUMP_CHUNKS  4

/* The sector kinds sndXaClassify tells apart */
#define SND_XA_KIND_NONE  0
#define SND_XA_KIND_AUDIO 1
#define SND_XA_KIND_VIDEO 2

/* One classified raw frame. */
typedef struct SndXaSector {
    int kind;                /* SND_XA_KIND_* */
    unsigned char file;      /* subheader file number  */
    unsigned char channel;   /* subheader channel number */
    unsigned char submode;   /* subheader submode byte */
    unsigned char coding;    /* subheader coding info */
} SndXaSector;

/*
 * Classifies a raw frame: 1 with *out filled when it is a well-formed CD-XA
 * sector (sync, a subheader whose two copies agree and a full payload),
 * 0 otherwise. A truncated or corrupt frame is refused here, before any
 * payload byte is touched.
 */
int sndXaClassify(const unsigned char *frame, unsigned long len, SndXaSector *out);

/*
 * The predictor history is per channel: channel 0 is the left (or mono's
 * first) half and channel 1 the right (or mono's second) half. It persists
 * across blocks, groups and sectors for the life of the stream; reset it only
 * when the stream starts (or after a seek/stop).
 */
typedef struct SndXaChannel {
    int old;
    int older;
} SndXaChannel;

typedef struct SndXaState {
    SndXaChannel channel[2];
} SndXaState;

void sndXaResetState(SndXaState *state);

/*
 * Decodes one audio frame's 18 groups into pcm. Stereo writes 2016 interleaved
 * L/R frames (4032 samples); mono writes 4032 frames, the low nibble as the
 * first half and the high nibble as the second. `state` carries the two
 * channels' predictor history across calls. Returns the number of 16-bit
 * samples written (4032) or -1 when the frame is not a full audio sector,
 * *pcm is too small, or state is NULL. Never writes past maxSamples.
 */
int sndXaDecodeAudio(const unsigned char *frame, unsigned long len,
                     short *pcm, int maxSamples, SndXaState *state);

/* --- the injectable stream driver ---------------------------------------- */

/*
 * The PCM sink: `samples` 16-bit values, `channels` interleaved (1 mono,
 * 2 stereo), at 37800 Hz. host_xa.c's own OpenAL sink and pc/sndtest.c's
 * recording sink both implement this; nothing below needs OpenAL.
 *
 * `space` and `queued` are optional (a NULL member is "cannot report", and a
 * zero-initialised struct is the old T36 sink):
 *
 *   space   sample frames the sink can accept right now, or -1 when it cannot
 *           say. With it the driver decodes only while there is room, so the
 *           queue stays full no matter how the main loop is paced.
 *   queued  sample frames accepted but not played yet, or -1. The driver
 *           subtracts them from what it produced, so sndXaPosition is the
 *           playhead (what the player hears), not the decode-ahead point.
 *
 * Counts are frames (a stereo L/R pair counts once), never interleaved
 * samples.
 */
typedef struct SndXaSink {
    void (*samples)(void *ctx, const short *pcm, int samples, int channels);
    int  (*space)(void *ctx);
    int  (*queued)(void *ctx);
} SndXaSink;

/* The sector source: reads one raw 2352-byte frame at `lba`; 1 = read,
   0 = past the stream's end (or unreadable). host_cd.c's disc reader and
   pc/sndtest.c's fake source both implement this. */
typedef int (*SndXaReadFn)(void *ctx, long lba, unsigned char *frame);

/* Inject the sink / the source (pc/sndtest.c replaces both; host_cd.c sets
   the source, host_snd.c sets the OpenAL sink). */
void sndXaSetSink(const SndXaSink *sink, void *ctx);
void sndXaSetSource(SndXaReadFn read, void *ctx);

/*
 * A PCM source instead of disc sectors (content-export E2b, a mod's replacement movie, shim/video_mod.c): while one is set the
 * driver asks it for audio instead of reading and decoding XA sectors, and everything after that (the sink, the pump, the clock
 * sndXaPosition, the stop at the end) is the same. `read` fills up to `frames` sample frames of `channels` interleaved 16-bit
 * values at 37800 Hz (at most SND_XA_SECTOR_SAMPLES / channels) and returns how many; 0 is the end of the stream. The sink fixes
 * its format at the first chunk it gets, so a source should keep to the stereo the discs' movies use. read is called under the
 * driver's lock, from whichever thread pumps. Pass read NULL to go back to the disc.
 */
typedef int (*SndXaPcmFn)(void *ctx, short *pcm, int frames);
void sndXaSetPcm(int channels, SndXaPcmFn read, void *ctx);

/* Install the real OpenAL streaming sink (host_snd.c, at sound init). */
void sndXaUseOpenAl(void);

/* The CD control surface, as the game's movie player drives it:
   CdControl(CdlSetloc) -> sndXaArm, CdRead2(CdlModeRT) -> sndXaStart,
   CdControl(CdlPause) -> sndXaStop. sndXaTick keeps the sink fed: demand-driven
   while the sink reports capacity, otherwise one PAL frame per call. It is a
   no-op while no stream is armed. */
void sndXaArm(long lba);
void sndXaStart(void);
void sndXaStop(void);
void sndXaTick(void);
int  sndXaActive(void);

/*
 * T40, the movie's audio clock: sample frames played since the stream started
 * (sndXaStart), 0 while no stream is active. Reset by sndXaArm/sndXaStop/
 * sndXaStart. 37800 per second -- the movies run at 15 fps, so one video frame
 * is 2520 sample frames (shim/host_press.c).
 *
 * The value is the playhead: the driver produced N frames and the sink still
 * holds M of them, so the position is N - M. A sink that cannot report what it
 * holds makes the position the produced count (a decode-ahead clock); that is
 * the fixed-tick path of pc/sndtest.c and the no-audio-device fallback.
 */
long sndXaPosition(void);

/* Sample frames the OpenAL sink still holds (0 when it cannot report). For the
   SND_DEBUG log only. */
int sndXaQueuedFrames(void);

/* Times the OpenAL sink ran dry while the stream was active. For the logs. */
long sndXaUnderruns(void);

#endif /* PC_SHIM_HOST_XA_H */
