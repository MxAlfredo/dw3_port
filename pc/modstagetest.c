#define _GNU_SOURCE 1
/*
 * Host test of the registry of mod stages (new-stages T4, ADR-012): pc/shim/mod_stages.c with the stage patch layer's name resolution
 * (pc/shim/stage_mods.c, stage_codes.c), the named variables (ext_state.c) and the virtual files (port_fs.c).
 *   - the registry: keys, the alphabetical session modes whatever the order they were registered in, lookups, the 256 limit, a sealed registry;
 *   - the visited flag in the named variables, saved in their EXTV chunk;
 *   - the save chunk EXTS: a mod stage is never saved as a mode (the block holds the fallback), and comes back by name or falls back;
 *   - "stage": "<mod>:<name>" in a slot and ["warp", "<mod>:<name>", x, y, dir] as an action;
 *   - a synthetic stage whose TMPK and PACK (the codecs of T2 and T3) are virtual files that read back through the port's file layer.
 * No game, no disc. Scratch folder under $TMPDIR. Exit 0: every check held.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "engine/game_state.h"
#include "shim/ext_state.h"
#include "shim/map_layers.h"
#include "shim/map_pack.h"
#include "shim/mod_stages.h"
#include "shim/mods.h"
#include "shim/port_fs.h"
#include "shim/stage_codes.h"
#include "shim/stage_mods.h"

GameState GAME;
TechData TECHS_CARD[443]; /* the game's tables, which ext_state.c reaches */
DigimonData DIGIMON_DATA_CARD[55];

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "modstagetest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char root[1024];

static void put(const char *rel, const char *text) {
    char path[1300], cmd[1400], mk[1500], *slash;
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", root, rel);
    snprintf(cmd, sizeof cmd, "%s", path);
    slash = strrchr(cmd, '/');
    *slash = '\0';
    snprintf(mk, sizeof mk, "mkdir -p '%s'", cmd);
    if (system(mk) != 0) abort();
    f = fopen(path, "wb");
    if (!f) abort();
    fputs(text, f);
    fclose(f);
}

static void mod(const char *name) {
    char rel[200], body[200];

    snprintf(rel, sizeof rel, "%s/mod.json", name);
    snprintf(body, sizeof body, "{\"format\": \"mod-v1\", \"name\": \"%s\", \"version\": \"1.0\"}", name);
    put(rel, body);
}

static int sSavedErr = -1;

static void logBegin(const char *path) {
    int fd;

    fflush(stderr);
    sSavedErr = dup(2);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

static char *logEnd(const char *path) {
    FILE *f;
    long n;
    char *b;

    fflush(stderr);
    dup2(sSavedErr, 2);
    close(sSavedErr);
    f = fopen(path, "rb");
    if (!f) return strdup("");
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)calloc((size_t)n + 1, 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) b[0] = '\0';
    fclose(f);
    return b;
}

static MsStage *reg(const char *key) {
    char why[200];
    MsStage *s = msRegister(key, why, sizeof why);

    if (!s) fprintf(stdout, "modstagetest: register %s: %s\n", key, why);
    return s;
}

/* ---- the registry ---- */

