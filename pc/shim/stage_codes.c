/*
 * The code vocabulary of the field stages: see stage_codes.h and docs/stages.md. The meaning of every name is the one of
 * src/engine/game/events.c (checkCondition, applyAction) and src/field/field_mode/event.c (FIELDSTG_runEvent).
 */
#include "stage_codes.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- text buffer ---- */

void scPrintf(ScBuf *B, const char *fmt, ...) {
    va_list ap;
    int k;

    if (B->bad) return;
    for (;;) {
        va_start(ap, fmt);
        k = vsnprintf(B->b ? B->b + B->n : NULL, B->cap - B->n, fmt, ap);
        va_end(ap);
        if (k < 0) {
            B->bad = 1;
            return;
        }
        if (B->b && B->n + (size_t)k < B->cap) {
            B->n += (size_t)k;
            return;
        }
        {
            size_t cap = B->cap ? B->cap * 2 : 1024;
            char *nb;

            while (cap <= B->n + (size_t)k) cap *= 2;
            nb = (char *)realloc(B->b, cap);
            if (!nb) {
                B->bad = 1;
                return;
            }
            B->b = nb;
            B->cap = cap;
        }
    }
}

void scPutEsc(ScBuf *B, const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\') scPrintf(B, "\\%c", c);
        else if (c == '\n') scPrintf(B, "\\n");
        else if (c < 0x20) scPrintf(B, "\\u%04x", c);
        else scPrintf(B, "%c", c);
    }
}

void scFree(ScBuf *B) {
    free(B->b);
    memset(B, 0, sizeof *B);
}

/* ---- vocabulary tables ---- */

#define PROG_ITEM_SET 0x00
#define PROG_PARTNER 0x10
#define PROG_MONEY 0x20
#define PROG_PROGRESS_RANGE 0x30
#define PROG_FLAG_COUNT 0x40
#define PROG_ACTOR_ICON 0x50

static int isFlagGroup(int g) {
    switch (g) {
    case 0x00: case 0x02: case 0x04: case 0x06: case 0x08: case 0x0A: case 0x0C: case 0x0E:
    case 0x10: case 0x18: case 0x1A: case 0x1C: case 0x20: case 0x40:
        return 1;
    }
    return 0;
}

/* "partner.in-party(1)": what a SPECIAL_CONDITIONS row does, as a comment after the pair */
static const char *specialPrimitive(int check) {
    switch (check & 0xF0) {
    case PROG_ITEM_SET: return "item-set";
    case PROG_PARTNER: return "partner";
    case PROG_MONEY: return "money";
    case PROG_PROGRESS_RANGE: return "progress-range";
    case PROG_FLAG_COUNT: return "flag-count";
    case PROG_ACTOR_ICON: return "actor-icon";
    }
    return "primitive";
}

static const char *specialOp(int check) {
    int op = check & 0x0F;

    switch (check & 0xF0) {
    case PROG_PARTNER: {
        static const char *const n[] = {"party-set", "unlocked", "in-party", "unlock", "level-45", "levels-sum", "locked",
                                        "all-level-45", "heal-party", "all-unlocked"};
        return op < 10 ? n[op] : "op";
    }
    case PROG_MONEY: {
        static const char *const n[] = {"has", "gain", "lose"};
        return op < 3 ? n[op] : "op";
    }
    case PROG_PROGRESS_RANGE: return "in-range";
    case PROG_FLAG_COUNT: return op == 0 ? "flags-1c" : "flags-10";
    case PROG_ACTOR_ICON: return "show";
    case PROG_ITEM_SET: return op == 0 ? "set-a" : "set-b";
    }
    return "op";
}

static const char *const ACT_NAMES[SC_ACT_KINDS] = {NULL, "give-money", "take-money", "set-progress", "warp", "sound", "start-battle", "run-event"};

const char *scActName(int kind) {
    return (kind > 0 && kind < SC_ACT_KINDS) ? ACT_NAMES[kind] : NULL;
}

int scActByName(const char *name) {
    int i;

    for (i = 1; name && i < SC_ACT_KINDS; i++) {
        if (!strcmp(name, ACT_NAMES[i])) return i;
    }
    return 0;
}

/* the game's shared sounds (include/engine/sound.h), by name */
static const char *const SOUND_NAMES[SC_SOUND_COUNT] = {"select", "cancel", "cursor", "menu-open", "menu-close", "menu-move", "menu-confirm", "count",
                                                         "recovery", "teleport", "win-jingle", "inn-jingle"};

const char *scSoundName(int i) {
    return (i >= 0 && i < SC_SOUND_COUNT) ? SOUND_NAMES[i] : NULL;
}

