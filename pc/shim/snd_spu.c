/*
 * The VAB/SPU backend behind the libsnd surface in shim/host_snd.c (ADR-002:
 * PsyCross provides libspu but no libsnd, so the game's own Ss* calls land on
 * this port's own code, which drives PsyCross's OpenAL SPU renderer through
 * PsyQ's LIBSPU wrappers -- Spu* in external/psycross/src/psx/LIBSPU.C, all
 * compiled into the same static library these binaries link).
 *
 * What this milestone (T34) implements, exactly the calls the game's own
 * sound unit (src/engine/sound/sound.c) makes on the one-shot path:
 *
 *   - VAB headers (VH): parsed here, from the bytes the game's own loader
 *     already hands over (no new file access). The layout is the one the
 *     discs carry, verified against MPCOMMON.BIN (observed: header length
 *     0x20 + 0x800 + ps*0x200 + 0x200 comes out exact, and every header's
 *     non-empty ProgAtr count equals ps):
 *         0x000 VabHdr         32 bytes ('pBAV', ps/ts/vs, master vol/pan)
 *         0x020 ProgAtr[128]   16 B each, .tones says which programs are used
 *         0x820 tone pages     0x200 B per USED program, packed in slot order
 *                              (a record's own .prog names the parent; 32 B
 *                              per tone, slots 0..15 of each page)
 *         after the pages      VAG size table: 256 u16, in 8-byte units,
 *                              1-indexed ([0] is the spacer)
 *   - the VAB body (VB) written into PsyX's SPU RAM at the address the game
 *     itself makes room for (SOUND_SPU_ADDRS: 0x1010 / 0x49C10 / 0x62410),
 *     where real LIBSPU would have put it, so a tone's VAG address is just
 *     base + summed sizes.
 *   - key-on: PsyX_SPUAL's own upload+play path (SetVoiceAttr, SetKey), with
 *     the samples PsyX_SPUAL decodes out of that SPU RAM.
 *
 * T34 simplifications, on purpose, so the milestone lands instead of stalling:
 *   - ADSR (A4): PsyX_SPUAL flags "TODO: implement ADSR" itself, so the
 *     envelope is done here in software (see the A4 block below): every
 *     voice runs the PS1 attack/decay/sustain/release from the tone's
 *     adsr1/adsr2 and the voice ends when the envelope reaches 0.
 *   - no loop points constructed: loop_addr == addr; OpenAL looping then
 *     comes from the ADPCM block flags PsyX_SPUAL's decodeSound finds
 *     itself (the discs mark loops in the block stream that way).
 *   - the reverb unit stays a stub in host_snd.c; PsyX exposes SpuSetReverb*
 *     hooks a later milestone could route it to.
 *
 * pc/sndtest.c drives the parser and the key-on math through
 * shim/snd_internal.h with a synthetic fixture in memory (no disc, no data
 * file committed).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include <psx/libspu.h>
#ifdef _WIN32
#include <SDL_stdinc.h>
#endif

#include "audio_mod.h"
#include "port_log.h"
#include "snd_internal.h"

#define VAB_MAGIC 0x56414270L /* "pBAV" read little endian */
#define VAB_PROG_ATR    0x020 /* ProgAtr[128], 16 B each */
#define VAB_TONE_PAGES  0x820 /* the tone pages, 0x200 B per used program */
#define VAB_PAGE_SIZE   0x200
#define VAB_PAGE_TONES  16
#define VAB_SIZE_TABLE  0x200 /* the trailing u16 VAG size table, fixed size */
/* an upper bound for the header bytes a key-on needs: the biggest VH of the
   format (0x20 + 0x800 + 128 * 0x200 + 0x200 = 0x9A20) fits inside every
   head buffer the game reserves, too (SOUND_HEAD_BUFFER_1, 40960 bytes) */
#define VAB_HEAD_MAX    0x9A20

/* one registry entry per SOUND slot; ids are the ids the old stub reported
   (slot + 1), which the game stores and later keys off of */
static SndVab sndVabs[3];

/* PsyX_SPUAL's 24-voice pool and our own bookkeeping of it */
static char sndVoiceUsed[SND_VOICE_COUNT];
/* what each booked voice plays, so a bend can re-pitch it (A3) */
static struct {
    char live;
    short vabId, prog, tone, note;
    /* A4: the envelope and what the gain is scaled from */
    SndEnv env;
    short baseL, baseR;       /* the key-on's volumes (envelope level = max) */
    int appliedLevel;         /* the level the AL gain was last set from */
    unsigned appliedGen;      /* O4: the slider generation it was set under */
    char looping;             /* the VAG's last block has the repeat flag */
    unsigned long endSamples; /* A6e: samples the VAG plays before its end flag mutes it; 0 = never */
    double pos;               /* A6e: samples played so far (pitch-scaled) */
    unsigned short pitch;     /* A6e: the voice's current PsyX pitch (0x1000 = 44100 Hz) */
    char warned;              /* the >10 s warning fired */
    char src;                 /* 0 = SsUtKeyOn, 1 = SEP music, 2 = SEP effect */
    unsigned long age;        /* samples since key-on */
    signed char prior;        /* the tone's priority (A5b, _SsVmAlloc +0x1A) */
    unsigned long serial;     /* A5d: key-on serial, never 0 */
    short allocAge;           /* key-ons granted since this one (A5b, +0x02) */
} sndVoiceInfo[SND_VOICE_COUNT];

/* the tick (vsync thread) and the game thread's key-on/off share the voice
   table; one lock around every touch of it */
static pthread_mutex_t sndVoiceLock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long sndSerialNext = 1;

static int sndInited;
static char sndDebugSet;
/* DW3_SND_NOENV: debug knob that turns the A4 envelope off (voices then sound
   until a key-off, as before A4) while the voice watch still logs. */
static char sndEnvOff;
/* DW3_SND_MUTE: the owner's test runs must not push audio to the system's
   default sink (which may be a Bluetooth device someone is listening to).
   When set, nothing is played through the real voices; every other part of
   the sound system keeps running as usual. */
static char sndMuted;

/* --- debug log ----------------------------------------------------------- */

