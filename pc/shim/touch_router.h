#ifndef PC_TOUCH_ROUTER_H
#define PC_TOUCH_ROUTER_H

/*
 * The touch router: every finger event the port receives is offered to its handlers in priority order, and the
 * first handler that claims a finger on its DOWN owns that finger until it lifts. A handler never sees the
 * fingers another handler owns, and a finger nobody claims is dropped.
 *
 * The on-screen virtual controller (touch_pad.h) is one handler. Another layer (tapping the menus themselves,
 * a gesture) registers beside it with its own priority and its own claim rule, without the layers knowing each
 * other. Pure logic (no SDL, no window): pc/touchtest.c drives it.
 */

enum { TR_DOWN = 0, TR_MOVE, TR_UP, TR_CANCEL };

#define TR_MAX_HANDLERS 8
#define TR_MAX_FINGERS 16

typedef struct TrEvent {
    int kind;          /* TR_DOWN / TR_MOVE / TR_UP; TR_CANCEL is sent to an owner when the router lets go of a finger */
    long long finger;  /* the platform's finger id */
    float x, y;        /* window pixels, origin top left */
    int winW, winH;    /* the window the coordinates belong to */
} TrEvent;

/* A handler gets a DOWN only for fingers no earlier handler claimed (return nonzero to claim it), then every
   MOVE / UP / CANCEL of the fingers it owns. The return value of MOVE / UP / CANCEL is ignored. */
typedef int (*TrHandler)(void *ctx, const TrEvent *ev);

typedef struct TrRouter {
    struct {
        TrHandler fn;
        void *ctx;
    } handler[TR_MAX_HANDLERS];
    int handlers;
    struct {
        long long finger;
        int owner;
    } owned[TR_MAX_FINGERS];
    int owners;
} TrRouter;

void trInit(TrRouter *r);
/* Appends a handler: handlers added first are offered a new finger first. Returns its index, -1 if full. */
int trAdd(TrRouter *r, TrHandler fn, void *ctx);
/* Routes one event. Returns 1 if a handler owns (or just claimed) the finger, 0 if nobody does. */
int trFeed(TrRouter *r, const TrEvent *ev);
/* Lets go of every finger (the window lost focus, the layer was switched off): owners get a TR_CANCEL. */
void trReleaseAll(TrRouter *r);
/* How many fingers are owned now (tests, diagnostics). */
int trOwned(const TrRouter *r);

#endif
