#ifndef PC_CONTROLS_SCREEN_H
#define PC_CONTROLS_SCREEN_H

/*
 * The in-game options menu (feature controls-config, C3 and O1): model and input.
 *
 * Select on any pad or F1 opens and closes it. While it is open the game keeps
 * running underneath (it is not frozen) but its pad word is held neutral; the
 * menu reads the keyboard and the controller from SDL directly, through an
 * event watch that turns events into navigation actions. This unit has no game
 * or drawing dependency, so pc/inputtest.c can drive it with synthetic
 * actions; the drawing is in controls_draw.c. The design is in
 * docs/design/options-menu.md.
 *
 * Tabs: Controls, Video, Display, Language, Audio. L1/R1, Q/E or
 * PageUp/PageDown switch tab; Up/Down move over the tab's rows; Left/Right
 * change the focused row's value. A tab without rows is a "Coming soon"
 * placeholder.
 *
 * Rows have a kind: binding (Controls only), action, toggle, cycle and slider.
 * The Controls tab is fixed: one binding row per INPUT_EXPOSED button, then
 * "Reset to defaults" and "Back"; there Left/Right switch the device
 * (Keyboard / Controller) and Confirm on a binding starts a press-to-bind
 * capture (inputCapBegin). The other tabs are filled with widget rows
 * (csWidgetAdd), each with a label, a description and an apply callback.
 */
#include "input_cfg.h"

/* Navigation actions (what an SDL event is turned into) */
enum {
    CS_ACT_NONE = 0, CS_ACT_UP, CS_ACT_DOWN, CS_ACT_LEFT, CS_ACT_RIGHT, CS_ACT_CONFIRM, CS_ACT_CANCEL,
    CS_ACT_TOGGLE, CS_ACT_TAB_PREV, CS_ACT_TAB_NEXT,
    CS_ACT_ROW_PREV, CS_ACT_ROW_NEXT /* a touch swipe: one row up / down, stopping at the ends instead of wrapping */
};

/* Touch actions (feature android T7c): an action number with a payload, so the queue between the event watch and the
   main thread stays a plain list of ints. A tap lands on a row (0 .. 255) in a zone of it; the focused row is
   activated, any other is focused first (a tap on the value activates at once). */
enum { CS_TAP_LABEL = 0, CS_TAP_VALUE, CS_TAP_DEC, CS_TAP_INC }; /* DEC / INC: the halves of a slider's bar */
#define CS_ACT_TAP_FLAG 0x1000
#define CS_ACT_TAB_FLAG 0x2000
#define CS_ACT_ROW_TAP(row, zone) (CS_ACT_TAP_FLAG | ((zone) << 8) | ((row) & 0xFF))
#define CS_ACT_TAB_TO(tab) (CS_ACT_TAB_FLAG | ((tab) & 0xFF))

enum { CS_TAB_CONTROLS = 0, CS_TAB_VIDEO, CS_TAB_DISPLAY, CS_TAB_LANGUAGE, CS_TAB_AUDIO, CS_TAB_ENHANCEMENTS, CS_TAB_SYSTEM, CS_TAB_MODS, CS_TAB_COUNT };

enum { CS_ROW_BINDING = 0, CS_ROW_ACTION, CS_ROW_TOGGLE, CS_ROW_CYCLE, CS_ROW_SLIDER };

/* The Controls tab: bindings, then these two action rows */
#define CS_CTL_ROW_RESET INPUT_EXPOSED_BUTTONS
#define CS_CTL_ROW_BACK  (INPUT_EXPOSED_BUTTONS + 1)

#define CS_MAX_WIDGETS 14 /* per tab: the most rows the panel fits */
#define CS_MODS_ROWS 66   /* the Mods tab alone holds more rows than fit: it scrolls (csTabWindowTop) */
#define CS_MAX_HIDDEN 4   /* per tab: rows that are saved but never listed (CsWidget.hidden) */

/* A widget row (not for the Controls tab). Strings are not copied: keep them alive.
   toggle: value 0/1. cycle: value 0 .. count-1, names[value] is shown.
   slider: value min .. max in steps of `step` (>= 1). action: runs apply(0).
   apply(value) is called after every change (and when an action runs). A row with a
   `key` is read from and written to the settings file by itself: the value found
   in the file is applied when the row is added, and every change saves the file. */
