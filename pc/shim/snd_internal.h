/*
 * Internal API of the port's own SPU backing for libsnd (see shim/host_snd.c
 * and ADR-002). This is not a public header: only host_snd.c, snd_spu.c and
 * pc/sndtest.c include it, and the game's own translation units keep calling
 * the retail libsnd.h prototypes.
 *
 * T34 milestone: the one-shot key-on path (SsVabOpenHeadSticky, SsVabTransBody,
 * SsVabTransCompleted, SsVabClose, SsUtKeyOn/KeyOff/AllKeyOff) is real, backed
 * by PsyCross's OpenAL SPU renderer through LIBSPU; the SEP/sequence side and
 * the reverb unit stay stubbed in host_snd.c (T35).
 */
#ifndef PC_SHIM_SND_INTERNAL_H
#define PC_SHIM_SND_INTERNAL_H

/* The voice pool: PsyX_SPUAL's own 24-voice OpenAL source pool. */
#define SND_VOICE_COUNT 24

/* The parsed VAB header (VH): the parts a key-on needs. One per slot. */
typedef struct SndTone {
    /* 0x00 */ unsigned char vag;    /* 1-indexed VAG reference (0 = unused) */
    /* 0x01 */ unsigned char vol;    /* tone volume 0..127 */
    /* 0x02 */ unsigned char pan;    /* tone panning 0..127, 64 = centre */
    /* 0x03 */ unsigned char center; /* centre note */
    /* 0x04 */ unsigned char shift;  /* centre fine tune, a signed 1/128-semitone value above 127 (see sndParseVab) */
    /* 0x05 */ unsigned char min;    /* note limit minimum */
    /* 0x06 */ unsigned char max;    /* note limit maximum */
    signed char prior;             /* VH byte 0x00: voice priority, signed (retail _SsVmAlloc) */
    /* 0x08 */ unsigned short adsr1; /* drives the software ADSR envelope (see SndEnv) */
    /* 0x0a */ unsigned short adsr2;
    /* 0x0c */ short prog;           /* parent program the record names */
    unsigned char pbDown;          /* VH byte 0x0C: bend range below centre, semitones */
    unsigned char pbUp;            /* VH byte 0x0D: bend range above centre, semitones */
} SndTone;

typedef struct SndVab {
    int used;
    short slot;
    unsigned long spuAddr;  /* the base the game asked for (SOUND_SPU_ADDRS) */
    int progs;              /* stored tone pages = used programs (measured) */
    int vagCount;
    /* A6: ProgAtr bytes 1 (volume) and 4 (pan) per program number; retail
       _SsVmSetVol scales every note of the program by them */
    unsigned char progVol[128];
    unsigned char progPan[128];
    /* O4: the VAB header's master volume (VH byte 0x18), retail's first
       factor after the velocity (libsnd_vm_vol.s:145, `lbu 0x18`) */
    unsigned char mvol;
    /* rank[i] = the tone-page rank of program i, or -1 when program i is unused */
    int rank[128];
    /* per stored page, 16 tone records (the slots of an unused program stay zero) */
    SndTone tone[128][16];
    /* 1-indexed VAG sizes in bytes and body offsets (vagOfs[0] = 0) */
    unsigned long vagSize[256];
    unsigned long vagOfs[256];
    unsigned long bodyBytes;
    int bodyLoaded;
    /* 1-indexed: the VAG's last ADPCM block carries the repeat flag (bit 1),
       so only the SPU envelope ends it (set by sndVabTransBody) */
    unsigned char vagLoops[256];
    /* A6e: PCM samples a VAG plays before its end flag mutes it (0 = loops) */
    unsigned long vagEnd[256];
    /* E3c: where a VAG a mod replaced lives in SPU RAM (0 = the original, at spuAddr + vagOfs); vagSize, vagLoops and vagEnd
       then describe the replacement */
    unsigned long vagAddr[256];
} SndVab;

struct SndNote {
    unsigned long addr;        /* SPU address of the VAG's waveform start */
    unsigned long loopAddr;    /* start of loop (== addr: PsyX_SPUAL loops on the ADPCM's own flags) */
    unsigned short pitch;      /* PsyX_SPUAL's pitch value (0x1000 = 44100 Hz) */
    short volL;
    short volR;
};

/* The VAB backend (snd_spu.c), called by host_snd.c's libsnd surface. */
void sndInit(void);
short sndVabOpen(unsigned char *head, short slot, unsigned long spuAddr);
short sndVabTransBody(unsigned char *body, short vabId);
short sndVabTransCompleted(short immediate);
unsigned long sndVagEndSamples(const unsigned char *vag, unsigned long size);
void sndVabClose(short vabId);
short sndKeyOn(short vabId, short prog, short tone, short note, short fine,
               short volL, short volR);
