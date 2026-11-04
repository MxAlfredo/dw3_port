/*
 * The port's extended game state (ADR-008): see ext_state.h.
 * The item counts (T1) and their dw3save-v1 chunk (T2).
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "content.h"
#include "engine/game_state.h"
#include "ext_state.h"
#include "port_log.h"

static s8 *sExtCount;    /* ids PC_ITEM_ORIG_COUNT .. sExtSize-1, index id - PC_ITEM_ORIG_COUNT */
static s8 *sExtEquipped;
static int sExtSize = PC_ITEM_ORIG_COUNT; /* ids covered, original included */
static s8 sScratch;
static int sWarned;

static void warnOnce(const char *what, int id) {
    if (!sWarned) {
        sWarned = 1;
        plogLine("state", "%s: item id %d is outside 0..%d; reads give 0, writes are dropped (logged once)\n", what,
                 id, PC_EXT_ITEM_LIMIT - 1);
    }
}

/* grows both stores to cover `id`, zero-filled; 0 when memory ran out */
static int grow(int id) {
    int want = sExtSize, extra;
    s8 *c, *e;

    if (want < 64 + PC_ITEM_ORIG_COUNT) want = 64 + PC_ITEM_ORIG_COUNT;
    while (want <= id) want *= 2;
    if (want > PC_EXT_ITEM_LIMIT) want = PC_EXT_ITEM_LIMIT;
    extra = want - PC_ITEM_ORIG_COUNT;
    c = realloc(sExtCount, (size_t)extra);
    if (c != NULL) sExtCount = c;
    e = realloc(sExtEquipped, (size_t)extra);
    if (e != NULL) sExtEquipped = e;
    if (c == NULL || e == NULL) return 0;
    memset(sExtCount + (sExtSize - PC_ITEM_ORIG_COUNT), 0, (size_t)(want - sExtSize));
    memset(sExtEquipped + (sExtSize - PC_ITEM_ORIG_COUNT), 0, (size_t)(want - sExtSize));
    sExtSize = want;
    return 1;
}

static s8 *slot(int id, s8 *orig, s8 **ext, const char *what) {
    if (id >= 0 && id < PC_ITEM_ORIG_COUNT) return &orig[id];
    if (id >= PC_ITEM_ORIG_COUNT && id < PC_EXT_ITEM_LIMIT) {
        if (id >= sExtSize && !grow(id)) goto scratch;
        return &(*ext)[id - PC_ITEM_ORIG_COUNT];
    }
scratch:
    warnOnce(what, id);
    sScratch = 0;
    return &sScratch;
}

/* ---- the cards content mods add (U8b) ---- */

#define CARD_EXT_SLOTS (PC_CARD_LIST_MAX - PC_CARD_ORIG_COUNT)
static s8 sCardCount[CARD_EXT_SLOTS];
static u8 sCardSeen[CARD_EXT_SLOTS];
static s8 sCardScratch;
static u8 sCardSeenScratch;
static int sCardWarned;
static int sCardExt;
static PcExtCardNameFn sCardNameOf;
static PcExtCardIdFn sCardIdOf;
static const int *sCardFallback;

static void cardWarn(int id) {
    if (!sCardWarned) {
        sCardWarned = 1;
        plogLine("state", "card id %d is outside 0..%d; reads give 0, writes are dropped (logged once)\n", id, PC_CARD_LIST_MAX - 1);
    }
}

s8 *pcCardCountRef(int id) {
    if (id >= 0 && id < PC_CARD_ORIG_COUNT) return &GAME.cardsCard[id];
    if (id >= PC_CARD_ORIG_COUNT && id < PC_CARD_LIST_MAX) return &sCardCount[id - PC_CARD_ORIG_COUNT];
    cardWarn(id);
    sCardScratch = 0;
    return &sCardScratch;
}

u8 *pcCardSeenRef(int id) {
    if (id >= 0 && id < PC_CARD_ORIG_COUNT) return &GAME.cardsSeenCard[id];
    if (id >= PC_CARD_ORIG_COUNT && id < PC_CARD_LIST_MAX) return &sCardSeen[id - PC_CARD_ORIG_COUNT];
    cardWarn(id);
    sCardSeenScratch = 0;
    return &sCardSeenScratch;
}

int pcCardTotal(void) {
    return sCardExt > 0 ? PC_CARD_ORIG_COUNT + sCardExt : 315;
}

void pcExtStateSetCardCount(int count) {
    sCardExt = count < 0 ? 0 : count > CARD_EXT_SLOTS ? CARD_EXT_SLOTS : count;
}

void pcExtStateSetCardHooks(PcExtCardNameFn nameOf, PcExtCardIdFn idOf) {
    sCardNameOf = nameOf;
    sCardIdOf = idOf;
}

void pcExtStateSetCardFallback(const int *deck) {
    sCardFallback = deck;
}

static const int *sItemBases;
static int sItemBaseCount;

void pcExtStateSetItemBases(const int *bases, int count) {
    sItemBases = bases;
    sItemBaseCount = bases != NULL ? count : 0;
}

/* An original id is its own base; a mod item follows its "based on" chain to an original one. Out of the store, or a
   chain that does not end (a cycle), gives the id itself. */
int pcItemBase(int id) {
    int cur = id;
    int hops;

    for (hops = 0; cur >= PC_ITEM_ORIG_COUNT; hops++) {
        int i = cur - PC_ITEM_ORIG_COUNT;
        if (i >= sItemBaseCount || hops >= 16) return id;
        cur = sItemBases[i];
    }
    return cur;
}

void pcItemScriptLog(int item, int effect, int sound) {
    PLOG("items", PLOG_MODS, "battle item %d (base %d): script effect 0x%X sound 0x%X\n", item, pcItemBase(item), effect, sound);
}

s8 *pcItemCountRef(int id) {
    return slot(id, GAME.itemsCard, &sExtCount, "items");
}

s8 *pcItemEquippedRef(int id) {
    return slot(id, GAME.equippedItemsCard, &sExtEquipped, "equippedItems");
}

/* ---- the technique table (extended techs T1, T2) ----
 * The original table, then the store of the techniques content mods add (set by shim/tech_ext.c), with a scratch
 * entry for any index outside both. */

static TechData *sTechRows;
static int sTechRowCount;
static TechData sTechScratch;
static int sTechWarned;

void pcExtStateSetTechStore(TechData *rows, int count) {
    sTechRows = rows;
    sTechRowCount = rows != NULL ? count : 0;
}

static const int *sTechBases;
static int sTechBaseCount;

void pcExtStateSetTechBases(const int *bases, int count) {
    sTechBases = bases;
    sTechBaseCount = bases != NULL ? count : 0;
}

