#include "options_mods.h"

#include "controls_screen.h"
#include "input_cfg.h"
#include "mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One mod of the list. Manageable ones (on or off) come first, in load order, then the ones the player cannot change */
enum { MM_ON, MM_OFF, MM_BLOCKED, MM_REJECTED, MM_MISSING };

typedef struct ModEntry {
    char name[64];
    char version[24];
    char kind[8];
    char reason[200];
    int state;
    int initOn;  /* what this run started with */
    int initIdx; /* its place in the list at the start */
} ModEntry;

#define LIST_MAX 64
#define NAME_SHOWN 16 /* characters of the folder name in the row: the value starts at the panel's middle */
#define DESC_SHOWN 40 /* characters of the help line that fit the panel (the game font is wide) */

static ModEntry list_[LIST_MAX];
static int listN_;
static int explicitOrder_; /* [mods] order exists, or the player reordered: the file then carries the order */
static int rows_;          /* rows added: one per listed mod, or the empty line */
static int shown_;         /* mods with a row of their own */
static char label_[LIST_MAX + 1][48];
static char desc_[LIST_MAX + 1][64];
static char value_[LIST_MAX + 1][32];

static int manageable(const ModEntry *e) { return e->state == MM_ON || e->state == MM_OFF; }

/* The game's font has no comma, parentheses or other marks: show them as a space or a dash */
static void clean(char *dst, int cap, const char *src) {
    int i = 0;

    for (; src[i] != '\0' && i < cap - 1; i++) {
        char c = src[i];

        dst[i] = c == ',' ? ' ' : (c == '(' || c == ')') ? '-' : ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) ? '?' : c;
    }
    dst[i] = '\0';
}

/* Writes text into a fixed buffer, cutting it with ".." when it is longer than `shown` characters */
static void putShown(char *dst, int cap, int shown, const char *text) {
    char tmp[256];

    clean(tmp, (int)sizeof tmp, text);
    {
        char *j = strstr(tmp, "mod.json"); /* the font has no j */

        if (j != NULL) {
            memcpy(j, "mod file", 8);
        }
    }
    if (shown > cap - 1) {
        shown = cap - 1;
    }
    if ((int)strlen(tmp) > shown) {
        tmp[shown - 2] = '.';
        tmp[shown - 1] = '.';
        tmp[shown] = '\0';
    }
    snprintf(dst, (size_t)cap, "%s", tmp);
}

static int posOf(int index) {
    int i, n = 0;

    if (list_[index].state != MM_ON) {
        return 0;
    }
    for (i = 0; i <= index; i++) {
        n += list_[i].state == MM_ON;
    }
    return n;
}

/* A mod moved when its place among the mods that were on at the start and still are changed: one above it turning off
   or on is not a move of its own */
static int entryPending(int i) {
    int on = list_[i].state == MM_ON, k, now = 0, then = 0;

    if (!manageable(&list_[i])) {
        return 0;
    }
    if (on != list_[i].initOn) {
        return 1;
    }
    if (!on) {
        return 0;
    }
    for (k = 0; k < listN_; k++) {
        if (list_[k].state == MM_ON && list_[k].initOn) {
            now += k <= i;
            then += list_[k].initIdx <= list_[i].initIdx;
        }
    }
    return now != then;
}

int optionsModsPending(void) {
    int i;

    for (i = 0; i < listN_; i++) {
        if (entryPending(i)) {
            return 1;
        }
    }
    return 0;
}

int optionsModsListed(void) { return shown_; }

static void rebuildStrings(void) {
    int i, pending = optionsModsPending();

    for (i = 0; i < shown_; i++) {
        const ModEntry *e = &list_[i];
        char detail[256];

        putShown(label_[i], (int)sizeof label_[i], NAME_SHOWN, e->name);
        switch (e->state) {
        case MM_ON:
        case MM_OFF:
            if (e->state == MM_ON) {
                snprintf(value_[i], sizeof value_[i], "On %d%s", posOf(i), entryPending(i) ? " Restart" : "");
            } else {
                snprintf(value_[i], sizeof value_[i], "Off%s", entryPending(i) ? " Restart" : "");
            }
            if ((int)strlen(e->name) > NAME_SHOWN) { /* the row cut the name: the help line carries all of it */
                snprintf(detail, sizeof detail, "%s v%s", e->name, e->version);
            } else {
                snprintf(detail, sizeof detail, "v%s %s. %s", e->version, e->kind,
                         pending ? "Restart to apply" : "Left Right move");
            }
            putShown(desc_[i], (int)sizeof desc_[i], DESC_SHOWN, detail);
            break;
        case MM_BLOCKED:
            snprintf(value_[i], sizeof value_[i], "Blocked");
            snprintf(detail, sizeof detail, "Blocked: %s", e->reason);
            putShown(desc_[i], (int)sizeof desc_[i], DESC_SHOWN, detail);
            break;
        case MM_REJECTED:
            snprintf(value_[i], sizeof value_[i], "Invalid"); /* the font has no j */
            snprintf(detail, sizeof detail, "Invalid: %s", e->reason);
            putShown(desc_[i], (int)sizeof desc_[i], DESC_SHOWN, detail);
            break;
        default:
            snprintf(value_[i], sizeof value_[i], "Missing");
            putShown(desc_[i], (int)sizeof desc_[i], DESC_SHOWN, "Listed in settings.ini but absent.");
            break;
        }
    }
}

