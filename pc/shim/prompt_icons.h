#ifndef PC_PROMPT_ICONS_H
#define PC_PROMPT_ICONS_H

/*
 * Button prompt icons of the options menu (feature controls-config, C4): which atlas
 * cell shows a given controller button or key for a given controller family. The
 * atlas is Kenney's "Input Prompts Pixel 16x" (CC0), packed by pc/gen_prompt_atlas.py
 * into prompt_atlas_data.c as 15-bit VRAM words; see third_party/kenney-prompts/LICENSE.txt.
 * Pure lookup, no SDL window or game: pc/inputtest.c drives it. The VRAM upload and
 * the drawing are in controls_draw.c.
 *
 * Atlas: PI_COLS x PI_ROWS cells of PI_CELL x PI_CELL pixels, drawn 1:1 (14 px, the
 * height of the game's text line), in the order of the PI_* enum.
 */
#include "input_device.h"

#define PI_CELL 14
#define PI_COLS 4
#define PI_ROWS 9
#define PI_ATLAS_W (PI_COLS * PI_CELL)
#define PI_ATLAS_H (PI_ROWS * PI_CELL)
#define PI_NONE (-1)

/* Cell indices: the order of CELLS in gen_prompt_atlas.py */
enum {
    PI_XB_A, PI_XB_B, PI_XB_X, PI_XB_Y,
    PI_SW_A, PI_SW_B, PI_SW_X, PI_SW_Y,
    PI_PS_CROSS, PI_PS_CIRCLE, PI_PS_SQUARE, PI_PS_TRIANGLE,
    PI_DPAD_UP, PI_DPAD_DOWN, PI_DPAD_LEFT, PI_DPAD_RIGHT,
    PI_XB_LB, PI_XB_RB, PI_PS_L1, PI_PS_R1,
    PI_SW_L, PI_SW_R,
    PI_XB_MENU, PI_PS_OPTIONS, PI_SW_PLUS, PI_SW_MINUS,
    PI_KEY_ESC, PI_KEY_F1, PI_KEY_Q, PI_KEY_E,
    PI_KEY_UP, PI_KEY_DOWN, PI_KEY_LEFT, PI_KEY_RIGHT,
    PI_XB_VIEW, PI_PS_SELECT,
    PI_CELL_COUNT
};

/* The 15-bit pixels of the atlas (0 = transparent), row after row, and the cell count
   the generator wrote (inputtest checks it equals PI_CELL_COUNT) */
extern const unsigned short PROMPT_ATLAS[PI_ATLAS_W * PI_ATLAS_H];
extern const int PROMPT_ATLAS_CELLS;

/* The cell for an SDL_GameControllerButton on a family, PI_NONE when there is no icon
   (generic has none for start/back). SDL names buttons by position: "a" is the south
   button, which is cross on PlayStation; Nintendo pads already report the label (SDL's
   default swaps A/B there), so on Switch "a" is the button labelled A. */
int promptIconPad(int family, int sdlButton);
/* The cell for an SDL scancode (Esc, F1, Q, E, the arrows), PI_NONE for any other key */
int promptIconKey(int sdlScancode);
/* What the menu's help line names: the same action on a keyboard or on a pad */
enum { PI_ACT_TAB_PREV, PI_ACT_TAB_NEXT, PI_ACT_OK, PI_ACT_BACK, PI_ACT_CLOSE, PI_ACT_LEFT, PI_ACT_RIGHT, PI_ACT_COUNT };
/* The cell of an action on the keyboard (kb != 0) or on a pad of `family`, PI_NONE when
   it has none; *text is what to write in its place (always set, never NULL) */
int promptIconAction(int kb, int family, int act, const char **text);

/* The top left corner of a cell in the atlas; 0 on a bad cell */
int promptIconUV(int cell, int *u, int *v);

#endif
