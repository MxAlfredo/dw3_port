/*
 * Host test of the original cards by folder (readable-assets A7.4): pc/shim/card_export.c over a synthetic raw disc in memory (CARDPAK0 with three cards, CARDPAK2
 * with two, and files that are not card files); no SDL, no retail bytes. A card is a 12-byte header and a 32x32 8-bit TIM with its own CLUT (image_data.c walks
 * them); the game's number of image i of CARDPAK<A> is A * 64 + i + 1 (cardDrawerSetCard). Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/card_export.h"
#include "shim/exp_core.h"
#include "shim/image_data.h"
#include "shim/png_read.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "cardexptest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

#define CARD_STRIDE 0x62C

/* a card file of `count` cards: card k (0-based in the file, `salt` tells the files apart) has pixel (x, y) = (x + y + k + salt) & 255 and colour i = (i + k + salt) & 31 */
static size_t makePak(uint8_t *out, int count, int salt) {
    int k, i, x, y;

    memset(out, 0, (size_t)count * CARD_STRIDE);
    for (k = 0; k < count; k++) {
        uint8_t *c = out + (size_t)k * CARD_STRIDE, *t = c + 12;

        c[0] = (uint8_t)(k + 1);
        t[0] = 0x10;
        t[4] = 9; /* 8-bit with a CLUT */
        t[8] = 0x0C;
        t[8 + 1] = 2;
        t[8 + 8] = 0; /* x */
        t[8 + 10] = 1; /* ... w = 256, h = 1 */
        t[8 + 9] = 1;
        for (i = 0; i < 256; i++) {
            unsigned v = i == 0 ? 0 : (unsigned)(((i + k + salt) & 31) | (((i * 3) & 31) << 5) | (7u << 10) | 0x8000u);

            t[8 + 12 + i * 2] = (uint8_t)v;
            t[8 + 12 + i * 2 + 1] = (uint8_t)(v >> 8);
        }
        t[8 + 0x20C] = 0x0C; /* the image block: 16 halfwords x 32 */
        t[8 + 0x20C + 1] = 4;
        t[8 + 0x20C + 8] = 16;
        t[8 + 0x20C + 10] = 32;
        for (y = 0; y < 32; y++) {
            for (x = 0; x < 32; x++) t[8 + 0x20C + 12 + y * 32 + x] = (uint8_t)((x + y + k + salt) & 255);
        }
    }
    return (size_t)count * CARD_STRIDE;
}

typedef struct DFile {
    char path[112];
    long lba;
    size_t size;
} DFile;

static DFile gDisc[16];
static int gDiscN;
static uint8_t (*gSect)[2352];
static long gSectN, gNextLba = 4;
static char gData[1024];
static int gReads;

static void discAdd(const char *path, const uint8_t *data, size_t size) {
    DFile *f = &gDisc[gDiscN++];
    long sectors = (long)((size + 2047) / 2048), i;

    snprintf(f->path, sizeof f->path, "%s", path);
    f->lba = gNextLba;
    f->size = size;
    gNextLba += sectors;
    gSect = (uint8_t(*)[2352])realloc(gSect, (size_t)gNextLba * 2352);
    memset(gSect + gSectN, 0, (size_t)(gNextLba - gSectN) * 2352);
    gSectN = gNextLba;
    for (i = 0; i < sectors; i++) {
        size_t n = size - (size_t)i * 2048 < 2048 ? size - (size_t)i * 2048 : 2048;

        memcpy(gSect[f->lba + i] + 24, data + (size_t)i * 2048, n);
    }
}

static int ioList(void *ud, int i, char *path, size_t cap, long *lba, long *size) {
    (void)ud;
    if (i < 0 || i >= gDiscN) return 0;
    snprintf(path, cap, "%s", gDisc[i].path);
    *lba = gDisc[i].lba;
    *size = (long)gDisc[i].size;
    return 1;
}

static int ioRead(void *ud, long lba, unsigned char *frame) {
    (void)ud;
    if (lba < 0 || lba >= gSectN) return 0;
    gReads++;
    memcpy(frame, gSect[lba], 2352);
    return 1;
}

static int exists(const char *rel) {
    char p[1400];

    snprintf(p, sizeof p, "%s/%s", gData, rel);
    return access(p, F_OK) == 0;
}

static uint8_t *slurpFile(const char *rel, size_t *n) {
    char p[1400];
    uint8_t *d = NULL;

    snprintf(p, sizeof p, "%s/%s", gData, rel);
    if (tdReadFile(p, &d, n) != 0) return NULL;
    d = (uint8_t *)realloc(d, *n + 1);
    d[*n] = 0;
    return d;
}

/* the exported face of card `id` is the card (file-local index k, salt) of the synthetic files */
static void checkFace(int id, int k, int salt) {
    char rel[64];
    uint8_t *png;
    size_t n;
    PngImage im;
    char why[100];

    snprintf(rel, sizeof rel, "cards/c%03d/face.png", id);
    png = slurpFile(rel, &n);
    CHECK(png != NULL);
    if (png == NULL) return;
    CHECK(pngReadIndexed(png, n, &im, why, sizeof why) == 0);
    if (im.px != NULL) {
        CHECK(im.w == 32 && im.h == 32 && im.nPal == 256);
        CHECK(im.px[0] == ((k + salt) & 255) && im.px[33] == ((2 + k + salt) & 255) && im.px[31 * 32 + 31] == ((62 + k + salt) & 255));
        CHECK(im.rgb[3 * 5] == (uint8_t)(((5 + k + salt) & 31) << 3 | ((5 + k + salt) & 31) >> 2)); /* the red of palette entry 5 */
        CHECK(im.nAlpha >= 1 && im.alpha[0] == 0);
        pngImageFree(&im);
    }
    free(png);
}

