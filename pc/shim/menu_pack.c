/* The packer of the pieces (readable-assets A5.3 for the menus, A6.3 for the stage objects): see menu_pack.h. */
#include "menu_pack.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exp_core.h"
#include "image_data.h"
#include "menu_export.h"
#include "mods.h"
#include "partner_art.h"
#include "png_read.h"
#include "port_log.h"
#include "text_data.h"

#define PAGE_HALFWORDS 64 /* the width of a page in the screen's VRAM space, in both texture modes */
#define MAX_PAGES 8       /* as many as imgPagesOpen keeps */

/* ---- what the edited pieces ask for ---- */

/* the indices of a rectangle of the page space (one byte per texel) */
typedef struct Rect {
    int layer, order; /* the layer the piece came from (0 wins) and its place in menu.json */
    char file[24];
    int mode, u, v, w, h;
    uint8_t *px;
} Rect;

/* the colours of a palette at a CLUT position: n entries from column x of row `row` */
typedef struct Pal {
    int layer, order;
    char file[24];
    int x, row, n;
    int alt;                 /* the colours of an 8-bit piece of a stage: row - IMG_ALT_CLUT_ROW of a TIM's CLUT, not of the pages */
    short owner[256];        /* alt only: the CLUT-carrying TIM (in walk order) whose CLUT the game keeps at each colour, -1: none */
    uint8_t rgb[256 * 3], alpha[256];
} Pal;

typedef struct Plan {
    const char *tag, *topic;
    int stage;
    char name[48], dir[64];
    Rect *rect;
    int nRect, capRect;
    Pal *pal;
    int nPal, capPal;
    int pageW[MAX_PAGES], pageH[MAX_PAGES]; /* halfwords of each page of the disc's file, 0: none */
} Plan;

static Plan sPlan;
static int sActive;           /* sPlan has something to write */
static unsigned sSeen;        /* the pages the patch already did (bit k) */
static int sClutTims;         /* the TIMs with a CLUT the patch has gone through (stage), as imgPagesAltOwner counts them */
static MenuPackStats sTotals;
static int sWriteAll;
static int sValidating;       /* --validate-mod: planBuild also reports what a mod ships that no piece takes */

/* <topic>/manifest.json of the data folder, read once each (the stages' lists 8000 files, and the index asks about each: the manifest finds by path in O(1)) */
typedef struct TopicMan {
    char topic[16];
    TdManifest man;
} TopicMan;
static TopicMan sMans[2];

static void planFree(void) {
    int i;

    for (i = 0; i < sPlan.nRect; i++) free(sPlan.rect[i].px);
    free(sPlan.rect);
    free(sPlan.pal);
    memset(&sPlan, 0, sizeof sPlan);
    sActive = 0;
    sSeen = 0;
    sClutTims = 0;
}

static void manifestsFree(void) {
    int i;

    for (i = 0; i < 2; i++) {
        if (sMans[i].topic[0] != '\0') tdManifestFree(&sMans[i].man);
    }
    memset(sMans, 0, sizeof sMans);
}

static const char *layerLabel(int layer, char *buf, size_t cap) {
    AssetLayer l;

    if (!assetLayerAt(layer, &l)) return "?";
    snprintf(buf, cap, "%s", l.mod < 0 ? "data (edited)" : l.label);
    return buf;
}

static int rectCmp(const void *a, const void *b) {
    const Rect *x = (const Rect *)a, *y = (const Rect *)b;

    return x->layer != y->layer ? y->layer - x->layer : x->order - y->order;
}

static int palCmp(const void *a, const void *b) {
    const Pal *x = (const Pal *)a, *y = (const Pal *)b;

    return x->layer != y->layer ? y->layer - x->layer : x->order - y->order;
}

/* The pixels of a piece. A rectangle already asked for (another piece drawn with another CLUT row): the same indices are no news; different ones are
   a conflict that the layer that wins settles (inside one layer, the piece that came first). Takes `px` (w * h bytes). 0, or -1 when memory ran out. */
