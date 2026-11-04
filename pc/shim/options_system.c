/* The options menu's System tab (feature system-menu); see options_system.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <SDL2/SDL.h>

#ifdef _WIN32
#include <errno.h>
#include <wchar.h>
#define WIN32_LEAN_AND_MEAN /* no winsock: it clashes with the port's u_long & co. */
#include <windows.h>
#include <shellapi.h>
#else
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <jni.h>
#endif
#endif

#include "controls_screen.h"
#include "input_cfg.h"
#include "options_system.h"
#include "options_mods.h"

void optSysConfirmInit(OptSysConfirm *c) {
    c->row = -1;
    c->epoch = 0;
    c->at = 0;
}

int optSysArmed(const OptSysConfirm *c, int row, unsigned epoch, unsigned nowMs) {
    return c->row == row && row >= 0 && c->epoch == epoch && (unsigned)(nowMs - c->at) <= OPT_SYS_CONFIRM_MS;
}

int optSysPress(OptSysConfirm *c, int row, unsigned epoch, unsigned nowMs) {
    if (optSysArmed(c, row, epoch, nowMs)) {
        optSysConfirmInit(c);
        return 1;
    }
    c->row = row;
    c->epoch = epoch;
    c->at = nowMs;
    return 0;
}

/* ---- the process ---- */

static int savedArgc;
static char **savedArgv;
static int restartPending;

void optionsSystemSetArgs(int argc, char **argv) {
    savedArgc = argc;
    savedArgv = argv;
}

/* ---- the restart's trace: a small file beside the logs, written before the new process has a log of its own ---- */

static char traceFile[1100];

void optionsSystemSetTraceFile(const char *path) {
    snprintf(traceFile, sizeof(traceFile), "%s", path != NULL ? path : "");
}

