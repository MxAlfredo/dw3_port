/*
 * New partners and starter teams from content mods: see partner_ext.h.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "content.h"
#include "engine/game_state.h"
#include "ext_state.h"
#include "mods.h"
#include "partner_ext.h"
#include "port_log.h"
#include "text_data.h"

#define NAME_MAX_LEN 96
#define EVO_MAX 256       /* evolution entries one partner declares (the list itself has no fixed size) */
#define TEAM_ORIG_COUNT 3 /* STARTER_PARTIES */
#define PARTNER_NEW_MAX (PC_PARTNER_LIMIT - PARTNER_COUNT)
#define TEAM_NEW_MAX 250

static PartnerExtHooks sHooks;

void partnerExtSetHooks(const PartnerExtHooks *hooks) {
    if (hooks != NULL) sHooks = *hooks;
    else memset(&sHooks, 0, sizeof sHooks);
}

/* ---- partners ---- */

/* one declared way to learn a form: an Evolution of the partner's list with the Digimon still by name (they resolve in publish) */
typedef struct EvoDecl {
    char form[NAME_MAX_LEN];
    int nNeeds;
    char need[2][NAME_MAX_LEN];
    int needLevel[2];
    int stat;  /* 0 none, 1..6 the battle stats, 7 level, 8..14 resistances */
    int value; /* 0..32767 */
} EvoDecl;

typedef struct PartnerRec {
    int row;       /* DIGIMON_DATA row of the first form, -1 until "firstForm" resolved */
    int like;      /* the original partner whose bit of an item's mask it reads (the base unless "equip.like") */
    int *allow, *deny;
    int nAllow, nDeny;
    int hasSet;
    int setItem[4];
    int setBonus[6];
    EvoDecl *evo;
    int nEvo;
} PartnerRec;

static int partnerCopyBase(int baseId, void *rec);

static int partnerField(const ContentEntry *e, const char *key, const ExtJson *v);
static void partnerFree(void *rec);
static int partnerPublish(ContentSet *s);
static int partnerAccept(const ContentEntry *e);
static int partnerReserved(const char *key);
static const char *noName(int id);
static int noId(const char *name);
static int textCount(void);
static const char *textOf(int index, const char *langDir, int desc);
static void keepNameHooks(const char *(*nameOf)(int), int (*idOf)(const char *));
static void keepTextHook(int (*count)(void), const char *(*text)(int, const char *, int));

/* The registry's own hooks do the naming (pcPartnerFind / pcPartnerName) and the text is one hook for partners and teams together (set by
   partnerExtLoad), so the core's attach/detach calls do nothing here. */
static const ContentType kPartner = {
    "partner", "partner", "partners", "partners",
    0, PARTNER_COUNT - 1, PARTNER_COUNT, PARTNER_NEW_MAX, "%d partners declared, only %d fit; the rest (alphabetical) are dropped\n",
    sizeof(PartnerRec),
    partnerCopyBase, partnerField, partnerFree, partnerPublish,
    noName, noId, textCount, textOf, keepNameHooks, keepTextHook,
    NULL, NULL, partnerAccept, 0,
    "partners", "partner.json", "partners-partner-v1", partnerReserved,
};

static ContentSet sPartners = {&kPartner, NULL, 0};

static const char *noName(int id) {
    (void)id;
    return NULL;
}
static int noId(const char *name) {
    (void)name;
    return -1;
}
static void keepNameHooks(const char *(*nameOf)(int), int (*idOf)(const char *)) {
    (void)nameOf;
    (void)idOf;
}
static void keepTextHook(int (*count)(void), const char *(*text)(int, const char *, int)) {
    (void)count;
    (void)text;
}

static int partnerCopyBase(int baseId, void *rec) {
    PartnerRec *r = rec;

    r->row = -1;
    r->like = baseId;
    return 1;
}

static void partnerFree(void *rec) {
    PartnerRec *r = rec;

    free(r->allow);
    free(r->deny);
    free(r->evo);
}

/* an original partner: 0..7 or "dw3:partner/<n>" */
static int originalPartnerOf(const ExtJson *v) {
    if (v->t == 0) return extWhole(v->num, 0, PARTNER_COUNT - 1) ? (int)v->num : -1;
    if (v->t == 1 && v->str != NULL) return contentOriginalId(&kPartner, v->str);
    return -1;
}

