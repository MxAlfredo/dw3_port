/*
 * Host test of the original field sprites by folder (readable-assets A7.5): pc/shim/sprite_export.c over a synthetic raw disc in memory (two sprite archives, the
 * follower of a partner, and files that are not sprites); no SDL, no retail bytes. A sprite archive is a table of offsets, frames (4-bit TIMs with their own 16-colour
 * palette) and a last entry that is the set table (here junk: the sprite has frames and no sets). Exit 0: every check held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shim/exp_core.h"
#include "shim/png_read.h"
#include "shim/sprite_data.h"
#include "shim/sprite_export.h"
#include "shim/text_data.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "sprexptest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static void w16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t v) {
    w16(p, v & 0xFFFF);
    w16(p + 2, v >> 16);
}

/* a 4-bit TIM of iw x ih halfwords with a 16-colour palette whose colour i is (i, salt, 31 - i); the halfword j holds the indices 4 * j .. 4 * j + 3 mod 16, shifted by salt */
static size_t makeFrame(uint8_t *out, int iw, int ih, int salt) {
    size_t at = 8, i;

    w32(out, 0x10);
    w32(out + 4, 8); /* 4-bit with a CLUT */
    w32(out + at, 12 + 32);
    w16(out + at + 4, 0);
    w16(out + at + 6, 480);
    w16(out + at + 8, 16);
    w16(out + at + 10, 1);
    for (i = 0; i < 16; i++) w16(out + at + 12 + 2 * i, i ? (unsigned)(i | (salt & 31) << 5 | (31 - i) << 10) : 0);
    at += 12 + 32;
    w32(out + at, (uint32_t)(12 + 2 * iw * ih));
    w16(out + at + 4, 0);
    w16(out + at + 6, 0);
    w16(out + at + 8, (unsigned)iw);
    w16(out + at + 10, (unsigned)ih);
    for (i = 0; i < (size_t)iw * ih; i++) {
        unsigned a = (unsigned)(4 * i + salt) & 15, b = (a + 1) & 15, c = (a + 2) & 15, d = (a + 3) & 15;

        w16(out + at + 12 + 2 * i, a | b << 4 | c << 8 | d << 12);
    }
    return at + 12 + 2 * (size_t)iw * (size_t)ih;
}

