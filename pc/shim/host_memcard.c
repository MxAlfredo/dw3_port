/*
 * Host memory card (menu-modes M2, rebuilt by extended-state T2 for ADR-008 option B): the libmcrd calls
 * the game's memcard layer (src/engine/memcard/memcard.c) makes, served from the port's own save files.
 *
 * PsyCross's own LIBMCRD.C is a stub for this game's purposes (ReadFile, WriteFile, CreateFile and
 * Format do nothing, it reads "0.MCD" from the working directory), so these definitions replace it:
 * they are the only ones in the link (every symbol of its header is defined here, so the archive
 * member is never pulled in).
 *
 * The game's own screens (STGMCARD) are unchanged: they still see a card in port 0 holding ONE file,
 * "BESLES-03936DMW3-EUR" (the name varies with the language: the game asks by name and any name with
 * "DMW3" in it is this file), of 4 blocks laid out as the game writes it:
 *
 *     header sector + icon frames   (iconCount + 1 sectors; the count is in header byte 2)
 *     info section   0x100 bytes    (MemCardFile: the 3 slots' summaries, the slot last saved)
 *     data section x3, 0x2700 each  (GAME's first 0x26C4 bytes = GameSave, checksum/version inside)
 *
 * Behind it there is no card image. Each of the three slots is a dw3save-v1 file (shim/save_file.h):
 *
 *     <saves>/card.dw3card     the file's header+icons ("HDRS"), its name ("NAME") and the info section the
 *                              game wrote when it created the file ("INFO"); its existence IS the save
 *                              file's existence on the virtual card
 *     <saves>/slot<N>.dw3save  sections GAME (the 0x26C4 original block, untouched), INFO (the info section
 *                              as the game wrote it when this slot was saved, a full copy), META (slot,
 *                              sequence number, game size), EXTI (the port's extension, by name:
 *                              shim/ext_state.c), MODS (reserved: the mods the save used)
 *     <saves>/imported.txt     the one-way import ran (see below)
 *
 * The info section the game reads is the INFO of the slot saved last (highest sequence number), else the
 * one of card.dw3card: every info write carries all three summaries, so the newest copy is the truth and
 * this file needs no knowledge of its layout. A slot is committed when the game's last data sector
 * arrives (the info write before it is only staged), atomically (tmp + rename, a .bak kept), so an
 * interrupted save leaves the previous slot file. A damaged slot file falls back to its .bak, else the
 * slot reads as empty; both are logged on [saves].
 *
 * Where: the saves folder is saves/<profile> of the user folder (shim/port_paths.h; $DW3_SAVES_DIR overrides it);
 * the readable view of each slot is saves/<profile>/view/slot<N>.json.
 *
 * Import (one way): on the first start, when the saves folder has no card.dw3card and no slot, and import/ holds no
 * imported.txt, the PS1 card images in import/ (every *.mcd, by name without regard to case; $DW3_MEMCARD names a single
 * card instead, for the harnesses) are tried in order until one gives saves: its DW3 file becomes the three slots (empty
 * slots are skipped, a slot whose data fails the game's own checksum is skipped and logged) and imported.txt is written
 * in import/ (next to the card for $DW3_MEMCARD). The card is only read: it stays exactly as it was. To import again,
 * delete imported.txt and the saves folder.
 *
 * The card is port 0 only (port 1 answers "no card").
 *
 * Image layout of the import source (the standard .mcd one, so emulators read it too): 16 blocks of
 * 8 KB, 128-byte frames. Block 0 is the header: frame 0 "MC", frames 1-15 the directory (state 0xA0 free,
 * 0x51/0x52/0x53 first/middle/last block of a file, size at +4, next block - 1 at +8 (0xFFFF none), name at
 * +10, XOR checksum at +127). Block n (1-15) is directory frame n's data.
 *
 * Every command is synchronous: the call itself does the work and leaves its answer for MemCardSync,
 * which then reports it as finished. Retail semantics kept: Accept answers McErrNewCard once after the
 * library starts (the game's STGMCARD_acceptCard treats it as a ready card), a missing save file answers
 * McErrFileNotExist (the game offers to create it), CreateFile/GetDirentry/Format return the error code
 * directly (0 = done). The virtual card is always formatted; Format changes nothing (it never erases
 * saves).
 *
 * MemCardSync writes each result at the width of the game's variable, not the prototype's int *: the
 * game's only caller (syncMemCard) passes a `long` cmds (64 bits on Linux, 32 on Windows) and a `u_long`
 * result, which is pointer-sized on both hosts (glibc's unsigned long; pc/win/bsd_types.h's unsigned long
 * long). Writing less leaves the uninitialised stack in the high half, which the game reads as an error
 * and retries forever (the Windows load/save that stalled at the end of the bar); writing more overruns it.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL.h>

#include "card_image.h"
#include "host_memcard.h"
#include "ext_state.h"
#include "port_log.h"
#include "save_file.h"
#include "save_view.h"

#include "port_paths.h"

#define CARD_SIZE 0x20000
#define BLOCK_SIZE 0x2000
#define FRAME_SIZE 128
#define DIR_FIRST 1
#define DIR_LAST 15
#define NAME_LEN 20

/* the game's save file (memcard.c initMemCard, memory_card.h): sector, info and data sizes, slot count */
#define SECTOR 128
#define INFO_SIZE 0x100
#define DATA_SIZE 0x2700
#define GAME_SIZE 0x26C4
#define SLOTS 3
#define FILE_BLOCKS 4
#define HDR_MAX (4 * SECTOR) /* header + up to 3 icon frames */
#define DEFAULT_NAME "BESLES-03936DMW3-EUR"

#define ERR_NONE 0
#define ERR_NO_CARD 1
#define ERR_INVALID 2
#define ERR_NEW_CARD 3
#define ERR_NOT_FORMAT 4
#define ERR_NO_FILE 5
#define ERR_EXISTS 6
#define ERR_FULL 7

