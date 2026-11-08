/*
 * settings.ini controls and press-to-bind (feature controls-config, C1/C2). See
 * input_cfg.h for the design; pc/inputtest.c tests it without a window.
 *
 * The INI looks like:
 *
 *     [keyboard]            SDL scancode names (SDL_GetScancodeName), "NONE"
 *     cross = C             for an unbound key
 *     ...
 *     [controller]          SDL game-controller names; "-" before an axis name
 *     cross = a             binds its negative direction; "NONE" for unbound
 *     l2 = lefttrigger
 *     axis_left_x = leftx
 *
 * Name <-> code reuses PsyCross's own lookups (PsyX_Lookup*Mapping), so the
 * file accepts exactly what the library documents.
 */
#include <SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "input_cfg.h"
#include "port_log.h"
#include "port_paths.h"

#define CAP_AXIS_THRESHOLD 16384 /* the pad word's own: GetControllerButtonState() > 16384 */
#define LINE_MAX_LEN 255
#define MODS_LINE_MAX_LEN 1048575 /* a [mods] line holds the names of every mod */
#define SETTINGS_FILE_MAX (4 * 1024 * 1024) /* a settings file past this is not read at all (said in the log) */

/* W2: the remembered disc image ([disc] path); pinned = the player just picked it and the file's older value must not win */
static char discPath[1024];
static int discPinned;
static int logLevelFile; /* [debug] log (the Log row of the System tab): PLOG_OFF, PLOG_MODS or PLOG_TRACE, kept so a save writes it back */
static char logChannelsFile[256]; /* [debug] channels, kept the same way */
/* [mods] order = a, b (ADR-005) and [mods] disabled = a, b (mods the player turned off), kept so a save writes them back. Heap strings
   (NULL = absent), with no length limit but the settings file's own 4 MB cap: 256 mods take a few KB. */
static char *modsOrder;
static char *modsDisabled;

/* Stores a [mods] value (heap; "" and NULL leave nothing). A failed allocation drops the value and says so. */
static int setModsText(char **dst, const char *val, const char *what) {
    char *copy = NULL;

    if (val != NULL && val[0] != '\0') {
        copy = strdup(val);
        if (copy == NULL) {
            plogLine("settings", "[mods] %s: out of memory, the line is ignored\n", what);
            val = NULL;
        }
    }
    free(*dst);
    *dst = copy;
    return val == NULL || val[0] == '\0' ? 0 : 1;
}

static const char *modsOrderStr(void) { return modsOrder != NULL ? modsOrder : ""; }
static const char *modsDisabledStr(void) { return modsDisabled != NULL ? modsDisabled : ""; }
/* The [data] export_* lines (one per exporter, B3): the value kept so a save writes it back, and the comment the exporter registered
   (inputCfgRegisterExport) that goes above it. A row is made by the first of the two to name its key. */
enum { EXPORT_ROWS = 16 };
static struct {
    char key[32];
    const char *note;
    int order; /* the position its exporter registered at (a save writes the rows in this order); -1: no exporter named it */
    char value[256];
} exportRows[EXPORT_ROWS];
static int exportRowCount, exportRegistered;

static int exportRowFind(const char *key) {
    int i;

    for (i = 0; i < exportRowCount; i++) {
        if (strcmp(exportRows[i].key, key) == 0) {
            return i;
        }
    }
    if (exportRowCount >= EXPORT_ROWS || strlen(key) >= sizeof(exportRows[0].key)) {
        return -1;
    }
    snprintf(exportRows[exportRowCount].key, sizeof(exportRows[0].key), "%s", key);
    exportRows[exportRowCount].order = -1;
    return exportRowCount++;
}

void inputCfgRegisterExport(const char *key, const char *note) {
    int i = exportRowFind(key);

    if (i >= 0) {
        exportRows[i].note = note;
        if (exportRows[i].order < 0) {
            exportRows[i].order = exportRegistered++;
        }
    }
}

/* PsyCross (PsyX_main.cpp / PsyX_pad.cpp) */
int PsyX_LookupKeyboardMapping(const char *str, int default_value);
int PsyX_LookupGameControllerMapping(const char *str, int default_value);

/* ---- slot tables -------------------------------------------------------- */

static const char *const SLOT_NAMES[INPUT_SLOT_COUNT] = {
    "square", "circle", "triangle", "cross", "l1", "l2", "l3", "r1", "r2", "r3",
    "start", "select", "dpad_left", "dpad_right", "dpad_up", "dpad_down",
    "axis_left_x", "axis_left_y", "axis_right_x", "axis_right_y"
};

static const size_t KB_OFFSETS[INPUT_BTN_COUNT] = {
    offsetof(PsyXKeyboardMapping, kc_square), offsetof(PsyXKeyboardMapping, kc_circle),
    offsetof(PsyXKeyboardMapping, kc_triangle), offsetof(PsyXKeyboardMapping, kc_cross),
    offsetof(PsyXKeyboardMapping, kc_l1), offsetof(PsyXKeyboardMapping, kc_l2),
    offsetof(PsyXKeyboardMapping, kc_l3), offsetof(PsyXKeyboardMapping, kc_r1),
    offsetof(PsyXKeyboardMapping, kc_r2), offsetof(PsyXKeyboardMapping, kc_r3),
    offsetof(PsyXKeyboardMapping, kc_start), offsetof(PsyXKeyboardMapping, kc_select),
    offsetof(PsyXKeyboardMapping, kc_dpad_left), offsetof(PsyXKeyboardMapping, kc_dpad_right),
    offsetof(PsyXKeyboardMapping, kc_dpad_up), offsetof(PsyXKeyboardMapping, kc_dpad_down)
};

