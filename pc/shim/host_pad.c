/*
 * The host side of the game's own input path (T17).
 *
 * The game already has a pad: src/engine/pad/{pad,port,demo}.c read a real
 * controller through libpad, and main() opens it (PAD.init -> PadInitDirect /
 * PadStartCom) and reads it once per frame (PAD.update). What was missing on
 * the host was the two things PsyCross cannot supply by itself:
 *
 *   * PadChkVsync. The game's updatePad gates its whole read on it
 *     (`ret = PadChkVsync(); if (ret != 1) return;`, pad.c) and PsyCross's
 *     libpad answers a flat 0 (LIBPAD.C: PSYX_UNIMPLEMENTED), so the game
 *     never looked at a button and the country select never moved. On the
 *     PlayStation it reports a finished pad read for that vsync; here the pad
 *     is read once per main-loop frame, so it reports 1 once per call.
 *
 *     It is not patched into PsyCross (ADR-003): the boot link wraps the
 *     symbol (-Wl,--wrap=PadChkVsync, pc/Makefile) and binds it to
 *     __wrap_PadChkVsync, which is this file's. That is a link-time choice of
 *     the port and leaves both the submodule and the game's own C untouched.
 *
 *   * PsyX_UpdateInput. PsyCross fills the pad buffer its libpad handed out
 *     (PadInitDirect stores it in PsyX_Pad_InitPad) from SDL inside
 *     PsyX_UpdateInput -> PsyX_Pad_InternalPadUpdates, and it only calls that
 *     itself under _DEBUG. The port calls it once per frame instead, from the
 *     frame hook (shim/gfx_engine.c), which runs right before the game's own
 *     PAD.update() reads those buffers.
 *
 * With those two the chain is the game's end to end: SDL's keyboard goes
 * through PsyCross's own mapping (arrows = d-pad, Enter = Start, Space =
 * Select, X/V/Z/C = square/circle/triangle/cross), PsyCross writes the raw
 * pad word into PAD.buf[0].pad, and the game's readPad/readPadButtons turn it
 * into the held/pressed/repeated the menus read (getPadPressed and friends).
 *
 * This file also carries the automated demo: `boot <disc> <frames> <shot> 0
 * <keys>` runs a comma-separated key sequence, and each frame prints what the
 * game's own pad code saw (the raw word the libpad buffer holds, and the
 * logical held/pressed) next to the option the country select's own menu task
 * highlights. `wait` holds the script until the game's own mode changes (T18);
 * `field` waits for the player actor and entry banner before scripted movement.
 *
 * There are also tokens that are not keys at all:
 *
 *   * `warp:<stage>[,<progress>]` (debug-warp TD1) jumps the boot straight
 *     into a field stage -- on the first harness frame in a boot mode it asks
 *     the game for the stage's mode, the way a field's own code does, and the
 *     field starts at the stage's own start without any menu play.
 *
 *   * `battle` (battle-entry T3) asks the game for the battle mode
 *     (MODE_BATTLE 0x600) with a nonzero argument, the way the field's own
 *     exit does at the first automatic battle. The argument makes FIGHTSTG's
 *     root task take the battle-test path (WFIGHTTS, a random stage,
 *     battle_mode/start.c), which is deterministic enough to wire the mode:
 *     no menu play, no scripted fights. It fires once, like the warp, on the
 *     first harness frame in a boot mode (the country select or the title).
 *
 *   * `partner:<member>,<form>[,<level>]` (review shortcut) puts party member <member> (0..2) in Digimon form
 *     <form> (PartnerEntry.id = DigimonData.id, 3 or more) at level >= <level> (default 1), in its first battle
 *     slot, when the game state exists: at the fire of `battle`/`battleauto` or of `mode:`. It uses the game's own
 *     functions (addPartnerEntry, STFGTREP_addDigimonExp, STFGTREP_addSkill/learnSkill), so the techniques the
 *     form learns at that level follow the game's rules, mod techniques included. It prints
 *     `pad: partner seeded: ... skills <id>[k] ...` (k = known). Example: `partner:0,3,2,battle`.
 *
 * The scripted keys are NOT injected into the session (T21). They used to go
 * through XTest, which writes into the whole X session instead of into the
 * game's window: a scripted run typed on top of whatever the owner was doing
 * and took his keyboard focus away. Now a scripted step writes the raw pad
 * word the PlayStation pad would send straight into the buffer libpad handed
 * out, right after PsyX_UpdateInput filled it from SDL: the same word, the
 * same active-low polarity, the same place the game reads. Nothing outside
 * this process sees it, and no scripted run takes the focus.
 *
 * SDL's header is not included here: SDL.h drags <string.h>, whose strlen
 * clashes with the game's own `int strlen(const char *)` (engine/game.h), so
 * the SDL calls this file makes are declared by hand, the way the rest of the
 * shim declares libc's exit and open.
 */
#include "engine/game.h"
#include "engine/heap.h"
#include "engine/text.h"
#include "battle/battle_mode.h"
#include "port_log.h"
#undef OVL_NAME
#include "battle/report.h"
#undef OVL_NAME /* each overlay header defines its own prefix; the next one redefines it */
#include "field/field_map.h"
#include "menus/country_select.h"
#include "menus/title.h"
/* Each overlay header redefines OVL_NAME for its own symbols (title.h,
   player_name.h); the macro is already expanded inside each header's own
   declarations and this file does not use it, so drop the title's copy
   before the name entry's define lands clean. */
#undef OVL_NAME
#include "menus/player_name.h"
#include "cardgame/card_battle.h"
#include "host.h"
#include "host_st.h"
#include "input_cfg.h"
#include "controls_screen.h"
#include "input_device.h"
#include "touch_sdl.h"
#include "touch_tap.h"
#include "options_video.h"
#include "options_language.h"
#include "text_data.h"
#include "options_audio.h"
#include "auto_text.h"
#include "snd_internal.h"

#include <PsyX/PsyX_public.h>

#include <stdio.h>

/*
 * PadChkVsync: the one libpad call the game's updatePad gates on (see the
 * file comment). The boot link wraps it here, so both PsyCross's flat 0 and
 * the game's own pad.c stay as they are.
 */
int __wrap_PadChkVsync(void) {
    return 1;
}

/*
 * The buttons a scripted sequence can name, and the raw pad bit each one
 * presses. The PlayStation pad word is active low, so a clear bit means the
 * button is down -- which is what the raw buffer the game reads holds.
 */
typedef struct PadKey {
    const char *name;
    u16 bit;
} PadKey;

static const PadKey PAD_KEYS[] = {
    { "up", 1 << 4 },
    { "down", 1 << 6 },
    { "left", 1 << 7 },
    { "right", 1 << 5 },
    { "start", 1 << 3 },
    { "select", 1 << 0 },
    { "cross", 1 << 14 },
    { "circle", 1 << 13 },
    { "triangle", 1 << 12 },
    { "square", 1 << 15 },
    { "l1", 1 << 10 }, /* the card album pages with L1/R1 (menu-modes M6a) */
    { "r1", 1 << 11 },
    { "l2", 1 << 8 },
    { "r2", 1 << 9 },
};

#define PAD_SCRIPT_MAX 128
#define PAD_KEY_HOLD 4  /* frames a scripted key stays down */
#define PAD_KEY_GAP 18  /* frames between two scripted presses */
#define PAD_START_DELAY 8 /* frames between "the menu accepts input" and the first key */
#define PAD_BARRIER_TIMEOUT 900 /* max frames a scripted state barrier may wait */

/*
 * One scripted step: a key press, or something that waits on the game's own
 * state. The kinds:
 *
 *   B_kindNone       press the key once (the country select and the movie).
 *   B_kindMode       hold until the game's own mode changes (T18: the country
 *                    select's confirm leads to the opening movie).
 *   B_kindTitle      hold until the title screen's menu waits for Start.
 *   B_kindOptions    hold until that menu navigates its two options.
 *   B_kindUntil      press the key, and keep pressing it every gap until a
 *                    condition of the game's own state holds (T19). A single
 *                    press is enough when the key events land, but they go
 *                    through the X server and SDL and one can be dropped, so
 *                    the title menu's presses retry until the game's own menu
 *                    has moved: the demo is then independent of a lost key
 *                    instead of guessing how many times to press.
 */
#define B_kindNone 0     /* a single key press */
#define B_kindMode 1     /* wait for the game's own mode to change */
#define B_kindTitle 2    /* wait until the title menu waits for Start */
#define B_kindOptions 3  /* wait until the title menu navigates its options */
#define B_kindUntil 4    /* press the key until a condition holds */
#define B_kindField 5    /* wait until the game entered a field mode with the
                            player's actor in it (the field's own intro event
                            may already be running) */
#define B_kindTalk 6     /* the field's dialog drain: while the game is in a
                            field mode, wait for the dialog pause a Text
                            Window has open, read the button the marker
                            itself asks for and press it exactly once; the
                            ~ dip pauses come back as the event's texts keep
                            typing. Ends when the mode is no longer a field
                            page (the chain left for the name entry). One
                            press per marker, no cadence and no guessing: the
                            markers' own data says what each pause wants. */
#define B_kindName 7     /* the player-name screen's exact drive (T5): the
                            mode's own state machine says what is on screen
                            and what it waits for -- the welcome dialog's
                            text window pauses at its markers (state 2,
                            substate 1), the keyboard waits at
                            STPLNMET_updateKeyboard's typing substate, the
                            partners' page confirms on cross (cross ->
                            substate 100, choice.c) and the Yes/No screen
                            waits at its own substate 0 with the cursor on
                            Yes. Every press answers a state the engine
                            itself holds; the drive ends when the mode leaves
                            0x500, because confirming the name returns the
                            game to the field stage (requestMode(0x2D8)). */

/* The conditions a B_kindUntil step waits for */
#define C_none 0
#define C_modeChanged 1    /* the game left the mode the step started in */
#define C_titleLeftStart 2 /* the title menu left "PRESS START" (Start took) */
#define C_titleNewGame 3   /* its highlight is on NEW GAME */
#define C_modeLeftTitle 4  /* the game left the title's mode (cross took) */

/* The pause markers' waiting-button table, the text engine's own (the number
   of the entry a marker waits for is in the text's own bytes; codes_and_buttons
   in the handshake: {CROSS, CIRCLE, CROSS, TRIANGLE, SQUARE}) */
extern s32 TEXT_WAIT_BUTTONS[5];

/* The text window's own update (text_window.c): the field's talk pauses live
   in TextWindow tasks, which the engine does not give a registry type id, so
   the scripted step identifies them by their own update pointer. */
void updateTextWindow();

typedef struct PadStep {
    u16 keyBit;  /* the raw pad bit of a press, 0 for a barrier */
    int kind;    /* B_kindNone (a press), a barrier's kind, or B_kindUntil */
    int cond;    /* a B_kindUntil step's condition */
} PadStep;

/* The mode task main runs (boot.c's createModeTask), the top of the tree the
   selection diagnostic walks down to the country select's menu task */
static void *padRootTask;

/* The scripted sequence: armed by pcBootPadInit, run one step at a time by
   pcBootPadFrame, and written in process -- no keyboard, no XTest (T21) */
static int padScriptArmed;
static u16 padHeldBit; /* the raw bit of the button the script holds, 0 for none */
static PadStep padSteps[PAD_SCRIPT_MAX];
static int padStepCount;
static int padKeyTotal; /* the presses among the steps (the barriers are not) */
static int padPhase; /* 0 waiting for the menu, 1 between presses, 2 holding, 3 done */
static int padTimer;
static int padIndex;
static int padBarrierMode = -1; /* the mode the current 'wait' barrier saw */
static int padBarrierTimer;     /* frames the current barrier has waited */
static int drainPresses;        /* exact presses the current dialog drain fired */
static int namePresses;         /* exact presses the current name drive fired */

void pcBootPadRootTask(void *task) {
    padRootTask = task;
}

/*
 * The STPLNMET screen task (player-name T5): the mode task's child is the
 * scene (STPLNMET_updateScene), and the scene's child is the screen
 * (STPLNMET_updateScreen, screen.c). The scene has no registry id, so the
 * walk tells the tasks apart by the update pointers themselves, the same
 * way the name screen's own tasks are told apart below.
 */
static Task *padPlayerNameScreen(void) {
    Task *mode = (Task *)padRootTask;
    Task *scene;
    Task *screen;

    if (mode == NULL || mode->children == NULL || mode->childCount == 0) {
        return NULL;
    }
    scene = ((Task **)mode->children)[0];
    if (scene == NULL || scene->update != STPLNMET_updateScene ||
        scene->children == NULL || scene->childCount == 0) {
        return NULL;
    }
    screen = ((Task **)scene->children)[0];
    if (screen == NULL || screen->update != STPLNMET_updateScreen) {
        return NULL;
    }
    return screen;
}

/*
 * One of the name screen's four member tasks, found by update pointer in
 * the screen's own children slots (dialog, keyboard, partners' choice,
 * confirmation; screen.c creates them in that order, PC host children
 * stride 8). NULL while the member is dead or not created yet.
 */
static Task *padNameChild(Task *screen, void *update) {
    Task **slots;
    int i;

    if (screen == NULL || screen->children == NULL || screen->childCount == 0) {
        return NULL;
    }
    slots = (Task **)screen->children;
    for (i = 0; i < screen->childCount && i < 4; i++) {
        if (slots[i] != NULL && slots[i]->update == update) {
            return slots[i];
        }
    }
    return NULL;
}

/*
 * The country select's own controller task (country_select.c): the mode task
 * creates the screen task as its child (boot.c's pcUpdateModeTask) and the
 * screen task creates the menu task as its first child (its update takes
 * `CountryMenuTask **menu`). NULL while any of them is still missing.
 */
static CountryMenuTask *padMenuTask(void) {
    Task *mode = padRootTask;
    Task *screen;
    Task *menu;

    if (mode == NULL || mode->children == NULL || mode->childCount == 0) {
        return NULL;
    }
    screen = ((Task **)mode->children)[0];
    if (screen == NULL || screen->children == NULL || screen->childCount == 0) {
        return NULL;
    }
    menu = ((Task **)screen->children)[0];
    if (menu == NULL) {
        return NULL;
    }
    return (CountryMenuTask *)menu;
}

/*
 * The title screen's own menu task (T19): the mode task's tree down to it.
 * STDWTITL_start creates the screen task (its children are a TitleModeChildren
 * whose first entry is the title loader), the loader creates the title task
 * (its one child), and the title task's children (a TitleChildren) keep the
 * menu. NULL while any of them is still missing, and only read while the
 * game's mode is the title's (the same reason padMenuTask is).
 */
static TitleMenuTask *padTitleMenuTask(void) {
    Task *mode = padRootTask;
    Task *screen;
    Task *loader;
    Task *title;

    if (mode == NULL || mode->children == NULL || mode->childCount == 0) {
        return NULL;
    }
    screen = ((Task **)mode->children)[0];
    if (screen == NULL || screen->children == NULL || screen->childCount == 0) {
        return NULL;
    }
    loader = ((Task **)screen->children)[0];
    if (loader == NULL || loader->children == NULL || loader->childCount == 0) {
        return NULL;
    }
    title = ((Task **)loader->children)[0];
    if (title == NULL || title->children == NULL) {
        return NULL;
    }
    return ((TitleChildren *)title->children)->menu;
}

/* What the game's own pad code has right now, and the option the menu
   highlights: the raw word of the buffer PadInitDirect handed to libpad, the
   logical buttons readPadButtons built from it, and the menu task's state.
   The menu walk is the country select's own tree, so it only runs while that
   is the game's mode: once the script has moved the game on (T18's title
   overlay), the tree under the mode task is somebody else's task with another
   layout, and reading it as a CountryMenuTask would be meaningless. The title
   screen's menu (T19) is the same idea, one tree deeper. */
static void padPrintLines(int frame) {
    CountryMenuTask *menu = (GAME.funcs.getMode() == MODE_COUNTRY_SELECT) ? padMenuTask() : NULL;
    TitleMenuTask *title = (GAME.funcs.getMode() == MODE_TITLE) ? padTitleMenuTask() : NULL;
    s32 state = (menu != NULL) ? menu->state : -1;
    s32 substate = (menu != NULL) ? menu->substate : -1;
    s32 selection = (menu != NULL) ? menu->selection : -1;
    s32 titleState = (title != NULL) ? title->state : -1;
    s32 titleSubstate = (title != NULL) ? title->substate : -1;
    s32 titleSelection = (title != NULL) ? title->selection : -1;

    PLOG("pad", PLOG_TRACE, "pad %03d: raw=%04X held=%04X pressed=%04X menu=%d/%d sel=%d title=%d/%d sel=%d choice=%d\n",
         frame,
         (unsigned)(PAD.buf[0].pad.buttons & 0xFFFF),
         (unsigned)(PAD.slots[0][0].held & 0xFFFF),
         (unsigned)(PAD.slots[0][0].pressed & 0xFFFF),
         (int)state, (int)substate, (int)selection,
         (int)titleState, (int)titleSubstate, (int)titleSelection,
         (title != NULL) ? (int)title->choice : -1);
    if ((GAME.funcs.getMode() >> 8) == 2 && plogOn("field", PLOG_TRACE)) {
        Actor *player = TASK_REGISTRY.funcs.find(FIELD_TASK_ACTOR, -1, 0);

        if (player != NULL) {
            plogLine("field", "field %03d: pos=%d,%d held=%04X flags=%d,%d,%d,%d\n", frame,
                     (int)player->pos.x, (int)player->pos.y,
                     (unsigned)(PAD.slots[0][0].held & 0xFFFF),
                     (int)FIELDSTG_state.frozen, (int)FIELDSTG_state.menuOpen,
                     (int)FIELDSTG_state.busy, (int)FIELDSTG_state.battleStarting);
        }
    }
}

/* The two trace lines of the frame, only when the pad or field channel is at trace: a normal session
   skips the menu and actor walks too */
static void padPrint(int frame) {
    if (plogOn("pad", PLOG_TRACE) || plogOn("field", PLOG_TRACE)) {
        padPrintLines(frame);
    }
}

/* The raw pad bit of a scripted token, or 0 */
static u16 padKeyBit(const char *token) {
    int i;

    for (i = 0; i < (int)(sizeof(PAD_KEYS) / sizeof(PAD_KEYS[0])); i++) {
        if (PAD_KEYS[i].name[0] == token[0]) {
            const char *a = PAD_KEYS[i].name;
            const char *b = token;

            while (*a != '\0' && *a == *b) {
                a++;
                b++;
            }
            if (*a == '\0' && *b == '\0') {
                return PAD_KEYS[i].bit;
            }
        }
    }
    return 0;
}

