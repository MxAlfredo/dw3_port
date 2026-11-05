#define _GNU_SOURCE 1
/*
 * Host test of the new field encounters of the mods (pc/shim/field_ext.c, new-stages T5b): the rows of tables/field_encounters.json keyed
 * "<mod>:<name>", their numbers after the originals, the rewards each carries, every refusal with its log line, mod order and reset.
 * No game, no disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/field_ext.h"
#include "shim/mods.h"

static int failures;

static int same(const char *a, const char *b) { return a != NULL && b != NULL && !strcmp(a, b); }

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "fieldexttest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gRoot[600];
static int gSavedErr;
static char gLogPath[700];

static void logBegin(void) {
    int fd;
    fflush(stderr);
    gSavedErr = dup(2);
    fd = open(gLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

static const char *logEnd(void) {
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

static void mod(const char *name, const char *rows) {
    char rel[200], body[8192];

    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    snprintf(body, sizeof body, "{\"format\": \"mod-v1\", \"name\": \"%s\", \"version\": \"1.0\", \"kind\": \"content\"}", name);
    putFile(rel, body);
    snprintf(rel, sizeof rel, "mods/%s/tables/field_encounters.json", name);
    snprintf(body, sizeof body, "{\"format\": \"field_encounters-v1\", \"rows\": {%s}}", rows);
    putFile(rel, body);
}

/* the enemies the fake game knows: 1..600 originals ("dw3:digimon/<id>"), "fe1:slime" is 900, "fe1:ghost" is a partner form (901) */
static int enemyByName(const char *name) {
    if (!strcmp(name, "fe1:slime")) return 900;
    if (!strcmp(name, "fe1:ghost")) return -1; /* a partner form: the host refuses it by name too */
    if (!strncmp(name, "dw3:digimon/", 12)) {
        int id = atoi(name + 12);
        return id >= 1 && id <= 600 ? id : -1;
    }
    return -1;
}

static int enemyIdOk(int id) {
    return id == 0 || (id >= 1 && id <= 600) || id == 900;
}

static const FeHost HOST = {335, enemyByName, enemyIdOk};

#define GOOD_ONE \
    "\"fe1:meadow\": {\"enemies\": [{\"enemy\": 76, \"level\": 10, \"hp\": 300, \"mp\": 40, \"strength\": 30}," \
    " {\"enemy\": \"fe1:slime\", \"level\": 9, \"hp\": 250, \"mp\": 0, \"strength\": 25}]," \
    " \"ambushChance\": 12, \"kind\": 4, \"runAway\": 1, \"poison\": 1," \
    " \"rewards\": {\"money\": 120, \"exp\": 45, \"digimonExp\": 30}}"

static void testRows(void) {
    const FeRow *r;
    const char *log;

    mod("fe1", GOOD_ONE ", \"fe1:cave\": {\"enemies\": [{\"enemy\": \"dw3:digimon/12\", \"level\": 3, \"hp\": 50, \"mp\": 1, \"strength\": 5}]},"
               " \"7\": {\"kind\": 2}");
    mod("fe2", "\"fe2:river\": {\"enemies\": [{\"enemy\": 5, \"level\": 99, \"hp\": 32767, \"mp\": 32767, \"strength\": 32767}],"
               " \"rewards\": {\"money\": 9999999, \"exp\": 999999, \"digimonExp\": 9999999}}");
    {
        char root[700];

        snprintf(root, sizeof root, "%s/mods", gRoot);
        CHECK(modsInit(root, "fe1, fe2") == 2);
    }
    logBegin();
    CHECK(fieldExtLoad(&HOST) == 3);
    log = logEnd();
    CHECK(strstr(log, "rejected") == NULL); /* the numeric row "7" belongs to the table engine: no line from here */
    CHECK(fieldExtCount() == 3 && fieldExtFirstRow() == 335);
    CHECK(fieldExtNumberOf("fe1:meadow") == 335 && fieldExtNumberOf("fe1:cave") == 336 && fieldExtNumberOf("fe2:river") == 337);
    CHECK(fieldExtNumberOf("fe1:nope") == -1 && fieldExtNumberOf("7") == -1 && fieldExtNumberOf(NULL) == -1);
    CHECK(same(fieldExtNameOf(336), "fe1:cave") && fieldExtNameOf(334) == NULL && fieldExtNameOf(338) == NULL);
    r = fieldExtRow(0);
    CHECK(r != NULL && same(r->name, "fe1:meadow"));
    if (r != NULL) {
        CHECK(r->enemy[0].fighter == 76 && r->enemy[0].level == 10 && r->enemy[0].hp == 300 && r->enemy[0].mp == 40 && r->enemy[0].strength == 30);
        CHECK(r->enemy[1].fighter == 900 && r->enemy[1].level == 9 && r->enemy[2].fighter == 0);
        CHECK(r->ambushChance == 12 && r->kind == 4 && r->blocks[0] == 1 && r->blocks[11] == 1 && r->blocks[1] == 0);
        CHECK(r->money == 120 && r->exp == 45 && r->digimonExp == 30);
    }
    r = fieldExtRow(1);
    CHECK(r != NULL && r->enemy[0].fighter == 12 && r->kind == 1 && r->ambushChance == 0 && r->money == 0 && r->exp == 0);
    r = fieldExtRow(2);
    CHECK(r != NULL && r->enemy[0].level == 99 && r->money == 9999999 && r->exp == 999999 && r->digimonExp == 9999999);
    CHECK(fieldExtRow(3) == NULL && fieldExtRow(-1) == NULL);
    CHECK(same(fieldExtBlockName(0), "poison") && same(fieldExtBlockName(11), "runAway") && fieldExtBlockName(12) == NULL);
    fieldExtReset();
    CHECK(fieldExtCount() == 0 && fieldExtNumberOf("fe1:meadow") == -1);
}

