#ifndef PC_TOUCH_SDL_H
#define PC_TOUCH_SDL_H

/*
 * The on-screen virtual controller on SDL (feature android T7a): finger events in, pad bits out, shapes drawn over
 * the finished frame. The logic is in touch_pad.h / touch_router.h; this file owns the SDL event watch, the
 * settings rows (Display tab: Touch controls, Touch opacity, Touch size) and the drawing.
 *
 * Latency: the event watch runs when SDL pushes the finger event (on Android, on the UI thread, straight from the
 * touch callback) and updates the finger table at once. The game thread merges the table into the pad word in the
 * same frame hook that pumps SDL (shim/host_pad.c, right after PsyX_UpdateInput), so a touch is never queued: it
 * reaches the game's pad read in the frame it is pumped, exactly as a real pad's buttons do.
 *
 * The virtual pad is one handler of the touch router; a layer that taps the menus themselves registers beside it
 * with touchRouter().
 */
#include "touch_router.h"

/* Installs the event watch and the overlay hook (idempotent). */
void touchInit(void);
/* Adds the three Display rows (after inputCfgInit, like inputDeviceRowsInit). Calls touchInit. */
void touchRowsInit(void);
/* The router every finger goes through. touchInit adds, in this order: the options menu, the virtual pad, the dialogue tap. */
TrRouter *touchRouter(void);

/* Once per frame, game thread, right after PsyX_UpdateInput: clears the raw active-low word's bits for every
   virtual button held. Returns the mask applied (0 when the controls are off or hidden). */
unsigned touchPadMerge(unsigned short *buttons);

/* Tapping the options menu (phase 1): the drawing code hands over where it put the tabs and rows each frame (a
   TmLayout, touch_menu.h), NULL when the menu is not drawn. */
void touchMenuLayout(const void *layout);

/* Tap a page (phase 1b): once per game frame, whether a plain page is being typed or waits (TT_*, touch_tap.h) and which; and, in the
   same frame hook as touchPadMerge, whether a tap answered it (1 once: press Confirm for one frame). */
void touchTextArm(int kind, int key);
int touchTextTake(void);

/* The harness: a tap (finger down and up) at a point of the 320x240 frame, through the real event path. */
void touchHarnessTap(int fx, int fy);

/* 1 when the controls are drawn and take touches now. */
int touchShown(void);

#endif
