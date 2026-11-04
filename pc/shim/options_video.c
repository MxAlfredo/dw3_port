/*
 * The options menu's Video and Display rows (feature controls-config, O2); see
 * options_video.h. PsyCross keeps the window (g_window, g_windowWidth/Height) and the
 * render switches (g_cfg_*) in plain globals: the rows read and write them directly.
 * The same code as PsyCross's Alt+Enter handler does the fullscreen switch.
 */
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "auto_text.h"
#include "battle_speed.h"
#include "controls_screen.h"
#include "fast_transitions.h"
#include "options_system.h"
#include "options_video.h"
#include "stage_banner.h"

/* PsyCross internals (PsyX_main.cpp / PsyX_render.cpp), plain C-linkage globals */
extern SDL_Window *g_window;
extern int g_windowWidth;
extern int g_windowHeight;
extern int g_cfg_swapInterval;           /* PsyX_public.h, not included: it needs the PsyQ types */
extern int g_cfg_bilinearFiltering;      /* forced off: it bled VRAM neighbours (seams, smeared text) */
extern int g_cfg_integerScale;
extern int g_cfg_scanlines;
extern int g_cfg_pgxpTextureCorrection;

int optWindowMaxScale(int desktopW, int desktopH) {
    int n;

    if (desktopW <= 0 || desktopH <= 0) {
        return 1;
    }
    n = desktopW / OPT_WINDOW_BASE_W;
    if (desktopH / OPT_WINDOW_BASE_H < n) {
        n = desktopH / OPT_WINDOW_BASE_H;
    }
    return n < 1 ? 1 : (n > OPT_WINDOW_MAX_SCALE ? OPT_WINDOW_MAX_SCALE : n);
}

int optWindowScaleOf(int w, int h, int max) {
    int n;

    if (w <= 0 || w % OPT_WINDOW_BASE_W != 0) {
        return 0;
    }
    n = w / OPT_WINDOW_BASE_W;
    return (n >= 1 && n <= max && h == n * OPT_WINDOW_BASE_H) ? n : 0;
}

void optFpsReset(OptFps *f, unsigned nowMs) {
    f->startMs = nowMs;
    f->frames = 0;
    f->fps = -1;
}

int optFpsTick(OptFps *f, unsigned nowMs) {
    unsigned elapsed = nowMs - f->startMs;

    f->frames++;
    if (elapsed >= 1000u) {
        f->fps = (int)(((unsigned long)f->frames * 1000UL + elapsed / 2) / elapsed);
        f->startMs = nowMs;
        f->frames = 0;
    }
    return f->fps;
}

/* ---- the rows ---- */

static int showFps;
static OptFps fpsCounter;
static int windowScale = 1;   /* the size last chosen (kept while fullscreen hides it) */
static int windowMax = 1;
static int booted;            /* the rows were added: later changes are the player's */
static char sizeNames[OPT_WINDOW_MAX_SCALE][4];
static const char *sizeNamePtr[OPT_WINDOW_MAX_SCALE];

