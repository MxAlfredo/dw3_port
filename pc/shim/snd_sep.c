/*
 * The SEP (sequence package) parser and player, behind the SsSep* surface in
 * shim/host_snd.c (ADR-002: PsyCross provides libspu but no libsnd, so the
 * game's own SsSep calls land on this port's own code).
 *
 * What the title, the commons and the field banks hand over is a 'pQES'
 * package: a big-endian version word, then sub-sequence records, each a
 * 13-byte big-endian header and a MIDI-like event stream. The layout is the
 * one the discs' own MPTTLBGM.BIN, MPCOMMON.BIN and MPBGM026.BIN carry
 * (measured with a one-off decoder over the disc; nothing retail lives
 * here):
 *
 *     'pQES'  u16 BE version       the package's head
 *     rec:    u16 BE id            the record's own seq id
 *             u16 BE resolution    ticks per quarter (480 measured)
 *             u24 BE tempo         us per quarter until an FF 51 says other
 *             u8 num, u8 den       the time signature (never read further)
 *             u32 BE span          the event stream's byte count
 *             span bytes           the stream itself:
 *               running status holds across everything, deltas are VLQ;
 *               0x9n with velocity 0 keys off (the discs never send 0x8n),
 *               0x9n keys on, 0xCn names the program, the CCs move the
 *               channel's volume/pan/expression, FF 51 carries a 3-byte
 *               tempo with NO length byte, FF 2F ends, and any other meta
 *               ends the record's decodable span.
 *     loops:  CC99 = 0x14 opens the loop body, the CC6 right after it
 *               counts it (0x7F = forever), CC99 = 0x1E closes it and the
 *               body always runs on to the record's end (measured in all
 *               three packages the game plays).
 *
 * The player keeps the position in musical ticks (a 1/256-tick fixed point)
 * and advances it once per sndSepTbyT call -- the game's own
 * SsSetTickMode(SS_NOTICK | 50) makes the EU build tick once a PAL frame
 * (display.c:37) -- by res * 1e6 / tempo / sndTickHz() ticks, so an FF 51 tempo
 * change retunes the clock rate without moving the music's place. Events
 * fire their kind through the SndEvSink: host_snd.c installs the real one
 * (snd_spu.c's voices through sndPickTone), pc/sndtest.c records them.
 *
 * T35 simplifications, on purpose, so the milestone lands instead of
 * stalling:
 *   - (O7: the engine's tick rate follows SsSetTickMode -- 60 a second for
 *     SS_NOTICK, 50 for the PAL build's SS_NOTICK | 50 -- through sndSetTickHz.)
 *   - the package volume (SsSepSetVol, SsSetMVol) and the channel state
 *     (the program, CC7/CC10/CC11) apply when a note books, never to the
 *     voices already ringing: the game only ever raises or fades between
 *     plays, and PsyX_SPUAL has no per-voice volume track yet.
 *   - the loop jump happens when the record's span is over -- in every
 *     measured package the loop's CC99 = 0x1E sits on the record's last
 *     tick -- frame-aligned: the frame's overshoot past the loop's end carries
 *     into the body, and the body's events it covers play in that same frame.
 *   - a finite CC6 count is unmeasured (the discs carry 0x7F); it is
 *     treated as n whole jumps of the body, and SsSepPlay's l_count as n
 *     whole passes of the record (the game always passes 1).
 *   - the other CCs and the NRPN config the discs ride (CC99 = 16, CC98)
 *     are dropped at play time.
 *   - 8 open packages; plays are per (package, record) like retail's
 *     (access number, track) pairs (_SsSndStop / Snd_SetPlayMode work on one
 *     track only), so different records of one package sound together; a
 *     fixed pool of SND_PLAY_MAX plays bounds the memory and a play past it
 *     is refused with a log line. Replaying a record that is already
 *     playing restarts it (retail rewinds the track, libsnd_playmode.s).
 *   - a key-on starts one voice per tone covering the note (retail's
 *     _SsVmSelectToneAndVag + _SsVmKeyOn), and the key-off releases them all.
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "audio_mod.h"
#include "snd_internal.h"

/* The package scan's bound when the caller cannot pass a length: the bank's
   head buffer the game copies the archive into is at most 57344 bytes
   (SOUND_HEAD_BUFFER_0). The record chain itself decides the end; the bound
   only stops a corrupt span from running off. */
#define SND_SEP_SCAN_MAX 57344u

/* The channel a note plays on: the program the last 0xCn named and the
   CC7/CC10/CC11 levels the later key-ons read. */
typedef struct SndChState {
    unsigned char prog;
    unsigned char vol;   /* CC7, 0..127 */
    unsigned char pan;   /* CC10, 64 = centre */
    unsigned char expr;  /* CC11 */
} SndChState;

/* The plays that can run at once, across all packages. Retail's track table
   is per package (16 tracks x 8 accesses); 32 is far past what the game
   keeps going (a music track plus a handful of blips). */
#define SND_PLAY_MAX 32

/* The voices one play holds at once. The SPU has SND_VOICE_COUNT (24) voices
   in all, so a play can never usefully hold more; an entry past the bound is
   keyed off at once and logged. */
#define SND_HELD_MAX 64

/* One playing (package, record). A slot is free while !playing; its fade
   with the master volume are the fields acc/fadeStep. */
typedef struct SndPlay {
    char playing;
    char music;                /* O4: 1 = the game's music, 0 = a sound effect sequence */
    int mod;                   /* E3d: the handle of the mod's WAV playing in place of this sequence, 0 = the sequence itself */
    SndSep *sep;
    int rec;
    unsigned long pos256;      /* the position, in 1/256 ticks */
    unsigned long tempo;       /* us per quarter, retuned by the FF 51s */
    unsigned long nextEv;      /* absolute event index of the next delivery */
    short passesLeft;          /* SsSepPlay's l_count, whole passes; 0 forever */
    short jumpsLeft;           /* the record's CC6 count; -1 forever */
    short volL, volR;          /* the package volume, fed by SetVol */
    char fading;
    unsigned long accL, accR;  /* the fading volume, 1/256 fixed point */
    unsigned long fadeStep;    /* the 1/256 points each frame takes away */
    unsigned long noteLog;     /* the note log's stride */
    SndChState ch[16];
    /* the voices ringing, (channel, note) -> voice; a layered key-on holds
       several entries for one (channel, note) */
    struct {
        unsigned char ch, note;
        short voice;
        unsigned long serial; /* the voice's key-on serial (0 = unknown) */
    } held[SND_HELD_MAX];
    short heldCount;
} SndPlay;

