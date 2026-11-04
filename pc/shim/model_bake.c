/*
 * model_bake.c - the model baker (model-import I5, ADR-011 section 4): a neutral model source (model.json of format
 * "model-src-v1" + a glTF 2.0 .gltf/.glb + PNG) becomes the game's battle model archive, in portable C, at load.
 *
 * It is a line by line port of tools/model_import.py, which stays the reference: the blob is byte for byte the Python
 * tool's. Same operations in the same order on doubles (the file is built with -ffp-contract=off), Python's round() is
 * rint() (half to even), its float // is pyFloorDiv, and every place where Python's dict or set order could matter was
 * checked to be order independent or is reproduced in insertion order. See docs/formats.md 4.6 "Importador".
 *
 * Why no cgltf: the reader needs the glTF as a JSON tree (the port has one: ExtJson), raw accessor bytes read the way the
 * reference does (normalized integers divided in double, not float) and base64 for data URIs; that is a few hundred lines
 * and one dependency fewer to vendor, license and audit for Windows and Android. The PNG reader is the port's own
 * (png_read.c's inflate) plus the colour conversions of the reference.
 *
 * Errors never crash: every failure is a longjmp to the entry point with a reason; all memory is an arena freed at the end.
 */
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "content.h"
#include "mods.h"
#include "model_bake.h"
#include "png_read.h"
#include "port_fs.h"
#include "port_log.h"
#include "port_paths.h"

#define FORMAT "model-src-v1"
#define SENTINEL 30000
#define SLOTS 62
#define MAX_STEPS 0x640
#define TEX_W 256
#define TEX_H 240
#define MAX_CLUTS 64
#define MAX_VERTS 256
#define WARN_BONES 40
#define MAX_BONES 90
#define WARN_POLYS 3000
#define BLEND_STEPS 10
#define FILE_MAX (64 * 1024 * 1024)

static const int MIN_SLOTS[9] = {0, 1, 2, 3, 4, 10, 11, 12, 14};
static const double C_AXES[3] = {-1.0, -1.0, 1.0};
static const double WEIGHTS[3] = {2.0, 3.0, 1.0};
#define PI_ 3.14159265358979323846

/* ---------------------------------------------------------------- arena, errors, vectors */

typedef struct Blk {
    struct Blk *next;
    size_t cap, used;
} Blk;

typedef struct Bk {
    Blk *blk;
    jmp_buf jb;
    char err[320];
    ModelLogFn log;
    void *ud;
    char *seen[128];
    int nseen;
    ExtJson *json[8]; /* parsed trees (malloc'ed by the JSON reader), freed with the arena */
    int njson;
    const char *baseDir;
    /* "from": the original fighter whose files fill what the mod's folder lacks, built on first need */
    const ModelOriginals *orig;
    int fromId;           /* -1: no "from" */
    int dry;              /* the cache-key pass: a file that would come from the original is not read */
    int fellBack;         /* the last readFile gave the original's bytes (not part of the hash) */
    ModelExportOut ex;
    int exLoaded;
    /* reading the sources: the hash of everything the bake depends on */
    uint64_t hash;
} Bk;

static void hashBytes(Bk *b, const void *p, size_t n) {
    const unsigned char *s = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        b->hash ^= s[i];
        b->hash *= 0x100000001b3ULL;
    }
}

static void *aalloc(Bk *b, size_t n) {
    Blk *k;
    void *p;
    n = (n + 15) & ~(size_t)15;
    if (n == 0) n = 16;
    k = b->blk;
    if (k == NULL || k->cap - k->used < n) {
        size_t cap = n + sizeof(Blk) + 16 > (size_t)1 << 20 ? n : (size_t)1 << 20;
        Blk *nk = (Blk *)malloc(sizeof(Blk) + cap + 16);
        if (nk == NULL) {
            snprintf(b->err, sizeof b->err, "out of memory");
            longjmp(b->jb, 1);
        }
        nk->next = b->blk;
        nk->cap = cap;
        nk->used = 0;
        b->blk = nk;
        k = nk;
    }
    p = (unsigned char *)(((uintptr_t)(k + 1) + 15) & ~(uintptr_t)15) + k->used;
    k->used += n;
    memset(p, 0, n);
    return p;
}

static void afreeAll(Bk *b) {
    int i;
    for (i = 0; i < b->njson; i++) extJsonFree(b->json[i]);
    free(b->ex.gltf);
    free(b->ex.bin);
    free(b->ex.png);
    free(b->ex.json);
    while (b->blk != NULL) {
        Blk *n = b->blk->next;
        free(b->blk);
        b->blk = n;
    }
}

static void fail(Bk *b, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
static void fail(Bk *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b->err, sizeof b->err, fmt, ap);
    va_end(ap);
    longjmp(b->jb, 1);
}

static void note(Bk *b, int warn, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void note(Bk *b, int warn, const char *fmt, ...) {
    char s[400];
    va_list ap;
    int i;
    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    if (warn) {
        for (i = 0; i < b->nseen; i++) if (strcmp(b->seen[i], s) == 0) return;
        if (b->nseen < 128) {
            b->seen[b->nseen] = (char *)aalloc(b, strlen(s) + 1);
            strcpy(b->seen[b->nseen++], s);
        }
    }
    if (b->log != NULL) b->log(b->ud, warn, s);
}

/* a growable array of anything: {p, n, cap} */
typedef struct Vec {
    void *p;
    int n, cap;
} Vec;

static void *vpush(Bk *b, Vec *v, size_t esz) {
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        void *np = aalloc(b, (size_t)cap * esz);
        if (v->n) memcpy(np, v->p, (size_t)v->n * esz);
        v->p = np;
        v->cap = cap;
    }
    memset((unsigned char *)v->p + (size_t)v->n * esz, 0, esz);
    return (unsigned char *)v->p + (size_t)v->n++ * esz;
}

typedef struct IVec {
    int *p;
    int n, cap;
} IVec;

static void ipush(Bk *b, IVec *v, int x) {
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        int *np = (int *)aalloc(b, (size_t)cap * sizeof(int));
        if (v->n) memcpy(np, v->p, (size_t)v->n * sizeof(int));
        v->p = np;
        v->cap = cap;
    }
    v->p[v->n++] = x;
}

/* a byte buffer */
typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
} Buf;

static void bgrow(Bk *b, Buf *x, size_t more) {
    if (x->n + more > x->cap) {
        size_t cap = x->cap ? x->cap : 256;
        uint8_t *np;
        while (cap < x->n + more) cap *= 2;
        np = (uint8_t *)aalloc(b, cap);
        if (x->n) memcpy(np, x->p, x->n);
        x->p = np;
        x->cap = cap;
    }
}

static void b8(Bk *b, Buf *x, unsigned v) {
    bgrow(b, x, 1);
    x->p[x->n++] = (uint8_t)v;
}

static void b16(Bk *b, Buf *x, unsigned v) {
    b8(b, x, v & 255);
    b8(b, x, (v >> 8) & 255);
}

static void b32(Bk *b, Buf *x, unsigned long v) {
    b16(b, x, (unsigned)(v & 0xFFFF));
    b16(b, x, (unsigned)((v >> 16) & 0xFFFF));
}

static void bput(Bk *b, Buf *x, const void *s, size_t n) {
    bgrow(b, x, n);
    if (n) memcpy(x->p + x->n, s, n);
    x->n += n;
}

static void bpad(Bk *b, Buf *x, size_t n) {
    while (x->n % n) b8(b, x, 0);
}

/* a stable merge sort with a context (Python's sort is stable and the reference relies on it) */
static void msortRec(unsigned char *a, unsigned char *tmp, size_t n, size_t esz, int (*cmp)(const void *, const void *, void *), void *ctx) {
    size_t h = n / 2, i = 0, j, k = 0;
    if (n < 2) return;
    msortRec(a, tmp, h, esz, cmp, ctx);
    msortRec(a + h * esz, tmp, n - h, esz, cmp, ctx);
    j = h;
    while (i < h && j < n) {
        if (cmp(a + j * esz, a + i * esz, ctx) < 0) {
            memcpy(tmp + k++ * esz, a + j++ * esz, esz);
        } else {
            memcpy(tmp + k++ * esz, a + i++ * esz, esz);
        }
    }
    while (i < h) memcpy(tmp + k++ * esz, a + i++ * esz, esz);
    while (j < n) memcpy(tmp + k++ * esz, a + j++ * esz, esz);
    memcpy(a, tmp, n * esz);
}

static void msortStable(Bk *b, void *a, size_t n, size_t esz, int (*cmp)(const void *, const void *, void *), void *ctx) {
    unsigned char *tmp;
    if (n < 2) return;
    if (n > ((size_t)1 << 28)) fail(b, "too many items to sort");
    tmp = (unsigned char *)malloc(n * esz);
    if (tmp == NULL) fail(b, "out of memory");
    msortRec((unsigned char *)a, tmp, n, esz, cmp, ctx);
    free(tmp);
}

/* ---------------------------------------------------------------- JSON helpers */

static const ExtJson *jget(const ExtJson *o, const char *k) {
    return o != NULL ? extJsonGet(o, k) : NULL;
}

static int isNum(const ExtJson *j) { return j != NULL && j->t == 0; }
static int isStr(const ExtJson *j) { return j != NULL && j->t == 1 && j->str != NULL; }
static int isObj(const ExtJson *j) { return j != NULL && j->t == 2; }
static int isArr(const ExtJson *j) { return j != NULL && j->t == 4; }

static double jnum(const ExtJson *o, const char *k, double d) {
    const ExtJson *v = jget(o, k);
    return isNum(v) ? v->num : d;
}

static int jint(Bk *b, const ExtJson *o, const char *k, int d) {
    const ExtJson *v = jget(o, k);
    if (v == NULL) return d;
    if (!isNum(v) || v->num != floor(v->num) || fabs(v->num) > 2147483647.0) fail(b, "\"%s\" must be a whole number", k);
    return (int)v->num;
}

static const ExtJson *needArr(Bk *b, const ExtJson *o, const char *k) {
    const ExtJson *a = jget(o, k);
    if (a != NULL && !isArr(a)) fail(b, "glTF: \"%s\" is not a list", k);
    return a;
}

static const ExtJson *elem(Bk *b, const ExtJson *arr, int i, const char *what) {
    if (arr == NULL || i < 0 || i >= arr->n) fail(b, "glTF: %s %d does not exist", what, i);
    return &arr->v[i];
}

static int idxOf(Bk *b, const ExtJson *v, const char *what) {
    if (!isNum(v) || v->num != floor(v->num) || v->num < 0 || v->num > 1e9) fail(b, "glTF: %s is not an index", what);
    return (int)v->num;
}

/* ---------------------------------------------------------------- files, base64 */

typedef struct Bytes {
    uint8_t *p;
    size_t n;
} Bytes;

static int relOk(const char *rel) {
    return !(rel[0] == '\0' || rel[0] == '/' || rel[0] == '\\' || strstr(rel, "..") != NULL || strchr(rel, ':') != NULL || strchr(rel, '\\') != NULL);
}

/* the original fighter's files (what the exporter names them), built on first use: the disc is read here, on the calling thread */
static void loadOriginal(Bk *b) {
    char err[256];
    if (b->exLoaded) return;
    if (b->orig == NULL || b->orig->build == NULL) fail(b, "\"from\": the original models need the disc, which is not available");
    err[0] = '\0';
    if (b->orig->build(b->orig->ud, b->fromId, &b->ex, err, sizeof err) != 0) fail(b, "\"from\": dw3:model/%d: %s", b->fromId, err);
    b->exLoaded = 1;
}

/* a file the mod's folder lacks, taken from the original when it is one of the exported four (model.json is never: it is merged) */
static int originalFile(Bk *b, const char *rel, Bytes *r) {
    const uint8_t *p;
    size_t n;
    if (b->fromId < 0) return 0;
    if (strcmp(rel, "model.gltf") != 0 && strcmp(rel, "model.bin") != 0 && strcmp(rel, "model.png") != 0) return 0;
    b->fellBack = 1;
    if (b->dry) {
        r->p = (uint8_t *)aalloc(b, 1);
        r->n = 0;
        return 1;
    }
    loadOriginal(b);
    p = rel[6] == 'g' ? b->ex.gltf : rel[6] == 'b' ? b->ex.bin : b->ex.png;
    n = rel[6] == 'g' ? b->ex.gltfLen : rel[6] == 'b' ? b->ex.binLen : b->ex.pngLen;
    r->p = (uint8_t *)aalloc(b, n + 1);
    r->n = n;
    memcpy(r->p, p, n);
    return 1;
}

static Bytes readFile(Bk *b, const char *dir, const char *rel, const char *what) {
    char path[PP_CAP];
    FILE *f;
    long size;
    Bytes r;
    b->fellBack = 0;
    if (!relOk(rel)) fail(b, "%s \"%s\": a path inside the model's folder (no \"..\", no drive)", what, rel);
    if (!portJoin(path, sizeof path, dir, rel)) fail(b, "%s \"%s\": path too long", what, rel);
    f = fopen(path, "rb");
    if (f == NULL) {
        if (originalFile(b, rel, &r)) return r;
        fail(b, "%s file not found: %s", what, rel);
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || size > FILE_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fail(b, "%s file \"%s\" is empty or too big", what, rel);
    }
    r.p = (uint8_t *)aalloc(b, (size_t)size + 1);
    r.n = (size_t)size;
    if (fread(r.p, 1, r.n, f) != r.n) {
        fclose(f);
        fail(b, "%s file \"%s\" could not be read", what, rel);
    }
    fclose(f);
    return r;
}

static Bytes b64decode(Bk *b, const char *s) {
    Bytes r;
    size_t n = strlen(s), i, o = 0;
    unsigned acc = 0;
    int bits = 0;
    r.p = (uint8_t *)aalloc(b, n / 4 * 3 + 4);
    for (i = 0; i < n; i++) {
        int c = (unsigned char)s[i], v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else fail(b, "glTF: a data URI is not valid base64");
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            r.p[o++] = (uint8_t)(acc >> bits);
            acc &= (1u << bits) - 1;
        }
    }
    r.n = o;
    return r;
}

