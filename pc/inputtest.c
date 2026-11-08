/*
 * Unit test for the port's controls (settings.ini) and press-to-bind (pc/shim/input_cfg.c,
 * feature controls-config C1/C2).
 *
 * Everything here runs without a window, a pad or a disc: the pure functions
 * (name <-> code, line parsing, INI formatting) and the capture state machine,
 * which is driven with synthetic SDL_Event values handed straight to
 * inputCapEvent. SDL is never initialised (the name tables are static).
 *
 * Exit 0: every assertion held. Exit 1: at least one failed (printed to stderr).
 */
#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shim/port_log.h"
#include "shim/input_cfg.h"
#include "shim/disc_picker.h"
#include "shim/controls_screen.h"
#include "shim/options_video.h"
#include "shim/options_language.h"
#include "shim/options_audio.h"
#include "shim/options_system.h"
#include "shim/exp_types.h"
#include "shim/options_mods.h"
#include "shim/mods.h"
#include "shim/snd_internal.h"
#include "shim/input_device.h"
#include "shim/prompt_icons.h"
#include "shim/text_icons.h"
#include "shim/menu_frame.h"
#include "shim/auto_text.h"
#include "shim/battle_speed.h"
#include "shim/stage_banner.h"
#include "shim/fast_transitions.h"
#include "shim/state_scope.h"

extern int g_cfg_swapInterval, g_cfg_bilinearFiltering, g_cfg_pgxpTextureCorrection; /* PsyCross */
extern int g_cfg_integerScale, g_cfg_scanlines;
void PsyX_CalcScreenRect(int winW, int winH, int baseW, int baseH, int integer, int *x, int *y, int *w, int *h);

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "inputtest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int logCount;
static char lastLog[160];

static void logSink(const char *msg) {
    logCount++;
    snprintf(lastLog, sizeof(lastLog), "%s", msg);
}

/* Same value in every exposed slot of both devices, ids ignored */
static int sameCfg(const InputCfg *a, const InputCfg *b) {
    InputCfg x = *a, y = *b;
    int n;
    for (n = 0; n < INPUT_EXPOSED_COUNT; n++) {
        int s = INPUT_EXPOSED[n];
        if (*inputCfgSlot(&x, INPUT_DEV_PAD, s) != *inputCfgSlot(&y, INPUT_DEV_PAD, s)) {
            return 0;
        }
        if (n < INPUT_EXPOSED_BUTTONS
            && *inputCfgSlot(&x, INPUT_DEV_KEYBOARD, s) != *inputCfgSlot(&y, INPUT_DEV_KEYBOARD, s)) {
            return 0;
        }
    }
    return 1;
}

static void testNames(void) {
    char buf[64];
    int c, ok = 1;

    CHECK(inputCfgKeyName(SDL_SCANCODE_LSHIFT, buf, sizeof(buf)) > 0 && strcmp(buf, "Left Shift") == 0);
    CHECK(inputCfgKeyCode("left shift") == SDL_SCANCODE_LSHIFT); /* case-insensitive */
    CHECK(inputCfgKeyCode("NONE") == 0 && inputCfgKeyName(0, buf, sizeof(buf)) > 0 && strcmp(buf, "NONE") == 0);
    CHECK(inputCfgKeyCode("no such key") == INPUT_CODE_INVALID);
    CHECK(inputCfgKeyCode("") == INPUT_CODE_INVALID);
    /* Every named scancode comes back to a scancode with the same name */
    for (c = 1; c < SDL_NUM_SCANCODES; c++) {
        char back[64];
        int r;
        if (inputCfgKeyName(c, buf, sizeof(buf)) == 0) {
            continue;
        }
        r = inputCfgKeyCode(buf);
        if (r == INPUT_CODE_INVALID || inputCfgKeyName(r, back, sizeof(back)) == 0 || strcmp(back, buf) != 0) {
            fprintf(stderr, "inputtest: scancode %d '%s' does not round-trip\n", c, buf);
            ok = 0;
        }
    }
    CHECK(ok);

    CHECK(inputCfgPadName(SDL_CONTROLLER_BUTTON_A, buf, sizeof(buf)) > 0 && strcmp(buf, "a") == 0);
    CHECK(inputCfgPadName(SDL_CONTROLLER_AXIS_TRIGGERLEFT | CONTROLLER_MAP_FLAG_AXIS, buf, sizeof(buf)) > 0
          && strcmp(buf, "lefttrigger") == 0);
    CHECK(inputCfgPadName(SDL_CONTROLLER_AXIS_LEFTY | CONTROLLER_MAP_FLAG_AXIS | CONTROLLER_MAP_FLAG_INVERSE,
                          buf, sizeof(buf)) > 0 && strcmp(buf, "-lefty") == 0);
    CHECK(inputCfgPadCode("-lefty") ==
          (SDL_CONTROLLER_AXIS_LEFTY | CONTROLLER_MAP_FLAG_AXIS | CONTROLLER_MAP_FLAG_INVERSE));
    CHECK(inputCfgPadCode("LeftShoulder") == SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    CHECK(inputCfgPadCode("NONE") == SDL_CONTROLLER_BUTTON_INVALID);
    CHECK(inputCfgPadCode("bogus") == INPUT_CODE_INVALID);
    CHECK(inputCfgPadCode("-") == INPUT_CODE_INVALID);
    CHECK(inputCfgPadName(SDL_CONTROLLER_BUTTON_INVALID, buf, sizeof(buf)) > 0 && strcmp(buf, "NONE") == 0);
    CHECK(inputCfgPadName(SDL_CONTROLLER_BUTTON_MAX + 5, buf, sizeof(buf)) == 0);
    CHECK(inputCfgPadName(SDL_CONTROLLER_BUTTON_A, buf, 1) == 0); /* too small */
}

static void testDefaults(void) {
    InputCfg d;

    inputCfgDefaults(&d);
    /* The values PsyX_Sys_InitialiseInput sets today */
    CHECK(d.kb.kc_cross == SDL_SCANCODE_C && d.kb.kc_circle == SDL_SCANCODE_V);
    CHECK(d.kb.kc_square == SDL_SCANCODE_X && d.kb.kc_triangle == SDL_SCANCODE_Z);
    CHECK(d.kb.kc_start == SDL_SCANCODE_RETURN && d.kb.kc_select == SDL_SCANCODE_SPACE);
    CHECK(d.kb.kc_l1 == SDL_SCANCODE_LSHIFT && d.kb.kc_r2 == SDL_SCANCODE_RCTRL);
    CHECK(d.kb.kc_dpad_up == SDL_SCANCODE_UP && d.kb.kc_dpad_right == SDL_SCANCODE_RIGHT);
    CHECK(d.pad.gc_cross == SDL_CONTROLLER_BUTTON_A && d.pad.gc_square == SDL_CONTROLLER_BUTTON_X);
    CHECK(d.pad.gc_l2 == (SDL_CONTROLLER_AXIS_TRIGGERLEFT | CONTROLLER_MAP_FLAG_AXIS));
    CHECK(d.pad.gc_axis_right_y == (SDL_CONTROLLER_AXIS_RIGHTY | CONTROLLER_MAP_FLAG_AXIS));
    CHECK(d.pad.gc_select == SDL_CONTROLLER_BUTTON_BACK && d.pad.gc_start == SDL_CONTROLLER_BUTTON_START);
}

static void testRoundTrip(void) {
    static char text[16384];
    InputCfg d, back, mod;
    int n;

    inputCfgDefaults(&d);
    n = inputCfgFormat(&d, text, sizeof(text));
    CHECK(n > 0 && (size_t)n == strlen(text));
    CHECK(inputCfgFormat(&d, text, 20) == -1);
    n = inputCfgFormat(&d, text, sizeof(text)); /* the short call clobbered it */

    /* defaults -> text -> parse into a deliberately different struct; only the
       exposed slots are written, the rest keep what the target had */
    inputCfgDefaults(&back);
    for (n = 0; n < INPUT_EXPOSED_COUNT; n++) {
        if (n < INPUT_EXPOSED_BUTTONS) {
            *inputCfgSlot(&back, INPUT_DEV_KEYBOARD, INPUT_EXPOSED[n]) = 0;
        }
        *inputCfgSlot(&back, INPUT_DEV_PAD, INPUT_EXPOSED[n]) = 99;
    }
    CHECK(!sameCfg(&d, &back));
    logCount = 0;
    CHECK(inputCfgParse(text, &back, logSink) == 0 && logCount == 0);
    CHECK(sameCfg(&d, &back));

    /* Rebinds, including a key whose name is '=' and one named ';', and axes */
    mod = d;
    mod.kb.kc_cross = SDL_SCANCODE_EQUALS;
    mod.kb.kc_circle = SDL_SCANCODE_SEMICOLON;
    mod.kb.kc_l1 = SDL_SCANCODE_KP_ENTER;
    mod.kb.kc_l2 = SDL_SCANCODE_F9; /* not exposed: the writer skips it */
    mod.kb.kc_start = 0; /* NONE */
    mod.pad.gc_square = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    mod.pad.gc_l1 = SDL_CONTROLLER_AXIS_RIGHTX | CONTROLLER_MAP_FLAG_AXIS | CONTROLLER_MAP_FLAG_INVERSE;
    mod.pad.gc_r1 = SDL_CONTROLLER_AXIS_LEFTY | CONTROLLER_MAP_FLAG_AXIS;
    mod.pad.gc_axis_left_x = SDL_CONTROLLER_AXIS_RIGHTX | CONTROLLER_MAP_FLAG_AXIS | CONTROLLER_MAP_FLAG_INVERSE;
    mod.pad.gc_dpad_up = SDL_CONTROLLER_BUTTON_INVALID;
    n = inputCfgFormat(&mod, text, sizeof(text));
    CHECK(n > 0);
    {
        InputCfg z;
        inputCfgDefaults(&z);
        z.kb.kc_cross = 0;
        z.pad.gc_axis_left_x = 99;
        CHECK(inputCfgParse(text, &z, logSink) == 0);
        CHECK(sameCfg(&mod, &z));
    }
}

static void testScope(void) {
    static char text[16384];
    InputCfg d, mod, got;
    int n;

    /* Only what the game reads is exposed: d-pad, cross/circle/triangle/square,
       L1, R1, start, and the left stick for the pad */
    CHECK(INPUT_EXPOSED_COUNT == 13);
    inputCfgDefaults(&d);
    n = inputCfgFormat(&d, text, sizeof(text));
    CHECK(n > 0);
    CHECK(strstr(text, "select") == NULL && strstr(text, "l2") == NULL && strstr(text, "r3") == NULL);
    CHECK(strstr(text, "axis_right") == NULL && strstr(text, "axis_left_x = leftx") != NULL);
    CHECK(strstr(text, "dpad_left = Left") != NULL && strstr(text, "cross = C") != NULL);
    /* The keyboard section has no stick: the only axis_left_x line is the pad's */
    CHECK(strstr(text, "axis_left_x") > strstr(text, "[controls.controller]"));
    /* The unexposed fields are neither written nor read back from a changed
       value in memory, but a hand-added line is still accepted */
    mod = d;
    mod.kb.kc_select = SDL_SCANCODE_F9;
    n = inputCfgFormat(&mod, text, sizeof(text));
    inputCfgDefaults(&got);
    CHECK(inputCfgParse(text, &got, logSink) == 0 && got.kb.kc_select == d.kb.kc_select);
    CHECK(inputCfgParse("[keyboard]\nselect = F9\n[controller]\nr3 = rightstick\n", &got, logSink) == 0);
    CHECK(got.kb.kc_select == SDL_SCANCODE_F9);
    /* Capture refuses the unexposed buttons */
    CHECK(inputCapBegin(INPUT_DEV_KEYBOARD, INPUT_BTN_SELECT) == -1);
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_L2) == -1);
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_L1) == 0);
    inputCapCancel();
}

static void testInvalid(void) {
    const char *text =
        "; a comment\n"
        "# another\n"
        "\n"
        "[keyboard]\n"
        "cross = F13\n"            /* valid, applied */
        "circle = Not A Key\n"     /* unknown name: default kept */
        "nonsense = A\n"           /* unknown slot */
        "axis_left_x = A\n"        /* keyboards have no axes */
        "this line has no equals\n"
        "[controller]\n"
        "cross = b\n"              /* valid */
        "circle = wat\n"           /* unknown name */
        "square = -a\n"            /* '-' only makes sense on an axis */
        "[bogus]\n"                /* unknown section */
        "cross = x\n"              /* ignored: inside the unknown section */
        "[keyboard]\n"
        "triangle =\n";            /* empty value */
    InputCfg d, got;
    int rejected;

    inputCfgDefaults(&d);
    got = d;
    logCount = 0;
    rejected = inputCfgParse(text, &got, logSink);
    CHECK(got.kb.kc_cross == SDL_SCANCODE_F13);
    CHECK(got.kb.kc_circle == d.kb.kc_circle);
    CHECK(got.kb.kc_triangle == d.kb.kc_triangle);
    CHECK(got.pad.gc_cross == SDL_CONTROLLER_BUTTON_B);
    CHECK(got.pad.gc_circle == d.pad.gc_circle);
    CHECK(got.pad.gc_square == d.pad.gc_square);
    CHECK(rejected == 9); /* kb circle, nonsense, axis_left_x, no '=', pad circle, -a, [bogus], its cross, empty triangle */
    CHECK(logCount == rejected);
    CHECK(strlen(lastLog) > 0);
    /* Binary junk / overlong lines must not crash or change anything */
    {
        char junk[5000];
        memset(junk, 'k', sizeof(junk) - 1);
        junk[sizeof(junk) - 1] = 0;
        got = d;
        CHECK(inputCfgParse(junk, &got, NULL) >= 1);
        CHECK(sameCfg(&d, &got));
    }
}

static void testFile(const char *dir) {
    char path[512];
    InputCfg d, mod, back;
    char text[16384];

    snprintf(path, sizeof(path), "%s/inputtest_controls.ini", dir);
    remove(path);
    inputCfgDefaults(&d);
    CHECK(inputCfgLoadFile(path, &back, logSink) == -1); /* missing */
    CHECK(inputCfgSaveFile(path, &d) == 0);
    memset(&back, 0, sizeof(back));
    CHECK(inputCfgLoadFile(path, &back, logSink) == 0 && sameCfg(&d, &back));
    mod = d;
    mod.kb.kc_cross = SDL_SCANCODE_F5;
    CHECK(inputCfgSaveFile(path, &mod) == 0);
    CHECK(inputCfgLoadFile(path, &back, logSink) == 0 && sameCfg(&mod, &back));
    CHECK(inputCfgSaveFile("/nonexistent-dir/x.ini", &d) != 0);
    (void)text;
}

static SDL_Event keyEv(int type, int scancode, int repeat) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.key.keysym.scancode = (SDL_Scancode)scancode;
    e.key.repeat = (Uint8)repeat;
    return e;
}

static SDL_Event padBtnEv(int button) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.button = (Uint8)button;
    return e;
}

static SDL_Event axisEv(int axis, int value) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.axis = (Uint8)axis;
    e.caxis.value = (Sint16)value;
    return e;
}

static void testCapture(const char *dir) {
    char path[512];
    InputCfg d, onDisk;
    SDL_Event e;
    unsigned short buttons;
    unsigned char analog[4];

    snprintf(path, sizeof(path), "%s/inputtest_capture.ini", dir);
    remove(path);
    inputCfgDefaults(&d);
    inputCfgApply(&d);
    inputCfgSetPath(path);

    /* Idle: events are not consumed, the game's pad word is left alone */
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_A, 0);
    CHECK(inputCapEvent(&e) == 0);
    buttons = 0xFFFF;
    inputCapFilterPad(&buttons, analog); /* releases any hold an earlier test left */
    buttons = 0xFFF7;
    CHECK(inputCapFilterPad(&buttons, analog) == 0 && buttons == 0xFFF7);
    CHECK(inputCapBegin(INPUT_DEV_KEYBOARD, INPUT_BTN_COUNT) == -1);
    CHECK(inputCapBegin(7, INPUT_BTN_CROSS) == -1);

    /* Keyboard capture of Cross */
    CHECK(inputCapBegin(INPUT_DEV_KEYBOARD, INPUT_BTN_CROSS) == 0);
    CHECK(inputCapState() == INPUT_CAP_WAITING);
    /* the game must see nothing while it waits, whatever is down */
    buttons = 0xBFFF;
    memset(analog, 0, sizeof(analog));
    CHECK(inputCapFilterPad(&buttons, analog) == 1 && buttons == 0xFFFF && analog[0] == 127 && analog[3] == 127);
    /* wrong device, key-up and a held key's auto-repeat do not bind */
    e = padBtnEv(SDL_CONTROLLER_BUTTON_Y);
    CHECK(inputCapEvent(&e) == 0 && inputCapState() == INPUT_CAP_WAITING);
    e = keyEv(SDL_KEYUP, SDL_SCANCODE_A, 0);
    CHECK(inputCapEvent(&e) == 0 && inputCapState() == INPUT_CAP_WAITING);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_A, 1);
    CHECK(inputCapEvent(&e) == 0 && inputCapState() == INPUT_CAP_WAITING);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_A, 0);
    CHECK(inputCapEvent(&e) == 1);
    CHECK(inputCapState() == INPUT_CAP_BOUND);
    CHECK(g_cfg_keyboardMapping.kc_cross == SDL_SCANCODE_A);
    CHECK(g_cfg_keyboardMapping.kc_circle == d.kb.kc_circle); /* only that one */
    /* saved on the main thread's next frame */
    CHECK(inputCfgLoadFile(path, &onDisk, NULL) == -1);
    inputCapFrame();
    CHECK(inputCfgLoadFile(path, &onDisk, logSink) == 0 && onDisk.kb.kc_cross == SDL_SCANCODE_A);
    /* the key is still down: held neutral until released, then free */
    buttons = 0xBFFF;
    CHECK(inputCapFilterPad(&buttons, analog) == 1 && buttons == 0xFFFF);
    buttons = 0xFFFF;
    CHECK(inputCapFilterPad(&buttons, analog) == 0);
    buttons = 0xBFFF;
    CHECK(inputCapFilterPad(&buttons, analog) == 0 && buttons == 0xBFFF); /* a new press reaches the game */

    /* Escape cancels and changes nothing */
    CHECK(inputCapBegin(INPUT_DEV_KEYBOARD, INPUT_BTN_START) == 0);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE, 0);
    CHECK(inputCapEvent(&e) == 1 && inputCapState() == INPUT_CAP_CANCELLED);
    CHECK(g_cfg_keyboardMapping.kc_start == d.kb.kc_start);
    buttons = 0xFFFF;
    CHECK(inputCapFilterPad(&buttons, analog) == 0);
    /* Escape is also the way out of a pad capture */
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_START) == 0);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE, 0);
    CHECK(inputCapEvent(&e) == 1 && inputCapState() == INPUT_CAP_CANCELLED);
    /* inputCapCancel */
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_START) == 0);
    inputCapCancel();
    CHECK(inputCapState() == INPUT_CAP_CANCELLED);

    /* Pad capture: a button */
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_SQUARE) == 0);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_A, 0); /* a key does not bind a pad button */
    CHECK(inputCapEvent(&e) == 0);
    e = axisEv(SDL_CONTROLLER_AXIS_LEFTX, 9000); /* under the threshold */
    CHECK(inputCapEvent(&e) == 0 && inputCapState() == INPUT_CAP_WAITING);
    e = padBtnEv(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    CHECK(inputCapEvent(&e) == 1);
    CHECK(g_cfg_controllerMapping.gc_square == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    CHECK(g_cfg_controllerMapping.gc_cross == d.pad.gc_cross);
    /* Pad capture: an axis, positive and negative (the inverse flag) */
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_L1) == 0);
    e = axisEv(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000);
    CHECK(inputCapEvent(&e) == 1);
    CHECK(g_cfg_controllerMapping.gc_l1 == (SDL_CONTROLLER_AXIS_TRIGGERRIGHT | CONTROLLER_MAP_FLAG_AXIS));
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_R1) == 0);
    e = axisEv(SDL_CONTROLLER_AXIS_LEFTY, -20000);
    CHECK(inputCapEvent(&e) == 1);
    CHECK(g_cfg_controllerMapping.gc_r1 ==
          (SDL_CONTROLLER_AXIS_LEFTY | CONTROLLER_MAP_FLAG_AXIS | CONTROLLER_MAP_FLAG_INVERSE));
    inputCapFrame();
    CHECK(inputCfgLoadFile(path, &onDisk, logSink) == 0 && onDisk.pad.gc_r1 == g_cfg_controllerMapping.gc_r1);

    /* Reset: one device, then both; saved */
    inputCfgReset(INPUT_DEV_PAD);
    CHECK(g_cfg_controllerMapping.gc_square == d.pad.gc_square && g_cfg_keyboardMapping.kc_cross == SDL_SCANCODE_A);
    inputCfgReset(INPUT_DEV_ALL);
    {
        InputCfg cur;
        inputCfgCurrent(&cur);
        CHECK(sameCfg(&cur, &d));
    }
    CHECK(inputCfgLoadFile(path, &onDisk, logSink) == 0 && sameCfg(&onDisk, &d));
}

static void testReservedKeys(void) {
    InputCfg d;
    SDL_Event e;
    int sc;

    inputCfgDefaults(&d);
    inputCfgApply(&d);
    inputCfgSetPath("");
    CHECK(inputCfgKeyReserved(SDL_SCANCODE_BACKSPACE) && inputCfgKeyReserved(SDL_SCANCODE_F1)
          && inputCfgKeyReserved(SDL_SCANCODE_F12));
    CHECK(!inputCfgKeyReserved(SDL_SCANCODE_F13) && !inputCfgKeyReserved(SDL_SCANCODE_A)
          && !inputCfgKeyReserved(SDL_SCANCODE_ESCAPE) && !inputCfgKeyReserved(SDL_SCANCODE_DELETE));
    /* the capture refuses each one, keeps waiting, and the binding is untouched */
    for (sc = SDL_SCANCODE_F1; sc <= SDL_SCANCODE_F12 + 1; sc++) {
        int reserved = sc <= SDL_SCANCODE_F12;

        CHECK(inputCapBegin(INPUT_DEV_KEYBOARD, INPUT_BTN_CROSS) == 0 && inputCapRefused() == 0);
        e = keyEv(SDL_KEYDOWN, sc, 0);
        inputCapEvent(&e);
        if (reserved) {
            CHECK(inputCapState() == INPUT_CAP_WAITING && inputCapRefused() == sc);
            CHECK(g_cfg_keyboardMapping.kc_cross == d.kb.kc_cross);
        } else {
            CHECK(inputCapState() == INPUT_CAP_BOUND && g_cfg_keyboardMapping.kc_cross == sc);
        }
        inputCapCancel();
        inputCfgReset(INPUT_DEV_KEYBOARD);
    }
    CHECK(inputCapBegin(INPUT_DEV_KEYBOARD, INPUT_BTN_CROSS) == 0);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_BACKSPACE, 0);
    inputCapEvent(&e);
    CHECK(inputCapState() == INPUT_CAP_WAITING && g_cfg_keyboardMapping.kc_cross == d.kb.kc_cross);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_K, 0); /* then a normal key still binds */
    inputCapEvent(&e);
    CHECK(inputCapState() == INPUT_CAP_BOUND && g_cfg_keyboardMapping.kc_cross == SDL_SCANCODE_K);
    /* a controller capture is not affected by the keyboard list */
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_CROSS) == 0);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_F2, 0);
    CHECK(inputCapEvent(&e) == 0 && inputCapRefused() == 0 && inputCapState() == INPUT_CAP_WAITING);
    inputCapCancel();
    inputCfgReset(INPUT_DEV_ALL);
}

