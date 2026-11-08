/*
 * A partner's own 2D art on screen (Betamon pack R4): the PNG a mod brings (partner_art.c) lives in the port's VRAM band
 * (sprite_data.h: x 0-319, y 256-495, empty while a menu overlay runs, measured on the player-name page) as one slot of the cells' texels and
 * one palette, goes up when a screen that shows partners starts (pcPartnerArtsUpload), and is drawn through a one-frame sheet per cell
 * (pcPartnerArtDraw) with the drawer pointed at the band for that one draw. The original eight keep their own frames, untouched.
 */
#include "engine/game.h"
#include <libgs.h>
#include <libetc.h>
#include <libsnd.h>

#include "partner_art.h"
#include "port_log.h"
#include "sprite_data.h"
#include "text_data.h"

/* engine/game.h redeclares part of libc with the PlayStation's types, so stdlib.h cannot come with it */
extern void *calloc(unsigned long n, unsigned long size);
extern void free(void *p);

#define ART_OFFSET_X 2 /* where the frame's part sits against the position the screen draws at: the (2, 2) most of the original's portraits have */
#define ART_OFFSET_Y 2

typedef struct Art {
    PaArt art;
    SprSlot slot;
    PaSheet sheet[PA_CELLS_MAX];
    int hasIcon; /* the memory card's icon (pcPartnerIconLoad), a slot of its own */
    PaArt icon;
    SprSlot iconSlot;
    PaSheet iconSheet;
} Art;

static Art *sArt[PC_PARTNER_LIMIT];
static SprVram sVram;

void pcPartnerArtsReset(void) {
    int i;

    for (i = 0; i < PC_PARTNER_LIMIT; i++) {
        if (sArt[i] != NULL) {
            paFree(&sArt[i]->art);
            paFree(&sArt[i]->icon);
            free(sArt[i]);
            sArt[i] = NULL;
        }
    }
    sprVramReset(&sVram);
}

int pcPartnerArtLoad(int partner, const char *file, const char *name) {
    unsigned char *data = NULL;
    size_t len = 0;
    char why[200];
    Art *a;
    int c;

    int fresh = 0, reduced = 0;

    if (partner < PARTNER_COUNT || partner >= PC_PARTNER_LIMIT || (sArt[partner] != NULL && sArt[partner]->art.cells > 0)) {
        return -1;
    }
    if (tdReadFile(file, &data, &len) != 0) {
        plogLine("partners", "art %s of %s rejected: cannot read it; the base's art stands in\n", file, name);
        return -1;
    }
    a = sArt[partner];
    if (a == NULL) {
        a = (Art *)calloc(1, sizeof *a);
        fresh = 1;
    }
    if (a == NULL || paFromPngFit(data, len, 0, &a->art, &reduced, why, sizeof why) != 0) {
        plogLine("partners", "art %s of %s rejected: %s; the base's art stands in\n", file, name, a != NULL ? why : "out of memory");
        free(data);
        if (fresh) free(a);
        return -1;
    }
    free(data);
    if (reduced) plogLine("partners", "art %s of %s: %d colours reduced to 16 (a 4-bit palette holds 16)\n", file, name, reduced);
    if (sprVramAlloc(&sVram, PA_CELL * a->art.cells, PA_CELL, &a->slot) != 0) {
        plogLine("partners", "art %s of %s rejected: the port's VRAM band has no room left; the base's art stands in\n", file, name);
        paFree(&a->art);
        if (fresh) free(a);
        return -1;
    }
    for (c = 0; c < a->art.cells; c++) {
        paSheetBuild(&a->sheet[c], c, a->slot.u, a->slot.v, a->slot.clutX - a->slot.x, a->slot.clutY - a->slot.y, ART_OFFSET_X, ART_OFFSET_Y);
    }
    sArt[partner] = a;
    PLOG("partners", PLOG_MODS, "art %s of %s: %d cell(s) at VRAM (%d, %d), palette (%d, %d)\n", file, name, a->art.cells, a->slot.imageX, a->slot.imageY,
         a->slot.clutX, a->slot.clutY);
    return 0;
}

/* Reads the PNG `file` as the memory card icon of new partner `partner` (a 44x28 image, partner_art.h). The icon belongs to the partner's Art record, which a
   partner that has an icon but no portrait gets with no cells (pcPartnerArtSteps 0: the base's portrait stands in); 0 on success. */
