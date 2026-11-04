/*
 * Host stand-in for the retail-PsyQ GPU calls the battle mode's one
 * direct-submission path makes (src/battle/battle_mode/cursor.c): its cursor
 * bar builds a private two-entry ordering table (ClearOTag, from PsyCross's
 * real LIBGPU) and then asks the GPU itself instead of drawing through the
 * game's Layer world:
 *
 *   IsIdleGPU(max_count)   cursor.c spins on it (`while (IsIdleGPU(0))`),
 *                          so it must report "the GPU is not busy" (0) and
 *                          let the loop pass; the PsyQ header promises the
 *                          count check, but there is no GPU queue on this
 *                          host for it to measure.
 *
 *   ContinueDraw(ins, ot)  PsyQ's "pick the OT draw up where it stopped".
 *                          The bar's DR_MOVE prims copy a VRAM rect into the
 *                          framebuffer, which PsyCross (OpenGL) never shows,
 *                          so here the OT is walked and each move is recorded;
 *                          hostBattleMovesDraw (called by gfx_engine.c before
 *                          the frame is presented) draws the recorded rects
 *                          to the window as 16-bit textured quads, the way
 *                          host_video.c draws a movie frame.
 *
 * This is a drawing unit (PsyCross headers). PsyCross itself provides
 * neither call (grep external/psycross: no symbol), so nothing is
 * overridden -- these are the only definitions in the link.
 */
#include "common.h"
#include "host.h"
#include <libgpu.h>

#define MOVE_MAX      8
#define MOVE_COL_W    64 /* a 16-bit texture page column, in pixels */
#define MOVE_PAGE_H   256
#define MOVE_TILE_MAX 32
#define MOVE_SCREEN_W 320
#define MOVE_SCREEN_H 240
#define MOVE_WALK_MAX 16

typedef struct {
    s16 srcX, srcY, w, h, dstX, dstY;
} HostMove;

static HostMove moves[MOVE_MAX];
static int moveCount;

int IsIdleGPU(int max_count) {
    (void)max_count;
    return 0;
}

/* Records the DR_MOVE prims of the OT. Each call replaces the previous list:
   the cursor callback runs every vsync and re-sends the same bar. */
void ContinueDraw(u_int *insaddr, u_int *contaddr) {
    P_TAG *node = (P_TAG *)insaddr;
    int guard;

    (void)contaddr;
    moveCount = 0;
    for (guard = 0; node != NULL && guard < MOVE_WALK_MAX; guard++) {
        if (node->len == 5) {
            DR_MOVE *m = (DR_MOVE *)node;

            if (m->code[1] == 0x80000000u) {
                u_int dst = m->code[3];
                int dstY = (s16)(dst >> 16);

                /* the second of the twin moves targets the other display
                   buffer (+0x100); only the first is on this window */
                if (dstY < MOVE_PAGE_H && moveCount < MOVE_MAX) {
                    HostMove *r = &moves[moveCount++];

                    r->srcX = (s16)(m->code[2] & 0xFFFF);
                    r->srcY = (s16)(m->code[2] >> 16);
                    r->dstX = (s16)(dst & 0xFFFF);
                    r->dstY = (s16)dstY;
                    r->w = (s16)(m->code[4] & 0xFFFF);
                    r->h = (s16)(m->code[4] >> 16);
                }
            }
        }
        if (isendprim(node)) {
            break;
        }
        node = (P_TAG *)nextPrim(node);
    }
}

/* Draws the recorded moves (VRAM rect -> screen) and forgets them. */
void hostBattleMovesDraw(void) {
    static POLY_FT4 tiles[MOVE_TILE_MAX];
    static OT_TAG ot[2];
    static DRAWENV env;
    int tileCount = 0;
    int i;

    if (moveCount == 0) {
        return;
    }
    SetDefDrawEnv(&env, 0, 0, MOVE_SCREEN_W, MOVE_SCREEN_H);
    env.dfe = 1;
    env.isbg = 0;
    PutDrawEnv(&env);
    ClearOTagR((u_long *)ot, 2);

    for (i = 0; i < moveCount; i++) {
        const HostMove *r = &moves[i];
        int col = 0;

        /* one tile per texture-page column the source crosses */
        while (col < r->w) {
            int sx = r->srcX + col;
            int tw = MOVE_COL_W - (sx & (MOVE_COL_W - 1));
            int sy = r->srcY;
            int th = MOVE_PAGE_H - (sy & (MOVE_PAGE_H - 1));
            int u = sx & (MOVE_COL_W - 1);
            int v = sy & (MOVE_PAGE_H - 1);
            int x = r->dstX + col;
            int y = r->dstY;
            POLY_FT4 *t;

            if (tw > r->w - col) {
                tw = r->w - col;
            }
            if (th > r->h) {
                th = r->h;
            }
            if (tileCount >= MOVE_TILE_MAX) {
                break;
            }
            t = &tiles[tileCount++];
            SetPolyFT4(t);
            setRGB0(t, 0x80, 0x80, 0x80);
            setXY4(t, x, y, x + tw, y, x, y + th, x + tw, y + th);
            setUV4(t, u, v, u + tw - 1, v, u, v + th - 1, u + tw - 1, v + th - 1);
            t->tpage = GetTPage(2, 0, sx, sy & ~(MOVE_PAGE_H - 1));
            t->clut = 0;
            addPrim(&ot[1], t);
            col += tw;
        }
    }
    DrawOTag((u_long *)&ot[1]);
    moveCount = 0;
}