int scSoundByName(const char *name) {
    int i;

    for (i = 0; name && i < SC_SOUND_COUNT; i++) {
        if (!strcmp(name, SOUND_NAMES[i])) return i;
    }
    return -1;
}

static const char *const CMP_NAMES[SC_CMP_COUNT] = {"==", "!=", ">=", ">", "<=", "<"};

const char *scCmpName(int cmp) {
    return (cmp >= 0 && cmp < SC_CMP_COUNT) ? CMP_NAMES[cmp] : NULL;
}

int scCmpByName(const char *name) {
    int i;

    for (i = 0; name && i < SC_CMP_COUNT; i++) {
        if (!strcmp(name, CMP_NAMES[i])) return i;
    }
    return -1;
}

int scModNameValid(const char *name) {
    const char *colon, *p;

    if (!name || strlen(name) > 120) return 0;
    colon = strchr(name, ':');
    if (!colon || colon == name || !colon[1] || strchr(colon + 1, ':')) return 0;
    for (p = name; p < colon; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) return 0;
    }
    for (p = colon + 1; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '/')) return 0;
    }
    return 1;
}

/* ---- decompile: one pair ---- */

static int namedRef(const StageCodeEnv *env, const char *kind, int id, char *out, size_t cap) {
    if (env && env->nameOf && env->nameOf(env->ud, kind, id, out, cap)) return 1;
    if (env && env->nameOf) return 0; /* a registry that has no name for it: a number */
    snprintf(out, cap, "dw3:%s/%d", kind, id);
    return 1;
}

static void putRef(ScBuf *B, const StageCodeEnv *env, const char *kind, int id) {
    char name[120];

    if (namedRef(env, kind, id, name, sizeof name)) {
        scPrintf(B, "\"");
        scPutEsc(B, name);
        scPrintf(B, "\"");
    } else {
        scPrintf(B, "%d", id);
    }
}

