/* The menus by piece (readable-assets A5.2): see menu_export.h. */
#include "menu_export.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exp_core.h"
#include "image_data.h"
#include "port_log.h"

#define TAG "menus"
#define SECTOR_RAW 2352
#define SECTOR_USER 2048
#define SECTOR_USER_AT 24
#define PAGE_HALFWORDS 64 /* the width of a page in the screen's VRAM space, in both texture modes */

/* ---- text builder ---- */

typedef struct Sb {
    char *p;
    size_t n, cap;
    int fail;
} Sb;

static void sbf(Sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sbf(Sb *b, const char *fmt, ...) {
    va_list ap;
    int w;

    if (b->fail) return;
    for (;;) {
        va_start(ap, fmt);
        w = vsnprintf(b->p ? b->p + b->n : NULL, b->cap - b->n, fmt, ap);
        va_end(ap);
        if (w < 0) {
            b->fail = 1;
            return;
        }
        if (b->p && (size_t)w < b->cap - b->n) {
            b->n += (size_t)w;
            return;
        }
        {
            size_t nc = b->cap ? b->cap * 2 : 4096;
            char *g;

            while (nc < b->n + (size_t)w + 1) nc *= 2;
            g = (char *)realloc(b->p, nc);
            if (!g) {
                b->fail = 1;
                return;
            }
            b->p = g;
            b->cap = nc;
        }
    }
}

/* ---- the pieces of a screen ---- */

/* one place a piece is drawn: slot k of frame `frame` (the sheet's id) at (x, y) from the sprite's origin, with the frame's semi-transparency rate */
typedef struct Use {
    int frame, slot, x, y, rate;
} Use;

typedef struct Piece {
    ImgPart part;      /* the rectangle, the texture mode and the CLUT position as the sheet names them */
    int row;           /* the CLUT row its frame adds to the part's (the palette is at clutY + row) */
    int frame, slot;   /* the first use: the name of the piece */
    const char *why;   /* NULL: it has a PNG; else the reason it has none */
    int outside;       /* texels of the rectangle that fall outside the pages (they are index 0) */
    Use *use;
    int nUse, capUse;
} Piece;

typedef struct Pieces {
    Piece *v;
    int n, cap;
} Pieces;

static void piecesFree(Pieces *ps) {
    int i;

    for (i = 0; i < ps->n; i++) free(ps->v[i].use);
    free(ps->v);
    memset(ps, 0, sizeof *ps);
}

static Piece *pieceFind(Pieces *ps, const ImgPart *part, int row) {
    int i;

    for (i = 0; i < ps->n; i++) {
        const Piece *q = &ps->v[i];

        if (q->part.u == part->u && q->part.v == part->v && q->part.w == part->w && q->part.h == part->h && q->part.mode == part->mode &&
            q->part.clutX == part->clutX && q->part.clutY + q->row == part->clutY + row) {
            return &ps->v[i];
        }
    }
    return NULL;
}

/* the piece of `part` drawn with a frame that adds `row` to its CLUT row; made when no earlier use named the same rectangle and palette. NULL when memory ran out. */
static Piece *pieceFor(Pieces *ps, const ImgPart *part, int row, int frame, int slot) {
    Piece *p = pieceFind(ps, part, row);

    if (p != NULL) return p;
    if (ps->n == ps->cap) {
        Piece *g = (Piece *)realloc(ps->v, sizeof *g * (size_t)(ps->cap ? ps->cap * 2 : 64));

        if (g == NULL) return NULL;
        ps->v = g;
        ps->cap = ps->cap ? ps->cap * 2 : 64;
    }
    p = &ps->v[ps->n++];
    memset(p, 0, sizeof *p);
    p->part = *part;
    p->row = row;
    p->frame = frame;
    p->slot = slot;
    return p;
}

static int pieceUse(Piece *p, const Use *u) {
    if (p->nUse == p->capUse) {
        Use *g = (Use *)realloc(p->use, sizeof *g * (size_t)(p->capUse ? p->capUse * 2 : 4));

        if (g == NULL) return -1;
        p->use = g;
        p->capUse = p->capUse ? p->capUse * 2 : 4;
    }
    p->use[p->nUse++] = *u;
    return 0;
}

/* Every part of every frame, in the sheet's order: the pieces and where each is used. 0, or -1 when memory ran out. */
static int piecesCollect(Pieces *ps, const uint8_t *cs, size_t csLen) {
    int nFrames = imgSheetFrameCount(cs, csLen), f, k;

    for (f = 0; f < nFrames; f++) {
        ImgFrame fr;

        if (imgSheetFrame(cs, csLen, f, &fr) != 0) continue;
        for (k = 0; k < fr.parts; k++) {
            ImgPart part;
            Piece *p;
            Use u;
            int pi, x, y;

            if (imgSheetFramePart(cs, csLen, f, k, &pi, &x, &y) != 0 || imgSheetPart(cs, csLen, pi, &part) != 0) continue; /* not a part of the sheet */
            p = pieceFor(ps, &part, fr.row, fr.id, k);
            u.frame = fr.id;
            u.slot = k;
            u.x = x;
            u.y = y;
            u.rate = fr.rate;
            if (p == NULL || pieceUse(p, &u) != 0) return -1;
        }
    }
    return 0;
}

/* ---- one PNG per piece ---- */

/* The indexed image of a piece: its pixels cut from the pages and its own palette, in the disc's index order. 0 on success; else NULL px and why says why. */
static int pieceImage(const ImgPages *pages, Piece *p, ImgPic *pic, const char **why) {
    uint16_t pal[256];
    int n = p->part.mode ? 256 : 16, i, outside = 0;

    memset(pic, 0, sizeof *pic);
    if (p->part.w <= 0 || p->part.h <= 0) {
        *why = "empty rectangle";
        return -1;
    }
    if (imgPartPalette(pages, &p->part, p->row, pal, NULL) != 0) {
        *why = "palette outside the pages";
        return -1;
    }
    pic->w = p->part.w;
    pic->h = p->part.h;
    pic->depth = p->part.mode ? 8 : 4;
    pic->nColors = n;
    pic->px = (uint8_t *)malloc((size_t)pic->w * (size_t)pic->h);
    if (pic->px == NULL) {
        *why = "out of memory";
        return -1;
    }
    imgPartPixels(pages, &p->part, pic->px, &outside);
    if (outside == pic->w * pic->h) {
        imgPicFree(pic);
        *why = "no pixels on the pages";
        return -1;
    }
    p->outside = outside;
    for (i = 0; i < n; i++) imgColor(pal[i], pic->rgb + 3 * i, pic->alpha + i);
    return 0;
}

int menuSheetPieces(const uint8_t *cs, size_t csLen, MenuPiece **out) {
    Pieces ps = {NULL, 0, 0};
    int i;

    *out = NULL;
    if (piecesCollect(&ps, cs, csLen) != 0) {
        piecesFree(&ps);
        return -1;
    }
    if (ps.n > 0) *out = (MenuPiece *)malloc(sizeof **out * (size_t)ps.n);
    if (ps.n > 0 && *out == NULL) {
        piecesFree(&ps);
        return -1;
    }
    for (i = 0; i < ps.n; i++) {
        (*out)[i].part = ps.v[i].part;
        (*out)[i].row = ps.v[i].row;
        (*out)[i].frame = ps.v[i].frame;
        (*out)[i].slot = ps.v[i].slot;
    }
    i = ps.n;
    piecesFree(&ps);
    return i;
}

void menuPieceFile(const MenuPiece *p, char *out, size_t cap) { snprintf(out, cap, "f%03dp%02d.png", p->frame, p->slot); }

const char *menuPieceReason(const ImgPages *pages, const MenuPiece *mp) {
    Piece p;
    ImgPic pic;
    const char *why = NULL;

    memset(&p, 0, sizeof p);
    p.part = mp->part;
    p.row = mp->row;
    if (pieceImage(pages, &p, &pic, &why) != 0) return why;
    imgPicFree(&pic);
    return NULL;
}

/* ---- menu.json ---- */

static const char *blendName(int rate) {
    static const char *const names[] = {"average", "add", "subtract", "quarter"};

    if (rate < 0) return "none";
    return rate < 4 ? names[rate] : "unknown";
}

static void pieceJson(Sb *b, const Piece *p, int first) {
    int perHalf = p->part.mode ? 2 : 4, page = p->part.u >= 0 ? p->part.u / (perHalf * PAGE_HALFWORDS) : 0, i;

    sbf(b, "%s\n  { ", first ? "" : ",");
    if (p->why == NULL) sbf(b, "\"file\": \"f%03dp%02d.png\"", p->frame, p->slot);
    else sbf(b, "\"file\": null, \"unexported\": \"%s\"", p->why);
    sbf(b, ", \"page\": %d, \"u\": %d, \"v\": %d, \"w\": %d, \"h\": %d, \"mode\": \"%dbit\", ", page, p->part.u, p->part.v, p->part.w, p->part.h,
        p->part.mode ? 8 : 4);
    sbf(b, "\"clut\": { \"x\": %d, \"row\": %d, \"page\": %d }, ", p->part.clutX, p->part.clutY + p->row,
        p->part.clutX >= 0 ? p->part.clutX / PAGE_HALFWORDS : 0);
    if (p->outside > 0) sbf(b, "\"outside\": true, ");
    sbf(b, "\"frames\": [");
    for (i = 0; i < p->nUse; i++) {
        const Use *u = &p->use[i];

        sbf(b, "%s{ \"frame\": %d, \"slot\": %d, \"at\": [%d, %d], \"blend\": \"%s\" }", i ? ", " : " ", u->frame, u->slot, u->x, u->y, blendName(u->rate));
    }
    sbf(b, " ] }");
}

static const char kMenuNotes[] =
    "One PNG per piece: a rectangle of the screen's art with the palette the game draws it with (16 colours, or 256 for mode 8bit; index "
    "0 and every colour the disc leaves as 0x0000 are transparent). The PNG keeps the disc's index order: edit its pixels and colours, keep its "
    "size. A piece is replaced by another of the same size, drawn where the game draws this one. pages: the size of each page of the screen in halfwords "
    "(width, height); the game loads them side by side, 64 halfwords per page. u and v are texels of that space (page is where the top-left corner falls: "
    "u / 256 for 4bit, u / 128 for 8bit); clut is the palette's column x (halfwords, the screen's space), row and page, the frame's CLUT row included. "
    "frames lists where the game draws the piece: the frame (its id in the sheet), the slot (the part's position in the frame: slot 0 is drawn "
    "last, on top), at (x, y) from the sprite's origin and the blend of the frame (none, or how it is mixed with what is behind: average, add, subtract, "
    "quarter). outside: true means the rectangle reaches past the pages (those pixels are index 0). A piece with file null cannot be cut and says why; "
    "it is listed so no part of the sheet is dropped silently. Information only: regenerated from the disc.";

static void docJson(Sb *b, const MenuPieceDoc *doc, const ImgPages *pages, const Pieces *ps) {
    int k, w, h, i;

    sbf(b, "{\n \"format\": \"%s\",\n \"name\": \"%s\",\n", doc->format, doc->name);
    sbf(b, " \"disc\": { %s },\n", doc->disc);
    if (doc->unexported != NULL) sbf(b, " \"unexported\": \"%s\",\n", doc->unexported);
    sbf(b, " \"pages\": [");
    for (k = 0; pages != NULL && k < imgPagesCount(pages); k++) {
        if (imgPageSize(pages, k, &w, &h) == 0) sbf(b, "%s[%d, %d]", k ? ", " : "", w, h);
        else sbf(b, "%snull", k ? ", " : "");
    }
    sbf(b, "],\n \"pieces\": [");
    for (i = 0; i < ps->n; i++) pieceJson(b, &ps->v[i], i == 0);
    sbf(b, "%s],\n", ps->n ? "\n " : "");
    sbf(b, " \"notes\": \"%s\"\n}\n", doc->notes);
}

/* ---- one screen ---- */

int menuScreenName(const char *discPath, char *out, size_t cap) {
    const char *file = strrchr(discPath, '/');
    size_t n, i;

    if (!imgIsScreenFile(discPath) || file == NULL) return 0;
    file++;
    n = strlen(file);
    if (n <= 6 || strcmp(file + n - 6, "TM.BIN") != 0 || n - 6 + 1 > cap) return 0;
    for (i = 0; i < n - 6; i++) out[i] = (char)tolower((unsigned char)file[i]);
    out[n - 6] = '\0';
    return 1;
}

int menuPiecesExport(const ImgPages *pages, const uint8_t *sheet, size_t sheetLen, const MenuPieceDoc *doc, SprEmitFn emit, void *ctx, MenuStats *st) {
    Pieces ps = {NULL, 0, 0};
    MenuStats local = {0, 0, 0, 0};
    Sb js;
    char file[24];
    int i, rc = -1;

    memset(&js, 0, sizeof js);
    if (doc->unexported == NULL) {
        if (pages == NULL || imgSheetFrameCount(sheet, sheetLen) == 0) goto done;
        if (piecesCollect(&ps, sheet, sheetLen) != 0) {
            local.failed++;
            goto done;
        }
    }
    for (i = 0; i < ps.n; i++) {
        Piece *p = &ps.v[i];
        ImgPic pic;
        uint8_t *png;
        size_t pl;

        if (pieceImage(pages, p, &pic, &p->why) != 0) {
            local.unexported++;
            continue;
        }
        snprintf(file, sizeof file, "f%03dp%02d.png", p->frame, p->slot);
        if (imgPicToPng(&pic, &png, &pl) != 0) {
            local.failed++;
            p->why = "the PNG could not be made";
        } else {
            if (emit(ctx, file, png, pl) != 0) local.failed++;
            else local.pieces++;
            free(png);
        }
        if (p->outside > 0) local.outside++;
        imgPicFree(&pic);
    }
    docJson(&js, doc, pages, &ps);
    if (js.fail || emit(ctx, doc->jsonFile, (const uint8_t *)js.p, js.n) != 0) local.failed++;
    rc = local.failed == 0 ? local.pieces : -1;
done:
    free(js.p);
    piecesFree(&ps);
    if (st != NULL) {
        st->pieces += local.pieces;
        st->unexported += local.unexported;
        st->outside += local.outside;
        st->failed += local.failed;
    }
    return rc;
}

int menuScreenExport(const char *name, const char *tmDisc, const char *csDisc, const uint8_t *tm, size_t tmLen, const uint8_t *cs, size_t csLen,
                     SprEmitFn emit, void *ctx, MenuStats *st) {
    ImgPages *pages = imgPagesOpen(tm, tmLen);
    char disc[256]; /* without "AAA/" */
    MenuPieceDoc doc = {"menus-menu-v1", name, disc, NULL, "menu.json", kMenuNotes};
    int rc;

    snprintf(disc, sizeof disc, "\"files\": [\"%s\", \"%s\"]", tmDisc + 4, csDisc + 4);
    rc = menuPiecesExport(pages, cs, csLen, &doc, emit, ctx, st);
    imgPagesFree(pages);
    return rc;
}

/* ---- the disc side ---- */

typedef struct DiscFile {
    char path[112];
    long lba, size;
} DiscFile;

static char sDataDir[1024];
static MenuExportIo sIo;
static MenuExportStats sTotals;

void menuExportSetup(const char *dataDir, const MenuExportIo *io) {
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

void menuExportTotals(MenuExportStats *out) { *out = sTotals; }

uint8_t *menuDiscRead(const MenuExportIo *io, long lba, long size) {
    long sectors = (size + SECTOR_USER - 1) / SECTOR_USER, i;
    uint8_t *buf, frame[SECTOR_RAW];

    if (size <= 0 || io->read == NULL) return NULL;
    buf = (uint8_t *)malloc((size_t)sectors * SECTOR_USER);
    if (buf == NULL) return NULL;
    for (i = 0; i < sectors; i++) {
        if (!io->read(io->ud, lba + i, frame)) {
            free(buf);
            return NULL;
        }
        memcpy(buf + (size_t)i * SECTOR_USER, frame + SECTOR_USER_AT, SECTOR_USER);
    }
    return buf;
}

static uint8_t *readFile(const DiscFile *f) { return menuDiscRead(&sIo, f->lba, f->size); }

/* one screen of the disc: its two files and its folder */
typedef struct Screen {
    char name[48];
    DiscFile tm, cs;
    int want;
} Screen;

typedef struct Disc {
    Screen *v;
    int n;
} Disc;

static const DiscFile *findFile(const DiscFile *f, int n, const char *path) {
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(f[i].path, path) == 0) return &f[i];
    }
    return NULL;
}

