/*
 * The on-screen virtual controller on SDL (see touch_sdl.h). The pure parts are touch_pad.c and touch_router.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include <PsyX/PsyX_overlay.h>

#include "controls_screen.h"
#include "input_cfg.h"
#include "touch_menu.h"
#include "touch_pad.h"
#include "touch_tap.h"
#include "touch_sdl.h"

/* PsyCross' window size (PsyX_main.cpp), the pixels the overlay and the finger coordinates are in */
extern int g_windowWidth;
extern int g_windowHeight;

#define STICK_ON 8000 /* a real pad's stick push that counts as "the pad is in use" */

static SDL_SpinLock lock_;
static TrRouter router_;
static TpState pad_;
static TpVis vis_;
static TmState menu_;
static TtState tap_;
static int inited_;
static int modeSetting_ = TP_MODE_OFF;
static int opacityPct_ = 55;
static int sizePct_ = 100;
static unsigned prevMask_;
static unsigned latched_; /* every bit seen down since the last frame merge: a tap shorter than a frame still counts */
static int recount_ = 1;
static int layoutKey_[7];

TrRouter *touchRouter(void) {
    return &router_;
}

/* ---- the pad count and the mode ---- */

static int countPads(void) {
    int i, n = 0;

    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            n++;
        }
    }
    return n;
}

/* DW3_TOUCH=on|off|auto forces the mode (the harness, a desktop test): the file's value does not override it */
static int effectiveMode(void) {
    const char *e = getenv("DW3_TOUCH");

    if (e != NULL) {
        return strcmp(e, "on") == 0 ? TP_MODE_ON : (strcmp(e, "auto") == 0 ? TP_MODE_AUTO : TP_MODE_OFF);
    }
    return modeSetting_;
}

static void applyMode(void) {
    int mode = effectiveMode();

    fprintf(stderr, "touch: mode %d\n", mode);

    SDL_AtomicLock(&lock_);
    tpVisSetMode(&vis_, mode);
    if (mode == TP_MODE_OFF) {
        trReleaseAll(&router_);
        prevMask_ = 0;
    }
    SDL_AtomicUnlock(&lock_);
#if !defined(__ANDROID__) && defined(SDL_HINT_MOUSE_TOUCH_EVENTS)
    /* on a desktop the mouse stands in for a finger while the controls are on (testing without a touch screen) */
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, mode == TP_MODE_OFF ? "0" : "1");
#endif
}

/* ---- the layout follows the window and the scaling mode ---- */

/* Caller holds the lock */
static void ensureLayout(void) {
    int key[7], gx = 0, gy = 0, gw = 0, gh = 0;
    TpLayout l;

    if (g_windowWidth <= 0 || g_windowHeight <= 0) {
        return;
    }
    PsyX_GetViewRect(&gx, &gy, &gw, &gh);
    key[0] = g_windowWidth;
    key[1] = g_windowHeight;
    key[2] = gx;
    key[3] = gy;
    key[4] = gw;
    key[5] = gh;
    key[6] = sizePct_;
    if (memcmp(key, layoutKey_, sizeof(key)) == 0) {
        return;
    }
    memcpy(layoutKey_, key, sizeof(key));
    tmnuSetView(&menu_, gx, gy, gw, gh);
    ttSetView(&tap_, gx, gy, gw, gh);
    tpLayoutBuild(&l, g_windowWidth, g_windowHeight, gx, gy, gw, gh, sizePct_);
    tpSetLayout(&pad_, &l);
}

/* ---- events ---- */

/* The options menu is driven by SDL events, not by the pad word (shim/controls_screen.c): the virtual buttons
   feed it the same actions a pad's buttons do. Select opens and closes it, like a real pad's Select. */
