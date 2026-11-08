#define _GNU_SOURCE 1
/*
 * Host test of the partner registry from content mods (pc/shim/partner_ext.c, Betamon pack P4): partners/<key>/partner.json and content/teams.json are read,
 * validated (one log line per rejection), given deterministic indices by sorted name, registered before any save is read, and their
 * equipment, evolution list, texts and starter teams reach the tables the game reads. No game, no disc, no window.
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
#include "shim/mods.h"
#include "shim/partner_ext.h"
#include "shim/port_log.h"

GameState GAME;
/* what the game's items.c provides, as far as ext_state.c needs it (extstatetest does the same) */
TechData TECHS_CARD[443];
DigimonData DIGIMON_DATA_CARD[55];
/* the game's original teams (party.c) */
u8 STARTER_PARTIES[][3] = {{0, 6, 7}, {2, 3, 6}, {1, 5, 7}};

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "partnerexttest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

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

/* a partner of a mod: mods/<mod>/partners/<key>/partner.json (A8.2), `body` the members after the header */
static void putPartner(const char *mod, const char *key, const char *body)
{
    char rel[300], text[8192];

    snprintf(rel, sizeof rel, "mods/%s/partners/%s/partner.json", mod, key);
    snprintf(text, sizeof text, "{\"format\":\"partners-partner-v1\",%s}", body);
    putFile(rel, text);
}

static void modJson(const char *mod, const char *kind)
{
    char rel[200], text[200];

    snprintf(rel, sizeof rel, "mods/%s/mod.json", mod);
    snprintf(text, sizeof text, "{\"format\":\"mod-v1\",\"name\":\"%s\",\"version\":\"1.0\",\"kind\":\"%s\"}", mod, kind);
    putFile(rel, text);
}

/* the host's resolvers, as stubs: partner forms are "dw3:digimon/<id>" (row id - 3 + 1... any id from 3 up: row = id, ref = id + 1) and
   "f:a" (row 56) and "f:b" (row 57); items are "dw3:item/<1..402>" and "m:item" (500) */
static long long stubForm(const char *name)
{
    if (!strcmp(name, "f:a")) return 57;
    if (!strcmp(name, "f:b")) return 58;
    if (!strncmp(name, "dw3:digimon/", 12)) {
        long id = strtol(name + 12, NULL, 10);

        return id >= 3 && id <= 470 ? id + 1 : -1; /* the stub's row is the id, so a form's row + 1 is id + 1 */
    }
    return -1;
}

static long long stubItem(const char *name)
{
    if (!strcmp(name, "m:item")) return 500;
    if (!strncmp(name, "dw3:item/", 9)) {
        long id = strtol(name + 9, NULL, 10);

        return id >= 1 && id <= 402 ? id : -1;
    }
    return -1;
}

static int gSavedErr;
static const char *gLogPath = "/tmp/partnerexttest.log";

