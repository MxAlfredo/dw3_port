#ifndef PC_MAP_PACK_H
#define PC_MAP_PACK_H

/*
 * Codec of a Z_STAGE SnnnPACK.BIN, the background tiles of a field stage (new-stages T3, ADR-012 section 4). Portable C99: no game header, no
 * other port file, so it builds on Linux, Windows and Android and the mod loader can serve a generated PACK as a virtual file.
 *
 * Every byte here is read from the game's own code (path:line in docs/formats.md section 4.4); the sweep of mappacktest proves it against the
 * 237 PACK files of the EU disc.
 *
 *   PACK   = sector 0 (2048 bytes), then width * height tiles of tileBytes each, in row order: tile `frame` is at sector
 *            frame * (tileBytes / 2048) + 1 (FIELDSTG_seekStream, src/field/field_mode/stream.c:17). The file has exactly 1 + width * height
 *            * (tileBytes / 2048) sectors.
 *   sector 0 = s32 tileCount (nothing reads it), s32 width, s32 height (in 128 px tiles), s32 tileBytes, u16 tileSizes[width * height]
 *            (map_streamer.c:315-323), zero to the end of the sector.
 *   tile   = tileSizes[frame] bytes (0: an empty tile, never read; its room is zero), always a whole number of sectors: the reader asks
 *            the CD for tileSizes >> 11 sectors (map_streamer.c:322). The bytes are an RLEN block ("RLEN", u32 unpacked size, runs, 0) or the
 *            plain tile; the Decompressor takes either (decompressor.c:13-25), the rest of the last sector is zero.
 *   tile, unpacked = u32 offset of the TIM, then three sprite lists (u32 count, then count records of six s16 x, y, u, v, w, h;
 *            FIELDSTG_loadStreamSprites, stream.c:83-107), then at the offset an 8 bit TIM with a 256 colour CLUT of one row.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Results */
#define MP_OK 0
#define MP_ERR_ARG (-1)    /* a null pointer, an index out of range */
#define MP_ERR_SIZE (-2)   /* a size that is zero, not a whole number of sectors, or does not fit */
#define MP_ERR_LIMIT (-3)  /* past a limit of the reader (below) */
#define MP_ERR_FORMAT (-4) /* a damaged, truncated or unsupported PACK or tile */
#define MP_ERR_MEMORY (-5)

/*
 * Limits, as the reader sees them:
 *  - MP_MAX_TILES 255: the tile of a view slot is kept in a u8 with 0xFF meaning none (MapStreamer.viewTiles, map_streamer.c:275,283), so
 *    frame = x + y * width must be 0..254. The disc's biggest map has 168 tiles (14 x 12).
 *  - MP_MAX_HEADER_TILES 1016: what sector 0 can hold ((2048 - 16) / 2 u16 sizes); the parser accepts up to it.
 *  - MP_MAX_TILE_BYTES 0xF800: tileSizes are u16 (so at most 31 sectors); tileBytes is also the buffer of each of the 30 stream tasks
 *    (map_streamer.c:326-328), 30 times over. The disc uses 14 KB to 22 KB.
 *  - MP_MAX_SPRITES 5 per list and 3 lists: StreamTask.sprites[3][5] (field_mode.h:440), the reader copies as many as the tile says without a
 *    check. 8 tiles of the disc list more (the parser reads them; the encoder refuses).
 *  - the image goes to VRAM slot s at (640 + 128 * (s / 2 % 3), 128 * (s % 2) + 256 * (s / 6)) (FIELDSTG_slotImages, fieldstg.c:1505), the
 *    slots are 128 halfwords and 128 rows apart: an image is at most 128 halfwords (256 px) wide and 128 rows high (MP_MAX_IMAGE_W/H). The
 *    CLUT is loaded at (0, 0xF0 + slot) with the TIM's own width and height (stream.c:111-112, tim_loader.c:44-48): 256 x 1.
 */
