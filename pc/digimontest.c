#define _GNU_SOURCE 1
/*
 * Host test of the Digimon content type (pc/shim/digimon_ext.c, content-techs-digimon D1/D2): the digimon/ folders (digimon-digimon-v1, A8.3), validation
 * of every field, patching and restoring DIGIMON_DATA_CARD, the battle table read-buffer patch, learnedBy by "dw3:digimon/<id>",
 * and the reference export. No game, no disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "engine/game_state.h"
#include "shim/content.h"
#include "shim/digimon_ext.h"
#include "shim/ext_state.h"
#include "shim/item_ext.h"
#include "shim/model_bake.h"
#include "shim/mods.h"
#include "shim/port_fs.h"
#include "shim/port_paths.h"
#include "shim/tech_ext.h"

GameState GAME;
TechData TECHS_CARD[443];
DigimonData DIGIMON_DATA_CARD[55];
ItemInfo ITEMS[402];
u8 ITEM_TYPE_CATEGORIES[32];
static ItemInfo *stubGet(s32 id) { return id <= 0 ? NULL : &ITEMS[id - 1]; }
static s32 stubCategory(s32 id) { return ITEM_TYPE_CATEGORIES[stubGet(id)->type]; }
static s32 stubIsKind(s32 id, s32 kind) { return stubGet(id)->kind == kind; }
static s32 stubList(s32 type, s16 *out) { (void)type; (void)out; return 0; }
ItemFuncs ITEM_FUNCS = {stubGet, stubCategory, stubIsKind, stubList};

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "digimontest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gRoot[600];
static int gSavedErr;
static const char *gLogPath = "/tmp/digimontest.log";

static void logBegin(void) {
    int fd;
    fflush(stderr);
    gSavedErr = dup(2);
    fd = open(gLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

static char *logEnd(void) {
    static char buf[16384];
    FILE *f;
    size_t n = 0;
    fflush(stderr);
    dup2(gSavedErr, 2);
    close(gSavedErr);
    f = fopen(gLogPath, "rb");
    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }
    buf[n] = 0;
    return buf;
}

static void putFile(const char *rel, const char *text) {
    char path[900], dir[900], *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "mkdir -p '%s'", path);
    slash = strrchr(dir, '/');
    *slash = '\'';
    slash[1] = 0;
    if (system(dir)) {}
    f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void modJson(const char *mod, const char *kind) {
    char rel[200], text[200];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", mod);
    snprintf(text, sizeof text, "{\"format\":\"mod-v1\",\"name\":\"%s\",\"version\":\"1.0\",\"kind\":\"%s\"}", mod, kind);
    putFile(rel, text);
}

/* ---- the fixtures ----
   Most fixtures of this file say a mod's Digimon as one object, {"new": {key: record, ...}, "change": {id: facets, ...}, "encounters": {...}}.
   The loader reads a folder per Digimon, so putDigimon splits that object into the files it reads: digimon/<key>/digimon.json for each "new" entry
   (its "name", the language texts, becomes "title"), digimon/d<nnn>/digimon.json for each "change" entry (the change of an original, A8.3; two entries
   of one id share the file) and content/encounters.json for "encounters". It deletes the mod's earlier ones first. */

static const char *skipWs(const char *p) {
    while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
    return p;
}

/* the end of the JSON value that starts at p: after its closing quote, brace or bracket, or at the ',' or '}' that ends a literal */
static const char *valueEnd(const char *p) {
    int depth = 0;

    while (*p != '\0') {
        if (*p == '"') {
            for (p++; *p != '"'; p++) {
                if (*p == '\\') p++;
            }
        } else if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            if (depth == 0) return p;
            depth--;
        } else if (*p == ',' && depth == 0) {
            return p;
        }
        p++;
        if (depth == 0 && (p[-1] == '"' || p[-1] == '}' || p[-1] == ']')) return p;
    }
    return p;
}

/* the next member of the object whose text continues at *pp (just after its '{' or after a ','): its key and value text; 0 at the end */
static int nextMember(const char **pp, char *key, size_t cap, const char **val, const char **vend) {
    const char *p = skipWs(*pp);
    size_t n = 0;

    if (*p != '"') return 0;
    for (p++; *p != '"'; p++) {
        if (*p == '\\') p++;
        if (n + 1 < cap) key[n++] = *p;
    }
    key[n] = '\0';
    p = skipWs(skipWs(p + 1) + 1); /* the ':' */
    *val = p;
    *vend = valueEnd(p);
    p = skipWs(*vend);
    if (*p == ',') p++;
    *pp = p;
    return 1;
}

/* appends the members of the object `val` as ,"key":value; the top-level "name" of a new Digimon is its text: "title" */
static size_t addMembers(char *out, size_t cap, size_t at, const char *val, int newDigimon) {
    const char *p = val + 1, *v, *e;
    char key[96];

    while (nextMember(&p, key, sizeof key, &v, &e)) {
        at += (size_t)snprintf(out + at, cap - at, ",\"%s\":%.*s", newDigimon && !strcmp(key, "name") ? "title" : key, (int)(e - v), v);
    }
    return at;
}

static void putDigimon(const char *mod, const char *text) {
    static char rec[60000], change[32][4000];
    static int changeId[32];
    char cmd[700], rel[200], key[96], sub[96], head[160];
    const char *p = skipWs(text) + 1, *v, *e, *q, *v2, *e2;
    int nChange = 0, i;

    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods/%s/digimon' '%s/mods/%s/content/encounters.json'", gRoot, mod, gRoot, mod);
    if (system(cmd)) {}
    while (nextMember(&p, key, sizeof key, &v, &e)) {
        if (!strcmp(key, "new")) {
            for (q = v + 1; nextMember(&q, sub, sizeof sub, &v2, &e2);) {
                snprintf(rel, sizeof rel, "mods/%s/digimon/%s/digimon.json", mod, sub);
                snprintf(head, sizeof head, "{\"format\":\"digimon-digimon-v1\",\"name\":\"%s\"", sub);
                snprintf(rec, sizeof rec, "%s", head);
                if (*v2 == '{') addMembers(rec, sizeof rec, strlen(rec), v2, 1);
                strcat(rec, "}");
                putFile(rel, rec);
            }
        } else if (!strcmp(key, "change")) {
            for (q = v + 1; nextMember(&q, sub, sizeof sub, &v2, &e2);) {
                int id = !strncmp(sub, "dw3:digimon/", 12) ? atoi(sub + 12) : atoi(sub);

                for (i = 0; i < nChange && changeId[i] != id; i++) {}
                if (i == nChange) {
                    changeId[nChange] = id;
                    change[nChange][0] = '\0';
                    nChange++;
                }
                if (*v2 == '{') addMembers(change[i], sizeof change[i], strlen(change[i]), v2, 0);
            }
        } else if (!strcmp(key, "encounters")) {
            snprintf(rec, sizeof rec, "{\"format\":\"encounters-v1\",\"encounters\":%.*s}", (int)(e - v), v);
            snprintf(rel, sizeof rel, "mods/%s/content/encounters.json", mod);
            putFile(rel, rec);
        }
    }
    for (i = 0; i < nChange; i++) {
        snprintf(rel, sizeof rel, "mods/%s/digimon/d%03d/digimon.json", mod, changeId[i]);
        snprintf(rec, sizeof rec, "{\"format\":\"digimon-digimon-v1\",\"name\":\"d%03d\"%s}", changeId[i], change[i]);
        putFile(rel, rec);
    }
}

/* the fake battle table: three enemies (ids 206, 207, 208), 70 bytes each, then the id 0 that ends it */
#define ROW 70
static unsigned char gTable[ROW * 4 + 8];