#define SND_LOG(...)                                                           \
    do {                                                                       \
        if (sndDebugSet) {                                                     \
            fputs("snd: ", stderr);                                            \
            fprintf(stderr, __VA_ARGS__);                                      \
        }                                                                      \
    } while (0)

/* --- the VH parser (exposed for pc/sndtest.c as well) -------------------- */

/* The little-endian words the header stores */
static unsigned long sndLoad32(const unsigned char *at) {
    return (unsigned long)at[0] | ((unsigned long)at[1] << 8) |
           ((unsigned long)at[2] << 16) | ((unsigned long)at[3] << 24);
}

static unsigned long sndLoad16(const unsigned char *at) {
    return (unsigned long)at[0] | ((unsigned long)at[1] << 8);
}

int sndParseVab(const unsigned char *vh, unsigned long len, SndVab *out) {
    unsigned long pages, i;
    unsigned long cum;

    memset(out, 0, sizeof(*out));
    out->mvol = vh[0x18]; /* O4: VabHdr.mvol */
    for (i = 0; i < 128; i++) {
        out->rank[i] = -1;
    }

    if (len < 0x20 || sndLoad32(vh) != VAB_MAGIC) {
        return -1;
    }
    out->progs = (int)sndLoad16(vh + 0x12);
    out->vagCount = (int)sndLoad16(vh + 0x16);
    if (out->progs < 1 || out->progs > 128 || out->vagCount < 1 ||
        out->vagCount > 255) {
        return -1;
    }

    for (i = 0; i < 128; i++) {
        out->progVol[i] = vh[VAB_PROG_ATR + i * 16 + 1];
        out->progPan[i] = vh[VAB_PROG_ATR + i * 16 + 4];
    }

    pages = VAB_TONE_PAGES + (unsigned long)out->progs * VAB_PAGE_SIZE;
    /* what the parser reads ends here (the size table's end); beyond it is
       the VB's business */
    if (pages + VAB_SIZE_TABLE > len) {
        return -1;
    }

    /* tone pages in rank order; each record's .prog names its own program */
    for (i = 0; i < (unsigned long)out->progs * VAB_PAGE_TONES; i++) {
        const unsigned char *rec = vh + VAB_TONE_PAGES + i * 0x20;
        int page = (int)(i / VAB_PAGE_TONES);
        int t = (int)(i % VAB_PAGE_TONES);
        int prog;
        int vag;

        if (page >= 128 || t >= VAB_PAGE_TONES) {
            return -1;
        }
        prog = (int)sndLoad16(rec + 0x14);
        vag = (int)sndLoad16(rec + 0x16);
        out->tone[page][t].vag = (unsigned char)vag;
        out->tone[page][t].prior = (signed char)rec[0];
        out->tone[page][t].vol = rec[2];
        out->tone[page][t].pan = rec[3];
        out->tone[page][t].center = rec[4];
        out->tone[page][t].shift = rec[5];
        out->tone[page][t].min = rec[6];
        out->tone[page][t].max = rec[7];
        out->tone[page][t].adsr1 = (unsigned short)sndLoad16(rec + 0x10);
        out->tone[page][t].adsr2 = (unsigned short)sndLoad16(rec + 0x12);
        out->tone[page][t].prog = (short)prog;
        out->tone[page][t].pbDown = rec[0x0C];
        out->tone[page][t].pbUp = rec[0x0D];
        if (prog >= 0 && prog < 128 && vag >= 1 && vag < 256 &&
            out->rank[prog] < 0) {
            out->rank[prog] = page;
        }
    }

    /* the VAG sizes, 8-byte units, in their order: the body offsets follow */
    cum = 0;
    for (i = 1; i <= (unsigned long)out->vagCount; i++) {
        unsigned long units = sndLoad16(vh + pages + i * 2);

        out->vagSize[i] = units * 8;
        out->vagOfs[i] = cum;
        cum += out->vagSize[i];
    }
    out->bodyBytes = cum;
    return 0;
}

/* --- the registry + the PsyX side ---------------------------------------- */

static SndVab *sndVab(short vabId) {
    if (vabId < 1 || vabId > 3) {
        return NULL;
    }
    return sndVabs + (vabId - 1);
}

void sndInit(void) {
    if (sndInited) {
        return;
    }
    sndInited = 1;
    sndDebugSet = (getenv("SND_DEBUG") != NULL);
    sndMuted = (getenv("DW3_SND_MUTE") != NULL);
    sndEnvOff = (getenv("DW3_SND_NOENV") != NULL);

    /* A muted run also points OpenAL at its null backend, before PsyX_SPUAL
       opens the device below. Reason: PsyX_SPUAL opens the default device
       through OpenAL regardless, and that open on its own creates a playback
       stream on the system's default sink — the exact Bluetooth speaker this
       flag exists to leave alone. With the null backend every OpenAL user of
       this process stays sinkless and the real audio bus of the user's other
       apps is untouched. */
    if (sndMuted) {
#ifdef _WIN32
        /* SDL_setenv reaches the process environment OpenAL Soft's DLL reads; the CRT's _putenv
           of this executable may be another copy of it. */
        SDL_setenv("ALSOFT_DRIVERS", "null", 1);
#else
        setenv("ALSOFT_DRIVERS", "null", 1);
#endif
        SND_LOG("mute: OpenAL pinned to the null backend\n");
    }

    /* PsyX_SPUAL's own init (the OpenAL device and its 24 voices), through
       LIBSPU's wrapper, as the retail boot sequence reaches this too */
    SpuInit();
    SND_LOG("spu init: PsyX_SPUAL through SpuInit, %d voices\n",
            (int)SND_VOICE_COUNT);
}

