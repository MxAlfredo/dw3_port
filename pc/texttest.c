/*
 * Unit test for the text codec (pc/shim/text_codec.c, feature es419-live-option L2a).
 *
 * Default run: synthetic cases only (hand-built tiny lang files and packs, charmap and JSON
 * round trips, an overlay merge on a synthetic base). No disc, no window.
 *
 * Parity mode, LOCAL ONLY (it reads the owner's disc and writes decoded text, which must
 * never be committed):
 *     texttest <disc-dir> <overlay-dir> <work-dir>
 * For every file of every language under <disc-dir>/AAA/DAT/COUNTRY/<LANG>/: parse + rebuild
 * must be byte-identical, and the decoded JSON goes to <work-dir>/json/<LANG>/<NAME>.json
 * (compared against tools/es419/lang.py by a script outside the repo). Then SPN + the
 * overlay files merge into <work-dir>/merge/<NAME>.BIN (compared against build_text.py).
 *
 * Exit 0: every check held. Exit 1: at least one failed (printed to stderr).
 */
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "shim/mods.h"
#include "shim/text_codec.h"
#include "shim/text_data.h"
#include "shim/text_reflow.h"
#include "shim/talk_box_wide.h"
#include "shim/text_fit.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "texttest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

#define CHECK_STR(got, want) \
    do { \
        const char *g_ = (got); \
        if (!g_ || strcmp(g_, (want))) { \
            fprintf(stderr, "texttest: FAIL line %d: got \"%s\", want \"%s\"\n", __LINE__, g_ ? g_ : "(null)", (want)); \
            failures++; \
        } \
    } while (0)

/* ---- synthetic data ---- */

typedef struct Bytes {
    uint8_t d[512];
    size_t n;
} Bytes;

static void put32(Bytes *b, size_t at, uint32_t v)
{
    b->d[at] = (uint8_t)v;
    b->d[at + 1] = (uint8_t)(v >> 8);
    b->d[at + 2] = (uint8_t)(v >> 16);
    b->d[at + 3] = (uint8_t)(v >> 24);
}

/* A canonical lang file of the given encoded bodies (the layout the game's files use) */
static void makeLang(Bytes *b, int count, const char *const *texts)
{
    int i;
    b->n = 4 + 4 * (size_t)count;
    memset(b->d, 0, b->n);
    put32(b, 0, (uint32_t)count);
    for (i = 0; i < count; i++) {
        uint8_t *enc;
        size_t len;
        while (b->n % 4) b->d[b->n++] = 0;
        put32(b, 4 + 4 * (size_t)i, (uint32_t)b->n);
        if (tcEncode(texts[i], &enc, &len, NULL, 0)) {
            fprintf(stderr, "texttest: makeLang cannot encode \"%s\"\n", texts[i]);
            exit(2);
        }
        memcpy(b->d + b->n, enc, len);
        b->n += len;
        b->d[b->n++] = 0;
        free(enc);
    }
}

static int sameBytes(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
    return an == bn && !memcmp(a, b, an);
}

/* parse + rebuild must give the input back */
static int roundTrips(const uint8_t *d, size_t n)
{
    TcDoc doc;
    size_t outLen = 0;
    uint8_t *out;
    int ok;
    if (tcDocParse(d, n, &doc, NULL, 0)) return 0;
    out = tcDocBuild(&doc, &outLen);
    ok = out && sameBytes(d, n, out, outLen);
    free(out);
    tcDocFree(&doc);
    return ok;
}

/* ---- cases ---- */

static void testCharmap(void)
{
    static const uint8_t hello[] = {0x28 + 7, 0x28 + 14, 0x28 + 11, 0x28 + 0}; /* "hola" */
    static const uint8_t mix[] = {0x0E, 0x01, 0x57, 0x01, 0x3F, 0x02, 0x01, 0xE7, 0x04}; /* "A\xc3\xb1\xc2\xbf\n!0" */
    static const uint8_t odd[] = {0x03, 0x01, 0xFF, 0x02, 0x07, 0x01};
    char *t;
    uint8_t *b;
    size_t n;
    char err[128];

    t = tcDecode(hello, sizeof hello);
    CHECK_STR(t, "hola");
    free(t);
    t = tcDecode(mix, sizeof mix);
    CHECK_STR(t, "A\xc3\xb1\xc2\xbf\n!0");
    free(t);
    t = tcDecode(odd, sizeof odd);
    CHECK_STR(t, "{03}{01ff}{0207}{01}"); /* unknown pair, control pair, lone trailing 01 */
    free(t);
    t = tcDecode(NULL, 0);
    CHECK_STR(t, "");
    free(t);

    CHECK(tcEncode("{03}{01ff}{0207}{01}", &b, &n, err, sizeof err) == 0);
    CHECK(sameBytes(b, n, odd, sizeof odd));
    free(b);
    CHECK(tcEncode("A\xc3\xb1\xc2\xbf\n!0", &b, &n, err, sizeof err) == 0);
    CHECK(sameBytes(b, n, mix, sizeof mix));
    free(b);
    /* uppercase hex is not a token ({0A} has no encoding: '{' is not in the charmap) */
    CHECK(tcEncode("{0A}", &b, &n, err, sizeof err) != 0);
    CHECK(strstr(err, "U+007B") != NULL);
    CHECK(tcEncode("\xe4\xb8\xad", &b, &n, err, sizeof err) != 0); /* a CJK character */
    CHECK(strstr(err, "U+4E2D") != NULL);
    CHECK(tcEncode("\xff", &b, &n, err, sizeof err) != 0); /* invalid UTF-8 */
}

static void testAliases(void)
{
    char *p = tcPrettify("a{0207}b{0202}{02}c{0202}{0202}{03}d");
    CHECK_STR(p, "a{hablante}b{fin}c{pagina}d");
    free(p);
    p = tcUglify("a{hablante}b{fin}c{pagina}d");
    CHECK_STR(p, "a{0207}b{0202}{02}c{0202}{0202}{03}d");
    free(p);
    p = tcPrettify("{0202}{0202}{03}{0202}{02}");
    CHECK_STR(p, "{pagina}{fin}");
    free(p);
    /* a {0202}{02} inside a {0202}{0202}{03} group must not be eaten first */
    p = tcPrettify("{0202}{0202}{02}");
    CHECK_STR(p, "{0202}{fin}");
    free(p);
}

static void testLangFile(void)
{
    static const char *const t[] = {"Hola", "A\xc3\xb1o \xc2\xbf" "qu\xc3\xa9?", ""};
    Bytes b;
    TcDoc doc;
    char *s;
    size_t n;
    uint8_t *out;

    makeLang(&b, 3, t);
    CHECK(roundTrips(b.d, b.n));
    CHECK(tcDocParse(b.d, b.n, &doc, NULL, 0) == 0);
    CHECK(!doc.isPack && doc.lang.count == 3);
    s = tcEntryText(&doc.lang, 1);
    CHECK_STR(s, t[1]);
    free(s);
    CHECK(tcEntryText(&doc.lang, 3) == NULL);

    /* growth: the entry stops sharing storage and the file gets longer */
    CHECK(tcSetText(&doc.lang, 0, "Adiós, qué día tan largo\n{0102}¡Sí!", NULL, 0) == 0);
    out = tcDocBuild(&doc, &n);
    CHECK(out && n > b.n);
    tcDocFree(&doc);
    CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0);
    s = tcEntryText(&doc.lang, 0);
    CHECK_STR(s, "Adiós, qué día tan largo\n{0102}¡Sí!");
    free(s);
    s = tcEntryText(&doc.lang, 1);
    CHECK_STR(s, t[1]);
    free(s);
    tcDocFree(&doc);
    CHECK(roundTrips(out, n));
    free(out);

    /* errors */
    CHECK(tcDocParse(b.d, b.n, &doc, NULL, 0) == 0);
    {
        char err[128];
        CHECK(tcSetText(&doc.lang, 9, "x", err, sizeof err) != 0);
        CHECK(tcSetText(&doc.lang, -1, "x", err, sizeof err) != 0);
        CHECK(tcSetText(&doc.lang, 0, "\xe4\xb8\xad", err, sizeof err) != 0);
        s = tcEntryText(&doc.lang, 0);
        CHECK_STR(s, "Hola"); /* a failed set leaves the entry alone */
        free(s);
    }
    tcDocFree(&doc);
    {
        static const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        TcDoc d2;
        char err[64];
        CHECK(tcDocParse(junk, sizeof junk, &d2, err, sizeof err) != 0);
        CHECK(tcDocParse(junk, 3, &d2, err, sizeof err) != 0);
    }
}