static void put16(unsigned char *p, int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static int get16(const unsigned char *p) { return (short)(p[0] | (p[1] << 8)); }

static void buildTable(void) {
    int i, k;
    memset(gTable, 0, sizeof gTable);
    for (i = 0; i < 3; i++) {
        unsigned char *r = gTable + i * ROW;
        put16(r + 0, 206 + i);
        put16(r + 2, 5 + i);      /* item */
        put16(r + 4, 10 + i);     /* itemChance */
        put16(r + 6, 60 + i);     /* nameId */
        put16(r + 8, 7 + i);      /* techs[0] */
        put16(r + 10, 8);         /* techs[1] */
        for (k = 0; k < 5; k++) put16(r + 0x0E + 2 * k, 100 + 10 * k + i);
        for (k = 0; k < 12; k++) put16(r + 0x18 + 2 * k, 20 + k);
        r[0x30] = (unsigned char)(2 + i);
        r[0x32] = 0xAB; /* the AI, which no field touches */
        r[0x45] = 0xCD;
    }
}

/* the fake fighters file: the header, the entry list (partners 3 and 4, enemies 206, 207, 208 with their kinds) ended by an id 0,
   then the infos: two 0xC4 partner infos and three 0x48 enemy infos, each with a marker in its first bytes */
#define FIGHTERS_LEN 2048
#define FE_AT 16
static unsigned char gFighters[FIGHTERS_LEN];

static void buildFighters(void) {
    static const int ids[5] = {3, 4, 206, 207, 208}, idx[5] = {0, 1, 0, 1, 2}, kind[5] = {5, 6, 0x3A, 0x3B, 0x3C};
    int i;

    memset(gFighters, 0, sizeof gFighters);
    put16(gFighters + 0, 0x1000);
    gFighters[2] = 0x10; /* base 0x00100000 */
    put16(gFighters + 4, FE_AT);
    put16(gFighters + 8, 64);          /* the partner infos */
    put16(gFighters + 12, 64 + 2 * 0xC4); /* the enemy infos */
    for (i = 0; i < 5; i++) {
        put16(gFighters + FE_AT + 4 * i, ids[i]);
        gFighters[FE_AT + 4 * i + 2] = (unsigned char)idx[i];
        gFighters[FE_AT + 4 * i + 3] = (unsigned char)kind[i];
    }
    gFighters[64] = 0x11;
    gFighters[64 + 0xC4] = 0x12;
    for (i = 0; i < 3; i++) gFighters[64 + 2 * 0xC4 + 0x48 * i] = (unsigned char)(0x21 + i);
}

static int probe(int digimon, int slot) {
    if ((digimon == 5 || digimon == 4) && slot == 11) return 0;
    if (digimon == 207 && slot == 11) return 0;
    return 1;
}

static void buildTables(void) {
    int i;
    memset(TECHS_CARD, 0, sizeof TECHS_CARD);
    for (i = 0; i < 443; i++) {
        TECHS_CARD[i].mp = (u16)(i + 1);
        TECHS_CARD[i].script = (u8)(i % 18);
        TECHS_CARD[i].icon = 2;
    }
    TECHS_CARD[11].script = 11; /* technique 12 needs slot 11 */
    TECHS_CARD[6].script = 5;   /* technique 7: the generic one (needs 5, 6 and 8 of a partner) */
    memset(DIGIMON_DATA_CARD, 0, sizeof DIGIMON_DATA_CARD);
    for (i = 0; i < 55; i++) {
        DIGIMON_DATA_CARD[i].id = (u16)(i < 52 ? 3 + i : 468 + (i - 52));
        DIGIMON_DATA_CARD[i].hp = (u8)(10 + i);
        DIGIMON_DATA_CARD[i].mp = (u8)(20 + i);
        DIGIMON_DATA_CARD[i].family = (u8)(1 + i % 3);
        DIGIMON_DATA_CARD[i].nameId = (u8)(i + 1);
        DIGIMON_DATA_CARD[i].battleStats[0] = (u16)(100 + i);
        DIGIMON_DATA_CARD[i].resistances[2] = (u16)(30 + i);
        DIGIMON_DATA_CARD[i].statusResists[3] = (u8)(5 + i);
        DIGIMON_DATA_CARD[i].skills[2] = 111;
        DIGIMON_DATA_CARD[i].skillLevels[1] = 20;
        DIGIMON_DATA_CARD[i].pairPartner = 9;
    }
    buildTable();
}

static int loadAll(void) {
    char root[700];
    snprintf(root, sizeof root, "%s/mods", gRoot);
    modsInit(root, NULL);
    itemExtLoad();
    techExtSetFighterProbe(probe);
    techExtLoad();
    digimonExtSetEnemyTable(gTable, sizeof gTable);
    digimonExtSetFighters(gFighters, sizeof gFighters);
    return digimonExtLoad();
}

static void unloadAll(void) {
    digimonExtReset();
    techExtReset();
    itemExtReset();
}

static const char *kNames(int id) {
    static char n[40];
    int nid = digimonExtNameId(id);
    if (nid < 0) return NULL;
    snprintf(n, sizeof n, "Name%d", nid);
    return n;
}

static void testNoMods(void) {
    DigimonData snap[55];
    memcpy(snap, DIGIMON_DATA_CARD, sizeof snap);
    CHECK(loadAll() == 0);
    CHECK(memcmp(snap, DIGIMON_DATA_CARD, sizeof snap) == 0);
    CHECK(digimonExtEnemyPatchCount() == 0);
    CHECK(digimonExtNewCount() == 0 && digimonExtPlaceCount() == 0);
    CHECK(digimonExtBuildBattleTable(NULL) == NULL && digimonExtBuildFighters(NULL) == NULL);
    unloadAll();
}

static void testChanges(void) {
    DigimonData snap[55];
    unsigned char buf[sizeof gTable], orig[sizeof gTable];
    const char *log;
    int n;

    buildTables();
    memcpy(snap, DIGIMON_DATA_CARD, sizeof snap);
    memcpy(orig, gTable, sizeof orig);
    modJson("zeta", "content");
    modJson("alpha", "content");
    modJson("plain", "assets");
    putFile("mods/zeta/content/techs.json",
            "{\"format\":\"techs-v1\",\"new\":{\"spark\":{\"base\":8,\"script\":5,\"name\":{\"eng\":\"Spark\"},"
            "\"learnedBy\":{\"dw3:digimon/6\":{\"slot\":3,\"level\":4},\"dw3:digimon/7\":{\"slot\":2,\"level\":9},"
            "\"dw3:digimon/x\":{\"slot\":1,\"level\":1}}}}}");
    putDigimon("zeta",
            "{\"format\":\"digimon-v1\",\"change\":{"
            "\"dw3:digimon/3\":{\"partner\":{\"hp\":77,\"mp\":0,\"stats\":{\"strength\":500,\"speed\":20,\"luck\":1,\"defense\":1000},"
            "\"resistances\":{\"2\":40,\"7\":1},\"statusResists\":{\"sleep\":9},\"family\":2,"
            "\"skills\":{\"2\":{\"tech\":\"zeta:spark\",\"level\":7},\"4\":{\"tech\":\"dw3:tech/12\"},\"5\":{\"level\":33},"
            "\"6\":{\"tech\":\"nobody:nothing\",\"level\":5},\"7\":{\"tech\":1}},"
            "\"statGrowth\":{\"strength\":4,\"charisma\":0,\"speed\":6,\"luck\":1},"
            "\"resistGrowth\":{\"0\":2,\"3\":0,\"6\":5,\"7\":1},\"knownLevels\":{\"0\":15,\"4\":99,\"2\":0,\"3\":100},"
            "\"nameId\":9,\"hp\":300}},"
            "\"4\":{\"partner\":{\"family\":9,\"expRate\":50,\"skills\":{\"1\":{\"tech\":7,\"level\":100}}}},"
            "\"5\":{\"partner\":{\"hp\":11,\"skills\":{\"3\":{\"tech\":12,\"level\":5}}}},"
            "\"dw3:digimon/468\":{\"partner\":{\"hp\":1}},"
            "\"dw3:digimon/400\":{\"partner\":{\"hp\":1}},"
            "\"dw3:digimon/206\":{\"partner\":{\"hp\":1}},"
            "\"dw3:digimon/10\":{\"enemy\":{\"hp\":1}},"
            "\"dw3:digimon/206\":{\"enemy\":{\"stats\":{\"attack\":900,\"speed\":5},\"resistances\":{\"0\":1,\"9\":1},"
            "\"statusResists\":{\"poison\":77},\"family\":3,\"techs\":{\"1\":\"zeta:spark\",\"3\":\"dw3:tech/9\"},"
            "\"item\":\"dw3:item/43\",\"itemChance\":1023}},"
            "\"207\":{\"enemy\":{\"techs\":{\"1\":0,\"2\":12,\"4\":5},\"item\":\"nobody:nothing\",\"itemChance\":2000,"
            "\"stats\":{\"attack\":40000},\"ai\":1}},"
            "\"208\":{\"enemy\":{\"item\":0},\"bogus\":{}},"
            "\"bad-key\":{\"partner\":{}}}}");
    putDigimon("alpha",
            "{\"format\":\"digimon-v1\",\"change\":{\"3\":{\"partner\":{\"hp\":55,\"expLevel\":9}},\"206\":{\"enemy\":{\"itemChance\":3}}}}");
    putDigimon("plain", "{\"format\":\"digimon-v1\",\"change\":{\"3\":{\"partner\":{\"hp\":1}}}}");

    logBegin();
    n = loadAll();
    log = logEnd();

    /* applied: the entries that kept at least one valid field (partner 3, 5; 4 changed expRate; enemies 206, 208) */
    CHECK(n >= 5);
    CHECK(DIGIMON_DATA_CARD[0].hp == 77 && DIGIMON_DATA_CARD[0].mp == 0 && DIGIMON_DATA_CARD[0].expLevel == 9); /* zeta after alpha wins hp */
    CHECK(DIGIMON_DATA_CARD[0].battleStats[0] == 500 && DIGIMON_DATA_CARD[0].battleStats[4] == 20 && DIGIMON_DATA_CARD[0].battleStats[1] == snap[0].battleStats[1]);
    CHECK(DIGIMON_DATA_CARD[0].resistances[2] == 40 && DIGIMON_DATA_CARD[0].statusResists[3] == 9 && DIGIMON_DATA_CARD[0].family == 2);
    CHECK(DIGIMON_DATA_CARD[0].skills[2] == techExtIdOf("zeta:spark") && DIGIMON_DATA_CARD[0].skillLevels[1] == 7); /* the tech of a mod, by name */
    CHECK(DIGIMON_DATA_CARD[0].skills[4] == 12 && DIGIMON_DATA_CARD[0].skillLevels[3] == snap[0].skillLevels[3]);   /* tech only: level kept */
    CHECK(DIGIMON_DATA_CARD[0].skillLevels[4] == 33 && DIGIMON_DATA_CARD[0].skills[5] == snap[0].skills[5]);        /* level only */
    CHECK(DIGIMON_DATA_CARD[0].skills[6] == snap[0].skills[6] && DIGIMON_DATA_CARD[0].skills[1] == snap[0].skills[1]);
    CHECK(DIGIMON_DATA_CARD[0].statGrowth[0] == 4 && DIGIMON_DATA_CARD[0].statGrowth[5] == 0 && DIGIMON_DATA_CARD[0].statGrowth[4] == snap[0].statGrowth[4]);
    CHECK(DIGIMON_DATA_CARD[0].resistGrowth[0] == 2 && DIGIMON_DATA_CARD[0].resistGrowth[6] == 5 && DIGIMON_DATA_CARD[0].resistGrowth[3] == snap[0].resistGrowth[3]);
    CHECK(DIGIMON_DATA_CARD[0].knownLevels[0] == 15 && DIGIMON_DATA_CARD[0].knownLevels[4] == 99 && DIGIMON_DATA_CARD[0].knownLevels[2] == snap[0].knownLevels[2] &&
          DIGIMON_DATA_CARD[0].knownLevels[3] == snap[0].knownLevels[3]);
    CHECK(DIGIMON_DATA_CARD[0].nameId == snap[0].nameId && DIGIMON_DATA_CARD[0].pairPartner == 9);                  /* excluded fields stay */
    CHECK(DIGIMON_DATA_CARD[1].expRate == 50 && DIGIMON_DATA_CARD[1].family == snap[1].family && DIGIMON_DATA_CARD[1].skills[1] == snap[1].skills[1]);
    CHECK(DIGIMON_DATA_CARD[2].hp == 11 && DIGIMON_DATA_CARD[2].skills[3] == snap[2].skills[3]);                    /* digimon 5: no slot 11 */
    CHECK(DIGIMON_DATA_CARD[3].skills[3] == techExtIdOf("zeta:spark"));                                       /* learnedBy by dw3:digimon/6 */
    /* digimon.json wins over learnedBy of digimon 7 (row 4, slot 2: it is the digimon.json slot of nobody; learnedBy took it) */
    CHECK(DIGIMON_DATA_CARD[4].skills[2] == techExtIdOf("zeta:spark") && DIGIMON_DATA_CARD[4].skillLevels[1] == 9);
    /* rows nothing names are byte-identical */
    CHECK(memcmp(&DIGIMON_DATA_CARD[5], &snap[5], sizeof(DigimonData) * 47) == 0);
    CHECK(DIGIMON_DATA_CARD[52].hp == snap[52].hp);

    /* the rejections, each with its line */
    CHECK(strstr(log, "field \"stats.luck\" rejected: unknown key") != NULL);
    CHECK(strstr(log, "field \"stats.defense\" rejected: a whole number 0..999") != NULL);
    CHECK(strstr(log, "field \"resistances.7\" rejected: unknown key") != NULL);
    CHECK(strstr(log, "field \"hp\" rejected: a whole number 0..255") != NULL);
    CHECK(strstr(log, "field \"nameId\" rejected: unknown field") != NULL);
    CHECK(strstr(log, "field \"statGrowth.speed\" rejected: a whole number 0..5") != NULL && strstr(log, "field \"statGrowth.luck\" rejected: unknown key") != NULL);
    CHECK(strstr(log, "field \"resistGrowth.3\" rejected: a whole number 1..5") != NULL && strstr(log, "field \"resistGrowth.7\" rejected: unknown key") != NULL);
    CHECK(strstr(log, "field \"knownLevels.2\" rejected: a whole number 1..99") != NULL && strstr(log, "field \"knownLevels.3\" rejected: a whole number 1..99") != NULL);
    CHECK(strstr(log, "field \"skills.6\" rejected: \"tech\" is not a technique") != NULL);
    CHECK(strstr(log, "field \"skills.7\" rejected: the slot is 1..6") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/4\" field \"family\" rejected: 9 is not a value any original partner form has") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/4\" field \"skills.1\" rejected: \"level\" must be 1..99") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/5\" field \"skills.3\" rejected: its fighter has no script slot 11") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/468\" rejected: it is an empty placeholder") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/400\" rejected: no such Digimon") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/206\" facet \"partner\" rejected: this Digimon is an enemy") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/10\" facet \"enemy\" rejected: this Digimon is a partner form") != NULL);
    CHECK(strstr(log, "d000/digimon.json rejected: d000 is not an original Digimon") != NULL); /* "bad-key" of the old file: not an id */
    CHECK(strstr(log, "\"dw3:digimon/207\" field \"techs.1\" rejected") != NULL && strstr(log, "the first one cannot be 0") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/207\" field \"techs.2\" rejected: its fighter has no script slot 11") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/207\" field \"techs.4\" rejected: the slot is 1..3") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/207\" field \"item\" rejected: not an item") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/207\" field \"itemChance\" rejected: a whole number 0..1023") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/207\" field \"stats.attack\" rejected: a whole number 0..32767") != NULL);
    CHECK(strstr(log, "\"dw3:digimon/207\" field \"ai\" rejected: unknown field") != NULL);
    CHECK(strstr(log, "key \"bogus\" rejected") != NULL);
    CHECK(strstr(log, "digimon/x") != NULL && strstr(log, "learnedBy \"dw3:digimon/x\" rejected") != NULL);

    /* the enemy patch on a read buffer: only the named bytes change */
    CHECK(digimonExtEnemyPatchCount() == 2);
    memcpy(buf, gTable, sizeof buf);
    digimonExtPatchBattleTable(buf, sizeof buf);
    CHECK(get16(buf + 0x0E) == 900 && get16(buf + 0x0E + 8) == 5 && get16(buf + 0x0E + 2) == get16(orig + 0x0E + 2));
    CHECK(get16(buf + 0x18) == 1 && get16(buf + 0x18 + 2 * 7) == 77 && get16(buf + 0x18 + 2 * 9) == get16(orig + 0x18 + 2 * 9));
    CHECK(buf[0x30] == 3 && get16(buf + 8) == techExtIdOf("zeta:spark") && get16(buf + 10) == 8 && get16(buf + 12) == 9);
    CHECK(get16(buf + 2) == 43 && get16(buf + 4) == 1023); /* zeta's 1023 after alpha's 3 */
    CHECK(get16(buf + 0) == 206 && get16(buf + 6) == 60 && buf[0x32] == 0xAB && buf[0x45] == 0xCD);
    CHECK(memcmp(buf + ROW, orig + ROW, ROW) == 0);        /* 207: every field rejected, untouched */
    CHECK(get16(buf + 2 * ROW + 2) == 0 && get16(buf + 2 * ROW + 4) == get16(orig + 2 * ROW + 4)); /* 208: item 0 */
    CHECK(memcmp(buf + 2 * ROW + 6, orig + 2 * ROW + 6, ROW - 6) == 0);
    CHECK(memcmp(buf + 3 * ROW, orig + 3 * ROW, sizeof buf - 3 * ROW) == 0);
    /* the original table the loader copied is not changed by patching: patching twice gives the same bytes */
    memcpy(buf, gTable, sizeof buf);
    digimonExtPatchBattleTable(buf, sizeof buf);
    digimonExtPatchBattleTable(buf, sizeof buf);
    CHECK(get16(buf + 2) == 43 && get16(buf + 0x0E) == 900);
    /* a short buffer never reads past its end */
    memcpy(buf, gTable, sizeof buf);
    digimonExtPatchBattleTable(buf, ROW - 1);
    CHECK(memcmp(buf, orig, sizeof buf) == 0);

    /* the reference lists the ORIGINAL values although the table is patched */
    {
        char *ref = contentReferenceText(digimonExtType(), kNames);
        const char *p;
        int count = 0, placeholders = 0, enemies = 0;

        CHECK(ref != NULL);
        if (ref != NULL) {
            for (p = ref; (p = strstr(p, "\"dw3:digimon/")) != NULL; p++) count++;
            for (p = ref; (p = strstr(p, "\"placeholder\": true")) != NULL; p++) placeholders++;
            for (p = ref; (p = strstr(p, "\"kind\": \"enemy\"")) != NULL; p++) enemies++;
            CHECK(count == 55 + 3 && placeholders == 3 && enemies == 3);
            CHECK(strstr(ref, "\"dw3:digimon/3\": { \"id\": 3, \"eng\": \"Name1\", \"kind\": \"partner\", \"model\": \"digimon/d003/model/\", \"hp\": 10,") != NULL);
            CHECK(strstr(ref, "\"dw3:digimon/206\": { \"id\": 206, \"eng\": \"Name60\", \"kind\": \"enemy\", \"model\": \"digimon/d206/model/\", \"family\": 2,") != NULL);
            CHECK(strstr(ref, "\"item\": \"dw3:item/5\"") != NULL && strstr(ref, "\"techs\": {\"1\": \"dw3:tech/7\", \"2\": \"dw3:tech/8\"}") != NULL);
            CHECK(strstr(ref, "\"skills\": {\"2\": {\"tech\": \"dw3:tech/111\", \"level\": 20}}") != NULL);
            CHECK(strstr(ref, "\"stats\": {\"strength\": 100,") != NULL && strstr(ref, "\"statusResists\": {\"poison\": 0,") != NULL);
            /* scriptSlots: all 18 for most; the probe says digimon 5, 4 and 207 lack slot 11 */
            CHECK(strstr(ref, "\"scriptSlots\": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17] }") != NULL);
            CHECK(strstr(ref, "\"scriptSlots\": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17] }") != NULL);
            free(ref);
        }
    }

    /* reload semantics: reset puts every row back, byte for byte, and learnedBy's own restore comes out right */
    digimonExtReset();
    techExtReset();
    CHECK(memcmp(snap, DIGIMON_DATA_CARD, sizeof snap) == 0);
    CHECK(digimonExtEnemyPatchCount() == 0);
    memcpy(buf, gTable, sizeof buf);
    digimonExtPatchBattleTable(buf, sizeof buf);
    CHECK(memcmp(buf, orig, sizeof buf) == 0);
    itemExtReset();
}

