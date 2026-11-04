#define _GNU_SOURCE 1
/*
 * Host test of the stage patch layer and the mod flags (port-close S4, S5): pc/shim/stage_mods.c, the "mod-flag" / "mod-var" / "set-var" codes
 * of pc/shim/stage_codes.c and the named variables of pc/shim/ext_state.c with their "EXTV" chunk.
 *   - patch / add / remove / keys / prependTalks / later mod wins (conflicts counted) / a missing index or a missing image is skipped, not fatal;
 *   - files that are not valid are refused whole; a stage without a patch is left alone (the caller keeps the game's arrays);
 *   - the codes: same op, same code; the table the interpreter reads (pcModCheck / pcModApply); JSON both ways;
 *   - the variables: set / get, the chunk round trip, unknown names kept, an old save (no chunk) loads empty, a damaged chunk empties.
 * No game, no disc, no retail bytes. Scratch folder under $TMPDIR.
 *
 * Exit 0: every check held.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "engine/game_state.h"
#include "shim/ext_state.h"
#include "shim/mods.h"
#include "shim/stage_mods.h"

GameState GAME;
TechData TECHS_CARD[443];          /* the game's tables, which ext_state.c reaches */
DigimonData DIGIMON_DATA_CARD[55];

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "stagemodtest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char root[1024];

static void put(const char *rel, const char *text) {
    char path[1300], cmd[1400], *slash;
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", root, rel);
    snprintf(cmd, sizeof cmd, "%s", path);
    slash = strrchr(cmd, '/');
    *slash = '\0';
    {
        char mk[1500];

        snprintf(mk, sizeof mk, "mkdir -p '%s'", cmd);
        if (system(mk) != 0) abort();
    }
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

/* ---- the synthetic stage the patches work on: three characters, two actor images ---- */

static void list(SdList *l, const uint16_t *w, int n) {
    l->w = (uint16_t *)malloc((size_t)n * sizeof *w);
    memcpy(l->w, w, (size_t)n * sizeof *w);
    l->n = n;
    l->set = 1;
}

static void talk(SdActor *a, int entry, const uint16_t *cond, int nc, const uint16_t *act, int na) {
    SdTalk *t = sdActorAddTalk(a);

    t->entry = entry;
    if (nc >= 0) list(&t->cond, cond, nc);
    if (na > 0) list(&t->actions, act, na);
}

static void build(SdStage *s) {
    static const uint16_t met[] = {0x1A2C, 0}, setMet[] = {0x1A2C, 1}, there[] = {0x1A21, 1};
    SdActor *a;

    memset(s, 0, sizeof *s);
    s->number = 100;
    s->nimages = 2; /* actor images 2 and 3 */
    a = sdStageAddActor(s);
    a->character = 75, a->image = 2, a->x = 100, a->y = 100, a->dir = 1;
    talk(a, 10, met, 2, setMet, 2);
    talk(a, 11, NULL, -1, NULL, 0);
    a = sdStageAddActor(s);
    a->character = 76, a->image = 3, a->x = 200, a->y = 100, a->dir = 2;
    talk(a, 20, NULL, -1, NULL, 0);
    a = sdStageAddActor(s);
    a->character = 77, a->image = 2, a->x = 300, a->y = 100, a->dir = 3;
    list(&a->cond, there, 2);
    talk(a, 30, NULL, -1, NULL, 0);
}

static int isModCode(unsigned code) {
    int g = (int)((code >> 8) & 0xFE);

    return g >= SC_MOD_GROUP_FIRST && g <= SC_MOD_GROUP_LAST;
}

/* ---- S3: the special conditions a mod defines, named in a patch ---- */

static int fakeResolve(void *ud, const char *kind, const char *name) {
    (void)ud;
    if (strcmp(kind, "special") == 0) {
        return strcmp(name, "t:greeting") == 0 ? 0x100 : strcmp(name, "t:rich") == 0 ? 0x1FF : -1;
    }
    return -1;
}

static const char *fakeSpecialName(void *ud, int id) {
    (void)ud;
    return id == 0x100 ? "t:greeting" : id == 0x1FF ? "t:rich" : NULL;
}

static int fakeSpecialRow(void *ud, int id, int *check, int *arg) {
    (void)ud;
    if (id == 0x100) {
        *check = 0x13;
        *arg = 2;
        return 1;
    }
    return 0;
}

static void testSpecialNames(void) {
    StageCodeEnv base, env;
    ExtJson j;
    uint16_t *w = NULL;
    int n = 0;
    char err[300] = "";
    ScBuf b;
    const char *text = "{\"l\": [[\"special\", \"t:greeting\", 1], [\"special\", \"t:rich\", 0], [\"special\", \"0x2D\", 1], [\"special\", 17, 0]]}";

    memset(&base, 0, sizeof base);
    base.resolve = fakeResolve;
    base.specialName = fakeSpecialName;
    base.specialRow = fakeSpecialRow;
    smCodeEnv(&env, &base);
    CHECK(extJsonParse((const uint8_t *)text, strlen(text), &j) == 0);
    CHECK(scListFromJson(&env, extJsonGet(&j, "l"), &w, &n, err, sizeof err) == 0 && n == 8);
    if (n == 8) {
        CHECK(w[0] == 0x7100 && w[1] == 1 && w[2] == 0x71FF && w[3] == 0 && w[4] == 0x702D && w[6] == 0x7011); /* a name is the id 0x100 up, in the special group */
        memset(&b, 0, sizeof b);
        CHECK(scListToJson(&env, w, n, &b) == 0);
        CHECK(strstr(b.b, "[\"special\", \"t:greeting\", 1, \"partner.unlock(2)\"]") != NULL); /* back to the name, with the row's description */
        CHECK(strstr(b.b, "[\"special\", \"t:rich\", 0]") != NULL && strstr(b.b, "\"0x2D\"") != NULL);
        scFree(&b);
        CHECK(scListToJson(NULL, w, n, &b) == 0 && strstr(b.b, "\"0x100\"") != NULL && strstr(b.b, "t:greeting") == NULL); /* without the names: the hex id */
        scFree(&b);
    }
    free(w);
    extJsonFree(&j);
    {
        static const char *const bad[] = {
            "[[\"special\", \"t:nothing\", 1]]",   /* no mod defines it */
            "[[\"special\", \"nocolon\", 1]]",     /* neither hex nor a name */
            "[[\"special\", \"0x200\", 1]]",       /* past the 9 bits */
        };
        size_t i;

        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            char wrap[200];
            ExtJson q;

            snprintf(wrap, sizeof wrap, "{\"l\": %s}", bad[i]);
            CHECK(extJsonParse((const uint8_t *)wrap, strlen(wrap), &q) == 0);
            err[0] = '\0';
            w = NULL;
            CHECK(scListFromJson(&env, extJsonGet(&q, "l"), &w, &n, err, sizeof err) != 0 && err[0] != '\0');
            free(w);
            extJsonFree(&q);
        }
        CHECK(strstr(err, "<mod>:<name>") != NULL);
    }
    {
        /* without an env that resolves, a name is refused with the same reason (the exporter's env never sees one) */
        ExtJson q;
        const char *one = "{\"l\": [[\"special\", \"t:greeting\", 1]]}";

        CHECK(extJsonParse((const uint8_t *)one, strlen(one), &q) == 0);
        w = NULL;
        err[0] = '\0';
        CHECK(scListFromJson(NULL, extJsonGet(&q, "l"), &w, &n, err, sizeof err) != 0 && strstr(err, "no installed mod defines") != NULL);
        extJsonFree(&q);
    }
}

/* ---- S6: named texts ---- */

static int sFirstCalls, sChanged, sChangedK = -1;

static int fakeTextFirst(int k) {
    sFirstCalls++;
    return k == 3 ? 40 : -1; /* the text file 3 of the disc has 40 entries; no other file can be read */
}

