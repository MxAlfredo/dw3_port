/* A small baseline JPEG decoder; see jpeg_read.h for what it accepts (content-export E2b) */
#include "jpeg_read.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ITU T.81 Annex K.3: the standard Huffman tables, used when a frame carries no DHT (the quantisation tables are never defaulted:
   a frame without them is broken). The zigzag order is the file's order of the 64 coefficients. */
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

#define MAX_COMP 3

typedef struct Huff {
    int defined;
    uint8_t bits[17];
    uint8_t vals[256];
    int mincode[17];
    int maxcode[18]; /* -1: no code of that length */
    int valptr[17];
} Huff;

typedef struct Comp {
    int id, h, v, tq, td, ta;
    int pw, ph; /* the plane's size in samples: whole MCUs */
    uint8_t *plane;
} Comp;

typedef struct Bits {
    const uint8_t *p, *end;
    uint32_t buf;
    int cnt;
    int marker; /* the marker byte the data ran into, 0 while none */
    int pad;    /* bits supplied beyond the data (zeros) */
} Bits;

typedef struct Dec {
    const uint8_t *data;
    size_t len;
    char *err;
    size_t errCap;
    int w, h, nc;
    int hmax, vmax;
    Comp comp[MAX_COMP];
    uint16_t qt[4][64]; /* natural order */
    int qdef[4];
    Huff dc[4], ac[4];
    int ri;
    int jfif, adobe, adobeTransform;
    int mcux, mcuy;
} Dec;

static int fail(Dec *d, const char *msg) {
    if (d->err != NULL && d->errCap > 0) {
        snprintf(d->err, d->errCap, "%s", msg);
    }
    return -1;
}

/* --- Huffman ------------------------------------------------------------- */

static int buildHuff(Huff *h, const uint8_t *bits, const uint8_t *vals, int nvals) {
    int l, k = 0, code = 0;

    if (nvals < 0 || nvals > 256) return -1;
    memset(h, 0, sizeof *h);
    memcpy(h->vals, vals, (size_t)nvals);
    for (l = 1; l <= 16; l++) {
        h->bits[l] = bits[l - 1];
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        if (k > nvals || code > (1 << l)) return -1; /* more codes than symbols, or not a prefix code */
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
    h->defined = 1;
    return 0;
}

static void defaultTables(Dec *d) {
    if (!d->dc[0].defined) buildHuff(&d->dc[0], kDcLumBits, kDcVals, 12);
    if (!d->dc[1].defined) buildHuff(&d->dc[1], kDcChrBits, kDcVals, 12);
    if (!d->ac[0].defined) buildHuff(&d->ac[0], kAcLumBits, kAcLumVals, 162);
    if (!d->ac[1].defined) buildHuff(&d->ac[1], kAcChrBits, kAcChrVals, 162);
}

/* --- the entropy-coded data ---------------------------------------------- */

static void bitsFill(Bits *b) {
    while (b->cnt <= 24) {
        unsigned v = 0;

        if (b->marker || b->p >= b->end) {
            b->pad += 8;
        } else {
            v = *b->p++;
            if (v == 0xFF) {
                while (b->p < b->end && *b->p == 0xFF) b->p++; /* fill bytes */
                if (b->p >= b->end) {
                    v = 0;
                    b->pad += 8;
                } else if (*b->p == 0) {
                    b->p++; /* a data byte of 0xFF */
                } else {
                    b->marker = *b->p++;
                    v = 0;
                    b->pad += 8;
                }
            }
        }
        b->buf |= (uint32_t)v << (24 - b->cnt);
        b->cnt += 8;
    }
}

static unsigned getBits(Bits *b, int n) {
    unsigned v;

    if (n <= 0) return 0;
    if (b->cnt < n) bitsFill(b);
    v = b->buf >> (32 - n);
    b->buf <<= n;
    b->cnt -= n;
    return v;
}

/* The symbol of the next code, or -1 when there is none (a code of no length, or the data ran out for good) */
static int huffDecode(Bits *b, const Huff *h) {
    int code = 0, l;

    for (l = 1; l <= 16; l++) {
        code = (code << 1) | (int)getBits(b, 1);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) {
            return h->vals[h->valptr[l] + code - h->mincode[l]];
        }
    }
    return -1;
}