/* an item: its id from the host's resolver, which knows "dw3:item/<id>" and "<mod>:<key>" (a plain number is the original item of that id); -1 when it is none */
static int itemOf(const ExtJson *v) {
    long long id;
    char name[32];

    if (sHooks.itemId == NULL) return -1;
    if (v->t == 0 && extWhole(v->num, 1, 0x7FFF)) {
        snprintf(name, sizeof name, "dw3:item/%d", (int)v->num);
        id = sHooks.itemId(name);
    } else if (v->t == 1 && v->str != NULL) {
        id = sHooks.itemId(v->str);
    } else {
        return -1;
    }
    return id > 0 && id <= 0x7FFF ? (int)id : -1;
}

static void takeEquipList(const ContentEntry *e, int **list, int *n, const ExtJson *v, const char *which) {
    int i, id, *grown;

    if (v->t != 4) {
        plogLine("partners", "%s partner \"%s\" equip \"%s\" rejected: a list of items (\"dw3:item/<id>\" or \"<mod>:<key>\")\n", e->path, e->name, which);
        return;
    }
    for (i = 0; i < v->n; i++) {
        int dup = 0, k;

        id = itemOf(&v->v[i]);
        if (id < 0) {
            plogLine("partners", "%s partner \"%s\" equip \"%s\" entry %d rejected: not an item (\"dw3:item/<id>\" or \"<mod>:<key>\")\n", e->path,
                     e->name, which, i + 1);
            continue;
        }
        for (k = 0; k < *n; k++) if ((*list)[k] == id) dup = 1;
        if (dup) continue;
        grown = realloc(*list, sizeof(int) * (size_t)(*n + 1));
        if (grown == NULL) return;
        *list = grown;
        grown[(*n)++] = id;
    }
}

static void takeEquip(const ContentEntry *e, PartnerRec *r, const ExtJson *v) {
    int i;

    if (v->t != 2) {
        plogLine("partners", "%s partner \"%s\" field \"equip\" rejected: an object {like, allow, deny}\n", e->path, e->name);
        return;
    }
    for (i = 0; i < v->n; i++) {
        const char *k = v->keys[i];
        const ExtJson *x = &v->v[i];

        if (!strcmp(k, "like")) {
            int p = originalPartnerOf(x);

            if (p < 0) plogLine("partners", "%s partner \"%s\" equip \"like\" rejected: an original partner (0..%d or \"dw3:partner/<n>\")\n", e->path,
                                e->name, PARTNER_COUNT - 1);
            else r->like = p;
        } else if (!strcmp(k, "allow")) {
            takeEquipList(e, &r->allow, &r->nAllow, x, "allow");
        } else if (!strcmp(k, "deny")) {
            takeEquipList(e, &r->deny, &r->nDeny, x, "deny");
        } else {
            plogLine("partners", "%s partner \"%s\" equip key \"%s\" rejected: use like, allow or deny\n", e->path, e->name, k);
        }
    }
}

static void takeEquipSet(const ContentEntry *e, PartnerRec *r, const ExtJson *v) {
    const ExtJson *items = extJsonGet(v, "items"), *bonuses = extJsonGet(v, "bonuses");
    int setItem[4], setBonus[6], i, ok = 1;

    if (v->t != 2 || items == NULL || items->t != 4 || items->n != 4 || bonuses == NULL || bonuses->t != 4 || bonuses->n != 6) {
        plogLine("partners", "%s partner \"%s\" field \"equipSet\" rejected: {\"items\": four items, \"bonuses\": six numbers}\n", e->path, e->name);
        return;
    }
    for (i = 0; i < 4; i++) {
        setItem[i] = itemOf(&items->v[i]);
        if (setItem[i] < 0) {
            plogLine("partners", "%s partner \"%s\" equipSet item %d rejected: not an item (\"dw3:item/<id>\" or \"<mod>:<key>\")\n", e->path, e->name, i + 1);
            ok = 0;
        }
    }
    for (i = 0; i < 6; i++) {
        if (bonuses->v[i].t != 0 || !extWhole(bonuses->v[i].num, -999, 999)) {
            plogLine("partners", "%s partner \"%s\" equipSet bonus %d rejected: a whole number -999..999\n", e->path, e->name, i + 1);
            ok = 0;
        } else {
            setBonus[i] = (int)bonuses->v[i].num;
        }
    }
    if (!ok) return;
    memcpy(r->setItem, setItem, sizeof setItem);
    memcpy(r->setBonus, setBonus, sizeof setBonus);
    r->hasSet = 1;
}