short sndVabOpen(unsigned char *head, short slot, unsigned long spuAddr) {
    SndVab *vab;
    int id;

    if (slot < 0 || slot > 2 || head == NULL) {
        return -1;
    }
    vab = sndVabs + slot;
    if (sndParseVab(head, VAB_HEAD_MAX, vab) != 0) {
        memset(vab, 0, sizeof(*vab));
        return -1;
    }
    id = slot + 1;
    vab->used = 1;
    vab->slot = slot;
    vab->spuAddr = spuAddr;
    vab->bodyLoaded = 0;
    audioModBankOpened(slot); /* E3c/E3d: which bank folder a mod's files for this slot are in */
    SND_LOG("vab open id=%d slot=%d progs=%d vags=%d base=%#lx body=%lu\n",
            id, (int)slot, vab->progs, vab->vagCount, spuAddr, vab->bodyBytes);
    if (sndDebugSet) {
        int pg, t;
        unsigned long pi;

        SND_LOG("vab %d header mvol=%d pan=%d\n", id, head[0x18], head[0x19]);

        for (pi = 0; pi < 128; pi++) {
            const unsigned char *pa = head + VAB_PROG_ATR + pi * 16;

            if (pa[0] != 0) {
                SND_LOG("vab %d prog %lu tones=%d mvol=%d prior=%d mode=%d "
                        "mpan=%d\n", id, pi, pa[0], pa[1], pa[2], pa[3], pa[4]);
            }
        }

        for (pg = 0; pg < vab->progs && pg < 128; pg++) {
            for (t = 0; t < VAB_PAGE_TONES; t++) {
                const SndTone *tn = &vab->tone[pg][t];

                if (tn->vag < 1 || tn->vag > vab->vagCount) {
                    continue;
                }
                SND_LOG("vab %d tone prog=%d t=%d vag=%d center=%d shift=%d "
                        "range=%d..%d vol=%d pan=%d prior=%d pb=%d/%d\n", id,
                        (int)tn->prog, t, (int)tn->vag, (int)tn->center,
                        (int)(signed char)tn->shift, (int)tn->min, (int)tn->max,
                        (int)tn->vol, (int)tn->pan, (int)tn->prior,
                        (int)tn->pbDown, (int)tn->pbUp);
            }
        }
    }
    return id;
}

/* The real LIBSPU would have written the whole body into SPU RAM at the
   base and told the game the transfer was done once the DMA was; PsyX_SPUAL
   copies into a host buffer addressed the same way */
/* A6e: where the SPU stops a sample by itself. The first ADPCM block with the
   end flag (bit 0) and no repeat flag (bit 1) mutes the voice: the envelope is
   forced to 0 and libsnd's _SsVmFlush (libsnd_vm_f.s:46-58, SpuGetVoiceEnvelope
   == 0) gives the voice back, whatever its ADSR says. Returns the PCM samples
   played before that point (28 per block), or 0 when the sample loops (end +
   repeat) or has no end flag at all: only the envelope or a key-off ends it. */
unsigned long sndVagEndSamples(const unsigned char *vag, unsigned long size) {
    unsigned long b;

    for (b = 0; b + 16 <= size; b += 16) {
        unsigned char flags = vag[b + 1];

        if (flags & 1) {
            return (flags & 2) ? 0 : (b / 16 + 1) * 28;
        }
    }
    return 0;
}

/* E3c/U7: the samples a mod replaced. PsyX_SPUAL's SPU RAM is a fixed 2 MB array (and its decoder a fixed 512K-sample buffer), so a
   replacement (maybe far longer than the original) is not written there at all: it is decoded to PCM once, when the bank loads,
   and kept by audio_mod.c's PCM output (no cap but memory). The tones keep pointing at the VAG number; only the address, the size
   and the end/loop facts of that VAG change (vagAddr). The address is a virtual one, SND_PCM_BASE + slot * 0x100 + the VAG number:
   not an SPU RAM address, the key-on sees it and starts the PCM voice instead of PsyX's. A bank slot reloading drops its buffers. */
#define SND_PCM_BASE 0x40000000UL

static int sndPcmBuf[3][256]; /* the output's buffer of each replaced VAG of a slot, 0 = the game's own */

static void sndPcmDropSlot(int slot) {
    int i, v;

    if (slot < 0 || slot >= 3) {
        return;
    }
    pthread_mutex_lock(&sndVoiceLock);
    for (i = 0; i < 256; i++) {
        if (sndPcmBuf[slot][i] != 0) {
            /* a voice still playing it is stopped first, and booked free like a key-off would */
            for (v = 0; v < SND_VOICE_COUNT; v++) {
                if (sndVoiceInfo[v].live && audioModPcmActive(v) && sndVoiceInfo[v].vabId == slot + 1) {
                    audioModPcmStop(v);
                }
            }
            audioModPcmRelease(sndPcmBuf[slot][i]);
            sndPcmBuf[slot][i] = 0;
        }
    }
    pthread_mutex_unlock(&sndVoiceLock);
}

static void sndVabApplyMods(SndVab *vab) {
    const char *bank = audioModBankOf(vab->slot);
    int i, count = 0;
    unsigned long used = 0;

    sndPcmDropSlot(vab->slot);
    if (bank[0] == '\0' || vab->slot < 0 || vab->slot >= 3) {
        return;
    }
    for (i = 1; i <= vab->vagCount && i < 256; i++) {
        AudioModVag rep;
        int buf;

        if (vab->vagSize[i] == 0 || !audioModVagReplacement(bank, i, &rep)) {
            continue;
        }
        buf = audioModPcmBuffer(rep.adpcm, rep.size, rep.loops);
        if (buf == 0) {
            plogLine("audio", "%s: vag_%03d.wav could not be loaded for playing (no audio output, or out of memory): the game's sample stays\n",
                     bank, i);
            free(rep.adpcm);
            continue;
        }
        sndPcmBuf[vab->slot][i] = buf;
        vab->vagAddr[i] = SND_PCM_BASE + (unsigned long)vab->slot * 0x100UL + (unsigned long)i;
        vab->vagSize[i] = rep.size;
        vab->vagLoops[i] = (unsigned char)rep.loops;
        vab->vagEnd[i] = sndVagEndSamples(rep.adpcm, rep.size);
        used += rep.size;
        count++;
        free(rep.adpcm);
    }
    if (count > 0) {
        SND_LOG("mod samples: %d replaced in slot %d, %lu bytes of ADPCM played as PCM\n", count, (int)vab->slot, used);
    }
}

