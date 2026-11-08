/* The disc files no other exporter covers, as raw copies (content-export E7); see raw_export.h */
#include "raw_export.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exp_core.h"
#include "image_data.h"
#include "port_log.h"
#include "sprite_data.h"
#include "text_data.h"

#define RAW_SECTOR 2352
#define USER_AT 24
#define USER_BYTES 2048
#define MAX_FILES 8192
#define MAX_KINDS 32
#define SCHEMA "raw-v1"

/* ---------------------------------------------------------------- the kinds */

static const RawKind KINDS[] = {
    {"stage-map", "dat/field/stage/s<nnn>tmpk.bin (one per field stage)",
     "Archive per field stage. Entry 0 is usually the sprite sheet (a list of SpritePart) the stage's object art is cut with; the other entries "
     "are the collision grid, the battle zones and the doors the game reads through FieldMap (include/field/field_map.h). The inner layout is "
     "only partly known (docs/formats.md 4.3); some files do not fit the archive layout.",
     "The field: FIELDSTG_setMapFile loads it through the file cache when a stage starts.",
     "Copy the file to mods/<mod>/files/dat/field/stage/ with the same name; the game reads it from there. Keep the size of every entry the stage "
     "code indexes.",
     "yes"},
    {"stage-demo", "dat/field/stage/ssubdemo.bin and sdigdemo.bin",
     "Animation archives of the field cutscenes (CutsceneAnim.kind, FIELD_ANIM_FILE). Layout unknown.",
     "The field cutscene code (src/field/field_mode/cutscene.c) asks the file cache for them by number.",
     "Copy the file to mods/<mod>/files/dat/field/stage/ with the same name. The read goes through the file cache, so it should be honoured; "
     "no scene was run with a replaced file.",
     "unverified"},
    {"battle-effect", "dat/fight/effect/*.bin (239 files)",
     "Technique animation scripts (FighterInfo.effects, EffectModelEntry): a table with sub-archives and data; two files hold a TIM. The opcodes "
     "are not documented (docs/formats.md 4.6).",
     "The battle: a fighter's effects archive is read with the file cache (getEntry) when its techniques play.",
     "Copy the file to mods/<mod>/files/dat/fight/effect/ with the same name. The read goes through the file cache, so it should be honoured; "
     "no battle was run with a replaced file.",
     "unverified"},
    {"battle-common", "dat/fight/pgfgtc00.bin, pgfgtw00.bin, f000com1-4.bin",
     "PGFGTC00 and PGFGTW00 are the battle menu's sprite sheet and image (FILE_BATTLE_MENU, FILE_BATTLE_IMAGES); F000COM1-4 are RLEN archives "
     "of images the battle loads into VRAM (FILE_BATTLE_IMAGES_1..4). Their TIMs are not exported as PNG.",
     "The battle loader (src/battle/normal_battle/loader.c) requests them through the file cache.",
     "Copy the file to mods/<mod>/files/dat/fight/ with the same name. The read goes through the file cache, so it should be honoured; no battle "
     "was run with a replaced file.",
     "unverified"},
    {"training-data", "dat/training/tranin<nn>.bin (12 files)",
     "Own format, not analysed (docs/formats.md 4.10).",
     "Nothing found: no code reads them by file number.",
     "Nothing to replace: the game does not read them.", "no"},
    {"sprite-sheet", "<name>cs.bin of screen, nameent, mcard, dglb, training and country (15 files)",
     "The sprite sheet (a list of SpritePart, include/engine/gfx.h) of the image archive <name>tm.bin next to it. The images of the TM file are "
     "exported as PNG; the sheet is not.",
     "Menus: sprite.draw(FILE_CACHE.getEntry(<file> << 16), ...).",
     "Copy the file to mods/<mod>/files/dat/<folder>/ with the same name. The read goes through the file cache, so it should be honoured; the "
     "image layer only takes over a file whose images were edited (the TM file), not the sheet.",
     "unverified"},
    {"card-opponents", "dat/card/card_npc.bin",
     "170 card-battle opponents of 0xD0 bytes (deck and card order); no table export yet (task E5d2).",
     "The card battle loads it with FILE_CACHE.load(FILE_CARDGAME_OPPONENTS).",
     "Copy the file to mods/<mod>/files/dat/card/ with the same name. The read goes through the file cache, so it should be honoured; no card "
     "battle was run with a replaced file.",
     "unverified"},
    {"card-pack", "dat/card/cardpack.bin",
     "316 contiguous CardImage records of 0x62C bytes (12 header bytes and a 32x32 8-bit TIM); almost the same series as CARDPAK0-4, which are "
     "exported as PNG (docs/formats.md 4.9).",
     "Nothing found: no code reads it by file number.",
     "Nothing to replace: the game does not read it. Edit the card art through cards/c<nnn>/face.png (dat/card/cardpak0-4).", "no"},
    {"text-unused", "dat/country/jpn/*.bin and dat/country/usa/*.bin (100 files)",
     "The 50 text files of the Japanese and the American versions, same format as the other languages (the text exporter writes JSON for those).",
     "Nothing: the EU build reads only the English, French, Italian, German and Spanish folders (TEXT_FILE).",
     "Nothing to replace: the game does not read them.", "no"},
    {"stage-overlay", "pro/wstag<nnn>.pro (one per field stage, 294 files)",
     "MIPS code of a field stage (docs/formats.md 4.2). The port links the decompiled stage code from src/ instead, so the file is not a data format.",
     "Nothing in the port: loadSubOverlay is a host stub that copies nothing.",
     "Nothing to replace: the port never reads them; change the stage in the C code.", "no"},
    {"mode-overlay", "pro/*.pro that are not stages (24 files)",
     "Mode overlays (FIELDSTG, FIGHTSTG, CARDGAME, STDWTITL, ...) are MIPS code the port links from src/. SMDLDATA.PRO is data, the fighters file "
     "(FILE_FIGHTERS, FighterInfo tables); SFSTDATA.PRO and SDIGIEDT.PRO are named like data files and were not examined.",
     "Code overlays: nothing in the port. SMDLDATA: the battle reads it through the file cache; the port also builds a patched copy of it for "
     "the content mods.",
     "Only SMDLDATA.PRO is read. A raw copy in mods/<mod>/files/pro/ replaces it whole, and the additions of the content mods to it are then not "
     "served (the log says so). The code overlays are not read.",
     "no"},
    {"filler", "dat/none____.bin",
     "One sector of padding on the disc.", "Nothing found.", "Nothing to replace.", "no"},
    {"other", "any other file under AAA/ that no exporter and no rule above names",
     "Not analysed. This kind exists so that nothing on the disc is left out unseen.", "Unknown.",
     "Copy it to mods/<mod>/files/ with the same path; whether the game reads it that way depends on the file.", "unverified"},
};
#define N_KINDS ((int)(sizeof KINDS / sizeof KINDS[0]))

