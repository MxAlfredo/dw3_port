/* The partner bundles (readable-assets A3): see partner_export.h. */
#include "partner_export.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exp_core.h"
#include "port_log.h"
#include "text_codec.h"

#define TAG "partners"
#define SECTOR_RAW 2352
#define SECTOR_USER 2048
#define SECTOR_USER_AT 24

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int s16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static int s32(const uint8_t *p) { return (int32_t)le32(p); }

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

/* ---- the tables the decomp holds ---- */

/* frames of the portrait animation of partner p (STPLNMET_partnerAnims, src/menus/player_name/data/stplnmet.c:825: the cells in the order the animation
   first shows them, then back) and the icon frame (STGMCARD_partnerIcons, src/menus/memory_card/data/stgmcard.c:41). The follower's character (FIELD/SPRT
   P003..P010: pcPartnerFieldCharacter, pc/shim/ext_state.c: the first character + the partner) is named once, in sprite_data.c (sprPartnerFile), because
   the field sprite importer needs the same list. */
static const PeLayout kLayout[PE_PARTNERS] = {
    {7, 4, 42, NULL}, {14, 3, 44, NULL}, {11, 3, 43, NULL}, {3, 4, 41, NULL}, {25, 4, 47, NULL}, {0, 3, 40, NULL}, {17, 4, 45, NULL}, {21, 4, 46, NULL},
};

const PeLayout *peLayout(int partner) {
    static PeLayout full[PE_PARTNERS];
    static int ready;

    if (partner < 0 || partner >= PE_PARTNERS) return NULL;
    if (!ready) {
        int i;

        memcpy(full, kLayout, sizeof full);
        for (i = 0; i < PE_PARTNERS; i++) full[i].fieldFile = sprPartnerFile(i);
        ready = 1;
    }
    return &full[partner];
}

/* STGTRAIN_files rows 1..12 (EU: 606 1086 640 641 642 643 770 771 1087 644 645 772); rows 13..24 are the same files again */
static const char *const kTrainFiles[PE_TRAININGS] = {
    "TRANIN00", "TRANIN01", "TRANIN02", "TRANIN03", "TRANIN04", "TRANIN10",
    "TRANIN11", "TRANIN12", "TRANIN13", "TRANIN14", "TRANIN15", "TRANIN16",
};

const char *peTrainFile(int i) { return i >= 0 && i < PE_TRAININGS ? kTrainFiles[i] : NULL; }

/* The entry of ESTRAINI (COUNTRY/<lang>/ESTRAINI, the gym's text, STGTRAIN_TEXT) that holds the name of the training whose file is kTrainFiles[i]: the `name` of
   STGTRAIN_trainings[i + 1] (src/menus/gym/files.c; row i + 1 because requestFile(training) reads STGTRAIN_files[training], menu.c:20 shows
   STGTRAIN_trainings[training].name and screen.c:572 asks for the file of the same number). Rows 13..24, the "Super" variants, use the same files again. */
static const int kTrainNameText[PE_TRAININGS] = {19, 20, 21, 22, 23, 29, 30, 31, 32, 33, 34, 35};

int peTrainNameEntry(int i) { return i >= 0 && i < PE_TRAININGS ? kTrainNameText[i] : -1; }

/* the screens that draw the portraits: the overlay's name, the sprite sheet file (and its entry), the file of the pages */
typedef struct Screen {
    const char *name, *sheet, *pages;
    int entry;
} Screen;
#define N_SCREENS 7
static const Screen kScreens[N_SCREENS] = {
    {"stplnmet", "SCREEN/NAMEDTCS", "SCREEN/NAMEDTTM", 0}, {"stdgname", "NAMEENT/STDGNMCS", "NAMEENT/STDGNMTM", 0},
    {"stgdglab", "DGLB/STDGLBCS", "DGLB/STDGLBTM", 0},     {"ststatus", "SCREEN/STSTATCS", "SCREEN/STSTATTM", 0},
    {"stgtrain", "TRAINING/STTRNGCS", "TRAINING/STTRNGTM", 3}, {"stfgtrep", "SCREEN/STFRPTCS", "SCREEN/STFRPTTM", 0},
    {"stgmcard", "MCARD/STMCRDCS", "MCARD/STMCRDTM", 0},
};

int peScreenOfPages(const char *discPath, int *entry) {
    int s;

    for (s = 0; discPath != NULL && s < N_SCREENS; s++) {
        char path[96];

        snprintf(path, sizeof path, "AAA/DAT/%s.BIN", kScreens[s].pages);
        if (strcmp(path, discPath) == 0) {
            if (entry != NULL) *entry = kScreens[s].entry;
            return s;
        }
    }
    return -1;
}

/* ---- names ---- */

int peSlug(const char *text, char *out, size_t cap) {
    size_t n = 0;
    int dash = 0;

    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;

        if (isalnum(c) && c < 128) {
            if (dash && n > 0 && n + 1 < cap && n < 48) out[n++] = '-';
            dash = 0;
            if (n + 1 < cap && n < 48) out[n++] = (char)tolower(c);
        } else {
            dash = 1;
        }
    }
    if (n > 0 && out[n - 1] == '-') n--;
    out[n] = '\0';
    return n > 0;
}

/* ---- portrait sheets ---- */

int peSheetFrame(const uint8_t *sh, size_t len, int id, PeFrame *out) {
    uint32_t po, io, fo, i, nParts;
    int n, pi;

    memset(out, 0, sizeof *out);
    if (len < 16) return 0;
    po = le32(sh);
    io = le32(sh + 4);
    if (po < 12 || io <= po || io >= len || (po & 3)) return 0;
    nParts = (io - po) / 14;
    for (i = 0; io + i < len && sh[io + i] != 255; i++) {
        if (sh[io + i] == id) break;
    }
    if (io + i >= len || sh[io + i] == 255 || 8 + 4 * (i + 1) > po) return 0;
    fo = le32(sh + 8 + 4 * i);
    if ((size_t)fo + 12 > len) return 0;
    n = s16(sh + fo);
    if (n < 1 || (size_t)fo + 6 + 6 * (size_t)n > len) return 0;
    pi = s16(sh + fo + 6);
    if (pi < 0 || (uint32_t)pi >= nParts) return 0;
    {
        const uint8_t *a = sh + po + 14 * (size_t)pi;

        out->id = id;
        out->parts = n;
        out->u = s16(a);
        out->v = s16(a + 2);
        out->w = s16(a + 4);
        out->h = s16(a + 6);
        out->clutX = s16(a + 8);
        out->clutY = s16(a + 10);
        out->mode = s16(a + 12);
        out->row = s16(sh + fo + 2);
        out->x = s16(sh + fo + 8);
        out->y = s16(sh + fo + 10);
    }
    return 1;
}

