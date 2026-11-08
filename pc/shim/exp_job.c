/* The job engine of the exporters; see exp_job.h */
#include "exp_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "port_log.h"

struct ExpJobSys {
    SDL_mutex *mu;
    SDL_cond *cv;
    SDL_Thread *thread;
};

void expJobLock(ExpJob *job) {
    if (job->sys != NULL && job->sys->mu != NULL) SDL_LockMutex(job->sys->mu);
}

void expJobUnlock(ExpJob *job) {
    if (job->sys != NULL && job->sys->mu != NULL) SDL_UnlockMutex(job->sys->mu);
}

void expJobWake(ExpJob *job) {
    if (job->sys != NULL && job->sys->cv != NULL) SDL_CondBroadcast(job->sys->cv);
}

int expJobSize(const ExpJob *job) { return job->count; }

int expJobItem(const ExpJob *job, int pos) { return job->items[pos]; }

volatile int *expJobStopping(ExpJob *job) { return &job->stop; }

int expJobBusy(const ExpJob *job) {
    int busy;
    ExpJob *j = (ExpJob *)job;

    if (j->sys == NULL || j->sys->thread == NULL) return 0;
    expJobLock(j);
    busy = !j->workerDone;
    expJobUnlock(j);
    return busy;
}

static void setStatus(ExpJob *job) { snprintf(job->status, sizeof job->status, "Exporting %d/%d", job->next + job->kind->shown, job->count); }

static void finishJob(ExpJob *job) {
    if (job->failed) snprintf(job->status, sizeof job->status, "Done, %d failed", job->failed);
    else snprintf(job->status, sizeof job->status, "Done (%d)", job->done);
    if (job->kind->finish != NULL) job->kind->finish(job->kind->ud, job->done, job->failed);
}

/* a failure that comes while stopping is the stop, not the item */
static void count(ExpJob *job, int r) {
    if (r == EXP_JOB_BUILT_FAILED) {
        job->built++;
        if (!job->stop) job->failed++;
    } else if (r < 0) {
        if (!job->stop) job->failed++;
    } else {
        job->done++;
        if (r == EXP_JOB_BUILT) job->built++;
    }
}

static void flush(const ExpJobKind *k) {
    if (k->flush != NULL) k->flush(k->ud);
}

static int workerMain(void *arg) {
    ExpJob *job = (ExpJob *)arg;
    const ExpJobKind *k = job->kind;
    int j, since = 0;

    for (j = 0; j < job->count && !job->stop; j++) {
        int r;

        if (k->ready != NULL) {
            SDL_LockMutex(job->sys->mu);
            while (!job->stop && !k->ready(k->ud, j)) SDL_CondWait(job->sys->cv, job->sys->mu);
            SDL_UnlockMutex(job->sys->mu);
            if (job->stop) break;
        }
        r = (k->oneWorker != NULL ? k->oneWorker : k->one)(k->ud, j, job->items[j], job->force);
        SDL_LockMutex(job->sys->mu);
        if (k->retire != NULL) k->retire(k->ud, j);
        count(job, r);
        job->next = j + 1;
        if (job->next < job->count) setStatus(job);
        SDL_CondBroadcast(job->sys->cv);
        SDL_UnlockMutex(job->sys->mu);
        if (k->flushEvery > 0 && ++since >= k->flushEvery) {
            flush(k);
            since = 0;
        }
    }
    flush(k);
    SDL_LockMutex(job->sys->mu);
    if (j >= job->count) finishJob(job);
    job->workerDone = 1;
    SDL_UnlockMutex(job->sys->mu);
    return 0;
}

/* join a finished (or stopped) worker and free what the job held */
static void reap(ExpJob *job) {
    if (job->sys == NULL || job->sys->thread == NULL) return;
    SDL_WaitThread(job->sys->thread, NULL);
    job->sys->thread = NULL;
    if (job->kind->release != NULL) job->kind->release(job->kind->ud);
    free(job->items);
    job->items = NULL;
}

