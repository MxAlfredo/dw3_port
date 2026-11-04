/*
 * Host test of the touch layer's pure logic (shim/touch_router.c, shim/touch_pad.c): the router hands each finger
 * to one handler, the stick becomes eight directions with a deadzone, buttons are hit from the layout, fingers
 * slide between buttons, and the Auto visibility state machine.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <stdio.h>
#include <string.h>

#include "shim/touch_menu.h"
#include "shim/touch_pad.h"
#include "shim/touch_tap.h"
#include "shim/touch_router.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* ---- router ---- */

typedef struct Probe {
    int claimAll;
    int downs, moves, ups, cancels;
    long long lastFinger;
} Probe;

static int probeFn(void *ctx, const TrEvent *ev) {
    Probe *p = (Probe *)ctx;

    p->lastFinger = ev->finger;
    switch (ev->kind) {
    case TR_DOWN: p->downs++; return p->claimAll;
    case TR_MOVE: p->moves++; return 0;
    case TR_UP: p->ups++; return 0;
    default: p->cancels++; return 0;
    }
}

static TrEvent ev(int kind, long long finger, float x, float y) {
    TrEvent e;

    e.kind = kind;
    e.finger = finger;
    e.x = x;
    e.y = y;
    e.winW = 2400;
    e.winH = 1080;
    return e;
}

static void testRouter(void) {
    TrRouter r;
    Probe first, second;
    TrEvent e;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    second.claimAll = 1;
    trInit(&r);
    CHECK(trAdd(&r, probeFn, &first) == 0);
    CHECK(trAdd(&r, probeFn, &second) == 1);

    /* the first handler declines, the second claims: both saw the DOWN, only the claimer gets the rest */
    e = ev(TR_DOWN, 7, 10, 10);
    CHECK(trFeed(&r, &e) == 1);
    CHECK(first.downs == 1 && second.downs == 1 && trOwned(&r) == 1);
    e = ev(TR_MOVE, 7, 20, 20);
    CHECK(trFeed(&r, &e) == 1);
    CHECK(first.moves == 0 && second.moves == 1);
    e = ev(TR_UP, 7, 20, 20);
    CHECK(trFeed(&r, &e) == 1);
    CHECK(second.ups == 1 && trOwned(&r) == 0);
    /* after the UP the finger is nobody's */
    e = ev(TR_MOVE, 7, 20, 20);
    CHECK(trFeed(&r, &e) == 0 && second.moves == 1);

    /* priority: an earlier handler that claims keeps the finger from the later one */
    first.claimAll = 1;
    memset(&second, 0, sizeof(second));
    second.claimAll = 1;
    e = ev(TR_DOWN, 8, 1, 1);
    CHECK(trFeed(&r, &e) == 1);
    CHECK(second.downs == 0);
    e = ev(TR_UP, 8, 1, 1);
    trFeed(&r, &e);

    /* a finger nobody claims is dropped and costs no table slot */
    first.claimAll = 0;
    second.claimAll = 0;
    e = ev(TR_DOWN, 9, 1, 1);
    CHECK(trFeed(&r, &e) == 0 && trOwned(&r) == 0);

    /* a missed UP: a second DOWN of the same finger cancels the old ownership first */
    second.claimAll = 1;
    memset(&first, 0, sizeof(first));
    e = ev(TR_DOWN, 3, 5, 5);
    trFeed(&r, &e);
    e = ev(TR_DOWN, 3, 6, 6);
    trFeed(&r, &e);
    CHECK(second.cancels == 1 && trOwned(&r) == 1);

    /* releaseAll cancels every owner */
    e = ev(TR_DOWN, 4, 5, 5);
    trFeed(&r, &e);
    CHECK(trOwned(&r) == 2);
    trReleaseAll(&r);
    CHECK(trOwned(&r) == 0 && second.cancels == 3);

    /* the table fills up: further fingers are not claimed */
    {
        int i, claimed = 0;

        for (i = 0; i < TR_MAX_FINGERS + 4; i++) {
            e = ev(TR_DOWN, 100 + i, 1, 1);
            claimed += trFeed(&r, &e);
        }
        CHECK(claimed == TR_MAX_FINGERS);
        trReleaseAll(&r);
    }
}

