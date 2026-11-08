#define _GNU_SOURCE 1
/*
 * Host test of the stage code vocabulary and the stage data model (port-close S1/S2): pc/shim/stage_codes.c and stage_data.c.
 *   - every condition/action code round-trips (all 65535 codes x edge values), named and raw, with and without a registry;
 *   - event scripts round-trip (every first word, every known form, unknown subs and unknown end words);
 *   - bad input is refused with a reason;
 *   - a synthetic stage -> JSON -> model is equal, and the reference export obeys the manifest rule (write, keep, .new, version skip).
 * No game, no disc, no retail bytes. Scratch folder under $TMPDIR.
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim/stage_data.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "stagedatatest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- a registry with two mod entries and some SPECIAL_CONDITIONS rows ---- */

static int nameOf(void *ud, const char *kind, int id, char *out, size_t cap) {
    (void)ud;
    if (!strcmp(kind, "item") && id == 0x1F0) {
        snprintf(out, cap, "mymod:sword");
        return 1;
    }
    if (id > 0x1E0) return 0; /* a registry without a name for it: a number */
    snprintf(out, cap, "dw3:%s/%d", kind, id);
    return 1;
}

static int resolve(void *ud, const char *kind, const char *name) {
    (void)ud;
    return (!strcmp(kind, "item") && !strcmp(name, "mymod:sword")) ? 0x1F0 : -1;
}

static int specialRow(void *ud, int id, int *check, int *arg) {
    (void)ud;
    if (id == 0x2D) {
        *check = 0x16;
        *arg = 7;
        return 1;
    }
    if (id == 0x74) {
        *check = 0x20;
        *arg = 0;
        return 1;
    }
    return 0;
}

static const StageCodeEnv ENV = {nameOf, resolve, specialRow, NULL};

/* ---- lists ---- */

/* words -> JSON text -> words; 1 when identical */
static int roundTrip(const StageCodeEnv *env, const uint16_t *w, int n, char *textOut, size_t cap) {
    ScBuf B = {0};
    char *doc;
    ExtJson root;
    uint16_t *back = NULL;
    int bn = 0, ok = 0;
    char err[200];

    if (scListToJson(env, w, n, &B)) {
        scFree(&B);
        return 0;
    }
    if (textOut) snprintf(textOut, cap, "%s", B.b);
    doc = (char *)malloc(B.n + 16);
    sprintf(doc, "{\"l\": %s}", B.b);
    scFree(&B);
    if (extJsonParse((const uint8_t *)doc, strlen(doc), &root) == 0) {
        if (scListFromJson(env, extJsonGet(&root, "l"), &back, &bn, err, sizeof err) == 0) {
            ok = bn == n && (n == 0 || memcmp(back, w, (size_t)n * sizeof *w) == 0);
            if (!ok) fprintf(stdout, "stagedatatest: mismatch %s\n", doc);
        } else {
            fprintf(stdout, "stagedatatest: compile failed (%s) for %s\n", err, doc);
        }
        free(back);
        extJsonFree(&root);
    }
    free(doc);
    return ok;
}

/* the text of one pair */
static void pairText(const StageCodeEnv *env, unsigned code, unsigned value, char *out, size_t cap) {
    uint16_t w[2] = {(uint16_t)code, (uint16_t)value};

    out[0] = 0;
    roundTrip(env, w, 2, out, cap);
}

