/*
 * The card type of the content core: see card_ext.h and content.h.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card_ext.h"
#include "content.h"
#include "engine/game_state.h"
#include "ext_state.h"
#include "mods.h"
#include "png_read.h"
#include "port_log.h"
#include "text_data.h"

/* The layout of CardEffect (cardgame/card_battle/battle.h, which redeclares libc's string functions, so a shim cannot include it);
   CardImage comes with engine/game_state.h */
typedef struct CardEffect {
    u8 condition, playCondition, message, target;
    u8 script[40];
} CardEffect;

_Static_assert(sizeof(CardImage) == CARD_EXT_IMAGE_SIZE, "CardImage is 12 + 0x620 bytes");
_Static_assert(sizeof(CardEffect) == CARD_EXT_EFFECT_SIZE, "CardEffect is 4 + 40 bytes");

#define CARD_ORIG_MAX 314
#define EFFECT_ORIG_MAX 60
#define STEP_MAX 174

/* the record of a new card: the image (stats and TIM) of its base with the overrides, and its own effect */
typedef struct CardRec {
    CardImage img;
    CardEffect effect;
    int hasEffect;
    int price; /* what the card shop charges (0..32767); a card the shops sell with no price is 1, as for the disc's cards */
} CardRec;

static int copyBase(int baseId, void *rec);
static int takeField(const ContentEntry *e, const char *k, const ExtJson *v);
static int publish(ContentSet *s);
static int describe(int id, char *out, size_t cap);

static const ContentType kCard = {
    "card", "card", "cards", "cards",
    1, CARD_ORIG_MAX, PC_CARD_ORIG_COUNT, PC_CARD_LIST_MAX - PC_CARD_ORIG_COUNT, "%d cards declared, only %d fit; the rest (alphabetical) are dropped\n",
    sizeof(CardRec),
    copyBase, takeField, NULL, publish,
    cardExtNameOf, cardExtIdOf, cardExtCount, cardExtText,
    pcExtStateSetCardHooks, tdSetCardTextHook,
    "color 1..6, price 0..32767 (the card shop; default 1), ap and hp 0..99, kind 0..16 (1..14 option cards, 16 Digimon), points 0..99, level 0..9, rank 0..999, comboCard 0..314; "
    "effect {like 1..60, condition, playCondition, message, target 0..255, script [steps 1..174, at most 39]} for an option card; "
    "art in card/<key>.png (32x32, 256 colours at most); name and description per language (eng, fra, ita, ger, spa, es-419)",
    describe,
};

static ContentSet sSet = {&kCard, NULL, 0};
static CardExtBase sBase;
static int sHaveBase;

void cardExtSetBase(const CardExtBase *base) {
    if (base != NULL) {
        sBase = *base;
        sHaveBase = 1;
    } else {
        memset(&sBase, 0, sizeof sBase);
        sHaveBase = 0;
    }
}

static int copyBase(int baseId, void *rec) {
    CardRec *r = rec;

    r->price = 1;
    if (!sHaveBase || sBase.image == NULL || !sBase.image(baseId, (unsigned char *)&r->img)) {
        plogLine("cards", "card base %d is unavailable (the disc's CARDPAK file could not be read); the card is skipped\n", baseId);
        return 0;
    }
    if (baseId <= EFFECT_ORIG_MAX && sBase.effect != NULL && sBase.effect(baseId, (unsigned char *)&r->effect)) r->hasEffect = 1;
    return 1;
}

static int resolveCard(const ExtJson *v) {
    int id;

    if (v->t == 0) return extWhole(v->num, 1, EFFECT_ORIG_MAX) ? (int)v->num : -1;
    if (v->t == 1 && v->str != NULL) {
        id = contentOriginalId(&kCard, v->str);
        return id >= 1 && id <= EFFECT_ORIG_MAX ? id : -1;
    }
    return -1;
}

