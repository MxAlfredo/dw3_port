/*
 * Host harness (milestone 3): builds a draw order with the game's own text
 * window and inspects the primitives it wrote.
 *
 *     draw_order <disc.bin> <file index> <string index>
 *
 * The file is resolved and read by the game's own file layer (FILE_TABLE and
 * FILE_CACHE over CD_READER, milestone 1); a text window is created with the
 * game's own createTextWindow (src/engine/text/text_window.c), given the
 * string through its own setString (getString takes the index'th string out
 * of the language file) and drawn with textWindowDraw. That unit is compiled
 * against PsyCross's libgpu/libgte, so its glyphs are written as PsyCross
 * SPRT/POLY_FT4 packets and linked into an ordering table with PsyCross's
 * addPrim; the ordering table and packet buffer are the host shim's
 * (shim/host_draw.c). No window is opened and no renderer is initialised:
 * nothing here calls PsyX_Initialise, SDL or the GPU.
 *
 * The output is the measurement: the packet layout, the counts by kind and
 * every primitive (code, length, size), then whether the chain is contiguous.
 * pc/check_draw.py compares it against an independent expectation.
 */
#include "engine/file.h"
#include "engine/text.h"
#include "shim/host.h"

#include <stdio.h>
#include <stdlib.h>

/* The layer the game's modes draw their menus on (engine/gfx.h) */
#define DRAW_LAYER SCREEN_LAYER

/* The banner's style, FONT_STYLES[1] (src/engine/text/font.c), the same the
   text engine check measures */
#define DRAW_STYLE 1

/* The kinds textWindowDraw writes, as its setcode values */
#define CODE_SPRT 0x64
#define CODE_POLY_FT4 0x2C
#define CODE_DR_TPAGE 0xE1

int main(int argc, char **argv) {
    s32 file;
    s32 index;
    u32 *data;
    TextWindow *win;
    P_TAG *node;
    void *next;
    void *head;
    s32 prims;
    s32 sprt;
    s32 ft4;
    s32 tpage;
    s32 chain;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <disc.bin> <file index> <string index>\n", argv[0]);
        return 2;
    }

    pcDrawInit();
    pcCdReaderInit(argv[1]);

    file = atoi(argv[2]);
    index = atoi(argv[3]);
    if (!FILE_TABLE.exists(file)) {
        fprintf(stderr, "%s: no file %d on the disc\n", argv[1], file);
        return 1;
    }

    data = FILE_CACHE.load(file);
    if (data == NULL) {
        fprintf(stderr, "file %d: the game's cache returned no data\n", file);
        return 1;
    }

    win = createTextWindow(DRAW_LAYER, DRAW_STYLE, 0, 0);
    textWindowSetString(win, (char *)data, index);
    textWindowDraw(win);

    /* The packet layout, measured: PsyCross's host sizes, not retail 4.7's. */
    printf("packet P_TAG=%zu OT_TAG=%zu SPRT=%zu POLY_FT4=%zu DR_TPAGE=%zu P_LEN=%d\n",
           sizeof(P_TAG), sizeof(OT_TAG), sizeof(SPRT), sizeof(POLY_FT4),
           sizeof(DR_TPAGE), (int)P_LEN);
    printf("file %d string %d\n", file, index);

    /* Walk the ordering table: each entry holds the head primitive, and each
       primitive holds the previous head in its tag's address. The chain ends
       at 0 (the shim's zeroed entry). Primitive sizes are (len + P_LEN) * 4;
       consecutive primitives were bump-allocated, so their address gap must
       be the size of the lower one. */
    prims = 0;
    sprt = 0;
    ft4 = 0;
    tpage = 0;
    chain = 1;
    head = pcDrawOtHead(win->depth);
    for (node = head; node != NULL; node = next) {
        s32 size = (node->len + P_LEN) * 4;

        printf("prim 0x%02x %d %d\n", node->code, (int)node->len, (int)size);
        prims++;
        if (node->code == CODE_SPRT) {
            sprt++;
        } else if (node->code == CODE_POLY_FT4) {
            ft4++;
        } else if (node->code == CODE_DR_TPAGE) {
            tpage++;
        }

        next = (void *)node->addr;
        if (next != NULL) {
            s32 nextSize = (((P_TAG *)next)->len + P_LEN) * 4;
            if ((char *)node - (char *)next != nextSize) {
                chain = 0;
            }
        }
    }

    printf("ot prims=%d sprt=%d ft4=%d tpage=%d bytes=%d\n",
           (int)prims, (int)sprt, (int)ft4, (int)tpage, (int)pcDrawUsed());
    printf("chain %s\n", chain ? "ok" : "broken");
    return 0;
}