/*
 * Holds or releases a scripted button by writing the raw word the pad would
 * send (T21): every button released except this one, which stays clear while
 * it is held. pcBootPadFrame puts it into the pad buffer the game reads, after
 * PsyX_UpdateInput has filled it from SDL; nothing leaves this process.
 */
static void padFakeKey(u16 bit, int down) {
    padHeldBit = down ? bit : 0;
}

/* include/field/field_mode.h's Encounter, mirrored (that header redefines
   OVL_NAME next to menus/title.h); the game's own arrays are linked */
typedef struct PadEncounter {
    BattleEnemy *enemies[3];
    u8 ambushChance;
    u8 kind;
    u8 blocks[12];
} PadEncounter;
extern PadEncounter FIELDSTG_encounters[];
extern BattleEnemy FIELDSTG_enemies[];

/*
 * Whether FIELDSTG_encounters[n] is a real entry: the array is unsized here,
 * so its three enemy pointers must land on FIELDSTG_enemies' 12-byte entries
 * (the table ends where a pointer stops doing so; pc/battle_sweep.sh takes
 * the count from the symbol size).
 */
static int padEncounterValid(int n) {
    int i;

    for (i = 0; i < 3; i++) {
        uintptr_t off = (uintptr_t)FIELDSTG_encounters[n].enemies[i] - (uintptr_t)&FIELDSTG_enemies[0];

        if (off >= 4096 * sizeof(FIELDSTG_enemies[0]) || off % sizeof(FIELDSTG_enemies[0]) != 0) {
            return 0;
        }
    }
    return 1;
}

/* Whether a scripted token is exactly `word` */
static int padTokenIs(const char *token, const char *word) {
    while (*word != '\0' && *token == *word) {
        token++;
        word++;
    }
    return *word == '\0' && *token == '\0';
}

/* Whether a scripted token begins with `prefix` (padTokenIs's leading part) */
static int padTokenStarts(const char *token, const char *prefix) {
    while (*prefix != '\0' && *token == *prefix) {
        token++;
        prefix++;
    }
    return *prefix == '\0';
}

/*
 * The warp token (debug-warp): `warp:<stage>[,<progress>]`. It is not a script
 * step and presses nothing: arming it is all pcBootPadInit does, and
 * pcBootPadFrame fires it once, at the first harness frame whose game mode is
 * a boot mode (the country select or the title), after which the token is
 * spent.
 *
 * The stage is named by its WSTAG number (src/field/stages/). The game's own
 * stage table FIELDSTG_stages (field_mode/data/fieldstg.c) names a mode's
 * stage only by its WSTAG symbol, which carries no number, so the map below is
 * a generated list (pc/gen_warp_stages.py): each row pairs the mode
 * FIELDSTG_stages gives the stage with the start the stage's own setupStage puts in
 * FIELDSTG_state.start/startDir. Those values matter because startField
 * forces GAME.modeArg to -1 for a field entered from a non-field mode and
 * points the default start at GAME.fieldPos/fieldDir (field_mode/field.c),
 * which is exactly the lane the warp rides: it writes the stage's start there
 * and asks the game for the stage's mode, so the player appears where the
 * retail mode entry would put them. An unknown stage id prints one warning
 * and the token is a no-op, the same treatment an unknown key gets.
 */
typedef struct PadWarpStage {
    s32 stage;  /* the WSTAG number the token names */
    s32 mode;   /* the game mode FIELDSTG_stages matches for it */
    Vec2 start; /* the stage's own FIELDSTG_state.start (its setupStage) */
    s32 dir;    /* ... and FIELDSTG_state.startDir */
    s32 progress; /* the GAME.progress that makes FIELDSTG_pickStage use the table
                     holding the stage (-1 = the default table has it) */
} PadWarpStage;

static const PadWarpStage PAD_WARP_STAGES[] = {
#include "warp_stages.inc" /* generated by pc/gen_warp_stages.py */
};

/* The armed warp: the stage's WSTAG number (0 = nothing armed), and the
   token's optional story progress (-1 = leave the game's own alone) */
static s32 padWarpStage;
static s32 padWarpProgress;
/* The `flag:<hex>` token: a FLAG(group, id) code (group << 8 | id) the warp
   sets to 1 before the stage starts (-1 = none), for a stage whose script
   branches on a flag an earlier scene set (WSTAG200's 0x400D) */
static s32 padWarpFlag = -1;

/* The armed battle token (battle-entry T3): 1 once the script names
   `battle`, consumed by padBattleFire's single fire; the arg the fire uses
   (`battle` and `battle:0`: the real battle, WFIGHTMN; `battle:1`: the
   stage-select's WFIGHTTS test) */
static int padBattleArmed;

static int padModeArmed;
static s32 padModeValue;
static s32 padModeArg;
static int padBattleArg;
/* The `battleenc:<n>[:<stage>[:<music>]]` token: encounter FIELDSTG_encounters[n]
   as FIELDSTG_startEncounter fills BATTLE_SETUP (-1: not armed) */
static int padEncIndex = -1;
static s32 padEncStage = 10; /* an AreaBattle's fight stage; 10 is the commonest */
static s32 padEncMusic = 2;  /* the first argument of the AreaBattle's MUSIC(bank, 0) */

/* The `partner:<member>,<form>[,<level>]` token: party member <member> (0..2) gets the Digimon form <form>
   (PartnerEntry.id, DigimonData.id) in its first battle slot, at level >= <level> (-1 = not armed) */
static s32 padPartnerMember = -1;
static s32 padPartnerForm;
static s32 padPartnerLevel = 1;
static s32 padPartnerHpPercent = 100; /* the optional 4th argument: HP left, in percent of the maximum */

/* Seeds the `partner:` token once the party exists. The game's own paths do the work: addPartnerEntry gives the
   form, STFGTREP_addDigimonExp levels it (10 exp a level up to its expLevel, so the level-ups are the game's),
   and STFGTREP_addSkill / STFGTREP_learnSkill give and mark every skill whose level it reached, mod techs
   included (they live in DigimonData.skills). The form takes battle slot 0, where the Tech menu, the field's
   Techniques screen and the lab read it. */
static void padSeedPartner(void) {
    PartnerEntry entry;
    s16 slots[3];
    s32 partner;
    s32 guard;
    s32 skill;
    int i;

    if (padPartnerMember < 0) {
        return;
    }
    partner = GAME.funcs.getPartyMember(padPartnerMember);
    if (partner < 0) {
        fprintf(stderr, "pad: the partner token names party member %d, which is empty; the token is a no-op\n",
                (int)padPartnerMember);
        padPartnerMember = -1;
        return;
    }
    GAME.funcs.addPartnerEntry(partner, padPartnerForm);
    if (GAME.funcs.getPartnerEntry(partner, padPartnerForm, &entry) < 0) {
        fprintf(stderr, "pad: partner %d could not take form %d (no room); the token is a no-op\n", (int)partner,
                (int)padPartnerForm);
        padPartnerMember = -1;
        return;
    }
    for (guard = 0; entry.level < padPartnerLevel && guard < 200; guard++) {
        STFGTREP_addDigimonExp(partner, padPartnerForm, 10);
        GAME.funcs.getPartnerEntry(partner, padPartnerForm, &entry);
    }
    while (STFGTREP_addSkill(partner, padPartnerForm) != 0) {
    }
    while (STFGTREP_learnSkill(partner, padPartnerForm) != 0) {
    }
    GAME.funcs.getPartnerSlots(partner, slots);
    slots[0] = padPartnerForm;
    GAME.funcs.setPartnerSlots(partner, slots);
    GAME.funcs.getPartnerEntry(partner, padPartnerForm, &entry);
    printf("pad: partner seeded: member %d partner %d form %d level %d skills", (int)padPartnerMember,
           (int)partner, (int)padPartnerForm, (int)entry.level);
    for (i = 0; i < 6; i++) {
        skill = entry.skills[i];
        printf(" %d%s", (int)(skill & SKILL_ID), (skill & SKILL_KNOWN) ? "k" : "");
    }
    printf("\n");
    if (padPartnerHpPercent < 100) {
        PartnerStats *stats = GAME.funcs.getPartnerStats(partner);
        s32 hp = stats->stats[STAT_MAX_HP] * padPartnerHpPercent / 100;

        stats->stats[STAT_HP] = hp > 0 ? hp : 1;
        printf("pad: partner %d HP set to %d of %d\n", (int)partner, (int)stats->stats[STAT_HP],
               (int)stats->stats[STAT_MAX_HP]);
    }
    padPartnerMember = -1; /* once */
}

/* The `options` token (alias `controls`; controls-config C3/O1): opens the options
   menu once the game has left the boot modes and settled, so a headless run can
   capture it. `options:<tab>[.<row>][k|p][w]` also picks the tab (0 Controls ..
   4 Audio), puts the cursor on a row, picks the keyboard (k) or controller (p)
   device and starts that row's capture (w). `controls:<row>[k|p][w]` is the
   Controls tab. */
#define PAD_CONTROLS_SETTLE 150
static int padControlsArmed;
static int padControlsTab = -1;
static int padControlsRow = -1;
static int padControlsDevice = -1;
static int padControlsCapture;
static int padControlsWait;

static void padControlsParse(const char *arg, int withTab) {
    padControlsTab = -1;
    padControlsRow = -1;
    padControlsDevice = -1;
    padControlsCapture = 0;
    if (withTab && *arg >= '0' && *arg <= '9') {
        padControlsTab = *arg - '0';
        arg++;
        if (*arg == '.') {
            arg++;
        }
    }
    if (*arg >= '0' && *arg <= '9') {
        padControlsRow = 0;
        while (*arg >= '0' && *arg <= '9') {
            padControlsRow = padControlsRow * 10 + (*arg - '0');
            arg++;
        }
    }
    for (; *arg != '\0'; arg++) {
        if (*arg == 'k') {
            padControlsDevice = INPUT_DEV_KEYBOARD;
        } else if (*arg == 'p') {
            padControlsDevice = INPUT_DEV_PAD;
        } else if (*arg == 'w') {
            padControlsCapture = 1;
        }
    }
    padControlsArmed = 1;
}

/* The `optlang` token's state (see padOptLangRun) */
static int padOptLang = -1;
static s32 padOptLangMode;
static int padOptLangStage; /* 1 armed, 2 menu open: pick, 3 picked: close, 4 closed: wait for the switch */
static int padOptLangWait;
static int padOptLangTalk;
static int padOptLangBattle;
static int padOptLangWalk = 60;

static void padControlsFire(void) {
    s32 mode;

    if (!padControlsArmed || padWarpStage != 0 || padModeArmed != 0 || padBattleArmed != 0) {
        return;
    }
    mode = GAME.funcs.getMode();
    if (mode == MODE_COUNTRY_SELECT || (mode >> 8) == 0xE || ++padControlsWait < PAD_CONTROLS_SETTLE) { /* no menu in the title or a movie */
        return;
    }
    padControlsArmed = 0;
    controlsScreenHarness(padControlsTab, padControlsRow, padControlsDevice, padControlsCapture);
    printf("pad: options menu opened (tab %d, row %d, device %d, capture %d) in mode 0x%X\n",
           padControlsTab, padControlsRow, padControlsDevice, padControlsCapture, (int)mode);
}
/* The `mode:<hex>[,<arg>]` token: one request of any game mode, fired once on
   the first boot-mode frame (how a field's LEAVE reaches a movie mode). */
/* 1 while the battle token was armed only by a `battle@` schedule, so a
   later `mode:` token (whose fire the schedule rides instead) can drop it */
static int padBattleImplicit;

/*
 * The end of the run, read from the game's state (never a timer): the mode
 * the scene lives in. The run ends on the first frame after the game has been
 * in that mode and has left it. `until:<hex>` sets it; `mode:` and the battle
 * tokens imply it (padUntilImplied); `until:` wins over an implied one and
 * `stay` drops every implied end (free play). A mode given as XX00 matches by
 * its high byte (the overlay's modes), any other exactly.
 */
static s32 padUntilMode;     /* 0 = no end armed */
static int padUntilExplicit; /* set by an `until:` token */
static int padUntilStay;     /* the `stay` token */
static int padUntilSeen;     /* the game has been in the mode */
static s32 padUntilFrom;     /* the mode last seen inside it, for the log */

static s32 padParseHex(const char *h) {
    s32 value = 0;

    while ((*h >= '0' && *h <= '9') || (*h >= 'a' && *h <= 'f') ||
           (*h >= 'A' && *h <= 'F')) {
        value = value * 16 + (*h <= '9' ? *h - '0' : (*h | 0x20) - 'a' + 10);
        h++;
    }
    return value;
}

static void padUntilImplied(s32 mode) {
    if (padUntilExplicit == 0) {
        padUntilMode = mode;
    }
}

/*
 * `movieskip:<n>`: plays a movie for n of its own frames, then skips it with
 * the button the movie player checks (Start, src/menus/title/movie.c:173).
 * The count is the movie's decoded-frame counter (frames the St layer handed
 * to the player, host_st.h), taken against the value when the run first was
 * in the `until` mode; the movies run at 15 fps, so 75 is about 5 seconds.
 * Start is held 3 frames so PAD.getPressed sees an edge.
 */
static s32 padMovieSkipFrames; /* 0 = off */
static long padMovieBase = -1;
static int padMovieSkipHold;   /* frames Start is still held, 0 idle */
static int padMovieSkipped;

static void padMovieSkipRun(void) {
    HostStStats stats;

    if (padMovieSkipFrames == 0) {
        return;
    }
    if (padMovieSkipHold > 0) {
        if (--padMovieSkipHold == 0) {
            padFakeKey(0, 0);
        }
        return;
    }
    hostStGetStats(&stats);
    if (padMovieBase < 0) {
        padMovieBase = stats.framesDelivered;
    }
    if (padMovieSkipped == 0 && stats.framesDelivered - padMovieBase >= padMovieSkipFrames) {
        printf("movie: %ld frames played -> press Start to skip\n",
               stats.framesDelivered - padMovieBase);
        padFakeKey(padKeyBit("start"), 1);
        padMovieSkipHold = 3;
        padMovieSkipped = 1;
    }
}

static void padUntilRun(void) {
    s32 mode;
    int inside;

    if (padUntilStay != 0 || padUntilMode == 0) {
        return;
    }
    mode = GAME.funcs.getMode();
    inside = (padUntilMode & 0xFF) == 0 ? (mode >> 8) == (padUntilMode >> 8)
                                        : mode == padUntilMode;
    /* the battle's implied end covers its results scene too: the report is
       its own mode (STFGTREP), entered when the fight closes */
    if (padUntilExplicit == 0 && padUntilMode == MODE_BATTLE &&
        (mode >> 8) == (MODE_BATTLE_REPORT >> 8)) {
        inside = 1;
    }
    if (inside != 0) {
        padUntilSeen = 1;
        padUntilFrom = mode;
        padMovieSkipRun();
    } else if (padUntilSeen != 0) {
        printf("end: left mode 0x%X for 0x%X -> end of run\n", (int)padUntilFrom,
               (int)mode);
        pcBootStopInMode(mode, 1);
        padUntilMode = 0;
    }
}

/*
 * The battle schedule (battle-entry T7): presses keyed to the frame the
 * battle token fired at, so the harness can drive the battle's own menus
 * (the command menu reads getPressed while cross is held, command_menu.c)
 * after the fire seeds the battle. NOT padSteps: those run under
 * padRunScript's phase 0, which waits for the country select's menu and so
 * never steps once the battle mode has taken over (the t7_red_keys run's
 * baseline: battle fired, zero presses in 300 frames).
 *
 * The script's token is `battle@<rel>:<key>[,<hold>][,<key>[,<hold>]]...`:
 * <rel> counts harness frames from the fire's frame; <key> is a PAD_KEYS
 * name, held <hold> frames (PAD_KEY_HOLD when not given); a comma-separated
 * list walks the presses down the menus (cross,down,cross), each press
 * starting after the previous one's release plus PAD_KEY_GAP, and no
 * earlier than its own rel. battle@ arms the bare `battle` fire (arg 0,
 * the real battle) unless a `battle`/`battle:N` token already did.
 */
#define PAD_BATTLE_SCHEDULE_MAX 48
typedef struct PadBattlePress {
    char name[9]; /* the key's PAD_KEYS name (max "triangle"), for the log */
    u16 bit;          /* the raw pad bit pressed */
    int rel;  /* harness frames after the battle fire the press waits for */
    int hold; /* frames the key stays down once pressed */
} PadBattlePress;

static PadBattlePress padBattleSchedule[PAD_BATTLE_SCHEDULE_MAX];
static int padBattleScheduleCount;
static int padBattleScheduleIndex; /* the press in progress */
static int padBattleSchedulePhase; /* 0 waiting for rel, 1 holding, 2 gap,
                                      3 done */
static int padBattleScheduleTimer;
static int padBattleFireFrame = -1; /* the frame padBattleFire fired at */

/* How many stages the warp map holds */
#define PAD_WARP_STAGE_COUNT (int)(sizeof(PAD_WARP_STAGES) / sizeof(PAD_WARP_STAGES[0]))

/*
 * Arms a warp token: `warp:` plus the stage's WSTAG number. An unknown stage
 * id (or trailing junk in the number) prints the warning and arms nothing,
 * the token spent as a no-op.
 */
static void padWarpArm(const char *token) {
    const char *scan = token + 5; /* past "warp:" */
    s32 stage = 0;
    int i;

    while (*scan >= '0' && *scan <= '9') {
        stage = stage * 10 + (*scan - '0');
        scan++;
    }
    if (*scan != '\0') {
        fprintf(stderr, "pad: the warp token '%s' does not end in a stage number\n", token);
        return;
    }
    for (i = 0; i < PAD_WARP_STAGE_COUNT; i++) {
        if (PAD_WARP_STAGES[i].stage == stage) {
            padWarpStage = stage;
            printf("pad: warp token armed: stage %d (mode 0x%X)\n",
                   (int)stage, (int)PAD_WARP_STAGES[i].mode);
            return;
        }
    }
    fprintf(stderr, "pad: unknown warp stage %d in '%s'; the map holds %d stages "
            "(pc/shim/warp_stages.inc); the token is a no-op\n",
            (int)stage, token, PAD_WARP_STAGE_COUNT);
}

/*
 * The battle token's arg: `battle:N` with N the arg (default 1, like the
 * stage select's scene test; the bare `battle` is 0, the field's own arg).
 */
static s32 padParseBattleArg(const char *text) {
    s32 arg = 0;

    while (*text >= '0' && *text <= '9') {
        arg = arg * 10 + (*text - '0');
        text++;
    }
    return arg;
}

