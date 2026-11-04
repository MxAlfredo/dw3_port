/*
 * New techniques from content mods: see tech_ext.h.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/game_state.h"
#include "ext_state.h"
#include "content.h"
#include "mods.h"
#include "port_log.h"
#include "tech_ext.h"
#include "text_data.h"

#define PC_DIGIMON_ROWS 52 /* the real rows of DIGIMON_DATA (findDigimon looks at 52 of its 55) */
#define SCRIPT_SLOTS 18    /* a fighter's effects archive (R1: a higher index reads memory outside it) */

typedef struct ExtLearn {
    int digimon; /* DigimonData.id */
    int slot;    /* 1..6: DigimonData.skills[slot], skillLevels[slot - 1] */
    int level;
} ExtLearn;

/* the record of a new technique: the base technique's TechData with the overrides, and its learnedBy list */
typedef struct TechRec {
    TechData data;
    int base; /* the original technique id it was copied from */
    ExtLearn *learn;
    int nLearn;
} TechRec;

typedef struct Patched {
    int row, slot;
    u16 skill;
    u8 level;
} Patched;

static int copyBase(int baseId, void *rec);
static int takeFieldOf(const ContentEntry *e, const char *k, const ExtJson *v);
static void freeRec(void *rec);
static int publish(ContentSet *s);
static int describe(int id, char *out, size_t cap);

static const ContentType kTech = {
    "tech", "technique", "techniques", "techs",
    1, PC_TECH_ORIG_COUNT, PC_TECH_FIRST_EXT_ID, PC_EXT_TECH_LAST_ID - PC_TECH_ORIG_COUNT,
    "%d techniques declared; only the first %d (alphabetical) fit in the 13-bit id of a saved skill\n", sizeof(TechRec),
    copyBase, takeFieldOf, freeRec, publish,
    techExtNameOf, techExtIdOf, techExtCount, techExtText,
    pcExtStateSetTechHooks, tdSetTechTextHook,
    "mp, power, accuracy, elementPower, effectChance, effectPower (whole numbers), hitCount, script (0..17), icon, kind, element, "
    "family, effect, scriptStage, scriptEffect, scriptSound (a value some original technique uses), learnedBy {Digimon id: slot, level}; "
    "name and description per language (eng, fra, ita, ger, spa, es-419)",
    describe,
};

static ContentSet sSet = {&kTech, NULL, 0};
static int *sBases; /* per store row: its base id (field healing, ext_state.h) */
static TechData *sRows; /* the store the game reads: sRows[i] is technique id 444 + i */
static Patched *sPatched;
static int sPatchedCount;
static TechExtProbeFn sProbe;
static int sUnchecked;

void techExtSetFighterProbe(TechExtProbeFn probe) {
    sProbe = probe;
}

/* ---- the overridable fields ---- */

enum { F_RANGE, F_ORIGINAL, F_MAXORIG };

typedef struct TField {
    const char *name;
    size_t off;
    int width; /* bytes: 1 or 2 */
    int mode;  /* F_RANGE: 0..hi; F_ORIGINAL: a value some original technique has; F_MAXORIG: 0..the most an original has */
    long hi;
} TField;

static const TField kFields[] = {
    {"mp", offsetof(TechData, mp), 2, F_RANGE, 65535},
    {"power", offsetof(TechData, power), 2, F_RANGE, 65535},
    {"accuracy", offsetof(TechData, accuracy), 1, F_RANGE, 255},
    {"elementPower", offsetof(TechData, elementPower), 1, F_RANGE, 255},
    {"effectChance", offsetof(TechData, effectChance), 1, F_RANGE, 255},
    {"effectPower", offsetof(TechData, effectPower), 1, F_RANGE, 255},
    {"script", offsetof(TechData, script), 1, F_RANGE, SCRIPT_SLOTS - 1},
    {"hitCount", offsetof(TechData, hitCount), 1, F_MAXORIG, 255},
    {"icon", offsetof(TechData, icon), 1, F_ORIGINAL, 255},
    {"kind", offsetof(TechData, kind), 1, F_ORIGINAL, 255},
    {"element", offsetof(TechData, element), 1, F_ORIGINAL, 255},
    {"family", offsetof(TechData, family), 1, F_ORIGINAL, 255},
    {"effect", offsetof(TechData, effect), 1, F_ORIGINAL, 255},
    {"scriptStage", offsetof(TechData, scriptStage), 1, F_ORIGINAL, 255},
    {"scriptEffect", offsetof(TechData, scriptEffect), 1, F_ORIGINAL, 255},
    {"scriptSound", offsetof(TechData, scriptSound), 1, F_ORIGINAL, 255},
};