static void testRegistry(void) {
    char why[200] = "";
    char key[64];
    int i;

    msReset();
    CHECK(msCount() == 0 && !msIsSealed() && msByMode(MS_MODE_FIRST) == NULL && msModeOfKey("zeta:b") == -1);
    /* registered out of order: the modes come from the keys' alphabetical order, not from the order they arrived in */
    CHECK(reg("zeta:b") != NULL && reg("alpha:z") != NULL && reg("alpha:a") != NULL && msCount() == 3);
    CHECK(msRegister("alpha:a", why, sizeof why) == NULL && strstr(why, "already") != NULL);
    CHECK(msRegister("nocolon", why, sizeof why) == NULL && strstr(why, "<mod>:<name>") != NULL);
    CHECK(msRegister("A B:c", why, sizeof why) == NULL && msRegister("a:UP", why, sizeof why) == NULL && msRegister(NULL, why, sizeof why) == NULL);
    CHECK(msByKey("alpha:a")->mode == 0 && msModeOfKey("alpha:a") == -1 && msNumberOfKey("alpha:a") == -1); /* no mode before the seal */
    CHECK(msSeal() == 3 && msIsSealed() && msSeal() == 3);
    CHECK(msByKey("alpha:a")->mode == 0x300 && msByKey("alpha:z")->mode == 0x301 && msByKey("zeta:b")->mode == 0x302);
    CHECK(msModeOfKey("alpha:z") == 0x301 && msModeOfKey("nope:x") == -1);
    CHECK(msByMode(0x300) == msByKey("alpha:a") && msByMode(0x302) == msByKey("zeta:b") && msByMode(0x303) == NULL && msByMode(0x2FF) == NULL);
    CHECK(msAt(0) == msByKey("alpha:a") && msAt(2) == msByKey("zeta:b") && msAt(3) == NULL && msAt(-1) == NULL);
    CHECK(msRegister("beta:late", why, sizeof why) == NULL && strstr(why, "sealed") != NULL && msCount() == 3);

    /* modes and numbers: the range, and the number a patch or an action holds */
    CHECK(!msIsModMode(0x2FF) && msIsModMode(0x300) && msIsModMode(0x3FF) && !msIsModMode(0x400) && !msIsModMode(0x21D) && !msIsModMode(0));
    CHECK(MS_IS_FIELD_MODE(0x21D) && MS_IS_FIELD_MODE(0x2D7) && MS_IS_FIELD_MODE(0x300) && MS_IS_FIELD_MODE(0x3FF) && !MS_IS_FIELD_MODE(0x400) &&
          !MS_IS_FIELD_MODE(0x100) && !MS_IS_FIELD_MODE(0x500) && !MS_IS_FIELD_MODE(0xE00));
    CHECK(msNumberOfKey("alpha:a") == MS_NUMBER_BASE && msNumberOfKey("zeta:b") == MS_NUMBER_BASE + 2 && msNumberOfKey("nope:x") == -1);
    CHECK(msNumberOfMode(0x301) == MS_NUMBER_BASE + 1 && msNumberOfMode(0x21D) == -1 && msNumberOfMode(0x303) == -1);
    CHECK(msModeOfNumber(MS_NUMBER_BASE + 1) == 0x301 && msModeOfNumber(330) == -1 && msModeOfNumber(MS_NUMBER_BASE + 3) == -1);
    CHECK(strcmp(msKeyOfNumber(MS_NUMBER_BASE + 2), "zeta:b") == 0 && msKeyOfNumber(330) == NULL && msKeyOfNumber(MS_NUMBER_BASE + 3) == NULL);

    /* another arrival order gives the same modes */
    msReset();
    CHECK(msCount() == 0 && msByKey("alpha:a") == NULL);
    CHECK(reg("alpha:a") && reg("alpha:z") && reg("zeta:b") && msSeal() == 3);
    CHECK(msModeOfKey("alpha:a") == 0x300 && msModeOfKey("alpha:z") == 0x301 && msModeOfKey("zeta:b") == 0x302);

    /* the 256 limit (0x300-0x3FF) */
    msReset();
    for (i = 0; i < MS_MODE_COUNT; i++) {
        snprintf(key, sizeof key, "m:s%03d", i);
        if (!reg(key)) break;
    }
    CHECK(i == MS_MODE_COUNT && msCount() == MS_MODE_COUNT);
    CHECK(msRegister("m:one-more", why, sizeof why) == NULL && strstr(why, "256") != NULL);
    CHECK(msSeal() == MS_MODE_COUNT && msModeOfKey("m:s000") == 0x300 && msModeOfKey("m:s255") == 0x3FF && msByMode(0x3FF) == msByKey("m:s255"));
    CHECK(msNumberOfKey("m:s255") == MS_NUMBER_BASE + 255 && msNumberOfKey("m:s255") <= 0xFFFF);

    /* a key that is too long for the visited variable is refused */
    msReset();
    {
        char longKey[200];

        memset(longKey, 'a', sizeof longKey);
        memcpy(longKey, "m:", 2);
        longKey[MS_KEY_MAX] = '\0';
        CHECK(reg(longKey) != NULL); /* exactly the limit */
        longKey[MS_KEY_MAX] = 'a';
        longKey[MS_KEY_MAX + 1] = '\0';
        CHECK(msRegister(longKey, why, sizeof why) == NULL);
    }
    msReset();
}