static const size_t PAD_OFFSETS[INPUT_SLOT_COUNT] = {
    offsetof(PsyXControllerMapping, gc_square), offsetof(PsyXControllerMapping, gc_circle),
    offsetof(PsyXControllerMapping, gc_triangle), offsetof(PsyXControllerMapping, gc_cross),
    offsetof(PsyXControllerMapping, gc_l1), offsetof(PsyXControllerMapping, gc_l2),
    offsetof(PsyXControllerMapping, gc_l3), offsetof(PsyXControllerMapping, gc_r1),
    offsetof(PsyXControllerMapping, gc_r2), offsetof(PsyXControllerMapping, gc_r3),
    offsetof(PsyXControllerMapping, gc_start), offsetof(PsyXControllerMapping, gc_select),
    offsetof(PsyXControllerMapping, gc_dpad_left), offsetof(PsyXControllerMapping, gc_dpad_right),
    offsetof(PsyXControllerMapping, gc_dpad_up), offsetof(PsyXControllerMapping, gc_dpad_down),
    offsetof(PsyXControllerMapping, gc_axis_left_x), offsetof(PsyXControllerMapping, gc_axis_left_y),
    offsetof(PsyXControllerMapping, gc_axis_right_x), offsetof(PsyXControllerMapping, gc_axis_right_y)
};

const int INPUT_EXPOSED[INPUT_EXPOSED_COUNT] = {
    INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT,
    INPUT_BTN_CROSS, INPUT_BTN_CIRCLE, INPUT_BTN_TRIANGLE, INPUT_BTN_SQUARE,
    INPUT_BTN_L1, INPUT_BTN_R1, INPUT_BTN_START,
    INPUT_SLOT_AXIS_LEFT_X, INPUT_SLOT_AXIS_LEFT_X + 1
};

int inputCfgIsExposed(int device, int slot) {
    int i;

    for (i = 0; i < INPUT_EXPOSED_COUNT; i++) {
        if (INPUT_EXPOSED[i] == slot) {
            return device == INPUT_DEV_PAD || i < INPUT_EXPOSED_BUTTONS;
        }
    }
    return 0;
}

static int *slotPtr(PsyXKeyboardMapping *kb, PsyXControllerMapping *pad, int device, int slot) {
    if (device == INPUT_DEV_KEYBOARD && slot >= 0 && slot < INPUT_BTN_COUNT) {
        return (int *)((char *)kb + KB_OFFSETS[slot]);
    }
    if (device == INPUT_DEV_PAD && slot >= 0 && slot < INPUT_SLOT_COUNT) {
        return (int *)((char *)pad + PAD_OFFSETS[slot]);
    }
    return NULL;
}

int *inputCfgSlot(InputCfg *cfg, int device, int slot) {
    return slotPtr(&cfg->kb, &cfg->pad, device, slot);
}

const char *inputCfgSlotName(int slot) {
    return (slot >= 0 && slot < INPUT_SLOT_COUNT) ? SLOT_NAMES[slot] : NULL;
}

/* PsyX_Sys_InitialiseInput (PsyX_main.cpp), the values the port ships with */
void inputCfgDefaults(InputCfg *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->kb.kc_square = SDL_SCANCODE_X;
    cfg->kb.kc_circle = SDL_SCANCODE_V;
    cfg->kb.kc_triangle = SDL_SCANCODE_Z;
    cfg->kb.kc_cross = SDL_SCANCODE_C;
    cfg->kb.kc_l1 = SDL_SCANCODE_LSHIFT;
    cfg->kb.kc_l2 = SDL_SCANCODE_LCTRL;
    cfg->kb.kc_l3 = SDL_SCANCODE_LEFTBRACKET;
    cfg->kb.kc_r1 = SDL_SCANCODE_RSHIFT;
    cfg->kb.kc_r2 = SDL_SCANCODE_RCTRL;
    cfg->kb.kc_r3 = SDL_SCANCODE_RIGHTBRACKET;
    cfg->kb.kc_dpad_up = SDL_SCANCODE_UP;
    cfg->kb.kc_dpad_down = SDL_SCANCODE_DOWN;
    cfg->kb.kc_dpad_left = SDL_SCANCODE_LEFT;
    cfg->kb.kc_dpad_right = SDL_SCANCODE_RIGHT;
    cfg->kb.kc_select = SDL_SCANCODE_SPACE;
    cfg->kb.kc_start = SDL_SCANCODE_RETURN;

    cfg->pad.gc_square = SDL_CONTROLLER_BUTTON_X;
    cfg->pad.gc_circle = SDL_CONTROLLER_BUTTON_B;
    cfg->pad.gc_triangle = SDL_CONTROLLER_BUTTON_Y;
    cfg->pad.gc_cross = SDL_CONTROLLER_BUTTON_A;
    cfg->pad.gc_l1 = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    cfg->pad.gc_l2 = SDL_CONTROLLER_AXIS_TRIGGERLEFT | CONTROLLER_MAP_FLAG_AXIS;
    cfg->pad.gc_l3 = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    cfg->pad.gc_r1 = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    cfg->pad.gc_r2 = SDL_CONTROLLER_AXIS_TRIGGERRIGHT | CONTROLLER_MAP_FLAG_AXIS;
    cfg->pad.gc_r3 = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    cfg->pad.gc_dpad_up = SDL_CONTROLLER_BUTTON_DPAD_UP;
    cfg->pad.gc_dpad_down = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    cfg->pad.gc_dpad_left = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    cfg->pad.gc_dpad_right = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    cfg->pad.gc_select = SDL_CONTROLLER_BUTTON_BACK;
    cfg->pad.gc_start = SDL_CONTROLLER_BUTTON_START;
    cfg->pad.gc_axis_left_x = SDL_CONTROLLER_AXIS_LEFTX | CONTROLLER_MAP_FLAG_AXIS;
    cfg->pad.gc_axis_left_y = SDL_CONTROLLER_AXIS_LEFTY | CONTROLLER_MAP_FLAG_AXIS;
    cfg->pad.gc_axis_right_x = SDL_CONTROLLER_AXIS_RIGHTX | CONTROLLER_MAP_FLAG_AXIS;
    cfg->pad.gc_axis_right_y = SDL_CONTROLLER_AXIS_RIGHTY | CONTROLLER_MAP_FLAG_AXIS;
}