static int addRect(const MenuPiece *mp, const char *file, int layer, int order, uint8_t *px) {
    Plan *p = &sPlan;
    int i, n = mp->part.w * mp->part.h;
    char a[100], b[100];

    for (i = 0; i < p->nRect; i++) {
        Rect *r = &p->rect[i];

        if (r->mode != mp->part.mode || r->u != mp->part.u || r->v != mp->part.v || r->w != mp->part.w || r->h != mp->part.h) continue;
        if (memcmp(r->px, px, (size_t)n) == 0) {
            free(px);
            return 0;
        }
        sTotals.conflicts++;
        if (layer < r->layer) {
            plogLine(sPlan.tag, "%s: %s (%s) and %s (%s) disagree about the pixels of the same rectangle; the first, of the layer that wins, is written\n",
                     p->name, file, layerLabel(layer, a, sizeof a), r->file, layerLabel(r->layer, b, sizeof b));
            free(r->px);
            r->px = px;
            r->layer = layer;
            r->order = order;
            snprintf(r->file, sizeof r->file, "%s", file);
        } else {
            plogLine(sPlan.tag, "%s: %s (%s) and %s (%s) disagree about the pixels of the same rectangle; the first, %s, is written\n", p->name, r->file,
                     layerLabel(r->layer, a, sizeof a), file, layerLabel(layer, b, sizeof b), r->file);
            free(px);
        }
        return 0;
    }
    if (p->nRect == p->capRect) {
        Rect *g = (Rect *)realloc(p->rect, sizeof *g * (size_t)(p->capRect ? p->capRect * 2 : 32));

        if (g == NULL) {
            free(px);
            return -1;
        }
        p->rect = g;
        p->capRect = p->capRect ? p->capRect * 2 : 32;
    }
    {
        Rect *r = &p->rect[p->nRect++];

        r->layer = layer;
        r->order = order;
        snprintf(r->file, sizeof r->file, "%s", file);
        r->mode = mp->part.mode;
        r->u = mp->part.u;
        r->v = mp->part.v;
        r->w = mp->part.w;
        r->h = mp->part.h;
        r->px = px;
    }
    return 0;
}

static int palEqual(const Pal *a, const Pal *b, int n) {
    int i;

    for (i = 0; i < n; i++) {
        if (a->alpha[i] != b->alpha[i] || (a->alpha[i] != 0 && memcmp(a->rgb + 3 * i, b->rgb + 3 * i, 3) != 0)) return 0;
    }
    return 1;
}

/* The colours of a piece at its CLUT position: same rules as addRect. 0, or -1 when memory ran out. */
static int addPal(const ImgPages *pages, const MenuPiece *mp, const char *file, int layer, int order, const PngImage *png) {
    Plan *p = &sPlan;
    Pal np;
    int i, row = mp->part.clutY + mp->row;
    char a[100], b[100];

    memset(&np, 0, sizeof np);
    np.layer = layer;
    np.order = order;
    snprintf(np.file, sizeof np.file, "%s", file);
    np.x = mp->part.clutX;
    np.row = row;
    np.n = png->nPal;
    np.alt = p->stage && mp->part.mode;
    for (i = 0; i < np.n; i++) {
        memcpy(np.rgb + 3 * i, png->rgb + 3 * i, 3);
        np.alpha[i] = i < png->nAlpha ? png->alpha[i] : 255;
        np.owner[i] = (short)(np.alt ? imgPagesAltOwner(pages, row - IMG_ALT_CLUT_ROW, np.x + i) : -1);
        if (np.alt && np.owner[i] < 0) plogLine(p->tag, "%s: colour %d of %s is in no CLUT of the archive and is not written\n", p->name, i, file);
    }
    for (i = 0; i < p->nPal; i++) {
        Pal *q = &p->pal[i];
        int common;

        if (q->x != np.x || q->row != np.row || q->alt != np.alt) continue;
        common = q->n < np.n ? q->n : np.n;
        if (palEqual(q, &np, common)) { /* the same colours: the longer one says more */
            if (np.n > q->n) {
                np.layer = q->layer < np.layer ? q->layer : np.layer;
                *q = np;
            }
            return 0;
        }
        sTotals.conflicts++;
        if (np.layer < q->layer) {
            plogLine(sPlan.tag, "%s: %s (%s) and %s (%s) disagree about the palette at the same place; the first, of the layer that wins, is written\n", p->name,
                     np.file, layerLabel(np.layer, a, sizeof a), q->file, layerLabel(q->layer, b, sizeof b));
            *q = np;
        } else {
            plogLine(sPlan.tag, "%s: %s (%s) and %s (%s) disagree about the palette at the same place; the first, %s, is written\n", p->name, q->file,
                     layerLabel(q->layer, a, sizeof a), np.file, layerLabel(np.layer, b, sizeof b), q->file);
        }
        return 0;
    }
    if (p->nPal == p->capPal) {
        Pal *g = (Pal *)realloc(p->pal, sizeof *g * (size_t)(p->capPal ? p->capPal * 2 : 32));

        if (g == NULL) return -1;
        p->pal = g;
        p->capPal = p->capPal ? p->capPal * 2 : 32;
    }
    p->pal[p->nPal++] = np;
    return 0;
}