static long fieldValue(const TechData *t, const TField *f) {
    const unsigned char *p = (const unsigned char *)t + f->off;
    return f->width == 2 ? (long)(p[0] | (p[1] << 8)) : (long)p[0];
}

static void setField(TechData *t, const TField *f, long v) {
    unsigned char *p = (unsigned char *)t + f->off;
    p[0] = (unsigned char)v;
    if (f->width == 2) p[1] = (unsigned char)(v >> 8);
}

/* the values of a field across the original table: whether `v` is one, and the biggest */
static int usedByOriginal(const TField *f, long v, long *max) {
    int i, found = 0;
    long m = 0;

    for (i = 0; i < PC_TECH_ORIG_COUNT; i++) {
        long x = fieldValue(&PC_TECH(i), f);
        if (x == v) found = 1;
        if (x > m) m = x;
    }
    if (max != NULL) *max = m;
    return found;
}

/* ---- loading ---- */

static void takeField(const ContentEntry *e, const TField *f, const ExtJson *v) {
    TechRec *t = e->rec;
    long max = 0, n;
    int ok;

    if (v->t != 0 || !extWhole(v->num, 0, f->hi)) {
        plogLine("techs", "%s tech \"%s\" field \"%s\" rejected: a whole number 0..%ld\n", e->path, e->name, f->name, f->hi);
        return;
    }
    n = (long)v->num;
    if (f->mode == F_ORIGINAL) {
        ok = usedByOriginal(f, n, NULL);
        if (!ok) {
            plogLine("techs", "%s tech \"%s\" field \"%s\" rejected: %ld is not a value any original technique uses (it indexes a game table)\n",
                     e->path, e->name, f->name, n);
            return;
        }
    } else if (f->mode == F_MAXORIG) {
        usedByOriginal(f, n, &max);
        if (n > max) {
            plogLine("techs", "%s tech \"%s\" field \"%s\" rejected: 0..%ld (the most an original technique has)\n", e->path, e->name,
                     f->name, max);
            return;
        }
    }
    setField(&t->data, f, n);
}

/* the "learnedBy" object: Digimon id -> {slot, level}; what can be checked without the table */
static void takeLearn(const ContentEntry *e, const ExtJson *o) {
    TechRec *t = e->rec;
    int i;

    if (o->t != 2) {
        plogLine("techs", "%s tech \"%s\" field \"learnedBy\" rejected: an object of Digimon ids\n", e->path, e->name);
        return;
    }
    for (i = 0; i < o->n; i++) {
        const char *key = o->keys[i];
        const ExtJson *v = &o->v[i], *slot, *level;
        char *end;
        long id;
        ExtLearn *grown;

        if (!strncmp(key, "dw3:", 4)) {
            id = contentNamedId("digimon", key);
            end = (char *)"";
        } else {
            id = strtol(key, &end, 10);
        }
        if (key[0] == '\0' || *end != '\0' || id < 1 || id > 65535) {
            plogLine("techs", "%s tech \"%s\" learnedBy \"%s\" rejected: the key is a Digimon (a number or \"dw3:digimon/<id>\")\n",
                     e->path, e->name, key);
            continue;
        }
        slot = extJsonGet(v, "slot");
        level = extJsonGet(v, "level");
        if (slot == NULL || level == NULL) {
            plogLine("techs", "%s tech \"%s\" learnedBy %ld rejected: it needs \"slot\" and \"level\"\n", e->path, e->name, id);
            continue;
        }
        if (slot->t != 0 || !extWhole(slot->num, 1, 6)) {
            plogLine("techs", "%s tech \"%s\" learnedBy %ld rejected: slot must be 1..6\n", e->path, e->name, id);
            continue;
        }
        if (level->t != 0 || !extWhole(level->num, 1, 99)) {
            plogLine("techs", "%s tech \"%s\" learnedBy %ld rejected: level must be 1..99\n", e->path, e->name, id);
            continue;
        }
        grown = realloc(t->learn, sizeof(ExtLearn) * (size_t)(t->nLearn + 1));
        if (grown == NULL) return;
        t->learn = grown;
        grown[t->nLearn].digimon = (int)id;
        grown[t->nLearn].slot = (int)slot->num;
        grown[t->nLearn].level = (int)level->num;
        t->nLearn++;
    }
}

