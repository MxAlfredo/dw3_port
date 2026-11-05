/*
 * The equipment and item effect tables: see game_tables.h.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/game_state.h"
#include "engine/digimon_access.h"
#include "card_ext.h"
#include "content.h"
#include "digimon_ext.h"
#include "ext_state.h"
#include "game_tables.h"
#include "item_ext.h"
#include "port_log.h"

/* the executable's arrays (items.c), which game_state.h does not declare */
extern WeaponData WEAPON_DATA[];
extern ArmorData ARMOR_DATA[];
extern AccessoryData ACCESSORY_DATA[];

#define GT_WEAPONS 123
#define GT_ARMOR 77
#define GT_ACCESSORIES 68
#define GT_FIRST_WEAPON 92 /* the item id of WEAPON_DATA[0]; the armour and the accessories follow in order */
#define GT_FIRST_ARMOR 215
#define GT_FIRST_ACCESSORY 292
#define GT_ITEMS 402

#define CHARISMA_NOTE "What it adds to the partner's charisma (summed over the worn items; the total stops at 999)."
#define KIND_NOTE \
    "Where it goes (what the status screens and the shops read): 1 right hand, 2 left hand, 3 either hand, 4 head, 5 body, " \
    "6 accessory, 7 both hands (takes both slots), 8 accessory of a group (one of its group at a time)."
#define GROUP_NOTE "The group of an accessory that shares a slot with others of its group (kind 8): only one of a group is worn."
#define PARTNERS_NOTE \
    "Who can wear it: one bit per partner (1 << partner number), 255 is all eight."
#define STAT_NOTE \
    "The stat the first/second bonus raises: 0 none, 1-6 strength, defense, spirit, wisdom, speed and charisma bonuses " \
    "(the equipment stats of the status screen), 7 all six, 8-14 the seven resistances."
#define AMOUNT_NOTE "The bonus itself, signed (negative lowers the stat); a stat stops at 999."

static const TmField kWeaponFields[] = {
    {"charisma", TM_S16, offsetof(WeaponData, charisma), -999, 999, CHARISMA_NOTE},
    {"kind", TM_U8, offsetof(WeaponData, kind), 1, 8, KIND_NOTE},
    {"group", TM_U8, offsetof(WeaponData, group), 0, 255, "Equipment group number (read for accessories; kept for completeness on weapons)."},
    {"partners", TM_U8, offsetof(WeaponData, partners), 0, 255, PARTNERS_NOTE},
    {"stat1", TM_U8, offsetof(WeaponData, stats), 0, 14, "First bonus: " STAT_NOTE},
    {"amount1", TM_S16, offsetof(WeaponData, amounts), -999, 999, "First bonus: " AMOUNT_NOTE},
    {"stat2", TM_U8, offsetof(WeaponData, stats) + 1, 0, 14, "Second bonus: " STAT_NOTE},
    {"amount2", TM_S16, offsetof(WeaponData, amounts) + 2, -999, 999, "Second bonus: " AMOUNT_NOTE},
    {"atk", TM_U16, offsetof(WeaponData, atk), 0, 999, "What it adds to the attack (the total stops at 999)."},
    {"accuracy", TM_U8, offsetof(WeaponData, accuracy), 0, 255,
     "Added to the partner's accuracy in battle (an 8-bit total over the worn weapons: keep it low)."},
    {"hitEffect", TM_U8, offsetof(WeaponData, hitEffect), 0, 255,
     "A label of the hit effect (1 none, 2 poison, 3 paralysis, 4 confusion, 6 knock-out, 8 drain, 11 criticals). The game does not "
     "read it: the effect follows the item id."},
    {"statusChance", TM_U8, offsetof(WeaponData, statusChance), 0, 255,
     "Chance of the status its hits inflict. Only the weapons with a hit effect read it (the game picks them by item id)."},
    {"statusPower", TM_U8, offsetof(WeaponData, statusPower), 0, 255,
     "Strength of that status, or the critical bonus (items with a hit effect only)."},
    {"family", TM_U8, offsetof(WeaponData, family), 0, 255,
     "The family its hits count as (2 and up; under 2 it has none): it hits that family harder."},
};

static const TmField kArmorFields[] = {
    {"charisma", TM_S16, offsetof(ArmorData, charisma), -999, 999, CHARISMA_NOTE},
    {"kind", TM_U8, offsetof(ArmorData, kind), 1, 8, KIND_NOTE},
    {"group", TM_U8, offsetof(ArmorData, group), 0, 255, "Equipment group number (read for accessories; kept for completeness on armour)."},
    {"partners", TM_U8, offsetof(ArmorData, partners), 0, 255, PARTNERS_NOTE},
    {"stat1", TM_U8, offsetof(ArmorData, stats), 0, 14, "First bonus: " STAT_NOTE},
    {"amount1", TM_S16, offsetof(ArmorData, amounts), -999, 999, "First bonus: " AMOUNT_NOTE},
    {"stat2", TM_U8, offsetof(ArmorData, stats) + 1, 0, 14, "Second bonus: " STAT_NOTE},
    {"amount2", TM_S16, offsetof(ArmorData, amounts) + 2, -999, 999, "Second bonus: " AMOUNT_NOTE},
    {"def", TM_S16, offsetof(ArmorData, def), -999, 999, "What it adds to the defense, signed (the total stops at 999 and never goes under 0)."},
    {"evasion", TM_U8, offsetof(ArmorData, evasion), 0, 255,
     "Added to the partner's evasion in battle (an 8-bit total over the worn armour: keep it low)."},
};

static const TmField kAccessoryFields[] = {
    {"charisma", TM_S16, offsetof(AccessoryData, charisma), -999, 999, CHARISMA_NOTE},
    {"kind", TM_U8, offsetof(AccessoryData, kind), 6, 8,
     "6 plain accessory, 8 accessory of a group (one of its group at a time); 7 is rejected, the accessory slots take no other kind."},
    {"group", TM_U8, offsetof(AccessoryData, group), 0, 255, GROUP_NOTE},
    {"partners", TM_U8, offsetof(AccessoryData, partners), 0, 255, PARTNERS_NOTE},
    {"stat", TM_U8, offsetof(AccessoryData, stat), 0, 21,
     "What it raises: 0 none, 1-6 strength, defense, spirit, wisdom, speed and charisma bonuses, 7 all six, 8-14 the seven "
     "resistances (status screen), 17-21 the battle resistance to poison, paralysis, confusion, sleep and knock-out (it "
     "replaces the partner's value in battle). A few accessories are read by their item id (triple hit, critical, counter): "
     "their amount is that effect's."},
    {"amount", TM_S16, offsetof(AccessoryData, amount), -999, 999, AMOUNT_NOTE},
};

static const char *accessoryCheck(const void *row) {
    return ((const AccessoryData *)row)->kind == EQUIP_KIND_BOTH_HANDS ? "kind 7 is a weapon kind, not an accessory's (6 or 8)" : NULL;
}

#define ID_NOTE "A mod's item has its own copy of the record, made from its base item's."
#define EXE_WHEN "Set once when the game starts; the status screens, the shops and every battle read the record when they need it, so a change shows everywhere."

static const TmTable kWeaponTable = {
    "weapon_data", WEAPON_DATA, sizeof(WeaponData), GT_WEAPONS, GT_FIRST_WEAPON,
    "The item id of the weapon, 92 to 214, or \"<mod>:<name>\" for a weapon a content mod adds. " ID_NOTE,
    EXE_WHEN, kWeaponFields, (int)(sizeof(kWeaponFields) / sizeof(kWeaponFields[0])), 0, 0, TM_S32, itemExtByName, itemExtWeaponRow};

static const TmTable kArmorTable = {
    "armor_data", ARMOR_DATA, sizeof(ArmorData), GT_ARMOR, GT_FIRST_ARMOR,
    "The item id of the armour, 215 to 291, or \"<mod>:<name>\" for an armour a content mod adds. " ID_NOTE,
    EXE_WHEN, kArmorFields, (int)(sizeof(kArmorFields) / sizeof(kArmorFields[0])), 0, 0, TM_S32, itemExtByName, itemExtArmorRow};

static const TmTable kAccessoryTable = {
    "accessory_data", ACCESSORY_DATA, sizeof(AccessoryData), GT_ACCESSORIES, GT_FIRST_ACCESSORY,
    "The item id of the accessory, 292 to 359, or \"<mod>:<name>\" for an accessory a content mod adds. " ID_NOTE,
    EXE_WHEN, kAccessoryFields, (int)(sizeof(kAccessoryFields) / sizeof(kAccessoryFields[0])), 0, 0, TM_S32, itemExtByName, itemExtAccessoryRow,
    NULL, NULL, accessoryCheck};

const TmTable *gameTableWeapons(void) {
    return &kWeaponTable;
}

const TmTable *gameTableArmor(void) {
    return &kArmorTable;
}

const TmTable *gameTableAccessories(void) {
    return &kAccessoryTable;
}

/* ---- the usable items' effects: separate records, one per item, reached through ITEMS[id - 1].data ---- */

static const TmField kEffectFields[] = {
    {"flags", TM_U8, offsetof(ItemEffect, flags), 0, 7,
     "Where it can be used, a sum: 1 from the status menu, 2 in battle, 4 marks a card booster (nothing tests that one)."},
    {"kind", TM_U8, offsetof(ItemEffect, kind), 0, 17,
     "What it does: 0 nothing here (the battle items act by their item id), 1 heals amount HP, 17 refills amount TP, 2-16 raise "
     "a stat by 1 up to amount points (HP, MP, strength, defense, spirit, wisdom, speed, resistances: the status menu's list)."},
    {"amount", TM_U16, offsetof(ItemEffect, amount), 0, 20000,
     "The HP healed, the TP refilled, or the top of the random stat boost (never 0 for kinds 2-16); the battle items read it as "
     "their own number (a gauge, a boost in 128ths). Up to 20000 so that the healed HP still fits the game's 16-bit stats."},
};

static const char *effectCheck(const void *row) {
    const ItemEffect *e = (const ItemEffect *)row;

    if (e->kind >= 2 && e->kind <= 16 && e->amount == 0) {
        return "a stat boost (kind 2-16) needs an amount of 1 or more, the game picks a random number up to it";
    }
    return NULL;
}

static long sEffectKeys[GT_ITEMS];
static ItemEffect *sEffectRows[GT_ITEMS];
static int sEffectCount;

static void *effectRowAt(int row) {
    return row >= 0 && row < sEffectCount ? (void *)sEffectRows[row] : NULL;
}

static TmTable sEffectTable = {
    "item_effects", NULL, sizeof(ItemEffect), 0, 0,
    "The item id of the usable item (the ids from 43 on that use an effect, including the card boosters), or \"<mod>:<name>\" for a usable item a content mod adds (it has its own copy of its base item's effect).",
    EXE_WHEN, kEffectFields, (int)(sizeof(kEffectFields) / sizeof(kEffectFields[0])), 0, 0, TM_S32, itemExtByName, itemExtEffectRow,
    sEffectKeys, effectRowAt, effectCheck};

const TmTable *gameTableItemEffects(void) {
    int id, k;

    sEffectCount = 0;
    for (id = 1; id <= GT_ITEMS; id++) {
        const ItemInfo *it = &ITEMS[id - 1];

        if (it->kind != ITEM_KIND_USABLE || it->data.effect == NULL) {
            continue;
        }
        for (k = 0; k < sEffectCount && sEffectRows[k] != it->data.effect; k++) {
        }
        if (k < sEffectCount) {
            continue; /* a record two items share is the first one's row */
        }
        sEffectKeys[sEffectCount] = id;
        sEffectRows[sEffectCount++] = it->data.effect;
    }
    sEffectTable.rows = sEffectCount;
    return &sEffectTable;
}

/* The value of a field in a row (every type of the engine) */
static long long fieldValue(const unsigned char *row, const TmField *f) {
    const unsigned char *p = row + f->off;

    switch (f->type) {
    case TM_S32: { s32 v; memcpy(&v, p, sizeof v); return v; }
    case TM_U32: { u32 v; memcpy(&v, p, sizeof v); return v; }
    case TM_S16: { s16 v; memcpy(&v, p, sizeof v); return v; }
    case TM_U16: { u16 v; memcpy(&v, p, sizeof v); return v; }
    case TM_S8: return (signed char)*p;
    default: return *p;
    }
}

/* The key of row r, `row` being its bytes */
static long rowKey(const TmTable *t, int r, const unsigned char *row) {
    TmField kf;

    if (t->keys != NULL) {
        return t->keys[r];
    }
    if (!t->keyFromRow || row == NULL) {
        return t->keyBase + r;
    }
    memset(&kf, 0, sizeof kf);
    kf.type = t->keyType;
    kf.off = t->keyOff;
    return (long)fieldValue(row, &kf);
}