/* ---- names -------------------------------------------------------------- */

static int putName(const char *name, char *buf, int cap) {
    int n = (int)strlen(name);
    if (n == 0 || n + 1 > cap) {
        return 0;
    }
    memcpy(buf, name, (size_t)n + 1);
    return n;
}

int inputCfgKeyName(int code, char *buf, int cap) {
    if (code == 0) {
        return putName("NONE", buf, cap);
    }
    if (code < 0 || code >= SDL_NUM_SCANCODES) {
        return 0;
    }
    return putName(SDL_GetScancodeName((SDL_Scancode)code), buf, cap);
}

int inputCfgKeyCode(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return INPUT_CODE_INVALID;
    }
    return PsyX_LookupKeyboardMapping(name, INPUT_CODE_INVALID);
}

int inputCfgPadName(int code, char *buf, int cap) {
    const char *n;
    int len;

    if (code == SDL_CONTROLLER_BUTTON_INVALID) {
        return putName("NONE", buf, cap);
    }
    if (code & CONTROLLER_MAP_FLAG_AXIS) {
        int axis = code & ~(CONTROLLER_MAP_FLAG_AXIS | CONTROLLER_MAP_FLAG_INVERSE);
        int off = (code & CONTROLLER_MAP_FLAG_INVERSE) ? 1 : 0;

        if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) {
            return 0;
        }
        n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)axis);
        if (n == NULL || n[0] == '\0' || (int)strlen(n) + off + 1 > cap) {
            return 0;
        }
        if (off) {
            buf[0] = '-';
        }
        len = (int)strlen(n);
        memcpy(buf + off, n, (size_t)len + 1);
        return len + off;
    }
    if (code < 0 || code >= SDL_CONTROLLER_BUTTON_MAX) {
        return 0;
    }
    n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)code);
    return n != NULL ? putName(n, buf, cap) : 0;
}

int inputCfgPadCode(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return INPUT_CODE_INVALID;
    }
    return PsyX_LookupGameControllerMapping(name, INPUT_CODE_INVALID);
}

/* ---- INI text ----------------------------------------------------------- */

static const InputCfgExt *ext_;

void inputCfgSetExt(const InputCfgExt *ext) {
    ext_ = ext;
}

/* What each exposed slot does, for the comment above its line. Kept free of the
   names of the keys the file leaves out. */
static const char *slotDesc(int slot) {
    switch (slot) {
    case INPUT_BTN_SQUARE: return "Square";
    case INPUT_BTN_CIRCLE: return "Circle (cancel)";
    case INPUT_BTN_TRIANGLE: return "Triangle";
    case INPUT_BTN_CROSS: return "Cross (confirm)";
    case INPUT_BTN_L1: return "L1";
    case INPUT_BTN_R1: return "R1";
    case INPUT_BTN_START: return "Start (field menu)";
    case INPUT_BTN_LEFT: return "D-pad left";
    case INPUT_BTN_RIGHT: return "D-pad right";
    case INPUT_BTN_UP: return "D-pad up";
    case INPUT_BTN_DOWN: return "D-pad down";
    case INPUT_SLOT_AXIS_LEFT_X: return "Left stick, horizontal";
    case INPUT_SLOT_AXIS_LEFT_X + 1: return "Left stick, vertical";
    default: return "Button";
    }
}

typedef struct {
    char *buf;
    int cap;
    int len;
    int overflow;
} Out;

static void outPrintf(Out *o, const char *fmt, const char *a, const char *b) {
    int n = snprintf(o->buf + o->len, (size_t)(o->cap - o->len), fmt, a, b);

    if (n < 0 || n >= o->cap - o->len) {
        o->overflow = 1;
        return;
    }
    o->len += n;
}