/* One line appended to the trace file; nothing when no file was set */
static void restartTrace(const char *fmt, ...) {
    va_list ap;
    FILE *f;

    if (traceFile[0] == '\0' || (f = fopen(traceFile, "a")) == NULL) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* A restart that did not start the new process: in the log and in the trace */
static void restartFail(const char *what, unsigned long code) {
    fprintf(stderr, "system: restart failed (%s, %lu)\n", what, code);
    restartTrace("old: restart failed (%s, %lu)", what, code);
}

#ifdef _WIN32
/* One argument as the C runtime reads it back: quoted when it has a space, a tab or a quote
   (_wspawnv joins the arguments with spaces and does not quote them itself) */
static wchar_t *quoteArg(const wchar_t *a) {
    size_t n = wcslen(a), i, slashes = 0;
    wchar_t *out = malloc((2 * n + 3) * sizeof(wchar_t)), *o = out;

    if (out == NULL) {
        return NULL;
    }
    if (n > 0 && wcspbrk(a, L" \t\"") == NULL) {
        wcscpy(out, a);
        return out;
    }
    *o++ = L'"';
    for (i = 0; i < n; i++) {
        if (a[i] == L'\\') {
            slashes++;
            *o++ = a[i];
            continue;
        }
        if (a[i] == L'"') {
            for (; slashes > 0; slashes--) {
                *o++ = L'\\'; /* the backslashes before a quote count double, then the quote itself */
            }
            *o++ = L'\\';
        }
        slashes = 0;
        *o++ = a[i];
    }
    for (; slashes > 0; slashes--) {
        *o++ = L'\\'; /* and before the closing quote */
    }
    *o++ = L'"';
    *o = L'\0';
    return out;
}

/* The same executable with the same arguments, as a new process. It is a plain CreateProcess that inherits no handles (_wspawnv
   passed on every open C file: the disc, the log), and it carries `--wait-pid <this process>`: the new process waits for this one
   to be gone (window, audio device, files, log) before it touches any of them, see optionsSystemTakeParent. */
static void relaunch(void) {
    wchar_t *line = NULL;
    size_t cap = 64, used = 0;
    int i, wargc = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t pidText[32], exe[MAX_PATH * 4];
    wchar_t *parts[66];
    wchar_t **wargv;
    int n = 0;
    DWORD exeLen;

    /* The executable and the arguments from the Win32 API: the C runtime's _wpgmptr and __wargv are only filled in by a wide
       (wmain) start, and this program starts through main */
    exeLen = GetModuleFileNameW(NULL, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    if (exeLen == 0 || exeLen >= (DWORD)(sizeof(exe) / sizeof(exe[0]))) {
        restartFail("GetModuleFileNameW", (unsigned long)GetLastError());
        return;
    }
    wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv == NULL || wargc < 1 || wargc > 63) {
        restartFail("CommandLineToArgvW", wargv == NULL ? (unsigned long)GetLastError() : (unsigned long)wargc);
        if (wargv != NULL) {
            LocalFree(wargv);
        }
        return;
    }
    swprintf(pidText, 32, L"%lu", (unsigned long)GetCurrentProcessId());
    parts[n++] = quoteArg(exe);
    parts[n++] = quoteArg(L"--wait-pid");
    parts[n++] = quoteArg(pidText);
    for (i = 1; i < wargc; i++) {
        if (wcscmp(wargv[i], L"--wait-pid") == 0) {
            i++; /* a stale one from the start that is ending: replaced by ours */
            continue;
        }
        parts[n++] = quoteArg(wargv[i]);
    }
    LocalFree(wargv);
    for (i = 0; i < n; i++) {
        if (parts[i] == NULL) {
            restartFail("out of memory", 0);
            return;
        }
        cap += wcslen(parts[i]) + 1;
    }
    line = malloc(cap * sizeof(wchar_t));
    if (line == NULL) {
        restartFail("out of memory", 0);
        return;
    }
    line[0] = L'\0';
    for (i = 0; i < n; i++) {
        if (i > 0) {
            line[used++] = L' ';
        }
        wcscpy(line + used, parts[i]);
        used += wcslen(parts[i]);
    }
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        restartFail("CreateProcessW", (unsigned long)GetLastError());
        return;
    }
    fprintf(stderr, "system: the new process is pid %lu\n", (unsigned long)pi.dwProcessId);
    restartTrace("old pid %lu: started the new process, pid %lu", (unsigned long)GetCurrentProcessId(), (unsigned long)pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
#elif defined(__ANDROID__)
/* There is no execv of the app: the activity (io.github.dw3port.DW3Activity.restartApp, a static method) starts a helper
   activity in its own process, which ends this process and launches the app again. The arguments are not carried over: an
   Android start has none. */
static void relaunch(void) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass cls;
    jmethodID method;

    if (env == NULL || activity == NULL) {
        restartFail("no JNI environment", 0);
        return;
    }
    cls = (*env)->GetObjectClass(env, activity);
    method = cls != NULL ? (*env)->GetStaticMethodID(env, cls, "restartApp", "()V") : NULL;
    if (method == NULL) {
        (*env)->ExceptionClear(env);
        restartFail("DW3Activity.restartApp not found", 0);
        return;
    }
    (*env)->CallStaticVoidMethod(env, cls, method);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        restartFail("DW3Activity.restartApp threw", 0);
        return;
    }
    restartTrace("old pid %lu: asked the activity to restart the app", (unsigned long)getpid());
}
#else
/* The running executable (through the AppImage when there is one), the same arguments */
static void relaunch(void) {
    char exe[4096];
    const char *path = getenv("APPIMAGE");
    char *args[64];
    int i, n = 0;
    ssize_t len;

    if (path == NULL || path[0] == '\0') {
        len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (len > 0) {
            exe[len] = '\0';
            path = exe;
        } else if (savedArgc > 0) {
            path = savedArgv[0];
        } else {
            return;
        }
    }
    args[n++] = (char *)path;
    for (i = 1; i < savedArgc && n < 63; i++) {
        args[n++] = savedArgv[i];
    }
    args[n] = NULL;
    fflush(NULL);
    execv(path, args);
    restartFail(path, (unsigned long)errno);
}
#endif

/* ---- the handoff: the new process waits for the old one ---- */

static long waitedMs = -1;

static unsigned long ownPid(void) {
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}
static unsigned long waitedPid;

