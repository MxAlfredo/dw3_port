/*
 * Host test of the exporters' job engine (B2): pc/shim/exp_job.c. No disc, no files; SDL only for the worker thread.
 *
 * Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "shim/exp_job.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "jobtest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* item n: n % 5 == 3 fails, n % 5 == 4 is built with failures, else even builds and odd is skipped as up to date */
typedef struct Rec {
    int ones, flushes, finishes, feeds, retired, released, lastDone, lastFailed;
    int gate; /* the worker may build positions below it (when gated) */
    int order[64];
} Rec;

static Rec R;

static int one(void *ud, int pos, int item, int force) {
    Rec *r = (Rec *)ud;
    (void)force;
    r->order[r->ones++] = pos * 1000 + item;
    if (item % 5 == 3) return EXP_JOB_FAILED;
    if (item % 5 == 4) return EXP_JOB_BUILT_FAILED;
    return item % 2 == 0 ? EXP_JOB_BUILT : EXP_JOB_SKIPPED;
}

static void flush(void *ud) { ((Rec *)ud)->flushes++; }

static void finish(void *ud, int done, int failed) {
    Rec *r = (Rec *)ud;
    r->finishes++;
    r->lastDone = done;
    r->lastFailed = failed;
}

static int ready(void *ud, int pos) { return pos < ((Rec *)ud)->gate; }
static void retire(void *ud, int pos) { (void)pos; ((Rec *)ud)->retired++; }
static void feed(void *ud) { ((Rec *)ud)->feeds++; }
static void release(void *ud) { ((Rec *)ud)->released++; }

static ExpJobKind kPlain = {"jobtest", "dw3-jobtest", 1, 0, &R, one, NULL, NULL, finish, NULL, NULL, NULL, NULL};
static ExpJobKind kFlush = {"jobtest", "dw3-jobtest", 0, 4, &R, one, NULL, flush, finish, NULL, NULL, NULL, NULL};
static ExpJobKind kGated = {"jobtest", "dw3-jobtest", 1, 0, &R, one, NULL, flush, finish, ready, retire, feed, release};

static int *items(int n) {
    int *q = (int *)malloc(sizeof(int) * (size_t)n), i;
    for (i = 0; i < n; i++) q[i] = i;
    return q;
}

static ExpJob job;

static void testSync(void) {
    int done, failed, total;

    memset(&R, 0, sizeof R);
    memset(&job, 0, sizeof job);
    CHECK(expJobBegin(&job, 0) == -1); /* no kind yet */
    CHECK(expJobPump(&job, 10) == 0 && !expJobActive(&job));
    expJobSetup(&job, &kPlain);
    CHECK(!strcmp(expJobStatus(&job), ""));
    CHECK(expJobBegin(&job, 0) == 0);
    CHECK(expJobQueue(&job, items(10), 10) == 10);
    CHECK(expJobActive(&job) && !expJobBusy(&job));
    CHECK(!strcmp(expJobStatus(&job), "Exporting 1/10"));
    /* a zero budget still builds one item */
    CHECK(expJobPump(&job, 0) >= 0 && R.ones >= 1);
    expJobCounts(&job, &done, &failed, &total);
    CHECK(total == 10 && done + failed == R.ones);
    expJobRun(&job);
    expJobCounts(&job, &done, &failed, &total);
    CHECK(R.ones == 10 && done == 6 && failed == 4 && total == 10); /* items 3 and 8 fail, 4 and 9 build with failures */
    CHECK(expJobBuilt(&job) == 5); /* items 0 2 6, and 4 and 9 (built, with failures) */
    CHECK(!strcmp(expJobStatus(&job), "Done, 4 failed"));
    CHECK(R.finishes == 1 && R.lastDone == 6 && R.lastFailed == 4);
    CHECK(!expJobActive(&job) && expJobPump(&job, 10) == 0);
    CHECK(R.order[0] == 0 && R.order[9] == 9009);
}