/* The stages of the game's start (Kusanagi City, the lab), where progress 0-2
   is the story's own state and no starter Digimon exists yet */
static int padWarpIsPreStarter(int stage) {
    return stage == 200 || stage == 225 || stage == 780 || stage == 785;
}

/*
 * The warp's fire: the game mode is a boot mode, so the player is not playing
 * anything the warp would wipe. It sets the token's story progress (when it
 * named one), puts the stage's own start in the game's default-start lane
 * (FIELDSTG_state.defaultStart is GAME.fieldPos for a modeArg of -1, the lane
 * startField forces for a field entered from a non-field mode) and asks the
 * game for the stage's mode the way the game's own code does (requestMode,
 * which commitMode applies before recreating the mode task) -- FIELDSTG then
 * picks the stage and the field starts there.
 */
static void padWarpFire(int frame) {
    s32 mode = GAME.funcs.getMode();
    const PadWarpStage *warp = NULL;
    int i;

    if (mode != MODE_COUNTRY_SELECT && mode != MODE_TITLE) {
        return;
    }
    for (i = 0; i < PAD_WARP_STAGE_COUNT; i++) {
        if (PAD_WARP_STAGES[i].stage == padWarpStage) {
            warp = &PAD_WARP_STAGES[i];
            break;
        }
    }
    padWarpStage = 0; /* the warp fires once and the token is spent */
    if (warp == NULL) {
        return; /* cannot happen: only known ids arm the stage */
    }
    if (padWarpProgress >= 0) {
        GAME.progress = padWarpProgress;
    } else if (warp->progress >= 0) {
        GAME.progress = warp->progress; /* the EU extra chapter's table */
    }
    if (padWarpFlag >= 0) {
        FLAGS_00.applyAction(padWarpFlag, 1);
        padWarpFlag = -1;
    }
    if ((GAME.progress >= 3 || !padWarpIsPreStarter(warp->stage)) && GAME.funcs.getPartyMember(0) < 0) {
        /* progress 3 is after the starter Digimon (WSTAG200's first battle
           follows it) and 4 is where START opens the field menu; a game that
           far has a party, and the menu's screens divide by its size. Any
           stage but the four of the game's start is reached by play only
           with the starter already chosen (e.g. WSTAG805 gates on progress
           0x20, and its fight reads the party), so the sweep's warp, which
           keeps progress 0 unless told otherwise, gives it one too */
        GAME.funcs.setParty(0);
        GAME.funcs.giveStarterDeck();
    }
    GAME.fieldPos = warp->start;
    GAME.fieldDir = warp->dir;
    /* the same font reload as padBattleFire: the country select wiped VRAM
       and the skipped opening movie is what reloads it in a real run */
    FONT.load();
    padBattleFireFrame = frame; /* a `battle@` press list rides the warp too */
    GAME.funcs.requestMode(warp->mode, 0);
    printf("pad: warp fired: stage %d, mode 0x%X, progress %d, from mode 0x%X\n",
           (int)warp->stage, (int)warp->mode, (int)GAME.progress, (int)mode);
}

/* The `mode:` token's fire: the font reload every harness lane needs (the
   country select wiped VRAM), then the plain mode request. */
static void padModeFire(int frame) {
    s32 mode = GAME.funcs.getMode();

    if (padModeArmed == 0 || (mode != MODE_COUNTRY_SELECT && mode != MODE_TITLE)) {
        return;
    }
    padModeArmed = 0; /* fires once */
    FONT.load();
    if ((padModeValue == MODE_STATUS || padModeValue == MODE_CONTINUE ||
         padModeValue == MODE_DIGI_LAB || padModeValue == MODE_NAMING ||
         padModeValue == MODE_TRAINING || padModeValue == MODE_ITEM_SHOP ||
         padModeValue == MODE_CARD_SHOP || padModeValue == MODE_DECK_EDITOR ||
         padModeValue == MODE_CARD_ALBUM || padModeValue == MODE_CARD_GAME) &&
        GAME.funcs.getPartyMember(0) < 0) {
        /* A game in progress for the screens that read one: the field
           menu's party pages divide by the member count, the card screens
           read the deck, and a save writes the party. Only where the boot
           state is empty. */
        GAME.funcs.setParty(0);
        GAME.funcs.giveStarterDeck();
        GAME.fieldMode = padModeValue == MODE_DIGI_LAB ? 0x206 : 0x207; /* a real field mode: where both screens return,
                                   and what a save keeps for the load (0 is no
                                   mode: the screen would hang black) */
    }
    if (padModeValue == MODE_CARD_GAME && PC_CARD_TOTAL > PC_CARD_ORIG_COUNT) {
        /* a duel with a card a content mod added (U8b): the first new card, twenty-four copies, in the front of every deck, so the player's hand
           shows it */
        s32 deck, slot;

        PC_CARD_COUNT(PC_CARD_ORIG_COUNT) = 9;
        PC_CARD_SEEN(PC_CARD_ORIG_COUNT) = 1;
        for (deck = 0; deck < DECK_COUNT; deck++) {
            for (slot = 0; slot < 24; slot++) {
                GAME.decks[deck].cards[slot] = PC_CARD_ORIG_COUNT;
            }
        }
    }
    if (padModeValue == MODE_TRAINING) {
        /* The gym spends TP (the trainings' intensities cost it) and a new
           game's partners have none: give each party member the TP of a
           little play, so a session can be started (menu-modes M5). */
        s32 member;

        for (member = 0; member < 3; member++) {
            s32 partner = GAME.funcs.getPartyMember(member);

            if (partner >= 0) {
                GAME.funcs.setStat(partner, STAT_TP, 30);
            }
        }
    }
    if (padModeValue == MODE_DECK_EDITOR || padModeValue == MODE_CARD_ALBUM) {
        /* The deck editor and the album (menu-modes M6a) list the cards the
           player owns / has seen: the starter deck alone leaves little to
           move around or page through. Own one copy of each of the first 48
           card ids and mark them seen. */
        s32 card;

        for (card = 1; card < 0x31; card++) {
            if (PC_CARD_COUNT(card) == 0) {
                PC_CARD_COUNT(card) = 1;
            }
            PC_CARD_SEEN(card) = 1;
        }
        /* the cards content mods add (U8b): one copy of each, seen, so the editor and the album show them */
        for (card = PC_CARD_ORIG_COUNT; card < PC_CARD_TOTAL; card++) {
            if (PC_CARD_COUNT(card) == 0) {
                PC_CARD_COUNT(card) = 1;
            }
            PC_CARD_SEEN(card) = 1;
        }
    }
    if (padModeValue == MODE_ITEM_SHOP || padModeValue == MODE_CARD_SHOP) {
        /* The shops (menu-modes M4) spend money and sell what the bag holds:
           a new game has no money (nothing could be bought) and an empty
           bag (nothing could be sold). Give the money of a little play and
           one of each of the first 63 items, so every sell category (items,
           weapons, armor, accessories) has something to list. */
        s32 item;
        s32 money = padModeValue == MODE_CARD_SHOP ? 50000 : 5000; /* a card costs 10000 */

        if (GAME.money < money) {
            GAME.money = money;
        }
        for (item = 1; item < 0x40; item++) {
            if (PC_ITEM_COUNT(item) == 0) {
                PC_ITEM_COUNT(item) = 1;
            }
        }
    }
    if (padModeValue == MODE_STATUS) {
        /* The field menu's screens (menu-modes M1) read a game in progress:
           the party's pages divide by the member count, the card screens
           read the deck, and cancelling the menu goes back to the field mode
           the player was in. Seed what a started game has, only where the
           boot state is empty, and take the mode's arg as the menu option
           (FIELD_MENU_CHOICE.option, 0 items ... 4 status, 5 partners;
           retail's own arg is 0, so the slot is free). `battle@` presses
           ride the fire like the battle's. */
        /* fieldMode is 0x207 above: MODE_NEW_GAME (0x2D7) would make STSTATUS_getGameHalf -1 */
        FIELD_MENU_CHOICE.option = padModeArg;
        FIELD_MENU_CHOICE.extra = 0;
        padModeArg = 0;
    }
    padSeedPartner();
    padBattleFireFrame = frame; /* the key schedule rides this fire too */
    GAME.funcs.requestMode(padModeValue, padModeArg);
    printf("pad: mode fired: mode 0x%X, modeArg %d, from mode 0x%X\n",
           (int)padModeValue, (int)padModeArg, (int)mode);
}

/*
 * The battle token's fire (battle-entry T3): on the first harness frame in a
 * boot mode (the country select or the title, the same guard the warp rides)
 * it asks the game for the battle mode with a nonzero argument, the way the
 * field's own exit does at the first automatic battle. The argument is what
 * makes FIGHTSTG_updateRoot load WFIGHTTS (the battle test) instead of
 * WFIGHTMN, so the run does not need the loaded battle world or a scripted
 * fight; BATTLE_SETUP.stage gets a random stage of its own.
 */
static int padBattleForce; /* `optlang:<n>:battle`: the lane fires from the field, after the language switch */
static void padBattleFire(int frame) {
    s32 mode = GAME.funcs.getMode();
    s32 arg = padBattleArg;

    if (padBattleArmed == 0) {
        return;
    }
    if (mode != MODE_COUNTRY_SELECT && mode != MODE_TITLE && !padBattleForce) {
        return; /* a boot mode's frame is the only lane: nothing to wipe */
    }
    padBattleForce = 0;
    if (arg == 0) {
        /* The real battle (WFIGHTMN) reads the party and BATTLE_SETUP the
           way a field encounter fills them (FIELDSTG_startEncounter): a
           starter party and one enemy of the fighters' file. Without them
           addFighters reads GAME.party's -1 and the file's ending id 0 and
           FIGHTSTG_addFighterModel faults on a NULL FighterInfo (the run's
           crash). The seed here is the decomp's own compiled encounter
           table entry 76 (FIELDSTG_enemies[76], src/field/field_mode/data/
           fieldstg.c:99: {4, 2, 144, 9999, 16}), a level-2 enemy of the
           early game, so a starter party can win the fight and reach the
           battle's end (entry 1 is Pharaohmon, far above the starters).
           The music stays the battle test's. */
        GAME.funcs.setParty(0);
        padSeedPartner();
        {
            int m;

            for (m = 0; m < 3; m++) {
                s32 who = GAME.funcs.getPartyMember(m);

                if (who >= 0) {
                    PartnerStats *ps = GAME.funcs.getPartnerStats(who);

                    printf("pad: battle party %d: partner %d level %d hp %d/%d mp %d/%d\n", m, (int)who,
                           (int)ps->stats[STAT_LEVEL], (int)ps->stats[STAT_HP], (int)ps->stats[STAT_MAX_HP],
                           (int)ps->stats[STAT_MP], (int)ps->stats[STAT_MAX_MP]);
                }
            }
        }
        GAME.fieldMode = 0x207; /* where the results scene returns to (0 is no mode:
                                   the run would hang black after the report) */
        if (padEncIndex >= 0) {
            /* `battleenc`: the game's own encounter table, copied the way
               FIELDSTG_startEncounter does (src/field/field_mode/field.c) and
               with an AreaBattle's stage and music (FIELDSTG_startBattle).
               The prize roll is left out (hasPrize 0). */
            const PadEncounter *enc = &FIELDSTG_encounters[padEncIndex];
            int i;

            BATTLE_SETUP.stage = padEncStage;
            BATTLE_SETUP.music = 0x60000000 | (padEncMusic << 18);
            BATTLE_SETUP.battle = padEncIndex;
            BATTLE_SETUP.ambushChance = enc->ambushChance;
            BATTLE_SETUP.encounterKind = enc->kind;
            for (i = 0; i < 12; i++) {
                BATTLE_SETUP.blocks[i] = enc->blocks[i];
            }
            for (i = 0; i < 3; i++) {
                BATTLE_SETUP.enemies[i] = *enc->enemies[i];
            }
            BATTLE_SETUP.hasPrize = 0;
            printf("pad: battle encounter %d: stage %d music %d, enemies %d/%d/%d\n",
                   padEncIndex, (int)padEncStage, (int)padEncMusic,
                   (int)BATTLE_SETUP.enemies[0].fighter, (int)BATTLE_SETUP.enemies[1].fighter,
                   (int)BATTLE_SETUP.enemies[2].fighter);
            padEncIndex = -1;
        } else {
        /* the encounter's own header, as FIELDSTG_startEncounter copies it
           (EU FIELDSTG_encounters[39] = enemies[76], ambush 4, kind 1) */
        BATTLE_SETUP.battle = 39;
        BATTLE_SETUP.ambushChance = 4;
        BATTLE_SETUP.encounterKind = 1;
        BATTLE_SETUP.enemies[0].fighter = 4;
        BATTLE_SETUP.enemies[0].level = 2;
        BATTLE_SETUP.enemies[0].hp = 144;
        BATTLE_SETUP.enemies[0].mp = 9999;
        BATTLE_SETUP.enemies[0].strength = 0x10; /* 1.0, in 16ths */
        BATTLE_SETUP.music = 0x60000000 | (2 << 18); /* MUSIC(2,0), engine/sound.h */
        }
    }
    padBattleArmed = 0; /* the token fires once and is spent */
    padBattleFireFrame = frame; /* the battle schedule's presses key off this */
    /* CNTY_SEL_start wipes all of VRAM (ClearImage2 1024x512) after main's
       FONT.load(); in a real run the opening movie reloads the font before
       any later mode (src/menus/title/movie.c:263). The harness skips that
       movie, so it reloads the font the same way, or every battle text
       window samples a blank page. */
    FONT.load();
    GAME.funcs.requestMode(MODE_BATTLE, arg);
    printf("pad: battle fired: mode 0x%X, modeArg %d, from mode 0x%X\n",
           (int)MODE_BATTLE, (int)arg, (int)mode);
}

/*
 * The battle schedule's run (battle-entry T7): its own lane, next to
 * padRunScript in the frame hook -- the phase gate that holds the script
 * (the country select's menu) does not exist here. One press at a time off
 * padHeldBit, started only when both the fire's rel frame is due and the
 * previous press's release gap is over, exactly what makes the walk down
 * the menus (cross, down, cross) land where they should.
 */
/*
 * The `settle` gate (menu-modes M5, extended with the game's own pad reads):
 * a press of the schedule is released only to a screen that is reading input,
 * and each press is delivered exactly once.
 * PAD.getPressed / PAD.getRepeated are wrapped (the game calls them through
 * the PAD struct, so the pointers are swapped at runtime, src/ untouched) and
 * the wrapper notes, for pad 0, whether the game polled since the last hook
 * and whether any poll returned an edge.
 *
 *   - a press is "consumed" when a poll returns an edge while it is down; it
 *     is logged ("padbattle <frame>: <key> consumed");
 *   - a press no poll saw (the game was not reading) is held out and pressed
 *     again, so none is lost and none is read twice;
 *   - the next press waits for the previous one's consumption, its release,
 *     a later frame on which the game polled with no edge, AND the task tree
 *     being quiet for PAD_SETTLE_FRAMES (`settle:<n>`). Polling alone is not
 *     a waiting screen: the item shop polls every frame while it fades, so a
 *     poll-only gate fired the next press into a closing screen (measured).
 *     The quiet check is the one time-based part left, and only there.
 * The polls the harness itself makes inside pcBootPadFrame are not counted.
 */
static int padSettleGate;    /* armed by the `settle` token */
static int padInHook;        /* inside pcBootPadFrame: the harness's own polls */
static int padPollCount;     /* game polls of pad 0 since the last hook */
static int padPollEdge;      /* ... of which returned a button edge */
static int padIdleSeen;      /* a poll without an edge since the last release */
static int padPressSeen;     /* the held press was consumed */
static int padPressLogged;   /* ... and its log line is out */
static s32 (*padRealPressed)(s32 pad);
static s32 (*padRealRepeated)(s32 pad);

static s32 padWrapPressed(s32 pad) {
    s32 v = padRealPressed(pad);

    if (padInHook == 0 && pad == 0) {
        padPollCount++;
        padPollEdge |= v != 0;
    }
    return v;
}

static s32 padWrapRepeated(s32 pad) {
    s32 v = padRealRepeated(pad);

    if (padInHook == 0 && pad == 0) {
        padPollCount++;
        padPollEdge |= v != 0;
    }
    return v;
}

#define PAD_SETTLE_FRAMES 30
static int padSettleFrames = PAD_SETTLE_FRAMES; /* `settle:<n>` sets it */
static unsigned padSettleHash;
static int padSettleSince;

static int padSettled(int frame) {
    void *block;
    unsigned hash = 2166136261u;

    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);

        if (pcHeapBlockTag(block) != MEM_MODE || task->setState != taskSetState ||
            task->update == updateCursor) { /* a blinking cursor is a waiting menu */
            continue;
        }
        hash = (hash ^ (unsigned)(size_t)task->update) * 16777619u;
        hash = (hash ^ (unsigned)task->state) * 16777619u;
        hash = (hash ^ (unsigned)task->substate) * 16777619u;
        hash = (hash ^ (unsigned)task->step) * 16777619u;
    }
    if (hash != padSettleHash) {
        padSettleHash = hash;
        padSettleSince = frame;
    }
    return frame - padSettleSince >= padSettleFrames;
}

/* Called at the top of each hook: reads what the game did since the last one */
static void padSettleSnapshot(void) {
    if (padSettleGate != 0 && padRealPressed == NULL) {
        padRealPressed = PAD.getPressed;
        padRealRepeated = PAD.getRepeated;
        PAD.getPressed = padWrapPressed;
        PAD.getRepeated = padWrapRepeated;
    }
    if (padPollCount > 0) {
        if (padPollEdge != 0) {
            padPressSeen = 1;
        } else {
            padIdleSeen = 1;
        }
    }
    padPollCount = 0;
    padPollEdge = 0;
}