static void logBegin(void)
{
    int fd;

    fflush(stderr);
    gSavedErr = dup(2);
    fd = open(gLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

/* the log text since logBegin (malloc'ed) */
static char *logEnd(void)
{
    char *buf = calloc(1, 65536);
    FILE *f;

    fflush(stderr);
    dup2(gSavedErr, 2);
    close(gSavedErr);
    f = fopen(gLogPath, "rb");
    if (f != NULL) {
        size_t n = fread(buf, 1, 65535, f);

        buf[n] = 0;
        fclose(f);
    }
    return buf;
}

static int load(void)
{
    char root[700];
    PartnerExtHooks h = {stubForm, stubItem};

    snprintf(root, sizeof root, "%s/mods", gRoot);
    modsInit(root, NULL);
    partnerExtSetHooks(&h);
    return partnerExtLoad();
}

static const PcEvolution *evoList(int partner)
{
    return (const PcEvolution *)pcEvolutionList(partner, NULL);
}

static void testPartners(void)
{
    char *log;
    int n;

    modJson("zeta", "content");
    modJson("alpha", "content");
    modJson("plain", "assets");
    putPartner("zeta", "bee",
               "\"base\":\"dw3:partner/3\",\"firstForm\":\"f:a\","
               "\"title\":{\"eng\":\"Bee\",\"es-419\":\"Abeja\"},\"description\":{\"eng\":\"Buzzes.\\nStings.\",\"es-419\":\"Zumba.\"},"
               "\"equip\":{\"like\":5,\"allow\":[\"dw3:item/10\",\"m:item\"],\"deny\":[\"dw3:item/11\",\"dw3:item/10\"]},"
               "\"equipSet\":{\"items\":[\"dw3:item/1\",\"dw3:item/2\",\"dw3:item/3\",\"dw3:item/4\"],\"bonuses\":[4,4,-4,0,0,20]},"
               "\"evolution\":[{\"digimon\":\"f:b\",\"needs\":[{\"digimon\":\"f:a\",\"level\":20}],\"stat\":\"level\",\"value\":30},"
               "                 {\"digimon\":\"dw3:digimon/300\",\"stat\":5,\"value\":99},"
               "                 {\"digimon\":\"nobody:there\"},"
               "                 {\"digimon\":\"f:b\",\"needs\":[{\"digimon\":\"nobody:there\"}]}]");
    putPartner("zeta", "no-form", "\"base\":0,\"title\":{\"eng\":\"x\"}");
    putPartner("zeta", "bad-form", "\"base\":0,\"firstForm\":\"q:z\",\"title\":{\"eng\":\"x\"}");
    putPartner("zeta", "bad-base", "\"base\":8,\"firstForm\":\"f:a\",\"title\":{\"eng\":\"x\"}");
    putPartner("zeta", "no-name", "\"base\":0,\"firstForm\":\"f:a\"");
    putPartner("zeta", "Bad Key", "\"base\":0,\"firstForm\":\"f:a\",\"title\":{\"eng\":\"x\"}");
    putPartner("zeta", "odd",
               "\"base\":1,\"firstForm\":\"f:b\",\"nope\":1,\"equip\":{\"like\":9,\"allow\":[\"dw3:item/9999\"],\"x\":1},"
               "\"equipSet\":{\"items\":[1,2,3,\"nope:x\"],\"bonuses\":[0,0,0,0,0,0]},\"evolution\":{\"stat\":\"nope\"},\"title\":{\"eng\":\"Odd\"}");
    putPartner("alpha", "ant", "\"name\":\"ant\",\"base\":0,\"firstForm\":\"dw3:digimon/383\",\"title\":{\"eng\":\"Ant\"},\"notes\":\"free text\"");
    putPartner("plain", "p", "\"base\":0,\"firstForm\":\"f:a\",\"title\":{\"eng\":\"P\"}"); /* kind "assets": not read */
    /* the header: a "name" that is not the folder's, another format, a file that is not JSON, art without a record, and the registry of before A8.2 */
    putPartner("zeta", "renamed", "\"name\":\"other\",\"base\":0,\"firstForm\":\"f:a\",\"title\":{\"eng\":\"x\"}");
    putFile("mods/zeta/partners/oldfmt/partner.json",
            "{\"format\":\"partners-v1\",\"base\":0,\"firstForm\":\"f:a\",\"title\":{\"eng\":\"x\"}}");
    putFile("mods/zeta/partners/broken/partner.json", "{not json");
    putFile("mods/zeta/partners/artonly/portrait.png", "art");
    putFile("mods/zeta/partners/stray.txt", "x");
    putFile("mods/zeta/content/partners.json", /* the registry of before A8.2: never read */
            "{\"format\":\"partners-v1\",\"new\":{\"old\":{\"base\":0,\"firstForm\":\"f:a\",\"name\":{\"eng\":\"Old\"}}}}");

    /* alphabetical by "<mod>:<key>" over the valid entries of content mods only: alpha:ant, zeta:bee, zeta:odd */
    logBegin();
    n = load();
    log = logEnd();
    CHECK(n == 3 && partnerExtCount() == 3 && PC_PARTNER_COUNT == 11);
    CHECK(pcPartnerFind("alpha:ant") == 8 && pcPartnerFind("zeta:bee") == 9 && pcPartnerFind("zeta:odd") == 10);
    CHECK(pcPartnerFind("zeta:no-form") == -1 && pcPartnerFind("zeta:bad-form") == -1 && pcPartnerFind("zeta:bad-base") == -1 &&
          pcPartnerFind("zeta:no-name") == -1 && pcPartnerFind("plain:p") == -1 && pcPartnerFind("zeta:Bad Key") == -1);
    CHECK(pcPartnerFind("dw3:partner/3") == 3 && !strcmp(pcPartnerName(9), "zeta:bee"));
    /* the first form is the row of the form (ref - 1), the art stand-in the base */
    CHECK(PC_PARTNER_ROW(8) == 383 && PC_PARTNER_ROW(9) == 56 && PC_PARTNER_ROW(10) == 57);
    CHECK(pcPartnerIconBase(8) == 0 && pcPartnerIconBase(9) == 3 && pcPartnerIconBase(10) == 1 && pcPartnerArt(9) == 3);
    /* equipment: like 5, m:item (500) allowed, item 10 denied wins over allowed, 11 denied */
    CHECK(pcPartnerCanEquip(9, 12, 1u << 5) == 1 && pcPartnerCanEquip(9, 12, 1u << 3) == 0 && pcPartnerCanEquip(9, 12, 0xFF) == 1);
    CHECK(pcPartnerCanEquip(9, 500, 0) == 1 && pcPartnerCanEquip(9, 11, 0xFF) == 0 && pcPartnerCanEquip(9, 10, 0xFF) == 0 && pcPartnerCanEquip(9, 10, 0) == 0);
    CHECK(pcPartnerCanEquip(8, 12, 1u << 0) == 1 && pcPartnerCanEquip(8, 12, 1u << 5) == 0); /* ant is like its base 0 */
    CHECK(pcPartnerCanEquip(3, 12, 1u << 3) == 1 && pcPartnerCanEquip(3, 12, 1u << 2) == 0); /* an original: the bit of the mask only */
    CHECK(pcPartnerCanEquip(30, 12, 0xFF) == 0 && pcPartnerCanEquip(-1, 12, 0xFF) == 0); /* no such partner: none */
    /* "odd": like 9 and a bad item list are refused; its base 1 stays the stand-in and the item mask the like */
    CHECK(pcPartnerCanEquip(10, 12, 1u << 1) == 1 && pcPartnerCanEquip(10, 12, 1u << 0) == 0);
    /* equipment sets */
    CHECK(pcPartnerHasEquipSet(9) == 1 && pcPartnerHasEquipSet(8) == 0 && pcPartnerHasEquipSet(10) == 0 && pcPartnerHasEquipSet(2) == 1);
    CHECK(pcPartnerEquipSetItem(9, 0) == 1 && pcPartnerEquipSetItem(9, 3) == 4 && pcPartnerEquipSetBonus(9, 2) == -4 && pcPartnerEquipSetBonus(9, 5) == 20);
    /* the evolution list of bee: two entries survive (the one that names f:b needing f:a, and 300 by stat 5); a form nobody defines drops the rest */
    CHECK(pcEvolutionCount(9) == 2 && pcEvolutionCount(8) == 0 && pcEvolutionCount(10) == 0);
    CHECK(evoList(9)[0].digimon == 58 && evoList(9)[0].needs[0].digimon == 57 && evoList(9)[0].needs[0].level == 20 &&
          evoList(9)[0].needs[1].digimon == 0 && evoList(9)[0].stat == 7 && evoList(9)[0].value == 30);
    CHECK(evoList(9)[1].digimon == 301 && evoList(9)[1].stat == 5 && evoList(9)[1].value == 99 && evoList(9)[1].needs[0].digimon == 0);

    /* the log has one line per rejection */
    CHECK(strstr(log, "partner \"zeta:no-form\" rejected: it needs a \"firstForm\""));
    CHECK(strstr(log, "partner \"zeta:bad-form\" field \"firstForm\" rejected") && strstr(log, "partner \"zeta:bad-form\" rejected: it needs a \"firstForm\""));
    CHECK(strstr(log, "partner \"zeta:bad-base\" rejected: \"base\" must be an original partner id (0..7)"));
    CHECK(strstr(log, "partner \"zeta:no-name\" rejected: it needs a title in \"eng\""));
    CHECK(strstr(log, "partners/renamed/partner.json rejected: \"name\" must be \"renamed\", the folder's name"));
    CHECK(strstr(log, "partners/oldfmt/partner.json rejected: \"format\" must be \"partners-partner-v1\""));
    CHECK(strstr(log, "partners/broken/partner.json rejected: not valid JSON"));
    CHECK(pcPartnerFind("zeta:renamed") == -1 && pcPartnerFind("zeta:oldfmt") == -1 && pcPartnerFind("zeta:broken") == -1 && pcPartnerFind("zeta:artonly") == -1);
    CHECK(pcPartnerFind("zeta:old") == -1 && !strstr(log, "artonly") && !strstr(log, "stray.txt")); /* content/partners.json is not read: no entry, no line */
    CHECK(strstr(log, "partner \"Bad Key\" rejected: the key is 1-48 characters"));
    CHECK(strstr(log, "partner \"zeta:odd\" field \"nope\" rejected: unknown field"));
    CHECK(strstr(log, "partner \"zeta:odd\" equip \"like\" rejected") && strstr(log, "partner \"zeta:odd\" equip \"allow\" entry 1 rejected") &&
          strstr(log, "partner \"zeta:odd\" equip key \"x\" rejected"));
    CHECK(strstr(log, "partner \"zeta:odd\" equipSet item 4 rejected") && !strstr(log, "equipSet item 3 rejected"));
    CHECK(strstr(log, "partner \"zeta:odd\" evolution \"stat\" rejected") && strstr(log, "partner \"zeta:bee\" evolution \"nobody:there\" dropped: not a partner form"));
    CHECK(strstr(log, "partner \"zeta:bee\" evolution \"f:b\" dropped: its need \"nobody:there\" is not a partner form"));
    CHECK(strstr(log, "partner alpha:ant = id 8 (based on 0)") && strstr(log, "partner zeta:bee = id 9 (based on 3)"));
    free(log);

    /* the same twice, and after a reset only the eight remain */
    CHECK(load() == 3 && pcPartnerFind("zeta:bee") == 9 && pcEvolutionCount(9) == 2 && PC_PARTNER_COUNT == 11);
    partnerExtReset();
    CHECK(PC_PARTNER_COUNT == 8 && pcPartnerFind("zeta:bee") == -1 && pcEvolutionCount(9) == 0 && pcPartnerCanEquip(9, 12, 0xFF) == 0);
}

static void testTexts(void)
{
    const ContentType *t = partnerExtType();

    CHECK(load() == 3);
    /* partners 8, 9, 10 take entries 31.. : name and description each; the text hook counts two per partner */
    CHECK(t->count() == 6);
    CHECK(pcPartnerTextEntry(8, 0) == 31 && pcPartnerTextEntry(8, 1) == 32 && pcPartnerTextEntry(9, 0) == 33 && pcPartnerTextEntry(10, 1) == 36);
    CHECK(!strcmp(t->text(0, "eng", 0), "Ant") && t->text(1, "eng", 0)[0] == '\0');
    CHECK(!strcmp(t->text(2, "es-419", 0), "Abeja") && !strcmp(t->text(2, "eng", 0), "Bee") && !strcmp(t->text(2, "fra", 0), "Bee"));
    CHECK(!strcmp(t->text(3, "eng", 0), "Buzzes.\nStings.") && !strcmp(t->text(3, "es-419", 0), "Zumba.") && !strcmp(t->text(3, "spa", 0), "Buzzes.\nStings."));
    CHECK(t->text(6, "eng", 0)[0] == '\0' && t->text(-1, "eng", 0)[0] == '\0');
    partnerExtReset();
}

static void testTeams(void)
{
    char *log;
    const int *party;
    int n;

    putFile("mods/zeta/content/teams.json",
            "{\"format\":\"teams-v1\",\"new\":{"
            "\"duo\":{\"partners\":[\"dw3:partner/0\",\"zeta:bee\",\"dw3:partner/7\"],\"name\":{\"eng\":\"River\",\"es-419\":\"Rio\"},\"description\":{\"eng\":\"Wet.\"}},"
            "\"tweak\":{\"base\":\"dw3:team/1\",\"name\":{\"eng\":\"Tweak\"}},"
            "\"dup\":{\"partners\":[\"dw3:partner/0\",\"dw3:partner/0\",\"dw3:partner/1\"],\"name\":{\"eng\":\"Dup\"}},"
            "\"ghost\":{\"partners\":[\"dw3:partner/0\",\"zeta:nope\",\"dw3:partner/1\"],\"name\":{\"eng\":\"Ghost\"}},"
            "\"two\":{\"partners\":[\"dw3:partner/0\",\"dw3:partner/1\"],\"name\":{\"eng\":\"Two\"}},"
            "\"empty\":{\"name\":{\"eng\":\"Empty\"}},"
            "\"bad-base\":{\"base\":\"dw3:team/9\",\"name\":{\"eng\":\"B\"}},"
            "\"over\":{\"base\":\"dw3:team/0\",\"partners\":[\"dw3:partner/2\",\"alpha:ant\",\"dw3:partner/4\"],\"name\":{\"eng\":\"Over\"}}}}");
    logBegin();
    n = load();
    log = logEnd();
    /* the originals first, then the valid new teams by sorted name: duo, over, tweak */
    CHECK(n == 3 && teamExtCount() == 3 && pcStarterPartyCount() == 6);
    party = pcStarterParty(0);
    CHECK(party[0] == 0 && party[1] == 6 && party[2] == 7);
    party = pcStarterParty(2);
    CHECK(party[0] == 1 && party[1] == 5 && party[2] == 7);
    party = pcStarterParty(3); /* zeta:duo */
    CHECK(party[0] == 0 && party[1] == 9 && party[2] == 7);
    party = pcStarterParty(4); /* zeta:over: its own partners beat the base's */
    CHECK(party[0] == 2 && party[1] == 8 && party[2] == 4);
    party = pcStarterParty(5); /* zeta:tweak: the members of team 1 */
    CHECK(party[0] == 2 && party[1] == 3 && party[2] == 6);
    party = pcStarterParty(6); /* a team that does not exist reads as no partners */
    CHECK(party[0] == -1 && party[1] == -1 && party[2] == -1 && pcStarterParty(-1)[0] == -1);
    CHECK(strstr(log, "team \"zeta:dup\" field \"partners\" rejected: a partner twice"));
    CHECK(strstr(log, "team \"zeta:ghost\" field \"partners\" rejected: entry 2 is not a partner"));
    CHECK(strstr(log, "team \"zeta:two\" field \"partners\" rejected: exactly three partners"));
    CHECK(strstr(log, "team \"zeta:empty\" rejected: it needs three \"partners\""));
    CHECK(strstr(log, "team \"zeta:bad-base\" rejected: \"base\" must be an original starter team id (0..2)"));
    CHECK(strstr(log, "team zeta:duo = id 3") && strstr(log, "team zeta:over = id 4") && strstr(log, "team zeta:tweak = id 5"));
    free(log);
    /* the texts of the teams follow the partners' (3 partners: 6 entries): 31 + 6 + 2j */
    CHECK(pcStarterTextEntry(3, 0) == 37 && pcStarterTextEntry(3, 1) == 38 && pcStarterTextEntry(5, 0) == 41);
    /* after them each team's label (P5: the page's left column shows letters; the originals' A, B, C are the disc's entries 7..9): D, E, F */
    CHECK(pcStarterLabelEntry(3) == 43 && pcStarterLabelEntry(5) == 45 && PC_STARTER_LABEL_ENTRY(1) == 8 && PC_STARTER_LABEL_ENTRY(4) == 44);
    CHECK(!strcmp(teamExtType()->text(12, "eng", 0), "D") && !strcmp(teamExtType()->text(14, "es-419", 0), "F") && !strcmp(teamExtType()->text(15, "eng", 0), ""));
    CHECK(!strcmp(teamExtType()->text(6, "es-419", 0), "Rio") && !strcmp(teamExtType()->text(7, "eng", 0), "Wet.") && !strcmp(teamExtType()->text(8, "eng", 0), "Over"));
    partnerExtReset();
    CHECK(pcStarterPartyCount() == 3 && pcStarterParty(3)[0] == -1);
}

/* readable-assets A4a: a partner's own art is its bundle partners/<key>/ through the asset layers; a missing file falls back to the base's; the removed
   fields art/icon/sprite do nothing */
static char gArtFiles[8][400], gIconFiles[8][400];
static int stubArt(int partner, const char *file, const char *name)
{
    (void)name;
    snprintf(gArtFiles[partner - 8], sizeof gArtFiles[0], "%s", file);
    return 0;
}
static int stubIcon(int partner, const char *file, const char *name)
{
    (void)name;
    snprintf(gIconFiles[partner - 8], sizeof gIconFiles[0], "%s", file);
    return 0;
}

static int stubOriginalKey(const char *key)
{
    return !strcmp(key, "agumon") || !strcmp(key, "gabumon");
}

static void testBundle(void)
{
    char root[700], *log;
    PartnerExtHooks h = {stubForm, stubItem, stubArt, NULL, stubIcon, NULL, stubOriginalKey};
    int n;

    {
        char base[600];

        snprintf(base, sizeof base, "%s", gRoot);
        snprintf(gRoot, sizeof gRoot, "%s_bundle", base);
    }
    modJson("one", "content");
    modJson("two", "assets");
    putPartner("one", "full", "\"base\":0,\"firstForm\":\"f:a\",\"title\":{\"eng\":\"Full\"}");
    putPartner("one", "bare", "\"base\":1,\"firstForm\":\"f:b\",\"title\":{\"eng\":\"Bare\"}");
    putPartner("one", "old", "\"base\":2,\"firstForm\":\"f:a\",\"title\":{\"eng\":\"Old\"},\"art\":\"art/old.png\",\"icon\":\"art/i.png\",\"sprite\":\"walker\"");
    /* a folder named like an original partner is that partner's art folder: its partner.json is never read, and says nothing */
    putPartner("one", "agumon", "\"base\":0,\"firstForm\":\"f:a\",\"title\":{\"eng\":\"Not new\"}");
    putFile("mods/one/partners/gabumon/icon.png", "gabumon-icon");
    putFile("mods/one/partners/full/portrait.png", "one-portrait");
    putFile("mods/one/partners/full/icon.png", "one-icon");
    putFile("mods/one/partners/full/field/sprite.json", "{}");
    putFile("mods/one/partners/bare/icon.png", "bare-icon"); /* an icon only: no portrait, no sprite */
    putFile("mods/one/art/old.png", "not used"); /* the removed field names a file that exists */
    putFile("mods/two/partners/full/portrait.png", "two-portrait"); /* a later mod replaces that one file */
    snprintf(root, sizeof root, "%s/mods", gRoot);
    modsInit(root, NULL);
    partnerExtSetHooks(&h);
    memset(gArtFiles, 0, sizeof gArtFiles);
    memset(gIconFiles, 0, sizeof gIconFiles);
    logBegin();
    n = partnerExtLoad();
    log = logEnd();
    /* alphabetical: one:bare 8, one:full 9, one:old 10 */
    CHECK(pcPartnerFind("one:agumon") == -1 && !strstr(log, "agumon"));
    CHECK(n == 3 && pcPartnerFind("one:bare") == 8 && pcPartnerFind("one:full") == 9 && pcPartnerFind("one:old") == 10);
    CHECK(strstr(gArtFiles[1], "mods/two/partners/full/portrait.png") != NULL);  /* the last mod wins, file by file */
    CHECK(strstr(gIconFiles[1], "mods/one/partners/full/icon.png") != NULL);     /* the file two does not have comes from one */
    CHECK(gArtFiles[0][0] == '\0' && strstr(gIconFiles[0], "mods/one/partners/bare/icon.png") != NULL); /* no portrait: the base's stands in */
    CHECK(gArtFiles[2][0] == '\0' && gIconFiles[2][0] == '\0');                  /* art/icon/sprite fields are gone: no bundle, no art */
    CHECK(pcPartnerSpriteName(9) != NULL && !strcmp(pcPartnerSpriteName(9), "partner:full"));
    CHECK(pcPartnerSpriteName(8) == NULL && pcPartnerSpriteName(10) == NULL);
    CHECK(strstr(log, "partner \"one:old\" field \"art\" rejected: unknown field") != NULL); /* the removed fields are reported as unknown */
    free(log);
    partnerExtReset();
}

int main(void)
{
    char tmp[] = "/tmp/partnerexttestXXXXXX";

    if (mkdtemp(tmp) == NULL) {
        fprintf(stdout, "partnerexttest: no temp folder\n");
        return 1;
    }
    snprintf(gRoot, sizeof gRoot, "%s", tmp);
    setenv("DW3_LOG", "mods", 1);
    testPartners();
    testTexts();
    testTeams();
    testBundle();
    {
        char cmd[700];

        snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s_bundle' /tmp/partnerexttest.log", tmp, tmp);
        if (system(cmd)) {}
    }
    if (failures) {
        fprintf(stdout, "partnerexttest: %d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stdout, "partnerexttest: OK\n");
    return 0;
}