/* The screens of the disc: every *TM file that imgIsScreenFile names, paired with its sheet (a screen without one is logged and left out). */
static int discScreens(Disc *d) {
    int cap = 4096, n = 0, i;
    DiscFile *all = (DiscFile *)malloc(sizeof *all * (size_t)cap);

    d->v = NULL;
    d->n = 0;
    if (all == NULL) return -1;
    for (i = 0; i < cap; i++) {
        DiscFile *x = &all[n];

        if (!sIo.list(sIo.ud, i, x->path, sizeof x->path, &x->lba, &x->size)) break;
        n++;
    }
    d->v = (Screen *)calloc((size_t)(n ? n : 1), sizeof *d->v);
    if (d->v == NULL) {
        free(all);
        return -1;
    }
    for (i = 0; i < n; i++) {
        Screen *s = &d->v[d->n];
        char sheet[112];
        const DiscFile *cs;

        if (!menuScreenName(all[i].path, s->name, sizeof s->name)) continue;
        if (!imgSheetPathOf(all[i].path, sheet, sizeof sheet) || (cs = findFile(all, n, sheet)) == NULL) {
            plogLine(TAG, "%s has no sheet on the disc; left out\n", all[i].path);
            continue;
        }
        s->tm = all[i];
        s->cs = *cs;
        d->n++;
    }
    free(all);
    return 0;
}