static void testCodes(void) {
    static const unsigned values[] = {0, 1, 2, 0x7FFF, 0xFFFF};
    uint16_t *w = (uint16_t *)malloc(2 * 4096 * sizeof *w);
    unsigned code, v;
    int n = 0, bad = 0, withEnv;
    long pairs = 0;

    /* every code but the terminator, with edge values, in chunks, with and without a registry */
    for (withEnv = 0; withEnv < 2; withEnv++) {
        n = 0;
        for (code = 0; code < 0xFFFF; code++) {
            for (v = 0; v < sizeof values / sizeof values[0]; v++) {
                w[n++] = (uint16_t)code;
                w[n++] = (uint16_t)values[v];
                if (n == 2 * 4096) {
                    if (!roundTrip(withEnv ? &ENV : NULL, w, n, NULL, 0)) bad++;
                    pairs += n / 2;
                    n = 0;
                }
            }
        }
        if (n && !roundTrip(withEnv ? &ENV : NULL, w, n, NULL, 0)) bad++;
        pairs += n / 2;
    }
    CHECK(bad == 0);
    CHECK(pairs == 2L * 0xFFFF * 5);
    free(w);
    CHECK(roundTrip(NULL, NULL, 0, NULL, 0)); /* an empty list */

    {
        char t[200];

        /* the names the docs promise */
        pairText(NULL, 0x1A1F, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"flag\", \"1A:1F\", 1]]"));
        pairText(&ENV, 0x1BFF, 0, t, sizeof t); /* flag 0x1FF of group 0x1A */
        CHECK(!strcmp(t, "[[\"flag\", \"1A:1FF\", 0]]"));
        pairText(NULL, 0x6027, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"progress\", 39, 1]]"));
        pairText(&ENV, 0x702D, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"special\", \"0x2D\", 1, \"partner.locked(7)\"]]"));
        pairText(&ENV, 0x7074, 0, t, sizeof t);
        CHECK(!strcmp(t, "[[\"special\", \"0x74\", 0, \"money.has(0)\"]]"));
        pairText(NULL, 0x8012, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"item\", \"dw3:item/18\", 1]]"));
        pairText(NULL, 0x8A12, 1, t, sizeof t); /* item class 5 */
        CHECK(!strcmp(t, "[[\"item\", \"dw3:item/18\", 1, 5]]"));
        pairText(&ENV, 0x81F0, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"item\", \"mymod:sword\", 1]]"));
        pairText(&ENV, 0x81EF, 1, t, sizeof t); /* no name: a number */
        CHECK(!strcmp(t, "[[\"item\", 495, 1]]"));
        pairText(NULL, 0x7400, 0, t, sizeof t);
        CHECK(!strcmp(t, "[[\"event-battle\", 0, 0]]"));
        pairText(NULL, 0x7A00 | 2, 0, t, sizeof t);
        CHECK(!strcmp(t, "[[\"shop\", 2, 0]]"));
        pairText(NULL, 0x7A00 | 0x31, 0, t, sizeof t);
        CHECK(!strcmp(t, "[[\"card-shop\", 49, 0]]"));
        pairText(NULL, 0x7A00 | 0x44, 0, t, sizeof t);
        CHECK(!strcmp(t, "[[\"inn\", 68, 0]]"));
        pairText(NULL, 0x7800 | 5, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"card-battle\", \"5:1\", 1]]"));
        pairText(NULL, 0x7E00 | 3, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"place\", 4, 1]]"));
        pairText(NULL, 0x7E00 | 31, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"place-arg\", 2, 1]]"));
        pairText(NULL, 0x9205, 1, t, sizeof t);
        CHECK(!strcmp(t, "[[\"card\", \"dw3:card/5\", 1]]"));
        pairText(NULL, 0x7C01, 0, t, sizeof t);
        CHECK(!strcmp(t, "[[\"naming\", 0]]"));
        pairText(NULL, 0x7C05, 0, t, sizeof t);
        CHECK(!strcmp(t, "[[\"raw\", \"0x7C05\", 0]]"));
        pairText(NULL, 0x1200, 3, t, sizeof t); /* a group the interpreter does not know */
        CHECK(!strcmp(t, "[[\"raw\", \"0x1200\", 3]]"));
    }
}