short sndVabTransBody(unsigned char *body, short vabId) {
    SndVab *vab = sndVab(vabId);

    if (vab == NULL || !vab->used || body == NULL) {
        return -1;
    }
    sndInit(); /* PsyX's SPU must be up before its RAM is written */
    if (SpuSetTransferStartAddr((unsigned int)vab->spuAddr) == 0 ||
        SpuWrite(body, (unsigned int)vab->bodyBytes) != vab->bodyBytes) {
        vab->bodyLoaded = 0;
        return -1;
    }
    vab->bodyLoaded = 1;
    {
        int i;

        /* a VAG whose last ADPCM block has the end (bit 0) and repeat
           (bit 1) flags set loops on the SPU: only the envelope ends it */
        memset(vab->vagLoops, 0, sizeof(vab->vagLoops));
        memset(vab->vagEnd, 0, sizeof(vab->vagEnd));
        for (i = 1; i <= vab->vagCount; i++) {
            unsigned long end = vab->vagOfs[i] + vab->vagSize[i];

            if (vab->vagSize[i] >= 16 && end <= vab->bodyBytes &&
                (body[end - 15] & 3) == 3) {
                vab->vagLoops[i] = 1;
            }
            vab->vagEnd[i] = sndVagEndSamples(body + vab->vagOfs[i],
                                              vab->vagSize[i]);
        }
        memset(vab->vagAddr, 0, sizeof(vab->vagAddr));
        sndVabApplyMods(vab);
    }
    SND_LOG("body write id=%d base=%#lx bytes=%lu\n", vabId, vab->spuAddr,
            vab->bodyBytes);
    return vabId;
}

/* the SPU write is a synchronous memcpy host-side, PsyX_SPUAL builds it so */
short sndVabTransCompleted(short immediate) {
    (void)immediate;
    return 1;
}

/* PsyX owns its SPU RAM and cannot free one region; the next bank into the
   same slot overwrites the very same bytes through sndVabTransBody. */
void sndVabClose(short vabId) {
    SndVab *vab = sndVab(vabId);

    if (vab == NULL || !vab->used) {
        return;
    }
    vab->used = 0;
    sndPcmDropSlot(vab->slot);
    audioModBankClosed(vab->slot);
    SND_LOG("vab close id=%d slot=%d\n", vabId, (int)vab->slot);
}

/* --- the key-on math (pc/sndtest.c drives it directly) -------------------- */

int sndNotePrepare(short vabId, short prog, short tone, short note, short fine,
                   short volL, short volR, struct SndNote *out) {
    SndVab *vab = sndVab(vabId);
    SndTone *toneRec;
    long fvol, total, volLr, volRr;
    long rank, pan;
    int i;
    double semitones, pitch;

    if (vab == NULL || !vab->used) {
        return -1;
    }
    if (prog < 0 || prog >= 128 || tone < 0 || tone >= VAB_PAGE_TONES) {
        return -1;
    }
    rank = vab->rank[prog];
    if (rank < 0 || rank >= vab->progs) {
        return -1;
    }
    toneRec = &vab->tone[rank][tone];
    if (toneRec->vag < 1 || toneRec->vag > vab->vagCount ||
        vab->vagSize[toneRec->vag] == 0) {
        return -1;
    }

    /* the VAG's own SPU address: the game's base for the slot, plus the VAG
       sizes before it in the body's stream */
    out->addr = vab->vagAddr[toneRec->vag] != 0
                    ? vab->vagAddr[toneRec->vag]
                    : vab->spuAddr + vab->vagOfs[toneRec->vag];
    /* T34: DECENT no loop points built here; PsyX_SPUAL's decodeSound finds
       the ADPCM's own loop/end flags in the body */
    out->loopAddr = out->addr;

    /* pitch: PsyX's 0x1000-based value, relative to the 44100 the decoded
       VAG is buffered as. The interval is the key-on note against the
       record's centre note, plus the record's shift fine tune and the
       key-on's fine, both treated as 1/128-semitone units. T34
       simplification: real libsnd's exact conversion is not reconstructed;
       the shortcut is at most a twentieth of a tone off near the switches
       the game's one-shots sit inside. */
    semitones = (double)(note - toneRec->center);
    semitones += (double)(((signed char)toneRec->shift) + fine) / 128.0;
    pitch = 4096.0 * exp2(semitones / 12.0);
    if (pitch < 64.0) {
        pitch = 64.0;
    }
    if (pitch > 65535.0) {
        pitch = 65535.0;
    }
    out->pitch = (unsigned short)pitch;

    /* volume: the key-on volumes are 0..127 (the game passes SOUND_VOLUME_MAX
       = 127 on both sides), scaled by the record's own tone volume; PsyX's
       gain is a 16384-fraction, so the SPU value is fvol*tvol/127 << 7 */
    fvol = ((long)volL + (long)volR) / 2;
    if (fvol < 0) {
        fvol = 0;
    }
    if (fvol > 127) {
        fvol = 127;
    }
    total = fvol * (long)toneRec->vol / 127;
    /* A6: then the program's own volume, as retail _SsVmSetVol does */
    total = total * (long)vab->progVol[prog] / 127;
    /* O4: then the VAB header's master volume, which retail multiplies in
       right after the velocity (libsnd_vm_vol.s:142-153, `lbu 0x18` of the
       vab's header): linear here, squared with the rest at the last step */
    total = (total * (long)vab->mvol / 127) << 7;

    /* panning, the PSX convention (0 = left, 64 = centre, 127 = right):
       the centre keeps the two halves equal. Retail applies the tone's pan,
       then the program's (A6), each as a step down of one side. */
    volLr = total;
    volRr = total;
    for (i = 0; i < 2; i++) {
        pan = i == 0 ? toneRec->pan : vab->progPan[prog];
        if (pan <= 64) {
            volRr = volRr * pan / 64;
        } else {
            volLr = volLr * (127 - pan) / 63;
        }
    }
    /* A6b: retail's last step squares each side (libsnd_vm_vol.s
       28408..28444: mult a2,a2 / mult a1,a1, then * 0x40011 >> 13 = /16384),
       so the loudness curve is quadratic in every factor above and the quiet
       voices of a mix (echo taps, soft programs) fall faster than loud ones */
    volLr = volLr * volLr / 16384;
    volRr = volRr * volRr / 16384;
    out->volL = (short)volLr;
    out->volR = (short)volRr;
    return 0;
}