/* ---- stick ---- */

static void testStick(void) {
    const float R = 100.0f, D = 0.30f;

    CHECK(tpStickDirs(0, 0, R, D) == 0);
    CHECK(tpStickDirs(29, 0, R, D) == 0);   /* inside the deadzone */
    CHECK(tpStickDirs(0, -29, R, D) == 0);
    CHECK(tpStickDirs(21, 21, R, D) == 0);  /* 29.7 from the origin */
    CHECK(tpStickDirs(31, 0, R, D) == TP_RIGHT);
    CHECK(tpStickDirs(-31, 0, R, D) == TP_LEFT);
    CHECK(tpStickDirs(0, -31, R, D) == TP_UP);   /* y grows downwards */
    CHECK(tpStickDirs(0, 31, R, D) == TP_DOWN);
    CHECK(tpStickDirs(50, -50, R, D) == (TP_UP | TP_RIGHT));
    CHECK(tpStickDirs(-50, -50, R, D) == (TP_UP | TP_LEFT));
    CHECK(tpStickDirs(50, 50, R, D) == (TP_DOWN | TP_RIGHT));
    CHECK(tpStickDirs(-50, 50, R, D) == (TP_DOWN | TP_LEFT));
    /* sector edges at 22.5 degrees: tan = 0.4142 */
    CHECK(tpStickDirs(100, -40, R, D) == (TP_RIGHT));       /* 21.8 degrees: still pure right */
    CHECK(tpStickDirs(100, -43, R, D) == (TP_RIGHT | TP_UP)); /* 23.3 degrees: the diagonal */
    CHECK(tpStickDirs(43, -100, R, D) == (TP_RIGHT | TP_UP));
    CHECK(tpStickDirs(40, -100, R, D) == TP_UP);
    /* far past full travel behaves the same */
    CHECK(tpStickDirs(900, 0, R, D) == TP_RIGHT);
    /* a bad radius presses nothing */
    CHECK(tpStickDirs(50, 0, 0.0f, D) == 0);
}

/* ---- layout and hit-testing ---- */

