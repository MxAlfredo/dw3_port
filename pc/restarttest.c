/* The restart handoff end to end, on the real process APIs (Windows: CreateProcess + --wait-pid; Linux: execv).
   The old process arms the restart, then takes 1.5 s to shut down (an atexit handler that runs after the restart's, like the
   exporters' and the log's); the new process must start only after that. Both write one timestamped line to $RT_OUT. Run by
   `make check-restart` (native) and, for the Windows build, `make check-restart TARGET=windows` through Wine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#include "shim/options_system.h"

/* the tab's collaborators, not needed here */
unsigned csNavEpoch(void) { return 0; }
void csWidgetAdd(int tab, int row, const void *w) { (void)tab; (void)row; (void)w; }
void inputCapFrame(void) {}
int inputCfgLogLevel(void) { return 0; }
void inputCfgSetLogLevel(int l) { (void)l; }
void inputCfgRegisterExport(const char *k, const char *n) { (void)k; (void)n; }
int inputCfgReadExport(const char *k, char *o, int c) { (void)k; (void)o; (void)c; return 0; }
void optionsModsInit(void) {}

static const char *outPath;

static unsigned long long nowMs(void) {
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000u + (unsigned long long)(t.tv_nsec / 1000000);
#endif
}

static void note(const char *what) {
    FILE *f = fopen(outPath, "a");

    if (f != NULL) {
        fprintf(f, "%llu %s\n", nowMs(), what);
        fclose(f);
    }
}

static void sleepMs(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000u);
#endif
}

static void slowShutdown(void) {
    sleepMs(1500); /* the log, the exporters' workers, the files: still closing */
    note("old-end");
}

int main(int argc, char **argv) {
    int second;

    outPath = getenv("RT_OUT");
    if (outPath == NULL) {
        fprintf(stderr, "restarttest: RT_OUT is not set\n");
        return 2;
    }
    {
        static char trace[1100];

        snprintf(trace, sizeof(trace), "%s.trace", outPath); /* the handoff's own trace, checked by check-restart */
        optionsSystemSetTraceFile(trace);
    }
    optionsSystemTakeParent(&argc, argv);
    {
        FILE *f = fopen(outPath, "r");

        second = f != NULL && fgetc(f) != EOF; /* the old process wrote its first line before it restarted */
        if (f != NULL) {
            fclose(f);
        }
    }
    if (second) {
        note("new-start");
        optionsSystemRestartNote();
        return 0;
    }
    optionsSystemSetArgs(argc, argv);
    atexit(slowShutdown);            /* registered first: runs after the restart's handler, like boot.c's exporters */
    optionsSystemRegisterExit();
    note("old-start");
    optionsSystemRestartAtExit();
    return 0;
}