/* a compile of `json` (a list) must fail with a reason that contains `why` */
static void refuses(const char *json, const char *why) {
    char doc[600], err[200] = "";
    ExtJson root;
    uint16_t *w = NULL;
    int n = 0, rc = 0;

    snprintf(doc, sizeof doc, "{\"l\": %s}", json);
    if (extJsonParse((const uint8_t *)doc, strlen(doc), &root)) {
        CHECK(0);
        return;
    }
    rc = scListFromJson(&ENV, extJsonGet(&root, "l"), &w, &n, err, sizeof err);
    if (rc == 0 || !strstr(err, why)) fprintf(stdout, "stagedatatest: %s -> rc %d \"%s\" (wanted \"%s\")\n", json, rc, err, why);
    CHECK(rc == -1 && strstr(err, why) != NULL);
    free(w);
    extJsonFree(&root);
}

static void testRefused(void) {
    refuses("[[\"flurb\", 1, 1]]", "unknown name");
    refuses("[[\"flag\", \"1A\", 1]]", "GG:III");
    refuses("[[\"flag\", \"13:01\", 1]]", "GG:III"); /* 0x13 is not a flag group */
    refuses("[[\"flag\", \"1A:1F\", 70000]]", "value");
    refuses("[[\"flag\", \"1A:1F\"]]", "needs 3");
    refuses("[[\"progress\", 600, 1]]", "0 to 511");
    refuses("[[\"item\", \"dw3:item/9999\", 1]]", "unknown name");
    refuses("[[\"item\", \"other:thing\", 1]]", "unknown name");
    refuses("[[\"item\", 5, 1, 9]]", "item class");
    refuses("[[\"shop\", 40, 1]]", "not one of that kind");
    refuses("[[\"card-shop\", 3, 1]]", "not one of that kind");
    refuses("[[\"place\", 0, 1]]", "from 1 to 30");
    refuses("[[\"card-battle\", \"5:2\", 1]]", "opponent:variant");
    refuses("[[\"raw\", \"0xFFFF\", 1]]", "0xFFFF");
    refuses("[\"flag\"]", "not [\"name\"");
    refuses("{}", "must be an array");
}

/* ---- scripts ---- */

static int scriptRoundTrip(const int16_t *w, int avail, const char *wantText) {
    ScBuf B = {0};
    int n = scScriptLength(w, avail), ok = 0;
    char *doc, err[200];
    ExtJson root;
    int16_t *back = NULL;
    int bn = 0;

    if (n <= 0 || scScriptToJson(w, n, &B)) {
        scFree(&B);
        return 0;
    }
    if (wantText && strcmp(B.b, wantText)) {
        fprintf(stdout, "stagedatatest: script text %s (wanted %s)\n", B.b, wantText);
        scFree(&B);
        return 0;
    }
    doc = (char *)malloc(B.n + 16);
    sprintf(doc, "{\"l\": %s}", B.b);
    if (extJsonParse((const uint8_t *)doc, strlen(doc), &root) == 0) {
        if (scScriptFromJson(extJsonGet(&root, "l"), &back, &bn, err, sizeof err) == 0) {
            ok = bn == n && memcmp(back, w, (size_t)n * sizeof *w) == 0;
            if (!ok) fprintf(stdout, "stagedatatest: script mismatch %s\n", doc);
        } else {
            fprintf(stdout, "stagedatatest: script compile failed (%s) for %s\n", err, doc);
        }
        free(back);
        extJsonFree(&root);
    }
    free(doc);
    scFree(&B);
    return ok;
}