/* a URI of a buffer or an image: a data URI or a file name next to the glTF (percent escapes decoded) */
static Bytes loadUri(Bk *b, const char *dir, const char *uri, const char *what) {
    char name[PP_CAP];
    size_t i, o = 0;
    Bytes r;
    if (strncmp(uri, "data:", 5) == 0) {
        const char *c = strchr(uri, ',');
        if (c == NULL) fail(b, "glTF: a data URI of a %s has no data", what);
        r = b64decode(b, c + 1);
        return r;
    }
    for (i = 0; uri[i] != '\0' && o < sizeof name - 1; i++) {
        if (uri[i] == '%' && isxdigit((unsigned char)uri[i + 1]) && isxdigit((unsigned char)uri[i + 2])) {
            char h[3];
            h[0] = uri[i + 1];
            h[1] = uri[i + 2];
            h[2] = '\0';
            name[o++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else {
            name[o++] = uri[i];
        }
    }
    name[o] = '\0';
    r = readFile(b, dir, name, what);
    if (!b->fellBack) hashBytes(b, r.p, r.n);
    return r;
}

/* ---------------------------------------------------------------- PNG reader (the reference's read_png) */

typedef struct Img {
    int w, h;
    uint8_t *rgba;
} Img;

static unsigned be32(const uint8_t *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static Img readPng(Bk *b, const uint8_t *data, size_t len) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    size_t pos = 8, idatN = 0, idatCap = 0, stride, rawN, o;
    uint8_t *idat = NULL, *raw, *rows, *prev, *line;
    const uint8_t *plte = NULL, *trns = NULL;
    size_t plteN = 0, trnsN = 0;
    unsigned w = 0, h = 0;
    int depth = 0, ctype = 0, interlace = 0, have = 0, chans, bits, bpp, y, x;
    Img im;
    size_t outLen = 0;

    if (len < 8 || memcmp(data, sig, 8) != 0) fail(b, "the texture is not a PNG (convert it to PNG)");
    while (pos + 8 <= len) {
        unsigned ln = be32(data + pos);
        const uint8_t *tag = data + pos + 4, *body = data + pos + 8;
        if ((size_t)ln > len - pos - 8) ln = (unsigned)(len - pos - 8);
        if (memcmp(tag, "IHDR", 4) == 0 && ln >= 13) {
            w = be32(body);
            h = be32(body + 4);
            depth = body[8];
            ctype = body[9];
            interlace = body[12];
            have = 1;
        } else if (memcmp(tag, "PLTE", 4) == 0) {
            plte = body;
            plteN = ln;
        } else if (memcmp(tag, "tRNS", 4) == 0) {
            trns = body;
            trnsN = ln;
        } else if (memcmp(tag, "IDAT", 4) == 0) {
            if (idatN + ln > idatCap) {
                size_t cap = idatCap ? idatCap : 4096;
                uint8_t *ni;
                while (cap < idatN + ln) cap *= 2;
                ni = (uint8_t *)aalloc(b, cap);
                if (idatN) memcpy(ni, idat, idatN);
                idat = ni;
                idatCap = cap;
            }
            if (ln) memcpy(idat + idatN, body, ln);
            idatN += ln;
        } else if (memcmp(tag, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + (size_t)ln;
    }
    if (!have) fail(b, "PNG without header");
    if (interlace) fail(b, "interlaced PNG is not supported (save it without interlacing)");
    chans = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (chans == 0 || (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16))
        fail(b, "PNG colour type %d / depth %d is not supported", ctype, depth);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) fail(b, "PNG size %ux%u is not supported (1..8192)", w, h);
    if (depth < 8 && chans > 1) fail(b, "PNG colour type %d / depth %d is not supported", ctype, depth);
    bits = depth * chans;
    stride = ((size_t)w * (size_t)bits + 7) / 8;
    bpp = bits / 8 > 1 ? bits / 8 : 1;
    rawN = (size_t)h * (1 + stride);
    raw = (uint8_t *)aalloc(b, rawN + 8);
    if (idatN < 6 || pngInflate(idat + 2, idatN - 2, raw, rawN, &outLen) != 0 || outLen < rawN) fail(b, "the PNG data is corrupt");
    rows = (uint8_t *)aalloc(b, (size_t)h * stride);
    prev = (uint8_t *)aalloc(b, stride);
    for (y = 0; y < (int)h; y++) {
        int ft = raw[(size_t)y * (1 + stride)];
        size_t i;
        line = rows + (size_t)y * stride;
        memcpy(line, raw + (size_t)y * (1 + stride) + 1, stride);
        if (ft == 1) {
            for (i = (size_t)bpp; i < stride; i++) line[i] = (uint8_t)(line[i] + line[i - (size_t)bpp]);
        } else if (ft == 2) {
            for (i = 0; i < stride; i++) line[i] = (uint8_t)(line[i] + prev[i]);
        } else if (ft == 3) {
            for (i = 0; i < stride; i++) {
                unsigned left = i >= (size_t)bpp ? line[i - (size_t)bpp] : 0;
                line[i] = (uint8_t)(line[i] + ((left + prev[i]) >> 1));
            }
        } else if (ft == 4) {
            for (i = 0; i < stride; i++) {
                int a = i >= (size_t)bpp ? line[i - (size_t)bpp] : 0, bb = prev[i], c = i >= (size_t)bpp ? prev[i - (size_t)bpp] : 0;
                int p = a + bb - c, pa = abs(p - a), pb = abs(p - bb), pc = abs(p - c);
                int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? bb : c);
                line[i] = (uint8_t)(line[i] + pr);
            }
        } else if (ft != 0) {
            fail(b, "PNG: unknown filter %d", ft);
        }
        memcpy(prev, line, stride);
    }
    im.w = (int)w;
    im.h = (int)h;
    im.rgba = (uint8_t *)aalloc(b, (size_t)w * h * 4);
    for (y = 0; y < (int)h; y++) {
        const uint8_t *ln = rows + (size_t)y * stride;
        o = (size_t)y * w * 4;
        for (x = 0; x < (int)w; x++) {
            unsigned s[4] = {0, 0, 0, 255};
            uint8_t *d = im.rgba + o + 4 * (size_t)x;
            if (depth == 16) {
                int c;
                for (c = 0; c < chans; c++) s[c] = ln[(size_t)(x * chans + c) * 2];
            } else if (depth < 8) {
                unsigned mask = (1u << depth) - 1;
                size_t bit = (size_t)x * (size_t)depth;
                unsigned v = (ln[bit / 8] >> (8 - depth - (int)(bit % 8))) & mask;
                s[0] = ctype == 0 ? v * 255 / mask : v;
            } else {
                int c;
                for (c = 0; c < chans; c++) s[c] = ln[(size_t)(x * chans + c)];
            }
            if (ctype == 6) {
                d[0] = (uint8_t)s[0]; d[1] = (uint8_t)s[1]; d[2] = (uint8_t)s[2]; d[3] = (uint8_t)s[3];
            } else if (ctype == 2) {
                d[0] = (uint8_t)s[0]; d[1] = (uint8_t)s[1]; d[2] = (uint8_t)s[2]; d[3] = 255;
            } else if (ctype == 0) {
                d[0] = d[1] = d[2] = (uint8_t)s[0]; d[3] = 255;
            } else if (ctype == 4) {
                d[0] = d[1] = d[2] = (uint8_t)s[0]; d[3] = (uint8_t)s[1];
            } else {
                unsigned i = s[0];
                if (plte == NULL) fail(b, "palette PNG without PLTE");
                if (3 * (size_t)i + 2 >= plteN) fail(b, "palette PNG: index outside PLTE");
                d[0] = plte[3 * i]; d[1] = plte[3 * i + 1]; d[2] = plte[3 * i + 2];
                d[3] = (trns != NULL && i < trnsN) ? trns[i] : 255;
            }
        }
    }
    return im;
}

/* ---------------------------------------------------------------- glTF reader */

typedef struct Gltf {
    const ExtJson *js;
    Bytes *bufs;
    int nbuf;
    Bytes *imgRaw;
    Img *img;
    int *imgDone;
    int nimg;
    const ExtJson *nodes, *meshes, *accessors, *views, *skins, *materials, *textures, *images, *anims;
    const char *dir;
} Gltf;

static int le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* one accessor: n elements of nc doubles (normalized integers divided in double, as the reference does) */
static double *accessor(Bk *b, Gltf *g, int ai, int *nOut, int *ncOut) {
    const ExtJson *a = elem(b, g->accessors, ai, "accessor");
    const ExtJson *ty = jget(a, "type"), *vw;
    static const char *const tn[7] = {"SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"};
    static const int tc[7] = {1, 2, 3, 4, 4, 9, 16};
    int n, nc = 0, ct, size, i, k, normalized;
    double *out, norm = 0;
    size_t stride, off;
    const Bytes *buf;

    if (jget(a, "sparse") != NULL) fail(b, "sparse accessors are not supported (export without sparse storage)");
    if (!isStr(ty)) fail(b, "glTF: accessor %d has no type", ai);
    for (i = 0; i < 7; i++) if (strcmp(ty->str, tn[i]) == 0) nc = tc[i];
    if (nc == 0) fail(b, "glTF: accessor %d has an unknown type", ai);
    n = jint(b, a, "count", -1);
    ct = jint(b, a, "componentType", -1);
    if (n < 0 || n > 4000000) fail(b, "glTF: accessor %d has a bad count", ai);
    size = (ct == 5120 || ct == 5121) ? 1 : (ct == 5122 || ct == 5123) ? 2 : (ct == 5125 || ct == 5126) ? 4 : 0;
    if (size == 0) fail(b, "glTF: accessor %d has an unknown componentType", ai);
    out = (double *)aalloc(b, (size_t)(n ? n : 1) * (size_t)nc * sizeof(double));
    *nOut = n;
    *ncOut = nc;
    vw = jget(a, "bufferView");
    if (vw == NULL) return out;
    {
        const ExtJson *v = elem(b, g->views, idxOf(b, vw, "bufferView"), "bufferView");
        int bi = jint(b, v, "buffer", -1);
        const ExtJson *nm = jget(a, "normalized");
        if (bi < 0 || bi >= g->nbuf) fail(b, "glTF: a bufferView names a buffer that does not exist");
        buf = &g->bufs[bi];
        off = (size_t)jnum(v, "byteOffset", 0) + (size_t)jnum(a, "byteOffset", 0);
        stride = (size_t)jnum(v, "byteStride", 0);
        if (stride == 0) stride = (size_t)nc * (size_t)size;
        normalized = nm != NULL && nm->num != 0;
        if (n > 0 && off + (size_t)(n - 1) * stride + (size_t)nc * (size_t)size > buf->n) fail(b, "glTF: accessor %d reads past the end of its buffer", ai);
        norm = ct == 5120 ? 127.0 : ct == 5121 ? 255.0 : ct == 5122 ? 32767.0 : ct == 5123 ? 65535.0 : 0.0;
        for (i = 0; i < n; i++) {
            const uint8_t *p = buf->p + off + (size_t)i * stride;
            for (k = 0; k < nc; k++) {
                const uint8_t *q = p + (size_t)k * (size_t)size;
                double x;
                if (ct == 5120) x = (signed char)q[0];
                else if (ct == 5121) x = q[0];
                else if (ct == 5122) x = (int16_t)le16(q);
                else if (ct == 5123) x = le16(q);
                else if (ct == 5125) x = (double)((uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24));
                else {
                    uint32_t u = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
                    float f;
                    memcpy(&f, &u, 4);
                    x = (double)f;
                }
                if (normalized && norm != 0.0) {
                    x = x / norm;
                    if (x < -1.0) x = -1.0;
                }
                out[(size_t)i * (size_t)nc + (size_t)k] = x;
            }
        }
    }
    return out;
}

static Img *gltfImage(Bk *b, Gltf *g, int ii) {
    if (ii < 0 || ii >= g->nimg) fail(b, "glTF: image %d does not exist", ii);
    if (!g->imgDone[ii]) {
        const ExtJson *im = &g->images->v[ii];
        const ExtJson *uri = jget(im, "uri");
        Bytes d;
        if (isStr(uri)) {
            d = g->imgRaw[ii];
        } else {
            const ExtJson *v = elem(b, g->views, jint(b, im, "bufferView", -1), "bufferView");
            int bi = jint(b, v, "buffer", -1);
            size_t off = (size_t)jnum(v, "byteOffset", 0), len = (size_t)jnum(v, "byteLength", 0);
            if (bi < 0 || bi >= g->nbuf || off + len > g->bufs[bi].n) fail(b, "glTF: an image bufferView is out of range");
            d.p = g->bufs[bi].p + off;
            d.n = len;
        }
        g->img[ii] = readPng(b, d.p, d.n);
        g->imgDone[ii] = 1;
    }
    return &g->img[ii];
}

static uint32_t le32p(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* the glTF bytes (.gltf JSON or .glb) -> a reader; every buffer and image file is read (and hashed) here */
static Gltf *loadGltf(Bk *b, const char *dir, const char *rel) {
    Bytes data = readFile(b, dir, rel, "glTF");
    Gltf *g = (Gltf *)aalloc(b, sizeof *g);
    ExtJson *js = (ExtJson *)aalloc(b, sizeof *js);
    const uint8_t *jsonP = data.p;
    size_t jsonN = data.n;
    Bytes bin = {NULL, 0};
    const ExtJson *bl, *asset;
    int i;

    if (!b->fellBack) hashBytes(b, data.p, data.n);
    if (data.n >= 12 && memcmp(data.p, "glTF", 4) == 0) {
        size_t off = 12, length = le32p(data.p + 8);
        int haveJson = 0;
        if (length > data.n) length = data.n;
        while (off + 8 <= length) {
            size_t clen = le32p(data.p + off);
            uint32_t ctype = le32p(data.p + off + 4);
            if (clen > length - off - 8) clen = length - off - 8;
            if (ctype == 0x4E4F534Au) {
                jsonP = data.p + off + 8;
                jsonN = clen;
                haveJson = 1;
            } else if (ctype == 0x004E4942u) {
                bin.p = data.p + off + 8;
                bin.n = clen;
            }
            off += 8 + clen;
        }
        if (!haveJson) fail(b, "glb without a JSON chunk");
    }
    if (extJsonParse(jsonP, jsonN, js) != 0) fail(b, "the glTF is not valid JSON");
    b->json[b->njson++] = js;
    g->js = js;
    g->dir = dir;
    asset = jget(js, "asset");
    if (asset != NULL && isStr(jget(asset, "version")) && jget(asset, "version")->str[0] != '2') fail(b, "only glTF 2.0 is supported");
    g->nodes = needArr(b, js, "nodes");
    g->meshes = needArr(b, js, "meshes");
    g->accessors = needArr(b, js, "accessors");
    g->views = needArr(b, js, "bufferViews");
    g->skins = needArr(b, js, "skins");
    g->materials = needArr(b, js, "materials");
    g->textures = needArr(b, js, "textures");
    g->images = needArr(b, js, "images");
    g->anims = needArr(b, js, "animations");
    bl = needArr(b, js, "buffers");
    g->nbuf = bl != NULL ? bl->n : 0;
    g->bufs = (Bytes *)aalloc(b, (size_t)(g->nbuf + 1) * sizeof(Bytes));
    for (i = 0; i < g->nbuf; i++) {
        const ExtJson *uri = jget(&bl->v[i], "uri");
        if (uri == NULL) g->bufs[i] = bin;
        else if (isStr(uri)) {
            g->bufs[i] = loadUri(b, dir, uri->str, "buffer");
            if (strncmp(uri->str, "data:", 5) == 0) hashBytes(b, g->bufs[i].p, g->bufs[i].n);
        } else fail(b, "glTF: a buffer has a bad uri");
    }
    g->nimg = g->images != NULL ? g->images->n : 0;
    g->imgRaw = (Bytes *)aalloc(b, (size_t)(g->nimg + 1) * sizeof(Bytes));
    g->img = (Img *)aalloc(b, (size_t)(g->nimg + 1) * sizeof(Img));
    g->imgDone = (int *)aalloc(b, (size_t)(g->nimg + 1) * sizeof(int));
    for (i = 0; i < g->nimg; i++) {
        const ExtJson *uri = jget(&g->images->v[i], "uri");
        if (isStr(uri)) {
            g->imgRaw[i] = loadUri(b, dir, uri->str, "texture");
            if (strncmp(uri->str, "data:", 5) == 0) hashBytes(b, g->imgRaw[i].p, g->imgRaw[i].n);
        }
    }
    return g;
}

/* ---------------------------------------------------------------- small math (plain left-to-right sums, as the reference) */

typedef double M3[3][3];

typedef struct Aff {
    M3 m;
    double t[3];
} Aff;

static double vlen3(const double *v) { return sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]); }
static double vlen4(const double *v) { return sqrt(((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]) + v[3] * v[3]); }

static void matmul3(M3 r, const M3 a, const M3 b) {
    M3 o;
    int i, j;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) o[i][j] = (a[i][0] * b[0][j] + a[i][1] * b[1][j]) + a[i][2] * b[2][j];
    memcpy(r, o, sizeof o);
}

static void matvec3(double *r, const M3 a, const double *v) {
    double o[3];
    int i;
    for (i = 0; i < 3; i++) o[i] = (a[i][0] * v[0] + a[i][1] * v[1]) + a[i][2] * v[2];
    memcpy(r, o, sizeof o);
}

static double det3(const M3 a) {
    return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
}

/* 0 when singular */
static int inv3(M3 c, const M3 a) {
    double d = det3(a);
    int i, j;
    M3 o;
    if (fabs(d) < 1e-12) return 0;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            int r[2], q[2], x, nr = 0, nq = 0;
            double m, sgn = ((i + j) & 1) ? -1.0 : 1.0;
            for (x = 0; x < 3; x++) {
                if (x != i) r[nr++] = x;
                if (x != j) q[nq++] = x;
            }
            m = a[r[0]][q[0]] * a[r[1]][q[1]] - a[r[0]][q[1]] * a[r[1]][q[0]];
            o[j][i] = sgn * m / d;
        }
    }
    memcpy(c, o, sizeof o);
    return 1;
}

static void quatMatrix(M3 r, const double *q) {
    double x = q[0], y = q[1], z = q[2], w = q[3];
    r[0][0] = 1 - 2 * (y * y + z * z); r[0][1] = 2 * (x * y - z * w); r[0][2] = 2 * (x * z + y * w);
    r[1][0] = 2 * (x * y + z * w); r[1][1] = 1 - 2 * (x * x + z * z); r[1][2] = 2 * (y * z - x * w);
    r[2][0] = 2 * (x * z - y * w); r[2][1] = 2 * (y * z + x * w); r[2][2] = 1 - 2 * (x * x + y * y);
}

static void quatFromMatrix(double *out, const M3 m) {
    double t = (m[0][0] + m[1][1]) + m[2][2], s, q[4], n;
    int i;
    if (t > 0) {
        s = sqrt(t + 1.0) * 2;
        q[0] = (m[2][1] - m[1][2]) / s; q[1] = (m[0][2] - m[2][0]) / s; q[2] = (m[1][0] - m[0][1]) / s; q[3] = 0.25 * s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        s = sqrt(((1.0 + m[0][0]) - m[1][1]) - m[2][2]) * 2;
        q[0] = 0.25 * s; q[1] = (m[0][1] + m[1][0]) / s; q[2] = (m[0][2] + m[2][0]) / s; q[3] = (m[2][1] - m[1][2]) / s;
    } else if (m[1][1] > m[2][2]) {
        s = sqrt(((1.0 + m[1][1]) - m[0][0]) - m[2][2]) * 2;
        q[0] = (m[0][1] + m[1][0]) / s; q[1] = 0.25 * s; q[2] = (m[1][2] + m[2][1]) / s; q[3] = (m[0][2] - m[2][0]) / s;
    } else {
        s = sqrt(((1.0 + m[2][2]) - m[0][0]) - m[1][1]) * 2;
        q[0] = (m[0][2] + m[2][0]) / s; q[1] = (m[1][2] + m[2][1]) / s; q[2] = 0.25 * s; q[3] = (m[1][0] - m[0][1]) / s;
    }
    n = sqrt(((q[0] * q[0] + q[1] * q[1]) + q[2] * q[2]) + q[3] * q[3]);
    for (i = 0; i < 4; i++) out[i] = q[i] / n;
}

static void affIdentity(Aff *a) {
    memset(a, 0, sizeof *a);
    a->m[0][0] = a->m[1][1] = a->m[2][2] = 1.0;
}

/* (M1, t1) * (M2, t2) */
static void compose(Aff *r, const Aff *a, const Aff *b) {
    Aff o;
    double v[3];
    int i;
    matmul3(o.m, a->m, b->m);
    matvec3(v, a->m, b->t);
    for (i = 0; i < 3; i++) o.t[i] = v[i] + a->t[i];
    *r = o;
}

static void decomposeMatrix(const double *m16, double *t, double *q, double *s) {
    double cols[3][3], rot[3][3];
    int i, j;
    t[0] = m16[12]; t[1] = m16[13]; t[2] = m16[14];
    for (j = 0; j < 3; j++) for (i = 0; i < 3; i++) cols[j][i] = m16[4 * j + i];
    for (j = 0; j < 3; j++) {
        s[j] = vlen3(cols[j]);
        if (s[j] == 0.0) s[j] = 1.0;
    }
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) rot[i][j] = cols[j][i] / s[j];
    if (det3(rot) < 0) {
        s[0] = -s[0];
        for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) rot[i][j] = cols[j][i] / s[j];
    }
    quatFromMatrix(q, rot);
}

#define RAW (4096.0 / (2 * PI_))

/* the two (rx, ry, rz) radian triples with rotzyx == m */
static int eulerCandidates(double c[2][3], const M3 m) {
    double sy = -m[2][0], b;
    if (sy > 1.0) sy = 1.0;
    if (sy < -1.0) sy = -1.0;
    b = asin(sy);
    if (fabs(sy) < 0.999999) {
        double a = atan2(m[2][1], m[2][2]), cc = atan2(m[1][0], m[0][0]);
        c[0][0] = a; c[0][1] = b; c[0][2] = cc;
        c[1][0] = a + PI_; c[1][1] = PI_ - b; c[1][2] = cc + PI_;
        return 2;
    }
    c[0][0] = 0.0; c[0][1] = b; c[0][2] = atan2(-m[0][1], m[1][1]);
    return 1;
}

/* raw (4096 = 360 degrees) angles of rotation matrix m, the equivalent triple closest to prev (NULL: around 0) */
static void rawAngles(double *res, const M3 m, const double *prev) {
    double cand[2][3], best[3] = {0, 0, 0}, bd = 0;
    int n = eulerCandidates(cand, m), c, i, have = 0;
    for (c = 0; c < n; c++) {
        double out[3], d = 0.0;
        for (i = 0; i < 3; i++) {
            double r = cand[c][i] * RAW, ref = prev != NULL ? prev[i] : 0.0;
            r += 4096.0 * rint((ref - r) / 4096.0);
            out[i] = r;
        }
        for (i = 0; i < 3; i++) d += fabs(out[i] - (prev != NULL ? prev[i] : 0.0));
        if (!have || d < bd) {
            memcpy(best, out, sizeof best);
            bd = d;
            have = 1;
        }
    }
    memcpy(res, best, sizeof best);
}

static void slerp(double *out, const double *a0, const double *b0, double u) {
    double b[4], d, q[4], n;
    int i;
    d = ((a0[0] * b0[0] + a0[1] * b0[1]) + a0[2] * b0[2]) + a0[3] * b0[3];
    for (i = 0; i < 4; i++) b[i] = b0[i];
    if (d < 0) {
        for (i = 0; i < 4; i++) b[i] = -b[i];
        d = -d;
    }
    if (d > 0.9995) {
        for (i = 0; i < 4; i++) q[i] = a0[i] + (b[i] - a0[i]) * u;
    } else {
        double th = acos(d), sa = sin((1 - u) * th), sb = sin(u * th), st = sin(th);
        for (i = 0; i < 4; i++) q[i] = (a0[i] * sa + b[i] * sb) / st;
    }
    n = vlen4(q);
    if (n == 0.0) n = 1.0;
    for (i = 0; i < 4; i++) out[i] = q[i] / n;
}

/* Python's float // (CPython float_floor_div) */
static double pyFloorDiv(double x, double w) {
    double mod = fmod(x, w), div = (x - mod) / w, fd;
    if (mod != 0.0 && ((w < 0) != (mod < 0))) {
        mod += w;
        div -= 1.0;
    }
    if (div != 0.0) {
        fd = floor(div);
        if (div - fd > 0.5) fd += 1.0;
    } else {
        fd = copysign(0.0, x / w);
    }
    return fd;
}

static long ifloorDiv(long a, long b) {
    long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

static long iceilDiv(long a, long b) { return -ifloorDiv(-a, b); }

/* ---------------------------------------------------------------- samplers */

enum { INTERP_LINEAR, INTERP_STEP, INTERP_CUBIC };

typedef struct Sampler {
    int n, nc, interp;
    double *times;
    double *vals;
} Sampler;

static const double *sval(const Sampler *s, int i) {
    return s->interp == INTERP_CUBIC ? s->vals + (size_t)(3 * i + 1) * (size_t)s->nc : s->vals + (size_t)i * (size_t)s->nc;
}

static void evalSampler(double *out, const Sampler *sm, double t, int isQuat) {
    int n = sm->n, nc = sm->nc, k, lo, hi, i;
    double dt, u;
    const double *a, *b;
    if (n == 1 || t <= sm->times[0]) {
        memcpy(out, sval(sm, 0), (size_t)nc * sizeof(double));
        return;
    }
    if (t >= sm->times[n - 1]) {
        memcpy(out, sval(sm, n - 1), (size_t)nc * sizeof(double));
        return;
    }
    /* bisect_right(times, t) - 1 */
    lo = 0;
    hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (t < sm->times[mid]) hi = mid;
        else lo = mid + 1;
    }
    i = lo - 1;
    dt = sm->times[i + 1] - sm->times[i];
    if (dt == 0.0) {
        memcpy(out, sval(sm, i), (size_t)nc * sizeof(double));
        return;
    }
    u = (t - sm->times[i]) / dt;
    if (sm->interp == INTERP_STEP) {
        memcpy(out, sval(sm, i), (size_t)nc * sizeof(double));
        return;
    }
    if (sm->interp == INTERP_CUBIC) {
        const double *v0 = sval(sm, i), *v1 = sval(sm, i + 1);
        const double *m0 = sm->vals + (size_t)(3 * i + 2) * (size_t)nc, *m1 = sm->vals + (size_t)(3 * (i + 1)) * (size_t)nc;
        double s2 = u * u, s3 = u * u * u;
        for (k = 0; k < nc; k++)
            out[k] = (((2 * s3 - 3 * s2 + 1) * v0[k] + (s3 - 2 * s2 + u) * dt * m0[k]) + (-2 * s3 + 3 * s2) * v1[k]) + (s3 - s2) * dt * m1[k];
        if (isQuat) {
            double n2 = vlen4(out);
            if (n2 == 0.0) n2 = 1.0;
            for (k = 0; k < 4; k++) out[k] = out[k] / n2;
        }
        return;
    }
    a = sval(sm, i);
    b = sval(sm, i + 1);
    if (isQuat) {
        slerp(out, a, b, u);
        return;
    }
    for (k = 0; k < nc; k++) out[k] = a[k] + (b[k] - a[k]) * u;
}

/* ---------------------------------------------------------------- scene: nodes, rest pose */

typedef struct Rest {
    double t[3], q[4], s[3];
} Rest;

typedef struct Scene {
    int nn;
    int *parent;
    char *reach, *animated;
    Rest *rest;
    IVec *children;
} Scene;

static void restTrs(Bk *b, Gltf *g, int i, Rest *r) {
    const ExtJson *n = &g->nodes->v[i], *m = jget(n, "matrix"), *x;
    int k;
    if (m != NULL) {
        double m16[16];
        if (!isArr(m) || m->n != 16) fail(b, "glTF: node %d has a bad matrix", i);
        for (k = 0; k < 16; k++) m16[k] = isNum(&m->v[k]) ? m->v[k].num : 0.0;
        decomposeMatrix(m16, r->t, r->q, r->s);
        return;
    }
    r->t[0] = r->t[1] = r->t[2] = 0.0;
    r->s[0] = r->s[1] = r->s[2] = 1.0;
    r->q[0] = r->q[1] = r->q[2] = 0.0;
    r->q[3] = 1.0;
    if ((x = jget(n, "translation")) != NULL) {
        if (!isArr(x) || x->n != 3) fail(b, "glTF: node %d has a bad translation", i);
        for (k = 0; k < 3; k++) r->t[k] = x->v[k].num;
    }
    if ((x = jget(n, "scale")) != NULL) {
        if (!isArr(x) || x->n != 3) fail(b, "glTF: node %d has a bad scale", i);
        for (k = 0; k < 3; k++) r->s[k] = x->v[k].num;
    }
    if ((x = jget(n, "rotation")) != NULL) {
        double ql;
        if (!isArr(x) || x->n != 4) fail(b, "glTF: node %d has a bad rotation", i);
        for (k = 0; k < 4; k++) r->q[k] = x->v[k].num;
        ql = vlen4(r->q);
        if (ql == 0.0) ql = 1.0;
        for (k = 0; k < 4; k++) r->q[k] = r->q[k] / ql;
    }
}

