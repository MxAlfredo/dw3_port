/*
 * Offline check for the virtual memory card (pc/shim/host_memcard.c), ADR-010 layout:
 *  - the game's MemCard* calls are served from native dw3save slot files in saves/<profile>/ (extended-state T2);
 *  - a PS1 card (*.mcd) placed in import/ is imported once, one way (the .mcd is never written); the
 *    "already imported" record lives in import/;
 *  - the readable view of a save goes to saves/<profile>/view/slotN.json;
 *  - nothing of the old layouts is read: a memcard0.mcd next to the settings is ignored.
 *
 * It includes the unit to reach its static logic and puts the user folder in a scratch one: run as
 * `make memcardtest`, it writes only under $TMPDIR (or /tmp).
 */
#define _GNU_SOURCE 1
#include "engine/game_state.h"
#include "shim/ext_state.h"
#include "shim/host_memcard.c"
#include "shim/mod_stages.h"

GameState GAME;
TechData TECHS_CARD[443]; /* the game's technique table, which ext_state.c reaches */
DigimonData DIGIMON_DATA_CARD[55]; /* and the Digimon table */

#include <unistd.h>

static char prefDir[1024];

/* the platform's per-user folder is the scratch one (strong over the layer's weak hook) */
int portPathsPlatform(char *exeDir, int exeCap, char *pref, int prefCap) {
    if (exeCap > 0) {
        exeDir[0] = '\0';
    }
    if (prefCap <= 0) {
        return 0;
    }
    snprintf(pref, (size_t)prefCap, "%s", prefDir);
    return 1;
}


static int failures;

static void expect(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* ---- the game's side of the card: sector-wise calls, as src/engine/memcard/memcard.c makes them ---- */

#define NAME_EUR "BESLES-03936DMW3-EUR"
#define SEC 128
#define HDR_END ((1 + 1) * SEC) /* header + 1 icon frame */
#define INFO_OFS HDR_END
#define DATA_OFS(n) (HDR_END + 0x100 + (n)*0x2700)

static int syncResult(void) {
    long cmds = 0;
    uintptr_t res = 99;

    if (MemCardSync(1, (int *)&cmds, (int *)&res) != 1) {
        return -1;
    }
    return (int)res;
}

static int rd(const char *name, void *buf, int ofs, int bytes) {
    if (MemCardReadFile(0, (char *)name, buf, ofs, bytes) != 1) {
        return -2;
    }
    return syncResult();
}

static int wr(const char *name, void *buf, int ofs, int bytes) {
    if (MemCardWriteFile(0, (char *)name, buf, ofs, bytes) != 1) {
        return -2;
    }
    return syncResult();
}

/* a whole section in sectors, the way readSave/writeSave step through it; 0 when every sector was fine */
static int rdSection(const char *name, void *buf, int ofs, int size) {
    int p;

    for (p = 0; p < size; p += SEC) {
        int r = rd(name, (unsigned char *)buf + p, ofs + p, SEC);

        if (r != 0) {
            return r ? r : -3;
        }
    }
    return 0;
}

static int wrSection(const char *name, void *buf, int ofs, int size) {
    int p;

    for (p = 0; p < size; p += SEC) {
        int r = wr(name, (unsigned char *)buf + p, ofs + p, SEC);

        if (r != 0) {
            return r ? r : -3;
        }
    }
    return 0;
}

#define SKILL_AT(p, e, k) (offsetof(GameState, partners) + (size_t)(p) * sizeof(Partner) + offsetof(Partner, info) + \
                           offsetof(PartnerStats, entries) + (size_t)(e) * sizeof(PartnerEntry) + offsetof(PartnerEntry, skills) + (size_t)(k) * 2)

static void putSkill(unsigned char *g, int p, int e, int k, unsigned raw) {
    g[SKILL_AT(p, e, k)] = (unsigned char)raw;
    g[SKILL_AT(p, e, k) + 1] = (unsigned char)(raw >> 8);
}

static unsigned skillOf(const unsigned char *g, int p, int e, int k) {
    return (unsigned)g[SKILL_AT(p, e, k)] | ((unsigned)g[SKILL_AT(p, e, k) + 1] << 8);
}

/* a plausible GAME block: byte 0 holds the XOR of bytes 4.. as the game stores it. The partners' skill words are left
   empty: a patterned filler there would read as technique ids of mods (above 443), which a load remaps or removes */
static void makeGame(unsigned char *g, int seed) {
    int i, p, e, k;
    unsigned char x = 0;

    memset(g, 0, 0x2700);
    for (i = 4; i < 0x26C4; i++) {
        g[i] = (unsigned char)(i * 7 + seed);
    }
    for (p = 0; p < PARTNER_COUNT; p++) {
        /* no id of a new technique or a new Digimon in the filler (a real block never holds one without its mod) */
        size_t base = offsetof(GameState, partners) + (size_t)p * sizeof(Partner);

        for (e = 0; e < PARTNER_ENTRY_COUNT; e++) {
            size_t at = base + offsetof(Partner, info) + offsetof(PartnerStats, entries) + (size_t)e * sizeof(PartnerEntry);

            for (k = 0; k < 6; k++) putSkill(g, p, e, k, 0);
            memset(g + at + offsetof(PartnerEntry, id), 0, 2);
        }
        memset(g + base + offsetof(Partner, battleDigivolve), 0, 4);
        memset(g + base + offsetof(Partner, info) + offsetof(PartnerStats, slots), 0, 8);
    }
    for (i = 4; i < 0x26C4; i++) {
        x ^= g[i];
    }
    g[0] = x;
}

static void makeInfo(unsigned char *info, int last) {
    int i;

    memset(info, 0, 0x100);
    for (i = 8; i < 0xD4; i++) {
        info[i] = (unsigned char)(i + last);
    }
    info[1] = (unsigned char)last;
    info[4] = 'D'; info[5] = 'M'; info[6] = 'W'; info[7] = '3';
}

