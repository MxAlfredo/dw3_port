/*
 * The on-screen virtual controller, pure logic (see touch_pad.h).
 *
 * The PS1 game is digital: the pad's directions are four bits. A real pad's analog stick already reaches the game
 * the same way (src/engine/pad/port.c turns an axis past STICK_LOW / STICK_HIGH into those bits); the touch stick
 * produces the bits directly, with its own deadzone and eight 45-degree sectors, because a thumb on glass has no
 * mechanical centre.
 */
#include "touch_pad.h"

#include <math.h>
#include <string.h>

/* tan(22.5 degrees): the edge between a straight direction and its diagonal */
#define SECTOR_TAN 0.41421356f

unsigned tpStickDirs(float dx, float dy, float radius, float deadzone) {
    float ax = fabsf(dx), ay = fabsf(dy), dead = radius * deadzone;
    unsigned dirs = 0;

    if (radius <= 0.0f || ax * ax + ay * ay < dead * dead || (ax == 0.0f && ay == 0.0f)) {
        return 0;
    }
    if (ax > ay * SECTOR_TAN) {
        dirs |= dx < 0.0f ? TP_LEFT : TP_RIGHT;
    }
    if (ay > ax * SECTOR_TAN) {
        dirs |= dy < 0.0f ? TP_UP : TP_DOWN;
    }
    return dirs;
}

/* ---- layout ---- */

static void put(TpLayout *l, unsigned bit, int shape, float x, float y, float w, float h) {
    TpButton *b = &l->buttons[l->count++];

    b->bit = bit;
    b->shape = shape;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
}

static float clampf(float v, float lo, float hi) {
    return hi < lo ? lo : (v < lo ? lo : (v > hi ? hi : v));
}

void tpLayoutBuild(TpLayout *l, int winW, int winH, int gx, int gy, int gw, int gh, int sizePct) {
    float u, m, leftBar, rightBar, fx, fy, shoulderL, shoulderR, w, h;

    (void)gy;
    (void)gh;
    memset(l, 0, sizeof(*l));
    w = winW > 0 ? (float)winW : 1.0f;
    h = winH > 0 ? (float)winH : 1.0f;
    sizePct = sizePct < TP_SIZE_MIN ? TP_SIZE_MIN : (sizePct > TP_SIZE_MAX ? TP_SIZE_MAX : sizePct);
    u = (w < h ? w : h) * 0.12f * (float)sizePct / 100.0f;
    m = 0.5f * u;
    l->winW = winW;
    l->winH = winH;
    l->unit = u;
    l->stickR = 1.6f * u;
    leftBar = (float)gx;
    rightBar = w - (float)(gx + gw);
    if (gw <= 0 || leftBar < 0.0f) {
        leftBar = 0.0f;
    }
    if (gw <= 0 || rightBar < 0.0f) {
        rightBar = 0.0f;
    }

    /* the stick rests in the middle of the left bar if it fits, else in the bottom left corner */
    l->stickX = clampf(leftBar >= 3.4f * u ? leftBar * 0.5f : m + 1.7f * u, 1.7f * u, w - 1.7f * u);
    l->stickY = clampf(h - m - 1.8f * u, 1.7f * u, h);
    l->zoneX = w * 0.45f;

    /* the face buttons: a diamond in the right bar, or in the bottom right corner */
    fx = clampf(rightBar >= 3.4f * u ? (float)(gx + gw) + rightBar * 0.5f : w - m - 1.9f * u, 1.9f * u, w - 1.9f * u);
    fy = clampf(h - m - 1.9f * u, 1.9f * u, h - 1.9f * u);

    shoulderL = clampf(leftBar >= 2.4f * u ? leftBar * 0.5f : m + 0.9f * u, 0.9f * u, w - 0.9f * u);
    shoulderR = clampf(rightBar >= 2.4f * u ? (float)(gx + gw) + rightBar * 0.5f : w - m - 0.9f * u, 0.9f * u, w - 0.9f * u);

    put(l, TP_L2, TP_SHAPE_PILL, shoulderL, m + 0.4f * u, 1.8f * u, 0.8f * u);
    put(l, TP_L1, TP_SHAPE_PILL, shoulderL, m + 1.35f * u, 1.8f * u, 0.8f * u);
    put(l, TP_R2, TP_SHAPE_PILL, shoulderR, m + 0.4f * u, 1.8f * u, 0.8f * u);
    put(l, TP_R1, TP_SHAPE_PILL, shoulderR, m + 1.35f * u, 1.8f * u, 0.8f * u);
    put(l, TP_SELECT, TP_SHAPE_PILL, w * 0.5f - 1.0f * u, h - m - 0.32f * u, 1.6f * u, 0.64f * u);
    put(l, TP_START, TP_SHAPE_PILL, w * 0.5f + 1.0f * u, h - m - 0.32f * u, 1.6f * u, 0.64f * u);
    put(l, TP_TRIANGLE, TP_SHAPE_DISC, fx, fy - 1.25f * u, 1.24f * u, 1.24f * u);
    put(l, TP_CROSS, TP_SHAPE_DISC, fx, fy + 1.25f * u, 1.24f * u, 1.24f * u);
    put(l, TP_SQUARE, TP_SHAPE_DISC, fx - 1.25f * u, fy, 1.24f * u, 1.24f * u);
    put(l, TP_CIRCLE, TP_SHAPE_DISC, fx + 1.25f * u, fy, 1.24f * u, 1.24f * u);
}