/* Every tone a SEP key-on plays: retail _SsVmSelectToneAndVag
   (libsnd_vm_stav.s) walks the program's tone pages and collects EVERY tone
   whose [min, max] range covers the note and whose VAG exists; _SsVmKeyOn
   then starts one voice per collected tone. Writes up to `cap` tone indices
   in page order and returns how many matched (0 when none, or when the VAB
   or program is unknown). The registry is snd_spu.c's own, nothing
   duplicated. */
int sndPickTones(short vabId, short prog, short note, short *tones, int cap) {
    SndVab *vab = sndVab(vabId);
    SndTone *toneRec;
    long rank;
    short i;
    int count = 0;

    if (vab == NULL || !vab->used || prog < 0 || prog >= 128) {
        return 0;
    }
    rank = vab->rank[prog];
    if (rank < 0 || rank >= vab->progs) {
        return 0;
    }
    for (i = 0; i < VAB_PAGE_TONES && count < cap; i++) {
        toneRec = &vab->tone[rank][i];
        if (toneRec->vag < 1 || toneRec->vag > vab->vagCount ||
            vab->vagSize[toneRec->vag] == 0) {
            continue;
        }
        if (toneRec->min <= note && note <= toneRec->max) {
            tones[count++] = i;
        }
    }
    return count;
}

/* The first matching tone, or -1: kept for callers that want one tone. */
short sndPickTone(short vabId, short prog, short note) {
    short tone;

    return sndPickTones(vabId, prog, note, &tone, 1) > 0 ? tone : -1;
}

/* A5b: voice allocation as retail _SsVmAlloc (libsnd_vm_aloc1.s), picking for
   a new note of priority `prior`. Verified from the asm:
   - the voices are scanned in index order; the first FREE one wins at once
     (not keyed, +0x1D == 0, and its SPU envelope level +0x06 == 0). The
     reserved-voice mask D_8005B818 (SsSetReservedVoice) is skipped; the shim
     has no reserved voices.
   - otherwise a busy voice is a steal candidate only when its priority is <=
     the best so far (the bar starts at the new note's own priority, so a
     voice of higher priority is never taken). A strictly lower priority
     replaces the candidate and lowers the bar; an equal priority replaces it
     when its envelope level is lower, or equal with a larger allocation age
     (+0x02, a count of key-ons since it started, not time).
   - with no free voice and no candidate it returns the voice count: the key-on
     is refused (_SsVmKeyOn and SsUtKeyOn then take their failure path).
   - on success every non-reserved voice's age++ and the chosen voice's age,
     +0x2A and +0x1E are zeroed, its priority set to the new one.
   Inferred: "keyed" is the shim's `live`, and a releasing voice (still
   audible) is a busy one, ranked only by its envelope level. Returns -1 on a
   refusal. */
static short sndFindVoice(signed char prior) {
    short i;
    short pick = -1;
    int bar = prior;
    int bestLevel = 0xFFFF;
    int bestAge = 0;

    for (i = 0; i < SND_VOICE_COUNT; i++) {
        if (!sndVoiceInfo[i].live) {
            pick = i;
            break;
        }
        if (sndVoiceInfo[i].prior < bar) {
            bar = sndVoiceInfo[i].prior;
            pick = i;
            bestLevel = sndVoiceInfo[i].env.level;
            bestAge = sndVoiceInfo[i].allocAge;
        } else if (sndVoiceInfo[i].prior == bar) {
            int level = sndVoiceInfo[i].env.level;

            if (level < bestLevel) {
                pick = i;
                bestLevel = level;
                bestAge = sndVoiceInfo[i].allocAge;
            } else if (level == bestLevel &&
                       bestAge < sndVoiceInfo[i].allocAge) {
                pick = i;
                bestAge = sndVoiceInfo[i].allocAge;
            }
        }
    }
    if (pick < 0) {
        SND_LOG("voice refused prior=%d: every voice is busier\n", (int)prior);
        return -1;
    }
    if (sndVoiceInfo[pick].live) {
        SND_LOG("voice steal voice=%d prior=%d <- prior=%d level=%d\n", pick,
                (int)sndVoiceInfo[pick].prior, (int)prior, bestLevel);
    }
    for (i = 0; i < SND_VOICE_COUNT; i++) {
        sndVoiceInfo[i].allocAge++;
    }
    sndVoiceInfo[pick].allocAge = 0;
    return pick;
}

/* --- the SPU ADSR envelope (A4) ---------------------------------------------
   psx-spx "SPU ADSR Envelope", reimplemented from the spec. Every phase is a
   (shift, step, direction, mode) rate:
     cycles = 1 << max(0, shift - 11); step <<= max(0, 11 - shift)
     increase steps are +7..+4 (7 - n), decrease steps -8..-5 (-8 + n)
     exponential decrease: step = step * level >> 15
     exponential increase above 0x6000: cycles *= 4
   and the level moves by `step` once every `cycles` samples, clamped to
   0..0x7FFF. Attack ends at the maximum, decay (always exponential decrease,
   step -8) ends at the sustain level, release (step -8, own mode and shift)
   ends at 0. A decreasing sustain that reaches 0 also ends the voice. */

void sndEnvInit(SndEnv *env, unsigned short adsr1, unsigned short adsr2) {
    env->adsr1 = adsr1;
    env->adsr2 = adsr2;
    env->phase = SND_ENV_ATTACK;
    env->level = 0;
    env->counter = 0;
}

void sndEnvKeyOff(SndEnv *env) {
    if (env->phase != SND_ENV_DONE) {
        env->phase = SND_ENV_RELEASE;
        env->counter = 0;
    }
}