static void testLayout(void) {
    TpLayout l, big;
    int i, faces = 0, found[16];
    unsigned seen = 0;

    /* a 20:9 phone in Aspect mode: the 4:3 picture is centred, side bars of 480 px */
    tpLayoutBuild(&l, 2400, 1080, 480, 0, 1440, 1080, 100);
    CHECK(l.count == 10);
    for (i = 0; i < l.count; i++) {
        seen |= l.buttons[i].bit;
        CHECK(l.buttons[i].x >= 0 && l.buttons[i].x <= 2400 && l.buttons[i].y >= 0 && l.buttons[i].y <= 1080);
        if (l.buttons[i].bit == TP_CROSS || l.buttons[i].bit == TP_CIRCLE || l.buttons[i].bit == TP_SQUARE ||
            l.buttons[i].bit == TP_TRIANGLE) {
            faces++;
        }
    }
    CHECK(faces == 4);
    CHECK(seen == (TP_SELECT | TP_START | TP_L1 | TP_L2 | TP_R1 | TP_R2 | TP_TRIANGLE | TP_CIRCLE | TP_CROSS | TP_SQUARE |
                   TP_UP * 0)); /* no d-pad: directions come from the stick only */
    CHECK((seen & TP_DIRS) == 0);
    /* the stick rests in the left bar and the face buttons in the right bar */
    CHECK(l.stickX > 0 && l.stickX < 480);
    for (i = 0; i < l.count; i++) {
        if (l.buttons[i].bit == TP_CROSS) {
            CHECK(l.buttons[i].x > 1920);
        }
    }
    /* face layout: triangle above cross, square left of circle */
    {
        const TpButton *t = NULL, *c = NULL, *s = NULL, *o = NULL;

        for (i = 0; i < l.count; i++) {
            t = l.buttons[i].bit == TP_TRIANGLE ? &l.buttons[i] : t;
            c = l.buttons[i].bit == TP_CROSS ? &l.buttons[i] : c;
            s = l.buttons[i].bit == TP_SQUARE ? &l.buttons[i] : s;
            o = l.buttons[i].bit == TP_CIRCLE ? &l.buttons[i] : o;
        }
        CHECK(t && c && s && o);
        CHECK(t->y < c->y && s->x < o->x && t->x == c->x);
    }
    /* each button's centre hits itself */
    memset(found, 0, sizeof(found));
    for (i = 0; i < l.count; i++) {
        CHECK(tpHitButton(&l, l.buttons[i].x, l.buttons[i].y) == i);
    }
    CHECK(tpHitButton(&l, 1200, 540) == -1); /* the middle of the picture */
    /* a bigger size makes bigger buttons */
    tpLayoutBuild(&big, 2400, 1080, 480, 0, 1440, 1080, 150);
    CHECK(big.unit > l.unit * 1.4f && big.unit < l.unit * 1.6f);
    /* the stick zone is the left side; a button inside it wins over the stick */
    CHECK(tpInStickZone(&l, 200, 600));
    CHECK(!tpInStickZone(&l, 2000, 600));
    for (i = 0; i < l.count; i++) {
        if (l.buttons[i].bit == TP_L1) {
            CHECK(!tpInStickZone(&l, l.buttons[i].x, l.buttons[i].y));
        }
    }
    /* Fit mode on a 4:3 window: no bars, the controls go to the corners and stay inside */
    tpLayoutBuild(&l, 960, 720, 0, 0, 960, 720, 100);
    CHECK(l.stickX > 0 && l.stickX < 960 * 0.5f);
    for (i = 0; i < l.count; i++) {
        CHECK(l.buttons[i].x - l.buttons[i].w / 2 >= 0 && l.buttons[i].x + l.buttons[i].w / 2 <= 960);
        CHECK(l.buttons[i].y - l.buttons[i].h / 2 >= 0 && l.buttons[i].y + l.buttons[i].h / 2 <= 720);
    }
    /* size is clamped, and a degenerate window does not crash */
    tpLayoutBuild(&l, 2400, 1080, 480, 0, 1440, 1080, 5);
    CHECK(l.unit > 0);
    tpLayoutBuild(&l, 0, 0, 0, 0, 0, 0, 100);
    CHECK(l.count == 10);
}

/* ---- fingers ---- */

static float cxOf(const TpState *s, unsigned bit) {
    int i;

    for (i = 0; i < s->layout.count; i++) {
        if (s->layout.buttons[i].bit == bit) {
            return s->layout.buttons[i].x;
        }
    }
    return -1;
}

static float cyOf(const TpState *s, unsigned bit) {
    int i;

    for (i = 0; i < s->layout.count; i++) {
        if (s->layout.buttons[i].bit == bit) {
            return s->layout.buttons[i].y;
        }
    }
    return -1;
}

static int feed(TrRouter *r, int kind, long long id, float x, float y) {
    TrEvent e = ev(kind, id, x, y);

    return trFeed(r, &e);
}