/* ---- fallback ---- */

static void testFallback(void) {
    MsStage *a, *b;
    int mode, x, y, dir;

    msReset();
    a = reg("a:one");
    b = reg("a:two");
    CHECK(a && b);
    b->fallbackMode = 0x22A;
    b->fallbackX = 10;
    b->fallbackY = 20;
    b->fallbackDir = 5;
    msSeal();
    msFallbackOf(a, &mode, &x, &y, &dir); /* the default: Central Park at its own start */
    CHECK(mode == MS_DEFAULT_FALLBACK_MODE && mode == 0x21D && x == 900 && y == 1000 && dir == 0);
    msFallbackOf(b, &mode, &x, &y, &dir);
    CHECK(mode == 0x22A && x == 10 && y == 20 && dir == 5);
    msFallbackOf(NULL, &mode, &x, &y, &dir);
    CHECK(mode == 0x21D);
    msReset();
}

/* ---- the visited flag ---- */

static void testVisited(void) {
    char name[160];
    unsigned long need;
    unsigned char *chunk;

    msReset();
    pcExtVarsReset();
    CHECK(reg("training:lower") && reg("training:upper") && msSeal() == 2);
    CHECK(msVisitedName("training:upper", name, sizeof name) == 1 && strcmp(name, "training:visited.upper") == 0);
    CHECK(msVisitedName("nocolon", name, sizeof name) == 0 && msVisitedName("training:upper", name, 10) == 0);
    CHECK(!msVisited("training:upper") && pcExtVarCount() == 0);
    CHECK(msMarkVisited(0x21D) == 0 && msMarkVisited(0x303) == 0 && pcExtVarCount() == 0); /* the game's own flag and an unregistered mode: not here */
    CHECK(msMarkVisited(0x301) == 1 && msVisited("training:upper") && !msVisited("training:lower") && pcExtVarGet("training:visited.upper") == 1);
    CHECK(msMarkVisited(0x301) == 1 && pcExtVarCount() == 1); /* again: the same variable */
    need = pcExtStateEncodeVars(NULL, 0);
    CHECK(need > 0);
    chunk = (unsigned char *)malloc(need);
    pcExtStateEncodeVars(chunk, need);
    pcExtVarsReset();
    CHECK(!msVisited("training:upper"));
    CHECK(pcExtStateDecodeVars(chunk, need) == 1 && msVisited("training:upper") && pcExtVarGet("training:visited.upper") == 1);
    /* a removed mod keeps its flag: the registry forgets the stage, the variable stays in the store */
    msReset();
    CHECK(pcExtVarGet("training:visited.upper") == 1);
    free(chunk);
    pcExtVarsReset();
}

/* ---- the save chunk ---- */

static unsigned char sumOf(const unsigned char *g) {
    unsigned char x = 0;
    size_t b;

    for (b = 4; b < GAME_SAVE_SIZE; b++) x ^= g[b];
    return x;
}

static void setBlock(unsigned char *g, int mode, int x, int y, int dir) {
    int v[2];

    memset(g, 0x5A, GAME_SAVE_SIZE);
    memcpy(g + offsetof(GameState, fieldMode), &mode, 4);
    v[0] = x;
    v[1] = y;
    memcpy(g + offsetof(GameState, fieldPos), v, 8);
    memcpy(g + offsetof(GameState, fieldDir), &dir, 4);
    g[0] = sumOf(g);
}

