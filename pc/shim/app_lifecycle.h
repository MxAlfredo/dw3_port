/*
 * App lifecycle (Android): when the app goes to the background the game must freeze (no frames, no vblanks, no sound)
 * and write its settings; on return it continues exactly where it was, without trying to catch up the lost time.
 *
 * This file is the pure state machine (host test: pc/applifetest.c). shim/app_lifecycle_sdl.c feeds it SDL's
 * SDL_APP_* events and applies the actions.
 */
#ifndef DW3_APP_LIFECYCLE_H
#define DW3_APP_LIFECYCLE_H

typedef enum AppLifeEvent {
    APPLIFE_WILL_BACKGROUND,
    APPLIFE_DID_BACKGROUND,
    APPLIFE_WILL_FOREGROUND,
    APPLIFE_DID_FOREGROUND,
    APPLIFE_LOW_MEMORY /* informational: never changes the state */
} AppLifeEvent;

/* What the caller does after an event; the bits may be combined */
enum {
    APPLIFE_ACT_FREEZE = 1, /* stop the vblank clock, pause the audio device */
    APPLIFE_ACT_SAVE = 2,   /* write the settings (Android may kill a background app) */
    APPLIFE_ACT_THAW = 4    /* audio device back, vblank clock rebased (no catch-up) */
};

typedef struct AppLife {
    int frozen;
} AppLife;

void appLifeInit(AppLife *life);
/* The actions for the event. A freeze happens once per background trip (the first event), a thaw once (the first
   DID_FOREGROUND after it: the surface is back by then). WILL_FOREGROUND alone does not thaw. */
int appLifeEvent(AppLife *life, AppLifeEvent event);

/* The next vblank deadline (seconds on the clock) after a freeze that ended at `now`: one step ahead, never the
   old deadline (a stale one would fire all the lost vblanks in a burst). */
double appLifeRebase(double oldNext, double now, double step);

#endif