/* How far into a target a point is, as a squared fraction of its (forgiving) hit area: <= 1 hits */
static float hitScore(const TpLayout *l, const TpButton *b, float x, float y) {
    float dx = x - b->x, dy = y - b->y;

    if (b->shape == TP_SHAPE_DISC) {
        float r = b->w * 0.5f * 1.35f;

        return (dx * dx + dy * dy) / (r * r);
    } else {
        float hw = b->w * 0.5f + 0.3f * l->unit, hh = b->h * 0.5f + 0.3f * l->unit;

        return (dx / hw) * (dx / hw) + (dy / hh) * (dy / hh) > 1.0f
                   ? 2.0f
                   : (fabsf(dx) / hw > fabsf(dy) / hh ? fabsf(dx) / hw : fabsf(dy) / hh);
    }
}

int tpHitButton(const TpLayout *l, float x, float y) {
    int i, best = -1;
    float bestScore = 1.0f;

    for (i = 0; i < l->count; i++) {
        float s = hitScore(l, &l->buttons[i], x, y);

        if (s <= bestScore) {
            bestScore = s;
            best = i;
        }
    }
    return best;
}

int tpInStickZone(const TpLayout *l, float x, float y) {
    return x < l->zoneX && tpHitButton(l, x, y) < 0;
}

/* ---- fingers ---- */

void tpInit(TpState *s) {
    memset(s, 0, sizeof(*s));
}

void tpSetLayout(TpState *s, const TpLayout *l) {
    s->layout = *l;
}

void tpReleaseAll(TpState *s) {
    memset(s->fingers, 0, sizeof(s->fingers));
}

static TpFinger *find(TpState *s, long long id) {
    int i;

    for (i = 0; i < TP_MAX_FINGERS; i++) {
        if (s->fingers[i].used && s->fingers[i].id == id) {
            return &s->fingers[i];
        }
    }
    return NULL;
}

static int stickHeld(const TpState *s) {
    int i;

    for (i = 0; i < TP_MAX_FINGERS; i++) {
        if (s->fingers[i].used && s->fingers[i].stick) {
            return 1;
        }
    }
    return 0;
}

static TpFinger *alloc(TpState *s) {
    int i;

    for (i = 0; i < TP_MAX_FINGERS; i++) {
        if (!s->fingers[i].used) {
            memset(&s->fingers[i], 0, sizeof(s->fingers[i]));
            s->fingers[i].used = 1;
            return &s->fingers[i];
        }
    }
    return NULL;
}