static void getBlock(const unsigned char *g, int *mode, int *x, int *y, int *dir) {
    int v[2];

    memcpy(mode, g + offsetof(GameState, fieldMode), 4);
    memcpy(v, g + offsetof(GameState, fieldPos), 8);
    memcpy(dir, g + offsetof(GameState, fieldDir), 4);
    *x = v[0];
    *y = v[1];
}

static void testSave(const char *logPath) {
    unsigned char block[GAME_SAVE_SIZE], copy[GAME_SAVE_SIZE], loaded[GAME_SAVE_SIZE];
    unsigned char chunk[300];
    unsigned long need;
    int mode, x, y, dir;
    char *log;
    MsStage *up;

    msReset();
    up = reg("training:upper");
    CHECK(up && reg("training:lower") && msSeal() == 2);
    up->fallbackMode = 0x22A;
    up->fallbackX = 123;
    up->fallbackY = 456;
    up->fallbackDir = 3;

    /* in the stage: the chunk names it, the block gets the fallback. Asking the size never touches the block. */
    setBlock(block, 0x301, 0x3E800, 0x4B000, 6);
    memcpy(copy, block, sizeof block);
    need = msEncodeSaved(block, sizeof block, NULL, 0);
    CHECK(need > 0 && need <= sizeof chunk && memcmp(block, copy, sizeof block) == 0);
    CHECK(msEncodeSaved(block, sizeof block, chunk, need - 1) == need && memcmp(block, copy, sizeof block) == 0); /* too small: not written, not touched */
    CHECK(msEncodeSaved(block, sizeof block, chunk, need) == need);
    getBlock(block, &mode, &x, &y, &dir);
    CHECK(mode == 0x22A && x == (123 << 8) && y == (456 << 8) && dir == 3); /* never a session mode; the fallback's place, in 1/256 pixels */
    CHECK(block[0] == sumOf(block) && block[0] != copy[0]); /* the checksum of the new bytes */
    CHECK(memcmp(block + 0x44, copy + 0x44, sizeof block - 0x44) == 0 && memcmp(block + 4, copy + 4, 0x30) == 0); /* nothing else changed */
    CHECK(chunk[0] == 1 && chunk[1] == 0 && chunk[2] == 0 && chunk[3] == 0 && chunk[4] == 14 && chunk[5] == 0 && memcmp(chunk + 6, "training:upper", 14) == 0);

    /* loading it again: the mod is there, the stage comes back with the player where it was */
    memcpy(loaded, block, sizeof block);
    CHECK(msRestoreSaved(loaded, sizeof loaded, chunk, need) == 1);
    getBlock(loaded, &mode, &x, &y, &dir);
    CHECK(mode == 0x301 && x == 0x3E800 && y == 0x4B000 && dir == 6 && loaded[0] == sumOf(loaded));
    CHECK(memcmp(loaded, copy, sizeof loaded) == 0); /* the very block that was saved */

    /* the mod is gone (or another one is installed): the block keeps its fallback, and a line says so */
    msReset();
    CHECK(reg("other:thing") && msSeal() == 1);
    memcpy(loaded, block, sizeof block);
    logBegin(logPath);
    CHECK(msRestoreSaved(loaded, sizeof loaded, chunk, need) == 0);
    log = logEnd(logPath);
    CHECK(strstr(log, "training:upper") != NULL && strstr(log, "[saves]") != NULL);
    free(log);
    CHECK(memcmp(loaded, block, sizeof loaded) == 0);
    getBlock(loaded, &mode, &x, &y, &dir);
    CHECK(mode == 0x22A && !msIsModMode(mode));

    /* not in a mod stage: nothing recorded, the block untouched */
    msReset();
    CHECK(reg("training:upper") && msSeal() == 1);
    setBlock(block, 0x21D, 900 << 8, 1000 << 8, 2);
    memcpy(copy, block, sizeof block);
    CHECK(msEncodeSaved(block, sizeof block, NULL, 0) == 0 && msEncodeSaved(block, sizeof block, chunk, sizeof chunk) == 0 && memcmp(block, copy, sizeof block) == 0);
    /* a mode of the mod range with no stage behind it: nothing to name either */
    setBlock(block, 0x3F0, 1, 2, 3);
    CHECK(msEncodeSaved(block, sizeof block, NULL, 0) == 0);
    /* an old save has no chunk; a damaged one leaves the block alone */
    CHECK(msRestoreSaved(block, sizeof block, NULL, 0) == 0);
    setBlock(block, 0x22A, 1, 2, 3);
    memcpy(copy, block, sizeof block);
    CHECK(msRestoreSaved(block, sizeof block, chunk, 5) == -1 && memcmp(block, copy, sizeof block) == 0);
    chunk[0] = 9; /* another version */
    CHECK(msRestoreSaved(block, sizeof block, chunk, need) == -1 && memcmp(block, copy, sizeof block) == 0);
    chunk[0] = 1;
    chunk[4] = 200; /* a name longer than the chunk */
    CHECK(msRestoreSaved(block, sizeof block, chunk, need) == -1 && memcmp(block, copy, sizeof block) == 0);
    msReset();
}

