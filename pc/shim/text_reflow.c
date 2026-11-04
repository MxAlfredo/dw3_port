/*
 * Automatic reflow of text that does not fit its box (feature es419-live-option, L4a).
 * See text_reflow.h for what it does and why.
 */
#include "text_reflow.h"
#include "port_log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The length of each control code with its arguments: FONT_CODE_LENGTHS in src/engine/text/font.c */
static const int CODE_LEN[11] = {1, 2, 3, 2, 5, 3, 3, 2, 3, 2, 0};

enum { K_GLYPH, K_SPACE, K_NL, K_PAGE, K_ZERO, K_INS };

typedef struct Atom {
    int kind;
    int w;      /* width in pixels (K_INS: set once the buffers are known) */
    int arg;    /* K_INS: the buffer index (-1: the player name code) */
    size_t off;
    size_t len;
} Atom;

typedef struct Atoms {
    Atom *a;
    int n;
    int cap;
    size_t bodyStart; /* after the speaker label */
} Atoms;

static int atomPush(Atoms *as, int kind, int w, int arg, size_t off, size_t len)
{
    if (as->n == as->cap) {
        int cap = as->cap ? as->cap * 2 : 64;
        Atom *na = (Atom *)realloc(as->a, (size_t)cap * sizeof *na);
        if (!na) return -1;
        as->a = na;
        as->cap = cap;
    }
    as->a[as->n].kind = kind;
    as->a[as->n].w = w;
    as->a[as->n].arg = arg;
    as->a[as->n].off = off;
    as->a[as->n].len = len;
    as->n++;
    return 0;
}

/* Splits the bytes of a string into atoms. -1: an unknown control code or a truncated one. */
static int parse(const TrOptions *o, const uint8_t *s, size_t len, Atoms *as)
{
    size_t i = 0;
    int nameBuf[6] = {0, 0, 0, 0, 0, 0};
    int k;

    memset(as, 0, sizeof *as);
    /* a speaker label: `02 07` ... `02 07` (the talk box takes it out of the text) */
    if (len >= 2 && s[0] == 2 && s[1] == 7) {
        size_t j = 2;
        int closed = 0;
        while (j < len) {
            if (s[j] == 1) {
                j += 2;
            } else if (s[j] == 2) {
                int op;
                if (j + 1 >= len) return -1;
                op = s[j + 1];
                if (op == 7) {
                    closed = 1;
                    j += 2;
                    break;
                }
                if (op < 1 || op > 9 || op == 0) return -1;
                j += (size_t)CODE_LEN[op];
            } else {
                j += 1;
            }
        }
        if (closed && j <= len) {
            as->bodyStart = j;
            i = j;
        }
    }
    while (i < len) {
        uint8_t b = s[i];
        if (b == 0) break;
        if (b == 1) {
            int n;
            if (i + 1 >= len) return -1;
            n = s[i + 1];
            if (s[i] == o->space[0] && n == o->space[1]) {
                if (atomPush(as, K_SPACE, o->width(o->ctx, 1, n), 0, i, 2)) return -1;
            } else {
                if (atomPush(as, K_GLYPH, o->width(o->ctx, 1, n), 0, i, 2)) return -1;
            }
            i += 2;
        } else if (b == 2) {
            int op;
            size_t l;
            if (i + 1 >= len) return -1;
            op = s[i + 1];
            if (op < 1 || op > 9) return -1;
            l = (size_t)CODE_LEN[op];
            if (i + l > len) return -1;
            if (op == 1) {
                if (atomPush(as, K_NL, 0, 0, i, l)) return -1;
            } else if (op == 3) {
                if (atomPush(as, K_PAGE, 0, 0, i, l)) return -1;
            } else if (op == 5) {
                if (atomPush(as, K_INS, 0, s[i + 2], i, l)) return -1;
            } else if (op == 9) {
                if (atomPush(as, K_INS, 0, -1, i, l)) return -1;
            } else {
                if (op == 8 && s[i + 2] < 6) nameBuf[s[i + 2]] = 1;
                if (atomPush(as, K_ZERO, 0, 0, i, l)) return -1;
            }
            i += l;
        } else if (b == 3) {
            if (atomPush(as, K_GLYPH, o->width(o->ctx, 2, 0), 0, i, 1)) return -1;
            i += 1;
        } else {
            if (atomPush(as, K_GLYPH, o->width(o->ctx, 0, b), 0, i, 1)) return -1;
            i += 1;
        }
    }
    for (k = 0; k < as->n; k++) {
        if (as->a[k].kind == K_INS) {
            int idx = as->a[k].arg;
            as->a[k].w = (idx < 0 || (idx < 6 && nameBuf[idx])) ? o->nameAllowance : o->numberAllowance;
        }
    }
    return 0;
}