static int copyBase(int baseId, void *rec) {
    ((TechRec *)rec)->data = PC_TECH(baseId - 1);
    ((TechRec *)rec)->base = baseId;
    return 1;
}

static void freeRec(void *rec) {
    free(((TechRec *)rec)->learn);
}

static int takeFieldOf(const ContentEntry *e, const char *k, const ExtJson *v) {
    int j;

    if (!strcmp(k, "learnedBy")) {
        takeLearn(e, v);
        return 1;
    }
    for (j = 0; j < (int)(sizeof kFields / sizeof kFields[0]); j++) {
        if (!strcmp(k, kFields[j].name)) {
            takeField(e, &kFields[j], v);
            return 1;
        }
    }
    return 0;
}

/* the settable fields of an original technique (the reference export) */
static int describe(int id, char *out, size_t cap) {
    const TechData *d;
    size_t n = 0;
    int j;

    if (id < 1 || id > PC_TECH_ORIG_COUNT) return 0;
    d = &PC_TECH(id - 1);
    out[0] = '\0';
    for (j = 0; j < (int)(sizeof kFields / sizeof kFields[0]) && n < cap; j++) {
        int w = snprintf(out + n, cap - n, "%s\"%s\": %ld", j ? ", " : "", kFields[j].name, fieldValue(d, &kFields[j]));
        if (w < 0 || (size_t)w >= cap - n) break;
        n += (size_t)w;
    }
    return 1;
}

const ContentType *techExtType(void) {
    return &kTech;
}

/* the script slots a technique needs in a fighter's effects archive: its own script, and the ones a partner's battle
   code can switch to (techs.c choosePartnerScript: 5 -> 6 or 8; 6 -> 8) */
static int firstMissingSlot(int digimon, int script, int partner) {
    int need[3], n = 0, i;

    need[n++] = script;
    if (!partner) {
        /* an enemy only plays its own script */
    } else if (script == 5) {
        need[n++] = 6;
        need[n++] = 8;
    } else if (script == 6) {
        need[n++] = 8;
    }
    for (i = 0; i < n; i++) {
        int r = sProbe != NULL ? sProbe(digimon, need[i]) : -1;
        if (r == 0) return need[i];
        if (r < 0) sUnchecked = 1;
    }
    return -1;
}

int techExtMissingSlot(int digimon, int script, int partner) {
    return firstMissingSlot(digimon, script, partner);
}

int techExtHasSlot(int digimon, int slot) {
    return sProbe != NULL ? sProbe(digimon, slot) : -1;
}

int techExtResolve(const char *name) {
    return contentResolve(&sSet, name);
}

static int digimonRow(int id) {
    int i;
    for (i = 0; i < PC_DIGIMON_ROWS; i++) if (PC_DIGIMON(i).id == id) return i;
    return -1;
}

static void restoreLearn(void) {
    int i;
    for (i = sPatchedCount - 1; i >= 0; i--) {
        PC_DIGIMON(sPatched[i].row).skills[sPatched[i].slot] = sPatched[i].skill;
        PC_DIGIMON(sPatched[i].row).skillLevels[sPatched[i].slot - 1] = sPatched[i].level;
    }
    free(sPatched);
    sPatched = NULL;
    sPatchedCount = 0;
}