static void testFingers(void) {
    static TpState s;
    TrRouter r;
    TpLayout l;
    float ox, oy, tx, ty;
    unsigned cross, circle;

    tpInit(&s);
    tpLayoutBuild(&l, 2400, 1080, 480, 0, 1440, 1080, 100);
    tpSetLayout(&s, &l);
    trInit(&r);
    trAdd(&r, tpHandle, &s);
    cross = TP_CROSS;
    circle = TP_CIRCLE;

    /* a button press and release */
    CHECK(feed(&r, TR_DOWN, 1, cxOf(&s, TP_CROSS), cyOf(&s, TP_CROSS)) == 1);
    CHECK(tpMask(&s) == cross);
    feed(&r, TR_UP, 1, 0, 0);
    CHECK(tpMask(&s) == 0);

    /* a tap on the picture is nobody's */
    CHECK(feed(&r, TR_DOWN, 2, 1200, 540) == 0 && tpMask(&s) == 0);

    /* the stick floats where the thumb lands in the left zone and a push right becomes Right */
    CHECK(feed(&r, TR_DOWN, 3, 300, 700) == 1);
    CHECK(tpMask(&s) == 0); /* no push yet */
    CHECK(tpStickPose(&s, &ox, &oy, &tx, &ty) == 1 && ox == 300 && oy == 700);
    feed(&r, TR_MOVE, 3, 300 + s.layout.stickR * 0.6f, 700);
    CHECK(tpMask(&s) == TP_RIGHT);
    feed(&r, TR_MOVE, 3, 300 + s.layout.stickR * 0.5f, 700 - s.layout.stickR * 0.5f);
    CHECK(tpMask(&s) == (TP_RIGHT | TP_UP));
    /* past full travel the origin follows the thumb, so turning back is immediate */
    feed(&r, TR_MOVE, 3, 300 + s.layout.stickR * 3.0f, 700);
    tpStickPose(&s, &ox, &oy, &tx, &ty);
    CHECK(ox > 300 + s.layout.stickR * 1.9f && ox < 300 + s.layout.stickR * 2.1f);
    feed(&r, TR_MOVE, 3, 300 + s.layout.stickR * 1.0f, 700);
    CHECK(tpMask(&s) == TP_LEFT);

    /* multi-touch: a face button while the stick is held; the stick is unaffected */
    CHECK(feed(&r, TR_DOWN, 4, cxOf(&s, TP_CIRCLE), cyOf(&s, TP_CIRCLE)) == 1);
    CHECK(tpMask(&s) == (TP_LEFT | circle));
    /* a second finger in the stick zone while the stick is held is not claimed */
    CHECK(feed(&r, TR_DOWN, 5, 100, 400) == 0);
    /* sliding the button finger onto the neighbour moves the press with it */
    feed(&r, TR_MOVE, 4, cxOf(&s, TP_CROSS), cyOf(&s, TP_CROSS));
    CHECK(tpMask(&s) == (TP_LEFT | cross));
    /* sliding off every button releases it */
    feed(&r, TR_MOVE, 4, 1200, 540);
    CHECK(tpMask(&s) == TP_LEFT);
    /* and back on again presses it (the finger is still ours) */
    feed(&r, TR_MOVE, 4, cxOf(&s, TP_SQUARE), cyOf(&s, TP_SQUARE));
    CHECK(tpMask(&s) == (TP_LEFT | TP_SQUARE));
    feed(&r, TR_UP, 4, 0, 0);
    feed(&r, TR_UP, 3, 0, 0);
    CHECK(tpMask(&s) == 0 && tpStickPose(&s, &ox, &oy, &tx, &ty) == 0);

    /* two buttons with two fingers (shoulder + face) */
    feed(&r, TR_DOWN, 6, cxOf(&s, TP_R1), cyOf(&s, TP_R1));
    feed(&r, TR_DOWN, 7, cxOf(&s, TP_TRIANGLE), cyOf(&s, TP_TRIANGLE));
    CHECK(tpMask(&s) == (TP_R1 | TP_TRIANGLE));
    /* the layout changes under a held finger (rotation, scaling mode): the finger stays, the mask follows the new layout */
    tpLayoutBuild(&l, 2400, 1080, 0, 0, 2400, 1080, 100);
    tpSetLayout(&s, &l);
    feed(&r, TR_MOVE, 6, cxOf(&s, TP_R1), cyOf(&s, TP_R1));
    CHECK((tpMask(&s) & TP_R1) != 0);
    trReleaseAll(&r);
    CHECK(tpMask(&s) == 0);

    /* start and select are plain buttons */
    feed(&r, TR_DOWN, 8, cxOf(&s, TP_START), cyOf(&s, TP_START));
    CHECK(tpMask(&s) == TP_START);
    feed(&r, TR_UP, 8, 0, 0);
    feed(&r, TR_DOWN, 9, cxOf(&s, TP_SELECT), cyOf(&s, TP_SELECT));
    CHECK(tpMask(&s) == TP_SELECT);
    feed(&r, TR_UP, 9, 0, 0);
    CHECK(tpMask(&s) == 0);
}

/* ---- visibility ---- */