/* ---- one piece ---- */

/* the data folder's own file counts only when the user changed it: its hash is not the one <topic>/manifest.json recorded (a file the manifest does not know is
   the user's too) */
int menuPackDataFileEdited(const char *tag, const char *topic, const char *rel, const uint8_t *bytes, size_t len) {
    char hex[17], path[1300];
    TopicMan *m = NULL;
    int i;

    for (i = 0; i < 2 && m == NULL; i++) {
        if (strcmp(sMans[i].topic, topic) == 0) m = &sMans[i];
    }
    for (i = 0; i < 2 && m == NULL; i++) {
        if (sMans[i].topic[0] == '\0') m = &sMans[i];
    }
    if (m == NULL) return 0;
    if (m->topic[0] == '\0') {
        snprintf(m->topic, sizeof m->topic, "%s", topic);
        snprintf(path, sizeof path, "%s/%s/manifest.json", assetDataDir() != NULL ? assetDataDir() : "", topic);
        if (assetDataDir() != NULL) expManRead(tag, path, &m->man);
    }
    tdHashHex(tdHash(bytes, len), hex);
    return tdFileState(1, tdManifestFind(&m->man, rel), hex) == TD_FILE_EDITED;
}

/* A PNG as the indices and colours of a piece: its own size, at most 16 (256) colours. 0 and a PNG with room for every index of the piece, else -1 and why. */
static int decodePiece(const uint8_t *bytes, size_t len, const MenuPiece *mp, PngImage *out, int *reducedFrom, char *why, size_t cap) {
    PngImage p, r;
    int n = mp->part.mode ? 256 : 16, i, maxIdx = 0;

    *reducedFrom = 0;
    if (pngReadIndexed(bytes, len, &p, why, cap) != 0) return -1;
    if (p.w != mp->part.w || p.h != mp->part.h) {
        snprintf(why, cap, "size %dx%d, the piece is %dx%d", p.w, p.h, mp->part.w, mp->part.h);
        pngImageFree(&p);
        return -1;
    }
    if (p.nPal > n) {
        if (paReduce(&p, &r, n) != 0) {
            snprintf(why, cap, "%d colours cannot be reduced to %d", p.nPal, n);
            pngImageFree(&p);
            return -1;
        }
        *reducedFrom = p.nPal;
        pngImageFree(&p);
        p = r;
    }
    for (i = 0; i < p.w * p.h; i++) {
        if (p.px[i] > maxIdx) maxIdx = p.px[i];
    }
    if (maxIdx >= n) {
        snprintf(why, cap, "pixel index %d out of range, the piece has %d colours", maxIdx, n);
        pngImageFree(&p);
        return -1;
    }
    *out = p;
    return 0;
}

/* What of an accepted PNG differs from the disc: the edits go into the plan. 0, or -1 when memory ran out. */
static int planPiece(const ImgPages *pages, const MenuPiece *mp, const char *file, const char *rel, int layer, int order, PngImage *png) {
    uint16_t discPal[256];
    uint8_t *discPx;
    int n = mp->part.w * mp->part.h, i, pixels, colours = 0, rc = 0;
    char label[100];

    discPx = (uint8_t *)malloc((size_t)n);
    if (discPx == NULL) return -1;
    memset(discPal, 0, sizeof discPal);
    imgPartPixels(pages, &mp->part, discPx, NULL);
    imgPartPalette(pages, &mp->part, mp->row, discPal, NULL);
    pixels = sWriteAll || memcmp(discPx, png->px, (size_t)n) != 0;
    for (i = 0; i < png->nPal && !colours; i++) {
        colours = imgColor16(png->rgb + 3 * i, i < png->nAlpha ? png->alpha[i] : 255, discPal[i]) != discPal[i];
    }
    colours = colours || sWriteAll;
    free(discPx);
    if (!pixels && !colours) {
        sTotals.equal++;
        return 0;
    }
    plogLine(sPlan.tag, "override %s (%s) from %s\n", rel, pixels && colours ? "pixels and colours" : pixels ? "pixels" : "colours",
             layerLabel(layer, label, sizeof label));
    if (pixels) {
        uint8_t *copy = (uint8_t *)malloc((size_t)n);

        if (copy == NULL) return -1;
        memcpy(copy, png->px, (size_t)n);
        rc = addRect(mp, file, layer, order, copy);
    }
    if (rc == 0 && colours) rc = addPal(pages, mp, file, layer, order, png);
    return rc;
}

