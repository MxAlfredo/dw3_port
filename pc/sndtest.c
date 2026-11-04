/*
 * Unit test for the port's own SPU backing (pc/shim/snd_spu.c) and its SEP
 * player (pc/shim/snd_sep.c), the libsnd shims that grew real
 * implementations (ADR-002; host_snd.c keeps the public libsnd surface).
 *
 * It builds the smallest DW3-shaped VAB header (VH) in memory -- no data file
 * is committed -- and drives the new parser and the key-on math against it:
 *
 *     0x000 VabHdr (32): 'pBAV', ver 7, ps/ts/vs, master vol/pan
 *     0x020 ProgAtr[128] (16 B each): prog 0 unused, progs 1 and 2 used
 *     0x820 tone pages, one 0x200 block per USED program, packed in slot order
 *          so page 0 is prog 1 (the rank mapping the parser has to learn)
 *     0x820+2*0x200+0x200 the VAG size table (u16, in 8-byte units, 1-indexed)
 *
 * The VAG body is a synthetic VAG stream whose blocks end with the 0x01
 * terminator flag (the one-shot convention the disc's own data uses, checked
 * against MVCOMMON.BIN), so PsyX_SPUAL's decodeSound stops at the sample end.
 *
 * T36 adds the XA (movie audio) side: synthetic 2352-byte raw frames (a video
 * sector, a stereo spec-layout sector, a predictor-history sector, a mono
 * sector of a foreign channel, an EOF sector and a filter 2/3 sector) drive
 * sndXaClassify/sndXaDecodeAudio and the stream
 * driver (sndXaArm/Start/Tick) over a fake source and a recording sink, so no
 * OpenAL and no disc are needed.
 *
 * Exit 0: every assertion held. Exit 1: the parser (or anything it feeds)
 * failed; per-attempt notes go to stderr.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim/host_xa.h"
#include "shim/snd_internal.h"

/* The fixture's own numbers, mirrored in the assertions below. */
#define FIX_BASE   0x1010u     /* BANK 1's SPU base (SOUND_SPU_ADDRS[0]) */
#define FIX_VAG1   512u        /* bytes of VAG 1 (64 * 8) */
#define FIX_VAG2   256u        /* bytes of VAG 2 (32 * 8) */
#define FIX_VAG3   128u        /* bytes of VAG 3 (16 * 8) */
#define FIX_BODY   (FIX_VAG1 + FIX_VAG2 + FIX_VAG3)
#define FIX_PROGS  2u
#define FIX_VAGS   3u
#define FIX_TONES  4u

static unsigned char vh[0x820 + 2 * 0x200 + 0x200];
static unsigned char body[FIX_BODY];

static int spoken;

static void fail(const char *what) {
    fprintf(stderr, "sndtest: FAIL %s\n", what);
    exit(1);
}

static void put32(unsigned long at, long v) {
    vh[at + 0] = (unsigned char)(v);
    vh[at + 1] = (unsigned char)(v >> 8);
    vh[at + 2] = (unsigned char)(v >> 16);
    vh[at + 3] = (unsigned char)(v >> 24);
}

static void put16(unsigned long at, int v) {
    vh[at + 0] = (unsigned char)(v);
    vh[at + 1] = (unsigned char)(v >> 8);
}

/* One 32-byte tone record, fields as the DW3 discs store them:
   00 prior, 01 mode, 02 vol, 03 pan, 04 center, 05 shift, 06 min, 07 max,
   08..0f vib/por/pb/res, 10 adsr1, 12 adsr2, 14 prog.2, 16 vag.2, 18 res.8 */
static void putTone(unsigned long at, int prior, int mode, int vol, int pan,
                    int center, int shift, int min, int max,
                    int adsr1, int adsr2, int prog, int vag) {
    memset(vh + at, 0, 0x20);
    vh[at + 0] = (unsigned char)prior;
    vh[at + 1] = (unsigned char)mode;
    vh[at + 2] = (unsigned char)vol;
    vh[at + 3] = (unsigned char)pan;
    vh[at + 4] = (unsigned char)center;
    vh[at + 5] = (unsigned char)shift;
    vh[at + 6] = (unsigned char)min;
    vh[at + 7] = (unsigned char)max;
    put16(at + 0x10, adsr1);
    put16(at + 0x12, adsr2);
    put16(at + 0x14, prog);
    put16(at + 0x16, vag);
}

static void makeVh(void) {
    int i;

    memset(vh, 0, sizeof(vh));

    /* VabHdr: 'pBAV' as the four ANSII bytes little endian reads 0x56414270 */
    vh[0] = 'p'; vh[1] = 'B'; vh[2] = 'A'; vh[3] = 'V';
    put32(0x04, 7);           /* version */
    put32(0x08, 0);           /* bank id */
    put32(0x0c, 0x4F310);     /* fsize: VH + VB, what the real headers carry */
    put16(0x10, 0xEEEE);      /* reserved */
    put16(0x12, FIX_PROGS);   /* ps: tone pages = used programs */
    put16(0x14, FIX_TONES);   /* ts */
    put16(0x16, FIX_VAGS);    /* vs */
    vh[0x18] = 127;           /* master vol: full, so the older expectations stand (O4 tests 64) */
    vh[0x19] = 64;            /* master pan */
    vh[0x1a] = 0; vh[0x1b] = 0;
    put32(0x1c, 0xFFFFFFFF);

    /* ProgAtr[128]: prog 0 unused, prog 1 three tones, prog 2 one */
    for (i = 0; i < 128; i++) {
        vh[0x20 + i * 16 + 0] = (unsigned char)(i == 1 ? 3 : (i == 2 ? 1 : 0));
        vh[0x20 + i * 16 + 1] = 0x7F;   /* program volume, as COMMON's own */
        vh[0x20 + i * 16 + 4] = 64;     /* program pan: centre (A6) */
    }

    /* page 0 = prog 1: three tones sharing VAGs 1-3 */
    putTone(0x820 + 0x000, 10, 4, 120, 64, 60, 63, 10, 100, 0x0801, 0x001F, 1, 1);
    putTone(0x820 + 0x020, 10, 4, 110, 90, 62, 0, 20, 90, 0x0800, 0x401F, 1, 2);
    putTone(0x820 + 0x040, 10, 0, 100, 0, 44, 10, 0, 80, 0x0800, 0x000A, 1, 3);
    /* A3: tone 0 of prog 1 carries pitch-bend ranges, byte 0x0C (down) = 2
       semitones, byte 0x0D (up) = 12 -- the bytes retail's _SsVmPBVoice reads */
    vh[0x820 + 0x00C] = 2;
    vh[0x820 + 0x00D] = 12;
    /* page 1 = prog 2: one tone reusing VAG 2, shift = 0xF9 a signed -7 */
    putTone(0x820 + 0x200 + 0x000, 12, 4, 100, 0, 45, 0xF9, 30, 60, 0x0801, 0x009F, 2, 2);

    /* The VAG size table, right after the pages, in 8-byte units */
    put16(0x820 + FIX_PROGS * 0x200 + 0x000, 0);   /* the 1-indexed spacer */
    put16(0x820 + FIX_PROGS * 0x200 + 0x002, FIX_VAG1 / 8);
    put16(0x820 + FIX_PROGS * 0x200 + 0x004, FIX_VAG2 / 8);
    put16(0x820 + FIX_PROGS * 0x200 + 0x006, FIX_VAG3 / 8);
}

/* A VAG stream: each 16-byte block with the flag byte low (0), whose last
   block carries 0x01 -- the one-shot terminator the SPU honours and PsyX
   decoder stops on. The 14 data bytes are a known ramp. */
static void makeBody(void) {
    unsigned long at;

    /* FIX_BODY and each VAG are multiples of 16, so the walk is uniform */
    for (at = 0; at < FIX_BODY; at += 16) {
        int last = (FIX_BODY - at == 16);
        int n;

        body[at + 0] = 0x00;                              /* predictor */
        /* the end of the body: A6e makes the last block a one-shot end (0x01)
           only inside its own test; elsewhere it repeats (0x03), so the
           fixture's voices live until the envelope or a key-off ends them */
        body[at + 1] = (unsigned char)(last ? 0x03 : 0);  /* flag byte */
        for (n = 2; n < 16; n++) {
            body[at + n] = (unsigned char)(0x30 + (n & 0x0f));
        }
    }
}

/*
 * T35: the SEP side. The fixture is one package in the exact bytes the discs
 * carry (verified against MPCOMMON.BIN / MPTTLBGM.BIN / MPBGM026.BIN):
 *
 *     0x00 'pQES' (the magic as the four ANSII bytes are stored)
 *     0x04 version, big endian, 0
 *     0x06 record: id 1 (BE), resolution 480 (BE), tempo 600000 us/quarter
 *          (3 bytes BE -> 480 * 1e6 / 600000 = 800 ticks/s: 16 a PAL frame),
 *          num 4, den 4, span 49 (BE), then the MIDI-like event stream, in
 *          which FF 51 carries a 3-byte tempo with NO length byte, FF 2F
 *          ends, the loop is CC99 = 0x14 ... CC6 = 0x7F ... CC99 = 0x1E, and
 *          one trailing 0x00 stays inside the record's span.
 *
 * The player is driven with a recording sink (SndEvSink), not real voices:
 * what the events say and how the volumes compose is what the assertions
 * pin. CC7 = 100 rides at tick 0 so the first key-on reads the volume the
 * marker set; the tempo change rides at the loop's end tick.
 */

/* The ticks the fixture's events sit at, and the two rates the clocks do */
#define SEP_RES        480u
#define SEP_TEMPO0     600000u  /* the header's tempo */
#define SEP_TEMPO51    250000u  /* the FF 51 at the loop's end tick */
#define SEP_TBYT_FRAME 16u      /* 480 * 1e6 / 600000 / 50 (PAL frame) */

static unsigned char sep[0x44];          /* 4 + 2 + 13 + 49 */
static unsigned char sepMeta[0x2F];      /* two records, one with an unknown meta */

/* The recording sink: book voices by counting, never touch the SPU. */
#define FAKE_MAX 64
struct FakeOn {
    short vabId;
    short prog;
    short note;
    short volL, volR;
};
static struct FakeOn fakeOn[FAKE_MAX];
static int fakeOnCount;
static short fakeOff[FAKE_MAX];
static int fakeOffCount;
static short fakeVoiceNext;

/* The note the fake layers: a key-on of it books two voices, the way a
   program with two tones covering the note does through the real sink. */
static short fakeLayerNote = -1;

static int fakeKeyOn(void *ctx, short vabId, short prog, short note,
                     short volL, short volR, short *voices) {
    struct FakeOn *rec;
    int n = (note == fakeLayerNote) ? 2 : 1, i;

    (void)ctx;
    if (fakeOnCount >= FAKE_MAX) {
        return 0;
    }
    rec = fakeOn + fakeOnCount++;
    rec->vabId = vabId;
    rec->prog = prog;
    rec->note = note;
    rec->volL = volL;
    rec->volR = volR;
    for (i = 0; i < n; i++) {
        voices[i] = fakeVoiceNext++;
    }
    return n;
}

static void fakeKeyOff(void *ctx, short voice) {
    (void)ctx;
    if (fakeOffCount < FAKE_MAX) {
        fakeOff[fakeOffCount++] = voice;
    }
}

/* The pitch-bend deliveries (A3): which voice, which MSB retail would read */
struct FakeBend {
    short voice;
    unsigned char msb;
};
static struct FakeBend fakeBend[FAKE_MAX];
static int fakeBendCount;

static void fakeBendVoice(void *ctx, short voice, unsigned char msb) {
    (void)ctx;
    if (fakeBendCount < FAKE_MAX) {
        fakeBend[fakeBendCount].voice = voice;
        fakeBend[fakeBendCount].msb = msb;
        fakeBendCount++;
    }
}

static const SndEvSink fakeSink = { fakeKeyOn, fakeKeyOff, fakeBendVoice };

/* The 3-byte big-endian fields of a record header and the package words */
static void sepPut16(unsigned char *at, int v) { /* big endian */
    at[0] = (unsigned char)(v >> 8);
    at[1] = (unsigned char)v;
}

static void sepPut24(unsigned char *at, long v) {
    at[0] = (unsigned char)(v >> 16);
    at[1] = (unsigned char)(v >> 8);
    at[2] = (unsigned char)v;
}

static void sepPut32(unsigned char *at, long v) {
    at[0] = (unsigned char)(v >> 24);
    at[1] = (unsigned char)(v >> 16);
    at[2] = (unsigned char)(v >> 8);
    at[3] = (unsigned char)v;
}

