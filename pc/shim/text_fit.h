/*
 * L4c: single-line menu fields fit their text by auto-scale.
 *
 * The retail screens put one-line fields (item names in the shop and pause-menu grids, equipment
 * rows, deck-editor rows...) in cells sized for the English strings. The es-419 names can be wider
 * than the cell and run into the next element. The port does not shorten the text: the field's
 * window is drawn with a horizontal scale = budget / measured width (vertical unchanged) when,
 * and only when, the text is wider than the cell. Retail strings fit, so nothing changes for them.
 *
 * Everything here is pure (no game, no SDL, no stdlib) so texttest drives it and the game-side
 * wrapper (text_fit.c) can include it. The budgets are measured from the screens.
 */
#ifndef TEXT_FIT_H
#define TEXT_FIT_H

#define TF_ONE 0x1000       /* 1.0 in the GTE scale the windows use (ONE) */
#define TF_MIN_SCALE 0x800  /* never squash under 50%: past that the glyphs are not letters any more */

/* the screens that hand their windows to the fit (one -D rename per object in pc/Makefile) */
enum {
    TF_NONE = 0,
    TF_ISHOP_LIST,   /* item shop: the 2-column grid of names (item_shop/item_list.c) */
    TF_FMENU_ITEMS,  /* pause menu: the 2-column item grid (field_menu/item_list.c) */
    TF_FMENU_EQUIP,  /* pause menu: the equipment panel's list rows (field_menu/equip_panel.c) */
    TF_FMENU_STATUS, /* pause menu: status screen slots and equipment (field_menu/status_screen.c) */
    TF_DECK_EDITOR,  /* deck editor: the card rows (cardgame/deck_editor/editor.c) */
    TF_DECK_SCREEN,  /* deck editor: the folder rows (cardgame/deck_editor/screen.c) */
    TF_CARD_SHOP,    /* card shop: the pack list (cardgame/card_packs/shop.c) */
    TF_ISHOP_SHOP,   /* item shop: the shop name in the title frame (item_shop/shop.c) */
    TF_FMENU_ISCREEN, /* pause menu: the item screen (field_menu/item_screen.c): no fitted field, only the capture-aid seed */
    TF_SCREEN_COUNT
};

typedef struct TfRule {
    int screen;
    int x;      /* the window's creation x (any y: a column or a list) */
    int budget; /* px from x to the next element */
    int grid;   /* 1: the left column of an item grid (DW3_TEXT_FIT_GRID widens it) */
} TfRule;

/* Measured on screen (L4c). x, budget in px of the 320x240 screen. */
static const TfRule tfRules[] = {
    {TF_ISHOP_LIST, 0x37, 105, 1},
    {TF_ISHOP_LIST, 0xBA, 105, 0},
    {TF_FMENU_ITEMS, 0x38, 105, 1},
    {TF_FMENU_ITEMS, 0xBB, 105, 0},
    {TF_FMENU_EQUIP, 0xA0, 100, 0},
    {TF_FMENU_STATUS, 0xB2, 108, 0},
    {TF_FMENU_STATUS, 0xC0, 108, 0},
    {TF_DECK_EDITOR, 0xA3, 105, 0},
    {TF_DECK_SCREEN, 0x1C, 110, 0},
    {TF_CARD_SHOP, 0xA7, 110, 0},
    {TF_CARD_SHOP, 0x1D, 118, 0},  /* the shop name: title frame 24..152 px, text from 29, 5 px of padding */
    {TF_ISHOP_SHOP, 0x1D, 118, 0},
};

/* The budget of the field a screen created at x, 0 when it has no rule. `gridOverride` > 0 (a
   capture aid) replaces the budget of the left item-grid columns. */
static inline int tfBudget(int screen, int x, int gridOverride)
{
    unsigned i;
    for (i = 0; i < sizeof(tfRules) / sizeof(tfRules[0]); i++) {
        if (tfRules[i].screen == screen && tfRules[i].x == x)
            return (gridOverride > 0 && tfRules[i].grid) ? gridOverride : tfRules[i].budget;
    }
    return 0;
}

