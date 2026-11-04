#ifndef PC_HOST_H
#define PC_HOST_H

/*
 * The host replacements in pc/shim/ that the harnesses set up. These are the
 * calls that the game's own C does not name: like HEAP and TASK_REGISTRY,
 * they stand in for what PsyQ and the PlayStation provide.
 *
 * common.h is what defines s32/u8/... in the game's headers; it is included
 * here so that this file can be used by a unit that does not include the
 * game's engine/game.h first (pc/shim/host_shot.c).
 */
#include "common.h"

/* Opens the raw .bin disc image the host CD reader (CD_READER) reads from. */
void pcCdReaderInit(const char *image);

/* Fills in the GFX methods the file cache uses (GFX.funcs.getTime). */
void pcGfxInit(void);

/*
 * The drawing shim (shim/host_draw.c, milestone 3): the GFX and Layer methods
 * the game's own text window draws through, over a packet buffer and an
 * ordering table that build here in memory.
 */
void pcDrawInit(void);
void *pcDrawOtHead(s32 depth);
s32 pcDrawUsed(void);

/*
 * The presented frame (shim/host_draw.c, milestone 4): pcDrawBeginFrame clears
 * the ordering table with PsyCross's own ClearOTagR (the terminator DrawOTag's
 * walk needs) and restarts the packet buffer; pcDrawOtTop is the entry
 * DrawOTag is given. It is `u_long` in the game's headers, which is
 * `unsigned long` in both header sets the port uses.
 */
void pcDrawBeginFrame(void);
unsigned long *pcDrawOtTop(void);

/*
 * The battle cursor bar (shim/host_battle.c): ContinueDraw records the bar's
 * DR_MOVE prims; this draws them to the window before the frame is presented
 * and forgets them. A no-op when none are recorded.
 */
void hostBattleMovesDraw(void);

/*
 * The presented frame index (T5's DW3_VIEW_DUMP and the other *_DUMP
 * diagnostics quote it) and the "<digits>" environment parse they share
 * (implemented in shim/gfx_engine.c).
 */
s32 hostPresentFrame(void);
void hostEnvFrameParse(const char *name, s32 *read, s32 *frame);

/*
 * The screenshot the windowed harnesses take (shim/host_shot.c): glReadPixels
 * of the frame that is in the back buffer, written as a PNG. The harnesses
 * that open a PsyCross window (font_frame, boot) call it; the headless ones
 * never do.
 */
void pcSaveShot(const char *path, int width, int height);

/*
 * The presented frame of the game's own boot path (shim/gfx_engine.c, T15):
 * how many frames the harness presents before it saves the screenshot and
 * exits, and where. `frames` 0 means "never exit on its own" (nothing is
 * saved). pcBootPresented is how many have been presented so far.
 */
void pcBootPresentInit(int frames, const char *shot);
int pcBootPresented(void);

/* O7: picks the vblank rate before the game's main() reads NTSC_MODE: 60 Hz
   always, 50 only with the internal debug env DW3_REFRESH=50 (gfx_engine.c). */
void pcRefreshInit(void);

/*
 * T30: multi-frame capture. Optional companion to pcBootPresentInit: when
 * armed with a list of zero-based frame indices, the host writes one PNG per
 * requested frame (under `<stem>_f<index>.png`, where `stem` is the screenshot
 * path passed to pcBootPresentInit with its last extension removed) from a
 * single run, before the frame advances. Indices must be < the `frames`
 * budget set with pcBootPresentInit and must not duplicate: the boot CLI
 * validates the list up front and exits 2 before the run starts, so a
 * silently-dropped capture cannot happen. `count` 0 leaves the host on the
 * single-shot path. With the list armed the existing single-shot capture at
 * the run's last frame still happens (pcBootPresentInit's `shot` is
 * unchanged).
 */
#define PC_BOOT_MAX_CAPTURES 8
void pcBootMultiCaptureInit(const int *indices, int count);

/*
 * T19: stop the run a number of frames after the game's own code enters a
 * mode, next to the frame budget. The field demo uses it so its length is
 * "a few frames of the field", not a magic total frame count (the field's own
 * loader reaches its first requirement a few frames in; see pc/README.md).
 * 0 frames leaves it off.
 */
void pcBootStopInMode(s32 mode, s32 frames);

/*
 * The country-select art diagnostic (T16, shim/gfx_engine.c): boot arms it with
 * the VRAM regions the mode's TIM archive fills (from country_select.c's
 * setImagePos/setClutPos) and the frame to read them on; gfx_engine reads each
 * one back with PsyCross's GR_ReadVRAM on that frame, prints it with a checksum,
 * and prints the frame's sprites (their UV, size, CLUT and texture page) so the
 * check can tell an upload apart from a drawing-coordinate problem.
 */
typedef struct PcArtRegion {
    int x;
    int y;
    int w; /* halfwords */
    int h; /* rows */
} PcArtRegion;

void pcBootArtInit(int frame, const PcArtRegion *regions, int count);

/*
 * The game's own input path (T17, shim/host_pad.c). PsyCross's libpad needs two
 * things the port supplies: PadChkVsync (its own libpad answers 0, so the
 * game's updatePad never reads a button -- the boot link wraps the symbol) and
 * a per-frame PsyX_UpdateInput (which polls SDL and fills the pad buffers the
 * game reads). pcBootPadInit arms an optional scripted key sequence -- the
 * automated demo types it with XTest, real key events at PsyCross's window --
 * and is NULL for the plain interactive run; pcBootPadRootTask hands the shim
 * the mode task so the demo can read the country select's highlighted option
 * from its own menu task; pcBootPadFrame runs once per frame from
 * gfx_engine.c's frame hook, prints what the game's own pad code saw, types
 * the script's next key and updates the pad input.
 */
void pcBootPadInit(const char *script);
void pcBootPadRootTask(void *task);
void pcBootPadFrame(int frame);

/*
 * The shim heap's live-block surface (shim/heap.c): the game's own heap is a
 * linked list of MemBlock between fixed PlayStation addresses, which the
 * host does not build, so probes walk the shim's own list instead. Tags are
 * the game's MEM_* numbers unchanged.
 */
void *pcHeapFirstBlock(void);
void *pcHeapNextBlock(void *block);
s32 pcHeapBlockTag(void *block);
void *pcHeapBlockData(void *block);

#endif /* PC_HOST_H */
