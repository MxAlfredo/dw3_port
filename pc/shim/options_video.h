#ifndef PC_OPTIONS_VIDEO_H
#define PC_OPTIONS_VIDEO_H

/*
 * The options menu's Video and Display rows (feature controls-config, O2).
 *
 * Video: fullscreen, window size, scaling (fit / integer), scanlines, texture correction and
 * vsync. Each row reads PsyCross's own switch every time it is drawn (CsWidget.read), so the
 * F5 hotkey and Alt+Enter keep working and the menu shows what they did. Display: the
 * FPS counter. The pure helpers below (window-size options, the FPS math) have no
 * SDL or window dependency, so pc/inputtest.c drives them.
 */

#define OPT_WINDOW_BASE_W 320 /* the game's display, 1x */
#define OPT_WINDOW_BASE_H 240
#define OPT_WINDOW_MAX_SCALE 8

/* The biggest whole multiple of 320x240 that fits a desktop (at least 1, at most
   OPT_WINDOW_MAX_SCALE); a desktop of 0 or less (unknown) gives 1 */
int optWindowMaxScale(int desktopW, int desktopH);
/* The scale (1..max) a window of w x h is exactly, else 0 */
int optWindowScaleOf(int w, int h, int max);

/* The FPS counter: frames shown over the milliseconds that passed, refreshed
   once at least a second has gone by */
typedef struct OptFps {
    unsigned startMs;
    int frames;
    int fps; /* the last full measure, -1 until there is one */
} OptFps;

void optFpsReset(OptFps *f, unsigned nowMs);
/* One presented frame at nowMs; returns the current reading (-1 if none yet) */
int optFpsTick(OptFps *f, unsigned nowMs);

/* ---- the boot binary only ---- */

/* Adds the Video and Display rows (after inputCfgInit, so the file's values apply) */
void optionsVideoInit(void);
/* From the draw hook, once per presented frame: the FPS to draw, -1 when the counter is off */
int optionsFpsFrame(void);

#endif
