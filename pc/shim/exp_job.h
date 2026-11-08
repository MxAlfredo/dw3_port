/* The one job engine of the exporters that run a queue of items on demand (B2; stages, audio, video, models): the queue, the
   optional worker thread, the pump with a time budget, the counts and the status text, the shutdown and the synchronous run.
   An exporter only supplies an ExpJobKind: how to build one item and a few optional hooks. SDL threads; nothing else of the port. */
#ifndef EXP_JOB_H
#define EXP_JOB_H

#include <stddef.h>

/* what building one item returns */
enum {
    EXP_JOB_FAILED = -1,
    EXP_JOB_SKIPPED = 0,       /* up to date: nothing built (counts as done) */
    EXP_JOB_BUILT = 1,         /* built (counts as done and as built) */
    EXP_JOB_BUILT_FAILED = 2   /* built, but some of its files failed (counts as built and as failed, not as done) */
};

typedef struct ExpJobKind {
    const char *tag;    /* log tag ("audio") */
    const char *thread; /* name of the worker thread ("dw3-audio-export") */
    int shown;          /* "Exporting n/total": n = items done + shown (1: the item in progress, 0: the items done) */
    int flushEvery;     /* flush after this many items (0: only when the job ends) */
    void *ud;           /* handed to every hook */
    /* Builds item `item` (the value queued at position `pos`). an EXP_JOB_ value. Runs on the thread
       that pumps (no worker), or on the worker thread when `oneWorker` is NULL. */
    int (*one)(void *ud, int pos, int item, int force);
    /* The same for the worker thread, when it must not build as the pumping thread does (NULL: `one`). */
    int (*oneWorker)(void *ud, int pos, int item, int force);
    /* Writes what the exporter keeps in memory (its manifest): after `flushEvery` items and when the job ends, stopped or not. */
    void (*flush)(void *ud);
    /* The job ran to the end (not stopped): the log line. The status text is already set. */
    void (*finish)(void *ud, int done, int failed);
    /* Worker side: nonzero when the item at `pos` can be built now (called with the lock held; NULL: always). */
    int (*ready)(void *ud, int pos);
    /* Worker side: the item at `pos` is built (called with the lock held, before it is counted); frees what `ready` waited for. */
    void (*retire)(void *ud, int pos);
    /* Main thread, from every pump while a worker lives: feeds the worker. */
    void (*feed)(void *ud);
    /* After the worker is joined, or could not start: frees what the threaded job held. */
    void (*release)(void *ud);
} ExpJobKind;

struct ExpJobSys;

/* One job. Declare it static (zero), then expJobSetup(). Read it only through the functions below. */
typedef struct ExpJob {
    const ExpJobKind *kind;
    int *items;                 /* the queue, malloc'ed; NULL once the job is over */
    int count, next;
    int done, failed, built, force, since;
    volatile int stop;          /* a running worker stops between two items (see expJobStopping) */
    int workerDone;
    char status[96], statusOut[96];
    struct ExpJobSys *sys;      /* mutex, condition and thread, made by the first threaded job */
} ExpJob;

/* Binds the job to its kind. Stops and joins a running worker, drops the queue and clears the status (the counts stay). */
void expJobSetup(ExpJob *job, const ExpJobKind *kind);

/* A new job: -1 when a worker is still running (one at a time); else the old one is dropped, the counts reset and 0 returned. */
int expJobBegin(ExpJob *job, int force);
/* Queues `n` items (malloc'ed by the caller, now owned by the job) and sets the status "Exporting 1/n". Returns n. */
int expJobQueue(ExpJob *job, int *items, int n);
/* Gives up a job that was begun or queued (its queue is freed) */
void expJobDrop(ExpJob *job);

/* The menu's job: a worker thread builds the queue (call expJobPump every frame). 0, or -1 when the thread cannot start (the
   job is then dropped). */
int expJobStart(ExpJob *job);
/* Stops a running worker between two items and joins it; atexit-safe. */
void expJobShutdown(ExpJob *job);
/* Builds queued items for about budgetMs milliseconds (at least one) when there is no worker; with one it feeds it and joins it
   at the end. 1 while work remains, 0 when the job is over. */
int expJobPump(ExpJob *job, unsigned budgetMs);
/* The synchronous run of the settings.ini keys: pumps until the job is over. */
void expJobRun(ExpJob *job);

int expJobActive(const ExpJob *job);
/* a worker is running */
int expJobBusy(const ExpJob *job);
void expJobCounts(ExpJob *job, int *done, int *failed, int *total);
/* how many items were really built (the rest were skipped as up to date) */
int expJobBuilt(ExpJob *job);
/* "" before any job, "Exporting 3/237", "Done (237)", "Done, 2 failed"; valid until the next call */
const char *expJobStatus(ExpJob *job);

/* for the hooks and the exporters: the lock that guards the counters (a no-op until the first threaded job) and the stop request
   a build can poll */
void expJobLock(ExpJob *job);
void expJobUnlock(ExpJob *job);
volatile int *expJobStopping(ExpJob *job);
/* the queue, for a hook that looks ahead of the worker (constant while a job runs) */
int expJobSize(const ExpJob *job);
int expJobItem(const ExpJob *job, int pos);
/* the worker's condition: wake it after changing what `ready` looks at (call with the lock held) */
void expJobWake(ExpJob *job);

#endif
