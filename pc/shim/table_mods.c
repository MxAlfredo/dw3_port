/*
 * Data table mods: see table_mods.h and ADR-007.
 */
#include "table_mods.h"

#include "mods.h"
#include "port_log.h"
#include "text_data.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OP_SET, OP_MUL, OP_ADD, OP_LIST_SET, OP_LIST_ADD, OP_LIST_REMOVE };

/* the longest list a field may declare (TmField.listMax): a sanity bound only, the buffers are sized from listMax on the
   heap. A descriptor above it is refused with a log line at registration. */
#define TM_LIST_CAP 65536

typedef struct Op {
    int row;    /* index of the row, or -1 for "*" */
    int field;  /* index in the table's fields */
    int kind;
    double val;
    long long *list; /* the ids of a list operation */
    int nlist;
    long long ext;   /* row == OP_ROW_EXT: the id of the record outside the table (TmTable.extRow) */
} Op;

#define OP_ROW_EXT (-3)

typedef struct Patch {
    int mod;    /* index in the active mods (the order) */
    Op *ops;
    int n, cap;
    long long curExt; /* while parsing: the ext id the row being read belongs to (row == OP_ROW_EXT) */
} Patch;

/* a field of an outside record, as it was before the first patch changed it */
typedef struct ExtOrig {
    long long id;
    int field;
    long long value;
} ExtOrig;

typedef struct Entry {
    TmTable t;
    unsigned char *orig; /* snapshot of the rows, rows * stride bytes */
    void **lists;        /* the patched lists' buffers, one per row and field (lazily), owned by the engine */
    Patch *patches;
    int npatches;
    int logged;
    ExtOrig *extOrig;
    int nExtOrig;
    unsigned char *stage; /* a table with rowAt: the contiguous working copy of its rows (t.base points here) */
} Entry;

/* the registry grows on demand; each entry is its own allocation so the pointers findEntry hands out stay valid */
static Entry **gEntries;
static int gCount, gEntriesCap;
static char gDataDir[1024];

/* ---- field access ---- */

static size_t typeSize(TmType t)
{
    switch (t) {
    case TM_S32:
    case TM_U32: return 4;
    case TM_S16:
    case TM_U16: return 2;
    default: return 1;
    }
}

static void typeRange(TmType t, long long *lo, long long *hi)
{
    switch (t) {
    case TM_S32: *lo = -2147483647LL - 1; *hi = 2147483647LL; break;
    case TM_U32: *lo = 0; *hi = 4294967295LL; break;
    case TM_S16: *lo = -32768; *hi = 32767; break;
    case TM_U16: *lo = 0; *hi = 65535; break;
    case TM_S8: *lo = -128; *hi = 127; break;
    default: *lo = 0; *hi = 255; break;
    }
}

static long long fieldGet(const unsigned char *p, TmType t)
{
    switch (t) {
    case TM_S32: { int32_t v; memcpy(&v, p, 4); return v; }
    case TM_U32: { uint32_t v; memcpy(&v, p, 4); return v; }
    case TM_S16: { int16_t v; memcpy(&v, p, 2); return v; }
    case TM_U16: { uint16_t v; memcpy(&v, p, 2); return v; }
    case TM_S8: return (signed char)p[0];
    default: return p[0];
    }
}

static void fieldSet(unsigned char *p, TmType t, long long x)
{
    switch (t) {
    case TM_S32: { int32_t v = (int32_t)x; memcpy(p, &v, 4); break; }
    case TM_U32: { uint32_t v = (uint32_t)x; memcpy(p, &v, 4); break; }
    case TM_S16: { int16_t v = (int16_t)x; memcpy(p, &v, 2); break; }
    case TM_U16: { uint16_t v = (uint16_t)x; memcpy(p, &v, 2); break; }
    default: p[0] = (unsigned char)x; break;
    }
}

/* A field with a mask (TmField.mask) is the bits of an unsigned value, read and written shifted down: the other bits of the
   word are never touched. Without one it is the whole value. */
static int maskShift(unsigned long long m)
{
    int s = 0;
    while (m && !(m & 1)) { m >>= 1; s++; }
    return s;
}

static long long fdGet(const TmField *fd, const unsigned char *row)
{
    long long v = fieldGet(row + fd->off, fd->type);
    if (!fd->mask) return v;
    return (long long)(((unsigned long long)v & fd->mask) >> maskShift(fd->mask));
}

static void fdSet(const TmField *fd, unsigned char *row, long long x)
{
    unsigned long long old, nv;
    if (!fd->mask) {
        fieldSet(row + fd->off, fd->type, x);
        return;
    }
    old = (unsigned long long)fieldGet(row + fd->off, fd->type);
    nv = (old & ~fd->mask) | (((unsigned long long)x << maskShift(fd->mask)) & fd->mask);
    fieldSet(row + fd->off, fd->type, (long long)nv);
}

static long long roundClamp(double x, long long lo, long long hi)
{
    long long r;
    if (x != x) return lo;
    if (x > 1e15) x = 1e15;
    if (x < -1e15) x = -1e15;
    r = x < 0 ? -(long long)(-x + 0.5) : (long long)(x + 0.5);
    return r < lo ? lo : r > hi ? hi : r;
}

/* The key of row r in `rows` (this table's layout) */
static long keyOfRow(const TmTable *t, const unsigned char *rows, int r)
{
    if (t->keys) return t->keys[r];
    if (t->keyFromRow) return (long)fieldGet(rows + (size_t)r * t->stride + t->keyOff, t->keyType);
    return t->keyBase + r;
}

/* The index of the row with that key in `rows` (n rows), or -1 */
static int findKey(const TmTable *t, const unsigned char *rows, int n, long key)
{
    int i;
    if (!t->keyFromRow && !t->keys) return (key >= t->keyBase && key < t->keyBase + n) ? (int)(key - t->keyBase) : -1;
    for (i = 0; i < n; i++) {
        if (keyOfRow(t, rows, i) == key) return i;
    }
    return -1;
}

/* ---- strict JSON cursor ---- */

typedef struct Cur {
    const char *p, *end;
    char err[160];
} Cur;

static int fail(Cur *c, const char *msg)
{
    if (!c->err[0]) snprintf(c->err, sizeof c->err, "%s", msg);
    return -1;
}

static void ws(Cur *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')) c->p++;
}

static int eat(Cur *c, char ch)
{
    ws(c);
    if (c->p < c->end && *c->p == ch) {
        c->p++;
        return 0;
    }
    {
        char m[48];
        snprintf(m, sizeof m, "expected '%c'", ch);
        return fail(c, m);
    }
}

static int parseString(Cur *c, char *out, size_t cap)
{
    size_t n = 0;
    ws(c);
    if (c->p >= c->end || *c->p != '"') return fail(c, "expected a string");
    c->p++;
    while (c->p < c->end && *c->p != '"') {
        char ch = *c->p++;
        if ((unsigned char)ch < 0x20) return fail(c, "control character in a string");
        if (ch == '\\') {
            if (c->p >= c->end) return fail(c, "unfinished string");
            ch = *c->p++;
            switch (ch) {
            case '"': case '\\': case '/': break;
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'u': /* \uXXXX: keep a placeholder, keys and operations are ASCII */
                if (c->end - c->p < 4) return fail(c, "bad \\u escape");
                c->p += 4;
                ch = '?';
                break;
            default: return fail(c, "bad escape in a string");
            }
        }
        if (n + 1 >= cap) return fail(c, "string too long");
        out[n++] = ch;
    }
    if (c->p >= c->end) return fail(c, "unfinished string");
    c->p++;
    out[n] = 0;
    return 0;
}

