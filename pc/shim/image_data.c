/*
 * Menu art as PNG (feature menu-art-png, T2). See image_data.h.
 *
 * Evidence for the formats: docs/formats.md 4.8, src/engine/gfx/tim_loader.c (timLoaderLoadArchive),
 * src/engine/file/file_cache.c (getArchiveEntry), include/engine/gfx.h (SpritePart, TimBlock).
 */
#include "image_data.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "exp_core.h"
#include "mods.h"
#include "port_log.h"
#include "png_read.h"
#include "png_write.h"
#include "port_fs.h"
#include "port_paths.h"
#include "sprite_data.h"

#define DISC_PREFIX "AAA/DAT/"

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

void imgPicFree(ImgPic *p)
{
    free(p->px);
    p->px = NULL;
}

/* PS1 colour: 0x0000 is transparent, any other colour opaque (STP is a per-primitive blend flag) */
void imgColor(uint16_t c, uint8_t rgb[3], uint8_t *alpha)
{
    unsigned r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    rgb[0] = (uint8_t)((r << 3) | (r >> 2));
    rgb[1] = (uint8_t)((g << 3) | (g >> 2));
    rgb[2] = (uint8_t)((b << 3) | (b >> 2));
    *alpha = c == 0 ? 0 : 255;
}

/* ---- TIM ---- */

typedef ImgTim TimInfo;

int imgTimParse(const uint8_t *t, size_t len, ImgTim *o)
{
    uint32_t flag, sz;
    size_t at = 8;
    memset(o, 0, sizeof *o);
    if (len < 20 || le32(t) != 0x10) return -1;
    flag = le32(t + 4);
    if (flag & ~0xBu || (flag & 3) == 3) return -1;
    o->mode = (int)(flag & 3);
    if (flag & 8) {
        if (at + 12 > len) return -1;
        sz = le32(t + at);
        o->cw = (int)le16(t + at + 8);
        o->ch = (int)le16(t + at + 10);
        if (sz < 12 || at + sz > len || (size_t)12 + 2u * (size_t)o->cw * (size_t)o->ch > sz) return -1;
        o->clut = t + at + 12;
        at += sz;
    }
    if (at + 12 > len) return -1;
    o->iw = (int)le16(t + at + 8);
    o->ih = (int)le16(t + at + 10);
    if (o->iw <= 0 || o->ih <= 0 || at + 12 + 2u * (size_t)o->iw * (size_t)o->ih > len) return -1;
    o->img = t + at + 12;
    return 0;
}

int imgDecodeTim(const uint8_t *tim, size_t len, const ImgHints *hints, ImgPic *out)
{
    TimInfo t;
    int bits, n, x, y, i;
    const uint16_t *pal = NULL;
    int nPal = 0;
    uint16_t own[256];

    memset(out, 0, sizeof *out);
    if (imgTimParse(tim, len, &t)) return -1;
    bits = t.mode == 0 ? 4 : t.mode == 1 ? 8 : (hints && hints->pixelBits == 8 ? 8 : 4);
    n = bits == 4 ? 16 : 256;
    out->w = t.iw * (16 / bits);
    out->h = t.ih;
    out->depth = bits;
    out->nColors = n;
    if (t.mode < 2 && t.clut) {
        nPal = t.cw < n ? t.cw : n;
        for (i = 0; i < nPal; i++) own[i] = (uint16_t)le16(t.clut + 2 * i); /* row 0 */
        pal = own;
    } else if (t.mode == 2 && hints && hints->pal) {
        pal = hints->pal;
        nPal = hints->nPal < n ? hints->nPal : n;
    }
    out->px = (uint8_t *)malloc((size_t)out->w * (size_t)out->h);
    if (!out->px) return -1;
    for (y = 0; y < out->h; y++) {
        for (x = 0; x < out->w; x++) {
            unsigned v = le16(t.img + 2 * ((size_t)y * (size_t)t.iw + (size_t)(x / (16 / bits))));
            out->px[(size_t)y * out->w + x] = bits == 4 ? (uint8_t)((v >> ((x & 3) * 4)) & 15) : (uint8_t)((v >> ((x & 1) * 8)) & 255);
        }
    }
    for (i = 0; i < n; i++) {
        if (pal) {
            imgColor(i < nPal ? pal[i] : 0, out->rgb + 3 * i, out->alpha + i);
        } else { /* no palette known: a grey ramp, index 0 transparent like every PS1 palette */
            out->rgb[3 * i] = out->rgb[3 * i + 1] = out->rgb[3 * i + 2] = (uint8_t)(i * 255 / (n - 1));
            out->alpha[i] = i ? 255 : 0;
        }
    }
    out->guessed = pal == NULL;
    return 0;
}

int imgPicToPng(const ImgPic *p, uint8_t **png, size_t *len)
{
    int nA = 0, i;
    for (i = 0; i < p->nColors; i++) {
        if (p->alpha[i] != 255) nA = i + 1;
    }
    return pngWriteIndexed(p->px, p->w, p->h, p->depth, p->rgb, p->nColors, p->alpha, nA, png, len);
}

/* ---- archives ---- */

/* RLEN: "RLEN", the unpacked size, then runs (0x80|n, byte: repeat; n, n bytes: literal), 0 ends */
uint8_t *imgRlenUnpack(const uint8_t *s, size_t len, size_t *outLen)
{
    size_t cap, i = 8, o = 0;
    uint8_t *d;
    if (len < 9) return NULL;
    cap = le32(s + 4);
    if (cap == 0 || cap > (4u << 20)) return NULL;
    d = (uint8_t *)malloc(cap);
    if (!d) return NULL;
    while (i < len && s[i]) {
        unsigned c = s[i];
        if (c & 0x80) {
            c &= 0x7F;
            if (i + 1 >= len || o + c > cap) goto bad;
            memset(d + o, s[i + 1], c);
            o += c;
            i += 2;
        } else {
            if (i + 1 + c > len || o + c > cap) goto bad;
            memcpy(d + o, s + i + 1, c);
            o += c;
            i += 1 + c;
        }
    }
    if (i >= len) goto bad;
    *outLen = o;
    return d;
bad:
    free(d);
    return NULL;
}

/* The archive table at `o`: the first word is the table's size, each entry an offset from `o`; a 0 ends it.
   Returns the entry count (0: not an archive). */
static int archiveCount(const uint8_t *d, size_t len, size_t o)
{
    uint32_t w0;
    int n, i;
    if (o + 4 > len) return 0;
    w0 = le32(d + o);
    if (w0 < 4 || (w0 & 3) || w0 > 256 || o + w0 > len) return 0;
    n = (int)(w0 / 4);
    for (i = 0; i < n; i++) {
        uint32_t e = le32(d + o + 4 * (size_t)i);
        if (e == 0) return i; /* the end of the table; some archives pad up to their first entry */
        if (o + e >= len) return 0;
    }
    return n;
}

typedef int (*WalkCb)(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len, size_t off);

static int walkNode(const uint8_t *d, size_t len, size_t o, int *chain, int depth, WalkCb cb, void *ctx, int *count, int *stop)
{
    int n = archiveCount(d, len, o), i;
    for (i = 0; i < n && !*stop; i++) {
        size_t c = o + le32(d + o + 4 * (size_t)i), left = len - c;
        TimInfo ti;
        chain[depth] = i;
        if (left >= 8 && !memcmp(d + c, "RLEN", 4)) {
            size_t ul;
            uint8_t *u = imgRlenUnpack(d + c, left, &ul);
            if (u && imgTimParse(u, ul, &ti) == 0) {
                (*count)++;
                if (cb(ctx, chain, depth + 1, u, ul, c)) *stop = 1;
            }
            free(u);
        } else if (imgTimParse(d + c, left, &ti) == 0) {
            (*count)++;
            if (cb(ctx, chain, depth + 1, d + c, left, c)) *stop = 1;
        } else if (depth + 1 < IMG_MAX_DEPTH) {
            walkNode(d, len, c, chain, depth + 1, cb, ctx, count, stop);
        }
    }
    return 0;
}

static int walkOff(const uint8_t *file, size_t len, WalkCb cb, void *ctx)
{
    int chain[IMG_MAX_DEPTH], count = 0, stop = 0;
    walkNode(file, len, 0, chain, 0, cb, ctx, &count, &stop);
    return count;
}

/* CARDPAK0-4 (content-export E4): up to 64 cards of CARD_STRIDE bytes, each a 12-byte header and a 32x32 8-bit TIM with its
   own CLUT (include/engine/gfx.h CardImage). There is no table: the card is image [i] at i * CARD_STRIDE + CARD_HEAD. */
#define CARD_STRIDE 0x62C
#define CARD_HEAD 12
#define CARD_TIM_LEN (CARD_STRIDE - CARD_HEAD)
#define CARD_MAX 64
/* the name of a card face in an override: "cards/c<nnn>/face.png", a path of the cards topic (readable-assets A7.4), not of images/ */
#define CARD_REL "cards/"
/* the name of a field sprite's frame in an override: "sprites/<name>/<EEE>.png", a path of the sprites topic (readable-assets A7.5) */
#define SPR_REL "sprites/"

static int walkCards(const uint8_t *file, size_t len, WalkCb cb, void *ctx)
{
    int chain[IMG_MAX_DEPTH], i, count = 0;
    for (i = 0; i < CARD_MAX && (size_t)(i + 1) * CARD_STRIDE <= len; i++) {
        size_t off = (size_t)i * CARD_STRIDE + CARD_HEAD;
        TimInfo ti;
        if (imgTimParse(file + off, CARD_TIM_LEN, &ti)) continue;
        chain[0] = i;
        count++;
        if (cb(ctx, chain, 1, file + off, CARD_TIM_LEN, off)) break;
    }
    return count;
}

int imgIsCardFile(const char *p)
{
    static const char pre[] = DISC_PREFIX "CARD/CARDPAK";
    size_t n = sizeof pre - 1;
    return !strncmp(p, pre, n) && p[n] >= '0' && p[n] <= '4' && !strcmp(p + n + 1, ".BIN");
}

/* The game's number of card `index` of CARDPAK<A>: cardDrawerSetCard (src/engine/gfx/card_drawer.c) takes id, reads file CARD_IMAGE_FILES[(id - 1) >> 6] and picks
   image (id - 1) & 63, so the card is A * 64 + index + 1. 0 when the path is no card file or the index is past the file. */
int imgCardId(const char *discPath, int index)
{
    if (!imgIsCardFile(discPath) || index < 0 || index >= CARD_MAX) return 0;
    return (discPath[sizeof DISC_PREFIX "CARD/CARDPAK" - 1] - '0') * CARD_MAX + index + 1;
}

int imgCardFolder(int id, char *out, size_t cap)
{
    return id < 1 || id > 999 || snprintf(out, cap, "c%03d", id) >= (int)cap ? -1 : 0;
}

int imgCardIdOf(const char *folder)
{
    int i, id = 0;

    if (folder[0] != 'c' || strlen(folder) != 4) return 0;
    for (i = 1; i < 4; i++) {
        if (folder[i] < '0' || folder[i] > '9') return 0;
        id = id * 10 + (folder[i] - '0');
    }
    return id;
}

/* The field stage art (content-export E6a): FIELD/STAGE/S<nnn>MASK.BIN is an archive of the stage's map-object pages (file_loader.c loads
   it with loadArchive at the objects area), FIELD/STAGE/S<nnn>BG<nn>.BIN an archive of background images. */
