/* The port's one log: levels, channels, the `[channel] message` format (see port_log.h). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_log.h"

int plogMax = -1;

static int envLevel;              /* DW3_LOG / DW3_MOD_LOG */
static int setLevel;              /* [debug] log */
static char envChannels[256];     /* DW3_LOG_CHANNELS */
static int haveEnvChannels;
static char setChannels[256];     /* [debug] channels */

/* The old per-channel switches: trace on that channel alone */
static const struct {
    const char *channel;
    const char *env;
} LEGACY[] = {
    {"tables", "DW3_TABLE_TRACE"},
    {"files", "DW3_FS_TRACE"},
    {"images", "DW3_IMG_TRACE"},
    {"stage", "DW3_STAGE_TIMING"},
    {"memcard", "DW3_MEMCARD_TRACE"},
    {"text", "DW3_TEXT_TRACE"},
    {"battle", "DW3_BATTLE_EVENT_LOG"},
};
#define LEGACY_COUNT ((int)(sizeof(LEGACY) / sizeof(LEGACY[0])))
static int legacyOn[LEGACY_COUNT];

static int truthy(const char *v) {
    return v != NULL && v[0] != '\0' && v[0] != '0';
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;

        if (x != y) {
            return 0;
        }
    }
    return *a == '\0' && *b == '\0';
}

int plogParseLevel(const char *text) {
    char buf[16];
    size_t n = 0;

    if (text == NULL) {
        return -1;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    while (text[n] != '\0' && text[n] != ' ' && text[n] != '\t' && text[n] != '\r' && text[n] != '\n') {
        n++;
    }
    if (n == 0 || n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, text, n);
    buf[n] = '\0';
    if (ieq(buf, "off") || ieq(buf, "false") || ieq(buf, "0")) {
        return PLOG_OFF;
    }
    if (ieq(buf, "mods") || ieq(buf, "on") || ieq(buf, "true") || ieq(buf, "1")) {
        return PLOG_MODS;
    }
    if (ieq(buf, "trace")) {
        return PLOG_TRACE;
    }
    return -1;
}

const char *plogLevelName(int level) {
    return level >= PLOG_TRACE ? "trace" : level == PLOG_MODS ? "mods" : "off";
}

int plogChannelListed(const char *list, const char *channel) {
    size_t cl = strlen(channel);

    if (list == NULL) {
        return 1;
    }
    while (*list == ' ' || *list == ',' || *list == '\t') {
        list++;
    }
    if (*list == '\0') {
        return 1;
    }
    while (*list != '\0') {
        const char *e = list;
        size_t n;

        while (*e != '\0' && *e != ',') {
            e++;
        }
        n = (size_t)(e - list);
        while (n > 0 && (list[n - 1] == ' ' || list[n - 1] == '\t')) {
            n--;
        }
        if (n == cl) {
            size_t i;

            for (i = 0; i < n; i++) {
                char x = list[i] >= 'A' && list[i] <= 'Z' ? (char)(list[i] + 32) : list[i];
                char y = channel[i] >= 'A' && channel[i] <= 'Z' ? (char)(channel[i] + 32) : channel[i];

                if (x != y) {
                    break;
                }
            }
            if (i == n) {
                return 1;
            }
        }
        list = *e == ',' ? e + 1 : e;
        while (*list == ' ' || *list == '\t') {
            list++;
        }
    }
    return 0;
}

static int level(void) {
    return envLevel > setLevel ? envLevel : setLevel;
}

static void recompute(void) {
    int i, m = level();

    for (i = 0; i < LEGACY_COUNT; i++) {
        if (legacyOn[i]) {
            m = PLOG_TRACE;
        }
    }
    plogMax = m;
}

static void readEnv(void) {
    const char *v;
    int i, l;

    envLevel = PLOG_OFF;
    v = getenv("DW3_LOG");
    if (v != NULL && (l = plogParseLevel(v)) >= 0) {
        envLevel = l;
    }
    if (envLevel < PLOG_MODS && truthy(getenv("DW3_MOD_LOG"))) {
        envLevel = PLOG_MODS;
    }
    v = getenv("DW3_LOG_CHANNELS");
    haveEnvChannels = v != NULL;
    snprintf(envChannels, sizeof envChannels, "%s", v != NULL ? v : "");
    for (i = 0; i < LEGACY_COUNT; i++) {
        legacyOn[i] = truthy(getenv(LEGACY[i].env));
    }
    recompute();
}

void plogReset(void) {
    envLevel = setLevel = 0;
    envChannels[0] = setChannels[0] = '\0';
    haveEnvChannels = 0;
    memset(legacyOn, 0, sizeof legacyOn);
    plogMax = -1;
}

void plogSetSettings(int lvl, const char *channels) {
    if (plogMax < 0) {
        readEnv();
    }
    setLevel = lvl < PLOG_OFF ? PLOG_OFF : lvl > PLOG_TRACE ? PLOG_TRACE : lvl;
    snprintf(setChannels, sizeof setChannels, "%s", channels != NULL ? channels : "");
    recompute();
}

int plogOn(const char *channel, int lvl) {
    int i;

    if (plogMax < 0) {
        readEnv();
    }
    if (plogMax < lvl) {
        return 0;
    }
    for (i = 0; i < LEGACY_COUNT; i++) {
        if (legacyOn[i] && strcmp(LEGACY[i].channel, channel) == 0) {
            return 1;
        }
    }
    if (level() < lvl) {
        return 0;
    }
    return plogChannelListed(haveEnvChannels ? envChannels : setChannels, channel);
}

void plogLine(const char *channel, const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "[%s] ", channel);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void plogRaw(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int plogLineSplit(char *buf, size_t *used, size_t cap, const char *data, size_t n, void (*emit)(const char *line, void *ud), void *ud) {
    int lines = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        if (data[i] == '\r') {
            continue;
        }
        if (data[i] == '\n' || *used + 1 >= cap) {
            buf[*used] = '\0';
            emit(buf, ud);
            lines++;
            *used = 0;
            if (data[i] == '\n') {
                continue;
            }
        }
        buf[(*used)++] = data[i];
    }
    return lines;
}

#ifdef __ANDROID__
#include <android/log.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

static int teeFile = -1;
static int teePipe = -1;

static void toLogcat(const char *line, void *ud) {
    (void)ud;
    __android_log_write(ANDROID_LOG_INFO, "dw3", line);
}

static void *teeReader(void *arg) {
    char chunk[2048], line[1024];
    size_t used = 0;
    ssize_t got;

    (void)arg;
    while ((got = read(teePipe, chunk, sizeof(chunk))) > 0) {
        ssize_t off = 0;

        while (off < got) { /* the file gets the bytes as they are */
            ssize_t w = write(teeFile, chunk + off, (size_t)(got - off));

            if (w <= 0) {
                break;
            }
            off += w;
        }
        plogLineSplit(line, &used, sizeof(line), chunk, (size_t)got, toLogcat, NULL);
    }
    return NULL;
}

int plogTeeToLogcat(const char *logFile) {
    int fds[2];
    pthread_t th;

    teeFile = open(logFile, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (teeFile < 0 || pipe(fds) != 0) {
        return 0;
    }
    teePipe = fds[0];
    if (pthread_create(&th, NULL, teeReader, NULL) != 0) {
        close(fds[0]);
        close(fds[1]);
        return 0;
    }
    pthread_detach(th);
    fflush(stdout);
    fflush(stderr);
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    close(fds[1]);
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 4096);
    return 1;
}
#else
int plogTeeToLogcat(const char *logFile) {
    (void)logFile;
    return 0;
}
#endif
