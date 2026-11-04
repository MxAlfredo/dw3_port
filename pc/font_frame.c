/*
 * Host harness (milestone 5, T14): the game's font in VRAM and its own text on
 * screen.
 *
 *     font_frame <disc.bin> [file index] [string index] [frames] [screenshot.png]
 *
 * Three of the game's own units run here, unmodified:
 *
 *   * the TIM loader (src/engine/gfx/tim_loader.c): FONT.load() -- loadFont in
 *     src/engine/text/font.c -- builds a TimLoader, asks the file cache for
 *     FILE_FONT (0x287 on the European disc) and uploads the TIMs it holds
 *     through libgpu's LoadImage/DrawSync. That is the font's sheet landing in
 *     VRAM;
 *   * the file layer (milestone 1): the font and the language file come from
 *     the owner's own disc, read by the game's own cache;
 *   * the text window (milestone 3, src/engine/text/text_window.c): a window in
 *     style 1 (the area banner's, FONT_STYLES[1]) is given a string of the
 *     language file and draws it with textWindowDraw, one SPRT and its
 *     DR_TPAGE per glyph, with the glyph table's real UV and CLUT.
 *
 * The output is the measurement: the VRAM region the font took (where and how
 * big, with a checksum of what landed there) and every glyph primitive (its
 * sprite's UV, size, CLUT and texture page). pc/check_font.py compares both
 * against the TIM data the disc holds and the glyph table the C carries.
 *
 * The window is glReadPixels'd into a PNG that this file writes itself: no
 * library, the zlib stream uses stored (uncompressed) deflate blocks. With
 * direct rendering xwd/x11grab capture the window black (see T13), so a GL
 * read is the only reliable capture here.
 *
 * The disc is the owner's own file: it is only read, and nothing of it is
 * written into the repository (ADR-001).
 */
#include "engine/file.h"
#include "engine/heap.h"
#include "engine/text.h"
#include "engine/gfx.h"
#include "shim/host.h"

#include <PsyX/PsyX_public.h>
#include <PsyX/PsyX_render.h>

#include <GL/gl.h>

#include <stdio.h>

/*
 * The game's own engine/game.h declares atoi/strlen with the types PsyQ's
 * libraries use, which clash with the host's <stdlib.h>/<string.h>; so this
 * harness takes the one name it needs from stdlib by hand (as first_frame.c
 * does), and the PNG writer below uses neither.
 */
extern void exit(int status);

/* The banner's style and its two window positions, as FIELDSTG_showAreaName
   passes them (src/field/field_mode/banner.c). */
#define BANNER_STYLE 1
#define BANNER_PLACE_X 0x28
#define BANNER_PLACE_Y 0x44

/* The field layer the banner draws its text windows on (field/field_map.h;
   the host shim has one layer and hands it to every id). */
#define BANNER_LAYER 0x1003

/* The defaults: the European disc numbers the language files once per language
   in a row and TEXT_FILE() picks the copy; this is the Spanish SSSTNAME (the
   area names), string 71, one of the ones the G-041 notes measure. */
#define DEFAULT_FILE 189
#define DEFAULT_STRING 71

/* A handful of seconds is plenty for the owner to see it; the run exits alone */
#define DEFAULT_FRAMES 300

/* Where the font's TIMs go: loadFont asks for (0x140, 0) and the loader moves
   each following image 0x40 halfwords right (timLoaderLoadArchive). */
#define FONT_VRAM_X 0x140
#define FONT_VRAM_Y 0
#define FONT_IMAGE_W 0x40   /* halfwords, from the TIM block */
#define FONT_IMAGE_H 0x100  /* rows */
#define FONT_IMAGES 2
#define FONT_IMAGE_BYTES (FONT_IMAGE_W * FONT_IMAGE_H * 2)
#define FONT_VRAM_BYTES (FONT_IMAGE_BYTES * FONT_IMAGES)

/* What a glyph draws as: textWindowDraw's own setcode values */
#define CODE_SPRT 0x64
#define CODE_DR_TPAGE 0xE1