/* sndKeyOn with the caller named for the log: 0 = SsUtKeyOn, 1 = SEP player
   (music), 2 = SEP player (a sequence that is a sound effect, see
   sndSepSetMusicQuery) */
short sndKeyOnSrc(short vabId, short prog, short tone, short note, short fine,
                  short volL, short volR, int src);
short sndKeyOff(short voice, short vabId, short prog, short tune, short note);
void sndAllKeyOff(short mode);

/*
 * O4: the player's own volume sliders, applied at the end of the chain (the
 * gain handed to the OpenAL voice), on top of the game's volumes and never
 * instead of them. A voice is music when a SEP sequence played it (src 1),
 * a sound effect when SsUtKeyOn did (src 0) or a SEP sequence that is not
 * the game's music (src 2); the movie class is the XA stream (host_xa.c).
 * Percent 0..100; the taper is quadratic (50 % is a quarter of the amplitude,
 * -12 dB), 0 is silence, 100 leaves the retail level untouched.
 */
enum { SND_VOL_MASTER = 0, SND_VOL_MUSIC, SND_VOL_EFFECTS, SND_VOL_MOVIE, SND_VOL_COUNT };
int sndVolTaperPermille(int percent);            /* 0 .. 1000 */
void sndSetUserVolume(int which, int percent);   /* clamps, re-applies live voices */
int sndUserVolume(int which);                    /* the percent last set */
int sndUserGainPermille(int which);              /* master * class taper, 0 .. 1000 */
short sndScaleVol(short vol, int permille);      /* vol * permille / 1000 */
unsigned sndVolGeneration(void);                 /* changes with every sndSetUserVolume */
int sndSrcIsMusic(int src);                      /* 1 when the key-on source is music */
/* The volumes a live voice is sent to the mixer with (the envelope and the
   user gain applied); 0 when the voice is not live */
int sndVoiceOutVol(short voice, short *left, short *right);
/* 1 = music, 0 = effect, -1 = not live */
int sndVoiceIsMusic(short voice);

/*
 * A4: the PS1 SPU ADSR envelope, in software (psx-spx "SPU ADSR Envelope").
 * PsyX_SPUAL has no envelope, so a key-on'd voice would sound until a key-off
 * and a looping VAG would never die. One SndEnv per voice runs at the SPU's
 * 44100 Hz sample clock; snd_spu.c advances it from the 50 Hz sound tick
 * (882 samples a tick) and scales the voice's gain by level / 0x7FFF.
 *   adsr1: 15 attack mode (1 = exponential), 14-10 attack shift, 9-8 attack
 *          step (+7..+4), 7-4 decay shift, 3-0 sustain level ((n + 1) * 0x800)
 *   adsr2: 15 sustain mode (1 = exp), 14 sustain direction (1 = decrease),
 *          12-8 sustain shift, 7-6 sustain step, 5 release mode (1 = exp),
 *          4-0 release shift
 */
#define SND_ENV_ATTACK 0
#define SND_ENV_DECAY 1
#define SND_ENV_SUSTAIN 2
#define SND_ENV_RELEASE 3
#define SND_ENV_DONE 4
#define SND_ENV_MAX 0x7FFF
#define SND_ENV_SAMPLE_RATE 44100
#define SND_ENV_TICK_SAMPLES (SND_ENV_SAMPLE_RATE / 50)

typedef struct SndEnv {
    unsigned short adsr1, adsr2;
    int phase;   /* SND_ENV_* */
    int level;   /* 0..SND_ENV_MAX */
    int counter; /* samples since the last step */
} SndEnv;

void sndEnvInit(SndEnv *env, unsigned short adsr1, unsigned short adsr2);
void sndEnvKeyOff(SndEnv *env);        /* switch to release (no-op once done) */
int sndEnvStep(SndEnv *env);           /* advance one sample, returns the level */

/* Voice-level view, for pc/sndtest.c and the sound tick. sndEnvAdvance moves
   every live voice's envelope by `samples`, scales the gains, and stops and
   frees the voices whose envelope ended. sndEnvTick = one tick at sndTickHz(). */
void sndEnvAdvance(unsigned long samples);
void sndEnvTick(void);
int sndEnvVoiceLive(short voice);
int sndEnvVoiceLevel(short voice);
/* A5d: the key-on serial of a live voice (changes on every re-allocation),
   0 when the voice is free or out of range. */