static void testBadFiles(void) {
    const char *log;

    buildTables();
    modJson("m1", "content");
    modJson("m2", "content");
    modJson("m3", "content");
    putFile("mods/m1/digimon/d003/digimon.json", "not json");
    putFile("mods/m1/digimon/y/digimon.json", "not json");
    putFile("mods/m2/digimon/d003/digimon.json", "{\"format\":\"digimon-v1\",\"name\":\"d003\",\"partner\":{\"hp\":1}}");
    putFile("mods/m3/digimon/x/digimon.json", "{\"format\":\"digimon-digimon-v1\",\"name\":\"x\"}");
    logBegin();
    CHECK(loadAll() == 0);
    log = logEnd();
    CHECK(strstr(log, "m1/digimon/d003/digimon.json rejected: not valid JSON") != NULL);
    CHECK(strstr(log, "m1/digimon/y/digimon.json rejected: not valid JSON") != NULL);
    CHECK(strstr(log, "m2/digimon/d003/digimon.json rejected: \"format\" must be \"digimon-digimon-v1\"") != NULL);
    CHECK(strstr(log, "digimon \"m3:x\" rejected: \"base\" must be an original Digimon id (3..470)") != NULL);
    CHECK(DIGIMON_DATA_CARD[0].hp == 10);
    unloadAll();

    /* without the battle table, enemy changes are rejected and partner ones still apply */
    putDigimon("m1",
            "{\"format\":\"digimon-v1\",\"change\":{\"206\":{\"enemy\":{\"itemChance\":1}},\"3\":{\"partner\":{\"hp\":12}}}}");
    {
        char root[700];
        snprintf(root, sizeof root, "%s/mods", gRoot);
        modsInit(root, NULL);
        itemExtLoad();
        techExtLoad();
        digimonExtSetEnemyTable(NULL, 0);
        logBegin();
        CHECK(digimonExtLoad() == 1);
        log = logEnd();
        CHECK(strstr(log, "the battle table is not available") != NULL && DIGIMON_DATA_CARD[0].hp == 12);
    }
    unloadAll();
    CHECK(DIGIMON_DATA_CARD[0].hp == 10);
}

/* new enemies (D3) and their placement (D4) */
static void testNew(void) {
    const char *log;
    unsigned char *bt, *ft;
    size_t blen = 0, flen = 0;
    int n, i;
    BattleEnemy orig, *row;
    const ContentType *t;

    buildTables();
    modJson("nm", "content");
    modJson("zz", "content");
    putDigimon("nm",
            "{\"format\":\"digimon-v1\",\"new\":{"
            "\"zed\":{\"base\":206,\"model\":207,\"name\":{\"eng\":\"Zed\",\"spa\":\"Zeta\"},\"description\":{\"eng\":\"d\"},"
            "\"stats\":{\"attack\":500,\"luck\":1},\"techs\":{\"1\":7},\"item\":\"dw3:item/43\",\"itemChance\":900,\"family\":3,\"ai\":1},"
            "\"alpha\":{\"base\":\"dw3:digimon/207\",\"name\":{\"eng\":\"Alpha\"}},"
            "\"bad-model\":{\"base\":206,\"model\":3,\"name\":{\"eng\":\"B\"}},"
            "\"slot\":{\"base\":206,\"model\":207,\"name\":{\"eng\":\"S\"},\"techs\":{\"2\":12,\"3\":9}},"
            "\"placeholder\":{\"base\":468,\"name\":{\"eng\":\"P\"}},"
            "\"nobody\":{\"base\":999,\"name\":{\"eng\":\"N\"}},"
            "\"no-name\":{\"base\":206}},"
            "\"change\":{\"471\":{\"enemy\":{\"family\":2}}},"
            "\"encounters\":{"
            "\"34\":{\"1\":{\"enemy\":\"nm:zed\",\"level\":5,\"hp\":100,\"mp\":10,\"strength\":16},"
            "\"2\":{\"enemy\":206},\"3\":{\"enemy\":\"nm:nothing\",\"level\":1},\"4\":{\"enemy\":206},"
            "\"x\":{\"enemy\":206}},"
            "\"35\":{\"3\":{\"enemy\":0},\"1\":{\"enemy\":\"dw3:digimon/208\",\"level\":100,\"hp\":0,\"mp\":-1,\"strength\":9,\"bogus\":1}},"
            "\"0\":{\"1\":{\"enemy\":206}},\"335\":{\"1\":{\"enemy\":206}},\"bad\":{},\"36\":5}}");
    putDigimon("zz",
            "{\"format\":\"digimon-v1\",\"encounters\":{\"34\":{\"1\":{\"enemy\":\"nm:alpha\",\"level\":7}}}}");

    logBegin();
    n = loadAll();
    log = logEnd();

    /* ids: alphabetical by full name, from 471 */
    CHECK(digimonExtNewCount() == 4);
    CHECK(digimonExtNewId("nm:alpha") == 471 && digimonExtNewId("nm:bad-model") == 472 && digimonExtNewId("nm:slot") == 473 &&
          digimonExtNewId("nm:zed") == 474 && digimonExtNewId("nm:placeholder") == -1 && digimonExtNewId("nm:nobody") == -1);
    CHECK(digimonExtNewName(474) != NULL && strcmp(digimonExtNewName(474), "nm:zed") == 0 && digimonExtNewName(470) == NULL);
    CHECK(digimonExtNameId(474) == 254 && digimonExtNameId(471) == 251 && digimonExtNameId(475) == -1);
    CHECK(n >= 4);
    /* the names reach the text layer's hooks, with the es-419 -> spa -> eng fallback */
    t = digimonExtType();
    CHECK(t->count() == 4 && strcmp(t->text(3, "eng", 0), "Zed") == 0 && strcmp(t->text(3, "es-419", 0), "Zeta") == 0 &&
          strcmp(t->text(0, "ger", 0), "Alpha") == 0);

    /* the battle table: the original rows untouched, then the new ones, then the id 0 */
    bt = digimonExtBuildBattleTable(&blen);
    CHECK(bt != NULL && blen == 2048);
    if (bt != NULL) {
        const unsigned char *zed = bt + 6 * ROW, *alpha = bt + 3 * ROW;

        CHECK(memcmp(bt, gTable, 3 * ROW) == 0);
        CHECK(get16(alpha) == 471 && get16(alpha + 6) == 251 && memcmp(alpha + 2, gTable + ROW + 2, 4) == 0 &&
              memcmp(alpha + 8, gTable + ROW + 8, ROW - 8) == 0); /* a copy of 207 but id and nameId */
        CHECK(get16(zed) == 474 && get16(zed + 6) == 254 && get16(zed + 0x0E) == 500 && get16(zed + 0x0E + 2) == get16(gTable + 0x0E + 2));
        CHECK(get16(zed + 8) == 7 && zed[0x30] == 3 && get16(zed + 2) == 43 && get16(zed + 4) == 900 && zed[0x32] == 0xAB);
        CHECK(get16(bt + 7 * ROW) == 0 && get16(bt + 8 * ROW) == 0); /* ended */
        /* the post-read patch still works on the longer buffer, and stops at the id 0 */
        digimonExtPatchBattleTable(bt, blen);
        CHECK(get16(bt + 6 * ROW) == 474);
        free(bt);
    }
    /* the tech of "slot": 2 has no slot 11 on its model (207) so the base's stays; 3 fits */
    bt = digimonExtBuildBattleTable(&blen);
    if (bt != NULL) {
        const unsigned char *s5 = bt + 5 * ROW;
        CHECK(get16(s5 + 10) == get16(gTable + 10) && get16(s5 + 12) == 9);
        CHECK(strstr(log, "\"nm:slot\" field \"techs.2\" rejected: its model (dw3:digimon/207) has no script slot 11") != NULL);
        free(bt);
    }
    /* a model that is a partner form is refused, the base's own model stays */
    CHECK(strstr(log, "\"nm:bad-model\" field \"model\" rejected: an original enemy") != NULL);
    CHECK(strstr(log, "\"nm:zed\" field \"stats.luck\" rejected: unknown key") != NULL);
    CHECK(strstr(log, "\"nm:zed\" field \"ai\" rejected: unknown field") != NULL);
    CHECK(strstr(log, "base\" dw3:digimon/468 is not a partner form or an enemy") != NULL);
    CHECK(strstr(log, "digimon \"nm:nobody\" rejected: \"base\" must be an original Digimon id (3..470)") != NULL);
    CHECK(strstr(log, "\"nm:no-name\" rejected: it needs a title in \"eng\"") != NULL);
    CHECK(strstr(log, "d471/digimon.json rejected: d471 is not an original Digimon") != NULL); /* a new enemy cannot be changed */

    /* the fighters file: original bytes untouched but the header's entries offset; a longer entry list after them */
    ft = digimonExtBuildFighters(&flen);
    CHECK(ft != NULL && flen == 4096); /* the original 2048 bytes, then the longer list: one more sector */
    if (ft != NULL) {
        unsigned start = (unsigned)(ft[4] | (ft[5] << 8) | (ft[6] << 16) | (ft[7] << 24));

        CHECK(start >= FIGHTERS_LEN - 3 && start % 4 == 0 && start + 10 * 4 <= flen);
        CHECK(memcmp(ft, gFighters, 4) == 0 && memcmp(ft + 8, gFighters + 8, FIGHTERS_LEN - 8) == 0);
        CHECK(memcmp(ft + start, gFighters + FE_AT, 5 * 4) == 0); /* the 5 original entries first */
        for (i = 0; i < 4; i++) {
            const unsigned char *e = ft + start + (5 + i) * 4;
            /* alpha (model 207: index 1, kind 0x3B), bad-model (206: 0, 0x3A), slot (207), zed (207) */
            CHECK(get16(e) == 471 + i && e[2] == (i == 1 ? 0 : 1) && e[3] == (i == 1 ? 0x3A : 0x3B));
        }
        CHECK(get16(ft + start + 9 * 4) == 0);
        free(ft);
    }

    /* placements: valid ones kept, a later mod replaces a slot, the rest rejected with a line */
    CHECK(strstr(log, "encounter 34 slot 3 rejected: \"enemy\" is not an enemy") != NULL);
    CHECK(strstr(log, "encounter 34 slot \"4\" rejected: the slot is 1..3") != NULL);
    CHECK(strstr(log, "encounter 34 slot \"x\" rejected: the slot is 1..3") != NULL);
    CHECK(strstr(log, "encounter 35 slot 1 field \"bogus\" rejected: unknown field") != NULL);
    CHECK(strstr(log, "encounter 35 slot 1 field \"level\" rejected: a whole number 1..99") != NULL);
    CHECK(strstr(log, "encounter 35 slot 1 field \"hp\" rejected: a whole number 1..32767") != NULL);
    CHECK(strstr(log, "encounter \"0\" rejected: the encounter is 1..334") != NULL && strstr(log, "encounter \"335\" rejected") != NULL);
    CHECK(strstr(log, "encounter \"bad\" rejected") != NULL && strstr(log, "encounter 36 rejected: an object of slots 1..3") != NULL);
    CHECK(digimonExtPlaceCount() == 4); /* 34/1 (replaced by zz), 34/2, 35/3, 35/1 */
    {
        int enc, slot, found = 0;

        for (i = 0; i < digimonExtPlaceCount(); i++) {
            digimonExtPlaceTarget(i, &enc, &slot);
            memset(&orig, 0, sizeof orig);
            if (enc == 34 && slot == 0) {
                found++;
                CHECK(digimonExtPlaceRow(i, NULL) == NULL); /* zz's names alpha with only a level: an empty slot cannot inherit */
                orig.fighter = 9;
                orig.level = 3;
                orig.hp = 80;
                orig.mp = 6;
                orig.strength = 18;
                row = digimonExtPlaceRow(i, &orig);
                CHECK(row != NULL && row->fighter == 471 && row->level == 7 && row->hp == 80 && row->mp == 6 && row->strength == 18);
            } else if (enc == 34 && slot == 1) {
                found++;
                CHECK(digimonExtPlaceRow(i, NULL) == NULL);
                orig.fighter = 9;
                orig.level = 4;
                orig.hp = 70;
                orig.mp = 2;
                orig.strength = 16;
                row = digimonExtPlaceRow(i, &orig);
                CHECK(row != NULL && row->fighter == 206 && row->level == 4 && row->hp == 70);
            } else if (enc == 35 && slot == 2) {
                found++;
                row = digimonExtPlaceRow(i, NULL);
                CHECK(row != NULL && row->fighter == 0 && row->level == 0 && row->hp == 0); /* "enemy": 0 empties the slot */
            } else if (enc == 35 && slot == 0) {
                found++;
                orig.fighter = 5;
                orig.level = 2;
                orig.hp = 30;
                orig.mp = 1;
                orig.strength = 16;
                row = digimonExtPlaceRow(i, &orig);
                CHECK(row != NULL && row->fighter == 208 && row->level == 2 && row->hp == 30 && row->strength == 9);
            }
        }
        CHECK(found == 4);
    }

    /* reload semantics: everything goes and the files are not extended any more */
    digimonExtReset();
    techExtReset();
    CHECK(digimonExtNewCount() == 0 && digimonExtPlaceCount() == 0 && digimonExtNewId("nm:zed") == -1);
    CHECK(digimonExtBuildBattleTable(NULL) == NULL && digimonExtBuildFighters(NULL) == NULL);
    itemExtReset();
}