/* The origin floats: past full travel it is dragged along, so the thumb is never further than `radius` from it */
static void follow(TpFinger *f, float radius) {
    float dx = f->cx - f->ox, dy = f->cy - f->oy, len = sqrtf(dx * dx + dy * dy);

    if (len > radius && len > 0.0f) {
        f->ox += dx / len * (len - radius);
        f->oy += dy / len * (len - radius);
    }
}

int tpHandle(void *ctx, const TrEvent *ev) {
    TpState *s = (TpState *)ctx;
    TpFinger *f;

    switch (ev->kind) {
    case TR_DOWN:
        if (tpHitButton(&s->layout, ev->x, ev->y) >= 0) {
            f = alloc(s);
            if (f == NULL) {
                return 0;
            }
            f->id = ev->finger;
            f->cx = ev->x;
            f->cy = ev->y;
            return 1;
        }
        if (tpInStickZone(&s->layout, ev->x, ev->y) && !stickHeld(s)) {
            f = alloc(s);
            if (f == NULL) {
                return 0;
            }
            f->id = ev->finger;
            f->stick = 1;
            f->ox = f->cx = ev->x;
            f->oy = f->cy = ev->y;
            return 1;
        }
        return 0;
    case TR_MOVE:
        f = find(s, ev->finger);
        if (f != NULL) {
            f->cx = ev->x;
            f->cy = ev->y;
            if (f->stick) {
                follow(f, s->layout.stickR);
            }
        }
        return 1;
    default: /* TR_UP, TR_CANCEL */
        f = find(s, ev->finger);
        if (f != NULL) {
            f->used = 0;
        }
        return 1;
    }
}

unsigned tpMask(const TpState *s) {
    unsigned mask = 0;
    int i;

    for (i = 0; i < TP_MAX_FINGERS; i++) {
        const TpFinger *f = &s->fingers[i];

        if (!f->used) {
            continue;
        }
        if (f->stick) {
            mask |= tpStickDirs(f->cx - f->ox, f->cy - f->oy, s->layout.stickR, TP_DEADZONE);
        } else {
            int b = tpHitButton(&s->layout, f->cx, f->cy);

            if (b >= 0) {
                mask |= s->layout.buttons[b].bit;
            }
        }
    }
    return mask;
}

int tpStickPose(const TpState *s, float *ox, float *oy, float *tx, float *ty) {
    int i;

    for (i = 0; i < TP_MAX_FINGERS; i++) {
        const TpFinger *f = &s->fingers[i];

        if (f->used && f->stick) {
            float dx = f->cx - f->ox, dy = f->cy - f->oy, len = sqrtf(dx * dx + dy * dy), r = s->layout.stickR;

            if (len > r && len > 0.0f) {
                dx *= r / len;
                dy *= r / len;
            }
            *ox = f->ox;
            *oy = f->oy;
            *tx = f->ox + dx;
            *ty = f->oy + dy;
            return 1;
        }
    }
    return 0;
}

/* ---- visibility ---- */

void tpVisSetMode(TpVis *v, int mode) {
    v->mode = mode == TP_MODE_ON || mode == TP_MODE_AUTO ? mode : TP_MODE_OFF;
}

void tpVisInit(TpVis *v, int mode, int padsConnected) {
    v->pads = padsConnected > 0 ? padsConnected : 0;
    v->shown = v->pads == 0;
    tpVisSetMode(v, mode);
}

void tpVisEvent(TpVis *v, int event) {
    switch (event) {
    case TP_EV_TOUCH:
        v->shown = 1;
        break;
    case TP_EV_PAD_USED:
        v->shown = 0;
        break;
    default:
        break;
    }
}

void tpVisPads(TpVis *v, int pads) {
    pads = pads > 0 ? pads : 0;
    if (pads > v->pads) {
        v->shown = 0;
    }
    if (pads == 0) {
        v->shown = 1;
    }
    v->pads = pads;
}

int tpVisShown(const TpVis *v) {
    return v->mode == TP_MODE_ON || (v->mode == TP_MODE_AUTO && v->shown);
}

int tpVisTakesTouch(const TpVis *v) {
    return v->mode != TP_MODE_OFF;
}
