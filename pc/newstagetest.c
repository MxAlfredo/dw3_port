#define _GNU_SOURCE 1
/*
 * Host test of the loader of the stages that mods add (new-stages T5, ADR-012): pc/shim/stage_new.c with the registry (mod_stages.c), the stage patch layer's
 * parser (stage_mods.c: the sections of a stage-new-v1 are the ones of a patch, "add" only), the codecs of T2 and T3 and the PNG reader.
 *   - a stage built from generated PNGs: registered, its TMPK (floor, triggers, areas) and PACK read back through the port's file layer and equal to the PNGs,
 *     the CLUT made from the palette, the header's values in the registry's MsStage, the sections applied to an empty stage;
 *   - the refusals, each with its reason in the log and no stage left behind: a bad file, a wrong format, a missing or odd-sized PNG, too many tiles, a floor value
 *     the game has no meaning for, a start outside the map, a fallback that is not a stage of the game, a patch in a new stage;
 *   - the checks of the map against the sections (a trigger cell naming a slot that is not there, an area cell with no battle area), done after the seal: the stage
 *     is refused whole, its mode stays taken and the others keep theirs, and a stage whose exit named it is refused with it.
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
#include "shim/png_read.h"
#include "shim/png_write.h"
#include "shim/port_fs.h"
#include "shim/stage_codes.h"
#include "shim/stage_mods.h"
#include "shim/stage_new.h"
#include "shim/text_data.h"

GameState GAME;
TechData TECHS_CARD[443]; /* the game's tables, which ext_state.c reaches */
DigimonData DIGIMON_DATA_CARD[55];

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "newstagetest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char root[1024];

static void mk(const char *rel) {
    char cmd[1500], path[1300], *slash;

    snprintf(path, sizeof path, "%s/%s", root, rel);
    slash = strrchr(path, '/');
    *slash = '\0';
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
    if (system(cmd) != 0) abort();
}

static void put(const char *rel, const char *text) {
    char path[1300];
    FILE *f;

    mk(rel);
    snprintf(path, sizeof path, "%s/%s", root, rel);
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

static void palRgb(int i, uint8_t *rgb) {
    rgb[0] = (uint8_t)(i == 1 ? 255 : (i * 7) & 255);
    rgb[1] = (uint8_t)(i == 2 ? 255 : 0);
    rgb[2] = (uint8_t)(i == 3 ? 255 : (i * 3) & 255);
}

/* the PSX 1555 colour the loader must make of palette entry i: opaque (bit 15), 5 bits a channel */
static unsigned expClut(int i) {
    uint8_t c[3];

    palRgb(i, c);
    return 0x8000u | (unsigned)(c[0] >> 3) | ((unsigned)(c[1] >> 3) << 5) | ((unsigned)(c[2] >> 3) << 10);
}

/* an indexed PNG: 8 bit, a palette of `n` colours, index 0 transparent when `clear` */
static void png(const char *rel, const uint8_t *px, int w, int h, int n, int clear) {
    uint8_t rgb[256 * 3], alpha[1] = {0};
    uint8_t *out = NULL;
    size_t len = 0;
    char path[1300];
    FILE *f;
    int i;

    for (i = 0; i < n; i++) palRgb(i, rgb + i * 3);
    if (pngWriteIndexed(px, w, h, 8, rgb, n, clear ? alpha : NULL, clear ? 1 : 0, &out, &len) != 0) abort();
    mk(rel);
    snprintf(path, sizeof path, "%s/%s", root, rel);
    f = fopen(path, "wb");
    if (!f) abort();
    fwrite(out, 1, len, f);
    fclose(f);
    free(out);
}

/* a w x h plane filled by a rule of the cell */
static uint8_t *plane(int w, int h, int (*rule)(int x, int y)) {
    uint8_t *p = (uint8_t *)malloc((size_t)w * (size_t)h);
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) p[y * w + x] = (uint8_t)rule(x, y);
    }
    return p;
}