/* a synthetic 128 KB .mcd with one DW3 file at blocks 1-4 (header + 1 icon, info, slots 0 and 2 saved) */
static void buildCard(const char *path, const unsigned char *g0, const unsigned char *g2, const unsigned char *info) {
    static unsigned char img[CARD_SIZE];
    int i;
    FILE *f;

    memset(img, 0, sizeof(img));
    img[0] = 'M';
    img[1] = 'C';
    for (i = DIR_FIRST; i <= DIR_LAST; i++) {
        unsigned char *e = &img[i * FRAME_SIZE];

        e[0] = ST_FREE;
        e[8] = e[9] = 0xFF;
    }
    for (i = 1; i <= 4; i++) {
        unsigned char *e = &img[i * FRAME_SIZE];
        int next = (i == 4) ? 0xFFFF : i; /* next block - 1 */

        e[0] = (i == 1) ? ST_FIRST : (i == 4 ? ST_LAST : ST_MIDDLE);
        e[8] = next & 0xFF;
        e[9] = next >> 8;
        if (i == 1) {
            e[4] = 0x00; e[5] = 0x80; e[6] = 0; /* 0x8000 */
            memcpy(e + 10, NAME_EUR, 20);
        }
    }
    /* the file's bytes: img[BLOCK_SIZE .. ] (blocks 1-4 are contiguous) */
    img[BLOCK_SIZE + 0] = 'S';
    img[BLOCK_SIZE + 1] = 'C';
    img[BLOCK_SIZE + 2] = 0x11;
    img[BLOCK_SIZE + 3] = 4;
    memcpy(&img[BLOCK_SIZE + 4], "TEST TITLE", 10);
    memset(&img[BLOCK_SIZE + SEC], 0x5A, SEC); /* the icon frame */
    memcpy(&img[BLOCK_SIZE + INFO_OFS], info, 0x100);
    memcpy(&img[BLOCK_SIZE + DATA_OFS(0)], g0, 0x2700);
    memcpy(&img[BLOCK_SIZE + DATA_OFS(2)], g2, 0x2700);
    f = fopen(path, "wb");
    fwrite(img, 1, sizeof(img), f);
    fclose(f);
}

static long sizeOf(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static const char *extName(int id) {
    return id == 600 ? "test:gadget" : NULL;
}

static int extId(const char *name) {
    return strcmp(name, "test:gadget") == 0 ? 600 : -1;
}

/* new partner forms: "x:fire" is id 471 in the run that saves and 480 later; "x:gone" has no mod later */
static const char *formName(int id) {
    return id == 471 ? "x:fire" : id == 472 ? "x:gone" : NULL;
}
static int formIdSame(const char *name) {
    return !strcmp(name, "x:fire") ? 471 : !strcmp(name, "x:gone") ? 472 : -1;
}
static int formIdLater(const char *name) {
    return !strcmp(name, "x:fire") ? 480 : -1;
}
static int formBase(int id) {
    return id == 471 ? 5 : id == 472 ? 6 : -1;
}
#define FORM_ID_AT(p, e) (offsetof(GameState, partners) + (p) * sizeof(Partner) + offsetof(Partner, info) + \
                          offsetof(PartnerStats, entries) + (e) * sizeof(PartnerEntry) + offsetof(PartnerEntry, id))
static void putForm(unsigned char *g, int p, int e, int id) {
    g[FORM_ID_AT(p, e)] = (unsigned char)id;
    g[FORM_ID_AT(p, e) + 1] = (unsigned char)(id >> 8);
}
static int formOf(const unsigned char *g, int p, int e) {
    return g[FORM_ID_AT(p, e)] | (g[FORM_ID_AT(p, e) + 1] << 8);
}

static const char *techName(int id) {
    return id == 500 ? "x:one" : id == 501 ? "x:two" : NULL;
}

/* the ids a later run gives those names: x:one moved to 510, x:two's mod is gone */
static int techIdLater(const char *name) {
    return strcmp(name, "x:one") == 0 ? 510 : -1;
}

static int techIdSame(const char *name) {
    return strcmp(name, "x:one") == 0 ? 500 : strcmp(name, "x:two") == 0 ? 501 : -1;
}

/* cards of mods (EXTK): "x:ca" is id 320 in the run that saves and 330 later; "x:cb" has no mod later */
static const char *cardName(int id) {
    return id == 320 ? "x:ca" : id == 321 ? "x:cb" : NULL;
}
static int cardIdSame(const char *name) {
    return !strcmp(name, "x:ca") ? 320 : !strcmp(name, "x:cb") ? 321 : -1;
}
static int cardIdLater(const char *name) {
    return !strcmp(name, "x:ca") ? 330 : -1;
}
#define DECK_AT(d, k) (offsetof(GameState, decks) + (size_t)(d) * sizeof(Deck) + offsetof(Deck, cards) + (size_t)(k) * 2)
static void putDeckCard(unsigned char *g, int d, int k, int id) {
    g[DECK_AT(d, k)] = (unsigned char)id;
    g[DECK_AT(d, k) + 1] = (unsigned char)(id >> 8);
}
static int deckCard(const unsigned char *g, int d, int k) {
    return (short)(g[DECK_AT(d, k)] | (g[DECK_AT(d, k) + 1] << 8));
}

static void restart(void) {
    hostMemcardReset();
    MemCardInit(0);
    MemCardStart();
}

