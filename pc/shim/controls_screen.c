/*
 * The in-game options menu: model and input (feature controls-config, C3 and O1).
 * See controls_screen.h. No game and no drawing dependency: SDL's event types,
 * input_cfg.c and the PsyCross mapping tables only.
 */
#include "controls_screen.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS_STICK_ON  16000 /* a left-stick push counts as a d-pad press past this */
#define CS_STICK_OFF 8000  /* ... and is released under this */
#define CS_QUEUE     32

static const char *const BUTTON_LABELS[INPUT_BTN_COUNT] = {
    [INPUT_BTN_SQUARE] = "Square", [INPUT_BTN_CIRCLE] = "Circle", [INPUT_BTN_TRIANGLE] = "Triangle",
    [INPUT_BTN_CROSS] = "Cross", [INPUT_BTN_L1] = "L1", [INPUT_BTN_L2] = "L2", [INPUT_BTN_L3] = "L3",
    [INPUT_BTN_R1] = "R1", [INPUT_BTN_R2] = "R2", [INPUT_BTN_R3] = "R3", [INPUT_BTN_START] = "Start",
    [INPUT_BTN_SELECT] = "Select", [INPUT_BTN_LEFT] = "Left", [INPUT_BTN_RIGHT] = "Right",
    [INPUT_BTN_UP] = "Up", [INPUT_BTN_DOWN] = "Down",
};

static int open_, device_ = INPUT_DEV_KEYBOARD, tab_, cursors_[CS_TAB_COUNT], waitRow_ = -1, blocked_, releaseHold_;
static char message_[40];
static unsigned navEpoch_;

static const char *const TAB_NAMES[CS_TAB_COUNT] = {"Controls", "Video", "Display", "Language", "Audio", "Enhancements", "System", "Mods"};

static CsWidget widgets_[CS_TAB_COUNT][CS_MODS_ROWS];
static int scroll_[CS_TAB_COUNT]; /* first row drawn, kept so the window moves only when the cursor leaves it */
static int widgetCount_[CS_TAB_COUNT];
static CsWidget hidden_[CS_TAB_COUNT][CS_MAX_HIDDEN];
static int hiddenCount_[CS_TAB_COUNT];

/* File entries of the widget sections that no row claimed (yet): rows are added
   after the file is read */
#define CS_PENDING 64
static struct {
    int used;
    char section[24], key[40], val[64];
} pending_[CS_PENDING];

static SDL_SpinLock queueLock;
static int queue_[CS_QUEUE];
static int queueLen;

/* ---- pure model ---- */

static int tabValid(int tab) {
    return tab >= 0 && tab < CS_TAB_COUNT;
}

int csTabRowCount(int tab) {
    if (!tabValid(tab)) {
        return 0;
    }
    return tab == CS_TAB_CONTROLS ? INPUT_EXPOSED_BUTTONS + 2 : widgetCount_[tab];
}

int csTabWindowTop(int tab) {
    int n = csTabRowCount(tab), cur, top;

    if (tab != CS_TAB_MODS || n <= CS_MAX_WIDGETS) {
        return 0; /* only the Mods tab scrolls */
    }
    cur = cursors_[tab];
    top = scroll_[tab];
    if (cur < top) {
        top = cur;
    } else if (cur >= top + CS_MAX_WIDGETS) {
        top = cur - CS_MAX_WIDGETS + 1;
    }
    if (top > n - CS_MAX_WIDGETS) {
        top = n - CS_MAX_WIDGETS;
    }
    return scroll_[tab] = top < 0 ? 0 : top;
}

int csTabPlaceholder(int tab) {
    return tabValid(tab) && csTabRowCount(tab) == 0;
}

const char *csTabName(int tab) {
    return tabValid(tab) ? TAB_NAMES[tab] : "";
}

int csRowKind(int tab, int row) {
    if (!tabValid(tab) || row < 0 || row >= csTabRowCount(tab)) {
        return -1;
    }
    if (tab == CS_TAB_CONTROLS) {
        return row < INPUT_EXPOSED_BUTTONS ? CS_ROW_BINDING : CS_ROW_ACTION;
    }
    return widgets_[tab][row].kind;
}

int csRowSlot(int row) {
    return csRowKind(CS_TAB_CONTROLS, row) == CS_ROW_BINDING ? INPUT_EXPOSED[row] : -1;
}

int csWrap(int cursor, int delta, int count) {
    if (count <= 0) {
        return 0;
    }
    cursor = (cursor + delta) % count;
    return cursor < 0 ? cursor + count : cursor;
}