/* learnedBy of every technique, in id order: the first claim on a Digimon's slot wins */
static void applyLearn(void) {
    int i, j, total = 0;

    for (i = 0; i < sSet.n; i++) total += ((const TechRec *)sSet.e[i].rec)->nLearn;
    if (total == 0) return;
    sPatched = malloc(sizeof(Patched) * (size_t)total);
    if (sPatched == NULL) return;
    for (i = 0; i < sSet.n; i++) {
        const ContentEntry *e = &sSet.e[i];
        const TechRec *rec = e->rec;

        for (j = 0; j < rec->nLearn; j++) {
            const ExtLearn *l = &rec->learn[j];
            int row = digimonRow(l->digimon), k, miss, taken = -1;

            if (row < 0) {
                plogLine("techs", "%s tech \"%s\" learnedBy %d rejected: no Digimon %d in the table\n", e->path, e->name,
                         l->digimon, l->digimon);
                continue;
            }
            for (k = 0; k < sPatchedCount; k++) if (sPatched[k].row == row && sPatched[k].slot == l->slot) taken = k;
            if (taken >= 0) {
                plogLine("techs", "%s tech \"%s\" learnedBy %d rejected: skill slot %d of that Digimon is already taken by \"%s\"\n",
                         e->path, e->name, l->digimon, l->slot, techExtNameOf(PC_DIGIMON(row).skills[l->slot]));
                continue;
            }
            miss = firstMissingSlot(l->digimon, rec->data.script, 1);
            if (miss >= 0) {
                plogLine("techs",
                         "%s tech \"%s\" learnedBy %d rejected: its fighter has no script slot %d in its effects archive "
                         "(the technique's animation is script %d)\n",
                         e->path, e->name, l->digimon, miss, (int)rec->data.script);
                continue;
            }
            sPatched[sPatchedCount].row = row;
            sPatched[sPatchedCount].slot = l->slot;
            sPatched[sPatchedCount].skill = PC_DIGIMON(row).skills[l->slot];
            sPatched[sPatchedCount].level = PC_DIGIMON(row).skillLevels[l->slot - 1];
            sPatchedCount++;
            PC_DIGIMON(row).skills[l->slot] = (u16)(PC_TECH_FIRST_EXT_ID + i);
            PC_DIGIMON(row).skillLevels[l->slot - 1] = (u8)l->level;
            PLOG("techs", PLOG_MODS, "tech %s is learned by Digimon %d in skill slot %d at level %d (replaces %d)\n", e->name,
                 l->digimon, l->slot, l->level, (int)sPatched[sPatchedCount - 1].skill);
        }
    }
    if (sUnchecked) {
        plogLine("techs", "learnedBy was accepted without the animation check for some Digimon: the fighters file is not available\n");
    }
}

/* the store the game reads (PC_TECH), the hooks, and learnedBy */
static int publish(ContentSet *s) {
    int i;

    sRows = malloc(sizeof(TechData) * (size_t)s->n);
    sBases = malloc(sizeof(int) * (size_t)s->n);
    if (sRows == NULL || sBases == NULL) {
        free(sRows);
        free(sBases);
        sRows = NULL;
        sBases = NULL;
        return 0;
    }
    for (i = 0; i < s->n; i++) {
        sRows[i] = ((const TechRec *)s->e[i].rec)->data;
        sBases[i] = ((const TechRec *)s->e[i].rec)->base;
    }
    pcExtStateSetTechStore(sRows, s->n);
    pcExtStateSetTechBases(sBases, s->n);
    contentAttach(s);
    applyLearn();
    return 1;
}

void techExtReset(void) {
    restoreLearn();
    pcExtStateSetTechStore(NULL, 0);
    pcExtStateSetTechBases(NULL, 0);
    free(sRows);
    free(sBases);
    sRows = NULL;
    sBases = NULL;
    contentUnload(&sSet);
    sUnchecked = 0;
}

int techExtLoad(void) {
    techExtReset();
    return contentLoad(&sSet);
}

int techExtCount(void) {
    return contentCount(&sSet);
}

int techExtIdOf(const char *name) {
    return contentIdOf(&sSet, name);
}

const char *techExtNameOf(int id) {
    return contentNameOf(&sSet, id);
}

const char *techExtText(int index, const char *langDir, int desc) {
    return contentText(&sSet, index, langDir, desc);
}

/* What the field Techniques screen listed for a party member (STSTATUS_listFieldTechs): the proof for headless runs */
void pcFieldTechsListed(s32 member, s32 count, const s16 *techs) {
    int i;

    if (!plogOn("techs", PLOG_MODS)) return;
    plogLine("techs", "field Techniques screen: party member %d lists %d:", (int)member, (int)count);
    for (i = 0; i < count; i++) {
        const char *n = techs[i] >= PC_TECH_FIRST_EXT_ID ? techExtNameOf(techs[i]) : NULL;
        plogRaw(" %d%s%s", (int)techs[i], n != NULL ? " " : "", n != NULL ? n : "");
    }
    plogRaw("\n");
}
