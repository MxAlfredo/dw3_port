#define _GNU_SOURCE 1
/*
 * Host test of the item base accessor (pc/shim/ext_state.c, pcItemBase / PC_ITEM_BASE): an original id is its own
 * base, a mod item resolves to the original item it is "based on" (following a chain, bounded against cycles), and
 * anything out of range or with no store resolves to itself. Stubbed data, no disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include <stdio.h>

#include "engine/game_state.h"
#include "engine/item_access.h"
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
    /* store rows: 0x193 based on 43, 0x194 based on 0x55, 0x195 based on 0x193 (chain), 0x196 on itself (cycle) */
    static const int bases[] = {43, 0x55, 0x193, 0x196};

    CHECK(PC_ITEM_BASE(0) == 0);
    CHECK(PC_ITEM_BASE(43) == 43);
    CHECK(PC_ITEM_BASE(0x5A) == 0x5A);
    CHECK(PC_ITEM_BASE(0x192) == 0x192);
    CHECK(PC_ITEM_BASE(0x193) == 0x193); /* no store yet */
    CHECK(PC_ITEM_BASE(-1) == -1);

    pcExtStateSetItemBases(bases, 4);
    CHECK(PC_ITEM_BASE(43) == 43);
    CHECK(PC_ITEM_BASE(0x193) == 43);
    CHECK(PC_ITEM_BASE(0x194) == 0x55);
    CHECK(PC_ITEM_BASE(0x195) == 43);
    CHECK(PC_ITEM_BASE(0x196) == 0x196); /* cycle: itself, no hang */
    CHECK(PC_ITEM_BASE(0x197) == 0x197); /* past the store */
    CHECK(PC_ITEM_BASE(-5) == -5);

    pcExtStateSetItemBases(NULL, 0);
    CHECK(PC_ITEM_BASE(0x193) == 0x193);
    if (sFail) return 1;
    printf("itembasetest: ok\n");
    return 0;
}
