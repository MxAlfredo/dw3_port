/*
 * The port's movie presenter (T39): the frame the MDEC shim left in VRAM,
 * drawn on the window as textured quads.
 *
 * Why this file exists: PsyCross never copies VRAM to the window. Its renderer
 * draws primitives straight to the window backbuffer (GR_BeginScene /
 * GR_DrawTriangles / PsyX_EndScene), and the one VRAM round trip it does have
 * goes the other way -- PsyX_EndScene's GR_StoreFrameBuffer copies the
 * backbuffer INTO VRAM, the framebuffer-readback emulation. So a frame that
 * only reaches VRAM through LoadImage is invisible: the movie renders into VRAM
 * correctly and the window keeps showing the clear colour (measured in T39:
 * 20 frames decoded, 20 presented, 1 colour, mean 31.00). The frame therefore
 * has to be drawn again, as a texture, which is what this file does.
 *
 * Geometry: the game calls setDisplayMode(320, 480, 1, 1) for the movie, and
 * the port's hostSetDisplayMode shows a 320x480 area at VRAM (0,0). The port
 * presents the frame at VRAM (0, 36) sized 320x416 (shim/host_press.c), so in
 * display space the quads cover (0, 36)-(320, 452). The window itself is
 * 320x240, so the 480-line display is shown at half height: the picture comes
 * out 320x208 inside the 240 rows, i.e. letterboxed by 16 rows top and bottom.
 *
 * Tiling: a 15bpp texture page is 256x256 and u/v are 8 bits, so one quad
 * cannot span the 416-row frame. The rect is split into tiles that stay inside
 * one page: 64-pixel columns (x = 0, 64, 128, 192, 256 -- the last one at page
 * x 256) and rows split at the page boundary (y 36..255 = 220 rows in page y 0,
 * y 256..451 = 196 rows in page y 256). That is ten quads. Each tile's tpage is
 * GetTPage(2, 0, x, y & ~255) (2 is 15bpp) and its u/v are relative to that
 * page: u 0..63 and v (y & 255).. . The bottom edge uses `v0 + h - 1`, not
 * `v0 + h`: v is a byte and 36 + 220 would wrap to 0 on the first row tile.
 * The one-texel shortfall is the same one a PS1 quad has and is invisible at
 * this scale.
 *
 * Drawing: PsyCross's DrawPrim does not flush its splits (it parses the single
 * primitive and returns; only DrawOTag ends with DrawAllSplits), so this file
 * builds a two-entry ordering table per frame -- ClearOTagR's terminator plus
 * the tiles -- and draws it with DrawOTag.
 *
 * The DRAWENV: a leftover clip from the previous mode would cut the picture,
 * so a full-display env with no clip goes in first. Its dfe MUST be 1:
 * PsyCross reads dfe as "this draws to the window" (DrawSplit's drawOnScreen),
 * and with dfe 0 it renders the split into its offscreen framebuffer instead
 * of the backbuffer. SetDefDrawEnv computes dfe from the height and sets it to
 * 0 for a 480-line display, so it is forced back to 1 here. The display env is
 * left alone; the next frame's layers apply their own draw env as usual.
 *
 * Host variables, like the rest of pc/: the page size, the 15bpp format and the
 * page mask are the GPU's, not the game's.
 */
#include "engine/game.h"

#include "host_video.h"

/* The largest movie the presenter accepts. Measured: the title's movies are
   320x416; anything larger disables the
   presenter instead of drawing a clipped or wrapped picture. */
#define HOST_VIDEO_MAX_W 320
#define HOST_VIDEO_MAX_H 416

/* One 15bpp texture page is 256x256; a tile never spans two. */
#define HOST_VIDEO_PAGE_H   256
#define HOST_VIDEO_COL_W     64   /* the u granularity, and the column width */

/* Tiles the largest accepted frame can need: up to 6 columns when the rect
   starts inside a page, times up to 3 page rows -- 18, bounded at 24. The
   movie's own rect (0, 36, 320, 416) uses 5 x 2 = 10. */
#define HOST_VIDEO_MAX_TILES 24

/* The display the movie runs on (setDisplayMode(320, 480)) and the DRAWENV the
   frame is drawn with. */