#define FN_EXIST 1
#define FN_ACCEPT 2
#define FN_READ_FILE 3
#define FN_WRITE_FILE 4

#define ST_FREE 0xA0
#define ST_FIRST 0x51
#define ST_MIDDLE 0x52
#define ST_LAST 0x53

typedef struct HostDirEntry {
    char name[20];
    int attr;
    int size;
    struct HostDirEntry *next;
    int head;
    char system[4];
} HostDirEntry;

typedef struct Slot {
    int valid;
    uint32_t seq;
    unsigned char game[DATA_SIZE];
    unsigned char info[INFO_SIZE];
    unsigned char *ext; /* the slot's EXTI chunk, NULL when it has none */
    unsigned long extLen;
    unsigned char *techs; /* the slot's EXTT chunk (new technique names), NULL when it has none */
    unsigned long techsLen;
    unsigned char *digs; /* the slot's EXTD chunk (names of the new partner forms), NULL when it has none */
    unsigned long digsLen;
    unsigned char *ents; /* the slot's EXTP chunk (partner entries beyond the original 44), NULL when it has none */
    unsigned long entsLen;
    unsigned char *cards; /* the slot's EXTK chunk (cards content mods add: counts, seen flags and the names of the ids in the decks), NULL when it has none */
    unsigned long cardsLen;
    unsigned char *vars; /* the slot's EXTV chunk (the named variables of content mods: stage flags and counters), NULL when it has none */
    unsigned long varsLen;
    int techsDone; /* game[] already holds this run's technique and Digimon ids (saved by this run, or remapped on the first read) */
} Slot;

static unsigned char card[CARD_SIZE]; /* the import source only */
static int started;
static int newCardPending = 1;
static int pending;
static int pendingCmd;
static int pendingResult;
static char savesDir[1024];

static int stateReady;
static int fileExists; /* card.dw3card is there: the virtual card holds the game's save file */
static int creating;   /* between CreateFile and the first data write: info writes belong to the card file */
static char fileName[24];
static unsigned char hdr[HDR_MAX];
static unsigned char cardInfo[INFO_SIZE];
static unsigned char stagedInfo[INFO_SIZE];
static int infoStaged;
static unsigned char wbuf[SLOTS][DATA_SIZE];
static Slot slots[SLOTS];

static int fileThere(const char *path) {
    struct stat st;

    return stat(path, &st) == 0;
}

static int portOf(int chan) {
    return (chan >> 4) & 1;
}

/* The memcard channel at trace (DW3_MEMCARD_TRACE=1 is the shortcut) logs every command's answer and every
   MemCardSync report (a stalled card screen shows which step never finishes) */
static int trace(void) {
    return plogOn("memcard", PLOG_TRACE);
}

static void done(int cmd, int result) {
    if (trace()) {
        plogLine("memcard", "cmd %d -> result %d\n", cmd, result);
    }
    pending = 1;
    pendingCmd = cmd;
    pendingResult = result;
}

/* ---- the PS1 card image (import source only) ---- */

/* The block of the file's byte OFS, following the directory's chain; 0 when the file is shorter */
static int blockOf(int first, int ofs) {
    int block = first;
    int n = ofs / BLOCK_SIZE;

    while (n-- > 0) {
        unsigned char *e = &card[block * FRAME_SIZE];
        int next = e[8] | (e[9] << 8);

        if (next == 0xFFFF) {
            return 0;
        }
        block = next + 1;
    }
    return block;
}

/* LEN bytes of the card file starting at its directory frame FIRST; 0 when the chain is shorter */
static int cardFileRead(int first, int ofs, unsigned char *out, int len) {
    while (len > 0) {
        int block = blockOf(first, ofs);
        int in = ofs % BLOCK_SIZE;
        int n = BLOCK_SIZE - in;

        if (block == 0) {
            return 0;
        }
        if (n > len) {
            n = len;
        }
        memcpy(out, &card[block * BLOCK_SIZE + in], n);
        out += n;
        ofs += n;
        len -= n;
    }
    return 1;
}

static int isDw3Name(const char *name) {
    char tmp[NAME_LEN + 1];

    memcpy(tmp, name, NAME_LEN);
    tmp[NAME_LEN] = '\0';
    return strstr(tmp, "DMW3") != NULL;
}

/* ---- the native files ---- */

static void savesPath(char *out, size_t cap, const char *leaf) {
    snprintf(out, cap, "%s/%s", savesDir, leaf);
}

static void slotPath(char *out, size_t cap, int n) {
    snprintf(out, cap, "%s/slot%d.dw3save", savesDir, n);
}

/* saves/<profile> of the user folder, or $DW3_SAVES_DIR */
static void savesInit(void) {
    if (!portPath(PP_SAVES, savesDir, sizeof(savesDir))) {
        snprintf(savesDir, sizeof(savesDir), "saves/%s", PP_DEFAULT_PROFILE);
    }
}

/* mkdir -p of the saves folder itself */
static void ensureSavesDir(void) {
    portMkdirs(savesDir);
}

static uint32_t maxSeq(void) {
    uint32_t best = 0;
    int i;

    for (i = 0; i < SLOTS; i++) {
        if (slots[i].valid && slots[i].seq > best) {
            best = slots[i].seq;
        }
    }
    return best;
}

