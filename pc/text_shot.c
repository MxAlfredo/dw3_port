/*
 * Host harness: one PNG per game string, drawn with the game's own font and text engine.
 *
 *     text_shot <disc.bin> <lang> <out-dir> <FILE>:<part>:<index> [...]
 *
 * <lang> is es419 (the S-prefixed Spanish file with the localization/es-419 overlay, served by the
 * port's own text layer, shim/text_data.c) or eng (the E-prefixed English file straight from the
 * disc). A string whose text starts with the speaker codes (02 07 name 02 07) is drawn like a
 * dialogue talk box: the speaker's name in a style 2 window and the body in a style 1 window of
 * 3 lines, both in PALETTE_DARK_BLUE, as createTalkBox sets them up (the panel itself is not
 * drawn: createTalkBox is a task tree with zoom boxes and a frame; the two text windows are what
 * decides how the text looks). Any other string is a plain text window. Page breaks (02 03, and
 * a full page) give one PNG per page: <FILE>_<part>_<index>_<lang>_p<N>.png.
 *
 * Engine number codes (02 05 n) show 7; the player is called "Junior" (GAME.name).
 * Run it headless: SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null.
 * The disc is only read; the PNGs go where the caller says (build/ is ignored by git, ADR-001).
 */
#include "engine/file.h"
#include "engine/heap.h"
#include "engine/text.h"
#include "engine/gfx.h"
#include "engine/game_state.h"
#include "shim/host.h"
#include "shim/text_data.h"

#include <PsyX/PsyX_public.h>
#include <PsyX/PsyX_render.h>

#include <stdio.h>

/* The game's engine/game.h declares strlen/strcpy/strncpy/atoi with PsyQ's types, which clash with
   the host's headers; the few helpers needed here are written by hand. */
extern void exit(int status);
extern int setenv(const char *name, const char *value, int overwrite);
extern int mkdir(const char *path, unsigned int mode);
extern char *getenv(const char *name);
extern void pcCdTextInit(const char *dataDir, const char *l10nDir);

#define LAYER 0x1003
#define MAX_PAGES 32
#define MAX_REDRAW 64
/* createTalkBox's type 1 (the bottom box): where its windows start, moved to the screen's left */
#define BOX_X 16
#define BOX_Y 96
#define NAME_DY 0
#define TEXT_DY 16