int sndEnvStep(SndEnv *env) {
    int shift, step, exp, dec, cycles, sl;

    switch (env->phase) {
    case SND_ENV_ATTACK:
        shift = (env->adsr1 >> 10) & 31;
        step = 7 - ((env->adsr1 >> 8) & 3);
        exp = (env->adsr1 >> 15) & 1;
        dec = 0;
        break;
    case SND_ENV_DECAY:
        shift = (env->adsr1 >> 4) & 15;
        step = -8;
        exp = 1;
        dec = 1;
        break;
    case SND_ENV_SUSTAIN:
        shift = (env->adsr2 >> 8) & 31;
        dec = (env->adsr2 >> 14) & 1;
        step = dec ? -8 + ((env->adsr2 >> 6) & 3) : 7 - ((env->adsr2 >> 6) & 3);
        exp = (env->adsr2 >> 15) & 1;
        break;
    case SND_ENV_RELEASE:
        shift = env->adsr2 & 31;
        step = -8;
        exp = (env->adsr2 >> 5) & 1;
        dec = 1;
        break;
    default:
        return env->level;
    }

    cycles = 1 << (shift > 11 ? shift - 11 : 0);
    step *= 1 << (shift < 11 ? 11 - shift : 0);
    if (exp && !dec && env->level > 0x6000) {
        cycles *= 4;
    }
    if (++env->counter < cycles) {
        return env->level;
    }
    env->counter = 0;
    if (exp && dec) {
        step = (step * env->level) >> 15; /* arithmetic shift, like the SPU */
    }
    env->level += step;
    if (env->level < 0) {
        env->level = 0;
    }
    if (env->level > SND_ENV_MAX) {
        env->level = SND_ENV_MAX;
    }

    sl = ((env->adsr1 & 15) + 1) * 0x800;
    switch (env->phase) {
    case SND_ENV_ATTACK:
        if (env->level >= SND_ENV_MAX) {
            env->phase = SND_ENV_DECAY;
            env->counter = 0;
        }
        break;
    case SND_ENV_DECAY:
        if (env->level <= sl) {
            env->phase = SND_ENV_SUSTAIN;
            env->counter = 0;
        }
        break;
    case SND_ENV_SUSTAIN:
        if (dec && env->level == 0) {
            env->phase = SND_ENV_DONE;
        }
        break;
    case SND_ENV_RELEASE:
        if (env->level == 0) {
            env->phase = SND_ENV_DONE;
        }
        break;
    }
    return env->level;
}

static const char *const sndEnvPhaseName[] = { "attack", "decay", "sustain",
                                               "release", "done" };

/* O4: the music/effect class of a key-on source, and the volumes a live voice
   is sent to the mixer with: the key-on volumes (the retail chain, A6/A6b)
   scaled by the envelope level and, last of all, by the player's sliders
   (master * the voice's class); 100 % leaves the retail level as it was. */
int sndSrcIsMusic(int src) {
    return src == 1;
}

int sndVoiceIsMusic(short voice) {
    if (voice < 0 || voice >= SND_VOICE_COUNT || !sndVoiceInfo[voice].live) {
        return -1;
    }
    return sndSrcIsMusic(sndVoiceInfo[voice].src);
}

int sndVoiceOutVol(short voice, short *left, short *right) {
    long gain, level;

    if (voice < 0 || voice >= SND_VOICE_COUNT || !sndVoiceInfo[voice].live) {
        *left = 0;
        *right = 0;
        return 0;
    }
    gain = sndUserGainPermille(sndSrcIsMusic(sndVoiceInfo[voice].src) ? SND_VOL_MUSIC : SND_VOL_EFFECTS);
    level = sndVoiceInfo[voice].env.level;
    *left = (short)((long)sndVoiceInfo[voice].baseL * level / SND_ENV_MAX * gain / 1000);
    *right = (short)((long)sndVoiceInfo[voice].baseR * level / SND_ENV_MAX * gain / 1000);
    return 1;
}

/* the AL gain follows the envelope: the key-on volumes scaled by the level */
static void sndEnvApplyGain(short voice) {
    short l, r;

    sndVoiceOutVol(voice, &l, &r);
    sndVoiceInfo[voice].appliedLevel = sndVoiceInfo[voice].env.level;
    sndVoiceInfo[voice].appliedGen = sndVolGeneration();
    if (audioModPcmActive(voice)) {
        audioModPcmVolume(voice, l, r);
    } else if (!sndMuted) {
        SpuSetVoiceVolume(voice, l, r);
    }
}

/* the voice's envelope is over (or the sound is cut): stop it and free it */
static void sndVoiceFree(short voice) {
    if (audioModPcmStop(voice)) {
        /* a PCM voice: stopped there, PsyX's voice of the same number was never on */
    } else if (!sndMuted) {
        SpuSetKey(SPU_OFF, (unsigned int)SPU_VOICECH(voice));
    }
    sndVoiceUsed[voice] = 0;
    sndVoiceInfo[voice].live = 0;
}

void sndEnvAdvance(unsigned long samples) {
    short v;
    unsigned long n;

    pthread_mutex_lock(&sndVoiceLock);
    for (v = 0; v < SND_VOICE_COUNT; v++) {
        if (!sndVoiceInfo[v].live) {
            continue;
        }
        sndVoiceInfo[v].age += samples;
        if (sndVoiceInfo[v].endSamples) {
            sndVoiceInfo[v].pos += (double)samples * sndVoiceInfo[v].pitch / 4096.0;
            if (sndVoiceInfo[v].pos >= (double)sndVoiceInfo[v].endSamples) {
                SND_LOG("sample end voice=%d vab=%d prog=%d tone=%d note=%d "
                        "src=%s age=%lu samples\n", v, sndVoiceInfo[v].vabId,
                        sndVoiceInfo[v].prog, sndVoiceInfo[v].tone,
                        sndVoiceInfo[v].note,
                        sndVoiceInfo[v].src ? "sep" : "ut", sndVoiceInfo[v].age);
                sndVoiceFree(v);
                continue;
            }
        }
        if (!sndEnvOff) {
            for (n = 0; n < samples &&
                        sndVoiceInfo[v].env.phase != SND_ENV_DONE; n++) {
                sndEnvStep(&sndVoiceInfo[v].env);
            }
            if (sndVoiceInfo[v].env.phase == SND_ENV_DONE) {
                SND_LOG("env end voice=%d vab=%d prog=%d tone=%d note=%d src=%s "
                        "age=%lu samples\n", v, sndVoiceInfo[v].vabId,
                        sndVoiceInfo[v].prog, sndVoiceInfo[v].tone,
                        sndVoiceInfo[v].note,
                        sndVoiceInfo[v].src ? "sep" : "ut", sndVoiceInfo[v].age);
                sndVoiceFree(v);
                continue;
            }
            if (sndVoiceInfo[v].env.level != sndVoiceInfo[v].appliedLevel ||
                sndVoiceInfo[v].appliedGen != sndVolGeneration()) {
                sndEnvApplyGain(v);
            }
        } else if (sndVoiceInfo[v].appliedGen != sndVolGeneration()) {
            sndEnvApplyGain(v); /* DW3_SND_NOENV: the sliders still move the voice */
        }
        /* a voice still sounding after 10 s is a candidate stuck sound */
        if (!sndVoiceInfo[v].warned &&
            sndVoiceInfo[v].age > 10UL * SND_ENV_SAMPLE_RATE) {
            sndVoiceInfo[v].warned = 1;
            SND_LOG("WARN voice=%d held >10 s: %s vab=%d prog=%d tone=%d note=%d "
                    "src=%s adsr=%04x/%04x phase=%s level=%d\n", v,
                    sndVoiceInfo[v].looping ? "looping sample" : "one-shot sample",
                    sndVoiceInfo[v].vabId, sndVoiceInfo[v].prog,
                    sndVoiceInfo[v].tone, sndVoiceInfo[v].note,
                    sndVoiceInfo[v].src ? "sep" : "ut",
                    sndVoiceInfo[v].env.adsr1, sndVoiceInfo[v].env.adsr2,
                    sndEnvPhaseName[sndVoiceInfo[v].env.phase],
                    sndVoiceInfo[v].env.level);
        }
    }
    pthread_mutex_unlock(&sndVoiceLock);
}