static int putText(const char *text, char *buf, int cap) {
    int n = (int)strlen(text);

    if (cap <= 0 || n >= cap) {
        if (cap > 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    memcpy(buf, text, (size_t)n + 1);
    return n;
}

/* A row with a live source takes its value from it, when that value fits the row */
static void widgetSync(CsWidget *w) {
    int v, lo, hi;

    if (w->read == NULL) {
        return;
    }
    v = w->read();
    lo = w->kind == CS_ROW_SLIDER ? w->min : 0;
    hi = w->kind == CS_ROW_TOGGLE ? 1 : (w->kind == CS_ROW_CYCLE ? w->count - 1 : (w->kind == CS_ROW_SLIDER ? w->max : -1));
    if (v >= lo && v <= hi) {
        w->value = v;
    }
}

static CsWidget *widgetAt(int tab, int row) {
    CsWidget *w = (tab != CS_TAB_CONTROLS && tabValid(tab) && row >= 0 && row < widgetCount_[tab]) ? &widgets_[tab][row] : NULL;

    if (w != NULL) {
        widgetSync(w);
    }
    return w;
}

int csRowLabel(int tab, int row, char *buf, int cap) {
    const CsWidget *w = widgetAt(tab, row);

    if (w != NULL) {
        return putText(w->label != NULL ? w->label : "", buf, cap);
    }
    switch (csRowKind(tab, row)) {
    case CS_ROW_BINDING:
        return putText(BUTTON_LABELS[csRowSlot(row)], buf, cap);
    case CS_ROW_ACTION:
        return putText(row == CS_CTL_ROW_RESET ? "Reset to defaults" : "Back", buf, cap);
    default:
        return putText("", buf, cap);
    }
}

int csRowDesc(int tab, int row, char *buf, int cap) {
    const CsWidget *w = widgetAt(tab, row);

    if (w != NULL) {
        return putText(w->desc != NULL ? w->desc : "", buf, cap);
    }
    switch (csRowKind(tab, row)) {
    case CS_ROW_BINDING:
        return putText("Press Confirm then the new button.", buf, cap);
    case CS_ROW_ACTION:
        return putText(row == CS_CTL_ROW_RESET ? "Restores the shown device's defaults." : "Closes the menu.", buf, cap);
    default:
        return putText("", buf, cap);
    }
}

int csRowValue(int tab, int device, int row, char *buf, int cap) {
    const CsWidget *w = widgetAt(tab, row);
    InputCfg cur;
    int *code;
    int n;

    if (w != NULL) {
        char num[16];

        switch (w->kind) {
        case CS_ROW_TOGGLE:
            return putText(w->value != 0 ? "On" : "Off", buf, cap);
        case CS_ROW_CYCLE:
            return putText(w->names != NULL && w->value >= 0 && w->value < w->count ? w->names[w->value] : "", buf, cap);
        case CS_ROW_SLIDER:
            snprintf(num, sizeof(num), "%d", w->value);
            return putText(num, buf, cap);
        case CS_ROW_ACTION:
            return putText(w->textId != NULL ? w->textId(w->id) : (w->text != NULL ? w->text() : ""), buf, cap);
        default:
            return putText("", buf, cap);
        }
    }
    if (csRowKind(tab, row) != CS_ROW_BINDING) {
        return putText("", buf, cap);
    }
    if (waitRow_ == row && device == device_) {
        return putText(device == INPUT_DEV_KEYBOARD ? "Press a key..." : "Press a button...", buf, cap);
    }
    inputCfgCurrent(&cur);
    code = inputCfgSlot(&cur, device, csRowSlot(row));
    if (code == NULL) {
        return putText("", buf, cap);
    }
    n = device == INPUT_DEV_KEYBOARD ? inputCfgKeyName(*code, buf, cap) : inputCfgPadName(*code, buf, cap);
    return n > 0 ? n : putText("NONE", buf, cap);
}

int csRowFraction(int tab, int row) {
    const CsWidget *w = widgetAt(tab, row);

    if (w == NULL || w->kind != CS_ROW_SLIDER || w->max <= w->min) {
        return -1;
    }
    return (w->value - w->min) * 1000 / (w->max - w->min);
}

/* ---- widgets ---- */

static int ieq(const char *a, const char *b);

/* The file value of a cycle's value i */
static const char *tokenOf(const CsWidget *w, int i) {
    return w->tokens != NULL ? w->tokens[i] : w->names[i];
}

static int ieq(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
    }
    return *a == *b;
}

/* Sets the widget from a file value, no apply, no save; returns NULL or the reason */
static const char *widgetParse(CsWidget *w, const char *val) {
    char *end;
    long n;
    int i;

    switch (w->kind) {
    case CS_ROW_TOGGLE:
        if (ieq(val, "on") || ieq(val, "true") || ieq(val, "yes") || strcmp(val, "1") == 0) {
            w->value = 1;
        } else if (ieq(val, "off") || ieq(val, "false") || ieq(val, "no") || strcmp(val, "0") == 0) {
            w->value = 0;
        } else {
            return "expected on or off";
        }
        return NULL;
    case CS_ROW_CYCLE:
        for (i = 0; i < w->count; i++) {
            if (w->names != NULL && ieq(val, tokenOf(w, i))) {
                w->value = i;
                return NULL;
            }
        }
        return "not one of the listed values";
    case CS_ROW_SLIDER:
        n = strtol(val, &end, 10);
        if (val[0] == '\0' || *end != '\0') {
            return "expected a number";
        }
        if (n < w->min || n > w->max) {
            return "number out of range";
        }
        w->value = (int)n;
        return NULL;
    default:
        return "not a setting";
    }
}

static CsWidget *widgetByKey(int tab, const char *key) {
    int i;

    for (i = 0; i < widgetCount_[tab]; i++) {
        if (widgets_[tab][i].key != NULL && strcmp(widgets_[tab][i].key, key) == 0) {
            return &widgets_[tab][i];
        }
    }
    for (i = 0; i < hiddenCount_[tab]; i++) {
        if (hidden_[tab][i].key != NULL && strcmp(hidden_[tab][i].key, key) == 0) {
            return &hidden_[tab][i];
        }
    }
    return NULL;
}

/* The settings file section a widget lives in: its own when it names one (a row moved to another tab
   keeps its old section), else the tab's name */
static const char *sectionOf(int tab, const CsWidget *w) {
    return w->section != NULL ? w->section : TAB_NAMES[tab];
}

static int sectionHas(const char *section, int tab, const CsWidget *w) {
    return ieq(sectionOf(tab, w), section);
}

/* The widget of that file section and key, on whatever tab it is shown */
static CsWidget *widgetBySection(const char *section, const char *key, int *tabOut) {
    int tab, i;

    for (tab = CS_TAB_VIDEO; tab < CS_TAB_COUNT; tab++) {
        for (i = 0; i < widgetCount_[tab] + hiddenCount_[tab]; i++) {
            CsWidget *w = i < widgetCount_[tab] ? &widgets_[tab][i] : &hidden_[tab][i - widgetCount_[tab]];

            if (w->key != NULL && strcmp(w->key, key) == 0 && sectionHas(section, tab, w)) {
                *tabOut = tab;
                return w;
            }
        }
    }
    return NULL;
}

int csWidgetValueByKey(int tab, const char *key) {
    const CsWidget *w = tabValid(tab) && tab != CS_TAB_CONTROLS && key != NULL ? widgetByKey(tab, key) : NULL;

    return w != NULL ? w->value : -1;
}

static int tabBySection(const char *section) {
    int tab;

    for (tab = CS_TAB_VIDEO; tab < CS_TAB_COUNT; tab++) {
        if (ieq(section, TAB_NAMES[tab])) {
            return tab;
        }
    }
    return -1;
}

/* A retired value of a cycle row: say so, keep the default and have the file rewritten */
static int widgetObsolete(int tab, const CsWidget *w, const char *val) {
    if (w->kind != CS_ROW_CYCLE || w->obsolete == NULL || !ieq(val, w->obsolete)) {
        return 0;
    }
    fprintf(stderr, "settings: [%s] %s: \"%.40s\" is no longer offered, using %s\n", TAB_NAMES[tab], w->key, val,
            w->names != NULL && w->value >= 0 && w->value < w->count ? w->names[w->value] : "the default");
    inputCfgRequestSave();
    return 1;
}

/* A file value that stands for another value of the row: say so, take it and have the file rewritten */
static int widgetAlias(int tab, CsWidget *w, const char *val) {
    if (w->kind != CS_ROW_CYCLE || w->alias == NULL || !ieq(val, w->alias) || w->aliasValue < 0 || w->aliasValue >= w->count) {
        return 0;
    }
    w->value = w->aliasValue;
    fprintf(stderr, "settings: [%s] %s: \"%.40s\" is not offered, using %s\n", TAB_NAMES[tab], w->key, val, w->names[w->value]);
    inputCfgRequestSave();
    return 1;
}

/* A file value for a row: parse it, say why when it is bad (the default stays) */
static void widgetLoad(int tab, CsWidget *w, const char *val) {
    const char *why;
    int v;

    if (widgetObsolete(tab, w, val)) {
        return;
    }
    if (widgetAlias(tab, w, val)) {
        if (w->apply != NULL) {
            w->apply(w->value);
        }
        return;
    }
    why = widgetParse(w, val);
    v = w->value;
    if (why != NULL) {
        fprintf(stderr, "settings: [%s] %s: %s: %.40s\n", TAB_NAMES[tab], w->key, why, val);
    } else if (w->apply != NULL) {
        w->apply(v);
    }
}

int csWidgetAdd(int tab, const CsWidget *w) {
    CsWidget *dst;
    int i, found;

    if (w == NULL || !tabValid(tab) || tab == CS_TAB_CONTROLS || widgetCount_[tab] >= (tab == CS_TAB_MODS ? CS_MODS_ROWS : CS_MAX_WIDGETS)
        || w->kind < CS_ROW_ACTION || w->kind > CS_ROW_SLIDER) {
        return -1;
    }
    if (w->hidden && ((w->kind != CS_ROW_TOGGLE && w->kind != CS_ROW_CYCLE) || w->key == NULL || hiddenCount_[tab] >= CS_MAX_HIDDEN)) {
        return -1;
    }
    dst = w->hidden ? &hidden_[tab][hiddenCount_[tab]] : &widgets_[tab][widgetCount_[tab]];
    *dst = *w;
    if (w->kind == CS_ROW_SLIDER && dst->step < 1) {
        dst->step = 1;
    }
    if (w->kind == CS_ROW_ACTION) {
        dst->key = NULL; /* an action has nothing to save */
    }
    /* the file was read before this row existed: take its entry */
    found = 0;
    for (i = 0; dst->key != NULL && i < CS_PENDING; i++) {
        if (pending_[i].used && ieq(pending_[i].section, sectionOf(tab, dst)) && strcmp(pending_[i].key, dst->key) == 0) {
            pending_[i].used = 0;
            found = 1;
            if (!dst->retired) {
                widgetLoad(tab, dst, pending_[i].val);
            }
        }
    }
    if (dst->retired) {
        if (found) {
            inputCfgRequestSave(); /* drop the old line from the file */
        }
    } else if (dst->key != NULL && !found) {
        inputCfgRequestSave(); /* the file lacks this key: write it, with its comment, next frame */
    }
    if (w->hidden) {
        hiddenCount_[tab]++;
        return 0;
    }
    return widgetCount_[tab]++;
}

/* ---- the settings file's widget sections ---- */

static int extOwns(const char *section) {
    int tab, i;

    if (tabBySection(section) >= 0) {
        return 1;
    }
    for (tab = CS_TAB_VIDEO; tab < CS_TAB_COUNT; tab++) {
        for (i = 0; i < widgetCount_[tab] + hiddenCount_[tab]; i++) {
            const CsWidget *w = i < widgetCount_[tab] ? &widgets_[tab][i] : &hidden_[tab][i - widgetCount_[tab]];

            if (sectionHas(section, tab, w)) {
                return 1;
            }
        }
    }
    return 0;
}

static const char *extEntry(const char *section, const char *key, const char *val) {
    int tab = -1, i;
    CsWidget *w;

    if (!extOwns(section)) {
        return "unknown section";
    }
    w = widgetBySection(section, key, &tab);
    if (w != NULL && w->retired) {
        inputCfgRequestSave(); /* the old key leaves the file */
        return NULL;
    }
    if (w != NULL) {
        const char *why;

        if (widgetObsolete(tab, w, val)) {
            return NULL;
        }
        if (widgetAlias(tab, w, val)) {
            if (w->apply != NULL) {
                w->apply(w->value);
            }
            return NULL;
        }
        why = widgetParse(w, val);

        if (why == NULL && w->apply != NULL) {
            w->apply(w->value);
        }
        return why;
    }
    /* the row may be added later (or never: reported by csSettingsReportUnknown) */
    for (i = 0; i < CS_PENDING; i++) {
        if (!pending_[i].used) {
            pending_[i].used = 1;
            snprintf(pending_[i].section, sizeof(pending_[i].section), "%s", section);
            snprintf(pending_[i].key, sizeof(pending_[i].key), "%s", key);
            snprintf(pending_[i].val, sizeof(pending_[i].val), "%s", val);
            return NULL;
        }
    }
    return "too many unknown entries";
}

/* One widget's "; comment\nkey = value\n" lines, written at buf; the length, -1 when it does not fit */
static int formatWidget(char *buf, int cap, int tab, int i) {
    const CsWidget *w = i < widgetCount_[tab] ? &widgets_[tab][i] : &hidden_[tab][i - widgetCount_[tab]];
    char values[160], val[48], line[320];
    int j;

    values[0] = '\0';
    if (w->kind == CS_ROW_TOGGLE) {
        snprintf(values, sizeof(values), "on or off");
    } else if (w->kind == CS_ROW_SLIDER) {
        snprintf(values, sizeof(values), "a number from %d to %d", w->min, w->max);
    } else if (w->kind == CS_ROW_CYCLE) {
        int m = snprintf(values, sizeof(values), "one of");

        for (j = 0; j < w->count && w->names != NULL && m < (int)sizeof(values) - 24; j++) {
            m += snprintf(values + m, sizeof(values) - (size_t)m, "%s %s", j == 0 ? "" : ",", tokenOf(w, j));
        }
    }
    if (i >= widgetCount_[tab] && w->kind == CS_ROW_CYCLE && w->value >= 0 && w->value < w->count) {
        snprintf(val, sizeof(val), "%s", tokenOf(w, w->value)); /* a hidden cycle (L4b) */
    } else if (i >= widgetCount_[tab]) {
        snprintf(val, sizeof(val), "%s", w->value != 0 ? "on" : "off");
    } else if (w->kind == CS_ROW_CYCLE && w->tokens != NULL && w->value >= 0 && w->value < w->count) {
        csRowValue(tab, 0, i, val, sizeof(val)); /* keeps the live read in step */
        snprintf(val, sizeof(val), "%s", tokenOf(w, w->value));
    } else {
        csRowValue(tab, 0, i, val, sizeof(val));
    }
    if (w->kind == CS_ROW_TOGGLE || w->kind == CS_ROW_CYCLE) {
        char *p;

        for (p = val; *p != '\0'; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
    }
    j = snprintf(line, sizeof(line), "; %s Values: %s.\n%s = %s\n",
                 w->comment != NULL ? w->comment : (w->desc != NULL && w->desc[0] != '\0' ? w->desc : w->label),
                 values, w->key, val);
    if (j < 0 || j >= cap) {
        return -1;
    }
    memcpy(buf, line, (size_t)j + 1);
    return j;
}

/* The file's sections follow the tabs, but a row that names a section (CsWidget.section) is written in
   it, next to the tab's own rows of that name, so a moved row keeps its old place in the file */
static int extFormat(char *buf, int cap) {
    char done[CS_TAB_COUNT * 2][24];
    int len = 0, doneCount = 0, tab, i, t2, i2, k;

    buf[0] = '\0';
    for (tab = CS_TAB_VIDEO; tab < CS_TAB_COUNT; tab++) {
        for (i = 0; i < widgetCount_[tab] + hiddenCount_[tab]; i++) {
            const CsWidget *w = i < widgetCount_[tab] ? &widgets_[tab][i] : &hidden_[tab][i - widgetCount_[tab]];
            char lowerSec[24];
            int seen = 0, n;

            if (w->key == NULL || w->retired) {
                continue;
            }
            for (k = 0; sectionOf(tab, w)[k] != '\0' && k < (int)sizeof(lowerSec) - 1; k++) {
                lowerSec[k] = (char)tolower((unsigned char)sectionOf(tab, w)[k]);
            }
            lowerSec[k] = '\0';
            for (k = 0; k < doneCount; k++) {
                seen |= strcmp(done[k], lowerSec) == 0;
            }
            if (seen || doneCount >= CS_TAB_COUNT * 2) {
                continue;
            }
            snprintf(done[doneCount++], sizeof(done[0]), "%s", lowerSec);
            n = snprintf(buf + len, (size_t)(cap - len), "\n[%s]\n", lowerSec);
            if (n < 0 || n >= cap - len) {
                return -1;
            }
            len += n;
            for (t2 = tab; t2 < CS_TAB_COUNT; t2++) {
                for (i2 = 0; i2 < widgetCount_[t2] + hiddenCount_[t2]; i2++) {
                    const CsWidget *w2 = i2 < widgetCount_[t2] ? &widgets_[t2][i2] : &hidden_[t2][i2 - widgetCount_[t2]];

                    if (w2->key == NULL || w2->retired || !sectionHas(lowerSec, t2, w2)) {
                        continue;
                    }
                    n = formatWidget(buf + len, cap - len, t2, i2);
                    if (n < 0) {
                        return -1;
                    }
                    len += n;
                }
            }
        }
    }
    return len;
}

static const InputCfgExt ext = {extFormat, extOwns, extEntry};

void csSettingsInstall(void) {
    inputCfgSetExt(&ext);
}

void csSettingsReportUnknown(void (*log)(const char *msg)) {
    int i;
    char msg[160];

    for (i = 0; i < CS_PENDING; i++) {
        if (pending_[i].used) {
            pending_[i].used = 0;
            snprintf(msg, sizeof(msg), "[%s] %s: unknown setting, ignored", pending_[i].section, pending_[i].key);
            if (log != NULL) {
                log(msg);
            }
        }
    }
}

void csWidgetsClear(void) {
    memset(widgets_, 0, sizeof(widgets_));
    memset(widgetCount_, 0, sizeof(widgetCount_));
    memset(hidden_, 0, sizeof(hidden_));
    memset(hiddenCount_, 0, sizeof(hiddenCount_));
    memset(pending_, 0, sizeof(pending_));
    memset(cursors_, 0, sizeof(cursors_));
}

int csWidgetGet(int tab, int row) {
    const CsWidget *w = widgetAt(tab, row);

    return w != NULL ? w->value : -1;
}

/* Stores a value as the row's kind allows it; applies it only when it changed */
int csWidgetSet(int tab, int row, int value) {
    CsWidget *w = widgetAt(tab, row);
    int old;

    if (w == NULL) {
        return -1;
    }
    old = w->value;
    switch (w->kind) {
    case CS_ROW_TOGGLE:
        value = value != 0;
        break;
    case CS_ROW_CYCLE:
        value = csWrap(value, 0, w->count);
        break;
    case CS_ROW_SLIDER:
        value = value < w->min ? w->min : (value > w->max ? w->max : value);
        break;
    default:
        return w->value;
    }
    w->value = value;
    if (value != old) {
        if (w->apply != NULL) {
            w->apply(value);
        }
        if (w->key != NULL) {
            inputCfgRequestSave();
        }
    }
    return value;
}

/* Left (-1) / Right (+1) on a widget; returns 1 when the value moved */
static int widgetStep(CsWidget *w, int tab, int row, int dir) {
    int old = w->value;

    switch (w->kind) {
    case CS_ROW_TOGGLE:
        csWidgetSet(tab, row, !w->value);
        break;
    case CS_ROW_CYCLE:
        csWidgetSet(tab, row, w->value + dir);
        break;
    case CS_ROW_SLIDER:
        csWidgetSet(tab, row, w->value + dir * w->step);
        break;
    case CS_ROW_ACTION:
        if (w->stepId == NULL) {
            return 0;
        }
        w->stepId(w->id, dir);
        return 1;
    default:
        return 0;
    }
    return w->value != old;
}

/* ---- state ---- */

int csIsOpen(void) { return open_; }
int csDevice(void) { return device_; }
int csTab(void) { return tab_; }
int csCursor(void) { return cursors_[tab_]; }
int csWaitingRow(void) { return waitRow_; }
const char *csMessage(void) { return message_; }
unsigned csNavEpoch(void) { return navEpoch_; }

static void (*soundHook_)(int event);

void csSetSoundHook(void (*hook)(int event)) { soundHook_ = hook; }

static void sound(int event) {
    if (soundHook_ != NULL) {
        soundHook_(event);
    }
}

static void setMessage(const char *text) {
    snprintf(message_, sizeof(message_), "%s", text);
}

void csOpen(void) {
    if (blocked_) {
        return;
    }
    open_ = 1;
    navEpoch_++;
    csRepeatReset();
    sound(CS_SND_OPEN);
    memset(cursors_, 0, sizeof(cursors_)); /* the tab is kept, the rows start at the top */
    waitRow_ = -1;
    message_[0] = '\0';
    releaseHold_ = 1;
}

static void closeScreen(int withSound) {
    if (waitRow_ >= 0) {
        inputCapCancel();
    }
    if (open_ && withSound) {
        sound(CS_SND_CLOSE);
    }
    waitRow_ = -1;
    open_ = 0;
    navEpoch_++;
    releaseHold_ = 1;
    csRepeatReset();
}

void csClose(void) {
    closeScreen(1);
}

void csSetCursor(int row) {
    navEpoch_++;
    cursors_[tab_] = csWrap(row, 0, csTabRowCount(tab_));
    csTabWindowTop(tab_); /* the window follows at once, not at the next frame */
}

void csSetTab(int tab) {
    if (tabValid(tab)) {
        navEpoch_++;
        tab_ = tab;
    }
}

void csSetDevice(int device) {
    if (device == INPUT_DEV_KEYBOARD || device == INPUT_DEV_PAD) {
        device_ = device;
    }
}

/* The capture the screen started ended (bound, Escape): returns 1 on that frame */
static int csSyncCapture(void) {
    int state;

    if (waitRow_ < 0) {
        return 0;
    }
    state = inputCapState();
    if (state == INPUT_CAP_WAITING) {
        if (inputCapRefused() != 0) {
            setMessage("That button is reserved");
        }
        return 0;
    }
    waitRow_ = -1;
    setMessage(state == INPUT_CAP_BOUND ? "Bound" : "Cancelled");
    sound(state == INPUT_CAP_BOUND ? CS_SND_CONFIRM : CS_SND_CANCEL);
    return 1;
}

static int csConfirm(void) {
    int row = cursors_[tab_];
    CsWidget *w = widgetAt(tab_, row);

    if (w != NULL) {
        switch (w->kind) {
        case CS_ROW_ACTION:
            if (w->applyId != NULL) {
                w->applyId(w->id, 0);
            } else if (w->apply != NULL) {
                w->apply(0);
            }
            return 1;
        case CS_ROW_TOGGLE:
        case CS_ROW_CYCLE:
            return widgetStep(w, tab_, row, 1);
        default:
            return 0;
        }
    }
    switch (csRowKind(tab_, row)) {
    case CS_ROW_BINDING:
        if (inputCapBegin(device_, csRowSlot(row)) == 0) {
            waitRow_ = row;
        }
        return 1;
    case CS_ROW_ACTION:
        if (row == CS_CTL_ROW_RESET) {
            inputCfgReset(device_);
            setMessage("Defaults restored");
        } else {
            csClose();
        }
        return 1;
    default:
        return 0;
    }
}

/* A tap on a row (android T7c): focus it; activate it when it already had the focus or the tap is on its value; a
   slider's bar steps down on its left half and up on its right half. */
static int tapRow(int action) {
    int row = action & 0xFF, zone = (action >> 8) & 3, moved = 0;

    if (row >= csTabRowCount(tab_)) {
        return 0;
    }
    if (cursors_[tab_] != row) {
        csSetCursor(row);
        sound(CS_SND_MOVE);
        moved = 1;
    }
    if (zone == CS_TAP_DEC || zone == CS_TAP_INC) {
        if (csRowKind(tab_, row) == CS_ROW_SLIDER) {
            csAct(zone == CS_TAP_DEC ? CS_ACT_LEFT : CS_ACT_RIGHT);
            return 1;
        }
        zone = CS_TAP_VALUE;
    }
    if (zone == CS_TAP_VALUE || !moved) {
        csAct(CS_ACT_CONFIRM);
    }
    return 1;
}

static int actCore(int action) {
    int count;

    csSyncCapture();
    if (action == CS_ACT_TOGGLE) {
        if (waitRow_ >= 0) {
            return 0;
        }
        if (open_) {
            csClose();
        } else {
            csOpen();
        }
        return 1;
    }
    if (!open_ || waitRow_ >= 0 || action == CS_ACT_NONE) {
        return 0;
    }
    message_[0] = '\0';
    count = csTabRowCount(tab_);
    if (action & CS_ACT_TAB_FLAG) {
        int tab = action & 0xFF;

        if (!tabValid(tab) || tab == tab_) {
            return 0;
        }
        csSetTab(tab);
        sound(CS_SND_TAB);
        return 1;
    }
    if (action & CS_ACT_TAP_FLAG) {
        return tapRow(action);
    }
    switch (action) {
    case CS_ACT_ROW_PREV:
    case CS_ACT_ROW_NEXT: {
        int was = cursors_[tab_], now = was + (action == CS_ACT_ROW_PREV ? -1 : 1);

        if (count == 0 || now < 0 || now >= count) {
            return 0;
        }
        cursors_[tab_] = now;
        navEpoch_++;
        csTabWindowTop(tab_);
        sound(CS_SND_MOVE);
        return 1;
    }
    case CS_ACT_UP:
    case CS_ACT_DOWN:
        if (count == 0) {
            return 0;
        }
        {
            int was = cursors_[tab_];

            cursors_[tab_] = csWrap(was, action == CS_ACT_UP ? -1 : 1, count);
            navEpoch_ += cursors_[tab_] != was;
            csTabWindowTop(tab_);
            return cursors_[tab_] != was; /* a single row stays put, silently */
        }
    case CS_ACT_TAB_PREV:
    case CS_ACT_TAB_NEXT:
        tab_ = csWrap(tab_, action == CS_ACT_TAB_PREV ? -1 : 1, CS_TAB_COUNT);
        navEpoch_++;
        return 1;
    case CS_ACT_LEFT:
    case CS_ACT_RIGHT: {
        CsWidget *w = widgetAt(tab_, cursors_[tab_]);

        if (w != NULL) {
            return widgetStep(w, tab_, cursors_[tab_], action == CS_ACT_LEFT ? -1 : 1);
        }
        if (tab_ == CS_TAB_CONTROLS) {
            device_ = device_ == INPUT_DEV_KEYBOARD ? INPUT_DEV_PAD : INPUT_DEV_KEYBOARD;
            return 1;
        }
        return 0;
    }
    case CS_ACT_CANCEL:
        csClose();
        return 1;
    case CS_ACT_CONFIRM:
        return count == 0 ? 0 : csConfirm();
    default:
        return 0;
    }
}

/* The sound a navigation action makes, the field menu's (src/engine/menu/field_menu.c) and the language menu's
   (country_select.c): a row move is the cursor's, a tab or value change the lateral move's, Confirm the select.
   Open and close sound where they happen (csOpen, closeScreen); an action that changed nothing is silent. */
int csAct(int action) {
    int was = open_, ok = actCore(action);

    if (!ok || !was || !open_) {
        return ok; /* nothing happened, or it opened or closed the screen (their own sounds) */
    }
    switch (action) {
    case CS_ACT_UP:
    case CS_ACT_DOWN:
        sound(CS_SND_MOVE);
        break;
    case CS_ACT_LEFT:
    case CS_ACT_RIGHT:
        sound(CS_SND_VALUE);
        break;
    case CS_ACT_TAB_PREV:
    case CS_ACT_TAB_NEXT:
        sound(CS_SND_TAB);
        break;
    case CS_ACT_CONFIRM:
        sound(CS_SND_CONFIRM);
        break;
    default:
        break;
    }
    return ok;
}

/* ---- SDL input ---- */

int csActionFromEvent(const union SDL_Event *ev) {
    const SDL_Event *e = (const SDL_Event *)ev;
    static int stick[2]; /* -1, 0, +1 per axis: x, y */

    switch (e->type) {
    case SDL_KEYDOWN:
        if (e->key.repeat) {
            return CS_ACT_NONE; /* the screen repeats a held direction itself (csRepeatFrame), the same on every device */
        }
        switch (e->key.keysym.scancode) {
        case SDL_SCANCODE_UP:
            return CS_ACT_UP;
        case SDL_SCANCODE_DOWN:
            return CS_ACT_DOWN;
        case SDL_SCANCODE_LEFT:
            return CS_ACT_LEFT;
        case SDL_SCANCODE_RIGHT:
            return CS_ACT_RIGHT;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            return CS_ACT_CONFIRM;
        case SDL_SCANCODE_ESCAPE:
            return CS_ACT_CANCEL;
        case SDL_SCANCODE_Q:
        case SDL_SCANCODE_PAGEUP:
            return CS_ACT_TAB_PREV;
        case SDL_SCANCODE_E:
        case SDL_SCANCODE_PAGEDOWN:
            return CS_ACT_TAB_NEXT;
        case SDL_SCANCODE_F1:
            return CS_ACT_TOGGLE;
        default:
            return CS_ACT_NONE;
        }
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return CS_ACT_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return CS_ACT_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return CS_ACT_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return CS_ACT_RIGHT;
        case SDL_CONTROLLER_BUTTON_A:
            return CS_ACT_CONFIRM;
        case SDL_CONTROLLER_BUTTON_B:
            return CS_ACT_CANCEL;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            return CS_ACT_TAB_PREV;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            return CS_ACT_TAB_NEXT;
        case SDL_CONTROLLER_BUTTON_BACK: /* Select: the game never reads it */
            return CS_ACT_TOGGLE;
        default:
            return CS_ACT_NONE;
        }
    case SDL_CONTROLLERAXISMOTION:
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int i = e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY;
            int v = e->caxis.value;

            if (abs(v) < CS_STICK_OFF) {
                stick[i] = 0;
            } else if (abs(v) > CS_STICK_ON && stick[i] == 0) {
                stick[i] = v < 0 ? -1 : 1;
                if (i == 0) {
                    return v < 0 ? CS_ACT_LEFT : CS_ACT_RIGHT;
                }
                return v < 0 ? CS_ACT_UP : CS_ACT_DOWN;
            }
        }
        return CS_ACT_NONE;
    default:
        return CS_ACT_NONE;
    }
}

