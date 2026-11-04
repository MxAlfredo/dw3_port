/*
 * Host harness (T15): the game's own boot path, on the host.
 *
 *     boot <disc.bin> [frames] [screenshot.png]
 *
 * This is where the port stops being a showcase: the program's entry point
 * calls the game's own main() (src/engine/system/main.c, renamed to
 * pcGameMain by the Makefile's -Dmain=pcGameMain, because the program still
 * needs an entry point of its own) and then gets out of the way. Everything
 * that follows is the game's:
 *
 *   * main sets the video mode, resets the graph, uploads the boot image,
 *     starts the heap, the sound, the pad, the memory card, the game state
 *     and the font, and enters its frame loop;
 *   * the loop creates the mode task, runs the task tree a frame at a time
 *     (task.c and the port's task registry), draws the frame and updates the
 *     file cache, the pad and the sound;
 *   * the first mode of the European version is the country select
 *     (GAME.funcs.newGame sets MODE_COUNTRY_SELECT), and its screen, menu,
 *     panels, cursor and background are the game's own country_select.c:
 *     the art comes from the owner's disc (CNTSELWN.BIN, file 0x8A3, the
 *     language menu's bank and TIM archive) through the game's own file
 *     cache and TIM loader, and the sprites are drawn by the game's own
 *     sprite_drawer.c into PsyCross's ordering table.
 *
 * What is the port's, and why (the dispatch):
 *
 *   * main() calls createModeTask and then TASK_REGISTRY.funcs.run, so the
 *     port has to provide both. This file provides createModeTask with its
 *     own entry-point table (PC_MODE_ENTRY_POINTS, below): the PlayStation's
 *     overlays are MIPS code loaded to fixed addresses and cannot be
 *     executed here, so the port compiles each mode's C into the host binary
 *     and dispatches on GAME.funcs.getMode() >> 8 instead. The game's own
 *     overlay.c (which would memcpy CNTY_SEL.PRO into OVERLAY_VRAM and jump
 *     into it) is not part of the host path, and the .PRO files are never
 *     read. Four overlays are linked here so far -- the country select, T18's
 *     title overlay STDWTITL (MODE_TITLE and everything else whose high byte
 *     is 0xE: it picks between the title screen, a movie or the still screen
 *     by the mode's low byte) and, T19, the field FIELDSTG (index 2: a new
 *     game, MODE_NEW_GAME 0x2D7, and the field modes 0x200/0x201 whose high
 *     byte is also 2 all start there; the field's own stage overlays, the
 *     WSTAG###.PRO files, are linked into that same entry point and picked at
 *     run time by the mode, so nothing is copied) and, T3, the battle mode
 *     FIGHTSTG (MODE_BATTLE 0x600: a nonzero argument picks the linked battle
 *     test WFIGHTTS with a random stage, zero picks the linked battle menu
 *     WFIGHTMN) -- so any other mode says so and stops.
 *   * OVERLAY_VRAM and STAGE_VRAM are the two linker symbols main.c turns
 *     into OVERLAY_ADDRESS/SUB_OVERLAY_ADDRESS (where the PS1's memory map
 *     puts the overlays and the boot image); the host has to define them,
 *     and the entry point's `GsGetTimInfo` reports no boot TIM there
 *     (shim/host_gs.c explains why).
 *
 * The demo ends on its own after the requested frames (main's loop is
 * `for (;;)` and never returns): the host's frame end saves the capture with
 * glReadPixels and exits. With no frames given it exits after 300. (T19 used
 * pcBootStopInMode to stop a few frames into the field, where its loader hit a
 * NULL FIELDSTG_state.slots; T22 removed that call -- the host guard in
 * pc/field/file_loader.c lets the field survive, so the demo runs its full
 * frame budget. The shim keeps pcBootStopInMode, but nothing arms it now.)
 *
 * The disc is the owner's own file: it is only read, and nothing of it is
 * written into the repository (ADR-001).
 */
#include "engine/game.h"
#include "engine/overlay.h"
#include "battle/battle_mode.h"
#undef OVL_NAME /* each overlay header defines its own */
#include "battle/report.h"
#undef OVL_NAME
#include "field/field_mode.h"
#include "menus/country_select.h"
#undef OVL_NAME
#include "menus/item_shop.h"
#undef OVL_NAME
#include "cardgame/card_packs.h" /* content-export E5d: the layouts game_tables.c mirrors */
#undef OVL_NAME
#undef CARD_GRID
#include "cardgame/card_battle.h"
#undef OVL_NAME
#include "menus/gym.h" /* content-export E5e: the layouts game_tables.c mirrors */
#include "shim/ext_state.h"
#include "shim/game_tables.h"
#include "shim/item_ext.h"
#include "shim/card_ext.h"
#include "shim/mod_tools.h"
#include "shim/mods.h"
#include "shim/save_view.h"
#include "shim/tech_ext.h"
#include "shim/digimon_ext.h"
#include "shim/text_codec.h"
#include "shim/text_data.h"
#include "shim/host.h"
#include "shim/options_system.h"
#include "shim/host_memcard.h"
#include "shim/model_bake.h"
#include "shim/model_export.h"
#include "shim/audio_export.h"
#include "shim/audio_mod.h"
#include "shim/video_export.h"
#include "shim/video_mod.h"
#include "shim/stage_export.h"
#include "shim/stage_mod.h"
#include "shim/stage_data.h"
#include "shim/stage_mods.h"
#include "shim/options_language.h"
#include "shim/raw_export.h"
#include "shim/image_data.h"
#include "shim/sprite_data.h"
#include "shim/port_fs.h"
#include "shim/port_log.h"
#include "shim/port_paths.h"
#ifdef __ANDROID__
#include "shim/l10n_install.h"
#include "shim/android_perms.h"
#include "shim/app_lifecycle_sdl.h"
#endif
#include "shim/state_scope.h"
#include "shim/table_mods.h"
#include "shim/disc_fd.h"

#include <PsyX/PsyX_public.h>

#if !defined(_WIN32) && !defined(__ANDROID__) /* Bionic has execinfo.h only from API 33 */
#include <execinfo.h>
#endif
#ifdef __ANDROID__
#include <android/log.h>
#include <dlfcn.h>
#include <unwind.h>
#include <ucontext.h>
#endif
#include <limits.h>
#include <stddef.h>
#include <signal.h>
#include <ctype.h>
#include <stdio.h>

/*
 * The other harnesses take exit by hand: the game's engine/game.h clashes
 * with <stdlib.h>. noreturn keeps the compiler from asking for a return after
 * the error paths below, which never come back.
 */
extern void exit(int status) __attribute__((noreturn));

/* Same clash for <stdlib.h>: boot reads one environment variable and exits. */
extern long strtol(const char *nptr, char **endptr, int base); /* also below */
extern char *getenv(const char *name);

/* How long the demo runs before it saves the capture and exits */
#define DEFAULT_FRAMES 300

/*
 * The game's own main, renamed by the Makefile so that this file can be the
 * program's entry point (a C program's entry point is called main; the
 * game's is the one that must run).
 */
extern int pcGameMain(void);
extern void pcItemGuardInstall(void); /* shim/text_fit.c: NULL-safe item category lookup */
/* L2b (shim/host_cd.c, shim/input_cfg.c): declared here like getenv above */
extern void pcCdTextInit(const char *dataDir, const char *l10nDir);
extern void free(void *); /* the game's headers clash with <stdlib.h> in this unit */
extern void *malloc(size_t size);
extern void *memset(void *s, int c, size_t n);
extern char *strrchr(const char *s, int c);
extern unsigned char *pcCdReadDiscFile(int file, size_t *len); /* shim/host_cd.c: the disc bytes of a file number */
/* U7: the [mods] order/disabled text as a heap string (no length limit; NULL when absent), freed by the caller */
extern char *inputCfgReadModsOrderAlloc(void);
extern int inputCfgReadExportModels(char *out, int cap);
extern int inputCfgTakeFlag(int *argc, char **argv, const char *flag); /* shim/input_cfg.c */
extern int inputCfgReadExportVideos(char *out, int cap);
extern int inputCfgReadExportAudio(char *out, int cap);
extern int inputCfgReadExportStages(char *out, int cap);
extern int inputCfgReadExportRaw(char *out, int cap);
extern int modsInit(const char *root, const char *order);
extern char *strcpy(char *dst, const char *src);
extern void pcCdSetVideoMod(const VideoModHooks *hooks); /* shim/host_cd.c */
extern void pcConfigLoad(const char *dataDir);

/*
 * The harness's own exit note. PsyX's only silent, clean shutdown is its
 * own SDL path (PsyX_Exit): the game window was closed, which ends the run
 * with exit code 0 and without the 'presented N frames' summary. Saying so
 * at exit keeps a shortened run's log from reading as an engine fault.
 * (Declared here, not in <stdlib.h>: the game's own headers shadow hostile
 * to the host's libc declarations in that header.)
 */
extern int atexit(void (*function)(void));
extern int strcmp(const char *a, const char *b); /* see above: <string.h> is not usable here */

/* extended-state T2, test harness only: DW3_ITEM_DUMP=1 prints every non-zero item count (bag and
   extension) when the run ends, so a save/load round trip can be compared between two processes */
static void pcItemDump(void) {
    const char *env = getenv("DW3_ITEM_DUMP");
    int id, n = 0;

    if (env == NULL || env[0] == '\0' || env[0] == '0') {
        return;
    }
    printf("[state] items:");
    for (id = 0; id < pcExtItemCapacity(); id++) {
        if (PC_ITEM_COUNT(id) != 0) {
            printf(" %d=%d", id, (int)PC_ITEM_COUNT(id));
            n++;
        }
    }
    printf(" (%d kinds)\n", n);
    {
        /* techniques of mods held by the partners' entries: partner.entry.slot=id(name) */
        int p, e, k;

        printf("[state] techs:");
        for (p = 0; p < PARTNER_COUNT; p++) {
            for (e = 0; e < PC_PARTNER_ENTRY_LIMIT(p); e++) {
                for (k = 0; k < 6; k++) {
                    int tech = PC_PARTNER_ENTRY(p, e).skills[k] & SKILL_ID;

                    if (tech >= PC_TECH_FIRST_EXT_ID) {
                        const char *name = techExtNameOf(tech);

                        printf(" %d.%d.%d=%d(%s)", p, e, k, tech, name != NULL ? name : "?");
                    }
                }
            }
        }
        printf("\n");
        /* new partner forms held by the partners' entries, picked forms and battle form: partner.entry=id(name) */
        printf("[state] forms:");
        for (p = 0; p < PARTNER_COUNT; p++) {
            const PartnerStats *ps = &GAME.partners[p].info;

            for (e = 0; e < PC_PARTNER_ENTRY_LIMIT(p); e++) {
                if (PC_PARTNER_ENTRY(p, e).id >= PC_DIGIMON_FIRST_NEW_ID) {
                    const char *name = digimonExtNewName(PC_PARTNER_ENTRY(p, e).id);

                    printf(" %d.%d=%d(%s)", p, e, (int)PC_PARTNER_ENTRY(p, e).id, name != NULL ? name : "?");
                }
            }
            for (k = 0; k < PARTNER_SLOT_COUNT; k++) {
                if (ps->slots[k] >= PC_DIGIMON_FIRST_NEW_ID) printf(" %d.slot%d=%d", p, k, (int)ps->slots[k]);
            }
            if (GAME.partners[p].battleDigivolve >= PC_DIGIMON_FIRST_NEW_ID) {
                printf(" %d.battle=%d", p, (int)GAME.partners[p].battleDigivolve);
            }
        }
        printf("\n");
        /* how many Digimon each partner holds, and the entries it can hold now (more than 44 once it holds enough) */
        printf("[state] entries:");
        for (p = 0; p < PARTNER_COUNT; p++) {
            int held = 0;

            for (e = 0; e < PC_PARTNER_ENTRY_LIMIT(p); e++) {
                if (PC_PARTNER_ENTRY(p, e).id >= FIRST_ENTRY_ID) held++;
            }
            if (held > 0) printf(" %d=%d(room %d)", p, held, PC_PARTNER_ENTRY_LIMIT(p));
        }
        printf("\n");
    }
}

/* The English entry `id` of a text file of the disc ("ITMNAM", "SKLNAM"), read from the disc once. Used by the readable save
   view and the content reference. NULL when the disc or the entry is missing. Overwritten by the next call. */
typedef struct DiscNames {
    const char *base;
    TcDoc doc;
    int tried, have;
} DiscNames;

static const char *pcDiscName(DiscNames *dn, int id) {
    static char name[96];
    char *text;

    if (!dn->tried) {
        int k;
        size_t len = 0;
        unsigned char *file = NULL;

        dn->tried = 1;
        for (k = 0; k < TD_FILES; k++) {
            if (strcmp(tdFileBase(k), dn->base) == 0) {
                file = pcCdReadDiscFile(tdIndexOf(TD_ENG, k), &len);
            }
        }
        if (file != NULL) {
            dn->have = tcDocParse(file, len, &dn->doc, NULL, 0) == 0 && !dn->doc.isPack;
            free(file);
        }
    }
    if (!dn->have || id < 0 || id >= dn->doc.lang.count) {
        return NULL;
    }
    text = tcEntryText(&dn->doc.lang, id);
    if (text == NULL) {
        return NULL;
    }
    snprintf(name, sizeof name, "%s", text);
    free(text);
    return name;
}

static DiscNames sItemNames = {"ITMNAM"};
static DiscNames sTechNames = {"SKLNAM"};
static DiscNames sDigNames = {"DIGNAM"};
static DiscNames sCardNames = {"CARDNM"};
static DiscNames sTrainNames = {"TRAINI"};
static DiscNames sInnNames = {"HTLNAM"};

/* extended-state T3b: the item names of the readable save view. Original items: the English entry of the item name
   file read from the disc (once); extended ones: "<mod>:<name>". Nothing is stored or published from here. */
static const char *pcViewItemName(int id) {
    if (id >= PC_ITEM_ORIG_COUNT) {
        return itemExtNameOf(id);
    }
    return pcDiscName(&sItemNames, id);
}

static const char *pcRefItemName(int id) {
    return pcDiscName(&sItemNames, id);
}

/* content-export E5d: a card's English name, CARDNM entry = the card id (the card shop reads it so) */
static const char *pcRefCardName(int id) {
    return pcDiscName(&sCardNames, id);
}

/* content-export E5e: a training's English name (TRAINI entry = TrainInfo.name) and an inn's (HTLNAM entry = InnInfo.string) */
static const char *pcRefTrainingName(int id) {
    return pcDiscName(&sTrainNames, id);
}

static const char *pcRefInnName(int id) {
    return pcDiscName(&sInnNames, id);
}

static const char *pcRefTechName(int id) {
    return pcDiscName(&sTechNames, id);
}

/* Digimon slice 1 D2: a fighter's English name, DIGNAM entry nameId (a partner form's or an enemy's) */
static const char *pcRefDigimonName(int id) {
    int nameId = digimonExtNameId(id);

    return nameId >= 0 ? pcDiscName(&sDigNames, nameId) : NULL;
}

/* content-core C2: data/content/<items|techs|digimon>.json, the original names ("dw3:item/<id>") with their English names (read from
   the disc now, never committed) and the fields a mod can set; same manifest and ".new" rule as the other data/ families */
static void pcExportContentReference(void) {
    char *text = contentReferenceText(itemExtType(), pcRefItemName);

    if (text != NULL) {
        tmExportFile("content", "items.json", text);
        free(text);
    }
    text = contentReferenceText(techExtType(), pcRefTechName);
    if (text != NULL) {
        tmExportFile("content", "techs.json", text);
        free(text);
    }
    text = contentReferenceText(cardExtType(), pcRefCardName);
    if (text != NULL) {
        tmExportFile("content", "cards.json", text);
        free(text);
    }
    text = contentReferenceText(digimonExtType(), pcRefDigimonName);
    if (text != NULL) {
        tmExportFile("content", "digimon.json", text);
        free(text);
    }
}

/* model-import I7: the original battle models as neutral sources in data/models/<id>/ (shim/model_export.c). The disc files come
   through the plain disc reader (no mod layers); the exporter copies each file before it asks for the next. */
static unsigned char *sExportFile;

static const unsigned char *pcExportDiscFile(void *ud, int file, size_t *len) {
    (void)ud;
    free(sExportFile);
    sExportFile = pcCdReadDiscFile(file, len);
    return sExportFile;
}

/* model-polish P3: a mod model's "from": "dw3:model/<id>" asks for an original fighter exported in memory. The baker calls this on the
   main thread (fighter-file construction), so the shared disc reader is safe; the export copies each disc file before it returns. */
static int pcModelOriginal(void *ud, int fighterId, ModelExportOut *out, char *err, size_t errCap) {
    ModelExportIo io;
    (void)ud;
    io.file = pcExportDiscFile;
    io.ud = NULL;
    return modelExportBuild(&io, fighterId, MODEL_EXPORT_UNIT_SCALE, out, err, errCap);
}

static int pcExportAllModels(void) {
    return modelExportStart("all", 1); /* the row is an explicit ask: rebuild (on a worker thread); the settings.ini key at start skips what is up to date */
}

/* the System tab's "Prepare modding files" row and --export-all: the same job, skipping what is up to date; and how many items failed */
static int pcPrepModels(void) {
    return modelExportStart("all", 0);
}

static int pcFailedModels(void) {
    int done, failed, total;

    modelExportCounts(&done, &failed, &total);
    return failed;
}

static void pcExportModelsPump(void) {
    modelExportPump(8); /* with a worker job running: feeds it the next fighters' disc files (the worker never reads the disc) */
}

static void pcInstallModelOriginals(void) {
    static const ModelOriginals originals = {pcModelOriginal, NULL};
    modelsSetOriginals(&originals);
}

static void pcInstallModelExport(const char *dataDir) {
    static const OptSysExport hooks = {pcExportAllModels, pcExportModelsPump, modelExportStatus, modelExportActive, pcPrepModels, pcFailedModels};
    ModelExportIo io;
    char spec[256];

    io.file = pcExportDiscFile;
    io.ud = NULL;
    modelExportSetup(dataDir, &io);
    atexit(modelExportShutdown); /* stops and joins the worker between two fighters; the files are written atomically */
    optionsSystemSetExport(&hooks); /* the System tab's "Export models" row; before the tab's rows are added */
    /* settings.ini [data] export_models = all | 383, 12: done now, before the window opens */
    if (dataDir != NULL && inputCfgReadExportModels(spec, (int)sizeof(spec)) && modelExportRequest(spec) > 0) {
        while (modelExportPump(1000)) {
        }
    }
}

/* content-export E2a: the movies as MJPEG AVI in data/video/ (shim/video_export.c). The exporter reads the raw sectors through a
   handle of its own (the game's reader and the XA/St pumps keep theirs, so a movie that is playing is not disturbed) and only the
   thread that runs the job calls it. The STR files come from the disc directory of port_fs (the same walk the other layers use). */
extern const char *pcCdDiscPath(void); /* shim/host_cd.c */
static FILE *sVideoDisc;

static int pcVideoMovie(void *ud, const char *name, long *lba, long *sectors) {
    char path[64];
    const PfsFile *f;
    (void)ud;
    snprintf(path, sizeof path, "AAA/STR/%s.STR", name);
    f = pfsFindPath(path);
    if (f == NULL || f->size <= 0) {
        return 0;
    }
    *lba = f->lba;
    *sectors = (f->size + 2047) / 2048;
    return 1;
}

static int pcVideoRead(void *ud, long lba, unsigned char *frame) {
    (void)ud;
    if (sVideoDisc == NULL) {
        const char *image = pcCdDiscPath();

        sVideoDisc = image != NULL ? dfdOpen(image) : NULL;
        if (sVideoDisc == NULL) {
            return 0;
        }
    }
    return fseek(sVideoDisc, lba * 2352L, SEEK_SET) == 0 && fread(frame, 1, 2352, sVideoDisc) == 2352;
}

/* content-export E2b: a mod's mods/<mod>/video/<NAME>.avi plays in place of the movie (shim/video_mod.c). The movie is told by the
   first sector of AAA/STR/<NAME>.STR, the mods are the active list; CdlSetloc asks (host_cd.c). */
static int pcVideoModLba(void *ud, const char *name, long *lba) {
    long sectors;
    (void)ud;
    return pcVideoMovie(NULL, name, lba, &sectors);
}

static int pcVideoModCount(void *ud) {
    (void)ud;
    return modsCount();
}

static const char *pcVideoModName(void *ud, int i) {
    (void)ud;
    return modsNameOf(i);
}

static const char *pcVideoModDir(void *ud, int i) {
    (void)ud;
    return modsDirOf(i);
}

static void pcInstallVideoMod(void) {
    static VideoModIo io;

    io.movieLba = pcVideoModLba;
    io.modCount = pcVideoModCount;
    io.modName = pcVideoModName;
    io.modDir = pcVideoModDir;
    io.ud = NULL;
    videoModSetup(&io);
    pcCdSetVideoMod(videoModHooks());
}

static int pcExportAllVideos(void) {
    return videoExportStart("all", 1); /* the row is an explicit ask: rebuild; the settings.ini key at start skips what is up to date */
}

/* the System tab's "Prepare modding files" row and --export-all: the same job, skipping what is up to date; and how many items failed */
static int pcPrepVideos(void) {
    return videoExportStart("all", 0);
}

static int pcFailedVideos(void) {
    int done, failed, total;

    videoExportCounts(&done, &failed, &total);
    return failed;
}

static void pcExportVideosPump(void) {
    videoExportPump(8); /* with a worker job running it only joins the worker once it is done */
}

static void pcInstallVideoExport(const char *dataDir) {
    static const OptSysExport hooks = {pcExportAllVideos, pcExportVideosPump, videoExportStatus, videoExportActive, pcPrepVideos, pcFailedVideos};
    VideoExportIo io;
    char spec[256];

    io.movie = pcVideoMovie;
    io.read = pcVideoRead;
    io.ud = NULL;
    videoExportSetup(dataDir, &io);
    atexit(videoExportShutdown); /* stops and joins the worker between two sectors; the partial file is removed */
    optionsSystemSetExportVideos(&hooks); /* the System tab's "Export videos" row; before the tab's rows are added */
    /* settings.ini [data] export_videos = all | MOVIED01, MOVIEOPN: done now, before the window opens */
    if (dataDir != NULL && inputCfgReadExportVideos(spec, (int)sizeof(spec)) && videoExportRequest(spec) > 0) {
        while (videoExportPump(1000)) {
        }
    }
}