static void virtualCardTests(const char *root, const char *pref) {
    char mcd[1100], saves[1100], marker[1100], p[1300], before[CARD_SIZE];
    unsigned char g0[0x2700], g2[0x2700], g1[0x2700], info[0x100], out[0x2700], hdr[SEC];
    FILE *f;
    int r;

    (void)root;
    pcExtStateSetNameHooks(extName, extId);
    snprintf(mcd, sizeof(mcd), "%simport/card.mcd", pref);
    snprintf(saves, sizeof(saves), "%ssaves/default", pref);
    snprintf(marker, sizeof(marker), "%simport/imported.txt", pref);
    snprintf(p, sizeof(p), "mkdir -p '%simport'", pref);
    system(p);
    makeGame(g0, 1);
    makeGame(g1, 3);
    makeGame(g2, 2);
    makeInfo(info, 2);
    buildCard(mcd, g0, g2, info);
    f = fopen(mcd, "rb");
    fread(before, 1, CARD_SIZE, f);
    fclose(f);
    unsetenv("DW3_MEMCARD");
    unsetenv("DW3_SAVES_DIR");
    /* the old places are not read: a memcard0.mcd next to the settings (the old import source) is ignored */
    remove(mcd); /* import/ is empty for this check */
    snprintf(p, sizeof(p), "%smemcard0.mcd", pref);
    buildCard(p, g0, g2, info);
    restart();
    MemCardAccept(0);
    syncResult();
    snprintf(p, sizeof(p), "%s/slot0.dw3save", saves);
    expect(sizeOf(p) < 0, "legacy: a memcard0.mcd at the root is not imported");
    snprintf(p, sizeof(p), "%smemcard0.mcd", pref);
    remove(p);
    snprintf(p, sizeof(p), "rm -rf '%s'", saves);
    system(p);
    remove(marker);
    buildCard(mcd, g0, g2, info);

    /* 1. first start with a card and no saves: the DW3 saves are imported, one way */
    restart();
    expect(MemCardAccept(0) == 1 && syncResult() == ERR_NEW_CARD, "card: the first accept is a new card");
    expect(MemCardAccept(0) == 1 && syncResult() == ERR_NONE, "card: then it is ready");
    snprintf(p, sizeof(p), "%s/slot0.dw3save", saves);
    expect(sizeOf(p) > 0x26C4, "import: slot 0 became a dw3save");
    snprintf(p, sizeof(p), "%s/slot1.dw3save", saves);
    expect(sizeOf(p) < 0, "import: the empty slot 1 was not written");
    snprintf(p, sizeof(p), "%s/slot2.dw3save", saves);
    expect(sizeOf(p) > 0x26C4, "import: slot 2 became a dw3save");
    expect(sizeOf(marker) > 0, "import: the record is written in import/");
    snprintf(p, sizeof(p), "%s/imported.txt", saves);
    expect(sizeOf(p) < 0, "import: and not in saves/");
    {
        static unsigned char after[CARD_SIZE];

        f = fopen(mcd, "rb");
        fread(after, 1, CARD_SIZE, f);
        fclose(f);
        expect(memcmp(before, after, CARD_SIZE) == 0, "import: the .mcd is untouched");
    }

    /* 2. the game's screens see the same card: listing, header, info, data */
    {
        HostDirEntry dir[4];
        int files = -1;

        expect(MemCardGetDirentry(0, "*", dir, &files, 0, 15) == ERR_NONE && files == 1, "list: one DW3 file");
        expect(files == 1 && strncmp(dir[0].name, NAME_EUR, 20) == 0 && dir[0].size == 0x8000, "list: its name and 4 blocks");
    }
    expect(rd(NAME_EUR, hdr, 0, SEC) == ERR_NONE && hdr[0] == 'S' && hdr[2] == 0x11, "read: the card header");
    expect(rd(NAME_EUR, hdr, SEC, SEC) == ERR_NONE && hdr[0] == 0x5A, "read: the icon frame");
    expect(rdSection(NAME_EUR, out, INFO_OFS, 0x100) == ERR_NONE && memcmp(out, info, 0x100) == 0, "read: the info section as the card had it");
    expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && memcmp(out, g0, 0x26C4) == 0, "read: slot 0 data");
    expect(rdSection(NAME_EUR, out, DATA_OFS(2), 0x2700) == ERR_NONE && memcmp(out, g2, 0x26C4) == 0, "read: slot 2 data");
    expect(rdSection(NAME_EUR, out, DATA_OFS(1), 0x2700) == ERR_NONE && out[0] == 0 && out[0x100] == 0, "read: the empty slot 1 is zeros");
    expect(rd("OTHERGAME-0000", out, 0, SEC) == ERR_NO_FILE, "read: another game's file does not exist");
    expect(rd(NAME_EUR, out, 0x7F00, SEC) == ERR_NO_FILE, "read: past the file's end");

    /* 3. the .mcd stays; a restart does not import again, and loses nothing */
    restart();
    remove(mcd); /* the importer's source is gone: the saves stay */
    expect(MemCardAccept(0) == 1 && syncResult() == ERR_NEW_CARD, "restart: accept");
    expect(rdSection(NAME_EUR, out, DATA_OFS(2), 0x2700) == ERR_NONE && memcmp(out, g2, 0x26C4) == 0, "restart: slot 2 loads from its dw3save");
    buildCard(mcd, g0, g0, info);
    restart();
    MemCardAccept(0);
    syncResult();
    expect(rdSection(NAME_EUR, out, DATA_OFS(2), 0x2700) == ERR_NONE && memcmp(out, g2, 0x26C4) == 0, "restart: a changed .mcd is not imported again");

    /* 4. saving slot 1 (the game writes the info, then the data) with an extension item */
    *pcItemCountRef(600) = 9;
    makeInfo(info, 1);
    expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE, "save: info written");
    snprintf(p, sizeof(p), "%s/slot1.dw3save", saves);
    expect(sizeOf(p) < 0, "save: nothing is committed before the data arrives");
    expect(wrSection(NAME_EUR, g1, DATA_OFS(1), 0x2700) == ERR_NONE, "save: data written");
    expect(sizeOf(p) > 0x26C4, "save: slot 1 is a dw3save now");
    snprintf(p, sizeof(p), "%s/slot1.dw3save.tmp", saves);
    expect(sizeOf(p) < 0, "save: no temporary file is left");
    snprintf(p, sizeof(p), "%s/view/slot1.json", saves);
    expect(sizeOf(p) > 0, "save: the readable view is in view/");
    snprintf(p, sizeof(p), "%s/slot1.json", saves);
    expect(sizeOf(p) < 0, "save: and not beside the save");

    /* 5. a new process sees the new slot, the newest info, and gets the extension back on load */
    pcExtStateReset();
    restart();
    MemCardAccept(0);
    syncResult();
    expect(rdSection(NAME_EUR, out, INFO_OFS, 0x100) == ERR_NONE && memcmp(out, info, 0x100) == 0, "reload: the info is the last save's");
    expect(*pcItemCountRef(600) == 0, "reload: the extension is empty before the load");
    expect(rdSection(NAME_EUR, out, DATA_OFS(1), 0x2700) == ERR_NONE && memcmp(out, g1, 0x26C4) == 0, "reload: slot 1 data");
    expect(*pcItemCountRef(600) == 9, "reload: the load brings the extension item back");
    expect(rdSection(NAME_EUR, out, DATA_OFS(2), 0x2700) == ERR_NONE && *pcItemCountRef(600) == 0, "reload: another slot has no such item");

    /* 6. a torn slot file: the previous copy loads (and is logged); both damaged: the slot reads empty */
    pcExtStateReset();
    makeGame(g1, 9);
    wrSection(NAME_EUR, g1, DATA_OFS(1), 0x2700); /* second generation of slot 1 */
    snprintf(p, sizeof(p), "%s/slot1.dw3save", saves);
    {
        long sz = sizeOf(p);

        snprintf(before, sizeof(before), "truncate -s %ld '%s'", sz / 2, p);
        system(before);
    }
    restart();
    MemCardAccept(0);
    syncResult();
    makeGame(out, 3);
    r = rdSection(NAME_EUR, out, DATA_OFS(1), 0x2700);
    expect(r == ERR_NONE, "torn: the read still answers");
    {
        unsigned char old[0x2700];

        makeGame(old, 3);
        expect(memcmp(out, old, 0x26C4) == 0, "torn: the previous generation is served");
    }
    snprintf(p, sizeof(p), "%s/slot1.dw3save.bak", saves);
    remove(p);
    restart();
    MemCardAccept(0);
    syncResult();
    expect(rdSection(NAME_EUR, out, DATA_OFS(1), 0x2700) == ERR_NONE && out[0] == 0 && out[0x200] == 0, "torn: with no good copy the slot is empty");

    /* 7. an explicit folder: DW3_SAVES_DIR wins over the card-derived one */
    {
        char own[1100];

        snprintf(own, sizeof(own), "%s/own_saves", root);
        setenv("DW3_SAVES_DIR", own, 1);
        remove(marker);
        buildCard(mcd, g0, g2, info);
        restart();
        MemCardAccept(0);
        syncResult();
        snprintf(p, sizeof(p), "%s/slot0.dw3save", own);
        expect(sizeOf(p) > 0x26C4, "env: DW3_SAVES_DIR holds the imported slots");
        unsetenv("DW3_SAVES_DIR");
    }

    /* 8. no card at all: an empty virtual card; the game creates its file, then saves */
    {
        char own[1100];

        snprintf(own, sizeof(own), "%s/fresh_saves", root);
        setenv("DW3_SAVES_DIR", own, 1);
        snprintf(mcd, sizeof(mcd), "%s/override/none.mcd", root);
        setenv("DW3_MEMCARD", mcd, 1);
        restart();
        MemCardAccept(0);
        syncResult();
        expect(rd(NAME_EUR, out, 0, SEC) == ERR_NO_FILE, "fresh: no save file yet");
        {
            HostDirEntry dir[2];
            int files = -1;

            expect(MemCardGetDirentry(0, "*", dir, &files, 0, 15) == ERR_NONE && files == 0, "fresh: empty listing");
        }
        expect(MemCardCreateFile(0, NAME_EUR, 4) == ERR_NONE, "fresh: the game creates its file");
        expect(MemCardCreateFile(0, NAME_EUR, 4) == ERR_EXISTS, "fresh: not twice");
        memset(hdr, 0, sizeof(hdr));
        hdr[0] = 'S'; hdr[1] = 'C'; hdr[2] = 0x11; hdr[3] = 4;
        expect(wr(NAME_EUR, hdr, 0, SEC) == ERR_NONE, "fresh: header");
        memset(hdr, 0x33, sizeof(hdr));
        expect(wr(NAME_EUR, hdr, SEC, SEC) == ERR_NONE, "fresh: icon");
        makeInfo(info, 0);
        memset(info + 8, 0, 0xD4 - 8);
        expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE, "fresh: empty info");
        restart();
        MemCardAccept(0);
        syncResult();
        expect(rd(NAME_EUR, out, 0, SEC) == ERR_NONE && out[0] == 'S', "fresh: the created file survives a restart");
        expect(rdSection(NAME_EUR, out, INFO_OFS, 0x100) == ERR_NONE && memcmp(out, info, 0x100) == 0, "fresh: and its empty info");
        expect(MemCardFormat(0) == ERR_NONE, "fresh: formatting is a harmless no-op");
        expect(rd(NAME_EUR, out, 0, SEC) == ERR_NONE, "fresh: formatting kept the file");

        /* 9. techniques of mods: the game block keeps their ids; the file adds their names and a later run with other ids
           (or without the mod) gets the block remapped, flags kept, the checksum byte following */
        {
            unsigned char gt[0x2700], x = 0;
            size_t i;
            char sp[1300];
            long sz;
            FILE *sf;
            static unsigned char file[0x4000];

            memset(gt, 0, sizeof gt);
            putSkill(gt, 0, 0, 0, 500 | SKILL_KNOWN);
            putSkill(gt, 1, 2, 3, 501 | (unsigned)SKILL_LAST);
            putSkill(gt, 2, 0, 1, 17 | SKILL_MARKED);
            for (i = 4; i < 0x26C4; i++) x ^= gt[i];
            gt[0] = x;
            pcExtStateSetTechHooks(techName, techIdSame);
            makeInfo(info, 4);
            expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE, "tech: info written");
            expect(wrSection(NAME_EUR, gt, DATA_OFS(0), 0x2700) == ERR_NONE, "tech: data written");
            snprintf(sp, sizeof(sp), "%s/slot0.dw3save", own);
            sf = fopen(sp, "rb");
            sz = sf != NULL ? (long)fread(file, 1, sizeof file, sf) : -1;
            if (sf != NULL) fclose(sf);
            expect(sz > 0x26C4 && memmem(file, (size_t)sz, "EXTT", 4) != NULL && memmem(file, (size_t)sz, "x:one", 5) != NULL &&
                       memmem(file, (size_t)sz, "x:two", 5) != NULL,
                   "tech: the slot file carries the names in an EXTT section");

            restart(); /* the same ids: nothing changes */
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && memcmp(out, gt, 0x26C4) == 0, "tech: the same mods give the block back");

            pcExtStateSetTechHooks(NULL, techIdLater);
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE, "tech: the later read answers");
            expect(skillOf(out, 0, 0, 0) == (510u | SKILL_KNOWN), "tech: x:one has its new id and keeps its flag");
            expect(skillOf(out, 1, 2, 3) == 0, "tech: x:two, whose mod is gone, is emptied");
            expect(skillOf(out, 2, 0, 1) == (17u | SKILL_MARKED), "tech: an original technique stays");
            for (x = 0, i = 4; i < 0x26C4; i++) x ^= out[i];
            expect(out[0] == x, "tech: the checksum byte follows the change");
            pcExtStateSetTechHooks(NULL, NULL);
        }

        /* 9b. cards of mods: counts and seen flags by name (EXTK), the names of the new ids in the decks; a later run with other ids
           (or without the mod) gets the counts moved and the decks remapped, a lost card's slot becomes the starter deck's */
        {
            unsigned char gc[0x2700], x = 0;
            size_t i;
            char sp[1300];
            long sz;
            FILE *sf;
            static unsigned char file[0x4000];
            static const int starter[DECK_SIZE] = {7, 8, 9, 10, 11, 12};

            memset(gc, 0, sizeof gc);
            putDeckCard(gc, 0, 0, 320);
            putDeckCard(gc, 0, 1, 321);
            putDeckCard(gc, 1, 5, 12);
            for (i = 4; i < 0x26C4; i++) x ^= gc[i];
            gc[0] = x;
            pcExtStateSetCardHooks(cardName, cardIdSame);
            pcExtStateSetCardFallback(starter);
            pcExtStateReset();
            PC_CARD_COUNT(320) = 4;
            PC_CARD_SEEN(320) = 1;
            makeInfo(info, 5);
            expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE, "cards: info written");
            expect(wrSection(NAME_EUR, gc, DATA_OFS(0), 0x2700) == ERR_NONE, "cards: data written");
            snprintf(sp, sizeof(sp), "%s/slot0.dw3save", own);
            sf = fopen(sp, "rb");
            sz = sf != NULL ? (long)fread(file, 1, sizeof file, sf) : -1;
            if (sf != NULL) fclose(sf);
            expect(sz > 0x26C4 && memmem(file, (size_t)sz, "EXTK", 4) != NULL && memmem(file, (size_t)sz, "x:ca", 4) != NULL &&
                       memmem(file, (size_t)sz, "x:cb", 4) != NULL,
                   "cards: the slot file carries the names in an EXTK section");

            pcExtStateReset();
            restart(); /* the same ids: the block is as saved and the count and seen flag come back */
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && memcmp(out, gc, 0x26C4) == 0, "cards: the same mods give the block back");
            expect(PC_CARD_COUNT(320) == 4 && PC_CARD_SEEN(320) == 1 && PC_CARD_COUNT(321) == 0, "cards: the count and the seen flag come back by name");

            pcExtStateSetCardHooks(NULL, cardIdLater);
            pcExtStateReset();
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE, "cards: the later read answers");
            expect(deckCard(out, 0, 0) == 330, "cards: x:ca has its new id in the deck");
            expect(deckCard(out, 0, 1) == starter[1] && deckCard(out, 1, 5) == 12, "cards: x:cb, whose mod is gone, becomes the starter card; an original card stays");
            expect(PC_CARD_COUNT(330) == 4 && PC_CARD_SEEN(330) == 1 && PC_CARD_COUNT(320) == 0, "cards: the count follows the name to its new id");
            for (x = 0, i = 4; i < 0x26C4; i++) x ^= out[i];
            expect(out[0] == x, "cards: the checksum byte follows the change");

            pcExtStateSetCardHooks(NULL, NULL); /* no mod at all: the save still loads, the cards are gone */
            pcExtStateReset();
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && deckCard(out, 0, 0) == starter[0], "cards: without the mod the save loads and its decks read as original cards");
            expect(PC_CARD_COUNT(330) == 0, "cards: and its counts are empty");
            pcExtStateSetCardHooks(NULL, NULL);
            pcExtStateSetCardFallback(NULL);
            pcExtStateReset();
        }

        /* 9c. named variables of content mods (stage flags and counters, EXTV): saved by name; a load keeps EVERY name, also one no installed mod
           uses (disable a mod, enable it again: its progress is there); a save written with none has no section and loads with none */
        {
            unsigned char gv[0x2700], x = 0;
            size_t i;
            char sp[1300];
            long sz;
            FILE *sf;
            static unsigned char file[0x4000];

            memset(gv, 0, sizeof gv);
            for (i = 4; i < 0x26C4; i++) x ^= gv[i];
            gv[0] = x;
            pcExtStateReset();
            pcExtVarSet("x:met", 1);
            pcExtVarSet("x:count", 5);
            pcExtVarSet("gone:thing", 9);
            makeInfo(info, 7);
            expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE, "vars: info written");
            expect(wrSection(NAME_EUR, gv, DATA_OFS(0), 0x2700) == ERR_NONE, "vars: data written");
            snprintf(sp, sizeof(sp), "%s/slot0.dw3save", own);
            sf = fopen(sp, "rb");
            sz = sf != NULL ? (long)fread(file, 1, sizeof file, sf) : -1;
            if (sf != NULL) fclose(sf);
            expect(sz > 0x26C4 && memmem(file, (size_t)sz, "EXTV", 4) != NULL && memmem(file, (size_t)sz, "x:count", 7) != NULL &&
                       memmem(file, (size_t)sz, "gone:thing", 10) != NULL,
                   "vars: the slot file carries the names in an EXTV section");

            pcExtStateReset();
            expect(pcExtVarGet("x:met") == 0, "vars: a reset forgets them");
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && memcmp(out, gv, 0x26C4) == 0, "vars: the block is as saved");
            expect(pcExtVarGet("x:met") == 1 && pcExtVarGet("x:count") == 5 && pcExtVarGet("gone:thing") == 9 && pcExtVarCount() == 3,
                   "vars: every name comes back, the one no mod uses included");

            /* a game saved with no variable: no section, and the read leaves none (the run before had some) */
            pcExtStateReset();
            makeInfo(info, 8);
            expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE && wrSection(NAME_EUR, gv, DATA_OFS(0), 0x2700) == ERR_NONE,
                   "vars: a save with none is written");
            sf = fopen(sp, "rb");
            sz = sf != NULL ? (long)fread(file, 1, sizeof file, sf) : -1;
            if (sf != NULL) fclose(sf);
            expect(sz > 0x26C4 && memmem(file, (size_t)sz, "EXTV", 4) == NULL, "vars: and it has no EXTV section");
            pcExtVarSet("x:met", 1);
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && pcExtVarCount() == 0 && pcExtVarGet("x:met") == 0,
                   "vars: an older save loads with no variable");
            pcExtStateReset();
        }

        /* 10. new partner forms: the file adds their names and base forms (EXTD); a later run renumbers them, and a form
           whose mod is gone becomes its base form */
        {
            unsigned char gt[0x2700], x = 0;
            size_t i;
            char sp[1300];
            long sz;
            FILE *sf;
            static unsigned char file[0x4000];

            memset(gt, 0, sizeof gt);
            putForm(gt, 0, 0, 3);
            putForm(gt, 0, 1, 471);
            putForm(gt, 1, 0, 472);
            for (i = 4; i < 0x26C4; i++) x ^= gt[i];
            gt[0] = x;
            pcExtStateSetDigimonHooks(formName, formIdSame, formBase);
            makeInfo(info, 4);
            expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE, "form: info written");
            expect(wrSection(NAME_EUR, gt, DATA_OFS(0), 0x2700) == ERR_NONE, "form: data written");
            snprintf(sp, sizeof(sp), "%s/slot0.dw3save", own);
            sf = fopen(sp, "rb");
            sz = sf != NULL ? (long)fread(file, 1, sizeof file, sf) : -1;
            if (sf != NULL) fclose(sf);
            expect(sz > 0x26C4 && memmem(file, (size_t)sz, "EXTD", 4) != NULL && memmem(file, (size_t)sz, "x:fire", 6) != NULL,
                   "form: the slot file carries the names in an EXTD section");
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && memcmp(out, gt, 0x26C4) == 0,
                   "form: the same mods give the block back");
            pcExtStateSetDigimonHooks(NULL, formIdLater, NULL);
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE, "form: the later read answers");
            expect(formOf(out, 0, 0) == 3 && formOf(out, 0, 1) == 480, "form: x:fire has its new id, an original form stays");
            expect(formOf(out, 1, 0) == 6, "form: x:gone, whose mod is gone, is its base form");
            for (x = 0, i = 4; i < 0x26C4; i++) x ^= out[i];
            expect(out[0] == x, "form: the checksum byte follows the change");
            pcExtStateSetDigimonHooks(NULL, NULL, NULL);
        }

        /* 11. partner entries beyond the original 44 (EXTP): the block keeps the first 44 as ever; the file adds the rest by
           name; a read brings them back (and a read of a save without them leaves none) */
        {
            unsigned char gt[0x2700], x = 0;
            size_t i;
            char sp[1300];
            long sz;
            FILE *sf;
            static unsigned char file[0x4000];

            memset(gt, 0, sizeof gt);
            putForm(gt, 0, 0, 3);
            for (i = 4; i < 0x26C4; i++) x ^= gt[i];
            gt[0] = x;
            pcExtStateSetDigimonHooks(formName, formIdSame, formBase);
            pcPartnerEntriesReset();
            pcPartnerEntryGrow(0);
            pcPartnerEntryGrow(0);
            pcPartnerEntryGrow(5);
            {
                GameState g;
                PartnerEntry *e;

                memset(&g, 0, sizeof g);
                e = pcPartnerEntryRef(&g.partners[0].info, 0, 44);
                e->id = 471;
                e->level = 7;
                e->exp = 4321;
                e = pcPartnerEntryRef(&g.partners[0].info, 0, 45);
                e->id = 12;
                e->level = 2;
                e = pcPartnerEntryRef(&g.partners[5].info, 5, 44);
                e->id = 9;
                e->level = 4;
            }
            makeInfo(info, 4);
            expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE, "entries: info written");
            expect(wrSection(NAME_EUR, gt, DATA_OFS(0), 0x2700) == ERR_NONE, "entries: data written");
            snprintf(sp, sizeof(sp), "%s/slot0.dw3save", own);
            sf = fopen(sp, "rb");
            sz = sf != NULL ? (long)fread(file, 1, sizeof file, sf) : -1;
            if (sf != NULL) fclose(sf);
            expect(sz > 0x26C4 && memmem(file, (size_t)sz, "EXTP", 4) != NULL && memmem(file, (size_t)sz, "x:fire", 6) != NULL,
                   "entries: the slot file carries the stored entries in an EXTP section");
            pcPartnerEntriesReset();
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && memcmp(out, gt, 0x26C4) == 0,
                   "entries: the block is the same");
            {
                GameState g;

                memset(&g, 0, sizeof g);
                expect(pcPartnerEntryLimit(0) == 46 && pcPartnerEntryLimit(5) == 45 && pcPartnerEntryLimit(1) == 44,
                       "entries: the read brings the stored entries back");
                expect(pcPartnerEntryRef(&g.partners[0].info, 0, 44)->id == 471 && pcPartnerEntryRef(&g.partners[0].info, 0, 44)->exp == 4321 &&
                           pcPartnerEntryRef(&g.partners[0].info, 0, 45)->id == 12 && pcPartnerEntryRef(&g.partners[5].info, 5, 44)->level == 4,
                       "entries: with their form, level and experience");
            }
            /* the same slot read with another id for the form: renumbered by name */
            pcExtStateSetDigimonHooks(NULL, formIdLater, NULL);
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE, "entries: the later read answers");
            {
                GameState g;

                memset(&g, 0, sizeof g);
                expect(pcPartnerEntryRef(&g.partners[0].info, 0, 44)->id == 480, "entries: x:fire has its new id");
            }
            /* a save made without stored entries has no section, and a read of it leaves none */
            pcPartnerEntriesReset();
            pcExtStateSetDigimonHooks(formName, formIdSame, formBase);
            expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE && wrSection(NAME_EUR, gt, DATA_OFS(0), 0x2700) == ERR_NONE,
                   "entries: a save without stored entries is written");
            sf = fopen(sp, "rb");
            sz = sf != NULL ? (long)fread(file, 1, sizeof file, sf) : -1;
            if (sf != NULL) fclose(sf);
            expect(sz > 0x26C4 && memmem(file, (size_t)sz, "EXTP", 4) == NULL, "entries: no EXTP section when nothing is stored");
            pcPartnerEntryGrow(0);
            restart();
            MemCardAccept(0);
            syncResult();
            expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && pcPartnerEntryLimit(0) == 44,
                   "entries: reading it leaves no stored entries");
            pcExtStateSetDigimonHooks(NULL, NULL, NULL);
        }
        unsetenv("DW3_SAVES_DIR");
    }
    unsetenv("DW3_MEMCARD");
}