static int stageName(const char *p, const char *mid, int midDigits, const char *tail)
{
    static const char pre[] = DISC_PREFIX "FIELD/STAGE/S";
    size_t n = sizeof pre - 1, i, ml = strlen(mid);
    if (strncmp(p, pre, n)) return 0;
    for (i = 0; i < 3; i++) {
        if (p[n + i] < '0' || p[n + i] > '9') return 0;
    }
    n += 3;
    if (strncmp(p + n, mid, ml)) return 0;
    n += ml;
    for (i = 0; i < (size_t)midDigits; i++) {
        if (p[n + i] < '0' || p[n + i] > '9') return 0;
    }
    return !strcmp(p + n + midDigits, tail);
}

int imgIsStageMask(const char *p) { return stageName(p, "MASK", 0, ".BIN"); }

int imgIsStageBg(const char *p) { return stageName(p, "BG", 2, ".BIN"); }

/* the images of one disc file, whatever its layout */
static int walkFile(const char *discPath, const uint8_t *file, size_t len, WalkCb cb, void *ctx)
{
    return imgIsCardFile(discPath) ? walkCards(file, len, cb, ctx) : walkOff(file, len, cb, ctx);
}

typedef struct VisitAdapter {
    ImgVisit cb;
    void *ctx;
} VisitAdapter;

static int adaptVisit(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len, size_t off)
{
    VisitAdapter *a = (VisitAdapter *)ctx;
    (void)off;
    return a->cb(a->ctx, chain, depth, tim, len);
}

int imgWalk(const uint8_t *file, size_t len, ImgVisit cb, void *ctx)
{
    VisitAdapter a;
    a.cb = cb;
    a.ctx = ctx;
    return walkOff(file, len, adaptVisit, &a);
}

/* ---- sprite sheets ---- */

typedef struct Parts {
    const uint8_t *p;
    int n;
} Parts;

static int sheetParts(const uint8_t *f, size_t len, Parts *o)
{
    uint32_t base, po, io;
    const uint8_t *s;
    int i;
    if (!f || len < 20) return -1;
    base = le32(f);
    if (base < 4 || base + 12 > len) return -1;
    s = f + base;
    po = le32(s);
    io = le32(s + 4);
    if (po < 8 || io <= po || (size_t)base + io > len || (io - po) / 14 < 1) return -1;
    o->p = s + po;
    o->n = (int)((io - po) / 14);
    for (i = 0; i < o->n; i++) { /* a real sheet: modes 0/1 and sane sizes; anything else is not one */
        int w = (short)le16(o->p + 14 * i + 4), h = (short)le16(o->p + 14 * i + 6), m = (short)le16(o->p + 14 * i + 12);
        if (m < 0 || m > 1 || w < 0 || h < 0 || w > 1024 || h > 1024) return -1;
    }
    return 0;
}

int imgSheetMode(const uint8_t *f, size_t len, int *mode)
{
    Parts ps;
    int i, c[2] = {0, 0};
    if (sheetParts(f, len, &ps)) return -1;
    for (i = 0; i < ps.n; i++) c[(short)le16(ps.p + 14 * i + 12) & 1]++;
    *mode = c[1] > c[0];
    return 0;
}

int imgSheetClut(const uint8_t *f, size_t len, int page, int mode, int *clutX, int *clutY)
{
    Parts ps;
    int i, j, best = 0, pw = mode ? 128 : 256;
    if (sheetParts(f, len, &ps)) return -1;
    for (i = 0; i < ps.n; i++) {
        int u = (short)le16(ps.p + 14 * i), cx = (short)le16(ps.p + 14 * i + 8), cy = (short)le16(ps.p + 14 * i + 10), k = 0;
        if (u < 0 || u / pw != page) continue;
        for (j = 0; j < ps.n; j++) {
            int u2 = (short)le16(ps.p + 14 * j);
            if (u2 >= 0 && u2 / pw == page && (short)le16(ps.p + 14 * j + 8) == cx && (short)le16(ps.p + 14 * j + 10) == cy) k++;
        }
        if (k > best) {
            best = k;
            *clutX = cx;
            *clutY = cy;
        }
    }
    return best ? 0 : -1;
}

int imgSheetPartCount(const uint8_t *f, size_t len)
{
    Parts ps;
    return sheetParts(f, len, &ps) ? 0 : ps.n;
}

int imgSheetPart(const uint8_t *f, size_t len, int index, ImgPart *out)
{
    Parts ps;
    const uint8_t *a;
    if (sheetParts(f, len, &ps) || index < 0 || index >= ps.n) return -1;
    a = ps.p + 14 * index;
    out->u = (short)le16(a);
    out->v = (short)le16(a + 2);
    out->w = (short)le16(a + 4);
    out->h = (short)le16(a + 6);
    out->clutX = (short)le16(a + 8);
    out->clutY = (short)le16(a + 10);
    out->mode = (short)le16(a + 12);
    return 0;
}

/* The offsets of a sheet (gfx.h): the sheet file starts with the offset of its first entry, and there three offsets (parts, frame ids, the first frame)
   followed by those of the other frames, so the frame table runs up to the parts. Frame i is the one whose id is the i-th byte of the id list (255 ends it). */
typedef struct Frames {
    const uint8_t *s;  /* the sheet */
    size_t span;       /* its bytes */
    size_t ids;        /* offset of the id list */
    int n;             /* frames */
    int nParts;
} Frames;

static int sheetFrames(const uint8_t *f, size_t len, Frames *o)
{
    Parts ps;
    uint32_t base, po, io;
    int n;
    if (sheetParts(f, len, &ps)) return -1;
    base = le32(f);
    o->s = f + base;
    o->span = len - base;
    po = le32(o->s);
    io = le32(o->s + 4);
    o->nParts = ps.n;
    o->ids = io;
    n = (int)((po - 8) / 4);
    for (o->n = 0; o->n < n && io + (size_t)o->n < o->span && o->s[io + (size_t)o->n] != 255; o->n++) {
    }
    return 0;
}

int imgSheetFrameCount(const uint8_t *f, size_t len)
{
    Frames fr;
    return sheetFrames(f, len, &fr) ? 0 : fr.n;
}

/* where frame `index` starts inside the sheet, 0 when it does not fit */
static size_t frameAt(const Frames *fr, int index, int *count)
{
    size_t o;
    if (index < 0 || index >= fr->n) return 0;
    o = le32(fr->s + 8 + 4 * (size_t)index);
    if (o + 6 > fr->span) return 0;
    *count = (short)le16(fr->s + o);
    return *count >= 0 && o + 6 + 6 * (size_t)*count <= fr->span ? o : 0;
}

int imgSheetFrame(const uint8_t *f, size_t len, int index, ImgFrame *out)
{
    Frames fr;
    size_t o;
    int n;
    if (sheetFrames(f, len, &fr) || !(o = frameAt(&fr, index, &n))) return -1;
    out->id = fr.s[fr.ids + (size_t)index];
    out->parts = n;
    out->row = (short)le16(fr.s + o + 2);
    out->rate = (short)le16(fr.s + o + 4);
    return 0;
}

int imgSheetFramePart(const uint8_t *f, size_t len, int index, int k, int *part, int *x, int *y)
{
    Frames fr;
    const uint8_t *it;
    size_t o;
    int n, pi;
    if (sheetFrames(f, len, &fr) || !(o = frameAt(&fr, index, &n)) || k < 0 || k >= n) return -1;
    it = fr.s + o + 6 + 6 * (size_t)k;
    pi = (short)le16(it);
    if (pi < 0 || pi >= fr.nParts) return -1;
    *part = pi;
    *x = (short)le16(it + 2);
    *y = (short)le16(it + 4);
    return 0;
}

/* ---- the pages of a screen and the palettes on them ---- */

#define PAGE_HALFWORDS 64 /* the width of a page in VRAM (a 256-texel page of 4-bit art) */
#define PAGES_MAX 8

#define ALT_CLUT_ROWS 16 /* rows the alternate CLUT area has: VRAM y 0x1F0 to 0x1FF */

struct ImgPages {
    int n;
    int w[PAGES_MAX], h[PAGES_MAX]; /* halfwords */
    uint8_t *px[PAGES_MAX];         /* w x h halfwords, a copy (the walk's RLEN buffers do not outlive it); NULL: no such page */
    int stage;                      /* a stage MASK file: any TIM's image is a page, and the TIMs' CLUTs fill the alternate CLUT area */
    uint16_t alt[ALT_CLUT_ROWS][256]; /* the alternate CLUT area, rows 0.. (VRAM y 0x1F0 + row) */
    short altOwner[ALT_CLUT_ROWS][256]; /* of each colour of it, the TIM with a CLUT that wrote it last (its place among them, from 0); -1: none */
    int clutTims;                   /* the TIMs with a CLUT that the walk went through */
    int altRows;                    /* rows of it a TIM has written (0: none) */
    int altCols;                    /* the widest CLUT written, in halfwords */
};

static int pagesVisit(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len)
{
    ImgPages *pg = (ImgPages *)ctx;
    int k = chain[depth - 1];
    TimInfo t;
    size_t bytes;
    if (imgTimParse(tim, len, &t)) return 0;
    if (pg->stage && t.clut && t.mode < 2) { /* the loader sends each CLUT to (0, 0x1F0), so a later TIM overwrites an earlier one's rows */
        int r, c;
        for (r = 0; r < t.ch && r < ALT_CLUT_ROWS; r++) {
            for (c = 0; c < t.cw && c < 256; c++) {
                pg->alt[r][c] = (uint16_t)le16(t.clut + 2 * ((size_t)r * (size_t)t.cw + (size_t)c));
                pg->altOwner[r][c] = (short)pg->clutTims;
            }
        }
        pg->clutTims++;
        if (t.ch > pg->altRows) pg->altRows = t.ch < ALT_CLUT_ROWS ? t.ch : ALT_CLUT_ROWS;
        if (t.cw > pg->altCols) pg->altCols = t.cw < 256 ? t.cw : 256;
    }
    if (k < 0 || k >= PAGES_MAX || pg->px[k] || (!pg->stage && t.mode != 2)) return 0;
    bytes = 2 * (size_t)t.iw * (size_t)t.ih;
    pg->px[k] = (uint8_t *)malloc(bytes ? bytes : 1);
    if (!pg->px[k]) return 0;
    memcpy(pg->px[k], t.img, bytes);
    pg->w[k] = t.iw;
    pg->h[k] = t.ih;
    if (k + 1 > pg->n) pg->n = k + 1;
    return 0;
}

static ImgPages *pagesOpen(const uint8_t *file, size_t len, int stage)
{
    ImgPages *pg = (ImgPages *)calloc(1, sizeof *pg);
    if (!pg) return NULL;
    pg->stage = stage;
    memset(pg->altOwner, 0xFF, sizeof pg->altOwner); /* -1 */
    walkOff(file, len, adaptVisit, &(VisitAdapter){pagesVisit, pg});
    if (pg->n == 0) {
        free(pg);
        return NULL;
    }
    return pg;
}

ImgPages *imgPagesOpen(const uint8_t *file, size_t len) { return pagesOpen(file, len, 0); }

ImgPages *imgStagePagesOpen(const uint8_t *file, size_t len) { return pagesOpen(file, len, 1); }

void imgPagesFree(ImgPages *pg)
{
    int k;
    if (!pg) return;
    for (k = 0; k < PAGES_MAX; k++) free(pg->px[k]);
    free(pg);
}

