/* Importing the gym training scene of a partner's bundle (readable-assets A4b): see partner_train.h. */
#include "partner_train.h"

#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>

#include "content.h"
#include "exp_core.h"
#include "mods.h"
#include "partner_bundle.h"
#include "partner_export.h"
#include "port_fs.h"
#include "port_log.h"

#define TAG "partners"
#define PAGE_W 256
#define PAGE_H 256
#define ROW_COLOURS 15 /* a row's entry 0 is the transparent colour */
#define MAX_FRAMES 4096

static void whyf(char *why, size_t cap, const char *fmt, ...) {
    va_list ap;

    if (why == NULL || cap == 0) return;
    va_start(ap, fmt);
    vsnprintf(why, cap, fmt, ap);
    va_end(ap);
}

/* ---- training.json and the PNGs ---- */

static int jWhole(const ExtJson *o, const char *key, long lo, long hi, long *out) {
    const ExtJson *v = extJsonGet(o, key);

    if (v == NULL || v->t != 0 || !extWhole(v->num, lo, hi)) return 0;
    *out = (long)v->num;
    return 1;
}

static int safeName(const char *s) {
    size_t i, n = strlen(s);

    if (n == 0 || n > 63 || s[0] == '.') return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

void ptSourceFree(PtSource *s) {
    int i;

    for (i = 0; s->frame != NULL && i < s->nFrames; i++) pngImageFree(&s->frame[i].png);
    free(s->frame);
    for (i = 0; i < PT_ANIMS; i++) free(s->anim[i].step);
    memset(s, 0, sizeof *s);
}

static int parseFrames(const ExtJson *fr, PtReadFn read, void *ud, int decode, PtSource *out, char *why, size_t cap) {
    int i, k, maxId = -1;

    for (i = 0; i < fr->n; i++) {
        long id;

        if (fr->v[i].t != 2 || !jWhole(&fr->v[i], "id", 0, MAX_FRAMES - 1, &id)) {
            whyf(why, cap, "training.json: frame %d has no \"id\" (0..%d)", i, MAX_FRAMES - 1);
            return -1;
        }
        if (id > maxId) maxId = (int)id;
    }
    if (maxId < 0) {
        whyf(why, cap, "training.json: no frames");
        return -1;
    }
    out->nFrames = maxId + 1;
    out->frame = (PtFrame *)calloc((size_t)out->nFrames, sizeof *out->frame);
    if (out->frame == NULL) return -1;
    for (i = 0; i < fr->n; i++) {
        const ExtJson *o = &fr->v[i], *img = extJsonGet(o, "image"), *org = extJsonGet(o, "origin"), *bl = extJsonGet(o, "blend");
        long id = 0, ox = 0, oy = 0;
        PtFrame *f;

        jWhole(o, "id", 0, MAX_FRAMES - 1, &id);
        f = &out->frame[id];
        if (f->hasImage) {
            whyf(why, cap, "training.json: frame %ld appears twice", id);
            return -1;
        }
        if (org != NULL && (org->t != 4 || org->n != 2 || org->v[0].t != 0 || org->v[1].t != 0 || !extWhole(org->v[0].num, -100000, 100000) ||
                            !extWhole(org->v[1].num, -100000, 100000))) {
            whyf(why, cap, "training.json: frame %ld \"origin\" is [x, y]", id);
            return -1;
        }
        if (org != NULL) {
            ox = (long)org->v[0].num;
            oy = (long)org->v[1].num;
        }
        f->ox = (int)ox;
        f->oy = (int)oy;
        f->paletteBase = -1;
        if (extJsonGet(o, "paletteBase") != NULL) {
            long pb;

            if (!jWhole(o, "paletteBase", 0, 1, &pb)) {
                whyf(why, cap, "training.json: frame %ld \"paletteBase\" is 0 or 1", id);
                return -1;
            }
            f->paletteBase = (int)pb;
        }
        for (k = 0; bl != NULL && bl->t == 4 && k < bl->n; k++) {
            const ExtJson *ix = extJsonGet(&bl->v[k], "indices");
            long lo, hi, mode;

            if (f->nBlend == 16 || ix == NULL || ix->t != 4 || ix->n != 2 || ix->v[0].t != 0 || ix->v[1].t != 0 || !extWhole(ix->v[0].num, 0, 255) ||
                !extWhole(ix->v[1].num, 0, 255) || !jWhole(&bl->v[k], "mode", 0, 3, &mode)) {
                whyf(why, cap, "training.json: frame %ld \"blend\" entries are {\"indices\": [lo, hi], \"mode\": 0..3}", id);
                return -1;
            }
            lo = (long)ix->v[0].num;
            hi = (long)ix->v[1].num;
            f->blend[f->nBlend].lo = (int)lo;
            f->blend[f->nBlend].hi = (int)hi;
            f->blend[f->nBlend].mode = (int)mode;
            f->nBlend++;
        }
        if (img == NULL || img->t != 1) {
            f->hasImage = 0;
            f->png.w = 0; /* an empty sprite */
            continue;
        }
        if (!safeName(img->str)) {
            whyf(why, cap, "training.json: frame %ld \"image\" is not a plain file name", id);
            return -1;
        }
        {
            uint8_t *bytes;
            size_t len;
            char sub[120];

            if (read(ud, img->str, &bytes, &len) != 0) {
                whyf(why, cap, "frame %ld: %s cannot be read", id, img->str);
                return -1;
            }
            if (decode) {
                int rc = pngReadIndexed(bytes, len, &f->png, sub, sizeof sub);

                free(bytes);
                if (rc != 0) {
                    whyf(why, cap, "frame %ld: %s: %s", id, img->str, sub);
                    return -1;
                }
                if (f->png.w > PAGE_W || f->png.h > PAGE_H) {
                    whyf(why, cap, "frame %ld: %s is %dx%d, a part is at most %dx%d (one page)", id, img->str, f->png.w, f->png.h, PAGE_W, PAGE_H);
                    return -1;
                }
            } else {
                free(bytes);
            }
            f->hasImage = 1;
        }
    }
    return 0;
}

static int parseAnims(const ExtJson *an, PtSource *out, char *why, size_t cap) {
    int i, k;

    for (i = 0; i < an->n; i++) {
        const ExtJson *o = &an->v[i], *st = extJsonGet(o, "steps");
        long id;
        PtAnim *a;

        if (o->t != 2 || !jWhole(o, "id", 0, PT_ANIMS - 1, &id) || st == NULL || st->t != 4) {
            whyf(why, cap, "training.json: animation %d is {\"id\": 0..%d, \"steps\": [...]}", i, PT_ANIMS - 1);
            return -1;
        }
        a = &out->anim[id];
        if (a->step != NULL || st->n < 1 || st->n > 4096) {
            whyf(why, cap, "training.json: animation %ld appears twice or has no steps", id);
            return -1;
        }
        a->step = (PtStep *)calloc((size_t)st->n, sizeof *a->step);
        if (a->step == NULL) return -1;
        a->n = st->n;
        for (k = 0; k < st->n; k++) {
            long fr, tm, x, y;

            if (!jWhole(&st->v[k], "frame", 0, out->nFrames - 1, &fr) || !jWhole(&st->v[k], "time", 0, 255, &tm) || !jWhole(&st->v[k], "x", -30000, 30000, &x) ||
                !jWhole(&st->v[k], "y", -30000, 30000, &y)) {
                whyf(why, cap, "training.json: animation %ld step %d needs frame (a frame id), time (0..255), x and y", id, k);
                return -1;
            }
            a->step[k].frame = (int)fr;
            a->step[k].time = (int)tm;
            a->step[k].x = (int)x;
            a->step[k].y = (int)y;
        }
    }
    return 0;
}

int ptParse(PtReadFn read, void *ud, int decode, PtSource *out, char *why, size_t cap) {
    uint8_t *bytes;
    size_t len;
    ExtJson j;
    const ExtJson *v;
    int rc = -1;

    memset(out, 0, sizeof *out);
    if (read(ud, "training.json", &bytes, &len) != 0) {
        whyf(why, cap, "training.json cannot be read");
        return -1;
    }
    if (extJsonParse(bytes, len, &j) != 0) {
        free(bytes);
        whyf(why, cap, "training.json is not a JSON object");
        return -1;
    }
    free(bytes);
    v = extJsonGet(&j, "format");
    if (v == NULL || v->t != 1 || strcmp(v->str, "partners-training-v1") != 0) {
        whyf(why, cap, "training.json: \"format\" must be \"partners-training-v1\"");
        goto done;
    }
    v = extJsonGet(&j, "frames");
    if (v == NULL || v->t != 4) {
        whyf(why, cap, "training.json: \"frames\" is a list");
        goto done;
    }
    if (parseFrames(v, read, ud, decode, out, why, cap) != 0) goto done;
    v = extJsonGet(&j, "animations");
    if (v != NULL && (v->t != 4 || parseAnims(v, out, why, cap) != 0)) {
        if (v->t != 4) whyf(why, cap, "training.json: \"animations\" is a list");
        goto done;
    }
    rc = 0;
done:
    extJsonFree(&j);
    if (rc != 0) ptSourceFree(out);
    return rc;
}

/* ---- cutting a frame into parts ---- */

typedef struct Part {
    int frame, lx, ly, w, h, blend; /* blend: 0 opaque, else 1 + mode */
    int page, u, v, row, placed;
    uint16_t pal[16];
    uint8_t *idx; /* w * h row indices, 0 transparent */
} Part;

typedef struct Parts {
    Part *p;
    int n, cap;
} Parts;

static int blendOf(const PtFrame *f, int idx) {
    int k;

    for (k = 0; k < f->nBlend; k++) {
        if (idx >= f->blend[k].lo && idx <= f->blend[k].hi) return 1 + f->blend[k].mode;
    }
    return 0;
}

static uint16_t colourOf(const PngImage *p, int idx) {
    unsigned c = (unsigned)(p->rgb[3 * idx] >> 3) | (unsigned)(p->rgb[3 * idx + 1] >> 3) << 5 | (unsigned)(p->rgb[3 * idx + 2] >> 3) << 10;

    return (uint16_t)(c == 0 ? 0x8000 : c); /* 0x0000 is the PS1's transparent: an opaque black is 0x8000 */
}

static Part *partAdd(Parts *ps) {
    if (ps->n == ps->cap) {
        int cap = ps->cap ? ps->cap * 2 : 64;
        Part *np = (Part *)realloc(ps->p, (size_t)cap * sizeof *np);

        if (np == NULL) return NULL;
        ps->p = np;
        ps->cap = cap;
    }
    memset(&ps->p[ps->n], 0, sizeof ps->p[ps->n]);
    return &ps->p[ps->n++];
}

/* A colour group is the pixels that share one palette row (and one blend mode); cut into parts */
typedef struct Grp {
    uint16_t pal[16];
    int blend;
} Grp;

#define MAX_GRP 160
#define SAVE_PERCENT 20 /* a band of empty rows or columns cuts a part in two when it saves this much of its box ... */
#define MIN_SAVE 40     /* ... and at least this many pixels */

static int visibleIdx(const PngImage *im, int idx) {
    return idx < im->nPal && !(idx < im->nAlpha && im->alpha[idx] == 0);
}

/* Free packing (a hand-made PNG): the visible colours in palette order, 15 to a row (entry 0 of a row is the transparent one), a row never mixes blend modes */
static int groupsFree(const PtFrame *f, const int used[256], uint8_t group[256], uint8_t local[256], Grp *g, int *ng) {
    const PngImage *im = &f->png;
    int idx, cnt = 0, cur = -1;

    *ng = 0;
    for (idx = 0; idx < 256; idx++) {
        int b;

        if (!used[idx] || !visibleIdx(im, idx)) continue;
        b = blendOf(f, idx);
        if (*ng == 0 || b != cur || cnt == ROW_COLOURS) {
            if (*ng == MAX_GRP) return -1;
            memset(&g[*ng], 0, sizeof g[*ng]);
            g[*ng].blend = b;
            (*ng)++;
            cnt = 0;
            cur = b;
        }
        group[idx] = (uint8_t)(*ng - 1);
        local[idx] = (uint8_t)++cnt;
        g[*ng - 1].pal[cnt] = colourOf(im, idx);
    }
    return 0;
}

/* Palette blocks (what the exporter writes: paletteBase, then 16 indices per palette): a palette's row is the block itself, entry for entry, so the same
   palette of two frames is the same row. A block whose entry 0 is a visible colour cannot keep it at index 0 (that is the part's transparent), so it is two
   rows: entries 0..14 at 1..15, and entry 15 at 1. */
static int groupsBlocks(const PtFrame *f, const int used[256], uint8_t group[256], uint8_t local[256], Grp *g, int *ng) {
    const PngImage *im = &f->png;
    int base = f->paletteBase, k, j, bl, nb;

    *ng = 0;
    for (k = 0; base + 16 * k < 256; k++) {
        int lo = base + 16 * k, blends[16];

        nb = 0;
        for (j = 0; j < 16 && lo + j < 256; j++) {
            int idx = lo + j, b, q;

            if (!used[idx] || !visibleIdx(im, idx)) continue;
            b = blendOf(f, idx);
            for (q = 0; q < nb && blends[q] != b; q++) {
            }
            if (q == nb) blends[nb++] = b;
        }
        for (bl = 0; bl < nb; bl++) {
            int entry0 = visibleIdx(im, lo), rows = entry0 ? 2 : 1, r;

            for (r = 0; r < rows; r++) {
                int made = 0, from = entry0 ? (r == 0 ? 0 : 15) : 0, to = entry0 ? (r == 0 ? 14 : 15) : 15, shift = entry0 ? 1 - from : 0;

                for (j = from; j <= to; j++) {
                    int idx = lo + j;

                    if (idx >= 256 || !visibleIdx(im, idx) || blendOf(f, idx) != blends[bl]) continue;
                    if (!made) {
                        if (*ng == MAX_GRP) return -1;
                        memset(&g[*ng], 0, sizeof g[*ng]);
                        g[*ng].blend = blends[bl];
                        (*ng)++;
                        made = 1;
                    }
                    g[*ng - 1].pal[j + shift] = colourOf(im, idx);
                    if (used[idx]) {
                        group[idx] = (uint8_t)(*ng - 1);
                        local[idx] = (uint8_t)(j + shift);
                    }
                }
            }
        }
    }
    return 0;
}

static int emitPart(Parts *ps, const PtFrame *f, int frame, const uint8_t group[256], const uint8_t local[256], const Grp *g, int gi, int x0, int y0, int x1, int y1,
                    int fx0, int fy0) {
    const PngImage *im = &f->png;
    int minx = x1, miny = y1, maxx = -1, maxy = -1, x, y;
    Part *p;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (group[im->px[y * im->w + x]] != gi) continue;
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }
    if (maxx < 0) return 0;
    /* a part starts at a byte of the sprite: one that would start more than 255 pixels after the frame's first pixel is made wider to the left (transparent) */
    if (minx > fx0 + 255) minx = fx0 + 255;
    if (miny > fy0 + 255) miny = fy0 + 255;
    if (maxx - minx + 1 > PAGE_W || maxy - miny + 1 > PAGE_H) return -1;
    p = partAdd(ps);
    if (p == NULL) return -1;
    p->frame = frame;
    p->lx = minx;
    p->ly = miny;
    p->w = maxx - minx + 1;
    p->h = maxy - miny + 1;
    p->blend = g[gi].blend;
    memcpy(p->pal, g[gi].pal, sizeof p->pal);
    p->idx = (uint8_t *)calloc((size_t)p->w * (size_t)p->h, 1);
    if (p->idx == NULL) return -1;
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < p->w; x++) {
            int s = im->px[(miny + y) * im->w + minx + x];

            if (group[s] == gi) p->idx[y * p->w + x] = local[s];
        }
    }
    return 0;
}

