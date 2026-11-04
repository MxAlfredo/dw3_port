#ifndef PC_AUDIO_MOD_H
#define PC_AUDIO_MOD_H

/*
 * A mod replaces sounds (content-export E3c/E3d, ADR-005 / ADR-010). The files are the ones the audio export writes
 * (data/audio/<BANK>/, audio_export.h), put in mods/<mod>/audio/<BANK>/ with the same names; the last active mod that has a file
 * wins. Without such files nothing here is touched and the sound plays exactly as before. A file the port cannot use is refused with
 * `[audio] <mod>: <file> rejected: <why>` (once per run) and the next mod, then the game's own sound, is used.
 *
 * E3c, sound effects: vag_NNN.wav replaces the sample NNN of the bank (every tone that uses the sample changes, in effects and in
 * music sequences alike). The WAV is 16-bit PCM, mono or stereo (the channels are averaged), at any rate. It is resampled to 44100 Hz,
 * the rate the SPU plays a sample at when its pitch value is 0x1000 (what the exported WAVs are), then encoded to the SPU's ADPCM
 * and written into the bank's SPU RAM when the bank loads (snd_spu.c): the tones keep their pitch, volume, pan and envelope.
 * The rate of the file only says how fast the recording runs; the note it sounds at is the tone's root note (the smpl chunk of the
 * exported file says it), so a recording of the original's pitch and length sounds right at any rate. A "smpl" loop makes the
 * sample loop (whole 28-sample blocks: start rounded down to a multiple of 28, see vag_encode.h) and the sound after the loop end is
 * dropped; without a loop the sample plays once. A sample longer than the original is fine and has no fixed cap (U7): the replaced
 * samples are not written into PsyCross's SPU RAM (a fixed 2 MB array with a 512K-sample decode buffer, a library the port never
 * patches) but decoded to PCM and kept by the output (see "PCM voices" below); only memory limits their length.
 *
 * E3d, music: seq_<pkg>_<rec>.wav (the name of the exported seq_<pkg>_<rec>.mid; the record number may also be written without the
 * leading zero) plays in place of that sequence when the game starts it (SsSepPlay). 16-bit PCM, mono or stereo, any rate (OpenAL
 * resamples). The loop is, in this order: the WAV's smpl loop (frames, the end being the last frame of the loop), a sidecar
 * seq_<pkg>_<rec>.json {"loopStart": frames, "loopEnd": frames}, the whole file when the original sequence loops forever, or none (it plays
 * once and the game's sequence is over). The game's stop, its volume (SsSepSetVol), its fade-out (SsSepSetDecrescendo), the player's master
 * and music (or effects, for a sequence that is not the music) sliders all act on it. A file up to AUDIO_MOD_MEM_BYTES of samples is
 * loaded whole, a longer one is read from the disk in chunks. A refused file falls back to the game's sequence.
 */

#include <stddef.h>

typedef struct AudioModIo {
    /* the folder name ("BGM001") of the bank being loaded into the game's sound slot 0..2, 1 when known */
    int (*slotBank)(void *ud, int slot, char *name, int cap);
    int (*modCount)(void *ud);
    const char *(*modName)(void *ud, int i);
    const char *(*modDir)(void *ud, int i); /* no trailing slash */
    void *ud;
} AudioModIo;

/* Where the banks and the mods are. NULL turns replacement off. Safe to call again. */
void audioModSetup(const AudioModIo *io);

/* The bank of a sound slot is read when snd_spu.c opens it and forgotten when it closes; "" when unknown. */
void audioModBankOpened(int slot);
void audioModBankClosed(int slot);
const char *audioModBankOf(int slot);

typedef struct AudioModVag {
    unsigned char *adpcm;  /* malloc'ed: the caller frees it */
    unsigned long size;    /* bytes, a multiple of 16 */
    int loops;             /* the last block has the repeat flag */
    unsigned long frames;  /* PCM frames at 44100 Hz before padding */
} AudioModVag;

/* The replacement of sample `vag` (1-based, as in vag_NNN.wav) of `bank` from the last mod that has a usable file: 1 and *out
   filled, or 0 (no file, or only refused ones). */
int audioModVagReplacement(const char *bank, int vag, AudioModVag *out);