/* content-export E6b: the field stage tiles as PNG and JSON in data/z_stage/ (shim/stage_export.c) and a mod's PNG in place of a tile (shim/stage_mod.c).
   The PACKs are AAA/DAT/Z_STAGE/S<nnn>PACK.BIN of the disc directory of port_fs. The exporter reads the raw sectors through a handle of its own and
   only the thread that runs the job calls it. */
#define PC_STAGE_DIR "AAA/DAT/Z_STAGE/"
static FILE *sStageDisc;

static int pcStagePack(void *ud, int i, char *name, size_t cap, long *lba, long *size) {
    const PfsFile *v[1024];
    const PfsFile *f;
    int n = pfsList(PC_STAGE_DIR, v, 1024);
    char *dot;
    (void)ud;
    if (i < 0 || i >= n) {
        return 0;
    }
    f = v[i];
    snprintf(name, cap, "%s", strrchr(f->path, '/') != NULL ? strrchr(f->path, '/') + 1 : f->path);
    dot = strrchr(name, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    *lba = f->lba;
    *size = f->size;
    return 1;
}

static int pcStageRead(void *ud, long lba, unsigned char *frame) {
    (void)ud;
    if (sStageDisc == NULL) {
        const char *image = pcCdDiscPath();

        sStageDisc = image != NULL ? dfdOpen(image) : NULL;
        if (sStageDisc == NULL) {
            return 0;
        }
    }
    return fseek(sStageDisc, lba * 2352L, SEEK_SET) == 0 && fread(frame, 1, 2352, sStageDisc) == 2352;
}

static int pcExportAllStages(void) {
    return stageExportStart("all", 1); /* the row is an explicit ask: rebuild; the settings.ini key at start skips what is up to date */
}

/* the System tab's "Prepare modding files" row and --export-all: the same job, skipping what is up to date; and how many items failed */
static int pcPrepStages(void) {
    return stageExportStart("all", 0);
}

static int pcFailedStages(void) {
    int done, failed, total;

    stageExportCounts(&done, &failed, &total);
    return failed;
}

static void pcExportStagesPump(void) {
    stageExportPump(8); /* with a worker job running it only joins the worker once it is done */
}

/* the stage mod: the PACK that starts at `lba` (its lower-case key, "s233pack") and its header sector, from the disc directory */
static int pcStageModPack(void *ud, long lba, char *name, size_t cap) {
    const PfsFile *v[1024];
    int n = pfsList(PC_STAGE_DIR, v, 1024), i;
    (void)ud;
    for (i = 0; i < n; i++) {
        if (v[i]->lba == lba) {
            const char *base = strrchr(v[i]->path, '/') != NULL ? strrchr(v[i]->path, '/') + 1 : v[i]->path;
            char *dot;
            size_t k;

            snprintf(name, cap, "%s", base);
            dot = strrchr(name, '.');
            if (dot != NULL) {
                *dot = '\0';
            }
            for (k = 0; name[k] != '\0'; k++) {
                name[k] = (char)tolower((unsigned char)name[k]);
            }
            return 1;
        }
    }
    return 0;
}

static int pcStageModHeader(void *ud, long lba, uint8_t *sector) {
    uint8_t *b = pfsDiscRead(lba, 2048);
    (void)ud;
    if (b == NULL) {
        return 0;
    }
    memcpy(sector, b, 2048);
    free(b);
    return 1;
}

static void pcInstallStages(const char *dataDir) {
    static const OptSysExport hooks = {pcExportAllStages, pcExportStagesPump, stageExportStatus, stageExportActive, pcPrepStages, pcFailedStages};
    static const StageModIo modIo = {pcStageModPack, pcStageModHeader, NULL};
    StageExportIo io;
    char spec[256];

    stageModSetup(dataDir, &modIo); /* the hook in pc/field/stream.c asks it for every tile */
    io.pack = pcStagePack;
    io.read = pcStageRead;
    io.ud = NULL;
    stageExportSetup(dataDir, &io);
    atexit(stageExportShutdown); /* stops and joins the worker between two tiles; the files are written atomically */
    optionsSystemSetExportStages(&hooks); /* the System tab's "Export stages" row; before the tab's rows are added */
    /* settings.ini [data] export_stages = all | S233, S330: done now, before the window opens */
    if (dataDir != NULL && inputCfgReadExportStages(spec, (int)sizeof(spec)) && stageExportRequest(spec) > 0) {
        while (stageExportPump(1000)) {
        }
    }
}

/* port-close S2: every field stage as data (shim/stage_data.c, docs/stages.md). Each stage's own setupStage() fills FIELDSTG_state with what the stage
   is made of; it is run here, before the first mode, once for every story progress 0-99 to see what depends on it, and the progress-0 run is read
   into an SdStage and written to <data>/stages/<nnn>.json. The game's state around it is saved and restored (setupStage sets FIELDSTG_state, the
   map files and GAME.mapIndex). Skipped when the folder holds the current exporter version. */

/* The rows of SPECIAL_CONDITIONS (and a mod's), for the symbolic comments of the codes */
static int pcSpecialRow(void *ud, int id, int *check, int *arg) {
    (void)ud;
    return gameSpecialRow(id, check, arg);
}

/* One text file of the language the export runs in, kept while its entries are read (the disc layers hand out a shared buffer) */
static uint8_t *sStageTextBuf;
static size_t sStageTextLen;
static int sStageTextFile = -1;

static void pcStageTextDrop(void) {
    free(sStageTextBuf);
    sStageTextBuf = NULL;
    sStageTextLen = 0;
    sStageTextFile = -1;
}

/* The line `entry` of text file `file` as UTF-8 in the readable token syntax, or NULL (malloc'ed). `block` -1: the file is the lines (a stage's text file);
   a number: the file is an archive and the lines are the block of that entry (the story events' file: FILE_CACHE.getEntry(eventText) is the block, and a
   talk command's entry counts its lines). */
static char *pcStageLineIn(int file, int block, int entry) {
    const uint8_t *b;
    unsigned count, off, end, base = 0;
    char *dec, *pretty;

    if (file != sStageTextFile) {
        size_t len = 0;
        const uint8_t *src = pfsResolve(file, &len, 0, NULL); /* a mod's or the es-419 text, else the disc's */

        pcStageTextDrop();
        if (src != NULL) {
            sStageTextBuf = (uint8_t *)malloc(len > 0 ? len : 1);
            if (sStageTextBuf != NULL) {
                memcpy(sStageTextBuf, src, len);
            }
        } else {
            /* the disc's file: its sectors through the plain disc reader (no mod layers) */
            int sectors = (int)FILE_TABLE.getSectorCount(file), first = (int)FILE_TABLE.getSector(file), i;
            unsigned char frame[2352];

            if (FILE_TABLE.exists(file) && sectors > 0 && sectors < 4096 && first > 0) {
                sStageTextBuf = (uint8_t *)malloc((size_t)sectors * 2048);
                for (i = 0; sStageTextBuf != NULL && i < sectors; i++) {
                    if (!pcStageRead(NULL, (long)first + i, frame)) {
                        pcStageTextDrop();
                        break;
                    }
                    memcpy(sStageTextBuf + (size_t)i * 2048, frame + 24, 2048);
                }
                len = (size_t)sectors * 2048;
            }
        }
        if (sStageTextBuf == NULL || len < 8) {
            pcStageTextDrop();
            return NULL;
        }
        sStageTextLen = len;
        sStageTextFile = file;
    }
    b = sStageTextBuf;
    if (block >= 0) { /* the archive's table: one offset per entry, from the file's start */
        if (4 * (size_t)block + 4 > sStageTextLen) {
            return NULL;
        }
        base = (unsigned)b[4 * block] | (unsigned)b[4 * block + 1] << 8 | (unsigned)b[4 * block + 2] << 16 | (unsigned)b[4 * block + 3] << 24;
        if ((size_t)base + 8 > sStageTextLen) {
            return NULL;
        }
        b += base;
    }
    count = (unsigned)b[0] | (unsigned)b[1] << 8 | (unsigned)b[2] << 16 | (unsigned)b[3] << 24;
    if (entry < 0 || (unsigned)entry >= count || (size_t)base + 4 + 4 * (size_t)entry + 4 > sStageTextLen) {
        return NULL;
    }
    off = (unsigned)b[4 + 4 * entry] | (unsigned)b[5 + 4 * entry] << 8 | (unsigned)b[6 + 4 * entry] << 16 | (unsigned)b[7 + 4 * entry] << 24;
    if ((size_t)base + off >= sStageTextLen) {
        return NULL;
    }
    off += base;
    b = sStageTextBuf;
    for (end = off; end < sStageTextLen && b[end] != 0; end++) {
    }
    dec = tcDecode(b + off, end - off);
    if (dec == NULL) {
        return NULL;
    }
    pretty = tcPrettify(dec);
    free(dec);
    return pretty;
}

static char *pcStageLine(int file, int entry) {
    return pcStageLineIn(file, -1, entry);
}

/* What setupStage hands over, as comparable words, to see what changes with the story progress */
#define PC_FP_COUNT 17
static const char *const PC_FP_NAMES[PC_FP_COUNT] = {"text file", "map file", "sheet", "objects", "slots", "image entry", "image file", "battles", "events",
                                                     "images", "start", "start direction", "sprite color", "sound bank", "music", "actors", "map files"};

static void pcStageFingerprint(uintptr_t *f) {
    const FieldState *s = &FIELDSTG_state;
    int i;

    f[0] = (uintptr_t)s->textFile;
    f[1] = (uintptr_t)s->mapFile;
    f[2] = (uintptr_t)s->sheetEntry;
    f[3] = (uintptr_t)s->objects;
    f[4] = (uintptr_t)s->slots;
    f[5] = (uintptr_t)s->imageEntry;
    f[6] = (uintptr_t)s->imageFile;
    f[7] = (uintptr_t)s->battles;
    f[8] = (uintptr_t)s->events;
    f[9] = (uintptr_t)s->images.actors;
    f[10] = (uintptr_t)s->start.x * 31u + (uintptr_t)s->start.y;
    f[11] = (uintptr_t)s->startDir;
    f[12] = (uintptr_t)s->spriteColor.r | (uintptr_t)s->spriteColor.g << 8 | (uintptr_t)s->spriteColor.b << 16 | (uintptr_t)s->spriteColor.cd << 24;
    f[13] = (uintptr_t)s->soundBank;
    f[14] = (uintptr_t)s->music;
    f[15] = (uintptr_t)s->actors;
    f[16] = 0;
    for (i = 0; i < 8; i++) {
        f[16] = f[16] * 31u + (uintptr_t)FIELDSTG_map.files[i];
    }
}

/* The modes of the stage tables that run this stage's startStage */
static void pcStageModes(const SdSetup *su, SdStage *s) {
    const StageEntry *t;
    int guard;

    for (t = FIELDSTG_extraStages, guard = 0; t != FIELDSTG_stages && t->mode != 0 && guard < 512; t++, guard++) {
        if (su->start != NULL && (const void *)t->init == (const void *)su->start && s->nmodes < 4) {
            s->modes[s->nmodes++] = t->mode;
        }
    }
    for (t = FIELDSTG_stages, guard = 0; t->mode != 0 && guard < 1024; t++, guard++) {
        int i, have = 0;

        if (su->start == NULL || (const void *)t->init != (const void *)su->start) {
            continue;
        }
        for (i = 0; i < s->nmodes; i++) {
            have |= s->modes[i] == t->mode;
        }
        if (!have && s->nmodes < 4) {
            s->modes[s->nmodes++] = t->mode;
        }
    }
}

/* The words of an event script that show a line: the talk commands' entries (kind 2) */
static void pcEventPreviews(SdEvent *e, int file, int block) {
    int at = 0, i;

    while (at < e->nscript) {
        int w = (uint16_t)e->script[at], kind = (w >> 8) & 0xFF;

        if (kind == 2 && at + 5 <= e->nscript) {
            int entry = (uint16_t)e->script[at + 2], have = 0;
            char *line;

            for (i = 0; i < e->npreviews; i++) {
                have |= e->previews[i].entry == entry;
            }
            if (!have && (line = pcStageLineIn(file, block, entry)) != NULL) {
                sdEventAddPreview(e, entry, line);
                free(line);
            }
            at += 5;
        } else if (kind == 1) {
            at += (w & 0xFF) == 2 ? 5 : 4;
        } else if (kind == 3) {
            at += (w & 0xFF) == 1 ? 1 : (w & 0xFF) == 4 ? 5 : 2;
        } else if (kind == 6) {
            at += (w & 0xFF) == 1 ? 4 : 3;
        } else {
            break;
        }
    }
}

/* FieldState.slots (the exits, up to the first type 0) into the model */
static void pcStageReadSlots(const FieldState *st, SdStage *s) {
    int i, j;
    const StageSlot *slot;

    for (slot = st->slots, i = 0; slot != NULL && slot->type != 0 && i < 512; slot++, i++) {
        SdSlot *x;
        int pairs = 0;

        if (slot->type > 14) {
            /* not a slot (SLOT_EXIT..SLOT_LAUNCH_OUT): a table of the source without its type-0 end row (WSTAG530 and WSTAG615's first one) runs on
               into whatever follows it in memory, so the read stops at the first record that cannot be a slot */
            sdStageAddNote(s, "the slot table has no end row in the source: the export stops at its last valid record.");
            break;
        }
        x = sdStageAddSlot(s);
        if (x == NULL) {
            break;
        }
        x->facing = -1;
        x->type = slot->type;
        x->arg = slot->arg;
        x->x = slot->x;
        x->y = slot->y;
        x->dir = slot->dir;
        x->hideAnim = slot->hideAnim;
        x->place = slot->place;
        x->placeArg = slot->placeArg;
        /* two (code, value) pairs; the first code 0xFFFF means none */
        while (pairs < 2 && slot->conditions[pairs][0] != 0xFFFF) {
            pairs++;
        }
        if (pairs > 0) {
            uint16_t w[5];

            for (j = 0; j < pairs; j++) {
                w[j * 2] = slot->conditions[j][0];
                w[j * 2 + 1] = slot->conditions[j][1];
            }
            w[pairs * 2] = 0xFFFF;
            sdListFromCodes(&x->cond, w, pairs);
        }
    }
}

/* FieldState.events (up to the first id -1) into the model, a script with the lines of its talks as previews */
static void pcStageReadEvents(const FieldState *st, SdStage *s) {
    int i, language = (int)LANGUAGE;
    const FieldEvent *ev;

    for (ev = st->events, i = 0; ev != NULL && ev->id != -1 && i < 1024; ev++, i++) {
        SdEvent *x = sdStageAddEvent(s);

        if (x == NULL) {
            break;
        }
        x->id = ev->id;
        if (ev->text != 0) {
            x->hasText = 1;
            x->textFile = (ev->text >> 16) & 0xFFFF;
            x->textEntry = ev->text & 0xFFFF;
        }
        x->hasStart = ev->start != NULL;
        x->hasEnd = ev->end != NULL;
        if (ev->script != NULL) {
            sdEventSetScript(x, ev->script);
            if (x->hasText) {
                pcEventPreviews(x, x->textFile + language, x->textEntry);
            }
        }
    }
}

/* FieldState.battles (the area battles of the place the setup chose, S10) into the model: four areas of a rate and eight battles; none when the stage has none */
static void pcStageReadBattles(const FieldState *st, SdStage *s) {
    const FieldBattles *fb = st->battles;
    int i, k;

    if (fb == NULL) {
        return;
    }
    /* A setup that picks its list by GAME.place (findBattles) walks 30 records past the table when the place has none, and a zeroed neighbour can match: a
       real list has its number (1 to 448) and the id asked for, anything else is not read (the lists' pointers are only followed after that). */
    if (fb->serial < 1 || fb->serial > 448 || fb->id != GAME.place) {
        sdStageAddNote(s, "battles: the setup picks them by the place (GAME.place) and has none for the place this file was made with (0); a patch sees the real place.");
        return;
    }
    s->battles.present = 1;
    s->battles.place = fb->id;
    s->battles.serial = fb->serial;
    s->battles.nareas = 4;
    for (i = 0; i < 4; i++) {
        const BattleList *l = fb->battles[i];

        if (l == NULL) {
            continue;
        }
        s->battles.areas[i].rate = l->count;
        for (k = 0; k < 8; k++) {
            if (l->battles[k] != NULL) {
                s->battles.areas[i].b[k].encounter = l->battles[k]->encounter;
                s->battles.areas[i].b[k].stage = l->battles[k]->stage;
                s->battles.areas[i].b[k].music = l->battles[k]->music;
            }
        }
    }
}

/* FieldState.actors (the characters and their talks) into the model; with `withText` each talk keeps its line as a preview. *maxImage is raised to the
   highest actor image any of them uses. */
static void pcStageReadActors(const FieldState *st, SdStage *s, int withText, int *maxImage) {
    int i, j;

    if (st->actors != NULL) {
        for (i = 0; st->actors[i] != NULL && i < 256; i++) {
            const FieldActorEntry *a = st->actors[i];
            SdActor *x = sdStageAddActor(s);
            const FieldTalk *t;

            if (x == NULL) {
                break;
            }
            x->character = a->id;
            x->image = a->image;
            x->x = a->x;
            x->y = a->y;
            x->dir = a->dir;
            if (a->image > (*maxImage)) {
                (*maxImage) = a->image;
            }
            sdListFromCodes(&x->cond, a->conditions, 64);
            for (t = a->talks, j = 0; t != NULL && j < 64; t++, j++) {
                SdTalk *y = sdActorAddTalk(x);
                char *line;

                if (y == NULL) {
                    break;
                }
                y->entry = t->entry;
                sdListFromCodes(&y->cond, t->conditions, 64);
                sdListFromCodes(&y->actions, t->actions, 64);
                if (withText && st->textFile != 0 && (line = pcStageLine(st->textFile, t->entry)) != NULL) {
                    sdTalkSetPreview(y, line);
                    free(line);
                }
                if (t->conditions == NULL) {
                    break; /* the last talk */
                }
            }
        }
    }
}

/* FIELDSTG_state, as the stage just set it up, into the model */
static void pcStageReadState(const SdSetup *su, SdStage *s) {
    const FieldState *st = &FIELDSTG_state;
    int i, maxImage = 1, language = (int)LANGUAGE;
    const StageTile *tile;

    s->textFile = st->textFile != 0 ? st->textFile - language : -1;
    s->mapFile = st->mapFile;
    s->sheetEntry = st->sheetEntry;
    s->imageEntry = st->imageEntry;
    s->imageFile = st->imageFile;
    s->soundBank = st->soundBank;
    s->music = st->music;
    s->startX = st->start.x;
    s->startY = st->start.y;
    s->startDir = st->startDir;
    s->spriteColor[0] = st->spriteColor.r;
    s->spriteColor[1] = st->spriteColor.g;
    s->spriteColor[2] = st->spriteColor.b;
    s->spriteColor[3] = st->spriteColor.cd;
    for (tile = st->objects, i = 0; tile != NULL && tile->margin != 0 && i < 4096; tile++, i++) {
        s->nobjects++;
    }
    pcStageReadActors(st, s, 1, &maxImage);
    if (st->images.actors != NULL) {
        for (i = 2; i <= maxImage && i < 64; i++) {
            const ActorImage *m = &st->images.actors[i];
            SdImage *x = sdStageAddImage(s);

            if (x == NULL) {
                break;
            }
            x->x = m->x;
            x->y = m->y;
            x->imageX = m->imageX;
            x->imageY = m->imageY;
            x->u = m->u;
            x->v = m->v;
            x->clutX = m->clutX;
            x->clutY = m->clutY;
        }
    }
    pcStageReadSlots(st, s);
    pcStageReadEvents(st, s);
    pcStageReadBattles(st, s);
    (void)su;
}

/* The comma separated C functions of a stage, into the model */
static void pcStageFunctions(const SdSetup *su, SdStage *s) {
    const char *p = su->functions;

    while (p != NULL && *p != '\0') {
        char name[96];
        size_t n = 0;

        while (p[n] != '\0' && p[n] != ',') {
            n++;
        }
        if (n > 0 && n < sizeof name) {
            memcpy(name, p, n);
            name[n] = '\0';
            sdStageAddFunction(s, name);
        }
        p += n + (p[n] == ',' ? 1 : 0);
    }
}

static void pcStageNotes(const SdSetup *su, SdStage *s, const int *varies) {
    char note[600];
    size_t at;
    int i, any = 0;

    if (su->setup == NULL) {
        sdStageAddNote(s, "WSTAG260 is the field's own stage (mode 528): FIELDSTG_setupField sets it up and FIELDSTG_createStoryEvents starts it, the overlay "
                          "holds only the story events' scripts.");
    }
    if (s->nfunctions > 0) {
        snprintf(note, sizeof note, "C code: %d functions besides the setup and start (see \"functions\"): their animations, tasks and cutscene code are not data.",
                 s->nfunctions);
        sdStageAddNote(s, note);
    }
    at = (size_t)snprintf(note, sizeof note, "setup varies with the story progress: ");
    for (i = 0; varies != NULL && i < PC_FP_COUNT; i++) {
        if (varies[i] && at < sizeof note - 40) {
            at += (size_t)snprintf(note + at, sizeof note - at, "%s%s", any ? ", " : "", PC_FP_NAMES[i]);
            any = 1;
        }
    }
    if (any) {
        snprintf(note + at, sizeof note - at, " (this file is progress 0).");
        sdStageAddNote(s, note);
    }
    /* the events that run C: one note for the tasks they start, one for the functions that end them */
    {
        int pass;

        for (pass = 0; pass < 2; pass++) {
            size_t n = 0;
            int count = 0;

            n = (size_t)snprintf(note, sizeof note, pass == 0 ? "events that start a C task (their behaviour is not data):" : "events that run a C function when they end:");
            for (i = 0; i < s->nevents; i++) {
                int has = pass == 0 ? (s->events[i].hasStart && !s->events[i].hasScript) : s->events[i].hasEnd;

                if (has && n < sizeof note - 12) {
                    n += (size_t)snprintf(note + n, sizeof note - n, "%s %d", count ? "," : "", s->events[i].id);
                    count++;
                }
            }
            if (count > 0) {
                sdStageAddNote(s, note);
            }
        }
    }
}

extern const StateScopeRange PC_STAGE_SCOPE_RANGES[]; /* build/stage_scopes.c */

/* The writable statics of the field and its stages (the state scopes' ranges), copied before the setups run and put back after: a setupStage
   may fill the tables it points to (the slots' place points), and the state scopes take their initial values from memory later. */
typedef struct PcStaticsCopy {
    char *buf;
    size_t total;
} PcStaticsCopy;

static size_t pcStaticsWalk(char *copy, int restore, size_t *changed) {
    extern char __ovl_fieldstg_data_start[], __ovl_fieldstg_data_end[];
    const StateScopeRange *r;
    size_t at = 0, n;
    char *begin, *end;
    int pass;

    for (pass = 0;; pass++) {
        if (pass == 0) {
            begin = __ovl_fieldstg_data_start;
            end = __ovl_fieldstg_data_end;
        } else {
            if (pass == 1) {
                r = PC_STAGE_SCOPE_RANGES;
            } else {
                r++;
            }
            if (r->name == NULL) {
                break;
            }
            begin = r->begin;
            end = r->end;
        }
        n = (size_t)(end - begin);
        if (copy != NULL) {
            if (restore) {
                size_t k;

                for (k = 0; changed != NULL && k < n; k++) {
                    *changed += begin[k] != copy[at + k];
                }
                memcpy(begin, copy + at, n);
            } else {
                memcpy(copy + at, begin, n);
            }
        }
        at += n;
    }
    return at;
}

static void pcExportStageData(const char *dataDir) {
    static const StageCodeEnv env = {NULL, NULL, pcSpecialRow, NULL};
    static FieldState savedState;
    static FieldMap savedMap;
    static GameState savedGame;
    SdExportStats st;
    SdCounts total;
    int numbers[512], count = 0, done = 0, failed = 0, mismatches = 0, savedLanguage = 0, savedVariant = 0, setLanguage = 0, setVariant = 0;
    PcStaticsCopy statics = {NULL, 0};
    size_t staticsChanged = 0;
    long lists = 0, pairs = 0, scripts = 0, words = 0, talks = 0, actors = 0, slotsTotal = 0, events = 0;
    const SdSetup *su;

    if (dataDir == NULL) {
        return;
    }
    sdExportSetup(dataDir);
    for (su = SD_SETUPS; su->name != NULL && count < 512; su++) {
        numbers[count++] = su->number;
    }
    if (sdExportUpToDate(numbers, count)) {
        return;
    }
    memset(&st, 0, sizeof st);
    memset(&total, 0, sizeof total);
    /* the language the player reads: the disc layers serve its text files and setupStage reads LANGUAGE */
    {
        int choice = optLangEffective(optLangChoice(), optLangEs419Offered());
        int lang = optLangGameValue(choice);

        setLanguage = lang > 0 ? lang : 2;
        setVariant = optLangVariant(choice) ? TD_VARIANT_ES419 : TD_VARIANT_SPA;
    }
    savedLanguage = (int)LANGUAGE;
    savedVariant = tdGetVariant();
    memcpy(&savedState, &FIELDSTG_state, sizeof savedState);
    memcpy(&savedMap, &FIELDSTG_map, sizeof savedMap);
    memcpy(&savedGame, &GAME, sizeof savedGame);
    statics.total = pcStaticsWalk(NULL, 0, NULL);
    statics.buf = (char *)malloc(statics.total > 0 ? statics.total : 1);
    if (statics.buf != NULL) {
        pcStaticsWalk(statics.buf, 0, NULL);
    }
    LANGUAGE = setLanguage;
    if (setVariant != savedVariant) {
        tdSetVariant(setVariant);
    }
    for (su = SD_SETUPS; su->name != NULL; su++) {
        SdStage s;
        uintptr_t base[PC_FP_COUNT], now[PC_FP_COUNT];
        int varies[PC_FP_COUNT], progress, k;
        char *json;
        SdStage back;
        char err[300];
        SdCounts c;

        void (*setup)(void) = su->setup;

        memset(&s, 0, sizeof s);
        memset(varies, 0, sizeof varies);
        if (setup == NULL && su->number == 260) {
            setup = FIELDSTG_setupField; /* the field's own stage: mode 528, set up by FIELDSTG itself */
            s.modes[s.nmodes++] = 528;
        }
        s.number = su->number;
        snprintf(s.name, sizeof s.name, "%s", su->name);
        pcStageModes(su, &s);
        if (setup != NULL) {
            for (progress = 99; progress >= 0; progress--) {
                memcpy(&FIELDSTG_state, &savedState, sizeof savedState);
                memcpy(&FIELDSTG_map, &savedMap, sizeof savedMap);
                memset(&FIELDSTG_state, 0, offsetof(FieldState, defaultStart));
                GAME.progress = progress;
                setup();
                if (progress == 99) {
                    pcStageFingerprint(base);
                } else {
                    pcStageFingerprint(now);
                    for (k = 0; k < PC_FP_COUNT; k++) {
                        if (now[k] != base[k]) {
                            varies[k] = 1;
                        }
                    }
                    if (progress == 0) {
                        break; /* the state of this run is the one read below */
                    }
                }
            }
            pcStageReadState(su, &s);
        }
        pcStageFunctions(su, &s);
        pcStageNotes(su, &s, varies);
        json = sdStageToJson(&env, &s);
        sdStageCount(&s, &c);
        lists += c.lists;
        pairs += c.pairs;
        scripts += c.scripts;
        words += c.words;
        talks += c.talks;
        actors += s.nactors;
        slotsTotal += s.nslots;
        events += s.nevents;
        if (json == NULL) {
            failed++;
        } else {
            /* the file must read back to the same stage: every code list and script is lossless */
            err[0] = '\0';
            if (sdStageFromJson(&env, json, strlen(json), &back, err, sizeof err) != 0 || !sdStageEqual(&s, &back)) {
                plogLine("stages", "%s does not read back to the same stage: %s\n", su->name, err);
                mismatches++;
            } else {
                sdStageFree(&back);
            }
            if (sdExportPut(su->number, json, &st) != 0) {
                failed++;
            }
            free(json);
        }
        done++;
        sdStageFree(&s);
    }
    pcStageTextDrop();
    if (statics.buf != NULL) {
        pcStaticsWalk(statics.buf, 1, &staticsChanged);
        free(statics.buf);
    }
    memcpy(&GAME, &savedGame, sizeof savedGame);
    memcpy(&FIELDSTG_map, &savedMap, sizeof savedMap);
    memcpy(&FIELDSTG_state, &savedState, sizeof savedState);
    LANGUAGE = savedLanguage;
    if (setVariant != savedVariant) {
        tdSetVariant(savedVariant);
    }
    sdExportEnd(failed == 0 && mismatches == 0);
    plogLine("stages", "%d stages: %d actors, %d talks, %ld code lists (%ld codes), %ld events (%ld scripts, %ld words), %ld slots; read back: %d mismatches; "
                       "files: %d written, %d kept, %d beside edited ones, %d failed; the setups wrote %ld bytes of overlay statics (put back)\n",
             done, (int)actors, (int)talks, lists, pairs, events, scripts, words, slotsTotal, mismatches, st.written, st.kept, st.newFiles, failed,
             (long)staticsChanged);
}

/* ---- port-close S4: the mods' patches of a stage's characters (shim/stage_mods.c) ----
   Right after a stage's setup (src/field/field_mode/field.c startField, pcStagePatchHook) the arrays it set in FIELDSTG_state.actors are read into the
   model, the active mods' patches are merged in (the stage's own output of THIS visit, so a setup that depends on the story progress is patched as it
   came out), and the result is written into buffers of the port that FIELDSTG_state.actors then points at. A stage no mod patches is not touched at
   all: the pointer stays the game's. Two buffer sets alternate, so the one of the previous stage is still there when the next one starts. */
typedef struct PcActorBuf {
    FieldActorEntry **list;
    FieldActorEntry *entries;
    FieldTalk *talks;
    u16 *codes;
} PcActorBuf;

static PcActorBuf sActorBuf[2];
static int sActorBufNow;

static void *pcZalloc(size_t count, size_t size) {
    void *p = malloc(count * size);

    if (p != NULL) {
        memset(p, 0, count * size);
    }
    return p;
}

static void pcActorBufFree(PcActorBuf *b) {
    free(b->list);
    free(b->entries);
    free(b->talks);
    free(b->codes);
    memset(b, 0, sizeof *b);
}

/* a list of the model as the game keeps it: NULL when not set, else the words and the 0xFFFF terminator at *at */
static u16 *pcCodesPut(u16 **at, const SdList *l) {
    u16 *start = *at;
    int i;

    if (!l->set) {
        return NULL;
    }
    for (i = 0; i < l->n; i++) {
        *(*at)++ = l->w[i];
    }
    *(*at)++ = 0xFFFF;
    return start;
}

static int pcActorsInstall(const SdStage *s) {
    PcActorBuf *b = &sActorBuf[sActorBufNow ^ 1];
    int i, j, talks = 0;
    long words = 0;
    FieldTalk *tp;
    u16 *cp;

    pcActorBufFree(b);
    for (i = 0; i < s->nactors; i++) {
        const SdActor *a = &s->actors[i];

        talks += a->ntalks + 1; /* + the closing talk a list that ends on a condition gets */
        words += a->cond.n + 1;
        for (j = 0; j < a->ntalks; j++) {
            words += a->talks[j].cond.n + a->talks[j].actions.n + 2;
        }
    }
    b->list = (FieldActorEntry **)pcZalloc((size_t)s->nactors + 1, sizeof *b->list);
    b->entries = (FieldActorEntry *)pcZalloc((size_t)s->nactors + 1, sizeof *b->entries);
    b->talks = (FieldTalk *)pcZalloc((size_t)talks + 1, sizeof *b->talks);
    b->codes = (u16 *)pcZalloc((size_t)words + 1, sizeof *b->codes);
    if (b->list == NULL || b->entries == NULL || b->talks == NULL || b->codes == NULL) {
        pcActorBufFree(b);
        return 0;
    }
    tp = b->talks;
    cp = b->codes;
    for (i = 0; i < s->nactors; i++) {
        const SdActor *a = &s->actors[i];
        FieldActorEntry *e = &b->entries[i];

        e->conditions = pcCodesPut(&cp, &a->cond);
        e->id = (s16)a->character;
        e->image = (s16)a->image;
        e->x = (s16)a->x;
        e->y = (s16)a->y;
        e->dir = (s16)a->dir;
        if (a->ntalks > 0) {
            e->talks = tp;
            for (j = 0; j < a->ntalks; j++, tp++) {
                tp->conditions = pcCodesPut(&cp, &a->talks[j].cond);
                tp->actions = pcCodesPut(&cp, &a->talks[j].actions);
                tp->entry = a->talks[j].entry;
            }
            if (a->talks[a->ntalks - 1].cond.set) {
                tp++; /* the game reads on to the first talk without conditions: a zeroed one (entry 0) closes the list */
            }
        }
        b->list[i] = e;
    }
    b->list[s->nactors] = NULL;
    sActorBufNow ^= 1;
    FIELDSTG_state.actors = b->list;
    return 1;
}

/* ---- port-close S8, S10: the slots, events and battles of a patched stage ----
   Like the characters: read from the arrays the setup set, merged by shim/stage_mods.c, written to buffers of the port that FieldState then points at (two
   sets alternate). A slot a patch added has no cell in the map's trigger layer, so it carries a rectangle of the map in pixels; the field's trigger lookup
   (src/field/field_mode/trigger.c, pcSlotAreaAt) asks for it before the map's cells. */
typedef struct PcStageBuf {
    StageSlot *slots;
    FieldEvent *events;
    s16 *scripts;
    FieldBattles *battles;
    BattleList *lists;
    AreaBattle *areas;
} PcStageBuf;

static PcStageBuf sStageBuf[2];
static int sStageBufNow;

typedef struct PcSlotArea {
    int index, x, y, w, h, facing;
} PcSlotArea;

static PcSlotArea sSlotAreas[64];
static int sSlotAreaCount;

static void pcStageBufFree(PcStageBuf *b) {
    free(b->slots);
    free(b->events);
    free(b->scripts);
    free(b->battles);
    free(b->lists);
    free(b->areas);
    memset(b, 0, sizeof *b);
}

/* the slot under (px, py) of the areas the patches gave (the last one added wins): 1 with its index in the table and the direction to face (-1 any) */
static int sAreaHeld = -1;

/* the index of the slot whose area the player stood in at the last trigger lookup, -1 when none (the harness's `fieldexit` reads it) */
int pcSlotAreaHeld(void) {
    return sAreaHeld;
}

int pcSlotAreaAt(int px, int py, s32 *index, s32 *facing) {
    static int lastIndex = -1;
    int i;

    for (i = sSlotAreaCount - 1; i >= 0; i--) {
        const PcSlotArea *a = &sSlotAreas[i];

        if (px >= a->x && px < a->x + a->w && py >= a->y && py < a->y + a->h) {
            *index = a->index;
            *facing = a->facing;
            sAreaHeld = a->index;
            if (lastIndex != a->index) {
                lastIndex = a->index;
                PLOG("stages", PLOG_TRACE, "the player is at (%d, %d), inside the area of slot %d\n", px, py, a->index);
            }
            return 1;
        }
    }
    lastIndex = -1;
    sAreaHeld = -1;
    return 0;
}

static int pcSlotsInstall(const SdStage *s, PcStageBuf *b) {
    int i;

    b->slots = (StageSlot *)pcZalloc((size_t)s->nslots + 1, sizeof *b->slots);
    if (b->slots == NULL) {
        return 0;
    }
    sSlotAreaCount = 0;
    for (i = 0; i < s->nslots; i++) {
        const SdSlot *x = &s->slots[i];
        StageSlot *t = &b->slots[i];
        int j;

        t->type = (u16)x->type;
        t->arg = (u16)x->arg;
        t->x = (u16)x->x;
        t->y = (u16)x->y;
        t->dir = (u16)x->dir;
        t->hideAnim = (u16)x->hideAnim;
        t->place = (u16)x->place;
        t->placeArg = (u16)x->placeArg;
        for (j = 0; j < 2; j++) {
            t->conditions[j][0] = x->cond.set && j * 2 < x->cond.n ? x->cond.w[j * 2] : 0xFFFF;
            t->conditions[j][1] = x->cond.set && j * 2 < x->cond.n ? x->cond.w[j * 2 + 1] : 0;
        }
        if (x->hasArea && sSlotAreaCount < (int)(sizeof sSlotAreas / sizeof sSlotAreas[0])) {
            PcSlotArea *a = &sSlotAreas[sSlotAreaCount++];

            a->index = i;
            a->x = x->ax;
            a->y = x->ay;
            a->w = x->aw;
            a->h = x->ah;
            a->facing = x->facing;
        }
    }
    return 1; /* the zeroed record after the last one is type 0, the table's end */
}

static int pcEventsInstall(const SdStage *s, PcStageBuf *b) {
    const FieldEvent *orig = FIELDSTG_state.events;
    int i, words = 0, own = 0;
    s16 *sp;

    for (i = 0; i < s->nevents; i++) {
        words += s->events[i].hasScript ? s->events[i].nscript : 0;
    }
    b->events = (FieldEvent *)pcZalloc((size_t)s->nevents + 1, sizeof *b->events);
    b->scripts = (s16 *)pcZalloc((size_t)words + 1, sizeof *b->scripts);
    if (b->events == NULL || b->scripts == NULL) {
        return 0;
    }
    sp = b->scripts;
    for (i = 0; i < s->nevents; i++) {
        const SdEvent *x = &s->events[i];
        FieldEvent *e = &b->events[i];

        e->id = x->id;
        if (!x->isNew) {
            e->start = orig[own].start; /* the C task and the C function an event of the game keeps */
            e->end = orig[own].end;
            own++;
        }
        if (x->hasScript) {
            memcpy(sp, x->script, (size_t)x->nscript * sizeof *sp);
            e->script = sp;
            sp += x->nscript;
        }
        e->text = x->hasText ? (s32)(((u32)x->textFile << 16) | ((u32)x->textEntry & 0xFFFF)) : 0;
        if (x->isNew && plogOn("stages", PLOG_TRACE)) {
            ScBuf sb = {0};

            if (x->hasScript && scScriptToJson(x->script, x->nscript, &sb) == 0) {
                plogLine("stages", "event %d%s%s: text file %d entry %d, script %s\n", x->id, smEventName(x->id) != NULL ? " " : "",
                         smEventName(x->id) != NULL ? smEventName(x->id) : "", x->textFile, x->textEntry, sb.b);
            }
            scFree(&sb);
        }
    }
    b->events[s->nevents].id = -1;
    return 1;
}

static int pcBattlesInstall(const SdStage *s, PcStageBuf *b) {
    const SdBattles *bt = &s->battles;
    size_t extra = bt->nareas > 4 ? (size_t)(bt->nareas - 4) * sizeof(BattleList *) : 0;
    BattleList **table;
    int i, k;

    b->battles = (FieldBattles *)pcZalloc(1, sizeof *b->battles + extra);
    b->lists = (BattleList *)pcZalloc((size_t)bt->nareas, sizeof *b->lists);
    b->areas = (AreaBattle *)pcZalloc((size_t)bt->nareas * 8, sizeof *b->areas);
    if (b->battles == NULL || b->lists == NULL || b->areas == NULL) {
        return 0;
    }
    b->battles->serial = bt->serial;
    b->battles->id = bt->place;
    table = (BattleList **)b->battles->battles; /* the game indexes battles[area] for any area of the map: the table runs on past its four */
    for (i = 0; i < bt->nareas; i++) {
        b->lists[i].count = bt->areas[i].rate;
        for (k = 0; k < 8; k++) {
            AreaBattle *a = &b->areas[i * 8 + k];

            a->encounter = bt->areas[i].b[k].encounter;
            a->stage = bt->areas[i].b[k].stage;
            a->music = bt->areas[i].b[k].music;
            b->lists[i].battles[k] = a;
        }
        table[i] = &b->lists[i];
    }
    return 1;
}

/* DW3_LOG=trace: for every character of the merged list, whether the game would create it now (its conditions, through the game's own interpreter, mod flags
   included) and which of its talks would be said. Evaluates only: nothing is applied. */
/* DW3_STAGE_APPLY="<actor>[:<rounds>]": a test hook (a normal run never sets it) that applies the actions of the talk the interpreter picks for the
   character at that place of the final list, `rounds` times (1 by default; the talk is picked again each round, so one talk's actions can change which one
   comes next), and logs the money and the story progress after each. It is how a harness shows a talk's actions run without a key press. */
static void pcApplyTalkHook(const char *name, const PcActorBuf *b, int n) {
    const char *e = getenv("DW3_STAGE_APPLY");
    int actor, rounds = 1, r;

    if (e == NULL || *e == '\0') {
        return;
    }
    for (actor = 0; *e >= '0' && *e <= '9'; e++) {
        actor = actor * 10 + (*e - '0');
    }
    if (*e == ':') {
        for (rounds = 0, e++; *e >= '0' && *e <= '9'; e++) {
            rounds = rounds * 10 + (*e - '0');
        }
    }
    if (actor < 0 || actor >= n) {
        plogLine("stages", "DW3_STAGE_APPLY: %s has no character %d (%d in the list)\n", name, actor, n);
        return;
    }
    for (r = 0; r < rounds; r++) {
        const FieldTalk *t;
        int k = 0;

        for (t = b->list[actor]->talks; t != NULL; t++, k++) {
            if (t->conditions == NULL || FLAGS_00.checkConditions(t->conditions) == 1) {
                break;
            }
        }
        if (t == NULL || b->list[actor]->talks == NULL) {
            plogLine("stages", "DW3_STAGE_APPLY: %s character %d has no talks\n", name, actor);
            return;
        }
        if (t->actions != NULL) {
            FLAGS_00.applyActions(t->actions);
        }
        plogLine("stages", "DW3_STAGE_APPLY: round %d: %s character %d said talk %d (entry %d): money %d, progress %d\n", r + 1, name, actor, k, t->entry, (int)GAME.money,
                 (int)GAME.progress);
    }
}

static void pcActorsTrace(const char *name, const PcActorBuf *b, int n) {
    int i;

    for (i = 0; i < n; i++) {
        const FieldActorEntry *e = b->list[i];
        const FieldTalk *t;
        int present = e->conditions == NULL || FLAGS_00.checkConditions(e->conditions) != 0, k = 0, said = -1, entry = 0;

        for (t = e->talks; t != NULL && said < 0; t++, k++) {
            if (t->conditions == NULL || FLAGS_00.checkConditions(t->conditions) == 1) {
                said = k;
                entry = t->entry;
                break;
            }
            if (k > 1000) {
                break;
            }
        }
        plogLine("stages", "%s actor %d: character %d image %d at (%d, %d) dir %d: %s%s", name, i, e->id, e->image, e->x, e->y, e->dir, present ? "created" : "not created now",
                 e->talks == NULL ? "; no talks\n" : "");
        if (e->talks != NULL) {
            char *line;

            pcStageTextDrop(); /* the text layer may have appended entries since the last read (S6) */
            line = pcStageLine(FIELDSTG_state.textFile, entry);
            plogRaw("; says talk %d (entry %d)%s%s\n", said, entry, line != NULL ? ": " : "", line != NULL ? line : "");
            free(line);
        }
    }
}

static int pcStageExists(int number) {
    const SdSetup *su;

    for (su = SD_SETUPS; su->name != NULL; su++) {
        if (su->number == number) {
            return 1;
        }
    }
    return 0;
}

static int pcStageEnvResolve(void *ud, const char *kind, const char *name) {
    (void)ud;
    if (strcmp(kind, "item") == 0) {
        return itemExtIdOf(name);
    }
    if (strcmp(kind, "card") == 0) {
        return cardExtIdOf(name);
    }
    if (strcmp(kind, "special") == 0) {
        return gameSpecialIdOf(name); /* a condition a mod's tables/special_conditions.json defines (S3) */
    }
    return -1;
}

/* ---- port-close S7: the game's side of the port actions (shim/stage_mods.c runs them through this) ----
   Each one calls what the engine already has: the money rows of SPECIAL_CONDITIONS do what give/take do (GAME.money between 0 and MONEY_MAX), the exits of a
   stage leave the field with FIELDSTG_leaveField, the shared sounds go through playSound. They run where a talk's actions run (the end of a talk). */

/* the game's shared sounds, in the order of scSoundName */
static const s32 sActSounds[SC_SOUND_COUNT] = {SOUND_SELECT, SOUND_MENU_CANCEL, SOUND_CURSOR, SOUND_MENU_OPEN, SOUND_MENU_CLOSE, SOUND_MENU_MOVE,
                                               SOUND_MENU_CONFIRM, SOUND_COUNT, SOUND_RECOVERY, SOUND_TELEPORT, SOUND_WIN_JINGLE, SOUND_INN_JINGLE};

/* The game mode that runs a stage (the first of its modes, as the exits name it), 0 when the stage has none */
static s32 pcStageWarpMode(int number) {
    const SdSetup *su;

    for (su = SD_SETUPS; su->name != NULL; su++) {
        if (su->number == number) {
            SdStage s;

            memset(&s, 0, sizeof s);
            pcStageModes(su, &s);
            return s.nmodes > 0 ? s.modes[0] : 0;
        }
    }
    return 0;
}

void pcStageWarpPoll(void) {
    SmRequest l;

    if (!smTakeRequest(&l)) {
        return;
    }
    if (l.kind == SM_REQ_BATTLE) {
        AreaBattle b;

        b.encounter = l.mode;
        b.stage = l.arg;
        b.music = l.x;
        PLOG("stages", PLOG_TRACE, "starting the field battle %d (fight stage %d, music %d)\n", l.mode, l.arg, l.x);
        FIELDSTG_startBattle(&b);
    } else if (l.kind == SM_REQ_EVENT) {
        FieldTask *field = (FieldTask *)TASK_REGISTRY.funcs.find(FIELD_TASK_FIELD, -1, -1);

        PLOG("stages", PLOG_TRACE, "starting the event %d\n", l.mode);
        if (field != NULL && field->children != NULL) {
            ((FieldChildren *)field->children)->event = FIELDSTG_startEvent(l.mode); /* what FIELDSTG_startListedEvent does for the game's own talks */
        }
    } else {
        PLOG("stages", PLOG_TRACE, "leaving the field for mode %d\n", l.mode);
        FIELDSTG_leaveField(l.mode, l.arg, l.x << 8, l.y << 8, l.dir);
    }
}

static int pcStageModeOf(int number) {
    return (int)pcStageWarpMode(number);
}

static int pcEncounterOk(int row) {
    return row >= 1 && row < gameTablesEncounterCount();
}

static void pcActRun(int kind, const int *a) {
    switch (kind) {
    case SC_ACT_GIVE_MONEY:
        GAME.money += a[0];
        if (GAME.money > MONEY_MAX) {
            GAME.money = MONEY_MAX;
        }
        PLOG("stages", PLOG_TRACE, "action give-money %d: money is now %d\n", a[0], (int)GAME.money);
        break;
    case SC_ACT_TAKE_MONEY:
        GAME.money -= a[0];
        if (GAME.money < 0) {
            GAME.money = 0;
        }
        PLOG("stages", PLOG_TRACE, "action take-money %d: money is now %d\n", a[0], (int)GAME.money);
        break;
    case SC_ACT_SET_PROGRESS:
        GAME.progress = a[0];
        PLOG("stages", PLOG_TRACE, "action set-progress %d: progress is now %d\n", a[0], (int)GAME.progress);
        break;
    case SC_ACT_WARP: {
        s32 mode = pcStageWarpMode(a[0]);

        PLOG("stages", PLOG_TRACE, "action warp to stage %d (mode %d) at (%d, %d) dir %d\n", a[0], (int)mode, a[1], a[2], a[3]);
        if (mode != 0) {
            /* Not now: a talk's actions can run before the field has its player (the DW3_STAGE_APPLY hook runs inside the stage's own setup, where closing
               the field reads a player that is not there yet) and the game's own exits leave from the field task's frame. The field task asks for the
               request on its next running frame (pcStageWarpPoll), which is the same FIELDSTG_leaveField the game's exits use. */
            SmRequest l = {SM_REQ_LEAVE, (int)mode, -1, a[1], a[2], a[3]};

            smPostRequest(&l);
        }
        break;
    }
    case SC_ACT_START_BATTLE:
        PLOG("stages", PLOG_TRACE, "action start-battle: encounter %d, fight stage %d, music %d\n", a[0], a[1], a[2]);
        if (a[0] >= 1 && a[0] < gameTablesEncounterCount()) {
            SmRequest l = {SM_REQ_BATTLE, a[0], a[1], a[2], 0, 0};

            smPostRequest(&l); /* the same FIELDSTG_startBattle the stages' own event battles call, from the field's frame (see warp) */
        }
        break;
    case SC_ACT_RUN_EVENT: {
        SmRequest l = {SM_REQ_EVENT, a[0], 0, 0, 0, 0};

        PLOG("stages", PLOG_TRACE, "action run-event %d%s%s\n", a[0], smEventName(a[0]) != NULL ? " " : "", smEventName(a[0]) != NULL ? smEventName(a[0]) : "");
        smPostRequest(&l);
        break;
    }
    case SC_ACT_SOUND:
        if (a[0] >= 0 && a[0] < SC_SOUND_COUNT) {
            PLOG("stages", PLOG_TRACE, "action sound %s\n", scSoundName(a[0]));
            playSound(sActSounds[a[0]]);
        }
        break;
    }
}

static int pcCharacterOk(int character);
static int pcSpriteAdd(const char *dir, const char *label, SmSprite *out, char *why, size_t cap);
static void pcSpritesDone(void);

/* After the content mods are loaded (their items and cards are the names a patch's codes may use) */
static void pcInstallStageMods(void) {
    static const StageCodeEnv env = {NULL, pcStageEnvResolve, NULL, NULL, NULL, NULL};
    SmHost host;

    memset(&host, 0, sizeof host);
    host.env = &env;
    host.stageExists = pcStageExists;
    host.stageMode = pcStageModeOf;
    host.encounterOk = pcEncounterOk;
    host.textFirst = tdStageTextFirst;
    host.textChanged = tdStageTextInvalidate;
    host.characterOk = pcCharacterOk;
    host.spriteAdd = pcSpriteAdd;
    host.spritesDone = pcSpritesDone;
    smSetActRunner(pcActRun); /* the port actions of the patches' talks (S7) */
    tdSetStageTextHook(smTextCount, smTextOf); /* the named texts of the patches' talks are appended to the stage's text file (S6) */
    smLoad(&host);
}

/* DW3_MODVAR="mod:name=1,other:name=3": mod variables set when the first patched stage starts (after the new game's own reset), so a harness run can
   start with a flag already set. A test hook: a normal run never sets it. */
static void pcModVarsPreset(void) {
    static int done;
    const char *e;

    if (done) {
        return;
    }
    done = 1;
    for (e = getenv("DW3_MODVAR"); e != NULL && *e != '\0';) {
        char name[PC_EXT_VAR_NAME_MAX + 1];
        size_t n = 0;
        int value = 0, neg = 0;

        while (*e != '\0' && *e != '=' && *e != ',' && n < sizeof name - 1) {
            name[n++] = *e++;
        }
        name[n] = '\0';
        if (*e == '=') {
            e++;
            if (*e == '-') {
                neg = 1;
                e++;
            }
            while (*e >= '0' && *e <= '9') {
                value = value * 10 + (*e++ - '0');
            }
        }
        while (*e != '\0' && *e != ',') {
            e++;
        }
        if (*e == ',') {
            e++;
        }
        if (n > 0 && pcExtVarSet(name, neg ? -value : value) == 0) {
            plogLine("stages", "DW3_MODVAR: %s = %d\n", name, neg ? -value : value);
        }
    }
}

/* ---- port-close S9: sprites and actor images beyond the stage's own ----
   Each frame of a character is streamed by FIELDSTG_animateActor into the VRAM slot its ActorImage names, so a character needs no VRAM of its own until it
   plays: what limits "any character in any stage" is the slots. A stage's list has 20 (FieldImages.actors) in places the stage's art leaves; a patch that
   gives an actor a character without an `image` gets a slot of the port's band (sprite_data.h: x 0-319, y 16-255, never texture space on the PC) sized for the
   character's largest frame, in a copy of the stage's table that FIELDSTG_state.images then points at. The sprites mods bring are ONE virtual file (the bank),
   the characters SM_SPRITE_CHAR_BASE up; FieldState's getFileEntry and getActorWidth are wrapped to answer for them. */
static SprBank *sBank;
static int sBankFile = -1;
static ActorImage *sImageBuf[2];
static int sImageBufNow;

static int pcSpriteAdd(const char *dir, const char *label, SmSprite *out, char *why, size_t cap) {
    SprInfo in;

    (void)label;
    if (sBank == NULL && (sBank = sprBankNew()) == NULL) {
        snprintf(why, cap, "out of memory");
        return -1;
    }
    if (sprBankAddDir(sBank, dir, &in, why, cap) != 0) {
        return -1;
    }
    out->entry = in.entry;
    out->width = in.width;
    out->w = in.w;
    out->h = in.h;
    return 0;
}

/* The load of the mods' sprites ended: the bank becomes a virtual file of the port (after pcInstallDigimon's clear: the numbers start from the base) */
static void pcSpritesDone(void) {
    size_t len = 0;
    uint8_t *bytes = sprBankBuild(sBank, &len);

    sBankFile = -1;
    if (bytes != NULL) {
        sBankFile = pfsAddVirtualFile(bytes, len);
        plogLine("stages", "mod sprites: %d entries, %lu bytes, %s\n", sprBankEntries(sBank), (unsigned long)len,
                 sBankFile >= 0 ? "one file of the port (a single slot of the file cache)" : "no room for the file: they are not shown");
        free(bytes);
    }
    sprBankFree(sBank);
    sBank = NULL;
}

/* whether the game has a character with that id and a sprite file */
static int pcCharacterOk(int character) {
    return character >= 0 && character < (int)FIELDSTG_fileEntryCount && FIELDSTG_fileEntries[character] != 0;
}

static s32 (*sOrigFileEntry)(s32);
static s32 (*sOrigActorWidth)(s32);

static s32 pcFileEntryOf(s32 character) {
    SmSprite sp;

    if (smSpriteInfo(character, &sp) && sBankFile >= 0) {
        return (s32)(((u32)sBankFile << 16) | (u32)sp.entry);
    }
    return sOrigFileEntry(character);
}

static s32 pcActorWidthOf(s32 character) {
    SmSprite sp;

    if (smSpriteInfo(character, &sp)) {
        return sp.width;
    }
    return sOrigActorWidth(character);
}

/* The largest frame a character plays, in pixels: a mod sprite's from the bank, a game character's from its sprite file on the disc (read once; 64x64 when it
   cannot be read) */
static int pcCharacterFrame(int character, int *w, int *h) {
    static short cacheW[2048], cacheH[2048];
    SmSprite sp;
    int fw = 0, fh = 0;

    if (smSpriteInfo(character, &sp)) {
        *w = sp.w;
        *h = sp.h;
        return 1;
    }
    if (character >= 0 && character < 2048 && cacheW[character] != 0) {
        *w = cacheW[character];
        *h = cacheH[character];
        return 1;
    }
    if (pcCharacterOk(character)) {
        s32 entry = FIELDSTG_fileEntries[character];
        size_t len = 0;
        unsigned char *file = pcCdReadDiscFile((int)((u32)entry >> 16), &len);

        if (file != NULL && sprMaxFrame(file, len, (int)(entry & 0xFFFF), &fw, &fh) == 0) {
            if (character >= 0 && character < 2048) {
                cacheW[character] = (short)fw;
                cacheH[character] = (short)fh;
            }
        } else {
            plogLine("stages", "character %d: its sprite file could not be measured, a slot of 64x64 is given\n", character);
            fw = fh = 64;
        }
        free(file);
        *w = fw;
        *h = fh;
        return 1;
    }
    return 0;
}

/* The extra actor images of a patched stage (S9): a copy of the stage's table with a slot of the band for each, FIELDSTG_state.images pointing at it.
   An actor whose character has no slot (no room, a file the port cannot read) is dropped from the list. */
static void pcImagesInstall(SdStage *s) {
    ActorImage *buf;
    SprVram vram;
    int k, i, n, dropped = 0, copy = 2 + SD_IMAGE_EXTRA_FIRST;
    int total = s->nextra;

    if (s->nextra <= 0 || FIELDSTG_state.images.actors == NULL) {
        return;
    }
    if (total > 4096) {
        total = 4096;
    }
    buf = (ActorImage *)pcZalloc((size_t)copy + (size_t)total, sizeof *buf);
    if (buf == NULL) {
        return;
    }
    memcpy(buf, FIELDSTG_state.images.actors, (size_t)copy * sizeof *buf);
    sprVramReset(&vram);
    for (k = 0; k < total; k++) {
        int fw, fh;
        SprSlot sl;
        const char *why = NULL;

        if (!pcCharacterFrame(s->extra[k], &fw, &fh)) {
            why = "no sprite file";
        } else if (s->extra[k] >= SM_SPRITE_CHAR_BASE && sBankFile < 0) {
            why = "the mods' sprite file is not there";
        } else if (sprVramAlloc(&vram, fw, fh, &sl) != 0) {
            why = "the port's VRAM band has no room left";
        }
        if (why != NULL) {
            plogLine("stages", "stage %d: the character %d gets no slot (%s): its actors are left out\n", s->number, s->extra[k], why);
            s->extra[k] = -1;
            continue;
        }
        buf[copy + k].x = (s16)sl.x;
        buf[copy + k].y = (s16)sl.y;
        buf[copy + k].imageX = (s16)sl.imageX;
        buf[copy + k].imageY = (s16)sl.imageY;
        buf[copy + k].u = (s16)sl.u;
        buf[copy + k].v = (s16)sl.v;
        buf[copy + k].clutX = (s16)sl.clutX;
        buf[copy + k].clutY = (s16)sl.clutY;
        PLOG("stages", PLOG_MODS, "stage %d: character %d: image %d at VRAM (%d, %d), frames up to %dx%d\n", s->number, s->extra[k], SD_IMAGE_EXTRA_FIRST + k,
             sl.imageX, sl.imageY, fw, fh);
    }
    for (i = 0, n = 0; i < s->nactors; i++) {
        int img = s->actors[i].image;

        if (img >= SD_IMAGE_EXTRA_FIRST && (img - SD_IMAGE_EXTRA_FIRST >= total || s->extra[img - SD_IMAGE_EXTRA_FIRST] < 0)) {
            dropped++;
            sdActorFree(&s->actors[i]); /* its slot did not come out */
            continue;
        }
        s->actors[n++] = s->actors[i];
    }
    if (dropped > 0) {
        /* the kept actors moved down: what is past the new end is the old places of the same pointers, cleared so sdStageFree does not free them twice */
        memset(&s->actors[n], 0, (size_t)dropped * sizeof *s->actors);
        s->nactors = n;
    }
    free(sImageBuf[sImageBufNow ^ 1]);
    sImageBuf[sImageBufNow ^ 1] = buf;
    sImageBufNow ^= 1;
    FIELDSTG_state.images.actors = buf;
    if (FIELDSTG_state.getFileEntry != pcFileEntryOf) {
        sOrigFileEntry = FIELDSTG_state.getFileEntry;
        sOrigActorWidth = FIELDSTG_state.getActorWidth;
        FIELDSTG_state.getFileEntry = pcFileEntryOf;
        FIELDSTG_state.getActorWidth = pcActorWidthOf;
    }
}

void pcStagePatchHook(void) {
    const SdSetup *su;
    SdStage s;
    SmReport rep;
    int maxImage = 1;

    sSlotAreaCount = 0; /* the previous stage's trigger areas are not this one's */
    if (smPatchCount() == 0 || FIELDSTG_state.stageInit == NULL) {
        return;
    }
    pcModVarsPreset();
    for (su = SD_SETUPS; su->name != NULL; su++) {
        if (su->start != NULL && (const void *)su->start == (const void *)FIELDSTG_state.stageInit) {
            break;
        }
    }
    if (su->name == NULL || !smHasPatches(su->number)) {
        return;
    }
    memset(&s, 0, sizeof s);
    s.number = su->number;
    s.textFile = FIELDSTG_state.textFile != 0 ? FIELDSTG_state.textFile - LANGUAGE : -1; /* named texts append to it (S6) */
    pcStageReadActors(&FIELDSTG_state, &s, 0, &maxImage);
    pcStageReadSlots(&FIELDSTG_state, &s);
    pcStageReadEvents(&FIELDSTG_state, &s);
    pcStageReadBattles(&FIELDSTG_state, &s);
    s.nimages = maxImage >= 2 ? maxImage - 1 : 0; /* the actor images the stage's own characters use: a patch can reuse those */
    if (smApplyStage(su->number, &s, &rep)) {
        PcStageBuf *sb = &sStageBuf[sStageBufNow ^ 1];
        int others = rep.slotsPatched + rep.slotsAdded + rep.slotsRemoved + rep.eventsPatched + rep.eventsAdded + rep.battlesPatched + rep.areasAdded;

        if (rep.patched + rep.added + rep.removed > 0) {
            pcImagesInstall(&s);
        }
        if (rep.patched + rep.added + rep.removed > 0 && pcActorsInstall(&s)) {
            if (plogOn("stages", PLOG_TRACE)) {
                pcActorsTrace(su->name, &sActorBuf[sActorBufNow], s.nactors);
            }
            pcApplyTalkHook(su->name, &sActorBuf[sActorBufNow], s.nactors);
        }
        if (others > 0) {
            pcStageBufFree(sb);
            if (rep.slotsPatched + rep.slotsAdded + rep.slotsRemoved > 0 && pcSlotsInstall(&s, sb)) {
                FIELDSTG_state.slots = sb->slots;
            }
            if (rep.eventsPatched + rep.eventsAdded > 0 && pcEventsInstall(&s, sb)) {
                FIELDSTG_state.events = sb->events;
            }
            if (rep.battlesPatched + rep.areasAdded > 0 && pcBattlesInstall(&s, sb)) {
                FIELDSTG_state.battles = sb->battles;
            }
            sStageBufNow ^= 1;
        }
        PLOG("stages", PLOG_MODS, "%s: %d character(s) patched, %d added, %d removed, %d skipped, %d conflict(s); %d in the list now%s\n", su->name, rep.patched,
             rep.added, rep.removed, rep.skipped, rep.conflicts, s.nactors, rep.texts > 0 ? "; named texts resolved" : "");
        if (others > 0) {
            PLOG("stages", PLOG_MODS, "%s: slots %d patched, %d added, %d removed; events %d patched, %d added; battles %d patched, %d area(s) added\n", su->name,
                 rep.slotsPatched, rep.slotsAdded, rep.slotsRemoved, rep.eventsPatched, rep.eventsAdded, rep.battlesPatched, rep.areasAdded);
        }
    }
    sdStageFree(&s);
}

/* content-export E7: the disc files no other exporter covers, copied byte for byte to data/raw (shim/raw_export.c). About 15 MB, so it runs here, before the
   window opens, and is skipped by exporter version afterwards; settings.ini [data] export_raw = all (default) | off | stage-map, battle-effect, ... The file list
   is the disc directory of port_fs; the sectors are read through a handle of its own. */
static FILE *sRawDisc;

static const PfsFile **sRawFiles;
static int sRawCount;

static int pcRawList(void *ud, int i, char *path, size_t cap, long *lba, long *size) {
    (void)ud;
    if (i == 0) { /* the job asks from the first file: take the disc directory once */
        free(sRawFiles);
        sRawCount = pfsList(NULL, NULL, 0);
        sRawFiles = sRawCount > 0 ? (const PfsFile **)malloc(sizeof *sRawFiles * (size_t)sRawCount) : NULL;
        if (sRawFiles != NULL) {
            pfsList(NULL, sRawFiles, sRawCount);
        } else {
            sRawCount = 0;
        }
    }
    if (sRawFiles == NULL || i < 0 || i >= sRawCount) {
        return 0;
    }
    snprintf(path, cap, "%s", sRawFiles[i]->path);
    *lba = sRawFiles[i]->lba;
    *size = sRawFiles[i]->size;
    return 1;
}

static int pcRawRead(void *ud, long lba, unsigned char *frame) {
    (void)ud;
    if (sRawDisc == NULL) {
        const char *image = pcCdDiscPath();

        sRawDisc = image != NULL ? dfdOpen(image) : NULL;
        if (sRawDisc == NULL) {
            return 0;
        }
    }
    return fseek(sRawDisc, lba * 2352L, SEEK_SET) == 0 && fread(frame, 1, 2352, sRawDisc) == 2352;
}

static void pcInstallRaw(const char *dataDir) {
    RawExportIo io;
    char spec[256];

    if (dataDir == NULL) {
        return;
    }
    if (!inputCfgReadExportRaw(spec, (int)sizeof(spec))) {
        snprintf(spec, sizeof(spec), "all"); /* the line is absent: the default */
    }
    if (strcmp(spec, "off") == 0) {
        return;
    }
    io.list = pcRawList;
    io.read = pcRawRead;
    io.ud = NULL;
    rawExportSetup(dataDir, &io);
    rawExportRun(spec);
    if (sRawDisc != NULL) {
        fclose(sRawDisc);
        sRawDisc = NULL;
    }
    free(sRawFiles);
    sRawFiles = NULL;
    sRawCount = 0;
}

/* content-export E3a/E3b: the sound banks as WAV, MIDI and JSON in data/audio/ (shim/audio_export.c). The banks are the game's own table
   (SOUND_BANK_FILES, bank ids 1..71: the head archive MP<NAME>.BIN and the body archive MV<NAME>.BIN); the bank's folder name is where
   its head file is on the disc. The files are read through a handle of its own (the game's reader and its sound state are not touched)
   and only the thread that runs the job calls pcAudioFile. */
static FILE *sAudioDisc;

static int pcAudioBank(void *ud, int i, AudioExportBank *out) {
    const SoundFiles *f;
    const char *head, *slash, *name;
    size_t len = 0;
    int k;
    (void)ud;
    if (i < 0 || i + 1 >= 72 || (f = SOUND_BANK_FILES[i + 1]) == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->id = i + 1;
    out->headFile = f->headFile;
    out->bodyFile = f->bodyFile;
    out->vhIndex = f->vhIndex & 0xFFFF;
    out->bodyIndex = f->bodyEntry & 0xFFFF;
    for (k = 0; k < AUDIO_EXPORT_MAX_SEPS && f->seps[k] != 0; k++) {
        out->seps[k] = f->seps[k] & 0xFFFF;
    }
    out->sepCount = k;
    /* "DAT/SOUND/BGM001/MPBGM001.BIN" -> "BGM001" */
    head = pfsName(f->headFile);
    slash = head != NULL ? strrchr(head, '/') : NULL;
    name = NULL;
    if (slash != NULL) {
        const char *prev = slash;
        while (prev > head && prev[-1] != '/') {
            prev--;
        }
        name = prev;
        len = (size_t)(slash - prev);
    }
    if (name != NULL && len > 0 && len < sizeof out->name) {
        memcpy(out->name, name, len);
    } else {
        snprintf(out->name, sizeof out->name, "BANK%02X", i + 1);
    }
    return 1;
}

static int pcAudioFile(void *ud, int file, unsigned char **bytes, size_t *len) {
    const char *name = pfsName(file);
    char path[128];
    const PfsFile *f;
    unsigned char raw[2352], *buf;
    long s, sectors;
    (void)ud;
    if (name == NULL) {
        return 0;
    }
    snprintf(path, sizeof path, "AAA/%s", name);
    f = pfsFindPath(path);
    if (f == NULL || f->size <= 0) {
        return 0;
    }
    if (sAudioDisc == NULL) {
        const char *image = pcCdDiscPath();

        sAudioDisc = image != NULL ? dfdOpen(image) : NULL;
        if (sAudioDisc == NULL) {
            return 0;
        }
    }
    sectors = (f->size + 2047) / 2048;
    buf = (unsigned char *)malloc((size_t)sectors * 2048);
    if (buf == NULL) {
        return 0;
    }
    for (s = 0; s < sectors; s++) {
        if (fseek(sAudioDisc, (f->lba + s) * 2352L, SEEK_SET) != 0 || fread(raw, 1, 2352, sAudioDisc) != 2352) {
            free(buf);
            return 0;
        }
        memcpy(buf + (size_t)s * 2048, raw + 24, 2048); /* the user bytes of a Mode 2 Form 1 sector */
    }
    *bytes = buf;
    *len = (size_t)f->size;
    return 1;
}

static int pcExportAllAudio(void) {
    return audioExportStart("all", 1); /* the row is an explicit ask: rebuild; the settings.ini key at start skips what is up to date */
}

/* the System tab's "Prepare modding files" row and --export-all: the same job, skipping what is up to date; and how many items failed */
static int pcPrepAudio(void) {
    return audioExportStart("all", 0);
}

static int pcFailedAudio(void) {
    int done, failed, total;

    audioExportCounts(&done, &failed, &total);
    return failed;
}

static void pcExportAudioPump(void) {
    audioExportPump(8); /* with a worker job running it only joins the worker once it is done */
}

static void pcInstallAudioExport(const char *dataDir) {
    static const OptSysExport hooks = {pcExportAllAudio, pcExportAudioPump, audioExportStatus, audioExportActive, pcPrepAudio, pcFailedAudio};
    AudioExportIo io;
    char spec[256];

    io.bank = pcAudioBank;
    io.file = pcAudioFile;
    io.ud = NULL;
    audioExportSetup(dataDir, &io);
    atexit(audioExportShutdown); /* stops and joins the worker between two files; the files are written atomically */
    optionsSystemSetExportAudio(&hooks); /* the System tab's "Export audio" row; before the tab's rows are added */
    /* settings.ini [data] export_audio = all | BGM001, COMMON: done now, before the window opens */
    if (dataDir != NULL && inputCfgReadExportAudio(spec, (int)sizeof(spec)) && audioExportRequest(spec) > 0) {
        while (audioExportPump(1000)) {
        }
    }
}

/* content-export E3c/E3d: a mod's mods/<mod>/audio/<BANK>/ files in place of the bank's samples and songs (shim/audio_mod.c). The bank of a
   sound slot is the one the game's loader put in SOUND.banks[slot].id just before it opens the VAB (src/engine/sound/sound.c). */
static int pcAudioModSlotBank(void *ud, int slot, char *name, int cap) {
    AudioExportBank bank;
    int id;
    (void)ud;
    if (slot < 0 || slot >= 3) {
        return 0;
    }
    id = (int)SOUND.banks[slot].id;
    if (id < 1 || !pcAudioBank(NULL, id - 1, &bank) || (int)strlen(bank.name) >= cap) {
        return 0;
    }
    memcpy(name, bank.name, strlen(bank.name) + 1);
    return 1;
}

static void pcInstallAudioMod(void) {
    static AudioModIo io;

    io.slotBank = pcAudioModSlotBank;
    io.modCount = pcVideoModCount;
    io.modName = pcVideoModName;
    io.modDir = pcVideoModDir;
    io.ud = NULL;
    audioModSetup(&io);
}

static SaveViewCtx sView;

/* U8b: an original card's image record (CARDPAK0-4: 64 cards of 0x62C bytes each) and effect (CARDGAME_cardEffects, cards 1-60), the
   base a new card copies. The file is read from the disc once and kept for the next card of the same file. */
extern int CARD_IMAGE_FILES[];

static int pcCardBaseImage(int id, unsigned char *out) {
    static unsigned char *file[5];
    static size_t flen[5];
    int f, i;

    if (id < 1 || id > 316) {
        return 0;
    }
    f = (id - 1) >> 6;
    i = (id - 1) & 63;
    if (file[f] == NULL) {
        file[f] = pcCdReadDiscFile(CARD_IMAGE_FILES[f], &flen[f]);
    }
    if (file[f] == NULL || flen[f] < (size_t)(i + 1) * 0x62C) {
        return 0;
    }
    memcpy(out, file[f] + (size_t)i * 0x62C, 0x62C);
    return 1;
}

static int pcCardBaseEffect(int id, unsigned char *out) {
    if (id < 1 || id > 60) {
        return 0;
    }
    memcpy(out, &CARDGAME_cardEffects[id - 1], sizeof(CardEffect));
    return 1;
}

/* U8b: the new cards (ids from 317, their image and effect records, names). After pcCdTextInit: it reads the originals from the disc. */
static void pcInstallCards(void) {
    static const CardExtBase cardBase = {pcCardBaseImage, pcCardBaseEffect};

    cardExtSetBase(&cardBase);
    pcExtStateSetCardFallback((const int *)STARTER_DECK);
    cardExtLoad();
}

static void pcInstallItems(void) {
    itemExtLoad();

    sView.modCount = modsCount();
    sView.modName = modsNameOf;
    sView.modVersion = modsVersionOf;
    sView.itemName = pcViewItemName;
    saveViewSetContext(&sView);
}

/* extended techs T3: does the fighter of Digimon `digimon` have script `slot` in its effects archive? The fighters file
   (FILE_FIGHTERS) lists FighterEntry {s16 id, u8 index, u8 kind} up to an id 0 at `entries`; a kind under 0x3A is a
   partner, FighterInfo at `partners` + index * 0xC4 (sizeof(FighterInfo)), else an enemy, FighterInfoEnemy at `enemies` +
   index * 0x48 (battle/battle_mode/model.h, fighter_info.c); its `effects` (+8) is (file << 16) | entry of an archive
   whose entry is a table of 18 script offsets, 0 for an empty slot. Read from the disc, nothing stored. -1: the files
   cannot be read (no disc), so the caller accepts. */
static unsigned pcLe32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* the script slots (bit n: slot n has a script) of the effects archive of the fighter of `digimon`: 0 for a Digimon without a
   fighter or an archive, -1 when the files cannot be read. Each Digimon has its own archive file (about 50 KB), so only the
   result is kept, once per Digimon. */
#define PROBE_CACHE 512

static int pcFighterSlotMask(int digimon) {
    static unsigned char *fighters;
    static size_t fightersLen;
    static int tried;
    static struct { int digimon; int mask; } cache[PROBE_CACHE];
    static int cached;
    const unsigned char *e, *data;
    unsigned info, effects, file, entry, at, i, n;
    size_t len = 0;
    int mask = 0, k;

    if (!tried) {
        tried = 1;
        fighters = pcCdReadDiscFile(FILE_FIGHTERS, &fightersLen);
    }
    if (fighters == NULL || fightersLen < 16) {
        return -1;
    }
    for (k = 0; k < cached; k++) {
        if (cache[k].digimon == digimon) {
            return cache[k].mask;
        }
    }
    for (e = fighters + pcLe32(fighters + 4); (size_t)(e - fighters) + 4 <= fightersLen; e += 4) {
        if ((short)(e[0] | (e[1] << 8)) == 0) {
            break; /* the Digimon has no fighter: no script at all */
        }
        if ((short)(e[0] | (e[1] << 8)) == digimon) {
            break;
        }
    }
    if ((size_t)(e - fighters) + 4 <= fightersLen && (short)(e[0] | (e[1] << 8)) == digimon) {
        info = e[3] >= 0x3A ? pcLe32(fighters + 12) + (unsigned)e[2] * 0x48 : pcLe32(fighters + 8) + (unsigned)e[2] * 0xC4;
        effects = (size_t)info + 12 <= fightersLen ? pcLe32(fighters + info + 8) : 0;
        file = effects >> 16;
        entry = effects & 0xFFFF;
        if (file != 0) {
            data = pcCdReadDiscFile((int)file, &len);
            if (data == NULL) {
                return -1;
            }
            if ((size_t)entry * 4 + 4 <= len && (at = pcLe32(data + entry * 4), (size_t)at + 4 <= len)) {
                n = pcLe32(data + at) / 4; /* the first slot's offset: where the table ends */
                for (i = 0; i < n && i < 18 && (size_t)at + (size_t)i * 4 + 4 <= len; i++) {
                    if (pcLe32(data + at + i * 4) != 0) {
                        mask |= 1 << i;
                    }
                }
            }
            free((void *)data);
        }
    }
    if (cached < PROBE_CACHE) {
        cache[cached].digimon = digimon;
        cache[cached].mask = mask;
        cached++;
    }
    return mask;
}

static int pcFighterSlotProbe(int digimon, int slot) {
    int mask = pcFighterSlotMask(digimon);

    if (mask < 0) {
        return -1;
    }
    return slot >= 0 && slot < 18 && (mask >> slot & 1);
}

/* Digimon slice 1 D1: the changes of the content mods to existing Digimon. The partner rows are patched here; the enemy rows
   are patched on every read of the battle table (pcPatchBattleTable). The original table is read from the disc for the enemy
   ids, so it comes first; names of techniques and items resolve, so it comes after both. */
_Static_assert(sizeof(Evolution) == sizeof(PcEvolution) && offsetof(Evolution, needs) == offsetof(PcEvolution, needs) &&
                   offsetof(Evolution, stat) == offsetof(PcEvolution, stat) && offsetof(Evolution, value) == offsetof(PcEvolution, value),
               "ext_state.c keeps the evolution lists as PcEvolution");
_Static_assert(sizeof(BattleTableEntry) == PC_BATTLE_ROW_SIZE, "digimon_ext.c reads BattleTableEntry as bytes");

/* Slice 2 D3: new enemies are rows of the battle table and entries of the fighters file. Both files are served from memory,
   built from the disc's bytes plus the mods' additions and longer than the disc's when needed (port_fs.h, generated layer): the
   file cache sizes its buffer from the length the wrapped getSectorCount reports, so the original files' sizes cap nothing. */
static void pcServeNewEnemyFiles(void) {
    size_t len = 0;
    unsigned char *file = digimonExtBuildBattleTable(&len);

    if (file != NULL) {
        pfsSetGenerated(FILE_BATTLE_TABLE, file, len);
        free(file);
    }
    file = digimonExtBuildFighters(&len);
    if (file != NULL) {
        pfsSetGenerated(FILE_FIGHTERS, file, len);
        free(file);
    }
}

/* content-export E5c: game_tables.c keeps its own mirror of the game's Encounter (it cannot include field_mode.h) */
_Static_assert(sizeof(GameFieldEncounter) == sizeof(Encounter) && offsetof(GameFieldEncounter, enemies) == offsetof(Encounter, enemies) &&
                   offsetof(GameFieldEncounter, ambushChance) == offsetof(Encounter, ambushChance) &&
                   offsetof(GameFieldEncounter, kind) == offsetof(Encounter, kind) &&
                   offsetof(GameFieldEncounter, blocks) == offsetof(Encounter, blocks) && sizeof(((Encounter *)0)->blocks) == 12,
               "game_tables.c reads Encounter as GameFieldEncounter");

/* content-export E5d: game_tables.c keeps its own mirror of the card game's records (it cannot include the game's headers) */
_Static_assert(sizeof(GameCardPrice) == sizeof(CardPrice) && offsetof(GameCardPrice, card) == offsetof(CardPrice, card) &&
                   offsetof(GameCardPrice, price) == offsetof(CardPrice, price),
               "game_tables.c reads CardPrice as GameCardPrice");
_Static_assert(sizeof(GameCardStock) == sizeof(CardShopStock) && offsetof(GameCardStock, shop) == offsetof(CardShopStock, shop) &&
                   offsetof(GameCardStock, count) == offsetof(CardShopStock, count) && offsetof(GameCardStock, cards) == offsetof(CardShopStock, cards),
               "game_tables.c reads CardShopStock as GameCardStock");
_Static_assert(sizeof(GameCardPack) == sizeof(CardPack) && offsetof(GameCardPack, item) == offsetof(CardPack, item) &&
                   offsetof(GameCardPack, slots) == offsetof(CardPack, slots) && sizeof(((CardPack *)0)->slots) == sizeof(((GameCardPack *)0)->slots),
               "game_tables.c reads CardPack as GameCardPack");
_Static_assert(sizeof(GameCardEffect) == sizeof(CardEffect) && offsetof(GameCardEffect, condition) == offsetof(CardEffect, condition) &&
                   offsetof(GameCardEffect, playCondition) == offsetof(CardEffect, playCondition) &&
                   offsetof(GameCardEffect, message) == offsetof(CardEffect, message) && offsetof(GameCardEffect, target) == offsetof(CardEffect, target) &&
                   offsetof(GameCardEffect, script) == offsetof(CardEffect, script) && sizeof(((CardEffect *)0)->script) == 40,
               "game_tables.c reads CardEffect as GameCardEffect");

/* content-export E5e: the gym's records (INNS is defined in inn.c with no header: nm -S of the EU build gives its 22 x 8 bytes) */
_Static_assert(sizeof(GameTrainEntry) == sizeof(TrainEntry) && offsetof(GameTrainEntry, id) == offsetof(TrainEntry, id) &&
                   offsetof(GameTrainEntry, stat) == offsetof(TrainEntry, stat) && offsetof(GameTrainEntry, other) == offsetof(TrainEntry, other),
               "game_tables.c reads TrainEntry as GameTrainEntry");
_Static_assert(sizeof(GameTrainGain) == sizeof(TrainGain) && offsetof(GameTrainGain, base) == offsetof(TrainGain, base) &&
                   offsetof(GameTrainGain, range) == offsetof(TrainGain, range),
               "game_tables.c reads TrainGain as GameTrainGain");
_Static_assert(sizeof(GameTrainInfo) == sizeof(TrainInfo) && offsetof(GameTrainInfo, name) == offsetof(TrainInfo, name) &&
                   offsetof(GameTrainInfo, desc) == offsetof(TrainInfo, desc) && offsetof(GameTrainInfo, icons) == offsetof(TrainInfo, icons),
               "game_tables.c reads TrainInfo as GameTrainInfo");

/* Slice 2 D4: the "encounters" of digimon.json. The encounter table is the FIELDSTG overlay's data, restored on every entry to
   the field, so the placements go on right after that (the "fieldstg" scope's onEnter hook), pointing an encounter's slot at a
   row the content owns; the original rows are never written, and with no mod nothing is registered. */
static void pcApplyEncounterPlacements(StateScope *scope, void *user) {
    int i;

    (void)scope;
    (void)user;
    gameTablesApplyField(); /* content-export E5c: the field tables first; the placements below go over the patched slots */
    for (i = 0; i < digimonExtPlaceCount(); i++) {
        int enc, slot;
        BattleEnemy *row;

        digimonExtPlaceTarget(i, &enc, &slot);
        row = digimonExtPlaceRow(i, FIELDSTG_encounters[enc].enemies[slot]);
        if (row != NULL) {
            FIELDSTG_encounters[enc].enemies[slot] = row;
        }
    }
}

extern Evolution STFGTREP_evolutionLists[8][44]; /* data/stfgtrep.c: the lists the port extends (a copy) */

static void pcInstallDigimon(void) {
    size_t len = 0;
    unsigned char *file;

    pfsClearVirtualFiles(); /* a reload: the numbers start again from the base (digimonExtLoad forgets the names) */
    file = pcCdReadDiscFile(FILE_BATTLE_TABLE, &len);

    digimonExtSetEnemyTable(file, len);
    pcEvolutionSetOriginal(STFGTREP_evolutionLists);
    free(file);
    file = pcCdReadDiscFile(FILE_FIGHTERS, &len);
    digimonExtSetFighters(file, len);
    free(file);
    digimonExtLoad();
    pcServeNewEnemyFiles();
}

/* extended techs T2/T3: the techniques of the content mods (store, ids, learnedBy, names). After the disc is open: the
   animation check of learnedBy reads the fighters file */
static void pcInstallTechs(void) {
    techExtSetFighterProbe(pcFighterSlotProbe);
    techExtLoad();
    sView.techName = techExtNameOf;
    sView.formName = digimonExtNewName;
    saveViewSetContext(&sView);
    pcInstallDigimon();
}

static void pcBootExitNote(void) {
    pcItemDump();
    printf("boot: the run ended after %d presented frame(s); a missing "
           "'presented N frames' summary means the game window was "
           "closed (PsyX_Exit, SDL_QUIT)\n", pcBootPresented());
}

/*
 * The two areas of the memory map main.c names. On the PlayStation they are
 * linker addresses (0x80082CB0 and 0x800A5DE0, mk/version/eu.mk); here they
 * are plain buffers of a size that would hold any overlay. Nothing is ever
 * copied into them on the host -- that is exactly the design consequence
 * above: modes are linked in, not loaded.
 */
u8 OVERLAY_VRAM[0x40000];
u8 STAGE_VRAM[0x80000];

/*
 * The port's own entry-point table, by `mode >> 8` (overlay.h's MODE_*): the
 * index the game uses to pick an overlay file, here picking the C that was
 * linked into this binary. The country select is MODE_COUNTRY_SELECT
 * (0x1600), so its entry point is at index 0x16 -- 22, where the game's own
 * MODE_ENTRY_POINTS has it too.
 */
static Task *pcStartCountrySelect(void) {
    return CNTY_SEL_start();
}

/* T18: STDWTITL, the title overlay. Every mode whose high byte is 0xE is its
   (MODE_TITLE 0xE00, MODE_OPENING 0xE01, MODE_OPENING_2 0xE02, ...), and its
   own STDWTITL_start picks between the title screen, a movie or the still
   screen by the mode's low byte (src/menus/title/scene.c). The port links the
   overlay once, exactly as the game's own MODE_ENTRY_POINTS holds one entry per
   overlay; the low byte decides what runs. */
static Task *pcStartTitle(void) {
    return STDWTITL_start();
}

/* T19: FIELDSTG, the field mode (src/field/). Index 2 is where a new game
   starts (MODE_NEW_GAME 0x2D7) and where the field modes 0x200/0x201 land;
   FIELDSTG_start creates its root task, which creates the field, picks the
   mode's stage (FIELDSTG_stages, linked from src/field/stages/ and renamed
   WSTAG###_*) and runs it. */
static Task *pcStartField(void) {
    return FIELDSTG_start();
}

/* T44: STPLNMET, the player's name entry (MODE_PLAYER_NAME 0x500 -> index 5).
   The intro's cutscene chain lands here: the second stage's own event leaves
   the field to this mode, where the game asks for the player's name and the
   partners before going back to the field. Its units are drawing units like
   the title's (PsyCross headers, C11). */
static Task *pcStartPlayerName(void) {
    return STPLNMET_start();
}

/* T4: STFGTREP, the battle report (MODE_BATTLE_REPORT 0x1400 -> index 20,
   where the game's own MODE_ENTRY_POINTS has it too: overlay.c). A won
   battle leaves WFIGHTMN for this mode; its start is task-shaped like the
   field's and links through the battle unit's wildcard rule. */
static Task *pcStartBattleReport(void) {
    return STFGTREP_start();
}

/* T3: FIGHTSTG, the battle mode (MODE_BATTLE 0x600 -> index 6, where the
   game's own MODE_ENTRY_POINTS has it too, src/engine/system/overlay.c). Its
   units live in src/battle/ and src/debug/battle_test/ and are linked like
   the field's (pc/Makefile's BATTLE_*). FIGHTSTG_start creates the mode's
   root task, which picks between the battle test (WFIGHTTS, when the mode's
   argument is nonzero) and the battle menu (WFIGHTMN) by the argument. */
static void pcSeedTechs(void);

static Task *pcStartBattle(void) {
    pcSeedTechs();
    return FIGHTSTG_start();
}

/* M1: STSTATUS, the field menu's screens (MODE_STATUS 0x1000 -> index 16,
   where the game's own MODE_ENTRY_POINTS has it too). Units: src/menus/
   field_menu/ (pc/Makefile's FMENU_*). */
static Task *pcStartFieldMenu(void) {
    pcSeedTechs();
    return STSTATUS_start();
}

/* new partner forms P2, test harness only: DW3_SEED_PARTNER="<partner>.<entry>=<name>[@<level>][/<slot>],..." puts a Digimon (a new
   form by its "<mod>:<key>", or an original id) into a partner's entry (level 1 unless given) and picks it as the partner's form
   <slot> (0..2, default 0); the slot 0 form is also the one the next battle starts as (Partner.battleDigivolve). It runs before the
   battle, the field menu, the lab or the card screen opens. Normal play obtains a new form through P3 (evolution lists); until
   then this is how a form gets into a save. */
static void pcSeedPartner(void) {
    const char *seed = getenv("DW3_SEED_PARTNER");

    while (seed != NULL && *seed != '\0') {
        char *end, name[96];
        long p = strtol(seed, &end, 10), e, level = 1, slot = 0;
        int id, n = 0;

        if (end == seed || *end != '.') {
            break;
        }
        e = strtol(end + 1, &end, 10);
        if (*end != '=') {
            break;
        }
        end++;
        while (*end != '\0' && *end != ',' && *end != '@' && *end != '/' && n < (int)sizeof(name) - 1) {
            name[n++] = *end++;
        }
        name[n] = '\0';
        if (*end == '@') {
            level = strtol(end + 1, &end, 10);
        }
        if (*end == '/') {
            slot = strtol(end + 1, &end, 10);
        }
        id = digimonExtNewId(name);
        if (id < 0) {
            id = (int)strtol(name, NULL, 10);
        }
        if (p >= 0 && p < PARTNER_COUNT && e >= 0 && e < PC_PARTNER_ENTRY_MAX && slot >= 0 && slot < PARTNER_SLOT_COUNT &&
            id >= FIRST_ENTRY_ID && GET_DIGIMON(id) != NULL) {
            PartnerStats *ps = &GAME.partners[p].info;

            while (pcPartnerEntryLimit(p) <= e && pcPartnerEntryGrow(p) >= 0) {
            }
            PC_PARTNER_ENTRY(p, e).id = (s16)id;
            PC_PARTNER_ENTRY(p, e).level = (s8)(level >= 1 && level <= 99 ? level : 1);
            /* what the game's growth would have taught it by that level (STFGTREP_addSkill), known, so a battle can use it */
            {
                const DigimonData *dd = GET_DIGIMON(id);
                int k;

                for (k = 0; k < 6; k++) {
                    PC_PARTNER_ENTRY(p, e).skills[k] = (s16)((dd->skills[k + 1] != 0 && PC_PARTNER_ENTRY(p, e).level >= dd->skillLevels[k])
                                                         ? (dd->skills[k + 1] | SKILL_KNOWN) : 0);
                }
            }
            ps->slots[slot] = (s16)id;
            if (slot == 0) {
                GAME.partners[p].battleDigivolve = id;
            }
        } else {
            fprintf(stderr, "seed: DW3_SEED_PARTNER \"%s\" is no Digimon here\n", name);
        }
        seed = (*end == ',') ? end + 1 : end;
    }
}

/* extended techs T2, test harness only: DW3_SEED_TECHS="partner.entry.slot=<mod>:<key>,..." (entry: a number, or "s" for the
   entries picked for battle) puts a technique (by name, or an id) into a partner's entry, known, before the card screen, a battle, the lab or the field menu opens, so a battle
   or a save carries it (a save stores it by name) */
static void pcSeedTechs(void) {
    const char *seed = getenv("DW3_SEED_TECHS");

    pcSeedPartner();

    while (seed != NULL && *seed != '\0') {
        char *end, name[96];
        long p = strtol(seed, &end, 10), e, k;
        int id, n = 0;

        if (end == seed || *end != '.') {
            break;
        }
        if (end[1] == 's') { /* every entry the partner has picked for battle (info.slots) */
            e = -2;
            end += 2;
        } else {
            e = strtol(end + 1, &end, 10);
        }
        if (*end != '.') {
            break;
        }
        k = strtol(end + 1, &end, 10);
        if (*end != '=') {
            break;
        }
        end++;
        while (*end != '\0' && *end != ',' && n < (int)sizeof(name) - 1) {
            name[n++] = *end++;
        }
        name[n] = '\0';
        id = techExtIdOf(name);
        if (id < 0) {
            id = (int)strtol(name, NULL, 10);
        }
        if (p >= 0 && p < PARTNER_COUNT && k >= 0 && k < 6 && id > 0) {
            if (e >= 0 && e < PC_PARTNER_ENTRY_LIMIT(p)) {
                PC_PARTNER_ENTRY(p, e).skills[k] = (s16)(id | SKILL_KNOWN);
            } else if (e == -2) {
                int j, c;

                for (j = 0; j < PARTNER_SLOT_COUNT; j++) {
                    for (c = 0; c < PC_PARTNER_ENTRY_LIMIT(p); c++) {
                        if (GAME.partners[p].info.slots[j] != 0 && PC_PARTNER_ENTRY(p, c).id == GAME.partners[p].info.slots[j]) {
                            PC_PARTNER_ENTRY(p, c).skills[k] = (s16)(id | SKILL_KNOWN);
                        }
                    }
                }
            }
        }
        seed = (*end == ',') ? end + 1 : end;
    }
}

/* M2: STGMCARD, the memory card screen (MODE_CONTINUE 0xC00 -> index 12): the
   title's CONTINUE loads from it and a save point saves with it (a negative
   mode argument). Units: src/menus/memory_card/ (pc/Makefile's MCARD_*); the
   card is shim/host_memcard.c. */
static Task *pcStartMemCard(void) {
    /* extended-state T2, test harness only: DW3_SEED_ITEMS="id=count,id=count" gives items before the card
       screen opens (a save then carries them; ids above the original range live in the extension) */
    const char *seed = getenv("DW3_SEED_ITEMS");

    while (seed != NULL && *seed != '\0') {
        char *end;
        long id = strtol(seed, &end, 10);
        long count;

        if (end == seed || *end != '=') {
            break;
        }
        count = strtol(end + 1, &end, 10);
        PC_ITEM_COUNT((int)id) = (s8)count;
        seed = (*end == ',') ? end + 1 : end;
    }
    pcSeedTechs();
    return STGMCARD_start();
}

/* M5: the gym (MODE_TRAINING 0xA00 -> 10), the partner name entry (MODE_NAMING
   0xB00 -> 11) and the Digimon Lab (MODE_DIGI_LAB 0xD00 -> 13). Units: src/menus/
   gym, partner_name, digimon_lab (pc/Makefile's GYM_*, PNAME_*, DLAB_*). */
static Task *pcStartGym(void) {
    return STGTRAIN_start();
}

static Task *pcStartPartnerName(void) {
    return STDGNAME_start();
}

static Task *pcStartDigiLab(void) {
    pcSeedTechs();
    return STGDGLAB_createScene();
}

/* M4: the item shop (MODE_ITEM_SHOP 0xF00 -> 15) and the card shop
   (MODE_CARD_SHOP 0x1300 -> 19). Units: src/menus/item_shop and
   src/cardgame/card_packs (pc/Makefile's ISHOP_*, CSHOP_*). */
static Task *pcStartItemShop(void) {
    return STITSHOP_start();
}

static Task *pcStartCardShop(void) {
    return STCRDSHP_start();
}

/* M6a: the deck editor (MODE_DECK_EDITOR 0x400 -> 4) and the card album
   (MODE_CARD_ALBUM 0x1200 -> 18). Units: src/cardgame/deck_editor and album
   (pc/Makefile's DECK_*, ALBUM_*). */
static Task *pcStartDeckEditor(void) {
    return STCRDDEK_start();
}

static Task *pcStartCardAlbum(void) {
    return STCRDABM_start();
}

/* M6b: the card battle (MODE_CARD_GAME 0x700 -> 7). Units: src/cardgame/
   card_battle (pc/Makefile's CGAME_*). */
static Task *pcStartCardGame(void) {
    return CARDGAME_start();
}

static Task *(*const PC_MODE_ENTRY_POINTS[23])(void) = {
    [MODE_COUNTRY_SELECT >> 8] = pcStartCountrySelect,
    [MODE_NEW_GAME >> 8] = pcStartField,
    [MODE_TITLE >> 8] = pcStartTitle,
    [MODE_PLAYER_NAME >> 8] = pcStartPlayerName,
    [MODE_BATTLE >> 8] = pcStartBattle,
    [MODE_BATTLE_REPORT >> 8] = pcStartBattleReport,
    [MODE_STATUS >> 8] = pcStartFieldMenu,
    [MODE_CONTINUE >> 8] = pcStartMemCard,
    [MODE_TRAINING >> 8] = pcStartGym,
    [MODE_NAMING >> 8] = pcStartPartnerName,
    [MODE_DIGI_LAB >> 8] = pcStartDigiLab,
    [MODE_ITEM_SHOP >> 8] = pcStartItemShop,
    [MODE_CARD_SHOP >> 8] = pcStartCardShop,
    [MODE_DECK_EDITOR >> 8] = pcStartDeckEditor,
    [MODE_CARD_ALBUM >> 8] = pcStartCardAlbum,
    [MODE_CARD_GAME >> 8] = pcStartCardGame,
};

/*
 * State scopes (shim/state_scope.h): the statics of an overlay go back to their initial values when
 * the game would load that overlay (overlay-state-reset). pc/ovl_sections.ld gathers the writable
 * statics of every overlay into one section each (and the build generates one per field stage,
 * build/stage_scopes.c); the tables below say which mode id and which sub-overlay file enter them.
 * pc/check_scopes.py fails the link if a writable symbol of those objects lies outside its range.
 * The mode entry-point table above stays as it is: it says what to start, the scope tables say what
 * to reset, and the two are looked up side by side in pcUpdateModeTask.
 */
#define PC_OVL(name) extern char __ovl_##name##_data_start[], __ovl_##name##_data_end[]
PC_OVL(fightstg);
PC_OVL(wfightmn);
PC_OVL(wfightts);
PC_OVL(stfgtrep);
PC_OVL(fieldstg);
PC_OVL(ststatus);
PC_OVL(stgmcard);
PC_OVL(stgdglab);
PC_OVL(stdgname);
PC_OVL(stgtrain);
PC_OVL(stitshop);
PC_OVL(stcrdshp);
PC_OVL(stcrddek);
PC_OVL(stcrdabm);
PC_OVL(cardgame);
PC_OVL(stdwtitl);
PC_OVL(stplnmet);
PC_OVL(cntysel);
extern const StateScopeRange PC_STAGE_SCOPE_RANGES[]; /* build/stage_scopes.c */

/* A mode overlay and the mode id (mode >> 8) that enters it. A mode id always enters the same
   scope; the original's no-reset cases (field -> field, the title and its movies within 0xE) fall
   out of stateScopeModeEntered, which only resets when the id changes. */
typedef struct PcModeScope {
    const char *name;
    char *begin, *end;
    int modeId;
} PcModeScope;

#define PC_MODE_SCOPE(name, mode) {#name, __ovl_##name##_data_start, __ovl_##name##_data_end, (mode) >> 8}
static const PcModeScope PC_MODE_SCOPES[] = {
    PC_MODE_SCOPE(fightstg, MODE_BATTLE),
    PC_MODE_SCOPE(stfgtrep, MODE_BATTLE_REPORT),
    PC_MODE_SCOPE(fieldstg, MODE_NEW_GAME), /* 0x200, 0x201, 0x2D7...: one id, so no reset field -> field */
    PC_MODE_SCOPE(ststatus, MODE_STATUS),
    PC_MODE_SCOPE(stgmcard, MODE_CONTINUE),
    PC_MODE_SCOPE(stgdglab, MODE_DIGI_LAB),
    PC_MODE_SCOPE(stdgname, MODE_NAMING),
    PC_MODE_SCOPE(stgtrain, MODE_TRAINING),
    PC_MODE_SCOPE(stitshop, MODE_ITEM_SHOP),
    PC_MODE_SCOPE(stcrdshp, MODE_CARD_SHOP),
    PC_MODE_SCOPE(stcrddek, MODE_DECK_EDITOR),
    PC_MODE_SCOPE(stcrdabm, MODE_CARD_ALBUM),
    PC_MODE_SCOPE(cardgame, MODE_CARD_GAME),
    PC_MODE_SCOPE(stdwtitl, MODE_TITLE), /* the title and its movies (0xE00..0xE0B) share the id */
    PC_MODE_SCOPE(stplnmet, MODE_PLAYER_NAME),
    PC_MODE_SCOPE(cntysel, MODE_COUNTRY_SELECT),
};

/* A stage's scope is a child of the field's, bound by every file id whose entry runs its startStage
   (the European extra chapter reuses stage code with other files; each file id resets on change,
   as each is its own .PRO on the PlayStation). Stages no table names stay out of the tables. */
static void pcBindStages(StateScope *field, const StageEntry *entry) {
    int guard;

    for (guard = 0; entry->mode != 0 && guard < 4096; entry++, guard++) {
        const StateScopeRange *r;

        for (r = PC_STAGE_SCOPE_RANGES; r->name != NULL; r++) {
            if (r->key != NULL && r->key == (const void *)entry->init) {
                StateScope *scope = stateScopeFind(r->name);

                if (scope == NULL) {
                    scope = stateScopeCreate(r->name, field);
                    stateScopeAddRange(scope, r->begin, r->end);
                }
                stateScopeBindSub(entry->file, scope);
                break;
            }
        }
    }
}

/* ADR-007: the first data table, what a battle gives (STFGTREP_rewards, one row per encounter number).
   Its rows live in the report overlay's data, which the scope restores on every entry to the report
   mode, so the patches go on right after that restore (the "stfgtrep" scope's onEnter hook). The caps
   are the ones the report code applies: Digimon EXP and money stop at 9999999 (growth.c, report.c),
   a partner's EXP at 999999 (growth.c, partner.c). */
#define PC_REWARD_ROWS 335 /* the EU table (docs/data-tables.md) */
static const TmField PC_REWARD_FIELDS[] = {
    {"digimonExp", TM_S32, offsetof(BattleReward, digimonExp), 0, 9999999,
     "EXP shared by the Digimon that were used in the battle."},
    {"exp", TM_S32, offsetof(BattleReward, exp), 0, 999999,
     "EXP shared by the partners that fought (the report gives 100%, 60% or 33% of it by how many fought)."},
    {"money", TM_S32, offsetof(BattleReward, money), 0, 9999999,
     "Money the battle gives (a fifth more with the item that boosts it); the wallet stops at 9999999."},
};
static const TmTable PC_REWARD_TABLE = {
    "battle_rewards", STFGTREP_rewards, sizeof(BattleReward), PC_REWARD_ROWS, 0,
    "The encounter number (the row of the battle in the table), starting at 0.",
    "Every battle report reads its row when the report opens, so a change shows in the next battle.",
    PC_REWARD_FIELDS, (int)(sizeof(PC_REWARD_FIELDS) / sizeof(PC_REWARD_FIELDS[0]))};

/* Log trace on the tables channel (DW3_TABLE_TRACE=1): one log line per report entry and exit with the row the report will read and the
   wallet, to see a table mod at work */
static void pcTraceReward(const char *when) {
    if (plogOn("tables", PLOG_TRACE)) {
        const BattleReward *row = &STFGTREP_rewards[BATTLE_RESULT.battle];

        plogLine("tables", "report %s: encounter %d digimonExp %d exp %d money %d, wallet %d\n", when,
                (int)BATTLE_RESULT.battle, (int)row->digimonExp, (int)row->exp, (int)row->money, (int)GAME.money);
        if (BATTLE_RESULT.item != 0) {
            plogLine("tables", "report %s: dropped item %d, in the bag %d\n", when, (int)BATTLE_RESULT.item,
                    (int)PC_ITEM_COUNT(BATTLE_RESULT.item));
        }
    }
}

/* The mods log (T5; level mods, channel tables): when a mod changed the row this battle reads, one line with the
   values in use and, where a mod changed them, the original ones. Nothing when the log is off or the
   row is the original. */
static void pcModLogReward(void) {
    const BattleReward *row, *orig;
    char who[256], line[400];
    int n;

    if (!plogOn("tables", PLOG_MODS) || tmPatchCount("battle_rewards") == 0) {
        return;
    }
    row = &STFGTREP_rewards[BATTLE_RESULT.battle];
    orig = (const BattleReward *)tmOriginalRow("battle_rewards", (long)BATTLE_RESULT.battle);
    if (orig == NULL || (row->exp == orig->exp && row->money == orig->money && row->digimonExp == orig->digimonExp)) {
        return;
    }
    tmWho("battle_rewards", who, sizeof who);
    n = snprintf(line, sizeof line, "battle_rewards encounter %d:", (int)BATTLE_RESULT.battle);
    if (row->exp != orig->exp) {
        n += snprintf(line + n, sizeof line - (size_t)n, " exp %d (orig %d),", (int)row->exp, (int)orig->exp);
    }
    if (row->money != orig->money) {
        n += snprintf(line + n, sizeof line - (size_t)n, " money %d (orig %d),", (int)row->money, (int)orig->money);
    }
    if (row->digimonExp != orig->digimonExp) {
        n += snprintf(line + n, sizeof line - (size_t)n, " digimonExp %d (orig %d),", (int)row->digimonExp,
                      (int)orig->digimonExp);
    }
    if (line[n - 1] == ',') {
        line[n - 1] = '\0';
    }
    plogLine("tables", "%s by %s\n", line, who);
}

static void pcModLogLoot(void); /* below, with the loot table */

static void pcApplyRewardPatches(StateScope *scope, void *user) {
    (void)scope;
    (void)user;
    tmApply("battle_rewards");
    pcModLogReward();
    pcModLogLoot();
    pcTraceReward("enter");
}

static void pcTraceRewardExit(StateScope *scope, void *user) {
    (void)scope;
    (void)user;
    pcTraceReward("exit");
}

/* ADR-007: the loot of each enemy, in the battle table (disc file FILE_BATTLE_TABLE, BattleTableEntry, one
   record per enemy id). Not a C array of the host: the game reads the file into its cache, so the patch
   goes on the buffer right after each read of the file (port_fs.h post-read hook), from the bytes the
   read produced (the disc's, or a raw override's) every time. `item` is what the enemy may leave (0: none,
   the enemy then leaves nothing and its chance is not used); `itemChance` is in 1024ths, less one: the
   drop roll is `itemChance + 1 > rand % 1024` (normal_battle/menu.c, WFIGHTMN_endBattle), so 1023 always
   drops, and the same value scales the steal roll (rolls.c). The ranges are the table's own: item ids are
   1..402 (ITEMS) plus the ids of the items content mods add (itemExtByName / itemExtIdOk), the chance fits 0..1023. */
static const TmField PC_LOOT_FIELDS[] = {
    {"item", TM_S16, offsetof(BattleTableEntry, item), 0, 32767,
     "The item the enemy may leave (0: none; 1 to 402 are the game's items). An item a content mod adds is written by its "
     "name (\"<mod>:<name>\"), because its number depends on the installed mods. An enemy whose original item is 0 never "
     "drops, so give it one here.",
     0, 0, itemExtByName, itemExtIdOk},
    {"itemChance", TM_S16, offsetof(BattleTableEntry, itemChance), 0, 1023,
     "Chance of leaving the item, in 1024ths less one: the drop roll is itemChance + 1 > a number from 0 to 1023, so "
     "1023 always drops. It also scales how easily the item can be stolen."},
};
static const TmTable PC_LOOT_TABLE = {
    "battle_loot", NULL, sizeof(BattleTableEntry), 0, 0,
    "The enemy id (BattleTableEntry.id).",
    "The battle table is read when a battle starts, so a change shows in the next battle.",
    PC_LOOT_FIELDS, (int)(sizeof(PC_LOOT_FIELDS) / sizeof(PC_LOOT_FIELDS[0])), 1, offsetof(BattleTableEntry, id), TM_S16};

/* The loot rows the last patched read produced, for the mod log: the battle table itself is only in the
   file cache, which the report does not own */
#define PC_LOOT_LIVE_MAX 2048
static struct {
    short id, item, chance;
} pcLootLive[PC_LOOT_LIVE_MAX];
static int pcLootLiveCount;

static void pcPatchBattleTable(int file, int offset, int sectors, uint8_t *dst, PfsSource src) {
    int rows, i;

    (void)src;
    if (file == FILE_CARDGAME_OPPONENTS && offset == 0) {
        gameTablesPatchCardOpponents(dst, (size_t)sectors * 2048); /* modding-ux U8a: the opponents' decks, the same way */
        return;
    }
    if (file != FILE_BATTLE_TABLE || offset != 0) {
        return;
    }
    /* the Digimon content first, the battle_loot table mod on top (it wins for item and itemChance) */
    digimonExtPatchBattleTable(dst, (size_t)sectors * 2048);
    if (tmPatchCount("battle_loot") == 0) {
        return;
    }
    if (tmPatchBuffer("battle_loot", dst, (size_t)sectors * 2048) <= 0) {
        return;
    }
    rows = 0;
    for (i = 0; (size_t)(i + 1) * sizeof(BattleTableEntry) <= (size_t)sectors * 2048 && rows < PC_LOOT_LIVE_MAX; i++) {
        const BattleTableEntry *e = (const BattleTableEntry *)dst + i;

        if (e->id == 0) {
            break;
        }
        pcLootLive[rows].id = e->id;
        pcLootLive[rows].item = e->item;
        pcLootLive[rows].chance = e->itemChance;
        rows++;
    }
    pcLootLiveCount = rows;
}

/* The mods log for the loot (T6): one line per enemy of the battle whose item or chance a mod changed, then
   the result of the roll. BATTLE_RESULT.item is what the drop roll left (0: nothing). */
static void pcModLogLoot(void) {
    char who[256];
    int i, k, logged = 0;

    if (!plogOn("tables", PLOG_MODS) || tmPatchCount("battle_loot") == 0) {
        return;
    }
    tmWho("battle_loot", who, sizeof who);
    for (i = 0; i < 3; i++) {
        s32 id = FIGHTSTG_battle.state.fighters[1][i].id;
        const BattleTableEntry *orig;

        if (id == 0 || (orig = (const BattleTableEntry *)tmOriginalRow("battle_loot", (long)id)) == NULL) {
            continue;
        }
        for (k = 0; k < pcLootLiveCount; k++) {
            if (pcLootLive[k].id == id) {
                break;
            }
        }
        if (k == pcLootLiveCount || (pcLootLive[k].item == orig->item && pcLootLive[k].chance == orig->itemChance) ||
            (pcLootLive[k].item == 0 && orig->item == 0)) {
            continue; /* unchanged, or an enemy that carries nothing: its chance is never used */
        }
        plogLine("tables", "battle_loot enemy %d: item %d (orig %d), itemChance %d (orig %d) by %s\n", (int)id,
                (int)pcLootLive[k].item, (int)orig->item, (int)pcLootLive[k].chance, (int)orig->itemChance, who);
        logged = 1;
    }
    if (logged) {
        if (BATTLE_RESULT.item != 0) {
            plogLine("tables", "battle_loot drop: item %d\n", (int)BATTLE_RESULT.item);
        } else {
            plogLine("tables", "battle_loot drop: none\n");
        }
    }
}

static void pcRegisterLootTable(void) {
    size_t len = 0;
    unsigned char *file = pcCdReadDiscFile(FILE_BATTLE_TABLE, &len);

    if (file == NULL) {
        return; /* no disc image open (a tool): nothing to patch */
    }
    if (tmRegisterFile(&PC_LOOT_TABLE, file, len) == 0 || digimonExtEnemyPatchCount() > 0) {
        pfsSetPostRead(pcPatchBattleTable);
    }
    free(file);
    /* modding-ux U8a: CARD_NPC.BIN, the card opponents (a disc file as well; the same hook patches it) */
    file = pcCdReadDiscFile(FILE_CARDGAME_OPPONENTS, &len);
    if (file != NULL) {
        if (gameTablesRegisterCardOpponents(file, len) == 0 && tmPatchCount("card_opponents") > 0) {
            pfsSetPostRead(pcPatchBattleTable);
        }
        free(file);
    }
}

/* ADR-007: the items' prices (ItemInfo.price, what a shop charges, and sellPrice, what it pays; 0 = cannot be
   sold). ITEMS is a table of the executable, which nothing resets, so the patches go on once, right after the
   scopes are made and before the first mode (point A of docs/data-tables.md). Item n is ITEMS[n - 1]. The
   shop reads the price field itself (trade.c, info.c: price * quantity), the sell price is its own field,
   not a fraction of the buy price. Both are u16 in the game. */
#define PC_ITEM_ROWS 402
static const TmField PC_ITEM_FIELDS[] = {
    {"price", TM_U16, offsetof(ItemInfo, price), 0, 65535,
     "What a shop charges for one (the buy list, the total and the money taken all read it; 0 is free)."},
    {"sellPrice", TM_U16, offsetof(ItemInfo, sellPrice), 0, 65535,
     "What a shop pays for one when it is sold (its own value, not a fraction of price). 0: the item cannot be sold."},
};
/* The key of a row is the item id, or the name of an item a content mod adds ("<mod>:<name>"): that row changes the
   price of the mod's item record (its declaration keeps the default), in mod order like any other patch. */
static const TmTable PC_ITEM_TABLE = {
    "item_prices", ITEMS, sizeof(ItemInfo), PC_ITEM_ROWS, 1,
    "The item id (the number the game uses for the item, starting at 1), or \"<mod>:<name>\" for an item a content mod adds (that row changes the price of the mod's record).",
    "Set once when the game starts; every shop reads it when it opens, so a change shows in all of them.",
    PC_ITEM_FIELDS, (int)(sizeof(PC_ITEM_FIELDS) / sizeof(PC_ITEM_FIELDS[0])), 0, 0, TM_S32, itemExtByName, itemExtRow};

/* ADR-007: what each item shop sells. STITSHOP_shops[n] is { count, pointer to the item ids, ended by 0 }; the
   shop number is the number the mode is opened with. A list field: a mod gives the whole list or edits it
   (add / remove), the engine points the shop at its own buffer and leaves the game's arrays alone. The list
   may be longer than the original (the shop pages it by count), up to 64 items (the biggest original shop
   has 52). Item ids are 1..402 (0 ends the list). The shop's data is reset every time the mode is entered,
   so the patch goes on in the scope's onEnter hook, after that restore (point B). */
#define PC_SHOP_ROWS 31
#define PC_SHOP_LIST_MAX 64
static const TmField PC_SHOP_FIELDS[] = {
    {"items", TM_S16, offsetof(ShopList, items), 1, 32767,
     "The item ids the shop sells, in the order the shop lists them. An item a content mod adds is written by its name "
     "(\"<mod>:<name>\"), because its number depends on the installed mods.",
     PC_SHOP_LIST_MAX, offsetof(ShopList, count), itemExtByName, itemExtIdOk},
};
static const TmTable PC_SHOP_TABLE = {
    "shop_stock", STITSHOP_shops, sizeof(ShopList), PC_SHOP_ROWS, 0,
    "The shop number the mode is opened with, starting at 0 (the shops of the item shop mode).",
    "The shop reads its list when it opens, so a change shows the next time you enter a shop.",
    PC_SHOP_FIELDS, (int)(sizeof(PC_SHOP_FIELDS) / sizeof(PC_SHOP_FIELDS[0]))};

/* The mods log for the item shop (T7, T8), once per visit: the shop that opens when a mod changed its stock, and
   each item the shop lists or the bag could sell whose price a mod changed, with the original value. */
static void pcModLogShop(StateScope *scope, void *user) {
    char who[256];
    const ShopList *shop, *orig;
    int shopNo = (int)GAME.funcs.getModeArg(), id, i;

    (void)scope;
    (void)user;
    if (!plogOn("tables", PLOG_MODS) || shopNo < 0 || shopNo >= PC_SHOP_ROWS) {
        return;
    }
    shop = &STITSHOP_shops[shopNo];
    orig = (const ShopList *)tmOriginalRow("shop_stock", (long)shopNo);
    if (tmPatchCount("shop_stock") > 0 && orig != NULL &&
        (orig->count != shop->count || orig->items != shop->items)) {
        tmWho("shop_stock", who, sizeof who);
        plogLine("tables", "shop_stock shop %d: %d items (orig %d) by %s\n", shopNo, (int)shop->count,
                (int)orig->count, who);
    }
    if (tmPatchCount("item_prices") == 0) {
        return;
    }
    tmWho("item_prices", who, sizeof who);
    /* the items content mods add: the record's price now (its declaration's is the default a patch replaced) */
    for (i = 0; i < shop->count && shop->items != NULL; i++) {
        const ItemInfo *ext = shop->items[i] > PC_ITEM_ROWS ? itemExtInfo(shop->items[i]) : NULL;

        if (ext != NULL) {
            plogLine("tables", "item_prices item %s: price %d, sellPrice %d (record now) by %s\n",
                    itemExtNameOf(shop->items[i]), (int)ext->price, (int)ext->sellPrice, who);
        }
    }
    for (id = 1; id <= PC_ITEM_ROWS; id++) {
        const ItemInfo *now = &ITEMS[id - 1], *was = (const ItemInfo *)tmOriginalRow("item_prices", (long)id);
        int sold = 0, selling = PC_ITEM_COUNT(id) > 0;
        char line[200];
        int n;

        if (was == NULL || (now->price == was->price && now->sellPrice == was->sellPrice)) {
            continue;
        }
        for (i = 0; i < shop->count && shop->items != NULL; i++) {
            sold |= shop->items[i] == id;
        }
        if (!sold && !selling) {
            continue;
        }
        n = snprintf(line, sizeof line, "item_prices item %d:", id);
        if (sold && now->price != was->price) {
            n += snprintf(line + n, sizeof line - (size_t)n, " price %d (orig %d),", (int)now->price, (int)was->price);
        }
        if (selling && now->sellPrice != was->sellPrice) {
            n += snprintf(line + n, sizeof line - (size_t)n, " sellPrice %d (orig %d),", (int)now->sellPrice,
                          (int)was->sellPrice);
        }
        if (line[n - 1] == ':') {
            continue; /* the changed field is not one this visit can use */
        }
        line[n - 1] = '\0';
        plogLine("tables", "%s by %s\n", line, who);
    }
}

static void pcApplyShopPatches(StateScope *scope, void *user) {
    tmApply("shop_stock");
    pcModLogShop(scope, user);
}

/* content-export E5b: the evolution lists and the growth tables are the report overlay's data too, so they go on in the same hook
   family, after the scope restored them. The port's extended lists (the mods' evolution entries, ext_state.c) get the patched
   originals in their first 44 entries. */
static void pcApplyReportTables(StateScope *scope, void *user) {
    (void)scope;
    (void)user;
    gameTablesApplyReport(pcEvolutionList);
}

/* content-export E5d: the card shop's prices, stock and packs (STCRDSHP's data) and the card battle's effects, prizes and default deck (CARDGAME's) are
   overlay data too: patched in their scopes' onEnter hooks, after the restore. */
static void pcApplyCardShopTables(StateScope *scope, void *user) {
    (void)scope;
    (void)user;
    gameTablesApplyCardShop();
}

static void pcApplyCardGameTables(StateScope *scope, void *user) {
    (void)scope;
    (void)user;
    gameTablesApplyCardGame();
}

/* content-export E5e: the gym's tables (STGTRAIN's data) are overlay data too: patched in the scope's onEnter hook, after the restore. */
static void pcApplyGymTables(StateScope *scope, void *user) {
    (void)scope;
    (void)user;
    gameTablesApplyGym();
}

static void pcRegisterTables(void) {
    StateScope *gym = stateScopeFind("stgtrain");
    StateScope *report = stateScopeFind("stfgtrep");
    StateScope *cardShop = stateScopeFind("stcrdshp");
    StateScope *cardGame = stateScopeFind("cardgame");
    StateScope *shop = stateScopeFind("stitshop");
    StateScope *field = stateScopeFind("fieldstg");
    pcRegisterLootTable();
    if (field != NULL) {
        stateScopeOnEnter(field, pcApplyEncounterPlacements, NULL); /* also applies the field tables (E5c) */
    }
    /* the field's enemy rows, encounters and battle rates (E5c): registered while the overlay holds the disc's values, patched by the hook above */
    gameTablesSetFighterNames(pcRefDigimonName);
    gameTablesRegisterField();
    /* the card game's tables (E5d): the same, registered while the overlays hold the disc's values */
    gameTablesSetCardNames(pcRefCardName);
    gameTablesSetItemNames(pcRefItemName);
    if (gameTablesRegisterCardShop() > 0 && cardShop != NULL) {
        stateScopeOnEnter(cardShop, pcApplyCardShopTables, NULL);
    }
    if (gameTablesRegisterCardGame() > 0 && cardGame != NULL) {
        stateScopeOnEnter(cardGame, pcApplyCardGameTables, NULL);
    }
    /* the gym's tables and the inns (E5e): the gym's the same way; INNS is the executable's, patched once inside gameTablesRegisterInns */
    gameTablesSetTrainingNames(pcRefTrainingName);
    gameTablesSetInnNames(pcRefInnName);
    if (gameTablesRegisterGym() > 0 && gym != NULL) {
        stateScopeOnEnter(gym, pcApplyGymTables, NULL);
    }
    gameTablesRegisterInns();

    /* the table still holds the disc's rows here: the snapshot of the engine is the original */
    if (tmRegister(&PC_REWARD_TABLE) == 0 && report != NULL) {
        stateScopeOnEnter(report, pcApplyRewardPatches, NULL);
        stateScopeOnExit(report, pcTraceRewardExit, NULL);
    }

    /* ITEMS is the executable's, never reset: the patch goes on once, now */
    if (tmRegister(&PC_ITEM_TABLE) == 0) {
        tmApply("item_prices");
    }
    /* the equipment records and the item effects (game_tables.c) are the executable's too: patched once, now */
    gameTablesRegister();
    if (gameTablesRegisterReport() > 0 && report != NULL) {
        stateScopeOnEnter(report, pcApplyReportTables, NULL);
    }
    /* the shops' data is the overlay's: patched on every entry, after the scope restored it */
    if (tmRegister(&PC_SHOP_TABLE) == 0 && shop != NULL) {
        stateScopeOnEnter(shop, pcApplyShopPatches, NULL);
    }
}


static void pcRegisterStateScopes(void) {
    static int done;
    StateScope *fightstg = NULL, *fieldstg = NULL, *wfightmn, *wfightts;
    size_t i;

    if (done) {
        return;
    }
    done = 1;
    /* The snapshots are taken here, from memory, after the C runtime has initialized the statics and
       relocated the PIE image, and before the first mode runs. */
    for (i = 0; i < sizeof(PC_MODE_SCOPES) / sizeof(PC_MODE_SCOPES[0]); i++) {
        const PcModeScope *m = &PC_MODE_SCOPES[i];
        StateScope *scope = stateScopeCreate(m->name, NULL);

        stateScopeAddRange(scope, m->begin, m->end);
        stateScopeBindMode(m->modeId, scope);
        if (m->modeId == MODE_BATTLE >> 8) {
            fightstg = scope;
        } else if (m->modeId == MODE_NEW_GAME >> 8) {
            fieldstg = scope;
        }
    }
    /* FIGHTSTG's sub-overlays: the battle menu and the battle test, one file each */
    wfightmn = stateScopeCreate("wfightmn", fightstg);
    wfightts = stateScopeCreate("wfightts", fightstg);
    stateScopeAddRange(wfightmn, __ovl_wfightmn_data_start, __ovl_wfightmn_data_end);
    stateScopeAddRange(wfightts, __ovl_wfightts_data_start, __ovl_wfightts_data_end);
    stateScopeBindSub(FILE_WFIGHTMN, wfightmn);
    stateScopeBindSub(FILE_WFIGHTTS, wfightts);
    /* The field's stages (the extra chapter's table runs on into the main one, as in pickStage) */
    pcBindStages(fieldstg, FIELDSTG_extraStages);
    pcBindStages(fieldstg, FIELDSTG_stages);
    pcRegisterTables();
}


/* The name of a mode, for the run's own output */
static const char *pcModeName(s32 mode) {
    switch (mode & 0xFF00) {
    case MODE_COUNTRY_SELECT:
        return "country select";
    case MODE_TITLE:
        return "STDWTITL (title screen / opening movies)";
    case MODE_NEW_GAME & 0xFF00:
        return "FIELDSTG (the field)";
    case MODE_PLAYER_NAME:
        return "STPLNMET (player name entry)";
    case MODE_BATTLE:
        return "FIGHTSTG (battle)";
    case MODE_BATTLE_REPORT:
        return "STFGTREP (battle report)";
    case MODE_CONTINUE:
        return "STGMCARD (memory card: load/save)";
    case MODE_TRAINING:
        return "STGTRAIN (gym)";
    case MODE_NAMING:
        return "STDGNAME (partner name entry)";
    case MODE_DIGI_LAB:
        return "STGDGLAB (Digimon Lab)";
    case MODE_STATUS:
        return "STSTATUS (field menu screens)";
    default:
        return "unknown";
    }
}

/* The entry point of a mode, or a clear stop when this build does not have it */
static Task *pcModeEntryPoint(s32 mode) {
    s32 index = (mode >> 8) & 0xFF;

    if (index < 23 && PC_MODE_ENTRY_POINTS[index] != NULL) {
        return PC_MODE_ENTRY_POINTS[index]();
    }
    fprintf(stderr, "boot: mode 0x%X has no entry point in this host build; only the\n"
                    "      country select (0x%X), the field (0x%X), the title overlay\n"
                    "      (0x%X), the player name entry (0x%X) and the battle mode\n"
                    "      (0x%X) are compiled in so far\n",
            (int)mode, (int)MODE_COUNTRY_SELECT, (int)(MODE_NEW_GAME & 0xFF00),
            (int)MODE_TITLE, (int)MODE_PLAYER_NAME, (int)MODE_BATTLE);
    exit(1);
}

/*
 * The mode task: the same three states the game's overlay.c runs, with the
 * port's own dispatch in place of the overlay copy. main calls the port's
 * createModeTask (below), which creates this task, and its child is the
 * mode's top task.
 */
static void pcUpdateModeTask(Task *task, Task **children) {
    switch (task->state) {
    case TASK_INIT:
    default:
        printf("boot: mode 0x%X (%s)\n", (int)GAME.funcs.getMode(), pcModeName(GAME.funcs.getMode()));
        /* overlay.c's loadModeOverlay: the mode's statics reset when mode >> 8 changes */
        stateScopeModeEntered(GAME.funcs.getMode());
        children[0] = pcModeEntryPoint(GAME.funcs.getMode());
        task->nextState(task);
        break;
    case TASK_RUN:
        if (GAME.funcs.isModeChangePending() != 0) {
            task->setState(task, TASK_KILL);
        }
        break;
    case TASK_DONE:
    case TASK_KILL:
        break;
    }
}

/* The task that runs the current mode (main's `ROOT_TASK = createModeTask()`).
   Its child slot is sizeof(Task *) and not the 4 bytes the PlayStation's
   overlay.c uses: on this host a pointer is 8 bytes, and a 4-byte slot would
   be overrun by the child pointer pcUpdateModeTask writes into it. (The
   game's own mode code sizes its child arrays with sizeof, so it is right on
   both; only the executable's hardcoded 4 is a PS1 assumption.) */
void *createModeTask(void) {
    void *task;

    pcRegisterStateScopes();
    task = createTask(pcUpdateModeTask, sizeof(Task), sizeof(Task *));

    /* T17: the input demo walks down from here to the country select's menu
       task to read the option it highlights (shim/host_pad.c). */
    pcBootPadRootTask(task);
    return task;
}

/* The frame count of the command line, without <stdlib.h> */
static s32 parseFrames(const char *text) {
    s32 value = 0;

    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    return value;
}

/*
 * T30: parse the optional 7th argument, a comma-separated list of zero-based
 * frame indices for the multi-frame capture. Each entry is a non-empty run
 * of ASCII digits ('0'..'9'); entries are separated by single commas. An
 * empty list, a trailing comma, repeated commas, a non-ASCII/non-digit byte,
 * an entry that overflows INT_MAX, or more than `maxOut` entries exits 2
 * before the run starts so the harness never silently drops a capture.
 * Returns the number of indices parsed (>= 1) on success; on any failure
 * the function calls exit(2) and never returns to the caller.
 */
static int parseCaptureList(const char *text, int *outIndices, int maxOut) {
    int count = 0;
    const char *at = text;

    /* Empty string: a present-but-empty capture list is not a request. */
    if (*at == '\0') {
        fprintf(stderr, "boot: capture list is empty\n");
        exit(2);
    }
    while (*at != '\0') {
        int value = 0;
        const char *start = at;

        /* Reject a leading comma: an entry must start with a digit. */
        if (*at < '0' || *at > '9') {
            fprintf(stderr,
                    "boot: malformed capture list at %s (expected digits)\n",
                    start);
            exit(2);
        }
        /* Accumulate digits; reject any non-digit byte before the next ','.
           INT_MAX is 2147483647 (10 digits); the overflow check fires at
           the 11th digit or when value would cross the limit. */
        while (*at >= '0' && *at <= '9') {
            int digit = *at - '0';

            if (value > INT_MAX / 10
                    || (value == INT_MAX / 10 && digit > INT_MAX % 10)) {
                fprintf(stderr,
                        "boot: capture index overflows INT_MAX at %s\n",
                        start);
                exit(2);
            }
            value = value * 10 + digit;
            at++;
        }
        if (count >= maxOut) {
            fprintf(stderr,
                    "boot: capture list has more than %d entries\n", maxOut);
            exit(2);
        }
        outIndices[count++] = value;
        /* The separator must be ',' (continue) or '\0' (end). A trailing
           ',' leaves us pointing at '\0' after the increment, so check
           here too: the run reached the end without a digit. */
        if (*at == ',') {
            at++;
            /* Reject a second comma right away: repeated commas read as
               an empty entry between them. */
            if (*at == ',') {
                fprintf(stderr,
                        "boot: malformed capture list at %s (repeated ',')\n",
                        at);
                exit(2);
            }
            if (*at == '\0') {
                fprintf(stderr,
                        "boot: malformed capture list at end (trailing ',')\n");
                exit(2);
            }
        } else if (*at != '\0') {
            fprintf(stderr,
                    "boot: malformed capture list at %s (expected ',')\n",
                    at);
            exit(2);
        }
    }
    return count;
}

/* T30 checks for out-of-run, negative and duplicate indices in the parsed
   list. The duplicates check is O(n^2) but n is bounded (PC_BOOT_MAX_CAPTURES). */
static void validateCaptureList(const int *indices, int count, s32 frames) {
    int i;
    int k;

    for (i = 0; i < count; i++) {
        if (indices[i] < 0) {
            fprintf(stderr, "boot: capture index %d is negative\n", indices[i]);
            exit(2);
        }
        if (frames > 0 && indices[i] >= frames) {
            fprintf(stderr,
                    "boot: capture index %d is out of run (frames budget is %d, "
                    "valid indices are 0..%d)\n", indices[i], (int)frames,
                    (int)frames - 1);
            exit(2);
        }
        for (k = i + 1; k < count; k++) {
            if (indices[i] == indices[k]) {
                fprintf(stderr, "boot: duplicate capture index %d\n", indices[i]);
                exit(2);
            }
        }
    }
}

/*
 * The country-select art diagnostic (T16): the VRAM regions the mode's own code
 * fills, from country_select.c's TASK_INIT -- `setImagePos(0x280, 0)` and
 * `setClutPos(0, 0x1F0)` for the one RLEN-packed 8-bit TIM of file 0x8A3
 * (AAA/DAT/COUNTRY/CNTSELWN.BIN), whose image is 64 halfwords by 256 rows and
 * whose CLUT is a 256-entry row. With the frame given, gfx_engine reads them
 * back and prints them, and prints the frame's sprites and DR_TPAGEs;
 * pc/check_country.py compares all of it against an independent read of that
 * file on the disc. Frame 40 is the one the check arms: the panels are open by
 * then and their art is up (country_select.c's menu substates).
 */
static const PcArtRegion CNTY_SEL_ART_REGIONS[] __attribute__((unused)) = {
    {0x280, 0, 0x40, 0x100},
    {0, 0x1F0, 256, 1},
};

/* The most regions one run arms; pcBootArtInit caps further at its own */
#define ART_DUMP_MAX_REGIONS 8

/* The fallback when the regions argument lists fewer entries: alignments and
   the field's own pages (the diagnostic still prints the armed regions) */
static const PcArtRegion DEFAULT_ART_REGIONS[ART_DUMP_MAX_REGIONS] = {
    {0x280, 0, 0x40, 0x100},
    {0, 0x1F0, 256, 1},
    {0x140, 0, 0x40, 0x100},
    {0x180, 0x100, 0x40, 0x180},
};

/*
 * Parses "x,y,w,h;x,y,w,h" into regions: halfword VRAM rects for pcBootArtInit.
 * Returns the count; a malformed spec keeps the count at 0 (the caller falls
 * back to the country-select regions, so old runs are unchanged).
 */
/* stdlib.h conflicts with the game's own stdlib declarations, so this host
   file declares the one function it uses (the same pattern as getenv above) */
extern long strtol(const char *nptr, char **endptr, int base);
static int parseArtRegions(const char *spec, PcArtRegion *regions, int max) {
    int count = 0;
    const char *scan = spec;

    while (*scan != '\0' && count < max) {
        char *end;
        long x;
        long y;
        long w;
        long h;

        x = strtol(scan, &end, 0);
        if (end == scan || *end != ',') {
            return count;
        }
        scan = end + 1;
        y = strtol(scan, &end, 0);
        if (end == scan || *end != ',') {
            return count;
        }
        scan = end + 1;
        w = strtol(scan, &end, 0);
        if (end == scan || *end != ',') {
            return count;
        }
        scan = end + 1;
        h = strtol(scan, &end, 0);
        if (end == scan || (*end != ';' && *end != '\0')) {
            return count;
        }
        regions[count].x = (s16)x;
        regions[count].y = (s16)y;
        regions[count].w = (s16)w;
        regions[count].h = (s16)h;
        count++;
        if (*end == '\0') {
            break;
        }
        scan = end + 1;
    }
    return count;
}

/*
 * T41: the window PsyCross opens. The game's own display modes are 320x240
 * (field, menus) and 320x480 (the movies). PsyCross scales whatever the game
 * draws to the window -- its ortho projection comes from the active DISPENV
 * -- so a 640x480 window shows a 480-line display with the 2x width the
 * PS1's interlaced mode means, instead of squeezing those 480 lines into 240.
 * The default is the game's own 320x240, so every capture-based check keeps
 * its bytes. DW3_WINDOW="<width>x<height>" overrides it (pc/README.md).
 */
static void pcWindowSize(int *width, int *height) {
    const char *env = getenv("DW3_WINDOW");
    int w = 0;
    int h = 0;
    int used = 0;

    *width = SCREEN_WIDTH;
    *height = SCREEN_HEIGHT;
    if (env == NULL || env[0] == '\0') {
        return;
    }
    if (sscanf(env, "%dx%d%n", &w, &h, &used) != 2 || env[used] != '\0'
            || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        fprintf(stderr,
                "boot: ignoring DW3_WINDOW=%s (expected WIDTHxHEIGHT, "
                "for example 640x480)\n", env);
        return;
    }
    *width = w;
    *height = h;
}

/*
 * A fault's own report: on SIGSEGV/SIGFPE/SIGBUS/SIGILL the run prints the
 * signal and the native backtrace (function names via -rdynamic, source lines
 * with addr2line on the -g build) to stderr before dying, so an owner's crash
 * in a long interactive run names its function without gdb. unistd.h's
 * write clashes with the PsyQ headers' own, so the header line goes through
 * stdio; backtrace_symbols_fd writes straight to fd 2.
 */
#if defined(__ANDROID__)
/* No backtrace() on Bionic below API 33: the report walks the stack with the unwinder (_Unwind_Backtrace) and names each frame with
   dladdr as lib+offset (llvm-addr2line -f -e <unstripped libmain.so> <offset> for the line) and the fault address, PC, LR and SP
   from the ucontext. It goes to logcat directly: stderr's pipe reader (plogTeeToLogcat) may not run before the process dies. The
   handler runs on its own stack (sigaltstack, SA_ONSTACK): a stack overflow has no stack left to report on. */
typedef struct { int n; } PcUnwindState;

static _Unwind_Reason_Code pcUnwindStep(struct _Unwind_Context *ctx, void *arg) {
    PcUnwindState *st = (PcUnwindState *)arg;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
    Dl_info info;

    if (pc == 0 || st->n >= 40) {
        return _URC_END_OF_STACK;
    }
    if (dladdr((void *)pc, &info) && info.dli_fname != NULL) {
        const char *base = info.dli_fname;
        const char *q;

        for (q = info.dli_fname; *q != '\0'; q++) {
            if (*q == '/') {
                base = q;
            }
        }
        if (*base == '/') {
            base++;
        }

        __android_log_print(ANDROID_LOG_FATAL, "dw3", "  #%02d pc %016lx %s+0x%lx %s", st->n, (unsigned long)pc,
                            base, (unsigned long)(pc - (uintptr_t)info.dli_fbase),
                            info.dli_sname != NULL ? info.dli_sname : "");
    } else {
        __android_log_print(ANDROID_LOG_FATAL, "dw3", "  #%02d pc %016lx", st->n, (unsigned long)pc);
    }
    st->n++;
    return _URC_NO_REASON;
}

static void pcFaultReport(int sig, siginfo_t *si, void *uctx) {
    PcUnwindState st = {0};
    ucontext_t *uc = (ucontext_t *)uctx;

    fflush(stdout);
    __android_log_print(ANDROID_LOG_FATAL, "dw3", "boot: fatal signal %d code %d fault addr %p", sig, si->si_code, si->si_addr);
#if defined(__aarch64__)
    __android_log_print(ANDROID_LOG_FATAL, "dw3", "boot: pc %016llx lr %016llx sp %016llx",
                        (unsigned long long)uc->uc_mcontext.pc, (unsigned long long)uc->uc_mcontext.regs[30],
                        (unsigned long long)uc->uc_mcontext.sp);
#else
    (void)uc;
#endif
    _Unwind_Backtrace(pcUnwindStep, &st);
    signal(sig, SIG_DFL);
    raise(sig);
}
#elif defined(_WIN32)
/* No backtrace() on Windows (MinGW has no execinfo.h): the handler only names the signal. */
static void pcFaultReport(int sig) {
    fflush(stdout);
    fprintf(stderr, "\nboot: fatal signal %d\n", sig);
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}
#else
static void pcFaultReport(int sig) {
    static const char head[] = "\nboot: fatal signal, backtrace follows "
                               "(addr2line -e pc/build/boot <addr> for lines)\n";
    void *frames[48];
    int count;

    fflush(stdout);
    fputs(head, stderr);
    fflush(stderr);
    count = backtrace(frames, 48);
    backtrace_symbols_fd(frames, count, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

static void pcInstallFaultReport(void) {
#ifdef _WIN32
    static const int sigs[] = { SIGSEGV, SIGFPE, SIGILL };
#else
    static const int sigs[] = { SIGSEGV, SIGFPE, SIGBUS, SIGILL };
#endif
    unsigned i;

#ifdef __ANDROID__
    static char altStack[64 * 1024];
    stack_t ss;
    struct sigaction sa = {0};

    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL); /* per thread: the one that runs the game (SDL's thread) installs it here */
    sa.sa_sigaction = pcFaultReport;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        sigaction(sigs[i], &sa, NULL);
    }
#else
    for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        signal(sigs[i], pcFaultReport);
    }
#endif
}

/* W2: shim/disc_picker.c (declared here: its header pulls in time.h and stdint.h next to the game's) */
extern int dpPlayerStart(char *bin, int cap);
extern int dpToolDisc(const char *arg, char *bin, int cap);

/* U5: --validate-mod / --validate-mods. The game side of the validator (shim/mod_tools.c): the real loaders over every installed mod,
   without a window and without running the game. The content, the config and the mods' own scan already ran in main's init block
   (their lines are in the capture); here the data tables register and apply (the overlay tables hold the disc's values in the
   executable, as they do when the game registers them), the images, audio, video and stage PNGs go through their loaders. */
static int pcValidateDiscHas(void *ud, const char *discPath) {
    (void)ud;
    return pfsFindPath(discPath) != NULL;
}

static void pcValidateRun(void *ud) {
    const char *dataDir = (const char *)ud;
    size_t len = 0;
    unsigned char *file;
    int i;

    pcRegisterStateScopes(); /* registers every table and reads the mods' patches (the row and field rejections are logged there) */
    for (i = 0; i < tmTableCount(); i++) {
        tmApply(tmTableName(i)); /* the apply-time checks (row checks, lists); the file-backed tables answer -1 and are patched below */
    }
    file = pcCdReadDiscFile(FILE_BATTLE_TABLE, &len);
    if (file != NULL) {
        tmPatchBuffer("battle_loot", file, len);
        free(file);
    }
    file = pcCdReadDiscFile(FILE_CARDGAME_OPPONENTS, &len);
    if (file != NULL) {
        gameTablesPatchCardOpponents(file, len); /* the row checks of card_opponents */
        free(file);
    }
    imgValidateMods();
    for (i = 0; i < modsCount(); i++) {
        audioModValidate(modsDirOf(i), modsNameOf(i));
        videoModValidate(modsDirOf(i), modsNameOf(i));
        stageModValidate(modsDirOf(i), modsNameOf(i), dataDir);
    }
}

static int pcValidateMods(const char *target, const char *dataDir) {
    ModToolsLayers layers;

    memset(&layers, 0, sizeof layers);
    layers.run = pcValidateRun;
    layers.discHas = pcValidateDiscHas;
    layers.covered = "config,content,card,tables,images,audio,video,stages";
    layers.ud = (void *)dataDir;
    fflush(stdout);
    return modToolsValidate(target, &layers, stdout);
}

/* U5: --list-mods and --new-mod need no disc: they run before anything else and exit */
static int pcRunModTool(const ModToolsCmd *c) {
    char modsDir[1024];
    char *order;

    if (!portPath(PP_MODS, modsDir, (int)sizeof(modsDir))) {
        fprintf(stderr, "boot: there is no mods folder (the preferences folder could not be made)\n");
        return 1;
    }
    if (c->kind == MT_NEW) {
        return modToolsNew(modsDir, c->name, c->kinds, stdout, stderr);
    }
    modToolsCaptureBegin(); /* the scan's log lines are not the listing */
    order = inputCfgReadModsOrderAlloc();
    modsInit(modsDir, order);
    free(order);
    modToolsCaptureEnd();
    return modToolsList(modsDir, stdout);
}

int main(int argc, char **argv) {
    s32 frames = DEFAULT_FRAMES;
    char playerDisc[1024];
    char *playerArgv[2];
    int playerStart = 0;
    s32 artFrame = 0;
    s32 withShot = 0;
    const char *keys = NULL;
    int width = 0;
    int height = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    int captureIndices[PC_BOOT_MAX_CAPTURES];
    int captureCount = 0;
    int exportAll;
    ModToolsCmd tool;
    int haveTool;
    char toolRoot[1024], toolName[300];
    const char *validateName = NULL;


#ifdef __ANDROID__
    androidPermsUmask(); /* what the app creates in Android/data/<id>/files must be group-writable for a PC's adb push */
#endif
    {
        char trace[PP_CAP];

        if (portPathIn(PP_LOGS, "restart-trace.txt", trace, sizeof(trace))) {
            optionsSystemSetTraceFile(trace); /* the restart's steps, also before this start has a log */
        }
    }
    optionsSystemTakeParent(&argc, argv); /* a restarted start waits for the old process before it opens anything */
    optionsSystemSetArgs(argc, argv); /* the restart repeats the command line as it came */
    {
        /* the System tab's "Import memory card" row (M2 / R4); before the tab's rows are added */
        static const OptSysCardImport cardImport = {hostMemcardImportNow, hostMemcardDropDir};

        optionsSystemSetCardImport(&cardImport);
    }
    exportAll = inputCfgTakeFlag(&argc, argv, "--export-all"); /* U4: export the heavy files and exit; the rest of the command line is as before */
    toolRoot[0] = '\0';
    haveTool = modToolsTakeArgs(&argc, argv, &tool); /* U5: --list-mods, --new-mod, --validate-mod(s): no game */
    portPathsEnsureTree(); /* the user folder's tree (shim/port_paths.h): saves, import, mods, data, logs */
#ifdef __ANDROID__
    if (argc > 1) {
        char harnessLog[PP_CAP];

        /* a scenario launched with `am start --esa args ...` has no player-start log (shim/disc_picker.c opens it only for a bare launch):
           stdout and stderr go to logcat (tag dw3) and to logs/harness.log */
        if (portPathIn(PP_LOGS, "harness.log", harnessLog, sizeof(harnessLog))) {
            plogTeeToLogcat(harnessLog);
        }
    }
    if (portPathsBase() != NULL) {
        int n = androidPermsFix(portPathsBase(), ANDROID_PERMS_DIR_MODE, ANDROID_PERMS_FILE_MODE); /* folders an older start made as rwxr-s--- */

        fprintf(stderr, "perms: %d entries of the app folder made group-writable\n", n);
    }
#endif
    if (haveTool) {
        if (tool.err[0]) {
            fprintf(stderr, "boot: %s (flags: --list-mods, --new-mod <name> [--kind <kind>], --validate-mod <name|path>, --validate-mods)\n", tool.err);
            return 2;
        }
        if (tool.kind == MT_LIST || tool.kind == MT_NEW) {
            return pcRunModTool(&tool);
        }
        /* the disc as the player gives it: a .bin or a .cue, or none for the one settings.ini remembers */
        if (!dpToolDisc(argc >= 2 ? argv[1] : NULL, playerDisc, (int)sizeof(playerDisc))) {
            fprintf(stderr, "boot: --validate-mod needs the disc image: boot <disc>.bin|.cue --validate-mod <name|path>, or a disc the game remembers\n");
            return 2;
        }
        if (argc < 2) {
            playerArgv[0] = argv[0];
            argv = playerArgv;
            argc = 2;
        }
        argv[1] = playerDisc;
        if (tool.kind == MT_VALIDATE) {
            toolRoot[0] = '\0';
            modToolsSplitTarget(tool.name, toolRoot, sizeof(toolRoot), toolName, sizeof(toolName));
            validateName = toolName;
        }
        modToolsCaptureBegin(); /* from here on the loaders' log lines are the validator's input */
    }
    pcInstallFaultReport();
    pcItemGuardInstall();
    PcArtRegion artRegions[ART_DUMP_MAX_REGIONS];
    int artRegionCount = 0;
    const char *artSpec = NULL;

    if (argc == 1) {
        /* W2: a player's start (a double click): ask for the disc, log to a file, run until the window closes */
        if (!dpPlayerStart(playerDisc, (int)sizeof(playerDisc))) {
            return 1;
        }
        optionsSystemRestartNote();
        playerArgv[0] = argv[0];
        playerArgv[1] = playerDisc;
        argv = playerArgv;
        argc = 2;
        frames = 2000000000;
        playerStart = 1;
    }
    if (argc < 2 || argc > 8) {
        fprintf(stderr,
                "usage: %s <disc.bin> [frames] [screenshot.png] [art-frame] [keys] "
                "[capture-list] [art-regions]\n"
                "      <frames>: the number of presented frames; 0 keeps the default 300\n"
                "      <keys>: the scripted pad's tokens (pc/shim/host_pad.c): key names,\n"
                "              barriers ('wait', 'field', ...), 'battle@<frame>:<keys>',\n"
                "              which fires the real battle (the bare 'battle' token's\n"
                "              lane) and presses <keys> -- a key name each,\n"
                "              ','-separated with an optional hold-frame count after\n"
                "              one -- <frame> frames after the fire,\n"
                "              'warp:<stage>[,<progress>]', which jumps the first frame\n"
                "              straight into a field stage (no menu play)\n"
                "      <capture-list> (T30): comma-separated zero-based frame indices, "
                "one PNG per index\n"
                "      <art-regions>: \"x,y,w,h;...\" halfword VRAM rects for the art "
                "dump; default is the country-select layout\n",
                argv[0]);
        return 2;
    }
    if (argc > 2) {
        frames = parseFrames(argv[2]);
        if (frames <= 0) {
            frames = DEFAULT_FRAMES;
        }
    }
    withShot = argc > 3;
    if (argc > 4) {
        artFrame = parseFrames(argv[4]);
    }
    if (argc > 5) {
        keys = argv[5];
    }
    if (argc > 6) {
        /* T30: the multi-frame capture list. The harness needs a non-empty
           screenshot path to derive per-index file names from; an empty
           path would leave the multi-capture list with no stem. The C
           list and bounds validation runs here, before pcCdReaderInit,
           so the run never starts on a list that cannot be honored. */
        if (argv[3][0] == '\0') {
            fprintf(stderr,
                    "boot: capture list given without a screenshot path\n");
            return 2;
        }
        captureCount = parseCaptureList(argv[6], captureIndices,
                                        PC_BOOT_MAX_CAPTURES);
        validateCaptureList(captureIndices, captureCount, frames);
    }

    if (argc > 7) {
        artSpec = argv[7];
    }

    if (playerStart) {
        extern int pcCdReaderOpen(const char *image);

        if (pcCdReaderOpen(argv[1]) != 0) {
            fprintf(stderr, "boot: the disc image could not be opened\n");
            return 1;
        }
    } else {
        pcCdReaderInit(argv[1]);
    }
    {
        /* L2b: the text data folder and the es-419 overlay folder (see shim/text_data.h) */
        char dataDir[1024], l10nDir[1024], modsDir[1024];
        char *modsOrder;
        int haveData = portPath(PP_DATA, dataDir, (int)sizeof(dataDir));

        modsDir[0] = '\0';

        /* ADR-005: the mods (folder and [mods] order of settings.ini), then the config files they
           can change; the text layer below reads the same mod list */
        if (haveTool && toolRoot[0] != '\0') {
            strcpy(modsDir, toolRoot); /* --validate-mod <path>: the folder that holds it */
        } else {
            portPath(PP_MODS, modsDir, (int)sizeof(modsDir));
        }
        modsOrder = haveTool ? NULL : inputCfgReadModsOrderAlloc(); /* a validation looks at every mod */
        modsInit(modsDir[0] != '\0' ? modsDir : NULL, modsOrder);
        free(modsOrder);
        pcConfigLoad(haveData ? dataDir : NULL);
        tmInit(haveData ? dataDir : NULL); /* the data tables register later, with the state scopes */
        pcInstallItems(); /* the new items of the content mods: ids, ITEM_FUNCS, names; before the shop table reads its patches */
#ifdef __ANDROID__
        {
            int n = l10nInstallAssets(); /* the APK's es-419 overlay into the user folder, before the text layer looks for it */

            if (n > 0) {
                fprintf(stderr, "l10n: installed %d overlay files from the app into %slocalization\n", n, portPathsBase());
            } else if (n < 0) {
                fprintf(stderr, "l10n: the overlay could not be installed from the app\n");
            }
        }
#endif
        pcCdTextInit(haveData ? dataDir : NULL,
                     portPathsL10n(l10nDir, (int)sizeof(l10nDir)) ? l10nDir : NULL);
        pcInstallCards();
        pcInstallModelOriginals(); /* before the Digimon load below bakes a model that says "from" */
        pcInstallTechs();
        pcExportContentReference();
        pcInstallModelExport(haveData ? dataDir : NULL);
        pcInstallVideoExport(haveData ? dataDir : NULL);
        pcInstallAudioExport(haveData ? dataDir : NULL);
        pcInstallStages(haveData ? dataDir : NULL);
        gameTablesRegisterEvents(); /* S3: the condition tables (the executable's data, patched once) and the mods' own special conditions, which the patches below name */
        pcInstallStageMods();
        pcExportStageData(haveData ? dataDir : NULL);
        pcInstallRaw(haveData ? dataDir : NULL);
        pcInstallVideoMod();
        pcInstallAudioMod();
        if (haveTool) {
            return pcValidateMods(validateName, haveData ? dataDir : NULL);
        }
        if (exportAll) {
            /* U4: no window; the exporters' worker threads and the progress lines are in shim/options_system.c */
            return haveData ? optionsSystemExportAllRun() : 1;
        }
    }
    atexit(pcBootExitNote);
    optionsSystemRegisterExit(); /* before PsyCross: its handlers (audio, window) run before the restart */

    /* PsyCross's window and renderer, which the game's main() (ResetGraph on
       the PS1) does not do itself. T41: the size comes from DW3_WINDOW, with
       the game's own 320x240 as the default. */
    pcWindowSize(&windowWidth, &windowHeight);
#ifdef __ANDROID__
    extern int setenv(const char *name, const char *value, int overwrite); /* stdlib.h clashes with the game's headers here */
    /* the game is landscape: without the hint SDL picks the activity's orientation from the window's shape and, on a phone held
       upright, the fullscreen window came out portrait (1220x2712 on the Poco X7) */
    setenv("SDL_IOS_ORIENTATIONS", "LandscapeLeft LandscapeRight", 1); /* SDL_HINT_ORIENTATIONS (this unit does not include SDL.h) */
#endif
    PsyX_Initialise("dw3_pc_spike", windowWidth, windowHeight, 0);
    PsyX_GetScreenSize(&width, &height);
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "boot: PsyX_Initialise did not open a window "
                        "(no display or no OpenGL context)\n");
        return 1;
    }