/* The length of a list field of a row without a count (it runs up to its 0; the elements are s16) and whether two lists hold the same elements */
static int listLength(const unsigned char *row, const TmField *f) {
    const short *items;
    int n = 0;

    memcpy(&items, row + f->off, sizeof items);
    while (items != NULL && n <= f->listMax && items[n] != 0) {
        n++;
    }
    return n;
}

static int listSame(const unsigned char *a, const unsigned char *b, const TmField *f, int n) {
    const short *pa, *pb;

    memcpy(&pa, a + f->off, sizeof pa);
    memcpy(&pb, b + f->off, sizeof pb);
    return n == 0 || (pa != NULL && pb != NULL && memcmp(pa, pb, (size_t)n * sizeof(short)) == 0);
}

/* "[1, 2, 3]" (the first 12 elements, "..." after them) of a list field; returns the bytes written */
static int listText(char *out, size_t cap, const unsigned char *row, const TmField *f) {
    const short *items;
    int k, n = 0, len = listLength(row, f);

    memcpy(&items, row + f->off, sizeof items);
    n += snprintf(out + n, cap - (size_t)n, "[");
    for (k = 0; k < len && k < 12 && (size_t)n < cap - 12; k++) {
        n += snprintf(out + n, cap - (size_t)n, "%s%d", k ? ", " : "", items[k]);
    }
    n += snprintf(out + n, cap - (size_t)n, "%s]", len > 12 ? ", ..." : "");
    return n;
}

/* The mods log (channel tables): one line per row a mod changed, with the values now in the game's memory and the
   original of each changed field. Nothing when the log is off or no mod patches the table. */
static void logChanged(const TmTable *t, const char *what) {
    char who[256];
    int r, f;

    if (!plogOn("tables", PLOG_MODS) || tmPatchCount(t->name) == 0) {
        return;
    }
    tmWho(t->name, who, sizeof who);
    for (r = 0; r < t->rows; r++) {
        const unsigned char *now = t->rowAt != NULL ? (const unsigned char *)t->rowAt(r) : (const unsigned char *)t->base + (size_t)r * t->stride;
        long key = rowKey(t, r, now);
        const unsigned char *was = (const unsigned char *)tmOriginalRow(t->name, key);
        char line[400];
        int n;

        if (now == NULL || was == NULL || memcmp(now, was, t->stride) == 0) {
            continue;
        }
        n = snprintf(line, sizeof line, "%s %s %ld:", t->name, what, key);
        for (f = 0; f < t->nfields && (size_t)n < sizeof line - 80; f++) {
            long long a, b;

            if (t->fields[f].listMax > 0) { /* a list: its length and whether it differs (the row holds a pointer) */
                int la = listLength(now, &t->fields[f]), lb = listLength(was, &t->fields[f]);

                if (la != lb || !listSame(now, was, &t->fields[f], la)) {
                    n += snprintf(line + n, sizeof line - (size_t)n, " %s %d items: ", t->fields[f].name, la);
                    n += listText(line + n, sizeof line - (size_t)n, now, &t->fields[f]);
                    n += snprintf(line + n, sizeof line - (size_t)n, " (orig %d: ", lb);
                    n += listText(line + n, sizeof line - (size_t)n, was, &t->fields[f]);
                    n += snprintf(line + n, sizeof line - (size_t)n, "),");
                }
                continue;
            }
            a = fieldValue(now, &t->fields[f]);
            b = fieldValue(was, &t->fields[f]);
            if (a != b) {
                n += snprintf(line + n, sizeof line - (size_t)n, " %s %lld (orig %lld),", t->fields[f].name, a, b);
            }
        }
        line[n - 1] = '\0';
        plogLine("tables", "%s by %s\n", line, who);
    }
}

/* ---- E5b: evolutions, blast forms and growth ---- */

/* the report overlay's data (battle/report/data/stfgtrep.c); the lists are PcEvolution here (same layout as Evolution, boot.c asserts it) */
extern PcEvolution STFGTREP_evolutionLists[8][44];
extern s32 STFGTREP_levelExp[4];
extern s32 STFGTREP_vitalCuts[4];
extern s32 STFGTREP_vitalRandom[9];
extern s32 STFGTREP_statGains[6][9];
extern s32 STFGTREP_resistGains[8];

#define GT_PARTNERS 8
#define GT_EVO_PER_PARTNER 44
#define GT_REAL_DIGIMON 52 /* the rows of DIGIMON_DATA that hold a Digimon (the other three are placeholders) */

/* the value of a Digimon-row field: 0 (none) or the row + 1 of a row the Digimon table has now (the original forms and the mods'
   forms; rows 53-55 are placeholders of the disc, with no model: nothing may learn, need or blast into them) */
static int digimonRefOk(long long v) {
    return v == 0 || (v > 0 && v <= GT_REAL_DIGIMON) || (v > PC_DIGIMON_ORIG_COUNT && v <= pcDigimonRowCount());
}

#define FORM_NOTE \
    "DIGIMON_DATA row + 1 (1-52 the original forms in table order, a mod's partner forms from 56; 53-55 are placeholders and refused), or a name: " \
    "\"dw3:digimon/<id>\" for an original partner form, \"<mod>:<key>\" for a partner form a content mod adds."

static const TmField kEvolutionFields[] = {
    {"digimon", TM_S32, offsetof(PcEvolution, digimon), 0, PC_DIGIMON_ROW_LIMIT,
     "The form the partner learns when it qualifies. " FORM_NOTE " 0 leaves the entry unused (the game skips it).", 0, 0, digimonExtRowRef,
     digimonRefOk},
    {"need1", TM_S16, offsetof(PcEvolution, needs), 0, PC_DIGIMON_ROW_LIMIT,
     "First requirement: a form the partner must already have learned. " FORM_NOTE " 0 for none.", 0, 0, digimonExtRowRef, digimonRefOk},
    {"need1Level", TM_S16, offsetof(PcEvolution, needs) + 2, 0, 99,
     "The level that form must have reached (the entry is skipped while it is lower; 1 when need1 is 0)."},
    {"need2", TM_S16, offsetof(PcEvolution, needs) + 4, 0, PC_DIGIMON_ROW_LIMIT,
     "Second requirement: another form the partner must already have learned. " FORM_NOTE " 0 for none.", 0, 0, digimonExtRowRef,
     digimonRefOk},
    {"need2Level", TM_S16, offsetof(PcEvolution, needs) + 6, 0, 99, "The level that form must have reached (1 when need2 is 0)."},
    {"stat", TM_S16, offsetof(PcEvolution, stat), 0, 14,
     "A requirement on the partner itself: 0 none, 1-6 the battle stats (strength, defense, spirit, wisdom, speed, charisma), 7 the level, "
     "8-14 the seven resistances in the game's order."},
    {"value", TM_S16, offsetof(PcEvolution, value), 0, 32767, "The least that stat must be (read only when stat is not 0)."},
};

static long sEvolutionKeys[GT_PARTNERS * GT_EVO_PER_PARTNER];

static TmTable sEvolutionTable = {
    "evolutions", NULL, sizeof(PcEvolution), GT_PARTNERS * GT_EVO_PER_PARTNER, 0,
    "partner * 100 + entry: partner 0-7 (the eight partners in the game's order), entry 0-43 (the order the game checks them in: after a "
    "battle a partner learns the first entry it qualifies for and has not learned yet, one per report). 203 is partner 2, entry 3. "
    "Entries 44 and up of a list are the ones content mods add with \"evolution\" in digimon.json: they are not rows of this table.",
    "The report reads a partner's list after every battle, so a change shows in the next report. The table patches the 44 original "
    "entries every time the report opens; the entries of digimon.json come after them in the list, so on a tie the originals win "
    "(the first entry that qualifies is learned).",
    kEvolutionFields, (int)(sizeof(kEvolutionFields) / sizeof(kEvolutionFields[0])), 0, 0, TM_S32, NULL, NULL, sEvolutionKeys};

const TmTable *gameTableEvolutions(void) {
    int p, i;

    for (p = 0; p < GT_PARTNERS; p++) {
        for (i = 0; i < GT_EVO_PER_PARTNER; i++) {
            sEvolutionKeys[p * GT_EVO_PER_PARTNER + i] = p * 100 + i;
        }
    }
    sEvolutionTable.base = STFGTREP_evolutionLists;
    return &sEvolutionTable;
}

#define BLAST_NOTE(levels) \
    "The form a blast turns the partner into when its level is " levels ". DIGIMON_DATA row + 1 (1-52 the original forms in table order, " \
    "a mod's partner forms from 56, any row the table has: 16 bits), or a name (\"dw3:digimon/<id>\", \"<mod>:<key>\"); 0 is none, " \
    "which a partner form that can blast should not have."

/* the table's row is the first 12 bytes of PcDigiLinks (ext_state.h): the id and the five blast forms, 16 bits each */
typedef struct BlastRow {
    unsigned short id;
    unsigned short blast[5];
} BlastRow;
_Static_assert(sizeof(BlastRow) == 12 && offsetof(PcDigiLinks, blast) == offsetof(BlastRow, blast), "BlastRow is the head of PcDigiLinks");

#define BLAST_FIELD(n, levels) \
    {"blast" #n, TM_U16, offsetof(BlastRow, blast) + ((n) - 1) * 2, 0, PC_DIGIMON_ROW_LIMIT, BLAST_NOTE(levels), 0, 0, digimonExtRowRef, \
     digimonRefOk}

static const TmField kBlastFields[] = {
    BLAST_FIELD(1, "1 to 3"), BLAST_FIELD(2, "4 to 18"), BLAST_FIELD(3, "19 to 38"), BLAST_FIELD(4, "39 to 69"), BLAST_FIELD(5, "70 and up"),
};

/* WFIGHTMN_blast reads PC_DIGIMON(PC_DIGIMON_BLAST(d, tier) - 1).id: a 0 where the disc has a form is row -1, the port's scratch row (id 0) */
static const char *blastRowCheck(const void *row) {
    const BlastRow *d = (const BlastRow *)row;
    const BlastRow *o = (const BlastRow *)tmOriginalRow("blast_forms", d->id);
    int i;

    for (i = 0; o != NULL && i < 5; i++) {
        if (d->blast[i] == 0 && o->blast[i] != 0) {
            return "a Digimon that blasts needs all five blast forms (a 0 makes the blast turn it into an empty Digimon)";
        }
    }
    return NULL;
}

static long sBlastKeys[GT_REAL_DIGIMON];

static void *blastRowAt(int row) {
    return row >= 0 && row < GT_REAL_DIGIMON ? (void *)pcDigimonLinks(row) : NULL;
}

static TmTable sBlastTable = {
    "blast_forms", NULL, sizeof(BlastRow), GT_REAL_DIGIMON, 0,
    "The Digimon id of the form that blasts (the number after \"dw3:digimon/\"; not the table row). Only the original forms are rows: "
    "a partner form of a mod sets its own with \"blastForms\" in digimon.json (and copies its base's without it).",
    "Set once when the game starts; a blast reads the row of the partner's current form when it starts.",
    kBlastFields, (int)(sizeof(kBlastFields) / sizeof(kBlastFields[0])), 0, 0, TM_S32, NULL, NULL, sBlastKeys, blastRowAt, blastRowCheck};

const TmTable *gameTableBlastForms(void) {
    int i;

    for (i = 0; i < GT_REAL_DIGIMON; i++) sBlastKeys[i] = (long)pcDigimonLinks(i)->id;
    return &sBlastTable;
}

#define LEVEL_WHEN \
    "A partner's growth is read in the battle report each time it gains a level, so a change shows in the next report that levels one up."

/* the levels each tier starts at: level_exp and vital_cuts have four, stat_gains six */
static const long kTier4Keys[4] = {1, 5, 20, 40};
static const long kTier6Keys[6] = {1, 5, 20, 40, 60, 80};

static const TmField kLevelExpFields[] = {
    {"exp", TM_S32, 0, -1000000000, 1000000000,
     "Added to the exp a level needs: (level^3 + 5 * level - 6) * expRate / 10 + this must be below the partner's exp for it to reach "
     "the level. Above 999999 the tier's levels cannot be reached (a partner's exp stops there). The limits keep the sum inside 32 bits."},
};

static TmTable sLevelExpTable = {
    "level_exp", NULL, sizeof(s32), 4, 0,
    "The first level of the tier of the level being reached: 1 (levels 1 to 4), 5 (5 to 19), 20 (20 to 39), 40 (40 to 99).",
    LEVEL_WHEN, kLevelExpFields, 1, 0, 0, TM_S32, NULL, NULL, kTier4Keys};

const TmTable *gameTableLevelExp(void) {
    sLevelExpTable.base = STFGTREP_levelExp;
    return &sLevelExpTable;
}

static const TmField kVitalCutFields[] = {
    {"cut", TM_S32, 0, 0, 255,
     "Taken from a level's HP and MP gain: hpGrowth (or mpGrowth) - cut + a value of vital_random is added to the maximum. Keep the total "
     "above 0 or the maximum goes down; the maximum stops at 9999."},
};

