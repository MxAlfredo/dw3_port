/*
 * L4c/L4d: single-line fields (see text_fit.h). The screens' objects are compiled with
 * -DcreateTextWindow=pcFitWin_<screen> (pc/Makefile), so every window they create comes through here.
 * - A window with a budget rule is measured every frame (DW3_TEXT_FIT_LOG logs it). The glyph scale of
 *   L4c is OFF (owner's decision: uneven glyphs); it only runs with DW3_TEXT_FIT=scale.
 * - L4d moves the layout instead: the equipment panel's "X" column shifts right by what the
 *   widest name needs, and the shop title frames (item shop and card shop, compiled with
 *   -DinitSpriteDrawer=pcFitSprite_<screen>) are drawn wider from their own caps and middle tiles.
 * The upstream sources stay untouched.
 */
#define TFIT_GAME_UNIT 1
#include "engine/game.h"
#include "text_fit.h"

/* text_data.c (the host's headers cannot be mixed with the game's here) */
extern int tdTextFitOn(void);
extern int tdTextFitGrid(void);
extern int tdTextFitSeed(void);
extern void tdTextFitLog(int screen, int x, int y, int w, int budget, int scale);

#define FIT_SLOTS 256

typedef struct FitEntry {
    TextWindow *win;
    int screen;
    int budget;
    int lastW;
    int lastScale;
    int shiftBaseX; /* > 0: the equipment panel's X column, which moves right with the widest name (L4d) */
} FitEntry;

/* the equipment panel's eight row names, so the X column can see the widest one */
#define EQUIP_ROWS 8
static TextWindow *equipNames[EQUIP_ROWS];
static int equipNameN;

/* the measured width of the item shop's and the card shop's title, for their frames (0: none yet) */
static int titleW[2];
enum { TITLE_ISHOP, TITLE_CSHOP };

static int equipWidestName(void)
{
    int i, best = 0;

    for (i = 0; i < equipNameN; i++) {
        TextWindow *n = equipNames[i];

        if (n != NULL && n->visible && n->text[0].data != NULL) {
            int w = (int)measureText(&n->text[0], n->style, n->fixedSpacing ? n->spacingX : 0);

            if (w > best)
                best = w;
        }
    }
    return best;
}

static FitEntry fitTable[FIT_SLOTS];
static int fitNext;

static FitEntry *fitFind(TextWindow *win)
{
    int i;
    for (i = 0; i < FIT_SLOTS; i++) {
        if (fitTable[i].win == win)
            return &fitTable[i];
    }
    return NULL;
}

static void fitUpdate(TextWindow *win)
{
    FitEntry *e = fitFind(win);

    if (e != NULL && e->shiftBaseX > 0) {
        int x = e->shiftBaseX + tfEquipShift(equipWidestName());

        if (win->x != x)
            win->setPos(win, x, win->y);
    } else if (e != NULL && win->text[0].data != NULL) {
        int w = (int)measureText(&win->text[0], win->style, win->fixedSpacing ? win->spacingX : 0);
        int scale = tdTextFitOn() ? tfScale(w, e->budget) : TF_ONE;

        if (e->screen == TF_ISHOP_SHOP && win->x == 0x1D)
            titleW[TITLE_ISHOP] = w;
        else if (e->screen == TF_CARD_SHOP && win->x == 0x1D)
            titleW[TITLE_CSHOP] = w;

        if (w != e->lastW || scale != e->lastScale) {
            tdTextFitLog(e->screen, win->x, win->y, w, e->budget, scale);
            e->lastW = w;
            e->lastScale = scale;
        }
        if (tdTextFitOn()) {
            win->setPivot(win, win->x, win->y); /* the pivot is in screen px: it follows setPos */
            win->setScale(win, scale, TF_ONE);  /* the draw drops the matrix by itself at 1.0 x 1.0 */
        }
    }
    updateTextWindow(win);
}

#undef createTextWindow /* never defined here; the real function is the one the game links */

/* Opening a pause-menu item category with items reaches getItemCategory(0): item_screen.c:380 draws the
   category icon one frame before the list sets screen->item, and items.c:28 then reads NULL->type. That
   read is harmless on the PS1 (low RAM) and a crash here (owner, 2026-10-29), so the port makes the lookup
   NULL-safe for every run, installed once at boot (pcItemGuardInstall). Category 0 for that single frame. */
static s32 (*realGetCategory)(s32 id);

static s32 safeGetCategory(s32 id)
{
    return id > 0 ? realGetCategory(id) : 0;
}