/* the group's pixels of the box (x0, y0)-(x1, y1) as one part, or, when a band of empty rows or columns would save a good part of its area, as the parts on
   either side of the widest such band */
static int splitParts(Parts *ps, const PtFrame *f, int frame, const uint8_t group[256], const uint8_t local[256], const Grp *g, int gi, int x0, int y0, int x1, int y1,
                      int fx0, int fy0) {
    const PngImage *im = &f->png;
    int minx = x1, miny = y1, maxx = -1, maxy = -1, x, y, w, h, axis, c, run;
    int colHas[256], rowHas[256], bestSave = 0, bestAxis = 0, bestFrom = 0, bestTo = 0;

    memset(colHas, 0, sizeof colHas);
    memset(rowHas, 0, sizeof rowHas);
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (group[im->px[y * im->w + x]] != gi) continue;
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
            colHas[x] = rowHas[y] = 1;
        }
    }
    if (maxx < 0) return 0;
    x0 = minx;
    y0 = miny;
    x1 = maxx + 1;
    y1 = maxy + 1;
    w = x1 - x0;
    h = y1 - y0;
    for (axis = 0; axis < 2; axis++) {
        const int *has = axis == 0 ? colHas : rowHas;
        int lo = axis == 0 ? x0 : y0, hi = axis == 0 ? x1 : y1, other = axis == 0 ? h : w;

        for (c = lo, run = 0; c <= hi; c++) {
            if (c < hi && !has[c]) {
                run++;
                continue;
            }
            if (run > 0 && run * other > bestSave) {
                bestSave = run * other;
                bestAxis = axis;
                bestFrom = c - run;
                bestTo = c;
            }
            run = 0;
        }
    }
    if (bestSave < MIN_SAVE || bestSave * 100 < w * h * SAVE_PERCENT) return emitPart(ps, f, frame, group, local, g, gi, x0, y0, x1, y1, fx0, fy0);
    if (bestAxis == 0) {
        if (splitParts(ps, f, frame, group, local, g, gi, x0, y0, bestFrom, y1, fx0, fy0) != 0) return -1;
        return splitParts(ps, f, frame, group, local, g, gi, bestTo, y0, x1, y1, fx0, fy0);
    }
    if (splitParts(ps, f, frame, group, local, g, gi, x0, y0, x1, bestFrom, fx0, fy0) != 0) return -1;
    return splitParts(ps, f, frame, group, local, g, gi, x0, bestTo, x1, y1, fx0, fy0);
}

