#define _GNU_SOURCE 1
/*
 * Host test of the dw3save-v1 container (pc/shim/save_file.c) and of the extension chunk
 * (pc/shim/ext_state.c), feature extended-state T2: round trip, CRC and torn-write rejection with the
 * fallback to the previous file, a newer format refused, the extension by name (a name no mod defines
 * is dropped, an id without a name is not saved). No disc, no window; files under $TMPDIR.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "engine/game_state.h"
#include "shim/ext_state.h"
#include "shim/save_file.h"
#include "shim/save_view.h"
#include "shim/text_codec.h"

GameState GAME;
TechData TECHS_CARD[443]; /* the game's technique table, which ext_state.c reaches */
DigimonData DIGIMON_DATA_CARD[55]; /* and the Digimon table */

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "savetest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char dir[600];

static void path(char *out, const char *name) {
    snprintf(out, 700, "%s/%s", dir, name);
}

static void flipByte(const char *p, long at) {
    FILE *f = fopen(p, "r+b");
    int c;

    if (f == NULL) return;
    fseek(f, at, SEEK_SET);
    c = fgetc(f);
    fseek(f, at, SEEK_SET);
    fputc(c ^ 0x40, f);
    fclose(f);
}

static void truncateTo(const char *p, long size) {
    char cmd[900];

    snprintf(cmd, sizeof(cmd), "truncate -s %ld '%s'", size, p);
    if (system(cmd) != 0) fprintf(stdout, "savetest: truncate failed\n");
}