static int pixel4(const ImgTim *t, int x, int y) {
    unsigned v = (unsigned)t->img[2 * ((size_t)y * (size_t)t->iw + (size_t)(x / 4))] | (unsigned)t->img[2 * ((size_t)y * (size_t)t->iw + (size_t)(x / 4)) + 1] << 8;

    return (int)((v >> ((x & 3) * 4)) & 15);
}

int peArtCell(const ImgTim *const *pages, int nPages, const PeFrame *f, uint8_t *px, uint16_t pal[16]) {
    const ImgTim *page;
    int pg, lu, x, y, py, i;

    if (f->mode != 0 || f->w <= 0 || f->h <= 0 || f->u < 0 || f->v < 0 || nPages < 1 || pages[0] == NULL || pages[0]->mode != 2) return 0;
    pg = f->u / 256;
    lu = f->u % 256;
    if (pg >= nPages || pages[pg] == NULL || pages[pg]->mode != 2) return 0;
    page = pages[pg];
    if (lu + f->w > page->iw * 4 || f->v + f->h > page->ih) return 0;
    py = f->clutY + f->row;
    if (f->clutX < 0 || py < 0 || f->clutX + 16 > pages[0]->iw || py >= pages[0]->ih) return 0;
    for (y = 0; y < f->h; y++) {
        for (x = 0; x < f->w; x++) px[y * f->w + x] = (uint8_t)pixel4(page, lu + x, f->v + y);
    }
    for (i = 0; i < 16; i++) {
        const uint8_t *c = pages[0]->img + 2 * ((size_t)py * (size_t)pages[0]->iw + (size_t)f->clutX + (size_t)i);

        pal[i] = (uint16_t)(c[0] | c[1] << 8);
    }
    return 1;
}

static void paletteTo(ImgPic *p, int at, const uint16_t *pal, int n) {
    int i;

    for (i = 0; i < n; i++) imgColor(pal[i], p->rgb + 3 * (at + i), p->alpha + at + i);
}

int peStrip(const uint8_t *const *cells, int n, int cw, int ch, const uint16_t pal[16], ImgPic *out) {
    int c, y;

    memset(out, 0, sizeof *out);
    if (n < 1 || cw < 1 || ch < 1) return -1;
    out->w = cw * n;
    out->h = ch;
    out->depth = 4;
    out->nColors = 16;
    out->px = (uint8_t *)malloc((size_t)out->w * (size_t)out->h);
    if (out->px == NULL) return -1;
    for (c = 0; c < n; c++) {
        for (y = 0; y < ch; y++) memcpy(out->px + (size_t)y * (size_t)out->w + (size_t)c * (size_t)cw, cells[c] + (size_t)y * (size_t)cw, (size_t)cw);
    }
    paletteTo(out, 0, pal, 16);
    return 0;
}

/* ---- gym training sets ---- */

static int tpageOf(const PeTrain *t, int set, int col, const PeTrainSet **src, int *image) {
    const PeTrainSet *s;

    if (col >= 0 && col <= 1 && set != 8) {
        s = &t->set[set];
        *image = col;
    } else if (col >= 2 && col <= 3) { /* the effect set's pages sit two pages to the right (actor.c: pos[0] + 0x80) */
        s = &t->set[8];
        *image = col - 2;
    } else {
        return 0;
    }
    if (!s->present || *image >= s->nImages) return 0;
    *src = s;
    return 1;
}

void peTrainClose(PeTrain *t) {
    int s, i;

    for (s = 0; s < PE_SETS; s++) {
        for (i = 0; i < 4; i++) free(t->set[s].buf[i]);
        free(t->set[s].spr);
    }
    memset(t, 0, sizeof *t);
}

int peTrainOpen(PeTrain *t, const uint8_t *file, size_t len) {
    int s, i, present = 0;

    memset(t, 0, sizeof *t);
    t->file = file;
    t->len = len;
    if (len < 4 * (PE_SETS + 1)) return 0;
    for (s = 0; s < PE_SETS; s++) {
        PeTrainSet *ts = &t->set[s];
        uint32_t o = le32(file + 4 * s), bank, p;
        int count, ok = 1;
        size_t sz;

        if (o == 0 || (size_t)o + 4 > len) continue;
        count = s32(file + o);
        /* 0 words: the set is not in this file (readSet answers 0); a set has 2 words of bank and 6 of animations before its images */
        if (count < 8 || count > 11 || (size_t)o + 4 * (size_t)(count + 3) > len) continue;
        bank = le32(file + o + 4);
        if ((size_t)bank + 6 > len) continue;
        ts->nImages = count - 7;
        for (i = 0; i < 6; i++) {
            uint32_t a = le32(file + o + 8 + 4 * (size_t)i);

            ts->anim[i] = a < len ? a : 0;
        }
        for (i = 0; i < ts->nImages && ok; i++) {
            uint32_t at = le32(file + o + 0x20 + 4 * (size_t)i);
            uint8_t *u;

            if (at >= len) {
                ok = 0;
                break;
            }
            if (len - at >= 8 && memcmp(file + at, "RLEN", 4) == 0) {
                u = imgRlenUnpack(file + at, len - at, &sz);
                if (u == NULL) {
                    ok = 0;
                    break;
                }
                ts->buf[i] = u;
            } else {
                sz = len - at;
                ts->buf[i] = (uint8_t *)malloc(sz);
                if (ts->buf[i] == NULL) {
                    ok = 0;
                    break;
                }
                memcpy(ts->buf[i], file + at, sz);
            }
            if (imgTimParse(ts->buf[i], sz, &ts->tim[i]) || ts->tim[i].mode != 0 || ts->tim[i].clut == NULL || ts->tim[i].cw < 16) ok = 0;
        }
        ts->sprites = (int)(file[bank + 4] | file[bank + 5] << 8);
        p = bank + (uint32_t)s32(file + o + 4 * (size_t)(count + 1)); /* the word after the images: where the sprites start in the bank */
        ts->spr = (uint32_t *)calloc((size_t)ts->sprites + 1, sizeof *ts->spr);
        if (ts->spr == NULL) ok = 0;
        for (i = 0; i < ts->sprites && ok; i++) {
            int n;

            if ((size_t)p + 4 > len) {
                ok = 0;
                break;
            }
            n = s32(file + p);
            if (n < 0 || n > 64 || (size_t)p + 4 + 20 * (size_t)n > len) {
                ok = 0;
                break;
            }
            ts->spr[i] = p;
            p += 4 + 20 * (uint32_t)n;
        }
        if (!ok) { /* a set that does not read the way the game reads it is not exported */
            for (i = 0; i < 4; i++) {
                free(ts->buf[i]);
                ts->buf[i] = NULL;
            }
            free(ts->spr);
            memset(ts, 0, sizeof *ts);
            plogLine(TAG, "set %d of a training file does not parse; skipped\n", s);
            continue;
        }
        ts->present = 1;
        present++;
    }
    return present;
}