static int cutFrame(const PtFrame *f, int frame, int tile, Parts *ps, char *why, size_t cap) {
    const PngImage *im = &f->png;
    uint8_t group[256], local[256];
    Grp *g = (Grp *)calloc(MAX_GRP, sizeof *g);
    int used[256], ng = 0, i, n = (int)((size_t)im->w * (size_t)im->h), rc = -1;

    if (g == NULL) return -1;
    memset(used, 0, sizeof used);
    memset(group, 255, sizeof group);
    memset(local, 0, sizeof local);
    for (i = 0; i < n; i++) {
        if (im->px[i] >= im->nPal) {
            whyf(why, cap, "frame %d: a pixel uses index %d and the palette has %d", frame, im->px[i], im->nPal);
            goto done;
        }
        used[im->px[i]] = 1;
    }
    if ((f->paletteBase >= 0 ? groupsBlocks(f, used, group, local, g, &ng) : groupsFree(f, used, group, local, g, &ng)) != 0) {
        whyf(why, cap, "frame %d has more palettes than a frame can use", frame);
        goto done;
    }
    {
        int fx0 = im->w, fy0 = im->h, x, y;

        for (y = 0; y < im->h; y++) {
            for (x = 0; x < im->w; x++) {
                if (group[im->px[y * im->w + x]] == 255) continue;
                if (x < fx0) fx0 = x;
                if (y < fy0) fy0 = y;
            }
        }
        for (i = 0; i < ng; i++) {
            int bad = 0;

            if (tile <= 0) {
                bad = splitParts(ps, f, frame, group, local, g, i, 0, 0, im->w, im->h, fx0, fy0) != 0;
            } else {
                /* tiles of a grid fixed in the sprite's own coordinates: the same pixels of two frames fall in the same tiles, so a tile is stored once */
                int ty, tx, ax0 = f->ox, ay0 = f->oy;

                for (ty = -(((ay0 % tile) + tile) % tile); ty < im->h && !bad; ty += tile) {
                    for (tx = -(((ax0 % tile) + tile) % tile); tx < im->w && !bad; tx += tile) {
                        bad = emitPart(ps, f, frame, group, local, g, i, tx < 0 ? 0 : tx, ty < 0 ? 0 : ty, tx + tile > im->w ? im->w : tx + tile,
                                       ty + tile > im->h ? im->h : ty + tile, fx0, fy0) != 0;
                    }
                }
            }
            if (bad) {
                whyf(why, cap, "frame %d: a part does not fit a page", frame);
                goto done;
            }
        }
    }
    rc = 0;
done:
    free(g);
    return rc;
}