/* Layout quirks the real files have: a gap after the header, shared offsets, odd tails */
static void testQuirks(void)
{
    /* count 3; offsets: [20, 20, 28]; 4 gap bytes; "ab\0\0" shared by 0 and 1; "c" then
       a non-canonical tail "\0\0\0\0\0" (more padding than needed) at the very end */
    Bytes b;
    TcDoc doc;
    char *s;
    memset(&b, 0, sizeof b);
    put32(&b, 0, 3);
    put32(&b, 4, 20);
    put32(&b, 8, 20);
    put32(&b, 12, 28);
    memset(b.d + 16, 0xAA, 4);
    b.d[20] = 0x28 + 0; /* a */
    b.d[21] = 0x28 + 1; /* b */
    b.d[22] = 0;
    b.d[23] = 0;
    b.d[28] = 0x28 + 2; /* c */
    memset(b.d + 29, 0, 5);
    b.n = 34;
    CHECK(roundTrips(b.d, b.n));
    CHECK(tcDocParse(b.d, b.n, &doc, NULL, 0) == 0);
    CHECK(doc.lang.gapLen == 4);
    CHECK(doc.lang.entries[0].slot == doc.lang.entries[1].slot);
    CHECK(doc.lang.entries[2].hasTail);
    s = tcEntryText(&doc.lang, 1);
    CHECK_STR(s, "ab");
    free(s);
    /* editing one of the sharers separates them */
    CHECK(tcSetText(&doc.lang, 1, "xyz", NULL, 0) == 0);
    {
        size_t n;
        uint8_t *out = tcDocBuild(&doc, &n);
        TcDoc d2;
        CHECK(out && tcDocParse(out, n, &d2, NULL, 0) == 0);
        s = tcEntryText(&d2.lang, 0);
        CHECK_STR(s, "ab");
        free(s);
        s = tcEntryText(&d2.lang, 1);
        CHECK_STR(s, "xyz");
        free(s);
        CHECK(d2.lang.entries[0].slot != d2.lang.entries[1].slot);
        tcDocFree(&d2);
        free(out);
    }
    tcDocFree(&doc);
}

static void buildPack(Bytes *pack, const Bytes *lang, const uint8_t *raw, size_t rawLen)
{
    memset(pack, 0, sizeof *pack);
    put32(pack, 0, 8);
    put32(pack, 4, (uint32_t)(8 + lang->n));
    memcpy(pack->d + 8, lang->d, lang->n);
    memcpy(pack->d + 8 + lang->n, raw, rawLen);
    pack->n = 8 + lang->n + rawLen;
}

static void testPack(void)
{
    static const char *const t[] = {"Hola", "Adi\xc3\xb3s"};
    static const uint8_t raw[] = {1, 2, 3, 4};
    Bytes lang, pack;
    TcDoc doc;
    char *s;
    makeLang(&lang, 2, t);
    buildPack(&pack, &lang, raw, sizeof raw);
    CHECK(roundTrips(pack.d, pack.n));
    CHECK(tcDocParse(pack.d, pack.n, &doc, NULL, 0) == 0);
    CHECK(doc.isPack && doc.partCount == 2);
    CHECK(doc.parts[0].isLang && !doc.parts[1].isLang && doc.parts[1].rawLen == 4);
    CHECK(tcDocLang(&doc, 0) != NULL);
    CHECK(tcDocLang(&doc, 1) == NULL); /* raw part */
    CHECK(tcDocLang(&doc, 2) == NULL);
    CHECK(tcDocLang(&doc, -1) == NULL); /* a pack needs a part */
    s = tcEntryText(tcDocLang(&doc, 0), 1);
    CHECK_STR(s, "Adi\xc3\xb3s");
    free(s);
    /* growing a part moves the offsets of the later ones and keeps the raw bytes */
    CHECK(tcSetText(tcDocLang(&doc, 0), 0, "Un texto bastante mas largo que antes", NULL, 0) == 0);
    {
        size_t n;
        uint8_t *out = tcDocBuild(&doc, &n);
        TcDoc d2;
        CHECK(out && tcDocParse(out, n, &d2, NULL, 0) == 0);
        CHECK(d2.partCount == 2 && !d2.parts[1].isLang);
        CHECK(sameBytes(d2.parts[1].raw, d2.parts[1].rawLen, raw, sizeof raw));
        s = tcEntryText(tcDocLang(&d2, 0), 0);
        CHECK_STR(s, "Un texto bastante mas largo que antes");
        free(s);
        tcDocFree(&d2);
        free(out);
    }
    tcDocFree(&doc);
    {
        Bytes plain;
        TcDoc d3;
        makeLang(&plain, 2, t);
        CHECK(tcDocParse(plain.d, plain.n, &d3, NULL, 0) == 0);
        CHECK(tcDocLang(&d3, -1) == &d3.lang);
        CHECK(tcDocLang(&d3, 0) == NULL); /* a part on a plain file */
        tcDocFree(&d3);
    }
}

static void testJson(void)
{
    static const char *const t[] = {"Hola", "{hablante}¡A\"b\nñ!{fin}", "{0102}{03}"};
    static const char *const raw[] = {"Hola", "{0207}¡A\"b\nñ!{0202}{02}", "{0102}{03}"};
    Bytes b, pack;
    TcDoc doc, back;
    TcJson j;
    char *json, err[160];
    size_t n;
    uint8_t *out;
    int i;

    makeLang(&b, 3, raw);
    CHECK(tcDocParse(b.d, b.n, &doc, NULL, 0) == 0);
    json = tcDocToJson(&doc, "SSTEST.BIN");
    CHECK(json != NULL);
    CHECK(strstr(json, "\"format\": \"" TC_FORMAT_FULL "\"") != NULL);
    CHECK(strstr(json, "{hablante}") != NULL && strstr(json, "{fin}") != NULL); /* prettified */
    CHECK(strstr(json, "\\\"b\\n") != NULL);
    CHECK(tcJsonParse(json, strlen(json), &j, err, sizeof err) == 0);
    CHECK_STR(j.file, "SSTEST.BIN");
    CHECK_STR(j.format, TC_FORMAT_FULL);
    CHECK(j.count == 3);
    for (i = 0; i < j.count; i++) {
        CHECK(j.strings[i].index == i && j.strings[i].part == -1);
        CHECK_STR(j.strings[i].text, t[i]);
    }
    /* applying the full JSON onto an untouched base changes nothing, and onto an empty one
       gives the same text */
    CHECK(tcApplyJson(&doc, &j, err, sizeof err) == 0);
    out = tcDocBuild(&doc, &n);
    CHECK(out != NULL);
    CHECK(tcDocParse(out, n, &back, NULL, 0) == 0);
    for (i = 0; i < 3; i++) {
        char *s = tcEntryText(&back.lang, i);
        char *u = tcUglify(t[i]);
        CHECK_STR(s, u);
        free(s);
        free(u);
    }
    tcDocFree(&back);
    free(out);
    tcJsonFree(&j);
    free(json);
    tcDocFree(&doc);

    /* a pack writes "part" and lists every lang part's every entry */
    {
        static const uint8_t rawPart[] = {9, 9, 9, 9};
        makeLang(&b, 3, raw);
        buildPack(&pack, &b, rawPart, sizeof rawPart);
        CHECK(tcDocParse(pack.d, pack.n, &doc, NULL, 0) == 0);
        json = tcDocToJson(&doc, "SSDMGTST.BIN");
        CHECK(json && tcJsonParse(json, strlen(json), &j, err, sizeof err) == 0);
        CHECK(j.count == 3 && j.strings[2].part == 0 && j.strings[2].index == 2);
        tcJsonFree(&j);
        free(json);
        tcDocFree(&doc);
    }

    /* reader: overlay shape with extra keys, escapes, surrogates, whitespace */
    {
        const char *ov =
            "{\"file\":\"X.BIN\",\"base\":\"SPN\",\"format\":\"overlay-v1\",\"n\":[1,{\"a\":null},true,-2.5e3],"
            "\"strings\":[{\"index\":2,\"part\":1,\"note\":\"G-1\",\"text\":\"a\\u00f1o \\ud83d\\ude00\\/\\t\\u0041\\\\\"},"
            " {\"text\":\"\",\"index\":0}]}\n";
        CHECK(tcJsonParse(ov, strlen(ov), &j, err, sizeof err) == 0);
        CHECK(j.count == 2);
        CHECK_STR(j.format, "overlay-v1");
        CHECK(j.strings[0].index == 2 && j.strings[0].part == 1);
        CHECK_STR(j.strings[0].text, "a\xc3\xb1o \xf0\x9f\x98\x80/\tA\\");
        CHECK(j.strings[1].part == -1);
        CHECK_STR(j.strings[1].text, "");
        tcJsonFree(&j);
    }

    /* reader: strictness */
    {
        static const char *const bad[] = {
            "", "{", "[]", "{\"strings\":[}", "{\"strings\":[{\"index\":1}]}",
            "{\"strings\":[{\"text\":\"x\"}]}", "{\"strings\":[{\"index\":1.5,\"text\":\"x\"}]}",
            "{\"strings\":[{\"index\":1,\"text\":\"x\\q\"}]}", "{\"strings\":[{\"index\":1,\"text\":\"\\ud800\"}]}",
            "{\"strings\":[{\"index\":1,\"text\":\"\\u0000\"}]}", "{\"strings\":[]} x",
            "{\"strings\":[{\"index\":1,\"text\":\"unterminated}]}", "{\"file\":3,\"strings\":[]}",
            "{\"file\":\"a\"}", "{\"strings\":[{\"index\":1,\"text\":\"a\nb\"}]}",
        };
        for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
            if (tcJsonParse(bad[i], strlen(bad[i]), &j, err, sizeof err) == 0) {
                fprintf(stderr, "texttest: FAIL line %d: accepted bad JSON #%d\n", __LINE__, i);
                failures++;
                tcJsonFree(&j);
            }
        }
        CHECK(tcJsonParse("{\"strings\":[]}", 14, &j, err, sizeof err) == 0 && j.count == 0);
        tcJsonFree(&j);
    }
}