static void pairToJson(const StageCodeEnv *env, ScBuf *B, unsigned code, unsigned value) {
    int group = (int)((code >> 8) & 0xFE);
    int id = (int)(code & 0x1FF);

    if (isFlagGroup(group)) {
        scPrintf(B, "[\"flag\", \"%02X:%X\", %u]", group, id, value);
    } else if (group == 0x60) {
        scPrintf(B, "[\"progress\", %d, %u]", id, value);
    } else if (group == 0x70) {
        int check, arg;

        const char *modName = id >= 0x100 && env && env->specialName ? env->specialName(env->ud, id) : NULL;

        if (modName) {
            scPrintf(B, "[\"special\", \"");
            scPutEsc(B, modName);
            scPrintf(B, "\", %u", value);
        } else {
            scPrintf(B, "[\"special\", \"0x%02X\", %u", id, value);
        }
        if (env && env->specialRow && env->specialRow(env->ud, id, &check, &arg)) {
            scPrintf(B, ", \"%s.%s(%d)\"", specialPrimitive(check), specialOp(check), arg);
        }
        scPrintf(B, "]");
    } else if (group == 0x72) {
        scPrintf(B, "[\"party-stat\", %d, %u]", id, value);
    } else if (group == 0x74) {
        scPrintf(B, "[\"event-battle\", %d, %u]", id, value);
    } else if (group == 0x76 || group == 0x78) {
        scPrintf(B, "[\"card-battle\", \"%X:%d\", %u]", id, group == 0x78, value);
    } else if (group == 0x7A) {
        const char *name = id < 30 ? "shop" : ((unsigned)(id - 0x31) < 0x13u || (unsigned)(id - 0x46) < 5u) ? "card-shop" : "inn";

        scPrintf(B, "[\"%s\", %d, %u]", name, id, value);
    } else if (group == 0x7C && (id == 0 || id == 1)) {
        scPrintf(B, "[\"%s\", %u]", id == 0 ? "lab" : "naming", value);
    } else if (group == 0x7E) {
        if (id < 30) scPrintf(B, "[\"place\", %d, %u]", id + 1, value);
        else scPrintf(B, "[\"place-arg\", %d, %u]", id - 29, value);
    } else if (group >= 0x80 && group < 0x8F) {
        int kind = (group - 0x80) >> 1;

        scPrintf(B, "[\"item\", ");
        putRef(B, env, "item", id);
        scPrintf(B, ", %u", value);
        if (kind) scPrintf(B, ", %d", kind);
        scPrintf(B, "]");
    } else if (group == 0x90) {
        scPrintf(B, "[\"start-event\", %d, %u]", id, value);
    } else if (group == 0x92) {
        scPrintf(B, "[\"card\", ");
        putRef(B, env, "card", id);
        scPrintf(B, ", %u]", value);
    } else if (group == 0x94) {
        scPrintf(B, "[\"training\", %d, %u]", id, value);
    } else if (group >= SC_ACT_GROUP_FIRST && group <= SC_ACT_GROUP_LAST && env && env->actOpInfo) {
        int kind, a[SC_ACT_ARGS] = {0};

        if (!env->actOpInfo(env->ud, code, &kind, a) || !scActName(kind)) {
            scPrintf(B, "[\"raw\", \"0x%04X\", %u]", code, value);
        } else if (kind == SC_ACT_WARP) {
            scPrintf(B, "[\"warp\", %d, %d, %d, %d]", a[0], a[1], a[2], a[3]);
        } else if (kind == SC_ACT_SOUND) {
            scPrintf(B, "[\"sound\", \"%s\"]", scSoundName(a[0]) ? scSoundName(a[0]) : "select");
        } else if (kind == SC_ACT_START_BATTLE) {
            scPrintf(B, "[\"start-battle\", %d, %d, %d]", a[0], a[1], a[2]);
        } else if (kind == SC_ACT_RUN_EVENT) {
            const char *en = env->eventName ? env->eventName(env->ud, a[0]) : NULL;

            if (en) {
                scPrintf(B, "[\"run-event\", \"");
                scPutEsc(B, en);
                scPrintf(B, "\"]");
            } else {
                scPrintf(B, "[\"run-event\", %d]", a[0]);
            }
        } else {
            scPrintf(B, "[\"%s\", %d]", scActName(kind), a[0]);
        }
    } else if (group >= SC_MOD_GROUP_FIRST && group <= SC_MOD_GROUP_LAST && env && env->modOpInfo) {
        int kind, cmp, n;
        char name[160];

        if (!env->modOpInfo(env->ud, code, &kind, name, sizeof name, &cmp, &n)) {
            scPrintf(B, "[\"raw\", \"0x%04X\", %u]", code, value);
        } else if (kind == SC_MOD_FLAG) {
            scPrintf(B, "[\"mod-flag\", \"");
            scPutEsc(B, name);
            scPrintf(B, "\", %u]", value);
        } else if (kind == SC_MOD_VAR) {
            scPrintf(B, "[\"mod-var\", \"");
            scPutEsc(B, name);
            scPrintf(B, "\", \"%s\", %d", scCmpName(cmp) ? scCmpName(cmp) : "==", n);
            if (value != 1) scPrintf(B, ", %u", value);
            scPrintf(B, "]");
        } else {
            scPrintf(B, "[\"set-var\", \"");
            scPutEsc(B, name);
            scPrintf(B, "\", %u]", value);
        }
    } else {
        scPrintf(B, "[\"raw\", \"0x%04X\", %u]", code, value);
    }
}

int scListToJson(const StageCodeEnv *env, const uint16_t *words, int n, ScBuf *out) {
    int i;

    if (n < 0 || (n & 1)) return -1;
    scPrintf(out, "[");
    for (i = 0; i < n; i += 2) {
        if (i) scPrintf(out, ", ");
        pairToJson(env, out, words[i], words[i + 1]);
    }
    scPrintf(out, "]");
    return out->bad ? -1 : 0;
}

/* ---- compile ---- */

static void fail(char *err, size_t cap, const char *fmt, ...) {
    va_list ap;

    if (!err || !cap) return;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

static int wholeIn(const ExtJson *j, long lo, long hi, long *out) {
    if (j->t != 0 || !extWhole(j->num, lo, hi)) return 0;
    *out = (long)j->num;
    return 1;
}

/* a number, or a string of hex digits (with an optional 0x) */
static int hexOf(const char *s, long *out) {
    char *end;
    long v;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s) return 0;
    v = strtol(s, &end, 16);
    if (*end || v < 0) return 0;
    *out = v;
    return 1;
}

/* "g:i" (flag) or "a:b" (card battle) as two hex numbers */
static int hexPair(const char *s, long *a, long *b) {
    char buf[40], *colon;

    if (strlen(s) >= sizeof buf) return 0;
    strcpy(buf, s);
    colon = strchr(buf, ':');
    if (!colon) return 0;
    *colon = 0;
    return hexOf(buf, a) && hexOf(colon + 1, b);
}

