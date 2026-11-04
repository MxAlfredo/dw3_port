#include "png_read.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- inflate (RFC 1951), after the structure of zlib's puff.c ---- */

typedef struct Inf {
    const uint8_t *in;
    size_t n, pos;
    uint32_t buf;
    int cnt;
    uint8_t *out;
    size_t cap, o;
} Inf;

typedef struct Huff {
    short count[16];
    short symbol[288];
} Huff;

static int getBits(Inf *s, int need)
{
    uint32_t v = s->buf;
    while (s->cnt < need) {
        if (s->pos >= s->n) return -1;
        v |= (uint32_t)s->in[s->pos++] << s->cnt;
        s->cnt += 8;
    }
    s->buf = v >> need;
    s->cnt -= need;
    return (int)(v & ((1u << need) - 1));
}

static int decodeSym(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len <= 15; len++) {
        int b = getBits(s, 1), count;
        if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* 0: complete code; > 0: incomplete; < 0: over-subscribed */
static int construct(Huff *h, const short *length, int n)
{
    int sym, len, left;
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++) {
        if (length[sym]) h->symbol[offs[length[sym]]++] = (short)sym;
    }
    return left;
}

static int codes(Inf *s, const Huff *lc, const Huff *dc)
{
    static const short lbase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int sym = decodeSym(s, lc);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->o >= s->cap) return -1;
            s->out[s->o++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            int len, dist, e;
            size_t k;
            sym -= 257;
            if (sym >= 29) return -1;
            e = getBits(s, lext[sym]);
            if (e < 0) return -1;
            len = lbase[sym] + e;
            sym = decodeSym(s, dc);
            if (sym < 0 || sym >= 30) return -1;
            e = getBits(s, dext[sym]);
            if (e < 0) return -1;
            dist = dbase[sym] + e;
            if ((size_t)dist > s->o || s->o + (size_t)len > s->cap) return -1;
            for (k = 0; k < (size_t)len; k++, s->o++) s->out[s->o] = s->out[s->o - (size_t)dist];
        }
    }
}

