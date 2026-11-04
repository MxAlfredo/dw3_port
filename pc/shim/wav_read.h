#ifndef PC_WAV_READ_H
#define PC_WAV_READ_H

/*
 * A WAV file from a mod (content-export E3c/E3d, ADR-005 / ADR-010). The accepted profile is the one wave editors and DAWs write by
 * default: RIFF/WAVE, one "fmt " chunk of 16-bit integer PCM (format 1, or WAVE_FORMAT_EXTENSIBLE carrying PCM), mono or stereo,
 * any rate from 1000 to 384000 Hz, a "data" chunk and optionally a "smpl" chunk (root note and the first loop; loopEnd is the
 * last frame of the loop, as the exporter writes it). Unknown chunks are skipped; a data chunk longer than the file (a recorder
 * that was cut, or a size of 0xFFFFFFFF) is clamped to what is there. Anything else is refused with a reason in err:
 * "not a WAV file", "not 16-bit PCM (...)", "more than 2 channels", "no data", ...
 *
 * Plain stdio, no game state: the header is parsed from a FILE so that a long song can be streamed from disk afterwards.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct WavInfo {
    int channels;            /* 1 or 2 */
    int rate;                /* Hz */
    unsigned long frames;    /* sample frames in the data chunk */
    long dataOfs;            /* where the samples start in the file */
    int hasSmpl;             /* a smpl chunk was there */
    int rootNote;            /* its MIDIUnityNote (hasSmpl) */
    int hasLoop;             /* the smpl chunk holds a loop that fits inside the data */
    unsigned long loopStart; /* first frame of the loop */
    unsigned long loopEnd;   /* last frame of the loop (inclusive) */
} WavInfo;

/* Parses the headers of the file `f` (from its start). 0 when it is usable, else -1 with the reason in err. */
int wavInfoRead(FILE *f, WavInfo *info, char *err, size_t errCap);

/* Up to n sample frames from frame `first`, interleaved, into out (channels * n samples). Returns the frames read (less than n
   only at the end of the data or on a read error). */
size_t wavReadFrames(FILE *f, const WavInfo *info, unsigned long first, int16_t *out, size_t n);

/* The whole file: malloc'ed interleaved samples (the caller frees them), *info filled; NULL with the reason in err. */
int16_t *wavLoad(const char *path, WavInfo *info, char *err, size_t errCap);

/* Mono at another rate: the channels are averaged, then resampled (linear interpolation up, a box average down). The frame count
   is rounded so that the duration is kept. malloc'ed (the caller frees it), *outFrames set; NULL when out of memory or arguments
   are not valid. from == to only mixes down. */
int16_t *wavResampleMono(const int16_t *pcm, size_t frames, int channels, int from, int to, size_t *outFrames);

#endif