static TmTable sVitalCutTable = {
    "vital_cuts", NULL, sizeof(s32), 4, 0,
    "The first level of the tier of the level reached: 1 (levels 1 to 4), 5 (5 to 19), 20 (20 to 39), 40 (40 and up).",
    LEVEL_WHEN, kVitalCutFields, 1, 0, 0, TM_S32, NULL, NULL, kTier4Keys};

const TmTable *gameTableVitalCuts(void) {
    sVitalCutTable.base = STFGTREP_vitalCuts;
    return &sVitalCutTable;
}

static const TmField kVitalRandomFields[] = {
    {"value", TM_S32, 0, -255, 255,
     "One of the nine values added to a level's HP and MP gain (see vital_cuts); the game picks one of the nine at random for each, all "
     "equally likely. Keep the total gain above 0."},
};

static const TmTable kVitalRandomTable = {
    "vital_random", STFGTREP_vitalRandom, sizeof(s32), 9, 0,
    "The slot, 0 to 8 (each is as likely as any other).",
    LEVEL_WHEN, kVitalRandomFields, 1};

const TmTable *gameTableVitalRandom(void) {
    return &kVitalRandomTable;
}

#define GAIN_FIELD(n) \
    {"gain" #n, TM_S32, (n) * 4, 0, 999, \
     "A battle stat's gain for a level when the partner's statGrowth of that stat (1-5 on the disc) plus a random 0-4 comes to " #n \
    ". A stat stops at 999. The game reads slot growth + random with no cap, so 5 + 4 = 9 reads one past the row (the next tier's gain0)."}

static const TmField kStatGainFields[] = {GAIN_FIELD(0), GAIN_FIELD(1), GAIN_FIELD(2), GAIN_FIELD(3), GAIN_FIELD(4),
                                          GAIN_FIELD(5), GAIN_FIELD(6), GAIN_FIELD(7), GAIN_FIELD(8)};

static TmTable sStatGainTable = {
    "stat_gains", NULL, 9 * sizeof(s32), 6, 0,
    "The first level of the tier of the level reached: 1 (levels 1 to 4), 5 (5 to 19), 20 (20 to 39), 40 (40 to 59), 60 (60 to 79), 80 (80 and up).",
    LEVEL_WHEN, kStatGainFields, 9, 0, 0, TM_S32, NULL, NULL, kTier6Keys};

const TmTable *gameTableStatGains(void) {
    sStatGainTable.base = STFGTREP_statGains;
    return &sStatGainTable;
}

static const TmField kResistGainFields[] = {
    {"gain", TM_S32, 0, 0, 999,
     "A resistance's gain for a level (up to level 40) when the partner's resistGrowth of it (1-5 on the disc) plus a random 0-3 is the "
     "slot. A resistance stops at 999. Slot 8 (5 + 3) is read too, one past the table: the game gets whatever follows it in memory."},
};

static const TmTable kResistGainTable = {
    "resist_gains", STFGTREP_resistGains, sizeof(s32), 8, 0,
    "The slot, 0 to 7: the partner's resistGrowth of that resistance (1-5 on the disc) plus a random 0 to 3 (slot 8 is read past the table).",
    LEVEL_WHEN, kResistGainFields, 1};

const TmTable *gameTableResistGains(void) {
    return &kResistGainTable;
}

static const TmTable *reportTable(int i) {
    switch (i) {
    case 0: return gameTableEvolutions();
    case 1: return gameTableLevelExp();
    case 2: return gameTableVitalCuts();
    case 3: return gameTableVitalRandom();
    case 4: return gameTableStatGains();
    default: return gameTableResistGains();
    }
}

#define GT_REPORT_TABLES 6
static int sReportLogged[GT_REPORT_TABLES];

int gameTablesRegisterReport(void) {
    int i, n = 0;

    for (i = 0; i < GT_REPORT_TABLES; i++) {
        sReportLogged[i] = 0;
        n += tmRegister(reportTable(i)) == 0;
    }
    return n;
}

void gameTablesApplyReport(void *(*extendedList)(int partner, void *original)) {
    int i, p;

    for (i = 0; i < GT_REPORT_TABLES; i++) {
        const TmTable *t = reportTable(i);

        if (tmApply(t->name) > 0 && !sReportLogged[i]) {
            sReportLogged[i] = 1;
            logChanged(t, "row");
        }
    }
    /* the port's extended list (original entries copied, then the mods'): the patched originals go into its first 44 entries */
    if (extendedList != NULL && tmPatchCount("evolutions") > 0) {
        for (p = 0; p < GT_PARTNERS; p++) {
            PcEvolution *list = (PcEvolution *)extendedList(p, NULL);

            if (list != NULL) {
                memcpy(list, STFGTREP_evolutionLists[p], sizeof STFGTREP_evolutionLists[p]);
            }
        }
    }
}

void gameTablesRegister(void) {
    const TmTable *all[4];
    int i;

    all[0] = gameTableWeapons();
    all[1] = gameTableArmor();
    all[2] = gameTableAccessories();
    all[3] = gameTableItemEffects();
    for (i = 0; i < 4; i++) {
        if (tmRegister(all[i]) == 0) {
            tmApply(all[i]->name);
            logChanged(all[i], "item");
        }
    }
    pcDigimonLinksInit(); /* the 16-bit copy of the disc's bytes: what blast_forms patches and the game reads (U9) */
    if (tmRegister(gameTableBlastForms()) == 0) {
        tmApply("blast_forms");
        logChanged(gameTableBlastForms(), "Digimon");
    }
}


/* ---- content-export E5c: the field's battle data (FIELDSTG.PRO's tables) ---- */

#define GT_FIELD_ENEMIES 523   /* FIELDSTG_enemies of the EU build (row 0 is "none", which fills the empty slots) */
#define GT_FIELD_ENCOUNTERS 335 /* FIELDSTG_encounters */
#define GT_BATTLE_RATES 6       /* FIELDSTG_battleRates; the count of a battle list indexes it */

extern BattleEnemy FIELDSTG_enemies[GT_FIELD_ENEMIES];
extern GameFieldEncounter FIELDSTG_encounters[GT_FIELD_ENCOUNTERS];
extern s32 FIELDSTG_battleRates[GT_BATTLE_RATES];

static const char *(*sFighterName)(int id);

void gameTablesSetFighterNames(const char *(*nameOf)(int id)) {
    sFighterName = nameOf;
}

static int isPartnerId(int id) {
    int i;

    if (id >= PC_DIGIMON_FIRST_NEW_ID) {
        return digimonExtIsPartner(id);
    }
    for (i = 0; i < PC_DIGIMON_ORIG_COUNT; i++) {
        if (PC_DIGIMON(i).id == id) {
            return 1;
        }
    }
    return 0;
}

/* a fighter an encounter can field: 0 (none), an original enemy or an enemy of a content mod. A partner form has no battle table row. */
static int enemyIdOk(long long id) {
    if (id == 0) {
        return 1;
    }
    if (id < 0 || id > 32767) {
        return 0;
    }
    return digimonExtNameId((int)id) >= 0 && !isPartnerId((int)id);
}

static long long enemyByName(const char *name) {
    int id = contentOriginalId(digimonExtType(), name);

    if (id < 0) {
        id = digimonExtNewId(name);
    }
    return enemyIdOk(id) ? id : -1;
}

/* the same two for the new encounters of the mods (shim/field_ext.c): a name or a number an encounter may field; negative / 0: not an enemy */
int gameEnemyByName(const char *name) {
    return (int)enemyByName(name);
}

int gameEnemyIdOk(int id) {
    return enemyIdOk(id);
}

static void fighterLabel(int id, char *buf, size_t cap) {
    const char *n = id > 0 && sFighterName != NULL ? sFighterName(id) : NULL;

    if (n != NULL) {
        snprintf(buf, cap, "%s", n);
    }
}

#define ENEMY_FIGHTER_NOTE \
    "The fighter: a battle table id (the enemies' ids; \"dw3:digimon/<id>\" or \"<mod>:<key>\" for a Digimon a content mod adds as an enemy). " \
    "A partner form has no battle table row and is refused. An enemy row cannot be emptied (0): the encounters using it would field a fighter 0."

static const TmField kFieldEnemyFields[] = {
    {"fighter", TM_S32, offsetof(BattleEnemy, fighter), 0, 32767, ENEMY_FIGHTER_NOTE, 0, 0, enemyByName, enemyIdOk},
    {"level", TM_S16, offsetof(BattleEnemy, level), 1, 99,
     "The enemy's level (1-99): its stats' level and, for the first enemy of an ambush-able encounter, the gap that lowers the ambush chance."},
    {"hp", TM_S16, offsetof(BattleEnemy, hp), 1, 32767, "Its hit points when the battle starts (also its maximum); 0 would start it defeated."},
    {"mp", TM_S16, offsetof(BattleEnemy, mp), 0, 32767, "Its magic points when the battle starts (also its maximum)."},
    {"strength", TM_S16, offsetof(BattleEnemy, strength), 1, 500,
     "Scales its five stats and its techniques' power, in 16ths: the battle table's stat * strength / 16 (16 is the table's own, the "
     "disc goes from 5 to 186). The scaled stats are kept in 16 bits: the highest stat of the disc's enemies is 930, which 500 keeps "
     "under 32767 (a mod enemy with bigger stats can still overflow)."},
};

/* row 0 is the empty slot every encounter shares; any other row of the disc has a fighter and keeps one */
static const char *fieldEnemyCheck(const void *row) {
    const BattleEnemy *b = (const BattleEnemy *)row;
    long idx = (long)(b - FIELDSTG_enemies);
    const BattleEnemy *o = (const BattleEnemy *)tmOriginalRow("field_enemies", idx);

    if (o == NULL) {
        return NULL;
    }
    if (idx == 0) {
        return memcmp(b, o, sizeof *b) != 0 ? "row 0 is the empty slot the encounters share; it cannot change" : NULL;
    }
    if (o->fighter != 0 && b->fighter == 0) {
        return "an enemy cannot be emptied: the encounters that use this row would field a fighter 0";
    }
    return NULL;
}

static void fieldEnemyLabel(int row, char *buf, size_t cap) {
    fighterLabel((int)FIELDSTG_enemies[row].fighter, buf, cap);
}

static TmTable sFieldEnemyTable = {
    "field_enemies", NULL, sizeof(BattleEnemy), GT_FIELD_ENEMIES, 0,
    "The row of FIELDSTG_enemies (0-522; row 0 is the empty slot and cannot change). The enemy rows are shared by the encounters: field_encounters "
    "names them by this number, so a change here changes every encounter that points at the row. \"labels\" gives each row's Digimon.",
    "Set on every entry to the field (the field's data is restored each time and the patch goes on after it); a change shows in the next battle "
    "the field starts.",
    kFieldEnemyFields, (int)(sizeof(kFieldEnemyFields) / sizeof(kFieldEnemyFields[0])), 0, 0, TM_S32, NULL, NULL, NULL, NULL, fieldEnemyCheck,
    fieldEnemyLabel};

const TmTable *gameTableFieldEnemies(void) {
    sFieldEnemyTable.base = FIELDSTG_enemies;
    return &sFieldEnemyTable;
}

/* An encounter as the table sees it: the pointers become row numbers of field_enemies (the game's row has 8-byte pointers on the host) */
typedef struct FieldEncRow {
    s32 enemy[3];
    u8 ambushChance;
    u8 kind;
    u8 blocks[12];
} FieldEncRow;

static FieldEncRow sEncRows[GT_FIELD_ENCOUNTERS];

int gameTablesEncounterCount(void) {
    return GT_FIELD_ENCOUNTERS;
}

#define ENEMY_SLOT_NOTE(n) \
    "The " n " enemy: the row of field_enemies (0-522; 0 is empty). The enemy rows are shared: patch field_enemies to change the Digimon, level " \
    "or stats itself."

#define BLOCK_NOTE(what) "1 keeps the player from " what " in this battle, 0 allows it (the game tests it for zero)."