/* The registry: one SndSep per SsSepOpen, ids 1-based like the VAB ids. */
static SndSep sndSeps[SND_SEP_MAX];

/* The plays: a pool, each slot tagged with its package and record. */
static SndPlay sndPlays[SND_PLAY_MAX];

/* The master volume, for the SEP side only: the game sets it once at boot
   (SOUND.init's SsSetMVol(127, 127)); a test that never calls it hears full. */
static short sndMasterL = 127, sndMasterR = 127;

/* The delivery sink: NULL until sndSepInit installs the real one, or until
   pc/sndtest.c beats it to it with its recording sink. */
static const SndEvSink *sndSink;
static void *sndSinkCtx;

/* O4: asked at every SsSepPlay whether the sequence is the game's music; the
   class of the play it starts. NULL = every sequence is music. */
static int (*sndMusicQuery)(short seqId, short track);
/* the class of the play whose event is being delivered (the engine sink reads
   it: the SndEvSink signature carries no play) */
static int sndDeliverMusic = 1;

static char sndInited;
static char sndDebugSet;

/* The sequencer's own lock. sndSepTbyT runs on PsyX's psyX_intr thread (the
   VSyncCallback(sndSepVsyncHook) of host_snd.c, external/psycross/src/
   PsyX_main.cpp), while SsSepOpen/Close/Play/Stop/SetVol/SetDecrescendo and
   SsSetMVol come from the game thread; both touch sndSeps, sndPlays and the
   master volume, so every public entry below takes it. Lock order is always
   this one first, then snd_spu.c's sndVoiceLock (the sink's keyOn/keyOff
   take it); snd_spu never calls into this file, so the order cannot invert.
   The lock is not recursive: a public entry never calls another one, the
   shared bodies live in the sepXxxLocked helpers. The pure parsers
   (sndParseSep, sndSepFree on a caller's own SndSep) stay lock-free. */
static pthread_mutex_t sndSepLock = PTHREAD_MUTEX_INITIALIZER;

/* --- debug log ----------------------------------------------------------- */

/* The SPU backend's SND_LOG lives in snd_spu.c's own statics; the SEP side
   grows its own, of the same shape (snd: ... to stderr while the SND_DEBUG
   env is set). */
#define SND_LOG(...)                                                           \
    do {                                                                       \
        if (sndDebugSet) {                                                     \
            fputs("snd: ", stderr);                                            \
            fprintf(stderr, __VA_ARGS__);                                      \
        }                                                                      \
    } while (0)

/* --- the big-endian reads the package's own layout is stored with --------- */

static unsigned long sepLoad16(const unsigned char *at) {
    return (unsigned long)at[0] << 8 | at[1];
}

static unsigned long sepLoad24(const unsigned char *at) {
    return (unsigned long)at[0] << 16 | ((unsigned long)at[1] << 8) | at[2];
}

static unsigned long sepLoad32(const unsigned char *at) {
    return (unsigned long)at[0] << 24 | ((unsigned long)at[1] << 16) |
           ((unsigned long)at[2] << 8) | at[3];
}

/* A continuation record's header that keeps the chain: real resolutions sit
   well inside this band, so a second SEP package beginning right after this
   one in the head buffer (the commons' archive holds two) stops the chain
   instead of parsing as a record -- 'pQES' reads a nonsense resolution. */
static int sepChainHeader(const unsigned char *rec) {
    unsigned long res = sepLoad16(rec + 2);
    unsigned long span = sepLoad32(rec + 9);

    return res >= 240 && res <= 1920 && span <= SND_SEP_SCAN_MAX;
}

/* --- the parser (exposed for pc/sndtest.c as well) ------------------------ */