/* The info section the game reads: the newest slot's copy, else the one from the file's creation */
static const unsigned char *currentInfo(void) {
    int best = -1;
    int i;

    for (i = 0; i < SLOTS; i++) {
        if (slots[i].valid && (best < 0 || slots[i].seq > slots[best].seq)) {
            best = i;
        }
    }
    return best >= 0 ? slots[best].info : cardInfo;
}

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* One slot as a dw3save; 1 when written */
static int writeSlotFile(int n, const unsigned char *game, const unsigned char *info, const unsigned char *ext, unsigned long extLen,
                         const unsigned char *techs, unsigned long techsLen, const unsigned char *digs, unsigned long digsLen,
                         const unsigned char *ents, unsigned long entsLen, const unsigned char *cards, unsigned long cardsLen, const unsigned char *vars, unsigned long varsLen, uint32_t seq) {
    SaveFile f;
    unsigned char meta[12], mods[8];
    char path[1200];

    memset(&f, 0, sizeof(f));
    put32(meta, (uint32_t)n);
    put32(meta + 4, seq);
    put32(meta + 8, GAME_SIZE);
    put32(mods, 1); /* MODS v1 ... */
    put32(mods + 4, 0); /* ... naming no mod yet (the content registry fills it in T3) */
    saveFileAdd(&f, "META", meta, sizeof(meta));
    saveFileAdd(&f, "GAME", game, GAME_SIZE);
    saveFileAdd(&f, "INFO", info, INFO_SIZE);
    saveFileAdd(&f, "EXTI", ext, extLen);
    if (techsLen != 0) {
        saveFileAdd(&f, "EXTT", techs, techsLen); /* the names of the new techniques the game block holds (ext_state.h) */
    }
    if (digsLen != 0) {
        saveFileAdd(&f, "EXTD", digs, digsLen); /* the names of the new partner forms it holds */
    }
    if (entsLen != 0) {
        saveFileAdd(&f, "EXTP", ents, entsLen); /* the partner entries beyond the original 44, with the names of new forms */
    }
    if (cardsLen != 0) {
        saveFileAdd(&f, "EXTK", cards, cardsLen); /* the new cards by name, and the names of the new ids in the decks */
    }
    if (varsLen != 0) {
        saveFileAdd(&f, "EXTV", vars, varsLen); /* the named variables of content mods (stage flags and counters), unknown names kept */
    }
    saveFileAdd(&f, "MODS", mods, sizeof(mods));
    slotPath(path, sizeof(path), n);
    return saveFileWrite(path, &f) == SAVEFILE_OK;
}

/* card.dw3card: the save file's existence, name, header+icons and creation-time info; 1 when written */
static int writeCardFile(void) {
    SaveFile f;
    char path[1200];

    memset(&f, 0, sizeof(f));
    saveFileAdd(&f, "NAME", fileName, sizeof(fileName));
    saveFileAdd(&f, "HDRS", hdr, sizeof(hdr));
    saveFileAdd(&f, "INFO", cardInfo, sizeof(cardInfo));
    savesPath(path, sizeof(path), "card.dw3card");
    return saveFileWrite(path, &f) == SAVEFILE_OK;
}

static void freeSlot(Slot *s) {
    free(s->ext);
    free(s->techs);
    free(s->digs);
    free(s->ents);
    free(s->cards);
    free(s->vars);
    memset(s, 0, sizeof(*s));
}

static void loadNative(void) {
    SaveFile f;
    char path[1200];
    int i;

    savesPath(path, sizeof(path), "card.dw3card");
    if (saveFileRead(path, &f) == SAVEFILE_OK) {
        const SaveSection *h = saveFileFind(&f, "HDRS");
        const SaveSection *n = saveFileFind(&f, "NAME");
        const SaveSection *in = saveFileFind(&f, "INFO");

        if (h != NULL && h->size == sizeof(hdr) && in != NULL && in->size == sizeof(cardInfo)) {
            memcpy(hdr, h->data, sizeof(hdr));
            memcpy(cardInfo, in->data, sizeof(cardInfo));
            memset(fileName, 0, sizeof(fileName));
            if (n != NULL && n->size <= sizeof(fileName)) {
                memcpy(fileName, n->data, n->size);
            }
            fileExists = 1;
        } else {
            plogLine("saves", "%s is not a card file of this port; ignored\n", path);
        }
        saveFileFree(&f);
    }
    for (i = 0; i < SLOTS; i++) {
        const SaveSection *g, *in, *meta, *ext, *techs, *digs, *ents, *cards, *vars;

        slotPath(path, sizeof(path), i);
        if (saveFileRead(path, &f) != SAVEFILE_OK) {
            continue;
        }
        g = saveFileFind(&f, "GAME");
        in = saveFileFind(&f, "INFO");
        meta = saveFileFind(&f, "META");
        ext = saveFileFind(&f, "EXTI");
        techs = saveFileFind(&f, "EXTT");
        digs = saveFileFind(&f, "EXTD");
        ents = saveFileFind(&f, "EXTP");
        cards = saveFileFind(&f, "EXTK");
        vars = saveFileFind(&f, "EXTV");
        if (g == NULL || g->size != GAME_SIZE || in == NULL || in->size != INFO_SIZE || meta == NULL || meta->size != 12) {
            plogLine("saves", "%s lacks the game block or info; the slot reads as empty\n", path);
        } else {
            memset(slots[i].game, 0, sizeof(slots[i].game));
            memcpy(slots[i].game, g->data, GAME_SIZE);
            memcpy(slots[i].info, in->data, INFO_SIZE);
            slots[i].seq = get32(meta->data + 4);
            if (ext != NULL && ext->size != 0) {
                slots[i].ext = malloc(ext->size);
                if (slots[i].ext != NULL) {
                    memcpy(slots[i].ext, ext->data, ext->size);
                    slots[i].extLen = ext->size;
                }
            }
            if (techs != NULL && techs->size != 0) {
                slots[i].techs = malloc(techs->size);
                if (slots[i].techs != NULL) {
                    memcpy(slots[i].techs, techs->data, techs->size);
                    slots[i].techsLen = techs->size;
                }
            }
            if (digs != NULL && digs->size != 0) {
                slots[i].digs = malloc(digs->size);
                if (slots[i].digs != NULL) {
                    memcpy(slots[i].digs, digs->data, digs->size);
                    slots[i].digsLen = digs->size;
                }
            }
            if (ents != NULL && ents->size != 0) {
                slots[i].ents = malloc(ents->size);
                if (slots[i].ents != NULL) {
                    memcpy(slots[i].ents, ents->data, ents->size);
                    slots[i].entsLen = ents->size;
                }
            }
            if (cards != NULL && cards->size != 0) {
                slots[i].cards = malloc(cards->size);
                if (slots[i].cards != NULL) {
                    memcpy(slots[i].cards, cards->data, cards->size);
                    slots[i].cardsLen = cards->size;
                }
            }
            if (vars != NULL && vars->size != 0) {
                slots[i].vars = malloc(vars->size);
                if (slots[i].vars != NULL) {
                    memcpy(slots[i].vars, vars->data, vars->size);
                    slots[i].varsLen = vars->size;
                }
            }
            slots[i].valid = 1;
        }
        saveFileFree(&f);
    }
}