/* The original healing techniques are ids 0xB8..0xBC; a mod technique counts when its base is one of them */
int pcTechIsFieldHeal(int id) {
    int hops;

    /* a mod technique may be based on another one: follow the chain (bounded: a cycle is not healing) */
    for (hops = 0; id >= PC_TECH_FIRST_EXT_ID; hops++) {
        int i = id - PC_TECH_FIRST_EXT_ID;
        if (i >= sTechBaseCount || hops >= 16) return 0;
        id = sTechBases[i];
    }
    return id >= 0xB8 && id < 0xBD;
}

TechData *pcTechRef(int index) {
    if (index >= 0 && index < PC_TECH_ORIG_COUNT) return &TECHS_CARD[index];
    if (index >= PC_TECH_ORIG_COUNT && index - PC_TECH_ORIG_COUNT < sTechRowCount) return &sTechRows[index - PC_TECH_ORIG_COUNT];
    if (!sTechWarned) {
        sTechWarned = 1;
        plogLine("state", "techs: index %d is outside 0..%d; reads give zeros, writes are dropped (logged once)\n",
                 index, PC_TECH_ORIG_COUNT + sTechRowCount - 1);
    }
    memset(&sTechScratch, 0, sizeof sTechScratch);
    return &sTechScratch;
}

/* ---- the Digimon table (extended Digimon P1) ----
 * Rows 0..PC_DIGIMON_ORIG_COUNT-1 are the original table; the rows above it are a growable store, zero-filled,
 * that pcDigimonExtendRows adds to (digimon_ext.c fills them with the new partner forms, P2). A row outside both gets a scratch row. */

static DigimonData *sDigRows;
static int *sDigNames;   /* per extension row: its DIGNAM entry + 1, 0 = the row's own nameId */
static PcDigiLinks *sDigLinks; /* per extension row (U9) */
static PcDigiLinks sOrigLinks[PC_DIGIMON_ORIG_COUNT];
static int sDigRowCount; /* extension rows only */
static DigimonData sDigScratch;
static int sDigWarned;

int pcDigimonRowCount(void) {
    return PC_DIGIMON_ORIG_COUNT + sDigRowCount;
}

int pcDigimonExtendRows(int count) {
    DigimonData *grown;
    int *names;
    int first = PC_DIGIMON_ORIG_COUNT + sDigRowCount;

    if (count <= 0) return first;
    if (count > PC_DIGIMON_ROW_LIMIT - first) return -1;

    grown = (DigimonData *)realloc(sDigRows, sizeof(DigimonData) * (size_t)(sDigRowCount + count));
    if (grown == NULL) return -1;
    sDigRows = grown;
    names = (int *)realloc(sDigNames, sizeof(int) * (size_t)(sDigRowCount + count));
    if (names == NULL) return -1; /* the rows grew but sDigRowCount did not: nothing was added */
    sDigNames = names;
    {
        PcDigiLinks *links = (PcDigiLinks *)realloc(sDigLinks, sizeof(PcDigiLinks) * (size_t)(sDigRowCount + count));

        if (links == NULL) return -1;
        sDigLinks = links;
        memset(links + sDigRowCount, 0, sizeof(PcDigiLinks) * (size_t)count);
    }
    memset(grown + sDigRowCount, 0, sizeof(DigimonData) * (size_t)count);
    memset(names + sDigRowCount, 0, sizeof(int) * (size_t)count);
    sDigRowCount += count;
    return first;
}

int pcDigimonNameId(const DigimonData *d) {
    if (sDigRows != NULL && d >= sDigRows && d < sDigRows + sDigRowCount && sDigNames[d - sDigRows] != 0) {
        return sDigNames[d - sDigRows] - 1;
    }
    return d->nameId;
}

void pcDigimonSetNameId(int row, int nameId) {
    if (row >= PC_DIGIMON_ORIG_COUNT && row - PC_DIGIMON_ORIG_COUNT < sDigRowCount && nameId >= 0) {
        sDigNames[row - PC_DIGIMON_ORIG_COUNT] = nameId + 1;
    }
}

DigimonData *pcDigimonRef(int row) {
    if (row >= 0 && row < PC_DIGIMON_ORIG_COUNT) return &DIGIMON_DATA_CARD[row];
    if (row >= PC_DIGIMON_ORIG_COUNT && row - PC_DIGIMON_ORIG_COUNT < sDigRowCount) return &sDigRows[row - PC_DIGIMON_ORIG_COUNT];
    if (!sDigWarned) {
        sDigWarned = 1;
        plogLine("state", "digimon: row %d is outside 0..%d; reads give zeros, writes are dropped (logged once)\n", row,
                 PC_DIGIMON_ORIG_COUNT + sDigRowCount - 1);
    }
    memset(&sDigScratch, 0, sizeof sDigScratch);
    return &sDigScratch;
}

PcDigiLinks *pcDigimonLinks(int row) {
    if (row >= 0 && row < PC_DIGIMON_ORIG_COUNT) return &sOrigLinks[row];
    if (row >= PC_DIGIMON_ORIG_COUNT && row - PC_DIGIMON_ORIG_COUNT < sDigRowCount) return &sDigLinks[row - PC_DIGIMON_ORIG_COUNT];
    return NULL;
}

void pcDigimonLinksInit(void) {
    int r, k;

    memset(sOrigLinks, 0, sizeof sOrigLinks);
    for (r = 0; r < PC_DIGIMON_ORIG_COUNT; r++) {
        sOrigLinks[r].id = DIGIMON_DATA_CARD[r].id;
        for (k = 0; k < 5; k++) sOrigLinks[r].blast[k] = DIGIMON_DATA_CARD[r].blastForms[k];
        sOrigLinks[r].pair = DIGIMON_DATA_CARD[r].pairPartner;
        sOrigLinks[r].set = 1;
    }
}

int pcDigimonSetLinks(int row, const unsigned short blast[5], unsigned short pair) {
    PcDigiLinks *l;

    if (row < PC_DIGIMON_ORIG_COUNT || (l = pcDigimonLinks(row)) == NULL) return 0;
    memcpy(l->blast, blast, sizeof l->blast);
    l->pair = pair;
    l->set = 1;
    return 1;
}

/* the links of the row `d` points to, NULL for a DigimonData that is not a row (a copy, the scratch row) */
static const PcDigiLinks *linksOf(const DigimonData *d) {
    if (d >= DIGIMON_DATA_CARD && d < DIGIMON_DATA_CARD + PC_DIGIMON_ORIG_COUNT) return &sOrigLinks[d - DIGIMON_DATA_CARD];
    if (sDigRows != NULL && d >= sDigRows && d < sDigRows + sDigRowCount) return &sDigLinks[d - sDigRows];
    return NULL;
}

int pcDigimonBlast(const DigimonData *d, int tier) {
    const PcDigiLinks *l = tier >= 0 && tier < 5 ? linksOf(d) : NULL;

    return l != NULL && l->set ? l->blast[tier] : (tier >= 0 && tier < 5 ? d->blastForms[tier] : 0);
}

int pcDigimonPair(const DigimonData *d) {
    const PcDigiLinks *l = linksOf(d);

    return l != NULL && l->set ? l->pair : d->pairPartner;
}