/* a reference: a number, "dw3:<kind>/<id>" or whatever the registry resolves; -1 when it names nothing */
static long refId(const StageCodeEnv *env, const ExtJson *j, const char *kind) {
    long v;
    char prefix[24];
    size_t pl;

    if (j->t == 0) return wholeIn(j, 0, 0x1FF, &v) ? v : -1;
    if (j->t != 1 || !j->str) return -1;
    snprintf(prefix, sizeof prefix, "dw3:%s/", kind);
    pl = strlen(prefix);
    if (!strncmp(j->str, prefix, pl)) {
        char *end;

        if (!j->str[pl]) return -1;
        v = strtol(j->str + pl, &end, 10);
        return (*end || v < 0 || v > 0x1FF) ? -1 : v;
    }
    if (env && env->resolve) {
        int id = env->resolve(env->ud, kind, j->str);

        return (id >= 0 && id <= 0x1FF) ? id : -1;
    }
    return -1;
}

typedef struct Word {
    unsigned code, value;
} Word;

/* one JSON pair into a code and a value; 0, or -1 with the reason in err */
static int pairFromJson(const StageCodeEnv *env, const ExtJson *p, int index, Word *w, char *err, size_t cap) {
    const char *name;
    long ref = 0, a, b, value;
    const ExtJson *r;
    int n;

    if (p->t != 4 || p->n < 2 || p->v[0].t != 1 || !p->v[0].str) {
        fail(err, cap, "code %d is not [\"name\", ...]", index);
        return -1;
    }
    name = p->v[0].str;
    n = p->n;
    r = &p->v[1];
#define NEED(cnt) \
    if (n < (cnt)) { \
        fail(err, cap, "code %d (\"%s\") needs %d elements", index, name, (cnt)); \
        return -1; \
    }
#define VALUE(at) \
    if (!wholeIn(&p->v[at], 0, 0xFFFF, &value)) { \
        fail(err, cap, "code %d (\"%s\"): the value must be a whole number from 0 to 65535", index, name); \
        return -1; \
    }
#define BADREF(what) \
    do { \
        fail(err, cap, "code %d (\"%s\"): %s", index, name, what); \
        return -1; \
    } while (0)

    if (!strcmp(name, "flag")) {
        NEED(3);
        if (r->t != 1 || !r->str || !hexPair(r->str, &a, &b) || !isFlagGroup((int)a) || b > 0x1FF) BADREF("the flag must be \"GG:III\" (hex group and flag, e.g. \"1A:1F\")");
        VALUE(2);
        w->code = (unsigned)((a << 8) | b);
    } else if (!strcmp(name, "progress") || !strcmp(name, "party-stat") || !strcmp(name, "event-battle") || !strcmp(name, "start-event") ||
               !strcmp(name, "training")) {
        unsigned base = !strcmp(name, "progress") ? 0x6000 : !strcmp(name, "party-stat") ? 0x7200 : !strcmp(name, "event-battle") ? 0x7400
                        : !strcmp(name, "start-event") ? 0x9000 : 0x9400;
        NEED(3);
        if (!wholeIn(r, 0, 0x1FF, &ref)) BADREF("the number must be a whole number from 0 to 511");
        VALUE(2);
        w->code = base | (unsigned)ref;
    } else if (!strcmp(name, "special")) {
        NEED(3);
        if (r->t == 1 && r->str && strchr(r->str, ':')) { /* a condition a mod defines in its tables/special_conditions.json */
            int id = env && env->resolve ? env->resolve(env->ud, "special", r->str) : -1;

            if (id < 0x100 || id > 0x1FF) BADREF("no installed mod defines a special condition with that name (a row of tables/special_conditions.json that is valid)");
            ref = id;
        } else if (!((r->t == 0 && wholeIn(r, 0, 0x1FF, &ref)) || (r->t == 1 && r->str && hexOf(r->str, &ref) && ref <= 0x1FF))) {
            BADREF("the id must be a number, a hex string up to 0x1FF or a mod's \"<mod>:<name>\"");
        }
        VALUE(2);
        w->code = 0x7000u | (unsigned)ref;
    } else if (!strcmp(name, "card-battle")) {
        NEED(3);
        if (r->t != 1 || !r->str || !hexPair(r->str, &a, &b) || a > 0x1FF || b > 1) BADREF("the battle must be \"opponent:variant\" (hex opponent, variant 0 or 1)");
        VALUE(2);
        w->code = (b ? 0x7800u : 0x7600u) | (unsigned)a;
    } else if (!strcmp(name, "shop") || !strcmp(name, "card-shop") || !strcmp(name, "inn")) {
        int shop = !strcmp(name, "shop"), card = !strcmp(name, "card-shop");

        NEED(3);
        if (!wholeIn(r, 0, 0x1FF, &ref)) BADREF("the number must be a whole number from 0 to 511");
        if (shop ? ref >= 30 : card ? !((unsigned long)(ref - 0x31) < 0x13u || (unsigned long)(ref - 0x46) < 5u) :
                                      (ref < 30 || (unsigned long)(ref - 0x31) < 0x13u || (unsigned long)(ref - 0x46) < 5u))
            BADREF("this number is not one of that kind (shops 0-29, card shops 49-67 and 70-74, the inn the rest)");
        VALUE(2);
        w->code = 0x7A00u | (unsigned)ref;
    } else if (!strcmp(name, "lab") || !strcmp(name, "naming")) {
        NEED(2);
        VALUE(1);
        w->code = !strcmp(name, "lab") ? 0x7C00u : 0x7C01u;
        w->value = (unsigned)value;
        return 0;
    } else if (!strcmp(name, "place") || !strcmp(name, "place-arg")) {
        int isPlace = !strcmp(name, "place");

        NEED(3);
        if (!wholeIn(r, isPlace ? 1 : 1, isPlace ? 30 : 0x1FF - 29, &ref)) BADREF(isPlace ? "the place must be a whole number from 1 to 30" : "the arg must be a whole number from 1 to 482");
        VALUE(2);
        w->code = 0x7E00u | (unsigned)(isPlace ? ref - 1 : ref + 29);
    } else if (!strcmp(name, "item") || !strcmp(name, "card")) {
        int isItem = !strcmp(name, "item");
        long kind = 0;

        NEED(3);
        ref = refId(env, r, isItem ? "item" : "card");
        if (ref < 0) BADREF("an unknown name or number (use \"dw3:<kind>/<id>\" or a mod's \"<mod>:<key>\")");
        VALUE(2);
        if (isItem && n > 3 && p->v[3].t == 0) {
            if (!wholeIn(&p->v[3], 0, 7, &kind)) BADREF("the item class must be a whole number from 0 to 7");
        }
        w->code = isItem ? (0x8000u | (unsigned)(kind << 9) | (unsigned)ref) : (0x9200u | (unsigned)ref);
    } else if (!strcmp(name, "mod-flag") || !strcmp(name, "mod-var") || !strcmp(name, "set-var")) {
        int kind = !strcmp(name, "mod-flag") ? SC_MOD_FLAG : !strcmp(name, "mod-var") ? SC_MOD_VAR : SC_MOD_SET, cmp = 0, arg = 0, code;

        if (!env || !env->modOp) BADREF("mod flags need the stage patch layer (no mod flag table is available here)");
        NEED(kind == SC_MOD_VAR ? 4 : 3);
        if (r->t != 1 || !r->str || !scModNameValid(r->str)) BADREF("the name must be \"<mod>:<name>\" (the mod's folder, a colon, then letters, digits, _ - /)");
        if (kind == SC_MOD_VAR) {
            long nn;

            if (p->v[2].t != 1 || !p->v[2].str || (cmp = scCmpByName(p->v[2].str)) < 0) BADREF("the comparison must be one of \"==\" \"!=\" \">=\" \">\" \"<=\" \"<\"");
            if (!wholeIn(&p->v[3], -0x7FFFFFFF, 0x7FFFFFFF, &nn)) BADREF("the number to compare with must be a whole number");
            arg = (int)nn;
            value = 1;
            if (n > 4) {
                VALUE(4);
                if (value > 1) BADREF("the last element (does the comparison hold?) must be 0 or 1");
            }
        } else {
            VALUE(2);
            if (kind == SC_MOD_FLAG && value > 1) BADREF("the value must be 0 or 1");
        }
        code = env->modOp(env->ud, kind, r->str, cmp, arg);
        if (code < 0) BADREF("the table of mod flags is full");
        w->code = (unsigned)code;
    } else if (scActByName(name)) {
        int kind = scActByName(name), args[SC_ACT_ARGS] = {0}, code;
        long v;

        if (!env || !env->actOp) BADREF("port actions need the stage patch layer (no table of actions is available here)");
        switch (kind) {
        case SC_ACT_GIVE_MONEY:
        case SC_ACT_TAKE_MONEY:
            NEED(2);
            if (!wholeIn(r, 0, SC_MONEY_MAX, &v)) BADREF("the amount must be a whole number from 0 to 9999999");
            args[0] = (int)v;
            break;
        case SC_ACT_SET_PROGRESS:
            NEED(2);
            if (!wholeIn(r, 0, SC_PROGRESS_MAX, &v)) BADREF("the story progress must be a whole number from 0 to 99");
            args[0] = (int)v;
            break;
        case SC_ACT_WARP:
            NEED(5);
            if (!wholeIn(&p->v[1], 1, 0xFFFF, &v)) BADREF("the stage must be a stage number (1 to 65535)");
            args[0] = (int)v;
            if (env->stageExists && !env->stageExists(env->ud, args[0])) BADREF("there is no such stage in this build");
            if (!wholeIn(&p->v[2], 0, 8191, &v)) BADREF("x must be a whole number of pixels from 0 to 8191");
            args[1] = (int)v;
            if (!wholeIn(&p->v[3], 0, 8191, &v)) BADREF("y must be a whole number of pixels from 0 to 8191");
            args[2] = (int)v;
            if (!wholeIn(&p->v[4], 0, 7, &v)) BADREF("the direction must be a whole number from 0 to 7");
            args[3] = (int)v;
            break;
        case SC_ACT_START_BATTLE:
            NEED(4);
            if (!wholeIn(&p->v[1], 1, 4095, &v)) BADREF("the encounter must be a whole number from 1 to 4095 (a row of field_encounters)");
            args[0] = (int)v;
            if (!wholeIn(&p->v[2], 0, 0x7FFFFFFF, &v)) BADREF("the fight stage must be a whole number (the \"stage\" of a battle in the stage's exported \"battles\")");
            args[1] = (int)v;
            if (!wholeIn(&p->v[3], 0, 0x7FFFFFFF, &v)) BADREF("the music must be a whole number (the \"music\" of a battle in the stage's exported \"battles\")");
            args[2] = (int)v;
            break;
        case SC_ACT_RUN_EVENT:
            NEED(2);
            if (r->t == 1 && r->str) {
                if (!env->eventId || (args[0] = env->eventId(env->ud, r->str)) < 0) BADREF("the event must be a name \"<mod>:<name>\" (the mod's folder, a colon, a lowercase name) or a number");
            } else if (wholeIn(r, 1, 0xFFFF, &v)) {
                args[0] = (int)v;
            } else {
                BADREF("the event must be a name \"<mod>:<name>\" or an event id (1 to 65535)");
            }
            break;
        default: /* sound */
            NEED(2);
            if (r->t != 1 || !r->str || (args[0] = scSoundByName(r->str)) < 0) BADREF("the sound must be one of \"select\" \"cancel\" \"cursor\" \"menu-open\" \"menu-close\" \"menu-move\" \"menu-confirm\" \"count\" \"recovery\" \"teleport\" \"win-jingle\" \"inn-jingle\"");
            break;
        }
        code = env->actOp(env->ud, kind, args);
        if (code < 0) BADREF("the table of port actions is full");
        w->code = (unsigned)code;
        w->value = 1;
        return 0;
    } else if (!strcmp(name, "raw")) {
        NEED(3);
        if (!(r->t == 1 && r->str && hexOf(r->str, &ref) && ref <= 0xFFFF && ref != 0xFFFF)) BADREF("the code must be a hex string \"0xHHHH\" other than 0xFFFF (which ends a list)");
        VALUE(2);
        w->code = (unsigned)ref;
    } else {
        fail(err, cap, "code %d: unknown name \"%s\"", index, name);
        return -1;
    }
    w->value = (unsigned)value;
    return 0;
#undef NEED
#undef VALUE
#undef BADREF
}

