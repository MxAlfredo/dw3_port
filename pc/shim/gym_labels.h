#ifndef PC_GYM_LABELS_H
#define PC_GYM_LABELS_H

#include <stddef.h>
#include <stdint.h>

/*
 * The gym's intensity labels (the buttons and the confirmation message of STGTRAIN) with the cost the
 * game really charges. The disc's entries end in the number of training points ("PE 5", "PE10"...), but
 * the costs are the moddable table gym_intensity_costs, so the number has to follow the table.
 *
 * Text is in the game's own encoding: a digit is byte 0x04 + d and a space is the pair 0x01 0x01.
 */

/*
 * Rewrites the trailing number of `entry` (a NUL-terminated game string) to `cost`, keeping the prefix,
 * the language's separator and the field width: the number is padded with spaces to the width the entry
 * had (at least one space is kept when the entry had one). Writes the NUL-terminated result to `out`
 * and returns its length. Returns 0 (and writes nothing) when the entry does not end in a number or
 * `out` is too small: the caller then shows the entry as it is. Costs below 0 count as 0.
 */
int gymLabelFormat(const uint8_t *entry, int cost, uint8_t *out, int cap);

#endif