static int lineOver(const Atoms *as, int from, int to, int maxWidth, int *widest)
{
    int w = 0, k, over = 0;
    for (k = from; k < to; k++) {
        int kind = as->a[k].kind;
        if (kind == K_NL || kind == K_PAGE) {
            if (w > *widest) *widest = w;
            if (w > maxWidth) over = 1;
            w = 0;
        } else if (kind == K_GLYPH || kind == K_SPACE || kind == K_INS) {
            w += as->a[k].w;
        }
    }
    if (w > *widest) *widest = w;
    if (w > maxWidth) over = 1;
    return over;
}

int trMaxLine(const TrOptions *o, const uint8_t *s, size_t len)
{
    Atoms as;
    int widest = 0;
    if (!o || !o->width) return 0;
    if (parse(o, s, len, &as)) {
        free(as.a);
        return 0;
    }
    (void)lineOver(&as, 0, as.n, 1 << 30, &widest);
    free(as.a);
    return widest;
}

void trOptionsInit(TrOptions *o, TrWidthFn width, void *ctx)
{
    int b, widest = 0;
    memset(o, 0, sizeof *o);
    o->width = width;
    o->ctx = ctx;
    o->space[0] = 1;
    o->space[1] = 1;
    /* the characters a name can hold are the digits and the letters: bytes 4 to 0x41 */
    for (b = 4; b <= 0x41; b++) {
        int w = width(ctx, 0, b);
        if (w > widest) widest = w;
    }
    o->nameAllowance = 8 * widest; /* the longest name the keyboard takes: 8 characters */
    o->numberAllowance = 6 * widest;
}

/* ---- output buffer ---- */

typedef struct Buf {
    uint8_t *d;
    size_t n;
    size_t cap;
    int bad;
} Buf;

static void bufAdd(Buf *b, const void *p, size_t n)
{
    if (b->bad) return;
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 128;
        uint8_t *nd;
        while (cap < b->n + n) cap *= 2;
        nd = (uint8_t *)realloc(b->d, cap);
        if (!nd) {
            b->bad = 1;
            return;
        }
        b->d = nd;
        b->cap = cap;
    }
    if (n) memcpy(b->d + b->n, p, n);
    b->n += n;
}

typedef struct Word {
    Buf b;
    int w;
} Word;

/* The page break the strings already use: `{pagina}` = a wait (02 02 02) then 02 03 */
static const uint8_t PAGE_SEQ[5] = {0x02, 0x02, 0x02, 0x02, 0x03};
static const uint8_t NL_SEQ[2] = {0x02, 0x01};

/* Rewrites the atoms [from, to) of one page (its closing 02 03, if any, is at to - 1 and is
   emitted by the caller) as words laid out greedily. */
