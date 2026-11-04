#include "battle_speed.h"

static int speedMode = BATTLE_SPEED_NORMAL;

int battleSpeedStep(int mode, int gate, int baseStep) {
    if (!gate) {
        return baseStep;
    }
    switch (mode) {
    case BATTLE_SPEED_X15:
        return baseStep + baseStep / 2;
    case BATTLE_SPEED_X2:
        return baseStep * 2;
    default:
        return baseStep;
    }
}

void battleSpeedSetMode(int mode) {
    speedMode = (mode >= 0 && mode < BATTLE_SPEED_MODES) ? mode : BATTLE_SPEED_NORMAL;
}

int battleSpeedMode(void) {
    return speedMode;
}
