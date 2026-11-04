/*
 * The shared content core: see content.h.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "content.h"
#include "mods.h"
#include "port_log.h"
#include "text_data.h"

const char *const EXT_LANGS[EXT_NLANG] = {"eng", "fra", "ita", "ger", "spa", "es-419"};

/* ---- a small JSON reader, shared with tech_ext.c (item_ext.h) ---- */

typedef struct Rd {
    const char *p, *end;
    int bad;
} Rd;

void extJsonFree(ExtJson *j) {
    int i;
    for (i = 0; i < j->n; i++) {
        if (j->keys != NULL) free(j->keys[i]);
        extJsonFree(&j->v[i]);
    }
    free(j->keys);
    free(j->v);
    free(j->str);
    memset(j, 0, sizeof *j);
}

static void ws(Rd *r) {
    while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')) r->p++;
}

static char *rstr(Rd *r) {
    size_t cap = 16, n = 0;
    char *s;
    if (r->p >= r->end || *r->p != '"') { r->bad = 1; return NULL; }
    r->p++;
    s = malloc(cap);
    if (s == NULL) { r->bad = 1; return NULL; }
    while (r->p < r->end && *r->p != '"') {
        char c = *r->p++;
        if ((unsigned char)c < 0x20) { r->bad = 1; break; }
        if (c == '\\') {
            if (r->p >= r->end) { r->bad = 1; break; }
            c = *r->p++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'u') { /* \uXXXX: UTF-8 for the BMP */
                unsigned u = 0;
                int k;
                if (r->end - r->p < 4) { r->bad = 1; break; }
                for (k = 0; k < 4; k++) {
                    char h = r->p[k];
                    u = u * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
                }
                r->p += 4;
                if (n + 4 >= cap) { cap *= 2; s = realloc(s, cap); if (s == NULL) { r->bad = 1; return NULL; } }
                if (u < 0x80) s[n++] = (char)u;
                else if (u < 0x800) { s[n++] = (char)(0xC0 | (u >> 6)); s[n++] = (char)(0x80 | (u & 0x3F)); }
                else { s[n++] = (char)(0xE0 | (u >> 12)); s[n++] = (char)(0x80 | ((u >> 6) & 0x3F)); s[n++] = (char)(0x80 | (u & 0x3F)); }
                continue;
            } else if (c != '"' && c != '\\' && c != '/') { r->bad = 1; break; }
        }
        if (n + 2 >= cap) { cap *= 2; s = realloc(s, cap); if (s == NULL) { r->bad = 1; return NULL; } }
        s[n++] = c;
    }
    if (r->p >= r->end || r->bad) { r->bad = 1; free(s); return NULL; }
    r->p++;
    s[n] = '\0';
    return s;
}