void pcItemGuardInstall(void)
{
    if (realGetCategory == NULL) {
        realGetCategory = ITEM_FUNCS.getCategory;
        ITEM_FUNCS.getCategory = safeGetCategory;
    }
}

/* DW3_TEXT_FIT_SEED=1 (a capture aid). The mode: harness starts the pause menu with an empty bag, so
   some of the longest-named items are put in it when the item screen creates its windows. */

static void fitSeedBag(void)
{
    static const short ids[] = {74, 88, 92, 106, 146, 157, 167, 190, 199, 200, 201, 202, 203, 204, 205, 206, 215, 226,
                                236, 279, 294, 295, 332, 348, 357};
    unsigned i;

    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        PC_ITEM_COUNT(ids[i]) = 1;
}

static TextWindow *fitCreate(int screen, s16 layer, s16 style, s16 x, s16 y)
{
    TextWindow *win = createTextWindow(layer, style, x, y);
    int budget = (win != NULL) ? tfBudget(screen, x, tdTextFitGrid()) : 0;

    if (win != NULL && screen == TF_FMENU_EQUIP) {
        if (x == TF_EQUIP_NAME_X) {
            if (y == 0x4B)
                equipNameN = 0; /* the first row of a new panel: the old windows are gone */
            if (equipNameN < EQUIP_ROWS)
                equipNames[equipNameN++] = win;
        } else if (x == TF_EQUIP_TIMES_X) { /* the count column stays: it ends 3 px from the panel border */
            FitEntry *e = fitFind(win);

            if (e == NULL) {
                e = &fitTable[fitNext];
                fitNext = (fitNext + 1) % FIT_SLOTS;
            }
            e->win = win;
            e->screen = screen;
            e->budget = 0;
            e->lastW = -1;
            e->lastScale = TF_ONE;
            e->shiftBaseX = x;
            win->update = fitUpdate;
            return win;
        }
    }
    if ((screen == TF_ISHOP_SHOP || screen == TF_CARD_SHOP) && x == 0x1D)
        titleW[screen == TF_ISHOP_SHOP ? TITLE_ISHOP : TITLE_CSHOP] = 0;
    if (budget > 0) {
        FitEntry *e = fitFind(win);

        if (e == NULL) {
            e = &fitTable[fitNext];
            fitNext = (fitNext + 1) % FIT_SLOTS;
        }
        e->win = win;
        e->screen = screen;
        e->budget = budget;
        e->lastW = -1;
        e->lastScale = TF_ONE;
        e->shiftBaseX = 0;
        win->update = fitUpdate;
    } else if ((screen == TF_FMENU_ISCREEN || screen == TF_FMENU_STATUS) && win != NULL && tdTextFitSeed()) {
        fitSeedBag();
    } else if (win != NULL) {
        FitEntry *e = fitFind(win); /* a reused task slot of a window that was fitted */

        if (e != NULL)
            e->win = NULL;
    }
    return win;
}

#define FIT_WRAP(name, id)                                                       \
    TextWindow *pcFitWin_##name(s16 layer, s16 style, s16 x, s16 y)              \
    {                                                                            \
        return fitCreate(id, layer, style, x, y);                                \
    }

FIT_WRAP(ishop_list, TF_ISHOP_LIST)
FIT_WRAP(fmenu_items, TF_FMENU_ITEMS)
FIT_WRAP(fmenu_equip, TF_FMENU_EQUIP)
FIT_WRAP(fmenu_status, TF_FMENU_STATUS)
FIT_WRAP(deck_editor, TF_DECK_EDITOR)
FIT_WRAP(deck_screen, TF_DECK_SCREEN)
FIT_WRAP(card_shop, TF_CARD_SHOP)
FIT_WRAP(ishop_shop, TF_ISHOP_SHOP)
FIT_WRAP(fmenu_iscreen, TF_FMENU_ISCREEN)

/* ---- L4d: the shop title frames, drawn wider for a long name ----
   The frame is a frame of the shop's sprite sheet: a left cap, two middle tiles and a right cap, all
   rectangles of the texture. The game's drawer is replaced (-DinitSpriteDrawer) by one whose draw swaps
   that one frame for a rebuilt one with the same parts (caps and tile) and as many tiles as the name
   needs; the width is the retail 136 up to the retail name budget, so the retail frames are drawn by the
   game's own draw call, untouched. Anything unexpected in the sheet falls back to that call too. */
#undef initSpriteDrawer

#define SYNTH_BYTES 512
#define SYNTH_MAX_TILES 8
#define SYNTH_PARTS 3