static void menuActions(unsigned before, unsigned now) {
    unsigned down = now & ~before, up = before & ~now;
    static const struct {
        unsigned bit;
        int action;
    } dirs[] = {{TP_UP, CS_ACT_UP}, {TP_DOWN, CS_ACT_DOWN}, {TP_LEFT, CS_ACT_LEFT}, {TP_RIGHT, CS_ACT_RIGHT}};
    int i;

    if (down & TP_SELECT) {
        csQueueAction(CS_ACT_TOGGLE);
    }
    if (csIsOpen()) {
        if (down & TP_CROSS) {
            csQueueAction(CS_ACT_CONFIRM);
        }
        if (down & TP_CIRCLE) {
            csQueueAction(CS_ACT_CANCEL);
        }
        if (down & TP_L1) {
            csQueueAction(CS_ACT_TAB_PREV);
        }
        if (down & TP_R1) {
            csQueueAction(CS_ACT_TAB_NEXT);
        }
    }
    for (i = 0; i < 4; i++) {
        if (down & dirs[i].bit) {
            csQueueHold(dirs[i].action, 1);
        } else if (up & dirs[i].bit) {
            csQueueHold(dirs[i].action, 0);
        }
    }
}

static void fingerEvent(const SDL_Event *e) {
    TrEvent t;
    unsigned now;

    if (!tpVisTakesTouch(&vis_)) {
        return;
    }
    t.kind = e->type == SDL_FINGERDOWN ? TR_DOWN : (e->type == SDL_FINGERUP ? TR_UP : TR_MOVE);
    t.finger = (long long)e->tfinger.fingerId;
    SDL_AtomicLock(&lock_);
    ensureLayout();
    t.winW = g_windowWidth;
    t.winH = g_windowHeight;
    t.x = e->tfinger.x * (float)g_windowWidth;
    t.y = e->tfinger.y * (float)g_windowHeight;
    if (t.kind == TR_DOWN) {
        tpVisEvent(&vis_, TP_EV_TOUCH); /* a touch brings the controls back in Auto */
    }
    trFeed(&router_, &t);
    now = tpVisShown(&vis_) ? tpMask(&pad_) : 0;
    menuActions(prevMask_, now);
    prevMask_ = now;
    latched_ |= now;
    SDL_AtomicUnlock(&lock_);
}

static void padUsed(void) {
    SDL_AtomicLock(&lock_);
    tpVisEvent(&vis_, TP_EV_PAD_USED);
    if (!tpVisShown(&vis_)) {
        trReleaseAll(&router_);
        menuActions(prevMask_, 0);
        prevMask_ = 0;
    }
    SDL_AtomicUnlock(&lock_);
}

static int touchWatch(void *userdata, SDL_Event *e) {
    (void)userdata;
    switch (e->type) {
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
        fingerEvent(e);
        break;
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
        recount_ = 1; /* counted on the game thread, once the device list is settled */
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        padUsed();
        break;
    case SDL_CONTROLLERAXISMOTION:
        if (abs(e->caxis.value) > STICK_ON && e->caxis.axis < SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            padUsed();
        }
        break;
#ifndef __ANDROID__
    case SDL_KEYDOWN: /* Android's Back and volume keys are not a keyboard in use */
        padUsed();
        break;
#endif
    default:
        break;
    }
    return 0;
}

unsigned touchPadMerge(unsigned short *buttons) {
    unsigned mask = 0;

    if (!inited_) {
        return 0;
    }
    SDL_AtomicLock(&lock_);
    if (recount_) {
        recount_ = 0;
        tpVisPads(&vis_, countPads());
        fprintf(stderr, "touch: mode %d, %d pad(s) connected, controls %s\n", vis_.mode, vis_.pads, tpVisShown(&vis_) ? "shown" : "hidden");
    }
    if (tpVisShown(&vis_)) {
        mask = tpMask(&pad_) | latched_;
    }
    latched_ = 0;
    SDL_AtomicUnlock(&lock_);
    *buttons = (unsigned short)(*buttons & ~mask);
    return mask;
}

int touchShown(void) {
    int shown;

    SDL_AtomicLock(&lock_);
    shown = tpVisShown(&vis_);
    SDL_AtomicUnlock(&lock_);
    return shown;
}

/* ---- drawing ---- */

static void shape(int kind, float x, float y, float w, float h, float r, float g, float b, float a, float param) {
    PsyXOverlayShape s;

    memset(&s, 0, sizeof(s));
    s.kind = kind;
    s.x = x - w * 0.5f;
    s.y = y - h * 0.5f;
    s.w = w;
    s.h = h;
    s.r = r;
    s.g = g;
    s.b = b;
    s.a = a;
    s.param = param;
    PsyX_OverlayShape(&s);
}