static void testScreenModel(void) {
    char buf[64];
    int i, kinds[2] = {0, 0};
    SDL_Event e;
    InputCfg d;

    inputCfgDefaults(&d);
    inputCfgApply(&d);
    inputCfgSetPath("");
    /* the Controls tab's rows are INPUT_EXPOSED's buttons, then reset and back */
    CHECK(CS_TAB_COUNT == 8 && csTabRowCount(CS_TAB_CONTROLS) == INPUT_EXPOSED_BUTTONS + 2);
    CHECK(CS_CTL_ROW_RESET == INPUT_EXPOSED_BUTTONS && CS_CTL_ROW_BACK == INPUT_EXPOSED_BUTTONS + 1);
    for (i = 0; i < csTabRowCount(CS_TAB_CONTROLS); i++) {
        int k = csRowKind(CS_TAB_CONTROLS, i);

        CHECK(k == CS_ROW_BINDING || k == CS_ROW_ACTION);
        kinds[k == CS_ROW_BINDING ? 0 : 1]++;
        CHECK(csRowLabel(CS_TAB_CONTROLS, i, buf, sizeof(buf)) > 0);
        CHECK(csRowDesc(CS_TAB_CONTROLS, i, buf, sizeof(buf)) > 0);
        if (k == CS_ROW_BINDING) {
            CHECK(csRowSlot(i) == INPUT_EXPOSED[i] && inputCfgIsExposed(INPUT_DEV_KEYBOARD, csRowSlot(i)));
        } else {
            CHECK(csRowSlot(i) == -1);
        }
    }
    CHECK(kinds[0] == INPUT_EXPOSED_BUTTONS && kinds[1] == 2);
    CHECK(csRowKind(CS_TAB_CONTROLS, -1) == -1 && csRowKind(CS_TAB_CONTROLS, CS_CTL_ROW_BACK + 1) == -1);
    CHECK(csRowKind(-1, 0) == -1 && csRowKind(CS_TAB_COUNT, 0) == -1);
    CHECK(csRowLabel(CS_TAB_CONTROLS, CS_CTL_ROW_RESET, buf, sizeof(buf)) > 0 && strcmp(buf, "Reset to defaults") == 0);
    CHECK(csRowLabel(CS_TAB_CONTROLS, CS_CTL_ROW_BACK, buf, sizeof(buf)) > 0 && strcmp(buf, "Back") == 0);
    CHECK(csRowLabel(CS_TAB_CONTROLS, 4, buf, 3) == 0); /* too small */
    CHECK(strcmp(csTabName(CS_TAB_CONTROLS), "Controls") == 0 && strcmp(csTabName(CS_TAB_AUDIO), "Audio") == 0);
    CHECK(strcmp(csTabName(CS_TAB_ENHANCEMENTS), "Enhancements") == 0
          && CS_TAB_SYSTEM == CS_TAB_COUNT - 2 && strcmp(csTabName(CS_TAB_SYSTEM), "System") == 0
          && CS_TAB_MODS == CS_TAB_COUNT - 1 && strcmp(csTabName(CS_TAB_MODS), "Mods") == 0);
    /* every shown name is plain text the game's font can draw (letters, digits, space, '-') */
    for (i = 0; i < INPUT_EXPOSED_BUTTONS; i++) {
        CHECK(csRowValue(CS_TAB_CONTROLS, INPUT_DEV_KEYBOARD, i, buf, sizeof(buf)) > 0);
        CHECK(csRowValue(CS_TAB_CONTROLS, INPUT_DEV_PAD, i, buf, sizeof(buf)) > 0);
    }
    /* the value of a row is the live binding (Up = the Up key, Cross = C) */
    CHECK(csRowValue(CS_TAB_CONTROLS, INPUT_DEV_KEYBOARD, 0, buf, sizeof(buf)) > 0 && strcmp(buf, "Up") == 0);
    CHECK(csRowValue(CS_TAB_CONTROLS, INPUT_DEV_KEYBOARD, 4, buf, sizeof(buf)) > 0 && strcmp(buf, "C") == 0);
    CHECK(csRowValue(CS_TAB_CONTROLS, INPUT_DEV_PAD, 4, buf, sizeof(buf)) > 0 && strcmp(buf, "a") == 0);
    CHECK(csRowValue(CS_TAB_CONTROLS, INPUT_DEV_KEYBOARD, CS_CTL_ROW_RESET, buf, sizeof(buf)) == 0 && buf[0] == '\0');

    /* cursor wrap */
    CHECK(csWrap(0, -1, 13) == 12 && csWrap(12, 1, 13) == 0 && csWrap(5, 1, 13) == 6 && csWrap(5, -13, 13) == 5);
    CHECK(csWrap(3, 1, 0) == 0);

    /* open / close and what is ignored while closed */
    csClose();
    CHECK(!csIsOpen() && csAct(CS_ACT_DOWN) == 0 && csAct(CS_ACT_CONFIRM) == 0);
    CHECK(csAct(CS_ACT_TOGGLE) == 1 && csIsOpen() && csCursor() == 0 && csDevice() == INPUT_DEV_KEYBOARD);
    CHECK(csAct(CS_ACT_UP) == 1 && csCursor() == csTabRowCount(CS_TAB_CONTROLS) - 1); /* wraps up */
    CHECK(csAct(CS_ACT_DOWN) == 1 && csCursor() == 0);
    CHECK(csAct(CS_ACT_RIGHT) == 1 && csDevice() == INPUT_DEV_PAD);
    CHECK(csAct(CS_ACT_LEFT) == 1 && csDevice() == INPUT_DEV_KEYBOARD);

    /* the pad word is neutral while open, and until released after it closes */
    {
        unsigned short buttons = 0xBFFF;
        unsigned char analog[4] = {0, 0, 0, 0};

        CHECK(csFilterPad(&buttons, analog) == 1 && buttons == 0xFFFF && analog[1] == 127);
        csClose();
        buttons = 0xBFFF;
        CHECK(csFilterPad(&buttons, analog) == 1 && buttons == 0xFFFF);
        buttons = 0xFFFF;
        CHECK(csFilterPad(&buttons, analog) == 0);
        buttons = 0xBFFF;
        CHECK(csFilterPad(&buttons, analog) == 0 && buttons == 0xBFFF);
    }

    /* confirm on a button row starts that row's capture on the shown device */
    csOpen();
    csSetCursor(4); /* Cross */
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && csWaitingRow() == 4 && inputCapState() == INPUT_CAP_WAITING);
    CHECK(inputCapDevice() == INPUT_DEV_KEYBOARD && inputCapButton() == INPUT_BTN_CROSS);
    CHECK(csRowValue(CS_TAB_CONTROLS, INPUT_DEV_KEYBOARD, 4, buf, sizeof(buf)) > 0 && strcmp(buf, "Press a key...") == 0);
    CHECK(csAct(CS_ACT_DOWN) == 0 && csCursor() == 4 && csAct(CS_ACT_TOGGLE) == 0); /* nothing moves meanwhile */
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_F1, 0); /* reserved: still waiting, says so */
    inputCapEvent(&e);
    csPump();
    CHECK(csWaitingRow() == 4 && strstr(csMessage(), "reserved") != NULL);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_Q, 0);
    inputCapEvent(&e);
    csQueueAction(CS_ACT_CANCEL); /* the same press as the capture's: dropped, not a Back */
    csPump();
    CHECK(csWaitingRow() == -1 && csIsOpen() && strcmp(csMessage(), "Bound") == 0);
    CHECK(g_cfg_keyboardMapping.kc_cross == SDL_SCANCODE_Q);
    CHECK(csRowValue(CS_TAB_CONTROLS, INPUT_DEV_KEYBOARD, 4, buf, sizeof(buf)) > 0 && strcmp(buf, "Q") == 0);
    /* Escape ends a capture without closing the screen */
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && csWaitingRow() == 4);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE, 0);
    inputCapEvent(&e);
    csQueueAction(csActionFromEvent((const union SDL_Event *)&e));
    csPump();
    CHECK(csWaitingRow() == -1 && csIsOpen() && strcmp(csMessage(), "Cancelled") == 0);
    CHECK(g_cfg_keyboardMapping.kc_cross == SDL_SCANCODE_Q);
    /* the controller tab captures a pad button */
    csAct(CS_ACT_RIGHT);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && inputCapDevice() == INPUT_DEV_PAD);
    inputCapCancel();
    csPump();
    CHECK(csWaitingRow() == -1);
    /* reset defaults only the shown device */
    csSetCursor(CS_CTL_ROW_RESET);
    csSetDevice(INPUT_DEV_KEYBOARD);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && g_cfg_keyboardMapping.kc_cross == d.kb.kc_cross);
    /* back closes */
    csSetCursor(CS_CTL_ROW_BACK);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && !csIsOpen());
    csOpen();
    CHECK(csAct(CS_ACT_CANCEL) == 1 && !csIsOpen());
    /* a movie (blocked) closes it and refuses to open */
    csOpen();
    csSetBlocked(1);
    CHECK(!csIsOpen() && csAct(CS_ACT_TOGGLE) == 1 && !csIsOpen());
    csSetBlocked(0);
    CHECK(csAct(CS_ACT_TOGGLE) == 1 && csIsOpen());
    csClose();

    /* SDL events -> actions */
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_F1, 0);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_TOGGLE);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_F1, 1);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_NONE);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_DOWN, 1);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_NONE); /* the OS key repeat is ignored: the screen repeats */
    e = keyEv(SDL_KEYUP, SDL_SCANCODE_DOWN, 0);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_NONE);
    e = padBtnEv(SDL_CONTROLLER_BUTTON_DPAD_UP);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_UP);
    e = padBtnEv(SDL_CONTROLLER_BUTTON_A);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_CONFIRM);
    e = padBtnEv(SDL_CONTROLLER_BUTTON_B);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_CANCEL);
    e = axisEv(SDL_CONTROLLER_AXIS_LEFTY, 30000); /* one press per push */
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_DOWN);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_NONE);
    e = axisEv(SDL_CONTROLLER_AXIS_LEFTY, 0);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_NONE);
    e = axisEv(SDL_CONTROLLER_AXIS_LEFTY, -30000);
    CHECK(csActionFromEvent((const union SDL_Event *)&e) == CS_ACT_UP);
    e = axisEv(SDL_CONTROLLER_AXIS_LEFTY, 0);
    csActionFromEvent((const union SDL_Event *)&e);
    inputCfgReset(INPUT_DEV_ALL);
}

static int applied, appliedValue;

static void recordApply(int value) {
    applied++;
    appliedValue = value;
}

static int actOf(const SDL_Event *e) {
    return csActionFromEvent((const union SDL_Event *)e);
}

/* O1: tabs, the open/close mapping and the reusable widget rows */
static void testOptionsMenu(void) {
    static const char *const names[3] = {"Low", "Mid", "High"};
    CsWidget w;
    char buf[64];
    SDL_Event e;
    int tab;

    inputCfgSetPath("");
    csClose();
    csWidgetsClear();
    csOpen();
    CHECK(csTab() == CS_TAB_CONTROLS && csAct(CS_ACT_TAB_NEXT) == 1 && csTab() == CS_TAB_VIDEO);
    /* an empty tab is a placeholder with no rows to move on */
    CHECK(csTabPlaceholder(CS_TAB_VIDEO) == 1 && csTabPlaceholder(CS_TAB_CONTROLS) == 0);
    CHECK(csTabRowCount(CS_TAB_VIDEO) == 0 && csAct(CS_ACT_DOWN) == 0 && csAct(CS_ACT_CONFIRM) == 0);
    CHECK(csAct(CS_ACT_LEFT) == 0 && csCursor() == 0);
    csSetTab(CS_TAB_CONTROLS);
    for (tab = 0; tab < CS_TAB_COUNT; tab++) {
        CHECK(csTab() == tab);
        csAct(CS_ACT_TAB_NEXT);
    }
    CHECK(csTab() == CS_TAB_CONTROLS); /* every tab once, wrapped */
    CHECK(csAct(CS_ACT_TAB_PREV) == 1 && csTab() == CS_TAB_MODS); /* wraps back */
    CHECK(csAct(CS_ACT_TAB_NEXT) == 1 && csTab() == CS_TAB_CONTROLS);
    csSetTab(99);
    CHECK(csTab() == CS_TAB_CONTROLS);
    /* each tab remembers its own cursor */
    csSetCursor(5);
    csAct(CS_ACT_TAB_NEXT);
    csAct(CS_ACT_TAB_PREV);
    CHECK(csCursor() == 5);
    /* tabs do not switch while closed or while a capture waits */
    csSetCursor(4);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && csWaitingRow() == 4);
    CHECK(csAct(CS_ACT_TAB_NEXT) == 0 && csTab() == CS_TAB_CONTROLS);
    inputCapCancel();
    csPump();
    csClose();
    CHECK(csAct(CS_ACT_TAB_NEXT) == 0);

    /* Select and F1 both toggle; the shoulders / Q E / PageUp PageDown switch tabs */
    e = padBtnEv(SDL_CONTROLLER_BUTTON_BACK);
    CHECK(actOf(&e) == CS_ACT_TOGGLE);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_F1, 0);
    CHECK(actOf(&e) == CS_ACT_TOGGLE);
    e = padBtnEv(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    CHECK(actOf(&e) == CS_ACT_TAB_PREV);
    e = padBtnEv(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    CHECK(actOf(&e) == CS_ACT_TAB_NEXT);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_Q, 0);
    CHECK(actOf(&e) == CS_ACT_TAB_PREV);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_E, 0);
    CHECK(actOf(&e) == CS_ACT_TAB_NEXT);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_PAGEUP, 0);
    CHECK(actOf(&e) == CS_ACT_TAB_PREV);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_PAGEDOWN, 0);
    CHECK(actOf(&e) == CS_ACT_TAB_NEXT);
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_E, 1);
    CHECK(actOf(&e) == CS_ACT_NONE); /* held: no repeat */
    e = keyEv(SDL_KEYDOWN, SDL_SCANCODE_LEFT, 0);
    CHECK(actOf(&e) == CS_ACT_LEFT); /* left / right stay for values */
    /* Select cannot be bound to a game button: the capture refuses it and keeps waiting */
    CHECK(inputCfgPadButtonReserved(SDL_CONTROLLER_BUTTON_BACK) == 1
          && inputCfgPadButtonReserved(SDL_CONTROLLER_BUTTON_A) == 0);
    CHECK(!inputCfgIsExposed(INPUT_DEV_PAD, INPUT_BTN_SELECT));
    CHECK(inputCapBegin(INPUT_DEV_PAD, INPUT_BTN_CROSS) == 0);
    e = padBtnEv(SDL_CONTROLLER_BUTTON_BACK);
    CHECK(inputCapEvent((const union SDL_Event *)&e) == 1 && inputCapState() == INPUT_CAP_WAITING
          && inputCapRefused() != 0);
    e = padBtnEv(SDL_CONTROLLER_BUTTON_A);
    inputCapEvent((const union SDL_Event *)&e);
    CHECK(inputCapState() == INPUT_CAP_BOUND);
    inputCfgReset(INPUT_DEV_ALL);

    /* widget rows: a toggle, a cycle, a slider and an action on the Video tab */
    CHECK(csWidgetAdd(CS_TAB_CONTROLS, &(CsWidget){.kind = CS_ROW_TOGGLE, .label = "X"}) == -1);
    CHECK(csWidgetAdd(CS_TAB_VIDEO, &(CsWidget){.kind = CS_ROW_BINDING, .label = "X"}) == -1);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_TOGGLE;
    w.label = "Fullscreen";
    w.desc = "Toggles fullscreen.";
    w.apply = recordApply;
    CHECK(csWidgetAdd(CS_TAB_VIDEO, &w) == 0);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_CYCLE;
    w.label = "Filter";
    w.names = names;
    w.count = 3;
    w.apply = recordApply;
    CHECK(csWidgetAdd(CS_TAB_VIDEO, &w) == 1);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_SLIDER;
    w.label = "Volume";
    w.min = 0;
    w.max = 100;
    w.step = 25;
    w.value = 50;
    w.apply = recordApply;
    CHECK(csWidgetAdd(CS_TAB_VIDEO, &w) == 2);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_ACTION;
    w.label = "Apply";
    w.apply = recordApply;
    CHECK(csWidgetAdd(CS_TAB_VIDEO, &w) == 3);
    CHECK(csTabRowCount(CS_TAB_VIDEO) == 4 && csTabPlaceholder(CS_TAB_VIDEO) == 0);
    CHECK(csRowKind(CS_TAB_VIDEO, 0) == CS_ROW_TOGGLE && csRowKind(CS_TAB_VIDEO, 1) == CS_ROW_CYCLE
          && csRowKind(CS_TAB_VIDEO, 2) == CS_ROW_SLIDER && csRowKind(CS_TAB_VIDEO, 3) == CS_ROW_ACTION);
    CHECK(csRowLabel(CS_TAB_VIDEO, 2, buf, sizeof(buf)) > 0 && strcmp(buf, "Volume") == 0);
    CHECK(csRowDesc(CS_TAB_VIDEO, 0, buf, sizeof(buf)) > 0 && strcmp(buf, "Toggles fullscreen.") == 0);
    CHECK(csRowDesc(CS_TAB_VIDEO, 1, buf, sizeof(buf)) == 0);

    csOpen();
    csSetTab(CS_TAB_VIDEO);
    /* rows wrap in both directions */
    CHECK(csCursor() == 0 && csAct(CS_ACT_UP) == 1 && csCursor() == 3 && csAct(CS_ACT_DOWN) == 1 && csCursor() == 0);
    /* toggle: either arrow and confirm flip it, each change calls apply */
    CHECK(csRowValue(CS_TAB_VIDEO, INPUT_DEV_KEYBOARD, 0, buf, sizeof(buf)) > 0 && strcmp(buf, "Off") == 0);
    CHECK(csAct(CS_ACT_RIGHT) == 1 && csWidgetGet(CS_TAB_VIDEO, 0) == 1 && applied == 1 && appliedValue == 1);
    CHECK(csRowValue(CS_TAB_VIDEO, INPUT_DEV_KEYBOARD, 0, buf, sizeof(buf)) > 0 && strcmp(buf, "On") == 0);
    CHECK(csAct(CS_ACT_LEFT) == 1 && csWidgetGet(CS_TAB_VIDEO, 0) == 0);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && csWidgetGet(CS_TAB_VIDEO, 0) == 1 && applied == 3);
    /* cycle wraps both ways, shows the value name */
    csAct(CS_ACT_DOWN);
    CHECK(csRowValue(CS_TAB_VIDEO, INPUT_DEV_KEYBOARD, 1, buf, sizeof(buf)) > 0 && strcmp(buf, "Low") == 0);
    CHECK(csAct(CS_ACT_LEFT) == 1 && csWidgetGet(CS_TAB_VIDEO, 1) == 2);
    CHECK(csRowValue(CS_TAB_VIDEO, INPUT_DEV_KEYBOARD, 1, buf, sizeof(buf)) > 0 && strcmp(buf, "High") == 0);
    CHECK(csAct(CS_ACT_RIGHT) == 1 && csWidgetGet(CS_TAB_VIDEO, 1) == 0);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && csWidgetGet(CS_TAB_VIDEO, 1) == 1);
    /* slider clamps at both ends and reports a change only when it moved */
    csAct(CS_ACT_DOWN);
    CHECK(csRowFraction(CS_TAB_VIDEO, 2) == 500);
    CHECK(csAct(CS_ACT_RIGHT) == 1 && csWidgetGet(CS_TAB_VIDEO, 2) == 75);
    CHECK(csAct(CS_ACT_RIGHT) == 1 && csWidgetGet(CS_TAB_VIDEO, 2) == 100);
    applied = 0;
    CHECK(csAct(CS_ACT_RIGHT) == 0 && csWidgetGet(CS_TAB_VIDEO, 2) == 100 && applied == 0);
    CHECK(csRowValue(CS_TAB_VIDEO, INPUT_DEV_KEYBOARD, 2, buf, sizeof(buf)) > 0 && strcmp(buf, "100") == 0);
    CHECK(csRowFraction(CS_TAB_VIDEO, 2) == 1000 && csRowFraction(CS_TAB_VIDEO, 0) == -1);
    csWidgetSet(CS_TAB_VIDEO, 2, 10);
    CHECK(csAct(CS_ACT_LEFT) == 1 && csWidgetGet(CS_TAB_VIDEO, 2) == 0); /* 10 - 25 clamps to min */
    CHECK(csAct(CS_ACT_LEFT) == 0 && csWidgetGet(CS_TAB_VIDEO, 2) == 0);
    CHECK(csAct(CS_ACT_CONFIRM) == 0);
    CHECK(csWidgetSet(CS_TAB_VIDEO, 2, 500) == 100 && csWidgetSet(CS_TAB_VIDEO, 2, -9) == 0);
    /* an action row runs its callback; left / right do nothing there */
    csAct(CS_ACT_DOWN);
    applied = 0;
    CHECK(csAct(CS_ACT_LEFT) == 0 && csAct(CS_ACT_RIGHT) == 0 && applied == 0);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && applied == 1);
    /* the widget tab is not the Controls tab: left / right never switch device there */
    CHECK(csDevice() == INPUT_DEV_KEYBOARD);
    /* clearing returns the tab to a placeholder */
    csWidgetsClear();
    CHECK(csTabRowCount(CS_TAB_VIDEO) == 0 && csTabPlaceholder(CS_TAB_VIDEO) == 1);
    csClose();
    csSetTab(CS_TAB_CONTROLS);
    inputCfgReset(INPUT_DEV_ALL);
}

/* O1: the shared settings file: controls sections with comments plus the menu rows' own */
static int sApplied;

static void sApply(int value) {
    sApplied = value + 1000;
}

static void addSettingRows(void) {
    static const char *const filters[3] = {"Sharp", "Smooth", "Crt"};
    CsWidget w;

    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_TOGGLE;
    w.label = "Fullscreen";
    w.key = "fullscreen";
    w.comment = "Start in fullscreen.";
    csWidgetAdd(CS_TAB_VIDEO, &w);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_CYCLE;
    w.label = "Filter";
    w.key = "filter";
    w.names = filters;
    w.count = 3;
    w.apply = sApply;
    csWidgetAdd(CS_TAB_VIDEO, &w);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_SLIDER;
    w.label = "Music";
    w.key = "music_volume";
    w.desc = "Music volume.";
    w.min = 0;
    w.max = 100;
    w.step = 10;
    w.value = 80;
    csWidgetAdd(CS_TAB_AUDIO, &w);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_ACTION;
    w.label = "Test";
    w.key = "ignored_for_actions";
    csWidgetAdd(CS_TAB_AUDIO, &w);
}

static void testSettingsFile(const char *dir) {
    static char text[16384], text2[16384];
    char path[512];
    InputCfg got, d;
    const char *p;
    int n, lines = 0, comments = 0;
    FILE *f;

    csSettingsInstall();
    csWidgetsClear();
    addSettingRows();
    CHECK(csTabRowCount(CS_TAB_AUDIO) == 2);
    inputCfgDefaults(&d);
    n = inputCfgFormat(&d, text, sizeof(text));
    CHECK(n > 0 && strstr(text, "[controls.keyboard]") != NULL && strstr(text, "[controls.controller]") != NULL);
    CHECK(strstr(text, "[video]\n") != NULL && strstr(text, "[audio]\n") != NULL && strstr(text, "[display]") == NULL);
    CHECK(strstr(text, "fullscreen = off") != NULL && strstr(text, "filter = sharp") != NULL
          && strstr(text, "music_volume = 80") != NULL && strstr(text, "ignored_for_actions") == NULL);
    CHECK(strstr(text, "one of Sharp , Smooth , Crt") == NULL); /* the value list reads cleanly */
    CHECK(strstr(text, "Values: on or off.") != NULL && strstr(text, "Values: a number from 0 to 100.") != NULL);
    /* every key line has a comment line right above it */
    for (p = text; *p != '\0';) {
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);

        if (len > 0 && p[0] != ';' && p[0] != '[' && memchr(p, '=', len) != NULL) {
            const char *prev = p - 1;

            lines++;
            while (prev > text && prev[-1] != '\n') {
                prev--;
            }
            comments += prev >= text && prev < p - 1 && prev[0] == ';';
        }
        p += len + (nl != NULL);
    }
    CHECK(lines == 24 + 3 + 2 && comments == lines); /* the [debug] log and channels lines included */

    /* change rows, write, read into a fresh row set: the file order is the rows' */
    csWidgetSet(CS_TAB_VIDEO, 0, 1);
    csWidgetSet(CS_TAB_VIDEO, 1, 2);
    csWidgetSet(CS_TAB_AUDIO, 0, 30);
    n = inputCfgFormat(&d, text, sizeof(text));
    CHECK(n > 0 && strstr(text, "fullscreen = on") && strstr(text, "filter = crt") && strstr(text, "music_volume = 30"));
    csWidgetsClear();
    addSettingRows();          /* (a) rows first, then the file */
    sApplied = 0;
    got = d;
    logCount = 0;
    CHECK(inputCfgParse(text, &got, logSink) == 0 && logCount == 0);
    CHECK(csWidgetGet(CS_TAB_VIDEO, 0) == 1 && csWidgetGet(CS_TAB_VIDEO, 1) == 2 && csWidgetGet(CS_TAB_AUDIO, 0) == 30);
    CHECK(sApplied == 1002); /* the file value is applied */
    csWidgetsClear();          /* (b) the file first (boot order), then the rows */
    sApplied = 0;
    logCount = 0;
    CHECK(inputCfgParse(text, &got, logSink) == 0 && logCount == 0);
    CHECK(csTabRowCount(CS_TAB_VIDEO) == 0);
    addSettingRows();
    CHECK(csWidgetGet(CS_TAB_VIDEO, 0) == 1 && csWidgetGet(CS_TAB_VIDEO, 1) == 2 && csWidgetGet(CS_TAB_AUDIO, 0) == 30);
    CHECK(sApplied == 1002);
    n = inputCfgFormat(&d, text2, sizeof(text2));
    CHECK(n > 0 && strcmp(text, text2) == 0); /* byte-identical round trip */

    /* wrong values are logged and ignored; unknown keys are kept for the report; names are case-insensitive */
    csWidgetsClear();
    addSettingRows();
    logCount = 0;
    CHECK(inputCfgParse("[video]\nfullscreen = maybe\nfilter = bogus\nFILTER = SMOOTH\nnope = 1\n"
                        "[Audio]\nmusic_volume = 500\n[bogus2]\nx = 1\n[audio]\nmusic_volume = 12abc\n", &got, logSink) == 6);
    CHECK(csWidgetGet(CS_TAB_VIDEO, 0) == 0 && csWidgetGet(CS_TAB_VIDEO, 1) == 1 && csWidgetGet(CS_TAB_AUDIO, 0) == 80);
    logCount = 0;
    csSettingsReportUnknown(logSink);
    CHECK(logCount == 1 && strstr(lastLog, "nope") != NULL);
    csSettingsReportUnknown(logSink);
    CHECK(logCount == 1); /* reported once */
    CHECK(inputCfgParse("[controls.keyboard]\ncross = F13\n[controls.controller]\ncross = b\n", &got, logSink) == 0);
    CHECK(got.kb.kc_cross == SDL_SCANCODE_F13 && got.pad.gc_cross == SDL_CONTROLLER_BUTTON_B);

    /* the file on disk */
    snprintf(path, sizeof(path), "%s/inputtest_settings.ini", dir);
    remove(path);
    inputCfgDefaults(&got);
    CHECK(inputCfgParse("[controls.keyboard]\ncross = F7\n[controls.controller]\ncross = y\n", &got, logSink) == 0);
    CHECK(got.kb.kc_cross == SDL_SCANCODE_F7 && got.pad.gc_cross == SDL_CONTROLLER_BUTTON_Y);
    csWidgetsClear();
    addSettingRows();
    csWidgetSet(CS_TAB_AUDIO, 0, 55);
    CHECK(inputCfgSaveFile(path, &got) == 0);
    csWidgetsClear();
    addSettingRows();
    d = got;
    memset(&d.kb, 0, sizeof(d.kb));
    CHECK(inputCfgLoadFile(path, &d, logSink) == 0 && d.kb.kc_cross == SDL_SCANCODE_F7 && csWidgetGet(CS_TAB_AUDIO, 0) == 55);
    remove(path);
    csWidgetsClear();
}