/* Writes a record's 13-byte header at `at`: id, res, tempo, num/den, span */
static void sepPutRec(unsigned char *at, int id, unsigned long res,
                      unsigned long tempo, unsigned long span) {
    sepPut16(at, id);
    sepPut16(at + 2, (int)res);
    sepPut24(at + 4, (long)tempo);
    at[7] = 4;
    at[8] = 4;
    sepPut32(at + 9, (long)span);
}

static void makeSep(void) {
    unsigned char *ev;
    int at;

    /* --- the main package ------------------------------------------------ */
    memset(sep, 0, sizeof(sep));
    sep[0] = 'p'; sep[1] = 0x51; sep[2] = 'E'; sep[3] = 'S';
    sepPut16(sep + 4, 0);
    sepPutRec(sep + 6, 1, SEP_RES, SEP_TEMPO0, 49);

    ev = sep + 6 + 13;
    at = 0;
    /* tick 0: the CC7 the first key-on reads, then the key-on */
    ev[at++] = 0x00; ev[at++] = 0xB0; ev[at++] = 0x07; ev[at++] = 100;
    ev[at++] = 0x00; ev[at++] = 0x90; ev[at++] = 0x3C; ev[at++] = 100;
    /* tick 16: its key-off, then the program change */
    ev[at++] = 0x10; ev[at++] = 0x3C; ev[at++] = 0x00;
    ev[at++] = 0x00; ev[at++] = 0xC0; ev[at++] = 2;
    /* tick 24: a key-on under the new program */
    ev[at++] = 0x08; ev[at++] = 0x90; ev[at++] = 0x43; ev[at++] = 80;
    /* tick 40: its key-off, the loop's two markers and the loop's key-on */
    ev[at++] = 0x10; ev[at++] = 0x43; ev[at++] = 0x00;
    ev[at++] = 0x00; ev[at++] = 0xB0; ev[at++] = 0x63; ev[at++] = 0x14;
    ev[at++] = 0x00; ev[at++] = 0x06; ev[at++] = 0x7F;   /* running status 0xB0 */
    ev[at++] = 0x00; ev[at++] = 0x90; ev[at++] = 0x48; ev[at++] = 120;
    /* tick 48: the loop body's key-off */
    ev[at++] = 0x08; ev[at++] = 0x48; ev[at++] = 0x00;
    /* tick 56: the loop's close, a tempo change and the end */
    ev[at++] = 0x08; ev[at++] = 0xB0; ev[at++] = 0x63; ev[at++] = 0x1E;
    ev[at++] = 0x00; ev[at++] = 0xFF; ev[at++] = 0x51;
    ev[at++] = (unsigned char)(SEP_TEMPO51 >> 16);       /* NO length byte */
    ev[at++] = (unsigned char)(SEP_TEMPO51 >> 8);
    ev[at++] = (unsigned char)SEP_TEMPO51;
    ev[at++] = 0x00; ev[at++] = 0xFF; ev[at++] = 0x2F;
    ev[at++] = 0x00;                                     /* the retail trailing byte */

    /* --- the unknown-meta package: record 0 dies at the meta, record 1 lives */
    memset(sepMeta, 0, sizeof(sepMeta));
    sepMeta[0] = 'p'; sepMeta[1] = 0x51; sepMeta[2] = 'E'; sepMeta[3] = 'S';
    sepPut16(sepMeta + 4, 0);
    sepPutRec(sepMeta + 6, 9, SEP_RES, 500000, 11);
    ev = sepMeta + 6 + 13;
    ev[0] = 0x00; ev[1] = 0x90; ev[2] = 0x3C; ev[3] = 0x40;  /* key-on tick 0 */
    ev[4] = 0x10; ev[5] = 0x3C; ev[6] = 0x00;               /* key-off tick 16 */
    ev[7] = 0x00; ev[8] = 0xFF; ev[9] = 0x01; ev[10] = 0x41; /* unknown meta -> stop */
    sepPutRec(sepMeta + 6 + 13 + 11, 11, SEP_RES, 500000, 4);
    ev = sepMeta + 6 + 13 + 11 + 13;
    ev[0] = 0x00; ev[1] = 0x90; ev[2] = 0x40; ev[3] = 0x40;  /* key-on tick 0 */
}

/* A record without markers and without an end meta: key-on at tick 0, a CC7
   = 0 at tick 204, natural exhaustion. size 8. */
static unsigned char sepNoEnd[0x1B]; /* 4 + 2 + 13 + 8 */

static void makeSepNoEnd(void) {
    memset(sepNoEnd, 0, sizeof(sepNoEnd));
    sepNoEnd[0] = 'p'; sepNoEnd[1] = 0x51; sepNoEnd[2] = 'E'; sepNoEnd[3] = 'S';
    sepPut16(sepNoEnd + 4, 0);
    sepPutRec(sepNoEnd + 6, 3, SEP_RES, SEP_TEMPO0, 8);
    sepNoEnd[19] = 0x00; sepNoEnd[20] = 0x90; sepNoEnd[21] = 0x3C; sepNoEnd[22] = 100;
    sepNoEnd[23] = (unsigned char)0x81; sepNoEnd[24] = 0x4C; /* delta 204 */
    sepNoEnd[25] = 0xB0; sepNoEnd[26] = 0x07; sepNoEnd[27] = 0x00;
}

/* A1/A2 fixtures. sepTwo: one package, two records that both ring (a key-on
   at tick 0 and a CC7 at tick 204, as sepNoEnd) on notes 60 and 61. sepLay:
   one record that keys note 61 on at tick 0 and off at tick 16, then holds
   to tick 220 so the key-off alone must release the layers. */
static unsigned char sepTwo[6 + 2 * (13 + 9)];
static unsigned char sepLay[6 + 13 + 12];
/* A3: a key-on, a bend to the max, then a running-status bend to the centre */
static unsigned char sepBend[6 + 13 + 16];

static void makeSepA(void) {
    unsigned char *ev;
    int r;

    memset(sepTwo, 0, sizeof(sepTwo));
    sepTwo[0] = 'p'; sepTwo[1] = 0x51; sepTwo[2] = 'E'; sepTwo[3] = 'S';
    for (r = 0; r < 2; r++) {
        sepPutRec(sepTwo + 6 + r * 22, 5 + r, SEP_RES, SEP_TEMPO0, 9);
        ev = sepTwo + 6 + r * 22 + 13;
        ev[0] = 0x00; ev[1] = 0x90; ev[2] = (unsigned char)(60 + r); ev[3] = 100;
        ev[4] = 0x81; ev[5] = 0x4C;
        ev[6] = 0xB0; ev[7] = 0x07; ev[8] = 0x00;
    }
    memset(sepLay, 0, sizeof(sepLay));
    sepLay[0] = 'p'; sepLay[1] = 0x51; sepLay[2] = 'E'; sepLay[3] = 'S';
    sepPutRec(sepLay + 6, 7, SEP_RES, SEP_TEMPO0, 12);
    ev = sepLay + 6 + 13;
    ev[0] = 0x00; ev[1] = 0x90; ev[2] = 61; ev[3] = 100;
    ev[4] = 0x10; ev[5] = 61; ev[6] = 0x00;
    ev[7] = 0x81; ev[8] = 0x4C; ev[9] = 0xB0; ev[10] = 0x07; ev[11] = 0x00;

    memset(sepBend, 0, sizeof(sepBend));
    sepBend[0] = 'p'; sepBend[1] = 0x51; sepBend[2] = 'E'; sepBend[3] = 'S';
    sepPutRec(sepBend + 6, 8, SEP_RES, SEP_TEMPO0, 16);
    ev = sepBend + 6 + 13;
    ev[0] = 0x00; ev[1] = 0x90; ev[2] = 60; ev[3] = 100;       /* key-on */
    ev[4] = 0x00; ev[5] = 0xE0; ev[6] = 0x00; ev[7] = 0x7F;    /* bend max */
    ev[8] = 0x00; ev[9] = 0x00; ev[10] = 0x40;                 /* running: centre */
    ev[11] = 0x81; ev[12] = 0x4C; ev[13] = 0xB0; ev[14] = 0x07; ev[15] = 0x00;
}

/*
 * T36: the XA (movie audio) fixtures. Each row is one raw frame, and the fake
 * source serves a chosen list of rows (in any order) from XA_ARM_LBA:
 *   [0] video/data, [1] stereo layout, [2] stereo history-ramp, [3] mono
 *   layout (also a foreign file/channel in stereo runs), [4] stereo layout
 *   with EOF, [5] stereo filter 2/3.
 * The stereo layout has four blocks with distinct shifts (0..3) and distinct
 * nibbles, so its 2016 L/R frames are exact by hand. The history sector uses
 * filter 1 and a constant nibble: its ramp and the absence of a reset at
 * block, group and sector boundaries pin the persistent predictor.
 */
#define XA_ARM_LBA 1000

static unsigned char xaFrames[6][SND_XA_FRAME_SIZE];

/* Sync, the BCD header and the doubled subheader of one raw frame. */
static void xaFrameHead(unsigned char *at, int file, int chan, int submode,
                        int coding) {
    int g;

    memset(at, 0, SND_XA_FRAME_SIZE);
    at[0] = 0x00;
    for (g = 1; g < 11; g++) {
        at[g] = 0xFF;
    }
    at[11] = 0x00;
    at[12] = 0x00; at[13] = 0x02; at[14] = 0x00; at[15] = 0x02; /* mode 2 */
    at[SND_XA_SUBHEADER_AT + 0] = (unsigned char)file;
    at[SND_XA_SUBHEADER_AT + 1] = (unsigned char)chan;
    at[SND_XA_SUBHEADER_AT + 2] = (unsigned char)submode;
    at[SND_XA_SUBHEADER_AT + 3] = (unsigned char)coding;
    at[SND_XA_SUBHEADER_AT + 4] = (unsigned char)file;
    at[SND_XA_SUBHEADER_AT + 5] = (unsigned char)chan;
    at[SND_XA_SUBHEADER_AT + 6] = (unsigned char)submode;
    at[SND_XA_SUBHEADER_AT + 7] = (unsigned char)coding;
}

/* A group is a 16-byte header plus 28 four-byte words. params[2b] is block
   b's left (or mono first) parameter, params[2b+1] its right (or second);
   bytes 00..03 copy 04..07 and 0C..0F copy 08..0B, as the spec stores them. */
static void xaGroupParams(unsigned char *at, const unsigned char params[8]) {
    int b;

    for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
        at[4 + b * 2] = params[b * 2];
        at[5 + b * 2] = params[b * 2 + 1];
    }
    for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
        at[b] = at[4 + b];
        at[12 + b] = at[8 + b];
    }
}

/* Word j's byte b: low nibble = block b left/mono-first sample, high nibble
   = block b right/mono-second sample. */
static void xaDataByte(unsigned char *at, int j, int b, int lo, int hi) {
    at[SND_XA_GROUP_HEADER + j * 4 + b] =
        (unsigned char)(((hi & 0x0F) << 4) | (lo & 0x0F));
}

static unsigned char *xaGroupAt(unsigned char *frame, int g) {
    return frame + SND_XA_PAYLOAD_AT + g * SND_XA_GROUP_SIZE;
}

/* Stereo layout: block b uses shift b (filter 0); left nibble j%8, right
   nibble 8+(j%8) (signed -8..-1). Every group repeats, so frame i's expected
   value is a function of i % 112. */
static void xaBuildLayout(unsigned char *at, int file, int chan, int submode,
                          int coding) {
    int g, b, j;
    unsigned char params[8];

    xaFrameHead(at, file, chan, submode, coding);
    for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
        params[b * 2] = (unsigned char)(12 - b);
        params[b * 2 + 1] = (unsigned char)(12 - b);
    }
    for (g = 0; g < SND_XA_GROUPS; g++) {
        unsigned char *grp = xaGroupAt(at, g);

        xaGroupParams(grp, params);
        for (j = 0; j < 28; j++) {
            int lo = j % 8;
            int hi = 8 + (j % 8);

            for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
                xaDataByte(grp, j, b, lo, hi);
            }
        }
    }
}

/* Stereo history: filter 1 (pos 60, neg 0), shift 0, every nibble constant
   (left 7, right 5), so a fresh stream ramps instead of resetting. */
static void xaBuildHistory(unsigned char *at, int file, int chan, int submode,
                           int coding) {
    int g, b, j;
    unsigned char params[8];

    xaFrameHead(at, file, chan, submode, coding);
    for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
        params[b * 2] = 0x1C;      /* filter 1, shift field 12 (shift 0) */
        params[b * 2 + 1] = 0x1C;
    }
    for (g = 0; g < SND_XA_GROUPS; g++) {
        unsigned char *grp = xaGroupAt(at, g);

        xaGroupParams(grp, params);
        for (j = 0; j < 28; j++) {
            for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
                xaDataByte(grp, j, b, 7, 5);
            }
        }
    }
}