int pcDigimonRefOf(const DigimonData *d) {
    if (sDigRows != NULL && d >= sDigRows && d < sDigRows + sDigRowCount) return PC_DIGIMON_ORIG_COUNT + (int)(d - sDigRows) + 1;
    return d->nameId;
}

void pcDigimonResetRows(void) {
    free(sDigLinks);
    sDigLinks = NULL;
    free(sDigRows);
    free(sDigNames);
    sDigRows = NULL;
    sDigNames = NULL;
    sDigRowCount = 0;
    sDigWarned = 0;
}

int pcExtItemCapacity(void) {
    return sExtSize;
}

void pcExtStateReset(void) {
    free(sExtCount);
    free(sExtEquipped);
    sExtCount = sExtEquipped = NULL;
    sExtSize = PC_ITEM_ORIG_COUNT;
    sWarned = 0;
    sTechWarned = 0;
    pcPartnerEntriesReset();
    memset(sCardCount, 0, sizeof sCardCount);
    memset(sCardSeen, 0, sizeof sCardSeen);
    sCardWarned = 0;
    pcExtVarsReset();
    /* the Digimon rows are not part of this: they belong to the mods loaded at boot (digimonExtReset), and a save load
       calls this */
}

/* ---- dw3save-v1 "EXTI" chunk (T2) ----
 * u32 chunk version (1), u32 entry count, then per entry: u16 name length, the name bytes (no NUL),
 * s8 count, s8 equipped. Little endian. */

static PcExtItemNameFn sNameOf;
static PcExtItemIdFn sIdOf;

void pcExtStateSetNameHooks(PcExtItemNameFn nameOf, PcExtItemIdFn idOf) {
    sNameOf = nameOf;
    sIdOf = idOf;
}

unsigned long pcExtStateEncode(unsigned char *out, unsigned long cap) {
    unsigned long need = 8, entries = 0, pos;
    int id, pass, unnamed = 0;

    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            if (out == NULL || cap < need) return need;
            if (unnamed != 0) {
                plogLine("saves", "%d extension item(s) have no name and were not saved\n", unnamed);
            }
            contentPut32(out, 1);
            contentPut32(out + 4, entries);
            pos = 8;
        }
        for (id = PC_ITEM_ORIG_COUNT; id < sExtSize; id++) {
            s8 cnt = sExtCount[id - PC_ITEM_ORIG_COUNT];
            s8 eq = sExtEquipped[id - PC_ITEM_ORIG_COUNT];
            const char *name;
            unsigned long n;

            if (cnt == 0 && eq == 0) continue;
            name = sNameOf != NULL ? sNameOf(id) : NULL;
            if (name == NULL || name[0] == '\0') {
                if (pass == 0) unnamed++;
                continue;
            }
            n = (unsigned long)strlen(name);
            if (pass == 0) {
                need += 2 + n + 2;
                entries++;
            } else {
                contentWriteName(out + pos, name, n);
                out[pos + 2 + n] = (unsigned char)cnt;
                out[pos + 3 + n] = (unsigned char)eq;
                pos += 4 + n;
            }
        }
    }
    return need;
}

int pcExtStateDecode(const unsigned char *in, unsigned long len) {
    unsigned long count, i, pos;
    int dropped = 0;

    pcExtStateReset();
    if (in == NULL || len == 0) return 0;
    if (contentChunkOpen(in, len, &count) != 0) goto bad;
    pos = 8;
    for (i = 0; i < count; i++) {
        unsigned long n;
        char name[256];
        int id;

        n = contentReadName(in, pos, len, 2, name, sizeof name);
        if (n == 0) goto bad;
        id = sIdOf != NULL ? sIdOf(name) : -1;
        if (id < PC_ITEM_ORIG_COUNT || id >= PC_EXT_ITEM_LIMIT) {
            plogLine("saves", "item \"%s\" is not defined by any installed mod; dropped from the loaded save\n", name);
            dropped++;
        } else {
            *pcItemCountRef(id) = (s8)in[pos + 2 + n];
            *pcItemEquippedRef(id) = (s8)in[pos + 3 + n];
        }
        pos += 4 + n;
    }
    return dropped;
bad:
    pcExtStateReset();
    plogLine("saves", "the extension chunk of the save is damaged; its extra items are empty\n");
    return -1;
}

/* ---- dw3save-v1 "EXTT" chunk (extended techs T2) ---- */

static PcExtTechNameFn sTechNameOf;
static PcExtTechIdFn sTechIdOf;

void pcExtStateSetTechHooks(PcExtTechNameFn nameOf, PcExtTechIdFn idOf) {
    sTechNameOf = nameOf;
    sTechIdOf = idOf;
}

/* where the 13-bit ids live in a saved GameState block: partners[8].info.entries[44].skills[6] (s16, little endian) */
typedef char sizeCheck[(offsetof(GameState, partners) == 0x75C && GAME_SAVE_SIZE >= 0x26C4) ? 1 : -1];
#define SKILL_SLOTS 6
#define SK_BASE(p, e) (offsetof(GameState, partners) + (size_t)(p) * sizeof(Partner) + offsetof(Partner, info) + \
                       offsetof(PartnerStats, entries) + (size_t)(e) * sizeof(PartnerEntry) + offsetof(PartnerEntry, skills))
#define SK_END (SK_BASE(PARTNER_COUNT - 1, PARTNER_ENTRY_COUNT - 1) + SKILL_SLOTS * 2)

static unsigned skillAt(const unsigned char *g, size_t off) {
    return (unsigned)g[off] | ((unsigned)g[off + 1] << 8);
}

unsigned long pcExtStateEncodeTechs(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap) {
    unsigned long need = 8, entries = 0, pos = 8;
    int p, e, k, pass, unnamed = 0;
    static unsigned char seen[(PC_EXT_TECH_LAST_ID + 1) / 8 + 1];

    if (game == NULL || gameLen < SK_END) return 0;
    for (pass = 0; pass < 2; pass++) {
        memset(seen, 0, sizeof seen);
        for (p = 0; p < PARTNER_COUNT; p++) {
            for (e = 0; e < PARTNER_ENTRY_COUNT; e++) {
                for (k = 0; k < SKILL_SLOTS; k++) {
                    int id = (int)(skillAt(game, SK_BASE(p, e) + (size_t)k * 2) & SKILL_ID);
                    const char *name;
                    unsigned long n;

                    if (id < PC_TECH_FIRST_EXT_ID) continue;
                    if (seen[id >> 3] & (1 << (id & 7))) continue;
                    seen[id >> 3] |= (unsigned char)(1 << (id & 7));
                    name = sTechNameOf != NULL ? sTechNameOf(id) : NULL;
                    if (name == NULL || name[0] == '\0') {
                        if (pass == 0) unnamed++;
                        continue;
                    }
                    n = (unsigned long)strlen(name);
                    if (pass == 0) {
                        need += 4 + n;
                        entries++;
                    } else if (out != NULL) {
                        contentPut16(out + pos, (unsigned long)id);
                        pos += 2 + contentWriteName(out + pos + 2, name, n);
                    }
                }
            }
        }
        if (pass == 0) {
            if (entries == 0 && unnamed == 0) return 0; /* nothing to record: no chunk */
            if (unnamed != 0) plogLine("saves", "%d new technique id(s) have no name and cannot be saved by name\n", unnamed);
            if (out == NULL || cap < need) return need;
            contentPut32(out, 1);
            contentPut32(out + 4, entries);
        }
    }
    return need;
}

