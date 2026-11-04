#ifndef PC_HOST_MDEC_H
#define PC_HOST_MDEC_H

/*
 * The PlayStation MDEC decoder, as a self-contained C module (T37).
 *
 * PsyCross carries libpress.h but not its implementation, and the port needs a
 * real decoder behind DecDCTvlc2/DecDCTin/DecDCTout (T39). This is that
 * decoder, with no PsyQ or PsyCross dependency: it compiles standalone and the
 * offline harness (pc/mdectest.c) drives it without a disc reader, a GPU or a
 * window.
 *
 * Two layers, the same split libpress has:
 *
 *   hostMdecVlcBuild  builds the VLC code table (the MPEG-1 DCT coefficient
 *                     table, ISO/IEC 11172-2 Table B.14, public);
 *   hostMdecVlc2      turns one frame's Huffman data into the MDEC run-level
 *                     halfword stream: the command word first, then one token
 *                     per coefficient run, padded with 0xFE00;
 *   hostMdecInit      builds the whole decoder: VLC table, dequantization
 *                     tables (the public MPEG-1 intra matrix) and the IDCT
 *                     scale table (computed by formula, not stored);
 *   hostMdecDecodeFrame  walks the run-level stream (dequantize, IDCT,
 *                     YUV->RGB) and writes the frame as RGB24 or BGR555.
 *
 * Everything here is standard MPEG-1/psx-spx data or computed by formula: no
 * bytes from the game or from Sony's libraries are stored (ADR-001).
 */

#include <stdint.h>

/*
 * The VLC table is a binary tree: each node has two children (-1 = absent) and
 * either a symbol index (>= 0) or -1 for an internal node. A code is at most 16
 * bits, and Table B.14 has 113 symbols, so 256 nodes are ample.
 */
#define HMDEC_VLC_MAX_NODES 256
#define HMDEC_VLC_SYMBOLS 113

typedef struct {
    int16_t child[HMDEC_VLC_MAX_NODES][2];
    int16_t symbol[HMDEC_VLC_MAX_NODES];
    int nodes;

    /* Filled in by hostMdecVlc2: where an invalid code was found (bit offset
       from the start of the Huffman data; -1 when there was none), and how
       many tokens were decoded before the end-of-frame padding. */
    int lastErrorBit;
    int lastTokens;
} HostMdecVlcTable;

/* What a frame decode saw, for the harness's structural assertions. */
typedef struct {
    int macroblocks; /* 16x16 macroblocks written */
    int blocks;      /* 8x8 blocks read from the run-level stream */
    int eobBlocks;   /* blocks closed by an explicit 0xFE00 end code */
    int errors;      /* invalid VLC codes or a truncated run-level stream */
    int tokens;      /* run-level tokens consumed (including skipped FE00) */
} HostMdecStats;

/*
 * The decoder state: the VLC table, the two dequantization tables (Y and
 * chroma; the MDEC keeps two, and the discs use the same matrix for both) and
 * the IDCT scale table, all in zigzag order where the format needs it.
 */
typedef struct {
    HostMdecVlcTable vlc;
    int16_t scale[64];
    uint8_t qt[2][64];
} HostMdec;

/* Depth modes the frame decoder understands; HMDEC_DEPTH_COMMAND takes the
   depth from the run-level stream's command word (what a player does). */
#define HMDEC_DEPTH_COMMAND 0
#define HMDEC_DEPTH_24BPP 24
#define HMDEC_DEPTH_15BPP 15

/*
 * Builds the VLC tree. Must be called before hostMdecVlc2 or hostMdecInit.
 */
void hostMdecVlcBuild(HostMdecVlcTable *table);

/*
 * The libpress DecDCTvlc2 contract: `src` points at a frame's MDEC payload
 * (u16 count at +0, u16 q_scale at +4, u16 version at +6, Huffman data from
 * byte 8, all little-endian, MSB-first through the 16-bit words). `dst`
 * receives the 32-bit command word 0x38000000|count followed by `count` 32-bit
 * words of run-level data (i.e. 2*count halfwords), padded with 0xFE00.
 *
 * A frame's compressed data can span several 2016-byte sectors; `src` must
 * point at a buffer that holds the whole frame (the sectors' payloads
 * concatenated), because the Huffman data runs on past the first sector.
 *
 * Returns 0 when the stream completed (count filled, or the frame's end marker
 * was seen and the rest padded) and non-zero for an invalid Huffman code;
 * `table->lastErrorBit` then reports the bit offset of the failure. This is
 * libpress's output layout, but the return value follows this module's own
 * convention (0 = success), which is what the harness asserts.
 */
int hostMdecVlc2(const void *src, void *dst, void *table);

/*
 * Builds the decoder tables: the VLC tree, the dequantization matrix and the
 * IDCT scale table. `scale[u*8+x] = floor(32768 * C(u) * cos((2x+1)*u*PI/16))`
 * with C(0) = 1/sqrt(2).
 */
void hostMdecInit(HostMdec *mdec);

/*
 * Decodes one frame from the run-level stream `stream` (the output of
 * hostMdecVlc2, command word included) into `out`:
 *
 *   depth 24: RGB24, 3 bytes per pixel, R,G,B order, rows top-down;
 *   depth 15: BGR555 little-endian, 2 bytes per pixel, as the PSX orders it;
 *   HMDEC_DEPTH_COMMAND: the command word's depth field (bits 27-26).
 *
 * `width` and `height` must be multiples of 16. Macroblocks are stored
 * column-major, as PSX movie data does: macroblock m is column m/(height/16),
 * row m%(height/16). Returns 0 on success, -1 for a bad argument or an
 * unsupported depth.
 */
int hostMdecDecodeFrame(HostMdec *mdec, const void *stream, int width,
                        int height, int depth, void *out, HostMdecStats *stats);

#endif /* PC_HOST_MDEC_H */
