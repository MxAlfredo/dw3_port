/*
 * Host test of the app lifecycle state machine (pc/shim/app_lifecycle.c): freeze once per background trip, save with
 * the freeze, thaw only when the app is fully back, and the pacing deadline restarts one step ahead (no catch-up).
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <stdio.h>

#include "shim/app_lifecycle.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

int main(void) {
    AppLife l;

    appLifeInit(&l);
    CHECK(appLifeEvent(&l, APPLIFE_DID_FOREGROUND) == 0); /* the start-up foreground event is not a thaw */
    CHECK(appLifeEvent(&l, APPLIFE_LOW_MEMORY) == 0);
    CHECK(appLifeEvent(&l, APPLIFE_WILL_BACKGROUND) == (APPLIFE_ACT_FREEZE | APPLIFE_ACT_SAVE));
    CHECK(appLifeEvent(&l, APPLIFE_DID_BACKGROUND) == 0); /* once per trip */
    CHECK(appLifeEvent(&l, APPLIFE_WILL_FOREGROUND) == 0); /* the surface is not back yet */
    CHECK(l.frozen == 1);
    CHECK(appLifeEvent(&l, APPLIFE_DID_FOREGROUND) == APPLIFE_ACT_THAW);
    CHECK(appLifeEvent(&l, APPLIFE_DID_FOREGROUND) == 0);
    CHECK(l.frozen == 0);
    /* a second trip works the same; DID_BACKGROUND alone also freezes */
    CHECK(appLifeEvent(&l, APPLIFE_DID_BACKGROUND) == (APPLIFE_ACT_FREEZE | APPLIFE_ACT_SAVE));
    CHECK(appLifeEvent(&l, APPLIFE_DID_FOREGROUND) == APPLIFE_ACT_THAW);

    /* ten seconds in the background: the next deadline is one step after "now", not the stale one */
    CHECK(appLifeRebase(5.0, 15.0, 0.02) > 15.0 + 0.0199 && appLifeRebase(5.0, 15.0, 0.02) < 15.0 + 0.0201);

    if (failures == 0) {
        printf("applifetest: OK\n");
    }
    return failures != 0;
}