static void takeEffect(const ContentEntry *e, CardRec *c, const ExtJson *v) {
    int j;
    const ExtJson *like = extJsonGet(v, "like");

    if (v->t != 2) {
        plogLine("cards", "%s card \"%s\" field \"effect\" rejected: an object\n", e->path, e->name);
        return;
    }
    if (like != NULL) { /* first: the other fields override it whatever their order */
        int id = resolveCard(like);
        if (id < 0 || sBase.effect == NULL || !sBase.effect(id, (unsigned char *)&c->effect)) {
            plogLine("cards", "%s card \"%s\" effect \"like\" rejected: an original option card, 1..%d\n", e->path, e->name, EFFECT_ORIG_MAX);
        } else {
            c->hasEffect = 1;
        }
    }
    for (j = 0; j < v->n; j++) {
        const char *k = v->keys[j];
        const ExtJson *x = &v->v[j];

        if (!strcmp(k, "like")) continue;
        if (!strcmp(k, "script")) {
            int i, ok = x->t == 4 && x->n <= 39;
            for (i = 0; ok && i < x->n; i++) ok = x->v[i].t == 0 && extWhole(x->v[i].num, 1, STEP_MAX);
            if (!ok) {
                plogLine("cards", "%s card \"%s\" effect \"script\" rejected: a list of at most 39 steps, each 1..%d\n", e->path, e->name, STEP_MAX);
            } else {
                memset(c->effect.script, 0, sizeof c->effect.script);
                for (i = 0; i < x->n; i++) c->effect.script[i] = (u8)x->v[i].num;
                c->hasEffect = 1;
            }
        } else if (!strcmp(k, "condition") || !strcmp(k, "playCondition") || !strcmp(k, "message") || !strcmp(k, "target")) {
            if (x->t != 0 || !extWhole(x->num, 0, 255)) {
                plogLine("cards", "%s card \"%s\" effect field \"%s\" rejected: a whole number 0..255\n", e->path, e->name, k);
            } else {
                u8 b = (u8)x->num;
                if (k[0] == 'c') c->effect.condition = b;
                else if (k[0] == 'p') c->effect.playCondition = b;
                else if (k[0] == 'm') c->effect.message = b;
                else c->effect.target = b;
                c->hasEffect = 1;
            }
        } else {
            plogLine("cards", "%s card \"%s\" effect field \"%s\" rejected: unknown (like, condition, playCondition, message, target, script)\n",
                     e->path, e->name, k);
        }
    }
}

static int takeField(const ContentEntry *e, const char *k, const ExtJson *v) {
    CardRec *c = e->rec;
    long lo = 0, hi = 99;
    u8 *dst8 = NULL;

    if (!strcmp(k, "effect")) {
        takeEffect(e, c, v);
        return 1;
    }
    if (!strcmp(k, "color")) { lo = 1; hi = 6; dst8 = &c->img.color; }
    else if (!strcmp(k, "ap")) dst8 = &c->img.ap;
    else if (!strcmp(k, "hp")) dst8 = &c->img.hp;
    else if (!strcmp(k, "kind")) { hi = 16; dst8 = &c->img.kind; }
    else if (!strcmp(k, "points")) dst8 = &c->img.points;
    else if (!strcmp(k, "level")) { hi = 9; dst8 = &c->img.level; }
    else if (!strcmp(k, "price")) {
        if (v->t != 0 || !extWhole(v->num, 0, 32767)) {
            plogLine("cards", "%s card \"%s\" field \"price\" rejected: a whole number 0..32767\n", e->path, e->name);
        } else {
            c->price = (int)v->num;
        }
        return 1;
    } else if (!strcmp(k, "rank") || !strcmp(k, "comboCard")) {
        hi = k[0] == 'r' ? 999 : CARD_ORIG_MAX;
        if (v->t != 0 || !extWhole(v->num, 0, hi)) {
            plogLine("cards", "%s card \"%s\" field \"%s\" rejected: a whole number 0..%ld\n", e->path, e->name, k, hi);
        } else if (k[0] == 'r') {
            c->img.rank = (s16)v->num;
        } else {
            c->img.comboCard = (s16)v->num;
        }
        return 1;
    } else {
        return 0;
    }
    if (v->t != 0 || !extWhole(v->num, lo, hi)) plogLine("cards", "%s card \"%s\" field \"%s\" rejected: a whole number %ld..%ld\n", e->path, e->name, k, lo, hi);
    else *dst8 = (u8)v->num;
    return 1;
}

/* ---- the art ---- */