static void padBattleScheduleRun(int frame) {
    PadBattlePress *press;
    int quiet = padSettleGate != 0 ? padSettled(frame) : 1;

    if (padBattleSchedulePhase == 3 || padBattleFireFrame < 0) {
        return;
    }
    if (padBattleScheduleIndex >= padBattleScheduleCount) {
        padBattleSchedulePhase = 3;
        return;
    }
    press = &padBattleSchedule[padBattleScheduleIndex];
    if (padBattleSchedulePhase == 0) {
        if (frame - padBattleFireFrame < press->rel ||
            (padSettleGate != 0 && (padIdleSeen == 0 || quiet == 0))) {
            return;
        }
        padFakeKey(press->bit, 1);
        padBattleSchedulePhase = 1;
        padBattleScheduleTimer = press->hold;
        padPressSeen = 0;
        padPressLogged = 0;
        printf("padbattle %d: %s held %d frames (rel %d from fire %d)\n",
               frame, press->name, press->hold,
               (int)(frame - padBattleFireFrame), padBattleFireFrame);
        return;
    }
    if (padBattleSchedulePhase == 1) {
        if (padSettleGate != 0 && padPressSeen != 0 && padPressLogged == 0) {
            padPressLogged = 1;
            printf("padbattle %d: %s consumed\n", frame, press->name);
        }
        if (--padBattleScheduleTimer > 0) {
            return;
        }
        padFakeKey(press->bit, 0);
        padIdleSeen = 0; /* the next press needs a poll after this release */
        padBattleSchedulePhase = 2;
        padBattleScheduleTimer = PAD_KEY_GAP;
        return;
    }
    /* phase 2: the inter-press gap, then the next press's own rel */
    if (--padBattleScheduleTimer > 0) {
        return;
    }
    if (padSettleGate != 0 && padPressSeen == 0) {
        printf("padbattle %d: %s not read by the game, pressed again\n",
               frame, press->name);
    } else {
        padBattleScheduleIndex++; /* the same press again when no poll saw it */
    }
    padBattleSchedulePhase = 0;
}

/*
 * Whether a pointer a menu task keeps (its children block, or the cursor in
 * it) still names a live heap block. The game keeps such pointers after it
 * frees what they name (the PS1 heap leaves the freed bytes readable; the
 * host's malloc may unmap them, and a menu task can outlive its children for
 * a frame), so the autopilot must not read through one blindly.
 */
static int padBlockLive(const void *ptr) {
    void *block;

    if (ptr == NULL) {
        return 0;
    }
    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        if (pcHeapBlockData(block) == ptr) {
            return 1;
        }
    }
    fprintf(stderr, "auto: a menu's pointer %p is not a live heap block; skipped\n", ptr);
    return 0;
}

/* Whether a menu's cursor is live and not locked */
static int padCursorUsable(const void *cursor, const int *locked) {
    return padBlockLive(cursor) && *locked == 0;
}

/*
 * The battle autopilot (`battleauto`): a player, not a timer. Every frame it
 * walks the mode's task heap (the same walk padFastTextOnButtonEdge does) and
 * reads the state of the battle's own menu tasks, then presses at most one
 * key through the padHeldBit lane:
 *
 *   message box (FIGHTSTG_updateMessage) waiting on its arrow, substate 3,
 *   or closing, substate 4 (message.c:58,70)           -> cross
 *   knock-out switch menu (FIGHTSTG_updateSwitchMenu), a healthy line under
 *   the cursor (switch_menu.c:165) or the empty message (state 2) -> cross
 *   switch-in menu (FIGHTSTG_updateSwitchInMenu) on its Digimon pick
 *   (substate 0, step 1) or its switching/pair choice (substate 1, step 1)
 *   (switch_in_menu.c:258,307)                          -> cross
 *   technique menu (FIGHTSTG_updateTechMenu, tech_menu.c:285): cross on a
 *   line whose MP the active fighter can pay, down past the ones it cannot,
 *   triangle when the page has none
 *   results scene (STFGTREP_updateReport substates 8 and 0x19, a partner's
 *   25 and 45: its message windows) -> cross
 *   command menu (FIGHTSTG_updateCommandMenu, command_menu.c:57), cursor not
 *   locked: move to Tech (Attack after a menu without a usable technique)
 *   and cross
 *
 * Anything else (the fighters acting, the camera, the results scene) gets no
 * key. A press is PAD_AUTO_HOLD frames down and PAD_AUTO_GAP frames up, so
 * PAD.getPressed sees an edge each time and the game has a frame or two to
 * move its state on before the next look. One log line per press.
 */
#define PAD_AUTO_HOLD 3
#define PAD_AUTO_GAP 8

static int padBattleAuto;      /* armed by the `battleauto` token */
static int padAutoTimer;       /* frames left of the current press / gap */
static int padAutoPhase;       /* 0 looking, 1 key down, 2 gap */
static int padAutoNoTech;      /* the last technique menu had nothing usable */
static int padAutoActions;

static const char *padAutoDecide(const char **keyName) {
    void *block;
    const char *why = NULL;
    int rank = 0;

    *keyName = NULL;
    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);
        const char *w = NULL;
        const char *k = NULL;
        int r = 0;

        if (pcHeapBlockTag(block) != MEM_MODE || task->state != TASK_RUN || task->paused) {
            continue;
        }
        if (task->update == FIGHTSTG_updateMessage) {
            BattleMessageBox *box = (BattleMessageBox *)task;

            if (box->substate == 3 || box->substate == 4) {
                w = "message waits"; k = "cross"; r = 5;
            }
        } else if (task->update == STFGTREP_updateReport) {
            /* the results scene's messages: exp, money, item (report.c:166) */
            if (task->substate == 8 || task->substate == 0x19) {
                w = "results message"; k = "cross"; r = 5;
            }
        } else if (task->update == STFGTREP_updatePartner) {
            /* a partner's level-up / learned-skill messages (partner.c:394) */
            if (task->substate == 25 || task->substate == 45) {
                w = "results partner message"; k = "cross"; r = 5;
            }
        } else if (task->update == FIGHTSTG_updateSwitchMenu) {
            SwitchMenu *menu = (SwitchMenu *)task;
            SwitchMenuWindows *sw = (SwitchMenuWindows *)task->children;

            if (menu->count == 0) {
                w = "no one to switch to"; k = "cross"; r = 5;
            } else if (padBlockLive(sw) && padCursorUsable(sw->cursor, &sw->cursor->locked)) {
                BattleFighter *f = &FIGHTSTG_battle.state.fighters[0][menu->others[sw->cursor->sel]];

                if (f->hp != 0) {
                    w = "switch menu, healthy partner"; k = "cross"; r = 5;
                } else {
                    w = "switch menu, knocked-out partner"; k = sw->cursor->sel > 0 ? "up" : "down"; r = 5;
                }
            }
        } else if (task->update == FIGHTSTG_updateSwitchInMenu) {
            if (task->step == 1 && (task->substate == 0 || task->substate == 1)) {
                w = task->substate == 0 ? "switch-in Digimon pick" : "switch-in choice";
                k = "cross"; r = 5;
            }
        } else if (task->update == FIGHTSTG_updateTechMenu) {
            TechMenu *menu = (TechMenu *)task;
            TechMenuChild *c = (TechMenuChild *)task->children;

            if (padBlockLive(c) && padCursorUsable(c[0].cursor, &c[0].cursor->locked)) {
                BattleFighter *active = &FIGHTSTG_battle.state.fighters[0][FIGHTSTG_battle.state.active[0]];
                int lines = c[0].cursor->params.count;
                int sel = c[0].cursor->sel;
                int idx = menu->page * TECH_MENU_LINES + sel;
                int ok = idx < menu->count &&
                         (active->temporary != 0 ||
                          active->mp >= FIGHTSTG_battleFuncs.getTechCost(0, menu->techs[idx]));

                if (menu->sel != sel) {
                    r = 0; /* the menu is still redrawing the line under the cursor */
                } else if (ok) {
                    w = "tech menu, usable technique"; k = "cross"; r = 4;
                } else if (sel + 1 < lines) {
                    w = "tech menu, not enough MP"; k = "down"; r = 4;
                } else {
                    w = "tech menu, none usable"; k = "triangle"; r = 4;
                    padAutoNoTech = 1;
                }
            }
        } else if (task->update == FIGHTSTG_updateCommandMenu) {
            CommandMenuWindows *cw = (CommandMenuWindows *)task->children;

            if (padBlockLive(cw) && padCursorUsable(cw->cursor, &cw->cursor->locked)) {
                int want = padAutoNoTech ? BATTLE_COMMAND_ATTACK : BATTLE_COMMAND_TECH;

                if (cw->cursor->sel < want) {
                    w = "command menu"; k = "down";
                } else if (cw->cursor->sel > want) {
                    w = "command menu"; k = "up";
                } else {
                    w = padAutoNoTech ? "command menu, Attack" : "command menu, Tech";
                    k = "cross";
                    if (padAutoNoTech) {
                        padAutoNoTech = 0;
                    }
                }
                r = 3;
            }
        }
        if (w != NULL && r > rank) {
            rank = r; why = w; *keyName = k;
        }
    }
    return why;
}


/*
 * The card battle autopilot (`cardauto`, menu-modes M6b): the same kind of
 * player as `battleauto`, for CARDGAME. The whole duel lives in one task
 * (CARDGAME_updateBattle, a CardBattle), so the pilot reads that task's own
 * state and presses the key the game is waiting for -- never a timer:
 *
 *   run CARD_RUN_PHASE (CARDGAME_runPhase):
 *     CHOOSE_DECK, phaseStep 9 (moveDeckMarker, battle.c:788)       -> cross
 *     PLAYS_*, turnState 17 with window 5 open (battle.c:1246)       -> cross
 *     END_ROUND, phaseStep 4, 13, 16, 20, 23 (battle.c:1803-1876)    -> cross
 *   run CARD_RUN_RESOLVE: resolveState 8 (battle.c:2238)             -> cross
 *   run CARD_RUN_STEP, by effectStep.id (CARDGAME_runEffectStep):
 *     18/19 stepPlayCard state 3 (EU card info, round.c:442)         -> cross
 *     47-49 waitMessage state 2 (battle_steps.c:25)                   -> cross
 *     159-166 stepMessage state 2 (hand_pick.c:827)                   -> cross
 *     167 drawFirstPlayer state 2 (hand_pick.c:918)                   -> cross
 *     20 stepStart states 8, 12; 27 stepSwap state 6; 28/29 stepRoundEnd
 *       state 4; 72 drawNewCards state 6 (their message windows)     -> cross
 *     152 yes/no state 2 (questions.c:87), "play a card?"            -> cross
 *     154 put-out pick state 3 (hand_pick.c:213): cross on each eligible
 *       card, square when none is left; state 7 the same confirm question
 *     153 stepChooseCards state 3 (hand_pick.c:184): walk the cursor to the
 *       first eligible card (effectStep.eligible) and cross; state 7, the
 *       confirm question (hand_pick.c:480), cross on Yes
 *     169-173, 151 chooseCard state 2 (card_choice.c:79): the cursor to an
 *       eligible card, cross; 141, 174 pickTableCard state 4/5
 *       (table_view.c:498, 525): the same along a row of slots
 *     112-132, 142-149 pile / slot choices, state 2 (card_choice.c:406,
 *       523, 661)                                                    -> cross,
 *       then triangle when cross left the state unchanged
 *
 * Anything else gets no key. A press is PAD_AUTO_HOLD frames down and
 * PAD_AUTO_GAP up (the battle pilot's lane). One log line per press, and one
 * "card" line each time the game's state moves, so a stall shows the state.
 */
static int padCardAuto;       /* armed by the `cardauto` token */
static int padCardArg = 3;    /* the opponent's mode arg: opponent * 2 + kind + 1 */
static unsigned padCardSig;   /* the state the last press was decided in */
static int padCardSame;       /* presses made in that same state */
static int padCardFrame;

static CardBattle *padCardBattle(void) {
    void *block;

    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);

        if (pcHeapBlockTag(block) == MEM_MODE && task->update == CARDGAME_updateBattle) {
            return (CardBattle *)task;
        }
    }
    return NULL;
}

/* The key that walks a hand's cursor to its first eligible card, or cross on it */
static const char *padCardPickHand(CardBattle *b, int count, const char *none) {
    int i;

    for (i = 0; i < count && i < 0x29; i++) {
        if (b->effectStep.eligible[i] != 0) {
            if (i == b->effectStep.cursor) {
                return "cross";
            }
            return i > b->effectStep.cursor ? "right" : "left";
        }
    }
    return none; /* nothing in the hand can be played: the pick is over */
}

/* The key that walks the table pick's cursor to an eligible slot (the player's
   row is sprites 0-5, the opponent's 6-11), or to the other row */
static const char *padCardPickRow(CardBattle *b, int row) {
    int i;

    for (i = 0; i < b->players[row].slotCount && i < 6; i++) {
        if (b->effectStep.eligible[row * 6 + i] != 0) {
            if (i == b->effectStep.cursor) {
                return "cross";
            }
            return i > b->effectStep.cursor ? "right" : "left";
        }
    }
    for (i = 0; i < b->players[row ^ 1].slotCount && i < 6; i++) {
        if (b->effectStep.eligible[(row ^ 1) * 6 + i] != 0) {
            return row == 0 ? "up" : "down";
        }
    }
    return "triangle";
}

static const char *padCardDecide(const char **keyName) {
    CardBattle *b = padCardBattle();
    CardBattleItems *items;
    CardScreen *screen;
    const char *w = NULL;
    const char *k = NULL;
    unsigned sig;

    *keyName = NULL;
    if (b == NULL || b->state != 1 || b->paused || b->children == NULL) {
        return NULL;
    }
    items = (CardBattleItems *)b->children;
    screen = items->screen;
    if (screen == NULL) {
        return NULL;
    }
    switch (b->run) {
    case CARD_RUN_PHASE:
        if (b->nextPhase != CARD_PHASE_NONE) {
            break;
        }
        if (b->phase == CARD_PHASE_CHOOSE_DECK && b->phaseStep == 9) {
            w = "deck choice"; k = "cross";
        } else if ((b->phase == CARD_PHASE_PLAYS_BEFORE || b->phase == CARD_PHASE_PLAYS_AFTER) &&
                   b->record.turnState == 17 && screen->windows[5].state == 2) {
            w = "plays message"; k = "cross";
        } else if (b->phase == CARD_PHASE_END_ROUND &&
                   (b->phaseStep == 4 || b->phaseStep == 13 || b->phaseStep == 16 ||
                    b->phaseStep == 20 || b->phaseStep == 23)) {
            w = "round end message"; k = "cross";
        }
        break;
    case CARD_RUN_RESOLVE:
        if (b->resolveState == 8) {
            w = "card message"; k = "cross";
        }
        break;
    case CARD_RUN_STEP:
        if (b->effectStep.next != 0) {
            break; /* a step is about to start */
        }
        switch (b->effectStep.id) {
        case 18:
        case 19:
            if (b->effectStep.state == 3) {
                w = "played card info"; k = "cross";
            }
            break;
        case 47:
        case 48:
        case 49:
            if (b->effectStep.state == 2) {
                w = "step message"; k = "cross";
            }
            break;
        case 159: case 160: case 161: case 162:
        case 163: case 164: case 165: case 166:
            if (b->effectStep.state == 2) {
                w = "phase message"; k = "cross";
            }
            break;
        case 167:
            if (b->effectStep.state == 2) {
                w = "first player draw"; k = "cross";
            }
            break;
        case 20:
            /* stepStart's "a deck is short" messages (round.c:749, 771) */
            if (b->effectStep.state == 8 || b->effectStep.state == 12) {
                w = "deal message"; k = "cross";
            }
            break;
        case 27:
            if (b->effectStep.state == 6) {
                w = "swap message"; k = "cross";
            }
            break;
        case 28:
        case 29:
            /* stepRoundEnd's window 20 (round.c:1303) */
            if (b->effectStep.state == 4) {
                w = "round end window"; k = "cross";
            }
            break;
        case 72:
            if (b->effectStep.state == 6) {
                w = "new cards message"; k = "cross";
            }
            break;
        case 152:
            if (b->effectStep.state == 2) {
                w = "play a card?"; k = "cross";
            }
            break;
        case 153:
            if (b->effectStep.state == 3) {
                w = "choose a card"; k = padCardPickHand(b, b->sides[b->record.turnSide].pile.handCount, "triangle");
            } else if (b->effectStep.state == 7) {
                w = "confirm the card"; k = b->effectStep.choice == 0 ? "cross" : "up";
            }
            break;
        case 154:
            /* the put-out pick (readPickInput, hand_pick.c:193): cross
               toggles the cards that pay, square ends it */
            if (b->effectStep.state == 3) {
                w = "put out cards"; k = padCardPickHand(b, b->sides[0].pile.handCount, "square");
            } else if (b->effectStep.state == 7) {
                w = "confirm the put-out"; k = b->effectStep.choice == 0 ? "cross" : "up";
            }
            break;
        case 169: case 170: case 171: case 172: case 173: case 151:
            /* chooseCard (card_choice.c:68): the row's cursor on an eligible card */
            if (b->effectStep.state == 2) {
                w = "pick a card"; k = padCardPickHand(b, b->effectStep.vars[3], "triangle");
            }
            break;
        case 141:
        case 174:
            /* pickTableCard (table_view.c:479): state 4 the player's row, 5 the opponent's */
            if (b->effectStep.state == 4 || b->effectStep.state == 5) {
                w = "pick a table card"; k = padCardPickRow(b, b->effectStep.state == 4 ? 0 : 1);
            }
            break;
        case 112: case 113: case 114: case 115: case 116: case 117: case 118: case 119:
        case 120: case 121: case 122: case 123: case 124: case 125: case 126: case 127:
        case 128: case 129: case 130: case 131: case 132:
        case 142: case 143: case 144: case 145: case 146: case 147: case 148: case 149:
            if (b->effectStep.state == 2) {
                w = "pile or slot choice"; k = "cross";
            }
            break;
        }
        break;
    default:
        break;
    }
    sig = ((unsigned)b->run << 24) ^ ((unsigned)b->phase << 16) ^ ((unsigned)b->phaseStep << 8) ^
          (unsigned)b->record.turnState ^ ((unsigned)b->effectStep.id << 20) ^
          ((unsigned)b->effectStep.state << 12) ^ ((unsigned)b->resolveState << 4) ^
          ((unsigned)b->effectStep.cursor << 28) ^ ((unsigned)b->record.playCount << 26);
    if (sig != padCardSig) {
        padCardSig = sig;
        padCardSame = 0;
        printf("card %d: run %d phase %d/%d turn %d/%d side %d play %d step %d/%d resolve %d hand %d/%d wins %d/%d%s\n",
               padCardFrame, (int)b->run, (int)b->phase, (int)b->phaseStep, (int)b->record.turnState,
               (int)b->record.turns, (int)b->record.turnSide, (int)b->record.playCount,
               (int)b->effectStep.id, (int)b->effectStep.state, (int)b->resolveState,
               (int)b->sides[0].pile.handCount, (int)b->sides[1].pile.handCount,
               (int)b->sides[0].pile.wins, (int)b->sides[1].pile.wins, k != NULL ? " <- waits" : "");
    }
    if (k != NULL && k[0] == 'c' && k[1] == 'r' && ++padCardSame > 3) {
        /* cross left the state unchanged: the choice has nothing to take */
        k = "triangle";
        w = "nothing to take";
    }
    *keyName = k;
    return k != NULL ? w : NULL;
}