static const TmField kFieldEncounterFields[] = {
    {"enemy1", TM_S32, offsetof(FieldEncRow, enemy), 0, GT_FIELD_ENEMIES - 1,
     ENEMY_SLOT_NOTE("first") " It cannot be empty: the battle's first enemy draws the model and the HUD."},
    {"enemy2", TM_S32, offsetof(FieldEncRow, enemy) + 4, 0, GT_FIELD_ENEMIES - 1, ENEMY_SLOT_NOTE("second")},
    {"enemy3", TM_S32, offsetof(FieldEncRow, enemy) + 8, 0, GT_FIELD_ENEMIES - 1, ENEMY_SLOT_NOTE("third")},
    {"ambushChance", TM_U8, offsetof(FieldEncRow, ambushChance), 0, 255,
     "The chance of an ambush, scaled by how far the first partner's level and the first enemy's are under 32: chance * (32 - partner level - "
     "enemy level) / 32 is compared with a roll of 0-127. 0 never (the disc has 0 and 127)."},
    {"kind", TM_U8, offsetof(FieldEncRow, kind), 0, 255,
     "The encounter kind the enemies' battle table actions can test: 1 nearly all the maps' random battles (some enemies run away in them), "
     "4 the Digimon Arena's, 2, 3 and 5 mostly the event battles (the disc has 0-5)."},
    {"poison", TM_U8, offsetof(FieldEncRow, blocks), 0, 1, BLOCK_NOTE("poisoning")},
    {"paralysis", TM_U8, offsetof(FieldEncRow, blocks) + 1, 0, 1, BLOCK_NOTE("paralysing")},
    {"confusion", TM_U8, offsetof(FieldEncRow, blocks) + 2, 0, 1, BLOCK_NOTE("confusing")},
    {"sleep", TM_U8, offsetof(FieldEncRow, blocks) + 3, 0, 1, BLOCK_NOTE("putting to sleep")},
    {"knockOut", TM_U8, offsetof(FieldEncRow, blocks) + 4, 0, 1, BLOCK_NOTE("knocking out")},
    {"drain", TM_U8, offsetof(FieldEncRow, blocks) + 5, 0, 1, BLOCK_NOTE("draining")},
    {"block6", TM_U8, offsetof(FieldEncRow, blocks) + 6, 0, 1, "Slot 6 of the blocks: nothing in the game reads it."},
    {"steal", TM_U8, offsetof(FieldEncRow, blocks) + 7, 0, 1, BLOCK_NOTE("stealing")},
    {"lowerAttack", TM_U8, offsetof(FieldEncRow, blocks) + 8, 0, 1, BLOCK_NOTE("lowering the enemy's attack")},
    {"lowerDefense", TM_U8, offsetof(FieldEncRow, blocks) + 9, 0, 1, BLOCK_NOTE("lowering the enemy's defense")},
    {"lowerSpeed", TM_U8, offsetof(FieldEncRow, blocks) + 10, 0, 1, BLOCK_NOTE("lowering the enemy's speed")},
    {"runAway", TM_U8, offsetof(FieldEncRow, blocks) + 11, 0, 1, BLOCK_NOTE("running away")},
};

/* encounter 0 is the empty one the battle lists use for "none": it stays; the first enemy must be a fighter (the current, patched enemy rows) */
static const char *fieldEncounterCheck(const void *row) {
    const FieldEncRow *r = (const FieldEncRow *)row;
    long idx = (long)(r - sEncRows);
    const FieldEncRow *o = (const FieldEncRow *)tmOriginalRow("field_encounters", idx);

    if (o == NULL) {
        return NULL;
    }
    if (idx == 0) {
        return memcmp(r, o, sizeof *r) != 0 ? "encounter 0 is the empty one the battle lists use for none; it cannot change" : NULL;
    }
    if (FIELDSTG_enemies[r->enemy[0]].fighter == 0) {
        return "the first enemy cannot be empty (the battle draws its model and HUD from it)";
    }
    return NULL;
}

static void fieldEncounterLabel(int row, char *buf, size_t cap) {
    int k;
    size_t n = 0;

    buf[0] = '\0';
    for (k = 0; k < 3 && n + 2 < cap; k++) {
        char one[96];

        one[0] = '\0';
        fighterLabel((int)FIELDSTG_enemies[sEncRows[row].enemy[k]].fighter, one, sizeof one);
        if (one[0] != '\0') {
            n += (size_t)snprintf(buf + n, cap - n, "%s%s", n != 0 ? ", " : "", one);
            if (n >= cap) {
                n = cap - 1;
            }
        }
    }
}

/* a key with a colon is a new encounter of the mods' ("<mod>:<name>", shim/field_ext.c), which that module reads from the same file */
static int fieldEncounterOwnKey(const char *key) {
    return strchr(key, ':') != NULL;
}

static TmTable sFieldEncounterTable = {
    "field_encounters", NULL, sizeof(FieldEncRow), GT_FIELD_ENCOUNTERS, 0,
    "The index of FIELDSTG_encounters (0-334; 0 is the empty encounter and cannot change): the \"encounter\" of the battles in a stage's battle lists "
    "and the number of the \"encounters\" of digimon.json. \"labels\" gives each encounter's enemies.",
    "Set on every entry to the field (after the field's data is restored); a change shows in the next battle that uses the encounter. "
    "The \"encounters\" placements of digimon.json go on after this table, over the patched slots.",
    kFieldEncounterFields, (int)(sizeof(kFieldEncounterFields) / sizeof(kFieldEncounterFields[0])), 0, 0, TM_S32, NULL, NULL, NULL, NULL,
    fieldEncounterCheck, fieldEncounterLabel, fieldEncounterOwnKey};

const TmTable *gameTableFieldEncounters(void) {
    int i, k;

    for (i = 0; i < GT_FIELD_ENCOUNTERS; i++) {
        const GameFieldEncounter *g = &FIELDSTG_encounters[i];
        FieldEncRow *r = &sEncRows[i];

        for (k = 0; k < 3; k++) {
            long at = (long)(g->enemies[k] - FIELDSTG_enemies);

            r->enemy[k] = at >= 0 && at < GT_FIELD_ENEMIES ? (s32)at : 0;
        }
        r->ambushChance = g->ambushChance;
        r->kind = g->kind;
        memcpy(r->blocks, g->blocks, sizeof r->blocks);
    }
    sFieldEncounterTable.base = sEncRows;
    return &sFieldEncounterTable;
}

static const TmField kBattleRateFields[] = {
    {"rate", TM_S32, 0, 0, 2000,
     "How many steps the area's rate takes off the count to the next battle at each step. The count is rolled at 0-1279, so 2000 (the disc's "
     "first rate) is a battle at the next step and anything above 1279 does the same; 0 never lowers it (no random battles). The disc has "
     "2000, 3, 4, 6, 9 and 18."},
};

static const TmTable kBattleRateTable = {
    "battle_rates", FIELDSTG_battleRates, sizeof(s32), GT_BATTLE_RATES, 0,
    "The rate index, 0-5: the \"count\" of a stage's battle list (FieldBattles in the WSTAG overlays; not a table yet). The stages keep their own "
    "index, so a change here changes every area that uses it.",
    "Set on every entry to the field (after the field's data is restored); a change shows from the next step.",
    kBattleRateFields, 1};

const TmTable *gameTableBattleRates(void) {
    return &kBattleRateTable;
}

static const TmTable *fieldTable(int i) {
    switch (i) {
    case 0: return gameTableFieldEnemies();
    case 1: return gameTableFieldEncounters();
    default: return gameTableBattleRates();
    }
}

#define GT_FIELD_TABLES 3
static int sFieldLogged[GT_FIELD_TABLES];

int gameTablesRegisterField(void) {
    int i, n = 0;

    for (i = 0; i < GT_FIELD_TABLES; i++) {
        sFieldLogged[i] = 0;
        n += tmRegister(fieldTable(i)) == 0;
    }
    return n;
}

void gameTablesApplyField(void) {
    int i, k;

    for (i = 0; i < GT_FIELD_TABLES; i++) {
        const TmTable *t = fieldTable(i);
        int changed = tmApply(t->name);

        if (changed > 0 && !sFieldLogged[i]) {
            sFieldLogged[i] = 1;
            logChanged(t, "row");
        }
        /* the game's encounters hold pointers: write the patched rows back (after every apply, so a restored overlay never keeps a stale one) */
        if (i == 1 && tmPatchCount("field_encounters") > 0) {
            for (k = 0; k < GT_FIELD_ENCOUNTERS; k++) {
                GameFieldEncounter *g = &FIELDSTG_encounters[k];
                const FieldEncRow *r = &sEncRows[k];
                int s;

                for (s = 0; s < 3; s++) {
                    g->enemies[s] = &FIELDSTG_enemies[r->enemy[s]];
                }
                g->ambushChance = r->ambushChance;
                g->kind = r->kind;
                memcpy(g->blocks, r->blocks, sizeof g->blocks);
            }
        }
    }
}

/* ---- content-export E5d: the card game (STCRDSHP.PRO's and CARDGAME.PRO's tables) ---- */

#define GT_CARD_PRICES 44   /* STCRDSHP_prices of the EU build, without its {0, 1} end row */
#define GT_CARD_STOCKS 24   /* STCRDSHP_stocks, without its {-1} end row */
#define GT_PACKS 35         /* STCRDSHP_packs, without its {0} end row */
#define GT_PACK_SLOTS (GT_PACKS * 6)
#define GT_CARD_EFFECTS 60  /* CARDGAME_cardEffects: the effects of the first 60 cards (card id 1-60) */
#define GT_PRIZES 36        /* CARDGAME_prizeItems */
#define GT_DECK_CARDS 40    /* the cards of a deck: CARDGAME_defaultDeck holds 80 values, and only the first 40 are read (battle.c loadChosenDeck) */
#define GT_CARD_MAX 314     /* the disc's card ids: 1-314 (315 and 316 exist in the album but are no card a table can use) */
#define GT_CARD_ID_MAX (PC_CARD_LIST_MAX - 1) /* the largest id a table may hold: the cards content mods add (cardExtIdOk says which exist) */
#define GT_EFFECT_STEP_MAX 174 /* the steps CARDGAME_runEffectStep has a case for: 1-174 */
#define GT_STOCK_LIST_MAX 29   /* the shop pages six cards at a time and may read to the end of the last page: 30 slots, the 0 included */

extern GameCardPrice STCRDSHP_prices[GT_CARD_PRICES + 1];
extern GameCardStock STCRDSHP_stocks[GT_CARD_STOCKS + 1];
extern GameCardPack STCRDSHP_packs[GT_PACKS + 1];
extern GameCardEffect CARDGAME_cardEffects[GT_CARD_EFFECTS];
extern s16 CARDGAME_prizeItems[GT_PRIZES];
extern u16 CARDGAME_defaultDeck[2 * GT_DECK_CARDS];

static const char *(*sCardName)(int id);
static const char *(*sItemName)(int id);

void gameTablesSetCardNames(const char *(*nameOf)(int id)) {
    sCardName = nameOf;
}

void gameTablesSetItemNames(const char *(*nameOf)(int id)) {
    sItemName = nameOf;
}

static void cardLabel(int id, char *buf, size_t cap) {
    const char *n = id > 0 && sCardName != NULL ? sCardName(id) : NULL;

    if (n != NULL) {
        snprintf(buf, cap, "%s", n);
    }
}

/* The hooks of every card id field (TmField.byName / idOk): a disc card 1-314 or a card a content mod adds, by number or "<mod>:<name>".
   The deck stores the id minus one and a counter list ends in 0, so those two have their own wrappers. */
static long long cardNameMinusOne(const char *name) {
    long long id = cardExtByName(name);

    return id < 0 ? -1 : id - 1;
}

static int cardIdMinusOneOk(long long v) {
    return cardExtIdOk(v + 1);
}

static int cardIdOrZeroOk(long long v) {
    return v == 0 || cardExtIdOk(v);
}

/* card_prices: the key is the card id, so the row's own `card` is not a field (a row cannot be made to price another card) */
static const TmField kCardPriceFields[] = {
    {"price", TM_S16, offsetof(GameCardPrice, price), 0, 32767,
     "What the card shop charges for one copy (the list, the money check and the payment read it; 0 is free). A disc card the shops sell that has no "
     "row here costs 1: the 44 rows of the disc cannot grow. The price of a card a mod adds is its own \"price\" in the mod's cards.json "
     "(default 1), not a row here."},
};

static void cardPriceLabel(int row, char *buf, size_t cap) {
    cardLabel(STCRDSHP_prices[row].card, buf, cap);
}

static const TmTable kCardPriceTable = {
    "card_prices", STCRDSHP_prices, sizeof(GameCardPrice), GT_CARD_PRICES, 0,
    "The card id (1-314), one of the 44 cards the disc prices (a card from a mod is priced in its cards.json, not here). \"labels\" gives each card's name. The shop looks the price up by card id (the first "
    "row with that id), so it is the same in every shop.",
    "Set on every entry to the card shop (the shop's data is restored each time and the patch goes on after it); the shop reads the price when it "
    "lists a card and when it is bought.",
    kCardPriceFields, 1, 1, offsetof(GameCardPrice, card), TM_S16, NULL, NULL, NULL, NULL, NULL, cardPriceLabel};

const TmTable *gameTableCardPrices(void) {
    return &kCardPriceTable;
}

/* card_shop_stock: a list of card ids per shop. The row has no count the engine could keep (getStock counts up to the 0 itself). */
static const TmField kCardStockFields[] = {
    {"cards", TM_S16, offsetof(GameCardStock, cards), 1, GT_CARD_ID_MAX,
     "The card ids the shop sells, in the order it lists them (six a page; the disc's shops sell five or six): a disc card id 1-314, or "
     "\"<mod>:<name>\" for a card a content mod adds. A disc card with no row in card_prices costs 1; a mod's card costs its cards.json price.",
     GT_STOCK_LIST_MAX, TM_NO_COUNT, cardExtByName, cardExtIdOk},
};