static union {
    s32 align;
    unsigned char b[SYNTH_BYTES];
} synth;

/* Draws sheet frame `frame` at (x, y) as a title frame `want` px wide; 0 when it cannot (the caller then
   draws the original) */
static int fitDrawWide(s32 *sheet, s32 frame, s32 x, s32 y, int want)
{
    SpritePart *parts = (SpritePart *)((unsigned char *)sheet + sheet[0]);
    unsigned char *ids = (unsigned char *)sheet + sheet[1];
    s16 *p;
    s16 *q;
    SpritePart *left = NULL, *mid = NULL, *right = NULL;
    s16 leftY = 0, midY = 0, rightY = 0, minX = 0x7FFF, maxX = -1;
    s16 midIdx = -1;
    int count, i, k, n, tiles[SYNTH_MAX_TILES];
    s16 *fp;
    unsigned char *base = synth.b;
    SpritePart *np = (SpritePart *)(base + 12);

    for (i = 0; ids[i] != frame; i++) {
        if (i > 255)
            return 0;
    }
    p = (s16 *)((unsigned char *)sheet + sheet[i + 2]);
    count = p[0];
    if (count < 3 || count > 6)
        return 0;
    for (k = 0; k < count; k++) { /* the end parts by x; anything else must be the one middle tile */
        s16 idx = p[3 + k * 3], px = p[4 + k * 3];

        if (px < minX) {
            minX = px;
            left = &parts[idx];
            leftY = p[5 + k * 3];
        }
        if (px > maxX) {
            maxX = px;
            right = &parts[idx];
            rightY = p[5 + k * 3];
        }
    }
    for (k = 0; k < count; k++) {
        s16 idx = p[3 + k * 3], px = p[4 + k * 3];

        if (px != minX && px != maxX) {
            if (midIdx >= 0 && midIdx != idx)
                return 0;
            midIdx = idx;
            mid = &parts[idx];
            midY = p[5 + k * 3];
        }
    }
    if (left == NULL || right == NULL || mid == NULL || minX != 0 || mid->w <= 0)
        return 0;
    n = tfFrameTiles(want, left->w, mid->w, tiles, SYNTH_MAX_TILES);
    if (n == 0)
        return 0;

    /* a sheet of one frame: header (parts, ids, frame offsets), three parts, the id, the frame */
    ((s32 *)base)[0] = 12;
    ((s32 *)base)[1] = 12 + SYNTH_PARTS * (int)sizeof(SpritePart);
    ((s32 *)base)[2] = ((s32 *)base)[1] + 4;
    np[0] = *left;
    np[1] = *mid;
    np[2] = *right;
    base[((s32 *)base)[1]] = (unsigned char)frame;
    base[((s32 *)base)[1] + 1] = 0xFF;
    fp = (s16 *)(base + ((s32 *)base)[2]);
    q = fp;
    *q++ = (s16)(n + 2);
    *q++ = p[1];
    *q++ = p[2];
    *q++ = 0; *q++ = 0; *q++ = leftY;
    for (k = 0; k < n; k++) {
        *q++ = 1; *q++ = (s16)tiles[k]; *q++ = midY;
    }
    *q++ = 2; *q++ = (s16)(want - right->w); *q++ = rightY;
    spriteDrawerDraw((s32 *)base, frame, x, y);
    return 1;
}

static void fitDrawTitle(s32 *sheet, s32 frame, s32 x, s32 y, s32 wantFrame, s32 wantX, int which)
{
    int w = (frame == wantFrame && x == wantX) ? tfTitleFrameW(titleW[which]) : 0;

    if (w <= TF_FRAME_BASE_W || !fitDrawWide(sheet, frame, x, y, w))
        spriteDrawerDraw(sheet, frame, x, y);
}

static void fitDrawIshop(s32 *sheet, s32 frame, s32 x, s32 y)
{
    fitDrawTitle(sheet, frame, x, y, 1, 0x16, TITLE_ISHOP);
}

static void fitDrawCshop(s32 *sheet, s32 frame, s32 x, s32 y)
{
    fitDrawTitle(sheet, frame, x, y, 0x28, 0x16, TITLE_CSHOP);
}

void pcFitSprite_ishop(SpriteDrawer *obj)
{
    initSpriteDrawer(obj);
    obj->draw = fitDrawIshop;
}

void pcFitSprite_cshop(SpriteDrawer *obj)
{
    initSpriteDrawer(obj);
    obj->draw = fitDrawCshop;
}