static int ruleBack(int x, int y) { return ((x / 16) + (y / 16)) % 4; }              /* four colours, 16 px squares */
static int ruleFloor(int x, int y) { return x < 8 || y < 8 || x >= 248 || y >= 248 ? 0 : (x > 100 && x < 140 ? 2 : 1); } /* walls, a slope strip */
static int ruleTrig(int x, int y) { return x > 120 && x < 136 && y > 200 ? 32 : 0; }    /* dir 1, slot 0 */
static int ruleTrigBad(int x, int y) { return x > 120 && x < 136 && y > 200 ? 33 : 0; } /* slot 1: there is only slot 0 */
static int ruleArea(int x, int y) { return x > 20 && x < 90 && y > 20 && y < 90 ? 1 : 0; }
static int ruleAreaBad(int x, int y) { return x > 20 && x < 90 && y > 20 && y < 90 ? 2 : 0; }
static int ruleFloorBad(int x, int y) { return x < 8 ? 9 : 1; } /* 9 is no floor value */
static int ruleOne(int x, int y) { (void)x; (void)y; return 1; }
static int ruleZero(int x, int y) { (void)x; (void)y; return 0; }

typedef struct {
    int back, floor, trig, area; /* 0: the file is not written */
} Maps;

/* writes background/floor/triggers/areas of a stage folder (256 x 256: two by two tiles) */
static void maps(const char *dir, int bw, int bh, int (*floor)(int, int), int (*trig)(int, int), int (*area)(int, int)) {
    char rel[300];
    uint8_t *p;

    p = plane(bw, bh, ruleBack);
    snprintf(rel, sizeof rel, "%s/background.png", dir);
    png(rel, p, bw, bh, 4, 0);
    free(p);
    if (floor) {
        p = plane(bw, bh, floor);
        snprintf(rel, sizeof rel, "%s/floor.png", dir);
        png(rel, p, bw, bh, 24, 0);
        free(p);
    }
    if (trig) {
        p = plane(bw, bh, trig);
        snprintf(rel, sizeof rel, "%s/triggers.png", dir);
        png(rel, p, bw, bh, 64, 0);
        free(p);
    }
    if (area) {
        p = plane(bw, bh, area);
        snprintf(rel, sizeof rel, "%s/areas.png", dir);
        png(rel, p, bw, bh, 4, 0);
        free(p);
    }
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

/* what FILE_CACHE's load does with a file: the sector count the wrapped getSectorCount answers, then CD_READER.read of all of them */
static int pfsReadFile(int file, uint8_t **out, size_t *len) {
    size_t n = 0, sectors;

    *out = NULL;
    if (pfsResolve(file, &n, 1, NULL) == NULL) return 0;
    sectors = (n + 2047) / 2048;
    *out = (uint8_t *)calloc(sectors, 2048);
    if (!*out || !pfsReadSectors(file, 0, (int)sectors, *out, NULL)) {
        free(*out);
        *out = NULL;
        return 0;
    }
    *len = sectors * 2048;
    return 1;
}

/* the host the game side builds in pc/boot.c, with the game's own stages reduced to 330 */
static int hostExists(int n) { return n == 330 || msModeOfNumber(n) >= 0; }
static int hostMode(int n) { return n == 330 ? 0x21D : msModeOfNumber(n) >= 0 ? msModeOfNumber(n) : 0; }
static int hostEncounter(int row) { return row >= 1 && row < 335; }
static int hostCharacter(int c) { return c >= 0 && c < 200; }

static void hostFill(SmHost *h) {
    memset(h, 0, sizeof *h);
    h->stageExists = hostExists;
    h->stageMode = hostMode;
    h->stageByName = msNumberOfKey;
    h->stageName = msKeyOfNumber;
    h->encounterOk = hostEncounter;
    h->characterOk = hostCharacter;
    h->newCheck = nsCheck;
    h->stageRefused = nsRefuse;
}

static const char *kGood =
    "{\"format\": \"stage-new-v1\", \"about\": \"a test stage\",\n"
    " \"fallback\": {\"stage\": 330, \"x\": 900, \"y\": 1000, \"dir\": 2},\n"
    " \"start\": {\"x\": 128, \"y\": 128, \"dir\": 3}, \"textFile\": 211, \"soundBank\": 9, \"music\": {\"bank\": 9, \"track\": 0},\n"
    " \"spriteColor\": [10, 20, 30, 1],\n"
    " \"actors\": {\"add\": [{\"key\": \"guide\", \"character\": 75, \"x\": 100, \"y\": 100, \"dir\": 2}]},\n"
    " \"slots\": {\"add\": [{\"key\": \"back\", \"stage\": 330, \"x\": 900, \"y\": 1000, \"dir\": 0}]},\n"
    " \"events\": {\"add\": [{\"key\": \"hello\", \"script\": [[\"wait\", 20], [\"end\"]]}]},\n"
    " \"battles\": {\"add\": [{\"area\": 1, \"rate\": 2, \"slots\": [{\"encounter\": 34, \"stage\": 1, \"music\": 1611137024}]}]}}\n";

/* the one pass of pc/boot.c's pcInstallStageMods, without the game */
static void load(const char *order, SmHost *host) {
    hostFill(host);
    pfsInit(NULL, 100); /* no disc: the virtual numbers start at 100 */
    pfsClearVirtualFiles();
    msReset();
    nsReset();
    CHECK(modsInit(root, order) >= 0);
    nsScan(host);
    msSeal();
    smLoad(host);
    nsInstall();
}

static void testStage(const char *logPath) {
    SmHost host;
    char *log;
    MsStage *s;
    uint8_t *back;
    size_t bl;
    SdStage st;
    SmReport rep;
    int i, x, y;
    const uint8_t *e1, *e2, *e3;
    size_t l1, l2, l3;
    MlImage img;
    MpPack pk;
    MpTile tile;
    uint8_t *tileBuf;

    mod("tm");
    put("tm/stages/yard/stage.json", kGood);
    maps("tm/stages/yard", 256, 256, ruleFloor, ruleTrig, ruleArea);
    logBegin(logPath);
    load("tm", &host);
    log = logEnd(logPath);
    CHECK(strstr(log, "rejected") == NULL);
    free(log);

    CHECK(msCount() == 1 && msIsSealed());
    s = msByKey("tm:yard");
    CHECK(s != NULL && s->mode == MS_MODE_FIRST && msByMode(MS_MODE_FIRST) == s && msNumberOfKey("tm:yard") == MS_NUMBER_BASE);
    if (!s) return;
    CHECK(s->startX == 128 && s->startY == 128 && s->startDir == 3);
    CHECK(s->fallbackMode == 0x21D && s->fallbackX == 900 && s->fallbackY == 1000 && s->fallbackDir == 2);
    CHECK(s->textFile == 211 && s->soundBank == 9 && s->music == (0x60000000 | (9 << 18)));
    CHECK(s->spriteColor[0] == 10 && s->spriteColor[1] == 20 && s->spriteColor[2] == 30 && s->spriteColor[3] == 1);
    CHECK(s->layerFile > 0 && s->packFile > 0 && s->packFile != s->layerFile);
    CHECK(msLayerRef(s, 2) == ((s->layerFile << 16) | 2));

    /* the TMPK as the field's reader gets it: 4 entries (the sheet, floor, triggers, areas), each layer equal to its PNG */
    CHECK(pfsReadFile(s->layerFile, &back, &bl) == 1);
    CHECK(mlArchiveCount(back, bl) == 4);
    CHECK(mlArchiveEntry(back, bl, 1, &e1, &l1) == ML_OK && mlArchiveEntry(back, bl, 2, &e2, &l2) == ML_OK && mlArchiveEntry(back, bl, 3, &e3, &l3) == ML_OK);
    for (i = 1; i <= 3; i++) {
        static int (*const rules[4])(int, int) = {NULL, ruleFloor, ruleTrig, ruleArea};
        const uint8_t *e = i == 1 ? e1 : i == 2 ? e2 : e3;
        size_t el = i == 1 ? l1 : i == 2 ? l2 : l3;
        int bad = 0;

        CHECK(mlDecode(e, el, &img) == ML_OK && img.w == 256 && img.h == 256);
        for (y = 0; y < 256 && img.v; y++) {
            for (x = 0; x < 256; x++) bad += img.v[y * 256 + x] != (uint8_t)rules[i](x, y);
        }
        CHECK(bad == 0);
        mlImageFree(&img);
    }
    free(back);

    /* the PACK: 2 x 2 tiles of 128 px, each the background's own quarter; the CLUT is the palette in PSX 1555, opaque */
    CHECK(pfsReadFile(s->packFile, &back, &bl) == 1);
    CHECK(mpPackParse(back, bl, &pk) == MP_OK && pk.width == 2 && pk.height == 2);
    tileBuf = (uint8_t *)calloc(1, (size_t)pk.tileBytes);
    for (i = 0; i < 4; i++) {
        int bad = 0;

        CHECK(pk.sizes[i] > 0 && pfsReadSectors(s->packFile, 1 + i * (pk.tileBytes / 2048), pk.tileBytes / 2048, tileBuf, NULL) == 1);
        CHECK(mpTileDecode(tileBuf, pk.sizes[i], &tile) == MP_OK && tile.w == 128 && tile.h == 128);
        for (y = 0; y < 128 && tile.pix; y++) {
            for (x = 0; x < 128; x++) bad += tile.pix[y * 128 + x] != (uint8_t)ruleBack((i % 2) * 128 + x, (i / 2) * 128 + y);
        }
        CHECK(bad == 0);
        CHECK(tile.count[0] == 1 && tile.sp[0][0].w == 128 && tile.sp[0][0].h == 128);
        CHECK(tile.clut[1] == expClut(1) && tile.clut[2] == expClut(2) && tile.clut[3] == expClut(3) && tile.clut[0] == expClut(0));
        mpTileFree(&tile);
    }
    free(tileBuf);
    free(back);

    /* the sections, on an empty stage, as pcStagePatchHook merges them */
    memset(&st, 0, sizeof st);
    st.number = msNumberOfKey("tm:yard");
    st.battles.present = 1;
    CHECK(smHasPatches(st.number) && smPatchCount() == 1);
    CHECK(smApplyStage(st.number, &st, &rep) == 1);
    CHECK(rep.added == 1 && st.nactors == 1 && st.actors[0].character == 75 && st.actors[0].x == 100);
    CHECK(rep.slotsAdded == 1 && st.nslots == 1 && st.slots[0].type == 1 && st.slots[0].arg == 0x21D && !st.slots[0].hasArea);
    CHECK(rep.eventsAdded == 1 && st.nevents == 1 && st.events[0].id >= SM_EVENT_BASE);
    CHECK(rep.areasAdded == 1 && st.battles.nareas == 1 && st.battles.areas[0].rate == 2 && st.battles.areas[0].b[0].encounter == 34);
    sdStageFree(&st);
    msReset();
    nsReset();
    smUnload();
    pfsClearVirtualFiles();
}

/* a stage folder with `json` as stage.json and the maps of the 256 x 256 good one, unless the case changes them */
static void bad(const char *name, const char *json) {
    char rel[300];

    snprintf(rel, sizeof rel, "tm/stages/%s/stage.json", name);
    put(rel, json);
    snprintf(rel, sizeof rel, "tm/stages/%s", name);
    maps(rel, 256, 256, ruleFloor, ruleTrig, ruleArea);
}

static const char *kHead = "{\"format\": \"stage-new-v1\", \"start\": {\"x\": 128, \"y\": 128, \"dir\": 0}";

static void testRefusals(const char *logPath) {
    SmHost host;
    char *log;
    char json[2000];
    uint8_t *p;

    mod("tm");
    {
        char cmd[1500];

        snprintf(cmd, sizeof cmd, "rm -rf '%s/tm/stages'", root);
        if (system(cmd) != 0) abort();
    }
    put("tm/stages/ok/stage.json", kGood);
    maps("tm/stages/ok", 256, 256, ruleFloor, ruleTrig, ruleArea);

    put("tm/stages/notjson/stage.json", "this is not json");
    maps("tm/stages/notjson", 256, 256, ruleFloor, NULL, NULL);
    bad("wrongformat", "{\"format\": \"stage-patch-v1\"}");
    snprintf(json, sizeof json, "%s}", kHead);
    put("tm/stages/nofloor/stage.json", json);
    maps("tm/stages/nofloor", 256, 256, NULL, NULL, NULL);
    bad("nobackground", json);
    {
        char cmd[1500];

        snprintf(cmd, sizeof cmd, "rm -f '%s/tm/stages/nobackground/background.png'", root);
        if (system(cmd) != 0) abort();
    }
    bad("oddsize", json);
    maps("tm/stages/oddsize", 200, 256, ruleFloor, NULL, NULL);
    bad("sizemismatch", json);
    p = plane(128, 256, ruleFloor);
    png("tm/stages/sizemismatch/floor.png", p, 128, 256, 24, 0);
    free(p);
    bad("floorvalue", json);
    p = plane(256, 256, ruleFloorBad);
    png("tm/stages/floorvalue/floor.png", p, 256, 256, 24, 0);
    free(p);
    bad("toomany", json);
    maps("tm/stages/toomany", 128 * 16, 128 * 16, ruleOne, NULL, NULL); /* 256 tiles, one over what the streamer keeps */
    snprintf(json, sizeof json, "{\"format\": \"stage-new-v1\"}");
    bad("nostart", json);
    snprintf(json, sizeof json, "{\"format\": \"stage-new-v1\", \"start\": {\"x\": 5000, \"y\": 5, \"dir\": 0}}");
    bad("startout", json);
    snprintf(json, sizeof json, "%s, \"fallback\": {\"stage\": 777, \"x\": 1, \"y\": 1, \"dir\": 0}}", kHead);
    bad("badfallback", json);
    snprintf(json, sizeof json, "%s, \"actors\": {\"patch\": [{\"index\": 0, \"x\": 5}]}}", kHead);
    bad("haspatch", json);
    snprintf(json, sizeof json, "%s, \"mystery\": 1}", kHead);
    bad("unknownkey", json);
    snprintf(json, sizeof json, "%s, \"music\": {\"bank\": 999, \"track\": 0}}", kHead);
    bad("badmusic", json);
    /* s<nnn> is an original stage's folder (its stage.json is a stage-patch-v1): a stage-new-v1 in it is never a new stage, and only the patch loader says why */
    snprintf(json, sizeof json, "%s}", kHead);
    put("tm/stages/s202/stage.json", json);
    maps("tm/stages/s202", 256, 256, ruleFloor, NULL, NULL);
    put("tm/stages/Bad Name/stage.json", json);
    maps("tm/stages/Bad Name", 256, 256, ruleFloor, NULL, NULL);

    logBegin(logPath);
    load("tm", &host);
    log = logEnd(logPath);
    /* haspatch and unknownkey are good until smLoad reads their sections (after the seal): they take a mode and are refused, like aaa below */
    CHECK(msCount() == 3 && msByKey("tm:ok") != NULL && msByKey("tm:haspatch") == NULL && msByKey("tm:unknownkey") == NULL);
    CHECK(msByKey("tm:ok") != NULL && msByKey("tm:ok")->layerFile > 0 && msByKey("tm:ok")->mode == MS_MODE_FIRST + 1);
    CHECK(strstr(log, "notjson/stage.json rejected: not a JSON object") != NULL);
    CHECK(strstr(log, "wrongformat/stage.json rejected: \"format\" must be \"stage-new-v1\"") != NULL);
    CHECK(strstr(log, "nofloor/floor.png rejected: the file could not be read") != NULL);
    CHECK(strstr(log, "nobackground/background.png rejected: the file could not be read") != NULL);
    CHECK(strstr(log, "oddsize/background.png rejected: the size 200 x 256 is not a multiple of 128") != NULL);
    CHECK(strstr(log, "sizemismatch/floor.png rejected: it is 128 x 256 and the background is 256 x 256") != NULL);
    CHECK(strstr(log, "floorvalue/floor.png rejected: the floor value 9") != NULL);
    CHECK(strstr(log, "toomany/background.png rejected: 256 tiles") != NULL);
    CHECK(strstr(log, "nostart/stage.json rejected: \"start\"") != NULL);
    CHECK(strstr(log, "startout/stage.json rejected: \"start\" (5000, 5) is outside the map") != NULL);
    CHECK(strstr(log, "badfallback/stage.json rejected: \"fallback\".\"stage\" 777 is not a stage of the game") != NULL);
    CHECK(strstr(log, "haspatch/stage.json rejected: \"actors\": unknown key \"patch\"") != NULL);
    CHECK(strstr(log, "unknownkey/stage.json rejected: the file: unknown key \"mystery\"") != NULL);
    CHECK(strstr(log, "badmusic/stage.json rejected: \"music\".\"bank\"") != NULL);
    CHECK(strstr(log, "/ok/") == NULL);
    CHECK(msByKey("tm:s202") == NULL && strstr(log, "s202/stage.json rejected: \"format\" must be \"stage-patch-v1\"") != NULL);
    CHECK(strstr(log, "s202/floor.png") == NULL && strstr(log, "s202 ignored") == NULL); /* the new-stage loader leaves it alone: no second line */
    CHECK(strstr(log, "Bad Name ignored: a new stage's folder") != NULL);
    free(log);
    msReset();
    nsReset();
    smUnload();
    pfsClearVirtualFiles();
}

static int sChanged[64];
static int hostFirst(int k) { return k >= 0 && k < 64 ? 20 + k : -1; } /* any file the port can extend: its disc entries + 1 */
static void hostChanged(int k) { if (k >= 0 && k < 64) sChanged[k]++; }

/* the area name banner of a new stage (stage.json "banner"): two named texts appended to the area-name and place-name files once, when the stages load;
   a stage without it, or whose texts are not defined, has no entries (its banner stays off); an object that is not {"area", "place"} refuses the stage */
/* a stage folder with `json` as stage.json and a floor only (no trigger or area layer, so no slot or battle area has to exist) */
static void plain(const char *name, const char *json) {
    char rel[300];

    snprintf(rel, sizeof rel, "tm/stages/%s/stage.json", name);
    put(rel, json);
    snprintf(rel, sizeof rel, "tm/stages/%s", name);
    maps(rel, 256, 256, ruleFloor, NULL, NULL);
}

static void testBanner(const char *logPath) {
    SmHost host;
    char *log, json[2000];
    MsStage *a, *b, *c;
    int ka = tdStageTextK(0xAA - 1), kp = tdStageTextK(0xB8 - 1);

    mod("tm");
    {
        char cmd[1500];

        snprintf(cmd, sizeof cmd, "rm -rf '%s/tm/stages'", root);
        if (system(cmd) != 0) abort();
    }
    put("tm/text/eng/stages.json",
        "{\"format\": \"stage-text-v1\", \"texts\": {\"yard-area\": \"Training Field\", \"yard-place\": \"Lower Floor\", \"other-place\": \"Upper Floor\"}}");
    put("tm/text/es-419/stages.json",
        "{\"format\": \"stage-text-v1\", \"texts\": {\"yard-area\": \"Campo de Entrenamiento\", \"yard-place\": \"Planta Baja\"}}");
    snprintf(json, sizeof json, "%s, \"banner\": {\"area\": \"tm:yard-area\", \"place\": \"tm:yard-place\"}}", kHead);
    plain("yard", json);
    snprintf(json, sizeof json, "%s, \"banner\": {\"area\": \"tm:yard-area\", \"place\": \"tm:other-place\"}}", kHead);
    plain("other", json);
    snprintf(json, sizeof json, "%s}", kHead);
    plain("plain", json);
    snprintf(json, sizeof json, "%s, \"banner\": {\"area\": \"tm:yard-area\", \"place\": \"tm:missing\"}}", kHead);
    plain("notext", json);
    snprintf(json, sizeof json, "%s, \"banner\": {\"area\": \"tm:yard-area\"}}", kHead);
    plain("noplace", json);
    snprintf(json, sizeof json, "%s, \"banner\": {\"area\": \"tm:yard-area\", \"place\": \"tm:yard-place\", \"extra\": 1}}", kHead);
    plain("extrakey", json);

    memset(sChanged, 0, sizeof sChanged);
    logBegin(logPath);
    hostFill(&host);
    host.textFirst = hostFirst;
    host.textChanged = hostChanged;
    pfsInit(NULL, 100);
    pfsClearVirtualFiles();
    msReset();
    nsReset();
    CHECK(modsInit(root, "tm") >= 0);
    nsScan(&host);
    msSeal();
    smLoad(&host);
    nsInstall();
    log = logEnd(logPath);
    if (getenv("NST_DEBUG")) fputs(log, stdout);
    a = msByKey("tm:yard");
    b = msByKey("tm:other");
    c = msByKey("tm:plain");
    CHECK(msByKey("tm:noplace") == NULL && msByKey("tm:extrakey") == NULL);
    CHECK(strstr(log, "noplace/stage.json rejected: \"banner\" needs \"area\" and \"place\"") != NULL);
    CHECK(strstr(log, "extrakey/stage.json rejected: \"banner\": unknown key \"extra\"") != NULL);
    CHECK(strstr(log, "stage tm:notext: the banner's place text \"tm:missing\" is not a line of any mod's text/eng/stages.json: no banner") != NULL);
    CHECK(ka >= 0 && kp >= 0 && ka != kp);
    CHECK(a != NULL && b != NULL && c != NULL && msByKey("tm:notext") != NULL);
    if (a && b && c) {
        /* the entries are the file's first one + the slot of the text: the big line is shared (one entry), the places have one each, in the order the stages are */
        CHECK(a->bannerAreaEntry == hostFirst(ka) && b->bannerAreaEntry == a->bannerAreaEntry);
        CHECK(a->bannerPlaceEntry != b->bannerPlaceEntry && a->bannerPlaceEntry >= hostFirst(kp) && a->bannerPlaceEntry <= hostFirst(kp) + 1 &&
              b->bannerPlaceEntry >= hostFirst(kp) && b->bannerPlaceEntry <= hostFirst(kp) + 1);
        CHECK(c->bannerAreaEntry == 0 && c->bannerPlaceEntry == 0 && msByKey("tm:notext")->bannerAreaEntry == 0 && msByKey("tm:notext")->bannerPlaceEntry == 0);
        CHECK(smTextCount(ka) == 1 && smTextCount(kp) == 2);
        CHECK(!strcmp(smTextOf(ka, 0, "eng"), "Training Field") && !strcmp(smTextOf(ka, 0, "es-419"), "Campo de Entrenamiento"));
        CHECK(!strcmp(smTextOf(kp, a->bannerPlaceEntry - hostFirst(kp), "eng"), "Lower Floor") && !strcmp(smTextOf(kp, a->bannerPlaceEntry - hostFirst(kp), "es-419"), "Planta Baja"));
        CHECK(!strcmp(smTextOf(kp, b->bannerPlaceEntry - hostFirst(kp), "es-419"), "Upper Floor")); /* a language without the line shows English */
        CHECK(sChanged[ka] >= 1 && sChanged[kp] >= 1); /* the text layer was told to serve the two files again */
    }
    free(log);
    msReset();
    nsReset();
    smUnload();
    pfsClearVirtualFiles();
}

static void testChecks(const char *logPath) {
    SmHost host;
    char *log;
    char json[2200];
    MsStage *a, *z;
    int modeA, modeZ, numZ;

    mod("tm");
    {
        char cmd[1500];

        snprintf(cmd, sizeof cmd, "rm -rf '%s/tm/stages'", root);
        if (system(cmd) != 0) abort();
    }
    /* "aaa" is refused after the seal (its trigger layer names a slot it does not have); "bbb" has an exit to it and goes with it; "zzz" stands, and so does "mid" */
    put("tm/stages/aaa/stage.json", kGood);
    maps("tm/stages/aaa", 256, 256, ruleFloor, ruleTrigBad, ruleArea);
    snprintf(json, sizeof json, "%s, \"slots\": {\"add\": [{\"key\": \"go\", \"stage\": \"tm:aaa\", \"x\": 5, \"y\": 6, \"dir\": 0}]}}", kHead);
    put("tm/stages/bbb/stage.json", json);
    maps("tm/stages/bbb", 256, 256, ruleFloor, ruleTrig, NULL);
    put("tm/stages/mid/stage.json", kGood);
    maps("tm/stages/mid", 256, 256, ruleFloor, ruleTrig, ruleAreaBad); /* area 2, one battle area */
    snprintf(json, sizeof json, "%s}", kHead);
    put("tm/stages/zzz/stage.json", json);
    maps("tm/stages/zzz", 256, 256, ruleFloor, NULL, NULL);
    put("tm/stages/zzy/stage.json", json);
    maps("tm/stages/zzy", 256, 256, ruleFloor, ruleZero, NULL);

    logBegin(logPath);
    load("tm", &host);
    log = logEnd(logPath);
    CHECK(strstr(log, "aaa/stage.json rejected: the trigger layer names the slot 1") != NULL);
    CHECK(strstr(log, "bbb/stage.json rejected: slot add 0: there is no stage \"tm:aaa\"") != NULL);
    CHECK(strstr(log, "mid/stage.json rejected: the area layer holds the area 2") != NULL);
    free(log);
    /* the modes were given out before the check: aaa 0x300, bbb 0x301, mid 0x302, zzy 0x303, zzz 0x304; the refused keep their numbers */
    a = msAt(0);
    z = msByKey("tm:zzz");
    modeA = a ? a->mode : 0;
    modeZ = z ? z->mode : 0;
    numZ = msNumberOfKey("tm:zzz");
    CHECK(msCount() == 5 && a != NULL && a->refused && modeA == 0x300);
    CHECK(msByKey("tm:aaa") == NULL && msByMode(0x300) == NULL && msModeOfKey("tm:aaa") == -1 && msNumberOfKey("tm:aaa") == -1);
    CHECK(msByKey("tm:bbb") == NULL && msByKey("tm:mid") == NULL);
    CHECK(z != NULL && modeZ == 0x304 && numZ == MS_NUMBER_BASE + 4 && msByMode(0x304) == z && msKeyOfNumber(numZ) != NULL);
    CHECK(z != NULL && z->layerFile > 0 && z->packFile > 0);
    CHECK(msByKey("tm:zzy") != NULL && msByKey("tm:zzy")->mode == 0x303);
    CHECK(!smHasPatches(MS_NUMBER_BASE) && !smHasPatches(MS_NUMBER_BASE + 1) && smPatchCount() == 2 && smHasPatches(numZ)); /* the two that stand keep their (empty) file */
    msReset();
    nsReset();
    smUnload();
    pfsClearVirtualFiles();
}

int main(void) {
    char tmpl[] = "/tmp/newstagetest.XXXXXX";
    char *base = getenv("TMPDIR");
    char *dir;
    char logPath[1300];

    if (base && *base) {
        snprintf(root, sizeof root, "%s/newstagetest.XXXXXX", base);
        dir = mkdtemp(root);
    } else {
        dir = mkdtemp(tmpl);
        if (dir) snprintf(root, sizeof root, "%s", dir);
    }
    if (!dir) {
        fprintf(stdout, "newstagetest: no scratch folder\n");
        return 2;
    }
    snprintf(logPath, sizeof logPath, "%s/.log", root);
    testStage(logPath);
    testRefusals(logPath);
    testChecks(logPath);
    testBanner(logPath);
    {
        char cmd[1100];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        if (system(cmd) != 0) fprintf(stdout, "newstagetest: could not remove %s\n", root);
    }
    fprintf(stdout, failures ? "newstagetest: %d check(s) FAILED\n" : "newstagetest: ok\n", failures);
    return failures ? 1 : 0;
}