/* Writes the [mods] lines for the list as it is now. -1 (nothing written) when the file refuses them */
static int writeSettings(void) {
    size_t cap = 1, lens[2] = { 0, 0 };
    char *lines[2];
    int i, rc;

    for (i = 0; i < listN_; i++) {
        cap += strlen(list_[i].name) + 2;
    }
    lines[0] = calloc(cap, 1);
    lines[1] = calloc(cap, 1);
    if (lines[0] == NULL || lines[1] == NULL) {
        free(lines[0]);
        free(lines[1]);
        return -1;
    }
    for (i = 0; i < listN_; i++) {
        const ModEntry *e = &list_[i];
        int k = e->state == MM_ON ? 0 : 1;

        if (!manageable(e) || (e->state == MM_ON && !explicitOrder_)) {
            continue;
        }
        lens[k] += (size_t)snprintf(lines[k] + lens[k], cap - lens[k], "%s%s", lens[k] > 0 ? ", " : "", e->name);
    }
    rc = inputCfgSetMods(lines[0], lines[1]);
    free(lines[0]);
    free(lines[1]);
    return rc;
}

/* Applies a change: when the file refuses it the list goes back to how it was */
static void commit(const ModEntry *before, int beforeN, int beforeExplicit) {
    if (writeSettings() != 0) {
        fprintf(stderr, "mods: settings.ini refused the [mods] lines, the change is dropped\n");
        memcpy(list_, before, sizeof(ModEntry) * (size_t)beforeN);
        explicitOrder_ = beforeExplicit;
    }
    rebuildStrings();
}

static void applyRow(int id, int unused) {
    ModEntry before[LIST_MAX];
    int beforeExplicit = explicitOrder_;

    (void)unused;
    if (id < 0 || id >= shown_ || !manageable(&list_[id])) {
        return;
    }
    memcpy(before, list_, sizeof(ModEntry) * (size_t)listN_);
    list_[id].state = list_[id].state == MM_ON ? MM_OFF : MM_ON;
    commit(before, listN_, beforeExplicit);
}

static void stepRow(int id, int dir) {
    ModEntry before[LIST_MAX], t;
    int other = id + (dir < 0 ? -1 : 1), beforeExplicit = explicitOrder_;

    if (id < 0 || id >= shown_ || !manageable(&list_[id]) || other < 0 || other >= shown_ || !manageable(&list_[other])) {
        return; /* the first or last of the ones that can move */
    }
    memcpy(before, list_, sizeof(ModEntry) * (size_t)listN_);
    t = list_[id];
    list_[id] = list_[other];
    list_[other] = t;
    explicitOrder_ = 1;
    commit(before, listN_, beforeExplicit);
    if (list_[other].name[0] != '\0' && strcmp(list_[other].name, t.name) == 0) {
        csSetCursor(other); /* the cursor follows the mod */
    }
}

static const char *textRow(int id) {
    return id >= 0 && id < shown_ ? value_[id] : "";
}

static void noRow(int unused) { (void)unused; }

void optionsModsInit(void) {
    CsWidget w;
    int i, n = modsReportCount(), pos, seen = 0;
    const ModsReport *by[LIST_MAX];
    int byN = 0, pass;

    listN_ = shown_ = rows_ = 0;
    explicitOrder_ = inputCfgModsOrderText()[0] != '\0';
    memset(list_, 0, sizeof list_);
    /* passes: the active mods by load position, then the ones the player can turn on, then the blocked, rejected and missing */
    for (pass = 0; pass < 4; pass++) {
        int k;

        for (pos = 1; pos <= n && pass == 0; pos++) {
            for (k = 0; k < n && byN < LIST_MAX; k++) {
                const ModsReport *r = modsReportAt(k);

                if (r->status == MODS_REP_ACTIVE && r->position == pos) {
                    by[byN++] = r;
                }
            }
        }
        for (k = 0; k < n && byN < LIST_MAX && pass > 0; k++) {
            const ModsReport *r = modsReportAt(k); /* sorted by folder name already */
            int mine = pass == 1 ? (r->status == MODS_REP_NOT_LISTED || (r->status == MODS_REP_DISABLED && r->byUser))
                     : pass == 2 ? (r->status == MODS_REP_DISABLED && !r->byUser)
                                 : (r->status == MODS_REP_REJECTED || r->status == MODS_REP_MISSING);

            if (mine) {
                by[byN++] = r;
            }
        }
    }
    for (i = 0; i < byN; i++) {
        ModEntry *e = &list_[listN_++];
        const ModsReport *r = by[i];

        snprintf(e->name, sizeof e->name, "%s", r->name);
        snprintf(e->version, sizeof e->version, "%s", r->version);
        snprintf(e->kind, sizeof e->kind, "%s", r->kind);
        snprintf(e->reason, sizeof e->reason, "%s", r->reason);
        e->state = r->status == MODS_REP_ACTIVE ? MM_ON
                 : (r->status == MODS_REP_NOT_LISTED || (r->status == MODS_REP_DISABLED && r->byUser)) ? MM_OFF
                 : r->status == MODS_REP_DISABLED ? MM_BLOCKED
                 : r->status == MODS_REP_REJECTED ? MM_REJECTED : MM_MISSING;
    }
    for (i = 0; i < listN_; i++) {
        list_[i].initOn = list_[i].state == MM_ON;
        list_[i].initIdx = i;
    }
    shown_ = listN_; /* every mod has a row: the tab scrolls */
    rebuildStrings();
    for (i = 0; i < shown_; i++) {
        w = (CsWidget){.kind = CS_ROW_ACTION, .label = label_[i], .desc = desc_[i], .apply = noRow, .id = i,
                       .applyId = applyRow, .textId = textRow, .stepId = stepRow};
        csWidgetAdd(CS_TAB_MODS, &w);
        seen++;
    }
    if (listN_ == 0) {
        w = (CsWidget){.kind = CS_ROW_ACTION, .label = "No mods installed", .apply = noRow,
                       .desc = "Put mod folders in the mods folder."};
        csWidgetAdd(CS_TAB_MODS, &w);
        seen++;
    }
    rows_ = seen;
}