static void testScripts(void) {
    /* walk 2 to (191, 400) facing 3, wait, a talk, a command, a pose, a leave, the end */
    static const int16_t script[] = {0x102, 2, 191, 400, 3, 0x300, 60, 0x200, 0, 7, 2, 1, 0x101, 2, 1, 6, 0x101, 0x348, 0x349, 1, 0x304, 0x2D8, 5, 6, 0,
                                     0x301, 0x600, 0, 2, 0x601, 1, -5, 7, 0x100, 63, 176, 392, 0};
    static const int16_t unknownSub[] = {0x105, 9, 1, 2, 0x303, 2, 0x0400}; /* kind 1 sub 5 runs as a place; 0x0400 is no command */
    static const int16_t truncated[] = {0x102, 1, 2};
    int bad = 0, w, k;
    int16_t buf[8];

    CHECK(scScriptLength(script, -1) == (int)(sizeof script / sizeof script[0]));
    CHECK(scriptRoundTrip(script, -1, NULL));
    CHECK(scriptRoundTrip(unknownSub, -1, "[[\"raw\", \"0x0105\", 9, 1, 2], [\"wait-anim\", 2], [\"end\", \"0x0400\"]]"));
    CHECK(scScriptLength(truncated, 3) == 3); /* cut off: what is there */
    {
        static const int16_t one[] = {0x200, 1, 2, 3, 4, 0};
        CHECK(scriptRoundTrip(one, -1, "[[\"talk\", 1, 2, 3, 4], [\"end\"]]"));
    }
    {
        static const int16_t c[] = {0x101, 800, 0x348, 1, 0};
        CHECK(scriptRoundTrip(c, -1, "[[\"command\", 800, 840, 1], [\"end\"]]"));
    }
    /* every first word, every sub, with odd arguments: a script of one command and its end */
    for (w = 0; w < 0x10000; w++) {
        int16_t s[12];
        int n;

        s[0] = (int16_t)w;
        for (k = 1; k < 11; k++) s[k] = (int16_t)(w * 7 + k * 0x1235 - 0x4000);
        s[11] = 0;
        n = scScriptLength(s, -1);
        /* the commands read their argument counts; the end word is the one after them */
        {
            int kind = (w >> 8) & 0xFF;

            if (kind == 1 || kind == 2 || kind == 3 || kind == 6) {
                int len = (kind == 1 && (w & 0xFF) == 2) ? 5 : kind == 1 ? 4 : kind == 2 ? 5 : kind == 3 ? ((w & 0xFF) == 1 ? 1 : (w & 0xFF) == 4 ? 5 : 2) : ((w & 0xFF) == 1 ? 4 : 3);
                int16_t t[16];
                int m;

                for (m = 0; m < len; m++) t[m] = s[m];
                t[len] = 0;
                if (!scriptRoundTrip(t, -1, NULL)) bad++;
            } else {
                if (n != 1) bad++;
                if (!scriptRoundTrip(s, -1, NULL)) bad++;
            }
        }
    }
    CHECK(bad == 0);
    (void)buf;

    {
        char err[200] = "";
        ExtJson root;
        int16_t *o = NULL;
        int n = 0;
        const char *doc = "{\"l\": [[\"walk\", 1, 2, 3], [\"end\"]]}";

        CHECK(extJsonParse((const uint8_t *)doc, strlen(doc), &root) == 0);
        CHECK(scScriptFromJson(extJsonGet(&root, "l"), &o, &n, err, sizeof err) == -1 && strstr(err, "takes 4 arguments"));
        extJsonFree(&root);
        doc = "{\"l\": [[\"wait\", 5]]}";
        CHECK(extJsonParse((const uint8_t *)doc, strlen(doc), &root) == 0);
        CHECK(scScriptFromJson(extJsonGet(&root, "l"), &o, &n, err, sizeof err) == -1 && strstr(err, "does not end"));
        extJsonFree(&root);
        doc = "{\"l\": [[\"end\"], [\"wait\", 5]]}";
        CHECK(extJsonParse((const uint8_t *)doc, strlen(doc), &root) == 0);
        CHECK(scScriptFromJson(extJsonGet(&root, "l"), &o, &n, err, sizeof err) == -1 && strstr(err, "must be the last"));
        extJsonFree(&root);
        /* the "item received" signal of a script: SCRIPT_COMMAND(0x32D, 0x34A, 2) (wstag741.c:217), 813 = the command task, 842 = the icon's substate 3 */
        doc = "{\"l\": [[\"command\", 813, 842, 2], [\"end\"]]}";
        CHECK(extJsonParse((const uint8_t *)doc, strlen(doc), &root) == 0);
        CHECK(scScriptFromJson(extJsonGet(&root, "l"), &o, &n, err, sizeof err) == 0 && n >= 5 && o[0] == 0x101 && o[1] == 0x32D && o[2] == 0x34A && o[3] == 2);
        CHECK(scriptRoundTrip(o, n, "[[\"command\", 813, 842, 2], [\"end\"]]"));
        free(o);
        extJsonFree(&root);
    }
}

