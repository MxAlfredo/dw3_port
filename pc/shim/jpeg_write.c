/* A small baseline JPEG encoder; see jpeg_write.h for the profile it writes (content-export E2a) */
#include "jpeg_write.h"

#include <math.h>
#include <string.h>

/* ITU T.81 Annex K.1: the example quantisation tables, natural order */
static const uint8_t kLumQ[64] = {
    16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,  14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,  49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
static const uint8_t kChrQ[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

static const uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

/* Annex K.3: the standard Huffman tables, as the DHT segment holds them (counts per code length, then the symbols) */
static const uint8_t kDcLumBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t kDcChrBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t kDcVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t kAcLumBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const uint8_t kAcLumVals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1,
    0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85,
    0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
    0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
    0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
    0xfa};
static const uint8_t kAcChrBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const uint8_t kAcChrVals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42,
    0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19,
    0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55,
    0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
    0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
    0xfa};

/* A code table indexed by symbol: the code and its length in bits (0: the symbol has none) */
typedef struct HuffCodes {
    uint16_t code[256];
    uint8_t len[256];
} HuffCodes;

static void buildCodes(HuffCodes *h, const uint8_t *bits, const uint8_t *vals) {
    unsigned code = 0;
    int i, k = 0, l;
    memset(h, 0, sizeof *h);
    for (l = 1; l <= 16; l++) {
        for (i = 0; i < bits[l - 1]; i++) {
            h->code[vals[k]] = (uint16_t)code;
            h->len[vals[k]] = (uint8_t)l;
            code++;
            k++;
        }
        code <<= 1;
    }
}

typedef struct Out {
    uint8_t *p;
    size_t n, cap;
    int bad;
    uint32_t acc; /* bit buffer: the pending bits are the low `nbits` of acc */
    int nbits;
} Out;

static void putByte(Out *o, unsigned v) {
    if (o->n >= o->cap) {
        o->bad = 1;
        return;
    }
    o->p[o->n++] = (uint8_t)v;
}

static void put16(Out *o, unsigned v) {
    putByte(o, v >> 8);
    putByte(o, v & 0xFF);
}

static void putBits(Out *o, unsigned code, int len) {
    o->acc = (o->acc << len) | (code & ((1u << len) - 1u));
    o->nbits += len;
    while (o->nbits >= 8) {
        unsigned b = (o->acc >> (o->nbits - 8)) & 0xFF;
        putByte(o, b);
        if (b == 0xFF) putByte(o, 0); /* a data byte of 0xFF is followed by a zero so it is not a marker */
        o->nbits -= 8;
    }
}

static void flushBits(Out *o) {
    if (o->nbits > 0) putBits(o, 0x7F, 8 - o->nbits); /* pad with ones */
    o->acc = 0;
    o->nbits = 0;
}

static void putDqt(Out *o, int id, const uint8_t *q) {
    int i;
    put16(o, 0xFFDB);
    put16(o, 67);
    putByte(o, id);
    for (i = 0; i < 64; i++) putByte(o, q[kZigzag[i]]);
}

static void putDht(Out *o, int cls, int id, const uint8_t *bits, const uint8_t *vals, int nvals) {
    int i;
    put16(o, 0xFFC4);
    put16(o, 19 + nvals);
    putByte(o, (cls << 4) | id);
    for (i = 0; i < 16; i++) putByte(o, bits[i]);
    for (i = 0; i < nvals; i++) putByte(o, vals[i]);
}

typedef struct Enc {
    float cosm[8][8];   /* cosm[u][x] = C(u)/2 * cos((2x+1) u pi / 16) */
    uint8_t q[2][64];   /* quantiser per table, natural order */
    HuffCodes dc[2], ac[2];
} Enc;