/* ---- the one-way import of a PS1 card image ---- */

static void writeMarker(const char *source, int slotsImported, const char *what) {
    char path[PP_CAP];
    FILE *f;

    if (!portPathsImportMarker(path, sizeof(path))) {
        return;
    }
    portMkdirsFor(path);
    f = fopen(path, "wb");
    if (f != NULL) {
        fprintf(f, "source=%s\nresult=%s\nslots=%d\n", source, what, slotsImported);
        fclose(f);
    }
}

/* GameState.checksum is the XOR of the bytes from 4; a slot of zeros is a free slot */
static int slotDataUsable(const unsigned char *g) {
    unsigned char x = 0;
    int i, any = 0;

    for (i = 4; i < GAME_SIZE; i++) {
        x ^= g[i];
        any |= g[i];
    }
    return any != 0 && (x & ~g[0]) == 0;
}

/* What parseCard read from one card image (the card itself stays in `card`): the DW3 file's name, header, info and the usable slots */
static unsigned char pdata[SLOTS][DATA_SIZE];
static int pok[SLOTS];
static int pfailed, pusable, plast;
static char pname[NAME_LEN + 1];

/* Reads one card image (raw .mcd/.mcr, or a .gme/.vmp whose header is stripped) and finds the DW3 file in it. Touches nothing on disk
   and no saved state except hdr/fileName/cardInfo (the card file's content, written by commitParsed). Returns the number of usable slots
   (0 with *what saying why, *mark 1 when retrying next start is pointless). A card that holds the file but no usable slot returns 0
   with *what "imported" and *mark 0/1 as the auto import records it. */
static int parseCard(const char *cardPath, const char **what, int *mark) {
    const char *why = "";
    int i, first = 0, bestPri = 99, others = 0, size, hdrEnd, n;
    unsigned char info[INFO_SIZE];
    char name[NAME_LEN + 1];

    *what = "";
    *mark = 0;
    pusable = pfailed = 0;
    memset(pok, 0, sizeof(pok));
    if (!portIsFile(cardPath)) {
        *what = "card file not found";
        return 0;
    }
    if (!cardImageRead(cardPath, card, &why)) {
        plogLine("saves", "%s is not a PS1 memory card image (%s); nothing to import\n", cardPath, why);
        *what = "not a memory card";
        *mark = 1;
        return 0;
    }
    for (i = DIR_FIRST; i <= DIR_LAST; i++) {
        unsigned char *e = &card[i * FRAME_SIZE];
        int pri;

        if (e[0] != ST_FIRST || !isDw3Name((char *)e + 10)) {
            continue;
        }
        memcpy(name, e + 10, NAME_LEN);
        name[NAME_LEN] = '\0';
        pri = strstr(name, "-EUR") ? 0 : (strstr(name, "-USA") ? 1 : 2);
        if (pri < bestPri) {
            if (first != 0) {
                others++;
            }
            bestPri = pri;
            first = i;
        } else {
            others++;
        }
    }
    if (first == 0) {
        plogLine("saves", "%s holds no Digimon World 3 save; nothing imported\n", cardPath);
        *what = "no Digimon World 3 file";
        *mark = 1;
        return 0;
    }
    memcpy(name, &card[first * FRAME_SIZE + 10], NAME_LEN);
    name[NAME_LEN] = '\0';
    size = card[first * FRAME_SIZE + 4] | (card[first * FRAME_SIZE + 5] << 8) | (card[first * FRAME_SIZE + 6] << 16);
    if (others != 0) {
        plogLine("saves", "the card holds %d more Digimon World 3 file(s); only %s is imported\n", others, name);
    }
    if (!cardFileRead(first, 0, hdr, size < HDR_MAX ? size : HDR_MAX) || (n = hdr[2] & 3) == 0 || hdr[0] != 'S' || hdr[1] != 'C') {
        plogLine("saves", "%s on the card has no valid header; nothing imported\n", name);
        memset(hdr, 0, sizeof(hdr));
        *what = "bad save file header";
        *mark = 1;
        return 0;
    }
    hdrEnd = (n + 1) * SECTOR;
    if (size < hdrEnd + INFO_SIZE || !cardFileRead(first, hdrEnd, info, INFO_SIZE)) {
        plogLine("saves", "%s on the card is too short; nothing imported\n", name);
        memset(hdr, 0, sizeof(hdr));
        *what = "save file too short";
        *mark = 1;
        return 0;
    }
    memset(fileName, 0, sizeof(fileName));
    memcpy(fileName, name, NAME_LEN);
    memcpy(pname, name, sizeof(pname));
    memcpy(cardInfo, info, INFO_SIZE);
    plast = info[1];
    for (i = 0; i < SLOTS; i++) {
        unsigned char *data = pdata[i];
        int ofs = hdrEnd + INFO_SIZE + i * DATA_SIZE;

        if (ofs + DATA_SIZE > size || !cardFileRead(first, ofs, data, DATA_SIZE)) {
            plogLine("saves", "slot %d of %s is cut short; skipped\n", i, name);
            continue;
        }
        if (!slotDataUsable(data)) {
            int any = 0, j;

            for (j = 0; j < GAME_SIZE; j++) {
                any |= data[j];
            }
            if (any != 0) {
                plogLine("saves", "slot %d of %s fails the game's own checksum; skipped\n", i, name);
                pfailed++;
            }
            continue;
        }
        pok[i] = 1;
        pusable++;
    }
    *what = "parsed";
    return pusable;
}