int pcExtStateRemapTechs(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len) {
    static short map[PC_EXT_TECH_LAST_ID + 1]; /* saved id -> id now, 0 = not named by the chunk, -1 = no mod */
    unsigned long pos = 8, count = 0, i;
    int p, e, k, removed = 0, changed = 0, damaged = 0;

    if (game == NULL || gameLen < SK_END) return 0;
    memset(map, 0, sizeof map);
    if (chunk != NULL && len != 0) {
        if (contentChunkOpen(chunk, len, &count) != 0) {
            damaged = 1;
        } else {
            for (i = 0; i < count; i++) {
                unsigned long n;
                char name[256];
                int saved, now;

                if (pos + 4 > len) { damaged = 1; break; }
                n = contentReadName(chunk, pos + 2, len, 0, name, sizeof name);
                if (n == 0) { damaged = 1; break; }
                saved = (int)contentGet16(chunk + pos) & SKILL_ID;
                now = sTechIdOf != NULL ? sTechIdOf(name) : -1;
                if (now < PC_TECH_FIRST_EXT_ID || now > PC_EXT_TECH_LAST_ID) {
                    plogLine("saves", "technique \"%s\" is not defined by any installed mod; removed from the loaded save\n", name);
                    now = -1;
                }
                map[saved] = (short)now;
                pos += 4 + n;
            }
        }
        if (damaged) plogLine("saves", "the technique chunk of the save is damaged; its new techniques are removed\n");
    }
    for (p = 0; p < PARTNER_COUNT; p++) {
        for (e = 0; e < PARTNER_ENTRY_COUNT; e++) {
            for (k = 0; k < SKILL_SLOTS; k++) {
                size_t off = SK_BASE(p, e) + (size_t)k * 2;
                unsigned raw = skillAt(game, off), id = raw & SKILL_ID;
                int now;

                if ((int)id < PC_TECH_FIRST_EXT_ID) continue;
                now = damaged ? -1 : (map[id] != 0 ? map[id] : -1);
                if (now == (int)id) continue;
                if (now < 0) {
                    plogLine("saves", "partner %d entry %d: technique id %d has no installed mod; the skill slot %d is emptied\n", p, e, (int)id, k);
                    raw = 0;
                    removed++;
                } else {
                    raw = (raw & ~(unsigned)SKILL_ID) | (unsigned)now;
                }
                game[off] = (unsigned char)raw;
                game[off + 1] = (unsigned char)(raw >> 8);
                changed = 1;
            }
        }
    }
    if (changed) {
        unsigned char x = 0;
        unsigned long b;

        for (b = 4; b < gameLen; b++) x ^= game[b];
        game[0] = x;
    }
    return damaged ? -1 : removed;
}

/* ---- dw3save-v1 "EXTD" chunk (new partner forms, P2) ---- */

static PcExtDigNameFn sDigNameOf;
static PcExtDigIdFn sDigIdOf;
static PcExtDigBaseFn sDigBaseOf;

void pcExtStateSetDigimonHooks(PcExtDigNameFn nameOf, PcExtDigIdFn idOf, PcExtDigBaseFn baseOf) {
    sDigNameOf = nameOf;
    sDigIdOf = idOf;
    sDigBaseOf = baseOf;
}

#define DIG_ID_LAST 32767
#define P_BASE(p) (offsetof(GameState, partners) + (size_t)(p) * sizeof(Partner))
#define P_BATTLE(p) (P_BASE(p) + offsetof(Partner, battleDigivolve))
#define P_SLOT(p, i) (P_BASE(p) + offsetof(Partner, info) + offsetof(PartnerStats, slots) + (size_t)(i) * 2)
#define P_ENTRY(p, e) (P_BASE(p) + offsetof(Partner, info) + offsetof(PartnerStats, entries) + (size_t)(e) * sizeof(PartnerEntry))
#define DIG_END (P_BASE(PARTNER_COUNT))

static int s16Of(const unsigned char *g, size_t off) {
    return (short)(g[off] | (g[off + 1] << 8));
}

static void put16Of(unsigned char *g, size_t off, int v) {
    g[off] = (unsigned char)v;
    g[off + 1] = (unsigned char)(v >> 8);
}

/* calls fn(offset, width) for every place of the block that holds a Digimon id: 0 = the id of a partner's entry, 1 = a
   picked form, 2 = the battle form (an s32 read here as its low s16: ids are < 32768) */
typedef void (*DigPlaceFn)(void *ctx, size_t off, int kind, int partner);

static void eachDigPlace(DigPlaceFn fn, void *ctx) {
    int p, e, i;

    for (p = 0; p < PARTNER_COUNT; p++) {
        for (e = 0; e < PARTNER_ENTRY_COUNT; e++) fn(ctx, P_ENTRY(p, e) + offsetof(PartnerEntry, id), 0, p);
        for (i = 0; i < 4; i++) fn(ctx, P_SLOT(p, i), 1, p);
        fn(ctx, P_BATTLE(p), 2, p);
    }
}

typedef struct DigEnc {
    const unsigned char *game;
    unsigned char *out;
    unsigned long pos, need, entries;
    int pass, unnamed;
    unsigned char seen[(DIG_ID_LAST + 1) / 8];
} DigEnc;

static void encPlace(void *ctx, size_t off, int kind, int partner) {
    DigEnc *c = ctx;
    int id = s16Of(c->game, off);
    const char *name;
    unsigned long n;

    (void)kind;
    (void)partner;
    if (id < PC_DIGIMON_FIRST_NEW_ID || id > DIG_ID_LAST || (c->seen[id >> 3] & (1 << (id & 7)))) return;
    c->seen[id >> 3] |= (unsigned char)(1 << (id & 7));
    name = sDigNameOf != NULL ? sDigNameOf(id) : NULL;
    if (name == NULL || name[0] == '\0') {
        if (c->pass == 0) c->unnamed++;
        return;
    }
    n = (unsigned long)strlen(name);
    if (c->pass == 0) {
        c->need += 6 + n;
        c->entries++;
    } else if (c->out != NULL) {
        int base = sDigBaseOf != NULL ? sDigBaseOf(id) : -1;

        contentPut16(c->out + c->pos, (unsigned long)id);
        contentPut16(c->out + c->pos + 2, (unsigned long)(base > 0 ? base : 0));
        c->pos += 4 + contentWriteName(c->out + c->pos + 4, name, n);
    }
}

