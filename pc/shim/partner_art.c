#include "partner_art.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void why(char *w, size_t cap, const char *fmt, ...)
{
    va_list ap;

    if (!w || !cap) return;
    va_start(ap, fmt);
    vsnprintf(w, cap, fmt, ap);
    va_end(ap);
}

/* the part both converters share: the palette check and the 4-bit packing of the whole image */
static int convert(const PngImage *p, PaArt *out, int cw, int ch, int cells, char *w, size_t cap)
{
    int x, y, i;

    if (p->nPal > 16) {
        why(w, cap, "at most 16 colours, it has %d", p->nPal);
        return -1;
    }
    for (i = 0; i < p->w * p->h; i++) {
        if (p->px[i] >= p->nPal) {
            why(w, cap, "a pixel uses the colour %d, past the palette of %d", p->px[i], p->nPal);
            return -1;
        }
    }
    out->rowHw = p->w / 4;
    out->img = (uint16_t *)calloc((size_t)ch * (size_t)out->rowHw, sizeof(uint16_t));
    if (out->img == NULL) {
        why(w, cap, "out of memory");
        return -1;
    }
    out->cells = cells;
    out->w = cw;
    out->h = ch;
    for (i = 0; i < 16; i++) {
        unsigned v = 0;

        if (i < p->nPal && (i >= p->nAlpha || p->alpha[i] != 0)) {
            v = (unsigned)(p->rgb[3 * i] >> 3) | (unsigned)(p->rgb[3 * i + 1] >> 3) << 5 | (unsigned)(p->rgb[3 * i + 2] >> 3) << 10;
            if (v == 0) v = 0x8000;
        }
        out->pal[i] = (uint16_t)v;
    }
    for (y = 0; y < ch; y++) {
        for (x = 0; x < p->w; x++) out->img[y * out->rowHw + x / 4] |= (uint16_t)(p->px[y * p->w + x] << ((x & 3) * 4));
    }
    return 0;
}

typedef struct Cluster {
    double w, r, g, b; /* pixels and the sums of channel * pixels */
    int first;         /* lowest source index, for a stable order */
    int alive;
} Cluster;

static double clusterDist(const Cluster *a, const Cluster *b) {
    double dr = a->r / a->w - b->r / b->w, dg = a->g / a->w - b->g / b->w, db = a->b / a->w - b->b / b->w;

    return a->w * b->w / (a->w + b->w) * (dr * dr + dg * dg + db * db);
}

int paReduce(const PngImage *in, PngImage *out, int maxColours) {
    Cluster cl[256];
    int where[256], map[256], used[256] = {0}, transp[256] = {0};
    int i, n = 0, nOpaque = 0, hasT = 0, budget, live, nOut = 0, firstT = -1;
    size_t px, npx = (size_t)in->w * (size_t)in->h;

    memset(out, 0, sizeof *out);
    if (in->px == NULL || npx == 0 || in->nPal < 1 || in->nPal > 256 || maxColours < 1) return -1;
    for (px = 0; px < npx; px++) {
        if (in->px[px] >= in->nPal) return -1;
        used[in->px[px]]++;
    }
    for (i = 0; i < in->nPal; i++) {
        if (used[i] && i < in->nAlpha && in->alpha[i] == 0) {
            transp[i] = 1;
            hasT = 1;
            if (firstT < 0) firstT = i;
        }
    }
    for (i = 0; i < in->nPal; i++) {
        where[i] = -1;
        if (!used[i] || transp[i]) continue;
        cl[n].w = (double)used[i];
        cl[n].r = in->rgb[3 * i] * cl[n].w;
        cl[n].g = in->rgb[3 * i + 1] * cl[n].w;
        cl[n].b = in->rgb[3 * i + 2] * cl[n].w;
        cl[n].first = i;
        cl[n].alive = 1;
        where[i] = n++;
        nOpaque++;
    }
    budget = maxColours - (hasT ? 1 : 0);
    if (budget < 1) return -1;
    live = nOpaque;
    while (live > budget) {
        double best = -1;
        int bi = 0, bj = 0, j;

        for (i = 0; i < n; i++) {
            if (!cl[i].alive) continue;
            for (j = i + 1; j < n; j++) {
                double d;

                if (!cl[j].alive) continue;
                d = clusterDist(&cl[i], &cl[j]);
                if (best < 0 || d < best) {
                    best = d;
                    bi = i;
                    bj = j;
                }
            }
        }
        cl[bi].w += cl[bj].w;
        cl[bi].r += cl[bj].r;
        cl[bi].g += cl[bj].g;
        cl[bi].b += cl[bj].b;
        if (cl[bj].first < cl[bi].first) cl[bi].first = cl[bj].first;
        cl[bj].alive = 0;
        for (i = 0; i < in->nPal; i++) {
            if (where[i] == bj) where[i] = bi;
        }
        live--;
    }
    /* the new order: the transparent colour first, then the clusters by their lowest source index */
    for (i = 0; i < 256; i++) map[i] = 0;
    if (hasT) {
        memcpy(out->rgb, in->rgb + 3 * firstT, 3);
        out->nAlpha = 1;
        out->alpha[0] = 0;
        nOut = 1;
    }
    for (i = 0; i < in->nPal; i++) {
        int c = where[i], k;

        if (c < 0 || cl[c].first != i) continue; /* the cluster is placed where its lowest source index is */
        out->rgb[3 * nOut] = (uint8_t)(cl[c].r / cl[c].w + 0.5);
        out->rgb[3 * nOut + 1] = (uint8_t)(cl[c].g / cl[c].w + 0.5);
        out->rgb[3 * nOut + 2] = (uint8_t)(cl[c].b / cl[c].w + 0.5);
        for (k = 0; k < in->nPal; k++) {
            if (where[k] == c) map[k] = nOut;
        }
        nOut++;
    }
    for (i = 0; i < in->nPal; i++) {
        if (transp[i]) map[i] = 0;
    }
    out->w = in->w;
    out->h = in->h;
    out->depth = 4;
    out->nPal = nOut > 0 ? nOut : 1;
    out->px = (uint8_t *)malloc(npx);
    if (out->px == NULL) return -1;
    for (px = 0; px < npx; px++) out->px[px] = (uint8_t)map[in->px[px]];
    return 0;
}

