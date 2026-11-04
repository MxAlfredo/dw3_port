/* The game's button icons inside its text (feature controls-config, C5); see text_icons.h. */
#include "text_icons.h"

#include <SDL.h>
#include <string.h>

/* Cross, circle, square, triangle: the order of PI_PS_CROSS.. in the atlas */
static const int SLOT_BUTTON[TI_BUTTONS] = {INPUT_BTN_CROSS, INPUT_BTN_CIRCLE, INPUT_BTN_SQUARE, INPUT_BTN_TRIANGLE};
static const int SLOT_NUMBER[TI_BUTTONS] = {27, 47, 50, 52};

/* FONT_ICONS_1[26], [46], [49], [51] of the EU table: page, u, v of each 12 x 12 cell */
const TextIconSite TEXT_ICON_SITES[TI_BUTTONS] = {{1, 232, 9}, {0, 96, 72}, {0, 132, 72}, {0, 180, 74}};

int textIconButton(int slot) { return slot >= 0 && slot < TI_BUTTONS ? SLOT_BUTTON[slot] : -1; }
int textIconNumber(int slot) { return slot >= 0 && slot < TI_BUTTONS ? SLOT_NUMBER[slot] : -1; }

/* The pad button an INI/PsyCross code names, -1 for none or an axis (triggers, sticks) */
static int padButtonOf(int code) {
    return code >= 0 && !(code & CONTROLLER_MAP_FLAG_AXIS) ? code : -1;
}

int textIconPlan(int device, int setting, int detected, const InputCfg *cfg, int cells[TI_BUTTONS]) {
    InputCfg live = *cfg, defaults;
    int family = inputIconFamilyFor(setting, detected);
    int kb = setting == INPUT_ICONS_AUTO && device == INPUT_DEV_KEYBOARD;
    int i, own = 1;

    inputCfgDefaults(&defaults);
    for (i = 0; i < TI_BUTTONS; i++) {
        int mine = PI_PS_CROSS + i; /* the game's own icon for this button */
        int cell = mine;
        int *code;

        if (kb) {
            code = inputCfgSlot(&live, INPUT_DEV_KEYBOARD, SLOT_BUTTON[i]);
            if (code != NULL && promptIconKey(*code) != PI_NONE) {
                cell = promptIconKey(*code);
            }
        } else if (family != INPUT_FAMILY_GENERIC) {
            /* a generic pad's labels are unknown: it keeps the game's own */
            code = inputCfgSlot(&live, INPUT_DEV_PAD, SLOT_BUTTON[i]);
            cell = code != NULL && padButtonOf(*code) >= 0 ? promptIconPad(family, *code) : PI_NONE;
            if (cell == PI_NONE) {
                /* bound to a trigger or a stick: what that button is by default */
                code = inputCfgSlot(&defaults, INPUT_DEV_PAD, SLOT_BUTTON[i]);
                cell = code != NULL && padButtonOf(*code) >= 0 ? promptIconPad(family, *code) : PI_NONE;
            }
            if (cell == PI_NONE) {
                cell = mine;
            }
        }
        cells[i] = cell;
        own &= cell == mine;
    }
    return own;
}

/* The source row/column of each of the 12 target ones: the 14-pixel cell without lines 3 and 10 */
static const unsigned char SRC12[TI_SIZE] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13};

#define MAX_COLOURS (TI_BUTTONS * TI_SIZE * TI_SIZE)

static int colourDistance(unsigned short a, unsigned short b) {
    int dr = (a & 31) - (b & 31), dg = ((a >> 5) & 31) - ((b >> 5) & 31), db = ((a >> 10) & 31) - ((b >> 10) & 31);

    return dr * dr + dg * dg + db * db;
}

void textIconBuild(const int cells[TI_BUTTONS], TextIconSet *out) {
    static unsigned short col[MAX_COLOURS];
    static int cnt[MAX_COLOURS], target[MAX_COLOURS], alive[MAX_COLOURS];
    static int idx[TI_BUTTONS][TI_SIZE * TI_SIZE]; /* colour id per pixel, -1 transparent */
    static int final[MAX_COLOURS];
    int n = 0, live, i, j, k, x, y, u, v;

    for (i = 0; i < TI_BUTTONS; i++) {
        int has = promptIconUV(cells[i], &u, &v);

        for (y = 0; y < TI_SIZE; y++) {
            for (x = 0; x < TI_SIZE; x++) {
                unsigned short w = has ? PROMPT_ATLAS[(v + SRC12[y]) * PI_ATLAS_W + u + SRC12[x]] : 0;
                int id = -1;

                if (w != 0) {
                    for (k = 0; k < n && col[k] != w; k++) {
                    }
                    if (k == n) {
                        col[n] = w;
                        cnt[n] = 0;
                        target[n] = n;
                        alive[n] = 1;
                        n++;
                    }
                    cnt[k]++;
                    id = k;
                }
                idx[i][y * TI_SIZE + x] = id;
            }
        }
    }
    /* 15 colours at most (entry 0 is the transparent one): fold the rarest into its nearest */
    for (live = n; live > 15; live--) {
        int rare = -1, closest = -1, best = 0x7FFFFFFF;

        for (k = 0; k < n; k++) {
            if (alive[k] && (rare < 0 || cnt[k] < cnt[rare])) {
                rare = k;
            }
        }
        for (k = 0; k < n; k++) {
            if (alive[k] && k != rare && colourDistance(col[k], col[rare]) < best) {
                best = colourDistance(col[k], col[rare]);
                closest = k;
            }
        }
        alive[rare] = 0;
        cnt[closest] += cnt[rare];
        for (k = 0; k < n; k++) {
            if (target[k] == rare) {
                target[k] = closest;
            }
        }
    }
    memset(out, 0, sizeof(*out));
    for (k = 0, j = 1; k < n; k++) {
        final[k] = 0;
        if (alive[k]) {
            out->clut[j] = col[k];
            final[k] = j++;
        }
    }
    for (i = 0; i < TI_BUTTONS; i++) {
        for (k = 0; k < TI_SIZE * TI_SIZE; k++) {
            out->px[i][k] = idx[i][k] < 0 ? 0 : (unsigned char)final[target[idx[i][k]]];
        }
    }
}

void textIconPack(const unsigned char *px, unsigned short *words) {
    int x, y;

    for (y = 0; y < TI_SIZE; y++) {
        for (x = 0; x < TI_WORDS_PER_ROW; x++) {
            const unsigned char *p = &px[y * TI_SIZE + x * 4];

            words[y * TI_WORDS_PER_ROW + x] = (unsigned short)((p[0] & 15) | (p[1] & 15) << 4 | (p[2] & 15) << 8 | (p[3] & 15) << 12);
        }
    }
}