/* ---- packing into pages ---- */

typedef struct Page {
    uint16_t pal[PT_PAGE_ROWS][16];
    int nrows, nShelf, usedH;
    int shelfY[PAGE_H], shelfH[PAGE_H], shelfX[PAGE_H];
    uint8_t *px; /* PAGE_W x PAGE_H indices */
} Page;

static int pageFindRow(const Page *pg, const uint16_t pal[16]) {
    int r;

    for (r = 0; r < pg->nrows; r++) {
        if (memcmp(pg->pal[r], pal, sizeof pg->pal[r]) == 0) return r;
    }
    return -1;
}

/* where a w x h part goes: the shelf it fits best, else a new one; 0 when the page has no room */
static int pageSpace(const Page *pg, int w, int h, int *x, int *y, int *shelf) {
    int s, best = -1, nextY = 0;

    for (s = 0; s < pg->nShelf; s++) {
        if (pg->shelfH[s] >= h && pg->shelfX[s] + w <= PAGE_W && (best < 0 || pg->shelfH[s] < pg->shelfH[best])) best = s;
        nextY = pg->shelfY[s] + pg->shelfH[s];
    }
    if (best >= 0) {
        *x = pg->shelfX[best];
        *y = pg->shelfY[best];
        *shelf = best;
        return 1;
    }
    if (nextY + h > PAGE_H || pg->nShelf == PAGE_H) return 0;
    *x = 0;
    *y = nextY;
    *shelf = -1;
    return 1;
}

