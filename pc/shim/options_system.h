#ifndef PC_OPTIONS_SYSTEM_H
#define PC_OPTIONS_SYSTEM_H

#include <stddef.h>

/*
 * The options menu's System tab (feature system-menu): "Restart game" and "Quit game". Each row
 * asks for a second Confirm before it acts. The first Confirm arms the row and its value reads
 * "Press again to confirm"; a second Confirm on the same row within OPT_SYS_CONFIRM_MS acts.
 * Moving the cursor, changing the tab, or closing the menu disarms it (the screen's navigation
 * epoch changes), and so does waiting too long.
 *
 * Quit pushes an SDL_QUIT event: the same path as closing the window (PsyX_Exit, exit(0), the
 * atexit handlers). Restart does the same, and the last atexit handler (registered before
 * PsyCross starts, so it runs after PsyCross has closed the audio and the window) starts the
 * program again with the same command line: execv on Linux (through $APPIMAGE when set),
 * CreateProcess on Windows.
 */

enum { OPT_SYS_RESTART = 0, OPT_SYS_QUIT, OPT_SYS_LOG, OPT_SYS_ROWS };

#define OPT_SYS_CONFIRM_MS 4000u

/* ---- pure confirmation model (inputtest drives it) ---- */

typedef struct OptSysConfirm {
    int row;         /* the armed row, -1 none */
    unsigned epoch;  /* csNavEpoch() when it was armed */
    unsigned at;     /* the time (ms) when it was armed */
} OptSysConfirm;

void optSysConfirmInit(OptSysConfirm *c);
/* 1 when `row` is armed and nothing moved or expired since */
int optSysArmed(const OptSysConfirm *c, int row, unsigned epoch, unsigned nowMs);
/* A Confirm on `row`: 1 when it is the second one (the action runs, the row disarms), 0 when it armed the row */
int optSysPress(OptSysConfirm *c, int row, unsigned epoch, unsigned nowMs);

/* ---- the tab ---- */

/* ---- the restart's handoff (Windows: CreateProcess, the new process waits for the old one) ---- */

#define OPT_SYS_WAIT_MS 30000u

/* Removes `--wait-pid <n>` from the command line. 1 and *pid = n when it was there with a valid number */
int optSysTakeWaitPid(int *argc, char **argv, unsigned long *pid);
/* Blocks until process `pid` has ended: the milliseconds waited (0 when it was already gone), -1 after timeoutMs */
long optSysWaitForPid(unsigned long pid, unsigned timeoutMs);
/* The restart's trace file (<logs>/restart-trace.txt): each step of the handoff appended as one line, from both processes, also
   before the new process opens its log. Set before optionsSystemTakeParent; unset, nothing is written (restarttest) */
void optionsSystemSetTraceFile(const char *path);
/* First thing in main: strips `--wait-pid <n>` and waits (OPT_SYS_WAIT_MS at most) for that process, before the log, SDL or OpenAL */
void optionsSystemTakeParent(int *argc, char **argv);
/* Once the log exists: one `system: restarted ...` line when this start followed a restart, else nothing */
void optionsSystemRestartNote(void);

/* The program's own command line, saved first thing in main (the restart repeats it) */
void optionsSystemSetArgs(int argc, char **argv);
/* Arms the restart: the handler registered below starts the program again when the process ends (the Restart row; restarttest) */
void optionsSystemRestartAtExit(void);
/* Registers the restart's atexit handler: call before PsyCross starts so it runs last */
void optionsSystemRegisterExit(void);
/* The "Prepare modding files" row (modding-ux U4): the four heavy exporters behind one row. It exists only when boot registered at
   least one of them before optionsSystemInit, so the tab builds without the exporters (inputtest). Fields:
   start: queues everything and rebuilds it (>0 queued, else nothing); startIfNeeded: the same but skips what is up to date (the row
   and --export-all use it, so a second press is quick; NULL falls back to start); pump: one slice of the job, called every frame;
   status: "Exporting 12/248", "Done (248)"; active: 1 while it runs; failed: how many items of the last job failed (may be NULL) */
typedef struct OptSysExport {
    int (*start)(void);
    void (*pump)(void);
    const char *(*status)(void);
    int (*active)(void);
    int (*startIfNeeded)(void);
    int (*failed)(void);
} OptSysExport;
/* The models (model-import I7), videos (content-export E2a), audio (E3a) and stages (E6b) exporters; NULL unregisters */
void optionsSystemSetExport(const OptSysExport *e);
void optionsSystemSetExportVideos(const OptSysExport *e);
void optionsSystemSetExportAudio(const OptSysExport *e);
void optionsSystemSetExportStages(const OptSysExport *e);
/* Starts the job of the row: videos, audio, models, stages one after the other, each skipping what is up to date. 1 started, 0 when
   no exporter is registered or the job is already running. The step that runs shows as "Videos 3/14", the end as "Done" or "Done, 2 failed" */
int optionsSystemPrepStart(void);
int optionsSystemPrepActive(void);
/* Items that failed in the last job */
int optionsSystemPrepFailed(void);
const char *optionsSystemPrepStatus(void);
/* The command line's --export-all (no window): runs the job of the row to its end, printing "export-all: <status>" lines to stdout
   (a new step at once, the counts of a step twice a second). 0 when everything went through, 1 when any item failed or no exporter exists */
int optionsSystemExportAllRun(void);
/* Once per frame (controlsScreenFrame): the exporters' slices and the job's next step */
void optionsSystemFrame(void);
/* The "Import memory card" row (M2 / R4): boot registers the import (shim/host_memcard.h) before optionsSystemInit; without it the
   row does not exist, so the tab builds without the card (inputtest). import: 1 when saves were imported, *msg a one-line result
   either way; dropDir: the folder where the player puts a card ("" when unknown). The row asks for a second Confirm (it replaces the
   saves; the old ones are kept in a backup folder) and then shows the result. NULL unregisters. */
typedef struct OptSysCardImport {
    int (*import)(char *msg, size_t cap);
    const char *(*dropDir)(void);
} OptSysCardImport;
void optionsSystemSetCardImport(const OptSysCardImport *c);
/* Adds the rows to the System tab: Restart game, Quit game and the Log level (off, mods, trace) */
void optionsSystemInit(void);

#endif
