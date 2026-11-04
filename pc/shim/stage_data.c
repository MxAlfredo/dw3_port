/*
 * The stage data model, the stage-v1 JSON and the reference export: see stage_data.h and docs/stages.md.
 */
#include "stage_data.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_log.h"
#include "text_data.h"

/* ---- memory ---- */

static void listFree(SdList *l) {
    free(l->w);
    memset(l, 0, sizeof *l);
}

void sdActorFree(SdActor *a) {
    int j;

    listFree(&a->cond);
    for (j = 0; j < a->ntalks; j++) {
        listFree(&a->talks[j].cond);
        listFree(&a->talks[j].actions);
        free(a->talks[j].preview);
    }
    free(a->talks);
    memset(a, 0, sizeof *a);
}

void sdStageFree(SdStage *s) {
    int i, j;

    for (i = 0; i < s->nactors; i++) sdActorFree(&s->actors[i]);
    free(s->actors);
    for (i = 0; i < s->nslots; i++) listFree(&s->slots[i].cond);
    free(s->slots);
    for (i = 0; i < s->nevents; i++) {
        free(s->events[i].script);
        for (j = 0; j < s->events[i].npreviews; j++) free(s->events[i].previews[j].text);
        free(s->events[i].previews);
    }
    free(s->events);
    free(s->images);
    free(s->extra);
    for (i = 0; i < s->nfunctions; i++) free(s->functions[i]);
    free(s->functions);
    for (i = 0; i < s->nnotes; i++) free(s->notes[i]);
    free(s->notes);
    memset(s, 0, sizeof *s);
}

static int addString(char ***arr, int *n, const char *text) {
    char **grown = (char **)realloc(*arr, (size_t)(*n + 1) * sizeof **arr);
    char *copy = (char *)malloc(strlen(text) + 1);

    if (!grown || !copy) {
        free(copy);
        if (grown) *arr = grown;
        return -1;
    }
    strcpy(copy, text);
    grown[(*n)++] = copy;
    *arr = grown;
    return 0;
}

int sdStageAddNote(SdStage *s, const char *note) { return addString(&s->notes, &s->nnotes, note); }
int sdStageAddFunction(SdStage *s, const char *name) { return addString(&s->functions, &s->nfunctions, name); }

static char *dupStr(const char *s);

/* ---- building ---- */

#define ADD(arr, n, type) \
    do { \
        type *grown_ = (type *)realloc((arr), (size_t)((n) + 1) * sizeof(type)); \
        if (!grown_) return NULL; \
        (arr) = grown_; \
        memset(&grown_[(n)], 0, sizeof(type)); \
        return &grown_[(n)++]; \
    } while (0)

SdActor *sdStageAddActor(SdStage *s) { ADD(s->actors, s->nactors, SdActor); }
SdSlot *sdStageAddSlot(SdStage *s) { ADD(s->slots, s->nslots, SdSlot); }
SdEvent *sdStageAddEvent(SdStage *s) { ADD(s->events, s->nevents, SdEvent); }
SdTalk *sdActorAddTalk(SdActor *a) { ADD(a->talks, a->ntalks, SdTalk); }
SdImage *sdStageAddImage(SdStage *s) { ADD(s->images, s->nimages, SdImage); }

int sdListFromCodes(SdList *l, const uint16_t *src, int maxPairs) {
    int ended;

    listFree(l);
    if (!src) return 0;
    l->w = scListCopy(src, maxPairs, &l->n, &ended);
    l->set = 1;
    return (l->n > 0 && !l->w) ? -1 : 0;
}

int sdEventSetScript(SdEvent *e, const int16_t *script) {
    int n;

    free(e->script);
    e->script = NULL;
    e->nscript = 0;
    e->hasScript = 0;
    if (!script) return 0;
    n = scScriptLength(script, -1);
    e->script = (int16_t *)malloc((size_t)n * sizeof *e->script);
    if (!e->script) return -1;
    memcpy(e->script, script, (size_t)n * sizeof *e->script);
    e->nscript = n;
    e->hasScript = 1;
    return 0;
}

int sdEventAddPreview(SdEvent *e, int entry, const char *text) {
    SdPreview *p = (SdPreview *)realloc(e->previews, (size_t)(e->npreviews + 1) * sizeof *e->previews);

    if (!p) return -1;
    e->previews = p;
    p[e->npreviews].entry = entry;
    p[e->npreviews].text = dupStr(text);
    if (!p[e->npreviews].text) return -1;
    e->npreviews++;
    return 0;
}

