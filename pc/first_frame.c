/*
 * Host harness (milestone 4, T13): the first frame on screen.
 *
 *     first_frame [frames] [screenshot]
 *
 * Opens a PsyCross window (PsyX_Initialise), builds the game's own area name
 * banner task (FIELDSTG_updateBanner, src/field/field_mode/banner.c) over the
 * real FIELDSTG_bannerBoxes table (data/fieldstg.c) and presents the frames
 * with PsyX_BeginScene / DrawOTag / PsyX_EndScene. The banner is the port's
 * first drawable: ten flat coloured quads (POLY_F4), no texture and no VRAM,
 * the same banner the game shows when a field is entered.
 *
 * The banner's windows (the area and place names) need the disc and a font in
 * VRAM, which is out of this milestone's scope, so the harness makes
 * GAME.funcs.getMode report no field mode: FIELDSTG_showAreaName finds no area,
 * creates no window and touches no disc, and the two TextWindows the harness
 * hands the banner stay valid and never finish, so the banner runs its whole
 * stretch sequence and then stays on screen.
 *
 * The output is the measurement: per frame, the state machine's state and the
 * quads the game's own drawing code wrote into the ordering table, with their
 * colours and rectangles. The banner is the content; PsyCross only presents.
 */
#include "engine/file.h"
#include "engine/game_state.h"
#include "engine/gfx.h"
#include "field/field_mode.h"
#include "shim/host.h"

#include <PsyX/PsyX_public.h>

#include <GL/gl.h>

#include <stdio.h>

/*
 * The game's own engine/game.h (reached through field/field_mode.h) declares
 * atoi, strlen and friends with the types PsyQ's libraries use, which clash
 * with the host's <stdlib.h>/<string.h>; so this harness does not include
 * <stdlib.h> and takes the one name it needs from it by hand.
 */
extern void exit(int status);

/* The frame count of the command line, without <stdlib.h> */
static s32 parseFrames(const char *text) {
    s32 value = 0;

    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    return value;
}

/* The banner's style, FONT_STYLES[1], and its two window positions, exactly
   what FIELDSTG_showAreaName passes (src/field/field_mode/banner.c). */
#define BANNER_STYLE 1
#define BANNER_AREA_X 0x80
#define BANNER_AREA_Y 0x1A
#define BANNER_PLACE_X 0x28
#define BANNER_PLACE_Y 0x44

/* The quad code the banner's own FIELDSTG_drawBannerBox sets */
#define CODE_POLY_F4 0x28

/* A second per frame is plenty for the owner to see it; the run exits alone */
#define DEFAULT_FRAMES 300

/*
 * GAME.funcs.getMode: no field mode. FIELDSTG_showAreaName then finds no area
 * in FIELDSTG_areaNames and creates neither window (its areas need the disc
 * and a font in VRAM). The harness's own windows are what the banner's
 * TASK_RUN waits on, and they never finish.
 */
static s32 noFieldMode(void) {
    return -1;
}

/*
 * The four FIELDSTG functions that FIELDSTG_state's table names. The harness
 * links data/fieldstg.c with --gc-sections only for FIELDSTG_bannerBoxes and
 * FIELDSTG_areaNames, which drops every game symbol it does not reference;
 * these four stay because the state's four function pointers point at them.
 * The banner only ever writes FIELDSTG_state.frozen, so they never run: if one
 * does, this says so instead of inventing an answer.
 */
static void bannerDataNotCalled(const char *name) {
    fprintf(stderr, "first_frame: FIELDSTG.%s is not part of this harness\n", name);
    exit(1);
}

/* the banner asks the port whether the mode is a stage of a mod; here no mode is */
s32 pcFieldBannerEntries(s32 mode, s32 *area, s32 *place) {
    (void)mode;
    (void)area;
    (void)place;
    return 0;
}

void FIELDSTG_pickStage(void) {
    bannerDataNotCalled("pickStage");
}

s32 FIELDSTG_getFileEntry(s32 index) {
    (void)index;
    bannerDataNotCalled("getFileEntry");
    return 0;
}

s32 FIELDSTG_getActorWidth(s32 index) {
    (void)index;
    bannerDataNotCalled("getActorWidth");
    return 0;
}

FieldBattles *FIELDSTG_findBattles(FieldBattles *list, s32 id) {
    (void)list;
    (void)id;
    bannerDataNotCalled("findBattles");
    return NULL;
}

/*
 * Walks the ordering table's chain (the one ClearOTagR linked and the banner
 * added its quads to) and prints every quad: its colour and the four corners
 * the game's own FIELDSTG_drawBannerBox wrote. Returns how many quads it saw.
 */
static s32 printFrameQuads(u_long *top) {
    P_TAG *node;
    s32 count;
    s32 guard;

    count = 0;
    node = (P_TAG *)top;
    for (guard = 0; guard < 128; guard++) {
        if (node->len > 0) {
            if (node->code == CODE_POLY_F4) {
                POLY_F4 *quad = (POLY_F4 *)node;

                printf(" %02X%02X%02X(%d,%d %dx%d)",
                       quad->r0, quad->g0, quad->b0,
                       (int)quad->x0, (int)quad->y0,
                       (int)(quad->x1 - quad->x0), (int)(quad->y3 - quad->y0));
                count++;
            }
        }
        if (isendprim(node)) {
            break;
        }
        node = (P_TAG *)node->addr;
    }
    return count;
}