/* Writes what parseCard found: the usable slots and the card file. Returns the slots written */
static int commitParsed(const char *cardPath, const char **what, int *mark) {
    unsigned char info[INFO_SIZE];
    int i, imported = 0, failed = pfailed;

    ensureSavesDir();
    memcpy(info, cardInfo, INFO_SIZE);
    for (i = 0; i < SLOTS; i++) {
        if (pok[i]) {
            if (writeSlotFile(i, pdata[i], info, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0, i == plast ? SLOTS + 1 : (uint32_t)(i + 1))) {
                imported++;
            } else {
                failed++;
            }
        }
    }
    if (!writeCardFile()) {
        failed++;
    }
    *what = "imported";
    *mark = failed == 0 || imported != 0;
    plogLine("saves", "imported %d slot(s) from %s (%s) into %s; the card file was not changed\n", imported, cardPath, pname, savesDir);
    return imported;
}

/* One card image: the number of slots imported. *what and *mark tell the record: mark 0 means "try again next start" */
static int importCard(const char *cardPath, const char **what, int *mark) {
    ensureSavesDir();
    if (!parseCard(cardPath, what, mark) && strcmp(*what, "parsed") != 0) {
        return 0;
    }
    return commitParsed(cardPath, what, mark);
}

/* The cards of import/ in order, until one gives saves; the record is written once, for the last one tried */
static void importCards(void) {
    static char cards[8][PP_CAP];
    int n = portPathsImportCards(cards, 8);
    int i, imported = 0, mark = 0;
    const char *what = "";

    for (i = 0; i < n && imported == 0; i++) {
        imported = importCard(cards[i], &what, &mark);
    }
    if (n > 0 && mark) {
        writeMarker(cards[i - 1], imported, what);
    }
}

/* ---- the player's own import (M2 / R4) ---- */
static void stateInit(void);

static int copyFile(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out;
    unsigned char buf[4096];
    size_t n;
    int ok = 1;

    if (in == NULL) {
        return 1; /* nothing to copy is not a failure */
    }
    out = fopen(to, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }
    fclose(in);
    return fclose(out) == 0 && ok;
}

/* Copies the card file and the slot files (and their .bak) to saves/<profile>/backup/<stamp>[-n]/. 1 when done or when there was nothing */
static int backupSaves(char *outDir, size_t cap) {
    static const char *const leaves[] = {"card.dw3card", "slot0.dw3save", "slot1.dw3save", "slot2.dw3save",
                                         "slot0.dw3save.bak", "slot1.dw3save.bak", "slot2.dw3save.bak"};
    char from[1200], to[1300], stamp[32], base[1200];
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    size_t i;
    int any = 0, n;

    outDir[0] = '\0';
    for (i = 0; i < sizeof(leaves) / sizeof(leaves[0]); i++) {
        savesPath(from, sizeof(from), leaves[i]);
        any |= fileThere(from);
    }
    if (!any) {
        return 1;
    }
    if (tmv == NULL || strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tmv) == 0) {
        snprintf(stamp, sizeof(stamp), "%lld", (long long)now);
    }
    snprintf(base, sizeof(base), "%s/backup/%s", savesDir, stamp);
    snprintf(outDir, cap, "%s", base);
    for (n = 2; portIsDir(outDir) && n < 100; n++) {
        snprintf(outDir, cap, "%s-%d", base, n);
    }
    if (!portMkdirs(outDir)) {
        outDir[0] = '\0';
        return 0;
    }
    for (i = 0; i < sizeof(leaves) / sizeof(leaves[0]); i++) {
        savesPath(from, sizeof(from), leaves[i]);
        snprintf(to, sizeof(to), "%s/%s", outDir, leaves[i]);
        if (!copyFile(from, to)) {
            return 0;
        }
    }
    return 1;
}

static const char *baseName(const char *p) {
    const char *b = p, *q;

    for (q = p; *q != '\0'; q++) {
        if (*q == '/' || *q == '\\') {
            b = q + 1;
        }
    }
    return b;
}

/* parseCard's reason as the player reads it */
static void failText(char *msg, size_t cap, const char *path, const char *what, int failedSlots) {
    if (!strcmp(what, "card file not found")) {
        snprintf(msg, cap, "Card file not found: %s", baseName(path));
    } else if (!strcmp(what, "not a memory card")) {
        snprintf(msg, cap, "%s is not a PS1 memory card (128 KB .mcr/.mcd)", baseName(path));
    } else if (!strcmp(what, "no Digimon World 3 file")) {
        snprintf(msg, cap, "No Digimon World 3 save on %s", baseName(path));
    } else if (!strcmp(what, "parsed")) {
        snprintf(msg, cap, failedSlots ? "The saves on %s are damaged (checksum)" : "%s holds no saved game", baseName(path));
    } else {
        snprintf(msg, cap, "The Digimon World 3 save on %s is damaged", baseName(path));
    }
}

