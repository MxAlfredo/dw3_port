/*
 * Mods and the flat JSON reader. See mods.h. Pure stdlib plus the file helpers of text_data.c.
 */
#include "mods.h"
#include "port_log.h"

#include "port_paths.h"
#include "port_version.h"
#include "text_data.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- flat JSON ---- */

typedef struct Jp {
    const char *p, *end;
    char *err;
    unsigned long errSize;
} Jp;

static int jfail(Jp *j, const char *msg)
{
    if (j->err && j->errSize) snprintf(j->err, j->errSize, "%s", msg);
    return -1;
}

static void jws(Jp *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

/* A string token into out (cap bytes, "" when out is NULL); escapes: \" \\ \/ \n \t only */
static int jstring(Jp *j, char *out, size_t cap)
{
    size_t n = 0;
    if (j->p >= j->end || *j->p != '"') return jfail(j, "expected a string");
    j->p++;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if ((unsigned char)c < 0x20) return jfail(j, "control character in a string");
        if (c == '\\') {
            if (j->p >= j->end) return jfail(j, "bad escape");
            c = *j->p++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c != '"' && c != '\\' && c != '/') return jfail(j, "unsupported escape");
        }
        if (out) {
            if (n + 1 >= cap) return jfail(j, "string too long");
            out[n] = c;
        }
        n++;
    }
    if (j->p >= j->end) return jfail(j, "unterminated string");
    j->p++;
    if (out) out[n] = 0;
    return 0;
}

static int jnumber(Jp *j, double *v)
{
    char buf[40];
    size_t n = 0;
    char *endp;
    while (j->p < j->end && n < sizeof buf - 1 &&
           ((*j->p >= '0' && *j->p <= '9') || *j->p == '-' || *j->p == '+' || *j->p == '.' || *j->p == 'e' || *j->p == 'E')) {
        buf[n++] = *j->p++;
    }
    buf[n] = 0;
    if (n == 0) return jfail(j, "expected a value");
    *v = strtod(buf, &endp);
    if (*endp) return jfail(j, "bad number");
    return 0;
}

static int jword(Jp *j, const char *w)
{
    size_t n = strlen(w);
    if ((size_t)(j->end - j->p) < n || strncmp(j->p, w, n)) return -1;
    j->p += n;
    return 0;
}

static int jobject(Jp *j, PjVal *out, int max, int depth);

static int jmembers(Jp *j, PjVal *v, int depth)
{
    jws(j);
    if (j->p >= j->end) return jfail(j, "unexpected end");
    if (*j->p == '"') {
        v->kind = PJ_STR;
        return jstring(j, v->str, sizeof v->str);
    }
    if (*j->p == '{') {
        if (depth >= 2) return jfail(j, "objects nested too deep");
        v->kind = PJ_OBJ;
        return jobject(j, NULL, 0, depth + 1) < 0 ? -1 : 0;
    }
    if (*j->p == '[') {
        double d;
        v->kind = PJ_ARR;
        v->arrCount = 0;
        j->p++;
        jws(j);
        if (j->p < j->end && *j->p == ']') { j->p++; return 0; }
        if (j->p < j->end && *j->p == '"') { /* a list of strings, kept one per line in str */
            size_t used = 0;
            v->kind = PJ_SARR;
            for (;;) {
                char one[96];
                size_t n;
                jws(j);
                if (jstring(j, one, sizeof one)) return -1;
                n = strlen(one);
                if (used + n + 2 > sizeof v->str || strchr(one, '\n')) return jfail(j, "list of strings too long");
                memcpy(v->str + used, one, n);
                used += n;
                v->str[used++] = '\n';
                v->str[used] = 0;
                v->arrCount++;
                jws(j);
                if (j->p < j->end && *j->p == ',') { j->p++; continue; }
                if (j->p < j->end && *j->p == ']') { j->p++; return 0; }
                return jfail(j, "expected , or ]");
            }
        }
        for (;;) {
            jws(j);
            if (jnumber(j, &d)) return -1;
            if (d != floor(d) || d < -1e9 || d > 1e9) return jfail(j, "array holds a non-integer");
            if (v->arrCount >= PJ_MAX_ARR) return jfail(j, "array too long");
            v->arr[v->arrCount++] = (int)d;
            jws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == ']') { j->p++; return 0; }
            return jfail(j, "expected , or ]");
        }
    }
    if (jword(j, "true") == 0) { v->kind = PJ_BOOL; v->num = 1; return 0; }
    if (jword(j, "false") == 0) { v->kind = PJ_BOOL; v->num = 0; return 0; }
    v->kind = PJ_NUM;
    return jnumber(j, &v->num);
}