/* import/ takes every *.mcd by name until one gives saves; the record is written once */
static void importFolderTests(const char *pref) {
    unsigned char g0[0x2700], g2[0x2700], info[0x100];
    char dir[1100], p[1300], saves[1100], marker[1100];
    static unsigned char blank[CARD_SIZE];
    FILE *f;

    snprintf(dir, sizeof(dir), "%simport", pref);
    snprintf(saves, sizeof(saves), "%ssaves/default", pref);
    snprintf(marker, sizeof(marker), "%s/imported.txt", dir);
    snprintf(p, sizeof(p), "rm -rf '%s' '%s'", dir, saves);
    system(p);
    snprintf(p, sizeof(p), "mkdir -p '%s'", dir);
    system(p);
    unsetenv("DW3_MEMCARD");
    unsetenv("DW3_SAVES_DIR");
    makeGame(g0, 5);
    makeGame(g2, 6);
    makeInfo(info, 2);
    blank[0] = 'M';
    blank[1] = 'C';
    snprintf(p, sizeof(p), "%s/a.mcd", dir); /* a card with no DW3 file comes first by name ... */
    f = fopen(p, "wb");
    fwrite(blank, 1, sizeof(blank), f);
    fclose(f);
    snprintf(p, sizeof(p), "%s/B.MCD", dir); /* ... the good one is next, in any case */
    buildCard(p, g0, g2, info);
    restart();
    MemCardAccept(0);
    syncResult();
    snprintf(p, sizeof(p), "%s/slot2.dw3save", saves);
    expect(sizeOf(p) > 0x26C4, "import/: the second card (B.MCD) gave its saves");
    expect(sizeOf(marker) > 0, "import/: the record is there");
}


