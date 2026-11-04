#ifndef PC_WAV_WRITE_H
#define PC_WAV_WRITE_H

/*
 * A WAV file in memory (content-export E3a): RIFF/WAVE, one "fmt " chunk (PCM, mono, 16-bit), the "data" chunk and, when the
 * sample has a root note or a loop, a "smpl" chunk after them (the sampler chunk samplers, DAWs and wave editors read:
 * MIDIUnityNote, MIDIPitchFraction and the loops). All integers are little endian. Pure: no files, no game state.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct WavSmpl {
    int rootNote;      /* MIDIUnityNote, 0..127 */
    uint32_t fraction; /* MIDIPitchFraction: how far above rootNote the sample is tuned, in 1/2^32 of a semitone */
    int hasLoop;
    uint32_t loopStart, loopEnd; /* sample frames; loopEnd is the last frame of the loop (inclusive) */
} WavSmpl;

/* The whole file as malloc'ed bytes (the caller frees them), *len set; NULL when out of memory or the arguments are not valid.
   smpl NULL: no smpl chunk. */
uint8_t *wavBuild(const int16_t *pcm, size_t frames, int rate, const WavSmpl *smpl, size_t *len);

#endif