static void cardStockLabel(int row, char *buf, size_t cap) {
    const short *c = STCRDSHP_stocks[row].cards;
    size_t n = 0;
    int k;

    buf[0] = '\0';
    for (k = 0; c != NULL && c[k] != 0 && k < GT_STOCK_LIST_MAX && n + 2 < cap; k++) {
        char one[96];

        one[0] = '\0';
        cardLabel(c[k], one, sizeof one);
        if (one[0] != '\0') {
            n += (size_t)snprintf(buf + n, cap - n, "%s%s", n != 0 ? ", " : "", one);
            if (n >= cap) {
                n = cap - 1;
            }
        }
    }
}

static const TmTable kCardStockTable = {
    "card_shop_stock", STCRDSHP_stocks, sizeof(GameCardStock), GT_CARD_STOCKS, 0,
    "The shop number the card shop is opened with (49-67 and 70-74, the field mode argument of each shop; a number with no row opens the first "
    "shop's stock). \"labels\" gives each shop's card names.",
    "Set on every entry to the card shop (after the shop's data is restored); the shop reads its list when the buy screen opens.",
    kCardStockFields, 1, 1, offsetof(GameCardStock, shop), TM_S32, NULL, NULL, NULL, NULL, NULL, cardStockLabel};

const TmTable *gameTableCardShopStock(void) {
    return &kCardStockTable;
}

/* card_pack_slots: 210 lists of 16 cards that the packs' slots point at (scattered over three arrays, so the rows go through rowAt) */
typedef struct PackSlot {
    s32 card[16];
} PackSlot;

static long sPackSlotKeys[GT_PACK_SLOTS];

static void *packSlotAt(int row) {
    return row >= 0 && row < GT_PACK_SLOTS ? (void *)STCRDSHP_packs[row / 6].slots[row % 6] : NULL;
}

#define PACK_CARD_NOTE(n) \
    "Card " #n " of the slot's 16: opening a pack draws one of the 16 at random for each slot, so a card listed twice is twice as likely (a card id 1-314, or \"<mod>:<name>\" for a card a content mod adds)."