static int wrapPage(const TrOptions *o, const TrBudget *bud, const uint8_t *s, const Atoms *as, int from, int to,
                    Buf *out, int *pagesAdded)
{
    Word *words = NULL;
    int nw = 0, cap = 0, k, r = 0;
    Buf pending; /* zero-width pieces before the first word */
    Buf cur;
    int curW = 0, curHas = 0;
    int spaceW = o->width(o->ctx, 1, o->space[1]);
    int lineW = 0, lineHas = 0, used = 0;

    memset(&pending, 0, sizeof pending);
    memset(&cur, 0, sizeof cur);
    for (k = from; k <= to; k++) {
        int kind = (k < to) ? as->a[k].kind : K_SPACE; /* a space closes the last word */
        if (kind == K_SPACE || kind == K_NL) {
            if (curHas || cur.n) {
                if (curW == 0 && nw > 0) {
                    bufAdd(&words[nw - 1].b, cur.d, cur.n); /* glue zero-width pieces to the word before */
                    free(cur.d);
                } else if (curW == 0) {
                    bufAdd(&pending, cur.d, cur.n);
                    free(cur.d);
                } else {
                    if (nw == cap) {
                        Word *nws;
                        cap = cap ? cap * 2 : 16;
                        nws = (Word *)realloc(words, (size_t)cap * sizeof *nws);
                        if (!nws) {
                            r = -1;
                            free(cur.d);
                            goto done;
                        }
                        words = nws;
                    }
                    words[nw].b = cur;
                    words[nw].w = curW;
                    if (pending.n) {
                        /* the pieces that came first lead the word */
                        Buf lead = pending;
                        bufAdd(&lead, cur.d, cur.n);
                        free(cur.d);
                        words[nw].b = lead;
                        memset(&pending, 0, sizeof pending);
                    }
                    nw++;
                }
                memset(&cur, 0, sizeof cur);
                curW = 0;
                curHas = 0;
            }
        } else {
            bufAdd(&cur, s + as->a[k].off, as->a[k].len);
            curW += as->a[k].w;
            curHas = 1;
        }
    }
    if (pending.n) { /* only zero-width pieces on the page: keep them */
        if (nw > 0) bufAdd(&words[nw - 1].b, pending.d, pending.n);
        else bufAdd(out, pending.d, pending.n);
        free(pending.d);
        memset(&pending, 0, sizeof pending);
    }
    for (k = 0; k < nw; k++) {
        int need = lineHas ? spaceW + words[k].w : words[k].w;
        if (lineHas && lineW + need > bud->maxWidth) {
            if (used + 1 >= bud->maxLines) {
                bufAdd(out, PAGE_SEQ, sizeof PAGE_SEQ);
                used = 0;
                (*pagesAdded)++;
            } else {
                bufAdd(out, NL_SEQ, sizeof NL_SEQ);
                used++;
            }
            lineW = 0;
            lineHas = 0;
            need = words[k].w;
        }
        if (lineHas) bufAdd(out, o->space, 2);
        bufAdd(out, words[k].b.d, words[k].b.n);
        lineW += need;
        lineHas = 1;
    }
done:
    for (k = 0; k < nw; k++) free(words[k].b.d);
    free(words);
    free(pending.d);
    if (out->bad) r = -1;
    return r;
}

int trReflow(const TrOptions *o, const TrBudget *b, const uint8_t *s, size_t len, uint8_t **out, size_t *outLen,
             int *pagesAdded, int *unfit)
{
    Atoms as;
    Buf res;
    int p, ps, widest = 0, anyOver = 0, pages = 0;

    if (pagesAdded) *pagesAdded = 0;
    if (unfit) *unfit = 0;
    if (!o || !o->width || !b || b->maxWidth <= 0) return 0;
    if (parse(o, s, len, &as)) {
        free(as.a);
        return -1;
    }
    for (p = 0, ps = 0; p <= as.n; p++) { /* is there a page with a line that does not fit? */
        if (p == as.n || as.a[p].kind == K_PAGE) {
            if (lineOver(&as, ps, p, b->maxWidth, &widest)) anyOver = 1;
            ps = p + 1;
        }
    }
    if (!anyOver) {
        free(as.a);
        return 0;
    }
    if (b->maxLines <= 1) { /* a single line cannot be wrapped: report it */
        if (unfit) *unfit = 1;
        free(as.a);
        return 0;
    }
    memset(&res, 0, sizeof res);
    bufAdd(&res, s, as.bodyStart);
    for (p = 0, ps = 0; p <= as.n; p++) {
        if (p == as.n || as.a[p].kind == K_PAGE) {
            int w2 = 0;
            int over = lineOver(&as, ps, p, b->maxWidth, &w2);
            size_t fromOff = ps < as.n ? as.a[ps].off : len;
            size_t toOff = p < as.n ? as.a[p].off : len;
            if (!over) {
                bufAdd(&res, s + fromOff, toOff - fromOff);
            } else if (wrapPage(o, b, s, &as, ps, p, &res, &pages)) {
                free(res.d);
                free(as.a);
                return -1;
            }
            if (p < as.n) bufAdd(&res, s + as.a[p].off, as.a[p].len);
            ps = p + 1;
        }
    }
    free(as.a);
    if (res.bad) {
        free(res.d);
        return -1;
    }
    if (res.n == len && memcmp(res.d, s, len) == 0) {
        free(res.d);
        return 0;
    }
    if (unfit) *unfit = trMaxLine(o, res.d, res.n) > b->maxWidth ? 1 : 0;
    if (pagesAdded) *pagesAdded = pages;
    *out = res.d;
    *outLen = res.n;
    return 1;
}

/* ---- the game's glyph tables ---- */

typedef struct TrGlyph {
    uint8_t page, u, v, clutX, clutY, w, h;
    int8_t dx, dy;
    uint8_t advance, unusedA;
} TrGlyph;

