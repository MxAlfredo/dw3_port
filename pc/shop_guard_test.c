/*
 * Host test of the item shop's equip guard (src/menus/item_shop/equip.c, STITSHOP_canEquip): an item that is not
 * equipment (a mod stocking a key item in the armory) has no equipment record, so no partner can equip it and the
 * record is never read. No disc, no window.
 *
 * Exit 0: every check held. Exit 1: at least one failed.
 */
#include "menus/item_shop.h"

int printf(const char *fmt, ...);

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static ItemInfo items[3];
static ItemInfo *stubGet(s32 id) { return &items[id - 1]; }

ItemFuncs ITEM_FUNCS = {stubGet};
ItemShopFuncs STITSHOP_funcs;
ItemInfo ITEMS[1];
GameState GAME;
ShopList STITSHOP_shops[31];

/* equip.c's other functions reference these; the test never calls them */
s8 *pcItemEquippedRef(int id) { return 0; }
s8 *pcItemCountRef(int id) { return 0; }

int main(void) {
    static ItemData weapon;
    weapon.weapon.partners = 0x5; /* partners 0 and 2 */

    /* item 1: a key item (type 1) with a garbage record; item 2: a weapon; item 3: a type-2 item without a record */
    items[0].type = 1;
    items[0].data.record = (ItemData *)0x10;
    items[1].type = 5;
    items[1].data.record = &weapon;
    items[2].type = 5;
    items[2].data.record = NULL;

    CHECK(STITSHOP_canEquip(0, 1) == 0);
    CHECK(STITSHOP_canEquip(2, 1) == 0);
    CHECK(STITSHOP_canEquip(0, 2) == 1);
    CHECK(STITSHOP_canEquip(1, 2) == 0);
    CHECK(STITSHOP_canEquip(2, 2) == 1);
    CHECK(STITSHOP_canEquip(0, 3) == 0);
    printf(failures ? "shop_guard_test: %d FAILED\n" : "shop_guard_test: ok\n", failures);
    return failures != 0;
}