const RawKind *rawKinds(int *n) {
    if (n != NULL) *n = N_KINDS;
    return KINDS;
}

static const RawKind *kindById(const char *id) {
    int i;
    for (i = 0; i < N_KINDS; i++) {
        if (strcmp(KINDS[i].id, id) == 0) return &KINDS[i];
    }
    return NULL;
}

static int startsWith(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }

static int endsWith(const char *s, const char *tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

/* "AAA/DAT/<dir>/<name>" -> 1 when the file sits directly in <dir> */
static int directlyIn(const char *p, const char *dir) {
    size_t n = strlen(dir);
    return startsWith(p, dir) && strchr(p + n, '/') == NULL;
}

static int isStageName(const char *p) {
    /* "AAA/PRO/WSTAG233.PRO" */
    size_t n = strlen(p);
    int i;
    if (!startsWith(p, "AAA/PRO/WSTAG") || n != strlen("AAA/PRO/WSTAG233.PRO")) return 0;
    for (i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)p[13 + i])) return 0;
    }
    return strcmp(p + 16, ".PRO") == 0;
}

const RawKind *rawKindOf(const char *p) {
    if (p == NULL || !startsWith(p, "AAA/")) return NULL; /* the executable, SYSTEM.CNF: not game data */
    /* covered by another exporter: video, audio, stage tiles, battle models, text (the five languages the EU game reads) */
    if (startsWith(p, "AAA/STR/") || startsWith(p, "AAA/DAT/SOUND/") || startsWith(p, "AAA/DAT/Z_STAGE/") || startsWith(p, "AAA/DAT/FIGHT/MODEL/")) {
        return NULL;
    }
    if (startsWith(p, "AAA/DAT/COUNTRY/")) {
        static const char *const live[] = {"ENG/", "FRA/", "GER/", "ITA/", "SPN/"};
        const char *rest = p + strlen("AAA/DAT/COUNTRY/");
        int i;
        for (i = 0; i < 5; i++) {
            if (startsWith(rest, live[i])) return NULL;
        }
        if (startsWith(rest, "JPN/") || startsWith(rest, "USA/")) return kindById("text-unused");
    }
    /* images: the same predicates the image exporter uses */
    if (sprIsSpriteFile(p) || imgIsScreenFile(p) || imgIsCardFile(p) || imgIsStageMask(p) || imgIsStageBg(p) || strcmp(p, "AAA/DAT/CARD/CARDDATA.BIN") == 0 ||
        strcmp(p, "AAA/DAT/FIELD/FIELDCOM.BIN") == 0) {
        return NULL;
    }
    if (directlyIn(p, "AAA/DAT/FIELD/STAGE/")) {
        if (endsWith(p, "TMPK.BIN")) return kindById("stage-map");
        if (endsWith(p, "DEMO.BIN")) return kindById("stage-demo");
    }
    if (startsWith(p, "AAA/DAT/FIGHT/EFFECT/")) return kindById("battle-effect");
    if (directlyIn(p, "AAA/DAT/FIGHT/")) return kindById("battle-common");
    if (startsWith(p, "AAA/DAT/TRAINING/TRANIN")) return kindById("training-data");
    if (endsWith(p, "CS.BIN") && (directlyIn(p, "AAA/DAT/SCREEN/") || directlyIn(p, "AAA/DAT/NAMEENT/") || directlyIn(p, "AAA/DAT/MCARD/") ||
                                  directlyIn(p, "AAA/DAT/DGLB/") || directlyIn(p, "AAA/DAT/TRAINING/") || directlyIn(p, "AAA/DAT/COUNTRY/"))) {
        return kindById("sprite-sheet");
    }
    if (strcmp(p, "AAA/DAT/CARD/CARD_NPC.BIN") == 0) return kindById("card-opponents");
    if (strcmp(p, "AAA/DAT/CARD/CARDPACK.BIN") == 0) return kindById("card-pack");
    if (isStageName(p)) return kindById("stage-overlay");
    if (startsWith(p, "AAA/PRO/")) return kindById("mode-overlay");
    if (strcmp(p, "AAA/DAT/NONE____.BIN") == 0) return kindById("filler");
    return kindById("other");
}

