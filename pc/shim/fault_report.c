/* The fatal-signal report (shim/fault_report.h). Everything the handler touches is static data prepared by faultReportInstall. */
#define _GNU_SOURCE 1
#include "shim/fault_report.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <elf.h>
#include <link.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/log.h>
#include <unwind.h>
#else
#include <execinfo.h>
#endif
#include "shim/port_log.h"
#endif

/* ---- the formatter (pure, no libc calls beyond the compiler's own) ---- */

void faultBufInit(FaultBuf *b, char *mem, int cap) {
    b->p = mem;
    b->n = 0;
    b->cap = cap;
    if (cap > 0) {
        mem[0] = '\0';
    }
}

static void putCh(FaultBuf *b, char c) {
    if (b->n + 1 < b->cap) {
        b->p[b->n++] = c;
        b->p[b->n] = '\0';
    }
}

void faultPutStr(FaultBuf *b, const char *s) {
    if (s == NULL) {
        return;
    }
    while (*s != '\0') {
        putCh(b, *s++);
    }
}

void faultPutDec(FaultBuf *b, long long v) {
    char tmp[24];
    int n = 0;
    unsigned long long u = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;

    if (v < 0) {
        putCh(b, '-');
    }
    do {
        tmp[n++] = (char)('0' + (int)(u % 10ULL));
        u /= 10ULL;
    } while (u != 0 && n < (int)sizeof(tmp));
    while (n > 0) {
        putCh(b, tmp[--n]);
    }
}

void faultPutHex(FaultBuf *b, unsigned long long v, int digits) {
    static const char hex[] = "0123456789abcdef";
    char tmp[17];
    int n = 0;

    do {
        tmp[n++] = hex[v & 15ULL];
        v >>= 4;
    } while (v != 0 && n < 16);
    while (digits > n && digits <= 16) {
        putCh(b, '0');
        digits--;
    }
    while (n > 0) {
        putCh(b, tmp[--n]);
    }
}

/* ---- the crash-test argument ---- */

const char *faultReportTakeCrashArg(int *argc, char **argv) {
    static const char flag[] = "--crash-test";
    const char *kind = NULL;
    int i, out = 1;

    if (argc == NULL || argv == NULL) {
        return NULL;
    }
    for (i = 1; i < *argc; i++) {
        if (kind == NULL && strncmp(argv[i], flag, sizeof(flag) - 1) == 0
                && (argv[i][sizeof(flag) - 1] == '\0' || argv[i][sizeof(flag) - 1] == '=')) {
            kind = argv[i][sizeof(flag) - 1] == '=' && argv[i][sizeof(flag)] != '\0' ? argv[i] + sizeof(flag) : "segv";
        } else {
            argv[out++] = argv[i];
        }
    }
    if (kind != NULL) {
        argv[out] = NULL;
        *argc = out;
    }
    return kind;
}

int faultReportCrash(const char *kind) {
    if (kind == NULL) {
        return 0;
    }
    if (strcmp(kind, "segv") == 0) {
        *(volatile int *)0 = 1; /* a real fault: a real fault address in the report */
    }
#ifdef _WIN32
    if (strcmp(kind, "fpe") == 0) {
        raise(SIGFPE);
    } else if (strcmp(kind, "ill") == 0) {
        raise(SIGILL);
    } else if (strcmp(kind, "abort") == 0) {
        raise(SIGABRT);
    }
#else
    if (strcmp(kind, "fpe") == 0) {
        raise(SIGFPE);
    } else if (strcmp(kind, "bus") == 0) {
        raise(SIGBUS);
    } else if (strcmp(kind, "ill") == 0) {
        raise(SIGILL);
    } else if (strcmp(kind, "abort") == 0) {
        raise(SIGABRT);
    }
#endif
    else {
        return 0;
    }
    return 1;
}

#ifdef _WIN32