static long fileSize(const char *p) {
    FILE *f = fopen(p, "rb");
    long n;

    if (f == NULL) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

/* the extension's names for the test: "test:alpha" = 500, "test:beta" = 1000 */
static const char *nameOf(int id) {
    if (id == 500) return "test:alpha";
    if (id == 1000) return "test:beta";
    return NULL;
}

static int idOf(const char *name) {
    if (strcmp(name, "test:alpha") == 0) return 500;
    if (strcmp(name, "test:beta") == 0) return 1000;
    return -1;
}

static int idOfWithoutBeta(const char *name) {
    return strcmp(name, "test:beta") == 0 ? -1 : idOf(name);
}

static const char *viewItemName(int id) {
    return id == 43 ? "Test potion" : id == 403 ? "viewmod:gadget" : NULL;
}
static const char *viewTechName(int id) { return id == 447 ? "viewmod:spark" : NULL; }
static const char *viewFormName(int id) { return id == 471 ? "viewmod:fire" : NULL; }
static const char *viewModName(int i) { return i == 0 ? "viewmod" : "other"; }
static const char *viewModVersion(int i) { return i == 0 ? "1.2" : "0"; }

/* T3b: the read-only readable view of a save */
static void testView(void) {
    static unsigned char block[0x26C4];
    static GameState g;
    GameState *src = (GameState *)block;
    SaveViewCtx ctx = {2, viewModName, viewModVersion, viewItemName, viewTechName};
    char *json, file[700], *back;
    uint8_t *name;
    char *expectName;
    FILE *f;
    long n;

    memset(block, 0, sizeof block);
    (void)g;
    memcpy(src->name, "\x2A\x2B\x2C", 4);
    src->money = 3339;
    src->playTime.hours = 3;
    src->playTime.minutes = 44;
    src->playTime.seconds = 27;
    src->progress = 17;
    src->place = 330;
    src->party[0] = 2;
    src->party[1] = 0;
    src->party[2] = 5;
    src->itemsCard[43] = 11;
    src->itemsCard[44] = 2; /* no name known: the id only */
    src->equippedItemsCard[44] = 1;
    PC_PARTNER_BLOCK(*src, 1).unlocked = 4;
    memcpy(PC_PARTNER_BLOCK(*src, 1).info.name, "\x2B\x2C", 3);
    PC_PARTNER_BLOCK(*src, 1).info.stats[STAT_LEVEL] = 9;
    PC_PARTNER_BLOCK(*src, 1).info.stats[STAT_HP] = 80;
    PC_PARTNER_BLOCK(*src, 1).info.stats[STAT_MAX_HP] = 120;
    PC_PARTNER_BLOCK(*src, 1).info.entries[2].skills[1] = (s16)(447 | SKILL_KNOWN); /* a technique of a mod */
    PC_PARTNER_BLOCK(*src, 1).info.entries[2].skills[2] = 17;                        /* an original one: not listed */
    PC_PARTNER_BLOCK(*src, 3).info.entries[0].skills[0] = 450;                       /* nobody names it: the id only */
    PC_PARTNER_BLOCK(*src, 1).info.entries[3].id = 471;                              /* a new partner form of a mod */
    PC_PARTNER_BLOCK(*src, 3).info.entries[1].id = 480;                              /* nobody names it: the id only */
    PC_PARTNER_BLOCK(*src, 3).info.entries[2].id = 5;                                /* an original form: not listed */
    pcExtStateReset();
    *pcItemCountRef(403) = 5;
    *pcItemCountRef(450) = 1; /* an extended id nobody names: listed by id */

    ctx.formName = viewFormName;
    saveViewSetContext(&ctx);
    json = saveViewBuild(block, sizeof block, 3, 42);
    CHECK(json != NULL);
    if (json != NULL) {
        name = (uint8_t *)"\x2A\x2B\x2C";
        expectName = tcDecode(name, 3);
        CHECK(strstr(json, "\"format\": \"dw3save-view-v1\"") != NULL);
        CHECK(strstr(json, "READ-ONLY") != NULL && strstr(json, "editing it changes nothing") != NULL);
        CHECK(strstr(json, "\"slot\": 3,") != NULL && strstr(json, "\"sequence\": 42,") != NULL);
        CHECK(strstr(json, "\"portVersion\": \"") != NULL);
        CHECK(strstr(json, "{ \"name\": \"viewmod\", \"version\": \"1.2\" }, { \"name\": \"other\", \"version\": \"0\" }") != NULL);
        CHECK(expectName != NULL && strstr(json, expectName) != NULL);
        CHECK(strstr(json, "\"money\": 3339,") != NULL);
        CHECK(strstr(json, "\"playTime\": { \"hours\": 3, \"minutes\": 44, \"seconds\": 27 }") != NULL);
        CHECK(strstr(json, "\"progress\": 17,") != NULL && strstr(json, "\"place\": 330,") != NULL);
        CHECK(strstr(json, "\"party\": [2, 0, 5]") != NULL);
        CHECK(strstr(json, "\"index\": 1,") != NULL && strstr(json, "\"level\": 9, \"hp\": 80, \"maxHp\": 120") != NULL);
        CHECK(strstr(json, "\"index\": 0,") == NULL); /* a locked partner is not listed */
        CHECK(strstr(json, "{ \"id\": 43, \"name\": \"Test potion\", \"count\": 11 }") != NULL);
        CHECK(strstr(json, "{ \"id\": 44, \"count\": 2, \"equipped\": 1 }") != NULL);
        CHECK(strstr(json, "{ \"id\": 403, \"name\": \"viewmod:gadget\", \"count\": 5 }") != NULL);
        CHECK(strstr(json, "{ \"id\": 450, \"count\": 1 }") != NULL);
        CHECK(strstr(json, "{ \"partner\": 1, \"entry\": 2, \"slot\": 1, \"id\": 447, \"name\": \"viewmod:spark\" }") != NULL);
        CHECK(strstr(json, "{ \"partner\": 3, \"entry\": 0, \"slot\": 0, \"id\": 450 }") != NULL);
        CHECK(strstr(json, "\"slot\": 2, \"id\": 17") == NULL);
        CHECK(strstr(json, "{ \"partner\": 1, \"entry\": 3, \"id\": 471, \"name\": \"viewmod:fire\" }") != NULL);
        CHECK(strstr(json, "{ \"partner\": 3, \"entry\": 1, \"id\": 480 }") != NULL && strstr(json, "\"entry\": 2, \"id\": 5") == NULL);
        free(expectName);

        /* atomic write: the file is there, no temporary one is left */
        path(file, "slot3.json");
        CHECK(saveViewWriteFile(file, json));
        CHECK(saveViewWriteFile(file, json)); /* over an existing one */
        snprintf(file + strlen(file), 8, ".tmp");
        CHECK(access(file, F_OK) != 0);
        path(file, "slot3.json");
        f = fopen(file, "rb");
        CHECK(f != NULL);
        if (f != NULL) {
            back = malloc(strlen(json) + 2);
            n = (long)fread(back, 1, strlen(json) + 1, f);
            fclose(f);
            CHECK(n == (long)strlen(json) && memcmp(back, json, (size_t)n) == 0);
            free(back);
        }
        free(json);
    }
    /* a partner a mod adds (Betamon pack P4) is listed too, with its stable name: its data is in the port's array, not in the block */
    {
        int idx = pcPartnerRegister("viewmod:river", 3);

        CHECK(idx == 8);
        PC_PARTNER(idx).unlocked = idx + 3;
        memcpy(PC_PARTNER(idx).info.name, "\x2B\x2C", 3);
        PC_PARTNER(idx).info.stats[STAT_LEVEL] = 4;
        PC_PARTNER(idx).info.stats[STAT_HP] = 30;
        PC_PARTNER(idx).info.stats[STAT_MAX_HP] = 31;
        PC_PARTNER(idx).info.entries[1].id = 471;
        json = saveViewBuild(block, sizeof block, 1, 7);
        CHECK(json != NULL && strstr(json, "\"index\": 8, \"id\": \"viewmod:river\", \"name\": ") != NULL);
        CHECK(json != NULL && strstr(json, "\"level\": 4, \"hp\": 30, \"maxHp\": 31") != NULL);
        CHECK(json != NULL && strstr(json, "{ \"partner\": 8, \"entry\": 1, \"id\": 471") != NULL);
        CHECK(json != NULL && strstr(json, "\"index\": 1, \"name\"") != NULL); /* an original has no \"id\" */
        free(json);
        pcPartnersUnregister();
        json = saveViewBuild(block, sizeof block, 1, 8); /* the mod is gone: nothing above the eight */
        CHECK(json != NULL && strstr(json, "\"index\": 8") == NULL);
        free(json);
    }
    /* an unwritable place only fails (the caller logs it) */
    CHECK(!saveViewWriteFile("/nonexistent-dir/slot0.json", "{}"));
    /* no context: ids only, no mods */
    saveViewSetContext(NULL);
    json = saveViewBuild(block, sizeof block, 0, 1);
    CHECK(json != NULL && strstr(json, "\"mods\": []") != NULL && strstr(json, "Test potion") == NULL &&
          strstr(json, "{ \"id\": 43, \"count\": 11 }") != NULL && strstr(json, "\"id\": 447 }") != NULL);
    free(json);
    pcExtStateReset();
}

int main(void) {
    const char *tmp = getenv("TMPDIR");
    char cmd[700], file[700];
    unsigned char game[0x26C4], info[0x100];
    SaveFile f, g;
    int i;

    snprintf(dir, sizeof(dir), "%s/savetest.%d", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", (int)getpid());
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
    if (system(cmd) != 0) return 2;
    for (i = 0; i < (int)sizeof(game); i++) game[i] = (unsigned char)(i * 7 + 1);
    for (i = 0; i < (int)sizeof(info); i++) info[i] = (unsigned char)(255 - i);

    /* CRC32 known vector */
    CHECK(saveCrc32("123456789", 9) == 0xCBF43926u);

    /* container round trip */
    memset(&f, 0, sizeof f);
    CHECK(saveFileAdd(&f, "GAME", game, sizeof game));
    CHECK(saveFileAdd(&f, "INFO", info, sizeof info));
    CHECK(saveFileAdd(&f, "NONE", NULL, 0));
    path(file, "slot0.dw3save");
    CHECK(saveFileWrite(file, &f) == SAVEFILE_OK);
    CHECK(saveFileRead(file, &g) == SAVEFILE_OK);
    CHECK(g.count == 3 && g.formatVersion == 1 && !g.usedBackup);
    CHECK(saveFileFind(&g, "GAME") != NULL && saveFileFind(&g, "GAME")->size == sizeof game);
    CHECK(memcmp(saveFileFind(&g, "GAME")->data, game, sizeof game) == 0);
    CHECK(memcmp(saveFileFind(&g, "INFO")->data, info, sizeof info) == 0);
    CHECK(saveFileFind(&g, "NONE") != NULL && saveFileFind(&g, "NONE")->size == 0);
    CHECK(saveFileFind(&g, "ABSENT") == NULL);
    saveFileFree(&g);

    /* a file with the partners section is format 2 (an earlier release refuses it); this build reads both */
    f.formatVersion = SAVEFILE_FORMAT_PARTNERS;
    CHECK(saveFileWrite(file, &f) == SAVEFILE_OK && saveFileRead(file, &g) == SAVEFILE_OK && g.formatVersion == 2 && g.count == 3);
    saveFileFree(&g);
    f.formatVersion = 0;
    CHECK(saveFileWrite(file, &f) == SAVEFILE_OK && saveFileRead(file, &g) == SAVEFILE_OK && g.formatVersion == 1);
    saveFileFree(&g);

    /* a second write keeps the first as the fallback; no temporary file is left */
    game[10] ^= 0xFF;
    memset(&f, 0, sizeof f);
    saveFileAdd(&f, "GAME", game, sizeof game);
    CHECK(saveFileWrite(file, &f) == SAVEFILE_OK);
    snprintf(cmd, sizeof(cmd), "%s.tmp", file);
    CHECK(access(cmd, F_OK) != 0);
    snprintf(cmd, sizeof(cmd), "%s.bak", file);
    CHECK(access(cmd, F_OK) == 0);

    /* torn write: the main file cut short -> the previous copy is used */
    truncateTo(file, fileSize(file) / 2);
    CHECK(saveFileRead(file, &g) == SAVEFILE_OK && g.usedBackup);
    CHECK(saveFileFind(&g, "INFO") != NULL); /* it is the first generation */
    saveFileFree(&g);

    /* a flipped byte in the main file is the same */
    game[10] ^= 0xFF;
    memset(&f, 0, sizeof f);
    saveFileAdd(&f, "GAME", game, sizeof game);
    CHECK(saveFileWrite(file, &f) == SAVEFILE_OK); /* the damaged main is not promoted to .bak */
    flipByte(file, 100);
    CHECK(saveFileRead(file, &g) == SAVEFILE_OK && g.usedBackup);
    CHECK(saveFileFind(&g, "INFO") != NULL);
    saveFileFree(&g);

    /* both damaged: an error, nothing guessed */
    snprintf(cmd, sizeof(cmd), "%s.bak", file);
    flipByte(cmd, 100);
    CHECK(saveFileRead(file, &g) == SAVEFILE_ERR_CRC);
    CHECK(g.count == 0);

    /* missing */
    path(cmd, "nothing.dw3save");
    CHECK(saveFileRead(cmd, &g) == SAVEFILE_ERR_MISSING);

    /* decode refusals */
    {
        unsigned char buf[2000];
        size_t n;

        memset(&f, 0, sizeof f);
        saveFileAdd(&f, "GAME", game, 100);
        n = saveFileEncode(&f, buf, sizeof buf);
        CHECK(saveFileDecode(buf, n, &g) == SAVEFILE_OK);
        CHECK(saveFileDecode(buf, n - 1, &g) == SAVEFILE_ERR_SHORT);
        CHECK(saveFileDecode(buf, 10, &g) == SAVEFILE_ERR_SHORT);
        buf[3] ^= 1;
        CHECK(saveFileDecode(buf, n, &g) == SAVEFILE_ERR_MAGIC);
        buf[3] ^= 1;
        buf[8] = SAVEFILE_FORMAT_VERSION + 1; /* a newer format than this build reads, CRC fixed up so only the version is wrong */
        {
            unsigned long crc = saveCrc32(buf, n - 4);

            buf[n - 4] = (unsigned char)crc;
            buf[n - 3] = (unsigned char)(crc >> 8);
            buf[n - 2] = (unsigned char)(crc >> 16);
            buf[n - 1] = (unsigned char)(crc >> 24);
        }
        CHECK(saveFileDecode(buf, n, &g) == SAVEFILE_ERR_VERSION);
    }

    /* the section table (U1): an old file reads byte-compatibly, 20 sections round trip, the table's roof is loud */
    {
        /* a file as the first builds wrote it, laid out by hand: 3 sections (META 4 bytes, GAME 3, NONE 0) */
        unsigned char old[24 + 12 + 11 + 8 + 4], *p = old;
        unsigned long crc;
        size_t total = sizeof old;
        static const unsigned char meta[4] = {1, 2, 3, 4}, gm[3] = {9, 8, 7};
        unsigned char *big = malloc(40 * 16);
        SaveFile many;
        char tag[4];
        int k;

        memset(old, 0, sizeof old);
        memcpy(p, "DW3SAVE", 7);
        p[8] = 1;   /* format 1 */
        p[12] = 1;  /* port version 1 */
        p[16] = 3;  /* 3 sections */
        p[20] = (unsigned char)total;
        p += 24;
        memcpy(p, "META", 4); p[4] = 4; memcpy(p + 8, meta, 4); p += 12;
        memcpy(p, "GAME", 4); p[4] = 3; memcpy(p + 8, gm, 3); p += 11;
        memcpy(p, "NONE", 4); p += 8;
        crc = saveCrc32(old, total - 4);
        old[total - 4] = (unsigned char)crc;
        old[total - 3] = (unsigned char)(crc >> 8);
        old[total - 2] = (unsigned char)(crc >> 16);
        old[total - 1] = (unsigned char)(crc >> 24);
        CHECK(saveFileDecode(old, total, &g) == SAVEFILE_OK && g.count == 3);
        CHECK(saveFileFind(&g, "GAME") != NULL && saveFileFind(&g, "GAME")->size == 3 && saveFileFind(&g, "GAME")->data[2] == 7);
        CHECK(saveFileFind(&g, "META") != NULL && saveFileFind(&g, "META")->size == 4 && saveFileFind(&g, "NONE")->size == 0);
        /* and the encoder still writes those same bytes */
        {
            SaveFile e;
            unsigned char out[sizeof old];
            memset(&e, 0, sizeof e);
            saveFileAdd(&e, "META", meta, 4);
            saveFileAdd(&e, "GAME", gm, 3);
            saveFileAdd(&e, "NONE", NULL, 0);
            CHECK(saveFileEncode(&e, out, sizeof out) == total && memcmp(out, old, total) == 0);
        }

        memset(&many, 0, sizeof many);
        memset(big, 7, 40 * 16);
        for (k = 0; k < 20; k++) {
            snprintf(tag, sizeof tag, "S%02d", k);
            tag[3] = 'x';
            CHECK(saveFileAdd(&many, tag, big + k, 4));
        }
        path(file, "many.dw3save");
        CHECK(saveFileWrite(file, &many) == SAVEFILE_OK);
        CHECK(saveFileRead(file, &g) == SAVEFILE_OK && g.count == 20);
        CHECK(saveFileFind(&g, "S19x") != NULL && saveFileFind(&g, "S19x")->size == 4);
        saveFileFree(&g);
        /* the roof: the add that does not fit returns 0 and says so on the log */
        for (k = 20; k < SAVEFILE_MAX_SECTIONS; k++) {
            snprintf(tag, sizeof tag, "S%02d", k);
            tag[3] = 'x';
            CHECK(saveFileAdd(&many, tag, big, 4));
        }
        fflush(stderr);
        {
            char logp[800];
            int saved = dup(2), fd;
            FILE *lf;
            char line[300] = "";

            snprintf(logp, sizeof logp, "%s/roof.log", dir);
            fd = open(logp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(fd, 2);
            close(fd);
            CHECK(saveFileAdd(&many, "OVER", big, 4) == 0);
            fflush(stderr);
            dup2(saved, 2);
            close(saved);
            lf = fopen(logp, "r");
            if (lf) {
                if (!fgets(line, sizeof line, lf)) line[0] = 0;
                fclose(lf);
            }
            CHECK(strstr(line, "[saves]") != NULL && strstr(line, "OVER") != NULL);
        }
        free(big);
    }

    /* the extension chunk: by name, round trip */
    pcExtStateReset();
    pcExtStateSetNameHooks(nameOf, idOf);
    *pcItemCountRef(500) = 7;
    *pcItemEquippedRef(500) = 1;
    *pcItemCountRef(1000) = 3;
    *pcItemCountRef(700) = 5; /* no name: not saved */
    {
        unsigned long need = pcExtStateEncode(NULL, 0);
        unsigned char *chunk = malloc(need);

        CHECK(need > 8 && pcExtStateEncode(chunk, need) == need);
        CHECK(memmem(chunk, need, "test:alpha", 10) != NULL && memmem(chunk, need, "test:beta", 9) != NULL);
        pcExtStateReset();
        CHECK(*pcItemCountRef(500) == 0);
        CHECK(pcExtStateDecode(chunk, need) == 0);
        CHECK(*pcItemCountRef(500) == 7 && *pcItemEquippedRef(500) == 1);
        CHECK(*pcItemCountRef(1000) == 3 && *pcItemEquippedRef(1000) == 0);
        CHECK(*pcItemCountRef(700) == 0);

        /* the mod behind "test:beta" is gone: it is dropped, the rest loads */
        pcExtStateSetNameHooks(nameOf, idOfWithoutBeta);
        CHECK(pcExtStateDecode(chunk, need) == 1);
        CHECK(*pcItemCountRef(500) == 7 && *pcItemCountRef(1000) == 0);

        /* a damaged chunk leaves the extension empty */
        CHECK(pcExtStateDecode(chunk, need - 3) == -1);
        CHECK(*pcItemCountRef(500) == 0);

        /* no chunk at all (a save from an original-only game): empty */
        *pcItemCountRef(500) = 9;
        CHECK(pcExtStateDecode(NULL, 0) == 0 && *pcItemCountRef(500) == 0);
        free(chunk);
    }

    testView();

    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stdout, "savetest: cleanup failed\n");
    if (failures) {
        fprintf(stdout, "savetest: %d failure(s)\n", failures);
        return 1;
    }
    printf("savetest: OK\n");
    return 0;
}