#define PACK_CARD_FIELD(n) \
    {"card" #n, TM_S32, offsetof(PackSlot, card) + ((n) - 1) * 4, 1, GT_CARD_ID_MAX, PACK_CARD_NOTE(n), 0, 0, cardExtByName, cardExtIdOk}

static const TmField kPackSlotFields[] = {
    PACK_CARD_FIELD(1), PACK_CARD_FIELD(2), PACK_CARD_FIELD(3), PACK_CARD_FIELD(4), PACK_CARD_FIELD(5), PACK_CARD_FIELD(6),
    PACK_CARD_FIELD(7), PACK_CARD_FIELD(8), PACK_CARD_FIELD(9), PACK_CARD_FIELD(10), PACK_CARD_FIELD(11), PACK_CARD_FIELD(12),
    PACK_CARD_FIELD(13), PACK_CARD_FIELD(14), PACK_CARD_FIELD(15), PACK_CARD_FIELD(16),
};

static void packSlotLabel(int row, char *buf, size_t cap) {
    int item = row >= 0 && row < GT_PACK_SLOTS ? STCRDSHP_packs[row / 6].item : 0;
    const char *n = item > 0 && sItemName != NULL ? sItemName(item) : NULL;

    snprintf(buf, cap, "%s slot %d", n != NULL ? n : "pack", row % 6 + 1);
}

static TmTable sPackSlotTable = {
    "card_pack_slots", NULL, sizeof(PackSlot), GT_PACK_SLOTS, 0,
    "The pack's item id times 10 plus the slot (0-5): 3610 is the first slot of item 361. The 35 packs are the items 91 and 361-394; each draws six cards, "
    "one from each of its six slots. \"labels\" gives each row's pack and slot.",
    "Set on every entry to the card shop (after the shop's data is restored); a pack reads its slots when it is opened.",
    kPackSlotFields, (int)(sizeof(kPackSlotFields) / sizeof(kPackSlotFields[0])), 0, 0, TM_S32, NULL, NULL, sPackSlotKeys, packSlotAt, NULL, packSlotLabel};

const TmTable *gameTableCardPackSlots(void) {
    int p, s;

    for (p = 0; p < GT_PACKS; p++) {
        for (s = 0; s < 6; s++) {
            sPackSlotKeys[p * 6 + s] = (long)STCRDSHP_packs[p].item * 10 + s;
        }
    }
    return &sPackSlotTable;
}

static const TmTable *shopTable(int i) {
    switch (i) {
    case 0: return gameTableCardPrices();
    case 1: return gameTableCardShopStock();
    default: return gameTableCardPackSlots();
    }
}

#define GT_SHOP_TABLES 3
static int sShopLogged[GT_SHOP_TABLES];

int gameTablesRegisterCardShop(void) {
    int i, n = 0;

    for (i = 0; i < GT_SHOP_TABLES; i++) {
        sShopLogged[i] = 0;
        n += tmRegister(shopTable(i)) == 0;
    }
    return n;
}

void gameTablesApplyCardShop(void) {
    int i;

    for (i = 0; i < GT_SHOP_TABLES; i++) {
        const TmTable *t = shopTable(i);
        int changed = tmApply(t->name);

        if (changed > 0 && !sShopLogged[i]) {
            sShopLogged[i] = 1;
            logChanged(t, i == 2 ? "slot" : i == 1 ? "shop" : "card");
        }
    }
}

/* card_effects: the four head fields and the 40 steps of the script */
#define EFFECT_STEP_NOTE(n) \
    "Step " #n " of the card's script, a step number 1-174 (a case of CARDGAME_runEffectStep) that runs after the one before; 0 ends the script, so the " \
    "script has to hold a 0 among its 40 values (the card's row is refused otherwise). Some steps skip the ones after them: a jump past the 0 runs into " \
    "the next card's row."

#define EFFECT_STEP_FIELD(n) \
    {"step" #n, TM_U8, offsetof(GameCardEffect, script) + (n) - 1, 0, GT_EFFECT_STEP_MAX, EFFECT_STEP_NOTE(n)}

static const TmField kCardEffectFields[] = {
    {"condition", TM_U8, offsetof(GameCardEffect, condition), 0, 255,
     "When the computer plays the card: a case of CARDGAME_checkComputerCondition (the disc uses 0x70-0x96 and a few small numbers); a value that is not "
     "one of its cases means the computer never plays the card. The player is never held back by it."},
    {"playCondition", TM_U8, offsetof(GameCardEffect, playCondition), 0, 255,
     "A condition that stops the card's effect when it holds and shows the message instead (CARDGAME_checkPlayCondition: 1-11 are its cases; 0 or any other "
     "value is no condition)."},
    {"message", TM_U8, offsetof(GameCardEffect, message), 0, 166,
     "The card battle text (CARDGM) shown when the play condition holds (0 shows nothing). 0-166: the highest entry the game's own code opens (0xA6)."},
    {"target", TM_U8, offsetof(GameCardEffect, target), 0, 9,
     "The kind of target the play marks (CARDGAME_flagTargetSlots): 0 the one slot the play names, 1 all the slots of side 1 (the opponent's), 2 those of side "
     "0 (the player's), 3 every slot, 4-8 the slots whose card color is not 1 / not 2 / 3 / not 4 / 6, 9 none."},
    EFFECT_STEP_FIELD(1), EFFECT_STEP_FIELD(2), EFFECT_STEP_FIELD(3), EFFECT_STEP_FIELD(4), EFFECT_STEP_FIELD(5), EFFECT_STEP_FIELD(6),
    EFFECT_STEP_FIELD(7), EFFECT_STEP_FIELD(8), EFFECT_STEP_FIELD(9), EFFECT_STEP_FIELD(10), EFFECT_STEP_FIELD(11), EFFECT_STEP_FIELD(12),
    EFFECT_STEP_FIELD(13), EFFECT_STEP_FIELD(14), EFFECT_STEP_FIELD(15), EFFECT_STEP_FIELD(16), EFFECT_STEP_FIELD(17), EFFECT_STEP_FIELD(18),
    EFFECT_STEP_FIELD(19), EFFECT_STEP_FIELD(20), EFFECT_STEP_FIELD(21), EFFECT_STEP_FIELD(22), EFFECT_STEP_FIELD(23), EFFECT_STEP_FIELD(24),
    EFFECT_STEP_FIELD(25), EFFECT_STEP_FIELD(26), EFFECT_STEP_FIELD(27), EFFECT_STEP_FIELD(28), EFFECT_STEP_FIELD(29), EFFECT_STEP_FIELD(30),
    EFFECT_STEP_FIELD(31), EFFECT_STEP_FIELD(32), EFFECT_STEP_FIELD(33), EFFECT_STEP_FIELD(34), EFFECT_STEP_FIELD(35), EFFECT_STEP_FIELD(36),
    EFFECT_STEP_FIELD(37), EFFECT_STEP_FIELD(38), EFFECT_STEP_FIELD(39), EFFECT_STEP_FIELD(40),
};

/* a script with no 0 in its 40 steps never ends: the game would run on into the next row (and, in the last, past the table) */
static const char *cardEffectCheck(const void *row) {
    const GameCardEffect *e = (const GameCardEffect *)row;
    int i;

    for (i = 0; i < 40; i++) {
        if (e->script[i] == 0) {
            return NULL;
        }
    }
    return "its script has no 0 step to end it (the game would run on into the next card's row)";
}

static void cardEffectLabel(int row, char *buf, size_t cap) {
    cardLabel(row + 1, buf, cap);
}

static const TmTable kCardEffectTable = {
    "card_effects", CARDGAME_cardEffects, sizeof(GameCardEffect), GT_CARD_EFFECTS, 1,
    "The card id (1-60): the effects exist for the first 60 cards only (the game reads CARDGAME_cardEffects[id - 1]). \"labels\" gives each card's name.",
    "Set on every entry to the card battle (the battle's data is restored each time and the patch goes on after it); a change shows the next time the "
    "card is played.",
    kCardEffectFields, (int)(sizeof(kCardEffectFields) / sizeof(kCardEffectFields[0])), 0, 0, TM_S32, NULL, NULL, NULL, NULL, cardEffectCheck,
    cardEffectLabel};

const TmTable *gameTableCardEffects(void) {
    return &kCardEffectTable;
}

/* prize_items: the item an opponent gives; an item id the shops and bags know (a mod's item by name) */
static const TmField kPrizeFields[] = {
    {"item", TM_S16, 0, 1, 32767,
     "The item the player wins from an opponent that points at this prize (CardOpponent.prize of CARD_NPC.BIN): an item id, or \"<mod>:<name>\" for an "
     "item a content mod adds. It is added to the bag (up to 99) and its name shows in the win message. 0 is not an item.",
     0, 0, itemExtByName, itemExtIdOk},
};

static void prizeLabel(int row, char *buf, size_t cap) {
    const char *n = sItemName != NULL ? sItemName(CARDGAME_prizeItems[row]) : NULL;

    if (n != NULL) {
        snprintf(buf, cap, "%s", n);
    }
}

static const TmTable kPrizeTable = {
    "prize_items", CARDGAME_prizeItems, sizeof(s16), GT_PRIZES, 0,
    "The prize number of an opponent, 0-35 (CardOpponent.prize, in CARD_NPC.BIN). Opponents can share a prize, so a change here changes all of them. "
    "\"labels\" gives each prize's item name.",
    "Set on every entry to the card battle (after the battle's data is restored); the prize is read when the opponent is loaded.",
    kPrizeFields, 1, 0, 0, TM_S32, NULL, NULL, NULL, NULL, NULL, prizeLabel};

const TmTable *gameTablePrizeItems(void) {
    return &kPrizeTable;
}

/* default_deck: card id minus one, as the game stores it */
static const TmField kDeckFields[] = {
    {"card", TM_U16, 0, 0, GT_CARD_ID_MAX - 1,
     "The card id MINUS ONE (the game adds one when it loads the card: 0 is card 1; 0-313 are the cards 1-314). The deck a player gets in a battle "
     "when the chosen deck slot is empty. \"<mod>:<name>\" names a card a content mod adds (the port subtracts the one). \"labels\" gives each "
     "position's card name.",
     0, 0, cardNameMinusOne, cardIdMinusOneOk},
};

static void deckLabel(int row, char *buf, size_t cap) {
    cardLabel(CARDGAME_defaultDeck[row] + 1, buf, cap);
}

static const TmTable kDeckTable = {
    "default_deck", CARDGAME_defaultDeck, sizeof(u16), GT_DECK_CARDS, 0,
    "The position in the deck, 0-39 (the array holds 80 values; the game reads the first 40 and nothing reads the rest).",
    "Set on every entry to the card battle (after the battle's data is restored); it is read when the battle chooses the player's deck.",
    kDeckFields, 1, 0, 0, TM_S32, NULL, NULL, NULL, NULL, NULL, deckLabel};

const TmTable *gameTableDefaultDeck(void) {
    return &kDeckTable;
}

static const TmTable *cardGameTable(int i) {
    switch (i) {
    case 0: return gameTableCardEffects();
    case 1: return gameTablePrizeItems();
    default: return gameTableDefaultDeck();
    }
}

#define GT_CARDGAME_TABLES 3
static int sCardGameLogged[GT_CARDGAME_TABLES];

int gameTablesRegisterCardGame(void) {
    int i, n = 0;

    for (i = 0; i < GT_CARDGAME_TABLES; i++) {
        sCardGameLogged[i] = 0;
        n += tmRegister(cardGameTable(i)) == 0;
    }
    return n;
}

void gameTablesApplyCardGame(void) {
    int i;

    for (i = 0; i < GT_CARDGAME_TABLES; i++) {
        const TmTable *t = cardGameTable(i);
        int changed = tmApply(t->name);

        if (changed > 0 && !sCardGameLogged[i]) {
            sCardGameLogged[i] = 1;
            logChanged(t, i == 0 ? "card" : i == 1 ? "prize" : "position");
        }
    }
}

/* ---- content-export E5d2 / modding-ux U8a: card_opponents, the card battle's opponents (CARD_NPC.BIN) ----
   A disc file of 170 rows of 0xD0 bytes with no key and no header (35360 bytes: the file's last sector is padded, so the count is fixed). The card battle
   reads one row per duel (CARDGAME_loadOpponent: FILE_CACHE.load(FILE_CARDGAME_OPPONENTS), row arg - 1), so the table is patched in the buffer each time the
   file is read (port_fs post-read hook), not in a scope hook. A row is 40 deck entries (card word, group, kind), 20 counter cards, a level and a prize. */

#define GT_OPPONENTS 170
#define GT_OPP_DECK 40
#define GT_OPP_COUNTERS 20
#define GT_OPP_FIELDS (GT_OPP_DECK * 4 + GT_OPP_COUNTERS + 2)
#define GT_OPP_COUNTER_SLOTS 13 /* the first 0 of the counter list must be among these: the game then writes up to 14 bytes past it into a 27-byte list */

_Static_assert(sizeof(GameCardOpponent) == 0xD0, "CardOpponent is 0xD0 bytes");

static TmField sOppFields[GT_OPP_FIELDS];
static char sOppNames[GT_OPP_FIELDS][16];

static void oppField(int *n, const char *fmt, int idx, TmType type, size_t off, long long min, long long max, const char *note, unsigned long long mask) {
    TmField *f = &sOppFields[*n];

    memset(f, 0, sizeof *f);
    snprintf(sOppNames[*n], sizeof sOppNames[*n], fmt, idx);
    f->name = sOppNames[*n];
    f->type = type;
    f->off = off;
    f->min = min;
    f->max = max;
    f->note = note;
    f->countOff = TM_NO_COUNT;
    f->mask = mask;
    (*n)++;
}

static void buildOppFields(void) {
    int i, n = 0;

    for (i = 0; i < GT_OPP_DECK; i++) {
        size_t base = offsetof(GameCardOpponent, cards) + (size_t)i * 4;

        oppField(&n, "card%d", i + 1, TM_U16, base, 1, GT_CARD_ID_MAX,
                 "Card id (1-314) of this place of the opponent's 40-card deck (the low 12 bits of the file's card word), or \"<mod>:<name>\" for a card a "
                 "content mod adds. The card names are the CARDNM text entries (entry = card id).", 0x0FFF);
        sOppFields[n - 1].byName = cardExtByName;
        sOppFields[n - 1].idOk = cardExtIdOk;
        oppField(&n, "flagged%d", i + 1, TM_U16, base, 0, 1,
                 "The flag bit (bit 15) of this deck place's card word, read by the computer when it picks an answer (CardPlan.flagged, computer.c). The disc "
                 "sets it on 376 of the 6800 places; 0 leaves the card normal.", 0x8000);
        oppField(&n, "group%d", i + 1, TM_U8, base + 2, 1, 7,
                 "When the computer may draw this place's card: once round * 2 + 2 reaches the group (rounds count from 0), and 7 is the reserve held back "
                 "(CardDraw.group, CARDGAME_findOpponentDeckLimits). 1-7, as the disc uses; a deck needs a card outside group 7.", 0);
        oppField(&n, "kind%d", i + 1, TM_U8, base + 3, 0, 7,
                 "How the computer plays this place's card (CardPlan.kind, 1-7): the hand is sorted by kind, then by a priority the kind sets plus the place "
                 "(CARDGAME_loadOpponent, CARDGAME_sortOpponentHand). One card of the disc uses 0, which sets no priority.", 0);
    }
    for (i = 0; i < GT_OPP_COUNTERS; i++) {
        oppField(&n, "counter%d", i + 1, TM_S16, offsetof(GameCardOpponent, counterCards) + (size_t)i * 2, 0, GT_CARD_ID_MAX,
                 "A card id (1-314, or \"<mod>:<name>\" for a card a content mod adds) the computer answers with a kind 4 card (the list runs from counter1 up "
                 "to the first 0, which must be among the first 13; an empty list, a 0 first, makes the game use its own standard answers).", 0);
        sOppFields[n - 1].byName = cardExtByName;
        sOppFields[n - 1].idOk = cardIdOrZeroOk;
    }
    oppField(&n, "level", 0, TM_U8, offsetof(GameCardOpponent, level), 1, 99,
             "The level shown after \"LV\" in the battle (the disc's opponents use 4-92); it is only shown, nothing reads it for the play.", 0);
    oppField(&n, "prize", 0, TM_S32, offsetof(GameCardOpponent, prize), 0, GT_PRIZES - 1,
             "The prize number (0-35): the item the player wins is prize_items[prize] (several opponents may share one).", 0);
}

/* the rows the game cannot handle: a counter list with no 0 in its first 13 places (CARDGAME_loadOpponent copies fourteen fixed answers after the 0 into a
   27-byte list, so a 0 later than the 13th writes past it), and a deck of only group 7 (the reserve start is then 0 and the return of the hand writes
   to deck place -1) */
static const char *oppRowCheck(const void *row) {
    const GameCardOpponent *o = (const GameCardOpponent *)row;
    int i, zero = 0, notReserve = 0;

    for (i = 0; i < GT_OPP_COUNTER_SLOTS; i++) {
        if (o->counterCards[i] == 0) {
            zero = 1;
            break;
        }
    }
    if (!zero) {
        return "its counter list has no 0 among the first 13 places (the game would write past its list of answers)";
    }
    for (i = 0; i < GT_OPP_DECK; i++) {
        notReserve |= o->cards[i].group != 7;
    }
    if (!notReserve) {
        return "all its deck is in group 7 (the reserve): the game would write before the start of the deck";
    }
    return NULL;
}

static TmTable sOpponentTable = {
    "card_opponents", NULL, sizeof(GameCardOpponent), GT_OPPONENTS, 0,
    "The card game mode's argument minus one, 0-169 (CardBattle.arg counts from 1 and the battle reads row arg - 1 of CARD_NPC.BIN; the field's "
    "CARD_BATTLE(opponent, kind) actions give the argument, so one NPC can use more than one row). "
    "The file has no names for its opponents, so there are no \"labels\". card/flagged/group/kind 1-40 are the deck's places, counter1-20 the answers.",
    "CARD_NPC.BIN is read when a card battle starts (CARDGAME_loadOpponent), so a change shows in the next duel with that opponent.",
    sOppFields, GT_OPP_FIELDS, 0, 0, TM_S32, NULL, NULL, NULL, NULL, oppRowCheck, NULL};

int gameTablesRegisterCardOpponents(const void *file, size_t len) {
    buildOppFields();
    return tmRegisterFile(&sOpponentTable, file, len);
}

int gameTablesPatchCardOpponents(void *buf, size_t len) {
    if (tmPatchCount("card_opponents") == 0) {
        return 0;
    }
    return tmPatchBuffer("card_opponents", buf, len);
}

/* ---- content-export E5e: the gym (STGTRAIN.PRO's tables) and the inns ---- */

#define GT_GYM_INDEXES 14      /* the rows of STGTRAIN_gymTrainings (the game reads the row of its index 1-14; index 0 and over 14 read row 0) */
#define GT_GYM_SLOTS 16        /* the entries of a gym's row: the menu goes through all 16 */
#define GT_GYM_ROWS (GT_GYM_INDEXES * GT_GYM_SLOTS)
#define GT_TRAININGS 24        /* the training ids 1-24: STGTRAIN_trainings has 25 entries (0 is none) and the menu places those ids only */
#define GT_INNS 21             /* INNS of the EU build, without its {0, 1, 1} end row */
#define GT_INN_STRINGS 22      /* the entries of HTLNAM (the same 22 in every language of the disc): the inn's name is read by its index */
/* The training's gain is added to a stat held in an s16 (a battle stat or resistance, at most 999 before the add, and the max HP or MP, at most 9999);
   the game clamps after the add, so a gain past the s16 would wrap first. A gain is base + a random 0..range-1 (range 0 is a fixed base). */
#define GT_STAT_GAIN_MAX (32767 - 999)
#define GT_HPMP_GAIN_MAX (32767 - 9999)
#define GT_STAT_LOSS_MAX 32767 /* taken off a stat of 0-999 (an s16) that the game then clamps at 0 */
#define GT_INN_PRICE_MAX 32767 /* an s16; the game pays price * party size (up to 3) in an s32 */

extern GameTrainEntry STGTRAIN_gymTrainings[GT_GYM_INDEXES][GT_GYM_SLOTS];
extern GameTrainInfo STGTRAIN_trainings[GT_TRAININGS + 1];
extern GameTrainGain STGTRAIN_statGains[12];
extern GameTrainGain STGTRAIN_statLosses[3];
extern GameTrainGain STGTRAIN_resistGainsLow[9];
extern GameTrainGain STGTRAIN_resistGainsMid[9];
extern GameTrainGain STGTRAIN_resistGainsHigh[9];
extern GameTrainGain STGTRAIN_maxHpMpGains[9];
extern s32 STGTRAIN_intensityCosts[3];
extern GameInn INNS[GT_INNS + 1];

static const char *(*sTrainingName)(int id);
static const char *(*sInnName)(int id);

void gameTablesSetTrainingNames(const char *(*nameOf)(int id)) {
    sTrainingName = nameOf;
}

void gameTablesSetInnNames(const char *(*nameOf)(int id)) {
    sInnName = nameOf;
}

/* gym_trainings: the key is the gym index and the slot; the game finds a training of a gym by its id (the first entry with it) */
static const TmField kGymTrainingFields[] = {
    {"id", TM_S32, offsetof(GameTrainEntry, id), 0, GT_TRAININGS,
     "The training in this slot: 1-24 (the entries of the trainings list: 1-5 and 13-17 raise the five battle stats, 6-12 and 18-24 the resistances), or 0 for "
     "an empty slot. The menu puts the training in its own place whatever the slot (1 and 13 in the first, 2 and 14 in the second...), so two entries of one "
     "gym with ids of the same place leave the last one on the menu; the gym's trainings per page count every entry with an id."},
    {"stat", TM_S16, offsetof(GameTrainEntry, stat), 0, 14,
     "The stat the training raises: 1-5 a battle stat (strength, defense, spirit, wisdom, speed), 8-14 a resistance. 0 is only for an empty slot (id 0); "
     "6 and 7 are no stat. A resistance training also lowers or raises the stat of `other`. The results text names the stat by this number."},
    {"other", TM_S16, offsetof(GameTrainEntry, other), 0, 16,
     "The second stat of a resistance training (stat 8-14; it is not read for stat 1-5): 0 none, 1-5 a battle stat that it may lower (half of the tries), 15 the "
     "maximum HP or 16 the maximum MP, which it raises. Any other number is refused."},
};

static const char *gymTrainingCheck(const void *row) {
    const GameTrainEntry *e = (const GameTrainEntry *)row;

    if (e->id == 0) {
        return NULL; /* an empty slot is never chosen, whatever else it holds */
    }
    if (e->stat == 0 || (e->stat > 5 && e->stat < 8)) {
        return "its stat is not a battle stat (1-5) or a resistance (8-14): the results screen reads totals the training never sets";
    }
    if (e->stat >= 8 && e->other != 0 && !(e->other <= 5 || e->other == 15 || e->other == 16)) {
        return "its other stat is not 1-5, 15 or 16 (the game names it with the text of that number)";
    }
    return NULL;
}

static void gymTrainingLabel(int row, char *buf, size_t cap) {
    int id = row >= 0 && row < GT_GYM_ROWS ? STGTRAIN_gymTrainings[row / GT_GYM_SLOTS][row % GT_GYM_SLOTS].id : 0;
    const char *n = id > 0 && id <= GT_TRAININGS && sTrainingName != NULL ? sTrainingName(STGTRAIN_trainings[id].name) : NULL;

    if (n != NULL) {
        snprintf(buf, cap, "%s", n);
    }
}

static long sGymTrainingKeys[GT_GYM_ROWS];

static const TmTable kGymTrainingTable = {
    "gym_trainings", STGTRAIN_gymTrainings, sizeof(GameTrainEntry), GT_GYM_ROWS, 0,
    "The gym index times 100 plus the slot (0-15): 305 is the sixth entry of the gym with index 3. The gym reads the row of its index (the game mode's argument, "
    "1-14; an index of 0 or over 14 reads row 0, and 14 would read past the table; the disc fills rows 0-13). \"labels\" gives the training's name.",
    "Set on every entry to the gym (the gym's data is restored each time and the patch goes on after it); the gym reads its row when the training menu opens.",
    kGymTrainingFields, 3, 0, 0, TM_S32, NULL, NULL, sGymTrainingKeys, NULL, gymTrainingCheck, gymTrainingLabel};

const TmTable *gameTableGymTrainings(void) {
    int r;

    for (r = 0; r < GT_GYM_ROWS; r++) {
        sGymTrainingKeys[r] = (long)(r / GT_GYM_SLOTS) * 100 + r % GT_GYM_SLOTS;
    }
    return &kGymTrainingTable;
}

/* the gains and losses: base and range, with a check that the largest result still fits the stat's s16 */
#define GAIN_RANGE_NOTE \
    "The result is base plus a random number from 0 to range - 1 (the game rolls it for every try); a range of 0 gives base alone."

static const char *gymGainCheck(const void *row, long long max) {
    const GameTrainGain *g = (const GameTrainGain *)row;
    long long top = g->range > 0 ? (long long)g->base + g->range - 1 : g->base;

    return top > max ? "the largest result (base + range - 1) would overflow the stat the game stores it in" : NULL;
}

static const char *gymStatGainCheck(const void *row) {
    return gymGainCheck(row, GT_STAT_GAIN_MAX);
}

static const char *gymHpMpGainCheck(const void *row) {
    return gymGainCheck(row, GT_HPMP_GAIN_MAX);
}

static const char *gymStatLossCheck(const void *row) {
    return gymGainCheck(row, GT_STAT_LOSS_MAX);
}

static const TmField kGymStatGainFields[] = {
    {"base", TM_S32, offsetof(GameTrainGain, base), 0, GT_STAT_GAIN_MAX,
     "The fixed part of what the training adds to a battle stat. The stat stops at 999; base + range - 1 must stay within 31768 (the s16 the stat is kept in)."},
    {"range", TM_S32, offsetof(GameTrainGain, range), 0, GT_STAT_GAIN_MAX + 1, GAIN_RANGE_NOTE},
};

static const TmTable kGymStatGainTable = {
    "gym_stat_gains", STGTRAIN_statGains, sizeof(GameTrainGain), 12, 0,
    "The column, 0-11: kind * 6 + bonus * 3 + intensity. Kind 0 is the trainings 1-12 and kind 1 the trainings 13-24; bonus is 1 when the bonus try worked "
    "(0 otherwise); the intensity is 0-2 (the three the player chooses). Only the trainings that raise a battle stat (stat 1-5) read this table.",
    "Set on every entry to the gym (after the gym's data is restored); read when a training's result is worked out.",
    kGymStatGainFields, 2, 0, 0, TM_S32, NULL, NULL, NULL, NULL, gymStatGainCheck};

const TmTable *gameTableGymStatGains(void) {
    return &kGymStatGainTable;
}

static const TmField kGymStatLossFields[] = {
    {"base", TM_S32, offsetof(GameTrainGain, base), 0, GT_STAT_LOSS_MAX,
     "The fixed part of what a resistance training may take off a battle stat (its `other`, 1-5; it happens in half of the tries). The stat stops at 0."},
    {"range", TM_S32, offsetof(GameTrainGain, range), 0, GT_STAT_LOSS_MAX + 1, GAIN_RANGE_NOTE},
};

static const TmTable kGymStatLossTable = {
    "gym_stat_losses", STGTRAIN_statLosses, sizeof(GameTrainGain), 3, 0,
    "The intensity, 0-2 (the one the player chose).",
    "Set on every entry to the gym (after the gym's data is restored); read when a training's result is worked out.",
    kGymStatLossFields, 2, 0, 0, TM_S32, NULL, NULL, NULL, NULL, gymStatLossCheck};

const TmTable *gameTableGymStatLosses(void) {
    return &kGymStatLossTable;
}

/* gym_resist_gains: three arrays of the overlay (not contiguous in the host), so the rows go through rowAt */
static GameTrainGain *const kResistGainArrays[3] = {STGTRAIN_resistGainsLow, STGTRAIN_resistGainsMid, STGTRAIN_resistGainsHigh};
static const long kResistGainKeys[27] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 25, 26, 27, 28};

static void *resistGainAt(int row) {
    return row >= 0 && row < 27 ? (void *)&kResistGainArrays[row / 9][row % 9] : NULL;
}

static const TmField kGymResistGainFields[] = {
    {"base", TM_S32, offsetof(GameTrainGain, base), 0, GT_STAT_GAIN_MAX,
     "The fixed part of what the training adds to a resistance. The resistance stops at 999; base + range - 1 must stay within 31768 (the s16 it is kept in)."},
    {"range", TM_S32, offsetof(GameTrainGain, range), 0, GT_STAT_GAIN_MAX + 1, GAIN_RANGE_NOTE},
};

static const TmTable kGymResistGainTable = {
    "gym_resist_gains", NULL, sizeof(GameTrainGain), 27, 0,
    "The table times 10 plus the slot (0-8): 0-8 the low table, 10-18 the middle one, 20-28 the high one. The game picks the table from the partner's growth of that "
    "resistance (1-5) and how high the resistance is already (under 100, under 300, more), and the slot is intensity * 3 + tier, where the tier is 0 without the "
    "bonus try, 1 with it for the trainings 1-12 and 2 with it for the trainings 13-24. Several growths share a table, so a change reaches all of them.",
    "Set on every entry to the gym (after the gym's data is restored); read when a training's result is worked out.",
    kGymResistGainFields, 2, 0, 0, TM_S32, NULL, NULL, kResistGainKeys, resistGainAt, gymStatGainCheck};

const TmTable *gameTableGymResistGains(void) {
    return &kGymResistGainTable;
}

static const TmField kGymHpMpGainFields[] = {
    {"base", TM_S32, offsetof(GameTrainGain, base), 0, GT_HPMP_GAIN_MAX,
     "The fixed part of what the training adds to the maximum HP or MP (the stat of `other`, 15 or 16). The maximum stops at 9999; base + range - 1 must stay "
     "within 22768 (the s16 it is kept in)."},
    {"range", TM_S32, offsetof(GameTrainGain, range), 0, GT_HPMP_GAIN_MAX + 1, GAIN_RANGE_NOTE},
};

static const TmTable kGymHpMpGainTable = {
    "gym_hp_mp_gains", STGTRAIN_maxHpMpGains, sizeof(GameTrainGain), 9, 0,
    "The slot, 0-8: intensity * 3 + tier, where the tier is 0 without the bonus try, 1 with it for the trainings 1-12 and 2 with it for the trainings 13-24.",
    "Set on every entry to the gym (after the gym's data is restored); read when a training's result is worked out.",
    kGymHpMpGainFields, 2, 0, 0, TM_S32, NULL, NULL, NULL, NULL, gymHpMpGainCheck};

const TmTable *gameTableGymHpMpGains(void) {
    return &kGymHpMpGainTable;
}

static const TmField kGymCostFields[] = {
    {"cost", TM_S32, 0, 0, 32767,
     "The training points (TP of the partner's stats) the intensity costs, paid when the player confirms a training. The gym refuses an intensity the partner "
     "cannot pay (the TP total has to be at least the cost); 0 is free. 32767 is the s16 the TP is kept in."},
};

static const TmTable kGymCostTable = {
    "gym_intensity_costs", STGTRAIN_intensityCosts, sizeof(s32), 3, 0,
    "The intensity, 0-2 (the three the player chooses in the gym).",
    "Set on every entry to the gym (after the gym's data is restored); read when the intensity is chosen and when it is paid.",
    kGymCostFields, 1};

const TmTable *gameTableGymIntensityCosts(void) {
    return &kGymCostTable;
}

static const TmTable *gymTable(int i) {
    switch (i) {
    case 0: return gameTableGymTrainings();
    case 1: return gameTableGymStatGains();
    case 2: return gameTableGymStatLosses();
    case 3: return gameTableGymResistGains();
    case 4: return gameTableGymHpMpGains();
    default: return gameTableGymIntensityCosts();
    }
}

#define GT_GYM_TABLES 6
static int sGymLogged[GT_GYM_TABLES];

int gameTablesRegisterGym(void) {
    int i, n = 0;

    for (i = 0; i < GT_GYM_TABLES; i++) {
        sGymLogged[i] = 0;
        n += tmRegister(gymTable(i)) == 0;
    }
    return n;
}

void gameTablesApplyGym(void) {
    int i;

    for (i = 0; i < GT_GYM_TABLES; i++) {
        const TmTable *t = gymTable(i);
        int changed = tmApply(t->name);

        if (changed > 0 && !sGymLogged[i]) {
            sGymLogged[i] = 1;
            logChanged(t, i == 0 ? "entry" : i == 3 ? "slot" : i == 5 || i == 2 ? "intensity" : "column");
        }
    }
}

/* inns: the key is the inn's game mode, so the row's own `mode` is not a field */
static const TmField kInnFields[] = {
    {"price", TM_S16, offsetof(GameInn, price), 0, GT_INN_PRICE_MAX,
     "What one night costs per party member (the inn asks price x the party's size, and takes it if the money covers it; 0 is free). 32767 is the s16 it is kept in."},
    {"string", TM_S16, offsetof(GameInn, string), 0, GT_INN_STRINGS - 1,
     "The entry of the inn text file (HTLNAM) the inn shows as its name: 0-21, the 22 entries of the file (1-15 and 21 are the inn names the disc uses; 0 is empty "
     "and 16-20 are the inn's own texts: \"BIT\", the price question, the two answers and the not-enough-money message, which would show as the name)."},
};

static void innLabel(int row, char *buf, size_t cap) {
    const char *n = row >= 0 && row < GT_INNS && sInnName != NULL ? sInnName(INNS[row].string) : NULL;

    if (n != NULL) {
        snprintf(buf, cap, "%s", n);
    }
}

static const TmTable kInnTable = {
    "inns", INNS, sizeof(GameInn), GT_INNS, 0,
    "The game mode of the inn (the field area it stands in), in decimal: 522 is 0x20A, the first one. The inn opens when the field enters that mode. "
    "\"labels\" gives each inn's name.",
    "Set once when the port starts (the executable's data is never reset); the inn reads its row when it opens and when the night is paid.",
    kInnFields, 2, 1, offsetof(GameInn, mode), TM_S32, NULL, NULL, NULL, NULL, NULL, innLabel};

const TmTable *gameTableInns(void) {
    return &kInnTable;
}

int gameTablesRegisterInns(void) {
    if (tmRegister(&kInnTable) != 0) {
        return 0;
    }
    tmApply("inns");
    logChanged(&kInnTable, "mode");
    return 1;
}

/* ---- port-close S3: the event system's condition tables (src/engine/game/events.c), see game_tables.h ---- */

#define GT_MONEY_REQUIRED 10
#define GT_MONEY_GAINS 8
#define GT_MONEY_LOSSES 10
#define GT_PROGRESS_RANGES 34
#define GT_PARTY_THRESHOLDS 15
#define GT_SPECIAL_EXT_FIRST 0x100
#define GT_SPECIAL_EXT_MAX 256 /* the id is the low 9 bits of a code: 0x100-0x1FF are free (the game's ids stop below 0xFF) */
#define GT_SPECIAL_PRIMITIVES 6
#define GT_SPECIAL_INVALID_CHECK 0xFF /* a new row before a mod gives it a primitive and an op: primitive 15 is none */

/* Why a (check, arg) pair is not a row the game can run, or NULL. Only what keeps the game inside its arrays (and the known primitives and
   ops): the original rows all pass, quirks included (a flag-count row of mode 3 never holds). */
static const char *specialProblem(int check, int arg) {
    int primitive = (check >> 4) & 0xF, op = check & 0xF;

    switch (primitive) {
    case 0:
        return op > 1 ? "an item set row's op is 0 or 1 (the two sets of the European version)" : NULL;
    case 1:
        if (op > 9) {
            return "a partner row's op is 0-9";
        }
        if ((op == 1 || op == 3 || op == 4 || op == 6) && arg >= PARTNER_COUNT) {
            return "this partner op reads partner `arg`, which must be 0-7";
        }
        return NULL;
    case 2:
        if (op > 2) {
            return "a money row's op is 0 (has), 1 (give) or 2 (take)";
        }
        if (arg >= (op == 0 ? GT_MONEY_REQUIRED : op == 1 ? GT_MONEY_GAINS : GT_MONEY_LOSSES)) {
            return "this money op reads the amount at index `arg` (has 0-9, give 0-7, take 0-9)";
        }
        return NULL;
    case 3:
        return arg >= GT_PROGRESS_RANGES ? "a progress range row's `arg` is the range index, 0-33" : NULL;
    case 4:
        return op > 1 ? "a flag count row's op is 0 or 1" : NULL;
    case 5:
        return NULL;
    }
    return "its primitive is not one of 0-5 (a new row must give one)";
}

static const char *specialRowCheck(const void *row) {
    const SpecialCondition *c = (const SpecialCondition *)row;

    return specialProblem(c->check, c->arg);
}

static void specialLabel(int row, char *buf, size_t cap) {
    static const char *const names[GT_SPECIAL_PRIMITIVES] = {"item set", "partner", "money", "progress range", "flag count", "actor icon"};
    const SpecialCondition *c = &SPECIAL_CONDITIONS[row];

    if ((c->check >> 4) < GT_SPECIAL_PRIMITIVES) {
        snprintf(buf, cap, "%s, op %d, arg %d", names[c->check >> 4], c->check & 0xF, c->arg);
    }
}

/* the rows a mod adds: the same layout as SPECIAL_CONDITIONS' (the table engine patches them through extRow) */
static SpecialCondition sSpecialExt[GT_SPECIAL_EXT_MAX];
static char *sSpecialExtName[GT_SPECIAL_EXT_MAX];
static int sSpecialExtCount;

static void specialExtReset(void) {
    int i;

    for (i = 0; i < sSpecialExtCount; i++) {
        free(sSpecialExtName[i]);
        sSpecialExtName[i] = NULL;
    }
    sSpecialExtCount = 0;
}

/* "<mod>:<name>": the mod's folder (letters, digits, . _ -), a colon and a lowercase name (a-z 0-9 _ - /), at most 120 bytes */
static int specialNameOk(const char *name) {
    const char *colon = strchr(name, ':'), *p;

    if (strlen(name) > 120 || colon == NULL || colon == name || colon[1] == '\0' || strchr(colon + 1, ':') != NULL) {
        return 0;
    }
    for (p = name; p < colon; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) {
            return 0;
        }
    }
    for (p = colon + 1; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '/')) {
            return 0;
        }
    }
    return 1;
}

