#include "png_write.h"

#include <stdlib.h>
#include <string.h>

static uint32_t crcTable[256];
static int crcReady;

static void crcInit(void)
{
    uint32_t n, c;
    int k;
    for (n = 0; n < 256; n++) {
        c = n;
        for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
    crcReady = 1;
}

static uint32_t crcUpdate(uint32_t crc, const uint8_t *p, size_t n)
{
    size_t i;
    if (!crcReady) crcInit();
    for (i = 0; i < n; i++) crc = crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
    int bad;
} Buf;

static void put(Buf *b, const void *d, size_t n)
{
    if (b->bad) return;
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap : 1024;
        uint8_t *g;
        while (c < b->n + n) c *= 2;
        g = (uint8_t *)realloc(b->p, c);
        if (!g) {
            b->bad = 1;
            return;
        }
        b->p = g;
        b->cap = c;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void put32(Buf *b, uint32_t v)
{
    uint8_t t[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    put(b, t, 4);
}

/* One chunk: length, type, data, CRC over type and data */
static void chunk(Buf *b, const char *type, const uint8_t *d, size_t n)
{
    uint32_t crc = crcUpdate(0xFFFFFFFFu, (const uint8_t *)type, 4);
    put32(b, (uint32_t)n);
    put(b, type, 4);
    if (n) {
        put(b, d, n);
        crc = crcUpdate(crc, d, n);
    }
    put32(b, crc ^ 0xFFFFFFFFu);
}

/* ---------------------------------------------------------------- deflate (RFC 1951) */

/* Our own compressor, no dependency: LZ77 with hash chains and one-step lazy matching, then per block the
   smallest of stored, fixed-Huffman and dynamic-Huffman coding. The output is deterministic (the manifest
   hash needs that) and every inflater reads it, including the port's own png_read.c. */

typedef struct BitW {
    Buf *b;
    uint32_t acc;
    int n;
} BitW;

static void bits(BitW *w, uint32_t v, int n)
{
    w->acc |= v << w->n;
    w->n += n;
    while (w->n >= 8) {
        uint8_t c = (uint8_t)(w->acc & 0xFF);
        put(w->b, &c, 1);
        w->acc >>= 8;
        w->n -= 8;
    }
}

static void flushBits(BitW *w)
{
    if (w->n > 0) bits(w, 0, 8 - w->n);
}

static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int lenSym(int len)
{
    int s = 28;
    while (LBASE[s] > len) s--;
    return s;
}

static uint32_t distBase(int s) { return s < 4 ? (uint32_t)s + 1 : (1u << (DEXT[s] + 1)) + 1 + ((uint32_t)(s & 1) << DEXT[s]); }

static int distSym(uint32_t d)
{
    int l = 0;
    uint32_t v = d - 1;
    if (d <= 4) return (int)d - 1;
    while ((v >> (l + 1)) != 0) l++; /* v has l+1 significant bits */
    return 2 * l + (int)((v >> (l - 1)) & 1);
}

/* Code lengths (at most `limit` bits) of a Huffman code over freq[0..n). At least two symbols always get a
   code, so the code is complete and every reader accepts it. */
static void huffLens(const uint32_t *freq0, int n, int limit, uint8_t *len)
{
    uint32_t freq[288];
    int i, nz = 0;
    for (i = 0; i < n; i++) {
        freq[i] = freq0[i];
        if (freq[i]) nz++;
    }
    for (i = 0; nz < 2; i++) {
        if (!freq[i]) {
            freq[i] = 1;
            nz++;
        }
    }
    for (;;) {
        uint32_t w[576];
        int par[576], live[576], nLive = 0, nNode = n, max = 0;
        for (i = 0; i < n; i++) {
            w[i] = freq[i];
            par[i] = -1;
            if (freq[i]) live[nLive++] = i;
        }
        while (nLive > 1) {
            int a = 0, b = 1, k, t;
            if (w[live[b]] < w[live[a]]) {
                a = 1;
                b = 0;
            }
            for (k = 2; k < nLive; k++) {
                if (w[live[k]] < w[live[a]]) {
                    b = a;
                    a = k;
                } else if (w[live[k]] < w[live[b]]) {
                    b = k;
                }
            }
            w[nNode] = w[live[a]] + w[live[b]];
            par[nNode] = -1;
            par[live[a]] = nNode;
            par[live[b]] = nNode;
            t = a > b ? a : b;
            live[a < b ? a : b] = nNode;
            live[t] = live[--nLive];
            nNode++;
        }
        for (i = 0; i < n; i++) {
            int d = 0, k = i;
            if (freq[i]) {
                while (par[k] >= 0) {
                    k = par[k];
                    d++;
                }
            }
            len[i] = (uint8_t)d;
            if (d > max) max = d;
        }
        if (max <= limit) return;
        for (i = 0; i < n; i++) {
            if (freq[i]) freq[i] = (freq[i] + 1) >> 1;
        }
    }
}

static uint32_t revBits(uint32_t v, int n)
{
    uint32_t r = 0;
    int i;
    for (i = 0; i < n; i++) {
        r = (r << 1) | (v & 1);
        v >>= 1;
    }
    return r;
}

/* canonical codes, already bit-reversed for the LSB-first writer */
static void canonCodes(const uint8_t *len, int n, uint16_t *code)
{
    int cnt[16] = {0}, next[16], i, c = 0;
    for (i = 0; i < n; i++) cnt[len[i]]++;
    cnt[0] = 0;
    for (i = 1; i < 16; i++) {
        c = (c + cnt[i - 1]) << 1;
        next[i] = c;
    }
    for (i = 0; i < n; i++) code[i] = len[i] ? (uint16_t)revBits((uint32_t)next[len[i]]++, len[i]) : 0;
}

#define TOK_MATCH(len, dist) (((uint32_t)(len) << 16) | (uint32_t)(dist))

enum { HB = 15, WIN = 32768, CHAIN = 128, NICE = 258 };

typedef struct Match {
    const uint8_t *s;
    size_t n, next; /* positions below `next` are in the hash chains */
    int32_t *head, *prev;
} Match;

static uint32_t hash3(const uint8_t *p) { return (((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 2654435761u) >> (32 - HB); }

static void insertUpTo(Match *m, size_t at)
{
    for (; m->next < at; m->next++) {
        if (m->next + 3 <= m->n) {
            uint32_t h = hash3(m->s + m->next);
            m->prev[m->next & (WIN - 1)] = m->head[h];
            m->head[h] = (int32_t)m->next;
        }
    }
}

/* the longest earlier match of position `at` (0 when shorter than 3) */
static size_t findMatch(Match *m, size_t at, size_t *dist)
{
    size_t best = 0, maxl;
    int32_t cand;
    int chain = CHAIN;
    if (at + 3 > m->n) return 0;
    insertUpTo(m, at);
    cand = m->head[hash3(m->s + at)];
    maxl = m->n - at < 258 ? m->n - at : 258;
    while (cand >= 0 && chain-- > 0 && at - (size_t)cand <= WIN) {
        size_t l = 0;
        while (l < maxl && m->s[(size_t)cand + l] == m->s[at + l]) l++;
        if (l > best) {
            best = l;
            *dist = at - (size_t)cand;
            if (l >= NICE || l == maxl) break;
        }
        if (m->prev[(size_t)cand & (WIN - 1)] >= cand) break;
        cand = m->prev[(size_t)cand & (WIN - 1)];
    }
    if (best == 3 && *dist > 4096) return 0; /* not worth its bits */
    return best >= 3 ? best : 0;
}

/* tokens of s: a literal is the byte, a match is TOK_MATCH(len, dist) (len >= 3, so it is >= 3 << 16) */
static size_t tokenize(const uint8_t *s, size_t n, uint32_t *tok)
{
    Match m;
    size_t i = 0, nt = 0;
    m.s = s;
    m.n = n;
    m.next = 0;
    m.head = (int32_t *)malloc(sizeof(int32_t) << HB);
    m.prev = (int32_t *)malloc(sizeof(int32_t) * WIN);
    if (!m.head || !m.prev) {
        free(m.head);
        free(m.prev);
        return (size_t)-1;
    }
    memset(m.head, 0xFF, sizeof(int32_t) << HB);
    memset(m.prev, 0xFF, sizeof(int32_t) * WIN);
    while (i < n) {
        size_t d0 = 0, d1 = 0, l0 = findMatch(&m, i, &d0), l1 = 0;
        if (l0 && l0 < 32) l1 = findMatch(&m, i + 1, &d1);
        if (l0 && l1 <= l0) {
            tok[nt++] = TOK_MATCH(l0, d0);
            i += l0;
        } else {
            tok[nt++] = s[i++]; /* a literal; with l1 > l0 the better match starts one byte later */
        }
    }
    free(m.head);
    free(m.prev);
    return nt;
}

/* ---- one block */

typedef struct Codes {
    uint8_t ll[288], dl[30];
    uint16_t lc[288], dc[30];
} Codes;

static void fixedCodes(Codes *c)
{
    int i;
    for (i = 0; i < 288; i++) c->ll[i] = (uint8_t)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
    for (i = 0; i < 30; i++) c->dl[i] = 5;
    canonCodes(c->ll, 288, c->lc);
    canonCodes(c->dl, 30, c->dc);
}

static uint64_t tokenBits(const uint32_t *tok, size_t nt, const Codes *c)
{
    uint64_t bitsN = c->ll[256];
    size_t i;
    for (i = 0; i < nt; i++) {
        if (tok[i] < (3u << 16)) {
            bitsN += c->ll[tok[i]];
        } else {
            int ls = lenSym((int)(tok[i] >> 16)), ds = distSym(tok[i] & 0xFFFF);
            bitsN += (uint64_t)c->ll[257 + ls] + LEXT[ls] + c->dl[ds] + DEXT[ds];
        }
    }
    return bitsN;
}

static void emitTokens(BitW *w, const uint32_t *tok, size_t nt, const Codes *c)
{
    size_t i;
    for (i = 0; i < nt; i++) {
        if (tok[i] < (3u << 16)) {
            bits(w, c->lc[tok[i]], c->ll[tok[i]]);
        } else {
            int len = (int)(tok[i] >> 16), dist = (int)(tok[i] & 0xFFFF), ls = lenSym(len), ds = distSym((uint32_t)dist);
            bits(w, c->lc[257 + ls], c->ll[257 + ls]);
            if (LEXT[ls]) bits(w, (uint32_t)(len - LBASE[ls]), LEXT[ls]);
            bits(w, c->dc[ds], c->dl[ds]);
            if (DEXT[ds]) bits(w, (uint32_t)dist - distBase(ds), DEXT[ds]);
        }
    }
    bits(w, c->lc[256], c->ll[256]);
}

static const uint8_t CL_ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* the run-length coded code lengths of a dynamic block header: (symbol, extra bits value) pairs */
static int rleLens(const uint8_t *all, int n, uint8_t *sym, uint8_t *ext)
{
    int i = 0, k = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && all[i + run] == all[i]) run++;
        if (all[i] == 0 && run >= 3) {
            if (run > 138) run = 138;
            sym[k] = (uint8_t)(run >= 11 ? 18 : 17);
            ext[k++] = (uint8_t)(run >= 11 ? run - 11 : run - 3);
            i += run;
        } else if (all[i] != 0 && run >= 4) {
            sym[k] = all[i];
            ext[k++] = 0;
            i++;
            run--;
            if (run > 6) run = 6;
            sym[k] = 16;
            ext[k++] = (uint8_t)(run - 3);
            i += run;
        } else {
            sym[k] = all[i];
            ext[k++] = 0;
            i++;
        }
    }
    return k;
}

/* One block of tokens covering `span` source bytes; src is where they start (for the stored fallback) */
static void emitBlock(BitW *w, const uint8_t *src, size_t span, const uint32_t *tok, size_t nt, int final)
{
    static Codes fixed;
    static int fixedReady;
    Codes dyn;
    uint32_t lf[288], df[30], cf[19];
    uint8_t all[288 + 30], rs[288 + 30], re[288 + 30], cl[19];
    uint16_t cc[19];
    int hlit, hdist, hclen, nrle, i;
    uint64_t fixedBits, dynBits, storedBits;
    size_t t;

    if (!fixedReady) {
        fixedCodes(&fixed);
        fixedReady = 1;
    }
    memset(lf, 0, sizeof lf);
    memset(df, 0, sizeof df);
    lf[256] = 1;
    for (t = 0; t < nt; t++) {
        if (tok[t] < (3u << 16)) {
            lf[tok[t]]++;
        } else {
            lf[257 + lenSym((int)(tok[t] >> 16))]++;
            df[distSym(tok[t] & 0xFFFF)]++;
        }
    }
    huffLens(lf, 286, 15, dyn.ll);
    memset(dyn.ll + 286, 0, 2);
    huffLens(df, 30, 15, dyn.dl);
    canonCodes(dyn.ll, 288, dyn.lc);
    canonCodes(dyn.dl, 30, dyn.dc);
    for (hlit = 286; hlit > 257 && !dyn.ll[hlit - 1]; hlit--) {
    }
    for (hdist = 30; hdist > 1 && !dyn.dl[hdist - 1]; hdist--) {
    }
    memcpy(all, dyn.ll, (size_t)hlit);
    memcpy(all + hlit, dyn.dl, (size_t)hdist);
    nrle = rleLens(all, hlit + hdist, rs, re);
    memset(cf, 0, sizeof cf);
    for (i = 0; i < nrle; i++) cf[rs[i]]++;
    huffLens(cf, 19, 7, cl);
    canonCodes(cl, 19, cc);
    for (hclen = 19; hclen > 4 && !cl[CL_ORDER[hclen - 1]]; hclen--) {
    }
    dynBits = 3 + 14 + 3 * (uint64_t)hclen + tokenBits(tok, nt, &dyn);
    for (i = 0; i < nrle; i++) dynBits += (uint64_t)cl[rs[i]] + (rs[i] == 16 ? 2 : rs[i] == 17 ? 3 : rs[i] == 18 ? 7 : 0);
    fixedBits = 3 + tokenBits(tok, nt, &fixed);
    storedBits = span <= 65535 ? 3 + 7 + 32 + 8 * (uint64_t)span : ~(uint64_t)0;

    if (storedBits < fixedBits && storedBits < dynBits) {
        uint8_t hdr[4] = {(uint8_t)span, (uint8_t)(span >> 8), (uint8_t)~span, (uint8_t)(~span >> 8)};
        bits(w, (uint32_t)final, 1);
        bits(w, 0, 2);
        flushBits(w);
        put(w->b, hdr, 4);
        put(w->b, src, span);
    } else if (fixedBits <= dynBits) {
        bits(w, (uint32_t)final, 1);
        bits(w, 1, 2);
        emitTokens(w, tok, nt, &fixed);
    } else {
        bits(w, (uint32_t)final, 1);
        bits(w, 2, 2);
        bits(w, (uint32_t)(hlit - 257), 5);
        bits(w, (uint32_t)(hdist - 1), 5);
        bits(w, (uint32_t)(hclen - 4), 4);
        for (i = 0; i < hclen; i++) bits(w, cl[CL_ORDER[i]], 3);
        for (i = 0; i < nrle; i++) {
            bits(w, cc[rs[i]], cl[rs[i]]);
            if (rs[i] == 16) bits(w, re[i], 2);
            else if (rs[i] == 17) bits(w, re[i], 3);
            else if (rs[i] == 18) bits(w, re[i], 7);
        }
        emitTokens(w, tok, nt, &dyn);
    }
}

/* A zlib stream of raw[0..n): header, deflate blocks of at most 32768 tokens, Adler-32. -1 on no memory. */
static int zlibCompress(Buf *z, const uint8_t *raw, size_t n)
{
    enum { BLOCK = 32768 };
    BitW w;
    uint32_t *tok;
    size_t nt, at = 0, t = 0, a1 = 1, a2 = 0, i;
    uint8_t adler[4];

    tok = (uint32_t *)malloc(sizeof(uint32_t) * (n ? n : 1));
    if (!tok) return -1;
    nt = tokenize(raw, n, tok);
    if (nt == (size_t)-1) {
        free(tok);
        return -1;
    }
    w.b = z;
    w.acc = 0;
    w.n = 0;
    put(z, "\x78\xDA", 2); /* 32K window, best compression flag */
    do {
        size_t end = nt - t < BLOCK ? nt : t + BLOCK, k, span = 0;
        for (k = t; k < end; k++) span += tok[k] < (3u << 16) ? 1 : tok[k] >> 16;
        emitBlock(&w, raw + at, span, tok + t, end - t, end == nt);
        at += span;
        t = end;
    } while (t < nt);
    flushBits(&w);
    for (i = 0; i < n; i++) {
        a1 = (a1 + raw[i]) % 65521u;
        a2 = (a2 + a1) % 65521u;
    }
    adler[0] = (uint8_t)(a2 >> 8);
    adler[1] = (uint8_t)a2;
    adler[2] = (uint8_t)(a1 >> 8);
    adler[3] = (uint8_t)a1;
    put(z, adler, 4);
    free(tok);
    return z->bad ? -1 : 0;
}

/* ---------------------------------------------------------------- PNG scanline filters */

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* filter type f of one row (rb bytes, one byte per pixel step) into out; prev is the row above or zeros */
static void filterRow(int f, const uint8_t *row, const uint8_t *prev, size_t rb, uint8_t *out)
{
    size_t i;
    for (i = 0; i < rb; i++) {
        int a = i ? row[i - 1] : 0, b = prev[i], c = i ? prev[i - 1] : 0, v = row[i];
        switch (f) {
        case 1: v -= a; break;
        case 2: v -= b; break;
        case 3: v -= (a + b) >> 1; break;
        case 4: v -= paeth(a, b, c); break;
        default: break;
        }
        out[i] = (uint8_t)v;
    }
}

/* Cost guess of a filtered row. Palette indices are labels, not magnitudes, so what compresses is a row of
   repeated bytes: count the bytes that differ from their left neighbour. */
static uint32_t rowCost(const uint8_t *r, size_t rb)
{
    uint32_t c = 0;
    size_t i;
    for (i = 0; i < rb; i++) c += (i == 0 ? r[i] != 0 : r[i] != r[i - 1]);
    return c;
}

int pngWriteIndexed(const uint8_t *px, int w, int h, int depth, const uint8_t *rgb, int nColors,
                    const uint8_t *alpha, int nAlpha, uint8_t **out, size_t *len)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Buf b = {0, 0, 0, 0}, z = {0, 0, 0, 0};
    uint8_t ihdr[13], *packed, *raw, *cand;
    size_t rowBytes, rawLen;
    int x, y, f;

    if (w <= 0 || h <= 0 || (depth != 4 && depth != 8) || nColors < 1 || nColors > (1 << depth) || nAlpha < 0 ||
        nAlpha > nColors) {
        return -1;
    }
    rowBytes = depth == 4 ? ((size_t)w + 1) / 2 : (size_t)w;
    rawLen = (rowBytes + 1) * (size_t)h;
    packed = (uint8_t *)calloc(rowBytes * (size_t)h + rowBytes, 1); /* the rows, then one zero row above the first */
    raw = (uint8_t *)malloc(rawLen);
    cand = (uint8_t *)malloc(rowBytes);
    if (!packed || !raw || !cand) {
        free(packed);
        free(raw);
        free(cand);
        return -1;
    }
    for (y = 0; y < h; y++) {
        uint8_t *row = packed + (size_t)y * rowBytes;
        for (x = 0; x < w; x++) {
            uint8_t v = px[(size_t)y * w + x];
            if (depth == 4) row[x / 2] |= (uint8_t)((v & 15) << ((x & 1) ? 0 : 4));
            else row[x] = v;
        }
    }
    for (y = 0; y < h; y++) {
        const uint8_t *row = packed + (size_t)y * rowBytes;
        const uint8_t *above = y ? row - rowBytes : packed + (size_t)h * rowBytes;
        uint8_t *dst = raw + (size_t)y * (rowBytes + 1);
        uint32_t bestCost = 0xFFFFFFFFu;
        int best = 0;
        for (f = 0; f < 5; f++) {
            uint32_t c;
            filterRow(f, row, above, rowBytes, cand);
            c = rowCost(cand, rowBytes);
            if (f == 0) c += c; /* the cost is a guess and the plain row never loses by much: another filter has to look at least twice as good */
            if (c < bestCost) {
                bestCost = c;
                best = f;
            }
        }
        dst[0] = (uint8_t)best;
        filterRow(best, row, above, rowBytes, dst + 1);
    }
    free(packed);
    free(cand);
    if (zlibCompress(&z, raw, rawLen) != 0) {
        free(raw);
        free(z.p);
        return -1;
    }
    free(raw);

    put(&b, sig, 8);
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16); ihdr[2] = (uint8_t)(w >> 8); ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16); ihdr[6] = (uint8_t)(h >> 8); ihdr[7] = (uint8_t)h;
    ihdr[8] = (uint8_t)depth;
    ihdr[9] = 3; /* colour type: indexed */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(&b, "IHDR", ihdr, 13);
    chunk(&b, "PLTE", rgb, (size_t)nColors * 3);
    if (alpha && nAlpha > 0) chunk(&b, "tRNS", alpha, (size_t)nAlpha);
    chunk(&b, "IDAT", z.p, z.n);
    chunk(&b, "IEND", NULL, 0);
    free(z.p);
    if (b.bad || z.bad) {
        free(b.p);
        return -1;
    }
    *out = b.p;
    *len = b.n;
    return 0;
}