void sndEnvTick(void) {
    sndEnvAdvance((unsigned long)sndEnvTickSamples());
}

int sndEnvVoiceLive(short voice) {
    return voice >= 0 && voice < SND_VOICE_COUNT && sndVoiceInfo[voice].live;
}

unsigned long sndVoiceSerial(short voice) {
    unsigned long serial = 0;

    if (voice >= 0 && voice < SND_VOICE_COUNT) {
        pthread_mutex_lock(&sndVoiceLock);
        serial = sndVoiceInfo[voice].live ? sndVoiceInfo[voice].serial : 0;
        pthread_mutex_unlock(&sndVoiceLock);
    }
    return serial;
}

int sndEnvVoiceLevel(short voice) {
    return voice >= 0 && voice < SND_VOICE_COUNT ? sndVoiceInfo[voice].env.level
                                                 : 0;
}

short sndKeyOnSrc(short vabId, short prog, short tone, short note, short fine,
                  short volL, short volR, int src) {
    struct SndNote snd;
    SpuVoiceAttr attr;
    SndVab *vab;
    SndTone *toneRec;
    short voice;
    int i;

    if (sndNotePrepare(vabId, prog, tone, note, fine, volL, volR, &snd) != 0) {
        return -1;
    }
    vab = sndVab(vabId);
    toneRec = &vab->tone[vab->rank[prog]][tone];

    pthread_mutex_lock(&sndVoiceLock);
    voice = sndFindVoice(toneRec->prior);
    if (voice < 0) {
        pthread_mutex_unlock(&sndVoiceLock);
        return -1;
    }
    sndVoiceUsed[voice] = 1;
    sndVoiceInfo[voice].live = 1;
    sndVoiceInfo[voice].vabId = vabId;
    sndVoiceInfo[voice].prog = prog;
    sndVoiceInfo[voice].tone = tone;
    sndVoiceInfo[voice].note = note;
    sndVoiceInfo[voice].src = (char)src;
    sndVoiceInfo[voice].baseL = snd.volL;
    sndVoiceInfo[voice].baseR = snd.volR;
    sndVoiceInfo[voice].age = 0;
    sndVoiceInfo[voice].prior = toneRec->prior;
    sndVoiceInfo[voice].serial = sndSerialNext++;
    sndVoiceInfo[voice].warned = 0;
    sndVoiceInfo[voice].looping = vab->vagLoops[toneRec->vag];
    sndVoiceInfo[voice].endSamples = vab->vagEnd[toneRec->vag];
    sndVoiceInfo[voice].pos = 0;
    sndVoiceInfo[voice].pitch = snd.pitch;
    sndEnvInit(&sndVoiceInfo[voice].env, toneRec->adsr1, toneRec->adsr2);
    /* The tick only moves the envelope every 1/rate s (20 ms at 50 Hz, 16.7 ms
       at 60), so the voice starts half a tick into it: a fast attack is not
       silent for its first tick. */
    for (i = 0; !sndEnvOff && i < sndEnvTickSamples() / 2; i++) {
        sndEnvStep(&sndVoiceInfo[voice].env);
    }
    if (sndEnvOff) {
        sndVoiceInfo[voice].env.level = SND_ENV_MAX;
    }
    sndVoiceInfo[voice].appliedLevel = sndVoiceInfo[voice].env.level;
    sndVoiceInfo[voice].appliedGen = sndVolGeneration();
    SND_LOG("env start voice=%d vab=%d prog=%d tone=%d note=%d src=%s adsr1=%04x "
            "adsr2=%04x loop=%d\n", voice, vabId, prog, tone, note,
            src == 2 ? "sep-fx" : (src ? "sep" : "ut"), toneRec->adsr1, toneRec->adsr2,
            sndVoiceInfo[voice].looping);

    /* The sound engine still runs whole when muted: the voice is booked, the
       SEP state machine gets a real voice id and key-offs stay symmetric.
       Only the actual key-on is dropped, so no OpenAL source plays and no
       stream is created for the voice. */
    if (sndMuted) {
        SND_LOG("keyOn (muted) id=%d prog=%d tone=%d note=%d -> voice=%d\n",
                vabId, prog, tone, note, voice);
        pthread_mutex_unlock(&sndVoiceLock);
        return voice;
    }

    if (snd.addr >= SND_PCM_BASE && sndPcmBuf[vab->slot][toneRec->vag] != 0) {
        /* a replaced sample: PCM voice, same pitch/volume/pan as PsyX's voice would have (the voice may have held one of PsyX's) */
        short l, r;

        SpuSetKey(SPU_OFF, (unsigned int)SPU_VOICECH(voice));
        sndVoiceOutVol(voice, &l, &r);
        audioModPcmPlay(voice, sndPcmBuf[vab->slot][toneRec->vag], sndVoiceInfo[voice].looping, snd.pitch, l, r);
        SND_LOG("keyOn id=%d prog=%d tone=%d note=%d -> voice=%d vag=%d addr=%#lx (pcm) "
                "pitch=%#x vol=%d/%d\n",
                vabId, prog, tone, note, voice, toneRec->vag, snd.addr, snd.pitch, snd.volL, snd.volR);
        pthread_mutex_unlock(&sndVoiceLock);
        return voice;
    }
    audioModPcmStop(voice); /* the voice held a PCM sample: PsyX's key-on takes it over */
    memset(&attr, 0, sizeof(attr));
    attr.voice = (unsigned int)SPU_VOICECH(voice);
    attr.mask = SPU_VOICE_WDSA | SPU_VOICE_LSAX | SPU_VOICE_VOLL |
                SPU_VOICE_VOLR | SPU_VOICE_PITCH;
    attr.addr = snd.addr;
    attr.loop_addr = snd.loopAddr;
    sndVoiceOutVol(voice, &attr.volume.left, &attr.volume.right);
    attr.pitch = snd.pitch;
    /* PsyX_SPUAL's own upload+play path: the record feeds the voice's AL
       buffer out of SPU RAM, and the key-on starts that OpenAL source */
    SpuSetKeyOnWithAttr(&attr);
    SND_LOG("keyOn id=%d prog=%d tone=%d note=%d -> voice=%d vag=%d addr=%#lx "
            "pitch=%#x vol=%d/%d\n",
            vabId, prog, tone, note, voice, toneRec->vag, snd.addr, snd.pitch,
            snd.volL, snd.volR);
    pthread_mutex_unlock(&sndVoiceLock);
    return voice;
}