/* P2: "new" entries whose base is a partner form: a row in the port's store, an entry in the fighters file, the name outside
   the u8 nameId, and ids shared with the new enemies */
static void testNewPartner(void) {
    const char *log;
    unsigned char *bt, *ft;
    size_t blen = 0, flen = 0;
    int n;
    const DigimonData *base, *fire, *ice, *bad;
    const ContentType *t;

    buildTables();
    modJson("pn", "content");
    putFile("mods/pn/content/techs.json",
            "{\"format\":\"techs-v1\",\"new\":{\"zap\":{\"base\":8,\"script\":5,\"name\":{\"eng\":\"Zap\"}}}}");
    putDigimon("pn",
            "{\"format\":\"digimon-v1\",\"new\":{"
            "\"fire\":{\"base\":3,\"name\":{\"eng\":\"Fire\",\"es-419\":\"Fuego\"},\"description\":{\"eng\":\"d\"},\"hp\":99,"
            "\"stats\":{\"strength\":321,\"luck\":1},\"statusResists\":{\"sleep\":7},\"resistances\":{\"2\":55},"
            "\"skills\":{\"1\":{\"tech\":7,\"level\":12},\"2\":{\"tech\":\"pn:zap\",\"level\":9},\"3\":{\"tech\":9}},\"family\":2,"
            "\"nameId\":4,\"id\":9},"
            "\"ice\":{\"base\":\"dw3:digimon/3\",\"model\":4,\"name\":{\"eng\":\"Ice\"},\"skills\":{\"2\":{\"tech\":12,\"level\":20}}},"
            "\"badmodel\":{\"base\":3,\"model\":206,\"name\":{\"eng\":\"B\"}},"
            "\"ghost\":{\"base\":3,\"model\":99,\"name\":{\"eng\":\"G\"}},"
            "\"enemy-one\":{\"base\":206,\"name\":{\"eng\":\"E\"}}},"
            "\"change\":{\"471\":{\"partner\":{\"hp\":1}}}}");

    logBegin();
    n = loadAll();
    log = logEnd();

    /* one id space with the new enemies: alphabetical by full name from 471 */
    CHECK(n >= 5 && digimonExtNewCount() == 5);
    CHECK(digimonExtNewId("pn:badmodel") == 471 && digimonExtNewId("pn:enemy-one") == 472 && digimonExtNewId("pn:fire") == 473 &&
          digimonExtNewId("pn:ghost") == 474 && digimonExtNewId("pn:ice") == 475);
    CHECK(digimonExtNameId(473) == 253 && digimonExtNameId(475) == 255);
    /* the rows: after the 55 original ones, in the order of the ids, a copy of the base with the overrides */
    CHECK(pcDigimonRowCount() == 55 + 4);
    base = &DIGIMON_DATA_CARD[0];
    fire = pcDigimonRef(56);
    ice = pcDigimonRef(58);
    bad = pcDigimonRef(55);
    CHECK(bad->id == 471 && fire->id == 473 && pcDigimonRef(57)->id == 474 && ice->id == 475);
    CHECK(memcmp(&bad->battleStats, &base->battleStats, sizeof base->battleStats) == 0 && bad->hp == base->hp);
    CHECK(fire->hp == 99 && fire->battleStats[0] == 321 && fire->battleStats[1] == base->battleStats[1] && fire->resistances[2] == 55 &&
          fire->statusResists[3] == 7 && fire->family == 2 && fire->mp == base->mp);
    CHECK(fire->skills[1] == 7 && fire->skillLevels[0] == 12 && fire->skills[2] == techExtIdOf("pn:zap") && fire->skillLevels[1] == 9);
    CHECK(fire->skills[3] == 9 && fire->skillLevels[2] == base->skillLevels[2]); /* tech only: the base's level stays */
    CHECK(fire->nameId == base->nameId && fire->pairPartner == base->pairPartner && fire->skills[4] == base->skills[4]);
    CHECK(pcDigimonNameId(fire) == 253 && pcDigimonNameId(ice) == 255 && pcDigimonNameId(base) == base->nameId);
    CHECK(DIGIMON_DATA_CARD[0].hp == 10); /* "change" cannot name a new id and the original row is never written */
    /* ice reuses the fighter of partner 4, whose effects archive lacks slot 11: technique 12 (needs it) is refused */
    CHECK(ice->skills[2] == base->skills[2] && ice->skillLevels[1] == base->skillLevels[1]);
    CHECK(strstr(log, "\"pn:ice\" field \"skills.2\" rejected: its model (dw3:digimon/4) has no script slot 11") != NULL);
    CHECK(strstr(log, "\"pn:fire\" field \"stats.luck\" rejected: unknown key") != NULL);
    CHECK(strstr(log, "\"pn:fire\" field \"nameId\" rejected: unknown field") != NULL);
    CHECK(strstr(log, "\"pn:fire\" field \"id\" rejected: unknown field") != NULL);
    CHECK(strstr(log, "\"pn:badmodel\" field \"model\" rejected: an original partner form") != NULL);
    CHECK(strstr(log, "\"pn:ghost\" field \"model\" rejected: an original partner form") != NULL);
    CHECK(strstr(log, "d471/digimon.json rejected: d471 is not an original Digimon") != NULL);
    /* names: every new Digimon has a DIGNAM entry, partners and enemies alike */
    t = digimonExtType();
    CHECK(t->count() == 5 && strcmp(t->text(2, "es-419", 0), "Fuego") == 0 && strcmp(t->text(2, "eng", 0), "Fire") == 0 &&
          strcmp(t->text(4, "ger", 0), "Ice") == 0);
    CHECK(digimonExtNewName(473) != NULL && strcmp(digimonExtNewName(473), "pn:fire") == 0);
    CHECK(digimonExtIsPartner(473) == 1 && digimonExtIsPartner(472) == 0 && digimonExtIsPartner(3) == 0 && digimonExtIsPartner(999) == 0);
    CHECK(digimonExtBaseOf(473) == 3 && digimonExtBaseOf(472) == 206 && digimonExtBaseOf(3) == -1);
    /* the battle table has only the enemy (id 472), with its own id and name entry */
    bt = digimonExtBuildBattleTable(&blen);
    CHECK(bt != NULL);
    if (bt != NULL) {
        CHECK(get16(bt + 3 * ROW) == 472 && get16(bt + 3 * ROW + 6) == 252 && get16(bt + 4 * ROW) == 0);
        free(bt);
    }
    /* the fighters file: an entry per new Digimon; a partner's is {id, index, kind} of its model, shared, no info row added */
    ft = digimonExtBuildFighters(&flen);
    CHECK(ft != NULL);
    if (ft != NULL) {
        unsigned start = (unsigned)(ft[4] | (ft[5] << 8) | (ft[6] << 16) | (ft[7] << 24));
        const unsigned char *e = ft + start + 5 * 4;

        CHECK(get16(e) == 471 && e[2] == 0 && e[3] == 5);          /* badmodel: its base's own model (partner 3: index 0, kind 5) */
        CHECK(get16(e + 4) == 472 && e[6] == 0 && e[7] == 0x3A);   /* the enemy */
        CHECK(get16(e + 8) == 473 && e[10] == 0 && e[11] == 5);    /* fire: the base's model */
        CHECK(get16(e + 12) == 474 && e[14] == 0 && e[15] == 5);   /* ghost: model 99 does not exist */
        CHECK(get16(e + 16) == 475 && e[18] == 1 && e[19] == 6);   /* ice: partner 4's model */
        CHECK(get16(e + 20) == 0);
        CHECK(ft[8] == gFighters[8] && memcmp(ft + 8, gFighters + 8, FIGHTERS_LEN - 8) == 0); /* the partner infos stay as they were */
        free(ft);
    }
    /* reload: the rows go, the original table is as it was */
    unloadAll();
    CHECK(pcDigimonRowCount() == 55 && digimonExtNewCount() == 0 && DIGIMON_DATA_CARD[0].hp == 10);
    itemExtReset();
}

/* ---- how a new partner form is obtained: "evolution" (P3) ---- */

static PcEvolution gEvoOrig[PARTNER_COUNT][PC_EVOLUTION_ORIG_COUNT];

