/* App lifecycle state machine (pure C; see app_lifecycle.h). */
#include "shim/app_lifecycle.h"

void appLifeInit(AppLife *life) {
    life->frozen = 0;
}

int appLifeEvent(AppLife *life, AppLifeEvent event) {
    switch (event) {
    case APPLIFE_WILL_BACKGROUND:
    case APPLIFE_DID_BACKGROUND:
        if (!life->frozen) {
            life->frozen = 1;
            return APPLIFE_ACT_FREEZE | APPLIFE_ACT_SAVE;
        }
        return 0;
    case APPLIFE_DID_FOREGROUND:
        if (life->frozen) {
            life->frozen = 0;
            return APPLIFE_ACT_THAW;
        }
        return 0;
    default:
        return 0;
    }
}

double appLifeRebase(double oldNext, double now, double step) {
    (void)oldNext;
    return now + step;
}