_Static_assert(sizeof(TrGlyph) == 11, "TrGlyph must mirror include/engine/text.h Glyph");

/* Linked in the game's binaries (src/engine/text/text_tools.c); weak so the pure tests link too */
extern TrGlyph FONT_GLYPHS_1[] __attribute__((weak));
extern TrGlyph FONT_ICONS_1[] __attribute__((weak));

#define ENGINE_GLYPHS 230 /* the EU sheet; FONT_STYLES[1].glyphCount is 234 = these plus 4 */
#define ENGINE_ICONS 114

static int engineWidth(void *ctx, int kind, int value)
{
    (void)ctx;
    /* the same sums as measureText (src/engine/text/text_tools.c) */
    if (kind == 0) {
        int i = value - 4;
        if (i < 0 || i >= ENGINE_GLYPHS) return FONT_GLYPHS_1[0].advance + FONT_GLYPHS_1[0].dx;
        return FONT_GLYPHS_1[i].advance + FONT_GLYPHS_1[i].dx;
    }
    if (kind == 1) {
        if (value <= 0 || value > ENGINE_ICONS) return 0;
        return FONT_ICONS_1[value - 1].advance + FONT_ICONS_1[value - 1].dx;
    }
    return FONT_GLYPHS_1[0].advance + FONT_GLYPHS_1[0].dx;
}

int trEngineMetrics(TrOptions *o)
{
    if (!FONT_GLYPHS_1 || !FONT_ICONS_1) return 0;
    trOptionsInit(o, engineWidth, NULL);
    return 1;
}

/* ---- layout.json ---- */

typedef struct Js {
    const char *start;
    const char *p;
    const char *end;
    char *err;
    size_t errSize;
} Js;

static int jfail(Js *j, const char *msg)
{
    if (j->err && j->errSize) snprintf(j->err, j->errSize, "layout: %s at byte %ld", msg, (long)(j->p - j->start));
    return -1;
}

static void jws(Js *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int jstr(Js *j, char *buf, size_t cap)
{
    size_t n = 0;
    jws(j);
    if (j->p >= j->end || *j->p != '"') return jfail(j, "expected a string");
    j->p++;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            c = *j->p++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'u') { /* not needed here: skip the 4 hex digits */
                j->p += (j->end - j->p >= 4) ? 4 : 0;
                c = '?';
            }
        }
        if (buf && n + 1 < cap) buf[n++] = c;
    }
    if (j->p >= j->end) return jfail(j, "unterminated string");
    j->p++;
    if (buf && cap) buf[n] = 0;
    return 0;
}

static int jskip(Js *j);

static int jskipContainer(Js *j, char open, char close)
{
    jws(j);
    j->p++; /* open */
    jws(j);
    if (j->p < j->end && *j->p == close) {
        j->p++;
        return 0;
    }
    for (;;) {
        if (open == '{') {
            if (jstr(j, NULL, 0)) return -1;
            jws(j);
            if (j->p >= j->end || *j->p != ':') return jfail(j, "expected ':'");
            j->p++;
        }
        if (jskip(j)) return -1;
        jws(j);
        if (j->p < j->end && *j->p == ',') {
            j->p++;
            continue;
        }
        if (j->p < j->end && *j->p == close) {
            j->p++;
            return 0;
        }
        return jfail(j, "expected ',' or the end of the container");
    }
}

static int jskip(Js *j)
{
    jws(j);
    if (j->p >= j->end) return jfail(j, "unexpected end");
    if (*j->p == '"') return jstr(j, NULL, 0);
    if (*j->p == '{') return jskipContainer(j, '{', '}');
    if (*j->p == '[') return jskipContainer(j, '[', ']');
    while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']' && !isspace((unsigned char)*j->p)) j->p++;
    return 0;
}

static int jint(Js *j, int *v)
{
    char *e;
    long n;
    jws(j);
    n = strtol(j->p, &e, 10);
    if (e == j->p) return jfail(j, "expected a number");
    j->p = e;
    *v = (int)n;
    return 0;
}

static int jbool(Js *j, int *v)
{
    jws(j);
    if (j->end - j->p >= 4 && !strncmp(j->p, "true", 4)) {
        *v = 1;
        j->p += 4;
        return 0;
    }
    if (j->end - j->p >= 5 && !strncmp(j->p, "false", 5)) {
        *v = 0;
        j->p += 5;
        return 0;
    }
    return jfail(j, "expected true or false");
}

