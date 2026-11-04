/*
 * Host test of the port's one log (pc/shim/port_log.c, feature port-log T1): levels, channels,
 * the environment overrides and the legacy shortcuts. No disc, no window. The log goes to stderr,
 * which the test points at a scratch file under $TMPDIR.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim/port_log.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "logtest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* plogLineSplit's sink: the lines joined with '|' */
static void joinLine(const char *line, void *ud)
{
    char *acc = ud;

    strcat(acc, line);
    strcat(acc, "|");
}

static char logPath[1100];
static int savedErr = -1;

static void captureStart(void)
{
    fflush(stderr);
    savedErr = dup(2);
    if (!freopen(logPath, "wb", stderr)) failures++;
}

static void captureStop(char *out, size_t cap)
{
    FILE *f;
    size_t n = 0;

    fflush(stderr);
    dup2(savedErr, 2);
    close(savedErr);
    clearerr(stderr);
    f = fopen(logPath, "rb");
    if (f) {
        n = fread(out, 1, cap - 1, f);
        fclose(f);
    }
    out[n] = 0;
}

static void clearEnv(void)
{
    unsetenv("DW3_LOG");
    unsetenv("DW3_LOG_CHANNELS");
    unsetenv("DW3_MOD_LOG");
    unsetenv("DW3_TABLE_TRACE");
    unsetenv("DW3_FS_TRACE");
    unsetenv("DW3_IMG_TRACE");
    unsetenv("DW3_STAGE_TIMING");
    unsetenv("DW3_MEMCARD_TRACE");
    unsetenv("DW3_TEXT_TRACE");
    unsetenv("DW3_BATTLE_EVENT_LOG");
    plogReset();
}

/* One line of each kind on tables and files, at the current level; the text the log got */
static void emit(char *out, size_t cap)
{
    captureStart();
    PLOG("tables", PLOG_MODS, "m-tables %d\n", 1);
    PLOG("tables", PLOG_TRACE, "t-tables %d\n", 2);
    PLOG("files", PLOG_MODS, "m-files\n");
    PLOG("files", PLOG_TRACE, "t-files\n");
    plogLine("config", "always %s\n", "on");
    captureStop(out, cap);
}

static int has(const char *log, const char *s)
{
    return strstr(log, s) != NULL;
}

static int sideEffects;
static int bump(void)
{
    return ++sideEffects;
}