typedef struct Slot {
    const ImgTim *tim;
    int row;
    int blend; /* 0 opaque, else 1 + the blend mode (abr, tpage bits 5..6) of a semi-transparent part: the same palette row drawn both ways is two slots */
} Slot;

int peTrainFrame(const PeTrain *t, int set, int sprite, PeTrainFrame *out) {
    const PeTrainSet *ts;
    const uint8_t *f = t->file;
    uint32_t p;
    int n, i, nSlots = 0, base = 0, minx = 1 << 30, miny = 1 << 30, maxx = -1, maxy = -1, pass;
    Slot slots[16];
    int ok[64];

    memset(out, 0, sizeof *out);
    if (set < 0 || set >= PE_SETS || !t->set[set].present) return 0;
    ts = &t->set[set];
    if (sprite < 0 || sprite >= ts->sprites) return 0;
    p = ts->spr[sprite];
    n = s32(f + p);
    out->parts = n;
    /* pass 0: which parts can be drawn, the box and the palettes they use; pass 1: the pixels */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < n; i++) {
            const uint8_t *a = f + p + 4 + 20 * (size_t)i;
            int u = a[0], v = a[1], x = a[2], y = a[3], clut = a[4] | a[5] << 8, tp = a[6] | a[7] << 8, w = a[8] | a[9] << 8, h = a[10] | a[11] << 8;
            const PeTrainSet *src = NULL;
            int image = 0, row = (clut & 0x7FC0) >> 6, blend = (clut & 0x8000) ? 1 + ((tp >> 5) & 3) : 0, k, px, py;

            if (pass == 0) {
                ok[i] = 0;
                if (w <= 0 || h <= 0) continue;
                if (((tp >> 7) & 3) != 0 || !tpageOf(t, set, tp & 0x1F, &src, &image) || row >= src->tim[image].ch || u + w > src->tim[image].iw * 4 ||
                    v + h > src->tim[image].ih) {
                    out->missing++;
                    continue;
                }
                for (k = 0; k < nSlots; k++) {
                    if (slots[k].tim == &src->tim[image] && slots[k].row == row && slots[k].blend == blend) break;
                }
                if (k == nSlots) {
                    if (nSlots == 16) {
                        out->missing++;
                        continue;
                    }
                    slots[nSlots].tim = &src->tim[image];
                    slots[nSlots].row = row;
                    slots[nSlots].blend = blend;
                    nSlots++;
                }
                ok[i] = 1;
                if ((tp & 0x1F) >= 2 && set != 8) out->shared++;
                if (clut & 0x8000) out->semi = 1;
                if (x < minx) minx = x;
                if (y < miny) miny = y;
                if (x + w > maxx) maxx = x + w;
                if (y + h > maxy) maxy = y + h;
                continue;
            }
            if (!ok[i]) continue;
            tpageOf(t, set, tp & 0x1F, &src, &image);
            {
                const ImgTim *tim = &src->tim[image];
                for (k = 0; slots[k].tim != tim || slots[k].row != row || slots[k].blend != blend; k++) {
                }
                for (py = 0; py < h; py++) {
                    for (px = 0; px < w; px++) {
                        int idx = pixel4(tim, u + px, v + py);
                        const uint8_t *c = tim->clut + 2 * ((size_t)row * (size_t)tim->cw + (size_t)idx);

                        if ((c[0] | c[1]) == 0) continue; /* colour 0x0000: nothing is drawn (the PS1's transparent) */
                        out->pic.px[(size_t)(y - miny + py) * (size_t)out->pic.w + (size_t)(x - minx + px)] = (uint8_t)(base + 16 * k + idx);
                    }
                }
            }
        }
        if (pass == 0) {
            int ncol;

            if (nSlots == 0) return 1; /* nothing visible */
            out->ox = minx;
            out->oy = miny;
            out->pic.w = maxx - minx;
            out->pic.h = maxy - miny;
            /* index 0 is the background: transparent. A palette whose entry 0 is a visible colour pushes every palette up by one. */
            base = (slots[0].tim->clut[2 * (size_t)slots[0].row * (size_t)slots[0].tim->cw] | slots[0].tim->clut[2 * (size_t)slots[0].row * (size_t)slots[0].tim->cw + 1]) != 0;
            ncol = base + 16 * nSlots;
            out->nSlots = nSlots;
            out->base = base;
            for (i = 0; i < nSlots; i++) out->slotBlend[i] = slots[i].blend;
            if (ncol > 256) {
                out->missing = n;
                memset(&out->pic, 0, sizeof out->pic);
                return 1;
            }
            out->pic.nColors = ncol <= 16 ? 16 : ncol;
            out->pic.depth = ncol <= 16 ? 4 : 8;
            out->pic.px = (uint8_t *)calloc((size_t)out->pic.w * (size_t)out->pic.h, 1);
            if (out->pic.px == NULL) {
                out->missing = n;
                memset(&out->pic, 0, sizeof out->pic);
                return 1;
            }
            out->pic.alpha[0] = 0;
            for (i = 0; i < nSlots; i++) {
                uint16_t pal[16];
                int e;

                for (e = 0; e < 16; e++) {
                    const uint8_t *c = slots[i].tim->clut + 2 * ((size_t)slots[i].row * (size_t)slots[i].tim->cw + (size_t)e);

                    pal[e] = (uint16_t)(c[0] | c[1] << 8);
                }
                paletteTo(&out->pic, base + 16 * i, pal, 16);
            }
        }
    }
    return 1;
}