/* Iterates the members of an object: calls fn(key) with j at the value */
static int jobject(Js *j, int (*fn)(Js *, const char *, void *), void *arg)
{
    char key[64];
    jws(j);
    if (j->p >= j->end || *j->p != '{') return jfail(j, "expected an object");
    j->p++;
    jws(j);
    if (j->p < j->end && *j->p == '}') {
        j->p++;
        return 0;
    }
    for (;;) {
        if (jstr(j, key, sizeof key)) return -1;
        jws(j);
        if (j->p >= j->end || *j->p != ':') return jfail(j, "expected ':'");
        j->p++;
        if (fn(j, key, arg)) return -1;
        jws(j);
        if (j->p < j->end && *j->p == ',') {
            j->p++;
            continue;
        }
        if (j->p < j->end && *j->p == '}') {
            j->p++;
            return 0;
        }
        return jfail(j, "expected ',' or '}'");
    }
}

static int entryMember(Js *j, const char *key, void *arg)
{
    TrEntryRule *e = (TrEntryRule *)arg;
    if (!strcmp(key, "noreflow")) return jbool(j, &e->noreflow);
    if (!strcmp(key, "maxWidthPx")) return jint(j, &e->maxWidth);
    if (!strcmp(key, "maxLines")) return jint(j, &e->maxLines);
    return jskip(j);
}

static int entriesMember(Js *j, const char *key, void *arg)
{
    TrFileRule *f = (TrFileRule *)arg;
    TrEntryRule *ne = (TrEntryRule *)realloc(f->entries, (size_t)(f->entryCount + 1) * sizeof *ne);
    TrEntryRule *e;
    const char *colon;
    if (!ne) return jfail(j, "out of memory");
    f->entries = ne;
    e = &f->entries[f->entryCount];
    memset(e, 0, sizeof *e);
    colon = strchr(key, ':');
    if (colon) {
        e->part = atoi(key);
        e->index = atoi(colon + 1);
    } else {
        e->part = -1;
        e->index = atoi(key);
    }
    f->entryCount++;
    return jobject(j, entryMember, e);
}

static int fileMember(Js *j, const char *key, void *arg)
{
    TrFileRule *f = (TrFileRule *)arg;
    if (!strcmp(key, "noreflow")) return jbool(j, &f->noreflow);
    if (!strcmp(key, "maxWidthPx")) return jint(j, &f->maxWidth);
    if (!strcmp(key, "maxLines")) return jint(j, &f->maxLines);
    if (!strcmp(key, "tightWidthPx")) return jint(j, &f->tightWidth);
    if (!strcmp(key, "entries")) return jobject(j, entriesMember, f);
    return jskip(j);
}

static int filesMember(Js *j, const char *key, void *arg)
{
    TrLayout *l = (TrLayout *)arg;
    TrFileRule *nf = (TrFileRule *)realloc(l->files, (size_t)(l->count + 1) * sizeof *nf);
    TrFileRule *f;
    if (!nf) return jfail(j, "out of memory");
    l->files = nf;
    f = &l->files[l->count];
    memset(f, 0, sizeof *f);
    snprintf(f->pattern, sizeof f->pattern, "%s", key);
    f->maxLines = 3;
    l->count++;
    return jobject(j, fileMember, f);
}

static int topMember(Js *j, const char *key, void *arg)
{
    if (!strcmp(key, "files")) return jobject(j, filesMember, arg);
    return jskip(j);
}

int trLayoutParse(const char *json, size_t len, TrLayout *out, char *err, size_t errSize)
{
    Js j;
    memset(out, 0, sizeof *out);
    j.start = json;
    j.p = json;
    j.end = json + len;
    j.err = err;
    j.errSize = errSize;
    if (jobject(&j, topMember, out)) {
        trLayoutFree(out);
        return -1;
    }
    return 0;
}

void trLayoutFree(TrLayout *l)
{
    int i;
    for (i = 0; i < l->count; i++) free(l->files[i].entries);
    free(l->files);
    memset(l, 0, sizeof *l);
}

const TrFileRule *trLayoutFind(const TrLayout *l, const char *base)
{
    const TrFileRule *best = NULL;
    size_t bestLen = 0;
    int i;
    for (i = 0; i < l->count; i++) {
        const TrFileRule *f = &l->files[i];
        size_t n = strlen(f->pattern);
        if (n && f->pattern[n - 1] == '*') {
            if (!strncmp(base, f->pattern, n - 1) && n - 1 >= bestLen && (!best || n - 1 > bestLen)) {
                best = f;
                bestLen = n - 1;
            }
        } else if (!strcmp(base, f->pattern)) {
            return f;
        }
    }
    return best;
}