/* Members of one object; returns the stored count or -1. out NULL: validate and discard. */
static int jobject(Jp *j, PjVal *out, int max, int depth)
{
    int n = 0, i;
    PjVal tmp;
    jws(j);
    if (j->p >= j->end || *j->p != '{') return jfail(j, "expected {");
    j->p++;
    jws(j);
    if (j->p < j->end && *j->p == '}') { j->p++; return 0; }
    for (;;) {
        PjVal *v = (out && n < max) ? &out[n] : &tmp;
        memset(v, 0, sizeof *v);
        jws(j);
        if (jstring(j, v->key, sizeof v->key)) return -1;
        jws(j);
        if (j->p >= j->end || *j->p != ':') return jfail(j, "expected :");
        j->p++;
        if (jmembers(j, v, depth)) return -1;
        if (out) {
            for (i = 0; i < n && i < max; i++) {
                if (!strcmp(out[i].key, v->key)) return jfail(j, "duplicate key");
            }
            if (n >= max) return jfail(j, "too many keys");
            n++;
        }
        jws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == '}') { j->p++; return n; }
        return jfail(j, "expected , or }");
    }
}

int pjParseFlat(const char *text, unsigned long len, PjVal *out, int max, char *err, unsigned long errSize)
{
    Jp j;
    int n;
    j.p = text;
    j.end = text + len;
    j.err = err;
    j.errSize = errSize;
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) j.p += 3; /* an editor's BOM */
    n = jobject(&j, out, max, 0);
    if (n < 0) {
        if (err && errSize) {
            size_t used = strlen(err);
            if (used + 1 < errSize) snprintf(err + used, errSize - used, " at byte %ld", (long)(j.p - text));
        }
        return -1;
    }
    jws(&j);
    if (j.p != j.end) {
        if (err && errSize) snprintf(err, errSize, "text after the object at byte %ld", (long)(j.p - text));
        return -1;
    }
    return n;
}

const PjVal *pjFind(const PjVal *vals, int n, const char *key)
{
    int i;
    for (i = 0; i < n; i++) {
        if (!strcmp(vals[i].key, key)) return &vals[i];
    }
    return NULL;
}

/* ---- the mods folder ---- */

#define MOD_REQ_MAX 8

typedef struct Req {
    char name[64];
    char op[3];       /* "" (any version), ">=", ">", "<=", "<", "=" */
    char version[24];
} Req;

typedef struct Manifest {
    char name[64];    /* the folder name: the id other mods and settings.ini use */
    char dir[1100];
    char version[24];
    char kind[8];
    Req port;         /* name unused */
    int hasPort;
    Req req[MOD_REQ_MAX];
    int nReq;
} Manifest;

static char gNames[MODS_MAX][64];
static char gDirs[MODS_MAX][1100];
static char gVersions[MODS_MAX][24];
static char gKinds[MODS_MAX][8];
static int gCount;

/* what the last modsInit saw, for --list-mods and --validate-mod: every folder with a mod.json, active or not, and why */
static ModsReport gRep[MODS_MAX];
static int gRepN;

static ModsReport *repFind(const char *name)
{
    int i;
    for (i = 0; i < gRepN; i++) {
        if (!strcmp(gRep[i].name, name)) return &gRep[i];
    }
    return NULL;
}

static void repAdd(const char *name, const char *dir, const char *version, const char *kind, int status, const char *reason)
{
    ModsReport *r;
    if (gRepN >= MODS_MAX) return;
    r = &gRep[gRepN++];
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%.63s", name);
    snprintf(r->dir, sizeof r->dir, "%.1099s", dir);
    snprintf(r->version, sizeof r->version, "%.23s", version ? version : "0");
    snprintf(r->kind, sizeof r->kind, "%.7s", kind ? kind : "assets");
    r->status = status;
    snprintf(r->reason, sizeof r->reason, "%.199s", reason ? reason : "");
}

