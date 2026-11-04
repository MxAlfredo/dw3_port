/*
 * Changes to existing Digimon from content mods: see digimon_ext.h.
 */

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/game_state.h"
#include "content.h"
#include "digimon_ext.h"
#include "ext_state.h"
#include "item_ext.h"
#include "model_bake.h"
#include "mods.h"
#include "port_fs.h"
#include "port_log.h"
#include "tech_ext.h"
#include "text_data.h"

#define DIGIMON_ROWS 55 /* DIGIMON_DATA rows: 52 real forms and 3 empty placeholders */
#define REAL_ROWS 52    /* what findDigimon looks at */
#define ENEMY_MAX 256
#define REC_BYTES 96 /* >= sizeof(DigimonData) and sizeof(BattleTableEntry) */
#define ID_MIN 3
#define ID_MAX 470
#define FIRST_NEW_ID PC_DIGIMON_FIRST_NEW_ID /* the first id of a new Digimon (enemy or partner form): above every original id */
#define LAST_ID 32767           /* BattleTableEntry.id, FighterEntry.id and BattleFighter.id are s16 */
#define DIGNAM_ORIG_COUNT 251   /* entries of the name file (DIGNAM) on the disc, the same in every language */
#define ENCOUNTER_ROWS 335      /* FIELDSTG_encounters (docs/data-tables.md section 3.5) */
#define ENEMY_KIND_MIN 0x3A     /* FighterEntry.kind from this on: an enemy (fighter_info.c) */

enum { FACET_PARTNER = 1, FACET_ENEMY = 2 };

static int describe(int id, char *out, size_t cap);
static int copyBase(int baseId, void *rec);
static int newField(const ContentEntry *e, const char *key, const ExtJson *v);
static int publish(ContentSet *s);
static const char *newNameOf(int id);
static int newIdOf(const char *name);
static int newCount(void);
static const char *newText(int index, const char *langDir, int desc);
static void setNameHooks(const char *(*nameOf)(int), int (*idOf)(const char *));
static int baseOfNew(int id);

/* a new Digimon: an enemy is the base's battle table row with the overrides, a partner form the base's DigimonData with them;
   either shares the model, motions and effects of an original fighter of its own facet */
#define EVO_MAX 4 /* evolution entries one new form declares (one per partner that can learn it) */
#define EVO_NAME 96

/* one declared way to get a new form: an Evolution of the partner's list, with the Digimon of the requirements still by name */
typedef struct EvoDecl {
    int partner;                     /* 0..7 */
    int nNeeds;                      /* 0..2 */
    char need[2][EVO_NAME];          /* "dw3:digimon/<id>" or "<mod>:<key>" */
    int needLevel[2];                /* 1..99 */
    int stat;                        /* 0 none, 1..6 the battle stats, 7 level, 8..14 resistances */
    int value;                       /* the least the stat must be */
} EvoDecl;

/* one of the three file references of a FighterInfo ((file << 16) | entry), as declared in a "model" object */
enum { REF_TEMPLATE = 0, REF_FILE, REF_NAME, REF_FIGHTER };
typedef struct FileRef {
    int kind;
    int file;            /* REF_FILE: the number; REF_FIGHTER: the original fighter id the ref is taken from */
    int entry;           /* REF_FILE, REF_NAME: the entry of the archive */
    char name[EVO_NAME]; /* REF_NAME: "<mod>:<key>" or "dw3:model/<id>", resolved when the fighters file is built */
} FileRef;

/* "model": {...}: the new Digimon's own FighterInfo row, from the template fighter's with these replaced */
typedef struct OwnModel {
    int own;
    FileRef ref[3]; /* model, motions, effects */
    int height, distance; /* the value, or INT_MIN: the template's */
} OwnModel;

typedef struct NewRec {
    unsigned char row[REC_BYTES]; /* enemy: BattleTableEntry (PC_BATTLE_ROW_SIZE bytes); partner: DigimonData */
    int facet;
    int baseId;
    int model; /* the id of an original fighter of the same facet */
    int nBlast;                  /* 5 when "blastForms" is declared, else 0 */
    char blast[5][EVO_NAME];     /* "dw3:digimon/<id>" or "<mod>:<key>", resolved in publish (U9) */
    int hasPair;                 /* "pairPartner" declared: pair is a name, or "" for none */
    char pair[EVO_NAME];
    int nEvo;
    EvoDecl evo[EVO_MAX]; /* partner forms: how a partner gets the form (Evolution lists, P3) */
    OwnModel own;         /* a "model" object: its own FighterInfo row (the template is `model`) */
} NewRec;
_Static_assert(sizeof(DigimonData) <= REC_BYTES, "NewRec.row holds a DigimonData");

static const ContentType kDigimon = {
    "digimon", "Digimon", "Digimon", "digimon", ID_MIN, ID_MAX, FIRST_NEW_ID, LAST_ID - FIRST_NEW_ID + 1, NULL, sizeof(NewRec),
    copyBase, newField, NULL, publish,
    newNameOf, newIdOf, newCount, newText, setNameHooks, tdSetDigimonTextHook,
    "partner: hp, mp, hpGrowth, mpGrowth, expRate, expLevel (0..255), family (a value some original Digimon has), "
    "stats {strength, defense, spirit, wisdom, speed, charisma} (0..999), resistances {0..6} (0..999), statusResists {poison, "
    "paralysis, confusion, sleep, knockOut} (0..255), statGrowth {strength, defense, spirit, wisdom, speed, charisma} (0..5), "
    "resistGrowth {0..6} (1..5), knownLevels {0..4} (1..99), skills {1..6: {tech, level}}; enemy: stats {attack, defense, spirit, "
    "wisdom, speed}, resistances {0..6}, statusResists {poison, paralysis, confusion, sleep, knockOut} (0..32767), family, techs "
    "{1..3}, item, itemChance (0..1023); a new Digimon (\"new\") takes the fields of its base's facet, model (an original fighter "
    "of the same facet), name and description, and \"encounters\" places enemies in encounters; a new partner form also takes "
    "evolution (how a partner gets it after a battle: partner, needs {digimon, level} up to 2, stat, value), blastForms (five partner "
    "forms by name, what a blast turns it into at each level tier) and pairPartner (a partner form by name, or 0 for none); scriptSlots (reference "
    "only) lists the animation slots the fighter has: a technique whose script is not there cannot be used by it",
    describe,
};

static ContentSet sSet = {&kDigimon, NULL, 0};

const ContentType *digimonExtType(void) {
    return &kDigimon;
}

/* ---- the original battle table ---- */

/* BattleTableEntry as bytes (the game header cannot be included here; boot.c checks the layout) */
#define BT_SIZE PC_BATTLE_ROW_SIZE
#define BT_ID 0x00
#define BT_ITEM 0x02
#define BT_CHANCE 0x04
#define BT_NAME 0x06
#define BT_TECHS 0x08
#define BT_STATS 0x0E
#define BT_RESIST 0x18
#define BT_FAMILY 0x30

static unsigned char sEnemyOrig[ENEMY_MAX][BT_SIZE];
static int sEnemyCount;
static unsigned char *sBattleFile; /* the whole original file: the extended one is built from it */
static size_t sBattleLen;
static unsigned char *sFightFile; /* FILE_FIGHTERS as on the disc */
static size_t sFightLen;

static int s16At(const unsigned char *p, size_t off) {
    return (short)(p[off] | (p[off + 1] << 8));
}

void digimonExtSetEnemyTable(const unsigned char *file, size_t len) {
    size_t i;

    sEnemyCount = 0;
    free(sBattleFile);
    sBattleFile = NULL;
    sBattleLen = 0;
    if (file == NULL) return;
    sBattleFile = malloc(len);
    if (sBattleFile != NULL) {
        memcpy(sBattleFile, file, len);
        sBattleLen = len;
    }
    for (i = 0; (i + 1) * BT_SIZE <= len && sEnemyCount < ENEMY_MAX; i++) {
        if (s16At(file + i * BT_SIZE, BT_ID) == 0) break;
        memcpy(sEnemyOrig[sEnemyCount++], file + i * BT_SIZE, BT_SIZE);
    }
}

/* FILE_FIGHTERS: {s32 base, s32 entries, s32 partners, s32 enemies} then, at `entries`, FighterEntry {s16 id, u8 index, u8 kind}
   up to an id of 0 (include/battle/battle_mode/model.h, fighter_info.c) */
#define FH_ENTRIES 4
#define FE_SIZE 4