/* "all", or screen names separated by commas or spaces; sets .want, returns how many are wanted */
static int parseSpec(const char *spec, Disc *d) {
    char tok[64];
    int named = 0, i;
    const char *s = spec;

    for (i = 0; i < d->n; i++) d->v[i].want = 0;
    while (*s) {
        size_t n = 0;

        while (*s == ',' || *s == ' ') s++;
        while (*s && *s != ',' && *s != ' ' && n + 1 < sizeof tok) tok[n++] = (char)tolower((unsigned char)*s++);
        tok[n] = '\0';
        if (n == 0) break;
        if (strcmp(tok, "all") == 0) {
            for (i = 0; i < d->n; i++) d->v[i].want = 1;
            continue;
        }
        for (i = 0; i < d->n && strcmp(d->v[i].name, tok) != 0; i++) {
        }
        if (i < d->n) d->v[i].want = 1;
        else plogLine(TAG, "export_menus: \"%s\" is not a screen of the disc (use all, or names like stmcrd separated by commas)\n", tok);
    }
    for (i = 0; i < d->n; i++) named += d->v[i].want;
    return named;
}

typedef struct Emit {
    const ExpSink *sink;
    char prefix[64];
    int failed;
} Emit;

static int emitFn(void *ctx, const char *rel, const uint8_t *data, size_t len) {
    Emit *e = (Emit *)ctx;
    char path[160];

    snprintf(path, sizeof path, "%s/%s", e->prefix, rel);
    if (expPut(e->sink, path, data, len)) {
        e->failed++;
        return -1;
    }
    return 0;
}

