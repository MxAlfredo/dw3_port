#include "gym_labels.h"

#define DIGIT0 0x04
#define IS_DIGIT(b) ((b) >= DIGIT0 && (b) <= DIGIT0 + 9)
#define MAX_DIGITS 10

int gymLabelFormat(const uint8_t *entry, int cost, uint8_t *out, int cap)
{
    size_t len = 0, tail, digits, spaces = 0, pad, i, total;
    uint8_t num[MAX_DIGITS];
    int nd = 0;

    while (entry[len]) {
        len++;
    }
    tail = len;
    while (tail > 0 && IS_DIGIT(entry[tail - 1])) {
        tail--;
    }
    digits = len - tail;
    if (digits == 0) {
        return 0;
    }
    while (tail >= 2 && entry[tail - 2] == 0x01 && entry[tail - 1] == 0x01) {
        tail -= 2;
        spaces++;
    }

    if (cost < 0) {
        cost = 0;
    }
    do {
        num[nd++] = (uint8_t)(DIGIT0 + cost % 10);
        cost /= 10;
    } while (cost > 0 && nd < MAX_DIGITS);

    /* the entry's field (spaces and digits) keeps its width; one space stays if it had one */
    pad = spaces + digits > (size_t)nd ? spaces + digits - (size_t)nd : 0;
    if (spaces > 0 && pad == 0) {
        pad = 1;
    }
    total = tail + pad * 2 + (size_t)nd;
    if (cap < 0 || total + 1 > (size_t)cap) {
        return 0;
    }
    for (i = 0; i < tail; i++) {
        out[i] = entry[i];
    }
    for (i = 0; i < pad; i++) {
        out[tail + i * 2] = 0x01;
        out[tail + i * 2 + 1] = 0x01;
    }
    for (i = 0; i < (size_t)nd; i++) {
        out[tail + pad * 2 + i] = num[(size_t)nd - 1 - i];
    }
    out[total] = 0;
    return (int)total;
}
