/*
 * The port's extended game state (ADR-008): see ext_state.h.
 * The item counts (T1) and their dw3save-v1 chunk (T2).
 */

#include <stddef.h>
#include <stdio.h>
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
typedef char sizeCheck[(offsetof(GameState, partnersCard) == 0x75C && GAME_SAVE_SIZE >= 0x26C4) ? 1 : -1];
#define SKILL_SLOTS 6
/* The tech and Digimon codecs below work on an array of Partner records read as bytes (`parr`, n partners): the block's mirror
   (GAME.partnersCard) for the wrappers the game block uses, or the partners a "PTNR" section holds. Offsets are relative to it. */
#define BLOCK_PARTNERS offsetof(GameState, partnersCard)
#define SK_BASE(p, e) ((size_t)(p) * sizeof(Partner) + offsetof(Partner, info) + \
                       offsetof(PartnerStats, entries) + (size_t)(e) * sizeof(PartnerEntry) + offsetof(PartnerEntry, skills))
#define SK_END (BLOCK_PARTNERS + SK_BASE(PARTNER_COUNT - 1, PARTNER_ENTRY_COUNT - 1) + SKILL_SLOTS * 2)

static unsigned skillAt(const unsigned char *g, size_t off) {
    return (unsigned)g[off] | ((unsigned)g[off + 1] << 8);
}