int main(void)
{
    char log[4096];
    const char *tmp = getenv("TMPDIR");

    snprintf(logPath, sizeof logPath, "%s/logtest.log", tmp && tmp[0] ? tmp : "/tmp");

    CHECK(plogParseLevel("off") == PLOG_OFF && plogParseLevel("MODS") == PLOG_MODS && plogParseLevel(" trace ") == PLOG_TRACE);
    CHECK(plogParseLevel("on") == PLOG_MODS && plogParseLevel("true") == PLOG_MODS && plogParseLevel("1") == PLOG_MODS);
    CHECK(plogParseLevel("false") == PLOG_OFF && plogParseLevel("0") == PLOG_OFF);
    CHECK(plogParseLevel("maybe") == -1 && plogParseLevel("") == -1 && plogParseLevel(NULL) == -1);
    CHECK(strcmp(plogLevelName(PLOG_OFF), "off") == 0 && strcmp(plogLevelName(PLOG_MODS), "mods") == 0 &&
          strcmp(plogLevelName(PLOG_TRACE), "trace") == 0);

    CHECK(plogChannelListed("", "tables") && plogChannelListed(NULL, "tables"));
    CHECK(plogChannelListed("tables, files", "files") && plogChannelListed(" Tables ,files", "tables"));
    CHECK(!plogChannelListed("tables, files", "image") && !plogChannelListed("tablesx", "tables"));

    /* off: only the always-on line, with the [channel] prefix */
    clearEnv();
    plogSetSettings(PLOG_OFF, "");
    emit(log, sizeof log);
    CHECK(!has(log, "m-tables") && !has(log, "t-tables") && !has(log, "m-files") && !has(log, "t-files"));
    CHECK(has(log, "[config] always on\n"));

    /* mods: the mods lines, with the prefix and the message as written */
    plogSetSettings(PLOG_MODS, "");
    emit(log, sizeof log);
    CHECK(has(log, "[tables] m-tables 1\n") && has(log, "[files] m-files\n"));
    CHECK(!has(log, "t-tables") && !has(log, "t-files"));

    /* trace: both */
    plogSetSettings(PLOG_TRACE, "");
    emit(log, sizeof log);
    CHECK(has(log, "[tables] m-tables 1\n") && has(log, "[tables] t-tables 2\n") && has(log, "[files] t-files\n"));

    /* channels filter the mods and trace lines, never the always-on ones */
    plogSetSettings(PLOG_TRACE, "tables");
    emit(log, sizeof log);
    CHECK(has(log, "[tables] t-tables 2\n") && !has(log, "files") && has(log, "[config] always on\n"));

    /* a line that is off never evaluates its arguments */
    plogSetSettings(PLOG_OFF, "");
    sideEffects = 0;
    PLOG("tables", PLOG_TRACE, "%d\n", bump());
    CHECK(sideEffects == 0);
    plogSetSettings(PLOG_TRACE, "");
    captureStart();
    PLOG("tables", PLOG_TRACE, "%d\n", bump());
    captureStop(log, sizeof log);
    CHECK(sideEffects == 1 && has(log, "[tables] 1\n"));

    /* the environment wins over the file, in both directions of the level and for the channels */
    clearEnv();
    setenv("DW3_LOG", "trace", 1);
    plogSetSettings(PLOG_OFF, "");
    CHECK(plogOn("files", PLOG_TRACE) && plogOn("tables", PLOG_MODS));
    setenv("DW3_LOG_CHANNELS", "files", 1);
    plogReset();
    plogSetSettings(PLOG_OFF, "tables");
    CHECK(plogOn("files", PLOG_TRACE) && !plogOn("tables", PLOG_MODS)); /* the env channel list replaces the file's */

    /* DW3_MOD_LOG=1 is mods */
    clearEnv();
    setenv("DW3_MOD_LOG", "1", 1);
    plogReset();
    plogSetSettings(PLOG_OFF, "");
    CHECK(plogOn("tables", PLOG_MODS) && !plogOn("tables", PLOG_TRACE));

    /* the old traces: trace on their own channel only, under any level and channel list */
    clearEnv();
    setenv("DW3_FS_TRACE", "1", 1);
    plogReset();
    plogSetSettings(PLOG_OFF, "tables");
    CHECK(plogOn("files", PLOG_TRACE) && plogOn("files", PLOG_MODS));
    CHECK(!plogOn("tables", PLOG_MODS) && !plogOn("images", PLOG_TRACE) && !plogOn("stage", PLOG_TRACE));
    clearEnv();
    setenv("DW3_FS_TRACE", "0", 1);
    plogReset();
    plogSetSettings(PLOG_OFF, "");
    CHECK(!plogOn("files", PLOG_TRACE));
    clearEnv();
    setenv("DW3_TABLE_TRACE", "1", 1);
    setenv("DW3_IMG_TRACE", "1", 1);
    setenv("DW3_STAGE_TIMING", "2", 1);
    plogReset();
    CHECK(plogOn("tables", PLOG_TRACE) && plogOn("images", PLOG_TRACE) && plogOn("stage", PLOG_TRACE) && !plogOn("files", PLOG_TRACE));

    /* T2: the per-event channels (gfx, pad, field, tasks, overlay) have no old switch: level and channel list only;
       memcard, text and battle keep theirs as shortcuts */
    clearEnv();
    plogReset();
    plogSetSettings(PLOG_TRACE, "gfx, pad");
    CHECK(plogOn("gfx", PLOG_TRACE) && plogOn("pad", PLOG_TRACE) && !plogOn("field", PLOG_TRACE) && !plogOn("tasks", PLOG_TRACE));
    plogSetSettings(PLOG_MODS, "");
    CHECK(!plogOn("gfx", PLOG_TRACE) && !plogOn("pad", PLOG_TRACE) && !plogOn("tasks", PLOG_TRACE));
    clearEnv();
    setenv("DW3_MEMCARD_TRACE", "1", 1);
    plogReset();
    plogSetSettings(PLOG_OFF, "");
    CHECK(plogOn("memcard", PLOG_TRACE) && !plogOn("text", PLOG_TRACE) && !plogOn("battle", PLOG_TRACE) && !plogOn("gfx", PLOG_TRACE));
    clearEnv();
    setenv("DW3_TEXT_TRACE", "1", 1);
    setenv("DW3_BATTLE_EVENT_LOG", "1", 1);
    plogReset();
    plogSetSettings(PLOG_OFF, "tables");
    CHECK(plogOn("text", PLOG_TRACE) && plogOn("battle", PLOG_TRACE) && !plogOn("memcard", PLOG_TRACE) && !plogOn("pad", PLOG_TRACE));
    clearEnv();
    setenv("DW3_TEXT_TRACE", "0", 1);
    plogReset();
    CHECK(!plogOn("text", PLOG_TRACE) && plogMax == PLOG_OFF);

    /* nothing set, nothing on */
    clearEnv();
    CHECK(!plogOn("tables", PLOG_MODS) && !plogOn("files", PLOG_TRACE) && plogMax == PLOG_OFF);

    /* the logcat tee's line splitter (Android): whole lines out, the partial one kept for the next read, \r\n and long lines handled */
    {
        char buf[16], acc[256] = "";
        size_t used = 0;

        CHECK(plogLineSplit(buf, &used, sizeof(buf), "one\ntw", 6, joinLine, acc) == 1 && strcmp(acc, "one|") == 0 && used == 2);
        CHECK(plogLineSplit(buf, &used, sizeof(buf), "o\r\n\nthree\n", 10, joinLine, acc) == 3 && strcmp(acc, "one|two||three|") == 0 && used == 0);
        acc[0] = '\0';
        CHECK(plogLineSplit(buf, &used, sizeof(buf), "0123456789abcdefXY\n", 19, joinLine, acc) == 2
              && strcmp(acc, "0123456789abcde|fXY|") == 0 && used == 0);
        CHECK(plogLineSplit(buf, &used, sizeof(buf), "", 0, joinLine, acc) == 0);
    }

    clearEnv();
    if (failures) {
        fprintf(stdout, "logtest: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stdout, "logtest: OK\n");
    return 0;
}