unsigned long pcExtStateEncodeDigimon(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap) {
    static DigEnc c;
    int pass;

    if (game == NULL || gameLen < DIG_END) return 0;
    memset(&c, 0, sizeof c);
    c.game = game;
    c.need = 8;
    for (pass = 0; pass < 2; pass++) {
        c.pass = pass;
        c.pos = 8;
        memset(c.seen, 0, sizeof c.seen);
        c.out = pass == 1 ? out : NULL;
        eachDigPlace(encPlace, &c);
        if (pass == 0) {
            if (c.entries == 0 && c.unnamed == 0) return 0; /* nothing to record: no chunk */
            if (c.unnamed != 0) plogLine("saves", "%d new Digimon id(s) have no name and cannot be saved by name\n", c.unnamed);
            if (out == NULL || cap < c.need) return c.need;
            contentPut32(out, 1);
            contentPut32(out + 4, c.entries);
        }
    }
    return c.need;
}

typedef struct DigMap {
    int saved, now, base; /* now: the id the name has now, or -1 for no mod */
} DigMap;

static const DigMap *mapOf(const DigMap *m, int n, int saved) {
    int i;

    for (i = 0; i < n; i++) if (m[i].saved == saved) return &m[i];
    return NULL;
}

static int partnerHasEntry(const unsigned char *g, int p, int id) {
    int e;

    for (e = 0; id > 0 && e < PARTNER_ENTRY_COUNT; e++) {
        if (s16Of(g, P_ENTRY(p, e) + offsetof(PartnerEntry, id)) == id) return 1;
    }
    return 0;
}

int pcExtStateRemapDigimon(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len) {
    static DigMap map[PARTNER_COUNT * (PARTNER_ENTRY_COUNT + 5) + 1];
    unsigned long pos = 8, count = 0, i;
    int n = 0, p, e, k, dropped = 0, changed = 0, damaged = 0;

    if (game == NULL || gameLen < DIG_END) return 0;
    if (chunk != NULL && len != 0) {
        if (contentChunkOpen(chunk, len, &count) != 0) {
            damaged = 1;
        } else {
            for (i = 0; i < count && n < (int)(sizeof map / sizeof map[0]); i++) {
                unsigned long nl;
                char name[256];

                if (pos + 6 > len) { damaged = 1; break; }
                nl = contentReadName(chunk, pos + 4, len, 0, name, sizeof name);
                if (nl == 0) { damaged = 1; break; }
                map[n].saved = (int)contentGet16(chunk + pos);
                map[n].base = (int)contentGet16(chunk + pos + 2);
                map[n].now = sDigIdOf != NULL ? sDigIdOf(name) : -1;
                if (map[n].now < PC_DIGIMON_FIRST_NEW_ID) {
                    plogLine("saves", "Digimon form \"%s\" is not defined by any installed mod; dropped from the loaded save\n", name);
                    map[n].now = -1;
                    dropped++;
                }
                n++;
                pos += 6 + nl;
            }
        }
        if (damaged) plogLine("saves", "the Digimon chunk of the save is damaged; its new forms are dropped\n");
    }
    for (p = 0; p < PARTNER_COUNT; p++) {
        /* 1: the partner's entries */
        for (e = 0; e < PARTNER_ENTRY_COUNT; e++) {
            size_t off = P_ENTRY(p, e);
            int id = s16Of(game, off + offsetof(PartnerEntry, id));
            const DigMap *m;

            if (id < PC_DIGIMON_FIRST_NEW_ID) continue;
            m = damaged ? NULL : mapOf(map, n, id);
            if (m != NULL && m->now == id) continue; /* the same id now: nothing to do */
            changed = 1;
            if (m != NULL && m->now > 0) {
                put16Of(game, off + offsetof(PartnerEntry, id), m->now);
            } else if (m != NULL && m->base > 0 && !partnerHasEntry(game, p, m->base)) {
                put16Of(game, off + offsetof(PartnerEntry, id), m->base);
                plogLine("saves", "partner %d entry %d: Digimon form %d has no installed mod; the entry is its base form %d\n", p, e, id,
                         m->base);
            } else {
                memset(game + off, 0, sizeof(PartnerEntry));
                plogLine("saves", "partner %d entry %d: Digimon form %d has no installed mod; the entry is emptied\n", p, e, id);
            }
        }
        /* 2: the picked forms and the battle form */
        for (k = 0; k < 5; k++) {
            size_t off = k < 4 ? P_SLOT(p, k) : P_BATTLE(p);
            int id = s16Of(game, off), to;
            const DigMap *m;

            if (id < PC_DIGIMON_FIRST_NEW_ID) continue;
            m = damaged ? NULL : mapOf(map, n, id);
            to = (m != NULL && m->now > 0) ? m->now : ((m != NULL && partnerHasEntry(game, p, m->base)) ? m->base : 0);
            if (to == id) continue;
            put16Of(game, off, to);
            if (k == 4) put16Of(game, off + 2, 0); /* the high half of the s32 */
            changed = 1;
        }
    }
    if (changed) {
        unsigned char x = 0;
        unsigned long b;

        for (b = 4; b < gameLen; b++) x ^= game[b];
        game[0] = x;
    }
    return damaged ? -1 : dropped;
}

/* ---- partner entries beyond the original 44 (extended Digimon P3, ADR-008 M3) ----
 * The first PARTNER_ENTRY_COUNT entries of a partner stay in the saved GAME block; the rest live here, one growable array per
 * partner (pcPartnerEntryGrow adds one free entry). A pointer from pcPartnerEntryRef into the store is invalid after a grow. */

static PartnerEntry *sXEntries[PARTNER_COUNT];
static int sXCount[PARTNER_COUNT];
static PartnerEntry sEntryScratch;
static int sEntryWarned;

int pcPartnerEntryLimit(int partner) {
    return PARTNER_ENTRY_COUNT + (partner >= 0 && partner < PARTNER_COUNT ? sXCount[partner] : 0);
}

PartnerEntry *pcPartnerEntryRef(PartnerStats *info, int partner, int index) {
    if (index >= 0 && index < PARTNER_ENTRY_COUNT) return &info->entries[index];
    if (partner >= 0 && partner < PARTNER_COUNT && index >= PARTNER_ENTRY_COUNT && index - PARTNER_ENTRY_COUNT < sXCount[partner]) {
        return &sXEntries[partner][index - PARTNER_ENTRY_COUNT];
    }
    if (!sEntryWarned) {
        sEntryWarned = 1;
        plogLine("state", "partner entries: entry %d of partner %d is outside 0..%d; reads give zeros, writes are dropped (logged once)\n",
                 index, partner, pcPartnerEntryLimit(partner) - 1);
    }
    memset(&sEntryScratch, 0, sizeof sEntryScratch);
    return &sEntryScratch;
}

