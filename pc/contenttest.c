#define _GNU_SOURCE 1
/*
 * Host test of the shared content core (pc/shim/content.c, content-core C1/C2) with a made-up type, "gizmo": ids in
 * alphabetical order, the limit, rejected entries, texts, original names ("dw3:gizmo/<id>"), hooks and the save chunk
 * byte helpers. No game, no disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim/content.h"
#include "shim/mods.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "contenttest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gRoot[600];

static void putFile(const char *rel, const char *text)
{
    char path[900], dir[900], *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "mkdir -p '%s'", path);
    slash = strrchr(dir, '/');
    *slash = '\'';
    slash[1] = 0;
    if (system(dir)) {}
    f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void modJson(const char *mod, const char *kind)
{
    char rel[200], text[200];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", mod);
    snprintf(text, sizeof text, "{\"format\":\"mod-v1\",\"name\":\"%s\",\"version\":\"1.0\",\"kind\":\"%s\"}", mod, kind);
    putFile(rel, text);
}

/* the made-up type: a record is one number, "v" (0..99); the original gizmos 1..10 have v = 10 * id */
typedef struct Gizmo { int v; } Gizmo;
static int sFail, sNameHooks, sTextHooks, sPublished;

static int copyBase(int baseId, void *rec) { ((Gizmo *)rec)->v = 10 * baseId; return 1; }
static int field(const ContentEntry *e, const char *k, const ExtJson *v)
{
    (void)e;
    if (strcmp(k, "v") != 0) return 0;
    if (v->t == 0 && extWhole(v->num, 0, 99)) ((Gizmo *)e->rec)->v = (int)v->num;
    return 1;
}
static ContentSet sSet;
static int publish(ContentSet *s) { sPublished = s->n; if (sFail) return 0; contentAttach(s); return 1; }
static const char *nameOf(int id) { return contentNameOf(&sSet, id); }
static int idOf(const char *name) { return contentIdOf(&sSet, name); }
static int count(void) { return contentCount(&sSet); }
static const char *text(int i, const char *l, int d) { return contentText(&sSet, i, l, d); }
static void setNames(const char *(*n)(int), int (*i)(const char *)) { sNameHooks = (n != NULL && i != NULL); }
static void setText(int (*c)(void), const char *(*t)(int, const char *, int)) { sTextHooks = (c != NULL && t != NULL); }

static const ContentType kGizmo = {
    "gizmo", "gizmo", "gizmos", "gizmos", 1, 10, 100, 3, "%d gizmos declared; only the first %d fit\n", sizeof(Gizmo),
    copyBase, field, NULL, publish, nameOf, idOf, count, text, setNames, setText,
};

static int load(void)
{
    char root[700];
    snprintf(root, sizeof root, "%s/mods", gRoot);
    modsInit(root, NULL);
    sSet.t = &kGizmo;
    contentUnload(&sSet);
    return contentLoad(&sSet);
}