static void testMerge(void)
{
    static const char *const base[] = {"Hola", "Adi\xc3\xb3s", "Gracias", "{0207}x"};
    const char *ov =
        "{\"file\":\"SSDMGTST.BIN\",\"format\":\"overlay-v1\",\"strings\":["
        "{\"index\":1,\"part\":0,\"text\":\"{hablante}\\u00a1Hasta luego!{fin}\"},"
        "{\"index\":3,\"part\":0,\"text\":\"Ma\\u00f1ana\"}]}";
    static const uint8_t rawPart[] = {7, 7, 7, 7};
    Bytes b, pack;
    TcDoc doc;
    TcJson j;
    char err[160], *s;
    size_t n;
    uint8_t *out;

    makeLang(&b, 4, base);
    buildPack(&pack, &b, rawPart, sizeof rawPart);
    CHECK(tcDocParse(pack.d, pack.n, &doc, NULL, 0) == 0);
    CHECK(tcJsonParse(ov, strlen(ov), &j, err, sizeof err) == 0);
    CHECK(tcApplyJson(&doc, &j, err, sizeof err) == 0);
    out = tcDocBuild(&doc, &n);
    tcDocFree(&doc);
    CHECK(out != NULL);
    CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0);
    s = tcEntryText(tcDocLang(&doc, 0), 0);
    CHECK_STR(s, "Hola"); /* not listed: untouched */
    free(s);
    s = tcEntryText(tcDocLang(&doc, 0), 1);
    CHECK_STR(s, "{0207}\xc2\xa1Hasta luego!{0202}{02}"); /* aliases became raw tokens */
    free(s);
    s = tcEntryText(tcDocLang(&doc, 0), 3);
    CHECK_STR(s, "Ma\xc3\xb1" "ana");
    free(s);
    CHECK(sameBytes(doc.parts[1].raw, doc.parts[1].rawLen, rawPart, sizeof rawPart));
    tcDocFree(&doc);
    free(out);
    tcJsonFree(&j);

    /* loud failures: bad index, bad part, unencodable text, part on a plain file */
    {
        static const char *const bads[] = {
            "{\"strings\":[{\"index\":9,\"part\":0,\"text\":\"x\"}]}",
            "{\"strings\":[{\"index\":0,\"part\":1,\"text\":\"x\"}]}",
            "{\"strings\":[{\"index\":0,\"part\":0,\"text\":\"\\u4e2d\"}]}",
            "{\"strings\":[{\"index\":0,\"text\":\"x\"}]}",
        };
        int i;
        for (i = 0; i < 4; i++) {
            CHECK(tcDocParse(pack.d, pack.n, &doc, NULL, 0) == 0);
            CHECK(tcJsonParse(bads[i], strlen(bads[i]), &j, err, sizeof err) == 0);
            CHECK(tcApplyJson(&doc, &j, err, sizeof err) != 0);
            CHECK(strstr(err, "index=") != NULL);
            tcJsonFree(&j);
            tcDocFree(&doc);
        }
    }
}


/* ---- L2b: index map, manifest, decisions, validation (shim/text_data.c) ---- */

static void testIndexMap(void)
{
    int lang, k, l, i;
    char name[32];

    /* TEXT_FILE(file) = LANGUAGE + file - 1 with file 0x02 + 7k: Spanish (LANGUAGE 6) is 7 + 7k */
    CHECK(tdIndexOf(TD_SPA, 0) == 7 && tdIndexOf(TD_SPA, 1) == 14 && tdIndexOf(TD_SPA, 49) == 350);
    CHECK(tdIndexOf(TD_ENG, 0) == 3 && tdIndexOf(TD_FRA, 0) == 4 && tdIndexOf(TD_ITA, 0) == 5);
    CHECK(tdIndexOf(TD_GER, 0) == 6 && tdIndexOf(TD_ES419, 3) == tdIndexOf(TD_SPA, 3));
    CHECK(TD_MAX_INDEX == 350);
    for (l = 0; l < TD_REAL_LANGS; l++) {
        for (k = 0; k < TD_FILES; k++) {
            i = tdIndexOf(l, k);
            CHECK(tdFromIndex(i, &lang, &i) == 1 && lang == l && i == k);
        }
    }
    /* the JPN (1 + 7k) and USA (2 + 7k) slots, index 0 and out of range are not ours */
    CHECK(tdFromIndex(1, &lang, &k) == 0 && tdFromIndex(2, &lang, &k) == 0);
    CHECK(tdFromIndex(8, &lang, &k) == 0 && tdFromIndex(9, &lang, &k) == 0);
    CHECK(tdFromIndex(0, &lang, &k) == 0 && tdFromIndex(351, &lang, &k) == 0 && tdFromIndex(-5, &lang, &k) == 0);
    CHECK(tdFromIndex(7, &lang, &k) == 1 && lang == TD_SPA && k == 0);
    CHECK(tdFromIndex(350, &lang, &k) == 1 && lang == TD_SPA && k == 49);
    /* names: letter + "S" + base; k is the disc order (DMG files last, TALK before TRAINI) */
    CHECK(tdFileName(TD_SPA, 0, name, sizeof name) == 0);
    CHECK_STR(name, "SSAMTMAP.BIN");
    CHECK(tdFileName(TD_ENG, 12, name, sizeof name) == 0);
    CHECK_STR(name, "ESFGTRPT.BIN");
    CHECK(tdFileName(TD_GER, 38, name, sizeof name) == 0);
    CHECK_STR(name, "DSTRAINI.BIN");
    CHECK(tdFileName(TD_ES419, 49, name, sizeof name) == 0);
    CHECK_STR(name, "SSDMG900.BIN");
    CHECK(tdFileName(TD_FRA, 50, name, sizeof name) != 0 && tdFileName(TD_SPA, 0, name, 5) != 0);
    CHECK_STR(tdLangDir(TD_ES419), "es-419");
    CHECK_STR(tdLangDir(TD_GER), "ger");
    CHECK_STR(tdDiscDir(TD_ES419), "SPN");
    CHECK_STR(tdDiscDir(TD_ENG), "ENG");
}