static int pagePlace(Page *pg, Part *p, int pageNo) {
    int x, y, shelf, row, yy;

    row = pageFindRow(pg, p->pal);
    if (row < 0 && pg->nrows == PT_PAGE_ROWS) return 0;
    if (!pageSpace(pg, p->w, p->h, &x, &y, &shelf)) return 0;
    if (row < 0) {
        row = pg->nrows++;
        memcpy(pg->pal[row], p->pal, sizeof pg->pal[row]);
    }
    if (shelf < 0) {
        shelf = pg->nShelf++;
        pg->shelfY[shelf] = y;
        pg->shelfH[shelf] = p->h;
        pg->shelfX[shelf] = 0;
    }
    pg->shelfX[shelf] = x + p->w;
    for (yy = 0; yy < p->h; yy++) memcpy(pg->px + (size_t)(y + yy) * PAGE_W + (size_t)x, p->idx + (size_t)yy * (size_t)p->w, (size_t)p->w);
    if (y + p->h > pg->usedH) pg->usedH = y + p->h;
    p->page = pageNo;
    p->u = x;
    p->v = y;
    p->row = row;
    p->placed = 1;
    return 1;
}

/* ---- the bytes ---- */

static void put16(uint8_t *b, size_t at, unsigned v) {
    b[at] = (uint8_t)v;
    b[at + 1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *b, size_t at, uint32_t v) {
    put16(b, at, v & 0xFFFF);
    put16(b, at + 2, v >> 16);
}

void ptSetFree(PtSet *s) {
    free(s->file);
    memset(s, 0, sizeof *s);
}

/* the TIM of a page: header, a CLUT block of 16 x rows halfwords at (clutX, 480), an image block of 64 x height halfwords at (768, 256 * page) */
static size_t timWrite(uint8_t *b, size_t at, const Page *pg, int pageNo) {
    int rows = pg->nrows > 0 ? pg->nrows : 1, h = pg->usedH > 0 ? pg->usedH : 1, r, x, y;
    size_t clutSize = 12 + 32 * (size_t)rows, imgSize = 12 + 128 * (size_t)h, o = at;

    put32(b, o, 0x10);
    put32(b, o + 4, 8); /* 4 bits with a CLUT */
    o += 8;
    put32(b, o, (uint32_t)clutSize);
    put16(b, o + 4, (unsigned)(16 * pageNo));
    put16(b, o + 6, 480);
    put16(b, o + 8, 16);
    put16(b, o + 10, (unsigned)rows);
    for (r = 0; r < pg->nrows; r++) {
        for (x = 0; x < 16; x++) put16(b, o + 12 + 32 * (size_t)r + 2 * (size_t)x, pg->pal[r][x]);
    }
    o += clutSize;
    put32(b, o, (uint32_t)imgSize);
    put16(b, o + 4, 768);
    put16(b, o + 6, (unsigned)(256 * pageNo));
    put16(b, o + 8, 64);
    put16(b, o + 10, (unsigned)h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < PAGE_W; x += 2) b[o + 12 + (size_t)y * 128 + (size_t)(x / 2)] = (uint8_t)(pg->px[(size_t)y * PAGE_W + (size_t)x] | pg->px[(size_t)y * PAGE_W + (size_t)x + 1] << 4);
    }
    return o + imgSize - at;
}