int sdTalkSetPreview(SdTalk *t, const char *text) {
    free(t->preview);
    t->preview = text ? dupStr(text) : NULL;
    return (text && !t->preview) ? -1 : 0;
}

static void countList(const SdList *l, SdCounts *c) {
    if (!l->set) return;
    c->lists++;
    c->pairs += l->n / 2;
}

void sdStageCount(const SdStage *s, SdCounts *c) {
    int i, j;

    memset(c, 0, sizeof *c);
    for (i = 0; i < s->nactors; i++) {
        countList(&s->actors[i].cond, c);
        c->talks += s->actors[i].ntalks;
        for (j = 0; j < s->actors[i].ntalks; j++) {
            countList(&s->actors[i].talks[j].cond, c);
            countList(&s->actors[i].talks[j].actions, c);
        }
    }
    for (i = 0; i < s->nslots; i++) countList(&s->slots[i].cond, c);
    for (i = 0; i < s->nevents; i++) {
        if (s->events[i].hasScript) {
            c->scripts++;
            c->words += s->events[i].nscript;
        }
    }
}

/* ---- write ---- */

static const char *characterName(int id) {
    if (id == 2) return "player";
    if (id == 3) return "partner";
    return NULL;
}

static void putList(const StageCodeEnv *env, ScBuf *B, const char *key, const SdList *l) {
    if (!l->set) return;
    scPrintf(B, ", \"%s\": ", key);
    scListToJson(env, l->w, l->n, B);
}

static void putStr(ScBuf *B, const char *key, const char *s) {
    scPrintf(B, ", \"%s\": \"", key);
    scPutEsc(B, s);
    scPrintf(B, "\"");
}

static const char ABOUT[] =
    "Reference copy of one field stage, written by the port from the player's own disc (stage-v1, docs/stages.md). Editing this file does not "
    "change the game: a mod will patch a stage with its own file (same names, only what changes). Lists of codes are [name, reference, value] "
    "arrays: [\"flag\", \"1A:1F\", 1] is flag 0x1F of group 0x1A set, [\"item\", \"dw3:item/18\", 1] the player has item 18. Conditions hold "
    "when the whole list does; a talk without \"conditions\" is the last one and always holds. A script is a list of commands ending with "
    "[\"end\"]. x and y of characters are pixels, start is pixels * 256. \"preview\" lines are the text in the language the export ran in, only "
    "to read: nothing reads them back. \"notes\" lists what the stage does in C code that these data do not hold; the file is the stage as it "
    "is at story progress 0.";