static void testManifestAndDecisions(void)
{
    TdManifest m, m2;
    char hex[17], hex2[17], err[120], stamp[256], stamp2[256];
    char *text;

    CHECK(tdHash("a", 1) != tdHash("b", 1) && tdHash("abc", 3) == tdHash("abc", 3));
    CHECK(tdHashUpdate(tdHashUpdate(TD_HASH_INIT, "ab", 2), "c", 1) == tdHash("abc", 3));
    tdHashHex(tdHash("abc", 3), hex);
    CHECK(strlen(hex) == 16);
    tdHashHex(0, hex2);
    CHECK_STR(hex2, "0000000000000000");

    tdMakeStamp(stamp, sizeof stamp, "d1", "o1");
    tdMakeStamp(stamp2, sizeof stamp2, "d1", "o2");
    CHECK(strcmp(stamp, stamp2) != 0 && strstr(stamp, "d1") && strstr(stamp, "codec="));

    memset(&m, 0, sizeof m);
    snprintf(m.stamp, sizeof m.stamp, "%s", stamp);
    CHECK(tdManifestSet(&m, "spa/SSAMTMAP.json", hex) == 0);
    CHECK(tdManifestSet(&m, "es-419/SSAMTMAP.json", hex2) == 0);
    CHECK(tdManifestSet(&m, "spa/SSAMTMAP.json", hex2) == 0); /* replaces */
    CHECK(m.count == 2);
    text = tdManifestFormat(&m);
    CHECK(text != NULL);
    memset(&m2, 0, sizeof m2);
    CHECK(tdManifestParse(text, strlen(text), &m2, err, sizeof err) == 0);
    CHECK_STR(m2.stamp, stamp);
    CHECK(m2.count == 2);
    CHECK_STR(tdManifestFind(&m2, "spa/SSAMTMAP.json"), hex2);
    CHECK(tdManifestFind(&m2, "eng/none.json") == NULL);
    tdManifestFree(&m2);
    free(text);
    {
        static const char *const bad[] = {"", "{", "{\"stamp\": 3}", "{\"files\": {}}", "[1]", "{\"stamp\":\"a\\\"b\"}"};
        int i;
        for (i = 0; i < 6; i++) {
            CHECK(tdManifestParse(bad[i], strlen(bad[i]), &m2, err, sizeof err) != 0);
            CHECK(err[0] != 0);
        }
    }
    tdManifestFree(&m);

    /* edited-vs-generated */
    CHECK(tdFileState(0, NULL, NULL) == TD_FILE_MISSING);
    CHECK(tdFileState(0, "aa", NULL) == TD_FILE_MISSING);
    CHECK(tdFileState(1, "aa", "aa") == TD_FILE_PRISTINE);
    CHECK(tdFileState(1, "aa", "bb") == TD_FILE_EDITED);
    CHECK(tdFileState(1, NULL, "bb") == TD_FILE_EDITED); /* the manifest does not know it: the user's */

    /* what the extractor does with each */
    CHECK(tdExtractAction(1, TD_FILE_PRISTINE) == TD_X_SKIP);
    CHECK(tdExtractAction(0, TD_FILE_PRISTINE) == TD_X_WRITE);
    CHECK(tdExtractAction(1, TD_FILE_MISSING) == TD_X_WRITE);
    CHECK(tdExtractAction(0, TD_FILE_MISSING) == TD_X_WRITE);
    CHECK(tdExtractAction(1, TD_FILE_EDITED) == TD_X_WRITE_NEW);
    CHECK(tdExtractAction(0, TD_FILE_EDITED) == TD_X_WRITE_NEW);

    /* serving order: mods, edited data, es-419 merge, then the disc (implicit) */
    {
        TdSource o[3];
        CHECK(tdSourceOrder(0, TD_FILE_PRISTINE, 0, o) == 0); /* retail: the disc, untouched */
        CHECK(tdSourceOrder(0, TD_FILE_MISSING, 0, o) == 0);
        CHECK(tdSourceOrder(0, TD_FILE_EDITED, 0, o) == 1 && o[0] == TD_SRC_DATA);
        CHECK(tdSourceOrder(1, TD_FILE_EDITED, 0, o) == 2 && o[0] == TD_SRC_MOD && o[1] == TD_SRC_DATA);
        CHECK(tdSourceOrder(0, TD_FILE_PRISTINE, 1, o) == 1 && o[0] == TD_SRC_ES419);
        CHECK(tdSourceOrder(1, TD_FILE_EDITED, 1, o) == 3 && o[2] == TD_SRC_ES419);
    }
}

static void testBuildValidation(void)
{
    static const char *const base[] = {"Hola", "Adi\xc3\xb3s", "Gracias"};
    Bytes b;
    uint8_t *out;
    size_t n;
    char err[200];
    TcDoc doc;
    char *s;
    const char *full =
        "{\"file\":\"X.BIN\",\"format\":\"text-v1\",\"strings\":[{\"index\":0,\"text\":\"Hola\"},"
        "{\"index\":1,\"text\":\"Nuevo\"},{\"index\":2,\"text\":\"Gracias\"}]}";
    const char *part = "{\"strings\":[{\"index\":1,\"text\":\"Solo\"}]}";
    const char *shortJ = "{\"strings\":[{\"index\":0,\"text\":\"Hola\"},{\"index\":1,\"text\":\"x\"}]}";
    const char *dup = "{\"strings\":[{\"index\":0,\"text\":\"a\"},{\"index\":0,\"text\":\"b\"},{\"index\":2,\"text\":\"c\"}]}";
    const char *range = "{\"strings\":[{\"index\":0,\"text\":\"a\"},{\"index\":1,\"text\":\"b\"},{\"index\":7,\"text\":\"c\"}]}";
    const char *enc = "{\"strings\":[{\"index\":0,\"text\":\"a\"},{\"index\":1,\"text\":\"\\u4e2d\"},{\"index\":2,\"text\":\"c\"}]}";
    const char *broken = "{\"strings\":[{\"index\":0,\"text\":\"a\"},,]}";

    makeLang(&b, 3, base);
    /* a whole file (user edit) */
    CHECK(tdBuild(b.d, b.n, full, strlen(full), 1, &out, &n, err, sizeof err) == 0);
    CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0);
    s = tcEntryText(&doc.lang, 1);
    CHECK_STR(s, "Nuevo");
    free(s);
    tcDocFree(&doc);
    free(out);
    /* an overlay may list a few */
    CHECK(tdBuild(b.d, b.n, part, strlen(part), 0, &out, &n, err, sizeof err) == 0);
    free(out);
    /* ...a user file may not */
    CHECK(tdBuild(b.d, b.n, part, strlen(part), 1, &out, &n, err, sizeof err) != 0);
    CHECK(strstr(err, "count") != NULL);
    CHECK(tdBuild(b.d, b.n, shortJ, strlen(shortJ), 1, &out, &n, err, sizeof err) != 0);
    CHECK(tdBuild(b.d, b.n, dup, strlen(dup), 1, &out, &n, err, sizeof err) != 0);
    CHECK(strstr(err, "twice") != NULL || strstr(err, "duplicate") != NULL);
    CHECK(tdBuild(b.d, b.n, range, strlen(range), 1, &out, &n, err, sizeof err) != 0);
    CHECK(strstr(err, "index=7") != NULL);
    CHECK(tdBuild(b.d, b.n, enc, strlen(enc), 1, &out, &n, err, sizeof err) != 0);
    CHECK(strstr(err, "index=1") != NULL && strstr(err, "U+4E2D") != NULL);
    CHECK(tdBuild(b.d, b.n, broken, strlen(broken), 1, &out, &n, err, sizeof err) != 0);
    CHECK(strstr(err, "byte") != NULL);
    CHECK(tdBuild((const uint8_t *)"zz", 2, full, strlen(full), 1, &out, &n, err, sizeof err) != 0);
}

/* ---- ADR-005/ADR-010: text mods in <pref>/mods/<mod>/text/<lang>/, any case; the old <data>/mods/text/ is not read ---- */

static void writeText(const char *root, const char *rel)
{
    char path[1300], dir[1300], *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        tdMakeDirs(dir);
    }
    f = fopen(path, "wb");
    if (f) {
        fputs("{\"format\":\"mod-v1\",\"strings\":[]}", f);
        fclose(f);
    }
}

static void testModTextPaths(void)
{
    const char *tmp = getenv("TMPDIR");
    char root[1100], mods[1200], data[1200], name[32], fname[32], p[1300];
    char paths[TD_MOD_FILES_MAX][1200];
    long mix = 0;
    int n;

    snprintf(root, sizeof root, "%s/texttest-mods-XXXXXX", (tmp && tmp[0]) ? tmp : "/tmp");
    if (!mkdtemp(root)) {
        CHECK(0 && "scratch folder");
        return;
    }
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(data, sizeof data, "%s/data", root);
    tdFileName(TD_ENG, 0, name, sizeof name);
    name[strlen(name) - 4] = 0;
    tdSetDataDir(data);

    /* no mods, no files: nothing */
    modsInit(mods, NULL);
    CHECK(tdModFiles(TD_ENG, 0, paths, &mix) == 0);

    /* two mods with the file (one spelled in upper case, one in lower), one with another language only, plus the old folder */
    snprintf(p, sizeof p, "mods/a/mod.json");
    writeText(root, p);
    writeText(root, "mods/b/mod.json");
    writeText(root, "mods/c/mod.json");
    snprintf(p, sizeof p, "mods/a/text/eng/%s.json", name);
    writeText(root, p);
    {
        char lower[32];
        int li;
        snprintf(lower, sizeof lower, "%s", name);
        for (li = 0; lower[li]; li++) lower[li] = (char)tolower((unsigned char)lower[li]);
        snprintf(p, sizeof p, "mods/b/text/eng/%s.json", lower);
        writeText(root, p);
    }
    tdFileName(TD_FRA, 0, fname, sizeof fname);
    fname[strlen(fname) - 4] = 0;
    snprintf(p, sizeof p, "mods/c/text/fra/%s.json", fname);
    writeText(root, p);
    snprintf(p, sizeof p, "data/mods/text/eng/%s.json", name);
    writeText(root, p);
    modsInit(mods, NULL);
    CHECK(modsCount() == 3);
    n = tdModFiles(TD_ENG, 0, paths, &mix);
    CHECK(n == 2); /* b, a (the last mod wins); the old <data>/mods/text file is not read */
    if (n == 2) {
        CHECK(strstr(paths[0], "/mods/b/text/eng/") != NULL);
        CHECK(strstr(paths[1], "/mods/a/text/eng/") != NULL); /* written in upper case, found all the same */
    }
    CHECK(tdModFiles(TD_FRA, 0, paths, &mix) == 1 && strstr(paths[0], "/mods/c/text/fra/") != NULL);

    /* the order of settings.ini: a last wins; mods not listed are off */
    modsInit(mods, "b, a");
    n = tdModFiles(TD_ENG, 0, paths, &mix);
    CHECK(n == 2 && strstr(paths[0], "/mods/a/") != NULL && strstr(paths[1], "/mods/b/") != NULL);
    modsInit(mods, "b");
    n = tdModFiles(TD_ENG, 0, paths, &mix);
    CHECK(n == 1 && strstr(paths[0], "/mods/b/") != NULL);
    CHECK(tdModFiles(TD_FRA, 0, paths, &mix) == 0); /* c is off */

    /* the old folder alone gives nothing */
    modsInit(NULL, NULL);
    n = tdModFiles(TD_ENG, 0, paths, &mix);
    CHECK(n == 0);

    tdSetDataDir(NULL);
    modsInit(NULL, NULL);
    {
        char cmd[1300];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        if (system(cmd)) {}
    }
}

