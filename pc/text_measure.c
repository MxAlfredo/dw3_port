/*
 * Host harness: measures strings of a disc language file with the game's own
 * text engine.
 *
 *     text_measure <disc.bin> <file index> <string index> [<string index>...]
 *
 * The file is resolved and read by the game's own file layer (FILE_TABLE and
 * FILE_CACHE over CD_READER, milestone 1); getString (src/engine/text/
 * text_tools.c) takes the index'th string out of it, and measureText measures
 * its widest line in style 1 -- the area banner's style, createTextWindow(...,
 * 1, 0x28, 0x44) in src/field/field_mode/banner.c -- with FONT.decode, the
 * game's own font decode. Each line prints the string's bytes (its font codes;
 * nothing of the disc is written down) and its width in pixels.
 *
 * pc/check_text.py compares both against an independent read of the same
 * string and an independent measurement of it.
 */
#include "engine/file.h"
#include "engine/text.h"
#include "shim/host.h"
#include "shim/text_codec.h"
#include "shim/text_reflow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The banner's style, FONT_STYLES[1] (src/engine/text/font.c) */
#define BANNER_STYLE 1

/* The window's text buffers: 0 is the string, 1-5 the work buffers control
   code 5 inserts (measureText recurses into them) */
#define TEXT_BUFFERS 6


/*
 * text_measure --audit <data dir> <layout.json> [<lang folder>]
 *
 * L4a, LOCAL ONLY: counts the strings of <data>/text/<lang folder> (default es-419) that do not fit
 * their box, with the game's own glyph widths (the tables measureText reads). The base is
 * <data>/text/spa; a string equal to the base is skipped (the reflow never touches it). The box of
 * an entry is its file rule's width, or the tight width when the base text fits it. Prints one line
 * per file with a rule: strings that differ from the base, strings with a line wider than the box
 * (inserts at their allowance), strings it cannot parse.
 */
