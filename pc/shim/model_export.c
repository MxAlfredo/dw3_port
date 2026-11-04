#include "model_export.h"

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "port_log.h"
#include "text_data.h"

/*
 * Port of tools/model_export.py (--source): read a fighter's model, motions and texture from the disc and write the glTF 2.0
 * (skinned, one animation per motion slot sampled at 60 fps), its binary, its atlas PNG and the model.json that model_bake.c reads.
 * Every float operation is done in the same order as the tool (plain left-to-right sums, no contraction) so the .gltf, .bin and
 * .json come out with the same bytes; see docs/formats.md section 4.6 for the format and docs/modding/README.md for the use.
 */

#define HIDDEN_SCALE 48
#define FPS 60.0
#define MAX_SAMPLES (1 << 18)
#define SLOTS_MAX 256
#define NO_LOOP (-100000)

static const double C_AXES[3] = {-1.0, -1.0, 1.0};

/* ---------------------------------------------------------------- arena, buffers, errors */

typedef struct Blk {
    struct Blk *next;
    size_t cap, used;
} Blk;
#define BLK_HDR 32

typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
} Buf;

typedef struct Ex {
    Blk *blk;
    jmp_buf jb;
    char err[256];
    Buf *bufs[64];
    int nbufs;
    void *scratch;
} Ex;

static void xfree(Ex *x) {
    int i;
    for (i = 0; i < x->nbufs; i++) {
        free(x->bufs[i]->p);
        x->bufs[i]->p = NULL;
    }
    x->nbufs = 0;
    while (x->blk != NULL) {
        Blk *n = x->blk->next;
        free(x->blk);
        x->blk = n;
    }
    free(x->scratch);
    x->scratch = NULL;
}

static void fail(Ex *x, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(x->err, sizeof x->err, fmt, ap);
    va_end(ap);
    longjmp(x->jb, 1);
}

static void *aalloc(Ex *x, size_t n) {
    Blk *b = x->blk;
    void *p;
    n = (n + 15) & ~(size_t)15;
    if (n == 0) n = 16;
    if (b == NULL || b->cap - b->used < n) {
        size_t cap = n > ((size_t)1 << 20) ? n : ((size_t)1 << 20);
        b = (Blk *)calloc(1, BLK_HDR + cap);
        if (b == NULL) fail(x, "out of memory");
        b->cap = cap;
        b->next = x->blk;
        x->blk = b;
    }
    p = (uint8_t *)b + BLK_HDR + b->used;
    b->used += n;
    return p;
}

#define VEC(T, name) typedef struct { T *p; int n, cap; } name
#define PUSH(x, v, item) \
    do { \
        if ((v).n == (v).cap) { \
            int c_ = (v).cap ? (v).cap * 2 : 16; \
            void *np_ = aalloc((x), (size_t)c_ * sizeof *(v).p); \
            if ((v).n) memcpy(np_, (v).p, (size_t)(v).n * sizeof *(v).p); \
            (v).p = np_; \
            (v).cap = c_; \
        } \
        (v).p[(v).n++] = (item); \
    } while (0)

/* a buffer owned by the caller's frame that lives until xfree (the caller's own function) */
static void binit(Ex *x, Buf *b) {
    if (x->nbufs >= 64) fail(x, "too many buffers");
    memset(b, 0, sizeof *b);
    x->bufs[x->nbufs++] = b;
}

/* a buffer that lives in the arena: any function may make one (its bytes go with xfree, or earlier with bdrop) */
static Buf *bnew(Ex *x) {
    Buf *b = (Buf *)aalloc(x, sizeof *b);
    binit(x, b);
    return b;
}

static void bdrop(Buf *b) {
    free(b->p);
    memset(b, 0, sizeof *b);
}