int rawRelPath(const char *discPath, char *out, size_t cap) {
    size_t i, n;
    if (discPath == NULL || !startsWith(discPath, "AAA/")) return 0;
    n = strlen(discPath) - 4;
    if (n == 0 || n >= cap || n >= 72) return 0; /* the manifest keeps paths up to 71 characters */
    for (i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)discPath[4 + i]);
    out[n] = '\0';
    return 1;
}

/* ---------------------------------------------------------------- the job */

typedef struct Entry {
    char path[104];      /* as on the disc */
    char rel[72];        /* under <data>/raw */
    long lba, size;
    int kind;            /* index into KINDS */
} Entry;

static char sDataDir[1024];
static RawExportIo sIo;
static RawExportStats sTotals;
static ExpManifest sMan;

static void manifestFree(void) { expManFree(&sMan); }

static void rawDir(char *out, size_t cap) { snprintf(out, cap, "%s/raw", sDataDir); }

static void manifestLoad(void) {
    char mp[1100], dir[1100];

    manifestFree();
    rawDir(dir, sizeof dir);
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    expManOpen(&sMan, "raw", mp, "raw");
}

/* the key of a kind's version in the manifest header */
static void markKey(const RawKind *k, char *out, size_t cap) { snprintf(out, cap, "%s", k->id); }