int hostMemcardImportFile(const char *path, char *msg, size_t cap) {
    unsigned char keepHdr[HDR_MAX], keepInfo[INFO_SIZE];
    char keepName[sizeof(fileName)], bak[1300], p[1300];
    const char *what = "";
    int mark = 0, usable, imported, i;

    stateInit();
    memcpy(keepHdr, hdr, sizeof(hdr));
    memcpy(keepInfo, cardInfo, sizeof(cardInfo));
    memcpy(keepName, fileName, sizeof(fileName));
    usable = parseCard(path, &what, &mark);
    if (usable <= 0) {
        failText(msg, cap, path, what, pfailed);
        memcpy(hdr, keepHdr, sizeof(hdr)); /* the live card file is untouched */
        memcpy(cardInfo, keepInfo, sizeof(cardInfo));
        memcpy(fileName, keepName, sizeof(fileName));
        memset(card, 0, sizeof(card));
        plogLine("saves", "import of %s refused: %s\n", path, msg);
        return 0;
    }
    if (!backupSaves(bak, sizeof(bak))) {
        snprintf(msg, cap, "Could not back up the current saves; nothing changed");
        memcpy(hdr, keepHdr, sizeof(hdr));
        memcpy(cardInfo, keepInfo, sizeof(cardInfo));
        memcpy(fileName, keepName, sizeof(fileName));
        memset(card, 0, sizeof(card));
        plogLine("saves", "import of %s refused: %s\n", path, msg);
        return 0;
    }
    imported = commitParsed(path, &what, &mark);
    for (i = 0; i < SLOTS; i++) { /* the saves become the card's: slots it does not hold go (they are in the backup) */
        if (!pok[i]) {
            slotPath(p, sizeof(p), i);
            remove(p);
            snprintf(p + strlen(p), sizeof(p) - strlen(p), ".bak");
            remove(p);
            snprintf(p, sizeof(p), "%s/view/slot%d.json", savesDir, i);
            remove(p);
        }
    }
    for (i = 0; i < SLOTS; i++) {
        freeSlot(&slots[i]);
    }
    memset(card, 0, sizeof(card));
    loadNative();
    writeMarker(path, imported, "imported");
    if (imported <= 0) {
        snprintf(msg, cap, "Could not write the saves; see the log");
        return 0;
    }
    snprintf(msg, cap, "Imported %d save%s from %s", imported, imported == 1 ? "" : "s", baseName(path));
    plogLine("saves", "player import: %s%s%s\n", msg, bak[0] ? "; the previous saves are in " : "", bak);
    return 1;
}

int hostMemcardImportNow(char *msg, size_t cap) {
    static char found[16][PP_CAP];
    char first[256] = "";
    int n = cardImageFind(found, 16), i;

    if (n == 0) {
        const char *d = hostMemcardDropDir();

        snprintf(msg, cap, d[0] ? "No card found. Put it in %s" : "No card found", d);
        plogLine("saves", "player import: %s\n", msg);
        return 0;
    }
    for (i = 0; i < n; i++) {
        char m[256];

        if (hostMemcardImportFile(found[i], m, sizeof(m))) {
            snprintf(msg, cap, "%s", m);
            return 1;
        }
        if (first[0] == '\0') {
            snprintf(first, sizeof(first), "%s", m);
        }
    }
    snprintf(msg, cap, "%s", first);
    return 0;
}

const char *hostMemcardDropDir(void) {
    static char dir[PP_CAP];

    return portPath(PP_IMPORT, dir, sizeof(dir)) ? dir : "";
}

static void stateInit(void) {
    char a[1200], b[1200];
    int i, any = 0;

    if (stateReady) {
        return;
    }
    stateReady = 1;
    savesInit();
    savesPath(a, sizeof(a), "card.dw3card");
    for (i = 0; i < SLOTS; i++) {
        slotPath(b, sizeof(b), i);
        any |= fileThere(b);
    }
    if (!fileThere(a) && !any && portPathsImportMarker(b, sizeof(b)) && !fileThere(b)) {
        importCards();
        memset(card, 0, sizeof(card));
    }
    loadNative();
    if (trace()) {
        plogLine("memcard", "saves in %s, file %s\n", savesDir, fileExists ? "there" : "not created");
    }
}

/* Forgets everything (a fresh process); the tests' hook */
void hostMemcardReset(void) {
    int i;

    for (i = 0; i < SLOTS; i++) {
        freeSlot(&slots[i]);
    }
    memset(card, 0, sizeof(card));
    memset(hdr, 0, sizeof(hdr));
    memset(cardInfo, 0, sizeof(cardInfo));
    memset(stagedInfo, 0, sizeof(stagedInfo));
    memset(fileName, 0, sizeof(fileName));
    savesDir[0] = '\0';
    stateReady = fileExists = creating = infoStaged = 0;
    started = 0;
    newCardPending = 1;
    pending = 0;
}

void MemCardInit(int val) {
    pending = 0;
    newCardPending = 1;
}

void MemCardEnd(void) {
}

void MemCardStart(void) {
    started = 1;
}

void MemCardStop(void) {
    started = 0;
}

int MemCardExist(int chan) {
    if (!started) {
        return 0;
    }
    done(FN_EXIST, portOf(chan) == 0 ? ERR_NONE : ERR_NO_CARD);
    return 1;
}

int MemCardAccept(int chan) {
    if (!started) {
        return 0;
    }
    if (portOf(chan) != 0) {
        done(FN_ACCEPT, ERR_NO_CARD);
        return 1;
    }
    stateInit();
    if (newCardPending) {
        newCardPending = 0;
        done(FN_ACCEPT, ERR_NEW_CARD);
    } else {
        done(FN_ACCEPT, ERR_NONE);
    }
    return 1;
}

int MemCardOpen(int chan, char *file, int flag) {
    return 0;
}

void MemCardClose(void) {
}

int MemCardReadData(unsigned int *adrs, int ofs, int bytes) {
    return 0;
}

int MemCardWriteData(unsigned int *adrs, int ofs, int bytes) {
    return 0;
}

/* The game's slot N is complete (its last data sector arrived): the dw3save of the slot, with the info the game
   staged just before and the extension as it is now. 1 when written */