static void testVisibility(void) {
    TpVis v;

    tpVisInit(&v, TP_MODE_OFF, 0);
    CHECK(!tpVisShown(&v) && !tpVisTakesTouch(&v));
    tpVisEvent(&v, TP_EV_TOUCH);
    CHECK(!tpVisShown(&v));
    tpVisInit(&v, TP_MODE_ON, 1);
    CHECK(tpVisShown(&v) && tpVisTakesTouch(&v));
    tpVisEvent(&v, TP_EV_PAD_USED);
    CHECK(tpVisShown(&v));

    /* Auto: shown at the start when no pad is connected */
    tpVisInit(&v, TP_MODE_AUTO, 0);
    CHECK(tpVisShown(&v) && tpVisTakesTouch(&v));
    /* a real pad is used: hidden; a touch brings the controls back */
    tpVisEvent(&v, TP_EV_PAD_USED);
    CHECK(!tpVisShown(&v) && tpVisTakesTouch(&v));
    tpVisEvent(&v, TP_EV_TOUCH);
    CHECK(tpVisShown(&v));
    /* connecting a pad hides; unplugging the last one shows again; a repeated count changes nothing */
    tpVisPads(&v, 1);
    CHECK(!tpVisShown(&v) && v.pads == 1);
    tpVisEvent(&v, TP_EV_TOUCH);
    tpVisPads(&v, 1);
    CHECK(tpVisShown(&v)); /* the same count again: the touch that showed the controls stays */
    tpVisPads(&v, 2);
    CHECK(!tpVisShown(&v) && v.pads == 2);
    tpVisPads(&v, 1);
    CHECK(!tpVisShown(&v) && v.pads == 1);
    tpVisPads(&v, 0);
    CHECK(tpVisShown(&v) && v.pads == 0);
    tpVisPads(&v, -3); /* never negative */
    CHECK(v.pads == 0);
    /* a pad already connected at boot starts hidden */
    tpVisInit(&v, TP_MODE_AUTO, 1);
    CHECK(!tpVisShown(&v));
    tpVisEvent(&v, TP_EV_TOUCH);
    CHECK(tpVisShown(&v));
    /* switching the mode keeps pad count and shows per the new rule */
    tpVisSetMode(&v, TP_MODE_OFF);
    CHECK(!tpVisShown(&v));
    tpVisSetMode(&v, TP_MODE_AUTO);
    CHECK(v.pads == 1 && tpVisShown(&v)); /* the Auto state (a touch showed it) survives a mode round trip */
    tpVisSetMode(&v, 99); /* unknown: Off */
    CHECK(v.mode == TP_MODE_OFF);
}


/* ---- the options menu (phase 1) ---- */

typedef struct Rec {
    int n, kind[16], a[16], b[16];
    int guardAll;
} Rec;

static void recSink(void *ctx, int kind, int a, int b) {
    Rec *r = (Rec *)ctx;

    if (r->n < 16) {
        r->kind[r->n] = kind;
        r->a[r->n] = a;
        r->b[r->n] = b;
        r->n++;
    }
}

static int recGuard(void *ctx, float x, float y) {
    (void)y;
    return ((Rec *)ctx)->guardAll || x < 100.0f;
}

/* a 4:3 picture of 960x720 at (720,180) in a 2400x1080 window: 3 window pixels per frame pixel */
static void menuLayout(TmLayout *l) {
    int i;

    memset(l, 0, sizeof(*l));
    l->valid = 1;
    l->frameW = 320;
    l->frameH = 240;
    l->panelX = 16; l->panelY = 2; l->panelW = 288; l->panelH = 236;
    l->tabY = 3; l->tabH = 17;
    l->tabCount = 2;
    l->tabs[0].tab = 1; l->tabs[0].x = 30; l->tabs[0].w = 60;
    l->tabs[1].tab = 2; l->tabs[1].x = 100; l->tabs[1].w = 60;
    l->rowX = 26; l->rowW = 268;
    l->valueX = 156; l->barX = 156; l->barW = 96;
    l->rowStep = 12;
    l->rowCount = 4;
    for (i = 0; i < 4; i++) {
        l->rows[i].index = 5 + i;
        l->rows[i].y = 28 + i * 12;
        l->rows[i].h = 12;
        l->rows[i].slider = i == 3;
    }
}