/* The frame count and the other numbers of the command line, without <stdlib.h> */
static s32 parseNumber(const char *text) {
    s32 value = 0;

    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    return value;
}

/*
 * The texture page a DR_TPAGE sets, as PsyCross's _get_mode packs it
 * (e1 <dtd> <dfe> <tpage>, at the primitive's code[0]): out of
 * `(0xe1000000)|0x200|(tpage & 0x9ff)`, the page's bit 9 is the word's bit 11.
 */
static s32 drTpage(u_long code) {
    return (s32)((code & 0x1FFu) | ((code >> 2) & 0x200u));
}

/* FNV-1a, to say in one number whether what landed in VRAM is the TIM */
static u32 checksum(const u8 *data, s32 size) {
    u32 hash = 0x811C9DC5u;
    s32 i;

    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 0x01000193u;
    }
    return hash;
}

/*
 * The PNG writer is the shim's (pc/shim/host_shot.c, pcSaveShot): the same
 * capture the boot harness (pc/boot.c) takes -- glReadPixels of the frame in
 * the back buffer, before PsyX_EndScene swaps it, written as an 8-bit RGB PNG
 * with stored deflate blocks and no library.
 */
/*
 * Walks the frame's ordering table the way DrawOTag does (the head ClearOTagR
 * linked) and, with `verbose`, prints every glyph sprite with the DR_TPAGE that
 * governs it. Returns how many sprites it saw.
 */