static int evoStat(const ExtJson *v) {
    static const char *const names[7] = {"strength", "defense", "spirit", "wisdom", "speed", "charisma", "level"};
    int i;

    if (v->t == 0 && extWhole(v->num, 1, 14)) return (int)v->num;
    if (v->t == 1 && v->str != NULL) for (i = 0; i < 7; i++) if (!strcmp(v->str, names[i])) return i + 1;
    return -1;
}

static void takeOneEvolution(const ContentEntry *e, PartnerRec *r, const ExtJson *o) {
    EvoDecl d;
    int i, j, ok = 1, haveForm = 0;
    EvoDecl *grown;

    memset(&d, 0, sizeof d);
    d.value = 1;
    if (o->t != 2) {
        plogLine("partners", "%s partner \"%s\" field \"evolution\" rejected: a list of objects {digimon, needs, stat, value}\n", e->path, e->name);
        return;
    }
    for (i = 0; i < o->n; i++) {
        const char *k = o->keys[i];
        const ExtJson *v = &o->v[i];

        if (!strcmp(k, "digimon")) {
            if (v->t != 1 || v->str == NULL || strlen(v->str) >= NAME_MAX_LEN) {
                plogLine("partners", "%s partner \"%s\" evolution \"digimon\" rejected: a partner form (\"dw3:digimon/<id>\" or \"<mod>:<key>\")\n", e->path, e->name);
                ok = 0;
            } else {
                snprintf(d.form, sizeof d.form, "%s", v->str);
                haveForm = 1;
            }
        } else if (!strcmp(k, "stat")) {
            d.stat = evoStat(v);
            if (d.stat < 0) {
                plogLine("partners", "%s partner \"%s\" evolution \"stat\" rejected: 1..14, or strength, defense, spirit, wisdom, speed, charisma or level\n",
                         e->path, e->name);
                ok = 0;
            }
        } else if (!strcmp(k, "value")) {
            if (v->t != 0 || !extWhole(v->num, 0, 32767)) {
                plogLine("partners", "%s partner \"%s\" evolution \"value\" rejected: 0..32767\n", e->path, e->name);
                ok = 0;
            } else {
                d.value = (int)v->num;
            }
        } else if (!strcmp(k, "needs")) {
            int cnt = v->t == 4 ? v->n : (v->t == 2 ? 1 : -1);

            if (cnt < 0 || cnt > 2) {
                plogLine("partners", "%s partner \"%s\" evolution \"needs\" rejected: up to 2 of {digimon, level}\n", e->path, e->name);
                ok = 0;
                continue;
            }
            for (j = 0; j < cnt; j++) {
                const ExtJson *q = v->t == 2 ? v : &v->v[j], *dg = extJsonGet(q, "digimon"), *lv = extJsonGet(q, "level");

                if (q->t != 2 || dg == NULL || dg->t != 1 || dg->str == NULL || strlen(dg->str) >= NAME_MAX_LEN ||
                    (lv != NULL && (lv->t != 0 || !extWhole(lv->num, 1, 99)))) {
                    plogLine("partners", "%s partner \"%s\" evolution need %d rejected: {\"digimon\": \"dw3:digimon/<id>\" or \"<mod>:<key>\", \"level\": 1..99}\n",
                             e->path, e->name, j + 1);
                    ok = 0;
                    continue;
                }
                snprintf(d.need[d.nNeeds], NAME_MAX_LEN, "%s", dg->str);
                d.needLevel[d.nNeeds] = lv != NULL ? (int)lv->num : 1;
                d.nNeeds++;
            }
        } else {
            plogLine("partners", "%s partner \"%s\" evolution key \"%s\" rejected: use digimon, needs, stat or value\n", e->path, e->name, k);
            ok = 0;
        }
    }
    if (!haveForm && ok) {
        plogLine("partners", "%s partner \"%s\" evolution rejected: it needs the \"digimon\" it teaches\n", e->path, e->name);
        ok = 0;
    }
    if (!ok) return;
    if (r->nEvo >= EVO_MAX) {
        plogLine("partners", "%s partner \"%s\" evolution rejected: at most %d entries\n", e->path, e->name, EVO_MAX);
        return;
    }
    grown = realloc(r->evo, sizeof(EvoDecl) * (size_t)(r->nEvo + 1));
    if (grown == NULL) return;
    r->evo = grown;
    r->evo[r->nEvo++] = d;
}