static int fixedBlock(Inf *s)
{
    static Huff lc, dc;
    static int ready;
    if (!ready) {
        short l[288];
        int i;
        for (i = 0; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        construct(&lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        construct(&dc, l, 30);
        ready = 1;
    }
    return codes(s, &lc, &dc);
}

static int dynamicBlock(Inf *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    Huff lc, dc;
    int nlen, ndist, ncode, i, err;
    nlen = getBits(s, 5);
    ndist = getBits(s, 5);
    ncode = getBits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return -1;
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > 286 || ndist > 30) return -1;
    for (i = 0; i < 19; i++) lengths[i] = 0;
    for (i = 0; i < ncode; i++) {
        int v = getBits(s, 3);
        if (v < 0) return -1;
        lengths[order[i]] = (short)v;
    }
    if (construct(&lc, lengths, 19) != 0) return -1;
    for (i = 0; i < nlen + ndist;) {
        int sym = decodeSym(s, &lc), rep, val = 0;
        if (sym < 0) return -1;
        if (sym < 16) {
            lengths[i++] = (short)sym;
            continue;
        }
        if (sym == 16) {
            if (i == 0) return -1;
            val = lengths[i - 1];
            rep = getBits(s, 2);
            rep = rep < 0 ? rep : rep + 3;
        } else if (sym == 17) {
            rep = getBits(s, 3);
            rep = rep < 0 ? rep : rep + 3;
        } else {
            rep = getBits(s, 7);
            rep = rep < 0 ? rep : rep + 11;
        }
        if (rep < 0 || i + rep > nlen + ndist) return -1;
        while (rep--) lengths[i++] = (short)val;
    }
    if (lengths[256] == 0) return -1;
    err = construct(&lc, lengths, nlen);
    if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return -1;
    err = construct(&dc, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return -1;
    return codes(s, &lc, &dc);
}

int pngInflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *outLen)
{
    Inf s;
    int last;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.n = n;
    s.out = out;
    s.cap = cap;
    do {
        int type;
        last = getBits(&s, 1);
        type = getBits(&s, 2);
        if (last < 0 || type < 0) return -1;
        if (type == 0) {
            unsigned len, nlen;
            s.buf = 0;
            s.cnt = 0;
            if (s.pos + 4 > s.n) return -1;
            len = s.in[s.pos] | (unsigned)s.in[s.pos + 1] << 8;
            nlen = s.in[s.pos + 2] | (unsigned)s.in[s.pos + 3] << 8;
            s.pos += 4;
            if ((len ^ 0xFFFFu) != nlen || s.pos + len > s.n || s.o + len > s.cap) return -1;
            memcpy(s.out + s.o, s.in + s.pos, len);
            s.o += len;
            s.pos += len;
        } else if (type == 1) {
            if (fixedBlock(&s)) return -1;
        } else if (type == 2) {
            if (dynamicBlock(&s)) return -1;
        } else {
            return -1;
        }
    } while (!last);
    *outLen = s.o;
    return 0;
}

/* ---- PNG ---- */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint32_t crcOf(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static int ready;
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    if (!ready) {
        uint32_t k, v;
        int b;
        for (k = 0; k < 256; k++) {
            v = k;
            for (b = 0; b < 8; b++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[k] = v;
        }
        ready = 1;
    }
    for (i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static int fail(char *why, size_t size, const char *msg)
{
    if (why && size) snprintf(why, size, "%s", msg);
    return -1;
}

void pngImageFree(PngImage *p)
{
    free(p->px);
    p->px = NULL;
}

#define PNG_MAX_DIM 4096

int pngReadIndexed(const uint8_t *d, size_t n, PngImage *o, char *why, size_t ws)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    size_t at = 8, zn = 0, zcap = 0, rowBytes, rawLen, got = 0, y;
    uint8_t *z = NULL, *raw = NULL, *prev;
    int ctype = -1, haveHdr = 0, sawEnd = 0, interlace = 0, x, bpp;
    uint32_t a1 = 1, a2 = 0, adler;

    memset(o, 0, sizeof *o);
    if (n < 8 || memcmp(d, sig, 8)) return fail(why, ws, "not a PNG file");
    while (at + 12 <= n && !sawEnd) {
        uint32_t len = be32(d + at);
        const uint8_t *type = d + at + 4, *body = d + at + 8;
        if (len > n || at + 12 + (size_t)len > n) {
            free(z);
            return fail(why, ws, "truncated chunk");
        }
        if (crcOf(type, 4 + (size_t)len) != be32(body + len)) {
            free(z);
            return fail(why, ws, "chunk CRC mismatch (corrupt file)");
        }
        if (!memcmp(type, "IHDR", 4) && len == 13) {
            o->w = (int)be32(body);
            o->h = (int)be32(body + 4);
            o->depth = body[8];
            ctype = body[9];
            interlace = body[12];
            haveHdr = body[10] == 0 && body[11] == 0;
        } else if (!memcmp(type, "PLTE", 4)) {
            if (len % 3 || len > 768 || len == 0) {
                free(z);
                return fail(why, ws, "bad palette chunk");
            }
            o->nPal = (int)len / 3;
            memcpy(o->rgb, body, len);
        } else if (!memcmp(type, "tRNS", 4)) {
            if (len > 256) {
                free(z);
                return fail(why, ws, "bad transparency chunk");
            }
            o->nAlpha = (int)len;
            memcpy(o->alpha, body, len);
        } else if (!memcmp(type, "IDAT", 4)) {
            if (zn + len > zcap) {
                uint8_t *g;
                zcap = zn + len + 4096;
                g = (uint8_t *)realloc(z, zcap);
                if (!g) {
                    free(z);
                    return fail(why, ws, "out of memory");
                }
                z = g;
            }
            memcpy(z + zn, body, len);
            zn += len;
        } else if (!memcmp(type, "IEND", 4)) {
            sawEnd = 1;
        }
        at += 12 + (size_t)len;
    }
    if (!haveHdr || !sawEnd) {
        free(z);
        return fail(why, ws, "incomplete PNG (no header or no end)");
    }
    if (ctype != 3) {
        free(z);
        return fail(why, ws, ctype == 2 || ctype == 6 ? "truecolor PNG: save it as an indexed (palette) image" : "not an indexed (palette) PNG");
    }
    if (interlace) {
        free(z);
        return fail(why, ws, "interlaced PNG: save it without interlacing");
    }
    if (o->depth != 1 && o->depth != 2 && o->depth != 4 && o->depth != 8) {
        free(z);
        return fail(why, ws, "bad bit depth");
    }
    if (o->w <= 0 || o->h <= 0 || o->w > PNG_MAX_DIM || o->h > PNG_MAX_DIM) {
        free(z);
        return fail(why, ws, "bad image size");
    }
    if (o->nPal == 0) {
        free(z);
        return fail(why, ws, "no palette");
    }
    rowBytes = ((size_t)o->w * (size_t)o->depth + 7) / 8;
    rawLen = (rowBytes + 1) * (size_t)o->h;
    if (zn < 6 || (z[0] & 15) != 8 || ((z[0] << 8) | z[1]) % 31 || (z[1] & 0x20)) {
        free(z);
        return fail(why, ws, "bad zlib header");
    }
    raw = (uint8_t *)malloc(rawLen);
    if (!raw || pngInflate(z + 2, zn - 6, raw, rawLen, &got) || got != rawLen) {
        free(z);
        free(raw);
        return fail(why, ws, "corrupt or short image data");
    }
    adler = be32(z + zn - 4);
    for (y = 0; y < rawLen; y++) {
        a1 = (a1 + raw[y]) % 65521u;
        a2 = (a2 + a1) % 65521u;
    }
    free(z);
    if (adler != ((a2 << 16) | a1)) {
        free(raw);
        return fail(why, ws, "image data checksum mismatch (corrupt file)");
    }
    /* unfilter in place (bytes per pixel 1: palette images, filters work on bytes) */
    bpp = 1;
    for (y = 0; y < (size_t)o->h; y++) {
        uint8_t *row = raw + y * (rowBytes + 1);
        int f = row[0];
        prev = y ? row - (rowBytes + 1) + 1 : NULL;
        row++;
        if (f > 4) {
            free(raw);
            return fail(why, ws, "bad filter type");
        }
        for (x = 0; x < (int)rowBytes; x++) {
            int a = x >= bpp ? row[x - bpp] : 0, b = prev ? prev[x] : 0, c = (prev && x >= bpp) ? prev[x - bpp] : 0, v = row[x];
            if (f == 1) v += a;
            else if (f == 2) v += b;
            else if (f == 3) v += (a + b) / 2;
            else if (f == 4) {
                int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            }
            row[x] = (uint8_t)v;
        }
    }
    o->px = (uint8_t *)malloc((size_t)o->w * (size_t)o->h);
    if (!o->px) {
        free(raw);
        return fail(why, ws, "out of memory");
    }
    for (y = 0; y < (size_t)o->h; y++) {
        const uint8_t *row = raw + y * (rowBytes + 1) + 1;
        for (x = 0; x < o->w; x++) {
            int per = 8 / o->depth, sh = 8 - o->depth * (x % per + 1), mask = (1 << o->depth) - 1;
            o->px[y * (size_t)o->w + (size_t)x] = (uint8_t)((row[x / per] >> sh) & mask);
        }
    }
    free(raw);
    return 0;
}