static void repMark(const char *name, int status, const char *reason)
{
    ModsReport *r = repFind(name);
    if (r) {
        r->status = status;
        snprintf(r->reason, sizeof r->reason, "%.199s", reason);
    }
}

/* ---- asset layers (mods.h) ---- */

static char gDataDir[1100];

void assetSetData(const char *dataDir) { snprintf(gDataDir, sizeof gDataDir, "%s", dataDir ? dataDir : ""); }
const char *assetDataDir(void) { return gDataDir[0] ? gDataDir : NULL; }
int assetLayerCount(void) { return gCount + (gDataDir[0] ? 1 : 0); }

int assetLayerAt(int i, AssetLayer *out)
{
    if (i < 0 || i >= assetLayerCount()) return 0;
    if (i < gCount) {
        int m = gCount - 1 - i; /* the last mod wins */
        out->root = gDirs[m];
        out->mod = m;
        snprintf(out->label, sizeof out->label, "mods/%s", gNames[m]);
    } else {
        out->root = gDataDir;
        out->mod = -1;
        snprintf(out->label, sizeof out->label, "data");
    }
    return 1;
}

/* <root>/<topic>/<rel> as a file or a folder, matched without regard to case (ADR-010 section 3); topic may be NULL */
static int rootEntry(const char *root, const char *topic, const char *rel, char *path, size_t cap, int wantDir)
{
    char sub[600], real[1400];
    struct stat st;
    int n = topic && topic[0] ? snprintf(sub, sizeof sub, "%s/%s", topic, rel) : snprintf(sub, sizeof sub, "%s", rel);

    if (n >= (int)sizeof sub) return 0;
    if (!portPathsResolveCI(root, sub, real, sizeof real) || stat(real, &st) != 0) return 0;
    if (wantDir ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) return 0;
    return snprintf(path, cap, "%s", real) < (int)cap;
}

int assetLayerFile(const AssetLayer *layer, const char *topic, const char *rel, char *path, size_t cap)
{
    return rootEntry(layer->root, topic, rel, path, cap, 0);
}

int assetModFile(int mod, const char *topic, const char *rel, char *path, size_t cap)
{
    return mod >= 0 && mod < gCount && rootEntry(gDirs[mod], topic, rel, path, cap, 0);
}

int assetModDir(int mod, const char *topic, const char *rel, char *path, size_t cap)
{
    return mod >= 0 && mod < gCount && rootEntry(gDirs[mod], topic, rel, path, cap, 1);
}

int assetResolve(const char *topic, const char *rel, char *path, size_t cap, AssetLayer *from)
{
    AssetLayer l;
    int i;

    for (i = 0; assetLayerAt(i, &l); i++) {
        if (!assetLayerFile(&l, topic, rel, path, cap)) continue;
        if (from) *from = l;
        return 1;
    }
    return 0;
}

int modsReportCount(void) { return gRepN; }
const ModsReport *modsReportAt(int i) { return (i >= 0 && i < gRepN) ? &gRep[i] : NULL; }

int modsCount(void) { return gCount; }

static int gHotReload = -1;

int modsHotReload(void)
{
    if (gHotReload < 0) {
        const char *v = getenv("DW3_MODS_HOTRELOAD");
        gHotReload = v != NULL && v[0] != '\0' && v[0] != '0';
    }
    return gHotReload;
}

void modsSetHotReload(int on) { gHotReload = on < 0 ? -1 : on != 0; }
const char *modsNameOf(int i) { return (i >= 0 && i < gCount) ? gNames[i] : NULL; }
const char *modsDirOf(int i) { return (i >= 0 && i < gCount) ? gDirs[i] : NULL; }
const char *modsVersionOf(int i) { return (i >= 0 && i < gCount) ? gVersions[i] : NULL; }
const char *modsKindOf(int i) { return (i >= 0 && i < gCount) ? gKinds[i] : NULL; }

static int isDirPath(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* "1.2.3-dev" -> 1, 2, 3 (a missing part is 0; the suffix after '-' is ignored). 0 when it is not a version. */
static int parseVersion(const char *v, long out[3])
{
    int part = 0;
    const char *p = v;
    out[0] = out[1] = out[2] = 0;
    if (!*p) return 0;
    for (;;) {
        long n = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            if (n > 100000000L) return 0;
            digits++;
            p++;
        }
        if (!digits || part >= 3) return 0;
        out[part++] = n;
        if (*p == '.') { p++; continue; }
        if (*p == 0 || *p == '-') return 1;
        return 0;
    }
}