static unsigned long encodeTechsOf(const unsigned char *game, int nPartners, unsigned char *out, unsigned long cap) {
    unsigned long need = 8, entries = 0, pos = 8;
    int p, e, k, pass, unnamed = 0;
    static unsigned char seen[(PC_EXT_TECH_LAST_ID + 1) / 8 + 1];

    for (pass = 0; pass < 2; pass++) {
        memset(seen, 0, sizeof seen);
        for (p = 0; p < nPartners; p++) {
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

/* *changed is set when a slot was rewritten */
static int remapTechsOf(unsigned char *game, int nPartners, const unsigned char *chunk, unsigned long len, int *changedOut) {
    static short map[PC_EXT_TECH_LAST_ID + 1]; /* saved id -> id now, 0 = not named by the chunk, -1 = no mod */
    unsigned long pos = 8, count = 0, i;
    int p, e, k, removed = 0, changed = 0, damaged = 0;

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
    for (p = 0; p < nPartners; p++) {
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
    if (changedOut != NULL) *changedOut = changed;
    return damaged ? -1 : removed;
}

static void fixChecksum(unsigned char *game, unsigned long gameLen) {
    unsigned char x = 0;
    unsigned long b;

    for (b = 4; b < gameLen; b++) x ^= game[b];
    game[0] = x;
}

unsigned long pcExtStateEncodeTechs(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap) {
    if (game == NULL || gameLen < SK_END) return 0;
    return encodeTechsOf(game + BLOCK_PARTNERS, PARTNER_COUNT, out, cap);
}

int pcExtStateRemapTechs(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len) {
    int changed = 0, r;

    if (game == NULL || gameLen < SK_END) return 0;
    r = remapTechsOf(game + BLOCK_PARTNERS, PARTNER_COUNT, chunk, len, &changed);
    if (changed) fixChecksum(game, gameLen);
    return r;
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
#define P_BASE(p) ((size_t)(p) * sizeof(Partner)) /* relative to the partner array (see BLOCK_PARTNERS) */
#define P_BATTLE(p) (P_BASE(p) + offsetof(Partner, battleDigivolve))
#define P_SLOT(p, i) (P_BASE(p) + offsetof(Partner, info) + offsetof(PartnerStats, slots) + (size_t)(i) * 2)
#define P_ENTRY(p, e) (P_BASE(p) + offsetof(Partner, info) + offsetof(PartnerStats, entries) + (size_t)(e) * sizeof(PartnerEntry))
#define DIG_END (BLOCK_PARTNERS + P_BASE(PARTNER_COUNT))

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

static void eachDigPlace(DigPlaceFn fn, void *ctx, int nPartners) {
    int p, e, i;

    for (p = 0; p < nPartners; p++) {
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

static unsigned long encodeDigimonOf(const unsigned char *game, int nPartners, unsigned char *out, unsigned long cap) {
    static DigEnc c;
    int pass;

    memset(&c, 0, sizeof c);
    c.game = game;
    c.need = 8;
    for (pass = 0; pass < 2; pass++) {
        c.pass = pass;
        c.pos = 8;
        memset(c.seen, 0, sizeof c.seen);
        c.out = pass == 1 ? out : NULL;
        eachDigPlace(encPlace, &c, nPartners);
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

#define DIG_MAP_MAX 4096 /* distinct new forms a chunk can name (a safety bound: the ids are s16 and the registry far smaller) */

static int remapDigimonOf(unsigned char *game, int nPartners, const unsigned char *chunk, unsigned long len, int *changedOut) {
    static DigMap map[DIG_MAP_MAX];
    unsigned long pos = 8, count = 0, i;
    int n = 0, p, e, k, dropped = 0, changed = 0, damaged = 0;

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
    for (p = 0; p < nPartners; p++) {
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
    if (changedOut != NULL) *changedOut = changed;
    return damaged ? -1 : dropped;
}

unsigned long pcExtStateEncodeDigimon(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap) {
    if (game == NULL || gameLen < DIG_END) return 0;
    return encodeDigimonOf(game + BLOCK_PARTNERS, PARTNER_COUNT, out, cap);
}

int pcExtStateRemapDigimon(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len) {
    int changed = 0, r;

    if (game == NULL || gameLen < DIG_END) return 0;
    r = remapDigimonOf(game + BLOCK_PARTNERS, PARTNER_COUNT, chunk, len, &changed);
    if (changed) fixChecksum(game, gameLen);
    return r;
}

/* ---- the partners (ADR-013, ADR-014) ----
 * One contiguous array of Partner, all of them (the original eight first), reached by PC_PARTNER(p) as plain indexing: no call, no
 * check. It starts as a static array of eight so that nothing needs an init call; pcPartnersGrow moves it to the heap. The block
 * mirror in GAME (partnersCard) is only synced at the edges: import after a load or a new game, export before GAME's saved part is
 * copied out. A pointer into the array is invalid after pcPartnersGrow, as the Digimon rows' pointers are after pcDigimonExtendRows.
 *
 * Beside it the port keeps what identifies a partner for good: its NAME ("dw3:partner/<i>" for the original eight, "<mod>:<key>"
 * for the others). An index is only the registration order of this run; every saved reference to a partner is a name (the "PTNR"
 * section, below). The per-partner stores (stored entries, evolution lists) grow with the count: no fixed eight. */

typedef struct PartnerMeta {
    char name[PC_PARTNER_NAME_MAX];
    char sprite[96]; /* "<mod>:<name>" of the sprite folder that is its character in the field, or "" (the removed partner.json "sprite" field) */
    int iconBase; /* the original partner (0..7) whose art stands in for this one until it has its own (P6/P7) */
    /* equipment (P4): which original partner's bit of an item's `partners` mask this partner reads, the items it is allowed or refused
       whatever that bit says, and its equipment set (items worn whole and what they add). The original eight read the game's tables. */
    int equipLike;
    int *allow, *deny;
    int nAllow, nDeny;
    int hasSet;
    int setItem[4];
    int setBonus[6];
} PartnerMeta;

static Partner sPartnersFirst[PARTNER_COUNT];
static int sPartnerCap = PARTNER_COUNT;
Partner *pcPartners = sPartnersFirst;
int pcPartnerCount = PARTNER_COUNT;
static int sRowsFirst[PARTNER_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7}; /* the original eight: partner p's first form is row p */
int *pcPartnerRows = sRowsFirst;
static PartnerMeta sMetaFirst[PARTNER_COUNT] = {
    {"dw3:partner/0", 0}, {"dw3:partner/1", 1}, {"dw3:partner/2", 2}, {"dw3:partner/3", 3},
    {"dw3:partner/4", 4}, {"dw3:partner/5", 5}, {"dw3:partner/6", 6}, {"dw3:partner/7", 7},
};
static PartnerMeta *sMeta = sMetaFirst;

/* the per-partner stores (below, after the registry): a slot per partner, grown with pcPartnersGrow */
static PartnerEntry **sXEntries;
static int *sXCount;
static PcEvolution **sEvo;
static int *sEvoExtra;
static int sStoreCap;
static int storesEnsure(int count);

void pcPartnerSetRow(int partner, int row) {
    if (partner >= PARTNER_COUNT && partner < pcPartnerCount) pcPartnerRows[partner] = row;
}

int pcPartnersGrow(int count) {
    int first = pcPartnerCount, i;

    if (count <= pcPartnerCount) return first;
    if (count > PC_PARTNER_LIMIT) return -1;
    if (!storesEnsure(count)) return -1;
    if (count > sPartnerCap) {
        int cap = sPartnerCap * 2 > count ? sPartnerCap * 2 : count;
        Partner *grown;
        int *rows;
        PartnerMeta *meta;

        if (cap > PC_PARTNER_LIMIT) cap = PC_PARTNER_LIMIT;
        if (pcPartners == sPartnersFirst) {
            grown = (Partner *)malloc(sizeof(Partner) * (size_t)cap);
            rows = (int *)malloc(sizeof(int) * (size_t)cap);
            meta = (PartnerMeta *)malloc(sizeof(PartnerMeta) * (size_t)cap);
            if (grown != NULL && rows != NULL && meta != NULL) {
                memcpy(grown, sPartnersFirst, sizeof(Partner) * (size_t)pcPartnerCount);
                memcpy(rows, sRowsFirst, sizeof(int) * (size_t)pcPartnerCount);
                memcpy(meta, sMetaFirst, sizeof(PartnerMeta) * (size_t)pcPartnerCount);
            } else {
                free(grown);
                free(rows);
                free(meta);
                return -1;
            }
        } else {
            grown = (Partner *)realloc(pcPartners, sizeof(Partner) * (size_t)cap);
            if (grown != NULL) pcPartners = grown; /* the old block is gone: keep the new one even if the others fail */
            rows = (int *)realloc(pcPartnerRows, sizeof(int) * (size_t)cap);
            if (rows != NULL) pcPartnerRows = rows;
            meta = (PartnerMeta *)realloc(sMeta, sizeof(PartnerMeta) * (size_t)cap);
            if (meta != NULL) sMeta = meta;
        }
        if (grown == NULL || rows == NULL || meta == NULL) return -1;
        pcPartners = grown;
        pcPartnerRows = rows;
        sMeta = meta;
        sPartnerCap = cap;
    }
    memset(&pcPartners[pcPartnerCount], 0, sizeof(Partner) * (size_t)(count - pcPartnerCount));
    for (i = pcPartnerCount; i < count; i++) {
        pcPartnerRows[i] = -1;
        memset(&sMeta[i], 0, sizeof(PartnerMeta));
    }
    pcPartnerCount = count;
    return first;
}

/* ---- the registry: names, first-form rows, art stand-in ---- */

int pcPartnerFind(const char *name) {
    int i;

    if (name == NULL) return -1;
    for (i = 0; i < pcPartnerCount; i++) {
        if (strcmp(sMeta[i].name, name) == 0) return i;
    }
    return -1;
}

const char *pcPartnerName(int partner) {
    return partner >= 0 && partner < pcPartnerCount && sMeta[partner].name[0] != '\0' ? sMeta[partner].name : NULL;
}

int pcPartnerRegister(const char *name, int firstFormRow) {
    int idx;

    if (name == NULL || name[0] == '\0' || strlen(name) >= PC_PARTNER_NAME_MAX) return -1;
    idx = pcPartnerFind(name);
    if (idx < 0) {
        idx = pcPartnersGrow(pcPartnerCount + 1);
        if (idx < 0) {
            plogLine("state", "partner \"%s\": no room for another partner (limit %d)\n", name, PC_PARTNER_LIMIT);
            return -1;
        }
        strcpy(sMeta[idx].name, name);
        sMeta[idx].iconBase = firstFormRow >= 0 && firstFormRow < PARTNER_COUNT ? firstFormRow : 0;
        sMeta[idx].equipLike = sMeta[idx].iconBase;
    }
    pcPartnerSetRow(idx, firstFormRow); /* a no-op for the original eight */
    return idx;
}

void pcPartnerSetIconBase(int partner, int base) {
    if (partner >= PARTNER_COUNT && partner < pcPartnerCount && base >= 0 && base < PARTNER_COUNT) sMeta[partner].iconBase = base;
}

void pcPartnerSetSprite(int partner, const char *name) {
    if (partner >= PARTNER_COUNT && partner < pcPartnerCount && name != NULL && strlen(name) < sizeof sMeta[partner].sprite) strcpy(sMeta[partner].sprite, name);
}

const char *pcPartnerSpriteName(int partner) {
    return partner >= PARTNER_COUNT && partner < pcPartnerCount && sMeta[partner].sprite[0] != '\0' ? sMeta[partner].sprite : NULL;
}

int pcPartnerFieldCharacter(int partner, int spriteChar, int firstChar) {
    if (partner < 0) return -1;
    if (partner < PARTNER_COUNT) return firstChar + partner;
    if (spriteChar >= 0) return spriteChar;
    return firstChar + pcPartnerArt(partner); /* the stand-in's own character, never an NPC of the table */
}

int pcPartnerIconBase(int partner) {
    return partner >= 0 && partner < PARTNER_COUNT ? partner : (partner >= PARTNER_COUNT && partner < pcPartnerCount ? sMeta[partner].iconBase : 0);
}

/* the index art tables of the original eight are read with: the partner itself, or its stand-in; anything else the first one's */
int pcPartnerArt(int partner) {
    if (partner < PARTNER_COUNT) return partner; /* negative: as the original, which only reads with a valid party member */
    return partner < pcPartnerCount ? sMeta[partner].iconBase : 0;
}

/* The one animation table of the partners' 2D art (P6): the frames every screen's [8][7] table held (the seven were identical, R4), read by the
   art index so a new partner shows its stand-in's row until it has art of its own. A row ends at -1; every row has one before its 7th entry. */
static PartnerAnim sPartnerAnims[PARTNER_COUNT] = {
    {{7, 8, 9, 10, 9, 8, -1}}, {{14, 15, 16, 15, -1, -1, -1}}, {{11, 12, 13, 12, -1, -1, -1}}, {{3, 4, 5, 6, 5, 4, -1}},
    {{25, 26, 27, 28, 27, 26, -1}}, {{0, 1, 2, 1, -1, -1, -1}}, {{17, 18, 19, 20, 19, 18, -1}}, {{21, 22, 23, 24, 23, 22, -1}},
};

PartnerAnim *pcPartnerAnim(int partner) {
    return &sPartnerAnims[pcPartnerArt(partner)];
}

int pcPartnerAnimNext(int partner, int step) {
    const s32 *frames = pcPartnerAnim(partner)->frames;

    step++;
    return step >= 7 || frames[step] == -1 ? 0 : step;
}

/* ---- what the game reads about a partner a mod adds, beside the array (P4) ---- */

/* ---- equipment eligibility (the u8 `partners` mask of an item's record has a bit for each of the original eight only) ---- */

void pcPartnerSetEquipLike(int partner, int like) {
    if (partner >= PARTNER_COUNT && partner < pcPartnerCount && like >= 0 && like < PARTNER_COUNT) sMeta[partner].equipLike = like;
}

static int idListAdd(int **list, int *n, int id) {
    int i, *grown;

    for (i = 0; i < *n; i++) if ((*list)[i] == id) return 0;
    grown = (int *)realloc(*list, sizeof(int) * (size_t)(*n + 1));
    if (grown == NULL) return -1;
    *list = grown;
    grown[(*n)++] = id;
    return 0;
}

static int idListHas(const int *list, int n, int id) {
    int i;

    for (i = 0; i < n; i++) if (list[i] == id) return 1;
    return 0;
}

int pcPartnerEquipRule(int partner, int item, int allow) {
    PartnerMeta *m;

    if (partner < PARTNER_COUNT || partner >= pcPartnerCount || item <= 0) return -1;
    m = &sMeta[partner];
    return allow ? idListAdd(&m->allow, &m->nAllow, item) : idListAdd(&m->deny, &m->nDeny, item);
}

/* whether `partner` can wear item `item` whose record's mask is `mask`: the bit for the original eight; for a new partner an explicit
   refusal first, then an explicit permission, then the bit of the original partner it is like */
int pcPartnerCanEquip(int partner, int item, unsigned mask) {
    const PartnerMeta *m;

    if (partner >= 0 && partner < PARTNER_COUNT) return (int)((mask >> partner) & 1u);
    if (partner < 0 || partner >= pcPartnerCount) return 0;
    m = &sMeta[partner];
    if (idListHas(m->deny, m->nDeny, item)) return 0;
    if (idListHas(m->allow, m->nAllow, item)) return 1;
    return (int)((mask >> m->equipLike) & 1u);
}

/* ---- equipment sets (EQUIP_SETS / EQUIP_SET_BONUSES hold the original eight) ---- */

void pcPartnerSetEquipSet(int partner, const int items[4], const int bonuses[6]) {
    PartnerMeta *m;

    if (partner < PARTNER_COUNT || partner >= pcPartnerCount) return;
    m = &sMeta[partner];
    memcpy(m->setItem, items, sizeof m->setItem);
    memcpy(m->setBonus, bonuses, sizeof m->setBonus);
    m->hasSet = 1;
}

int pcPartnerHasEquipSet(int partner) {
    return partner >= 0 && partner < PARTNER_COUNT ? 1 : (partner >= PARTNER_COUNT && partner < pcPartnerCount && sMeta[partner].hasSet);
}

int pcPartnerEquipSetItem(int partner, int k) {
    return partner >= PARTNER_COUNT && partner < pcPartnerCount && k >= 0 && k < 4 ? sMeta[partner].setItem[k] : 0;
}

int pcPartnerEquipSetBonus(int partner, int k) {
    return partner >= PARTNER_COUNT && partner < pcPartnerCount && k >= 0 && k < 6 ? sMeta[partner].setBonus[k] : 0;
}

/* ---- the starter teams: the original sets (STARTER_PARTIES) and the ones mods add, one table for the player-name screen and setParty ---- */

typedef char StarterName[PC_PARTNER_NAME_MAX];
static int (*sStarter)[3];
static StarterName *sStarterName; /* team k's name, beside its partners */
static int sStarterCount, sStarterCap;
static int sStarterOrig;
static int sStarterNone[3];

static int starterRoom(int need) {
    int cap = sStarterCap > 8 ? sStarterCap : 8;
    int (*grown)[3];
    StarterName *names;

    if (need <= sStarterCap) return 1;
    while (cap < need) cap *= 2;
    grown = (int (*)[3])realloc(sStarter, sizeof(*grown) * (size_t)cap);
    if (grown == NULL) return 0;
    sStarter = grown;
    names = (StarterName *)realloc(sStarterName, sizeof(*names) * (size_t)cap);
    if (names == NULL) return 0;
    sStarterName = names;
    sStarterCap = cap;
    return 1;
}

void pcStarterPartiesInit(const unsigned char (*orig)[3], int count) {
    int i, k;

    if (!starterRoom(count)) return;
    for (i = 0; i < count; i++) {
        for (k = 0; k < 3; k++) sStarter[i][k] = orig[i][k];
        snprintf(sStarterName[i], sizeof sStarterName[i], "dw3:team/%d", i);
    }
    sStarterCount = sStarterOrig = count;
}

int pcStarterPartyAdd(const char *name, const int partners[3]) {
    int k;

    if (name == NULL || name[0] == '\0' || strlen(name) >= PC_PARTNER_NAME_MAX || !starterRoom(sStarterCount + 1)) return -1;
    for (k = 0; k < 3; k++) sStarter[sStarterCount][k] = partners[k];
    memcpy(sStarterName[sStarterCount], name, strlen(name) + 1);
    return sStarterCount++;
}

void pcStarterPartiesReset(void) {
    sStarterCount = sStarterOrig;
}

int pcStarterPartyCount(void) {
    return sStarterCount;
}

const char *pcStarterPartyName(int set) {
    return set >= 0 && set < sStarterCount ? sStarterName[set] : NULL;
}

int pcStarterPartyFind(const char *name) {
    int i;

    if (name == NULL) return -1;
    for (i = 0; i < sStarterCount; i++) {
        if (strcmp(sStarterName[i], name) == 0) return i;
    }
    return -1;
}

/* How many rows the team page shows for `count` teams */
int pcStarterRowsShown(int count, int max) {
    if (count < 0 || max < 0) {
        return 0;
    }
    return count < max ? count : max;
}

/* One step of the team page's cursor among `count` teams with `rows` rows on screen: `choice` moves by `delta` (clamped, as the original page
   does with three), `top` (the first team shown) only moves when the cursor leaves the rows, and stays where no blank row would show under the
   last team. A state that no longer fits (a team a mod removed) is pulled inside. */
void pcStarterPageMove(int *choice, int *top, int delta, int count, int rows) {
    int c = *choice + delta, t = *top, last = count - 1;

    if (count <= 0 || rows <= 0) {
        *choice = *top = 0;
        return;
    }
    if (c > last) c = last;
    if (c < 0) c = 0;
    if (c < t) t = c;
    if (c >= t + rows) t = c - rows + 1;
    if (t > count - rows) t = count - rows;
    if (t < 0) t = 0;
    *choice = c;
    *top = t;
}

/* team `set`'s three partners; a set that does not exist reads as no partners (-1), never outside the table */
int *pcStarterParty(int set) {
    if (set < 0 || set >= sStarterCount) {
        sStarterNone[0] = sStarterNone[1] = sStarterNone[2] = -1;
        return sStarterNone;
    }
    return sStarter[set];
}

/* ---- the texts of the partners and teams a mod adds: entries of NAMEET after the disc's own (text_data.h, tdSetPartnerTextHook) ---- */

int pcPartnerTextEntry(int partner, int desc) {
    return PC_NAMEET_FIRST + 2 * (partner - PARTNER_COUNT) + (desc ? 1 : 0);
}

int pcStarterTextEntry(int set, int desc) {
    return PC_NAMEET_FIRST + 2 * (pcPartnerCount - PARTNER_COUNT) + 2 * (set - sStarterOrig) + (desc ? 1 : 0);
}

/* the label of a new team's row (the original's are the letters A, B, C: entries 7..9): one entry per new team after all the titles and descriptions */
int pcStarterLabelEntry(int set) {
    return PC_NAMEET_FIRST + 2 * (pcPartnerCount - PARTNER_COUNT) + 2 * (sStarterCount - sStarterOrig) + (set - sStarterOrig);
}

/* ---- the digimon lab's list of partners outside the party: room for every partner (LabPartyScreen.partners was a fixed [8]) ---- */

static int sLabFirst[PARTNER_COUNT + 1];
static int *sLabPartners = sLabFirst;
static int sLabCap = PARTNER_COUNT + 1;

int *pcLabPartnerBuffer(void) {
    int need = pcPartnerCount + 1;

    if (need > sLabCap) {
        int *grown = (int *)malloc(sizeof(int) * (size_t)need);

        if (grown == NULL) return sLabPartners; /* keep what there is */
        if (sLabPartners != sLabFirst) free(sLabPartners);
        sLabPartners = grown;
        sLabCap = need;
    }
    return sLabPartners;
}

/* a partner of a saved block (the card screen's copy of GAME) or of the port's array: the block holds the first eight only */
static Partner sSavedNone;
Partner *pcPartnerSaved(GameState *game, int partner) {
    if (partner >= 0 && partner < PARTNER_COUNT) return &game->partnersCard[partner];
    if (partner >= PARTNER_COUNT && partner < pcPartnerCount) return &pcPartners[partner];
    memset(&sSavedNone, 0, sizeof sSavedNone); /* an empty party slot (-1) or an index nobody has: a locked, level 0 partner */
    return &sSavedNone;
}

static void stagedForget(void);

/* back to the original eight: the registry, their data and stores forgotten (a boot that registers again, tests) */
void pcPartnersUnregister(void) {
    int i;

    if (pcPartnerCount > PARTNER_COUNT) {
        memset(&pcPartners[PARTNER_COUNT], 0, sizeof(Partner) * (size_t)(pcPartnerCount - PARTNER_COUNT));
        for (i = PARTNER_COUNT; i < pcPartnerCount; i++) {
            free(sMeta[i].allow);
            free(sMeta[i].deny);
            memset(&sMeta[i], 0, sizeof(PartnerMeta));
        }
        pcPartnerCount = PARTNER_COUNT;
    }
    for (i = PARTNER_COUNT; i < sStoreCap; i++) { /* what was stored for the partners that are gone */
        free(sXEntries[i]);
        sXEntries[i] = NULL;
        sXCount[i] = 0;
        free(sEvo[i]);
        sEvo[i] = NULL;
        sEvoExtra[i] = 0;
    }
    stagedForget();
}

static void stagedApply(void);

/* GAME's mirror -> the array's first eight. The partners above them are cleared (a load or a new game starts them from nothing);
   they stay registered. A save the load read held their data in a "PTNR" section: it is laid over now (stagedApply). */
void pcPartnersImport(void) {
    if (pcPartnerCount > PARTNER_COUNT) {
        memset(&pcPartners[PARTNER_COUNT], 0, sizeof(Partner) * (size_t)(pcPartnerCount - PARTNER_COUNT));
    }
    memcpy(pcPartners, GAME.partnersCard, sizeof GAME.partnersCard);
    stagedApply();
}

void pcPartnersExport(void) {
    memcpy(GAME.partnersCard, pcPartners, sizeof GAME.partnersCard);
}

/* ---- the per-partner stores: partner entries beyond the original 44 (extended Digimon P3, ADR-008 M3) and the evolution lists ----
 * The first PARTNER_ENTRY_COUNT entries of a partner stay in the saved GAME block; the rest live here, one growable array per
 * partner (pcPartnerEntryGrow adds one free entry). A pointer from pcPartnerEntryRef into the store is invalid after a grow.
 * Every array here has a slot per partner (sStoreCap, grown with pcPartnersGrow); a partner past it has nothing stored. */

static PartnerEntry sEntryScratch;
static int sEntryWarned;

static int storesEnsure(int count) {
    int cap;
    PartnerEntry **xe;
    int *xc;
    PcEvolution **ev;
    int *ex;

    if (count <= sStoreCap) return 1;
    cap = count > PARTNER_COUNT ? count : PARTNER_COUNT;
    xe = (PartnerEntry **)realloc(sXEntries, sizeof(*xe) * (size_t)cap);
    if (xe != NULL) sXEntries = xe;
    xc = (int *)realloc(sXCount, sizeof(*xc) * (size_t)cap);
    if (xc != NULL) sXCount = xc;
    ev = (PcEvolution **)realloc(sEvo, sizeof(*ev) * (size_t)cap);
    if (ev != NULL) sEvo = ev;
    ex = (int *)realloc(sEvoExtra, sizeof(*ex) * (size_t)cap);
    if (ex != NULL) sEvoExtra = ex;
    if (xe == NULL || xc == NULL || ev == NULL || ex == NULL) return 0;
    memset(&sXEntries[sStoreCap], 0, sizeof(*xe) * (size_t)(cap - sStoreCap));
    memset(&sXCount[sStoreCap], 0, sizeof(*xc) * (size_t)(cap - sStoreCap));
    memset(&sEvo[sStoreCap], 0, sizeof(*ev) * (size_t)(cap - sStoreCap));
    memset(&sEvoExtra[sStoreCap], 0, sizeof(*ex) * (size_t)(cap - sStoreCap));
    sStoreCap = cap;
    return 1;
}

static int xCountOf(int partner) {
    return partner >= 0 && partner < sStoreCap ? sXCount[partner] : 0;
}

int pcPartnerEntryLimit(int partner) {
    return PARTNER_ENTRY_COUNT + xCountOf(partner);
}

PartnerEntry *pcPartnerEntryRef(PartnerStats *info, int partner, int index) {
    if (index >= 0 && index < PARTNER_ENTRY_COUNT) return &info->entries[index];
    if (index >= PARTNER_ENTRY_COUNT && index - PARTNER_ENTRY_COUNT < xCountOf(partner)) {
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

    if (partner < 0 || partner >= pcPartnerCount || !storesEnsure(pcPartnerCount) || pcPartnerEntryLimit(partner) >= PC_PARTNER_ENTRY_MAX) return -1;
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

    for (p = 0; p < sStoreCap; p++) {
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

/* One stored entry as an EXTP record (21 bytes and the names; byte 0 is `partnerByte`). Returns its length, or 0 when the entry cannot be
   written because its Digimon form has no name; writes it only when `o` is not NULL. */
static unsigned long encodeStoredEntry(const PartnerEntry *x, int partnerByte, unsigned char *o) {
    unsigned flags = 0;
    const char *form = NULL, *tech[6];
    unsigned long len = 21;
    int k;
    unsigned char *start = o;

    if (x->id >= PC_DIGIMON_FIRST_NEW_ID) {
        form = formNameOf(x->id);
        if (form == NULL) return 0;
        flags |= XP_NAMED_FORM;
        len += 2 + strlen(form);
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
    if (o == NULL) return len;
    o[0] = (unsigned char)partnerByte;
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
    return (unsigned long)(o - start);
}

/* Reads one record at *pos: 0 and the entry in *x (*ok = 0 when its form's mod is gone: the entry is dropped), or -1 when damaged. The
   byte 0 of the record is returned in *partnerByte. */
static int decodeStoredEntry(const unsigned char *in, unsigned long len, unsigned long *pos, PartnerEntry *x, int *ok, int *partnerByte,
                             const char *whose) {
    unsigned flags;
    int k, id;
    char name[256];

    if (*pos + 21 > len) return -1;
    *partnerByte = in[*pos];
    flags = in[*pos + 1];
    memset(x, 0, sizeof *x);
    x->level = (s8)in[*pos + 2];
    x->exp = (s32)contentGet32(in + *pos + 3);
    id = (int)contentGet16(in + *pos + 7);
    for (k = 0; k < 6; k++) x->skills[k] = (s16)contentGet16(in + *pos + 9 + 2 * k);
    *pos += 21;
    *ok = 1;
    if (flags & XP_NAMED_FORM) {
        unsigned long nl = contentReadName(in, *pos, len, 0, name, sizeof name);

        if (nl == 0) return -1;
        *pos += 2 + nl;
        id = sDigIdOf != NULL ? sDigIdOf(name) : -1;
        if (id < PC_DIGIMON_FIRST_NEW_ID) {
            plogLine("saves", "Digimon form \"%s\" is not defined by any installed mod; its stored entry of %s is dropped\n", name, whose);
            *ok = 0;
        }
    }
    for (k = 0; k < 6; k++) {
        if (flags & (1u << k)) {
            unsigned long nl = contentReadName(in, *pos, len, 0, name, sizeof name);
            int t;

            if (nl == 0) return -1;
            *pos += 2 + nl;
            t = sTechIdOf != NULL ? sTechIdOf(name) : -1;
            if (t >= PC_TECH_FIRST_EXT_ID) x->skills[k] = (s16)(((unsigned)x->skills[k] & ~(unsigned)SKILL_ID) | (unsigned)t);
            else x->skills[k] = 0; /* the technique's mod is gone: the slot reads empty */
        }
    }
    x->id = (s16)id;
    return 0;
}

unsigned long pcExtStateEncodeEntries(unsigned char *out, unsigned long cap) {
    unsigned long need = 8, entries = 0, pos;
    int p, e, pass, unnamed = 0;

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
        for (p = 0; p < sStoreCap && p < PARTNER_COUNT; p++) { /* EXTP v1 names partners by index: only the original eight, whose index is stable */
            for (e = 0; e < sXCount[p]; e++) {
                const PartnerEntry *x = &sXEntries[p][e];
                unsigned long len;

                if (x->id == 0) continue;
                len = encodeStoredEntry(x, p, NULL);
                if (len == 0) {
                    if (pass == 0) unnamed++;
                    continue;
                }
                if (pass == 0) {
                    need += len;
                    entries++;
                } else {
                    pos += encodeStoredEntry(x, p, out + pos);
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
        int p, ok, slot;
        char whose[40];

        if (pos + 21 > len) goto bad;
        p = in[pos];
        if (p >= PARTNER_COUNT) goto bad;
        snprintf(whose, sizeof whose, "partner %d", p);
        if (decodeStoredEntry(in, len, &pos, &x, &ok, &p, whose) != 0) goto bad;
        if (!ok) { dropped++; continue; }
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

/* ---- dw3save-v1 "PTNR" section: every partner, by name (ADR-014) ----
 * Written when the port has more than the original eight (a save of the eight alone has none: it is the same bytes as before and the
 * original indices are stable). All little endian:
 *   u32 version (1), u32 partner count,
 *   u32 length + an EXTD-format chunk (the names of the new Digimon forms the records hold), u32 length + an EXTT-format chunk (the
 *     new techniques'),
 *   the party: three names (u16 length + bytes; length 0: an empty slot),
 *   per partner: its name, u32 record length (sizeof(Partner), the GAME block's layout) + the record, u32 count of stored entries
 *     (beyond the 44 of the record) + that many EXTP records (byte 0 unused).
 * The record is the partner whole (stats, the 44 entries, the picked forms, the battle form). Form and technique ids inside it
 * depend on the mods installed, so a load maps them through the two embedded chunks exactly as the GAME block's EXTD/EXTT do.
 * `unlocked` holds the partner's own index + 3, so a load writes it again from the index the name has now.
 * The first eight are written from the GAME block the caller holds (its mirror, authoritative for them as ever); the others from
 * the port's array. */

#define PTNR_VERSION 1

typedef struct Wr {
    unsigned char *p;
    unsigned long n, cap;
    int err;
} Wr;

static unsigned char *wrRoom(Wr *w, unsigned long more) {
    if (w->err) return NULL;
    if (w->n + more > w->cap) {
        unsigned long cap = w->cap != 0 ? w->cap : 4096;
        unsigned char *g;

        while (cap < w->n + more) cap *= 2;
        g = (unsigned char *)realloc(w->p, cap);
        if (g == NULL) { w->err = 1; return NULL; }
        w->p = g;
        w->cap = cap;
    }
    return w->p + w->n;
}

static void wrU32(Wr *w, unsigned long v) {
    unsigned char *o = wrRoom(w, 4);

    if (o != NULL) { contentPut32(o, v); w->n += 4; }
}

static void wrBytes(Wr *w, const void *b, unsigned long n) {
    unsigned char *o = wrRoom(w, n);

    if (o != NULL && n != 0) { memcpy(o, b, n); w->n += n; }
}

static void wrName(Wr *w, const char *name) {
    unsigned long n = name != NULL ? (unsigned long)strlen(name) : 0;
    unsigned char *o = wrRoom(w, 2 + n);

    if (o != NULL) { w->n += contentWriteName(o, name != NULL ? name : "", n) ; }
}

/* the party of a GAME block (three s32), as stored in it: host byte order, as the block is a struct copy */
static int blockParty(const unsigned char *game, int i) {
    int v;

    memcpy(&v, game + offsetof(GameState, party) + (size_t)i * sizeof(s32), sizeof v);
    return v;
}

unsigned long pcExtStateEncodePartners(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap) {
    Wr w;
    Partner *all;
    int n = pcPartnerCount, i, e, unnamed = 0;
    unsigned long dl, tl, size;
    unsigned char *tmp;

    if (n <= PARTNER_COUNT || game == NULL || gameLen < DIG_END || offsetof(GameState, party) + 3 * sizeof(s32) > gameLen) return 0;
    all = (Partner *)malloc(sizeof(Partner) * (size_t)n);
    if (all == NULL) return 0;
    memcpy(all, game + BLOCK_PARTNERS, sizeof(Partner) * PARTNER_COUNT);
    memcpy(all + PARTNER_COUNT, pcPartners + PARTNER_COUNT, sizeof(Partner) * (size_t)(n - PARTNER_COUNT));
    memset(&w, 0, sizeof w);
    wrU32(&w, PTNR_VERSION);
    wrU32(&w, (unsigned long)n);
    dl = encodeDigimonOf((const unsigned char *)all, n, NULL, 0);
    wrU32(&w, dl);
    if (dl != 0) {
        tmp = (unsigned char *)malloc(dl);
        if (tmp != NULL) { encodeDigimonOf((const unsigned char *)all, n, tmp, dl); wrBytes(&w, tmp, dl); } else w.err = 1;
        free(tmp);
    }
    tl = encodeTechsOf((const unsigned char *)all, n, NULL, 0);
    wrU32(&w, tl);
    if (tl != 0) {
        tmp = (unsigned char *)malloc(tl);
        if (tmp != NULL) { encodeTechsOf((const unsigned char *)all, n, tmp, tl); wrBytes(&w, tmp, tl); } else w.err = 1;
        free(tmp);
    }
    for (i = 0; i < PARTY_SIZE; i++) {
        int m = blockParty(game, i);

        wrName(&w, m >= 0 && m < n ? pcPartnerName(m) : NULL);
    }
    for (i = 0; i < n; i++) {
        unsigned long countPos, xcount = 0;

        wrName(&w, pcPartnerName(i));
        wrU32(&w, sizeof(Partner));
        wrBytes(&w, &all[i], sizeof(Partner));
        countPos = w.n;
        wrU32(&w, 0);
        for (e = 0; i < sStoreCap && e < sXCount[i]; e++) {
            const PartnerEntry *x = &sXEntries[i][e];
            unsigned long len;
            unsigned char *o;

            if (x->id == 0) continue;
            len = encodeStoredEntry(x, 0, NULL);
            if (len == 0) { unnamed++; continue; }
            o = wrRoom(&w, len);
            if (o != NULL) { encodeStoredEntry(x, 0, o); w.n += len; xcount++; }
        }
        if (w.p != NULL && !w.err) contentPut32(w.p + countPos, xcount);
    }
    free(all);
    if (unnamed != 0) plogLine("saves", "%d stored partner entr(ies) hold a Digimon form nobody names and were not saved\n", unnamed);
    if (w.err) { free(w.p); return 0; }
    size = w.n;
    if (out != NULL && cap >= size) memcpy(out, w.p, size);
    free(w.p);
    return size;
}

/* ---- what a load stages: the partners the section held, by their index now ---- */

static Partner *sStage;
static unsigned char *sStageHas;
static int sStageN;

static void stagedForget(void) {
    free(sStage);
    free(sStageHas);
    sStage = NULL;
    sStageHas = NULL;
    sStageN = 0;
}

void pcPartnersStageClear(void) {
    stagedForget();
}

static void stagedApply(void) {
    int i;

    for (i = 0; i < sStageN && i < pcPartnerCount; i++) {
        if (sStageHas[i]) pcPartners[i] = sStage[i];
    }
    stagedForget();
}

/* Opens a PTNR chunk: 0, with the embedded chunks and the position of the party names, or -1 */
typedef struct PtnrHead {
    unsigned long count, digLen, techLen, partyPos, partnersPos;
    const unsigned char *dig, *tech;
} PtnrHead;

static int ptnrOpen(const unsigned char *in, unsigned long len, PtnrHead *h) {
    unsigned long pos = 8, i;
    char name[PC_PARTNER_NAME_MAX + 8];

    if (in == NULL || len < 16 || contentGet32(in) != PTNR_VERSION) return -1;
    h->count = contentGet32(in + 4);
    h->digLen = contentGet32(in + pos);
    pos += 4;
    if (h->digLen > len - pos) return -1;
    h->dig = in + pos;
    pos += h->digLen;
    if (pos + 4 > len) return -1;
    h->techLen = contentGet32(in + pos);
    pos += 4;
    if (h->techLen > len - pos) return -1;
    h->tech = in + pos;
    pos += h->techLen;
    h->partyPos = pos;
    for (i = 0; i < PARTY_SIZE; i++) {
        unsigned long n;

        if (pos + 2 > len) return -1;
        n = contentGet16(in + pos);
        if (n == 0) { pos += 2; continue; }
        n = contentReadName(in, pos, len, 0, name, sizeof name);
        if (n == 0) return -1;
        pos += 2 + n;
    }
    h->partnersPos = pos;
    return 0;
}

int pcExtStateDecodePartners(const unsigned char *in, unsigned long len) {
    PtnrHead h;
    unsigned long pos, i;
    int dropped = 0, ok = 0, r;
    char name[PC_PARTNER_NAME_MAX + 8];

    stagedForget();
    if (in == NULL || len == 0) return 0;
    pcPartnerEntriesReset();
    if (ptnrOpen(in, len, &h) != 0 || h.count > PC_PARTNER_LIMIT * 4UL) goto bad;
    sStage = (Partner *)calloc((size_t)pcPartnerCount, sizeof(Partner));
    sStageHas = (unsigned char *)calloc((size_t)pcPartnerCount, 1);
    sStageN = pcPartnerCount;
    if (sStage == NULL || sStageHas == NULL) goto bad;
    pos = h.partnersPos;
    for (i = 0; i < h.count; i++) {
        unsigned long nl, xc, k;
        int idx;
        Partner rec;
        char whose[PC_PARTNER_NAME_MAX + 16];

        nl = contentReadName(in, pos, len, 0, name, sizeof name);
        if (nl == 0) goto bad;
        pos += 2 + nl;
        if (pos + 4 > len || contentGet32(in + pos) != sizeof(Partner) || pos + 4 + sizeof(Partner) + 4 > len) goto bad;
        memcpy(&rec, in + pos + 4, sizeof rec);
        pos += 4 + sizeof(Partner);
        xc = contentGet32(in + pos);
        pos += 4;
        idx = pcPartnerFind(name);
        if (idx < 0) {
            plogLine("saves", "partner \"%s\" is not defined by any installed mod; its data is dropped from the loaded save\n", name);
            dropped++;
        } else {
            sStage[idx] = rec;
            sStageHas[idx] = 1;
        }
        snprintf(whose, sizeof whose, "partner \"%s\"", name);
        for (k = 0; k < xc; k++) {
            PartnerEntry x;
            int fine, ignored, slot;

            if (decodeStoredEntry(in, len, &pos, &x, &fine, &ignored, whose) != 0) goto bad;
            if (idx < 0 || !fine || x.id == 0) continue;
            slot = pcPartnerEntryGrow(idx);
            if (slot < 0) { plogLine("saves", "%s holds more entries than the port keeps (%d); the rest are dropped\n", whose, PC_PARTNER_ENTRY_MAX); continue; }
            sXEntries[idx][slot - PARTNER_ENTRY_COUNT] = x;
        }
    }
    /* the forms and techniques of the records: the ids the names have now */
    r = remapDigimonOf((unsigned char *)sStage, sStageN, h.dig, h.digLen, NULL);
    if (r < 0) goto bad;
    r = remapTechsOf((unsigned char *)sStage, sStageN, h.tech, h.techLen, NULL);
    if (r < 0) goto bad;
    for (i = 0; i < (unsigned long)sStageN; i++) {
        if (sStageHas[i] && sStage[i].unlocked != 0) sStage[i].unlocked = (s32)i + 3; /* the partner's own index + 3 */
        ok += sStageHas[i];
    }
    (void)ok;
    return dropped;
bad:
    stagedForget();
    pcPartnerEntriesReset();
    plogLine("saves", "the partners section of the save is damaged; the partners it held are dropped (the game block's eight are used)\n");
    return -1;
}

/* The party of a loaded GAME block from the names the section holds (no chunk: the block is left as it is), then made valid: every
   slot is a partner that exists or -1, the members packed to the front, and a party left empty gets the first original partner
   the block has unlocked (the code that reads the party takes -1 as "no member", GAME.party[1] and [2] being -1 in a one-member
   party: src/engine/game/party.c:15-17, src/menus/digimon_lab/lab.c:41-60). Returns the members that were lost. */
int pcExtStateRemapParty(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len) {
    PtnrHead h;
    int party[PARTY_SIZE], out[PARTY_SIZE], i, n = 0, lost = 0, changed = 0, havePtnr;
    unsigned long pos;

    if (game == NULL || gameLen < DIG_END || offsetof(GameState, party) + sizeof party > gameLen) return 0;
    havePtnr = chunk != NULL && len != 0;
    if (!havePtnr) return 0; /* a block of the original game, or of this port with the eight: its party is indices of the eight, valid as it is */
    if (ptnrOpen(chunk, len, &h) != 0) {
        plogLine("saves", "the partners section of the save is damaged; the party stays as the game block has it\n");
        havePtnr = 0;
    }
    for (i = 0; i < PARTY_SIZE; i++) party[i] = blockParty(game, i);
    pos = havePtnr ? h.partyPos : 0;
    for (i = 0; i < PARTY_SIZE; i++) {
        int v = party[i];

        if (havePtnr) {
            char name[PC_PARTNER_NAME_MAX + 8];
            unsigned long nl = contentGet16(chunk + pos);

            if (nl == 0) { v = -1; pos += 2; }
            else {
                nl = contentReadName(chunk, pos, len, 0, name, sizeof name);
                pos += 2 + nl;
                v = pcPartnerFind(name);
                if (v < 0) {
                    plogLine("saves", "party member \"%s\" is not defined by any installed mod; the slot is emptied\n", name);
                    lost++;
                }
            }
        } else if (v < -1 || v >= pcPartnerCount) {
            plogLine("saves", "party slot %d holds partner %d, which does not exist; the slot is emptied\n", i, v);
            lost++;
        }
        out[i] = v < -1 || v >= pcPartnerCount ? -1 : v;
    }
    for (i = 0; i < PARTY_SIZE; i++) {
        if (out[i] >= 0) party[n++] = out[i];
    }
    if (n == 0) {
        int first = 0;

        for (i = 0; i < PARTNER_COUNT; i++) {
            if (((const Partner *)(game + BLOCK_PARTNERS))[i].unlocked != 0) { first = i; break; }
        }
        party[n++] = first;
        plogLine("saves", "the party has no member left; partner %d takes its place\n", first);
    }
    while (n < PARTY_SIZE) party[n++] = -1;
    for (i = 0; i < PARTY_SIZE; i++) {
        if (party[i] != blockParty(game, i)) changed = 1;
    }
    if (changed) {
        memcpy(game + offsetof(GameState, party), party, sizeof party);
        fixChecksum(game, gameLen);
    }
    return lost;
}

#define TEAM_VERSION 1

static int blockPartySet(const unsigned char *game) {
    int v;

    memcpy(&v, game + offsetof(GameState, partySet), sizeof v);
    return v;
}

unsigned long pcExtStateEncodeTeam(const unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap) {
    Wr w;
    const char *name;
    int set;
    unsigned long size;

    if (game == NULL || gameLen < offsetof(GameState, partySet) + sizeof(s32)) return 0;
    set = blockPartySet(game);
    name = set >= PC_STARTER_ORIG_COUNT ? pcStarterPartyName(set) : NULL;
    if (name == NULL) return 0; /* an original team (its number is stable), none, or a number no team has */
    memset(&w, 0, sizeof w);
    wrU32(&w, TEAM_VERSION);
    wrName(&w, name);
    if (w.err) { free(w.p); return 0; }
    size = w.n;
    if (out != NULL && cap >= size) memcpy(out, w.p, size);
    free(w.p);
    return size;
}

int pcExtStateRemapTeam(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len) {
    int old, now;

    if (game == NULL || gameLen < offsetof(GameState, partySet) + sizeof(s32)) return 0;
    old = now = blockPartySet(game);
    if (chunk != NULL && len != 0) {
        char name[PC_PARTNER_NAME_MAX + 8];
        unsigned long nl = len >= 8 && contentGet32(chunk) == TEAM_VERSION && contentGet16(chunk + 4) != 0 ? contentReadName(chunk, 4, len, 0, name, sizeof name) : 0;

        if (nl == 0) {
            plogLine("saves", "the team section of the save is damaged; the game keeps no starter team\n");
            now = -1;
        } else {
            now = pcStarterPartyFind(name);
            if (now < 0) plogLine("saves", "starter team \"%s\" is not defined by any installed mod; the game keeps no starter team\n", name);
        }
    }
    if (now == old) return 0;
    memcpy(game + offsetof(GameState, partySet), &now, sizeof now);
    fixChecksum(game, gameLen);
    return 1;
}

/* ---- dw3save-v1 "ICON" section: the partners of the card screen's summaries, by name (ADR-014) ----
 * The info section (MemCardFile) holds, for each of the three slots, the partners the party had as `unlocked` = index + 3 (the card
 * screen draws their art from it). An index of the eight is stable; a higher one is not, so the section names it:
 * u32 version (1), u32 count, per entry u8 slot, u8 position, u8 stand-in (the original partner whose art stood in for it), name. */

/* the layout of MemCardFile / MemCardSave (menus/memory_card.h, which a shim cannot include): pc/boot.c asserts these numbers */
#define ICON_AT(k, j) ((size_t)PC_INFO_SAVES_OFS + (size_t)(k) * PC_INFO_SAVE_SIZE + PC_INFO_PARTNERS_OFS + (size_t)(j) * sizeof(s32))

static int iconValue(const unsigned char *info, int k, int j) {
    int v;

    memcpy(&v, info + ICON_AT(k, j), sizeof v);
    return v;
}

unsigned long pcExtStateEncodeIcons(const unsigned char *info, unsigned long infoLen, unsigned char *out, unsigned long cap) {
    Wr w;
    int k, j;
    unsigned long count = 0, size;

    if (info == NULL || infoLen < PC_INFO_USED) return 0;
    memset(&w, 0, sizeof w);
    wrU32(&w, 1);
    wrU32(&w, 0);
    for (k = 0; k < 3; k++) {
        for (j = 0; j < 3; j++) {
            int v = iconValue(info, k, j) - 3;
            const char *name = v >= PARTNER_COUNT ? pcPartnerName(v) : NULL;
            unsigned char *o;

            if (name == NULL) continue;
            o = wrRoom(&w, 3);
            if (o != NULL) { o[0] = (unsigned char)k; o[1] = (unsigned char)j; o[2] = (unsigned char)pcPartnerIconBase(v); w.n += 3; }
            wrName(&w, name);
            count++;
        }
    }
    if (count == 0 || w.err) { free(w.p); return 0; }
    contentPut32(w.p + 4, count);
    size = w.n;
    if (out != NULL && cap >= size) memcpy(out, w.p, size);
    free(w.p);
    return size;
}

int pcExtStateRemapIcons(unsigned char *info, unsigned long infoLen, const unsigned char *chunk, unsigned long len) {
    int k, j, changed = 0, fallbacks = 0;
    unsigned long count = 0, pos = 8, i;
    int valid = chunk != NULL && len != 0 && contentChunkOpen(chunk, len, &count) == 0;
    struct { int k, j, base, now; } e[9];
    int n = 0;

    if (info == NULL || infoLen < PC_INFO_USED) return 0;
    for (i = 0; valid && i < count && n < 9; i++) {
        char name[PC_PARTNER_NAME_MAX + 8];
        unsigned long nl;

        if (pos + 3 > len) break;
        nl = contentReadName(chunk, pos + 3, len, 0, name, sizeof name);
        if (nl == 0) break;
        e[n].k = chunk[pos];
        e[n].j = chunk[pos + 1];
        e[n].base = chunk[pos + 2] < PARTNER_COUNT ? chunk[pos + 2] : 0;
        e[n].now = pcPartnerFind(name);
        n++;
        pos += 3 + 2 + nl;
    }
    for (k = 0; k < 3; k++) {
        for (j = 0; j < 3; j++) {
            int v = iconValue(info, k, j), want = v, m;

            if (v - 3 < PARTNER_COUNT) continue; /* empty (below 3) or one of the eight: stable */
            for (m = 0; m < n && !(e[m].k == k && e[m].j == j); m++) {}
            if (m >= n) continue; /* the section does not name it (an info not written by this port): left as it is; PC_PARTNER_ART guards the art tables */
            if (e[m].now >= 0) want = e[m].now + 3;
            else { want = e[m].base + 3; fallbacks++; } /* its mod is gone: the stand-in's art */
            if (want != v) {
                memcpy(info + ICON_AT(k, j), &want, sizeof want);
                changed = 1;
            }
        }
    }
    if (changed) {
        unsigned char x = 0;
        size_t b;

        for (b = PC_INFO_MAGIC_OFS; b < PC_INFO_USED; b++) x ^= info[b]; /* STGMCARD_fillSave's checksum of the rest */
        info[0] = x;
    }
    if (fallbacks != 0) plogLine("saves", "%d partner icon(s) of the card screen name a partner no installed mod defines; the stand-in's art is shown\n", fallbacks);
    return fallbacks;
}

/* ---- the evolution lists (extended Digimon P3) ----
 * STFGTREP_learnDigimon reads a partner's list whole. The port's list of an original partner is its 44 entries (copied; the original
 * array is never written) and then the entries content mods declare, in order. A partner a mod adds has no original list: only the
 * entries its mod declares (none until it does). The stores are per partner (sEvo, sEvoExtra: see storesEnsure). */

static const PcEvolution sEvoNone[1]; /* the list of a partner that has none: never read, its count is 0 */
static const PcEvolution (*sEvoOrig)[PC_EVOLUTION_ORIG_COUNT];

static int evoBase(int partner) {
    return partner < PARTNER_COUNT ? PC_EVOLUTION_ORIG_COUNT : 0;
}

int pcEvolutionCount(int partner) {
    if (partner < 0) return 0;
    return evoBase(partner) + (partner < sStoreCap ? sEvoExtra[partner] : 0);
}

void pcEvolutionSetOriginal(const void *lists) {
    sEvoOrig = lists;
}

void *pcEvolutionList(int partner, void *original) {
    if (partner >= 0 && partner < sStoreCap && sEvo[partner] != NULL) return sEvo[partner];
    return partner >= 0 && partner < PARTNER_COUNT ? original : (void *)sEvoNone;
}

int pcEvolutionExtend(int partner, const PcEvolution *entry) {
    PcEvolution *grown;
    int base;

    if (partner < 0 || partner >= pcPartnerCount || !storesEnsure(pcPartnerCount) || entry == NULL) return -1;
    base = evoBase(partner);
    if (base != 0 && sEvoOrig == NULL) return -1;
    grown = (PcEvolution *)realloc(sEvo[partner], sizeof(PcEvolution) * (size_t)(base + sEvoExtra[partner] + 1));
    if (grown == NULL) return -1;
    if (sEvo[partner] == NULL && base != 0) memcpy(grown, sEvoOrig[partner], sizeof(PcEvolution) * PC_EVOLUTION_ORIG_COUNT);
    sEvo[partner] = grown;
    grown[base + sEvoExtra[partner]] = *entry;
    return base + sEvoExtra[partner]++;
}

void pcEvolutionReset(void) {
    int p;

    for (p = 0; p < sStoreCap; p++) {
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

static int sCardOpponent = -1; /* the opponent of the card battle in progress, -1 none */

void pcCardBattleBegin(int opponent) {
    sCardOpponent = opponent >= 0 && opponent <= 0x1FF ? opponent : -1;
}

void pcCardBattleEnd(int won) {
    char name[64];
    int opponent = sCardOpponent;

    sCardOpponent = -1;
    if (opponent < 0) return;
    snprintf(name, sizeof name, "dw3:card-played/%x", opponent);
    pcExtVarSet(name, 1);
    if (won) {
        snprintf(name, sizeof name, "dw3:card-won/%x", opponent);
        pcExtVarSet(name, 1);
    }
    plogLine("state", "card battle against opponent 0x%X ended: %s\n", opponent, won ? "won" : "lost");
}

/* The result of a battle a talk or an event started (`start-battle`), by encounter: `key` is the name of a mod's encounter with the colon turned into a slash
   ("training-field/duel-trio") or the row in lower-case hex ("1a"). A won battle comes back through the report (MODE_BATTLE_REPORT), a fled one straight from
   the battle; a lost one goes to the title screen, so it leaves nothing to record. */
static char sBattleKey[128];

void pcFieldBattleBegin(const char *key) {
    snprintf(sBattleKey, sizeof sBattleKey, "%s", key ? key : "");
}

void pcFieldBattleEnd(int won) {
    char name[192];

    if (!sBattleKey[0]) return;
    snprintf(name, sizeof name, "dw3:battle-played/%s", sBattleKey);
    pcExtVarSet(name, 1);
    if (won) {
        snprintf(name, sizeof name, "dw3:battle-won/%s", sBattleKey);
        pcExtVarSet(name, 1);
    }
    plogLine("state", "field battle %s ended: %s\n", sBattleKey, won ? "won" : "left");
    sBattleKey[0] = 0;
}

void pcExtVarsReset(void) {
    int i;

    sBattleKey[0] = 0; /* a new game or a loaded one: the battle in progress (if any) is not theirs */

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

int pcLevelExpToExceed(int level, int expRate, const int *tierExp) {
    int tier;

    if (tierExp == NULL || level < 2 || level > 99) return -1;
    tier = level < 5 ? 0 : level < 20 ? 1 : level < 40 ? 2 : 3;
    return (level * level * level + level * 5 - 6) * expRate / 10 + tierExp[tier];
}