/* ---- names in slots and in the warp action ---- */

static int sRan, sRanKind, sRanArgs[SC_ACT_ARGS];

static void fakeRunner(int kind, const int *args) {
    sRan++;
    sRanKind = kind;
    memcpy(sRanArgs, args, sizeof sRanArgs);
}

static int hostExists(int n) {
    return (n >= 330 && n <= 334) || msModeOfNumber(n) >= 0; /* the build has a few stages */
}

static int hostMode(int n) {
    return n == 330 ? 0x21D : n < MS_NUMBER_BASE ? n + 0x100 : msModeOfNumber(n);
}

static int hostByName(const char *name) {
    return msNumberOfKey(name);
}

static const char *hostName(int number) {
    return msKeyOfNumber(number);
}

static int envExists(void *ud, int n) {
    (void)ud;
    return hostExists(n);
}

static void testNames(const char *logPath) {
    StageCodeEnv base, env;
    ExtJson j;
    uint16_t *w = NULL;
    int n = 0;
    char err[300] = "", *log;
    ScBuf b;
    SmHost host;
    SdStage s;
    SmReport rep;
    SdSlot *sl;
    const char *text = "{\"l\": [[\"warp\", \"training:upper\", 100, 200, 3], [\"warp\", 330, 1, 2, 0]]}";
    const char *bad = "{\"l\": [[\"warp\", \"training:nope\", 100, 200, 3]]}";
    const char *bad2 = "{\"l\": [[\"warp\", \"nocolon\", 100, 200, 3]]}";

    msReset();
    CHECK(reg("training:lower") && reg("training:upper") && msSeal() == 2);

    /* the warp action: the name becomes the stage number the action holds, and prints back as the name */
    memset(&base, 0, sizeof base);
    base.stageExists = envExists;
    memset(&host, 0, sizeof host);
    host.stageExists = hostExists;
    host.stageMode = hostMode;
    host.stageByName = hostByName;
    host.stageName = hostName;
    mod("t4");
    put("t4/stages/330.json",
        "{\"format\": \"stage-patch-v1\", \"slots\": {\"patch\": [{\"index\": 0, \"stage\": \"training:upper\", \"x\": 700, \"y\": 800, \"dir\": 3},\n"
        "                       {\"index\": 1, \"stage\": 330, \"x\": 5, \"y\": 6, \"dir\": 1}],\n"
        "  \"add\": [{\"key\": \"door\", \"stage\": \"training:lower\", \"x\": 10, \"y\": 20, \"dir\": 2, \"area\": {\"x\": 100, \"y\": 120, \"w\": 32, \"h\": 16}}]}}\n");
    put("t4/stages/331.json", "{\"format\": \"stage-patch-v1\", \"slots\": {\"patch\": [{\"index\": 0, \"stage\": \"training:nope\"}]}}\n");
    put("t4/stages/332.json", "{\"format\": \"stage-patch-v1\", \"slots\": {\"patch\": [{\"index\": 0, \"stage\": \"nocolon\"}]}}\n");
    put("t4/stages/333.json", "{\"format\": \"stage-patch-v1\", \"slots\": {\"patch\": [{\"index\": 0, \"stage\": \"training:upper\", \"arg\": 5}]}}\n");
    CHECK(modsInit(root, "t4") == 1);
    logBegin(logPath);
    CHECK(smLoad(&host) == 1); /* only 330 stands */
    log = logEnd(logPath);
    CHECK(strstr(log, "331.json rejected: slot patch 0: there is no stage \"training:nope\"") != NULL);
    CHECK(strstr(log, "332.json rejected: slot patch 0: there is no stage \"nocolon\"") != NULL);
    CHECK(strstr(log, "333.json rejected: slot patch 0: give \"arg\" or \"stage\", not both") != NULL);
    free(log);

    memset(&s, 0, sizeof s);
    s.number = 330;
    sl = sdStageAddSlot(&s);
    sl->type = 1;
    sl = sdStageAddSlot(&s);
    sl->type = 1;
    CHECK(smApplyStage(330, &s, &rep) == 1 && s.nslots == 3);
    CHECK(s.slots[0].type == 1 && s.slots[0].arg == 0x301 && s.slots[0].x == 700 && s.slots[0].y == 800 && s.slots[0].dir == 3); /* the session mode of the stage */
    CHECK(s.slots[1].arg == 0x21D); /* a number is still a number */
    CHECK(s.slots[2].arg == 0x300 && s.slots[2].x == 10 && s.slots[2].hasArea);
    sdStageFree(&s);

    smCodeEnv(&env, &base); /* takes the names from the host smLoad kept */
    smSetActRunner(fakeRunner);
    CHECK(extJsonParse((const uint8_t *)text, strlen(text), &j) == 0);
    CHECK(scListFromJson(&env, extJsonGet(&j, "l"), &w, &n, err, sizeof err) == 0 && n == 4);
    if (n == 4) {
        sRan = 0;
        pcModApply((int)w[0], 1);
        CHECK(sRan == 1 && sRanKind == SC_ACT_WARP && sRanArgs[0] == msNumberOfKey("training:upper") && msModeOfNumber(sRanArgs[0]) == 0x301);
        CHECK(sRanArgs[1] == 100 && sRanArgs[2] == 200 && sRanArgs[3] == 3);
        pcModApply((int)w[2], 1);
        CHECK(sRan == 2 && sRanArgs[0] == 330);
        memset(&b, 0, sizeof b);
        CHECK(scListToJson(&env, w, n, &b) == 0);
        CHECK(strcmp(b.b, "[[\"warp\", \"training:upper\", 100, 200, 3], [\"warp\", 330, 1, 2, 0]]") == 0);
        scFree(&b);
    }
    free(w);
    extJsonFree(&j);
    w = NULL;
    n = 0;
    CHECK(extJsonParse((const uint8_t *)bad, strlen(bad), &j) == 0);
    CHECK(scListFromJson(&env, extJsonGet(&j, "l"), &w, &n, err, sizeof err) != 0 && strstr(err, "no such stage") != NULL);
    extJsonFree(&j);
    CHECK(extJsonParse((const uint8_t *)bad2, strlen(bad2), &j) == 0);
    CHECK(scListFromJson(&env, extJsonGet(&j, "l"), &w, &n, err, sizeof err) != 0 && strstr(err, "stage") != NULL);
    extJsonFree(&j);
    smSetActRunner(NULL);
    smUnload();
    msReset();
}