int scListFromJson(const StageCodeEnv *env, const ExtJson *list, uint16_t **words, int *n, char *err, size_t errCap) {
    uint16_t *out;
    int i;

    *words = NULL;
    *n = 0;
    if (!list || list->t != 4) {
        fail(err, errCap, "a code list must be an array");
        return -1;
    }
    if (list->n == 0) return 0;
    out = (uint16_t *)malloc((size_t)list->n * 2 * sizeof *out);
    if (!out) {
        fail(err, errCap, "out of memory");
        return -1;
    }
    for (i = 0; i < list->n; i++) {
        Word w;

        if (pairFromJson(env, &list->v[i], i, &w, err, errCap)) {
            free(out);
            return -1;
        }
        out[i * 2] = (uint16_t)w.code;
        out[i * 2 + 1] = (uint16_t)w.value;
    }
    *words = out;
    *n = list->n * 2;
    return 0;
}

uint16_t *scListCopy(const uint16_t *src, int maxPairs, int *n, int *ended) {
    int pairs = 0;
    uint16_t *out;

    *n = 0;
    *ended = 0;
    if (!src) return NULL;
    while (pairs < maxPairs && src[pairs * 2] != 0xFFFF) pairs++;
    *ended = pairs < maxPairs;
    if (pairs == 0) return NULL;
    out = (uint16_t *)malloc((size_t)pairs * 2 * sizeof *out);
    if (!out) return NULL;
    memcpy(out, src, (size_t)pairs * 2 * sizeof *out);
    *n = pairs * 2;
    return out;
}