/* ---- the model ---- */

static uint16_t *words(int n, const unsigned *v) {
    uint16_t *w = (uint16_t *)malloc((size_t)n * sizeof *w);
    int i;

    for (i = 0; i < n; i++) w[i] = (uint16_t)v[i];
    return w;
}

static void setList(SdList *l, int n, const unsigned *v) {
    l->w = n ? words(n, v) : NULL;
    l->n = n;
    l->set = 1;
}

static char *dup(const char *s) {
    char *c = (char *)malloc(strlen(s) + 1);

    strcpy(c, s);
    return c;
}

static void makeStage(SdStage *s) {
    static const unsigned c1[] = {0x1A2C, 0, 0x8012, 1};
    static const unsigned a1[] = {0x1A2C, 1, 0x7000 | 0x13, 1, 0x9020, 1};
    static const unsigned c2[] = {0x1A2C, 1};
    SdActor *a;

    memset(s, 0, sizeof *s);
    s->number = 380;
    snprintf(s->name, sizeof s->name, "WSTAG380");
    s->modes[0] = 512;
    s->nmodes = 1;
    s->textFile = 239;
    s->mapFile = 0x2BA;
    s->sheetEntry = 0x2BB0000;
    s->imageFile = 0x32F;
    s->soundBank = 9;
    s->music = 0x60000000 | (9 << 18);
    s->startX = 0x13D00;
    s->startY = 0x34000;
    s->startDir = 3;
    s->spriteColor[0] = 0x54;
    s->spriteColor[1] = 0x67;
    s->spriteColor[2] = 0x96;
    s->nobjects = 12;
    s->images = (SdImage *)calloc(2, sizeof *s->images);
    s->nimages = 2;
    s->images[0] = (SdImage){0x200, 0x100, 0x216, 0x138, 0x58, 0x38, 0x200, 0x1FD};
    s->images[1] = (SdImage){0x200, 0x100, 0x208, 0x1BC, 0x20, 0xBC, 0x210, 0x1FD};
    s->actors = (SdActor *)calloc(2, sizeof *s->actors);
    s->nactors = 2;
    a = &s->actors[0];
    a->character = 2;
    a->image = 2;
    a->x = 100;
    a->y = 200;
    a->dir = 5;
    a = &s->actors[1];
    a->character = 0x2A;
    a->image = 3;
    a->x = -4;
    a->y = 300;
    a->dir = 1;
    setList(&a->cond, 2, c2);
    a->talks = (SdTalk *)calloc(3, sizeof *a->talks);
    a->ntalks = 3;
    setList(&a->talks[0].cond, 4, c1);
    setList(&a->talks[0].actions, 6, a1);
    a->talks[0].entry = 4;
    a->talks[0].preview = dup("Hello \"friend\"!\nSecond line \\ end");
    setList(&a->talks[1].cond, 0, NULL); /* set but empty: always holds, but not the last */
    a->talks[1].entry = 5;
    a->talks[2].entry = 6; /* the last: no conditions */
    s->slots = (SdSlot *)calloc(2, sizeof *s->slots);
    s->nslots = 2;
    s->slots[0] = (SdSlot){{NULL, 0, 0}, 1, 0x201, 0x38, 0x58, 0, 0, 3, 4};
    setList(&s->slots[1].cond, 4, (const unsigned[]){0x8005, 1, 0xFFF0, 0});
    s->slots[1].type = 7;
    s->events = (SdEvent *)calloc(3, sizeof *s->events);
    s->nevents = 3;
    s->events[0].id = 205;
    s->events[0].hasText = 1;
    s->events[0].textFile = 0x127;
    s->events[0].textEntry = 3;
    s->events[0].hasScript = 1;
    s->events[0].script = (int16_t *)malloc(6 * sizeof(int16_t));
    memcpy(s->events[0].script, (int16_t[]){0x200, 0, 3, 2, 1, 0}, 6 * sizeof(int16_t));
    s->events[0].nscript = 6; /* a talk and its end word 0 */
    s->events[0].previews = (SdPreview *)calloc(1, sizeof(SdPreview));
    s->events[0].previews[0].entry = 3;
    s->events[0].previews[0].text = dup("A line");
    s->events[0].npreviews = 1;
    s->events[1].id = 9000;
    s->events[1].hasStart = 1;
    s->events[1].hasEnd = 1;
    s->events[2].id = 1409;
    s->functions = NULL;
    sdStageAddFunction(s, "endEvent205");
    sdStageAddFunction(s, "updateStage");
    sdStageAddNote(s, "has 2 functions in C");
    sdStageAddNote(s, "setup varies with progress: music");
}