static void localMatrix(Aff *a, const Rest *r) {
    M3 q;
    int i, j;
    quatMatrix(q, r->q);
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) a->m[i][j] = q[i][j] * r->s[j];
    a->t[0] = r->t[0]; a->t[1] = r->t[1]; a->t[2] = r->t[2];
}

/* the ancestors of i from the topmost down, not i; count in *n */
static int ancestors(const Scene *sc, int i, int *out) {
    int n = 0, k, a;
    int tmp[4096];
    a = i;
    while (sc->parent[a] >= 0 && n < 4096) {
        a = sc->parent[a];
        tmp[n++] = a;
    }
    for (k = 0; k < n; k++) out[k] = tmp[n - 1 - k];
    return n;
}

static void worldRest(Bk *b, const Scene *sc, int i, Aff *w) {
    int anc[4097], n = ancestors(sc, i, anc), k;
    affIdentity(w);
    anc[n++] = i;
    for (k = 0; k < n; k++) {
        Aff l;
        localMatrix(&l, &sc->rest[anc[k]]);
        compose(w, w, &l);
    }
    (void)b;
}

static Scene *makeScene(Bk *b, Gltf *g) {
    Scene *sc = (Scene *)aalloc(b, sizeof *sc);
    const ExtJson *js = g->js, *scenes = jget(js, "scenes"), *roots = NULL;
    int i, k, nn = g->nodes != NULL ? g->nodes->n : 0;
    IVec stack = {0, 0, 0};
    sc->nn = nn;
    sc->parent = (int *)aalloc(b, (size_t)(nn + 1) * sizeof(int));
    sc->reach = (char *)aalloc(b, (size_t)nn + 1);
    sc->animated = (char *)aalloc(b, (size_t)nn + 1);
    sc->rest = (Rest *)aalloc(b, (size_t)(nn + 1) * sizeof(Rest));
    for (i = 0; i < nn; i++) sc->parent[i] = -1;
    for (i = 0; i < nn; i++) {
        const ExtJson *ch = jget(&g->nodes->v[i], "children");
        if (ch == NULL) continue;
        if (!isArr(ch)) fail(b, "glTF: node %d has bad children", i);
        for (k = 0; k < ch->n; k++) {
            int c = idxOf(b, &ch->v[k], "a child");
            if (c >= nn) fail(b, "glTF: node %d has a child that does not exist", i);
            sc->parent[c] = i;
        }
    }
    if (scenes != NULL && isArr(scenes) && scenes->n > 0) {
        int si = jint(b, js, "scene", 0);
        roots = jget(elem(b, scenes, si, "scene"), "nodes");
    }
    if (roots != NULL && isArr(roots)) {
        for (k = 0; k < roots->n; k++) ipush(b, &stack, idxOf(b, &roots->v[k], "a scene node"));
    } else {
        for (i = 0; i < nn; i++) if (sc->parent[i] < 0) ipush(b, &stack, i);
    }
    while (stack.n > 0) {
        int n = stack.p[--stack.n];
        const ExtJson *ch;
        if (n >= nn) fail(b, "glTF: the scene names a node that does not exist");
        if (sc->reach[n]) continue;
        sc->reach[n] = 1;
        ch = jget(&g->nodes->v[n], "children");
        if (ch != NULL) for (k = 0; k < ch->n; k++) ipush(b, &stack, (int)ch->v[k].num);
    }
    for (i = 0; i < nn; i++) restTrs(b, g, i, &sc->rest[i]);
    if (g->anims != NULL) {
        for (i = 0; i < g->anims->n; i++) {
            const ExtJson *chs = jget(&g->anims->v[i], "channels");
            if (chs == NULL) continue;
            for (k = 0; k < chs->n; k++) {
                const ExtJson *tg = jget(&chs->v[k], "target"), *path = jget(tg, "path"), *node = jget(tg, "node");
                if (isStr(path) && node != NULL && (!strcmp(path->str, "translation") || !strcmp(path->str, "rotation") || !strcmp(path->str, "scale"))) {
                    int nd = idxOf(b, node, "an animation target");
                    if (nd >= nn) fail(b, "glTF: an animation targets a node that does not exist");
                    sc->animated[nd] = 1;
                }
            }
        }
    }
    return sc;
}

/* ---------------------------------------------------------------- primitives */

typedef struct Infl {
    int n;
    int j[8];
    double w[8];
} Infl;

typedef struct Prim {
    int node, skin, nv, ntri, mat;
    double *pos, *nrm, *uv;
    Infl *infl;
    int *tri;
} Prim;

static void readPrimitives(Bk *b, Gltf *g, const Scene *sc, Vec *out) {
    int ni, pi;
    for (ni = 0; ni < sc->nn; ni++) {
        const ExtJson *n, *mesh, *prims;
        const ExtJson *skinJ;
        if (!sc->reach[ni]) continue;
        n = &g->nodes->v[ni];
        if (jget(n, "mesh") == NULL) continue;
        mesh = elem(b, g->meshes, jint(b, n, "mesh", -1), "mesh");
        prims = jget(mesh, "primitives");
        if (!isArr(prims)) fail(b, "glTF: a mesh has no primitives");
        for (pi = 0; pi < prims->n; pi++) {
            const ExtJson *p = &prims->v[pi], *at = jget(p, "attributes");
            Prim *pr;
            int nc, cnt, k, vi;
            double *tmp;
            if (jint(b, p, "mode", 4) != 4) fail(b, "mesh primitive %d: only triangle lists (mode 4) are supported", pi);
            if (!isObj(at) || jget(at, "POSITION") == NULL) fail(b, "glTF: a primitive has no POSITION");
            pr = (Prim *)vpush(b, out, sizeof *pr);
            pr->node = ni;
            pr->skin = -1;
            pr->mat = jget(p, "material") != NULL ? jint(b, p, "material", -1) : -1;
            pr->pos = accessor(b, g, idxOf(b, jget(at, "POSITION"), "POSITION"), &pr->nv, &nc);
            if (nc != 3) fail(b, "glTF: POSITION is not a VEC3");
            if (jget(at, "NORMAL") != NULL) {
                pr->nrm = accessor(b, g, idxOf(b, jget(at, "NORMAL"), "NORMAL"), &cnt, &nc);
                if (nc != 3 || cnt != pr->nv) fail(b, "glTF: NORMAL does not match POSITION");
            }
            if (jget(at, "TEXCOORD_0") != NULL) {
                pr->uv = accessor(b, g, idxOf(b, jget(at, "TEXCOORD_0"), "TEXCOORD_0"), &cnt, &nc);
                if (nc != 2 || cnt != pr->nv) fail(b, "glTF: TEXCOORD_0 does not match POSITION");
            }
            if (jget(at, "COLOR_0") != NULL) note(b, 1, "vertex colours (COLOR_0) are ignored: the game lights textures only");
            if (pr->uv == NULL) note(b, 1, "a primitive has no TEXCOORD_0: it gets one texel of its material colour");
            if (jget(p, "indices") != NULL) {
                tmp = accessor(b, g, idxOf(b, jget(p, "indices"), "indices"), &cnt, &nc);
                if (nc != 1) fail(b, "glTF: indices are not SCALAR");
            } else {
                cnt = pr->nv;
                tmp = NULL;
            }
            if (cnt % 3) fail(b, "a primitive has %d indices (not a multiple of 3)", cnt);
            pr->ntri = cnt / 3;
            pr->tri = (int *)aalloc(b, (size_t)cnt * sizeof(int) + 4);
            for (k = 0; k < cnt; k++) {
                int v = tmp != NULL ? (int)tmp[k] : k;
                if (v < 0 || v >= pr->nv) fail(b, "glTF: an index is outside the vertices");
                pr->tri[k] = v;
            }
            skinJ = jget(n, "skin");
            if (skinJ != NULL && jget(at, "JOINTS_0") != NULL) {
                pr->skin = idxOf(b, skinJ, "skin");
                pr->infl = (Infl *)aalloc(b, (size_t)(pr->nv + 1) * sizeof(Infl));
                for (k = 0; k < 2; k++) {
                    char jn[16], wn[16];
                    int jc, wc, jcn, wcn;
                    double *js_, *ws_;
                    snprintf(jn, sizeof jn, "JOINTS_%d", k);
                    snprintf(wn, sizeof wn, "WEIGHTS_%d", k);
                    if (jget(at, jn) == NULL) break;
                    if (jget(at, wn) == NULL) fail(b, "glTF: %s without %s", jn, wn);
                    js_ = accessor(b, g, idxOf(b, jget(at, jn), jn), &jcn, &jc);
                    ws_ = accessor(b, g, idxOf(b, jget(at, wn), wn), &wcn, &wc);
                    if (jcn != pr->nv || wcn != pr->nv || jc != 4 || wc != 4) fail(b, "glTF: %s / %s do not match POSITION", jn, wn);
                    for (vi = 0; vi < pr->nv; vi++) {
                        int c;
                        for (c = 0; c < 4; c++) {
                            double w = ws_[vi * 4 + c];
                            if (w > 0) {
                                Infl *f = &pr->infl[vi];
                                f->j[f->n] = (int)js_[vi * 4 + c];
                                f->w[f->n++] = w;
                            }
                        }
                    }
                }
            }
        }
    }
    if (out->n == 0) fail(b, "the glTF scene has no triangle meshes");
}

/* ---------------------------------------------------------------- the build: skeleton and polygons */

typedef struct Corner {
    double pos[3], nrm[3], uv[2];
} Corner;

typedef struct MatKey {
    int img;
    double fac[4];
    int alphaMode; /* 0 OPAQUE, 1 MASK, 2 BLEND */
    double cutoff;
} MatKey;

typedef struct Poly {
    int bone, mat;
    Corner c[3];
    int pos[3][3], nrm[3][3];
    double px[3][2];
    int uv[3][2];
    int isl, clutX, clutY;
} Poly;

typedef struct Bone {
    int node, parent;
    IVec polys, children, kids; /* kids: the kept children */
    int top, keep;
    double k;
    M3 foldR;
    double foldT[3], foldS;
} Bone;

typedef struct Skin {
    int nj;
    int *joints;
    double *ibm; /* nj * 16 */
} Skin;

typedef struct Build {
    Bk *b;
    Gltf *g;
    const ExtJson *meta;
    Scene *sc;
    Skin *skins;
    int nskins;
    char *inc;
    Vec bones;  /* Bone */
    int *nodeBone;
    Vec polys;  /* Poly */
    Vec mats;   /* MatKey */
} Build;

static Bone *bone(Build *bd, int i) { return &((Bone *)bd->bones.p)[i]; }
static Poly *poly(Build *bd, int i) { return &((Poly *)bd->polys.p)[i]; }

static const double *ibmFor(const Skin *s, int node) {
    int k;
    for (k = s->nj - 1; k >= 0; k--) if (s->joints[k] == node) return s->ibm + (size_t)k * 16;
    return NULL;
}

static void toLocal(double *o, const double *ibm, const double *v) {
    if (ibm == NULL) {
        memcpy(o, v, 3 * sizeof(double));
        return;
    }
    {
        double r[3];
        r[0] = ((ibm[0] * v[0] + ibm[4] * v[1]) + ibm[8] * v[2]) + ibm[12];
        r[1] = ((ibm[1] * v[0] + ibm[5] * v[1]) + ibm[9] * v[2]) + ibm[13];
        r[2] = ((ibm[2] * v[0] + ibm[6] * v[1]) + ibm[10] * v[2]) + ibm[14];
        memcpy(o, r, sizeof r);
    }
}

static void toLocalN(double *o, const double *ibm, const double *n) {
    M3 m, inv, mi;
    int i, j;
    if (ibm == NULL) {
        memcpy(o, n, 3 * sizeof(double));
        return;
    }
    m[0][0] = ibm[0]; m[0][1] = ibm[4]; m[0][2] = ibm[8];
    m[1][0] = ibm[1]; m[1][1] = ibm[5]; m[1][2] = ibm[9];
    m[2][0] = ibm[2]; m[2][1] = ibm[6]; m[2][2] = ibm[10];
    if (!inv3(inv, m)) {
        memcpy(o, n, 3 * sizeof(double));
        return;
    }
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) mi[i][j] = inv[j][i];
    matvec3(o, mi, n);
}

/* the included nodes in preorder below n whose nearest included ancestor is n */
static void nestedChildren(Bk *b, const Scene *sc, Gltf *g, const char *inc, int n, IVec *out) {
    /* stack = children; pop(0); a non-included child puts its own children in front */
    IVec st = {0, 0, 0};
    const ExtJson *ch = jget(&g->nodes->v[n], "children");
    int k, head = 0;
    if (ch != NULL) for (k = 0; k < ch->n; k++) ipush(b, &st, (int)ch->v[k].num);
    while (head < st.n) {
        int c = st.p[head++];
        if (inc[c]) {
            ipush(b, out, c);
        } else {
            const ExtJson *cc = jget(&g->nodes->v[c], "children");
            if (cc != NULL && cc->n > 0) {
                /* stack = children(c) + stack */
                IVec ns = {0, 0, 0};
                for (k = 0; k < cc->n; k++) ipush(b, &ns, (int)cc->v[k].num);
                for (k = head; k < st.n; k++) ipush(b, &ns, st.p[k]);
                st = ns;
                head = 0;
            }
        }
    }
    (void)sc;
}

static void addBone(Build *bd, int n, int par) {
    Bone *bn;
    IVec nest = {0, 0, 0};
    int idx, k;
    if (bd->bones.n > 5000) fail(bd->b, "more than 5000 bones: the skeleton is too big");
    bn = (Bone *)vpush(bd->b, &bd->bones, sizeof *bn);
    bn->node = n;
    bn->parent = par;
    bn->k = 1.0;
    idx = bd->bones.n - 1;
    bd->nodeBone[n] = idx;
    ipush(bd->b, &bone(bd, par)->children, idx);
    nestedChildren(bd->b, bd->sc, bd->g, bd->inc, n, &nest);
    for (k = 0; k < nest.n; k++) addBone(bd, nest.p[k], idx);
}

static void skeleton(Build *bd, const Prim *prims, int np) {
    Bk *b = bd->b;
    Scene *sc = bd->sc;
    Gltf *g = bd->g;
    char *J = (char *)aalloc(b, (size_t)sc->nn + 1), *desc = (char *)aalloc(b, (size_t)sc->nn + 1);
    char *skinSeen = (char *)aalloc(b, (size_t)(g->skins != NULL ? g->skins->n : 0) + 1);
    int i, k, any = 0, anc[4097], na, x;
    Bone *root;

    bd->nskins = g->skins != NULL ? g->skins->n : 0;
    bd->skins = (Skin *)aalloc(b, (size_t)(bd->nskins + 1) * sizeof(Skin));
    for (i = 0; i < np; i++) if (prims[i].skin < 0) J[prims[i].node] = 1;
    for (i = 0; i < np; i++) {
        int si = prims[i].skin;
        const ExtJson *sk, *jl;
        Skin *s;
        if (si < 0 || skinSeen[si]) continue;
        skinSeen[si] = 1;
        sk = elem(b, g->skins, si, "skin");
        jl = jget(sk, "joints");
        if (!isArr(jl) || jl->n == 0) fail(b, "glTF: skin %d has no joints", si);
        s = &bd->skins[si];
        s->nj = jl->n;
        s->joints = (int *)aalloc(b, (size_t)jl->n * sizeof(int));
        s->ibm = (double *)aalloc(b, (size_t)jl->n * 16 * sizeof(double));
        for (k = 0; k < jl->n; k++) {
            s->joints[k] = idxOf(b, &jl->v[k], "a joint");
            if (s->joints[k] >= sc->nn) fail(b, "glTF: skin %d names a joint that does not exist", si);
            J[s->joints[k]] = 1;
        }
        if (jget(sk, "inverseBindMatrices") != NULL) {
            int cn, cc;
            double *a = accessor(b, g, idxOf(b, jget(sk, "inverseBindMatrices"), "inverseBindMatrices"), &cn, &cc);
            if (cc != 16 || cn < jl->n) fail(b, "glTF: skin %d has too few inverse bind matrices", si);
            memcpy(s->ibm, a, (size_t)jl->n * 16 * sizeof(double));
        } else {
            for (k = 0; k < jl->n; k++) {
                static const double id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                memcpy(s->ibm + (size_t)k * 16, id, sizeof id);
            }
        }
    }
    for (i = 0; i < sc->nn; i++) any |= J[i];
    if (!any) fail(b, "no joints and no mesh nodes");
    for (i = 0; i < sc->nn; i++) {
        if (!J[i]) continue;
        desc[i] = 1;
        na = ancestors(sc, i, anc);
        for (k = 0; k < na; k++) desc[anc[k]] = 1;
    }
    bd->inc = (char *)aalloc(b, (size_t)sc->nn + 1);
    for (x = 0; x < sc->nn; x++) {
        int ok;
        if (!desc[x]) continue;
        ok = J[x] || sc->animated[x];
        if (!ok) {
            na = ancestors(sc, x, anc);
            for (k = 0; k < na; k++) if (J[anc[k]]) ok = 1;
        }
        bd->inc[x] = (char)ok;
    }
    bd->nodeBone = (int *)aalloc(b, (size_t)(sc->nn + 1) * sizeof(int));
    root = (Bone *)vpush(b, &bd->bones, sizeof *root);
    root->node = -1;
    root->k = 1.0;
    for (x = 0; x < sc->nn; x++) {
        int hasInc = 0;
        if (!bd->inc[x]) continue;
        na = ancestors(sc, x, anc);
        for (k = 0; k < na; k++) if (bd->inc[anc[k]]) hasInc = 1;
        if (!hasInc) addBone(bd, x, 0);
    }
}

static double round3(double x) { return (double)llround(x * 1000.0) / 1000.0; }

static int matKey(Build *bd, const ExtJson *mat) {
    const ExtJson *pbr = jget(mat, "pbrMetallicRoughness"), *tex = jget(pbr, "baseColorTexture"), *fac = jget(pbr, "baseColorFactor"), *am;
    MatKey mk, *m;
    int i, k;
    memset(&mk, 0, sizeof mk);
    mk.img = -1;
    for (k = 0; k < 4; k++) mk.fac[k] = 1.0;
    if (fac != NULL) {
        if (!isArr(fac) || fac->n != 4) fail(bd->b, "glTF: a baseColorFactor is not 4 numbers");
        for (k = 0; k < 4; k++) mk.fac[k] = round3(fac->v[k].num);
    } else {
        for (k = 0; k < 4; k++) mk.fac[k] = round3(1.0);
    }
    if (tex != NULL) {
        const ExtJson *t = elem(bd->b, bd->g->textures, jint(bd->b, tex, "index", -1), "texture");
        const ExtJson *ex = jget(tex, "extensions");
        if (jget(t, "source") == NULL) fail(bd->b, "glTF: a texture has no source image");
        mk.img = jint(bd->b, t, "source", -1);
        if (ex != NULL && jget(ex, "KHR_texture_transform") != NULL) note(bd->b, 1, "KHR_texture_transform is ignored (bake the UV offset/scale in Blender)");
    } else {
        const ExtJson *nm = jget(mat, "name");
        note(bd->b, 1, "material %s has no texture: it becomes a flat colour patch", isStr(nm) ? nm->str : "None");
    }
    am = jget(mat, "alphaMode");
    mk.alphaMode = isStr(am) ? (!strcmp(am->str, "MASK") ? 1 : !strcmp(am->str, "BLEND") ? 2 : 0) : 0;
    mk.cutoff = jnum(mat, "alphaCutoff", 0.5);
    for (i = 0; i < bd->mats.n; i++) {
        m = &((MatKey *)bd->mats.p)[i];
        if (m->img == mk.img && m->alphaMode == mk.alphaMode && m->cutoff == mk.cutoff && m->fac[0] == mk.fac[0] && m->fac[1] == mk.fac[1] &&
            m->fac[2] == mk.fac[2] && m->fac[3] == mk.fac[3])
            return i;
    }
    m = (MatKey *)vpush(bd->b, &bd->mats, sizeof *m);
    *m = mk;
    return bd->mats.n - 1;
}

typedef struct KeyIdx {
    long k[3];
    int i;
} KeyIdx;

static int cmpKeyIdx(const void *a, const void *b, void *ctx) {
    const KeyIdx *x = (const KeyIdx *)a, *y = (const KeyIdx *)b;
    int k;
    (void)ctx;
    for (k = 0; k < 3; k++) {
        if (x->k[k] != y->k[k]) return x->k[k] < y->k[k] ? -1 : 1;
    }
    return 0;
}