#define MP_SECTOR 2048
#define MP_MAX_TILES 255
#define MP_MAX_HEADER_TILES 1016
#define MP_MAX_TILE_BYTES 0xF800
#define MP_LISTS 3
#define MP_MAX_SPRITES 5
#define MP_MAX_LIST_READ 64 /* what the parser takes in a list before it calls the tile damaged */
#define MP_MAX_IMAGE_W 256
#define MP_MAX_IMAGE_H 128
#define MP_MAX_UNPACKED 0x100000
#define MP_CLUT 256

const char *mpErrorText(int code);

/* ---- the container ---- */

typedef struct {
    int tileCount, width, height, tileBytes;
    uint16_t sizes[MP_MAX_HEADER_TILES]; /* the first width * height are set */
} MpPack;

/* Reads sector 0 and checks it against the file (len = bytes of the file): a grid inside the sector, tileBytes a whole number of sectors,
   every size a whole number of sectors that fits tileBytes, every tile inside the file. tileCount is not checked (nothing reads it). */
int mpPackParse(const uint8_t *data, size_t len, MpPack *out);

/* The bytes of tile `frame` (its tileSizes[frame] of them): *block = NULL, *blockLen = 0 for an empty tile. */
int mpPackTile(const uint8_t *data, size_t len, const MpPack *p, int frame, const uint8_t **block, size_t *blockLen);

/*
 * Builds a PACK of width x height tiles (1..MP_MAX_TILES in all). blocks[frame] / blockLens[frame] is the bytes of each tile, a NULL or 0 for
 * an empty one; a length must be a whole number of sectors (mpTileEncode gives them). tileBytes 0 = the smallest that holds every tile (at
 * least one sector); otherwise a multiple of 2048 that holds them. tileCount is set to the tiles with data. Free *out with free().
 */
int mpPackBuild(int width, int height, int tileBytes, const uint8_t *const *blocks, const size_t *blockLens, uint8_t **out, size_t *outLen);

/* ---- one tile ---- */

typedef struct {
    int x, y, u, v, w, h; /* the SPRT the game draws: at (x, y) from the tile's top left, from (u, v) of the 8 bit image, w x h px */
} MpSprite;

typedef struct {
    int rle;                /* decode: 1 when the block was RLEN packed. encode: 1 to pack it */
    int count[MP_LISTS];    /* sprites per list: the three depths 7, 3 and 11 of the map layer (FIELDSTG_spriteDepths, fieldstg.c:1502) */
    MpSprite sp[MP_LISTS][MP_MAX_LIST_READ];
    int w, h;               /* the image in pixels: w even, up to MP_MAX_IMAGE_W x MP_MAX_IMAGE_H */
    uint8_t *pix;           /* w * h palette indices, row-major; owned when decoded: mpTileFree. encode: the caller's */
    uint16_t clut[MP_CLUT]; /* PSX 1555 colours */
    int clutX, clutY, imgX, imgY; /* the TIM's own rectangles (the loader ignores them: it places the image and the CLUT itself) */
} MpTile;

/* Fills a tile of width x height pixels (even width) with a single sprite that shows it all at the tile's origin (the common case of
   the disc: 7234 of 11745 tiles have exactly that in list 0). pix and clut are the caller's. */
void mpTileSimple(MpTile *t, int width, int height, uint8_t *pix);

/* Reads a tile block (len = bytes of the block, RLEN or plain). The pixels are malloc'ed: free with mpTileFree. */
int mpTileDecode(const uint8_t *block, size_t len, MpTile *out);
void mpTileFree(MpTile *t);

/* Builds the block of a tile: unpacked data, RLEN packed when t->rle, zero padded to a whole number of sectors. Free *out with free(). The
   greedy packer (runs of 2 or more, literals up to 127) gives the disc's bytes for 7309 of the 7342 packed tiles; the other 33 differ only
   in the tail: the disc's packer ends on a run of one where this one writes a literal (both unpack alike). */
int mpTileEncode(const MpTile *t, uint8_t **block, size_t *len);

/* RLEN: unpacks src (the whole block: magic, size, runs) into a malloc'ed buffer; packs src into a block */
int mpRlenUnpack(const uint8_t *block, size_t len, uint8_t **out, size_t *outLen);
int mpRlenPack(const uint8_t *src, size_t len, uint8_t **out, size_t *outLen);

#ifdef __cplusplus
}
#endif

#endif