/* ---- hold to repeat ---- */

void csRepeatInit(CsRepeat *r) {
    r->action = CS_ACT_NONE;
    r->frames = r->stage = 0;
}

int csRepeatTick(CsRepeat *r, int held) {
    int due;

    if (held != r->action) {
        r->action = held;
        r->frames = r->stage = 0;
        return CS_ACT_NONE;
    }
    if (held == CS_ACT_NONE) {
        return CS_ACT_NONE;
    }
    due = r->stage == 0 ? CS_REPEAT_FIRST : r->stage == 1 ? CS_REPEAT_SECOND : CS_REPEAT_REST;
    if (++r->frames < due) {
        return CS_ACT_NONE;
    }
    r->frames = 0;
    if (r->stage < 2) {
        r->stage++;
    }
    return held;
}

static int dirIndex(int action) {
    return action >= CS_ACT_UP && action <= CS_ACT_RIGHT ? action - CS_ACT_UP : -1;
}

void csHoldInit(CsHold *h) {
    memset(h, 0, sizeof(*h));
    h->last = CS_ACT_NONE;
}

void csHoldSet(CsHold *h, int action, int down) {
    int i = dirIndex(action), k;

    if (i < 0) {
        return;
    }
    h->down[i] = down != 0;
    if (down) {
        h->last = action;
    } else if (h->last == action) {
        h->last = CS_ACT_NONE;
        for (k = 3; k >= 0; k--) {
            if (h->down[k]) {
                h->last = CS_ACT_UP + k;
                break;
            }
        }
    }
}