void peTrainFrameFree(PeTrainFrame *f) {
    imgPicFree(&f->pic);
    memset(f, 0, sizeof *f);
}

int peTrainEmit(const PeTrain *t, int set, const char *format, const char *name, const char *discId, const char *discFile, SprEmitFn emit, void *ctx,
                PeTrainStats *st) {
    const PeTrainSet *ts;
    Sb b;
    PeTrainStats loc;
    int j, a, rc = 0, firstFrame = 1;

    if (set < 0 || set >= PE_SETS || !t->set[set].present) return -1;
    ts = &t->set[set];
    memset(&b, 0, sizeof b);
    memset(&loc, 0, sizeof loc);
    sbf(&b, "{\n \"format\": \"%s\",\n \"name\": \"%s\",\n \"disc\": { ", format, name);
    if (discId != NULL) sbf(&b, "\"id\": \"%s\", ", discId);
    sbf(&b, "\"files\": [\"%s\"], \"set\": %d },\n \"frames\": [", discFile, set);
    for (j = 0; j < ts->sprites; j++) {
        PeTrainFrame fr;
        uint8_t *png = NULL;
        size_t pl = 0;
        char rel[32];
        int wrote = 0;

        if (!peTrainFrame(t, set, j, &fr)) continue;
        loc.frames++;
        loc.missing += fr.missing;
        if (fr.pic.px != NULL) {
            snprintf(rel, sizeof rel, "%03d.png", j);
            if (imgPicToPng(&fr.pic, &png, &pl) == 0 && emit(ctx, rel, png, pl) == 0) {
                wrote = 1;
            } else {
                loc.failed++;
                rc = -1;
            }
            free(png);
        } else if (fr.parts > 0 && fr.missing == fr.parts) {
            loc.failed++;
        } else {
            loc.empty++;
        }
        sbf(&b, "%s\n  { \"id\": %d, ", firstFrame ? "" : ",", j);
        firstFrame = 0;
        if (wrote) {
            sbf(&b, "\"image\": \"%03d.png\", \"w\": %d, \"h\": %d, \"origin\": [%d, %d], ", j, fr.pic.w, fr.pic.h, fr.ox, fr.oy);
        } else {
            sbf(&b, "\"image\": null, ");
        }
        if (wrote) sbf(&b, "\"paletteBase\": %d, ", fr.base);
        sbf(&b, "\"parts\": %d, \"effectParts\": %d, \"semiTransparent\": %s, \"blend\": [", fr.parts, fr.shared, fr.semi ? "true" : "false");
        if (wrote) {
            int k, n = 0;

            for (k = 0; k < fr.nSlots; k++) {
                if (fr.slotBlend[k] == 0) continue;
                sbf(&b, "%s{ \"indices\": [%d, %d], \"mode\": %d }", n++ ? ", " : "", fr.base + 16 * k, fr.base + 16 * k + 15, fr.slotBlend[k] - 1);
            }
        }
        sbf(&b, "] }");
        peTrainFrameFree(&fr);
    }
    sbf(&b, "%s],\n \"animations\": [", firstFrame ? "" : "\n ");
    firstFrame = 1;
    for (a = 0; a < 6; a++) {
        const uint8_t *an;
        int n, k;

        if (ts->anim[a] == 0 || (size_t)ts->anim[a] + 4 > t->len) continue;
        an = t->file + ts->anim[a];
        n = s16(an + 2);
        if (n < 0 || (size_t)ts->anim[a] + 4 + 8 * (size_t)n > t->len) continue;
        sbf(&b, "%s\n  { \"id\": %d, \"steps\": [", firstFrame ? "" : ",", a);
        firstFrame = 0;
        for (k = 0; k < n; k++) {
            const uint8_t *s = an + 4 + 8 * (size_t)k;

            sbf(&b, "%s{ \"frame\": %d, \"time\": %d, \"x\": %d, \"y\": %d }", k ? ", " : "", s16(s), s[2], s16(s + 4), s16(s + 6));
        }
        sbf(&b, "] }");
    }
    sbf(&b, "%s],\n \"notes\": \"One PNG per sprite of the set (the id is its index in the set; a sprite with no visible part has no image). A sprite is "
            "drawn from its parts, later parts over earlier ones; origin is where the image's top-left corner sits relative to the sprite's origin, in "
            "pixels. An animation step shows a frame for time ticks of the game's clock and places the sprite's origin at (x, y) relative to the "
            "Digimon's position; the last step is held. A sprite of a partner may use pixels of the shared effect set (effectParts counts them); "
            "an image with several palettes keeps each at its own 16 indices, from paletteBase (index 0 is transparent). blend lists the palettes of an "
            "image (ranges of its indices) the game draws blended with what is behind them, by mode: 0 average, 1 add, 2 subtract, 3 a quarter added; the "
            "picture itself keeps their colours opaque. The port reads this file back (the training of a mod's partner, or an edited original): frames "
            "are placed by origin, an id without an image is an empty sprite, a step's frame is an id.\"\n}\n",
        firstFrame ? "" : "\n ");
    if (b.fail || emit(ctx, "training.json", (const uint8_t *)b.p, b.n) != 0) rc = -1;
    free(b.p);
    if (st != NULL) {
        st->frames += loc.frames;
        st->empty += loc.empty;
        st->failed += loc.failed;
        st->missing += loc.missing;
    }
    return rc;
}

