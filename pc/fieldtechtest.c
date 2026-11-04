#define _GNU_SOURCE 1
/*
 * Host test of the field-healing accessor (pc/shim/ext_state.c, pcTechIsFieldHeal / PC_TECH_IS_FIELD_HEAL):
 * the original ids 0xB8..0xBC are field techniques, others are not, and a mod technique counts when its base
 * (or the base of its base) is one of them. Stubbed tech data, no disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <stdio.h>

#include "engine/game_state.h"
#include "shim/ext_state.h"

GameState GAME;
TechData TECHS_CARD[443];
DigimonData DIGIMON_DATA_CARD[55];
/* what item_ext.c needs from the game's items.c; unused here */
ItemInfo ITEMS[402];
u8 ITEM_TYPE_CATEGORIES[32];
ItemFuncs ITEM_FUNCS;

static int sFail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); sFail = 1; } } while (0)

int main(void) {
    /* store rows: 444 based on 0xB8, 445 based on 9, 446 based on 444 (chain), 447 based on 447 (cycle) */
    static const int bases[] = {0xB8, 9, 444, 447};

    CHECK(!PC_TECH_IS_FIELD_HEAL(0xB7));
    CHECK(PC_TECH_IS_FIELD_HEAL(0xB8));
    CHECK(PC_TECH_IS_FIELD_HEAL(0xBC));
    CHECK(!PC_TECH_IS_FIELD_HEAL(0xBD));
    CHECK(!PC_TECH_IS_FIELD_HEAL(444)); /* no store yet */

    pcExtStateSetTechBases(bases, 4);
    CHECK(PC_TECH_IS_FIELD_HEAL(444));
    CHECK(!PC_TECH_IS_FIELD_HEAL(445));
    CHECK(PC_TECH_IS_FIELD_HEAL(446));
    CHECK(!PC_TECH_IS_FIELD_HEAL(447));
    CHECK(!PC_TECH_IS_FIELD_HEAL(448)); /* past the store */
    CHECK(PC_TECH_IS_FIELD_HEAL(0xBA));

    pcExtStateSetTechBases(NULL, 0);
    CHECK(!PC_TECH_IS_FIELD_HEAL(444));
    if (sFail) return 1;
    printf("fieldtechtest: ok\n");
    return 0;
}