static int parseNumber(Cur *c, double *out)
{
    const char *s;
    char buf[48], *e;
    size_t n = 0;
    ws(c);
    s = c->p;
    while (c->p < c->end && n < sizeof buf - 1 &&
           ((*c->p >= '0' && *c->p <= '9') || *c->p == '-' || *c->p == '+' || *c->p == '.' || *c->p == 'e' || *c->p == 'E')) {
        buf[n++] = *c->p++;
    }
    buf[n] = 0;
    if (n == 0 || (buf[0] != '-' && (buf[0] < '0' || buf[0] > '9'))) {
        c->p = s;
        return fail(c, "expected a number");
    }
    *out = strtod(buf, &e);
    if (*e || *out != *out) return fail(c, "bad number");
    return 0;
}

static int skipValue(Cur *c, int depth)
{
    ws(c);
    if (depth > 16) return fail(c, "nested too deep");
    if (c->p >= c->end) return fail(c, "unexpected end");
    if (*c->p == '"') {
        char tmp[512];
        return parseString(c, tmp, sizeof tmp);
    }
    if (*c->p == '{' || *c->p == '[') {
        char open = *c->p++, close = open == '{' ? '}' : ']';
        ws(c);
        if (c->p < c->end && *c->p == close) {
            c->p++;
            return 0;
        }
        for (;;) {
            if (open == '{') {
                char k[128];
                if (parseString(c, k, sizeof k) || eat(c, ':')) return -1;
            }
            if (skipValue(c, depth + 1)) return -1;
            ws(c);
            if (c->p < c->end && *c->p == ',') {
                c->p++;
                continue;
            }
            return eat(c, close);
        }
    }
    if (c->end - c->p >= 4 && !strncmp(c->p, "true", 4)) { c->p += 4; return 0; }
    if (c->end - c->p >= 5 && !strncmp(c->p, "false", 5)) { c->p += 5; return 0; }
    if (c->end - c->p >= 4 && !strncmp(c->p, "null", 4)) { c->p += 4; return 0; }
    {
        double d;
        return parseNumber(c, &d);
    }
}

/* ---- patch parsing ---- */

static int addOp(Patch *p, int row, int field, int kind, double val)
{
    if (p->n == p->cap) {
        int ncap = p->cap ? p->cap * 2 : 16;
        Op *no = (Op *)realloc(p->ops, (size_t)ncap * sizeof(Op));
        if (!no) return -1;
        p->ops = no;
        p->cap = ncap;
    }
    p->ops[p->n].row = row;
    p->ops[p->n].field = field;
    p->ops[p->n].kind = kind;
    p->ops[p->n].val = val;
    p->ops[p->n].list = NULL;
    p->ops[p->n].nlist = 0;
    p->ops[p->n].ext = row == OP_ROW_EXT ? p->curExt : 0;
    p->n++;
    return 0;
}

static void freeOps(Op *ops, int n)
{
    int i;
    for (i = 0; i < n; i++) free(ops[i].list);
    free(ops);
}

static int addListOp(Patch *p, int row, int field, int kind, const long long *v, int n)
{
    long long *copy = (long long *)malloc(((size_t)n + 1) * sizeof(long long));
    if (!copy) return -1;
    memcpy(copy, v, (size_t)n * sizeof(long long));
    if (addOp(p, row, field, kind, 0)) {
        free(copy);
        return -1;
    }
    p->ops[p->n - 1].list = copy;
    p->ops[p->n - 1].nlist = n;
    return 0;
}

/* "*k", "+k" or "-k" -> kind and amount; returns 0, or -1 with a reason */
static int parseOp(const char *s, int *kind, double *val, const char **why)
{
    char *e;
    double k;
    if (*s != '*' && *s != '+' && *s != '-') {
        *why = "a text value must be an operation (\"*k\", \"+k\" or \"-k\")";
        return -1;
    }
    k = strtod(s + 1, &e);
    if (e == s + 1 || *e || k != k || k > 1e15 || k < -1e15) {
        *why = "the operation has no valid number";
        return -1;
    }
    *kind = *s == '*' ? OP_MUL : OP_ADD;
    *val = *s == '-' ? -k : k;
    return 0;
}

static int rowHasField(const Patch *p, int row, int field)
{
    int i;
    for (i = 0; i < p->n; i++) {
        if (p->ops[i].row == row && p->ops[i].field == field && (row != OP_ROW_EXT || p->ops[i].ext == p->curExt)) return 1;
    }
    return 0;
}

/* A JSON array of ids for a list field. Returns -1 for a syntax error; otherwise 0 with the ids in v and their count in n,
   or a reason in *why (the value is then rejected, the array is still consumed). */
static int parseIdList(Cur *c, const TmField *fd, long long *v, int *n, const char **why)
{
    int count = 0;

    *n = 0;
    *why = NULL;
    if (eat(c, '[')) return -1;
    ws(c);
    if (c->p < c->end && *c->p == ']') {
        c->p++;
        return 0;
    }
    for (;;) {
        ws(c);
        if (c->p < c->end && (*c->p == '-' || (*c->p >= '0' && *c->p <= '9'))) {
            double d;
            if (parseNumber(c, &d)) return -1;
            if (d != (double)(long long)d) {
                if (!*why) *why = "a list holds whole numbers";
            } else if (d < (double)fd->min || d > (double)fd->max) {
                if (!*why) *why = "an id is outside the valid range";
            } else if (fd->idOk && !fd->idOk((long long)d)) {
                if (!*why) *why = "no installed mod defines that id";
            } else if (count < fd->listMax) {
                v[(*n)++] = (long long)d;
            }
        } else if (fd->byName && c->p < c->end && *c->p == '"') {
            char nm[128];
            long long id;
            if (parseString(c, nm, sizeof nm)) return -1;
            id = fd->byName(nm);
            if (id < fd->min || id > fd->max) {
                if (!*why) *why = "no installed mod defines that name";
            } else if (count < fd->listMax) {
                v[(*n)++] = id;
            }
        } else {
            if (skipValue(c, 1)) return -1;
            if (!*why) *why = "a list holds numbers";
        }
        count++;
        ws(c);
        if (c->p < c->end && *c->p == ',') {
            c->p++;
            continue;
        }
        if (eat(c, ']')) return -1;
        break;
    }
    if (!*why && count > fd->listMax) *why = "more than the list can hold";
    return 0;
}

/* The value of a list field: an array (replaces) or {"add": [...], "remove": [...]} */
static int parseListFieldBuf(Cur *c, const Entry *e, const char *path, const char *rowKey, int row, int f, Patch *p, long long *v,
                             long long *r);