static void testFlush(void) {
    int i;

    memset(&R, 0, sizeof R);
    expJobSetup(&job, &kFlush);
    CHECK(expJobBegin(&job, 1) == 0 && job.force == 1);
    CHECK(expJobQueue(&job, items(10), 10) == 10);
    CHECK(!strcmp(expJobStatus(&job), "Exporting 0/10")); /* shown 0: the items done */
    for (i = 0; i < 3; i++) CHECK(expJobPump(&job, 0) == 1);
    CHECK(R.flushes == 0);
    CHECK(expJobPump(&job, 0) == 1 && R.flushes == 1); /* after the 4th */
    CHECK(!strcmp(expJobStatus(&job), "Exporting 4/10"));
    expJobRun(&job);
    CHECK(R.flushes == 3); /* 4, 8, and the end */
    CHECK(!strcmp(expJobStatus(&job), "Done, 4 failed"));
    /* a new job resets the counts and the status */
    CHECK(expJobBegin(&job, 0) == 0);
    CHECK(expJobBuilt(&job) == 0 && !strcmp(expJobStatus(&job), "") && !expJobActive(&job));
    expJobDrop(&job);
}

static void testThread(void) {
    int done, failed, total, guard;

    memset(&R, 0, sizeof R);
    expJobSetup(&job, &kGated);
    CHECK(expJobBegin(&job, 0) == 0);
    CHECK(expJobQueue(&job, items(6), 6) == 6);
    CHECK(expJobStart(&job) == 0);
    CHECK(expJobBusy(&job) && expJobActive(&job));
    CHECK(expJobBegin(&job, 0) == -1); /* one job at a time */
    /* the gate holds the worker: nothing is built, the pump feeds */
    SDL_Delay(30);
    CHECK(R.ones == 0 && expJobPump(&job, 8) == 1 && R.feeds == 1);
    expJobLock(&job);
    R.gate = 2;
    expJobWake(&job);
    expJobUnlock(&job);
    for (guard = 0; guard < 500; guard++) {
        expJobCounts(&job, &done, &failed, &total);
        if (done + failed == 2) break;
        SDL_Delay(5);
    }
    CHECK(done + failed == 2 && R.retired == 2 && total == 6);
    CHECK(!strcmp(expJobStatus(&job), "Exporting 3/6"));
    expJobLock(&job);
    R.gate = 6;
    expJobWake(&job);
    expJobUnlock(&job);
    for (guard = 0; guard < 500 && expJobPump(&job, 8); guard++) SDL_Delay(5);
    CHECK(guard < 500 && !expJobActive(&job) && !expJobBusy(&job));
    expJobCounts(&job, &done, &failed, &total);
    CHECK(done == 4 && failed == 2 && R.retired == 6 && R.released == 1); /* item 3 fails, 4 builds with failures */
    CHECK(!strcmp(expJobStatus(&job), "Done, 2 failed") && R.finishes == 1 && R.flushes == 1);
    CHECK(R.order[0] == 0 && R.order[5] == 5005); /* in queue order */
    CHECK(expJobPump(&job, 8) == 0);
}

static void testShutdown(void) {
    int guard, done, failed, total;

    memset(&R, 0, sizeof R);
    expJobSetup(&job, &kGated);
    CHECK(expJobBegin(&job, 0) == 0);
    CHECK(expJobQueue(&job, items(6), 6) == 6);
    CHECK(expJobStart(&job) == 0);
    expJobLock(&job);
    R.gate = 1;
    expJobWake(&job);
    expJobUnlock(&job);
    for (guard = 0; guard < 500 && R.retired < 1; guard++) SDL_Delay(5);
    expJobShutdown(&job); /* the worker is parked on the gate of position 1 */
    CHECK(!expJobBusy(&job) && !expJobActive(&job) && R.released == 1 && job.stop == 0 && job.items == NULL);
    CHECK(R.ones == 1 && R.finishes == 0 && R.flushes == 1); /* stopped: no "Done", the manifest still flushed */
    expJobCounts(&job, &done, &failed, &total);
    CHECK(done == 1 && failed == 0 && total == 6);
    expJobShutdown(&job); /* nothing running: a no-op */
    /* a new job after the stop works */
    CHECK(expJobBegin(&job, 0) == 0);
    CHECK(expJobQueue(&job, items(2), 2) == 2);
    R.gate = 2;
    CHECK(expJobStart(&job) == 0);
    for (guard = 0; guard < 500 && expJobPump(&job, 8); guard++) SDL_Delay(5);
    CHECK(!expJobActive(&job) && !strcmp(expJobStatus(&job), "Done (2)"));
    expJobSetup(&job, &kGated); /* Setup stops and drops too */
}

int main(void) {
    testSync();
    testFlush();
    testThread();
    testShutdown();
    if (failures) {
        fprintf(stdout, "jobtest: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stdout, "jobtest: ok\n");
    return 0;
}