short sndKeyOn(short vabId, short prog, short tone, short note, short fine,
               short volL, short volR) {
    return sndKeyOnSrc(vabId, prog, tone, note, fine, volL, volR, 0);
}

/* A key-off starts the release (retail: the SPU key-off, the envelope's own
   release rate then fades the voice and the SPU ends it at level 0). */
short sndKeyOff(short voice, short vabId, short prog, short tone, short note) {
    (void)vabId;
    (void)prog;
    (void)tone;
    (void)note;
    if (voice < 0 || voice >= SND_VOICE_COUNT) {
        return 0;
    }
    pthread_mutex_lock(&sndVoiceLock);
    if (!sndVoiceInfo[voice].live) {
        /* already ended by its envelope */
        pthread_mutex_unlock(&sndVoiceLock);
        return 0;
    }
    if (sndEnvOff) {
        sndVoiceFree(voice);
    } else {
        sndEnvKeyOff(&sndVoiceInfo[voice].env);
    }
    pthread_mutex_unlock(&sndVoiceLock);
    return 0;
}

/* --- pitch bend (A3) -------------------------------------------------------
   Retail _SsSetPitchBend (libsnd_midibend.s) takes ONE data byte of the 0xEn
   event -- the dispatcher (_SsGetSeqData, libsnd_midiread.s:.L80030134) skips
   the first (LSB) -- and _SsVmPitchBend/_SsVmPBVoice (libsnd_vm_pb.s) re-pitch
   every voice of the sequence+program that is ringing. Centre is 0x40.
   Above: note' = note + t * pbUp / 63, fine = (t * pbUp % 63) * 2.
   Below: v = t * pbDown / 64 (toward zero), note' = note + v - 1,
   fine = (t * pbDown - v * 64) * 2 + 127. The pitch is then note2pitch2 =
   SsPitchFromNote(note', fine, tone centre, tone shift). */
void sndBendNote(unsigned char msb, unsigned char pbUp, unsigned char pbDown,
                 short note, short *outNote, short *outFine) {
    int t = (int)msb - 0x40;
    int a0, q;

    if (t > 0) {
        a0 = t * (int)pbUp;
        q = a0 / 63;
        *outNote = (short)(note + q);
        *outFine = (short)((a0 - q * 63) * 2);
    } else if (t < 0) {
        a0 = t * (int)pbDown;
        q = a0 / 64;
        *outNote = (short)(note + q - 1);
        *outFine = (short)((a0 - q * 64) * 2 + 127);
    } else {
        *outNote = note;
        *outFine = 0;
    }
}

int sndBendPitch(short vabId, short prog, short tone, short note,
                 unsigned char msb, unsigned short *pitch) {
    SndVab *vab = sndVab(vabId);
    struct SndNote snd;
    short bn, bf;

    if (vab == NULL || !vab->used || prog < 0 || prog >= 128 || tone < 0 ||
        tone >= VAB_PAGE_TONES || vab->rank[prog] < 0 ||
        vab->rank[prog] >= vab->progs) {
        return -1;
    }
    sndBendNote(msb, vab->tone[vab->rank[prog]][tone].pbUp,
                vab->tone[vab->rank[prog]][tone].pbDown, note, &bn, &bf);
    if (sndNotePrepare(vabId, prog, tone, bn, bf, 0, 0, &snd) != 0) {
        return -1;
    }
    *pitch = snd.pitch;
    return 0;
}

void sndVoiceBend(short voice, unsigned char msb) {
    unsigned short pitch;

    if (voice < 0 || voice >= SND_VOICE_COUNT || !sndVoiceInfo[voice].live) {
        return;
    }
    if (sndBendPitch(sndVoiceInfo[voice].vabId, sndVoiceInfo[voice].prog,
                     sndVoiceInfo[voice].tone, sndVoiceInfo[voice].note, msb,
                     &pitch) != 0) {
        return;
    }
    SND_LOG("bend voice=%d note=%d msb=%#x -> pitch=%#x\n", voice,
            sndVoiceInfo[voice].note, msb, pitch);
    sndVoiceInfo[voice].pitch = pitch;
    if (audioModPcmActive(voice)) {
        audioModPcmPitch(voice, pitch);
    } else if (!sndMuted) {
        SpuSetVoicePitch(voice, pitch);
    }
}

void sndAllKeyOff(short mode) {
    (void)mode;
    pthread_mutex_lock(&sndVoiceLock);
    audioModPcmStopAll();
    SpuSetKey(SPU_OFF, 0xFFFFFF);
    memset(sndVoiceUsed, 0, sizeof(sndVoiceUsed));
    memset(sndVoiceInfo, 0, sizeof(sndVoiceInfo));
    pthread_mutex_unlock(&sndVoiceLock);
}