/* Mono layout: block b's first half uses shift b, its second half shift 3-b;
   first-half nibble j%8, second-half nibble 15-(j%8) (signed -1..-8). */
static void xaBuildMono(unsigned char *at, int file, int chan, int submode,
                        int coding) {
    int g, b, j;
    unsigned char params[8];

    xaFrameHead(at, file, chan, submode, coding);
    for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
        params[b * 2] = (unsigned char)(12 - b);
        params[b * 2 + 1] = (unsigned char)(9 + b);
    }
    for (g = 0; g < SND_XA_GROUPS; g++) {
        unsigned char *grp = xaGroupAt(at, g);

        xaGroupParams(grp, params);
        for (j = 0; j < 28; j++) {
            int lo = j % 8;
            int hi = 15 - (j % 8);

            for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
                xaDataByte(grp, j, b, lo, hi);
            }
        }
    }
}

/* Filter spot check: block 0's left is filter 2 and its right filter 3, both
   shift 0; blocks 1..3 are filter 0. Every nibble is 1. */
static void xaBuildFilters(unsigned char *at, int file, int chan, int submode,
                           int coding) {
    int g, b, j;
    unsigned char params[8];

    xaFrameHead(at, file, chan, submode, coding);
    params[0] = 0x2C;              /* block 0 left: filter 2, shift 0 */
    params[1] = 0x3C;              /* block 0 right: filter 3, shift 0 */
    for (b = 1; b < SND_XA_GROUP_BLOCKS; b++) {
        params[b * 2] = 0x0C;
        params[b * 2 + 1] = 0x0C;
    }
    for (g = 0; g < SND_XA_GROUPS; g++) {
        unsigned char *grp = xaGroupAt(at, g);

        xaGroupParams(grp, params);
        for (j = 0; j < 28; j++) {
            for (b = 0; b < SND_XA_GROUP_BLOCKS; b++) {
                xaDataByte(grp, j, b, 1, 1);
            }
        }
    }
}

static void makeXaFrames(void) {
    xaBuildLayout(xaFrames[0], 0, 1, 0x48, 0x00);   /* video/data, no audio */
    xaBuildLayout(xaFrames[1], 1, 1, 0x64, 0x01);   /* stereo layout        */
    xaBuildHistory(xaFrames[2], 1, 1, 0x64, 0x01);  /* stereo history       */
    xaBuildMono(xaFrames[3], 2, 0, 0x64, 0x00);     /* mono, foreign channel*/
    xaBuildLayout(xaFrames[4], 1, 1, 0xE4, 0x01);   /* stereo layout, EOF   */
    xaBuildFilters(xaFrames[5], 1, 1, 0x64, 0x01);  /* stereo filters 2/3   */
}

/* The next value of the history fixture's left (t=7) / right (t=5) ramp:
   filter 1, shift 0, positive history, so plain division is the floor. */
static int xaHistNext(int old, int t) {
    return t + (old * 60 + 32) / 64;
}

/* The fake sector source: serves xaFakeRows[0..xaFakeRowN) from xaFakeBase,
   then end-of-stream. Rows are pointers so a run can pick any order (audio,
   video, audio) and reuse a frame. */
#define XA_ROW_MAX 8
static const unsigned char *xaFakeRows[XA_ROW_MAX];
static int xaFakeRowN;
static long xaFakeBase;
static long xaFakeReads;
static long xaFakeLastLba;

static int xaFakeSource(void *ctx, long lba, unsigned char *frame) {
    long idx = lba - xaFakeBase;

    (void)ctx;
    xaFakeLastLba = lba;
    if (idx < 0 || idx >= xaFakeRowN) {
        return 0;
    }
    memcpy(frame, xaFakeRows[idx], SND_XA_FRAME_SIZE);
    xaFakeReads++;
    return 1;
}

/* Install one run: the rows served and the LBA of the first. */
static void xaFakeUse(const unsigned char **rows, int n, long base) {
    int k;

    for (k = 0; k < n && k < XA_ROW_MAX; k++) {
        xaFakeRows[k] = rows[k];
    }
    xaFakeRowN = n;
    xaFakeBase = base;
    xaFakeReads = 0;
    xaFakeLastLba = 0;
}

/* The recording sink: keeps each chunk's size, channels and a few samples
   from both ends (the history run compares the last frame of one sector with
   the first frame of the next). */
#define XA_CHUNK_MAX 32
struct XaChunk {
    int samples;
    int channels;
    short first[4];
    short last[4];
};
static struct XaChunk xaChunks[XA_CHUNK_MAX];
static int xaChunkCount;
static long xaFramesDelivered;
static int xaChannelsSeen;

static void xaFakeSink(void *ctx, const short *pcm, int samples, int channels) {
    struct XaChunk *c;

    (void)ctx;
    if (xaChunkCount < XA_CHUNK_MAX) {
        int take = (samples < 4) ? samples : 4;

        c = xaChunks + xaChunkCount++;
        c->samples = samples;
        c->channels = channels;
        memcpy(c->first, pcm, sizeof(short) * take);
        memcpy(c->last, pcm + (samples - take), sizeof(short) * take);
    }
    xaFramesDelivered += samples / channels;
    xaChannelsSeen = channels;
}

static const SndXaSink xaFake = { xaFakeSink };

/* Clear the recording sink before a run. */
static void xaFakeReset(void) {
    xaChunkCount = 0;
    xaFramesDelivered = 0;
    xaChannelsSeen = 0;
}

/* The key-on's resonance-point value for a note: PsyX's pitch is relative to
   the 44100 Hz the decoded VAG is buffered as, so 0x1000 = "no shift". */
static short expectPitch(double semitones) {
    double v = 4096.0 * exp2(semitones / 12.0);

    if (v < 64.0) {
        v = 64.0;
    }
    if (v > 65535.0) {
        v = 65535.0;
    }
    return (short)(v + 0.5);
}

/* A5d: a sink that keys real voices (prog 1 tone 2, priority 10) so a steal
   can happen under a SEP play's held note */
static short realLastVoice = -1;

static int realKeyOn(void *ctx, short vabId, short prog, short note,
                     short volL, short volR, short *voices) {
    (void)ctx;
    (void)prog;
    realLastVoice = sndKeyOnSrc(vabId, 1, 2, note, 0, volL, volR, 1);
    if (realLastVoice < 0) {
        return 0;
    }
    voices[0] = realLastVoice;
    return 1;
}

static void realKeyOff(void *ctx, short voice) {
    (void)ctx;
    sndKeyOff(voice, 0, 0, 0, 0);
}

static const SndEvSink realSink = { realKeyOn, realKeyOff, NULL };

/* O4: the game's music query, faked */
static int fakeQueryEffect(short seqId, short track) {
    (void)seqId;
    (void)track;
    return 0;
}

static int fakeQueryMusic(short seqId, short track) {
    (void)seqId;
    (void)track;
    return 1;
}