unsigned long sndVoiceSerial(short voice);

/* The header parser, exposed for pc/sndtest.c. Fills *out and returns 0, or
   -1 with nothing written on a header the DW3 VAB layout does not match. */
int sndParseVab(const unsigned char *vh, unsigned long len, SndVab *out);

/* The key-on math without touching a voice: resolves (vab, prog, tone, note)
   to the SPU address and the PsyX pitch/volume the backend would use. Exposed
   for pc/sndtest.c; returns 0 and fills *out, or -1 when the record cannot be
   resolved. */
int sndNotePrepare(short vabId, short prog, short tone, short note, short fine,
                   short volL, short volR, struct SndNote *out);

/* The tone a SEP key-on plays: the first tone record of the program whose
   note range covers the key-on note, or -1 when none does (the VAB registry
   and its tone pages are snd_spu.c's own -- no second copy of them here). */
short sndPickTone(short vabId, short prog, short note);

/* Every tone of the program covering the note (retail _SsVmSelectToneAndVag
   keeps them all and keys one voice per tone). Fills tones[] in page order up
   to cap and returns the count. */
int sndPickTones(short vabId, short prog, short note, short *tones, int cap);

/* Pitch bend, as retail _SsVmPBVoice (libsnd_vm_pb.s) does it. The bend is
   the MSB data byte only (the LSB is skipped), 0x40 the centre. Above it:
   note += t * pbUp / 63, fine = rem * 2; below it: note += t * pbDown / 64 - 1,
   fine = rem * 2 + 127 (t = msb - 0x40, C division). pbUp/pbDown are the
   tone's VH bytes 0x0D / 0x0C. */
void sndBendNote(unsigned char msb, unsigned char pbUp, unsigned char pbDown,
                 short note, short *outNote, short *outFine);

/* The PsyX pitch of a (vab, prog, tone) at `note` bent by msb, using the
   tone's own centre/shift/bend range. Returns 0 and fills *pitch, or -1. */
int sndBendPitch(short vabId, short prog, short tone, short note,
                 unsigned char msb, unsigned short *pitch);

/* Re-pitches a voice sndKeyOn started (retail: only voices already ringing
   are bent; later key-ons start unbent, note2pitch with fine 0). */
void sndVoiceBend(short voice, unsigned char msb);

/* The most tones one key-on can layer: a program page holds 16 tone records. */
#define SND_LAYERS_MAX 16

/*
 * T35: the SEP (sequence package) side, real as well. A SEP file is the
 * data the game's own loader hands to SsSepOpen from the bank's head
 * archive ('pQES', a big-endian version word, then sub-sequence records;
 * the layout verified against MPCOMMON.BIN / MPTTLBGM.BIN / MPBGM026.BIN):
 *
 *     record: u16 BE seq id, u16 BE resolution, u24 BE tempo (us/quarter),
 *             u8 num, u8 den, u32 BE event span, then `size` MIDI-like
 *             bytes (running status across everything, VLQ deltas; FF 51
 *             carries a 3-byte big-endian tempo with NO length byte; FF 2F
 *             ends; any other meta ends the record's decodable span).
 *     loops are the CC99 = 20 / CC6 markers: CC99 = 20 opens the loop
 *     body, the CC6 right after it counts (0x7F = forever), CC99 = 30
 *     closes it, and the body always runs to the record's end.
 */

/* One decoded event. kind says which fields speak. */

/* The event array of one package, fixed at parse: the biggest package the
   discs carry decodes to 6484 events (MPBGM026.BIN), TTL's to 4726. */
/* T35 simplification: a package bigger than the cap keeps its parsed
   prefix and the records behind the cap are dropped. */
#define SND_SEP_EVENT_CAP 16384

#define SND_EV_KEYON 0
#define SND_EV_KEYOFF 1
#define SND_EV_PROG 2
#define SND_EV_CTRL 3
#define SND_EV_TEMPO 4
#define SND_EV_END 5
#define SND_EV_BEND 6

typedef struct SndEv {
    unsigned long tick;  /* musical tick it fires at (from the record's own clock) */
    unsigned long datum; /* TEMPO: us per quarter */
    unsigned char kind;
    unsigned char ch;
    unsigned char note;  /* key-on/off: note; CTRL: controller number */
    unsigned char data;  /* keyon/off: velocity; CTRL: value; PROG: program; BEND: the MSB */
} SndEv;

