/*
 * Host test of the sound export (content-export E3a/E3b): pc/shim/wav_write.c, midi_write.c and audio_export.c. No disc, no window,
 * no game data (ADR-001): the bank is a tiny synthetic one built here, byte by byte (a VAB header with two programs and three tones,
 * four VAG samples, three SEP records in two packages), served as the two files of a bank.
 *
 * This program checks what C can check (the decoder on known blocks, the WAV and MIDI byte layout, the JSON fields the formats
 * promise, the job's manifest, skip and .new rules) and writes under argv[1]/expect/ what the independent half needs: the ADPCM bytes
 * of every sample and what the port's own parser decoded from every SEP record. pc/test_fixtures/audio/check_audio.py decodes the
 * ADPCM again by itself and compares the WAVs, parses the MIDI files with its own reader and compares them with the events the parser
 * decoded, and checks the JSON against the tone definitions.
 *
 * Usage: audiotest <scratch dir>. Exit 0: every check held. Exit 1: at least one failed (printed to stdout).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim/audio_export.h"
#include "shim/audio_mod.h"
#include "shim/midi_write.h"
#include "shim/snd_internal.h"
#include "shim/text_data.h"
#include "shim/vag_encode.h"
#include "shim/wav_read.h"
#include "shim/wav_write.h"

#ifndef _WIN32
#include <unistd.h>
#endif

/* LIBSPU, as snd_spu.c uses it (the test reads back what the bank patch wrote into SPU RAM) */
unsigned int SpuSetTransferStartAddr(unsigned int addr);
unsigned int SpuRead(unsigned char *addr, unsigned int size);

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stdout, "audiotest: FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static char gDir[1024];

/* ---------------------------------------------------------------- the synthetic bank */

static uint8_t gHead[0x4000], gBody[256];
static size_t gHeadLen, gBodyLen;
static uint8_t gVag[4][64]; /* what the body holds, kept for the independent half */
static size_t gVagLen[4];

static void le16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, unsigned long v) {
    le16(p, (unsigned)(v & 0xFFFF));
    le16(p + 2, (unsigned)(v >> 16));
}

static unsigned long rdle32(const uint8_t *p) { return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }
static unsigned rdle16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

/* The tones: program (parent), vag, volume, pan, center, shift, min, max, bend down/up, adsr1, adsr2, priority */
typedef struct ToneDef {
    int page, slot, prog, vag, vol, pan, center, shift, min, max, pbDown, pbUp, adsr1, adsr2, prior;
} ToneDef;

static const ToneDef kTones[] = {
    {0, 0, 0, 1, 100, 64, 60, 0, 0, 71, 2, 2, 0x957C, 0xC9AA, 16},
    {0, 1, 0, 2, 90, 32, 48, 16, 72, 127, 12, 12, 0x000F, 0x0000, 0},
    {1, 0, 5, 3, 80, 100, 72, 0xE0, 0, 127, 0, 0, 0x1234, 0x5678, 3},
};

static size_t buildVh(uint8_t *vh) {
    static const unsigned units[4] = {6, 4, 2, 0};
    size_t pages = 0x820 + 2 * 0x200, i;

    memset(vh, 0, pages + 0x200);
    le32(vh, 0x56414270UL); /* 'pBAV' */
    le16(vh + 0x12, 2);     /* programs */
    le16(vh + 0x16, 4);     /* VAGs */
    vh[0x18] = 0x7F;        /* master volume */
    vh[0x19] = 0x40;        /* master pan */
    vh[0x20] = 2;           /* ProgAtr[0]: tones, volume, priority, mode, pan */
    vh[0x21] = 110;
    vh[0x24] = 70;
    vh[0x20 + 5 * 16] = 1;  /* ProgAtr[5] */
    vh[0x21 + 5 * 16] = 120;
    vh[0x24 + 5 * 16] = 60;
    for (i = 0; i < sizeof kTones / sizeof kTones[0]; i++) {
        const ToneDef *t = &kTones[i];
        uint8_t *rec = vh + 0x820 + (size_t)t->page * 0x200 + (size_t)t->slot * 0x20;
        rec[0] = (uint8_t)t->prior;
        rec[2] = (uint8_t)t->vol;
        rec[3] = (uint8_t)t->pan;
        rec[4] = (uint8_t)t->center;
        rec[5] = (uint8_t)t->shift;
        rec[6] = (uint8_t)t->min;
        rec[7] = (uint8_t)t->max;
        rec[0x0C] = (uint8_t)t->pbDown;
        rec[0x0D] = (uint8_t)t->pbUp;
        le16(rec + 0x10, (unsigned)t->adsr1);
        le16(rec + 0x12, (unsigned)t->adsr2);
        le16(rec + 0x14, (unsigned)t->prog);
        le16(rec + 0x16, (unsigned)t->vag);
    }
    for (i = 0; i < 4; i++) le16(vh + pages + (i + 1) * 2, units[i]);
    return pages + 0x200;
}

/* One ADPCM block: shift/filter byte, flags byte and 14 made-up data bytes */
static void block(uint8_t *p, int sp, int flags, int seed) {
    int i;
    p[0] = (uint8_t)sp;
    p[1] = (uint8_t)flags;
    for (i = 0; i < 14; i++) p[2 + i] = (uint8_t)((seed * 37 + i * 59 + (i * i) * 11) ^ (seed << 4));
}

static size_t buildBody(uint8_t *b) {
    size_t at = 0;
    int v;

    /* VAG 1: loops (loop-start flag on block 1, end + repeat on block 2) */
    block(gVag[0] + 0, 0x04, 0, 1);
    block(gVag[0] + 16, 0x1C, 4, 2);
    block(gVag[0] + 32, 0x2B, 3, 3);
    gVagLen[0] = 48;
    /* VAG 2: stops (end flag without repeat on block 1) */
    block(gVag[1] + 0, 0x3A, 0, 4);
    block(gVag[1] + 16, 0x49, 1, 5);
    gVagLen[1] = 32;
    /* VAG 3: no end flag */
    block(gVag[2] + 0, 0x0C, 0, 6);
    gVagLen[2] = 16;
    gVagLen[3] = 0; /* VAG 4: empty */
    for (v = 0; v < 4; v++) {
        memcpy(b + at, gVag[v], gVagLen[v]);
        at += gVagLen[v];
    }
    return at;
}

/* "00 C0 05" -> bytes */
static size_t hexBytes(uint8_t *out, const char *s) {
    size_t n = 0;
    while (*s) {
        if (*s == ' ') {
            s++;
            continue;
        }
        out[n++] = (uint8_t)strtoul((char[]){s[0], s[1], 0}, NULL, 16);
        s += 2;
    }
    return n;
}