static void rval(Rd *r, ExtJson *out, int depth) {
    memset(out, 0, sizeof *out);
    ws(r);
    if (r->bad || depth > 24 || r->p >= r->end) { r->bad = 1; return; }
    if (*r->p == '"') {
        out->t = 1;
        out->str = rstr(r);
    } else if (*r->p == '{') {
        out->t = 2;
        r->p++;
        ws(r);
        if (r->p < r->end && *r->p == '}') { r->p++; return; }
        for (;;) {
            char *k;
            char **nk;
            ExtJson *nv;
            ws(r);
            k = rstr(r);
            if (k == NULL) return;
            ws(r);
            if (r->p >= r->end || *r->p != ':') { free(k); r->bad = 1; return; }
            r->p++;
            nk = realloc(out->keys, sizeof(char *) * (size_t)(out->n + 1));
            if (nk == NULL) { free(k); r->bad = 1; return; }
            out->keys = nk;
            nv = realloc(out->v, sizeof(ExtJson) * (size_t)(out->n + 1));
            if (nv == NULL) { free(k); r->bad = 1; return; }
            out->v = nv;
            out->keys[out->n] = k;
            rval(r, &out->v[out->n], depth + 1);
            out->n++;
            if (r->bad) return;
            ws(r);
            if (r->p < r->end && *r->p == ',') { r->p++; continue; }
            if (r->p < r->end && *r->p == '}') { r->p++; return; }
            r->bad = 1;
            return;
        }
    } else if (*r->p == '[') { /* a list: t 4, the elements in v (keys is NULL) */
        out->t = 4;
        r->p++;
        ws(r);
        if (r->p < r->end && *r->p == ']') { r->p++; return; }
        for (;;) {
            ExtJson *nv = realloc(out->v, sizeof(ExtJson) * (size_t)(out->n + 1));
            if (nv == NULL) { r->bad = 1; return; }
            out->v = nv;
            rval(r, &out->v[out->n], depth + 1);
            out->n++;
            if (r->bad) return;
            ws(r);
            if (r->p < r->end && *r->p == ',') { r->p++; continue; }
            if (r->p < r->end && *r->p == ']') { r->p++; return; }
            r->bad = 1;
            return;
        }
    } else if ((*r->p >= '0' && *r->p <= '9') || *r->p == '-') {
        char buf[48], *e;
        size_t n = 0;
        while (r->p < r->end && n < sizeof buf - 1 && strchr("0123456789+-.eE", *r->p)) buf[n++] = *r->p++;
        buf[n] = '\0';
        out->num = strtod(buf, &e);
        if (*e) r->bad = 1;
    } else {
        out->t = 3;
        if (r->end - r->p >= 4 && !strncmp(r->p, "true", 4)) { out->num = 1; r->p += 4; }
        else if (r->end - r->p >= 5 && !strncmp(r->p, "false", 5)) r->p += 5;
        else if (r->end - r->p >= 4 && !strncmp(r->p, "null", 4)) r->p += 4;
        else r->bad = 1;
    }
}

const ExtJson *extJsonGet(const ExtJson *o, const char *key) {
    int i;
    if (o->t != 2) return NULL;
    for (i = 0; i < o->n; i++) if (!strcmp(o->keys[i], key)) return &o->v[i];
    return NULL;
}

/* ---- loading ---- */

int extWhole(double d, long lo, long hi) {
    return d == (double)(long)d && d >= (double)lo && d <= (double)hi;
}

int extJsonParse(const uint8_t *data, size_t len, ExtJson *out) {
    Rd r;

    r.p = (const char *)data;
    r.end = r.p + len;
    r.bad = 0;
    rval(&r, out, 0);
    ws(&r);
    if (r.bad || r.p != r.end || out->t != 2) {
        extJsonFree(out);
        return -1;
    }
    return 0;
}

/* the language texts of a {"eng": "...", ...} object into text[language]; `kind` is "item" or "tech" (the log) */
void extTakeTexts(char *text[EXT_NLANG], const ExtJson *o, const char *kind, const char *path, const char *name, const char *what) {
    int i, l;
    char channel[16];

    snprintf(channel, sizeof channel, "%ss", kind);
    if (o->t != 2) {
        plogLine(channel, "%s %s \"%s\" %s rejected: expected an object of languages\n", path, kind, name, what);
        return;
    }
    for (i = 0; i < o->n; i++) {
        for (l = 0; l < EXT_NLANG; l++) if (!strcmp(o->keys[i], EXT_LANGS[l])) break;
        if (l == EXT_NLANG || o->v[i].t != 1 || o->v[i].str == NULL || o->v[i].str[0] == '\0') {
            plogLine(channel, "%s %s \"%s\" %s \"%s\" rejected: use eng, fra, ita, ger, spa or es-419 with a non-empty string\n",
                     path, kind, name, what, o->keys[i]);
            continue;
        }
        free(text[l]);
        text[l] = strdup(o->v[i].str);
    }
}

/* the text for a language folder name: es-419 -> spa -> eng; NULL when even English is missing */
const char *extPickText(char *const text[EXT_NLANG], const char *langDir) {
    int l = -1, i;

    for (i = 0; langDir != NULL && i < EXT_NLANG; i++) if (!strcmp(EXT_LANGS[i], langDir)) l = i;
    if (l == 5 && text[5] == NULL) l = 4;
    if (l >= 0 && text[l] != NULL) return text[l];
    return text[0];
}

