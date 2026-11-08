/*
 * Host test of the fatal-signal report (pc/shim/fault_report.c, task R1): the signal-safe formatter, the crash-test argument and a
 * forked child per fault kind that crashes into a scratch log file, whose report the parent reads back.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#define _GNU_SOURCE 1
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shim/fault_report.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "faulttest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static void formatter(void) {
    char mem[64];
    FaultBuf b;

    faultBufInit(&b, mem, sizeof(mem));
    faultPutDec(&b, 0);
    faultPutStr(&b, "|");
    faultPutDec(&b, 4242);
    faultPutStr(&b, "|");
    faultPutDec(&b, -7);
    faultPutStr(&b, "|");
    faultPutDec(&b, LLONG_MIN);
    CHECK(strcmp(mem, "0|4242|-7|-9223372036854775808") == 0);

    faultBufInit(&b, mem, sizeof(mem));
    faultPutHex(&b, 0, 0);
    faultPutStr(&b, "|");
    faultPutHex(&b, 0xabcdefULL, 0);
    faultPutStr(&b, "|");
    faultPutHex(&b, 0x1a2bULL, 16);
    faultPutStr(&b, "|");
    faultPutHex(&b, 0xffffffffffffffffULL, 16);
    CHECK(strcmp(mem, "0|abcdef|0000000000001a2b|ffffffffffffffff") == 0);

    faultBufInit(&b, mem, 8); /* a full buffer drops the rest and stays terminated */
    faultPutStr(&b, "0123456789");
    faultPutDec(&b, 99);
    CHECK(strcmp(mem, "0123456") == 0 && b.n == 7);
    faultBufInit(&b, mem, 1);
    faultPutStr(&b, "x");
    CHECK(mem[0] == '\0' && b.n == 0);
}

static void crashArg(void) {
    char *a[] = {"boot", "disc.bin", "--crash-test=fpe", "10", NULL};
    char *c[] = {"boot", "--crash-test", NULL};
    char *d[] = {"boot", "disc.bin", "5", NULL};
    char *e[] = {"boot", "--crash-testing", "--crash-test=", NULL};
    int ac = 4, cc = 2, dc = 3, ec = 3;

    CHECK(strcmp(faultReportTakeCrashArg(&ac, a), "fpe") == 0 && ac == 3 && strcmp(a[2], "10") == 0 && a[3] == NULL);
    CHECK(strcmp(faultReportTakeCrashArg(&cc, c), "segv") == 0 && cc == 1 && c[1] == NULL);
    CHECK(faultReportTakeCrashArg(&dc, d) == NULL && dc == 3);
    CHECK(strcmp(faultReportTakeCrashArg(&ec, e), "segv") == 0 && ec == 2 && strcmp(e[1], "--crash-testing") == 0); /* exact word, empty kind = segv */
    CHECK(faultReportTakeCrashArg(NULL, a) == NULL && faultReportCrash("nope") == 0 && faultReportCrash(NULL) == 0);
}

static void frameLine(void) {
    char mem[200];
    FaultBuf b;

    faultReportInstall();
    faultBufInit(&b, mem, sizeof(mem));
    faultPutFrame(&b, 3, (unsigned long long)(size_t)&frameLine);
    CHECK(strncmp(mem, "  #03 pc 0000", 13) == 0);
    CHECK(strstr(mem, " exe+0x") != NULL); /* the test program itself is a known module, named "exe" like glibc's empty name */
    faultBufInit(&b, mem, sizeof(mem));
    faultPutFrame(&b, 12, 0x10ULL); /* no module there: only the address */
    CHECK(strcmp(mem, "  #12 pc 0000000000000010") == 0);
}

static char *slurp(const char *path) {
    static char buf[16384];
    FILE *f = fopen(path, "rb");
    size_t n;

    buf[0] = '\0';
    if (f == NULL) {
        return buf;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void crashChild(void) {
    static const struct {
        const char *kind;
        int sig;
        const char *name;
    } kinds[] = {{"segv", SIGSEGV, "SIGSEGV"}, {"abort", SIGABRT, "SIGABRT"}, {"fpe", SIGFPE, "SIGFPE"},
                 {"ill", SIGILL, "SIGILL"}, {"bus", SIGBUS, "SIGBUS"}};
    char path[200];
    unsigned i;

    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        pid_t pid;
        int status = 0;

        snprintf(path, sizeof(path), "/tmp/dw3-faulttest-%d-%s.log", (int)getpid(), kinds[i].kind);
        remove(path);
        pid = fork();
        if (pid == 0) {
            int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);

            if (fd < 0) {
                _exit(90);
            }
            dprintf(fd, "earlier log line\n");
            faultReportSetFd(fd);
            faultReportInstall();
            faultReportCrash(kinds[i].kind);
            _exit(91); /* not reached */
        }
        CHECK(pid > 0 && waitpid(pid, &status, 0) == pid);
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == kinds[i].sig); /* it still dies of the same signal */
        {
            char *log = slurp(path);
            char want[80];
            char *id;

            snprintf(want, sizeof(want), "boot: fatal signal %d (%s) code ", kinds[i].sig, kinds[i].name);
            CHECK(strncmp(log, "earlier log line\n", 17) == 0); /* appended to the log, not over it */
            CHECK(strstr(log, want) != NULL);
            CHECK(strstr(log, " tid ") != NULL && strstr(log, " thread \"") != NULL);
            CHECK(strstr(log, "\n  #00 pc ") != NULL && strstr(log, " exe+0x") != NULL);
            id = strstr(log, "boot: module exe build-id ");
            CHECK(id != NULL);
            if (id != NULL) {
                id += strlen("boot: module exe build-id ");
                CHECK(strncmp(id, "none", 4) == 0 || (strspn(id, "0123456789abcdef") >= 16 && id[strspn(id, "0123456789abcdef")] == '\n'));
            }
            CHECK(strstr(log, "boot: end of report\n") != NULL);
            if (strcmp(kinds[i].kind, "segv") == 0) {
                CHECK(strstr(log, "fault addr 0x0000000000000000") != NULL); /* the real null write */
                fputs(log, stdout); /* one sample for the notes */
            }
        }
        remove(path);
    }
}

int main(void) {
    formatter();
    crashArg();
    frameLine();
    crashChild();
    if (failures != 0) {
        fprintf(stdout, "faulttest: %d check(s) failed\n", failures);
        return 1;
    }
    fprintf(stdout, "faulttest: ok\n");
    return 0;
}