static void testEvolution(void) {
    const char *log;
    PcEvolution *l0, *l2;
    int n;

    buildTables();
    pcEvolutionSetOriginal(gEvoOrig);
    modJson("pe", "content");
    putDigimon("pe",
            "{\"format\":\"digimon-v1\",\"new\":{"
            /* row 55: partner 0, needs the rookie (id 3, row 0) at 5, the partner's level 10 */
            "\"a\":{\"base\":3,\"name\":{\"eng\":\"A\"},\"evolution\":{\"partner\":0,"
            "\"needs\":[{\"digimon\":\"dw3:digimon/3\",\"level\":5}],\"stat\":\"level\",\"value\":10}},"
            /* row 56: two partners, one needs the new form a (row 55) and a second original form, the other a stat by number */
            "\"b\":{\"base\":4,\"name\":{\"eng\":\"B\"},\"evolution\":["
            "{\"partner\":2,\"needs\":[{\"digimon\":\"pe:a\",\"level\":7},{\"digimon\":\"dw3:digimon/5\"}]},"
            "{\"partner\":\"dw3:digimon/6\",\"stat\":9,\"value\":300}]},"
            /* rejected: an enemy need, a bad partner, a bad stat, three needs, no partner, an unknown key, an enemy form */
            "\"c\":{\"base\":3,\"name\":{\"eng\":\"C\"},\"evolution\":["
            "{\"partner\":1,\"needs\":[{\"digimon\":\"dw3:digimon/206\"}]},"
            "{\"partner\":8},{\"partner\":1,\"stat\":15},"
            "{\"partner\":1,\"needs\":[{\"digimon\":\"dw3:digimon/3\"},{\"digimon\":\"dw3:digimon/3\"},{\"digimon\":\"dw3:digimon/3\"}]},"
            "{\"stat\":1},{\"partner\":1,\"luck\":1},{\"partner\":1,\"needs\":{\"digimon\":\"dw3:digimon/4\",\"level\":99},\"value\":4}]},"
            "\"e\":{\"base\":206,\"name\":{\"eng\":\"E\"},\"evolution\":{\"partner\":0}}}}");

    logBegin();
    n = loadAll();
    log = logEnd();
    CHECK(n >= 4 && digimonExtNewId("pe:a") == 471 && digimonExtNewId("pe:b") == 472 && digimonExtNewId("pe:c") == 473);
    CHECK(pcDigimonRef(55)->id == 471 && pcDigimonRef(56)->id == 472 && pcDigimonRef(57)->id == 473);

    /* partner 0: the original 44, then a (row 55 -> digimon 56) in the list the game reads; the original array is untouched */
    CHECK(pcEvolutionCount(0) == 45 && pcEvolutionCount(1) == 45 && pcEvolutionCount(2) == 45 && pcEvolutionCount(3) == 45 &&
          pcEvolutionCount(4) == 44);
    l0 = pcEvolutionList(0, gEvoOrig[0]);
    CHECK(l0 != gEvoOrig[0] && l0[44].digimon == 56 && l0[44].needs[0].digimon == 1 && l0[44].needs[0].level == 5);
    CHECK(l0[44].needs[1].digimon == 0 && l0[44].needs[1].level == 1 && l0[44].stat == 7 && l0[44].value == 10);
    CHECK(gEvoOrig[0][44 - 1].digimon == 0);
    /* partner 2: b (row 56 -> 57) needs a (row 55 -> 56) at 7 and the original form of id 5 (row 2 -> 3) at 1 */
    l2 = pcEvolutionList(2, gEvoOrig[2]);
    CHECK(l2[44].digimon == 57 && l2[44].needs[0].digimon == 56 && l2[44].needs[0].level == 7 && l2[44].needs[1].digimon == 3 &&
          l2[44].needs[1].level == 1 && l2[44].stat == 0 && l2[44].value == 1);
    /* the second entry of b: "dw3:digimon/6" is row 3, partner 3; stat 9 value 300 */
    {
        PcEvolution *l3 = pcEvolutionList(3, gEvoOrig[3]);

        CHECK(l3[44].digimon == 57 && l3[44].needs[0].digimon == 0 && l3[44].stat == 9 && l3[44].value == 300);
    }
    /* c: only the valid entry survives: partner 1 with the need dw3:digimon/4 at 99 and "value" without a stat -> 1 */
    {
        PcEvolution *l1 = pcEvolutionList(1, gEvoOrig[1]);
        CHECK(pcEvolutionCount(1) == 45 && l1[44].digimon == 58 && l1[44].needs[0].digimon == 2 && l1[44].needs[0].level == 99 &&
              l1[44].stat == 0 && l1[44].value == 1);
    }
    CHECK(strstr(log, "\"pe:c\" evolution need \"dw3:digimon/206\" rejected: not a partner form") != NULL);
    CHECK(strstr(log, "\"pe:c\" evolution \"partner\" rejected") != NULL);
    CHECK(strstr(log, "\"pe:c\" evolution \"stat\" rejected") != NULL);
    CHECK(strstr(log, "\"pe:c\" evolution \"needs\" rejected: up to 2") != NULL);
    CHECK(strstr(log, "\"pe:c\" evolution rejected: it needs a \"partner\"") != NULL);
    CHECK(strstr(log, "\"pe:c\" evolution key \"luck\" rejected") != NULL);
    CHECK(strstr(log, "\"pe:e\" field \"evolution\" rejected: only a partner form") != NULL);

    /* a reload forgets the entries: the lists are the original ones again */
    unloadAll();
    CHECK(pcEvolutionCount(0) == 44 && pcEvolutionList(0, gEvoOrig[0]) == (void *)gEvoOrig[0]);
    itemExtReset();
    pcEvolutionSetOriginal(NULL);
}

/* ---- rows past 254 (U9): evolution entries and requirements keep row + 1 in 16/32 bits, not a byte ---- */

static void testWideRows(void) {
    static char json[40000];
    const char *log;
    PcEvolution *l0;
    int i, n, len;

    buildTables();
    pcEvolutionSetOriginal(gEvoOrig);
    modJson("pw", "content");
    len = snprintf(json, sizeof json, "{\"format\":\"digimon-v1\",\"new\":{");
    for (i = 0; i < 300; i++) {
        len += snprintf(json + len, sizeof json - (size_t)len, "%s\"f%03d\":{\"base\":%d,\"name\":{\"eng\":\"F%d\"}", i ? "," : "", i,
                        3 + i % 2, i);
        /* f298 (row 353) is learned by partner 0 and needs f299 (row 354) and f270 (row 325); f299 by partner 1 needing f001 (row 56) */
        if (i == 250) { /* row 305: blast forms past a byte (one original, rows 354/355 new) and a pair partner */
            len += snprintf(json + len, sizeof json - (size_t)len, ",\"blastForms\":[\"dw3:digimon/3\",\"pw:f299\",\"pw:f298\",\"pw:f270\","
                            "\"dw3:digimon/4\"],\"pairPartner\":\"pw:f299\"");
        } else if (i == 251) { /* every bad shape: an enemy, an unknown name, four entries, a bad pair */
            len += snprintf(json + len, sizeof json - (size_t)len, ",\"blastForms\":[\"dw3:digimon/206\",\"pw:f299\",\"pw:f298\",\"pw:f270\","
                            "\"dw3:digimon/4\"],\"pairPartner\":\"pw:nope\"");
        } else if (i == 252) {
            len += snprintf(json + len, sizeof json - (size_t)len, ",\"blastForms\":[\"dw3:digimon/3\",\"pw:f299\"]");
        }
        if (i == 298) {
            len += snprintf(json + len, sizeof json - (size_t)len, ",\"evolution\":{\"partner\":0,\"needs\":[{\"digimon\":\"pw:f299\",\"level\":3},"
                            "{\"digimon\":\"pw:f270\",\"level\":4}]}");
        } else if (i == 299) {
            len += snprintf(json + len, sizeof json - (size_t)len, ",\"evolution\":{\"partner\":1,\"needs\":[{\"digimon\":\"pw:f001\"}]}");
        }
        len += snprintf(json + len, sizeof json - (size_t)len, "}");
    }
    snprintf(json + len, sizeof json - (size_t)len, "}}");
    putDigimon("pw", json);

    logBegin();
    n = loadAll();
    log = logEnd();
    CHECK(n >= 300 && digimonExtNewCount() == 300);
    CHECK(pcDigimonRowCount() == 55 + 300);
    CHECK(pcDigimonRef(354)->id == digimonExtNewId("pw:f299") && pcDigimonRef(353)->id == digimonExtNewId("pw:f298"));
    l0 = pcEvolutionList(0, gEvoOrig[0]);
    CHECK(pcEvolutionCount(0) == 45 && l0[44].digimon == 354 && l0[44].needs[0].digimon == 355 && l0[44].needs[1].digimon == 326);
    CHECK(pcEvolutionCount(1) == 45 && ((PcEvolution *)pcEvolutionList(1, gEvoOrig[1]))[44].digimon == 355);
    /* f250 (row 305): its blast forms reach rows 354, 353 and 325 and its pair partner row 354, none of which fits a byte;
       the originals' values are the u8 of the disc */
    {
        const DigimonData *g = pcDigimonRef(305), *bad = pcDigimonRef(306), *short5 = pcDigimonRef(307);
        int t;

        CHECK(g->id == digimonExtNewId("pw:f250") && pcDigimonBlast(g, 0) == 1 && pcDigimonBlast(g, 1) == 355 &&
              pcDigimonBlast(g, 2) == 354 && pcDigimonBlast(g, 3) == 326 && pcDigimonBlast(g, 4) == 2);
        CHECK(pcDigimonPair(g) == 355);
        /* the rejected declarations leave the base's own values */
        for (t = 0; t < 5; t++) CHECK(pcDigimonBlast(bad, t) == bad->blastForms[t] && pcDigimonBlast(short5, t) == short5->blastForms[t]);
        CHECK(pcDigimonPair(bad) == bad->pairPartner);
        CHECK(strstr(log, "\"pw:f251\" field \"blastForms\" rejected") != NULL && strstr(log, "\"pw:f251\" field \"pairPartner\" rejected") != NULL);
        CHECK(strstr(log, "\"pw:f252\" field \"blastForms\" rejected") != NULL);
        /* an original row without a side value reads its u8 */
        CHECK(pcDigimonBlast(&DIGIMON_DATA_CARD[3], 2) == DIGIMON_DATA_CARD[3].blastForms[2] &&
              pcDigimonPair(&DIGIMON_DATA_CARD[3]) == DIGIMON_DATA_CARD[3].pairPartner);
        CHECK(pcDigimonRefOf(g) == 306 && pcDigimonRefOf(&DIGIMON_DATA_CARD[3]) == DIGIMON_DATA_CARD[3].nameId);
    }
    CHECK(strstr(log, "\"pw:f298\"") == NULL && strstr(log, "evolution") == NULL);
    CHECK(digimonExtRowRef("pw:f299") == 355 && digimonExtRowRef("pw:f270") == 326 && digimonExtRowRef("pw:nope") == -1);
    unloadAll();
    CHECK(pcDigimonRowCount() == 55);
    itemExtReset();
    pcEvolutionSetOriginal(NULL);
}

/* ---- own FighterInfo rows (model-import I3): a new Digimon whose "model" is an object gets its own info row ---- */

static int resolveName(const char *name) {
    if (strcmp(name, "om:mot") == 0) return 7001;
    if (strcmp(name, "om:virt") == 0) return 8000;
    return -1;
}

static unsigned get32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void put32at(unsigned char *p, unsigned v) {
    put16(p, (int)(v & 0xFFFF));
    put16(p + 2, (int)(v >> 16));
}

#define PINFO(i) (gFighters + 64 + (i) * 0xC4)
#define EINFO(i) (gFighters + 64 + 2 * 0xC4 + (i) * 0x48)