int pcPartnerEntryGrow(int partner) {
    PartnerEntry *grown;
    int at;

    if (partner < 0 || partner >= PARTNER_COUNT || pcPartnerEntryLimit(partner) >= PC_PARTNER_ENTRY_MAX) return -1;
    grown = (PartnerEntry *)realloc(sXEntries[partner], sizeof(PartnerEntry) * (size_t)(sXCount[partner] + 1));
    if (grown == NULL) return -1;
    sXEntries[partner] = grown;
    memset(&grown[sXCount[partner]], 0, sizeof(PartnerEntry));
    at = PARTNER_ENTRY_COUNT + sXCount[partner];
    sXCount[partner]++;
    return at;
}

void pcPartnerEntriesReset(void) {
    int p;

    for (p = 0; p < PARTNER_COUNT; p++) {
        free(sXEntries[p]);
        sXEntries[p] = NULL;
        sXCount[p] = 0;
    }
    sEntryWarned = 0;
}

/* ---- dw3save-v1 "EXTP" chunk (the entries beyond 44, P3) ----
 * u32 version (1), u32 count, then per stored entry (the empty ones are not written): u8 partner, u8 flags (bit k: skill k's
 * technique is named; bit 6: the Digimon is named), s8 level, s32 exp, u16 Digimon id (0 when named), 6 x u16 skill (the 13-bit
 * id is 0 when named; the flags stay), then the names in order: the Digimon's, then the techniques'. A name is u16 length and
 * the bytes. Names are the ones of the Digimon (EXTD) and technique (EXTT) hooks. Little endian. */

#define XP_NAMED_FORM 0x40

static const char *formNameOf(int id) {
    const char *n = id >= PC_DIGIMON_FIRST_NEW_ID && sDigNameOf != NULL ? sDigNameOf(id) : NULL;

    return n != NULL && n[0] != '\0' ? n : NULL;
}

static const char *techNameOf(int id) {
    const char *n = id >= PC_TECH_FIRST_EXT_ID && sTechNameOf != NULL ? sTechNameOf(id) : NULL;

    return n != NULL && n[0] != '\0' ? n : NULL;
}

unsigned long pcExtStateEncodeEntries(unsigned char *out, unsigned long cap) {
    unsigned long need = 8, entries = 0, pos;
    int p, e, k, pass, unnamed = 0;

    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            if (unnamed != 0 && out != NULL) {
                plogLine("saves", "%d stored partner entr(ies) hold a Digimon form nobody names and were not saved\n", unnamed);
            }
            if (entries == 0) return 0; /* nothing to keep: no chunk */
            if (out == NULL || cap < need) return need;
            contentPut32(out, 1);
            contentPut32(out + 4, entries);
            pos = 8;
        }
        for (p = 0; p < PARTNER_COUNT; p++) {
            for (e = 0; e < sXCount[p]; e++) {
                const PartnerEntry *x = &sXEntries[p][e];
                unsigned flags = 0;
                const char *form = NULL, *tech[6];
                unsigned long len = 21;
                int bad = 0;

                if (x->id == 0) continue;
                if (x->id >= PC_DIGIMON_FIRST_NEW_ID) {
                    form = formNameOf(x->id);
                    if (form == NULL) bad = 1;
                    else { flags |= XP_NAMED_FORM; len += 2 + strlen(form); }
                }
                for (k = 0; k < 6; k++) {
                    int t = (int)((unsigned)x->skills[k] & SKILL_ID);

                    tech[k] = NULL;
                    if (t >= PC_TECH_FIRST_EXT_ID) {
                        tech[k] = techNameOf(t);
                        if (tech[k] != NULL) { flags |= 1u << k; len += 2 + strlen(tech[k]); }
                        /* a technique nobody names is left out of the skill slot (it would read as another one) */
                    }
                }
                if (bad) {
                    if (pass == 0) unnamed++;
                    continue;
                }
                if (pass == 0) {
                    need += len;
                    entries++;
                } else {
                    unsigned char *o = out + pos;

                    o[0] = (unsigned char)p;
                    o[1] = (unsigned char)flags;
                    o[2] = (unsigned char)x->level;
                    contentPut32(o + 3, (unsigned long)x->exp);
                    contentPut16(o + 7, (flags & XP_NAMED_FORM) ? 0 : (unsigned long)x->id);
                    for (k = 0; k < 6; k++) {
                        unsigned raw = (unsigned)(unsigned short)x->skills[k];

                        if (flags & (1u << k)) raw &= ~(unsigned)SKILL_ID;
                        else if (((unsigned)x->skills[k] & SKILL_ID) >= PC_TECH_FIRST_EXT_ID) raw &= ~(unsigned)SKILL_ID; /* unnamed: empty */
                        contentPut16(o + 9 + 2 * k, raw);
                    }
                    o += 21;
                    if (form != NULL) o += contentWriteName(o, form, strlen(form));
                    for (k = 0; k < 6; k++) if (tech[k] != NULL) o += contentWriteName(o, tech[k], strlen(tech[k]));
                    pos = (unsigned long)(o - out);
                }
            }
        }
    }
    return need;
}

int pcExtStateDecodeEntries(const unsigned char *in, unsigned long len) {
    unsigned long count, i, pos = 8;
    int dropped = 0;

    pcPartnerEntriesReset();
    if (in == NULL || len == 0) return 0;
    if (contentChunkOpen(in, len, &count) != 0) goto bad;
    for (i = 0; i < count; i++) {
        PartnerEntry x;
        unsigned flags;
        int p, k, id, ok = 1, slot;
        char name[256];

        if (pos + 21 > len) goto bad;
        p = in[pos];
        flags = in[pos + 1];
        if (p >= PARTNER_COUNT) goto bad;
        memset(&x, 0, sizeof x);
        x.level = (s8)in[pos + 2];
        x.exp = (s32)contentGet32(in + pos + 3);
        id = (int)contentGet16(in + pos + 7);
        for (k = 0; k < 6; k++) x.skills[k] = (s16)contentGet16(in + pos + 9 + 2 * k);
        pos += 21;
        if (flags & XP_NAMED_FORM) {
            unsigned long nl = contentReadName(in, pos, len, 0, name, sizeof name);

            if (nl == 0) goto bad;
            pos += 2 + nl;
            id = sDigIdOf != NULL ? sDigIdOf(name) : -1;
            if (id < PC_DIGIMON_FIRST_NEW_ID) {
                plogLine("saves", "Digimon form \"%s\" is not defined by any installed mod; its stored partner %d entry is dropped\n", name, p);
                ok = 0;
                dropped++;
            }
        }
        for (k = 0; k < 6; k++) {
            if (flags & (1u << k)) {
                unsigned long nl = contentReadName(in, pos, len, 0, name, sizeof name);
                int t;

                if (nl == 0) goto bad;
                pos += 2 + nl;
                t = sTechIdOf != NULL ? sTechIdOf(name) : -1;
                if (t >= PC_TECH_FIRST_EXT_ID) x.skills[k] = (s16)(((unsigned)x.skills[k] & ~(unsigned)SKILL_ID) | (unsigned)t);
                else x.skills[k] = 0; /* the technique's mod is gone: the slot reads empty */
            }
        }
        if (!ok) continue;
        x.id = (s16)id;
        slot = pcPartnerEntryGrow(p);
        if (slot < 0) { plogLine("saves", "partner %d holds more entries than the port keeps (%d); the rest are dropped\n", p, PC_PARTNER_ENTRY_MAX); continue; }
        sXEntries[p][slot - PARTNER_ENTRY_COUNT] = x;
    }
    return dropped;
bad:
    pcPartnerEntriesReset();
    plogLine("saves", "the partner entries chunk of the save is damaged; its stored entries are dropped\n");
    return -1;
}