/* O2: window-size options from a desktop size, the FPS counter's math, the Video/Display rows' file keys */
static void testOptionsVideo(void) {
    static char text[16384];
    OptFps f;
    InputCfg d, got;
    unsigned t;
    int i, r;

    CHECK(optWindowMaxScale(1920, 1080) == 4 && optWindowMaxScale(1920, 1200) == 5);
    CHECK(optWindowMaxScale(640, 480) == 2 && optWindowMaxScale(639, 480) == 1);
    CHECK(optWindowMaxScale(3840, 2160) == OPT_WINDOW_MAX_SCALE && optWindowMaxScale(200, 100) == 1);
    CHECK(optWindowMaxScale(0, 0) == 1 && optWindowMaxScale(-5, 1080) == 1);
    CHECK(optWindowScaleOf(320, 240, 4) == 1 && optWindowScaleOf(960, 720, 4) == 3);
    CHECK(optWindowScaleOf(960, 720, 2) == 0 && optWindowScaleOf(640, 481, 4) == 0);
    CHECK(optWindowScaleOf(0, 0, 4) == 0 && optWindowScaleOf(800, 600, 4) == 0);

    optFpsReset(&f, 5000);
    CHECK(optFpsTick(&f, 5020) == -1 && f.fps == -1); /* no full second yet */
    for (i = 2, r = -1, t = 5040; i <= 50; i++, t += 20) {
        r = optFpsTick(&f, t);
    }
    CHECK(r == 50 && f.frames == 0 && f.startMs == 6000); /* 50 frames in 1000 ms */
    CHECK(optFpsTick(&f, 6016) == 50); /* the last reading stays until the next second */
    optFpsReset(&f, 0);
    for (i = 1, r = -1, t = 100; i <= 10; i++, t += 100) {
        r = optFpsTick(&f, t);
    }
    CHECK(r == 10); /* a slow game reads 10 */
    optFpsReset(&f, 0xFFFFFF00u);
    for (i = 0, r = -1, t = 0xFFFFFF00u; i < 80; i++) {
        t += 16;
        r = optFpsTick(&f, t);
    }
    CHECK(r == 63 || r == 62); /* 63 frames over 1008 ms across the 32 bit wrap */
    optFpsReset(&f, 0);
    CHECK(optFpsTick(&f, 2000) == 1 && f.frames == 0); /* one frame after a long stall: 1 fps */

    /* integer scaling: the biggest whole multiple of the 320x240 image, centred */
    {
        int x, y, w, h;

        PsyX_CalcScreenRect(1000, 700, 320, 240, 1, &x, &y, &w, &h);
        CHECK(w == 640 && h == 480 && x == 180 && y == 110); /* 2x: width 3.1x, height 2.9x */
        PsyX_CalcScreenRect(1920, 1080, 320, 240, 1, &x, &y, &w, &h);
        CHECK(w == 1280 && h == 960 && x == 320 && y == 60); /* 4x does not fit 1080 */
        PsyX_CalcScreenRect(640, 480, 320, 240, 1, &x, &y, &w, &h);
        CHECK(w == 640 && h == 480 && x == 0 && y == 0); /* an exact fit has no borders */
        PsyX_CalcScreenRect(800, 240, 320, 240, 1, &x, &y, &w, &h);
        CHECK(w == 320 && h == 240 && x == 240 && y == 0); /* the height limits it */
        PsyX_CalcScreenRect(300, 200, 320, 240, 1, &x, &y, &w, &h);
        CHECK(w == 300 && h == 200 && x == 0 && y == 0); /* smaller than 1x: all of the window */
        PsyX_CalcScreenRect(1000, 700, 320, 240, 0, &x, &y, &w, &h);
        CHECK(w == 1000 && h == 700 && x == 0 && y == 0); /* Fit stretches */
        /* keep aspect (mode 2): the biggest picture of the game's own shape, centred, black bars on the long side */
        PsyX_CalcScreenRect(2712, 1220, 320, 240, 2, &x, &y, &w, &h);
        CHECK(w == 1626 && h == 1220 && x == 543 && y == 0); /* the 20:9 phone: pillarbox */
        PsyX_CalcScreenRect(1000, 700, 320, 240, 2, &x, &y, &w, &h);
        CHECK(w == 933 && h == 700 && x == 33 && y == 0); /* 4:3 of the height */
        PsyX_CalcScreenRect(1000, 1000, 320, 240, 2, &x, &y, &w, &h);
        CHECK(w == 1000 && h == 750 && x == 0 && y == 125); /* a square window: letterbox */
        PsyX_CalcScreenRect(640, 480, 320, 240, 2, &x, &y, &w, &h);
        CHECK(w == 640 && h == 480 && x == 0 && y == 0); /* an exact 4:3 window has no bars */
        PsyX_CalcScreenRect(300, 200, 320, 240, 2, &x, &y, &w, &h);
        CHECK(w == 266 && h == 200 && x == 17 && y == 0); /* smaller than 1x still keeps the shape (not whole multiples) */
        PsyX_CalcScreenRect(1920, 1080, 320, 416, 2, &x, &y, &w, &h);
        CHECK(w == 830 && h == 1080 && x == 545 && y == 0); /* the function keeps the shape it is given; the renderer passes 4:3 */
        PsyX_CalcScreenRect(0, 0, 320, 240, 2, &x, &y, &w, &h);
        CHECK(w == 0 && h == 0); /* a minimised window does not divide by zero */
    }

    /* the rows: keys written, read back, hotkeys visible */
    csSettingsInstall();
    csWidgetsClear();
    optionsVideoInit();
    CHECK(csTabRowCount(CS_TAB_VIDEO) == 6 && csTabRowCount(CS_TAB_DISPLAY) == 4);
    /* Battle speed and Stage banner moved to Enhancements and kept their [display] keys */
    CHECK(csTabRowCount(CS_TAB_ENHANCEMENTS) == 6);
    /* the three fast-transition rows: Fast by default, saved in [enhancements] */
    CHECK(csWidgetGet(CS_TAB_ENHANCEMENTS, 2) == FAST_MODE_FAST && csWidgetGet(CS_TAB_ENHANCEMENTS, 3) == FAST_MODE_FAST
          && csWidgetGet(CS_TAB_ENHANCEMENTS, 4) == FAST_MODE_FAST
          && csWidgetGet(CS_TAB_ENHANCEMENTS, 5) == FAST_MODE_FAST && fastMapMode() == FAST_MODE_FAST);
    CHECK(fastDoorMode() == FAST_MODE_FAST && fastBattleEntryMode() == FAST_MODE_FAST && fastResultsMode() == FAST_MODE_FAST);
    fastDoorSetMode(99);
    CHECK(fastDoorMode() == FAST_MODE_FAST); /* a bad mode is Fast */
    {
        static char fileText[16384];
        InputCfg dflt;

        inputCfgDefaults(&dflt);
        CHECK(inputCfgFormat(&dflt, fileText, sizeof(fileText)) > 0 && strstr(fileText, "battle_speed = normal") != NULL
              && strstr(fileText, "stage_banner = fast") != NULL && strstr(fileText, "[enhancements]\n") != NULL
              && strstr(fileText, "door_transition = fast") != NULL && strstr(fileText, "battle_transition = fast") != NULL
              && strstr(fileText, "battle_results = fast") != NULL);
        /* battle_speed and stage_banner stay in [display], the new keys are in [enhancements] */
        CHECK(strstr(fileText, "[display]") != NULL && strstr(fileText, "battle_speed") < strstr(fileText, "[enhancements]")
              && strstr(fileText, "door_transition") > strstr(fileText, "[enhancements]"));
    }
    g_cfg_bilinearFiltering = 1;
    g_cfg_integerScale = 0;
    g_cfg_scanlines = 0;
    g_cfg_pgxpTextureCorrection = 1;
    g_cfg_swapInterval = 0;
    csWidgetsClear();
    optionsVideoInit();
    CHECK(g_cfg_bilinearFiltering == 0); /* the old smooth mode cannot come back */
    /* rows: 0 Fullscreen, 1 Window size, 2 Scaling, 3 Scanlines, 4 Texture correction, 5 VSync */
    CHECK(csWidgetGet(CS_TAB_VIDEO, 2) == 0 && csWidgetGet(CS_TAB_VIDEO, 3) == 0);
    CHECK(csWidgetGet(CS_TAB_VIDEO, 4) == 1 && csWidgetGet(CS_TAB_VIDEO, 5) == 0);
    g_cfg_integerScale = 1;
    g_cfg_scanlines = 1;
    g_cfg_pgxpTextureCorrection = 0; /* F5 */
    CHECK(csWidgetGet(CS_TAB_VIDEO, 2) == 1 && csWidgetGet(CS_TAB_VIDEO, 3) == 1 && csWidgetGet(CS_TAB_VIDEO, 4) == 0);
    {
        char val[32];

        csRowValue(CS_TAB_VIDEO, 0, 2, val, sizeof(val));
        CHECK(strcmp(val, "Integer") == 0);
    }
    csWidgetSet(CS_TAB_VIDEO, 2, 2);
    CHECK(g_cfg_integerScale == 2 && csWidgetGet(CS_TAB_VIDEO, 2) == 2); /* the third value: keep the aspect */
    {
        char val[32];

        csRowValue(CS_TAB_VIDEO, 0, 2, val, sizeof(val));
        CHECK(strcmp(val, "Aspect") == 0);
    }
    csWidgetSet(CS_TAB_VIDEO, 2, 0);
    csWidgetSet(CS_TAB_VIDEO, 3, 0);
    csWidgetSet(CS_TAB_VIDEO, 4, 1);
    csWidgetSet(CS_TAB_VIDEO, 5, 1);
    CHECK(g_cfg_integerScale == 0 && g_cfg_scanlines == 0 && g_cfg_pgxpTextureCorrection == 1 && g_cfg_swapInterval == 1);
    csWidgetSet(CS_TAB_VIDEO, 2, 1);
    csWidgetSet(CS_TAB_VIDEO, 3, 1);
    csWidgetSet(CS_TAB_DISPLAY, 0, 1);
    inputCfgDefaults(&d);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0);
    CHECK(strstr(text, "[video]\n") != NULL && strstr(text, "[display]\n") != NULL);
    CHECK(strstr(text, "fullscreen = off") && strstr(text, "window_size = 1x") && strstr(text, "scaling = integer") &&
          strstr(text, "scanlines = on") && strstr(text, "filtering") == NULL);
    CHECK(strstr(text, "texture_correction = on") && strstr(text, "vsync = on") && strstr(text, "show_fps = on"));
    /* file -> rows (boot order: file first, then the rows) */
    csWidgetsClear();
    got = d;
    CHECK(inputCfgParse("[video]\nfiltering = smooth\nscaling = fit\nscanlines = on\ntexture_correction = off\nvsync = off\n[display]\nshow_fps = off\n",
                        &got, logSink) == 0);
    optionsVideoInit();
    /* the old filtering key is an unknown setting now: logged and ignored, smooth never turns on */
    CHECK(g_cfg_bilinearFiltering == 0 && g_cfg_pgxpTextureCorrection == 0 && g_cfg_swapInterval == 0);
    CHECK(g_cfg_integerScale == 0 && g_cfg_scanlines == 1);
    CHECK(csWidgetGet(CS_TAB_VIDEO, 2) == 0 && csWidgetGet(CS_TAB_VIDEO, 3) == 1 && csWidgetGet(CS_TAB_DISPLAY, 0) == 0);
    csWidgetsClear();
    got = d;
    CHECK(inputCfgParse("[video]\nscaling = aspect\n", &got, logSink) == 0);
    optionsVideoInit();
    CHECK(g_cfg_integerScale == 2); /* the file value reaches the renderer */
    /* an older file keeps working: the moved rows still read [display] */
    csWidgetsClear();
    got = d;
    CHECK(inputCfgParse("[display]\nbattle_speed = x2\nstage_banner = classic\n", &got, logSink) == 0);
    optionsVideoInit();
    CHECK(csWidgetGet(CS_TAB_ENHANCEMENTS, 0) == BATTLE_SPEED_X2 && battleSpeedMode() == BATTLE_SPEED_X2);
    CHECK(csWidgetGet(CS_TAB_ENHANCEMENTS, 1) == STAGE_BANNER_CLASSIC && stageBannerMode() == STAGE_BANNER_CLASSIC);
    /* and the new rows read their own section */
    csWidgetsClear();
    got = d;
    CHECK(inputCfgParse("[enhancements]\ndoor_transition = classic\nbattle_transition = classic\nbattle_results = classic\nmap_loading = classic\n", &got, logSink) == 0);
    optionsVideoInit();
    CHECK(fastDoorMode() == FAST_MODE_CLASSIC && fastBattleEntryMode() == FAST_MODE_CLASSIC && fastResultsMode() == FAST_MODE_CLASSIC
          && fastMapMode() == FAST_MODE_CLASSIC);
    fastMapSetMode(FAST_MODE_FAST);
    fastDoorSetMode(FAST_MODE_FAST);
    fastBattleEntrySetMode(FAST_MODE_FAST);
    fastResultsSetMode(FAST_MODE_FAST);
    battleSpeedSetMode(BATTLE_SPEED_NORMAL);
    stageBannerSetMode(STAGE_BANNER_FAST);
    g_cfg_integerScale = 0;
    g_cfg_scanlines = 0;
    g_cfg_pgxpTextureCorrection = 1;
    csWidgetsClear();
}

/* O4: the Audio rows -> the sliders of snd_vol.c, settings round trip */
static void testOptionsAudio(void) {
    static const char *const keys[OPT_AUDIO_ROWS] = {"master_volume", "music_volume", "effects_volume", "movie_volume"};
    static const int which[OPT_AUDIO_ROWS] = {SND_VOL_MASTER, SND_VOL_MUSIC, SND_VOL_EFFECTS, SND_VOL_MOVIE};
    InputCfg d, got;
    char text[4096], val[32], want[64];
    int i;

    for (i = 0; i < OPT_AUDIO_ROWS; i++) {
        sndSetUserVolume(which[i], 40); /* leftovers of another test must not matter */
    }
    csSettingsInstall();
    csWidgetsClear();
    optionsAudioInit();
    CHECK(csTabRowCount(CS_TAB_AUDIO) == OPT_AUDIO_ROWS && !csTabPlaceholder(CS_TAB_AUDIO));
    inputCfgDefaults(&d);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "[audio]\n") != NULL);
    for (i = 0; i < OPT_AUDIO_ROWS; i++) {
        CHECK(csRowKind(CS_TAB_AUDIO, i) == CS_ROW_SLIDER && csWidgetGet(CS_TAB_AUDIO, i) == 100);
        snprintf(want, sizeof(want), "%s = 100", keys[i]);
        CHECK(strstr(text, want) != NULL);
    }
    /* no file entry: the defaults, full volume, are in force */
    CHECK(sndUserVolume(SND_VOL_MUSIC) == 40); /* untouched until a row is set (the default equals the engine's own) */
    csWidgetSet(CS_TAB_AUDIO, OPT_AUDIO_MUSIC, 50);
    CHECK(sndUserVolume(SND_VOL_MUSIC) == 50 && sndUserGainPermille(SND_VOL_MUSIC) < 1000);
    csRowValue(CS_TAB_AUDIO, 0, OPT_AUDIO_MUSIC, val, sizeof(val));
    CHECK(strcmp(val, "50") == 0 && csRowFraction(CS_TAB_AUDIO, OPT_AUDIO_MUSIC) == 500);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "music_volume = 50") != NULL);
    /* the keys move by the step and stop at both ends */
    csOpen();
    csSetTab(CS_TAB_AUDIO);
    csSetCursor(OPT_AUDIO_EFFECTS);
    csAct(CS_ACT_LEFT);
    CHECK(csWidgetGet(CS_TAB_AUDIO, OPT_AUDIO_EFFECTS) == 100 - OPT_AUDIO_STEP && sndUserVolume(SND_VOL_EFFECTS) == 95);
    csAct(CS_ACT_RIGHT);
    csAct(CS_ACT_RIGHT);
    CHECK(csWidgetGet(CS_TAB_AUDIO, OPT_AUDIO_EFFECTS) == 100);
    csWidgetSet(CS_TAB_AUDIO, OPT_AUDIO_EFFECTS, 0);
    csAct(CS_ACT_LEFT);
    CHECK(csWidgetGet(CS_TAB_AUDIO, OPT_AUDIO_EFFECTS) == 0 && sndUserGainPermille(SND_VOL_EFFECTS) == 0);
    csClose();
    csWidgetsClear();

    /* the file's values apply when the rows are added; a bad one is ignored */
    got = d;
    CHECK(inputCfgParse("[audio]\nmaster_volume = 80\nmusic_volume = 25\neffects_volume = 140\nmovie_volume = 0\n",
                        &got, logSink) == 0);
    for (i = 0; i < OPT_AUDIO_ROWS; i++) {
        sndSetUserVolume(which[i], 100);
    }
    optionsAudioInit();
    CHECK(sndUserVolume(SND_VOL_MASTER) == 80 && sndUserVolume(SND_VOL_MUSIC) == 25 && sndUserVolume(SND_VOL_MOVIE) == 0);
    CHECK(sndUserVolume(SND_VOL_EFFECTS) == 100 && csWidgetGet(CS_TAB_AUDIO, OPT_AUDIO_EFFECTS) == 100);
    for (i = 0; i < OPT_AUDIO_ROWS; i++) {
        sndSetUserVolume(which[i], 100);
    }
    csWidgetsClear();
}

/* The restart handoff: the flag is stripped, and a new process waits for the old one to be gone */
static void testRestartHandoff(void) {
    char *v1[] = {"dw3", "--wait-pid", "4242", "disc.bin", NULL};
    char *v2[] = {"dw3", "a", "--wait-pid", "x", "b", NULL};
    char *v3[] = {"dw3", "--wait-pid", NULL};
    char *v4[] = {"dw3", "a", NULL};
    int c1 = 4, c2 = 5, c3 = 2, c4 = 2;
    unsigned long pid = 0;
    int fd[2];
    pid_t old, waiter;
    struct timespec t0, t1;
    long ms = 0;

    CHECK(optSysTakeWaitPid(&c1, v1, &pid) == 1 && pid == 4242 && c1 == 2 && strcmp(v1[1], "disc.bin") == 0 && v1[2] == NULL);
    pid = 0;
    CHECK(optSysTakeWaitPid(&c2, v2, &pid) == 0 && pid == 0 && c2 == 3 && strcmp(v2[1], "a") == 0 && strcmp(v2[2], "b") == 0); /* bad number: dropped */
    CHECK(optSysTakeWaitPid(&c3, v3, &pid) == 0 && c3 == 2); /* no number: nothing to take */
    CHECK(optSysTakeWaitPid(&c4, v4, &pid) == 0 && c4 == 2 && strcmp(v4[1], "a") == 0);

    /* a process that already ended is not waited for; one that is alive is waited for until it ends */
    CHECK(optSysWaitForPid(2147483000ul, 1000) == 0);
    CHECK(pipe(fd) == 0);
    old = fork();
    if (old == 0) {
        struct timespec nap = {0, 400 * 1000000L};

        nanosleep(&nap, NULL);
        _exit(0);
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    waiter = fork();
    if (waiter == 0) {
        long w = optSysWaitForPid((unsigned long)old, 5000);

        if (write(fd[1], &w, sizeof(w)) != (ssize_t)sizeof(w)) {
            _exit(2);
        }
        _exit(0);
    }
    waitpid(old, NULL, 0); /* the old one is reaped at about 400 ms; only then is it really gone */
    waitpid(waiter, NULL, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK(read(fd[0], &ms, sizeof(ms)) == (ssize_t)sizeof(ms));
    close(fd[0]);
    close(fd[1]);
    CHECK(ms >= 300 && ms < 3000); /* it did wait (not 0) and it did not time out (-1) */
    CHECK((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000 >= 300);
    {
        pid_t alive = fork();

        if (alive == 0) {
            struct timespec nap = {2, 0};

            nanosleep(&nap, NULL);
            _exit(0);
        }
        CHECK(optSysWaitForPid((unsigned long)alive, 100) == -1); /* a process that stays: the wait gives up */
        kill(alive, SIGKILL);
        waitpid(alive, NULL, 0);
    }
}

/* system-menu: the System tab's two rows and the confirm-twice model */
static void testOptionsSystem(void) {
    OptSysConfirm c;
    char buf[64];
    unsigned e = csNavEpoch();

    /* the pure model: the first press arms, the second acts, anything else disarms */
    optSysConfirmInit(&c);
    CHECK(!optSysArmed(&c, OPT_SYS_RESTART, e, 0) && !optSysArmed(&c, OPT_SYS_QUIT, e, 0));
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e, 1000) == 0 && optSysArmed(&c, OPT_SYS_QUIT, e, 1000));
    CHECK(!optSysArmed(&c, OPT_SYS_RESTART, e, 1000)); /* only that row */
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e, 1500) == 1 && !optSysArmed(&c, OPT_SYS_QUIT, e, 1500)); /* acts, disarms */
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e, 1600) == 0); /* the next one arms again */
    CHECK(optSysPress(&c, OPT_SYS_RESTART, e, 1700) == 0 && !optSysArmed(&c, OPT_SYS_QUIT, e, 1700)); /* another row takes over */
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e, 1800) == 0); /* it did not act on the first row */
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e + 1, 1900) == 0); /* the cursor moved in between: arms, does not act */
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e + 1, 2000 + OPT_SYS_CONFIRM_MS) == 0); /* too late: arms again */
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e + 1, 20000) == 0 && optSysArmed(&c, OPT_SYS_QUIT, e + 1, 20000 + OPT_SYS_CONFIRM_MS)
          && !optSysArmed(&c, OPT_SYS_QUIT, e + 1, 20000 + OPT_SYS_CONFIRM_MS + 1)); /* the window is closed at its end */
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e + 1, 4000000000u) == 0
          && optSysPress(&c, OPT_SYS_QUIT, e + 1, 4000000100u) == 1); /* an exact boundary and the clock's wrap are fine */
    CHECK(optSysPress(&c, OPT_SYS_QUIT, e, 5) == 0 && optSysPress(&c, OPT_SYS_QUIT, e, 100) == 1);
    CHECK(optSysPress(&c, -1, e, 5) == 0 && !optSysArmed(&c, -1, e, 5)); /* no row is never armed */

    /* the tab: two action rows, Restart then Quit, one short English description each */
    csSettingsInstall();
    csWidgetsClear();
    optionsSystemInit();
    CHECK(csTabRowCount(CS_TAB_SYSTEM) == OPT_SYS_ROWS && !csTabPlaceholder(CS_TAB_SYSTEM));
    CHECK(csRowKind(CS_TAB_SYSTEM, OPT_SYS_RESTART) == CS_ROW_ACTION && csRowKind(CS_TAB_SYSTEM, OPT_SYS_QUIT) == CS_ROW_ACTION);
    CHECK(csRowLabel(CS_TAB_SYSTEM, OPT_SYS_RESTART, buf, sizeof(buf)) > 0 && strcmp(buf, "Restart game") == 0);
    CHECK(csRowLabel(CS_TAB_SYSTEM, OPT_SYS_QUIT, buf, sizeof(buf)) > 0 && strcmp(buf, "Quit game") == 0);
    CHECK(csRowDesc(CS_TAB_SYSTEM, OPT_SYS_RESTART, buf, sizeof(buf)) > 0 && strlen(buf) <= 48);
    CHECK(csRowDesc(CS_TAB_SYSTEM, OPT_SYS_QUIT, buf, sizeof(buf)) > 0 && strlen(buf) <= 48);
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_QUIT, buf, sizeof(buf)) == 0 && buf[0] == '\0'); /* idle: no value */

    /* on the screen: the first Confirm arms the row, moving off it, another tab or closing the menu cancels */
    csOpen();
    csSetTab(CS_TAB_SYSTEM);
    csSetCursor(OPT_SYS_QUIT);
    CHECK(csAct(CS_ACT_CONFIRM) == 1);
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_QUIT, buf, sizeof(buf)) > 0 && strcmp(buf, "Press again to confirm") == 0);
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_RESTART, buf, sizeof(buf)) == 0); /* the other row is idle */
    CHECK(csAct(CS_ACT_UP) == 1 && csAct(CS_ACT_DOWN) == 1); /* off the row and back */
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_QUIT, buf, sizeof(buf)) == 0); /* cancelled */
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_QUIT, buf, sizeof(buf)) > 0);
    CHECK(csAct(CS_ACT_TAB_NEXT) == 1 && csAct(CS_ACT_TAB_PREV) == 1);
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_QUIT, buf, sizeof(buf)) == 0); /* a tab change cancels */
    csSetCursor(OPT_SYS_RESTART);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_RESTART, buf, sizeof(buf)) > 0);
    csClose();
    csOpen();
    csSetTab(CS_TAB_SYSTEM);
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_RESTART, buf, sizeof(buf)) == 0); /* closing the menu cancels */
    csClose();
    csWidgetsClear();

    /* port-log T1: the Log row (off, mods, trace), the third row: off by default, saved in [debug], applied from the file */
    csSettingsInstall();
    csWidgetsClear();
    optionsSystemInit();
    CHECK(csTabRowCount(CS_TAB_SYSTEM) == 3 && OPT_SYS_LOG == 2);
    CHECK(csRowKind(CS_TAB_SYSTEM, OPT_SYS_LOG) == CS_ROW_CYCLE);
    CHECK(csRowLabel(CS_TAB_SYSTEM, OPT_SYS_LOG, buf, sizeof(buf)) > 0 && strcmp(buf, "Log") == 0);
    CHECK(csRowDesc(CS_TAB_SYSTEM, OPT_SYS_LOG, buf, sizeof(buf)) > 0 && strlen(buf) <= 48);
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_LOG, buf, sizeof(buf)) > 0 && strcmp(buf, "Off") == 0);
    CHECK(csWidgetGet(CS_TAB_SYSTEM, OPT_SYS_LOG) == 0 && inputCfgLogLevel() == PLOG_OFF);
    {
        InputCfg d2, got2;
        char text2[4096];

        inputCfgDefaults(&d2);
        CHECK(inputCfgFormat(&d2, text2, sizeof(text2)) > 0);
        CHECK(strstr(text2, "log = off") != NULL && strstr(text2, "channels = \n") != NULL); /* written by default, so they can be found and edited */
        CHECK(strstr(text2, "mod_log") == NULL);
        csWidgetSet(CS_TAB_SYSTEM, OPT_SYS_LOG, 1); /* what a change in the menu does: the file value follows */
        CHECK(inputCfgLogLevel() == PLOG_MODS && plogOn("tables", PLOG_MODS) && !plogOn("tables", PLOG_TRACE));
        CHECK(inputCfgFormat(&d2, text2, sizeof(text2)) > 0 && strstr(text2, "log = mods") != NULL);
        csWidgetSet(CS_TAB_SYSTEM, OPT_SYS_LOG, 2);
        CHECK(inputCfgLogLevel() == PLOG_TRACE && plogOn("files", PLOG_TRACE));
        CHECK(csRowValue(CS_TAB_SYSTEM, 0, OPT_SYS_LOG, buf, sizeof(buf)) > 0 && strcmp(buf, "Trace") == 0);
        CHECK(inputCfgFormat(&d2, text2, sizeof(text2)) > 0 && strstr(text2, "log = trace") != NULL);
        csWidgetsClear();
        got2 = d2;
        CHECK(inputCfgParse("[debug]\nlog = off\n", &got2, logSink) == 0 && inputCfgLogLevel() == PLOG_OFF && !plogOn("tables", PLOG_MODS));
        CHECK(inputCfgParse("[Debug]\nLog = TRACE\nChannels = tables, Files\n", &got2, logSink) == 0 && inputCfgLogLevel() == PLOG_TRACE);
        CHECK(strcmp(inputCfgLogChannels(), "tables, Files") == 0 && plogOn("files", PLOG_TRACE) && !plogOn("images", PLOG_TRACE));
        CHECK(inputCfgFormat(&got2, text2, sizeof(text2)) > 0 && strstr(text2, "log = trace") != NULL && strstr(text2, "channels = tables, Files\n") != NULL);
        CHECK(inputCfgParse("[debug]\nchannels =\n", &got2, logSink) == 0 && inputCfgLogChannels()[0] == '\0');
        optionsSystemInit(); /* the file's value shows in the row when it is added */
        CHECK(csWidgetGet(CS_TAB_SYSTEM, OPT_SYS_LOG) == 2);
        csWidgetsClear();
        /* the old key: on is mods, off is off, and a save writes `log` */
        CHECK(inputCfgParse("[debug]\nmod_log = on\n", &got2, logSink) == 0 && inputCfgLogLevel() == PLOG_MODS);
        CHECK(inputCfgFormat(&got2, text2, sizeof(text2)) > 0 && strstr(text2, "log = mods") != NULL && strstr(text2, "mod_log") == NULL);
        CHECK(inputCfgParse("[Debug]\nMod_Log = OFF\n", &got2, logSink) == 0 && inputCfgLogLevel() == PLOG_OFF);
        CHECK(inputCfgParse("[debug]\nmod_log = on\n", &got2, logSink) == 0 && inputCfgLogLevel() == PLOG_MODS);
        optionsSystemInit();
        CHECK(csWidgetGet(CS_TAB_SYSTEM, OPT_SYS_LOG) == 1);
        csWidgetsClear();
        CHECK(inputCfgParse("[debug]\nmod_log = maybe\nlog = loud\nlog = 1\nother = 1\n", &got2, logSink) == 4 && inputCfgLogLevel() == PLOG_MODS); /* bad: logged, kept */
        inputCfgSetLogLevel(0);
        CHECK(inputCfgLogLevel() == PLOG_OFF && !plogOn("tables", PLOG_MODS));
    }
}