static void tapAt(TmState *s, float fx, float fy) {
    TrEvent e = ev(TR_DOWN, 1, 720.0f + fx * 3.0f, 180.0f + fy * 3.0f);

    CHECK(tmnuHandle(s, &e) == 1);
    e.kind = TR_UP;
    tmnuHandle(s, &e);
}

/* a touch-only player cannot press the key a "Press a key" capture waits for: a tap anywhere cancels it */
static void testCaptureTap(void) {
    TmState s;
    TmLayout l;
    Rec r;
    TrEvent e;

    memset(&r, 0, sizeof(r));
    tmnuInit(&s, recSink, &r, recGuard, &r);
    tmnuSetView(&s, 720, 180, 960, 720);
    menuLayout(&l);
    tmnuSetLayout(&s, &l);

    /* a capture waits: a tap on a row, outside the panel, and where the guard owns the point all cancel it and do nothing else */
    tmnuSetCapture(&s, 1);
    tapAt(&s, 60, 28 + 12 + 5);
    CHECK(r.n == 1 && r.kind[0] == TM_EV_CAPTURE_CANCEL);
    tapAt(&s, 8, 100);
    CHECK(r.n == 2 && r.kind[1] == TM_EV_CAPTURE_CANCEL);
    e = ev(TR_DOWN, 2, 50, 500); /* the guard owns x < 100 */
    CHECK(tmnuHandle(&s, &e) == 1 && r.n == 3 && r.kind[2] == TM_EV_CAPTURE_CANCEL);
    e = ev(TR_UP, 2, 50, 500);
    CHECK(tmnuHandle(&s, &e) == 1 && r.n == 3); /* its up is swallowed, no second event */
    e = ev(TR_MOVE, 2, 60, 500);
    CHECK(tmnuHandle(&s, &e) == 1 && r.n == 3);

    /* the capture ended: taps are rows again */
    tmnuSetCapture(&s, 0);
    tapAt(&s, 60, 28 + 12 + 5);
    CHECK(r.n == 4 && r.kind[3] == TM_EV_ROW);

    /* a menu that is not drawn claims nothing even with a capture flag */
    tmnuSetCapture(&s, 1);
    tmnuSetLayout(&s, NULL);
    e = ev(TR_DOWN, 3, 1000, 500);
    CHECK(tmnuHandle(&s, &e) == 0 && r.n == 4);
}