static void padBattleAutoRun(int frame) {
    const char *key;
    const char *why;

    if ((padBattleAuto == 0 && padCardAuto == 0) || padBattleFireFrame < 0) {
        return;
    }
    padCardFrame = frame;
    if (padAutoPhase == 1) {
        if (--padAutoTimer > 0) {
            return;
        }
        padFakeKey(0, 0);
        padAutoPhase = 2;
        padAutoTimer = PAD_AUTO_GAP;
        return;
    }
    if (padAutoPhase == 2) {
        if (--padAutoTimer > 0) {
            return;
        }
        padAutoPhase = 0;
    }
    why = padCardAuto != 0 ? padCardDecide(&key) : padAutoDecide(&key);
    if (why == NULL) {
        return;
    }
    padFakeKey(padKeyBit(key), 1);
    padAutoPhase = 1;
    padAutoTimer = PAD_AUTO_HOLD;
    printf("auto %d: %s -> %s (#%d)\n", frame, why, key, ++padAutoActions);
}

/* Adds one step to the script (a press, a barrier or a press-until) */
static void padAddStep(u16 keyBit, int kind, int cond) {    if (padStepCount >= PAD_SCRIPT_MAX) {
        return;
    }
    padSteps[padStepCount].keyBit = keyBit;
    padSteps[padStepCount].kind = kind;
    padSteps[padStepCount].cond = cond;
    padStepCount++;
    if (kind == B_kindNone || kind == B_kindUntil || kind == B_kindTalk) {
        padKeyTotal++;
    }
}

/*
 * Whether a barrier's condition holds yet. B_kindMode is the T18 one: the
 * game's own mode is no longer the one the barrier saw when it was reached
 * (the country select's confirm moves the game into the opening movie).
 * B_kindTitle and B_kindOptions (T19) read the title screen's own menu task
 * instead of guessing a frame count: title ready to show its two options
 * means it is in the "PRESS START" substate that reads Start (the menu's
 * state TASK_RUN, substate 2), and options navigable means it has moved on to
 * TASK_DONE (runOptions), where up and down move and cross confirms.
 */
static int padBarrierReady(int kind) {
    TitleMenuTask *title;

    switch (kind) {
    case B_kindTitle:
    case B_kindOptions:
        if (GAME.funcs.getMode() != MODE_TITLE) {
            return 0;
        }
        title = padTitleMenuTask();
        if (title == NULL) {
            return 0;
        }
        if (kind == B_kindTitle) {
            return title->state == TASK_RUN && title->substate == 2;
        }
        return title->state == TASK_DONE && title->substate == 1;
    case B_kindField:
        /* Any mode whose page is the field's: the player's actor also has to
           exist so the scripted walk keys land on someone. */
        if ((GAME.funcs.getMode() >> 8) != 2) {
            return 0;
        }
        return TASK_REGISTRY.funcs.find(FIELD_TASK_ACTOR, -1, 0) != NULL;
    case B_kindMode:
    default:
        return GAME.funcs.getMode() != padBarrierMode;
    }
}

/*
 * The field's dialog pause kinds (field-transit), in engine-faithful order:
 *  - PAD_PAUSE_MARKER: a TextWindow paused at a button marker (state 2,
 *    substate 1; text_window.c's TEXT_WAIT_BUTTONS[step] answers it, the
 *    marker's own button).
 *  - PAD_PAUSE_PAGE: a talk box's text page waiting for the box's own
 *    advance button: updateTalkBox's runtime case presses cross and calls
 *    showPage (talk_box.c), the game's own page-advance. These never look
 *    like a window marker pause and show no arrow (no isWaitingForButton),
 *    which is exactly how stage 785's chain waits.
 *
 * Windows are found walking the shim heap's live blocks: the game's own
 * heap list does not exist on the host (shim/heap.c), and the tasks lack a
 * registry id (createTaskWithId registers id != 0 only), so a talk task's
 * block is told apart from any other allocation by its own update pointer.
 */
#define PAD_PAUSE_NONE 0
#define PAD_PAUSE_MARKER 1
#define PAD_PAUSE_PAGE 2

/* Finds the first pause the engine asks the player to answer, walking the
   shim heap's live blocks. A marker pause outranks a page pause (both can
   exist: the box's name window sits beside its text window). found counts
   marker pauses; seen counts TextWindow tasks; kind says which pause the
   returned window is. Returns NULL with kind PAD_PAUSE_NONE when nothing
   waits. */
static TextWindow *padPausedWindow(int *found, int *seen, int *kind) {
    void *block;
    TextWindow *marker = NULL;
    TextWindow *page = NULL;

    *found = 0;
    *seen = 0;
    *kind = PAD_PAUSE_NONE;
    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);
        TextWindow *win;

        if (pcHeapBlockTag(block) != MEM_MODE) {
            continue;
        }
        if (task->update != updateTextWindow) {
            continue;
        }
        (*seen)++;
        win = (TextWindow *)task;
        if (win->state == 2 && win->substate == 1) {
            (*found)++;
            if (marker == NULL) {
                marker = win;
            }
        } else if (win->state == 1 && win->visible != 0 && win->finished == 0) {
            if (page == NULL) {
                page = win;
            }
        }
    }
    if (marker != NULL) {
        *kind = PAD_PAUSE_MARKER;
        return marker;
    }
    if (page != NULL) {
        *kind = PAD_PAUSE_PAGE;
        return page;
    }
    return NULL;
}

/*
 * One line per text window in the heap: its state machine's fields, so a
 * wait that never sees a pause says which state the windows are in (the
 * state numbers' meanings are updateTextWindow's switch: 1 types, 2 with
 * substate 0 is the timed wait, substate 1 the button wait).
 */
static void padPrintWindowStates(void) {
    void *block;

    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);
        TextWindow *win;

        if (pcHeapBlockTag(block) != MEM_MODE) {
            continue;
        }
        if (task->update != updateTextWindow) {
            continue;
        }
        win = (TextWindow *)task;
        printf("      window: state %d substate %d visible %d finished %d "
               "typeDelay %d visibleEnd %d text->len %d\n",
               (int)win->state, (int)win->substate, (int)win->visible,
               (int)win->finished, (int)win->typeDelay,
               (int)win->visibleEnd, (int)win->text[0].len);
    }
}

/* The raw libpad bit of a pause marker's waiting logical button. The
   marker table (TEXT_WAIT_BUTTONS) holds the buttons' LOGICAL INDEXES
   (PAD_CROSS is 13, the number, not the bit word 1<<13); the game's
   readPadButtons rotates circle, cross and triangle from the raw word into
   the held it returns (engine/pad.h), so each logical index maps back to
   its own raw position. Square is the same on both sides. This takes the
   INDEX: an index misread as a bit word falls off the table and presses
   square, which is exactly the dialog-freeze bug this note records. */
static u16 padLogicalIndexToRaw(int logicalIndex) {
    switch (logicalIndex) {
    case 12: return 1 << 13; /* circle */
    case 13: return 1 << 14; /* cross */
    case 14: return 1 << 12; /* triangle */
    case 15: return 1 << 15; /* square */
    default: return 0;
    }
}

/*
 * Whether a B_kindUntil step's condition holds: the game's own state has
 * moved the way that step's key is meant to move it. Read from the same menu
 * task the barrier uses.
 */
static int padCondReady(int cond) {
    TitleMenuTask *title = (GAME.funcs.getMode() == MODE_TITLE) ? padTitleMenuTask() : NULL;

    switch (cond) {
    case C_modeChanged:
        return padBarrierMode != -1 && GAME.funcs.getMode() != padBarrierMode;
    case C_titleLeftStart:
        /* Start was taken (the menu moved on), or the mode already changed */
        return title == NULL || title->state != TASK_RUN || title->substate != 2;
    case C_titleNewGame:
        return title != NULL && title->state == TASK_DONE && title->selection == 0;
    case C_modeLeftTitle:
        return GAME.funcs.getMode() != MODE_TITLE;
    case C_none:
    default:
        return 0;
    }
}

/*
 * The field autopilot (`fieldauto`): a player for the field's dialogs, for
 * the runs that ride a warp (the script's steps only start from the country
 * select's menu). Every frame it looks for a text window the engine holds
 * paused (padPausedWindow, the same walk `talkdrain` makes) and presses the
 * button that window asks for, one press at a time, with the battle
 * autopilot's hold and gap. It presses nothing while the field walks or
 * fades, and switches itself off for good once the story leaves the first
 * field stage it drove (another field mode than the warp's, e.g. the LEAVE
 * into the Digimon Lab): from there the owner plays, and a still-armed pilot
 * would keep confirming every menu (the inn's rest menu looped that way).
 */
static int padFieldAuto; /* armed by the `fieldauto` token */
static int padFieldExit; /* `fieldexit`: also press cross while the player stands in the rectangle of a slot a stage patch added (port-close S8) */
extern int pcSlotAreaHeld(void);
static s32 padFieldAutoMode = -1; /* the field mode it started on */

/*
 * The `fieldbattles:<n>` token (stage-sweep S4): n random battles from a real field, the way
 * FIELDSTG_countBattleSteps starts one (FIELDSTG_startAreaBattle, the area's own table), without
 * walking. Each one waits for a quiet field (nothing busy/acting, a short settle after the mode
 * began), so the second is entered from the field the first one returned to.
 */
static int padFieldBattles; /* battles left to start */
static s32 padFieldBattlesMode = -1; /* the field mode last seen */
static int padFieldBattlesSince;     /* frames in it */

static void padFieldBattlesRun(int frame) {
    s32 mode;

    if (padFieldBattles <= 0) {
        return;
    }
    mode = GAME.funcs.getMode();
    if ((mode >> 8) != 2) {
        padFieldBattlesMode = -1;
        return;
    }
    if (mode != padFieldBattlesMode) {
        padFieldBattlesMode = mode;
        padFieldBattlesSince = 0;
    }
    if (++padFieldBattlesSince < 240 || FIELDSTG_state.battles == NULL ||
        FIELDSTG_state.battleStarting != 0 || FIELDSTG_state.busy != 0 ||
        FIELDSTG_state.acting != 0 || FIELDSTG_state.frozen != 0) {
        return;
    }
    padFieldBattles--;
    padFieldBattlesMode = -1;
    printf("pad: frame %d: fieldbattles starts an area battle (%d left)\n", frame, padFieldBattles);
    FIELDSTG_battleFuncs.startAreaBattle();
    printf("pad: fieldbattles: encounter %d stage %d fighters %d/%d/%d\n", (int)BATTLE_SETUP.battle,
           (int)BATTLE_SETUP.stage, (int)BATTLE_SETUP.enemies[0].fighter,
           (int)BATTLE_SETUP.enemies[1].fighter, (int)BATTLE_SETUP.enemies[2].fighter);
}

static void padFieldAutoRun(int frame) {
    TextWindow *win;
    int paused;
    int seen;
    int kind;
    u16 bit;

    if (padFieldAuto == 0 || padBattleFireFrame < 0) {
        return;
    }
    if (padAutoPhase == 1) {
        if (--padAutoTimer > 0) {
            return;
        }
        padFakeKey(0, 0);
        padAutoPhase = 2;
        padAutoTimer = PAD_AUTO_GAP;
        return;
    }
    if (padAutoPhase == 2) {
        if (--padAutoTimer > 0) {
            return;
        }
        padAutoPhase = 0;
    }
    if ((GAME.funcs.getMode() >> 8) != 2) {
        return;
    }
    if (padFieldAutoMode < 0) {
        padFieldAutoMode = GAME.funcs.getMode();
    } else if (GAME.funcs.getMode() != padFieldAutoMode) {
        padFieldAuto = 0;
        printf("fieldauto %d: off, the story reached field mode 0x%X\n", frame,
               (int)GAME.funcs.getMode());
        return;
    }
    win = padPausedWindow(&paused, &seen, &kind);
    if (win == NULL) {
        if (padFieldExit != 0 && pcSlotAreaHeld() >= 0 && FIELDSTG_state.busy == 0 && FIELDSTG_state.menuOpen == 0 && FIELDSTG_state.frozen == 0) {
            padFakeKey(padLogicalIndexToRaw(PAD_CROSS), 1);
            padAutoPhase = 1;
            padAutoTimer = PAD_AUTO_HOLD;
            printf("fieldauto %d: the player stands in the area of slot %d -> press #%d\n", frame, pcSlotAreaHeld(), ++padAutoActions);
        }
        return;
    }
    bit = kind == PAD_PAUSE_MARKER ? padLogicalIndexToRaw(TEXT_WAIT_BUTTONS[win->step])
                                   : padLogicalIndexToRaw(PAD_CROSS);
    padFakeKey(bit, 1);
    padAutoPhase = 1;
    padAutoTimer = PAD_AUTO_HOLD;
    printf("fieldauto %d: dialog %s waits -> press #%d\n", frame,
           kind == PAD_PAUSE_MARKER ? "marker" : "page", ++padAutoActions);
}

/*
 * Auto text (controls-config O6). The field's speech is a TalkBox or a MessageBox (the only
 * creators are field_mode/speech.c); each runs one text window, and the only place such a
 * window stops for the player is a "wait for a button" code (code 2): window state 2,
 * substate 1, released by TEXT_WAIT_BUTTONS[step] (talk_box.c / message_box.c show the arrow
 * for exactly that, isWaitingForButton). Pages that scroll or break (codes 1 and 3) go on
 * by themselves. So the prompt is: a window that is the text of a live TalkBox / MessageBox
 * and waits on step 0 or 2 (cross, the confirm button; circle / triangle / square waits are
 * left alone). Never when:
 *   - a yes/no question is live (FIELDSTG_runChoice: its title and options are its own
 *     windows, and its cursor is an updateCursor task) or any cursor task exists;
 *   - the field menu / inn is open, or the mode is not the field (battle and the menus have
 *     no TalkBox or MessageBox, and name entry etc. never reach this);
 *   - the options menu is open (csIsOpen).
 * The press is one frame of the cross bit cleared in the raw pad word (the same active-low
 * word padFakeKey writes), applied at the end of pcBootPadFrame.
 */
/* field_mode.h cannot be included here (it redefines OVL_NAME next to menus/title.h) */
extern void FIELDSTG_runChoice();
static AutoText padAutoText;
static u16 padAutoTextBit; /* the raw bit to press this frame, 0 for none */

#define PAD_AUTO_TEXT_OWNERS 8

/* The text windows of the live speech boxes (TalkBox windows[1], MessageBox window), and whether
   a question / cursor task is alive. Shared by auto text and fast text (O6b) */
static int padDialogueWindows(const void **owners, int max, int *cursor) {
    void *block;
    int count = 0;

    *cursor = 0;
    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);

        if (pcHeapBlockTag(block) != MEM_MODE) {
            continue;
        }
        if (task->update == updateCursor || task->update == FIELDSTG_runChoice) {
            *cursor = 1;
        } else if (task->update == updateTalkBox && task->state == 1 && task->children != NULL && count < max) {
            owners[count++] = ((TalkBoxChildren *)task->children)->windows[1];
        } else if (task->update == updateMessageBox && task->state == 1 && task->substate == 1 &&
                   task->children != NULL && count < max) {
            owners[count++] = ((struct MessageBox *)task->children)->window;
        }
    }
    return count;
}

/* The plain next-page prompt that waits now: 1 with its identity and length, 0 for none, -1 when one waits but a
   question or a cursor menu is up (the prompt is the player's). Never in battle, the field menu or the options menu.
   Shared by Auto text and the tap on the picture (android T7c phase 1b). */
static int padWaitingPage(int *key, int *chars) {
    void *block;
    const void *owners[PAD_AUTO_TEXT_OWNERS];
    int count = 0;
    int cursor = 0;
    int waiting = 0;

    if (csIsOpen() || (GAME.funcs.getMode() >> 8) != 2 || FIELDSTG_state.menuOpen != 0) {
        return 0;
    }
    count = padDialogueWindows(owners, PAD_AUTO_TEXT_OWNERS, &cursor);
    for (block = pcHeapFirstBlock(); block != NULL && count > 0; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);
        TextWindow *win = (TextWindow *)task;
        int i;

        if (pcHeapBlockTag(block) != MEM_MODE || task->update != updateTextWindow) {
            continue;
        }
        if (win->state != 2 || win->substate != 1 || (win->step != 0 && win->step != 2)) {
            continue;
        }
        for (i = 0; i < count; i++) {
            if (owners[i] == (const void *)win) {
                waiting = 1;
                *key = (int)(((size_t)win >> 4) * 31u) + (int)win->counter;
                *chars = (int)win->counter - (int)win->start;
                break;
            }
        }
        if (waiting) {
            break;
        }
    }
    if (waiting && cursor) {
        return -1;
    }
    return waiting;
}

/* A plain dialogue page being typed now (the window's state 1): 1 with a page identity (the window; a finished page forgets its answer), -1 when
   a question or cursor menu is up, 0 for none. Same gates as padWaitingPage. A tap there presses Cross, which the port's
   fast text (padFastTextOnButtonEdge) takes as "finish this page". */
static int padTypingPage(int *key) {
    void *block;
    const void *owners[PAD_AUTO_TEXT_OWNERS];
    int count = 0;
    int cursor = 0;
    int typing = 0;

    if (csIsOpen() || (GAME.funcs.getMode() >> 8) != 2 || FIELDSTG_state.menuOpen != 0) {
        return 0;
    }
    count = padDialogueWindows(owners, PAD_AUTO_TEXT_OWNERS, &cursor);
    for (block = pcHeapFirstBlock(); block != NULL && count > 0 && !typing; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);
        TextWindow *win = (TextWindow *)task;
        int i;

        if (pcHeapBlockTag(block) != MEM_MODE || task->update != updateTextWindow || win->state != 1 ||
            win->visible == 0) {
            continue;
        }
        for (i = 0; i < count; i++) {
            if (owners[i] == (const void *)win) {
                typing = 1;
                *key = (int)(((size_t)win >> 4) * 31u);
                break;
            }
        }
    }
    if (typing && cursor) {
        return -1;
    }
    return typing;
}

static void padAutoTextFrame(int frame) {
    int found;
    int waiting = 0;
    int key = 0;
    int chars = 0;
    int speed = autoTextSpeed();

    padAutoTextBit = 0;
    if (speed == AUTO_TEXT_OFF) {
        autoTextTick(&padAutoText, AUTO_TEXT_OFF, 0, 0, 0);
        return;
    }
    found = padWaitingPage(&key, &chars);
    if (found < 0) {
        /* a question or a cursor menu is up: the prompt is left for the player */
        static int lastLogged;

        if (lastLogged != key) {
            lastLogged = key;
            printf("autotext %d: prompt left alone, a question or menu is up\n", frame);
        }
    }
    waiting = found > 0;
    if (autoTextTick(&padAutoText, speed, waiting, key, chars)) {
        padAutoTextBit = padLogicalIndexToRaw(TEXT_WAIT_BUTTONS[0]);
        printf("autotext %d: page of %d chars waited %d frames -> confirm\n", frame, chars,
               autoTextDelayFrames(speed, chars));
    }
}