static const char *kExporterKey = "menus";

/* a run of the current version whose files are all there (the partner bundles' gym effect sets share this manifest: they are checked too, a missing one
   only makes the run happen again): nothing to build */
static int upToDate(const ExpManifest *man, const char *dir) {
    char path[1400];
    int i;

    if (!expManIsCurrent(man, kExporterKey, MENU_EXPORT_VERSION)) return 0;
    for (i = 0; i < man->m.count; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, man->m.files[i].path);
        if (!expFileExists(path)) return 0;
    }
    return 1;
}

int menuExportRun(const char *spec) {
    Disc disc = {NULL, 0};
    ExpManifest man;
    ExpStats es;
    ExpSink sink;
    Emit em;
    MenuStats ms;
    char dir[1100], mp[1200];
    int s, nWant, rc = -1, wantAll, manLoaded = 0;

    memset(&sTotals, 0, sizeof sTotals);
    memset(&ms, 0, sizeof ms);
    if (!sDataDir[0] || sIo.list == NULL || sIo.read == NULL || spec == NULL) return -1;
    if (discScreens(&disc) != 0) goto done;
    nWant = parseSpec(spec, &disc);
    if (nWant == 0) goto done;
    wantAll = strstr(spec, "all") != NULL;
    snprintf(dir, sizeof dir, "%s/menus", sDataDir);
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    expManOpen(&man, TAG, mp, "menus");
    manLoaded = 1;
    if (upToDate(&man, dir)) {
        sTotals.skipped = 1;
        rc = 0;
        goto done;
    }
    memset(&es, 0, sizeof es);
    expSinkInPlace(&sink, TAG, dir, &man, &es);
    memset(&em, 0, sizeof em);
    em.sink = &sink;
    for (s = 0; s < disc.n; s++) {
        const Screen *sc = &disc.v[s];
        uint8_t *tm, *cs;
        char tmDisc[120];

        if (!sc->want) continue;
        tm = readFile(&sc->tm);
        cs = readFile(&sc->cs);
        snprintf(em.prefix, sizeof em.prefix, "%s", sc->name);
        snprintf(tmDisc, sizeof tmDisc, "%s", sc->tm.path);
        if (tm == NULL || cs == NULL || menuScreenExport(sc->name, tmDisc, sc->cs.path, tm, (size_t)sc->tm.size, cs, (size_t)sc->cs.size, emitFn, &em, &ms) < 0) {
            plogLine(TAG, "cannot cut %s into pieces\n", sc->name);
            ms.failed++;
        } else {
            sTotals.screens++;
        }
        free(tm);
        free(cs);
    }
    sTotals.failed = ms.failed;
    sTotals.written = es.written;
    sTotals.kept = es.kept;
    sTotals.newFiles = es.newFiles;
    sTotals.bytes = es.bytes;
    sTotals.pieces = ms.pieces;
    sTotals.unexported = ms.unexported;
    sTotals.outside = ms.outside;
    if (sTotals.failed == 0 && wantAll) expManMark(&man, kExporterKey, MENU_EXPORT_VERSION);
    if (expManSave(&man, 0)) sTotals.failed++;
    plogLine(TAG, "Done: %d screens in %s/menus, %d pieces (%d with pixels past their pages), %d listed without a PNG, %d written, %d kept, %d new beside edited, %d failed, %ld bytes\n",
             sTotals.screens, sDataDir, sTotals.pieces, sTotals.outside, sTotals.unexported, sTotals.written, sTotals.kept, sTotals.newFiles, sTotals.failed, sTotals.bytes);
    rc = sTotals.screens;
done:
    if (manLoaded) expManFree(&man);
    free(disc.v);
    return rc;
}