/* No unwinder and no dl_iterate_phdr on Windows: the handler only names the signal (stderr is the game's log there). */
static void faultHandler(int sig) {
    fflush(stdout);
    fprintf(stderr, "\nboot: fatal signal %d\n", sig);
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

void faultReportSetFd(int fd) {
    (void)fd;
}

void faultPutFrame(FaultBuf *b, int n, unsigned long long pc) {
    faultPutStr(b, "  #");
    faultPutDec(b, n);
    faultPutStr(b, " pc ");
    faultPutHex(b, pc, 16);
}

void faultReportRefreshModules(void) {}

void faultReportInstall(void) {
    signal(SIGSEGV, faultHandler);
    signal(SIGFPE, faultHandler);
    signal(SIGILL, faultHandler);
    signal(SIGABRT, faultHandler);
}

#else

/* ---- the module table, taken at install time ---- */

#define FAULT_MODS 512
#define FAULT_FRAMES 48
#define FAULT_NAME 40

typedef struct {
    unsigned long long lo, hi, bias;
    const ElfW(Phdr) *ph;
    int phnum;
    char name[FAULT_NAME];
} FaultMod;

static FaultMod sMods[FAULT_MODS];
static int sModCount;
static char sUsed[FAULT_MODS];
static volatile int sBusy;
static int sFd = -1;
static void *sPcs[FAULT_FRAMES];

static int collectMod(struct dl_phdr_info *info, size_t size, void *data) {
    FaultMod *m;
    const char *base = info->dlpi_name != NULL ? info->dlpi_name : "";
    const char *q;
    int i, loads = 0;

    (void)size;
    if (sModCount >= FAULT_MODS) {
        return 1;
    }
    if (data != NULL) { /* a refresh: the modules already in the table stay as they are (a crash may read it meanwhile) */
        for (i = 0; i < sModCount; i++) {
            if (sMods[i].bias == (unsigned long long)info->dlpi_addr && sMods[i].ph == info->dlpi_phdr) {
                return 0;
            }
        }
    }
    m = &sMods[sModCount];
    m->lo = ~0ULL;
    m->hi = 0;
    m->bias = (unsigned long long)info->dlpi_addr;
    m->ph = info->dlpi_phdr;
    m->phnum = info->dlpi_phnum;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        unsigned long long lo, hi;

        if (ph->p_type != PT_LOAD) {
            continue;
        }
        lo = m->bias + (unsigned long long)ph->p_vaddr;
        hi = lo + (unsigned long long)ph->p_memsz;
        if (lo < m->lo) {
            m->lo = lo;
        }
        if (hi > m->hi) {
            m->hi = hi;
        }
        loads++;
    }
    if (loads == 0) {
        return 0;
    }
    for (q = base; *q != '\0'; q++) {
        if (*q == '/') {
            base = q + 1;
        }
    }
    snprintf(m->name, sizeof(m->name), "%s", *base != '\0' ? base : "exe"); /* glibc names the main program "" */
    sModCount++;
    return 0;
}

static int findMod(unsigned long long pc) {
    int i;

    for (i = 0; i < sModCount; i++) {
        if (pc >= sMods[i].lo && pc < sMods[i].hi) {
            return i;
        }
    }
    return -1;
}

/* " module+0xOFF" for an address inside a known module; marks the module as seen so its build id is printed once at the end */
static void putLoc(FaultBuf *b, unsigned long long pc) {
    int m = findMod(pc);

    if (m < 0) {
        return;
    }
    sUsed[m] = 1;
    faultPutStr(b, " ");
    faultPutStr(b, sMods[m].name);
    faultPutStr(b, "+0x");
    faultPutHex(b, pc - sMods[m].bias, 0);
}

void faultPutFrame(FaultBuf *b, int n, unsigned long long pc) {
    faultPutStr(b, "  #");
    if (n < 10) {
        faultPutStr(b, "0");
    }
    faultPutDec(b, n);
    faultPutStr(b, " pc ");
    faultPutHex(b, pc, 16);
    putLoc(b, pc);
}

/* the GNU build id of a module as hex into b (nothing when it has none); reads the notes straight from the mapped segments */
static int putBuildId(FaultBuf *b, const FaultMod *m) {
    int i;

    for (i = 0; i < m->phnum; i++) {
        const ElfW(Phdr) *ph = &m->ph[i];
        const unsigned char *p, *end;

        if (ph->p_type != PT_NOTE) {
            continue;
        }
        p = (const unsigned char *)(m->bias + (unsigned long long)ph->p_vaddr);
        end = p + ph->p_filesz;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
            const ElfW(Nhdr) *nh = (const ElfW(Nhdr) *)p;
            const unsigned char *name = p + sizeof(ElfW(Nhdr));
            const unsigned char *desc = name + ((nh->n_namesz + 3u) & ~3u);
            const unsigned char *next = desc + ((nh->n_descsz + 3u) & ~3u);
            unsigned k;

            if (next > end || next < p) {
                break;
            }
            if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && memcmp(name, "GNU", 4) == 0) {
                for (k = 0; k < nh->n_descsz; k++) {
                    faultPutHex(b, desc[k], 2);
                }
                return 1;
            }
            p = next;
        }
    }
    return 0;
}

/* ---- the report ---- */

static const char *sigName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGABRT: return "SIGABRT";
    default: return "?";
    }
}

void faultReportSetFd(int fd) {
    sFd = fd;
}

/* one finished line (the buffer ends without a newline): to the log file's descriptor, and to logcat on Android */
static void emit(int fd, FaultBuf *b) {
    const char *s = b->p;
    int left = b->n;

#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_FATAL, "dw3", b->p);
#endif
    while (left > 0) {
        ssize_t w = write(fd, s, (size_t)left);

        if (w <= 0) {
            break;
        }
        s += w;
        left -= (int)w;
    }
    if (left == 0) {
        ssize_t w = write(fd, "\n", 1);

        (void)w;
    }
    faultBufInit(b, b->p, b->cap);
}