static void testLoad(void)
{
    modJson("zeta", "content");
    modJson("alpha", "content");
    modJson("plain", "assets");
    putFile("mods/zeta/content/gizmos.json",
            "{\"format\":\"gizmos-v1\",\"new\":{"
            "\"b\":{\"base\":3,\"v\":5,\"name\":{\"eng\":\"Bee\",\"spa\":\"Abeja\"},\"description\":{\"eng\":\"Buzz\"}},"
            "\"a\":{\"base\":4,\"name\":{\"eng\":\"Ay\",\"es-419\":\"Ei\"}},"
            "\"bad-base\":{\"base\":99,\"name\":{\"eng\":\"x\"}},"
            "\"no-name\":{\"base\":3},"
            "\"Bad Key\":{\"base\":3,\"name\":{\"eng\":\"x\"}},"
            "\"odd\":{\"base\":3,\"nope\":1,\"name\":{\"eng\":\"Odd\"}}}}");
    putFile("mods/alpha/content/gizmos.json", "{\"format\":\"gizmos-v1\",\"new\":{\"z\":{\"base\":1,\"name\":{\"eng\":\"Zed\"}}}}");
    putFile("mods/plain/content/gizmos.json", "{\"format\":\"gizmos-v1\",\"new\":{\"p\":{\"base\":1,\"name\":{\"eng\":\"P\"}}}}");

    /* alphabetical by "<mod>:<key>", valid entries of content mods only; 4 are valid, the limit keeps 3 */
    CHECK(load() == 3 && sPublished == 3 && count() == 3);
    CHECK(sNameHooks == 1 && sTextHooks == 1);
    CHECK(idOf("alpha:z") == 100 && idOf("zeta:a") == 101 && idOf("zeta:b") == 102);
    CHECK(idOf("zeta:odd") == -1 && idOf("plain:p") == -1 && idOf("zeta:bad-base") == -1 && idOf("zeta:no-name") == -1);
    CHECK(strcmp(nameOf(102), "zeta:b") == 0 && nameOf(99) == NULL && nameOf(103) == NULL);
    CHECK(((Gizmo *)contentRec(&sSet, 0))->v == 10 && ((Gizmo *)contentRec(&sSet, 1))->v == 40 &&
          ((Gizmo *)contentRec(&sSet, 2))->v == 5);
    CHECK(contentRec(&sSet, 3) == NULL && contentRec(&sSet, -1) == NULL);
    CHECK(load() == 3 && idOf("zeta:b") == 102); /* the same twice */

    /* texts: es-419 -> spa -> eng; a missing description is empty; the name falls back to the full name */
    CHECK(strcmp(text(2, "eng", 0), "Bee") == 0 && strcmp(text(2, "es-419", 0), "Abeja") == 0 && strcmp(text(2, "fra", 0), "Bee") == 0);
    CHECK(strcmp(text(1, "es-419", 0), "Ei") == 0 && strcmp(text(1, "spa", 0), "Ay") == 0);
    CHECK(strcmp(text(2, "eng", 1), "Buzz") == 0 && text(1, "eng", 1)[0] == '\0' && text(77, "eng", 0)[0] == '\0');

    /* a type that cannot publish leaves nothing */
    sFail = 1;
    CHECK(load() == 0 && count() == 0 && idOf("zeta:b") == -1);
    sFail = 0;
    CHECK(load() == 3);
    contentUnload(&sSet);
    CHECK(count() == 0 && sNameHooks == 0 && sTextHooks == 0);
}

static void testNames(void)
{
    char out[40];

    sSet.t = &kGizmo;
    CHECK(contentOriginalId(&kGizmo, "dw3:gizmo/1") == 1 && contentOriginalId(&kGizmo, "dw3:gizmo/10") == 10);
    CHECK(contentOriginalId(&kGizmo, "dw3:gizmo/0") == -1 && contentOriginalId(&kGizmo, "dw3:gizmo/11") == -1);
    CHECK(contentOriginalId(&kGizmo, "dw3:item/3") == -1 && contentOriginalId(&kGizmo, "dw3:gizmo/3x") == -1);
    CHECK(contentNamedId("gizmo", "dw3:gizmo/0") == 0 && contentNamedId("gizmo", "dw3:gizmo/400") == 400);
    CHECK(contentNamedId("gizmo", "dw3:item/3") == -1 && contentNamedId("gizmo", "dw3:gizmo/-3") == -1 && contentNamedId("gizmo", "dw3:gizmo/3x") == -1 && contentNamedId("gizmo", "dw3:gizmo/") == -1);
    CHECK(contentOriginalId(&kGizmo, "dw3:gizmo/") == -1 && contentOriginalId(&kGizmo, "dw3:gizmo/-3") == -1);
    CHECK(contentOriginalId(&kGizmo, "dw3:gizmo/+3") == -1 && contentOriginalId(&kGizmo, "zeta:b") == -1 && contentOriginalId(&kGizmo, NULL) == -1);
    contentOriginalName(&kGizmo, 7, out, sizeof out);
    CHECK(strcmp(out, "dw3:gizmo/7") == 0 && contentOriginalId(&kGizmo, out) == 7);
    CHECK(load() == 3);
    CHECK(contentResolve(&sSet, "dw3:gizmo/4") == 4 && contentResolve(&sSet, "zeta:a") == 101 && contentResolve(&sSet, "zeta:q") == -1);
    CHECK(contentResolve(&sSet, "dw3:gizmo/40") == -1);
    contentUnload(&sSet);
}