static int extend(int v, int s) {
    return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
}

/* --- the inverse DCT ------------------------------------------------------ */

static float gCos[8][8]; /* [u][x] = C(u) / 2 * cos((2x + 1) u pi / 16) */
static int gCosReady;

static void idctInit(void) {
    int u, x;

    if (gCosReady) return;
    for (u = 0; u < 8; u++) {
        for (x = 0; x < 8; x++) {
            double c = (u == 0) ? 0.35355339059327373 : 0.5;
            gCos[u][x] = (float)(c * cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0));
        }
    }
    gCosReady = 1;
}

static void idctBlock(const int *coef, uint8_t *dst, int stride) {
    float tmp[64];
    int x, y, u, v, onlyDc = 1;

    for (v = 1; v < 64; v++) {
        if (coef[v]) {
            onlyDc = 0;
            break;
        }
    }
    if (onlyDc) {
        int s = (int)floorf((float)coef[0] / 8.0f + 128.5f);
        uint8_t px = (uint8_t)(s < 0 ? 0 : s > 255 ? 255 : s);

        for (y = 0; y < 8; y++) memset(dst + y * stride, px, 8);
        return;
    }
    for (v = 0; v < 8; v++) { /* rows: along u */
        for (x = 0; x < 8; x++) {
            float s = 0.0f;

            for (u = 0; u < 8; u++) s += gCos[u][x] * (float)coef[v * 8 + u];
            tmp[v * 8 + x] = s;
        }
    }
    for (x = 0; x < 8; x++) { /* columns: along v */
        for (y = 0; y < 8; y++) {
            float s = 128.5f;
            int r;

            for (v = 0; v < 8; v++) s += gCos[v][y] * tmp[v * 8 + x];
            r = (int)floorf(s);
            dst[y * stride + x] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
        }
    }
}

/* One block: 0, or -1 on bad data */
static int decodeBlock(Bits *b, const Huff *dc, const Huff *ac, const uint16_t *q, int *pred, uint8_t *dst, int stride) {
    int coef[64];
    int t, k;

    memset(coef, 0, sizeof coef);
    t = huffDecode(b, dc);
    if (t < 0 || t > 11) return -1;
    if (t) *pred += extend((int)getBits(b, t), t);
    coef[0] = *pred * (int)q[0];
    for (k = 1; k < 64;) {
        int rs = huffDecode(b, ac);
        int r, s;

        if (rs < 0) return -1;
        r = rs >> 4;
        s = rs & 15;
        if (s == 0) {
            if (r == 15) {
                k += 16;
                continue;
            }
            break;
        }
        k += r;
        if (k > 63) return -1;
        coef[kZigzag[k]] = extend((int)getBits(b, s), s) * (int)q[kZigzag[k]];
        k++;
    }
    if (b->pad > 8 * 8) return -1; /* well past the end of the data */
    idctBlock(coef, dst, stride);
    return 0;
}

/* --- markers -------------------------------------------------------------- */

static unsigned be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

/* The next marker at or after pos: its code, with *pos left on its length field (or after it for the markers without one).
   0 at the end of the data. */
static int nextMarker(const uint8_t *data, size_t len, size_t *pos) {
    size_t i = *pos;

    for (;;) {
        while (i < len && data[i] != 0xFF) i++;
        while (i < len && data[i] == 0xFF) i++;
        if (i >= len) {
            *pos = len;
            return 0;
        }
        if (data[i] != 0) {
            *pos = i + 1;
            return data[i];
        }
        i++;
    }
}

static int parseDqt(Dec *d, const uint8_t *p, size_t n) {
    while (n > 0) {
        int pq = p[0] >> 4, tq = p[0] & 15, k;
        size_t need = 1 + (pq ? 128 : 64);

        if (pq > 1 || tq > 3 || n < need) return fail(d, "bad quantisation table");
        for (k = 0; k < 64; k++) {
            unsigned v = pq ? be16(p + 1 + 2 * k) : p[1 + k];

            if (v == 0) v = 1; /* a zero step would divide nothing; treat it as 1 */
            d->qt[tq][kZigzag[k]] = (uint16_t)v;
        }
        d->qdef[tq] = 1;
        p += need;
        n -= need;
    }
    return 0;
}

