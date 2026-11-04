#ifndef PC_TOUCH_PAD_H
#define PC_TOUCH_PAD_H

/*
 * The on-screen virtual controller, pure logic (feature android T7a): a floating analog stick on the left that
 * becomes the PlayStation pad's eight directions, the four face buttons and Start/Select/L1/R1/L2/R2 as touch
 * targets, and the rule that decides when the controls are shown. No SDL, no window, no GL: pc/touchtest.c drives
 * it; shim/touch_sdl.c feeds it finger events and draws it.
 *
 * A pad mask here uses the PlayStation pad word's bit positions with a SET bit meaning "down" (the raw word the
 * game reads is active low: shim/host_pad.c clears these bits).
 */
#include "touch_router.h"

enum {
    TP_SELECT = 1u << 0,
    TP_START = 1u << 3,
    TP_UP = 1u << 4,
    TP_RIGHT = 1u << 5,
    TP_DOWN = 1u << 6,
    TP_LEFT = 1u << 7,
    TP_L2 = 1u << 8,
    TP_R2 = 1u << 9,
    TP_L1 = 1u << 10,
    TP_R1 = 1u << 11,
    TP_TRIANGLE = 1u << 12,
    TP_CIRCLE = 1u << 13,
    TP_CROSS = 1u << 14,
    TP_SQUARE = 1u << 15
};

#define TP_DIRS (TP_UP | TP_RIGHT | TP_DOWN | TP_LEFT)

/* ---- the stick ---- */

/* A stick pushed (dx, dy) pixels from its origin (y grows downwards) with full travel `radius`: the directions it
   presses. Nothing inside `deadzone` * radius; outside it the angle picks one of eight directions (the four
   diagonals are two bits), each sector 45 degrees wide. */
unsigned tpStickDirs(float dx, float dy, float radius, float deadzone);

/* ---- the layout ---- */

enum { TP_SHAPE_DISC = 0, TP_SHAPE_PILL };

typedef struct TpButton {
    unsigned bit;  /* the pad bit it presses */
    int shape;
    float x, y;    /* centre, window pixels */
    float w, h;    /* size (a disc: w == h == its diameter) */
} TpButton;

#define TP_MAX_BUTTONS 14

typedef struct TpLayout {
    int winW, winH;
    float unit;    /* the size every part scales with */
    int count;
    TpButton buttons[TP_MAX_BUTTONS];
    float stickX, stickY; /* where the stick rests (drawn there while nobody holds it) */
    float stickR;         /* full travel of the stick, pixels */
    float zoneX;          /* the stick takes a finger that lands left of this */
} TpLayout;

#define TP_SIZE_MIN 50
#define TP_SIZE_MAX 200
#define TP_DEADZONE 0.30f

/* Lays the controls out in a window of winW x winH whose game picture occupies (gx, gy, gw, gh): the stick and
   the face buttons go to the middle of the side bars when those are wide enough, else to the window corners.
   sizePct scales everything (100 = the default size). */
void tpLayoutBuild(TpLayout *l, int winW, int winH, int gx, int gy, int gw, int gh, int sizePct);
/* The button under a point (the nearest when targets overlap, slightly oversized to forgive thumbs), -1 for none. */
int tpHitButton(const TpLayout *l, float x, float y);
/* Whether a finger that lands here starts the stick (the left zone, wherever no button is). */
int tpInStickZone(const TpLayout *l, float x, float y);

/* ---- the fingers ---- */

#define TP_MAX_FINGERS 10

typedef struct TpFinger {
    long long id;
    int used;
    int stick;           /* 1: this finger is the stick; 0: a button finger */
    float ox, oy;        /* the stick: where it floats now (the origin follows the thumb past full travel) */
    float cx, cy;        /* the finger now */
} TpFinger;

typedef struct TpState {
    TpLayout layout;
    TpFinger fingers[TP_MAX_FINGERS];
} TpState;

void tpInit(TpState *s);
/* Replaces the layout; a stick finger keeps its place relative to its origin. Does not release fingers. */
void tpSetLayout(TpState *s, const TpLayout *l);
/* The router handler shape (ctx = TpState *): claims a DOWN on a button or in the stick zone (one stick at a time). */
int tpHandle(void *ctx, const TrEvent *ev);
/* Every pad bit the fingers hold now. A finger slid from one button to its neighbour moves the press with it. */
unsigned tpMask(const TpState *s);
/* The stick for drawing: 1 and its origin and thumb (thumb clamped to full travel) if a finger holds it. */
int tpStickPose(const TpState *s, float *ox, float *oy, float *tx, float *ty);
void tpReleaseAll(TpState *s);

/* ---- when the controls show ---- */

enum { TP_MODE_OFF = 0, TP_MODE_ON, TP_MODE_AUTO };
enum { TP_EV_TOUCH = 0, TP_EV_PAD_USED };

typedef struct TpVis {
    int mode;
    int pads;     /* controllers connected now */
    int shown;    /* the Auto state: shown until a real pad is used or connected, back on a touch */
} TpVis;

void tpVisInit(TpVis *v, int mode, int padsConnected);
void tpVisSetMode(TpVis *v, int mode);
void tpVisEvent(TpVis *v, int event);
/* The number of controllers connected now (a recount, so a repeated or late hot-plug event never double counts):
   more than before hides the controls, none left shows them. */
void tpVisPads(TpVis *v, int pads);
/* On: always. Off: never. Auto: the state above. */
int tpVisShown(const TpVis *v);
/* Whether a touch should be processed at all (Off drops every finger; Auto/On take them, a touch also shows the controls). */
int tpVisTakesTouch(const TpVis *v);

#endif