static void testBaseByName(void)
{
    putFile("mods/zeta/content/gizmos.json",
            "{\"format\":\"gizmos-v1\",\"new\":{"
            "\"by-name\":{\"base\":\"dw3:gizmo/6\",\"name\":{\"eng\":\"N\"}},"
            "\"wrong-kind\":{\"base\":\"dw3:item/6\",\"name\":{\"eng\":\"W\"}},"
            "\"too-big\":{\"base\":\"dw3:gizmo/11\",\"name\":{\"eng\":\"T\"}},"
            "\"by-number\":{\"base\":7,\"name\":{\"eng\":\"M\"}}}}");
    putFile("mods/alpha/content/gizmos.json", "{\"format\":\"gizmos-v1\"}");
    CHECK(load() == 2 && idOf("zeta:by-name") == 100 && idOf("zeta:by-number") == 101);
    CHECK(((Gizmo *)contentRec(&sSet, 0))->v == 60 && ((Gizmo *)contentRec(&sSet, 1))->v == 70);
    contentUnload(&sSet);
}

static int describeGizmo(int id, char *out, size_t cap)
{
    if (id == 5) return 0;
    snprintf(out, cap, "\"v\": %d", 10 * id);
    return 1;
}

static const char *engGizmo(int id)
{
    return id == 2 ? "Two \"quoted\" \\ name\n" : id == 3 ? NULL : "Gizmo";
}

static void testReference(void)
{
    ContentType t = kGizmo;
    char *ref;

    t.refNote = "v (0..99)";
    t.describe = describeGizmo;
    ref = contentReferenceText(&t, engGizmo);
    CHECK(ref != NULL);
    if (ref) {
        CHECK(strstr(ref, "\"format\": \"content-ref-v1\"") != NULL && strstr(ref, "\"kind\": \"gizmo\"") != NULL);
        CHECK(strstr(ref, "\"fields\": \"v (0..99)\"") != NULL && strstr(ref, "dw3:gizmo/<id>") != NULL);
        CHECK(strstr(ref, "\"dw3:gizmo/1\": { \"id\": 1, \"eng\": \"Gizmo\", \"v\": 10 }") != NULL);
        CHECK(strstr(ref, "\"dw3:gizmo/2\": { \"id\": 2, \"eng\": \"Two \\\"quoted\\\" \\\\ name\\u000a\", \"v\": 20 }") != NULL);
        CHECK(strstr(ref, "\"dw3:gizmo/3\": { \"id\": 3, \"v\": 30 }") != NULL); /* no English name: left out */
        CHECK(strstr(ref, "dw3:gizmo/5") == NULL && strstr(ref, "\"dw3:gizmo/10\"") != NULL && strstr(ref, "dw3:gizmo/11") == NULL);
        CHECK(strstr(ref, "dw3:gizmo/0\"") == NULL);
        free(ref);
    }
    /* without describe or notes the entries are just names, and the file parses as the mods' JSON */
    t.refNote = NULL;
    t.describe = NULL;
    ref = contentReferenceText(&t, NULL);
    CHECK(ref != NULL);
    if (ref) {
        ExtJson j;
        const ExtJson *entries;
        CHECK(extJsonParse((const uint8_t *)ref, strlen(ref), &j) == 0);
        entries = extJsonGet(&j, "entries");
        CHECK(entries != NULL && entries->n == 10 && strstr(ref, "\"fields\"") == NULL);
        extJsonFree(&j);
        free(ref);
    }
}