/* Tap the picture to turn a page (android T7c phase 1b): the touch layer is told every frame whether a plain page
   waits; a tap that answered it presses Confirm for this frame, through the same raw word Auto text uses. The
   `taptext` token makes the harness tap the picture's middle for every page that waits (a desktop proof). */
static int padTapTextHarness;
static int padTapTextKey;
static int padTapMenuQueue[16][2];
static int padTapMenuCount, padTapMenuNext, padTapMenuWait, padTapMenuOpened;

static void padTouchTextFrame(int frame) {
    int key = 0, chars = 0, tkey = 0;
    int found = padWaitingPage(&key, &chars);
    int typing = found == 0 ? padTypingPage(&tkey) : 0;
    int kind = found > 0 ? TT_WAITING : (typing > 0 ? TT_TYPING : TT_NONE);

    touchTextArm(kind, kind == TT_TYPING ? tkey : key);
    if (padTapTextHarness && kind != TT_NONE && padTapTextKey != (kind == TT_TYPING ? tkey : key) * 4 + kind) {
        padTapTextKey = (kind == TT_TYPING ? tkey : key) * 4 + kind;
        printf(kind == TT_TYPING ? "touchtext %d: harness tap on a page being typed\n"
                                 : "touchtext %d: harness tap on a page of %d chars\n", frame, chars);
        touchHarnessTap(160, 120);
    } else if (found < 0 && padTapTextHarness && padTapTextKey != key) {
        padTapTextKey = key;
        printf("touchtext %d: harness tap while a question is up\n", frame);
        touchHarnessTap(160, 120);
    }
    if (touchTextTake()) {
        padAutoTextBit |= padLogicalIndexToRaw(TEXT_WAIT_BUTTONS[0]);
        if (kind == TT_TYPING) {
            printf("touchtext %d: tap on the picture -> finish the page being typed\n", frame);
        } else {
            printf("touchtext %d: tap on the picture -> confirm (page of %d chars)\n", frame, chars);
        }
    }
}

/* `tapmenu:<x>x<y>` (repeatable): once the options menu is open and drawn, taps the frame point (0..319 x 0..239),
   one tap every 20 frames in the order given (a desktop proof of the menu's touch layer) */
static void padTapMenuFrame(void) {
    if (padTapMenuNext >= padTapMenuCount || !csIsOpen()) {
        return;
    }
    if (!padTapMenuOpened) {
        padTapMenuOpened = 1;
        padTapMenuWait = -40; /* the panel opens first: a tap needs it drawn */
    }
    if (++padTapMenuWait < 20) {
        return;
    }
    padTapMenuWait = 0;
    printf("touchmenu: harness tap at %d,%d\n", padTapMenuQueue[padTapMenuNext][0], padTapMenuQueue[padTapMenuNext][1]);
    touchHarnessTap(padTapMenuQueue[padTapMenuNext][0], padTapMenuQueue[padTapMenuNext][1]);
    padTapMenuNext++;
}

/*
 * The script's schedule: wait until the country select's menu is in the
 * substate that reads the pad (MENU_SELECT), then run the steps in order. A key
 * step presses its key for PAD_KEY_HOLD frames with PAD_KEY_GAP frames in
 * between; a barrier holds until its condition holds (see padBarrierReady).
 * Waiting for the game's own state instead of guessing a frame is what makes
 * the demo independent of how long the panels take to open and how long the
 * title screen's intro runs.
 */
static void padRunScript(void) {
    PadStep *step;

    if (padPhase == 0) {
        CountryMenuTask *menu = padMenuTask();

        if (menu == NULL || menu->state != TASK_RUN || menu->substate != MENU_SELECT) {
            return;
        }
        printf("pad: the menu accepts input; scripting %d step(s), %d press(es)\n",
               padStepCount, padKeyTotal);
        padPhase = 1;
        padTimer = PAD_START_DELAY;
        padIndex = 0;
        padBarrierMode = -1;
        return;
    }
    if (padPhase == 3) {
        return;
    }
    if (padIndex >= padStepCount) {
        padPhase = 3;
        return;
    }

    step = &padSteps[padIndex];
    /* The field's exact dialog drain (B_kindTalk, field-transit): one press
       per marker the engine itself holds open, whatever else runs in
       parallel (walks, anims, music). The marker's own step says which
       button answers (TEXT_WAIT_BUTTONS), so every press is the engine's
       own request, and one press releases every window waiting on that
       button the same frame, because they all read the same pressed edge. */
    if (step->kind == B_kindTalk) {
        TextWindow *win;
        int paused;
        int seen;
        int kind;

        if (padPhase == 2) {
            /* The press is down: hold it, then release and wait out the
               exact inter-press gap before the next marker. */
            if (--padTimer > 0) {
                return;
            }
            padFakeKey(step->keyBit, 0);
            padPhase = 1;
            padTimer = PAD_KEY_GAP;
            return;
        }
        if ((GAME.funcs.getMode() >> 8) != 2) {
            /* The chain left the field: the drain is done. */
            printf("pad: dialog drain done after %d exact press(es) "
                   "(mode 0x%X); the script goes on\n",
                   (int)drainPresses, (int)GAME.funcs.getMode());
            padIndex++;
            padTimer = PAD_KEY_GAP;
            return;
        }
        win = padPausedWindow(&paused, &seen, &kind);
        if (win == NULL) {
            if (padBarrierTimer % 120 == 0) {
                printf("pad: dialog drain waiting (frame %d in this wait): mode 0x%X, "
                       "%d text window(s) in the heap, none paused\n",
                       (int)padBarrierTimer,
                       (int)GAME.funcs.getMode(), seen);
                padPrintWindowStates();
            }
            if (++padBarrierTimer >= PAD_BARRIER_TIMEOUT) {
                fprintf(stderr,
                        "pad: dialog drain waited %d frames with no dialog pause\n"
                        "      while mode 0x%X; the chain is stuck without input;\n"
                        "      the script stops\n",
                        (int)padBarrierTimer,
                        (int)GAME.funcs.getMode());
                padPhase = 3;
            }
            return;
        }
        if (padTimer > 0) {
            padTimer--;
            return;
        }
        padBarrierTimer = 0;
        if (kind == PAD_PAUSE_MARKER) {
            step->keyBit = padLogicalIndexToRaw(TEXT_WAIT_BUTTONS[win->step]);
            printf("pad: dialog pause %d: marker step %d asks its own button "
                   "(%d paused, %d window(s) in heap)\n",
                   (int)(drainPresses + 1), (int)win->step, paused, seen);
        } else {
            /* The box's own page advance: updateTalkBox's runtime case
               shows the page on cross (talk_box.c). */
            step->keyBit = padLogicalIndexToRaw(PAD_CROSS);
            printf("pad: dialog page %d: the talk box shows its page on cross "
                   "(marked pause 0 of this walk)\n",
                   (int)(drainPresses + 1));
        }
        drainPresses++;
        padFakeKey(step->keyBit, 1);
        padPhase = 2;
        padTimer = PAD_KEY_HOLD;
        return;
    }

    /* The player-name screen's exact drive (B_kindName, T5): every press
       answers a state the engine itself holds, with the numbers from the
       decomp (welcome.c's cross -> showPage on the paused window,
       updateKeyboard's typing substate, choice.c's cross -> 100 and
       confirm.c's Yes/No substates). The keyboard's name buffer starts
       pre-filled with the game's own default name (the screen itself got
       it), so no letters are typed: Start parks the cursor on the END cell
       (updateKeyboard parks it at row 6, column 13, NAME_KEY_END) and the
       cross accepts, as a player keeping the suggested name does. Nothing
       here reads timing or guesses. */
    if (step->kind == B_kindName) {
        Task *screen;
        Task *welcome;
        Task *name;
        Task *choice;
        Task *confirm;
        Task *win;
        NameEntry *entry;
        PartnerChoice *pick;
        NameConfirm *yesno;

        if (padPhase == 2) {
            /* The press is down, same hold/release/gap shape as the drain. */
            if (--padTimer > 0) {
                return;
            }
            padFakeKey(step->keyBit, 0);
            padPhase = 1;
            padTimer = PAD_KEY_GAP;
            return;
        }
        if ((GAME.funcs.getMode() >> 8) != 5) {
            printf("pad: name drive done after %d exact press(es) (mode 0x%X); "
                   "the script goes on\n",
                   (int)namePresses, (int)GAME.funcs.getMode());
            padIndex++;
            padTimer = PAD_KEY_GAP;
            return;
        }
        screen = padPlayerNameScreen();
        welcome = padNameChild(screen, STPLNMET_updateWelcome);
        name = padNameChild(screen, STPLNMET_updateNameEntry);
        choice = padNameChild(screen, STPLNMET_updateChoice);
        confirm = padNameChild(screen, STPLNMET_updateConfirm);
        if (welcome == NULL && name == NULL) {
            /* The screen is loading its files or between its members. */
            if (padBarrierTimer % 120 == 0) {
                printf("pad: name drive waiting (frame %d in this wait): mode 0x%X, "
                       "welcome %d name %d\n",
                       (int)padBarrierTimer, (int)GAME.funcs.getMode(),
                       welcome != 0, name != 0);
            }
            if (++padBarrierTimer >= PAD_BARRIER_TIMEOUT) {
                fprintf(stderr,
                        "pad: name drive waited %d frames with no welcome dialog;\n"
                        "      and no keyboard while mode 0x%X; the script stops\n",
                        (int)padBarrierTimer, (int)GAME.funcs.getMode());
                padPhase = 3;
            }
            return;
        }
        entry = (NameEntry *)name;
        pick = (PartnerChoice *)choice;
        yesno = (NameConfirm *)confirm;
        if (welcome != NULL) {
            /* The welcome dialog's own update shows the page on ANY cross
               press while its text is not finished (welcome.c, case 2), so
               a page earns a press exactly like the field's talk-box pages:
               a marker pause (state 2, substate 1) or a page still waiting
               to be fully shown (state 1, visible, not finished). A finished
               window earns nothing: the dialog closes itself (substate 3
               after isFinished). */
            win = NULL;
            if (welcome->children != NULL && welcome->childCount > 0) {
                win = ((Task **)welcome->children)[0];
            }
            if (win != NULL) {
                TextWindow *text = (TextWindow *)win;

                if ((text->state == 2 && text->substate == 1) ||
                    (text->state == 1 && text->visible != 0 &&
                     text->finished == 0)) {
                    if (padTimer > 0) {
                        padTimer--;
                        return;
                    }
                    padBarrierTimer = 0;
                    step->keyBit = padLogicalIndexToRaw(PAD_CROSS);
                    printf("pad: name welcome page %d: the dialog's window asks for its page\n",
                           (int)(namePresses + 1));
                    namePresses++;
                    padFakeKey(step->keyBit, 1);
                    padPhase = 2;
                    padTimer = PAD_KEY_HOLD;
                }
            }
            return;
        }
        if (name != NULL) {
            if (entry->substate == 2 && entry->active != 0) {
                if (padTimer > 0) {
                    padTimer--;
                    return;
                }
                padBarrierTimer = 0;
                /* The keyboard starts pre-filled with the name the screen
                   itself got (STPLNMET_createNameEntry(GAME.name),
                   src/menus/player_name/name_entry.c:49; the shared
                   set_name.inc.c converts it and pads the rest), so the
                   buffer is already non-empty and the retail state
                   machine's END cell accepts it with no typed letters.
                   The drive therefore never types: it parks the cursor on
                   END and accepts, exactly what a player who keeps the
                   suggested name does. (An earlier revision typed two
                   letters first; run 17 showed the buffer pre-filled and
                   the drive left "AA" prefixes on the name plate of every
                   later dialog.) */
                if (entry->column != 13 || entry->row != 6) {
                    /* Start parks the keyboard cursor at NAME_KEY_END. */
                    step->keyBit = padKeyBit("start");
                    printf("pad: name end key: Start parks the cursor at the "
                           "END cell (from column %d row %d)\n",
                           (int)entry->column, (int)entry->row);
                    namePresses++;
                } else {
                    /* Cross on END: a non-empty name leaves the keyboard at
                       substate 100 with no confirmation yet. */
                    step->keyBit = padLogicalIndexToRaw(PAD_CROSS);
                    printf("pad: name accept %d: cross on END leaves the "
                           "keyboard (substate 100)\n",
                           (int)(namePresses + 1));
                    namePresses++;
                }
                padFakeKey(step->keyBit, 1);
                padPhase = 2;
                padTimer = PAD_KEY_HOLD;
                return;
            }
            if (entry->substate == 100) {
                /* The keyboard is done: first the partners' page (cross
                   confirms the highlighted set, choice.c), then the Yes/No
                   screen whose cursor rests on Yes. */
                if (choice != NULL && pick->substate == 0) {
                    if (padTimer > 0) {
                        padTimer--;
                        return;
                    }
                    padBarrierTimer = 0;
                    step->keyBit = padLogicalIndexToRaw(PAD_CROSS);
                    printf("pad: partners %d: cross keeps choice %d (substate "
                           "100)\n",
                           (int)(namePresses + 1), (int)pick->choice);
                    namePresses++;
                    padFakeKey(step->keyBit, 1);
                    padPhase = 2;
                    padTimer = PAD_KEY_HOLD;
                    return;
                }
                if (choice != NULL && pick->substate == 100 &&
                    confirm != NULL && yesno->substate == 0) {
                    if (padTimer > 0) {
                        padTimer--;
                        return;
                    }
                    padBarrierTimer = 0;
                    step->keyBit = padLogicalIndexToRaw(PAD_CROSS);
                    printf("pad: name confirm %d: cross on Yes (cursor %d)\n",
                           (int)(namePresses + 1), (int)yesno->cursor);
                    namePresses++;
                    padFakeKey(step->keyBit, 1);
                    padPhase = 2;
                    padTimer = PAD_KEY_HOLD;
                    return;
                }
            }
            /* Every other keyboard state (twens, the empty-name message,
               the closing tween) advances by itself. */
            return;
        }
        return;
    }

    /* A B_kindUntil step re-presses its key until the game's own state says
       the key took effect (see the struct's note). */
    if (step->kind == B_kindUntil) {
        if (step->cond == C_modeChanged && padBarrierMode == -1) {
            padBarrierMode = GAME.funcs.getMode();
        }
        if (padCondReady(step->cond)) {
            padFakeKey(step->keyBit, 0);
            printf("pad: step %d's key took effect (mode 0x%X); the script goes on\n",
                   (int)(padIndex + 1), (int)GAME.funcs.getMode());
            padBarrierMode = -1;
            padIndex++;
            padTimer = PAD_KEY_GAP;
            return;
        }
    }
    if (step->kind != B_kindNone && step->kind != B_kindUntil) {
        if (step->kind == B_kindMode && padBarrierMode == -1) {
            padBarrierMode = GAME.funcs.getMode();
            padBarrierTimer = 0;
            return;
        }
        if (!padBarrierReady(step->kind)) {
            if (++padBarrierTimer >= PAD_BARRIER_TIMEOUT) {
                fprintf(stderr, "pad: barrier %d waited %d frames and the game is still mode 0x%X;\n"
                                "      the script stops\n",
                        (int)(padIndex + 1), (int)padBarrierTimer, (int)GAME.funcs.getMode());
                padPhase = 3;
            }
            return;
        }
        printf("pad: barrier %d is satisfied (mode 0x%X); the script goes on\n",
               (int)(padIndex + 1), (int)GAME.funcs.getMode());
        padBarrierMode = -1;
        padIndex++;
        padTimer = PAD_KEY_GAP;
        return;
    }

    if (--padTimer > 0) {
        return;
    }
    if (padPhase == 1) {
        int ordinal = 0;
        int k;

        for (k = 0; k <= padIndex; k++) {
            if (padSteps[k].kind == B_kindNone || padSteps[k].kind == B_kindUntil) {
                ordinal++;
            }
        }
        printf("pad: press %d of %d (step %d)\n", ordinal, padKeyTotal, (int)(padIndex + 1));
        padFakeKey(step->keyBit, 1);
        padPhase = 2;
        padTimer = PAD_KEY_HOLD;
        return;
    }
    padFakeKey(step->keyBit, 0);
    /* A B_kindUntil step stays where it is and presses again after the gap;
       every other step moves on. */
    if (step->kind != B_kindUntil) {
        padIndex++;
    }
    padPhase = 1;
    padTimer = PAD_KEY_GAP;
}

/* O4: is this SEP sequence the game's music? playSound (src/engine/sound/sound.c)
   stores an exclusive sound in SOUND.music before it starts its sequence
   (bank, SEQ and track in the packed id, engine/sound.h); every other
   sequence (the gondola, the elevator...) is a sound effect. snd_sep.c asks at
   each SsSepPlay and keeps the answer for that play. */
static int padSoundIsMusic(short seqId, short track) {
    s32 m = SOUND.music;
    s32 slot, seq;

    if (m == 0 || SOUND_IS_KEY_ON(m)) {
        return 0;
    }
    slot = findSoundBank(SOUND_BANK(m));
    seq = SOUND_SEQ(m);
    if (slot < 0 || seq < 0 || seq >= 4) {
        return 0;
    }
    return SOUND.banks[slot].seqs[seq] == seqId && SOUND_SEP(m) == track;
}