/* ---- M2 / R4: the player's own import (formats, validation, backup, the search) ---- */

/* a card file wrapped as another tool stores it: `head` bytes of header and then the raw card */
static void wrapCard(const char *rawPath, const char *outPath, const unsigned char *head, int magicLen, int headLen) {
    static unsigned char raw[CARD_SIZE];
    FILE *f = fopen(rawPath, "rb");

    fread(raw, 1, sizeof(raw), f);
    fclose(f);
    f = fopen(outPath, "wb");
    if (headLen > 0) {
        unsigned char pad[0xF40];

        memset(pad, 0, sizeof(pad));
        memcpy(pad, head, (size_t)magicLen);
        fwrite(pad, 1, (size_t)headLen, f);
    }
    fwrite(raw, 1, sizeof(raw), f);
    fclose(f);
}

static int fileSame(const char *a, const char *b) {
    unsigned char x[0x3000], y[0x3000];
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    size_t na = 0, nb = 1;

    if (fa != NULL && fb != NULL) {
        na = fread(x, 1, sizeof(x), fa);
        nb = fread(y, 1, sizeof(y), fb);
    }
    if (fa != NULL) fclose(fa);
    if (fb != NULL) fclose(fb);
    return fa != NULL && fb != NULL && na == nb && na > 0 && memcmp(x, y, na) == 0;
}

