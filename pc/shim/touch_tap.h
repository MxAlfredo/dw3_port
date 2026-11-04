#ifndef PC_TOUCH_TAP_H
#define PC_TOUCH_TAP_H

/*
 * Tap the picture to turn a dialogue page (feature android T7c, phase 1b), pure logic.
 *
 * The game thread says every frame whether a plain next-page prompt waits (ttArm; shim/host_pad.c finds it the
 * way Auto text does, and never for a question or menu). A finger that lands on the game picture while one waits
 * asks for one confirm press; the game thread takes it (ttTake) in the same frame hook that merges the virtual
 * pad, so the press reaches the game with no added frame. The whole picture counts: the game does not tell where
 * its box is, and a press only happens while a page waits, which is when the player would press Cross anyway.
 * The router puts this handler after the virtual pad, so a virtual button is never a tap.
 *
 * pc/touchtest.c drives it.
 */
#include "touch_router.h"

/* What the dialogue shows: a question or menu (or no dialogue) is never answered by a tap */
enum { TT_NONE = 0, TT_TYPING = 1, TT_WAITING = 2 };

typedef struct TtState {
    int vx, vy, vw, vh; /* the game picture in the window */
    int armed;          /* a page waits and has not been answered */
    int key;            /* which page */
    int spentKey, spent;/* the page already answered (key and kind): not armed again */
    int pending;        /* a press waits for the game thread */
} TtState;

void ttInit(TtState *s);
void ttSetView(TtState *s, int x, int y, int w, int h);
/* Once per game frame: `kind` says a plain page is being typed (TT_TYPING: a tap completes it) or waits
   (TT_WAITING: a tap turns it), `key` identifies the page. A page is answered once per kind, so one tap is one
   action: the tap that completed a page never also turns it. */
void ttArm(TtState *s, int kind, int key);
/* Router handler (ctx = TtState *): claims (and answers) a DOWN on the picture while armed. */
int ttHandle(void *ctx, const TrEvent *ev);
/* 1 once per answered tap: the caller presses Confirm for one frame (while typing, that press is what the
   port's fast text takes as "finish this page"). */
int ttTake(TtState *s);

#endif