static int parseListField(Cur *c, const Entry *e, const char *path, const char *rowKey, int row, int f, Patch *p)
{
    const TmField *fd = &e->t.fields[f];
    long long *v, *r;
    int rc;

    v = (long long *)malloc(((size_t)fd->listMax + 1) * sizeof *v);
    r = (long long *)malloc(((size_t)fd->listMax + 1) * sizeof *r);
    if (!v || !r) {
        free(v);
        free(r);
        return fail(c, "out of memory");
    }
    rc = parseListFieldBuf(c, e, path, rowKey, row, f, p, v, r);
    free(v);
    free(r);
    return rc;
}

static int parseListFieldBuf(Cur *c, const Entry *e, const char *path, const char *rowKey, int row, int f, Patch *p, long long *v,
                             long long *r)
{
    const TmField *fd = &e->t.fields[f];
    int nv = 0, nr = 0, haveAdd = 0, haveRem = 0, bad = 0;
    const char *why = NULL;

    ws(c);
    if (c->p < c->end && *c->p == '[') {
        if (parseIdList(c, fd, v, &nv, &why)) return -1;
        if (!why && nv == 0) why = "an empty list";
        if (why) {
            plogLine("tables", "%s row \"%s\" field \"%s\" rejected: %s", path, rowKey, fd->name, why);
            if (!strcmp(why, "more than the list can hold")) plogRaw(" (more than %d items)", fd->listMax);
            plogRaw("\n");
        } else if (addListOp(p, row, f, OP_LIST_SET, v, nv)) {
            return fail(c, "out of memory");
        }
        return 0;
    }
    if (c->p < c->end && *c->p == '{') {
        c->p++;
        ws(c);
        if (c->p < c->end && *c->p == '}') {
            c->p++;
            plogLine("tables", "%s row \"%s\" field \"%s\" rejected: nothing to add or remove\n", path, rowKey, fd->name);
            return 0;
        }
        for (;;) {
            char k[32];
            if (parseString(c, k, sizeof k) || eat(c, ':')) return -1;
            if (!strcmp(k, "add") || !strcmp(k, "remove")) {
                int isAdd = k[0] == 'a', *have = isAdd ? &haveAdd : &haveRem;
                long long *dst = isAdd ? v : r;
                int *dn = isAdd ? &nv : &nr;
                const char *w = NULL;
                if (*have) return fail(c, "a list operation is listed twice");
                *have = 1;
                ws(c);
                if (c->p < c->end && *c->p == '[') {
                    if (parseIdList(c, fd, dst, dn, &w)) return -1;
                    if (w) {
                        plogLine("tables", "%s row \"%s\" field \"%s\" rejected: \"%s\": %s\n", path, rowKey, fd->name, k, w);
                        bad = 1;
                    }
                } else {
                    plogLine("tables", "%s row \"%s\" field \"%s\" rejected: \"%s\" must be a list\n", path, rowKey, fd->name, k);
                    bad = 1;
                    if (skipValue(c, 1)) return -1;
                }
            } else {
                plogLine("tables", "%s row \"%s\" field \"%s\" rejected: unknown key \"%s\" (use \"add\" and \"remove\")\n", path,
                        rowKey, fd->name, k);
                bad = 1;
                if (skipValue(c, 1)) return -1;
            }
            ws(c);
            if (c->p < c->end && *c->p == ',') {
                c->p++;
                continue;
            }
            if (eat(c, '}')) return -1;
            break;
        }
        if (bad) return 0;
        if (haveRem && nr > 0 && addListOp(p, row, f, OP_LIST_REMOVE, r, nr)) return fail(c, "out of memory");
        if (haveAdd && nv > 0 && addListOp(p, row, f, OP_LIST_ADD, v, nv)) return fail(c, "out of memory");
        return 0;
    }
    plogLine("tables", "%s row \"%s\" field \"%s\" rejected: a list field takes a list, or {\"add\": [...], \"remove\": [...]}\n", path,
            rowKey, fd->name);
    return skipValue(c, 0);
}

static int parseRowFields(Cur *c, const Entry *e, const char *path, const char *rowKey, int row, Patch *p)
{
    ws(c);
    if (eat(c, '{')) return -1;
    ws(c);
    if (c->p < c->end && *c->p == '}') {
        c->p++;
        return 0;
    }
    for (;;) {
        char name[64];
        int f = -1, i;
        if (parseString(c, name, sizeof name) || eat(c, ':')) return -1;
        for (i = 0; i < e->t.nfields; i++) {
            if (!strcmp(e->t.fields[i].name, name)) f = i;
        }
        ws(c);
        if (f < 0) {
            plogLine("tables", "%s row \"%s\" field \"%s\" rejected: unknown field\n", path, rowKey, name);
            if (skipValue(c, 0)) return -1;
        } else if (rowHasField(p, row, f)) {
            return fail(c, "a field is listed twice in one row");
        } else if (e->t.fields[f].listMax > 0) {
            if (parseListField(c, e, path, rowKey, row, f, p)) return -1;
        } else if (c->p < c->end && *c->p == '"') {
            char s[128];
            int kind;
            double val;
            const char *why = NULL;
            const TmField *fd = &e->t.fields[f];
            if (parseString(c, s, sizeof s)) return -1;
            if (fd->byName && strchr(s, ':')) {
                long long id = fd->byName(s);
                if (id < fd->min || id > fd->max) {
                    plogLine("tables", "%s row \"%s\" field \"%s\" rejected: no installed mod defines \"%s\"\n", path, rowKey, name, s);
                } else if (addOp(p, row, f, OP_SET, (double)id)) {
                    return fail(c, "out of memory");
                }
            } else if (parseOp(s, &kind, &val, &why)) {
                plogLine("tables", "%s row \"%s\" field \"%s\" rejected: %s\n", path, rowKey, name, why);
            } else if (addOp(p, row, f, kind, val)) {
                return fail(c, "out of memory");
            }
        } else if (c->p < c->end && (*c->p == '-' || (*c->p >= '0' && *c->p <= '9'))) {
            double d;
            const TmField *fd = &e->t.fields[f];
            if (parseNumber(c, &d)) return -1;
            if (d < (double)fd->min - 0.5 || d > (double)fd->max + 0.5) {
                plogLine("tables", "%s row \"%s\" field \"%s\" rejected: %g is outside %lld to %lld\n", path, rowKey, name, d,
                        fd->min, fd->max);
            } else if (fd->idOk && (d != (double)(long long)d || !fd->idOk((long long)d))) {
                plogLine("tables", "%s row \"%s\" field \"%s\" rejected: no installed mod defines the id %g\n", path, rowKey, name, d);
            } else if (addOp(p, row, f, OP_SET, d)) {
                return fail(c, "out of memory");
            }
        } else {
            plogLine("tables", "%s row \"%s\" field \"%s\" rejected: not a number or an operation\n", path, rowKey, name);
            if (skipValue(c, 0)) return -1;
        }
        ws(c);
        if (c->p < c->end && *c->p == ',') {
            c->p++;
            continue;
        }
        return eat(c, '}');
    }
}