/* One sub-sequence record of the package. */
typedef struct SndSepRec {
    unsigned short id;       /* the record's own seq id (the game names records by position, not by it) */
    unsigned short res;      /* resolution in ticks per quarter */
    unsigned long tempo;     /* the header's tempo until an FF 51 retunes the clock */
    unsigned char num, den;  /* the header's time signature: den is the power of two (2 = quarters), as MIDI's FF 58 */
    unsigned long first;     /* the record's span inside the package's event array */
    unsigned long count;
    long loopBody;           /* absolute event index to resume at, -1 without markers */
    unsigned long loopStartTick, loopEndTick;
    unsigned short loopCount; /* the CC6 count; 0x7F = forever */
} SndSepRec;

/* One opened SEP package. pc/sndtest.c parses its own; the registry below
   keeps its own copies once SsSepOpen runs. */
typedef struct SndSep {
    short vabId;
    int recCount;
    /* The discs' packages carry far more records than the eight the menu
       blips' ids land past (SOUND_MENU_OPEN 0x40019 is sep 25, SOUND_MENU_CLOSE
       0x4001A sep 26, up to 0x6004001E sep 30): the array was capped at 16
       while the package registry's cap (SND_SEP_MAX, 8) is a different,
       unrelated count. Sized generously against the measured max. */
    SndSepRec recs[64];
    SndEv *evs; /* one malloc'd array for the whole package (SND_SEP_EVENT_CAP events) */
} SndSep;

/* The open packages, seen from inside host_snd.c and pc/sndtest.c.
   SND_SEP_MAX is the PACKAGE registry size (SndSep slots), not a record
   count: parsing must bound each package's records by its own array size. */
#define SND_SEP_MAX 8
#define SND_SEP_RECS 64

int sndParseSep(const unsigned char *sep, unsigned long len, SndSep *out);
void sndSepFree(SndSep *sep);

/* The player. host_snd.c calls it through the SsSep* and SsSeqCalledTbyT
   surface; the engine-side event delivery goes through this sink, so
   pc/sndtest.c can record the events instead of really keying voices on. */
typedef struct SndEvSink {
    /* A key-on: the sink picks the tones inside (one voice per tone covering
       the note, as retail's _SsVmKeyOn does), writes the booked voices to
       voices[0..SND_LAYERS_MAX-1] and returns how many (0 when refused). */
    int (*keyOn)(void *ctx, short vabId, short prog, short note,
                 short volL, short volR, short *voices);
    /* A key-off of a voice the player booked before. */
    void (*keyOff)(void *ctx, short voice);
    /* A pitch bend (0xEn) for a voice still ringing: msb is the one data
       byte retail reads (0x40 = centre). May be NULL (a sink without bend). */
    void (*bend)(void *ctx, short voice, unsigned char msb);
} SndEvSink;

void sndSepSetSink(const SndEvSink *sink, void *ctx);
/* O4: tells music from sound effects among the SEP sequences. Asked once per
   SsSepPlay(seqId, track); non-zero = music (the default when unset). The
   game starts its music as an exclusive sound (engine/sound.h, playSound);
   host_pad.c installs the query that reads that state. */
void sndSepSetMusicQuery(int (*isMusic)(short seqId, short track));
void sndSepInit(void);
short sndSepOpen(unsigned char *sep, short vabId);
void sndSepClose(short sepId);
short sndSepPlayStart(short sepId, short rec, short playMode, short loopCount,
                      short volL, short volR);
void sndSepPlayStop(short sepId, short rec);
void sndSepPlaySetVol(short sepId, short rec, short volL, short volR);
void sndSepPlayDecrescendo(short sepId, short rec, short vol, long frames);
void sndSepTbyT(void);

/* O7: the sequencer's ticks a second. The tick hook runs once per vblank, so
   the rate must be the vblank rate the game chose: SsSetTickMode(SS_NOTICK)
   in the NTSC (60 Hz) path, SS_NOTICK | 50 in the PAL one. The default is 50
   (the first port's fixed rate, what pc/sndtest.c's fixtures assume).
   sndTickHzFromMode mirrors libsnd_sstick.s: the low 12 bits 0..3 pick
   60/50/120/240, a larger value is the rate itself. sndSepStep256 is the
   musical tick advance per call in 1/256 ticks: res * 1e6 * 256 / tempo / hz. */
int sndTickHzFromMode(long tickMode);
void sndSetTickHz(int hz);
int sndTickHz(void);
unsigned long sndSepStep256(unsigned long res, unsigned long tempo, int hz);
int sndEnvTickSamples(void); /* SPU samples per tick at the current rate */
void sndSetMasterVol(short left, short right);

#endif /* PC_SHIM_SND_INTERNAL_H */
