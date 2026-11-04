#ifndef PC_TOUCH_MENU_H
#define PC_TOUCH_MENU_H

/*
 * Tapping the port's own options menu (feature android T7c, phase 1), pure logic: no SDL, no window.
 *
 * The drawing code (controls_draw.c) records where it put the tabs and rows of the frame as a TmLayout, in the
 * 320x240 logical frame. A router handler (tmnuHandle) maps window pixels to that frame with the view rect the
 * renderer uses (PsyX_GetViewRect, so it holds in every scaling mode), turns a finger into a tap or a swipe and
 * reports it through a sink: pick a tab, tap a row, scroll, close. touch_sdl.c turns those into the menu's own
 * actions (controls_screen.h), which the main thread applies like a key press.
 *
 * pc/touchtest.c drives it.
 */
#include "touch_router.h"

#define TM_MAX_ROWS 24
#define TM_MAX_TABS 8
#define TM_SLOP 6 /* frame pixels a finger may drift and still be a tap */

/* Where in a row a tap fell. DEC / INC: the left / right half of a slider's bar. */
enum { TM_ZONE_LABEL = 0, TM_ZONE_VALUE, TM_ZONE_DEC, TM_ZONE_INC };

typedef struct TmRow {
    int index;  /* the row number in its tab */
    int y, h;   /* frame pixels */
    int slider; /* 1 when the value is a bar */
} TmRow;

typedef struct TmLayout {
    int valid; /* 0: the menu is not drawn (closed, opening): nothing is claimed */
    int frameW, frameH;
    int panelX, panelY, panelW, panelH;
    int tabY, tabH;
    int tabCount;
    struct {
        int tab, x, w;
    } tabs[TM_MAX_TABS]; /* the tabs drawn now (a carousel shows a run of them) */
    int deviceOn, deviceY, deviceH; /* the Keyboard / Controller line (Controls tab) */
    int rowX, rowW;
    int valueX;       /* a tap at or right of this is on the value */
    int barX, barW;   /* a slider's bar */
    int rowStep;      /* the rows' pitch: a swipe of this many pixels moves the cursor one row */
    int rowCount;
    TmRow rows[TM_MAX_ROWS];
} TmLayout;

/* What a gesture asks for */
enum {
    TM_EV_TAB = 0,  /* a = tab number */
    TM_EV_ROW,      /* a = row index, b = zone */
    TM_EV_SCROLL,   /* a = +1 (cursor down) or -1 (up) */
    TM_EV_CLOSE,
    TM_EV_TAB_STEP, /* a = +1 / -1: a carousel arrow side */
    TM_EV_DEVICE,
    TM_EV_CAPTURE_CANCEL /* a tap while a "Press a key" capture waits: the touch player's way out */
};

typedef void (*TmSink)(void *ctx, int kind, int a, int b);
/* Returns nonzero for a window point that something else owns (a virtual button): the menu leaves it alone. */
typedef int (*TmGuard)(void *ctx, float x, float y);

typedef struct TmState {
    TmLayout layout;
    int vx, vy, vw, vh; /* the game picture in the window */
    TmSink sink;
    void *sinkCtx;
    TmGuard guard;
    void *guardCtx;
    int capturing; /* a press-to-bind capture waits (set by the host before each event) */
    int used;
    long long finger;
    float sx, sy, lastY; /* frame pixels */
    float acc;
    int moved;
} TmState;

void tmnuInit(TmState *s, TmSink sink, void *sinkCtx, TmGuard guard, void *guardCtx);
/* Copies the layout; NULL marks the menu as not drawn (an unfinished gesture is dropped). */
void tmnuSetLayout(TmState *s, const TmLayout *l);
void tmnuSetView(TmState *s, int x, int y, int w, int h);
/* The router handler shape (ctx = TmState *). Claims a DOWN while the menu is drawn, unless the guard owns the point. */
int tmnuHandle(void *ctx, const TrEvent *ev);
/* While a capture waits, a tap anywhere (a DOWN, even outside the panel or on a virtual button) cancels it and nothing else
   happens: a touch-only player cannot press the key it asks for. Needs the menu drawn (a valid layout). */
void tmnuSetCapture(TmState *s, int capturing);

#endif