static int partnerField(const ContentEntry *e, const char *key, const ExtJson *v) {
    PartnerRec *r = e->rec;

    if (!strcmp(key, "firstForm")) {
        long long ref = v->t == 1 && v->str != NULL && sHooks.formRef != NULL ? sHooks.formRef(v->str) : -1;

        if (ref < 1) {
            plogLine("partners", "%s partner \"%s\" field \"firstForm\" rejected: not a partner form (an original \"dw3:digimon/<id>\" or \"<mod>:<key>\" "
                     "of a digimon/<key>/ folder)\n", e->path, e->name);
        } else {
            r->row = (int)(ref - 1);
        }
        return 1;
    }
    if (!strcmp(key, "equip")) {
        takeEquip(e, r, v);
        return 1;
    }
    if (!strcmp(key, "equipSet")) {
        takeEquipSet(e, r, v);
        return 1;
    }
    if (!strcmp(key, "evolution")) {
        int i;

        if (v->t == 4) for (i = 0; i < v->n; i++) takeOneEvolution(e, r, &v->v[i]);
        else takeOneEvolution(e, r, v);
        return 1;
    }
    return 0;
}

/* partners/<name>/ of an original partner (the slug of its English name) is that partner's art folder, not a new partner: its partner.json is the exporter's
   information file and is never read (the host tells which names those are) */
static int partnerReserved(const char *key) {
    return sHooks.isOriginalKey != NULL && sHooks.isOriginalKey(key);
}

static int partnerAccept(const ContentEntry *e) {
    const PartnerRec *r = e->rec;

    if (r->row < 0) {
        plogLine("partners", "%s partner \"%s\" rejected: it needs a \"firstForm\" (a partner form)\n", e->path, e->name);
        return 0;
    }
    return 1;
}

static long long formRow(const char *name) {
    long long ref = sHooks.formRef != NULL ? sHooks.formRef(name) : -1;

    return ref >= 1 ? ref - 1 : -1;
}

static void publishEvolutions(const ContentEntry *e, const PartnerRec *r, int idx) {
    int k, j;

    for (k = 0; k < r->nEvo; k++) {
        const EvoDecl *d = &r->evo[k];
        PcEvolution ev;
        long long row = formRow(d->form);
        int ok = row >= 0;

        if (!ok) {
            plogLine("partners", "%s partner \"%s\" evolution \"%s\" dropped: not a partner form\n", e->path, e->name, d->form);
            continue;
        }
        memset(&ev, 0, sizeof ev);
        ev.digimon = (int)row + 1;
        for (j = 0; j < 2; j++) {
            ev.needs[j].level = 1;
            if (j >= d->nNeeds) continue;
            row = formRow(d->need[j]);
            if (row < 0) {
                plogLine("partners", "%s partner \"%s\" evolution \"%s\" dropped: its need \"%s\" is not a partner form\n", e->path, e->name, d->form,
                         d->need[j]);
                ok = 0;
                break;
            }
            ev.needs[j].digimon = (short)(row + 1);
            ev.needs[j].level = (short)d->needLevel[j];
        }
        if (!ok) continue;
        ev.stat = (short)d->stat;
        ev.value = (short)(d->stat != 0 ? d->value : 1);
        if (pcEvolutionExtend(idx, &ev) < 0) {
            plogLine("partners", "%s partner \"%s\" evolution \"%s\" dropped: no room for it\n", e->path, e->name, d->form);
            continue;
        }
        PLOG("partners", PLOG_MODS, "partner %s: evolution list entry %d teaches row %d, needs %d@%d %d@%d, stat %d value %d\n", e->name,
             pcEvolutionCount(idx) - 1, (int)row, (int)ev.needs[0].digimon, (int)ev.needs[0].level, (int)ev.needs[1].digimon,
             (int)ev.needs[1].level, (int)ev.stat, (int)ev.value);
    }
}

/* The partner's own 2D art is its bundle partners/<key>/ (readable-assets A4a, partner_bundle.h), resolved through the asset layers: portrait.png and icon.png
   go to the VRAM band, field/ is its character in the field. A file that is not there falls back to the base's. */