/* The file's bytes from the disc through the reader (malloc'ed, exactly `size`); NULL when a sector cannot be read */
static uint8_t *readFile(long lba, long size) {
    long sectors = (size + USER_BYTES - 1) / USER_BYTES, i;
    uint8_t *buf, frame[RAW_SECTOR];

    if (size <= 0) return NULL;
    buf = (uint8_t *)malloc((size_t)sectors * USER_BYTES);
    if (buf == NULL) return NULL;
    for (i = 0; i < sectors; i++) {
        if (!sIo.read(sIo.ud, lba + i, frame)) {
            free(buf);
            return NULL;
        }
        memcpy(buf + (size_t)i * USER_BYTES, frame + USER_AT, USER_BYTES);
    }
    return buf;
}

/* ---- the notes: README.txt and raw.json ---- */

typedef struct Sb {
    char *p;
    size_t len, cap;
} Sb;

static void sbPrintf(Sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sbPrintf(Sb *b, const char *fmt, ...) {
    va_list ap;
    int n;
    if (b->p == NULL) return;
    for (;;) {
        va_start(ap, fmt);
        n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (b->len + (size_t)n + 1 <= b->cap) {
            b->len += (size_t)n;
            return;
        }
        {
            size_t nc = b->cap * 2 + (size_t)n + 64;
            char *np = (char *)realloc(b->p, nc);
            if (np == NULL) {
                free(b->p);
                b->p = NULL;
                return;
            }
            b->p = np;
            b->cap = nc;
        }
    }
}

static void sbJson(Sb *b, const char *s) {
    sbPrintf(b, "\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') sbPrintf(b, "\\%c", *s);
        else if ((unsigned char)*s < 0x20) sbPrintf(b, " ");
        else sbPrintf(b, "%c", *s);
    }
    sbPrintf(b, "\"");
}

static int sbInit(Sb *b) {
    b->cap = 4096;
    b->len = 0;
    b->p = (char *)malloc(b->cap);
    if (b->p != NULL) b->p[0] = '\0';
    return b->p != NULL;
}

static char *buildReadme(const int *count, const long *bytes) {
    Sb b;
    int i;

    if (!sbInit(&b)) return NULL;
    sbPrintf(&b,
             "RAW COPIES OF DISC FILES\n"
             "========================\n\n"
             "This folder holds byte-exact copies of the files of your disc that the port has no open format for yet: the field collision maps,\n"
             "the battle effects, the training files, the stage demos, the sprite sheets, the overlays and a few more. Text, images, models,\n"
             "movies, sound and the tables are written as open files in the other folders of data/.\n\n"
             "Nothing in this folder is read by the game. To change one of these files, copy it to\n\n"
             "    mods/<your mod>/files/<the same path>        (for example mods/mymod/files/dat/field/stage/s233tmpk.bin)\n\n"
             "and edit the copy there. The game reads it from the mod (the last active mod wins); the disc is never touched. Names may be\n"
             "written in any case. raw.json says, per kind, what is known about the format, who reads the file and whether the port honours a\n"
             "replacement (\"works\": yes, no, unverified). Where the format is not known, treat the file as a black box: replace it with another\n"
             "file of the same layout.\n\n"
             "An edited file in this folder is never overwritten: a newer copy goes beside it as <file>.new.\n"
             "Set [data] export_raw in settings.ini to \"off\" to stop writing this folder, or to a list of kinds (stage-map, battle-effect, ...).\n\n"
             "Kinds on this disc\n"
             "------------------\n");
    for (i = 0; i < N_KINDS; i++) {
        if (count[i] == 0) continue;
        sbPrintf(&b, "%-15s %4d files %9ld bytes  %s\n", KINDS[i].id, count[i], bytes[i], KINDS[i].pattern);
    }
    sbPrintf(&b, "\nThe game's own data is yours: do not share these files.\n");
    return b.p;
}

static char *buildJson(const Entry *e, int n, const int *count, const long *bytes) {
    Sb b;
    int i, first = 1;

    if (!sbInit(&b)) return NULL;
    sbPrintf(&b, "{\n  \"schema\": \"%s\",\n  \"exporter\": %d,\n  \"kinds\": [\n", SCHEMA, RAW_EXPORT_VERSION);
    for (i = 0; i < N_KINDS; i++) {
        if (count[i] == 0) continue;
        sbPrintf(&b, "%s    {\"id\": ", first ? "" : ",\n");
        first = 0;
        sbJson(&b, KINDS[i].id);
        sbPrintf(&b, ", \"files\": %d, \"bytes\": %ld, \"pattern\": ", count[i], bytes[i]);
        sbJson(&b, KINDS[i].pattern);
        sbPrintf(&b, ",\n     \"know\": ");
        sbJson(&b, KINDS[i].know);
        sbPrintf(&b, ",\n     \"usedBy\": ");
        sbJson(&b, KINDS[i].usedBy);
        sbPrintf(&b, ",\n     \"replace\": ");
        sbJson(&b, KINDS[i].replace);
        sbPrintf(&b, ",\n     \"works\": \"%s\"}", KINDS[i].works);
    }
    sbPrintf(&b, "\n  ],\n  \"files\": [\n");
    for (i = 0; i < n; i++) {
        sbPrintf(&b, "    {\"path\": ");
        sbJson(&b, e[i].rel);
        sbPrintf(&b, ", \"size\": %ld, \"kind\": \"%s\"}%s\n", e[i].size, KINDS[e[i].kind].id, i + 1 < n ? "," : "");
    }
    sbPrintf(&b, "  ]\n}\n");
    return b.p;
}

/* ---- the request ---- */

/* want[k] = 1 for the kinds spec asks for; the number of valid kinds named, or -1 for "all" */
static int parseSpec(const char *spec, int *want) {
    const char *p = spec;
    int named = 0, k;

    for (k = 0; k < N_KINDS; k++) want[k] = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, "all", 3) && (p[3] == '\0' || p[3] == ' ' || p[3] == ',' || p[3] == '\t')) {
        for (k = 0; k < N_KINDS; k++) want[k] = 1;
        return -1;
    }
    while (*p) {
        const char *s;
        char tok[40];
        size_t len;
        const RawKind *kind;

        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        s = p;
        while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
        len = (size_t)(p - s);
        if (len >= sizeof tok) len = sizeof tok - 1;
        memcpy(tok, s, len);
        tok[len] = '\0';
        for (k = 0; (size_t)k < len; k++) tok[k] = (char)tolower((unsigned char)tok[k]);
        kind = kindById(tok);
        if (kind == NULL) {
            plogLine("raw", "export_raw: \"%s\" is not a kind (use all, off or kinds like stage-map separated by commas)\n", tok);
            continue;
        }
        if (!want[kind - KINDS]) named++;
        want[kind - KINDS] = 1;
    }
    return named;
}