int inputCfgFormat(const InputCfg *cfg, char *buf, int cap) {
    Out o;
    InputCfg c = *cfg;
    int dev, s, i;

    if (cap <= 0) {
        return -1;
    }
    o.buf = buf;
    o.cap = cap;
    o.len = 0;
    o.overflow = 0;
    buf[0] = '\0';
    outPrintf(&o, "%s%s", "; Digimon World 3 PC port: settings.\n"
              "; Edit this file by hand if you like; the options menu (Select or F1 in game) writes it too.\n"
              "; Lines starting with ; or # are comments. Wrong or unknown values are reported in the\n"
              "; log and ignored (the default stays). Delete this file to restore every default.\n"
              ";\n"
              "; Keyboard names are SDL scancode names (A, Left Shift, Return, F5, ...). Controller\n"
              "; names are SDL game-controller names: a b x y back start leftstick rightstick\n"
              "; leftshoulder rightshoulder dpup dpdown dpleft dpright, and the axes leftx lefty\n"
              "; lefttrigger righttrigger (a leading '-' binds the negative direction).\n"
              "; NONE leaves a button unbound. Only the buttons the game reads are listed.\n", "");
    for (dev = INPUT_DEV_KEYBOARD; dev <= INPUT_DEV_PAD; dev++) {
        outPrintf(&o, "\n[%s]\n%s", dev == INPUT_DEV_KEYBOARD ? "controls.keyboard" : "controls.controller", "");
        for (i = 0; i < INPUT_EXPOSED_COUNT; i++) {
            char name[64], note[160];
            s = INPUT_EXPOSED[i];
            if (!inputCfgIsExposed(dev, s)) {
                continue;
            }
            int code = *inputCfgSlot(&c, dev, s);
            int n = dev == INPUT_DEV_KEYBOARD ? inputCfgKeyName(code, name, sizeof(name))
                                              : inputCfgPadName(code, name, sizeof(name));

            if (n == 0) {
                /* a code with no name (never produced by the port): unbound */
                snprintf(name, sizeof(name), "NONE");
            }
            snprintf(note, sizeof(note), dev == INPUT_DEV_KEYBOARD ? "; %s: a key name or NONE.\n%%s = %%s\n"
                     : (s >= INPUT_SLOT_AXIS_LEFT_X ? "; %s: a stick axis (leftx, lefty, -leftx, ...) or NONE.\n%%s = %%s\n"
                                                    : "; %s: a controller button or NONE.\n%%s = %%s\n"),
                     slotDesc(s));
            outPrintf(&o, note, SLOT_NAMES[s], name);
        }
    }
    if (ext_ != NULL && ext_->format != NULL && !o.overflow) {
        int n = ext_->format(o.buf + o.len, o.cap - o.len);

        if (n < 0) {
            o.overflow = 1;
        } else {
            o.len += n;
        }
    }
    if (!o.overflow) {
        outPrintf(&o, "\n[debug]\n; The port log (lines [channel] message in the session log). off: only the startup summaries and the problems.\n"
                  "; mods: also what the active mods change while you play. trace: also the technical detail (every file read...).\n"
                  "log = %s%s\n", plogLevelName(logLevelFile), "");
        outPrintf(&o, "; Optional: only these channels (tables, files, images, stage...), separated by commas. Empty: all of them.\n"
                  "channels = %s%s\n", logChannelsFile, "");
    }
    if ((modsOrderStr()[0] != '\0' || modsDisabledStr()[0] != '\0') && !o.overflow) {
        outPrintf(&o, "\n; Mods (folders in the mods/ folder next to this file). The Mods tab of the options menu writes these lines;\n"
                  "; changes apply the next time the game starts.\n[mods]\n", "", "");
        if (modsOrderStr()[0] != '\0') {
            outPrintf(&o, "; order: only the mods listed here are on, the last one wins. Without this line every mod is on, in alphabetical order.\n"
                      "order = %s%s\n", modsOrderStr(), "");
        }
        if (modsDisabledStr()[0] != '\0') {
            outPrintf(&o, "; disabled: mods turned off, even when listed in order. Mods not listed here are on.\n"
                      "disabled = %s%s\n", modsDisabledStr(), "");
        }
    }
    {
        int i, pass, any = 0;

        for (pass = 0; pass <= exportRegistered && !o.overflow; pass++) { /* registered rows by position, then the others */
            for (i = 0; i < exportRowCount && !o.overflow; i++) {
                if (exportRows[i].value[0] == '\0' || (pass < exportRegistered ? exportRows[i].order != pass : exportRows[i].order >= 0)) {
                    continue;
                }
                outPrintf(&o, "\n%s%s", exportRows[i].note != NULL ? exportRows[i].note : "", any ? "" : "[data]\n");
                outPrintf(&o, "%s = %s\n", exportRows[i].key, exportRows[i].value);
                any = 1;
            }
        }
    }
    if (discPath[0] != '\0' && !o.overflow) {
        outPrintf(&o, "\n; The disc image the game starts from (set by the first-run file picker).\n[disc]\npath = %s%s\n", discPath, "");
    }
    return o.overflow ? -1 : o.len;
}

static int slotByName(const char *name) {
    int s;

    for (s = 0; s < INPUT_SLOT_COUNT; s++) {
        if (strcmp(name, SLOT_NAMES[s]) == 0) {
            return s;
        }
    }
    return -1;
}

static char *trim(char *s) {
    char *e;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        e--;
    }
    *e = '\0';
    return s;
}

static void lower(char *s) {
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z') {
            *s = (char)(*s + 32);
        }
    }
}

/* The current section: dev is -1 (none or unknown), INPUT_DEV_KEYBOARD / INPUT_DEV_PAD,
   or INPUT_SECTION_EXT for a section the menu's rows own (its name in `name`) */
#define INPUT_SECTION_EXT 2
#define INPUT_SECTION_DISC 3
#define INPUT_SECTION_MODS 4
#define INPUT_SECTION_DEBUG 5
#define INPUT_SECTION_DATA 6
typedef struct {
    int dev;
    char name[32];
} Section;