void expJobDrop(ExpJob *job) {
    free(job->items);
    job->items = NULL;
    job->count = job->next = 0;
}

void expJobShutdown(ExpJob *job) {
    if (job->sys == NULL || job->sys->thread == NULL) return;
    expJobLock(job);
    job->stop = 1;
    expJobWake(job);
    expJobUnlock(job);
    reap(job);
    job->stop = 0;
}

void expJobSetup(ExpJob *job, const ExpJobKind *kind) {
    job->kind = kind;
    expJobShutdown(job);
    expJobDrop(job);
    job->status[0] = '\0';
}

int expJobBegin(ExpJob *job, int force) {
    if (job->kind == NULL || expJobBusy(job)) return -1; /* one job at a time: a press while one runs changes nothing */
    reap(job);
    expJobDrop(job);
    job->done = job->failed = job->built = 0;
    job->force = force;
    job->status[0] = '\0';
    return 0;
}

int expJobQueue(ExpJob *job, int *items, int n) {
    job->items = items;
    job->count = n;
    job->next = job->since = 0;
    setStatus(job);
    return n;
}

int expJobStart(ExpJob *job) {
    struct ExpJobSys *sys;

    if (job->kind == NULL || job->items == NULL) return -1;
    if (job->sys == NULL) job->sys = (struct ExpJobSys *)calloc(1, sizeof *job->sys);
    sys = job->sys;
    if (sys != NULL) {
        if (sys->mu == NULL) sys->mu = SDL_CreateMutex();
        if (sys->cv == NULL) sys->cv = SDL_CreateCond();
    }
    if (sys != NULL && sys->mu != NULL && sys->cv != NULL) {
        job->stop = 0;
        job->workerDone = 0;
        sys->thread = SDL_CreateThread(workerMain, job->kind->thread, job);
    }
    if (sys == NULL || sys->thread == NULL) {
        plogLine(job->kind->tag, "the export thread cannot start: %s\n", SDL_GetError());
        if (job->kind->release != NULL) job->kind->release(job->kind->ud);
        expJobDrop(job);
        return -1;
    }
    return 0;
}

int expJobActive(const ExpJob *job) {
    if (job->sys != NULL && job->sys->thread != NULL) return expJobBusy(job);
    return job->items != NULL && job->next < job->count;
}

int expJobPump(ExpJob *job, unsigned budgetMs) {
    const ExpJobKind *k = job->kind;
    clock_t start = clock();

    if (k == NULL) return 0;
    if (job->sys != NULL && job->sys->thread != NULL) {
        if (k->feed != NULL) k->feed(k->ud);
        if (expJobBusy(job)) return 1;
        reap(job);
        return 0;
    }
    if (!expJobActive(job)) return 0;
    do {
        count(job, k->one(k->ud, job->next, job->items[job->next], job->force));
        job->next++;
        if (k->flushEvery > 0 && ++job->since >= k->flushEvery) {
            flush(k);
            job->since = 0;
        }
        if (job->next < job->count) setStatus(job);
    } while (job->next < job->count && (unsigned)((clock() - start) * 1000 / CLOCKS_PER_SEC) < budgetMs);
    if (job->next < job->count) return 1;
    flush(k);
    finishJob(job);
    free(job->items);
    job->items = NULL;
    return 0;
}

void expJobRun(ExpJob *job) {
    while (expJobPump(job, 1000)) {
    }
}

void expJobCounts(ExpJob *job, int *done, int *failed, int *total) {
    expJobLock(job);
    if (done) *done = job->done;
    if (failed) *failed = job->failed;
    if (total) *total = job->count;
    expJobUnlock(job);
}

int expJobBuilt(ExpJob *job) {
    int n;
    expJobLock(job);
    n = job->built;
    expJobUnlock(job);
    return n;
}

const char *expJobStatus(ExpJob *job) {
    expJobLock(job);
    snprintf(job->statusOut, sizeof job->statusOut, "%s", job->status);
    expJobUnlock(job);
    return job->statusOut;
}