static void testRun(void) {
    static uint8_t pak0[3 * CARD_STRIDE], pak2[2 * CARD_STRIDE];
    MenuExportIo io = {ioList, ioRead, NULL};
    CardExportStats st;
    ExpManifest man;
    char p[1400], rm[1500];
    uint8_t *txt, one[64] = {0};
    size_t n;
    int i;
    static const int kIds[5] = {1, 2, 3, 129, 130};

    n = makePak(pak0, 3, 0);
    discAdd("AAA/DAT/CARD/CARDPAK0.BIN", pak0, n);
    n = makePak(pak2, 2, 40);
    discAdd("AAA/DAT/CARD/CARDPAK2.BIN", pak2, n);
    discAdd("AAA/DAT/CARD/CARDPAK5.BIN", pak0, 3 * CARD_STRIDE); /* not a card file */
    discAdd("AAA/DAT/CARD/CARD_NPC.BIN", pak0, 3 * CARD_STRIDE);
    discAdd("AAA/DAT/CARD/CARDDATA.BIN", pak0, 3 * CARD_STRIDE); /* battle sprites, not card faces */

    snprintf(gData, sizeof gData, "%s/cardexptest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(gData) != NULL);
    cardExportSetup(gData, &io);
    CHECK(cardExportRun("nonsense") == -1);
    CHECK(cardExportRun("130") == 1); /* one card by number: its files only */
    CHECK(exists("cards/c130/face.png") && exists("cards/c130/card.json") && !exists("cards/c129/face.png") && !exists("cards/c001/face.png"));
    CHECK(cardExportRun("c002, 3") == 2 && exists("cards/c002/face.png") && exists("cards/c003/face.png") && !exists("cards/c001/face.png"));
    CHECK(cardExportRun("all") == 5);
    cardExportTotals(&st);
    CHECK(st.cards == 5 && st.failed == 0 && !st.skipped);
    for (i = 0; i < 3; i++) checkFace(kIds[i], i, 0);
    for (i = 3; i < 5; i++) checkFace(kIds[i], i - 3, 40);
    CHECK(!exists("cards/c004/face.png") && !exists("cards/c131/face.png") && !exists("images/card/cardpak0/00.png"));
    txt = slurpFile("cards/c129/card.json", &n);
    CHECK(txt != NULL && strstr((char *)txt, "\"format\": \"cards-card-v1\"") && strstr((char *)txt, "\"name\": \"c129\"") && strstr((char *)txt, "\"id\": \"dw3:card/129\""));
    CHECK(txt != NULL && strstr((char *)txt, "\"number\": 129") && strstr((char *)txt, "\"file\": \"dat/card/cardpak2.bin\", \"index\": 0 }"));
    free(txt);
    snprintf(p, sizeof p, "%s/cards/manifest.json", gData);
    expManOpen(&man, "test", p, "cards");
    CHECK(strcmp(man.m.topic, "cards") == 0 && expManIsCurrent(&man, "cards", CARD_EXPORT_VERSION));
    CHECK(man.m.count == 10 && tdManifestFind(&man.m, "c001/face.png") != NULL && tdManifestFind(&man.m, "c130/card.json") != NULL);
    CHECK(tdKvFind(&man.m.names, "c001") && !strcmp(tdKvFind(&man.m.names, "c001"), "dw3:card/1"));
    CHECK(tdKvFind(&man.m.names, "c130") && !strcmp(tdKvFind(&man.m.names, "c130"), "dw3:card/130") && man.m.names.count == 5);
    expManFree(&man);
    /* up to date: the disc is not read */
    gReads = 0;
    CHECK(cardExportRun("all") == 0);
    cardExportTotals(&st);
    CHECK(st.skipped == 1 && gReads == 0);
    /* a file that went missing is made again */
    snprintf(p, sizeof p, "%s/cards/c001/face.png", gData);
    CHECK(remove(p) == 0);
    CHECK(cardExportRun("all") == 5 && exists("cards/c001/face.png"));
    /* an edited face stays, and the new version goes beside it */
    snprintf(p, sizeof p, "%s/cards/c002/face.png", gData);
    CHECK(tdWriteFileAtomic(p, one, sizeof one) == 0);
    snprintf(p, sizeof p, "%s/cards/manifest.json", gData);
    expManOpen(&man, "test", p, "cards");
    tdKvSet(&man.m.exporter, "cards", "0"); /* a version that is not the current one: the run happens again */
    CHECK(expManSave(&man, 0) == 0);
    expManFree(&man);
    CHECK(cardExportRun("all") == 5);
    txt = slurpFile("cards/c002/face.png", &n);
    CHECK(txt != NULL && n == sizeof one);
    free(txt);
    CHECK(exists("cards/c002/face.png.new"));
    snprintf(rm, sizeof rm, "rm -rf '%s'", gData);
    if (system(rm)) {
    }
    free(gSect);
    gSect = NULL;
}

int main(void) {
    testRun();
    if (failures) {
        fprintf(stdout, "cardexptest: %d FAILED\n", failures);
        return 1;
    }
    fprintf(stdout, "cardexptest: all checks passed\n");
    return 0;
}