/* Returns NULL when the line is fine, else the reason. */
static const char *parseLine(char *line, Section *section, InputCfg *cfg) {
    char *eq, *key, *val;
    int slot, code, *dst;

    line = trim(line);
    if (*line == '\0' || *line == ';' || *line == '#') {
        return NULL;
    }
    if (*line == '[') {
        char *end = strchr(line, ']');

        section->dev = -1;
        if (end == NULL) {
            return "malformed section";
        }
        *end = '\0';
        line = trim(line + 1);
        lower(line);
        if (strcmp(line, "keyboard") == 0 || strcmp(line, "controls.keyboard") == 0) {
            section->dev = INPUT_DEV_KEYBOARD;
        } else if (strcmp(line, "controller") == 0 || strcmp(line, "controls.controller") == 0) {
            section->dev = INPUT_DEV_PAD;
        } else if (strcmp(line, "disc") == 0) {
            section->dev = INPUT_SECTION_DISC;
        } else if (strcmp(line, "mods") == 0) {
            section->dev = INPUT_SECTION_MODS;
        } else if (strcmp(line, "debug") == 0) {
            section->dev = INPUT_SECTION_DEBUG;
        } else if (strcmp(line, "data") == 0) {
            section->dev = INPUT_SECTION_DATA;
        } else if (ext_ != NULL && ext_->owns != NULL && strlen(line) < sizeof(section->name) && ext_->owns(line)) {
            section->dev = INPUT_SECTION_EXT;
            snprintf(section->name, sizeof(section->name), "%s", line);
        } else {
            return "unknown section";
        }
        return NULL;
    }
    /* The first '=' splits; a key's own name may be '=' (the Equals key) */
    eq = strchr(line, '=');
    if (eq == NULL) {
        return "no '='";
    }
    *eq = '\0';
    key = trim(line);
    val = trim(eq + 1);
    if (section->dev < 0) {
        return "entry outside a known section";
    }
    lower(key);
    if (section->dev == INPUT_SECTION_DISC) {
        if (strcmp(key, "path") != 0) {
            return "unknown key";
        }
        if (!discPinned) {
            snprintf(discPath, sizeof(discPath), "%s", val);
        }
        return NULL;
    }
    if (section->dev == INPUT_SECTION_MODS) {
        if (strcmp(key, "order") == 0) {
            setModsText(&modsOrder, val, "order");
            return NULL;
        }
        if (strcmp(key, "disabled") == 0) {
            setModsText(&modsDisabled, val, "disabled");
            return NULL;
        }
        return "unknown key";
    }
    if (section->dev == INPUT_SECTION_DATA) {
        if (strncmp(key, "export_", 7) == 0) {
            int i = exportRowFind(key);

            if (i < 0) {
                return "too many export keys";
            }
            snprintf(exportRows[i].value, sizeof(exportRows[0].value), "%s", val);
            return NULL;
        }
        return "unknown key";
    }
    if (section->dev == INPUT_SECTION_DEBUG) {
        if (strcmp(key, "channels") == 0) {
            snprintf(logChannelsFile, sizeof(logChannelsFile), "%s", val);
            plogSetSettings(logLevelFile, logChannelsFile);
            return NULL;
        }
        if (strcmp(key, "log") == 0) {
            int level = plogParseLevel(val);

            if (level < 0 || strcmp(val, "1") == 0 || strcmp(val, "0") == 0) {
                return "not off, mods or trace";
            }
            logLevelFile = level;
        } else if (strcmp(key, "mod_log") == 0) { /* the key before the Log row: on is mods */
            int level;

            lower(val);
            level = plogParseLevel(val);
            if (level < 0 || level > PLOG_MODS) {
                return "not on or off";
            }
            logLevelFile = level;
        } else {
            return "unknown key";
        }
        plogSetSettings(logLevelFile, logChannelsFile);
        return NULL;
    }
    if (section->dev == INPUT_SECTION_EXT) {
        return ext_->entry(section->name, key, val);
    }
    slot = slotByName(key);
    if (slot < 0) {
        return "unknown button";
    }
    dst = inputCfgSlot(cfg, section->dev, slot);
    if (dst == NULL) {
        return "button not available on this device";
    }
    if (section->dev == INPUT_DEV_KEYBOARD) {
        code = inputCfgKeyCode(val);
    } else {
        code = inputCfgPadCode(val);
        if (code != INPUT_CODE_INVALID && slot >= INPUT_SLOT_AXIS_LEFT_X && !(code & CONTROLLER_MAP_FLAG_AXIS)) {
            return "a stick needs an axis";
        }
    }
    if (code == INPUT_CODE_INVALID) {
        return "unknown name";
    }
    *dst = code;
    return NULL;
}

int inputCfgParse(const char *text, InputCfg *cfg, InputCfgLog log) {
    Section section = {-1, ""};
    int bad = 0, lineNo = 0;
    const char *p = text;

    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
        char line[LINE_MAX_LEN + 1];
        char *big = NULL;
        const char *why;
        char msg[200];

        lineNo++;
        if (len > LINE_MAX_LEN && section.dev == INPUT_SECTION_MODS && len <= MODS_LINE_MAX_LEN) {
            /* a [mods] line can list every mod: it is parsed from the heap, not refused */
            big = (char *)malloc(len + 1);
            if (big != NULL) {
                memcpy(big, p, len);
                big[len] = '\0';
                why = parseLine(big, &section, cfg);
                snprintf(line, sizeof(line), "%.40s...", p);
                free(big);
                big = NULL;
            } else {
                why = "out of memory";
                snprintf(line, sizeof(line), "%.40s...", p);
            }
        } else if (len > LINE_MAX_LEN) {
            why = "line too long";
            snprintf(line, sizeof(line), "%.40s...", p);
        } else {
            memcpy(line, p, len);
            line[len] = '\0';
            {
                char copy[LINE_MAX_LEN + 1];
                memcpy(copy, line, len + 1);
                why = parseLine(copy, &section, cfg);
            }
        }
        if (why != NULL) {
            bad++;
            if (log != NULL) {
                snprintf(msg, sizeof(msg), "line %d: %s: %.60s", lineNo, why, line);
                log(msg);
            }
        }
        p += len;
        if (*p == '\n') {
            p++;
        }
    }
    return bad;
}