/*
 * Saves what is on screen: glReadPixels of the presented frame, written as a
 * binary PPM (no library of ours, and no tool the owner may not have). The
 * window is the PSX framebuffer PsyCross scaled to it, so what comes back is
 * the frame the banner drew. Only when the harness is asked for it: it writes
 * the file in the working directory.
 */
static void saveFrame(const char *path, s32 width, s32 height) {
    u8 *rgba;
    FILE *file;
    s32 x;
    s32 y;

    rgba = HEAP.allocZeroed(width * height * 4, MEM_MODE);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "first_frame: cannot write %s\n", path);
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", (int)width, (int)height);
    /* GL reads bottom-up; the PPM is top-down */
    for (y = height - 1; y >= 0; y--) {
        for (x = 0; x < width; x++) {
            u8 *pixel = rgba + (y * width + x) * 4;

            fwrite(pixel, 1, 3, file);
        }
    }
    fclose(file);
}

int main(int argc, char **argv) {
    s32 frames = DEFAULT_FRAMES;
    s32 frame;
    s32 screenshot = 0;
    s32 totalFrames = 0;
    s32 totalQuads = 0;
    AreaBanner *banner;
    AreaNameWindows windows;
    DISPENV dispEnv;
    DRAWENV drawEnv;
    int width = 0;
    int height = 0;

    if (argc > 3) {
        fprintf(stderr, "usage: %s [frames] [screenshot]\n", argv[0]);
        return 2;
    }
    if (argc > 1) {
        frames = parseFrames(argv[1]);
        if (frames <= 0) {
            frames = DEFAULT_FRAMES;
        }
    }
    if (argc == 3) {
        screenshot = 1;
    }

    pcDrawInit();

    /* The banner's own C reads GAME.funcs.getMode in FIELDSTG_showAreaName */
    GAME.funcs.getMode = noFieldMode;

    /* The banner over the two windows the real game gives it (style 1, the
       banner's own positions); they are never typed into, so isFinished stays
       false and the banner's TASK_RUN stays in its last substate. */
    windows.area = createTextWindow(FIELD_LAYER_BANNER, BANNER_STYLE, BANNER_AREA_X, BANNER_AREA_Y);
    windows.place = createTextWindow(FIELD_LAYER_BANNER, BANNER_STYLE, BANNER_PLACE_X, BANNER_PLACE_Y);

    /* The game's own task constructor and update, the same call
       FIELDSTG_createBanner makes (without the registry id: this harness does
       not run the task registry). key1 != 0 copies FIELDSTG_bannerBoxes. */
    banner = createTask(FIELDSTG_updateBanner, sizeof(AreaBanner), 0);
    banner->key1 = 1;

    printf("first_frame: %d frames, no disc and no VRAM needed\n", (int)frames);

    PsyX_Initialise("dw3_pc_spike", SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    PsyX_GetScreenSize(&width, &height);
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "first_frame: PsyX_Initialise did not open a window "
                        "(no display or no OpenGL context)\n");
        return 1;
    }
    printf("window %dx%d, PsyCross presents %dx%d\n", width, height, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* The drawing environment, as a mode sets it up on the PlayStation: the
       whole screen, cleared to black each frame. */
    SetDefDrawEnv(&drawEnv, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    drawEnv.isbg = 1;
    PutDrawEnv(&drawEnv);
    SetDefDispEnv(&dispEnv, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    PutDispEnv(&dispEnv);

    for (frame = 0; frame < frames; frame++) {
        s32 quads;

        pcDrawBeginFrame();

        /* The game's own banner: FIELDSTG_updateBanner stretches its visible
           boxes (FIELDSTG_stretchBannerBox) and draws each as a POLY_F4
           (FIELDSTG_drawBannerBox) into the layer's ordering table. */
        FIELDSTG_updateBanner(banner, &windows);

        printf("frame %03d: state=%d substate=%d bytes=%d quads:",
               (int)frame, (int)banner->state, (int)banner->substate,
               (int)pcDrawUsed());
        quads = printFrameQuads(pcDrawOtTop());
        printf("\n");

        totalQuads += quads;
        totalFrames++;

        PsyX_BeginScene();
        DrawOTag(pcDrawOtTop());
        /* The frame is in the back buffer now; read it before the swap, which
           is what the window will show. */
        if (screenshot && frame == frames - 1) {
            saveFrame("first_frame.ppm", width, height);
        }
        PsyX_EndScene();
        PsyX_WaitForTimestep(1);
    }

    printf("presented %d frames, %d quads drawn by the game's own banner code\n",
           (int)totalFrames, (int)totalQuads);
    printf("last frame: state=%d substate=%d\n", (int)banner->state, (int)banner->substate);
    if (totalQuads == 0) {
        fprintf(stderr, "first_frame: the banner drew no quad in %d frames\n", (int)totalFrames);
        return 1;
    }
    if (screenshot) {
        printf("screenshot: first_frame.ppm\n");
    }

    PsyX_Shutdown();
    return 0;
}
