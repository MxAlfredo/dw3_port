/* G1: the battle's element boost (src/battle/battle_mode/damage.c). Links the
   real FIGHTSTG_getElementBoost and FIGHTSTG_opposedElements and checks, for
   every boosted element, which element is weakened. The reference pairs are
   the game's own Field descriptions ("boost X, weaken Y"). */
#include <stdio.h>

#include "battle/battle_mode.h"

#define FIRE 2
#define WATER 3
#define ICE 4
#define WIND 5
#define THUNDER 6
#define METAL 7
#define DARK 8

static int failures;

#define EXPECT(cond, ...)                \
    do {                                 \
        if (!(cond)) {                   \
            failures++;                  \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                \
        }                                \
    } while (0)

int main(void) {
    /* boosted element -> the element it weakens */
    static const int weakens[][2] = {
        {FIRE, ICE}, {WATER, FIRE}, {ICE, WIND}, {WIND, WATER}, {THUNDER, METAL}, {METAL, DARK}, {DARK, THUNDER},
    };
    int i, e;

    for (i = 0; i < 7; i++) {
        int boosted = weakens[i][0];
        int weak = weakens[i][1];

        FIGHTSTG_battle.state.boost.element = boosted;
        FIGHTSTG_battle.state.boost.amount = 0x40;
        for (e = FIRE; e <= DARK; e++) {
            s32 got = FIGHTSTG_getElementBoost(1000, e);
            s32 want = e == boosted ? 1000 * 0x40 / 128 : (e == weak ? -(1000 * 0x40 / 256) : 0);

            EXPECT(got == want, "boost %d, element %d: got %d, want %d", boosted, e, (int)got, (int)want);
        }
    }
    FIGHTSTG_battle.state.boost.element = 0;
    EXPECT(FIGHTSTG_getElementBoost(1000, FIRE) == 0, "no boost must add nothing");
    printf(failures ? "battletest: %d FAILED\n" : "battletest: OK\n", failures);
    return failures != 0;
}