static int parseRows(Cur *c, const Entry *e, const char *path, Patch *p)
{
    unsigned char *seen = (unsigned char *)calloc((size_t)e->t.rows + 1, 1); /* [rows] is "*" */
    int rc = 0;
    if (!seen) return fail(c, "out of memory");
    ws(c);
    if (eat(c, '{')) { free(seen); return -1; }
    ws(c);
    if (c->p < c->end && *c->p == '}') {
        c->p++;
        free(seen);
        return 0;
    }
    for (;;) {
        char key[64];
        int row = -2;
        if (parseString(c, key, sizeof key) || eat(c, ':')) { rc = -1; break; }
        if (!strcmp(key, "*")) {
            row = -1;
        } else {
            char *end;
            long k = strtol(key, &end, 10);
            int plain = key[0] >= '0' && key[0] <= '9' && !*end && (key[0] != '0' || key[1] == 0);
            if (plain) {
                int at = findKey(&e->t, e->orig, e->t.rows, k);
                if (at >= 0) row = at;
            } else if (e->t.keyByName && e->t.base && strchr(key, ':')) {
                long long id = e->t.keyByName(key);
                int at = id >= 0 ? findKey(&e->t, e->orig, e->t.rows, id) : -1;
                if (at >= 0) {
                    row = at; /* an original's name ("dw3:item/43"): the row of the table, like its number */
                } else if (id >= 0) {
                    row = OP_ROW_EXT;
                    p->curExt = id;
                } else {
                    plogLine("tables", "%s row \"%s\" rejected: no installed mod defines that name\n", path, key);
                    row = -4;
                }
            }
        }
        if (row == -4) {
            if (skipValue(c, 0)) { rc = -1; break; }
        } else if (row == -2) {
            plogLine("tables", "%s row \"%s\" rejected: no row with that key\n", path, key);
            if (skipValue(c, 0)) { rc = -1; break; }
        } else {
            unsigned char dummy = 0;
            unsigned char *s = row == OP_ROW_EXT ? &dummy : &seen[row < 0 ? e->t.rows : row];
            ws(c);
            if (*s) { rc = fail(c, "a row key is listed twice"); break; }
            *s = 1;
            if (c->p >= c->end || *c->p != '{') {
                plogLine("tables", "%s row \"%s\" rejected: not an object of fields\n", path, key);
                if (skipValue(c, 0)) { rc = -1; break; }
            } else if (parseRowFields(c, e, path, key, row, p)) {
                rc = -1;
                break;
            }
        }
        ws(c);
        if (c->p < c->end && *c->p == ',') {
            c->p++;
            continue;
        }
        rc = eat(c, '}');
        break;
    }
    free(seen);
    return rc;
}

/* Parse a whole patch file. Returns 0 (ops in *p) or -1 with the reason in err. */
static int parsePatch(const Entry *e, const char *path, const char *text, size_t len, Patch *p, char *err, size_t errSize)
{
    Cur c;
    int sawFormat = 0, sawRows = 0, rc = 0;
    char want[64];

    memset(&c, 0, sizeof c);
    c.p = text;
    c.end = text + len;
    snprintf(want, sizeof want, "%s-v1", e->t.name);
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) c.p += 3;
    if (eat(&c, '{')) rc = -1;
    ws(&c);
    if (rc == 0 && c.p < c.end && *c.p == '}') {
        c.p++;
    } else if (rc == 0) {
        for (;;) {
            char key[64];
            if (parseString(&c, key, sizeof key) || eat(&c, ':')) { rc = -1; break; }
            if (!strcmp(key, "format")) {
                char f[64];
                if (parseString(&c, f, sizeof f)) { rc = -1; break; }
                if (strcmp(f, want)) {
                    snprintf(c.err, sizeof c.err, "unknown format \"%s\" (expected \"%s\")", f, want);
                    rc = -1;
                    break;
                }
                sawFormat = 1;
            } else if (!strcmp(key, "rows")) {
                if (sawRows) { rc = fail(&c, "\"rows\" is listed twice"); break; }
                sawRows = 1;
                if (parseRows(&c, e, path, p)) { rc = -1; break; }
            } else if (skipValue(&c, 0)) { /* "notes" and anything else is ignored */
                rc = -1;
                break;
            }
            ws(&c);
            if (c.p < c.end && *c.p == ',') {
                c.p++;
                continue;
            }
            if (eat(&c, '}')) rc = -1;
            break;
        }
    }
    if (rc == 0) {
        ws(&c);
        if (c.p != c.end) rc = fail(&c, "text after the object");
        else if (!sawFormat) {
            snprintf(c.err, sizeof c.err, "no \"format\" (expected \"%s\")", want);
            rc = -1;
        }
    }
    if (rc) snprintf(err, errSize, "%s", c.err[0] ? c.err : "not valid JSON");
    return rc;
}

/* ---- reference export ---- */

typedef struct Buf {
    char *b;
    size_t n, cap;
    int bad;
} Buf;

static void put(Buf *B, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Buf *B, const char *fmt, ...)
{
    va_list ap;
    int w;
    if (B->bad) return;
    for (;;) {
        va_start(ap, fmt);
        w = vsnprintf(B->b ? B->b + B->n : NULL, B->cap - B->n, fmt, ap);
        va_end(ap);
        if (w < 0) { B->bad = 1; return; }
        if (B->b && B->n + (size_t)w < B->cap) {
            B->n += (size_t)w;
            return;
        }
        {
            size_t ncap = B->cap ? B->cap * 2 : 4096;
            char *nb;
            while (ncap < B->n + (size_t)w + 1) ncap *= 2;
            nb = (char *)realloc(B->b, ncap);
            if (!nb) { B->bad = 1; return; }
            B->b = nb;
            B->cap = ncap;
        }
    }
}

/* The elements of a list field of a row (at most listMax; the registration checked the original rows) */
static int readList(const unsigned char *row, const TmField *fd, long long *out)
{
    int32_t count;
    const unsigned char *items;
    int i;

    memcpy(&items, row + fd->off, sizeof items);
    if (fd->countOff == TM_NO_COUNT) {
        /* no count in the row: the list runs up to its 0 (one element past listMax is enough to tell it is too long) */
        if (!items) return 0;
        for (count = 0; count <= fd->listMax && fieldGet(items + (size_t)count * typeSize(fd->type), fd->type) != 0; count++) {
        }
        if (count > fd->listMax) return 0;
    } else {
        memcpy(&count, row + fd->countOff, 4);
    }
    if (count < 0 || count > fd->listMax || (count > 0 && !items)) return 0;
    for (i = 0; i < count; i++) out[i] = fieldGet(items + (size_t)i * typeSize(fd->type), fd->type);
    return count;
}

/* a note as the inside of a JSON string: quotes, backslashes and control characters escaped */
static void putEsc(Buf *B, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') put(B, "\\%c", c);
        else if (c < 0x20) put(B, "\\u%04x", c);
        else put(B, "%c", c);
    }
}