int imgPartPalette(const ImgPages *pg, const ImgPart *part, int row, uint16_t *pal, ImgPalAt *at)
{
    int n = part->mode ? 256 : 16, y = part->clutY + row, i;
    if (part->clutX < 0 || y < 0) return -1;
    if (pg->stage && part->mode) {
        /* A stage object's 8-bit part reads the alternate CLUT area, not the pages. The game sets it with setAltClut(0, FIELD_OBJECTS_CLUT_Y)
           (src/field/field_mode/map_objects.c:12), which the sprite drawer stores as (0, 0x1F0 - 0x100) (sprite_drawer.c:25) and adds the
           part's clutY and the frame's row to (sprite_drawer.c:111), so the CLUT sits at VRAM y 0xF0 + clutY + row. The loader puts each
           TIM's CLUT at (0, FIELD_OBJECTS_CLUT_Y = 0x1F0) (file_loader.c:99-109; tim_loader.c:41-47), so CLUT row r of the archive is at
           y 0x1F0 + r and clutY - 256 + row is that r. A position below that row is not a CLUT row the loader wrote. */
        int r = y - IMG_ALT_CLUT_ROW;
        if (r < 0 || r >= pg->altRows || part->clutX + n > pg->altCols) return -1;
        for (i = 0; i < n; i++) pal[i] = pg->alt[r][part->clutX + i];
        if (at) at->page = at->x = at->y = -1; /* not in a page: not editable in place */
        return 0;
    }
    /* A 4-bit part (a screen's or a stage's) reads the pages: the drawer's CLUT position is the texture position (sprite_drawer.c:18-19),
       VRAM x 0x140 + clutX and y 0x100 + clutY + row, i.e. column clutX of the pages' space and row clutY + row of a page. */
    for (i = 0; i < n; i++) {
        int x = part->clutX + i, k = x / PAGE_HALFWORDS, col = x % PAGE_HALFWORDS;
        if (k >= PAGES_MAX || !pg->px[k] || col >= pg->w[k] || y >= pg->h[k]) return -1;
        pal[i] = (uint16_t)le16(pg->px[k] + 2 * ((size_t)y * (size_t)pg->w[k] + (size_t)col));
    }
    if (at) {
        at->page = part->clutX / PAGE_HALFWORDS;
        at->x = part->clutX % PAGE_HALFWORDS;
        at->y = y;
    }
    return 0;
}

int imgPagesAltOwner(const ImgPages *pg, int r, int c)
{
    return pg->stage && r >= 0 && r < ALT_CLUT_ROWS && c >= 0 && c < 256 ? pg->altOwner[r][c] : -1;
}

int imgPagesCount(const ImgPages *pg) { return pg->n; }

int imgPageSize(const ImgPages *pg, int k, int *w, int *h)
{
    if (k < 0 || k >= PAGES_MAX || !pg->px[k]) return -1;
    *w = pg->w[k];
    *h = pg->h[k];
    return 0;
}

int imgPartPixels(const ImgPages *pg, const ImgPart *part, uint8_t *px, int *outside)
{
    int perHalf = part->mode ? 2 : 4, bits = part->mode ? 8 : 4, x, y, out = 0;
    if (part->w <= 0 || part->h <= 0) return -1;
    for (y = 0; y < part->h; y++) {
        for (x = 0; x < part->w; x++) {
            int u = part->u + x, v = part->v + y, hx = u / perHalf, k = hx / PAGE_HALFWORDS, col = hx % PAGE_HALFWORDS;
            uint8_t *dst = px + (size_t)y * (size_t)part->w + (size_t)x;
            if (u < 0 || v < 0 || k >= PAGES_MAX || !pg->px[k] || col >= pg->w[k] || v >= pg->h[k]) {
                *dst = 0;
                out++;
            } else {
                unsigned half = le16(pg->px[k] + 2 * ((size_t)v * (size_t)pg->w[k] + (size_t)col));
                *dst = (uint8_t)((half >> (bits * (u % perHalf))) & (part->mode ? 255u : 15u));
            }
        }
    }
    if (outside) *outside = out;
    return 0;
}

/* ---- piece maps (atlas-v1) ---- */

typedef struct Sb {
    char *p;
    size_t n, cap;
    int fail;
} Sb;

static void sbPrintf(Sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sbPrintf(Sb *b, const char *fmt, ...)
{
    va_list ap, ap2;
    int k;
    if (b->fail) return;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    k = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (k < 0) {
        b->fail = 1;
    } else if (b->cap - b->n < (size_t)k + 1) {
        size_t nc = b->cap ? b->cap : 1024;
        char *g;
        while (nc - b->n < (size_t)k + 1) nc *= 2;
        g = (char *)realloc(b->p, nc);
        if (!g) {
            b->fail = 1;
        } else {
            b->p = g;
            b->cap = nc;
        }
    }
    if (!b->fail) {
        vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap2);
        b->n += (size_t)k;
    }
    va_end(ap2);
}

typedef struct Piece {
    int mode, part, frame, u, v, w, h, cx, cy, dx, dy;
    int *frames, nFrames;
} Piece;

static int pieceAddFrame(Piece *p, int frame)
{
    int *g;
    if (p->nFrames && p->frames[p->nFrames - 1] == frame) return 0;
    g = (int *)realloc(p->frames, (size_t)(p->nFrames + 1) * sizeof(int));
    if (!g) return -1;
    p->frames = g;
    p->frames[p->nFrames++] = frame;
    return 0;
}

/* Frames and parts of a sprite sheet file (gfx.h: SpritePart and the frame layout), bounds checked */
char *imgAtlasJson(const char *pngName, int w, int h, int bits, int page, const char *sheetName, const uint8_t *sheet, size_t sheetLen,
                   size_t *outLen)
{
    Sb b;
    Parts ps;
    Piece *pc = NULL;
    int nPc = 0, i, k, f, nFrames = 0, first = 1, pmode = bits == 8 ? 1 : 0, pph = 16 / bits, ok = 1;
    const uint8_t *s = NULL;
    size_t span = 0;

    memset(&b, 0, sizeof b);
    if (sheet && sheetParts(sheet, sheetLen, &ps) == 0) {
        uint32_t base = le32(sheet), po;
        s = sheet + base;
        span = sheetLen - base;
        po = le32(s);
        nFrames = (int)((po - 8) / 4);
        for (f = 0; f < nFrames && ok; f++) {
            uint32_t o;
            int c;
            if ((size_t)8 + 4 * (size_t)f + 4 > span) break;
            o = le32(s + 8 + 4 * (size_t)f);
            if ((size_t)o + 6 > span) continue;
            c = (short)le16(s + o);
            for (k = 0; k < c && (size_t)o + 6 + 6 * (size_t)k + 6 <= span; k++) {
                const uint8_t *it = s + o + 6 + 6 * (size_t)k;
                int pi = (short)le16(it), dx = (short)le16(it + 2), dy = (short)le16(it + 4), u, v, pwid, phei, cx, cy, j, x0, x1, pm, tph;
                const uint8_t *pp;
                if (pi < 0 || pi >= ps.n) continue;
                pp = ps.p + 14 * (size_t)pi;
                u = (short)le16(pp);
                v = (short)le16(pp + 2);
                pwid = (short)le16(pp + 4);
                phei = (short)le16(pp + 6);
                cx = (short)le16(pp + 8);
                cy = (short)le16(pp + 10);
                pm = (short)le16(pp + 12) & 1;
                if (u < 0 || pwid <= 0 || phei <= 0) continue;
                tph = pm ? 2 : 4; /* texels per VRAM halfword: the page is 64 halfwords wide in both modes */
                x0 = u / tph - page * 64;
                x1 = (u + pwid + tph - 1) / tph - page * 64;
                if (x1 <= 0 || x0 >= 64) continue; /* on another page */
                for (j = 0; j < nPc; j++) {
                    if (pc[j].mode == pm && pc[j].u == u && pc[j].v == v && pc[j].w == pwid && pc[j].h == phei && pc[j].cx == cx && pc[j].cy == cy) break;
                }
                if (j == nPc) {
                    Piece *g = (Piece *)realloc(pc, (size_t)(nPc + 1) * sizeof *pc);
                    if (!g) {
                        ok = 0;
                        break;
                    }
                    pc = g;
                    memset(&pc[nPc], 0, sizeof pc[nPc]);
                    pc[nPc].mode = pm;
                    pc[nPc].part = pi;
                    pc[nPc].frame = f;
                    pc[nPc].u = u;
                    pc[nPc].v = v;
                    pc[nPc].w = pwid;
                    pc[nPc].h = phei;
                    pc[nPc].cx = cx;
                    pc[nPc].cy = cy;
                    pc[nPc].dx = dx;
                    pc[nPc].dy = dy;
                    nPc++;
                }
                if (pieceAddFrame(&pc[j], f)) ok = 0;
            }
        }
    }
    sbPrintf(&b, "{\n \"format\": \"atlas-v1\",\n \"image\": \"");
    {
        const char *nm = strrchr(pngName, '/');
        sbPrintf(&b, "%s", nm ? nm + 1 : pngName);
    }
    sbPrintf(&b, "\",\n \"width\": %d,\n \"height\": %d,\n \"mode\": \"%dbit\",\n", w, h, bits);
    if (s) sbPrintf(&b, " \"sheet\": \"%s\",\n", sheetName);
    else sbPrintf(&b, " \"sheet\": null,\n");
    sbPrintf(&b, " \"pieces\": [");
    for (i = 0; i < nPc && ok; i++) {
        Piece *p = &pc[i];
        int tph = p->mode ? 2 : 4, hx0 = p->u / tph - page * 64, hx1 = (p->u + p->w + tph - 1) / tph - page * 64;
        int x = hx0 * pph, y = p->v, pwid = (hx1 - hx0) * pph, phei = p->h, outside = 0;
        if (x < 0) {
            pwid += x;
            x = 0;
            outside = 1;
        }
        if (x + pwid > w) {
            pwid = w - x;
            outside = 1;
        }
        if (y < 0) {
            phei += y;
            y = 0;
            outside = 1;
        }
        if (y + phei > h) {
            phei = h - y;
            outside = 1;
        }
        if (pwid <= 0 || phei <= 0) { /* nothing of it is inside this image: the unclipped rectangle, flagged */
            x = hx0 * pph;
            y = p->v;
            pwid = (hx1 - hx0) * pph;
            phei = p->h;
            outside = 1;
        }
        sbPrintf(&b, "%s\n  { \"id\": \"f%dp%d\", \"frame\": %d, \"part\": %d, \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"palette\": \"%d,%d\", \"draw_at\": [%d, %d], \"frames\": [",
                 first ? "" : ",", p->frame, p->part, p->frame, p->part, x, y, pwid, phei, p->cx, p->cy, p->dx, p->dy);
        first = 0;
        for (k = 0; k < p->nFrames; k++) sbPrintf(&b, "%s%d", k ? ", " : "", p->frames[k]);
        sbPrintf(&b, "]%s%s%s }", p->mode != pmode ? ", \"read_as\": \"" : "", p->mode != pmode ? (p->mode ? "8bit\"" : "4bit\"") : "",
                 outside ? ", \"outside\": true" : "");
    }
    sbPrintf(&b, "%s],\n", nPc ? "\n " : "");
    sbPrintf(&b, " \"notes\": \"x, y, w, h are pixels of the PNG next to this file (palette indices, not colors). palette is \\\"column,row\\\" of the "
                 "piece's palette inside its page (halfwords, rows); a frame may add a row. draw_at is where the first frame in frames draws the piece "
                 "(relative to the sprite origin); frames lists every frame that uses it. outside: true means the piece is clipped by, or lies off, this "
                 "image. %s Edit the PNG keeping its width and height and at most %d palette colors; this file is information only and is regenerated "
                 "from the disc.\"\n}\n",
             s ? "Pieces come from the sprite sheet named above (read_as marks a piece the game reads in the other texture mode: its rectangle is converted to this PNG's pixels, rounded outward)."
               : "No sprite sheet describes this image, so no pieces are listed.",
             bits == 8 ? 256 : 16);
    for (i = 0; i < nPc; i++) free(pc[i].frames);
    free(pc);
    if (b.fail || !ok) {
        free(b.p);
        return NULL;
    }
    if (outLen) *outLen = b.n;
    return b.p;
}

