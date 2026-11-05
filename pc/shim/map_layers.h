#ifndef PC_MAP_LAYERS_H
#define PC_MAP_LAYERS_H

/*
 * Codec of the map layers of a SnnnTMPK.BIN (new-stages T2, ADR-012 §5.2). Portable C99: no game header, no other port file, so it
 * builds on Linux, Windows and Android and a tool or a mod loader can use it.
 *
 * A TMPK file is an archive: a table of u32 offsets (the first nonzero one is also the size of the table; an offset of 0 is an absent
 * entry), entry 0 (the map-object sprite sheet, opaque here) and then one entry per layer. A layer is a plane of one byte per pixel, a multiple of 128 px
 * in both directions, stored as the deduplicated tree FIELDSTG_getMapCell walks (src/field/field_mode/map.c:64-97):
 *
 *   entry   = 6 u32 offsets from the entry start (grid, cells64, cells32, cells16, cells8, pixels), then the parts
 *   grid    = u8 width, u8 height (in 128 px cells), then one u8 per cell: a node of cells64
 *   cells64 = per node 4 u8, a node of cells32 for each 64 px quadrant   (quadrant = (y & 0x40 ? 2 : 0) + (x & 0x40 ? 1 : 0))
 *   cells32 = per node 4 s16, a node of cells16 for each 32 px quadrant
 *   cells16 = per node 4 s16, a node of cells8 for each 16 px quadrant
 *   cells8  = per node 4 s16, a pixel block for each 8 px quadrant
 *   pixels  = 64 u8 per block, row-major 8x8
 *
 * What a layer's values mean (floor, triggers, areas) is not this codec's business; see docs/formats.md section 4.3.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Results */
#define ML_OK 0
#define ML_ERR_ARG (-1) /* a null pointer, an index out of range */
#define ML_ERR_SIZE (-2) /* width/height not a multiple of 128, zero, or past the limits below */
#define ML_ERR_TOO_MANY (-3) /* more distinct nodes than an index of the format can name (limits below) */
#define ML_ERR_FORMAT (-4) /* a damaged or truncated entry or archive */
#define ML_ERR_MEMORY (-5)

/*
 * Limits of the format as FIELDSTG_getMapCell reads it:
 *  - the grid size is two u8: 1..255 cells of 128 px each way;
 *  - a grid value (a cells64 node) is a u8: at most 256 distinct 128 px blocks;
 *  - a cells64 value (a cells32 node) is a u8: at most 256 distinct 64 px blocks;
 *  - cells32/cells16/cells8 values are s16: at most 32767 distinct 32 px, 16 px blocks and 8x8 pixel blocks.
 * Encoding past any of them fails with ML_ERR_TOO_MANY (nothing is truncated). ML_MAX_PIXELS caps a plane's area (memory guard).
 */
#define ML_CELL 128
#define ML_MAX_CELLS 255
#define ML_MAX_NODES_64 256
#define ML_MAX_NODES_32 256
#define ML_MAX_NODES_SMALL 32767
#define ML_MAX_PIXELS (1L << 28)

typedef struct {
    int w, h;   /* in pixels, multiples of 128 */
    uint8_t *v; /* w * h bytes, row-major; owned: free with mlImageFree */
} MlImage;

const char *mlErrorText(int code);

/* Decodes one layer entry (len = bytes of the entry). Every index is checked; a damaged entry is ML_ERR_FORMAT. */
int mlDecode(const uint8_t *entry, size_t len, MlImage *out);
void mlImageFree(MlImage *img);

/* Builds a layer entry from a plane (w and h multiples of 128). The caller frees *out with free(). On failure *out is NULL. */
int mlEncode(const uint8_t *v, int w, int h, uint8_t **out, size_t *len);

/* Archive helpers. mlArchiveCount returns the number of entries (entry 0 included) or a negative ML_ERR_*. Entry n spans its offset to
   the next one that is there (the last: to the end of the file), so it includes any padding. An offset of 0 is an absent entry (some
   stages have no second or third layer): *entry = NULL, *size = 0, ML_OK. */
int mlArchiveCount(const uint8_t *data, size_t len);
int mlArchiveEntry(const uint8_t *data, size_t len, int n, const uint8_t **entry, size_t *size);

/* Builds an archive: entry 0 = sheet (may be NULL/0: empty) then the layers in order. Entries start 4-aligned (zero padding); a NULL
   layer is an absent entry (offset 0). Free *out with free(). */
int mlArchiveBuild(const uint8_t *sheet, size_t sheetLen, const uint8_t *const *layers, const size_t *layerLens, int nLayers,
                   uint8_t **out, size_t *outLen);

#ifdef __cplusplus
}
#endif

#endif