/* ---- parity on the owner's disc (local only) ---- */

/* ---- L4a: reflow ---- */

/* Synthetic metrics: a letter is 6 px, the space (icon 1) 4, any other icon 3, a missing glyph 6 */
static int fakeWidth(void *ctx, int kind, int value)
{
    (void)ctx;
    if (kind == 1) return value == 1 ? 4 : 3;
    return 6;
}

/* Test notation to raw bytes: a-z letters, ' ' space, '|' newline, '#' {pagina}, '~' a wait
   (02 02 02), '@' insert of buffer 1 (02 05 01), '$' name into buffer 1 (02 08 01), '[' the
   speaker mark (02 07), '!' an unknown code (02 0A) */
static size_t rawOf(const char *s, uint8_t *out)
{
    size_t n = 0;
    for (; *s; s++) {
        char c = *s;
        if (c >= 'a' && c <= 'z') {
            out[n++] = (uint8_t)(0x28 + (c - 'a'));
        } else if (c == ' ') {
            out[n++] = 1;
            out[n++] = 1;
        } else if (c == '|') {
            out[n++] = 2;
            out[n++] = 1;
        } else if (c == '#') {
            static const uint8_t p[] = {2, 2, 2, 2, 3};
            memcpy(out + n, p, 5);
            n += 5;
        } else if (c == '~') {
            out[n++] = 2;
            out[n++] = 2;
            out[n++] = 2;
        } else if (c == '@') {
            out[n++] = 2;
            out[n++] = 5;
            out[n++] = 1;
        } else if (c == '$') {
            out[n++] = 2;
            out[n++] = 8;
            out[n++] = 1;
        } else if (c == '[') {
            out[n++] = 2;
            out[n++] = 7;
        } else if (c == '!') {
            out[n++] = 2;
            out[n++] = 10;
        }
    }
    return n;
}

/* Reflow `in` (test notation) and compare with `want` (NULL: it must come back unchanged) */
static int reflowIs(const TrOptions *o, int width, int lines, const char *in, const char *want, int wantPages,
                    int wantUnfit)
{
    uint8_t a[256], w[256], *out = NULL;
    size_t an = rawOf(in, a), wn = want ? rawOf(want, w) : 0, on = 0;
    TrBudget b;
    int pages = -1, unfit = -1, r;
    b.maxWidth = width;
    b.maxLines = lines;
    r = trReflow(o, &b, a, an, &out, &on, &pages, &unfit);
    if (!want) {
        free(out);
        if (r != 0 || unfit != wantUnfit) fprintf(stderr, "texttest: reflow of \"%s\" returned %d unfit %d, wanted it kept\n", in, r, unfit);
        return r == 0 && unfit == wantUnfit;
    }
    if (r != 1 || !out || !sameBytes(out, on, w, wn) || pages != wantPages || unfit != wantUnfit) {
        char *got = out ? tcDecode(out, on) : NULL;
        fprintf(stderr, "texttest: reflow of \"%s\": r=%d pages=%d unfit=%d got %s\n", in, r, pages, unfit,
                got ? got : "(none)");
        free(got);
        free(out);
        return 0;
    }
    free(out);
    return 1;
}

/* entry `i` of the rebuilt file is the notation `want` */
static int entryIs(const TcDoc *doc, int i, const char *want)
{
    uint8_t w[256];
    size_t wn = rawOf(want, w);
    return i < doc->lang.count && sameBytes(doc->lang.entries[i].body, doc->lang.entries[i].bodyLen, w, wn);
}

/* the appended entries of the S6 test: the first is wider than a talk box */
static const char *appendText(void *ud, int i)
{
    (void)ud;
    return i == 0 ? "aaaa bbbb cccc dddd eeee" : "aaaa";
}