static int strEq(const char *a, const char *b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int parseNum(const char **p, int *out) {
    int v = 0;
    int n = 0;

    while (**p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        (*p)++;
        n++;
    }
    *out = v;
    return n > 0;
}

/* "FILE:part:index" -> the file's base name (up to 15 chars), part and index */
static int parseSpec(const char *spec, char *base, int *part, int *index) {
    int n = 0;

    while (*spec != 0 && *spec != ':' && n < 15) {
        base[n++] = *spec++;
    }
    base[n] = 0;
    if (*spec++ != ':' || !parseNum(&spec, part) || *spec++ != ':' || !parseNum(&spec, index) || *spec != 0) {
        return 0;
    }
    return n > 0;
}

/* The font's codes of an ASCII name (digits 04.., capitals 0E.., lowercase 28..) */
static void setPlayerName(const char *ascii) {
    int i;

    for (i = 0; ascii[i] != 0 && i < 0x15; i++) {
        char c = ascii[i];

        GAME.name[i] = (c >= '0' && c <= '9') ? c - '0' + 0x04
                     : (c >= 'A' && c <= 'Z') ? c - 'A' + 0x0E
                                              : c - 'a' + 0x28;
        GAME.name[i + 1] = 0;
    }
}

static int countGlyphs(u_long *top) {
    P_TAG *node = (P_TAG *)top;
    int count = 0;
    int guard;

    for (guard = 0; guard < 8192; guard++) {
        if (node->len > 0 && node->code == 0x64) {
            count++;
        }
        if (isendprim(node)) {
            break;
        }
        node = (P_TAG *)node->addr;
    }
    return count;
}

/* One presented frame; saves the PNG when `path` is not NULL and the page is not waiting. Returns the glyph sprites drawn. */
static int renderFrame(TextWindow *name, TextWindow *win, int light, const char *path, int w, int h) {
    DRAWENV drawEnv;
    int glyphs;

    SetDefDrawEnv(&drawEnv, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    drawEnv.isbg = 1;
    drawEnv.r0 = drawEnv.g0 = drawEnv.b0 = light ? 0xD8 : 0x30;
    PutDrawEnv(&drawEnv);
    pcDrawBeginFrame();
    if (name != NULL) {
        textWindowDraw(name);
    }
    textWindowDraw(win);
    glyphs = countGlyphs(pcDrawOtTop());
    PsyX_BeginScene();
    DrawOTag(pcDrawOtTop());
    if (path != NULL && win->state != 2) {
        pcSaveShot(path, w, h);
    }
    PsyX_EndScene();
    return glyphs;
}

static void makeDirs(const char *path) {
    char buf[1024];
    int i;

    for (i = 0; path[i] != 0 && i < 1000; i++) {
        buf[i] = path[i];
        if (path[i] == '/' && i > 0) {
            buf[i] = 0;
            mkdir(buf, 0777);
            buf[i] = '/';
        }
    }
    buf[i] = 0;
    mkdir(buf, 0777);
}

static int findFile(int lang, const char *base) {
    int k;

    for (k = 0; k < TD_FILES; k++) {
        if (strEq(tdFileBase(k), base)) {
            return tdIndexOf(lang, k);
        }
    }
    return -1;
}

static int dirExists(const char *p) {
    FILE *f = fopen(p, "r");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    const char *langName;
    int es419;
    const char *outDir;
    const char *l10n = NULL;
    DISPENV dispEnv;
    int width = 0;
    int height = 0;
    int arg;
    int failures = 0;

    if (argc < 5) {
        fprintf(stderr, "usage: %s <disc.bin> <es419|eng> <out-dir> <FILE>:<part>:<index> [...]\n", argv[0]);
        return 2;
    }
    langName = argv[2];
    es419 = strEq(langName, "es419");
    if (!es419 && !strEq(langName, "eng")) {
        fprintf(stderr, "text_shot: lang must be es419 or eng\n");
        return 2;
    }
    outDir = argv[3];
    makeDirs(outDir);

    pcDrawInit();
    pcCdReaderInit(argv[1]);
    if (es419) {
        char probe[1100];

        setenv("DW3_TEXT_VARIANT", "es419", 1);
        l10n = getenv("DW3_L10N_DIR");
        if (l10n == NULL) {
            l10n = "localization/es-419";
            snprintf(probe, sizeof probe, "%s/SSTALK00.json", l10n);
            if (!dirExists(probe)) {
                l10n = "../localization/es-419";
            }
        }
    }
    pcCdTextInit(NULL, l10n);

    PsyX_Initialise("dw3_text_shot", SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    PsyX_GetScreenSize(&width, &height);
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "text_shot: PsyX_Initialise did not open a window (no GL context)\n");
        return 1;
    }
    SetDefDispEnv(&dispEnv, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    PutDispEnv(&dispEnv);
    FONT.load();
    setPlayerName("Junior");

    for (arg = 4; arg < argc; arg++) {
        char base[16];
        char fileTag[16];
        int part, index, file, pageNo, redraw, i, dialog;
        u8 *data;
        char *table;
        char *text;
        TextWindow *win;
        TextWindow *nameWin = NULL;
        char nameBuf[0x20];

        if (!parseSpec(argv[arg], base, &part, &index)) {
            fprintf(stderr, "text_shot: bad spec %s (want FILE:part:index)\n", argv[arg]);
            failures++;
            continue;
        }
        for (i = 0; i < 16; i++) {
            fileTag[i] = base[i];
        }
        /* the spec may carry the language letter and the S (SSTALK00, ESTALK00): the base is what follows */
        if (findFile(TD_ENG, base) < 0 && base[0] != 0 && base[1] == 'S') {
            for (i = 0; base[i + 2] != 0; i++) {
                base[i] = base[i + 2];
            }
            base[i] = 0;
        }
        file = findFile(es419 ? TD_SPA : TD_ENG, base);
        if (file < 0 || !FILE_TABLE.exists(file)) {
            fprintf(stderr, "text_shot: no text file %s\n", base);
            failures++;
            continue;
        }
        data = FILE_CACHE.load(file);
        if (data == NULL) {
            fprintf(stderr, "text_shot: %s: no data\n", base);
            failures++;
            continue;
        }
        /* a SSDMG* pack is a table of offsets to embedded lang files; the others are one lang file */
        table = (char *)data;
        if (base[0] == 'D' && base[1] == 'M' && base[2] == 'G') {
            table = (char *)data + ((u32 *)data)[part];
        } else if (part != 0) {
            fprintf(stderr, "text_shot: %s has no parts, got part %d\n", base, part);
            failures++;
            continue;
        }
        text = getString((s32 *)table, index);
        if (text == NULL) {
            fprintf(stderr, "text_shot: %s:%d:%d: no such string\n", base, part, index);
            failures++;
            continue;
        }

        dialog = (u8)text[0] == 2 && (u8)text[1] == 7;
        if (dialog) {
            int end = 2;

            while (text[end] != 2 && text[end + 1] != 7) {
                end++;
            }
            HEAP.zero(nameBuf, sizeof nameBuf);
            if (text[2] == 2 && text[3] == 9) {
                for (i = 0; GAME.name[i] != 0 && i < 0x1F; i++) {
                    nameBuf[i] = GAME.name[i];
                }
            } else {
                for (i = 0; i < end - 2 && i < 0x1F; i++) {
                    nameBuf[i] = text[2 + i];
                }
            }
            nameWin = createTextWindow(LAYER, 2, BOX_X, BOX_Y + NAME_DY);
            nameWin->setLines(nameWin, 3);
            nameWin->setString(nameWin, nameBuf, -1);
            nameWin->setPalette(nameWin, PALETTE_DARK_BLUE);
            win = createTextWindow(LAYER, 1, BOX_X, BOX_Y + TEXT_DY);
            win->setLines(win, 3);
            win->setString(win, &text[end + 2], -1);
            win->setPalette(win, PALETTE_DARK_BLUE);
        } else {
            win = createTextWindow(LAYER, 1, BOX_X, 24);
            win->setLines(win, 6);
            win->setString(win, text, -1);
        }
        win->setTypeDelay(win, 0);
        win->insertPlayerName(win);
        for (i = 1; i <= 5; i++) {
            win->setNumber(win, i, 7);
        }

        for (pageNo = 1; pageNo <= MAX_PAGES; pageNo++) {
            char path[1100];
            int glyphs = 0;

            snprintf(path, sizeof path, "%s/%s_%d_%d_%s_p%d.png", outDir, fileTag, part, index, langName, pageNo);
            /* a wait for a button or a pause stops the page's drawing: release it as the press would */
            for (redraw = 0; redraw < MAX_REDRAW; redraw++) {
                glyphs = renderFrame(nameWin, win, dialog, path, width, height);
                if (win->state != 2) {
                    break;
                }
                if (win->substate == 1) {
                    win->text[0].data[win->counter] = 5;
                }
                win->state = 1;
            }
            printf("%s (%d glyphs%s)\n", path, glyphs, dialog ? ", talk box" : ", text window");
            if (win->finished != 0) {
                break;
            }
        }
        FILE_CACHE.free(file);
    }
    PsyX_Shutdown();
    return failures != 0;
}