static unsigned le32At(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

void digimonExtSetFighters(const unsigned char *file, size_t len) {
    free(sFightFile);
    sFightFile = NULL;
    sFightLen = 0;
    if (file == NULL || len < 16) return;
    sFightFile = malloc(len);
    if (sFightFile != NULL) {
        memcpy(sFightFile, file, len);
        sFightLen = len;
    }
}

/* the fighter entries of the original file: how many (up to the id 0), and entry i */
static int fighterCount(void) {
    size_t at;
    int n = 0;

    if (sFightFile == NULL) return 0;
    at = le32At(sFightFile + FH_ENTRIES);
    while (at + FE_SIZE <= sFightLen && s16At(sFightFile, at) != 0) {
        n++;
        at += FE_SIZE;
    }
    return n;
}

static const unsigned char *fighterEntry(int i) {
    return sFightFile + le32At(sFightFile + FH_ENTRIES) + (size_t)i * FE_SIZE;
}

/* the entry of an original fighter id, or NULL */
static const unsigned char *fighterOf(int id) {
    int i, n = fighterCount();

    for (i = 0; i < n; i++) if (s16At(fighterEntry(i), 0) == id) return fighterEntry(i);
    return NULL;
}

static const unsigned char *enemyOf(int id) {
    int i;
    for (i = 0; i < sEnemyCount; i++) if (s16At(sEnemyOrig[i], BT_ID) == id) return sEnemyOrig[i];
    return NULL;
}

/* ---- partner rows: the live table, and the rows as they were before any patch ---- */

typedef struct Saved {
    int row;
    DigimonData orig;
} Saved;

static Saved sSaved[DIGIMON_ROWS];
static int sSavedCount;

static int rowOf(int id) {
    int i;
    for (i = 0; id >= ID_MIN && i < DIGIMON_ROWS; i++) if (PC_DIGIMON(i).id == id) return i;
    return -1;
}

static const DigimonData *originalRow(int row) {
    int i;
    for (i = 0; i < sSavedCount; i++) if (sSaved[i].row == row) return &sSaved[i].orig;
    return &PC_DIGIMON(row);
}

int digimonExtNameId(int id) {
    int row = rowOf(id);
    const unsigned char *e;

    if (id >= FIRST_NEW_ID) return contentNameOf(&sSet, id) != NULL ? DIGNAM_ORIG_COUNT + (id - FIRST_NEW_ID) : -1;

    if (row >= 0) return originalRow(row)->nameId;
    e = enemyOf(id);
    return e != NULL ? s16At(e, BT_NAME) : -1;
}

/* ---- the fields ---- */

typedef struct Field {
    const char *name;
    const char *const *keys; /* sub-keys of a group; NULL with count > 0: numeric keys "0".. */
    int count;               /* group size (0: a scalar) */
    size_t off;              /* byte offset of the (first) value in the record */
    int width;               /* 1 or 2 bytes */
    long hi;
    int original; /* the value must be one an original row has (it indexes a game table) */
    long lo;      /* the lowest value (0 when left out) */
} Field;

static const char *const kStatNames[] = {"strength", "defense", "spirit", "wisdom", "speed", "charisma"};
static const char *const kEnemyStatNames[] = {"attack", "defense", "spirit", "wisdom", "speed"};
static const char *const kStatusNames[] = {"poison", "paralysis", "confusion", "sleep", "knockOut"};

#define PO(m) offsetof(DigimonData, m)

static const Field kPartnerFields[] = {
    {"hp", NULL, 0, PO(hp), 1, 255, 0},
    {"mp", NULL, 0, PO(mp), 1, 255, 0},
    {"hpGrowth", NULL, 0, PO(hpGrowth), 1, 255, 0},
    {"mpGrowth", NULL, 0, PO(mpGrowth), 1, 255, 0},
    {"expRate", NULL, 0, PO(expRate), 1, 255, 0},
    {"expLevel", NULL, 0, PO(expLevel), 1, 255, 0},
    {"family", NULL, 0, PO(family), 1, 255, 1},
    {"stats", kStatNames, 6, PO(battleStats), 2, 999, 0},
    {"resistances", NULL, 7, PO(resistances), 2, 999, 0},
    {"statusResists", kStatusNames, 5, PO(statusResists), 1, 255, 0},
    /* growth: statGrowth[i] is the column of STFGTREP_statGains[level tier] the battle stat i gains from (0..5: the disc has up
       to 5, whose read goes one past the row, see docs/data-tables.md); resistGrowth[i] picks the gym tables
       STGTRAIN_resistGainTables[(g - 1) * 3 + bracket] and the battle column of resist_gains (1..5, 0 would index before the
       table); knownLevels[i] is the level that marks skills[i] known (no table) */
    {"statGrowth", kStatNames, 6, PO(statGrowth), 1, 5, 0, 0},
    {"resistGrowth", NULL, 7, PO(resistGrowth), 1, 5, 0, 1},
    {"knownLevels", NULL, 5, PO(knownLevels), 1, 99, 0, 1},
};

static const Field kEnemyFields[] = {
    {"family", NULL, 0, BT_FAMILY, 1, 255, 1},
    {"itemChance", NULL, 0, BT_CHANCE, 2, 1023, 0},
    {"stats", kEnemyStatNames, 5, BT_STATS, 2, 32767, 0},
    {"resistances", NULL, 7, BT_RESIST, 2, 32767, 0},
    {"statusResists", kStatusNames, 5, BT_RESIST + 7 * 2, 2, 32767, 0},
};

#define NPF ((int)(sizeof kPartnerFields / sizeof kPartnerFields[0]))
#define NEF ((int)(sizeof kEnemyFields / sizeof kEnemyFields[0]))

static long readAt(const void *rec, size_t off, int width) {
    const unsigned char *p = (const unsigned char *)rec + off;
    return width == 2 ? (long)(p[0] | (p[1] << 8)) : (long)p[0];
}

static void writeAt(unsigned char *val, unsigned char *mask, size_t off, int width, long v) {
    int i;
    for (i = 0; i < width; i++) {
        val[off + (size_t)i] = (unsigned char)(v >> (8 * i));
        mask[off + (size_t)i] = 1;
    }
}

/* whether some original row of the facet has `v` in that field */
static int usedByOriginal(int facet, size_t off, int width, long v) {
    int i;

    if (facet == FACET_PARTNER) {
        for (i = 0; i < REAL_ROWS; i++) if (readAt(originalRow(i), off, width) == v) return 1;
    } else {
        for (i = 0; i < sEnemyCount; i++) if (readAt(sEnemyOrig[i], off, width) == v) return 1;
    }
    return 0;
}

/* ---- the records of the mods ---- */

typedef struct DRec {
    int id;
    char name[100]; /* what the log calls it: "dw3:digimon/<id>" or the "<mod>:<key>" of a new enemy */
    int probe;      /* the fighter whose effects archive the technique animation check looks at */
    int defer;      /* 1: techniques are checked later, once the entry's model is known (new enemies) */
    int facet;
    unsigned char val[REC_BYTES];
    unsigned char mask[REC_BYTES];
    char note[360]; /* what changed, for the log */
} DRec;

static DRec *sRecs;
static int sRecCount;
static int sEnemyRecs;

static void note(DRec *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void note(DRec *r, const char *fmt, ...) {
    size_t n = strlen(r->note);
    va_list ap;

    if (n + 2 >= sizeof r->note) return;
    if (n > 0) {
        r->note[n++] = ',';
        r->note[n++] = ' ';
        r->note[n] = '\0';
    }
    va_start(ap, fmt);
    vsnprintf(r->note + n, sizeof r->note - n, fmt, ap);
    va_end(ap);
}

static DRec *recFor(int id, int facet) {
    DRec *grown;
    int i;

    for (i = 0; i < sRecCount; i++) if (sRecs[i].id == id && sRecs[i].facet == facet) return &sRecs[i];
    grown = realloc(sRecs, sizeof(DRec) * (size_t)(sRecCount + 1));
    if (grown == NULL) return NULL;
    sRecs = grown;
    memset(&sRecs[sRecCount], 0, sizeof(DRec));
    sRecs[sRecCount].id = id;
    sRecs[sRecCount].facet = facet;
    return &sRecs[sRecCount++];
}

static void reject(const char *path, const DRec *r, const char *field, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void reject(const char *path, const DRec *r, const char *field, const char *fmt, ...) {
    char why[200];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    plogLine("digimon", "%s digimon \"%s\" field \"%s\" rejected: %s\n", path, r->name, field, why);
}

/* a technique: a number (0, or an original 1..443), "dw3:tech/<id>" or "<mod>:<key>"; -1 when it is none of those */
static int resolveTech(const ExtJson *v) {
    if (v->t == 0) {
        if (!extWhole(v->num, 0, PC_TECH_FIRST_EXT_ID - 1)) return -1;
        return (int)v->num;
    }
    if (v->t == 1 && v->str != NULL) return techExtResolve(v->str);
    return -1;
}

/* the animation check shared with learnedBy; 1 ok, 0 rejected (logged) */
static int techFits(const char *path, const DRec *r, const char *field, int tech, int partner) {
    int miss;

    if (tech <= 0) return 1;
    miss = techExtMissingSlot(r->probe, PC_TECH(tech - 1).script, partner);
    if (miss >= 0) {
        reject(path, r, field, "its fighter has no script slot %d in its effects archive (the technique's animation is script %d)",
               miss, (int)PC_TECH(tech - 1).script);
        return 0;
    }
    return 1;
}

static int subIndex(const Field *f, const char *key) {
    int i;

    if (f->keys != NULL) {
        for (i = 0; i < f->count; i++) if (!strcmp(f->keys[i], key)) return i;
        return -1;
    }
    if (key[0] >= '0' && key[0] <= '9' && key[1] == '\0' && key[0] - '0' < f->count) return key[0] - '0';
    return -1;
}

static void takeValue(DRec *r, const char *path, const Field *f, const char *label, size_t off, const ExtJson *v) {
    long n;

    if (v->t != 0 || !extWhole(v->num, f->lo, f->hi)) {
        reject(path, r, label, "a whole number %ld..%ld", f->lo, f->hi);
        return;
    }
    n = (long)v->num;
    if (f->original && !usedByOriginal(r->facet, f->off, f->width, n)) {
        reject(path, r, label, "%ld is not a value any original %s has (it indexes a game table)", n,
               r->facet == FACET_PARTNER ? "partner form" : "enemy");
        return;
    }
    writeAt(r->val, r->mask, off, f->width, n);
    note(r, "%s=%ld", label, n);
}

static void takeField(DRec *r, const char *path, const Field *f, const ExtJson *v) {
    int i;

    if (f->count == 0) {
        takeValue(r, path, f, f->name, f->off, v);
        return;
    }
    if (v->t != 2) {
        reject(path, r, f->name, "an object of values");
        return;
    }
    for (i = 0; i < v->n; i++) {
        char label[64];
        int k = subIndex(f, v->keys[i]);

        snprintf(label, sizeof label, "%s.%s", f->name, v->keys[i]);
        if (k < 0) {
            reject(path, r, label, "unknown key");
            continue;
        }
        takeValue(r, path, f, label, f->off + (size_t)k * (size_t)f->width, &v->v[i]);
    }
}

/* partner "skills": {"1".."6": {"tech": .., "level": 1..99}} */
static void takeSkills(DRec *r, const char *path, const ExtJson *o) {
    int i;

    if (o->t != 2) {
        reject(path, r, "skills", "an object of skill slots 1..6");
        return;
    }
    for (i = 0; i < o->n; i++) {
        const char *key = o->keys[i];
        const ExtJson *v = &o->v[i], *tech, *level;
        char label[48];
        int slot = (key[0] >= '1' && key[0] <= '6' && key[1] == '\0') ? key[0] - '0' : 0, id = -1, lv = -1, ok = 1;

        snprintf(label, sizeof label, "skills.%s", key);
        if (slot == 0) {
            reject(path, r, label, "the slot is 1..6");
            continue;
        }
        tech = extJsonGet(v, "tech");
        level = extJsonGet(v, "level");
        if (v->t != 2 || (tech == NULL && level == NULL)) {
            reject(path, r, label, "an object with \"tech\" and/or \"level\"");
            continue;
        }
        if (tech != NULL) {
            id = resolveTech(tech);
            if (id < 0) {
                reject(path, r, label, "\"tech\" is not a technique (0, 1..%d, \"dw3:tech/<id>\" or \"<mod>:<key>\")",
                       PC_TECH_ORIG_COUNT);
                ok = 0;
            } else if (!r->defer && !techFits(path, r, label, id, 1)) {
                ok = 0;
            }
        }
        if (level != NULL) {
            if (level->t != 0 || !extWhole(level->num, 1, 99)) {
                reject(path, r, label, "\"level\" must be 1..99");
                ok = 0;
            } else {
                lv = (int)level->num;
            }
        }
        if (!ok) continue;
        if (id >= 0) {
            writeAt(r->val, r->mask, PO(skills) + (size_t)slot * 2, 2, id);
            note(r, "skills.%d.tech=%d", slot, id);
        }
        if (lv >= 0) {
            writeAt(r->val, r->mask, PO(skillLevels) + (size_t)(slot - 1), 1, lv);
            note(r, "skills.%d.level=%d", slot, lv);
        }
    }
}

/* enemy "techs": {"1".."3": tech}; the first one cannot be empty */
static void takeTechs(DRec *r, const char *path, const ExtJson *o) {
    int i;

    if (o->t != 2) {
        reject(path, r, "techs", "an object of technique slots 1..3");
        return;
    }
    for (i = 0; i < o->n; i++) {
        const char *key = o->keys[i];
        char label[48];
        int slot = (key[0] >= '1' && key[0] <= '3' && key[1] == '\0') ? key[0] - '0' : 0, id;

        snprintf(label, sizeof label, "techs.%s", key);
        if (slot == 0) {
            reject(path, r, label, "the slot is 1..3");
            continue;
        }
        id = resolveTech(&o->v[i]);
        if (id < 0 || (slot == 1 && id == 0)) {
            reject(path, r, label, "not a technique (%s, 1..%d, \"dw3:tech/<id>\" or \"<mod>:<key>\")",
                   slot == 1 ? "the first one cannot be 0" : "0 for none", PC_TECH_ORIG_COUNT);
            continue;
        }
        if (!r->defer && !techFits(path, r, label, id, 0)) continue;
        writeAt(r->val, r->mask, BT_TECHS + (size_t)(slot - 1) * 2, 2, id);
        note(r, "techs.%d=%d", slot, id);
    }
}

/* enemy "item": 0, an item id (1..402, or the id of an item a mod adds) or a name */
static void takeItem(DRec *r, const char *path, const ExtJson *v) {
    long long id = -1;

    if (v->t == 0 && extWhole(v->num, 0, 32767)) id = (long long)v->num;
    else if (v->t == 1 && v->str != NULL) id = itemExtByName(v->str);
    if (id < 0 || (id > 0 && !itemExtIdOk(id))) {
        reject(path, r, "item", "not an item (0, 1..402, \"dw3:item/<id>\" or \"<mod>:<key>\")");
        return;
    }
    writeAt(r->val, r->mask, BT_ITEM, 2, (long)id);
    note(r, "item=%lld", id);
}

/* One key of a facet's object into `scratch`: 1 when the key is a field of the facet (accepted or rejected with its own log
   line), 0 when it is none */
static int takeKey(DRec *scratch, const char *path, const char *k, const ExtJson *v) {
    int j;
    const Field *fields = scratch->facet == FACET_PARTNER ? kPartnerFields : kEnemyFields;
    int nf = scratch->facet == FACET_PARTNER ? NPF : NEF;

    if (scratch->facet == FACET_PARTNER && !strcmp(k, "skills")) {
        takeSkills(scratch, path, v);
        return 1;
    }
    if (scratch->facet == FACET_ENEMY && !strcmp(k, "techs")) {
        takeTechs(scratch, path, v);
        return 1;
    }
    if (scratch->facet == FACET_ENEMY && !strcmp(k, "item")) {
        takeItem(scratch, path, v);
        return 1;
    }
    for (j = 0; j < nf; j++) if (!strcmp(k, fields[j].name)) break;
    if (j == nf) return 0;
    takeField(scratch, path, &fields[j], v);
    return 1;
}

static void initScratch(DRec *r, int id, int facet) {
    memset(r, 0, sizeof *r);
    r->id = id;
    r->probe = id;
    r->facet = facet;
    snprintf(r->name, sizeof r->name, "dw3:digimon/%d", id);
}

static void takeFacet(int id, int facet, const char *path, const ExtJson *o) {
    DRec scratch, *r;
    int i;

    initScratch(&scratch, id, facet);
    if (o->t != 2) {
        reject(path, &scratch, facet == FACET_PARTNER ? "partner" : "enemy", "an object of fields");
        return;
    }
    for (i = 0; i < o->n; i++) {
        if (!takeKey(&scratch, path, o->keys[i], &o->v[i])) {
            reject(path, &scratch, o->keys[i], "unknown field (nameId, the AI and the like are not editable)");
        }
    }
    /* merge into the fighter's record: a later mod wins field by field */
    for (i = 0; i < REC_BYTES && scratch.mask[i] == 0; i++) {
    }
    if (i == REC_BYTES) return;
    r = recFor(id, facet);
    if (r == NULL) return;
    snprintf(r->name, sizeof r->name, "%s", scratch.name);
    r->probe = scratch.probe;
    for (i = 0; i < REC_BYTES; i++) {
        if (scratch.mask[i]) {
            r->val[i] = scratch.val[i];
            r->mask[i] = 1;
        }
    }
    {
        size_t n = strlen(r->note);
        if (n > 0 && n + 3 < sizeof r->note) strcat(r->note, "; ");
        n = strlen(r->note);
        snprintf(r->note + n, sizeof r->note - n, "%s", scratch.note);
    }
}

/* "dw3:digimon/<id>" or a plain number */
static int parseId(const char *key) {
    char *end;
    long n;
    int id = contentOriginalId(&kDigimon, key);

    if (id >= 0) return id;
    if (key[0] < '0' || key[0] > '9') return -1;
    n = strtol(key, &end, 10);
    return (*end == '\0' && n >= ID_MIN && n <= ID_MAX) ? (int)n : -1;
}

static void takeEntry(const char *path, const char *key, const ExtJson *def) {
    int id = parseId(key), row, i;
    int partner, enemy;

    if (id < 0) {
        plogLine("digimon", "%s digimon \"%s\" rejected: use \"dw3:digimon/<id>\" or the number of a Digimon (%d..%d)\n", path, key,
                 ID_MIN, ID_MAX);
        return;
    }
    row = rowOf(id);
    partner = row >= 0 && row < REAL_ROWS;
    enemy = !partner && enemyOf(id) != NULL;
    if (row >= REAL_ROWS) {
        plogLine("digimon", "%s digimon \"dw3:digimon/%d\" rejected: it is an empty placeholder row of the partner table\n", path, id);
        return;
    }
    if (!partner && !enemy) {
        plogLine("digimon", "%s digimon \"dw3:digimon/%d\" rejected: no such Digimon%s\n", path, id,
                 sEnemyCount == 0 ? " (the battle table is not available, so no enemy can be changed)" : "");
        return;
    }
    if (def->t != 2) {
        plogLine("digimon", "%s digimon \"dw3:digimon/%d\" rejected: expected an object with \"partner\" or \"enemy\"\n", path, id);
        return;
    }
    for (i = 0; i < def->n; i++) {
        const char *k = def->keys[i];

        if (!strcmp(k, "partner") || !strcmp(k, "enemy")) {
            int facet = k[0] == 'p' ? FACET_PARTNER : FACET_ENEMY;

            if ((facet == FACET_PARTNER) != partner) {
                plogLine("digimon", "%s digimon \"dw3:digimon/%d\" facet \"%s\" rejected: this Digimon is %s, use \"%s\"\n", path, id, k,
                         partner ? "a partner form" : "an enemy", partner ? "partner" : "enemy");
                continue;
            }
            takeFacet(id, facet, path, &def->v[i]);
        } else {
            plogLine("digimon", "%s digimon \"dw3:digimon/%d\" key \"%s\" rejected: use \"partner\" or \"enemy\"\n", path, id, k);
        }
    }
}

static void takePlacements(const char *path, const ExtJson *o);

static void readModFile(int m) {
    char path[1200];
    uint8_t *data;
    size_t len;
    ExtJson root;
    const ExtJson *change, *encounters;
    int i;

    snprintf(path, sizeof path, "%s/content/digimon.json", modsDirOf(m));
    if (tdReadFile(path, &data, &len) != 0) return; /* a content mod without Digimon */
    /* a file that is not JSON or has another format was already logged by the core (contentLoad) */
    if (extJsonParse(data, len, &root) == 0) {
        const ExtJson *fmt = extJsonGet(&root, "format");

        if (fmt != NULL && fmt->t == 1 && strcmp(fmt->str, "digimon-v1") == 0) {
            change = extJsonGet(&root, "change");
            if (change != NULL && change->t != 2) {
                plogLine("digimon", "%s rejected: \"change\" must be an object of Digimon\n", path);
            } else if (change != NULL) {
                for (i = 0; i < change->n; i++) takeEntry(path, change->keys[i], &change->v[i]);
            }
            encounters = extJsonGet(&root, "encounters");
            if (encounters != NULL) takePlacements(path, encounters);
        }
    }
    extJsonFree(&root);
    free(data);
}

/* ---- new enemies ("new": the core reads the entries, this part owns the record and the rows) ---- */

static void put16At(unsigned char *p, size_t off, long v) {
    p[off] = (unsigned char)v;
    p[off + 1] = (unsigned char)(v >> 8);
}

static void setNameHooks(const char *(*nameOf)(int), int (*idOf)(const char *)) {
    /* an enemy id is never saved (ADR-009 section 4: nothing stored holds one), but a new partner form's can be: the "EXTD"
       chunk records those by name (ext_state.h). Detaching (NULL) also drops the base hook. */
    pcExtStateSetDigimonHooks(nameOf, idOf, nameOf != NULL ? baseOfNew : NULL);
}

static const char *newNameOf(int id) {
    return contentNameOf(&sSet, id);
}

static int newIdOf(const char *name) {
    return contentIdOf(&sSet, name);
}

static int newCount(void) {
    return contentCount(&sSet);
}

static const char *newText(int index, const char *langDir, int desc) {
    return contentText(&sSet, index, langDir, desc);
}

/* the base of a new Digimon must be an original partner form (a real row of DIGIMON_DATA) or an original ENEMY (its battle
   table row is what is copied), with a fighter entry to take the model from */
static int copyBase(int baseId, void *rec) {
    NewRec *n = rec;
    const unsigned char *e = enemyOf(baseId);
    int row = rowOf(baseId);

    if (row >= 0 && row < REAL_ROWS) {
        memcpy(n->row, originalRow(row), sizeof(DigimonData));
        n->facet = FACET_PARTNER;
    } else if (e != NULL) {
        memcpy(n->row, e, BT_SIZE);
        n->facet = FACET_ENEMY;
    } else {
        plogLine("digimon", "new digimon rejected: \"base\" dw3:digimon/%d is not a partner form or an enemy%s\n", baseId,
                 sEnemyCount == 0 ? " (the battle table is not available)"
                                  : " (a new Digimon copies a partner form's row or an enemy's battle table row)");
        return 0;
    }
    if (fighterOf(baseId) == NULL) {
        plogLine("digimon", "new digimon rejected: \"base\" dw3:digimon/%d has no fighter entry%s\n", baseId,
                 sFightFile == NULL ? " (the fighters file is not available)" : "");
        return 0;
    }
    n->baseId = baseId;
    n->model = baseId;
    return 1;
}

/* the original fighter a "model" names (a number or "dw3:digimon/<id>") if it has the layout of this facet, or -1 */
static int modelFighter(const NewRec *n, const ExtJson *v) {
    int id = -1, partner = n->facet == FACET_PARTNER;
    const unsigned char *f;

    if (v->t == 0 && extWhole(v->num, ID_MIN, ID_MAX)) id = (int)v->num;
    else if (v->t == 1 && v->str != NULL) {
        id = contentOriginalId(&kDigimon, v->str);
        if (id < 0 && (id = contentNamedId("model", v->str)) >= 0 && (id < ID_MIN || id > ID_MAX)) id = -1; /* "dw3:model/<id>": that fighter's model */
    }
    f = id >= 0 ? fighterOf(id) : NULL;
    if (f != NULL && partner && (f[3] >= ENEMY_KIND_MIN || rowOf(id) < 0 || rowOf(id) >= REAL_ROWS)) f = NULL;
    if (f == NULL || (!partner && f[3] < ENEMY_KIND_MIN)) return -1;
    return id;
}

/* a file reference of a "model" object: {"file": number or name, "entry": 0..65535} (an archive of the file), or the name of
   an original fighter ("dw3:digimon/<id>") to take that fighter's reference */
static int takeFileRef(const ExtJson *v, FileRef *r) {
    const ExtJson *file, *entry;
    int k;

    memset(r, 0, sizeof *r);
    if (v->t == 1 && v->str != NULL) {
        r->file = contentOriginalId(&kDigimon, v->str);
        r->kind = REF_FIGHTER;
        return r->file >= 0 && fighterOf(r->file) != NULL;
    }
    if (v->t != 2) return 0;
    for (k = 0; k < v->n; k++) if (strcmp(v->keys[k], "file") != 0 && strcmp(v->keys[k], "entry") != 0) return 0;
    file = extJsonGet(v, "file");
    entry = extJsonGet(v, "entry");
    if (entry != NULL) {
        if (entry->t != 0 || !extWhole(entry->num, 0, 65535)) return 0;
        r->entry = (int)entry->num;
    }
    if (file == NULL) return 0;
    if (file->t == 0 && extWhole(file->num, 1, 65535)) {
        r->kind = REF_FILE;
        r->file = (int)file->num;
        return 1;
    }
    if (file->t == 1 && file->str != NULL && file->str[0] != '\0' && strlen(file->str) < EVO_NAME) {
        r->kind = REF_NAME;
        snprintf(r->name, sizeof r->name, "%s", file->str);
        return 1;
    }
    return 0;
}

/* "model": {"from": an original fighter of the same facet (the template: cameras, height, distance and whatever is not replaced),
   "model"/"motions"/"effects": a file reference each, "height", "distance"}: the Digimon gets its OWN FighterInfo row */
static void takeOwnModel(const ContentEntry *e, NewRec *n, const ExtJson *v) {
    static const char *const refKeys[3] = {"model", "motions", "effects"};
    int i, k, id;

    n->own.own = 1;
    n->own.height = n->own.distance = INT_MIN;
    for (i = 0; i < v->n; i++) {
        const char *key = v->keys[i];
        const ExtJson *x = &v->v[i];

        if (!strcmp(key, "from")) {
            id = modelFighter(n, x);
            if (id < 0) {
                plogLine("digimon", "%s digimon \"%s\" field \"model.from\" rejected: an original %s (a number or \"dw3:digimon/<id>\")\n",
                         e->path, e->name, n->facet == FACET_PARTNER ? "partner form" : "enemy");
            } else {
                n->model = id;
            }
            continue;
        }
        if (!strcmp(key, "height") || !strcmp(key, "distance")) {
            if (x->t != 0 || !extWhole(x->num, -32768, 32767)) {
                plogLine("digimon", "%s digimon \"%s\" field \"model.%s\" rejected: a whole number -32768..32767\n", e->path, e->name, key);
            } else if (key[0] == 'h') {
                n->own.height = (int)x->num;
            } else {
                n->own.distance = (int)x->num;
            }
            continue;
        }
        for (k = 0; k < 3 && strcmp(key, refKeys[k]) != 0; k++) {}
        if (k == 3) {
            plogLine("digimon", "%s digimon \"%s\" field \"model.%s\" rejected: unknown key (from, model, motions, effects, height, distance)\n",
                     e->path, e->name, key);
        } else if (!takeFileRef(x, &n->own.ref[k])) {
            memset(&n->own.ref[k], 0, sizeof n->own.ref[k]);
            plogLine("digimon", "%s digimon \"%s\" field \"model.%s\" rejected: {\"file\": 1..65535 or a name, \"entry\": 0..65535} or "
                     "\"dw3:digimon/<id>\"\n", e->path, e->name, key);
        }
    }
}

/* "model": "<mod>:<key>": a model source of a content mod (mods/<mod>/content/models/<key>/model.json, shim/model_bake.c). The
   Digimon gets its own info row: the model and motions are the baked archive's entries 1 and 0, the effects come from the
   donor fighter of the source, the template (cameras and the rest of the row) is the source's "template" (the base's fighter
   without one), height and distance are the source's "info". A source that cannot be baked is logged when it is resolved
   (publish) and the Digimon keeps its template's model. */
static void takeSourceModel(const ContentEntry *e, NewRec *n, const char *name) {
    ModelSrcInfo info;
    ExtJson num;
    int id;

    if (strlen(name) >= EVO_NAME || !modelsHas(name)) {
        plogLine("digimon", "%s digimon \"%s\" field \"model\" rejected: no model source \"%s\" (mods/<mod>/content/models/<key>/model.json of a content mod), "
                 "and it is not an original fighter (a number or \"dw3:digimon/<id>\")\n", e->path, e->name, name);
        return;
    }
    if (!modelsInfo(name, &info)) return; /* the models channel said why */
    n->own.own = 1;
    n->own.height = info.height;
    n->own.distance = info.distance;
    if (info.templateId >= 0) {
        memset(&num, 0, sizeof num);
        num.num = info.templateId;
        id = modelFighter(n, &num);
        if (id < 0) {
            plogLine("digimon", "%s digimon \"%s\" model \"%s\": its \"template\" must be an original %s (the base's fighter is used)\n", e->path, e->name,
                     name, n->facet == FACET_PARTNER ? "partner form" : "enemy");
        } else {
            n->model = id;
        }
    }
    n->own.ref[0].kind = REF_NAME;
    snprintf(n->own.ref[0].name, sizeof n->own.ref[0].name, "%s", name);
    n->own.ref[0].entry = 1;
    n->own.ref[1] = n->own.ref[0];
    n->own.ref[1].entry = 0;
    if (info.effectsId >= 0) {
        if (fighterOf(info.effectsId) != NULL) {
            n->own.ref[2].kind = REF_FIGHTER;
            n->own.ref[2].file = info.effectsId;
        } else {
            plogLine("digimon", "%s digimon \"%s\" model \"%s\": its \"effects\" donor %d is not an original fighter (the template's effects are used)\n",
                     e->path, e->name, name, info.effectsId);
        }
    }
}

/* "model": an original fighter of the same facet as the base, by number or "dw3:digimon/<id>" (it shares that fighter's info row),
   a model source "<mod>:<key>" (takeSourceModel), or an object (its own info row, takeOwnModel) */
static void takeModel(const ContentEntry *e, NewRec *n, const ExtJson *v) {
    int id;
    int partner = n->facet == FACET_PARTNER;

    if (v->t == 2) {
        takeOwnModel(e, n, v);
        return;
    }
    if (v->t == 1 && v->str != NULL && strncmp(v->str, "dw3:", 4) != 0 && strchr(v->str, ':') != NULL) {
        takeSourceModel(e, n, v->str);
        return;
    }
    id = modelFighter(n, v);
    if (id < 0) {
        plogLine("digimon", "%s digimon \"%s\" field \"model\" rejected: an original %s (a number or \"dw3:digimon/<id>\"); the model of "
                 "%s has another layout\n", e->path, e->name, partner ? "partner form" : "enemy",
                 partner ? "an enemy" : "a partner form");
        return;
    }
    n->model = id;
}

/* "evolution": how a partner gets this form after a battle, an object or a list of objects (one per partner):
   {"partner": 0..7 or "dw3:digimon/<id of that partner's first form>", "needs": [{"digimon": ..., "level": 1..99}] (up to 2),
    "stat": 1..14 or "strength"|"defense"|"spirit"|"wisdom"|"speed"|"charisma"|"level", "value": 0..32767}
   It is the Evolution struct of the partner's list (include/battle/report.h) with the Digimon by name; they resolve in publish. */
static int evoStat(const ExtJson *v) {
    static const char *const names[7] = {"strength", "defense", "spirit", "wisdom", "speed", "charisma", "level"};
    int i;

    if (v->t == 0 && extWhole(v->num, 1, 14)) return (int)v->num;
    if (v->t == 1 && v->str != NULL) for (i = 0; i < 7; i++) if (!strcmp(v->str, names[i])) return i + 1;
    return -1;
}

static int evoPartner(const ExtJson *v) {
    int id, row;

    if (v->t == 0 && extWhole(v->num, 0, PARTNER_COUNT - 1)) return (int)v->num;
    if (v->t == 1 && v->str != NULL && (id = contentOriginalId(&kDigimon, v->str)) >= 0 && (row = rowOf(id)) >= 0 &&
        row < PARTNER_COUNT) return row;
    return -1;
}

static void takeOneEvolution(const ContentEntry *e, NewRec *n, const ExtJson *o) {
    EvoDecl d;
    int i, j, ok = 1;

    memset(&d, 0, sizeof d);
    d.partner = -1;
    d.value = 1;
    if (o->t != 2) {
        plogLine("digimon", "%s digimon \"%s\" field \"evolution\" rejected: an object {partner, needs, stat, value}\n", e->path, e->name);
        return;
    }
    for (i = 0; i < o->n; i++) {
        const char *k = o->keys[i];
        const ExtJson *v = &o->v[i];

        if (!strcmp(k, "partner")) {
            d.partner = evoPartner(v);
            if (d.partner < 0) {
                plogLine("digimon", "%s digimon \"%s\" evolution \"partner\" rejected: 0..%d or the first form of a partner "
                         "(\"dw3:digimon/<id>\")\n", e->path, e->name, PARTNER_COUNT - 1);
                ok = 0;
            }
        } else if (!strcmp(k, "stat")) {
            d.stat = evoStat(v);
            if (d.stat < 0) {
                plogLine("digimon", "%s digimon \"%s\" evolution \"stat\" rejected: 1..14, or strength, defense, spirit, wisdom, "
                         "speed, charisma or level\n", e->path, e->name);
                ok = 0;
            }
        } else if (!strcmp(k, "value")) {
            if (v->t != 0 || !extWhole(v->num, 0, 32767)) {
                plogLine("digimon", "%s digimon \"%s\" evolution \"value\" rejected: 0..32767\n", e->path, e->name);
                ok = 0;
            } else {
                d.value = (int)v->num;
            }
        } else if (!strcmp(k, "needs")) {
            const ExtJson *one = v->t == 2 ? v : NULL;
            int cnt = v->t == 4 ? v->n : (one != NULL ? 1 : -1);

            if (cnt < 0 || cnt > 2) {
                plogLine("digimon", "%s digimon \"%s\" evolution \"needs\" rejected: up to 2 of {digimon, level}\n", e->path, e->name);
                ok = 0;
                continue;
            }
            for (j = 0; j < cnt; j++) {
                const ExtJson *q = one != NULL ? one : &v->v[j], *dg = extJsonGet(q, "digimon"), *lv = extJsonGet(q, "level");

                if (q->t != 2 || dg == NULL || dg->t != 1 || dg->str == NULL || strlen(dg->str) >= EVO_NAME ||
                    (lv != NULL && (lv->t != 0 || !extWhole(lv->num, 1, 99)))) {
                    plogLine("digimon", "%s digimon \"%s\" evolution need %d rejected: {\"digimon\": \"dw3:digimon/<id>\" or "
                             "\"<mod>:<key>\", \"level\": 1..99}\n", e->path, e->name, j + 1);
                    ok = 0;
                    continue;
                }
                snprintf(d.need[d.nNeeds], EVO_NAME, "%s", dg->str);
                d.needLevel[d.nNeeds] = lv != NULL ? (int)lv->num : 1;
                d.nNeeds++;
            }
        } else {
            plogLine("digimon", "%s digimon \"%s\" evolution key \"%s\" rejected: use partner, needs, stat or value\n", e->path,
                     e->name, k);
            ok = 0;
        }
    }
    if (d.partner < 0 && ok) {
        plogLine("digimon", "%s digimon \"%s\" evolution rejected: it needs a \"partner\"\n", e->path, e->name);
        ok = 0;
    }
    if (!ok) return;
    if (n->nEvo >= EVO_MAX) {
        plogLine("digimon", "%s digimon \"%s\" evolution rejected: at most %d entries per form\n", e->path, e->name, EVO_MAX);
        return;
    }
    n->evo[n->nEvo++] = d;
}

static void takeEvolution(const ContentEntry *e, NewRec *n, const ExtJson *v) {
    int i;

    if (n->facet != FACET_PARTNER) {
        plogLine("digimon", "%s digimon \"%s\" field \"evolution\" rejected: only a partner form is learned by a partner\n", e->path,
                 e->name);
        return;
    }
    if (v->t == 4) {
        for (i = 0; i < v->n; i++) takeOneEvolution(e, n, &v->v[i]);
    } else {
        takeOneEvolution(e, n, v);
    }
}

/* "blastForms": five names of partner forms ("dw3:digimon/<id>" or "<mod>:<key>"); "pairPartner": one name, or 0 for none. They resolve in
   publish (a form can name one that comes later in id order). */
static void takeLinks(const ContentEntry *e, NewRec *n, const char *key, const ExtJson *v) {
    int i;

    if (n->facet != FACET_PARTNER) {
        plogLine("digimon", "%s digimon \"%s\" field \"%s\" rejected: only a partner form has it\n", e->path, e->name, key);
        return;
    }
    if (!strcmp(key, "pairPartner")) {
        if (v->t == 0 && v->num == 0) {
            n->hasPair = 1;
            n->pair[0] = '\0';
        } else if (v->t == 1 && v->str != NULL && strlen(v->str) < EVO_NAME) {
            n->hasPair = 1;
            snprintf(n->pair, EVO_NAME, "%s", v->str);
        } else {
            plogLine("digimon", "%s digimon \"%s\" field \"pairPartner\" rejected: a partner form (\"dw3:digimon/<id>\" or \"<mod>:<key>\") or 0\n",
                     e->path, e->name);
        }
        return;
    }
    if (v->t != 4 || v->n != 5) {
        plogLine("digimon", "%s digimon \"%s\" field \"blastForms\" rejected: a list of 5 partner forms (\"dw3:digimon/<id>\" or \"<mod>:<key>\")\n",
                 e->path, e->name);
        return;
    }
    for (i = 0; i < 5; i++) {
        if (v->v[i].t != 1 || v->v[i].str == NULL || strlen(v->v[i].str) >= EVO_NAME) {
            plogLine("digimon", "%s digimon \"%s\" field \"blastForms\" rejected: entry %d must be a name (\"dw3:digimon/<id>\" or "
                     "\"<mod>:<key>\")\n", e->path, e->name, i + 1);
            return;
        }
    }
    for (i = 0; i < 5; i++) snprintf(n->blast[i], EVO_NAME, "%s", v->v[i].str);
    n->nBlast = 5;
}

static int newField(const ContentEntry *e, const char *key, const ExtJson *v) {
    NewRec *n = e->rec;
    DRec scratch;
    int i, size = n->facet == FACET_PARTNER ? (int)sizeof(DigimonData) : BT_SIZE;

    if (!strcmp(key, "model")) {
        takeModel(e, n, v);
        return 1;
    }
    if (!strcmp(key, "evolution")) {
        takeEvolution(e, n, v);
        return 1;
    }
    if (!strcmp(key, "blastForms") || !strcmp(key, "pairPartner")) {
        takeLinks(e, n, key, v);
        return 1;
    }
    initScratch(&scratch, 0, n->facet);
    snprintf(scratch.name, sizeof scratch.name, "%s", e->name);
    scratch.defer = 1; /* a technique's animation is checked in publish, against the final model */
    if (!takeKey(&scratch, e->path, key, v)) return 0;
    for (i = 0; i < size; i++) if (scratch.mask[i]) n->row[i] = scratch.val[i];
    return 1;
}

/* a technique of a new Digimon that its model cannot animate is refused: the technique sits at `off` of the row (a u16/s16
   in both facets) and `base` is the base's own row; the base's technique stays */
static void checkTech(const ContentEntry *e, NewRec *n, const unsigned char *base, size_t off, const char *label, int partner) {
    int t = (int)(n->row[off] | (n->row[off + 1] << 8)), miss;

    if (t <= 0 || t == (int)(base[off] | (base[off + 1] << 8))) return;
    miss = techExtMissingSlot(n->model, PC_TECH(t - 1).script, partner);
    if (miss >= 0) {
        plogLine("digimon", "%s digimon \"%s\" field \"%s\" rejected: its model (dw3:digimon/%d) has no script slot %d in its "
                 "effects archive (the technique's animation is script %d); the base's technique stays\n", e->path, e->name, label,
                 n->model, miss, (int)PC_TECH(t - 1).script);
        n->row[off] = base[off];
        n->row[off + 1] = base[off + 1];
    }
}

/* the evolution entries of the new partner forms (P3): rows[i] is the DigimonData row of entry i, or -1 for an enemy. A requirement
   names an original partner form or a new one (never an enemy); one that does not resolve rejects that entry. */
static int evoNeedRow(const ContentSet *s, const int *rows, const char *name) {
    int id = contentOriginalId(&kDigimon, name), i;

    if (id >= 0) return (rowOf(id) >= 0 && rowOf(id) < REAL_ROWS) ? rowOf(id) : -1;
    for (i = 0; i < s->n; i++) if (!strcmp(s->e[i].name, name)) return rows[i];
    return -1;
}

/* the 16-bit blast forms and pair partner of every new partner form (U9): the base's own unless the form declares them; a name that is
   not a partner form (an original "dw3:digimon/<id>" or a new one) rejects that field and the base's stays */
static void publishLinks(const ContentSet *s, const int *rows) {
    int i, k;

    for (i = 0; i < s->n; i++) {
        const ContentEntry *e = &s->e[i];
        const NewRec *n = e->rec;
        unsigned short blast[5], pair;
        const DigimonData *base;

        if (n->facet != FACET_PARTNER || rows[i] < 0) continue;
        base = originalRow(rowOf(e->baseId));
        for (k = 0; k < 5; k++) blast[k] = (unsigned short)pcDigimonBlast(base, k);
        pair = (unsigned short)pcDigimonPair(base);
        if (n->nBlast == 5) {
            unsigned short got[5];

            for (k = 0; k < 5; k++) {
                int row = evoNeedRow(s, rows, n->blast[k]);

                if (row < 0) {
                    plogLine("digimon", "%s digimon \"%s\" field \"blastForms\" rejected: entry %d \"%s\" is not a partner form; the base's "
                             "stay\n", e->path, e->name, k + 1, n->blast[k]);
                    break;
                }
                got[k] = (unsigned short)(row + 1);
            }
            if (k == 5) memcpy(blast, got, sizeof blast);
        }
        if (n->hasPair) {
            int row = n->pair[0] == '\0' ? -2 : evoNeedRow(s, rows, n->pair);

            if (row == -2) {
                pair = 0;
            } else if (row < 0) {
                plogLine("digimon", "%s digimon \"%s\" field \"pairPartner\" rejected: \"%s\" is not a partner form; the base's stays\n",
                         e->path, e->name, n->pair);
            } else {
                pair = (unsigned short)(row + 1);
            }
        }
        pcDigimonSetLinks(rows[i], blast, pair);
        PLOG("digimon", PLOG_MODS, "digimon %s: blast forms %d/%d/%d/%d/%d, pair partner %d\n", e->name, blast[0], blast[1], blast[2],
             blast[3], blast[4], pair);
    }
}

static void publishEvolutions(const ContentSet *s, const int *rows) {
    int i, k, j;

    for (i = 0; i < s->n; i++) {
        const ContentEntry *e = &s->e[i];
        const NewRec *n = e->rec;

        for (k = 0; k < n->nEvo; k++) {
            const EvoDecl *d = &n->evo[k];
            PcEvolution ev;
            int ok = 1;

            memset(&ev, 0, sizeof ev);
            ev.digimon = rows[i] + 1;
            for (j = 0; j < 2; j++) {
                ev.needs[j].level = 1;
                if (j >= d->nNeeds) continue;
                {
                    int row = evoNeedRow(s, rows, d->need[j]);

                    if (row < 0) {
                        plogLine("digimon", "%s digimon \"%s\" evolution need \"%s\" rejected: not a partner form (an original "
                                 "\"dw3:digimon/<id>\" or a new one \"<mod>:<key>\"); the entry is dropped\n", e->path, e->name,
                                 d->need[j]);
                        ok = 0;
                        break;
                    }
                    ev.needs[j].digimon = (short)(row + 1);
                    ev.needs[j].level = (short)d->needLevel[j];
                }
            }
            if (!ok) continue;
            ev.stat = (short)d->stat;
            ev.value = (short)(d->stat != 0 ? d->value : 1);
            if (pcEvolutionExtend(d->partner, &ev) < 0) {
                plogLine("digimon", "%s digimon \"%s\" evolution dropped: no room for it\n", e->path, e->name);
                continue;
            }
            PLOG("digimon", PLOG_MODS, "digimon %s: evolution of partner %d (list entry %d): needs %d@%d %d@%d, stat %d value %d\n",
                 e->name, d->partner, pcEvolutionCount(d->partner) - 1, (int)ev.needs[0].digimon, (int)ev.needs[0].level,
                 (int)ev.needs[1].digimon, (int)ev.needs[1].level, (int)ev.stat, (int)ev.value);
        }
    }
}

static int publish(ContentSet *s) {
    int i, k, partners = 0, first = 0;
    int *rows = NULL;

    for (i = 0; i < s->n; i++) if (((NewRec *)s->e[i].rec)->facet == FACET_PARTNER) partners++;
    rows = calloc((size_t)(s->n > 0 ? s->n : 1), sizeof *rows);
    if (rows == NULL) return 0;
    for (i = 0; i < s->n; i++) rows[i] = -1;
    if (partners > 0) {
        first = pcDigimonExtendRows(partners);
        if (first < 0) {
            plogLine("digimon", "new partner forms dropped: the Digimon table cannot hold %d more rows\n", partners);
            free(rows);
            return 0;
        }
    }
    for (i = 0; i < s->n; i++) {
        const ContentEntry *e = &s->e[i];
        NewRec *n = e->rec;

        if (n->facet == FACET_PARTNER) {
            const DigimonData *base = originalRow(rowOf(e->baseId));
            DigimonData *d;

            for (k = 1; k <= 6; k++) {
                char label[24];

                snprintf(label, sizeof label, "skills.%d", k);
                checkTech(e, n, (const unsigned char *)base, PO(skills) + (size_t)k * 2, label, 1);
            }
            rows[i] = first;
            d = pcDigimonRef(first);
            memcpy(d, n->row, sizeof *d);
            d->id = (u16)(FIRST_NEW_ID + i);
            pcDigimonSetNameId(first, DIGNAM_ORIG_COUNT + i);
            PLOG("digimon", PLOG_MODS, "digimon %s: id %d, DIGNAM entry %d, partner form row %d based on dw3:digimon/%d, model of "
                 "dw3:digimon/%d, hp %d mp %d, techs %d/%d/%d/%d/%d/%d\n", e->name, FIRST_NEW_ID + i, DIGNAM_ORIG_COUNT + i, first,
                 e->baseId, n->model, (int)d->hp, (int)d->mp, (int)d->skills[1], (int)d->skills[2], (int)d->skills[3],
                 (int)d->skills[4], (int)d->skills[5], (int)d->skills[6]);
            first++;
            continue;
        }
        put16At(n->row, BT_ID, FIRST_NEW_ID + i);
        put16At(n->row, BT_NAME, DIGNAM_ORIG_COUNT + i);
        for (k = 0; k < 3; k++) {
            char label[16];

            snprintf(label, sizeof label, "techs.%d", k + 1);
            checkTech(e, n, enemyOf(e->baseId), BT_TECHS + (size_t)k * 2, label, 0);
        }
        PLOG("digimon", PLOG_MODS, "digimon %s: id %d, DIGNAM entry %d, model of dw3:digimon/%d, item %d (chance %d), techs %d/%d/%d\n",
             e->name, FIRST_NEW_ID + i, DIGNAM_ORIG_COUNT + i, n->model, s16At(n->row, BT_ITEM), s16At(n->row, BT_CHANCE),
             s16At(n->row, BT_TECHS), s16At(n->row, BT_TECHS + 2), s16At(n->row, BT_TECHS + 4));
    }
    publishLinks(s, rows);
    publishEvolutions(s, rows);
    free(rows);
    contentAttach(s);
    return 1;
}

static const NewRec *newRecOf(int id) {
    int i = id - FIRST_NEW_ID;

    return (i >= 0 && i < contentCount(&sSet)) ? contentRec(&sSet, i) : NULL;
}

static int baseOfNew(int id) {
    const NewRec *n = newRecOf(id);

    return n != NULL ? n->baseId : -1;
}

int digimonExtBaseOf(int id) {
    return baseOfNew(id);
}

long long digimonExtRowRef(const char *name) {
    int id = contentOriginalId(&kDigimon, name), r;

    if (id >= 0) return (rowOf(id) >= 0 && rowOf(id) < REAL_ROWS) ? rowOf(id) + 1 : -1;
    id = contentIdOf(&sSet, name);
    if (id < FIRST_NEW_ID) return -1;
    for (r = DIGIMON_ROWS; r < pcDigimonRowCount(); r++) {
        if (PC_DIGIMON(r).id == id) return r + 1;
    }
    return -1;
}

int digimonExtIsPartner(int id) {
    const NewRec *n = newRecOf(id);

    return n != NULL && n->facet == FACET_PARTNER;
}

int digimonExtNewCount(void) {
    return contentCount(&sSet);
}

int digimonExtNewId(const char *name) {
    return contentIdOf(&sSet, name);
}

const char *digimonExtNewName(int id) {
    return contentNameOf(&sSet, id);
}

/* The extended battle table: the original rows, then the new ones, then the id 0 that ends it. The size is whole sectors,
   whatever the disc file's was: the file cache sizes its buffer from the length the port serves. */
unsigned char *digimonExtBuildBattleTable(size_t *len) {
    int total = contentCount(&sSet), n = 0, rows = 0, i;
    size_t bytes, padded;
    unsigned char *out;

    for (i = 0; i < total; i++) if (((const NewRec *)contentRec(&sSet, i))->facet == FACET_ENEMY) n++;
    if (n <= 0 || sBattleFile == NULL) return NULL;
    while ((size_t)(rows + 1) * BT_SIZE <= sBattleLen && s16At(sBattleFile, (size_t)rows * BT_SIZE) != 0) rows++;
    bytes = (size_t)(rows + n + 1) * BT_SIZE;
    padded = (bytes + 2047) / 2048 * 2048;
    out = calloc(1, padded);
    if (out == NULL) return NULL;
    memcpy(out, sBattleFile, (size_t)rows * BT_SIZE);
    for (i = 0, n = 0; i < total; i++) {
        const NewRec *r = contentRec(&sSet, i);

        if (r->facet == FACET_ENEMY) memcpy(out + (size_t)(rows + n++) * BT_SIZE, r->row, BT_SIZE);
    }
    if (len != NULL) *len = padded;
    return out;
}

/* ---- the fighters file: entries, and the info rows of the Digimon with an own model ---- */

#define FH_PARTNERS 8
#define FH_ENEMIES 12
#define FI_PARTNER 0xC4 /* sizeof(FighterInfo) */
#define FI_ENEMY 0x48   /* sizeof(FighterInfoEnemy) */
#define FI_MODEL 0
#define FI_DISTANCE 0x10
#define FI_HEIGHT 0x18
#define FI_FACE 0x0C
#define FACE_RECT 10 /* sizeof(FaceRect): the list ends at an x of 0xFF */
#define INFO_ROWS 256 /* FighterEntry.index is a u8 */

/* The default resolver: "<mod>:<path inside the mod's folder>" is a raw model archive in the game's format (what
   tools/model_import.py writes), read once and served as a virtual file (port_fs.h) whose number FighterInfo takes. The same
   name gives the same number until the next load (digimonExtReset forgets the names; the host clears the virtual files). The
   baked models of model-import I5 (neutral source in, the archive made at load) replace it through digimonExtSetFileResolver. */
#define MODEL_FILES 64
#define MODEL_FILE_MAX (16 * 1024 * 1024)
static struct { char name[160]; int file; } sModelFiles[MODEL_FILES];
static int sModelFileCount;

static int modFileResolver(const char *name) {
    const char *colon = strchr(name, ':'), *rel;
    char mod[96], path[1100];
    unsigned char *bytes;
    long size;
    FILE *f;
    int i, m, file = -1;

    for (i = 0; i < sModelFileCount; i++) {
        if (strcmp(sModelFiles[i].name, name) == 0) return sModelFiles[i].file;
    }
    if (strchr(name, '/') == NULL && modelsHas(name)) return modelsFile(name); /* "<mod>:<key>": a model source, baked here */
    if (colon == NULL || colon == name || (size_t)(colon - name) >= sizeof mod || strlen(name) >= sizeof sModelFiles[0].name) return -1;
    rel = colon + 1;
    if (rel[0] == '\0' || rel[0] == '/' || strstr(rel, "..") != NULL || strchr(rel, '\\') != NULL) {
        plogLine("digimon", "model file \"%s\" rejected: a path inside the mod's folder, no \"..\"\n", name);
        return -1;
    }
    memcpy(mod, name, (size_t)(colon - name));
    mod[colon - name] = '\0';
    for (m = 0; m < modsCount() && strcmp(modsNameOf(m), mod) != 0; m++) {}
    if (m == modsCount() || sModelFileCount >= MODEL_FILES) return -1;
    snprintf(path, sizeof path, "%s/%s", modsDirOf(m), rel);
    f = fopen(path, "rb");
    if (f == NULL) return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 || size > MODEL_FILE_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    bytes = (unsigned char *)malloc((size_t)size);
    if (bytes != NULL && fread(bytes, 1, (size_t)size, f) == (size_t)size) file = pfsAddVirtualFile(bytes, (size_t)size);
    free(bytes);
    fclose(f);
    if (file >= 0) {
        snprintf(sModelFiles[sModelFileCount].name, sizeof sModelFiles[0].name, "%s", name);
        sModelFiles[sModelFileCount++].file = file;
        PLOG("digimon", PLOG_MODS, "model file \"%s\" is virtual file %d (%ld bytes)\n", name, file, size);
    }
    return file;
}

static int (*sResolveFile)(const char *name) = modFileResolver;

void digimonExtSetFileResolver(int (*resolve)(const char *name)) {
    sResolveFile = resolve != NULL ? resolve : modFileResolver;
}

/* the info row a fighter entry points at in the ORIGINAL file, or NULL when it lies outside it */
static const unsigned char *infoOf(const unsigned char *entry) {
    int enemy = entry[3] >= ENEMY_KIND_MIN;
    size_t row = enemy ? FI_ENEMY : FI_PARTNER, at = le32At(sFightFile + (enemy ? FH_ENEMIES : FH_PARTNERS)) + (size_t)entry[2] * row;

    return at + row <= sFightLen ? sFightFile + at : NULL;
}

/* How many rows of the partners' or the enemies' info table the entries use (the highest index + 1): the copy that grows holds
   that many, so a new row's index is never one an entry could have */
static int usedRows(int enemy) {
    int i, n = fighterCount(), rows = 0;

    for (i = 0; i < n; i++) {
        const unsigned char *e = fighterEntry(i);

        if ((e[3] >= ENEMY_KIND_MIN) == enemy && e[2] + 1 > rows) rows = e[2] + 1;
    }
    return rows;
}

/* The value of one reference of an own row: the declared one, or the template row's (`tpl`). *why says what failed. */
static int resolveRef(const FileRef *r, const unsigned char *tpl, int slot, unsigned *out, const char **why) {
    const unsigned char *f, *row;
    int file;

    switch (r->kind) {
    case REF_FILE:
        *out = ((unsigned)r->file << 16) | (unsigned)r->entry;
        return 1;
    case REF_NAME:
        file = sResolveFile != NULL ? sResolveFile(r->name) : -1;
        if (file < 1 || file > 0xFFFF) {
            *why = r->name;
            return 0;
        }
        *out = ((unsigned)file << 16) | (unsigned)r->entry;
        return 1;
    case REF_FIGHTER:
        f = fighterOf(r->file);
        row = f != NULL ? infoOf(f) : NULL;
        if (row == NULL) {
            *why = "a fighter without an info row";
            return 0;
        }
        *out = le32At(row + 4 * (size_t)slot);
        return 1;
    default:
        *out = le32At(tpl + 4 * (size_t)slot);
        return 1;
    }
}

/* One new Digimon's info row and kind, if it has an own model that resolves: the template's row with the references, height and
   distance replaced and the face list empty (`face`, an address as the file's base assumes it). 0: it shares the template's. */
typedef struct OwnRow {
    int has;
    int enemy;
    int index;
    unsigned char kind;
    unsigned char row[FI_PARTNER];
} OwnRow;

static void makeOwnRow(const NewRec *r, const char *name, OwnRow *o) {
    const unsigned char *f = fighterOf(r->model), *tpl = f != NULL ? infoOf(f) : NULL;
    const char *why = NULL;
    unsigned v[3];
    int k;

    memset(o, 0, sizeof *o);
    if (!r->own.own) return;
    if (tpl == NULL) {
        plogLine("digimon", "digimon \"%s\" has no own model: its template fighter has no info row in the fighters file\n", name);
        return;
    }
    for (k = 0; k < 3; k++) {
        if (!resolveRef(&r->own.ref[k], tpl, k, &v[k], &why)) {
            plogLine("digimon", "digimon \"%s\" has no own model: \"%s\" is not a file\n", name, why);
            return;
        }
    }
    o->has = 1;
    o->enemy = f[3] >= ENEMY_KIND_MIN;
    o->kind = f[3];
    memcpy(o->row, tpl, o->enemy ? FI_ENEMY : FI_PARTNER);
    for (k = 0; k < 3; k++) {
        o->row[4 * k] = (unsigned char)v[k];
        o->row[4 * k + 1] = (unsigned char)(v[k] >> 8);
        o->row[4 * k + 2] = (unsigned char)(v[k] >> 16);
        o->row[4 * k + 3] = (unsigned char)(v[k] >> 24);
    }
    if (r->own.distance != INT_MIN) put16At(o->row, FI_DISTANCE, r->own.distance);
    if (r->own.height != INT_MIN) put16At(o->row, FI_HEIGHT, r->own.height);
}

/* The fighters file with one entry per new Digimon. The entry list sits between the header and the infos, so it cannot grow in
   place: the original bytes stay where they are (every offset in the file, the faces included, keeps its meaning) and a longer
   copy of the list goes after them, with the header's `entries` pointing at it. A new Digimon's entry is {its id, the info row's
   index, its kind}. Its info row is the template fighter's own, shared (no row is added and the u8 index never runs out), unless
   its "model" is an object: then it has a row of its own. Those rows go into a longer copy of the partners' or the enemies' info
   table (the used rows, then the new ones; the header's pointer moves), with an empty face list at the end; the first index
   free is the number of used rows, and the table holds 256 (the u8), after which a new Digimon shares its template's row again. */
unsigned char *digimonExtBuildFighters(size_t *len) {
    int n = contentCount(&sSet), cnt, i, used[2], added[2] = {0, 0};
    size_t start, tail, pStart = 0, eStart = 0, faceAt, bytes, padded;
    unsigned char *out;
    OwnRow *own;
    int *index;

    if (n <= 0 || sFightFile == NULL) return NULL;
    cnt = fighterCount();
    own = calloc((size_t)n, sizeof *own);
    index = calloc((size_t)n, sizeof *index);
    if (own == NULL || index == NULL) {
        free(own);
        free(index);
        return NULL;
    }
    used[0] = usedRows(0);
    used[1] = usedRows(1);
    if (used[0] > 0 && le32At(sFightFile + FH_PARTNERS) + (size_t)used[0] * FI_PARTNER > sFightLen) used[0] = -1;
    if (used[1] > 0 && le32At(sFightFile + FH_ENEMIES) + (size_t)used[1] * FI_ENEMY > sFightLen) used[1] = -1;
    for (i = 0; i < n; i++) {
        const NewRec *r = contentRec(&sSet, i);
        const char *name = contentNameOf(&sSet, FIRST_NEW_ID + i);

        makeOwnRow(r, name, &own[i]);
        if (!own[i].has) continue;
        if (used[own[i].enemy] < 0 || used[own[i].enemy] + added[own[i].enemy] >= INFO_ROWS) {
            plogLine("digimon", "digimon \"%s\" has no own model: the %s' info table is full (%d rows)\n", name,
                     own[i].enemy ? "enemies" : "partners", INFO_ROWS);
            own[i].has = 0;
            continue;
        }
        index[i] = used[own[i].enemy] + added[own[i].enemy]++;
    }
    start = (sFightLen + 3) / 4 * 4;
    tail = start + (size_t)(cnt + n + 1) * FE_SIZE;
    if (added[0] > 0) {
        pStart = (tail + 3) / 4 * 4;
        tail = pStart + (size_t)(used[0] + added[0]) * FI_PARTNER;
    }
    if (added[1] > 0) {
        eStart = (tail + 3) / 4 * 4;
        tail = eStart + (size_t)(used[1] + added[1]) * FI_ENEMY;
    }
    faceAt = tail;
    bytes = (added[0] + added[1] > 0) ? faceAt + FACE_RECT : tail;
    padded = (bytes + 2047) / 2048 * 2048;
    out = calloc(1, padded);
    if (out == NULL) {
        free(own);
        free(index);
        return NULL;
    }
    memcpy(out, sFightFile, sFightLen);
    put16At(out, FH_ENTRIES, (long)(start & 0xFFFF));
    put16At(out, FH_ENTRIES + 2, (long)(start >> 16));
    memcpy(out + start, fighterEntry(0), (size_t)cnt * FE_SIZE);
    if (added[0] > 0) {
        memcpy(out + pStart, sFightFile + le32At(sFightFile + FH_PARTNERS), (size_t)used[0] * FI_PARTNER);
        put16At(out, FH_PARTNERS, (long)(pStart & 0xFFFF));
        put16At(out, FH_PARTNERS + 2, (long)(pStart >> 16));
    }
    if (added[1] > 0) {
        memcpy(out + eStart, sFightFile + le32At(sFightFile + FH_ENEMIES), (size_t)used[1] * FI_ENEMY);
        put16At(out, FH_ENEMIES, (long)(eStart & 0xFFFF));
        put16At(out, FH_ENEMIES + 2, (long)(eStart >> 16));
    }
    if (added[0] + added[1] > 0) out[faceAt] = 0xFF; /* the empty face list: it ends at its first rect */
    for (i = 0; i < n; i++) {
        const NewRec *r = contentRec(&sSet, i);
        const unsigned char *m = fighterOf(r->model);
        unsigned char *e = out + start + (size_t)(cnt + i) * FE_SIZE;

        put16At(e, 0, FIRST_NEW_ID + i);
        if (own[i].has) {
            unsigned face = le32At(sFightFile) + (unsigned)faceAt;
            unsigned char *row = out + (own[i].enemy ? eStart + (size_t)index[i] * FI_ENEMY : pStart + (size_t)index[i] * FI_PARTNER);

            memcpy(row, own[i].row, own[i].enemy ? FI_ENEMY : FI_PARTNER);
            put16At(row, FI_FACE, (long)(face & 0xFFFF));
            put16At(row, FI_FACE + 2, (long)(face >> 16));
            e[2] = (unsigned char)index[i];
            e[3] = own[i].kind;
            PLOG("digimon", PLOG_MODS, "digimon \"%s\": own %s info row %d (template dw3:digimon/%d)\n", contentNameOf(&sSet, FIRST_NEW_ID + i),
                 own[i].enemy ? "enemy" : "partner", index[i], r->model);
        } else {
            e[2] = m[2];
            e[3] = m[3];
        }
    }
    free(own);
    free(index);
    if (len != NULL) *len = padded;
    return out;
}

/* ---- placing enemies in encounters ("encounters": {"<n>": {"<slot 1..3>": {"enemy", "level", "hp", "mp", "strength"}}}) ---- */

typedef struct DPlace {
    int encounter, slot;          /* the encounter's index, the slot 0..2 */
    int enemy;                    /* a fighter id; 0 empties the slot */
    long level, hp, mp, strength; /* -1: the slot's own value */
    char note[200];
    int logged;
    BattleEnemy row; /* what the encounter points at once applied */
} DPlace;

static DPlace *sPlace;
static int sPlaceCount;

/* an enemy: 0 (none), an original enemy (a number or "dw3:digimon/<id>") or a new enemy of a mod ("<mod>:<key>"); -1 otherwise */
static int resolveEnemy(const ExtJson *v) {
    int id = -1;

    if (v->t == 0 && extWhole(v->num, 0, ID_MAX)) {
        id = (int)v->num;
    } else if (v->t == 1 && v->str != NULL) {
        id = contentOriginalId(&kDigimon, v->str);
        if (id < 0) {
            id = contentIdOf(&sSet, v->str);
            return (id >= 0 && !digimonExtIsPartner(id)) ? id : -1; /* a new partner form is no enemy */
        }
    }
    return (id == 0 || (id > 0 && enemyOf(id) != NULL)) ? id : -1;
}

static void takePlace(const char *path, int encounter, const char *slotKey, const ExtJson *def) {
    DPlace p, *grown;
    int i, k, slot = (slotKey[0] >= '1' && slotKey[0] <= '3' && slotKey[1] == '\0') ? slotKey[0] - '1' : -1;
    const ExtJson *en;

    if (slot < 0) {
        plogLine("digimon", "%s encounter %d slot \"%s\" rejected: the slot is 1..3\n", path, encounter, slotKey);
        return;
    }
    if (def->t != 2 || (en = extJsonGet(def, "enemy")) == NULL) {
        plogLine("digimon", "%s encounter %d slot %s rejected: an object with \"enemy\" (and \"level\", \"hp\", \"mp\", \"strength\")\n", path,
                 encounter, slotKey);
        return;
    }
    memset(&p, 0, sizeof p);
    p.encounter = encounter;
    p.slot = slot;
    p.level = p.hp = p.mp = p.strength = -1;
    p.enemy = resolveEnemy(en);
    if (p.enemy < 0) {
        plogLine("digimon", "%s encounter %d slot %s rejected: \"enemy\" is not an enemy (0 for none, an original enemy as a number or "
                 "\"dw3:digimon/<id>\", or a new enemy \"<mod>:<key>\")\n", path, encounter, slotKey);
        return;
    }
    for (i = 0; i < def->n; i++) {
        const char *key = def->keys[i];
        const ExtJson *v = &def->v[i];
        long *dst = NULL, lo = 0, hi = 32767;

        if (!strcmp(key, "enemy")) continue;
        if (!strcmp(key, "level")) {
            dst = &p.level;
            lo = 1;
            hi = 99;
        } else if (!strcmp(key, "hp")) {
            dst = &p.hp;
            lo = 1;
        } else if (!strcmp(key, "mp")) {
            dst = &p.mp;
        } else if (!strcmp(key, "strength")) {
            dst = &p.strength;
            lo = 1;
        }
        if (dst == NULL) {
            plogLine("digimon", "%s encounter %d slot %s field \"%s\" rejected: unknown field\n", path, encounter, slotKey, key);
        } else if (v->t != 0 || !extWhole(v->num, lo, hi)) {
            plogLine("digimon", "%s encounter %d slot %s field \"%s\" rejected: a whole number %ld..%ld\n", path, encounter, slotKey, key,
                     lo, hi);
        } else {
            *dst = (long)v->num;
        }
    }
    snprintf(p.note, sizeof p.note, "slot %d (%s)", slot + 1, en->t == 1 ? en->str : "by number");
    for (k = 0; k < sPlaceCount; k++) { /* a later mod replaces the whole placement of that slot */
        if (sPlace[k].encounter == encounter && sPlace[k].slot == slot) {
            sPlace[k] = p;
            return;
        }
    }
    grown = realloc(sPlace, sizeof(DPlace) * (size_t)(sPlaceCount + 1));
    if (grown == NULL) return;
    sPlace = grown;
    sPlace[sPlaceCount++] = p;
}

static void takePlacements(const char *path, const ExtJson *o) {
    int i, j;

    if (o->t != 2) {
        plogLine("digimon", "%s rejected: \"encounters\" must be an object of encounters\n", path);
        return;
    }
    for (i = 0; i < o->n; i++) {
        char *end = NULL;
        long enc = o->keys[i][0] >= '0' && o->keys[i][0] <= '9' ? strtol(o->keys[i], &end, 10) : -1;
        const ExtJson *slots = &o->v[i];

        if (enc < 1 || enc >= ENCOUNTER_ROWS || end == NULL || *end != '\0') {
            plogLine("digimon", "%s encounter \"%s\" rejected: the encounter is 1..%d\n", path, o->keys[i], ENCOUNTER_ROWS - 1);
            continue;
        }
        if (slots->t != 2) {
            plogLine("digimon", "%s encounter %ld rejected: an object of slots 1..3\n", path, enc);
            continue;
        }
        for (j = 0; j < slots->n; j++) takePlace(path, (int)enc, slots->keys[j], &slots->v[j]);
    }
}

int digimonExtPlaceCount(void) {
    return sPlaceCount;
}

void digimonExtPlaceTarget(int i, int *encounter, int *slot) {
    *encounter = sPlace[i].encounter;
    *slot = sPlace[i].slot;
}

/* The row placement `i` puts in its slot, with the values the placement leaves out taken from `orig`, the slot's row as the
   game has it (NULL or an empty row: nothing to inherit). NULL when a value is missing (logged once). The row lives until
   digimonExtReset. */
BattleEnemy *digimonExtPlaceRow(int i, const BattleEnemy *orig) {
    DPlace *p = &sPlace[i];
    int inherit = orig != NULL && orig->fighter != 0;

    if (p->enemy == 0) {
        memset(&p->row, 0, sizeof p->row);
    } else {
        if ((p->level < 0 || p->hp < 0 || p->mp < 0 || p->strength < 0) && !inherit) {
            if (!p->logged) {
                p->logged = 1;
                plogLine("digimon", "encounter %d slot %d skipped: that slot is empty, so the placement needs \"level\", \"hp\", \"mp\" and "
                         "\"strength\"\n", p->encounter, p->slot + 1);
            }
            return NULL;
        }
        p->row.fighter = p->enemy;
        p->row.level = (s16)(p->level >= 0 ? p->level : orig->level);
        p->row.hp = (s16)(p->hp >= 0 ? p->hp : orig->hp);
        p->row.mp = (s16)(p->mp >= 0 ? p->mp : orig->mp);
        p->row.strength = (s16)(p->strength >= 0 ? p->strength : orig->strength);
    }
    if (!p->logged) {
        p->logged = 1;
        PLOG("digimon", PLOG_MODS, "encounter %d %s placed: fighter %d level %d hp %d mp %d strength %d\n", p->encounter, p->note,
             (int)p->row.fighter, (int)p->row.level, (int)p->row.hp, (int)p->row.mp, (int)p->row.strength);
    }
    return &p->row;
}

/* ---- applying ---- */

static void applyPartners(void) {
    int i, b;

    for (i = 0; i < sRecCount; i++) {
        const DRec *r = &sRecs[i];
        int row;
        unsigned char *dst;

        if (r->facet != FACET_PARTNER || (row = rowOf(r->id)) < 0) continue;
        sSaved[sSavedCount].row = row;
        sSaved[sSavedCount].orig = PC_DIGIMON(row);
        sSavedCount++;
        dst = (unsigned char *)&PC_DIGIMON(row);
        for (b = 0; b < (int)sizeof(DigimonData); b++) if (r->mask[b]) dst[b] = r->val[b];
        PLOG("digimon", PLOG_MODS, "digimon dw3:digimon/%d (partner form, row %d) changed: %s\n", r->id, row, r->note);
    }
}

void digimonExtReset(void) {
    int i;

    for (i = sSavedCount - 1; i >= 0; i--) PC_DIGIMON(sSaved[i].row) = sSaved[i].orig;
    sSavedCount = 0;
    contentUnload(&sSet);
    free(sPlace);
    sPlace = NULL;
    sPlaceCount = 0;
    free(sRecs);
    sRecs = NULL;
    sRecCount = 0;
    sEnemyRecs = 0;
    sModelFileCount = 0; /* the names the last load resolved: the host clears the virtual files they pointed at */
    modelsReset();       /* and the model sources baked for them */
    pcDigimonResetRows(); /* the rows of the new partner forms */
    pcEvolutionReset();   /* and the evolution entries that name them */
}

int digimonExtLoad(void) {
    int m, i;

    digimonExtReset();
    contentLoad(&sSet); /* the new enemies first: a placement can name one */
    for (m = 0; m < modsCount(); m++) {
        if (strcmp(modsKindOf(m), "content") != 0) continue;
        readModFile(m);
    }
    applyPartners();
    sEnemyRecs = 0;
    for (i = 0; i < sRecCount; i++) {
        if (sRecs[i].facet == FACET_ENEMY) {
            sEnemyRecs++;
            PLOG("digimon", PLOG_MODS, "digimon dw3:digimon/%d (enemy) will change in the battle table: %s\n", sRecs[i].id,
                 sRecs[i].note);
        }
    }
    return sRecCount + contentCount(&sSet) + sPlaceCount;
}

int digimonExtEnemyPatchCount(void) {
    return sEnemyRecs > 0 ? sEnemyRecs : 0;
}

void digimonExtPatchBattleTable(uint8_t *buf, size_t len) {
    size_t i;
    int k;

    if (sEnemyRecs <= 0) return;
    for (i = 0; (i + 1) * BT_SIZE <= len; i++) {
        unsigned char *row = buf + i * BT_SIZE;
        int id = (short)(row[0] | (row[1] << 8));

        if (id == 0) break;
        for (k = 0; k < sRecCount; k++) {
            const DRec *r = &sRecs[k];
            int b;

            if (r->facet != FACET_ENEMY || r->id != id) continue;
            for (b = 0; b < BT_SIZE; b++) if (r->mask[b]) row[b] = r->val[b];
            PLOG("digimon", PLOG_MODS, "digimon dw3:digimon/%d (enemy) changed in the battle table: %s\n", id, r->note);
        }
    }
}

/* ---- the reference export ---- */

typedef struct Out {
    char *b;
    size_t n, cap;
} Out;

static void put(Out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Out *o, const char *fmt, ...) {
    va_list ap;
    int w;

    if (o->n + 1 >= o->cap) return;
    va_start(ap, fmt);
    w = vsnprintf(o->b + o->n, o->cap - o->n, fmt, ap);
    va_end(ap);
    if (w > 0) o->n = (size_t)w >= o->cap - o->n ? o->cap - 1 : o->n + (size_t)w;
}

static void putGroup(Out *o, const Field *f, const void *rec) {
    int i;

    put(o, ", \"%s\": {", f->name);
    for (i = 0; i < f->count; i++) {
        long v = readAt(rec, f->off + (size_t)i * (size_t)f->width, f->width);

        if (f->keys != NULL) put(o, "%s\"%s\": %ld", i ? ", " : "", f->keys[i], v);
        else put(o, "%s\"%d\": %ld", i ? ", " : "", i, v);
    }
    put(o, "}");
}

static void putFields(Out *o, const Field *fields, int nf, const void *rec) {
    int i;

    for (i = 0; i < nf; i++) {
        if (fields[i].count > 0) putGroup(o, &fields[i], rec);
        else put(o, ", \"%s\": %ld", fields[i].name, readAt(rec, fields[i].off, fields[i].width));
    }
}

/* the animation slots (of the 18 a fighter's effects archive can hold) this fighter has, for the reference: a modder sees which
   technique scripts it can play (TechData.script) without tools. Left out when the fighters file is not available. */
static void putScriptSlots(Out *o, int id) {
    int s, first = 1;

    if (techExtHasSlot(id, 0) < 0) return;
    put(o, ", \"scriptSlots\": [");
    for (s = 0; s < 18; s++) {
        if (techExtHasSlot(id, s) != 1) continue;
        put(o, "%s%d", first ? "" : ", ", s);
        first = 0;
    }
    put(o, "]");
}

/* the folder the in-game export writes the fighter's model to (model_export.c), named even before it is exported */
static void putModelFolder(Out *o, int id) {
    if (fighterOf(id) == NULL) return;
    put(o, ", \"model\": \"models/%d/\"", id); /* data/models/<id>/, as model_export.c writes it */
}

static int describe(int id, char *out, size_t cap) {
    Out o = {out, 0, cap};
    int row = rowOf(id), i, first;
    const unsigned char *e;

    out[0] = '\0';
    if (row >= 0) {
        const DigimonData *d = originalRow(row);

        put(&o, "\"kind\": \"partner\"");
        putModelFolder(&o, id);
        if (row >= REAL_ROWS) put(&o, ", \"placeholder\": true");
        putFields(&o, kPartnerFields, NPF, d);
        put(&o, ", \"skills\": {");
        for (i = 1, first = 1; i <= 6; i++) {
            if (d->skills[i] == 0) continue;
            put(&o, "%s\"%d\": {\"tech\": \"dw3:tech/%d\", \"level\": %d}", first ? "" : ", ", i, (int)d->skills[i],
                (int)d->skillLevels[i - 1]);
            first = 0;
        }
        put(&o, "}");
        putScriptSlots(&o, id);
        return 1;
    }
    e = enemyOf(id);
    if (e == NULL) return 0;
    put(&o, "\"kind\": \"enemy\"");
    putModelFolder(&o, id);
    putFields(&o, kEnemyFields, NEF, e);
    put(&o, ", \"techs\": {");
    for (i = 0, first = 1; i < 3; i++) {
        int t = s16At(e, BT_TECHS + (size_t)i * 2);

        if (t <= 0) continue;
        put(&o, "%s\"%d\": \"dw3:tech/%d\"", first ? "" : ", ", i + 1, t);
        first = 0;
    }
    put(&o, "}");
    if (s16At(e, BT_ITEM) > 0) put(&o, ", \"item\": \"dw3:item/%d\"", s16At(e, BT_ITEM));
    else put(&o, ", \"item\": 0");
    putScriptSlots(&o, id);
    return 1;
}
