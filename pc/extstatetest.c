#define _GNU_SOURCE 1
/*
 * Host test of the port's extended item state (pc/shim/ext_state.c, feature extended-state T1):
 * original ids hit GAME, ids above the original range hit the growable store, and an id outside the
 * range never writes GAME. No disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "engine/game_state.h"
#include "shim/content.h"
#include "shim/ext_state.h"
#include "shim/item_ext.h"
#include "shim/card_ext.h"
#include "shim/png_write.h"
#include "shim/tech_ext.h"
#include "shim/mods.h"
#include "shim/table_mods.h"
#include "shim/text_reflow.h"
#include "shim/text_data.h"

GameState GAME;
/* what the game's items.c provides: the technique table (renamed under DW3_PC, game_state.h) */
TechData TECHS_CARD[443];
/* and the Digimon table, which the learnedBy of a technique patches */
DigimonData DIGIMON_DATA_CARD[55];

/* what the game's items.c provides, as far as item_ext.c needs it */
static ItemEffect gEffect43 = {ITEM_USE_MENU, 1, 500};
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
            fprintf(stdout, "extstatetest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int gameIsClean(void)
{
    static const GameState zero;
    return memcmp(&GAME, &zero, sizeof GAME) == 0;
}

static char gRoot[600];

static void putFile(const char *rel, const char *text)
{
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

static void modJson(const char *mod, const char *kind)
{
    char rel[200], text[200];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", mod);
    snprintf(text, sizeof text, "{\"format\":\"mod-v1\",\"name\":\"%s\",\"version\":\"1.2\",\"kind\":\"%s\"}", mod, kind);
    putFile(rel, text);
}

static int loadMods(void)
{
    char root[700];
    snprintf(root, sizeof root, "%s/mods", gRoot);
    modsInit(root, NULL);
    return itemExtLoad();
}

/* the extended items of content mods (T3): ids, lookup through ITEM_FUNCS, names, EXTI by name, removal */
static void testItemRegistry(void)
{
    s16 out[8];
    unsigned char chunk[256];
    unsigned long need;
    int i;

    for (i = 0; i < 402; i++) {
        ITEMS[i].kind = ITEM_KIND_KEY;
        ITEMS[i].type = 0x1D;
    }
    ITEMS[42].data.effect = &gEffect43;
    ITEMS[42].price = 12;
    ITEMS[42].sellPrice = 6;
    ITEMS[42].kind = ITEM_KIND_USABLE;
    ITEMS[42].type = 5;
    ITEM_TYPE_CATEGORIES[5] = 0x4D;

    modJson("zeta", "content");
    modJson("alpha", "content");
    modJson("plain", "assets");
    putFile("mods/zeta/content/items.json",
            "{\"format\":\"items-v1\",\"new\":{"
            "\"b-item\":{\"base\":43,\"price\":50,\"sellPrice\":20,\"effect\":{\"amount\":300},"
            "\"name\":{\"eng\":\"Bee\",\"spa\":\"Abeja\"},\"description\":{\"eng\":\"Buzz\"}},"
            "\"a-item\":{\"base\":43,\"name\":{\"eng\":\"Ay\",\"es-419\":\"Ei\"}},"
            "\"bad-base\":{\"base\":9999,\"name\":{\"eng\":\"x\"}},"
            "\"no-name\":{\"base\":43},"
            "\"Bad Key\":{\"base\":43,\"name\":{\"eng\":\"x\"}}}}");
    putFile("mods/alpha/content/items.json",
            "{\"format\":\"items-v1\",\"new\":{\"z\":{\"base\":43,\"price\":7,\"unknown\":1,\"name\":{\"eng\":\"Zed\"}}}}");
    putFile("mods/plain/content/items.json", "{\"format\":\"items-v1\",\"new\":{\"p\":{\"base\":43,\"name\":{\"eng\":\"P\"}}}}");

    /* deterministic ids: alphabetical by "<mod>:<key>"; only valid items of content mods; the same twice */
    CHECK(loadMods() == 3);
    CHECK(itemExtIdOf("alpha:z") == 403 && itemExtIdOf("zeta:a-item") == 404 && itemExtIdOf("zeta:b-item") == 405);
    CHECK(itemExtIdOf("plain:p") == -1 && itemExtIdOf("zeta:bad-base") == -1 && itemExtIdOf("zeta:no-name") == -1);
    CHECK(strcmp(itemExtNameOf(405), "zeta:b-item") == 0 && itemExtNameOf(402) == NULL && itemExtNameOf(406) == NULL);
    CHECK(loadMods() == 3 && itemExtIdOf("zeta:b-item") == 405);

    /* the game sees them through ITEM_FUNCS; the originals are untouched */
    CHECK(ITEM_FUNCS.get(405) != NULL && ITEM_FUNCS.get(405)->price == 50 && ITEM_FUNCS.get(405)->sellPrice == 20);
    CHECK(ITEM_FUNCS.get(404)->price == 12 && ITEM_FUNCS.get(403)->price == 7); /* cloned from the base / overridden */
    CHECK(ITEM_FUNCS.get(405)->data.effect->amount == 300 && ITEM_FUNCS.get(405)->data.effect->kind == 1);
    CHECK(gEffect43.amount == 500 && ITEM_FUNCS.get(43) == &ITEMS[42] && ITEM_FUNCS.get(43)->data.effect->amount == 500);
    CHECK(ITEM_FUNCS.get(404)->data.effect != &gEffect43); /* its own record */
    CHECK(ITEM_FUNCS.get(9000) != NULL); /* an unknown id above the original range never gives NULL */
    CHECK(ITEM_FUNCS.get(0) == NULL);
    CHECK(ITEM_FUNCS.isKind(405, ITEM_KIND_USABLE) && !ITEM_FUNCS.isKind(405, ITEM_KIND_KEY) && ITEM_FUNCS.isKind(43, ITEM_KIND_USABLE));
    CHECK(ITEM_FUNCS.getCategory(405) == 0x4D);
    CHECK(itemExtInfo(405) == ITEM_FUNCS.get(405));

    /* the lists the menus build: the bag's usable items include the ones the player has */
    memset(&GAME, 0, sizeof GAME);
    pcExtStateReset();
    CHECK(ITEM_FUNCS.list(1, out) == 0);
    PC_ITEM_COUNT(405) = 2;
    CHECK(ITEM_FUNCS.list(1, out) == 1 && out[0] == 405);
    CHECK(ITEM_FUNCS.list(2, out) == 0); /* weapons */
    PC_ITEM_EQUIPPED(404) = 1;
    CHECK(ITEM_FUNCS.list(1, out) == 1);
    CHECK(ITEM_FUNCS.list((s32)0x80000001, out) == 2 && out[0] == 404 && out[1] == 405); /* "all" adds the equipped */

    /* texts: language, then es-419 -> spa -> eng, a description may be missing */
    CHECK(strcmp(itemExtText(2, "eng", 0), "Bee") == 0 && strcmp(itemExtText(2, "spa", 0), "Abeja") == 0);
    CHECK(strcmp(itemExtText(2, "es-419", 0), "Abeja") == 0 && strcmp(itemExtText(2, "fra", 0), "Bee") == 0);
    CHECK(strcmp(itemExtText(1, "es-419", 0), "Ei") == 0 && strcmp(itemExtText(1, "spa", 0), "Ay") == 0);
    CHECK(strcmp(itemExtText(2, "eng", 1), "Buzz") == 0 && itemExtText(1, "eng", 1)[0] == '\0');
    CHECK(itemExtText(77, "eng", 0)[0] == '\0');

    /* the save stores names: a round trip, then the mod behind one is gone */
    PC_ITEM_COUNT(405) = 7;
    PC_ITEM_EQUIPPED(405) = 1;
    PC_ITEM_COUNT(403) = 3;
    need = pcExtStateEncode(chunk, sizeof chunk);
    CHECK(need > 8 && need <= sizeof chunk);
    CHECK(memmem(chunk, need, "zeta:b-item", 11) != NULL && memmem(chunk, need, "alpha:z", 7) != NULL);
    pcExtStateReset();
    CHECK(pcExtStateDecode(chunk, need) == 0);
    CHECK(PC_ITEM_COUNT(405) == 7 && PC_ITEM_EQUIPPED(405) == 1 && PC_ITEM_COUNT(403) == 3);

    /* remove alpha: its item vanishes, ids shift (zeta's move down), the others keep their counts, no crash */
    putFile("mods/alpha/mod.json", "not json");
    CHECK(loadMods() == 2);
    CHECK(itemExtIdOf("alpha:z") == -1 && itemExtIdOf("zeta:b-item") == 404);
    CHECK(pcExtStateDecode(chunk, need) == 1);
    CHECK(PC_ITEM_COUNT(404) == 7 && PC_ITEM_EQUIPPED(404) == 1 && PC_ITEM_COUNT(403) == 0 && PC_ITEM_COUNT(405) == 0);

    /* no content mod: ITEM_FUNCS is the game's again, nothing is named */
    putFile("mods/zeta/mod.json", "{\"format\":\"mod-v1\",\"name\":\"zeta\",\"version\":\"1\"}");
    CHECK(loadMods() == 0);
    CHECK(ITEM_FUNCS.get == stubGet && ITEM_FUNCS.list == stubList);
    CHECK(pcExtStateDecode(chunk, need) == 3); /* the three saved names are unknown now */

    /* a malformed file or a wrong format registers nothing and never stops the others */
    putFile("mods/zeta/mod.json", "{\"format\":\"mod-v1\",\"name\":\"zeta\",\"version\":\"1\",\"kind\":\"content\"}");
    putFile("mods/zeta/content/items.json", "{\"format\":\"items-v2\",\"new\":{}}");
    CHECK(loadMods() == 0);
    putFile("mods/zeta/content/items.json", "{\"format\":\"items-v1\",\"new\":{\"x\":");
    CHECK(loadMods() == 0);

    itemExtReset();
    pcExtStateReset();
}

/* ---- T6/T7: table patches that name an item a mod adds ---- */

static int gSavedErr;
static const char *gLogPath = "/tmp/extstatetest.log";

static void logBegin(void)
{
    int fd;
    fflush(stderr);
    gSavedErr = dup(2);
    fd = open(gLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

static char *logEnd(void)
{
    static char buf[4096];
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

typedef struct Loot {
    short id, item, chance;
} Loot;
static Loot gLoot[3];
static const TmField kLootFields[] = {
    {"item", TM_S16, offsetof(Loot, item), 0, 32767, "item", 0, 0, itemExtByName, itemExtIdOk},
    {"chance", TM_S16, offsetof(Loot, chance), 0, 1023, "chance"},
};
static const TmTable kLootTable = {"battle_loot", gLoot, sizeof(Loot), 3, 1, "enemy", "when", kLootFields, 2};
static const TmField kPriceFields[] = {
    {"price", TM_U16, offsetof(ItemInfo, price), 0, 65535, "buy"},
    {"sellPrice", TM_U16, offsetof(ItemInfo, sellPrice), 0, 65535, "sell"},
};
static const TmTable kPriceTable = {"item_prices", ITEMS, sizeof(ItemInfo), 402, 1, "id", "when", kPriceFields, 2,
                                    0, 0, TM_S32, itemExtByName, itemExtRow};

static void resetLoot(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        gLoot[i].id = (short)(i + 1);
        gLoot[i].item = (short)(10 + i);
        gLoot[i].chance = 100;
    }
}

static void testTablesNameModItems(void)
{
    char *log;
    int i;

    for (i = 0; i < 402; i++) {
        ITEMS[i].kind = ITEM_KIND_KEY;
        ITEMS[i].type = 0x1D;
        ITEMS[i].price = 100;
        ITEMS[i].sellPrice = 40;
    }
    ITEMS[42].kind = ITEM_KIND_USABLE;
    ITEMS[42].type = 5;
    ITEMS[42].data.effect = &gEffect43;
    modJson("zeta", "content");
    modJson("tweak", "assets");
    modJson("tweak2", "assets");
    putFile("mods/zeta/content/items.json",
            "{\"format\":\"items-v1\",\"new\":{\"b-item\":{\"base\":43,\"price\":50,\"sellPrice\":20,\"name\":{\"eng\":\"Bee\"}},"
            "\"a-item\":{\"base\":43,\"price\":60,\"name\":{\"eng\":\"Ay\"}}}}");
    /* tweak: loot by name and by a registered id, an unknown name, an unregistered id; prices of a mod item and a bad name */
    putFile("mods/tweak/tables/battle_loot.json",
            "{\"format\":\"battle_loot-v1\",\"rows\":{\"1\":{\"item\":\"zeta:b-item\"},\"2\":{\"item\":404},"
            "\"3\":{\"item\":\"zeta:nope\",\"chance\":7}}}");
    putFile("mods/tweak/tables/item_prices.json",
            "{\"format\":\"item_prices-v1\",\"rows\":{\"zeta:b-item\":{\"price\":99,\"sellPrice\":\"+5\"},"
            "\"nobody:none\":{\"price\":1},\"43\":{\"price\":13}}}");
    /* tweak2 (applies after tweak): "*" never reaches the mod items; its own row multiplies the new price */
    putFile("mods/tweak2/tables/item_prices.json",
            "{\"format\":\"item_prices-v1\",\"rows\":{\"*\":{\"sellPrice\":1},\"zeta:b-item\":{\"price\":\"*2\"}}}");
    putFile("mods/tweak2/tables/battle_loot.json", "{\"format\":\"battle_loot-v1\",\"rows\":{\"1\":{\"item\":9000}}}");

    CHECK(loadMods() == 2); /* a-item = 403, b-item = 404 */
    CHECK(itemExtIdOf("zeta:a-item") == 403 && itemExtIdOf("zeta:b-item") == 404);
    resetLoot();
    tmInit(NULL);
    logBegin();
    CHECK(tmRegister(&kLootTable) == 0);
    CHECK(tmRegister(&kPriceTable) == 0);
    log = logEnd();
    CHECK(strstr(log, "row \"zeta:nope\"") == NULL); /* it is a field value, not a row key */
    CHECK(strstr(log, "field \"item\" rejected: no installed mod defines \"zeta:nope\"") != NULL);
    CHECK(strstr(log, "field \"item\" rejected: no installed mod defines the id 9000") != NULL);
    CHECK(strstr(log, "row \"nobody:none\" rejected: no installed mod defines that name") != NULL);

    /* loot */
    CHECK(tmApply("battle_loot") == 3);
    CHECK(gLoot[0].item == 404 && gLoot[1].item == 404 && gLoot[2].item == 12 && gLoot[2].chance == 7); /* rest of the row applies */
    CHECK(tmApply("battle_loot") == 3 && gLoot[0].item == 404); /* idempotent */

    /* prices: the record of the mod item changes, the declaration default is restored on the next application */
    CHECK(ITEM_FUNCS.get(404)->price == 50 && ITEM_FUNCS.get(404)->sellPrice == 20);
    tmApply("item_prices");
    CHECK(ITEM_FUNCS.get(404)->price == 198 && ITEM_FUNCS.get(404)->sellPrice == 25); /* 99*2 and 20+5 */
    CHECK(ITEM_FUNCS.get(403)->price == 60); /* untouched */
    CHECK(ITEMS[42].price == 13 && ITEMS[0].sellPrice == 1 && ITEMS[0].price == 100); /* originals: plain rows and "*" */
    tmApply("item_prices");
    CHECK(ITEM_FUNCS.get(404)->price == 198 && ITEM_FUNCS.get(404)->sellPrice == 25); /* never adds up */
    CHECK(ITEM_FUNCS.get(404)->data.effect != NULL);

    tmClear();
    itemExtReset();
    pcExtStateReset();
}

/* ---- content-core C2: an original item or technique by name, "dw3:item/<id>" and "dw3:tech/<id>" ---- */

static int loadTechMods(void);

static void testOriginalNames(void)
{
    char saved[600], *log;
    int i;

    snprintf(saved, sizeof saved, "%s", gRoot);
    snprintf(gRoot, sizeof gRoot, "%s/names", saved);
    for (i = 0; i < 402; i++) {
        ITEMS[i].kind = ITEM_KIND_KEY;
        ITEMS[i].type = 0x1D;
        ITEMS[i].price = 100;
        ITEMS[i].sellPrice = 40;
    }
    ITEMS[42].kind = ITEM_KIND_USABLE;
    ITEMS[42].type = 5;
    ITEMS[42].data.effect = &gEffect43;
    ITEMS[42].price = 12;
    memset(TECHS_CARD, 0, sizeof TECHS_CARD);
    TECHS_CARD[8].mp = 2;
    TECHS_CARD[8].power = 55;
    modJson("orig", "content");
    modJson("tweak", "assets");
    putFile("mods/orig/content/items.json",
            "{\"format\":\"items-v1\",\"new\":{"
            "\"by-name\":{\"base\":\"dw3:item/43\",\"price\":77,\"name\":{\"eng\":\"N\"}},"
            "\"wrong-kind\":{\"base\":\"dw3:tech/43\",\"name\":{\"eng\":\"W\"}},"
            "\"too-big\":{\"base\":\"dw3:item/403\",\"name\":{\"eng\":\"T\"}}}}");
    putFile("mods/orig/content/techs.json",
            "{\"format\":\"techs-v1\",\"new\":{"
            "\"by-name\":{\"base\":\"dw3:tech/9\",\"mp\":4,\"name\":{\"eng\":\"N\"}},"
            "\"wrong-kind\":{\"base\":\"dw3:item/9\",\"name\":{\"eng\":\"W\"}}}}");
    putFile("mods/tweak/tables/battle_loot.json",
            "{\"format\":\"battle_loot-v1\",\"rows\":{\"1\":{\"item\":\"dw3:item/43\"},\"2\":{\"item\":\"orig:by-name\"},"
            "\"3\":{\"item\":\"dw3:item/403\"}}}");
    putFile("mods/tweak/tables/item_prices.json",
            "{\"format\":\"item_prices-v1\",\"rows\":{\"dw3:item/43\":{\"price\":21},\"dw3:item/403\":{\"price\":1}}}");

    logBegin();
    CHECK(loadMods() == 1 && loadTechMods() == 1);
    log = logEnd();
    CHECK(strstr(log, "item \"orig:wrong-kind\" rejected: \"base\" must be an original item id (1..402)") != NULL);
    CHECK(strstr(log, "item \"orig:too-big\" rejected: \"base\" must be an original item id (1..402)") != NULL);
    CHECK(strstr(log, "tech \"orig:wrong-kind\" rejected: \"base\" must be an original technique id (1..443)") != NULL);
    CHECK(itemExtIdOf("orig:by-name") == 403 && techExtIdOf("orig:by-name") == 444);
    CHECK(ITEM_FUNCS.get(403)->price == 77 && ITEM_FUNCS.get(403)->sellPrice == ITEMS[42].sellPrice);
    CHECK(ITEM_FUNCS.get(403)->data.effect != NULL && ITEM_FUNCS.get(403)->data.effect != &gEffect43); /* its own copy */
    CHECK(PC_TECH(443).mp == 4 && PC_TECH(443).power == 55);

    /* the table hooks take an original's name (a list member or a field value) and a new one */
    CHECK(itemExtByName("dw3:item/43") == 43 && itemExtByName("dw3:item/1") == 1 && itemExtByName("dw3:item/402") == 402);
    CHECK(itemExtByName("dw3:item/0") < 0 && itemExtByName("dw3:item/403") < 0 && itemExtByName("dw3:tech/43") < 0);
    CHECK(itemExtByName("orig:by-name") == 403 && itemExtIdOf("dw3:item/43") == -1); /* a name of a NEW item only */

    resetLoot();
    tmInit(NULL);
    logBegin();
    CHECK(tmRegister(&kLootTable) == 0 && tmRegister(&kPriceTable) == 0);
    log = logEnd();
    CHECK(strstr(log, "field \"item\" rejected: no installed mod defines \"dw3:item/403\"") != NULL);
    CHECK(strstr(log, "row \"dw3:item/403\" rejected: no installed mod defines that name") != NULL);
    CHECK(tmApply("battle_loot") == 2); /* row 3 names an item no mod defines: rejected, unchanged */
    CHECK(gLoot[0].item == 43 && gLoot[1].item == 403 && gLoot[2].item == 12);
    tmApply("item_prices");
    CHECK(ITEMS[42].price == 21 && ITEMS[0].price == 100);
    CHECK(ITEM_FUNCS.get(403)->price == 77); /* the mod item was copied from the base before the patch: unchanged */

    tmClear();
    techExtReset();
    itemExtReset();
    pcExtStateReset();
    snprintf(gRoot, sizeof gRoot, "%s", saved);
}

/* ---- T8: a one-line description is fitted with the reflow metrics ---- */

static int flatWidth(void *ctx, int kind, int value)
{
    (void)ctx;
    (void)value;
    return 8; /* every glyph 8 px, spaces (the icon `01 01`) too */
}

static void testFitLine(void)
{
    static const char layout[] = "{\"format\":\"layout-v1\",\"files\":{\"ITMINF\":{\"maxWidthPx\":160,\"maxLines\":1}}}";
    TrOptions o;
    char out[200];

    CHECK(tdFitLine("short", 160, out, sizeof out) == -1); /* no metrics yet: untouched */
    trOptionsInit(&o, flatWidth, NULL);
    CHECK(tdReflowConfigure(layout, sizeof layout - 1, &o) == 0);
    CHECK(tdFitLine("Heals a little HP.", 160, out, sizeof out) == 0 && strcmp(out, "Heals a little HP.") == 0); /* 18 * 8 = 144 */
    CHECK(tdFitLine("Heals a little HP.", 144, out, sizeof out) == 0); /* exactly the budget */
    /* 160 px = 20 glyphs: cut at a word, "..." included */
    CHECK(tdFitLine("Restores a large amount of health to one partner", 160, out, sizeof out) == 1);
    CHECK(strcmp(out, "Restores a...") == 0 || strcmp(out, "Restores a large...") == 0);
    CHECK(strcmp(out, "Restores a large...") == 0); /* 19 glyphs = 152 px: the longest whole-word prefix that fits */
    /* one long word: cut at a character */
    CHECK(tdFitLine("Supercalifragilisticexpialidocious", 160, out, sizeof out) == 1 && strcmp(out, "Supercalifragilis...") == 0 && strlen(out) == 20);
    /* UTF-8: a character is never split */
    CHECK(tdFitLine("\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA \xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA", 80, out, sizeof out) == 1);
    CHECK(strcmp(out, "\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA...") == 0); /* 8 glyphs of 10 fit 80 px: the whole word */
    tdReflowOff();
}

/* extended techs T1: PC_TECH reaches the original table; an index outside it never touches the table */
static void testTechAccess(void) {
    int i;
    TechData snapshot[443];

    for (i = 0; i < 443; i++) {
        TECHS_CARD[i].mp = (u16)(i + 1);
        TECHS_CARD[i].icon = (u8)(i % 200);
    }
    CHECK(&PC_TECH(0) == &TECHS_CARD[0]);
    CHECK(&PC_TECH(442) == &TECHS_CARD[442]);
    CHECK(&PC_TECH(439) == &TECHS_CARD[439]); /* the battle menu's scratch slot */
    CHECK(PC_TECH_PLUS(5) == &TECHS_CARD[5]);
    CHECK(PC_TECH(0x88).mp == 0x89);
    for (i = 0; i < 443; i++) CHECK(PC_TECH(i).mp == (u16)(i + 1));

    /* writes through the accessor land in the table */
    PC_TECH(7).power = 123;
    CHECK(TECHS_CARD[7].power == 123);
    PC_TECH(439) = PC_TECH(3);
    CHECK(TECHS_CARD[439].mp == 4);

    /* outside the table: a scratch entry that is not the table and always reads zero */
    memcpy(snapshot, TECHS_CARD, sizeof snapshot);
    for (i = 0; i < 4; i++) {
        int idx = i == 0 ? -1 : i == 1 ? 443 : i == 2 ? 100000 : -32768;
        TechData *t = &PC_TECH(idx);
        CHECK(t < &TECHS_CARD[0] || t >= &TECHS_CARD[443]);
        CHECK(PC_TECH(idx).mp == 0);
        PC_TECH(idx).mp = 99;
        CHECK(PC_TECH(idx).mp == 0);
    }
    CHECK(memcmp(snapshot, TECHS_CARD, sizeof snapshot) == 0);
}

/* extended Digimon P1: PC_DIGIMON reaches the original rows, the growable store above them, and a scratch row elsewhere */
static void testDigimonAccess(void) {
    int i, first;
    DigimonData snapshot[55];
    DigimonData *r;

    pcExtStateReset();
    memset(DIGIMON_DATA_CARD, 0, sizeof DIGIMON_DATA_CARD);
    for (i = 0; i < 55; i++) DIGIMON_DATA_CARD[i].hp = (u8)(i + 1);
    CHECK(pcDigimonRowCount() == 55);
    CHECK(&PC_DIGIMON(0) == &DIGIMON_DATA_CARD[0] && &PC_DIGIMON(54) == &DIGIMON_DATA_CARD[54]);
    CHECK(PC_DIGIMON_BASE == &DIGIMON_DATA_CARD[0]);
    for (i = 0; i < 55; i++) CHECK(PC_DIGIMON(i).hp == (u8)(i + 1));
    PC_DIGIMON(3).mp = 77;
    CHECK(DIGIMON_DATA_CARD[3].mp == 77);

    /* outside everything: a scratch row that reads zero and is not the table */
    memcpy(snapshot, DIGIMON_DATA_CARD, sizeof snapshot);
    for (i = 0; i < 3; i++) {
        int idx = i == 0 ? -1 : i == 1 ? 55 : 100000;
        r = &PC_DIGIMON(idx);
        CHECK(r < &DIGIMON_DATA_CARD[0] || r >= &DIGIMON_DATA_CARD[55]);
        PC_DIGIMON(idx).hp = 99;
        CHECK(PC_DIGIMON(idx).hp == 0);
    }
    CHECK(memcmp(snapshot, DIGIMON_DATA_CARD, sizeof snapshot) == 0);

    /* the store: zeroed rows from 55, written through the accessor, kept by a later extension */
    CHECK(pcDigimonExtendRows(0) == 55 && pcDigimonRowCount() == 55);
    first = pcDigimonExtendRows(3);
    CHECK(first == 55 && pcDigimonRowCount() == 58);
    CHECK(PC_DIGIMON(55).hp == 0 && PC_DIGIMON(57).id == 0);
    PC_DIGIMON(56).hp = 42;
    PC_DIGIMON(56).id = 600;
    CHECK(pcDigimonExtendRows(2) == 58 && pcDigimonRowCount() == 60);
    CHECK(PC_DIGIMON(56).hp == 42 && PC_DIGIMON(56).id == 600 && PC_DIGIMON(59).hp == 0);
    CHECK(PC_DIGIMON(60).hp == 0 && &PC_DIGIMON(60) != &PC_DIGIMON(59)); /* one past the end is scratch */
    CHECK(memcmp(snapshot, DIGIMON_DATA_CARD, sizeof snapshot) == 0);

    /* the safety limit adds nothing */
    CHECK(pcDigimonExtendRows(PC_DIGIMON_ROW_LIMIT) == -1 && pcDigimonRowCount() == 60);

    /* reset forgets the added rows */
    pcDigimonResetRows();
    CHECK(pcDigimonRowCount() == 55 && pcDigimonExtendRows(1) == 55 && PC_DIGIMON(55).id == 0);
    pcExtStateReset();
    CHECK(pcDigimonRowCount() == 56); /* the rows are not part of the extension: a save load (pcExtStateDecode) keeps them */
    pcDigimonResetRows();
    CHECK(pcDigimonRowCount() == 55);
}

/* ---- extended techs T2/T3: the registry, the store, texts, the save by name, learnedBy and its compatibility check ---- */

static int gProbeLog[32][2];
static int gProbeCalls;
/* digimon 5 has no script 5 (so no learnedBy of a script-5 technique), digimon 4 has 5 but lacks 6 (the partner's alternative), 3 and 6 have everything */
static int probe(int digimon, int slot)
{
    if (gProbeCalls < 32) {
        gProbeLog[gProbeCalls][0] = digimon;
        gProbeLog[gProbeCalls][1] = slot;
    }
    gProbeCalls++;
    if (digimon == 5 && slot == 5) return 0;
    if (digimon == 4 && slot == 6) return 0;
    if (digimon == 7) return -1; /* unknown: accepted */
    return 1;
}

static int loadTechMods(void)
{
    char root[700];
    snprintf(root, sizeof root, "%s/mods", gRoot);
    modsInit(root, NULL);
    return techExtLoad();
}

#define SKILL_OFF(p, e, k) (offsetof(GameState, partners) + (size_t)(p) * sizeof(Partner) + offsetof(Partner, info) + \
                            offsetof(PartnerStats, entries) + (size_t)(e) * sizeof(PartnerEntry) + offsetof(PartnerEntry, skills) + (size_t)(k) * 2)

static void setSkill(unsigned char *blk, int p, int e, int k, unsigned raw)
{
    blk[SKILL_OFF(p, e, k)] = (unsigned char)raw;
    blk[SKILL_OFF(p, e, k) + 1] = (unsigned char)(raw >> 8);
}

static unsigned getSkill(const unsigned char *blk, int p, int e, int k)
{
    return (unsigned)blk[SKILL_OFF(p, e, k)] | ((unsigned)blk[SKILL_OFF(p, e, k) + 1] << 8);
}

static unsigned char xorFrom4(const unsigned char *blk)
{
    unsigned char x = 0;
    size_t i;
    for (i = 4; i < GAME_SAVE_SIZE; i++) x ^= blk[i];
    return x;
}

static void testTechRegistry(void)
{
    static unsigned char blk[GAME_SAVE_SIZE];
    static unsigned char copy[GAME_SAVE_SIZE];
    unsigned char chunk[512];
    unsigned long need;
    const char *log;
    char cmd[800];
    int i;

    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    memset(TECHS_CARD, 0, sizeof TECHS_CARD);
    for (i = 0; i < 443; i++) {
        TECHS_CARD[i].mp = (u16)(i + 1);
        TECHS_CARD[i].power = (u16)(i * 2);
        TECHS_CARD[i].icon = (u8)(2 + i % 4);   /* 2..5 */
        TECHS_CARD[i].element = (u8)(i % 3);     /* 0..2 */
        TECHS_CARD[i].script = (u8)(i % 18);
        TECHS_CARD[i].hitCount = (u8)(1 + i % 3);
    }
    TECHS_CARD[6].script = 5; /* technique 7: the generic animation */
    TECHS_CARD[7].script = 11;
    memset(DIGIMON_DATA_CARD, 0, sizeof DIGIMON_DATA_CARD);
    for (i = 0; i < 52; i++) DIGIMON_DATA_CARD[i].id = (u16)(3 + i); /* ids 3..54, row = id - 3 */
    DIGIMON_DATA_CARD[0].skills[2] = 111;
    DIGIMON_DATA_CARD[0].skillLevels[1] = 20;
    techExtSetFighterProbe(probe);

    modJson("zeta", "content");
    modJson("alpha", "content");
    modJson("plain", "assets");
    putFile("mods/zeta/content/techs.json",
            "{\"format\":\"techs-v1\",\"new\":{"
            "\"spark\":{\"base\":7,\"mp\":3,\"power\":120,\"name\":{\"eng\":\"Spark\",\"spa\":\"Chispa\"},"
            "\"description\":{\"eng\":\"Zap\",\"es-419\":\"Descarga\"},"
            "\"learnedBy\":{\"3\":{\"slot\":2,\"level\":5},\"5\":{\"slot\":1,\"level\":3},\"4\":{\"slot\":3,\"level\":9},"
            "\"99\":{\"slot\":1,\"level\":1},\"6\":{\"slot\":9,\"level\":4},\"7\":{\"slot\":6,\"level\":50},\"8\":{\"slot\":4,\"level\":120}}},"
            "\"beta\":{\"base\":8,\"script\":17,\"name\":{\"eng\":\"Beta\"},\"learnedBy\":{\"3\":{\"slot\":2,\"level\":2}}},"
            "\"bad-script\":{\"base\":8,\"script\":18,\"icon\":200,\"hitCount\":9,\"mp\":70000,\"name\":{\"eng\":\"Bad\"}},"
            "\"bad-base\":{\"base\":444,\"name\":{\"eng\":\"x\"}},"
            "\"no-name\":{\"base\":8},"
            "\"Bad Key\":{\"base\":8,\"name\":{\"eng\":\"x\"}}}}");
    putFile("mods/alpha/content/techs.json",
            "{\"format\":\"techs-v1\",\"new\":{\"a-tech\":{\"base\":7,\"name\":{\"eng\":\"Aye\"},\"learnedBy\":{\"3\":{\"slot\":2,\"level\":4}}}}}");
    putFile("mods/plain/content/techs.json", "{\"format\":\"techs-v1\",\"new\":{\"p\":{\"base\":7,\"name\":{\"eng\":\"P\"}}}}");

    /* deterministic ids: alphabetical by full name, only valid techs of content mods; the same twice */
    logBegin();
    CHECK(loadTechMods() == 4);
    log = logEnd();
    CHECK(techExtIdOf("alpha:a-tech") == 444 && techExtIdOf("zeta:bad-script") == 445 && techExtIdOf("zeta:beta") == 446 &&
          techExtIdOf("zeta:spark") == 447);
    CHECK(techExtIdOf("plain:p") == -1 && techExtIdOf("zeta:bad-base") == -1 && techExtIdOf("zeta:no-name") == -1);
    CHECK(strcmp(techExtNameOf(447), "zeta:spark") == 0 && techExtNameOf(443) == NULL && techExtNameOf(448) == NULL);
    CHECK(strstr(log, "field \"script\" rejected") != NULL && strstr(log, "field \"icon\" rejected") != NULL);
    CHECK(strstr(log, "field \"hitCount\" rejected") != NULL && strstr(log, "field \"mp\" rejected") != NULL);
    CHECK(strstr(log, "\"zeta:bad-base\" rejected") != NULL && strstr(log, "\"zeta:no-name\" rejected: it needs a name in \"eng\"") != NULL);
    CHECK(loadTechMods() == 4 && techExtIdOf("zeta:spark") == 447);

    /* the store: a copy of the base with the overrides; the originals are untouched */
    CHECK(PC_TECH(443).mp == 7 && PC_TECH(443).script == 5);                          /* a-tech = base 7 as is */
    CHECK(PC_TECH(446).mp == 3 && PC_TECH(446).power == 120 && PC_TECH(446).script == 5 && PC_TECH(446).element == TECHS_CARD[6].element);
    CHECK(PC_TECH(445).script == 17 && PC_TECH(445).mp == 8);                          /* beta (id 446) */
    CHECK(PC_TECH(444).script == 11 && PC_TECH(444).icon == TECHS_CARD[7].icon && PC_TECH(444).hitCount == TECHS_CARD[7].hitCount && PC_TECH(444).mp == 8); /* bad-script (id 445): bad fields ignored */
    CHECK(TECHS_CARD[6].mp == 7 && TECHS_CARD[6].power == 12 && TECHS_CARD[7].script == 11);
    CHECK(&PC_TECH(446) != &TECHS_CARD[6] && PC_TECH(447).mp == 0); /* beyond the last: scratch, zeros */
    PC_TECH(446).power = 55;
    CHECK(PC_TECH(446).power == 55 && TECHS_CARD[6].power == 12);
    PC_TECH(446).power = 120;

    /* texts: language, then es-419 -> spa -> eng; a description may be missing */
    CHECK(strcmp(techExtText(3, "eng", 0), "Spark") == 0 && strcmp(techExtText(3, "spa", 0), "Chispa") == 0);
    CHECK(strcmp(techExtText(3, "es-419", 0), "Chispa") == 0 && strcmp(techExtText(3, "fra", 0), "Spark") == 0);
    CHECK(strcmp(techExtText(3, "es-419", 1), "Descarga") == 0 && strcmp(techExtText(3, "eng", 1), "Zap") == 0 && techExtText(0, "eng", 1)[0] == '\0');
    CHECK(techExtText(77, "eng", 0)[0] == '\0');

    /* learnedBy patches the Digimon table: alpha (first by name) takes digimon 3 slot 2; beta and spark lose that slot;
       digimon 5 lacks script 5; digimon 4 lacks 6, which a partner's script 5 can turn into; 99 does not exist; slot 9 and
       level 120 are out of range; digimon 7's fighter is unknown (accepted) */
    CHECK(DIGIMON_DATA_CARD[0].skills[2] == 444 && DIGIMON_DATA_CARD[0].skillLevels[1] == 4);
    CHECK(DIGIMON_DATA_CARD[2].skills[1] == 0 && DIGIMON_DATA_CARD[1].skills[3] == 0);       /* digimon 5 and 4 rejected */
    CHECK(DIGIMON_DATA_CARD[3].skills[1] == 0 && DIGIMON_DATA_CARD[5].skills[4] == 0);       /* 6 (slot 9) and 8 (level 120) rejected */
    CHECK(DIGIMON_DATA_CARD[4].skills[6] == 447 && DIGIMON_DATA_CARD[4].skillLevels[5] == 50); /* digimon 7: its fighter is unknown, accepted */
    CHECK(strstr(log, "no script slot 5") != NULL && strstr(log, "no script slot 6") != NULL);
    CHECK(strstr(log, "no Digimon 99") != NULL && strstr(log, "already taken") != NULL);
    CHECK(strstr(log, "slot must be 1..6") != NULL && strstr(log, "level must be 1..99") != NULL);

    /* the save: ids stored by name next to the block; the same mods give the block back untouched */
    memset(blk, 0, sizeof blk);
    setSkill(blk, 0, 0, 0, 447 | SKILL_KNOWN);
    setSkill(blk, 1, 3, 2, 444 | (unsigned)SKILL_LAST);
    setSkill(blk, 7, 43, 5, 447);
    setSkill(blk, 2, 1, 1, 17 | SKILL_MARKED); /* an original technique stays */
    blk[0] = xorFrom4(blk);
    memcpy(copy, blk, sizeof blk);
    need = pcExtStateEncodeTechs(blk, sizeof blk, NULL, 0);
    CHECK(need > 8 && need <= sizeof chunk);
    CHECK(pcExtStateEncodeTechs(blk, sizeof blk, chunk, sizeof chunk) == need);
    CHECK(memmem(chunk, need, "zeta:spark", 10) != NULL && memmem(chunk, need, "alpha:a-tech", 12) != NULL);
    CHECK(pcExtStateEncodeTechs(copy, sizeof copy, NULL, 0) == need);
    CHECK(pcExtStateRemapTechs(copy, sizeof copy, chunk, need) == 0 && memcmp(copy, blk, sizeof blk) == 0);

    /* a save without any new technique records nothing */
    memset(copy, 0, sizeof copy);
    setSkill(copy, 0, 0, 0, 17);
    CHECK(pcExtStateEncodeTechs(copy, sizeof copy, NULL, 0) == 0);

    /* alpha goes away: a-tech vanishes and the others move down one id; flags stay, the checksum follows */
    putFile("mods/alpha/mod.json", "not json");
    logBegin();
    CHECK(loadTechMods() == 3 && techExtIdOf("zeta:spark") == 446 && techExtIdOf("alpha:a-tech") == -1);
    memcpy(copy, blk, sizeof blk);
    CHECK(pcExtStateRemapTechs(copy, sizeof copy, chunk, need) == 1);
    log = logEnd();
    CHECK(strstr(log, "technique \"alpha:a-tech\" is not defined by any installed mod") != NULL);
    CHECK(getSkill(copy, 0, 0, 0) == (446u | SKILL_KNOWN) && getSkill(copy, 7, 43, 5) == 446u);
    CHECK(getSkill(copy, 1, 3, 2) == 0 && getSkill(copy, 2, 1, 1) == (17u | SKILL_MARKED));
    CHECK(copy[0] == xorFrom4(copy) && copy[0] != blk[0]);
    /* learnedBy is applied again with the mods that remain */
    CHECK(DIGIMON_DATA_CARD[0].skills[2] == 445 && DIGIMON_DATA_CARD[0].skillLevels[1] == 2); /* the first claim is beta's now */

    /* a save with no chunk, or a damaged one, never leaves a dangling id */
    memcpy(copy, blk, sizeof blk);
    logBegin();
    CHECK(pcExtStateRemapTechs(copy, sizeof copy, NULL, 0) == 3);
    log = logEnd();
    CHECK(getSkill(copy, 0, 0, 0) == 0 && getSkill(copy, 1, 3, 2) == 0 && getSkill(copy, 7, 43, 5) == 0 && getSkill(copy, 2, 1, 1) == (17u | SKILL_MARKED));
    CHECK(strstr(log, "has no installed mod") != NULL);
    memcpy(copy, blk, sizeof blk);
    CHECK(pcExtStateRemapTechs(copy, sizeof copy, chunk, need - 3) == -1);
    CHECK(getSkill(copy, 0, 0, 0) == 0 && getSkill(copy, 7, 43, 5) == 0);

    /* no content mod: the Digimon table and the store are the game's again */
    putFile("mods/zeta/mod.json", "{\"format\":\"mod-v1\",\"name\":\"zeta\",\"version\":\"1\"}");
    CHECK(loadTechMods() == 0);
    CHECK(techExtCount() == 0 && PC_TECH(443).mp == 0 && techExtIdOf("zeta:spark") == -1);
    CHECK(DIGIMON_DATA_CARD[0].skills[2] == 111 && DIGIMON_DATA_CARD[0].skillLevels[1] == 20 && DIGIMON_DATA_CARD[4].skills[6] == 0);
    memcpy(copy, blk, sizeof blk);
    CHECK(pcExtStateRemapTechs(copy, sizeof copy, chunk, need) == 3);

    /* a malformed file or a wrong format registers nothing */
    putFile("mods/zeta/mod.json", "{\"format\":\"mod-v1\",\"name\":\"zeta\",\"version\":\"1\",\"kind\":\"content\"}");
    putFile("mods/zeta/content/techs.json", "{\"format\":\"techs-v2\",\"new\":{}}");
    CHECK(loadTechMods() == 0);
    putFile("mods/zeta/content/techs.json", "{\"format\":\"techs-v1\",\"new\":{\"x\":");
    CHECK(loadTechMods() == 0);

    techExtReset();
    CHECK(DIGIMON_DATA_CARD[0].skills[2] == 111);
    pcExtStateReset();
}

/* ---- new partner forms (P2): the name outside the u8 nameId, and the "EXTD" chunk ---- */

static const char *gForms[][2] = {{"mymod:fire", "x"}, {"mymod:ice", "y"}};
static int gFormNow[2] = {471, 472}; /* the ids the installed mods give the two forms */
static int gFormBase[2] = {5, 6};

static const char *formName(int id) {
    int i;
    for (i = 0; i < 2; i++) if (gFormNow[i] == id) return gForms[i][0];
    return NULL;
}
static int formId(const char *name) {
    int i;
    for (i = 0; i < 2; i++) if (!strcmp(name, gForms[i][0])) return gFormNow[i];
    return -1;
}
static int formBase(int id) {
    int i;
    for (i = 0; i < 2; i++) if (gFormNow[i] == id) return gFormBase[i];
    return -1;
}

#define ENTRY_ID(p, e) GAME.partners[p].info.entries[e].id

static void testNewForms(void) {
    static unsigned char blk[GAME_SAVE_SIZE], copy[GAME_SAVE_SIZE], chunk[256];
    unsigned long need, i;
    int first, dropped;
    unsigned char x;

    /* the name: an extension row answers with its own entry, any other DigimonData with its nameId */
    pcDigimonResetRows();
    first = pcDigimonExtendRows(2);
    DIGIMON_DATA_CARD[3].nameId = 77;
    PC_DIGIMON(first).nameId = 9;
    CHECK(pcDigimonNameId(&PC_DIGIMON(first)) == 9 && pcDigimonNameId(&PC_DIGIMON(3)) == 77); /* not set: the row's nameId */
    pcDigimonSetNameId(first, 600);
    pcDigimonSetNameId(first + 1, -1);
    pcDigimonSetNameId(3, 4);   /* an original row: ignored */
    pcDigimonSetNameId(99, 4);  /* outside: ignored */
    CHECK(pcDigimonNameId(&PC_DIGIMON(first)) == 600 && pcDigimonNameId(&PC_DIGIMON(first + 1)) == 0);
    CHECK(pcDigimonNameId(&PC_DIGIMON(3)) == 77 && PC_DIGIMON_NAME(&PC_DIGIMON(first)) == 600);
    {
        DigimonData copyRow = PC_DIGIMON(first); /* a copy is no row: its nameId */
        CHECK(pcDigimonNameId(&copyRow) == 9);
    }
    pcExtStateReset(); /* a save load keeps the rows and their names */
    CHECK(pcDigimonRowCount() == 57 && pcDigimonNameId(&PC_DIGIMON(first)) == 600);
    pcDigimonExtendRows(1); /* growing keeps the names of the earlier rows */
    CHECK(pcDigimonNameId(&PC_DIGIMON(first)) == 600 && pcDigimonNameId(&PC_DIGIMON(first + 2)) == 0);
    pcDigimonResetRows();
    CHECK(pcDigimonRowCount() == 55);
    DIGIMON_DATA_CARD[3].nameId = 0;

    /* the chunk */
    pcExtStateSetDigimonHooks(formName, formId, formBase);
    memset(blk, 0, sizeof blk);
    ENTRY_ID(0, 0) = 3;
    ENTRY_ID(0, 1) = 471; GAME.partners[0].info.entries[1].level = 12;
    ENTRY_ID(2, 0) = 472; GAME.partners[2].info.entries[0].level = 5;
    GAME.partners[0].info.slots[0] = 471;
    GAME.partners[0].info.slots[1] = 3;
    GAME.partners[0].battleDigivolve = 471;
    GAME.partners[2].info.slots[0] = 472;
    memcpy(blk, &GAME, sizeof GAME < sizeof blk ? sizeof GAME : sizeof blk);
    memset(&GAME, 0, sizeof GAME);
    CHECK(pcExtStateEncodeDigimon(blk, sizeof blk, NULL, 0) > 8);
    need = pcExtStateEncodeDigimon(blk, sizeof blk, chunk, sizeof chunk);
    CHECK(need > 8 && need <= sizeof chunk && contentGet32(chunk) == 1 && contentGet32(chunk + 4) == 2);
    CHECK(contentGet16(chunk + 8) == 471 && contentGet16(chunk + 10) == 5 && contentGet16(chunk + 12) == 10);   /* id, base, name length */
    CHECK(pcExtStateEncodeDigimon(blk, sizeof blk, chunk, 8) == need); /* too small: the size only */
    {
        static unsigned char orig[GAME_SAVE_SIZE];
        memset(orig, 0, sizeof orig);
        CHECK(pcExtStateEncodeDigimon(orig, sizeof orig, chunk, sizeof chunk) == 0); /* a save without new forms: no chunk */
    }
    pcExtStateEncodeDigimon(blk, sizeof blk, chunk, sizeof chunk);

    /* the same mods: nothing changes */
    memcpy(copy, blk, sizeof copy);
    CHECK(pcExtStateRemapDigimon(copy, sizeof copy, chunk, need) == 0 && memcmp(copy, blk, sizeof blk) == 0);

    /* other ids by name: every place is renumbered, the checksum byte follows */
    gFormNow[0] = 480;
    gFormNow[1] = 471;
    memcpy(copy, blk, sizeof copy);
    CHECK(pcExtStateRemapDigimon(copy, sizeof copy, chunk, need) == 0);
    memcpy(&GAME, copy, sizeof GAME < sizeof copy ? sizeof GAME : sizeof copy);
    CHECK(ENTRY_ID(0, 1) == 480 && ENTRY_ID(2, 0) == 471 && ENTRY_ID(0, 0) == 3 && GAME.partners[0].info.entries[1].level == 12);
    CHECK(GAME.partners[0].info.slots[0] == 480 && GAME.partners[0].info.slots[1] == 3 && GAME.partners[0].battleDigivolve == 480);
    CHECK(GAME.partners[2].info.slots[0] == 471);
    for (x = 0, i = 4; i < sizeof copy; i++) x ^= copy[i];
    CHECK(copy[0] == x);
    memset(&GAME, 0, sizeof GAME);

    /* a removed mod: the entry becomes its base form (keeping what it learned), the picks follow; if the partner already has
       the base the entry is emptied and the picks point at it */
    gFormNow[0] = 471;
    gFormNow[1] = -1;
    memcpy(copy, blk, sizeof copy);
    CHECK(pcExtStateRemapDigimon(copy, sizeof copy, chunk, need) == 1);
    memcpy(&GAME, copy, sizeof GAME < sizeof copy ? sizeof GAME : sizeof copy);
    CHECK(ENTRY_ID(2, 0) == 6 && GAME.partners[2].info.entries[0].level == 5 && GAME.partners[2].info.slots[0] == 6);
    CHECK(ENTRY_ID(0, 1) == 471 && GAME.partners[0].info.slots[0] == 471);
    memset(&GAME, 0, sizeof GAME);
    gFormNow[0] = 471;
    gFormNow[1] = 472;
    gFormBase[0] = 3; /* partner 0 already has form 3: the chunk records that base */
    need = pcExtStateEncodeDigimon(blk, sizeof blk, chunk, sizeof chunk);
    gFormNow[0] = -1;
    gFormNow[1] = -1;
    memcpy(copy, blk, sizeof copy);
    CHECK(pcExtStateRemapDigimon(copy, sizeof copy, chunk, need) == 2);
    memcpy(&GAME, copy, sizeof GAME < sizeof copy ? sizeof GAME : sizeof copy);
    CHECK(ENTRY_ID(0, 1) == 0 && GAME.partners[0].info.entries[1].level == 0 && ENTRY_ID(0, 0) == 3);
    CHECK(GAME.partners[0].info.slots[0] == 3 && GAME.partners[0].battleDigivolve == 3 && GAME.partners[0].info.slots[1] == 3);
    memset(&GAME, 0, sizeof GAME);
    gFormBase[0] = 5;
    gFormNow[0] = 471;
    gFormNow[1] = 472;
    need = pcExtStateEncodeDigimon(blk, sizeof blk, chunk, sizeof chunk);

    /* no chunk or a damaged chunk: no base is known, so entries are emptied and picks cleared */
    memcpy(copy, blk, sizeof copy);
    dropped = pcExtStateRemapDigimon(copy, sizeof copy, NULL, 0);
    CHECK(dropped == 0);
    memcpy(&GAME, copy, sizeof GAME < sizeof copy ? sizeof GAME : sizeof copy);
    CHECK(ENTRY_ID(0, 1) == 0 && ENTRY_ID(2, 0) == 0 && GAME.partners[0].info.slots[0] == 0 && GAME.partners[0].battleDigivolve == 0 &&
          ENTRY_ID(0, 0) == 3);
    memset(&GAME, 0, sizeof GAME);
    gFormNow[0] = 471;
    gFormNow[1] = 472;
    memcpy(copy, blk, sizeof copy);
    CHECK(pcExtStateRemapDigimon(copy, sizeof copy, chunk, need - 3) == -1);
    memcpy(&GAME, copy, sizeof GAME < sizeof copy ? sizeof GAME : sizeof copy);
    CHECK(ENTRY_ID(0, 1) == 0 && ENTRY_ID(2, 0) == 0);
    memset(&GAME, 0, sizeof GAME);

    /* an id with no name is not recorded */
    gFormNow[0] = 900;
    memcpy(copy, blk, sizeof copy);
    CHECK(pcExtStateEncodeDigimon(copy, sizeof copy, chunk, sizeof chunk) > 8 && contentGet32(chunk + 4) == 1); /* only the form still named */
    gFormNow[0] = 471;
    pcExtStateSetDigimonHooks(NULL, NULL, NULL);
    CHECK(pcExtStateEncodeDigimon(blk, sizeof blk, chunk, sizeof chunk) > 0); /* both unnamed: nothing to write but a log */
    memset(&GAME, 0, sizeof GAME);
}

/* ---- partner entries beyond 44 and the evolution lists (P3) ---- */

static const char *entTechName(int id) {
    return id == 447 ? "mymod:spark" : NULL;
}
static int entTechId(const char *name) {
    return !strcmp(name, "mymod:spark") ? 447 : -1;
}

static void testPartnerEntries(void) {
    static unsigned char chunk[4096], again[4096];
    PartnerEntry *e;
    unsigned long need;
    int i, at;

    memset(&GAME, 0, sizeof GAME);
    pcPartnerEntriesReset();

    /* the first 44 are GAME's own, exactly as before; nothing is stored until something asks for room */
    CHECK(pcPartnerEntryLimit(0) == 44 && PC_PARTNER_ENTRY_LIMIT(3) == 44);
    PC_PARTNER_ENTRY(2, 43).id = 77;
    CHECK(GAME.partners[2].info.entries[43].id == 77 && &PC_PARTNER_ENTRY(2, 43) == &GAME.partners[2].info.entries[43]);
    CHECK(pcExtStateEncodeEntries(NULL, 0) == 0); /* nothing stored: no chunk */

    /* an entry past 44 is the store's: the limit grows one at a time, up to the maximum, and GAME is not touched */
    at = pcPartnerEntryGrow(2);
    CHECK(at == 44 && pcPartnerEntryLimit(2) == 45 && pcPartnerEntryLimit(1) == 44);
    PC_PARTNER_ENTRY(2, 44).id = 480;
    PC_PARTNER_ENTRY(2, 44).level = 9;
    PC_PARTNER_ENTRY(2, 44).exp = 123456;
    CHECK(PC_PARTNER_ENTRY(2, 44).id == 480 && PC_PARTNER_ENTRY(2, 44).level == 9);
    for (i = 45; i < PC_PARTNER_ENTRY_MAX; i++) CHECK(pcPartnerEntryGrow(2) == i);
    CHECK(pcPartnerEntryLimit(2) == PC_PARTNER_ENTRY_MAX && pcPartnerEntryGrow(2) == -1); /* the port's maximum */
    CHECK(PC_PARTNER_ENTRY(2, 44).id == 480 && PC_PARTNER_ENTRY(2, 255).id == 0); /* growing keeps what was there; new ones are empty */
    CHECK(GAME.partners[2].info.entries[43].id == 77 && GAME.partners[3].info.entries[0].id == 0);
    CHECK(pcPartnerEntryGrow(-1) == -1 && pcPartnerEntryGrow(PARTNER_COUNT) == -1);

    /* outside the limit: a scratch entry that reads zero and drops writes */
    e = pcPartnerEntryRef(&GAME.partners[1].info, 1, 44);
    e->id = 5;
    CHECK(pcPartnerEntryRef(&GAME.partners[1].info, 1, 44)->id == 0 && pcPartnerEntryRef(&GAME.partners[1].info, 1, -1)->id == 0);
    CHECK(pcPartnerEntryRef(&GAME.partners[2].info, 2, PC_PARTNER_ENTRY_MAX)->id == 0);

    /* the chunk: names, not ids, for the form and the technique; the level, exp and skills come back */
    pcPartnerEntriesReset();
    memset(&GAME, 0, sizeof GAME);
    pcExtStateSetDigimonHooks(formName, formId, formBase);
    pcExtStateSetTechHooks(entTechName, entTechId);
    gFormNow[0] = 471;
    gFormNow[1] = 472;
    pcPartnerEntryGrow(4);
    pcPartnerEntryGrow(4);
    pcPartnerEntryGrow(4);
    pcPartnerEntryGrow(6);
    PC_PARTNER_ENTRY(4, 44).id = 471;                 /* a new form */
    PC_PARTNER_ENTRY(4, 44).level = 12;
    PC_PARTNER_ENTRY(4, 44).exp = 5000;
    PC_PARTNER_ENTRY(4, 44).skills[0] = (s16)(447 | SKILL_KNOWN);   /* a mod technique, known */
    PC_PARTNER_ENTRY(4, 44).skills[1] = (s16)(17 | SKILL_MARKED);   /* an original one */
    PC_PARTNER_ENTRY(4, 45).id = 0;                   /* a hole: not written */
    PC_PARTNER_ENTRY(4, 46).id = 9;                   /* an original form beyond 44 */
    PC_PARTNER_ENTRY(4, 46).level = 3;
    PC_PARTNER_ENTRY(6, 44).id = 472;
    PC_PARTNER_ENTRY(6, 44).level = 1;
    need = pcExtStateEncodeEntries(NULL, 0);
    CHECK(need > 8 && need <= sizeof chunk);
    CHECK(pcExtStateEncodeEntries(chunk, need) == need && contentGet32(chunk) == 1 && contentGet32(chunk + 4) == 3);
    CHECK(pcExtStateEncodeEntries(chunk, 8) == need); /* too small: the size only */

    /* the same mods: everything back, compacted (the hole is gone) */
    pcPartnerEntriesReset();
    CHECK(pcPartnerEntryLimit(4) == 44);
    CHECK(pcExtStateDecodeEntries(chunk, need) == 0);
    CHECK(pcPartnerEntryLimit(4) == 46 && pcPartnerEntryLimit(6) == 45 && pcPartnerEntryLimit(0) == 44);
    CHECK(PC_PARTNER_ENTRY(4, 44).id == 471 && PC_PARTNER_ENTRY(4, 44).level == 12 && PC_PARTNER_ENTRY(4, 44).exp == 5000);
    CHECK(PC_PARTNER_ENTRY(4, 44).skills[0] == (s16)(447 | SKILL_KNOWN) && PC_PARTNER_ENTRY(4, 44).skills[1] == (s16)(17 | SKILL_MARKED));
    CHECK(PC_PARTNER_ENTRY(4, 45).id == 9 && PC_PARTNER_ENTRY(4, 45).level == 3 && PC_PARTNER_ENTRY(6, 44).id == 472);
    CHECK(pcExtStateEncodeEntries(again, sizeof again) == need && memcmp(chunk, again, need) == 0); /* stable */

    /* other ids by name */
    gFormNow[0] = 490;
    CHECK(pcExtStateDecodeEntries(chunk, need) == 0 && PC_PARTNER_ENTRY(4, 44).id == 490 && PC_PARTNER_ENTRY(6, 44).id == 472);
    gFormNow[0] = 471;

    /* a removed mod: the form's entry is dropped (a [saves] line), a missing technique empties its skill slot */
    gFormNow[1] = -1;
    CHECK(pcExtStateDecodeEntries(chunk, need) == 1);
    CHECK(pcPartnerEntryLimit(6) == 44 && pcPartnerEntryLimit(4) == 46);
    pcExtStateSetTechHooks(NULL, NULL);
    CHECK(pcExtStateDecodeEntries(chunk, need) == 1 && PC_PARTNER_ENTRY(4, 44).skills[0] == 0 &&
          PC_PARTNER_ENTRY(4, 44).skills[1] == (s16)(17 | SKILL_MARKED));
    pcExtStateSetTechHooks(entTechName, entTechId);
    gFormNow[1] = 472;

    /* none or damaged: nothing is kept; a new load starts clean */
    CHECK(pcExtStateDecodeEntries(NULL, 0) == 0 && pcPartnerEntryLimit(4) == 44);
    CHECK(pcExtStateDecodeEntries(chunk, need - 3) == -1 && pcPartnerEntryLimit(4) == 44 && pcPartnerEntryLimit(6) == 44);
    pcExtStateDecodeEntries(chunk, need);
    pcExtStateReset(); /* a save load / new game */
    CHECK(pcPartnerEntryLimit(4) == 44 && pcExtStateEncodeEntries(NULL, 0) == 0);

    /* a form nobody names cannot be saved by name */
    pcPartnerEntryGrow(1);
    PC_PARTNER_ENTRY(1, 44).id = 900;
    CHECK(pcExtStateEncodeEntries(NULL, 0) == 0);
    pcPartnerEntriesReset();
    pcExtStateSetDigimonHooks(NULL, NULL, NULL);
    pcExtStateSetTechHooks(NULL, NULL);
    memset(&GAME, 0, sizeof GAME);
}

static void testEvolutionStore(void) {
    static PcEvolution orig[PARTNER_COUNT][PC_EVOLUTION_ORIG_COUNT];
    PcEvolution ev;
    PcEvolution *list;
    int i;

    for (i = 0; i < PC_EVOLUTION_ORIG_COUNT; i++) { orig[2][i].digimon = i + 1; orig[2][i].value = 1; }
    pcEvolutionReset();
    pcEvolutionSetOriginal(orig);
    CHECK(pcEvolutionCount(2) == 44 && pcEvolutionList(2, orig[2]) == (void *)orig[2]); /* nothing added: the original list itself */

    memset(&ev, 0, sizeof ev);
    ev.digimon = 60;
    ev.needs[0].digimon = 12;
    ev.needs[0].level = 7;
    ev.stat = 7;
    ev.value = 30;
    CHECK(pcEvolutionExtend(2, &ev) == 44 && pcEvolutionCount(2) == 45 && pcEvolutionCount(1) == 44);
    ev.digimon = 61;
    CHECK(pcEvolutionExtend(2, &ev) == 45 && pcEvolutionCount(2) == 46);
    list = pcEvolutionList(2, orig[2]);
    CHECK(list != orig[2] && list[0].digimon == 1 && list[43].digimon == 44 && list[44].digimon == 60 && list[45].digimon == 61);
    CHECK(list[44].needs[0].digimon == 12 && list[44].needs[0].level == 7 && list[44].stat == 7 && list[44].value == 30);
    CHECK(orig[2][44 - 1].digimon == 44 && pcEvolutionList(1, orig[1]) == (void *)orig[1]); /* the original is not written */
    CHECK(pcEvolutionExtend(-1, &ev) == -1 && pcEvolutionExtend(PARTNER_COUNT, &ev) == -1 && pcEvolutionExtend(2, NULL) == -1);

    pcExtStateReset(); /* a save load does not touch the lists: they belong to the mods loaded at boot */
    CHECK(pcEvolutionCount(2) == 46);
    pcEvolutionReset();
    CHECK(pcEvolutionCount(2) == 44 && pcEvolutionList(2, orig[2]) == (void *)orig[2]);
    pcEvolutionSetOriginal(NULL);
    CHECK(pcEvolutionExtend(2, &ev) == -1);
}

/* ---- cards from content mods (U8b): ids, ownership by name, album size, art, effect, decks across a removed mod ---- */

static int fixImage(int id, unsigned char *out)
{
    CardImage *c = (CardImage *)out;
    if (id < 1 || id > 316) return 0;
    memset(out, 0, CARD_EXT_IMAGE_SIZE);
    c->color = 1;
    c->ap = (u8)(id % 50);
    c->hp = 7;
    c->kind = id <= 60 ? 4 : 16;
    c->rank = (s16)id;
    out[12] = 0x77; /* the base's art marker */
    return 1;
}

static int fixEffect(int id, unsigned char *out)
{
    unsigned char *e = out;
    if (id < 1 || id > 60) return 0;
    memset(out, 0, CARD_EXT_EFFECT_SIZE);
    e[0] = (unsigned char)id; /* condition */
    e[2] = 3;                 /* message */
    e[4] = 9;                 /* script[0] */
    return 1;
}

const void *pcCardEffect(int id);
static const CardExtBase kFixBase = {fixImage, fixEffect};
static int gStarter[DECK_SIZE];

static int loadCardMods(void)
{
    char root[700];
    snprintf(root, sizeof root, "%s/mods", gRoot);
    modsInit(root, NULL);
    cardExtSetBase(&kFixBase);
    return cardExtLoad();
}

static void putCardArt(const char *rel)
{
    uint8_t px[32 * 32], rgb[3 * 4] = {0, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 255}, alpha[1] = {0}, *png = NULL;
    size_t n = 0, i;
    char path[900];
    FILE *f;
    for (i = 0; i < sizeof px; i++) px[i] = (uint8_t)(i % 4);
    CHECK(pngWriteIndexed(px, 32, 32, 8, rgb, 4, alpha, 1, &png, &n) == 0);
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    {
        char dir[900], *slash;
        snprintf(dir, sizeof dir, "mkdir -p '%s'", path);
        slash = strrchr(dir, '/');
        *slash = '\'';
        slash[1] = 0;
        if (system(dir)) {}
    }
    f = fopen(path, "wb");
    if (f) {
        fwrite(png, 1, n, f);
        fclose(f);
    }
    free(png);
}

static void testCardRegistry(void)
{
    unsigned char chunk[512], block[GAME_SAVE_SIZE];
    unsigned long need;
    const unsigned char *img;
    int i;

    for (i = 0; i < DECK_SIZE; i++) gStarter[i] = 100 + i;
    pcExtStateSetCardFallback(gStarter);
    /* no card mods: the album is the original size and nothing is recorded */
    CHECK(pcCardTotal() == 315);
    memset(block, 0, sizeof block);
    CHECK(pcExtStateEncodeCards(block, sizeof block, NULL, 0) == 0);

    modJson("zeta", "content");
    modJson("alpha", "content");
    putFile("mods/zeta/content/cards.json",
            "{\"format\":\"cards-v1\",\"new\":{"
            "\"b-card\":{\"base\":120,\"ap\":30,\"hp\":25,\"rank\":150,\"price\":250,\"name\":{\"eng\":\"Bee Card\",\"spa\":\"Carta Abeja\"}},"
            "\"a-card\":{\"base\":12,\"effect\":{\"like\":5,\"message\":9,\"script\":[3,7]},\"description\":{\"eng\":\"Does a thing\"},"
            "\"name\":{\"eng\":\"Ay Card\"}},"
            "\"bad-base\":{\"base\":9999,\"name\":{\"eng\":\"x\"}},"
            "\"bad-ap\":{\"base\":120,\"ap\":100,\"price\":40000,\"name\":{\"eng\":\"Bad AP\"}},"
            "\"bad-script\":{\"base\":12,\"effect\":{\"script\":[0,200]},\"name\":{\"eng\":\"Bad Script\"}},"
            "\"no-name\":{\"base\":120}}}");
    putFile("mods/alpha/content/cards.json",
            "{\"format\":\"cards-v1\",\"new\":{\"z-card\":{\"base\":\"dw3:card/120\",\"name\":{\"eng\":\"Zed Card\"}}}}");
    putCardArt("mods/zeta/card/b-card.png");

    CHECK(loadCardMods() == 5); /* alpha:z-card, zeta:a-card, zeta:b-card, zeta:bad-ap and zeta:bad-script (only their bad field is refused) */
    CHECK(cardExtIdOf("alpha:z-card") == 317 && cardExtIdOf("zeta:a-card") == 318 && cardExtIdOf("zeta:b-card") == 319);
    CHECK(cardExtIdOf("zeta:bad-base") == -1 && cardExtIdOf("zeta:no-name") == -1);
    CHECK(strcmp(cardExtNameOf(319), "zeta:b-card") == 0 && cardExtNameOf(316) == NULL);
    CHECK(pcCardTotal() == PC_CARD_ORIG_COUNT + 5);
    /* U8c: the shop's price is the card's own: its cards.json "price" (a number out of range is refused and the default stays), else 1 */
    CHECK(pcCardPrice(319) == 250 && pcCardPrice(317) == 1 && pcCardPrice(318) == 1);
    CHECK(pcCardPrice(cardExtIdOf("zeta:bad-ap")) == 1);
    CHECK(pcCardPrice(5) == 1 && pcCardPrice(400) == 1);
    CHECK(cardExtByName("dw3:card/12") == 12 && cardExtByName("zeta:b-card") == 319 && cardExtIdOk(319) && !cardExtIdOk(330));

    /* the image record: the base's with the overrides; the art from the PNG replaces the base's TIM */
    img = cardExtImageBytes(319);
    CHECK(img != NULL && img[0] == 1 && img[1] == 30 && img[2] == 25 && img[3] == 16 && img[8] == (150 & 255));
    CHECK(img[12] == 0x10 && img[16] == 9 && img[20] == 0x0C && img[21] == 0x02); /* TIM header, flags 9, CLUT block 0x20C */
    CHECK(img[12 + 8 + 8] == 0 && img[12 + 8 + 9] == 1 && img[12 + 8 + 10] == 1 && img[12 + 8 + 11] == 0); /* the CLUT block is 256 x 1 */
    CHECK(img[12 + 8 + 0x20C + 8] == 16 && img[12 + 8 + 0x20C + 10] == 32);                              /* the image block is 16 x 32 halfwords */
    CHECK(img[12 + 8 + 12] == 0 && img[12 + 8 + 12 + 1] == 0);                  /* colour 0: alpha 0 -> 0x0000 */
    CHECK(img[12 + 8 + 12 + 2] == 0x1F && img[12 + 8 + 12 + 3] == 0x80);        /* colour 1: red with the STP bit */
    CHECK(img[12 + 8 + 12 + 4] == 0x00 && img[12 + 8 + 12 + 5] == 0x80); /* colour 2: opaque black -> 0x8000 */
    CHECK(img[12 + 8 + 0x20C + 12 + 5] == 1 && img[12 + 8 + 0x20C + 12 + 6] == 2); /* the pixels are the PNG's indices */
    CHECK(pcCardImage(319) == (CardImage *)img && pcCardImage(316) == NULL && pcCardImage(5) == NULL && pcCardImage(400) == NULL);
    CHECK(cardExtImageBytes(317)[12] == 0x77); /* no PNG: the base's art */
    CHECK(cardExtImageBytes(318)[1] == 12 % 50 && cardExtImageBytes(318)[2] == 7);
    CHECK(cardExtImageBytes(321)[1] == 12 % 50); /* bad-script keeps its base's stats */
    CHECK(cardExtImageBytes(320)[1] == 120 % 50); /* bad-ap: the field is refused, the base's AP stays */

    /* the effect: the base's row, "like" another card's, field overrides, the script; a bad script is rejected whole */
    {
        const unsigned char *e = cardExtEffectBytes(318);
        CHECK(e != NULL && e[0] == 5 && e[2] == 9 && e[4] == 3 && e[5] == 7 && e[6] == 0);
        CHECK(cardExtEffectBytes(319) == NULL); /* a Digimon base has no effect */
        e = cardExtEffectBytes(321);
        CHECK(e != NULL && e[0] == 12 && e[4] == 9); /* the base (12) row unchanged: the script was refused */
        CHECK(pcCardEffect(318) != NULL && pcCardEffect(319) == NULL && pcCardEffect(100) == NULL);
    }

    /* texts: names per language with the fallback, the description */
    CHECK(strcmp(cardExtText(2, "eng", 0), "Bee Card") == 0 && strcmp(cardExtText(2, "es-419", 0), "Carta Abeja") == 0);
    CHECK(strcmp(cardExtText(1, "eng", 1), "Does a thing") == 0);

    /* ownership: originals in GAME, new ids in the store, out of range scratch */
    memset(&GAME, 0, sizeof GAME);
    pcExtStateReset();
    PC_CARD_COUNT(5) = 4;
    PC_CARD_SEEN(5) = 1;
    CHECK(GAME.cardsCard[5] == 4 && GAME.cardsSeenCard[5] == 1);
    PC_CARD_COUNT(318) = 3;
    PC_CARD_SEEN(318) = 1;
    PC_CARD_COUNT(317) += 2;
    PC_CARD_COUNT(317)--;
    CHECK(PC_CARD_COUNT(318) == 3 && PC_CARD_SEEN(318) == 1 && PC_CARD_COUNT(317) == 1 && PC_CARD_SEEN(319) == 0);
    {
        GameState snap;
        memcpy(&snap, &GAME, sizeof GAME);
        PC_CARD_COUNT(PC_CARD_LIST_MAX) = 5;
        PC_CARD_SEEN(-1) = 5;
        CHECK(PC_CARD_COUNT(PC_CARD_LIST_MAX) == 0 && PC_CARD_SEEN(-1) == 0 && memcmp(&snap, &GAME, sizeof GAME) == 0);
    }
    /* a deck holding a new card, saved by name */
    memset(block, 0, sizeof block);
    {
        Deck *d = (Deck *)(block + offsetof(GameState, decks));
        d[0].cards[0] = 319;
        d[0].cards[1] = 318;
        d[1].cards[39] = 7;
    }
    need = pcExtStateEncodeCards(block, sizeof block, NULL, 0);
    CHECK(need > 8 && need <= sizeof chunk);
    CHECK(pcExtStateEncodeCards(block, sizeof block, chunk, need) == need);
    CHECK(contentGet32(chunk) == 1 && contentGet32(chunk + 4) == 3); /* 317 and 318 (counts), 319 (deck only) */

    /* the same mods: nothing changes */
    memset(&GAME, 0, sizeof GAME);
    pcExtStateReset();
    CHECK(pcExtStateDecodeCards(chunk, need) == 0);
    CHECK(PC_CARD_COUNT(318) == 3 && PC_CARD_SEEN(318) == 1 && PC_CARD_COUNT(317) == 1 && PC_CARD_COUNT(319) == 0);
    CHECK(pcExtStateRemapCards(block, sizeof block, chunk, need) == 0);
    CHECK(((Deck *)(block + offsetof(GameState, decks)))[0].cards[0] == 319);

    /* the zeta mod loses a-card (and so b-card moves to id 318): the counts follow the names, the deck is remapped, the lost card's
       slot becomes the starter deck's card, the save stays loadable */
    putFile("mods/zeta/content/cards.json",
            "{\"format\":\"cards-v1\",\"new\":{\"b-card\":{\"base\":120,\"name\":{\"eng\":\"Bee Card\"}}}}");
    CHECK(loadCardMods() == 2);
    CHECK(cardExtIdOf("alpha:z-card") == 317 && cardExtIdOf("zeta:b-card") == 318 && pcCardTotal() == PC_CARD_ORIG_COUNT + 2);
    memset(&GAME, 0, sizeof GAME);
    pcExtStateReset();
    CHECK(pcExtStateDecodeCards(chunk, need) == 1); /* a-card dropped */
    CHECK(PC_CARD_COUNT(317) == 1 && PC_CARD_COUNT(318) == 0 && PC_CARD_COUNT(319) == 0);
    CHECK(pcExtStateRemapCards(block, sizeof block, chunk, need) == 1);
    {
        const Deck *d = (const Deck *)(block + offsetof(GameState, decks));
        CHECK(d[0].cards[0] == 318 && d[0].cards[1] == 101 && d[1].cards[39] == 7);
    }
    {
        unsigned char x = 0;
        size_t b;
        for (b = 4; b < sizeof block; b++) x ^= block[b];
        CHECK(block[0] == x); /* the checksum byte follows the change */
    }

    /* all the mods gone (a save with cards, no mod): everything dropped, every deck slot readable, the chunk of a damaged save */
    modsInit(NULL, NULL);
    CHECK(cardExtLoad() == 0 && pcCardTotal() == 315);
    memset(&GAME, 0, sizeof GAME);
    pcExtStateReset();
    CHECK(pcExtStateDecodeCards(chunk, need) == 3);
    CHECK(PC_CARD_COUNT(317) == 0);
    memset(block, 0, sizeof block);
    ((Deck *)(block + offsetof(GameState, decks)))[2].cards[3] = 318;
    CHECK(pcExtStateRemapCards(block, sizeof block, chunk, need) == 1);
    CHECK(((Deck *)(block + offsetof(GameState, decks)))[2].cards[3] == 103);
    CHECK(pcExtStateDecodeCards(chunk, 5) == -1); /* damaged: empty */
    CHECK(pcExtStateRemapCards(block, sizeof block, chunk, 5) == -1); /* damaged chunk: reported */
    ((Deck *)(block + offsetof(GameState, decks)))[1].cards[0] = 400;
    CHECK(pcExtStateRemapCards(block, sizeof block, NULL, 0) == 0 && ((Deck *)(block + offsetof(GameState, decks)))[1].cards[0] == 400); /* no chunk: untouched */
    CHECK(pcExtStateRemapCards(block, sizeof block, chunk, 5) == -1 && ((Deck *)(block + offsetof(GameState, decks)))[1].cards[0] == gStarter[0]); /* damaged: replaced */
    cardExtReset();
    CHECK(pcCardTotal() == 315);
}

int main(void)
{
    int i;
    GameState snapshot;
    const char *tmp = getenv("TMPDIR");

    memset(&GAME, 0, sizeof GAME);
    pcExtStateReset();

    /* original ids reach the arrays inside GAME, first and last */
    PC_ITEM_COUNT(0) = 3;
    PC_ITEM_COUNT(402) = 99;
    PC_ITEM_EQUIPPED(1) = 2;
    PC_ITEM_EQUIPPED(402) = 5;
    CHECK(GAME.itemsCard[0] == 3 && GAME.itemsCard[402] == 99);
    CHECK(GAME.equippedItemsCard[1] == 2 && GAME.equippedItemsCard[402] == 5);
    CHECK(pcExtItemCapacity() == PC_ITEM_ORIG_COUNT); /* nothing above was touched: no store yet */

    /* lvalue forms the game uses */
    PC_ITEM_COUNT(7)++;
    PC_ITEM_COUNT(7) += 4;
    PC_ITEM_COUNT(7)--;
    CHECK(GAME.itemsCard[7] == 4);

    /* the first id above the original range: the store, not GAME */
    memcpy(&snapshot, &GAME, sizeof GAME);
    CHECK(PC_ITEM_COUNT(403) == 0);
    PC_ITEM_COUNT(403) = 12;
    PC_ITEM_EQUIPPED(403) = 1;
    CHECK(PC_ITEM_COUNT(403) == 12 && PC_ITEM_EQUIPPED(403) == 1);
    CHECK(memcmp(&snapshot, &GAME, sizeof GAME) == 0);
    CHECK(pcExtItemCapacity() > 403);

    /* growth keeps what was there and starts the new ids at zero */
    PC_ITEM_COUNT(1500) = 77;
    CHECK(PC_ITEM_COUNT(403) == 12 && PC_ITEM_COUNT(1500) == 77 && PC_ITEM_COUNT(1499) == 0);
    CHECK(PC_ITEM_EQUIPPED(403) == 1 && PC_ITEM_EQUIPPED(1500) == 0);
    CHECK(memcmp(&snapshot, &GAME, sizeof GAME) == 0);

    /* the two arrays are separate */
    PC_ITEM_EQUIPPED(2000) = 9;
    CHECK(PC_ITEM_COUNT(2000) == 0 && PC_ITEM_EQUIPPED(2000) == 9);

    /* out of range: reads 0, a write is lost, GAME is never touched */
    memcpy(&snapshot, &GAME, sizeof GAME);
    CHECK(PC_ITEM_COUNT(-1) == 0);
    PC_ITEM_COUNT(-1) = 55;
    CHECK(PC_ITEM_COUNT(-1) == 0);
    PC_ITEM_COUNT(PC_EXT_ITEM_LIMIT) = 55;
    PC_ITEM_EQUIPPED(PC_EXT_ITEM_LIMIT + 100) = 55;
    PC_ITEM_COUNT(-32768) = 55;
    CHECK(PC_ITEM_COUNT(PC_EXT_ITEM_LIMIT) == 0);
    CHECK(PC_ITEM_EQUIPPED(PC_EXT_ITEM_LIMIT + 100) == 0);
    CHECK(memcmp(&snapshot, &GAME, sizeof GAME) == 0);

    /* the last id the store accepts */
    PC_ITEM_COUNT(PC_EXT_ITEM_LIMIT - 1) = 8;
    CHECK(PC_ITEM_COUNT(PC_EXT_ITEM_LIMIT - 1) == 8);
    CHECK(memcmp(&snapshot, &GAME, sizeof GAME) == 0);

    /* reset forgets the extension */
    pcExtStateReset();
    CHECK(pcExtItemCapacity() == PC_ITEM_ORIG_COUNT);
    CHECK(PC_ITEM_COUNT(403) == 0);

    /* every original id round-trips on its own cell */
    memset(&GAME, 0, sizeof GAME);
    for (i = 0; i < PC_ITEM_ORIG_COUNT; i++) PC_ITEM_COUNT(i) = (signed char)(i % 100);
    for (i = 0; i < PC_ITEM_ORIG_COUNT; i++) CHECK(GAME.itemsCard[i] == (signed char)(i % 100));
    for (i = 0; i < PC_ITEM_ORIG_COUNT; i++) CHECK(PC_ITEM_EQUIPPED(i) == 0);
    memset(&GAME, 0, sizeof GAME);
    CHECK(gameIsClean());

    pcExtStateReset();
    snprintf(gRoot, sizeof gRoot, "%s/extstatetest.%d", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", (int)getpid());
    {
        char cmd[700];
        snprintf(cmd, sizeof cmd, "mkdir -p '%s'", gRoot);
        if (system(cmd)) return 2;
        testTechAccess();
        testDigimonAccess();
        testNewForms();
        testPartnerEntries();
        testEvolutionStore();
        testTechRegistry();
        testItemRegistry();
        testTablesNameModItems();
        testOriginalNames();
        testFitLine();
        testCardRegistry();
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
        if (system(cmd)) {}
    }
    if (failures == 0) printf("extstatetest: ok\n");
    return failures ? 1 : 0;
}