/* ---- names ---- */


static int endsWith(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcmp(s + a - b, suf);
}

static int isCardData(const char *p)
{
    return !strcmp(p, DISC_PREFIX "CARD/CARDDATA.BIN");
}

int imgIsScreenFile(const char *p)
{
    static const char *const dirs[] = {"SCREEN", "COUNTRY", "NAMEENT", "MCARD", "DGLB", "TRAINING"};
    const char *name, *slash;
    size_t dl, i;
    int ok = 0;
    if (strncmp(p, DISC_PREFIX, 8)) return 0;
    p += 8;
    slash = strchr(p, '/');
    if (!slash || strchr(slash + 1, '/')) return 0;
    dl = (size_t)(slash - p);
    for (i = 0; i < sizeof dirs / sizeof *dirs; i++) {
        if (strlen(dirs[i]) == dl && !strncmp(dirs[i], p, dl)) ok = 1;
    }
    name = slash + 1;
    return ok && endsWith(name, ".BIN") &&
           (endsWith(name, "TM.BIN") || !strncmp(name, "TLOGO", 5) || !strcmp(name, "TTCOMMPK.BIN") ||
            !strcmp(name, "END__ALL.BIN") || !strcmp(name, "CNTSELWN.BIN"));
}

/* The raw 8-bit pages of the world map screens: the pieces of their sheet have no PNG (menu_export.h says why), so images/ is the only way to edit them */
static int isRawScreen(const char *p)
{
    return endsWith(p, "/STWMP0TM.BIN") || endsWith(p, "/STWMP1TM.BIN");
}

int imgIsMenuScreen(const char *p)
{
    return imgIsScreenFile(p) && endsWith(p, "TM.BIN") && !isRawScreen(p);
}

int imgImageName(const char *discPath, const int *chain, int depth, char *out, size_t cap)
{
    size_t n;
    int i;
    if (strncmp(discPath, DISC_PREFIX, 8) || !endsWith(discPath, ".BIN") || depth < 1) return -1;
    n = strlen(discPath) - 8 - 4;
    if (n + 1 + (size_t)depth * 3 + 4 >= cap) return -1;
    for (i = 0; (size_t)i < n; i++) out[i] = (char)tolower((unsigned char)discPath[8 + i]); /* ADR-010: the port writes lower case */
    out[n++] = '/';
    for (i = 0; i < depth; i++) {
        if (chain[i] < 0 || chain[i] > 99) return -1;
        n += (size_t)snprintf(out + n, cap - n, "%s%02d", i ? "_" : "", chain[i]);
    }
    snprintf(out + n, cap - n, ".png");
    return 0;
}

int imgSheetPathOf(const char *discPath, char *out, size_t cap)
{
    size_t n = strlen(discPath);
    if (imgIsStageMask(discPath) && n < cap) { /* the stage's sprite sheet is entry 0 of its TMPK file */
        memcpy(out, discPath, n + 1);
        memcpy(out + n - 8, "TMPK", 4);
        return 1;
    }
    if (!endsWith(discPath, "TM.BIN") || n >= cap) return 0;
    memcpy(out, discPath, n + 1);
    out[n - 6] = 'C';
    out[n - 5] = 'S';
    return 1;
}

/* ---- run: manifest and files ---- */