int optSysTakeWaitPid(int *argc, char **argv, unsigned long *pid) {
    int i, out = 1, found = 0;

    if (argc == NULL || argv == NULL || pid == NULL) {
        return 0;
    }
    for (i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--wait-pid") == 0 && i + 1 < *argc) {
            char *end;
            unsigned long v = strtoul(argv[i + 1], &end, 10);

            i++; /* the number goes with the flag, valid or not */
            if (end != argv[i] && *end == '\0' && v > 0) {
                *pid = v;
                found = 1;
            }
        } else {
            argv[out++] = argv[i];
        }
    }
    if (out != *argc) {
        argv[out] = NULL;
        *argc = out;
    }
    return found;
}

long optSysWaitForPid(unsigned long pid, unsigned timeoutMs) {
#ifdef _WIN32
    DWORD t0 = GetTickCount();
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);

    if (h == NULL) {
        return 0; /* nothing to wait for: it is already gone */
    }
    if (WaitForSingleObject(h, timeoutMs) == WAIT_TIMEOUT) {
        CloseHandle(h);
        return -1;
    }
    CloseHandle(h);
    return (long)(GetTickCount() - t0);
#else
    unsigned waited = 0;

    while (kill((pid_t)pid, 0) == 0 || errno == EPERM) {
        if (waited >= timeoutMs) {
            return -1;
        }
        usleep(10000);
        waited += 10;
    }
    return (long)waited;
#endif
}

void optionsSystemTakeParent(int *argc, char **argv) {
    unsigned long pid;

    if (optSysTakeWaitPid(argc, argv, &pid)) {
        waitedPid = pid;
        restartTrace("new pid %lu: started, waiting for pid %lu", ownPid(), pid);
        waitedMs = optSysWaitForPid(pid, OPT_SYS_WAIT_MS);
        restartTrace("new pid %lu: pid %lu %s after %ld ms", ownPid(), pid, waitedMs >= 0 ? "was gone" : "was still there",
                     waitedMs >= 0 ? waitedMs : (long)OPT_SYS_WAIT_MS);
    }
}

void optionsSystemRestartNote(void) {
    if (waitedPid != 0) {
        restartTrace("new pid %lu: the log is open", ownPid());
        if (waitedMs >= 0) {
            fprintf(stderr, "system: restarted, the old process (pid %lu) was gone after %ld ms\n", waitedPid, waitedMs);
        } else {
            fprintf(stderr, "system: restarted, the old process (pid %lu) was still there after %u ms; going on\n", waitedPid,
                    OPT_SYS_WAIT_MS);
        }
        waitedPid = 0;
    }
}

static void atExit(void) {
    if (restartPending) {
        restartPending = 0;
        fprintf(stderr, "system: restarting\n");
        restartTrace("old pid %lu: restarting", ownPid());
        fflush(NULL);
        relaunch();
    }
}

void optionsSystemRegisterExit(void) {
    atexit(atExit);
}

void optionsSystemRestartAtExit(void) {
    restartPending = 1;
}

/* The normal way out: what closing the window does (PsyCross: SDL_QUIT, PsyX_Exit, exit(0)) */
static void requestQuit(void) {
    SDL_Event ev;

    inputCapFrame(); /* a setting changed this frame is on disk before the process ends */
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
}

/* ---- the rows ---- */

static OptSysConfirm confirm_;

static int press(int row) {
    return optSysPress(&confirm_, row, csNavEpoch(), SDL_GetTicks());
}

static void applyRestart(int unused) {
    (void)unused;
    if (press(OPT_SYS_RESTART)) {
        optionsSystemRestartAtExit();
        requestQuit();
    }
}

static void applyQuit(int unused) {
    (void)unused;
    if (press(OPT_SYS_QUIT)) {
        requestQuit();
    }
}

static const char *textRow(int row) {
    return optSysArmed(&confirm_, row, csNavEpoch(), SDL_GetTicks()) ? "Press again to confirm" : "";
}

static const char *textRestart(void) { return textRow(OPT_SYS_RESTART); }
static const char *textQuit(void) { return textRow(OPT_SYS_QUIT); }

/* ---- Prepare modding files (modding-ux U4) ----
   The four heavy exporters (videos, audio, models, stages) sit behind one row. boot registers each one's hooks; the row runs them one
   after the other (each on its own worker thread, so the game keeps its frame rate), skips what is already up to date, and shows the
   step that is running ("Videos 3/14"). The command line's --export-all drives the same job without a window. */

