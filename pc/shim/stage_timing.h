#ifndef PC_STAGE_TIMING_H
#define PC_STAGE_TIMING_H

/*
 * Stage change timing trace (host side, opt-in, off by default). Its lines are the "stage" channel of
 * the port log (port_log.h): `[stage] t=<ms> f=<frame> <event>`; trace level with that channel shows
 * them, and DW3_STAGE_TIMING=1 is the shortcut. Levels 2 and 3 below also drive the run, and only
 * the variable sets them.
 *
 *   DW3_STAGE_TIMING=1   one stderr line per event: "[stage] t=<ms> f=<frame> <event>"
 *                        (t is milliseconds since the first event, monotonic; f the game's
 *                        60 Hz frame count). Events: every disc file read (index, sectors,
 *                        bytes, ms spent reading), mode changes, the field task's and the
 *                        area banner's state changes, and the player's frozen flag.
 *
 *   DW3_STAGE_TIMING=2   the same, and the run ends by itself once the banner is gone and the
 *                        player is unfrozen (for batch measurements)
 *
 *   DW3_STAGE_TIMING=3   the same, and 30 frames after the first stage is playable the run leaves
 *                        through the stage's first exit slot, as the door's cross press does, and
 *                        ends once the next stage is playable (times the exit and the entry)
 *
 * Every level also logs "playable" on the frame the player is free (field modes), and, in the battle
 * (0x600) and its report (0x1400), the live count and state of the command and tech menus, the
 * message boxes, the player turn, the camera and the report tasks, on every change.
 *
 * With the variable unset every hook costs one flag test. The read hook lives in header
 * inlines so host_cd.c (linked into several tools that do not have the game's tasks) needs no
 * extra object; only the per-frame poll (stageTimingFrame, from hostDrawFrame) is in
 * stage_timing.c.
 */

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "port_log.h"

/* declared here, not from <stdlib.h>, whose random() clashes with the game headers' */
extern char *getenv(const char *name);

/* The game frame the last poll saw; defined in host_cd.c (always linked) */
extern int stageTimingFrameNo;
/* The time of the first event, so every object counts from the same zero; defined in host_cd.c */
extern double stageTimingT0;
/* The clock when the process began (a constructor in host_cd.c), -1 with the trace off */
extern double stageTimingBootMs;

static inline double stageTimingNowMs(void) {
#ifdef CLOCK_MONOTONIC
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    }
#endif
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

/* 0 off, 1 trace, 2 trace and end the run once the stage is playable, 3 exit probe */
static inline int stageTimingLevel(void) {
    static int level = -1;

    if (level < 0) {
        const char *env = getenv("DW3_STAGE_TIMING");

        level = (env == NULL || env[0] == '\0' || env[0] == '0') ? 0 : (env[0] == '2' ? 2 : (env[0] == '3' ? 3 : 1));
    }
    return level;
}

/* On with DW3_STAGE_TIMING (any level) or with trace on the stage channel of the port log */
static inline int stageTimingOn(void) {
    return stageTimingLevel() != 0 || plogOn("stage", PLOG_TRACE);
}

static inline void stageTimingLog(const char *fmt, ...) {
    double now = stageTimingNowMs();
    va_list ap;

    if (stageTimingT0 < 0.0) {
        stageTimingT0 = now;
    }
    plogLine("stage", "t=%.1f f=%d ", now - stageTimingT0, stageTimingFrameNo);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Returns nonzero when the run should end: DW3_STAGE_TIMING=2 ends it by state, on the frame the
   area banner is gone and the player is unfrozen (a run that never shows a banner never ends) */
/* Once per presented frame, after the frame counters moved (gfx_engine.c's hostDrawFrame) */
int stageTimingFrame(int frame);

#endif
