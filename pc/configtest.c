/*
 * Host test of the port's config and mods layer (pc/shim/mods.c, pc/shim/port_config.c, ADR-005;
 * feature stage-banner T2). Runs in a scratch folder under $TMPDIR; no disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed (printed to stderr).
 */
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/mod_tools.h"
#include "shim/mods.h"
#include "shim/port_config.h"
#include "shim/port_log.h"
#include "shim/port_version.h"
#include "shim/table_mods.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "configtest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gRoot[1024];
static char gLogPath[1100];
static int gSavedErr = -1;

static void put(const char *rel, const char *text)
{
    char path[1300], dir[1300], *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        tdMakeDirs(dir);
    }
    f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static char *slurp(const char *rel)
{
    char path[1300];
    uint8_t *d;
    size_t n;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    if (tdReadFile(path, &d, &n)) return NULL;
    return (char *)d;
}

static void wipe(void)
{
    char cmd[1300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'/*", gRoot);
    if (system(cmd)) {}
}

static void logBegin(void)
{
    fflush(stderr);
    gSavedErr = dup(2);
    int fd = open(gLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
}

static char *logEnd(void)
{
    uint8_t *d;
    size_t n;
    fflush(stderr);
    dup2(gSavedErr, 2);
    close(gSavedErr);
    if (tdReadFile(gLogPath, &d, &n)) return strdup("");
    return (char *)d;
}

/* One start: scan the mods, load the configs; returns the captured log (caller frees) */
static char *start(const char *order)
{
    char mods[1300], data[1300];
    snprintf(mods, sizeof mods, "%s/mods", gRoot);
    snprintf(data, sizeof data, "%s/data", gRoot);
    logBegin();
    modsInit(mods, order);
    pcConfigLoad(data);
    return logEnd();
}

static void mod(const char *name, const char *banner, const char *trans)
{
    char rel[200];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    put(rel, "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"1\"}");
    if (banner) {
        snprintf(rel, sizeof rel, "mods/%s/config/stage_banner.json", name);
        put(rel, banner);
    }
    if (trans) {
        snprintf(rel, sizeof rel, "mods/%s/config/transitions.json", name);
        put(rel, trans);
    }
}

#define B "\"format\":\"stage_banner-v1\""
#define T "\"format\":\"transitions-v1\""

static void testDefaults(void)
{
    char *log, *ref, *man;
    wipe();
    log = start(NULL);
    CHECK(pcStageBanner()->typeDelayFrames == 1);
    CHECK(pcStageBanner()->holdFrames == 45);
    CHECK(pcStageBanner()->raisePx == 14);
    CHECK(pcStageBanner()->hiddenBoxes == ((1 << 2) | (1 << 3) | (1 << 4) | (1 << 7)));
    CHECK(pcTransitions()->doorFactor == 3);
    CHECK(pcTransitions()->battleTilesPerFrame == 3);
    CHECK(pcTransitions()->battleWaitFrames == 4);
    CHECK(pcTransitions()->resultsFactor == 3);
    CHECK(pcTransitions()->mapTilesPerFrame == 12);
    CHECK(strstr(log, "[config] stage_banner from default\n") != NULL);
    CHECK(strstr(log, "[config] transitions from default\n") != NULL);
    free(log);
    ref = slurp("data/config/stage_banner.json");
    CHECK(ref != NULL);
    if (ref) {
        CHECK(strstr(ref, "\"format\": \"stage_banner-v1\"") != NULL);
        CHECK(strstr(ref, "\"hold_frames\": 45") != NULL);
        CHECK(strstr(ref, "\"hidden_boxes\": [2, 3, 4, 7]") != NULL);
        CHECK(strstr(ref, "\"notes\"") != NULL);
    }
    CHECK(slurp("data/config/transitions.json") != NULL);
    man = slurp("data/config/manifest.json");
    CHECK(man != NULL && strstr(man, "stage_banner.json") != NULL);
    free(man);
    /* a second start with the pristine copy: still defaults, file untouched */
    log = start(NULL);
    CHECK(strstr(log, "[config] stage_banner from default\n") != NULL);
    free(log);
    {
        char *again = slurp("data/config/stage_banner.json");
        CHECK(ref && again && !strcmp(ref, again));
        free(again);
    }
    free(ref);
}

static void testUserEditedData(void)
{
    char *log, *kept;
    const char *mine = "{" B ",\"hold_frames\":100}\n";
    wipe();
    free(start(NULL)); /* writes the reference copies */
    put("data/config/stage_banner.json", mine);
    log = start(NULL);
    CHECK(pcStageBanner()->holdFrames == 100);
    CHECK(pcStageBanner()->raisePx == 14);
    CHECK(strstr(log, "[config] stage_banner from default+data\n") != NULL);
    free(log);
    kept = slurp("data/config/stage_banner.json");
    CHECK(kept && !strcmp(kept, mine)); /* never overwritten */
    free(kept);
}

static void testMerge(void)
{
    char *log;
    wipe();
    put("data/config/stage_banner.json", "{" B ",\"hold_frames\":100,\"raise_px\":20}");
    mod("a", "{" B ",\"hold_frames\":110,\"type_delay_frames\":2}", NULL);
    mod("b", "{" B ",\"hold_frames\":120}", NULL);
    log = start(NULL); /* alphabetical: a then b, the last wins */
    CHECK(pcStageBanner()->holdFrames == 120);
    CHECK(pcStageBanner()->typeDelayFrames == 2);
    CHECK(pcStageBanner()->raisePx == 20);
    CHECK(strstr(log, "[config] stage_banner from default+data+mods/a+mods/b\n") != NULL);
    CHECK(modsCount() == 2);
    free(log);
    /* settings.ini [mods] order = b, a */
    log = start("b, a");
    CHECK(pcStageBanner()->holdFrames == 110);
    CHECK(strstr(log, "+mods/b+mods/a\n") != NULL);
    free(log);
    /* only the listed mods are active; a missing one is skipped with a log line */
    log = start("ghost, a");
    CHECK(modsCount() == 1 && !strcmp(modsNameOf(0), "a"));
    CHECK(pcStageBanner()->holdFrames == 110);
    CHECK(strstr(log, "\"ghost\"") != NULL);
    free(log);
    /* modding-ux U6: "-name" entries are the mods the player turned off ([mods] disabled) */
    log = start("-a");
    CHECK(modsCount() == 1 && !strcmp(modsNameOf(0), "b")); /* only "-" entries: the rest stay on */
    CHECK(strstr(log, "alphabetical") != NULL);
    {
        int i, seen = 0;
        for (i = 0; i < modsReportCount(); i++) {
            const ModsReport *r = modsReportAt(i);
            if (!strcmp(r->name, "a")) { CHECK(r->status == MODS_REP_DISABLED && r->byUser == 1 && r->position == 0); seen++; }
            if (!strcmp(r->name, "b")) { CHECK(r->status == MODS_REP_ACTIVE && r->byUser == 0); seen++; }
        }
        CHECK(seen == 2);
    }
    free(log);
    log = start("b, a, -b"); /* turned off wins over the order */
    CHECK(modsCount() == 1 && !strcmp(modsNameOf(0), "a"));
    free(log);
    log = start("-ghost, -a, -b"); /* a name that is not installed is ignored, all off */
    CHECK(modsCount() == 0);
    free(log);
    log = start("b, -a");
    CHECK(modsCount() == 1 && !strcmp(modsNameOf(0), "b"));
    free(log);
}

static void testRejections(void)
{
    char *log;
    wipe();
    /* a mod with bad JSON, one of another format, one out of range, one with the wrong type */
    mod("a-badjson", "{" B ",\"hold_frames\":", NULL);
    mod("b-badformat", "{\"format\":\"stage_banner-v2\",\"hold_frames\":99}", NULL);
    mod("c-range", "{" B ",\"hold_frames\":99999,\"raise_px\":7}", NULL);
    mod("d-type", "{" B ",\"hold_frames\":\"long\",\"type_delay_frames\":3}", NULL);
    mod("e-noformat", "{\"hold_frames\":99}", NULL);
    mod("f-fraction", "{" B ",\"type_delay_frames\":2.5}", NULL);
    mod("g-boxes", "{" B ",\"hidden_boxes\":[1,11]}", NULL);
    put("data/config/stage_banner.json", "{" B ",\"hold_frames\":100}");
    log = start(NULL);
    CHECK(pcStageBanner()->holdFrames == 100); /* every bad mod fell back to the data file */
    CHECK(pcStageBanner()->raisePx == 7);      /* the good key of a half-bad file still applies */
    CHECK(pcStageBanner()->typeDelayFrames == 3);
    CHECK(pcStageBanner()->hiddenBoxes == ((1 << 2) | (1 << 3) | (1 << 4) | (1 << 7)));
    CHECK(strstr(log, "[config] ") != NULL && strstr(log, "a-badjson/config/stage_banner.json rejected:") != NULL);
    CHECK(strstr(log, "b-badformat/config/stage_banner.json rejected: unknown format") != NULL);
    CHECK(strstr(log, "key \"hold_frames\" rejected: 99999 is outside 0 to 600") != NULL);
    CHECK(strstr(log, "key \"hold_frames\" rejected: not a number") != NULL);
    CHECK(strstr(log, "e-noformat/config/stage_banner.json rejected: no \"format\"") != NULL);
    CHECK(strstr(log, "key \"type_delay_frames\" rejected: not a whole number") != NULL);
    CHECK(strstr(log, "key \"hidden_boxes\" rejected: index 11") != NULL);
    free(log);

    /* a data file that is not JSON: defaults */
    wipe();
    free(start(NULL));
    put("data/config/stage_banner.json", "not json at all");
    log = start(NULL);
    CHECK(pcStageBanner()->holdFrames == 45);
    CHECK(strstr(log, "data/config/stage_banner.json rejected:") != NULL);
    CHECK(strstr(log, "[config] stage_banner from default\n") != NULL);
    free(log);

    /* a mod folder with a bad mod.json is skipped and logged */
    wipe();
    put("mods/broken/mod.json", "{\"format\":\"mod-v9\"}");
    put("mods/broken/config/stage_banner.json", "{" B ",\"hold_frames\":77}");
    put("mods/nomanifest/config/stage_banner.json", "{" B ",\"hold_frames\":66}");
    log = start(NULL);
    CHECK(modsCount() == 0);
    CHECK(pcStageBanner()->holdFrames == 45);
    CHECK(strstr(log, "mods/broken rejected: mod.json: unknown format") != NULL);
    CHECK(strstr(log, "mods/nomanifest rejected: no readable mod.json") != NULL);
    free(log);
}

static void testTransitions(void)
{
    char *log;
    wipe();
    mod("t", NULL, "{" T ",\"door_factor\":2,\"battle_tiles_per_frame\":5,\"battle_wait_frames\":0,\"results_factor\":1,\"map_tiles_per_frame\":4}");
    log = start(NULL);
    CHECK(pcTransitions()->doorFactor == 2);
    CHECK(pcTransitions()->battleTilesPerFrame == 5);
    CHECK(pcTransitions()->battleWaitFrames == 4); /* 0 is outside 1 to 16 */
    CHECK(pcTransitions()->resultsFactor == 1);
    CHECK(pcTransitions()->mapTilesPerFrame == 4);
    CHECK(strstr(log, "[config] transitions from default+mods/t\n") != NULL);
    CHECK(strstr(log, "key \"battle_wait_frames\" rejected") != NULL);
    free(log);
    /* the example boxes list */
    wipe();
    mod("boxes", "{" B ",\"hidden_boxes\":[0,1]}", NULL);
    free(start(NULL));
    CHECK(pcStageBanner()->hiddenBoxes == 3);
}


/* ---- the manifest (ADR-006 section 5): requires, port, kind, version; load order; rejections ---- */

/* mods/<name>/mod.json with the given version and extra members ("" or ",\"requires\":[...]") */
static void modm(const char *name, const char *version, const char *extra)
{
    char rel[200], json[12000];
    snprintf(rel, sizeof rel, "mods/%s/mod.json", name);
    snprintf(json, sizeof json, "{\"format\":\"mod-v1\",\"name\":\"x\",\"version\":\"%s\"%s}", version, extra);
    put(rel, json);
}

static int indexOfMod(const char *name)
{
    int i;
    for (i = 0; i < modsCount(); i++) {
        if (!strcmp(modsNameOf(i), name)) return i;
    }
    return -1;
}

static void testManifest(void)
{
    char *log;

    CHECK(strcmp(PORT_VERSION, "") != 0);

    /* dependencies load before their dependents, even against the alphabetical order */
    wipe();
    modm("a-user", "1.0", ",\"requires\":[\"base>=1.2\"]");
    modm("base", "1.3", "");
    modm("c-free", "2", "");
    log = start(NULL);
    CHECK(modsCount() == 3);
    CHECK(indexOfMod("base") >= 0 && indexOfMod("base") < indexOfMod("a-user"));
    CHECK(!strcmp(modsNameOf(0), "base") && !strcmp(modsNameOf(1), "a-user") && !strcmp(modsNameOf(2), "c-free"));
    CHECK(!strcmp(modsVersionOf(indexOfMod("base")), "1.3"));
    free(log);

    /* the settings.ini order is stable too: a, base -> base, a */
    log = start("a-user, base");
    CHECK(modsCount() == 2 && !strcmp(modsNameOf(0), "base") && !strcmp(modsNameOf(1), "a-user"));
    free(log);

    /* too old, missing, not listed in settings.ini: the dependent is disabled with a reason */
    wipe();
    modm("old-dep", "1.1", "");
    modm("needs-new", "1", ",\"requires\":[\"old-dep>=1.2\"]");
    modm("needs-ghost", "1", ",\"requires\":[\"ghost\"]");
    modm("cascade", "1", ",\"requires\":[\"needs-ghost\"]");
    log = start(NULL);
    CHECK(modsCount() == 1 && !strcmp(modsNameOf(0), "old-dep"));
    CHECK(strstr(log, "[mods] needs-new disabled: requires old-dep>=1.2, found 1.1\n") != NULL);
    CHECK(strstr(log, "[mods] needs-ghost disabled: requires ghost, not installed or not active\n") != NULL);
    CHECK(strstr(log, "[mods] cascade disabled: requires needs-ghost, which is disabled\n") != NULL);
    free(log);
    log = start("needs-new");
    CHECK(modsCount() == 0);
    CHECK(strstr(log, "[mods] needs-new disabled: requires old-dep>=1.2, not installed or not active\n") != NULL);
    free(log);

    /* a cycle disables its members and what hangs from it; the rest loads */
    wipe();
    modm("x", "1", ",\"requires\":[\"y\"]");
    modm("y", "1", ",\"requires\":[\"x\"]");
    modm("z", "1", "");
    modm("w", "1", ",\"requires\":[\"x\"]");
    modm("self", "1", ",\"requires\":[\"self\"]");
    log = start(NULL);
    CHECK(modsCount() == 1 && !strcmp(modsNameOf(0), "z"));
    CHECK(strstr(log, "[mods] dependency cycle, disabled:") != NULL);
    CHECK(strstr(log, " x") != NULL && strstr(log, " y") != NULL && strstr(log, " self") != NULL);
    free(log);

    /* "port": the minimum version of this build */
    wipe();
    modm("port-ok", "1", ",\"port\":\">=0.4\"");
    modm("port-future", "1", ",\"port\":\">=99.0\"");
    modm("port-exact", "1", ",\"port\":\">=" PORT_VERSION "\"");
    log = start(NULL);
    CHECK(indexOfMod("port-ok") >= 0 && indexOfMod("port-exact") >= 0 && indexOfMod("port-future") < 0);
    CHECK(strstr(log, "[mods] port-future disabled: needs port >=99.0, this is " PORT_VERSION "\n") != NULL);
    free(log);

    /* "kind" and a bad manifest */
    wipe();
    modm("k-assets", "1", ",\"kind\":\"assets\"");
    modm("k-content", "1", ",\"kind\":\"content\"");
    modm("k-bad", "1", ",\"kind\":\"weird\"");
    modm("req-bad", "1", ",\"requires\":[\"a>>1\"]");
    modm("req-type", "1", ",\"requires\":\"base\"");
    modm("port-bad", "1", ",\"port\":\"new\"");
    modm("ver-bad", "one.two", "");
    modm("future-key", "1", ",\"tags\":[1,2],\"homepage\":\"x\"");
    log = start(NULL);
    CHECK(modsCount() == 3);
    CHECK(!strcmp(modsKindOf(indexOfMod("k-assets")), "assets"));
    CHECK(!strcmp(modsKindOf(indexOfMod("k-content")), "content"));
    CHECK(!strcmp(modsKindOf(indexOfMod("future-key")), "assets")); /* the default */
    CHECK(strstr(log, "k-bad rejected: mod.json: unknown kind \"weird\"") != NULL);
    CHECK(strstr(log, "req-bad rejected: mod.json: bad \"requires\" entry \"a>>1\"") != NULL);
    CHECK(strstr(log, "req-type rejected: mod.json: \"requires\" must be a list of strings") != NULL);
    CHECK(strstr(log, "port-bad rejected: mod.json: bad \"port\" requirement \"new\"") != NULL);
    CHECK(strstr(log, "ver-bad rejected: mod.json: bad \"version\" \"one.two\"") != NULL);
    free(log);
}


/* ---- the limits (U1): many mods, many members, long arrays; an excess is logged, never dropped silently ---- */

static void testLimits(void)
{
    char *log, extra[8192], name[32];
    int i;

    /* 200 mods: all active (the scan used to stop at 32) */
    wipe();
    for (i = 0; i < 200; i++) {
        snprintf(name, sizeof name, "m%03d", i);
        modm(name, "1", "");
    }
    log = start(NULL);
    CHECK(modsCount() == 200 && !strcmp(modsNameOf(0), "m000") && !strcmp(modsNameOf(199), "m199"));
    CHECK(strstr(log, "more than") == NULL);
    free(log);

    /* past the engine's roof: the excess is rejected with a log line each */
    wipe();
    for (i = 0; i < MODS_MAX + 5; i++) {
        snprintf(name, sizeof name, "m%03d", i);
        modm(name, "1", "");
    }
    log = start(NULL);
    CHECK(modsCount() == MODS_MAX);
    {
        const char *p = log;
        int lines = 0;
        while ((p = strstr(p, "rejected: more than")) != NULL) {
            lines++;
            p++;
        }
        CHECK(lines == 5);
    }
    free(log);

    /* a manifest with 60 members (future keys are ignored) and a 50-number array is read; 400 members are rejected loudly */
    wipe();
    extra[0] = 0;
    for (i = 0; i < 60; i++) snprintf(extra + strlen(extra), sizeof extra - strlen(extra), ",\"k%d\":%d", i, i);
    snprintf(extra + strlen(extra), sizeof extra - strlen(extra), ",\"tags\":[");
    for (i = 0; i < 50; i++) snprintf(extra + strlen(extra), sizeof extra - strlen(extra), "%s%d", i ? "," : "", i);
    snprintf(extra + strlen(extra), sizeof extra - strlen(extra), "]");
    modm("wide", "1", extra);
    extra[0] = 0;
    for (i = 0; i < 400; i++) snprintf(extra + strlen(extra), sizeof extra - strlen(extra), ",\"k%d\":%d", i, i);
    modm("toowide", "1", extra);
    extra[0] = 0;
    snprintf(extra, sizeof extra, ",\"tags\":[");
    for (i = 0; i < 500; i++) snprintf(extra + strlen(extra), sizeof extra - strlen(extra), "%s%d", i ? "," : "", i);
    snprintf(extra + strlen(extra), sizeof extra - strlen(extra), "]");
    modm("toolong", "1", extra);
    log = start(NULL);
    CHECK(modsCount() == 1 && !strcmp(modsNameOf(0), "wide"));
    CHECK(strstr(log, "toowide rejected: mod.json: too many keys") != NULL);
    CHECK(strstr(log, "toolong rejected: mod.json: array too long") != NULL);
    free(log);
}


/* ---- U5: the mod tools ---- */

static char *readAll(FILE *f)
{
    long n;
    char *t;
    fflush(f);
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    t = (char *)calloc((size_t)n + 1, 1);
    if (t && n > 0 && fread(t, 1, (size_t)n, f)) {}
    return t;
}

static int exists(const char *rel)
{
    char path[1300];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", gRoot, rel);
    return stat(path, &st) == 0;
}

static void testToolArgs(void)
{
    ModToolsCmd c;
    char *argv1[] = {"boot", "disc.bin", "--validate-mod", "demo", "300", NULL};
    char *argv2[] = {"boot", "--new-mod", "x", "--kind", "content,tables", NULL};
    char *argv3[] = {"boot", "--list-mods", NULL};
    char *argv4[] = {"boot", "--new-mod", NULL};
    char *argv5[] = {"boot", "--kind", "content", NULL};
    char *argv6[] = {"boot", "disc.bin", "100", NULL};
    char *argv7[] = {"boot", "--validate-mods", "disc.bin", NULL};
    char root[300], name[300];
    int argc;

    argc = 5;
    CHECK(modToolsTakeArgs(&argc, argv1, &c) == 1 && c.kind == MT_VALIDATE && !strcmp(c.name, "demo") && !c.err[0]);
    CHECK(argc == 3 && !strcmp(argv1[1], "disc.bin") && !strcmp(argv1[2], "300") && argv1[3] == NULL);
    argc = 5;
    CHECK(modToolsTakeArgs(&argc, argv2, &c) == 1 && c.kind == MT_NEW && !strcmp(c.name, "x") && !strcmp(c.kinds, "content,tables") && argc == 1);
    argc = 2;
    CHECK(modToolsTakeArgs(&argc, argv3, &c) == 1 && c.kind == MT_LIST && argc == 1);
    argc = 2;
    CHECK(modToolsTakeArgs(&argc, argv4, &c) == 1 && c.err[0]); /* no name */
    argc = 3;
    CHECK(modToolsTakeArgs(&argc, argv5, &c) == 1 && c.err[0]); /* --kind alone */
    argc = 3;
    CHECK(modToolsTakeArgs(&argc, argv6, &c) == 0 && argc == 3 && c.kind == MT_NONE); /* a plain boot is untouched */
    argc = 3;
    CHECK(modToolsTakeArgs(&argc, argv7, &c) == 1 && c.kind == MT_VALIDATE_ALL && argc == 2 && !strcmp(argv7[1], "disc.bin"));

    CHECK(modToolsSplitTarget("demo", root, sizeof root, name, sizeof name) == 0 && !root[0] && !strcmp(name, "demo"));
    CHECK(modToolsSplitTarget("/a/b/demo", root, sizeof root, name, sizeof name) == 1 && !strcmp(root, "/a/b") && !strcmp(name, "demo"));
    CHECK(modToolsSplitTarget("/a/b/demo/", root, sizeof root, name, sizeof name) == 1 && !strcmp(root, "/a/b") && !strcmp(name, "demo"));
    CHECK(modToolsSplitTarget("rel/demo", root, sizeof root, name, sizeof name) == 1 && !strcmp(root, "rel") && !strcmp(name, "demo"));
}

static void testNewMod(void)
{
    char mods[1300];
    FILE *out = tmpfile(), *err = tmpfile();
    char *txt, *json;
    int i;

    wipe();
    snprintf(mods, sizeof mods, "%s/mods", gRoot);
    tdMakeDirs(mods);
    CHECK(modToolsNew(mods, "demo", "", out, err) == 0);
    CHECK(exists("mods/demo/mod.json") && exists("mods/demo/README.txt"));
    CHECK(exists("mods/demo/tables") && exists("mods/demo/images") && exists("mods/demo/files") && !exists("mods/demo/content"));
    json = slurp("mods/demo/mod.json");
    CHECK(json && strstr(json, "\"name\": \"demo\"") && strstr(json, "\"kind\": \"assets\"") && strstr(json, "\"format\": \"mod-v1\""));
    free(json);
    /* the new mod is a valid one: the real scan accepts it */
    logBegin();
    CHECK(modsInit(mods, NULL) == 1);
    txt = logEnd();
    CHECK(!strstr(txt, "rejected"));
    free(txt);

    /* never overwrites: the mod.json the author edited stays */
    put("mods/demo/mod.json", "{\"format\":\"mod-v1\",\"name\":\"mine\"}");
    CHECK(modToolsNew(mods, "demo", "", out, err) == 1);
    json = slurp("mods/demo/mod.json");
    CHECK(json && strstr(json, "mine"));
    free(json);
    txt = readAll(err);
    CHECK(txt && strstr(txt, "already exists"));
    free(txt);
    CHECK(modToolsNew(mods, "Demo", "", out, err) == 1 || exists("mods/Demo")); /* a case-insensitive disk refuses the clash; a case-sensitive one is told */
    txt = readAll(err);
    CHECK(txt && (strstr(txt, "only in case") || strstr(txt, "already exists")));
    free(txt);

    /* kinds: content makes content/ and tables/ and writes "kind": "content"; a folder name makes only that folder */
    CHECK(modToolsNew(mods, "stuff", "content", out, err) == 0);
    json = slurp("mods/stuff/mod.json");
    CHECK(json && strstr(json, "\"kind\": \"content\""));
    free(json);
    CHECK(exists("mods/stuff/content") && exists("mods/stuff/tables") && !exists("mods/stuff/images"));
    CHECK(modToolsNew(mods, "only-tables", "tables", out, err) == 0);
    CHECK(exists("mods/only-tables/tables") && !exists("mods/only-tables/content") && !exists("mods/only-tables/images") && !exists("mods/only-tables/files"));

    /* bad input leaves nothing behind */
    CHECK(modToolsNew(mods, "bad name", "", out, err) == 1 && !exists("mods/bad name"));
    CHECK(modToolsNew(mods, "../up", "", out, err) == 1);
    CHECK(modToolsNew(mods, ".hidden", "", out, err) == 1 && !exists("mods/.hidden"));
    CHECK(modToolsNew(mods, "odd-kind", "sprites", out, err) == 1 && !exists("mods/odd-kind"));
    CHECK(modToolsNew(mods, "no-root", "", out, err) == 0);
    CHECK(modToolsNew("", "x", "", out, err) == 1);

    /* the template folder shipped in the repository holds the same mod.json and README.txt (with the name filled in) */
    for (i = 0;; i++) {
        const char *text, *file = modToolsTemplateFile(i, &text);
        char path[300], *disk, *want, *at;
        uint8_t *d;
        size_t n;
        if (!file) break;
        snprintf(path, sizeof path, "examples/mods/_template/%s", file);
        if (tdReadFile(path, &d, &n)) {
            CHECK(!"examples/mods/_template/ has the file");
            continue;
        }
        disk = (char *)d;
        want = strdup(text);
        while ((at = strstr(want, "@NAME@")) != NULL) {
            memmove(at + 9, at + 6, strlen(at + 6) + 1);
            memcpy(at, "_template", 9);
        }
        while ((at = strstr(want, "@KIND@")) != NULL) {
            memmove(at + 7, at + 6, strlen(at + 6) + 1);
            memcpy(at, "content", 7);
        }
        CHECK(!strcmp(disk, want));
        free(want);
        free(disk);
    }
    fclose(out);
    fclose(err);
    wipe();
}

/* a table of the "game" for the validator: the real engine parses the patches of the mods */
typedef struct TRow { int money; int exp; } TRow;
static TRow gTRows[3] = {{10, 1}, {20, 2}, {30, 3}};
static const TmField kTFields[] = {
    {"money", TM_S32, offsetof(TRow, money), 0, 100, "money"},
    {"exp", TM_S32, offsetof(TRow, exp), 0, 50, "exp"},
};
static const TmTable kTTable = {"battle_rewards", gTRows, sizeof(TRow), 3, 0, "the row", "next battle", kTFields, 2};
static int gDiscSaysYes;

static void gameLayers(void *ud)
{
    (void)ud;
    gTRows[0].money = 10;
    tmInit(NULL);
    tmRegister(&kTTable);
    tmApply("battle_rewards");
    /* the images layer of the real game is checked on the disc (image_data.c); here a loader's refusal is a line in the log like any other */
    if (exists("mods/oversize/images/SCREEN/X/00_00.png"))
        plogLine("images", "%s/mods/oversize/images/SCREEN/X/00_00.png rejected: 40x40, the original is 32x32\n", gRoot);
    /* P8: digimon_ext.c says one line for each motion slot a baked model leaves empty */
    if (exists("mods/slotmod/mod.json")) {
        plogLine("digimon", "slotmod:pal: motion slot 3 (flinch) has no animation: a hit of an attack that lands (effect script 1)\n");
        plogLine("digimon", "slotmod:pal: motion slot 13 (blast entrance) has no animation: the temporary form coming in\n");
    }
}

static int discHas(void *ud, const char *path)
{
    (void)ud;
    return gDiscSaysYes && !strcmp(path, "AAA/DAT/Z_STAGE/S330PACK.BIN");
}

/* the disc of the test has two original sprites: p001play and the follower of a partner (A7.5) */
static int spriteKind(void *ud, const char *folder)
{
    (void)ud;
    return !strcmp(folder, "p001play") ? 1 : !strcmp(folder, "p006agum") ? 2 : 0;
}

/* the disc of the test has two original partners with folders: agumon and gabumon (A8.2) */
static int partnerOriginal(void *ud, const char *name)
{
    (void)ud;
    return !strcmp(name, "agumon") || !strcmp(name, "gabumon");
}

/* the gym of the test has two trainings, punch and defense; the user changed the files of data/partners/agumon (A8.4) */
static int partnerTraining(void *ud, const char *name)
{
    (void)ud;
    return !strcmp(name, "punch") || !strcmp(name, "defense");
}

static int partnerDataEdited(void *ud, const char *key)
{
    (void)ud;
    return !strcmp(key, "agumon");
}

static char *validate(const char *target, int *rc)
{
    char mods[1300];
    ModToolsLayers L = {gameLayers, discHas, "config,content,digimon,tables,images,sprites,partners", NULL, spriteKind, partnerOriginal, partnerTraining, partnerDataEdited};
    FILE *out = tmpfile();
    char *txt;
    snprintf(mods, sizeof mods, "%s/mods", gRoot);
    modToolsCaptureBegin();
    modsInit(mods, NULL);
    *rc = modToolsValidate(target, &L, out);
    txt = readAll(out);
    fclose(out);
    return txt;
}

static void testListAndValidate(void)
{
    char *txt, *err;
    FILE *out = tmpfile();
    int rc;
    char mods[1300];
    wipe();
    snprintf(mods, sizeof mods, "%s/mods", gRoot);

    put("mods/good/mod.json", "{\"format\":\"mod-v1\",\"name\":\"Good\",\"version\":\"1.2\",\"kind\":\"assets\"}");
    put("mods/good/tables/battle_rewards.json", "{\"format\":\"battle_rewards-v1\",\"rows\":{\"1\":{\"money\":50}}}");
    put("mods/good/tools/make_art.py", "print(1)\n"); /* an author's script: no \"never read\" warning */
    put("mods/broken/mod.json", "{\"format\":\"mod-v1\",\"name\":");
    put("mods/needy/mod.json", "{\"format\":\"mod-v1\",\"requires\":[\"nobody\"]}");
    put("mods/badrow/mod.json", "{\"format\":\"mod-v1\",\"kind\":\"content\",\"nmae\":\"typo\"}");
    put("mods/badrow/tables/battle_rewards.json", "{\"format\":\"battle_rewards-v1\",\"rows\":{\"2\":{\"money\":9999},\"1\":{\"exp\":\"*2\"}}}");
    put("mods/badrow/tables/battle_reward.json", "{\"format\":\"battle_rewards-v1\",\"rows\":{}}");
    put("mods/badrow/content/item.json", "{}");
    put("mods/badrow/table/x.json", "{}");
    put("mods/badrow/files/dat/z_stage/s330pack.bin", "x");
    put("mods/badrow/files/dat/nothing.bin", "x");
    put("mods/oversize/mod.json", "{\"format\":\"mod-v1\"}");
    put("mods/oversize/images/SCREEN/X/00_00.png", "not a png");
    put("mods/oversize/text/eng/a.json", "{}");
    put("mods/assetsmod/mod.json", "{\"format\":\"mod-v1\"}");
    put("mods/assetsmod/content/items.json", "{}");
    put("mods/assetsmod/sprites/p001play/000.png", "x");      /* an original's frame: nothing to say */
    put("mods/assetsmod/sprites/p001play/sprite.json", "{}"); /* not read in an original's folder */
    put("mods/assetsmod/sprites/p001play/notes.txt", "x");    /* not a frame */
    put("mods/assetsmod/sprites/p006agum/000.png", "x");      /* a partner's follower lives in partners/ */
    put("mods/assetsmod/sprites/p002fake/sprite.json", "{}"); /* looks like an original's name, the disc has none: a new sprite */
    put("mods/assetsmod/sprites/fox/sprite.json", "{}");      /* a new sprite: nothing to say here */
    put("mods/assetsmod/sprites/stray.txt", "x");
    put("mods/assetsmod/images/field/sprt/p001play/000.png", "x"); /* the route of before A7.5: never read */
    put("mods/assetsmod/cards/c012/face.png", "x");      /* an original's art: nothing to say */
    put("mods/assetsmod/cards/c013/card.json", "{}");    /* an original's folder: its card.json is never read; no face.png */
    put("mods/assetsmod/cards/bee/card.json", "{}");     /* a new card in a mod that is not "content" */
    put("mods/assetsmod/cards/bee/face.png", "x");
    put("mods/assetsmod/cards/artonly/face.png", "x");   /* no card.json: not a card */
    put("mods/assetsmod/cards/stray.txt", "x");
    put("mods/assetsmod/content/cards.json", "{}");      /* the registry of before A8.1: never read */
    put("mods/assetsmod/partners/agumon/icon.png", "x"); /* an original partner's art: nothing to say */
    put("mods/assetsmod/partners/gabumon/partner.json", "{}"); /* an original's folder: its partner.json is never read */
    put("mods/assetsmod/partners/bee/partner.json", "{}");     /* a new partner in a mod that is not "content" */
    put("mods/assetsmod/partners/bee/portrait.png", "x");
    put("mods/assetsmod/partners/artonly/portrait.png", "x");  /* no partner.json: art only, nothing to say here (A8.4) */
    put("mods/assetsmod/content/partners.json", "{}");   /* the registry of before A8.2: never read */
    put("mods/assetsmod/content/digimon.json", "{}");    /* the registry of before A8.3: never read */
    put("mods/assetsmod/content/models/robot/model.json", "{}"); /* the models of before A8.3: never read */
    put("mods/assetsmod/content/encounters.json", "{}"); /* the placements: a content file, nothing to say */
    put("mods/assetsmod/digimon/d012/model/model.json", "{}"); /* an original's folder: its model/ is never read */
    put("mods/assetsmod/digimon/d012/digimon.json", "{}");     /* a change of an original in a mod that is not "content" */
    put("mods/assetsmod/digimon/bee/digimon.json", "{}");      /* a new Digimon in a mod that is not "content" */
    put("mods/assetsmod/digimon/bee/model/model.json", "{}");  /* its model: nothing to say */
    put("mods/assetsmod/digimon/modelonly/model/model.json", "{}"); /* a model source with no Digimon: nothing to say */
    put("mods/assetsmod/digimon/empty/readme.txt", "x");       /* neither a record nor a model */
    put("mods/assetsmod/digimon/nomodel/model/model.png", "x"); /* a model/ with no model.json */
    put("mods/assetsmod/digimon/stray.txt", "x");              /* a file straight in digimon/ */

    /* --list-mods */
    logBegin();
    modsInit(mods, NULL);
    free(logEnd());
    modToolsList(mods, out);
    txt = readAll(out);
    CHECK(strstr(txt, "good") && strstr(txt, "1.2") && strstr(txt, "enabled"));
    CHECK(strstr(txt, "broken") && strstr(txt, "rejected: mod.json"));
    CHECK(strstr(txt, "needy") && strstr(txt, "disabled: requires nobody"));
    CHECK(strstr(txt, "6 mods") && strstr(txt, "4 enabled"));
    free(txt);
    {
        FILE *none = tmpfile();
        char empty[1300];
        snprintf(empty, sizeof empty, "%s/empty", gRoot);
        tdMakeDirs(empty);
        logBegin();
        modsInit(empty, NULL);
        free(logEnd());
        modToolsList(empty, none);
        txt = readAll(none);
        CHECK(strstr(txt, "no mods"));
        free(txt);
        fclose(none);
    }

    /* --validate-mods */
    gDiscSaysYes = 1;
    txt = validate(NULL, &rc);
    CHECK(rc == 1);
    CHECK(strstr(txt, "validating 6 mods"));
    CHECK(strstr(txt, "mod good (1.2, assets)"));
    CHECK(strstr(txt, "mod.json: ")); /* broken */
    CHECK(strstr(txt, "mod is disabled: requires nobody")); /* needy */
    CHECK(strstr(txt, "tables/battle_rewards.json row \"2\" field \"money\" rejected")); /* the real parser's refusal, with file and key */
    CHECK(strstr(txt, "mod.json key \"nmae\" is ignored (did you mean \"name\"?)"));
    CHECK(strstr(txt, "tables/battle_reward.json: there is no table called \"battle_reward\" (did you mean \"battle_rewards\"?)"));
    CHECK(strstr(txt, "content/item.json is ignored (did you mean content/items.json?)"));
    CHECK(strstr(txt, "folder table/ is never read (did you mean tables/?)"));
    CHECK(!strstr(txt, "folder tools/"));
    CHECK(strstr(txt, "files/dat/nothing.bin: no such file on the disc (looked for AAA/DAT/NOTHING.BIN)"));
    CHECK(!strstr(txt, "files/dat/z_stage/s330pack.bin: no such")); /* the one the "disc" has */
    CHECK(strstr(txt, "images/SCREEN/X/00_00.png") && strstr(txt, "40x40")); /* oversize image, attributed to its mod */
    CHECK(strstr(txt, "text/ not checked"));
    CHECK(strstr(txt, "content/ is ignored: this mod's kind is \"assets\""));
    CHECK(strstr(txt, "sprites/p001play/sprite.json is ignored: p001play is an original sprite"));
    CHECK(strstr(txt, "sprites/p001play/notes.txt is ignored (a frame of an original sprite is NNN.png"));
    CHECK(!strstr(txt, "sprites/p001play/000.png is") && !strstr(txt, "sprites/fox"));
    CHECK(strstr(txt, "sprites/p006agum is ignored: that is the follower of an original partner"));
    CHECK(strstr(txt, "sprites/p002fake is read as a new sprite: the disc has no original sprite with that name"));
    CHECK(strstr(txt, "sprites/stray.txt is ignored (a sprite is a folder"));
    CHECK(strstr(txt, "folder images/field/sprt/ is never read (the frames of an original sprite are sprites/<name>/NNN.png"));
    CHECK(strstr(txt, "cards/c013/card.json is never read (an original card's folder only replaces its face.png"));
    CHECK(strstr(txt, "cards/c013 has no face.png (it is ignored)"));
    CHECK(strstr(txt, "cards/bee/card.json is ignored: this mod's kind is \"assets\""));
    CHECK(strstr(txt, "cards/artonly is ignored: it has no card.json"));
    CHECK(strstr(txt, "cards/stray.txt is ignored (a card is a folder"));
    CHECK(strstr(txt, "content/cards.json is never read (a card is cards/<card>/card.json and face.png"));
    CHECK(strstr(txt, "partners/gabumon/partner.json is never read (an original partner's folder only replaces its art"));
    CHECK(strstr(txt, "partners/bee/partner.json is ignored: this mod's kind is \"assets\""));
    CHECK(strstr(txt, "content/partners.json is never read (a partner is partners/<partner>/partner.json beside its art"));
    CHECK(!strstr(txt, "partners/ not checked") && !strstr(txt, "partners/agumon/icon.png"));
    CHECK(strstr(txt, "partners/artonly is ignored: it is not an original partner's folder and no active mod has a partners/artonly/partner.json"));
    CHECK(!strstr(txt, "content/partners.json is ignored"));
    CHECK(strstr(txt, "content/digimon.json is never read (a Digimon is digimon/<digimon>/digimon.json and model/"));
    CHECK(strstr(txt, "content/models/ is never read (a model is digimon/<digimon>/model/"));
    CHECK(strstr(txt, "digimon/d012/model/ is never read (no mod replaces an original's model"));
    CHECK(strstr(txt, "digimon/d012/digimon.json is ignored: this mod's kind is \"assets\""));
    CHECK(strstr(txt, "digimon/bee/digimon.json is ignored: this mod's kind is \"assets\""));
    CHECK(strstr(txt, "digimon/empty is ignored: it has no digimon.json and no model/"));
    CHECK(strstr(txt, "digimon/nomodel/model/ has no model.json (it is ignored)"));
    CHECK(strstr(txt, "digimon/stray.txt is ignored (a Digimon is a folder"));
    CHECK(!strstr(txt, "digimon/modelonly") && !strstr(txt, "digimon/bee/model") && !strstr(txt, "encounters.json is ignored") && !strstr(txt, "digimon/ not checked"));
    CHECK(!strstr(txt, "cards/c012 ") && !strstr(txt, "cards/c012/card") && !strstr(txt, "cards/bee/face.png"));
    CHECK(strstr(txt, "summary: 6 mods"));
    CHECK(strstr(txt, "ok\n") != NULL); /* the good mod */
    free(txt);
    /* the good mod's patch really applied: the table engine ran for real */
    CHECK(gTRows[1].money == 50 || gTRows[1].exp == 4);

    /* one mod: only its lines, exit code from its own result */
    txt = validate("good", &rc);
    CHECK(rc == 0 && strstr(txt, "validating 1 mod") && !strstr(txt, "badrow") && strstr(txt, "ok"));
    free(txt);
    txt = validate("badrow", &rc);
    CHECK(rc == 1 && strstr(txt, "validating 1 mod") && strstr(txt, "money") && !strstr(txt, "oversize"));
    free(txt);
    txt = validate("nosuch", &rc);
    CHECK(rc == 2 && strstr(txt, "no mod called \"nosuch\""));
    free(txt);
    /* without the disc the files/ layer is reported as not checked, never guessed */
    gDiscSaysYes = 0;
    {
        char mods2[1300];
        ModToolsLayers L = {gameLayers, NULL, "config,content,tables,images", NULL};
        FILE *o2 = tmpfile();
        snprintf(mods2, sizeof mods2, "%s/mods", gRoot);
        modToolsCaptureBegin();
        modsInit(mods2, NULL);
        rc = modToolsValidate("badrow", &L, o2);
        txt = readAll(o2);
        CHECK(strstr(txt, "files/ not checked") && !strstr(txt, "no such file on the disc"));
        free(txt);
        fclose(o2);
    }
    (void)err;
    fclose(out);
    wipe();
}

/* A8.4: what --validate-mod says of a mod's partners/ folders besides what the loaders say (their lines come from the game: art sizes, partner.json, training.json) */
static int countOf(const char *txt, const char *what)
{
    int n = 0;
    const char *p = txt;
    while ((p = strstr(p, what)) != NULL) {
        n++;
        p += strlen(what);
    }
    return n;
}

/* P8: a model with empty slots is a warning line each (never an error), owned by the mod that the model's name starts with */
static void testValidateEmptySlots(void)
{
    char *txt;
    int rc;

    wipe();
    put("mods/slotmod/mod.json", "{\"format\":\"mod-v1\",\"kind\":\"content\"}");
    txt = validate("slotmod", &rc);
    CHECK(rc == 0 && countOf(txt, "  warning slotmod:pal: motion slot ") == 2 && !strstr(txt, "  error"));
    CHECK(strstr(txt, "warning slotmod:pal: motion slot 3 (flinch) has no animation: ") && strstr(txt, "warning slotmod:pal: motion slot 13 (blast entrance) has no animation: "));
    CHECK(strstr(txt, "0 errors, 2 warnings"));
    free(txt);
    wipe();
}

static void testValidatePartners(void)
{
    char *txt;
    int rc;

    wipe();
    put("mods/p1/mod.json", "{\"format\":\"mod-v1\",\"kind\":\"content\"}");
    /* a good new partner */
    put("mods/p1/partners/bee/partner.json", "{}");
    put("mods/p1/partners/bee/portrait.png", "x");
    put("mods/p1/partners/bee/icon.png", "x");
    put("mods/p1/partners/bee/field/sprite.json", "{}");
    put("mods/p1/partners/bee/field/000.png", "x");
    put("mods/p1/partners/bee/training/punch/training.json", "{}");
    put("mods/p1/partners/bee/training/punch/000.png", "x");
    /* a new partner with what nobody reads */
    put("mods/p1/partners/ant/partner.json", "{}");
    put("mods/p1/partners/ant/notes.txt", "x");
    put("mods/p1/partners/ant/portait.png", "x");
    put("mods/p1/partners/ant/extra/a.txt", "x");
    put("mods/p1/partners/ant/field/000.png", "x");
    put("mods/p1/partners/ant/training/punch/000.png", "x");
    put("mods/p1/partners/ant/training/nosuch/training.json", "{}");
    put("mods/p1/partners/ant/training/stray.txt", "x");
    /* an original's folder: its art and gym stand, a file nobody reads is a warning, a field/ without sprite.json is how the exporter's frames go in */
    put("mods/p1/partners/agumon/portrait.png", "x");
    put("mods/p1/partners/agumon/field/000.png", "x");
    put("mods/p1/partners/agumon/training/defense/training.json", "{}");
    put("mods/p1/partners/agumon/stray.txt", "x");
    /* art for the partner another mod declares, and a folder nothing declares */
    put("mods/p3/mod.json", "{\"format\":\"mod-v1\",\"kind\":\"assets\"}");
    put("mods/p3/partners/bee/icon.png", "x");
    put("mods/p3/partners/orphan/icon.png", "x");

    txt = validate(NULL, &rc);
    CHECK(strstr(txt, "partners/ant/notes.txt is ignored (a partner's folder holds partner.json, portrait.png, icon.png, field/ and training/)"));
    CHECK(strstr(txt, "partners/ant/portait.png is ignored (did you mean portrait.png?)"));
    CHECK(strstr(txt, "partners/ant/extra is ignored (a partner's folder holds"));
    CHECK(strstr(txt, "partners/ant/field/ is ignored: it has no sprite.json"));
    CHECK(strstr(txt, "partners/ant/training/punch has no training.json (it is ignored)"));
    CHECK(strstr(txt, "partners/ant/training/nosuch is ignored: no training is called that"));
    CHECK(strstr(txt, "partners/ant/training/stray.txt is ignored (a training is a folder: training/<name>/training.json)"));
    CHECK(strstr(txt, "partners/agumon/stray.txt is ignored (a partner's folder holds"));
    CHECK(!strstr(txt, "partners/agumon/field") && !strstr(txt, "partners/agumon/training") && !strstr(txt, "partners/bee/field") && !strstr(txt, "partners/bee/training"));
    CHECK(!strstr(txt, "partners/bee/portrait") && !strstr(txt, "partners/bee/icon") && !strstr(txt, "partners/bee/partner.json"));
    CHECK(strstr(txt, "partners/orphan is ignored: it is not an original partner's folder and no active mod has a partners/orphan/partner.json"));
    CHECK(!strstr(txt, "partners/bee is ignored"));
    /* one line per shared folder, with both layers and the winner: p3 loads after p1 */
    CHECK(countOf(txt, "partners/bee/ is shared by") == 1);
    CHECK(strstr(txt, "partners/bee/ is shared by mods/p3, mods/p1: mods/p3 wins (a file is read from the first layer that has it: the mods from the last to the first, then data/)"));
    CHECK(countOf(txt, "partners/agumon/ is shared by") == 1);
    CHECK(strstr(txt, "partners/agumon/ is shared by mods/p1, data/ (edited): mods/p1 wins"));
    CHECK(!strstr(txt, "partners/orphan/ is shared"));
    free(txt);
    /* one mod alone: the folders it shares still say so */
    txt = validate("p1", &rc);
    CHECK(countOf(txt, "partners/bee/ is shared by") == 1 && strstr(txt, "partners/agumon/ is shared by"));
    free(txt);
    /* a good partner alone in its mod: nothing to say */
    wipe();
    put("mods/q1/mod.json", "{\"format\":\"mod-v1\",\"kind\":\"content\"}");
    put("mods/q1/partners/bee/partner.json", "{}");
    put("mods/q1/partners/bee/icon.png", "x");
    txt = validate(NULL, &rc);
    CHECK(rc == 0 && !strstr(txt, "is shared by") && !strstr(txt, "is ignored") && strstr(txt, "ok"));
    free(txt);
}

static void testValidateClean(void)
{
    char *txt;
    int rc;
    ModToolsCmd c;
    int argc = 2;
    char *argv[] = {"boot", "--list-mods", NULL};

    wipe();
    put("mods/a/mod.json", "{\"format\":\"mod-v1\",\"name\":\"A\"}");
    put("mods/a/tables/battle_rewards.json", "{\"format\":\"battle_rewards-v1\",\"rows\":{\"*\":{\"money\":\"*2\"}}}");
    txt = validate(NULL, &rc);
    CHECK(rc == 0 && strstr(txt, "0 error") && strstr(txt, "validating 1 mod"));
    free(txt);
    CHECK(modToolsTakeArgs(&argc, argv, &c) == 1);
    wipe();
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    char tpl[1100];
    snprintf(tpl, sizeof tpl, "%s/configtest-XXXXXX", (tmp && tmp[0]) ? tmp : "/tmp");
    if (!mkdtemp(tpl)) {
        fprintf(stderr, "configtest: cannot make a scratch folder in %s\n", tpl);
        return 2;
    }
    snprintf(gRoot, sizeof gRoot, "%s", tpl);
    snprintf(gLogPath, sizeof gLogPath, "%s/.log", gRoot);
    testDefaults();
    testUserEditedData();
    testMerge();
    testRejections();
    testTransitions();
    testManifest();
    testLimits();
    testToolArgs();
    testNewMod();
    testListAndValidate();
    testValidatePartners();
    testValidateEmptySlots();
    testValidateClean();
    {
        char cmd[1200];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", gRoot);
        if (system(cmd)) {}
    }
    if (failures) {
        printf("configtest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("configtest: OK\n");
    return 0;
}