static char *referenceText(const Entry *e)
{
    Buf B = {0};
    int r, f;

    put(&B, "{\n  \"format\": \"%s-v1\",\n  \"notes\": {\n", e->t.name);
    put(&B, "    \"about\": \"Reference copy of the original values, written by the port. Editing this file does not change the game: put your changes in a patch, mods/<mod>/tables/%s.json (same format, only what changes; a number replaces, \\\"*k\\\" multiplies, \\\"+k\\\" and \\\"-k\\\" add; the key \\\"*\\\" is every row).\",\n", e->t.name);
    put(&B, "    \"key\": \"");
    putEsc(&B, e->t.keyNote);
    put(&B, "\",\n    \"when\": \"");
    putEsc(&B, e->t.whenNote);
    put(&B, "\",\n");
    for (f = 0; f < e->t.nfields; f++) {
        const TmField *fd = &e->t.fields[f];
        if (fd->listMax > 0) {
            put(&B, "    \"%s\": \"", fd->name);
            putEsc(&B, fd->note);
            put(&B, " A list of 1 up to %d numbers, each valid from %lld to %lld (0 ends the list, so it is never an element). A patch gives the whole list ([1, 2, 3]) or edits it ({\\\"remove\\\": [1], \\\"add\\\": [9]}).\"%s\n", fd->listMax, fd->min, fd->max, f + 1 < e->t.nfields ? "," : "");
        } else {
            put(&B, "    \"%s\": \"", fd->name);
            putEsc(&B, fd->note);
            put(&B, " Valid: %lld to %lld.\"%s\n", fd->min, fd->max, f + 1 < e->t.nfields ? "," : "");
        }
    }
    put(&B, "  },\n");
    if (e->t.rowLabel) {
        int any = 0;
        for (r = 0; r < e->t.rows; r++) {
            char label[160];
            label[0] = 0;
            e->t.rowLabel(r, label, sizeof label);
            if (!label[0]) continue;
            put(&B, "%s    \"%ld\": \"", any ? ",\n" : "  \"labels\": {\n", keyOfRow(&e->t, e->orig, r));
            putEsc(&B, label);
            put(&B, "\"");
            any = 1;
        }
        if (any) put(&B, "\n  },\n");
    }
    put(&B, "  \"rows\": {\n");
    for (r = 0; r < e->t.rows; r++) {
        const unsigned char *row = e->orig + (size_t)r * e->t.stride;
        put(&B, "    \"%ld\": {", keyOfRow(&e->t, e->orig, r));
        for (f = 0; f < e->t.nfields; f++) {
            const TmField *fd = &e->t.fields[f];
            if (fd->listMax > 0) {
                long long *v = (long long *)malloc(((size_t)fd->listMax + 1) * sizeof *v);
                int n, k;
                if (!v) {
                    B.bad = 1;
                    continue;
                }
                n = readList(row, fd, v);
                put(&B, "%s \"%s\": [", f ? "," : "", fd->name);
                for (k = 0; k < n; k++) put(&B, "%s%lld", k ? ", " : "", v[k]);
                put(&B, "]");
                free(v);
                continue;
            }
            put(&B, "%s \"%s\": %lld", f ? "," : "", fd->name, fdGet(fd, row));
        }
        put(&B, " }%s\n", r + 1 < e->t.rows ? "," : "");
    }
    put(&B, "  }\n}\n");
    if (B.bad) {
        free(B.b);
        return NULL;
    }
    return B.b;
}

/* Writes one generated file of the data folder (<data>/<family>/<rel>) under the manifest rule of ADR-005: an unedited
   copy is replaced, an edited one stays and the new version goes beside it as <rel>.new. `man` is that family's manifest. */
static void exportText(const char *family, const char *rel, const char *want, TdManifest *man, int *manDirty)
{
    char path[1200], dir[1200], hex[17], wantHex[17];
    uint8_t *cur = NULL;
    size_t curLen = 0;
    int edited = 0;

    snprintf(path, sizeof path, "%s/%s/%s", gDataDir, family, rel);
    snprintf(dir, sizeof dir, "%s/%s", gDataDir, family);
    tdHashHex(tdHash(want, strlen(want)), wantHex);
    if (tdReadFile(path, &cur, &curLen) == 0) {
        tdHashHex(tdHash(cur, curLen), hex);
        free(cur);
        if (!strcmp(hex, wantHex)) {
            if (!tdManifestFind(man, rel)) { tdManifestSet(man, rel, hex); *manDirty = 1; }
            return;
        }
        edited = tdFileState(1, tdManifestFind(man, rel), hex) == TD_FILE_EDITED;
    }
    if (edited) { /* the user's copy stays; the new version goes beside it */
        char np[1300];
        snprintf(np, sizeof np, "%s.new", path);
        if (tdMakeDirs(dir) || tdWriteFileAtomic(np, want, strlen(want))) plogLine(family, "cannot write %s\n", np);
        else plogLine(family, "%s was edited, kept; the new version is %s.new\n", rel, rel);
    } else if (tdMakeDirs(dir) || tdWriteFileAtomic(path, want, strlen(want))) {
        plogLine(family, "cannot write the reference copy %s\n", path);
    } else {
        tdManifestSet(man, rel, wantHex);
        *manDirty = 1;
    }
}

static void exportReference(const Entry *e, TdManifest *man, int *manDirty)
{
    char rel[96];
    char *want = referenceText(e);

    snprintf(rel, sizeof rel, "%s.json", e->t.name);
    if (!want) return;
    exportText("tables", rel, want, man, manDirty);
    free(want);
}

void tmExportFile(const char *family, const char *rel, const char *text)
{
    TdManifest man;
    char mpath[1200];
    int dirty = 0;
    uint8_t *mt;
    size_t ml;

    if (!gDataDir[0] || !text) return;
    memset(&man, 0, sizeof man);
    snprintf(man.stamp, sizeof man.stamp, "%s-v1", family);
    snprintf(mpath, sizeof mpath, "%s/%s/manifest.json", gDataDir, family);
    if (tdReadFile(mpath, &mt, &ml) == 0) {
        TdManifest old;
        char err[120];
        memset(&old, 0, sizeof old);
        if (tdManifestParse((const char *)mt, ml, &old, err, sizeof err) == 0) {
            tdManifestFree(&man);
            man = old;
        }
        free(mt);
    }
    exportText(family, rel, text, &man, &dirty);
    if (dirty) {
        char *out = tdManifestFormat(&man);
        if (!out || tdWriteFileAtomic(mpath, out, strlen(out))) plogLine(family, "cannot write %s\n", mpath);
        free(out);
    }
    tdManifestFree(&man);
}

/* ---- registration and application ---- */

void tmClear(void)
{
    int i, j;
    for (i = 0; i < gCount; i++) {
        Entry *e = gEntries[i];
        for (j = 0; j < e->npatches; j++) freeOps(e->patches[j].ops, e->patches[j].n);
        free(e->patches);
        free(e->orig);
        free(e->extOrig);
        free(e->stage);
        if (e->lists) {
            for (j = 0; j < e->t.rows * e->t.nfields; j++) free(e->lists[j]);
            free(e->lists);
        }
        free(e);
    }
    free(gEntries);
    gEntries = NULL;
    gCount = gEntriesCap = 0;
}