static void bput(Ex *x, Buf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        uint8_t *np;
        while (cap < b->n + n) cap *= 2;
        np = (uint8_t *)realloc(b->p, cap);
        if (np == NULL) fail(x, "out of memory");
        b->p = np;
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void bs(Ex *x, Buf *b, const char *s) { bput(x, b, s, strlen(s)); }

static void bpf(Ex *x, Buf *b, const char *fmt, ...) {
    char t[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof t) fail(x, "text too long");
    bput(x, b, t, (size_t)n);
}

/* a double as Python's repr prints it (the shortest text that reads back exactly) */
static void fmtDouble(char *out, double v) {
    char t[48], dg[24];
    int prec, nd = 0, ex, i, neg = v < 0;
    char *e;

    if (!(v == v) || v - v != 0.0) {
        strcpy(out, "0.0");
        return;
    }
    if (v == 0) {
        strcpy(out, signbit(v) ? "-0.0" : "0.0");
        return;
    }
    for (prec = 1; prec <= 17; prec++) {
        snprintf(t, sizeof t, "%.*e", prec - 1, v);
        if (strtod(t, NULL) == v) break;
    }
    e = strchr(t, 'e');
    ex = atoi(e + 1);
    for (i = neg; t + i < e; i++) if (t[i] != '.') dg[nd++] = t[i];
    dg[nd] = '\0';
    if (neg) *out++ = '-';
    if (ex >= -4 && ex < 16) {
        int decpt = ex + 1;
        if (decpt <= 0) {
            *out++ = '0';
            *out++ = '.';
            for (i = 0; i < -decpt; i++) *out++ = '0';
            memcpy(out, dg, (size_t)nd + 1);
        } else if (decpt >= nd) {
            memcpy(out, dg, (size_t)nd);
            out += nd;
            for (i = 0; i < decpt - nd; i++) *out++ = '0';
            strcpy(out, ".0");
        } else {
            memcpy(out, dg, (size_t)decpt);
            out += decpt;
            *out++ = '.';
            memcpy(out, dg + decpt, (size_t)(nd - decpt) + 1);
        }
    } else {
        *out++ = dg[0];
        if (nd > 1) {
            *out++ = '.';
            memcpy(out, dg + 1, (size_t)nd - 1);
            out += nd - 1;
        }
        sprintf(out, "e%c%02d", ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
    }
}

static void bnum(Ex *x, Buf *b, double v) {
    char t[64];
    fmtDouble(t, v);
    bs(x, b, t);
}

static void blist(Ex *x, Buf *b, const double *v, int n) {
    int i;
    bs(x, b, "[");
    for (i = 0; i < n; i++) {
        if (i) bs(x, b, ",");
        bnum(x, b, v[i]);
    }
    bs(x, b, "]");
}

static void putF32(Ex *x, Buf *b, double v) {
    float f = (float)v;
    uint8_t t[4];
    memcpy(t, &f, 4);
    bput(x, b, t, 4);
}

/* ---------------------------------------------------------------- reading the disc's bytes */

typedef struct Sp {
    const uint8_t *p;
    size_t n;
} Sp;

static unsigned rd8(Ex *x, Sp d, size_t o) {
    if (o >= d.n) fail(x, "the data ends at 0x%zx", o);
    return d.p[o];
}

static uint32_t rd32(Ex *x, Sp d, size_t o) {
    if (o > d.n || d.n - o < 4) fail(x, "the data ends at 0x%zx", o);
    return (uint32_t)d.p[o] | ((uint32_t)d.p[o + 1] << 8) | ((uint32_t)d.p[o + 2] << 16) | ((uint32_t)d.p[o + 3] << 24);
}

static int rd16(Ex *x, Sp d, size_t o) {
    if (o > d.n || d.n - o < 2) fail(x, "the data ends at 0x%zx", o);
    return (int16_t)(d.p[o] | (d.p[o + 1] << 8));
}

static unsigned rd16u(Ex *x, Sp d, size_t o) { return (unsigned)rd16(x, d, o) & 0xFFFF; }

/* ---------------------------------------------------------------- model parsing */

typedef struct Key {
    int x, y, z, f;
} Key;
typedef struct Keys {
    Key *p;
    int n;
} Keys;

typedef struct Poly {
    int k;
    int idx[4], nrm[4], hasN;
    int hasUV, u[4], v[4];
    int page, mode, cx, cy;
    int lit, abr;
    int color[3];
} Poly;
VEC(Poly, PolyV);

typedef struct Mesh {
    int nv, nn;
    int (*verts)[3];
    int (*norms)[3];
    PolyV polys;
} Mesh;

typedef struct Bone {
    int parent;
    Mesh mesh;
    Keys key[3]; /* position, rotation, scale */
} Bone;

typedef struct Model {
    int nb;
    Bone *b;
    int vw;        /* halfwords per row of the packed texture */
    uint16_t *vram; /* 512 rows */
} Model;

typedef struct Steps {
    int n;
    int (*s)[4];
} Steps;

typedef struct Motions {
    int n;
    Steps *slot;
} Motions;

static Sp copySp(Ex *x, const uint8_t *p, size_t n) {
    Sp s;
    uint8_t *c = (uint8_t *)aalloc(x, n);
    memcpy(c, p, n);
    s.p = c;
    s.n = n;
    return s;
}

/* the RLEN block at t (docs/formats.md section 3) unpacked into b */
static void rlenUnpack(Ex *x, Sp d, size_t t, Buf *b) {
    size_t src;
    if (t > d.n || d.n - t < 8 || memcmp(d.p + t, "RLEN", 4) != 0) fail(x, "not an RLEN block");
    src = t + 8;
    for (;;) {
        unsigned c = rd8(x, d, src);
        if (c == 0) break;
        if (c & 0x80) {
            uint8_t v = (uint8_t)rd8(x, d, src + 1);
            unsigned i;
            for (i = 0; i < (c & 0x7F); i++) bput(x, b, &v, 1);
            src += 2;
        } else {
            size_t take = c;
            if (src + 1 > d.n) fail(x, "the data ends at 0x%zx", src + 1);
            if (src + 1 + take > d.n) take = d.n - src - 1;
            bput(x, b, d.p + src + 1, take);
            src += 1 + c;
        }
    }
}

/* the packed texture: 64 halfwords per TIM, relative to texPos */
static void parseTims(Ex *x, Sp d, size_t off, Model *m) {
    int count = 0, i;
    struct { uint16_t w, h; const uint8_t *data; size_t len; } tims[16];
    Buf *raw[16];

    for (;;) {
        uint32_t e = rd32(x, d, off + 4 * (size_t)count);
        Sp b;
        size_t p, sz, end;
        unsigned flag;
        if (e == 0) break;
        if (count >= 16) fail(x, "more than 16 textures");
        {
            size_t t = off + e;
            if (t <= d.n && d.n - t >= 4 && memcmp(d.p + t, "RLEN", 4) == 0) {
                raw[count] = bnew(x);
                rlenUnpack(x, d, t, raw[count]);
                b.p = raw[count]->p;
                b.n = raw[count]->n;
            } else {
                if (t > d.n) fail(x, "the data ends at 0x%zx", t);
                b.p = d.p + t;
                b.n = d.n - t;
            }
        }
        if (rd32(x, b, 0) != 0x10) fail(x, "not a TIM");
        flag = rd32(x, b, 4);
        p = 8;
        if (flag & 8) p += rd32(x, b, p);
        sz = rd32(x, b, p);
        tims[count].w = (uint16_t)rd16u(x, b, p + 8);
        tims[count].h = (uint16_t)rd16u(x, b, p + 10);
        end = p + sz > b.n ? b.n : p + sz;
        if (p + 12 > end) { tims[count].data = b.p; tims[count].len = 0; }
        else { tims[count].data = b.p + p + 12; tims[count].len = end - (p + 12); }
        count++;
    }
    m->vw = 64 * (count > 0 ? count : 1);
    m->vram = (uint16_t *)aalloc(x, (size_t)m->vw * 512 * 2);
    for (i = 0; i < count; i++) {
        int wd = tims[i].w, h = tims[i].h, r;
        size_t n = (size_t)wd * (size_t)h, hw = tims[i].len / 2;
        if (n > hw) n = hw;
        if (wd == 0) fail(x, "a texture is 0 halfwords wide");
        if (64 * i + wd > m->vw) fail(x, "a texture is wider than the packed rows");
        for (r = 0; (size_t)r < n / (size_t)wd; r++) {
            int c;
            if (r >= 512) fail(x, "a texture is taller than 512 rows");
            for (c = 0; c < wd; c++) {
                const uint8_t *q = tims[i].data + 2 * ((size_t)r * (size_t)wd + (size_t)c);
                m->vram[(size_t)r * (size_t)m->vw + (size_t)(64 * i + c)] = (uint16_t)(q[0] | (q[1] << 8));
            }
        }
    }
}

static void parseMesh(Ex *x, Sp d, size_t off, Mesh *me) {
    size_t v0 = off + rd32(x, d, off), n0 = off + rd32(x, d, off + 4), c = off + rd32(x, d, off + 8);
    int n = rd16(x, d, v0), nn = rd16(x, d, n0), i;
    int quad = 0, tex = 0, lit = 0, abr = 0;
    int tb[6] = {0, 0, 0, 0, 0, 0};
    int color[4][3];

    if (n < 0 || nn < 0) fail(x, "a mesh with a negative count");
    me->nv = n;
    me->nn = nn;
    me->verts = (int(*)[3])aalloc(x, sizeof(int[3]) * (size_t)(n + 1));
    me->norms = (int(*)[3])aalloc(x, sizeof(int[3]) * (size_t)(nn + 1));
    for (i = 0; i < n; i++) {
        me->verts[i][0] = rd16(x, d, v0 + 6 + 6 * (size_t)i);
        me->verts[i][1] = rd16(x, d, v0 + 8 + 6 * (size_t)i);
        me->verts[i][2] = rd16(x, d, v0 + 10 + 6 * (size_t)i);
    }
    for (i = 0; i < nn; i++) {
        me->norms[i][0] = rd16(x, d, n0 + 6 + 6 * (size_t)i);
        me->norms[i][1] = rd16(x, d, n0 + 8 + 6 * (size_t)i);
        me->norms[i][2] = rd16(x, d, n0 + 10 + 6 * (size_t)i);
    }
    for (i = 0; i < 4; i++) color[i][0] = color[i][1] = color[i][2] = 128;
    for (;;) {
        unsigned op = rd8(x, d, c), hi = op >> 4, lo = op & 15;
        if (op == 0xFF) break;
        if (hi) {
            if (hi < 8 || hi > 14) fail(x, "mesh command %02x at %zx", op, c);
            if (hi == 8) quad = (int)lo;
            else if (hi == 9) tex = (int)lo;
            else if (hi == 12) lit = (int)lo;
            else if (hi == 14) abr = (int)lo;
            c++;
        } else if (lo == 1) {
            tb[0] = (int)rd8(x, d, c + 1);
            tb[1] = (int)rd8(x, d, c + 3);
            tb[2] = (int)rd8(x, d, c + 2);
            tb[3] = (int)rd8(x, d, c + 4);
            tb[4] = (int)rd8(x, d, c + 5);
            tb[5] = (int)rd8(x, d, c + 6);
            c += 7;
        } else if (lo >= 2 && lo <= 5) {
            color[lo - 2][0] = (int)rd8(x, d, c + 1);
            color[lo - 2][1] = (int)rd8(x, d, c + 2);
            color[lo - 2][2] = (int)rd8(x, d, c + 3);
            c += 4;
        } else if (lo == 0) {
            while (rd8(x, d, c) == 0) {
                Poly p;
                int k = quad + 3, j;
                memset(&p, 0, sizeof p);
                if (quad > 1) fail(x, "a polygon with %d corners", k);
                c++;
                p.k = k;
                for (j = 0; j < k; j++) p.idx[j] = (int)rd8(x, d, c + (size_t)j);
                c += (size_t)k;
                if (lit) {
                    for (j = 0; j < k; j++) p.nrm[j] = (int)rd8(x, d, c + (size_t)j);
                    p.hasN = 1;
                    c += (size_t)k;
                }
                if (tex) {
                    for (j = 0; j < k; j++) {
                        p.u[j] = ((int)rd8(x, d, c + 2 * (size_t)j) + tb[0]) & 255;
                        p.v[j] = ((int)rd8(x, d, c + 2 * (size_t)j + 1) + tb[1]) & 255;
                    }
                    c += 2 * (size_t)k;
                    p.hasUV = 1;
                    p.page = tb[2];
                    p.mode = tb[5];
                    p.cx = tb[3];
                    p.cy = tb[4];
                }
                for (j = 0; j < k; j++) {
                    if (p.idx[j] >= n || (p.hasN && p.nrm[j] >= nn)) fail(x, "index out of range at %zx", c);
                }
                p.lit = lit;
                p.abr = abr;
                p.color[0] = color[0][0];
                p.color[1] = color[0][1];
                p.color[2] = color[0][2];
                PUSH(x, me->polys, p);
            }
        } else {
            fail(x, "mesh command %02x at %zx", op, c);
        }
    }
}

static void parseKeys(Ex *x, Sp d, size_t ko, Keys out[3]) {
    uint32_t a[4];
    int k;
    for (k = 0; k < 4; k++) a[k] = rd32(x, d, ko + 4 * (size_t)k);
    for (k = 0; k < 3; k++) {
        size_t p;
        int n = 0;
        if (a[k + 1] < a[k]) { out[k].n = 0; out[k].p = NULL; continue; }
        n = (int)((a[k + 1] - a[k] + 7) / 8);
        if (n > 100000) fail(x, "a key list with %d keys", n);
        out[k].p = (Key *)aalloc(x, sizeof(Key) * (size_t)(n + 1));
        out[k].n = n;
        for (p = 0; p < (size_t)n; p++) {
            size_t o = ko + a[k] + 8 * p;
            out[k].p[p].x = rd16(x, d, o);
            out[k].p[p].y = rd16(x, d, o + 2);
            out[k].p[p].z = rd16(x, d, o + 4);
            out[k].p[p].f = rd16(x, d, o + 6);
        }
    }
}

static void parseModel(Ex *x, Sp d, size_t o, Model *m) {
    uint32_t texf = rd32(x, d, o);
    int count = (int)rd32(x, d, o + 4) + 1, i;

    if (count < 1 || count > 4096) fail(x, "a model with %d bones", count);
    m->nb = count;
    m->b = (Bone *)aalloc(x, sizeof(Bone) * (size_t)count);
    for (i = 1; i < count; i++) {
        size_t w = o + 8 + 12 * (size_t)(i - 1);
        uint32_t par = rd32(x, d, w), mf = rd32(x, d, w + 4), kf = rd32(x, d, w + 8);
        m->b[i].parent = (int)par;
        if ((int)par < 0 || (int)par >= i) fail(x, "bone %d has the parent %d", i, (int)par);
        parseKeys(x, d, rd32(x, d, 4 * (size_t)kf), m->b[i].key);
        parseMesh(x, d, rd32(x, d, 4 * (size_t)mf), &m->b[i].mesh);
    }
    if (texf) {
        parseTims(x, d, rd32(x, d, 4 * (size_t)texf), m);
    } else { /* no texture: one empty 64 halfword page */
        m->vw = 64;
        m->vram = (uint16_t *)aalloc(x, (size_t)m->vw * 512 * 2);
    }
}

static void parseMotions(Ex *x, Sp d, size_t o, Motions *mo) {
    int n = (int)(rd32(x, d, o) / 4), m;
    if (n < 0 || n > SLOTS_MAX) fail(x, "%d motion slots", n);
    mo->n = n;
    mo->slot = (Steps *)aalloc(x, sizeof(Steps) * (size_t)(n + 1));
    for (m = 0; m < n; m++) {
        uint32_t eo = rd32(x, d, o + 4 * (size_t)m);
        size_t p;
        int cnt = 0;
        if (eo == 0) continue;
        p = o + eo;
        for (;;) {
            if (p > d.n || d.n - p < 8) fail(x, "the data ends at 0x%zx", p);
            cnt++;
            if (rd16(x, d, p) == 0x7FFF) break;
            p += 8;
        }
        mo->slot[m].n = cnt;
        mo->slot[m].s = (int(*)[4])aalloc(x, sizeof(int[4]) * (size_t)cnt);
        p = o + eo;
        for (cnt = 0; cnt < mo->slot[m].n; cnt++, p += 8) {
            int j;
            for (j = 0; j < 4; j++) mo->slot[m].s[cnt][j] = rd16(x, d, p + 2 * (size_t)j);
        }
    }
}

/* ---------------------------------------------------------------- pose sampling (FIGHTSTG_poseBone / blendBone) */

typedef struct BP {
    long long v[9]; /* pos, rot, scale */
} BP;

static long long floorDiv(long long a, long long b) {
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

static int bisectLeft(const Keys *k, int frame) {
    int lo = 0, hi = k->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (k->p[mid].f < frame) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void sampleKeys(Ex *x, const Keys *k, long long frame, long long *out) {
    int i;
    if (k->n == 0) fail(x, "a bone without keys");
    i = bisectLeft(k, (int)frame);
    if (i >= k->n) {
        out[0] = k->p[k->n - 1].x;
        out[1] = k->p[k->n - 1].y;
        out[2] = k->p[k->n - 1].z;
        return;
    }
    if (k->p[i].f == frame || i == 0) {
        out[0] = k->p[i].x;
        out[1] = k->p[i].y;
        out[2] = k->p[i].z;
    } else {
        const Key *pr = &k->p[i - 1], *cu = &k->p[i];
        long long t, pv[3], cv[3];
        int j;
        if (cu->f == pr->f) fail(x, "two keys on the same frame");
        t = floorDiv((frame - pr->f) * 4096, cu->f - pr->f);
        pv[0] = pr->x; pv[1] = pr->y; pv[2] = pr->z;
        cv[0] = cu->x; cv[1] = cu->y; cv[2] = cu->z;
        for (j = 0; j < 3; j++) out[j] = pv[j] + floorDiv((cv[j] - pv[j]) * t, 4096);
    }
}

static void firstKeyAt(const Keys *k, long long frame, long long *out) {
    int i = bisectLeft(k, (int)frame);
    if (i > k->n - 1) i = k->n - 1;
    out[0] = k->p[i].x;
    out[1] = k->p[i].y;
    out[2] = k->p[i].z;
}

static void mix3(const long long *pv, const long long *cv, long long w, long long *out) {
    int j;
    for (j = 0; j < 3; j++) out[j] = pv[j] + floorDiv((cv[j] - pv[j]) * w, 4096);
}

static void poseAt(Ex *x, const Model *m, long long frame, BP *out) {
    int i;
    memset(&out[0], 0, sizeof out[0]);
    out[0].v[6] = out[0].v[7] = out[0].v[8] = 4096;
    for (i = 1; i < m->nb; i++) {
        sampleKeys(x, &m->b[i].key[0], frame, out[i].v);
        sampleKeys(x, &m->b[i].key[1], frame, out[i].v + 3);
        sampleKeys(x, &m->b[i].key[2], frame, out[i].v + 6);
    }
}

static void blendPose(const Model *m, const BP *prev, long long target, long long w, BP *out) {
    int i;
    out[0] = prev[0];
    for (i = 1; i < m->nb; i++) {
        long long cp[3], cr[3], cs[3];
        const long long *ps = prev[i].v + 6;
        firstKeyAt(&m->b[i].key[0], target, cp);
        firstKeyAt(&m->b[i].key[1], target, cr);
        firstKeyAt(&m->b[i].key[2], target, cs);
        mix3(prev[i].v, cp, w, out[i].v);
        mix3(prev[i].v + 3, cr, w, out[i].v + 3);
        if (ps[0] == 0 || ps[1] == 0 || ps[2] == 0 || cs[0] == 0 || cs[1] == 0 || cs[2] == 0) {
            out[i].v[6] = cs[0]; out[i].v[7] = cs[1]; out[i].v[8] = cs[2];
        } else {
            mix3(ps, cs, w, out[i].v + 6);
        }
    }
}

typedef struct Sample {
    int blend, hasSrc;
    long long frame, src, to, w;
} Sample;
VEC(Sample, SampleV);

/* FIGHTSTG_setMotion: the per-vsync samples of a slot */
static void expandMotion(Ex *x, const Steps *st, int idleFrame, SampleV *out, int *loopStart, int *ends) {
    int i = 0;
    *loopStart = NO_LOOP;
    *ends = 0;
    out->n = 0;
    while (st->s[i][0] != 0x7FFF) {
        int index = st->s[i][0], count = st->s[i][1], frame = st->s[i][2], end = st->s[i][3];
        if (count == 0) {
            if (frame == -32768) {
                *loopStart = end;
            } else if (frame == -1) {
                *ends = 1;
            } else {
                Sample s;
                memset(&s, 0, sizeof s);
                s.frame = frame;
                PUSH(x, *out, s);
            }
            break;
        }
        if (end == 0) {
            long long to = i + 1 < st->n ? st->s[i + 1][2] : 0;
            int hasSrc = index != 0, j;
            long long src = hasSrc ? st->s[(i - 1 + st->n) % st->n][3] : 0;
            if (to == -1) {
                to = idleFrame;
                *ends = 1;
            }
            for (j = 0; j < count; j++) {
                Sample s;
                if (out->n >= MAX_SAMPLES) fail(x, "a motion with more than %d samples", MAX_SAMPLES);
                s.blend = 1;
                s.hasSrc = hasSrc;
                s.frame = 0;
                s.src = src;
                s.to = to;
                s.w = floorDiv((long long)(j + 1) * 4096, count + 1);
                PUSH(x, *out, s);
            }
        } else {
            int j;
            for (j = 0; j < count; j++) {
                Sample s;
                if (out->n >= MAX_SAMPLES) fail(x, "a motion with more than %d samples", MAX_SAMPLES);
                memset(&s, 0, sizeof s);
                s.frame = frame + j;
                PUSH(x, *out, s);
            }
        }
        i++;
        if (i >= st->n) break;
    }
}

/* ---------------------------------------------------------------- matrices */

typedef struct Mx {
    double r[3][3];
    double t[3];
} Mx;

static void matmul3(double a[3][3], double b[3][3], double o[3][3]) {
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) o[i][j] = 0.0 + a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
}

static void matvec3(double a[3][3], const double *v, double *o) {
    int i;
    for (i = 0; i < 3; i++) o[i] = 0.0 + a[i][0] * v[0] + a[i][1] * v[1] + a[i][2] * v[2];
}

static double det3(double a[3][3]) {
    return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
            a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]));
}