int csHoldAction(const CsHold *h) {
    return h->last;
}

static CsRepeat repeat_;
static CsHold hold_; /* written by the event watch (any thread), read in csPump under the same lock */

void csRepeatReset(void) {
    csRepeatInit(&repeat_);
}

int csRepeatFrame(int held) {
    CsWidget *w;
    int act;

    if (!open_ || waitRow_ >= 0 || blocked_) {
        csRepeatReset();
        return CS_ACT_NONE;
    }
    act = csRepeatTick(&repeat_, held);
    if (act == CS_ACT_LEFT || act == CS_ACT_RIGHT) {
        w = widgetAt(tab_, cursors_[tab_]);
        if (w == NULL || (w->kind != CS_ROW_SLIDER && w->kind != CS_ROW_CYCLE)) {
            return CS_ACT_NONE; /* a toggle or the device switch changes once per press */
        }
    }
    return act != CS_ACT_NONE && csAct(act) ? act : CS_ACT_NONE;
}

/* Which direction an SDL event presses or releases: the keyboard arrows, the D-pad and the left stick */
static void holdFromEvent(const SDL_Event *e) {
    static int stick[2];
    int action = CS_ACT_NONE, down = 0;

    switch (e->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        down = e->type == SDL_KEYDOWN;
        action = e->key.keysym.scancode == SDL_SCANCODE_UP      ? CS_ACT_UP
                 : e->key.keysym.scancode == SDL_SCANCODE_DOWN  ? CS_ACT_DOWN
                 : e->key.keysym.scancode == SDL_SCANCODE_LEFT  ? CS_ACT_LEFT
                 : e->key.keysym.scancode == SDL_SCANCODE_RIGHT ? CS_ACT_RIGHT
                                                                : CS_ACT_NONE;
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        down = e->type == SDL_CONTROLLERBUTTONDOWN;
        action = e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP      ? CS_ACT_UP
                 : e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN  ? CS_ACT_DOWN
                 : e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT  ? CS_ACT_LEFT
                 : e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? CS_ACT_RIGHT
                                                                         : CS_ACT_NONE;
        break;
    case SDL_CONTROLLERAXISMOTION:
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int i = e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY, v = e->caxis.value;
            int neg = i ? CS_ACT_UP : CS_ACT_LEFT, pos = i ? CS_ACT_DOWN : CS_ACT_RIGHT;

            if (abs(v) < CS_STICK_OFF && stick[i] != 0) {
                csHoldSet(&hold_, stick[i] < 0 ? neg : pos, 0);
                stick[i] = 0;
            } else if (abs(v) > CS_STICK_ON && stick[i] == 0) {
                stick[i] = v < 0 ? -1 : 1;
                csHoldSet(&hold_, v < 0 ? neg : pos, 1);
            }
        }
        return;
    default:
        return;
    }
    if (action != CS_ACT_NONE) {
        csHoldSet(&hold_, action, down);
    }
}