int extValidKey(const char *k) {
    size_t i, n = strlen(k);
    if (n == 0 || n > 48) return 0;
    for (i = 0; i < n; i++) {
        char c = k[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
    }
    return 1;
}
/* ---- the entries ---- */

static void freeEntry(const ContentType *t, ContentEntry *e) {
    int d, l;
    for (d = 0; d < 2; d++)
        for (l = 0; l < EXT_NLANG; l++) free(e->text[d][l]);
    if (e->rec != NULL && t->freeRec != NULL) t->freeRec(e->rec);
    free(e->rec);
    free(e->path);
    memset(e, 0, sizeof *e);
}

static int cmpEntry(const void *a, const void *b) {
    return strcmp(((const ContentEntry *)a)->name, ((const ContentEntry *)b)->name);
}

/* "dw3:<kind>/<id>" -> id in the type's original range, else -1 */
int contentOriginalId(const ContentType *t, const char *name) {
    size_t k = strlen(t->kind);
    char *end;
    long id;

    if (name == NULL || strncmp(name, "dw3:", 4) != 0 || strncmp(name + 4, t->kind, k) != 0 || name[4 + k] != '/') return -1;
    id = strtol(name + 5 + k, &end, 10);
    if (end == name + 5 + k || *end != '\0' || name[5 + k] == '+' || name[5 + k] == '-') return -1;
    return (id >= t->origMin && id <= t->origMax) ? (int)id : -1;
}

int contentNamedId(const char *kind, const char *name) {
    size_t k = strlen(kind);
    char *end;
    long id;

    if (name == NULL || strncmp(name, "dw3:", 4) != 0 || strncmp(name + 4, kind, k) != 0 || name[4 + k] != '/') return -1;
    if (name[5 + k] < '0' || name[5 + k] > '9') return -1;
    id = strtol(name + 5 + k, &end, 10);
    return (*end == '\0' && id >= 0 && id <= 0x7FFFFFFF) ? (int)id : -1;
}

void contentOriginalName(const ContentType *t, int id, char *out, size_t cap) {
    snprintf(out, cap, "dw3:%s/%d", t->kind, id);
}

/* a "base": an original id as a number, or as its name "dw3:<kind>/<id>"; -1 when it is neither */
static int baseOf(const ContentType *t, const ExtJson *f) {
    if (f == NULL) return -1;
    if (f->t == 0) return extWhole(f->num, t->origMin, t->origMax) ? (int)f->num : -1;
    if (f->t == 1 && f->str != NULL) return contentOriginalId(t, f->str);
    return -1;
}

static void addEntry(const ContentType *t, const char *mod, const char *key, const ExtJson *def, const char *path,
                     ContentEntry **list, int *n) {
    ContentEntry *e, *grown;
    void *rec;
    int i, base;
    char full[96];

    if (!extValidKey(key) || strlen(mod) + strlen(key) + 2 > sizeof full) {
        plogLine(t->channel, "%s %s \"%s\" rejected: the key is 1-48 characters of a-z, 0-9, - and _\n", path, t->kind, key);
        return;
    }
    snprintf(full, sizeof full, "%s:%s", mod, key);
    base = def->t == 2 ? baseOf(t, extJsonGet(def, "base")) : -1;
    if (base < 0) {
        plogLine(t->channel, "%s %s \"%s\" rejected: \"base\" must be an original %s id (%d..%d)\n", path, t->kind, full, t->noun,
                 t->origMin, t->origMax);
        return;
    }
    rec = calloc(1, t->recSize);
    if (rec == NULL) return;
    if (!t->copyBase(base, rec)) {
        free(rec);
        return;
    }
    grown = realloc(*list, sizeof(ContentEntry) * (size_t)(*n + 1));
    if (grown == NULL) {
        if (t->freeRec != NULL) t->freeRec(rec);
        free(rec);
        return;
    }
    *list = grown;
    e = &grown[*n];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", full);
    e->path = strdup(path);
    e->baseId = base;
    e->rec = rec;
    for (i = 0; i < def->n; i++) {
        const char *k = def->keys[i];
        const ExtJson *v = &def->v[i];

        if (!strcmp(k, "base")) continue;
        if (!strcmp(k, "name")) {
            extTakeTexts(e->text[0], v, t->kind, path, full, "name");
        } else if (!strcmp(k, "description")) {
            extTakeTexts(e->text[1], v, t->kind, path, full, "description");
        } else if (!t->field(e, k, v)) {
            plogLine(t->channel, "%s %s \"%s\" field \"%s\" rejected: unknown field\n", path, t->kind, full, k);
        }
    }
    if (e->text[0][0] == NULL) {
        plogLine(t->channel, "%s %s \"%s\" rejected: it needs a name in \"eng\"\n", path, t->kind, full);
        freeEntry(t, e);
        return;
    }
    (*n)++;
}

/* one content mod's file of this type */
static void readFile(const ContentType *t, int m, ContentEntry **list, int *n) {
    char path[1200], format[40];
    uint8_t *data;
    size_t len;
    ExtJson root;
    const ExtJson *fmt, *news;
    int i;

    snprintf(path, sizeof path, "%s/content/%s.json", modsDirOf(m), t->channel);
    snprintf(format, sizeof format, "%s-v1", t->channel);
    if (tdReadFile(path, &data, &len) != 0) return; /* a content mod without this type */
    if (extJsonParse(data, len, &root) != 0) {
        plogLine(t->channel, "%s rejected: not valid JSON\n", path);
    } else if ((fmt = extJsonGet(&root, "format")) == NULL || fmt->t != 1 || strcmp(fmt->str, format) != 0) {
        plogLine(t->channel, "%s rejected: \"format\" must be \"%s\"\n", path, format);
    } else if ((news = extJsonGet(&root, "new")) != NULL) {
        if (news->t != 2) {
            plogLine(t->channel, "%s rejected: \"new\" must be an object of %s\n", path, t->plural);
        } else {
            for (i = 0; i < news->n; i++) addEntry(t, modsNameOf(m), news->keys[i], &news->v[i], path, list, n);
        }
    }
    extJsonFree(&root);
    free(data);
}

int contentLoad(ContentSet *s) {
    const ContentType *t = s->t;
    ContentEntry *list = NULL;
    int n = 0, m, i;

    for (m = 0; m < modsCount(); m++) {
        if (strcmp(modsKindOf(m), "content") != 0) continue;
        readFile(t, m, &list, &n);
    }
    if (n == 0) {
        free(list);
        return 0;
    }
    qsort(list, (size_t)n, sizeof *list, cmpEntry);
    if (n > t->maxExt) {
        if (t->limitNote != NULL) plogLine(t->channel, t->limitNote, n, t->maxExt);
        for (i = t->maxExt; i < n; i++) freeEntry(t, &list[i]);
        n = t->maxExt;
    }
    for (i = 0; i < n; i++) PLOG(t->channel, PLOG_MODS, "%s %s = id %d (based on %d)\n", t->kind, list[i].name, t->firstExtId + i, list[i].baseId);
    s->e = list;
    s->n = n;
    if (t->publish != NULL && !t->publish(s)) {
        contentUnload(s);
        return 0;
    }
    return n;
}

void contentUnload(ContentSet *s) {
    int i;
    for (i = 0; i < s->n; i++) freeEntry(s->t, &s->e[i]);
    free(s->e);
    s->e = NULL;
    s->n = 0;
    s->t->setNameHooks(NULL, NULL);
    s->t->setTextHook(NULL, NULL);
}

void contentAttach(const ContentSet *s) {
    s->t->setNameHooks(s->t->nameOf, s->t->idOf);
    s->t->setTextHook(s->t->count, s->t->text);
}

int contentCount(const ContentSet *s) {
    return s->n;
}

int contentIdOf(const ContentSet *s, const char *name) {
    int i;
    for (i = 0; name != NULL && i < s->n; i++) if (!strcmp(s->e[i].name, name)) return s->t->firstExtId + i;
    return -1;
}

const char *contentNameOf(const ContentSet *s, int id) {
    int i = id - s->t->firstExtId;
    return (i >= 0 && i < s->n) ? s->e[i].name : NULL;
}

void *contentRec(const ContentSet *s, int index) {
    return (index >= 0 && index < s->n) ? s->e[index].rec : NULL;
}

int contentResolve(const ContentSet *s, const char *name) {
    int id = contentOriginalId(s->t, name);
    return id >= 0 ? id : contentIdOf(s, name);
}

const char *contentText(const ContentSet *s, int index, const char *langDir, int desc) {
    const ContentEntry *e;
    const char *t;
    static const char *const none = "";

    if (index < 0 || index >= s->n || desc < 0 || desc > 1) return none;
    e = &s->e[index];
    t = extPickText(e->text[desc], langDir);
    if (t != NULL) return t;
    return desc == 0 ? e->name : none; /* a description may be absent */
}

/* ---- the reference export ---- */

typedef struct Buf {
    char *b;
    size_t n, cap;
    int bad;
} Buf;

static void put(Buf *B, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Buf *B, const char *fmt, ...) {
    va_list ap;
    int w;

    if (B->bad) return;
    for (;;) {
        va_start(ap, fmt);
        w = vsnprintf(B->b != NULL ? B->b + B->n : NULL, B->cap - B->n, fmt, ap);
        va_end(ap);
        if (w < 0) { B->bad = 1; return; }
        if (B->b != NULL && B->n + (size_t)w < B->cap) {
            B->n += (size_t)w;
            return;
        }
        {
            size_t ncap = B->cap != 0 ? B->cap * 2 : 4096;
            char *nb;
            while (ncap < B->n + (size_t)w + 1) ncap *= 2;
            nb = realloc(B->b, ncap);
            if (nb == NULL) { B->bad = 1; return; }
            B->b = nb;
            B->cap = ncap;
        }
    }
}

/* a JSON string body: quotes, backslashes and control characters escaped; UTF-8 passes through */
static void putEscaped(Buf *B, const char *s) {
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') put(B, "\\%c", c);
        else if (c < 0x20) put(B, "\\u%04x", c);
        else put(B, "%c", c);
    }
}

