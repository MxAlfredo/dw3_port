/*
 * The item type of the content core: see item_ext.h and content.h.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "content.h"
#include "engine/game_state.h"
#include "ext_state.h"
#include "item_ext.h"
#include "port_log.h"
#include "text_data.h"

/* the record of a new item: the base item's ItemInfo with the overrides, and its own copy of the effect or of the
   equipment record (the tables weapon_data, armor_data, accessory_data and item_effects change those copies, never
   the base item's) */
typedef struct ItemRec {
    ItemInfo info;
    ItemEffect effect;
    ItemData equip;
} ItemRec;

static int copyBase(int baseId, void *rec);
static int takeField(const ContentEntry *e, const char *k, const ExtJson *v);
static int publish(ContentSet *s);
static int describe(int id, char *out, size_t cap);

static const ItemInfo *extGetC(int id);

static const ContentType kItem = {
    "item", "item", "items", "items",
    1, PC_ITEM_ORIG_COUNT - 1, PC_ITEM_ORIG_COUNT, PC_EXT_ITEM_LIMIT - PC_ITEM_ORIG_COUNT, NULL, sizeof(ItemRec),
    copyBase, takeField, NULL, publish,
    itemExtNameOf, itemExtIdOf, itemExtCount, itemExtText,
    pcExtStateSetNameHooks, tdSetItemTextHook,
    "price and sellPrice (whole numbers 0..65535); effect {amount 0..65535, kind 1..17, flags 0..7}, only for a usable item; "
    "name and description per language (eng, fra, ita, ger, spa, es-419)",
    describe,
};

static ContentSet sSet = {&kItem, NULL, 0};

/* the game's own functions, when redirected */
static ItemFuncs sOrig;
static int *sBases; /* per new item: its base id (pcItemBase) */
static int sInstalled;

static int copyBase(int baseId, void *rec) {
    ItemRec *r = rec;
    const ItemInfo *base = sOrig.get((s32)baseId);

    if (base == NULL) return 0;
    r->info = *base;
    if (base->kind == ITEM_KIND_USABLE && base->data.effect != NULL) r->effect = *base->data.effect;
    if (base->data.record != NULL) {
        if (base->kind == ITEM_KIND_WEAPON) r->equip.weapon = *base->data.weapon;
        else if (base->kind == ITEM_KIND_ARMOR) r->equip.armor = *base->data.armor;
        else if (base->kind == ITEM_KIND_ACCESSORY) r->equip.acc = *base->data.acc;
    }
    return 1;
}

static int takeField(const ContentEntry *e, const char *k, const ExtJson *v) {
    ItemRec *it = e->rec;
    const char *path = e->path, *full = e->name;

    if (!strcmp(k, "price") || !strcmp(k, "sellPrice")) {
        if (v->t != 0 || !extWhole(v->num, 0, 65535)) {
            plogLine("items", "%s item \"%s\" field \"%s\" rejected: a whole number 0..65535\n", path, full, k);
        } else if (k[0] == 'p') {
            it->info.price = (u16)v->num;
        } else {
            it->info.sellPrice = (u16)v->num;
        }
    } else if (!strcmp(k, "effect")) {
        int j;
        if (it->info.kind != ITEM_KIND_USABLE || v->t != 2) {
            plogLine("items", "%s item \"%s\" field \"effect\" rejected: only for a usable base item, as an object\n", path, full);
            return 1;
        }
        for (j = 0; j < v->n; j++) {
            const char *ek = v->keys[j];
            const ExtJson *ev = &v->v[j];
            long lo = 0, hi = 65535;
            if (!strcmp(ek, "kind")) { lo = 1; hi = 17; }
            else if (!strcmp(ek, "flags")) hi = 7;
            else if (strcmp(ek, "amount")) {
                plogLine("items", "%s item \"%s\" effect field \"%s\" rejected: unknown (amount, kind, flags)\n", path, full, ek);
                continue;
            }
            if (ev->t != 0 || !extWhole(ev->num, lo, hi)) {
                plogLine("items", "%s item \"%s\" effect field \"%s\" rejected: a whole number %ld..%ld\n", path, full, ek, lo, hi);
            } else if (ek[0] == 'k') it->effect.kind = (u8)ev->num;
            else if (ek[0] == 'f') it->effect.flags = (u8)ev->num;
            else it->effect.amount = (u16)ev->num;
        }
    } else {
        return 0;
    }
    return 1;
}

/* the settable fields of an original item (the reference export) */
static int describe(int id, char *out, size_t cap) {
    const ItemInfo *it = sOrig.get != NULL ? sOrig.get(id) : ITEM_FUNCS.get(id);
    int n;

    if (it == NULL) return 0;
    n = snprintf(out, cap, "\"price\": %d, \"sellPrice\": %d", (int)it->price, (int)it->sellPrice);
    if (it->kind == ITEM_KIND_USABLE && it->data.effect != NULL && n > 0 && (size_t)n < cap) {
        snprintf(out + n, cap - (size_t)n, ", \"effect\": {\"amount\": %d, \"kind\": %d, \"flags\": %d}", (int)it->data.effect->amount,
                 (int)it->data.effect->kind, (int)it->data.effect->flags);
    }
    return 1;
}