/* A row keyed by a name in a patch defines (or finds) the mod's row: the table engine asks while it reads the patches */
static long long specialExtByName(const char *name) {
    char *copy;
    int i;

    if (!specialNameOk(name)) {
        return -1;
    }
    for (i = 0; i < sSpecialExtCount; i++) {
        if (strcmp(sSpecialExtName[i], name) == 0) {
            return GT_SPECIAL_EXT_FIRST + i;
        }
    }
    if (sSpecialExtCount >= GT_SPECIAL_EXT_MAX || (copy = (char *)malloc(strlen(name) + 1)) == NULL) {
        return -1;
    }
    strcpy(copy, name);
    sSpecialExtName[sSpecialExtCount] = copy;
    sSpecialExt[sSpecialExtCount].id = 0;
    sSpecialExt[sSpecialExtCount].check = GT_SPECIAL_INVALID_CHECK;
    sSpecialExt[sSpecialExtCount].arg = 0;
    return GT_SPECIAL_EXT_FIRST + sSpecialExtCount++;
}

static void *specialExtRow(long long id) {
    return id >= GT_SPECIAL_EXT_FIRST && id < GT_SPECIAL_EXT_FIRST + sSpecialExtCount ? (void *)&sSpecialExt[id - GT_SPECIAL_EXT_FIRST] : NULL;
}