/* ---- the evolution lists (extended Digimon P3) ----
 * STFGTREP_learnDigimon reads a partner's list whole. The port's list is the original 44 entries (copied; the original array
 * is never written) and then the entries content mods declare, in order. */

static PcEvolution *sEvo[PARTNER_COUNT];
static const PcEvolution (*sEvoOrig)[PC_EVOLUTION_ORIG_COUNT];
static int sEvoExtra[PARTNER_COUNT];

int pcEvolutionCount(int partner) {
    return PC_EVOLUTION_ORIG_COUNT + (partner >= 0 && partner < PARTNER_COUNT ? sEvoExtra[partner] : 0);
}

void pcEvolutionSetOriginal(const void *lists) {
    sEvoOrig = lists;
}

void *pcEvolutionList(int partner, void *original) {
    return (partner >= 0 && partner < PARTNER_COUNT && sEvo[partner] != NULL) ? (void *)sEvo[partner] : original;
}

int pcEvolutionExtend(int partner, const PcEvolution *entry) {
    PcEvolution *grown;

    if (partner < 0 || partner >= PARTNER_COUNT || sEvoOrig == NULL || entry == NULL) return -1;
    grown = (PcEvolution *)realloc(sEvo[partner], sizeof(PcEvolution) * (size_t)(PC_EVOLUTION_ORIG_COUNT + sEvoExtra[partner] + 1));
    if (grown == NULL) return -1;
    if (sEvo[partner] == NULL) memcpy(grown, sEvoOrig[partner], sizeof(PcEvolution) * PC_EVOLUTION_ORIG_COUNT);
    sEvo[partner] = grown;
    grown[PC_EVOLUTION_ORIG_COUNT + sEvoExtra[partner]] = *entry;
    return PC_EVOLUTION_ORIG_COUNT + sEvoExtra[partner]++;
}

void pcEvolutionReset(void) {
    int p;

    for (p = 0; p < PARTNER_COUNT; p++) {
        free(sEvo[p]);
        sEvo[p] = NULL;
        sEvoExtra[p] = 0;
    }
}

/* ---- dw3save-v1 "EXTK" chunk (cards from content mods, U8b) ---- */

#define DECK_BASE(d, i) (offsetof(GameState, decks) + (size_t)(d) * sizeof(Deck) + offsetof(Deck, cards) + (size_t)(i) * 2)
#define DECK_END DECK_BASE(DECK_COUNT, 0)

unsigned long pcExtStateEncodeCards(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap) {
    unsigned long need = 8, entries = 0, pos = 8;
    int id, d, i, pass, unnamed = 0;
    static unsigned char used[PC_CARD_LIST_MAX];

    memset(used, 0, sizeof used);
    for (id = PC_CARD_ORIG_COUNT; id < PC_CARD_LIST_MAX; id++) {
        if (sCardCount[id - PC_CARD_ORIG_COUNT] != 0 || sCardSeen[id - PC_CARD_ORIG_COUNT] != 0) used[id] = 1;
    }
    if (game != NULL && gameLen >= DECK_END) {
        for (d = 0; d < DECK_COUNT; d++) {
            for (i = 0; i < DECK_SIZE; i++) {
                int v = s16Of(game, DECK_BASE(d, i));
                if (v >= PC_CARD_ORIG_COUNT && v < PC_CARD_LIST_MAX) used[v] = 1;
            }
        }
    }
    for (pass = 0; pass < 2; pass++) {
        for (id = PC_CARD_ORIG_COUNT; id < PC_CARD_LIST_MAX; id++) {
            const char *name;
            unsigned long n;

            if (!used[id]) continue;
            name = sCardNameOf != NULL ? sCardNameOf(id) : NULL;
            if (name == NULL || name[0] == '\0') {
                if (pass == 0) unnamed++;
                continue;
            }
            n = (unsigned long)strlen(name);
            if (pass == 0) {
                need += 2 + 2 + n + 2;
                entries++;
            } else if (out != NULL) {
                contentPut16(out + pos, (unsigned long)id);
                pos += 2 + contentWriteName(out + pos + 2, name, n);
                out[pos] = (unsigned char)sCardCount[id - PC_CARD_ORIG_COUNT];
                out[pos + 1] = sCardSeen[id - PC_CARD_ORIG_COUNT];
                pos += 2;
            }
        }
        if (pass == 0) {
            if (unnamed != 0) plogLine("saves", "%d new card id(s) have no name and cannot be saved by name\n", unnamed);
            if (entries == 0) return 0; /* nothing nameable to record: no chunk */
            if (out == NULL || cap < need) return need;
            contentPut32(out, 1);
            contentPut32(out + 4, entries);
        }
    }
    return need;
}

int pcExtStateDecodeCards(const unsigned char *in, unsigned long len) {
    unsigned long count, i, pos = 8;
    int dropped = 0;

    memset(sCardCount, 0, sizeof sCardCount);
    memset(sCardSeen, 0, sizeof sCardSeen);
    if (in == NULL || len == 0) return 0;
    if (contentChunkOpen(in, len, &count) != 0) goto bad;
    for (i = 0; i < count; i++) {
        unsigned long n;
        char name[256];
        int id;

        if (pos + 4 > len) goto bad;
        n = contentReadName(in, pos + 2, len, 2, name, sizeof name);
        if (n == 0) goto bad;
        id = sCardIdOf != NULL ? sCardIdOf(name) : -1;
        if (id < PC_CARD_ORIG_COUNT || id >= PC_CARD_LIST_MAX) {
            plogLine("saves", "card \"%s\" is not defined by any installed mod; dropped from the loaded save\n", name);
            dropped++;
        } else {
            sCardCount[id - PC_CARD_ORIG_COUNT] = (s8)in[pos + 4 + n];
            sCardSeen[id - PC_CARD_ORIG_COUNT] = in[pos + 5 + n];
        }
        pos += 6 + n;
    }
    return dropped;
bad:
    memset(sCardCount, 0, sizeof sCardCount);
    memset(sCardSeen, 0, sizeof sCardSeen);
    plogLine("saves", "the card chunk of the save is damaged; its new cards are empty\n");
    return -1;
}