static void le32(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* the card's TIM (0x620 bytes: header, 256-colour CLUT block, 16x32 halfword image block) from an indexed 32x32 PNG; the colour rules are
   those of the other mod images (image_data.h): alpha 0 is transparent (0x0000), opaque black is written 0x8000 */
static int timFromPng(const PngImage *pi, unsigned char *tim) {
    int i;

    memset(tim, 0, 0x620);
    le32(tim, 0x10);
    le32(tim + 4, 9);
    le32(tim + 8, 0x20C);
    tim[17] = 1; /* x = 0, y = 0 (bytes 12-15), w = 256 */
    tim[18] = 1; /* h = 1 */
    for (i = 0; i < 256; i++) {
        unsigned c;
        int alpha = 255;

        if (i >= pi->nPal) c = 0;
        else {
            if (i < pi->nAlpha) alpha = pi->alpha[i];
            if (alpha == 0) c = 0;
            else {
                c = (unsigned)(pi->rgb[i * 3] >> 3) | (unsigned)(pi->rgb[i * 3 + 1] >> 3) << 5 | (unsigned)(pi->rgb[i * 3 + 2] >> 3) << 10;
                if (c == 0) c = 0x8000;
                else c |= 0x8000u;
            }
        }
        tim[8 + 12 + i * 2] = (unsigned char)c;
        tim[8 + 12 + i * 2 + 1] = (unsigned char)(c >> 8);
    }
    le32(tim + 8 + 0x20C, 0x40C);
    tim[8 + 0x20C + 8] = 16; /* w in halfwords */
    tim[8 + 0x20C + 10] = 32;
    memcpy(tim + 8 + 0x20C + 12, pi->px, 1024);
    return 0;
}

/* mods/<mod>/card/<key>.png, from the cards.json path "<dir>/content/cards.json" */
static void loadArt(ContentEntry *e) {
    CardRec *c = e->rec;
    char dir[1200], png[1300];
    const char *key = strchr(e->name, ':');
    size_t n = strlen(e->path), tail = strlen("/content/cards.json");
    unsigned char *data = NULL;
    size_t len = 0;
    PngImage pi;
    char why[160];

    if (key == NULL || n <= tail || n - tail >= sizeof dir) return;
    memcpy(dir, e->path, n - tail);
    dir[n - tail] = '\0';
    snprintf(png, sizeof png, "%s/card/%s.png", dir, key + 1);
    if (tdReadFile(png, &data, &len) != 0) return; /* no art: the base's */
    if (pngReadIndexed(data, len, &pi, why, sizeof why) != 0) {
        plogLine("cards", "%s card \"%s\" art rejected: %s (the base's art is used)\n", png, e->name, why);
    } else if (pi.w != 32 || pi.h != 32) {
        plogLine("cards", "%s card \"%s\" art rejected: %dx%d, a card is 32x32 (the base's art is used)\n", png, e->name, pi.w, pi.h);
    } else if (pi.nPal > 256) {
        plogLine("cards", "%s card \"%s\" art rejected: %d colors, at most 256 (the base's art is used)\n", png, e->name, pi.nPal);
    } else {
        timFromPng(&pi, c->img.tim);
    }
    if (pi.px != NULL) pngImageFree(&pi);
    free(data);
}

static int publish(ContentSet *s) {
    int i;

    for (i = 0; i < s->n; i++) loadArt(&s->e[i]);
    pcExtStateSetCardCount(s->n);
    contentAttach(s);
    return 1;
}

static int describe(int id, char *out, size_t cap) {
    unsigned char img[CARD_EXT_IMAGE_SIZE];
    const CardImage *ci = (const CardImage *)img;
    int n;

    if (!sHaveBase || sBase.image == NULL || !sBase.image(id, img)) return 0;
    n = snprintf(out, cap, "\"color\": %d, \"ap\": %d, \"hp\": %d, \"kind\": %d, \"points\": %d, \"level\": %d, \"rank\": %d, \"comboCard\": %d",
                 ci->color, ci->ap, ci->hp, ci->kind, ci->points, ci->level, ci->rank, ci->comboCard);
    if (id <= EFFECT_ORIG_MAX && n > 0 && (size_t)n < cap && sBase.effect != NULL) {
        unsigned char eb[CARD_EXT_EFFECT_SIZE];
        if (sBase.effect(id, eb)) {
            const CardEffect *ef = (const CardEffect *)eb;
            snprintf(out + n, cap - (size_t)n, ", \"effect\": {\"condition\": %d, \"playCondition\": %d, \"message\": %d, \"target\": %d}",
                     ef->condition, ef->playCondition, ef->message, ef->target);
        }
    }
    return 1;
}

const ContentType *cardExtType(void) {
    return &kCard;
}

void cardExtReset(void) {
    contentUnload(&sSet);
    pcExtStateSetCardCount(0);
}

int cardExtLoad(void) {
    cardExtReset();
    return contentLoad(&sSet);
}

int cardExtCount(void) {
    return contentCount(&sSet);
}

int cardExtIdOf(const char *name) {
    return contentIdOf(&sSet, name);
}

const char *cardExtNameOf(int id) {
    return contentNameOf(&sSet, id);
}

const char *cardExtText(int index, const char *langDir, int desc) {
    return contentText(&sSet, index, langDir, desc);
}

long long cardExtByName(const char *name) {
    return contentResolve(&sSet, name);
}

int cardExtIdOk(long long id) {
    return (id >= 1 && id <= CARD_ORIG_MAX) || (id >= PC_CARD_ORIG_COUNT && id - PC_CARD_ORIG_COUNT < contentCount(&sSet));
}

static const CardRec *recOf(int id) {
    return contentRec(&sSet, id - PC_CARD_ORIG_COUNT);
}

const unsigned char *cardExtImageBytes(int id) {
    const CardRec *r = recOf(id);
    return r != NULL ? (const unsigned char *)&r->img : NULL;
}

const unsigned char *cardExtEffectBytes(int id) {
    const CardRec *r = recOf(id);
    return r != NULL && r->hasEffect ? (const unsigned char *)&r->effect : NULL;
}

/* the game-side entry points (gfx.h, battle.h) */
s16 pcCardPrice(s32 id) {
    const CardRec *r = id >= PC_CARD_ORIG_COUNT ? recOf((int)id) : NULL;

    return r != NULL ? (s16)r->price : 1;
}

CardImage *pcCardImage(s32 id) {
    const CardRec *r = id >= PC_CARD_ORIG_COUNT ? recOf((int)id) : NULL;

    return r != NULL ? (CardImage *)&r->img : NULL;
}

const void *pcCardEffect(s32 id) {
    const CardRec *r = id >= PC_CARD_ORIG_COUNT ? recOf((int)id) : NULL;

    return r != NULL && r->hasEffect ? &r->effect : NULL;
}