static void testModel(void) {
    SdStage a, b, c;
    char *json, *json2, err[300] = "";

    makeStage(&a);
    json = sdStageToJson(&ENV, &a);
    CHECK(json != NULL);
    CHECK(sdStageFromJson(&ENV, json, strlen(json), &b, err, sizeof err) == 0);
    if (err[0]) fprintf(stdout, "stagedatatest: %s\n", err);
    CHECK(sdStageEqual(&a, &b));
    json2 = sdStageToJson(&ENV, &b);
    CHECK(json2 && !strcmp(json, json2)); /* the file is stable */
    CHECK(strstr(json, "\"format\": \"stage-v1\"") != NULL);
    CHECK(strstr(json, "\"name\": \"player\"") != NULL);
    CHECK(strstr(json, "\"type\": \"exit\"") != NULL);
    CHECK(strstr(json, "\"type\": \"gauge\"") != NULL);
    free(json2);

    /* a change is seen */
    makeStage(&c);
    c.actors[1].talks[0].cond.w[3] = 0;
    CHECK(!sdStageEqual(&a, &c));
    sdStageFree(&c);
    makeStage(&c);
    c.events[0].script[3] = 9;
    CHECK(!sdStageEqual(&a, &c));
    sdStageFree(&c);
    makeStage(&c);
    c.actors[1].talks[1].cond.set = 0; /* empty-set is not the same as not-set */
    CHECK(!sdStageEqual(&a, &c));
    sdStageFree(&c);

    /* bad documents are refused, with the place */
    {
        const char *bad = "{\"format\": \"stage-v1\", \"stage\": 1, \"actors\": [{\"character\": 2, \"talks\": [{\"entry\": 1, \"conditions\": [[\"flurb\", 1, 1]]}]}]}";

        err[0] = 0;
        CHECK(sdStageFromJson(&ENV, bad, strlen(bad), &c, err, sizeof err) == -1);
        CHECK(strstr(err, "actor 0 talk 0") && strstr(err, "unknown name"));
        err[0] = 0;
        CHECK(sdStageFromJson(&ENV, "{\"format\": \"stage-v2\"}", 22, &c, err, sizeof err) == -1 && strstr(err, "format"));
        CHECK(sdStageFromJson(&ENV, "[1]", 3, &c, err, sizeof err) == -1);
    }
    free(json);
    sdStageFree(&a);
    sdStageFree(&b);
}

/* ---- the export ---- */

static char gRoot[600];

static int readAll(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f) return 0;
    n = fread(out, 1, cap - 1, f);
    out[n] = 0;
    fclose(f);
    return 1;
}

