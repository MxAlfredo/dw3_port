/*
 * The fatal-signal report (R1): on SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT the process writes what it knows into the game's own log
 * file (and logcat on Android) before it dies, so a crash is never lost to a rotated logcat.
 *
 * Async-signal-safe on purpose: the handler formats into a stack buffer with the tiny formatter below and calls write() on a file
 * descriptor opened beforehand; no stdio, no malloc, no locks. The modules (name, load bias, build id) come from a table taken at
 * install time, so the handler never calls dladdr.
 *
 * Report lines (one sample in docs/modding/README.md next to the harness tokens; symbolization in pc/README.md, "Reportes de crash"):
 *   boot: fatal signal 11 (SIGSEGV) code 1 fault addr 0x0000000000000008 tid 4242 thread "SDLThread"
 *   boot: pc 0000007a12345678 lr 0000007a12345600 sp 0000007fe0001230      (aarch64 only)
 *     #00 pc 0000007a12345678 libmain.so+0x1a2b3c
 *   boot: module libmain.so build-id 94238816b1a45837649aaad1b57e48afe438f77e
 *   boot: end of report
 */
#ifndef FAULT_REPORT_H
#define FAULT_REPORT_H

#include <stddef.h>

/* The formatter: appends to a bounded buffer, always keeps room for the end; a full buffer drops the rest. */
typedef struct {
    char *p;
    int n;
    int cap;
} FaultBuf;

void faultBufInit(FaultBuf *b, char *mem, int cap);
void faultPutStr(FaultBuf *b, const char *s);
void faultPutDec(FaultBuf *b, long long v);
/* lower-case hex of v, left-padded with zeros to `digits` (0: no padding) */
void faultPutHex(FaultBuf *b, unsigned long long v, int digits);

/* Takes the module table, sets the alternate stack and installs the handlers (Windows: a plain handler that names the signal). */
void faultReportInstall(void);
/* Adds the modules loaded since the table was taken (a later dlopen); call it now and then from the game's thread. No-op on Windows. */
void faultReportRefreshModules(void);
/* The log file's descriptor (opened beforehand, O_APPEND). -1: the report goes to plogLogFd() when the log has one, else to fd 2. */
void faultReportSetFd(int fd);
/* The line `n` of a report for a frame: "  #NN pc <hex16> module+0xOFF" (module unknown: no module part). Exposed for tests. */
void faultPutFrame(FaultBuf *b, int n, unsigned long long pc);

/* Test hook: removes "--crash-test" / "--crash-test=<segv|abort|fpe|bus|ill>" from argv. Returns the kind ("segv" for the bare
   flag) or NULL when absent (argv untouched); an unknown kind is returned as it is and faultReportCrash rejects it. */
const char *faultReportTakeCrashArg(int *argc, char **argv);
/* Raises the named fault now (a real null write for segv, raise() for the others). Returns 0 when the name is unknown. */
int faultReportCrash(const char *kind);

#endif