#define HOST_VIDEO_DISPLAY_W 320
#define HOST_VIDEO_DISPLAY_H 480

/* The frame is drawn opaque, like the MDEC output it comes from. */
#define HOST_VIDEO_COLOUR 0x80

static int videoValid;
static int videoX;
static int videoY;
static int videoW;
static int videoH;

static POLY_FT4 videoTiles[HOST_VIDEO_MAX_TILES];
static int videoTileCount;
static OT_TAG videoOt[2];
static DRAWENV videoEnv;

void hostVideoDisable(void) {
    videoValid = 0;
}

void hostVideoSetSource(int vramX, int vramY, int w, int h) {
    if (w <= 0 || h <= 0 || w > HOST_VIDEO_MAX_W || h > HOST_VIDEO_MAX_H) {
        hostVideoDisable();
        return;
    }
    videoX = vramX;
    videoY = vramY;
    videoW = w;
    videoH = h;
    videoValid = 1;
}

/* One tile's quad: screen (x, y)-(x + w, y + h) from its own texture page,
   with u/v relative to that page. */
static void hostVideoAddTile(int x, int y, int w, int h) {
    POLY_FT4 *tile;
    int u;
    int v;

    if (videoTileCount >= HOST_VIDEO_MAX_TILES) {
        return;
    }
    tile = &videoTiles[videoTileCount++];
    u = x & (HOST_VIDEO_COL_W - 1);
    v = y & (HOST_VIDEO_PAGE_H - 1);

    SetPolyFT4(tile);
    setRGB0(tile, HOST_VIDEO_COLOUR, HOST_VIDEO_COLOUR, HOST_VIDEO_COLOUR);
    /* TL, TR, BL, BR -- the order MakeVertexQuad/MakeTexcoordQuad pair up */
    setXY4(tile, x, y, x + w, y, x, y + h, x + w, y + h);
    setUV4(tile, u, v, u + w - 1, v, u, v + h - 1, u + w - 1, v + h - 1);
    tile->tpage = GetTPage(2, 0, x, y & ~(HOST_VIDEO_PAGE_H - 1));
    tile->clut = 0;
    addPrim(&videoOt[1], tile);
}

void hostVideoDraw(void) {
    int col;
    int row;

    if (!videoValid) {
        return;
    }

    /* The movie's display is 15bpp at VRAM (0,0) for both fields; dfe is what
       PsyCross reads to draw on the window instead of its offscreen buffer
       (see the file comment). the bars around the frame are cleared (below). */
    SetDefDrawEnv(&videoEnv, 0, 0, HOST_VIDEO_DISPLAY_W, HOST_VIDEO_DISPLAY_H);
    videoEnv.dfe = 1;
    /* Retail displays VRAM (0,0) 320x480 and the movie player clears VRAM
       (STDWTITL_clearVram) before it plays, so the lines above and below the
       416-line frame are black. The port's backbuffer keeps whatever the
       previous mode drew there (the country select's grey, the field's blue
       scene), so the bars are cleared to black explicitly every frame. */
    videoEnv.isbg = 1;
    setRGB0(&videoEnv, 0, 0, 0);
    PutDrawEnv(&videoEnv);

    ClearOTagR((u_long *)videoOt, 2);
    videoTileCount = 0;
    /* Tiles advance by their own size, not by the page size: an unaligned
       start makes the first tile of the row/column shorter. */
    col = 0;
    while (col < videoW) {
        int x = videoX + col;
        int tileW = HOST_VIDEO_COL_W - (x & (HOST_VIDEO_COL_W - 1));

        if (tileW > videoW - col) {
            tileW = videoW - col;
        }
        row = 0;
        while (row < videoH) {
            int y = videoY + row;
            int tileH = HOST_VIDEO_PAGE_H - (y & (HOST_VIDEO_PAGE_H - 1));

            if (tileH > videoH - row) {
                tileH = videoH - row;
            }
            hostVideoAddTile(x, y, tileW, tileH);
            row += tileH;
        }
        col += tileW;
    }
    DrawOTag((u_long *)&videoOt[1]);
}

int hostVideoActive(void) {
    return videoValid;
}