/* modding-ux U4: the "Prepare modding files" row and --export-all: one job over the four exporters */
static char prepLog[256];
static int prepStatusN[4], prepActiveFor[4], prepFails[4], prepQueued[4], prepForced[4], prepCalls[4];
static char prepText[4][32];

#define PREP_FAKE(N) \
    static int prepStart##N(const char *spec, int force) { \
        size_t l = strlen(prepLog); \
        (void)spec; \
        prepCalls[N]++; \
        prepForced[N] += force; \
        prepLog[l] = (char)('0' + N); \
        prepLog[l + 1] = '\0'; \
        if (prepQueued[N] <= 0) return prepQueued[N] == 0 ? 0 : -1; \
        prepStatusN[N] = 0; \
        prepActiveFor[N] = prepQueued[N]; \
        return prepQueued[N]; \
    } \
    static int prepPump##N(unsigned budgetMs) { \
        (void)budgetMs; \
        if (prepActiveFor[N] > 0) { \
            prepActiveFor[N]--; \
            prepStatusN[N]++; \
        } \
        return prepActiveFor[N] > 0; \
    } \
    static const char *prepStatus##N(void) { \
        snprintf(prepText[N], sizeof prepText[N], prepActiveFor[N] > 0 ? "Exporting %d/3" : "Done (3)", prepStatusN[N]); \
        return prepText[N]; \
    } \
    static int prepActive##N(void) { return prepActiveFor[N] > 0; } \
    static void prepCounts##N(int *done, int *failed, int *total) { *done = 0; *failed = prepFails[N]; *total = 3; } \
    static int prepRun##N(const char *spec) { (void)spec; return 0; }
PREP_FAKE(0)
PREP_FAKE(1)
PREP_FAKE(2)
PREP_FAKE(3)

#define PREP_ROW(N, KEY, NAME) \
    {KEY, NAME, NULL, NULL, NULL, prepRun##N, NULL, prepStart##N, prepPump##N, prepActive##N, prepCounts##N, prepStatus##N, NULL}

static void prepRegister(void) {
    /* the numbers are the fakes', and the table's order is the run order: 0 videos, 1 audio, 2 models, 3 stages */
    static const ExpType types[] = {PREP_ROW(0, "export_videos", "Videos"), PREP_ROW(1, "export_audio", "Audio"),
                                    PREP_ROW(2, "export_models", "Models"), PREP_ROW(3, "export_stages", "Stages")};
    expTypesSet(types, 4);
}

static void testPrepareModding(void) {
    char buf[96], seen[512];
    int i, frames;

    /* the command line flag: taken out of argv wherever it stands, the rest keeps its order */
    {
        char *av[6] = {"boot", "disc.bin", "--export-all", "100", "x.png", NULL};
        int ac = 5;
        CHECK(inputCfgTakeFlag(&ac, av, "--export-all") == 1 && ac == 4);
        CHECK(!strcmp(av[0], "boot") && !strcmp(av[1], "disc.bin") && !strcmp(av[2], "100") && !strcmp(av[3], "x.png") && av[4] == NULL);
        CHECK(inputCfgTakeFlag(&ac, av, "--export-all") == 0 && ac == 4); /* gone: nothing more to take */
        CHECK(inputCfgTakeFlag(&ac, av, "--export-al") == 0 && inputCfgTakeFlag(&ac, av, "") == 0 && ac == 4); /* exact words only */
        {
            char *bv[4] = {"boot", "--export-all", "--export-all", NULL};
            int bc = 3;
            CHECK(inputCfgTakeFlag(&bc, bv, "--export-all") == 1 && bc == 1 && bv[1] == NULL); /* twice is the same as once */
        }
        {
            char *cv[3] = {"boot", "--export-all", NULL};
            int cc = 2;
            CHECK(inputCfgTakeFlag(&cc, cv, "--export-all") == 1 && cc == 1);
            CHECK(inputCfgTakeFlag(NULL, cv, "--export-all") == 0);
        }
    }

    /* no exporter registered: no row, nothing to start */
    csSettingsInstall();
    csWidgetsClear();
    expTypesSet(NULL, 0);
    optionsSystemInit();
    CHECK(csTabRowCount(CS_TAB_SYSTEM) == 3 && optionsSystemPrepStart() == 0 && !optionsSystemPrepActive());
    csWidgetsClear();

    /* the four old rows are one: Restart, Quit, Log, Prepare modding files */
    memset(prepStatusN, 0, sizeof prepStatusN);
    memset(prepFails, 0, sizeof prepFails);
    memset(prepForced, 0, sizeof prepForced);
    memset(prepCalls, 0, sizeof prepCalls);
    for (i = 0; i < 4; i++) prepQueued[i] = 2 + i;
    prepLog[0] = '\0';
    prepRegister();
    optionsSystemInit();
    CHECK(csTabRowCount(CS_TAB_SYSTEM) == 4 && csRowKind(CS_TAB_SYSTEM, 3) == CS_ROW_ACTION);
    CHECK(csRowLabel(CS_TAB_SYSTEM, 3, buf, sizeof(buf)) > 0 && strcmp(buf, "Prepare modding files") == 0);
    CHECK(csRowDesc(CS_TAB_SYSTEM, 3, buf, sizeof(buf)) > 0 && strlen(buf) <= 64);
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, 3, buf, sizeof(buf)) == 0 && buf[0] == '\0'); /* idle: no value */

    /* one press runs the four, one after the other (videos, audio, models, stages), each skipping what is up to date */
    csOpen();
    csSetTab(CS_TAB_SYSTEM);
    csSetCursor(3);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && optionsSystemPrepActive());
    CHECK(csAct(CS_ACT_CONFIRM) == 1); /* a second press while it runs changes nothing */
    CHECK(strcmp(prepLog, "0") == 0);
    seen[0] = '\0';
    for (frames = 0; frames < 100 && optionsSystemPrepActive(); frames++) {
        const char *st = optionsSystemPrepStatus();
        if (strstr(seen, st) == NULL && strlen(seen) + strlen(st) + 2 < sizeof seen) strcat(strcat(seen, st), ";");
        optionsSystemFrame();
    }
    CHECK(!optionsSystemPrepActive() && strcmp(prepLog, "0123") == 0);
    CHECK(prepCalls[0] == 1 && prepCalls[1] == 1 && prepCalls[2] == 1 && prepCalls[3] == 1);
    CHECK(prepForced[0] + prepForced[1] + prepForced[2] + prepForced[3] == 0); /* it never forces a rebuild */
    CHECK(strstr(seen, "Videos 0/3;") && strstr(seen, "Videos 1/3;") && strstr(seen, "Audio 0/3;") && strstr(seen, "Models 2/3;") &&
          strstr(seen, "Stages 4/3;")); /* the status of each step, with its own name */
    CHECK(strstr(seen, "Videos") < strstr(seen, "Audio") && strstr(seen, "Audio") < strstr(seen, "Models") && strstr(seen, "Models") < strstr(seen, "Stages"));
    CHECK(strcmp(optionsSystemPrepStatus(), "Done") == 0 && optionsSystemPrepFailed() == 0);
    CHECK(csRowValue(CS_TAB_SYSTEM, 0, 3, buf, sizeof(buf)) > 0 && strcmp(buf, "Done") == 0);

    /* pressing again starts again (and, up to date, ends at once: the exporters report nothing queued) */
    for (i = 0; i < 4; i++) prepQueued[i] = 0;
    prepLog[0] = '\0';
    CHECK(optionsSystemPrepStart() == 1);
    optionsSystemFrame();
    CHECK(!optionsSystemPrepActive() && strcmp(prepLog, "0123") == 0 && strcmp(optionsSystemPrepStatus(), "Done") == 0);

    /* a failing exporter: the others still run, the row says how many items failed, the command line gets a non-zero result */
    for (i = 0; i < 4; i++) prepQueued[i] = 1;
    prepFails[1] = 2;
    prepFails[3] = 1;
    CHECK(optionsSystemPrepStart() == 1);
    for (frames = 0; frames < 100 && optionsSystemPrepActive(); frames++) optionsSystemFrame();
    CHECK(!optionsSystemPrepActive() && optionsSystemPrepFailed() == 3 && strcmp(optionsSystemPrepStatus(), "Done, 3 failed") == 0);

    /* an exporter that says "nothing to do" (-1: no data folder, nothing valid) is skipped, not a failure */
    memset(prepFails, 0, sizeof prepFails);
    prepQueued[2] = -1;
    prepLog[0] = '\0';
    CHECK(optionsSystemPrepStart() == 1);
    for (frames = 0; frames < 100 && optionsSystemPrepActive(); frames++) optionsSystemFrame();
    CHECK(optionsSystemPrepFailed() == 0 && strcmp(prepLog, "0123") == 0 && strcmp(optionsSystemPrepStatus(), "Done") == 0);

    csClose();
    csWidgetsClear();
    expTypesSet(NULL, 0);
}

/* B3: the table of exporter types: setup in order, the settings.ini keys (given, default, off, absent), the notes in a save */
static char etLog[256];
static void etSetupA(const char *d) { snprintf(etLog + strlen(etLog), sizeof(etLog) - strlen(etLog), "sA:%s;", d != NULL ? d : "-"); }
static void etSetupB(const char *d) { (void)d; strcat(etLog, "sB;"); }
static int etRunA(const char *spec) { snprintf(etLog + strlen(etLog), sizeof(etLog) - strlen(etLog), "rA=%s;", spec); return 0; }
static int etRunB(const char *spec) { snprintf(etLog + strlen(etLog), sizeof(etLog) - strlen(etLog), "rB=%s;", spec); return 0; }
static int etRunC(const char *spec) { snprintf(etLog + strlen(etLog), sizeof(etLog) - strlen(etLog), "rC=%s;", spec); return 0; }
static void etAfterB(void) { strcat(etLog, "aB;"); }

static void testExportTypes(void) {
    static const ExpType types[] = {
        {"export_et_a", "A", "; note of a\n", NULL, etSetupA, etRunA, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
        {"export_et_b", "B", "; note of b\n", "all", etSetupB, etRunB, etAfterB, NULL, NULL, NULL, NULL, NULL, NULL},
        {"export_et_c", "C", NULL, NULL, NULL, etRunC, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    };
    const char *file = "inputtest_exptypes.ini";
    static char text[16384];
    InputCfg c;
    FILE *f;
    int i;

    /* every row is addressable and has a unique key and a run function */
    expTypesSet(types, 3);
    CHECK(expTypeCount() == 3 && expTypesJobCount() == 0 && !expTypeIsJob(expTypeAt(0)) && expTypeAt(3) == NULL && expTypeAt(-1) == NULL);
    for (i = 0; i < expTypeCount(); i++) {
        int j;
        CHECK(expTypeAt(i)->run != NULL && expTypeAt(i)->key != NULL && strncmp(expTypeAt(i)->key, "export_", 7) == 0);
        for (j = 0; j < i; j++) CHECK(strcmp(expTypeAt(i)->key, expTypeAt(j)->key) != 0);
    }
    f = fopen(file, "wb");
    CHECK(f != NULL);
    fprintf(f, "[data]\nexport_et_a = 12, 13\nexport_et_b = off\nexport_et_c = all\n");
    fclose(f);
    inputCfgSetPath(file);
    setenv("DW3_SETTINGS", file, 1);
    etLog[0] = '\0';
    expTypesSetup("data");
    CHECK(strcmp(etLog, "sA:data;sB;") == 0); /* in table order */
    etLog[0] = '\0';
    expTypesRunSettings("data");
    CHECK(strcmp(etLog, "rA=12, 13;rC=all;") == 0); /* b is off: neither run nor after */
    etLog[0] = '\0';
    expTypesRunSettings(NULL);
    CHECK(etLog[0] == '\0'); /* no data folder: nothing */
    f = fopen(file, "wb");
    CHECK(f != NULL);
    fprintf(f, "[data]\nexport_et_c = 5\n");
    fclose(f);
    etLog[0] = '\0';
    expTypesRunSettings("data");
    CHECK(strcmp(etLog, "rB=all;aB;rC=5;") == 0); /* the default of b, its after hook; a is absent and has none */
    /* a save writes each value back with the note the row registered (c has none) and one [data] header */
    inputCfgDefaults(&c);
    inputCfgLoadFile(file, &c, NULL);
    f = fopen(file, "wb");
    fprintf(f, "[data]\nexport_et_a = 1\nexport_et_c = 5\n");
    fclose(f);
    inputCfgReadExport("export_et_a", text, sizeof(text));
    inputCfgLoadFile(file, &c, NULL);
    CHECK(inputCfgFormat(&c, text, sizeof(text)) > 0);
    CHECK(strstr(text, "\n; note of a\n[data]\nexport_et_a = 1\n") != NULL && strstr(text, "\nexport_et_c = 5\n") != NULL);
    CHECK(strstr(text, "note of b") == NULL && strstr(strstr(text, "[data]") + 1, "[data]") == NULL);
    unsetenv("DW3_SETTINGS");
    remove(file);
    expTypesSet(NULL, 0);
    CHECK(expTypeCount() == 0 && expTypeAt(0) == NULL);
}

/* C4: controller type -> family, Auto vs forced icons, device tracking, button -> atlas cell, settings round trip */
/* ---- modding-ux U6: the Mods tab ---- */

static void putMod(const char *root, const char *name, const char *json) {
    char p[600];
    FILE *f;

    snprintf(p, sizeof p, "%s/%s", root, name);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/%s/mod.json", root, name);
    f = fopen(p, "wb");
    if (f != NULL) {
        fputs(json, f);
        fclose(f);
    }
}

static void modsBoot(const char *root) { /* what boot does: the file's [mods] lines into modsInit */
    char text[512];

    modsInit(root, inputCfgModsText(text, sizeof text) ? text : NULL);
}

static void modsRebuild(void) {
    csSettingsInstall();
    csWidgetsClear();
    optionsSystemInit();
}

static int plainText(const char *t) {
    return strchr(t, ',') == NULL && strchr(t, '(') == NULL && strchr(t, ')') == NULL;
}

/* the row's value and the help line, plain */
static const char *mv(int row) {
    static char b[8][64];
    static int n;
    char *o = b[n++ & 7];

    csRowValue(CS_TAB_MODS, 0, row, o, 64);
    CHECK(plainText(o));
    return o;
}

static const char *md(int row) {
    static char b[8][64];
    static int n;
    char *o = b[n++ & 7];

    csRowDesc(CS_TAB_MODS, row, o, 64);
    CHECK(plainText(o));
    return o;
}

static void readBack(const char *file) { /* a fresh load of what the tab saved */
    InputCfg c;

    inputCapFrame();
    inputCfgSetMods(NULL, NULL);
    inputCfgLoadFile(file, &c, NULL);
}

/* U7: the [mods] order and disabled lines have no length limit (256 mods and more): they are read, kept, saved and read again whole */
extern int inputCfgReadModsOrder(char *out, int cap); /* boot.c declares it the same way */

static void testLongModsLines(const char *dir) {
    char file[600], name[24];
    char *order, *dis, *want, *text;
    size_t oc = 0, dc = 0, cap = 40000;
    InputCfg c;
    FILE *f;
    int i;

    snprintf(file, sizeof file, "%s/inputtest_longmods.ini", dir);
    order = (char *)malloc(cap);
    dis = (char *)malloc(cap);
    want = (char *)malloc(cap * 2);
    text = (char *)malloc(cap * 3);
    CHECK(order != NULL && dis != NULL && want != NULL && text != NULL);
    order[0] = dis[0] = '\0';
    for (i = 0; i < 300; i++) { /* ~11 chars each: far past the old 512 bytes (and the 255 of a line) */
        snprintf(name, sizeof name, "modname%03d", i);
        oc += (size_t)snprintf(order + oc, cap - oc, "%s%s", i ? ", " : "", name);
        if (i % 3 == 0) dc += (size_t)snprintf(dis + dc, cap - dc, "%s%s", dc ? ", " : "", name);
    }
    CHECK(oc > 3000 && dc > 1000);
    f = fopen(file, "wb");
    CHECK(f != NULL);
    fprintf(f, "[mods]\norder = %s\ndisabled = %s\n", order, dis);
    fclose(f);
    inputCfgSetPath(file);
    inputCfgSetMods(NULL, NULL);
    /* read at boot (from the settings path boot resolves): the order and every disabled mod as "-name", all of it */
    setenv("DW3_SETTINGS", file, 1);
    CHECK(inputCfgReadModsOrder(text, (int)(cap * 3)) == 1);
    unsetenv("DW3_SETTINGS");
    CHECK(strncmp(text, order, oc) == 0 && strstr(text, ", -modname000") != NULL && strstr(text, "-modname297") != NULL);
    CHECK(strcmp(inputCfgModsOrderText(), order) == 0 && strcmp(inputCfgModsDisabledText(), dis) == 0);
    /* the tab's save and a fresh read give the same two lines back */
    CHECK(inputCfgSetMods(order, dis) == 0);
    inputCfgDefaults(&c);
    CHECK(inputCfgSaveFile(file, &c) == 0);
    inputCfgSetMods(NULL, NULL);
    CHECK(inputCfgModsOrderText()[0] == '\0');
    inputCfgLoadFile(file, &c, NULL);
    CHECK(strcmp(inputCfgModsOrderText(), order) == 0 && strcmp(inputCfgModsDisabledText(), dis) == 0);
    remove(file);
    inputCfgSetMods(NULL, NULL);
    inputCfgSetPath("");
    free(order);
    free(dis);
    free(want);
    free(text);
}

static void testOptionsMods(const char *dir) {
    char root[512], file[600], buf[64];
    int i;
    const char *order, *dis;

    snprintf(root, sizeof root, "%s/inputtest_mods", dir);
    snprintf(file, sizeof file, "%s/inputtest_mods.ini", dir);
    {
        char cmd[1200];
        snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", root, file);
        CHECK(system(cmd) == 0);
    }
    mkdir(root, 0755);
    inputCfgSetPath(file);
    inputCfgSetMods(NULL, NULL);
    CHECK(strcmp(csTabName(CS_TAB_MODS), "Mods") == 0);

    /* no mods: one empty-state line */
    modsInit(NULL, NULL);
    modsRebuild();
    CHECK(csTabRowCount(CS_TAB_MODS) == 1 && optionsModsListed() == 0 && !csTabPlaceholder(CS_TAB_MODS));
    csRowLabel(CS_TAB_MODS, 0, buf, sizeof buf);
    CHECK(strcmp(buf, "No mods installed") == 0 && strlen(md(0)) > 0 && mv(0)[0] == '\0');
    CHECK(optionsModsPending() == 0);

    /* three good mods, a broken one, one that needs a mod that is not there */
    putMod(root, "a", "{\"format\":\"mod-v1\",\"name\":\"A\",\"version\":\"1.2\"}");
    putMod(root, "b", "{\"format\":\"mod-v1\",\"name\":\"B\",\"kind\":\"content\"}");
    putMod(root, "c", "{\"format\":\"mod-v1\",\"name\":\"C\",\"version\":\"3\"}");
    putMod(root, "bad", "{\"format\":\"mod-v1\",\"name\":");
    putMod(root, "dep", "{\"format\":\"mod-v1\",\"name\":\"D\",\"requires\":[\"zz\"]}");
    modsBoot(root);
    modsRebuild();
    CHECK(csTabRowCount(CS_TAB_MODS) == 5 && optionsModsListed() == 5);
    CHECK(csRowKind(CS_TAB_MODS, 0) == CS_ROW_ACTION);
    for (i = 0; i < 3; i++) {
        csRowLabel(CS_TAB_MODS, i, buf, sizeof buf);
        CHECK(buf[0] == (char)('a' + i) && buf[1] == '\0');
        CHECK(strncmp(mv(i), "On ", 3) == 0 && mv(i)[3] == (char)('1' + i) && mv(i)[4] == '\0');
    }
    CHECK(strncmp(md(0), "v1.2 assets.", 12) == 0 && strncmp(md(1), "v0 content.", 11) == 0);
    csRowLabel(CS_TAB_MODS, 3, buf, sizeof buf);
    CHECK(strcmp(buf, "dep") == 0 && strcmp(mv(3), "Blocked") == 0 && strncmp(md(3), "Blocked: requires zz", 20) == 0);
    csRowLabel(CS_TAB_MODS, 4, buf, sizeof buf);
    CHECK(strcmp(buf, "bad") == 0 && strcmp(mv(4), "Invalid") == 0 && strncmp(md(4), "Invalid: ", 9) == 0 && strlen(md(4)) <= 40);
    CHECK(optionsModsPending() == 0);

    /* the tab on screen: Confirm on a blocked row changes nothing, nothing is written */
    csOpen();
    csSetTab(CS_TAB_MODS);
    csSetCursor(3);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && optionsModsPending() == 0);
    CHECK(csAct(CS_ACT_RIGHT) == 1 && csAct(CS_ACT_LEFT) == 1 && optionsModsPending() == 0 && csCursor() == 3);
    csSetCursor(1);

    /* off: b leaves the load order, "disabled = b" is the only line, and "Restart" shows */
    CHECK(csAct(CS_ACT_CONFIRM) == 1);
    CHECK(strcmp(mv(1), "Off Restart") == 0 && optionsModsPending() == 1 && strstr(md(1), "Restart to apply") != NULL);
    CHECK(strcmp(mv(2), "On 2") == 0); /* c is second among the ones on now, and a mod above leaving is not a move of its own */
    readBack(file);
    order = inputCfgModsOrderText();
    dis = inputCfgModsDisabledText();
    CHECK(order[0] == '\0' && strcmp(dis, "b") == 0);
    modsBoot(root); /* the next start: b is not loaded, a and c are, in alphabetical order */
    CHECK(modsCount() == 2 && strcmp(modsNameOf(0), "a") == 0 && strcmp(modsNameOf(1), "c") == 0);
    for (i = 0; i < modsReportCount(); i++) {
        if (strcmp(modsReportAt(i)->name, "b") == 0) {
            CHECK(modsReportAt(i)->status == MODS_REP_DISABLED && modsReportAt(i)->byUser == 1);
        }
    }
    modsInit(root, NULL);
    modsRebuild(); /* this run still started with all on */
    csSetTab(CS_TAB_MODS);

    /* on again: back to what the run started with, nothing pending, the file has no [mods] lines left */
    csSetCursor(1);
    csAct(CS_ACT_CONFIRM);
    csAct(CS_ACT_CONFIRM);
    CHECK(strcmp(mv(1), "On 2") == 0 && optionsModsPending() == 0);
    readBack(file);
    CHECK(inputCfgModsOrderText()[0] == '\0' && inputCfgModsDisabledText()[0] == '\0');

    /* order: Right moves the mod later and the cursor follows; the file then carries the whole order */
    csSetCursor(0);
    CHECK(csAct(CS_ACT_RIGHT) == 1 && csCursor() == 1);
    csRowLabel(CS_TAB_MODS, 0, buf, sizeof buf);
    CHECK(buf[0] == 'b');
    csRowLabel(CS_TAB_MODS, 1, buf, sizeof buf);
    CHECK(buf[0] == 'a' && strcmp(mv(1), "On 2 Restart") == 0 && strcmp(mv(0), "On 1 Restart") == 0 && optionsModsPending() == 1);
    readBack(file);
    CHECK(strcmp(inputCfgModsOrderText(), "b, a, c") == 0 && inputCfgModsDisabledText()[0] == '\0');
    modsBoot(root);
    CHECK(modsCount() == 3 && strcmp(modsNameOf(0), "b") == 0 && strcmp(modsNameOf(1), "a") == 0 && strcmp(modsNameOf(2), "c") == 0);
    modsInit(root, NULL);
    modsRebuild();
    csSetTab(CS_TAB_MODS);
    csSetCursor(0);
    CHECK(csAct(CS_ACT_LEFT) == 1 && csCursor() == 0 && optionsModsPending() == 0); /* the first cannot move earlier */
    csSetCursor(2);
    csAct(CS_ACT_RIGHT); /* c is the last mod that can move; dep and bad stay below */
    csRowLabel(CS_TAB_MODS, 2, buf, sizeof buf);
    CHECK(buf[0] == 'c' && optionsModsPending() == 0 && csCursor() == 2);
    csRowLabel(CS_TAB_MODS, 3, buf, sizeof buf);
    CHECK(strcmp(buf, "dep") == 0);

    /* off while an order exists: the order keeps the others in place, the mod moves to "disabled" */
    csSetCursor(0);
    csAct(CS_ACT_RIGHT); /* the file holds b, a, c and the list started a, b, c: now b, a, c */
    csSetCursor(0);
    csAct(CS_ACT_CONFIRM); /* b off */
    readBack(file);
    CHECK(strcmp(inputCfgModsOrderText(), "a, c") == 0 && strcmp(inputCfgModsDisabledText(), "b") == 0);
    modsBoot(root);
    CHECK(modsCount() == 2 && strcmp(modsNameOf(0), "a") == 0 && strcmp(modsNameOf(1), "c") == 0);
    csClose();

    /* the file by hand: a mod in the order and in disabled is off; unlisted mods are not turned off by disabled */
    {
        FILE *f = fopen(file, "wb");
        InputCfg c;

        CHECK(f != NULL);
        fputs("[mods]\norder = a, b, c\ndisabled = a\n", f);
        fclose(f);
        inputCfgSetMods(NULL, NULL);
        CHECK(inputCfgLoadFile(file, &c, NULL) >= 0);
        CHECK(strcmp(inputCfgModsOrderText(), "a, b, c") == 0 && strcmp(inputCfgModsDisabledText(), "a") == 0);
        modsBoot(root);
        CHECK(modsCount() == 2 && strcmp(modsNameOf(0), "b") == 0 && strcmp(modsNameOf(1), "c") == 0);
        modsRebuild();
        csSetTab(CS_TAB_MODS);
        CHECK(strcmp(mv(0), "On 1") == 0 && strcmp(mv(1), "On 2") == 0);
        csRowLabel(CS_TAB_MODS, 2, buf, sizeof buf);
        CHECK(strcmp(buf, "a") == 0 && strcmp(mv(2), "Off") == 0 && optionsModsPending() == 0);
        f = fopen(file, "wb");
        fputs("[mods]\ndisabled = a, c\n", f);
        fclose(f);
        inputCfgSetMods(NULL, NULL);
        inputCfgLoadFile(file, &c, NULL);
        modsBoot(root);
        CHECK(modsCount() == 1 && strcmp(modsNameOf(0), "b") == 0);
    }

    /* more mods than rows (40): every mod has a row and the tab scrolls a window of CS_MAX_WIDGETS rows */
    {
        char cmd[1200];

        snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s' && mkdir '%s'", root, file, root);
        CHECK(system(cmd) == 0);
    }
    for (i = 0; i < 40; i++) {
        char n[8];

        snprintf(n, sizeof n, "m%02d", i);
        putMod(root, n, "{\"format\":\"mod-v1\"}");
    }
    inputCfgSetMods(NULL, NULL);
    modsInit(root, NULL);
    modsRebuild();
    CHECK(csTabRowCount(CS_TAB_MODS) == 40 && optionsModsListed() == 40);
    csRowLabel(CS_TAB_MODS, 39, buf, sizeof buf);
    CHECK(strcmp(buf, "m39") == 0);
    CHECK(csTabWindowTop(CS_TAB_SYSTEM) == 0 && csTabWindowTop(CS_TAB_CONTROLS) == 0); /* only Mods scrolls */
    csOpen();
    csSetTab(CS_TAB_MODS);
    CHECK(csCursor() == 0 && csTabWindowTop(CS_TAB_MODS) == 0);
    for (i = 0; i < 13; i++) {
        csAct(CS_ACT_DOWN);
    }
    CHECK(csCursor() == 13 && csTabWindowTop(CS_TAB_MODS) == 0);
    csAct(CS_ACT_DOWN);
    CHECK(csCursor() == 14 && csTabWindowTop(CS_TAB_MODS) == 1);
    csAct(CS_ACT_UP);
    CHECK(csCursor() == 13 && csTabWindowTop(CS_TAB_MODS) == 1); /* inside the window: it stays */
    csSetCursor(0);
    csAct(CS_ACT_UP); /* wraps to the last mod */
    CHECK(csCursor() == 39 && csTabWindowTop(CS_TAB_MODS) == 40 - CS_MAX_WIDGETS);
    csAct(CS_ACT_DOWN); /* and back to the first */
    CHECK(csCursor() == 0 && csTabWindowTop(CS_TAB_MODS) == 0);
    /* a row that was off screen: turn it off, the file says so */
    csSetCursor(20);
    CHECK(csTabWindowTop(CS_TAB_MODS) == 7);
    CHECK(strcmp(mv(20), "On 21") == 0);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && strcmp(mv(20), "Off Restart") == 0);
    readBack(file);
    CHECK(strcmp(inputCfgModsDisabledText(), "m20") == 0);
    /* reorder past the bottom edge of the window: the cursor follows the mod and the window follows the cursor */
    csSetCursor(0);
    CHECK(csTabWindowTop(CS_TAB_MODS) == 0);
    csSetCursor(13);
    CHECK(csTabWindowTop(CS_TAB_MODS) == 0);
    csRowLabel(CS_TAB_MODS, 13, buf, sizeof buf);
    CHECK(strcmp(buf, "m13") == 0 && csAct(CS_ACT_RIGHT) == 1);
    CHECK(csCursor() == 14 && csTabWindowTop(CS_TAB_MODS) == 1);
    csRowLabel(CS_TAB_MODS, 14, buf, sizeof buf);
    CHECK(strcmp(buf, "m13") == 0);
    /* and past the top edge */
    csSetCursor(30);
    csSetCursor(17);
    CHECK(csTabWindowTop(CS_TAB_MODS) == 17);
    csRowLabel(CS_TAB_MODS, 17, buf, sizeof buf);
    CHECK(csAct(CS_ACT_LEFT) == 1 && csCursor() == 16 && csTabWindowTop(CS_TAB_MODS) == 16);
    csRowLabel(CS_TAB_MODS, 16, buf, sizeof buf);
    CHECK(strcmp(buf, "m17") == 0);
    csClose();

    inputCfgSetMods(NULL, NULL);
    modsInit(NULL, NULL);
    csWidgetsClear();
    inputCfgSetPath("");
}