#ifdef __ANDROID__
    appLifecycleInit(); /* the app in the background: clock and audio frozen, settings saved; back: resume without catch-up */
#endif
    pcBootPresentInit(frames, withShot ? argv[3] : NULL);
    if (captureCount > 0) {
        /* T30: arm the multi-frame capture. Each entry produces one PNG under
           `<stem>_f<index>.png` from a single run; the single-shot capture
           at the run's last frame still happens via pcBootPresentInit. */
        pcBootMultiCaptureInit(captureIndices, captureCount);
    }
    if (artFrame > 0) {
        artRegionCount = 0;
        if (artSpec != NULL) {
            artRegionCount = parseArtRegions(artSpec, artRegions,
                                             ART_DUMP_MAX_REGIONS);
            if (artRegionCount == 0) {
                fprintf(stderr, "boot: malformed art regions %s, using the "
                                "defaults\n", artSpec);
            }
        }
        if (artRegionCount == 0) {
            artRegionCount = (int)(sizeof(DEFAULT_ART_REGIONS) /
                                   sizeof(DEFAULT_ART_REGIONS[0]));
            for (int i = 0; i < artRegionCount; i++) {
                artRegions[i] = DEFAULT_ART_REGIONS[i];
            }
        }
        pcBootArtInit(artFrame, artRegions, artRegionCount);
    }
    /* T17: arm the scripted key sequence (NULL: the keyboard path is wired
       anyway and the owner presses keys himself). After PsyX_Initialise, so
       the window the keys are aimed at exists. */
    pcBootPadInit(keys);

    printf("boot: the game's own main(), %dx%d window, %d frames\n", width, height, (int)frames);
    if (withShot) {
        printf("boot: capture goes to %s\n", argv[3]);
        if (captureCount > 0) {
            printf("boot: T30 multi-capture of %d frame(s)\n", captureCount);
        }
    }

    return pcGameMain();
}