int pcSpecialExtRow(int id, unsigned char *check, unsigned char *arg) {
    const SpecialCondition *c = (const SpecialCondition *)specialExtRow(id);

    if (c == NULL || specialProblem(c->check, c->arg) != NULL) {
        return 0;
    }
    *check = c->check;
    *arg = c->arg;
    return 1;
}

int gameSpecialIdOf(const char *name) {
    int i;

    for (i = 0; name != NULL && i < sSpecialExtCount; i++) {
        if (strcmp(sSpecialExtName[i], name) == 0) {
            return specialProblem(sSpecialExt[i].check, sSpecialExt[i].arg) == NULL ? GT_SPECIAL_EXT_FIRST + i : -1;
        }
    }
    return -1;
}

const char *gameSpecialNameOf(int id) {
    return id >= GT_SPECIAL_EXT_FIRST && id < GT_SPECIAL_EXT_FIRST + sSpecialExtCount ? sSpecialExtName[id - GT_SPECIAL_EXT_FIRST] : NULL;
}

int gameSpecialRow(int id, int *check, int *arg) {
    const SpecialCondition *c;

    if (id >= GT_SPECIAL_EXT_FIRST) {
        unsigned char k, a;

        if (!pcSpecialExtRow(id, &k, &a)) {
            return 0;
        }
        *check = k;
        *arg = a;
        return 1;
    }
    for (c = SPECIAL_CONDITIONS; c->id != 0xFF; c++) {
        if (c->id == id) {
            *check = c->check;
            *arg = c->arg;
            return 1;
        }
    }
    return 0;
}

static const TmField kSpecialFields[] = {
    {"primitive", TM_U8, offsetof(SpecialCondition, check), 0, 5,
     "What the row asks or does: 0 an item set, 1 a partner, 2 money, 3 a story progress range, 4 a flag count, 5 the actor icon (high nibble of the check byte).",
     0, 0, NULL, NULL, 0xF0},
    {"op", TM_U8, offsetof(SpecialCondition, check), 0, 15,
     "Which of the primitive's tests or actions (low nibble of the check byte). Item set: 0 or 1 (the two sets). Partner: 0 party set is `arg`, 1 `arg` is unlocked, "
     "2 `arg` is in the party, 3 UNLOCK `arg` (an action), 4 `arg` is unlocked at level 45, 5 the party's levels reach `arg` * 15 + 30, 6 `arg` is locked, 7 every "
     "unlocked partner is level 45, 8 heal the party (an action), 9 all eight are unlocked. Money: 0 the player has the amount at index `arg` of money_required, "
     "1 GIVE money_gains[`arg`], 2 TAKE money_losses[`arg`]. Progress range: ignored. Flag count: 0 the flags of group 1C counted by mode `arg` (0 any is set, 1 two "
     "or more are clear, 2 exactly one is clear), 1 four flags of group 10 are all set. Actor icon: ignored.",
     0, 0, NULL, NULL, 0x0F},
    {"arg", TM_U8, offsetof(SpecialCondition, arg), 0, 255,
     "The index or number the op reads (see `op`): a partner, an index of money_required, money_gains or money_losses, a progress_ranges row, a mode."},
};

static TmTable sSpecialTable = {
    "special_conditions", SPECIAL_CONDITIONS, sizeof(SpecialCondition), 0, 0,
    "The row id the `special` code names (0-254, in decimal; 255 ends the array and is no row), or \"<mod>:<name>\" for a condition a mod adds: its first "
    "patch row with that name creates it (ids 0x100 and up, up to 256) and it needs `primitive`, `op` and, where the op reads it, `arg`. \"labels\" says what each original row does.",
    "Set once when the port starts (the executable's data is never reset); the events read a row when a talk's or an event's code names it.",
    kSpecialFields, 3, 1, offsetof(SpecialCondition, id), TM_U8, specialExtByName, specialExtRow, NULL, NULL, specialRowCheck, specialLabel};

const TmTable *gameTableSpecialConditions(void) {
    const SpecialCondition *c;
    int n = 0;

    for (c = SPECIAL_CONDITIONS; c->id != 0xFF; c++) {
        n++;
    }
    sSpecialTable.rows = n;
    return &sSpecialTable;
}

static const TmField kMoneyRequiredFields[] = {
    {"amount", TM_S32, 0, 0, MONEY_MAX, "The money the player must have for this `money` row's \"has\" test to hold (op 0)."},
};
static const TmField kMoneyGainFields[] = {
    {"amount", TM_S32, 0, 0, MONEY_MAX, "The money a `money` row's \"give\" action (op 1) adds; the total stops at 9999999."},
};
static const TmField kMoneyLossFields[] = {
    {"amount", TM_S32, 0, 0, MONEY_MAX, "The money a `money` row's \"take\" action (op 2) removes; the total stops at 0."},
};

static const TmTable kMoneyRequiredTable = {
    "money_required", MONEY_REQUIRED, sizeof(s32), GT_MONEY_REQUIRED, 0,
    "The index, 0-9: the `arg` of a special_conditions row of primitive 2, op 0.",
    "Set once when the port starts; read each time a talk's or an event's code tests the row.", kMoneyRequiredFields, 1};
static const TmTable kMoneyGainsTable = {
    "money_gains", MONEY_GAINS, sizeof(s32), GT_MONEY_GAINS, 0,
    "The index, 0-7: the `arg` of a special_conditions row of primitive 2, op 1.",
    "Set once when the port starts; read each time a talk's or an event's action runs the row.", kMoneyGainFields, 1};
static const TmTable kMoneyLossesTable = {
    "money_losses", MONEY_LOSSES, sizeof(s32), GT_MONEY_LOSSES, 0,
    "The index, 0-9: the `arg` of a special_conditions row of primitive 2, op 2.",
    "Set once when the port starts; read each time a talk's or an event's action runs the row.", kMoneyLossFields, 1};

const TmTable *gameTableMoneyRequired(void) {
    return &kMoneyRequiredTable;
}

const TmTable *gameTableMoneyGains(void) {
    return &kMoneyGainsTable;
}

const TmTable *gameTableMoneyLosses(void) {
    return &kMoneyLossesTable;
}

static const char *progressRangeCheck(const void *row) {
    const u8 *r = (const u8 *)row;

    return r[0] > r[1] && !(r[0] == 0 && r[1] == 0) ? "its min is above its max, so the range never holds (use min 0 and max 0 for the unused row)" : NULL;
}

static const TmField kProgressRangeFields[] = {
    {"min", TM_U8, 0, 0, 255, "The lowest story progress (GAME.progress) inside the range; the story runs 0-99."},
    {"max", TM_U8, 1, 0, 255, "The highest story progress inside the range; not under `min`."},
};

static const TmTable kProgressRangeTable = {
    "progress_ranges", PROGRESS_RANGES, 2, GT_PROGRESS_RANGES, 0,
    "The index, 0-33: the `arg` of a special_conditions row of primitive 3. The last row (33) is the disc's unused 0-0.",
    "Set once when the port starts; read each time a talk's or an event's code tests the row.", kProgressRangeFields, 2, 0, 0, TM_S32, NULL, NULL, NULL, NULL, progressRangeCheck};

const TmTable *gameTableProgressRanges(void) {
    return &kProgressRangeTable;
}

static const TmField kPartyStatFields[] = {
    {"threshold", TM_S32, 0, 0, 99999,
     "The total of the party's battle stat 5 that a `[\"party-stat\", index, 1]` test needs (1 holds when the total reaches it, 0 when it stays under)."},
};

static const TmTable kPartyStatTable = {
    "party_stat_thresholds", PARTY_STAT_THRESHOLDS, sizeof(s32), GT_PARTY_THRESHOLDS, 0,
    "The index, 0-14: the number of the `party-stat` code.",
    "Set once when the port starts; read each time a talk's or an event's code tests it.", kPartyStatFields, 1};

const TmTable *gameTablePartyStatThresholds(void) {
    return &kPartyStatTable;
}

static const TmTable *eventsTable(int i) {
    switch (i) {
    case 0: return gameTableSpecialConditions();
    case 1: return gameTableMoneyRequired();
    case 2: return gameTableMoneyGains();
    case 3: return gameTableMoneyLosses();
    case 4: return gameTableProgressRanges();
    default: return gameTablePartyStatThresholds();
    }
}

#define GT_EVENT_TABLES 6

int gameTablesRegisterEvents(void) {
    int i, n = 0;

    specialExtReset(); /* the patches are read by tmRegister: the mods' names define their rows then */
    for (i = 0; i < GT_EVENT_TABLES; i++) {
        const TmTable *t = eventsTable(i);

        if (tmRegister(t) != 0) {
            continue;
        }
        n++;
        tmApply(t->name);
        logChanged(t, "id");
    }
    for (i = 0; i < sSpecialExtCount; i++) {
        const char *why = specialProblem(sSpecialExt[i].check, sSpecialExt[i].arg);

        if (why != NULL) {
            plogLine("tables", "special_conditions row %s rejected: %s\n", sSpecialExtName[i], why); /* the name unquoted: --validate-mod ties the line to its mod */
        } else {
            PLOG("tables", PLOG_MODS, "special_conditions: \"%s\" is condition 0x%X (primitive %d, op %d, arg %d)\n", sSpecialExtName[i], GT_SPECIAL_EXT_FIRST + i,
                 sSpecialExt[i].check >> 4, sSpecialExt[i].check & 0xF, sSpecialExt[i].arg);
        }
    }
    return n;
}