/* The horizontal scale for a text `width` px wide in a cell of `budget` px: TF_ONE when it fits (or
   there is no budget or no text), else budget/width, never under TF_MIN_SCALE. */
static inline int tfScale(int width, int budget)
{
    long s;
    if (budget <= 0 || width <= 0 || width <= budget)
        return TF_ONE;
    s = ((long)budget * TF_ONE) / width;
    return s < TF_MIN_SCALE ? TF_MIN_SCALE : (int)s;
}

/* L4d (owner's decision after the L4c captures): the scale made the glyphs uneven, so it is OFF. It runs
   only when DW3_TEXT_FIT=scale (a capture aid for future experiments); the layout moves instead. */
static inline int tfScaleMode(const char *v)
{
    return v != 0 && v[0] == 's' && v[1] == 'c' && v[2] == 'a' && v[3] == 'l' && v[4] == 'e' && v[5] == 0;
}

/* The equipment panel (field_menu/equip_panel.c): the "X" of the rows sits at x=0x10D and the name at 0xA0. A
   name up to TF_EQUIP_FREE px wide clears it (L4c: at 101 px the text touches the X); a longer one pushes the
   X right by the excess plus TF_EQUIP_GAP (a visible space like the other rows, owner 2026-10-29), up to
   TF_EQUIP_MAX_SHIFT. Only the X moves: the count (x=0x120) already ends 3 px
   from the panel's border (x=291) and the "1" starts 14 px after the X, so the X can give up to 10 px. */
#define TF_EQUIP_NAME_X 0xA0
#define TF_EQUIP_TIMES_X 0x10D
#define TF_EQUIP_COUNT_X 0x120
#define TF_EQUIP_FREE 100
#define TF_EQUIP_MAX_SHIFT 10
#define TF_EQUIP_GAP 4

static inline int tfEquipShift(int nameWidth)
{
    int s = nameWidth - TF_EQUIP_FREE;
    if (s <= 0) {
        return 0;
    }
    s += TF_EQUIP_GAP;
    return s > TF_EQUIP_MAX_SHIFT ? TF_EQUIP_MAX_SHIFT : s;
}

/* The shop title frame (item shop frame 1, card shop frame 0x28): a left cap, tiles of the middle and a right
   cap, 136 px at (22, 18) with the text from x=29 (7 px of padding on the left, 11 on the right when the text
   is the 118 px budget). A wider name widens the frame: text + 18 px, never under the retail 136 and never
   past TF_FRAME_MAX_W (the money panel starts at x=214, the frame at 22). */
#define TF_FRAME_BASE_W 136
#define TF_FRAME_MAX_W 188
#define TF_FRAME_PAD 18

static inline int tfTitleFrameW(int textWidth)
{
    int w = textWidth + TF_FRAME_PAD;
    return w < TF_FRAME_BASE_W ? TF_FRAME_BASE_W : (w > TF_FRAME_MAX_W ? TF_FRAME_MAX_W : w);
}

/* Where the middle tiles (midW px each) of a w px frame with capW px caps go (x from the frame's left): back
   to back from the left cap, the last one pulled back against the right cap when the middle is not a multiple
   of midW (the middle is the same texture along its length, so the overlap does not show). Returns the count
   (at most `max`), 0 when the frame is narrower than two caps and one tile. */
static inline int tfFrameTiles(int w, int capW, int midW, int *xs, int max)
{
    int inner = w - 2 * capW, n = 0, x = 0;
    if (midW <= 0 || inner < midW)
        return 0;
    while (n < max && x < inner) {
        xs[n++] = capW + (x + midW <= inner ? x : inner - midW);
        x += midW;
    }
    return n;
}

#endif /* TEXT_FIT_H */
