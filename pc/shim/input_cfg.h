#ifndef PC_INPUT_CFG_H
#define PC_INPUT_CFG_H

/*
 * The port's input configuration (feature controls-config, C1/C2).
 *
 * PsyCross translates the keyboard and the SDL game controller into PS1 pad
 * bits through two public tables, g_cfg_keyboardMapping and
 * g_cfg_controllerMapping (PsyX_public.h). Nothing in PsyCross reads a file, so
 * this unit does:
 *
 *   C1  the controls of settings.ini: human-readable names (SDL scancode names for the keys,
 *       SDL game-controller names for the pad: "a", "leftshoulder",
 *       "lefttrigger", "-lefty"), loaded at boot over PsyCross's defaults and
 *       written with those defaults when the file is missing.
 *       gamecontrollerdb.txt is loaded here too, from absolute paths.
 *   C2  press-to-bind: start capturing for one PS1 button on one device, the
 *       next key-down / controller button-down / axis past the threshold is
 *       assigned and saved, Escape cancels. While it waits (and until what was
 *       pressed is released) the game's pad word is held neutral.
 *
 * The parse/format/name functions are pure (no window, no SDL_Init) so
 * pc/inputtest.c can test them. Capture events arrive through an SDL event
 * watch, which sees every event before PsyCross's own polling drains it; the
 * tests call inputCapEvent directly with synthetic events.
 */
struct _RECT16; /* PsyX_public.h names it without including libgpu */
#include <PsyX/PsyX_public.h>

/* The two devices a button can be bound on */
enum { INPUT_DEV_KEYBOARD = 0, INPUT_DEV_PAD = 1, INPUT_DEV_ALL = 2 };

/* The 16 rebindable PS1 buttons, in PsyXKeyboardMapping field order */
enum {
    INPUT_BTN_SQUARE, INPUT_BTN_CIRCLE, INPUT_BTN_TRIANGLE, INPUT_BTN_CROSS,
    INPUT_BTN_L1, INPUT_BTN_L2, INPUT_BTN_L3,
    INPUT_BTN_R1, INPUT_BTN_R2, INPUT_BTN_R3,
    INPUT_BTN_START, INPUT_BTN_SELECT,
    INPUT_BTN_LEFT, INPUT_BTN_RIGHT, INPUT_BTN_UP, INPUT_BTN_DOWN,
    INPUT_BTN_COUNT
};

/* Slots of the INI: the buttons, then (pad only) the four stick axes */
#define INPUT_SLOT_AXIS_LEFT_X  INPUT_BTN_COUNT
#define INPUT_SLOT_COUNT        (INPUT_BTN_COUNT + 4)

/*
 * The slots the player can see and rebind: only what the game's code reads
 * (d-pad, cross/circle/triangle/square, L1, R1, start; the left stick through
 * port.c's analog[2..3] -> d-pad). The screen of C3 lists exactly this array.
 * Select, L2/R2, L3/R3 and the right stick stay at PsyCross's defaults: they
 * are neither written to settings.ini nor capturable, though a hand-added line
 * for them is still read. The stick axes are INI-only (not press-to-bind).
 */
#define INPUT_EXPOSED_COUNT 13 /* 11 buttons + left stick x, y */
extern const int INPUT_EXPOSED[INPUT_EXPOSED_COUNT];
#define INPUT_EXPOSED_BUTTONS 11 /* the first 11 entries are buttons */

/* 1 when slot is in INPUT_EXPOSED (for the keyboard only the 11 buttons count) */
int inputCfgIsExposed(int device, int slot);

/* Returned by the name -> code functions for a name they do not know */
#define INPUT_CODE_INVALID      (-2)

typedef struct {
    PsyXKeyboardMapping kb;
    PsyXControllerMapping pad;
} InputCfg;

/* ---- pure functions (C1) ---- */

/* PsyCross's own defaults (PsyX_Sys_InitialiseInput) */
void inputCfgDefaults(InputCfg *cfg);