static void testOwnModel(void) {
    const char *log;
    unsigned char *ft;
    size_t flen = 0;
    int i;
    char json[40000];
    size_t at;

    buildTables();
    buildFighters();
    for (i = 0; i < 2; i++) {          /* distinct refs, cameras and heights in every original info row */
        put32at(PINFO(i), 0x04200000u + 0x10 * (unsigned)i);
        put32at(PINFO(i) + 4, 0x04210000u + (unsigned)i);
        put32at(PINFO(i) + 8, 0x04220000u + (unsigned)i);
        put32at(PINFO(i) + 12, 0x00100000u + 0x300 + 10 * (unsigned)i);
        put16(PINFO(i) + 0x18, 400 + i);
        PINFO(i)[0x1A] = (unsigned char)(0x70 + i);
        PINFO(i)[0xAA] = (unsigned char)(0x60 + i);
    }
    for (i = 0; i < 3; i++) {
        put32at(EINFO(i), 0x05200000u + 0x10 * (unsigned)i);
        put32at(EINFO(i) + 4, 0x05210000u + (unsigned)i);
        put32at(EINFO(i) + 8, 0x05220000u + (unsigned)i);
        put32at(EINFO(i) + 12, 0x00100000u + 0x400 + 10 * (unsigned)i);
        put16(EINFO(i) + 0x10, 90 + i);
        put16(EINFO(i) + 0x18, 300 + i);
        EINFO(i)[0x1A] = (unsigned char)(0x50 + i);
        put16(EINFO(i) + 0x44, 3);
    }
    modJson("om", "content");
    putDigimon("om",
            "{\"format\":\"digimon-v1\",\"new\":{"
            "\"orc\":{\"base\":206,\"name\":{\"eng\":\"Orc\"},\"model\":{\"from\":207,\"model\":{\"file\":7000,\"entry\":3},"
            "\"motions\":{\"file\":\"om:mot\",\"entry\":1},\"effects\":\"dw3:digimon/208\",\"height\":123,\"distance\":-50}},"
            "\"pim\":{\"base\":3,\"name\":{\"eng\":\"Pim\"},\"model\":{\"from\":4,\"model\":{\"file\":\"om:virt\"}}},"
            "\"alias\":{\"base\":206,\"name\":{\"eng\":\"Alias\"},\"model\":207},"
            "\"bad\":{\"base\":206,\"name\":{\"eng\":\"Bad\"},\"model\":{\"model\":{\"file\":\"nobody:none\"}}},"
            "\"badkey\":{\"base\":206,\"name\":{\"eng\":\"BadKey\"},\"model\":{\"zzz\":1,\"height\":\"x\",\"model\":{\"file\":0}}}}}");

    digimonExtSetFileResolver(resolveName);
    logBegin();
    loadAll();
    ft = digimonExtBuildFighters(&flen);
    log = logEnd();
    CHECK(digimonExtNewId("om:alias") == 471 && digimonExtNewId("om:bad") == 472 && digimonExtNewId("om:badkey") == 473 &&
          digimonExtNewId("om:orc") == 474 && digimonExtNewId("om:pim") == 475);
    CHECK(ft != NULL);
    if (ft != NULL) {
        unsigned start = get32(ft + 4), pStart = get32(ft + 8), eStart = get32(ft + 12), base = get32(gFighters);
        const unsigned char *e = ft + start + 5 * 4, *orc, *pim, *bk, *face;

        CHECK(memcmp(ft, gFighters, 4) == 0 && memcmp(ft + 16, gFighters + 16, FIGHTERS_LEN - 16) == 0); /* the original bytes stay */
        CHECK(pStart >= FIGHTERS_LEN && eStart >= pStart + 3 * 0xC4 && flen >= eStart + 5 * 0x48);
        /* entries: alias and bad share a model; badkey, orc (enemies) and pim (partner) have their own rows after the originals */
        CHECK(get16(e) == 471 && e[2] == 1 && e[3] == 0x3B);
        CHECK(get16(e + 4) == 472 && e[6] == 0 && e[7] == 0x3A);
        CHECK(get16(e + 8) == 473 && e[10] == 3 && e[11] == 0x3A);
        CHECK(get16(e + 12) == 474 && e[14] == 4 && e[15] == 0x3B);
        CHECK(get16(e + 16) == 475 && e[18] == 2 && e[19] == 6);
        CHECK(get16(e + 20) == 0);
        /* the originals first, in the copy, byte for byte */
        CHECK(memcmp(ft + pStart, gFighters + 64, 2 * 0xC4) == 0 && memcmp(ft + eStart, EINFO(0), 3 * 0x48) == 0);
        pim = ft + pStart + 2 * 0xC4;
        bk = ft + eStart + 3 * 0x48;
        orc = ft + eStart + 4 * 0x48;
        face = ft + (get32(orc + 12) - base);
        CHECK(face[0] == 0xFF && get32(orc + 12) == get32(pim + 12) && get32(orc + 12) == get32(bk + 12)); /* an empty list, shared */
        /* orc: template 207 (row 1) with its own model, motions by name, 208's effects, height and distance */
        CHECK(get32(orc) == (7000u << 16 | 3) && get32(orc + 4) == (7001u << 16 | 1) && get32(orc + 8) == get32(EINFO(2) + 8));
        CHECK(get16(orc + 0x18) == 123 && get16(orc + 0x10) == -50 && orc[0x1A] == 0x51 && get16(orc + 0x44) == 3);
        /* pim: template partner 4 (row 1): the model by name, motions and effects its own */
        CHECK(get32(pim) == 8000u << 16 && get32(pim + 4) == get32(PINFO(1) + 4) && get32(pim + 8) == get32(PINFO(1) + 8));
        CHECK(get16(pim + 0x18) == 401 && pim[0x1A] == 0x71 && pim[0xAA] == 0x61);
        /* badkey: the template is the base (206, row 0); its "model" file 0 is no file */
        CHECK(get32(bk + 4) == get32(EINFO(0) + 4) && get16(bk + 0x18) == 300);
        free(ft);
    }
    CHECK(strstr(log, "\"om:badkey\" field \"model.zzz\" rejected: unknown key") != NULL);
    CHECK(strstr(log, "\"om:badkey\" field \"model.height\" rejected") != NULL);
    CHECK(strstr(log, "\"om:bad\" has no own model: \"nobody:none\" is not a file") != NULL);
    unloadAll();

    /* the u8 index: 3 original enemy rows, so 253 own rows fit; the rest keep sharing the template's row */
    at = 0;
    at += (size_t)snprintf(json + at, sizeof json - at, "{\"format\":\"digimon-v1\",\"new\":{");
    for (i = 0; i < 260; i++) {
        at += (size_t)snprintf(json + at, sizeof json - at,
                               "%s\"e%03d\":{\"base\":206,\"name\":{\"eng\":\"E%d\"},\"model\":{\"model\":{\"file\":%d}}}", i ? "," : "", i, i,
                               9000 + i);
    }
    snprintf(json + at, sizeof json - at, "}}");
    putDigimon("om", json);
    logBegin();
    loadAll();
    ft = digimonExtBuildFighters(&flen);
    log = logEnd();
    CHECK(digimonExtNewCount() == 260 && ft != NULL);
    if (ft != NULL) {
        unsigned start = get32(ft + 4), eStart = get32(ft + 12);
        const unsigned char *e = ft + start + 5 * 4;

        CHECK(get16(e + 4 * 252) == 471 + 252 && e[4 * 252 + 2] == 255);
        CHECK(get32(ft + eStart + 255 * 0x48) == (9252u << 16));
        CHECK(get16(e + 4 * 253) == 471 + 253 && e[4 * 253 + 2] == 0 && e[4 * 259 + 2] == 0); /* the template's own row */
        free(ft);
    }
    CHECK(strstr(log, "e253\" has no own model: the enemies' info table is full (256 rows)") != NULL);
    unloadAll();
    digimonExtSetFileResolver(NULL);

    /* without an object in "model" nothing changes: the header keeps its offsets (testNew checks the bytes) */
    putDigimon("om",
            "{\"format\":\"digimon-v1\",\"new\":{\"a\":{\"base\":206,\"name\":{\"eng\":\"A\"},\"model\":207}}}");
    loadAll();
    ft = digimonExtBuildFighters(&flen);
    CHECK(ft != NULL && memcmp(ft + 8, gFighters + 8, 8) == 0 && flen == 4096);
    free(ft);
    unloadAll();
    itemExtReset();
    buildFighters();
}

/* "model": "<mod>:<key>": a model source of the mod (glTF + PNG + model.json) is baked when the fighters file is built, and the
   Digimon gets its own info row from it (model-import I5); "dw3:model/<id>" shares that fighter's row */
