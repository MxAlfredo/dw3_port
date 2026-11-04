/*
 * L4b: a wider talk box, as a player-visible choice.
 *
 * The retail talk box is 95..139 px wide (talk_box.c clamps the measured text to that range) and
 * is only as wide as its widest line. With [display] talk_box_width a longer line fits (the
 * es-419 text is about 15% longer than the English one). Everything here is pure (no game, no
 * SDL) so texttest drives it; the host wrapper is talk_box_wide.c.
 *
 *   original            139 px, the retail maximum: the box geometry is byte-identical to retail
 *   <n>                 139 < n <= 280 px; 180 is the default (the owner's pick, 2026-10-29)
 *
 * DW3_TALK_BOX_WIDTH (the same two forms) overrides the file, for captures.
 *
 * Geometry (from talk_box.c; w is the box width, x the task's x):
 *   type 0/1: the box grows to the LEFT of the actor, outline x-w .. x+32
 *   type 2/3: the box grows to the RIGHT of the actor, outline x-26 .. x+w+6
 */
#ifndef TALK_BOX_WIDE_H
#define TALK_BOX_WIDE_H

#ifndef TBW_GAME_UNIT /* the game's headers redeclare strlen & co.: the game-side wrapper takes the math only */
#include <stdlib.h>
#include <string.h>
#endif

#define TBW_MIN 95        /* talk_box.c's minimum */
#define TBW_ORIGINAL 139  /* talk_box.c's maximum */
#define TBW_DEFAULT 180   /* the width when settings.ini has none (owner's pick after the L4b captures) */
#define TBW_LIMIT 280     /* the widest the setting accepts */
#define TBW_SCREEN_W 320
#define TBW_MARGIN 2      /* px kept free at both screen edges */

#ifndef TBW_GAME_UNIT
/* "original" -> 139, a number in 139..280 -> itself, anything else -> 0 */
static inline int tbwParseWidth(const char *v)
{
    char *end;
    long n;
    if (!v) return 0;
    while (*v == ' ' || *v == '\t') v++;
    if (!strncmp(v, "original", 8)) {
        const char *p = v + 8;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        return *p == '\0' ? TBW_ORIGINAL : 0;
    }
    n = strtol(v, &end, 10);
    if (end == v) return 0;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (*end != '\0') return 0;
    return (n >= TBW_ORIGINAL && n <= TBW_LIMIT) ? (int)n : 0;
}

/* The widths the settings file offers (the hidden cycle's values); the environment accepts any */
#define TBW_LISTED_COUNT 6
static const int tbwListed_[TBW_LISTED_COUNT] = {139, 160, 180, 200, 220, 250};
static inline int tbwListed(int w)
{
    int i;
    for (i = 0; i < TBW_LISTED_COUNT; i++)
        if (tbwListed_[i] == w) return w;
    return 0;
}

/* The value of `talk_box_width` under [display] in the text of settings.ini; 0 when absent or invalid */
static inline int tbwScanSettings(const char *text, size_t len)
{
    size_t i = 0;
    int inDisplay = 0;
    while (i < len) {
        size_t s = i, e;
        char line[160];
        while (i < len && text[i] != '\n') i++;
        e = i;
        if (i < len) i++;
        while (s < e && (text[s] == ' ' || text[s] == '\t')) s++;
        while (e > s && (text[e - 1] == '\r' || text[e - 1] == ' ' || text[e - 1] == '\t')) e--;
        if (e <= s || text[s] == ';' || text[s] == '#') continue;
        if (e - s >= sizeof line) continue;
        memcpy(line, text + s, e - s);
        line[e - s] = '\0';
        if (line[0] == '[') {
            inDisplay = !strcmp(line, "[display]");
            continue;
        }
        if (inDisplay) {
            char *eq = strchr(line, '=');
            char *k = line;
            if (!eq) continue;
            *eq = '\0';
            while (*k == ' ' || *k == '\t') k++;
            {
                size_t kl = strlen(k);
                while (kl && (k[kl - 1] == ' ' || k[kl - 1] == '\t')) k[--kl] = '\0';
            }
            if (!strcmp(k, "talk_box_width")) return tbwListed(tbwParseWidth(eq + 1));
        }
    }
    return 0;
}

#endif

/* The width of a box whose widest line measures `measured`, with the maximum `max` (retail: 139) */
static inline int tbwBoxWidth(int measured, int max)
{
    if (max < TBW_ORIGINAL) max = TBW_ORIGINAL;
    if (max > TBW_LIMIT) max = TBW_LIMIT;
    if (measured < TBW_MIN) return TBW_MIN;
    return measured > max ? max : measured;
}

/* The outline's left and right edge of a box of this type at x with width w */
static inline int tbwLeft(int type, int x, int w) { return type < 2 ? x - w : x - 26; }
static inline int tbwRight(int type, int x, int w) { return type < 2 ? x + 32 : x + w + 6; }

/* x moved the least that keeps the outline inside the screen (margin px from each edge). A box no
   wider than the retail maximum is never moved (retail geometry). */
static inline int tbwClampX(int type, int x, int w)
{
    int lo, hi;
    if (w <= TBW_ORIGINAL) return x;
    if (type < 2) {
        lo = TBW_MARGIN + w;
        hi = TBW_SCREEN_W - TBW_MARGIN - 32;
    } else {
        lo = TBW_MARGIN + 26;
        hi = TBW_SCREEN_W - TBW_MARGIN - w - 6;
    }
    if (lo > hi) return (lo + hi) / 2;
    return x < lo ? lo : (x > hi ? hi : x);
}

#endif