void rawExportSetup(const char *dataDir, const RawExportIo *io) {
    manifestFree();
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

void rawExportTotals(RawExportStats *out) { *out = sTotals; }

/* a kind of the current version whose files are all there: nothing to build */
static int kindUpToDate(int k, const Entry *e, int n) {
    char key[40], dir[1100], path[1250];
    int i;

    markKey(&KINDS[k], key, sizeof key);
    if (!expManIsCurrent(&sMan, key, RAW_EXPORT_VERSION)) return 0;
    rawDir(dir, sizeof dir);
    for (i = 0; i < n; i++) {
        if (e[i].kind != k) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e[i].rel);
        if (!expFileExists(path)) return 0;
    }
    return 1;
}

int rawExportRun(const char *spec) {
    Entry *e = NULL;
    int want[MAX_KINDS], count[MAX_KINDS], failedKind[MAX_KINDS];
    long bytes[MAX_KINDS];
    int n = 0, i, k, nKinds = 0, rc = -1, notesOk = 1;
    char dir[1100];
    char *readme, *json;
    ExpStats es;
    ExpSink sink;

    memset(&sTotals, 0, sizeof sTotals);
    if (!sDataDir[0] || sIo.list == NULL || sIo.read == NULL || spec == NULL) return -1;
    if (parseSpec(spec, want) == 0) return -1;
    e = (Entry *)malloc(sizeof *e * MAX_FILES);
    if (e == NULL) return -1;
    for (k = 0; k < MAX_KINDS; k++) count[k] = failedKind[k] = 0, bytes[k] = 0;
    for (i = 0; n < MAX_FILES; i++) {
        Entry *x = &e[n];
        const RawKind *kind;

        if (!sIo.list(sIo.ud, i, x->path, sizeof x->path, &x->lba, &x->size)) break;
        kind = rawKindOf(x->path);
        if (kind == NULL || !rawRelPath(x->path, x->rel, sizeof x->rel)) continue;
        x->kind = (int)(kind - KINDS);
        count[x->kind]++;
        bytes[x->kind] += x->size;
        n++;
    }
    manifestLoad();
    memset(&es, 0, sizeof es);
    rawDir(dir, sizeof dir);
    expSinkInPlace(&sink, "raw", dir, &sMan, &es);
    for (k = 0; k < N_KINDS; k++) {
        if (!want[k] || count[k] == 0) continue;
        nKinds++;
        if (kindUpToDate(k, e, n)) {
            sTotals.skipped++;
            continue;
        }
        for (i = 0; i < n; i++) {
            uint8_t *data;

            if (e[i].kind != k) continue;
            sTotals.files++;
            data = readFile(e[i].lba, e[i].size);
            if (data == NULL) {
                plogLine("raw", "cannot read %s from the disc\n", e[i].path);
                sTotals.failed++;
                failedKind[k] = 1;
                continue;
            }
            if (expPut(&sink, e[i].rel, data, (size_t)e[i].size)) {
                sTotals.failed++;
                failedKind[k] = 1;
            }
            free(data);
        }
        if (!failedKind[k]) {
            char key[40];
            markKey(&KINDS[k], key, sizeof key);
            expManMark(&sMan, key, RAW_EXPORT_VERSION);
        }
    }
    sTotals.kinds = nKinds;
    if (nKinds == 0) {
        plogLine("raw", "export_raw: nothing of that kind is on this disc\n");
        goto done;
    }
    /* the notes describe the whole disc, whichever kinds this request named */
    readme = buildReadme(count, bytes);
    json = buildJson(e, n, count, bytes);
    if (readme == NULL || json == NULL || expPut(&sink, "README.txt", readme, strlen(readme)) ||
        expPut(&sink, "raw.json", json, strlen(json))) {
        plogLine("raw", "cannot write the notes of %s/raw\n", sDataDir);
        notesOk = 0;
    }
    free(readme);
    free(json);
    sTotals.written = es.written;
    sTotals.kept = es.kept;
    sTotals.newFiles = es.newFiles;
    sTotals.bytes = es.bytes;
    if (expManSave(&sMan, 0) || !notesOk) sTotals.failed++;
    plogLine("raw", "Done: %d kinds (%d up to date) in %s/raw, %d files looked at, %d written, %d kept, %d new beside edited, %d failed, %ld bytes\n",
             sTotals.kinds, sTotals.skipped, sDataDir, sTotals.files, sTotals.written, sTotals.kept, sTotals.newFiles, sTotals.failed, sTotals.bytes);
    rc = nKinds;
done:
    manifestFree();
    free(e);
    return rc;
}
