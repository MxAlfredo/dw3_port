/*
 * Host shim for the graphics state (src/engine/gfx/display.c, GfxState GFX).
 *
 * The game's display.c drives the GPU through libgpu, Gs and vsync interrupts,
 * none of which the host has; so the whole table is left NULL except the one
 * method the file layer reaches: GFX.funcs.getTime, the 8.8 frame clock the
 * file cache stamps its slots with (isFileLoading, requestFile, updateFileCache,
 * findOldestFile, touchMarkedFiles). The harness is not a frame loop, so the
 * clock stands still at 0: enough for the cache to order its slots.
 *
 * (This is not the game's GFX. Only its two methods are needed here.)
 */
#include "engine/gfx.h"
#include "host.h"

static s32 hostGetTime(void) {
    return 0;
}

/* Zero everywhere but the clock the file cache reads */
GfxState GFX;

void pcGfxInit(void) {
    GFX.funcs.getTime = hostGetTime;
}