void csQueueHold(int action, int down) {
    SDL_AtomicLock(&queueLock);
    csHoldSet(&hold_, action, down);
    SDL_AtomicUnlock(&queueLock);
    if (down) {
        csQueueAction(action); /* the press itself, as the event watch queues it */
    }
}

void csQueueAction(int action) {
    if (action == CS_ACT_NONE) {
        return;
    }
    SDL_AtomicLock(&queueLock);
    if (queueLen < CS_QUEUE) {
        queue_[queueLen++] = action;
    }
    SDL_AtomicUnlock(&queueLock);
}

static int csWatch(void *userdata, SDL_Event *e) {
    (void)userdata;
    SDL_AtomicLock(&queueLock);
    holdFromEvent(e);
    SDL_AtomicUnlock(&queueLock);
    csQueueAction(csActionFromEvent((const union SDL_Event *)e));
    return 0;
}

void csInit(void) {
    static int inited;

    if (!inited) {
        inited = 1;
        SDL_AddEventWatch(csWatch, NULL);
    }
}

void csSetBlocked(int blocked) {
    blocked_ = blocked;
    if (blocked && open_) {
        closeScreen(0); /* a movie takes the screen: no sound over it */
    }
}

void csPump(void) {
    int local[CS_QUEUE];
    int n, i, held;
    int ended = csSyncCapture();

    SDL_AtomicLock(&queueLock);
    n = queueLen;
    memcpy(local, queue_, sizeof(int) * (size_t)n);
    queueLen = 0;
    held = csHoldAction(&hold_);
    SDL_AtomicUnlock(&queueLock);
    if (ended || waitRow_ >= 0) {
        csRepeatReset();
        return; /* what arrived during (or ended) a capture is the capture's */
    }
    for (i = 0; i < n; i++) {
        csAct(local[i]);
    }
    csRepeatFrame(held);
}

int csFilterPad(unsigned short *buttons, unsigned char *analog) {
    int hold = 0;

    if (open_) {
        hold = 1;
    } else if (releaseHold_) {
        if (*buttons != 0xFFFF) {
            hold = 1;
        } else {
            releaseHold_ = 0;
        }
    }
    if (hold) {
        *buttons = 0xFFFF;
        analog[0] = analog[1] = analog[2] = analog[3] = 127;
    }
    return hold;
}
