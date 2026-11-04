/* Tap the picture to turn a page: pure (see touch_tap.h). */
#include "touch_tap.h"

#include <string.h>

void ttInit(TtState *s) {
    memset(s, 0, sizeof(*s));
}

void ttSetView(TtState *s, int x, int y, int w, int h) {
    s->vx = x;
    s->vy = y;
    s->vw = w;
    s->vh = h;
}

void ttArm(TtState *s, int kind, int key) {
    /* a finished page (waiting) is a new start for the window's next page: its typing answer is forgotten */
    if (kind == TT_WAITING && s->spent && (s->spentKey & 3) == TT_TYPING) {
        s->spent = 0;
    }
    /* typing and waiting are two answers of the same page */
    s->key = key * 4 + (kind & 3);
    s->armed = kind != TT_NONE && !(s->spent && s->spentKey == s->key);
}

int ttHandle(void *ctx, const TrEvent *ev) {
    TtState *s = (TtState *)ctx;

    if (ev->kind != TR_DOWN) {
        return 1; /* the rest of a finger this handler owns */
    }
    if (!s->armed || s->vw <= 0 || s->vh <= 0 || ev->x < (float)s->vx || ev->x >= (float)(s->vx + s->vw) ||
        ev->y < (float)s->vy || ev->y >= (float)(s->vy + s->vh)) {
        return 0;
    }
    s->armed = 0;
    s->spent = 1;
    s->spentKey = s->key;
    s->pending = 1;
    return 1;
}

int ttTake(TtState *s) {
    int p = s->pending;

    s->pending = 0;
    return p;
}
