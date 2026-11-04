/* PlayStation SPU ADPCM encoder (content-export E3c); see vag_encode.h */
#include "vag_encode.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The filters of the port's SPU renderer (PsyX_SPUAL.cpp: K0 and K1) */
static const float kK0[5] = {0.0f, 0.9375f, 1.796875f, 1.53125f, 1.90625f};
static const float kK1[5] = {0.0f, 0.0f, -0.8125f, -0.859375f, -0.9375f};

static int16_t toPcm(float v) {
    int r = (int)roundf(v);

    return (int16_t)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
}

/* One decoded sample from a residual, as PsyX_SPUAL's vagToPcm does it (state kept unclamped) */
static float step(int filter, int shift, int nibble, float *p1, float *p2) {
    float v = (float)nibble * (float)(1 << (12 - shift)) + *p1 * kK0[filter] + *p2 * kK1[filter];

    *p2 = *p1;
    *p1 = v;
    return v;
}

size_t vagDecodeBlocks(const uint8_t *vag, size_t size, int16_t *out) {
    float p1 = 0, p2 = 0;
    size_t b, n = 0;

    for (b = 0; b + VAG_BLOCK_BYTES <= size; b += VAG_BLOCK_BYTES) {
        int filter = (vag[b] >> 4) & 15, shift = vag[b] & 15, i;

        if (filter > 4) filter = 4;
        if (shift > 12) shift = 12;
        for (i = 0; i < 14; i++) {
            int lo = vag[b + 2 + i] & 15, hi = vag[b + 2 + i] >> 4;

            out[n++] = toPcm(step(filter, shift, lo > 7 ? lo - 16 : lo, &p1, &p2));
            out[n++] = toPcm(step(filter, shift, hi > 7 ? hi - 16 : hi, &p1, &p2));
        }
        if (vag[b + 1] & 1) break;
    }
    return n;
}

/* Encodes one block of 28 samples with a filter and shift from the state (p1, p2): the residuals into nib, the squared error of
   what the decoder will output, the state after it in (*o1, *o2) */
static double tryBlock(const int16_t *in, int filter, int shift, float p1, float p2, int *nib, float *o1, float *o2) {
    double err = 0;
    int i;
    float scale = (float)(1 << (12 - shift));

    for (i = 0; i < VAG_BLOCK_SAMPLES; i++) {
        float pred = p1 * kK0[filter] + p2 * kK1[filter];
        int q = (int)lroundf(((float)in[i] - pred) / scale);
        float v;
        double d;

        if (q > 7) q = 7;
        if (q < -8) q = -8;
        nib[i] = q;
        v = (float)q * scale + pred;
        d = (double)in[i] - (double)toPcm(v);
        err += d * d;
        p2 = p1;
        p1 = v;
    }
    *o1 = p1;
    *o2 = p2;
    return err;
}

uint8_t *vagEncode(const int16_t *pcm, size_t frames, long loopStart, size_t *size) {
    size_t blocks, b, loopBlock = 0, loopFrom = 0, loopLen = 0;
    uint8_t *out;
    float p1 = 0, p2 = 0;
    int16_t blk[VAG_BLOCK_SAMPLES];

    if (pcm == NULL || frames == 0 || size == NULL) return NULL;
    if (loopStart >= 0) {
        if ((size_t)loopStart >= frames) return NULL;
        loopBlock = (size_t)loopStart / VAG_BLOCK_SAMPLES;
        loopFrom = (size_t)loopStart;
        loopLen = frames - loopFrom;
    }
    blocks = (frames + VAG_BLOCK_SAMPLES - 1) / VAG_BLOCK_SAMPLES;
    if (blocks > 0x7FFFFFFFu / VAG_BLOCK_BYTES) return NULL;
    out = (uint8_t *)calloc(blocks, VAG_BLOCK_BYTES);
    if (out == NULL) return NULL;
    for (b = 0; b < blocks; b++) {
        double best = -1;
        int bf = 0, bs = 0, bn[VAG_BLOCK_SAMPLES], nib[VAG_BLOCK_SAMPLES], f, s, i;
        float b1 = 0, b2 = 0, n1, n2;
        uint8_t *o = out + b * VAG_BLOCK_BYTES;

        for (i = 0; i < VAG_BLOCK_SAMPLES; i++) {
            size_t k = b * VAG_BLOCK_SAMPLES + (size_t)i;

            if (k < frames) blk[i] = pcm[k];
            else if (loopLen > 0) blk[i] = pcm[loopFrom + (k - frames) % loopLen]; /* the loop goes on past its end */
            else blk[i] = 0;
        }
        for (f = 0; f < 5; f++) {
            for (s = 0; s <= 12; s++) {
                double e = tryBlock(blk, f, s, p1, p2, nib, &n1, &n2);

                if (best < 0 || e < best) {
                    best = e;
                    bf = f;
                    bs = s;
                    b1 = n1;
                    b2 = n2;
                    memcpy(bn, nib, sizeof bn);
                }
            }
        }
        o[0] = (uint8_t)(bf << 4 | bs);
        o[1] = 0;
        for (i = 0; i < 14; i++) o[2 + i] = (uint8_t)((bn[i * 2] & 15) | ((bn[i * 2 + 1] & 15) << 4));
        p1 = b1;
        p2 = b2;
    }
    if (loopStart >= 0) {
        out[loopBlock * VAG_BLOCK_BYTES + 1] |= 4;
        out[(blocks - 1) * VAG_BLOCK_BYTES + 1] |= 3;
    } else {
        out[(blocks - 1) * VAG_BLOCK_BYTES + 1] |= 1;
    }
    *size = blocks * VAG_BLOCK_BYTES;
    return out;
}