enum { PREP_VIDEOS, PREP_AUDIO, PREP_MODELS, PREP_STAGES, PREP_N }; /* the order they run in */

static const char *const prepName[PREP_N] = {"Videos", "Audio", "Models", "Stages"};
static OptSysExport sEx[PREP_N];
static int sExSet[PREP_N];

static void setEx(int i, const OptSysExport *e) {
    if (e != NULL) {
        sEx[i] = *e;
        sExSet[i] = 1;
    } else {
        sExSet[i] = 0;
    }
}

void optionsSystemSetExport(const OptSysExport *e) { setEx(PREP_MODELS, e); }
void optionsSystemSetExportVideos(const OptSysExport *e) { setEx(PREP_VIDEOS, e); }
void optionsSystemSetExportAudio(const OptSysExport *e) { setEx(PREP_AUDIO, e); }
void optionsSystemSetExportStages(const OptSysExport *e) { setEx(PREP_STAGES, e); }

static struct {
    int running;
    int cur;        /* the step in progress, -1 before the first and after the last */
    int curStarted; /* its job really started (a step with nothing to do is skipped) */
    int failed;     /* items that failed in this run */
    int finished;   /* a run ended since the last start: the row reads Done */
} sPrep = {0, -1, 0, 0, 0};

static void prepAdvance(void) {
    while (sPrep.running) {
        int next;

        if (sPrep.cur >= 0 && sPrep.curStarted) {
            if (sEx[sPrep.cur].active != NULL && sEx[sPrep.cur].active()) {
                return;
            }
            if (sEx[sPrep.cur].failed != NULL) {
                sPrep.failed += sEx[sPrep.cur].failed();
            }
            sPrep.curStarted = 0;
        }
        for (next = sPrep.cur + 1; next < PREP_N && !sExSet[next]; next++) {
        }
        if (next >= PREP_N) {
            sPrep.running = 0;
            sPrep.cur = -1;
            sPrep.finished = 1;
            return;
        }
        sPrep.cur = next;
        if (sEx[next].startIfNeeded != NULL) {
            sPrep.curStarted = sEx[next].startIfNeeded() > 0;
        } else if (sEx[next].start != NULL) {
            sPrep.curStarted = sEx[next].start() > 0;
        }
    }
}

int optionsSystemPrepStart(void) {
    int i, any = 0;

    for (i = 0; i < PREP_N; i++) {
        any |= sExSet[i];
    }
    if (!any || sPrep.running) {
        return 0;
    }
    sPrep.running = 1;
    sPrep.cur = -1;
    sPrep.curStarted = 0;
    sPrep.failed = 0;
    sPrep.finished = 0;
    prepAdvance();
    return 1;
}

int optionsSystemPrepActive(void) { return sPrep.running; }
int optionsSystemPrepFailed(void) { return sPrep.failed; }

const char *optionsSystemPrepStatus(void) {
    static char text[96];

    if (sPrep.running && sPrep.cur >= 0) {
        const char *st = sPrep.curStarted && sEx[sPrep.cur].status != NULL ? sEx[sPrep.cur].status() : "";

        if (strncmp(st, "Exporting ", 10) == 0) {
            st += 10; /* "Exporting 3/14" -> "Videos 3/14" */
        }
        snprintf(text, sizeof(text), "%s%s%s", prepName[sPrep.cur], st[0] != '\0' ? " " : "", st);
        return text;
    }
    if (sPrep.finished) {
        if (sPrep.failed > 0) {
            snprintf(text, sizeof(text), "Done, %d failed", sPrep.failed);
            return text;
        }
        return "Done";
    }
    return "";
}

void optionsSystemFrame(void) {
    int i;

    for (i = 0; i < PREP_N; i++) {
        if (sExSet[i] && sEx[i].pump != NULL) {
            sEx[i].pump();
        }
    }
    prepAdvance();
}