static void glyph(char c, float x, float y, float w, float h, float a) {
    static const unsigned char L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const unsigned char R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const unsigned char N1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const unsigned char N2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    const unsigned char *rows = c == 'L' ? L : (c == 'R' ? R : (c == '1' ? N1 : N2));
    PsyXOverlayShape s;

    memset(&s, 0, sizeof(s));
    s.kind = PSYX_SHAPE_GLYPH;
    s.x = x - w * 0.5f;
    s.y = y - h * 0.5f;
    s.w = w;
    s.h = h;
    s.r = s.g = s.b = 1.0f;
    s.a = a;
    memcpy(s.rows, rows, 7);
    PsyX_OverlayShape(&s);
}

/* A dark body with a light rim reads over a bright picture and over the black bars alike; held, it fills light. */
static void base(const TpButton *b, int down, float u, float alpha) {
    int kind = b->shape == TP_SHAPE_DISC ? PSYX_SHAPE_DISC : PSYX_SHAPE_PILL;
    float t = 0.05f * u;

    if (down) {
        shape(kind, b->x, b->y, b->w, b->h, 0.92f, 0.92f, 0.96f, alpha * 0.75f, 0);
        return;
    }
    shape(kind, b->x, b->y, b->w, b->h, 0.88f, 0.88f, 0.94f, alpha * 0.55f, 0);
    shape(kind, b->x, b->y, b->w - 2.0f * t, b->h - 2.0f * t, 0.04f, 0.04f, 0.07f, alpha * 0.80f, 0);
}

static void drawButton(const TpButton *b, int down, float u, float alpha) {
    float sym = alpha * (down ? 1.0f : 0.95f);
    float lw = 0.09f * u;

    switch (b->bit) {
    case TP_TRIANGLE:
        base(b, down, u, alpha);
        shape(PSYX_SHAPE_TRIANGLE, b->x, b->y, b->w, b->h, 0.30f, 0.85f, 0.60f, sym, lw);
        break;
    case TP_CIRCLE:
        base(b, down, u, alpha);
        shape(PSYX_SHAPE_CIRCLE, b->x, b->y, b->w, b->h, 0.95f, 0.38f, 0.38f, sym, lw);
        break;
    case TP_CROSS:
        base(b, down, u, alpha);
        shape(PSYX_SHAPE_CROSS, b->x, b->y, b->w, b->h, 0.42f, 0.60f, 1.00f, sym, lw);
        break;
    case TP_SQUARE:
        base(b, down, u, alpha);
        shape(PSYX_SHAPE_SQUARE, b->x, b->y, b->w, b->h, 0.92f, 0.55f, 0.85f, sym, lw);
        break;
    case TP_SELECT:
        base(b, down, u, alpha);
        shape(PSYX_SHAPE_BARS, b->x, b->y, b->h, b->h, 1.0f, 1.0f, 1.0f, sym, 0);
        break;
    case TP_START:
        base(b, down, u, alpha);
        shape(PSYX_SHAPE_PLAY, b->x, b->y, b->h, b->h, 1.0f, 1.0f, 1.0f, sym, 0);
        break;
    default: { /* L1 L2 R1 R2 */
        char side = (b->bit == TP_L1 || b->bit == TP_L2) ? 'L' : 'R';
        char num = (b->bit == TP_L1 || b->bit == TP_R1) ? '1' : '2';
        float gh = b->h * 0.6f, gw = gh * 5.0f / 7.0f * 0.9f;

        base(b, down, u, alpha);
        glyph(side, b->x - gw * 0.75f, b->y, gw, gh, sym);
        glyph(num, b->x + gw * 0.75f, b->y, gw, gh, sym);
        break;
    }
    }
}