typedef struct CsWidget {
    int kind;
    const char *label;
    const char *desc;
    const char *key;     /* the settings file key, in the section named like the tab ([video]...);
                            NULL = not saved. Lower case letters, digits and '_' */
    const char *comment; /* the line above the key in the file (NULL: desc, else the label) */
    const char *const *names;
    int count;
    int min, max, step;
    int value;
    void (*apply)(int value);
    int (*read)(void);   /* optional: the live value, asked every time the row is read or drawn, for a
                            setting that something else can change (a PsyCross hotkey, the window
                            manager); a result outside the row's range keeps the stored value */
    const char *obsolete; /* a cycle's retired file value: logged, the row keeps its default and the
                            file is rewritten without it (NULL: none) */
    /* A cycle's file values when they differ from the shown names (NULL: the names), one per name */
    const char *const *tokens;
    /* A cycle's file value that is not offered (right now) and stands for another row value: logged, the
       row takes aliasValue and the file is rewritten (NULL: none) */
    const char *alias;
    int aliasValue;
    /* A toggle that is saved in the file but has no row (an unreleased switch): it does not count as a
       row, is not drawn, and is read with csWidgetValueByKey */
    int hidden;
    /* A hidden key an older build wrote and nothing reads now: the file's entry is accepted without a
       word and the file is rewritten without it (needs hidden) */
    int retired;
    /* The settings file section (lower case) when it is not the tab's name: a row moved to another tab
       keeps its section and key, so older files still apply (NULL: the tab's name) */
    const char *section;
    /* An action's value text (NULL: none), asked every time the row is read or drawn: "Press again to
       confirm" while it waits for its second Confirm, "" when idle */
    const char *(*text)(void);
    /* Rows that share their callbacks (the Mods tab's one row per mod): `id` is passed to these, which win over apply and
       text. applyId runs on Confirm of an action row; textId is the action's value text; stepId runs on Left (-1) or
       Right (+1) of an action row, which otherwise ignores them. */
    int id;
    void (*applyId)(int id, int value);
    const char *(*textId)(int id);
    void (*stepId)(int id, int dir);
} CsWidget;

/* ---- pure model ---- */

/* Rows of a tab; 0 for a placeholder tab */
int csTabRowCount(int tab);
int csTabPlaceholder(int tab); /* 1 when the tab has no rows ("Coming soon") */
const char *csTabName(int tab);

/* The row's kind (-1 out of range), and for a binding row its INPUT_BTN_* slot (else -1) */
int csRowKind(int tab, int row);
int csRowSlot(int row); /* Controls tab only */

/* First row drawn of a tab: 0 for every tab but Mods, and for it while its rows fit; else a window of CS_MAX_WIDGETS rows that follows the cursor */
int csTabWindowTop(int tab);

/* Cursor movement with wrap-around over `count` rows */
int csWrap(int cursor, int delta, int count);

/* Texts, written at most cap bytes (NUL included); return the length.
   Label: the row's name. Desc: a one-line help for the focused row ("" if none).
   Value: a binding row's key/button on `device` ("Left Shift", "leftshoulder", "NONE"),
   "On"/"Off", the cycle's name or the slider's number; "" for an action. */
int csRowLabel(int tab, int row, char *buf, int cap);
int csRowDesc(int tab, int row, char *buf, int cap);
int csRowValue(int tab, int device, int row, char *buf, int cap);
/* A slider's position 0 .. 1000 (-1 for any other row) */
int csRowFraction(int tab, int row);

/* Installs the settings file hooks for the widget sections; call before inputCfgInit */
void csSettingsInstall(void);
/* Logs (once) the file entries that no row claimed, after every row was added */
void csSettingsReportUnknown(void (*log)(const char *msg));

/* Widget rows. Add returns the row index or -1 (Controls tab, bad kind, full). */
int csWidgetAdd(int tab, const CsWidget *w); /* a hidden widget answers 0 */
/* The current value of the widget (row or hidden) with that key, -1 if there is none */
int csWidgetValueByKey(int tab, const char *key);
void csWidgetsClear(void);
int csWidgetGet(int tab, int row);          /* the current value, -1 if not a widget */
int csWidgetSet(int tab, int row, int value); /* clamps (wraps a cycle); returns the stored value */

