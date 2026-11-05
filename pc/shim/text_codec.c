#include "text_codec.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Text codec: see text_codec.h. Mirrors tools/es419/lang.py step for step. */

/* ---- small helpers ---- */

typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
    int fail;
} Buf;

static void bufPut(Buf *b, const void *src, size_t n)
{
    if (b->fail) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        uint8_t *q;
        while (b->n + n + 1 > cap) cap *= 2;
        q = (uint8_t *)realloc(b->p, cap);
        if (!q) {
            b->fail = 1;
            return;
        }
        b->p = q;
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->n, src, n);
    b->n += n;
    b->p[b->n] = 0; /* always NUL-terminated, so a Buf is also a C string */
}

static void bufByte(Buf *b, uint8_t c) { bufPut(b, &c, 1); }

static void setErr(char *err, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void setErr(char *err, size_t size, const char *fmt, ...)
{
    va_list ap;
    if (!err || !size) return;
    va_start(ap, fmt);
    vsnprintf(err, size, fmt, ap);
    va_end(ap);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint8_t *dup(const uint8_t *src, size_t n)
{
    uint8_t *p = (uint8_t *)malloc(n ? n : 1);
    if (p && n) memcpy(p, src, n);
    return p;
}

static void putUtf8(Buf *b, uint32_t cp)
{
    if (cp < 0x80) {
        bufByte(b, (uint8_t)cp);
    } else if (cp < 0x800) {
        bufByte(b, (uint8_t)(0xC0 | cp >> 6));
        bufByte(b, (uint8_t)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        bufByte(b, (uint8_t)(0xE0 | cp >> 12));
        bufByte(b, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        bufByte(b, (uint8_t)(0x80 | (cp & 0x3F)));
    } else {
        bufByte(b, (uint8_t)(0xF0 | cp >> 18));
        bufByte(b, (uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
        bufByte(b, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        bufByte(b, (uint8_t)(0x80 | (cp & 0x3F)));
    }
}

/* Reads one UTF-8 code point; returns its length or 0 when malformed */
static int getUtf8(const uint8_t *s, uint32_t *cp)
{
    int n, i;
    uint32_t c = s[0], min;
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2 && c < 0xE0) { n = 2; c &= 0x1F; min = 0x80; }
    else if (c >= 0xE0 && c < 0xF0) { n = 3; c &= 0x0F; min = 0x800; }
    else if (c >= 0xF0 && c < 0xF5) { n = 4; c &= 0x07; min = 0x10000; }
    else return 0;
    for (i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        c = c << 6 | (s[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) return 0;
    *cp = c;
    return n;
}

/* ---- charmap ---- */

/* 0x01 xx -> code point (the EXT table of lang.py) */
static const struct { uint8_t b; uint16_t cp; } kExt[] = {
    {0x01, 0x0020}, {0x04, 0x002C}, {0x05, 0x002E}, {0x07, 0x003A}, {0x08, 0x003B}, {0x0B, 0x005E},
    {0x0C, 0x005F}, {0x0F, 0x002F}, {0x12, 0x22EF}, {0x13, 0x0027}, {0x14, 0x0022}, {0x15, 0x0028},
    {0x16, 0x0029}, {0x19, 0x002B}, {0x1A, 0x002D}, {0x1C, 0x003D}, {0x1D, 0x003C}, {0x1E, 0x003E},
    {0x2A, 0x0025}, {0x2B, 0x0026}, {0x3B, 0x00A1}, {0x3D, 0x00A7}, {0x3E, 0x00B5}, {0x3F, 0x00BF},
    {0x40, 0x00C3}, {0x41, 0x00C4}, {0x43, 0x00C6}, {0x44, 0x00C7}, {0x45, 0x00D1}, {0x46, 0x00D5},
    {0x47, 0x00D6}, {0x48, 0x00D8}, {0x49, 0x00DC}, {0x4A, 0x03B2}, {0x4B, 0x00E0}, {0x4C, 0x00E2},
    {0x4D, 0x00E3}, {0x4E, 0x00E4}, {0x4F, 0x00E7}, {0x50, 0x00E8}, {0x51, 0x00E9}, {0x52, 0x00EA},
    {0x53, 0x00EB}, {0x54, 0x00EC}, {0x55, 0x00EE}, {0x56, 0x00EF}, {0x57, 0x00F1}, {0x58, 0x00F2},
    {0x59, 0x00F4}, {0x5A, 0x00F5}, {0x5B, 0x00F6}, {0x5C, 0x00F9}, {0x5D, 0x00FB}, {0x5E, 0x00FC},
    {0x5F, 0x00E1}, {0x60, 0x00ED}, {0x61, 0x00F3}, {0x62, 0x00FA}, {0x63, 0x00C2}, {0x64, 0x00CA},
    {0x65, 0x00D4}, {0x66, 0x00CB}, {0x67, 0x00CF}, {0x68, 0x00C1}, {0x69, 0x00C9}, {0x6A, 0x00CD},
    {0x6B, 0x00D3}, {0x6C, 0x00DA}, {0x6D, 0x00C0}, {0x6E, 0x00C8}, {0x6F, 0x00CC}, {0x70, 0x00D2},
    {0x71, 0x00D9}, {0x72, 0x00BA},
};
#define EXT_COUNT ((int)(sizeof(kExt) / sizeof(kExt[0])))

/* One-byte glyphs: digits, letters and ? ! ~; 0 when the byte is not one */
static uint32_t singleChar(uint8_t b)
{
    if (b >= 0x04 && b <= 0x0D) return (uint32_t)('0' + (b - 0x04));
    if (b >= 0x0E && b <= 0x27) return (uint32_t)('A' + (b - 0x0E));
    if (b >= 0x28 && b <= 0x41) return (uint32_t)('a' + (b - 0x28));
    if (b == 0xE6) return '?';
    if (b == 0xE7) return '!';
    if (b == 0xE9) return '~';
    return 0;
}

static int singleByte(uint32_t cp)
{
    if (cp >= '0' && cp <= '9') return 0x04 + (int)(cp - '0');
    if (cp >= 'A' && cp <= 'Z') return 0x0E + (int)(cp - 'A');
    if (cp >= 'a' && cp <= 'z') return 0x28 + (int)(cp - 'a');
    if (cp == '?') return 0xE6;
    if (cp == '!') return 0xE7;
    if (cp == '~') return 0xE9;
    return -1;
}

static int extIndexByByte(uint8_t b)
{
    int i;
    for (i = 0; i < EXT_COUNT; i++)
        if (kExt[i].b == b) return i;
    return -1;
}

char *tcDecode(const uint8_t *data, size_t len)
{
    Buf out = {0};
    size_t i = 0;
    bufPut(&out, "", 0); /* so an empty text is still a string */
    while (i < len) {
        uint8_t b = data[i];
        int have = i + 1 < len;
        uint8_t nxt = have ? data[i + 1] : 0;
        int e;
        char tok[8];
        if (b == 0x01 && have && (e = extIndexByByte(nxt)) >= 0) {
            putUtf8(&out, kExt[e].cp);
            i += 2;
        } else if (b == 0x02 && have && nxt == 0x01) {
            bufByte(&out, '\n');
            i += 2;
        } else if ((b == 0x01 || b == 0x02) && have) {
            snprintf(tok, sizeof tok, "{%02x%02x}", b, nxt);
            bufPut(&out, tok, 6);
            i += 2;
        } else if (singleChar(b)) {
            bufByte(&out, (uint8_t)singleChar(b));
            i += 1;
        } else {
            snprintf(tok, sizeof tok, "{%02x}", b);
            bufPut(&out, tok, 4);
            i += 1;
        }
    }
    if (out.fail) {
        free(out.p);
        return NULL;
    }
    return (char *)out.p;
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* `{xx}` or `{xxxx}` at s (lowercase hex); returns the token length (0 = none) and the bytes */
static int matchToken(const char *s, uint8_t *bytes, int *nbytes)
{
    int h[4], i;
    if (s[0] != '{') return 0;
    for (i = 0; i < 4; i++) {
        h[i] = hexv((unsigned char)s[1 + i]);
        if (h[i] < 0) break;
    }
    if (i == 4 && s[5] == '}') {
        bytes[0] = (uint8_t)(h[0] << 4 | h[1]);
        bytes[1] = (uint8_t)(h[2] << 4 | h[3]);
        *nbytes = 2;
        return 6;
    }
    if (h[0] >= 0 && h[1] >= 0 && s[3] == '}') {
        bytes[0] = (uint8_t)(h[0] << 4 | h[1]);
        *nbytes = 1;
        return 4;
    }
    return 0;
}

int tcEncode(const char *text, uint8_t **out, size_t *outLen, char *err, size_t errSize)
{
    Buf b = {0};
    size_t i = 0;
    bufPut(&b, "", 0);
    while (text[i]) {
        uint8_t tb[2];
        int nb, tl, sb, k;
        uint32_t cp;
        int n;
        tl = matchToken(text + i, tb, &nb);
        if (tl) {
            bufPut(&b, tb, (size_t)nb);
            i += (size_t)tl;
            continue;
        }
        n = getUtf8((const uint8_t *)text + i, &cp);
        if (!n) {
            setErr(err, errSize, "invalid UTF-8 at position %zu", i);
            free(b.p);
            return -1;
        }
        sb = singleByte(cp);
        if (sb >= 0) {
            bufByte(&b, (uint8_t)sb);
        } else if (cp == '\n') {
            bufByte(&b, 0x02);
            bufByte(&b, 0x01);
        } else {
            for (k = 0; k < EXT_COUNT && kExt[k].cp != cp; k++) {}
            if (k == EXT_COUNT) {
                setErr(err, errSize, "character U+%04X has no encoding (position %zu)", (unsigned)cp, i);
                free(b.p);
                return -1;
            }
            bufByte(&b, 0x01);
            bufByte(&b, kExt[k].b);
        }
        i += (size_t)n;
    }
    if (b.fail) {
        free(b.p);
        setErr(err, errSize, "out of memory");
        return -1;
    }
    *out = b.p;
    *outLen = b.n;
    return 0;
}

/* Replace every `from` in s with `to`; consumes s, returns a new malloc'ed string */
static char *replaceAll(char *s, const char *from, const char *to)
{
    Buf b = {0};
    size_t fl = strlen(from), tl = strlen(to);
    const char *p = s;
    bufPut(&b, "", 0);
    while (*p) {
        if (!strncmp(p, from, fl)) {
            bufPut(&b, to, tl);
            p += fl;
        } else {
            bufByte(&b, (uint8_t)*p++);
        }
    }
    free(s);
    if (b.fail) {
        free(b.p);
        return NULL;
    }
    return (char *)b.p;
}

static const char *const kGroups[][2] = {{"{0202}{0202}{03}", "{pagina}"}, {"{0202}{02}", "{fin}"}};
static const char *const kAlias[][2] = {{"{0207}", "{hablante}"}};

char *tcPrettify(const char *text)
{
    char *s = (char *)dup((const uint8_t *)text, strlen(text) + 1);
    size_t i;
    for (i = 0; s && i < sizeof kGroups / sizeof kGroups[0]; i++) s = replaceAll(s, kGroups[i][0], kGroups[i][1]);
    for (i = 0; s && i < sizeof kAlias / sizeof kAlias[0]; i++) s = replaceAll(s, kAlias[i][0], kAlias[i][1]);
    return s;
}

char *tcUglify(const char *text)
{
    char *s = (char *)dup((const uint8_t *)text, strlen(text) + 1);
    size_t i;
    for (i = 0; s && i < sizeof kAlias / sizeof kAlias[0]; i++) s = replaceAll(s, kAlias[i][1], kAlias[i][0]);
    for (i = 0; s && i < sizeof kGroups / sizeof kGroups[0]; i++) s = replaceAll(s, kGroups[i][1], kGroups[i][0]);
    return s;
}

/* ---- lang files ---- */

static int isLang(const uint8_t *d, size_t n)
{
    uint32_t count, i;
    if (n < 8) return 0;
    count = rd32(d);
    if (count == 0 || 4 + 4 * (uint64_t)count > n) return 0;
    if (rd32(d + 4) < 4 + 4 * (uint64_t)count) return 0;
    for (i = 0; i < count; i++)
        if (rd32(d + 4 + 4 * (size_t)i) > n) return 0;
    return 1;
}

static int isPack(const uint8_t *d, size_t n)
{
    uint32_t first, i, cnt, prev = 0;
    if (n < 8) return 0;
    first = rd32(d);
    if (first % 4 || first < 8 || first > n) return 0;
    cnt = first / 4;
    for (i = 0; i < cnt; i++) {
        uint32_t o = rd32(d + 4 * (size_t)i);
        if (o > n || o < prev) return 0;
        prev = o;
    }
    return isLang(d + first, n - first);
}

static int cmpU32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static void freeLang(TcLang *l)
{
    int i;
    for (i = 0; i < l->count; i++) {
        free(l->entries[i].body);
        free(l->entries[i].tail);
    }
    free(l->entries);
    free(l->gap);
    memset(l, 0, sizeof *l);
}

static int parseLang(const uint8_t *d, size_t n, TcLang *l)
{
    uint32_t count = rd32(d), i, minOff;
    uint32_t *sorted;
    memset(l, 0, sizeof *l);
    l->entries = (TcEntry *)calloc(count, sizeof(TcEntry));
    sorted = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!l->entries || !sorted) {
        free(sorted);
        free(l->entries);
        memset(l, 0, sizeof *l);
        return -1;
    }
    l->count = (int)count;
    for (i = 0; i < count; i++) sorted[i] = rd32(d + 4 + 4 * (size_t)i);
    qsort(sorted, count, sizeof(uint32_t), cmpU32);
    minOff = sorted[0];
    for (i = 0; i < count; i++) {
        uint32_t o = rd32(d + 4 + 4 * (size_t)i), limit = (uint32_t)n, lo = 0, hi = count, bodyLen, tailLen;
        const uint8_t *chunk, *nul;
        TcEntry *e = &l->entries[i];
        /* limit = the next larger offset (first element > o), or the end of the file */
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if (sorted[mid] > o) hi = mid; else lo = mid + 1;
        }
        if (lo < count) limit = sorted[lo];
        chunk = d + o;
        nul = limit > o ? (const uint8_t *)memchr(chunk, 0, limit - o) : NULL;
        bodyLen = nul ? (uint32_t)(nul - chunk) : limit - o;
        tailLen = limit - o - bodyLen;
        e->slot = o;
        e->body = dup(chunk, bodyLen);
        e->bodyLen = bodyLen;
        if (limit == n) {
            e->hasTail = !(tailLen == 1 && chunk[bodyLen] == 0);
        } else {
            uint32_t pad = (4 - (o + bodyLen + 1) % 4) % 4, k;
            int canon = tailLen == 1 + pad;
            for (k = 0; canon && k < tailLen; k++) canon = chunk[bodyLen + k] == 0;
            e->hasTail = !canon;
        }
        if (e->hasTail) {
            e->tail = dup(chunk + bodyLen, tailLen);
            e->tailLen = tailLen;
            if (!e->tail) goto oom;
        }
        if (!e->body) goto oom;
    }
    {
        size_t hdr = 4 + 4 * (size_t)count;
        if (minOff > hdr) {
            l->gapLen = minOff - (uint32_t)hdr;
            l->gap = dup(d + hdr, l->gapLen);
            if (!l->gap) goto oom;
        }
    }
    free(sorted);
    return 0;
oom:
    free(sorted);
    freeLang(l);
    return -1;
}

/* qsort has no context argument (qsort_r is glibc only): the entries go in a file-scope pointer */
static const TcEntry *gSortEntries;
static int cmpSlotIdx(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    if (gSortEntries[x].slot != gSortEntries[y].slot) return gSortEntries[x].slot < gSortEntries[y].slot ? -1 : 1;
    return x < y ? -1 : x > y;
}

static int buildLang(const TcLang *l, Buf *out)
{
    int count = l->count, i, *idx;
    uint32_t *owner, *placed;
    size_t base = out->n, tbl;
    uint8_t zero[4] = {0, 0, 0, 0};
    idx = (int *)malloc((size_t)count * sizeof(int));
    owner = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
    placed = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
    if (!idx || !owner || !placed) {
        free(idx); free(owner); free(placed);
        return -1;
    }
    /* owner[i] = first entry (in file order) that uses entry i's slot */
    for (i = 0; i < count; i++) idx[i] = i;
    gSortEntries = l->entries;
    qsort(idx, (size_t)count, sizeof(int), cmpSlotIdx);
    for (i = 0; i < count; i++)
        owner[idx[i]] = (i > 0 && l->entries[idx[i]].slot == l->entries[idx[i - 1]].slot) ? owner[idx[i - 1]] : (uint32_t)idx[i];
    tbl = 4 + 4 * (size_t)count;
    {
        uint8_t *hdr = (uint8_t *)calloc(tbl, 1);
        if (!hdr) { free(idx); free(owner); free(placed); return -1; }
        wr32(hdr, (uint32_t)count);
        bufPut(out, hdr, tbl);
        free(hdr);
    }
    bufPut(out, l->gap, l->gapLen);
    for (i = 0; i < count; i++) {
        const TcEntry *e = &l->entries[i];
        if (owner[i] == (uint32_t)i) {
            while ((out->n - base) % 4) bufPut(out, zero, 1);
            placed[i] = (uint32_t)(out->n - base);
            bufPut(out, e->body, e->bodyLen);
            if (e->hasTail) bufPut(out, e->tail, e->tailLen); else bufPut(out, zero, 1);
        } else {
            placed[i] = placed[owner[i]];
        }
        if (!out->fail) wr32(out->p + base + 4 + 4 * (size_t)i, placed[i]);
    }
    free(idx); free(owner); free(placed);
    return out->fail ? -1 : 0;
}

/* ---- documents ---- */

void tcDocFree(TcDoc *doc)
{
    int i;
    freeLang(&doc->lang);
    for (i = 0; i < doc->partCount; i++) {
        freeLang(&doc->parts[i].lang);
        free(doc->parts[i].raw);
    }
    free(doc->parts);
    memset(doc, 0, sizeof *doc);
}

int tcDocParse(const uint8_t *data, size_t len, TcDoc *doc, char *err, size_t errSize)
{
    memset(doc, 0, sizeof *doc);
    if (isLang(data, len)) {
        if (parseLang(data, len, &doc->lang)) goto oom;
        return 0;
    }
    if (isPack(data, len)) {
        uint32_t first = rd32(data), cnt = first / 4, i;
        doc->isPack = 1;
        doc->parts = (TcPart *)calloc(cnt, sizeof(TcPart));
        if (!doc->parts) goto oom;
        doc->partCount = (int)cnt;
        for (i = 0; i < cnt; i++) {
            uint32_t o = rd32(data + 4 * (size_t)i);
            uint32_t end = i + 1 < cnt ? rd32(data + 4 * (size_t)(i + 1)) : (uint32_t)len;
            TcPart *p = &doc->parts[i];
            if (isLang(data + o, end - o)) {
                p->isLang = 1;
                if (parseLang(data + o, end - o, &p->lang)) goto oom;
            } else {
                p->raw = dup(data + o, end - o);
                p->rawLen = end - o;
                if (!p->raw) goto oom;
            }
        }
        return 0;
    }
    setErr(err, errSize, "not a lang file or lang pack");
    return -1;
oom:
    tcDocFree(doc);
    setErr(err, errSize, "out of memory");
    return -1;
}

uint8_t *tcDocBuild(const TcDoc *doc, size_t *outLen)
{
    Buf out = {0};
    bufPut(&out, "", 0);
    if (!doc->isPack) {
        if (buildLang(&doc->lang, &out)) { free(out.p); return NULL; }
    } else {
        int i;
        uint8_t *tbl = (uint8_t *)calloc((size_t)doc->partCount, 4);
        if (!tbl) { free(out.p); return NULL; }
        bufPut(&out, tbl, 4 * (size_t)doc->partCount);
        free(tbl);
        for (i = 0; i < doc->partCount; i++) {
            const TcPart *p = &doc->parts[i];
            size_t at = out.n;
            if (p->isLang) {
                if (buildLang(&p->lang, &out)) { free(out.p); return NULL; }
            } else {
                bufPut(&out, p->raw, p->rawLen);
            }
            if (!out.fail) wr32(out.p + 4 * (size_t)i, (uint32_t)at);
        }
        if (out.fail) { free(out.p); return NULL; }
    }
    *outLen = out.n;
    return out.p;
}

TcLang *tcDocLang(TcDoc *doc, int part)
{
    if (!doc->isPack) return part == -1 ? &doc->lang : NULL;
    if (part < 0 || part >= doc->partCount || !doc->parts[part].isLang) return NULL;
    return &doc->parts[part].lang;
}

char *tcEntryText(const TcLang *lang, int index)
{
    if (index < 0 || index >= lang->count) return NULL;
    return tcDecode(lang->entries[index].body, lang->entries[index].bodyLen);
}

int tcSpeakerRef(const char *text, char *file, size_t cap, int *entry, const char **rest, int *bad)
{
    static const char tag[] = "{speaker-of:";
    const char *p = text + sizeof tag - 1, *colon, *end;
    size_t n;
    long v = 0;

    if (bad) *bad = 0;
    if (strncmp(text, tag, sizeof tag - 1)) return 0;
    if (bad) *bad = 1;
    colon = strchr(p, ':');
    end = strchr(p, '}');
    if (!colon || !end || colon > end) return 0;
    n = (size_t)(colon - p);
    if (n == 0 || n >= cap) return 0;
    memcpy(file, p, n);
    file[n] = 0;
    if (colon + 1 == end || end - colon > 7) return 0;
    for (p = colon + 1; p < end; p++) {
        if (*p < '0' || *p > '9') return 0;
        v = v * 10 + (*p - '0');
    }
    *entry = (int)v;
    *rest = end + 1;
    if (bad) *bad = 0;
    return 1;
}

char *tcSpeakerLabel(const TcLang *lang, int index)
{
    char *t = tcEntryText(lang, index), *a, *b, *out;

    if (!t) return NULL;
    a = strstr(t, "{0207}");
    b = a ? strstr(a + 6, "{0207}") : NULL;
    if (!b || a != t) {
        free(t);
        return NULL;
    }
    out = (char *)malloc((size_t)(b - a - 6) + 1);
    if (out) {
        memcpy(out, a + 6, (size_t)(b - a - 6));
        out[b - a - 6] = 0;
    }
    free(t);
    return out;
}

int tcSetText(TcLang *lang, int index, const char *text, char *err, size_t errSize)
{
    uint8_t *body;
    size_t len, i;
    uint32_t maxSlot = 0;
    TcEntry *e;
    if (index < 0 || index >= lang->count) {
        setErr(err, errSize, "index out of range (0..%d)", lang->count - 1);
        return -1;
    }
    if (tcEncode(text, &body, &len, err, errSize)) return -1;
    for (i = 0; i < (size_t)lang->count; i++)
        if (lang->entries[i].slot > maxSlot) maxSlot = lang->entries[i].slot;
    e = &lang->entries[index];
    free(e->body);
    free(e->tail);
    e->body = body;
    e->bodyLen = (uint32_t)len;
    e->tail = NULL;
    e->tailLen = 0;
    e->hasTail = 0;
    e->slot = maxSlot + 1;
    return 0;
}

/* ---- JSON writer ---- */

static void jsonStr(Buf *b, const char *s)
{
    bufByte(b, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char u[8];
        switch (c) {
        case '"': bufPut(b, "\\\"", 2); break;
        case '\\': bufPut(b, "\\\\", 2); break;
        case '\n': bufPut(b, "\\n", 2); break;
        case '\r': bufPut(b, "\\r", 2); break;
        case '\t': bufPut(b, "\\t", 2); break;
        default:
            if (c < 0x20) {
                snprintf(u, sizeof u, "\\u%04x", c);
                bufPut(b, u, 6);
            } else {
                bufByte(b, c);
            }
        }
    }
    bufByte(b, '"');
}

static int jsonLang(Buf *b, const TcLang *l, int part, int *first)
{
    int i;
    char num[32];
    for (i = 0; i < l->count; i++) {
        char *raw = tcEntryText(l, i), *pretty;
        if (!raw) return -1;
        pretty = tcPrettify(raw);
        free(raw);
        if (!pretty) return -1;
        bufPut(b, *first ? "\n  {\n   \"index\": " : ",\n  {\n   \"index\": ", *first ? 17 : 18);
        *first = 0;
        snprintf(num, sizeof num, "%d", i);
        bufPut(b, num, strlen(num));
        if (part >= 0) {
            snprintf(num, sizeof num, ",\n   \"part\": %d", part);
            bufPut(b, num, strlen(num));
        }
        bufPut(b, ",\n   \"text\": ", 12);
        jsonStr(b, pretty);
        bufPut(b, "\n  }", 4);
        free(pretty);
    }
    return 0;
}

char *tcDocToJson(const TcDoc *doc, const char *fileName)
{
    Buf b = {0};
    int first = 1, i, bad = 0;
    bufPut(&b, "{\n \"file\": ", 11);
    jsonStr(&b, fileName);
    bufPut(&b, ",\n \"format\": ", 13);
    jsonStr(&b, TC_FORMAT_FULL);
    bufPut(&b, ",\n \"strings\": [", 15);
    if (!doc->isPack) {
        bad = jsonLang(&b, &doc->lang, -1, &first);
    } else {
        for (i = 0; i < doc->partCount && !bad; i++)
            if (doc->parts[i].isLang) bad = jsonLang(&b, &doc->parts[i].lang, i, &first);
    }
    bufPut(&b, "\n ]\n}\n", 6);
    if (bad || b.fail) {
        free(b.p);
        return NULL;
    }
    return (char *)b.p;
}

/* ---- JSON reader ---- */

typedef struct Rd {
    const char *s;
    size_t n, i;
    char *err;
    size_t errSize;
} Rd;

static int rfail(Rd *r, const char *msg)
{
    setErr(r->err, r->errSize, "JSON: %s at byte %zu", msg, r->i);
    return -1;
}

static void ws(Rd *r)
{
    while (r->i < r->n && (r->s[r->i] == ' ' || r->s[r->i] == '\n' || r->s[r->i] == '\t' || r->s[r->i] == '\r')) r->i++;
}

static int expect(Rd *r, char c)
{
    ws(r);
    if (r->i >= r->n || r->s[r->i] != c) {
        char m[24];
        snprintf(m, sizeof m, "expected '%c'", c);
        return rfail(r, m);
    }
    r->i++;
    return 0;
}

static int hex4(Rd *r, uint32_t *v)
{
    int k;
    *v = 0;
    if (r->i + 4 > r->n) return rfail(r, "bad \\u escape");
    for (k = 0; k < 4; k++) {
        int c = (unsigned char)r->s[r->i + (size_t)k];
        int h = (c >= 'A' && c <= 'F') ? c - 'A' + 10 : hexv(c);
        if (h < 0) return rfail(r, "bad \\u escape");
        *v = *v << 4 | (uint32_t)h;
    }
    r->i += 4;
    return 0;
}

/* A JSON string into a malloc'ed UTF-8 C string */
static int rString(Rd *r, char **out)
{
    Buf b = {0};
    bufPut(&b, "", 0);
    if (expect(r, '"')) return -1;
    for (;;) {
        unsigned char c;
        if (r->i >= r->n) { free(b.p); return rfail(r, "unterminated string"); }
        c = (unsigned char)r->s[r->i++];
        if (c == '"') break;
        if (c < 0x20) { free(b.p); r->i--; return rfail(r, "control character in string"); }
        if (c != '\\') { bufByte(&b, c); continue; }
        if (r->i >= r->n) { free(b.p); return rfail(r, "unterminated string"); }
        c = (unsigned char)r->s[r->i++];
        switch (c) {
        case '"': case '\\': case '/': bufByte(&b, c); break;
        case 'b': bufByte(&b, '\b'); break;
        case 'f': bufByte(&b, '\f'); break;
        case 'n': bufByte(&b, '\n'); break;
        case 'r': bufByte(&b, '\r'); break;
        case 't': bufByte(&b, '\t'); break;
        case 'u': {
            uint32_t cp, lo;
            if (hex4(r, &cp)) { free(b.p); return -1; }
            if (cp >= 0xD800 && cp < 0xDC00) {
                if (r->i + 2 > r->n || r->s[r->i] != '\\' || r->s[r->i + 1] != 'u') { free(b.p); return rfail(r, "lone surrogate"); }
                r->i += 2;
                if (hex4(r, &lo)) { free(b.p); return -1; }
                if (lo < 0xDC00 || lo >= 0xE000) { free(b.p); return rfail(r, "lone surrogate"); }
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp < 0xE000) {
                free(b.p);
                return rfail(r, "lone surrogate");
            }
            if (cp == 0) { free(b.p); return rfail(r, "\\u0000 is not allowed"); }
            putUtf8(&b, cp);
            break;
        }
        default:
            free(b.p);
            r->i--;
            return rfail(r, "bad escape");
        }
    }
    if (b.fail) { free(b.p); return rfail(r, "out of memory"); }
    *out = (char *)b.p;
    return 0;
}

static int rInt(Rd *r, int *v)
{
    long x = 0;
    int neg = 0, digits = 0;
    ws(r);
    if (r->i < r->n && r->s[r->i] == '-') { neg = 1; r->i++; }
    while (r->i < r->n && r->s[r->i] >= '0' && r->s[r->i] <= '9') {
        x = x * 10 + (r->s[r->i++] - '0');
        digits++;
        if (x > 1000000000L) return rfail(r, "number too large");
    }
    if (!digits) return rfail(r, "expected an integer");
    if (r->i < r->n && (r->s[r->i] == '.' || r->s[r->i] == 'e' || r->s[r->i] == 'E')) return rfail(r, "expected an integer");
    *v = (int)(neg ? -x : x);
    return 0;
}

static int skipValue(Rd *r, int depth)
{
    ws(r);
    if (depth > 32) return rfail(r, "nesting too deep");
    if (r->i >= r->n) return rfail(r, "unexpected end");
    switch (r->s[r->i]) {
    case '"': {
        char *s;
        if (rString(r, &s)) return -1;
        free(s);
        return 0;
    }
    case '{':
    case '[': {
        char open = r->s[r->i++], close = open == '{' ? '}' : ']';
        ws(r);
        if (r->i < r->n && r->s[r->i] == close) { r->i++; return 0; }
        for (;;) {
            if (open == '{') {
                char *k;
                if (rString(r, &k)) return -1;
                free(k);
                if (expect(r, ':')) return -1;
            }
            if (skipValue(r, depth + 1)) return -1;
            ws(r);
            if (r->i < r->n && r->s[r->i] == ',') { r->i++; continue; }
            return expect(r, close);
        }
    }
    default: {
        size_t start = r->i;
        while (r->i < r->n && strchr("+-.0123456789eEtruefalsn", r->s[r->i])) r->i++;
        if (r->i == start) return rfail(r, "unexpected character");
        return 0;
    }
    }
}

void tcJsonFree(TcJson *j)
{
    int i;
    for (i = 0; i < j->count; i++) free(j->strings[i].text);
    free(j->strings);
    free(j->file);
    free(j->format);
    memset(j, 0, sizeof *j);
}

static int rStringItem(Rd *r, TcJsonString *s)
{
    int haveIndex = 0, haveText = 0;
    s->index = 0;
    s->part = -1;
    s->text = NULL;
    if (expect(r, '{')) return -1;
    ws(r);
    if (r->i < r->n && r->s[r->i] == '}') return rfail(r, "string entry without index and text");
    for (;;) {
        char *k;
        int ok;
        if (rString(r, &k)) goto bad;
        if (expect(r, ':')) { free(k); goto bad; }
        if (!strcmp(k, "index")) { ok = rInt(r, &s->index); haveIndex = 1; }
        else if (!strcmp(k, "part")) ok = rInt(r, &s->part);
        else if (!strcmp(k, "text")) {
            free(s->text);
            s->text = NULL;
            ok = rString(r, &s->text);
            haveText = ok == 0;
        } else ok = skipValue(r, 1);
        free(k);
        if (ok) goto bad;
        ws(r);
        if (r->i < r->n && r->s[r->i] == ',') { r->i++; continue; }
        if (expect(r, '}')) goto bad;
        break;
    }
    if (!haveIndex || !haveText) { rfail(r, "string entry needs index and text"); goto bad; }
    return 0;
bad:
    free(s->text);
    s->text = NULL;
    return -1;
}

int tcJsonParse(const char *json, size_t len, TcJson *out, char *err, size_t errSize)
{
    Rd r = {json, len, 0, err, errSize};
    int cap = 0, haveStrings = 0;
    memset(out, 0, sizeof *out);
    if (expect(&r, '{')) goto bad;
    ws(&r);
    if (r.i < r.n && r.s[r.i] == '}') { rfail(&r, "missing \"strings\""); goto bad; }
    for (;;) {
        char *k;
        int ok;
        if (rString(&r, &k)) goto bad;
        if (expect(&r, ':')) { free(k); goto bad; }
        if (!strcmp(k, "file")) { free(out->file); out->file = NULL; ok = rString(&r, &out->file); }
        else if (!strcmp(k, "format")) { free(out->format); out->format = NULL; ok = rString(&r, &out->format); }
        else if (!strcmp(k, "strings")) {
            ok = 0;
            haveStrings = 1;
            while (out->count) free(out->strings[--out->count].text);
            if (expect(&r, '[')) ok = -1;
            else {
                ws(&r);
                if (r.i < r.n && r.s[r.i] == ']') r.i++;
                else for (;;) {
                    if (out->count == cap) {
                        TcJsonString *q;
                        cap = cap ? cap * 2 : 64;
                        q = (TcJsonString *)realloc(out->strings, (size_t)cap * sizeof *q);
                        if (!q) { rfail(&r, "out of memory"); ok = -1; break; }
                        out->strings = q;
                    }
                    if (rStringItem(&r, &out->strings[out->count])) { ok = -1; break; }
                    out->count++;
                    ws(&r);
                    if (r.i < r.n && r.s[r.i] == ',') { r.i++; continue; }
                    if (expect(&r, ']')) ok = -1;
                    break;
                }
            }
        } else ok = skipValue(&r, 1);
        free(k);
        if (ok) goto bad;
        ws(&r);
        if (r.i < r.n && r.s[r.i] == ',') { r.i++; continue; }
        if (expect(&r, '}')) goto bad;
        break;
    }
    ws(&r);
    if (r.i != r.n) { rfail(&r, "trailing data"); goto bad; }
    if (!haveStrings) { setErr(err, errSize, "JSON: missing \"strings\""); goto bad; }
    return 0;
bad:
    tcJsonFree(out);
    return -1;
}

/* ---- merge ---- */

int tcApplyJson(TcDoc *doc, const TcJson *json, char *err, size_t errSize)
{
    int i;
    for (i = 0; i < json->count; i++) {
        const TcJsonString *s = &json->strings[i];
        TcLang *l = tcDocLang(doc, s->part);
        char *raw;
        char sub[200];
        int rc;
        if (!l) {
            setErr(err, errSize, "part=%d index=%d: part does not fit this file", s->part, s->index);
            return -1;
        }
        raw = tcUglify(s->text);
        if (!raw) {
            setErr(err, errSize, "out of memory");
            return -1;
        }
        rc = tcSetText(l, s->index, raw, sub, sizeof sub);
        free(raw);
        if (rc) {
            setErr(err, errSize, "part=%d index=%d: %s", s->part, s->index, sub);
            return -1;
        }
    }
    return 0;
}