char *sdStageToJson(const StageCodeEnv *env, const SdStage *s) {
    ScBuf B = {0};
    int i, j, k;

    scPrintf(&B, "{\n  \"format\": \"%s\",\n  \"about\": \"", SD_FORMAT);
    scPutEsc(&B, ABOUT);
    scPrintf(&B, "\",\n  \"notes\": [");
    for (i = 0; i < s->nnotes; i++) {
        scPrintf(&B, "%s\n    \"", i ? "," : "");
        scPutEsc(&B, s->notes[i]);
        scPrintf(&B, "\"");
    }
    scPrintf(&B, "%s],\n  \"stage\": %d,\n  \"name\": \"", s->nnotes ? "\n  " : "", s->number);
    scPutEsc(&B, s->name);
    scPrintf(&B, "\",\n  \"modes\": [");
    for (i = 0; i < s->nmodes; i++) scPrintf(&B, "%s%d", i ? ", " : "", s->modes[i]);
    scPrintf(&B, "],\n  \"textFile\": %d,\n", s->textFile);
    scPrintf(&B, "  \"mapFile\": %d, \"sheetEntry\": %d, \"imageEntry\": %d, \"imageFile\": %d,\n", s->mapFile, s->sheetEntry, s->imageEntry, s->imageFile);
    scPrintf(&B, "  \"soundBank\": %d, \"music\": %d,\n", s->soundBank, s->music);
    scPrintf(&B, "  \"start\": {\"x\": %d, \"y\": %d, \"dir\": %d},\n", s->startX, s->startY, s->startDir);
    scPrintf(&B, "  \"spriteColor\": [%d, %d, %d, %d],\n  \"objects\": %d,\n", s->spriteColor[0], s->spriteColor[1], s->spriteColor[2], s->spriteColor[3], s->nobjects);
    scPrintf(&B, "  \"images\": [");
    for (i = 0; i < s->nimages; i++) {
        const SdImage *m = &s->images[i];

        scPrintf(&B, "%s\n    {\"x\": %d, \"y\": %d, \"imageX\": %d, \"imageY\": %d, \"u\": %d, \"v\": %d, \"clutX\": %d, \"clutY\": %d}", i ? "," : "", m->x, m->y,
                 m->imageX, m->imageY, m->u, m->v, m->clutX, m->clutY);
    }
    scPrintf(&B, "%s],\n  \"actors\": [", s->nimages ? "\n  " : "");
    for (i = 0; i < s->nactors; i++) {
        const SdActor *a = &s->actors[i];
        const char *cn = characterName(a->character);

        scPrintf(&B, "%s\n    {\"index\": %d, \"character\": %d", i ? "," : "", i, a->character);
        if (cn) putStr(&B, "name", cn);
        scPrintf(&B, ", \"image\": %d, \"x\": %d, \"y\": %d, \"dir\": %d", a->image, a->x, a->y, a->dir);
        putList(env, &B, "conditions", &a->cond);
        scPrintf(&B, ", \"talks\": [");
        for (j = 0; j < a->ntalks; j++) {
            const SdTalk *t = &a->talks[j];

            scPrintf(&B, "%s\n      {\"entry\": %d", j ? "," : "", t->entry);
            putList(env, &B, "conditions", &t->cond);
            putList(env, &B, "actions", &t->actions);
            if (t->preview) putStr(&B, "preview", t->preview);
            scPrintf(&B, "}");
        }
        scPrintf(&B, "%s]}", a->ntalks ? "\n    " : "");
    }
    scPrintf(&B, "%s],\n  \"slots\": [", s->nactors ? "\n  " : "");
    for (i = 0; i < s->nslots; i++) {
        const SdSlot *t = &s->slots[i];
        const char *tn = scSlotTypeName(t->type);

        scPrintf(&B, "%s\n    {\"type\": ", i ? "," : "");
        if (tn) scPrintf(&B, "\"%s\"", tn);
        else scPrintf(&B, "%d", t->type);
        scPrintf(&B, ", \"arg\": %d, \"x\": %d, \"y\": %d, \"dir\": %d, \"hideAnim\": %d, \"place\": %d, \"placeArg\": %d", t->arg, t->x, t->y, t->dir,
                 t->hideAnim, t->place, t->placeArg);
        if (t->hasArea) {
            scPrintf(&B, ", \"area\": {\"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d", t->ax, t->ay, t->aw, t->ah);
            if (t->facing >= 0) scPrintf(&B, ", \"facing\": %d", t->facing);
            scPrintf(&B, "}");
        }
        putList(env, &B, "conditions", &t->cond);
        scPrintf(&B, "}");
    }
    scPrintf(&B, "%s],\n  \"events\": [", s->nslots ? "\n  " : "");
    for (i = 0; i < s->nevents; i++) {
        const SdEvent *e = &s->events[i];

        scPrintf(&B, "%s\n    {\"id\": %d", i ? "," : "", e->id);
        if (e->hasText) scPrintf(&B, ", \"text\": {\"file\": %d, \"entry\": %d}", e->textFile, e->textEntry);
        if (e->hasStart) scPrintf(&B, ", \"start\": true");
        if (e->hasEnd) scPrintf(&B, ", \"end\": true");
        if (e->hasScript) {
            scPrintf(&B, ", \"script\": ");
            scScriptToJson(e->script, e->nscript, &B);
        }
        if (e->npreviews) {
            scPrintf(&B, ", \"previews\": {");
            for (k = 0; k < e->npreviews; k++) {
                scPrintf(&B, "%s\"%d\": \"", k ? ", " : "", e->previews[k].entry);
                scPutEsc(&B, e->previews[k].text);
                scPrintf(&B, "\"");
            }
            scPrintf(&B, "}");
        }
        scPrintf(&B, "}");
    }
    scPrintf(&B, "%s]", s->nevents ? "\n  " : "");
    if (s->battles.present) {
        const SdBattles *bt = &s->battles;

        scPrintf(&B, ",\n  \"battles\": {\"place\": %d, \"serial\": %d, \"areas\": [", bt->place, bt->serial);
        for (i = 0; i < bt->nareas; i++) {
            scPrintf(&B, "%s\n    {\"area\": %d, \"rate\": %d, \"slots\": [", i ? "," : "", i + 1, bt->areas[i].rate);
            for (k = 0; k < 8; k++) {
                const SdAreaBattle *x = &bt->areas[i].b[k];

                scPrintf(&B, "%s{\"encounter\": %d, \"stage\": %d, \"music\": %d}", k ? ", " : "", x->encounter, x->stage, x->music);
            }
            scPrintf(&B, "]}");
        }
        scPrintf(&B, "\n  ]}");
    }
    scPrintf(&B, ",\n  \"functions\": [");
    for (i = 0; i < s->nfunctions; i++) {
        scPrintf(&B, "%s\"", i ? ", " : "");
        scPutEsc(&B, s->functions[i]);
        scPrintf(&B, "\"");
    }
    scPrintf(&B, "]\n}\n");
    if (B.bad) {
        scFree(&B);
        return NULL;
    }
    return B.b;
}