static void testOptionsLanguage(void) {
    InputCfg d, got;
    char text[4096], val[32];
    int c;

    /* the choice <-> the game's LANGUAGE (country_select.c: 2 EN, 3 FR, 5 DE, 4 IT, 6 ES) */
    CHECK(optLangGameValue(OPT_LANG_EN) == 2 && optLangGameValue(OPT_LANG_FR) == 3 && optLangGameValue(OPT_LANG_DE) == 5);
    CHECK(optLangGameValue(OPT_LANG_ES) == 6 && optLangGameValue(OPT_LANG_IT) == 4);
    CHECK(optLangGameValue(OPT_LANG_NONE) == -1 && optLangGameValue(99) == -1 && optLangGameValue(-1) == -1);
    for (c = OPT_LANG_EN; c < OPT_LANG_ES419; c++) { /* es-419 shares LANGUAGE 6 with Espanol */
        CHECK(optLangFromGameValue(optLangGameValue(c)) == c);
    }
    CHECK(optLangFromGameValue(0) == OPT_LANG_NONE && optLangFromGameValue(1) == OPT_LANG_NONE && optLangFromGameValue(7) == OPT_LANG_NONE);
    /* O3c: outside the harness the boot menu is always skipped; scripted runs keep it */
    CHECK(optLangSkipsMenu(OPT_LANG_ES, 0) == 1 && optLangSkipsMenu(OPT_LANG_EN, 0) == 1 && optLangSkipsMenu(OPT_LANG_IT, 0) == 1);
    CHECK(optLangSkipsMenu(OPT_LANG_ES, 1) == 0 && optLangSkipsMenu(OPT_LANG_EN, 1) == 0);
    CHECK(optLangSkipsMenu(OPT_LANG_NONE, 0) == 0);
    CHECK(optLangOpening(0) == 1 && optLangOpening(2) == 2 && optLangOpening(6) == 2);

    /* the first run: the system's first preferred language the game has, else English */
    {
        const char *es[] = {"es", "en"}, *xx[] = {"ja", "fr", "en"}, *none[] = {"ja", "zh"}, *up[] = {"DE_de"};
        CHECK(optLangFromLocale("es") == OPT_LANG_ES && optLangFromLocale("en") == OPT_LANG_EN);
        CHECK(optLangFromLocale("fr") == OPT_LANG_FR && optLangFromLocale("de") == OPT_LANG_DE && optLangFromLocale("it") == OPT_LANG_IT);
        CHECK(optLangFromLocale("es_ES.UTF-8") == OPT_LANG_ES && optLangFromLocale("pt") == OPT_LANG_NONE);
        /* L5: a Latin American Spanish locale picks es-419 (the row falls back to Espanol when it is not offered) */
        CHECK(optLangFromLocale("es_MX.UTF-8") == OPT_LANG_ES419 && optLangFromLocale("es_419") == OPT_LANG_ES419);
        CHECK(optLangFromLocale("es-419") == OPT_LANG_ES419 && optLangFromLocale("ES-ar") == OPT_LANG_ES419);
        CHECK(optLangFromLocale("es_US") == OPT_LANG_ES419 && optLangFromLocale("es_PR@euro") == OPT_LANG_ES419);
        CHECK(optLangFromLocale("es_ES") == OPT_LANG_ES && optLangFromLocale("es") == OPT_LANG_ES && optLangFromLocale("es_XX") == OPT_LANG_ES);
        CHECK(optLangFromLocale("en_US") == OPT_LANG_EN && optLangFromLocale("pt_MX") == OPT_LANG_NONE);
        CHECK(optLangFromLocale("") == OPT_LANG_NONE && optLangFromLocale(NULL) == OPT_LANG_NONE && optLangFromLocale("e") == OPT_LANG_NONE);
        CHECK(optLangFromLocale("esp") == OPT_LANG_NONE); /* three letters is another language code */
        CHECK(optLangFirstRun(es, 2) == OPT_LANG_ES && optLangFirstRun(xx, 3) == OPT_LANG_FR);
        CHECK(optLangFirstRun(none, 2) == OPT_LANG_EN && optLangFirstRun(NULL, 0) == OPT_LANG_EN);
        CHECK(optLangFirstRun(up, 1) == OPT_LANG_DE);
        {
            const char *mx[] = {"es_MX", "en_US"}, *spain[] = {"es_ES", "en"}, *us[] = {"en_US", "es_MX"};
            CHECK(optLangFirstRun(mx, 2) == OPT_LANG_ES419 && optLangFirstRun(spain, 2) == OPT_LANG_ES && optLangFirstRun(us, 2) == OPT_LANG_EN);
        }
    }

    /* the row: no Ask; the first run takes the locale's language and writes it */
    csSettingsInstall();
    csWidgetsClear();
    optionsLanguageInit(OPT_LANG_ES, 1);
    CHECK(csTabRowCount(CS_TAB_LANGUAGE) == 1 && optLangChoice() == OPT_LANG_ES);
    csRowValue(CS_TAB_LANGUAGE, 0, 0, val, sizeof(val));
    CHECK(strcmp(val, "Espanol") == 0);
    inputCfgDefaults(&d);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0);
    CHECK(strstr(text, "[language]\n") != NULL && strstr(text, "game = espanol") != NULL && strstr(text, "ask") == NULL);
    csWidgetSet(CS_TAB_LANGUAGE, 0, OPT_LANG_DE - 1); /* the row's values start at English */
    CHECK(optLangChoice() == OPT_LANG_DE);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "game = deutsch") != NULL);
    CHECK(optLangPending() == 1); /* a player's change waits for its switch */
    optLangClearPending();
    CHECK(optLangPending() == 0);
    csWidgetsClear();
    got = d;
    CHECK(inputCfgParse("[language]\ngame = Italiano\n", &got, logSink) == 0);
    optionsLanguageInit(OPT_LANG_ES, 1); /* the file wins over the first-run default */
    CHECK(optLangChoice() == OPT_LANG_IT && optLangGameValue(optLangChoice()) == 4);
    CHECK(optLangPending() == 0); /* the file's value at startup is not a change to switch to */
    /* live switching (O3b): modes 0x2xx field, 0xE00 title, 0x1600 country select */
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0x214, 1, 0) == OPT_LANG_LIVE_FIELD);
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0x214, 0, 0) == OPT_LANG_LIVE_WAIT); /* a talk, an event or a menu holds the field */
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0x214, 1, 1) == OPT_LANG_LIVE_WAIT); /* the options menu is still open */
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0x305, 1, 0) == OPT_LANG_LIVE_FIELD); /* a stage a mod adds is a field too */
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0xE00, 1, 0) == OPT_LANG_LIVE_TITLE);
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0x600, 1, 0) == OPT_LANG_LIVE_WAIT); /* battle: its text is loaded per fight */
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0x1000, 1, 0) == OPT_LANG_LIVE_WAIT);
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0xE02, 1, 0) == OPT_LANG_LIVE_WAIT); /* a movie */
    CHECK(optLangLive(1, OPT_LANG_ES, 6, 0, 0x214, 1, 0) == OPT_LANG_LIVE_NONE); /* already there */
    CHECK(optLangLive(1, OPT_LANG_NONE, 6, 0, 0x214, 1, 0) == OPT_LANG_LIVE_NONE);
    CHECK(optLangLive(0, OPT_LANG_EN, 6, 0, 0x214, 1, 0) == OPT_LANG_LIVE_NONE);
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 0, 0x1600, 1, 0) == OPT_LANG_LIVE_NONE); /* the country select's lane owns it */
    CHECK(optLangShiftFile(0xE8 + 6, 6, 2) == 0xE8 + 2 && optLangShiftFile(0xE8 + 2, 2, 5) == 0xE8 + 5);
    CHECK(optLangShiftFile(0, 6, 2) == 0);
        /* es-419 shares LANGUAGE 6 with Espanol: the pair (language, variant) decides */
    CHECK(optLangLive(1, OPT_LANG_ES419, 6, 0, 0x214, 1, 0) == OPT_LANG_LIVE_FIELD); /* ES -> es-419 */
    CHECK(optLangLive(1, OPT_LANG_ES, 6, 1, 0x214, 1, 0) == OPT_LANG_LIVE_FIELD);    /* es-419 -> ES */
    CHECK(optLangLive(1, OPT_LANG_ES419, 6, 1, 0x214, 1, 0) == OPT_LANG_LIVE_NONE);  /* already there */
    CHECK(optLangLive(1, OPT_LANG_ES, 6, 0, 0x214, 1, 0) == OPT_LANG_LIVE_NONE);
    CHECK(optLangLive(1, OPT_LANG_EN, 6, 1, 0x214, 1, 0) == OPT_LANG_LIVE_FIELD);   /* es-419 -> English */
    CHECK(optLangLive(1, OPT_LANG_ES419, 2, 0, 0x214, 1, 0) == OPT_LANG_LIVE_FIELD); /* English -> es-419 */
    CHECK(optLangLive(1, OPT_LANG_ES419, 6, 0, 0xE00, 1, 0) == OPT_LANG_LIVE_TITLE);
    CHECK(optLangLive(1, OPT_LANG_ES419, 6, 0, 0x214, 0, 0) == OPT_LANG_LIVE_WAIT);
    CHECK(optLangLive(1, OPT_LANG_ES419, 6, 0, 0x214, 1, 1) == OPT_LANG_LIVE_WAIT);
    CHECK(optLangLive(1, OPT_LANG_ES419, 6, 0, 0x1600, 1, 0) == OPT_LANG_LIVE_NONE);
    /* the choice <-> (language, variant), and the offer */
    CHECK(optLangGameValue(OPT_LANG_ES419) == 6 && optLangVariant(OPT_LANG_ES419) == 1);
    CHECK(optLangVariant(OPT_LANG_ES) == 0 && optLangVariant(OPT_LANG_EN) == 0 && optLangVariant(OPT_LANG_NONE) == 0);
    CHECK(optLangFromGameValue(6) == OPT_LANG_ES); /* a bare LANGUAGE 6 is the retail Spanish */
    CHECK(optLangFromLocale("es") == OPT_LANG_ES);  /* a bare es is the retail Spanish */
    CHECK(optLangOffered(1) == 1 && optLangOffered(0) == 0); /* offered whenever the es-419 data can be built */
    CHECK(optLangEffective(OPT_LANG_ES419, 1) == OPT_LANG_ES419 && optLangEffective(OPT_LANG_ES419, 0) == OPT_LANG_ES);
    CHECK(optLangEffective(OPT_LANG_IT, 0) == OPT_LANG_IT && optLangEffective(OPT_LANG_NONE, 0) == OPT_LANG_NONE);
    CHECK(optLangSkipsMenu(OPT_LANG_ES419, 0) == 1 && optLangSkipsMenu(OPT_LANG_ES419, 1) == 0);
    /* the boot lane: warp runs (a mode change already pending on the first visit) take the language too */
    CHECK(optLangLane(1, 0, 1, 0, OPT_LANG_ES, 0) == OPT_LANG_LANE_SET_SKIP); /* plain boot: set and skip the menu */
    CHECK(optLangLane(1, 0, 1, 0, OPT_LANG_ES, 1) == OPT_LANG_LANE_SET);      /* scripted keys: set, keep the menu */
    CHECK(optLangLane(1, 1, 1, 0, OPT_LANG_ES, 1) == OPT_LANG_LANE_SET);      /* warp: set, the warp's mode stands */
    CHECK(optLangLane(1, 1, 1, 1, OPT_LANG_ES, 0) == OPT_LANG_LANE_NONE);     /* the title's NEW GAME later: leave it */
    CHECK(optLangLane(1, 0, 0, 1, OPT_LANG_ES, 0) == OPT_LANG_LANE_NONE);     /* the menu's own pick must stand */
    CHECK(optLangLane(1, 0, 1, 1, OPT_LANG_EN, 0) == OPT_LANG_LANE_SET_SKIP); /* a later visit (soft reset) */
    CHECK(optLangLane(0, 0, 1, 0, OPT_LANG_ES, 1) == OPT_LANG_LANE_SET);      /* first frame already past the menu */
    CHECK(optLangLane(0, 0, 1, 1, OPT_LANG_ES, 1) == OPT_LANG_LANE_NONE);
    CHECK(optLangLane(1, 0, 1, 0, OPT_LANG_NONE, 0) == OPT_LANG_LANE_NONE);
/* an obsolete "ask", or a value not offered (es-419 is deferred): the first-run default, and the file is rewritten */
    csWidgetsClear();
    got = d;
    CHECK(inputCfgParse("[language]\ngame = ask\n", &got, logSink) == 0);
    optionsLanguageInit(OPT_LANG_FR, 1);
    CHECK(optLangChoice() == OPT_LANG_FR && optLangPending() == 0);
    csWidgetsClear();
    CHECK(inputCfgParse("[language]\ngame = es-419\n", &got, logSink) == 0);
    optionsLanguageInit(OPT_LANG_EN, 1);
    CHECK(optLangChoice() == OPT_LANG_EN);
    csWidgetsClear();

    /* L5: the sixth choice is offered whenever the es-419 data can be built, no preview key */
    optionsLanguageInit(OPT_LANG_EN, 0); /* data cannot be built: five rows, no es419_preview key */
    CHECK(csTabRowCount(CS_TAB_LANGUAGE) == 1 && optLangEs419Offered() == 0);
    csWidgetSet(CS_TAB_LANGUAGE, 0, 4);
    csRowValue(CS_TAB_LANGUAGE, 0, 0, val, sizeof(val));
    CHECK(strcmp(val, "Italiano") == 0 && optLangChoice() == OPT_LANG_IT);
    csWidgetSet(CS_TAB_LANGUAGE, 0, 5); /* wraps: only the five retail languages cycle */
    CHECK(optLangChoice() == OPT_LANG_EN);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "es419_preview") == NULL);
    CHECK(strstr(text, "game = english") != NULL);
    csWidgetsClear();
    got = d;
    CHECK(inputCfgParse("[language]\ngame = es419\n", &got, logSink) == 0); /* unavailable: back to Espanol */
    optionsLanguageInit(OPT_LANG_EN, 0);
    CHECK(optLangChoice() == OPT_LANG_ES && optLangPending() == 0 && optLangEs419Offered() == 0);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "game = espanol") != NULL);
    csWidgetsClear();
    /* an old es419_preview line (on or off) is accepted, logs nothing, and is not written back */
    for (c = 0; c < 2; c++) {
        csWidgetsClear();
        got = d;
        CHECK(inputCfgParse(c ? "[language]\nes419_preview = off\ngame = es419\n" : "[language]\nes419_preview = on\ngame = es419\n", &got, logSink) == 0);
        logCount = 0;
        optionsLanguageInit(OPT_LANG_EN, 1); /* available: the sixth row, token es419, whatever the old key said */
        csSettingsReportUnknown(logSink);
        CHECK(logCount == 0);
        CHECK(optLangEs419Offered() == 1 && csTabRowCount(CS_TAB_LANGUAGE) == 1);
        CHECK(optLangChoice() == OPT_LANG_ES419 && optLangPending() == 0);
        csRowValue(CS_TAB_LANGUAGE, 0, 0, val, sizeof(val));
        CHECK(strcmp(val, "Espanol Latam") == 0);
        CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "game = es419") != NULL && strstr(text, "es419_preview") == NULL);
    }
    csWidgetsClear();
    got = d; /* no preview key at all: es419 is offered when available */
    CHECK(inputCfgParse("[language]\ngame = es419\n", &got, logSink) == 0);
    optionsLanguageInit(OPT_LANG_EN, 1);
    CHECK(optLangEs419Offered() == 1 && optLangChoice() == OPT_LANG_ES419);
    /* first run: a Latin American locale picks es-419 when available, else Espanol */
    csWidgetsClear();
    optionsLanguageInit(OPT_LANG_ES419, 1);
    CHECK(optLangChoice() == OPT_LANG_ES419);
    csWidgetsClear();
    optionsLanguageInit(OPT_LANG_ES419, 0);
    CHECK(optLangChoice() == OPT_LANG_ES && optLangEs419Offered() == 0);
    csWidgetsClear();
    optionsLanguageInit(OPT_LANG_ES, 1);
    CHECK(optLangChoice() == OPT_LANG_ES);
    csWidgetsClear();
    optionsLanguageInit(OPT_LANG_EN, 1);
    CHECK(optLangChoice() == OPT_LANG_EN);
    csWidgetsClear();
    optionsLanguageInit(OPT_LANG_ES419, 1);
    csWidgetSet(CS_TAB_LANGUAGE, 0, 3); /* Espanol: the variant goes with the choice */
    CHECK(optLangChoice() == OPT_LANG_ES && optLangPending() == 1);
    csWidgetSet(CS_TAB_LANGUAGE, 0, 5);
    CHECK(optLangChoice() == OPT_LANG_ES419);
    csWidgetSet(CS_TAB_LANGUAGE, 0, 6); /* six values wrap to English */
    CHECK(optLangChoice() == OPT_LANG_EN);
    optLangClearPending();
    csWidgetsClear();

    /* L4b: a hidden cycle ([display] talk_box_width): no row, kept in the file, read by key */
    {
        static const char *const widths[] = {"original", "160", "180", "200", "220", "250"};
        CsWidget tb = {.kind = CS_ROW_CYCLE, .key = "talk_box_width", .hidden = 1, .label = "Talk box width", .names = widths, .count = 6};

        got = d;
        CHECK(inputCfgParse("[display]\ntalk_box_width = 220\n", &got, logSink) == 0);
        CHECK(csWidgetAdd(CS_TAB_DISPLAY, &tb) == 0);
        CHECK(csTabRowCount(CS_TAB_DISPLAY) == 0 && csWidgetValueByKey(CS_TAB_DISPLAY, "talk_box_width") == 4);
        CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "talk_box_width = 220") != NULL);
        csWidgetsClear();
        CHECK(csWidgetAdd(CS_TAB_DISPLAY, &tb) == 0); /* the file lacks the key: the default is written */
        CHECK(csWidgetValueByKey(CS_TAB_DISPLAY, "talk_box_width") == 0);
        CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "talk_box_width = original") != NULL);
    }
    csWidgetsClear();
}

static unsigned short atlasWordRaw(int cell, int x, int y) {
    int u, v;

    return promptIconUV(cell, &u, &v) ? PROMPT_ATLAS[(v + y) * PI_ATLAS_W + u + x] : 0;
}

