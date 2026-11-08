/*
 * Field sprites as PNG with their animations (feature field-sprites-png, T2). See sprite_data.h.
 *
 * Evidence: src/field/field_mode/actor_control.c (FIELDSTG_animateActor, FIELDSTG_drawActor), actor.c
 * (FIELDSTG_createActor), include/field/field_mode.h (ACTOR_ANIM_*), src/engine/file/file_cache.c (getFileEntry).
 */
#include "sprite_data.h"
#include "content.h"
#include "png_read.h"
#include "port_log.h"
#include "text_data.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISC_PREFIX "AAA/DAT/FIELD/SPRT/"
#define SPR_MAX_ENTRIES 1024

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int le16s(const uint8_t *p) { return (int16_t)((unsigned)p[0] | (unsigned)p[1] << 8); }

int sprIsSpriteFile(const char *p)
{
    size_t n = strlen(p), pl = strlen(DISC_PREFIX);
    return n > pl + 4 && !strncmp(p, DISC_PREFIX, pl) && !strchr(p + pl, '/') && !strcmp(p + n - 4, ".BIN");
}

static const char *const kPartnerFiles[8] = {"P003PKEN", "P004KOGU", "P005KOET", "P006AGUM", "P007VMON", "P008GIRU", "P009RENA", "P010PATA"};

const char *sprPartnerFile(int partner) { return partner >= 0 && partner < 8 ? kPartnerFiles[partner] : NULL; }

static int ieq(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
        if (!a[i]) return 0;
    }
    return 1;
}

int sprPartnerOf(const char *p)
{
    const char *base;
    int i;

    if (!p) return -1;
    for (base = p; *base && !ieq(base, "sprt/", 5) && !ieq(base, "sprites/", 8); base++) {
    }
    if (!*base) return -1;
    base += ieq(base, "sprt/", 5) ? 5 : 8;
    for (i = 0; i < 8; i++) {
        size_t n = strlen(kPartnerFiles[i]);

        if (ieq(base, kPartnerFiles[i], n) && (base[n] == '\0' || base[n] == '/' || base[n] == '.')) return i;
    }
    return -1;
}

int sprFolder(const char *discPath, char *out, size_t cap)
{
    size_t n, w;
    if (!sprIsSpriteFile(discPath)) return -1;
    n = strlen(discPath) - strlen(DISC_PREFIX) - 4; /* without the folder and ".BIN" */
    if (n + 1 > cap) return -1;
    for (w = 0; w < n; w++) out[w] = (char)tolower((unsigned char)discPath[strlen(DISC_PREFIX) + w]); /* ADR-010: the port writes lower case */
    out[n] = '\0';
    return 0;
}

int sprDiscOf(const char *folder, char *out, size_t cap)
{
    size_t n = strlen(folder), i;
    if (n == 0 || n > 40) return -1;
    for (i = 0; i < n; i++) {
        if (!isalnum((unsigned char)folder[i]) && folder[i] != '_') return -1; /* a disc file name: no separators, no dots */
    }
    if (snprintf(out, cap, "%s", DISC_PREFIX) >= (int)cap) return -1;
    for (i = 0; i < n; i++) out[strlen(DISC_PREFIX) + i] = (char)toupper((unsigned char)folder[i]);
    if (strlen(DISC_PREFIX) + n + 5 > cap) return -1;
    strcpy(out + strlen(DISC_PREFIX) + n, ".BIN");
    return 0;
}

int sprName(const char *discPath, int entry, const char *what, char *out, size_t cap)
{
    char folder[48];
    int w;
    if (entry < 0 || entry >= SPR_MAX_ENTRIES || sprFolder(discPath, folder, sizeof folder)) return -1;
    if (what) w = snprintf(out, cap, "sprites/%s/%s", folder, what);
    else w = snprintf(out, cap, "sprites/%s/%03d.png", folder, entry);
    return w > 0 && (size_t)w < cap ? 0 : -1;
}

/* ---- text builder ---- */

typedef struct Sb {
    char *p;
    size_t n, cap;
    int fail;
} Sb;