/* the JSON reader keeps lists (t 4: the elements in v) and a note with quotes stays valid JSON in the reference */
static void testJsonLists(void)
{
    static const char doc[] = "{\"a\":[1,\"x\",{\"k\":[true,[]]}],\"e\":[],\"s\":\"q\"}";
    static const char bad[] = "{\"a\":[1,2}";
    static const char bad2[] = "{\"a\":[1 2]}";
    ExtJson j;
    const ExtJson *a, *e, *o;
    ContentType t;
    char *ref;

    CHECK(extJsonParse((const uint8_t *)doc, strlen(doc), &j) == 0);
    a = extJsonGet(&j, "a");
    e = extJsonGet(&j, "e");
    CHECK(a != NULL && a->t == 4 && a->n == 3 && a->keys == NULL && a->v[0].t == 0 && a->v[1].t == 1 && !strcmp(a->v[1].str, "x"));
    o = a != NULL && a->n == 3 ? &a->v[2] : NULL;
    CHECK(o != NULL && o->t == 2 && extJsonGet(o, "k") != NULL && extJsonGet(o, "k")->t == 4 && extJsonGet(o, "k")->n == 2);
    CHECK(e != NULL && e->t == 4 && e->n == 0 && extJsonGet(&j, "s") != NULL);
    extJsonFree(&j); /* the lists free their elements */
    CHECK(extJsonParse((const uint8_t *)bad, strlen(bad), &j) == -1);
    CHECK(extJsonParse((const uint8_t *)bad2, strlen(bad2), &j) == -1);

    memset(&t, 0, sizeof t);
    t.kind = "digimon";
    t.noun = "Digimon";
    t.refNote = "a \"quoted\" note";
    ref = contentReferenceText(&t, NULL);
    CHECK(ref != NULL);
    if (ref) {
        CHECK(extJsonParse((const uint8_t *)ref, strlen(ref), &j) == 0);
        extJsonFree(&j);
        free(ref);
    }
}

static void testChunk(void)
{
    unsigned char buf[64], tail[4] = {0, 0, 0, 0};
    unsigned long count = 0, n;
    char name[8];

    contentPut32(buf, 1);
    contentPut32(buf + 4, 0x01020304);
    CHECK(buf[0] == 1 && buf[1] == 0 && buf[4] == 4 && buf[7] == 1 && contentGet32(buf + 4) == 0x01020304);
    CHECK(contentChunkOpen(buf, 8, &count) == 0 && count == 0x01020304);
    CHECK(contentChunkOpen(buf, 7, &count) == -1);
    contentPut32(buf, 2);
    CHECK(contentChunkOpen(buf, 8, &count) == -1);

    CHECK(contentWriteName(buf, "ab:cd", 5) == 7 && buf[0] == 5 && buf[1] == 0 && memcmp(buf + 2, "ab:cd", 5) == 0);
    memcpy(buf + 7, tail, 2);
    CHECK(contentReadName(buf, 0, 9, 2, name, sizeof name) == 5 && strcmp(name, "ab:cd") == 0);
    CHECK(contentReadName(buf, 0, 8, 2, name, sizeof name) == 0); /* the tail is cut */
    CHECK(contentReadName(buf, 0, 7, 0, name, sizeof name) == 5);
    CHECK(contentReadName(buf, 0, 6, 0, name, sizeof name) == 0); /* the name is cut */
    CHECK(contentReadName(buf, 0, 1, 0, name, sizeof name) == 0); /* even the length is cut */
    CHECK(contentReadName(buf, 0, 9, 0, name, 5) == 0);           /* longer than the buffer (NUL included) */
    contentPut16(buf, 0);
    CHECK(contentReadName(buf, 0, 9, 0, name, sizeof name) == 0);  /* an empty name is damaged */
    n = contentWriteName(buf, "x", 1);
    CHECK(n == 3 && contentGet16(buf) == 1);
}

int main(void)
{
    char tmpl[] = "/tmp/contenttestXXXXXX";
    char cmd[700];

    if (!mkdtemp(tmpl)) return 2;
    snprintf(gRoot, sizeof gRoot, "%s", tmpl);
    testLoad();
    testNames();
    testBaseByName();
    testReference();
    testJsonLists();
    testChunk();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
    if (system(cmd)) {}
    if (failures) {
        printf("contenttest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("contenttest: ok\n");
    return 0;
}