/* The INI key of a slot ("square", "dpad_left", "axis_left_x"), NULL if bad */
const char *inputCfgSlotName(int slot);

/* The code stored in a slot. NULL when the device/slot has none (kb axes). */
int *inputCfgSlot(InputCfg *cfg, int device, int slot);

/* Code <-> name. Name functions write at most cap bytes (NUL included) and
   return the length, 0 on failure. Code functions return INPUT_CODE_INVALID
   for an unknown name. "NONE" is the unbound key / pad entry. */
int inputCfgKeyName(int code, char *buf, int cap);
int inputCfgKeyCode(const char *name);
int inputCfgPadName(int code, char *buf, int cap);
int inputCfgPadCode(const char *name);

/* The settings file is shared with the options menu's own rows (O1): sections
   [video] [display] [language] [audio] belong to `ext`, which the menu installs
   (controls_screen.c). format appends those sections to the file text (returns
   the length written, or -1 when cap is too small); owns says whether a section
   name (lower case) is its own; entry handles one `key = value` of such a section
   (key lower case) and returns NULL or the reason it was rejected. */
typedef struct InputCfgExt {
    int (*format)(char *buf, int cap);
    int (*owns)(const char *section);
    const char *(*entry)(const char *section, const char *key, const char *val);
} InputCfgExt;
void inputCfgSetExt(const InputCfgExt *ext);

/* Asks for the settings file to be written from the main thread's next
   inputCapFrame (a menu row changed) */
void inputCfgRequestSave(void);

/* [debug] log and channels of settings.ini (the Log row of the System tab; port_log.h): the level is
   PLOG_OFF, PLOG_MODS or PLOG_TRACE. The file's values are loaded with the file (and handed to the port
   log); the setter asks for a save. The old key `mod_log = on|off` still reads (on = mods) and is
   written back as `log`. The environment overrides both (see port_log.h). */
int inputCfgLogLevel(void);
const char *inputCfgLogChannels(void);
void inputCfgSetLogLevel(int level);

/* A command line switch ("--export-all"): removes every argv[i] equal to `flag` (argv[0] is never taken), keeps the order of the rest,
   lowers *argc and null-terminates the vector. 1 when the flag was there, else 0. */
int inputCfgTakeFlag(int *argc, char **argv, const char *flag);

/* Whole file <-> structs. Format writes the INI text (returns its length, or
   -1 when cap is too small). Parse overwrites only the entries that parse;
   every rejected line is reported through log (may be NULL) and counted. */
typedef void (*InputCfgLog)(const char *msg);
int inputCfgFormat(const InputCfg *cfg, char *buf, int cap);
int inputCfgParse(const char *text, InputCfg *cfg, InputCfgLog log);

/* ---- files and boot (C1) ---- */

/* The settings file: DW3_SETTINGS, else <user folder>/settings.ini (shim/port_paths.h). 0 on failure */
int inputCfgResolvePath(char *path, int cap);

/* W2: the disc image remembered under [disc] path (the settings file's, read before the window exists) */
int inputCfgReadDisc(char *out, int cap);
/* The [data] export_models value ("all" or fighter numbers), "" -> 0: read at start, before the window opens */
int inputCfgReadExportModels(char *out, int cap);
/* The [data] export_videos value ("all" or movie names), "" -> 0: read at start, before the window opens (content-export E2a) */
int inputCfgReadExportVideos(char *out, int cap);
/* The [data] export_audio value ("all" or sound bank names), "" -> 0: read at start, before the window opens (content-export E3a) */
int inputCfgReadExportAudio(char *out, int cap);
/* The [data] export_stages value ("all" or stage names like S233), "" -> 0: read at start, before the window opens (content-export E6b) */
int inputCfgReadExportStages(char *out, int cap);
/* The [data] export_raw value ("all", "off" or kinds like stage-map), "" -> 0: read at start, before the window opens; the caller treats an absent
   line as "all" (content-export E7) */