int pcExtStateRemapCards(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len) {
    static short map[PC_CARD_LIST_MAX]; /* saved id -> id now, 0 = not named by the chunk, -1 = no mod */
    unsigned long pos = 8, count = 0, i;
    int d, k, replaced = 0, damaged = 0, changed = 0;

    if (game == NULL || gameLen < DECK_END) return 0;
    if (chunk == NULL || len == 0) return 0; /* a save with no EXTK section held no new card (its decks are original ids) */
    memset(map, 0, sizeof map);
    {
        if (contentChunkOpen(chunk, len, &count) != 0) {
            damaged = 1;
        } else {
            for (i = 0; i < count; i++) {
                unsigned long n;
                char name[256];
                int saved, now;

                if (pos + 4 > len) { damaged = 1; break; }
                n = contentReadName(chunk, pos + 2, len, 2, name, sizeof name);
                if (n == 0) { damaged = 1; break; }
                saved = (int)contentGet16(chunk + pos);
                now = sCardIdOf != NULL ? sCardIdOf(name) : -1;
                if (now < PC_CARD_ORIG_COUNT || now >= PC_CARD_LIST_MAX) now = -1;
                if (saved >= PC_CARD_ORIG_COUNT && saved < PC_CARD_LIST_MAX) map[saved] = (short)now;
                pos += 6 + n;
            }
        }
        if (damaged) plogLine("saves", "the card chunk of the save is damaged; the new cards of its decks are replaced\n");
    }
    for (d = 0; d < DECK_COUNT; d++) {
        for (k = 0; k < DECK_SIZE; k++) {
            size_t off = DECK_BASE(d, k);
            int id = s16Of(game, off), now;

            if (id < PC_CARD_ORIG_COUNT || id >= PC_CARD_LIST_MAX) continue;
            now = damaged ? -1 : (map[id] != 0 ? map[id] : -1);
            if (now == id) continue;
            if (now < 0) {
                now = sCardFallback != NULL ? sCardFallback[k] : 1;
                plogLine("saves", "deck %d slot %d: card id %d has no installed mod; it becomes card %d\n", d, k, id, now);
                replaced++;
            }
            put16Of(game, off, now);
            changed = 1;
        }
    }
    if (changed) {
        unsigned char x = 0;
        unsigned long b;

        for (b = 4; b < gameLen; b++) x ^= game[b];
        game[0] = x;
    }
    return damaged ? -1 : replaced;
}

/* ---- named variables of content mods (S5) and their dw3save-v1 "EXTV" chunk ---- */

typedef struct PcExtVar {
    char *name;
    int value;
} PcExtVar;

static PcExtVar *sVars;
static int sVarCount, sVarCap, sVarWarned;

static PcExtVar *varFind(const char *name) {
    int i;

    for (i = 0; i < sVarCount; i++) {
        if (strcmp(sVars[i].name, name) == 0) return &sVars[i];
    }
    return NULL;
}

int pcExtVarGet(const char *name) {
    PcExtVar *v = name != NULL ? varFind(name) : NULL;

    return v != NULL ? v->value : 0;
}

int pcExtVarSet(const char *name, int value) {
    PcExtVar *v;
    size_t n;

    if (name == NULL || name[0] == '\0' || (n = strlen(name)) > PC_EXT_VAR_NAME_MAX) {
        if (!sVarWarned) plogLine("state", "a mod variable needs a name of 1 to %d characters; the write is lost\n", PC_EXT_VAR_NAME_MAX);
        sVarWarned = 1;
        return -1;
    }
    v = varFind(name);
    if (v != NULL) {
        v->value = value;
        return 0;
    }
    if (value == 0) return 0; /* unset is zero */
    if (sVarCount >= PC_EXT_VAR_MAX) {
        if (!sVarWarned) plogLine("state", "more than %d mod variables are set; the write is lost\n", PC_EXT_VAR_MAX);
        sVarWarned = 1;
        return -1;
    }
    if (sVarCount == sVarCap) {
        int cap = sVarCap ? sVarCap * 2 : 32;
        PcExtVar *grown = (PcExtVar *)realloc(sVars, (size_t)cap * sizeof *sVars);

        if (grown == NULL) return -1;
        sVars = grown;
        sVarCap = cap;
    }
    sVars[sVarCount].name = (char *)malloc(n + 1);
    if (sVars[sVarCount].name == NULL) return -1;
    memcpy(sVars[sVarCount].name, name, n + 1);
    sVars[sVarCount].value = value;
    sVarCount++;
    return 0;
}

int pcExtVarCount(void) {
    int i, n = 0;

    for (i = 0; i < sVarCount; i++) n += sVars[i].value != 0;
    return n;
}

const char *pcExtVarNameAt(int i) {
    int k, n = 0;

    for (k = 0; k < sVarCount; k++) {
        if (sVars[k].value == 0) continue;
        if (n++ == i) return sVars[k].name;
    }
    return NULL;
}

void pcExtVarsReset(void) {
    int i;

    for (i = 0; i < sVarCount; i++) free(sVars[i].name);
    free(sVars);
    sVars = NULL;
    sVarCount = sVarCap = 0;
    sVarWarned = 0;
}

unsigned long pcExtStateEncodeVars(unsigned char *out, unsigned long cap) {
    unsigned long need = 8, pos = 8, entries = 0;
    int i;

    for (i = 0; i < sVarCount; i++) {
        if (sVars[i].value == 0) continue;
        need += 2 + (unsigned long)strlen(sVars[i].name) + 4;
        entries++;
    }
    if (entries == 0) return 0;
    if (out == NULL || cap < need) return need;
    contentPut32(out, 1);
    contentPut32(out + 4, entries);
    for (i = 0; i < sVarCount; i++) {
        unsigned long n;

        if (sVars[i].value == 0) continue;
        n = (unsigned long)strlen(sVars[i].name);
        pos += contentWriteName(out + pos, sVars[i].name, n);
        contentPut32(out + pos, (unsigned long)(unsigned int)sVars[i].value);
        pos += 4;
    }
    return need;
}

int pcExtStateDecodeVars(const unsigned char *in, unsigned long len) {
    unsigned long count, i, pos = 8;
    int kept = 0;

    pcExtVarsReset();
    if (in == NULL || len == 0) return 0;
    if (contentChunkOpen(in, len, &count) != 0) goto bad;
    for (i = 0; i < count; i++) {
        unsigned long n;
        char name[PC_EXT_VAR_NAME_MAX + 8];

        n = contentReadName(in, pos, len, 4, name, sizeof name);
        if (n == 0 || n > PC_EXT_VAR_NAME_MAX) goto bad;
        if (pcExtVarSet(name, (int)(unsigned int)contentGet32(in + pos + 2 + n)) == 0) kept++;
        pos += 2 + n + 4;
    }
    return kept;
bad:
    pcExtVarsReset();
    plogLine("saves", "the mod variables chunk of the save is damaged; the mods' flags start again\n");
    return -1;
}