static int buildOnce(const PtSource *src, PtSet *out, int tile, char *why, size_t cap) {
    Parts ps;
    Page pg[PT_PAGES];
    int *order = NULL, *bx = NULL, *by = NULL, *first = NULL, *count = NULL;
    int i, j, k, a, rc = -1, nPages;
    size_t bankSize, animBytes[PT_ANIMS], total, at, setAt = 36, hdr, rowsBytes;
    uint8_t *b = NULL;

    memset(&ps, 0, sizeof ps);
    memset(pg, 0, sizeof pg);
    memset(out, 0, sizeof *out);
    if (src->nFrames < 1) {
        whyf(why, cap, "no frames");
        return -1;
    }
    bx = (int *)calloc((size_t)src->nFrames, sizeof *bx);
    by = (int *)calloc((size_t)src->nFrames, sizeof *by);
    first = (int *)calloc((size_t)src->nFrames, sizeof *first);
    count = (int *)calloc((size_t)src->nFrames, sizeof *count);
    for (i = 0; i < PT_PAGES; i++) pg[i].px = (uint8_t *)calloc((size_t)PAGE_W * PAGE_H, 1);
    if (bx == NULL || by == NULL || first == NULL || count == NULL || pg[0].px == NULL || pg[1].px == NULL) goto done;
    for (i = 0; i < src->nFrames; i++) {
        const PtFrame *f = &src->frame[i];

        first[i] = ps.n;
        if (f->hasImage && cutFrame(f, i, tile, &ps, why, cap) != 0) goto done;
        count[i] = ps.n - first[i];
        for (j = first[i]; j < ps.n; j++) {
            ps.p[j].lx += f->ox; /* now relative to the sprite's origin */
            ps.p[j].ly += f->oy;
        }
        /* a part's x and y are bytes: a frame outside 0..255 moves and its animation steps move back */
        if (count[i] > 0) {
            int minx = 1 << 30, miny = 1 << 30, maxx = -(1 << 30), maxy = -(1 << 30);

            for (j = first[i]; j < ps.n; j++) {
                if (ps.p[j].lx < minx) minx = ps.p[j].lx;
                if (ps.p[j].ly < miny) miny = ps.p[j].ly;
                if (ps.p[j].lx > maxx) maxx = ps.p[j].lx;
                if (ps.p[j].ly > maxy) maxy = ps.p[j].ly;
            }
            bx[i] = minx < 0 ? -minx : (maxx > 255 ? 255 - maxx : 0);
            by[i] = miny < 0 ? -miny : (maxy > 255 ? 255 - maxy : 0);
            if (minx + bx[i] < 0 || miny + by[i] < 0 || maxx + bx[i] > 255 || maxy + by[i] > 255) {
                whyf(why, cap, "frame %d spans more than 255 pixels from its first part to its last (a part starts at a byte)", i);
                goto done;
            }
        }
    }
    for (a = 0; a < PT_ANIMS; a++) {
        for (k = 0; k < src->anim[a].n; k++) {
            const PtStep *s = &src->anim[a].step[k];

            if (s->frame < 0 || s->frame >= src->nFrames) {
                whyf(why, cap, "animation %d step %d shows frame %d, which is not there", a, k, s->frame);
                goto done;
            }
            if (s->x - bx[s->frame] < -32768 || s->x - bx[s->frame] > 32767 || s->y - by[s->frame] < -32768 || s->y - by[s->frame] > 32767) {
                whyf(why, cap, "animation %d step %d is out of range", a, k);
                goto done;
            }
        }
    }
    /* pack: tallest first; identical parts (same size, palette row and pixels) once */
    order = (int *)malloc((size_t)(ps.n > 0 ? ps.n : 1) * sizeof *order);
    if (order == NULL) goto done;
    for (i = 0; i < ps.n; i++) {
        for (j = i; j > 0 && (ps.p[order[j - 1]].h < ps.p[i].h || (ps.p[order[j - 1]].h == ps.p[i].h && ps.p[order[j - 1]].w < ps.p[i].w)); j--) order[j] = order[j - 1];
        order[j] = i;
    }
    for (i = 0; i < ps.n; i++) {
        Part *p = &ps.p[order[i]];
        int placed = 0;

        for (j = 0; j < i && !placed; j++) {
            const Part *q = &ps.p[order[j]];

            if (q->w == p->w && q->h == p->h && memcmp(q->pal, p->pal, sizeof q->pal) == 0 && memcmp(q->idx, p->idx, (size_t)p->w * (size_t)p->h) == 0) {
                p->page = q->page;
                p->u = q->u;
                p->v = q->v;
                p->row = q->row;
                p->placed = 1;
                placed = 1;
            }
        }
        for (k = 0; k < PT_PAGES && !placed; k++) placed = pagePlace(&pg[k], p, k);
        if (!placed) {
            long area = 0;
            int q, m, distinct = 0;

            for (q = 0; q < ps.n; q++) {
                area += (long)ps.p[q].w * ps.p[q].h;
                for (m = 0; m < q && memcmp(ps.p[m].pal, ps.p[q].pal, sizeof ps.p[q].pal) != 0; m++) {
                }
                distinct += m == q;
            }
            whyf(why, cap, "the frames do not fit two %dx%d pages with %d palettes each (%d parts of %ld pixels, %d palettes; frame %d's %dx%d part found no room)", PAGE_W, PAGE_H,
                 PT_PAGE_ROWS, ps.n, area, distinct, p->frame, p->w, p->h);
            rc = -2;
            goto done;
        }
    }
    nPages = pg[1].usedH > 0 ? 2 : 1;
    /* sizes */
    hdr = 4 * (size_t)(7 + nPages + 3);
    bankSize = 8;
    for (i = 0; i < src->nFrames; i++) bankSize += 4 + 20 * (size_t)count[i];
    total = setAt + hdr + bankSize;
    for (a = 0; a < PT_ANIMS; a++) {
        animBytes[a] = src->anim[a].n > 0 ? 4 + 8 * (size_t)src->anim[a].n : 0;
        total += animBytes[a];
    }
    rowsBytes = 0;
    for (i = 0; i < nPages; i++) rowsBytes += 8 + 12 + 32 * (size_t)(pg[i].nrows > 0 ? pg[i].nrows : 1) + 12 + 128 * (size_t)(pg[i].usedH > 0 ? pg[i].usedH : 1);
    total += rowsBytes;
    b = (uint8_t *)calloc(total, 1);
    if (b == NULL) goto done;
    put32(b, 0, (uint32_t)setAt);
    at = setAt + hdr;
    out->bank = (uint32_t)at;
    out->bankOffset = 8;
    out->bankSize = (uint32_t)bankSize;
    put16(b, at, 801);
    put16(b, at + 2, 0);
    put16(b, at + 4, (unsigned)src->nFrames);
    {
        size_t o = at + 8;

        for (i = 0; i < src->nFrames; i++) {
            put32(b, o, (uint32_t)count[i]);
            o += 4;
            for (j = first[i]; j < first[i] + count[i]; j++) {
                const Part *p = &ps.p[j];

                b[o] = (uint8_t)p->u;
                b[o + 1] = (uint8_t)p->v;
                b[o + 2] = (uint8_t)(p->lx + bx[i]);
                b[o + 3] = (uint8_t)(p->ly + by[i]);
                put16(b, o + 4, (unsigned)(p->row << 6) | (p->blend ? 0x8000u : 0));
                put16(b, o + 6, (unsigned)p->page | (unsigned)(p->blend ? (p->blend - 1) << 5 : 0));
                put16(b, o + 8, (unsigned)p->w);
                put16(b, o + 10, (unsigned)p->h);
                put16(b, o + 16, 0x1000); /* the four words the files carry after the part (nothing reads them) */
                put16(b, o + 18, 0x1000);
                o += 20;
            }
        }
    }
    at += bankSize;
    for (a = 0; a < PT_ANIMS; a++) {
        if (animBytes[a] == 0) continue;
        out->anim[a] = (uint32_t)at;
        put16(b, at, 0x124);
        put16(b, at + 2, (unsigned)src->anim[a].n);
        for (k = 0; k < src->anim[a].n; k++) {
            const PtStep *s = &src->anim[a].step[k];
            size_t o = at + 4 + 8 * (size_t)k;

            put16(b, o, (unsigned)s->frame);
            b[o + 2] = (uint8_t)s->time;
            put16(b, o + 4, (unsigned)(s->x - bx[s->frame]) & 0xFFFF);
            put16(b, o + 6, (unsigned)(s->y - by[s->frame]) & 0xFFFF);
        }
        at += animBytes[a];
    }
    for (i = 0; i < nPages; i++) {
        out->image[i] = (uint32_t)at;
        at += timWrite(b, at, &pg[i], i);
        out->rows[i] = pg[i].nrows;
    }
    /* the set's header: count, bank, 6 animations, the images, then where the sprites start in the bank and the bank's size */
    put32(b, setAt, (uint32_t)(7 + nPages));
    put32(b, setAt + 4, out->bank);
    for (a = 0; a < PT_ANIMS; a++) put32(b, setAt + 8 + 4 * (size_t)a, out->anim[a]);
    for (i = 0; i < nPages; i++) put32(b, setAt + 0x20 + 4 * (size_t)i, out->image[i]);
    put32(b, setAt + 4 * (size_t)(7 + nPages + 1), out->bankOffset);
    put32(b, setAt + 4 * (size_t)(7 + nPages + 2), out->bankSize);
    out->file = b;
    out->len = total;
    out->nImages = nPages;
    out->sprites = src->nFrames;
    out->parts = ps.n;
    b = NULL;
    rc = 0;
done:
    free(b);
    for (i = 0; i < ps.n; i++) free(ps.p[i].idx);
    free(ps.p);
    for (i = 0; i < PT_PAGES; i++) free(pg[i].px);
    free(order);
    free(bx);
    free(by);
    free(first);
    free(count);
    return rc;
}