#ifdef __ANDROID__
static _Unwind_Reason_Code unwindStep(struct _Unwind_Context *ctx, void *arg) {
    int *n = (int *)arg;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);

    if (pc == 0 || *n >= FAULT_FRAMES) {
        return _URC_END_OF_STACK;
    }
    sPcs[(*n)++] = (void *)pc;
    return _URC_NO_REASON;
}
#endif

static int collectFrames(void) {
#ifdef __ANDROID__
    int n = 0;

    _Unwind_Backtrace(unwindStep, &n);
    return n;
#else
    return backtrace(sPcs, FAULT_FRAMES);
#endif
}

static void faultHandler(int sig, siginfo_t *si, void *uctx) {
    char mem[320];
    FaultBuf b;
    char tname[20];
    int fd = sFd >= 0 ? sFd : plogLogFd();
    int i, n;
    ucontext_t *uc = (ucontext_t *)uctx;

    if (__sync_lock_test_and_set(&sBusy, 1)) { /* a second fault while reporting (or on another thread): no report, just die */
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    if (fd < 0) {
        fd = 2;
    }
    memset(sUsed, 0, sizeof(sUsed));
    memset(tname, 0, sizeof(tname));
    prctl(PR_GET_NAME, tname, 0, 0, 0);

    faultBufInit(&b, mem, (int)sizeof(mem));
    faultPutStr(&b, "\nboot: fatal signal ");
    faultPutDec(&b, sig);
    faultPutStr(&b, " (");
    faultPutStr(&b, sigName(sig));
    faultPutStr(&b, ") code ");
    faultPutDec(&b, si != NULL ? si->si_code : 0);
    faultPutStr(&b, " fault addr 0x");
    faultPutHex(&b, si != NULL ? (unsigned long long)(uintptr_t)si->si_addr : 0ULL, 16);
    faultPutStr(&b, " tid ");
    faultPutDec(&b, (long long)syscall(SYS_gettid));
    faultPutStr(&b, " thread \"");
    faultPutStr(&b, tname);
    faultPutStr(&b, "\"");
    emit(fd, &b);

#if defined(__aarch64__)
    if (uc != NULL) {
        faultPutStr(&b, "boot: pc ");
        faultPutHex(&b, (unsigned long long)uc->uc_mcontext.pc, 16);
        putLoc(&b, (unsigned long long)uc->uc_mcontext.pc);
        faultPutStr(&b, " lr ");
        faultPutHex(&b, (unsigned long long)uc->uc_mcontext.regs[30], 16);
        faultPutStr(&b, " sp ");
        faultPutHex(&b, (unsigned long long)uc->uc_mcontext.sp, 16);
        emit(fd, &b);
    }
#elif defined(__x86_64__) && defined(__linux__)
    if (uc != NULL) {
        faultPutStr(&b, "boot: pc ");
        faultPutHex(&b, (unsigned long long)uc->uc_mcontext.gregs[REG_RIP], 16);
        putLoc(&b, (unsigned long long)uc->uc_mcontext.gregs[REG_RIP]);
        faultPutStr(&b, " sp ");
        faultPutHex(&b, (unsigned long long)uc->uc_mcontext.gregs[REG_RSP], 16);
        emit(fd, &b);
    }
#else
    (void)uc;
#endif

    n = collectFrames();
    for (i = 0; i < n; i++) {
        faultPutFrame(&b, i, (unsigned long long)(uintptr_t)sPcs[i]);
        emit(fd, &b);
    }
    for (i = 0; i < sModCount; i++) {
        if (!sUsed[i]) {
            continue;
        }
        faultPutStr(&b, "boot: module ");
        faultPutStr(&b, sMods[i].name);
        faultPutStr(&b, " build-id ");
        if (!putBuildId(&b, &sMods[i])) {
            faultPutStr(&b, "none");
        }
        emit(fd, &b);
    }
    faultPutStr(&b, "boot: end of report");
    emit(fd, &b);

    signal(sig, SIG_DFL);
    raise(sig);
}

/* Libraries loaded after the start (the audio stack's, for one) get a name in the report. Only appends, never from the handler. */
void faultReportRefreshModules(void) {
    static int one = 1;

    dl_iterate_phdr(collectMod, &one);
}

void faultReportInstall(void) {
    static const int sigs[] = {SIGSEGV, SIGFPE, SIGBUS, SIGILL, SIGABRT};
    static char altStack[64 * 1024];
    stack_t ss;
    struct sigaction sa;
    void *warm[2];
    unsigned i;

    sModCount = 0;
    sBusy = 0;
    dl_iterate_phdr(collectMod, NULL);
#ifndef __ANDROID__
    backtrace(warm, 2); /* the first call loads libgcc: not in the handler */
#else
    (void)warm;
#endif
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, NULL); /* per thread: the one that installs it (the game's) gets a stack that survives a stack overflow */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = faultHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        sigaction(sigs[i], &sa, NULL);
    }
}

#endif
