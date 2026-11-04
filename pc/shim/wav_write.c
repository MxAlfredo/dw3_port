/* A WAV file in memory, with the sampler chunk (content-export E3a); see wav_write.h */
#include "wav_write.h"

#include <stdlib.h>
#include <string.h>

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

#define SMPL_BYTES (36 + 24) /* the fixed part of the chunk and one loop */

uint8_t *wavBuild(const int16_t *pcm, size_t frames, int rate, const WavSmpl *smpl, size_t *len) {
    size_t dataBytes = frames * 2, total, i;
    size_t smplBytes = 0;
    uint8_t *out, *p;

    if (rate <= 0 || (frames > 0 && pcm == NULL) || frames > 0x40000000u / 2 || len == NULL) return NULL;
    if (smpl != NULL) smplBytes = 8 + (smpl->hasLoop ? SMPL_BYTES : 36);
    total = 12 + 8 + 16 + 8 + dataBytes + smplBytes;
    out = (uint8_t *)calloc(1, total);
    if (out == NULL) return NULL;
    p = out;
    memcpy(p, "RIFF", 4);
    put32(p + 4, (uint32_t)(total - 8));
    memcpy(p + 8, "WAVE", 4);
    p += 12;
    memcpy(p, "fmt ", 4);
    put32(p + 4, 16);
    put16(p + 8, 1);                       /* PCM */
    put16(p + 10, 1);                      /* mono */
    put32(p + 12, (uint32_t)rate);
    put32(p + 16, (uint32_t)rate * 2);     /* bytes a second */
    put16(p + 20, 2);                      /* block align */
    put16(p + 22, 16);                     /* bits */
    p += 24;
    memcpy(p, "data", 4);
    put32(p + 4, (uint32_t)dataBytes);
    p += 8;
    for (i = 0; i < frames; i++) put16(p + i * 2, (uint16_t)pcm[i]);
    p += dataBytes;
    if (smpl != NULL) {
        size_t body = smplBytes - 8;
        memcpy(p, "smpl", 4);
        put32(p + 4, (uint32_t)body);
        /* manufacturer 0, product 0, sample period in nanoseconds */
        put32(p + 16, (uint32_t)(1000000000u / (uint32_t)rate));
        put32(p + 20, (uint32_t)smpl->rootNote);
        put32(p + 24, smpl->fraction);
        /* SMPTE format and offset 0 */
        put32(p + 36, smpl->hasLoop ? 1u : 0u);
        /* sampler data 0 */
        if (smpl->hasLoop) {
            /* cue point id 0, type 0 (forward), start, end, fraction 0, play count 0 (forever) */
            put32(p + 52, smpl->loopStart);
            put32(p + 56, smpl->loopEnd);
        }
    }
    *len = total;
    return out;
}