/* smooth normals (the sum of the face normals around each welded position) or flat ones; round(x, 5) keys */
static double *makeNormals(Build *bd, const Prim *p, int flat) {
    Bk *b = bd->b;
    int nv = p->nv, t, k, vi, g_ = 0;
    KeyIdx *ki = (KeyIdx *)aalloc(b, (size_t)(nv + 1) * sizeof *ki);
    int *grp = (int *)aalloc(b, (size_t)(nv + 1) * sizeof(int));
    double *acc, *faces, *out = (double *)aalloc(b, (size_t)(nv + 1) * 3 * sizeof(double));
    for (vi = 0; vi < nv; vi++) {
        for (k = 0; k < 3; k++) ki[vi].k[k] = llround(p->pos[vi * 3 + k] * 1e5);
        ki[vi].i = vi;
    }
    msortStable(b, ki, (size_t)nv, sizeof *ki, cmpKeyIdx, NULL);
    for (vi = 0; vi < nv; vi++) {
        if (vi > 0 && cmpKeyIdx(&ki[vi - 1], &ki[vi], NULL) != 0) g_++;
        grp[ki[vi].i] = g_;
    }
    acc = (double *)aalloc(b, (size_t)(g_ + 1) * 3 * sizeof(double));
    faces = (double *)aalloc(b, (size_t)(p->ntri + 1) * 3 * sizeof(double));
    for (t = 0; t < p->ntri; t++) {
        int a = p->tri[3 * t], bb = p->tri[3 * t + 1], c = p->tri[3 * t + 2], v;
        double u[3], w[3], n[3];
        for (k = 0; k < 3; k++) {
            u[k] = p->pos[bb * 3 + k] - p->pos[a * 3 + k];
            w[k] = p->pos[c * 3 + k] - p->pos[a * 3 + k];
        }
        n[0] = u[1] * w[2] - u[2] * w[1];
        n[1] = u[2] * w[0] - u[0] * w[2];
        n[2] = u[0] * w[1] - u[1] * w[0];
        memcpy(faces + 3 * t, n, sizeof n);
        for (v = 0; v < 3; v++) {
            int id = p->tri[3 * t + v];
            for (k = 0; k < 3; k++) acc[grp[id] * 3 + k] += n[k];
        }
    }
    for (vi = 0; vi < nv; vi++) {
        out[vi * 3] = 0.0; out[vi * 3 + 1] = 1.0; out[vi * 3 + 2] = 0.0;
    }
    if (flat) {
        for (t = 0; t < p->ntri; t++) {
            int v;
            for (v = 0; v < 3; v++) memcpy(out + 3 * p->tri[3 * t + v], faces + 3 * t, 3 * sizeof(double));
        }
    } else {
        for (vi = 0; vi < nv; vi++) memcpy(out + 3 * vi, acc + 3 * grp[vi], 3 * sizeof(double));
    }
    return out;
}

static const char *metaStr(const ExtJson *meta, const char *k) {
    const ExtJson *v = jget(meta, k);
    return isStr(v) ? v->str : NULL;
}

static void polygons(Build *bd, const Prim *prims, int np) {
    Bk *b = bd->b;
    int pi, t, v, k, doubleAny = 0;
    long splitV = 0, totalV = 0, splitT = 0;
    const char *normals = metaStr(bd->meta, "normals"), *dsMode = metaStr(bd->meta, "doubleSided");
    for (pi = 0; pi < np; pi++) {
        const Prim *p = &prims[pi];
        const double *nrm = p->nrm;
        const ExtJson *mat = NULL;
        int mk, doubleSided = 0, skin = p->skin;
        if (nrm == NULL || (normals != NULL && (!strcmp(normals, "smooth") || !strcmp(normals, "flat"))))
            nrm = makeNormals(bd, p, normals != NULL && !strcmp(normals, "flat"));
        if (p->mat >= 0) mat = elem(b, bd->g->materials, p->mat, "material");
        if (mat != NULL && jget(mat, "doubleSided") != NULL && jget(mat, "doubleSided")->num != 0) {
            doubleSided = 1;
            doubleAny = 1;
        }
        mk = matKey(bd, mat);
        if (p->infl != NULL) {
            for (v = 0; v < p->nv; v++) {
                const Infl *f = &p->infl[v];
                totalV++;
                if (f->n > 1) {
                    double mx = f->w[0];
                    for (k = 1; k < f->n; k++) if (f->w[k] > mx) mx = f->w[k];
                    if (mx < 0.999) splitV++;
                }
            }
        }
        for (t = 0; t < p->ntri; t++) {
            int bi, node;
            const double *ibm = NULL;
            Poly *poly_;
            Corner cs[3], r[3];
            if (skin < 0) {
                node = p->node;
                bi = bd->nodeBone[node];
            } else {
                int nodes[16], nn = 0, c, owners[3], no = 0;
                double w[16];
                const Skin *sk = &bd->skins[skin];
                for (v = 0; v < 3; v++) {
                    const Infl *f = &p->infl[p->tri[3 * t + v]];
                    for (k = 0; k < f->n; k++) {
                        int nd, e;
                        if (f->j[k] < 0 || f->j[k] >= sk->nj) fail(b, "glTF: a vertex names a joint that the skin does not have");
                        nd = sk->joints[f->j[k]];
                        for (e = 0; e < nn && nodes[e] != nd; e++) {}
                        if (e == nn) {
                            if (nn >= 16) fail(b, "glTF: a triangle is influenced by too many joints");
                            nodes[nn] = nd;
                            w[nn++] = 0.0;
                        }
                        w[e] = w[e] + f->w[k];
                    }
                }
                if (nn == 0) {
                    note(b, 1, "a vertex has no joint weights: it follows the first joint");
                    node = sk->joints[0];
                } else {
                    int best = 0;
                    for (c = 1; c < nn; c++) {
                        if (w[c] > w[best] || (w[c] == w[best] && nodes[c] < nodes[best])) best = c;
                    }
                    node = nodes[best];
                }
                bi = bd->nodeBone[node];
                for (v = 0; v < 3; v++) {
                    const Infl *f = &p->infl[p->tri[3 * t + v]];
                    int bestk = 0, e, dup = 0;
                    if (f->n == 0) continue;
                    for (k = 1; k < f->n; k++) if (f->w[k] > f->w[bestk]) bestk = k;
                    for (e = 0; e < no; e++) if (owners[e] == f->j[bestk]) dup = 1;
                    if (!dup) owners[no++] = f->j[bestk];
                }
                if (no > 1) splitT++;
                ibm = ibmFor(sk, node);
            }
            for (v = 0; v < 3; v++) {
                int vi = p->tri[3 * t + v];
                toLocal(cs[v].pos, ibm, p->pos + 3 * vi);
                toLocalN(cs[v].nrm, ibm, nrm + 3 * vi);
                if (p->uv != NULL) {
                    cs[v].uv[0] = p->uv[2 * vi];
                    cs[v].uv[1] = p->uv[2 * vi + 1];
                }
            }
            /* glTF is counter-clockwise, the game's order is (0, 2, 1) of it */
            r[0] = cs[0]; r[1] = cs[2]; r[2] = cs[1];
            poly_ = (Poly *)vpush(b, &bd->polys, sizeof *poly_);
            poly_->bone = bi;
            poly_->mat = mk;
            memcpy(poly_->c, r, sizeof r);
            if (doubleSided && dsMode != NULL && !strcmp(dsMode, "duplicate")) {
                Corner back[3];
                back[0] = r[0]; back[1] = r[2]; back[2] = r[1];
                for (v = 0; v < 3; v++) for (k = 0; k < 3; k++) back[v].nrm[k] = -back[v].nrm[k];
                poly_ = (Poly *)vpush(b, &bd->polys, sizeof *poly_);
                poly_->bone = bi;
                poly_->mat = mk;
                memcpy(poly_->c, back, sizeof back);
            }
        }
    }
    if (doubleAny && !(dsMode != NULL && !strcmp(dsMode, "duplicate")))
        note(b, 1, "a material is double sided: the game draws one side only (set \"doubleSided\": \"duplicate\" to add the back faces)");
    if (splitV) note(b, 1, "%ld of %ld skinned vertices have split weights: each triangle follows its dominant bone (rigid skin)", splitV, totalV);
    if (splitT) note(b, 1, "%ld triangles span bones: they follow the bone that weighs most (rigid skin)", splitT);
}

/* ---------------------------------------------------------------- texture: islands, packing, palettes */

typedef struct Size {
    int w, h;
} Size;

typedef struct Pos {
    int x, y;
} Pos;

typedef struct ShelfItem {
    int i, w, h;
} ShelfItem;

static int cmpShelf(const void *a, const void *b, void *ctx) {
    const ShelfItem *x = (const ShelfItem *)a, *y = (const ShelfItem *)b;
    (void)ctx;
    if (x->h != y->h) return x->h > y->h ? -1 : 1; /* -h ascending */
    if (x->w != y->w) return x->w > y->w ? -1 : 1;
    return x->i < y->i ? -1 : x->i > y->i ? 1 : 0;
}

static int shelfPack(Bk *b, const Size *sizes, int n, int W, int H, Pos *pos) {
    ShelfItem *order = (ShelfItem *)aalloc(b, (size_t)(n + 1) * sizeof *order);
    int k, x = 0, y = 0, sh = 0;
    for (k = 0; k < n; k++) {
        order[k].i = k;
        order[k].w = sizes[k].w;
        order[k].h = sizes[k].h;
    }
    msortStable(b, order, (size_t)n, sizeof *order, cmpShelf, NULL);
    for (k = 0; k < n; k++) {
        int w = order[k].w, h = order[k].h, i = order[k].i;
        if (w > W || h > H) return 0;
        if (x + w > W) {
            y += sh;
            x = sh = 0;
        }
        if (y + h > H) return 0;
        pos[i].x = x;
        pos[i].y = y;
        x += w;
        if (h > sh) sh = h;
    }
    return 1;
}

static int rndi(double x) { return (int)rint(x); }

/* the largest scale f <= 1 at which the rects pack; fills sizes and pos, returns f */
static double packScaled(Bk *b, const Size *rects, int n, int W, int H, Size *sizes, Pos *pos) {
    int k, it, have = 0;
    double lo = 0.0, hi = 1.0, bestF = 1.0;
    Size *tsz = (Size *)aalloc(b, (size_t)(n + 1) * sizeof *tsz);
    Pos *tpos = (Pos *)aalloc(b, (size_t)(n + 1) * sizeof *tpos);
    for (k = 0; k < n; k++) {
        int w = rndi(rects[k].w * 1.0), h = rndi(rects[k].h * 1.0);
        sizes[k].w = w < 1 ? 1 : w;
        sizes[k].h = h < 1 ? 1 : h;
    }
    if (shelfPack(b, sizes, n, W, H, pos)) return 1.0;
    for (it = 0; it < 18; it++) {
        double mid = (lo + hi) / 2;
        for (k = 0; k < n; k++) {
            int w = rndi(rects[k].w * mid), h = rndi(rects[k].h * mid);
            tsz[k].w = w < 1 ? 1 : w;
            tsz[k].h = h < 1 ? 1 : h;
        }
        if (!shelfPack(b, tsz, n, W, H, tpos)) {
            hi = mid;
        } else {
            lo = mid;
            bestF = mid;
            have = 1;
            memcpy(sizes, tsz, (size_t)n * sizeof *tsz);
            memcpy(pos, tpos, (size_t)n * sizeof *tpos);
        }
    }
    if (!have) fail(b, "the texture islands do not fit in %dx%d even at 1/50 scale: too many UV islands", W, H);
    return bestF;
}

static int key15(double r, double g, double bl) {
    int a = rndi(r * 31 / 255.0), c = rndi(g * 31 / 255.0), d = rndi(bl * 31 / 255.0);
    if (a > 31) a = 31;
    if (c > 31) c = 31;
    if (d > 31) d = 31;
    return a | (c << 5) | (d << 10);
}

static double dist15(int a, int b) {
    int ar = a & 31, ag = (a >> 5) & 31, ab = (a >> 10) & 31, br = b & 31, bg = (b >> 5) & 31, bb = (b >> 10) & 31;
    return (WEIGHTS[0] * (double)((ar - br) * (ar - br)) + WEIGHTS[1] * (double)((ag - bg) * (ag - bg))) + WEIGHTS[2] * (double)((ab - bb) * (ab - bb));
}

typedef struct Hist {
    int n;
    int *k, *c;
} Hist;

static long histTotal(const Hist *h) {
    long t = 0;
    int i;
    for (i = 0; i < h->n; i++) t += h->c[i];
    return t;
}

typedef struct KC {
    int k, c;
} KC;

typedef struct Box {
    int n;
    KC *it;
} Box;

static int mcChannel;

static int cmpKCch(const void *a, const void *b, void *ctx) {
    int ca = (((const KC *)a)->k >> (5 * mcChannel)) & 31, cb = (((const KC *)b)->k >> (5 * mcChannel)) & 31;
    (void)ctx;
    return ca < cb ? -1 : ca > cb ? 1 : 0;
}

static int cmpInt(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* up to n colour keys of the histogram, ascending; the palette of a UV island */
static int medianCut(Bk *b, const Hist *h, int n, int *pal) {
    Box boxes[16];
    int nb = 1, i, ch, np = 0;
    if (h->n <= n) {
        for (i = 0; i < h->n; i++) pal[i] = h->k[i];
        return h->n;
    }
    boxes[0].n = h->n;
    boxes[0].it = (KC *)aalloc(b, (size_t)h->n * sizeof(KC));
    for (i = 0; i < h->n; i++) {
        boxes[0].it[i].k = h->k[i];
        boxes[0].it[i].c = h->c[i];
    }
    while (nb < n) {
        int bi, bch = -1, bbi = -1, cut = 1, j;
        double bs = 0.0, half;
        long acc, tot;
        Box old, lo_, hi_;
        for (bi = 0; bi < nb; bi++) {
            if (boxes[bi].n < 2) continue;
            for (ch = 0; ch < 3; ch++) {
                int mn = 99, mx = -1;
                double sc;
                long t = 0;
                for (j = 0; j < boxes[bi].n; j++) {
                    int v = (boxes[bi].it[j].k >> (5 * ch)) & 31;
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                    t += boxes[bi].it[j].c;
                }
                sc = ((double)(mx - mn) * WEIGHTS[ch]) * sqrt((double)t);
                if (sc > bs) {
                    bs = sc;
                    bch = ch;
                    bbi = bi;
                }
            }
        }
        if (bbi < 0) break;
        old = boxes[bbi];
        for (i = bbi; i < nb - 1; i++) boxes[i] = boxes[i + 1];
        nb--;
        mcChannel = bch;
        msortStable(b, old.it, (size_t)old.n, sizeof(KC), cmpKCch, NULL);
        tot = 0;
        for (j = 0; j < old.n; j++) tot += old.it[j].c;
        half = (double)tot / 2.0;
        acc = 0;
        for (j = 0; j < old.n; j++) {
            acc += old.it[j].c;
            if ((double)acc >= half) {
                cut = j + 1;
                if (cut > old.n - 1) cut = old.n - 1;
                if (cut < 1) cut = 1;
                break;
            }
        }
        lo_.n = cut;
        lo_.it = old.it;
        hi_.n = old.n - cut;
        hi_.it = old.it + cut;
        boxes[nb++] = lo_;
        boxes[nb++] = hi_;
    }
    for (i = 0; i < nb; i++) {
        long tot = 0, sr = 0, sg = 0, sb = 0;
        int j;
        double r, g, bl;
        for (j = 0; j < boxes[i].n; j++) {
            long c = boxes[i].it[j].c;
            int k = boxes[i].it[j].k;
            tot += c;
            sr += (k & 31) * c;
            sg += ((k >> 5) & 31) * c;
            sb += ((k >> 10) & 31) * c;
        }
        r = (double)sr / (double)tot;
        g = (double)sg / (double)tot;
        bl = (double)sb / (double)tot;
        pal[np++] = rndi(r) | (rndi(g) << 5) | (rndi(bl) << 10);
    }
    qsort(pal, (size_t)np, sizeof(int), cmpInt);
    {
        int o = 0;
        for (i = 0; i < np; i++) if (i == 0 || pal[i] != pal[i - 1]) pal[o++] = pal[i];
        np = o;
    }
    return np;
}

/* the cells of a (gw x gh) grid that a triangle (pts in texels) covers, sampled at 5 points per cell */
static void triCells(Bk *b, IVec *out, double pts[3][2], int cell, int gw, int gh) {
    double xs[3] = {pts[0][0], pts[1][0], pts[2][0]}, ys[3] = {pts[0][1], pts[1][1], pts[2][1]};
    double minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0], den, ax = pts[0][0], ay = pts[0][1], bx = pts[1][0], by = pts[1][1], cx_ = pts[2][0], cy_ = pts[2][1];
    int cx0, cx1, cy0, cy1, k, cx, cy;
    static const double OFF[5][2] = {{0.5, 0.5}, {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}};
    for (k = 1; k < 3; k++) {
        if (xs[k] < minx) minx = xs[k];
        if (xs[k] > maxx) maxx = xs[k];
        if (ys[k] < miny) miny = ys[k];
        if (ys[k] > maxy) maxy = ys[k];
    }
    cx0 = (int)pyFloorDiv(minx, (double)cell);
    if (cx0 < 0) cx0 = 0;
    cx1 = (int)pyFloorDiv(maxx, (double)cell);
    if (cx1 > gw - 1) cx1 = gw - 1;
    cy0 = (int)pyFloorDiv(miny, (double)cell);
    if (cy0 < 0) cy0 = 0;
    cy1 = (int)pyFloorDiv(maxy, (double)cell);
    if (cy1 > gh - 1) cy1 = gh - 1;
    den = (by - cy_) * (ax - cx_) + (cx_ - bx) * (ay - cy_);
    for (k = 0; k < 3; k++) {
        int ix = (int)pyFloorDiv(pts[k][0], (double)cell), iy = (int)pyFloorDiv(pts[k][1], (double)cell);
        if (ix < 0) ix = 0;
        if (ix > gw - 1) ix = gw - 1;
        if (iy < 0) iy = 0;
        if (iy > gh - 1) iy = gh - 1;
        ipush(b, out, iy * gw + ix);
    }
    for (cy = cy0; cy <= cy1; cy++) {
        for (cx = cx0; cx <= cx1; cx++) {
            if (fabs(den) < 1e-12) continue;
            for (k = 0; k < 5; k++) {
                double px = (cx + OFF[k][0]) * cell, py = (cy + OFF[k][1]) * cell;
                double l1 = ((by - cy_) * (px - cx_) + (cx_ - bx) * (py - cy_)) / den;
                double l2 = ((cy_ - ay) * (px - cx_) + (ax - cx_) * (py - cy_)) / den;
                if (l1 >= -1e-9 && l2 >= -1e-9 && 1 - l1 - l2 >= -1e-9) {
                    ipush(b, out, cy * gw + cx);
                    break;
                }
            }
        }
    }
}

static int ufFind(int *p, int x) {
    while (p[x] != x) {
        p[x] = p[p[x]];
        x = p[x];
    }
    return x;
}

typedef struct MatImg {
    Img *im;
    Img one;
    double fac[4];
    int opaque, cell, gw, gh, base;
} MatImg;

/* Box-filter the source rect (texels) into rw x rh: colour keys (-1 = transparent) */
static void resample(Bk *b, const Img *img, long x0, long y0, long x1, long y1, int rw, int rh, const double *fac, int opaque, double cutoff, int *out) {
    int dx, dy, o = 0;
    for (dy = 0; dy < rh; dy++) {
        long sy0 = y0 + ifloorDiv((y1 - y0) * dy, rh), sy1 = y0 + iceilDiv((y1 - y0) * (dy + 1), rh);
        if (sy1 < sy0 + 1) sy1 = sy0 + 1;
        for (dx = 0; dx < rw; dx++) {
            long sx0 = x0 + ifloorDiv((x1 - x0) * dx, rw), sx1 = x0 + iceilDiv((x1 - x0) * (dx + 1), rw);
            long sr = 0, sg = 0, sb = 0, sa = 0, n = 0, sy, sx, ey, ex;
            double alpha;
            if (sx1 < sx0 + 1) sx1 = sx0 + 1;
            ey = sy1 < img->h ? sy1 : img->h;
            ex = sx1 < img->w ? sx1 : img->w;
            for (sy = sy0; sy < ey; sy++) {
                for (sx = sx0; sx < ex; sx++) {
                    const uint8_t *px = img->rgba + ((size_t)sy * (size_t)img->w + (size_t)sx) * 4;
                    long a = px[3];
                    sr += px[0] * a;
                    sg += px[1] * a;
                    sb += px[2] * a;
                    sa += a;
                    n++;
                }
            }
            if (n == 0) {
                out[o++] = -1;
                continue;
            }
            alpha = (double)sa / (double)n * fac[3];
            if (opaque) alpha = 255;
            if (alpha < 255 * cutoff) {
                out[o++] = -1;
                continue;
            }
            if (sa == 0) {
                out[o++] = -1;
                continue;
            }
            {
                double r = (double)sr / (double)sa * fac[0], g = (double)sg / (double)sa * fac[1], bl = (double)sb / (double)sa * fac[2];
                out[o++] = key15(r < 255 ? r : 255, g < 255 ? g : 255, bl < 255 ? bl : 255);
            }
        }
    }
    (void)b;
}

typedef struct Group {
    IVec isl;
    Hist hist;
    int t, npal, pal[16];
} Group;

static void histAdd(Bk *b, Hist *dst, const Hist *src) {
    int *k = (int *)aalloc(b, (size_t)(dst->n + src->n + 1) * sizeof(int)), *c = (int *)aalloc(b, (size_t)(dst->n + src->n + 1) * sizeof(int));
    int i = 0, j = 0, n = 0;
    while (i < dst->n || j < src->n) {
        if (j >= src->n || (i < dst->n && dst->k[i] < src->k[j])) {
            k[n] = dst->k[i];
            c[n++] = dst->c[i++];
        } else if (i >= dst->n || src->k[j] < dst->k[i]) {
            k[n] = src->k[j];
            c[n++] = src->c[j++];
        } else {
            k[n] = dst->k[i];
            c[n++] = dst->c[i++] + src->c[j++];
        }
    }
    dst->k = k;
    dst->c = c;
    dst->n = n;
}