static int countOf(const char *hay, const char *needle) {
    int n = 0;
    const char *p = hay;

    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

static void testSourceModel(void) {
    const char *log;
    unsigned char *ft;
    size_t flen = 0, elen = 0, glen = 0;
    char cmd[2000], path[900];
    unsigned char *expect;
    FILE *f;
    int i;

    buildTables();
    buildFighters();
    for (i = 0; i < 3; i++) { /* markers in the originals' rows: the effects, the height, the byte of the template */
        put32at(EINFO(i) + 8, 0x05220000u + (unsigned)i);
        put16(EINFO(i) + 0x18, 300 + i);
        EINFO(i)[0x1A] = (unsigned char)(0x50 + i);
    }
    {
        char user[800];
        snprintf(user, sizeof user, "%s/user", gRoot);
        setenv("DW3_USER_DIR", user, 1);
        unsetenv("DW3_CACHE_DIR");
        portPathsReset();
    }
    pfsInit(NULL, 3000); /* no disc: the virtual file numbers start at 3000 */
    pfsClearVirtualFiles();
    modJson("sm", "content");
    putDigimon("sm",
            "{\"format\":\"digimon-v1\",\"new\":{"
            "\"alias\":{\"base\":206,\"name\":{\"eng\":\"Alias\"},\"model\":\"dw3:model/207\"},"
            "\"bot\":{\"base\":206,\"name\":{\"eng\":\"Bot\"},\"model\":\"sm:robot\"},"
            "\"missing\":{\"base\":206,\"name\":{\"eng\":\"Missing\"},\"model\":\"sm:nothing\"},"
            "\"pal\":{\"base\":3,\"name\":{\"eng\":\"Pal\"},\"model\":\"sm:pal\"},"
            "\"zeb\":{\"base\":206,\"name\":{\"eng\":\"Zeb\"},\"model\":\"sm:zeb\"}}}");
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/mods/sm/digimon/robot/model' && cp test_fixtures/model/robot/m.* '%s/mods/sm/digimon/robot/model/'", gRoot, gRoot);
    if (system(cmd)) {}
    /* P8: models with no "fill": a partner's (template 4) and an enemy's (template 207) leave empty the slots the battle code plays; one line each */
    for (i = 0; i < 2; i++) {
        snprintf(cmd, sizeof cmd, "mkdir -p '%s/mods/sm/digimon/%s/model' && cp test_fixtures/model/robot/m.* '%s/mods/sm/digimon/%s/model/'", gRoot,
                 i == 0 ? "pal" : "zeb", gRoot, i == 0 ? "pal" : "zeb");
        if (system(cmd)) {}
    }
    putFile("mods/sm/digimon/pal/model/model.json",
            "{\"format\":\"model-src-v1\",\"name\":\"pal\",\"gltf\":\"m.gltf\",\"fitHeight\":1800,\"template\":\"dw3:digimon/4\","
            "\"effects\":\"dw3:digimon/3\",\"motions\":{\"0\":\"idle\",\"1\":\"idle\",\"2\":\"idle\",\"4\":\"idle\",\"10\":\"idle\",\"11\":\"idle\",\"12\":\"idle\"}}");
    putFile("mods/sm/digimon/zeb/model/model.json",
            "{\"format\":\"model-src-v1\",\"name\":\"zeb\",\"gltf\":\"m.gltf\",\"fitHeight\":1800,\"template\":\"dw3:digimon/207\","
            "\"effects\":\"dw3:digimon/208\",\"motions\":{\"0\":\"idle\",\"1\":\"idle\",\"2\":\"idle\",\"3\":\"idle\",\"4\":\"idle\",\"10\":\"idle\","
            "\"11\":\"idle\",\"12\":\"idle\"}}");
    putFile("mods/sm/digimon/robot/model/model.json",
            "{\"format\":\"model-src-v1\",\"name\":\"robot\",\"gltf\":\"m.gltf\",\"fitHeight\":1800,\"template\":\"dw3:digimon/207\","
            "\"effects\":\"dw3:digimon/208\",\"info\":{\"distance\":-321,\"height\":77},\"motions\":{\"0\":\"idle\",\"1\":\"idle\"},\"fill\":\"idle\"}");
    snprintf(path, sizeof path, "test_fixtures/model/robot/expected.bin");
    expect = NULL;
    f = fopen(path, "rb");
    if (f != NULL) {
        expect = (unsigned char *)malloc(1 << 20);
        elen = fread(expect, 1, 1 << 20, f);
        fclose(f);
    }
    digimonExtSetFileResolver(NULL); /* the default: model sources and raw files */
    logBegin();
    loadAll();
    ft = digimonExtBuildFighters(&flen);
    log = logEnd();
    CHECK(digimonExtNewId("sm:alias") == 471 && digimonExtNewId("sm:bot") == 472 && digimonExtNewId("sm:missing") == 473);
    CHECK(ft != NULL && expect != NULL);
    if (ft != NULL && expect != NULL) {
        unsigned start = get32(ft + 4), eStart = get32(ft + 12);
        const unsigned char *e = ft + start + 5 * 4, *bot;
        const unsigned char *served;
        int file;

        CHECK(get16(e) == 471 && e[2] == 1 && e[3] == 0x3B);      /* alias: 207's own row */
        CHECK(get16(e + 4) == 472 && e[6] == 3 && e[7] == 0x3B);  /* bot: a row of its own, the kind of its template (207) */
        CHECK(get16(e + 8) == 473 && e[10] == 0 && e[11] == 0x3A); /* missing: the base's (206) row */
        bot = ft + eStart + 3 * 0x48;
        file = (int)(get32(bot) >> 16);
        CHECK(file >= 3000 && file == (int)(get32(bot + 4) >> 16) && (get32(bot) & 0xFFFF) == 1 && (get32(bot + 4) & 0xFFFF) == 0);
        CHECK(get32(bot + 8) == 0x05220002u);                     /* the effects of the donor, 208 */
        CHECK(get16(bot + 0x18) == 77 && get16(bot + 0x10) == -321 && bot[0x1A] == 0x51); /* height, distance, the rest is 207's */
        served = pfsResolve(file, &glen, 0, NULL);
        CHECK(served != NULL && glen == elen && memcmp(served, expect, elen) == 0);
        CHECK(get32(ft + eStart + 0 * 0x48) == get32(EINFO(0)));  /* the originals stay */
        free(ft);
    }
    CHECK(strstr(log, "[models] sm:robot: baked 5 bones") != NULL);
    /* P8: a partner's model lists each empty slot its battle code plays (3, 13, 14, 20, 32), an enemy's only the attack; "fill" covers them all (the robot) */
    CHECK(digimonExtNewId("sm:pal") == 474 && digimonExtNewId("sm:zeb") == 475);
    CHECK(countOf(log, "[digimon] sm:pal: motion slot ") == 5 && countOf(log, "[digimon] sm:zeb: motion slot ") == 1);
    CHECK(strstr(log, "[digimon] sm:pal: motion slot 3 (flinch) has no animation: ") != NULL);
    CHECK(strstr(log, "[digimon] sm:pal: motion slot 13 (blast entrance) has no animation: ") != NULL);
    CHECK(strstr(log, "[digimon] sm:pal: motion slot 14 (attack) has no animation: ") != NULL);
    CHECK(strstr(log, "[digimon] sm:pal: motion slot 20 (attack, script 6) has no animation: ") != NULL);
    CHECK(strstr(log, "[digimon] sm:pal: motion slot 32 (attack, script 8) has no animation: ") != NULL);
    CHECK(strstr(log, "[digimon] sm:zeb: motion slot 14 (attack) has no animation: ") != NULL);
    CHECK(strstr(log, "sm:pal: motion slot 0 ") == NULL && strstr(log, "sm:pal: motion slot 12 ") == NULL && strstr(log, "sm:robot: motion slot") == NULL);
    CHECK(strstr(log, "\"sm:missing\" field \"model\" rejected: no model source \"sm:nothing\"") != NULL);
    unloadAll();
    pfsClearVirtualFiles();
    free(expect);
    unsetenv("DW3_USER_DIR");
    portPathsReset();
    itemExtReset();
    buildFighters();
}

/* P13: a model that takes an original fighter's motions by reference. The donor's archive is read from the disc (digimonExtSetDiscReader), each
   slot its table leaves empty that the battle code plays is one warning line, and "fill" copies a slot's offset into every empty slot of a
   copy of the file (a virtual file) so the game never reads an empty slot. Here a fake disc: file 0x600, entry 0, 62 slots. */
#define BYREF_FILE 0x600
static unsigned char *fakeDisc(int file, size_t *len) {
    static const int have[] = {0, 1, 2, 4, 10, 11, 12, 14};
    unsigned char *d;
    size_t i;

    if (file != BYREF_FILE) return NULL;
    d = (unsigned char *)calloc(1, 2048);
    put32at(d, 4); /* entry 0: the archive at 4 */
    for (i = 0; i < sizeof have / sizeof have[0]; i++) put32at(d + 4 + 4 * have[i], (unsigned)(248 + 16 * i));
    for (i = 4 + 248; i < 2048; i++) d[i] = (unsigned char)i;
    /* motion steps {index, count, frame, endFrame} that end holding a pose (frame 0x8000 loops the tail: an enemy's idle, entrance and
       attack), each list closed by index 0x7FFF: slot 0 at 252, slot 12 at 348, slot 14 at 364 (absolute: the archive starts at 4) */
    for (i = 0; i < 3; i++) {
        static const int at[3] = {252, 348, 364};
        static const unsigned short steps[2][4] = {{0, 10, 0, 0}, {10, 0, 0x8000, 66}};
        int k, j;

        for (k = 0; k < 2; k++) {
            for (j = 0; j < 4; j++) d[at[i] + 8 * k + 2 * j] = (unsigned char)steps[k][j], d[at[i] + 8 * k + 2 * j + 1] = (unsigned char)(steps[k][j] >> 8);
        }
        d[at[i] + 16] = 0xFF;
        d[at[i] + 17] = 0x7F;
        d[at[i] + 18] = d[at[i] + 19] = 0;
    }
    d[332] = 0xFF; /* slot 11's list (filler bytes) closes before slot 12's */
    d[333] = 0x7F;
    *len = 2048;
    return d;
}

static void testByReference(void) {
    const char *log;
    unsigned char *ft;
    size_t flen = 0, glen = 0;
    int i;

    buildTables();
    buildFighters();
    for (i = 0; i < 3; i++) {
        put32at(EINFO(i), 0x05200000u + 0x10 * (unsigned)i);
        put32at(EINFO(i) + 4, 0x05210000u + (unsigned)i);
        put32at(EINFO(i) + 8, 0x05220000u + (unsigned)i);
    }
    put32at(EINFO(1) + 4, (unsigned)BYREF_FILE << 16); /* fighter 207's motions: the fake file */
    pfsInit(NULL, 3000);
    pfsClearVirtualFiles();
    modJson("br", "content");
    putDigimon("br",
            "{\"format\":\"digimon-v1\",\"new\":{"
            "\"raw\":{\"base\":3,\"name\":{\"eng\":\"Raw\"},\"model\":{\"from\":4,\"model\":\"dw3:digimon/207\",\"motions\":\"dw3:digimon/207\",\"effects\":\"dw3:digimon/207\"}},"
            "\"fil\":{\"base\":3,\"name\":{\"eng\":\"Fil\"},\"model\":{\"from\":4,\"model\":\"dw3:digimon/207\",\"motions\":\"dw3:digimon/207\",\"effects\":\"dw3:digimon/207\",\"fill\":14}},"
            "\"nof\":{\"base\":3,\"name\":{\"eng\":\"Nof\"},\"model\":{\"from\":4,\"motions\":\"dw3:digimon/207\",\"fill\":7}},"
            "\"big\":{\"base\":3,\"name\":{\"eng\":\"Big\"},\"model\":{\"from\":4,\"motions\":\"dw3:digimon/207\",\"fill\":99}},"
            "\"foe\":{\"base\":206,\"name\":{\"eng\":\"Foe\"},\"model\":{\"from\":207,\"motions\":\"dw3:digimon/207\"}}}}");
    digimonExtSetDiscReader(fakeDisc);
    logBegin();
    loadAll();
    ft = digimonExtBuildFighters(&flen);
    log = logEnd();
    CHECK(digimonExtNewId("br:big") == 471 && digimonExtNewId("br:fil") == 472 && digimonExtNewId("br:raw") == 475);
    /* a partner's form without "fill": the slots 3, 13, 20, 32 (the donor has 0, 1, 2, 4, 10, 11, 12, 14), one line each; with "fill" none */
    CHECK(countOf(log, "[digimon] br:raw: motion slot ") == 4);
    CHECK(strstr(log, "[digimon] br:raw: motion slot 3 (flinch) has no animation: ") != NULL);
    CHECK(strstr(log, "[digimon] br:raw: motion slot 13 (blast entrance) has no animation: ") != NULL);
    CHECK(strstr(log, "[digimon] br:raw: motion slot 20 (attack, script 6) has no animation: ") != NULL);
    CHECK(strstr(log, "[digimon] br:raw: motion slot 32 (attack, script 8) has no animation: ") != NULL);
    CHECK(strstr(log, "br:fil: motion slot") == NULL);
    /* "fill" 7 (empty in the donor) and 99 (outside the table) are rejected and leave the slots empty (the warnings stay) */
    CHECK(strstr(log, "model.fill\" rejected") != NULL && countOf(log, "model.fill\" rejected") == 2);
    CHECK(countOf(log, "[digimon] br:nof: motion slot ") == 4);
    /* an enemy's facet does not need 13, 20 and 32: only the flinch (3) */
    CHECK(countOf(log, "[digimon] br:foe: motion slot ") == 1 && strstr(log, "br:foe: motion slot 3 (flinch)") != NULL);
    CHECK(ft != NULL);
    if (ft != NULL) {
        unsigned pStart = get32(ft + 8);
        const unsigned char *raw = ft + pStart + 5 * 0xC4, *fil = ft + pStart + 3 * 0xC4, *served;
        unsigned ref;
        int file;

        {
            unsigned rref = get32(raw + 4);
            size_t rlen = 0;
            const unsigned char *rs = (rref >> 16) >= 3000 ? pfsResolve((int)(rref >> 16), &rlen, 0, NULL) : NULL;

            /* without "fill" a partner's form still gets its own copy, where the looping motions end (the slots stay empty) */
            CHECK(rs != NULL && rlen == 2048 && get32(rs + get32(rs) + 4 * 3) == 0);
            if (rs != NULL && rlen == 2048) CHECK(get16(rs + 364 + 12) == -1);
        }
        ref = get32(fil + 4);
        file = (int)(ref >> 16);
        CHECK(file >= 3000 && (ref & 0xFFFF) == 0);
        served = file >= 3000 ? pfsResolve(file, &glen, 0, NULL) : NULL;
        CHECK(served != NULL && glen == 2048);
        if (served != NULL && glen == 2048) {
            unsigned at = get32(served), want = get32(served + at + 4 * 14);
            int ok = 1;

            for (i = 0; i < 62; i++) {
                unsigned v = get32(served + at + 4 * i);
                if (i == 0 || i == 1 || i == 2 || i == 4 || i == 10 || i == 11 || i == 12) continue;
                if (v != want) ok = 0; /* every other slot, 14 included, plays slot 14's */
            }
            CHECK(ok && want == 248 + 16 * 7);
            /* a partner's script waits for each motion to end (keyframe 0xFFFF); the enemy's attack loops (0x8000), so the copy ends it,
               while the idle (0) and the entrance/victory pose (12) keep holding */
            CHECK(get16(served + 364 + 12) == -1 && get16(served + 364 + 14) == 0);
            CHECK(get16(served + 252 + 12) == -32768 && get16(served + 348 + 12) == -32768);
            CHECK(get32(served + at + 4 * 4) == 248 + 16 * 3 && get32(served + at + 4 * 12) == 248 + 16 * 6); /* the donor's own slots stay */
        }
        free(ft);
    }
    unloadAll();
    digimonExtSetDiscReader(NULL);
    pfsClearVirtualFiles();
    buildFighters();
}

/* P17a: "scripts" in a model object: a slot of the form's effects archive takes the script of another fighter's archive, converted for the
   form's side and place. A fake disc file 0x610 holds three archives: entry 0 (fighter 4's, 18 slots), entry 1 (enemy 207's, 18 slots, with an
   empty slot 3 and a camera of its own in slot 4) and entry 2 (fighter 3's, only 12 slots). */
#define SCRIPT_FILE 0x610
static unsigned char sScriptFile[4096];

static void scriptWords(int at, const int *w, int n) {
    int i;

    for (i = 0; i < n; i++) put16(sScriptFile + at + 2 * i, w[i]);
}

static unsigned char *scriptDisc(int file, size_t *len) {
    static const int donor5[] = {2, 0, 0, 3, 255}, donor6[] = {2, 0, 0, 4, 255}, end[] = {255}, foe5[] = {3, 1, 1014, 3, 0, 1014, 0, -3968, 4992, 5, 0, 0, 1, 2, 2, 1, 255},
                     foe4[] = {5, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 255}, own5[] = {2, 0, 0, 9, 255};
    unsigned char *d;
    int i;

    if (file != SCRIPT_FILE) return NULL;
    memset(sScriptFile, 0, sizeof sScriptFile);
    put32at(sScriptFile + 0, 16);   /* entry 0 at 16: 18 slots (72 bytes), scripts from 88 */
    put32at(sScriptFile + 4, 200);  /* entry 1 at 200 */
    put32at(sScriptFile + 8, 400);  /* entry 2 at 400: 12 slots */
    put32at(sScriptFile + 16, 72);
    put32at(sScriptFile + 16 + 4 * 5, 72 + 4);
    put32at(sScriptFile + 16 + 4 * 6, 72 + 20);
    scriptWords(16 + 72, end, 1);
    scriptWords(16 + 76, donor5, 5);
    scriptWords(16 + 92, donor6, 5);
    put32at(sScriptFile + 200, 72);
    put32at(sScriptFile + 200 + 4 * 5, 72 + 4);
    put32at(sScriptFile + 200 + 4 * 4, 72 + 60);
    scriptWords(200 + 72, end, 1);
    scriptWords(200 + 76, foe5, 17);
    scriptWords(200 + 132, foe4, 18);
    put32at(sScriptFile + 400, 48);
    put32at(sScriptFile + 400 + 4 * 5, 48 + 4);
    scriptWords(400 + 48, end, 1);
    scriptWords(400 + 52, own5, 5);
    d = (unsigned char *)malloc(sizeof sScriptFile);
    for (i = 0; i < (int)sizeof sScriptFile; i++) d[i] = sScriptFile[i];
    *len = sizeof sScriptFile;
    return d;
}

static void testScripts(void) {
    const char *log;
    unsigned char *ft;
    size_t flen = 0;
    int i;

    buildTables();
    buildFighters();
    for (i = 0; i < 2; i++) {
        put32at(PINFO(i) + 8, ((unsigned)SCRIPT_FILE << 16) | (i == 0 ? 2u : 0u));
        put16(PINFO(i) + 0x10, i == 0 ? 5 : 200);
        put16(PINFO(i) + 0x18, 0);
    }
    for (i = 0; i < 3; i++) put32at(EINFO(i) + 8, ((unsigned)SCRIPT_FILE << 16) | (i == 1 ? 1u : 0u));
    put16(EINFO(1) + 0x10, 50);
    put16(EINFO(1) + 0x18, 100);
    pfsInit(NULL, 3000);
    pfsClearVirtualFiles();
    modJson("sc", "content");
    putDigimon("sc",
            "{\"format\":\"digimon-v1\",\"new\":{"
            "\"mir\":{\"base\":3,\"name\":{\"eng\":\"Mir\"},\"model\":{\"from\":4,\"effects\":\"dw3:digimon/4\",\"scripts\":{"
            "\"5\":{\"from\":\"dw3:digimon/207\",\"slot\":5},"
            "\"3\":{\"from\":\"dw3:digimon/207\",\"slot\":3},"
            "\"7\":{\"from\":207,\"slot\":4},"
            "\"40\":{\"from\":\"dw3:digimon/207\",\"slot\":5},"
            "\"2\":{\"from\":\"dw3:digimon/999\",\"slot\":5},"
            "\"9\":{\"from\":\"dw3:digimon/207\",\"slot\":99}}}},"
            "\"same\":{\"base\":3,\"name\":{\"eng\":\"Same\"},\"model\":{\"from\":4,\"effects\":\"dw3:digimon/4\",\"scripts\":{"
            "\"8\":{\"from\":\"dw3:digimon/3\",\"slot\":5}}}},"
            "\"few\":{\"base\":3,\"name\":{\"eng\":\"Few\"},\"model\":{\"from\":4,\"effects\":\"dw3:digimon/3\",\"scripts\":{"
            "\"15\":{\"from\":\"dw3:digimon/207\",\"slot\":5}}}}}}");
    digimonExtSetDiscReader(scriptDisc);
    logBegin();
    loadAll();
    ft = digimonExtBuildFighters(&flen);
    log = logEnd();
    /* one line each: an unknown slot (40, and 15 of a 12-slot archive), an empty source slot (3, from 207's slot 3), a camera of its own (7), and
       the bad entries (a fighter that does not exist, a slot 99) */
    CHECK(strstr(log, "model.scripts.40\" rejected: unknown slot") != NULL);
    CHECK(strstr(log, "sc:few: field \"model.scripts.15\" rejected: unknown slot (its effects archive has 12 slots)") != NULL);
    CHECK(strstr(log, "sc:mir: field \"model.scripts.3\" rejected: dw3:digimon/207 has no script in slot 3 of its effects archive") != NULL);
    CHECK(strstr(log, "sc:mir: field \"model.scripts.7\" rejected: dw3:digimon/207 slot 4: ") != NULL && strstr(log, "view of its own") != NULL);
    CHECK(strstr(log, "model.scripts.2\" rejected: {\"from\"") != NULL && strstr(log, "model.scripts.9\" rejected: {\"from\"") != NULL);
    CHECK(countOf(log, "rejected") == 6);
    CHECK(ft != NULL);
    if (ft != NULL) {
        unsigned pStart = get32(ft + 8);
        const unsigned char *mir = ft + pStart + 3 * 0xC4, *served;
        unsigned ref = get32(mir + 8);
        size_t glen = 0;
        int file = (int)(ref >> 16);

        CHECK(file >= 3000 && (ref & 0xFFFF) == 0);
        served = file >= 3000 ? pfsResolve(file, &glen, 0, NULL) : NULL;
        CHECK(served != NULL && glen % 2048 == 0);
        if (served != NULL) {
            unsigned at = get32(served), s5 = at + get32(served + at + 4 * 5), s6 = at + get32(served + at + 4 * 6);

            CHECK(at == 16 && get32(served + at + 4 * 3) == 0 && get32(served + at + 4 * 7) == 0); /* the refused slots stay as they were */
            CHECK(s6 == 16 + 72 + 20 && get16(served + s6 + 6) == 4); /* a slot not named: the donor's */
            CHECK(s5 >= 4096 && s5 + 34 <= glen); /* slot 5 moved to the end of the file: the enemy's script, mirrored for the form */
            if (s5 + 34 <= glen) {
                /* the enemy (height 100, distance 50) to a partner form of the template's place (height 0, distance 200): y + 100, z mirrored and
                   moved by 50 - 200; the camera 1 becomes 5; the partner's model 0 (type 1) becomes the enemy's (4) */
                CHECK(get16(served + s5) == 3 && get16(served + s5 + 2) == 1 && get16(served + s5 + 4) == 1014);
                CHECK(get16(served + s5 + 6) == 3 && get16(served + s5 + 8) == 0 && get16(served + s5 + 10) == 1014);
                CHECK(get16(served + s5 + 12) == 0 && get16(served + s5 + 14) == -3968 + 100 && get16(served + s5 + 16) == -4992 - 150);
                CHECK(get16(served + s5 + 18) == 5 && get16(served + s5 + 24) == 5);
                CHECK(get16(served + s5 + 26) == 2 && get16(served + s5 + 28) == 2 && get16(served + s5 + 30) == 4 && get16(served + s5 + 32) == 255);
            }
        }
        {
            const unsigned char *same = ft + pStart + 4 * 0xC4;
            unsigned sref = get32(same + 8);
            size_t slen = 0;
            const unsigned char *sf = (sref >> 16) >= 3000 ? pfsResolve((int)(sref >> 16), &slen, 0, NULL) : NULL;

            /* the same side (a partner's script into a partner form) is a copy */
            CHECK(sf != NULL);
            if (sf != NULL) {
                unsigned at = get32(sf), s8 = at + get32(sf + at + 4 * 8);

                CHECK(s8 + 10 <= slen && get16(sf + s8) == 2 && get16(sf + s8 + 6) == 9 && get16(sf + s8 + 8) == 255);
            }
        }
        free(ft);
    }
    unloadAll();
    digimonExtSetDiscReader(NULL);
    pfsClearVirtualFiles();
    buildFighters();
}

/* A8.3: the folders of digimon/. A new Digimon is digimon/<key>/ with digimon.json and an optional model/; d<nnn> is original fighter <nnn>'s (its
   digimon.json changes it, its model/ is never read); the old content/digimon.json and content/models/ are not read */
static void testFolders(void) {
    const char *log;
    unsigned char *ft;
    size_t flen = 0;
    char cmd[2000];
    int i, n;
    static const char *const modelJson =
        "{\"format\":\"model-src-v1\",\"name\":\"robot\",\"gltf\":\"m.gltf\",\"fitHeight\":1800,\"template\":\"dw3:digimon/207\","
        "\"effects\":\"dw3:digimon/208\",\"info\":{\"distance\":-321,\"height\":77},\"motions\":{\"0\":\"idle\",\"1\":\"idle\"},\"fill\":\"idle\"}";
    static const char *const kModelFolders[] = {"own", "given", "d206", "nodigimon"};

    buildTables();
    buildFighters();
    for (i = 0; i < 3; i++) put32at(EINFO(i) + 8, 0x05220000u + (unsigned)i);
    {
        char user[800];
        snprintf(user, sizeof user, "%s/user", gRoot);
        setenv("DW3_USER_DIR", user, 1);
        unsetenv("DW3_CACHE_DIR");
        portPathsReset();
    }
    pfsInit(NULL, 3000);
    pfsClearVirtualFiles();
    modJson("fo", "content");
    putDigimon("fo", "{\"new\":{"
               "\"own\":{\"base\":206,\"name\":{\"eng\":\"Own\"}},"
               "\"given\":{\"base\":206,\"name\":{\"eng\":\"Given\"},\"model\":207}}}");
    /* a digimon.json whose "name" is not its folder, one with another format, d-folders with a wrong name and out of range, a folder with no record */
    putFile("mods/fo/digimon/named/digimon.json", "{\"format\":\"digimon-digimon-v1\",\"name\":\"other\",\"base\":206,\"title\":{\"eng\":\"N\"}}");
    putFile("mods/fo/digimon/oldfmt/digimon.json", "{\"format\":\"digimon-v1\",\"base\":206,\"title\":{\"eng\":\"O\"}}");
    putFile("mods/fo/digimon/d207/digimon.json", "{\"format\":\"digimon-digimon-v1\",\"name\":\"d206\",\"enemy\":{\"itemChance\":5}}");
    putFile("mods/fo/digimon/d208/digimon.json", "{\"format\":\"digimon-digimon-v1\",\"name\":\"d208\",\"enemy\":{\"itemChance\":6},\"notes\":\"x\"}");
    putFile("mods/fo/digimon/d001/digimon.json", "{\"format\":\"digimon-digimon-v1\",\"partner\":{\"hp\":1}}");
    putFile("mods/fo/digimon/only-art/readme.txt", "no record: not a Digimon");
    /* the old layout of before A8.3 is not read */
    putFile("mods/fo/content/digimon.json", "{\"format\":\"digimon-v1\",\"new\":{\"legacy\":{\"base\":206,\"name\":{\"eng\":\"Legacy\"}}}}");
    putFile("mods/fo/content/encounters.json", "{\"format\":\"digimon-v1\",\"encounters\":{\"34\":{\"1\":{\"enemy\":206}}}}");
    for (i = 0; i < 4; i++) {
        snprintf(cmd, sizeof cmd, "mkdir -p '%s/mods/fo/digimon/%s/model' && cp test_fixtures/model/robot/m.* '%s/mods/fo/digimon/%s/model/'", gRoot,
                 kModelFolders[i], gRoot, kModelFolders[i]);
        if (system(cmd)) {}
        snprintf(cmd, sizeof cmd, "mods/fo/digimon/%s/model/model.json", kModelFolders[i]);
        putFile(cmd, modelJson);
    }
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/mods/fo/content/models/legacy' && cp test_fixtures/model/robot/m.* '%s/mods/fo/content/models/legacy/'", gRoot, gRoot);
    if (system(cmd)) {}
    putFile("mods/fo/content/models/legacy/model.json", modelJson);

    logBegin();
    n = loadAll();
    ft = digimonExtBuildFighters(&flen);
    log = logEnd();
    /* the entries: own and given only; the changes of d207 (wrong name) and d001 are refused, d208's applies; the old files add nothing */
    CHECK(digimonExtNewCount() == 2 && digimonExtNewId("fo:given") == 471 && digimonExtNewId("fo:own") == 472);
    CHECK(digimonExtNewId("fo:d206") == -1 && digimonExtNewId("fo:legacy") == -1 && digimonExtNewId("fo:named") == -1 && digimonExtNewId("fo:oldfmt") == -1);
    CHECK(n == 3 && digimonExtEnemyPatchCount() == 1 && digimonExtPlaceCount() == 0); /* two entries and the change of 208 */
    CHECK(strstr(log, "fo/digimon/named/digimon.json rejected: \"name\" must be \"named\", the folder's name") != NULL);
    CHECK(strstr(log, "fo/digimon/oldfmt/digimon.json rejected: \"format\" must be \"digimon-digimon-v1\"") != NULL);
    CHECK(strstr(log, "fo/digimon/d207/digimon.json rejected: \"name\" must be \"d207\", the folder's name") != NULL);
    CHECK(strstr(log, "fo/digimon/d001/digimon.json rejected: d001 is not an original Digimon (d003..d470)") != NULL);
    CHECK(strstr(log, "fo/content/encounters.json rejected: \"format\" must be \"encounters-v1\"") != NULL);
    CHECK(strstr(log, "legacy") == NULL && strstr(log, "only-art") == NULL && strstr(log, "d208") == NULL);
    /* the folder's own model/ is the model of "fo:own" (a row of its own), not of "given" (its "model" wins: 207's row) or of a d<nnn> or a model-only folder */
    CHECK(modelsHas("fo:own") && modelsHas("fo:given") && modelsHas("fo:nodigimon") && !modelsHas("fo:d206") && !modelsHas("fo:legacy"));
    CHECK(strstr(log, "[models] fo:own: ") != NULL && strstr(log, "[models] fo:given") == NULL && strstr(log, "fo:d206") == NULL); /* baked here, or read back from the cache of the same bytes */
    CHECK(ft != NULL);
    if (ft != NULL) {
        unsigned start = get32(ft + 4), eStart = get32(ft + 12);
        const unsigned char *e = ft + start + 5 * 4, *own;

        CHECK(get16(e) == 471 && e[2] == 1 && e[3] == 0x3B);      /* given: 207's row */
        CHECK(get16(e + 4) == 472 && e[6] == 3 && e[7] == 0x3B);  /* own: a row of its own after the three originals, the kind of its template (207) */
        own = ft + eStart + 3 * 0x48;
        CHECK(get16(own + 0x18) == 77 && get16(own + 0x10) == -321 && get32(own + 8) == 0x05220002u);
    }
    free(ft);
    unloadAll();
    pfsClearVirtualFiles();
    unsetenv("DW3_USER_DIR");
    portPathsReset();
    itemExtReset();
    buildFighters();
}

int main(void) {
    const char *tmp = getenv("TMPDIR");
    char cmd[700];

    snprintf(gRoot, sizeof gRoot, "%s/digimontest.%d", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", (int)getpid());
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", gRoot);
    if (system(cmd)) return 2;
    buildTables();
    buildFighters();
    {
        char root[700];
        snprintf(root, sizeof root, "%s/mods", gRoot);
        snprintf(cmd, sizeof cmd, "mkdir -p '%s'", root);
        if (system(cmd)) return 2;
    }
    testNoMods();
    testChanges();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testBadFiles();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testNew();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testNewPartner();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testEvolution();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testWideRows();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testOwnModel();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testSourceModel();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testByReference();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testScripts();
    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    testFolders();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
    if (system(cmd)) {}
    if (failures == 0) printf("digimontest: ok\n");
    return failures ? 1 : 0;
}