static void testReflow(void)
{
    TrOptions o;
    const char *layout =
        "{\"format\":\"layout-v1\",\"note\":\"x\",\"files\":{\"TALK*\":{\"maxWidthPx\":100,\"tightWidthPx\":60,\"maxLines\":3},"
        "\"TALK02\":{\"maxWidthPx\":60,\"maxLines\":2,\"entries\":{\"3\":{\"noreflow\":true}}},\"SOLO\":{\"noreflow\":true}}}";
    TrLayout lay;
    const TrFileRule *r;
    char err[200];

    trOptionsInit(&o, fakeWidth, NULL);
    o.nameAllowance = 30;
    o.numberAllowance = 10;
    CHECK(o.space[0] == 1 && o.space[1] == 1);

    /* keep when every line fits: the original breaks stay, even odd ones */
    CHECK(reflowIs(&o, 60, 3, "aaaa bbbb|cccc|d", NULL, 0, 0));
    /* wrap greedily at the spaces: 52 px fit, 80 do not */
    CHECK(reflowIs(&o, 60, 3, "aaaa bbbb cccc dddd eeee", "aaaa bbbb|cccc dddd|eeee", 0, 0));
    /* the original breaks of a page that needs a rewrap are joined into the paragraph */
    CHECK(reflowIs(&o, 60, 3, "aaaa|bbbb cccc dddd eeee", "aaaa bbbb|cccc dddd|eeee", 0, 0));
    /* more lines than the box has: the page break the strings use */
    CHECK(reflowIs(&o, 60, 3, "aaaa bbbb cccc dddd eeee ffff gggg hhhh", "aaaa bbbb|cccc dddd|eeee ffff#gggg hhhh", 1, 0));
    CHECK(reflowIs(&o, 60, 2, "aaaa bbbb cccc dddd eeee ffff gggg hhhh", "aaaa bbbb|cccc dddd#eeee ffff|gggg hhhh", 1, 0));
    /* tokens: the speaker label is not measured, waits stay glued to their word, the page marks split pages */
    CHECK(reflowIs(&o, 60, 3, "[aaaaaaaaaaaaaaaaaaaaaaa[aaaa bbbb cccc~ dddd#eeee ffff gggg hhhh",
                   "[aaaaaaaaaaaaaaaaaaaaaaa[aaaa bbbb|cccc~ dddd#eeee ffff|gggg hhhh", 0, 0));
    /* a page that fits is not touched while its neighbour is rewritten */
    CHECK(reflowIs(&o, 60, 3, "a|b|c|d#aaaa bbbb cccc", "a|b|c|d#aaaa bbbb|cccc", 0, 0));
    /* inserts: a name (a buffer filled by 02 08) reserves 30 px, a number 10 */
    CHECK(reflowIs(&o, 70, 3, "aaaa bbbb @", NULL, 0, 0));
    CHECK(reflowIs(&o, 70, 3, "$aaaa bbbb @", "$aaaa bbbb|@", 0, 0));
    /* a word wider than the box goes alone on its line and is reported */
    CHECK(reflowIs(&o, 60, 3, "aaaaaaaaaaaaa b", "aaaaaaaaaaaaa|b", 0, 1));
    /* a one-line box cannot be wrapped: kept, reported */
    CHECK(reflowIs(&o, 60, 1, "aaaa bbbb cccc", NULL, 0, 1));
    /* idempotent */
    {
        uint8_t a[256], *o1 = NULL, *o2 = NULL;
        size_t an = rawOf("aaaa bbbb cccc dddd eeee ffff gggg hhhh", a), n1 = 0, n2 = 0;
        TrBudget bd = {60, 3};
        CHECK(trReflow(&o, &bd, a, an, &o1, &n1, NULL, NULL) == 1);
        CHECK(trReflow(&o, &bd, o1, n1, &o2, &n2, NULL, NULL) == 0);
        free(o1);
        free(o2);
    }
    /* an unknown control code is left alone */
    {
        uint8_t a[64], *x = NULL;
        size_t an = rawOf("aaaa bbbb cccc dddd!", a), xn = 0;
        TrBudget bd = {60, 3};
        CHECK(trReflow(&o, &bd, a, an, &x, &xn, NULL, NULL) == -1);
    }
    /* no metrics: nothing happens */
    {
        TrOptions none;
        uint8_t a[64], *x = NULL;
        size_t an = rawOf("aaaa bbbb cccc dddd eeee", a), xn = 0;
        TrBudget bd = {60, 3};
        memset(&none, 0, sizeof none);
        CHECK(trReflow(&none, &bd, a, an, &x, &xn, NULL, NULL) == 0);
    }

    /* the layout file */
    CHECK(trLayoutParse(layout, strlen(layout), &lay, err, sizeof err) == 0);
    CHECK(lay.count == 3);
    r = trLayoutFind(&lay, "TALK05");
    CHECK(r && r->maxWidth == 100 && r->tightWidth == 60 && r->maxLines == 3);
    r = trLayoutFind(&lay, "TALK02");
    CHECK(r && r->maxWidth == 60 && r->maxLines == 2 && r->entryCount == 1 && r->entries[0].noreflow);
    r = trLayoutFind(&lay, "SOLO");
    CHECK(r && r->noreflow);
    CHECK(trLayoutFind(&lay, "ITMNAM") == NULL);
    trLayoutFree(&lay);
    CHECK(trLayoutParse("{\"files\":{\"X\":{\"maxWidthPx\":}}}", 31, &lay, err, sizeof err) != 0);
    CHECK(strstr(err, "layout") != NULL);

    /* a whole file through tdBuildFile: the rule of its name; entries equal to the disc are never
       touched; an entry whose original text fits the tight width gets the tight width */
    {
        static const char *const baseT[] = {"aaaa bbbb", "aaaa bbbb cccc", "aaaa", "aaaa bbbb cccc", "aaaa bbbb cccc dddd eeee ffff"};
        const char *lj = "{\"format\":\"layout-v1\",\"files\":{\"TALK*\":{\"maxWidthPx\":100,\"tightWidthPx\":60,\"maxLines\":3}}}";
        const char *full =
            "{\"strings\":[{\"index\":0,\"text\":\"aaaa bbbb cccc dddd eeee\"},"
            "{\"index\":1,\"text\":\"aaaa bbbb cccc\"},"
            "{\"index\":2,\"text\":\"aaaa bbbb cccc dddd eeee ffff gggg hhhh\"},"
            "{\"index\":3,\"text\":\"aaaa bbbb cccc dddd eeee\"},"
            "{\"index\":4,\"text\":\"aaaa bbbb cccc dddd eeee ffff\"}]}";
        Bytes bb;
        uint8_t *out;
        size_t n;
        TcDoc doc;

        makeLang(&bb, 5, baseT);
        CHECK(tdReflowConfigure(lj, strlen(lj), &o) == 0);
        CHECK(tdReflowActive());
        CHECK(tdBuildFile("TALK01", "test/TALK01", bb.d, bb.n, full, strlen(full), 1, &out, &n, err, sizeof err) == 0);
        CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0);
        CHECK(entryIs(&doc, 0, "aaaa bbbb|cccc dddd|eeee"));           /* the original fit 60: 60 px */
        CHECK(entryIs(&doc, 1, "aaaa bbbb cccc"));                     /* equal to the disc: untouched */
        CHECK(entryIs(&doc, 2, "aaaa bbbb|cccc dddd|eeee ffff#gggg hhhh")); /* "aaaa": tight, 3 lines, a page */
        CHECK(entryIs(&doc, 3, "aaaa bbbb cccc|dddd eeee"));           /* the original was 80 px: 100 px */
        CHECK(entryIs(&doc, 4, "aaaa bbbb cccc dddd eeee ffff")); /* equal to the disc's: untouched even though it is too wide */
        tcDocFree(&doc);
        free(out);
        /* no rule for the file: nothing is rewritten */
        CHECK(tdBuildFile("ITMNAM", "test/ITMNAM", bb.d, bb.n, full, strlen(full), 1, &out, &n, err, sizeof err) == 0);
        CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0);
        CHECK(entryIs(&doc, 0, "aaaa bbbb cccc dddd eeee"));
        tcDocFree(&doc);
        free(out);
        tdReflowOff();
        CHECK(!tdReflowActive());
        /* off: tdBuildFile is tdBuild */
        CHECK(tdBuildFile("TALK01", "x", bb.d, bb.n, full, strlen(full), 1, &out, &n, err, sizeof err) == 0);
        CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0);
        CHECK(entryIs(&doc, 0, "aaaa bbbb cccc dddd eeee"));
        tcDocFree(&doc);
        free(out);
    }

    /* S6: entries appended to a talk file (tdAppendTexts): the new ones are reflowed with the talk-box budget, the file's own are untouched */
    {
        static const char *const base3[] = {"aaaa bbbb cccc dddd", "bbbb", "cccc"};
        static const char *const base5[] = {"a", "b", "c", "d", "e"};
        const char *lj = "{\"format\":\"layout-v1\",\"files\":{\"TALK*\":{\"maxWidthPx\":100,\"tightWidthPx\":60,\"maxLines\":3}}}";
        Bytes bb;
        uint8_t *out = NULL;
        size_t n = 0;
        TcDoc doc;

        makeLang(&bb, 3, base3);
        CHECK(tdReflowConfigure(lj, strlen(lj), &o) == 0);
        CHECK(tdAppendTexts(bb.d, bb.n, "TALK07", 3, 2, appendText, NULL, &out, &n, err, sizeof err) == 0);
        CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0 && doc.lang.count == 5);
        CHECK(entryIs(&doc, 0, "aaaa bbbb cccc dddd") && entryIs(&doc, 1, "bbbb") && entryIs(&doc, 2, "cccc")); /* the file's own, even the one wider than the box */
        CHECK(entryIs(&doc, 3, "aaaa bbbb|cccc dddd|eeee")); /* a new entry has no original: the talk-box budget (60 px) */
        CHECK(entryIs(&doc, 4, "aaaa"));
        tcDocFree(&doc);
        free(out);
        /* a file with fewer entries than the English one is padded with empty ones */
        CHECK(tdAppendTexts(bb.d, bb.n, "TALK07", 5, 1, appendText, NULL, &out, &n, err, sizeof err) == 0);
        CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0 && doc.lang.count == 6);
        CHECK(entryIs(&doc, 2, "cccc") && entryIs(&doc, 3, "") && entryIs(&doc, 4, "") && entryIs(&doc, 5, "aaaa bbbb|cccc dddd|eeee"));
        tcDocFree(&doc);
        free(out);
        /* no rule for the file name: written as given */
        CHECK(tdAppendTexts(bb.d, bb.n, "ITMNAM", 3, 1, appendText, NULL, &out, &n, err, sizeof err) == 0);
        CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0 && entryIs(&doc, 3, "aaaa bbbb cccc dddd eeee"));
        tcDocFree(&doc);
        free(out);
        /* one with more entries than it should start from is refused, with the reason */
        makeLang(&bb, 5, base5);
        err[0] = 0;
        CHECK(tdAppendTexts(bb.d, bb.n, "TALK07", 3, 1, appendText, NULL, &out, &n, err, sizeof err) != 0 && strstr(err, "5 entries") != NULL);
        /* reflow off: written as given */
        tdReflowOff();
        makeLang(&bb, 3, base3);
        CHECK(tdAppendTexts(bb.d, bb.n, "TALK07", 3, 1, appendText, NULL, &out, &n, err, sizeof err) == 0);
        CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0 && entryIs(&doc, 3, "aaaa bbbb cccc dddd eeee"));
        tcDocFree(&doc);
        free(out);
    }
}

static int readFile(const char *path, uint8_t **data, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long sz;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = (uint8_t *)malloc(sz ? (size_t)sz : 1);
    *len = (size_t)sz;
    if (!*data || (sz && fread(*data, 1, (size_t)sz, f) != (size_t)sz)) {
        fclose(f);
        free(*data);
        return -1;
    }
    fclose(f);
    return 0;
}