/* One 8x8 block (level shifted samples, row major) to the entropy stream; *pred is the component's DC predictor */
static void encodeBlock(Out *o, const Enc *e, const float *blk, int table, int *pred) {
    float tmp[64], dct[64];
    int coef[64], u, v, x, k, diff, last, size, run;
    const HuffCodes *dc = &e->dc[table], *ac = &e->ac[table];
    const uint8_t *q = e->q[table];

    /* the 2D DCT as two passes of the 8x8 matrix: tmp = rows transformed, dct = columns transformed */
    for (v = 0; v < 8; v++) {
        for (u = 0; u < 8; u++) {
            float s = 0.0f;
            for (x = 0; x < 8; x++) s += blk[v * 8 + x] * e->cosm[u][x];
            tmp[v * 8 + u] = s;
        }
    }
    for (u = 0; u < 8; u++) {
        for (v = 0; v < 8; v++) {
            float s = 0.0f;
            for (x = 0; x < 8; x++) s += tmp[x * 8 + u] * e->cosm[v][x];
            dct[v * 8 + u] = s;
        }
    }
    for (k = 0; k < 64; k++) {
        float f = dct[k] / (float)q[k];
        coef[k] = (int)(f >= 0.0f ? f + 0.5f : f - 0.5f);
    }

    diff = coef[0] - *pred;
    *pred = coef[0];
    {
        int mag = diff < 0 ? -diff : diff;
        size = 0;
        while (mag) {
            size++;
            mag >>= 1;
        }
        putBits(o, dc->code[size], dc->len[size]);
        if (size) putBits(o, (unsigned)(diff < 0 ? diff - 1 : diff), size);
    }
    last = 0;
    for (k = 63; k > 0; k--) {
        if (coef[kZigzag[k]] != 0) {
            last = k;
            break;
        }
    }
    run = 0;
    for (k = 1; k <= last; k++) {
        int c = coef[kZigzag[k]];
        if (c == 0) {
            run++;
            continue;
        }
        while (run > 15) {
            putBits(o, ac->code[0xF0], ac->len[0xF0]);
            run -= 16;
        }
        {
            int mag = c < 0 ? -c : c, sym;
            size = 0;
            while (mag) {
                size++;
                mag >>= 1;
            }
            sym = (run << 4) | size;
            putBits(o, ac->code[sym], ac->len[sym]);
            putBits(o, (unsigned)(c < 0 ? c - 1 : c), size);
        }
        run = 0;
    }
    if (last < 63) putBits(o, ac->code[0x00], ac->len[0x00]);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

size_t jpegMaxSize(int w, int h) {
    size_t mcus, blocks;
    if (w < 1 || h < 1 || w > 65535 || h > 65535) return 0;
    mcus = (size_t)((w + 15) / 16) * (size_t)((h + 15) / 16);
    blocks = mcus * 6;
    /* a block cannot take more than 64 coefficients of (16 + 10) bits plus stuffing; headers are under 700 bytes */
    return blocks * 256 + 1024;
}

size_t jpegEncodeRgb24(const uint8_t *rgb, int w, int h, int quality, uint8_t *out, size_t cap) {
    Enc e;
    Out o;
    int mcx, mcy, mx, my, i, j, t, scale, predY = 0, predCb = 0, predCr = 0;

    if (rgb == NULL || out == NULL || w < 1 || h < 1 || w > 65535 || h > 65535 || quality < 1 || quality > 100) return 0;

    scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    for (i = 0; i < 64; i++) {
        e.q[0][i] = (uint8_t)clampi((kLumQ[i] * scale + 50) / 100, 1, 255);
        e.q[1][i] = (uint8_t)clampi((kChrQ[i] * scale + 50) / 100, 1, 255);
    }
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            double cu = i == 0 ? 0.70710678118654752440 : 1.0;
            e.cosm[i][j] = (float)(0.5 * cu * cos((double)((2 * j + 1) * i) * 3.14159265358979323846 / 16.0));
        }
    }
    buildCodes(&e.dc[0], kDcLumBits, kDcVals);
    buildCodes(&e.dc[1], kDcChrBits, kDcVals);
    buildCodes(&e.ac[0], kAcLumBits, kAcLumVals);
    buildCodes(&e.ac[1], kAcChrBits, kAcChrVals);

    o.p = out;
    o.n = 0;
    o.cap = cap;
    o.bad = 0;
    o.acc = 0;
    o.nbits = 0;

    put16(&o, 0xFFD8);
    /* JFIF 1.01, no density unit, 1:1 */
    put16(&o, 0xFFE0);
    put16(&o, 16);
    for (i = 0; i < 5; i++) putByte(&o, "JFIF"[i]);
    putByte(&o, 1);
    putByte(&o, 1);
    putByte(&o, 0);
    put16(&o, 1);
    put16(&o, 1);
    putByte(&o, 0);
    putByte(&o, 0);
    putDqt(&o, 0, e.q[0]);
    putDqt(&o, 1, e.q[1]);
    put16(&o, 0xFFC0);
    put16(&o, 17);
    putByte(&o, 8);
    put16(&o, (unsigned)h);
    put16(&o, (unsigned)w);
    putByte(&o, 3);
    putByte(&o, 1); putByte(&o, 0x22); putByte(&o, 0); /* Y: 2x2 */
    putByte(&o, 2); putByte(&o, 0x11); putByte(&o, 1); /* Cb */
    putByte(&o, 3); putByte(&o, 0x11); putByte(&o, 1); /* Cr */
    putDht(&o, 0, 0, kDcLumBits, kDcVals, 12);
    putDht(&o, 1, 0, kAcLumBits, kAcLumVals, 162);
    putDht(&o, 0, 1, kDcChrBits, kDcVals, 12);
    putDht(&o, 1, 1, kAcChrBits, kAcChrVals, 162);
    put16(&o, 0xFFDA);
    put16(&o, 12);
    putByte(&o, 3);
    putByte(&o, 1); putByte(&o, 0x00);
    putByte(&o, 2); putByte(&o, 0x11);
    putByte(&o, 3); putByte(&o, 0x11);
    putByte(&o, 0);
    putByte(&o, 63);
    putByte(&o, 0);

    mcx = (w + 15) / 16;
    mcy = (h + 15) / 16;
    for (my = 0; my < mcy; my++) {
        for (mx = 0; mx < mcx; mx++) {
            float yb[4][64], cb[64], cr[64];
            for (j = 0; j < 8; j++) {
                for (i = 0; i < 8; i++) {
                    int sumCb = 0, sumCr = 0, dy, dx;
                    for (dy = 0; dy < 2; dy++) {
                        for (dx = 0; dx < 2; dx++) {
                            int px = clampi(mx * 16 + i * 2 + dx, 0, w - 1), py = clampi(my * 16 + j * 2 + dy, 0, h - 1);
                            const uint8_t *p = rgb + ((size_t)py * (size_t)w + (size_t)px) * 3;
                            sumCb += -11059 * p[0] - 21709 * p[1] + 32768 * p[2];
                            sumCr += 32768 * p[0] - 27439 * p[1] - 5329 * p[2];
                        }
                    }
                    /* the average of four pixels, 16 bit fixed point; the +128 of the chroma and the level shift cancel */
                    cb[j * 8 + i] = (float)sumCb / 262144.0f;
                    cr[j * 8 + i] = (float)sumCr / 262144.0f;
                }
            }
            for (t = 0; t < 4; t++) {
                int bx = (t & 1) * 8, by = (t >> 1) * 8;
                for (j = 0; j < 8; j++) {
                    for (i = 0; i < 8; i++) {
                        int px = clampi(mx * 16 + bx + i, 0, w - 1), py = clampi(my * 16 + by + j, 0, h - 1);
                        const uint8_t *p = rgb + ((size_t)py * (size_t)w + (size_t)px) * 3;
                        int y = 19595 * p[0] + 38470 * p[1] + 7471 * p[2]; /* 0.299 0.587 0.114 in 16 bit fixed point */
                        yb[t][j * 8 + i] = (float)y / 65536.0f - 128.0f;
                    }
                }
            }
            for (t = 0; t < 4; t++) encodeBlock(&o, &e, yb[t], 0, &predY);
            encodeBlock(&o, &e, cb, 1, &predCb);
            encodeBlock(&o, &e, cr, 1, &predCr);
        }
    }
    flushBits(&o);
    put16(&o, 0xFFD9);
    return o.bad ? 0 : o.n;
}