/* 0 when singular */
static int inv3(double a[3][3], double c[3][3]) {
    double d = det3(a);
    int i, j;
    if (fabs(d) < 1e-12) return 0;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            int r[2], q[2], n = 0, m = 0, t;
            double mm;
            for (t = 0; t < 3; t++) {
                if (t != i) r[n++] = t;
                if (t != j) q[m++] = t;
            }
            mm = a[r[0]][q[0]] * a[r[1]][q[1]] - a[r[0]][q[1]] * a[r[1]][q[0]];
            c[j][i] = (double)(((i + j) & 1) ? -1 : 1) * mm / d;
        }
    }
    return 1;
}

static void rotzyx(const long long *r, double m[3][3]) {
    double a[3], s[3], c[3];
    int i;
    for (i = 0; i < 3; i++) {
        a[i] = 2 * 3.14159265358979323846 * (double)r[i] / 4096.0;
        s[i] = sin(a[i]);
        c[i] = cos(a[i]);
    }
    {
        double sx = s[0], sy = s[1], sz = s[2], cx = c[0], cy = c[1], cz = c[2];
        m[0][0] = cz * cy;
        m[0][1] = cz * sy * sx - cx * sz;
        m[0][2] = cz * sy * cx + sx * sz;
        m[1][0] = sz * cy;
        m[1][1] = sz * sy * sx + cx * cz;
        m[1][2] = sz * sy * cx - sx * cz;
        m[2][0] = -sy;
        m[2][1] = cy * sx;
        m[2][2] = cy * cx;
    }
}

static void conjM(double m[3][3]) {
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) m[i][j] = C_AXES[i] * C_AXES[j] * m[i][j];
}