/* ---- documents ---- */

static const TrEntryRule *entryRule(const TrFileRule *f, int part, int index)
{
    int i;
    for (i = 0; i < f->entryCount; i++) {
        if (f->entries[i].part == part && f->entries[i].index == index) return &f->entries[i];
    }
    return NULL;
}

static int reflowLang(TcLang *lang, const TcLang *base, int part, const TrOptions *o, const TrFileRule *rule,
                      const char *name, TrStats *st, char *err, size_t errSize)
{
    int i;
    if (lang->count != base->count) {
        if (err && errSize) snprintf(err, errSize, "entry count differs from the original (%d, %d)", lang->count, base->count);
        return -1;
    }
    for (i = 0; i < lang->count; i++) {
        const TcEntry *e = &lang->entries[i];
        const TcEntry *be = &base->entries[i];
        const TrEntryRule *er;
        TrBudget bud;
        uint8_t *out = NULL;
        size_t outLen = 0;
        int pages = 0, unfit = 0, r;

        if (e->bodyLen == be->bodyLen && (e->bodyLen == 0 || memcmp(e->body, be->body, e->bodyLen) == 0)) continue;
        st->checked++;
        er = entryRule(rule, part, i);
        if (er && er->noreflow) continue;
        bud.maxLines = (er && er->maxLines) ? er->maxLines : rule->maxLines;
        bud.maxWidth = (er && er->maxWidth) ? er->maxWidth : rule->maxWidth;
        if (!(er && er->maxWidth) && rule->tightWidth > 0 && trMaxLine(o, be->body, be->bodyLen) <= rule->tightWidth) {
            bud.maxWidth = rule->tightBudget > 0 ? rule->tightBudget : rule->tightWidth; /* the original fit a talk box: so does this */
        }
        r = trReflow(o, &bud, e->body, e->bodyLen, &out, &outLen, &pages, &unfit);
        if (r == 1) {
            char *text = tcDecode(out, outLen);
            int rc;
            free(out);
            if (!text) {
                if (err && errSize) snprintf(err, errSize, "out of memory");
                return -1;
            }
            rc = tcSetText(lang, i, text, err, errSize);
            free(text);
            if (rc) return -1;
            st->changed++;
            st->pagesAdded += pages;
        } else if (r == 0 && trMaxLine(o, e->body, e->bodyLen) > bud.maxWidth) {
            unfit = 1;
        }
        if (unfit) {
            st->unfit++;
            fprintf(stderr, "text: reflow %s part=%d index=%d: a line does not fit %d px (a word wider than the box, or a one-line box)\n",
                    name ? name : "?", part, i, bud.maxWidth);
        }
    }
    return 0;
}

int trReflowDoc(TcDoc *doc, const TcDoc *base, const TrOptions *o, const TrFileRule *rule, const char *name,
                TrStats *stats, char *err, size_t errSize)
{
    TrStats local;
    int p;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof *stats);
    if (!o || !o->width || !rule || rule->noreflow || rule->maxWidth <= 0) return 0;
    if (doc->isPack != base->isPack || doc->partCount != base->partCount) {
        if (err && errSize) snprintf(err, errSize, "structure differs from the original");
        return -1;
    }
    if (!doc->isPack) {
        if (reflowLang(&doc->lang, &base->lang, -1, o, rule, name, stats, err, errSize)) return -1;
    } else {
        for (p = 0; p < doc->partCount; p++) {
            if (!doc->parts[p].isLang || !base->parts[p].isLang) continue;
            if (reflowLang(&doc->parts[p].lang, &base->parts[p].lang, p, o, rule, name, stats, err, errSize)) return -1;
        }
    }
    /* a string that still does not fit is a problem (always shown); the per-file totals are trace detail */
    if (stats->unfit) {
        plogLine("text", "reflow %s: %d of %d changed strings rewritten (%d page breaks added), %d still do not fit\n",
                 name ? name : "?", stats->changed, stats->checked, stats->pagesAdded, stats->unfit);
    } else if (stats->changed) {
        PLOG("text", PLOG_TRACE, "reflow %s: %d of %d changed strings rewritten (%d page breaks added)\n",
             name ? name : "?", stats->changed, stats->checked, stats->pagesAdded);
    }
    return 0;
}