static void testPromptIcons(void) {
    static char text[16384];
    SDL_Event ev;
    InputCfg got, d;
    const char *t;
    int f, u, v;

    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_XBOX360) == INPUT_FAMILY_XBOX);
    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_XBOXONE) == INPUT_FAMILY_XBOX);
    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_PS3) == INPUT_FAMILY_PLAYSTATION);
    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_PS4) == INPUT_FAMILY_PLAYSTATION);
    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_PS5) == INPUT_FAMILY_PLAYSTATION);
    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO) == INPUT_FAMILY_SWITCH);
#if SDL_VERSION_ATLEAST(2, 0, 16)
    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR) == INPUT_FAMILY_SWITCH);
#endif
    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_UNKNOWN) == INPUT_FAMILY_GENERIC);
    CHECK(inputFamilyFromType(SDL_CONTROLLER_TYPE_VIRTUAL) == INPUT_FAMILY_GENERIC);

    /* Auto follows the detection, a forced setting wins over it */
    for (f = 0; f < INPUT_FAMILY_COUNT; f++) {
        CHECK(inputIconFamilyFor(INPUT_ICONS_AUTO, f) == f);
    }
    CHECK(inputIconFamilyFor(INPUT_ICONS_XBOX, INPUT_FAMILY_PLAYSTATION) == INPUT_FAMILY_XBOX);
    CHECK(inputIconFamilyFor(INPUT_ICONS_PLAYSTATION, INPUT_FAMILY_SWITCH) == INPUT_FAMILY_PLAYSTATION);
    CHECK(inputIconFamilyFor(INPUT_ICONS_SWITCH, INPUT_FAMILY_GENERIC) == INPUT_FAMILY_SWITCH);
    CHECK(inputIconFamilyFor(INPUT_ICONS_AUTO, 99) == INPUT_FAMILY_GENERIC);

    /* the last device used, from events */
    inputDeviceSet(INPUT_DEV_KEYBOARD, INPUT_FAMILY_GENERIC);
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_CONTROLLERBUTTONDOWN; /* no such controller is open: family stays generic */
    ev.cbutton.which = 7;
    CHECK(inputDeviceNote((const union SDL_Event *)&ev) == INPUT_DEV_PAD && inputDeviceCurrent() == INPUT_DEV_PAD);
    ev.type = SDL_KEYDOWN;
    CHECK(inputDeviceNote((const union SDL_Event *)&ev) == INPUT_DEV_KEYBOARD && inputDeviceCurrent() == INPUT_DEV_KEYBOARD);
    ev.type = SDL_CONTROLLERAXISMOTION;
    ev.caxis.axis = SDL_CONTROLLER_AXIS_LEFTX;
    ev.caxis.value = 3000; /* a resting stick is not a press */
    CHECK(inputDeviceNote((const union SDL_Event *)&ev) == -1 && inputDeviceCurrent() == INPUT_DEV_KEYBOARD);
    ev.caxis.value = 30000;
    CHECK(inputDeviceNote((const union SDL_Event *)&ev) == INPUT_DEV_PAD);
    ev.type = SDL_MOUSEMOTION;
    CHECK(inputDeviceNote((const union SDL_Event *)&ev) == -1 && inputDeviceCurrent() == INPUT_DEV_PAD);
    inputDeviceSet(INPUT_DEV_PAD, INPUT_FAMILY_PLAYSTATION);
    inputIconSettingSet(INPUT_ICONS_AUTO);
    CHECK(inputIconFamily() == INPUT_FAMILY_PLAYSTATION);
    inputIconSettingSet(INPUT_ICONS_XBOX);
    CHECK(inputIconFamily() == INPUT_FAMILY_XBOX && inputDeviceFamily() == INPUT_FAMILY_PLAYSTATION);
    inputIconSettingSet(99);
    CHECK(inputIconSetting() == INPUT_ICONS_AUTO);

    /* button -> cell per family */
    CHECK(promptIconPad(INPUT_FAMILY_XBOX, SDL_CONTROLLER_BUTTON_A) == PI_XB_A);
    CHECK(promptIconPad(INPUT_FAMILY_XBOX, SDL_CONTROLLER_BUTTON_Y) == PI_XB_Y);
    CHECK(promptIconPad(INPUT_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_A) == PI_PS_CROSS);
    CHECK(promptIconPad(INPUT_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_B) == PI_PS_CIRCLE);
    CHECK(promptIconPad(INPUT_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_X) == PI_PS_SQUARE);
    CHECK(promptIconPad(INPUT_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_Y) == PI_PS_TRIANGLE);
    CHECK(promptIconPad(INPUT_FAMILY_SWITCH, SDL_CONTROLLER_BUTTON_X) == PI_SW_X);
    CHECK(promptIconPad(INPUT_FAMILY_GENERIC, SDL_CONTROLLER_BUTTON_B) == PI_SW_B);
    CHECK(promptIconPad(INPUT_FAMILY_XBOX, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == PI_XB_LB);
    CHECK(promptIconPad(INPUT_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == PI_PS_R1);
    CHECK(promptIconPad(INPUT_FAMILY_SWITCH, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == PI_SW_L);
    CHECK(promptIconPad(INPUT_FAMILY_XBOX, SDL_CONTROLLER_BUTTON_START) == PI_XB_MENU);
    CHECK(promptIconPad(INPUT_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_START) == PI_PS_OPTIONS);
    CHECK(promptIconPad(INPUT_FAMILY_SWITCH, SDL_CONTROLLER_BUTTON_START) == PI_SW_PLUS);
    CHECK(promptIconPad(INPUT_FAMILY_GENERIC, SDL_CONTROLLER_BUTTON_START) == PI_NONE); /* text fallback */
    CHECK(promptIconPad(INPUT_FAMILY_XBOX, SDL_CONTROLLER_BUTTON_BACK) == PI_XB_VIEW);
    CHECK(promptIconPad(INPUT_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_BACK) == PI_PS_SELECT);
    CHECK(promptIconPad(INPUT_FAMILY_GENERIC, SDL_CONTROLLER_BUTTON_BACK) == PI_NONE);
    CHECK(promptIconPad(INPUT_FAMILY_SWITCH, SDL_CONTROLLER_BUTTON_BACK) == PI_SW_MINUS);
    CHECK(promptIconPad(INPUT_FAMILY_XBOX, SDL_CONTROLLER_BUTTON_DPAD_LEFT) == PI_DPAD_LEFT);
    CHECK(promptIconPad(INPUT_FAMILY_XBOX, SDL_CONTROLLER_BUTTON_LEFTSTICK) == PI_NONE);
    CHECK(promptIconPad(INPUT_FAMILY_XBOX, SDL_CONTROLLER_BUTTON_INVALID) == PI_NONE);
    /* every button of the exposed Controller rows has an icon on every family but start/back on generic */
    for (f = INPUT_FAMILY_XBOX; f < INPUT_FAMILY_COUNT; f++) {
        int n;

        InputCfg cfg;
        inputCfgDefaults(&cfg);
        for (n = 0; n < INPUT_EXPOSED_BUTTONS; n++) {
            int code = *inputCfgSlot(&cfg, INPUT_DEV_PAD, INPUT_EXPOSED[n]);

            CHECK(code >= 0 && promptIconPad(f, code) != PI_NONE);
        }
    }
    CHECK(promptIconKey(SDL_SCANCODE_ESCAPE) == PI_KEY_ESC && promptIconKey(SDL_SCANCODE_Q) == PI_KEY_Q);
    CHECK(promptIconKey(SDL_SCANCODE_RIGHT) == PI_KEY_RIGHT && promptIconKey(SDL_SCANCODE_A) == PI_NONE);
    CHECK(promptIconAction(1, INPUT_FAMILY_XBOX, PI_ACT_BACK, &t) == PI_KEY_ESC && strcmp(t, "Esc") == 0);
    CHECK(promptIconAction(1, INPUT_FAMILY_XBOX, PI_ACT_OK, &t) == PI_NONE && strcmp(t, "Enter") == 0);
    CHECK(promptIconAction(0, INPUT_FAMILY_PLAYSTATION, PI_ACT_OK, &t) == PI_PS_CROSS && strcmp(t, "A") == 0);
    CHECK(promptIconAction(0, INPUT_FAMILY_SWITCH, PI_ACT_TAB_NEXT, &t) == PI_SW_R);
    CHECK(promptIconAction(0, INPUT_FAMILY_GENERIC, PI_ACT_CLOSE, &t) == PI_NONE && strcmp(t, "Select") == 0);
    CHECK(promptIconAction(0, INPUT_FAMILY_XBOX, PI_ACT_CLOSE, &t) == PI_XB_VIEW && promptIconAction(0, INPUT_FAMILY_PLAYSTATION, PI_ACT_CLOSE, &t) == PI_PS_SELECT);
    CHECK(promptIconAction(0, INPUT_FAMILY_SWITCH, PI_ACT_CLOSE, &t) == PI_SW_MINUS);

    /* the atlas: cells all inside it, the generator wrote exactly the enum's cells, art is not blank */
    CHECK(PROMPT_ATLAS_CELLS == PI_CELL_COUNT && PI_CELL_COUNT <= PI_COLS * PI_ROWS);
    CHECK(promptIconUV(PI_KEY_RIGHT, &u, &v) && u + PI_CELL <= PI_ATLAS_W && v + PI_CELL <= PI_ATLAS_H);
    CHECK(!promptIconUV(PI_CELL_COUNT, &u, &v) && !promptIconUV(-1, &u, &v));
    for (f = 0; f < PI_CELL_COUNT; f++) {
        int x, y, opaque = 0;

        promptIconUV(f, &u, &v);
        for (y = 0; y < PI_CELL; y++) {
            for (x = 0; x < PI_CELL; x++) {
                opaque += PROMPT_ATLAS[(v + y) * PI_ATLAS_W + u + x] != 0;
            }
        }
        CHECK(opaque > 40);
    }
    /* O5c: the Switch prompts are one set: face buttons, + and - are equal full discs, L/R rounded keys */
    {
        static const int disc[] = {PI_SW_A, PI_SW_B, PI_SW_X, PI_SW_Y, PI_SW_PLUS, PI_SW_MINUS};
        int n, ref = 0, x, y;

        for (n = 0; n < 6; n++) {
            int opaque = 0;

            promptIconUV(disc[n], &u, &v);
            for (y = 0; y < PI_CELL; y++) {
                for (x = 0; x < PI_CELL; x++) {
                    opaque += PROMPT_ATLAS[(v + y) * PI_ATLAS_W + u + x] != 0;
                }
            }
            ref = n == 0 ? opaque : ref;
            CHECK(opaque == ref && opaque > 140);
            CHECK(PROMPT_ATLAS[v * PI_ATLAS_W + u] == 0); /* round: the corner is clear */
        }
        for (n = PI_SW_L; n <= PI_SW_R; n++) {
            int opaque = 0;

            promptIconUV(n, &u, &v);
            for (y = 0; y < PI_CELL; y++) {
                for (x = 0; x < PI_CELL; x++) {
                    opaque += PROMPT_ATLAS[(v + y) * PI_ATLAS_W + u + x] != 0;
                }
            }
            CHECK(opaque > 120);
        }
        /* the four face buttons are four different pictures (the letters differ) */
        for (n = PI_SW_A; n < PI_SW_Y; n++) {
            int same = 1;

            for (y = 0; y < PI_CELL; y++) {
                for (x = 0; x < PI_CELL; x++) {
                    same &= atlasWordRaw(n, x, y) == atlasWordRaw(n + 1, x, y);
                }
            }
            CHECK(!same);
        }
    }

    /* the setting round trip: row -> file text -> row, and a bad value is reported */
    csSettingsInstall();
    csWidgetsClear();
    inputDeviceRowsInit();
    CHECK(csTabRowCount(CS_TAB_DISPLAY) == 1 && csWidgetGet(CS_TAB_DISPLAY, 0) == INPUT_ICONS_AUTO);
    inputCfgDefaults(&d);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "[display]\n") != NULL && strstr(text, "button_icons = auto") != NULL);
    csWidgetSet(CS_TAB_DISPLAY, 0, INPUT_ICONS_PLAYSTATION);
    CHECK(inputIconSetting() == INPUT_ICONS_PLAYSTATION);
    CHECK(inputCfgFormat(&d, text, sizeof(text)) > 0 && strstr(text, "button_icons = playstation") != NULL);
    csWidgetsClear();
    got = d;
    CHECK(inputCfgParse("[display]\nbutton_icons = switch\n", &got, logSink) == 0);
    inputDeviceRowsInit();
    CHECK(inputIconSetting() == INPUT_ICONS_SWITCH && csWidgetGet(CS_TAB_DISPLAY, 0) == INPUT_ICONS_SWITCH);
    csWidgetsClear();
    logCount = 0;
    CHECK(inputCfgParse("[display]\nbutton_icons = nintendo\n", &got, logSink) == 0);
    inputIconSettingSet(INPUT_ICONS_AUTO);
    inputDeviceRowsInit();
    CHECK(inputIconSetting() == INPUT_ICONS_AUTO); /* the bad value is reported on stderr and ignored */
    csWidgetsClear();
    inputIconSettingSet(INPUT_ICONS_AUTO);
    inputDeviceSet(INPUT_DEV_KEYBOARD, INPUT_FAMILY_GENERIC);
}

/* ---- C5: the game's own button icons inside its text ---- */

static unsigned short atlasWord(int cell, int x, int y) {
    int u, v;

    return promptIconUV(cell, &u, &v) ? PROMPT_ATLAS[(v + y) * PI_ATLAS_W + u + x] : 0;
}

/* The 14-pixel rows/columns a 12-pixel icon keeps (text_icons.c drops 3 and 10) */
static const int KEEP12[TI_SIZE] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 11, 12, 13};

/* Number of pixels of icon i that are not what a faithful resample says, or whose index is out of range */
static int iconMismatch(const TextIconSet *set, const int *cells, int i, int exact) {
    int x, y, bad = 0;

    for (y = 0; y < TI_SIZE; y++) {
        for (x = 0; x < TI_SIZE; x++) {
            unsigned char idx = set->px[i][y * TI_SIZE + x];
            unsigned short want = atlasWord(cells[i], KEEP12[x], KEEP12[y]);

            bad += idx > 15 || (idx != 0 && set->clut[idx] == 0) || (idx == 0) != (want == 0);
            bad += exact && set->clut[idx] != want;
        }
    }
    return bad;
}

static void testTextIcons(void) {
    InputCfg cfg, remap;
    TextIconSet set;
    int cells[TI_BUTTONS], own, i, n, big[TI_BUTTONS];
    unsigned char px[TI_SIZE * TI_SIZE];
    unsigned short words[TI_WORDS_PER_ROW * TI_SIZE], seen[4 * 14 * 14];

    /* which glyph is which button: the EU icon table's four CLUT-224 glyphs (docs/design/options-menu.md) */
    CHECK(textIconNumber(0) == 27 && textIconNumber(1) == 47 && textIconNumber(2) == 50 && textIconNumber(3) == 52);
    CHECK(textIconButton(0) == INPUT_BTN_CROSS && textIconButton(1) == INPUT_BTN_CIRCLE);
    CHECK(textIconButton(2) == INPUT_BTN_SQUARE && textIconButton(3) == INPUT_BTN_TRIANGLE);
    CHECK(textIconNumber(-1) == -1 && textIconNumber(TI_BUTTONS) == -1 && textIconButton(TI_BUTTONS) == -1);
    for (i = 0; i < TI_BUTTONS; i++) {
        CHECK(TEXT_ICON_SITES[i].u % 4 == 0 && TEXT_ICON_SITES[i].u + TI_SIZE <= 256);
        CHECK(TEXT_ICON_SITES[i].page >= 0 && TEXT_ICON_SITES[i].page <= 1);
    }

    inputCfgDefaults(&cfg);
    /* the keyboard in Auto: no key cap for C/V/X/Z, so the game's own icons stay */
    own = textIconPlan(INPUT_DEV_KEYBOARD, INPUT_ICONS_AUTO, INPUT_FAMILY_XBOX, &cfg, cells);
    CHECK(own == 1 && cells[0] == PI_PS_CROSS && cells[1] == PI_PS_CIRCLE && cells[2] == PI_PS_SQUARE && cells[3] == PI_PS_TRIANGLE);
    /* a pad of each family follows its own labels (south/east/west/north = cross/circle/square/triangle) */
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_AUTO, INPUT_FAMILY_XBOX, &cfg, cells);
    CHECK(own == 0 && cells[0] == PI_XB_A && cells[1] == PI_XB_B && cells[2] == PI_XB_X && cells[3] == PI_XB_Y);
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_AUTO, INPUT_FAMILY_SWITCH, &cfg, cells);
    CHECK(own == 0 && cells[0] == PI_SW_A && cells[1] == PI_SW_B && cells[2] == PI_SW_X && cells[3] == PI_SW_Y);
    /* PlayStation and a pad nobody can label: the originals are restored */
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_AUTO, INPUT_FAMILY_PLAYSTATION, &cfg, cells);
    CHECK(own == 1 && cells[0] == PI_PS_CROSS && cells[3] == PI_PS_TRIANGLE);
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_AUTO, INPUT_FAMILY_GENERIC, &cfg, cells);
    CHECK(own == 1 && cells[1] == PI_PS_CIRCLE);
    /* a forced family wins over the device, in both directions */
    own = textIconPlan(INPUT_DEV_KEYBOARD, INPUT_ICONS_XBOX, INPUT_FAMILY_GENERIC, &cfg, cells);
    CHECK(own == 0 && cells[0] == PI_XB_A && cells[2] == PI_XB_X);
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_PLAYSTATION, INPUT_FAMILY_XBOX, &cfg, cells);
    CHECK(own == 1 && cells[0] == PI_PS_CROSS);
    own = textIconPlan(INPUT_DEV_KEYBOARD, INPUT_ICONS_SWITCH, INPUT_FAMILY_PLAYSTATION, &cfg, cells);
    CHECK(own == 0 && cells[1] == PI_SW_B);

    /* remap awareness: the icon is the physical binding of the logical button */
    remap = cfg;
    remap.pad.gc_cross = SDL_CONTROLLER_BUTTON_B;
    remap.pad.gc_circle = SDL_CONTROLLER_BUTTON_A;
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_AUTO, INPUT_FAMILY_XBOX, &remap, cells);
    CHECK(own == 0 && cells[0] == PI_XB_B && cells[1] == PI_XB_A && cells[2] == PI_XB_X);
    /* on a PlayStation pad a swapped binding is no longer the game's own picture */
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_AUTO, INPUT_FAMILY_PLAYSTATION, &remap, cells);
    CHECK(own == 0 && cells[0] == PI_PS_CIRCLE && cells[1] == PI_PS_CROSS && cells[2] == PI_PS_SQUARE);
    /* bound to a trigger or to nothing (no icon): what that button is by default */
    remap = cfg;
    remap.pad.gc_triangle = SDL_CONTROLLER_AXIS_TRIGGERRIGHT | CONTROLLER_MAP_FLAG_AXIS;
    remap.pad.gc_square = SDL_CONTROLLER_BUTTON_INVALID;
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_AUTO, INPUT_FAMILY_XBOX, &remap, cells);
    CHECK(own == 0 && cells[3] == PI_XB_Y && cells[2] == PI_XB_X);
    /* a shoulder button shows its own icon on the cross */
    remap = cfg;
    remap.pad.gc_cross = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    own = textIconPlan(INPUT_DEV_PAD, INPUT_ICONS_AUTO, INPUT_FAMILY_XBOX, &remap, cells);
    CHECK(own == 0 && cells[0] == PI_XB_RB);
    /* the keyboard: a key cap where the atlas has one, the game's own icon elsewhere */
    remap = cfg;
    remap.kb.kc_cross = SDL_SCANCODE_Q;
    remap.kb.kc_circle = SDL_SCANCODE_ESCAPE;
    own = textIconPlan(INPUT_DEV_KEYBOARD, INPUT_ICONS_AUTO, INPUT_FAMILY_XBOX, &remap, cells);
    CHECK(own == 0 && cells[0] == PI_KEY_Q && cells[1] == PI_KEY_ESC && cells[2] == PI_PS_SQUARE && cells[3] == PI_PS_TRIANGLE);

    /* building the four icons: 12 x 12 indices of the atlas' own colours, transparent where it is */
    cells[0] = PI_XB_A; cells[1] = PI_XB_B; cells[2] = PI_XB_X; cells[3] = PI_XB_Y;
    textIconBuild(cells, &set);
    CHECK(set.clut[0] == 0);
    for (i = 0; i < TI_BUTTONS; i++) {
        CHECK(iconMismatch(&set, cells, i, 1) == 0); /* 14 colours: nothing merged, every pixel exact */
        CHECK(set.px[i][5 * TI_SIZE + 6] != 0);      /* the middle of a round button is opaque */
    }
    /* more than 15 colours: merged down, every index valid and the silhouette intact */
    big[0] = PI_XB_A; big[1] = PI_XB_B; big[2] = PI_XB_Y; big[3] = PI_DPAD_RIGHT;
    n = 0;
    for (i = 0; i < TI_BUTTONS; i++) {
        int x, y, j;

        for (y = 0; y < 14; y++) {
            for (x = 0; x < 14; x++) {
                unsigned short w = atlasWord(big[i], x, y);

                for (j = 0; j < n && seen[j] != w; j++) {
                }
                if (w != 0 && j == n) {
                    seen[n++] = w;
                }
            }
        }
    }
    CHECK(n > 15); /* the case is real */
    textIconBuild(big, &set);
    for (i = 0; i < TI_BUTTONS; i++) {
        CHECK(iconMismatch(&set, big, i, 0) == 0);
    }
    /* a cell with no picture is transparent; packing keeps the first pixel in the low nibble */
    cells[0] = cells[1] = cells[2] = cells[3] = PI_NONE;
    textIconBuild(cells, &set);
    for (i = 0; i < TI_SIZE * TI_SIZE; i++) {
        CHECK(set.px[0][i] == 0 && set.px[3][i] == 0);
        px[i] = (unsigned char)(i % 15 + 1);
    }
    textIconPack(px, words);
    CHECK(words[0] == 0x4321 && words[1] == 0x8765 && words[2] == 0xCBA9 && words[3] == 0x1FED);
}


/* O5b: the options menu's window art (menu_frame.c): pure pieces, on synthetic bytes (no retail data) */
static void put16(unsigned char *p, int v) {
    p[0] = (unsigned char)(v & 255);
    p[1] = (unsigned char)((v >> 8) & 255);
}

static void put32(unsigned char *p, long v) {
    put16(p, (int)(v & 0xFFFF));
    put16(p + 2, (int)((v >> 16) & 0xFFFF));
}

static int sndLog[32], sndN;

static void sndHook(int ev) {
    if (sndN < 32) {
        sndLog[sndN++] = ev;
    }
}

/* Plays the actions and returns the sounds that came out, in a list the test compares by value */
static void sndReset(void) { sndN = 0; }

/* android T7c: the touch actions (tap a row, pick a tab, swipe one row) */
static void testTouchActions(void) {
    int widgetRow = 1, slider = 2, i;
    CsWidget w;

    csWidgetsClear();
    w = (CsWidget){.kind = CS_ROW_ACTION, .label = "A"};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    w = (CsWidget){.kind = CS_ROW_TOGGLE, .label = "T"};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    w = (CsWidget){.kind = CS_ROW_SLIDER, .label = "S", .min = 0, .max = 10, .step = 1, .value = 5};
    csWidgetAdd(CS_TAB_DISPLAY, &w);
    w = (CsWidget){.kind = CS_ROW_ACTION, .label = "B"};
    csWidgetAdd(CS_TAB_DISPLAY, &w);

    csOpen();
    csSetTab(CS_TAB_DISPLAY);
    csSetCursor(0);
    /* a tap on another row's label focuses it and changes nothing */
    {
        int before = csWidgetGet(CS_TAB_DISPLAY, widgetRow);

        CHECK(csAct(CS_ACT_ROW_TAP(widgetRow, CS_TAP_LABEL)) == 1 && csCursor() == widgetRow);
        CHECK(csWidgetGet(CS_TAB_DISPLAY, widgetRow) == before);
        /* a second tap on the focused row activates it */
        CHECK(csAct(CS_ACT_ROW_TAP(widgetRow, CS_TAP_LABEL)) == 1);
        CHECK(csWidgetGet(CS_TAB_DISPLAY, widgetRow) != before);
        /* a tap on the value of another row focuses and activates at once */
        csSetCursor(0);
        CHECK(csAct(CS_ACT_ROW_TAP(widgetRow, CS_TAP_VALUE)) == 1 && csCursor() == widgetRow);
        CHECK(csWidgetGet(CS_TAB_DISPLAY, widgetRow) == before);
    }
    /* a row that does not exist is nothing */
    CHECK(csAct(CS_ACT_ROW_TAP(200, CS_TAP_LABEL)) == 0);
    /* a tab by number, the cursor of each tab is kept */
    csSetCursor(2);
    CHECK(csAct(CS_ACT_TAB_TO(CS_TAB_VIDEO)) == 1 && csTab() == CS_TAB_VIDEO);
    CHECK(csAct(CS_ACT_TAB_TO(CS_TAB_VIDEO)) == 0); /* already there */
    CHECK(csAct(CS_ACT_TAB_TO(CS_TAB_DISPLAY)) == 1 && csCursor() == 2);
    CHECK(csAct(CS_ACT_TAB_TO(99)) == 0 && csTab() == CS_TAB_DISPLAY);
    /* swipe rows stop at the ends */
    csSetCursor(0);
    CHECK(csAct(CS_ACT_ROW_PREV) == 0 && csCursor() == 0);
    CHECK(csAct(CS_ACT_ROW_NEXT) == 1 && csCursor() == 1);
    csSetCursor(csTabRowCount(CS_TAB_DISPLAY) - 1);
    CHECK(csAct(CS_ACT_ROW_NEXT) == 0 && csCursor() == csTabRowCount(CS_TAB_DISPLAY) - 1);
    /* a slider's halves step the value down and up */
    csSetCursor(0);
    i = csWidgetGet(CS_TAB_DISPLAY, slider);
    CHECK(csAct(CS_ACT_ROW_TAP(slider, CS_TAP_DEC)) == 1 && csCursor() == slider && csWidgetGet(CS_TAB_DISPLAY, slider) < i);
    CHECK(csAct(CS_ACT_ROW_TAP(slider, CS_TAP_INC)) == 1 && csWidgetGet(CS_TAB_DISPLAY, slider) == i);
    /* nothing happens while the screen is closed */
    csClose();
    CHECK(csAct(CS_ACT_ROW_TAP(0, CS_TAP_VALUE)) == 0 && csAct(CS_ACT_TAB_TO(CS_TAB_VIDEO)) == 0);
    csWidgetsClear();
}