static void touchOverlayDraw(void) {
    TpLayout l;
    unsigned mask;
    float ox, oy, tx, ty, alpha;
    int shown, active, i;

    SDL_AtomicLock(&lock_);
    ensureLayout();
    shown = tpVisShown(&vis_);
    l = pad_.layout;
    mask = tpMask(&pad_);
    active = tpStickPose(&pad_, &ox, &oy, &tx, &ty);
    SDL_AtomicUnlock(&lock_);
    if (!shown || l.count == 0) {
        return;
    }
    alpha = (float)opacityPct_ / 100.0f;
    if (!active) {
        ox = tx = l.stickX;
        oy = ty = l.stickY;
    }
    /* the stick: a ring where it floats, a small thumb disc where the finger is */
    shape(PSYX_SHAPE_DISC, ox, oy, l.stickR * 2.0f, l.stickR * 2.0f, 0.80f, 0.80f, 0.86f, alpha * (active ? 0.16f : 0.10f), 0);
    shape(PSYX_SHAPE_RING, ox, oy, l.stickR * 2.0f, l.stickR * 2.0f, 0.90f, 0.90f, 0.95f, alpha * (active ? 0.70f : 0.45f), 0.07f * l.unit);
    shape(PSYX_SHAPE_DISC, tx, ty, l.unit * 1.1f, l.unit * 1.1f, 0.90f, 0.90f, 0.95f, alpha * (active ? 0.75f : 0.38f), 0);
    for (i = 0; i < l.count; i++) {
        drawButton(&l.buttons[i], (mask & l.buttons[i].bit) != 0, l.unit, alpha);
    }
}

/* ---- tapping the options menu (T7c phase 1) and the dialogue (phase 1b) ---- */

/* The menu leaves a point to the virtual pad when a button is there, or when it is a bar's stick zone (the stick is
   how the pad moves through the menu too). */
static int menuGuard(void *ctx, float x, float y) {
    int gx = 0, gy = 0, gw = 0, gh = 0;

    (void)ctx;
    if (tpHitButton(&pad_.layout, x, y) >= 0) {
        return 1;
    }
    PsyX_GetViewRect(&gx, &gy, &gw, &gh);
    return (x < (float)gx || x >= (float)(gx + gw)) && tpInStickZone(&pad_.layout, x, y);
}

/* Runs in the event watch: the menu's own queue carries the action to the main thread, like a key press */
static void menuSink(void *ctx, int kind, int a, int b) {
    (void)ctx;
    switch (kind) {
    case TM_EV_TAB:
        csQueueAction(CS_ACT_TAB_TO(a));
        break;
    case TM_EV_ROW:
        csQueueAction(CS_ACT_ROW_TAP(a, b));
        break;
    case TM_EV_SCROLL:
        csQueueAction(a > 0 ? CS_ACT_ROW_NEXT : CS_ACT_ROW_PREV);
        break;
    case TM_EV_CLOSE:
        csQueueAction(CS_ACT_CANCEL);
        break;
    case TM_EV_TAB_STEP:
        csQueueAction(a > 0 ? CS_ACT_TAB_NEXT : CS_ACT_TAB_PREV);
        break;
    case TM_EV_DEVICE:
        csQueueAction(CS_ACT_LEFT); /* the Controls tab's Left / Right swap the device */
        break;
    case TM_EV_CAPTURE_CANCEL:
        inputCapCancel(); /* the same path as Escape; the screen notices it in csSyncCapture */
        break;
    default:
        return;
    }
    fprintf(stderr, "touch: menu tap kind %d (%d, %d)\n", kind, a, b);
}

static int menuHandler(void *ctx, const TrEvent *ev) {
    int r;

    if (ev->kind == TR_DOWN && !csIsOpen()) {
        return 0;
    }
    tmnuSetCapture((TmState *)ctx, csWaitingRow() >= 0);
    r = tmnuHandle(ctx, ev);
    return r;
}

static int tapHandler(void *ctx, const TrEvent *ev) {
    int claimed = ttHandle(ctx, ev);

    if (claimed && ev->kind == TR_DOWN) {
        fprintf(stderr, "touch: tap on the picture answers a typing or waiting page\n");
    }
    return claimed;
}

void touchMenuLayout(const void *layout) {
    SDL_AtomicLock(&lock_);
    tmnuSetLayout(&menu_, (const TmLayout *)layout);
    SDL_AtomicUnlock(&lock_);
}

void touchTextArm(int kind, int key) {
    SDL_AtomicLock(&lock_);
    ttArm(&tap_, tpVisTakesTouch(&vis_) ? kind : TT_NONE, key);
    SDL_AtomicUnlock(&lock_);
}