static int writeFile(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) return -1;
    ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int cmpName(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* Sorted names ending in .BIN or .json inside dir */
static int listDir(const char *dir, const char *ext, char ***names)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0, cap = 0;
    *names = NULL;
    if (!d) return -1;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name), el = strlen(ext);
        if (l > el && !strcmp(e->d_name + l - el, ext)) {
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                *names = (char **)realloc(*names, (size_t)cap * sizeof(char *));
            }
            (*names)[n++] = strdup(e->d_name);
        }
    }
    closedir(d);
    qsort(*names, (size_t)n, sizeof(char *), cmpName);
    return n;
}

static void freeNames(char **names, int n)
{
    int i;
    for (i = 0; i < n; i++) free(names[i]);
    free(names);
}

static int parity(const char *disc, const char *overlay, const char *work)
{
    static const char *const langs[] = {"SPN", "ENG", "FRA", "ITA", "GER", "JPN", "USA"};
    char path[2048], dir[1024];
    int li, total = 0, rebuilt = 0, notLang = 0, jsonOut = 0, merged = 0, mergeFail = 0;

    snprintf(dir, sizeof dir, "%s/json", work);
    mkdir(work, 0777);
    mkdir(dir, 0777);
    snprintf(dir, sizeof dir, "%s/merge", work);
    mkdir(dir, 0777);

    for (li = 0; li < (int)(sizeof langs / sizeof langs[0]); li++) {
        char **names;
        int n, i;
        snprintf(dir, sizeof dir, "%s/AAA/DAT/COUNTRY/%s", disc, langs[li]);
        n = listDir(dir, ".BIN", &names);
        if (n < 0) {
            fprintf(stderr, "texttest: no %s\n", dir);
            continue;
        }
        snprintf(path, sizeof path, "%s/json/%s", work, langs[li]);
        mkdir(path, 0777);
        for (i = 0; i < n; i++) {
            uint8_t *data, *out;
            size_t len, outLen;
            TcDoc doc;
            char *json, jpath[2200];
            total++;
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            if (readFile(path, &data, &len)) {
                fprintf(stderr, "texttest: cannot read %s\n", path);
                failures++;
                continue;
            }
            if (tcDocParse(data, len, &doc, NULL, 0)) {
                notLang++;
                fprintf(stderr, "texttest: not a lang file: %s/%s\n", langs[li], names[i]);
                free(data);
                continue;
            }
            out = tcDocBuild(&doc, &outLen);
            if (out && sameBytes(data, len, out, outLen)) {
                rebuilt++;
            } else {
                fprintf(stderr, "texttest: REBUILD MISMATCH %s/%s\n", langs[li], names[i]);
                failures++;
            }
            free(out);
            json = tcDocToJson(&doc, names[i]);
            snprintf(jpath, sizeof jpath, "%s/json/%s/%s.json", work, langs[li], names[i]);
            if (json && !writeFile(jpath, json, strlen(json))) jsonOut++;
            else failures++;
            free(json);
            tcDocFree(&doc);
            free(data);
        }
        freeNames(names, n);
    }

    /* merge: SPN + overlay -> work/merge */
    {
        char **names;
        int n, i;
        snprintf(dir, sizeof dir, "%s/AAA/DAT/COUNTRY/SPN", disc);
        n = listDir(dir, ".BIN", &names);
        for (i = 0; i < n; i++) {
            uint8_t *data, *ovData, *out;
            size_t len, ovLen, outLen;
            TcDoc doc;
            TcJson j;
            char err[256], stem[64];
            size_t sl = strlen(names[i]) - 4;
            memcpy(stem, names[i], sl);
            stem[sl] = 0;
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            if (readFile(path, &data, &len)) { mergeFail++; continue; }
            snprintf(path, sizeof path, "%s/%s.json", overlay, stem);
            if (readFile(path, &ovData, &ovLen)) {
                fprintf(stderr, "texttest: missing overlay %s\n", path);
                mergeFail++;
                free(data);
                continue;
            }
            if (tcDocParse(data, len, &doc, err, sizeof err) || tcJsonParse((const char *)ovData, ovLen, &j, err, sizeof err)) {
                fprintf(stderr, "texttest: %s: %s\n", names[i], err);
                mergeFail++;
            } else {
                if (tcApplyJson(&doc, &j, err, sizeof err)) {
                    fprintf(stderr, "texttest: %s: %s\n", names[i], err);
                    mergeFail++;
                } else if ((out = tcDocBuild(&doc, &outLen))) {
                    snprintf(path, sizeof path, "%s/merge/%s", work, names[i]);
                    if (writeFile(path, out, outLen)) mergeFail++; else merged++;
                    free(out);
                }
                tcJsonFree(&j);
                tcDocFree(&doc);
            }
            free(ovData);
            free(data);
        }
        freeNames(names, n);
    }
    printf("texttest parity: files %d, lang %d, rebuilt byte-identical %d, not lang %d, json written %d, merged %d, merge failures %d\n",
           total, total - notLang, rebuilt, notLang, jsonOut, merged, mergeFail);
    failures += mergeFail + notLang;
    return failures != 0;
}


/* L4b: the wider talk box: pure width/position math and the reflow budget that follows the setting */
static void testTalkBoxWide(void)
{
    TrOptions o;
    char err[200];
    const char *lj = "{\"format\":\"layout-v1\",\"files\":{\"TALK*\":{\"maxWidthPx\":100,\"tightWidthPx\":60,\"maxLines\":3}}}";
    const char *full = "{\"strings\":[{\"index\":0,\"text\":\"aaaa bbbb cccc dddd eeee\"},{\"index\":1,\"text\":\"aaaa bbbb cccc dddd\"}]}";
    static const char *const baseT[] = {"aaaa bbbb", "aaaa bbbb cccc dddd eeee"};
    Bytes bb;
    uint8_t *out;
    size_t n;
    TcDoc doc;

    /* the setting's values */
    CHECK(tbwParseWidth("original") == 139);
    CHECK(tbwParseWidth(" original \n") == 139);
    CHECK(tbwParseWidth("180") == 180);
    CHECK(tbwParseWidth("139") == 139);
    CHECK(tbwParseWidth("280") == 280);
    CHECK(tbwParseWidth("281") == 0);
    CHECK(tbwParseWidth("138") == 0);
    CHECK(tbwParseWidth("wide") == 0);
    CHECK(tbwParseWidth("180px") == 0);
    CHECK(tbwParseWidth("") == 0);
    CHECK(tbwScanSettings("[display]\n; c\nshow_fps = off\ntalk_box_width = 220\n", 49) == 220);
    {
        const char *t = "[video]\ntalk_box_width = 220\n[display]\nTALK_BOX_WIDTH = 220\ntalk_box_width=original\r\n";
        CHECK(tbwScanSettings(t, strlen(t)) == 139);
        t = "[video]\ntalk_box_width = 220\n[display]\nshow_fps = on\n";
        CHECK(tbwScanSettings(t, strlen(t)) == 0); /* only under [display] */
        t = "[display]\ntalk_box_width = 190\n";
        CHECK(tbwScanSettings(t, strlen(t)) == 0); /* the file offers 139 160 180 200 220 250 */
        t = "[display]\ntalk_box_width = 250\n";
        CHECK(tbwScanSettings(t, strlen(t)) == 250);
        t = "[display]\ntalk_box_width = nope\n";
        CHECK(tbwScanSettings(t, strlen(t)) == 0);
    }
    /* the width of a box: retail is 95..139 */
    CHECK(tbwBoxWidth(10, 139) == 95);
    CHECK(tbwBoxWidth(120, 139) == 120);
    CHECK(tbwBoxWidth(500, 139) == 139);
    CHECK(tbwBoxWidth(500, 100) == 139); /* a maximum under retail's is retail's */
    CHECK(tbwBoxWidth(170, 220) == 170);
    CHECK(tbwBoxWidth(300, 220) == 220);
    CHECK(tbwBoxWidth(300, 999) == 280);
    /* the outline per type */
    CHECK(tbwLeft(0, 200, 150) == 50 && tbwRight(0, 200, 150) == 232);
    CHECK(tbwLeft(3, 100, 150) == 74 && tbwRight(3, 100, 150) == 256);
    /* retail geometry is never moved, wherever it is */
    CHECK(tbwClampX(0, 5, 139) == 5);
    CHECK(tbwClampX(2, 315, 139) == 315);
    /* left-growing boxes (0/1): x - w >= 2 and x + 32 <= 318 */
    CHECK(tbwClampX(0, 200, 220) == 222);  /* would start at -20: moved right by 22 */
    CHECK(tbwClampX(1, 260, 220) == 260);  /* fits: unmoved */
    CHECK(tbwClampX(1, 310, 220) == 286);  /* right edge: x <= 286 */
    /* right-growing boxes (2/3): x - 26 >= 2 and x + w + 6 <= 318 */
    CHECK(tbwClampX(2, 10, 220) == 28);
    CHECK(tbwClampX(3, 200, 220) == 92);
    CHECK(tbwClampX(2, 60, 220) == 60);
    /* too wide for the screen: centred, never NaN */
    CHECK(tbwClampX(0, 100, 290) == (292 + 286) / 2);
    /* every result is on screen for the widths of the setting, any anchor */
    {
        int t, x, w;
        for (w = 140; w <= 250; w += 10)
            for (t = 0; t < 4; t++)
                for (x = -20; x <= 340; x += 7) {
                    int cx = tbwClampX(t, x, w);
                    CHECK(tbwLeft(t, cx, w) >= TBW_MARGIN && tbwRight(t, cx, w) <= TBW_SCREEN_W - TBW_MARGIN);
                }
    }

    /* the reflow budget of a talk-box entry follows the width; the class (did the original fit
       the retail box) does not */
    trOptionsInit(&o, fakeWidth, NULL);
    makeLang(&bb, 2, baseT);
    tdSetTalkBoxWidth(90);
    CHECK(tdTalkBoxWidth() == 139); /* 90 is under retail's: ignored */
    tdSetTalkBoxWidth(180);
    CHECK(tdTalkBoxWidth() == 180);
    CHECK(tdReflowConfigure(lj, strlen(lj), &o) == 0);
    CHECK(tdBuildFile("TALK01", "test/TALK01", bb.d, bb.n, full, strlen(full), 1, &out, &n, err, sizeof err) == 0);
    CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0);
    CHECK(entryIs(&doc, 0, "aaaa bbbb cccc dddd eeee")); /* the original fit 60 (a talk box): the budget is 180, one line */
    CHECK(entryIs(&doc, 1, "aaaa bbbb cccc|dddd")); /* not a talk box (the original was wider): 100 px */
    tcDocFree(&doc);
    free(out);
    tdSetTalkBoxWidth(139);
    CHECK(tdReflowConfigure(lj, strlen(lj), &o) == 0);
    CHECK(tdBuildFile("TALK01", "test/TALK01", bb.d, bb.n, full, strlen(full), 1, &out, &n, err, sizeof err) == 0);
    CHECK(tcDocParse(out, n, &doc, NULL, 0) == 0);
    CHECK(entryIs(&doc, 0, "aaaa bbbb|cccc dddd|eeee")); /* the default: layout.json's own 60 px */
    tcDocFree(&doc);
    free(out);
    tdReflowOff();
}