int inputCfgReadExportRaw(char *out, int cap);
/* The [mods] lines as the one text modsInit takes: the order, then each [mods] disabled name as "-name" ("" -> 0, also when it
   would not fit `cap`); inputCfgReadModsOrder is this after loading the file */
int inputCfgModsText(char *out, int cap);
/* The same, with no length limit: a malloc'ed text (the caller frees it), NULL when there are no [mods] lines. Boot uses these. */
char *inputCfgModsTextAlloc(void);
char *inputCfgReadModsOrderAlloc(void);
/* The loaded [mods] order and disabled lines ("" when absent) */
const char *inputCfgModsOrderText(void);
const char *inputCfgModsDisabledText(void);
/* The in-game Mods tab's choice: both lines (NULL or "" removes one), saved with the next settings write. 0 ok, -1 when memory ran out
   (nothing changed); the lines have no length limit (up to the 4 MB the settings file may have) */
int inputCfgSetMods(const char *order, const char *disabled);
const char *inputCfgDisc(void);
void inputCfgRememberDisc(const char *path);
int inputCfgLoadFile(const char *path, InputCfg *cfg, InputCfgLog log); /* -1: no file */
int inputCfgSaveFile(const char *path, const InputCfg *cfg);            /* 0 ok */

/* Copy to/from the live PsyCross tables (their `id` fields are kept) */
void inputCfgApply(const InputCfg *cfg);
void inputCfgCurrent(InputCfg *cfg);

/* Boot: after PsyX_Initialise. Loads or writes settings.ini, applies it, loads
   gamecontrollerdb.txt, installs the capture watch. Idempotent. */
void inputCfgInit(void);

/* The path the capture saves to ("" = do not save); inputCfgInit sets it */
void inputCfgSetPath(const char *path);

/* ---- press-to-bind (C2) ---- */

enum {
    INPUT_CAP_IDLE = 0,
    INPUT_CAP_WAITING,   /* a press is awaited */
    INPUT_CAP_BOUND,     /* the last capture assigned a binding */
    INPUT_CAP_CANCELLED  /* the last capture ended with Escape */
};

/* Start capturing PS1 button `button` (INPUT_BTN_*) on `device`
   (KEYBOARD or PAD). 0 on success, -1 on bad arguments. */
int inputCapBegin(int device, int button);
void inputCapCancel(void);
int inputCapState(void);        /* INPUT_CAP_* */
int inputCapDevice(void);

/* Keys the capture refuses to bind: PsyCross's fixed hotkeys (Backspace lifts
   the vsync wait, F3-F12 switch filters / the active keyboard / capture) and
   F1, which opens the controls screen. A press of one keeps the capture
   waiting and is reported by inputCapRefused (its scancode; 0 = none since
   inputCapBegin). */
int inputCfgKeyReserved(int scancode);
/* The pad button (SDL_CONTROLLER_BUTTON_*) the options menu owns: Select (Back). The capture
   refuses it, as it does the reserved keys. */
int inputCfgPadButtonReserved(int button);
int inputCapRefused(void);
int inputCapButton(void);

/* One SDL event (key-down, controller button-down, axis past 16384). Returns 1
   when it ended the capture. Safe from SDL's event-watch thread. */
union SDL_Event;
int inputCapEvent(const union SDL_Event *ev);

/* Once per frame, main thread: saves a binding the watch just assigned. */
void inputCapFrame(void);

/* Back to PsyCross's defaults for a device (INPUT_DEV_ALL for both) and saved */
void inputCfgReset(int device);

/* Hold the game's pad word neutral while a capture waits and after it, until
   the pressed input is released. `buttons` is the raw active-low word right
   after PsyX_UpdateInput; `analog` the 4 stick bytes. Returns 1 if it
   overwrote them. */
int inputCapFilterPad(unsigned short *buttons, unsigned char *analog);

#endif