int optionsSystemExportAllRun(void) {
    char last[96] = "";
    unsigned lastAt = 0;
    int failed;

    printf("export-all: start\n");
    fflush(stdout);
    if (!optionsSystemPrepStart()) {
        fprintf(stderr, "export-all: no exporter is available\n");
        return 1;
    }
    while (optionsSystemPrepActive()) {
        const char *st;
        unsigned now;

        optionsSystemFrame();
        st = optionsSystemPrepStatus();
        now = SDL_GetTicks();
        if (strcmp(st, last) != 0 && (strncmp(st, last, strcspn(st, " ")) != 0 || now - lastAt >= 500u)) {
            printf("export-all: %s\n", st);
            fflush(stdout);
            snprintf(last, sizeof(last), "%s", st);
            lastAt = now;
        }
        SDL_Delay(10);
    }
    failed = optionsSystemPrepFailed();
    printf("export-all: %s\n", optionsSystemPrepStatus());
    fflush(stdout);
    if (failed > 0) {
        fprintf(stderr, "export-all: %d item(s) failed (the log has the details)\n", failed);
        return 1;
    }
    return 0;
}

static void applyPrepare(int unused) {
    (void)unused;
    optionsSystemPrepStart();
}

static const char *textPrepare(void) { return optionsSystemPrepStatus(); }

/* ---- Import memory card (M2 / R4) ---- */

#define ROW_CARD_IMPORT 90 /* the confirmation model's id for this row (the others are OPT_SYS_*) */

static OptSysCardImport sCard;
static int sCardSet;
static char sCardMsg[256];
static char sCardDesc[512];

void optionsSystemSetCardImport(const OptSysCardImport *c) {
    if (c != NULL) {
        sCard = *c;
        sCardSet = 1;
    } else {
        sCardSet = 0;
    }
    sCardMsg[0] = '\0';
}

static void applyCardImport(int unused) {
    (void)unused;
    if (sCardSet && press(ROW_CARD_IMPORT)) {
        sCard.import(sCardMsg, sizeof(sCardMsg));
    }
}

static const char *textCardImport(void) {
    if (optSysArmed(&confirm_, ROW_CARD_IMPORT, csNavEpoch(), SDL_GetTicks())) {
        return "Press again to confirm";
    }
    return sCardMsg;
}

/* ---- the log level (port_log.h) ---- */

static const char *const logNames[] = {"Off", "Mods", "Trace"};

static void applyLog(int value) {
    inputCfgSetLogLevel(value);
}

void optionsSystemInit(void) {
    CsWidget w;

    optSysConfirmInit(&confirm_);
    w = (CsWidget){.kind = CS_ROW_ACTION, .label = "Restart game", .apply = applyRestart, .text = textRestart,
                   .desc = "Starts the game again. Confirm twice."};
    csWidgetAdd(CS_TAB_SYSTEM, &w);
    w = (CsWidget){.kind = CS_ROW_ACTION, .label = "Quit game", .apply = applyQuit, .text = textQuit,
                   .desc = "Saves the settings and closes. Confirm twice."};
    csWidgetAdd(CS_TAB_SYSTEM, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Log", .names = logNames, .count = 3, .value = inputCfgLogLevel(),
                   .apply = applyLog,
                   .desc = "Mods: what mods change. Trace: all detail."};
    csWidgetAdd(CS_TAB_SYSTEM, &w);
    if (sExSet[PREP_VIDEOS] || sExSet[PREP_AUDIO] || sExSet[PREP_MODELS] || sExSet[PREP_STAGES]) {
        w = (CsWidget){.kind = CS_ROW_ACTION, .label = "Prepare modding files", .apply = applyPrepare, .text = textPrepare,
                       .desc = "Exports the heavy files for mods. About 700 MB."};
        csWidgetAdd(CS_TAB_SYSTEM, &w);
    }
    if (sCardSet) {
        const char *d = sCard.dropDir != NULL ? sCard.dropDir() : "";

        snprintf(sCardDesc, sizeof(sCardDesc), "Replaces your saves with a PS1 card (.mcr .mcd .gme .vmp) found in %s or an emulator's folder. The old saves are kept in a backup folder. Confirm twice.",
                 d != NULL && d[0] ? d : "the import folder");
        w = (CsWidget){.kind = CS_ROW_ACTION, .label = "Import memory card", .apply = applyCardImport, .text = textCardImport,
                       .desc = sCardDesc};
        csWidgetAdd(CS_TAB_SYSTEM, &w);
    }
    optionsModsInit(); /* the Mods tab (modding-ux U6) */
}