static void quatFromMatrix(double m[3][3], double q[4]) {
    double t = m[0][0] + m[1][1] + m[2][2], s, n;
    int i;
    if (t > 0) {
        s = sqrt(t + 1.0) * 2;
        q[0] = (m[2][1] - m[1][2]) / s; q[1] = (m[0][2] - m[2][0]) / s; q[2] = (m[1][0] - m[0][1]) / s; q[3] = 0.25 * s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
        q[0] = 0.25 * s; q[1] = (m[0][1] + m[1][0]) / s; q[2] = (m[0][2] + m[2][0]) / s; q[3] = (m[2][1] - m[1][2]) / s;
    } else if (m[1][1] > m[2][2]) {
        s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
        q[0] = (m[0][1] + m[1][0]) / s; q[1] = 0.25 * s; q[2] = (m[1][2] + m[2][1]) / s; q[3] = (m[0][2] - m[2][0]) / s;
    } else {
        s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
        q[0] = (m[0][2] + m[2][0]) / s; q[1] = (m[1][2] + m[2][1]) / s; q[2] = 0.25 * s; q[3] = (m[1][0] - m[0][1]) / s;
    }
    n = sqrt(0.0 + q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (i = 0; i < 4; i++) q[i] = q[i] / n;
}

static int isHidden(const long long *s) { return (s[0] | s[1] | s[2]) <= HIDDEN_SCALE; }

static void glScale(const long long *s, double *o) {
    int i;
    for (i = 0; i < 3; i++) o[i] = isHidden(s) ? 0.0 : (double)s[i] / 4096.0;
}

static void bindScale(const long long *s, double *o) {
    int i;
    for (i = 0; i < 3; i++) o[i] = isHidden(s) ? 1.0 : (llabs(s[i]) < 16 ? 1.0 : (double)s[i] / 4096.0);
}

static void worldMatrices(const Model *m, const BP *pose, int bind, Mx *out) {
    int i;
    for (i = 0; i < m->nb; i++) {
        double s[3], r[3][3], local[3][3], pos[3], mv[3];
        const Mx *par;
        int a, c;
        if (i == 0) {
            memset(&out[0], 0, sizeof out[0]);
            out[0].r[0][0] = out[0].r[1][1] = out[0].r[2][2] = 1.0;
            continue;
        }
        if (bind) bindScale(pose[i].v + 6, s);
        else glScale(pose[i].v + 6, s);
        rotzyx(pose[i].v + 3, r);
        for (a = 0; a < 3; a++)
            for (c = 0; c < 3; c++) local[a][c] = r[a][c] * s[c];
        par = &out[m->b[i].parent];
        {
            double pr[3][3];
            memcpy(pr, par->r, sizeof pr);
            matmul3(pr, local, out[i].r);
            for (a = 0; a < 3; a++) pos[a] = (double)pose[i].v[a];
            matvec3(pr, pos, mv);
        }
        for (a = 0; a < 3; a++) out[i].t[a] = mv[a] + par->t[a];
    }
}

/* ---------------------------------------------------------------- textures */

static int c15r(int v) { return (v & 31) * 255 / 31; }
static int c15g(int v) { return ((v >> 5) & 31) * 255 / 31; }
static int c15b(int v) { return ((v >> 10) & 31) * 255 / 31; }

static unsigned vget(const Model *m, int row, int x) {
    if (row < 0 || row >= 512 || x < 0 || x >= m->vw) return 0;
    return m->vram[(size_t)row * (size_t)m->vw + (size_t)x];
}

/* the 15 bit colour values of the texel box a textured polygon covers: does any carry the STP bit */
static int polyHasStp(const Model *m, const Poly *p) {
    int umin = 255, umax = 0, vmin = 255, vmax = 0, i, u, v;
    for (i = 0; i < p->k; i++) {
        if (p->u[i] < umin) umin = p->u[i];
        if (p->u[i] > umax) umax = p->u[i];
        if (p->v[i] < vmin) vmin = p->v[i];
        if (p->v[i] > vmax) vmax = p->v[i];
    }
    for (v = vmin; v <= vmax; v++) {
        for (u = umin; u <= umax; u++) {
            unsigned c;
            if (p->mode == 0) {
                unsigned idx = (vget(m, v, p->page * 64 + (u >> 2)) >> ((u & 3) * 4)) & 15;
                c = (p->cy >= 0 && p->cy < 512) ? vget(m, p->cy, p->cx + (int)idx) : 0;
            } else if (p->mode == 1) {
                unsigned idx = (vget(m, v, p->page * 64 + (u >> 1)) >> ((u & 1) * 8)) & 255;
                c = (p->cy >= 0 && p->cy < 512) ? vget(m, p->cy, p->cx + (int)idx) : 0;
            } else {
                c = vget(m, v, p->page * 64 + u);
            }
            if (c & 0x8000) return 1;
        }
    }
    return 0;
}

/* 256x256 RGBA of one texture page: alphaCode 0 none, 1 half, 2 quarter (only STP texels take it) */
static void variantRgba(const Model *m, int page, int mode, int cx, int cy, int alphaCode, uint8_t *out) {
    uint8_t pal[256][4];
    int npal = mode == 0 ? 16 : mode == 1 ? 256 : 0, i, v, u;
    int semi = alphaCode == 1 ? 128 : alphaCode == 2 ? 64 : 255;

#define PX(dst, val) \
    do { \
        unsigned v_ = (val); \
        if (v_ == 0) { (dst)[0] = (dst)[1] = (dst)[2] = (dst)[3] = 0; } \
        else { \
            (dst)[0] = (uint8_t)c15r((int)v_); \
            (dst)[1] = (uint8_t)c15g((int)v_); \
            (dst)[2] = (uint8_t)c15b((int)v_); \
            (dst)[3] = (uint8_t)((alphaCode != 0 && (v_ & 0x8000)) ? semi : 255); \
        } \
    } while (0)
    for (i = 0; i < npal; i++) {
        unsigned c = (cy >= 0 && cy < 512 && cx + i < m->vw) ? vget(m, cy, cx + i) : 0;
        PX(pal[i], c);
    }
    for (v = 0; v < 256; v++) {
        uint8_t *row = out + (size_t)v * 1024;
        for (u = 0; u < 256; u++) {
            if (mode == 0) {
                unsigned hw = vget(m, v, page * 64 + (u >> 2));
                memcpy(row + 4 * u, pal[(hw >> ((u & 3) * 4)) & 15], 4);
            } else if (mode == 1) {
                unsigned hw = vget(m, v, page * 64 + (u >> 1));
                memcpy(row + 4 * u, pal[(hw >> ((u & 1) * 8)) & 255], 4);
            } else {
                PX(row + 4 * u, vget(m, v, page * 64 + u));
            }
        }
    }
#undef PX
}

/* ---------------------------------------------------------------- PNG (a small deflate: fixed Huffman codes, hash chains) */

static uint32_t crcTab[256];
static int crcReady;

static uint32_t crcUpdate(uint32_t crc, const uint8_t *p, size_t n) {
    size_t i;
    if (!crcReady) {
        uint32_t c, k;
        int nn;
        for (nn = 0; nn < 256; nn++) {
            c = (uint32_t)nn;
            for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTab[nn] = c;
        }
        crcReady = 1;
    }
    for (i = 0; i < n; i++) crc = crcTab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

typedef struct Bits {
    Buf *b;
    uint32_t acc;
    int n;
} Bits;

static void putBits(Ex *x, Bits *w, uint32_t v, int n) {
    w->acc |= v << w->n;
    w->n += n;
    while (w->n >= 8) {
        uint8_t c = (uint8_t)(w->acc & 0xFF);
        bput(x, w->b, &c, 1);
        w->acc >>= 8;
        w->n -= 8;
    }
}

static uint32_t revBits(uint32_t v, int n) {
    uint32_t r = 0;
    int i;
    for (i = 0; i < n; i++) {
        r = (r << 1) | (v & 1);
        v >>= 1;
    }
    return r;
}

static void putSym(Ex *x, Bits *w, int sym) {
    if (sym < 144) putBits(x, w, revBits((uint32_t)(0x30 + sym), 8), 8);
    else if (sym < 256) putBits(x, w, revBits((uint32_t)(0x190 + sym - 144), 9), 9);
    else if (sym < 280) putBits(x, w, revBits((uint32_t)(sym - 256), 7), 7);
    else putBits(x, w, revBits((uint32_t)(0xC0 + sym - 280), 8), 8);
}

static const int LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const int LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const int DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const int DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* a zlib stream of src: one fixed-Huffman block with LZ77 matches */
static void zlibCompress(Ex *x, Buf *out, const uint8_t *src, size_t n) {
    enum { HB = 15, HS = 1 << HB, WIN = 32768, CHAIN = 32 };
    int32_t *head = (int32_t *)malloc(sizeof(int32_t) * HS), *prev = (int32_t *)malloc(sizeof(int32_t) * WIN);
    Bits w;
    size_t i = 0;
    uint32_t a = 1, b = 0;
    uint8_t hdr[2] = {0x78, 0x01};

    if (head == NULL || prev == NULL) {
        free(head);
        free(prev);
        fail(x, "out of memory");
    }
    memset(head, 0xFF, sizeof(int32_t) * HS);
    memset(prev, 0xFF, sizeof(int32_t) * WIN);
    w.b = out;
    w.acc = 0;
    w.n = 0;
    bput(x, out, hdr, 2);
    putBits(x, &w, 1, 1);
    putBits(x, &w, 1, 2);
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        uint32_t h = 0;
        if (i + 3 <= n) {
            int32_t cand;
            int chain = CHAIN;
            h = ((uint32_t)src[i] * 506832829u ^ (uint32_t)src[i + 1] * 2654435761u ^ (uint32_t)src[i + 2] * 40503u) >> (32 - HB);
            h &= HS - 1;
            cand = head[h];
            while (cand >= 0 && chain-- > 0 && i - (size_t)cand <= WIN) {
                size_t maxl = n - i < 258 ? n - i : 258, l = 0;
                while (l < maxl && src[(size_t)cand + l] == src[i + l]) l++;
                if ((int)l > bestLen) {
                    bestLen = (int)l;
                    bestDist = (int)(i - (size_t)cand);
                    if (l == maxl) break;
                }
                cand = prev[(size_t)cand & (WIN - 1)];
            }
        }
        if (bestLen >= 3) {
            int ls = 28, ds = 29, k;
            while (LBASE[ls] > bestLen) ls--;
            putSym(x, &w, 257 + ls);
            if (LEXT[ls]) putBits(x, &w, (uint32_t)(bestLen - LBASE[ls]), LEXT[ls]);
            while (DBASE[ds] > bestDist) ds--;
            putBits(x, &w, revBits((uint32_t)ds, 5), 5);
            if (DEXT[ds]) putBits(x, &w, (uint32_t)(bestDist - DBASE[ds]), DEXT[ds]);
            for (k = 0; k < bestLen; k++) {
                size_t pos = i + (size_t)k;
                if (pos + 3 <= n) {
                    uint32_t hh = (((uint32_t)src[pos] * 506832829u ^ (uint32_t)src[pos + 1] * 2654435761u ^ (uint32_t)src[pos + 2] * 40503u) >> (32 - HB)) & (HS - 1);
                    prev[pos & (WIN - 1)] = head[hh];
                    head[hh] = (int32_t)pos;
                }
            }
            i += (size_t)bestLen;
        } else {
            if (i + 3 <= n) {
                prev[i & (WIN - 1)] = head[h];
                head[h] = (int32_t)i;
            }
            putSym(x, &w, src[i]);
            i++;
        }
    }
    putSym(x, &w, 256);
    if (w.n > 0) putBits(x, &w, 0, 8 - w.n);
    for (i = 0; i < n; i++) {
        a = (a + src[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    {
        uint8_t t[4] = {(uint8_t)(b >> 8), (uint8_t)b, (uint8_t)(a >> 8), (uint8_t)a};
        bput(x, out, t, 4);
    }
    free(head);
    free(prev);
}

static void pngChunk(Ex *x, Buf *o, const char *tag, const uint8_t *d, size_t n) {
    uint8_t t[4];
    uint32_t crc;
    t[0] = (uint8_t)(n >> 24); t[1] = (uint8_t)(n >> 16); t[2] = (uint8_t)(n >> 8); t[3] = (uint8_t)n;
    bput(x, o, t, 4);
    bput(x, o, tag, 4);
    if (n) bput(x, o, d, n);
    crc = crcUpdate(0xFFFFFFFFu, (const uint8_t *)tag, 4);
    if (n) crc = crcUpdate(crc, d, n);
    crc ^= 0xFFFFFFFFu;
    t[0] = (uint8_t)(crc >> 24); t[1] = (uint8_t)(crc >> 16); t[2] = (uint8_t)(crc >> 8); t[3] = (uint8_t)crc;
    bput(x, o, t, 4);
}

static void writePng(Ex *x, Buf *o, int w, int h, const uint8_t *rgba) {
    Buf *raw = bnew(x), *z = bnew(x);
    uint8_t ih[13];
    int y;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    for (y = 0; y < h; y++) {
        uint8_t f = 0;
        bput(x, raw, &f, 1);
        bput(x, raw, rgba + (size_t)y * (size_t)w * 4, (size_t)w * 4);
    }
    zlibCompress(x, z, raw->p, raw->n);
    ih[0] = (uint8_t)(w >> 24); ih[1] = (uint8_t)(w >> 16); ih[2] = (uint8_t)(w >> 8); ih[3] = (uint8_t)w;
    ih[4] = (uint8_t)(h >> 24); ih[5] = (uint8_t)(h >> 16); ih[6] = (uint8_t)(h >> 8); ih[7] = (uint8_t)h;
    ih[8] = 8; ih[9] = 6; ih[10] = ih[11] = ih[12] = 0;
    bput(x, o, sig, 8);
    pngChunk(x, o, "IHDR", ih, 13);
    pngChunk(x, o, "IDAT", z->p, z->n);
    pngChunk(x, o, "IEND", NULL, 0);
    bdrop(raw);
    bdrop(z);
}

/* ---------------------------------------------------------------- the glTF */

enum { T_SCALAR, T_VEC2, T_VEC3, T_VEC4, T_MAT4 };
static const char *const TYPE_NAME[] = {"SCALAR", "VEC2", "VEC3", "VEC4", "MAT4"};
#define F32 5126
#define U8 5121
#define U16 5123
#define U32 5125

typedef struct View {
    size_t off, len;
    int target;
} View;
VEC(View, ViewV);

typedef struct Acc {
    int view, ctype, count, atype;
    size_t off;
    int nmm;
    double mn[3], mx[3];
} Acc;
VEC(Acc, AccV);

typedef struct Gb {
    Buf bin;
    ViewV views;
    AccV accs;
} Gb;

static int gbView(Ex *x, Gb *g, const void *data, size_t len, int target) {
    View v;
    while (g->bin.n % 4) {
        uint8_t z = 0;
        bput(x, &g->bin, &z, 1);
    }
    v.off = g->bin.n;
    v.len = len;
    v.target = target;
    bput(x, &g->bin, data, len);
    PUSH(x, g->views, v);
    return g->views.n - 1;
}

static int gbAcc(Ex *x, Gb *g, int view, int ctype, int count, int atype, size_t off, int nmm, const double *mn, const double *mx) {
    Acc a;
    int i;
    memset(&a, 0, sizeof a);
    a.view = view;
    a.ctype = ctype;
    a.count = count;
    a.atype = atype;
    a.off = off;
    a.nmm = nmm;
    for (i = 0; i < nmm; i++) {
        a.mn[i] = mn[i];
        a.mx[i] = mx[i];
    }
    PUSH(x, g->accs, a);
    return g->accs.n - 1;
}

static int gbAdd(Ex *x, Gb *g, const void *data, size_t len, int ctype, int count, int atype, int target, int nmm, const double *mn,
                 const double *mx) {
    return gbAcc(x, g, gbView(x, g, data, len, target), ctype, count, atype, 0, nmm, mn, mx);
}

typedef struct VKey {
    int bi, vidx, nk, n[3];
    int hasUV, tvar, u, v;
    int col[3], alpha;
} VKey;

typedef struct Vtx {
    VKey k;
    double pos[3], nrm[3];
} Vtx;
VEC(Vtx, VtxV);
VEC(int, IntV);

typedef struct Pr {
    VtxV vtx;
    IntV idx;
    int *tab;
    int tcap;
} Pr;

static uint32_t vkHash(const VKey *k) {
    const int *p = (const int *)k;
    size_t i, n = sizeof(VKey) / sizeof(int);
    uint32_t h = 2166136261u;
    for (i = 0; i < n; i++) h = (h ^ (uint32_t)p[i]) * 16777619u;
    return h;
}

static int prVertex(Ex *x, Pr *pr, const VKey *k, int *isNew) {
    uint32_t h = vkHash(k);
    int i;
    if (pr->tcap == 0 || pr->vtx.n * 2 >= pr->tcap) {
        int cap = pr->tcap ? pr->tcap * 2 : 1024;
        int *nt = (int *)aalloc(x, sizeof(int) * (size_t)cap);
        for (i = 0; i < pr->vtx.n; i++) {
            uint32_t hh = vkHash(&pr->vtx.p[i].k) & (uint32_t)(cap - 1);
            while (nt[hh]) hh = (hh + 1) & (uint32_t)(cap - 1);
            nt[hh] = i + 1;
        }
        pr->tab = nt;
        pr->tcap = cap;
    }
    h &= (uint32_t)(pr->tcap - 1);
    while (pr->tab[h]) {
        if (memcmp(&pr->vtx.p[pr->tab[h] - 1].k, k, sizeof *k) == 0) {
            *isNew = 0;
            return pr->tab[h] - 1;
        }
        h = (h + 1) & (uint32_t)(pr->tcap - 1);
    }
    *isNew = 1;
    return -1; /* the caller adds it with prAdd */
}

static int prAdd(Ex *x, Pr *pr, const Vtx *v) {
    uint32_t h = vkHash(&v->k) & (uint32_t)(pr->tcap - 1);
    PUSH(x, pr->vtx, *v);
    while (pr->tab[h]) h = (h + 1) & (uint32_t)(pr->tcap - 1);
    pr->tab[h] = pr->vtx.n;
    return pr->vtx.n - 1;
}

typedef struct Variant {
    int page, mode, cx, cy, alpha;
} Variant;
VEC(Variant, VarV);

typedef struct PrimOut {
    int pos, nrm, third, joints, weights, indices, material, textured, blend;
} PrimOut;

typedef struct AnimOut {
    int slot, frames, ends, loopStart;
    int nch;
    int *sIn, *sOut, *cNode, *cPath;
} AnimOut;
VEC(AnimOut, AnimV);

static const char *const PATH_NAME[] = {"translation", "rotation", "scale"};

static void convVec(const double *v, const double *t, double us, double *o) {
    int i;
    for (i = 0; i < 3; i++) o[i] = C_AXES[i] * (v[i] + t[i]) * us;
}

static void buildMeshPrimitives(Ex *x, const Model *m, const Mx *bindWorld, VarV *variants, double us, Pr prs[4]) {
    int bi;
    for (bi = 1; bi < m->nb; bi++) {
        const Mesh *me = &m->b[bi].mesh;
        double wr[3][3], ninv[3][3], inv[3][3], (*gpos)[3];
        int v, pi;
        memcpy(wr, bindWorld[bi].r, sizeof wr);
        if (inv3(wr, inv)) {
            int a, c;
            for (a = 0; a < 3; a++)
                for (c = 0; c < 3; c++) ninv[a][c] = inv[c][a];
        } else {
            memcpy(ninv, wr, sizeof ninv);
        }
        gpos = (double(*)[3])aalloc(x, sizeof(double[3]) * (size_t)(me->nv + 1));
        for (v = 0; v < me->nv; v++) {
            double vv[3], mv[3];
            vv[0] = me->verts[v][0]; vv[1] = me->verts[v][1]; vv[2] = me->verts[v][2];
            matvec3(wr, vv, mv);
            convVec(mv, bindWorld[bi].t, us, gpos[v]);
        }
        for (pi = 0; pi < me->polys.n; pi++) {
            const Poly *p = &me->polys.p[pi];
            int blend = p->abr != 0 && (!p->hasUV || polyHasStp(m, p));
            int alpha = blend ? ((p->abr == 4) ? 2 : 1) : 0;
            Pr *pr = &prs[(p->hasUV ? 2 : 0) + (blend ? 1 : 0)];
            long long fn[3] = {0, 0, 0};
            int tvar = 0, i, corner[4];
            if (!p->hasN) {
                long long a[3], c[3];
                for (i = 0; i < 3; i++) {
                    a[i] = me->verts[p->idx[1]][i] - me->verts[p->idx[0]][i];
                    c[i] = me->verts[p->idx[2]][i] - me->verts[p->idx[0]][i];
                }
                fn[0] = a[2] * c[1] - a[1] * c[2];
                fn[1] = a[0] * c[2] - a[2] * c[0];
                fn[2] = a[1] * c[0] - a[0] * c[1];
            }
            if (p->hasUV) {
                for (tvar = 0; tvar < variants->n; tvar++) {
                    const Variant *va = &variants->p[tvar];
                    if (va->page == p->page && va->mode == p->mode && va->cx == p->cx && va->cy == p->cy && va->alpha == alpha) break;
                }
                if (tvar == variants->n) {
                    Variant nv;
                    nv.page = p->page; nv.mode = p->mode; nv.cx = p->cx; nv.cy = p->cy; nv.alpha = alpha;
                    PUSH(x, *variants, nv);
                }
            }
            for (i = 0; i < p->k; i++) {
                VKey k;
                Vtx nvx;
                int vid, isNew;
                memset(&k, 0, sizeof k);
                k.bi = bi;
                k.vidx = p->idx[i];
                if (p->hasN) {
                    k.nk = 0;
                    k.n[0] = p->nrm[i];
                } else {
                    k.nk = 1;
                    k.n[0] = (int)fn[0]; k.n[1] = (int)fn[1]; k.n[2] = (int)fn[2];
                    if ((long long)k.n[0] != fn[0] || (long long)k.n[1] != fn[1] || (long long)k.n[2] != fn[2]) fail(x, "a face normal is too large");
                }
                if (p->hasUV) {
                    k.hasUV = 1;
                    k.tvar = tvar;
                    k.u = p->u[i];
                    k.v = p->v[i];
                }
                k.col[0] = p->color[0]; k.col[1] = p->color[1]; k.col[2] = p->color[2];
                k.alpha = alpha;
                vid = prVertex(x, pr, &k, &isNew);
                if (isNew) {
                    double nv[3], mv[3], nn[3], z3[3] = {0, 0, 0}, ln;
                    if (p->hasN) {
                        nv[0] = me->norms[p->nrm[i]][0]; nv[1] = me->norms[p->nrm[i]][1]; nv[2] = me->norms[p->nrm[i]][2];
                    } else {
                        nv[0] = (double)fn[0]; nv[1] = (double)fn[1]; nv[2] = (double)fn[2];
                    }
                    matvec3(ninv, nv, mv);
                    convVec(mv, z3, 1.0, nn);
                    ln = sqrt(0.0 + nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                    if (ln == 0.0) ln = 1.0;
                    memset(&nvx, 0, sizeof nvx);
                    nvx.k = k;
                    nvx.pos[0] = gpos[p->idx[i]][0]; nvx.pos[1] = gpos[p->idx[i]][1]; nvx.pos[2] = gpos[p->idx[i]][2];
                    nvx.nrm[0] = nn[0] / ln; nvx.nrm[1] = nn[1] / ln; nvx.nrm[2] = nn[2] / ln;
                    vid = prAdd(x, pr, &nvx);
                }
                corner[i] = vid;
            }
            if (p->k == 3) {
                PUSH(x, pr->idx, corner[0]); PUSH(x, pr->idx, corner[2]); PUSH(x, pr->idx, corner[1]);
            } else {
                PUSH(x, pr->idx, corner[0]); PUSH(x, pr->idx, corner[2]); PUSH(x, pr->idx, corner[1]);
                PUSH(x, pr->idx, corner[2]); PUSH(x, pr->idx, corner[3]); PUSH(x, pr->idx, corner[1]);
            }
        }
    }
}

typedef struct Sec {
    size_t off;
    int ctype, atype, count, nmm;
    double mn, mx;
} Sec;
VEC(Sec, SecV);

static int secPut(Ex *x, Buf *data, SecV *secs, const double *vals, int nvals, int atype, int count, int nmm, double mn, double mx) {
    Sec s;
    int i;
    while (data->n % 4) {
        uint8_t z = 0;
        bput(x, data, &z, 1);
    }
    s.off = data->n;
    for (i = 0; i < nvals; i++) putF32(x, data, vals[i]);
    s.ctype = F32;
    s.atype = atype;
    s.count = count;
    s.nmm = nmm;
    s.mn = mn;
    s.mx = mx;
    PUSH(x, *secs, s);
    return secs->n - 1;
}

static void buildAnimations(Ex *x, const Model *m, const Motions *mo, Gb *g, int idleFrame, double us, AnimV *anims, int *slotUsed) {
    int nb = m->nb, slot;
    BP *rest = (BP *)aalloc(x, sizeof(BP) * (size_t)nb), *cur = (BP *)aalloc(x, sizeof(BP) * (size_t)nb);
    BP *tmp = (BP *)aalloc(x, sizeof(BP) * (size_t)nb);
    SampleV samples;
    Buf *data = bnew(x);
    size_t serCap = 0;

    memset(&samples, 0, sizeof samples);
    poseAt(x, m, idleFrame, rest);
    for (slot = 0; slot < mo->n; slot++) {
        const Steps *st = &mo->slot[slot];
        int loopStart, ends, n, s, bi, tIn, zIn, nsec0;
        double *ser;
        SecV secs;
        struct { int bi, path, tin, out; } *chans;
        int nch = 0, view, k;
        AnimOut ao;

        if (st->n == 0) continue;
        expandMotion(x, st, idleFrame, &samples, &loopStart, &ends);
        n = samples.n;
        if (n == 0) continue;
        slotUsed[slot < SLOTS_MAX ? slot : SLOTS_MAX - 1] = 1;
        if ((size_t)nb * (size_t)n * 10 > serCap) {
            serCap = (size_t)nb * (size_t)n * 10;
            free(x->scratch);
            x->scratch = malloc(serCap * sizeof(double));
            if (x->scratch == NULL) fail(x, "out of memory");
        }
        ser = (double *)x->scratch;
        for (s = 0; s < n; s++) {
            const Sample *sm = &samples.p[s];
            if (!sm->blend) {
                poseAt(x, m, sm->frame, cur);
            } else {
                if (sm->hasSrc) poseAt(x, m, sm->src, tmp);
                blendPose(m, sm->hasSrc ? tmp : rest, sm->to, sm->w, cur);
            }
            for (bi = 1; bi < nb; bi++) {
                double *o = ser + ((size_t)bi * (size_t)n + (size_t)s) * 10, r[3][3], q[4], sc[3];
                int j;
                for (j = 0; j < 3; j++) o[j] = C_AXES[j] * (double)cur[bi].v[j] * us;
                rotzyx(cur[bi].v + 3, r);
                conjM(r);
                quatFromMatrix(r, q);
                if (s > 0) {
                    const double *pq = o - 10 + 3;
                    double dot = 0.0 + q[0] * pq[0] + q[1] * pq[1] + q[2] * pq[2] + q[3] * pq[3];
                    if (dot < 0) for (j = 0; j < 4; j++) q[j] = -q[j];
                }
                for (j = 0; j < 4; j++) o[3 + j] = q[j];
                glScale(cur[bi].v + 6, sc);
                for (j = 0; j < 3; j++) o[7 + j] = sc[j];
            }
        }
        memset(&secs, 0, sizeof secs);
        data->n = 0;
        {
            double times[1], tv;
            int i2;
            /* the times: i / 60, one accessor with min and max */
            while (data->n % 4) { uint8_t z = 0; bput(x, data, &z, 1); }
            {
                Sec sc;
                sc.off = data->n;
                for (i2 = 0; i2 < n; i2++) {
                    tv = (double)i2 / FPS;
                    putF32(x, data, tv);
                }
                sc.ctype = F32; sc.atype = T_SCALAR; sc.count = n; sc.nmm = 1;
                sc.mn = 0.0 / FPS;
                sc.mx = (double)(n - 1) / FPS;
                PUSH(x, secs, sc);
            }
            tIn = 0;
            times[0] = 0.0;
            zIn = secPut(x, data, &secs, times, 1, T_SCALAR, 1, 1, 0.0, 0.0);
        }
        chans = aalloc(x, sizeof(*chans) * (size_t)(3 * nb + 1));
        for (bi = 1; bi < nb; bi++) {
            int path;
            for (path = 0; path < 3; path++) {
                int width = path == 1 ? 4 : 3, off = path == 0 ? 0 : path == 1 ? 3 : 7, isConst = 1, out;
                const double *s0 = ser + ((size_t)bi * (size_t)n) * 10 + off;
                for (s = 0; s < n && isConst; s++) {
                    const double *r = ser + ((size_t)bi * (size_t)n + (size_t)s) * 10 + off;
                    double mx = 0.0;
                    int j;
                    for (j = 0; j < width; j++) {
                        double dd = fabs(s0[j] - r[j]);
                        if (j == 0 || dd > mx) mx = dd;
                    }
                    if (!(mx < 1e-6)) isConst = 0;
                }
                if (isConst) {
                    out = secPut(x, data, &secs, s0, width, path == 1 ? T_VEC4 : T_VEC3, 1, 0, 0, 0);
                    chans[nch].tin = zIn;
                } else {
                    Sec sc;
                    while (data->n % 4) { uint8_t z = 0; bput(x, data, &z, 1); }
                    sc.off = data->n;
                    for (s = 0; s < n; s++) {
                        const double *r = ser + ((size_t)bi * (size_t)n + (size_t)s) * 10 + off;
                        int j;
                        for (j = 0; j < width; j++) putF32(x, data, r[j]);
                    }
                    sc.ctype = F32; sc.atype = path == 1 ? T_VEC4 : T_VEC3; sc.count = n; sc.nmm = 0; sc.mn = sc.mx = 0;
                    PUSH(x, secs, sc);
                    out = secs.n - 1;
                    chans[nch].tin = tIn;
                }
                chans[nch].bi = bi;
                chans[nch].path = path;
                chans[nch].out = out;
                nch++;
            }
        }
        view = gbView(x, g, data->p, data->n, 0);
        nsec0 = g->accs.n;
        for (k = 0; k < secs.n; k++) {
            const Sec *sc = &secs.p[k];
            gbAcc(x, g, view, sc->ctype, sc->count, sc->atype, sc->off, sc->nmm, &sc->mn, &sc->mx);
        }
        memset(&ao, 0, sizeof ao);
        ao.slot = slot;
        ao.frames = n;
        ao.ends = ends;
        ao.loopStart = loopStart;
        ao.nch = nch;
        ao.sIn = (int *)aalloc(x, sizeof(int) * (size_t)(nch + 1));
        ao.sOut = (int *)aalloc(x, sizeof(int) * (size_t)(nch + 1));
        ao.cNode = (int *)aalloc(x, sizeof(int) * (size_t)(nch + 1));
        ao.cPath = (int *)aalloc(x, sizeof(int) * (size_t)(nch + 1));
        for (k = 0; k < nch; k++) {
            ao.sIn[k] = nsec0 + chans[k].tin;
            ao.sOut[k] = nsec0 + chans[k].out;
            ao.cNode[k] = chans[k].bi;
            ao.cPath[k] = chans[k].path;
        }
        PUSH(x, *anims, ao);
    }
}

static void emitAcc(Ex *x, Buf *j, const Acc *a, int first) {
    if (!first) bs(x, j, ",");
    bpf(x, j, "{\"bufferView\":%d,\"componentType\":%d,\"count\":%d,\"type\":\"%s\"", a->view, a->ctype, a->count, TYPE_NAME[a->atype]);
    if (a->off) bpf(x, j, ",\"byteOffset\":%llu", (unsigned long long)a->off);
    if (a->nmm) {
        bs(x, j, ",\"min\":");
        blist(x, j, a->mn, a->nmm);
        bs(x, j, ",\"max\":");
        blist(x, j, a->mx, a->nmm);
    }
    bs(x, j, "}");
}

/* glTF of one model: gltf JSON text into json, the buffer into g->bin, the PNG into png */
static void buildGltf(Ex *x, const Model *m, const Motions *mo, const char *name, const char *uriBase, double us, Buf *json, Gb *g,
                      Buf *png, int *slotUsed, int *nAnims, int *nTris) {
    int nb = m->nb, i, idleFrame = 0, ibmAcc, nv, cols, rowsN, aw, ah, ntex;
    BP *rest;
    Mx *bindWorld;
    double *ibm;
    uint8_t *atlas, *page;
    VarV variants;
    Pr prs[4];
    PrimOut *prims;
    int nprim = 0, key;
    AnimV anims;
    struct { double t[3], q[4], s[3]; } *nodes;

    if (mo->n > 0 && mo->slot[0].n > 0) idleFrame = mo->slot[0].s[0][2];
    rest = (BP *)aalloc(x, sizeof(BP) * (size_t)nb);
    poseAt(x, m, idleFrame, rest);
    bindWorld = (Mx *)aalloc(x, sizeof(Mx) * (size_t)nb);
    worldMatrices(m, rest, 1, bindWorld);
    nodes = aalloc(x, sizeof(*nodes) * (size_t)(nb + 1));
    for (i = 1; i < nb; i++) {
        double r[3][3], sc[3];
        int j;
        for (j = 0; j < 3; j++) nodes[i].t[j] = C_AXES[j] * (double)rest[i].v[j] * us;
        rotzyx(rest[i].v + 3, r);
        conjM(r);
        quatFromMatrix(r, nodes[i].q);
        glScale(rest[i].v + 6, sc);
        for (j = 0; j < 3; j++) nodes[i].s[j] = sc[j];
    }
    ibm = (double *)aalloc(x, sizeof(double) * 16 * (size_t)nb);
    for (i = 0; i < nb; i++) {
        double cr[3][3], inv[3][3], ct[3], it[3], mv[3];
        int a, c;
        memcpy(cr, bindWorld[i].r, sizeof cr);
        conjM(cr);
        for (a = 0; a < 3; a++) ct[a] = C_AXES[a] * bindWorld[i].t[a] * us;
        if (!inv3(cr, inv)) fail(x, "a bone matrix cannot be inverted");
        matvec3(inv, ct, mv);
        for (a = 0; a < 3; a++) it[a] = -mv[a];
        for (c = 0; c < 3; c++) {
            ibm[16 * i + 4 * c] = inv[0][c];
            ibm[16 * i + 4 * c + 1] = inv[1][c];
            ibm[16 * i + 4 * c + 2] = inv[2][c];
            ibm[16 * i + 4 * c + 3] = 0.0;
        }
        ibm[16 * i + 12] = it[0];
        ibm[16 * i + 13] = it[1];
        ibm[16 * i + 14] = it[2];
        ibm[16 * i + 15] = 1.0;
    }
    binit(x, &g->bin);
    {
        Buf *tb = bnew(x);
        for (i = 0; i < 16 * nb; i++) putF32(x, tb, ibm[i]);
        ibmAcc = gbAdd(x, g, tb->p, tb->n, F32, nb, T_MAT4, 0, 0, NULL, NULL);
        bdrop(tb);
    }
    memset(&variants, 0, sizeof variants);
    memset(prs, 0, sizeof prs);
    buildMeshPrimitives(x, m, bindWorld, &variants, us, prs);
    ntex = variants.n;
    nv = ntex > 1 ? ntex : 1;
    cols = nv < 4 ? nv : 4;
    rowsN = (nv + cols - 1) / cols;
    aw = cols * 256;
    ah = rowsN * 256;
    atlas = (uint8_t *)aalloc(x, (size_t)aw * (size_t)ah * 4);
    page = (uint8_t *)aalloc(x, 256 * 1024);
    for (i = 0; i < ntex; i++) {
        int ox = (i % cols) * 256, oy = (i / cols) * 256, y;
        variantRgba(m, variants.p[i].page, variants.p[i].mode, variants.p[i].cx, variants.p[i].cy, variants.p[i].alpha, page);
        for (y = 0; y < 256; y++) memcpy(atlas + ((size_t)(oy + y) * (size_t)aw + (size_t)ox) * 4, page + (size_t)y * 1024, 1024);
    }
    writePng(x, png, aw, ah, atlas);

    prims = (PrimOut *)aalloc(x, sizeof(PrimOut) * 4);
    *nTris = 0;
    for (key = 0; key < 4; key++) {
        Pr *pr = &prs[key];
        int textured = key >> 1, blend = key & 1, n = pr->vtx.n, j;
        Buf *tb = bnew(x);
        double mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
        PrimOut *po;
        if (n == 0 || pr->idx.n == 0) continue;
        po = &prims[nprim++];
        po->textured = textured;
        po->blend = blend;
        for (j = 0; j < n; j++) {
            int c;
            for (c = 0; c < 3; c++) {
                double v = pr->vtx.p[j].pos[c];
                if (j == 0 || v < mn[c]) mn[c] = v;
                if (j == 0 || v > mx[c]) mx[c] = v;
                putF32(x, tb, v);
            }
        }
        po->pos = gbAdd(x, g, tb->p, tb->n, F32, n, T_VEC3, 34962, 3, mn, mx);
        tb->n = 0;
        for (j = 0; j < n; j++) { putF32(x, tb, pr->vtx.p[j].nrm[0]); putF32(x, tb, pr->vtx.p[j].nrm[1]); putF32(x, tb, pr->vtx.p[j].nrm[2]); }
        po->nrm = gbAdd(x, g, tb->p, tb->n, F32, n, T_VEC3, 34962, 0, NULL, NULL);
        tb->n = 0;
        if (textured) {
            for (j = 0; j < n; j++) {
                const VKey *k = &pr->vtx.p[j].k;
                putF32(x, tb, ((double)((k->tvar % cols) * 256 + k->u) + 0.5) / (double)aw);
                putF32(x, tb, ((double)((k->tvar / cols) * 256 + k->v) + 0.5) / (double)ah);
            }
            po->third = gbAdd(x, g, tb->p, tb->n, F32, n, T_VEC2, 34962, 0, NULL, NULL);
        } else {
            int nc = blend ? 4 : 3;
            for (j = 0; j < n; j++) {
                const VKey *k = &pr->vtx.p[j].k;
                int c;
                for (c = 0; c < 3; c++) putF32(x, tb, (double)k->col[c] / 255.0);
                if (blend) putF32(x, tb, k->alpha == 2 ? 0.25 : 0.5);
            }
            po->third = gbAdd(x, g, tb->p, tb->n, F32, n, nc == 4 ? T_VEC4 : T_VEC3, 34962, 0, NULL, NULL);
        }
        tb->n = 0;
        for (j = 0; j < n; j++) {
            uint8_t q[4];
            q[0] = (uint8_t)pr->vtx.p[j].k.bi; q[1] = q[2] = q[3] = 0;
            if (pr->vtx.p[j].k.bi > 255) fail(x, "more than 256 bones");
            bput(x, tb, q, 4);
        }
        po->joints = gbAdd(x, g, tb->p, tb->n, U8, n, T_VEC4, 34962, 0, NULL, NULL);
        tb->n = 0;
        for (j = 0; j < n; j++) { putF32(x, tb, 1.0); putF32(x, tb, 0); putF32(x, tb, 0); putF32(x, tb, 0); }
        po->weights = gbAdd(x, g, tb->p, tb->n, F32, n, T_VEC4, 34962, 0, NULL, NULL);
        tb->n = 0;
        if (n < 65536) {
            for (j = 0; j < pr->idx.n; j++) { uint8_t q[2] = {(uint8_t)pr->idx.p[j], (uint8_t)(pr->idx.p[j] >> 8)}; bput(x, tb, q, 2); }
            po->indices = gbAdd(x, g, tb->p, tb->n, U16, pr->idx.n, T_SCALAR, 34963, 0, NULL, NULL);
        } else {
            for (j = 0; j < pr->idx.n; j++) {
                uint8_t q[4] = {(uint8_t)pr->idx.p[j], (uint8_t)(pr->idx.p[j] >> 8), (uint8_t)(pr->idx.p[j] >> 16), (uint8_t)(pr->idx.p[j] >> 24)};
                bput(x, tb, q, 4);
            }
            po->indices = gbAdd(x, g, tb->p, tb->n, U32, pr->idx.n, T_SCALAR, 34963, 0, NULL, NULL);
        }
        po->material = nprim - 1;
        *nTris += pr->idx.n / 3;
        bdrop(tb);
    }
    memset(&anims, 0, sizeof anims);
    buildAnimations(x, m, mo, g, idleFrame, us, &anims, slotUsed);
    *nAnims = anims.n;

    /* ---- the JSON, in the tool's key order ---- */
    bpf(x, json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"dw3 model export\",\"extras\":{\"bones\":%d,\"textureVariants\":%d,\"restFrame\":%d}},", nb, ntex, idleFrame);
    bs(x, json, "\"scene\":0,");
    bpf(x, json, "\"scenes\":[{\"name\":\"%s\",\"nodes\":[0,%d]}],", name, nb);
    bs(x, json, "\"nodes\":[");
    for (i = 0; i < nb; i++) {
        int c, firstChild = 1;
        if (i) bs(x, json, ",");
        bpf(x, json, "{\"name\":\"bone_%02d\"", i);
        if (i) {
            bs(x, json, ",\"translation\":");
            blist(x, json, nodes[i].t, 3);
            bs(x, json, ",\"rotation\":");
            blist(x, json, nodes[i].q, 4);
            bs(x, json, ",\"scale\":");
            blist(x, json, nodes[i].s, 3);
        }
        for (c = 1; c < nb; c++) {
            if (m->b[c].parent != i) continue;
            bs(x, json, firstChild ? ",\"children\":[" : ",");
            bpf(x, json, "%d", c);
            firstChild = 0;
        }
        if (!firstChild) bs(x, json, "]");
        bs(x, json, "}");
    }
    bs(x, json, ",{\"name\":\"mesh\",\"mesh\":0,\"skin\":0}],");
    bpf(x, json, "\"skins\":[{\"name\":\"skeleton\",\"inverseBindMatrices\":%d,\"skeleton\":0,\"joints\":[", ibmAcc);
    for (i = 0; i < nb; i++) bpf(x, json, "%s%d", i ? "," : "", i);
    bs(x, json, "]}],");
    bpf(x, json, "\"meshes\":[{\"name\":\"%s\",\"primitives\":[", name);
    for (i = 0; i < nprim; i++) {
        const PrimOut *po = &prims[i];
        if (i) bs(x, json, ",");
        bpf(x, json, "{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"%s\":%d,\"JOINTS_0\":%d,\"WEIGHTS_0\":%d},\"indices\":%d,\"material\":%d,\"mode\":4}",
            po->pos, po->nrm, po->textured ? "TEXCOORD_0" : "COLOR_0", po->third, po->joints, po->weights, po->indices, po->material);
    }
    bs(x, json, "]}],\"materials\":[");
    for (i = 0; i < nprim; i++) {
        const PrimOut *po = &prims[i];
        if (i) bs(x, json, ",");
        bpf(x, json, "{\"name\":\"%s%s\",\"pbrMetallicRoughness\":{\"metallicFactor\":0.0,\"roughnessFactor\":1.0,\"baseColorFactor\":[1.0,1.0,1.0,1.0]%s},"
            "\"alphaMode\":\"%s\",\"doubleSided\":false%s}",
            po->textured ? "tex" : "flat", po->blend ? "_blend" : "", po->textured ? ",\"baseColorTexture\":{\"index\":0}" : "",
            po->blend ? "BLEND" : (po->textured ? "MASK" : "OPAQUE"), (po->textured && !po->blend) ? ",\"alphaCutoff\":0.5" : "");
    }
    bpf(x, json, "],\"buffers\":[{\"byteLength\":%llu,\"uri\":\"%s.bin\"}],", (unsigned long long)g->bin.n, uriBase);
    bpf(x, json, "\"images\":[{\"uri\":\"%s.png\",\"name\":\"atlas\"}],", uriBase);
    bs(x, json, "\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728,\"wrapS\":33071,\"wrapT\":33071}],");
    bs(x, json, "\"textures\":[{\"source\":0,\"sampler\":0}]");
    if (anims.n) {
        bs(x, json, ",\"animations\":[");
        for (i = 0; i < anims.n; i++) {
            const AnimOut *ao = &anims.p[i];
            int k;
            if (i) bs(x, json, ",");
            bpf(x, json, "{\"name\":\"motion_%02d\",\"samplers\":[", ao->slot);
            for (k = 0; k < ao->nch; k++) bpf(x, json, "%s{\"input\":%d,\"output\":%d,\"interpolation\":\"LINEAR\"}", k ? "," : "", ao->sIn[k], ao->sOut[k]);
            bs(x, json, "],\"channels\":[");
            for (k = 0; k < ao->nch; k++)
                bpf(x, json, "%s{\"sampler\":%d,\"target\":{\"node\":%d,\"path\":\"%s\"}}", k ? "," : "", k, ao->cNode[k], PATH_NAME[ao->cPath[k]]);
            bpf(x, json, "],\"extras\":{\"slot\":%d,\"frames\":%d,\"ends\":%s", ao->slot, ao->frames, ao->ends ? "true" : "false");
            if (ao->loopStart != NO_LOOP) bpf(x, json, ",\"loopStartFrame\":%d", ao->loopStart);
            bs(x, json, "}}");
        }
        bs(x, json, "]");
    }
    bs(x, json, ",\"bufferViews\":[");
    for (i = 0; i < g->views.n; i++) {
        const View *v = &g->views.p[i];
        bpf(x, json, "%s{\"buffer\":0,\"byteOffset\":%llu,\"byteLength\":%llu", i ? "," : "", (unsigned long long)v->off, (unsigned long long)v->len);
        if (v->target) bpf(x, json, ",\"target\":%d", v->target);
        bs(x, json, "}");
    }
    bs(x, json, "],\"accessors\":[");
    for (i = 0; i < g->accs.n; i++) emitAcc(x, json, &g->accs.p[i], i == 0);
    bs(x, json, "]}");
}

/* ---------------------------------------------------------------- one fighter */

static Sp loadFile(Ex *x, const ModelExportIo *io, int file) {
    size_t len = 0;
    const uint8_t *p = io->file != NULL ? io->file(io->ud, file, &len) : NULL;
    if (p == NULL || len == 0) fail(x, "file %d cannot be read", file);
    return copySp(x, p, len);
}

typedef struct Fighter {
    int id, enemy;
    uint32_t model, motions, effects;
    int distance, height;
} Fighter;

static int findFighter(Ex *x, Sp d, int fid, Fighter *f) {
    size_t ents = rd32(x, d, 4), partners = rd32(x, d, 8), enemies = rd32(x, d, 12), o = ents;
    for (;;) {
        int id = rd16(x, d, o);
        unsigned idx = rd8(x, d, o + 2), kind = rd8(x, d, o + 3);
        if (id == 0) return 0;
        if (id == fid) {
            size_t p = kind >= 0x3A ? enemies + (size_t)idx * 0x48 : partners + (size_t)idx * 0xC4;
            f->id = fid;
            f->enemy = kind >= 0x3A;
            f->model = rd32(x, d, p);
            f->motions = rd32(x, d, p + 4);
            f->effects = rd32(x, d, p + 8);
            f->distance = rd16(x, d, p + 0x10);
            f->height = rd16(x, d, p + 0x18);
            return 1;
        }
        o += 4;
    }
}

int modelExportIds(const ModelExportIo *io, int *ids, int cap) {
    Ex x;
    volatile int n = 0;
    memset(&x, 0, sizeof x);
    if (setjmp(x.jb)) {
        xfree(&x);
        return -1;
    }
    {
        Sp d = loadFile(&x, io, MODEL_EXPORT_FILE_FIGHTERS);
        size_t o = rd32(&x, d, 4);
        for (;;) {
            int id = rd16(&x, d, o);
            if (id == 0) break;
            if (n < cap) ids[n] = id;
            n++;
            o += 4;
        }
    }
    xfree(&x);
    return n < cap ? n : cap;
}

void modelExportFree(ModelExportOut *o) {
    free(o->gltf);
    free(o->bin);
    free(o->png);
    free(o->json);
    memset(o, 0, sizeof *o);
}

static void detach(Ex *x, Buf *b, uint8_t **out, size_t *len) {
    uint8_t *p = (uint8_t *)malloc(b->n ? b->n : 1);
    if (p == NULL) fail(x, "out of memory");
    if (b->n) memcpy(p, b->p, b->n);
    *out = p;
    *len = b->n;
}

int modelExportBuild(const ModelExportIo *io, int fighterId, double us, ModelExportOut *out, char *err, size_t errCap) {
    Ex x;
    Buf json, png, mj;
    Gb g;
    char name[32];

    memset(&x, 0, sizeof x);
    memset(out, 0, sizeof *out);
    if (setjmp(x.jb)) {
        if (err != NULL && errCap) snprintf(err, errCap, "%s", x.err);
        xfree(&x);
        modelExportFree(out);
        return -1;
    }
    {
        Sp fighters = loadFile(&x, io, MODEL_EXPORT_FILE_FIGHTERS), md, mf;
        Fighter f;
        Model model;
        Motions mo;
        int slotUsed[SLOTS_MAX], i, nAnims = 0, nTris = 0, firstSlot = 1;

        memset(slotUsed, 0, sizeof slotUsed);
        memset(&model, 0, sizeof model);
        memset(&mo, 0, sizeof mo);
        memset(&g, 0, sizeof g);
        if (!findFighter(&x, fighters, fighterId, &f)) fail(&x, "no fighter %d", fighterId);
        mf = loadFile(&x, io, (int)(f.model >> 16));
        parseModel(&x, mf, rd32(&x, mf, (size_t)(f.model & 0xFFFF) * 4), &model);
        md = (f.motions >> 16) == (f.model >> 16) ? mf : loadFile(&x, io, (int)(f.motions >> 16));
        parseMotions(&x, md, rd32(&x, md, (size_t)(f.motions & 0xFFFF) * 4), &mo);
        binit(&x, &json);
        binit(&x, &png);
        binit(&x, &mj);
        snprintf(name, sizeof name, "fighter_%d", fighterId);
        buildGltf(&x, &model, &mo, name, "model", us, &json, &g, &png, slotUsed, &nAnims, &nTris);
        bs(&x, &mj, "{\n  \"format\": \"model-src-v1\",\n");
        bpf(&x, &mj, "  \"name\": \"%s\",\n  \"gltf\": \"model.gltf\",\n", name);
        {
            double inv = 1.0 / us;
            if (fabs(inv - floor(inv + 0.5)) < 1e-9) bpf(&x, &mj, "  \"scale\": %d,\n", (int)floor(inv + 0.5));
            else { char t[64]; fmtDouble(t, inv); bpf(&x, &mj, "  \"scale\": %s,\n", t); }
        }
        bpf(&x, &mj, "  \"effects\": \"dw3:digimon/%d\",\n  \"template\": \"dw3:digimon/%d\",\n", fighterId, fighterId);
        bpf(&x, &mj, "  \"info\": {\n    \"distance\": %d,\n    \"height\": %d\n  },\n  \"motions\": {\n", f.distance, f.height);
        for (i = 0; i < SLOTS_MAX; i++) {
            if (!slotUsed[i]) continue;
            bpf(&x, &mj, "%s    \"%d\": \"motion_%02d\"", firstSlot ? "" : ",\n", i, i);
            firstSlot = 0;
        }
        bs(&x, &mj, "\n  }\n}\n");
        detach(&x, &json, &out->gltf, &out->gltfLen);
        detach(&x, &g.bin, &out->bin, &out->binLen);
        detach(&x, &png, &out->png, &out->pngLen);
        detach(&x, &mj, &out->json, &out->jsonLen);
        out->bones = model.nb;
        out->anims = nAnims;
        out->tris = nTris;
    }
    xfree(&x);
    return 0;
}

/* ---------------------------------------------------------------- the job and the data folder */

static char sDataDir[1024];
static ModelExportIo sIo;
static int *sQueue;
static int sCount, sNext, sDone, sFailed, sSince;
static int sRan, sForce, sBuilt;
static TdManifest sMan;
static int sManLoaded, sManDirty;
static char sStatus[96];

void modelExportFolder(int fighterId, char *out, size_t cap) { snprintf(out, cap, "models/%d/", fighterId); }

static void manifestPath(char *out, size_t cap) { snprintf(out, cap, "%s/models/manifest.json", sDataDir); }

static void manifestLoad(void) {
    char mp[1200];
    uint8_t *mt;
    size_t ml;

    if (sManLoaded) return;
    memset(&sMan, 0, sizeof sMan);
    snprintf(sMan.stamp, sizeof sMan.stamp, "models-v1");
    manifestPath(mp, sizeof mp);
    if (tdReadFile(mp, &mt, &ml) == 0) {
        TdManifest old;
        char err[120];
        memset(&old, 0, sizeof old);
        if (tdManifestParse((const char *)mt, ml, &old, err, sizeof err) == 0) {
            tdManifestFree(&sMan);
            sMan = old;
        }
        free(mt);
    }
    sManLoaded = 1;
    sManDirty = 0;
}

static void manifestSave(void) {
    char mp[1200], dir[1200];
    char *text;

    if (!sManLoaded || !sManDirty) return;
    snprintf(dir, sizeof dir, "%s/models", sDataDir);
    manifestPath(mp, sizeof mp);
    text = tdManifestFormat(&sMan);
    if (text == NULL || tdMakeDirs(dir) || tdWriteFileAtomic(mp, text, strlen(text))) plogLine("models", "cannot write %s\n", mp);
    else sManDirty = 0;
    free(text);
}

/* one generated file of <data>/models/ under the manifest rule of ADR-005: an unedited copy is replaced, an edited one stays and
   the new version goes beside it as <rel>.new */
static void writeGenerated(const char *rel, const void *want, size_t len) {
    char path[1300], dir[1300], hex[17], wantHex[17], *slash;
    uint8_t *cur = NULL;
    size_t curLen = 0;
    int edited = 0;

    snprintf(path, sizeof path, "%s/models/%s", sDataDir, rel);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL) *slash = '\0';
    tdHashHex(tdHash(want, len), wantHex);
    if (tdReadFile(path, &cur, &curLen) == 0) {
        tdHashHex(tdHash(cur, curLen), hex);
        free(cur);
        if (!strcmp(hex, wantHex)) {
            if (!tdManifestFind(&sMan, rel)) {
                tdManifestSet(&sMan, rel, hex);
                sManDirty = 1;
            }
            return;
        }
        edited = tdFileState(1, tdManifestFind(&sMan, rel), hex) == TD_FILE_EDITED;
    }
    if (edited) {
        char np[1400];
        snprintf(np, sizeof np, "%s.new", path);
        if (tdMakeDirs(dir) || tdWriteFileAtomic(np, want, len)) plogLine("models", "cannot write %s\n", np);
        else plogLine("models", "%s was edited, kept; the new version is %s.new\n", rel, rel);
    } else if (tdMakeDirs(dir) || tdWriteFileAtomic(path, want, len)) {
        plogLine("models", "cannot write %s\n", path);
    } else {
        tdManifestSet(&sMan, rel, wantHex);
        sManDirty = 1;
    }
}

static const char *const kModelFiles[4] = {"model.gltf", "model.bin", "model.png", "model.json"};

/* "<id>/exporter" in the manifest holds the version that wrote the fighter's files (a manifest without it: old, rebuild) */
static void versionKey(int id, char *rel, size_t cap) { snprintf(rel, cap, "%d/exporter", id); }

static void versionHex(char *hex) { snprintf(hex, 17, "%016x", (unsigned)MODEL_EXPORT_VERSION); }

/* written by this version and all four files there: nothing to build. An edited file still counts as present */
static int upToDate(int id) {
    char rel[96], path[1300], hex[17];
    const char *have;
    int i;

    manifestLoad();
    versionKey(id, rel, sizeof rel);
    have = tdManifestFind(&sMan, rel);
    versionHex(hex);
    if (have == NULL || strcmp(have, hex)) return 0;
    for (i = 0; i < 4; i++) {
        FILE *f;
        snprintf(path, sizeof path, "%s/models/%d/%s", sDataDir, id, kModelFiles[i]);
        f = fopen(path, "rb");
        if (f == NULL) return 0;
        fclose(f);
    }
    return 1;
}

static SDL_mutex *sMu; /* guards the counters, the status and the slots of a worker job (NULL until the first one) */

static void lockMu(void) {
    if (sMu != NULL) SDL_LockMutex(sMu);
}

static void unlockMu(void) {
    if (sMu != NULL) SDL_UnlockMutex(sMu);
}

static int exportOneIo(const ModelExportIo *io, int id, int force) {
    ModelExportOut o;
    char err[256], rel[96], hex[17];
    int i;
    const uint8_t *data[4];
    size_t len[4];

    if (!force && sDataDir[0] && upToDate(id)) return 0;
    if (modelExportBuild(io, id, MODEL_EXPORT_UNIT_SCALE, &o, err, sizeof err) != 0) {
        plogLine("models", "fighter %d not exported: %s\n", id, err);
        return -1;
    }
    manifestLoad();
    data[0] = o.gltf; len[0] = o.gltfLen;
    data[1] = o.bin; len[1] = o.binLen;
    data[2] = o.png; len[2] = o.pngLen;
    data[3] = o.json; len[3] = o.jsonLen;
    for (i = 0; i < 4; i++) {
        snprintf(rel, sizeof rel, "%d/%s", id, kModelFiles[i]);
        writeGenerated(rel, data[i], len[i]);
    }
    modelExportFree(&o);
    versionKey(id, rel, sizeof rel);
    versionHex(hex);
    if (tdManifestSet(&sMan, rel, hex) == 0) sManDirty = 1;
    lockMu();
    sBuilt++;
    unlockMu();
    return 0;
}

static int exportOne(int id, int force) { return exportOneIo(&sIo, id, force); }

/* ---- the worker thread of the menu's job ----
 * The disc reader (host_cd.c) shares one FILE* with the game, so the worker never calls it: the main thread (modelExportPump,
 * once per frame) reads the files of the next few fighters into a small ring and the worker builds and writes from that memory.
 * The manifest, the files and the counters of a running job belong to the worker; the counters and the status text are read
 * under sMu. The job is joined by the pump when it is over and by modelExportShutdown at exit. */

#define PF_SLOTS 4

typedef struct Slot {
    int job; /* the queue index whose files these are, -1: free */
    int num[2];
    uint8_t *f[2];
    size_t len[2];
} Slot;

static SDL_Thread *sThread;
static SDL_cond *sCv;
static int sStop, sWorkerDone, sFed;
static Slot sSlots[PF_SLOTS];
static uint8_t *sFightersCopy;
static size_t sFightersLen;
static char sStatusOut[96];

static void slotClear(Slot *s) {
    int i;
    for (i = 0; i < 2; i++) {
        free(s->f[i]);
        s->f[i] = NULL;
        s->len[i] = 0;
        s->num[i] = 0;
    }
    s->job = -1;
}

/* a running job (the worker has not finished) */
static int workerBusy(void) {
    int busy;
    if (sThread == NULL) return 0;
    lockMu();
    busy = !sWorkerDone;
    unlockMu();
    return busy;
}

/* join a finished (or stopped) worker and free what the job held */
static void reap(void) {
    int i;
    if (sThread == NULL) return;
    SDL_WaitThread(sThread, NULL);
    sThread = NULL;
    for (i = 0; i < PF_SLOTS; i++) slotClear(&sSlots[i]);
    free(sFightersCopy);
    sFightersCopy = NULL;
    sFightersLen = 0;
    free(sQueue);
    sQueue = NULL;
}

static const uint8_t *slotFile(void *ud, int file, size_t *len) {
    const Slot *s = (const Slot *)ud;
    int i;
    if (file == MODEL_EXPORT_FILE_FIGHTERS) {
        *len = sFightersLen;
        return sFightersCopy;
    }
    for (i = 0; i < 2; i++) {
        if (s->f[i] != NULL && s->num[i] == file) {
            *len = s->len[i];
            return s->f[i];
        }
    }
    return NULL;
}

static void finishStatus(void) {
    if (sFailed) snprintf(sStatus, sizeof sStatus, "Done, %d failed", sFailed);
    else snprintf(sStatus, sizeof sStatus, "Done (%d)", sDone);
    plogLine("models", "Done: %d models in %s/models, %d failed\n", sDone, sDataDir, sFailed);
}

static int workerMain(void *arg) {
    int j, since = 0;
    (void)arg;
    for (j = 0; j < sCount; j++) {
        Slot *s = &sSlots[j % PF_SLOTS];
        ModelExportIo io;
        int r;

        SDL_LockMutex(sMu);
        while (!sStop && s->job != j) SDL_CondWait(sCv, sMu);
        SDL_UnlockMutex(sMu);
        if (sStop) break;
        io.file = slotFile;
        io.ud = s;
        r = exportOneIo(&io, sQueue[j], sForce);
        SDL_LockMutex(sMu);
        slotClear(s);
        if (r == 0) sDone++;
        else sFailed++;
        sNext = j + 1;
        if (sNext < sCount) snprintf(sStatus, sizeof sStatus, "Exporting %d/%d", sNext, sCount);
        SDL_CondBroadcast(sCv);
        SDL_UnlockMutex(sMu);
        if (++since >= 16) {
            manifestSave();
            since = 0;
        }
    }
    manifestSave();
    SDL_LockMutex(sMu);
    if (j >= sCount) finishStatus();
    sWorkerDone = 1;
    SDL_UnlockMutex(sMu);
    return 0;
}

/* the model file and the motions file numbers of a fighter, 0 when it is not in the fighters file */
static void fighterFiles(int id, int *num) {
    Ex x;
    num[0] = num[1] = 0;
    memset(&x, 0, sizeof x);
    if (setjmp(x.jb)) {
        xfree(&x);
        return;
    }
    {
        Sp d;
        Fighter f;
        d.p = sFightersCopy;
        d.n = sFightersLen;
        if (findFighter(&x, d, id, &f)) {
            num[0] = (int)(f.model >> 16);
            num[1] = (int)(f.motions >> 16);
        }
    }
    xfree(&x);
}

/* main thread: read the files of up to maxN of the next fighters into free slots */
static void feed(int maxN) {
    while (maxN-- > 0 && sFed < sCount) {
        Slot *s = &sSlots[sFed % PF_SLOTS];
        int fn[2], i, isFree;

        lockMu();
        isFree = s->job == -1;
        unlockMu();
        if (!isFree) return;
        fighterFiles(sQueue[sFed], fn);
        for (i = 0; i < 2; i++) {
            const uint8_t *p;
            size_t len = 0;
            if (fn[i] <= 0 || (i == 1 && fn[1] == fn[0])) continue;
            p = sIo.file(sIo.ud, fn[i], &len);
            if (p == NULL || len == 0) continue;
            s->f[i] = (uint8_t *)malloc(len);
            if (s->f[i] == NULL) continue;
            memcpy(s->f[i], p, len);
            s->len[i] = len;
            s->num[i] = fn[i];
        }
        lockMu();
        s->job = sFed;
        SDL_CondBroadcast(sCv);
        unlockMu();
        sFed++;
    }
}

static void dropJob(void) {
    free(sQueue);
    sQueue = NULL;
    sCount = sNext = 0;
}

void modelExportSetup(const char *dataDir, const ModelExportIo *io) {
    modelExportShutdown();
    dropJob();
    if (sManLoaded) tdManifestFree(&sMan);
    sManLoaded = 0;
    sRan = 0;
    sStatus[0] = '\0';
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

int modelExportRequest(const char *spec) { return modelExportRequestEx(spec, 0); }

int modelExportRequestEx(const char *spec, int force) {
    int all[1024], nAll, n = 0, i;
    int *q;

    if (workerBusy()) return -1; /* one job at a time: a press while one runs changes nothing */
    reap();
    dropJob();
    sDone = sFailed = sBuilt = 0;
    sForce = force;
    sRan = 0;
    sStatus[0] = '\0';
    if (!sDataDir[0] || sIo.file == NULL || spec == NULL) return -1;
    nAll = modelExportIds(&sIo, all, 1024);
    if (nAll <= 0) {
        plogLine("models", "the fighters file cannot be read: nothing to export\n");
        return -1;
    }
    q = (int *)malloc(sizeof(int) * (size_t)nAll);
    if (q == NULL) return -1;
    {
        const char *p = spec;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "all", 3) && (p[3] == '\0' || p[3] == ' ' || p[3] == ',' || p[3] == '\t')) {
            for (i = 0; i < nAll; i++) q[n++] = all[i];
        } else {
            while (*p) {
                char *end;
                long v;
                while (*p == ' ' || *p == '\t' || *p == ',') p++;
                if (!*p) break;
                if (!strncmp(p, "dw3:digimon/", 12)) p += 12;
                v = strtol(p, &end, 10);
                if (end == p) {
                    plogLine("models", "export_models: \"%.20s\" is not a fighter number (use all or numbers separated by commas)\n", p);
                    break;
                }
                p = end;
                for (i = 0; i < nAll; i++) if (all[i] == (int)v) break;
                if (i == nAll) {
                    plogLine("models", "export_models: no fighter %ld\n", v);
                    continue;
                }
                {
                    int j;
                    for (j = 0; j < n; j++) if (q[j] == (int)v) break;
                    if (j == n) q[n++] = (int)v;
                }
            }
        }
    }
    if (n == 0) {
        free(q);
        return -1;
    }
    sQueue = q;
    sCount = n;
    sNext = 0;
    sRan = 1;
    sSince = 0;
    snprintf(sStatus, sizeof sStatus, "Exporting 0/%d", n);
    return n;
}

int modelExportStart(const char *spec, int force) {
    const uint8_t *fp;
    size_t flen = 0;
    int n;

    n = modelExportRequestEx(spec, force);
    if (n <= 0) return -1;
    fp = sIo.file(sIo.ud, MODEL_EXPORT_FILE_FIGHTERS, &flen);
    if (fp != NULL && flen > 0) sFightersCopy = (uint8_t *)malloc(flen);
    if (sFightersCopy == NULL) {
        dropJob();
        return -1;
    }
    memcpy(sFightersCopy, fp, flen);
    sFightersLen = flen;
    if (sMu == NULL) sMu = SDL_CreateMutex();
    if (sCv == NULL) sCv = SDL_CreateCond();
    if (sMu != NULL && sCv != NULL) {
        int i;
        for (i = 0; i < PF_SLOTS; i++) sSlots[i].job = -1;
        sStop = sWorkerDone = sFed = 0;
        sThread = SDL_CreateThread(workerMain, "dw3-model-export", NULL);
    }
    if (sThread == NULL) {
        plogLine("models", "the export thread cannot start: %s\n", SDL_GetError());
        free(sFightersCopy);
        sFightersCopy = NULL;
        sFightersLen = 0;
        dropJob();
        return -1;
    }
    return n;
}

void modelExportShutdown(void) {
    if (sThread == NULL) return;
    SDL_LockMutex(sMu);
    sStop = 1;
    SDL_CondBroadcast(sCv);
    SDL_UnlockMutex(sMu);
    reap();
}

int modelExportBuilt(void) {
    int n;
    lockMu();
    n = sBuilt;
    unlockMu();
    return n;
}

int modelExportActive(void) {
    if (sThread != NULL) return workerBusy();
    return sQueue != NULL && sNext < sCount;
}

void modelExportCounts(int *done, int *failed, int *total) {
    lockMu();
    if (done) *done = sDone;
    if (failed) *failed = sFailed;
    if (total) *total = sCount;
    unlockMu();
}

const char *modelExportStatus(void) {
    lockMu();
    snprintf(sStatusOut, sizeof sStatusOut, "%s", sStatus);
    unlockMu();
    return sStatusOut;
}

int modelExportPump(unsigned budgetMs) {
    clock_t start = clock();

    if (sThread != NULL) {
        feed(2);
        if (workerBusy()) return 1;
        reap();
        return 0;
    }
    if (!modelExportActive()) return 0;
    do {
        if (exportOne(sQueue[sNext], sForce) == 0) sDone++;
        else sFailed++;
        sNext++;
        if (++sSince >= 16) {
            manifestSave();
            sSince = 0;
        }
    } while (sNext < sCount && (unsigned)((clock() - start) * 1000 / CLOCKS_PER_SEC) < budgetMs);
    if (sNext < sCount) {
        snprintf(sStatus, sizeof sStatus, "Exporting %d/%d", sNext, sCount);
        return 1;
    }
    manifestSave();
    finishStatus();
    free(sQueue);
    sQueue = NULL;
    return 0;
}

int modelExportOne(int fighterId) {
    int r;
    if (!sDataDir[0] || sIo.file == NULL || workerBusy()) return -1;
    r = exportOne(fighterId, 1);
    manifestSave();
    return r;
}