int sndParseSep(const unsigned char *sep, unsigned long len, SndSep *out) {
    SndEv *evs;
    unsigned long at, evCount = 0;
    int recs = 0;

    if (sep == NULL || out == NULL || len < 6 || sep[0] != 'p' ||
        sep[1] != 0x51 || sep[2] != 'E' || sep[3] != 'S') {
        return -1;
    }

    evs = malloc(SND_SEP_EVENT_CAP * sizeof(SndEv));
    if (evs == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->evs = evs;

    SND_LOG("sep parse hdr: %02x %02x %02x %02x %02x %02x\n",
            sep[0], sep[1], sep[2], sep[3], sep[4], sep[5]);

    for (at = 6; at + 13 <= len && recs < SND_SEP_RECS; ) {
        unsigned long first = evCount;
        unsigned long span, dataAt, dataEnd, tick = 0, endTick = 0;
        unsigned long loopStartTick = 0, loopEndTick = 0;
        long loopBody = -1;
        unsigned short loopCount = 0;
        unsigned char status = 0;
        char sawStart = 0, sawEnd = 0, onLoopStart = 0, markNext = 0;

        if (recs > 0 && !sepChainHeader(sep + at)) {
            SND_LOG("sep chain stop after rec %d: hdr=%02x%02x%02x%02x "
                    "res=%lu span=%lu left=%lu\n",
                    recs, sep[at], sep[at + 1], sep[at + 2], sep[at + 3],
                    sepLoad16(sep + at + 2), sepLoad32(sep + at + 9), len - at);
            {
                unsigned long d, n = (len - at < 20) ? (len - at) : 20;
                for (d = 0; d < n; d++) {
                    SND_LOG("sep chain stop byte[%lu]=%02x\n", d, sep[at + d]);
                }
            }
            break;
        }
        span = sepLoad32(sep + at + 9);
        SND_LOG("sep rec %d hdr: %02x %02x res=%lu %02x %02x %02x %02x %02x"
                " %02x span=%lu\n",
                recs, sep[at], sep[at + 1], sepLoad16(sep + at + 2),
                sep[at + 4], sep[at + 5], sep[at + 6], sep[at + 7],
                sep[at + 8], sep[at + 12], span);
        if (span > SND_SEP_SCAN_MAX || at + 13 + span > len) {
            goto fail;
        }
        dataAt = at + 13;
        dataEnd = dataAt + span;

        at = dataAt;
        while (at < dataEnd && evCount < (unsigned long)SND_SEP_EVENT_CAP) {
            unsigned long delta = 0;
            unsigned char kind, b;
            char running;
            int nbytes = 0;

            /* the delta, or none: real records stay far under 4 bytes of it */
            while (nbytes < 4) {
                b = sep[at++];
                delta = delta << 7 | (b & 0x7F);
                nbytes++;
                if (!(b & 0x80)) {
                    break;
                }
            }
            tick += delta;
            if (at >= dataEnd) {
                break;
            }

            b = sep[at++];
            if (b < 0x80) {
                /* a data byte out of the running status the last status
                   latched: b itself is that event's first data byte */
                kind = status;
                if (kind == 0) {
                    break;
                }
                running = 1;
            } else {
                status = kind = b;
                onLoopStart = 0;
                running = 0;
            }

            if (kind == 0xFF) {
                unsigned char meta;

                if (at >= dataEnd) {
                    break;
                }
                meta = sep[at++];
                if (meta == 0x51) {
                    /* FF 51 carries the tempo in 3 bytes, no length byte */
                    SndEv *ev;

                    if (at + 3 > dataEnd) {
                        break;
                    }
                    ev = evs + evCount++;
                    ev->kind = SND_EV_TEMPO;
                    ev->tick = tick;
                    ev->datum = sepLoad24(sep + at);
                    ev->ch = ev->note = ev->data = 0;
                    at += 3;
                    endTick = tick;
                } else if (meta == 0x2F) {
                    SndEv *ev = evs + evCount++;

                    ev->kind = SND_EV_END;
                    ev->tick = tick;
                    endTick = tick;
                    break;
                } else {
                    /* T35 simplification: the discs' other metas carry no
                       length this parser can rely on, so the record's
                       decodable span ends here (the one-off decoder's
                       behaviour). */
                    break;
                }
            } else if (kind >= 0xF0) {
                /* T35 simplification: sysex and the realtime statuses are
                   unmeasured; the record's decodable span ends here. */
                break;
            } else {
                unsigned char ch = (unsigned char)(kind & 0x0F);
                unsigned char hi = (unsigned char)(kind >> 4);
                char stored = 1;    /* a touch / a pressure drop out */

                if (hi == 0x9) {
                    unsigned char note, vel;
                    SndEv *ev;

                    if (at + (running ? 1 : 2) > dataEnd) {
                        break;
                    }
                    note = running ? b : sep[at++];
                    vel = sep[at++];
                    ev = evs + evCount++;
                    if (vel != 0) {
                        ev->kind = SND_EV_KEYON;
                        ev->data = vel;
                    } else {
                        ev->kind = SND_EV_KEYOFF;
                        ev->data = 0;
                    }
                    ev->ch = ch;
                    ev->note = note;
                    ev->tick = tick;
                    ev->datum = 0;
                    endTick = tick;
                } else if (hi == 0x8) {
                    unsigned char note;
                    SndEv *ev;

                    if (at + (running ? 0 : 2) > dataEnd) {
                        break;
                    }
                    note = running ? b : sep[at++];
                    if (!running) {
                        at++;   /* 0x8n still carries its own velocity byte */
                    }
                    ev = evs + evCount++;
                    ev->kind = SND_EV_KEYOFF;
                    ev->ch = ch;
                    ev->note = note;
                    ev->tick = tick;
                    ev->datum = 0;
                    endTick = tick;
                } else if (hi == 0xC) {
                    SndEv *ev;

                    if (at + (running ? 0 : 1) > dataEnd) {
                        break;
                    }
                    ev = evs + evCount++;
                    ev->kind = SND_EV_PROG;
                    ev->ch = ch;
                    ev->data = running ? b : sep[at++];
                    ev->tick = tick;
                    ev->datum = 0;
                    endTick = tick;
                } else if (hi == 0xB) {
                    unsigned char cc, val;
                    SndEv *ev;

                    if (at + (running ? 1 : 2) > dataEnd) {
                        break;
                    }
                    cc = running ? b : sep[at++];
                    val = sep[at++];

                    /* The loop markers are the package's own convention, not
                       events: CC99 = 0x14 opens the body, the CC6 right
                       after it counts it, CC99 = 0x1E closes it. They stay
                       out of the event array; any other cc (the discs' NRPN
                       config with CC99 = 16, CC98, ...) is an event the
                       player drops. */
                    if (cc == 0x63) {
                        if (val == 0x14) {
                            loopStartTick = tick;
                            sawStart = 1;
                            markNext = 1;   /* the body is what comes next */
                            onLoopStart = 1;
                            continue;
                        } else if (val == 0x1E) {
                            loopEndTick = tick;
                            sawEnd = 1;
                            onLoopStart = 0;
                            continue;
                        }
                        onLoopStart = 0;
                    } else if (cc == 0x06 && onLoopStart) {
                        /* The count as the disc carries it: 0x7F is
                           forever and 0 means as much (the play's own
                           business, see sndSepTbyT). */
                        loopCount = val;
                        onLoopStart = 0;
                        continue;
                    }
                    ev = evs + evCount++;
                    ev->kind = SND_EV_CTRL;
                    ev->ch = ch;
                    ev->note = cc;
                    ev->data = val;
                    ev->tick = tick;
                    ev->datum = 0;
                    endTick = tick;
                } else if (hi == 0xE) {
                    SndEv *ev;

                    if (at + (running ? 1 : 2) > dataEnd) {
                        break;
                    }
                    /* a bend: the LSB is skipped, the MSB is the value
                       (retail _SsGetSeqData / _SsSetPitchBend) */
                    if (!running) {
                        at++;
                    }
                    ev = evs + evCount++;
                    ev->kind = SND_EV_BEND;
                    ev->ch = ch;
                    ev->note = 0;
                    ev->data = sep[at++];
                    ev->tick = tick;
                    ev->datum = 0;
                    endTick = tick;
                } else if (hi == 0xA) {
                    if (at + (running ? 1 : 2) > dataEnd) {
                        break;
                    }
                    at += running ? 1 : 2;  /* a touch: dropped */
                    stored = 0;
                } else if (hi == 0xD) {
                    if (at + (running ? 0 : 1) > dataEnd) {
                        break;
                    }
                    if (!running) {
                        at += 1;            /* a channel pressure: dropped */
                    }
                    stored = 0;
                } else {
                    break;
                }
                if (stored && markNext) {
                    /* the event just stored is the loop body's first one */
                    loopBody = evCount - 1;
                    markNext = 0;
                }
                onLoopStart = 0;
            }
        }

        if (evCount > first) {
            SndSepRec *rec = out->recs + recs;

            rec->id = (unsigned short)sepLoad16(sep + dataAt - 13);
            rec->res = (unsigned short)sepLoad16(sep + dataAt - 11);
            rec->tempo = sepLoad24(sep + dataAt - 9);
            rec->num = sep[dataAt - 6];
            rec->den = sep[dataAt - 5];
            rec->first = first;
            rec->count = evCount - first;
            if (sawStart && sawEnd && loopBody >= 0) {
                rec->loopBody = loopBody;
                rec->loopStartTick = loopStartTick;
                rec->loopEndTick = loopEndTick;
                rec->loopCount = loopCount;
            } else {
                /* no loop: the record's own last tick is where a play stops */
                rec->loopBody = -1;
                rec->loopEndTick = endTick;
                rec->loopCount = 0;
            }
            recs++;
            out->recCount = recs;
        }
        at = dataEnd;
    }

    if (out->recCount == 0) {
        goto fail;
    }
    SND_LOG("sep parse: %d record(s), %lu event(s)\n", out->recCount, evCount);
    if (sndDebugSet) {
        int r;

        for (r = 0; r < out->recCount; r++) {
            const SndSepRec *rc = out->recs + r;
            unsigned long k, ons = 0, bends = 0, tempos = 0;
            unsigned long cc[128];
            unsigned long chOn[16];
            int c;
            char buf[600];
            size_t n = 0;

            memset(cc, 0, sizeof(cc));
            memset(chOn, 0, sizeof(chOn));
            for (k = 0; k < rc->count; k++) {
                const SndEv *e = out->evs + rc->first + k;

                if (e->kind == SND_EV_KEYON) {
                    ons++;
                    chOn[e->ch & 15]++;
                } else if (e->kind == SND_EV_BEND) {
                    bends++;
                } else if (e->kind == SND_EV_TEMPO) {
                    tempos++;
                } else if (e->kind == SND_EV_CTRL) {
                    cc[e->note & 127]++;
                }
            }
            buf[0] = 0;
            for (c = 0; c < 128 && n < sizeof(buf) - 24; c++) {
                if (cc[c] != 0) {
                    n += (size_t)snprintf(buf + n, sizeof(buf) - n, " cc%d=%lu",
                                          c, cc[c]);
                }
            }
            for (c = 0; c < 16 && n < sizeof(buf) - 24; c++) {
                if (chOn[c] != 0) {
                    n += (size_t)snprintf(buf + n, sizeof(buf) - n, " ch%d:%lu",
                                          c, chOn[c]);
                }
            }
            SND_LOG("sep rec %d stats: res=%u tempo=%lu keyons=%lu bends=%lu "
                    "tempoEv=%lu loop=%d |%s\n", r, (unsigned)rc->res,
                    (unsigned long)rc->tempo, ons, bends, tempos,
                    rc->loopBody, buf);
        }
    }
    return 0;

fail:
    memset(out, 0, sizeof(*out));
    free(evs);
    return -1;
}

void sndSepFree(SndSep *sep) {
    if (sep == NULL) {
        return;
    }
    free(sep->evs);
    sep->evs = NULL;
    sep->recCount = 0;
    memset(sep->recs, 0, sizeof(sep->recs));
}

/* --- the registry --------------------------------------------------------- */

static SndSep *sndSepOf(short sepId) {
    if (sepId < 1 || sepId > SND_SEP_MAX) {
        return NULL;
    }
    return sndSeps + sepId - 1;
}

/* The running play of (package, record), or NULL. */
static SndPlay *sndPlayFind(const SndSep *pkg, int rec) {
    int i;

    for (i = 0; i < SND_PLAY_MAX; i++) {
        if (sndPlays[i].playing && sndPlays[i].sep == pkg &&
            sndPlays[i].rec == rec) {
            return sndPlays + i;
        }
    }
    return NULL;
}

/* A free slot, or NULL when the pool is full. */
static SndPlay *sndPlayAlloc(void) {
    int i;

    for (i = 0; i < SND_PLAY_MAX; i++) {
        if (!sndPlays[i].playing) {
            return sndPlays + i;
        }
    }
    return NULL;
}

/* The engine-side sink, installed when nothing else did: the tone a SEP
   key-on plays is picked out of the VAB's own pages by note range. */
static int sepEngineKeyOn(void *ctx, short vabId, short prog, short note,
                          short volL, short volR, short *voices) {
    short tones[SND_LAYERS_MAX];
    int n, i, booked = 0;
    short voice;

    (void)ctx;
    n = sndPickTones(vabId, prog, note, tones, SND_LAYERS_MAX);
    for (i = 0; i < n; i++) {
        voice = sndKeyOnSrc(vabId, prog, tones[i], note, 0, volL, volR,
                            sndDeliverMusic ? 1 : 2);
        if (voice >= 0) {
            voices[booked++] = voice;
        }
    }
    if (n > 1) {
        SND_LOG("sep layers vab=%d prog=%d note=%d: %d tone(s), %d voice(s)\n",
                (int)vabId, (int)prog, (int)note, n, booked);
    }
    return booked;
}

static void sepEngineKeyOff(void *ctx, short voice) {
    (void)ctx;
    sndKeyOff(voice, 0, 0, 0, 0);
}

static void sepEngineBend(void *ctx, short voice, unsigned char msb) {
    (void)ctx;
    sndVoiceBend(voice, msb);
}

static const SndEvSink sndEngineSink = { sepEngineKeyOn, sepEngineKeyOff,
                                         sepEngineBend };

static void sndSepInitLocked(void) {
    int i;

    if (sndInited) {
        return;
    }
    sndInited = 1;
    sndDebugSet = (getenv("SND_DEBUG") != NULL);

    for (i = 0; i < SND_PLAY_MAX; i++) {
        memset(sndPlays + i, 0, sizeof(sndPlays[i]));
    }
    if (sndSink == NULL) {
        sndSink = &sndEngineSink;
    }
    SND_LOG("sep init: %d packages, %d ticks a second\n", (int)SND_SEP_MAX, sndTickHz());
}

void sndSepInit(void) {
    pthread_mutex_lock(&sndSepLock);
    sndSepInitLocked();
    pthread_mutex_unlock(&sndSepLock);
}

void sndSepSetSink(const SndEvSink *sink, void *ctx) {
    pthread_mutex_lock(&sndSepLock);
    /* NULL puts the engine sink (the real SPU voices) back */
    sndSink = sink != NULL ? sink : &sndEngineSink;
    sndSinkCtx = sink != NULL ? ctx : NULL;
    pthread_mutex_unlock(&sndSepLock);
}

void sndSepSetMusicQuery(int (*isMusic)(short seqId, short track)) {
    pthread_mutex_lock(&sndSepLock);
    sndMusicQuery = isMusic;
    pthread_mutex_unlock(&sndSepLock);
}

static short sndSepOpenLocked(unsigned char *sep, short vabId) {
    SndSep *pkg;
    int i;

    sndSepInitLocked();
    if (vabId < 1 || vabId > 3 || sep == NULL) {
        return -1;
    }
    for (i = 0; i < SND_SEP_MAX; i++) {
        pkg = sndSeps + i;
        if (pkg->evs == NULL) {
            if (sndParseSep(sep, SND_SEP_SCAN_MAX, pkg) != 0) {
                return -1;
            }
            pkg->vabId = vabId;
            SND_LOG("sep open id=%d recs=%d vab=%d\n", i + 1, pkg->recCount,
                    (int)vabId);
            {
                int r;
                for (r = 0; r < pkg->recCount; r++) {
                    SndSepRec *rec = pkg->recs + r;
                    SND_LOG("sep rec id=%d rec=%d evs=%lu@%lu res=%d tempo=%lu "
                            "loop %lu..%lu every %d from %ld\n",
                            i + 1, r, rec->count, rec->first, (int)rec->res,
                            rec->tempo, rec->loopStartTick, rec->loopEndTick,
                            (int)rec->loopCount, rec->loopBody);
                }
            }
            return i + 1;
        }
    }
    return -1;
}

short sndSepOpen(unsigned char *sep, short vabId) {
    short id;

    pthread_mutex_lock(&sndSepLock);
    id = sndSepOpenLocked(sep, vabId);
    pthread_mutex_unlock(&sndSepLock);
    return id;
}

/* A5d: key a held voice off unless it was re-allocated since this play booked
   it. Retail's _SsVmKeyOff (libsnd_vm_key.s) never trusts a voice id: it
   scans the voices and releases only those whose stored vab/prog/seq/note
   still match, so a stolen voice playing something else is left alone. The
   shim's voice ids are bare, so the key-on serial stands in for that match
   (a steal always bumps it). Serial 0 (a sink without real voices) keeps the
   plain key-off. */
static void sndPlayKeyOffHeld(SndPlay *play, int i) {
    unsigned long now = sndVoiceSerial(play->held[i].voice);

    if (play->held[i].serial != 0 && now != play->held[i].serial) {
        SND_LOG("stale key-off ignored voice=%d ch=%d note=%d\n",
                (int)play->held[i].voice, (int)play->held[i].ch,
                (int)play->held[i].note);
        return;
    }
    sndSink->keyOff(sndSinkCtx, play->held[i].voice);
}

/* Guts of a stop: every voice the play still books goes back to the pool. */
static void sndPlayReleaseVoices(SndPlay *play) {
    int i;

    if (play->mod != 0) {
        audioModTrackStop(play->mod);
        play->mod = 0;
    }
    for (i = 0; i < play->heldCount; i++) {
        sndPlayKeyOffHeld(play, i);
    }
    play->heldCount = 0;
}

/* A key-off of (channel, note): every layer of it goes back to the pool,
   like retail's _SsVmKeyOff, which releases every voice matching it. */
static void sndPlayReleaseNote(SndPlay *play, int ch, int note) {
    int i, kept = 0;

    for (i = 0; i < play->heldCount; i++) {
        if (play->held[i].ch == ch && play->held[i].note == note) {
            sndPlayKeyOffHeld(play, i);
        } else {
            play->held[kept++] = play->held[i];
        }
    }
    play->heldCount = (short)kept;
}

void sndSepClose(short sepId) {
    SndSep *pkg;
    int i;

    pthread_mutex_lock(&sndSepLock);
    pkg = sndSepOf(sepId);
    if (pkg == NULL || pkg->evs == NULL) {
        pthread_mutex_unlock(&sndSepLock);
        return;
    }
    for (i = 0; i < SND_PLAY_MAX; i++) {
        if (sndPlays[i].playing && sndPlays[i].sep == pkg) {
            sndPlayReleaseVoices(sndPlays + i);
            sndPlays[i].playing = 0;
        }
    }
    sndSepFree(pkg);
    SND_LOG("sep close id=%d\n", (int)sepId);
    pthread_mutex_unlock(&sndSepLock);
}

/* --- the flat track space ------------------------------------------------- */

/* The game's SEP numbers run flat across a bank's opened packages:
   SOUND_SEP_COUNT (16) tracks per package, in the game's own SsSepOpen order.
   The ids' low byte addresses that flat space (sound.h: the W_JINGLE 0x6004
   001E addresses track 30, which lands in the second COMMON package's record
   14); a play that misses its own package looks the track up flat among the
   open packages sharing its vab. Tracks inside the handle's own package keep
   the direct indexing, which is what the music ids use. */
#define SND_SEP_TRACKS 16

static SndSep *sndSepTrackFor(short sepId, short rec, int *outRec) {
    SndSep *own;
    short vab;
    int i, bank[SND_SEP_MAX], bankCount = 0;

    own = sndSepOf(sepId);
    if (own == NULL || own->evs == NULL) {
        return NULL;
    }
    if (rec >= 0 && rec < own->recCount) {
        *outRec = rec;
        return own;
    }
    if (rec < 0) {
        return NULL;
    }
    vab = own->vabId;
    for (i = 0; i < SND_SEP_MAX; i++) {
        if (sndSeps[i].evs != NULL && sndSeps[i].vabId == vab) {
            bank[bankCount++] = i;
        }
    }
    if (rec / SND_SEP_TRACKS >= bankCount) {
        return NULL;
    }
    own = sndSeps + bank[rec / SND_SEP_TRACKS];
    if (rec % SND_SEP_TRACKS >= own->recCount) {
        return NULL;
    }
    *outRec = rec % SND_SEP_TRACKS;
    return own;
}

/* --- the player ----------------------------------------------------------- */

/* The play's own state, fresh: channels off their defaults, the voice map
   empty, the clock at the record's head tempo. The package volume stays --
   the game raises it between a stop and a play itself. */
static void sndPlayReset(SndPlay *play, SndSep *pkg, int rec) {
    SndSepRec *r = pkg->recs + rec;
    int ch;

    play->sep = pkg;
    play->rec = rec;
    play->pos256 = 0;
    play->tempo = r->tempo;
    play->nextEv = r->first;
    play->jumpsLeft = (r->loopCount == 0 || r->loopCount == 0x7F)
                    ? -1 : (short)r->loopCount;
    play->playing = 1;
    play->noteLog = 0;
    play->heldCount = 0;
    for (ch = 0; ch < 16; ch++) {
        play->ch[ch].prog = 0;
        play->ch[ch].vol = 127;
        play->ch[ch].pan = 64;
        play->ch[ch].expr = 127;
    }
}

/* E3d: the level a mod's WAV plays at: the package volume (with its fade) and the master, the way sndPlayNoteVol composes them, the two sides
   averaged, then squared like the last step of the voice chain (libsnd_vm_vol.s), so a fade sounds as it does with the sequence. 0..1000. */
static int sepModLevel(const SndPlay *play) {
    long v = (((long)(play->accL >> 8) * sndMasterL / 127) + ((long)(play->accR >> 8) * sndMasterR / 127)) / 2;

    return (int)(v * v * 1000 / (127 * 127));
}

/* The package's place among the packages of its bank, in the order the game opened them (slots fill in ascending order): the <pkg> of seq_<pkg>_<rec> */
static int sepOrdinal(const SndSep *pkg) {
    int j, n = 0;

    for (j = 0; j < (int)(pkg - sndSeps); j++) {
        if (sndSeps[j].evs != NULL && sndSeps[j].vabId == pkg->vabId) n++;
    }
    return n;
}

/* E3d: a mod's seq_<pkg>_<rec>.wav replaces the sequence the game just started: the play stays (stop, volume and fade still address it) but it
   delivers no events; the WAV is its sound. */
static void sepModStart(SndPlay *play, const SndSep *pkg, int rec) {
    const SndSepRec *r = pkg->recs + rec;
    int forever = r->loopBody >= 0 && (r->loopCount == 0 || r->loopCount == 0x7F);

    play->mod = audioModTrackStart(audioModBankOf(pkg->vabId - 1), sepOrdinal(pkg), rec, forever, play->music, sepModLevel(play));
}

static short sepPlayStartLocked(short sepId, short rec, short playMode,
                                short loopCount, short volL, short volR) {
    SndPlay *play;
    SndSep *pkg;
    int recLocal;

    /* SSPLAY_PAUSE (0) and the other modes get no delivery here (T35
       simplification); retail's SSPLAY_PLAY is 1. */
    (void)playMode;
    if (rec < 0 || sndSepOf(sepId) == NULL) {
        return -1;
    }
    pkg = sndSepTrackFor(sepId, rec, &recLocal);
    if (pkg == NULL) {
        SND_LOG("sep reject id=%d rec=%d: no package in its bank's flat space "
                "holds it\n", (int)sepId, (int)rec);
        return -1;
    }
    /* one play per (package, record): replaying the same record restarts it
       (retail rewinds the track in Snd_SetPlayMode; the ringing voices go
       back here too, as the shim has no ADSR tail to let them die in). A
       different record of the package plays alongside. */
    play = sndPlayFind(pkg, recLocal);
    if (play != NULL) {
        sndPlayReleaseVoices(play);
    } else {
        play = sndPlayAlloc();
        if (play == NULL) {
            SND_LOG("sep reject id=%d rec=%d: all %d plays busy\n",
                    (int)sepId, (int)rec, SND_PLAY_MAX);
            return -1;
        }
    }
    /* SsSepPlay's own l_count: retail's SSPLAY_INFINITY is 0 */
    play->passesLeft = (loopCount == 0) ? -1 : loopCount;
    play->music = sndMusicQuery == NULL || sndMusicQuery(sepId, rec) != 0;
    play->volL = volL;
    play->volR = volR;
    play->fading = 0;
    play->fadeStep = 0;
    play->accL = (unsigned long)volL << 8;
    play->accR = (unsigned long)volR << 8;
    sndPlayReset(play, pkg, recLocal);
    sepModStart(play, pkg, recLocal);
    SND_LOG("sep play id=%d rec=%d flat->id=%d/%d slot=%d passes=%d vol=%d/%d\n",
            (int)sepId, (int)rec, (int)(pkg - sndSeps) + 1, recLocal,
            (int)(play - sndPlays),
            (int)play->passesLeft, (int)volL, (int)volR);
    return (short)(sepId - 1);
}

short sndSepPlayStart(short sepId, short rec, short playMode, short loopCount,
                      short volL, short volR) {
    short ret;

    pthread_mutex_lock(&sndSepLock);
    ret = sepPlayStartLocked(sepId, rec, playMode, loopCount, volL, volR);
    pthread_mutex_unlock(&sndSepLock);
    return ret;
}

void sndSepPlayStop(short sepId, short rec) {
    SndPlay *play;
    SndSep *pkg;
    int recLocal;

    pthread_mutex_lock(&sndSepLock);
    pkg = sndSepTrackFor(sepId, rec, &recLocal);
    play = pkg != NULL ? sndPlayFind(pkg, recLocal) : NULL;
    if (play != NULL) {
        sndPlayReleaseVoices(play);
        play->playing = 0;
        SND_LOG("sep stop id=%d rec=%d flat->%d\n", (int)sepId, (int)rec,
                recLocal);
    }
    pthread_mutex_unlock(&sndSepLock);
}

void sndSepPlaySetVol(short sepId, short rec, short volL, short volR) {
    SndPlay *play;
    SndSep *pkg;
    int recLocal;

    pthread_mutex_lock(&sndSepLock);
    pkg = sndSepTrackFor(sepId, rec, &recLocal);
    play = pkg != NULL ? sndPlayFind(pkg, recLocal) : NULL;
    if (play == NULL) {
        /* a play not running takes its volume from SsSepPlay */
        pthread_mutex_unlock(&sndSepLock);
        return;
    }
    play->volL = volL;
    play->volR = volR;
    play->accL = (unsigned long)volL << 8;
    play->accR = (unsigned long)volR << 8;
    play->fading = 0;
    play->fadeStep = 0;
    SND_LOG("sep vol id=%d rec=%d %d/%d\n", (int)sepId, (int)rec, (int)volL,
            (int)volR);
    pthread_mutex_unlock(&sndSepLock);
}

void sndSepPlayDecrescendo(short sepId, short rec, short vol, long frames) {
    SndPlay *play;
    SndSep *pkg;
    int recLocal;
    unsigned long total;

    pthread_mutex_lock(&sndSepLock);
    pkg = sndSepTrackFor(sepId, rec, &recLocal);
    play = pkg != NULL ? sndPlayFind(pkg, recLocal) : NULL;
    if (play == NULL) {
        pthread_mutex_unlock(&sndSepLock);
        return;
    }
    if (vol <= 0 || frames <= 0) {
        /* nothing to take away, or no time to take it in: silent in place */
        play->volL = play->volR = 0;
        play->accL = play->accR = 0;
        play->fading = 0;
        play->fadeStep = 0;
        pthread_mutex_unlock(&sndSepLock);
        return;
    }
    /* The game's own call is SsSepSetDecrescendo(seq, sep, 0x80, 0x3C): the
       volume to lose, spread over so many frames, each frame a step. */
    total = (unsigned long)vol << 8;
    play->accL = (unsigned long)play->volL << 8;
    play->accR = (unsigned long)play->volR << 8;
    play->fadeStep = total / (unsigned long)frames;
    if (play->fadeStep == 0) {
        play->accL = play->accR = 0;    /* a decay too small to ever step */
    }
    play->fading = play->fadeStep != 0;
    SND_LOG("sep decrescendo id=%d rec=%d %d over %ld\n", (int)sepId,
            (int)rec, (int)vol, frames);
    pthread_mutex_unlock(&sndSepLock);
}

void sndSetMasterVol(short left, short right) {
    if (left < 0) {
        left = 0;
    } else if (left > 127) {
        left = 127;
    }
    if (right < 0) {
        right = 0;
    } else if (right > 127) {
        right = 127;
    }
    pthread_mutex_lock(&sndSepLock);
    sndMasterL = left;
    sndMasterR = right;
    SND_LOG("sep master vol %d/%d\n", sndMasterL, sndMasterR);
    pthread_mutex_unlock(&sndSepLock);
}

/* The volume of the next key-on: velocity * package * channel * expression *
   master, each 0..127 lowered in turn; the player hands the sink the two
   sides it computed (the sink's tone volume and pan then still apply,
   exactly like the one-shot key-on's do). */
static void sndPlayNoteVol(const SndPlay *play, int ch, int vel,
                           short *volL, short *volR) {
    unsigned long l, r;

    /* Composition step by step, each factor a 0..127 step down, floored: */
    l = (unsigned long)vel;
    r = (unsigned long)vel;
    l = l * (play->accL >> 8) / 127;    /* the package (incl. its fade) */
    r = r * (play->accR >> 8) / 127;
    l = l * play->ch[ch].vol / 127;
    r = r * play->ch[ch].vol / 127;
    l = l * play->ch[ch].expr / 127;
    r = r * play->ch[ch].expr / 127;
    l = l * sndMasterL / 127;
    r = r * sndMasterR / 127;
    if (play->ch[ch].pan <= 64) {
        r = r * play->ch[ch].pan / 64;
    } else {
        l = l * (127 - play->ch[ch].pan) / 63;
    }
    *volL = (short)l;
    *volR = (short)r;
}

static void sndPlayDeliver(SndPlay *play, SndEv *ev) {
    short voices[SND_LAYERS_MAX];
    short volL, volR;
    int ch = (int)ev->ch;
    int n, i;

    switch (ev->kind) {
    case SND_EV_KEYON:
        sndPlayNoteVol(play, ch, ev->data, &volL, &volR);
        /* A repeated key-on on the same (channel, note) replaces the voice
           the map had: without a key-off of the previous one, the old voice
           rings at full volume forever in a sink without an ADSR envelope,
           and the track starts sounding doubled over itself (the field
           BGM's restarts of stage 785 and 790 do exactly this). Key the
           previous voice off first, the same release the map's own
           KEYOFF case and the polyphonic handoff below use. */
        sndPlayReleaseNote(play, ch, ev->note);
        sndDeliverMusic = play->music;
        n = sndSink->keyOn(sndSinkCtx, play->sep->vabId,
                           play->ch[ch].prog, (short)ev->note, volL, volR,
                           voices);
        for (i = 0; i < n; i++) {
            if (play->heldCount >= SND_HELD_MAX) {
                SND_LOG("sep held voices full (%d): voice %d released\n",
                        SND_HELD_MAX, (int)voices[i]);
                sndSink->keyOff(sndSinkCtx, voices[i]);
                continue;
            }
            play->held[play->heldCount].ch = (unsigned char)ch;
            play->held[play->heldCount].note = ev->note;
            play->held[play->heldCount].voice = voices[i];
            play->held[play->heldCount].serial = sndVoiceSerial(voices[i]);
            play->heldCount++;
        }
        play->noteLog++;
        if (play->noteLog % 25 == 1) {
            SND_LOG("sep note id=%d ch=%d prog=%d note=%d vel=%d -> "
                    "%d voice(s) first=%d vol=%d/%d\n",
                    (int)(play->sep - sndSeps) + 1, ch, (int)play->ch[ch].prog,
                    (int)ev->note, (int)ev->data, n, n > 0 ? (int)voices[0] : -1,
                    (int)volL, (int)volR);
        }
        break;
    case SND_EV_KEYOFF:
        sndPlayReleaseNote(play, ch, ev->note);
        break;
    case SND_EV_PROG:
        play->ch[ch].prog = ev->data;
        break;
    case SND_EV_CTRL:
        /* T35 simplification: only volume/pan/expression move state */
        if (ev->note == 7) {
            play->ch[ch].vol = ev->data;
        } else if (ev->note == 10) {
            play->ch[ch].pan = ev->data;
        } else if (ev->note == 11) {
            play->ch[ch].expr = ev->data;
        }
        break;
    case SND_EV_BEND:
        /* retail bends the voices ringing on the channel; a later key-on
           starts unbent (note2pitch, fine 0), so no bend state is kept */
        if (sndSink->bend != NULL) {
            for (i = 0; i < play->heldCount; i++) {
                if (play->held[i].ch == ch) {
                    sndSink->bend(sndSinkCtx, play->held[i].voice, ev->data);
                }
            }
        }
        break;
    case SND_EV_TEMPO:
        play->tempo = ev->datum;
        break;
    default:
        break;
    }
}

/* The most times one frame may wrap a loop body: a body shorter than a frame
   (a tempo change can make it so) would otherwise spin without end. */
#define SND_LOOP_WRAPS 8

/* O7: the tick rate (see snd_internal.h). One int written at init, read by the
   vsync thread. */
static volatile int sndTickRate = 50;

int sndTickHzFromMode(long tickMode) {
    long low = tickMode & 0xFFF;

    switch (low) {
    case 0: return 60;
    case 1: return 50;
    case 2: return 120;
    case 3: return 240;
    default: return low > 5 ? (int)low : 60;
    }
}

void sndSetTickHz(int hz) {
    if (hz > 0) {
        sndTickRate = hz;
    }
}

int sndTickHz(void) {
    return sndTickRate;
}

unsigned long sndSepStep256(unsigned long res, unsigned long tempo, int hz) {
    if (tempo == 0 || hz <= 0) {
        return 0;
    }
    return (unsigned long)((unsigned long long)res * 1000000u * 256u /
                           ((unsigned long long)tempo * (unsigned long long)hz));
}

int sndEnvTickSamples(void) {
    return 44100 / sndTickRate;
}

/* The frame: the package's fade (SsSepSetDecrescendo) first, then the
   engine's tick of res * 1e6 / tempo / sndTickHz() (the game's
   SsSetTickMode: SS_NOTICK ticks 60 a second, SS_NOTICK | 50 the PAL
   build's 50), then the
   delivery of what sits between the last position and this one. */
void sndSepTbyT(void) {
    int i;

    pthread_mutex_lock(&sndSepLock);
    sndSepInitLocked();
    for (i = 0; i < SND_PLAY_MAX; i++) {
        SndPlay *play = sndPlays + i;
        SndSep *pkg;
        SndSepRec *r;
        unsigned long step256, target;
        int wraps;

        if (!play->playing) {
            continue;
        }
        pkg = play->sep;
        if (pkg == NULL || pkg->evs == NULL || play->rec >= pkg->recCount) {
            sndPlayReleaseVoices(play);
            play->playing = 0;
            continue;
        }
        r = pkg->recs + play->rec;

        /* the fade reduces the package volume before the frame's own notes */
        if (play->fading) {
            if (play->accL <= play->fadeStep) {
                play->accL = 0;
            } else {
                play->accL -= play->fadeStep;
            }
            if (play->accR <= play->fadeStep) {
                play->accR = 0;
            } else {
                play->accR -= play->fadeStep;
            }
            if (play->accL == 0 && play->accR == 0) {
                play->volL = play->volR = 0;
                play->fading = 0;
            } else {
                play->volL = (short)(play->accL >> 8);
                play->volR = (short)(play->accR >> 8);
            }
        }

        /* E3d: a WAV in place of the sequence only follows the game's volume and ends when it plays out */
        if (play->mod != 0) {
            audioModTrackLevel(play->mod, sepModLevel(play));
            if (!audioModTrackActive(play->mod)) {
                play->mod = 0;
                play->playing = 0;
                SND_LOG("sep stop (wav over) id=%d rec=%d\n", (int)(pkg - sndSeps) + 1, play->rec);
            }
            continue;
        }

        /* the engine's tick, in 1/256 ticks of the record's own clock; the
           FF 51s already retuned play->tempo keeps the position musical */
        step256 = sndSepStep256(r->res, play->tempo, sndTickHz());
        play->pos256 += step256;
        target = play->pos256 >> 8;

        /* Deliver up to the position; when the span is over the play loops
           back and the frame's overshoot past the loop's end carries into
           the body, whose events it covers are delivered in this same frame.
           A frame longer than a short body wraps it again, so the wraps are
           bounded: past SND_LOOP_WRAPS the rest waits for the next frame. */
        for (wraps = 0;; wraps++) {
            while (play->nextEv < r->first + r->count) {
                SndEv *ev = pkg->evs + play->nextEv;

                if (target < ev->tick) {
                    break;
                }
                play->nextEv++;
                if (ev->kind == SND_EV_END) {
                    break;
                }
                sndPlayDeliver(play, ev);
            }

            if (play->nextEv < r->first + r->count ||
                target < r->loopEndTick) {
                break;
            }
            /* the span is over: jump back into the CC99 = 0x14 body, spend
               one of the finite counts, or stop (its voices back) */
            if (r->loopBody >= 0 &&
                (r->loopCount == 0 || r->loopCount == 0x7F ||
                 play->jumpsLeft > 0)) {
                if (r->loopCount != 0 && r->loopCount != 0x7F) {
                    play->jumpsLeft--;
                }
                if (r->loopEndTick > r->loopStartTick) {
                    /* keep what the frame advanced past the loop's end */
                    play->pos256 = (r->loopStartTick << 8) +
                                   (play->pos256 - (r->loopEndTick << 8));
                } else {
                    /* an empty body has no overshoot to carry */
                    play->pos256 = r->loopStartTick << 8;
                }
                play->nextEv = r->loopBody;
                SND_LOG("sep loop id=%d rec=%d to tick %lu\n", (int)(pkg - sndSeps) + 1,
                        play->rec, r->loopStartTick);
                target = play->pos256 >> 8;
                if (r->loopEndTick <= r->loopStartTick ||
                    wraps + 1 >= SND_LOOP_WRAPS) {
                    break;
                }
                continue;
            } else if (r->loopBody >= 0 && play->passesLeft > 1) {
                /* T35 simplification: l_count counts whole passes and the
                   game always passes 1 */
                play->passesLeft--;
                sndPlayReset(play, pkg, play->rec);
            } else {
                sndPlayReleaseVoices(play);
                play->playing = 0;
                SND_LOG("sep stop (over) id=%d rec=%d pos=%lu\n", (int)(pkg - sndSeps) + 1,
                        play->rec, target);
            }
            break;
        }
    }
    pthread_mutex_unlock(&sndSepLock);
}