void pcBootPadInit(const char *script) {
    const char *scan;

    /* controls-config C1: controls.ini and gamecontrollerdb.txt, over the
       defaults PsyX_Initialise just set (boot.c calls this right after it) */
    csSettingsInstall(); /* O1: the menu's rows share the file */
    inputCfgInit();
    optionsVideoInit(); /* O2: the Video and Display rows; the file's values apply now */
    inputDeviceRowsInit(); /* C4: the Button icons row (Display) */
    touchRowsInit(); /* android T7a: the Touch controls, opacity and size rows (Display) */
    optionsLanguageInit(optLangSystem(), tdEs419Available()); /* O3: the Language row; the file's value applies now */
    optionsAudioInit(); /* O4: the Audio rows; the file's volumes apply now */
    sndSepSetMusicQuery(padSoundIsMusic);

    /*
     * The window is deliberately NOT raised and NOT focused (T21): doing that
     * takes the owner's keyboard away from whatever he is doing, and opening a
     * window is allowed only if it stays out of the way. Interactively, click
     * the window once and the keyboard goes there, the way any window works.
     */
    if (script == NULL) {
        printf("pad: interactive; click the window to give it the keyboard\n");
        return;
    }
    padWarpFlag = -1;
    padWarpProgress = -1; /* a warp token without `,progress` keeps the game's own */

    scan = script;
    while (*scan != '\0' && padStepCount < PAD_SCRIPT_MAX) {
        char token[16];
        int n = 0;

        while (*scan == ',' || *scan == ' ') {
            scan++;
        }
        /* The battle schedule (battle-entry T7, see padBattleSchedule's
           comment): parsed straight from the scan before the comma split,
           because its press list owns the commas the split would eat -- the
           same reason the warp's progress reads on past the split. */
        if (*scan == 'b' && padTokenStarts(scan, "battle@")) {
            const char *p = scan + 7; /* past "battle@" */
            s32 rel = 0;
            int scheduleFrom = padBattleScheduleCount;

            while (*p >= '0' && *p <= '9') {
                rel = rel * 10 + (*p - '0');
                p++;
            }
            if (*p != ':') {
                fprintf(stderr,
                        "pad: the battle schedule 'battle@' has no "
                        "':<key>' press list; the token is a no-op\n");
                while (*p != '\0' && *p != ',') {
                    p++;
                }
            } else {
                p++; /* past ':' */
                for (;;) {
                    char key[16];
                    int nameLen = 0;

                    while ((p[nameLen] >= 'a' && p[nameLen] <= 'z') ||
                           (nameLen > 0 && p[nameLen] >= '0' && p[nameLen] <= '9')) {
                        nameLen++; /* digits after a letter: l1, r1, l2, r2 */
                    }
                    if (nameLen == 0 || nameLen >= (int)sizeof(key)) {
                        if (*p != '\0' && *p != ',') {
                            fprintf(stderr,
                                    "pad: the battle schedule's key '%.*s' "
                                    "is not a PAD_KEYS name; it is skipped\n",
                                    (int)sizeof(key) - 1, p);
                            while (*p != '\0' && *p != ',') {
                                p++;
                            }
                        }
                    } else {
                        u16 bit;

                        for (n = 0; n < nameLen; n++) {
                            key[n] = p[n];
                        }
                        key[nameLen] = '\0';
                        bit = padKeyBit(key);
                        p += nameLen; /* the press list reads past the name */
                        if (bit == 0) {
                            fprintf(stderr,
                                    "pad: unknown battle schedule key '%s'\n",
                                    key);
                        } else if (padBattleScheduleCount <
                                   PAD_BATTLE_SCHEDULE_MAX) {
                            PadBattlePress *press =
                                &padBattleSchedule[padBattleScheduleCount++];

                            press->name[0] = '\0';
                            for (n = 0; n < nameLen; n++) {
                                press->name[n] = key[n];
                            }
                            press->name[nameLen] = '\0';
                            press->bit = bit;
                            press->rel = (int)rel;
                            press->hold = PAD_KEY_HOLD;
                        } else {
                            fprintf(stderr,
                                    "pad: the battle schedule is full (%d "
                                    "presses); '%s' is dropped\n",
                                    PAD_BATTLE_SCHEDULE_MAX, key);
                        }
                    }
                    /* The hold frames or the next key come after a comma:
                       digits are the previous key's hold, a name the next
                       press. Then the scan stops on anything else. */
                    if (*p != ',') {
                        break;
                    }
                    p++;
                    if (*p >= '0' && *p <= '9') {
                        s32 hold = 0;

                        while (*p >= '0' && *p <= '9') {
                            hold = hold * 10 + (*p - '0');
                            p++;
                        }
                        if (padBattleScheduleCount > 0) {
                            padBattleSchedule[padBattleScheduleCount - 1]
                                .hold = (int)hold;
                        }
                        if (*p != ',') {
                            break;
                        }
                    }
                    if (*p == ',') {
                        p++; /* the next press follows this separator */
                    }
                }
                if (*p == ',') {
                    p++;
                }
                if (padBattleScheduleCount == 1) {
                    printf("pad: battle schedule armed: 1 press, first %d "
                           "frame(s) after the fire\n",
                           (int)rel);
                } else if (padBattleScheduleCount > 1) {
                    printf("pad: battle schedule armed: %d presses, first %d "
                           "frame(s) after the fire\n",
                           padBattleScheduleCount, (int)rel);
                }
            }
            /* battle@ rides the bare battle's fire (the real battle) when a
               `battle`/`battle:N` token has not; the no-op above does not
               arm: the token said nothing to press (scheduleFrom is the
               presses this token itself added) */
            if (padBattleScheduleCount > scheduleFrom &&
                padBattleArmed == 0 && padModeArmed == 0 && padWarpStage == 0) {
                padBattleArg = 0;
                padBattleArmed = 1;
                padBattleImplicit = 1;
                padUntilImplied(MODE_BATTLE);
            }
            scan = p;
            continue;
        }
        while (*scan != '\0' && *scan != ',' && n < (int)sizeof(token) - 1) {
            token[n++] = *scan++;
        }
        token[n] = '\0';
        if (n == 0) {
            continue;
        }
        if (padTokenIs(token, "wait")) {
            /* a barrier: hold until the game's own mode changes (T18) */
            padAddStep(0, B_kindMode, C_none);
            continue;
        }
        if (padTokenIs(token, "waittitle")) {
            /* a barrier: hold until the title menu waits for Start (T19) */
            padAddStep(0, B_kindTitle, C_none);
            continue;
        }
        if (padTokenIs(token, "waitoptions")) {
            /* a barrier: hold until the title menu navigates its options (T19) */
            padAddStep(0, B_kindOptions, C_none);
            continue;
        }
        if (padTokenIs(token, "skipmovie")) {
            /* press Start until the game leaves the movie's mode (T19): the
               movie player does not finish on its own in this build, so a
               dropped key event would stall the script */
            padAddStep(padKeyBit("start"), B_kindUntil, C_modeChanged);
            continue;
        }
        if (padTokenIs(token, "startuntil")) {
            /* press Start until the title menu leaves "PRESS START" (T19) */
            padAddStep(padKeyBit("start"), B_kindUntil, C_titleLeftStart);
            continue;
        }
        if (padTokenIs(token, "newgame")) {
            /* press Up until the title menu highlights NEW GAME (T19) */
            padAddStep(padKeyBit("up"), B_kindUntil, C_titleNewGame);
            continue;
        }
        if (padTokenIs(token, "confirm")) {
            /* press cross until the game leaves the title's mode (T19) */
            padAddStep(padKeyBit("cross"), B_kindUntil, C_modeLeftTitle);
            continue;
        }
        if (padTokenIs(token, "field")) {
            /* a barrier: hold until the game is in a field mode with the
               player's actor (the field-transit milestone) */
            padAddStep(0, B_kindField, C_none);
            continue;
        }
        if (padTokenIs(token, "talkdrain")) {
            /* the field's exact dialog drain (field-transit): one press per
               marker the engine holds, with the marker's own button; ends
               when the chain leaves the field page */
            padAddStep(padKeyBit("cross"), B_kindTalk, C_none);
            continue;
        }
        if (padTokenIs(token, "namedrive")) {
            /* the player-name screen's exact drive (T5): welcome pages,
               Start + END (the buffer starts pre-filled with the game's
               default name), partners and Yes; ends when the mode leaves
               the name entry */
            padAddStep(padKeyBit("cross"), B_kindName, C_none);
            continue;
        }
        if (padTokenStarts(token, "fieldbattles:")) {
            padFieldBattles = (int)padParseBattleArg(token + 13);
            continue;
        }
        if (padTokenIs(token, "fieldauto")) {
            /* the field's dialog autopilot (padFieldAutoRun), which rides a
               warp's fire */
            padFieldAuto = 1;
            continue;
        }
        if (padTokenIs(token, "fieldexit")) {
            /* `fieldauto` that also presses cross on the trigger of a slot a stage patch gave an area: an exit that is walked into */
            padFieldAuto = 1;
            padFieldExit = 1;
            continue;
        }
        if (padTokenIs(token, "battlepilot")) {
            /* the battle autopilot alone, without the battle's fire: for a
               run whose own field starts the fight (warp + fieldauto) */
            padBattleAuto = 1;
            continue;
        }
        if (padTokenIs(token, "settle") || padTokenStarts(token, "settle:")) {
            /* the schedule's presses wait for the screen to settle
               (the game's own pad reads): menu-modes M5's state-driven walk */
            padSettleGate = 1;
            if (token[6] == ':' && token[7] >= '0' && token[7] <= '9') {
                padSettleFrames = (int)padParseBattleArg(token + 7); /* `settle:<frames>`: a longer
                                                                       quiet for screens whose
                                                                       animations idle the tasks */
            }
            continue;
        }
        if (padTokenIs(token, "cardauto") || padTokenStarts(token, "cardauto:")) {
            /* the card battle autopilot (menu-modes M6b): the mode's fire
               with the opponent's arg (`cardauto:<arg>`, default 3: Asuka
               City's first opponent), then a player that reads the duel's
               state (padCardDecide). The run ends when the mode closes. */
            padModeValue = MODE_CARD_GAME;
            padModeArg = token[8] == ':' ? (s32)padParseBattleArg(token + 9) : padCardArg;
            padModeArmed = 1;
            padCardAuto = 1;
            if (padBattleImplicit != 0) {
                padBattleArmed = 0;
                padBattleImplicit = 0;
            }
            padUntilImplied(MODE_CARD_GAME);
            continue;
        }
        if (token[0] == 'b' && padTokenStarts(token, "battleenc:")) {
            /* `battleenc:<n>[:<stage>[:<music>]]`: the real battle with
               FIELDSTG_encounters[n] (padBattleFire) */
            const char *q = token + 10;
            s32 v[3] = { -1, padEncStage, padEncMusic };
            int k;

            for (k = 0; k < 3; k++) {
                s32 num = 0;
                int digits = 0;

                while (*q >= '0' && *q <= '9') {
                    num = num * 10 + (*q - '0');
                    q++;
                    digits++;
                }
                if (digits == 0) {
                    break;
                }
                v[k] = num;
                if (*q != ':') {
                    break;
                }
                q++;
            }
            if (v[0] < 0 || *q != '\0' || !padEncounterValid(v[0])) {
                fprintf(stderr, "pad: the battleenc token '%s' names no encounter of "
                        "FIELDSTG_encounters; the token is a no-op\n", token);
                continue;
            }
            padEncIndex = v[0];
            padEncStage = v[1];
            padEncMusic = v[2];
            padBattleArg = 0;
            padBattleArmed = 1;
            padUntilImplied(MODE_BATTLE);
            continue;
        }
        if (padTokenIs(token, "battleauto")) {
            /* the battle autopilot: the real battle's fire, then a player
               that reads the menus' state (padBattleAutoRun) */
            padBattleArg = 0;
            padBattleArmed = 1;
            padBattleAuto = 1;
            padUntilImplied(MODE_BATTLE); /* the run ends when the fight is left */
            continue;
        }
        if (padTokenIs(token, "battle") ||
            (token[0] == 'b' && padTokenStarts(token, "battle:"))) {
            /* the battle-entry harness token (T3), armed and fired later
               from the frame hook, not a step (padBattleFire above).
               `battle` and `battle:0` enter the real battle (WFIGHTMN, the
               field's own arg); `battle:1` enters the stage-select's
               WFIGHTTS test (mode Arg 1, retail's シーンテスト entry). */
            padBattleArg = padTokenIs(token, "battle") ? 0
                                                       : padParseBattleArg(token + 7);
            padBattleArmed = 1;
            padUntilImplied(MODE_BATTLE);
            continue;
        }
        if (token[0] == 'm' && padTokenStarts(token, "mode:")) {
            /* `mode:<hex>[,<arg>]`: armed and fired later, not a step. The
               optional decimal arg is read past the comma split like the
               warp's progress. */
            const char *h = token + 5;
            s32 value = 0;

            while ((*h >= '0' && *h <= '9') || (*h >= 'a' && *h <= 'f') ||
                   (*h >= 'A' && *h <= 'F')) {
                value = value * 16 + (*h <= '9' ? *h - '0' : (*h | 0x20) - 'a' + 10);
                h++;
            }
            padModeValue = value;
            padModeArg = 0;
            if (*scan == ',' && padTokenStarts(scan + 1, "save")) {
                /* `mode:C00,save`: STGMCARD's saving arg is negative
                   (stage_select.c's セーブ entry: 0x80000000) */
                padModeArg = (s32)0x80000000;
                scan += 5;
            } else if (*scan == ',' && scan[1] >= '0' && scan[1] <= '9') {
                const char *a = scan + 1;

                while (*a >= '0' && *a <= '9') {
                    padModeArg = padModeArg * 10 + (*a - '0');
                    a++;
                }
                scan = a;
            }
            padModeArmed = (value != 0);
            if (padModeArmed != 0 && padBattleImplicit != 0) {
                padBattleArmed = 0;
                padBattleImplicit = 0;
            }
            if (padModeArmed != 0) {
                padUntilImplied(value); /* the run ends when the mode closes */
            }
            continue;
        }
        if (token[0] == 'u' && padTokenStarts(token, "until:")) {
            /* `until:<hex>`: the explicit end of the run (padUntilRun) */
            padUntilMode = padParseHex(token + 6);
            padUntilExplicit = 1;
            continue;
        }
        if (token[0] == 'm' && padTokenStarts(token, "movieskip:")) {
            padMovieSkipFrames = padParseBattleArg(token + 10);
            continue;
        }
        if (padTokenIs(token, "controls") || (token[0] == 'c' && padTokenStarts(token, "controls:"))) {
            padControlsParse(token[8] == ':' ? token + 9 : token + 8, 0); /* alias: the Controls tab's row */
            continue;
        }
        if (padTokenIs(token, "options") || (token[0] == 'o' && padTokenStarts(token, "options:"))) {
            padControlsParse(token[7] == ':' ? token + 8 : token + 7, 1);
            continue;
        }
        if (token[0] == 'o' && padTokenStarts(token, "optlang:")) {
            /* `optlang:<choice>[:<hex mode>]` (O3b): once the options menu is open, picks the
               Language row's value index (0 English, 1 Francais, 2 Deutsch, 3 Espanol, 4 Italiano, 5 Espanol (Latam),
               the last one only while the row offers it),
               closes the menu, and after the live switch requests <mode> (a harness stand-in for
               the player opening a game screen) */
            const char *a = token + 8;

            padOptLang = 0;
            while (*a >= '0' && *a <= '9') {
                padOptLang = padOptLang * 10 + (*a++ - '0');
            }
            if (a[0] == ':' && a[1] == 'b') {
                padOptLangBattle = 1; /* `:battle`: the real battle after the switch */
            } else if (a[0] == ':' && a[1] == 't') {
                padOptLangTalk = 1; /* `:talk`: walk right and press cross after the switch */
            } else if (*a == ':') {
                padOptLangMode = padParseHex(a + 1);
            }
            if (!padControlsArmed) {
                padControlsParse("3", 1);
            }
            padOptLangStage = 1;
            continue;
        }
        if (padTokenIs(token, "taptext")) {
            padTapTextHarness = 1;
            continue;
        }
        if (token[0] == 't' && padTokenStarts(token, "tapmenu:")) {
            int tx = 0, ty = 0;
            const char *a = token + 8;

            while (*a >= '0' && *a <= '9') {
                tx = tx * 10 + (*a++ - '0');
            }
            if (*a == 'x') {
                a++;
            }
            while (*a >= '0' && *a <= '9') {
                ty = ty * 10 + (*a++ - '0');
            }
            if (padTapMenuCount < 16) {
                padTapMenuQueue[padTapMenuCount][0] = tx;
                padTapMenuQueue[padTapMenuCount][1] = ty;
                padTapMenuCount++;
            }
            continue;
        }
        if (padTokenIs(token, "stay")) {
            /* free play: no implicit end, the window stays open */
            padUntilStay = 1;
            continue;
        }
        if (token[0] == 'p' && padTokenStarts(token, "partner:")) {
            /* `partner:<member>,<form>[,<level>[,<hp%>]]`: padSeedPartner, at the fire of the battle or the mode. The
               script's comma split cut the token after <member>, so <form> and <level> are read from the scan
               (like the warp's progress). */
            const char *q = token + 8;
            s32 v[4] = { 0, 0, 1, 100 };
            int k = 0;
            int ok = 1;

            for (;;) {
                s32 num = 0;
                int digits = 0;

                while (*q >= '0' && *q <= '9') {
                    num = num * 10 + (*q - '0');
                    q++;
                    digits++;
                }
                if (digits == 0 || (*q != '\0' && *q != ',' && *q != ' ')) {
                    ok = 0;
                    break;
                }
                v[k++] = num;
                if (k == 4 || *scan != ',' || scan[1] < '0' || scan[1] > '9') {
                    break;
                }
                q = scan + 1;
                scan = q;
                while (*scan >= '0' && *scan <= '9') {
                    scan++;
                }
            }
            if (!ok || k < 2 || v[0] > 2 || v[1] < 3) {
                fprintf(stderr, "pad: the partner token needs partner:<member 0..2>,<form >= 3>[,<level>[,<hp percent>]]; "
                        "the token is a no-op\n");
                continue;
            }
            padPartnerMember = v[0];
            padPartnerForm = v[1];
            padPartnerLevel = v[2];
            padPartnerHpPercent = (k >= 4 && v[3] >= 1 && v[3] <= 100) ? v[3] : 100;
            continue;
        }
        if (token[0] == 'f' && padTokenStarts(token, "flag:")) {
            /* `flag:<hex>`: the flag the warp sets (padWarpFlag) */
            const char *h = token + 5;
            s32 value = 0;

            while ((*h >= '0' && *h <= '9') || (*h >= 'a' && *h <= 'f') ||
                   (*h >= 'A' && *h <= 'F')) {
                value = value * 16 + (*h <= '9' ? *h - '0' : (*h | 0x20) - 'a' + 10);
                h++;
            }
            padWarpFlag = value;
            continue;
        }
        if (token[0] == 'w' && padTokenStarts(token, "warp:")) {
            /* the warp (debug-warp), armed and fired later, not a step */
            padWarpArm(token);
            if (padWarpStage != 0 && padBattleImplicit != 0) {
                padBattleArmed = 0; /* the schedule rides the warp's fire */
                padBattleImplicit = 0;
            }
            /* its optional `,progress`: the script's own comma split it into
               the next token, so the digits right after the comma are the
               progress and the scan resumes past them */
            if (*scan == ',') {
                const char *p = scan + 1;
                s32 progress = 0;

                while (*p >= '0' && *p <= '9') {
                    progress = progress * 10 + (*p - '0');
                    p++;
                }
                if (p != scan + 1 && (*p == '\0' || *p == ',' || *p == ' ')) {
                    padWarpProgress = progress;
                    scan = p;
                } else {
                    fprintf(stderr, "pad: the warp token's progress is not a number\n");
                }
            }
            continue;
        }
        {
            u16 bit = padKeyBit(token);

            if (bit == 0) {
                fprintf(stderr, "pad: unknown scripted key '%s'\n", token);
                continue;
            }
            padAddStep(bit, B_kindNone, C_none);
            continue;
        }
    }
    if (padStepCount == 0) {
        if (padWarpStage != 0) {
            /* the warp alone: there are no steps to arm as a script, the
               frame hook fires the warp itself */
            return;
        }
        if (padModeArmed != 0 || padControlsArmed != 0) {
            return; /* the mode / controls token alone: the frame hook fires it */
        }
        if (padBattleArmed != 0) {
            /* the battle token alone: the frame hook fires it itself, the
               same lane the warp rides */
            return;
        }
        if (padFieldAuto != 0 || padBattleAuto != 0 || padCardAuto != 0 || padFieldBattles != 0) {
            return; /* the field autopilot alone: the frame hook runs it */
        }
        if (padBattleScheduleCount != 0) {
            /* the battle schedule alone (T7): battle@ armed the fire, the
               frame hook fires and then runs the presses; no steps follow */
            return;
        }
        fprintf(stderr, "pad: the script '%s' names no key\n", script);
        return;
    }
    padScriptArmed = 1;
    printf("pad: scripted pad armed (in process, no XTest): %s\n", script);
}