static int isFullscreen(void) {
    return g_window != NULL && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

static void syncWindowSize(void) {
    if (g_window != NULL) {
        SDL_GetWindowSize(g_window, &g_windowWidth, &g_windowHeight);
    }
}

static void applyWindowSize(int idx) {
    windowScale = idx + 1;
    /* DW3_WINDOW names the boot size; the file does not override it (the harness) */
    if (g_window == NULL || isFullscreen() || (!booted && getenv("DW3_WINDOW") != NULL)) {
        return;
    }
    SDL_SetWindowSize(g_window, OPT_WINDOW_BASE_W * windowScale, OPT_WINDOW_BASE_H * windowScale);
    syncWindowSize();
}

static int readWindowSize(void) {
    int n = isFullscreen() ? 0 : optWindowScaleOf(g_windowWidth, g_windowHeight, windowMax);

    return n > 0 ? n - 1 : -1; /* a window dragged to another size keeps the chosen row */
}

static void applyFullscreen(int on) {
    if (g_window == NULL || on == isFullscreen()) {
        return;
    }
    if (SDL_SetWindowFullscreen(g_window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        fprintf(stderr, "settings: fullscreen: %s\n", SDL_GetError());
        return;
    }
    if (!on) {
        SDL_SetWindowSize(g_window, OPT_WINDOW_BASE_W * windowScale, OPT_WINDOW_BASE_H * windowScale);
    }
    syncWindowSize();
}

static int readFullscreen(void) {
    return g_window != NULL ? isFullscreen() : -1;
}

/* Row values: 0 Fit, 1 Integer, 2 Aspect; PsyCross' g_cfg_integerScale holds the same number (PsyX_CalcScreenRect's mode) */
static void applyScaling(int mode) {
    g_cfg_integerScale = mode;
}

static int readScaling(void) {
    return g_cfg_integerScale;
}

static void applyScanlines(int on) {
    g_cfg_scanlines = on;
}

static int readScanlines(void) {
    return g_cfg_scanlines != 0;
}

static void applyTexCorrection(int on) {
    g_cfg_pgxpTextureCorrection = on;
}

static int readTexCorrection(void) {
    return g_cfg_pgxpTextureCorrection != 0;
}

static void applyVsync(int on) {
    g_cfg_swapInterval = on;
}

static int readVsync(void) {
    return g_cfg_swapInterval != 0;
}

static void applyShowFps(int on) {
    showFps = on;
    optFpsReset(&fpsCounter, SDL_GetTicks());
}

static void applyAutoText(int speed) {
    autoTextSetSpeed(speed);
}

static void applyTextSpeed(int on) {
    textSpeedSetMode(on);
}

static void applyBattleSpeed(int mode) {
    battleSpeedSetMode(mode);
}

static void applyStageBanner(int mode) {
    stageBannerSetMode(mode);
}

void optionsVideoInit(void) {
    static const char *const autoTextNames[] = {"Off", "Slow", "Normal", "Fast"};
    static const char *const battleSpeedNames[] = {"Normal", "x1.5", "x2"};
    static const char *const stageBannerNames[] = {"Fast", "Classic"};
    static const char *const scalingNames[] = {"Fit", "Integer", "Aspect"};
    static const char *const scalingTokens[] = {"fit", "integer", "aspect"};
    SDL_Rect desk;
    CsWidget w;
    int i;

    g_cfg_bilinearFiltering = 0; /* the Filtering row and F3 are gone: always sharp */
    windowMax = 1;
    if (SDL_GetDisplayUsableBounds(0, &desk) == 0) {
        windowMax = optWindowMaxScale(desk.w, desk.h);
    }
    for (i = 0; i < windowMax; i++) {
        snprintf(sizeNames[i], sizeof(sizeNames[i]), "%dx", i + 1);
        sizeNamePtr[i] = sizeNames[i];
    }
    i = optWindowScaleOf(g_windowWidth, g_windowHeight, windowMax);
    windowScale = i > 0 ? i : 1;

    w = (CsWidget){.kind = CS_ROW_TOGGLE, .label = "Fullscreen", .key = "fullscreen", .apply = applyFullscreen,
                   .read = readFullscreen, .desc = "Borderless fullscreen. Alt Enter too.",
                   .comment = "Borderless fullscreen at the desktop size."};
    csWidgetAdd(CS_TAB_VIDEO, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Window size", .key = "window_size", .names = sizeNamePtr,
                   .count = windowMax, .apply = applyWindowSize, .read = readWindowSize,
                   .desc = "Times 320 by 240. Not used in fullscreen.",
                   .comment = "Window size as a multiple of 320x240, up to what the desktop fits. Ignored in fullscreen."};
    csWidgetAdd(CS_TAB_VIDEO, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Scaling", .key = "scaling", .names = scalingNames,
                   .tokens = scalingTokens, .count = 3, .apply = applyScaling, .read = readScaling,
#ifdef __ANDROID__
                   .value = 2, /* phone screens are 20:9: a stretched 4:3 picture looks wrong, so a fresh install keeps the aspect */
#endif
                   .desc = "Fit fills the window. Integer: whole pixels. Aspect: biggest 4:3.",
                   .comment = "Screen scaling: fit (stretch to the window), integer (biggest whole multiple of 320x240, black borders) or aspect (biggest picture that keeps the 4:3 shape, black bars)."};
    csWidgetAdd(CS_TAB_VIDEO, &w);
    w = (CsWidget){.kind = CS_ROW_TOGGLE, .label = "Scanlines", .key = "scanlines", .apply = applyScanlines,
                   .read = readScanlines, .desc = "Darkens every other line, like a CRT.",
                   .comment = "Darken every other line of the 240-line game image (CRT look). Off by default."};
    csWidgetAdd(CS_TAB_VIDEO, &w);
    w = (CsWidget){.kind = CS_ROW_TOGGLE, .label = "Texture correction", .key = "texture_correction",
                   .apply = applyTexCorrection, .read = readTexCorrection, .value = 1,
                   .desc = "Straighter 3D textures. Same as F5.",
                   .comment = "Perspective-correct textures on 3D models (PGXP). F5 toggles it."};
    csWidgetAdd(CS_TAB_VIDEO, &w);
    w = (CsWidget){.kind = CS_ROW_TOGGLE, .label = "VSync", .key = "vsync", .apply = applyVsync,
                   .read = readVsync, .desc = "Waits for the screen refresh.",
                   .comment = "Wait for the screen refresh when showing a frame (less tearing)."};
    csWidgetAdd(CS_TAB_VIDEO, &w);

    w = (CsWidget){.kind = CS_ROW_TOGGLE, .label = "Show FPS", .key = "show_fps", .apply = applyShowFps,
                   .desc = "FPS counter in the top right corner.",
                   .comment = "Draw a frames per second counter in the top right corner."};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Auto text", .key = "auto_text", .names = autoTextNames,
                   .count = AUTO_TEXT_SPEEDS, .apply = applyAutoText,
                   .desc = "Dialogue moves on by itself. A button still skips.",
                   .comment = "Dialogue pages move on by themselves after a wait that grows with the text: off, slow, normal or fast. Questions and menus always wait for you."};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    w = (CsWidget){.kind = CS_ROW_TOGGLE, .label = "Text speed", .key = "text_speed", .apply = applyTextSpeed,
                   .desc = "Dialogue types fast. Off is the original pace.",
                   .comment = "Dialogue typing speed: off (as the original game) or on (one character per frame, from the first page). Waits and questions work the same."};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Battle speed", .key = "battle_speed", .names = battleSpeedNames,
                   .count = BATTLE_SPEED_MODES, .apply = applyBattleSpeed,
                   .desc = "Faster attacks and techniques in battle.",
                   .comment = "Battle animation speed while attacks and techniques play: normal, x1.5 or x2. The command menu and the music keep their pace.",
                   .section = "display"}; /* the Enhancements tab, the file section it always had */
    csWidgetAdd(CS_TAB_ENHANCEMENTS, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Stage banner", .key = "stage_banner", .names = stageBannerNames,
                   .count = STAGE_BANNER_MODES, .apply = applyStageBanner,
                   .desc = "Fast: move at once. Classic: wait for the name.",
                   .comment = "Area name when you enter a stage: fast (you can move as soon as the stage loads, the name shows over it and goes away by itself) or classic (the original: black screen and no control until the name ends).",
                   .section = "display"};
    csWidgetAdd(CS_TAB_ENHANCEMENTS, &w);
    fastTransitionsRows(); /* Door transition, Battle transition, Battle results */
    {
        /* L4b: the widest talk box, a Display row (owner, 2026-10-29). Read before the game starts
           (text_data.c takes it from the file when it builds the es-419 text, so the wrap and the box
           agree), so a change needs a restart. */
        static const char *const widthNames[] = {"original", "160", "180", "200", "220", "250"};
        CsWidget tb = {.kind = CS_ROW_CYCLE, .key = "talk_box_width", .label = "Dialogue box width",
                       .desc = "Wider box, fewer pages. Needs a restart.",
                       .names = widthNames, .count = 6, .value = 2,
                       .comment = "Widest dialogue box in pixels: original (139, as the game) or 160, 180 (default), 200, 220, 250. Longer lines fit; takes effect on the next start."};
        csWidgetAdd(CS_TAB_DISPLAY, &tb);
    }
    optionsSystemInit(); /* the System tab: Restart game, Quit game */
    booted = 1;
}

int optionsFpsFrame(void) {
    int fps;

    if (!showFps) {
        return -1;
    }
    fps = optFpsTick(&fpsCounter, SDL_GetTicks());
    return fps < 0 ? 0 : fps;
}