/* ---- read ---- */

static void fail(char *err, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void fail(char *err, size_t cap, const char *fmt, ...) {
    va_list ap;

    if (!err || !cap) return;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

/* an integer member in lo..hi, or `def` when absent; 0 when present and wrong */
static int getInt(const ExtJson *o, const char *key, long lo, long hi, long def, long *out, char *err, size_t cap, const char *where) {
    const ExtJson *j = extJsonGet(o, key);

    if (!j) {
        *out = def;
        return 1;
    }
    if (j->t != 0 || !extWhole(j->num, lo, hi)) {
        fail(err, cap, "%s: \"%s\" must be a whole number from %ld to %ld", where, key, lo, hi);
        return 0;
    }
    *out = (long)j->num;
    return 1;
}

#define INT(o, key, lo, hi, def, dst, where) \
    do { \
        long v_; \
        if (!getInt((o), (key), (lo), (hi), (def), &v_, err, errCap, (where))) goto bad; \
        (dst) = (int)v_; \
    } while (0)

static char *dupStr(const char *s) {
    char *c = (char *)malloc(strlen(s) + 1);

    if (c) strcpy(c, s);
    return c;
}

static int readList(const StageCodeEnv *env, const ExtJson *o, const char *key, SdList *l, char *err, size_t cap, const char *where) {
    const ExtJson *j = extJsonGet(o, key);
    char why[200];

    if (!j) return 0;
    if (scListFromJson(env, j, &l->w, &l->n, why, sizeof why)) {
        fail(err, cap, "%s \"%s\": %s", where, key, why);
        return -1;
    }
    l->set = 1;
    return 0;
}

static int readStringList(const ExtJson *o, const char *key, char ***arr, int *n, char *err, size_t cap) {
    const ExtJson *j = extJsonGet(o, key);
    int i;

    if (!j) return 0;
    if (j->t != 4) {
        fail(err, cap, "\"%s\" must be an array of strings", key);
        return -1;
    }
    for (i = 0; i < j->n; i++) {
        if (j->v[i].t != 1 || !j->v[i].str || addString(arr, n, j->v[i].str)) {
            fail(err, cap, "\"%s\" must be an array of strings", key);
            return -1;
        }
    }
    return 0;
}

int sdStageFromJson(const StageCodeEnv *env, const char *text, size_t len, SdStage *out, char *err, size_t errCap) {
    ExtJson root;
    const ExtJson *j, *f;
    int i, k;
    char where[80];

    memset(out, 0, sizeof *out);
    if (extJsonParse((const uint8_t *)text, len, &root)) {
        fail(err, errCap, "not a JSON object");
        return -1;
    }
    f = extJsonGet(&root, "format");
    if (!f || f->t != 1 || !f->str || strcmp(f->str, SD_FORMAT)) {
        fail(err, errCap, "\"format\" must be \"%s\"", SD_FORMAT);
        goto bad;
    }
    INT(&root, "stage", 0, 9999, 0, out->number, "stage");
    if ((f = extJsonGet(&root, "name")) && f->t == 1 && f->str) snprintf(out->name, sizeof out->name, "%s", f->str);
    if ((j = extJsonGet(&root, "modes")) && j->t == 4) {
        for (i = 0; i < j->n && out->nmodes < 4; i++) {
            if (j->v[i].t != 0 || !extWhole(j->v[i].num, 0, 0xFFFF)) {
                fail(err, errCap, "\"modes\" must be whole numbers");
                goto bad;
            }
            out->modes[out->nmodes++] = (int)j->v[i].num;
        }
    }
    INT(&root, "textFile", -1, 0xFFFFFF, -1, out->textFile, "stage");
    INT(&root, "mapFile", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->mapFile, "stage");
    INT(&root, "sheetEntry", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->sheetEntry, "stage");
    INT(&root, "imageEntry", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->imageEntry, "stage");
    INT(&root, "imageFile", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->imageFile, "stage");
    INT(&root, "soundBank", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->soundBank, "stage");
    INT(&root, "music", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->music, "stage");
    INT(&root, "objects", 0, 0xFFFFFF, 0, out->nobjects, "stage");
    if ((j = extJsonGet(&root, "start")) && j->t == 2) {
        INT(j, "x", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->startX, "start");
        INT(j, "y", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->startY, "start");
        INT(j, "dir", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->startDir, "start");
    }
    if ((j = extJsonGet(&root, "spriteColor")) && j->t == 4 && j->n == 4) {
        for (i = 0; i < 4; i++) out->spriteColor[i] = (int)j->v[i].num;
    }
    if ((j = extJsonGet(&root, "images")) && j->t == 4 && j->n > 0) {
        out->images = (SdImage *)calloc((size_t)j->n, sizeof *out->images);
        if (!out->images) goto bad;
        out->nimages = j->n;
        for (i = 0; i < j->n; i++) {
            SdImage *m = &out->images[i];
            const ExtJson *o = &j->v[i];

            snprintf(where, sizeof where, "image %d", i);
            INT(o, "x", -32768, 32767, 0, m->x, where);
            INT(o, "y", -32768, 32767, 0, m->y, where);
            INT(o, "imageX", -32768, 32767, 0, m->imageX, where);
            INT(o, "imageY", -32768, 32767, 0, m->imageY, where);
            INT(o, "u", -32768, 32767, 0, m->u, where);
            INT(o, "v", -32768, 32767, 0, m->v, where);
            INT(o, "clutX", -32768, 32767, 0, m->clutX, where);
            INT(o, "clutY", -32768, 32767, 0, m->clutY, where);
        }
    }
    if ((j = extJsonGet(&root, "actors")) && j->t == 4 && j->n > 0) {
        out->actors = (SdActor *)calloc((size_t)j->n, sizeof *out->actors);
        if (!out->actors) goto bad;
        out->nactors = j->n;
        for (i = 0; i < j->n; i++) {
            SdActor *a = &out->actors[i];
            const ExtJson *o = &j->v[i], *tl;

            snprintf(where, sizeof where, "actor %d", i);
            INT(o, "character", 0, 0xFFFF, 0, a->character, where);
            INT(o, "image", -32768, 32767, 0, a->image, where);
            INT(o, "x", -32768, 32767, 0, a->x, where);
            INT(o, "y", -32768, 32767, 0, a->y, where);
            INT(o, "dir", -32768, 32767, 0, a->dir, where);
            if (readList(env, o, "conditions", &a->cond, err, errCap, where)) goto bad;
            if ((tl = extJsonGet(o, "talks")) && tl->t == 4 && tl->n > 0) {
                a->talks = (SdTalk *)calloc((size_t)tl->n, sizeof *a->talks);
                if (!a->talks) goto bad;
                a->ntalks = tl->n;
                for (k = 0; k < tl->n; k++) {
                    SdTalk *t = &a->talks[k];
                    const ExtJson *p;

                    snprintf(where, sizeof where, "actor %d talk %d", i, k);
                    INT(&tl->v[k], "entry", -0x7FFFFFFF, 0x7FFFFFFF, 0, t->entry, where);
                    if (readList(env, &tl->v[k], "conditions", &t->cond, err, errCap, where)) goto bad;
                    if (readList(env, &tl->v[k], "actions", &t->actions, err, errCap, where)) goto bad;
                    if ((p = extJsonGet(&tl->v[k], "preview")) && p->t == 1 && p->str) t->preview = dupStr(p->str);
                }
            }
        }
    }
    if ((j = extJsonGet(&root, "slots")) && j->t == 4 && j->n > 0) {
        out->slots = (SdSlot *)calloc((size_t)j->n, sizeof *out->slots);
        if (!out->slots) goto bad;
        out->nslots = j->n;
        for (i = 0; i < j->n; i++) {
            SdSlot *t = &out->slots[i];
            const ExtJson *o = &j->v[i], *ty = extJsonGet(o, "type"), *p;

            snprintf(where, sizeof where, "slot %d", i);
            if (ty && ty->t == 1 && ty->str) {
                t->type = scSlotTypeByName(ty->str);
                if (!t->type) {
                    fail(err, errCap, "%s: unknown type \"%s\"", where, ty->str);
                    goto bad;
                }
            } else if (ty && ty->t == 0 && extWhole(ty->num, 1, 0xFFFF)) {
                t->type = (int)ty->num;
            } else {
                fail(err, errCap, "%s: \"type\" is a slot name or a number", where);
                goto bad;
            }
            INT(o, "arg", 0, 0xFFFF, 0, t->arg, where);
            INT(o, "x", 0, 0xFFFF, 0, t->x, where);
            INT(o, "y", 0, 0xFFFF, 0, t->y, where);
            INT(o, "dir", 0, 0xFFFF, 0, t->dir, where);
            INT(o, "hideAnim", 0, 0xFFFF, 0, t->hideAnim, where);
            INT(o, "place", 0, 0xFFFF, 0, t->place, where);
            INT(o, "placeArg", 0, 0xFFFF, 0, t->placeArg, where);
            t->facing = -1;
            if ((p = extJsonGet(o, "area")) && p->t == 2) {
                t->hasArea = 1;
                INT(p, "x", 0, 0x7FFF, 0, t->ax, where);
                INT(p, "y", 0, 0x7FFF, 0, t->ay, where);
                INT(p, "w", 1, 0x7FFF, 1, t->aw, where);
                INT(p, "h", 1, 0x7FFF, 1, t->ah, where);
                INT(p, "facing", -1, 7, -1, t->facing, where);
            }
            if (readList(env, o, "conditions", &t->cond, err, errCap, where)) goto bad;
        }
    }
    if ((j = extJsonGet(&root, "events")) && j->t == 4 && j->n > 0) {
        out->events = (SdEvent *)calloc((size_t)j->n, sizeof *out->events);
        if (!out->events) goto bad;
        out->nevents = j->n;
        for (i = 0; i < j->n; i++) {
            SdEvent *e = &out->events[i];
            const ExtJson *o = &j->v[i], *p;

            snprintf(where, sizeof where, "event %d", i);
            INT(o, "id", -0x7FFFFFFF, 0x7FFFFFFF, 0, e->id, where);
            if ((p = extJsonGet(o, "text")) && p->t == 2) {
                e->hasText = 1;
                INT(p, "file", 0, 0xFFFF, 0, e->textFile, where);
                INT(p, "entry", 0, 0xFFFF, 0, e->textEntry, where);
            }
            if ((p = extJsonGet(o, "start")) && p->t == 3) e->hasStart = p->num != 0;
            if ((p = extJsonGet(o, "end")) && p->t == 3) e->hasEnd = p->num != 0;
            if ((p = extJsonGet(o, "script"))) {
                char why[200];

                if (scScriptFromJson(p, &e->script, &e->nscript, why, sizeof why)) {
                    fail(err, errCap, "%s \"script\": %s", where, why);
                    goto bad;
                }
                e->hasScript = 1;
            }
            if ((p = extJsonGet(o, "previews")) && p->t == 2 && p->n > 0) {
                e->previews = (SdPreview *)calloc((size_t)p->n, sizeof *e->previews);
                if (!e->previews) goto bad;
                for (k = 0; k < p->n; k++) {
                    if (p->v[k].t != 1 || !p->v[k].str) continue;
                    e->previews[e->npreviews].entry = atoi(p->keys[k]);
                    e->previews[e->npreviews].text = dupStr(p->v[k].str);
                    e->npreviews++;
                }
            }
        }
    }
    if ((j = extJsonGet(&root, "battles")) && j->t == 2) {
        const ExtJson *areas = extJsonGet(j, "areas");

        out->battles.present = 1;
        INT(j, "place", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->battles.place, "battles");
        INT(j, "serial", -0x7FFFFFFF, 0x7FFFFFFF, 0, out->battles.serial, "battles");
        if (!areas || areas->t != 4 || areas->n < 1 || areas->n > SD_AREAS_MAX) {
            fail(err, errCap, "\"battles\".\"areas\" must be a list of 1 to %d areas", SD_AREAS_MAX);
            goto bad;
        }
        out->battles.nareas = areas->n;
        for (i = 0; i < areas->n; i++) {
            const ExtJson *o = &areas->v[i], *sl = extJsonGet(o, "slots");

            snprintf(where, sizeof where, "battles area %d", i + 1);
            INT(o, "rate", 0, 0xFFFF, 0, out->battles.areas[i].rate, where);
            if (!sl || sl->t != 4 || sl->n != 8) {
                fail(err, errCap, "%s: \"slots\" must be a list of 8 battles", where);
                goto bad;
            }
            for (k = 0; k < 8; k++) {
                SdAreaBattle *x = &out->battles.areas[i].b[k];

                INT(&sl->v[k], "encounter", 0, 0x7FFFFFFF, 0, x->encounter, where);
                INT(&sl->v[k], "stage", -0x7FFFFFFF, 0x7FFFFFFF, 0, x->stage, where);
                INT(&sl->v[k], "music", -0x7FFFFFFF, 0x7FFFFFFF, 0, x->music, where);
            }
        }
    }
    if (readStringList(&root, "functions", &out->functions, &out->nfunctions, err, errCap)) goto bad;
    if (readStringList(&root, "notes", &out->notes, &out->nnotes, err, errCap)) goto bad;
    extJsonFree(&root);
    return 0;
bad:
    if (err && errCap && !err[0]) fail(err, errCap, "damaged stage data");
    extJsonFree(&root);
    sdStageFree(out);
    return -1;
}

/* ---- equality ---- */

static int sameList(const SdList *a, const SdList *b) {
    return a->set == b->set && a->n == b->n && (a->n == 0 || memcmp(a->w, b->w, (size_t)a->n * sizeof *a->w) == 0);
}

static int sameStr(const char *a, const char *b) {
    if (!a || !b) return a == b;
    return !strcmp(a, b);
}

int sdStageEqual(const SdStage *a, const SdStage *b) {
    int i, j;

    if (a->number != b->number || strcmp(a->name, b->name) || a->nmodes != b->nmodes || a->textFile != b->textFile || a->mapFile != b->mapFile ||
        a->sheetEntry != b->sheetEntry || a->imageEntry != b->imageEntry || a->imageFile != b->imageFile || a->soundBank != b->soundBank ||
        a->music != b->music || a->startX != b->startX || a->startY != b->startY || a->startDir != b->startDir || a->nobjects != b->nobjects ||
        a->nimages != b->nimages || a->nactors != b->nactors || a->nslots != b->nslots || a->nevents != b->nevents ||
        a->nfunctions != b->nfunctions || a->nnotes != b->nnotes)
        return 0;
    for (i = 0; i < a->nmodes; i++) {
        if (a->modes[i] != b->modes[i]) return 0;
    }
    if (memcmp(a->spriteColor, b->spriteColor, sizeof a->spriteColor)) return 0;
    if (a->nimages && memcmp(a->images, b->images, (size_t)a->nimages * sizeof *a->images)) return 0;
    for (i = 0; i < a->nactors; i++) {
        const SdActor *x = &a->actors[i], *y = &b->actors[i];

        if (x->character != y->character || x->image != y->image || x->x != y->x || x->y != y->y || x->dir != y->dir || x->ntalks != y->ntalks ||
            !sameList(&x->cond, &y->cond))
            return 0;
        for (j = 0; j < x->ntalks; j++) {
            if (x->talks[j].entry != y->talks[j].entry || !sameList(&x->talks[j].cond, &y->talks[j].cond) ||
                !sameList(&x->talks[j].actions, &y->talks[j].actions) || !sameStr(x->talks[j].preview, y->talks[j].preview))
                return 0;
        }
    }
    for (i = 0; i < a->nslots; i++) {
        const SdSlot *x = &a->slots[i], *y = &b->slots[i];

        if (x->type != y->type || x->arg != y->arg || x->x != y->x || x->y != y->y || x->dir != y->dir || x->hideAnim != y->hideAnim ||
            x->place != y->place || x->placeArg != y->placeArg || !sameList(&x->cond, &y->cond) || x->hasArea != y->hasArea ||
            (x->hasArea && (x->ax != y->ax || x->ay != y->ay || x->aw != y->aw || x->ah != y->ah || x->facing != y->facing)))
            return 0;
    }
    for (i = 0; i < a->nevents; i++) {
        const SdEvent *x = &a->events[i], *y = &b->events[i];

        if (x->id != y->id || x->hasText != y->hasText || x->textFile != y->textFile || x->textEntry != y->textEntry || x->hasScript != y->hasScript ||
            x->nscript != y->nscript || x->hasStart != y->hasStart || x->hasEnd != y->hasEnd || x->npreviews != y->npreviews ||
            (x->nscript && memcmp(x->script, y->script, (size_t)x->nscript * sizeof *x->script)))
            return 0;
        for (j = 0; j < x->npreviews; j++) {
            if (x->previews[j].entry != y->previews[j].entry || !sameStr(x->previews[j].text, y->previews[j].text)) return 0;
        }
    }
    if (a->battles.present != b->battles.present) return 0;
    if (a->battles.present) {
        if (a->battles.place != b->battles.place || a->battles.serial != b->battles.serial || a->battles.nareas != b->battles.nareas) return 0;
        for (i = 0; i < a->battles.nareas; i++) {
            if (memcmp(&a->battles.areas[i], &b->battles.areas[i], sizeof a->battles.areas[i])) return 0;
        }
    }
    for (i = 0; i < a->nfunctions; i++) {
        if (strcmp(a->functions[i], b->functions[i])) return 0;
    }
    for (i = 0; i < a->nnotes; i++) {
        if (strcmp(a->notes[i], b->notes[i])) return 0;
    }
    return 1;
}

/* ---- the reference export ---- */

#define MARK_KEY "version"

static char sDataDir[1024];
static TdManifest sMan;
static int sManLoaded;

static void stagesDir(char *out, size_t cap) { snprintf(out, cap, "%s/stages", sDataDir); }

static void versionHex(char *hex) { snprintf(hex, 17, "%016x", (unsigned)SD_EXPORT_VERSION); }

void sdExportSetup(const char *dataDir) {
    char path[1200];
    uint8_t *mt;
    size_t ml;

    if (sManLoaded) tdManifestFree(&sMan);
    memset(&sMan, 0, sizeof sMan);
    sManLoaded = 0;
    sDataDir[0] = 0;
    if (!dataDir || !dataDir[0]) return;
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir);
    snprintf(sMan.stamp, sizeof sMan.stamp, "stages-v1");
    snprintf(path, sizeof path, "%s/stages/manifest.json", sDataDir);
    if (tdReadFile(path, &mt, &ml) == 0) {
        TdManifest old;
        char err[120];

        memset(&old, 0, sizeof old);
        if (tdManifestParse((const char *)mt, ml, &old, err, sizeof err) == 0) {
            tdManifestFree(&sMan);
            sMan = old;
        }
        free(mt);
    }
    sManLoaded = 1;
}

static int fileExists(const char *p) {
    FILE *f = fopen(p, "rb");

    if (!f) return 0;
    fclose(f);
    return 1;
}

int sdExportUpToDate(const int *numbers, int count) {
    char hex[17], dir[1100], path[1250];
    const char *have;
    int i;

    if (!sManLoaded) return 0;
    have = tdManifestFind(&sMan, MARK_KEY);
    versionHex(hex);
    if (!have || strcmp(have, hex)) return 0;
    stagesDir(dir, sizeof dir);
    for (i = 0; i < count; i++) {
        snprintf(path, sizeof path, "%s/%03d.json", dir, numbers[i]);
        if (!fileExists(path)) return 0;
    }
    return 1;
}

/* One file under the manifest rule of ADR-005: an unedited copy is replaced, an edited one stays and the new version goes beside it as <file>.new */
int sdExportPut(int number, const char *text, SdExportStats *st) {
    char rel[32], dir[1100], path[1250], np[1300], hex[17], curHex[17] = "";
    uint8_t *cur = NULL;
    size_t curLen = 0, len = strlen(text);
    int exists = 0;
    const char *man;

    if (!sManLoaded) return -1;
    snprintf(rel, sizeof rel, "%03d.json", number);
    man = tdManifestFind(&sMan, rel);
    stagesDir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    tdHashHex(tdHash(text, len), hex);
    if (tdReadFile(path, &cur, &curLen) == 0) {
        exists = 1;
        tdHashHex(tdHash(cur, curLen), curHex);
        free(cur);
    }
    if (exists && !strcmp(curHex, hex)) {
        st->kept++;
        tdManifestSet(&sMan, rel, hex);
        return 0;
    }
    if (exists && tdFileState(1, man, curHex) == TD_FILE_EDITED) {
        snprintf(np, sizeof np, "%s.new", path);
        if (tdMakeDirs(dir) || tdWriteFileAtomic(np, text, len)) {
            plogLine("stages", "cannot write %s\n", np);
            st->failed++;
            return -1;
        }
        st->newFiles++;
        plogLine("stages", "%s was edited, kept; the new version is %s.new\n", rel, rel);
        return 0;
    }
    if (tdMakeDirs(dir) || tdWriteFileAtomic(path, text, len)) {
        plogLine("stages", "cannot write %s\n", path);
        st->failed++;
        return -1;
    }
    tdManifestSet(&sMan, rel, hex);
    st->written++;
    return 0;
}

void sdExportEnd(int ok) {
    char path[1200], dir[1100], hex[17];
    char *out;

    if (!sManLoaded) return;
    if (ok) {
        versionHex(hex);
        tdManifestSet(&sMan, MARK_KEY, hex);
    }
    stagesDir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    out = tdManifestFormat(&sMan);
    if (!out || tdMakeDirs(dir) || tdWriteFileAtomic(path, out, strlen(out))) plogLine("stages", "cannot write %s\n", path);
    free(out);
}
