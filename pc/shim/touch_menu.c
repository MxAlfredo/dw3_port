/* Tapping the options menu: pure (see touch_menu.h). */
#include "touch_menu.h"

#include <string.h>

void tmnuInit(TmState *s, TmSink sink, void *sinkCtx, TmGuard guard, void *guardCtx) {
    memset(s, 0, sizeof(*s));
    s->sink = sink;
    s->sinkCtx = sinkCtx;
    s->guard = guard;
    s->guardCtx = guardCtx;
}

void tmnuSetLayout(TmState *s, const TmLayout *l) {
    if (l == NULL) {
        s->layout.valid = 0;
        s->used = 0; /* the menu left: a finger that was on it ends without a tap */
        return;
    }
    s->layout = *l;
    s->layout.valid = 1;
}

void tmnuSetView(TmState *s, int x, int y, int w, int h) {
    s->vx = x;
    s->vy = y;
    s->vw = w;
    s->vh = h;
}

static void emit(TmState *s, int kind, int a, int b) {
    if (s->sink != NULL) {
        s->sink(s->sinkCtx, kind, a, b);
    }
}

static int inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void resolveTap(TmState *s, float fxf, float fyf) {
    const TmLayout *l = &s->layout;
    int fx = (int)fxf, fy = (int)fyf, i;

    if (fxf < 0.0f || fyf < 0.0f || !inside(fx, fy, l->panelX, l->panelY, l->panelW, l->panelH)) {
        emit(s, TM_EV_CLOSE, 0, 0);
        return;
    }
    if (fy >= l->tabY && fy < l->tabY + l->tabH && l->tabCount > 0) {
        for (i = 0; i < l->tabCount; i++) {
            if (fx >= l->tabs[i].x && fx < l->tabs[i].x + l->tabs[i].w) {
                emit(s, TM_EV_TAB, l->tabs[i].tab, 0);
                return;
            }
        }
        if (fx < l->tabs[0].x) {
            emit(s, TM_EV_TAB_STEP, -1, 0);
        } else if (fx >= l->tabs[l->tabCount - 1].x + l->tabs[l->tabCount - 1].w) {
            emit(s, TM_EV_TAB_STEP, 1, 0);
        }
        return;
    }
    if (l->deviceOn && fy >= l->deviceY && fy < l->deviceY + l->deviceH) {
        emit(s, TM_EV_DEVICE, 0, 0);
        return;
    }
    for (i = 0; i < l->rowCount; i++) {
        const TmRow *r = &l->rows[i];

        if (!inside(fx, fy, l->rowX, r->y, l->rowW, r->h)) {
            continue;
        }
        if (fx < l->valueX) {
            emit(s, TM_EV_ROW, r->index, TM_ZONE_LABEL);
        } else if (r->slider && fx >= l->barX && fx < l->barX + l->barW) {
            emit(s, TM_EV_ROW, r->index, fx < l->barX + l->barW / 2 ? TM_ZONE_DEC : TM_ZONE_INC);
        } else {
            emit(s, TM_EV_ROW, r->index, TM_ZONE_VALUE);
        }
        return;
    }
}

static int toFrame(const TmState *s, const TrEvent *ev, float *fx, float *fy) {
    if (s->vw <= 0 || s->vh <= 0) {
        return 0;
    }
    *fx = (ev->x - (float)s->vx) * (float)s->layout.frameW / (float)s->vw;
    *fy = (ev->y - (float)s->vy) * (float)s->layout.frameH / (float)s->vh;
    return 1;
}

void tmnuSetCapture(TmState *s, int capturing) {
    s->capturing = capturing != 0;
}

int tmnuHandle(void *ctx, const TrEvent *ev) {
    TmState *s = (TmState *)ctx;
    float fx, fy;

    if (ev->kind == TR_DOWN) {
        if (s->capturing && s->layout.valid && !s->used) {
            emit(s, TM_EV_CAPTURE_CANCEL, 0, 0);
            return 1; /* claimed, not tracked: its move and up are swallowed below */
        }
        if (!s->layout.valid || s->used || !toFrame(s, ev, &fx, &fy)) {
            return 0;
        }
        if (s->guard != NULL && s->guard(s->guardCtx, ev->x, ev->y)) {
            return 0;
        }
        s->used = 1;
        s->finger = ev->finger;
        s->sx = fx;
        s->sy = s->lastY = fy;
        s->acc = 0.0f;
        s->moved = 0;
        return 1;
    }
    if (!s->used || s->finger != ev->finger) {
        return 1;
    }
    if (ev->kind == TR_CANCEL) {
        s->used = 0;
        return 1;
    }
    if (!s->layout.valid || !toFrame(s, ev, &fx, &fy)) {
        s->used = 0;
        return 1;
    }
    if (ev->kind == TR_MOVE) {
        if (!s->moved) {
            float dx = fx - s->sx, dy = fy - s->sy;

            if (dx > TM_SLOP || dx < -TM_SLOP || dy > TM_SLOP || dy < -TM_SLOP) {
                s->moved = 1;
                s->lastY = s->sy + (dy > 0 ? (float)TM_SLOP : (dy < 0 ? -(float)TM_SLOP : 0.0f));
            }
        }
        if (s->moved && s->layout.rowStep > 0 && s->sy >= (float)(s->layout.tabY + s->layout.tabH)) {
            float step = (float)s->layout.rowStep;

            s->acc += s->lastY - fy; /* a swipe up shows later rows: the cursor goes down */
            s->lastY = fy;
            while (s->acc >= step) {
                s->acc -= step;
                emit(s, TM_EV_SCROLL, 1, 0);
            }
            while (s->acc <= -step) {
                s->acc += step;
                emit(s, TM_EV_SCROLL, -1, 0);
            }
        }
        return 1;
    }
    /* TR_UP */
    s->used = 0;
    if (!s->moved) {
        resolveTap(s, s->sx, s->sy);
    }
    return 1;
}