int paFromIndexed(const PngImage *p, PaArt *out, char *w, size_t cap)
{
    memset(out, 0, sizeof *out);
    if (p->h != PA_CELL) {
        why(w, cap, "the height must be %d (one row of cells), not %d", PA_CELL, p->h);
        return -1;
    }
    if (p->w < PA_CELL || p->w % PA_CELL != 0 || p->w / PA_CELL > PA_CELLS_MAX) {
        why(w, cap, "the width must be 1 to %d cells of %d pixels, not %d", PA_CELLS_MAX, PA_CELL, p->w);
        return -1;
    }
    return convert(p, out, PA_CELL, PA_CELL, p->w / PA_CELL, w, cap);
}

int paIconFromIndexed(const PngImage *p, PaArt *out, char *w, size_t cap)
{
    memset(out, 0, sizeof *out);
    if (p->w != PA_ICON_W || p->h != PA_ICON_H) {
        why(w, cap, "the icon must be exactly %dx%d pixels, not %dx%d", PA_ICON_W, PA_ICON_H, p->w, p->h);
        return -1;
    }
    return convert(p, out, PA_ICON_W, PA_ICON_H, 1, w, cap);
}

int paIconFromPng(const uint8_t *data, size_t len, PaArt *out, char *w, size_t cap)
{
    PngImage p;
    int r;

    memset(out, 0, sizeof *out);
    if (pngReadIndexed(data, len, &p, w, cap) != 0) return -1;
    r = paIconFromIndexed(&p, out, w, cap);
    pngImageFree(&p);
    return r;
}

int paFromPng(const uint8_t *data, size_t len, PaArt *out, char *w, size_t cap)
{
    PngImage p;
    int r;

    memset(out, 0, sizeof *out);
    if (pngReadIndexed(data, len, &p, w, cap) != 0) return -1;
    r = paFromIndexed(&p, out, w, cap);
    pngImageFree(&p);
    return r;
}

int paFromPngFit(const uint8_t *data, size_t len, int icon, PaArt *out, int *reducedFrom, char *w, size_t cap)
{
    PngImage p, r;
    int rc;

    memset(out, 0, sizeof *out);
    if (reducedFrom) *reducedFrom = 0;
    if (pngReadIndexed(data, len, &p, w, cap) != 0) return -1;
    if (p.nPal > 16) {
        if (paReduce(&p, &r, 16) != 0) {
            why(w, cap, "the colours cannot be reduced to 16");
            pngImageFree(&p);
            return -1;
        }
        if (reducedFrom) *reducedFrom = p.nPal;
        pngImageFree(&p);
        p = r;
    }
    rc = icon ? paIconFromIndexed(&p, out, w, cap) : paFromIndexed(&p, out, w, cap);
    pngImageFree(&p);
    return rc;
}

void paFree(PaArt *a)
{
    free(a->img);
    memset(a, 0, sizeof *a);
}

int paSteps(int cells)
{
    return cells < 1 ? 0 : (cells == 1 ? 1 : 2 * cells - 2);
}

int paCellAt(int cells, int step)
{
    int n = paSteps(cells);

    if (step < 0 || step >= n) return 0;
    return step < cells ? step : n - step;
}

int paNext(int cells, int step)
{
    return step + 1 >= paSteps(cells) ? 0 : step + 1;
}

void paSheetBuild(PaSheet *s, int cell, int u, int v, int clutX, int clutY, int x, int y)
{
    paSheetBuildSize(s, cell, PA_CELL, PA_CELL, u, v, clutX, clutY, x, y);
}

void paSheetBuildSize(PaSheet *s, int cell, int cw, int ch, int u, int v, int clutX, int clutY, int x, int y)
{
    memset(s, 0, sizeof *s);
    s->offsets[0] = (int32_t)offsetof(PaSheet, part);
    s->offsets[1] = (int32_t)offsetof(PaSheet, ids);
    s->offsets[2] = (int32_t)offsetof(PaSheet, frame);
    s->frame[0] = 1;
    s->frame[1] = 0;
    s->frame[2] = -1;
    s->frame[3] = 0;
    s->frame[4] = (int16_t)x;
    s->frame[5] = (int16_t)y;
    s->part.u = (int16_t)(u + cell * cw);
    s->part.v = (int16_t)v;
    s->part.w = (int16_t)cw;
    s->part.h = (int16_t)ch;
    s->part.clutX = (int16_t)clutX;
    s->part.clutY = (int16_t)clutY;
    s->part.mode = 0;
}