static void writeFile(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");

    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void testExport(void) {
    SdExportStats st;
    char path[800], buf[200000];
    int nums[2] = {380, 381};
    SdStage s;
    char *json;

    makeStage(&s);
    json = sdStageToJson(&ENV, &s);

    memset(&st, 0, sizeof st);
    {
        /* a manifest of the old layout (stages/<nnn>.json): its entries are dropped, the rest is kept */
        TdManifest old;
        char *text, dir[800];

        memset(&old, 0, sizeof old);
        snprintf(old.topic, sizeof old.topic, "stages");
        CHECK(tdKvSet(&old.exporter, "stage-objects", "1") == 0);
        CHECK(tdManifestSet(&old, "380.json", "0123456789abcdef") == 0 && tdManifestSet(&old, "s380/objects/f000p00.png", "0123456789abcdef") == 0);
        text = tdManifestFormat(&old);
        snprintf(dir, sizeof dir, "%s/stages", gRoot);
        snprintf(path, sizeof path, "%s/manifest.json", dir);
        CHECK(text != NULL && tdMakeDirs(dir) == 0 && tdWriteFileAtomic(path, text, strlen(text)) == 0);
        free(text);
        tdManifestFree(&old);
    }
    sdExportSetup(gRoot);
    CHECK(!sdExportUpToDate(nums, 2));
    CHECK(sdExportPut(380, json, &st) == 0 && st.written == 1);
    CHECK(sdExportPut(381, json, &st) == 0 && st.written == 2);
    sdExportEnd(1);
    snprintf(path, sizeof path, "%s/stages/s380/stage.json", gRoot);
    CHECK(readAll(path, buf, sizeof buf) && !strcmp(buf, json));
    snprintf(path, sizeof path, "%s/stages/manifest.json", gRoot);
    CHECK(readAll(path, buf, sizeof buf) && strstr(buf, "s380/stage.json") && strstr(buf, "\"topic\": \"stages\""));
    CHECK(strstr(buf, "\"380.json\"") == NULL && strstr(buf, "s380/objects/f000p00.png") && strstr(buf, "\"stage-objects\"")); /* the old entry went, the objects' stayed */

    /* a second run: up to date, so nothing to build; and if it ran, every file is kept */
    sdExportSetup(gRoot);
    CHECK(sdExportUpToDate(nums, 2));
    memset(&st, 0, sizeof st);
    CHECK(sdExportPut(380, json, &st) == 0 && st.kept == 1 && st.written == 0);
    sdExportEnd(1);

    /* the player edits a file: the new version goes beside it and the edit stays */
    snprintf(path, sizeof path, "%s/stages/s380/stage.json", gRoot);
    writeFile(path, "{\"edited\": true}\n");
    sdExportSetup(gRoot);
    memset(&st, 0, sizeof st);
    s.music = 1;
    free(json);
    json = sdStageToJson(&ENV, &s);
    CHECK(sdExportPut(380, json, &st) == 0 && st.newFiles == 1);
    sdExportEnd(1);
    CHECK(readAll(path, buf, sizeof buf) && strstr(buf, "edited"));
    snprintf(path, sizeof path, "%s/stages/s380/stage.json.new", gRoot);
    CHECK(readAll(path, buf, sizeof buf) && !strcmp(buf, json));

    /* a missing file is not up to date */
    snprintf(path, sizeof path, "%s/stages/s381/stage.json", gRoot);
    remove(path);
    sdExportSetup(gRoot);
    CHECK(!sdExportUpToDate(nums, 2));
    sdExportSetup(NULL);
    CHECK(!sdExportUpToDate(nums, 2));
    memset(&st, 0, sizeof st);
    CHECK(sdExportPut(380, json, &st) == -1);
    free(json);
    sdStageFree(&s);
}

int main(void) {
    char cmd[700];

    snprintf(gRoot, sizeof gRoot, "%s/stagedatatest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(gRoot)) {
        fprintf(stdout, "stagedatatest: no scratch folder\n");
        return 2;
    }
    testCodes();
    testRefused();
    testScripts();
    testModel();
    testExport();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
    if (system(cmd)) {}
    if (failures) {
        fprintf(stdout, "stagedatatest: %d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stdout, "stagedatatest: all checks held\n");
    return 0;
}