static void testHoldRepeatSound(void) {
    CsRepeat rp;
    CsHold hold;
    CsWidget w;
    static const char *const names[] = {"A", "B", "C"};
    int i, k, fired[8], nf;

    /* the held direction: the last one pressed that is still down */
    csHoldInit(&hold);
    CHECK(csHoldAction(&hold) == CS_ACT_NONE);
    csHoldSet(&hold, CS_ACT_UP, 1);
    CHECK(csHoldAction(&hold) == CS_ACT_UP);
    csHoldSet(&hold, CS_ACT_LEFT, 1);
    CHECK(csHoldAction(&hold) == CS_ACT_LEFT);
    csHoldSet(&hold, CS_ACT_LEFT, 0);
    CHECK(csHoldAction(&hold) == CS_ACT_UP);
    csHoldSet(&hold, CS_ACT_CONFIRM, 1); /* not a direction: ignored */
    CHECK(csHoldAction(&hold) == CS_ACT_UP);
    csHoldSet(&hold, CS_ACT_UP, 0);
    CHECK(csHoldAction(&hold) == CS_ACT_NONE);

    /* the timer is the game's own (port.c: the first repeat after 0x10 vsyncs, then 6, then every 4) */
    csRepeatInit(&rp);
    CHECK(csRepeatTick(&rp, CS_ACT_NONE) == CS_ACT_NONE);
    CHECK(csRepeatTick(&rp, CS_ACT_DOWN) == CS_ACT_NONE); /* the press frame: its own action came from the event */
    nf = 0;
    for (i = 1; i <= 40; i++) {
        if (csRepeatTick(&rp, CS_ACT_DOWN) == CS_ACT_DOWN && nf < 8) {
            fired[nf++] = i;
        }
    }
    CHECK(nf == 6 && fired[0] == 16 && fired[1] == 22 && fired[2] == 26 && fired[3] == 30 && fired[4] == 34 && fired[5] == 38);
    CHECK(csRepeatTick(&rp, CS_ACT_NONE) == CS_ACT_NONE); /* released: the next press starts again */
    CHECK(csRepeatTick(&rp, CS_ACT_UP) == CS_ACT_NONE);
    for (i = 1; i < 16; i++) {
        CHECK(csRepeatTick(&rp, CS_ACT_UP) == CS_ACT_NONE);
    }
    CHECK(csRepeatTick(&rp, CS_ACT_UP) == CS_ACT_UP);
    CHECK(csRepeatTick(&rp, CS_ACT_LEFT) == CS_ACT_NONE); /* another direction: a new press */
    for (i = 1; i < 16; i++) {
        CHECK(csRepeatTick(&rp, CS_ACT_LEFT) == CS_ACT_NONE);
    }
    CHECK(csRepeatTick(&rp, CS_ACT_LEFT) == CS_ACT_LEFT);

    /* the screen: sounds and the frame hook */
    csClose();
    csWidgetsClear();
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_SLIDER;
    w.label = "Volume";
    w.min = 0;
    w.max = 100;
    w.step = 10;
    w.value = 50;
    CHECK(csWidgetAdd(CS_TAB_AUDIO, &w) == 0);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_CYCLE;
    w.label = "Mode";
    w.names = names;
    w.count = 3;
    CHECK(csWidgetAdd(CS_TAB_AUDIO, &w) == 1);
    memset(&w, 0, sizeof(w));
    w.kind = CS_ROW_TOGGLE;
    w.label = "Flag";
    CHECK(csWidgetAdd(CS_TAB_AUDIO, &w) == 2);
    csSetSoundHook(sndHook);
    sndReset();
    csSetTab(CS_TAB_AUDIO);
    CHECK(csAct(CS_ACT_TOGGLE) == 1 && sndN == 1 && sndLog[0] == CS_SND_OPEN);
    csSetCursor(0);
    CHECK(csAct(CS_ACT_DOWN) == 1 && sndN == 2 && sndLog[1] == CS_SND_MOVE);
    CHECK(csAct(CS_ACT_UP) == 1 && sndN == 3 && sndLog[2] == CS_SND_MOVE);
    CHECK(csAct(CS_ACT_RIGHT) == 1 && sndN == 4 && sndLog[3] == CS_SND_VALUE && csWidgetGet(CS_TAB_AUDIO, 0) == 60);
    CHECK(csAct(CS_ACT_TAB_NEXT) == 1 && sndN == 5 && sndLog[4] == CS_SND_TAB);
    CHECK(csAct(CS_ACT_TAB_PREV) == 1 && sndN == 6 && sndLog[5] == CS_SND_TAB);
    csWidgetSet(CS_TAB_AUDIO, 0, 100);
    sndReset();
    CHECK(csAct(CS_ACT_RIGHT) == 0 && sndN == 0); /* the slider is at its end: nothing moved, no sound */
    csSetCursor(2);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && sndN == 1 && sndLog[0] == CS_SND_CONFIRM);
    sndReset();
    csSetTab(CS_TAB_CONTROLS);
    csSetCursor(CS_CTL_ROW_BACK);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && !csIsOpen() && sndN == 1 && sndLog[0] == CS_SND_CLOSE); /* Back is a close, once */
    sndReset();
    CHECK(csAct(CS_ACT_TOGGLE) == 1 && csAct(CS_ACT_CANCEL) == 1 && !csIsOpen());
    CHECK(sndN == 2 && sndLog[0] == CS_SND_OPEN && sndLog[1] == CS_SND_CLOSE);

    /* the bind capture still works and its end has a sound; the keys pressed meanwhile make none */
    sndReset();
    csSetTab(CS_TAB_CONTROLS);
    CHECK(csAct(CS_ACT_TOGGLE) == 1 && csDevice() == INPUT_DEV_KEYBOARD);
    csSetCursor(0);
    CHECK(csAct(CS_ACT_CONFIRM) == 1 && csWaitingRow() == 0 && sndLog[sndN - 1] == CS_SND_CONFIRM);
    k = sndN;
    CHECK(csAct(CS_ACT_DOWN) == 0 && sndN == k); /* held back while it waits: no sound */
    CHECK(csRepeatFrame(CS_ACT_DOWN) == CS_ACT_NONE && sndN == k);
    inputCapCancel();
    csPump(); /* sees the capture end */
    CHECK(csWaitingRow() == -1 && sndLog[sndN - 1] == CS_SND_CANCEL);
    csAct(CS_ACT_TOGGLE);
    sndReset();

    /* held keys on the screen: rows repeat, the sliders and cycles repeat, a toggle and the device switch do not */
    csOpen();
    csSetTab(CS_TAB_AUDIO);
    csSetCursor(0);
    csWidgetSet(CS_TAB_AUDIO, 0, 50);
    csRepeatReset();
    CHECK(csRepeatFrame(CS_ACT_RIGHT) == CS_ACT_NONE);
    for (i = 1; i < 16; i++) {
        CHECK(csRepeatFrame(CS_ACT_RIGHT) == CS_ACT_NONE);
    }
    CHECK(csRepeatFrame(CS_ACT_RIGHT) == CS_ACT_RIGHT && csWidgetGet(CS_TAB_AUDIO, 0) == 60);
    csSetCursor(2); /* the toggle */
    csRepeatReset();
    CHECK(csRepeatFrame(CS_ACT_RIGHT) == CS_ACT_NONE);
    for (i = 1; i <= 40; i++) {
        CHECK(csRepeatFrame(CS_ACT_RIGHT) == CS_ACT_NONE);
    }
    csRepeatReset();
    CHECK(csRepeatFrame(CS_ACT_DOWN) == CS_ACT_NONE);
    for (i = 1; i < 16; i++) {
        csRepeatFrame(CS_ACT_DOWN);
    }
    CHECK(csRepeatFrame(CS_ACT_DOWN) == CS_ACT_DOWN && csCursor() == 0); /* 2 -> wraps to 0 */
    csClose();
    CHECK(csRepeatFrame(CS_ACT_DOWN) == CS_ACT_NONE); /* closed: nothing */
    csSetSoundHook(NULL);
    csWidgetsClear();
}

static void testMenuFrame(void) {
    static unsigned char sheet[128], arch[160], scratch[64];
    unsigned short px[4], bk[3] = {1, 2, 3}, ours[3] = {9, 9, 9}, cur[3] = {9, 5, 3};
    MfSlice sl[9];
    MfPart part;
    MfImage im;
    long area;
    int i, n, last;

    /* the open animation grows by a tenth of ONE a frame and stays there */
    CHECK(mfOpenLevel(0) == 0 && mfOpenLevel(-3) == 0);
    CHECK(mfOpenLevel(1) == MF_ONE / 10 && mfOpenLevel(5) == 5 * (MF_ONE / 10));
    CHECK(mfOpenLevel(MF_OPEN_FRAMES) == MF_ONE && mfOpenLevel(99) == MF_ONE);
    for (i = 1, last = 0; i <= 12; i++) {
        CHECK(mfOpenLevel(i) >= last);
        last = mfOpenLevel(i);
    }
    /* the cursor rests 32 frames on glyph 0, then swings 1 2 3 2 for 6 frames each and starts over */
    CHECK(mfCursorFrame(0) == 0 && mfCursorFrame(31) == 0 && mfCursorFrame(-1) == 0);
    CHECK(mfCursorFrame(32) == 1 && mfCursorFrame(37) == 1 && mfCursorFrame(38) == 2);
    CHECK(mfCursorFrame(44) == 3 && mfCursorFrame(49) == 3 && mfCursorFrame(50) == 2 && mfCursorFrame(55) == 2);
    CHECK(mfCursorFrame(56) == 0 && mfCursorFrame(56 + 33) == 1);

    /* the cycle arrows swing outward with the cursor's timing: still for 32 frames, then 1, 2, 3, 2 pixels */
    CHECK(mfArrowOffset(0) == 0 && mfArrowOffset(31) == 0 && mfArrowOffset(-5) == 0);
    CHECK(mfArrowOffset(32) == 1 && mfArrowOffset(38) == 2 && mfArrowOffset(44) == 3 && mfArrowOffset(50) == 2);
    CHECK(mfArrowOffset(56) == 0);
    for (i = 0; i < 200; i++) {
        CHECK(mfArrowOffset(i) == mfCursorFrame(i));
    }

    /* the tab strip: boxes in a row, centred in the space, shrinking the padding and gaps when it is short */
    {
        int tw[5] = {50, 30, 40, 46, 28}, tx[5], bw[5], tot, k;

        tot = mfTabLayout(tw, 5, 260, 4, 2, tx, bw);
        CHECK(tot == 194 + 40 + 8 && tx[0] == (260 - tot) / 2 && bw[0] == 58 && bw[4] == 36);
        for (k = 1; k < 5; k++) {
            CHECK(tx[k] == tx[k - 1] + bw[k - 1] + 2);
        }
        tot = mfTabLayout(tw, 5, 220, 4, 2, tx, bw); /* 242 wide does not fit: the gap goes first, then the padding */
        CHECK(tot <= 220 && tx[0] >= 0 && tx[4] + bw[4] <= 220 && bw[0] > tw[0]);
        for (k = 1; k < 5; k++) {
            CHECK(tx[k] >= tx[k - 1] + bw[k - 1]);
        }
        tot = mfTabLayout(tw, 5, 100, 4, 2, tx, bw); /* never fits: still ordered, never negative */
        CHECK(tx[0] >= 0 && bw[0] >= tw[0]);
    }

    /* the tab carousel: all tabs when they fit at full size, else a run around the current one that fits */
    {
        int tw[6] = {50, 30, 40, 46, 28, 80}, first, count, k, cur, sum;

        count = mfTabWindow(tw, 5, 2, 260, 4, 2, 10, &first);
        CHECK(first == 0 && count == 5); /* 242 wide fits in 260 */
        for (cur = 0; cur < 6; cur++) {
            count = mfTabWindow(tw, 6, cur, 220, 4, 2, 10, &first);
            CHECK(count >= 1 && count < 6 && first <= cur && cur < first + count && first + count <= 6);
            for (sum = 0, k = first; k < first + count; k++) {
                sum += tw[k] + 8;
            }
            CHECK(sum + 2 * (count - 1) <= 220 - 2 * 10); /* full padding and gaps, room left for both arrows */
        }
        count = mfTabWindow(tw, 6, 5, 30, 4, 2, 10, &first); /* not even the current one fits: still shown */
        CHECK(first == 5 && count == 1);
    }

    /* nine-slice: corners keep their size, the pieces tile the panel with no gap or overlap */
    n = mfSlice9(288, 236, 44, 44, 4, 6, 4, 6, sl);
    CHECK(n == 9);
    CHECK(sl[0].dx == 0 && sl[0].dy == 0 && sl[0].dw == 4 && sl[0].dh == 4 && sl[0].su == 0 && sl[0].sv == 0);
    CHECK(sl[2].dx == 282 && sl[2].dw == 6 && sl[2].su == 38 && sl[2].sw == 6);
    CHECK(sl[4].dx == 4 && sl[4].dy == 4 && sl[4].dw == 278 && sl[4].dh == 226 && sl[4].sw == 34 && sl[4].sh == 34);
    CHECK(sl[8].dx == 282 && sl[8].dy == 230 && sl[8].dw == 6 && sl[8].dh == 6 && sl[8].su == 38 && sl[8].sv == 38);
    for (i = 0, area = 0; i < n; i++) {
        area += (long)sl[i].dw * sl[i].dh;
    }
    CHECK(area == 288L * 236);
    /* narrower than its margins: they shrink in proportion and the middle disappears */
    n = mfSlice9(5, 236, 44, 44, 4, 6, 4, 6, sl);
    CHECK(n == 6);
    for (i = 0, area = 0; i < n; i++) {
        area += (long)sl[i].dw * sl[i].dh;
    }
    CHECK(area == 5L * 236);
    CHECK(mfSlice9(0, 10, 44, 44, 4, 6, 4, 6, sl) == 0 && mfSlice9(10, 10, 8, 8, 4, 6, 4, 6, sl) == 0);

    /* a sheet: header (parts, ids, one offset per frame), 3 parts, ids 7 and 9, one part in frame 7, two in frame 9 */
    memset(sheet, 0, sizeof(sheet));
    put32(sheet, 4); /* the sheet is entry 0 of the file, at byte 4 */
    {
        unsigned char *s = sheet + 4;

        put32(s, 16);      /* parts */
        put32(s + 4, 58);  /* ids */
        put32(s + 8, 62);  /* the offset of frame 7 */
        put32(s + 12, 74); /* and of frame 9 */
        /* parts: u v w h clutX clutY mode */
        put16(s + 16 + 4, 8);
        put16(s + 16 + 6, 8);
        put16(s + 30, 384);
        put16(s + 30 + 2, 95);
        put16(s + 30 + 4, 44);
        put16(s + 30 + 6, 44);
        put16(s + 30 + 8, 48);
        put16(s + 30 + 10, 154);
        put16(s + 44 + 4, 20);
        put16(s + 44 + 6, 2);
        s[58] = 7;
        s[59] = 9;
        s[60] = 0xFF;
        /* frame 7: one part (index 1); frame 9: two parts (indices 0 and 2); each part is index, x, y */
        put16(s + 62, 1);
        put16(s + 62 + 4, -1);
        put16(s + 62 + 6, 1);
        put16(s + 74, 2);
        put16(s + 74 + 4, -1);
        put16(s + 74 + 6, 0);
        put16(s + 74 + 12, 2);
    }
    CHECK(mfSheetPart(sheet, sizeof(sheet), 7, -1, &part) == 1);
    CHECK(part.u == 384 && part.v == 95 && part.w == 44 && part.h == 44 && part.clutX == 48 && part.clutY == 154);
    CHECK(mfSheetPart(sheet, sizeof(sheet), 9, -1, &part) == 1 && part.w == 8 && part.h == 8); /* the largest */
    CHECK(mfSheetPart(sheet, sizeof(sheet), 9, 1, &part) == 1 && part.w == 20 && part.h == 2);
    CHECK(mfSheetPart(sheet, sizeof(sheet), 8, -1, &part) == 0);                    /* no such frame */
    CHECK(mfSheetPart(sheet, 20, 7, -1, &part) == 0 && mfSheetPart(sheet, 3, 7, -1, &part) == 0); /* cut short */

    /* an archive of two 2x2 images, the first raw, the second packed ("RLEN": a literal run, then 0) */
    memset(arch, 0, sizeof(arch));
    put32(arch, 4);
    put32(arch + 4, 12);
    put32(arch + 8, 12 + 28);
    {
        unsigned char *a = arch + 4 + 12, tim[28];
        int k;

        memset(tim, 0, sizeof(tim));
        put32(tim, 0x10);
        put32(tim + 4, 2);
        put32(tim + 8, 20);
        put16(tim + 16, 2);
        put16(tim + 18, 2);
        for (k = 0; k < 4; k++) {
            put16(tim + 20 + 2 * k, 0x1111 * (k + 1));
        }
        memcpy(a, tim, sizeof(tim));
        a += 28;
        put32(a, 0x4E454C52);
        put32(a + 4, 28);
        a[8] = 28;
        memcpy(a + 9, tim, sizeof(tim));
        a[9 + 28] = 0;
    }
    CHECK(mfArchiveImage(arch, sizeof(arch), 0, scratch, sizeof(scratch), &im) == 1 && im.w == 2 && im.h == 2);
    CHECK(mfImageCopy(&im, 0, 0, 2, 2, px, 2) == 1 && px[0] == 0x1111 && px[3] == 0x4444);
    CHECK(mfImageCopy(&im, 1, 1, 1, 1, px, 1) == 1 && px[0] == 0x4444);
    CHECK(mfImageCopy(&im, 1, 1, 2, 1, px, 2) == 0 && mfImageCopy(&im, 0, 0, 2, 2, px, 1) == 0); /* outside */
    CHECK(mfArchiveImage(arch, sizeof(arch), 1, scratch, sizeof(scratch), &im) == 1 && im.w == 2);
    CHECK(mfImageCopy(&im, 0, 1, 2, 1, px, 2) == 1 && px[0] == 0x3333 && px[1] == 0x4444);
    CHECK(mfArchiveImage(arch, sizeof(arch), 2, scratch, sizeof(scratch), &im) == 0);     /* the table's end */
    CHECK(mfArchiveImage(arch, sizeof(arch), 1, scratch, 20, &im) == 0);                  /* does not fit */
    CHECK(mfArchiveImage(arch, 30, 1, scratch, sizeof(scratch), &im) == 0);               /* cut short */
    CHECK(mfRlenUnpack(arch + 4 + 12 + 28, 20, scratch, sizeof(scratch)) == -1);

    /* the borrowed VRAM: the game's own write into the area becomes the backup; ours and untouched words do not */
    CHECK(mfLeaseSync(bk, ours, cur, 3) == 1 && bk[0] == 1 && bk[1] == 5 && bk[2] == 3);
    cur[0] = 7;
    CHECK(mfLeaseSync(bk, ours, cur, 3) == 1 && bk[0] == 7);
    CHECK(mfLeaseSync(bk, ours, cur, 3) == 0);
}

/* Battle speed -- the scaled clock step (8.8 fixed point, 0x100 = one vsync) */
static void testBattleSpeed(void) {
    CHECK(battleSpeedStep(BATTLE_SPEED_NORMAL, 1, 0x100) == 0x100);
    CHECK(battleSpeedStep(BATTLE_SPEED_NORMAL, 0, 0x100) == 0x100);
    CHECK(battleSpeedStep(BATTLE_SPEED_X15, 0, 0x100) == 0x100); /* gate closed */
    CHECK(battleSpeedStep(BATTLE_SPEED_X2, 0, 0x100) == 0x100);
    CHECK(battleSpeedStep(BATTLE_SPEED_X15, 1, 0x100) == 0x180);
    CHECK(battleSpeedStep(BATTLE_SPEED_X2, 1, 0x100) == 0x200);
    CHECK(battleSpeedStep(BATTLE_SPEED_X15, 1, 0x200) == 0x300); /* a 30 Hz base scales too */
    CHECK(battleSpeedStep(-1, 1, 0x100) == 0x100 && battleSpeedStep(BATTLE_SPEED_MODES, 1, 0x100) == 0x100);
    battleSpeedSetMode(BATTLE_SPEED_X2);
    CHECK(battleSpeedMode() == BATTLE_SPEED_X2);
    battleSpeedSetMode(99); /* not a mode: back to Normal */
    CHECK(battleSpeedMode() == BATTLE_SPEED_NORMAL);
    battleSpeedSetMode(BATTLE_SPEED_NORMAL);
}

/* Fast stage banner: the stage is shown only once no tile in view is pending (the blue placeholder grid) */
static void testStageBannerGate(void) {
    stageBannerSetMode(STAGE_BANNER_FAST);
    stageBannerSetShown(0);
    CHECK(stageBannerHidesMap());
    CHECK(!stageBannerMayShow(12, 0) && !stageBannerMayShow(1, 5) && !stageBannerMayShow(1, STAGE_BANNER_SHOW_WAIT_MAX - 1));
    CHECK(stageBannerMayShow(0, 0) && stageBannerMayShow(0, 30));
    CHECK(stageBannerMayShow(3, STAGE_BANNER_SHOW_WAIT_MAX)); /* a tile that never comes cannot hide the stage for good */
    stageBannerSetShown(1);
    CHECK(!stageBannerHidesMap());
    stageBannerSetShown(0);
    /* the exit wipe: the game's grid cover is not drawn in Fast, only in Classic (the original) */
    CHECK(stageExitCoverHidden(1, 0, 1));
    CHECK(!stageExitCoverHidden(1, 0, 0) && !stageExitCoverHidden(0, 0, 1) && !stageExitCoverHidden(1, 1, 1) &&
          !stageExitCoverHidden(0, 1, 0));
}

/* O6: auto text -- the wait before the port presses confirm, and the per-frame decision */
static void testAutoText(void) {
    AutoText a = {0, 0};
    int n, i, fired;

    CHECK(autoTextDelayFrames(AUTO_TEXT_OFF, 20) == -1);
    CHECK(autoTextDelayFrames(-1, 20) == -1 && autoTextDelayFrames(AUTO_TEXT_SPEEDS, 20) == -1);
    CHECK(autoTextDelayFrames(AUTO_TEXT_NORMAL, 0) == 30);   /* 0.5 s, no characters */
    CHECK(autoTextDelayFrames(AUTO_TEXT_NORMAL, 20) == 60);  /* + 25 ms each */
    CHECK(autoTextDelayFrames(AUTO_TEXT_SLOW, 20) == 120);   /* x2 */
    CHECK(autoTextDelayFrames(AUTO_TEXT_FAST, 20) == 30);    /* x0.5 */
    CHECK(autoTextDelayFrames(AUTO_TEXT_FAST, 0) == AUTO_TEXT_MIN_FRAMES); /* 15 raised to the minimum */
    CHECK(autoTextDelayFrames(AUTO_TEXT_NORMAL, -5) == 30); /* a bad length counts as none */
    CHECK(autoTextDelayFrames(AUTO_TEXT_NORMAL, 100000) == autoTextDelayFrames(AUTO_TEXT_NORMAL, AUTO_TEXT_MAX_CHARS));
    CHECK(autoTextDelayFrames(AUTO_TEXT_NORMAL, 60) > autoTextDelayFrames(AUTO_TEXT_NORMAL, 10)); /* grows */
    CHECK(autoTextDelayFrames(AUTO_TEXT_SLOW, 30) > autoTextDelayFrames(AUTO_TEXT_NORMAL, 30));
    CHECK(autoTextDelayFrames(AUTO_TEXT_NORMAL, 30) > autoTextDelayFrames(AUTO_TEXT_FAST, 30));

    /* Off never fires, however long the prompt waits */
    for (i = 0, fired = 0; i < 2000; i++) {
        fired += autoTextTick(&a, AUTO_TEXT_OFF, 1, 7, 10);
    }
    CHECK(fired == 0);

    /* Normal, 20 characters: fires on the 60th waiting frame, once, then waits again */
    memset(&a, 0, sizeof(a));
    n = autoTextDelayFrames(AUTO_TEXT_NORMAL, 20);
    for (i = 1, fired = 0; i < n; i++) {
        fired += autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20);
    }
    CHECK(fired == 0);
    CHECK(autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20) == 1);
    CHECK(autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20) == 0); /* the press is a single frame */
    for (i = 2; i < n; i++) {
        CHECK(autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20) == 0);
    }
    CHECK(autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20) == 1);  /* still there: the wait ran again */

    /* a frame with no prompt resets the wait (the player pressed, the box moved on) */
    memset(&a, 0, sizeof(a));
    for (i = 0; i < n - 1; i++) {
        autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20);
    }
    CHECK(autoTextTick(&a, AUTO_TEXT_NORMAL, 0, 7, 20) == 0 && a.waited == 0);
    CHECK(autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20) == 0 && a.waited == 1);

    /* a different prompt (another box or another place in the text) restarts it */
    memset(&a, 0, sizeof(a));
    for (i = 0; i < n - 1; i++) {
        autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20);
    }
    CHECK(autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 8, 20) == 0 && a.waited == 1);

    /* switching the speed to Off mid wait cancels it; back on, it starts over */
    memset(&a, 0, sizeof(a));
    for (i = 0; i < n - 1; i++) {
        autoTextTick(&a, AUTO_TEXT_NORMAL, 1, 7, 20);
    }
    CHECK(autoTextTick(&a, AUTO_TEXT_OFF, 1, 7, 20) == 0 && a.waited == 0);

    /* the setting */
    autoTextSetSpeed(AUTO_TEXT_FAST);
    CHECK(autoTextSpeed() == AUTO_TEXT_FAST);
    autoTextSetSpeed(99);
    CHECK(autoTextSpeed() == AUTO_TEXT_OFF); /* out of range -> Off */
    autoTextSetSpeed(AUTO_TEXT_OFF);
    CHECK(autoTextSpeed() == AUTO_TEXT_OFF);

    /* O6b: Text speed -- the typing delay of a dialogue window, per page */
    CHECK(TEXT_SPEED_MODES == 2);
    CHECK(textSpeedDelay(TEXT_SPEED_OFF, 6) == 6 && textSpeedDelay(TEXT_SPEED_ON, 6) == 1);
    CHECK(textSpeedDelay(99, 6) == 6 && textSpeedDelay(-1, 6) == 6);
    /* a cross press speeds only this page: Off is 1 while boosted, back to the setting at the next page */
    CHECK(textSpeedPageDelay(TEXT_SPEED_OFF, 6, 0) == 6 && textSpeedPageDelay(TEXT_SPEED_OFF, 6, 1) == 1);
    CHECK(textSpeedPageDelay(TEXT_SPEED_ON, 6, 1) == 1 && textSpeedPageDelay(TEXT_SPEED_ON, 6, 0) == 1);
    CHECK(textSpeedPageStart(0, 1, 10, 10, 1) == 1);  /* first sight */
    CHECK(textSpeedPageStart(1, 1, 10, 10, 0) == 0);  /* typing on */
    CHECK(textSpeedPageStart(1, 1, 10, 40, 0) == 1);  /* start moved */
    CHECK(textSpeedPageStart(2, 1, 40, 40, 0) == 1);  /* released from a wait */
    CHECK(textSpeedPageStart(1, 2, 40, 40, 0) == 0);  /* entering a wait is not a page start */
    CHECK(textSpeedBoost(1, 1, 1) == 1);
    CHECK(textSpeedBoost(2, 1, 1) == 0 && textSpeedBoost(1, 2, 1) == 0 && textSpeedBoost(1, 1, 0) == 0);
    textSpeedSetMode(TEXT_SPEED_ON);
    CHECK(textSpeedMode() == TEXT_SPEED_ON);
    textSpeedSetMode(99);
    CHECK(textSpeedMode() == TEXT_SPEED_OFF);
    {
        InputCfg got, d;

        inputCfgDefaults(&d);
        csSettingsInstall();
        csWidgetsClear();
        got = d;
        CHECK(inputCfgParse("[display]\nauto_text = fast\ntext_speed = on\n", &got, logSink) == 0);
        optionsVideoInit();
        CHECK(csWidgetGet(CS_TAB_DISPLAY, 1) == AUTO_TEXT_FAST && autoTextSpeed() == AUTO_TEXT_FAST);
        CHECK(csWidgetGet(CS_TAB_DISPLAY, 2) == 1 && textSpeedMode() == TEXT_SPEED_ON);
        autoTextSetSpeed(AUTO_TEXT_OFF);
        textSpeedSetMode(TEXT_SPEED_OFF);
        csWidgetsClear();
    }
}