int modsVersionCompare(const char *a, const char *b)
{
    long x[3], y[3];
    int i;
    parseVersion(a, x);
    parseVersion(b, y);
    for (i = 0; i < 3; i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

static int reqHolds(const Req *r, const char *have)
{
    int c;
    if (!r->op[0]) return 1;
    c = modsVersionCompare(have, r->version);
    if (!strcmp(r->op, ">=")) return c >= 0;
    if (!strcmp(r->op, ">")) return c > 0;
    if (!strcmp(r->op, "<=")) return c <= 0;
    if (!strcmp(r->op, "<")) return c < 0;
    return c == 0;
}

static void trimInto(char *dst, size_t cap, const char *s, size_t n)
{
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (n >= cap) n = cap - 1;
    memcpy(dst, s, n);
    dst[n] = 0;
}

/* "name", "name>=1.2" (op then version) or, with wantName 0, just the ">=0.4" part. 0 when well formed. */
static int parseReq(const char *text, int wantName, Req *r)
{
    const char *p = text, *opAt;
    size_t i;
    long v[3];
    memset(r, 0, sizeof *r);
    opAt = text + strcspn(text, "<>=");
    if (wantName) {
        trimInto(r->name, sizeof r->name, text, (size_t)(opAt - text));
        if (!r->name[0]) return -1;
        for (i = 0; r->name[i]; i++) {
            char c = r->name[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return -1;
        }
    } else if (opAt != text) {
        return -1;
    }
    p = opAt;
    if (!*p) return wantName ? 0 : -1; /* a "port" needs a version */
    if (!strncmp(p, ">=", 2) || !strncmp(p, "<=", 2)) { memcpy(r->op, p, 2); p += 2; }
    else if (*p == '>' || *p == '<') { r->op[0] = *p; p++; }
    else if (!strncmp(p, "==", 2)) { r->op[0] = '='; p += 2; }
    else if (*p == '=') { r->op[0] = '='; p++; }
    else return -1;
    trimInto(r->version, sizeof r->version, p, strlen(p));
    return parseVersion(r->version, v) ? 0 : -1;
}

/* 0 when <dir>/mod.json is a valid mod-v1 manifest (filled into m), else the reason in why */
static int checkMod(const char *dir, Manifest *m, char *why, size_t whySize)
{
    char path[1300], err[160];
    uint8_t *data;
    size_t len;
    PjVal v[PJ_MAX_KEYS];
    int n;
    const PjVal *f;

    snprintf(path, sizeof path, "%s/mod.json", dir);
    if (tdReadFile(path, &data, &len)) {
        snprintf(why, whySize, "no readable mod.json");
        return -1;
    }
    n = pjParseFlat((const char *)data, (unsigned long)len, v, PJ_MAX_KEYS, err, sizeof err);
    free(data);
    if (n < 0) {
        snprintf(why, whySize, "mod.json: %s", err);
        return -1;
    }
    f = pjFind(v, n, "format");
    if (!f || f->kind != PJ_STR) {
        snprintf(why, whySize, "mod.json has no \"format\"");
        return -1;
    }
    if (strcmp(f->str, "mod-v1")) {
        snprintf(why, whySize, "mod.json: unknown format \"%.60s\" (this port reads mod-v1)", f->str);
        return -1;
    }
    snprintf(m->version, sizeof m->version, "0");
    snprintf(m->kind, sizeof m->kind, "assets");
    f = pjFind(v, n, "version");
    if (f) {
        long x[3];
        if (f->kind != PJ_STR || strlen(f->str) >= sizeof m->version || !parseVersion(f->str, x)) {
            snprintf(why, whySize, "mod.json: bad \"version\" \"%.40s\" (digits and dots, like 1.2.0)", f->kind == PJ_STR ? f->str : "?");
            return -1;
        }
        snprintf(m->version, sizeof m->version, "%.23s", f->str);
    }
    f = pjFind(v, n, "kind");
    if (f) {
        if (f->kind != PJ_STR || (strcmp(f->str, "assets") && strcmp(f->str, "content"))) {
            snprintf(why, whySize, "mod.json: unknown kind \"%.40s\" (assets or content)", f->kind == PJ_STR ? f->str : "?");
            return -1;
        }
        snprintf(m->kind, sizeof m->kind, "%.7s", f->str);
    }
    f = pjFind(v, n, "port");
    if (f) {
        if (f->kind != PJ_STR || parseReq(f->str, 0, &m->port)) {
            snprintf(why, whySize, "mod.json: bad \"port\" requirement \"%.40s\" (like >=0.4)", f->kind == PJ_STR ? f->str : "?");
            return -1;
        }
        m->hasPort = 1;
    }
    f = pjFind(v, n, "requires");
    if (f && f->kind == PJ_ARR && f->arrCount == 0) f = NULL; /* [] */
    if (f) {
        const char *p;
        if (f->kind != PJ_SARR) {
            snprintf(why, whySize, "mod.json: \"requires\" must be a list of strings");
            return -1;
        }
        for (p = f->str; *p;) {
            const char *nl = strchr(p, '\n');
            char one[96];
            trimInto(one, sizeof one, p, (size_t)(nl - p));
            if (m->nReq >= MOD_REQ_MAX) {
                snprintf(why, whySize, "mod.json: more than %d \"requires\" entries", MOD_REQ_MAX);
                return -1;
            }
            if (parseReq(one, 1, &m->req[m->nReq])) {
                snprintf(why, whySize, "mod.json: bad \"requires\" entry \"%.40s\" (like name or name>=1.2)", one);
                return -1;
            }
            m->nReq++;
            p = nl + 1;
        }
    }
    return 0;
}

static void reqText(const Req *r, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s%s", r->name, r->op, r->version);
}

/* Disable what cannot load (port, missing/old/disabled dependencies), then order the rest so a dependency
   comes before its dependents; `list` holds `*count` manifests in the wanted order and is rewritten. */
static void resolveMods(Manifest *list, int *count)
{
    int n = *count, i, j, k, changed;
    int off[MODS_MAX] = {0}, done[MODS_MAX] = {0}, order[MODS_MAX], nOrder = 0;
    int dep[MODS_MAX][MOD_REQ_MAX];
    char text[160], why[200];

    for (i = 0; i < n; i++) {
        if (list[i].hasPort && !reqHolds(&list[i].port, PORT_VERSION)) {
            reqText(&list[i].port, text, sizeof text);
            plogLine("mods", "%s disabled: needs port %s%s, this is %s\n", list[i].name, list[i].port.op, list[i].port.version, PORT_VERSION);
            {
                char why[160];
                snprintf(why, sizeof why, "needs port %s%s, this is %s", list[i].port.op, list[i].port.version, PORT_VERSION);
                repMark(list[i].name, MODS_REP_DISABLED, why);
            }
            off[i] = 1;
        }
    }
    do { /* a dependency that is absent, too old or itself disabled disables its dependents (until nothing changes) */
        changed = 0;
        for (i = 0; i < n; i++) {
            if (off[i]) continue;
            for (k = 0; k < list[i].nReq; k++) {
                const Req *r = &list[i].req[k];
                int found = -1;
                for (j = 0; j < n; j++) {
                    if (!strcmp(list[j].name, r->name)) found = j;
                }
                reqText(r, text, sizeof text);
                if (found < 0) {
                    plogLine("mods", "%s disabled: requires %s, not installed or not active\n", list[i].name, text);
                    snprintf(why, sizeof why, "requires %s, not installed or not active", text);
                } else if (off[found] && found != i) {
                    plogLine("mods", "%s disabled: requires %s, which is disabled\n", list[i].name, r->name);
                    snprintf(why, sizeof why, "requires %s, which is disabled", r->name);
                } else if (!reqHolds(r, list[found].version)) {
                    plogLine("mods", "%s disabled: requires %s, found %s\n", list[i].name, text, list[found].version);
                    snprintf(why, sizeof why, "requires %s, found %s", text, list[found].version);
                } else {
                    continue;
                }
                repMark(list[i].name, MODS_REP_DISABLED, why);
                off[i] = 1;
                changed = 1;
                break;
            }
        }
    } while (changed);
    for (i = 0; i < n; i++) {
        for (k = 0; k < list[i].nReq; k++) {
            dep[i][k] = -1;
            for (j = 0; j < n; j++) {
                if (!strcmp(list[j].name, list[i].req[k].name)) dep[i][k] = j;
            }
        }
    }
    /* stable topological order: always the first mod, in the wanted order, whose dependencies are all placed */
    for (;;) {
        int pick = -1;
        for (i = 0; i < n && pick < 0; i++) {
            int ready = !off[i] && !done[i];
            for (k = 0; ready && k < list[i].nReq; k++) {
                if (dep[i][k] >= 0 && !done[dep[i][k]]) ready = 0;
            }
            if (ready) pick = i;
        }
        if (pick < 0) break;
        done[pick] = 1;
        order[nOrder++] = pick;
    }
    {
        char names[600];
        size_t used = 0;
        names[0] = 0;
        for (i = 0; i < n; i++) {
            if (!off[i] && !done[i]) {
                used += (size_t)snprintf(names + used, sizeof names - used, " %s", list[i].name);
                if (used >= sizeof names) used = sizeof names - 1;
            }
        }
        if (names[0]) plogLine("mods", "dependency cycle, disabled:%s\n", names);
        for (i = 0; i < n; i++) {
            if (!off[i] && !done[i]) repMark(list[i].name, MODS_REP_DISABLED, "in a dependency cycle");
        }
    }
    {
        Manifest *copy = (Manifest *)malloc((size_t)(nOrder ? nOrder : 1) * sizeof *copy);
        if (!copy) { *count = 0; return; }
        for (i = 0; i < nOrder; i++) copy[i] = list[order[i]];
        for (i = 0; i < nOrder; i++) list[i] = copy[i];
        free(copy);
        *count = nOrder;
    }
}

int modsInit(const char *root, const char *order)
{
    static Manifest valid[MODS_MAX], active[MODS_MAX];
    int nvalid = 0, nactive = 0, i, scanned = 0;
    char *orderBuf = NULL;
    DIR *d;
    struct dirent *e;

    gCount = 0;
    gRepN = 0;
    if (!root || !root[0]) return 0;
    d = opendir(root);
    if (!d) {
        if (order && order[0]) plogLine("mods", "%s does not exist; the [mods] order is ignored\n", root);
        return 0;
    }
    memset(valid, 0, sizeof valid);
    memset(active, 0, sizeof active);
    while ((e = readdir(d)) != NULL) {
        char dir[1200], why[200];
        Manifest m;
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof valid[0].name) continue;
        snprintf(dir, sizeof dir, "%s/%s", root, e->d_name);
        if (!isDirPath(dir)) continue;
        memset(&m, 0, sizeof m);
        if (checkMod(dir, &m, why, sizeof why)) {
            plogLine("mods", "%s rejected: %s\n", dir, why);
            repAdd(e->d_name, dir, "0", "assets", MODS_REP_REJECTED, why);
            continue;
        }
        if (nvalid >= MODS_MAX) {
            plogLine("mods", "%s rejected: more than %d mods\n", dir, MODS_MAX);
            repAdd(e->d_name, dir, "0", "assets", MODS_REP_REJECTED, "more than the most mods one run holds");
            continue;
        }
        snprintf(m.name, sizeof m.name, "%s", e->d_name);
        snprintf(m.dir, sizeof m.dir, "%.1000s/%.63s", root, e->d_name);
        valid[nvalid++] = m;
        repAdd(m.name, m.dir, m.version, m.kind, MODS_REP_NOT_LISTED, "not in the [mods] order of settings.ini");
    }
    closedir(d);
    scanned = nvalid;
    {
        /* sort by folder name */
        int a, b;
        for (a = 1; a < nvalid; a++) {
            Manifest t = valid[a];
            for (b = a - 1; b >= 0 && strcmp(valid[b].name, t.name) > 0; b--) valid[b + 1] = valid[b];
            valid[b + 1] = t;
        }
    }

    if (order && order[0] && (orderBuf = strdup(order)) == NULL) {
        plogLine("mods", "out of memory reading the [mods] order; every mod stays in alphabetical order\n");
        order = NULL;
    }
    if (order && order[0]) {
        /* tokens "-name" are the mods the player turned off ([mods] disabled); the others are the order. The text is the
           whole [mods] order line (no length cap): split it once, then run the two lists */
        char *tok, *save = NULL, **names = NULL;
        int nNames = 0, nPos = 0, capNames = 0, k;
        unsigned char *isOff = NULL;
        for (tok = orderBuf;; tok = save) {
            char *comma = strchr(tok, ',');
            char *a, *b;
            if (comma) { *comma = 0; save = comma + 1; } else save = NULL;
            a = tok;
            while (*a == ' ' || *a == '\t') a++;
            b = a + strlen(a);
            while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n')) b--;
            *b = 0;
            if (*a) {
                if (nNames >= capNames) {
                    int ncap = capNames ? capNames * 2 : 32;
                    char **nn = (char **)realloc(names, (size_t)ncap * sizeof *nn);
                    unsigned char *no = (unsigned char *)realloc(isOff, (size_t)ncap);
                    if (nn) names = nn;
                    if (no) isOff = no;
                    if (!nn || !no) break;
                    capNames = ncap;
                }
                isOff[nNames] = (unsigned char)(*a == '-');
                names[nNames++] = *a == '-' ? a + 1 : a;
                nPos += *a != '-';
            }
            if (!save) break;
        }
        for (k = 0; k < nNames; k++) {
            if (isOff[k]) {
                ModsReport *r = repFind(names[k]);
                if (r) r->byUser = 1; /* kept after the repMark below */
            }
        }
        if (nPos == 0) {
            /* only "-name" entries: every mod stays on in alphabetical order, minus the ones turned off */
            order = NULL;
            for (i = 0; i < nvalid; i++) {
                int off = 0;
                for (k = 0; k < nNames; k++) off |= isOff[k] && !strcmp(names[k], valid[i].name);
                if (!off) active[nactive++] = valid[i];
            }
        } else {
            for (k = 0; k < nNames; k++) {
                int found = 0, dup = 0, off = 0, j;
                if (isOff[k]) continue;
                for (j = 0; j < nNames; j++) off |= isOff[j] && !strcmp(names[j], names[k]);
                if (off) continue; /* turned off wins over the order */
                for (i = 0; i < nactive; i++) dup |= !strcmp(active[i].name, names[k]);
                for (i = 0; i < nvalid && !dup; i++) {
                    if (!strcmp(valid[i].name, names[k])) {
                        found = 1;
                        if (nactive < MODS_MAX) active[nactive++] = valid[i];
                    }
                }
                if (!found && !dup) {
                    plogLine("mods", "[mods] order lists \"%s\": not installed or not valid, skipped\n", names[k]);
                    if (!repFind(names[k])) repAdd(names[k], "", "0", "assets", MODS_REP_MISSING, "listed in the [mods] order but not installed");
                }
            }
        }
        free(names);
        free(isOff);
        free(orderBuf);
    } else {
        for (i = 0; i < nvalid; i++) active[nactive++] = valid[i];
    }
    {
        int r;
        for (r = 0; r < gRepN; r++) {
            if (gRep[r].byUser && gRep[r].status == MODS_REP_NOT_LISTED) {
                gRep[r].status = MODS_REP_DISABLED;
                snprintf(gRep[r].reason, sizeof gRep[r].reason, "turned off in the [mods] disabled line of settings.ini");
            }
        }
    }
    for (i = 0; i < nactive; i++) repMark(active[i].name, MODS_REP_ACTIVE, "");
    resolveMods(active, &nactive);
    for (i = 0; i < nactive; i++) {
        ModsReport *r = repFind(active[i].name);
        if (r) r->position = i + 1;
    }
    {
        int a, b; /* the report sorted by folder name */
        for (a = 1; a < gRepN; a++) {
            ModsReport t = gRep[a];
            for (b = a - 1; b >= 0 && strcmp(gRep[b].name, t.name) > 0; b--) gRep[b + 1] = gRep[b];
            gRep[b + 1] = t;
        }
    }
    for (i = 0; i < nactive; i++) {
        snprintf(gNames[i], sizeof gNames[0], "%.63s", active[i].name);
        snprintf(gDirs[i], sizeof gDirs[0], "%.1099s", active[i].dir);
        snprintf(gVersions[i], sizeof gVersions[0], "%.23s", active[i].version);
        snprintf(gKinds[i], sizeof gKinds[0], "%.7s", active[i].kind);
    }
    gCount = nactive;
    if (gCount || scanned) {
        plogLine("mods", "%d active in %s (%s order):", gCount, root, (order && order[0]) ? "settings.ini" : "alphabetical");
        for (i = 0; i < gCount; i++) plogRaw(" %s", gNames[i]);
        plogRaw("\n");
    }
    return gCount;
}