static void publishBundle(const ContentEntry *e, int idx) {
    const char *colon = strchr(e->name, ':');
    const char *key = colon != NULL ? colon + 1 : e->name;
    char rel[160], path[1500], name[160];
    AssetLayer from;

    /* a file that does not load says why on a line of its own (the hooks log it with the path), so there is no second line here */
    if (sHooks.artLoad != NULL && snprintf(rel, sizeof rel, "%s/portrait.png", key) < (int)sizeof rel && assetResolve("partners", rel, path, sizeof path, &from)) {
        sHooks.artLoad(idx, path, e->name);
    }
    if (sHooks.iconLoad != NULL && snprintf(rel, sizeof rel, "%s/icon.png", key) < (int)sizeof rel && assetResolve("partners", rel, path, sizeof path, &from)) {
        sHooks.iconLoad(idx, path, e->name);
    }
    if (sHooks.trainLoad != NULL) sHooks.trainLoad(idx, key);
    if (snprintf(rel, sizeof rel, "%s/field/sprite.json", key) < (int)sizeof rel && assetResolve("partners", rel, path, sizeof path, NULL) &&
        snprintf(name, sizeof name, "%s%s", ASSET_PARTNER_FIELD_PREFIX, key) < (int)sizeof name) {
        pcPartnerSetSprite(idx, name);
    }
}

static int partnerPublish(ContentSet *s) {
    int i, k;

    for (i = 0; i < s->n; i++) {
        const ContentEntry *e = &s->e[i];
        const PartnerRec *r = e->rec;
        int idx = pcPartnerRegister(e->name, r->row);

        if (idx != PARTNER_COUNT + i) { /* the registry is empty of mod partners at a load: the order of registration is the order of ids */
            plogLine("partners", "partner \"%s\" could not be registered at index %d (got %d); the new partners are dropped\n", e->name,
                     PARTNER_COUNT + i, idx);
            pcPartnersUnregister();
            return 0;
        }
        pcPartnerSetIconBase(idx, e->baseId);
        pcPartnerSetEquipLike(idx, r->like);
        for (k = 0; k < r->nAllow; k++) pcPartnerEquipRule(idx, r->allow[k], 1);
        for (k = 0; k < r->nDeny; k++) pcPartnerEquipRule(idx, r->deny[k], 0);
        if (r->hasSet) pcPartnerSetEquipSet(idx, r->setItem, r->setBonus);
        publishEvolutions(e, r, idx);
        publishBundle(e, idx);
        PLOG("partners", PLOG_MODS, "partner %s = index %d, first form row %d, art/equipment like partner %d%s\n", e->name, idx, r->row, r->like,
             r->hasSet ? ", equipment set" : "");
    }
    return 1;
}

/* ---- starter teams ---- */

typedef struct TeamRec {
    int partner[3]; /* registry indices, -1 until "partners" resolved */
} TeamRec;

static int teamCopyBase(int baseId, void *rec);
static int teamField(const ContentEntry *e, const char *key, const ExtJson *v);
static int teamPublish(ContentSet *s);
static int teamAccept(const ContentEntry *e);

static const ContentType kTeam = {
    "team", "starter team", "teams", "teams",
    0, TEAM_ORIG_COUNT - 1, TEAM_ORIG_COUNT, TEAM_NEW_MAX, "%d teams declared, only %d fit; the rest (alphabetical) are dropped\n",
    sizeof(TeamRec),
    teamCopyBase, teamField, NULL, teamPublish,
    noName, noId, textCount, textOf, keepNameHooks, keepTextHook,
    NULL, NULL, teamAccept, 1,
};

static ContentSet sTeams = {&kTeam, NULL, 0};

static int teamCopyBase(int baseId, void *rec) {
    TeamRec *t = rec;
    int k;

    for (k = 0; k < 3; k++) t->partner[k] = baseId >= 0 ? pcStarterParty(baseId)[k] : -1;
    return 1;
}