static int parseDht(Dec *d, const uint8_t *p, size_t n) {
    while (n >= 17) {
        int tc = p[0] >> 4, th = p[0] & 15, i, total = 0;
        Huff *h;

        if (tc > 1 || th > 3) return fail(d, "bad Huffman table");
        for (i = 0; i < 16; i++) total += p[1 + i];
        if (total > 256 || n < (size_t)17 + (size_t)total) return fail(d, "bad Huffman table");
        h = tc ? &d->ac[th] : &d->dc[th];
        if (buildHuff(h, p + 1, p + 17, total) != 0) return fail(d, "bad Huffman table");
        p += 17 + total;
        n -= 17 + (size_t)total;
    }
    return n == 0 ? 0 : fail(d, "bad Huffman table");
}

static int parseSof(Dec *d, int marker, const uint8_t *p, size_t n) {
    int i;

    if (marker != 0xC0 && marker != 0xC1) {
        if (marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE) return fail(d, "progressive JPEG is not supported");
        if (marker == 0xC3 || marker == 0xC7 || marker == 0xCB || marker == 0xCF) return fail(d, "lossless JPEG is not supported");
        if (marker >= 0xC9) return fail(d, "arithmetic coded JPEG is not supported");
        return fail(d, "unsupported JPEG frame type");
    }
    if (n < 6) return fail(d, "bad frame header");
    if (p[0] != 8) return fail(d, "only 8-bit JPEG is supported");
    d->h = (int)be16(p + 1);
    d->w = (int)be16(p + 3);
    d->nc = p[5];
    if (d->w <= 0 || d->h <= 0) return fail(d, "bad picture size");
    if ((long)d->w * (long)d->h > JPEG_MAX_PIXELS) return fail(d, "picture too large");
    if (d->nc != 1 && d->nc != 3) return fail(d, "only grey or three-component JPEG is supported");
    if (n < (size_t)6 + (size_t)d->nc * 3) return fail(d, "bad frame header");
    d->hmax = d->vmax = 1;
    for (i = 0; i < d->nc; i++) {
        Comp *c = &d->comp[i];

        c->id = p[6 + i * 3];
        c->h = p[7 + i * 3] >> 4;
        c->v = p[7 + i * 3] & 15;
        c->tq = p[8 + i * 3];
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->tq > 3) return fail(d, "bad component");
        if (c->h > d->hmax) d->hmax = c->h;
        if (c->v > d->vmax) d->vmax = c->v;
    }
    if (d->nc == 1) { /* a lone component is scanned block by block, whatever sampling it declares */
        d->comp[0].h = d->comp[0].v = d->hmax = d->vmax = 1;
    }
    for (i = 0; i < d->nc; i++) {
        if (d->hmax % d->comp[i].h != 0 || d->vmax % d->comp[i].v != 0) return fail(d, "unsupported chroma sampling");
    }
    return 0;
}

static int parseSos(Dec *d, const uint8_t *p, size_t n) {
    int ns, i, j;

    if (n < 1) return fail(d, "bad scan header");
    ns = p[0];
    if (ns != d->nc) return fail(d, "scans that split the components are not supported");
    if (n < (size_t)1 + (size_t)ns * 2 + 3) return fail(d, "bad scan header");
    for (i = 0; i < ns; i++) {
        int id = p[1 + i * 2], tdta = p[2 + i * 2];

        for (j = 0; j < d->nc; j++) {
            if (d->comp[j].id == id) break;
        }
        if (j == d->nc || (tdta >> 4) > 3 || (tdta & 15) > 3) return fail(d, "bad scan header");
        d->comp[j].td = tdta >> 4;
        d->comp[j].ta = tdta & 15;
    }
    return 0;
}

/* Walks the markers up to the frame header (stopAtSos 0) or the scan header; fills d. Returns the position of the scan's data,
   or 0 on failure (the reason in d->err) or, for a header-only read, the position past the frame header. */