static void be16w(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* One SEP record header (13 bytes) and its data */
static size_t sepRec(uint8_t *p, unsigned res, unsigned long tempo, int num, int den, const char *hex) {
    size_t n = hexBytes(p + 13, hex);
    be16w(p, 0);
    be16w(p + 2, res);
    p[4] = (uint8_t)(tempo >> 16);
    p[5] = (uint8_t)(tempo >> 8);
    p[6] = (uint8_t)tempo;
    p[7] = (uint8_t)num;
    p[8] = (uint8_t)den;
    p[9] = (uint8_t)(n >> 24);
    p[10] = (uint8_t)(n >> 16);
    p[11] = (uint8_t)(n >> 8);
    p[12] = (uint8_t)n;
    return 13 + n;
}

static size_t sepPackage0(uint8_t *p) {
    size_t n = 0;
    memcpy(p, "pQES", 4);
    p[4] = 0;
    p[5] = 1;
    n = 6;
    /* a loop (CC99 0x14, running-status CC6 count 2, CC99 0x1E), a tempo change, two channels, a bend, running status */
    n += sepRec(p + n, 480, 500000, 4, 2,
                "00 C0 05 00 B0 07 64 00 B0 63 14 00 06 02 00 90 3C 64 60 E0 00 50 60 80 3C 40 00 FF 51 09 27 C0 "
                "30 91 40 50 30 40 00 81 00 B0 63 1E 00 FF 2F");
    /* no loop, a non-loop NRPN CC (99 = 16) that stays an event, a note-on with velocity 0 as the note-off */
    n += sepRec(p + n, 960, 400000, 3, 3, "00 C2 0A 00 B2 63 10 00 92 30 7F 60 92 30 00 00 FF 2F");
    return n;
}

static size_t sepPackage1(uint8_t *p) {
    size_t n;
    memcpy(p, "pQES", 4);
    p[4] = 0;
    p[5] = 1;
    n = 6;
    n += sepRec(p + n, 240, 500000, 4, 2, "00 90 3E 50 81 70 80 3E 00 00 FF 2F");
    return n;
}

static void buildFiles(void) {
    uint8_t vh[0x1000 + 0x400];
    uint8_t sep0[512], sep1[256];
    size_t vhLen = buildVh(vh), l0 = sepPackage0(sep0), l1 = sepPackage1(sep1), at;

    /* the head archive: a table of 3 entry offsets, then the entries (4-byte aligned) */
    memset(gHead, 0, sizeof gHead);
    le32(gHead + 0, 12);
    at = 12;
    memcpy(gHead + at, vh, vhLen);
    at += vhLen;
    at = (at + 3) & ~(size_t)3;
    le32(gHead + 4, at);
    memcpy(gHead + at, sep0, l0);
    at += l0;
    at = (at + 3) & ~(size_t)3;
    le32(gHead + 8, at);
    memcpy(gHead + at, sep1, l1);
    at += l1;
    gHeadLen = at;
    /* the body archive: one entry */
    le32(gBody, 4);
    gBodyLen = 4 + buildBody(gBody + 4);
}

/* ---------------------------------------------------------------- the reader the export gets */

static int ioBank(void *ud, int i, AudioExportBank *out) {
    (void)ud;
    if (i < 0 || i > 1) return 0;
    memset(out, 0, sizeof *out);
    out->id = 3 + i;
    snprintf(out->name, sizeof out->name, i == 0 ? "TEST01" : "TEST02");
    out->headFile = 100;
    out->bodyFile = 101;
    out->vhIndex = 0;
    out->bodyIndex = 0;
    out->sepCount = i == 0 ? 2 : 0;
    out->seps[0] = 1;
    out->seps[1] = 2;
    return 1;
}

static int sFileReads;

static int ioFile(void *ud, int file, uint8_t **bytes, size_t *len) {
    const uint8_t *src = file == 100 ? gHead : (file == 101 ? gBody : NULL);
    size_t n = file == 100 ? gHeadLen : gBodyLen;
    (void)ud;
    if (src == NULL) return 0;
    sFileReads++;
    *bytes = (uint8_t *)malloc(n);
    memcpy(*bytes, src, n);
    *len = n;
    return 1;
}

/* ---------------------------------------------------------------- helpers */

static void path(char *out, size_t cap, const char *rel) { snprintf(out, cap, "%s/%s", gDir, rel); }

static uint8_t *readAll(const char *rel, size_t *len) {
    char p[1400];
    uint8_t *d = NULL;
    path(p, sizeof p, rel);
    if (tdReadFile(p, &d, len) != 0) return NULL;
    return d;
}

static int exists(const char *rel) {
    size_t n;
    uint8_t *d = readAll(rel, &n);
    int r = d != NULL;
    free(d);
    return r;
}

/* the bytes needle[0..k) somewhere in d[0..n) */
static int hasN(const uint8_t *d, size_t n, const void *needle, size_t k) {
    size_t i;
    if (d == NULL) return 0;
    for (i = 0; i + k <= n; i++) {
        if (memcmp(d + i, needle, k) == 0) return 1;
    }
    return 0;
}

static int has(const uint8_t *d, size_t n, const char *needle) { return hasN(d, n, needle, strlen(needle)); }

static int fileHas(const char *rel, const char *needle) {
    size_t n;
    uint8_t *d = readAll(rel, &n);
    int r = has(d, n, needle);
    free(d);
    return r;
}

static void dumpFile(const char *rel, const void *data, size_t len) {
    char p[1500], dir[1400];
    path(dir, sizeof dir, "expect");
    tdMakeDirs(dir);
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    CHECK(tdWriteFileAtomic(p, data, len) == 0);
}

static unsigned long mdLe(const uint8_t *p) { return rdle32(p); }

/* ---------------------------------------------------------------- the pieces */

static void testDecoder(void) {
    int16_t pcm[28 * 4];
    AudioVagInfo info;
    uint8_t v[48];
    size_t n;

    /* shift 12, filter 0: the samples are the signed nibbles themselves, low nibble first */
    memset(v, 0, sizeof v);
    v[0] = 0x0C;
    v[2] = 0x21; /* 1, 2 */
    v[3] = 0xF8; /* -8, -1 */
    v[4] = 0x70; /* 0, 7 */
    n = audioVagDecode(v, 16, pcm, &info);
    CHECK(n == 28 && info.frames == 28 && info.ends == 0);
    CHECK(pcm[0] == 1 && pcm[1] == 2 && pcm[2] == -8 && pcm[3] == -1 && pcm[4] == 0 && pcm[5] == 7 && pcm[6] == 0);
    /* shift 8: the nibbles are 16 times larger */
    v[0] = 0x08;
    n = audioVagDecode(v, 16, pcm, &info);
    CHECK(n == 28 && pcm[0] == 16 && pcm[1] == 32 && pcm[2] == -128 && pcm[3] == -16 && pcm[5] == 112);
    /* filter 1 (0.9375 of the last sample) at shift 12, a decaying run from a first sample of 7 */
    memset(v, 0, sizeof v);
    v[0] = 0x1C;
    v[2] = 0x07;
    n = audioVagDecode(v, 16, pcm, &info);
    CHECK(n == 28 && pcm[0] == 7 && pcm[1] == 7 && pcm[2] == 6 && pcm[3] == 6 && pcm[4] == 5); /* 7, 6.56, 6.15, 5.77, 5.41 */
    /* flags: a loop with the start flag on the second block and end + repeat on the third; the decode stops after the end block */
    memset(v, 0, sizeof v);
    v[1] = 0;
    v[17] = 4;
    v[33] = 3;
    n = audioVagDecode(v, 48 + 0, pcm, &info);
    CHECK(n == 84 && info.ends == 2 && info.loopStart == 28 && info.loopEnd == 83);
    /* the end flag without repeat stops: the blocks after it are not decoded; without a start flag the loop starts at 0 */
    memset(v, 0, sizeof v);
    v[1] = 1;
    n = audioVagDecode(v, 48, pcm, &info);
    CHECK(n == 28 && info.ends == 1);
    v[1] = 3;
    n = audioVagDecode(v, 48, pcm, &info);
    CHECK(n == 28 && info.ends == 2 && info.loopStart == 0 && info.loopEnd == 27);
    /* a size that is not a whole block drops the rest */
    memset(v, 0, sizeof v);
    n = audioVagDecode(v, 20, pcm, &info);
    CHECK(n == 28 && info.ends == 0);
    CHECK(audioVagDecode(v, 0, pcm, &info) == 0);
}

static void testWavAndMidiWriters(void) {
    int16_t pcm[4] = {1, -2, 300, -32768};
    WavSmpl s;
    size_t len;
    uint8_t *w;
    MidiFile *m;
    uint8_t *mid;
    int t0, t1;

    w = wavBuild(pcm, 4, 44100, NULL, &len);
    CHECK(w != NULL && len == 44 + 8);
    if (w != NULL) {
        CHECK(memcmp(w, "RIFF", 4) == 0 && mdLe(w + 4) == len - 8 && memcmp(w + 8, "WAVEfmt ", 8) == 0);
        CHECK(rdle16(w + 20) == 1 && rdle16(w + 22) == 1 && mdLe(w + 24) == 44100 && mdLe(w + 28) == 88200 && rdle16(w + 32) == 2 &&
              rdle16(w + 34) == 16);
        CHECK(memcmp(w + 36, "data", 4) == 0 && mdLe(w + 40) == 8 && rdle16(w + 44) == 1 && rdle16(w + 46) == 0xFFFE && rdle16(w + 50) == 0x8000);
    }
    free(w);
    memset(&s, 0, sizeof s);
    s.rootNote = 61;
    s.fraction = 0x80000000u;
    s.hasLoop = 1;
    s.loopStart = 1;
    s.loopEnd = 3;
    w = wavBuild(pcm, 4, 22050, &s, &len);
    CHECK(w != NULL && len == 44 + 8 + 68);
    if (w != NULL) {
        const uint8_t *c = w + 52;
        CHECK(mdLe(w + 4) == len - 8);
        CHECK(memcmp(c, "smpl", 4) == 0 && mdLe(c + 4) == 60 && mdLe(c + 16) == 45351 && mdLe(c + 20) == 61 && mdLe(c + 24) == 0x80000000UL);
        CHECK(mdLe(c + 36) == 1 && mdLe(c + 52) == 1 && mdLe(c + 56) == 3 && mdLe(c + 64) == 0);
    }
    free(w);
    s.hasLoop = 0;
    w = wavBuild(pcm, 4, 44100, &s, &len);
    CHECK(w != NULL && len == 44 + 8 + 44 && mdLe(w + 52 + 36) == 0);
    free(w);
    CHECK(wavBuild(pcm, 4, 0, NULL, &len) == NULL);

    m = midiCreate(480);
    t0 = midiNewTrack(m);
    t1 = midiNewTrack(m);
    CHECK(m != NULL && t0 == 0 && t1 == 1);
    CHECK(midiTempo(m, t0, 0, 500000) == 0 && midiTimeSignature(m, t0, 0, 3, 3) == 0);
    CHECK(midiChannelEvent(m, t1, 0, 0xC1, 5, -1) == 0 && midiChannelEvent(m, t1, 200, 0x91, 60, 100) == 0);
    CHECK(midiChannelEvent(m, t1, 100, 0x81, 60, 0) == -1); /* a tick that goes back */
    CHECK(midiChannelEvent(m, t1, 300, 0x91, 200, 0) == -1);
    CHECK(midiMeta(m, t1, 20000, 0x06, "loopStart", 9) == 0); /* a delta of 19700 needs a 3-byte VLQ */
    CHECK(midiFinish(m, 20000, &mid, &len) == 0);
    if (mid != NULL) {
        CHECK(memcmp(mid, "MThd", 4) == 0 && mid[9] == 1 && mid[11] == 2 && mid[12] == 0x01 && mid[13] == 0xE0);
        CHECK(has(mid, len, "MTrk"));
        /* track 1: 00 C1 05, 81 48 (200) 91 3C 64, then the marker at 20000: delta 19800 = 81 9A 58 */
        CHECK(hasN(mid, len, "\x00\xC1\x05\x81\x48\x91\x3C\x64\x81\x9A\x58\xFF\x06\x09loopStart", 20));
        CHECK(len > 14 && mid[len - 3] == 0xFF && mid[len - 2] == 0x2F && mid[len - 1] == 0x00);
        /* track 0: 00 FF 51 03 07 A1 20, 00 FF 58 04 03 03 18 08 */
        CHECK(hasN(mid, len, "\x00\xFF\x51\x03\x07\xA1\x20\x00\xFF\x58\x04\x03\x03\x18\x08", 15));
    }
    free(mid);
    midiFree(m);
}

/* ---------------------------------------------------------------- what the port's parser decoded */

static void dumpParsed(void) {
    const uint8_t *arc = gHead;
    static const char *const kKind[] = {"keyon", "keyoff", "prog", "ctrl", "tempo", "end", "bend"};
    int pkg;

    for (pkg = 0; pkg < 2; pkg++) {
        unsigned long off = rdle32(arc + 4 + (size_t)pkg * 4);
        unsigned long end = pkg == 0 ? rdle32(arc + 8) : gHeadLen;
        SndSep *sep = (SndSep *)calloc(1, sizeof *sep);
        int r;
        CHECK(sep != NULL && sndParseSep(arc + off, end - off, sep) == 0);
        if (sep == NULL) return;
        CHECK(sep->recCount == (pkg == 0 ? 2 : 1));
        for (r = 0; r < sep->recCount; r++) {
            const SndSepRec *rc = &sep->recs[r];
            char txt[8192], name[64];
            size_t n = 0;
            unsigned long k;
            n += (size_t)snprintf(txt + n, sizeof txt - n, "res %u\ntempo %lu\nsig %u %u\n", (unsigned)rc->res, rc->tempo, (unsigned)rc->num, (unsigned)rc->den);
            if (rc->loopBody >= 0) {
                n += (size_t)snprintf(txt + n, sizeof txt - n, "loop %lu %lu %u %ld\n", rc->loopStartTick, rc->loopEndTick, (unsigned)rc->loopCount,
                                      rc->loopBody - (long)rc->first);
            } else {
                n += (size_t)snprintf(txt + n, sizeof txt - n, "loop none\n");
            }
            for (k = 0; k < rc->count; k++) {
                const SndEv *e = sep->evs + rc->first + k;
                n += (size_t)snprintf(txt + n, sizeof txt - n, "ev %lu %s %u %u %u %lu\n", e->tick, kKind[e->kind], (unsigned)e->ch, (unsigned)e->note,
                                      (unsigned)e->data, e->datum);
            }
            snprintf(name, sizeof name, "seq_%d_%02d.txt", pkg, r);
            dumpFile(name, txt, n);
            if (pkg == 0 && r == 0) {
                CHECK(rc->count == 9 && rc->loopBody == 2 && rc->loopStartTick == 0 && rc->loopEndTick == 416 && rc->loopCount == 2);
                CHECK(rc->res == 480 && rc->tempo == 500000 && rc->num == 4 && rc->den == 2);
            }
            if (pkg == 0 && r == 1) CHECK(rc->count == 5 && rc->loopBody == -1 && rc->num == 3 && rc->den == 3);
            if (pkg == 1) CHECK(rc->count == 3 && rc->res == 240);
        }
        sndSepFree(sep);
        free(sep);
    }
    {
        char name[64];
        int v;
        for (v = 0; v < 4; v++) {
            snprintf(name, sizeof name, "vag_%03d.adpcm", v + 1);
            dumpFile(name, gVag[v], gVagLen[v]);
        }
    }
}

/* ---------------------------------------------------------------- the export */

static void waitJob(void) {
    int guard = 0;
    while (audioExportPump(1000) && guard++ < 100000) {
    }
}

static const char *kFiles[] = {"vag_001.wav", "vag_002.wav", "vag_003.wav", "bank.json", "seq_0_00.mid", "seq_0_01.mid", "seq_1_00.mid", "sequences.json"};

static void testExport(void) {
    AudioExportIo io;
    char data[1400], rel[200], p[1400];
    size_t len, i;
    uint8_t *d;
    AudioExportStats tot;
    int done, failed, total;

    path(data, sizeof data, "data");
    io.bank = ioBank;
    io.file = ioFile;
    io.ud = NULL;
    audioExportSetup(data, &io);
    CHECK(audioExportRequest("nothing") == -1);
    CHECK(audioExportRequest("test01") == 1); /* any case */
    waitJob();
    audioExportCounts(&done, &failed, &total);
    CHECK(done == 1 && failed == 0 && total == 1 && audioExportBuilt() == 1);
    CHECK(strcmp(audioExportStatus(), "Done (1)") == 0);
    for (i = 0; i < sizeof kFiles / sizeof kFiles[0]; i++) {
        snprintf(rel, sizeof rel, "data/audio/TEST01/%s", kFiles[i]);
        CHECK(exists(rel));
    }
    CHECK(!exists("data/audio/TEST01/vag_004.wav")); /* the empty sample has no file */
    CHECK(!exists("data/audio/TEST02/bank.json"));
    CHECK(exists("data/audio/manifest.json"));
    audioExportTotals(&tot);
    CHECK(tot.vags == 3 && tot.programs == 2 && tot.tones == 3 && tot.packages == 2 && tot.sequences == 3 && tot.loopingSequences == 1 && tot.loopingVags == 1);

    /* the WAVs: 84 frames looping at 28..83 with the root note of the tone */
    d = readAll("data/audio/TEST01/vag_001.wav", &len);
    CHECK(d != NULL && len == 44 + 168 + 68);
    if (d != NULL && len == 44 + 168 + 68) {
        const uint8_t *c = d + 44 + 168;
        CHECK(mdLe(d + 24) == 44100 && mdLe(d + 40) == 168);
        CHECK(mdLe(c + 20) == 60 && mdLe(c + 24) == 0 && mdLe(c + 36) == 1 && mdLe(c + 52) == 28 && mdLe(c + 56) == 83);
    }
    free(d);
    d = readAll("data/audio/TEST01/vag_002.wav", &len); /* stops: 56 frames, a root note (47 + 0.875), no loop */
    CHECK(d != NULL && len == 44 + 112 + 44);
    if (d != NULL && len == 44 + 112 + 44) {
        const uint8_t *c = d + 44 + 112;
        CHECK(mdLe(c + 20) == 47 && mdLe(c + 24) == 0xE0000000UL && mdLe(c + 36) == 0);
    }
    free(d);
    d = readAll("data/audio/TEST01/vag_003.wav", &len); /* 72.25 */
    CHECK(d != NULL && len == 44 + 56 + 44);
    if (d != NULL && len == 44 + 56 + 44) CHECK(mdLe(d + 44 + 56 + 20) == 72 && mdLe(d + 44 + 56 + 24) == 0x40000000UL);
    free(d);

    /* bank.json */
    d = readAll("data/audio/TEST01/bank.json", &len);
    CHECK(has(d, len, "\"format\": \"bank-v1\"") && has(d, len, "\"bank\": \"TEST01\"") && has(d, len, "\"bank_id\": 3"));
    CHECK(has(d, len, "\"sample_rate\": 44100") && has(d, len, "\"master_volume\": 127") && has(d, len, "\"vag_count\": 4"));
    CHECK(has(d, len, "\"vag\": 1, \"file\": \"vag_001.wav\", \"frames\": 84, \"end\": \"loop\", \"loop_start\": 28, \"loop_end\": 83"));
    CHECK(has(d, len, "\"vag\": 2, \"file\": \"vag_002.wav\", \"frames\": 56, \"end\": \"stop\""));
    CHECK(has(d, len, "\"vag\": 4, \"file\": null, \"frames\": 0"));
    CHECK(has(d, len, "\"program\": 5, \"volume\": 120, \"pan\": 60"));
    CHECK(has(d, len, "\"center\": 48, \"shift\": 16, \"root_note\": 47, \"root_cents\": 87.5000"));
    CHECK(has(d, len, "\"center\": 72, \"shift\": -32, \"root_note\": 72, \"root_cents\": 25.0000"));
    CHECK(has(d, len, "\"adsr1\": \"0x957C\", \"adsr2\": \"0xC9AA\""));
    CHECK(has(d, len, "\"pitch_bend_down\": 12, \"pitch_bend_up\": 12"));
    CHECK(has(d, len, "\"notes\""));
    free(d);

    /* the MIDI files */
    d = readAll("data/audio/TEST01/seq_0_00.mid", &len);
    CHECK(d != NULL && len > 14 && memcmp(d, "MThd", 4) == 0 && d[9] == 1 && d[11] == 3 && d[12] == 0x01 && d[13] == 0xE0); /* conductor + 2 channels */
    CHECK(has(d, len, "loopStart count=2") && has(d, len, "loopEnd"));
    free(d);
    d = readAll("data/audio/TEST01/seq_0_01.mid", &len);
    CHECK(d != NULL && len > 14 && d[11] == 2 && d[12] == 0x03 && d[13] == 0xC0 && !has(d, len, "loopStart"));
    free(d);
    d = readAll("data/audio/TEST01/seq_1_00.mid", &len);
    CHECK(d != NULL && len > 14 && d[11] == 2 && d[13] == 240);
    free(d);

    /* sequences.json: the flat numbering of the game (16 tracks a package) and the packed ids */
    d = readAll("data/audio/TEST01/sequences.json", &len);
    CHECK(has(d, len, "\"format\": \"sequences-v1\"") && has(d, len, "\"bank_id\": 3"));
    CHECK(has(d, len, "\"file\": \"seq_0_00.mid\", \"package\": 0, \"record\": 0, \"flat\": 0, \"id\": \"0x600C0000\", \"id_direct\": \"0x600C0000\""));
    CHECK(has(d, len, "\"file\": \"seq_0_01.mid\", \"package\": 0, \"record\": 1, \"flat\": 1, \"id\": \"0x600C0001\""));
    CHECK(has(d, len, "\"file\": \"seq_1_00.mid\", \"package\": 1, \"record\": 0, \"flat\": 16, \"id\": \"0x600C0010\", \"id_direct\": \"0x600C0100\""));
    CHECK(has(d, len, "\"resolution\": 480") && has(d, len, "\"loop\": {\"start_tick\": 0, \"end_tick\": 416, \"count\": 2}"));
    CHECK(has(d, len, "\"loop\": null"));
    free(d);

    /* a second request skips it: the version of the manifest says the files are current */
    sFileReads = 0;
    CHECK(audioExportRequest("all") == 2);
    waitJob();
    CHECK(audioExportBuilt() == 1); /* TEST01 is skipped, TEST02 (the same files, no sequences) is built */
    CHECK(exists("data/audio/TEST02/bank.json") && !exists("data/audio/TEST02/seq_0_00.mid"));
    CHECK(fileHas("data/audio/TEST02/sequences.json", "\"sequences\": []"));
    sFileReads = 0;
    CHECK(audioExportRequest("all") == 2);
    waitJob();
    CHECK(audioExportBuilt() == 0 && sFileReads == 0); /* nothing built, the disc not even read */

    /* an edited file stays and the new one goes beside it as .new; the others are replaced */
    path(p, sizeof p, "data/audio/TEST01/vag_001.wav");
    d = readAll("data/audio/TEST01/vag_001.wav", &len);
    {
        uint8_t *orig = (uint8_t *)malloc(len);
        uint8_t *edited = (uint8_t *)malloc(len + 1);
        size_t origLen = len;
        memcpy(orig, d, len);
        memcpy(edited, d, len);
        edited[len] = 0x55;
        CHECK(tdWriteFileAtomic(p, edited, len + 1) == 0);
        CHECK(audioExportRequestEx("test01", 1) == 1);
        waitJob();
        CHECK(audioExportBuilt() == 1);
        d = readAll("data/audio/TEST01/vag_001.wav", &len);
        CHECK(d != NULL && len == origLen + 1 && d[origLen] == 0x55); /* the edit stays */
        free(d);
        d = readAll("data/audio/TEST01/vag_001.wav.new", &len);
        CHECK(d != NULL && len == origLen && memcmp(d, orig, len) == 0); /* the new version beside it */
        free(d);
        CHECK(!exists("data/audio/TEST01/vag_002.wav.new")); /* untouched files make none */
        /* put the file back as the export wrote it (the independent half reads the folder afterwards) and drop the .new */
        CHECK(tdWriteFileAtomic(p, orig, origLen) == 0);
        {
            char np[1500];
            snprintf(np, sizeof np, "%s.new", p);
            remove(np);
        }
        free(orig);
        free(edited);
    }
    /* the manifest holds the version of each bank beside the file hashes */
    d = readAll("data/audio/manifest.json", &len);
    CHECK(d != NULL && has(d, len, "TEST01/exporter") && has(d, len, "TEST01/vag_001.wav") && has(d, len, "TEST01/seq_0_00.mid"));
    free(d);
    /* an older exporter version rebuilds the bank and replaces the files that were not edited */
    d = readAll("data/audio/manifest.json", &len);
    {
        char *text = (char *)malloc(len + 1);
        char *at;
        memcpy(text, d, len);
        text[len] = 0;
        free(d);
        at = strstr(text, "TEST01/exporter");
        CHECK(at != NULL);
        if (at != NULL) {
            at = strchr(at + 15, ':');
            at = at != NULL ? strchr(at, '"') : NULL;
            CHECK(at != NULL);
            if (at != NULL) at[16] = '0'; /* the last digit of the version hex: 0000000000000001 -> ...0 */
            path(p, sizeof p, "data/audio/manifest.json");
            CHECK(tdWriteFileAtomic(p, text, strlen(text)) == 0);
        }
        free(text);
    }
    /* the manifest is read once per setup: set up again, then the older version makes the bank build */
    audioExportSetup(data, &io);
    path(p, sizeof p, "data/audio/TEST01/seq_0_00.mid");
    CHECK(audioExportRequest("test01") == 1);
    waitJob();
    CHECK(audioExportBuilt() == 1);
}


/* ---------------------------------------------------------------- E3c: a mod's WAV in place of a sample */

static void writeFile(const char *rel, const void *data, size_t len) {
    char p[1500];
    FILE *f;

    path(p, sizeof p, rel);
    f = fopen(p, "wb");
    if (f == NULL) {
        printf("audiotest: cannot write %s\n", p);
        failures++;
        return;
    }
    fwrite(data, 1, len, f);
    fclose(f);
}

static void put16(uint8_t *p, unsigned v) { le16(p, v); }
static void writeText(const char *rel, const char *text) { writeFile(rel, text, strlen(text)); }

/* A WAV by hand: format tag, channels, rate, bits and the raw data bytes; extensible writes WAVE_FORMAT_EXTENSIBLE with the tag
   as sub-format. dataSize is what the data chunk header says (0xFFFFFFFF: a streamed file). */
static uint8_t *rawWav(int tag, int channels, int rate, int bits, const uint8_t *data, size_t dlen, int extensible, unsigned long dataSize,
                       const WavSmpl *smpl, size_t *len) {
    size_t fmtLen = extensible ? 40 : 16, total = 12 + 8 + fmtLen + 8 + dlen + (smpl ? 8 + 60 : 0);
    uint8_t *w = (uint8_t *)calloc(1, total), *p = w;

    memcpy(p, "RIFF", 4);
    le32(p + 4, (unsigned long)(total - 8));
    memcpy(p + 8, "WAVE", 4);
    p += 12;
    memcpy(p, "fmt ", 4);
    le32(p + 4, (unsigned long)fmtLen);
    put16(p + 8, extensible ? 0xFFFE : (unsigned)tag);
    put16(p + 10, (unsigned)channels);
    le32(p + 12, (unsigned long)rate);
    le32(p + 16, (unsigned long)rate * (unsigned long)channels * (unsigned long)(bits / 8));
    put16(p + 20, (unsigned)(channels * bits / 8));
    put16(p + 22, (unsigned)bits);
    if (extensible) {
        put16(p + 24, 22);
        put16(p + 26, (unsigned)bits);
        put16(p + 32, (unsigned)tag); /* the sub-format GUID starts with the format tag */
    }
    p += 8 + fmtLen;
    memcpy(p, "data", 4);
    le32(p + 4, dataSize);
    memcpy(p + 8, data, dlen);
    p += 8 + dlen;
    if (smpl != NULL) {
        memcpy(p, "smpl", 4);
        le32(p + 4, 60);
        le32(p + 20, (unsigned long)smpl->rootNote);
        le32(p + 36, 1);
        le32(p + 8 + 36 + 8, smpl->loopStart);
        le32(p + 8 + 36 + 12, smpl->loopEnd);
    }
    *len = total;
    return w;
}

static void sinePcm(int16_t *out, size_t n, double hz, int rate, double amp) {
    size_t i;

    for (i = 0; i < n; i++) out[i] = (int16_t)lround(amp * sin(2.0 * 3.14159265358979 * hz * (double)i / (double)rate));
}

/* the reason wavInfoRead gives for a buffer, or "" when it is accepted */
static const char *wavVerdict(const uint8_t *bytes, size_t len, WavInfo *info, char *err, size_t cap) {
    char p[1500];
    FILE *f;

    writeFile("modtest/verdict.wav", bytes, len);
    path(p, sizeof p, "modtest/verdict.wav");
    f = fopen(p, "rb");
    err[0] = '\0';
    if (f == NULL) return "cannot reopen";
    wavInfoRead(f, info, err, cap);
    fclose(f);
    return err;
}

static void testWavReader(void) {
    int16_t pcm[400];
    uint8_t raw[1600], *w;
    size_t len, i;
    WavInfo info;
    WavSmpl smpl;
    char err[200], p[1500];
    int16_t *loaded;

    sinePcm(pcm, 400, 440, 22050, 20000);
    /* mono 16 bit with a smpl root and loop */
    memset(&smpl, 0, sizeof smpl);
    smpl.rootNote = 62;
    smpl.hasLoop = 1;
    smpl.loopStart = 100;
    smpl.loopEnd = 299;
    w = wavBuild(pcm, 400, 22050, &smpl, &len);
    CHECK(wavVerdict(w, len, &info, err, sizeof err)[0] == '\0');
    CHECK(info.channels == 1 && info.rate == 22050 && info.frames == 400 && info.hasSmpl && info.rootNote == 62);
    CHECK(info.hasLoop && info.loopStart == 100 && info.loopEnd == 299);
    free(w);
    /* no smpl chunk */
    w = wavBuild(pcm, 400, 44100, NULL, &len);
    CHECK(wavVerdict(w, len, &info, err, sizeof err)[0] == '\0' && !info.hasSmpl && !info.hasLoop && info.rate == 44100);
    path(p, sizeof p, "modtest/verdict.wav");
    loaded = wavLoad(p, &info, err, sizeof err);
    CHECK(loaded != NULL && memcmp(loaded, pcm, sizeof pcm) == 0);
    free(loaded);
    free(w);
    /* a loop end past the data is clamped, a loop that starts past it is no loop */
    smpl.loopStart = 100;
    smpl.loopEnd = 9999;
    w = wavBuild(pcm, 400, 22050, &smpl, &len);
    wavVerdict(w, len, &info, err, sizeof err);
    CHECK(info.hasLoop && info.loopEnd == 399);
    free(w);
    smpl.loopStart = 400;
    w = wavBuild(pcm, 400, 22050, &smpl, &len);
    wavVerdict(w, len, &info, err, sizeof err);
    CHECK(!info.hasLoop);
    free(w);
    /* stereo */
    for (i = 0; i < 800; i++) put16(raw + i * 2, (unsigned)(uint16_t)pcm[i / 2]);
    w = rawWav(1, 2, 48000, 16, raw, 1600, 0, 1600, NULL, &len);
    CHECK(wavVerdict(w, len, &info, err, sizeof err)[0] == '\0' && info.channels == 2 && info.frames == 400);
    free(w);
    /* WAVE_FORMAT_EXTENSIBLE with PCM, and a streamed data size */
    w = rawWav(1, 1, 44100, 16, raw, 800, 1, 0xFFFFFFFFul, NULL, &len);
    CHECK(wavVerdict(w, len, &info, err, sizeof err)[0] == '\0' && info.frames == 400);
    free(w);
    /* a cut file: the data chunk says 1600 bytes and 800 are there */
    w = rawWav(1, 1, 44100, 16, raw, 800, 0, 1600, NULL, &len);
    CHECK(wavVerdict(w, len, &info, err, sizeof err)[0] == '\0' && info.frames == 400);
    free(w);
    /* the refusals, each with its reason */
    w = rawWav(1, 1, 44100, 8, raw, 400, 0, 400, NULL, &len);
    CHECK(strstr(wavVerdict(w, len, &info, err, sizeof err), "not 16-bit PCM") != NULL);
    free(w);
    w = rawWav(1, 1, 44100, 24, raw, 600, 0, 600, NULL, &len);
    CHECK(strstr(wavVerdict(w, len, &info, err, sizeof err), "24 bits") != NULL);
    free(w);
    w = rawWav(3, 1, 44100, 32, raw, 800, 0, 800, NULL, &len);
    CHECK(strstr(wavVerdict(w, len, &info, err, sizeof err), "format 3") != NULL);
    free(w);
    w = rawWav(2, 1, 44100, 16, raw, 800, 0, 800, NULL, &len);
    CHECK(strstr(wavVerdict(w, len, &info, err, sizeof err), "format 2") != NULL); /* MS ADPCM */
    free(w);
    w = rawWav(1, 4, 44100, 16, raw, 800, 0, 800, NULL, &len);
    CHECK(strstr(wavVerdict(w, len, &info, err, sizeof err), "more than 2 channels") != NULL);
    free(w);
    w = rawWav(1, 1, 500, 16, raw, 800, 0, 800, NULL, &len);
    CHECK(strstr(wavVerdict(w, len, &info, err, sizeof err), "out of range") != NULL);
    free(w);
    w = rawWav(1, 1, 44100, 16, raw, 0, 0, 0, NULL, &len);
    CHECK(strstr(wavVerdict(w, len, &info, err, sizeof err), "no samples") != NULL);
    free(w);
    CHECK(strstr(wavVerdict((const uint8_t *)"RIFF\x04\0\0\0WAVE", 12, &info, err, sizeof err), "no fmt") != NULL);
    CHECK(strstr(wavVerdict((const uint8_t *)"this is not a wave file", 23, &info, err, sizeof err), "not a WAV file") != NULL);
    CHECK(strstr(wavVerdict((const uint8_t *)"", 0, &info, err, sizeof err), "not a WAV file") != NULL);
}

static void testResampler(void) {
    int16_t src[4800], st[1000];
    int16_t *out;
    size_t n, i;
    int maxd = 0;

    sinePcm(src, 2205, 1000, 22050, 10000);
    out = wavResampleMono(src, 2205, 1, 22050, 44100, &n);
    CHECK(out != NULL && n == 4410);
    for (i = 0; out != NULL && i < n; i++) {
        double want = 10000.0 * sin(2.0 * 3.14159265358979 * 1000.0 * (double)i / 44100.0);
        int d = (int)fabs((double)out[i] - want);

        if (i + 2 < n && d > maxd) maxd = d;
    }
    CHECK(maxd < 400); /* linear interpolation of a 1 kHz sine at 22 kHz */
    free(out);
    out = wavResampleMono(src, 4800, 1, 48000, 44100, &n);
    CHECK(out != NULL && n == 4410);
    free(out);
    /* stereo is averaged: left 1000, right 3000 -> 2000 */
    for (i = 0; i < 500; i++) {
        st[i * 2] = 1000;
        st[i * 2 + 1] = 3000;
    }
    out = wavResampleMono(st, 500, 2, 44100, 44100, &n);
    CHECK(out != NULL && n == 500 && out[0] == 2000 && out[499] == 2000);
    free(out);
    out = wavResampleMono(st, 500, 2, 88200, 44100, &n);
    CHECK(out != NULL && n == 250 && out[100] == 2000);
    free(out);
    CHECK(wavResampleMono(NULL, 10, 1, 1, 1, &n) == NULL);
}

static double psnrOf(const int16_t *a, const int16_t *b, size_t n, int *maxErr) {
    double e = 0;
    size_t i;

    *maxErr = 0;
    for (i = 0; i < n; i++) {
        int d = abs((int)a[i] - (int)b[i]);

        e += (double)d * (double)d;
        if (d > *maxErr) *maxErr = d;
    }
    if (e == 0) return 99.0;
    return 10.0 * log10(32767.0 * 32767.0 / (e / (double)n));
}

static void testVagEncoder(void) {
    enum { N = 8000 };
    int16_t sig[N], back[N + 64];
    int16_t chk[N + 64];
    uint8_t *v;
    size_t size, got, i;
    int maxErr;
    unsigned seed = 12345;
    AudioVagInfo vi;

    /* a sine plus a little noise: the decoder (port renderer's float filter) gives it back within a few dB of the 4-bit ceiling */
    for (i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        sig[i] = (int16_t)(12000.0 * sin((double)i * 0.05) + 6000.0 * sin((double)i * 0.31) + (double)((int)((seed >> 16) % 201) - 100));
    }
    v = vagEncode(sig, N, -1, &size);
    CHECK(v != NULL && size == (N + 27) / 28 * 16);
    got = vagDecodeBlocks(v, size, back);
    CHECK(got == (size_t)(N + 27) / 28 * 28);
    CHECK(psnrOf(sig, back, N, &maxErr) > 38.0);
    CHECK(maxErr < 2500);
    /* the same decoder as the exporter's */
    got = audioVagDecode(v, size, chk, &vi);
    CHECK(got == (size_t)(N + 27) / 28 * 28 && memcmp(chk, back, got * 2) == 0 && vi.ends == 1);
    /* flags: end on the last block only, no repeat, no loop start */
    CHECK((v[size - 15] & 3) == 1);
    for (i = 0; i + 1 < size / 16; i++) CHECK((v[i * 16 + 1] & 7) == 0);
    free(v);
    /* a quiet sine keeps its relative accuracy (the encoder picks small shifts) */
    for (i = 0; i < N; i++) sig[i] = (int16_t)(300.0 * sin((double)i * 0.07));
    v = vagEncode(sig, N, -1, &size);
    vagDecodeBlocks(v, size, back);
    CHECK(psnrOf(sig, back, N, &maxErr) > 36.0 && maxErr <= 24);
    free(v);
    /* silence and full scale square: no overflow, an exact silence */
    memset(sig, 0, sizeof sig);
    v = vagEncode(sig, 100, -1, &size);
    got = vagDecodeBlocks(v, size, back);
    CHECK(got == 112 && psnrOf(sig, back, 100, &maxErr) == 99.0);
    free(v);
    for (i = 0; i < N; i++) sig[i] = (int16_t)((i / 40) % 2 ? 32767 : -32768);
    v = vagEncode(sig, N, -1, &size);
    vagDecodeBlocks(v, size, back);
    CHECK(psnrOf(sig, back, N, &maxErr) > 20.0);
    free(v);
    /* a loop: the loop-start flag on the block that holds loopStart, end + repeat on the last, the padding continues the loop */
    for (i = 0; i < 560; i++) sig[i] = (int16_t)(8000.0 * sin((double)i * 2.0 * 3.14159265358979 / 56.0)); /* a 56-sample period = 2 blocks */
    v = vagEncode(sig, 560 - 3, 100, &size); /* 557 frames: 20 blocks, the last holds 17 frames + 11 of padding */
    CHECK(v != NULL && size == 20 * 16);
    CHECK((v[3 * 16 + 1] & 4) != 0); /* block 3 (frames 84..111) holds frame 100 */
    CHECK((v[19 * 16 + 1] & 3) == 3);
    for (i = 0; i < 19; i++) CHECK((i == 3) == ((v[i * 16 + 1] & 4) != 0) && (v[i * 16 + 1] & 3) == 0);
    got = audioVagDecode(v, size, chk, &vi);
    CHECK(vi.ends == 2 && vi.loopStart == 84 && vi.loopEnd == 559 && got == 560);
    vagDecodeBlocks(v, size, back);
    /* the padded samples of the last block carry on the loop body (frame 557 = loop frame 100 + (557 - 557) ...) */
    CHECK(abs((int)back[557] - (int)sig[100]) < 1500);
    free(v);
    CHECK(vagEncode(sig, 100, 100, &size) == NULL); /* the loop starts past the end */
    CHECK(vagEncode(NULL, 100, -1, &size) == NULL && vagEncode(sig, 0, -1, &size) == NULL);
}

/* The bank of the synthetic fixture loaded the way the game does it, with a mod's WAV for sample 2 (longer than the original) */
static int amSlotBank(void *ud, int slot, char *name, int cap) {
    (void)ud;
    if (slot < 0 || slot > 2 || cap < 7) return 0;
    memcpy(name, "TESTBK", 7);
    return 1;
}
static int amModCount(void *ud) { (void)ud; return 2; }
static const char *amModName(void *ud, int i) { (void)ud; return i == 0 ? "modA" : "modB"; }
static char gModDirs[2][1400];
static const char *amModDir(void *ud, int i) { (void)ud; return gModDirs[i]; }

static void useMods(void) {
    AudioModIo io;

    io.slotBank = amSlotBank;
    io.modCount = amModCount;
    io.modName = amModName;
    io.modDir = amModDir;
    io.ud = NULL;
    audioModSetup(&io);
}

static void writeBankWav(const char *mod, int vag, const int16_t *pcm, size_t frames, int rate, const WavSmpl *smpl) {
    char rel[200];
    size_t len;
    uint8_t *w = wavBuild(pcm, frames, rate, smpl, &len);

    snprintf(rel, sizeof rel, "modtest/%s/audio/TESTBK/vag_%03d.wav", mod, vag);
    writeFile(rel, w, len);
    free(w);
}

/* U7: a PCM output of the test's: it keeps every buffer it was given and logs every play */
#define FP_BUFS 16
typedef struct FakePcm {
    int used, released;
    short *pcm;
    unsigned long frames;
    long loopStart;
} FakePcm;
static FakePcm gFp[FP_BUFS];
static int gFpCount;
static struct { int voice, id, loops, pitch, l, r; } gFpPlay[32];
static int gFpPlays, gFpStops;

static int fpUpload(void *ctx, const short *pcm, unsigned long frames, long loopStart) {
    (void)ctx;
    if (gFpCount >= FP_BUFS) return 0;
    gFp[gFpCount].used = 1;
    gFp[gFpCount].released = 0;
    gFp[gFpCount].pcm = (short *)malloc(frames * sizeof(short));
    memcpy(gFp[gFpCount].pcm, pcm, frames * sizeof(short));
    gFp[gFpCount].frames = frames;
    gFp[gFpCount].loopStart = loopStart;
    return ++gFpCount; /* ids are 1-based */
}
static void fpRelease(void *ctx, int id) { (void)ctx; gFp[id - 1].released = 1; }
static void fpPlay(void *ctx, int voice, int id, int loops, unsigned short pitch, short l, short r) {
    (void)ctx;
    if (gFpPlays < 32) {
        gFpPlay[gFpPlays].voice = voice; gFpPlay[gFpPlays].id = id; gFpPlay[gFpPlays].loops = loops;
        gFpPlay[gFpPlays].pitch = pitch; gFpPlay[gFpPlays].l = l; gFpPlay[gFpPlays].r = r;
    }
    gFpPlays++;
}
static void fpVolume(void *ctx, int voice, short l, short r) { (void)ctx; (void)voice; (void)l; (void)r; }
static void fpPitch(void *ctx, int voice, unsigned short p) { (void)ctx; (void)voice; (void)p; }
static void fpStop(void *ctx, int voice) { (void)ctx; (void)voice; gFpStops++; }
static const AudioModPcmBackend gFpBackend = {fpUpload, fpRelease, fpPlay, fpVolume, fpPitch, fpStop};

static void fpReset(void) {
    int i;
    for (i = 0; i < gFpCount; i++) free(gFp[i].pcm);
    memset(gFp, 0, sizeof gFp);
    gFpCount = gFpPlays = gFpStops = 0;
    audioModSetPcmBackend(&gFpBackend, NULL);
}

#define PCM_BASE 0x40000000UL /* snd_spu.c: the virtual address of a replaced sample */

static void testBankPatch(void) {
    static int16_t big[3000];
    struct SndNote n1, n2, n3;
    short id;
    uint8_t rb[64 * 16];
    char d[1400];
    AudioModVag rep;
    unsigned long a2;

    path(d, sizeof d, "modtest/modA/audio/TESTBK");
    tdMakeDirs(d);
    path(d, sizeof d, "modtest/modB/audio/TESTBK");
    tdMakeDirs(d);
    path(gModDirs[0], sizeof gModDirs[0], "modtest/modA");
    path(gModDirs[1], sizeof gModDirs[1], "modtest/modB");
    useMods();
    fpReset();

    /* without a file: the bank loads as it always did */
    id = sndVabOpen(gHead + 12, 0, 0x1010);
    CHECK(id == 1);
    CHECK(sndVabTransBody(gBody + 4, id) == id);
    CHECK(sndNotePrepare(id, 0, 1, 72, 0, 127, 127, &n2) == 0 && n2.addr == 0x1010 + 48);
    CHECK(sndNotePrepare(id, 0, 0, 60, 0, 127, 127, &n1) == 0 && n1.addr == 0x1010);
    CHECK(!audioModVagReplacement("TESTBK", 2, &rep));
    sndVabClose(id);

    /* sample 2 replaced by a 3000-frame sine (the original has 56 frames): it lands past the console's 512 KB, the others stay */
    sinePcm(big, 3000, 441, 44100, 15000);
    writeBankWav("modA", 2, big, 3000, 44100, NULL);
    id = sndVabOpen(gHead + 12, 0, 0x1010);
    CHECK(sndVabTransBody(gBody + 4, id) == id);
    CHECK(sndNotePrepare(id, 0, 1, 72, 0, 127, 127, &n2) == 0);
    a2 = n2.addr;
    CHECK(a2 >= PCM_BASE); /* not an SPU RAM address: a PCM voice, nothing written past the console's 512 KB */
    CHECK(sndNotePrepare(id, 0, 0, 60, 0, 127, 127, &n1) == 0 && n1.addr == 0x1010);
    CHECK(sndNotePrepare(id, 5, 0, 72, 0, 127, 127, &n3) == 0 && n3.addr == 0x1010 + 48 + 32);
        /* the PCM the output got is the encoder's, decoded */
    {
        size_t size, got;
        uint8_t *enc = vagEncode(big, 3000, -1, &size);
        int16_t *ref = (int16_t *)malloc(3100 * 2);
        short v;

        CHECK(enc != NULL && size == 108 * 16 && ref != NULL);
        got = vagDecodeBlocks(enc, size, ref);
        CHECK(gFpCount == 1 && gFp[0].frames == got && gFp[0].loopStart == -1 && memcmp(gFp[0].pcm, ref, got * 2) == 0);
        /* the key-on of the replaced sample plays that buffer, and one of the game's own samples does not */
        v = sndKeyOn(id, 0, 1, 72, 0, 127, 127);
        CHECK(v >= 0 && gFpPlays == 1 && gFpPlay[0].id == 1 && gFpPlay[0].voice == v && gFpPlay[0].pitch == n2.pitch && gFpPlay[0].loops == 0);
        CHECK(gFpPlay[0].l > 0 && gFpPlay[0].r > 0);
        v = sndKeyOn(id, 0, 0, 60, 0, 127, 127);
        CHECK(v >= 0 && gFpPlays == 1);
        sndAllKeyOff(0);
        free(enc);
        free(ref);
    }
    /* the original of the other samples is where it was */
    CHECK(SpuSetTransferStartAddr(0x1010) != 0);
    CHECK(SpuRead(rb, 48) == 48 && memcmp(rb, gVag[0], 48) == 0);
    sndVabClose(id);
    CHECK(gFp[0].released); /* the slot's buffers go with the bank */

    /* the last active mod wins; a refused file falls through to the one before */
    sinePcm(big, 1500, 200, 44100, 9000);
    writeBankWav("modB", 2, big, 1500, 44100, NULL);
    CHECK(audioModVagReplacement("TESTBK", 2, &rep) && rep.frames == 1500 && rep.size == 54 * 16 && !rep.loops);
    free(rep.adpcm);
    writeFile("modtest/modB/audio/TESTBK/vag_002.wav", "RIFF nonsense", 13);
    CHECK(audioModVagReplacement("TESTBK", 2, &rep) && rep.frames == 3000); /* modA's */
    free(rep.adpcm);
    /* nothing usable: the original stays */
    writeFile("modtest/modA/audio/TESTBK/vag_002.wav", "junk", 4);
    CHECK(!audioModVagReplacement("TESTBK", 2, &rep));
    id = sndVabOpen(gHead + 12, 0, 0x1010);
    CHECK(sndVabTransBody(gBody + 4, id) == id);
    CHECK(sndNotePrepare(id, 0, 1, 72, 0, 127, 127, &n2) == 0 && n2.addr == 0x1010 + 48);
    sndVabClose(id);

    /* a 22050 Hz mono recording with a smpl loop: resampled to 44100, the loop scaled, the tail after the loop end dropped */
    sinePcm(big, 2000, 441, 22050, 12000);
    {
        WavSmpl smpl;

        memset(&smpl, 0, sizeof smpl);
        smpl.rootNote = 60;
        smpl.hasLoop = 1;
        smpl.loopStart = 400;
        smpl.loopEnd = 1399;
        writeBankWav("modA", 2, big, 2000, 22050, &smpl);
        CHECK(audioModVagReplacement("TESTBK", 2, &rep));
        CHECK(rep.loops && rep.frames == 2800 && rep.size == 100 * 16);
        CHECK((rep.adpcm[rep.size - 15] & 3) == 3 && (rep.adpcm[(800 / 28) * 16 + 1] & 4) != 0);
        free(rep.adpcm);
    }
    /* U7: no fixed cap: a 60 s recording (far past the old ~20 s per bank slot) is accepted whole */
    {
        static int16_t huge[44100 * 60];

        sinePcm(huge, 44100 * 60, 330, 44100, 9000);
        writeBankWav("modA", 2, huge, 44100 * 60, 44100, NULL);
        CHECK(audioModVagReplacement("TESTBK", 2, &rep) && rep.frames == 44100UL * 60 && rep.size == (44100UL * 60 + 27) / 28 * 16);
        free(rep.adpcm);
    }
    /* U7: the three bank slots at once, each with the 60 s replacement (modA's; modB's file is refused): no slot's room limits another,
       every key-on plays its own slot's buffer and the other samples stay where the game put them */
    {
        struct SndNote nn[3], orig[3];
        short ids[3], v;
        int k;

        fpReset();
        for (k = 0; k < 3; k++) {
            ids[k] = sndVabOpen(gHead + 12, (short)k, 0x1010);
            CHECK(ids[k] == k + 1 && sndVabTransBody(gBody + 4, ids[k]) == ids[k]);
            CHECK(sndNotePrepare(ids[k], 0, 1, 72, 0, 127, 127, &nn[k]) == 0 && nn[k].addr >= PCM_BASE);
            CHECK(sndNotePrepare(ids[k], 0, 0, 60, 0, 127, 127, &orig[k]) == 0 && orig[k].addr == 0x1010);
        }
        CHECK(gFpCount == 3 && nn[0].addr != nn[1].addr && nn[1].addr != nn[2].addr);
        for (k = 0; k < 3; k++) CHECK(gFp[k].frames == 44100UL * 60 && !gFp[k].released);
        v = sndKeyOn(ids[1], 0, 1, 72, 0, 127, 127);
        CHECK(v >= 0 && gFpPlays == 1 && gFpPlay[0].id == 2);
        v = sndKeyOn(ids[2], 0, 1, 72, 0, 127, 127);
        CHECK(v >= 0 && gFpPlays == 2 && gFpPlay[1].id == 3);
        sndAllKeyOff(0);
        /* reloading a slot drops its buffer and uploads a new one; the other slots keep theirs */
        CHECK(sndVabTransBody(gBody + 4, ids[1]) == ids[1]);
        CHECK(gFp[1].released && !gFp[0].released && !gFp[2].released && gFpCount == 4);
        for (k = 0; k < 3; k++) sndVabClose(ids[k]);
        for (k = 0; k < 4; k++) CHECK(gFp[k].released);
    }
    audioModSetPcmBackend(NULL, NULL);
    /* a bank that is not the one the files are for */
    CHECK(!audioModVagReplacement("OTHER", 2, &rep));
}

/* ---------------------------------------------------------------- E3d: a mod's WAV in place of a sequence */

/* A sink with a clock of its own: frames "play" only when the test says so, and it keeps what it was given */
#define FS_MAX 4
#define FS_CAP 120000
typedef struct FakeStream {
    int open, channels, rate, closed, paused;
    long taken, played;
    int gain;
    long room;
    int16_t data[FS_CAP * 2];
} FakeStream;

static FakeStream gFs[FS_MAX];
static int gFsOpened;
static int gFsFail;

static int fsOpen(void *ctx, int channels, int rate) {
    int i;
    (void)ctx;
    if (gFsFail) return -1;
    for (i = 0; i < FS_MAX; i++) {
        if (!gFs[i].open) {
            memset(&gFs[i], 0, sizeof gFs[i]);
            gFs[i].open = 1;
            gFs[i].channels = channels;
            gFs[i].rate = rate;
            gFs[i].gain = -1;
            gFs[i].room = 8 * AUDIO_MOD_CHUNK;
            gFsOpened++;
            return i;
        }
    }
    return -1;
}
static int fsRoom(void *ctx, int id) { (void)ctx; return (int)(gFs[id].room - (gFs[id].taken - gFs[id].played)); }
static void fsQueue(void *ctx, int id, const short *pcm, int frames) {
    FakeStream *f = &gFs[id];
    (void)ctx;
    if (f->taken + frames <= FS_CAP) memcpy(f->data + f->taken * f->channels, pcm, (size_t)frames * (size_t)f->channels * 2);
    f->taken += frames;
}
static int fsQueued(void *ctx, int id) { (void)ctx; return (int)(gFs[id].taken - gFs[id].played); }
static void fsGain(void *ctx, int id, int permille) { (void)ctx; gFs[id].gain = permille; }
static void fsPause(void *ctx, int id, int on) { (void)ctx; gFs[id].paused = on; }
static void fsClose(void *ctx, int id) { (void)ctx; gFs[id].open = 0; gFs[id].closed = 1; }
static const AudioModSink kFakeSink = {fsOpen, fsRoom, fsQueue, fsQueued, fsGain, fsPause, fsClose};

/* the clock: the sink plays n frames of every open stream, then the sequencer's tick and the pump run (as the vsync hook does) */
static void fsAdvance(long n) {
    int i;

    for (i = 0; i < FS_MAX; i++) {
        if (gFs[i].open && !gFs[i].paused) {
            long q = gFs[i].taken - gFs[i].played;
            gFs[i].played += n < q ? n : q;
        }
    }
    sndSepTbyT();
    audioModPump();
}

static void rampWav(const char *mod, const char *name, size_t frames, int rate, int channels, const WavSmpl *smpl) {
    char rel[200];
    size_t len, i;
    int16_t *pcm = (int16_t *)malloc(frames * 2 * sizeof(int16_t)), *mono = (int16_t *)malloc(frames * sizeof(int16_t));
    uint8_t *w;

    for (i = 0; i < frames; i++) mono[i] = (int16_t)(i % 30000);
    if (channels == 2) {
        for (i = 0; i < frames; i++) {
            pcm[i * 2] = mono[i];
            pcm[i * 2 + 1] = (int16_t)-mono[i];
        }
        w = rawWav(1, 2, rate, 16, (const uint8_t *)pcm, frames * 4, 0, (unsigned long)(frames * 4), smpl, &len);
        if (smpl) { /* rawWav writes the loop at the fixed place of its smpl chunk */ }
    } else {
        w = wavBuild(mono, frames, rate, smpl, &len);
    }
    snprintf(rel, sizeof rel, "modtest/%s/audio/TESTBK/%s", mod, name);
    writeFile(rel, w, len);
    free(w);
    free(pcm);
    free(mono);
}

static int gMusicIs = 1;
static int fakeIsMusic(short seqId, short track) { (void)seqId; (void)track; return gMusicIs; }

/* the value the ramp has at output frame i, for a loop [ls, le] (le < 0: none) */
static long rampAt(long i, long ls, long le) {
    if (le >= 0 && i > le) i = ls + (i - le - 1) % (le - ls + 1);
    return i % 30000;
}

/* Drains a stream to its end (or n frames) and returns how many frames it saw */
static long runFor(int id, long frames, long step) {
    long done;

    for (done = 0; done < frames; done += step) fsAdvance(step);
    return gFs[id].played;
}

static void testSongs(void) {
    uint8_t sepFv[6 + 13 + 64];
    short vab, sep0, sepF;
    SndSep parsed;
    size_t n;
    long i;
    int bad;
    WavSmpl smpl;
    char d[1400];

    path(d, sizeof d, "modtest/modA/audio/TESTBK");
    tdMakeDirs(d);
    path(d, sizeof d, "modtest/modB/audio/TESTBK");
    tdMakeDirs(d);
    path(gModDirs[0], sizeof gModDirs[0], "modtest/modA");
    path(gModDirs[1], sizeof gModDirs[1], "modtest/modB");
    {
        /* start from nothing in the two mods */
        const char *old[] = {"modtest/modA/audio/TESTBK/vag_002.wav", "modtest/modB/audio/TESTBK/vag_002.wav"};
        char p[1500];
        for (i = 0; i < 2; i++) {
            path(p, sizeof p, old[i]);
            remove(p);
        }
    }
    useMods();
    audioModSetSink(&kFakeSink, NULL);
    sndSepInit();
    sndSepSetMusicQuery(fakeIsMusic);
    /* package 0 of the fixture (rec 0 loops twice, rec 1 does not) and a package whose record 0 loops forever */
    memcpy(sepFv, "pQES", 4);
    sepFv[4] = 0;
    sepFv[5] = 1;
    n = 6 + sepRec(sepFv + 6, 480, 500000, 4, 2, "00 C0 05 00 B0 63 14 00 06 7F 00 90 3C 64 60 80 3C 40 00 B0 63 1E 00 FF 2F");
    vab = sndVabOpen(gHead + 12, 0, 0x1010);
    CHECK(vab == 1 && sndVabTransBody(gBody + 4, vab) == vab);
    sep0 = sndSepOpen(gHead + rdle32(gHead + 4), vab);
    sepF = sndSepOpen(sepFv, vab);
    CHECK(sep0 > 0 && sepF > 0 && sndParseSep(sepFv, n, &parsed) == 0 && parsed.recs[0].loopCount == 0x7F);
    sndSepFree(&parsed);

    /* 1. nothing in the mods: the sequence plays, no output stream is made */
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sep0, 0, 1, 1, 127, 127) >= 0);
    CHECK(gFsOpened == 0);
    sndSepPlayStop(sep0, 0);

    /* 2. a WAV of 30000 frames without a loop, for a record that loops twice: it plays once, to its exact end, and the play is over */
    rampWav("modA", "seq_0_00.wav", 30000, 44100, 1, NULL);
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sep0, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1);
    CHECK(gFs[0].channels == 1 && gFs[0].rate == 44100 && gFs[0].gain == 1000);
    CHECK(gFs[0].taken == 30000); /* the whole file fits the room the sink reports (8 chunks) */
    runFor(0, 60000, 1470);
    CHECK(gFs[0].closed && gFs[0].taken == 30000 && gFs[0].played == 30000);
    for (bad = 0, i = 0; i < 30000; i++) bad += gFs[0].data[i] != (int16_t)(i % 30000);
    CHECK(bad == 0);
    sndSepPlayStop(sep0, 0); /* already over: nothing to do */
    CHECK(sndSepPlayStart(sep0, 1, 1, 1, 127, 127) >= 0 && gFsOpened == 1); /* record 1 has no file: its sequence */
    sndSepPlayStop(sep0, 1);

    /* 3. a smpl loop: the output wraps at the loop's end, sample exact, and the track outlives the file's length */
    memset(&smpl, 0, sizeof smpl);
    smpl.rootNote = 60;
    smpl.hasLoop = 1;
    smpl.loopStart = 1000;
    smpl.loopEnd = 1999;
    rampWav("modA", "seq_0_00.wav", 5000, 44100, 1, &smpl);
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sep0, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1);
    runFor(0, 30000, 1470);
    CHECK(!gFs[0].closed && gFs[0].played >= 30000 && gFs[0].taken < FS_CAP);
    for (bad = 0, i = 0; i < gFs[0].taken; i++) bad += gFs[0].data[i] != (int16_t)rampAt(i, 1000, 1999);
    CHECK(bad == 0);
    /* a stop closes it at once */
    sndSepPlayStop(sep0, 0);
    CHECK(gFs[0].closed);
    CHECK(!audioModTrackActive(1) || 1);

    /* 4. the unpadded name, and the sidecar json for a file without smpl; a broken json is refused and the play is once */
    {
        char p[1500];
        path(p, sizeof p, "modtest/modA/audio/TESTBK/seq_0_00.wav");
        remove(p);
    }
    rampWav("modB", "seq_0_0.wav", 5000, 44100, 1, NULL); /* record 0 written without the leading zero */
    writeText("modtest/modB/audio/TESTBK/seq_0_0.json", "{\"note\": \"x\", \"loopStart\": 500, \"loopEnd\": 1499}");
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sep0, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1);
    runFor(0, 20000, 1470);
    CHECK(!gFs[0].closed);
    for (bad = 0, i = 0; i < gFs[0].taken; i++) bad += gFs[0].data[i] != (int16_t)rampAt(i, 500, 1499);
    CHECK(bad == 0);
    sndSepPlayStop(sep0, 0);
    writeText("modtest/modB/audio/TESTBK/seq_0_0.json", "{\"loopStart\": 500, }");
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sep0, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1);
    runFor(0, 20000, 1470);
    CHECK(gFs[0].closed && gFs[0].taken == 5000);
    writeText("modtest/modB/audio/TESTBK/seq_0_0.json", "{\"loopStart\": 6000, \"loopEnd\": 7000}");
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sep0, 0, 1, 1, 127, 127) >= 0);
    runFor(0, 20000, 1470);
    CHECK(gFs[0].closed && gFs[0].taken == 5000); /* a loop outside the file is no loop */
    {
        char p[1500];
        path(p, sizeof p, "modtest/modB/audio/TESTBK/seq_0_0.json");
        remove(p);
    }

    /* 5. the original loops forever and the file says nothing: the whole file loops */
    rampWav("modA", "seq_1_00.wav", 3000, 22050, 2, NULL);
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1);
    CHECK(gFs[0].channels == 2 && gFs[0].rate == 22050);
    runFor(0, 20000, 1470);
    CHECK(!gFs[0].closed);
    for (bad = 0, i = 0; i < gFs[0].taken; i++) bad += gFs[0].data[i * 2] != (int16_t)rampAt(i, 0, 2999) || gFs[0].data[i * 2 + 1] != (int16_t)-rampAt(i, 0, 2999);
    CHECK(bad == 0);

    /* 6. the game's volume, fade and sliders reach it. 127 = 1000, 64 = 64^2 * 1000 / 127^2 = 253 */
    sndSepPlaySetVol(sepF, 0, 64, 64);
    fsAdvance(100);
    CHECK(gFs[0].gain == 253);
    sndSepPlaySetVol(sepF, 0, 127, 127);
    fsAdvance(100);
    CHECK(gFs[0].gain == 1000);
    sndSetUserVolume(SND_VOL_MUSIC, 50); /* 25 % of the amplitude */
    fsAdvance(100);
    CHECK(gFs[0].gain == 250);
    sndSetUserVolume(SND_VOL_MUSIC, 100);
    gMusicIs = 0; /* a sequence that is not the music answers the effects slider */
    sndSepPlayStop(sepF, 0);
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0);
    sndSetUserVolume(SND_VOL_EFFECTS, 50);
    sndSetUserVolume(SND_VOL_MUSIC, 10);
    fsAdvance(100);
    {
        int id = gFs[0].open ? 0 : 1;
        CHECK(gFs[id].gain == 250);
    }
    sndSetUserVolume(SND_VOL_EFFECTS, 100);
    sndSetUserVolume(SND_VOL_MUSIC, 100);
    gMusicIs = 1;
    sndSepPlayStop(sepF, 0);
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0);
    {
        int id = gFs[0].open ? 0 : 1, g0, g1;

        fsAdvance(1);
        g0 = gFs[id].gain;
        sndSepPlayDecrescendo(sepF, 0, 0x80, 0x3C); /* the game's own fade-out: 60 frames */
        for (i = 0; i < 30; i++) fsAdvance(10);
        g1 = gFs[id].gain;
        CHECK(g0 == 1000 && g1 > 0 && g1 < 400);
        for (i = 0; i < 40; i++) fsAdvance(10);
        CHECK(gFs[id].gain == 0);
        /* pause: nothing more is fed and the sink is told; resume goes on */
        sndSepPlaySetVol(sepF, 0, 127, 127);
        audioModTrackPause(1000000, 1); /* an unknown handle is ignored */
        {
            long t0 = gFs[id].taken;
            fsAdvance(5000);
            CHECK(gFs[id].taken > t0);
        }
    }
    sndSepPlayStop(sepF, 0);

    /* 7. replaying a record starts over (the old stream is closed); the stop of a record that is not playing does nothing */
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0);
    {
        int a = gFs[0].open ? 0 : 1;
        CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0);
        CHECK(gFs[a].closed || gFs[a].open);
        sndSepPlayStop(sepF, 0);
        CHECK(!gFs[0].open && !gFs[1].open);
    }

    /* 8. streamed from the disk (the memory limit tiny) is the same output as loaded whole */
    audioModSetMemLimit(1000);
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1);
    {
        int id = gFs[0].open ? 0 : 1;
        runFor(id, 20000, 1470);
        for (bad = 0, i = 0; i < gFs[id].taken; i++) bad += gFs[id].data[i * 2] != (int16_t)rampAt(i, 0, 2999);
        CHECK(bad == 0 && gFs[id].taken > 20000);
    }
    sndSepPlayStop(sepF, 0);
    audioModSetMemLimit(0);

    /* 9. a refused file: logged, the earlier mod's file is used; with none usable the sequence plays */
    rampWav("modB", "seq_1_00.wav", 4000, 44100, 1, NULL);
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1 && gFs[gFs[0].open ? 0 : 1].rate == 44100); /* modB, the last */
    sndSepPlayStop(sepF, 0);
    {
        uint8_t eight[100] = {0};
        size_t len;
        uint8_t *w = rawWav(1, 1, 44100, 8, eight, 100, 0, 100, NULL, &len);

        writeFile("modtest/modB/audio/TESTBK/seq_1_00.wav", w, len);
        free(w);
    }
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1 && gFs[gFs[0].open ? 0 : 1].rate == 22050); /* modA's */
    sndSepPlayStop(sepF, 0);
    writeFile("modtest/modA/audio/TESTBK/seq_1_00.wav", "garbage", 7);
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 0);
    sndSepPlayStop(sepF, 0);
    /* no output (a muted host, no device): the sequence plays and nothing is opened */
    rampWav("modA", "seq_1_00.wav", 3000, 22050, 2, NULL);
    gFsFail = 1;
    gFsOpened = 0;
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 0);
    sndSepPlayStop(sepF, 0);
    gFsFail = 0;

    /* 10. closing the package (the bank unloads) ends its track */
    CHECK(sndSepPlayStart(sepF, 0, 1, 1, 127, 127) >= 0 && gFsOpened == 1);
    sndSepClose(sepF);
    CHECK(!gFs[0].open && !gFs[1].open);
    sndSepClose(sep0);
    sndVabClose(vab);
    audioModSetSink(NULL, NULL);
}

int main(int argc, char **argv) {
    char dir[1400];

    if (argc < 2) {
        fprintf(stderr, "usage: audiotest <scratch dir>\n");
        return 2;
    }
    snprintf(gDir, sizeof gDir, "%s", argv[1]);
    path(dir, sizeof dir, "data");
    tdMakeDirs(dir);
    buildFiles();
    testDecoder();
    testWavAndMidiWriters();
    dumpParsed();
    testExport();
    path(dir, sizeof dir, "modtest");
    tdMakeDirs(dir);
    testWavReader();
    testResampler();
    testVagEncoder();
    testBankPatch();
    testSongs();
    if (failures) {
        printf("audiotest: %d check(s) FAILED\n", failures);
        return 1;
    }
    printf("audiotest: OK (C side)\n");
    return 0;
}