static int hasBackup(const char *saves, const char *leaf) {
    char cmd[1500];

    snprintf(cmd, sizeof(cmd), "ls '%s'/backup/*/%s >/dev/null 2>&1", saves, leaf);
    return system(cmd) == 0;
}

static void playerImportTests(const char *pref) {
    unsigned char gA0[0x2700], gA2[0x2700], gB0[0x2700], zero[0x2700], info[0x100], out[0x2700];
    char dir[1100], emu[1100], saves[1100], p[1400], q[1400], msg[256], found[8][PP_CAP];
    static unsigned char junk[CARD_SIZE];
    unsigned char gme[16] = "123-456-STD", vmp[8] = {0, 'P', 'M', 'V'};
    FILE *f;
    int n;

    snprintf(dir, sizeof(dir), "%simport", pref);
    snprintf(emu, sizeof(emu), "%semu", pref);
    snprintf(saves, sizeof(saves), "%ssaves/default", pref);
    snprintf(p, sizeof(p), "rm -rf '%s' '%s' '%s'", dir, emu, saves);
    system(p);
    snprintf(p, sizeof(p), "mkdir -p '%s' '%s'", dir, emu);
    system(p);
    unsetenv("DW3_MEMCARD");
    unsetenv("DW3_SAVES_DIR");
    setenv("DW3_EMU_DIRS", emu, 1);
    makeGame(gA0, 11);
    makeGame(gA2, 12);
    makeGame(gB0, 13);
    memset(zero, 0, sizeof(zero));
    makeInfo(info, 2);
    snprintf(p, sizeof(p), "%s/imported.txt", dir); /* the first-start import has already run: only the player's action imports here */
    f = fopen(p, "wb"); fputs("x", f); fclose(f);
    restart();

    /* extensions and the search: only card-looking files, newest first */
    snprintf(p, sizeof(p), "%s/a.mcr", emu);
    buildCard(p, gA0, gA2, info);
    snprintf(q, sizeof(q), "%s/notes.txt", emu);
    f = fopen(q, "wb"); fputs("x", f); fclose(f);
    snprintf(q, sizeof(q), "%s/small.mcd", emu);
    f = fopen(q, "wb"); fputs("tiny", f); fclose(f);
    snprintf(q, sizeof(q), "%s/new.MCD", dir);
    buildCard(q, gB0, zero, info);
    snprintf(q, sizeof(q), "touch -d '2 days ago' '%s/a.mcr'", emu);
    system(q);
    n = cardImageFind(found, 8);
    expect(n == 2, "find: the .mcr and the .MCD, not the .txt nor the tiny file");
    expect(n == 2 && strstr(found[0], "new.MCD") != NULL, "find: the newest card comes first");

    /* a raw card imports: slots 0 and 2, never slot 1, and the game reads them through the card calls */
    snprintf(p, sizeof(p), "%s/a.mcr", emu);
    expect(hostMemcardImportFile(p, msg, sizeof(msg)) == 1, "player import: a raw .mcr imports");
    expect(strstr(msg, "Imported 2 saves") != NULL, "player import: the message counts the saves");
    snprintf(q, sizeof(q), "%s/slot0.dw3save", saves);
    expect(sizeOf(q) > 0x26C4, "player import: slot 0 written");
    snprintf(q, sizeof(q), "%s/slot1.dw3save", saves);
    expect(sizeOf(q) < 0, "player import: slot 1 stays empty");
    snprintf(q, sizeof(q), "%s/slot2.dw3save", saves);
    expect(sizeOf(q) > 0x26C4, "player import: slot 2 written");
    expect(MemCardAccept(0) == 1, "player import: the card answers");
    syncResult();
    memset(out, 0, sizeof(out));
    expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == 0 && memcmp(out, gA0, 0x26C4) == 0, "player import: the game reads slot 0 back");
    expect(!hasBackup(saves, "slot0.dw3save"), "player import: no backup when there were no saves");

    /* a wrong size, a card without the MC header, a card without DW3, and a damaged save are refused and nothing changes */
    snprintf(p, sizeof(p), "%s/slot0.dw3save", saves);
    snprintf(q, sizeof(q), "%s/slot0.keep", pref);
    {
        char cp[1400];

        snprintf(cp, sizeof(cp), "cp '%s' '%s'", p, q);
        system(cp);
    }
    snprintf(p, sizeof(p), "%s/short.mcr", emu);
    f = fopen(p, "wb"); fwrite(junk, 1, 1000, f); fclose(f);
    expect(hostMemcardImportFile(p, msg, sizeof(msg)) == 0 && strstr(msg, "not a PS1") != NULL, "refused: wrong size says it is not a card");
    snprintf(p, sizeof(p), "%s/zeros.mcr", emu);
    f = fopen(p, "wb"); fwrite(junk, 1, sizeof(junk), f); fclose(f);
    expect(hostMemcardImportFile(p, msg, sizeof(msg)) == 0 && strstr(msg, "not a PS1") != NULL, "refused: 128 KB without the MC header");
    snprintf(p, sizeof(p), "%s/blank.mcr", emu);
    junk[0] = 'M'; junk[1] = 'C';
    f = fopen(p, "wb"); fwrite(junk, 1, sizeof(junk), f); fclose(f);
    expect(hostMemcardImportFile(p, msg, sizeof(msg)) == 0 && strstr(msg, "No Digimon World 3 save") != NULL, "refused: a card with no DW3 file");
    snprintf(p, sizeof(p), "%s/missing.mcr", emu);
    expect(hostMemcardImportFile(p, msg, sizeof(msg)) == 0 && strstr(msg, "not found") != NULL, "refused: a missing file");
    {
        unsigned char bad[0x2700];

        makeGame(bad, 14);
        bad[10] ^= 0x55; /* the checksum no longer matches */
        snprintf(p, sizeof(p), "%s/bad.mcr", emu);
        buildCard(p, bad, bad, info);
        expect(hostMemcardImportFile(p, msg, sizeof(msg)) == 0 && strstr(msg, "damaged") != NULL, "refused: every save fails its checksum");
    }
    snprintf(p, sizeof(p), "%s/slot0.dw3save", saves);
    expect(fileSame(p, q), "refused: the saves are byte for byte as before");
    expect(!hasBackup(saves, "slot0.dw3save"), "refused: no backup was made for nothing");
    memset(out, 0, sizeof(out));
    expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == 0 && memcmp(out, gA0, 0x26C4) == 0, "refused: the live card still reads the old slot 0");
    remove(q);

    /* the .gme and .vmp wrappers are stripped; the import replaces the saves and backs up the old ones */
    snprintf(p, sizeof(p), "%s/new.MCD", dir);
    snprintf(q, sizeof(q), "%s/wrapped.gme", emu);
    wrapCard(p, q, gme, 11, 0xF40);
    expect(hostMemcardImportFile(q, msg, sizeof(msg)) == 1 && strstr(msg, "Imported 1 save ") != NULL, "gme: the DexDrive header is stripped");
    expect(hasBackup(saves, "slot0.dw3save") && hasBackup(saves, "slot2.dw3save") && hasBackup(saves, "card.dw3card"), "backup: the previous saves were copied");
    snprintf(q, sizeof(q), "%s/slot2.dw3save", saves);
    expect(sizeOf(q) < 0, "replace: slot 2 is gone, the new card has none (it is in the backup)");
    memset(out, 0, sizeof(out));
    expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == 0 && memcmp(out, gB0, 0x26C4) == 0, "gme: the game reads the new slot 0");
    snprintf(q, sizeof(q), "%s/wrapped.vmp", emu);
    snprintf(p, sizeof(p), "%s/a.mcr", emu);
    wrapCard(p, q, vmp, 4, 0x80);
    expect(hostMemcardImportFile(q, msg, sizeof(msg)) == 1 && strstr(msg, "Imported 2 saves") != NULL, "vmp: the PSP/PS3 header is stripped");
    snprintf(p, sizeof(p), "%s/wrapped.vmp", emu);
    snprintf(q, sizeof(q), "%s/badhead.gme", emu);
    wrapCard(p, q, gme, 11, 0xF40); /* a vmp inside a gme header is not a card */
    expect(hostMemcardImportFile(q, msg, sizeof(msg)) == 0, "gme: a wrapped file of the wrong size is refused");

    /* the action: the newest valid card of the search */
    snprintf(q, sizeof(q), "rm -f '%s'/*.vmp '%s'/*.gme", emu, emu);
    system(q);
    snprintf(p, sizeof(p), "%s/new.MCD", dir);
    snprintf(q, sizeof(q), "touch '%s'", p);
    system(q);
    expect(hostMemcardImportNow(msg, sizeof(msg)) == 1 && strstr(msg, "new.MCD") != NULL, "import now: takes the newest valid card (the drop folder's)");
    snprintf(q, sizeof(q), "rm -f '%s'/* '%s'/*", dir, emu);
    system(q);
    expect(hostMemcardImportNow(msg, sizeof(msg)) == 0 && strstr(msg, "No card found") != NULL && strstr(msg, "import") != NULL,
           "import now: with no card, says where to put it");
    unsetenv("DW3_EMU_DIRS");
}