int touchTextTake(void) {
    int p;

    SDL_AtomicLock(&lock_);
    p = ttTake(&tap_);
    SDL_AtomicUnlock(&lock_);
    return p;
}

void touchHarnessTap(int fx, int fy) {
    SDL_Event e;
    int gx = 0, gy = 0, gw = 0, gh = 0, k;

    if (g_windowWidth <= 0 || g_windowHeight <= 0) {
        return;
    }
    PsyX_GetViewRect(&gx, &gy, &gw, &gh);
    memset(&e, 0, sizeof(e));
    e.tfinger.touchId = 1;
    e.tfinger.fingerId = 4242;
    e.tfinger.x = ((float)gx + (float)fx * (float)gw / 320.0f) / (float)g_windowWidth;
    e.tfinger.y = ((float)gy + (float)fy * (float)gh / 240.0f) / (float)g_windowHeight;
    for (k = 0; k < 2; k++) {
        e.type = k == 0 ? SDL_FINGERDOWN : SDL_FINGERUP;
        SDL_PushEvent(&e);
    }
}

/* ---- init and settings ---- */

static int virtualPad(void *ctx, const TrEvent *ev) {
    return tpHandle(ctx, ev);
}

void touchInit(void) {
    if (inited_) {
        return;
    }
    inited_ = 1;
    trInit(&router_);
    tpInit(&pad_);
    tpVisInit(&vis_, TP_MODE_OFF, countPads());
    tmnuInit(&menu_, menuSink, NULL, menuGuard, NULL);
    ttInit(&tap_);
    trAdd(&router_, menuHandler, &menu_); /* the menu first: it declines the virtual buttons itself */
    trAdd(&router_, virtualPad, &pad_);
    trAdd(&router_, tapHandler, &tap_);   /* last: a virtual button is never a tap on the picture */
    SDL_AddEventWatch(touchWatch, NULL);
    PsyX_SetOverlayHook(touchOverlayDraw);
}

static void applyTouchMode(int value) {
    modeSetting_ = value == 1 ? TP_MODE_ON : (value == 2 ? TP_MODE_AUTO : TP_MODE_OFF);
    applyMode();
}

static void applyOpacity(int idx) {
    static const int pct[] = {25, 40, 55, 70, 85};

    opacityPct_ = idx >= 0 && idx < 5 ? pct[idx] : 55;
}

static void applySize(int idx) {
    static const int pct[] = {80, 100, 125};

    sizePct_ = idx >= 0 && idx < 3 ? pct[idx] : 100;
}

void touchRowsInit(void) {
    static const char *const modeNames[] = {"Off", "On", "Auto"};
    static const char *const modeTokens[] = {"off", "on", "auto"};
    static const char *const opacityNames[] = {"25%", "40%", "55%", "70%", "85%"};
    static const char *const sizeNames[] = {"Small", "Medium", "Large"};
    CsWidget w;

    touchInit();
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Touch controls", .key = "touch_controls", .names = modeNames,
                   .tokens = modeTokens, .count = 3, .apply = applyTouchMode,
#ifdef __ANDROID__
                   .value = 2, /* a phone has no pad until one is plugged in: shown, and hidden while a pad is in use */
#endif
                   .desc = "On-screen pad. Auto hides it while a pad is used.",
                   .comment = "On-screen controller (a stick, the four face buttons, L1 L2 R1 R2, Start and Select): off, on, or auto (shown until a real pad is used, back on touch)."};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Touch opacity", .key = "touch_opacity", .names = opacityNames,
                   .count = 5, .apply = applyOpacity, .value = 2,
                   .desc = "How solid the on-screen pad looks.",
                   .comment = "Opacity of the on-screen controller: 25%, 40%, 55% (default), 70% or 85%."};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Touch size", .key = "touch_size", .names = sizeNames,
                   .count = 3, .apply = applySize, .value = 1,
                   .desc = "Size of the on-screen pad's buttons.",
                   .comment = "Size of the on-screen controller's buttons: small, medium (default) or large."};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    applyMode(); /* DW3_TOUCH names the mode whether or not a file value was applied */
}