static int teamField(const ContentEntry *e, const char *key, const ExtJson *v) {
    TeamRec *t = e->rec;
    int k, got[3];

    if (strcmp(key, "partners") != 0) return 0;
    if (v->t != 4 || v->n != 3) {
        plogLine("teams", "%s team \"%s\" field \"partners\" rejected: exactly three partners (\"dw3:partner/<n>\" or \"<mod>:<key>\")\n", e->path, e->name);
        return 1;
    }
    for (k = 0; k < 3; k++) {
        got[k] = v->v[k].t == 1 && v->v[k].str != NULL ? pcPartnerFind(v->v[k].str) : -1;
        if (got[k] < 0) {
            plogLine("teams", "%s team \"%s\" field \"partners\" rejected: entry %d is not a partner (an original \"dw3:partner/<n>\" or a \"<mod>:<key>\" of "
                     "a partners/<key>/ folder)\n", e->path, e->name, k + 1);
            return 1;
        }
    }
    if (got[0] == got[1] || got[0] == got[2] || got[1] == got[2]) {
        plogLine("teams", "%s team \"%s\" field \"partners\" rejected: a partner twice\n", e->path, e->name);
        return 1;
    }
    for (k = 0; k < 3; k++) t->partner[k] = got[k];
    return 1;
}

static int teamAccept(const ContentEntry *e) {
    const TeamRec *t = e->rec;

    if (t->partner[0] < 0 || t->partner[1] < 0 || t->partner[2] < 0) {
        plogLine("teams", "%s team \"%s\" rejected: it needs three \"partners\" (or an original team as \"base\")\n", e->path, e->name);
        return 0;
    }
    return 1;
}

static int teamPublish(ContentSet *s) {
    int i;

    for (i = 0; i < s->n; i++) {
        const TeamRec *t = s->e[i].rec;
        int set = pcStarterPartyAdd(s->e[i].name, t->partner);

        if (set != TEAM_ORIG_COUNT + i) {
            plogLine("teams", "team \"%s\" could not be added at number %d (got %d); the new teams are dropped\n", s->e[i].name, TEAM_ORIG_COUNT + i, set);
            pcStarterPartiesReset();
            return 0;
        }
        PLOG("teams", PLOG_MODS, "team %s = number %d: partners %d, %d, %d\n", s->e[i].name, set, t->partner[0], t->partner[1], t->partner[2]);
    }
    return 1;
}

/* ---- texts: NAMEET entries after the disc's 31, partners first (name, description each) and then the teams ---- */

/* the label of team `k` on the page's left column: the originals' A, B, C go on as D, E, ... Z, AA, AB, ... (a spreadsheet's column names) */
static const char *teamLabel(int k) {
    static char ring[4][8];
    static int next;
    char *out = ring[next++ & 3], rev[8];
    int n = 0, len, i;

    for (k++; k > 0 && n < 6; k = (k - 1) / 26) rev[n++] = (char)('A' + (k - 1) % 26);
    for (len = 0, i = n - 1; i >= 0; i--) out[len++] = rev[i];
    out[len] = '\0';
    return out;
}

static int textCount(void) {
    return 2 * (sPartners.n + sTeams.n) + sTeams.n; /* the partners' and the teams' name and description, then each team's label */
}

static const char *textOf(int index, const char *langDir, int desc) {
    (void)desc;
    if (index < 0) return "";
    if (index < 2 * sPartners.n) return contentText(&sPartners, index / 2, langDir, index % 2);
    index -= 2 * sPartners.n;
    if (index < 2 * sTeams.n) return contentText(&sTeams, index / 2, langDir, index % 2);
    index -= 2 * sTeams.n;
    return index < sTeams.n ? teamLabel(TEAM_ORIG_COUNT + index) : "";
}

/* ---- load ---- */

void partnerExtReset(void) {
    if (sHooks.artReset != NULL) sHooks.artReset();
    tdSetPartnerTextHook(NULL, NULL);
    contentUnload(&sTeams);
    contentUnload(&sPartners);
    pcPartnersUnregister();
    pcStarterPartiesReset();
}


int partnerExtLoad(void) {
    int n;

    partnerExtReset();
    pcStarterPartiesInit(STARTER_PARTIES, TEAM_ORIG_COUNT); /* the table the player-name screen and setParty read starts from the game's */
    n = contentLoad(&sPartners);
    contentLoad(&sTeams);
    if (sPartners.n + sTeams.n > 0) tdSetPartnerTextHook(textCount, textOf);
    return n;
}

const ContentType *partnerExtType(void) {
    return &kPartner;
}

const ContentType *teamExtType(void) {
    return &kTeam;
}

int partnerExtCount(void) {
    return sPartners.n;
}

int teamExtCount(void) {
    return sTeams.n;
}