/* ---- event scripts ---- */

typedef struct ScriptCmd {
    int kind;       /* word >> 8 */
    int sub;        /* word & 0xFF, or -1 for any other sub of the kind (it runs like sub 0) */
    int words;      /* in all, the command word included */
    const char *name;
} ScriptCmd;

/* FIELDSTG_runEvent: kind 1 sub 0 place, 1 pose, 2 walk; kind 2 talk; kind 3 subs 0 wait, 1 wait-box, 2 wait-walk, 3 wait-anim, 4 leave;
   kind 6 subs 0 follow, 1 look-at. Any other sub of a kind runs as its sub 0 ("raw" here). */
static const ScriptCmd CMDS[] = {
    {1, 0, 4, "place"}, {1, 1, 4, "pose"}, {1, 2, 5, "walk"}, {2, 0, 5, "talk"}, {3, 0, 2, "wait"}, {3, 1, 1, "wait-box"},
    {3, 2, 2, "wait-walk"}, {3, 3, 2, "wait-anim"}, {3, 4, 5, "leave"}, {6, 0, 3, "follow"}, {6, 1, 4, "look-at"},
};
#define NCMDS ((int)(sizeof CMDS / sizeof CMDS[0]))

static int kindKnown(int kind) { return kind == 1 || kind == 2 || kind == 3 || kind == 6; }