static char *auditRead(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *d;
    long n;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (char *)malloc((size_t)n + 1);
    if (d == NULL || fread(d, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(d);
        return NULL;
    }
    fclose(f);
    d[n] = 0;
    *len = (size_t)n;
    return d;
}

static uint8_t *auditBytes(const char *text, size_t *len)
{
    char *ug = tcUglify(text);
    uint8_t *b = NULL;
    char err[120];
    if (ug == NULL || tcEncode(ug, &b, len, err, sizeof(err)) != 0) {
        free(ug);
        return NULL;
    }
    free(ug);
    return b;
}

static int audit(const char *dataDir, const char *layoutPath, const char *langDir)
{
    TrLayout lay;
    TrOptions opt;
    char err[200];
    char path[1400];
    char *lj;
    size_t ll;
    int totalDiff = 0, totalOver = 0, totalUnparsed = 0, k;
    static const char *const bases[] = {
        "AMTMAP", "ASKMAP", "CARDGM", "CARDNM", "CARDST", "CRDABM", "CRDDEK", "CRDSHP", "DGLABO", "DGNMET",
        "DIGINF", "DIGNAM", "FGTRPT", "HTLNAM", "ITMINF", "ITMNAM", "ITSHOP", "MEMCRD", "MFIGHT", "NAMEDT",
        "NAMEET", "SHPNAM", "SKLINF", "SKLNAM", "STAREA", "STATUS", "STNAME", "SYSTEM", "TALK00", "TALK01",
        "TALK02", "TALK03", "TALK04", "TALK05", "TALK06", "TALK07", "TALK08", "TALK09", "TRAINI", "DMG200",
        "DMG260", "DMG270", "DMG300", "DMG305", "DMG400", "DMG500", "DMG600", "DMG700", "DMG800", "DMG900"};

    lj = auditRead(layoutPath, &ll);
    if (lj == NULL || trLayoutParse(lj, ll, &lay, err, sizeof(err)) != 0) {
        fprintf(stderr, "audit: cannot read the layout %s\n", layoutPath);
        return 1;
    }
    if (!trEngineMetrics(&opt)) {
        fprintf(stderr, "audit: the game's glyph tables are not linked\n");
        return 1;
    }
    printf("allowance: name %d px, number %d px\n", opt.nameAllowance, opt.numberAllowance);
    for (k = 0; k < 50; k++) {
        const TrFileRule *rule = trLayoutFind(&lay, bases[k]);
        char *tj, *bj;
        size_t tl, bl;
        TcJson t, b;
        int i, j, diff = 0, over = 0, unparsed = 0;

        if (rule == NULL || rule->noreflow || rule->maxWidth <= 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/text/%s/SS%s.json", dataDir, langDir, bases[k]);
        tj = auditRead(path, &tl);
        snprintf(path, sizeof(path), "%s/text/spa/SS%s.json", dataDir, bases[k]);
        bj = auditRead(path, &bl);
        if (tj == NULL || bj == NULL) {
            printf("%s: missing json\n", bases[k]);
            free(tj);
            free(bj);
            continue;
        }
        if (tcJsonParse(tj, tl, &t, err, sizeof(err)) != 0 || tcJsonParse(bj, bl, &b, err, sizeof(err)) != 0) {
            printf("%s: bad json (%s)\n", bases[k], err);
            free(tj);
            free(bj);
            continue;
        }
        for (i = 0; i < t.count; i++) {
            const TcJsonString *s = &t.strings[i];
            const TcJsonString *bs = NULL;
            uint8_t *tb, *bb;
            size_t tn, bn;
            TrBudget bud;
            uint8_t *x = NULL;
            size_t xn = 0;
            int w;

            for (j = 0; j < b.count; j++) {
                if (b.strings[j].index == s->index && b.strings[j].part == s->part) {
                    bs = &b.strings[j];
                    break;
                }
            }
            if (bs == NULL || strcmp(bs->text, s->text) == 0) {
                continue;
            }
            tb = auditBytes(s->text, &tn);
            bb = auditBytes(bs->text, &bn);
            if (tb == NULL || bb == NULL) {
                free(tb);
                free(bb);
                continue;
            }
            diff++;
            bud.maxLines = rule->maxLines;
            bud.maxWidth = rule->maxWidth;
            if (rule->tightWidth > 0 && trMaxLine(&opt, bb, bn) <= rule->tightWidth) {
                bud.maxWidth = rule->tightWidth;
            }
            {
                TrBudget big;
                big.maxWidth = 1 << 20;
                big.maxLines = 3;
                if (trReflow(&opt, &big, tb, tn, &x, &xn, NULL, NULL) < 0) {
                    unparsed++;
                }
            }
            w = trMaxLine(&opt, tb, tn);
            if (w > bud.maxWidth) {
                over++;
                if (getenv("AUDIT_LIST") != NULL) {
                    printf("  over: %s part %d index %d: %d px > %d\n", bases[k], s->part, s->index, w, bud.maxWidth);
                }
            }
            free(tb);
            free(bb);
        }
        printf("%s: %d differ from the base, %d with a line over the box, %d unparsed\n", bases[k], diff, over, unparsed);
        totalDiff += diff;
        totalOver += over;
        totalUnparsed += unparsed;
        tcJsonFree(&t);
        tcJsonFree(&b);
        free(tj);
        free(bj);
    }
    printf("TOTAL: %d differ, %d over the box, %d unparsed\n", totalDiff, totalOver, totalUnparsed);
    trLayoutFree(&lay);
    free(lj);
    return 0;
}

int main(int argc, char **argv) {
    s32 file;
    u32 *data;
    int i;

    if (argc >= 4 && strcmp(argv[1], "--audit") == 0) {
        return audit(argv[2], argv[3], argc > 4 ? argv[4] : "es-419");
    }
    if (argc < 4) {
        fprintf(stderr, "usage: %s <disc.bin> <file index> <string index>...\n", argv[0]);
        return 2;
    }

    pcGfxInit();
    pcCdReaderInit(argv[1]);

    file = atoi(argv[2]);
    if (!FILE_TABLE.exists(file)) {
        fprintf(stderr, "%s: no file %d on the disc\n", argv[1], file);
        return 1;
    }

    data = FILE_CACHE.load(file);
    if (data == NULL) {
        fprintf(stderr, "file %d: the game's cache returned no data\n", file);
        return 1;
    }

    for (i = 3; i < argc; i++) {
        TextBuffer buffers[TEXT_BUFFERS];
        char *str;
        s32 index;
        s32 len;
        s32 width;
        s32 j;

        index = atoi(argv[i]);
        str = getString((s32 *)data, index);
        if (str == NULL) {
            fprintf(stderr, "file %d: no string %d\n", file, index);
            return 1;
        }

        memset(buffers, 0, sizeof(buffers));
        len = strlen(str);
        buffers[0].data = (u8 *)str;
        buffers[0].len = (s16)len;

        width = measureText(buffers, &FONT.styles[BANNER_STYLE], 0);

        printf("index %d width %d bytes ", index, width);
        for (j = 0; j < len; j++) {
            printf("%02x", ((u8 *)str)[j]);
        }
        printf("\n");
    }
    return 0;
}
