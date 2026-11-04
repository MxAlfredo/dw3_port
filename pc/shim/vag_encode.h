#ifndef PC_VAG_ENCODE_H
#define PC_VAG_ENCODE_H

/*
 * PlayStation SPU ADPCM encoder (content-export E3c). Mono 16-bit PCM in, 16-byte blocks of 28 samples out: a header byte
 * (filter << 4 | shift), a flag byte and 14 bytes of 4-bit residuals. The encoder tries the five filters and the shifts 0..12 of
 * every block and keeps the pair with the least squared error, predicting from what the decoder will have (closed loop), with the
 * arithmetic of the port's SPU renderer (PsyX_SPUAL's float decoder), so what the game plays is what the encoder measured.
 *
 * Block flags: bit 0 is the end flag (the last block always has it), bit 1 the repeat flag (set on the last block of a looping
 * sample: the SPU jumps back, instead of muting the voice), bit 2 the loop start (on the block the loop returns to).
 * Pure: no files, no game state.
 */

#include <stddef.h>
#include <stdint.h>

/* Samples per block and bytes per block */
#define VAG_BLOCK_SAMPLES 28
#define VAG_BLOCK_BYTES 16

/* Encodes frames samples. loopStart < 0: the sample plays once (end flag, no repeat). loopStart >= 0: a loop from that frame to the
   last one: the SPU loops whole blocks, so the loop starts at the block that holds loopStart (rounded down to a multiple of 28)
   and the last block is filled by repeating the loop's own samples instead of silence. Returns the malloc'ed bytes (the caller
   frees them), *size a multiple of 16; NULL for no samples, a loopStart past the end or out of memory. */
uint8_t *vagEncode(const int16_t *pcm, size_t frames, long loopStart, size_t *size);

/* The decoder the encoder predicts with, for tests and tools: decodes blocks up to and including the first with the end flag (or all
   of them) into out (size / 16 * 28 samples) and returns the frame count. */
size_t vagDecodeBlocks(const uint8_t *vag, size_t size, int16_t *out);

#endif