/*
 * Text speed (field-transit's speed-up for the test chains, reworked in controls-config O6b).
 *
 * Two things, both port side (the game's code is not edited; this reads the same shim heap walk
 * the dialogue drain does):
 *  - the Text speed setting (Off / On) gives every dialogue window (TalkBox windows[1], MessageBox
 *    window; the retail delay is 6, one character per 7 frames) its typing delay from the FIRST
 *    page: Off leaves it alone, On sets the engine's floor of 1. The engine's own typing loop
 *    keeps drawing and playing the type sound; waits (code 2), page breaks and questions work as
 *    before.
 *  - the pressed edge of the dialog button (cross) while a page is typing speeds up ONLY that
 *    page: typeDelay goes to the engine's floor of 1 mid-page (a direct field set, not the
 *    setter, which would restart the page; the typing loop keeps its sound at the new pace), and
 *    the window's delay returns to the setting's value when the next page starts. A page starts
 *    when the window is first seen, its start moves (page break / name code) or it leaves a wait.
 *    The cross press that releases a wait (the player's or auto text's) does not boost the page it
 *    opens: the boost needs the window to have been typing on the frame before too.
 * A paused window (state 2) is not touchable here: releasing it stays the marker's own press.
 * Windows that are not dialogue (banners, list menus) keep the old behaviour: a press sets 1.
 */
#define PAD_TEXT_SLOTS 8
#define PAD_TEXT_RETAIL_DELAY 6 /* talk_box.c:400, message_box.c:150 */
typedef struct PadTextSlot {
    const void *win;
    int state, start, mode;
    int fresh, boosted, changed, seen;
} PadTextSlot;
static PadTextSlot padTextSlots[PAD_TEXT_SLOTS];

static PadTextSlot *padTextSlotFor(const void *win) {
    int i;

    for (i = 0; i < PAD_TEXT_SLOTS; i++) {
        if (padTextSlots[i].win == win) {
            return &padTextSlots[i];
        }
    }
    for (i = 0; i < PAD_TEXT_SLOTS; i++) {
        if (padTextSlots[i].win == NULL) {
            padTextSlots[i] = (PadTextSlot){0};
            padTextSlots[i].win = win;
            padTextSlots[i].fresh = 1;
            padTextSlots[i].mode = -1;
            return &padTextSlots[i];
        }
    }
    return NULL;
}

static void padFastTextOnButtonEdge(void) {
    void *block;
    const void *owners[PAD_AUTO_TEXT_OWNERS];
    int cursor, count, i;
    int mode = textSpeedMode();
    int pressed = (PAD.getPressed(0) >> PAD.getButtonBit(0, PAD_CROSS)) & 1;

    count = padDialogueWindows(owners, PAD_AUTO_TEXT_OWNERS, &cursor);
    for (i = 0; i < PAD_TEXT_SLOTS; i++) {
        padTextSlots[i].seen = 0;
    }
    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);
        TextWindow *win;
        PadTextSlot *slot = NULL;
        int dialogue = 0;

        if (pcHeapBlockTag(block) != MEM_MODE || task->update != updateTextWindow) {
            continue;
        }
        win = (TextWindow *)task;
        for (i = 0; i < count; i++) {
            dialogue |= owners[i] == (const void *)win;
        }
        if (dialogue) {
            slot = padTextSlotFor(win);
        }
        if (slot == NULL) {
            /* not a dialogue window: a press while typing sets the floor, for good */
            if (pressed && win->state == 1 && win->visible != 0 && win->typeDelay > 1) {
                win->typeDelay = 1;
            }
            continue;
        }
        slot->seen = 1;
        if (textSpeedPageStart(slot->state, win->state, slot->start, win->start, slot->fresh) ||
            (win->state == 1 && slot->mode != mode)) {
            int want = textSpeedDelay(mode, PAD_TEXT_RETAIL_DELAY);

            slot->fresh = 0;
            slot->boosted = 0;
            slot->mode = mode;
            if (mode != TEXT_SPEED_OFF || slot->changed) {
                win->typeDelay = want;
                slot->changed = mode != TEXT_SPEED_OFF;
            }
        } else if (textSpeedBoost(slot->state, win->state, pressed) && win->typeDelay > 1) {
            /* The retail text sound comes from the window's own typing loop (state 1 calls
               SOUND.playSound with the typeSound per typed character at the window's delay);
               snapping the page with textWindowSetTypeDelay(win, 0) silenced it, so only the
               pace goes to the floor, mid-page, without resetting the visible text */
            win->typeDelay = textSpeedPageDelay(mode, PAD_TEXT_RETAIL_DELAY, 1);
            slot->boosted = 1;
            slot->changed = 1;
        }
        slot->state = win->state;
        slot->start = win->start;
    }
    for (i = 0; i < PAD_TEXT_SLOTS; i++) {
        if (!padTextSlots[i].seen) {
            padTextSlots[i].win = NULL; /* the window is gone */
        }
    }
}

/*
 * The Language row's lane (controls-config O3): while the game is in the country select
 * (the first mode, or a soft reset back to it) a chosen language is written to LANGUAGE,
 * which is what the menu's Start does. With nothing scripted the menu is skipped too: the
 * mode its exit would request is requested here, after the same font reload the other
 * lanes do (CNTY_SEL_start wiped VRAM). The scripted lanes keep the menu (their keys walk
 * it) or ride their own first mode, but still get the language.
 */
/* Latched on the run's first frame, before the warp/battle/mode lanes fire and spend their
   flags: a run with any of them is scripted for the language lane too (else the warp fired
   first and the skip then requested the opening over it) */
static int padScriptedRun;
static void padLatchScripted(void) {
    static int done;

    if (done) {
        return;
    }
    done = 1;
    padScriptedRun = padScriptArmed || padWarpStage != 0 || padBattleArmed != 0 || padModeArmed != 0 ||
                     padControlsArmed != 0 || padFieldAuto != 0 || padBattleAuto != 0 || padCardAuto != 0 ||
                     padBattleScheduleCount != 0 || padFieldBattles != 0;
}

static void padSetTextVariant(int choice) {
    tdSetVariant(optLangVariant(choice) ? TD_VARIANT_ES419 : TD_VARIANT_SPA);
}

static void padLanguageFire(void) {
    static int entered = 1; /* once per visit: the menu's own pick must stand afterwards */
    static int bootDone;    /* the run's first language write happened (a warp's pending request included) */
    int choice = optLangEffective(optLangChoice(), optLangEs419Offered());
    int language = optLangGameValue(choice);
    int scripted = padScriptedRun || padScriptArmed || padWarpStage != 0 || padBattleArmed != 0 ||
                   padModeArmed != 0 || padControlsArmed != 0 || padFieldAuto != 0 || padBattleAuto != 0 ||
                   padCardAuto != 0 || padBattleScheduleCount != 0;
    s32 mode = GAME.funcs.getMode();
    int inCountrySelect = mode == MODE_COUNTRY_SELECT;
    /* The title's NEW GAME and CONTINUE call newGame() (title.c:16/24), which makes the country
       select the current mode on the spot and then request the real next mode: that is not a
       visit to the menu, and a skip here would replace the title's request with the opening.
       A warp's request is pending on the run's first frame too (it fires before this lane): that
       one still gets the language, without the skip (optLangLane) */
    int pending = inCountrySelect && GAME.funcs.isModeChangePending();
    int lane = optLangLane(inCountrySelect, pending, entered, bootDone, choice, scripted);

    if (!inCountrySelect) {
        entered = 1;
    }
    if (lane == OPT_LANG_LANE_NONE) {
        return;
    }
    bootDone = 1;
    if (inCountrySelect && !pending) {
        entered = 0;
    }
    LANGUAGE = language;
    padSetTextVariant(choice); /* before any text file loads */
    if (lane == OPT_LANG_LANE_SET_SKIP) {
        FONT.load();
        GAME.funcs.requestMode(optLangOpening(language) == 1 ? MODE_OPENING : MODE_OPENING_2, 0);
        printf("pad: language %d (%s) from the settings, the language menu is skipped\n", language,
               optLangVariant(choice) ? "es419" : "disc");
    } else if (pending) {
        printf("pad: language %d (%s) from the settings, under a request already made (warp)\n", language,
               optLangVariant(choice) ? "es419" : "disc");
    }
}

/*
 * The live language switch (controls-config O3b, es419-live-option L2c). The Language row only
 * records the choice; once the options menu is closed this applies it where the scene can refresh
 * in place (optLangLive): the field, with nothing holding the player, gets LANGUAGE and its stage
 * text file moved to the new language (FIELDSTG_state.textFile is LANGUAGE + base, set once at the
 * stage's setup; every other text read there adds LANGUAGE at the time of use and FILE_CACHE is
 * keyed by the file index, so the new language simply loads on first use); the title restarts, as
 * it picks its art by LANGUAGE at start. Other scenes keep their language until the game is back in
 * the field.
 *
 * es-419 is a variant of LANGUAGE 6, so Espanol <-> es-419 (and any switch that leaves or enters
 * es-419) also changes the Spanish text files behind the same cache indices: the variant is set and
 * every cached Spanish text file is freed, then the stage's text file is requested again. The
 * switch waits while a Spanish file is still being read (the cache keeps a loading slot). Text is
 * read at the time of use (FILE_CACHE.load/getEntry inside each screen, setString copies into the
 * window), speech.c:83 takes its pointer when a talk starts (the idle gate rules a talk out) and
 * there is no static table of text pointers; what keeps old text until it is built again: the
 * party names copied into GAME by newGame (party.c: the player and partner names, deck names),
 * and windows already open in a scene that waits (battle, shops: they switch on the way back to the
 * field).
 */
static int padLiveFrame; /* the harness frame, for the log lines */

static int padSpanishTextLoading(void) {
    int i, lang, k;

    for (i = 0; i < FILE_CACHE_SLOTS; i++) {
        FileSlot *slot = &FILE_CACHE.slots[i];

        if (slot->file != 0 && slot->state != FILE_LOADED && tdFromIndex(slot->file, &lang, &k) && lang == TD_SPA) {
            return 1;
        }
    }
    return 0;
}

static void padFreeSpanishText(void) {
    int k;

    for (k = 0; k < TD_FILES; k++) {
        FILE_CACHE.free(tdIndexOf(TD_SPA, k));
    }
}

static void padLanguageLive(void) {
    s32 mode = GAME.funcs.getMode();
    int idle = FIELDSTG_state.frozen == 0 && FIELDSTG_state.menuOpen == 0 && FIELDSTG_state.busy == 0 &&
               FIELDSTG_state.battleStarting == 0;
    int choice = optLangEffective(optLangChoice(), optLangEs419Offered());
    int variant = tdGetVariant() == TD_VARIANT_ES419;
    int kind = optLangLive(optLangPending(), choice, (int)LANGUAGE, variant, (int)mode, idle, csIsOpen());
    int target = optLangGameValue(choice);
    int from = (int)LANGUAGE;
    int variantChange = optLangVariant(choice) != variant;

    if (kind == OPT_LANG_LIVE_WAIT || (kind != OPT_LANG_LIVE_NONE && variantChange && padSpanishTextLoading())) {
        return;
    }
    optLangClearPending();
    if (kind == OPT_LANG_LIVE_NONE) {
        return;
    }
    LANGUAGE = target;
    if (variantChange) {
        padSetTextVariant(choice);
        padFreeSpanishText();
    }
    if (kind == OPT_LANG_LIVE_FIELD) {
        s32 old = FIELDSTG_state.textFile;

        FIELDSTG_state.textFile = optLangShiftFile(old, from, target);
        if (old != 0) {
            FILE_CACHE.free(old); /* the old language's stage text: not needed again */
            FILE_CACHE.request(FIELDSTG_state.textFile);
        }
    } else if (kind == OPT_LANG_LIVE_TITLE) {
        GAME.funcs.requestMode(MODE_TITLE, 0);
    }
    printf("pad: frame %d: language %d -> %d%s applied live (%s)\n", padLiveFrame, from, target,
           optLangVariant(choice) ? " (es419)" : "", kind == OPT_LANG_LIVE_TITLE ? "title" : "field");
}

/* The `optlang` token: pick the row, close the menu, then (after the switch) the mode request */
static void padOptLangRun(void) {
    if (padOptLangStage == 0) {
        return;
    }
    switch (padOptLangStage) {
    case 1:
        if (!csIsOpen() && GAME.funcs.getMode() == MODE_TITLE && ++padOptLangWait >= 200) {
            padControlsArmed = 0;
            controlsScreenHarness(CS_TAB_LANGUAGE, -1, -1, 0); /* the `options` token waits out the title */
        }
        if (csIsOpen() && csTab() == CS_TAB_LANGUAGE) {
            padOptLangStage = 2;
            padOptLangWait = 0;
        }
        break;
    case 2:
        if (++padOptLangWait >= 20) {
            csWidgetSet(CS_TAB_LANGUAGE, 0, padOptLang);
            padOptLangStage = 3;
            padOptLangWait = 0;
        }
        break;
    case 3:
        if (++padOptLangWait >= 20) {
            csClose();
            padOptLangStage = 4;
            padOptLangWait = 0;
        }
        break;
    case 4:
        if (++padOptLangWait >= 30 && !optLangPending()) {
            padOptLangStage = padOptLangTalk ? 5 : 0;
            padOptLangWait = 0;
            if (padOptLangBattle) {
                padBattleArg = 0;
                padBattleArmed = 1;
                padBattleForce = 1;
            }
            if (padOptLangMode != 0) {
                printf("pad: frame %d: optlang requests mode 0x%X\n", padLiveFrame, (int)padOptLangMode);
                GAME.funcs.requestMode(padOptLangMode, 0);
            }
        }
        break;
    case 5: /* walk right a while, then a cross press (talk to whoever is there) */
        padFakeKey(padKeyBit("right"), 1);
        if (++padOptLangWait >= padOptLangWalk) {
            padFakeKey(padKeyBit("cross"), 1);
            padOptLangStage = 6;
            padOptLangWait = 0;
        }
        break;
    case 6:
        if (++padOptLangWait >= 6) {
            padFakeKey(0, 0);
            padOptLangStage = 0;
        }
        break;
    }
}

void pcBootPadFrame(int frame) {
    /* What the game's own code had this frame, before it is updated for the
       next one (the menu has already run from it at the top of the loop) */
    padInHook = 1;
    padSettleSnapshot();
    padPrint(frame);
    padFastTextOnButtonEdge();

    /* The warp token (debug-warp): armed at parse, it fires once on the
       first frame in a boot mode and is spent after that */
    padLatchScripted();
    if (padWarpStage != 0) {
        padWarpFire(frame);
    }

    /* The battle token (battle-entry T3): the same lane, the battle mode's
       own request instead of a stage's */
    padLanguageFire();
    padLiveFrame = frame;
    padLanguageLive();
    padOptLangRun();
    padBattleFire(frame);
    padModeFire(frame);
    padControlsFire();

    if (padScriptArmed && padPhase != 3) {
        padRunScript();
    }

    /* The battle schedule (T7): its own lane, out of padRunScript's phase
       gate, the presses start once the fire's frame says each rel is due */
    padBattleScheduleRun(frame);
    padBattleAutoRun(frame);
    padFieldAutoRun(frame);
    padFieldBattlesRun(frame);
    padUntilRun();

    /* The host -> pad wiring: PsyCross polls SDL and writes the raw pad word
       into the buffers main's PAD.update() is about to read */
    PsyX_UpdateInput();

    /* controls-config C2: while a press-to-bind capture waits (and until the
       pressed input is released) the game's pad word stays neutral; a binding
       just assigned is saved here, on the main thread */
    inputCapFilterPad(&PAD.buf[0].pad.buttons, PAD.buf[0].pad.analog);
    /* android T7a: the virtual controller's held buttons, in the frame the finger events were pumped (no queue) */
    touchPadMerge(&PAD.buf[0].pad.buttons);
    inputCapFrame();
    /* C3: the controls screen's own input (read from SDL), once per frame */
    controlsScreenFrame();

    /*
     * A scripted button is written on top, in process (T21): the same buffer,
     * the same active-low raw word a real pad would send, and nothing outside
     * this process sees it. No key event, no focus, no session.
     */
    if (padHeldBit != 0) {
        PAD.buf[0].pad.buttons = (u16)~padHeldBit;
    }
    /* O6: the auto text press, one frame (never while the options menu is open: csFilterPad
       below blanks the word then, and padAutoTextFrame does not fire) */
    padAutoTextFrame(frame);
    padTouchTextFrame(frame);
    padTapMenuFrame();
    if (padAutoTextBit != 0) {
        PAD.buf[0].pad.buttons &= (u16)~padAutoTextBit;
    }
    /* C3: while the controls screen is open (and until what closed it is
       released) the game sees a neutral pad, scripted presses included */
    csFilterPad(&PAD.buf[0].pad.buttons, PAD.buf[0].pad.analog);
    padInHook = 0;
}