static void sbf(Sb *b, const char *fmt, ...)
{
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

/* ---- the archive ---- */

typedef struct Ent {
    size_t off, len;
} Ent;

enum { K_OTHER = 0, K_FRAME = 1, K_ANIM = 2, K_SETS = 3 };

typedef struct Frame {
    int entry, w, h, used;
} Frame;

typedef struct Anim {
    int entry, nSteps, loop, ok;
    int *steps; /* 4 ints per step: frame entry, time, x, y */
    long ticks;
} Anim;

typedef struct SetRec {
    int id;
    int slot[5];
} SetRec;

/* The table: the first word is its size; an offset of 0 ends it early. Entries run to the next offset. */
static int readTable(const uint8_t *f, size_t len, Ent *e)
{
    uint32_t w0;
    int n, i, j;
    if (len < 8) return 0;
    w0 = le32(f);
    if (w0 < 8 || (w0 & 3) || w0 > 4u * SPR_MAX_ENTRIES || w0 > len) return 0;
    n = (int)(w0 / 4);
    for (i = 0; i < n; i++) {
        uint32_t o = le32(f + 4 * (size_t)i);
        if (o == 0) {
            n = i;
            break;
        }
        if (o < w0 || o >= len) return 0;
        e[i].off = o;
    }
    for (i = 0; i < n; i++) {
        size_t end = len;
        for (j = 0; j < n; j++) {
            if (e[j].off > e[i].off && e[j].off < end) end = e[j].off;
        }
        e[i].len = end - e[i].off;
    }
    return n;
}

/* An animation: steps until a 0 word (loops) or a -1 word (plays once). 0 when the entry has no end marker. */
static int readAnim(const uint8_t *d, size_t len, Anim *a)
{
    size_t p = 0;
    int cap = 0;
    a->nSteps = 0;
    a->steps = NULL;
    a->ticks = 0;
    a->ok = 0;
    while (p + 4 <= len) {
        int32_t w = (int32_t)le32(d + p);
        if (w == 0 || w == -1) {
            a->loop = w == 0;
            a->ok = 1;
            return 1;
        }
        if (p + 16 > len) return 0;
        if (a->nSteps == cap) {
            int *g = (int *)realloc(a->steps, (size_t)(cap ? cap * 2 : 8) * 4 * sizeof(int));
            if (!g) return 0;
            a->steps = g;
            cap = cap ? cap * 2 : 8;
        }
        a->steps[4 * a->nSteps] = (int)le32(d + p + 4);
        a->steps[4 * a->nSteps + 1] = w;
        a->steps[4 * a->nSteps + 2] = (int)(int32_t)le32(d + p + 8);
        a->steps[4 * a->nSteps + 3] = (int)(int32_t)le32(d + p + 12);
        a->ticks += w;
        a->nSteps++;
        p += 16;
    }
    return 0;
}

static const struct {
    int id;
    const char *name;
} kSetNames[] = {
    {1, "STAND"}, {4, "WALK"}, {5, "RUN"}, {6, "STOP"}, {8, "SEARCH"}, {0x11, "GAUGE_START"}, {0x12, "GAUGE_RESULT"},
    {0x13, "GAUGE_PLAY"}, {0x14, "GAUGE_WAIT"}, {0x15, "GAUGE_END"}, {0x16, "DROP_OFF"}, {0x17, "FALL"}, {0x18, "FALL_FAR"},
    {0x19, "LAND"}, {0x1A, "CLIMB_UP"}, {0x1B, "CLIMB_DOWN"}, {0x1C, "GET_ON_WALL"}, {0x1D, "CLIMB_OFF_TOP"},
    {0x1E, "GET_OVER_EDGE"}, {0x1F, "CLIMB_OFF_BOTTOM"}, {0x20, "CLIMB"}, {0x41, "OPEN"}, {0x45, "USE"}, {0x54, "USED"},
};

static const char *setName(int id)
{
    size_t i;
    for (i = 0; i < sizeof kSetNames / sizeof *kSetNames; i++) {
        if (kSetNames[i].id == id) return kSetNames[i].name;
    }
    return NULL;
}

static int cmpFrame(const void *a, const void *b) { return ((const Frame *)a)->entry - ((const Frame *)b)->entry; }
static int cmpAnim(const void *a, const void *b) { return ((const Anim *)a)->entry - ((const Anim *)b)->entry; }

/* The set table in the last entry: records of 6 words until a set number of 0. Each slot must be an animation
   entry with an end marker. 0 when it is not one. */
static int readSets(const uint8_t *f, const Ent *e, int n, SetRec **out, int *nOut)
{
    const uint8_t *s = f + e[n - 1].off;
    size_t len = e[n - 1].len, o;
    SetRec *r = NULL;
    int nr = 0, i;
    for (o = 0; o + 12 <= len; o += 12) {
        if (le16s(s + o) == 0) break;
        {
            SetRec *g = (SetRec *)realloc(r, (size_t)(nr + 1) * sizeof *r);
            if (!g) goto bad;
            r = g;
        }
        r[nr].id = le16s(s + o);
        for (i = 0; i < 5; i++) {
            int a = le16s(s + o + 2 + 2 * (size_t)i);
            if (a < 0 || a >= n - 1) goto bad;
            r[nr].slot[i] = a;
        }
        nr++;
    }
    if (nr == 0) goto bad;
    *out = r;
    *nOut = nr;
    return 1;
bad:
    free(r);
    return 0;
}

static void freeAnims(Anim *a, int n)
{
    int i;
    for (i = 0; i < n; i++) free(a[i].steps);
    free(a);
}

/* Where the files of one extraction go: a callback with the names relative to the sprite's own folder ("004.png", "sprite.json"). The exporter of the
   originals (sprite_export.c) writes them under sprites/<name>/ and the partner bundles (partner_export.c) under partners/<name>/field/; `format` is the format
   name the sprite.json carries. */
typedef struct Out {
    SprEmitFn emit;
    void *ctx;
    const char *format;
} Out;

static int outOn(const Out *o) { return o->emit != NULL; }

static int outPut(const Out *o, int entry, const char *what, const uint8_t *data, size_t len)
{
    char rel[160];
    if (what) snprintf(rel, sizeof rel, "%s", what);
    else snprintf(rel, sizeof rel, "%03d.png", entry);
    return o->emit(o->ctx, rel, data, len);
}

static int process(const Out *out, const char *discPath, const uint8_t *file, size_t len, SprStats *st, char **jsonOut, size_t *jsonLen)
{
    Ent *ent = (Ent *)calloc(SPR_MAX_ENTRIES, sizeof *ent);
    uint8_t *kind = NULL, *used = NULL;
    Frame *fr = NULL;
    Anim *an = NULL;
    SetRec *sets = NULL;
    int n, nFr = 0, nAn = 0, nSets = 0, i, k, s, hasSets = 0, pngs = 0;
    SprStats loc;
    Sb b;

    memset(&b, 0, sizeof b);
    memset(&loc, 0, sizeof loc);
    if (!ent) return 0;
    n = readTable(file, len, ent);
    if (n < 2) {
        plogLine("images", "%s is not a sprite archive\n", discPath);
        st->failed++;
        free(ent);
        return 0;
    }
    kind = (uint8_t *)calloc((size_t)n, 1);
    used = (uint8_t *)calloc((size_t)n, 1);
    fr = (Frame *)calloc((size_t)n, sizeof *fr);
    an = (Anim *)calloc((size_t)n, sizeof *an);
    if (!kind || !used || !fr || !an) goto done;

    if (readSets(file, ent, n, &sets, &nSets)) {
        hasSets = 1;
        kind[n - 1] = K_SETS;
        for (s = 0; s < nSets; s++) {
            for (k = 0; k < 5; k++) kind[sets[s].slot[k]] = K_ANIM;
        }
        for (i = 0; i < n - 1; i++) {
            if (kind[i] != K_ANIM) continue;
            an[nAn].entry = i;
            if (!readAnim(file + ent[i].off, ent[i].len, &an[nAn])) {
                plogLine("images", "%s: animation entry %d has no end marker\n", discPath, i);
                loc.failed++;
            }
            for (k = 0; k < an[nAn].nSteps; k++) {
                int f = an[nAn].steps[4 * k];
                if (f >= 0 && f < n - 1 && kind[f] != K_ANIM) used[f] = 1;
            }
            nAn++;
        }
    } else {
        plogLine("images", "%s: no set table in its last entry; frames only\n", discPath);
    }

    /* the frames: every entry that is a TIM and is not an animation (referenced ones first count as used) */
    for (i = 0; i < n - (hasSets ? 1 : 0); i++) {
        ImgPic pic;
        uint8_t *png = NULL;
        size_t pl = 0;
        if (kind[i] == K_ANIM) continue;
        if (imgDecodeTim(file + ent[i].off, ent[i].len, NULL, &pic)) {
            if (used[i]) {
                plogLine("images", "%s: frame entry %d is not a TIM\n", discPath, i);
                loc.failed++;
            }
            continue;
        }
        kind[i] = K_FRAME;
        fr[nFr].entry = i;
        fr[nFr].w = pic.w;
        fr[nFr].h = pic.h;
        fr[nFr].used = used[i];
        nFr++;
        if (!used[i]) loc.unused++;
        if (outOn(out)) {
            if (imgPicToPng(&pic, &png, &pl) || outPut(out, i, NULL, png, pl)) {
                plogLine("images", "cannot convert frame %d of %s\n", i, discPath);
                loc.failed++;
            } else {
                pngs++;
            }
            free(png);
        }
        imgPicFree(&pic);
    }
    qsort(fr, (size_t)nFr, sizeof *fr, cmpFrame);
    qsort(an, (size_t)nAn, sizeof *an, cmpAnim);

    sbf(&b, "{\n \"format\": \"%s\",\n \"source\": \"%s\",\n", out->format, discPath + 8);
    sbf(&b, " \"set_table\": %d,\n", hasSets ? n - 1 : -1);
    sbf(&b, " \"frames\": [");
    for (i = 0; i < nFr; i++) {
        sbf(&b, "%s\n  { \"id\": %d, \"image\": \"%03d.png\", \"w\": %d, \"h\": %d%s }", i ? "," : "", fr[i].entry, fr[i].entry, fr[i].w,
            fr[i].h, fr[i].used ? "" : ", \"used\": false");
    }
    sbf(&b, "%s],\n \"animations\": [", nFr ? "\n " : "");
    for (i = 0; i < nAn; i++) {
        sbf(&b, "%s\n  { \"id\": %d, \"loop\": %s, \"ticks\": %ld%s, \"steps\": [", i ? "," : "", an[i].entry, an[i].loop ? "true" : "false",
            an[i].ticks, an[i].ok ? "" : ", \"error\": \"no end marker\"");
        for (k = 0; k < an[i].nSteps; k++) {
            const int *p = an[i].steps + 4 * k;
            sbf(&b, "%s{ \"frame\": %d, \"time\": %d, \"x\": %d, \"y\": %d }", k ? ", " : "", p[0], p[1], p[2], p[3]);
        }
        sbf(&b, "] }");
    }
    sbf(&b, "%s],\n \"sets\": [", nAn ? "\n " : "");
    for (s = 0; s < nSets; s++) {
        const char *nm = setName(sets[s].id);
        sbf(&b, "%s\n  { \"id\": %d", s ? "," : "", sets[s].id);
        if (nm) sbf(&b, ", \"name\": \"%s\"", nm);
        sbf(&b, ", \"animations\": [%d, %d, %d, %d, %d] }", sets[s].slot[0], sets[s].slot[1], sets[s].slot[2], sets[s].slot[3], sets[s].slot[4]);
    }
    sbf(&b, "%s],\n", nSets ? "\n " : "");
    sbf(&b, " \"directions\": [\n"
            "  { \"dir\": 0, \"slot\": 0, \"flip\": false, \"step\": \"down\" },\n"
            "  { \"dir\": 1, \"slot\": 1, \"flip\": false, \"step\": \"down-left\" },\n"
            "  { \"dir\": 2, \"slot\": 2, \"flip\": false, \"step\": \"left\" },\n"
            "  { \"dir\": 3, \"slot\": 3, \"flip\": false, \"step\": \"up-left\" },\n"
            "  { \"dir\": 4, \"slot\": 4, \"flip\": false, \"step\": \"up\" },\n"
            "  { \"dir\": 5, \"slot\": 3, \"flip\": true, \"step\": \"up-right\" },\n"
            "  { \"dir\": 6, \"slot\": 2, \"flip\": true, \"step\": \"right\" },\n"
            "  { \"dir\": 7, \"slot\": 1, \"flip\": true, \"step\": \"down-right\" }\n ],\n");
    sbf(&b, " \"notes\": \"Each frame is the PNG named by its id (the entry's index in the archive): a 4-bit image of w x h pixels with its own "
            "16-colour palette (index 0 is transparent). An animation lists steps: frame is a frame id, time is how long it shows (game ticks: the "
            "engine subtracts its frame time each update), x and y place the frame's top-left corner relative to the actor's tile position (its feet), "
            "in pixels. loop: true repeats from the first step; false stops on the last frame (the game then reports it done). A set is an animation "
            "set the game asks for by its id (name: the decompilation's ACTOR_ANIM_* identifier); its five animations are the slots 0 to 4, one per "
            "facing; the directions table says which slot each of the 8 facings uses and which are mirrored (a mirrored frame is drawn flipped "
            "horizontally and its x becomes -(w + x)). used: false marks a frame no animation of this file references. Edit the PNGs keeping each "
            "one's width and height and at most 16 colours; this file is information only and is regenerated from the disc.\"\n}\n");
    if (b.fail) goto done;
    if (outOn(out)) {
        if (outPut(out, 0, "sprite.json", (const uint8_t *)b.p, b.n)) loc.failed++;
    }
    st->files++;
    st->frames += nFr;
    st->unused += loc.unused;
    st->anims += nAn;
    st->sets += nSets;
    st->failed += loc.failed;
    if (jsonOut) {
        *jsonOut = b.p;
        if (jsonLen) *jsonLen = b.n;
        b.p = NULL;
    }
done:
    free(b.p);
    free(sets);
    if (an) freeAnims(an, nAn);
    free(fr);
    free(kind);
    free(used);
    free(ent);
    return pngs;
}

int sprTable(const uint8_t *file, size_t len, SprEntry *out, int max)
{
    Ent *ent = (Ent *)calloc(SPR_MAX_ENTRIES, sizeof *ent);
    int n, i;
    if (!ent) return 0;
    n = readTable(file, len, ent);
    if (n > max) n = max;
    for (i = 0; i < n; i++) {
        out[i].off = ent[i].off;
        out[i].len = ent[i].len;
    }
    free(ent);
    return n;
}

char *sprJson(const char *discPath, const uint8_t *file, size_t len, size_t *outLen, SprStats *st)
{
    SprStats tmp;
    char *j = NULL;
    memset(&tmp, 0, sizeof tmp);
    Out none = {NULL, NULL, "sprite-v1"};
    process(&none, discPath, file, len, st ? st : &tmp, &j, outLen);
    return j;
}

int sprExtractTo(SprEmitFn emit, void *ctx, const char *discPath, const uint8_t *file, size_t len, SprStats *st)
{
    return sprExtractAs("sprite-v1", emit, ctx, discPath, file, len, st);
}

int sprExtractAs(const char *format, SprEmitFn emit, void *ctx, const char *discPath, const uint8_t *file, size_t len, SprStats *st)
{
    Out out = {emit, ctx, format};
    return process(&out, discPath, file, len, st, NULL, NULL);
}

/* ================= sprites a mod brings (S9) ================= */

typedef struct BankEntry {
    uint8_t *data;
    size_t len;
} BankEntry;

struct SprBank {
    BankEntry *e;
    int n, cap;
};

SprBank *sprBankNew(void) { return (SprBank *)calloc(1, sizeof(SprBank)); }

void sprBankFree(SprBank *b)
{
    int i;
    if (!b) return;
    for (i = 0; i < b->n; i++) free(b->e[i].data);
    free(b->e);
    free(b);
}

int sprBankEntries(const SprBank *b) { return b ? b->n : 0; }

static int bankPush(SprBank *b, uint8_t *data, size_t len)
{
    if (b->n == b->cap) {
        int cap = b->cap ? b->cap * 2 : 64;
        BankEntry *g = (BankEntry *)realloc(b->e, (size_t)cap * sizeof *g);
        if (!g) return -1;
        b->e = g;
        b->cap = cap;
    }
    b->e[b->n].data = data;
    b->e[b->n].len = len;
    return b->n++;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void why(char *w, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void why(char *w, size_t cap, const char *fmt, ...)
{
    va_list ap;
    if (!w || !cap) return;
    va_start(ap, fmt);
    vsnprintf(w, cap, fmt, ap);
    va_end(ap);
}

/* a 4-bit TIM with a 16-colour CLUT from an indexed PNG; the width is padded to whole halfwords (4 pixels) with index 0 */
static uint8_t *pngToTim(const PngImage *p, size_t *len, int *paddedW)
{
    int iw = (p->w + 3) / 4, x, y, i;
    size_t total = 8 + 12 + 32 + 12 + 2 * (size_t)iw * (size_t)p->h;
    uint8_t *t = (uint8_t *)calloc(1, total), *q;
    if (!t) return NULL;
    put32(t, 0x10);
    put32(t + 4, 8);
    q = t + 8;
    put32(q, 12 + 32);
    put16(q + 8, 16);
    put16(q + 10, 1);
    for (i = 0; i < 16; i++) {
        unsigned v = 0;
        if (i < p->nPal && (i >= p->nAlpha || p->alpha[i] != 0)) {
            v = (unsigned)(p->rgb[3 * i] >> 3) | (unsigned)(p->rgb[3 * i + 1] >> 3) << 5 | (unsigned)(p->rgb[3 * i + 2] >> 3) << 10;
            if (v == 0) v = 0x8000; /* an opaque black: 0 would read as transparent */
        }
        put16(q + 12 + 2 * i, v);
    }
    q += 12 + 32;
    put32(q, (uint32_t)(12 + 2 * iw * p->h));
    put16(q + 8, (unsigned)iw);
    put16(q + 10, (unsigned)p->h);
    q += 12;
    for (y = 0; y < p->h; y++) {
        for (x = 0; x < iw * 4; x++) {
            unsigned px = x < p->w ? p->px[(size_t)y * (size_t)p->w + (size_t)x] : 0;
            uint8_t *hw = q + 2 * ((size_t)y * (size_t)iw + (size_t)(x / 4));
            unsigned v = (unsigned)hw[0] | (unsigned)hw[1] << 8;
            v |= px << ((x & 3) * 4);
            put16(hw, v);
        }
    }
    *len = total;
    *paddedW = iw * 4;
    return t;
}

typedef struct DefFrame {
    int id, w, h, entry;
} DefFrame;
typedef struct DefAnim {
    int id, entry;
} DefAnim;

static int findId(const int *ids, size_t stride, int n, int id)
{
    int i;
    for (i = 0; i < n; i++) {
        if (*(const int *)((const uint8_t *)ids + stride * (size_t)i) == id) return i;
    }
    return -1;
}

static int wholeMember(const ExtJson *o, const char *key, long lo, long hi, int *out)
{
    const ExtJson *j = extJsonGet(o, key);
    if (!j || j->t != 0 || !extWhole(j->num, lo, hi)) return 0;
    *out = (int)j->num;
    return 1;
}

static int validPngName(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && l < 100 && !strchr(n, '/') && !strchr(n, '\\') && n[0] != '.' && !strcmp(n + l - 4, ".png");
}

int sprBankAdd(SprBank *b, const char *json, size_t jsonLen, SprPngFn png, void *ctx, SprInfo *info, char *w, size_t wcap)
{
    ExtJson root;
    const ExtJson *fmt, *fr, *an, *st;
    DefFrame *frames = NULL;
    DefAnim *anims = NULL;
    int nfr, nan, nst, i, k, base, startN, maxW = 0, maxH = 0, width = 0, rc = -1, hasWidth, ok = 1;
    int *used = NULL;
    uint8_t *setBytes = NULL;

    if (!b) return -1;
    if (extJsonParse((const uint8_t *)json, jsonLen, &root)) {
        why(w, wcap, "sprite.json is not a JSON object");
        return -1;
    }
    startN = b->n;
    fmt = extJsonGet(&root, "format");
    fr = extJsonGet(&root, "frames");
    an = extJsonGet(&root, "animations");
    st = extJsonGet(&root, "sets");
    if (!fmt || fmt->t != 1 || (strcmp(fmt->str, "sprites-sprite-v1") && strcmp(fmt->str, "sprite-v1"))) {
        why(w, wcap, "\"format\" must be \"sprites-sprite-v1\" (or \"sprite-v1\", the one a partner's field/ folder writes)");
        goto done;
    }
    if (!fr || fr->t != 4 || fr->n < 1 || fr->n > 2048 || !an || an->t != 4 || an->n < 1 || an->n > 2048 || !st || st->t != 4 || st->n < 1 || st->n > 255) {
        why(w, wcap, "\"frames\", \"animations\" and \"sets\" must be lists (at least one each)");
        goto done;
    }
    nfr = fr->n;
    nan = an->n;
    nst = st->n;
    frames = (DefFrame *)calloc((size_t)nfr, sizeof *frames);
    anims = (DefAnim *)calloc((size_t)nan, sizeof *anims);
    used = (int *)calloc((size_t)nfr, sizeof *used);
    if (!frames || !anims || !used) {
        why(w, wcap, "out of memory");
        goto done;
    }
    hasWidth = wholeMember(&root, "actor_width", 8, 256, &width);
    if (extJsonGet(&root, "actor_width") && !hasWidth) {
        why(w, wcap, "\"actor_width\" must be a whole number from 8 to 256");
        goto done;
    }
    /* entries are laid out frames, animations, set table: the frame entries are known before the animations that name them */
    base = b->n;
    for (i = 0; i < nfr && ok; i++) {
        const ExtJson *f = &fr->v[i], *img = extJsonGet(f, "image");
        PngImage pi;
        uint8_t *bytes = NULL, *tim;
        size_t blen = 0, tl = 0;
        char pw[160];
        int pad, maxIdx = 0, j;

        if (f->t != 2 || !wholeMember(f, "id", 0, 65535, &frames[i].id) || !img || img->t != 1 || !validPngName(img->str)) {
            why(w, wcap, "frame %d needs an \"id\" and an \"image\" (the name of a .png file in the sprite's folder)", i);
            ok = 0;
            break;
        }
        if (findId(&frames[0].id, sizeof *frames, i, frames[i].id) >= 0) {
            why(w, wcap, "frame id %d is listed twice", frames[i].id);
            ok = 0;
            break;
        }
        if (png(ctx, img->str, &bytes, &blen)) {
            why(w, wcap, "frame %d: %s could not be read", frames[i].id, img->str);
            ok = 0;
            break;
        }
        if (pngReadIndexed(bytes, blen, &pi, pw, sizeof pw)) {
            why(w, wcap, "frame %d: %s: %s", frames[i].id, img->str, pw);
            free(bytes);
            ok = 0;
            break;
        }
        free(bytes);
        for (j = 0; j < pi.w * pi.h; j++) {
            if (pi.px[j] > maxIdx) maxIdx = pi.px[j];
        }
        if (pi.nPal > 16 || maxIdx > 15) {
            why(w, wcap, "frame %d: %s has more than 16 colours (a frame's palette is 16)", frames[i].id, img->str);
            pngImageFree(&pi);
            ok = 0;
            break;
        }
        if (pi.w > SPR_SLOT_MAX_W || pi.h > SPR_SLOT_MAX_H) {
            why(w, wcap, "frame %d: %s is %dx%d, at most %dx%d", frames[i].id, img->str, pi.w, pi.h, SPR_SLOT_MAX_W, SPR_SLOT_MAX_H);
            pngImageFree(&pi);
            ok = 0;
            break;
        }
        tim = pngToTim(&pi, &tl, &pad);
        pngImageFree(&pi);
        if (!tim) {
            why(w, wcap, "out of memory");
            ok = 0;
            break;
        }
        frames[i].w = pad;
        frames[i].h = 0;
        { /* the height: read back from the TIM we just made */
            frames[i].h = (int)((unsigned)tim[8 + 12 + 32 + 10] | (unsigned)tim[8 + 12 + 32 + 11] << 8);
        }
        frames[i].entry = bankPush(b, tim, tl);
        if (frames[i].entry < 0 || b->n > SPR_ENTRIES_MAX) {
            if (frames[i].entry < 0) free(tim);
            why(w, wcap, "the bank is full (%d entries at most)", SPR_ENTRIES_MAX);
            ok = 0;
            break;
        }
        (void)base;
    }
    if (!ok) goto done;
    for (i = 0; i < nan && ok; i++) {
        const ExtJson *a = &an->v[i], *steps = extJsonGet(a, "steps"), *lp = extJsonGet(a, "loop");
        uint8_t *d;
        size_t dl;

        if (a->t != 2 || !wholeMember(a, "id", 0, 65535, &anims[i].id) || !steps || steps->t != 4 || steps->n < 1 || steps->n > 4096) {
            why(w, wcap, "animation %d needs an \"id\" and \"steps\" (a list of at least one)", i);
            ok = 0;
            break;
        }
        if (findId(&anims[0].id, sizeof *anims, i, anims[i].id) >= 0 || findId(&frames[0].id, sizeof *frames, nfr, anims[i].id) >= 0) {
            why(w, wcap, "animation id %d is used twice (frame and animation ids share one list)", anims[i].id);
            ok = 0;
            break;
        }
        if (lp && lp->t != 3) {
            why(w, wcap, "animation %d: \"loop\" must be true or false", anims[i].id);
            ok = 0;
            break;
        }
        dl = 16 * (size_t)steps->n + 4;
        d = (uint8_t *)calloc(1, dl);
        if (!d) {
            why(w, wcap, "out of memory");
            ok = 0;
            break;
        }
        for (k = 0; k < steps->n; k++) {
            const ExtJson *s2 = &steps->v[k];
            int fid, time, x, y, fi;

            if (s2->t != 2 || !wholeMember(s2, "frame", 0, 65535, &fid) || !wholeMember(s2, "time", 1, 0x7FFFFF, &time) ||
                !wholeMember(s2, "x", -32768, 32767, &x) || !wholeMember(s2, "y", -32768, 32767, &y)) {
                why(w, wcap, "animation %d, step %d needs \"frame\", \"time\" (1 or more), \"x\" and \"y\"", anims[i].id, k);
                free(d);
                ok = 0;
                break;
            }
            fi = findId(&frames[0].id, sizeof *frames, nfr, fid);
            if (fi < 0) {
                why(w, wcap, "animation %d, step %d names frame %d, which is not listed", anims[i].id, k, fid);
                free(d);
                ok = 0;
                break;
            }
            if (!used[fi]) {
                used[fi] = 1;
                if (frames[fi].w > maxW) maxW = frames[fi].w;
                if (frames[fi].h > maxH) maxH = frames[fi].h;
            }
            put32(d + 16 * (size_t)k, (uint32_t)time);
            put32(d + 16 * (size_t)k + 4, (uint32_t)frames[fi].entry);
            put32(d + 16 * (size_t)k + 8, (uint32_t)(int32_t)x);
            put32(d + 16 * (size_t)k + 12, (uint32_t)(int32_t)y);
        }
        if (!ok) break;
        put32(d + 16 * (size_t)steps->n, (!lp || lp->num != 0) ? 0u : 0xFFFFFFFFu); /* 0 loops, -1 plays once */
        anims[i].entry = bankPush(b, d, dl);
        if (anims[i].entry < 0 || b->n > SPR_ENTRIES_MAX) {
            if (anims[i].entry < 0) free(d);
            why(w, wcap, "the bank is full (%d entries at most)", SPR_ENTRIES_MAX);
            ok = 0;
        }
    }
    if (!ok) goto done;
    setBytes = (uint8_t *)calloc(1, 12 * (size_t)nst + 12);
    if (!setBytes) {
        why(w, wcap, "out of memory");
        goto done;
    }
    for (i = 0; i < nst; i++) {
        const ExtJson *sj = &st->v[i], *al = extJsonGet(sj, "animations");
        int id, slot[5];

        if (sj->t != 2 || !wholeMember(sj, "id", 1, 0x7FFF, &id) || !al || al->t != 4 || (al->n != 1 && al->n != 5)) {
            why(w, wcap, "set %d needs an \"id\" (1 or more) and \"animations\" (one animation id, or five: one per facing)", i);
            goto done;
        }
        for (k = 0; k < 5; k++) {
            const ExtJson *a = &al->v[al->n == 1 ? 0 : k];
            int ai = (a->t == 0 && extWhole(a->num, 0, 65535)) ? findId(&anims[0].id, sizeof *anims, nan, (int)a->num) : -1;

            if (ai < 0) {
                why(w, wcap, "set %d names an animation id that is not listed", id);
                goto done;
            }
            slot[k] = anims[ai].entry;
        }
        put16(setBytes + 12 * (size_t)i, (unsigned)id);
        for (k = 0; k < 5; k++) put16(setBytes + 12 * (size_t)i + 2 + 2 * (size_t)k, (unsigned)slot[k]);
    }
    {
        int e = bankPush(b, setBytes, 12 * (size_t)nst + 12); /* the zero record ends it */
        if (e < 0 || b->n > SPR_ENTRIES_MAX) {
            if (e < 0) free(setBytes);
            setBytes = NULL;
            why(w, wcap, "the bank is full (%d entries at most)", SPR_ENTRIES_MAX);
            goto done;
        }
        setBytes = NULL;
        if (info) {
            memset(info, 0, sizeof *info);
            info->entry = e;
            info->w = maxW;
            info->h = maxH;
            info->width = hasWidth ? width : maxW;
            info->frames = nfr;
            info->anims = nan;
            info->sets = nst;
        }
    }
    rc = 0;
done:
    if (rc) { /* the bank goes back to what it was */
        while (b->n > startN) free(b->e[--b->n].data);
    }
    free(setBytes);
    free(frames);
    free(anims);
    free(used);
    extJsonFree(&root);
    return rc;
}

typedef struct DirCtx {
    const char *dir;
} DirCtx;

static int dirPng(void *ctx, const char *name, uint8_t **data, size_t *len)
{
    char path[1700];
    snprintf(path, sizeof path, "%s/%s", ((DirCtx *)ctx)->dir, name);
    return tdReadFile(path, data, len);
}

int sprBankAddDir(SprBank *b, const char *dir, SprInfo *info, char *w, size_t wcap)
{
    char path[1700];
    uint8_t *json = NULL;
    size_t jl = 0;
    DirCtx c;
    int rc;

    snprintf(path, sizeof path, "%s/sprite.json", dir);
    if (tdReadFile(path, &json, &jl)) {
        why(w, wcap, "sprite.json could not be read");
        return -1;
    }
    c.dir = dir;
    rc = sprBankAdd(b, (const char *)json, jl, dirPng, &c, info, w, wcap);
    free(json);
    return rc;
}

uint8_t *sprBankBuild(const SprBank *b, size_t *len)
{
    size_t table, at, i;
    uint8_t *out;

    if (!b || b->n == 0) return NULL;
    table = 4 * (size_t)b->n;
    at = table;
    for (i = 0; i < (size_t)b->n; i++) at += (b->e[i].len + 3) & ~(size_t)3;
    out = (uint8_t *)calloc(1, at);
    if (!out) return NULL;
    at = table;
    for (i = 0; i < (size_t)b->n; i++) {
        put32(out + 4 * i, (uint32_t)at);
        memcpy(out + at, b->e[i].data, b->e[i].len);
        at += (b->e[i].len + 3) & ~(size_t)3;
    }
    if (len) *len = at;
    return out;
}

/* ---- the size of a game sprite ---- */

static int timFrameSize(const uint8_t *t, size_t len, int *w, int *h)
{
    uint32_t flag, sz;
    size_t at = 8;
    int iw, ih, bits;
    if (len < 20 || le32(t) != 0x10) return -1;
    flag = le32(t + 4);
    if (flag & ~0xBu || (flag & 3) == 3) return -1;
    bits = (flag & 3) == 0 ? 4 : (flag & 3) == 1 ? 8 : 16;
    if (flag & 8) {
        if (at + 12 > len) return -1;
        sz = le32(t + at);
        if (sz < 12 || at + sz > len) return -1;
        at += sz;
    }
    if (at + 12 > len) return -1;
    iw = (int)((unsigned)t[at + 8] | (unsigned)t[at + 9] << 8);
    ih = (int)((unsigned)t[at + 10] | (unsigned)t[at + 11] << 8);
    if (iw <= 0 || ih <= 0) return -1;
    *w = iw * (16 / bits);
    *h = ih;
    return 0;
}

int sprMaxFrame(const uint8_t *file, size_t len, int setEntry, int *w, int *h)
{
    Ent *ent = (Ent *)calloc(SPR_MAX_ENTRIES, sizeof *ent);
    int n, maxW = 0, maxH = 0, a, k, steps;
    size_t o;

    if (!ent) return -1;
    n = readTable(file, len, ent);
    if (n < 2 || setEntry < 0 || setEntry >= n) {
        free(ent);
        return -1;
    }
    for (o = 0; o + 12 <= ent[setEntry].len; o += 12) {
        const uint8_t *r = file + ent[setEntry].off + o;
        if (le16s(r) == 0) break;
        for (a = 0; a < 5; a++) {
            int ae = le16s(r + 2 + 2 * (size_t)a);
            const uint8_t *d;
            size_t dl;

            if (ae < 0 || ae >= n) continue;
            d = file + ent[ae].off;
            dl = ent[ae].len;
            for (k = 0, steps = (int)(dl / 16); k < steps; k++) {
                int32_t tm = (int32_t)le32(d + 16 * (size_t)k);
                int fe, fw, fh;

                if (tm == 0 || tm == -1) break;
                fe = (int)le32(d + 16 * (size_t)k + 4);
                if (fe < 0 || fe >= n || timFrameSize(file + ent[fe].off, ent[fe].len, &fw, &fh)) continue;
                if (fw > maxW) maxW = fw;
                if (fh > maxH) maxH = fh;
            }
        }
    }
    free(ent);
    if (maxW == 0 || maxH == 0) return -1;
    *w = maxW;
    *h = maxH;
    return 0;
}

/* ---- the VRAM band ---- */

void sprVramReset(SprVram *v) { memset(v, 0, sizeof *v); }

static int vramPlace(SprVram *v, int hw, int h, int *page, int *x, int *y)
{
    int p, i;
    int hr = (h + 7) & ~7; /* shelves grow in steps of 8 rows, so heights that differ a little still share one */

    for (p = 0; p < SPR_BAND_PAGES; p++) {
        for (i = 0; i < v->nshelf[p]; i++) {
            SprShelf *s = &v->shelf[p][i];
            if (s->h >= hr && s->h <= hr + 16 && s->x + hw <= SPR_BAND_PAGE_W) {
                *page = p;
                *x = s->x;
                *y = s->y;
                s->x += hw;
                return 0;
            }
        }
    }
    for (p = 0; p < SPR_BAND_PAGES; p++) {
        if (v->nshelf[p] < SPR_SHELVES_MAX && v->rows[p] + hr <= SPR_BAND_ROWS && hw <= SPR_BAND_PAGE_W) {
            SprShelf *s = &v->shelf[p][v->nshelf[p]++];
            s->y = v->rows[p];
            s->h = hr;
            s->x = hw;
            v->rows[p] += hr;
            *page = p;
            *x = 0;
            *y = s->y;
            return 0;
        }
    }
    return -1;
}

int sprVramAlloc(SprVram *v, int w, int h, SprSlot *out)
{
    int hw, page, x, y;
    SprVram keep;

    if (w < 1 || h < 1 || w > SPR_SLOT_MAX_W || h > SPR_SLOT_MAX_H || v->cluts >= SPR_CLUTS_MAX) return -1;
    hw = (w + 3) / 4;
    keep = *v;
    if (vramPlace(v, hw, h, &page, &x, &y)) {
        *v = keep;
        return -1;
    }
    out->x = page * SPR_BAND_PAGE_W;
    out->y = SPR_BAND_Y;
    out->imageX = out->x + x;
    out->imageY = SPR_BAND_Y + y;
    out->u = x * 4;
    out->v = y;
    out->clutX = (v->cluts % 20) * 16;
    out->clutY = SPR_CLUT_Y + v->cluts / 20;
    v->cluts++;
    return 0;
}

int sprVramCapacity(const SprVram *v, int w, int h)
{
    SprVram t = *v;
    SprSlot s;
    int n = 0;

    while (sprVramAlloc(&t, w, h, &s) == 0) n++;
    return n;
}