const ContentType *itemExtType(void) {
    return &kItem;
}

static ItemInfo *extGet(s32 id) {
    if (id >= PC_ITEM_ORIG_COUNT) {
        const ItemInfo *i = extGetC(id);
        return (ItemInfo *)(i != NULL ? i : sOrig.get(1));
    }
    return sOrig.get(id);
}

static s32 extCategory(s32 id) {
    if (id >= PC_ITEM_ORIG_COUNT) return ITEM_TYPE_CATEGORIES[extGet(id)->type & 31];
    return sOrig.getCategory(id);
}

static s32 extIsKind(s32 id, s32 kind) {
    if (id >= PC_ITEM_ORIG_COUNT) return extGet(id)->kind == kind;
    return sOrig.isKind(id, kind);
}

static s32 extList(s32 type, s16 *out) {
    s32 n = sOrig.list(type, out), index = type & 0x7FFFFFFF;
    int all = (type >> 31) != 0, i;
    if (index >= 5) return n;
    for (i = 0; i < contentCount(&sSet) && n < 0x193; i++) {
        int id = PC_ITEM_ORIG_COUNT + i;
        if (((const ItemRec *)contentRec(&sSet, i))->info.kind == index + 1 && (PC_ITEM_COUNT(id) != 0 || (all && PC_ITEM_EQUIPPED(id) != 0))) out[n++] = (s16)id;
    }
    return n;
}

static const ItemInfo *extGetC(int id) {
    const ItemRec *r = contentRec(&sSet, id - PC_ITEM_ORIG_COUNT);
    return r != NULL ? &r->info : NULL;
}

static int publish(ContentSet *s) {
    int i;

    for (i = 0; i < s->n; i++) {
        ItemRec *r = s->e[i].rec;
        if (r->info.kind == ITEM_KIND_USABLE && r->info.data.effect != NULL) r->info.data.effect = &r->effect;
        else if (r->info.kind >= ITEM_KIND_WEAPON && r->info.kind <= ITEM_KIND_ACCESSORY && r->info.data.record != NULL) {
            r->info.data.record = &r->equip;
        }
    }
    free(sBases);
    sBases = malloc(sizeof(int) * (size_t)(s->n > 0 ? s->n : 1));
    if (sBases != NULL) {
        for (i = 0; i < s->n; i++) sBases[i] = s->e[i].baseId;
        pcExtStateSetItemBases(sBases, s->n);
    }
    ITEM_FUNCS.get = extGet;
    ITEM_FUNCS.getCategory = extCategory;
    ITEM_FUNCS.isKind = extIsKind;
    ITEM_FUNCS.list = extList;
    sInstalled = 1;
    contentAttach(s);
    return 1;
}

void itemExtReset(void) {
    pcExtStateSetItemBases(NULL, 0);
    free(sBases);
    sBases = NULL;
    contentUnload(&sSet);
    if (sInstalled) {
        ITEM_FUNCS = sOrig;
        sInstalled = 0;
    }
}

int itemExtLoad(void) {
    itemExtReset();
    sOrig = ITEM_FUNCS;
    return contentLoad(&sSet);
}

int itemExtCount(void) {
    return contentCount(&sSet);
}

int itemExtIdOf(const char *name) {
    return contentIdOf(&sSet, name);
}

const char *itemExtNameOf(int id) {
    return contentNameOf(&sSet, id);
}

const ItemInfo *itemExtInfo(int id) {
    return extGetC(id);
}

const char *itemExtText(int index, const char *langDir, int desc) {
    return contentText(&sSet, index, langDir, desc);
}

long long itemExtByName(const char *name) {
    return contentResolve(&sSet, name);
}

int itemExtIdOk(long long id) {
    return id < PC_ITEM_ORIG_COUNT || (id >= PC_ITEM_ORIG_COUNT && id - PC_ITEM_ORIG_COUNT < contentCount(&sSet));
}

void *itemExtRow(long long id) {
    return (void *)extGetC((int)id);
}

/* The own record of a new item of that kind (the tables' extRow), or NULL */
static void *extRecordOf(long long id, int kind) {
    const ItemRec *r = id >= PC_ITEM_ORIG_COUNT ? contentRec(&sSet, (int)id - PC_ITEM_ORIG_COUNT) : NULL;

    if (r == NULL || r->info.kind != kind || r->info.data.record == NULL) return NULL;
    return kind == ITEM_KIND_USABLE ? (void *)&r->effect : (void *)&r->equip;
}

void *itemExtWeaponRow(long long id) {
    return extRecordOf(id, ITEM_KIND_WEAPON);
}

void *itemExtArmorRow(long long id) {
    return extRecordOf(id, ITEM_KIND_ARMOR);
}

void *itemExtAccessoryRow(long long id) {
    return extRecordOf(id, ITEM_KIND_ACCESSORY);
}

void *itemExtEffectRow(long long id) {
    return extRecordOf(id, ITEM_KIND_USABLE);
}