/* Counts every change the player makes to where the cursor is (a row move, a tab change, the menu opening or
   closing): an action row that waits for a second Confirm is cancelled when it changes */
unsigned csNavEpoch(void);

/* ---- sounds (O5d) ---- */

/* The screen makes the sounds of the game's own menus, but this unit has no game dependency: it tells a hook what
   happened and controls_draw.c (the boot binary) plays the game's sound for it. No hook, no sound. */
enum { CS_SND_OPEN = 1, CS_SND_CLOSE, CS_SND_MOVE, CS_SND_CONFIRM, CS_SND_CANCEL, CS_SND_TAB, CS_SND_VALUE };
void csSetSoundHook(void (*hook)(int event));

/* ---- hold to repeat (O5d) ---- */

/* The game's own repeat (src/engine/pad/port.c, repeatRate 0x10 and the count that rises by 10 up to 12): the first
   repeat 16 frames after the press, the second 6 later, then every 4. Counted in presented frames (60 Hz here) */
#define CS_REPEAT_FIRST 16
#define CS_REPEAT_SECOND 6
#define CS_REPEAT_REST 4

typedef struct CsRepeat {
    int action, frames, stage;
} CsRepeat;
void csRepeatInit(CsRepeat *r);
/* One frame: `held` is the direction held now (CS_ACT_NONE for none). Returns the action that repeats on this
   frame, else CS_ACT_NONE. The frame a direction starts being held never repeats (its press was its own action) */
int csRepeatTick(CsRepeat *r, int held);

/* Which of Up/Down/Left/Right are down: the last one pressed that still is */
typedef struct CsHold {
    int down[4], last;
} CsHold;
void csHoldInit(CsHold *h);
void csHoldSet(CsHold *h, int action, int down); /* other actions are ignored */
int csHoldAction(const CsHold *h);

/* One frame of the screen's repeat, main thread: fires the repeating action when due (Up/Down on any row,
   Left/Right only on a slider or a cycle; never during a capture or while closed). Returns the action it ran. */
int csRepeatFrame(int held);
/* The headless hook for a held direction (the event watch's job on a real device): down 1 holds it, 0 lets go */
void csQueueHold(int action, int down);
void csRepeatReset(void);

/* ---- state ---- */

int csIsOpen(void);
int csDevice(void);        /* INPUT_DEV_KEYBOARD or INPUT_DEV_PAD */
int csTab(void);
int csCursor(void);        /* the shown tab's row, 0 .. csTabRowCount - 1 */
int csWaitingRow(void);    /* the row whose capture waits, -1 if none */
const char *csMessage(void); /* a one-line status ("Bound.", "Key reserved"), "" if none */

void csOpen(void);
void csClose(void);
void csSetCursor(int row);
void csSetTab(int tab);
void csSetDevice(int device);

/* Applies one action (the testable core). Returns 1 when it changed anything. */
int csAct(int action);

/* Feeds an SDL event; returns the action it maps to (CS_ACT_NONE for most). */
union SDL_Event;
int csActionFromEvent(const union SDL_Event *ev);

/* The queue between SDL's event watch and the main thread */
void csInit(void);                 /* installs the event watch, idempotent */
void csQueueAction(int action);    /* also the headless hook */
void csSetBlocked(int blocked);    /* the screen cannot be shown (a movie plays) */

/* Once per frame, main thread: drains the queue into csAct */
void csPump(void);

/* Holds the game's pad word neutral while the screen is open and, after it
   closes, until everything is released. Returns 1 if it overwrote them. */
int csFilterPad(unsigned short *buttons, unsigned char *analog);

/* ---- drawing glue (controls_draw.c; the boot binary only) ---- */

void controlsScreenDraw(void);  /* after the game's layers, in hostDrawFrame */
void controlsScreenFrame(void); /* once per frame, from pcBootPadFrame */
/* The headless token: open, tab (-1 keeps it), cursor row (-1 keeps it), device
   (-1 keeps it), start that row's capture */
void controlsScreenHarness(int tab, int row, int device, int capture);

#endif