/* an archive of two frames (2x3 and 1x2 halfwords) and a junk last entry */
static size_t makeSprite(uint8_t *out, int salt) {
    uint8_t f0[512], f1[512], junk[20];
    size_t l0 = makeFrame(f0, 2, 3, salt), l1 = makeFrame(f1, 1, 2, salt + 1), at = 12;

    memset(junk, 0xA5, sizeof junk);
    w32(out, 12);
    w32(out + 4, (uint32_t)(at + ((l0 + 3) & ~(size_t)3)));
    w32(out + 8, (uint32_t)(at + ((l0 + 3) & ~(size_t)3) + ((l1 + 3) & ~(size_t)3)));
    memcpy(out + at, f0, l0);
    at += (l0 + 3) & ~(size_t)3;
    memcpy(out + at, f1, l1);
    at += (l1 + 3) & ~(size_t)3;
    memcpy(out + at, junk, sizeof junk);
    return at + sizeof junk;
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

static void testNames(void) {
    char out[96];

    CHECK(sprFolder("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", out, sizeof out) == 0 && !strcmp(out, "p001play"));
    CHECK(sprFolder("AAA/DAT/FIELD/SPRT/P001PLAY.TXT", out, sizeof out) != 0 && sprFolder("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", out, 4) != 0);
    CHECK(sprDiscOf("p001play", out, sizeof out) == 0 && !strcmp(out, "AAA/DAT/FIELD/SPRT/P001PLAY.BIN"));
    CHECK(sprDiscOf("P001PLAY", out, sizeof out) == 0 && !strcmp(out, "AAA/DAT/FIELD/SPRT/P001PLAY.BIN"));
    CHECK(sprDiscOf("p001/play", out, sizeof out) != 0 && sprDiscOf("", out, sizeof out) != 0 && sprDiscOf("..", out, sizeof out) != 0);
    CHECK(sprName("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", 4, NULL, out, sizeof out) == 0 && !strcmp(out, "sprites/p001play/004.png"));
    CHECK(sprName("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", 0, "sprite.json", out, sizeof out) == 0 && !strcmp(out, "sprites/p001play/sprite.json"));
    CHECK(sprPartnerOf("sprites/p006agum/004.png") == 3 && sprPartnerOf("sprites/p001play/004.png") == -1);
}

static void testRun(void) {
    static uint8_t a[4096], b[4096];
    MenuExportIo io = {ioList, ioRead, NULL};
    SpriteExportStats st;
    ExpManifest man;
    char p[1400], rm[1500];
    uint8_t *txt, one[64] = {0};
    size_t n;

    n = makeSprite(a, 0);
    discAdd("AAA/DAT/FIELD/SPRT/P001PLAY.BIN", a, n);
    n = makeSprite(b, 5);
    discAdd("AAA/DAT/FIELD/SPRT/P002NPC0.BIN", b, n);
    discAdd("AAA/DAT/FIELD/SPRT/P006AGUM.BIN", a, n); /* a partner's follower: partners/<name>/field/ holds it */
    discAdd("AAA/DAT/FIELD/FIELDCOM.BIN", a, n);      /* not a sprite file */
    discAdd("AAA/DAT/FIELD/SPRT/P003JUNK.TXT", a, n);

    snprintf(gData, sizeof gData, "%s/sprexptest-XXXXXX", getenv("TMPDIR") && getenv("TMPDIR")[0] ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(gData) != NULL);
    spriteExportSetup(gData, &io);
    CHECK(spriteExportRun("nonsense") == 0); /* names no sprite of the disc */
    CHECK(spriteExportRun("P002NPC0") == 1); /* one sprite by name: its files only */
    CHECK(exists("sprites/p002npc0/000.png") && exists("sprites/p002npc0/sprite.json") && !exists("sprites/p001play/000.png"));
    CHECK(spriteExportRun("all") == 2);
    spriteExportTotals(&st);
    CHECK(st.sprites == 2 && st.frames == 4 && st.failed == 0 && !st.skipped);
    CHECK(exists("sprites/p001play/000.png") && exists("sprites/p001play/001.png") && exists("sprites/p001play/sprite.json") && !exists("sprites/p001play/002.png"));
    CHECK(!exists("sprites/p006agum/000.png") && !exists("sprites/fieldcom/000.png") && !exists("images/field/sprt/p001play/000.png"));
    {
        uint8_t *png = slurpFile("sprites/p001play/000.png", &n);
        PngImage im;
        char why[100];
        int i;

        CHECK(png != NULL && pngReadIndexed(png, n, &im, why, sizeof why) == 0);
        if (png != NULL && im.px != NULL) {
            CHECK(im.w == 8 && im.h == 3 && im.nPal == 16);
            for (i = 0; i < 8; i++) CHECK(im.px[i] == i); /* the first row: halfwords 0 and 1 */
            CHECK(im.rgb[3 * 5] == (uint8_t)(5 << 3 | 5 >> 2) && im.alpha[0] == 0);
            pngImageFree(&im);
        }
        free(png);
    }
    txt = slurpFile("sprites/p001play/sprite.json", &n);
    CHECK(txt != NULL && strstr((char *)txt, "\"format\": \"sprites-sprite-v1\"") && strstr((char *)txt, "\"source\": \"FIELD/SPRT/P001PLAY.BIN\""));
    CHECK(txt != NULL && strstr((char *)txt, "{ \"id\": 0, \"image\": \"000.png\", \"w\": 8, \"h\": 3, \"used\": false }"));
    free(txt);
    snprintf(p, sizeof p, "%s/sprites/manifest.json", gData);
    expManOpen(&man, "test", p, "sprites");
    CHECK(strcmp(man.m.topic, "sprites") == 0 && expManIsCurrent(&man, "sprites", SPRITE_EXPORT_VERSION));
    CHECK(man.m.count == 6 && tdManifestFind(&man.m, "p001play/000.png") != NULL && tdManifestFind(&man.m, "p002npc0/sprite.json") != NULL);
    CHECK(tdKvFind(&man.m.names, "p001play") && !strcmp(tdKvFind(&man.m.names, "p001play"), "dw3:sprite/p001play") && man.m.names.count == 2);
    expManFree(&man);
    /* up to date: the disc is not read */
    gReads = 0;
    CHECK(spriteExportRun("all") == 0);
    spriteExportTotals(&st);
    CHECK(st.skipped == 1 && gReads == 0);
    /* a file that went missing is made again */
    snprintf(p, sizeof p, "%s/sprites/p001play/000.png", gData);
    CHECK(remove(p) == 0);
    CHECK(spriteExportRun("all") == 2 && exists("sprites/p001play/000.png"));
    /* an edited frame stays, and the new version goes beside it */
    snprintf(p, sizeof p, "%s/sprites/p001play/001.png", gData);
    CHECK(tdWriteFileAtomic(p, one, sizeof one) == 0);
    snprintf(p, sizeof p, "%s/sprites/manifest.json", gData);
    expManOpen(&man, "test", p, "sprites");
    tdKvSet(&man.m.exporter, "sprites", "0"); /* a version that is not the current one: the run happens again */
    CHECK(expManSave(&man, 0) == 0);
    expManFree(&man);
    CHECK(spriteExportRun("all") == 2);
    txt = slurpFile("sprites/p001play/001.png", &n);
    CHECK(txt != NULL && n == sizeof one);
    free(txt);
    CHECK(exists("sprites/p001play/001.png.new"));
    snprintf(rm, sizeof rm, "rm -rf '%s'", gData);
    if (system(rm)) {
    }
    free(gSect);
    gSect = NULL;
}

int main(void) {
    testNames();
    testRun();
    if (failures) {
        fprintf(stdout, "sprexptest: %d FAILED\n", failures);
        return 1;
    }
    fprintf(stdout, "sprexptest: all checks passed\n");
    return 0;
}