static void testMenu(void) {
    TmState s;
    TmLayout l;
    Rec r;
    TrEvent e;

    memset(&r, 0, sizeof(r));
    tmnuInit(&s, recSink, &r, recGuard, &r);
    tmnuSetView(&s, 720, 180, 960, 720);
    menuLayout(&l);

    /* not drawn: nothing is claimed */
    e = ev(TR_DOWN, 1, 1000, 500);
    CHECK(tmnuHandle(&s, &e) == 0);
    tmnuSetLayout(&s, &l);

    /* a tap on a label and on a value, in frame pixels mapped through the view rect */
    tapAt(&s, 60, 28 + 12 + 5);
    CHECK(r.n == 1 && r.kind[0] == TM_EV_ROW && r.a[0] == 6 && r.b[0] == TM_ZONE_LABEL);
    tapAt(&s, 200, 28 + 12 + 5);
    CHECK(r.n == 2 && r.kind[1] == TM_EV_ROW && r.a[1] == 6 && r.b[1] == TM_ZONE_VALUE);
    /* a slider's bar: left half decreases, right half increases */
    tapAt(&s, 170, 28 + 36 + 5);
    CHECK(r.n == 3 && r.a[2] == 8 && r.b[2] == TM_ZONE_DEC);
    tapAt(&s, 240, 28 + 36 + 5);
    CHECK(r.n == 4 && r.b[3] == TM_ZONE_INC);
    /* a tab, and the strip beyond the visible tabs (carousel arrows) */
    tapAt(&s, 130, 10);
    CHECK(r.n == 5 && r.kind[4] == TM_EV_TAB && r.a[4] == 2);
    tapAt(&s, 20, 10);
    CHECK(r.n == 6 && r.kind[5] == TM_EV_TAB_STEP && r.a[5] == -1);
    tapAt(&s, 280, 10);
    CHECK(r.n == 7 && r.kind[6] == TM_EV_TAB_STEP && r.a[6] == 1);
    /* the footer of the panel does nothing; outside the panel closes */
    tapAt(&s, 160, 215);
    CHECK(r.n == 7);
    tapAt(&s, 8, 100);
    CHECK(r.n == 8 && r.kind[7] == TM_EV_CLOSE);
    /* a device line */
    l.deviceOn = 1; l.deviceY = 24; l.deviceH = 12;
    tmnuSetLayout(&s, &l);
    tapAt(&s, 160, 28);
    CHECK(r.n == 9 && r.kind[8] == TM_EV_DEVICE);

    /* a point the guard owns (a virtual button) is declined, so the pad gets it */
    e = ev(TR_DOWN, 2, 50, 500);
    CHECK(tmnuHandle(&s, &e) == 0);
    r.guardAll = 1;
    e = ev(TR_DOWN, 3, 1000, 500);
    CHECK(tmnuHandle(&s, &e) == 0);
    r.guardAll = 0;

    /* a swipe scrolls one row per row pitch and is not a tap */
    r.n = 0;
    e = ev(TR_DOWN, 4, 1000, 180 + 100 * 3);
    CHECK(tmnuHandle(&s, &e) == 1);
    e = ev(TR_MOVE, 4, 1000, 180 + 100 * 3 - 12);
    tmnuHandle(&s, &e); /* inside the slop: nothing yet */
    CHECK(r.n == 0);
    e = ev(TR_MOVE, 4, 1000, 180 + (100 - 30) * 3); /* 30 frame pixels up */
    tmnuHandle(&s, &e);
    CHECK(r.n >= 1 && r.kind[0] == TM_EV_SCROLL && r.a[0] == 1);
    {
        int scrolls = r.n, k;

        for (k = 0; k < r.n; k++) {
            CHECK(r.kind[k] == TM_EV_SCROLL && r.a[k] == 1);
        }
        CHECK(scrolls == 2); /* 24 of the 30 px count after the slop crossing: two rows */
        e = ev(TR_UP, 4, 1000, 180 + 70 * 3);
        tmnuHandle(&s, &e);
        CHECK(r.n == scrolls); /* lifting after a swipe taps nothing */
        e = ev(TR_DOWN, 5, 1000, 180 + 70 * 3);
        tmnuHandle(&s, &e);
        e = ev(TR_MOVE, 5, 1000, 180 + 100 * 3); /* back down: rows up */
        tmnuHandle(&s, &e);
        CHECK(r.n > scrolls && r.kind[r.n - 1] == TM_EV_SCROLL && r.a[r.n - 1] == -1);
    }

    /* the scaling mode changes the view rect: the same picture point still hits (integer scale 2x) */
    tmnuSetView(&s, 0, 0, 640, 480);
    l.deviceOn = 0;
    tmnuSetLayout(&s, &l);
    r.n = 0;
    e = ev(TR_UP, 5, 0, 0);
    tmnuHandle(&s, &e);
    r.n = 0;
    e = ev(TR_DOWN, 6, 120.0f, (28 + 5) * 2.0f);
    CHECK(tmnuHandle(&s, &e) == 1);
    e.kind = TR_UP;
    tmnuHandle(&s, &e);
    CHECK(r.n == 1 && r.kind[0] == TM_EV_ROW && r.a[0] == 5 && r.b[0] == TM_ZONE_LABEL);

    /* the menu going away drops the gesture: no tap from a finger that lands, then the menu closes */
    e = ev(TR_DOWN, 7, 120.0f, (28 + 5) * 2.0f);
    tmnuHandle(&s, &e);
    tmnuSetLayout(&s, NULL);
    r.n = 0;
    e.kind = TR_UP;
    tmnuHandle(&s, &e);
    CHECK(r.n == 0);
    e = ev(TR_DOWN, 8, 120.0f, 66.0f);
    CHECK(tmnuHandle(&s, &e) == 0);
}