/* the words a command takes: its table row, or the sub-0 form of a kind (an unknown sub) */
static const ScriptCmd *cmdOf(int word, int *isRaw) {
    int kind = (word >> 8) & 0xFF, sub = word & 0xFF, i;
    const ScriptCmd *zero = NULL;

    *isRaw = 0;
    for (i = 0; i < NCMDS; i++) {
        if (CMDS[i].kind != kind) continue;
        if (CMDS[i].sub == sub) return &CMDS[i];
        if (CMDS[i].sub == 0) zero = &CMDS[i]; /* kind 2 has one form whatever its sub: sub 0's, so another sub is raw */
    }
    if (zero) *isRaw = 1;
    return zero;
}

int scScriptLength(const int16_t *words, int avail) {
    int at = 0;

    while (avail < 0 || at < avail) {
        int w = (uint16_t)words[at], isRaw;
        const ScriptCmd *c;

        if (!kindKnown((w >> 8) & 0xFF)) return at + 1; /* the end word */
        c = cmdOf(w, &isRaw);
        if (!c || (avail >= 0 && at + c->words > avail)) return avail >= 0 ? avail : at;
        at += c->words;
    }
    return at;
}

int scScriptTalks(const int16_t *words, int n, int *offsets, int max) {
    int at = 0, found = 0;

    while (at < n) {
        int w = (uint16_t)words[at], isRaw;
        const ScriptCmd *c;

        if (!kindKnown((w >> 8) & 0xFF)) break;
        c = cmdOf(w, &isRaw);
        if (!c || at + c->words > n) break;
        if (((w >> 8) & 0xFF) == 2) {
            if (found < max) offsets[found] = at;
            found++;
        }
        at += c->words;
    }
    return found;
}

static void putWord(ScBuf *B, int w) { scPrintf(B, ", %d", (int16_t)w); }

int scScriptToJson(const int16_t *words, int n, ScBuf *out) {
    int at = 0;

    scPrintf(out, "[");
    while (at < n) {
        int w = (uint16_t)words[at], isRaw, k;
        const ScriptCmd *c;

        if (at) scPrintf(out, ", ");
        if (!kindKnown((w >> 8) & 0xFF)) {
            if (w == 0) scPrintf(out, "[\"end\"]");
            else scPrintf(out, "[\"end\", \"0x%04X\"]", w);
            at++;
            break;
        }
        c = cmdOf(w, &isRaw);
        if (!c || at + c->words > n) {
            scFree(out); /* a truncated script is a caller's bug */
            out->bad = 1;
            return -1;
        }
        if (isRaw) {
            scPrintf(out, "[\"raw\", \"0x%04X\"", w);
        } else {
            const char *name = c->name;

            if (!strcmp(name, "pose") && (int16_t)words[at + 1] >= 0x320) name = "command"; /* SCRIPT_COMMAND(id, command, arg) */
            scPrintf(out, "[\"%s\"", name);
        }
        for (k = 1; k < c->words; k++) putWord(out, words[at + k]);
        scPrintf(out, "]");
        at += c->words;
    }
    scPrintf(out, "]");
    return (out->bad || at != n) ? -1 : 0;
}