/* State scopes (overlay-state-reset R2a): registry logic only, on plain byte arrays. */
static unsigned char scModeData[8], scSubData[8], scSubData2[8];
static char scLog[256];

static void scNote(const char *what) {
    strncat(scLog, what, sizeof(scLog) - strlen(scLog) - 1);
}

static void scHookEnter(StateScope *s, void *user) {
    scNote("+");
    scNote(user != NULL ? (const char *)user : stateScopeName(s));
}

static void scHookExit(StateScope *s, void *user) {
    scNote("-");
    scNote(user != NULL ? (const char *)user : stateScopeName(s));
}

static void scHookEnterTwice(StateScope *s, void *user) {
    (void)s;
    scNote("+");
    scNote((const char *)user);
}

static void testStateScopes(void) {
    StateScope *mode, *sub, *sub2, *other;

    stateScopeClear();
    memset(scModeData, 0x11, sizeof(scModeData));
    memset(scSubData, 0x22, sizeof(scSubData));
    memset(scSubData2, 0x33, sizeof(scSubData2));
    mode = stateScopeCreate("mode", NULL);
    sub = stateScopeCreate("sub", mode);
    sub2 = stateScopeCreate("sub2", mode);
    other = stateScopeCreate("other", NULL);
    CHECK(mode != NULL && sub != NULL && sub2 != NULL && other != NULL);
    CHECK(stateScopeFind("sub") == sub && stateScopeFind("nope") == NULL);
    CHECK(stateScopeAddRange(mode, scModeData, scModeData + 8) == 0);
    CHECK(stateScopeAddRange(sub, scSubData, scSubData + 8) == 0);
    CHECK(stateScopeAddRange(sub2, scSubData2, scSubData2 + 8) == 0);
    CHECK(stateScopeAddRange(mode, scModeData + 4, scModeData) == -1); /* end before begin */
    CHECK(stateScopeOnEnter(mode, scHookEnter, NULL) == 0);
    CHECK(stateScopeOnEnter(mode, scHookEnterTwice, (void *)"second") == 0);
    CHECK(stateScopeOnExit(mode, scHookExit, NULL) == 0);
    CHECK(stateScopeOnExit(sub, scHookExit, NULL) == 0);
    CHECK(stateScopeBindMode(6, mode) == 0);
    CHECK(stateScopeBindMode(2, other) == 0);
    CHECK(stateScopeBindSub(0x208, sub) == 0);
    CHECK(stateScopeBindSub(0x300, sub2) == 0);

    /* First entry: restore (a no-op, nothing changed), hooks in registration order */
    scLog[0] = 0;
    stateScopeModeEntered(0x600);
    CHECK(strcmp(scLog, "+mode+second") == 0);
    CHECK(stateScopeIsActive(mode) && !stateScopeIsActive(sub));

    /* A sub load enters its scope; the mode scope is untouched by it */
    scSubData[0] = 0x99;
    scModeData[1] = 0x77;
    scLog[0] = 0;
    stateScopeSubLoaded(0x208); /* enters: the sub's range goes back to its snapshot */
    CHECK(scLog[0] == 0 && stateScopeIsActive(sub) && scSubData[0] == 0x22);
    scSubData[0] = 0x99;
    CHECK(scSubData[0] == 0x99 && scModeData[1] == 0x77);
    stateScopeSubLoaded(0x208); /* same file: no reset */
    CHECK(scSubData[0] == 0x99);

    /* Same mode id again (field -> field): no reset, and the sub stays remembered */
    stateScopeModeEntered(0x601);
    CHECK(scModeData[1] == 0x77 && scSubData[0] == 0x99 && scLog[0] == 0);
    stateScopeSubLoaded(0x208);
    CHECK(scSubData[0] == 0x99);

    /* A different sub file: the old sub leaves (onExit), the new one resets */
    scSubData2[3] = 0x55;
    stateScopeSubLoaded(0x300);
    CHECK(strcmp(scLog, "-sub") == 0 && scSubData2[3] == 0x33 && !stateScopeIsActive(sub));
    CHECK(scSubData[0] == 0x99); /* a scope that left keeps what it had until it enters again */
    scSubData2[3] = 0x55;

    /* A different mode id: the mode leaves (children first), restores, re-enters later */
    scLog[0] = 0;
    stateScopeModeEntered(0x200);
    CHECK(strcmp(scLog, "-sub2-mode") == 0 || strcmp(scLog, "-mode") == 0);
    CHECK(!stateScopeIsActive(mode) && stateScopeIsActive(other));
    scLog[0] = 0;
    stateScopeModeEntered(0x600); /* back: reset to the snapshot, sub forgotten */
    CHECK(strcmp(scLog, "+mode+second") == 0);
    CHECK(scModeData[1] == 0x11 && scSubData2[3] == 0x33 && scSubData[0] == 0x22);
    scSubData[0] = 0x99;
    stateScopeSubLoaded(0x208); /* the sub forgotten with the mode: resets on the next load */
    CHECK(scSubData[0] == 0x22);

    /* Entering an active scope re-enters it: exit hooks, restore (children too), enter hooks */
    scSubData[0] = 0x99;
    scModeData[2] = 0x66;
    scLog[0] = 0;
    stateScopeEnter(mode);
    CHECK(strcmp(scLog, "-sub-mode+mode+second") == 0);
    CHECK(scSubData[0] == 0x22 && scModeData[2] == 0x11 && !stateScopeIsActive(sub));

    /* An unbound mode or sub id leaves the tables alone, and a mode change still leaves the old scope */
    stateScopeModeEntered(0x1700);
    CHECK(!stateScopeIsActive(mode));
    stateScopeSubLoaded(0x9999);
    stateScopeModeEntered(0x600);
    CHECK(stateScopeIsActive(mode));

    /* A carried range survives the reset when the scope is entered from its mode id (field 0x2xx <-> 0x3xx), and only then */
    {
        static unsigned char carried[4];

        CHECK(stateScopeBindMode(7, mode) == 0);
        CHECK(stateScopeAddRange(mode, carried, carried + 4) == 0);
        CHECK(stateScopeCarry(mode, carried, 4, 7) == 0);
        CHECK(stateScopeCarry(mode, NULL, 4, 7) == -1);
        stateScopeModeEntered(0x600);
        carried[0] = 0x42;
        stateScopeModeEntered(0x700); /* 6 -> 7: reset, but the carry says "from 7" only */
        CHECK(carried[0] == 0);
        carried[0] = 0x42;
        stateScopeModeEntered(0x600); /* 7 -> 6: the carry applies */
        CHECK(carried[0] == 0x42);
        stateScopeModeEntered(0x1700); /* leave, then come back from an unrelated mode: reset */
        stateScopeModeEntered(0x600);
        CHECK(carried[0] == 0);
    }

    /* Many scopes: the registry has no fixed size */
    {
        int i;
        char names[200][12];
        for (i = 0; i < 200; i++) {
            sprintf(names[i], "s%d", i);
            CHECK(stateScopeCreate(names[i], NULL) != NULL);
            CHECK(stateScopeBindSub(1000 + i, stateScopeFind(names[i])) == 0);
        }
        CHECK(stateScopeFind("s199") != NULL);
        stateScopeSubLoaded(1199);
        CHECK(stateScopeIsActive(stateScopeFind("s199")));
        stateScopeClear();
        CHECK(stateScopeFind("s199") == NULL);
    }
}

/* ---- W2: the player's first run (shim/disc_picker.c, the pref folder choice, the [disc] key) ---- */

/* A raw 2352-byte-frame image with an ISO 9660 volume: PVD at LBA 16, the root directory at 17 (SYSTEM.CNF
   at 18). cooked = 2048-byte sectors instead. */
static void putData(unsigned char *img, int cooked, int lba, const unsigned char *data, int n) {
    static const unsigned char sync[12] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};

    if (cooked) {
        memcpy(img + (size_t)lba * 2048, data, (size_t)n);
    } else {
        memcpy(img + (size_t)lba * 2352, sync, 12);
        memcpy(img + (size_t)lba * 2352 + 24, data, (size_t)n);
    }
}

static int writeImage(const char *path, const char *bootLine, int cooked, int withVolume) {
    size_t sector = cooked ? 2048 : 2352;
    size_t size = sector * 20;
    unsigned char *img = (unsigned char *)calloc(1, size);
    unsigned char pvd[2048] = {0}, dirBlock[2048] = {0}, cnf[2048] = {0};
    unsigned char *r;
    FILE *f;
    int len;

    if (withVolume) {
        pvd[0] = 1;
        memcpy(pvd + 1, "CD001", 5);
        pvd[156] = 34;
        pvd[156 + 2] = 17;
        pvd[156 + 10] = 0;
        pvd[156 + 11] = 8; /* 2048 */
        putData(img, cooked, 16, pvd, 2048);
        r = dirBlock;
        r[0] = 34; r[2] = 17; r[32] = 1; r[33] = 0; /* "." */
        r += 34;
        r[0] = 44; r[2] = 18; r[10] = 0; r[11] = 1; r[32] = 12;
        memcpy(r + 33, "SYSTEM.CNF;1", 12);
        putData(img, cooked, 17, dirBlock, 2048);
        len = snprintf((char *)cnf, sizeof(cnf), "%s\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n", bootLine);
        (void)len;
        putData(img, cooked, 18, cnf, 2048);
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        free(img);
        return 0;
    }
    fwrite(img, 1, size, f);
    fclose(f);
    free(img);
    return 1;
}

static void testFirstRun(const char *dir) {
    char out[1100], path[600], bin[600], det[64], msg[600];
    uint8_t dirBlk[2048] = {0};
    uint32_t lba = 0, size = 0;
    struct tm t = {0};
    const char *names[14];
    char store[14][40];
    int drop[14], i, n;
    InputCfg cfg;
    char text[4096];

    /* cue -> bin */
    CHECK(dpCueBin("FILE \"Digimon World 2003.bin\" BINARY\n  TRACK 01 MODE2/2352\n", "/games/DW3/x.cue", out, sizeof(out))
          && strcmp(out, "/games/DW3/Digimon World 2003.bin") == 0);
    CHECK(dpCueBin("  file disc.bin BINARY\r\n", "C:\\games\\x.cue", out, sizeof(out)) && strcmp(out, "C:\\games\\disc.bin") == 0);
    CHECK(dpCueBin("FILE \"/abs/d.bin\" BINARY\n", "/games/x.cue", out, sizeof(out)) && strcmp(out, "/abs/d.bin") == 0);
    CHECK(dpCueBin("FILE \"d.bin\" BINARY\n", "x.cue", out, sizeof(out)) && strcmp(out, "d.bin") == 0);
    CHECK(!dpCueBin("TRACK 01 MODE2/2352\n", "x.cue", out, sizeof(out)));
    CHECK(!dpCueBin("FILE \"unterminated\n", "x.cue", out, sizeof(out)));
    CHECK(!dpCueBin("FILE \"d.bin\" BINARY\n", "/long/x.cue", out, 5)); /* too small */

    /* directory records and SYSTEM.CNF */
    dirBlk[0] = 34; dirBlk[32] = 1;
    dirBlk[34] = 44; dirBlk[34 + 2] = 0x12; dirBlk[34 + 3] = 0x34; dirBlk[34 + 10] = 0x21; dirBlk[34 + 32] = 12;
    memcpy(dirBlk + 34 + 33, "system.cnf;1", 12);
    CHECK(dpFindFile(dirBlk, sizeof(dirBlk), "SYSTEM.CNF", &lba, &size) && lba == 0x3412 && size == 0x21);
    CHECK(!dpFindFile(dirBlk, sizeof(dirBlk), "SYSTEM.CN", &lba, &size));
    CHECK(!dpFindFile(dirBlk, sizeof(dirBlk), "OTHER.DAT", &lba, &size));
    CHECK(dpBootId("BOOT = cdrom:\\SLES_039.36;1\r\nTCB = 4\r\n", 36, out, sizeof(out)) && strcmp(out, "SLES_039.36") == 0);
    CHECK(dpBootId("TCB = 4\nboot=cdrom:SLUS_012.34;1\n", 33, out, sizeof(out)) && strcmp(out, "SLUS_012.34") == 0);
    CHECK(!dpBootId("TCB = 4\n", 8, out, sizeof(out)));
    CHECK(!dpBootId("BOOT2 = cdrom:\\PS2.ELF;1\n", 25, out, sizeof(out)));

    /* the player's language */
    CHECK(dpIsSpanish("es") && dpIsSpanish("es_MX.UTF-8") && dpIsSpanish("es-419") && dpIsSpanish("ES_ES"));
    CHECK(!dpIsSpanish("en_US") && !dpIsSpanish("est") && !dpIsSpanish("") && !dpIsSpanish(NULL) && !dpIsSpanish("e"));

    /* log names and the keep-last-10 plan */
    t.tm_year = 126; t.tm_mon = 9; t.tm_mday = 29; t.tm_hour = 7; t.tm_min = 5; t.tm_sec = 3;
    dpLogName(out, sizeof(out), &t);
    CHECK(strcmp(out, "dw3-20261029-070503.log") == 0 && dpLogIsName(out));
    CHECK(!dpLogIsName("dw3-20261029-070503.txt") && !dpLogIsName("settings.ini") && !dpLogIsName("dw3-2026102x-070503.log"));
    for (i = 0; i < 12; i++) {
        snprintf(store[i], sizeof(store[i]), "dw3-20261029-1200%02d.log", i);
        names[i] = store[i];
    }
    snprintf(store[12], sizeof(store[12]), "notes.txt");
    names[12] = store[12];
    n = dpLogPlan(names, 13, 10, drop);
    CHECK(n == 2 && drop[0] == 1 && drop[1] == 1 && drop[2] == 0 && drop[11] == 0 && drop[12] == 0);
    CHECK(dpLogPlan(names, 5, 10, drop) == 0);

    /* the user folder's choice (portable or not) is the path layer's: pathstest */

    /* images */
    snprintf(path, sizeof(path), "%s/w2_eu.bin", dir);
    CHECK(writeImage(path, "BOOT = cdrom:\\SLES_039.36;1", 0, 1));
    CHECK(dpCheckImage(path, bin, sizeof(bin), det, sizeof(det)) == DP_OK && strcmp(bin, path) == 0);
    {
        char cue[600], cueText[800];
        FILE *f;

        snprintf(cue, sizeof(cue), "%s/w2_eu.cue", dir);
        snprintf(cueText, sizeof(cueText), "FILE \"w2_eu.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
        f = fopen(cue, "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fputs(cueText, f);
            fclose(f);
        }
        CHECK(dpCheckImage(cue, bin, sizeof(bin), det, sizeof(det)) == DP_OK && strstr(bin, "w2_eu.bin") != NULL);
        remove(cue);
    }
    snprintf(path, sizeof(path), "%s/w2_us.bin", dir);
    CHECK(writeImage(path, "BOOT = cdrom:\\SLUS_012.34;1", 0, 1));
    CHECK(dpCheckImage(path, bin, sizeof(bin), det, sizeof(det)) == DP_OTHER_DISC && strcmp(det, "SLUS_012.34") == 0);
    CHECK(strstr(dpMessage(DP_OTHER_DISC, 0, det, msg, sizeof(msg)), "SLUS_012.34") != NULL);
    CHECK(strstr(dpMessage(DP_OTHER_DISC, 1, det, msg, sizeof(msg)), "SLUS_012.34") != NULL
          && strstr(msg, "no es Digimon") != NULL);
    /* Android: no file dialog, the message names the folder to copy the image into */
    CHECK(strstr(dpNoDiscMessage(1, "/storage/emulated/0/Android/data/io.github.dw3port/files/", msg, sizeof(msg)),
                 "Android/data/io.github.dw3port/files/") != NULL && strstr(msg, "reinicia") != NULL);
    CHECK(strstr(dpNoDiscMessage(0, "/sdcard/x/", msg, sizeof(msg)), "/sdcard/x/") != NULL && strstr(msg, "restart") != NULL);
    CHECK(dpNoDiscMessage(0, NULL, msg, sizeof(msg))[0] != '\0' && strstr(msg, "(null)") == NULL);
    CHECK(dpMessage(DP_CANT_OPEN, 1, NULL, msg, sizeof(msg))[0] != '\0' && dpMessage(DP_NOT_PS1, 0, NULL, msg, sizeof(msg))[0] != '\0');
    remove(path);
    snprintf(path, sizeof(path), "%s/w2_iso.bin", dir);
    CHECK(writeImage(path, "BOOT = cdrom:\\SLES_039.36;1", 1, 1));
    CHECK(dpCheckImage(path, bin, sizeof(bin), det, sizeof(det)) == DP_COOKED_ISO);
    remove(path);
    snprintf(path, sizeof(path), "%s/w2_junk.bin", dir);
    CHECK(writeImage(path, "", 0, 0));
    CHECK(dpCheckImage(path, bin, sizeof(bin), det, sizeof(det)) == DP_NOT_PS1);
    remove(path);
    snprintf(path, sizeof(path), "%s/w2_missing.bin", dir);
    CHECK(dpCheckImage(path, bin, sizeof(bin), det, sizeof(det)) == DP_CANT_OPEN);
    snprintf(path, sizeof(path), "%s/w2_eu.bin", dir);
    remove(path);


    /* W2b: portable paths and the auto-detect order */
    CHECK(dpRelativeTo("/media/usb/DW3/", "/media/usb/DW3/game/x.cue", out, sizeof(out)) && strcmp(out, "game/x.cue") == 0);
    CHECK(dpRelativeTo("E:\\DW3\\", "e:\\dw3\\game\\x.cue", out, sizeof(out)) && strcmp(out, "game/x.cue") == 0);
    CHECK(dpRelativeTo("/app", "/app/x.bin", out, sizeof(out)) && strcmp(out, "x.bin") == 0);
    CHECK(!dpRelativeTo("/app/", "/app2/x.bin", out, sizeof(out)) && !dpRelativeTo("/app/", "/other/x.bin", out, sizeof(out)));
    CHECK(!dpRelativeTo("/app/", "/app/", out, sizeof(out)) && !dpRelativeTo("", "/app/x", out, sizeof(out)));
    CHECK(dpResolveStored("/media/usb/DW3/", "game/x.cue", out, sizeof(out)) && strcmp(out, "/media/usb/DW3/game/x.cue") == 0);
    CHECK(dpResolveStored("F:\\DW3", "x.cue", out, sizeof(out)) && strcmp(out, "F:\\DW3/x.cue") == 0);
    CHECK(dpResolveStored("/app/", "/abs/x.bin", out, sizeof(out)) && strcmp(out, "/abs/x.bin") == 0);
    CHECK(dpResolveStored("/app/", "D:/x.bin", out, sizeof(out)) && strcmp(out, "D:/x.bin") == 0);
    CHECK(!dpResolveStored("/app/", "", out, sizeof(out)));
    CHECK(dpCandidateRank("A.CUE") == 0 && dpCandidateRank("a.bin") == 1 && dpCandidateRank("a.Iso") == 2 && dpCandidateRank("a.txt") == -1);
    CHECK(dpCandidateCmp(0, 1, "z.cue", 1, 0, "a.bin") < 0); /* a .cue in a subfolder before a .bin beside the exe */
    CHECK(dpCandidateCmp(1, 0, "z.bin", 1, 1, "a.bin") < 0 && dpCandidateCmp(1, 0, "a.bin", 1, 0, "b.bin") < 0);
    {
        char sub[700], picked[1100], bin2[1100];
        FILE *f;

        snprintf(sub, sizeof(sub), "%s/w2b", dir);
        mkdir(sub, 0755);
        snprintf(path, sizeof(path), "%s/w2b/game", dir);
        mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/w2b/readme.bin", dir); /* junk first: skipped */
        CHECK(writeImage(path, "", 0, 0));
        snprintf(path, sizeof(path), "%s/w2b/game/dw3.bin", dir);
        CHECK(writeImage(path, "BOOT = cdrom:\\SLES_039.36;1", 0, 1));
        CHECK(dpAutoDetect(sub, picked, sizeof(picked), bin2, sizeof(bin2)) && strstr(picked, "game/dw3.bin") != NULL);
        snprintf(path, sizeof(path), "%s/w2b/other.bin", dir); /* a US disc beside it: skipped */
        CHECK(writeImage(path, "BOOT = cdrom:\\SLUS_012.34;1", 0, 1));
        CHECK(dpAutoDetect(sub, picked, sizeof(picked), bin2, sizeof(bin2)) && strstr(picked, "game/dw3.bin") != NULL);
        snprintf(path, sizeof(path), "%s/w2b/game/dw3.bin", dir);
        remove(path);
        CHECK(!dpAutoDetect(sub, picked, sizeof(picked), bin2, sizeof(bin2)));
        snprintf(path, sizeof(path), "%s/w2b/other.bin", dir);
        remove(path);
        snprintf(path, sizeof(path), "%s/w2b/readme.bin", dir);
        remove(path);
        snprintf(path, sizeof(path), "%s/w2b/game", dir);
        rmdir(path);
        rmdir(sub);
        (void)f;
    }

    /* [disc] path: parsed, written back, and the player's pick wins over the file's older value */
    inputCfgDefaults(&cfg);
    CHECK(inputCfgParse("[disc]\npath = /games/My Disc/dw3.bin\n", &cfg, logSink) == 0);
    CHECK(strcmp(inputCfgDisc(), "/games/My Disc/dw3.bin") == 0);
    CHECK(inputCfgFormat(&cfg, text, sizeof(text)) > 0 && strstr(text, "[disc]\npath = /games/My Disc/dw3.bin\n") != NULL);
    CHECK(inputCfgParse("[disc]\nbogus = 1\n", &cfg, logSink) == 1);
    inputCfgRememberDisc("/new/place.bin");
    CHECK(inputCfgParse("[disc]\npath = /old.bin\n", &cfg, logSink) == 0 && strcmp(inputCfgDisc(), "/new/place.bin") == 0);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "build";

    testNames();
    testDefaults();
    testRoundTrip();
    testScope();
    testInvalid();
    testFile(dir);
    testCapture(dir);
    testReservedKeys();
    testScreenModel();
    testOptionsMenu();
    testSettingsFile(dir);
    testOptionsVideo();
    testOptionsLanguage();
    testOptionsAudio();
    testOptionsSystem();
    testRestartHandoff();
    testPrepareModding();
    testExportTypes();
    testOptionsMods(dir);
    testLongModsLines(dir);
    testPromptIcons();
    testTextIcons();
    testMenuFrame();
    testHoldRepeatSound();
    testTouchActions();
    testAutoText();
    testBattleSpeed();
    testStageBannerGate();
    testStateScopes();
    testFirstRun(dir);
    if (failures != 0) {
        fprintf(stderr, "inputtest: %d assertion(s) failed\n", failures);
        return 1;
    }
    printf("inputtest: all assertions held\n");
    return 0;
}