static size_t parseHeaders(Dec *d, int headerOnly) {
    size_t pos = 2;
    int sawSof = 0;

    if (d->len < 4 || d->data[0] != 0xFF || d->data[1] != 0xD8) {
        fail(d, "not a JPEG (no SOI)");
        return 0;
    }
    for (;;) {
        int m = nextMarker(d->data, d->len, &pos);
        size_t seglen;
        const uint8_t *seg;

        if (m == 0) {
            fail(d, "no scan in the JPEG");
            return 0;
        }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
        if (m == 0xD9) {
            fail(d, "no scan in the JPEG");
            return 0;
        }
        if (pos + 2 > d->len) {
            fail(d, "truncated JPEG");
            return 0;
        }
        seglen = be16(d->data + pos);
        if (seglen < 2 || pos + seglen > d->len) {
            fail(d, "truncated JPEG");
            return 0;
        }
        seg = d->data + pos + 2;
        seglen -= 2;
        pos += seglen + 2;
        if (m == 0xDB) {
            if (parseDqt(d, seg, seglen)) return 0;
        } else if (m == 0xC4) {
            if (parseDht(d, seg, seglen)) return 0;
        } else if (m == 0xDD) {
            if (seglen < 2) {
                fail(d, "bad restart interval");
                return 0;
            }
            d->ri = (int)be16(seg);
        } else if (m == 0xE0) {
            if (seglen >= 5 && memcmp(seg, "JFIF", 5) == 0) d->jfif = 1;
        } else if (m == 0xEE) {
            if (seglen >= 12 && memcmp(seg, "Adobe", 5) == 0) {
                d->adobe = 1;
                d->adobeTransform = seg[11];
            }
        } else if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            if (sawSof) {
                fail(d, "more than one frame in the JPEG");
                return 0;
            }
            if (parseSof(d, m, seg, seglen)) return 0;
            sawSof = 1;
            if (headerOnly) return pos;
        } else if (m == 0xDA) {
            if (!sawSof) {
                fail(d, "scan before the frame header");
                return 0;
            }
            if (parseSos(d, seg, seglen)) return 0;
            return pos;
        }
    }
}

/* --- the picture ---------------------------------------------------------- */

static void freePlanes(Dec *d) {
    int i;

    for (i = 0; i < MAX_COMP; i++) {
        free(d->comp[i].plane);
        d->comp[i].plane = NULL;
    }
}