void tmInit(const char *dataDir)
{
    tmClear();
    gDataDir[0] = 0;
    if (dataDir && dataDir[0]) snprintf(gDataDir, sizeof gDataDir, "%s", dataDir);
}

int tmTableCount(void) { return gCount; }
const char *tmTableName(int i) { return (i >= 0 && i < gCount) ? gEntries[i]->t.name : NULL; }

static Entry *findEntry(const char *name)
{
    int i;
    for (i = 0; i < gCount; i++) {
        if (!strcmp(gEntries[i]->t.name, name)) return gEntries[i];
    }
    return NULL;
}

static void loadPatches(Entry *e)
{
    int mi;
    for (mi = 0; mi < modsCount(); mi++) {
        char path[1200], err[160];
        uint8_t *data;
        size_t len;
        Patch p, *np;

        snprintf(path, sizeof path, "%s/tables/%s.json", modsDirOf(mi), e->t.name);
        if (tdReadFile(path, &data, &len)) continue; /* the mod does not patch this table */
        memset(&p, 0, sizeof p);
        p.mod = mi;
        if (parsePatch(e, path, (const char *)data, len, &p, err, sizeof err)) {
            plogLine("tables", "%s rejected: %s\n", path, err);
            freeOps(p.ops, p.n);
            free(data);
            continue;
        }
        free(data);
        np = (Patch *)realloc(e->patches, (size_t)(e->npatches + 1) * sizeof(Patch));
        if (!np) {
            freeOps(p.ops, p.n);
            continue;
        }
        e->patches = np;
        e->patches[e->npatches++] = p;
    }
}

/* a table that cannot be registered says so: its mods would otherwise be ignored without a trace */
static int regFail(const TmTable *t, const char *why)
{
    plogLine("tables", "table \"%s\" not registered: %s\n", t && t->name ? t->name : "?", why);
    return -1;
}

static int registerEntry(const TmTable *t, const void *orig, int fileBacked)
{
    Entry *e;
    int i;
    size_t bytes;
    long long *v;

    if (!t || !t->name || t->stride == 0 || t->rows <= 0 || t->nfields <= 0) return regFail(t, "an empty or incomplete descriptor");
    if (findEntry(t->name)) return regFail(t, "a table with that name is already registered");
    if (t->keyFromRow && t->keyOff + typeSize(t->keyType) > t->stride) return regFail(t, "the key does not fit the row");
    for (i = 0; i < t->nfields; i++) {
        long long lo, hi;
        typeRange(t->fields[i].type, &lo, &hi);
        if (t->fields[i].mask) {
            /* a bit field: an unsigned type, contiguous bits inside it, a range that fits the bits */
            unsigned long long m = t->fields[i].mask, w = m >> maskShift(m);
            if (t->fields[i].type == TM_S32 || t->fields[i].type == TM_S16 || t->fields[i].type == TM_S8 || (w & (w + 1)) != 0 ||
                m > (unsigned long long)hi || t->fields[i].listMax != 0 || t->fields[i].min < 0 || t->fields[i].max > (long long)w) {
                return regFail(t, "a bit field's mask or range does not fit its type");
            }
            lo = 0;
            hi = (long long)w;
        }
        if (t->fields[i].off + typeSize(t->fields[i].type) > t->stride || t->fields[i].min < lo || t->fields[i].max > hi ||
            t->fields[i].min > t->fields[i].max) {
            return regFail(t, "a field does not fit the row or its range does not fit its type");
        }
    }
    for (i = 0; i < t->nfields; i++) {
        const TmField *fd = &t->fields[i];
        int r;
        if (fd->listMax == 0) continue;
        /* a list: in a table of memory, ended by 0 (min >= 1), the count and the pointer in the row, and
           every original list within listMax */
        if (fd->listMax > TM_LIST_CAP) {
            plogLine("tables", "table \"%s\" not registered: the list \"%s\" declares %d items, the most the engine takes is %d\n",
                    t->name, fd->name, fd->listMax, TM_LIST_CAP);
            return -1;
        }
        if (fileBacked || !t->base || fd->listMax < 0 || fd->min < 1 ||
            (fd->countOff != TM_NO_COUNT && fd->countOff + 4 > t->stride) || fd->off + sizeof(void *) > t->stride) {
            return regFail(t, "a list field is not valid (file-backed table, no base, min below 1 or the count or pointer outside the row)");
        }
        v = (long long *)malloc(((size_t)fd->listMax + 1) * sizeof *v);
        if (!v) return regFail(t, "out of memory");
        for (r = 0; r < t->rows; r++) {
            const unsigned char *row = (const unsigned char *)orig + (size_t)r * t->stride;
            int32_t count;
            if (fd->countOff == TM_NO_COUNT) {
                const unsigned char *items;
                memcpy(&items, row + fd->off, sizeof items);
                if (!items) {
                    free(v);
                    return regFail(t, "a list row has no items pointer");
                }
                count = readList(row, fd, v);
                if (count == 0 && fieldGet(items, fd->type) != 0) { /* longer than listMax */
                    free(v);
                    return regFail(t, "an original list is longer than its listMax");
                }
                continue;
            }
            memcpy(&count, row + fd->countOff, 4);
            if (count < 0 || count > fd->listMax || readList(row, fd, v) != count) {
                free(v);
                return regFail(t, "an original list is longer than its listMax");
            }
        }
        free(v);
    }
    if (gCount >= gEntriesCap) {
        int ncap = gEntriesCap ? gEntriesCap * 2 : 64;
        Entry **g = (Entry **)realloc(gEntries, (size_t)ncap * sizeof *g);
        if (!g) return regFail(t, "out of memory growing the registry");
        gEntries = g;
        gEntriesCap = ncap;
    }
    e = (Entry *)calloc(1, sizeof *e);
    if (!e) return regFail(t, "out of memory");
    e->t = *t;
    bytes = (size_t)t->rows * t->stride;
    e->orig = (unsigned char *)malloc(bytes);
    if (!e->orig) {
        free(e);
        return regFail(t, "out of memory");
    }
    memcpy(e->orig, orig, bytes);
    (void)fileBacked;
    gEntries[gCount++] = e;
    if (gDataDir[0]) {
        TdManifest man;
        char mpath[1200];
        int dirty = 0;
        uint8_t *mt;
        size_t ml;

        memset(&man, 0, sizeof man);
        snprintf(man.stamp, sizeof man.stamp, "tables-v1");
        snprintf(mpath, sizeof mpath, "%s/tables/manifest.json", gDataDir);
        if (tdReadFile(mpath, &mt, &ml) == 0) {
            TdManifest old;
            char err[120];
            memset(&old, 0, sizeof old);
            if (tdManifestParse((const char *)mt, ml, &old, err, sizeof err) == 0) {
                tdManifestFree(&man);
                man = old;
            }
            free(mt);
        }
        exportReference(e, &man, &dirty);
        if (dirty) {
            char *text = tdManifestFormat(&man);
            if (!text || tdWriteFileAtomic(mpath, text, strlen(text))) plogLine("tables", "cannot write %s\n", mpath);
            free(text);
        }
        tdManifestFree(&man);
    }
    loadPatches(e);
    return 0;
}