static void fakeTextChanged(int k) {
    sChanged++;
    sChangedK = k;
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

static void testNamedTexts(void) {
    char logPath[1200], *log;
    SmHost host;
    SdStage s;
    SmReport rep;
    SdActor *a;

    mod("t1");
    mod("t2");
    put("t1/text/eng/stages.json", "{\"format\": \"stage-text-v1\", \"texts\": {\"hello\": \"Hello, traveler!\", \"bye\": \"Bye now.\"}}\n");
    put("t1/text/es-419/stages.json", "{\"format\": \"stage-text-v1\", \"texts\": {\"hello\": \"Hola, viajero!\"}}\n");
    put("t1/text/fra/stages.json", "{\"format\": \"stage-text-v1\", \"texts\": {\"x\": \"\"}}\n");                 /* an empty line: the file is refused */
    put("t1/text/ita/stages.json", "{\"format\": \"stage-text-v1\", \"texts\": {\"only\": \"Solo italiano\"}}\n"); /* no English for it */
    put("t2/text/eng/stages.json", "{\"texts\": {\"other\": \"Other\"}}\n");                                         /* no format: refused */
    put("t1/stages/200.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"npc\", \"character\": 75, \"image\": 2, \"x\": 5, \"y\": 6, \"talks\": [\n"
        "  {\"text\": \"t1:hello\", \"conditions\": [[\"mod-flag\", \"t1:met\", 1]]}, {\"text\": \"t1:bye\", \"actions\": [[\"mod-flag\", \"t1:met\", 1]]}]}]}}\n");
    put("t1/stages/206.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"npc\", \"character\": 75, \"image\": 2, \"x\": 5, \"y\": 6, \"talks\": [{\"text\": \"t1:bye\"}]}]}}\n");
    put("t1/stages/201.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"talks\": [{\"text\": \"t1:only\"}]}]}}\n");
    put("t1/stages/202.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"talks\": [{\"text\": \"t1:nothing\"}]}]}}\n");
    put("t1/stages/203.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"talks\": [{\"text\": \"t1:bye\", \"entry\": 3}]}]}}\n");
    put("t1/stages/204.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"talks\": [{\"text\": \"nocolon\"}]}]}}\n");
    put("t2/stages/205.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"talks\": [{\"text\": \"t2:other\"}]}]}}\n");

    snprintf(logPath, sizeof logPath, "%s/.stagelog", root);
    CHECK(modsInit(root, "t1, t2") == 2);
    memset(&host, 0, sizeof host);
    host.textFirst = fakeTextFirst;
    host.textChanged = fakeTextChanged;
    logBegin(logPath);
    CHECK(smLoad(&host) == 2); /* t1:200 and t1:206; the others name texts that are not there */
    log = logEnd(logPath);
    CHECK(strstr(log, "text/fra/stages.json rejected: \"x\" must be a line of text that is not empty") != NULL);
    CHECK(strstr(log, "text/eng/stages.json rejected: \"format\" must be \"stage-text-v1\"") != NULL);
    CHECK(strstr(log, "\"t1:only\" has no English text") != NULL);
    CHECK(strstr(log, "text/es-419/stages.json: \"t1:bye\" is missing here and skipped: the English line is shown instead") != NULL);
    CHECK(strstr(log, "text/ita/stages.json: \"t1:hello\" is missing here and skipped") != NULL && strstr(log, "text/ita/stages.json: \"t1:bye\" is missing here and skipped") != NULL);
    CHECK(strstr(log, "201.json rejected: \"talks\" talk 0: the text \"t1:only\" has no English text") != NULL);
    CHECK(strstr(log, "202.json rejected: \"talks\" talk 0: the text \"t1:nothing\" is not defined") != NULL);
    CHECK(strstr(log, "203.json rejected: \"talks\" talk 0: give an \"entry\" or a \"text\", not both") != NULL);
    CHECK(strstr(log, "204.json rejected: \"talks\" talk 0: \"text\" must be a name") != NULL);
    CHECK(strstr(log, "205.json rejected: \"talks\" talk 0: the text \"t2:other\" is not defined") != NULL);
    free(log);
    CHECK(smTextNames() == 3 && smTextCount(3) == 0); /* t1:hello, t1:bye, t1:only; nothing is appended until a stage that uses them starts */

    /* stage 200: text file 3 (SdStage.textFile 22 is LANGUAGE 2 + the file index 24 = ENG of file k 3) */
    memset(&s, 0, sizeof s);
    s.number = 200;
    s.textFile = 22;
    s.nimages = 2;
    a = sdStageAddActor(&s);
    a->character = 76, a->image = 2, a->x = 1, a->y = 1;
    CHECK(smApplyActors(200, &s, &rep) == 1 && rep.added == 1 && rep.texts == 2);
    CHECK(s.nactors == 2 && s.actors[1].ntalks == 2 && s.actors[1].talks[0].entry == 40 && s.actors[1].talks[1].entry == 41); /* after the file's own 40 entries */
    CHECK(sChanged == 1 && sChangedK == 3 && sFirstCalls >= 1);
    CHECK(smTextCount(3) == 2 && smTextCount(4) == 0 && smTextCount(-1) == 0 && smTextCount(50) == 0);
    CHECK(strcmp(smTextOf(3, 0, "eng"), "Hello, traveler!") == 0 && strcmp(smTextOf(3, 1, "eng"), "Bye now.") == 0);
    CHECK(strcmp(smTextOf(3, 0, "es-419"), "Hola, viajero!") == 0);  /* its own language */
    CHECK(strcmp(smTextOf(3, 1, "es-419"), "Bye now.") == 0);        /* the language lacks it: English */
    CHECK(strcmp(smTextOf(3, 0, "fra"), "Hello, traveler!") == 0 && strcmp(smTextOf(3, 1, "ger"), "Bye now.") == 0 && strcmp(smTextOf(3, 0, "spa"), "Hello, traveler!") == 0);
    CHECK(strcmp(smTextOf(3, 2, "eng"), "") == 0 && strcmp(smTextOf(3, -1, "eng"), "") == 0 && strcmp(smTextOf(9, 0, "eng"), "") == 0);
    sdStageFree(&s);

    /* the same stage again: the same entries, and the text layer is not told again */
    memset(&s, 0, sizeof s);
    s.number = 200;
    s.textFile = 22;
    s.nimages = 2;
    a = sdStageAddActor(&s);
    a->character = 76, a->image = 2;
    CHECK(smApplyActors(200, &s, &rep) == 1 && s.actors[1].talks[0].entry == 40 && s.actors[1].talks[1].entry == 41 && sChanged == 1 && smTextCount(3) == 2);
    sdStageFree(&s);

    /* a stage whose text file the port cannot read (SdStage.textFile -1, or a file the host does not know): the talk says entry 0, with a line */
    memset(&s, 0, sizeof s);
    s.number = 206;
    s.textFile = -1;
    s.nimages = 2;
    a = sdStageAddActor(&s);
    a->character = 76, a->image = 2;
    logBegin(logPath);
    CHECK(smApplyActors(206, &s, &rep) == 1 && rep.texts == 0);
    log = logEnd(logPath);
    CHECK(strstr(log, "the named text \"t1:bye\" has no entry") != NULL);
    CHECK(s.actors[1].ntalks == 1 && s.actors[1].talks[0].entry == 0);
    free(log);
    sdStageFree(&s);
    memset(&s, 0, sizeof s);
    s.number = 206;
    s.textFile = 8; /* file 5's index 10 is ENG? no: 2 + 8 = 10 is slot 3 of row 1, k = 1, which the fake host cannot read */
    s.nimages = 2;
    a = sdStageAddActor(&s);
    a->character = 76, a->image = 2;
    CHECK(smApplyActors(206, &s, &rep) == 1 && s.actors[1].talks[0].entry == 0 && smTextCount(1) == 0);
    sdStageFree(&s);

    smUnload();
    CHECK(smTextNames() == 0 && smTextCount(3) == 0); /* unloading forgets the names and the appended entries */
}

/* ---- S7: the port's own actions ---- */

static int sRan, sRanKind, sRanArgs[SC_ACT_ARGS];

static void fakeRunner(int kind, const int *args) {
    sRan++;
    sRanKind = kind;
    memcpy(sRanArgs, args, sizeof sRanArgs);
}

static int fakeStageExists(void *ud, int stage) {
    (void)ud;
    return stage == 330 || stage == 380;
}

static int isActCode(unsigned code) {
    int g = (int)((code >> 8) & 0xFE);

    return g >= SC_ACT_GROUP_FIRST && g <= SC_ACT_GROUP_LAST;
}

static void testActions(void) {
    StageCodeEnv base, env;
    ExtJson j;
    uint16_t *w = NULL;
    int n = 0, i;
    char err[300] = "";
    ScBuf b;
    const char *text = "{\"l\": [[\"give-money\", 500], [\"take-money\", 0], [\"set-progress\", 12], [\"warp\", 380, 810, 820, 3], [\"sound\", \"teleport\"],"
                       " [\"give-money\", 500], [\"mod-flag\", \"a:met\", 1]]}";

    memset(&base, 0, sizeof base);
    base.stageExists = fakeStageExists;
    smCodeEnv(&env, &base);
    smSetActRunner(fakeRunner);
    CHECK(extJsonParse((const uint8_t *)text, strlen(text), &j) == 0);
    CHECK(scListFromJson(&env, extJsonGet(&j, "l"), &w, &n, err, sizeof err) == 0 && n == 14);
    if (n == 14) {
        for (i = 0; i < 5; i++) CHECK(isActCode(w[2 * i]) && w[2 * i + 1] == 1);
        CHECK(w[0] == w[10] && w[0] != w[2] && w[2] != w[4] && w[4] != w[6] && w[6] != w[8]); /* the same op, the same code */
        CHECK(!isActCode(w[12])); /* a mod flag stays in its own groups */

        /* the interpreter's side: a condition always holds, an action reaches the runner with its numbers */
        CHECK(pcModCheck((unsigned short)w[0], 1) == 1 && pcModCheck((unsigned short)w[0], 0) == 1 && pcModCheck(0x6A00 | 0x1FF, 0) == 1);
        sRan = 0;
        pcModApply((int)w[0], 1);
        CHECK(sRan == 1 && sRanKind == SC_ACT_GIVE_MONEY && sRanArgs[0] == 500);
        pcModApply((int)w[2], 1);
        CHECK(sRan == 2 && sRanKind == SC_ACT_TAKE_MONEY && sRanArgs[0] == 0);
        pcModApply((int)w[4], 1);
        CHECK(sRan == 3 && sRanKind == SC_ACT_SET_PROGRESS && sRanArgs[0] == 12);
        pcModApply((int)w[6], 1);
        CHECK(sRan == 4 && sRanKind == SC_ACT_WARP && sRanArgs[0] == 380 && sRanArgs[1] == 810 && sRanArgs[2] == 820 && sRanArgs[3] == 3);
        pcModApply((int)w[8], 1);
        CHECK(sRan == 5 && sRanKind == SC_ACT_SOUND && sRanArgs[0] == scSoundByName("teleport"));
        pcModApply(0x6A00 | 0x1FF, 1); /* a code the table never made does nothing */
        CHECK(sRan == 5);
        {
            SmRequest l = {SM_REQ_LEAVE, 551, -1, 810, 820, 3}, got;

            CHECK(smTakeRequest(&got) == 0); /* nothing posted: nothing to take */
            smPostRequest(&l);
            l.mode = 552;
            smPostRequest(&l); /* the later request replaces the earlier */
            CHECK(smTakeRequest(&got) == 1 && got.kind == SM_REQ_LEAVE && got.mode == 552 && got.arg == -1 && got.x == 810 && got.y == 820 && got.dir == 3);
            CHECK(smTakeRequest(&got) == 0); /* taken once */
        }
        smSetActRunner(NULL);
        pcModApply((int)w[0], 1); /* no runner: nothing, no crash */
        CHECK(sRan == 5);
        smSetActRunner(fakeRunner);

        /* back to JSON, with the table and without */
        memset(&b, 0, sizeof b);
        CHECK(scListToJson(&env, w, n, &b) == 0);
        CHECK(strcmp(b.b, "[[\"give-money\", 500], [\"take-money\", 0], [\"set-progress\", 12], [\"warp\", 380, 810, 820, 3], [\"sound\", \"teleport\"], "
                          "[\"give-money\", 500], [\"mod-flag\", \"a:met\", 1]]") == 0);
        scFree(&b);
        CHECK(scListToJson(NULL, w, n, &b) == 0 && strstr(b.b, "give-money") == NULL && strstr(b.b, "\"raw\"") != NULL);
        scFree(&b);
    }
    free(w);
    extJsonFree(&j);
    CHECK(scSoundByName("select") == 0 && scSoundByName("inn-jingle") == SC_SOUND_COUNT - 1 && scSoundByName("nope") == -1 && scSoundName(SC_SOUND_COUNT) == NULL);
    CHECK(scActByName("warp") == SC_ACT_WARP && scActByName("item") == 0 && !strcmp(scActName(SC_ACT_SOUND), "sound") && scActName(0) == NULL);
    /* refused, each with its reason */
    {
        static const struct {
            const char *list, *why;
        } bad[] = {
            {"[[\"give-money\", -1]]", "0 to 9999999"},
            {"[[\"give-money\", 10000000]]", "0 to 9999999"},
            {"[[\"take-money\", 1.5]]", "whole number"},
            {"[[\"give-money\"]]", "is not [\"name\""},
            {"[[\"set-progress\", 100]]", "0 to 99"},
            {"[[\"warp\", 380, 1, 2]]", "needs 5 elements"},
            {"[[\"warp\", 999, 1, 2, 3]]", "no such stage"},
            {"[[\"warp\", 380, 9000, 2, 3]]", "x must be"},
            {"[[\"warp\", 380, 1, -2, 3]]", "y must be"},
            {"[[\"warp\", 380, 1, 2, 8]]", "direction"},
            {"[[\"sound\", \"boom\"]]", "must be one of"},
            {"[[\"sound\", 3]]", "must be one of"},
        };
        size_t k;

        for (k = 0; k < sizeof bad / sizeof bad[0]; k++) {
            char wrap[200];
            ExtJson q;
            uint16_t *qw = NULL;
            int qn = 0;

            snprintf(wrap, sizeof wrap, "{\"l\": %s}", bad[k].list);
            CHECK(extJsonParse((const uint8_t *)wrap, strlen(wrap), &q) == 0);
            err[0] = '\0';
            CHECK(scListFromJson(&env, extJsonGet(&q, "l"), &qw, &qn, err, sizeof err) != 0 && strstr(err, bad[k].why) != NULL);
            free(qw);
            extJsonFree(&q);
        }
        {
            ExtJson q;
            const char *one = "{\"l\": [[\"give-money\", 5]]}";
            uint16_t *qw = NULL;
            int qn = 0;

            CHECK(extJsonParse((const uint8_t *)one, strlen(one), &q) == 0);
            err[0] = '\0';
            CHECK(scListFromJson(NULL, extJsonGet(&q, "l"), &qw, &qn, err, sizeof err) != 0 && strstr(err, "stage patch layer") != NULL); /* the exporter's env has no table */
            extJsonFree(&q);
        }
    }
    /* the table fills up: 1024 distinct ops, then -1 */
    smUnload();
    {
        int a[SC_ACT_ARGS] = {0}, made = 0, last = 0;

        {
            /* S8, S10: start-battle and run-event, with a name and with a number */
            ExtJson j2;
            uint16_t *w2 = NULL;
            int n2 = 0;
            ScBuf b2;
            const char *t2 = "{\"l\": [[\"start-battle\", 35, 5, 12], [\"run-event\", \"s8:intro\"], [\"run-event\", 7]]}";

            CHECK(extJsonParse((const uint8_t *)t2, strlen(t2), &j2) == 0);
            CHECK(scListFromJson(&env, extJsonGet(&j2, "l"), &w2, &n2, err, sizeof err) == 0 && n2 == 6);
            if (n2 == 6) {
                sRan = 0;
                pcModApply((int)w2[0], 1);
                CHECK(sRan == 1 && sRanKind == SC_ACT_START_BATTLE && sRanArgs[0] == 35 && sRanArgs[1] == 5 && sRanArgs[2] == 12);
                pcModApply((int)w2[2], 1);
                CHECK(sRan == 2 && sRanKind == SC_ACT_RUN_EVENT && sRanArgs[0] == smEventId("s8:intro"));
                pcModApply((int)w2[4], 1);
                CHECK(sRanArgs[0] == 7);
                memset(&b2, 0, sizeof b2);
                CHECK(scListToJson(&env, w2, n2, &b2) == 0 && strcmp(b2.b, "[[\"start-battle\", 35, 5, 12], [\"run-event\", \"s8:intro\"], [\"run-event\", 7]]") == 0);
                scFree(&b2);
            }
            free(w2);
            extJsonFree(&j2);
            {
                static const char *const badActs[] = {"[[\"start-battle\", 0, 1, 1]]", "[[\"start-battle\", 5000, 1, 1]]", "[[\"start-battle\", 5, 1]]",
                                                      "[[\"run-event\", \"nocolon\"]]", "[[\"run-event\", 0]]"};
                size_t q;

                for (q = 0; q < sizeof badActs / sizeof badActs[0]; q++) {
                    char wrap[200];
                    ExtJson x;
                    uint16_t *wx = NULL;
                    int nx = 0;

                    snprintf(wrap, sizeof wrap, "{\"l\": %s}", badActs[q]);
                    CHECK(extJsonParse((const uint8_t *)wrap, strlen(wrap), &x) == 0);
                    err[0] = '\0';
                    CHECK(scListFromJson(&env, extJsonGet(&x, "l"), &wx, &nx, err, sizeof err) != 0 && err[0] != '\0');
                    free(wx);
                    extJsonFree(&x);
                }
            }
        }

        for (i = 0; i < SC_ACT_OPS_MAX + 5; i++) {
            a[0] = i;
            last = smActCode(SC_ACT_GIVE_MONEY, a);
            made += last >= 0;
        }
        CHECK(made == SC_ACT_OPS_MAX - 3 && last == -1 && smActCode(0, a) == -1 && smActCode(SC_ACT_KINDS, a) == -1);
        a[0] = 7;
        CHECK(smActCode(SC_ACT_GIVE_MONEY, a) >= 0); /* an op that is already there still answers */
    }
    smSetActRunner(NULL);
}

/* ---- S8, S10: slots, events and battles of a stage ---- */

static int fakeStageMode(int n) {
    return n > 0 && n < 1000 ? n + 100 : 0;
}

static int fakeEncounterOk(int row) {
    return row >= 1 && row <= 335;
}

static int fakeStageExists2(int n) {
    return n > 0 && n < 1000;
}

static void testStageData(void) {
    char logPath[1200], *log;
    SmHost host;
    SdStage s;
    SmReport rep;
    SdSlot *sl;
    SdEvent *ev;
    int i, k;
    static const int16_t gameScript[] = {0x300, 10, 0};

    mod("s8");
    mod("s8bad");
    put("s8/text/eng/stages.json", "{\"format\": \"stage-text-v1\", \"texts\": {\"hi\": \"Hi there.\", \"bye\": \"Bye.\"}}\n");
    put("s8/stages/300.json",
        "{\"format\": \"stage-patch-v1\",\n"
        " \"slots\": {\"patch\": [{\"index\": 0, \"stage\": 330, \"x\": 700, \"y\": 800, \"dir\": 3, \"conditions\": [[\"mod-flag\", \"s8:open\", 1]]},\n"
        "                       {\"index\": 2, \"event\": \"s8:intro\"}, {\"index\": 9, \"x\": 1}, {\"key\": \"s8:door\", \"x\": 11}],\n"
        "  \"add\": [{\"key\": \"door\", \"stage\": 380, \"x\": 810, \"y\": 820, \"dir\": 3, \"area\": {\"x\": 100, \"y\": 120, \"w\": 32, \"h\": 16, \"facing\": 2}},\n"
        "           {\"key\": \"s8:talk\", \"event\": \"s8:intro\", \"area\": {\"x\": 5, \"y\": 6, \"w\": 7, \"h\": 8}}],\n"
        "  \"remove\": [1, 7]},\n"
        " \"events\": {\"patch\": [{\"id\": 7, \"script\": [[\"wait\", 60], [\"end\"]]}, {\"id\": 99, \"script\": [[\"end\"]]}],\n"
        "   \"add\": [{\"key\": \"intro\", \"script\": [[\"place\", 2, 10, 20], [\"talk\", 0, \"s8:hi\", 2, 4], [\"wait-box\"], [\"talk\", 1, 3, 2, 4], [\"leave\", 551, 810, 820, 3], [\"end\"]]},\n"
        "            {\"key\": \"s8:note\", \"script\": [[\"wait\", 1], [\"talk\", 0, \"s8:bye\", 0, 4], [\"end\"]]}]},\n"
        " \"battles\": {\"patch\": [{\"area\": 1, \"rate\": 4}, {\"area\": 2, \"index\": 3, \"encounter\": 35, \"stage\": 5, \"music\": 12},\n"
        "              {\"area\": 3, \"indices\": [0, 7], \"encounter\": 36}, {\"area\": 9, \"encounter\": 36}],\n"
        "   \"add\": [{\"area\": 4, \"rate\": 2, \"slots\": [{\"encounter\": 40, \"stage\": 1, \"music\": 2}, {\"encounter\": 41, \"stage\": 1, \"music\": 2}, {\"encounter\": 42, \"stage\": 1, \"music\": 2}]},\n"
        "             {\"area\": 7, \"rate\": 1, \"slots\": [{\"encounter\": 40, \"stage\": 1, \"music\": 2}]}]}}\n");
    /* files that must be refused, with the reason */
    put("s8bad/stages/301.json", "{\"format\": \"stage-patch-v1\", \"slots\": {\"add\": [{\"key\": \"x\", \"type\": \"exit\", \"arg\": 1}]}}\n"); /* no area */
    put("s8bad/stages/302.json", "{\"format\": \"stage-patch-v1\", \"slots\": {\"patch\": [{\"index\": 0, \"stage\": 330, \"arg\": 5}]}}\n");
    put("s8bad/stages/303.json", "{\"format\": \"stage-patch-v1\", \"slots\": {\"patch\": [{\"index\": 0, \"conditions\": [[\"flag\", \"00:01\", 1], [\"flag\", \"00:02\", 1], [\"flag\", \"00:03\", 1]]}]}}\n");
    put("s8bad/stages/304.json", "{\"format\": \"stage-patch-v1\", \"events\": {\"add\": [{\"key\": \"e\"}]}}\n");
    put("s8bad/stages/305.json", "{\"format\": \"stage-patch-v1\", \"events\": {\"add\": [{\"key\": \"e\", \"script\": [[\"talk\", 0, \"s8:nothing\", 2, 4], [\"end\"]]}]}}\n");
    put("s8bad/stages/306.json", "{\"format\": \"stage-patch-v1\", \"battles\": {\"patch\": [{\"area\": 1, \"encounter\": 336}]}}\n");
    put("s8bad/stages/307.json", "{\"format\": \"stage-patch-v1\", \"battles\": {\"add\": [{\"area\": 5, \"rate\": 1}]}}\n");
    put("s8bad/stages/310.json", "{\"format\": \"stage-patch-v1\", \"events\": {\"add\": [{\"key\": \"e\", \"text\": \"s8:hi\", \"script\": [[\"end\"]]}]}}\n");
    put("s8bad/stages/308.json", "{\"format\": \"stage-patch-v1\", \"slots\": {\"patch\": [{\"index\": 0, \"stage\": 5000}]}}\n");
    put("s8bad/stages/309.json", "{\"format\": \"stage-patch-v1\", \"slots\": {\"patch\": [{\"index\": 0, \"type\": \"teleport\"}]}}\n");

    snprintf(logPath, sizeof logPath, "%s/.stagelog2", root);
    CHECK(modsInit(root, "s8, s8bad") == 2);
    memset(&host, 0, sizeof host);
    host.textFirst = fakeTextFirst;
    host.textChanged = fakeTextChanged;
    host.stageMode = fakeStageMode;
    host.stageExists = fakeStageExists2;
    host.encounterOk = fakeEncounterOk;
    logBegin(logPath);
    CHECK(smLoad(&host) == 1); /* only s8's 300 stands */
    log = logEnd(logPath);
    CHECK(strstr(log, "301.json rejected: slot add 0 needs an \"area\"") != NULL);
    CHECK(strstr(log, "302.json rejected: slot patch 0: give \"arg\" or \"stage\", not both") != NULL);
    CHECK(strstr(log, "303.json rejected: slot patch 0: a slot holds at most two conditions") != NULL);
    CHECK(strstr(log, "304.json rejected: event add 0 needs a \"script\"") != NULL);
    CHECK(strstr(log, "305.json rejected: event add 0 command 0: the text \"s8:nothing\" is not defined") != NULL);
    CHECK(strstr(log, "306.json rejected: battles patch 0: there is no field encounter 336") != NULL);
    CHECK(strstr(log, "307.json rejected: battles add 0 needs a \"rate\" and \"slots\"") != NULL);
    CHECK(strstr(log, "308.json rejected: slot patch 0: there is no stage 5000 in this build") != NULL);
    CHECK(strstr(log, "310.json rejected: event add 0: \"text\" is the game's own block of lines") != NULL);
    CHECK(strstr(log, "309.json rejected: slot patch 0: unknown slot type \"teleport\"") != NULL);
    free(log);
    CHECK(smEventId("s8:intro") >= SM_EVENT_BASE && smEventId("s8:intro") == smEventId("s8:intro") && smEventId("s8:intro") != smEventId("s8:note"));
    CHECK(strcmp(smEventName(smEventId("s8:intro")), "s8:intro") == 0 && smEventName(5) == NULL && smEventId("nocolon") < 0);

    /* the stage as its setup gave it: three slots, two events (one that runs C), battles of three areas, a text file of 40 entries (k 3) */
    memset(&s, 0, sizeof s);
    s.number = 300;
    s.textFile = 22;
    for (i = 0; i < 3; i++) {
        sl = sdStageAddSlot(&s);
        sl->type = 1 + i;
        sl->arg = 100 + i;
        sl->x = 10 * i;
        sl->facing = -1;
    }
    ev = sdStageAddEvent(&s);
    ev->id = 7;
    ev->hasStart = 1;
    ev->hasEnd = 1;
    ev = sdStageAddEvent(&s);
    ev->id = 8;
    sdEventSetScript(ev, gameScript);
    ev->hasText = 1;
    ev->textFile = 22;
    s.battles.present = 1;
    s.battles.nareas = 3;
    for (i = 0; i < 3; i++) {
        s.battles.areas[i].rate = i;
        for (k = 0; k < 8; k++) s.battles.areas[i].b[k].encounter = 10 + i;
    }
    logBegin(logPath);
    CHECK(smApplyStage(300, &s, &rep) == 1);
    log = logEnd(logPath);
    /* slots: 3 + 2 added; 0 patched to stage 330 (mode 430) with its destination and a mod flag; 2 to the event; 1 removed; the missing 9, key and 7 are logged */
    CHECK(s.nslots == 5 && rep.slotsPatched == 2 && rep.slotsAdded == 2 && rep.slotsRemoved == 1);
    CHECK(s.slots[0].type == 1 && s.slots[0].arg == 430 && s.slots[0].x == 700 && s.slots[0].y == 800 && s.slots[0].dir == 3 && s.slots[0].cond.n == 2);
    CHECK(s.slots[1].type == SM_SLOT_OFF && !s.slots[1].hasArea && s.slots[1].cond.n == 0); /* removed: it stays in its place */
    CHECK(s.slots[2].type == 3 && s.slots[2].arg == smEventId("s8:intro"));
    CHECK(s.slots[3].type == 1 && s.slots[3].arg == 480 && s.slots[3].x == 810 && s.slots[3].hasArea && s.slots[3].ax == 100 && s.slots[3].aw == 32 && s.slots[3].facing == 2);
    CHECK(s.slots[4].type == 8 && s.slots[4].arg == smEventId("s8:intro") && s.slots[4].hasArea && s.slots[4].facing == -1);
    CHECK(strstr(log, "slot 9 skipped: the stage's table has no slot here") != NULL);
    CHECK(strstr(log, "slot \"s8:door\" skipped: no slot has this key") != NULL); /* patches run before the adds of their file, as the characters' do */
    CHECK(strstr(log, "slot 7 skipped: the stage's table has no slot here") != NULL);
    /* events: 7 keeps its C functions (a script now), 8 untouched, two added */
    CHECK(s.nevents == 4 && rep.eventsPatched == 1 && rep.eventsAdded == 2);
    CHECK(s.events[0].id == 7 && s.events[0].hasScript && s.events[0].nscript == 3 && s.events[0].hasStart && s.events[0].hasEnd && !s.events[0].isNew);
    CHECK(s.events[1].id == 8 && s.events[1].nscript == 3 && !s.events[1].isNew);
    CHECK(s.events[2].isNew && s.events[2].id == smEventId("s8:intro") && s.events[2].nscript == 21 && s.events[2].hasText && s.events[2].textFile == 22 && s.events[2].textEntry == SM_EVENT_TEXT_STAGE);
    CHECK((uint16_t)s.events[2].script[0] == 0x100 && (uint16_t)s.events[2].script[4] == 0x200 && s.events[2].script[6] == 40); /* the named text is entry 40, after the file's own */
    CHECK(s.events[2].script[12] == 3 && (uint16_t)s.events[2].script[10] == 0x200); /* a numeric entry stays */
    CHECK(s.events[3].isNew && s.events[3].hasText && s.events[3].textEntry == SM_EVENT_TEXT_STAGE && s.events[3].script[4] == 41);
    CHECK(strstr(log, "event 99 skipped: the stage's table has no event with this id") != NULL);
    CHECK(smTextCount(3) == 2 && strcmp(smTextOf(3, 0, "eng"), "Hi there.") == 0 && strcmp(smTextOf(3, 1, "eng"), "Bye.") == 0);
    /* battles: rate 4 on the first area; area 2's fourth battle; area 3's first and last; area 4 added after the three; area 7 is not next */
    CHECK(s.battles.nareas == 4 && rep.battlesPatched == 3 && rep.areasAdded == 1);
    CHECK(s.battles.areas[0].rate == 4 && s.battles.areas[0].b[0].encounter == 10);
    CHECK(s.battles.areas[1].b[3].encounter == 35 && s.battles.areas[1].b[3].stage == 5 && s.battles.areas[1].b[3].music == 12 && s.battles.areas[1].b[2].encounter == 11);
    CHECK(s.battles.areas[2].b[0].encounter == 36 && s.battles.areas[2].b[7].encounter == 36 && s.battles.areas[2].b[3].encounter == 12);
    CHECK(s.battles.areas[3].rate == 2 && s.battles.areas[3].b[0].encounter == 40 && s.battles.areas[3].b[1].encounter == 41 && s.battles.areas[3].b[3].encounter == 40 &&
          s.battles.areas[3].b[7].encounter == 41); /* three given, repeated to eight: 40 41 42 40 41 42 40 41 */
    CHECK(strstr(log, "battles area 9 skipped: the stage has 4 areas") != NULL || strstr(log, "battles area 9 skipped: the stage has 3 areas") != NULL);
    CHECK(strstr(log, "battles area 7 skipped: an added area is the next one") != NULL);
    free(log);
    {
        /* the model with all of it writes and reads back, lossless */
        char *json;
        SdStage back;
        char err[300] = "";

        json = sdStageToJson(NULL, &s);
        CHECK(json != NULL);
        memset(&back, 0, sizeof back);
        CHECK(json && sdStageFromJson(NULL, json, strlen(json), &back, err, sizeof err) == 0);
        CHECK(back.nslots == 5 && back.slots[3].hasArea && back.slots[3].facing == 2 && back.slots[2].hasArea == 0 && back.battles.present && back.battles.nareas == 4);
        CHECK(back.battles.areas[3].b[7].encounter == 41);
        sdStageFree(&back);
        free(json);
    }
    sdStageFree(&s);

    /* a stage without a patch is not touched; battles-less stage logs the skip */
    memset(&s, 0, sizeof s);
    s.number = 301;
    CHECK(smApplyStage(301, &s, &rep) == 0);
    sdStageFree(&s);
    smUnload();
    CHECK(smEventName(SM_EVENT_BASE) == NULL); /* the names are forgotten with the patches */
}


/* ---- S9: the sprites mods bring and the actors that need a slot of their own ---- */

static int sSpriteBuilt;
static char sSpriteDirs[4][300];
static int sDone;

static int fakeCharacterOk(int c) { return c >= 1 && c <= 400 && c != 5; }

static int fakeSpriteAdd(const char *dir, const char *label, SmSprite *out, char *why, size_t cap) {
    char path[1400];
    FILE *f;

    snprintf(path, sizeof path, "%s/ok.txt", dir);
    f = fopen(path, "rb");
    if (!f) {
        snprintf(why, cap, "%s has no ok.txt", label);
        return -1;
    }
    fclose(f);
    if (sSpriteBuilt < 4) snprintf(sSpriteDirs[sSpriteBuilt], sizeof sSpriteDirs[0], "%s", label);
    out->entry = 10 + 5 * sSpriteBuilt;
    out->width = 24;
    out->w = 32 + 4 * sSpriteBuilt;
    out->h = 48;
    sSpriteBuilt++;
    return 0;
}

static void fakeSpritesDone(void) { sDone++; }

static void testSprites(void) {
    SmHost host;
    SdStage s;
    SmReport rep;
    SmSprite info;
    char logPath[1200], *log;
    int i;

    mod("s9");
    mod("s9b");
    mod("s9bad");
    put("s9/sprites/fox/ok.txt", "x");
    put("s9/sprites/Bad Name/ok.txt", "x");
    put("s9/sprites/broken/readme.txt", "x"); /* the host refuses it */
    put("s9b/sprites/owl/ok.txt", "x");
    put("s9/stages/400.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\n"
        "  \"patch\": [\n"
        "    {\"index\": 0, \"character\": 43},\n"                      /* a character the stage lacks: its own slot */
        "    {\"index\": 1, \"sprite\": \"fox\", \"image\": 3},\n"       /* an explicit slot is kept */
        "    {\"index\": 2, \"x\": 5}],\n"                              /* no character change: the slot stays */
        "  \"add\": [\n"
        "    {\"key\": \"a\", \"character\": 43, \"x\": 1, \"y\": 2},\n"          /* the same character shares the slot of index 0 */
        "    {\"key\": \"b\", \"sprite\": \"s9:fox\", \"x\": 3, \"y\": 4},\n"
        "    {\"key\": \"c\", \"sprite\": \"s9b:owl\", \"x\": 5, \"y\": 6, \"image\": 2},\n"
        "    {\"key\": \"d\", \"sprite\": \"fox\", \"x\": 7, \"y\": 8},\n"
        "    {\"key\": \"e\", \"character\": 77, \"x\": 9, \"y\": 10, \"image\": 2}]}}\n");
    put("s9bad/stages/401.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"x\", \"sprite\": \"nothing\", \"x\": 1, \"y\": 2}]}}\n");
    put("s9bad/stages/402.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"x\", \"character\": 5, \"x\": 1, \"y\": 2}]}}\n");
    put("s9bad/stages/403.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"x\", \"character\": 43, \"sprite\": \"s9:fox\", \"x\": 1, \"y\": 2}]}}\n");
    put("s9bad/stages/404.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"x\", \"character\": 16385, \"x\": 1, \"y\": 2}]}}\n");
    put("s9bad/stages/405.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"x\", \"sprite\": \"s9:broken\", \"x\": 1, \"y\": 2}]}}\n");
    put("s9bad/stages/406.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"sprite\": 3}]}}\n");
    put("s9bad/stages/407.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"x\", \"sprite\": \"s9:Fox\", \"x\": 1, \"y\": 2}]}}\n");
    put("s9bad/stages/408.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"x\", \"character\": 43, \"x\": 1, \"y\": 2, \"image\": 99}]}}\n");

    snprintf(logPath, sizeof logPath, "%s/.stagelog3", root);
    CHECK(modsInit(root, "s9, s9b, s9bad") == 3);
    memset(&host, 0, sizeof host);
    host.characterOk = fakeCharacterOk;
    host.spriteAdd = fakeSpriteAdd;
    host.spritesDone = fakeSpritesDone;
    logBegin(logPath);
    CHECK(smLoad(&host) == 2); /* s9's 400 and the one of 408, which only fails when it is applied */
    log = logEnd(logPath);
    CHECK(sDone == 1 && sSpriteBuilt == 2 && smSpriteCount() == 2);
    CHECK(strstr(log, "s9/sprites/broken rejected: s9:broken has no ok.txt") != NULL);
    CHECK(strstr(log, "sprite s9:fox is character 16384 (frames up to 32x48)") != NULL);
    CHECK(strstr(log, "the sprite folder \"Bad Name\" is skipped") != NULL);
    CHECK(strstr(log, "401.json rejected: add 0: there is no sprite \"s9bad:nothing\"") != NULL);
    CHECK(strstr(log, "402.json rejected: add 0: the game has no character 5 with a sprite") != NULL);
    CHECK(strstr(log, "403.json rejected: add 0: give \"character\" (one of the game's) or \"sprite\" (one a mod brings), not both") != NULL);
    CHECK(strstr(log, "404.json rejected: add 0: the characters from 16384 up are the sprites mods bring") != NULL);
    CHECK(strstr(log, "405.json rejected: add 0: there is no sprite \"s9:broken\"") != NULL);
    CHECK(strstr(log, "406.json rejected: patch 0: \"sprite\" must be a string") != NULL);
    CHECK(strstr(log, "407.json rejected: add 0: there is no sprite \"s9:Fox\"") != NULL || strstr(log, "407.json rejected: add 0: s9:Fox") != NULL ||
          strstr(log, "407.json rejected") != NULL);
    free(log);
    CHECK(strcmp(smSpriteName(16384), "s9:fox") == 0 && strcmp(smSpriteName(16385), "s9b:owl") == 0 && smSpriteName(16386) == NULL && smSpriteName(75) == NULL);
    CHECK(smSpriteInfo(16385, &info) == 1 && info.entry == 15 && info.w == 36 && info.h == 48 && info.width == 24 && smSpriteInfo(16384, &info) == 1 && info.entry == 10 &&
          smSpriteInfo(75, &info) == 0 && smSpriteInfo(16386, &info) == 0);

    build(&s);
    s.number = 400;
    memset(&rep, 0, sizeof rep);
    CHECK(smApplyStage(400, &s, &rep) == 1);
    CHECK(rep.added == 5 && s.nactors == 8);
    /* 0: character 43 with no image -> the first extra; 1: kept its explicit slot 3; 2: untouched */
    CHECK(s.actors[0].character == 43 && s.actors[0].image == SD_IMAGE_EXTRA_FIRST);
    CHECK(s.actors[1].character == SM_SPRITE_CHAR_BASE && s.actors[1].image == 3);
    CHECK(s.actors[2].character == 77 && s.actors[2].image == 2);
    /* a: character 43 shares index 0's slot; b: the fox, a new one; c: explicit image 2; d: the fox again shares b's; e: explicit */
    CHECK(s.actors[3].character == 43 && s.actors[3].image == SD_IMAGE_EXTRA_FIRST);
    CHECK(s.actors[4].character == SM_SPRITE_CHAR_BASE && s.actors[4].image == SD_IMAGE_EXTRA_FIRST + 1);
    CHECK(s.actors[5].character == SM_SPRITE_CHAR_BASE + 1 && s.actors[5].image == 2);
    CHECK(s.actors[6].character == SM_SPRITE_CHAR_BASE && s.actors[6].image == SD_IMAGE_EXTRA_FIRST + 1);
    CHECK(s.actors[7].character == 77 && s.actors[7].image == 2);
    CHECK(s.nextra == 2 && s.extra[0] == 43 && s.extra[1] == SM_SPRITE_CHAR_BASE);
    for (i = 0; i < s.nactors; i++) CHECK(s.actors[i].image != SD_IMAGE_AUTO);
    sdStageFree(&s);
    /* a stage with no patch of its own leaves the model alone */
    build(&s);
    CHECK(smApplyStage(100, &s, &rep) == 0 && s.nextra == 0 && s.extra == NULL);
    sdStageFree(&s);
    smUnload();
    CHECK(smSpriteCount() == 0 && smSpriteName(16384) == NULL);
}

int main(void) {
    char tmpl[] = "/tmp/stagemodtest.XXXXXX";
    char *base = getenv("TMPDIR");
    SdStage s;
    SmReport rep;
    SmHost host;
    StageCodeEnv env;
    char *dir;

    if (base && *base) {
        snprintf(root, sizeof root, "%s/stagemodtest.XXXXXX", base);
        dir = mkdtemp(root);
    } else {
        dir = mkdtemp(tmpl);
        if (dir) snprintf(root, sizeof root, "%s", dir);
    }
    if (!dir) {
        fprintf(stdout, "stagemodtest: no scratch folder\n");
        return 2;
    }

    /* ---- mods: a patches stages 100 and 101; b patches 100 after it; "bad" holds files that must be refused ---- */
    mod("a");
    mod("b");
    mod("bad");
    put("a/stages/100.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\n"
        "  \"patch\": [\n"
        "    {\"index\": 0, \"x\": 150, \"y\": 160, \"dir\": 5},\n"
        "    {\"index\": 1, \"talks\": [{\"entry\": 21, \"conditions\": [[\"mod-flag\", \"a:met\", 1]]}, {\"entry\": 22}]},\n"
        "    {\"index\": 9, \"x\": 1}],\n"
        "  \"add\": [{\"key\": \"guide\", \"character\": 75, \"image\": 2, \"x\": 50, \"y\": 60, \"dir\": 0, \"talks\": [\n"
        "    {\"entry\": 40, \"conditions\": [[\"mod-flag\", \"a:met\", 1]]},\n"
        "    {\"entry\": 41, \"actions\": [[\"mod-flag\", \"a:met\", 1], [\"set-var\", \"a:count\", 3]]}]}],\n"
        "  \"remove\": [2]}}\n");
    put("a/stages/101.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 1, \"image\": 9, \"x\": 5}]}}\n");
    put("b/stages/100.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\n"
        "  \"patch\": [\n"
        "    {\"index\": 0, \"x\": 999},\n"
        "    {\"key\": \"a:guide\", \"x\": 70, \"image\": 3},\n"
        "    {\"index\": 2, \"x\": 1},\n"
        "    {\"index\": 1, \"prependTalks\": [{\"entry\": 23, \"conditions\": [[\"mod-var\", \"b:score\", \">=\", 2]]}]}],\n"
        "  \"add\": [{\"key\": \"extra\", \"character\": 76, \"image\": 3, \"x\": 1, \"y\": 2, \"talks\": [{\"entry\": 50}]}]}}\n");
    put("bad/stages/103.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"k\", \"character\": 1, \"image\": 2, \"x\": 0, \"y\": 0, "
        "\"talks\": [{\"entry\": 1, \"conditions\": [[\"flag\", \"1A:1\", 1]]}]}]}}\n"); /* the last talk must have no conditions */
    put("bad/stages/104.json", "{\"format\": \"stage-v1\"}\n");
    put("bad/stages/105.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"conditions\": [[\"mod-flag\", \"nocolon\", 1]]}]}}\n");
    put("bad/stages/106.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"conditions\": [[\"mod-var\", \"bad:v\", \"~\", 1]]}]}}\n");
    put("bad/stages/107.json", "{\"format\": \"stage-patch-v1\", \"actors\": {\n");
    put("bad/stages/108.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"key\": \"bad:k\", \"x\": 1}]}}\n");
    put("bad/stages/109.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"add\": [{\"key\": \"Upper Case\", \"character\": 1, \"image\": 2, \"x\": 0, \"y\": 0}]}}\n");
    put("bad/stages/110.json",
        "{\"format\": \"stage-patch-v1\", \"actors\": {\"patch\": [{\"index\": 0, \"dir\": 8}]}}\n");
    put("bad/stages/notes.json", "{\"format\": \"stage-patch-v1\"}\n"); /* not a stage number: not read */

    CHECK(modsInit(root, "a, b, bad") == 3);
    memset(&host, 0, sizeof host);
    CHECK(smLoad(&host) == 3); /* a:100, a:101, b:100 */
    CHECK(smPatchCount() == 3);
    CHECK(smHasPatches(100) && smHasPatches(101));
    CHECK(!smHasPatches(102) && !smHasPatches(103) && !smHasPatches(104) && !smHasPatches(105) && !smHasPatches(106) && !smHasPatches(107) &&
          !smHasPatches(108) && !smHasPatches(109) && !smHasPatches(110));

    /* ---- no patch: nothing moves ---- */
    build(&s);
    {
        SdActor *before = s.actors;

        memset(&rep, 7, sizeof rep);
        CHECK(smApplyActors(999, &s, &rep) == 0);
        CHECK(s.actors == before && s.nactors == 3 && s.actors[0].x == 100);
        CHECK(rep.patched == 0 && rep.added == 0 && rep.removed == 0 && rep.skipped == 0 && rep.conflicts == 0);
    }
    sdStageFree(&s);

    /* ---- a missing image is skipped, the rest of the patch applies ---- */
    build(&s);
    CHECK(smApplyActors(101, &s, &rep) == 1);
    CHECK(rep.skipped == 1 && rep.patched == 1);
    CHECK(s.nactors == 3 && s.actors[1].image == 3 && s.actors[1].x == 5);
    sdStageFree(&s);

    /* ---- a, then b ---- */
    build(&s);
    memset(&rep, 0, sizeof rep);
    CHECK(smApplyActors(100, &s, &rep) == 1);
    /* a: index 0 patched (x, y, dir), index 1 talks replaced, index 9 missing (skipped), guide added, index 2 removed.
       b: index 0 x again (a conflict), a:guide patched (x, image), index 2 gone (skipped), index 1 gets a talk in front, b:extra added. */
    CHECK(s.nactors == 4); /* 0, 1, a:guide, b:extra */
    CHECK(rep.added == 2 && rep.removed == 1 && rep.conflicts == 1 && rep.skipped == 2);
    CHECK(s.actors[0].character == 75 && s.actors[0].x == 999 && s.actors[0].y == 160 && s.actors[0].dir == 5); /* the later mod wins x; y and dir stay a's */
    CHECK(s.actors[1].ntalks == 3 && s.actors[1].talks[0].entry == 23 && s.actors[1].talks[1].entry == 21 && s.actors[1].talks[2].entry == 22);
    CHECK(s.actors[1].talks[0].cond.set && s.actors[1].talks[0].cond.n == 2 && isModCode(s.actors[1].talks[0].cond.w[0]));
    CHECK(!s.actors[1].talks[2].cond.set); /* the last talk still has no conditions */
    CHECK(s.actors[2].character == 75 && s.actors[2].x == 70 && s.actors[2].y == 60 && s.actors[2].image == 3 && s.actors[2].ntalks == 2);
    CHECK(s.actors[2].talks[0].entry == 40 && s.actors[2].talks[1].entry == 41 && s.actors[2].talks[1].actions.n == 4);
    CHECK(s.actors[3].character == 76 && s.actors[3].x == 1 && s.actors[3].y == 2 && s.actors[3].ntalks == 1);
    CHECK(s.actors[0].ntalks == 2 && s.actors[0].talks[0].entry == 10); /* untouched talks are still the original's */
    sdStageFree(&s);

    /* applying again on a fresh stage gives the same: the parsed patches are not consumed */
    build(&s);
    CHECK(smApplyActors(100, &s, &rep) == 1 && s.nactors == 4 && s.actors[0].x == 999);
    sdStageFree(&s);

    /* a stage with fewer actors at this progress: an index that is not there is skipped (the story progress changes the setup) */
    build(&s);
    sdStageFree(&s);
    memset(&s, 0, sizeof s);
    {
        SdActor *a = sdStageAddActor(&s);

        a->character = 75, a->image = 2, a->x = 1, a->y = 1;
        s.nimages = 2;
    }
    CHECK(smApplyActors(100, &s, &rep) == 1);
    CHECK(rep.skipped >= 3 && s.nactors == 3); /* index 1, 2 and 9 are not there; 0, a:guide and b:extra are */
    sdStageFree(&s);

    /* ---- the codes ---- */
    smCodeEnv(&env, NULL);
    {
        const char *text = "{\"l\": [[\"mod-flag\", \"a:met\", 1], [\"mod-var\", \"a:count\", \">=\", 3], [\"set-var\", \"a:count\", 3], "
                           "[\"mod-var\", \"a:count\", \"<\", 1, 0], [\"mod-flag\", \"a:met\", 0]]}";
        ExtJson j;
        const ExtJson *l;
        uint16_t *w = NULL;
        int n = 0;
        char err[200] = "";
        ScBuf b;

        CHECK(extJsonParse((const uint8_t *)text, strlen(text), &j) == 0);
        l = extJsonGet(&j, "l");
        CHECK(scListFromJson(&env, l, &w, &n, err, sizeof err) == 0 && n == 10);
        if (n == 10) {
            unsigned flag = w[0], ge = w[2], set = w[4], lt = w[6], flag0 = w[8];

            CHECK(isModCode(flag) && isModCode(ge) && isModCode(set) && isModCode(lt));
            CHECK(flag == flag0 && w[1] == 1 && w[9] == 0);             /* the same op, the same code */
            CHECK(ge != flag && ge != set && set != lt && ge != lt);     /* different ops, different codes */
            CHECK(w[3] == 1 && w[5] == 3 && w[7] == 0);                  /* mod-var: whether it holds; set-var: the value */

            /* the interpreter's side */
            pcExtVarsReset();
            CHECK(pcModCheck((unsigned short)flag, 1) == 0 && pcModCheck((unsigned short)flag, 0) == 1);
            pcModApply((int)flag, 1);
            CHECK(pcExtVarGet("a:met") == 1 && pcModCheck((unsigned short)flag, 1) == 1 && pcModCheck((unsigned short)flag, 0) == 0);
            CHECK(pcModCheck((unsigned short)ge, 1) == 0 && pcModCheck((unsigned short)ge, 0) == 1); /* a:count is 0 */
            pcModApply((int)set, 3);
            CHECK(pcExtVarGet("a:count") == 3 && pcModCheck((unsigned short)ge, 1) == 1 && pcModCheck((unsigned short)ge, 0) == 0);
            CHECK(pcModCheck((unsigned short)lt, 0) == 1 && pcModCheck((unsigned short)lt, 1) == 0); /* "count < 1" does not hold: the 0 asks for "does not" */
            CHECK(pcModCheck((unsigned short)set, 1) == 1); /* a set-var condition always holds */
            pcModApply((int)ge, 1);                          /* a mod-var action does nothing */
            CHECK(pcExtVarGet("a:count") == 3);
            pcModApply((int)flag, 0);
            CHECK(pcExtVarGet("a:met") == 0);
            CHECK(pcModCheck(0x6200 | 0x1FF, 1) == 0 && pcModCheck(0x6800 | 0x1FF, 0) == 0); /* a code the table never made does not hold, whatever is asked */

            /* back to JSON, with the table and without */
            memset(&b, 0, sizeof b);
            CHECK(scListToJson(&env, w, n, &b) == 0);
            CHECK(strcmp(b.b, "[[\"mod-flag\", \"a:met\", 1], [\"mod-var\", \"a:count\", \">=\", 3], [\"set-var\", \"a:count\", 3], "
                              "[\"mod-var\", \"a:count\", \"<\", 1, 0], [\"mod-flag\", \"a:met\", 0]]") == 0);
            scFree(&b);
            CHECK(scListToJson(NULL, w, n, &b) == 0);
            CHECK(strstr(b.b, "\"raw\"") != NULL && strstr(b.b, "mod-flag") == NULL);
            scFree(&b);
        }
        free(w);
        extJsonFree(&j);
        /* names and shapes that are refused, each with its reason */
        {
            static const char *const bad[] = {
                "[[\"mod-flag\", \"a\", 1]]",                  /* no colon */
                "[[\"mod-flag\", \"A:Met\", 1]]",              /* upper case in the name */
                "[[\"mod-flag\", \"a:met\", 2]]",              /* a flag is 0 or 1 */
                "[[\"mod-var\", \"a:v\", \">=\"]]",            /* no number */
                "[[\"mod-var\", \"a:v\", \"=>\", 1]]",         /* no such comparison */
                "[[\"mod-var\", \"a:v\", \">=\", 1, 2]]",      /* truth is 0 or 1 */
                "[[\"set-var\", \"a:v\", 70000]]",             /* the value is a u16 */
                "[[\"set-var\", 5, 1]]",                       /* the name is a string */
            };
            size_t i;

            for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
                ExtJson q;
                uint16_t *qw = NULL;
                int qn = 0;

                {
                    char wrap[300];

                    snprintf(wrap, sizeof wrap, "{\"l\": %s}", bad[i]);
                    CHECK(extJsonParse((const uint8_t *)wrap, strlen(wrap), &q) == 0);
                    err[0] = '\0';
                    CHECK(scListFromJson(&env, extJsonGet(&q, "l"), &qw, &qn, err, sizeof err) != 0 && err[0] != '\0');
                    free(qw);
                    extJsonFree(&q);
                }
            }
        }
    }

    testSpecialNames();
    testActions();

    /* ---- the variables and their save chunk ---- */
    pcExtVarsReset();
    CHECK(pcExtVarGet("a:met") == 0 && pcExtVarCount() == 0);
    {
        unsigned long need;
        unsigned char *chunk;
        unsigned char again[512];

        CHECK(pcExtStateEncodeVars(NULL, 0) == 0); /* nothing set: no chunk */
        CHECK(pcExtVarSet("a:met", 1) == 0 && pcExtVarSet("a:count", 3) == 0 && pcExtVarSet("gone:thing", 7) == 0 && pcExtVarSet("z:z", 0) == 0);
        CHECK(pcExtVarSet("", 1) == -1 && pcExtVarSet(NULL, 1) == -1);
        CHECK(pcExtVarCount() == 3); /* the zero is the same as unset */
        need = pcExtStateEncodeVars(NULL, 0);
        CHECK(need > 8 && need < sizeof again);
        CHECK(pcExtStateEncodeVars(again, 4) == need); /* too small a buffer: only the size */
        chunk = (unsigned char *)malloc(need);
        CHECK(pcExtStateEncodeVars(chunk, need) == need);
        CHECK(memmem(chunk, need, "gone:thing", 10) != NULL && memmem(chunk, need, "z:z", 3) == NULL);

        pcExtStateReset(); /* a save load starts from nothing... */
        CHECK(pcExtVarCount() == 0);
        CHECK(pcExtStateDecodeVars(chunk, need) == 3); /* ...and gets its variables back, the name no mod defines included */
        CHECK(pcExtVarGet("a:met") == 1 && pcExtVarGet("a:count") == 3 && pcExtVarGet("gone:thing") == 7 && pcExtVarCount() == 3);
        CHECK(pcExtStateEncodeVars(again, sizeof again) == need && memcmp(again, chunk, need) == 0); /* saved again: the unknown name stays */
        pcModApply((int)smOpCode(SC_MOD_FLAG, "a:met", 0, 0), 0); /* a mod's own flag changes... */
        CHECK(pcExtVarGet("a:met") == 0 && pcExtVarGet("gone:thing") == 7); /* ...and the one nothing evaluates is not touched */

        CHECK(pcExtStateDecodeVars(NULL, 0) == 0 && pcExtVarCount() == 0); /* an old save, with no section: nothing */
        CHECK(pcExtStateDecodeVars(chunk, need - 3) == -1 && pcExtVarCount() == 0); /* cut off: damaged, empty */
        chunk[0] = 9;
        CHECK(pcExtStateDecodeVars(chunk, need) == -1 && pcExtVarCount() == 0); /* another version: damaged */
        free(chunk);

        pcExtVarsReset();
        CHECK(pcExtVarSet("a:met", -5) == 0 && pcExtVarGet("a:met") == -5); /* counters can go below zero */
        need = pcExtStateEncodeVars(NULL, 0);
        chunk = (unsigned char *)malloc(need);
        pcExtStateEncodeVars(chunk, need);
        pcExtStateDecodeVars(chunk, need);
        CHECK(pcExtVarGet("a:met") == -5);
        free(chunk);
    }

    smUnload();
    CHECK(smPatchCount() == 0 && !smHasPatches(100));
    testNamedTexts();
    testStageData();
    testSprites();
    {
        char cmd[1100];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        if (system(cmd) != 0) fprintf(stdout, "stagemodtest: could not remove %s\n", root);
    }
    fprintf(stdout, failures ? "stagemodtest: %d check(s) FAILED\n" : "stagemodtest: ok\n", failures);
    return failures ? 1 : 0;
}