static int clamp8(int v) {
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static int decodeScan(Dec *d, size_t pos) {
    Bits b;
    int pred[MAX_COMP] = {0, 0, 0};
    int mx, my, i, n = 0, total;

    memset(&b, 0, sizeof b);
    b.p = d->data + pos;
    b.end = d->data + d->len;
    defaultTables(d);
    for (i = 0; i < d->nc; i++) {
        Comp *c = &d->comp[i];

        if (!d->qdef[c->tq]) return fail(d, "a quantisation table is missing");
        if (!d->dc[c->td].defined || !d->ac[c->ta].defined) return fail(d, "a Huffman table is missing");
        c->pw = d->mcux * c->h * 8;
        c->ph = d->mcuy * c->v * 8;
        c->plane = (uint8_t *)malloc((size_t)c->pw * (size_t)c->ph);
        if (c->plane == NULL) return fail(d, "out of memory");
    }
    total = d->mcux * d->mcuy;
    for (my = 0; my < d->mcuy; my++) {
        for (mx = 0; mx < d->mcux; mx++, n++) {
            for (i = 0; i < d->nc; i++) {
                Comp *c = &d->comp[i];
                int bx, by;

                for (by = 0; by < c->v; by++) {
                    for (bx = 0; bx < c->h; bx++) {
                        uint8_t *dst = c->plane + (size_t)(my * c->v * 8 + by * 8) * (size_t)c->pw + (size_t)(mx * c->h * 8 + bx * 8);

                        if (decodeBlock(&b, &d->dc[c->td], &d->ac[c->ta], d->qt[c->tq], &pred[i], dst, c->pw) != 0) {
                            return fail(d, "damaged picture data");
                        }
                    }
                }
            }
            if (d->ri > 0 && (n + 1) % d->ri == 0 && n + 1 < total) {
                /* restart: drop the partial byte, find RSTn (the data may run into it already), reset the predictors */
                b.cnt = 0;
                b.buf = 0;
                b.pad = 0;
                if (!(b.marker >= 0xD0 && b.marker <= 0xD7)) {
                    int guard = 0;

                    for (;;) {
                        int m;
                        size_t at = (size_t)(b.p - d->data);

                        if (b.marker) return fail(d, "damaged picture data (restart marker missing)");
                        m = nextMarker(d->data, d->len, &at);
                        if (m == 0 || ++guard > 64) return fail(d, "damaged picture data (restart marker missing)");
                        b.p = d->data + at;
                        if (m >= 0xD0 && m <= 0xD7) break;
                        if (m == 0xD9) return fail(d, "damaged picture data (ends early)");
                    }
                }
                b.marker = 0;
                pred[0] = pred[1] = pred[2] = 0;
            }
        }
    }
    return 0;
}

static int convert(Dec *d, uint8_t *rgb) {
    int x, y, i;
    int rgbMode = 0;

    if (d->nc == 3) {
        if (d->adobe) rgbMode = (d->adobeTransform == 0);
        else if (!d->jfif && d->comp[0].id == 'R' && d->comp[1].id == 'G' && d->comp[2].id == 'B') rgbMode = 1;
    }
    for (y = 0; y < d->h; y++) {
        uint8_t *o = rgb + (size_t)y * (size_t)d->w * 3;
        const uint8_t *row[MAX_COMP];

        for (i = 0; i < d->nc; i++) {
            const Comp *c = &d->comp[i];

            row[i] = c->plane + (size_t)(y * c->v / d->vmax) * (size_t)c->pw;
        }
        for (x = 0; x < d->w; x++, o += 3) {
            if (d->nc == 1) {
                o[0] = o[1] = o[2] = row[0][x];
            } else {
                int s0 = row[0][x * d->comp[0].h / d->hmax];
                int s1 = row[1][x * d->comp[1].h / d->hmax];
                int s2 = row[2][x * d->comp[2].h / d->hmax];

                if (rgbMode) {
                    o[0] = (uint8_t)s0;
                    o[1] = (uint8_t)s1;
                    o[2] = (uint8_t)s2;
                } else {
                    int cb = s1 - 128, cr = s2 - 128;

                    o[0] = (uint8_t)clamp8(s0 + ((91881 * cr + 32768) >> 16));
                    o[1] = (uint8_t)clamp8(s0 - ((22554 * cb + 46802 * cr + 32768) >> 16));
                    o[2] = (uint8_t)clamp8(s0 + ((116130 * cb + 32768) >> 16));
                }
            }
        }
    }
    return 0;
}

int jpegReadInfo(const uint8_t *data, size_t len, int *w, int *h, int *components, char *err, size_t errCap) {
    Dec d;

    memset(&d, 0, sizeof d);
    d.data = data;
    d.len = len;
    d.err = err;
    d.errCap = errCap;
    if (err != NULL && errCap > 0) err[0] = '\0';
    if (data == NULL || parseHeaders(&d, 1) == 0) {
        if (data == NULL) fail(&d, "no data");
        return -1;
    }
    if (w != NULL) *w = d.w;
    if (h != NULL) *h = d.h;
    if (components != NULL) *components = d.nc;
    return 0;
}

int jpegDecodeRgb24(const uint8_t *data, size_t len, uint8_t **rgb, int *w, int *h, char *err, size_t errCap) {
    Dec *d;
    size_t pos;
    uint8_t *out;
    int rc = -1;

    if (rgb != NULL) *rgb = NULL;
    if (err != NULL && errCap > 0) err[0] = '\0';
    d = (Dec *)calloc(1, sizeof *d);
    if (d == NULL || data == NULL || rgb == NULL) {
        free(d);
        if (err != NULL && errCap > 0) snprintf(err, errCap, "%s", data == NULL ? "no data" : "out of memory");
        return -1;
    }
    d->data = data;
    d->len = len;
    d->err = err;
    d->errCap = errCap;
    idctInit();
    pos = parseHeaders(d, 0);
    if (pos != 0) {
        d->mcux = (d->w + 8 * d->hmax - 1) / (8 * d->hmax);
        d->mcuy = (d->h + 8 * d->vmax - 1) / (8 * d->vmax);
        out = (uint8_t *)malloc((size_t)d->w * (size_t)d->h * 3);
        if (out == NULL) {
            fail(d, "out of memory");
        } else if (decodeScan(d, pos) == 0 && convert(d, out) == 0) {
            *rgb = out;
            if (w != NULL) *w = d->w;
            if (h != NULL) *h = d->h;
            rc = 0;
        } else {
            free(out);
        }
    }
    freePlanes(d);
    free(d);
    return rc;
}