/* ---- files -------------------------------------------------------------- */

const char *inputCfgDisc(void) {
    return discPath;
}

/* The remembered disc image of the settings file ("" -> 0): loads the file, which leaves it in discPath */
int inputCfgReadDisc(char *out, int cap) {
    char path[1024];
    InputCfg scratch;

    if (!inputCfgResolvePath(path, sizeof(path))) {
        return 0;
    }
    inputCfgDefaults(&scratch);
    discPath[0] = '\0';
    inputCfgLoadFile(path, &scratch, NULL);
    return discPath[0] != '\0' && snprintf(out, (size_t)cap, "%s", discPath) < cap;
}

int inputCfgLogLevel(void) {
    return logLevelFile;
}

const char *inputCfgLogChannels(void) {
    return logChannelsFile;
}

void inputCfgSetLogLevel(int level) {
    level = level < PLOG_OFF ? PLOG_OFF : level > PLOG_TRACE ? PLOG_TRACE : level;
    if (level != logLevelFile) {
        logLevelFile = level;
        plogSetSettings(logLevelFile, logChannelsFile);
        inputCfgRequestSave();
    }
}

/* The player's pick: the next save of settings.ini writes it (inputCfgInit saves when it differs) */
void inputCfgRememberDisc(const char *path) {
    if (path != NULL && strcmp(path, discPath) != 0) {
        snprintf(discPath, sizeof(discPath), "%s", path);
        discPinned = 1;
    }
}

int inputCfgResolvePath(char *path, int cap) {
    return portPath(PP_SETTINGS, path, cap);
}

/* The [mods] lines as one text for modsInit: the order, then every disabled mod as "-name" (NULL when there is nothing). Heap: the
   caller frees it; no length limit. */
char *inputCfgModsTextAlloc(void) {
    const char *order = modsOrderStr();
    size_t cap = strlen(order) + strlen(modsDisabledStr()) * 2 + 16, n = 0;
    char *tmp, *d, *copy;

    tmp = (char *)malloc(cap);
    copy = strdup(modsDisabledStr());
    if (tmp == NULL || copy == NULL) {
        free(tmp);
        free(copy);
        plogLine("settings", "[mods]: out of memory, the order and disabled lines are ignored\n");
        return NULL;
    }
    tmp[0] = '\0';
    if (order[0] != '\0') {
        n = (size_t)snprintf(tmp, cap, "%s", order);
    }
    for (d = copy;;) {
        char *comma = strchr(d, ','), *e, *name;

        if (comma != NULL) {
            *comma = '\0';
        }
        name = trim(d);
        e = name + strlen(name);
        while (e > name && (e[-1] == '\r' || e[-1] == '\n')) {
            *--e = '\0';
        }
        if (*name != '\0') {
            n += (size_t)snprintf(tmp + n, cap - n, "%s-%s", n > 0 ? ", " : "", name);
        }
        if (comma == NULL) {
            break;
        }
        d = comma + 1;
    }
    free(copy);
    if (tmp[0] == '\0') {
        free(tmp);
        return NULL;
    }
    return tmp;
}

/* The [mods] order line of the settings file, as a heap text (NULL when absent): loads the file like inputCfgReadDisc, before the
   rows are installed, so boot can fix the mod order before the text layer starts. The caller frees it. */
char *inputCfgReadModsOrderAlloc(void) {
    char path[1024];
    InputCfg scratch;

    setModsText(&modsOrder, NULL, "order");
    setModsText(&modsDisabled, NULL, "disabled");
    if (!inputCfgResolvePath(path, sizeof(path))) {
        return NULL;
    }
    inputCfgDefaults(&scratch);
    inputCfgLoadFile(path, &scratch, NULL);
    return inputCfgModsTextAlloc();
}

/* The same into a buffer of the caller's ("" -> 0, also when it does not fit `cap`) */
int inputCfgReadModsOrder(char *out, int cap) {
    char *t = inputCfgReadModsOrderAlloc();
    int ok = t != NULL && snprintf(out, (size_t)cap, "%s", t) < cap;

    free(t);
    return ok;
}

int inputCfgModsText(char *out, int cap) {
    char *t = inputCfgModsTextAlloc();
    int ok = t != NULL && snprintf(out, (size_t)cap, "%s", t) < cap;

    free(t);
    return ok;
}

const char *inputCfgModsOrderText(void) { return modsOrderStr(); }
const char *inputCfgModsDisabledText(void) { return modsDisabledStr(); }

/* The Mods tab's choice: the next save writes it. -1 only when memory ran out (nothing changes): no line is too long any more */
int inputCfgSetMods(const char *order, const char *disabled) {
    char *oldO = modsOrder, *oldD = modsDisabled;

    order = order != NULL ? order : "";
    disabled = disabled != NULL ? disabled : "";
    modsOrder = NULL;
    modsDisabled = NULL;
    if ((order[0] != '\0' && !setModsText(&modsOrder, order, "order")) ||
        (disabled[0] != '\0' && !setModsText(&modsDisabled, disabled, "disabled"))) {
        free(modsOrder);
        free(modsDisabled);
        modsOrder = oldO;
        modsDisabled = oldD;
        return -1;
    }
    free(oldO);
    free(oldD);
    inputCfgRequestSave();
    return 0;
}

/* One [data] export_* line of the settings file ("" or absent -> 0): which items the exporter writes at start */
int inputCfgReadExport(const char *key, char *out, int cap) {
    char path[1024];
    InputCfg scratch;
    int i = exportRowFind(key);

    if (i < 0 || !inputCfgResolvePath(path, sizeof(path))) {
        return 0;
    }
    inputCfgDefaults(&scratch);
    exportRows[i].value[0] = '\0';
    inputCfgLoadFile(path, &scratch, NULL);
    return exportRows[i].value[0] != '\0' && snprintf(out, (size_t)cap, "%s", exportRows[i].value) < cap;
}

