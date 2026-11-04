/* Auto text, the pure part (feature controls-config, O6); see auto_text.h. */
#include "auto_text.h"

static int speed_;

int autoTextDelayFrames(int speed, int chars) {
    int n;

    if (speed <= AUTO_TEXT_OFF || speed >= AUTO_TEXT_SPEEDS) {
        return -1;
    }
    if (chars < 0) {
        chars = 0;
    } else if (chars > AUTO_TEXT_MAX_CHARS) {
        chars = AUTO_TEXT_MAX_CHARS;
    }
    n = AUTO_TEXT_BASE_FRAMES + AUTO_TEXT_PER_CHAR_HALVES * chars / 2;
    if (speed == AUTO_TEXT_SLOW) {
        n = n * 2;
    } else if (speed == AUTO_TEXT_FAST) {
        n = n / 2;
    }
    return n < AUTO_TEXT_MIN_FRAMES ? AUTO_TEXT_MIN_FRAMES : n;
}

int autoTextTick(AutoText *a, int speed, int waiting, int key, int chars) {
    int delay = autoTextDelayFrames(speed, chars);

    if (!waiting || delay < 0) {
        a->waited = 0;
        return 0;
    }
    if (key != a->key) {
        a->key = key;
        a->waited = 0;
    }
    if (++a->waited >= delay) {
        a->waited = 0;
        return 1;
    }
    return 0;
}

void autoTextSetSpeed(int speed) {
    speed_ = (speed > AUTO_TEXT_OFF && speed < AUTO_TEXT_SPEEDS) ? speed : AUTO_TEXT_OFF;
}

int autoTextSpeed(void) {
    return speed_;
}

static int textMode_;

int textSpeedDelay(int mode, int retailDelay) {
    return mode == TEXT_SPEED_ON ? 1 : retailDelay;
}

int textSpeedPageDelay(int mode, int retailDelay, int boosted) {
    return boosted ? 1 : textSpeedDelay(mode, retailDelay);
}

int textSpeedPageStart(int prevState, int state, int prevStart, int start, int fresh) {
    if (state != 1) {
        return 0;
    }
    return fresh || prevState == 2 || start != prevStart;
}

int textSpeedBoost(int prevState, int state, int pressed) {
    return pressed && prevState == 1 && state == 1;
}

void textSpeedSetMode(int mode) {
    textMode_ = (mode == TEXT_SPEED_ON) ? TEXT_SPEED_ON : TEXT_SPEED_OFF;
}

int textSpeedMode(void) {
    return textMode_;
}