/* The frames as coarse parts; when they do not fit (frames that carry pixels the original shared between sprites, as the effect pages' or one body under many
   heads), as tiles of 16 and then of 8 pixels, which store what the frames repeat once */
int ptBuild(const PtSource *src, PtSet *out, char *why, size_t cap) {
    static const int tiles[] = {0, 16, 8};
    char first[240];
    int i, rc = -1;

    for (i = 0; i < 3; i++) {
        rc = buildOnce(src, out, tiles[i], why, cap);
        if (i == 0) snprintf(first, sizeof first, "%s", why != NULL ? why : "");
        if (rc != -2) return rc;
    }
    if (why != NULL && cap > 0) snprintf(why, cap, "%s (tiles of 16 and 8 pixels neither)", first);
    return -1;
}

void ptView(const PtSet *s, PcTrainView *v) {
    int i;

    memset(v, 0, sizeof *v);
    v->bank = s->file + s->bank;
    v->bankOffset = (int32_t)s->bankOffset;
    v->bankSize = (int32_t)s->bankSize;
    for (i = 0; i < PT_ANIMS; i++) v->anim[i] = s->anim[i] != 0 ? s->file + s->anim[i] : NULL;
    for (i = 0; i < s->nImages; i++) v->image[i] = s->file + s->image[i];
    v->nImages = s->nImages;
}

/* ---- the partners' sets ---- */

typedef struct Own {
    int partner;
    PtSet set[PT_TRAININGS];
    int has[PT_TRAININGS];
} Own;

static Own *sOwn;
static int sOwnN;
static char sNames[PT_TRAININGS][64];
static int sHaveNames;
static int (*sBase)(int partner);
static unsigned char sLogged[PT_PARTNERS][PT_TRAININGS];

void ptSetNames(char names[PT_TRAININGS][64]) {
    int i;

    sHaveNames = 0;
    for (i = 0; i < PT_TRAININGS; i++) {
        snprintf(sNames[i], sizeof sNames[i], "%s", names != NULL ? names[i] : "");
        if (sNames[i][0] != '\0') sHaveNames = 1;
    }
}

void ptSetBase(int (*base)(int partner)) {
    sBase = base;
}

int ptTrainingOfRow(int row) {
    return row < 1 ? 0 : (row - 1) % PT_TRAININGS;
}

static void namesFromDisc(void) {
    const PfsFile *f = pfsDiscIsOpen() ? pfsFindPath("AAA/DAT/COUNTRY/ENG/ESTRAINI.BIN") : NULL;
    uint8_t *bytes = NULL;
    char names[PT_TRAININGS][64];

    if (f != NULL) bytes = pfsDiscRead(f->lba, (size_t)f->size);
    peTrainFolders(bytes, bytes != NULL ? (size_t)f->size : 0, names);
    free(bytes);
    ptSetNames(names);
}

int ptTrainingNamed(const char *name) {
    int i;

    if (!sHaveNames) namesFromDisc();
    for (i = 0; name != NULL && i < PT_TRAININGS; i++) {
        if (sNames[i][0] != '\0' && strcasecmp(sNames[i], name) == 0) return 1;
    }
    return 0;
}

void ptReset(int first) {
    int i, t, keep = 0;

    for (i = 0; i < sOwnN; i++) {
        if (sOwn[i].partner >= first) {
            for (t = 0; t < PT_TRAININGS; t++) ptSetFree(&sOwn[i].set[t]);
        } else {
            sOwn[keep++] = sOwn[i];
        }
    }
    sOwnN = keep;
    if (first <= 0) {
        free(sOwn);
        sOwn = NULL;
    }
    memset(sLogged, 0, sizeof sLogged);
}