int scScriptFromJson(const ExtJson *list, int16_t **words, int *n, char *err, size_t errCap) {
    int16_t *out;
    int cap, at = 0, i;

    *words = NULL;
    *n = 0;
    if (!list || list->t != 4 || list->n == 0) {
        fail(err, errCap, "a script must be a non-empty array of commands ending with [\"end\"]");
        return -1;
    }
    cap = list->n * 5 + 1;
    out = (int16_t *)malloc((size_t)cap * sizeof *out);
    if (!out) {
        fail(err, errCap, "out of memory");
        return -1;
    }
    for (i = 0; i < list->n; i++) {
        const ExtJson *p = &list->v[i];
        const char *name;
        int j, want = -1, word = 0, argAt = 1;
        long v;

        if (p->t != 4 || p->n < 1 || p->v[0].t != 1 || !p->v[0].str) {
            fail(err, errCap, "command %d is not [\"name\", ...]", i);
            goto bad;
        }
        name = p->v[0].str;
        if (!strcmp(name, "end")) {
            long ew = 0;

            if (p->n > 1 && !(p->n == 2 && p->v[1].t == 1 && p->v[1].str && hexOf(p->v[1].str, &ew) && ew <= 0xFFFF)) {
                fail(err, errCap, "command %d: [\"end\", \"0xHHHH\"] takes one hex word", i);
                goto bad;
            }
            if (kindKnown((int)((ew >> 8) & 0xFF))) {
                fail(err, errCap, "command %d: that word is a command, not an end", i);
                goto bad;
            }
            if (i != list->n - 1) {
                fail(err, errCap, "command %d: \"end\" must be the last command", i);
                goto bad;
            }
            out[at++] = (int16_t)ew;
            *words = out;
            *n = at;
            return 0;
        }
        if (!strcmp(name, "raw")) {
            long rw = 0;
            int isRaw;

            if (p->n < 2 || !(p->v[1].t == 1 && p->v[1].str && hexOf(p->v[1].str, &rw) && rw <= 0xFFFF && kindKnown((int)((rw >> 8) & 0xFF)))) {
                fail(err, errCap, "command %d: [\"raw\", \"0xHHHH\", ...] takes the hex word of a known kind", i);
                goto bad;
            }
            word = (int)rw;
            want = cmdOf(word, &isRaw)->words;
            argAt = 2;
        } else {
            int row = -1, isCommand = !strcmp(name, "command");

            for (j = 0; j < NCMDS; j++) {
                if (!strcmp(CMDS[j].name, isCommand ? "pose" : name)) row = j;
            }
            if (row < 0) {
                fail(err, errCap, "command %d: unknown name \"%s\"", i, name);
                goto bad;
            }
            want = CMDS[row].words;
            word = (CMDS[row].kind << 8) | CMDS[row].sub;
        }
        if (p->n != want - 1 + argAt) {
            fail(err, errCap, "command %d (\"%s\") takes %d arguments", i, name, want - 1);
            goto bad;
        }
        for (j = argAt; j < p->n; j++) {
            if (!wholeIn(&p->v[j], -32768, 65535, &v)) {
                fail(err, errCap, "command %d (\"%s\"): the arguments must be whole numbers", i, name);
                goto bad;
            }
        }
        if (!strcmp(name, "command") && !wholeIn(&p->v[1], 0x320, 32767, &v)) {
            fail(err, errCap, "command %d (\"command\"): the id must be 800 or more", i);
            goto bad;
        }
        if (!strcmp(name, "pose") && wholeIn(&p->v[1], 0x320, 32767, &v)) {
            fail(err, errCap, "command %d (\"pose\"): an id of 800 or more is a \"command\"", i);
            goto bad;
        }
        if (at + want > cap) goto bad;
        out[at++] = (int16_t)word;
        for (j = 0; j < want - 1; j++) out[at++] = (int16_t)(long)p->v[argAt + j].num;
    }
    fail(err, errCap, "the script does not end with [\"end\"]");
bad:
    free(out);
    return -1;
}

/* ---- slots ---- */

static const char *const SLOT_NAMES[] = {NULL, "exit", "climb-up", "climb-down", "drop", "depth", "map", "gauge", "event", "warp-1", "warp-0",
                                         "slide", "stop-slide", "launch", "launch-out"};

const char *scSlotTypeName(int type) {
    return (type >= 1 && type <= 14) ? SLOT_NAMES[type] : NULL;
}

int scSlotTypeByName(const char *name) {
    int i;

    for (i = 1; i <= 14; i++) {
        if (!strcmp(SLOT_NAMES[i], name)) return i;
    }
    return 0;
}