/* new-stages T4: a game saved in a mod stage holds the stage's fallback in the block and the stage's name in EXTS; a load puts the stage
   back when its mod is installed, and keeps the fallback when it is not */
static int blockWord(const unsigned char *g, size_t off) {
    int v;

    memcpy(&v, g + off, 4);
    return v;
}

static void putWord(unsigned char *g, size_t off, int v) {
    memcpy(g + off, &v, 4);
}

static void modStageSaveTests(const char *pref) {
    char saves[1100], p[1300];
    unsigned char g[0x2700], out[0x2700], info[0x100];
    SaveFile f;
    const SaveSection *sec, *game;
    unsigned char x = 0;
    int i;
    char why[100];
    MsStage *st;

    snprintf(saves, sizeof(saves), "%ssaves/default", pref);
    msReset();
    st = msRegister("zmod:hall", why, sizeof why);
    expect(st != NULL && msSeal() == 1, "stage save: a stage is registered");
    st->fallbackMode = 0x21D;
    st->fallbackX = 111;
    st->fallbackY = 222;
    st->fallbackDir = 3;
    makeGame(g, 5);
    putWord(g, offsetof(GameState, fieldMode), 0x300);
    putWord(g, offsetof(GameState, fieldPos), 7 << 8);
    putWord(g, offsetof(GameState, fieldPos) + 4, 9 << 8);
    putWord(g, offsetof(GameState, fieldDir), 5);
    for (i = 4; i < 0x26C4; i++) x ^= g[i];
    g[0] = x;
    makeInfo(info, 0);
    restart();
    MemCardAccept(0);
    syncResult();
    expect(wrSection(NAME_EUR, info, INFO_OFS, 0x100) == ERR_NONE && wrSection(NAME_EUR, g, DATA_OFS(0), 0x2700) == ERR_NONE, "stage save: written");
    snprintf(p, sizeof(p), "%s/slot0.dw3save", saves);
    memset(&f, 0, sizeof f);
    expect(saveFileRead(p, &f) == SAVEFILE_OK, "stage save: the file reads");
    sec = saveFileFind(&f, "EXTS");
    game = saveFileFind(&f, "GAME");
    expect(sec != NULL && sec->size == 4 + 2 + 9 + 12, "stage save: the file holds an EXTS chunk with the key");
    expect(game != NULL && blockWord(game->data, offsetof(GameState, fieldMode)) == 0x21D &&
               blockWord(game->data, offsetof(GameState, fieldPos)) == 111 << 8 && blockWord(game->data, offsetof(GameState, fieldDir)) == 3,
           "stage save: the block holds the fallback, never a session mode");
    saveFileFree(&f);

    /* a new process, the mod installed: the stage and the real position come back */
    pcExtStateReset();
    restart();
    MemCardAccept(0);
    syncResult();
    expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && blockWord(out, offsetof(GameState, fieldMode)) == 0x300 &&
               blockWord(out, offsetof(GameState, fieldPos)) == 7 << 8 && blockWord(out, offsetof(GameState, fieldPos) + 4) == 9 << 8 &&
               blockWord(out, offsetof(GameState, fieldDir)) == 5,
           "stage load: the stage and the position are back");
    x = 0;
    for (i = 4; i < 0x26C4; i++) x ^= out[i];
    expect(out[0] == x, "stage load: the checksum is the block's own");

    /* the mod is gone: the block keeps the fallback */
    msReset();
    pcExtStateReset();
    restart();
    MemCardAccept(0);
    syncResult();
    expect(rdSection(NAME_EUR, out, DATA_OFS(0), 0x2700) == ERR_NONE && blockWord(out, offsetof(GameState, fieldMode)) == 0x21D &&
               blockWord(out, offsetof(GameState, fieldPos)) == 111 << 8,
           "stage load: with the mod gone the game continues at the fallback");
}

int main(void) {
    char root[512], cmd[700];
    const char *tmp = getenv("TMPDIR");

    snprintf(root, sizeof(root), "%s/memcardtest.%d", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", (int)getpid());
    snprintf(prefDir, sizeof(prefDir), "%s/user/", root);
    unsetenv("DW3_MEMCARD");
    unsetenv("DW3_USER_DIR");
    unsetenv("DW3_IMPORT_DIR");
    unsetenv("DW3_SAVES_DIR");
    unsetenv("XDG_DATA_HOME");
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", prefDir);
    system(cmd);
    portPathsReset();

    /* ---- extended-state T2: the virtual card over dw3save slot files ---- */
    virtualCardTests(root, prefDir);
    importFolderTests(prefDir);
    playerImportTests(prefDir);
    modStageSaveTests(prefDir);

    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    system(cmd);
    if (failures) {
        fprintf(stderr, "memcardtest: %d failure(s)\n", failures);
        return 1;
    }
    printf("memcardtest: OK\n");
    return 0;
}
