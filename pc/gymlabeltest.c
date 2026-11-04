/*
 * Unit test for the gym's intensity labels (pc/shim/gym_labels.c). Synthetic strings in the game's
 * encoding only (digit = 0x04 + d, space = 0x01 0x01, letters 0x0E+ for upper case): no disc, no retail text.
 */
#include <stdio.h>
#include <string.h>

#include "shim/gym_labels.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "gymlabeltest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

#define D(n) (0x04 + (n))
#define SP 0x01, 0x01
#define L(c) (0x0E + ((c) - 'A'))

static int same(const uint8_t *got, int len, const uint8_t *want)
{
    return (size_t)len == strlen((const char *)want) && !memcmp(got, want, len + 1);
}

int main(void)
{
    uint8_t out[32];
    int n;

    /* "XY 5" and "XY10" (two shapes of one field, two digits wide) */
    const uint8_t padded[] = {L('X'), L('Y'), SP, D(5), 0};
    const uint8_t tight[] = {L('X'), L('Y'), D(1), D(0), 0};
    const uint8_t padded10[] = {L('X'), L('Y'), D(1), D(0), 0};
    const uint8_t want3[] = {L('X'), L('Y'), SP, D(3), 0};
    const uint8_t want5[] = {L('X'), L('Y'), SP, D(5), 0};
    const uint8_t want1[] = {L('X'), L('Y'), SP, D(1), 0};
    const uint8_t want12[] = {L('X'), L('Y'), SP, D(1), D(2), 0};
    const uint8_t want100[] = {L('X'), L('Y'), D(1), D(0), D(0), 0};
    const uint8_t wantTight0[] = {L('X'), L('Y'), SP, D(0), 0};
    const uint8_t noNumber[] = {L('X'), L('Y'), 0};

    /* unchanged values give the entry back */
    n = gymLabelFormat(padded, 5, out, sizeof out);
    CHECK(same(out, n, padded));
    n = gymLabelFormat(tight, 10, out, sizeof out);
    CHECK(same(out, n, padded10));
    n = gymLabelFormat(padded, 1 + 4, out, sizeof out);
    CHECK(same(out, n, padded));

    /* patched values */
    n = gymLabelFormat(padded, 3, out, sizeof out);
    CHECK(same(out, n, want3));
    n = gymLabelFormat(tight, 5, out, sizeof out);
    CHECK(same(out, n, want5));
    n = gymLabelFormat(tight, 1, out, sizeof out);
    CHECK(same(out, n, want1));
    n = gymLabelFormat(padded, 12, out, sizeof out);
    CHECK(same(out, n, want12));
    n = gymLabelFormat(tight, 100, out, sizeof out);
    CHECK(same(out, n, want100));
    n = gymLabelFormat(tight, -4, out, sizeof out);
    CHECK(same(out, n, wantTight0));

    /* nothing to rewrite, or no room */
    CHECK(gymLabelFormat(noNumber, 3, out, sizeof out) == 0);
    CHECK(gymLabelFormat(padded, 3, out, 3) == 0);
    CHECK(gymLabelFormat(padded, 3, out, 0) == 0);

    if (failures) {
        fprintf(stderr, "gymlabeltest: %d failure(s)\n", failures);
        return 1;
    }
    printf("gymlabeltest: all checks passed\n");
    return 0;
}