/* ---- --validate-mod ---- */

static int sameName(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

/* What the log lines of planBuild do not say about the mods' <topic>/<dir>/ folders: a file that no piece of the sheet is called (a misspelt name, or a piece
   of another screen: it is never read) and a piece that two mods ship (the later mod wins, the other one is shadowed). A PNG of the wrong size or one that
   cannot be read is the "rejected" line of planBuild. The data folder is not looked at: its files are the exporter's own. */
static void validateSource(const MenuPiece *pieces, int np) {
    int s, i;

    for (s = 0; s < assetLayerCount(); s++) {
        AssetLayer l;
        char dirPath[1400], file[24];
        DIR *d;
        struct dirent *e;

        if (!assetLayerAt(s, &l) || l.mod < 0 || !assetModDir(l.mod, sPlan.topic, sPlan.dir, dirPath, sizeof dirPath) || (d = opendir(dirPath)) == NULL) continue;
        while ((e = readdir(d)) != NULL) {
            int known = e->d_name[0] == '.' || sameName(e->d_name, "menu.json");

            for (i = 0; i < np && !known; i++) {
                menuPieceFile(&pieces[i], file, sizeof file);
                known = sameName(e->d_name, file);
            }
            if (!known) plogLine(sPlan.tag, "%s/%s ignored: no piece of %s is called that (the pieces are the files of its piece list)\n", dirPath, e->d_name, sPlan.name);
        }
        closedir(d);
    }
    for (i = 0; i < np; i++) {
        char file[24], rel[100], a[100];
        int winner = -1;

        menuPieceFile(&pieces[i], file, sizeof file);
        snprintf(rel, sizeof rel, "%s/%s", sPlan.dir, file);
        for (s = 0; s < assetLayerCount(); s++) { /* layer 0 is the last mod: the first one that has the piece wins */
            AssetLayer l;
            char path[1500];

            if (!assetLayerAt(s, &l) || l.mod < 0 || !assetLayerFile(&l, sPlan.topic, rel, path, sizeof path)) continue;
            if (winner < 0) {
                winner = s;
            } else {
                plogLine(sPlan.tag, "%s ignored: the piece is also in %s, which loads later and wins\n", path, layerLabel(winner, a, sizeof a));
            }
        }
    }
}

/* The plan of one source: every piece of the sheet with a PNG in a layer that counts. 1 when something differs from the disc. */
static int planBuild(const uint8_t *file, size_t fileLen, const uint8_t *sheet, size_t sheetLen) {
    ImgPages *pages = NULL;
    MenuPiece *pieces = NULL;
    int np, i, s, k, ok = 0;

    np = menuSheetPieces(sheet, sheetLen, &pieces);
    if (sValidating) validateSource(pieces, np > 0 ? np : 0); /* a sheet with no pieces (a stage with parts and no frames): every file a mod puts there is reported */
    if (np <= 0) goto done;
    for (i = 0; i < np; i++) {
        const MenuPiece *mp = &pieces[i];
        char name[24], rel[100];

        menuPieceFile(mp, name, sizeof name);
        snprintf(rel, sizeof rel, "%s/%s", sPlan.dir, name);
        for (s = 0; s < assetLayerCount(); s++) {
            AssetLayer l;
            char path[1500], why[200];
            uint8_t *bytes;
            size_t len;
            PngImage png;
            int reduced, bad;
            const char *reason;

            if (!assetLayerAt(s, &l) || !assetLayerFile(&l, sPlan.topic, rel, path, sizeof path) || tdReadFile(path, &bytes, &len) != 0) continue;
            if (l.mod < 0 && !menuPackDataFileEdited(sPlan.tag, sPlan.topic, rel, bytes, len)) {
                free(bytes);
                continue;
            }
            if (pages == NULL && (pages = sPlan.stage ? imgStagePagesOpen(file, fileLen) : imgPagesOpen(file, fileLen)) == NULL) {
                free(bytes);
                goto done;
            }
            if ((reason = menuPieceReason(pages, mp)) != NULL) { /* the exporter listed it without a PNG: nowhere to put one */
                plogLine(sPlan.tag, "%s ignored: the piece has no PNG on the disc (%s)\n", path, reason);
                free(bytes);
                break;
            }
            bad = decodePiece(bytes, len, mp, &png, &reduced, why, sizeof why);
            free(bytes);
            if (bad) {
                plogLine(sPlan.tag, "%s rejected: %s\n", path, why);
                sTotals.rejected++;
                continue;
            }
            sTotals.pieces++;
            if (reduced) {
                plogLine(sPlan.tag, "%s: %d colours reduced to %d\n", path, reduced, mp->part.mode ? 256 : 16);
                sTotals.reduced++;
            }
            k = planPiece(pages, mp, name, rel, s, i, &png);
            pngImageFree(&png);
            if (k != 0) goto done;
            break;
        }
    }
    if (pages != NULL) {
        for (k = 0; k < MAX_PAGES; k++) {
            int w, h;

            if (imgPageSize(pages, k, &w, &h) == 0) {
                sPlan.pageW[k] = w;
                sPlan.pageH[k] = h;
            }
        }
    }
    qsort(sPlan.rect, (size_t)sPlan.nRect, sizeof *sPlan.rect, rectCmp);
    qsort(sPlan.pal, (size_t)sPlan.nPal, sizeof *sPlan.pal, palCmp);
    ok = sPlan.nRect + sPlan.nPal > 0;
done:
    free(pieces);
    imgPagesFree(pages);
    return ok;
}

int menuPackBegin(const PackSource *src, const uint8_t *file, size_t len, const uint8_t *sheet, size_t sheetLen) {
    planFree();
    sPlan.tag = src->tag;
    sPlan.topic = src->topic;
    sPlan.stage = src->stage;
    snprintf(sPlan.name, sizeof sPlan.name, "%s", src->name);
    snprintf(sPlan.dir, sizeof sPlan.dir, "%s", src->dir);
    sTotals.screens++;
    sActive = planBuild(file, len, sheet, sheetLen);
    if (!sActive) planFree();
    return sActive;
}

/* ---- the hooks ---- */

static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* the texels of every rectangle that fall in page k (`img`: iw x ih halfwords), the losing layers first */
static void writeTexels(int k, uint8_t *img, int iw, int ih) {
    int i;

    for (i = 0; i < sPlan.nRect; i++) {
        const Rect *r = &sPlan.rect[i];
        int perHalf = r->mode ? 2 : 4, bits = r->mode ? 8 : 4, cols = iw < PAGE_HALFWORDS ? iw : PAGE_HALFWORDS;
        int lo = k * PAGE_HALFWORDS * perHalf, hi = lo + cols * perHalf, x0 = r->u > lo ? r->u : lo, x1 = r->u + r->w < hi ? r->u + r->w : hi, x, y;
        unsigned mask = r->mode ? 255u : 15u;

        for (y = 0; y < r->h; y++) {
            int v = r->v + y;

            if (v < 0 || v >= ih) continue;
            for (x = x0; x < x1; x++) {
                uint8_t *hw = img + 2 * ((size_t)v * (size_t)iw + (size_t)((x / perHalf) - k * PAGE_HALFWORDS));
                int sh = bits * (x % perHalf);
                unsigned old = le16(hw), now = (old & ~(mask << sh)) | ((unsigned)r->px[y * r->w + (x - r->u)] & mask) << sh;

                sTotals.texelsWritten++;
                if (now != old) {
                    put16(hw, now);
                    sTotals.texelsChanged++;
                }
            }
        }
    }
}

/* the palette halfwords that fall in page k, each with the STP bit the disc has */
static void writeColours(int k, uint8_t *img, int iw, int ih) {
    int i, j;

    for (i = 0; i < sPlan.nPal; i++) {
        const Pal *p = &sPlan.pal[i];

        if (p->alt || p->row < 0 || p->row >= ih) continue;
        for (j = 0; j < p->n; j++) {
            int x = p->x + j, col = x % PAGE_HALFWORDS;
            uint8_t *hw;
            unsigned now;

            if (x < 0 || x / PAGE_HALFWORDS != k || col >= iw) continue;
            hw = img + 2 * ((size_t)p->row * (size_t)iw + (size_t)col);
            now = imgColor16(p->rgb + 3 * j, p->alpha[j], (uint16_t)le16(hw));
            sTotals.halfwordsWritten++;
            if (now != le16(hw)) {
                put16(hw, now);
                sTotals.halfwordsChanged++;
            }
        }
    }
}

/* the colours of the 8-bit pieces that the game keeps from this TIM's CLUT (`clut`: cw x ch halfwords; `ordinal`: its place among the TIMs with a CLUT),
   each with the STP bit the disc has */
static void writeAltColours(uint8_t *clut, int cw, int ch, int ordinal) {
    int i, j;

    for (i = 0; i < sPlan.nPal; i++) {
        const Pal *p = &sPlan.pal[i];
        int r = p->row - IMG_ALT_CLUT_ROW;

        if (!p->alt || r < 0 || r >= ch) continue;
        for (j = 0; j < p->n; j++) {
            int col = p->x + j;
            uint8_t *hw;
            unsigned now;

            if (p->owner[j] != ordinal || col < 0 || col >= cw) continue;
            hw = clut + 2 * ((size_t)r * (size_t)cw + (size_t)col);
            now = imgColor16(p->rgb + 3 * j, p->alpha[j], (uint16_t)le16(hw));
            sTotals.halfwordsWritten++;
            if (now != le16(hw)) {
                put16(hw, now);
                sTotals.halfwordsChanged++;
            }
        }
    }
}

int menuPackPatch(const int *chain, int depth, uint8_t *tim, size_t len) {
    ImgTim t;
    long before = sTotals.texelsChanged + sTotals.halfwordsChanged;
    int k;

    if (!sActive || imgTimParse(tim, len, &t) != 0) return 0;
    if (sPlan.stage && t.clut != NULL && t.mode < 2) { /* a stage's loader sends every CLUT to the same place: the one that wins is the last (imgPagesAltOwner) */
        writeAltColours(tim + (t.clut - tim), t.cw, t.ch, sClutTims++);
    }
    /* the first page image of a number is the page, as imgPagesOpen takes it: for a screen a 16-bit one, for a stage any TIM's */
    if (depth >= 1 && (k = chain[depth - 1]) >= 0 && k < MAX_PAGES && (sSeen & (1u << k)) == 0 && (sPlan.stage || t.mode == 2) && t.iw == sPlan.pageW[k] &&
        t.ih == sPlan.pageH[k]) {
        sSeen |= 1u << k;
        writeTexels(k, tim + (t.img - tim), t.iw, t.ih);
        writeColours(k, tim + (t.img - tim), t.iw, t.ih);
    }
    return sTotals.texelsChanged + sTotals.halfwordsChanged != before;
}

void menuPackEnd(void) { planFree(); }

static int hookBegin(const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet, size_t sheetLen) {
    char name[48];
    PackSource src = {"menus", "menus", name, name, 0};

    planFree();
    if (assetLayerCount() == 0 || sheet == NULL || !menuScreenName(discPath, name, sizeof name)) return 0;
    return menuPackBegin(&src, file, len, sheet, sheetLen);
}

static int hookPatch(const char *discPath, const int *chain, int depth, uint8_t *tim, size_t len) {
    (void)discPath;
    return menuPackPatch(chain, depth, tim, len);
}

static void hookEnd(const char *discPath) {
    (void)discPath;
    menuPackEnd();
}

void menuPackStart(void) {
    ImgMenuHooks h;

    planFree();
    manifestsFree();
    memset(&h, 0, sizeof h);
    h.begin = hookBegin;
    h.patch = hookPatch;
    h.end = hookEnd;
    imgSetMenuHooks(&h);
}

void menuPackReset(void) {
    imgSetMenuHooks(NULL);
    planFree();
    manifestsFree();
    memset(&sTotals, 0, sizeof sTotals);
    sWriteAll = 0;
}

void menuPackTotals(MenuPackStats *out) { *out = sTotals; }

void menuPackWriteUnchanged(int on) { sWriteAll = on; }

void menuPackValidating(int on) { sValidating = on; }
