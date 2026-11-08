#ifndef PC_AUDIO_EXPORT_H
#define PC_AUDIO_EXPORT_H

/*
 * The game's sound banks as open files (content-export E3a/E3b, ADR-005 / ADR-010). Each of the 71 banks (AAA/DAT/SOUND/<NAME>/) is
 * a head archive (MP<NAME>.BIN: the VAB header and the SEP sequence packages) and a body archive (MV<NAME>.BIN: the VAG samples);
 * the game's own table (SOUND_BANK_FILES) says which entry is which. Each bank is written to
 *
 *   <data>/audio/<NAME>/vag_NNN.wav     one WAV per VAG sample (mono, 16-bit, 44100 Hz) with a smpl chunk: the loop and the root note
 *   <data>/audio/<NAME>/bank.json       "bank-v1": the programs, their tones and the samples (range, root note, volume, pan, ADSR, bend)
 *   <data>/audio/<NAME>/seq_P_RR.mid    one Standard MIDI File (format 1) per SEP record: package P, record RR
 *   <data>/audio/<NAME>/sequences.json  "sequences-v1": each file's game ids and its numbers
 *
 * so a modder can open the samples in any editor and the sequences in any DAW. The formats and the rules behind them are in
 * docs/formats.md and docs/modding. The decoders are the port's own: the VH and SEP parsers of snd_spu.c and snd_sep.c (so what is
 * written is what the game plays, the same record numbering included) and, for the ADPCM, the same arithmetic as the SPU renderer
 * the game's sound goes through (PsyX_SPUAL's decodeSound).
 *
 * The disc is read through the reader the caller hands in (boot.c gives it its own file handle): nothing of the game's sound state
 * (the loaded banks, the SPU, the sequencer) is touched, so a song that is playing is never disturbed.
 *
 * On demand only: the System tab's "Export audio" row and the [data] export_audio key of settings.ini ("all" or bank names
 * separated by commas). The menu's job runs on a worker thread (audioExportStart); the settings.ini key at start runs synchronously.
 * Every file follows the rule of the other data/ families: a manifest of what the port wrote (data/audio/manifest.json), an edited
 * file stays and the new one goes beside it as <file>.new. The manifest also holds, per bank, the exporter version that wrote it:
 * a request skips a bank of the current version (an edited file counts); the menu row forces a rebuild of all. Bump
 * AUDIO_EXPORT_VERSION when the output bytes change. Compile with -ffp-contract=off (the ADPCM filter is float).
 */

#include <stddef.h>
#include <stdint.h>

#define AUDIO_EXPORT_VERSION 1
#define AUDIO_EXPORT_RATE 44100 /* the rate a VAG plays at when its note is the root note (SPU pitch 0x1000) */
#define AUDIO_EXPORT_MAX_SEPS 8

/* One bank, as the game's SoundFiles says it (include/engine/sound.h) */
typedef struct AudioExportBank {
    int id;            /* the bank id of the sound ids (bits 18-24), 1..71 */
    char name[24];     /* the folder of the bank on the disc, "BGM001" */
    int headFile;      /* the file number of the head archive (MP<NAME>.BIN) */
    int bodyFile;      /* the file number of the body archive (MV<NAME>.BIN) */
    int vhIndex;       /* the VAB header's entry in headFile (low 16 bits of SoundFiles.vhIndex) */
    int bodyIndex;     /* the body's entry in bodyFile (low 16 bits of SoundFiles.bodyEntry) */
    int sepCount;      /* how many SEP packages */
    int seps[AUDIO_EXPORT_MAX_SEPS]; /* their entries in headFile, in the game's SsSepOpen order (the SEQ number of a sound id) */
} AudioExportBank;

/* Where the banks are and how to read the disc. file is only called by the thread that runs the job. */
typedef struct AudioExportIo {
    /* bank i (0, 1, 2...) into *out; 1 while there is one, 0 past the last */
    int (*bank)(void *ud, int i, AudioExportBank *out);
    /* a whole disc file by number as malloc'ed bytes (the caller frees them); 1 when read */
    int (*file)(void *ud, int file, uint8_t **bytes, size_t *len);
    void *ud;
} AudioExportIo;

typedef struct AudioExportStats {
    int vags;       /* WAV files */
    int programs;
    int tones;
    int packages;   /* SEP packages read */
    int sequences;  /* MID files */
    int loopingSequences;
    int loopingVags;
    long bytes;     /* all the bytes of the files written */
} AudioExportStats;

/* ---- the pieces, for the tests ---- */

typedef struct AudioVagInfo {
    size_t frames;     /* PCM samples decoded */
    int ends;          /* 0: no end flag (all the blocks decoded), 1: the sample stops, 2: it loops */
    size_t loopStart;  /* ends == 2: first frame of the loop (the block with the loop-start flag, 0 without one) */
    size_t loopEnd;    /* ends == 2: last frame of the loop */
} AudioVagInfo;

/* Decodes a VAG (16-byte ADPCM blocks of 28 samples) into out, which holds (size / 16) * 28 samples: up to and including the first
   block with the end flag (the SPU repeats or mutes there), else all the blocks. Returns the frame count. */
size_t audioVagDecode(const uint8_t *vag, size_t size, int16_t *out, AudioVagInfo *info);

/* Where one file of a bank goes. 0 to go on, nonzero to give up (the build then fails). */
typedef int (*AudioExportPut)(void *ud, const char *rel, const void *data, size_t len);

/* One bank to files, now, no manifest: every file is handed to put as <rel> ("vag_001.wav") and its bytes. 0, or -1 with the
   reason in err. */
int audioExportBuild(const AudioExportIo *io, const AudioExportBank *bank, AudioExportPut put, void *putUd, AudioExportStats *st,
                     char *err, size_t errCap);

/* ---- the job: where the files go and which banks ---- */

/* The data folder (<data>/audio/...) and the disc reader. Call again to change them; an unfinished job is dropped */
void audioExportSetup(const char *dataDir, const AudioExportIo *io);
/* Queues "all" or bank names separated by commas/spaces (any case). The queued count, or -1 when there is nothing to do (no data
   folder, no reader, nothing valid, a job is running). */
int audioExportRequest(const char *spec);
/* The same; force != 0 rebuilds every queued bank even when it is up to date */
int audioExportRequestEx(const char *spec, int force);
/* How many banks of the last job were really built (the rest were skipped as up to date) */
int audioExportBuilt(void);
/* The settings.ini key: audioExportRequest, then builds the queue now, on this thread. The queued count, or -1. */
int audioExportRun(const char *spec);
/* The menu's job: audioExportRequestEx, then a worker thread builds and writes the queue. Then call audioExportPump every frame */
int audioExportStart(const char *spec, int force);
/* Stops a running worker between two files (a bank in progress is left as it is, its manifest entry not written) and joins it;
   atexit-safe */
void audioExportShutdown(void);
/* Exports queued banks for about budgetMs milliseconds (at least one bank) when there is no worker; with one it only joins it at
   the end. 1 while work remains, 0 when the job is over. */
int audioExportPump(unsigned budgetMs);
int audioExportActive(void);
/* done and failed so far, how many were queued */
void audioExportCounts(int *done, int *failed, int *total);
/* The totals of the banks built by the last job (the skipped ones add nothing) */
void audioExportTotals(AudioExportStats *st);
/* The row's value text: "" before any job, "Exporting 3/71", "Done (71)", "Done, 2 failed" */
const char *audioExportStatus(void);

#endif