/* ---- tap the picture to turn a page (phase 1b) ---- */

static void testTap(void) {
    TtState t;
    TrEvent e;

    ttInit(&t);
    ttSetView(&t, 720, 180, 960, 720);
    /* no page waits: the finger is nobody's and nothing is asked */
    e = ev(TR_DOWN, 1, 1000, 500);
    CHECK(ttHandle(&t, &e) == 0 && ttTake(&t) == 0);
    /* a page waits: a tap on the picture asks for one press */
    ttArm(&t, TT_WAITING, 77);
    e = ev(TR_DOWN, 2, 1000, 500);
    CHECK(ttHandle(&t, &e) == 1 && ttTake(&t) == 1 && ttTake(&t) == 0);
    /* the same page is not answered twice, even while the game still shows it */
    ttArm(&t, TT_WAITING, 77);
    e = ev(TR_DOWN, 3, 1000, 500);
    CHECK(ttHandle(&t, &e) == 0 && ttTake(&t) == 0);
    /* the next page is */
    ttArm(&t, TT_WAITING, 78);
    e = ev(TR_DOWN, 4, 1000, 500);
    CHECK(ttHandle(&t, &e) == 1 && ttTake(&t) == 1);
    /* outside the picture (the bars) is not a tap */
    ttArm(&t, TT_WAITING, 79);
    e = ev(TR_DOWN, 5, 300, 500);
    CHECK(ttHandle(&t, &e) == 0 && ttTake(&t) == 0);
    /* the prompt goes away (a question appears): nothing to answer */
    ttArm(&t, TT_NONE, 79);
    e = ev(TR_DOWN, 6, 1000, 500);
    CHECK(ttHandle(&t, &e) == 0 && ttTake(&t) == 0);
    /* and it comes back as the same page: still armed once */
    ttArm(&t, TT_WAITING, 79);
    e = ev(TR_DOWN, 7, 1000, 500);
    CHECK(ttHandle(&t, &e) == 1 && ttTake(&t) == 1);
    /* two taps: a page being typed is completed by the first, the finished page is turned by the second */
    ttArm(&t, TT_TYPING, 80);
    e = ev(TR_DOWN, 8, 1000, 500);
    CHECK(ttHandle(&t, &e) == 1 && ttTake(&t) == 1 && ttTake(&t) == 0);
    /* the same typing page does not take a second press */
    ttArm(&t, TT_TYPING, 80);
    e = ev(TR_DOWN, 9, 1000, 500);
    CHECK(ttHandle(&t, &e) == 0 && ttTake(&t) == 0);
    /* it finishes: the waiting page has its own identity, so one more tap turns it */
    ttArm(&t, TT_WAITING, 80);
    e = ev(TR_DOWN, 10, 1000, 500);
    CHECK(ttHandle(&t, &e) == 1 && ttTake(&t) == 1);
    /* the next page of the same window is typed: answered again (the finished page cleared the old answer) */
    ttArm(&t, TT_TYPING, 80);
    e = ev(TR_DOWN, 12, 1000, 500);
    CHECK(ttHandle(&t, &e) == 1 && ttTake(&t) == 1);
    ttArm(&t, TT_WAITING, 80);
    /* one tap is one action: the page that appears next is not answered by the same finger lifting */
    ttArm(&t, TT_WAITING, 81);
    e = ev(TR_UP, 10, 1000, 500);
    CHECK(ttHandle(&t, &e) == 1 && ttTake(&t) == 0);
    /* a question (kind none) is never answered, typing or waiting */
    ttArm(&t, TT_NONE, 82);
    e = ev(TR_DOWN, 11, 1000, 500);
    CHECK(ttHandle(&t, &e) == 0 && ttTake(&t) == 0);
}

int main(void) {
    testMenu();
    testCaptureTap();
    testTap();
    testRouter();
    testStick();
    testLayout();
    testFingers();
    testVisibility();
    if (failures != 0) {
        fprintf(stderr, "touchtest: %d failure(s)\n", failures);
        return 1;
    }
    printf("touchtest: OK\n");
    return 0;
}