static int commitSlot(int n) {
    unsigned long extLen = pcExtStateEncode(NULL, 0);
    unsigned char *ext = malloc(extLen);
    unsigned long techsLen = pcExtStateEncodeTechs(wbuf[n], GAME_SIZE, NULL, 0);
    unsigned char *techs = malloc(techsLen + 1);
    unsigned long digsLen = pcExtStateEncodeDigimon(wbuf[n], GAME_SIZE, NULL, 0);
    unsigned char *digs = malloc(digsLen + 1);
    unsigned long entsLen = pcExtStateEncodeEntries(NULL, 0);
    unsigned char *ents = malloc(entsLen + 1);
    unsigned long cardsLen = pcExtStateEncodeCards(wbuf[n], GAME_SIZE, NULL, 0);
    unsigned char *cards = malloc(cardsLen + 1);
    unsigned long varsLen = pcExtStateEncodeVars(NULL, 0);
    unsigned char *vars = malloc(varsLen + 1);
    unsigned char info[INFO_SIZE];
    uint32_t seq = maxSeq() + 1;

    if (ext == NULL || techs == NULL || digs == NULL || ents == NULL || cards == NULL || vars == NULL) {
        free(vars);
        free(cards);
        free(ext);
        free(techs);
        free(digs);
        free(ents);
        return 0;
    }
    pcExtStateEncode(ext, extLen);
    if (techsLen != 0) {
        pcExtStateEncodeTechs(wbuf[n], GAME_SIZE, techs, techsLen);
    }
    if (digsLen != 0) {
        pcExtStateEncodeDigimon(wbuf[n], GAME_SIZE, digs, digsLen);
    }
    if (entsLen != 0) {
        pcExtStateEncodeEntries(ents, entsLen); /* the partner entries beyond the original 44 */
    }
    if (cardsLen != 0) {
        pcExtStateEncodeCards(wbuf[n], GAME_SIZE, cards, cardsLen);
    }
    if (varsLen != 0) {
        pcExtStateEncodeVars(vars, varsLen); /* the mods' named variables */
    }
    memcpy(info, infoStaged ? stagedInfo : currentInfo(), INFO_SIZE);
    if (!writeSlotFile(n, wbuf[n], info, ext, extLen, techs, techsLen, digs, digsLen, ents, entsLen, cards, cardsLen, vars, varsLen, seq)) {
        free(vars);
        free(cards);
        free(ext);
        free(techs);
        free(digs);
        free(ents);
        return 0;
    }
    {
        /* T3b: the read-only readable view next to the save; the save is already safe, so a failure only logs */
        char vpath[1200];
        char *view = saveViewBuild(wbuf[n], GAME_SIZE, n, seq);

        char leaf[32];

        snprintf(leaf, sizeof(leaf), "view/slot%d.json", n);
        portJoin(vpath, sizeof(vpath), savesDir, leaf);
        portMkdirsFor(vpath);
        if (view == NULL || !saveViewWriteFile(vpath, view)) {
            plogLine("saves", "slot %d: the readable view %s could not be written (the save itself is fine)\n", n, vpath);
        }
        free(view);
    }
    free(slots[n].ext);
    free(slots[n].techs);
    free(slots[n].digs);
    free(slots[n].ents);
    free(slots[n].cards);
    free(slots[n].vars);
    memset(slots[n].game, 0, sizeof(slots[n].game));
    memcpy(slots[n].game, wbuf[n], GAME_SIZE);
    memcpy(slots[n].info, info, INFO_SIZE);
    slots[n].ext = ext;
    slots[n].extLen = extLen;
    slots[n].techs = techs;
    slots[n].techsLen = techsLen;
    slots[n].digs = digs;
    slots[n].digsLen = digsLen;
    slots[n].ents = ents;
    slots[n].entsLen = entsLen;
    slots[n].cards = cards;
    slots[n].cardsLen = cardsLen;
    slots[n].vars = vars;
    slots[n].varsLen = varsLen;
    slots[n].techsDone = 1; /* wbuf holds this run's ids */
    slots[n].seq = seq;
    slots[n].valid = 1;
    infoStaged = 0;
    PLOG("saves", PLOG_TRACE, "slot %d saved (sequence %u, extension %lu bytes)\n", n, (unsigned)seq, extLen);
    return 1;
}