/* ---- the disc side ---- */

typedef struct DiscFile {
    char path[112];
    long lba, size;
} DiscFile;

static char sDataDir[1024];
static PartnerExportIo sIo;
static PartnerExportStats sTotals;

void partnerExportSetup(const char *dataDir, const PartnerExportIo *io) {
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

void partnerExportTotals(PartnerExportStats *out) { *out = sTotals; }

static uint8_t *readFile(const DiscFile *f) {
    long sectors = (f->size + SECTOR_USER - 1) / SECTOR_USER, i;
    uint8_t *buf, frame[SECTOR_RAW];

    if (f->size <= 0) return NULL;
    buf = (uint8_t *)malloc((size_t)sectors * SECTOR_USER);
    if (buf == NULL) return NULL;
    for (i = 0; i < sectors; i++) {
        if (!sIo.read(sIo.ud, f->lba + i, frame)) {
            free(buf);
            return NULL;
        }
        memcpy(buf + (size_t)i * SECTOR_USER, frame + SECTOR_USER_AT, SECTOR_USER);
    }
    return buf;
}

typedef struct Disc {
    DiscFile *f;
    int n;
} Disc;

static int discLoad(Disc *d) {
    int cap = 4096, i;

    d->n = 0;
    d->f = (DiscFile *)malloc(sizeof *d->f * (size_t)cap);
    if (d->f == NULL) return -1;
    for (i = 0; i < cap; i++) {
        DiscFile *x = &d->f[d->n];

        if (!sIo.list(sIo.ud, i, x->path, sizeof x->path, &x->lba, &x->size)) break;
        d->n++;
    }
    return 0;
}

static const DiscFile *discFind(const Disc *d, const char *path) {
    int i;

    for (i = 0; i < d->n; i++) {
        if (strcmp(d->f[i].path, path) == 0) return &d->f[i];
    }
    return NULL;
}

/* "AAA/DAT/<dir>/<name>.BIN" from "<dir>/<name>" */
static const DiscFile *discDat(const Disc *d, const char *dirName, uint8_t **bytes, size_t *len) {
    char path[128];
    const DiscFile *f;

    snprintf(path, sizeof path, "AAA/DAT/%s.BIN", dirName);
    f = discFind(d, path);
    *bytes = NULL;
    if (f == NULL) return NULL;
    *bytes = readFile(f);
    *len = *bytes != NULL ? (size_t)f->size : 0;
    return *bytes != NULL ? f : NULL;
}

typedef struct Emit {
    const ExpSink *sink;
    char prefix[160];
    int files, failed;
} Emit;

static int emitFn(void *ctx, const char *rel, const uint8_t *data, size_t len) {
    Emit *e = (Emit *)ctx;
    char path[300];

    snprintf(path, sizeof path, "%s%s%s", e->prefix, e->prefix[0] ? "/" : "", rel);
    e->files++;
    if (expPut(e->sink, path, data, len)) {
        e->failed++;
        return -1;
    }
    return 0;
}

static int putPng(Emit *e, const char *rel, const ImgPic *pic) {
    uint8_t *png;
    size_t pl;
    int rc;

    if (imgPicToPng(pic, &png, &pl)) {
        e->failed++;
        return -1;
    }
    rc = emitFn(e, rel, png, pl);
    free(png);
    return rc;
}

/* the 29 cells of one screen's sheet, cut from its pages */
typedef struct ScreenArt {
    int ok;
    uint8_t cell[29][32 * 32];
    uint16_t pal[29][16];
    int x[29], y[29];
    uint8_t icon[8][44 * 28];
    uint16_t iconPal[8][16];
} ScreenArt;

typedef struct PageSet {
    uint8_t *buf[4];
    ImgTim tim[4];
    const ImgTim *ptr[4];
    int n;
} PageSet;

static int pageVisit(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len) {
    PageSet *ps = (PageSet *)ctx;
    int k = chain[depth - 1];
    uint8_t *copy;

    if (k < 0 || k >= 4 || ps->buf[k] != NULL) return 0;
    copy = (uint8_t *)malloc(len);
    if (copy == NULL) return 0;
    memcpy(copy, tim, len);
    if (imgTimParse(copy, len, &ps->tim[k]) != 0 || ps->tim[k].mode != 2) {
        free(copy);
        return 0;
    }
    ps->buf[k] = copy;
    ps->ptr[k] = &ps->tim[k];
    if (k + 1 > ps->n) ps->n = k + 1;
    return 0;
}

static void screenArt(const Disc *d, const Screen *sc, int wantIcons, ScreenArt *art) {
    uint8_t *cs = NULL, *tm = NULL;
    size_t csLen = 0, tmLen = 0;
    PageSet ps;
    int f, i, ok = 1;
    uint32_t at;

    memset(&ps, 0, sizeof ps);
    memset(art, 0, sizeof *art);
    if (discDat(d, sc->sheet, &cs, &csLen) == NULL || discDat(d, sc->pages, &tm, &tmLen) == NULL) goto done;
    if (csLen < 4 * (size_t)(sc->entry + 1)) goto done;
    at = le32(cs + 4 * (size_t)sc->entry);
    if (at >= csLen) goto done;
    imgWalk(tm, tmLen, pageVisit, &ps);
    for (f = 0; f < 29 && ok; f++) {
        PeFrame fr;

        if (!peSheetFrame(cs + at, csLen - at, f, &fr) || fr.parts != 1 || fr.w != 32 || fr.h != 32 ||
            !peArtCell(ps.ptr, ps.n, &fr, art->cell[f], art->pal[f])) {
            ok = 0;
        } else {
            art->x[f] = fr.x;
            art->y[f] = fr.y;
        }
    }
    for (i = 0; wantIcons && i < 8 && ok; i++) {
        PeFrame fr;

        if (!peSheetFrame(cs + at, csLen - at, 40 + i, &fr) || fr.parts != 1 || fr.w != 44 || fr.h != 28 ||
            !peArtCell(ps.ptr, ps.n, &fr, art->icon[i], art->iconPal[i])) {
            ok = 0;
        }
    }
    art->ok = ok;
done:
    for (i = 0; i < 4; i++) free(ps.buf[i]);
    free(cs);
    free(tm);
}

/* The slug of text entry entry[i] of a text file (COUNTRY/ENG/...) for each i, "" where it cannot be read (ADR-016 section 2: the folder is the slug of the
   English name on the user's own disc; nothing of the game's text is kept in the source). */
void peSlugsFromText(const uint8_t *bytes, size_t len, const int *entry, int n, char (*out)[64]) {
    TcDoc doc;
    char err[80];
    TcLang *lang;
    int i;

    for (i = 0; i < n; i++) out[i][0] = '\0';
    if (bytes == NULL || tcDocParse(bytes, len, &doc, err, sizeof err) != 0) return;
    lang = tcDocLang(&doc, -1);
    for (i = 0; lang != NULL && i < n; i++) {
        char *text = entry[i] >= 0 && entry[i] < lang->count ? tcEntryText(lang, entry[i]) : NULL;

        if (text != NULL) {
            peSlug(text, out[i], 64);
            free(text);
        }
    }
    tcDocFree(&doc);
}

/* the English names of the eight: entry p + 1 of the English DIGNAM (ADR-016 open question 10), as slugs; "" where one cannot be read */
void peNamesFromText(const uint8_t *bytes, size_t len, char key[PE_PARTNERS][64]) {
    int entry[PE_PARTNERS], p;

    for (p = 0; p < PE_PARTNERS; p++) entry[p] = p + 1;
    peSlugsFromText(bytes, len, entry, PE_PARTNERS, key);
}

static void partnerNames(const Disc *d, char key[PE_PARTNERS][64]) {
    uint8_t *bytes = NULL;
    size_t len = 0;

    if (discDat(d, "COUNTRY/ENG/ESDIGNAM", &bytes, &len) == NULL) bytes = NULL;
    peNamesFromText(bytes, len, key);
    free(bytes);
}

/* the 12 training folders: the slug of the training's English name (ESTRAINI), "training-<row>" where it cannot be read, and "-2", "-3" for a repeat */
void peTrainFolders(const uint8_t *bytes, size_t len, char names[PE_TRAININGS][64]) {
    int i, j, k;

    peSlugsFromText(bytes, len, kTrainNameText, PE_TRAININGS, names);
    for (i = 0; i < PE_TRAININGS; i++) {
        if (names[i][0] == '\0') snprintf(names[i], 64, "training-%d", i + 1);
        for (j = 0, k = 1; j < i; j++) {
            if (strcmp(names[j], names[i]) == 0) {
                char base[48];

                snprintf(base, sizeof base, "%.40s", names[i]);
                snprintf(names[i], 64, "%s-%d", base, ++k);
                j = -1;
            }
        }
    }
}

static void trainingFolders(const Disc *d, char names[PE_TRAININGS][64]) {
    uint8_t *bytes = NULL;
    size_t len = 0;

    if (discDat(d, "COUNTRY/ENG/ESTRAINI", &bytes, &len) == NULL) bytes = NULL;
    peTrainFolders(bytes, len, names);
    free(bytes);
}

typedef struct Plan {
    char train[PE_TRAININGS][64];
    char key[PE_PARTNERS][64];
    char title[PE_PARTNERS][64];
    int want[PE_PARTNERS];
} Plan;

static void titleOf(const char *key, char *out, size_t cap) {
    size_t i;

    for (i = 0; key[i] && i + 1 < cap; i++) out[i] = key[i];
    out[i] = '\0';
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
}

/* "all", or names separated by commas or spaces */
static int parseSpec(const char *spec, const Plan *plan, int want[PE_PARTNERS]) {
    char tok[80];
    int named = 0, i;
    const char *p = spec;

    memset(want, 0, sizeof(int) * PE_PARTNERS);
    while (*p) {
        size_t n = 0;

        while (*p == ',' || *p == ' ') p++;
        while (*p && *p != ',' && *p != ' ' && n + 1 < sizeof tok) tok[n++] = (char)tolower((unsigned char)*p++);
        tok[n] = '\0';
        if (n == 0) continue;
        if (strcmp(tok, "all") == 0) {
            for (i = 0; i < PE_PARTNERS; i++) want[i] = 1;
            named++;
            continue;
        }
        for (i = 0; i < PE_PARTNERS; i++) {
            if (strcmp(tok, plan->key[i]) == 0) {
                want[i] = 1;
                named++;
                break;
            }
        }
        if (i == PE_PARTNERS) plogLine(TAG, "export_partners: \"%s\" is not a partner (use all, or names like agumon separated by commas)\n", tok);
    }
    return named;
}

static const char *kExporterKey = "partners";

/* a run of the current version whose files are all there: nothing to build */
static int upToDate(const ExpManifest *part, const ExpManifest *menus, const char *partDir, const char *menuDir) {
    char path[1400];
    int i;

    if (!expManIsCurrent(part, kExporterKey, PARTNER_EXPORT_VERSION)) return 0;
    for (i = 0; i < part->m.count; i++) {
        snprintf(path, sizeof path, "%s/%s", partDir, part->m.files[i].path);
        if (!expFileExists(path)) return 0;
    }
    for (i = 0; i < menus->m.count; i++) {
        snprintf(path, sizeof path, "%s/%s", menuDir, menus->m.files[i].path);
        if (!expFileExists(path)) return 0;
    }
    return 1;
}

static void partnerJson(Sb *b, int p, const char *key, const char *title, const ScreenArt *art, char (*train)[64]) {
    const PeLayout *lay = peLayout(p);
    int c, t;

    sbf(b, "{\n \"format\": \"partners-partner-v1\",\n \"name\": \"%s\",\n", key);
    sbf(b, " \"title\": \"%s\",\n", title);
    sbf(b, " \"disc\": { \"id\": \"dw3:partner/%d\", \"files\": [\"DAT/FIELD/SPRT/%s.BIN\", \"DAT/MCARD/STMCRDCS.BIN\", \"DAT/MCARD/STMCRDTM.BIN\"] },\n", p,
        lay->fieldFile);
    sbf(b, " \"firstForm\": \"dw3:digimon/%d\",\n", p);
    sbf(b, " \"portrait\": { \"file\": \"portrait.png\", \"cell\": [32, 32], \"cells\": %d, \"animation\": [", lay->portraitCells);
    for (c = 0; c < lay->portraitCells; c++) sbf(b, "%s%d", c ? ", " : "", c);
    for (c = lay->portraitCells - 2; c > 0; c--) sbf(b, ", %d", c);
    sbf(b, "], \"offsets\": [");
    for (c = 0; c < lay->portraitCells; c++) sbf(b, "%s[%d, %d]", c ? ", " : "", art->x[lay->portraitFirst + c], art->y[lay->portraitFirst + c]);
    sbf(b, "], \"sheetFrames\": [");
    for (c = 0; c < lay->portraitCells; c++) sbf(b, "%s%d", c ? ", " : "", lay->portraitFirst + c);
    sbf(b, "], \"screens\": [");
    for (c = 0; c < N_SCREENS; c++) sbf(b, "%s\"%s\"", c ? ", " : "", kScreens[c].name);
    sbf(b, "] },\n \"icon\": { \"file\": \"icon.png\", \"size\": [44, 28], \"sheetFrame\": %d },\n", lay->iconFrame);
    sbf(b, " \"field\": { \"directory\": \"field\", \"format\": \"sprite-v1\", \"character\": \"%s\" },\n \"training\": [", lay->fieldFile);
    for (t = 0; t < PE_TRAININGS; t++) {
        sbf(b, "%s\n  { \"name\": \"%s\", \"directory\": \"training/%s\", \"set\": %d, \"rows\": [%d, %d] }", t ? "," : "", train[t], train[t], p, t + 1, t + 13);
    }
    sbf(b, "\n ],\n \"notes\": \"Everything 2D the game draws of this partner, each file once. portrait.png: the cells of the menus' portrait animation side by side "
           "(the animation shows them in the order of animation and back); offsets are where each cell sits in its frame, as the game places it. icon.png: the "
           "memory card icon. field: the follower sprite of the field (sprite-v1). training: the gym's training scene, one folder per training "
           "(named like the training; rows are the training numbers of the gym that use it: the second one is its Super variant); the effects every partner shares are in menus/gym/effects/. The "
           "first form is a Digimon of its own (model and techs) and is named here, not copied. Information only: regenerated from the disc.\"\n}\n");
}

int partnerExportRun(const char *spec) {
    Disc disc = {NULL, 0};
    Plan *plan = (Plan *)calloc(1, sizeof *plan);
    ScreenArt *art = (ScreenArt *)calloc(N_SCREENS, sizeof *art);
    ExpManifest part, menus;
    ExpStats ps, ms;
    ExpSink psink, msink;
    Emit pe, me;
    char partDir[1100], menuDir[1100], mp[1200];
    int p, t, s, rc = -1, wantAll = 0, nWant = 0, failed = 0, partsLoaded = 0;
    PeTrainStats trainSt;

    memset(&sTotals, 0, sizeof sTotals);
    memset(&part, 0, sizeof part);
    memset(&menus, 0, sizeof menus);
    memset(&trainSt, 0, sizeof trainSt);
    if (plan == NULL || art == NULL || !sDataDir[0] || sIo.list == NULL || sIo.read == NULL || spec == NULL) goto done;
    if (discLoad(&disc) != 0) goto done;
    partnerNames(&disc, plan->key);
    trainingFolders(&disc, plan->train);
    for (p = 0; p < PE_PARTNERS; p++) {
        if (plan->key[p][0] == '\0') {
            snprintf(plan->key[p], sizeof plan->key[p], "partner-%d", p);
            plogLine(TAG, "cannot read the English name of partner %d; its folder is %s\n", p, plan->key[p]);
        }
        titleOf(plan->key[p], plan->title[p], sizeof plan->title[p]);
    }
    nWant = parseSpec(spec, plan, plan->want);
    if (nWant == 0) goto done;
    wantAll = strstr(spec, "all") != NULL;
    snprintf(partDir, sizeof partDir, "%s/partners", sDataDir);
    snprintf(menuDir, sizeof menuDir, "%s/menus", sDataDir);
    snprintf(mp, sizeof mp, "%s/manifest.json", partDir);
    expManOpen(&part, TAG, mp, "partners");
    snprintf(mp, sizeof mp, "%s/manifest.json", menuDir);
    expManOpen(&menus, TAG, mp, "menus");
    partsLoaded = 1;
    if (upToDate(&part, &menus, partDir, menuDir)) {
        sTotals.skipped = 1;
        rc = 0;
        goto done;
    }
    memset(&ps, 0, sizeof ps);
    memset(&ms, 0, sizeof ms);
    expSinkInPlace(&psink, TAG, partDir, &part, &ps);
    expSinkInPlace(&msink, TAG, menuDir, &menus, &ms);
    memset(&pe, 0, sizeof pe);
    memset(&me, 0, sizeof me);
    pe.sink = &psink;
    me.sink = &msink;

    /* portraits and icons: the first screen's cells, the others must be the same */
    for (s = 0; s < N_SCREENS; s++) screenArt(&disc, &kScreens[s], s == N_SCREENS - 1, &art[s]);
    if (!art[0].ok || !art[N_SCREENS - 1].ok) {
        plogLine(TAG, "export_partners: the portrait sheets of the disc are not the EU ones; nothing written\n");
        goto done;
    }
    for (s = 1; s < N_SCREENS; s++) {
        int f;

        if (!art[s].ok) {
            sTotals.mismatches++;
            plogLine(TAG, "the portraits of %s could not be read\n", kScreens[s].name);
            continue;
        }
        for (f = 0; f < 29; f++) {
            if (memcmp(art[s].cell[f], art[0].cell[f], sizeof art[0].cell[f]) != 0 || memcmp(art[s].pal[f], art[0].pal[f], sizeof art[0].pal[f]) != 0) {
                sTotals.mismatches++;
                plogLine(TAG, "frame %d of %s differs from the first screen's\n", f, kScreens[s].name);
                break;
            }
        }
    }
    for (p = 0; p < PE_PARTNERS; p++) {
        const PeLayout *lay = peLayout(p);
        const uint8_t *cells[PE_CELLS_MAX];
        ImgPic pic;
        int c, bad = 0;
        ImgPic icon;
        uint8_t *fieldBytes = NULL;
        size_t fieldLen = 0;
        char field[64];
        const DiscFile *ff;

        if (!plan->want[p]) continue;
        snprintf(pe.prefix, sizeof pe.prefix, "%s", plan->key[p]);
        for (c = 0; c < lay->portraitCells; c++) {
            cells[c] = art[0].cell[lay->portraitFirst + c];
            if (memcmp(art[0].pal[lay->portraitFirst + c], art[0].pal[lay->portraitFirst], sizeof art[0].pal[0]) != 0) bad = 1;
        }
        if (bad) plogLine(TAG, "the cells of %s do not share one palette\n", plan->key[p]);
        if (peStrip(cells, lay->portraitCells, 32, 32, art[0].pal[lay->portraitFirst], &pic) == 0) putPng(&pe, "portrait.png", &pic);
        else failed++;
        imgPicFree(&pic);
        /* the icon of partner p: frame partnerIcons[p] of the memory card sheet, which the last screen holds as frames 40..47 */
        memset(&icon, 0, sizeof icon);
        icon.w = 44;
        icon.h = 28;
        icon.depth = 4;
        icon.nColors = 16;
        icon.px = (uint8_t *)malloc(44 * 28);
        if (icon.px != NULL) {
            memcpy(icon.px, art[N_SCREENS - 1].icon[lay->iconFrame - 40], 44 * 28);
            paletteTo(&icon, 0, art[N_SCREENS - 1].iconPal[lay->iconFrame - 40], 16);
            putPng(&pe, "icon.png", &icon);
        } else {
            failed++;
        }
        imgPicFree(&icon);
        /* the follower sprite */
        snprintf(field, sizeof field, "AAA/DAT/FIELD/SPRT/%s.BIN", lay->fieldFile);
        ff = discFind(&disc, field);
        if (ff != NULL) fieldBytes = readFile(ff);
        fieldLen = ff != NULL ? (size_t)ff->size : 0;
        if (fieldBytes != NULL) {
            SprStats st;

            memset(&st, 0, sizeof st);
            snprintf(pe.prefix, sizeof pe.prefix, "%s/field", plan->key[p]);
            sprExtractTo(emitFn, &pe, field, fieldBytes, fieldLen, &st);
            failed += st.failed;
            free(fieldBytes);
        } else {
            plogLine(TAG, "cannot read %s from the disc\n", field);
            failed++;
        }
        sTotals.partners++;
    }

    /* the training files: set p of each is a partner's, set 8 the effect they share */
    for (t = 0; t < PE_TRAININGS; t++) {
        char path[96], name[64], dir[96];
        const DiscFile *tf;
        uint8_t *bytes;
        PeTrain tr;

        snprintf(path, sizeof path, "AAA/DAT/TRAINING/%s.BIN", peTrainFile(t));
        snprintf(name, sizeof name, "%s", plan->train[t]);
        tf = discFind(&disc, path);
        bytes = tf != NULL ? readFile(tf) : NULL;
        if (bytes == NULL || peTrainOpen(&tr, bytes, (size_t)tf->size) == 0) {
            plogLine(TAG, "cannot read the training file %s\n", path);
            failed++;
            free(bytes);
            continue;
        }
        for (p = 0; p < PE_PARTNERS; p++) {
            char id[32];

            if (!plan->want[p]) continue;
            snprintf(pe.prefix, sizeof pe.prefix, "%s/training/%s", plan->key[p], name);
            snprintf(id, sizeof id, "dw3:partner/%d", p);
            if (peTrainEmit(&tr, p, "partners-training-v1", name, id, path + 4, emitFn, &pe, &trainSt) != 0) failed++;
        }
        if (tr.set[8].present) {
            snprintf(dir, sizeof dir, "gym/effects/%s", name);
            snprintf(me.prefix, sizeof me.prefix, "%s", dir);
            if (peTrainEmit(&tr, 8, "menus-gym-effect-v1", name, NULL, path + 4, emitFn, &me, &trainSt) != 0) failed++;
        }
        peTrainClose(&tr);
        free(bytes);
    }

    for (p = 0; p < PE_PARTNERS; p++) {
        Sb js;

        if (!plan->want[p]) continue;
        memset(&js, 0, sizeof js);
        partnerJson(&js, p, plan->key[p], plan->title[p], &art[0], plan->train);
        snprintf(pe.prefix, sizeof pe.prefix, "%s", plan->key[p]);
        if (js.fail || emitFn(&pe, "partner.json", (const uint8_t *)js.p, js.n) != 0) failed++;
        free(js.p);
    }
    sTotals.failed = failed + pe.failed + me.failed;
    sTotals.written = ps.written + ms.written;
    sTotals.kept = ps.kept + ms.kept;
    sTotals.newFiles = ps.newFiles + ms.newFiles;
    sTotals.bytes = ps.bytes + ms.bytes;
    if (trainSt.missing > 0) plogLine(TAG, "%d training parts could not be drawn (no page, palette or pixels)\n", trainSt.missing);
    if (sTotals.failed == 0 && wantAll) expManMark(&part, kExporterKey, PARTNER_EXPORT_VERSION);
    if (expManSave(&part, 0) || expManSave(&menus, 0)) sTotals.failed++;
    plogLine(TAG, "Done: %d partners in %s/partners, %d written, %d kept, %d new beside edited, %d failed, %ld bytes, %d training sprites, %d screen mismatches\n",
             sTotals.partners, sDataDir, sTotals.written, sTotals.kept, sTotals.newFiles, sTotals.failed, sTotals.bytes, trainSt.frames, sTotals.mismatches);
    rc = sTotals.partners;
done:
    if (partsLoaded) {
        expManFree(&part);
        expManFree(&menus);
    }
    free(disc.f);
    free(plan);
    free(art);
    return rc;
}