int pcPartnerIconLoad(int partner, const char *file, const char *name) {
    unsigned char *data = NULL;
    size_t len = 0;
    char why[200];
    Art *a;
    int fresh = 0, reduced = 0;

    if (partner < PARTNER_COUNT || partner >= PC_PARTNER_LIMIT || (sArt[partner] != NULL && sArt[partner]->hasIcon)) {
        return -1;
    }
    if (tdReadFile(file, &data, &len) != 0) {
        plogLine("partners", "icon %s of %s rejected: cannot read it; the base's icon stands in\n", file, name);
        return -1;
    }
    a = sArt[partner];
    if (a == NULL) {
        a = (Art *)calloc(1, sizeof *a);
        fresh = 1;
    }
    if (a == NULL || paFromPngFit(data, len, 1, &a->icon, &reduced, why, sizeof why) != 0) {
        plogLine("partners", "icon %s of %s rejected: %s; the base's icon stands in\n", file, name, a != NULL ? why : "out of memory");
        free(data);
        if (fresh) free(a);
        return -1;
    }
    free(data);
    if (reduced) plogLine("partners", "icon %s of %s: %d colours reduced to 16 (a 4-bit palette holds 16)\n", file, name, reduced);
    if (sprVramAlloc(&sVram, PA_ICON_W, PA_ICON_H, &a->iconSlot) != 0) {
        plogLine("partners", "icon %s of %s rejected: the port's VRAM band has no room left; the base's icon stands in\n", file, name);
        paFree(&a->icon);
        if (fresh) free(a);
        return -1;
    }
    paSheetBuildSize(&a->iconSheet, 0, PA_ICON_W, PA_ICON_H, a->iconSlot.u, a->iconSlot.v, a->iconSlot.clutX - a->iconSlot.x, a->iconSlot.clutY - a->iconSlot.y, 0, 0);
    a->hasIcon = 1;
    sArt[partner] = a;
    PLOG("partners", PLOG_MODS, "icon %s of %s: at VRAM (%d, %d), palette (%d, %d)\n", file, name, a->iconSlot.imageX, a->iconSlot.imageY, a->iconSlot.clutX,
         a->iconSlot.clutY);
    return 0;
}

/* Sends every art to the band: the start of each screen that shows partners (the band holds nothing else there) */
void pcPartnerArtsUpload(void) {
    int i;

    PLOG("partners", PLOG_MODS, "partner arts: upload\n");
    for (i = PARTNER_COUNT; i < PC_PARTNER_LIMIT; i++) {
        Art *a = sArt[i];
        RECT r;

        if (a == NULL) continue;
        if (a->art.cells > 0) {
            r.x = (short)a->slot.clutX;
            r.y = (short)a->slot.clutY;
            r.w = 16;
            r.h = 1;
            LoadImage(&r, (u_long *)a->art.pal);
            r.x = (short)a->slot.imageX;
            r.y = (short)a->slot.imageY;
            r.w = (short)(a->art.cells * PA_CELL_HW);
            r.h = PA_CELL;
            LoadImage(&r, (u_long *)a->art.img);
        }
        if (a->hasIcon) {
            r.x = (short)a->iconSlot.clutX;
            r.y = (short)a->iconSlot.clutY;
            r.w = 16;
            r.h = 1;
            LoadImage(&r, (u_long *)a->icon.pal);
            r.x = (short)a->iconSlot.imageX;
            r.y = (short)a->iconSlot.imageY;
            r.w = (short)a->icon.rowHw;
            r.h = (short)a->icon.h;
            LoadImage(&r, (u_long *)a->icon.img);
        }
    }
}

int pcPartnerArtSteps(int partner) {
    return partner >= 0 && partner < PC_PARTNER_LIMIT && sArt[partner] != NULL ? paSteps(sArt[partner]->art.cells) : 0; /* an icon-only record has no cells: 0 */
}

int pcPartnerArtDraw(SpriteDrawer *sprite, int partner, int step, s32 x, s32 y) {
    Art *a = partner >= 0 && partner < PC_PARTNER_LIMIT ? sArt[partner] : NULL;
    s32 tx, ty;

    if (a == NULL || a->art.cells == 0) return 0;
    tx = sprite->tpageX;
    ty = sprite->tpageY;
    sprite->setTexture(a->slot.x, a->slot.y);
    sprite->draw((s32 *)&a->sheet[paCellAt(a->art.cells, step)], 0, x, y);
    sprite->setTexture(tx, ty);
    return 1;
}

int pcPartnerStepNext(int partner, int step) {
    int n = pcPartnerArtSteps(partner);

    return n != 0 ? (step + 1 >= n ? 0 : step + 1) : pcPartnerAnimNext(partner, step);
}

void pcPartnerDraw(SpriteDrawer *sprite, void *sheet, int partner, int step, int x, int y) {
    if (!pcPartnerArtDraw(sprite, partner, step, x, y)) {
        sprite->draw(sheet, pcPartnerAnim(partner)->frames[step], x, y);
    }
}

/* The first part's offset of frame `frame` of a sprite sheet (the layout spriteDrawerDraw reads): where the original icon sits against the position it is
   drawn at; 0, 0 when the frame is not found */
static void firstPartOffset(const s32 *sheet, int frame, int *x, int *y) {
    const u8 *ids = (const u8 *)sheet + sheet[1];
    const s16 *list;
    int i;

    *x = *y = 0;
    for (i = 0; i < 256; i++) {
        if (ids[i] == frame) {
            list = (const s16 *)((const u8 *)sheet + sheet[i + 2]) + 3; /* parts, CLUT row, opacity, then each part's index, x, y */
            *x = list[1];
            *y = list[2];
            return;
        }
    }
}

int pcPartnerIconDraw(SpriteDrawer *sprite, void *sheet, int frame, int partner, int x, int y) {
    Art *a = partner >= 0 && partner < PC_PARTNER_LIMIT ? sArt[partner] : NULL;
    s32 tx, ty;
    int ox, oy;

    if (a == NULL || !a->hasIcon) return 0;
    firstPartOffset((const s32 *)sheet, frame, &ox, &oy);
    a->iconSheet.frame[4] = (s16)ox;
    a->iconSheet.frame[5] = (s16)oy;
    tx = sprite->tpageX;
    ty = sprite->tpageY;
    sprite->setTexture(a->iconSlot.x, a->iconSlot.y);
    sprite->draw((s32 *)&a->iconSheet, 0, x, y);
    sprite->setTexture(tx, ty);
    return 1;
}