/* One sector of the game's save file: READ copies out, else in */
static int fileIo(int chan, const char *file, void *buf, int ofs, int bytes, int write) {
    int hdrEnd, dataBase, end, ok = 1, code = write ? FN_WRITE_FILE : FN_READ_FILE;

    if (!started) {
        return 0;
    }
    if (portOf(chan) != 0) {
        done(code, ERR_NO_CARD);
        return 1;
    }
    stateInit();
    if (trace()) {
        plogLine("memcard", "%s %s ofs 0x%X bytes %d\n", write ? "write" : "read", file, ofs, bytes);
    }
    if (!fileExists || !isDw3Name(file)) {
        done(code, ERR_NO_FILE);
        return 1;
    }
    hdrEnd = ((hdr[2] & 3) + 1) * SECTOR;
    dataBase = hdrEnd + INFO_SIZE;
    end = ofs + bytes;
    if (ofs < 0 || bytes <= 0 || (bytes % SECTOR) != 0 || end > dataBase + SLOTS * DATA_SIZE || end > FILE_BLOCKS * BLOCK_SIZE) {
        done(code, ERR_NO_FILE);
        return 1;
    }
    if (ofs < hdrEnd) { /* header and icon frames */
        if (end > hdrEnd || end > HDR_MAX) {
            done(code, ERR_NO_FILE);
            return 1;
        }
        if (write) {
            memcpy(&hdr[ofs], buf, bytes);
            ok = writeCardFile();
        } else {
            memcpy(buf, &hdr[ofs], bytes);
        }
    } else if (ofs < dataBase) { /* info section */
        int rel = ofs - hdrEnd;

        if (end > dataBase) {
            done(code, ERR_NO_FILE);
            return 1;
        }
        if (write) {
            memcpy(&stagedInfo[rel], buf, bytes);
            if (rel + bytes >= INFO_SIZE) {
                if (creating) { /* the new file's empty info belongs to the card file */
                    memcpy(cardInfo, stagedInfo, INFO_SIZE);
                    ok = writeCardFile();
                } else { /* a save's: it goes with the data that follows */
                    infoStaged = 1;
                }
            }
        } else {
            memcpy(buf, &currentInfo()[rel], bytes);
        }
    } else { /* a slot's data section */
        int slot = (ofs - dataBase) / DATA_SIZE;
        int within = (ofs - dataBase) - slot * DATA_SIZE;

        if (within + bytes > DATA_SIZE) {
            done(code, ERR_NO_FILE);
            return 1;
        }
        if (write) {
            creating = 0;
            memcpy(&wbuf[slot][within], buf, bytes);
            if (within + bytes >= GAME_SIZE) {
                ok = commitSlot(slot);
            }
        } else {
            if (slots[slot].valid && !slots[slot].techsDone) {
                /* the game block of a file read from disk holds the technique ids of the run that saved it: the ids the
                   installed mods give those names now (a removed mod's technique is emptied from its skill slot) */
                slots[slot].techsDone = 1;
                pcExtStateRemapTechs(slots[slot].game, GAME_SIZE, slots[slot].techs, slots[slot].techsLen);
                pcExtStateRemapDigimon(slots[slot].game, GAME_SIZE, slots[slot].digs, slots[slot].digsLen);
                pcExtStateRemapCards(slots[slot].game, GAME_SIZE, slots[slot].cards, slots[slot].cardsLen);
            }
            if (slots[slot].valid) {
                memcpy(buf, &slots[slot].game[within], bytes);
            } else {
                memset(buf, 0, bytes);
            }
            if (within + bytes >= GAME_SIZE && slots[slot].valid) {
                /* the slot's extra state arrives with its data (an original-only save has none: empty) */
                pcExtStateDecode(slots[slot].ext, slots[slot].extLen);
                pcExtStateDecodeEntries(slots[slot].ents, slots[slot].entsLen);
                pcExtStateDecodeCards(slots[slot].cards, slots[slot].cardsLen);
                pcExtStateDecodeVars(slots[slot].vars, slots[slot].varsLen); /* last: DecodeEntries (and a new game) forget the variables too */
            }
        }
    }
    done(code, ok ? ERR_NONE : ERR_INVALID);
    return 1;
}

int MemCardReadFile(int chan, char *file, unsigned int *adrs, int ofs, int bytes) {
    return fileIo(chan, file, adrs, ofs, bytes, 0);
}

int MemCardWriteFile(int chan, char *file, unsigned int *adrs, int ofs, int bytes) {
    return fileIo(chan, file, adrs, ofs, bytes, 1);
}

int MemCardCreateFile(int chan, char *file, int blocks) {
    if (portOf(chan) != 0) {
        return ERR_NO_CARD;
    }
    stateInit();
    if (fileExists) {
        return ERR_EXISTS;
    }
    if (blocks < 1 || blocks > DIR_LAST) {
        return ERR_FULL;
    }
    memset(hdr, 0, sizeof(hdr));
    memset(cardInfo, 0, sizeof(cardInfo));
    memset(stagedInfo, 0, sizeof(stagedInfo));
    memset(fileName, 0, sizeof(fileName));
    strncpy(fileName, file, sizeof(fileName) - 1);
    fileExists = 1;
    creating = 1;
    infoStaged = 0;
    ensureSavesDir();
    if (!writeCardFile()) {
        fileExists = 0;
        creating = 0;
        return ERR_FULL;
    }
    return ERR_NONE;
}

/* Not reachable from the game; removes the save file from the virtual card but never the slots */
int MemCardDeleteFile(int chan, char *file) {
    char path[1200];

    stateInit();
    if (!fileExists || !isDw3Name(file)) {
        return ERR_NO_FILE;
    }
    savesPath(path, sizeof(path), "card.dw3card");
    remove(path);
    fileExists = 0;
    return ERR_NONE;
}

/* The virtual card is always formatted and never erased */
int MemCardFormat(int chan) {
    if (portOf(chan) != 0) {
        return ERR_NO_CARD;
    }
    return ERR_NONE;
}

int MemCardUnformat(int chan) {
    return ERR_NO_CARD;
}

int MemCardSync(int mode, int *cmds, int *rslt) {
    if (!pending) {
        return -1;
    }
    pending = 0;
    if (trace()) {
        plogLine("memcard", "sync reports cmd %d result %d\n", pendingCmd, pendingResult);
    }
    *(long *)cmds = pendingCmd; /* see the file's header: long/u_long callers */
    *(uintptr_t *)rslt = (uintptr_t)pendingResult;
    return 1;
}

void *MemCardCallback(void *func) {
    return NULL;
}

int MemCardGetDirentry(int chan, char *name, struct HostDirEntry *dir, int *files, int ofs, int max) {
    int n = 0;

    if (portOf(chan) != 0) {
        return ERR_NO_CARD;
    }
    stateInit();
    if (fileExists && max > 0 && ofs <= 0 && (strcmp(name, "*") == 0 || strncmp(name, fileName, NAME_LEN) == 0)) {
        HostDirEntry *d = &dir[n++];

        memset(d, 0, sizeof(*d));
        memcpy(d->name, fileName[0] != '\0' ? fileName : DEFAULT_NAME, NAME_LEN);
        d->size = FILE_BLOCKS * BLOCK_SIZE;
        d->head = BLOCK_SIZE;
    }
    *files = n;
    if (trace()) {
        plogLine("memcard", "directory lists %d file(s)\n", n);
    }
    return ERR_NONE;
}
