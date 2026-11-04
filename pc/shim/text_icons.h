#ifndef PC_TEXT_ICONS_H
#define PC_TEXT_ICONS_H

/*
 * The game's own button icons inside its text (feature controls-config, C5).
 *
 * The game draws a button icon as a font glyph: the text bytes `01 nn` select
 * style->icons[nn-1] (processTextChar, src/engine/text/text_window.c), a 12x12
 * sprite from the font sheet in VRAM. Evidence (docs/design/options-menu.md, C5): in
 * the EU font table FONT_ICONS_1, the four glyphs whose CLUT row is (368, 224) are the PS1
 * face buttons, numbered here as the text bytes see them (1-based):
 *
 *   27 cross, 47 circle, 50 square, 52 triangle
 *
 * Nothing else uses CLUT rows 224..231 (the glyph and icon tables of the
 * three styles were checked), so the port swaps those four cells host-side without editing src/:
 * it rewrites their 4-bit texels and that CLUT (16 colours, 8 identical rows: the
 * text palette adds 0..7 to the row) in the CPU copy of VRAM, every frame it finds the
 * font as the game loaded it. Styles 2 and 3 have no face-button icons. L1/R1,
 * start/select and the d-pad have no icon in the font, so the text spells them out.
 *
 * Which picture goes on which logical button follows the player: the physical binding
 * of that PS1 button on the device in use (input_cfg), drawn as that device family's
 * icon; a keyboard shows key caps where the atlas has one, else the game's own icon.
 *
 * This unit is the pure part (no window, no VRAM): pc/inputtest.c drives it. The VRAM
 * side is text_icons_draw.c.
 */
#include "prompt_icons.h"

#define TI_BUTTONS 4 /* cross, circle, square, triangle */
#define TI_SIZE 12   /* the game's icon cells are 12 x 12 pixels */

/* Which logical button (INPUT_BTN_*) the TI_BUTTONS-th icon stands for, in the order
   cross, circle, square, triangle; the text byte `01 nn` that draws it is textIconNumber */
int textIconButton(int slot);
int textIconNumber(int slot);

/* What to draw for each of the four icons.
     device    INPUT_DEV_KEYBOARD or INPUT_DEV_PAD (the last one used)
     setting   INPUT_ICONS_* (Auto follows the device; a forced family always shows that pad's icons)
     detected  the family of the last controller used
     cfg       the live bindings
   cells[i] is the atlas cell for icon i. Returns 1 when every icon is the game's own
   (the cell of its own PS1 button), so the original texels are to be restored. */
int textIconPlan(int device, int setting, int detected, const InputCfg *cfg, int cells[TI_BUTTONS]);

/* One 12 x 12 icon (palette indices) and the shared 16-colour CLUT, 15-bit words, entry 0
   transparent, built from four atlas cells by textIconBuild: the 14 x 14 cells are
   resampled to 12 x 12 (dropping rows/columns 3 and 10) and the colours of the four
   merged down to 15 by dropping the rarest into their nearest */
typedef struct TextIconSet {
    unsigned short clut[16];
    unsigned char px[TI_BUTTONS][TI_SIZE * TI_SIZE];
} TextIconSet;
void textIconBuild(const int cells[TI_BUTTONS], TextIconSet *out);

/* 4 bits per pixel, as VRAM keeps a 4-bit texture: 3 words per row, first pixel in the low nibble */
#define TI_WORDS_PER_ROW (TI_SIZE / 4)
void textIconPack(const unsigned char *px, unsigned short *words /* TI_WORDS_PER_ROW * TI_SIZE */);

/* Where the cells and the CLUT are in VRAM (the game's font page column is x = 320): the
   texture page of font page `page` is x = 320 + 64 * page, a pixel u is word u / 4 */
typedef struct TextIconSite {
    int page, u, v;      /* the glyph's page, u (a multiple of 4) and v */
} TextIconSite;
extern const TextIconSite TEXT_ICON_SITES[TI_BUTTONS];
#define TI_FONT_X 320
#define TI_CLUT_X 368 /* TI_FONT_X + the glyphs' clutX (48) */
#define TI_CLUT_Y 224
#define TI_CLUT_ROWS 8


/* text_icons_draw.c (the drawing world): once per frame before the layers are drawn, brings the
   font's face-button cells to the plan; textIconsNoteSprite is told of each SPRT's CLUT while
   the frame's tables are walked (a log line when the count of face-button icons drawn changes) */
void textIconsFrame(void);
void textIconsNoteSprite(unsigned short clut);

#endif