/* ---- a stage's virtual files read back like the game reads them ---- */

/* what FILE_CACHE's load does with a file (the sector count the wrapped getSectorCount answers, then CD_READER.read of all of them) */
static uint8_t *readWhole(int file, size_t *sectors) {
    size_t len = 0;
    uint8_t *buf;

    if (pfsResolve(file, &len, 1, NULL) == NULL) return NULL;
    *sectors = (len + 2047) / 2048;
    buf = (uint8_t *)calloc(*sectors, 2048);
    if (!buf || !pfsReadSectors(file, 0, (int)*sectors, buf, NULL)) {
        free(buf);
        return NULL;
    }
    return buf;
}

static void testFiles(void) {
    MsStage *st, *st2;
    static uint8_t floor[128 * 128], trig[128 * 128], areas[128 * 128], pix[64 * 64];
    uint8_t *l1, *l2, *l3, *tmpk = NULL, *pack = NULL, *back, *hdr;
    size_t t1, t2, t3, tl = 0, pl = 0, sectors = 0;
    const uint8_t *layers[3], *entry;
    size_t lens[3], elen;
    static const uint8_t sheet[40] = {1, 2, 3, 4};
    MlImage img;
    MpTile tile;
    MpPack p;
    uint8_t *tb = NULL;
    size_t tbl = 0;
    const uint8_t *blocks[1];
    size_t blens[1];
    int i, first;

    msReset();
    pfsClearVirtualFiles();
    pfsInit(NULL, 100); /* no disc: the virtual numbers start at 100 */
    st = reg("training:lower");
    st2 = reg("training:upper");
    CHECK(st && st2 && msSeal() == 2);

    /* the layers: a floor with a mark, a trigger and an area; the codec of T2 */
    memset(floor, 1, sizeof floor);
    floor[5 * 128 + 7] = 0;
    memset(trig, 0, sizeof trig);
    trig[9 * 128 + 9] = (uint8_t)(2 << 5 | 1);
    memset(areas, 0, sizeof areas);
    areas[20 * 128 + 30] = 1;
    CHECK(mlEncode(floor, 128, 128, &l1, &t1) == ML_OK && mlEncode(trig, 128, 128, &l2, &t2) == ML_OK && mlEncode(areas, 128, 128, &l3, &t3) == ML_OK);
    layers[0] = l1;
    layers[1] = l2;
    layers[2] = l3;
    lens[0] = t1;
    lens[1] = t2;
    lens[2] = t3;
    CHECK(mlArchiveBuild(sheet, sizeof sheet, layers, lens, 3, &tmpk, &tl) == ML_OK);
    /* the background: one 128 x 128 tile (the codec of T3) */
    for (i = 0; i < 64 * 64; i++) pix[i] = (uint8_t)i;
    memset(&tile, 0, sizeof tile);
    mpTileSimple(&tile, 64, 64, pix);
    CHECK(mpTileEncode(&tile, &tb, &tbl) == MP_OK);
    blocks[0] = tb;
    blens[0] = tbl;
    CHECK(mpPackBuild(1, 1, 0, blocks, blens, &pack, &pl) == MP_OK);

    CHECK(msSetFiles(st, tmpk, tl, pack, pl) == 0 && msSetFiles(st2, tmpk, tl, pack, pl) == 0);
    first = pfsVirtualBase();
    CHECK(first == 100 && st->layerFile == 100 && st->packFile == 101 && st2->layerFile == 102 && st2->packFile == 103); /* mode order, TMPK then PACK */
    CHECK(pfsIsVirtual(st->layerFile) && pfsIsVirtual(st->packFile) && pfsVirtualCount() == 4);
    CHECK(msLayerRef(st, 1) == (100 << 16 | 1) && msLayerRef(st2, 0) == (102 << 16));

    /* FILE_CACHE.load: the whole TMPK; the game's getEntry splits it by its offsets (here mlArchiveEntry) */
    back = readWhole(st->layerFile, &sectors);
    CHECK(back != NULL && sectors == (tl + 2047) / 2048 && memcmp(back, tmpk, tl) == 0);
    CHECK(mlArchiveCount(back, tl) == 4);
    CHECK(mlArchiveEntry(back, tl, 0, &entry, &elen) == ML_OK && memcmp(entry, sheet, 4) == 0); /* the object sheet: SheetEntry's entry 0 */
    CHECK(mlArchiveEntry(back, tl, 1, &entry, &elen) == ML_OK && mlDecode(entry, elen, &img) == ML_OK && img.w == 128 && img.v[5 * 128 + 7] == 0 && img.v[0] == 1);
    mlImageFree(&img);
    CHECK(mlArchiveEntry(back, tl, 2, &entry, &elen) == ML_OK && mlDecode(entry, elen, &img) == ML_OK && img.v[9 * 128 + 9] == (uint8_t)(2 << 5 | 1));
    mlImageFree(&img);
    CHECK(mlArchiveEntry(back, tl, 3, &entry, &elen) == ML_OK && mlDecode(entry, elen, &img) == ML_OK && img.v[20 * 128 + 30] == 1);
    mlImageFree(&img);
    free(back);

    /* the map streamer: CD_READER.read(file, 0, 1) for the header, then the tiles by sector */
    hdr = (uint8_t *)calloc(1, 2048);
    CHECK(pfsReadSectors(st->packFile, 0, 1, hdr, NULL) == 1 && memcmp(hdr, pack, 2048) == 0);
    CHECK(hdr[0] != 0xFF); /* the header sector is the pack's own, byte for byte */
    free(hdr);
    back = readWhole(st->packFile, &sectors);
    CHECK(back != NULL && sectors == pl / 2048 && memcmp(back, pack, pl) == 0);
    CHECK(mpPackParse(back, pl, &p) == MP_OK && p.width == 1 && p.height == 1 && p.sizes[0] == tbl);
    {
        /* a tile is read by its sector, as the streamer's streams do (frame * frameSectors + 1) */
        uint8_t *tileBuf = (uint8_t *)calloc(1, p.tileBytes);
        MpTile d;

        CHECK(pfsReadSectors(st->packFile, 1, p.tileBytes / 2048, tileBuf, NULL) == 1);
        CHECK(mpTileDecode(tileBuf, p.sizes[0], &d) == MP_OK && d.w == 64 && d.h == 64 && d.pix[5] == 5);
        mpTileFree(&d);
        free(tileBuf);
    }
    free(back);

    /* a failed add keeps nothing */
    CHECK(msSetFiles(st, NULL, 0, pack, pl) == -1 && msSetFiles(st, tmpk, tl, NULL, 0) == -1);
    CHECK(pfsVirtualCount() == 4); /* the stage's two files and the other's: a call that fails adds nothing */
    free(l1);
    free(l2);
    free(l3);
    free(tmpk);
    free(pack);
    free(tb);
    pfsClearVirtualFiles();
    msReset();
}

int main(void) {
    char tmpl[] = "/tmp/modstagetest.XXXXXX";
    char *base = getenv("TMPDIR");
    char *dir;
    char logPath[1300];

    if (base && *base) {
        snprintf(root, sizeof root, "%s/modstagetest.XXXXXX", base);
        dir = mkdtemp(root);
    } else {
        dir = mkdtemp(tmpl);
        if (dir) snprintf(root, sizeof root, "%s", dir);
    }
    if (!dir) {
        fprintf(stdout, "modstagetest: no scratch folder\n");
        return 2;
    }
    snprintf(logPath, sizeof logPath, "%s/.log", root);
    testRegistry();
    testFallback();
    testVisited();
    testSave(logPath);
    testNames(logPath);
    testFiles();
    {
        char cmd[1100];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        if (system(cmd) != 0) fprintf(stdout, "modstagetest: could not remove %s\n", root);
    }
    fprintf(stdout, failures ? "modstagetest: %d check(s) FAILED\n" : "modstagetest: ok\n", failures);
    return failures ? 1 : 0;
}