static const char *refusal(const char *rows, int expectRows) {
    static char log[16384];
    char cmd[800];

    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    mod("fe1", rows);
    {
        char root[700];

        snprintf(root, sizeof root, "%s/mods", gRoot);
        CHECK(modsInit(root, "fe1") == 1);
    }
    logBegin();
    CHECK(fieldExtLoad(&HOST) == expectRows);
    snprintf(log, sizeof log, "%s", logEnd());
    fieldExtReset();
    return log;
}

#define E1 "{\"enemy\": 76, \"level\": 10, \"hp\": 300, \"mp\": 40, \"strength\": 30}"

static void testRefusals(void) {
    CHECK(strstr(refusal("\"other:a\": {\"enemies\": [" E1 "]}", 0), "row \"other:a\" rejected: the name must be the mod's own folder"));
    CHECK(strstr(refusal("\"fe1:Bad Name\": {\"enemies\": [" E1 "]}", 0), "row \"fe1:Bad Name\" rejected"));
    CHECK(strstr(refusal("\"fe1:a\": {}", 0), "needs \"enemies\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": []}", 0), "1 to 3"));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [" E1 "," E1 "," E1 "," E1 "]}", 0), "1 to 3"));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [{\"enemy\": 0, \"level\": 1, \"hp\": 1, \"mp\": 0, \"strength\": 1}]}", 0), "first enemy"));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [{\"enemy\": 700, \"level\": 1, \"hp\": 1, \"mp\": 0, \"strength\": 1}]}", 0), "not an enemy"));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [{\"enemy\": \"fe1:ghost\", \"level\": 1, \"hp\": 1, \"mp\": 0, \"strength\": 1}]}", 0), "not an enemy"));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [{\"enemy\": 76, \"level\": 100, \"hp\": 1, \"mp\": 0, \"strength\": 1}]}", 0), "\"level\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [{\"enemy\": 76, \"level\": 5, \"hp\": 0, \"mp\": 0, \"strength\": 1}]}", 0), "\"hp\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [{\"enemy\": 76, \"level\": 5, \"hp\": 9, \"mp\": 0}]}", 0), "needs \"enemy\", \"level\", \"hp\", \"mp\" and \"strength\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [{\"enemy\": 76, \"level\": 5, \"hp\": 9, \"mp\": 0, \"strength\": 1, \"speed\": 3}]}", 0), "unknown field \"speed\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [" E1 "], \"wobble\": 1}", 0), "unknown field \"wobble\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [" E1 "], \"poison\": 2}", 0), "\"poison\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [" E1 "], \"kind\": 256}", 0), "\"kind\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [" E1 "], \"rewards\": {\"money\": 10000000}}", 0), "\"money\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [" E1 "], \"rewards\": {\"gold\": 5}}", 0), "unknown field \"gold\""));
    CHECK(strstr(refusal("\"fe1:a\": {\"enemies\": [" E1 "]}, \"fe1:a\": {\"enemies\": [" E1 "]}", 1), "listed twice"));
    CHECK(strstr(refusal("\"fe1:a\": 5, \"fe1:b\": {\"enemies\": [" E1 "]}", 1), "row \"fe1:a\" rejected: not an object"));
    /* a bad row does not take a number: the next good one is the first */
    refusal("\"fe1:bad\": {}, \"fe1:good\": {\"enemies\": [" E1 "]}", 1);
    CHECK(fieldExtLoad(&HOST) == 1 && fieldExtNumberOf("fe1:good") == 335 && fieldExtNumberOf("fe1:bad") == -1); /* the mods are still the last refusal's */
    fieldExtReset();
}

static void testBadFile(void) {
    char cmd[800];
    const char *log;

    snprintf(cmd, sizeof cmd, "rm -rf '%s/mods'", gRoot);
    if (system(cmd)) {}
    putFile("mods/fe1/mod.json", "{\"format\": \"mod-v1\", \"name\": \"fe1\", \"version\": \"1.0\", \"kind\": \"content\"}");
    putFile("mods/fe1/tables/field_encounters.json", "{\"format\": \"field_encounters-v1\", \"rows\": {\"fe1:a\": ");
    {
        char root[700];

        snprintf(root, sizeof root, "%s/mods", gRoot);
        CHECK(modsInit(root, "fe1") == 1);
    }
    logBegin();
    CHECK(fieldExtLoad(&HOST) == 0);
    log = logEnd();
    CHECK(strstr(log, "fe1:a") == NULL); /* a file that is not JSON is the table engine's to report: nothing stands from here */
    fieldExtReset();
}

int main(void) {
    const char *tmp = getenv("TMPDIR");
    char cmd[800];

    snprintf(gRoot, sizeof gRoot, "%s/fieldexttest.%d", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", (int)getpid());
    snprintf(gLogPath, sizeof gLogPath, "%s/log.txt", gRoot);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", gRoot);
    if (system(cmd)) return 2;
    testRows();
    testRefusals();
    testBadFile();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
    if (system(cmd)) {}
    if (failures == 0) printf("fieldexttest: ok\n");
    else printf("fieldexttest: %d check(s) FAILED\n", failures);
    return failures != 0;
}