int inputCfgLoadFile(const char *path, InputCfg *cfg, InputCfgLog log) {
    FILE *f = fopen(path, "rb");
    char *text;
    long size;

    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > SETTINGS_FILE_MAX) {
        fclose(f);
        plogLine("settings", "%s is larger than %d bytes: none of it is read\n", path, SETTINGS_FILE_MAX);
        if (log != NULL) {
            log("file missing or too large");
        }
        return 0;
    }
    text = (char *)malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(f);
        return -1;
    }
    size = (long)fread(text, 1, (size_t)size, f);
    text[size] = '\0';
    fclose(f);
    inputCfgParse(text, cfg, log);
    free(text);
    return 0;
}

int inputCfgSaveFile(const char *path, const InputCfg *cfg) {
    char *text;
    char tmp[1024];
    FILE *f;
    int cap = 16384 + (int)(strlen(modsOrderStr()) + strlen(modsDisabledStr())) + 64, n; /* the [mods] lines have no length limit */

    text = (char *)malloc((size_t)cap);
    if (text == NULL) {
        return -1;
    }
    n = inputCfgFormat(cfg, text, cap);
    if (n < 0 || snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        free(text);
        return -1;
    }
    f = fopen(tmp, "wb");
    if (f == NULL) {
        free(text);
        return -1;
    }
    if (fwrite(text, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        remove(tmp);
        free(text);
        return -1;
    }
    free(text);
    if (fclose(f) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
#ifdef _WIN32
        /* the C runtime's rename does not replace an existing file */
        remove(path);
        if (rename(tmp, path) == 0) {
            return 0;
        }
#endif
        remove(tmp);
        return -1;
    }
    return 0;
}

void inputCfgApply(const InputCfg *cfg) {
    int kbId = g_cfg_keyboardMapping.id, padId = g_cfg_controllerMapping.id;

    g_cfg_keyboardMapping = cfg->kb;
    g_cfg_controllerMapping = cfg->pad;
    g_cfg_keyboardMapping.id = kbId;
    g_cfg_controllerMapping.id = padId;
}

void inputCfgCurrent(InputCfg *cfg) {
    cfg->kb = g_cfg_keyboardMapping;
    cfg->pad = g_cfg_controllerMapping;
}

/* ---- capture state ------------------------------------------------------ */

static SDL_SpinLock capLock;
static char savePath[1024];
static int capState = INPUT_CAP_IDLE;
static int capDevice, capButton;
static int capDirty;  /* a binding was assigned and not yet saved */
static int capHold;   /* hold the game's pad neutral until all is released */
static int capRefused; /* scancode of the last reserved key refused (-1 - button for a pad button), 0 if none */

void inputCfgRequestSave(void) {
    SDL_AtomicLock(&capLock);
    capDirty = 1;
    SDL_AtomicUnlock(&capLock);
}

void inputCfgSetPath(const char *path) {
    snprintf(savePath, sizeof(savePath), "%s", path != NULL ? path : "");
}

int inputCapBegin(int device, int button) {
    if ((device != INPUT_DEV_KEYBOARD && device != INPUT_DEV_PAD) || button < 0 || button >= INPUT_BTN_COUNT
        || !inputCfgIsExposed(device, button)) {
        return -1;
    }
    SDL_AtomicLock(&capLock);
    capDevice = device;
    capButton = button;
    capRefused = 0;
    capState = INPUT_CAP_WAITING;
    SDL_AtomicUnlock(&capLock);
    return 0;
}

static void capFinish(int state) {
    capState = state;
    capHold = 1;
}

void inputCapCancel(void) {
    SDL_AtomicLock(&capLock);
    if (capState == INPUT_CAP_WAITING) {
        capFinish(INPUT_CAP_CANCELLED);
    }
    SDL_AtomicUnlock(&capLock);
}

int inputCfgKeyReserved(int scancode) {
    return scancode == SDL_SCANCODE_BACKSPACE || (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12);
}

int inputCfgPadButtonReserved(int button) {
    return button == SDL_CONTROLLER_BUTTON_BACK;
}

int inputCapRefused(void) {
    return capRefused;
}

int inputCapState(void) {
    return capState;
}

int inputCapDevice(void) {
    return capDevice;
}

int inputCapButton(void) {
    return capButton;
}

static void capAssign(int code) {
    int *dst = slotPtr(&g_cfg_keyboardMapping, &g_cfg_controllerMapping, capDevice, capButton);

    *dst = code;
    capDirty = 1;
    capFinish(INPUT_CAP_BOUND);
}

int inputCapEvent(const union SDL_Event *ev) {
    const SDL_Event *e = (const SDL_Event *)ev;
    int consumed = 0;

    SDL_AtomicLock(&capLock);
    if (capState == INPUT_CAP_WAITING) {
        switch (e->type) {
        case SDL_KEYDOWN:
            if (!e->key.repeat) {
                int sc = (int)e->key.keysym.scancode;

                if (sc == SDL_SCANCODE_ESCAPE) {
                    capFinish(INPUT_CAP_CANCELLED);
                    consumed = 1;
                } else if (capDevice == INPUT_DEV_KEYBOARD && inputCfgKeyReserved(sc)) {
                    /* PsyCross (and the controls screen) listen to these: the
                       capture keeps waiting for another key */
                    capRefused = sc;
                    consumed = 1;
                } else if (capDevice == INPUT_DEV_KEYBOARD && sc > 0 && sc < SDL_NUM_SCANCODES) {
                    capAssign(sc);
                    consumed = 1;
                }
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (capDevice == INPUT_DEV_PAD && inputCfgPadButtonReserved(e->cbutton.button)) {
                capRefused = -1 - (int)e->cbutton.button; /* negative: a pad button */
                consumed = 1;
            } else if (capDevice == INPUT_DEV_PAD && e->cbutton.button < SDL_CONTROLLER_BUTTON_MAX) {
                capAssign(e->cbutton.button);
                consumed = 1;
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (capDevice == INPUT_DEV_PAD && e->caxis.axis < SDL_CONTROLLER_AXIS_MAX
                && abs((int)e->caxis.value) > CAP_AXIS_THRESHOLD) {
                capAssign(e->caxis.axis | CONTROLLER_MAP_FLAG_AXIS
                          | (e->caxis.value < 0 ? CONTROLLER_MAP_FLAG_INVERSE : 0));
                consumed = 1;
            }
            break;
        default:
            break;
        }
    }
    SDL_AtomicUnlock(&capLock);
    return consumed;
}

static int saveCurrent(const char *path) {
    InputCfg cur;

    if (path[0] == '\0') {
        return 0;
    }
    inputCfgCurrent(&cur);
    if (inputCfgSaveFile(path, &cur) != 0) {
        fprintf(stderr, "controls: could not write %s\n", path);
        return -1;
    }
    return 0;
}

void inputCapFrame(void) {
    int dirty;
    char path[sizeof(savePath)];

    SDL_AtomicLock(&capLock);
    dirty = capDirty;
    capDirty = 0;
    memcpy(path, savePath, sizeof(path));
    SDL_AtomicUnlock(&capLock);
    if (dirty) {
        saveCurrent(path);
    }
}

void inputCfgReset(int device) {
    InputCfg d, cur;

    inputCfgDefaults(&d);
    inputCfgCurrent(&cur);
    if (device == INPUT_DEV_KEYBOARD || device == INPUT_DEV_ALL) {
        cur.kb = d.kb;
    }
    if (device == INPUT_DEV_PAD || device == INPUT_DEV_ALL) {
        cur.pad = d.pad;
    }
    SDL_AtomicLock(&capLock);
    inputCfgApply(&cur);
    capState = INPUT_CAP_IDLE;
    capDirty = 0;
    SDL_AtomicUnlock(&capLock);
    saveCurrent(savePath);
}

int inputCapFilterPad(unsigned short *buttons, unsigned char *analog) {
    int hold = 0;

    SDL_AtomicLock(&capLock);
    if (capState == INPUT_CAP_WAITING) {
        hold = 1;
    } else if (capHold) {
        if (*buttons != 0xFFFF) {
            hold = 1;
        } else {
            capHold = 0;
        }
    }
    SDL_AtomicUnlock(&capLock);
    if (hold) {
        *buttons = 0xFFFF;
        analog[0] = analog[1] = analog[2] = analog[3] = 127;
    }
    return hold;
}

/* ---- boot --------------------------------------------------------------- */

static int capWatch(void *userdata, SDL_Event *e) {
    (void)userdata;
    inputCapEvent((const union SDL_Event *)e);
    return 0;
}

static void logLine(const char *msg) {
    fprintf(stderr, "controls: %s\n", msg);
}

static void loadControllerDb(const char *dir) {
    char path[1100];
    FILE *f;
    int n;

    if (dir == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%sgamecontrollerdb.txt", dir);
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    fclose(f);
    n = SDL_GameControllerAddMappingsFromFile(path);
    fprintf(stderr, "controls: %d controller mapping(s) loaded from %s\n", n, path);
}

void inputCfgInit(void) {
    static int inited;
    InputCfg defs, cur;
    char path[1024];

    if (inited) {
        return;
    }
    inited = 1;

    inputCfgDefaults(&defs);
    inputCfgCurrent(&cur);
    if (memcmp(&defs.kb.kc_square, &cur.kb.kc_square, sizeof(int) * INPUT_BTN_COUNT) != 0) {
        fprintf(stderr, "controls: PsyCross's keyboard defaults differ from the port's table\n");
    }
    if (inputCfgResolvePath(path, sizeof(path))) {
        inputCfgSetPath(path);
        if (inputCfgLoadFile(path, &cur, logLine) == -1) {
            /* no file yet: write today's defaults so the player can edit them */
            if (saveCurrent(path) == 0) {
                fprintf(stderr, "controls: wrote the defaults to %s\n", path);
            }
        } else {
            inputCfgApply(&cur);
            fprintf(stderr, "controls: loaded %s\n", path);
        }
    } else {
        fprintf(stderr, "controls: no place for settings.ini, using the defaults\n");
    }

    loadControllerDb(portPathsExeDir());
    loadControllerDb(portPathsBase());
    if (discPinned && savePath[0] != '\0') {
        /* W2: the file existed with an older (or no) disc: write the new one now */
        saveCurrent(savePath);
    }

    SDL_AddEventWatch(capWatch, NULL);
}

int inputCfgTakeFlag(int *argc, char **argv, const char *flag) {
    int i, out = 1, found = 0;

    if (argc == NULL || argv == NULL || flag == NULL || flag[0] == '\0') {
        return 0;
    }
    for (i = 1; i < *argc; i++) {
        if (strcmp(argv[i], flag) == 0) {
            found = 1;
        } else {
            argv[out++] = argv[i];
        }
    }
    if (found) {
        argv[out] = NULL;
        *argc = out;
    }
    return found;
}