int tmRegister(const TmTable *t)
{
    TmTable c;
    unsigned char *stage;
    int i, rc;

    if (!t) return -1;
    if (!t->rowAt) {
        if (!t->base) return -1;
        return registerEntry(t, t->base, 0);
    }
    /* rows scattered in memory: work on a contiguous copy and write each row back through rowAt (tmApply) */
    if (t->rows <= 0 || t->stride == 0 || t->keyFromRow || t->keyBase != 0 || !t->keys) return -1;
    for (i = 0; i < t->nfields; i++) {
        if (t->fields[i].listMax != 0) return -1;
    }
    stage = (unsigned char *)malloc((size_t)t->rows * t->stride);
    if (!stage) return -1;
    for (i = 0; i < t->rows; i++) {
        const void *row = t->rowAt(i);
        if (!row) {
            free(stage);
            return -1;
        }
        memcpy(stage + (size_t)i * t->stride, row, t->stride);
    }
    c = *t;
    c.base = stage;
    rc = registerEntry(&c, stage, 0);
    if (rc) {
        free(stage);
        return rc;
    }
    gEntries[gCount - 1]->stage = stage;
    return 0;
}

int tmRegisterFile(const TmTable *t, const void *orig, size_t len)
{
    TmTable c;
    int n = 0;

    if (!t || !orig || t->stride == 0 || t->rowAt || t->keys) return -1;
    if (!t->keyFromRow && t->rows <= 0) return -1;
    c = *t;
    c.base = NULL;
    if (t->keyFromRow) {
        while ((size_t)(n + 1) * t->stride <= len && keyOfRow(&c, (const unsigned char *)orig, n) != 0) n++;
    } else {
        n = (size_t)t->rows * t->stride <= len ? t->rows : (int)(len / t->stride); /* positional: a fixed count */
    }
    c.rows = n;
    return registerEntry(&c, orig, 1);
}

int tmPatchCount(const char *name)
{
    Entry *e = findEntry(name);
    return e ? e->npatches : 0;
}

/* One list operation on the list field of one row: the new list is built in the engine's own buffer
   for that row and field and the row is pointed at it (the original array is never written). A result
   that would be empty or longer than listMax is rejected whole, with a log line, and the row keeps what
   it held. */
static void applyListBuf(Entry *e, unsigned char *rowp, int row, int field, const Op *op, long long *cur);

static void applyList(Entry *e, unsigned char *rowp, int row, int field, const Op *op)
{
    const TmField *fd = &e->t.fields[field];
    /* room for the current list plus every id an add could bring (each at most listMax) */
    long long *cur = (long long *)malloc(((size_t)fd->listMax * 2 + 2) * sizeof *cur);

    if (!cur) {
        plogLine("tables", "%s row %ld field \"%s\" not patched: out of memory\n", e->t.name, keyOfRow(&e->t, e->orig, row), fd->name);
        return;
    }
    applyListBuf(e, rowp, row, field, op, cur);
    free(cur);
}

static void applyListBuf(Entry *e, unsigned char *rowp, int row, int field, const Op *op, long long *cur)
{
    const TmField *fd = &e->t.fields[field];
    int n = readList(rowp, fd, cur), i, k;
    size_t sz = typeSize(fd->type);
    const char *what = op->kind == OP_LIST_SET ? "replace" : op->kind == OP_LIST_ADD ? "add" : "remove";
    void **slot = &e->lists[(size_t)row * (size_t)e->t.nfields + (size_t)field];
    unsigned char *buf;

    if (op->kind == OP_LIST_SET) {
        n = 0;
        for (i = 0; i < op->nlist; i++) cur[n++] = op->list[i];
    } else if (op->kind == OP_LIST_REMOVE) {
        int m = 0;
        for (i = 0; i < n; i++) {
            int drop = 0;
            for (k = 0; k < op->nlist; k++) drop |= cur[i] == op->list[k];
            if (!drop) cur[m++] = cur[i];
        }
        n = m;
    } else {
        int m = n;
        for (i = 0; i < op->nlist; i++) {
            int have = 0;
            for (k = 0; k < m; k++) have |= cur[k] == op->list[i];
            if (!have) cur[m++] = op->list[i];
        }
        if (m > fd->listMax) {
            plogLine("tables", "%s row %ld field \"%s\" %s rejected: more than %d items (%d)\n", e->t.name,
                    keyOfRow(&e->t, e->orig, row), fd->name, what, fd->listMax, m);
            return;
        }
        n = m;
    }
    if (n < 1) {
        plogLine("tables", "%s row %ld field \"%s\" %s rejected: no items left\n", e->t.name, keyOfRow(&e->t, e->orig, row),
                fd->name, what);
        return;
    }
    if (!*slot) {
        *slot = calloc((size_t)fd->listMax + 1, sz); /* zeroed: a screen that reads past the terminator (a partial page) sees empty slots */
        if (!*slot) return;
    }
    buf = (unsigned char *)*slot;
    for (i = 0; i < n; i++) fieldSet(buf + (size_t)i * sz, fd->type, cur[i]);
    fieldSet(buf + (size_t)n * sz, fd->type, 0); /* the terminator */
    {
        int32_t count = n;
        if (fd->countOff != TM_NO_COUNT) memcpy(rowp + fd->countOff, &count, 4);
        memcpy(rowp + fd->off, &buf, sizeof buf);
    }
}

/* One operation on a record outside the table (TmTable.extRow): its field is remembered the first time it changes, so
   tmApply can put it back (extRestore) */
static void applyExt(Entry *e, const Op *op)
{
    const TmField *fd = &e->t.fields[op->field];
    unsigned char *row = e->t.extRow ? (unsigned char *)e->t.extRow(op->ext) : NULL;
    double cur;
    int k;

    if (row == NULL || fd->listMax > 0) return;
    for (k = 0; k < e->nExtOrig; k++) {
        if (e->extOrig[k].id == op->ext && e->extOrig[k].field == op->field) break;
    }
    if (k == e->nExtOrig) {
        ExtOrig *g = (ExtOrig *)realloc(e->extOrig, (size_t)(e->nExtOrig + 1) * sizeof(ExtOrig));
        if (!g) return;
        e->extOrig = g;
        e->extOrig[k].id = op->ext;
        e->extOrig[k].field = op->field;
        e->extOrig[k].value = fdGet(fd, row);
        e->nExtOrig++;
    }
    cur = (double)fdGet(fd, row);
    switch (op->kind) {
    case OP_SET: cur = op->val; break;
    case OP_MUL: cur *= op->val; break;
    default: cur += op->val; break;
    }
    fdSet(fd, row, roundClamp(cur, fd->min, fd->max));
}

static void extRestore(Entry *e)
{
    int k;
    for (k = 0; k < e->nExtOrig; k++) {
        const TmField *fd = &e->t.fields[e->extOrig[k].field];
        unsigned char *row = e->t.extRow ? (unsigned char *)e->t.extRow(e->extOrig[k].id) : NULL;
        if (row != NULL) fdSet(fd, row, e->extOrig[k].value);
    }
}

/* Applies one patch to `base` (`rows` rows of this table's layout). In a file the rows are found by key,
   since a raw override of the file may hold them in another order. */
