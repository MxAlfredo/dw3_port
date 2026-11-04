#include "stage_banner.h"

static int bannerMode = STAGE_BANNER_FAST;
static int stageShown;

void stageBannerSetMode(int mode) {
    bannerMode = (mode >= 0 && mode < STAGE_BANNER_MODES) ? mode : STAGE_BANNER_FAST;
}

int stageBannerMode(void) {
    return bannerMode;
}

void stageBannerSetShown(int shown) {
    stageShown = shown;
}

int stageBannerHidesMap(void) {
    return bannerMode == STAGE_BANNER_CLASSIC || !stageShown;
}

int stageBannerMayShow(int pendingTiles, int waitedFrames) {
    return pendingTiles <= 0 || waitedFrames >= STAGE_BANNER_SHOW_WAIT_MAX;
}

int stageExitCoverHidden(int fieldClosing, int battleStarting, int fastDoor) {
    return fastDoor != 0 && fieldClosing != 0 && battleStarting == 0;
}