static double nowSeconds(void)
{
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

int imgRunOpen(ImgRun *run, const char *dataDir, const char *disc, unsigned version)
{
    char path[1200], dec[16];

    memset(run, 0, sizeof *run);
    if (strlen(dataDir) + 8 >= sizeof run->dir || strlen(disc) >= sizeof run->next.disc) return -1;
    snprintf(run->dir, sizeof run->dir, "%s/images", dataDir);
    expManInit(&run->next, "images");
    snprintf(run->next.disc, sizeof run->next.disc, "%s", disc);
    snprintf(dec, sizeof dec, "%u", version);
    tdKvSet(&run->next.exporter, "images", dec);
    snprintf(path, sizeof path, "%s/manifest.json", run->dir);
    if (expManRead("images", path, &run->old)) run->sameRun = tdManifestSameRun(&run->old, &run->next);
    return 0;
}

int imgRunCurrent(const ImgRun *run)
{
    int i;
    char path[1400];
    if (!run->sameRun || run->old.count == 0) return 0;
    for (i = 0; i < run->old.count; i++) {
        snprintf(path, sizeof path, "%s/%s", run->dir, run->old.files[i].path);
        if (!expFileExists(path)) return 0;
    }
    return 1;
}

/* the images/card/cardpak<A>/ faces of before A7.4: cards/ holds them now, so the manifest stops listing them */
static int oldCardPath(const char *path)
{
    return !strncmp(path, "card/cardpak", 12) || !strncmp(path, "field/sprt/", 11); /* and the field sprites of before A7.5: sprites/ holds them now */
}

int imgRunClose(ImgRun *run)
{
    int rc = 0;
    char path[1200];
    expManCarry(&run->next, &run->old);
    tdManifestDrop(&run->next, oldCardPath);
    snprintf(path, sizeof path, "%s/manifest.json", run->dir);
    if (expManWrite("images", path, &run->next)) rc = -1;
    tdManifestFree(&run->old);
    tdManifestFree(&run->next);
    return rc;
}

void imgEmit(ImgRun *run, const char *rel, const uint8_t *png, size_t len)
{
    ExpSink sink;
    ExpStats st;

    memset(&st, 0, sizeof st);
    sink.tag = "images";
    sink.dir = run->dir;
    sink.old = &run->old;
    sink.next = &run->next;
    sink.dirty = NULL;
    sink.sameRun = run->sameRun;
    sink.stats = &st;
    if (expPut(&sink, rel, png, len)) run->failed++;
    run->written += st.written;
    run->kept += st.kept;
    run->newFiles += st.newFiles;
}

/* ---- extraction ---- */

/* What reading an image as indices needs from its neighbours (the sheet, the 8-bit TIM before it), kept
   while an archive is walked in order: shared by the extraction and the substitution */
typedef struct HintCtx {
    const uint8_t *sheet;
    size_t sheetLen;
    int sheetMode;           /* -1: no sheet */
    const uint8_t *file;     /* the file itself (a screen's palettes may sit on another page) */
    size_t fileLen;
    char sheetName[96];      /* "SCREEN/STSHOPCS.BIN", or the file's own name when entry 0 is the sheet */
    uint16_t pal8[256];      /* the last 8-bit TIM's palette, for the raw pages after it */
    int havePal8;
    int parent[IMG_MAX_DEPTH];
    int parentDepth;
    int screenSpace;         /* a screen's TM file: its pages share one VRAM space, a part's CLUT position falls on whichever page holds it */
    ImgPages *pages;         /* the file's pages, opened by the first palette that needs them (hintPages) */
    int pagesTried;
} HintCtx;

static void hintInit(HintCtx *hc, const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet, size_t sheetLen)
{
    int mode;
    memset(hc, 0, sizeof *hc);
    int stage = imgIsStageMask(discPath) || imgIsStageBg(discPath);
    if (imgIsStageMask(discPath)) sheet = NULL; /* its pages are edited by piece (stage_obj_pack.h): no PNG, so no hints either */
    if (!sheet && !endsWith(discPath, "TM.BIN") && !imgIsCardFile(discPath) && !isCardData(discPath) && !stage) { /* not a TM file: entry 0 is its own sheet */
        sheet = file;
        sheetLen = len;
    }
    hc->sheet = sheet;
    hc->sheetLen = sheetLen;
    hc->sheetMode = sheet && imgSheetMode(sheet, sheetLen, &mode) == 0 ? mode : -1;
    if (imgIsStageBg(discPath)) hc->sheetMode = 1; /* no sheet: the raw pages follow the archive's 8-bit TIM, like its CLUT (file_loader/wstag loaders) */
    hc->screenSpace = endsWith(discPath, "TM.BIN");
    hc->file = file;
    hc->fileLen = len;
    if (!strncmp(discPath, DISC_PREFIX, 8)) {
        char sp[96];
        if (hc->sheet == file) snprintf(hc->sheetName, sizeof hc->sheetName, "%s (entry 0)", discPath + 8);
        else if (imgSheetPathOf(discPath, sp, sizeof sp)) snprintf(hc->sheetName, sizeof hc->sheetName, "%s", sp + 8);
    }
}

static void hintFree(HintCtx *hc)
{
    imgPagesFree(hc->pages);
    hc->pages = NULL;
}

static const ImgPages *hintPages(HintCtx *hc)
{
    if (!hc->pagesTried) {
        hc->pagesTried = 1;
        hc->pages = imgPagesOpen(hc->file, hc->fileLen);
    }
    return hc->pages;
}

/* The hints of the image at `chain` (walk order). 0, or -1 when `tim` is not a TIM. */
static int hintsFor(HintCtx *hc, const int *chain, int depth, const uint8_t *tim, size_t len, ImgHints *h, uint16_t pal4[16])
{
    TimInfo t;
    int i, sameParent;

    if (imgTimParse(tim, len, &t)) return -1;
    sameParent = hc->parentDepth == depth - 1 && !memcmp(hc->parent, chain, sizeof(int) * (size_t)(depth - 1));
    memset(h, 0, sizeof *h);
    h->pixelBits = 4;
    if (t.mode == 1 && t.clut) {
        for (i = 0; i < 256; i++) hc->pal8[i] = i < t.cw ? (uint16_t)le16(t.clut + 2 * i) : 0;
        hc->havePal8 = 1;
        memcpy(hc->parent, chain, sizeof(int) * (size_t)(depth - 1));
        hc->parentDepth = depth - 1;
    } else if (t.mode == 2) {
        if (hc->sheetMode == 1) {
            h->pixelBits = 8;
            if (hc->havePal8 && sameParent) {
                h->pal = hc->pal8;
                h->nPal = 256;
            }
        } else if (hc->sheetMode == 0) {
            ImgPart part = {0};
            if (imgSheetClut(hc->sheet, hc->sheetLen, chain[depth - 1], 0, &part.clutX, &part.clutY) == 0) {
                if (hc->screenSpace) {
                    /* the palette most parts of this page use, read where its position falls (it may be on another page) */
                    ImgPalAt at;
                    const ImgPages *pages = hintPages(hc);
                    if (pages && imgPartPalette(pages, &part, 0, pal4, &at) == 0) {
                        h->pal = pal4;
                        h->nPal = 16;
                        if (at.page == chain[depth - 1]) { /* editable: the 16 halfwords sit in this very page */
                            h->palAt = 1;
                            h->palX = at.x;
                            h->palY = at.y;
                        }
                    }
                } else if (part.clutY >= 0 && part.clutY < t.ih && part.clutX >= 0 && part.clutX + 16 <= t.iw) {
                    /* an archive of pictures (card data, field common, logos): the palette is in the picture itself */
                    for (i = 0; i < 16; i++) pal4[i] = (uint16_t)le16(t.img + 2 * ((size_t)part.clutY * (size_t)t.iw + (size_t)part.clutX + (size_t)i));
                    h->pal = pal4;
                    h->nPal = 16;
                    h->palAt = 1;
                    h->palX = part.clutX;
                    h->palY = part.clutY;
                }
            }
        }
    }
    return 0;
}

/* FIELDCOM.BIN: image 02_00 is described by the sheet in entry 0, 03_00 by entry 1 */
typedef struct FcCtx {
    const uint8_t *file; /* the disc file, for FIELDCOM's per-image sheets */
    size_t len;
    int perEntry;
    int cardData;        /* CARDDATA.BIN: the textures in entries 0 and 1 are described by the sheets in entries 2 and 3 */
    const char *label;   /* the file's name for the sheet's label ("FIELD/FIELDCOM.BIN") */
    uint8_t *wrap;       /* the current sheet, wrapped as a sheet file */
} FcCtx;

typedef struct Ex {
    ImgRun *run;
    const char *discPath;
    HintCtx hc;
    FcCtx fc;
} Ex;

/* FIELDCOM.BIN: entries 0 and 1 are the sheets of the images in entries 2 and 3 (file_loader.c:73-76), each a
   sheet that starts at its own first word; wrap it like a sheet file ([4] then the sheet) */
static void pairSheet(HintCtx *hc, FcCtx *fc, int k)
{
    size_t o, end;
    int n;
    if (fc->len < 4) return;
    n = (int)(le32(fc->file) / 4);
    if (k < 0 || k >= n) return;
    o = le32(fc->file + 4 * (size_t)k);
    end = k + 1 < n ? le32(fc->file + 4 * (size_t)(k + 1)) : fc->len;
    if (o >= end || end > fc->len) return;
    free(fc->wrap);
    fc->wrap = (uint8_t *)malloc(end - o + 4);
    if (!fc->wrap) return;
    fc->wrap[0] = 4;
    fc->wrap[1] = fc->wrap[2] = fc->wrap[3] = 0;
    memcpy(fc->wrap + 4, fc->file + o, end - o);
    hc->sheet = fc->wrap;
    hc->sheetLen = end - o + 4;
    {
        int mode;
        hc->sheetMode = imgSheetMode(hc->sheet, hc->sheetLen, &mode) == 0 ? mode : -1;
    }
    snprintf(hc->sheetName, sizeof hc->sheetName, "%s (entry %d)", fc->label, k);
}

static int onImage(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len)
{
    Ex *ex = (Ex *)ctx;
    ImgHints h;
    uint16_t pal4[16];
    ImgPic pic;
    int card = imgIsStageBg(ex->discPath); /* these pictures have no sprite sheet to map */
    uint8_t *png;
    size_t pl;
    char rel[128];

    if (ex->fc.perEntry && (chain[0] == 2 || chain[0] == 3)) pairSheet(&ex->hc, &ex->fc, chain[0] - 2);
    if (ex->fc.cardData && chain[0] < 2) pairSheet(&ex->hc, &ex->fc, chain[0] + 2);
    if (hintsFor(&ex->hc, chain, depth, tim, len, &h, pal4)) return 0;
    if (imgDecodeTim(tim, len, &h, &pic) || imgImageName(ex->discPath, chain, depth, rel, sizeof rel) || imgPicToPng(&pic, &png, &pl)) {
        plogLine("images", "cannot convert an image of %s\n", ex->discPath);
        ex->run->failed++;
        imgPicFree(&pic);
        return 0;
    }
    if (pic.guessed) ex->run->guessed++;
    imgEmit(ex->run, rel, png, pl);
    free(png);
    if (!card) {
        size_t jl;
        char jrel[132];
        char *json = imgAtlasJson(rel, pic.w, pic.h, pic.depth, chain[depth - 1], ex->hc.sheetName,
                                  ex->hc.sheetMode >= 0 ? ex->hc.sheet : NULL, ex->hc.sheetLen, &jl);
        snprintf(jrel, sizeof jrel, "%.*s.json", (int)(strlen(rel) - 4), rel);
        if (json) {
            imgEmit(ex->run, jrel, (const uint8_t *)json, jl);
            free(json);
        } else {
            plogLine("images", "cannot write the piece map of %s\n", rel);
            ex->run->failed++;
        }
    }
    imgPicFree(&pic);
    return 0;
}

typedef struct CardFaceCtx {
    HintCtx hc;
    const char *discPath;
    ImgCardFn cb;
    void *ud;
    int failed;
} CardFaceCtx;

static int onCardFace(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len, size_t off)
{
    CardFaceCtx *c = (CardFaceCtx *)ctx;
    ImgHints h;
    uint16_t pal4[16];
    ImgPic pic;
    uint8_t *png;
    size_t pl;
    int id = imgCardId(c->discPath, chain[0]);

    (void)off;
    if (hintsFor(&c->hc, chain, depth, tim, len, &h, pal4)) return 0;
    if (id == 0 || imgDecodeTim(tim, len, &h, &pic)) {
        c->failed++;
        return 0;
    }
    if (imgPicToPng(&pic, &png, &pl)) {
        imgPicFree(&pic);
        c->failed++;
        return 0;
    }
    imgPicFree(&pic);
    if (c->cb(c->ud, id, png, pl)) c->failed++;
    free(png);
    return 0;
}

int imgCardFaces(const char *discPath, const uint8_t *file, size_t len, ImgCardFn cb, void *ud)
{
    CardFaceCtx c;
    int n;

    if (!imgIsCardFile(discPath)) return -1;
    memset(&c, 0, sizeof c);
    c.discPath = discPath;
    c.cb = cb;
    c.ud = ud;
    hintInit(&c.hc, discPath, file, len, NULL, 0);
    n = walkCards(file, len, onCardFace, &c);
    hintFree(&c.hc);
    return c.failed ? -1 : n;
}

static int onImageOff(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len, size_t off)
{
    (void)off;
    return onImage(ctx, chain, depth, tim, len);
}

int imgExtractFile(ImgRun *run, const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet, size_t sheetLen)
{
    Ex ex;
    int n;

    if (imgIsCardFile(discPath)) return 0; /* the faces are cards/c<nnn>/face.png (card_export.c), not images/ (A7.4) */
    memset(&ex, 0, sizeof ex);
    ex.run = run;
    ex.discPath = discPath;
    hintInit(&ex.hc, discPath, file, len, sheet, sheetLen);
    ex.fc.file = file;
    ex.fc.len = len;
    ex.fc.perEntry = !strcmp(discPath, DISC_PREFIX "FIELD/FIELDCOM.BIN");
    ex.fc.label = discPath + (strncmp(discPath, DISC_PREFIX, 8) ? 0 : 8);
    ex.fc.cardData = isCardData(discPath); /* entries 2 and 3 are the sprite sheets of the pictures */
    n = walkFile(discPath, file, len, onImageOff, &ex);
    free(ex.fc.wrap);
    hintFree(&ex.hc);
    run->images += n;
    return n;
}

/* ---- substitution ---- */

static void setWhy(char *why, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void setWhy(char *why, size_t size, const char *fmt, ...)
{
    va_list ap;
    if (!why || !size) return;
    va_start(ap, fmt);
    vsnprintf(why, size, fmt, ap);
    va_end(ap);
}

uint16_t imgColor16(const uint8_t *rgb, int alpha, uint16_t orig)
{
    unsigned c;
    if (alpha == 0) return 0;
    c = (unsigned)(rgb[0] >> 3) | (unsigned)(rgb[1] >> 3) << 5 | (unsigned)(rgb[2] >> 3) << 10;
    if (c == 0) return 0x8000;
    return (uint16_t)(c | (orig & 0x8000u));
}

int imgSubstitute(const uint8_t *tim, size_t len, const ImgHints *hints, const uint8_t *png, size_t pngLen, uint8_t **out, char *why,
                  size_t whySize)
{
    PngImage pi;
    TimInfo t;
    uint8_t *c;
    int bits, n, w, x, y, i, maxIdx = 0;
    uint8_t *palBase = NULL;
    int palLim = 0;

    *out = NULL;
    if (pngReadIndexed(png, pngLen, &pi, why, whySize)) return -1;
    if (imgTimParse(tim, len, &t)) {
        pngImageFree(&pi);
        setWhy(why, whySize, "the original image is not a readable TIM");
        return -1;
    }
    bits = t.mode == 0 ? 4 : t.mode == 1 ? 8 : (hints && hints->pixelBits == 8 ? 8 : 4);
    n = bits == 4 ? 16 : 256;
    w = t.iw * (16 / bits);
    if (pi.w != w || pi.h != t.ih) {
        setWhy(why, whySize, "size %dx%d, the original is %dx%d", pi.w, pi.h, w, t.ih);
        pngImageFree(&pi);
        return -1;
    }
    if (pi.nPal > n) {
        setWhy(why, whySize, "%d colors, the original has at most %d (%d-bit)", pi.nPal, n, bits);
        pngImageFree(&pi);
        return -1;
    }
    for (i = 0; i < w * t.ih; i++) {
        if (pi.px[i] > maxIdx) maxIdx = pi.px[i];
    }
    if (maxIdx >= n) {
        setWhy(why, whySize, "pixel index %d out of range, the original has %d colors", maxIdx, n);
        pngImageFree(&pi);
        return -1;
    }
    c = (uint8_t *)malloc(len);
    if (!c) {
        pngImageFree(&pi);
        setWhy(why, whySize, "out of memory");
        return -1;
    }
    memcpy(c, tim, len);
    for (y = 0; y < t.ih; y++) {
        for (x = 0; x < w; x++) {
            uint8_t *hw = c + (size_t)(t.img - tim) + 2 * ((size_t)y * (size_t)t.iw + (size_t)(x / (16 / bits)));
            unsigned v = le16(hw), px = pi.px[(size_t)y * (size_t)w + (size_t)x];
            if (bits == 4) {
                int sh = (x & 3) * 4;
                v = (v & ~(15u << sh)) | px << sh;
            } else {
                int sh = (x & 1) * 8;
                v = (v & ~(255u << sh)) | px << sh;
            }
            hw[0] = (uint8_t)v;
            hw[1] = (uint8_t)(v >> 8);
        }
    }
    if (t.mode < 2 && t.clut) {
        palBase = c + (t.clut - tim);
        palLim = t.cw < n ? t.cw : n;
    } else if (t.mode == 2 && bits == 4 && hints && hints->palAt && hints->palX >= 0 && hints->palY >= 0 && hints->palX + 16 <= t.iw &&
               hints->palY < t.ih) {
        palBase = c + (size_t)(t.img - tim) + 2 * ((size_t)hints->palY * (size_t)t.iw + (size_t)hints->palX);
        palLim = 16;
    }
    for (i = 0; palBase && i < pi.nPal && i < palLim; i++) {
        unsigned v = imgColor16(pi.rgb + 3 * i, i < pi.nAlpha ? pi.alpha[i] : 255, (uint16_t)le16(palBase + 2 * i));
        palBase[2 * i] = (uint8_t)v;
        palBase[2 * i + 1] = (uint8_t)(v >> 8);
    }
    pngImageFree(&pi);
    *out = c;
    return 0;
}

/* ---- the overrides of one file ---- */

/* ---- the source index ----
   Which PNGs each source (every active mod's images/ folder and data/images) holds, read ONCE: one directory walk per source,
   kept as a sorted table of lower-case names with the real spelling beside them. Every later question (does this source have
   that image, and under which spelling) is a table lookup: no stat, no directory scan while the game runs. The game reads the
   mods at startup only; a file added or removed later is seen after a restart (imgOverridesRescan is for a modder's opt-in hot
   reload, mods.h modsHotReload, and for the tests). The matching ignores case, so a mod written with the disc's upper-case
   names on Windows still works (ADR-010 section 3). */
typedef struct SrcEntry {
    char *key;  /* lower case */
    char *real; /* as the folder spells it, relative to <root>/images */
} SrcEntry;

typedef struct SrcIndex {
    char root[1100];
    SrcEntry *e;
    int n, cap;
} SrcIndex;

static SrcIndex *gIdx;
static int gNIdx;

static int isPngName(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && n[l - 4] == '.' && tolower((unsigned char)n[l - 3]) == 'p' && tolower((unsigned char)n[l - 2]) == 'n' &&
           tolower((unsigned char)n[l - 1]) == 'g';
}

static void lowerCopy(char *dst, size_t cap, const char *src)
{
    size_t i;
    for (i = 0; src[i] && i + 1 < cap; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

static void idxAdd(SrcIndex *x, const char *rel)
{
    char low[400];
    if (x->n == x->cap) {
        int nc = x->cap ? x->cap * 2 : 256;
        SrcEntry *ne = (SrcEntry *)realloc(x->e, (size_t)nc * sizeof *ne);
        if (!ne) return;
        x->e = ne;
        x->cap = nc;
    }
    lowerCopy(low, sizeof low, rel);
    x->e[x->n].key = strdup(low);
    x->e[x->n].real = strdup(rel);
    if (x->e[x->n].key && x->e[x->n].real) x->n++;
}

static void idxWalk(SrcIndex *x, const char *dir, const char *rel, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char sub[1500], r[400];
        if (e->d_name[0] == '.') continue;
        snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        if (isPngName(e->d_name)) {
            idxAdd(x, r);
        } else if (depth < 6) { /* a folder (the entry type is not always known: the open tells) */
            snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
            idxWalk(x, sub, r, depth + 1);
        }
    }
    closedir(d);
}

static int idxCmp(const void *a, const void *b)
{
    return strcmp(((const SrcEntry *)a)->key, ((const SrcEntry *)b)->key);
}

static SrcIndex *idxFor(const char *root)
{
    int i;
    SrcIndex *x;
    char dir[1500], real[1400];
    struct stat st;

    for (i = 0; i < gNIdx; i++) {
        if (strcmp(gIdx[i].root, root) == 0) return &gIdx[i];
    }
    if (strlen(root) >= sizeof gIdx[0].root) return NULL;
    x = (SrcIndex *)realloc(gIdx, (size_t)(gNIdx + 1) * sizeof *gIdx);
    if (!x) return NULL;
    gIdx = x;
    x = &gIdx[gNIdx++];
    memset(x, 0, sizeof *x);
    snprintf(x->root, sizeof x->root, "%s", root);
    snprintf(dir, sizeof dir, "%s/images", root);
    if (stat(dir, &st) != 0 && portPathsResolveCI(root, "images", real, sizeof real)) snprintf(dir, sizeof dir, "%s", real);
    idxWalk(x, dir, "", 0);
    if (x->n > 1) qsort(x->e, (size_t)x->n, sizeof *x->e, idxCmp);
    return x;
}

void imgOverridesRescan(void)
{
    int i, j;
    for (i = 0; i < gNIdx; i++) {
        for (j = 0; j < gIdx[i].n; j++) {
            free(gIdx[i].e[j].key);
            free(gIdx[i].e[j].real);
        }
        free(gIdx[i].e);
    }
    free(gIdx);
    gIdx = NULL;
    gNIdx = 0;
}

/* <root>/images/<rel> under the spelling the folder has; 0 (and the plain path) when the source has no such image */
static int imagePath(const char *root, const char *rel, char *path, size_t cap)
{
    SrcIndex *x = idxFor(root);
    SrcEntry key, *hit;
    char low[400];

    snprintf(path, cap, "%s/images/%s", root, rel);
    if (!x || x->n == 0) return 0;
    lowerCopy(low, sizeof low, rel);
    key.key = low;
    key.real = NULL;
    hit = (SrcEntry *)bsearch(&key, x->e, (size_t)x->n, sizeof *x->e, idxCmp);
    if (!hit) return 0;
    snprintf(path, cap, "%s/images/%s", root, hit->real);
    return 1;
}

static ImgPartnerHooks gPartner;

void imgSetPartnerHooks(const ImgPartnerHooks *hooks)
{
    if (hooks) gPartner = *hooks;
    else memset(&gPartner, 0, sizeof gPartner);
}

const ImgPartnerHooks *imgGetPartnerHooks(void)
{
    return &gPartner;
}

static ImgMenuHooks gMenu, gStage;

void imgSetMenuHooks(const ImgMenuHooks *hooks)
{
    if (hooks) gMenu = *hooks;
    else memset(&gMenu, 0, sizeof gMenu);
}

const ImgMenuHooks *imgGetMenuHooks(void)
{
    return &gMenu;
}

void imgSetStageHooks(const ImgMenuHooks *hooks)
{
    if (hooks) gStage = *hooks;
    else memset(&gStage, 0, sizeof gStage);
}

const ImgMenuHooks *imgGetStageHooks(void)
{
    return &gStage;
}

/* Layer s of an image (mods.h asset layers: the mods from the last to the first, then data/images): 1 when the layer has it (path is
   where), 0 when not; label names the layer either way. The layers keep a case-insensitive index of their images folder (idxFor).
   The follower sprites of the eight original partners are not under images/ any more: their frames come from the partner bundle (A4a), and the hook has
   already judged whether a data-folder file was edited (`bundle` says so, see sourceIsBundle). */
static int sourcePath(int s, const char *rel, char *path, size_t cap, char *label, size_t lcap)
{
    AssetLayer l;
    int partner = sprPartnerOf(rel);

    if (!assetLayerAt(s, &l)) return 0;
    snprintf(label, lcap, "%s", l.mod < 0 ? "data (edited)" : l.label);
    if (!strncmp(rel, CARD_REL, sizeof CARD_REL - 1)) return assetLayerFile(&l, "cards", rel + sizeof CARD_REL - 1, path, cap); /* a card's face is not under images/ */
    if (partner >= 0) {
        const char *tail = strrchr(rel, '/');

        return gPartner.fieldFile != NULL && tail != NULL && gPartner.fieldFile(partner, &l, tail + 1, path, cap);
    }
    if (!strncmp(rel, SPR_REL, sizeof SPR_REL - 1)) return assetLayerFile(&l, "sprites", rel + sizeof SPR_REL - 1, path, cap); /* nor is a field sprite's frame */
    return imagePath(l.root, rel, path, cap);
}

static int layerIsData(int s)
{
    AssetLayer l;
    return assetLayerAt(s, &l) && l.mod < 0;
}

typedef struct BuildCtx {
    HintCtx hc;
    ImgOverrides *o;
    const char *discPath;
    const char *dataDir;
    TdManifest man;
    FcCtx fc;
    int failed;
    const uint8_t *sheet; /* the file's sprite sheet (partner hooks), may be NULL */
    size_t sheetLen;
    int partnerFile;      /* the partner hook wants this file's pages */
    int menuFile;         /* the menu hook wants this file's pages (A5.3) */
    int stageFile;        /* the stage hook wants this MASK file's pages (A6.3) */
    int cardFile;         /* a CARDPAK file: its faces are cards/c<nnn>/face.png, read through the cards manifest */
    int piecesOnly;       /* the file is a screen or a stage's MASK, edited by pieces (imgIsMenuScreen, imgIsStageMask): no PNG of images/ is read for it (A5.5, A6.4) */
} BuildCtx;

static int onBuild(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len, size_t off)
{
    BuildCtx *b = (BuildCtx *)ctx;
    ImgOverrides *o = b->o;
    ImgOverride *v;
    ImgHints h;
    uint16_t pal4[16];
    int s;
    char path[1400], label[100];

    if (b->fc.perEntry && (chain[0] == 2 || chain[0] == 3)) pairSheet(&b->hc, &b->fc, chain[0] - 2);
    if (b->fc.cardData && chain[0] < 2) pairSheet(&b->hc, &b->fc, chain[0] + 2);
    if (hintsFor(&b->hc, chain, depth, tim, len, &h, pal4)) return 0;
    v = (ImgOverride *)realloc(o->v, (size_t)(o->n + 1) * sizeof *o->v);
    if (!v) {
        b->failed = 1;
        return 1;
    }
    o->v = v;
    v += o->n++;
    memset(v, 0, sizeof *v);
    memcpy(v->chain, chain, sizeof(int) * (size_t)depth);
    v->depth = depth;
    v->off = off;
    v->resolved = 1;
    v->origLen = len;
    /* a page of such a file has no images/ name: the entry is only the carrier of what the hooks write into it, and imgOverridesMix has no PNG to watch */
    if (b->cardFile) {
        char folder[8];
        int id = imgCardId(b->discPath, chain[0]);

        if (id == 0 || imgCardFolder(id, folder, sizeof folder) || snprintf(v->rel, sizeof v->rel, CARD_REL "%s/face.png", folder) >= (int)sizeof v->rel) {
            v->rel[0] = 0;
            return 0;
        }
    } else if (b->piecesOnly || imgImageName(b->discPath, chain, depth, v->rel, sizeof v->rel)) {
        v->rel[0] = 0;
        if (!b->piecesOnly) return 0;
    }
    for (s = 0; s < assetLayerCount() && !v->tim && v->rel[0]; s++) {
        uint8_t *png, *res;
        size_t pl;
        char why[200];
        if (!sourcePath(s, v->rel, path, sizeof path, label, sizeof label) || tdReadFile(path, &png, &pl)) continue;
        if (layerIsData(s) && sprPartnerOf(v->rel) < 0) { /* data/images: only a file the user changed counts, not the extractor's own */
            char hex[17];
            tdHashHex(tdHash(png, pl), hex);
            if (tdFileState(1, tdManifestFind(&b->man, b->cardFile ? v->rel + sizeof CARD_REL - 1 : v->rel), hex) != TD_FILE_EDITED) {
                free(png);
                continue;
            }
        }
        if (imgSubstitute(tim, len, &h, png, pl, &res, why, sizeof why) == 0) {
            v->tim = res;
            v->len = len;
            o->applied++;
            plogLine("images", "override %s from %s\n", v->rel, label);
        } else {
            plogLine("images", "%s rejected: %s\n", path, why);
        }
        free(png);
    }
    if ((b->partnerFile && gPartner.patch != NULL) || (b->menuFile && gMenu.patch != NULL) || (b->stageFile && gStage.patch != NULL)) {
        /* the partner art, the menu pieces and the stage pieces rewrite the page in place, in this order, over a PNG's result if there is one (only the partner art has PNGs left) */
        uint8_t *w = v->tim;
        int own = 0, changed = 0;

        if (w == NULL && (w = (uint8_t *)malloc(len)) != NULL) {
            memcpy(w, tim, len);
            own = 1;
        }
        if (w != NULL && b->partnerFile && gPartner.patch != NULL && gPartner.patch(b->discPath, chain, depth, w, len, b->sheet, b->sheetLen)) changed = 1;
        if (w != NULL && b->menuFile && gMenu.patch != NULL && gMenu.patch(b->discPath, chain, depth, w, len)) {
            changed = 1;
        }
        if (w != NULL && b->stageFile && gStage.patch != NULL && gStage.patch(b->discPath, chain, depth, w, len)) {
            changed = 1;
        }
        if (changed && own) {
            v->tim = w;
            v->len = len;
            o->applied++;
        } else if (own) {
            free(w);
        }
    }
    return 0;
}

/* ---- lookup by offset, lazy sprite frames ---- */

typedef struct IdxEnt {
    size_t off;
    int i;
} IdxEnt;

static int cmpIdx(const void *a, const void *b)
{
    const IdxEnt *x = (const IdxEnt *)a, *y = (const IdxEnt *)b;
    return x->off < y->off ? -1 : x->off > y->off ? 1 : x->i - y->i;
}

/* The entries sorted by offset: imgOverrideGet finds one in O(log n) (a file has a few hundred at most) */
static void indexOverrides(ImgOverrides *o)
{
    IdxEnt *ix;
    int i;
    free(o->idx);
    o->idx = NULL;
    if (o->n <= 0) return;
    ix = (IdxEnt *)malloc((size_t)o->n * sizeof *ix);
    if (!ix) return;
    for (i = 0; i < o->n; i++) {
        ix[i].off = o->v[i].off;
        ix[i].i = i;
    }
    qsort(ix, (size_t)o->n, sizeof *ix, cmpIdx);
    o->idx = ix;
}

struct ImgLazy {
    const uint8_t *file;
    size_t len;
    char dataDir[1100];
};

/* The manifest of data/sprites, parsed once and shared by every sprite file (it lists the frames of all of them); it is read
   again when its mtime or size changes */
static TdManifest gSharedMan;
static int gSharedManOk;
static char gSharedManDir[1100];
static time_t gSharedManMtime;
static long gSharedManSize;

static const TdManifest *sharedManifest(const char *dataDir)
{
    char path[1200];
    struct stat st;
    if (!dataDir || !dataDir[0]) return NULL;
    if (gSharedManOk && !modsHotReload() && strcmp(gSharedManDir, dataDir) == 0) return &gSharedMan; /* read once */
    snprintf(path, sizeof path, "%s/sprites/manifest.json", dataDir);
    if (stat(path, &st) != 0) return NULL;
    if (!gSharedManOk || strcmp(gSharedManDir, dataDir) || gSharedManMtime != st.st_mtime || gSharedManSize != (long)st.st_size) {
        uint8_t *txt;
        size_t tl;
        char err[120];
        if (gSharedManOk) tdManifestFree(&gSharedMan);
        gSharedManOk = 0;
        memset(&gSharedMan, 0, sizeof gSharedMan);
        if (tdReadFile(path, &txt, &tl) != 0) return NULL;
        if (tdManifestParse((const char *)txt, tl, &gSharedMan, err, sizeof err) == 0) {
            gSharedManOk = 1;
            snprintf(gSharedManDir, sizeof gSharedManDir, "%s", dataDir);
            gSharedManMtime = st.st_mtime;
            gSharedManSize = (long)st.st_size;
        } else {
            tdManifestFree(&gSharedMan);
        }
        free(txt);
    }
    return gSharedManOk ? &gSharedMan : NULL;
}

int imgSpriteOverridesBuild(ImgOverrides *o, const char *discPath, const uint8_t *file, size_t len)
{
    SprEntry *ent = (SprEntry *)calloc(1024, sizeof *ent);
    int n, i, s, withSource = 0, *perMod = NULL;
    struct ImgLazy *lz;
    char path[1400], label[100], rel[128];

    memset(o, 0, sizeof *o);
    if (!ent) return 0;
    n = sprTable(file, len, ent, 1024);
    lz = (struct ImgLazy *)calloc(1, sizeof *lz);
    perMod = (int *)calloc((size_t)assetLayerCount() + 1, sizeof *perMod);
    if (!lz || !perMod || n < 1) goto done;
    lz->file = file;
    lz->len = len;
    snprintf(lz->dataDir, sizeof lz->dataDir, "%s", assetDataDir() ? assetDataDir() : "");
    o->lazy = lz;
    lz = NULL;
    o->v = (ImgOverride *)calloc((size_t)n, sizeof *o->v);
    if (!o->v) goto done;
    for (i = 0; i < n; i++) {
        TimInfo t;
        ImgOverride *v;
        if (imgTimParse(file + ent[i].off, ent[i].len, &t)) continue; /* an animation, the set table: not an image */
        if (sprName(discPath, i, NULL, rel, sizeof rel)) continue;
        v = &o->v[o->n++];
        v->chain[0] = i;
        v->depth = 1;
        v->off = ent[i].off;
        v->origLen = ent[i].len;
        snprintf(v->rel, sizeof v->rel, "%s", rel);
        v->resolved = 1; /* until a source file turns up */
        for (s = 0; s < assetLayerCount(); s++) {
            if (sourcePath(s, rel, path, sizeof path, label, sizeof label)) {
                v->resolved = 0;
                perMod[s]++;
            }
        }
        withSource += !v->resolved;
    }
    for (s = 0; s < assetLayerCount() && !layerIsData(s); s++) {
        if (perMod[s]) {
            sourcePath(s, rel, path, sizeof path, label, sizeof label);
            plogLine("images", "override %.*s/<EEE>.png (%d) from %s\n", (int)(strrchr(rel, '/') - rel), rel, perMod[s], label);
        }
    }
    indexOverrides(o);
done:
    free(lz);
    free(perMod);
    free(ent);
    return withSource;
}

/* Read the sources of one frame in order (mods from the last, then an edited data/images file) and keep the first
   one the rules accept; the original stays when none does */
static void resolveOverride(ImgOverrides *o, ImgOverride *v)
{
    int s;
    const char *dataDir = o->lazy->dataDir;
    char path[1400], label[100];

    v->resolved = 1;
    for (s = 0; s < assetLayerCount() && !v->tim; s++) {
        uint8_t *png, *res;
        size_t pl;
        char why[200];
        if (!sourcePath(s, v->rel, path, sizeof path, label, sizeof label) || tdReadFile(path, &png, &pl)) continue;
        if (layerIsData(s) && sprPartnerOf(v->rel) < 0) { /* data/images: only a file the user changed counts, not the extractor's own */
            char hex[17];
            const TdManifest *man = sharedManifest(dataDir);
            tdHashHex(tdHash(png, pl), hex);
            if (tdFileState(1, man ? tdManifestFind(man, v->rel + sizeof SPR_REL - 1) : NULL, hex) != TD_FILE_EDITED) {
                free(png);
                continue;
            }
        }
        if (imgSubstitute(o->lazy->file + v->off, v->origLen, NULL, png, pl, &res, why, sizeof why) == 0) {
            v->tim = res;
            v->len = v->origLen;
            o->applied++;
            if (layerIsData(s)) plogLine("images", "override %s from %s\n", v->rel, label);
        } else {
            plogLine("images", "%s rejected: %s\n", path, why);
        }
        free(png);
    }
}

const ImgOverride *imgOverrideGet(ImgOverrides *o, size_t off)
{
    const IdxEnt *ix = (const IdxEnt *)o->idx;
    int lo = 0, hi = o->n - 1;
    ImgOverride *v;

    if (!ix) return imgOverrideAt(o, off);
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ix[mid].off == off) {
            v = &o->v[ix[mid].i];
            if (!v->resolved && o->lazy) resolveOverride(o, v);
            return v;
        }
        if (ix[mid].off < off) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

int imgOverridesBuild(ImgOverrides *o, const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet, size_t sheetLen)
{
    const char *dataDir = assetDataDir();
    BuildCtx b;
    char path[1200];
    uint8_t *txt;
    size_t tl;

    memset(o, 0, sizeof *o);
    memset(&b, 0, sizeof b);
    b.o = o;
    b.discPath = discPath;
    b.dataDir = dataDir;
    b.sheet = sheet;
    b.sheetLen = sheetLen;
    b.partnerFile = gPartner.begin != NULL && gPartner.begin(discPath);
    b.cardFile = imgIsCardFile(discPath);
    b.piecesOnly = imgIsMenuScreen(discPath) || imgIsStageMask(discPath);
    b.menuFile = gMenu.begin != NULL && gMenu.begin(discPath, file, len, sheet, sheetLen);
    b.stageFile = imgIsStageMask(discPath) && gStage.begin != NULL && gStage.begin(discPath, file, len, sheet, sheetLen);
    hintInit(&b.hc, discPath, file, len, sheet, sheetLen);
    b.fc.file = file;
    b.fc.len = len;
    b.fc.perEntry = !strcmp(discPath, DISC_PREFIX "FIELD/FIELDCOM.BIN");
    b.fc.label = discPath + (strncmp(discPath, DISC_PREFIX, 8) ? 0 : 8);
    b.fc.cardData = isCardData(discPath);
    if (dataDir && dataDir[0]) {
        snprintf(path, sizeof path, "%s/%s/manifest.json", dataDir, b.cardFile ? "cards" : "images");
        if (tdReadFile(path, &txt, &tl) == 0) {
            char err[120];
            if (tdManifestParse((const char *)txt, tl, &b.man, err, sizeof err)) tdManifestFree(&b.man);
            free(txt);
        }
    }
    walkFile(discPath, file, len, onBuild, &b);
    if (b.menuFile && gMenu.end != NULL) gMenu.end(discPath);
    if (b.stageFile && gStage.end != NULL) gStage.end(discPath);
    free(b.fc.wrap);
    hintFree(&b.hc);
    tdManifestFree(&b.man);
    indexOverrides(o);
    return o->applied;
}

long imgOverridesMix(const ImgOverrides *o)
{
    uint64_t sum = 0;
    int i, s;
    char path[1400], label[100];
    for (i = 0; i < o->n; i++) {
        for (s = 0; s < assetLayerCount() && o->v[i].rel[0]; s++) {
            struct stat st;
            if (sourcePath(s, o->v[i].rel, path, sizeof path, label, sizeof label) && stat(path, &st) == 0) {
                sum += ((uint64_t)st.st_mtime * 31u + (uint64_t)st.st_size) * (uint64_t)(2 * i + 3 + 1000 * s) + 1;
            }
        }
    }
    return (long)(sum & 0x3FFFFFFFFFFFFFFFull);
}

const ImgOverride *imgOverrideAt(const ImgOverrides *o, size_t off)
{
    int i;
    for (i = 0; i < o->n; i++) {
        if (o->v[i].off == off) return &o->v[i];
    }
    return NULL;
}

void imgOverridesFree(ImgOverrides *o)
{
    int i;
    for (i = 0; i < o->n; i++) free(o->v[i].tim);
    free(o->v);
    free(o->lazy);
    free(o->idx);
    memset(o, 0, sizeof *o);
}

/* ---- disc side ---- */

/* Every file under AAA/DAT/ of the disc directory (port_fs walks it once); *out is malloc'ed, the entries are
   port_fs's. Returns how many, -1 when the directory is not open. */
static int listDat(const PfsFile ***out)
{
    int n = pfsList(DISC_PREFIX, NULL, 0);
    const PfsFile **v;

    *out = NULL;
    if (!pfsDiscIsOpen()) return -1;
    v = (const PfsFile **)malloc((size_t)(n ? n : 1) * sizeof *v);
    if (!v) return -1;
    pfsList(DISC_PREFIX, v, n);
    *out = v;
    return n;
}

static int isArtFile(const char *path)
{
    return sprIsSpriteFile(path) || imgIsScreenFile(path) || imgIsCardFile(path) || isCardData(path) || imgIsStageMask(path) || imgIsStageBg(path) || !strcmp(path, DISC_PREFIX "FIELD/FIELDCOM.BIN");
}

/* What the runtime substitution needs from imgStart: the disc and the data folder */
static int gHaveDisc;
static char gDataDir[1100];

/* Reads the source folders now (loading time), so the first field sprite of a walk does not */
static void indexSources(void)
{
    double t0 = nowSeconds();
    int s, n = 0, nm = modsCount();
    imgOverridesRescan();
    for (s = 0; s < assetLayerCount(); s++) {
        AssetLayer l;
        SrcIndex *x;
        assetLayerAt(s, &l);
        x = idxFor(l.root);
        if (x) n += x->n;
    }
    if (plogOn("images", PLOG_MODS)) plogLine("images", "source index: %d PNGs in %d mod(s) and the data folder, %.3f s\n", n, nm, nowSeconds() - t0);
}

int imgStart(const TdDisc *disc, const char *dataDir)
{
    double t0 = nowSeconds();
    char hex[17];
    ImgRun run;
    int i, rc = 0;
    const PfsFile **ents = NULL;
    int nEnts = 0;
    if (!disc || !disc->readSector || !dataDir || !dataDir[0]) return -1;
    if (strlen(dataDir) < sizeof gDataDir) {
        snprintf(gDataDir, sizeof gDataDir, "%s", dataDir);
        gHaveDisc = 1;
    }
    if (!pfsDiscIsOpen()) return -1;
    /* the disc: the volume descriptor and the path table (every directory and its place) */
    expDiscHex(hex);
    if (imgRunOpen(&run, dataDir, hex, IMG_CODEC_VERSION)) return -1;
    if (imgRunCurrent(&run)) {
        plogLine("images", "data folder %s is current (same run), %d files, %.3f s\n", run.dir, run.old.count, nowSeconds() - t0);
        imgRunClose(&run);
        indexSources();
        return 0;
    }
    nEnts = listDat(&ents);
    if (nEnts < 0) rc = -1;
    for (i = 0; i < nEnts; i++) {
        uint8_t *f, *sh = NULL;
        char sp[96];
        size_t shLen = 0;
        const PfsFile *sf;

        if (!isArtFile(ents[i]->path) || imgIsMenuScreen(ents[i]->path) || imgIsStageMask(ents[i]->path) || imgIsCardFile(ents[i]->path) || sprIsSpriteFile(ents[i]->path)) continue; /* their pieces are exported by menu_export.c (menus/), stage_obj_export.c (stages/), card_export.c (cards/) and sprite_export.c (sprites/; the eight partner followers are in partners/<name>/field/) */
        f = pfsDiscRead(ents[i]->lba, (size_t)ents[i]->size);
        if (!f) {
            plogLine("images", "cannot read %s from the disc\n", ents[i]->path);
            run.failed++;
            continue;
        }
        if (imgSheetPathOf(ents[i]->path, sp, sizeof sp) && (sf = pfsFindPath(sp)) != NULL) {
            sh = pfsDiscRead(sf->lba, (size_t)sf->size);
            shLen = (size_t)sf->size;
        }
        imgExtractFile(&run, ents[i]->path, f, (size_t)ents[i]->size, sh, shLen);
        free(f);
        free(sh);
    }
    free(ents);
    rc = run.failed ? -1 : run.written;
    plogLine("images", "extracted %d files (%d kept, %d new beside edited, %d failed) from %d images (%d with a grey placeholder palette) into %s in %.3f s\n",
            run.written, run.kept, run.newFiles, run.failed, run.images, run.guessed, run.dir, nowSeconds() - t0);
    if (imgRunClose(&run)) rc = -1;
    indexSources();
    return rc;
}

/* ---- runtime: substituted images of the screen files the game loads ---- */

typedef struct OvFile {
    char path[104];
    long lba;
    size_t size;
    uint8_t *file, *sheet;
    size_t sheetLen;
    int loaded;
    int kind;           /* 0 screen art, 1 field sprite file (frames resolved on first use) */
    ImgOverrides ov;
    long mix;
    unsigned epoch;     /* the recheck epoch this file was last looked at in */
    long sheetLba;      /* -1: no sheet */
    size_t sheetSize;
} OvFile;

static OvFile *gFiles;
static int gNFiles, gScanned;
static OvFile *gLastFile;   /* the file the last lookup found: sprites ask for the same one frame after frame */
static unsigned gEpoch = 1; /* bumped at most once a second: a file rechecks its sources at most once per epoch */
static double gEpochAt;

static void scanOnce(void)
{
    const PfsFile **ents = NULL;
    int n, i;

    gScanned = 1;
    if (!gHaveDisc) return;
    n = listDat(&ents);
    for (i = 0; i < n; i++) {
        OvFile *f;
        char sp[96];
        const PfsFile *sf;
        int spr = sprIsSpriteFile(ents[i]->path);
        if (!isArtFile(ents[i]->path)) continue;
        f = (OvFile *)realloc(gFiles, (size_t)(gNFiles + 1) * sizeof *gFiles);
        if (!f) break;
        gFiles = f;
        f += gNFiles++;
        memset(f, 0, sizeof *f);
        snprintf(f->path, sizeof f->path, "%s", ents[i]->path);
        f->lba = ents[i]->lba;
        f->size = (size_t)ents[i]->size;
        f->sheetLba = -1;
        f->kind = spr;
        if (imgSheetPathOf(ents[i]->path, sp, sizeof sp) && (sf = pfsFindPath(sp)) != NULL) {
            f->sheetLba = sf->lba;
            f->sheetSize = (size_t)sf->size;
        }
    }
    free(ents);
}

static void rebuildFile(OvFile *f)
{
    imgOverridesFree(&f->ov);
    if (f->kind == 1) imgSpriteOverridesBuild(&f->ov, f->path, f->file, f->size);
    else imgOverridesBuild(&f->ov, f->path, f->file, f->size, f->sheet, f->sheetLen);
    f->mix = imgOverridesMix(&f->ov);
}

/* The scanned file that starts at `lba`, its sources looked at (loaded on first use, rechecked at most once per epoch) */
static OvFile *ovFileFor(long lba)
{
    OvFile *f = NULL;
    int i;

    if (!gHaveDisc) return NULL;
    if (!gScanned) scanOnce();
    if (gLastFile && gLastFile->lba == lba) {
        f = gLastFile;
    } else {
        for (i = 0; i < gNFiles; i++) {
            if (gFiles[i].lba == lba) f = &gFiles[i];
        }
        if (!f) return NULL;
        gLastFile = f;
    }
    if (!f->loaded) {
        f->loaded = 1;
        f->file = pfsDiscRead(f->lba, f->size);
        if (f->sheetLba >= 0) {
            f->sheet = pfsDiscRead(f->sheetLba, f->sheetSize);
            f->sheetLen = f->sheet ? f->sheetSize : 0;
        }
        if (f->file) rebuildFile(f);
        f->epoch = gEpoch;
    } else if (f->file && modsHotReload()) {
        /* opt-in only (DW3_MODS_HOTRELOAD=1): a modder may edit while the game runs, look again at most once a second overall.
           By default the mods are read once and this costs nothing. */
        double now = nowSeconds();
        if (now - gEpochAt >= 1.0) {
            gEpochAt = now;
            gEpoch++;
            imgOverridesRescan(); /* also files that appeared or went away */
        }
        if (f->epoch != gEpoch) {
            f->epoch = gEpoch;
            if (imgOverridesMix(&f->ov) != f->mix) rebuildFile(f);
        }
    }
    return f->file ? f : NULL;
}

int imgFileOverridden(long lba)
{
    OvFile *f = ovFileFor(lba);

    return f != NULL && f->ov.applied > 0;
}

const uint8_t *imgOverrideTim(long lba, size_t off, size_t *len)
{
    OvFile *f = ovFileFor(lba);
    const ImgOverride *o;
    
    if (!f) return NULL;
    o = imgOverrideGet(&f->ov, off);
    if (o && plogOn("images", PLOG_TRACE)) { /* which art the game loads: load <name> (override|disc) */
        plogLine("images", "load %s (%s)\n", o->rel[0] ? o->rel : "<piece page>", o->tim ? "override" : "disc");
    }
    if (!o || !o->tim) return NULL;
    if (len) *len = o->len;
    return o->tim;
}

/* A mod's sprites/<name>/NNN.png of an original sprite that is not a frame of it (an animation, the set table, or past the end of the archive) is never read:
   one "ignored" line per file (readable-assets A7.5; mod_tools.c lists the files that are not NNN.png at all) */
static void checkSpriteFrames(const OvFile *f)
{
    char folder[48];
    int k, j;

    if (sprPartnerOf(f->path) >= 0 || sprFolder(f->path, folder, sizeof folder)) return;
    for (k = 0; k < modsCount(); k++) {
        char dir[1300], sub[1500];
        DIR *d;
        struct dirent *e;

        if (!assetModDir(k, NULL, "sprites", dir, sizeof dir)) continue;
        snprintf(sub, sizeof sub, "%s/%s", dir, folder);
        if ((d = opendir(sub)) == NULL) continue;
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            int num, found = 0;

            if (n != 7 || !isdigit((unsigned char)e->d_name[0]) || !isdigit((unsigned char)e->d_name[1]) || !isdigit((unsigned char)e->d_name[2]) || strcmp(e->d_name + 3, ".png")) continue;
            num = atoi(e->d_name);
            for (j = 0; j < f->ov.n && !found; j++) found = f->ov.v[j].chain[0] == num;
            if (!found) plogLine("images", "%s/%s is ignored: sprite %s has no frame %d (its frames are %s)\n", sub, e->d_name, folder, num, "the entries that are images; animations and the set table have none");
        }
        closedir(d);
    }
}

/* --validate-mod (U5): builds the override table of every art file of the disc as the game does on first use, so every PNG of the mods'
   images/ folders goes through imgSubstitute and the refusals are logged (`images: <path> rejected: <why>`); a screen's pages also build the plan of the
   menu pieces (menu_pack.h) and a stage's MASK file the plan of the object pieces (stage_obj_pack.h), which log their own refusals. Field sprites resolve each
   frame that has a source. Needs imgStart to have been given the disc. Returns how many art files were looked at, or -1 without a disc. */
int imgValidateMods(void)
{
    int i, j, k, any = 0, files = 0;

    if (!gHaveDisc) return -1;
    for (k = 0; k < modsCount(); k++) {
        char dir[1300];
        if (assetModDir(k, NULL, "images", dir, sizeof dir) || assetModDir(k, NULL, "menus", dir, sizeof dir) || assetModDir(k, NULL, "stages", dir, sizeof dir) ||
            assetModDir(k, NULL, "cards", dir, sizeof dir) || assetModDir(k, NULL, "sprites", dir, sizeof dir)) any = 1;
    }
    if (!any) return 0;
    if (!gScanned) scanOnce();
    for (i = 0; i < gNFiles; i++) {
        OvFile *f = ovFileFor(gFiles[i].lba);
        if (!f) continue;
        files++;
        if (f->kind == 1) {
            for (j = 0; j < f->ov.n; j++) imgOverrideGet(&f->ov, f->ov.v[j].off);
            checkSpriteFrames(f);
        }
    }
    return files;
}