static void applyPatch(Entry *e, const Patch *p, unsigned char *base, int rows)
{
    const TmTable *t = &e->t;
    unsigned char *named = (unsigned char *)calloc((size_t)rows * (size_t)t->nfields + 1, 1);
    int *at = (int *)malloc(((size_t)p->n + 1) * sizeof(int));
    int pass, i;

    if (!named || !at) {
        free(named);
        free(at);
        return;
    }
    for (i = 0; i < p->n; i++) {
        int row = p->ops[i].row;
        if (row >= 0 && t->base == NULL) row = findKey(t, base, rows, keyOfRow(t, e->orig, row));
        at[i] = p->ops[i].row == OP_ROW_EXT ? OP_ROW_EXT : p->ops[i].row < 0 ? -1 : (row >= 0 ? row : -2); /* -2: the row is not in this buffer */
        if (at[i] >= 0) named[(size_t)at[i] * (size_t)t->nfields + (size_t)p->ops[i].field] = 1;
    }
    /* the "*" operations first, on every field the row does not name; then each row's own */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < p->n; i++) {
            const Op *op = &p->ops[i];
            const TmField *fd = &t->fields[op->field];
            int r, r0, r1;
            if (at[i] == -2 || at[i] == OP_ROW_EXT || (pass == 0) != (at[i] < 0)) continue;
            r0 = at[i] < 0 ? 0 : at[i];
            r1 = at[i] < 0 ? rows : at[i] + 1;
            for (r = r0; r < r1; r++) {
                double cur;
                if (at[i] < 0 && named[(size_t)r * (size_t)t->nfields + (size_t)op->field]) continue;
                if (fd->listMax > 0) {
                    applyList(e, base + (size_t)r * t->stride, r, op->field, op);
                    continue;
                }
                cur = (double)fdGet(fd, base + (size_t)r * t->stride);
                switch (op->kind) {
                case OP_SET: cur = op->val; break;
                case OP_MUL: cur *= op->val; break;
                default: cur += op->val; break;
                }
                {
                    long long nv = roundClamp(cur, fd->min, fd->max);
                    if (fd->idOk && op->kind != OP_SET && !fd->idOk(nv)) continue; /* an id nothing defines: unchanged */
                    fdSet(fd, base + (size_t)r * t->stride, nv);
                }
            }
        }
    }
    for (i = 0; i < p->n; i++) {
        if (at[i] == OP_ROW_EXT) applyExt(e, &p->ops[i]);
    }
    free(named);
    free(at);
}

static void logPatched(Entry *e, int changed)
{
    char who[400];
    if (e->logged) return;
    e->logged = 1;
    tmWho(e->t.name, who, sizeof who);
    plogLine("tables", "%s patched by %s (%d rows)\n", e->t.name, who, changed);
}

int tmApply(const char *name)
{
    Entry *e = findEntry(name);
    int i, changed = 0;

    if (!e || e->t.base == NULL) return -1;
    if (e->npatches == 0) return 0;
    if (!e->lists) e->lists = (void **)calloc((size_t)e->t.rows * (size_t)e->t.nfields + 1, sizeof(void *));
    if (!e->lists) return -1;
    memcpy(e->t.base, e->orig, (size_t)e->t.rows * e->t.stride);
    extRestore(e);
    for (i = 0; i < e->npatches; i++) applyPatch(e, &e->patches[i], (unsigned char *)e->t.base, e->t.rows);
    if (e->t.rowCheck) {
        for (i = 0; i < e->t.rows; i++) {
            unsigned char *row = (unsigned char *)e->t.base + (size_t)i * e->t.stride;
            const char *why = e->t.rowCheck(row);
            if (why) {
                plogLine("tables", "%s row %ld rejected: %s\n", e->t.name, keyOfRow(&e->t, e->orig, i), why);
                memcpy(row, e->orig + (size_t)i * e->t.stride, e->t.stride); /* the row keeps its original values */
            }
        }
    }
    for (i = 0; i < e->t.rows; i++) {
        unsigned char *row = (unsigned char *)e->t.base + (size_t)i * e->t.stride;
        if (memcmp(row, e->orig + (size_t)i * e->t.stride, e->t.stride)) changed++;
        if (e->stage) {
            void *dst = e->t.rowAt(i);
            if (dst) memcpy(dst, row, e->t.stride);
        }
    }
    logPatched(e, changed);
    return changed;
}

int tmPatchBuffer(const char *name, void *buf, size_t len)
{
    Entry *e = findEntry(name);
    unsigned char *b = (unsigned char *)buf, *before;
    int i, rows = 0, changed = 0;

    if (!e || e->t.base != NULL || !buf) return -1;
    if (e->npatches == 0) return 0;
    if (e->t.keyFromRow) {
        while ((size_t)(rows + 1) * e->t.stride <= len && keyOfRow(&e->t, b, rows) != 0) rows++;
    } else {
        rows = (size_t)e->t.rows * e->t.stride <= len ? e->t.rows : (int)(len / e->t.stride);
    }
    before = (unsigned char *)malloc((size_t)rows * e->t.stride + 1);
    if (!before) return -1;
    memcpy(before, b, (size_t)rows * e->t.stride);
    for (i = 0; i < e->npatches; i++) applyPatch(e, &e->patches[i], b, rows);
    if (e->t.rowCheck) {
        for (i = 0; i < rows; i++) {
            unsigned char *row = b + (size_t)i * e->t.stride;
            const char *why = e->t.rowCheck(row);
            if (why && !(e->t.rowCheck(before + (size_t)i * e->t.stride) != NULL)) {
                /* a row the game cannot handle goes back as the buffer held it; a row the buffer already held in that state
                   (a raw override of the file) is not ours to judge */
                plogLine("tables", "%s row %ld rejected: %s\n", e->t.name, (long)(e->t.keyFromRow ? keyOfRow(&e->t, b, i) : e->t.keyBase + i), why);
                memcpy(row, before + (size_t)i * e->t.stride, e->t.stride);
            }
        }
    }
    for (i = 0; i < rows; i++) {
        if (memcmp(b + (size_t)i * e->t.stride, before + (size_t)i * e->t.stride, e->t.stride)) changed++;
    }
    free(before);
    logPatched(e, changed);
    return changed;
}

const void *tmOriginalRow(const char *name, long key)
{
    Entry *e = findEntry(name);
    int r;

    if (!e) return NULL;
    r = findKey(&e->t, e->orig, e->t.rows, key);
    return r < 0 ? NULL : e->orig + (size_t)r * e->t.stride;
}

int tmWho(const char *name, char *out, size_t cap)
{
    Entry *e = findEntry(name);
    size_t n = 0;
    int i;

    if (cap) out[0] = 0;
    if (!e) return 0;
    for (i = 0; i < e->npatches && n + 1 < cap; i++) {
        size_t w = (size_t)snprintf(out + n, cap - n, "%smods/%s", i ? ", " : "", modsNameOf(e->patches[i].mod));
        n = w >= cap - n ? cap - 1 : n + w; /* a long list is cut, never overrun */
    }
    return e->npatches;
}