/* ---- U7: PCM voices (replaced samples without SPU RAM) ---- */

/* Where a replaced sample plays: OpenAL by default (one buffer per sample, one source per voice, the gain/pan/pitch arithmetic of
   PsyX_SPUAL's voices), or a test's. Ids are > 0; 0 is a failure. Volumes are the SPU's (16384 = full). */
typedef struct AudioModPcmBackend {
    int (*upload)(void *ctx, const short *pcm, unsigned long frames, long loopStart); /* 44100 Hz mono; loopStart < 0: plays once */
    void (*release)(void *ctx, int id);                                              /* also stops the voices playing it */
    void (*play)(void *ctx, int voice, int id, int loops, unsigned short pitch, short volL, short volR);
    void (*volume)(void *ctx, int voice, short volL, short volR);
    void (*pitch)(void *ctx, int voice, unsigned short pitch);                       /* 0x1000 = 44100 Hz */
    void (*stop)(void *ctx, int voice);
} AudioModPcmBackend;

void audioModSetPcmBackend(const AudioModPcmBackend *backend, void *ctx); /* NULL: OpenAL again */
/* Decodes a replacement's ADPCM (the SPU's own decoder, vag_encode.c) and hands it to the output: an id, or 0 (no output). */
int audioModPcmBuffer(const unsigned char *adpcm, unsigned long size, int loops);
void audioModPcmRelease(int buf);
void audioModPcmPlay(int voice, int buf, int loops, unsigned short pitch, short volL, short volR);
int audioModPcmActive(int voice);             /* a voice that was started here and not stopped */
void audioModPcmVolume(int voice, short volL, short volR);
void audioModPcmPitch(int voice, unsigned short pitch);
int audioModPcmStop(int voice);               /* 1 when it was one of ours */
void audioModPcmStopAll(void);


/* ---- E3d: the songs ---- */

/* Samples of a file that are loaded whole (8 MB: about 47 s of 44100 Hz stereo); more is streamed from the disk */
#define AUDIO_MOD_MEM_BYTES (8UL * 1024UL * 1024UL)
#define AUDIO_MOD_CHUNK 4096 /* frames fed to the output at a time */

/* Where the sound goes: the OpenAL streaming sink by default (DW3_SND_MUTE has no sink: the game's sequence plays, silent), or a test's. */
typedef struct AudioModSink {
    int (*open)(void *ctx, int channels, int rate);        /* a stream id >= 0, or -1 */
    int (*room)(void *ctx, int id);                         /* frames it can take now */
    void (*queue)(void *ctx, int id, const short *pcm, int frames);
    int (*queued)(void *ctx, int id);                       /* frames taken and not yet played */
    void (*setGain)(void *ctx, int id, int permille);       /* 0..1000 */
    void (*pause)(void *ctx, int id, int on);
    void (*close)(void *ctx, int id);
} AudioModSink;

void audioModSetSink(const AudioModSink *sink, void *ctx); /* NULL: OpenAL again */
void audioModSetMemLimit(unsigned long bytes);             /* 0: AUDIO_MOD_MEM_BYTES */

/* A track for sequence pkg/rec of `bank`, from the last mod that has a usable file. loopForever: the original loops without end (the whole
   file then loops when the file says nothing). music: the music slider applies, else the effects one. permille: the game's level now
   (0..1000). Returns a handle (> 0), or 0 when no file, only refused ones, or no output: the caller plays its own sequence. */
int audioModTrackStart(const char *bank, int pkg, int rec, int loopForever, int music, int permille);
void audioModTrackLevel(int handle, int permille);  /* the game's volume (SetVol, fade, master), 0..1000 */
void audioModTrackPause(int handle, int on);
void audioModTrackStop(int handle);
int audioModTrackActive(int handle);                /* 0 once it stopped or played to its end */
/* Feeds every track's output and finds the ends; the vsync hook calls it */
void audioModPump(void);

/* --validate-mod (U5): every audio file of one mod through the loaders above, with a sink that plays nothing. Refusals are logged as
   always (`[audio] <mod>: <file> rejected: <why>`); files the port never reads are logged as ignored. Returns how many files were
   checked. Leaves the module as it found it (the mod list, the sink). */
int audioModValidate(const char *modDir, const char *modName);

#endif