typedef struct Folder {
    const char *dir;
    const char *key, *name;
    int edited; /* a file the manifest does not vouch for (data/ only) */
    int checkEdit;
} Folder;

static int readFolderFile(void *ud, const char *name, uint8_t **bytes, size_t *len) {
    Folder *f = (Folder *)ud;
    char path[1500], rel[200];

    if (snprintf(path, sizeof path, "%s/%s", f->dir, name) >= (int)sizeof path) return -1;
    if (tdReadFile(path, bytes, len) != 0) return -1;
    if (f->checkEdit && snprintf(rel, sizeof rel, "%s/training/%s/%s", f->key, f->name, name) < (int)sizeof rel && pbDataFileEdited(rel, *bytes, *len)) f->edited = 1;
    return 0;
}

static Own *ownFor(int partner) {
    int i;
    Own *np;

    for (i = 0; i < sOwnN; i++) {
        if (sOwn[i].partner == partner) return &sOwn[i];
    }
    np = (Own *)realloc(sOwn, (size_t)(sOwnN + 1) * sizeof *sOwn);
    if (np == NULL) return NULL;
    sOwn = np;
    memset(&sOwn[sOwnN], 0, sizeof sOwn[sOwnN]);
    sOwn[sOwnN].partner = partner;
    return &sOwn[sOwnN++];
}

int ptLoadPartner(int partner, const char *key, int original) {
    Own *own;
    int t, built = 0, i, n;

    if (partner < 0 || partner >= PT_PARTNERS || key == NULL) return 0;
    if (!sHaveNames) namesFromDisc();
    own = ownFor(partner);
    if (own == NULL) return 0;
    for (t = 0; t < PT_TRAININGS; t++) {
        ptSetFree(&own->set[t]);
        own->has[t] = 0;
    }
    n = assetLayerCount();
    for (t = 0; t < PT_TRAININGS; t++) {
        char rel[200];

        if (sNames[t][0] == '\0' || snprintf(rel, sizeof rel, "%s/training/%s/training.json", key, sNames[t]) >= (int)sizeof rel) continue;
        for (i = 0; i < n && !own->has[t]; i++) {
            AssetLayer l;
            char path[1500], dir[1500], why[240];
            Folder f;
            PtSource src;
            PtSet set;
            char *slash;

            if (!assetLayerAt(i, &l) || !assetLayerFile(&l, "partners", rel, path, sizeof path)) continue;
            snprintf(dir, sizeof dir, "%s", path);
            slash = strrchr(dir, '/');
            if (slash == NULL) continue;
            *slash = '\0';
            memset(&f, 0, sizeof f);
            f.dir = dir;
            f.key = key;
            f.name = sNames[t];
            f.checkEdit = original && l.mod < 0;
            if (f.checkEdit) {
                /* the files of an exported folder as the exporter wrote them: only read and hashed, nothing decoded unless one changed */
                if (ptParse(readFolderFile, &f, 0, &src, why, sizeof why) == 0) ptSourceFree(&src);
                else f.edited = 1; /* it does not read: let the real parse say why */
                if (!f.edited) continue;
            }
            if (ptParse(readFolderFile, &f, 1, &src, why, sizeof why) != 0) {
                plogLine(TAG, "%s rejected: %s\n", path, why);
                continue;
            }
            if (ptBuild(&src, &set, why, sizeof why) != 0) {
                plogLine(TAG, "%s rejected: %s\n", path, why);
                ptSourceFree(&src);
                continue;
            }
            ptSourceFree(&src);
            own->set[t] = set;
            own->has[t] = 1;
            built++;
            PLOG(TAG, PLOG_MODS, "partner %d (%s): training %s from %s: %d sprites, %d parts, %d + %d palette rows, %d page(s)\n", partner, key, sNames[t], l.label, set.sprites,
                 set.parts, set.rows[0], set.rows[1], set.nImages);
        }
    }
    return built;
}

int pcTrainSetFor(int row, int partner, PcTrainView *view, int *discSet) {
    int t = ptTrainingOfRow(row), p = partner, hops, i;

    for (hops = 0; hops < 8; hops++) {
        for (i = 0; i < sOwnN; i++) {
            if (sOwn[i].partner == p && sOwn[i].has[t]) {
                ptView(&sOwn[i].set[t], view);
                if (hops > 0 && partner >= 0 && partner < PT_PARTNERS && !sLogged[partner][t]) {
                    sLogged[partner][t] = 1;
                    plogLine(TAG, "partner %d has no training %s of its own: it shows partner %d's\n", partner, sNames[t], p);
                }
                return 1;
            }
        }
        if (p < 8) break;
        i = sBase != NULL ? sBase(p) : 0;
        if (i == p || i < 0) {
            p = 0;
            break;
        }
        p = i;
    }
    if (p < 0 || p > 7) p = 0;
    *discSet = p;
    if (partner >= 8 && partner < PT_PARTNERS && !sLogged[partner][t]) {
        sLogged[partner][t] = 1;
        plogLine(TAG, "partner %d has no training %s of its own: it shows partner %d's (the game's)\n", partner, sNames[t], p);
    }
    return 0;
}

int ptStartOriginals(void) {
    int p, n = 0;

    ptReset(0);
    for (p = 0; p < 8; p++) {
        const char *key = pbOriginalKey(p);

        if (key != NULL) n += ptLoadPartner(p, key, 1) > 0;
    }
    return n;
}

int ptLoadMod(int partner, const char *key) {
    return ptLoadPartner(partner, key, 0);
}
