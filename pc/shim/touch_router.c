/* The touch router (see touch_router.h): pure, no SDL. */
#include "touch_router.h"

#include <string.h>

void trInit(TrRouter *r) {
    memset(r, 0, sizeof(*r));
}

int trAdd(TrRouter *r, TrHandler fn, void *ctx) {
    if (fn == NULL || r->handlers >= TR_MAX_HANDLERS) {
        return -1;
    }
    r->handler[r->handlers].fn = fn;
    r->handler[r->handlers].ctx = ctx;
    return r->handlers++;
}

static int findOwned(const TrRouter *r, long long finger) {
    int i;

    for (i = 0; i < r->owners; i++) {
        if (r->owned[i].finger == finger) {
            return i;
        }
    }
    return -1;
}

static void dropOwned(TrRouter *r, int slot) {
    r->owned[slot] = r->owned[--r->owners];
}

static void send(TrRouter *r, int owner, int kind, const TrEvent *like) {
    TrEvent e = *like;

    e.kind = kind;
    r->handler[owner].fn(r->handler[owner].ctx, &e);
}

int trFeed(TrRouter *r, const TrEvent *ev) {
    int slot = findOwned(r, ev->finger);
    int i;

    if (ev->kind == TR_DOWN) {
        if (slot >= 0) { /* the UP was missed: the old press ends before the new one starts */
            send(r, r->owned[slot].owner, TR_CANCEL, ev);
            dropOwned(r, slot);
        }
        if (r->owners >= TR_MAX_FINGERS) {
            return 0;
        }
        for (i = 0; i < r->handlers; i++) {
            if (r->handler[i].fn(r->handler[i].ctx, ev)) {
                r->owned[r->owners].finger = ev->finger;
                r->owned[r->owners].owner = i;
                r->owners++;
                return 1;
            }
        }
        return 0;
    }
    if (slot < 0) {
        return 0;
    }
    send(r, r->owned[slot].owner, ev->kind, ev);
    if (ev->kind == TR_UP || ev->kind == TR_CANCEL) {
        dropOwned(r, slot);
    }
    return 1;
}

void trReleaseAll(TrRouter *r) {
    TrEvent e;

    memset(&e, 0, sizeof(e));
    while (r->owners > 0) {
        e.finger = r->owned[r->owners - 1].finger;
        send(r, r->owned[r->owners - 1].owner, TR_CANCEL, &e);
        r->owners--;
    }
}

int trOwned(const TrRouter *r) {
    return r->owners;
}