int main(void) {
    SndVab vab;
    struct SndNote note;
    short voice;

    makeVh();
    makeBody();

    /* --- O7: the tick rate follows the game's SsSetTickMode ----------------
       libsnd_sstick.s: the low 12 bits of the mode select 60/50/120/240
       ticks a second (0..3), anything above the table is the rate itself
       (the EU build's SS_NOTICK | 50). Both clocks must give the same musical
       time per real second: tempo 600000 us/quarter, resolution 480 ->
       800 ticks/s, in 1/256 ticks. */
    if (sndTickHzFromMode(0x1000) != 60 || sndTickHzFromMode(0x1000 | 50) != 50 ||
        sndTickHzFromMode(0x1000 | 1) != 50 || sndTickHzFromMode(0x1000 | 2) != 120 ||
        sndTickHzFromMode(0x1000 | 3) != 240 || sndTickHzFromMode(0x1000 | 60) != 60) {
        fail("O7: tick mode -> ticks a second");
    }
    if (sndTickHz() != 50) {
        fail("O7: the default tick rate stays 50 (the tests' PAL frame)");
    }
    {
        unsigned long s50 = sndSepStep256(480, 600000, 50);
        unsigned long s60 = sndSepStep256(480, 600000, 60);
        unsigned long per50 = s50 * 50, per60 = s60 * 60, perSec = 800ul * 256;

        if (s50 != 4096 || s60 != 3413) {
            fprintf(stderr, "sndtest: step256 %lu / %lu\n", s50, s60);
            fail("O7: ticks per frame at 50 and 60 Hz");
        }
        if (per50 != perSec || perSec - per60 > 60 || per60 > perSec) {
            fprintf(stderr, "sndtest: a second is %lu / %lu, want %lu\n", per50, per60, perSec);
            fail("O7: one second of music is the same at 50 and 60 Hz");
        }
    }
    sndSetTickHz(60);
    if (sndTickHz() != 60 || sndEnvTickSamples() != SND_ENV_SAMPLE_RATE / 60) {
        fail("O7: the envelope tick follows the rate");
    }
    sndSetTickHz(50);
    if (sndEnvTickSamples() != SND_ENV_TICK_SAMPLES) {
        fail("O7: the envelope tick at 50 Hz is the old constant");
    }

    /* --- The header -> the registry ------------------------------------- */
    sndParseVab(vh, sizeof vh, &vab);
    if (vab.progs != (int)FIX_PROGS) {
        fprintf(stderr, "sndtest: wrong progs %d\n", vab.progs);
        fail("parse progs");
    }
    if (vab.vagCount != (int)FIX_VAGS) {
        fprintf(stderr, "sndtest: wrong vags %d\n", vab.vagCount);
        fail("parse vags");
    }
    if (vab.rank[0] != -1) {
        fprintf(stderr, "sndtest: unused prog 0 got rank %d\n", vab.rank[0]);
        fail("rank of unused prog");
    }
    if (vab.rank[1] != 0 || vab.rank[2] != 1) {
        fprintf(stderr, "sndtest: ranks %d %d\n", vab.rank[1], vab.rank[2]);
        fail("rank mapping");
    }

    /* Tones of page 0 (prog 1), fields compared byte for byte */
    if (vab.tone[0][0].vag != 1 || vab.tone[0][1].vag != 2 ||
        vab.tone[0][2].vag != 3) {
        fail("tone vags");
    }
    if (vab.tone[0][0].vol != 120 || vab.tone[0][0].pan != 64 ||
        vab.tone[0][0].center != 60 || vab.tone[0][0].shift != 63 ||
        vab.tone[0][0].min != 10 || vab.tone[0][0].max != 100 ||
        vab.tone[0][0].adsr1 != 0x0801 || vab.tone[0][0].adsr2 != 0x001F ||
        vab.tone[0][0].prog != 1) {
        fprintf(stderr, "sndtest: tone[0][0] vol %d pan %d center %d shift %d "
                        "min %d max %d adsr1 %04x adsr2 %04x prog %d\n",
                vab.tone[0][0].vol, vab.tone[0][0].pan, vab.tone[0][0].center,
                vab.tone[0][0].shift, vab.tone[0][0].min, vab.tone[0][0].max,
                vab.tone[0][0].adsr1, vab.tone[0][0].adsr2, vab.tone[0][0].prog);
        fail("tone[0][0] fields");
    }

    /* The VAG sizes and the body the transfer expects */
    if (vab.vagSize[1] != FIX_VAG1 || vab.vagSize[2] != FIX_VAG2 ||
        vab.vagSize[3] != FIX_VAG3 || vab.bodyBytes != FIX_BODY ||
        vab.vagOfs[1] != 0 || vab.vagOfs[2] != FIX_VAG1 ||
        vab.vagOfs[3] != FIX_VAG1 + FIX_VAG2) {
        fprintf(stderr, "sndtest: vags %lu/%lu/%lu body %lu ofs %lu/%lu/%lu\n",
                vab.vagSize[1], vab.vagSize[2], vab.vagSize[3], vab.bodyBytes,
                vab.vagOfs[1], vab.vagOfs[2], vab.vagOfs[3]);
        fail("vag sizes");
    }

    /* --- The bank through the registry (host_snd's public behalf) -------- */
    if (sndVabOpen(vh, 0, FIX_BASE) != 1) {
        fail("sndVabOpen");
    }
    /* the body arrives exactly once, from getFileEntry's pointer */
    if (sndVabTransBody(body, 1) != 1) {
        fail("sndVabTransBody");
    }

    /* --- The key-on math ------------------------------------------------- */
    /* prog 1, tone 0 at its own centre note as a key-on the game would give:
       playSound passes fvol = 127; tone's vol 120 scales it */
    if (sndNotePrepare(1, 1, 0, 60, 0, 127, 127, &note) != 0) {
        fail("prepare prog1 tone0");
    }
    if (note.addr != FIX_BASE + 0 || note.loopAddr != note.addr) {
        fprintf(stderr, "sndtest: addr %08lx loop %08lx\n", note.addr, note.loopAddr);
        fail("addr of vag 1");
    }
    /* center 60, shift 63 (+63/128 semitone), note == centre */
    if (note.pitch < expectPitch(63.0 / 128.0) - 3 ||
        note.pitch > expectPitch(63.0 / 128.0) + 3) {
        fprintf(stderr, "sndtest: pitch %d expected %d\n", note.pitch,
                expectPitch(63.0 / 128.0));
        fail("pitch of vag 1");
    }
    /* workout: fvol 127 * tone vol 120 -> 120 of 127 -> 120 << 7 = 15360,
       squared like retail (A6b): 15360^2 / 16384 = 14400 */
    if (note.volL != 14400 || note.volR != 14400) {   /* pan 64 = centre */
        fprintf(stderr, "sndtest: vol %d/%d expected 14400/14400\n",
                note.volL, note.volR);
        fail("volume of vag 1");
    }

    /* the far-right panned tone above 90: prog 1 tone 1, note 62 */
    if (sndNotePrepare(1, 1, 1, 62, 0, 127, 127, &note) != 0) {
        fail("prepare prog1 tone1");
    }
    if (note.addr != FIX_BASE + FIX_VAG1 + 0) {
        fail("addr of vag 2 through prog 1");
    }
    if (note.volL != 4173 || note.volR != 12100) {   /* 8269, 14080 squared */
        fprintf(stderr, "sndtest: panned vol %d/%d\n", note.volL, note.volR);
        fail("panning of prog1 tone1");
    }

    /* prog 2's tone: note 45 semitones below nothing (would overflow the
       pitch's u16): center 45, note 45 with shift 121 (=-7/128) */
    if (sndNotePrepare(1, 2, 0, 45, 0, 127, 127, &note) != 0) {
        fail("prepare prog2 tone0");
    }
    if (note.pitch < expectPitch(-7.0 / 128.0) - 3 ||
        note.pitch > expectPitch(-7.0 / 128.0) + 3) {
        fprintf(stderr, "sndtest: prog2 pitch %d expected %d\n", note.pitch,
                expectPitch(-7.0 / 128.0));
        fail("pitch clamp high");
    }

    /* The refusals the game's own tones would hit */
    /* A3: retail _SsVmPBVoice (libsnd_vm_pb.s) -- the bend is the MSB byte,
       0x40 the centre; above it note += t * pbUp / 63 and fine = rem * 2,
       below it note += t * pbDown / 64 - 1 and fine = rem * 2 + 127 */
    {
        short bn, bf;
        struct SndNote c0, b1;
        unsigned short pp;
        double want;

        sndBendNote(0x40, 12, 2, 60, &bn, &bf);
        if (bn != 60 || bf != 0) {
            fail("bend centre leaves the note alone");
        }
        sndBendNote(0x7F, 12, 2, 60, &bn, &bf);
        if (bn != 72 || bf != 0) {
            fail("bend max is +pbUp semitones");
        }
        sndBendNote(0x50, 12, 2, 60, &bn, &bf);
        if (bn != 63 || bf != 6) {
            fail("bend 0x50 note/fine");
        }
        sndBendNote(0x00, 12, 2, 60, &bn, &bf);
        if (bn != 57 || bf != 127) {
            fail("bend min note/fine (retail's -1 / +127)");
        }
        if (sndNotePrepare(1, 1, 0, 60, 0, 127, 127, &c0) != 0) {
            fail("bend base prepare");
        }
        if (sndBendPitch(1, 1, 0, 60, 0x40, &pp) != 0 || pp != c0.pitch) {
            fail("bend centre keeps the key-on pitch");
        }
        if (sndBendPitch(1, 1, 0, 60, 0x7F, &pp) != 0) {
            fail("bend max pitch");
        }
        want = c0.pitch * 2.0;
        if (fabs((double)pp - want) > want * 0.002) {
            fprintf(stderr, "sndtest: bend max pitch %u want ~%.0f\n", pp, want);
            fail("bend max is an octave up");
        }
        if (sndBendPitch(1, 1, 0, 60, 0x00, &pp) != 0) {
            fail("bend min pitch");
        }
        want = c0.pitch * exp2(-2.0 / 12.0);
        if (fabs((double)pp - want) > want * 0.002) {
            fprintf(stderr, "sndtest: bend min pitch %u want ~%.0f\n", pp, want);
            fail("bend min is pbDown semitones down");
        }
        if (sndNotePrepare(1, 1, 0, 63, 6, 127, 127, &b1) != 0 ||
            sndBendPitch(1, 1, 0, 60, 0x50, &pp) != 0 || pp != b1.pitch) {
            fail("bend 0x50 pitch equals the (63, fine 6) key-on pitch");
        }
    }

    if (sndNotePrepare(1, 0, 0, 60, 0, 127, 127, &note) == 0) {
        fail("unused prog must be refused");
    }
    if (sndNotePrepare(1, 1, 7, 60, 0, 127, 127, &note) == 0) {
        fail("tone 7 of prog 1 must be refused");
    }
    if (sndNotePrepare(9, 1, 0, 60, 0, 127, 127, &note) == 0) {
        fail("unknown vab must be refused");
    }

    /* --- A1: every tone covering the note, in page order ------------------ */
    {
        short tones[SND_LAYERS_MAX];

        /* prog 1: tone 0 [10,100], tone 1 [20,90], tone 2 [0,80] */
        if (sndPickTones(1, 1, 60, tones, SND_LAYERS_MAX) != 3 ||
            tones[0] != 0 || tones[1] != 1 || tones[2] != 2) {
            fail("note 60 of prog 1 layers all three tones");
        }
        if (sndPickTones(1, 1, 95, tones, SND_LAYERS_MAX) != 1 ||
            tones[0] != 0) {
            fail("note 95 of prog 1 is tone 0 only");
        }
        if (sndPickTones(1, 1, 5, tones, SND_LAYERS_MAX) != 1 ||
            tones[0] != 2) {
            fail("note 5 of prog 1 is tone 2 only");
        }
        if (sndPickTones(1, 1, 60, tones, 2) != 2) {
            fail("the cap bounds the layers");
        }
        if (sndPickTones(1, 1, 120, tones, SND_LAYERS_MAX) != 0 ||
            sndPickTones(1, 0, 60, tones, SND_LAYERS_MAX) != 0 ||
            sndPickTones(9, 1, 60, tones, SND_LAYERS_MAX) != 0) {
            fail("no tone covers: out of range, unused prog, unknown vab");
        }
        if (sndPickTone(1, 1, 60) != 0 || sndPickTone(1, 1, 120) != -1) {
            fail("sndPickTone keeps the first tone");
        }
    }

    /* --- A voice really keyed on, then released --------------------------- */
    voice = sndKeyOn(1, 1, 0, 60, 0, 127, 127);
    if (voice < 0 || voice >= SND_VOICE_COUNT) {
        fprintf(stderr, "sndtest: keyOn gave voice %d\n", voice);
        fail("sndKeyOn voice range");
    }
    if (sndKeyOff(voice, 1, 1, 0, 60) != 0) {
        fail("sndKeyOff");
    }
    if (sndKeyOn(1, 1, 0, 60, 0, 127, 127) < 0) {
        fail("sndKeyOn after keyOff");
    }
    sndAllKeyOff(0);

    /* --- A5b: voice allocation as retail _SsVmAlloc ------------------------
       (libsnd_vm_aloc1.s) fixture priorities: prog 1 tone 2 = 10, prog 2
       tone 0 = 12. A free voice goes first by index; with none free the
       lowest priority (<= the new note's) goes, then the lowest envelope
       level; a note under every busy voice is refused (-1). */
    {
        short v, got, key;

        sndAllKeyOff(0);
        for (v = 0; v < SND_VOICE_COUNT; v++) {
            key = sndKeyOn(1, 1, 2, 60, 0, 127, 127);
            if (key != v) {
                fprintf(stderr, "sndtest: fill voice %d got %d\n", v, key);
                fail("alloc: free voices fill in index order");
            }
        }
        /* equal priority: the releasing voice (lowest level) is taken */
        sndKeyOff(7, 1, 1, 2, 60);
        sndEnvAdvance(SND_ENV_TICK_SAMPLES);
        got = sndKeyOn(1, 1, 2, 60, 0, 127, 127);
        if (got != 7) {
            fprintf(stderr, "sndtest: alloc stole voice %d, expected 7\n", got);
            fail("alloc: equal priority steals the lowest envelope level");
        }
        /* a higher priority note takes a lower priority voice, not voice 0 */
        sndAllKeyOff(0);
        for (v = 0; v < SND_VOICE_COUNT; v++) {
            key = sndKeyOn(1, v == 9 ? 1 : 2, v == 9 ? 2 : 0, 60, 0, 127, 127);
        }
        got = sndKeyOn(1, 2, 0, 60, 0, 127, 127);
        if (got != 9) {
            fprintf(stderr, "sndtest: alloc stole voice %d, expected 9\n", got);
            fail("alloc: the lowest priority voice is stolen");
        }
        /* a note under every busy voice is refused */
        sndAllKeyOff(0);
        for (v = 0; v < SND_VOICE_COUNT; v++) {
            sndKeyOn(1, 2, 0, 60, 0, 127, 127);
        }
        got = sndKeyOn(1, 1, 2, 60, 0, 127, 127);
        if (got != -1) {
            fprintf(stderr, "sndtest: alloc gave voice %d, expected -1\n", got);
            fail("alloc: a lower priority note is refused");
        }
        sndAllKeyOff(0);
    }

    /* --- A5d: a late key-off of a stolen note leaves the new note alone ---
       (retail _SsVmKeyOff, libsnd_vm_key.s, matches the voices' stored
       identity instead of trusting a voice id) */
    {
        short pk, v, sepVoice, thief;
        int before, after;

        makeSepA();
        sndAllKeyOff(0);
        sndSepSetSink(&realSink, NULL);
        pk = sndSepOpen(sepBend, 1);
        if (pk < 1 || sndSepPlayStart(pk, 0, 1, 1, 127, 127) < 0) {
            fail("A5d: bend package starts");
        }
        sndSepTbyT();               /* the key-on of note 60 */
        sepVoice = realLastVoice;
        if (sepVoice < 0) {
            fail("A5d: the SEP note got a voice");
        }
        for (v = 0; v < SND_VOICE_COUNT - 1; v++) {
            sndKeyOn(1, 1, 2, 60, 0, 127, 127);
        }
        thief = sndKeyOn(1, 1, 2, 60, 0, 127, 127);
        if (thief != sepVoice) {
            fprintf(stderr, "sndtest: thief %d sep voice %d\n", thief, sepVoice);
            fail("A5d: the steal takes the SEP voice");
        }
        sndEnvAdvance(2 * SND_ENV_TICK_SAMPLES);
        before = sndEnvVoiceLevel(thief);
        sndSepPlayStop(pk, 0);      /* the old note's late key-off */
        sndEnvAdvance(SND_ENV_TICK_SAMPLES);
        after = sndEnvVoiceLevel(thief);
        if (!sndEnvVoiceLive(thief) || after < before) {
            fprintf(stderr, "sndtest: stolen voice level %d -> %d\n", before,
                    after);
            fail("A5d: a stale key-off must not release the new note");
        }
        sndSepClose(pk);
        sndSepSetSink(NULL, NULL);
        sndAllKeyOff(0);
    }

    /* --- A6e: a sample that reaches its end flag frees its voice ---------
       (the SPU mutes a voice at an end block without repeat, ENVX = 0, and
       _SsVmFlush gives it back, libsnd_vm_f.s:46-58). The disc's footsteps
       (SsUtKeyOn, no key-off) have a never-decaying sustain: only this ends
       them. Fixture VAG 3 = 8 blocks, the last one the end flag = 224
       samples; at pitch 0x1000 that is 224 clock samples. */
    {
        unsigned char vag[16 * 4];
        short vend, vloop;

        memset(vag, 0, sizeof(vag));
        vag[16 * 2 + 1] = 1;
        if (sndVagEndSamples(vag, sizeof(vag)) != 3 * 28) {
            fail("A6e: an end block stops the sample after it");
        }
        vag[16 * 2 + 1] = 3;
        if (sndVagEndSamples(vag, sizeof(vag)) != 0) {
            fail("A6e: end + repeat loops, no sample end");
        }
        vag[16 * 2 + 1] = 0;
        if (sndVagEndSamples(vag, sizeof(vag)) != 0) {
            fail("A6e: no end flag, no sample end");
        }
        sndAllKeyOff(0);
        body[FIX_BODY - 16 + 1] = 0x01; /* VAG 3 now ends: a one-shot */
        if (sndVabTransBody(body, 1) != 1) {
            fail("A6e: body rewrite");
        }
        vend = sndKeyOn(1, 1, 2, 44, 0, 127, 127); /* VAG 3 */
        vloop = sndKeyOn(1, 1, 0, 60, 0, 127, 127); /* VAG 1, no end flag */
        if (vend < 0 || vloop < 0) {
            fail("A6e: key-ons start");
        }
        sndEnvAdvance(100);
        if (!sndEnvVoiceLive(vend)) {
            fail("A6e: the sample is still playing at 100 samples");
        }
        sndEnvAdvance(200);
        if (sndEnvVoiceLive(vend)) {
            fail("A6e: a sample past its end flag must free its voice");
        }
        if (!sndEnvVoiceLive(vloop)) {
            fail("A6e: a VAG without an end flag keeps its voice");
        }
        body[FIX_BODY - 16 + 1] = 0x03;
        if (sndVabTransBody(body, 1) != 1) {
            fail("A6e: body restore");
        }
        sndAllKeyOff(0);
    }

    /* --- O4: the player's volume sliders ----------------------------------- */
    {
        short vE, vM, e0, m0, k;
        short el, er, ml, mr, el0, er0;

        /* the taper: quadratic, 0 silent, 100 the game's own level */
        if (sndVolTaperPermille(100) != 1000 || sndVolTaperPermille(0) != 0 ||
            sndVolTaperPermille(50) != 250 || sndVolTaperPermille(10) != 10 ||
            sndVolTaperPermille(150) != 1000 || sndVolTaperPermille(-5) != 0) {
            fail("O4 slider taper");
        }
        sndSetUserVolume(SND_VOL_MUSIC, 50);
        if (sndUserVolume(SND_VOL_MUSIC) != 50 || sndUserGainPermille(SND_VOL_MUSIC) != 250 ||
            sndUserGainPermille(SND_VOL_EFFECTS) != 1000 || sndUserGainPermille(SND_VOL_MOVIE) != 1000) {
            fail("O4 music slider leaves the other classes alone");
        }
        sndSetUserVolume(SND_VOL_MASTER, 50);
        if (sndUserGainPermille(SND_VOL_MUSIC) != 62 || sndUserGainPermille(SND_VOL_EFFECTS) != 250 ||
            sndUserGainPermille(SND_VOL_MOVIE) != 250) {
            fail("O4 master multiplies every class");
        }
        sndSetUserVolume(SND_VOL_MASTER, 100);
        sndSetUserVolume(SND_VOL_MUSIC, 150);
        if (sndUserVolume(SND_VOL_MUSIC) != 100) {
            fail("O4 slider clamps");
        }
        if (sndScaleVol(14400, 250) != 3600 || sndScaleVol(14400, 1000) != 14400 || sndScaleVol(14400, 0) != 0) {
            fail("O4 sndScaleVol");
        }
        if (!sndSrcIsMusic(1) || sndSrcIsMusic(0) || sndSrcIsMusic(2)) {
            fail("O4 key-on source -> class");
        }

        /* two live voices of the same note, one effect and one music: each
           slider moves only its own class, the envelope and the retail chain
           underneath stay as they were */
        sndAllKeyOff(0);
        vE = sndKeyOnSrc(1, 1, 0, 60, 0, 127, 127, 0);
        vM = sndKeyOnSrc(1, 1, 0, 60, 0, 127, 127, 1);
        if (vE < 0 || vM < 0 || sndVoiceIsMusic(vE) != 0 || sndVoiceIsMusic(vM) != 1 ||
            sndVoiceIsMusic(23) != -1) {
            fail("O4 voice classes");
        }
        sndVoiceOutVol(vE, &el0, &er0);
        sndVoiceOutVol(vM, &ml, &mr);
        if (el0 <= 0 || el0 != ml || er0 != mr) {
            fprintf(stderr, "sndtest: O4 equal voices %d/%d vs %d/%d\n", el0, er0, ml, mr);
            fail("O4 sliders at 100 % leave the level");
        }
        sndSetUserVolume(SND_VOL_MUSIC, 50);
        sndVoiceOutVol(vE, &el, &er);
        sndVoiceOutVol(vM, &ml, &mr);
        if (el != el0 || er != er0 || ml != sndScaleVol(el0, 250) || mr != sndScaleVol(er0, 250)) {
            fprintf(stderr, "sndtest: O4 music 50: effect %d/%d music %d/%d (base %d/%d)\n", el, er, ml, mr, el0, er0);
            fail("O4 music slider only moves music voices");
        }
        sndSetUserVolume(SND_VOL_MUSIC, 100);
        sndSetUserVolume(SND_VOL_EFFECTS, 0);
        sndVoiceOutVol(vE, &el, &er);
        sndVoiceOutVol(vM, &ml, &mr);
        if (el != 0 || er != 0 || ml != el0 || mr != er0) {
            fail("O4 effects slider at 0 mutes effects only");
        }
        sndSetUserVolume(SND_VOL_EFFECTS, 100);
        sndSetUserVolume(SND_VOL_MASTER, 0);
        sndVoiceOutVol(vE, &el, &er);
        sndVoiceOutVol(vM, &ml, &mr);
        if (el != 0 || er != 0 || ml != 0 || mr != 0) {
            fail("O4 master at 0 mutes everything");
        }
        sndSetUserVolume(SND_VOL_MASTER, 100);
        sndAllKeyOff(0);

        /* the SEP player tells music from effects through the game's query, at
           the moment the sequence starts; unset, every sequence is music */
        {
            static unsigned char sepFx[6 + 13 + 12];
            unsigned char *ev = sepFx + 6 + 13;
            short pk;

            /* program 1, note 60, then the usual end-of-record controller */
            memset(sepFx, 0, sizeof(sepFx));
            sepFx[0] = 'p'; sepFx[1] = 0x51; sepFx[2] = 'E'; sepFx[3] = 'S';
            sepPutRec(sepFx + 6, 9, SEP_RES, SEP_TEMPO0, 12);
            ev[0] = 0x00; ev[1] = 0xC0; ev[2] = 1;
            ev[3] = 0x00; ev[4] = 0x90; ev[5] = 60; ev[6] = 100;
            ev[7] = 0x81; ev[8] = 0x4C; ev[9] = 0xB0; ev[10] = 0x07; ev[11] = 0x00;
            sndSepSetSink(NULL, NULL); /* back to the engine sink */
            sndSepSetMusicQuery(NULL);
            for (k = 0; k < 3; k++) {
                if (k == 1) {
                    sndSepSetMusicQuery(fakeQueryEffect);
                } else if (k == 2) {
                    sndSepSetMusicQuery(fakeQueryMusic);
                }
                pk = sndSepOpen(sepFx, 1);
                if (pk < 1 || sndSepPlayStart(pk, 0, 1, 1, 127, 127) < 0) {
                    fail("O4: fx package starts");
                }
                sndSepTbyT();
                e0 = m0 = 0;
                for (vE = 0; vE < SND_VOICE_COUNT; vE++) {
                    if (sndVoiceIsMusic(vE) == 1) {
                        m0++;
                    } else if (sndVoiceIsMusic(vE) == 0) {
                        e0++;
                    }
                }
                if (k == 1 ? (e0 < 1 || m0 != 0) : (m0 < 1 || e0 != 0)) {
                    fprintf(stderr, "sndtest: O4 query %d: %d music %d effect voices\n", k, m0, e0);
                    fail("O4 SEP sequence class follows the query");
                }
                sndSepPlayStop(pk, 0);
                sndSepClose(pk);
                sndAllKeyOff(0);
            }
            sndSepSetMusicQuery(NULL);
        }
    }

    /* --- A4: the SPU ADSR envelope, against the psx-spx spec ---------------- */
    {
        SndEnv env;
        int n, prev, endAttack = -1, endDecay = -1;

        /* attack: linear, shift 10, step +7: cycles 1, step 7 << 1 = 14 a
           sample, so 0x7FFF is first reached on sample ceil(32767 / 14) = 2341
           (adsr1 0x2857: attack shift 10, decay shift 5, sustain level 7) */
        sndEnvInit(&env, 0x2857, 0x4C08);
        for (n = 1; n <= 2340; n++) {
            sndEnvStep(&env);
        }
        if (env.phase != SND_ENV_ATTACK || env.level != 14 * 2340) {
            fail("env attack still rising at sample 2340");
        }
        sndEnvStep(&env);
        if (env.level != SND_ENV_MAX || env.phase != SND_ENV_DECAY) {
            fail("env attack reaches the maximum on sample 2341");
        }

        /* a slow attack: shift 13 is cycles 4, step 7: 4 * ceil(32767 / 7) = 4 * 4681 */
        sndEnvInit(&env, 0x3400, 0x4C08);
        for (n = 1; n <= 4 * 4681 + 8 && env.phase == SND_ENV_ATTACK; n++) {
            sndEnvStep(&env);
        }
        if (n != 4 * 4681 + 1 || env.level != SND_ENV_MAX) {
            fprintf(stderr, "sndtest: slow attack ended after %d samples\n", n - 1);
            fail("env slow attack length");
        }

        /* decay: exponential, shift 5 (step -8 << 6 = -512) with the level
           scaling, down to the sustain level 0x4000; each step is
           level += (-512 * level) >> 15, i.e. -level / 64 rounded away */
        sndEnvInit(&env, 0x2857, 0x4C08);
        env.phase = SND_ENV_DECAY;
        env.level = SND_ENV_MAX;
        prev = env.level;
        for (n = 1; n < 100000 && env.phase == SND_ENV_DECAY; n++) {
            sndEnvStep(&env);
            if (env.level >= prev) {
                fail("env decay must fall every sample");
            }
            if (env.level != prev + ((-512 * prev) >> 15)) {
                fail("env decay is exponential (step scales with the level)");
            }
            prev = env.level;
        }
        endDecay = env.level;
        if (env.phase != SND_ENV_SUSTAIN || endDecay > 0x4000 ||
            endDecay < 0x4000 - 512) {
            fprintf(stderr, "sndtest: decay ended at %#x\n", endDecay);
            fail("env decay ends at the sustain level");
        }
        (void)endAttack;

        /* a decreasing linear sustain with no key-off ends the voice: adsr2
           0x4C08 = sustain linear decrease, shift 12 (cycles 2, step -8):
           0x4000 / 8 steps of 2 samples = 4096 samples */
        sndEnvInit(&env, 0x2857, 0x4C08);
        env.phase = SND_ENV_SUSTAIN;
        env.level = 0x4000;
        for (n = 1; n <= 4095; n++) {
            sndEnvStep(&env);
        }
        if (env.phase != SND_ENV_SUSTAIN || env.level != 8) {
            fail("env decreasing sustain still sounding before its end");
        }
        sndEnvStep(&env);
        sndEnvStep(&env);
        if (env.phase != SND_ENV_DONE || env.level != 0) {
            fail("env decreasing sustain ends at level 0");
        }

        /* an increasing sustain holds the maximum until the key-off, then
           release (linear, shift 10: step -16 a sample) takes
           ceil(32767 / 16) = 2048 samples to 0 */
        sndEnvInit(&env, 0x2857, 0x0A0A);
        env.phase = SND_ENV_SUSTAIN;
        env.level = SND_ENV_MAX;
        for (n = 0; n < 100000; n++) {
            sndEnvStep(&env);
        }
        if (env.phase != SND_ENV_SUSTAIN || env.level != SND_ENV_MAX) {
            fail("env increasing sustain holds with no key-off");
        }
        sndEnvKeyOff(&env);
        for (n = 1; n <= 2047; n++) {
            sndEnvStep(&env);
        }
        if (env.phase != SND_ENV_RELEASE || env.level <= 0) {
            fail("env release still fading at sample 2047");
        }
        sndEnvStep(&env);
        if (env.phase != SND_ENV_DONE || env.level != 0) {
            fail("env release reaches 0 on sample 2048");
        }

        /* the voice level: tone 2 (adsr1 0x0800, adsr2 0x000A) rises, holds at
           the maximum in an increasing sustain, and after the key-off is gone
           in 2048 samples and its voice freed */
        voice = sndKeyOn(1, 1, 2, 44, 0, 127, 127);
        if (voice < 0) {
            fail("env voice key-on");
        }
        sndEnvAdvance(5000);
        if (!sndEnvVoiceLive(voice) || sndEnvVoiceLevel(voice) != SND_ENV_MAX) {
            fail("env voice holds at the maximum before the key-off");
        }
        sndKeyOff(voice, 1, 1, 2, 44);
        sndEnvAdvance(2040);
        if (!sndEnvVoiceLive(voice)) {
            fail("env voice still releasing");
        }
        sndEnvAdvance(8);
        if (sndEnvVoiceLive(voice)) {
            fail("env voice freed once the release reaches 0");
        }

        /* tone 1 (adsr2 0x401F): a decreasing sustain, so with no key-off the
           voice ends by itself */
        voice = sndKeyOn(1, 1, 1, 62, 0, 127, 127);
        if (voice < 0) {
            fail("env voice 2 key-on");
        }
        sndEnvAdvance(2000);
        if (sndEnvVoiceLive(voice)) {
            fail("env voice with a decreasing sustain ends without key-off");
        }
    }
    sndAllKeyOff(0);

    /* And again: two banks overwriting one slot keep the registry consistent */
    if (sndVabOpen(vh, 2, 0x62410) != 3) {
        fail("sndVabOpen slot 2");
    }
    if (sndNotePrepare(3, 2, 0, 45, 0, 127, 127, &note) != 0) {
        fail("prepare in slot 2");
    }
    if (note.addr != 0x62410 + FIX_VAG1 + 0) {
        fail("addr of slot 2's own base");
    }
    sndVabClose(3);
    if (sndNotePrepare(3, 2, 0, 45, 0, 127, 127, &note) == 0) {
        fail("closed vab must be refused");
    }

    /* A6: the program's own volume (ProgAtr byte 1) and pan (byte 4) scale
       every note of the program, retail _SsVmSetVol (libsnd_vm_vol.s) reads
       both next to the tone's. Central Park's bank (stage 330) carries
       programs at volume 40..52 / pan 40..70 that were played at 127 / centre,
       so the mix peaked at twice full scale. Prog 1 tone 0 (vol 120, pan 64):
       program volume 64 and pan 32 -> 120*64/127 = 60 -> 60<<7 = 7680 on the
       left, the right at 32/64 of it. */
    {
        struct SndNote plain, scaled;

        vh[0x20 + 1 * 16 + 1] = 64;
        vh[0x20 + 1 * 16 + 4] = 32;
        if (sndVabOpen(vh, 1, 0x49c10) != 2) {
            fail("A6 sndVabOpen slot 1");
        }
        vh[0x20 + 1 * 16 + 1] = 0x7F;
        vh[0x20 + 1 * 16 + 4] = 64;
        if (sndNotePrepare(2, 1, 0, 60, 0, 127, 127, &scaled) != 0 ||
            sndNotePrepare(1, 1, 0, 60, 0, 127, 127, &plain) != 0) {
            fail("A6 prepare");
        }
        /* A6b: retail squares each side as its last step (libsnd_vm_vol.s
           28408..28444: mult a2,a2 / mult a1,a1, then x * 0x40011 >> 13, that
           is x / 16384), so a side of 7680 plays at 7680^2/16384 = 3600 and
           the quiet voices of a mix fall much faster than the loud ones. */
        if (plain.volL != 14400 || plain.volR != 14400) {
            fprintf(stderr, "sndtest: A6b plain %d/%d expected 14400\n",
                    plain.volL, plain.volR);
            fail("A6b retail squares the side volume");
        }
        if (scaled.volL != 3600 || scaled.volR != 900) {
            fprintf(stderr, "sndtest: A6 vol %d/%d expected 3600/900\n",
                    scaled.volL, scaled.volR);
            fail("A6 program volume and pan scale the note");
        }
        sndVabClose(2);
    }


    /* O4: the VAB header's master volume (VH byte 0x18) is retail's second
       factor: libsnd_vm_vol.s:142-153 loads D_80081DEC (the vab's header, set
       by libsnd_vm_vsu.s:54) and multiplies velocity/127 by mvol * 0x3FFF
       (0x28184: sll 14, subu), before the program and tone volumes, the pan
       steps and the final squaring. So it is linear in the amplitude before
       the square: mvol 64 of 127 on tone 0 of prog 1 (vol 120): 120 * 64 / 127
       = 60 -> 60 << 7 = 7680, squared / 16384 = 3600 on both sides. */
    {
        struct SndNote half, full;

        vh[0x18] = 64;
        if (sndVabOpen(vh, 1, 0x49c10) != 2) {
            fail("O4 sndVabOpen slot 1");
        }
        vh[0x18] = 127;
        if (sndNotePrepare(2, 1, 0, 60, 0, 127, 127, &half) != 0 ||
            sndNotePrepare(1, 1, 0, 60, 0, 127, 127, &full) != 0) {
            fail("O4 mvol prepare");
        }
        if (full.volL != 14400 || half.volL != 3600 || half.volR != 3600) {
            fprintf(stderr, "sndtest: O4 mvol vol %d/%d (full %d) expected 3600/3600 (14400)\n",
                    half.volL, half.volR, full.volL);
            fail("O4 the vab master volume scales the note");
        }
        sndVabClose(2);
    }

    /* --- T35: the SEP side ----------------------------------------------- */
    {
        SndSep parsed;
        short sepId, sepId2, slot, ringVoice;
        int rec, i;

        makeSep();
        makeSepNoEnd();

        /* The parser, on owned memory */
        if (sndParseSep(sep, sizeof(sep), &parsed) != 0) {
            fail("sndParseSep");
        }
        if (parsed.recCount != 1) {
            fprintf(stderr, "sndtest: sep records %d\n", parsed.recCount);
            fail("sep record count");
        }
        rec = 0;
        if (parsed.recs[rec].id != 1 || parsed.recs[rec].res != SEP_RES ||
            parsed.recs[rec].tempo != SEP_TEMPO0 ||
            parsed.recs[rec].count != 10) {
            fprintf(stderr, "sndtest: rec id %d res %d tempo %lu count %lu\n",
                    (int)parsed.recs[rec].id, (int)parsed.recs[rec].res,
                    parsed.recs[rec].tempo, parsed.recs[rec].count);
            fail("record fields");
        }
        /* The markers: the loop body resumes at the key-on inside it */
        if (parsed.recs[rec].loopBody != 6 ||
            parsed.recs[rec].loopStartTick != 40 ||
            parsed.recs[rec].loopEndTick != 56 ||
            parsed.recs[rec].loopCount != 0x7F) {
            fprintf(stderr, "sndtest: loop %ld %lu %lu %d\n",
                    parsed.recs[rec].loopBody, parsed.recs[rec].loopStartTick,
                    parsed.recs[rec].loopEndTick, (int)parsed.recs[rec].loopCount);
            fail("loop markers");
        }
        /* Spot events */
        if (parsed.evs[0].kind != SND_EV_CTRL || parsed.evs[0].ch != 0 ||
            parsed.evs[0].note != 7 || parsed.evs[0].data != 100 ||
            parsed.evs[0].tick != 0) {
            fail("ctrl event");
        }
        if (parsed.evs[1].kind != SND_EV_KEYON || parsed.evs[1].ch != 0 ||
            parsed.evs[1].note != 60 || parsed.evs[1].data != 100 ||
            parsed.evs[1].tick != 0) {
            fail("first key-on");
        }
        if (parsed.evs[3].kind != SND_EV_PROG || parsed.evs[3].ch != 0 ||
            parsed.evs[3].data != 2 || parsed.evs[3].tick != 16) {
            fail("program change");
        }
        if (parsed.evs[6].kind != SND_EV_KEYON || parsed.evs[6].note != 72 ||
            parsed.evs[6].data != 120 || parsed.evs[6].tick != 40) {
            fail("loop body's key-on");
        }
        if (parsed.evs[8].kind != SND_EV_TEMPO ||
            parsed.evs[8].datum != SEP_TEMPO51 || parsed.evs[8].tick != 56) {
            fail("tempo event");
        }
        if (parsed.evs[9].kind != SND_EV_END || parsed.evs[9].tick != 56) {
            fail("end event");
        }
        sndSepFree(&parsed);

        /* An unknown meta ends the record's decodable span; the next record
           of the same package still decodes */
        if (sndParseSep(sepMeta, sizeof(sepMeta), &parsed) != 0) {
            fail("sndParseSep meta");
        }
        if (parsed.recCount != 2 || parsed.recs[0].count != 2 ||
            parsed.recs[1].count != 1 || parsed.recs[1].id != 11) {
            fprintf(stderr, "sndtest: meta records %d counts %lu %lu id %d\n",
                    parsed.recCount, parsed.recs[0].count, parsed.recs[1].count,
                    (int)parsed.recs[1].id);
            fail("meta cuts the record");
        }
        sndSepFree(&parsed);

        /* The refusals the game's own malformed data would hit */
        sep[0] = 'q';
        if (sndParseSep(sep, sizeof(sep), &parsed) == 0) {
            sep[0] = 'p';
            fail("bad magic must be refused");
        }
        sep[0] = 'p';
        if (sndParseSep(sep, 0x20, &parsed) == 0) {
            fail("truncated package must be refused");
        }

        /* The player, driven through the recording sink. Volumes: the
           composite is velocity * package vol * channel vol * expression *
           master, all 127 by default here but the CC7 the parser rides
           (100/127) on ch 0. */
        sndSepSetSink(&fakeSink, NULL);
        sepId = sndSepOpen(sep, 1);
        if (sepId < 1) {
            fail("sndSepOpen");
        }
        sepId2 = sndSepOpen(sepNoEnd, 1);
        if (sepId2 <= sepId) {
            fail("sndSepOpen second id");
        }
        if (sndSepPlayStart(sepId, 1, 1, 1, 127, 127) >= 0) {
            fail("out-of-range record refused");
        }
        if (sndSepPlayStart((short)(sepId2 + 1), 0, 1, 1, 127, 127) >= 0) {
            fail("unknown package refused");
        }
        slot = sndSepPlayStart(sepId, 0, 1, 1, 127, 127);
        if (slot < 0) {
            fail("sndSepPlayStart");
        }

        /* frame 1, ticks 0..16: the CC7 the key-on reads, the key-on of
           note 60 (100 * 100/127), its key-off and the program change */
        sndSepTbyT();
        if (fakeOnCount != 1 || fakeOffCount != 1) {
            fail("frame 1 delivery");
        }
        if (fakeOn[0].vabId != 1 || fakeOn[0].prog != 0 ||
            fakeOn[0].note != 60) {
            fail("frame 1 note");
        }
        if (fakeOn[0].volL != 78 || fakeOn[0].volR != 78) {
            fprintf(stderr, "sndtest: frame1 vol %d/%d\n",
                    fakeOn[0].volL, fakeOn[0].volR);
            fail("frame 1 volume");
        }
        if (fakeOff[0] != 0) {
            fail("frame 1 release");
        }

        /* frame 2, ticks 16..32: the key-on under the new program */
        sndSepTbyT();
        if (fakeOnCount != 2 || fakeOn[1].prog != 2 ||
            fakeOn[1].note != 67 || fakeOn[1].volL != 62) {
            fail("frame 2 delivery");
        }

        /* frame 3, ticks 32..48: the loop's key-on and the two releases
           the window up to and including tick 48 delivers (note 67's own
           and the loop body's, the same inclusive boundary frame 1 used
           for the key-off at its own tick) */
        sndSepTbyT();
        if (fakeOnCount != 3 || fakeOn[2].note != 72 ||
            fakeOn[2].volL != 94 || fakeOffCount != 3 || fakeOff[1] != 1 ||
            fakeOff[2] != 2) {
            fail("frame 3 delivery");
        }

        /* frame 4, ticks 48..64: the loop ends (tick 56, the tempo change to
           250000 rides there) and the player jumps back to it. The frame ran
           8 ticks past the loop's end, so the jump lands 8 ticks into the
           body (tick 48) and the body's key-on (40) and key-off (48) are
           delivered in this very frame, not one frame late (A5a). */
        sndSepTbyT();
        if (fakeOnCount != 4 || fakeOn[3].note != 72 ||
            fakeOffCount != 4 || fakeOff[3] != 3) {
            fprintf(stderr, "sndtest: loop overshoot on %d off %d\n",
                    fakeOnCount, fakeOffCount);
            fail("loop jump keeps the overshoot");
        }

        /* frame 5: the tempo is now 250000, 38.4 ticks a frame, longer than
           the 16-tick body: the frame wraps the body twice (the first jump
           lands at tick 70.4, still past the loop's end, the second at 54.4),
           so the body's key-on and key-off play twice more in it */
        sndSepTbyT();
        if (fakeOnCount != 6 || fakeOn[4].note != 72 || fakeOn[5].note != 72 ||
            fakeOffCount != 6 || fakeOff[5] != 5) {
            fprintf(stderr, "sndtest: loop replay on %d off %d\n",
                    fakeOnCount, fakeOffCount);
            fail("loop replay");
        }
        /* rewind the recorder to its pre-loop mark: the checks below keep
           their absolute counts */
        fakeOnCount = 4;
        fakeOffCount = 4;

        /* nothing left ringing at the stop */
        sndSepPlayStop(sepId, 0);
        if (fakeOffCount != 4) {
            fail("stop with nothing ringing");
        }

        /* The no-end record: a key-on books a voice and rings; the stop
           releases it explicitly */
        slot = sndSepPlayStart(sepId2, 0, 1, 1, 127, 127);
        if (slot < 0) {
            fail("sndSepPlayStart no-end");
        }
        sndSepTbyT();
        if (fakeOnCount != 5) {
            fail("no-end key-on");
        }
        ringVoice = fakeVoiceNext;
        ringVoice--;
        sndSepPlayStop(sepId2, 0);
        if (fakeOffCount != 5 || fakeOff[4] != ringVoice) {
            fprintf(stderr, "sndtest: stop ring off %d voice %d\n",
                    fakeOffCount, fakeOffCount ? fakeOff[4] : -1);
            fail("stop releases the ringing voice");
        }

        /* Natural exhaustion, no markers and no end meta: the span runs to
           tick 204 and the ringing voice is released there */
        if (sndSepPlayStart(sepId2, 0, 1, 1, 127, 127) < 0) {
            fail("sndSepPlayStart again");
        } else {
            for (i = 0; i < 14; i++) {
                sndSepTbyT();
            }
        }
        if (fakeOnCount != 6 || fakeOffCount != 6) {
            fprintf(stderr, "sndtest: exhaustion on %d off %d\n",
                    fakeOnCount, fakeOffCount);
            fail("exhaustion without a loop");
        }

        /* A decrescendo: 0x40 over 4 frames decays 16 a frame, applied
           before the frame's own deliveries. Restarting does not reset the
           package volume the game sets between a stop and a play. */
        sndSepPlaySetVol(sepId2, 0, 127, 127);
        if (sndSepPlayStart(sepId2, 0, 1, 1, 127, 127) < 0) {
            fail("sndSepPlayStart decrescendo");
        }
        sndSepPlayDecrescendo(sepId2, 0, 0x40, 4);
        sndSepTbyT();   /* the package volume is 127-16 = 111 at delivery */
        if (fakeOnCount != 7 || fakeOn[6].volL != 87) {
            fprintf(stderr, "sndtest: decrescendo on %d vol %d\n",
                    fakeOnCount, fakeOnCount > 6 ? fakeOn[6].volL : -1);
            fail("decrescendo step");
        }
        sndSepPlayStop(sepId2, 0);
        sndSepClose(sepId2);

        if (sndSepOpen(sep, 0) >= 0) {
            fail("sndSepOpen takes vabId 0 (no such bank)");
        }
        sndSepClose(sepId);

        /* A2: two records of one package play together; a stop or a replay
           touches only its own record */
        makeSepA();
        {
            short pk;
            int on0, off0;

            pk = sndSepOpen(sepTwo, 1);
            if (pk < 1) {
                fail("sndSepOpen two-record package");
            }
            on0 = fakeOnCount;
            off0 = fakeOffCount;
            if (sndSepPlayStart(pk, 0, 1, 1, 127, 127) < 0 ||
                sndSepPlayStart(pk, 1, 1, 1, 127, 127) < 0) {
                fail("two records of one package start");
            }
            sndSepTbyT();
            if (fakeOnCount != on0 + 2 || fakeOffCount != off0 ||
                fakeOn[on0].note != 60 || fakeOn[on0 + 1].note != 61) {
                fprintf(stderr, "sndtest: concurrent on %d off %d\n",
                        fakeOnCount - on0, fakeOffCount - off0);
                fail("playing record 1 must not stop record 0");
            }
            sndSepPlayStop(pk, 0);
            if (fakeOffCount != off0 + 1) {
                fail("stopping record 0 releases only its own voice");
            }
            sndSepTbyT();
            if (fakeOnCount != on0 + 2) {
                fail("a stopped record delivers nothing more");
            }
            /* record 1 still plays: replaying it restarts it (its voice
               back, then its key-on again) */
            if (sndSepPlayStart(pk, 1, 1, 1, 127, 127) < 0) {
                fail("replay of a playing record");
            }
            if (fakeOffCount != off0 + 2) {
                fail("a replay releases the old run's voice");
            }
            sndSepTbyT();
            if (fakeOnCount != on0 + 3 || fakeOn[on0 + 2].note != 61) {
                fail("a replay keys on again from the top");
            }
            sndSepClose(pk);
            if (fakeOffCount != off0 + 3) {
                fail("closing the package releases its running record");
            }
        }

        /* A1: a key-on that books two voices is released by one key-off */
        {
            short pk;
            int on0 = fakeOnCount, off0 = fakeOffCount;
            short first;

            fakeLayerNote = 61;
            first = fakeVoiceNext;
            pk = sndSepOpen(sepLay, 1);
            if (pk < 1 || sndSepPlayStart(pk, 0, 1, 1, 127, 127) < 0) {
                fail("layer package starts");
            }
            sndSepTbyT();   /* ticks 0..16: the key-on and its key-off */
            fakeLayerNote = -1;
            if (fakeOnCount != on0 + 1 || fakeOffCount != off0 + 2 ||
                fakeVoiceNext != first + 2 ||
                fakeOff[off0] != first || fakeOff[off0 + 1] != first + 1) {
                fprintf(stderr, "sndtest: layer on %d off %d voices %d\n",
                        fakeOnCount - on0, fakeOffCount - off0,
                        fakeVoiceNext - first);
                fail("one key-off releases both layered voices");
            }
            sndSepClose(pk);
        }

        /* A3: a bend reaches the voices ringing on its channel only */
        {
            short pk;
            short first;

            fakeBendCount = 0;
            first = fakeVoiceNext;
            pk = sndSepOpen(sepBend, 1);
            if (pk < 1 || sndSepPlayStart(pk, 0, 1, 1, 127, 127) < 0) {
                fail("bend package starts");
            }
            sndSepTbyT();   /* ticks 0..16: key-on, bend max, running bend */
            if (fakeBendCount != 2 || fakeBend[0].voice != first ||
                fakeBend[0].msb != 0x7F || fakeBend[1].voice != first ||
                fakeBend[1].msb != 0x40) {
                fprintf(stderr, "sndtest: bend count %d (%d/%02x %d/%02x)\n",
                        fakeBendCount, fakeBend[0].voice, fakeBend[0].msb,
                        fakeBend[1].voice, fakeBend[1].msb);
                fail("the sink gets the voice and the MSB of each bend");
            }
            sndSepClose(pk);
        }
    }

    /* --- T36: the XA (movie audio) side ---------------------------------- */
    {
        /*
         * The synthetic raw frames, built by hand. The framing is the EU
         * disc's own: sync 00 + 10*FF + 00, a 4-byte BCD header, a subheader
         * stored twice, then 18 sound groups. An audio sector is submode 0x64
         * (RT | FORM2 | AUDIO); the disc's own audio is file 1, channel 1,
         * coding 0x01 (stereo, 37800 Hz, 4-bit).
         *
         * The layout fixture pins the spec: four blocks with distinct shifts
         * and nibbles, L/R interleaved per frame. The history fixture pins the
         * per-channel predictor across block, group and sector boundaries.
         */
        int i, t, lastL, lastR;
        short pcm[SND_XA_SECTOR_SAMPLES];
        SndXaState st;
        SndXaSector sec;

        makeXaFrames();

        /* The classification: the disc's own two kinds of sector, and a
           frame that is too short to hold a payload */
        if (!sndXaClassify(xaFrames[0], SND_XA_FRAME_SIZE, &sec) ||
            sec.kind != SND_XA_KIND_VIDEO || sec.file != 0 || sec.channel != 1 ||
            sec.submode != 0x48) {
            fail("xa classify video");
        }
        if (!sndXaClassify(xaFrames[1], SND_XA_FRAME_SIZE, &sec) ||
            sec.kind != SND_XA_KIND_AUDIO || sec.file != 1 || sec.channel != 1 ||
            sec.submode != 0x64 || sec.coding != 0x01) {
            fprintf(stderr, "sndtest: xa audio kind %d file %d chan %d sm %02x cd %02x\n",
                    sec.kind, sec.file, sec.channel, sec.submode, sec.coding);
            fail("xa classify audio");
        }
        if (sndXaClassify(xaFrames[1], SND_XA_FRAME_SIZE - 1, &sec)) {
            fail("xa truncated frame must be refused");
        }

        /* Stereo layout: exact 2016 L/R frames. Frame i within a group is
           block i/28, sample i%28 of that block; block b shifts by b. */
        sndXaResetState(&st);
        if (sndXaDecodeAudio(xaFrames[1], SND_XA_FRAME_SIZE, pcm,
                             SND_XA_SECTOR_SAMPLES, &st) !=
            SND_XA_SECTOR_SAMPLES) {
            fail("xa decode sample count");
        }
        for (i = 0; i < SND_XA_GROUPS * 112; i++) {
            int idx = i % 112;
            int b = idx / 28;
            int j = idx % 28;
            int l = (j % 8) << b;
            int r = ((j % 8) - 8) << b;

            if (pcm[i * 2] != l || pcm[i * 2 + 1] != r) {
                fprintf(stderr, "sndtest: xa layout frame %d got %d/%d "
                        "expected %d/%d\n", i, pcm[i * 2], pcm[i * 2 + 1],
                        l, r);
                fail("xa stereo layout");
            }
        }
        /* the last frame of the sector is group 17, block 3, sample 27 */
        if (pcm[4030] != 24 || pcm[4031] != -40) {
            fprintf(stderr, "sndtest: xa last frame %d/%d\n",
                    pcm[4030], pcm[4031]);
            fail("xa layout tail");
        }

        /* Mono layout: 4032 frames, block b's first half then second half. */
        sndXaResetState(&st);
        if (sndXaDecodeAudio(xaFrames[3], SND_XA_FRAME_SIZE, pcm,
                             SND_XA_SECTOR_SAMPLES, &st) !=
            SND_XA_SECTOR_SAMPLES) {
            fail("xa mono sample count");
        }
        for (i = 0; i < SND_XA_MONO_FRAMES; i++) {
            int rem = i % 224;     /* one group: 4 blocks * 2 halves * 28 */
            int b = rem / 56;
            int r = rem % 56;
            int pass = r / 28;
            int j = r % 28;
            int v = pass == 0 ? (j % 8) << b
                              : (-((j % 8) + 1)) << (3 - b);

            if (pcm[i] != v) {
                fprintf(stderr, "sndtest: xa mono frame %d got %d expected %d\n",
                        i, pcm[i], v);
                fail("xa mono layout");
            }
        }

        /* History: filter 1, constant nibbles. A fresh stream ramps 7, 14,
           20... (left) and 5, 10, 14... (right); the next blocks, groups and
           the next sector continue that ramp instead of restarting. */
        sndXaResetState(&st);
        if (sndXaDecodeAudio(xaFrames[2], SND_XA_FRAME_SIZE, pcm,
                             SND_XA_SECTOR_SAMPLES, &st) !=
            SND_XA_SECTOR_SAMPLES) {
            fail("xa history sample count");
        }
        if (pcm[0] != 7 || pcm[1] != 5 || pcm[2] != 14 || pcm[3] != 10 ||
            pcm[4] != 20 || pcm[5] != 14) {
            fprintf(stderr, "sndtest: xa history start %d %d %d %d %d %d\n",
                    pcm[0], pcm[1], pcm[2], pcm[3], pcm[4], pcm[5]);
            fail("xa history start");
        }
        /* frame 28 is block 1's first sample: it must continue frame 27 */
        if (pcm[56] != xaHistNext(pcm[54], 7) ||
            pcm[57] != xaHistNext(pcm[55], 5) || pcm[56] == 7 ||
            pcm[57] == 5) {
            fprintf(stderr, "sndtest: xa block reset %d/%d\n",
                    pcm[56], pcm[57]);
            fail("xa history block continuity");
        }
        /* frame 112 is group 1's first sample */
        if (pcm[224] != xaHistNext(pcm[222], 7) ||
            pcm[225] != xaHistNext(pcm[223], 5)) {
            fprintf(stderr, "sndtest: xa group reset %d/%d\n",
                    pcm[224], pcm[225]);
            fail("xa history group continuity");
        }
        lastL = pcm[4030];
        lastR = pcm[4031];
        if (sndXaDecodeAudio(xaFrames[2], SND_XA_FRAME_SIZE, pcm,
                             SND_XA_SECTOR_SAMPLES, &st) !=
            SND_XA_SECTOR_SAMPLES) {
            fail("xa history second sector");
        }
        if (pcm[0] != xaHistNext(lastL, 7) ||
            pcm[1] != xaHistNext(lastR, 5) || pcm[0] == 7) {
            fprintf(stderr, "sndtest: xa sector reset %d/%d expected %d/%d\n",
                    pcm[0], pcm[1], xaHistNext(lastL, 7),
                    xaHistNext(lastR, 5));
            fail("xa history sector continuity");
        }

        /* Filters 2 and 3 from a known history (old 100, older 60): left
           (115, -52) gives 132 and right (98, -55) gives 103. */
        sndXaResetState(&st);
        st.channel[0].old = 100;
        st.channel[0].older = 60;
        st.channel[1].old = 100;
        st.channel[1].older = 60;
        if (sndXaDecodeAudio(xaFrames[5], SND_XA_FRAME_SIZE, pcm,
                             SND_XA_SECTOR_SAMPLES, &st) !=
            SND_XA_SECTOR_SAMPLES || pcm[0] != 132 || pcm[1] != 103) {
            fprintf(stderr, "sndtest: xa filters got %d/%d expected 132/103\n",
                    pcm[0], pcm[1]);
            fail("xa filter coefficients");
        }

        /* a truncated sector is refused before a single sample is written */
        sndXaResetState(&st);
        pcm[0] = 0x1234;
        if (sndXaDecodeAudio(xaFrames[1], SND_XA_FRAME_SIZE - 1, pcm,
                             SND_XA_SECTOR_SAMPLES, &st) != -1 ||
            pcm[0] != 0x1234) {
            fail("xa truncated decode refused");
        }
        if (sndXaDecodeAudio(xaFrames[1], SND_XA_FRAME_SIZE, pcm,
                             SND_XA_SECTOR_SAMPLES, NULL) != -1) {
            fail("xa null state refused");
        }
        /* and a frame whose subheader copies disagree is not a sector */
        xaFrames[1][16] = 9;
        if (sndXaClassify(xaFrames[1], SND_XA_FRAME_SIZE, &sec)) {
            xaFrames[1][16] = 1;
            fail("xa mismatched subheader refused");
        }
        xaFrames[1][16] = 1;

        /*
         * The stream driver, on a fake source and a recording sink (no
         * OpenAL). Each sector is one ring item: 2016 stereo frames or 4032
         * mono frames. Video/data and other (file, channel) sectors between
         * audio sectors are skipped, and the predictor keeps its history
         * across the sectors of one stream. A tick delivers 756 whole frames.
         */
        sndXaSetSource(xaFakeSource, NULL);
        sndXaSetSink(&xaFake, NULL);

        /* Run 1: two stereo sectors with a video sector and a foreign mono
           sector between them. Both audio sectors reach the sink, the skipped
           sectors do not, and the chunk schedule follows 756-frame ticks. */
        {
            const unsigned char *rows[XA_ROW_MAX];

            rows[0] = xaFrames[0]; /* video                          */
            rows[1] = xaFrames[1]; /* stereo layout A                */
            rows[2] = xaFrames[0]; /* video between the audio        */
            rows[3] = xaFrames[3]; /* foreign file/channel, skipped  */
            rows[4] = xaFrames[1]; /* stereo layout B                */
            xaFakeUse(rows, 5, XA_ARM_LBA);
            xaFakeReset();
            sndXaArm(XA_ARM_LBA);
            sndXaStart();
            if (!sndXaActive()) {
                fail("xa start");
            }
            for (t = 0; t < 16 && sndXaActive(); t++) {
                sndXaTick();
            }
            if (sndXaActive()) {
                fail("xa stream must end after the last sector");
            }
            /* two sectors = 4032 stereo frames */
            if (xaFramesDelivered != 2L * SND_XA_STEREO_FRAMES) {
                fprintf(stderr, "sndtest: xa frames %ld expected %d\n",
                        xaFramesDelivered, 2 * SND_XA_STEREO_FRAMES);
                fail("xa stereo frames");
            }
            if (xaChannelsSeen != 2) {
                fprintf(stderr, "sndtest: xa channels %d\n", xaChannelsSeen);
                fail("xa stereo routing");
            }
            /* 756, 756, 504 (A), 252, 756, 756, 252 (B) frames */
            if (xaChunkCount != 7 || xaChunks[0].samples != 1512 ||
                xaChunks[1].samples != 1512 || xaChunks[2].samples != 1008 ||
                xaChunks[3].samples != 504 || xaChunks[4].samples != 1512 ||
                xaChunks[5].samples != 1512 || xaChunks[6].samples != 504) {
                fprintf(stderr, "sndtest: xa chunk schedule n %d\n",
                        xaChunkCount);
                fail("xa chunk schedule");
            }
            /* the first frame is the layout's [L=0,R=-8, L=1,R=-7] */
            if (xaChunks[0].channels != 2 || xaChunks[0].first[0] != 0 ||
                xaChunks[0].first[1] != -8 || xaChunks[0].first[2] != 1 ||
                xaChunks[0].first[3] != -7) {
                fprintf(stderr, "sndtest: xa first chunk %d %d %d %d\n",
                        xaChunks[0].first[0], xaChunks[0].first[1],
                        xaChunks[0].first[2], xaChunks[0].first[3]);
                fail("xa layout delivery");
            }
            /* chunk 3 is the second sector's own first frame */
            if (xaChunks[3].channels != 2 || xaChunks[3].first[0] != 0 ||
                xaChunks[3].first[1] != -8) {
                fail("xa second sector delivery");
            }
            if (xaFakeReads != 5 || xaFakeLastLba != XA_ARM_LBA + 5) {
                fprintf(stderr, "sndtest: xa reads %ld last lba %ld\n",
                        xaFakeReads, xaFakeLastLba);
                fail("xa lba walk");
            }
            /* one tick past the end changes nothing */
            i = xaChunkCount;
            sndXaTick();
            if (xaChunkCount != i) {
                fail("xa tick past the end");
            }
        }

        /* Run 2: one stereo sector with the EOF submode ends the stream after
           its 2016 frames (three ticks: 756, 756, 504). */
        {
            const unsigned char *rows[XA_ROW_MAX];

            rows[0] = xaFrames[4]; /* stereo layout, EOF */
            xaFakeUse(rows, 1, XA_ARM_LBA);
            xaFakeReset();
            sndXaArm(XA_ARM_LBA);
            sndXaStart();
            for (t = 0; t < 8 && sndXaActive(); t++) {
                sndXaTick();
            }
            if (sndXaActive()) {
                fail("xa EOF-flagged stream must end");
            }
            if (xaFramesDelivered != (long)SND_XA_STEREO_FRAMES) {
                fprintf(stderr, "sndtest: xa EOF frames %ld\n",
                        xaFramesDelivered);
                fail("xa EOF frames");
            }
            if (xaChunkCount != 3 || xaChunks[2].samples != 1008) {
                fprintf(stderr, "sndtest: xa EOF chunks %d last %d\n",
                        xaChunkCount,
                        xaChunkCount ? xaChunks[xaChunkCount - 1].samples : -1);
                fail("xa EOF schedule");
            }
            if (xaChunks[0].first[0] != 0 || xaChunks[0].first[1] != -8) {
                fail("xa EOF layout");
            }
            if (xaFakeReads != 1 || xaFakeLastLba != XA_ARM_LBA) {
                fprintf(stderr, "sndtest: xa EOF reads %ld last lba %ld\n",
                        xaFakeReads, xaFakeLastLba);
                fail("xa EOF lba");
            }
        }

        /* Run 3: one mono sector is 4032 mono frames, delivered a tick at a
           time. */
        {
            const unsigned char *rows[XA_ROW_MAX];

            rows[0] = xaFrames[3];
            xaFakeUse(rows, 1, XA_ARM_LBA);
            xaFakeReset();
            sndXaArm(XA_ARM_LBA);
            sndXaStart();
            for (t = 0; t < 16 && sndXaActive(); t++) {
                sndXaTick();
            }
            if (sndXaActive()) {
                fail("xa mono stream must end");
            }
            if (xaFramesDelivered != (long)SND_XA_MONO_FRAMES) {
                fprintf(stderr, "sndtest: xa mono frames %ld\n",
                        xaFramesDelivered);
                fail("xa mono frames");
            }
            if (xaChannelsSeen != 1) {
                fail("xa mono channels");
            }
            if (xaChunkCount != 6 || xaChunks[0].samples != 756 ||
                xaChunks[5].samples != 252 || xaChunks[0].channels != 1 ||
                xaChunks[0].first[0] != 0 || xaChunks[0].first[1] != 1 ||
                xaChunks[0].first[2] != 2 || xaChunks[0].first[3] != 3) {
                fprintf(stderr, "sndtest: xa mono chunk n %d first %d %d %d %d "
                        "last %d\n", xaChunkCount,
                        xaChunkCount ? xaChunks[0].first[0] : -1,
                        xaChunkCount ? xaChunks[0].first[1] : -1,
                        xaChunkCount ? xaChunks[0].first[2] : -1,
                        xaChunkCount ? xaChunks[0].first[3] : -1,
                        xaChunkCount ? xaChunks[xaChunkCount - 1].samples : -1);
                fail("xa mono chunk");
            }
            if (xaFakeReads != 1 || xaFakeLastLba != XA_ARM_LBA + 1) {
                fail("xa mono lba");
            }
        }

        /* Run 4: two history sectors in one stream. The second sector's first
           frame continues the first sector's ramp, so the driver did not reset
           the predictor between sectors. */
        {
            const unsigned char *rows[XA_ROW_MAX];

            rows[0] = xaFrames[2];
            rows[1] = xaFrames[2];
            xaFakeUse(rows, 2, XA_ARM_LBA);
            xaFakeReset();
            sndXaArm(XA_ARM_LBA);
            sndXaStart();
            for (t = 0; t < 16 && sndXaActive(); t++) {
                sndXaTick();
            }
            if (sndXaActive()) {
                fail("xa history stream must end");
            }
            if (xaFramesDelivered != 2L * SND_XA_STEREO_FRAMES) {
                fprintf(stderr, "sndtest: xa history frames %ld\n",
                        xaFramesDelivered);
                fail("xa history frames");
            }
            if (xaChunkCount != 7 || xaChunks[2].samples != 1008 ||
                xaChunks[3].samples != 504) {
                fprintf(stderr, "sndtest: xa history chunks n %d\n",
                        xaChunkCount);
                fail("xa history schedule");
            }
            if (xaChunks[0].first[0] != 7 || xaChunks[0].first[1] != 5) {
                fprintf(stderr, "sndtest: xa history start %d/%d\n",
                        xaChunks[0].first[0], xaChunks[0].first[1]);
                fail("xa history stream start");
            }
            /* chunk 2 ends sector 1; chunk 3 starts sector 2 */
            if (xaChunks[3].first[0] != xaHistNext(xaChunks[2].last[2], 7) ||
                xaChunks[3].first[1] != xaHistNext(xaChunks[2].last[3], 5) ||
                xaChunks[3].first[0] == 7 ||
                xaChunks[3].first[1] == 5) {
                fprintf(stderr, "sndtest: xa history across sectors %d/%d "
                        "expected %d/%d\n", xaChunks[3].first[0],
                        xaChunks[3].first[1],
                        xaHistNext(xaChunks[2].last[2], 7),
                        xaHistNext(xaChunks[2].last[3], 5));
                fail("xa history across sectors");
            }
        }

        /* a stop with a stream still armed halts it */
        sndXaStart();
        if (!sndXaActive()) {
            fail("xa restart");
        }
        sndXaStop();
        if (sndXaActive()) {
            fail("xa stop");
        }
    }

    if (!spoken) {
        printf("sndtest: all assertions held (%d vags synthetic, %lu bytes body, "
               "2 SEP packages opened)\n",
               (int)FIX_VAGS, (unsigned long)FIX_BODY);
    }
    return 0;
}