char *contentReferenceText(const ContentType *t, const char *(*engName)(int id)) {
    Buf B = {0};
    char member[2048];
    int id, first = 1;

    put(&B, "{\n  \"format\": \"content-ref-v1\",\n  \"kind\": \"%s\",\n  \"notes\": {\n", t->kind);
    put(&B, "    \"about\": \"Reference copy of the original %s, written by the port from your disc. Editing this file does not change the game: it is only a list of names. Your changes go in a content mod, mods/<mod>/content/%s.json (format %s-v1).\",\n",
        t->plural, t->channel, t->channel);
    put(&B, "    \"names\": \"Wherever a mod gives the id of an original %s it can write the name shown here (dw3:%s/<id>) instead of the number; a new %s of a mod is <mod folder>:<key>.\"", t->noun, t->kind, t->noun);
    if (t->refNote != NULL) {
        put(&B, ",\n    \"fields\": \"");
        putEscaped(&B, t->refNote);
        put(&B, "\"");
    }
    put(&B, "\n  },\n  \"entries\": {\n");
    for (id = t->origMin; id <= t->origMax; id++) {
        const char *eng = engName != NULL ? engName(id) : NULL;

        member[0] = '\0';
        if (t->describe != NULL && !t->describe(id, member, sizeof member)) continue;
        put(&B, "%s    \"dw3:%s/%d\": { \"id\": %d", first ? "" : ",\n", t->kind, id, id);
        first = 0;
        if (eng != NULL) {
            put(&B, ", \"eng\": \"");
            putEscaped(&B, eng);
            put(&B, "\"");
        }
        if (member[0] != '\0') put(&B, ", %s", member);
        put(&B, " }");
    }
    put(&B, "\n  }\n}\n");
    if (B.bad) {
        free(B.b);
        return NULL;
    }
    return B.b;
}
