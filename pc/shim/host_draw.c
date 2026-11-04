/*
 * Host drawing shim: the pieces the game's own drawing code reaches through
 * GFX and a Layer, backed by a plain ordering table and packet buffer.
 *
 * This is the milestone 3 counterpart of host_gfx.c (which fills in the
 * clock the file cache uses). The game's text window (text_window.c) draws
 * through GFX.funcs.getLayer, Layer.getOtEntry, GFX.funcs.getPrim and
 * GFX.funcs.setPrim, and builds the ordering table with PsyCross's addPrim.
 * On the host there is no GPU, so this file lays those pieces out in memory:
 * a single Layer, a small ordering table and a bump-allocated packet buffer.
 * Nothing here displays: getPrim hands out bytes, setPrim takes them back,
 * and getOtEntry returns the table entry for that depth.
 *
 * It is compiled against PsyCross's headers (like text_window.o in the same
 * harness), because the game's Layer layout depends on them: DRAWENV sits in
 * PsyCross's #pragma pack(1) region and is 101 bytes there against the retail
 * header's 160, so a Layer built with the retail headers would put getOtEntry
 * at the wrong offset. GfxState, by contrast, is the same size (608) and
 * GfxFuncs the same offsets under both header sets, so the GFX the shims
 * share stays consistent with the retail-compiled units (file_cache.c).
 *
 * The ordering table starts all-zero. addPrim stores the previous head in the
 * new primitive and the primitive in the entry, so an entry of 0 simply ends
 * the chain: the milestone 3 harness walks it while the stored address is
 * non-NULL. No PsyCross symbol is needed for that, which keeps this file (and
 * the targets that link it) out of the library's PGXP/GPU objects.
 *
 * Milestone 4 (T13) does present, so it needs the other ending: pcDrawBeginFrame
 * clears the table with PsyCross's own ClearOTagR, which links the entries
 * (entry i -> i-1, entry 0 the library's prim_terminator) and is what
 * DrawOTag's walk expects. That is why the table is an OT_TAG array (the
 * library's 12-byte entry), not an array of u_long: the two agree on where the
 * address goes (offset 0), and ClearOTagR/DrawOTag only speak OT_TAG. The
 * table is passed to DrawOTag by pcDrawOtTop, the entry its walk starts from.
 *
 * (This is not the game's GFX: only the methods this harness reaches are set.)
 */
#include "engine/game.h"
#include "host.h"

#define OT_LENGTH 64      /* the depths getOtEntry may be asked for */
#define PRIM_BYTES 65536  /* the packet buffer the primitives are written to */

/* The one Layer the harness draws on, and the memory its methods hand out */
static Layer layer;
static OT_TAG ot[OT_LENGTH];
static u8 primBuffer[PRIM_BYTES];
static u8 *primNext;

/* GFX.funcs.getLayer: the layer for an id. FindLayer in the game looks one up
   by id; here there is only the harness's layer, so every id gets it. */
struct Layer *pcDrawGetLayer(s32 id) {
    (void)id;
    return &layer;
}

/* Layer.getOtEntry: the ordering table entry for a depth. */
u_long *pcDrawGetOtEntry(struct Layer *layer_, s32 depth) {
    (void)layer_;
    if (depth < 0) {
        depth = 0;
    }
    if (depth >= OT_LENGTH) {
        depth = OT_LENGTH - 1;
    }
    return (u_long *)&ot[depth];
}

/* Layer.setClipPos / setClipSize: the banner's TASK_DONE shrinks the layer's
   clip as it closes. The host has one layer and no clip hardware, so these
   are no-ops; the harness's banner never reaches TASK_DONE (its windows never
   finish), and this only keeps that path from calling NULL if it did. */
static void pcDrawSetClipPos(void) {
}

static void pcDrawSetClipSize(void) {
}

/* GFX.funcs.getFrameTime: the vsyncs of the last frame, in the game's 8.8
   time. The harness presents a frame's worth each time, so one vsync. */
static s32 pcDrawGetFrameTime(void) {
    return 1;
}

/* GFX.funcs.getPrim: the next free byte of the packet buffer. */
void *pcDrawGetPrim(void) {
    return primNext;
}

/* GFX.funcs.setPrim: where the next frame starts. The harness draws once, so
   this only records how much of the buffer the draw order used. */
void pcDrawSetPrim(void *next) {
    primNext = next;
}

/* The primitives this harness has built: the entry for a depth, or NULL when
   it is empty (the ordering table starts zeroed and never gets a terminator
   symbol, so an entry of 0 ends the chain -- see the file comment). */
void *pcDrawOtHead(s32 depth) {
    if (depth < 0) {
        depth = 0;
    }
    if (depth >= OT_LENGTH) {
        depth = OT_LENGTH - 1;
    }
    return (void *)((P_TAG *)&ot[depth])->addr;
}

/* How many bytes of the packet buffer the primitives occupy. */
s32 pcDrawUsed(void) {
    return (s32)(primNext - primBuffer);
}

/*
 * Milestone 4 (T13): the entry DrawOTag's walk starts from, the last of the
 * ordering table (ClearOTagR links it down to entry 0, the library's
 * prim_terminator).
 */
u_long *pcDrawOtTop(void) {
    return (u_long *)&ot[OT_LENGTH - 1];
}

/*
 * Milestone 4 (T13): the start of a presented frame. ClearOTagR links the
 * ordering table with PsyCross's own terminator (what DrawOTag's walk needs
 * from the milestone 3 NULL-ended chain) and the packet buffer restarts.
 */
void pcDrawBeginFrame(void) {
    ClearOTagR((u_long *)ot, OT_LENGTH);
    primNext = primBuffer;
}

/*
 * Sets up the drawing shim: the clock (pcGfxInit), the three GFX methods the
 * text window calls, and the Layer's method. Zeroes the ordering table so its
 * entries end their chains and resets the packet buffer.
 */
void pcDrawInit(void) {
    s32 i;

    pcGfxInit();
    GFX.funcs.getLayer = pcDrawGetLayer;
    GFX.funcs.getPrim = pcDrawGetPrim;
    GFX.funcs.setPrim = pcDrawSetPrim;
    GFX.funcs.getFrameTime = pcDrawGetFrameTime;

    layer.getOtEntry = pcDrawGetOtEntry;
    layer.setClipPos = pcDrawSetClipPos;
    layer.setClipSize = pcDrawSetClipSize;
    for (i = 0; i < OT_LENGTH; i++) {
        setaddr(&ot[i], 0);
        setlen(&ot[i], 0);
    }
    primNext = primBuffer;
}

/*
 * The game globals the text window's other methods touch (updateTextWindow
 * plays SOUND.playSound and reads PAD; the name-insertion control codes read
 * GAME.name). This harness draws directly through textWindowDraw, which
 * reaches none of them, but the unit names them, so the link needs them
 * defined. Zeroed: they are never read on this path.
 */
PadState PAD;
SoundState SOUND;
GameState GAME;