static s32 walkGlyphs(u_long *top, s32 verbose) {
    P_TAG *node;
    s32 tpage;
    s32 count;
    s32 guard;

    tpage = 0;
    count = 0;
    node = (P_TAG *)top;
    for (guard = 0; guard < 4096; guard++) {
        if (node->len > 0) {
            if (node->code == CODE_DR_TPAGE) {
                tpage = drTpage(((DR_TPAGE *)node)->code[0]);
            } else if (node->code == CODE_SPRT) {
                SPRT *sprt = (SPRT *)node;

                if (verbose) {
                    printf(" glyph %d u=%d v=%d w=%d h=%d clut=%d tpage=%d at %d,%d\n",
                           (int)count, (int)sprt->u0, (int)sprt->v0,
                           (int)sprt->w, (int)sprt->h, (int)sprt->clut, tpage,
                           (int)sprt->x0, (int)sprt->y0);
                }
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

int main(int argc, char **argv) {
    s32 file = DEFAULT_FILE;
    s32 index = DEFAULT_STRING;
    s32 frames = DEFAULT_FRAMES;
    s32 frame;
    s32 glyphs;
    s32 total = 0;
    u8 *vram;
    u8 *head;
    u8 *data;
    TextWindow *win;
    DISPENV dispEnv;
    DRAWENV drawEnv;
    int width = 0;
    int height = 0;
    s32 i;

    if (argc < 2 || argc > 6) {
        fprintf(stderr, "usage: %s <disc.bin> [file index] [string index] [frames] [screenshot.png]\n",
                argv[0]);
        return 2;
    }
    if (argc > 2) {
        file = parseNumber(argv[2]);
    }
    if (argc > 3) {
        index = parseNumber(argv[3]);
    }
    if (argc > 4) {
        frames = parseNumber(argv[4]);
        if (frames <= 0) {
            frames = DEFAULT_FRAMES;
        }
    }

    pcDrawInit();
    pcCdReaderInit(argv[1]);

    if (!FILE_TABLE.exists(file)) {
        fprintf(stderr, "%s: no file %d on the disc\n", argv[1], file);
        return 1;
    }

    /* The font's own file as the game's cache reads it: the check ties this
       head to the file on the disc, and the two indexes it is keyed by. */
    head = FILE_CACHE.load(FILE_FONT);
    printf("font file %d, %d sectors, head ", (int)FILE_FONT,
           (int)FILE_TABLE.getSectorCount(FILE_FONT));
    for (i = 0; i < 32; i++) {
        printf("%02x", head[i]);
    }
    printf("\n");
    printf("text file %d, %d sectors\n", (int)file, (int)FILE_TABLE.getSectorCount(file));
    FILE_CACHE.free(FILE_FONT);

    PsyX_Initialise("dw3_pc_spike", SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    PsyX_GetScreenSize(&width, &height);
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "font_frame: PsyX_Initialise did not open a window "
                        "(no display or no OpenGL context)\n");
        return 1;
    }
    printf("window %dx%d, PsyCross presents %dx%d\n", width, height, SCREEN_WIDTH, SCREEN_HEIGHT);

    SetDefDrawEnv(&drawEnv, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    drawEnv.isbg = 1;
    PutDrawEnv(&drawEnv);
    SetDefDispEnv(&dispEnv, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    PutDispEnv(&dispEnv);

    /* The game's own font loading: loadFont (src/engine/text/font.c) through
       the real TIM loader (src/engine/gfx/tim_loader.c), which uploads the
       font archive's TIMs to VRAM with LoadImage/DrawSync. */
    FONT.load();

    /* What landed in VRAM, read back with PsyCross's GR_ReadVRAM. */
    vram = HEAP.allocZeroed(FONT_VRAM_BYTES, MEM_MODE);
    for (i = 0; i < FONT_IMAGES; i++) {
        GR_ReadVRAM((unsigned short *)(vram + i * FONT_IMAGE_BYTES),
                    FONT_VRAM_X + i * FONT_IMAGE_W, FONT_VRAM_Y, FONT_IMAGE_W, FONT_IMAGE_H);
    }
    for (i = 0; i < FONT_IMAGES; i++) {
        printf("vram %d,%d %dx%d (%d bytes) sum=%08x\n",
               (int)(FONT_VRAM_X + i * FONT_IMAGE_W), FONT_VRAM_Y,
               FONT_IMAGE_W, FONT_IMAGE_H, (int)FONT_IMAGE_BYTES,
               (unsigned)checksum(vram + i * FONT_IMAGE_BYTES, FONT_IMAGE_BYTES));
    }
    printf("vram region %d,%d %dx%d (%d bytes) sum=%08x\n",
           FONT_VRAM_X, FONT_VRAM_Y, FONT_IMAGE_W * FONT_IMAGES, FONT_IMAGE_H,
           (int)FONT_VRAM_BYTES, (unsigned)checksum(vram, FONT_VRAM_BYTES));

    /* A string of a language file on the disc, drawn in the banner's style by
       the game's own text window. */
    data = FILE_CACHE.load(file);
    if (data == NULL) {
        fprintf(stderr, "file %d: the game's cache returned no data\n", (int)file);
        return 1;
    }
    win = createTextWindow(BANNER_LAYER, BANNER_STYLE, BANNER_PLACE_X, BANNER_PLACE_Y);
    textWindowSetString(win, (char *)data, index);

    for (frame = 0; frame < frames; frame++) {
        pcDrawBeginFrame();
        textWindowDraw(win);
        /* Every frame draws the same thing: the first one is printed glyph by
           glyph (the measurement), the rest only say how many. */
        glyphs = walkGlyphs(pcDrawOtTop(), frame == 0);
        printf("frame %03d: %d glyphs, %d bytes\n",
               (int)frame, (int)glyphs, (int)pcDrawUsed());
        total += glyphs;

        PsyX_BeginScene();
        DrawOTag(pcDrawOtTop());
        /* The frame is in the back buffer now; read it before the swap, which
           is what the window will show. */
        if (argc == 6 && frame == frames - 1) {
            pcSaveShot(argv[5], width, height);
        }
        PsyX_EndScene();
        PsyX_WaitForTimestep(1);
    }

    printf("presented %d frames, %d glyph sprites drawn by the game's own text window\n",
           (int)frames, (int)total);
    if (argc == 6) {
        printf("screenshot: %s\n", argv[5]);
    }
    if (total == 0) {
        fprintf(stderr, "font_frame: the text window drew no glyph in %d frames\n", (int)frames);
        return 1;
    }

    PsyX_Shutdown();
    return 0;
}