extern int tdTextFitOn(void); /* text_data.c (declared there for the game side only) */

static void testTextFit(void)
{
    /* the budget lookup: by screen and x, any y */
    CHECK(tfBudget(TF_ISHOP_LIST, 0x37, 0) == 105);
    CHECK(tfBudget(TF_FMENU_EQUIP, 0xA0, 0) == 100);
    CHECK(tfBudget(TF_ISHOP_LIST, 0x38, 0) == 0);  /* another screen's x */
    CHECK(tfBudget(TF_NONE, 0x37, 0) == 0);
    CHECK(tfBudget(TF_SCREEN_COUNT, 0x37, 0) == 0);
    CHECK(tfBudget(TF_ISHOP_SHOP, 0x1D, 0) == 118); /* the shop name in the title frame */
    CHECK(tfBudget(TF_CARD_SHOP, 0x1D, 0) == 118);
    CHECK(tfBudget(TF_CARD_SHOP, 0xA7, 0) == 110);
    CHECK(tfScale(168, 118) == 118 * 0x1000 / 168); /* the longest shop name measured in L3b */
    /* the capture aid widens only the left item-grid columns */
    CHECK(tfBudget(TF_ISHOP_LIST, 0x37, 127) == 127);
    CHECK(tfBudget(TF_FMENU_ITEMS, 0x38, 127) == 127);
    CHECK(tfBudget(TF_ISHOP_LIST, 0xBA, 127) == tfBudget(TF_ISHOP_LIST, 0xBA, 0));
    CHECK(tfBudget(TF_FMENU_EQUIP, 0xA0, 127) == 100);
    /* the scale: 1.0 whenever the text fits or there is nothing to compare */
    CHECK(tfScale(105, 105) == TF_ONE);
    CHECK(tfScale(60, 105) == TF_ONE);
    CHECK(tfScale(0, 105) == TF_ONE);
    CHECK(tfScale(168, 0) == TF_ONE);
    CHECK(tfScale(168, -4) == TF_ONE);
    /* wider than the cell: budget / width, in 1/4096 */
    CHECK(tfScale(210, 105) == 0x800);
    CHECK(tfScale(168, 105) == 105 * 0x1000 / 168);
    CHECK(tfScale(106, 105) > 0x1000 * 99 / 100 && tfScale(106, 105) < TF_ONE);
    /* never under half: a 4x-too-wide text is squashed to 50% and overflows rather than turn to noise */
    CHECK(tfScale(420, 105) == TF_MIN_SCALE);
    /* L4d: no scale unless asked (DW3_TEXT_FIT=scale); the owner chose moving the layout over shrinking glyphs */
    CHECK(tfScaleMode(NULL) == 0);
    CHECK(tfScaleMode("") == 0);
    CHECK(tfScaleMode("0") == 0);
    CHECK(tfScaleMode("1") == 0);
    CHECK(tfScaleMode("scale") == 1);
    CHECK(tdTextFitOn() == 0); /* the test runs without the variable */
    /* the equipment panel's X column moves right only as far as the widest name needs */
    CHECK(tfEquipShift(0) == 0);
    CHECK(tfEquipShift(93) == 0);
    CHECK(tfEquipShift(100) == 0);   /* up to 100 px clears the X (L4c) */
    CHECK(tfEquipShift(101) == 5);   /* "Enchufe Protector": the excess plus the gap */
    CHECK(tfEquipShift(105) == 9);
    CHECK(tfEquipShift(107) == TF_EQUIP_MAX_SHIFT); /* 7 + 4, capped */
    CHECK(tfEquipShift(300) == TF_EQUIP_MAX_SHIFT && TF_EQUIP_MAX_SHIFT == 10); /* never into the count */
    CHECK(tfEquipShift(110) == 10 && tfEquipShift(111) == 10);
    CHECK(tfEquipShift(-5) == 0);
    /* the shop title frame: the retail size (136) up to the retail budget, text + padding above, capped */
    CHECK(tfTitleFrameW(0) == TF_FRAME_BASE_W);
    CHECK(tfTitleFrameW(118) == TF_FRAME_BASE_W);   /* a retail name never changes the frame */
    CHECK(tfTitleFrameW(119) == 137);
    CHECK(tfTitleFrameW(168) == 186);               /* "Tienda Articulos Amaterasu" */
    CHECK(tfTitleFrameW(400) == TF_FRAME_MAX_W);
    {
        int xs[16], n;
        n = tfFrameTiles(136, 16, 52, xs, 16); /* the retail frame: two middle tiles, side by side */
        CHECK(n == 2 && xs[0] == 16 && xs[1] == 68);
        n = tfFrameTiles(186, 16, 52, xs, 16); /* 154 px of middle: 52 + 52 + the last one back against the right cap */
        CHECK(n == 3 && xs[0] == 16 && xs[1] == 68 && xs[2] == 16 + 154 - 52);
        n = tfFrameTiles(TF_FRAME_MAX_W, 16, 52, xs, 16);
        CHECK(n >= 2 && xs[n - 1] + 52 == TF_FRAME_MAX_W - 16);
        CHECK(tfFrameTiles(136, 16, 52, xs, 1) == 1); /* never past the buffer */
        CHECK(tfFrameTiles(40, 16, 52, xs, 16) == 0); /* narrower than the caps and one tile: nothing */
    }
}

int main(int argc, char **argv)
{
    testCharmap();
    testAliases();
    testLangFile();
    testQuirks();
    testPack();
    testJson();
    testMerge();
    testIndexMap();
    testManifestAndDecisions();
    testBuildValidation();
    testModTextPaths();
    testReflow();
    testTalkBoxWide();
    testTextFit();
    if (failures) {
        fprintf(stderr, "texttest: %d synthetic check(s) failed\n", failures);
        return 1;
    }
    printf("texttest: synthetic checks passed\n");
    if (argc == 4) return parity(argv[1], argv[2], argv[3]);
    if (argc != 1) {
        fprintf(stderr, "usage: texttest [<disc-dir> <overlay-dir> <work-dir>]\n");
        return 2;
    }
    return 0;
}