static double groupCost(const Group *a, const Group *bg) {
    double tot = 0.0;
    int pass, i, q;
    for (pass = 0; pass < 2; pass++) {
        const Group *x = pass == 0 ? a : bg, *y = pass == 0 ? bg : a;
        for (i = 0; i < x->hist.n; i++) {
            double m = 0.0;
            for (q = 0; q < y->npal; q++) {
                double d = dist15(x->hist.k[i], y->pal[q]);
                if (q == 0 || d < m) m = d;
            }
            tot += x->hist.c[i] * m;
        }
    }
    return tot;
}

static int cmpGroupTotal(const void *a, const void *b, void *ctx) {
    long x = histTotal(&((const Group *)a)->hist), y = histTotal(&((const Group *)b)->hist);
    (void)ctx;
    return x < y ? -1 : x > y ? 1 : 0;
}

typedef struct TexStats {
    int islands, palettes, merged, maxColors;
    double scale, rms;
} TexStats;

/* Repack the UV islands of every polygon (the indices in `ids`, in bone order) into one 256x240 page of 4 bpp indices with CLUTs.
   Sets the px, uv, clut and isl of the polygons; rows: 256 x 64 halfwords. */
static void buildTexture(Build *bd, const int *ids, int np, uint16_t *rows, TexStats *st) {
    Bk *b = bd->b;
    Gltf *g = bd->g;
    int nm = bd->mats.n, m, pi, k, ii, clamped = 0, nodes = 0, nisl = 0;
    MatImg *mi = (MatImg *)aalloc(b, (size_t)(nm + 1) * sizeof *mi);
    int *uf, *islId, *islOfRoot;
    IVec cellsV = {0, 0, 0};
    int *isl_first;
    IVec *islPolys;
    Size *rectSz, *sizes;
    long (*rects)[4];
    Pos *place;
    int **texels;
    uint8_t **used;
    Hist *hists;
    char *hasT;
    Group *groups;
    int ng = 0, nmerged = 0, gi;
    uint8_t *atlas;
    double errSum = 0.0;
    long errN = 0;
    int maxDistinct = 0;
    int (*islClut)[2];

    for (m = 0; m < nm; m++) {
        MatKey *mk = &((MatKey *)bd->mats.p)[m];
        MatImg *x = &mi[m];
        if (mk->img < 0) {
            x->one.w = x->one.h = 1;
            x->one.rgba = (uint8_t *)aalloc(b, 4);
            for (k = 0; k < 4; k++) {
                int v = rndi(mk->fac[k] * 255);
                x->one.rgba[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            }
            x->im = &x->one;
            x->fac[0] = x->fac[1] = x->fac[2] = x->fac[3] = 1.0;
            x->opaque = 1;
        } else {
            x->im = gltfImage(b, g, mk->img);
            for (k = 0; k < 4; k++) x->fac[k] = mk->fac[k];
            x->opaque = mk->alphaMode == 0;
        }
        {
            int mxd = x->im->w > x->im->h ? x->im->w : x->im->h;
            x->cell = (mxd + 127) / 128;
            if (x->cell < 1) x->cell = 1;
        }
        x->gw = (x->im->w + x->cell - 1) / x->cell;
        x->gh = (x->im->h + x->cell - 1) / x->cell;
        x->base = nodes;
        nodes += x->gw * x->gh;
    }
    uf = (int *)aalloc(b, (size_t)(nodes + 1) * sizeof(int));
    for (k = 0; k < nodes; k++) uf[k] = k;
    isl_first = (int *)aalloc(b, (size_t)(np + 1) * sizeof(int));
    for (pi = 0; pi < np; pi++) {
        Poly *p = poly(bd, ids[pi]);
        MatImg *x = &mi[p->mat];
        double pts[3][2];
        int first = -1, c;
        for (k = 0; k < 3; k++) {
            double u = p->c[k].uv[0], v = p->c[k].uv[1];
            if (u < -1e-4 || u > 1 + 1e-4 || v < -1e-4 || v > 1 + 1e-4) clamped = 1;
            u = u < 0.0 ? 0.0 : u;
            u = u > 1.0 ? 1.0 : u;
            v = v < 0.0 ? 0.0 : v;
            v = v > 1.0 ? 1.0 : v;
            pts[k][0] = u * x->im->w;
            pts[k][1] = v * x->im->h;
            p->px[k][0] = pts[k][0];
            p->px[k][1] = pts[k][1];
        }
        cellsV.n = 0;
        triCells(b, &cellsV, pts, x->cell, x->gw, x->gh);
        for (c = 0; c < cellsV.n; c++) {
            int node = x->base + cellsV.p[c];
            if (first < 0) first = node;
            else {
                int ra = ufFind(uf, first), rb = ufFind(uf, node);
                if (ra != rb) uf[rb] = ra;
            }
        }
        isl_first[pi] = first;
    }
    if (clamped) note(b, 1, "some UVs are outside 0..1 (tiling is not supported): they were clamped to the border");
    islOfRoot = (int *)aalloc(b, (size_t)(nodes + 1) * sizeof(int));
    for (k = 0; k < nodes; k++) islOfRoot[k] = -1;
    islId = (int *)aalloc(b, (size_t)(np + 1) * sizeof(int));
    for (pi = 0; pi < np; pi++) {
        int r = ufFind(uf, isl_first[pi]); /* after every polygon has merged its cells: the component, as the tool computes it */
        if (islOfRoot[r] < 0) islOfRoot[r] = nisl++;
        islId[pi] = islOfRoot[r];
    }
    islPolys = (IVec *)aalloc(b, (size_t)(nisl + 1) * sizeof(IVec));
    for (pi = 0; pi < np; pi++) ipush(b, &islPolys[islId[pi]], pi);
    rects = (long (*)[4])aalloc(b, (size_t)(nisl + 1) * sizeof *rects);
    rectSz = (Size *)aalloc(b, (size_t)(nisl + 1) * sizeof *rectSz);
    for (ii = 0; ii < nisl; ii++) {
        double mnx = 0, mny = 0, mxx = 0, mxy = 0;
        int first = 1;
        long x0, y0, x1, y1;
        for (k = 0; k < islPolys[ii].n; k++) {
            Poly *p = poly(bd, ids[islPolys[ii].p[k]]);
            int c;
            for (c = 0; c < 3; c++) {
                double x = p->px[c][0], y = p->px[c][1];
                if (first || x < mnx) mnx = x;
                if (first || x > mxx) mxx = x;
                if (first || y < mny) mny = y;
                if (first || y > mxy) mxy = y;
                first = 0;
            }
        }
        x0 = (long)floor(mnx + 1e-6);
        y0 = (long)floor(mny + 1e-6);
        x1 = (long)ceil(mxx - 1e-6);
        y1 = (long)ceil(mxy - 1e-6);
        if (x1 < x0 + 1) x1 = x0 + 1;
        if (y1 < y0 + 1) y1 = y0 + 1;
        rects[ii][0] = x0; rects[ii][1] = y0; rects[ii][2] = x1; rects[ii][3] = y1;
        rectSz[ii].w = (int)(x1 - x0);
        rectSz[ii].h = (int)(y1 - y0);
    }
    sizes = (Size *)aalloc(b, (size_t)(nisl + 1) * sizeof *sizes);
    place = (Pos *)aalloc(b, (size_t)(nisl + 1) * sizeof *place);
    st->scale = packScaled(b, rectSz, nisl, TEX_W, TEX_H, sizes, place);
    texels = (int **)aalloc(b, (size_t)(nisl + 1) * sizeof(int *));
    used = (uint8_t **)aalloc(b, (size_t)(nisl + 1) * sizeof(uint8_t *));
    hists = (Hist *)aalloc(b, (size_t)(nisl + 1) * sizeof *hists);
    hasT = (char *)aalloc(b, (size_t)nisl + 1);
    for (ii = 0; ii < nisl; ii++) {
        long x0 = rects[ii][0], y0 = rects[ii][1], x1 = rects[ii][2], y1 = rects[ii][3];
        int rw = sizes[ii].w, rh = sizes[ii].h, rx = place[ii].x, ry = place[ii].y;
        int mat = poly(bd, ids[islPolys[ii].p[0]])->mat;
        MatImg *x = &mi[mat];
        double cutoff = ((MatKey *)bd->mats.p)[mat].cutoff;
        texels[ii] = (int *)aalloc(b, (size_t)rw * (size_t)rh * sizeof(int));
        used[ii] = (uint8_t *)aalloc(b, (size_t)rw * (size_t)rh);
        resample(b, x->im, x0, y0, x1, y1, rw, rh, x->fac, x->opaque, cutoff, texels[ii]);
        for (k = 0; k < islPolys[ii].n; k++) {
            Poly *p = poly(bd, ids[islPolys[ii].p[k]]);
            int c, mnu = 1 << 30, mxu = -1, mnv = 1 << 30, mxv = -1, tx, ty;
            for (c = 0; c < 3; c++) {
                double u = (rx + (p->px[c][0] - (double)x0) * rw / (double)(x1 - x0)) - 0.5;
                double v = (ry + (p->px[c][1] - (double)y0) * rh / (double)(y1 - y0)) - 0.5;
                int iu = rndi(u), iv = rndi(v);
                if (iu < rx) iu = rx;
                if (iu > rx + rw - 1) iu = rx + rw - 1;
                if (iv < ry) iv = ry;
                if (iv > ry + rh - 1) iv = ry + rh - 1;
                p->uv[c][0] = iu;
                p->uv[c][1] = iv;
                if (iu < mnu) mnu = iu;
                if (iu > mxu) mxu = iu;
                if (iv < mnv) mnv = iv;
                if (iv > mxv) mxv = iv;
            }
            for (ty = mnv; ty <= mxv; ty++) for (tx = mnu; tx <= mxu; tx++) used[ii][(ty - ry) * rw + (tx - rx)] = 1;
        }
    }
    for (ii = 0; ii < nisl; ii++) {
        int rw = sizes[ii].w, rh = sizes[ii].h, tx, ty, nk = 0, *ks;
        ks = (int *)aalloc(b, (size_t)rw * (size_t)rh * sizeof(int) + 4);
        for (ty = 0; ty < rh; ty++) {
            for (tx = 0; tx < rw; tx++) {
                int kk;
                if (!used[ii][ty * rw + tx]) continue;
                kk = texels[ii][ty * rw + tx];
                if (kk < 0) hasT[ii] = 1;
                else ks[nk++] = kk;
            }
        }
        qsort(ks, (size_t)nk, sizeof(int), cmpInt);
        hists[ii].k = (int *)aalloc(b, (size_t)nk * sizeof(int) + 4);
        hists[ii].c = (int *)aalloc(b, (size_t)nk * sizeof(int) + 4);
        for (k = 0; k < nk; k++) {
            if (hists[ii].n > 0 && hists[ii].k[hists[ii].n - 1] == ks[k]) hists[ii].c[hists[ii].n - 1]++;
            else {
                hists[ii].k[hists[ii].n] = ks[k];
                hists[ii].c[hists[ii].n++] = 1;
            }
        }
    }
    /* one palette per island, identical ones shared, then merged down to MAX_CLUTS */
    groups = (Group *)aalloc(b, (size_t)(nisl + 1) * sizeof *groups);
    for (ii = 0; ii < nisl; ii++) {
        int pal[16], npal = medianCut(b, &hists[ii], hasT[ii] ? 15 : 16, pal), q, found = -1;
        for (q = 0; q < ng && found < 0; q++) {
            if (groups[q].t == hasT[ii] && groups[q].npal == npal && memcmp(groups[q].pal, pal, (size_t)npal * sizeof(int)) == 0) found = q;
        }
        if (found >= 0) {
            ipush(b, &groups[found].isl, ii);
            histAdd(b, &groups[found].hist, &hists[ii]);
        } else {
            Group *gr = &groups[ng++];
            ipush(b, &gr->isl, ii);
            gr->hist = hists[ii];
            gr->t = hasT[ii];
            gr->npal = npal;
            memcpy(gr->pal, pal, (size_t)npal * sizeof(int));
        }
    }
    while (ng > MAX_CLUTS) {
        Group a, *bg;
        int bi = 0, q, i;
        double bc = 0.0;
        msortStable(b, groups, (size_t)ng, sizeof *groups, cmpGroupTotal, NULL);
        a = groups[0];
        for (i = 0; i < ng - 1; i++) groups[i] = groups[i + 1];
        ng--;
        for (q = 0; q < ng; q++) {
            double c = groupCost(&a, &groups[q]);
            if (q == 0 || c < bc) {
                bc = c;
                bi = q;
            }
        }
        bg = &groups[bi];
        for (i = 0; i < a.isl.n; i++) ipush(b, &bg->isl, a.isl.p[i]);
        histAdd(b, &bg->hist, &a.hist);
        bg->t = bg->t || a.t;
        bg->npal = medianCut(b, &bg->hist, bg->t ? 15 : 16, bg->pal);
        nmerged++;
    }
    /* indices, CLUTs, error */
    atlas = (uint8_t *)aalloc(b, 256 * 256);
    islClut = (int (*)[2])aalloc(b, (size_t)(nisl + 1) * sizeof *islClut);
    memset(rows, 0, 256 * 64 * sizeof(uint16_t));
    for (gi = 0; gi < ng; gi++) {
        Group *gr = &groups[gi];
        int pal[17], np_ = 0, i, cx = 16 * (gi % 4), cy = 240 + gi / 4, offset = gr->t ? 1 : 0;
        if (gr->t) pal[np_++] = 0;
        for (i = 0; i < gr->npal; i++) pal[np_++] = gr->pal[i];
        for (i = 0; i < np_; i++) {
            int v = pal[i];
            int cv = (gr->t && i == 0) ? 0 : (v ? v : 0x0421);
            rows[cy * 64 + gi % 4 * 16 + i] = (uint16_t)cv;
        }
        for (i = 0; i < gr->isl.n; i++) {
            int ii2 = gr->isl.p[i], rw = sizes[ii2].w, rh = sizes[ii2].h, rx = place[ii2].x, ry = place[ii2].y, tx, ty;
            islClut[ii2][0] = cx;
            islClut[ii2][1] = cy;
            if (hists[ii2].n > maxDistinct) maxDistinct = hists[ii2].n;
            for (ty = 0; ty < rh; ty++) {
                for (tx = 0; tx < rw; tx++) {
                    int kk = texels[ii2][ty * rw + tx], idx;
                    if (kk < 0 && gr->t) {
                        idx = 0;
                    } else if (gr->npal == 0) {
                        idx = 0;
                    } else {
                        int best = 0, q;
                        double bd2 = 0.0;
                        if (kk < 0) kk = gr->pal[0];
                        for (q = 0; q < gr->npal; q++) {
                            double d = dist15(kk, gr->pal[q]);
                            if (q == 0 || d < bd2) {
                                bd2 = d;
                                best = q;
                            }
                        }
                        idx = best + offset;
                        if (used[ii2][ty * rw + tx]) {
                            errSum += dist15(kk, gr->pal[idx - offset]);
                            errN++;
                        }
                    }
                    atlas[(ry + ty) * 256 + rx + tx] = (uint8_t)idx;
                }
            }
        }
    }
    for (pi = 0; pi < np; pi++) {
        Poly *p = poly(bd, ids[pi]);
        p->isl = islId[pi];
        p->clutX = islClut[islId[pi]][0];
        p->clutY = islClut[islId[pi]][1];
    }
    for (k = 0; k < 240; k++) {
        int c;
        for (c = 0; c < 64; c++) {
            size_t bb = (size_t)k * 256 + (size_t)c * 4;
            rows[k * 64 + c] = (uint16_t)(atlas[bb] | (atlas[bb + 1] << 4) | (atlas[bb + 2] << 8) | (atlas[bb + 3] << 12));
        }
    }
    st->islands = nisl;
    st->palettes = ng;
    st->merged = nmerged;
    st->maxColors = maxDistinct;
    st->rms = errN ? sqrt(errSum / (double)errN / ((WEIGHTS[0] + WEIGHTS[1]) + WEIGHTS[2])) * 255 / 31 : 0.0;
    if (st->scale < 0.999) note(b, 0, "texture: %d UV islands scaled to %.0f%% to fit %dx%d", nisl, st->scale * 100, TEX_W, TEX_H);
    if (nmerged) note(b, 1, "%d palettes were merged to stay within %d CLUTs", nmerged, MAX_CLUTS);
    if (st->rms > 14) note(b, 1, "texture quantization error is high (rms %.0f of 255): the game keeps 16 colours per UV island", st->rms);
    (void)cellsV;
}

/* ---------------------------------------------------------------- encoders (the game's file formats) */

static void putS16(Bk *b, Buf *x, long v) { b16(b, x, (unsigned)(v & 0xFFFF)); }

static long floorDiv2(long a) { return ifloorDiv(a, 2); }

static void encodeBounds(Bk *b, Buf *o, const int (*v)[3], int n) {
    int i, k;
    if (n == 0) {
        putS16(b, o, 9);
        for (i = 0; i < 29; i++) putS16(b, o, 0);
        return;
    }
    {
        int mn[3], mx[3], x, y, z;
        long pts[9][3];
        int np = 0;
        for (k = 0; k < 3; k++) mn[k] = mx[k] = v[0][k];
        for (i = 1; i < n; i++) {
            for (k = 0; k < 3; k++) {
                if (v[i][k] < mn[k]) mn[k] = v[i][k];
                if (v[i][k] > mx[k]) mx[k] = v[i][k];
            }
        }
        for (k = 0; k < 3; k++) pts[0][k] = floorDiv2((long)mn[k] + mx[k]);
        np = 1;
        for (z = 1; z >= 0; z--) {
            for (y = 1; y >= 0; y--) {
                for (x = 1; x >= 0; x--) {
                    pts[np][0] = x ? mx[0] : mn[0];
                    pts[np][1] = y ? mx[1] : mn[1];
                    pts[np][2] = z ? mx[2] : mn[2];
                    np++;
                }
            }
        }
        putS16(b, o, 9);
        putS16(b, o, 0);
        putS16(b, o, 0);
        for (i = 0; i < 9; i++) for (k = 0; k < 3; k++) putS16(b, o, pts[i][k]);
    }
}

typedef struct OutPoly {
    int idx[3], nrm[3], uv[3][2], clutX, clutY;
} OutPoly;

static void encodeCmds(Bk *b, Buf *o, const OutPoly *polys, int n) {
    static const uint8_t head[7] = {0xD1, 0x80, 0x91, 0xC1, 0xE0, 0xA0, 0xB0};
    int i, k, cx = -1, cy = -1;
    if (n == 0) {
        b8(b, o, 0xFF);
        return;
    }
    bput(b, o, head, 7);
    for (i = 0; i < n; i++) {
        const OutPoly *p = &polys[i];
        if (p->clutX != cx || p->clutY != cy) {
            b8(b, o, 1); b8(b, o, 0); b8(b, o, 0); b8(b, o, 0); b8(b, o, (unsigned)p->clutX); b8(b, o, (unsigned)p->clutY); b8(b, o, 0);
            cx = p->clutX;
            cy = p->clutY;
        }
        b8(b, o, 0);
        for (k = 0; k < 3; k++) b8(b, o, (unsigned)p->idx[k]);
        for (k = 0; k < 3; k++) b8(b, o, (unsigned)p->nrm[k]);
        for (k = 0; k < 3; k++) {
            b8(b, o, (unsigned)p->uv[k][0]);
            b8(b, o, (unsigned)p->uv[k][1]);
        }
    }
    b8(b, o, 0xFF);
}

static void encodeMesh(Bk *b, Buf *out, const int (*verts)[3], int nv, const int (*norms)[3], int nn, const OutPoly *polys, int np) {
    Buf vb = {0, 0, 0}, bounds = {0, 0, 0}, nb = {0, 0, 0}, cmds = {0, 0, 0};
    Buf *parts[4];
    size_t offs[4], o = 24;
    int i, k;
    putS16(b, &vb, nv); putS16(b, &vb, 0); putS16(b, &vb, 0);
    for (i = 0; i < nv; i++) for (k = 0; k < 3; k++) putS16(b, &vb, verts[i][k]);
    encodeBounds(b, &bounds, verts, nv);
    putS16(b, &nb, nn); putS16(b, &nb, 0); putS16(b, &nb, 0);
    for (i = 0; i < nn; i++) for (k = 0; k < 3; k++) putS16(b, &nb, norms[i][k]);
    encodeCmds(b, &cmds, polys, np);
    parts[0] = &vb; parts[1] = &bounds; parts[2] = &nb; parts[3] = &cmds;
    for (i = 0; i < 4; i++) {
        bpad(b, parts[i], 4);
        offs[i] = o;
        o += parts[i]->n;
    }
    b32(b, out, (unsigned long)offs[0]); b32(b, out, (unsigned long)offs[2]); b32(b, out, (unsigned long)offs[3]);
    b32(b, out, 0); b32(b, out, 0); b32(b, out, (unsigned long)offs[1]);
    for (i = 0; i < 4; i++) bput(b, out, parts[i]->p, parts[i]->n);
    bpad(b, out, 8);
}

typedef struct Key {
    int x, y, z, f;
} Key;

typedef struct Track {
    int n;
    Key *k;
} Track;

static void encodeKeys(Bk *b, Buf *out, const Track *t) {
    Buf lists[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    size_t o = 16, offs[3];
    int c, i;
    for (c = 0; c < 3; c++) {
        for (i = 0; i < t[c].n; i++) {
            putS16(b, &lists[c], t[c].k[i].x);
            putS16(b, &lists[c], t[c].k[i].y);
            putS16(b, &lists[c], t[c].k[i].z);
            putS16(b, &lists[c], t[c].k[i].f);
        }
        offs[c] = o;
        o += lists[c].n;
    }
    for (c = 0; c < 3; c++) b32(b, out, (unsigned long)offs[c]);
    b32(b, out, (unsigned long)o);
    for (c = 0; c < 3; c++) bput(b, out, lists[c].p, lists[c].n);
    bpad(b, out, 8);
}

static void rlenEncode(Bk *b, Buf *out, const uint8_t *raw, size_t n) {
    size_t i = 0;
    bput(b, out, "RLEN", 4);
    b32(b, out, (unsigned long)n);
    while (i < n) {
        size_t j = i, k;
        while (j < n && raw[j] == raw[i] && j - i < 127) j++;
        if (j - i >= 3) {
            b8(b, out, 0x80 | (unsigned)(j - i));
            b8(b, out, raw[i]);
            i = j;
            continue;
        }
        k = i;
        while (k < n && k - i < 127 && !(k + 2 < n && raw[k] == raw[k + 1] && raw[k + 1] == raw[k + 2])) k++;
        if (k == i) k = i + 1;
        b8(b, out, (unsigned)(k - i));
        bput(b, out, raw + i, k - i);
        i = k;
    }
    b8(b, out, 0);
}

static void encodeTextureArchive(Bk *b, Buf *out, const uint16_t *rows) {
    Buf tim = {0, 0, 0}, rl = {0, 0, 0};
    int i;
    b32(b, &tim, 0x10); b32(b, &tim, 2); b32(b, &tim, 256 * 64 * 2 + 12);
    b16(b, &tim, 0); b16(b, &tim, 0); b16(b, &tim, 64); b16(b, &tim, 256);
    for (i = 0; i < 256 * 64; i++) b16(b, &tim, rows[i]);
    b32(b, out, 8);
    b32(b, out, 0);
    rlenEncode(b, &rl, tim.p, tim.n);
    bput(b, out, rl.p, rl.n);
    bpad(b, out, 8);
}

typedef struct Entry {
    const uint8_t *p;
    size_t n;
    int has;
} Entry;

static void archive(Bk *b, Buf *out, const Entry *e, int n) {
    Buf body = {0, 0, 0};
    size_t start = ((size_t)n * 4 + 7) / 8 * 8;
    int i;
    for (i = 0; i < n; i++) {
        if (e[i].has) {
            b32(b, out, (unsigned long)(start + body.n));
            bput(b, &body, e[i].p, e[i].n);
            bpad(b, &body, 8);
        } else {
            b32(b, out, 0);
        }
    }
    bpad(b, out, 8);
    bput(b, out, body.p, body.n);
}

/* ---------------------------------------------------------------- animation */

typedef struct Spec {
    int used, anim, loop, hold, blendIn, blendOut, loopStart;
    double speed;
} Spec;

typedef struct Clip {
    int anim, loop, n, base;
    double speed, tmin, dur;
    double *poses; /* n frames x nb bones x 9 */
} Clip;

typedef struct Chans {
    int built;
    Sampler **s; /* nn * 3 */
    double tmin, tmax;
} Chans;

static int clamp16(Bk *b, double v, const char *what, int bone) {
    double r = rint(v);
    if (r < -32768 || r > 32767) {
        if (bone >= 0) fail(b, "%s of bone %d = %.0f does not fit the game's 16 bit values (lower \"scale\"/\"fitHeight\" or move the root)", what, bone, r);
        fail(b, "%s = %.0f does not fit the game's 16 bit values (lower \"scale\"/\"fitHeight\" or move the root)", what, r);
    }
    return (int)r;
}

/* Douglas-Peucker on a track of 3-vectors with linear interpolation: the kept indices, ascending */
static int reduceTrack(Bk *b, const double *vals, const int *frames, int n, double tol, int *keepOut) {
    char *keep;
    IVec stack = {0, 0, 0};
    int i, nk = 0;
    if (n <= 2) {
        for (i = 0; i < n; i++) keepOut[i] = i;
        return n;
    }
    keep = (char *)aalloc(b, (size_t)n);
    keep[0] = keep[n - 1] = 1;
    ipush(b, &stack, 0);
    ipush(b, &stack, n - 1);
    while (stack.n > 0) {
        int bb = stack.p[--stack.n], a = stack.p[--stack.n], wi = -1;
        double worst = -1.0, fa, span;
        const double *va, *vb;
        if (bb <= a + 1) continue;
        fa = frames[a];
        span = frames[bb] - frames[a];
        va = vals + 3 * a;
        vb = vals + 3 * bb;
        for (i = a + 1; i < bb; i++) {
            double t = (frames[i] - fa) / span, err = 0.0;
            const double *vi = vals + 3 * i;
            int j;
            for (j = 0; j < 3; j++) {
                double e = fabs(vi[j] - (va[j] + (vb[j] - va[j]) * t));
                if (j == 0 || e > err) err = e;
            }
            if (err > worst) {
                worst = err;
                wi = i;
            }
        }
        if (worst > tol) {
            keep[wi] = 1;
            ipush(b, &stack, a); ipush(b, &stack, wi);
            ipush(b, &stack, wi); ipush(b, &stack, bb);
        }
    }
    for (i = 0; i < n; i++) if (keep[i]) keepOut[nk++] = i;
    return nk;
}

/* ---------------------------------------------------------------- the driver */


static int hasContent(Build *bd, int i) {
    Bone *bn = bone(bd, i);
    int k;
    if (bn->polys.n) return 1;
    for (k = 0; k < bn->children.n; k++) if (hasContent(bd, bn->children.p[k])) return 1;
    return 0;
}

static int passthrough(Build *bd, int i) {
    Bone *bn = bone(bd, i);
    const Rest *r;
    int k;
    if (bd->sc->animated[bn->node] || bn->polys.n) return 0;
    r = &bd->sc->rest[bn->node];
    for (k = 0; k < 3; k++) if (fabs(r->t[k]) > 1e-9) return 0;
    for (k = 0; k < 4; k++) if (fabs(r->q[k] - (k == 3 ? 1.0 : 0.0)) > 1e-9) return 0;
    for (k = 0; k < 3; k++) if (fabs(r->s[k] - 1) > 1e-9) return 0;
    return 1;
}

static int truthy(const ExtJson *v) {
    if (v->t == 0 || v->t == 3) return v->num != 0.0;
    if (v->t == 1) return v->str != NULL && v->str[0] != '\0';
    return v->n > 0;
}

typedef struct AnimList {
    int n;
    char **name;
} AnimList;

static int animByName(const AnimList *al, const char *name) {
    int i;
    for (i = al->n - 1; i >= 0; i--) if (strcmp(al->name[i], name) == 0) return i;
    return -1;
}

static void makeSpec(Bk *b, Spec *sp, int slot, const ExtJson *v, const AnimList *al) {
    const ExtJson *an, *x;
    const char *animName;
    int idle = slot == 0 || slot == 1;
    memset(sp, 0, sizeof *sp);
    if (v->t == 1 && v->str != NULL) {
        animName = v->str;
    } else {
        an = isObj(v) ? jget(v, "animation") : NULL;
        if (!isStr(an)) fail(b, "motion slot %d: expected an animation name or {\"animation\": ...}", slot);
        animName = an->str;
    }
    sp->anim = animByName(al, animName);
    if (sp->anim < 0) {
        char list[200] = "";
        int i;
        for (i = 0; i < al->n && strlen(list) < 150; i++) {
            if (i) strcat(list, ", ");
            strncat(list, al->name[i], 40);
        }
        fail(b, "motion slot %d: the glTF has no animation \"%s\" (it has: %s)", slot, animName, al->n ? list : "none");
    }
    sp->used = 1;
    sp->speed = isObj(v) ? jnum(v, "speed", 1.0) : 1.0;
    x = isObj(v) ? jget(v, "loop") : NULL;
    sp->loop = x != NULL ? truthy(x) : idle;
    sp->blendIn = isObj(v) ? (int)jnum(v, "blendIn", idle ? 0 : BLEND_STEPS) : (idle ? 0 : BLEND_STEPS);
    sp->blendOut = isObj(v) ? (int)jnum(v, "blendOut", BLEND_STEPS) : BLEND_STEPS;
    sp->loopStart = isObj(v) ? (int)jnum(v, "loopStart", 0) : 0;
    if (sp->speed <= 0) fail(b, "motion slot %d: speed must be positive", slot);
    x = isObj(v) ? jget(v, "end") : NULL;
    if (x != NULL) {
        if (!isStr(x) || (strcmp(x->str, "idle") != 0 && strcmp(x->str, "hold") != 0)) fail(b, "motion slot %d: \"end\" is \"idle\" or \"hold\"", slot);
        sp->hold = strcmp(x->str, "hold") == 0;
    }
}

static void parseMotions(Bk *b, const ExtJson *meta, const AnimList *al, Spec *out) {
    const ExtJson *raw = jget(meta, "motions"), *fill;
    int i, slot;
    int pendSlot[SLOTS], pendSrc[SLOTS], np = 0;
    ExtJson synth;
    memset(&synth, 0, sizeof synth);
    if (raw == NULL) {
        /* animations named motion_NN */
        synth.t = 2;
        for (i = 0; i < al->n; i++) {
            const char *nm = al->name[i];
            size_t k;
            int digits = strlen(nm) > 7;
            if (strncmp(nm, "motion_", 7) != 0) continue;
            for (k = 7; nm[k] != '\0'; k++) if (!isdigit((unsigned char)nm[k])) digits = 0;
            if (!digits) continue;
            {
                int sl = atoi(nm + 7), e, found = -1;
                char key[16];
                snprintf(key, sizeof key, "%d", sl);
                for (e = 0; e < synth.n; e++) if (strcmp(synth.keys[e], key) == 0) found = e;
                if (found >= 0) {
                    synth.v[found].str = (char *)nm;
                } else {
                    char **nk = (char **)aalloc(b, sizeof(char *) * (size_t)(synth.n + 1));
                    ExtJson *nv = (ExtJson *)aalloc(b, sizeof(ExtJson) * (size_t)(synth.n + 1));
                    if (synth.n) {
                        memcpy(nk, synth.keys, sizeof(char *) * (size_t)synth.n);
                        memcpy(nv, synth.v, sizeof(ExtJson) * (size_t)synth.n);
                    }
                    nk[synth.n] = (char *)aalloc(b, strlen(key) + 1);
                    strcpy(nk[synth.n], key);
                    nv[synth.n].t = 1;
                    nv[synth.n].str = (char *)nm;
                    synth.keys = nk;
                    synth.v = nv;
                    synth.n++;
                }
            }
        }
        if (synth.n == 0) fail(b, "\"motions\" is required (slot -> animation) or name the animations motion_NN");
        raw = &synth;
    }
    if (!isObj(raw)) fail(b, "\"motions\" is an object: slot -> animation");
    for (i = 0; i < raw->n; i++) {
        char *end;
        long v = strtol(raw->keys[i], &end, 10);
        if (*end != '\0' || end == raw->keys[i]) fail(b, "motion slot \"%s\" is not a number", raw->keys[i]);
        slot = (int)v;
        if (v < 0 || v >= SLOTS) fail(b, "motion slot %ld is outside 0..%d", v, SLOTS - 1);
        if (raw->v[i].t == 0) {
            pendSlot[np] = slot;
            pendSrc[np] = (int)raw->v[i].num;
            np++;
        } else {
            makeSpec(b, &out[slot], slot, &raw->v[i], al);
        }
    }
    for (i = 0; i < np; i++) {
        int sl = pendSlot[i], src = pendSrc[i];
        if (src < 0 || src >= SLOTS || !out[src].used) fail(b, "motion slot %d copies slot %d, which is not defined", sl, src);
        out[sl] = out[src];
    }
    fill = jget(meta, "fill");
    if (fill != NULL) {
        for (slot = 0; slot < SLOTS; slot++) {
            if (out[slot].used) continue;
            makeSpec(b, &out[slot], slot, fill, al);
            out[slot].loop = 0;
        }
    }
}

static void localGame(const Bone *bn, double s, const double *t, const double *q, const double *sc, double *pos, M3 rmc, double *scl) {
    M3 rm, qm;
    int i, j;
    quatMatrix(qm, q);
    if (bn->top) {
        double mv[3], tt[3];
        matvec3(mv, bn->foldR, t);
        for (j = 0; j < 3; j++) tt[j] = bn->foldT[j] + bn->foldS * mv[j];
        matmul3(rm, bn->foldR, qm);
        for (j = 0; j < 3; j++) pos[j] = C_AXES[j] * (tt[j] * s);
    } else {
        memcpy(rm, qm, sizeof rm);
        for (j = 0; j < 3; j++) pos[j] = C_AXES[j] * ((t[j] * s) * bn->k);
    }
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) rmc[i][j] = (C_AXES[i] * C_AXES[j]) * rm[i][j];
    for (j = 0; j < 3; j++) scl[j] = sc[j] * 4096.0;
}

static Chans *channelsOf(Bk *b, Gltf *g, Scene *sc, Chans *cache, int ai) {
    Chans *c = &cache[ai];
    const ExtJson *a, *chs, *sms;
    int k, first = 1;
    if (c->built) return c;
    c->built = 1;
    c->s = (Sampler **)aalloc(b, (size_t)(sc->nn + 1) * 3 * sizeof(Sampler *));
    a = &g->anims->v[ai];
    chs = jget(a, "channels");
    sms = jget(a, "samplers");
    if (chs == NULL || sms == NULL) fail(b, "glTF: animation %d has no channels or samplers", ai);
    for (k = 0; k < chs->n; k++) {
        const ExtJson *tg = jget(&chs->v[k], "target"), *path = jget(tg, "path"), *node = jget(tg, "node");
        const ExtJson *sm;
        int pi, nd, cnt, nc, cnt2, nc2, want;
        double *times, *vals;
        Sampler *sp;
        const ExtJson *ip;
        if (node == NULL || !isStr(path)) continue;
        pi = !strcmp(path->str, "translation") ? 0 : !strcmp(path->str, "rotation") ? 1 : !strcmp(path->str, "scale") ? 2 : -1;
        if (pi < 0) continue;
        nd = idxOf(b, node, "an animation target");
        sm = elem(b, sms, jint(b, &chs->v[k], "sampler", -1), "sampler");
        times = accessor(b, g, idxOf(b, jget(sm, "input"), "sampler input"), &cnt, &nc);
        vals = accessor(b, g, idxOf(b, jget(sm, "output"), "sampler output"), &cnt2, &nc2);
        if (cnt == 0) continue;
        sp = (Sampler *)aalloc(b, sizeof *sp);
        ip = jget(sm, "interpolation");
        sp->interp = isStr(ip) && !strcmp(ip->str, "STEP") ? INTERP_STEP : isStr(ip) && !strcmp(ip->str, "CUBICSPLINE") ? INTERP_CUBIC : INTERP_LINEAR;
        want = pi == 1 ? 4 : 3;
        if (nc != 1 || nc2 != want || cnt2 < (sp->interp == INTERP_CUBIC ? 3 * cnt : cnt)) fail(b, "glTF: an animation sampler does not match its target (%s)", path->str);
        sp->n = cnt;
        sp->nc = nc2;
        sp->times = times;
        sp->vals = vals;
        c->s[nd * 3 + pi] = sp;
        if (first || times[0] < c->tmin) c->tmin = times[0];
        if (first || times[cnt - 1] > c->tmax) c->tmax = times[cnt - 1];
        first = 0;
    }
    return c;
}

/* The whole conversion of a loaded source. */
static void bakeBody(Bk *b, const ExtJson *meta, Gltf *g, ModelBaked *res) {
    Build bd;
    Vec primsV = {0, 0, 0};
    Prim *prims;
    int np, i, k, pi, n_all, pruned = 0, nkept = 0;
    char *keep;
    int *newidx;
    IVec order = {0, 0, 0};
    Bone *bones, *fb;
    double yaw, ps_, pc_, height, s;
    M3 P;
    const ExtJson *tolJ;
    int n0;

    memset(&bd, 0, sizeof bd);
    bd.b = b;
    bd.g = g;
    bd.meta = meta;
    bd.sc = makeScene(b, g);
    readPrimitives(b, g, bd.sc, &primsV);
    prims = (Prim *)primsV.p;
    np = primsV.n;
    skeleton(&bd, prims, np);
    polygons(&bd, prims, np);
    bones = (Bone *)bd.bones.p;
    n_all = bd.bones.n;
    for (pi = 0; pi < bd.polys.n; pi++) ipush(b, &bones[poly(&bd, pi)->bone].polys, pi);
    keep = (char *)aalloc(b, (size_t)n_all + 1);
    for (i = 0; i < n_all; i++) keep[i] = 1;
    for (i = 1; i < n_all; i++) if (!hasContent(&bd, i)) keep[i] = 0;
    for (i = 1; i < n_all; i++) if (keep[i]) nkept++;
    if (nkept == 0) fail(b, "no polygon is left: the model is empty");
    pruned = (n_all - 1) - nkept;
    for (i = 0; i < n_all; i++) {
        for (k = 0; k < bones[i].children.n; k++) if (keep[bones[i].children.p[k]]) ipush(b, &bones[i].kids, bones[i].children.p[k]);
    }
    {
        int changed = 1;
        while (changed) {
            IVec *k0 = &bones[0].kids;
            changed = 0;
            for (i = 0; i < k0->n; i++) {
                int c = k0->p[i], e;
                if (!passthrough(&bd, c)) continue;
                for (e = i; e < k0->n - 1; e++) k0->p[e] = k0->p[e + 1];
                k0->n--;
                for (e = 0; e < bones[c].kids.n; e++) {
                    ipush(b, k0, bones[c].kids.p[e]);
                    bones[bones[c].kids.p[e]].parent = 0;
                }
                keep[c] = 0;
                pruned++;
                changed = 1;
                break;
            }
        }
    }
    {
        /* walk(0): preorder over the kept children */
        IVec stack = {0, 0, 0};
        ipush(b, &stack, 0);
        while (stack.n > 0) {
            int cur = stack.p[--stack.n];
            ipush(b, &order, cur);
            for (k = bones[cur].kids.n - 1; k >= 0; k--) ipush(b, &stack, bones[cur].kids.p[k]);
        }
    }
    newidx = (int *)aalloc(b, (size_t)n_all * sizeof(int) + 4);
    for (i = 0; i < order.n; i++) newidx[order.p[i]] = i;
    n0 = order.n;
    fb = (Bone *)aalloc(b, (size_t)n0 * sizeof(Bone));
    for (i = 0; i < n0; i++) {
        fb[i] = bones[order.p[i]];
        fb[i].parent = order.p[i] ? newidx[fb[i].parent] : 0;
    }
    bones = fb;
    for (i = 0; i < n0; i++) bones[i].top = i > 0 && bones[i].parent == 0;
    if (pruned) note(b, 0, "skeleton: %d bones without polygons were dropped", pruned);

    /* unit scale: scale or fitHeight against the rest pose height */
    yaw = jnum(meta, "yaw", 0.0) * (PI_ / 180.0);
    ps_ = sin(yaw);
    pc_ = cos(yaw);
    P[0][0] = pc_; P[0][1] = 0; P[0][2] = ps_;
    P[1][0] = 0; P[1][1] = 1.0; P[1][2] = 0;
    P[2][0] = -ps_; P[2][1] = 0; P[2][2] = pc_;
    {
        double mny = 0, mxy = 0;
        int have = 0;
        for (pi = 0; pi < np; pi++) {
            const Prim *p = &prims[pi];
            Aff m;
            Aff *wr = NULL;
            const double **ibs = NULL;
            int *wrNode = NULL, nwr = 0, v;
            if (p->skin < 0) {
                worldRest(b, bd.sc, p->node, &m);
            } else {
                wr = (Aff *)aalloc(b, (size_t)(bd.skins[p->skin].nj + 1) * sizeof(Aff));
                ibs = (const double **)aalloc(b, (size_t)(bd.skins[p->skin].nj + 1) * sizeof(double *));
                wrNode = (int *)aalloc(b, (size_t)(bd.skins[p->skin].nj + 1) * sizeof(int));
            }
            for (v = 0; v < p->nv; v++) {
                double w[3], mv[3];
                if (p->skin < 0) {
                    matvec3(mv, m.m, p->pos + 3 * v);
                    w[1] = mv[1] + m.t[1];
                } else {
                    const Infl *f = p->infl != NULL ? &p->infl[v] : NULL;
                    int bestk = 0, j, e, slot = -1;
                    double loc[3];
                    if (f == NULL || f->n == 0) continue;
                    for (k = 1; k < f->n; k++) if (f->w[k] > f->w[bestk]) bestk = k;
                    j = bd.skins[p->skin].joints[f->j[bestk]];
                    for (e = 0; e < nwr; e++) if (wrNode[e] == j) slot = e;
                    if (slot < 0) {
                        slot = nwr++;
                        wrNode[slot] = j;
                        worldRest(b, bd.sc, j, &wr[slot]);
                        ibs[slot] = ibmFor(&bd.skins[p->skin], j);
                    }
                    toLocal(loc, ibs[slot], p->pos + 3 * v);
                    matvec3(mv, wr[slot].m, loc);
                    w[1] = mv[1] + wr[slot].t[1];
                }
                if (!have || w[1] < mny) mny = w[1];
                if (!have || w[1] > mxy) mxy = w[1];
                have = 1;
            }
        }
        height = have ? mxy - mny : 0.0;
    }
    if (jget(meta, "scale") != NULL && jget(meta, "fitHeight") != NULL) fail(b, "use \"scale\" or \"fitHeight\", not both");
    if (jget(meta, "fitHeight") != NULL) {
        if (height <= 0) fail(b, "cannot fit the height: the rest pose has no height");
        s = jnum(meta, "fitHeight", 0.0) / height;
    } else {
        s = jnum(meta, "scale", 1.0);
        if (jget(meta, "scale") == NULL && (height < 50 || height > 20000))
            note(b, 1, "the rest pose is %.4g units high: set \"scale\" or \"fitHeight\" (original fighters are 1300-10000 game units)", height);
    }
    note(b, 0, "model: %d bones, %d polygons, rest height %.4g glTF units, scale %.5g -> %.0f game units high", n0, bd.polys.n, height, s, height * s);

    /* fold the static nodes above each top bone */
    for (i = 1; i < n0; i++) {
        Aff m, pm;
        int anc[4097], na, j, ok;
        double cols[3][3], lens[3], r;
        if (!bones[i].top) continue;
        affIdentity(&m);
        na = ancestors(bd.sc, bones[i].node, anc);
        for (k = 0; k < na; k++) {
            Aff l;
            localMatrix(&l, &bd.sc->rest[anc[k]]);
            compose(&m, &m, &l);
        }
        affIdentity(&pm);
        memcpy(pm.m, P, sizeof P);
        compose(&m, &pm, &m);
        for (j = 0; j < 3; j++) for (k = 0; k < 3; k++) cols[j][k] = m.m[k][j];
        for (j = 0; j < 3; j++) lens[j] = vlen3(cols[j]);
        r = ((lens[0] + lens[1]) + lens[2]) / 3.0;
        ok = r > 1e-9;
        for (j = 0; j < 3; j++) if (!(fabs(lens[j] - r) < 1e-3 * r)) ok = 0;
        if (ok) {
            int a_, b_;
            for (a_ = 0; a_ < 3; a_++) {
                for (b_ = a_ + 1; b_ < 3; b_++) {
                    double d = (cols[a_][0] * cols[b_][0] + cols[a_][1] * cols[b_][1]) + cols[a_][2] * cols[b_][2];
                    if (fabs(d) > 1e-3 * r * r) ok = 0;
                }
            }
        }
        if (!ok) fail(b, "the nodes above the skeleton have a non-uniform scale or a shear: apply the scale in Blender (Ctrl+A) and re-export");
        for (j = 0; j < 3; j++) for (k = 0; k < 3; k++) bones[i].foldR[j][k] = m.m[j][k] / r;
        memcpy(bones[i].foldT, m.t, sizeof m.t);
        bones[i].foldS = r;
        bones[i].k = r;
    }
    for (i = 1; i < n0; i++) if (!bones[i].top) bones[i].k = bones[bones[i].parent].k;

    {
        /* ---- finish: polygons -> quantized local geometry */
        int *ids = (int *)aalloc(b, (size_t)(bd.polys.n + 1) * sizeof(int)), nids = 0;
        uint16_t *rows = (uint16_t *)aalloc(b, 256 * 64 * sizeof(uint16_t));
        TexStats ts;
        double (*restPos)[3], (*restRot)[3], (*restScl)[3];
        double tolPos, tolRot, tolScl;
        AnimList al;
        Spec specs[SLOTS];
        Chans *chCache;
        Clip *clips;
        int nclips = 0, nextBase = 10, slot, nslots = 0;
        Vec slotSteps[SLOTS];
        Track (*tracks)[3];
        int nb, extra = 0;
        int *parents;
        Track **tracksAll;
        Buf *boneMeshes;
        Track identity[3];

        for (i = 0; i < n0; i++) {
            for (k = 0; k < bones[i].polys.n; k++) {
                poly(&bd, bones[i].polys.p[k])->bone = i;
                ids[nids++] = bones[i].polys.p[k];
            }
        }
        for (pi = 0; pi < nids; pi++) {
            Poly *p = poly(&bd, ids[pi]);
            double kk = bones[p->bone].k * s;
            int c, j;
            for (c = 0; c < 3; c++) {
                double lv[3], nn[3], ln;
                for (j = 0; j < 3; j++) lv[j] = C_AXES[j] * p->c[c].pos[j];
                for (j = 0; j < 3; j++) p->pos[c][j] = clamp16(b, lv[j] * kk, "vertex", -1);
                for (j = 0; j < 3; j++) nn[j] = C_AXES[j] * p->c[c].nrm[j];
                ln = vlen3(nn);
                if (ln < 1e-12) {
                    nn[0] = 0.0; nn[1] = -1.0; nn[2] = 0.0;
                    ln = 1.0;
                }
                for (j = 0; j < 3; j++) p->nrm[c][j] = (int)rint(nn[j] / ln * 4096);
            }
        }
        buildTexture(&bd, ids, nids, rows, &ts);

        /* ---- animations */
        restPos = (double (*)[3])aalloc(b, (size_t)n0 * sizeof *restPos);
        restRot = (double (*)[3])aalloc(b, (size_t)n0 * sizeof *restRot);
        restScl = (double (*)[3])aalloc(b, (size_t)n0 * sizeof *restScl);
        for (i = 1; i < n0; i++) {
            const Rest *r = &bd.sc->rest[bones[i].node];
            M3 rm;
            localGame(&bones[i], s, r->t, r->q, r->s, restPos[i], rm, restScl[i]);
            rawAngles(restRot[i], rm, NULL);
        }
        tolJ = jget(meta, "tolerance");
        tolPos = jnum(tolJ, "position", (0.001 * height) * s > 0.5 ? (0.001 * height) * s : 0.5);
        tolRot = jnum(tolJ, "rotation", 4.0);
        tolScl = jnum(tolJ, "scale", 16.0);

        al.n = g->anims != NULL ? g->anims->n : 0;
        al.name = (char **)aalloc(b, (size_t)(al.n + 1) * sizeof(char *));
        for (i = 0; i < al.n; i++) {
            const ExtJson *nm = jget(&g->anims->v[i], "name");
            if (isStr(nm) && nm->str[0] != '\0') al.name[i] = nm->str;
            else {
                al.name[i] = (char *)aalloc(b, 24);
                snprintf(al.name[i], 24, "animation_%d", i);
            }
        }
        memset(specs, 0, sizeof specs);
        parseMotions(b, meta, &al, specs);
        chCache = (Chans *)aalloc(b, (size_t)(al.n + 1) * sizeof(Chans));
        clips = (Clip *)aalloc(b, (size_t)(SLOTS + 1) * sizeof(Clip));
        memset(slotSteps, 0, sizeof slotSteps);

        for (slot = 0; slot < SLOTS; slot++) {
            const Spec *sp = &specs[slot];
            Clip *clip = NULL;
            int bi_, bo_, idx = 0, loopTo, f, ci;
            typedef struct Step { int a, b, c, d; } Step;
            if (!sp->used) continue;
            for (ci = 0; ci < nclips; ci++) {
                if (clips[ci].anim == sp->anim && clips[ci].speed == sp->speed && clips[ci].loop == sp->loop) clip = &clips[ci];
            }
            if (clip == NULL) {
                Chans *ch = channelsOf(b, g, bd.sc, chCache, sp->anim);
                double dur = ch->tmax - ch->tmin, tmin = ch->tmin;
                int n = (int)rint(dur * 60.0 / sp->speed) + 1;
                double *poses;
                if (n < 1) n = 1;
                if (n > SENTINEL) fail(b, "the motions need more than %d frames on the shared timeline: shorten or speed up some (\"speed\")", SENTINEL);
                if ((size_t)n * (size_t)n0 > 40u * 1000u * 1000u / 9u) fail(b, "a motion is too long to sample");
                poses = (double *)aalloc(b, (size_t)n * (size_t)n0 * 9 * sizeof(double));
                {
                    double (*pv)[3] = (double (*)[3])aalloc(b, (size_t)n0 * sizeof *pv);
                    for (i = 1; i < n0; i++) memcpy(pv[i], restRot[i], sizeof pv[i]);
                    for (f = 0; f < n; f++) {
                        double t = tmin + (n > 1 ? (dur * f) / (n - 1) : 0.0);
                        for (i = 1; i < n0; i++) {
                            int node = bones[i].node;
                            const Rest *r = &bd.sc->rest[node];
                            double tr[3], q[4], sc_[3], *o = poses + ((size_t)f * (size_t)n0 + (size_t)i) * 9, rm[3][3], rot[3];
                            Sampler *sT = ch->s[node * 3], *sR = ch->s[node * 3 + 1], *sS = ch->s[node * 3 + 2];
                            memcpy(tr, r->t, sizeof tr);
                            memcpy(q, r->q, sizeof q);
                            memcpy(sc_, r->s, sizeof sc_);
                            if (sT != NULL) evalSampler(tr, sT, t, 0);
                            if (sR != NULL) evalSampler(q, sR, t, 1);
                            if (sS != NULL) evalSampler(sc_, sS, t, 0);
                            localGame(&bones[i], s, tr, q, sc_, o, rm, o + 6);
                            rawAngles(rot, rm, pv[i]);
                            memcpy(pv[i], rot, sizeof rot);
                            memcpy(o + 3, rot, sizeof rot);
                        }
                    }
                }
                if (sp->loop && n > 2) {
                    int same = 1;
                    for (i = 1; i < n0 && same; i++) {
                        const double *a0 = poses + (size_t)i * 9, *a1 = poses + ((size_t)(n - 1) * (size_t)n0 + (size_t)i) * 9;
                        double mx = 0.0;
                        for (k = 0; k < 9; k++) {
                            double d = fabs(a0[k] - a1[k]);
                            if (k == 0 || d > mx) mx = d;
                        }
                        if (!(mx < 1.0)) same = 0;
                    }
                    if (same) n--;
                }
                clip = &clips[nclips++];
                clip->anim = sp->anim;
                clip->speed = sp->speed;
                clip->loop = sp->loop;
                clip->n = n;
                clip->tmin = tmin;
                clip->dur = dur;
                clip->base = nextBase;
                clip->poses = poses;
                nextBase += n + 2;
                if (nextBase >= SENTINEL - 10) fail(b, "the motions need more than %d frames on the shared timeline: shorten or speed up some (\"speed\")", SENTINEL);
            }
            bi_ = sp->blendIn;
            bo_ = sp->blendOut;
            {
                Vec *st = &slotSteps[slot];
                Step *x;
                if (bi_) {
                    x = (Step *)vpush(b, st, sizeof *x);
                    x->a = idx; x->b = bi_; x->c = 0; x->d = 0;
                    idx += bi_;
                }
                x = (Step *)vpush(b, st, sizeof *x);
                x->a = idx; x->b = clip->n; x->c = clip->base; x->d = clip->base + clip->n - 1;
                loopTo = idx + (sp->loopStart < 0 ? 0 : sp->loopStart > clip->n - 1 ? clip->n - 1 : sp->loopStart);
                idx += clip->n;
                if (sp->loop) {
                    x = (Step *)vpush(b, st, sizeof *x);
                    x->a = idx; x->b = 0; x->c = -32768; x->d = loopTo;
                } else if (sp->hold || bo_ <= 0) {
                    x = (Step *)vpush(b, st, sizeof *x);
                    x->a = idx; x->b = 0; x->c = -1; x->d = 0;
                } else {
                    x = (Step *)vpush(b, st, sizeof *x);
                    x->a = idx; x->b = bo_; x->c = 0; x->d = 0;
                    idx += bo_;
                    x = (Step *)vpush(b, st, sizeof *x);
                    x->a = idx; x->b = 0; x->c = -1; x->d = 0;
                }
            }
            if (idx + 1 > MAX_STEPS) fail(b, "motion slot %d has %d steps (the game keeps %d): speed it up or shorten it", slot, idx, MAX_STEPS);
            if (idx > 1500) note(b, 1, "motion slot %d has %d steps (limit %d)", slot, idx, MAX_STEPS);
            nslots++;
        }
        if (!specs[0].used) fail(b, "motion slot 0 (idle) is required: map an animation to it");
        if (!specs[1].used) fail(b, "motion slot 1 (idle) is required: map an animation to it");
        {
            char miss[160] = "";
            int e, any = 0;
            for (e = 0; e < 9; e++) {
                if (!specs[MIN_SLOTS[e]].used) {
                    char one[16];
                    snprintf(one, sizeof one, "%s%d", any ? ", " : "", MIN_SLOTS[e]);
                    strcat(miss, one);
                    any = 1;
                }
            }
            if (any) note(b, 1, "motion slots [%s] are not mapped: fighters need [0, 1, 2, 3, 4, 10, 11, 12, 14] (the effect scripts of the donor play them; see \"fill\")", miss);
        }
        /* ---- keys per bone */
        tracks = (Track (*)[3])aalloc(b, (size_t)n0 * sizeof *tracks);
        for (i = 1; i < n0; i++) {
            int comp;
            for (comp = 0; comp < 3; comp++) {
                const double *rest = comp == 0 ? restPos[i] : comp == 1 ? restRot[i] : restScl[i];
                double tol = comp == 0 ? tolPos : comp == 1 ? tolRot : tolScl;
                const char *rnd = comp == 0 ? "position" : comp == 1 ? "rotation" : "scale";
                Vec keysV = {0, 0, 0};
                int cst = 1, ci, e;
                typedef struct DKey { double v[3]; int f; } DKey;
                DKey *kx;
                Track *tr = &tracks[i][comp];
                kx = (DKey *)vpush(b, &keysV, sizeof *kx);
                memcpy(kx->v, rest, sizeof kx->v);
                kx->f = 0;
                for (ci = 0; ci < nclips; ci++) {
                    const Clip *c = &clips[ci];
                    double *vals = (double *)aalloc(b, (size_t)c->n * 3 * sizeof(double));
                    int *frames = (int *)aalloc(b, (size_t)c->n * sizeof(int)), *kept = (int *)aalloc(b, (size_t)c->n * sizeof(int)), nk, f;
                    for (f = 0; f < c->n; f++) {
                        const double *pp = c->poses + ((size_t)f * (size_t)n0 + (size_t)i) * 9 + comp * 3;
                        memcpy(vals + 3 * f, pp, 3 * sizeof(double));
                        frames[f] = c->base + f;
                        {
                            double mx = 0.0;
                            for (e = 0; e < 3; e++) {
                                double d = fabs(pp[e] - rest[e]);
                                if (e == 0 || d > mx) mx = d;
                            }
                            if (mx > tol) cst = 0;
                        }
                    }
                    nk = reduceTrack(b, vals, frames, c->n, tol, kept);
                    for (e = 0; e < nk; e++) {
                        kx = (DKey *)vpush(b, &keysV, sizeof *kx);
                        memcpy(kx->v, vals + 3 * kept[e], sizeof kx->v);
                        kx->f = frames[kept[e]];
                    }
                }
                if (cst) keysV.n = 1;
                kx = (DKey *)vpush(b, &keysV, sizeof *kx);
                memcpy(kx->v, ((DKey *)keysV.p)[keysV.n - 2].v, sizeof kx->v);
                kx->f = SENTINEL;
                tr->n = keysV.n;
                tr->k = (Key *)aalloc(b, (size_t)keysV.n * sizeof(Key));
                for (e = 0; e < keysV.n; e++) {
                    const DKey *dk = &((DKey *)keysV.p)[e];
                    tr->k[e].x = clamp16(b, dk->v[0], rnd, i);
                    tr->k[e].y = clamp16(b, dk->v[1], rnd, -1);
                    tr->k[e].z = clamp16(b, dk->v[2], rnd, -1);
                    tr->k[e].f = dk->f;
                }
            }
            {
                const Track *t2 = &tracks[i][2];
                int e;
                for (e = 0; e < t2->n; e++) {
                    const Key *sx = &t2->k[e];
                    int mx = abs(sx->x) > abs(sx->y) ? abs(sx->x) : abs(sx->y);
                    if (abs(sx->z) > mx) mx = abs(sx->z);
                    if (0 < mx && mx <= 48 && (sx->x | sx->y | sx->z) > 0 && sx->x != 0 && sx->y != 0 && sx->z != 0) {
                        note(b, 1, "bone %d has a scale of %.4f or less: the game hides such a bone", i, 48 / 4096.0);
                        break;
                    }
                }
            }
        }
        /* ---- meshes: chunk per bone, extra child bones */
        identity[0].n = identity[1].n = identity[2].n = 2;
        identity[0].k = (Key *)aalloc(b, 2 * sizeof(Key));
        identity[1].k = identity[0].k;
        identity[2].k = (Key *)aalloc(b, 2 * sizeof(Key));
        identity[0].k[1].f = SENTINEL;
        identity[2].k[0].x = identity[2].k[0].y = identity[2].k[0].z = 4096;
        identity[2].k[1].x = identity[2].k[1].y = identity[2].k[1].z = 4096;
        identity[2].k[1].f = SENTINEL;

        parents = (int *)aalloc(b, (size_t)(n0 + 4096) * sizeof(int));
        tracksAll = (Track **)aalloc(b, (size_t)(n0 + 4096) * sizeof(Track *));
        boneMeshes = (Buf *)aalloc(b, (size_t)(n0 + 4096) * sizeof(Buf));
        for (i = 1; i < n0; i++) {
            parents[i] = bones[i].parent;
            tracksAll[i] = tracks[i];
        }
        nb = n0;
        for (i = 1; i < n0; i++) {
            /* chunks of the bone's polygons: no more than MAX_VERTS distinct vertices or normals each */
            const IVec *pl = &bones[i].polys;
            int start = 0, e, ci = 0;
            while (1) {
                int cnt = 0, nv = 0, nn = 0, end = start;
                int (*vs)[3] = (int (*)[3])aalloc(b, (size_t)(MAX_VERTS + 4) * sizeof *vs);
                int (*ns)[3] = (int (*)[3])aalloc(b, (size_t)(MAX_VERTS + 4) * sizeof *ns);
                OutPoly *outp;
                int (*verts)[3], (*norms)[3];
                int *sortIdx, nverts = 0, nnorms = 0;
                Buf mb = {0, 0, 0};
                for (e = start; e < pl->n; e++) {
                    const Poly *p = poly(&bd, pl->p[e]);
                    int c, f, pv[3][3], pn[3][3], npv = 0, npn = 0;
                    /* the distinct vertices and normals this polygon would add to the chunk */
                    for (c = 0; c < 3; c++) {
                        int dup = 0;
                        for (f = 0; f < nv; f++) if (memcmp(vs[f], p->pos[c], sizeof(int[3])) == 0) dup = 1;
                        for (f = 0; f < npv; f++) if (memcmp(pv[f], p->pos[c], sizeof(int[3])) == 0) dup = 1;
                        if (!dup) memcpy(pv[npv++], p->pos[c], sizeof(int[3]));
                        dup = 0;
                        for (f = 0; f < nn; f++) if (memcmp(ns[f], p->nrm[c], sizeof(int[3])) == 0) dup = 1;
                        for (f = 0; f < npn; f++) if (memcmp(pn[f], p->nrm[c], sizeof(int[3])) == 0) dup = 1;
                        if (!dup) memcpy(pn[npn++], p->nrm[c], sizeof(int[3]));
                    }
                    if (cnt > 0 && (nv + npv > MAX_VERTS || nn + npn > MAX_VERTS)) break;
                    for (c = 0; c < npv; c++) memcpy(vs[nv++], pv[c], sizeof(int[3]));
                    for (c = 0; c < npn; c++) memcpy(ns[nn++], pn[c], sizeof(int[3]));
                    cnt++;
                    end = e + 1;
                }
                if (pl->n == 0) end = start;
                /* the chunk is polygons [start, end) */
                outp = (OutPoly *)aalloc(b, (size_t)(end - start + 1) * sizeof *outp);
                sortIdx = (int *)aalloc(b, (size_t)(end - start + 1) * sizeof(int));
                for (e = 0; e < end - start; e++) sortIdx[e] = pl->p[start + e];
                /* stable by clut (x, y) */
                {
                    int a2, b2;
                    for (a2 = 1; a2 < end - start; a2++) {
                        int cur = sortIdx[a2];
                        const Poly *pc2 = poly(&bd, cur);
                        b2 = a2 - 1;
                        while (b2 >= 0) {
                            const Poly *pb2 = poly(&bd, sortIdx[b2]);
                            if (pb2->clutX > pc2->clutX || (pb2->clutX == pc2->clutX && pb2->clutY > pc2->clutY)) {
                                sortIdx[b2 + 1] = sortIdx[b2];
                                b2--;
                            } else {
                                break;
                            }
                        }
                        sortIdx[b2 + 1] = cur;
                    }
                }
                verts = (int (*)[3])aalloc(b, (size_t)(MAX_VERTS + 4) * sizeof *verts);
                norms = (int (*)[3])aalloc(b, (size_t)(MAX_VERTS + 4) * sizeof *norms);
                for (e = 0; e < end - start; e++) {
                    const Poly *p = poly(&bd, sortIdx[e]);
                    int c, f;
                    for (c = 0; c < 3; c++) {
                        for (f = 0; f < nverts && memcmp(verts[f], p->pos[c], sizeof(int[3])) != 0; f++) {}
                        if (f == nverts) memcpy(verts[nverts++], p->pos[c], sizeof(int[3]));
                        outp[e].idx[c] = f;
                        for (f = 0; f < nnorms && memcmp(norms[f], p->nrm[c], sizeof(int[3])) != 0; f++) {}
                        if (f == nnorms) memcpy(norms[nnorms++], p->nrm[c], sizeof(int[3]));
                        outp[e].nrm[c] = f;
                        outp[e].uv[c][0] = p->uv[c][0];
                        outp[e].uv[c][1] = p->uv[c][1];
                    }
                    outp[e].clutX = p->clutX;
                    outp[e].clutY = p->clutY;
                }
                encodeMesh(b, &mb, (const int (*)[3])verts, nverts, (const int (*)[3])norms, nnorms, outp, end - start);
                if (ci == 0) {
                    boneMeshes[i] = mb;
                } else {
                    if (nb >= n0 + 4000) fail(b, "too many extra bones");
                    parents[nb] = i;
                    tracksAll[nb] = identity;
                    boneMeshes[nb] = mb;
                    nb++;
                    extra++;
                }
                ci++;
                if (end >= pl->n) break;
                start = end;
            }
        }
        if (extra) note(b, 0, "%d extra child bones carry the polygons of bones with more than %d vertices or normals", extra, MAX_VERTS);
        if (nb > MAX_BONES) fail(b, "%d bones: the battle layer has 100 callbacks for all fighters on screen (limit %d)", nb, MAX_BONES);
        if (nb > WARN_BONES) note(b, 1, "%d bones: the 100 callbacks of the battle layer are shared by every fighter on screen", nb);
        if (nids > WARN_POLYS) note(b, 1, "%d polygons: the biggest original has 1334", nids);
        {
            /* ---- file layout: 0 motions, 1 model, 2 texture, then (mesh, keys) per bone */
            int ne = 3 + 2 * (nb - 1), e;
            Entry *entries = (Entry *)aalloc(b, (size_t)ne * sizeof(Entry));
            Buf texArc = {0, 0, 0}, modelE = {0, 0, 0}, motionsA = {0, 0, 0}, blob = {0, 0, 0};
            Buf *keyBufs = (Buf *)aalloc(b, (size_t)nb * sizeof(Buf));
            Buf *stepBufs = (Buf *)aalloc(b, SLOTS * sizeof(Buf));
            Entry motionEntries[SLOTS];
            encodeTextureArchive(b, &texArc, rows);
            entries[2].p = texArc.p; entries[2].n = texArc.n; entries[2].has = 1;
            b32(b, &modelE, 2);
            b32(b, &modelE, (unsigned long)(nb - 1));
            for (i = 1; i < nb; i++) {
                int mesh_e = 3 + 2 * (i - 1), keys_e = mesh_e + 1;
                entries[mesh_e].p = boneMeshes[i].p; entries[mesh_e].n = boneMeshes[i].n; entries[mesh_e].has = 1;
                encodeKeys(b, &keyBufs[i], tracksAll[i]);
                entries[keys_e].p = keyBufs[i].p; entries[keys_e].n = keyBufs[i].n; entries[keys_e].has = 1;
                b32(b, &modelE, (unsigned long)parents[i]);
                b32(b, &modelE, (unsigned long)mesh_e);
                b32(b, &modelE, (unsigned long)keys_e);
            }
            bpad(b, &modelE, 8);
            entries[1].p = modelE.p; entries[1].n = modelE.n; entries[1].has = 1;
            memset(motionEntries, 0, sizeof motionEntries);
            for (slot = 0; slot < SLOTS; slot++) {
                Buf *sb = &stepBufs[slot];
                if (!specs[slot].used) continue;
                for (e = 0; e < slotSteps[slot].n; e++) {
                    const int *st = (const int *)slotSteps[slot].p + 4 * e;
                    putS16(b, sb, st[0]); putS16(b, sb, st[1]); putS16(b, sb, st[2]); putS16(b, sb, st[3]);
                }
                putS16(b, sb, 0x7FFF); putS16(b, sb, 0); putS16(b, sb, 0); putS16(b, sb, 0);
                motionEntries[slot].p = sb->p;
                motionEntries[slot].n = sb->n;
                motionEntries[slot].has = 1;
            }
            archive(b, &motionsA, motionEntries, SLOTS);
            entries[0].p = motionsA.p; entries[0].n = motionsA.n; entries[0].has = 1;
            archive(b, &blob, entries, ne);
            res->blob = (uint8_t *)malloc(blob.n);
            if (res->blob == NULL) fail(b, "out of memory");
            memcpy(res->blob, blob.p, blob.n);
            res->len = blob.n;
            res->bones = nb;
            res->polys = nids;
            res->clips = nclips;
            res->slots = nslots;
            res->islands = ts.islands;
            res->palettes = ts.palettes;
            res->rms = ts.rms;
        }
    }
}

/* ---------------------------------------------------------------- entry points */

static void fnvInit(Bk *b) {
    char v[16];
    b->hash = 0xcbf29ce484222325ULL;
    snprintf(v, sizeof v, "bake%d", MODEL_BAKE_VERSION);
    hashBytes(b, v, strlen(v));
}

static int nameOk(const char *n) {
    size_t i;
    if (n == NULL || n[0] == '\0') return 0;
    for (i = 0; n[i] != '\0'; i++) if (!isalnum((unsigned char)n[i]) && n[i] != '-' && n[i] != '_') return 0;
    return 1;
}

/* "from": "dw3:model/<id>" -> the fighter id; -1 when the key is absent */
static int parseFrom(Bk *b, const ExtJson *meta) {
    const ExtJson *v = jget(meta, "from");
    const char *s;
    char *end;
    long id;
    if (v == NULL) return -1;
    if (!isStr(v)) fail(b, "\"from\" must be a text like \"dw3:model/383\"");
    s = v->str;
    if (strncmp(s, "dw3:model/", 10) != 0) fail(b, "\"from\" must be \"dw3:model/<fighter id>\", not \"%s\"", s);
    id = strtol(s + 10, &end, 10);
    if (end == s + 10 || *end != '\0' || id < 0 || id > 100000) fail(b, "\"from\" must be \"dw3:model/<fighter id>\", not \"%s\"", s);
    if (b->orig == NULL || b->orig->build == NULL) fail(b, "\"from\": the original models need the disc, which is not available");
    return (int)id;
}

/* the top-level keys of `mod` over those of `base` (a new object in the arena; the values are the trees' own). "motions" is merged one
   level down, slot by slot, the mod's slot winning */
static ExtJson *mergeObj(Bk *b, const ExtJson *base, const ExtJson *mod, int slots) {
    ExtJson *m = (ExtJson *)aalloc(b, sizeof *m);
    int i, j;
    m->t = 2;
    m->keys = (char **)aalloc(b, sizeof(char *) * (size_t)(base->n + mod->n + 1));
    m->v = (ExtJson *)aalloc(b, sizeof(ExtJson) * (size_t)(base->n + mod->n + 1));
    for (i = 0; i < base->n; i++) {
        m->keys[i] = base->keys[i];
        m->v[i] = base->v[i];
    }
    m->n = base->n;
    for (i = 0; i < mod->n; i++) {
        for (j = 0; j < m->n && strcmp(m->keys[j], mod->keys[i]) != 0; j++) {}
        if (j == m->n) {
            m->keys[j] = mod->keys[i];
            m->n++;
        }
        if (slots && j < base->n && strcmp(mod->keys[i], "motions") == 0 && isObj(&base->v[j]) && isObj(&mod->v[i])) {
            ExtJson *mm = mergeObj(b, &base->v[j], &mod->v[i], 0);
            m->v[j] = *mm;
        } else {
            m->v[j] = mod->v[i];
        }
    }
    return m;
}

/* the mod's model.json completed with the original's */
static const ExtJson *completeMeta(Bk *b, const ExtJson *meta) {
    ExtJson *base = (ExtJson *)aalloc(b, sizeof *base);
    loadOriginal(b);
    if (extJsonParse(b->ex.json, b->ex.jsonLen, base) != 0) fail(b, "\"from\": the original's model.json is not valid");
    b->json[b->njson++] = base;
    return mergeObj(b, base, meta, 1);
}

/* the key-pass hash of a mod's files that the original's would otherwise provide */
static int localFile(const char *dir, const char *rel) {
    char path[PP_CAP];
    return relOk(rel) && portJoin(path, sizeof path, dir, rel) && portIsFile(path);
}

static const char *checkMeta(Bk *b, const ExtJson *meta) {
    const ExtJson *fmt = jget(meta, "format"), *gl;
    if (!isStr(fmt) || strcmp(fmt->str, FORMAT) != 0) fail(b, "\"format\" must be \"%s\"", FORMAT);
    if (!nameOk(metaStr(meta, "name"))) fail(b, "\"name\" is required: letters, digits, - and _");
    gl = jget(meta, "gltf");
    if (!isStr(gl)) fail(b, "\"gltf\": path of the .gltf/.glb (relative to this file) is required");
    return gl->str;
}

int modelBake(const char *dir, const char *metaFile, ModelLogFn log, void *ud, const ModelCacheHooks *cache, ModelBaked *out, char *err, size_t errCap) {
    return modelBakeFrom(dir, metaFile, log, ud, cache, NULL, out, err, errCap);
}

int modelBakeFrom(const char *dir, const char *metaFile, ModelLogFn log, void *ud, const ModelCacheHooks *cache, const ModelOriginals *orig,
                  ModelBaked *out, char *err, size_t errCap) {
    Bk *b = (Bk *)calloc(1, sizeof *b);
    volatile int rc = -1;
    if (b == NULL) {
        snprintf(err, errCap, "out of memory");
        return -1;
    }
    memset(out, 0, sizeof *out);
    b->log = log;
    b->ud = ud;
    b->baseDir = dir;
    b->orig = orig;
    b->fromId = -1;
    if (setjmp(b->jb) == 0) {
        Bytes mb;
        ExtJson *meta = (ExtJson *)aalloc(b, sizeof *meta);
        const ExtJson *use = meta;
        const char *glRel;
        Gltf *g = NULL;
        fnvInit(b);
        mb = readFile(b, dir, metaFile, "model.json");
        hashBytes(b, mb.p, mb.n);
        if (extJsonParse(mb.p, mb.n, meta) != 0) fail(b, "%s is not a JSON object", metaFile);
        b->json[b->njson++] = meta;
        b->fromId = parseFrom(b, meta);
        if (b->fromId < 0) {
            glRel = checkMeta(b, meta);
            g = loadGltf(b, dir, glRel);
        } else {
            /* the key pass: the mod's own files and the original's id and exporter version; the original is not built */
            char v[40];
            const ExtJson *gl = jget(meta, "gltf");
            if (gl != NULL && !isStr(gl)) fail(b, "\"gltf\": path of the .gltf/.glb (relative to this file) is required");
            glRel = gl != NULL ? gl->str : "model.gltf";
            snprintf(v, sizeof v, "from%d/export%d", b->fromId, MODEL_EXPORT_VERSION);
            hashBytes(b, v, strlen(v));
            b->dry = 1;
            if (localFile(dir, glRel)) {
                loadGltf(b, dir, glRel);
            } else {
                static const char *const std[2] = {"model.bin", "model.png"};
                int i;
                for (i = 0; i < 2; i++) {
                    if (!localFile(dir, std[i])) continue;
                    hashBytes(b, std[i], strlen(std[i]));
                    mb = readFile(b, dir, std[i], "texture");
                    hashBytes(b, mb.p, mb.n);
                }
            }
            b->dry = 0;
        }
        out->key = b->hash;
        if (cache != NULL && cache->get != NULL && cache->get(cache->ud, out->key, &out->blob, &out->len) && out->blob != NULL && out->len > 0) {
            out->cached = 1;
            rc = 0;
        } else {
            out->blob = NULL;
            if (b->fromId >= 0) {
                use = completeMeta(b, meta);
                glRel = checkMeta(b, use);
                g = loadGltf(b, dir, glRel);
            }
            bakeBody(b, use, g, out);
            if (cache != NULL && cache->put != NULL) cache->put(cache->ud, out->key, out->blob, out->len);
            rc = 0;
        }
    } else {
        if (out->blob != NULL && !out->cached) free(out->blob);
        memset(out, 0, sizeof *out);
        snprintf(err, errCap, "%s", b->err);
    }
    afreeAll(b);
    free(b);
    return rc;
}

void modelBakedFree(ModelBaked *m) {
    free(m->blob);
    memset(m, 0, sizeof *m);
}

int modelSourceInfo(const char *dir, const char *metaFile, ModelSrcInfo *out, char *err, size_t errCap) {
    return modelSourceInfoFrom(dir, metaFile, NULL, out, err, errCap);
}

int modelSourceInfoFrom(const char *dir, const char *metaFile, const ModelOriginals *orig, ModelSrcInfo *out, char *err, size_t errCap) {
    Bk *b = (Bk *)calloc(1, sizeof *b);
    volatile int rc = -1;
    if (b == NULL) {
        snprintf(err, errCap, "out of memory");
        return -1;
    }
    out->templateId = out->effectsId = -1;
    out->distance = out->height = INT_MIN;
    b->orig = orig;
    b->fromId = -1;
    if (setjmp(b->jb) == 0) {
        Bytes mb = readFile(b, dir, metaFile, "model.json");
        ExtJson *meta0 = (ExtJson *)aalloc(b, sizeof *meta0);
        const ExtJson *meta = meta0, *info, *x;
        const char *s;
        if (extJsonParse(mb.p, mb.n, meta0) != 0) fail(b, "%s is not a JSON object", metaFile);
        b->json[b->njson++] = meta0;
        b->fromId = parseFrom(b, meta0);
        if (b->fromId >= 0 && (jget(meta0, "template") == NULL || jget(meta0, "effects") == NULL || jget(meta0, "info") == NULL))
            meta = completeMeta(b, meta0); /* the original is built only when something is missing */
        if (!isStr(jget(meta, "format")) || strcmp(jget(meta, "format")->str, FORMAT) != 0) {
            if (b->fromId < 0 || jget(meta, "format") != NULL) fail(b, "\"format\" must be \"%s\"", FORMAT);
        }
        s = metaStr(meta, "template");
        if (s != NULL) out->templateId = contentNamedId("digimon", s);
        else if (isNum(jget(meta, "template")) && extWhole(jget(meta, "template")->num, 0, 100000)) out->templateId = (int)jget(meta, "template")->num;
        s = metaStr(meta, "effects");
        if (s != NULL) out->effectsId = contentNamedId("digimon", s);
        else if (isNum(jget(meta, "effects")) && extWhole(jget(meta, "effects")->num, 0, 100000)) out->effectsId = (int)jget(meta, "effects")->num;
        info = jget(meta, "info");
        if ((x = jget(info, "distance")) != NULL && isNum(x) && extWhole(x->num, -32768, 32767)) out->distance = (int)x->num;
        if ((x = jget(info, "height")) != NULL && isNum(x) && extWhole(x->num, -32768, 32767)) out->height = (int)x->num;
        rc = 0;
    } else {
        snprintf(err, errCap, "%s", b->err);
    }
    afreeAll(b);
    free(b);
    return rc;
}

/* ---------------------------------------------------------------- the registry of the active mods and the cache */

#define MODEL_NAMES 64
static struct {
    char name[96];
    int file;
} sBaked[MODEL_NAMES];
static int sBakedN;
static ModelOriginals sOriginals; /* for "from": set by the host once the disc reader exists; empty without a disc */

void modelsSetOriginals(const ModelOriginals *orig) {
    if (orig != NULL) sOriginals = *orig;
    else memset(&sOriginals, 0, sizeof sOriginals);
}

/* "<mod>:<key>" -> mods/<mod>/content/models/<key> (a content mod's folder), 1 when model.json is there */
static int sourceDir(const char *name, char *dir, size_t cap) {
    const char *colon = strchr(name, ':'), *key;
    char mod[96], sub[160];
    int m;
    if (colon == NULL || colon == name || (size_t)(colon - name) >= sizeof mod) return 0;
    key = colon + 1;
    if (!extValidKey(key)) return 0;
    memcpy(mod, name, (size_t)(colon - name));
    mod[colon - name] = '\0';
    for (m = 0; m < modsCount() && strcmp(modsNameOf(m), mod) != 0; m++) {}
    if (m == modsCount() || strcmp(modsKindOf(m), "content") != 0) return 0;
    snprintf(sub, sizeof sub, "content/models/%s", key);
    if (!portJoin(dir, cap, modsDirOf(m), sub)) return 0;
    snprintf(sub, sizeof sub, "%s/model.json", dir);
    return portIsFile(sub);
}

int modelsHas(const char *name) {
    char dir[PP_CAP];
    return sourceDir(name, dir, sizeof dir);
}

int modelsInfo(const char *name, ModelSrcInfo *out) {
    char dir[PP_CAP], err[320];
    if (!sourceDir(name, dir, sizeof dir)) return 0;
    if (modelSourceInfoFrom(dir, "model.json", &sOriginals, out, err, sizeof err) != 0) {
        plogLine("models", "%s rejected: %s\n", name, err);
        return 0;
    }
    return 1;
}

static int cachePath(uint64_t key, char *path, int cap) {
    char leaf[64];
    snprintf(leaf, sizeof leaf, "models/%08lx%08lx.bin", (unsigned long)(key >> 32), (unsigned long)(key & 0xFFFFFFFFu));
    return portPathIn(PP_CACHE, leaf, path, cap);
}

static unsigned long sum32(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* cache file: "DW3M", u32 baker version, u32 length, the blob, u32 checksum of the blob */
static int cacheGet(void *ud, uint64_t key, uint8_t **blob, size_t *len) {
    char path[PP_CAP];
    FILE *f;
    uint8_t head[12], tail[4];
    size_t n;
    uint8_t *p;
    (void)ud;
    if (!cachePath(key, path, sizeof path)) return 0;
    f = fopen(path, "rb");
    if (f == NULL) return 0;
    if (fread(head, 1, 12, f) != 12 || memcmp(head, "DW3M", 4) != 0 || le32p(head + 4) != MODEL_BAKE_VERSION) {
        fclose(f);
        return 0;
    }
    n = le32p(head + 8);
    if (n == 0 || n > FILE_MAX) {
        fclose(f);
        return 0;
    }
    p = (uint8_t *)malloc(n);
    if (p == NULL || fread(p, 1, n, f) != n || fread(tail, 1, 4, f) != 4 || le32p(tail) != sum32(p, n)) {
        free(p);
        fclose(f);
        return 0;
    }
    fclose(f);
    *blob = p;
    *len = n;
    return 1;
}

static void cachePut(void *ud, uint64_t key, const uint8_t *blob, size_t len) {
    char path[PP_CAP], tmp[PP_CAP + 8];
    FILE *f;
    uint8_t head[12], tail[4];
    unsigned long c = sum32(blob, len);
    int ok;
    (void)ud;
    if (!cachePath(key, path, sizeof path) || !portMkdirsFor(path)) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (f == NULL) return;
    memcpy(head, "DW3M", 4);
    head[4] = MODEL_BAKE_VERSION; head[5] = head[6] = head[7] = 0;
    head[8] = (uint8_t)len; head[9] = (uint8_t)(len >> 8); head[10] = (uint8_t)(len >> 16); head[11] = (uint8_t)(len >> 24);
    tail[0] = (uint8_t)c; tail[1] = (uint8_t)(c >> 8); tail[2] = (uint8_t)(c >> 16); tail[3] = (uint8_t)(c >> 24);
    ok = fwrite(head, 1, 12, f) == 12 && fwrite(blob, 1, len, f) == len && fwrite(tail, 1, 4, f) == 4;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        remove(tmp);
        return;
    }
    remove(path);
    if (rename(tmp, path) != 0) remove(tmp);
}

static void registryLog(void *ud, int warning, const char *msg) {
    const char *name = (const char *)ud;
    if (warning) plogLine("models", "%s: warning: %s\n", name, msg);
    else PLOG("models", PLOG_MODS, "%s: %s\n", name, msg);
}

int modelsFile(const char *name) {
    char dir[PP_CAP], err[320];
    ModelCacheHooks hooks;
    ModelBaked baked;
    clock_t t0;
    int i, file;
    for (i = 0; i < sBakedN; i++) if (strcmp(sBaked[i].name, name) == 0) return sBaked[i].file;
    if (sBakedN >= MODEL_NAMES || strlen(name) >= sizeof sBaked[0].name) {
        plogLine("models", "%s rejected: too many model sources (%d)\n", name, MODEL_NAMES);
        return -1;
    }
    snprintf(sBaked[sBakedN].name, sizeof sBaked[0].name, "%s", name);
    sBaked[sBakedN].file = -1;
    sBakedN++;
    if (!sourceDir(name, dir, sizeof dir)) {
        plogLine("models", "%s rejected: no model source of that name (mods/<mod>/content/models/<key>/model.json)\n", name);
        return -1;
    }
    hooks.get = cacheGet;
    hooks.put = cachePut;
    hooks.ud = NULL;
    t0 = clock();
    if (modelBakeFrom(dir, "model.json", registryLog, (void *)name, &hooks, &sOriginals, &baked, err, sizeof err) != 0) {
        plogLine("models", "%s rejected: %s\n", name, err);
        return -1;
    }
    file = pfsAddVirtualFile(baked.blob, baked.len);
    if (file < 0) {
        plogLine("models", "%s rejected: no room for another virtual file\n", name);
    } else {
        sBaked[sBakedN - 1].file = file;
        if (baked.cached)
            plogLine("models", "%s: %lu bytes from the cache in %.1f ms (virtual file %d)\n", name, (unsigned long)baked.len,
                     (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC, file);
        else
            plogLine("models", "%s: baked %d bones, %d polygons, %d motions in %d clips, %d palettes, %lu bytes in %.1f ms (virtual file %d)\n", name,
                     baked.bones, baked.polys, baked.slots, baked.clips, baked.palettes, (unsigned long)baked.len,
                     (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC, file);
    }
    modelBakedFree(&baked);
    return file;
}

void modelsReset(void) {
    sBakedN = 0;
}
