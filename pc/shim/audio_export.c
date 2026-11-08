/* The sound banks as WAV, MIDI and JSON files (content-export E3a/E3b); see audio_export.h */
#include "audio_export.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "exp_core.h"
#include "exp_job.h"
#include "midi_write.h"
#include "port_log.h"
#include "snd_internal.h"
#include "text_data.h"
#include "wav_write.h"

/* ---------------------------------------------------------------- the ADPCM */

/* The SPU's ADPCM filters, as PsyX_SPUAL's decodeSound (the renderer the game's sound goes through) has them. Filters 5..15 do not
   exist on the hardware; they are read as 4 here (decodeSound reads past its table). */
static const float K0[5] = {0, 0.9375f, 1.796875f, 1.53125f, 1.90625f};
static const float K1[5] = {0, 0, -0.8125f, -0.859375f, -0.9375f};

size_t audioVagDecode(const uint8_t *vag, size_t size, int16_t *out, AudioVagInfo *info) {
    size_t blocks = size / 16, b, k = 0;
    float prev1 = 0.0f, prev2 = 0.0f;
    AudioVagInfo local;
    int seenStart = 0;

    if (info == NULL) info = &local;
    memset(info, 0, sizeof *info);
    for (b = 0; b < blocks; b++) {
        const uint8_t *p = vag + b * 16;
        int shift = p[0] & 0x0F, filter = (p[0] >> 4) & 0x0F, flags = p[1], i;

        if (filter > 4) filter = 4;
        if ((flags & 4) && !seenStart) {
            info->loopStart = k;
            seenStart = 1;
        }
        for (i = 0; i < 28; i++) {
            int nib = (i & 1) ? (p[2 + i / 2] >> 4) & 0xF : p[2 + i / 2] & 0xF, r;
            float t1, t2, t3;

            if (nib > 7) nib -= 16;
            t1 = (float)((double)(float)nib * pow(2, (float)(12 - shift)));
            t2 = prev1 * K0[filter];
            t3 = prev2 * K1[filter];
            prev2 = prev1;
            prev1 = t1 + t2 + t3;
            r = (int)round(prev1);
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            out[k++] = (int16_t)r;
        }
        if (flags & 1) { /* the end flag: the SPU repeats (flag 2) or mutes the voice here */
            info->ends = (flags & 2) ? 2 : 1;
            if (info->ends == 2) info->loopEnd = k - 1;
            else info->loopStart = 0;
            break;
        }
    }
    if (info->ends != 2) info->loopStart = 0;
    info->frames = k;
    return k;
}

/* ---------------------------------------------------------------- text out */

typedef struct Sb {
    char *p;
    size_t len, cap;
    int oom;
} Sb;

static void sbf(Sb *s, const char *fmt, ...) {
    va_list ap;
    int n;
    size_t need;

    if (s->oom) return;
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        s->oom = 1;
        return;
    }
    need = s->len + (size_t)n + 1;
    if (need > s->cap) {
        size_t cap = s->cap ? s->cap : 1024;
        char *p;
        while (cap < need) cap *= 2;
        p = (char *)realloc(s->p, cap);
        if (p == NULL) {
            s->oom = 1;
            return;
        }
        s->p = p;
        s->cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(s->p + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    s->len += (size_t)n;
}

/* A string array: one line each, quotes and backslashes escaped */
static void sbNotes(Sb *s, const char *const *notes, int n) {
    int i;

    sbf(s, "  \"notes\": [\n");
    for (i = 0; i < n; i++) {
        const char *c;
        sbf(s, "    \"");
        for (c = notes[i]; *c; c++) {
            if (*c == '"' || *c == '\\') sbf(s, "\\");
            sbf(s, "%c", *c);
        }
        sbf(s, "\"%s\n", i + 1 < n ? "," : "");
    }
    sbf(s, "  ]\n");
}

static const char *const kBankNotes[] = {
    "Each sample is vag_NNN.wav: mono, 16-bit, 44100 Hz. 44100 Hz is the rate the SPU plays a sample at when its pitch value is 0x1000, "
    "which is the case when the note played is the sample's root note. A sample that no tone uses has the root note 60.",
    "root_note and root_cents: the note (MIDI numbering, 60 is middle C) at which the sample sounds at its recorded pitch, and how many "
    "cents above root_note. A tone's center and shift say it: root = center - shift / 128 semitones (shift is signed, 1/128 of a "
    "semitone a unit; the game plays note N at 4096 * 2^((N - center + shift / 128) / 12) in SPU pitch units, 4096 being 44100 Hz). The "
    "same values are in the smpl chunk of the WAV (MIDIUnityNote, MIDIPitchFraction), taken from the first tone that uses the sample.",
    "end says how a sample ends, from the flags of its ADPCM blocks: loop (end flag with repeat: the SPU jumps back to loop_start, the "
    "block with the loop-start flag or frame 0 without one; loop_end is the last frame of the loop, as in the smpl chunk of the WAV), stop "
    "(end flag without repeat: the voice is muted there) or none (no end flag: all the data is exported).",
    "A tone answers the notes min..max of its program: a key-on of a note plays every tone of the program whose range covers it, one "
    "voice each. Volume and pan are 0..127 (pan 64 is the centre); the program's volume and pan and the bank's master volume and pan "
    "scale them in the game.",
    "adsr1 and adsr2 are the SPU registers as the bank stores them; adsr is the same decoded as psx-spx describes the envelope: the "
    "shift and step of each phase set its speed (a smaller shift and a bigger step is faster), sustain_level is the level (0..32767) "
    "where the decay stops, a step has the sign of the direction, and the modes are linear or exponential.",
    "pitch_bend_down and pitch_bend_up are the range of the pitch bend in semitones.",
    "The samples are decoded with the arithmetic of the port's SPU renderer (a float filter); the integer filter of the console can "
    "differ from it by a few units.",
};

static const char *const kSeqNotes[] = {
    "Each file is one SEP record as the port's player decodes it. package is the SEP package in the bank's order (the SEQ number of a "
    "sound id, bits 8-15) and record the position in the package (the track, bits 0-7).",
    "flat = package * 16 + record: the game numbers the tracks flat across a bank's packages, 16 a package, and a sound id whose SEQ is 0 "
    "and whose track is not a record of package 0 is looked up there.",
    "id is the music id MUSIC(bank, n) = 0x60000000 | bank << 18 | n, with n = record for package 0 and flat for the others; null when "
    "the number n is a record of package 0. id_direct is the same with SEQ = package and track = record: it always resolves.",
    "The MIDI files are format 1: track 0 holds the tempo, the time signature and the loop markers, then one track per channel in use. "
    "The game's loop markers (CC99 = 20, CC6 count, CC99 = 30) become the marker events \"loopStart count=N\" and \"loopEnd\" "
    "(count \"forever\" for 0 and 127) and a CC111 at the loop start on the first channel. At the end of the record the player jumps back "
    "to the loop start, count times or without end.",
    "The sequences name no sounds: the debug sound test that lists them (SOUNDTST) is not part of the port.",
};

/* ---------------------------------------------------------------- archives */

static unsigned long rd32(const uint8_t *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* Entry `index` of an archive (a table of 32-bit offsets, the first entry right after it): its bytes and length (up to the next
   entry or the end of the file). 1 when it is there. */
static int arcEntry(const uint8_t *d, size_t n, unsigned index, const uint8_t **p, size_t *len) {
    unsigned long first, off, end;
    unsigned count, i;

    if (n < 8) return 0;
    first = rd32(d);
    if (first < 4 || (first & 3) || first > n) return 0;
    count = (unsigned)(first / 4);
    if (index >= count) return 0;
    off = rd32(d + (size_t)index * 4);
    if (off >= n) return 0;
    end = n;
    for (i = 0; i < count; i++) {
        unsigned long o = rd32(d + (size_t)i * 4);
        if (o > off && o < end) end = o;
    }
    *p = d + off;
    *len = (size_t)(end - off);
    return 1;
}

/* ---------------------------------------------------------------- one bank */

typedef struct SampleInfo {
    size_t frames;
    int ends;
    size_t loopStart, loopEnd;
    int have;
} SampleInfo;

/* The note a sample sounds at its recorded pitch: center - shift / 128 semitones, as a whole note and a rest in 1/128 */
static void rootOf(int center, int shift, int *note, int *rest128) {
    int total = center * 128 - shift, n = total >= 0 ? total / 128 : -((127 - total) / 128);
    *note = n;
    *rest128 = total - n * 128;
}

static void adsrJson(Sb *s, unsigned a1, unsigned a2) {
    int susLevel = (int)(((a1 & 15) + 1) * 0x800), dec = (int)((a2 >> 14) & 1), raw = (int)((a2 >> 6) & 3);

    if (susLevel > 0x7FFF) susLevel = 0x7FFF;
    sbf(s, "\"adsr\": {\"attack_mode\": \"%s\", \"attack_shift\": %u, \"attack_step\": %d, \"decay_shift\": %u, \"sustain_level\": %d, "
           "\"sustain_mode\": \"%s\", \"sustain_direction\": \"%s\", \"sustain_shift\": %u, \"sustain_step\": %d, "
           "\"release_mode\": \"%s\", \"release_shift\": %u}",
        (a1 >> 15) & 1 ? "exponential" : "linear", (a1 >> 10) & 31, 7 - (int)((a1 >> 8) & 3), (a1 >> 4) & 15, susLevel,
        (a2 >> 15) & 1 ? "exponential" : "linear", dec ? "decrease" : "increase", (a2 >> 8) & 31, dec ? raw - 8 : 7 - raw,
        (a2 >> 5) & 1 ? "exponential" : "linear", a2 & 31);
}

static void fail(char *err, size_t cap, const char *fmt, const char *what) {
    if (err != NULL && cap > 0 && err[0] == '\0') snprintf(err, cap, fmt, what);
}

typedef struct Ctx {
    const AudioExportBank *bk;
    AudioExportPut put;
    void *ud;
    AudioExportStats *st;
    char *err;
    size_t errCap;
} Ctx;

static int putFile(Ctx *c, const char *rel, const void *data, size_t len) {
    if (c->put(c->ud, rel, data, len) != 0) {
        fail(c->err, c->errCap, "%s could not be written", rel);
        return -1;
    }
    c->st->bytes += (long)len;
    return 0;
}

/* the first tone (in program order) that plays VAG v, or NULL */
static const SndTone *firstToneOf(const SndVab *vab, int v) {
    int p, t;

    for (p = 0; p < 128; p++) {
        if (vab->rank[p] < 0 || vab->rank[p] >= vab->progs) continue;
        for (t = 0; t < 16; t++) {
            const SndTone *tn = &vab->tone[vab->rank[p]][t];
            if (tn->vag == v) return tn;
        }
    }
    return NULL;
}

static int exportSamples(Ctx *c, const SndVab *vab, const uint8_t *body, size_t bodyLen, SampleInfo *info) {
    int16_t *pcm = (int16_t *)malloc(65536 * sizeof(int16_t) * 4);
    size_t cap = 65536 * 4;
    int v, rc = 0;

    if (pcm == NULL) return -1;
    for (v = 1; v <= vab->vagCount && !rc; v++) {
        size_t size = vab->vagSize[v], ofs = vab->vagOfs[v], frames;
        AudioVagInfo vi;
        WavSmpl smpl;
        const SndTone *tn;
        uint8_t *wav;
        size_t wavLen;
        char rel[32];

        if (size == 0 || ofs >= bodyLen) continue;
        if (ofs + size > bodyLen) size = bodyLen - ofs;
        if (size / 16 * 28 > cap) {
            int16_t *np;
            cap = size / 16 * 28;
            np = (int16_t *)realloc(pcm, cap * sizeof(int16_t));
            if (np == NULL) {
                rc = -1;
                break;
            }
            pcm = np;
        }
        frames = audioVagDecode(body + ofs, size, pcm, &vi);
        if (frames == 0) continue;
        memset(&smpl, 0, sizeof smpl);
        smpl.rootNote = 60;
        tn = firstToneOf(vab, v);
        if (tn != NULL) {
            int note, rest;
            rootOf(tn->center, (signed char)tn->shift, &note, &rest);
            smpl.rootNote = note < 0 ? 0 : (note > 127 ? 127 : note);
            smpl.fraction = note < 0 || note > 127 ? 0 : (uint32_t)((uint64_t)rest * 0x100000000ull / 128);
        }
        if (vi.ends == 2) {
            smpl.hasLoop = 1;
            smpl.loopStart = (uint32_t)vi.loopStart;
            smpl.loopEnd = (uint32_t)vi.loopEnd;
        }
        wav = wavBuild(pcm, frames, AUDIO_EXPORT_RATE, &smpl, &wavLen);
        if (wav == NULL) {
            rc = -1;
            break;
        }
        snprintf(rel, sizeof rel, "vag_%03d.wav", v);
        rc = putFile(c, rel, wav, wavLen);
        free(wav);
        info[v].frames = frames;
        info[v].ends = vi.ends;
        info[v].loopStart = vi.loopStart;
        info[v].loopEnd = vi.loopEnd;
        info[v].have = 1;
        c->st->vags++;
        if (vi.ends == 2) c->st->loopingVags++;
    }
    free(pcm);
    return rc;
}

static int bankJson(Ctx *c, const SndVab *vab, const uint8_t *vh, const SampleInfo *info) {
    Sb s;
    int v, p, t, first = 1, rc;

    memset(&s, 0, sizeof s);
    sbf(&s, "{\n  \"format\": \"bank-v1\",\n  \"bank\": \"%s\",\n  \"bank_id\": %d,\n  \"sample_rate\": %d,\n", c->bk->name, c->bk->id,
        AUDIO_EXPORT_RATE);
    sbf(&s, "  \"master_volume\": %d,\n  \"master_pan\": %d,\n  \"vag_count\": %d,\n", vh[0x18], vh[0x19], vab->vagCount);
    sbf(&s, "  \"samples\": [\n");
    for (v = 1; v <= vab->vagCount; v++) {
        const SampleInfo *si = &info[v];
        sbf(&s, "    {\"vag\": %d, ", v);
        if (si->have) sbf(&s, "\"file\": \"vag_%03d.wav\", ", v);
        else sbf(&s, "\"file\": null, ");
        sbf(&s, "\"frames\": %lu, \"end\": \"%s\"", (unsigned long)si->frames, si->ends == 2 ? "loop" : (si->ends == 1 ? "stop" : "none"));
        if (si->ends == 2) sbf(&s, ", \"loop_start\": %lu, \"loop_end\": %lu", (unsigned long)si->loopStart, (unsigned long)si->loopEnd);
        sbf(&s, "}%s\n", v < vab->vagCount ? "," : "");
    }
    sbf(&s, "  ],\n  \"programs\": [\n");
    for (p = 0; p < 128; p++) {
        int page = vab->rank[p], any = 0;

        if (page < 0 || page >= vab->progs) continue;
        for (t = 0; t < 16; t++) {
            if (vab->tone[page][t].vag >= 1 && vab->tone[page][t].vag <= vab->vagCount) any = 1;
        }
        if (!any) continue;
        sbf(&s, "%s    {\"program\": %d, \"volume\": %d, \"pan\": %d, \"tones\": [\n", first ? "" : ",\n", p, vab->progVol[p], vab->progPan[p]);
        first = 0;
        {
            int firstTone = 1;
            for (t = 0; t < 16; t++) {
                const SndTone *tn = &vab->tone[page][t];
                int note, rest;

                if (tn->vag < 1 || tn->vag > vab->vagCount) continue;
                rootOf(tn->center, (signed char)tn->shift, &note, &rest);
                sbf(&s, "%s      {\"tone\": %d, \"vag\": %d, \"min\": %d, \"max\": %d, \"center\": %d, \"shift\": %d, \"root_note\": %d, "
                        "\"root_cents\": %.4f, \"volume\": %d, \"pan\": %d, \"priority\": %d, \"pitch_bend_down\": %d, \"pitch_bend_up\": %d, "
                        "\"adsr1\": \"0x%04X\", \"adsr2\": \"0x%04X\", ",
                    firstTone ? "" : ",\n", t, tn->vag, tn->min, tn->max, tn->center, (int)(signed char)tn->shift, note, rest * 100.0 / 128.0, tn->vol,
                    tn->pan, tn->prior, tn->pbDown, tn->pbUp, tn->adsr1, tn->adsr2);
                adsrJson(&s, tn->adsr1, tn->adsr2);
                sbf(&s, "}");
                firstTone = 0;
                c->st->tones++;
            }
            sbf(&s, "\n    ]}");
        }
        c->st->programs++;
    }
    sbf(&s, "\n  ],\n");
    sbNotes(&s, kBankNotes, (int)(sizeof kBankNotes / sizeof kBankNotes[0]));
    sbf(&s, "}\n");
    if (s.oom) {
        free(s.p);
        return -1;
    }
    rc = putFile(c, "bank.json", s.p, s.len);
    free(s.p);
    return rc;
}

/* ---------------------------------------------------------------- the sequences */

typedef struct Cond { /* an event of the conductor track */
    unsigned long tick;
    int kind; /* 0 tempo, 1 loopStart, 2 loopEnd */
    unsigned long value;
} Cond;

/* One SEP record as a Standard MIDI File. seconds/endTick are what the sequences.json says of it; chMask the channels it plays on. */
static int buildMidi(const SndSep *sep, int r, const char *bank, int pkg, uint8_t **out, size_t *len, double *seconds, unsigned long *endTick,
                     unsigned *chMask) {
    const SndSepRec *rc = &sep->recs[r];
    const SndEv *ev = sep->evs + rc->first;
    unsigned long n = rc->count, i, end = 0, lastTick = 0;
    int track[16], ch, firstCh = -1, ok = 0, tc;
    unsigned mask = 0;
    MidiFile *m;
    Cond *cond;
    unsigned long nc = 0;
    double sec = 0.0, tempo = (double)rc->tempo;
    char name[96];
    int loop = rc->loopBody >= 0;
    long body = loop ? rc->loopBody - (long)rc->first : -1;

    for (i = 0; i < n; i++) {
        if (ev[i].tick > end) end = ev[i].tick;
        if (ev[i].kind != SND_EV_TEMPO && ev[i].kind != SND_EV_END) mask |= 1u << (ev[i].ch & 15);
    }
    for (i = 0; i < n; i++) {
        if (ev[i].kind == SND_EV_TEMPO && ev[i].datum >= 1 && ev[i].datum <= 0xFFFFFF) tempo = (double)ev[i].datum;
        if (ev[i].tick > lastTick) {
            sec += (double)(ev[i].tick - lastTick) * tempo / ((double)rc->res * 1e6);
            lastTick = ev[i].tick;
        }
    }
    *seconds = sec;
    *endTick = end;
    *chMask = mask;

    m = midiCreate(rc->res);
    cond = (Cond *)malloc((n + 4) * sizeof *cond);
    if (m == NULL || cond == NULL) goto done;
    tc = midiNewTrack(m);
    for (ch = 0; ch < 16; ch++) {
        track[ch] = -1;
        if (mask & (1u << ch)) {
            track[ch] = midiNewTrack(m);
            if (firstCh < 0) firstCh = ch;
        }
    }
    if (tc < 0) goto done;
    snprintf(name, sizeof name, "%s package %d record %d", bank, pkg, r);
    if (midiMeta(m, tc, 0, 0x03, name, strlen(name)) != 0) goto done;
    if (rc->num > 0 && rc->den <= 7 && midiTimeSignature(m, tc, 0, rc->num, rc->den) != 0) goto done;
    if (rc->tempo >= 1 && rc->tempo <= 0xFFFFFF && midiTempo(m, tc, 0, rc->tempo) != 0) goto done;
    /* the conductor's events in tick order (the tempo changes are in order already; the loop markers slot in by tick) */
    for (i = 0; i < n; i++) {
        if (ev[i].kind == SND_EV_TEMPO) {
            cond[nc].tick = ev[i].tick;
            cond[nc].kind = 0;
            cond[nc].value = ev[i].datum;
            nc++;
        }
    }
    if (loop) {
        unsigned long k;
        Cond c1 = {rc->loopStartTick, 1, 0}, c2 = {rc->loopEndTick, 2, 0};
        for (k = nc; k > 0 && cond[k - 1].tick > c1.tick; k--) cond[k] = cond[k - 1];
        cond[k] = c1;
        nc++;
        for (k = nc; k > 0 && cond[k - 1].tick > c2.tick; k--) cond[k] = cond[k - 1];
        cond[k] = c2;
        nc++;
    }
    for (i = 0; i < nc; i++) {
        char text[40];
        int rv = 0;
        if (cond[i].kind == 0) {
            if (cond[i].value >= 1 && cond[i].value <= 0xFFFFFF) rv = midiTempo(m, tc, cond[i].tick, cond[i].value);
        } else if (cond[i].kind == 1) {
            if (rc->loopCount == 0 || rc->loopCount == 0x7F) snprintf(text, sizeof text, "loopStart count=forever");
            else snprintf(text, sizeof text, "loopStart count=%u", (unsigned)rc->loopCount);
            rv = midiMeta(m, tc, cond[i].tick, 0x06, text, strlen(text));
        } else {
            rv = midiMeta(m, tc, cond[i].tick, 0x06, "loopEnd", 7);
        }
        if (rv != 0) goto done;
    }
    for (i = 0; i < n; i++) {
        const SndEv *e = &ev[i];
        int c = e->ch & 15, rv = 0;

        if (e->kind == SND_EV_TEMPO || e->kind == SND_EV_END) continue;
        if (loop && (long)i == body && firstCh >= 0) midiChannelEvent(m, track[firstCh], rc->loopStartTick, 0xB0 | firstCh, 111, 0);
        switch (e->kind) {
        case SND_EV_KEYON:
            rv = midiChannelEvent(m, track[c], e->tick, 0x90 | c, e->note & 0x7F, e->data & 0x7F);
            break;
        case SND_EV_KEYOFF:
            rv = midiChannelEvent(m, track[c], e->tick, 0x80 | c, e->note & 0x7F, 0x40);
            break;
        case SND_EV_PROG:
            rv = midiChannelEvent(m, track[c], e->tick, 0xC0 | c, e->data & 0x7F, -1);
            break;
        case SND_EV_CTRL:
            rv = midiChannelEvent(m, track[c], e->tick, 0xB0 | c, e->note & 0x7F, e->data & 0x7F);
            break;
        case SND_EV_BEND: /* the player keeps only the MSB; the LSB is 0, so the MSB 0x40 is the centre */
            rv = midiChannelEvent(m, track[c], e->tick, 0xE0 | c, 0, e->data & 0x7F);
            break;
        default:
            break;
        }
        if (rv != 0) goto done;
    }
    ok = midiFinish(m, end, out, len) == 0;
done:
    free(cond);
    midiFree(m);
    return ok ? 0 : -1;
}

/* The packages: the .mid files, and the entries of sequences.json into body (the caller wraps them) */
static int exportSequences(Ctx *c, const uint8_t *head, size_t headLen, Sb *body, int *count) {
    int k, pkg0Recs = 0, rc = 0;
    SndSep *sep = (SndSep *)calloc(1, sizeof *sep);

    if (sep == NULL) return -1;
    for (k = 0; k < c->bk->sepCount && k < AUDIO_EXPORT_MAX_SEPS && !rc; k++) {
        const uint8_t *p;
        size_t len;
        int r;

        if (!arcEntry(head, headLen, (unsigned)c->bk->seps[k] & 0xFFFF, &p, &len) || sndParseSep(p, len, sep) != 0) {
            plogLine("audio", "%s: SEP package %d does not parse, skipped\n", c->bk->name, k);
            continue;
        }
        c->st->packages++;
        if (k == 0) pkg0Recs = sep->recCount;
        for (r = 0; r < sep->recCount && !rc; r++) {
            const SndSepRec *rec = &sep->recs[r];
            uint8_t *mid = NULL;
            size_t midLen = 0;
            double seconds;
            unsigned long endTick;
            unsigned mask;
            char rel[40];
            int flat = k * 16 + r, reachable = k == 0 || flat >= pkg0Recs, ch, firstCh = 1;
            unsigned long idDirect = 0x60000000ul | ((unsigned long)c->bk->id << 18) | ((unsigned long)k << 8) | (unsigned long)r;
            unsigned long idFlat = 0x60000000ul | ((unsigned long)c->bk->id << 18) | (unsigned long)(k == 0 ? r : flat);

            if (buildMidi(sep, r, c->bk->name, k, &mid, &midLen, &seconds, &endTick, &mask) != 0) {
                fail(c->err, c->errCap, "%s could not be built", c->bk->name);
                rc = -1;
                break;
            }
            snprintf(rel, sizeof rel, "seq_%d_%02d.mid", k, r);
            rc = putFile(c, rel, mid, midLen);
            free(mid);
            if (rc) break;
            c->st->sequences++;
            sbf(body, "%s    {\"file\": \"%s\", \"package\": %d, \"record\": %d, \"flat\": %d, ", *count ? ",\n" : "", rel, k, r, flat);
            if (reachable) sbf(body, "\"id\": \"0x%08lX\", ", idFlat);
            else sbf(body, "\"id\": null, ");
            sbf(body, "\"id_direct\": \"0x%08lX\", \"resolution\": %u, \"tempo_us\": %lu, \"bpm\": %.2f, \"time_signature\": \"%u/%u\", ", idDirect,
                (unsigned)rec->res, rec->tempo, rec->tempo ? 60000000.0 / (double)rec->tempo : 0.0, (unsigned)rec->num,
                rec->den <= 15 ? 1u << rec->den : 0u);
            sbf(body, "\"events\": %lu, \"length_ticks\": %lu, \"length_seconds\": %.3f, \"channels\": [", rec->count, endTick, seconds);
            for (ch = 0; ch < 16; ch++) {
                if (mask & (1u << ch)) {
                    sbf(body, "%s%d", firstCh ? "" : ", ", ch);
                    firstCh = 0;
                }
            }
            sbf(body, "], ");
            if (rec->loopBody >= 0) {
                sbf(body, "\"loop\": {\"start_tick\": %lu, \"end_tick\": %lu, \"count\": ", rec->loopStartTick, rec->loopEndTick);
                if (rec->loopCount == 0 || rec->loopCount == 0x7F) sbf(body, "\"forever\"}");
                else sbf(body, "%u}", (unsigned)rec->loopCount);
                c->st->loopingSequences++;
            } else {
                sbf(body, "\"loop\": null");
            }
            sbf(body, "}");
            (*count)++;
        }
        sndSepFree(sep);
    }
    sndSepFree(sep);
    free(sep);
    return rc;
}

/* ---------------------------------------------------------------- the build */

static int buildBank(const AudioExportIo *io, const AudioExportBank *bk, AudioExportPut put, void *putUd, AudioExportStats *st, char *err,
                     size_t errCap) {
    Ctx c;
    uint8_t *head = NULL, *body = NULL;
    size_t headLen = 0, bodyLen = 0, vhLen = 0, vbLen = 0;
    const uint8_t *vh, *vb;
    SndVab *vab = NULL;
    SampleInfo *info = NULL;
    Sb seq;
    int rc = -1, count = 0;

    memset(st, 0, sizeof *st);
    memset(&seq, 0, sizeof seq);
    if (err != NULL && errCap > 0) err[0] = '\0';
    if (io == NULL || io->file == NULL || bk == NULL || put == NULL) {
        fail(err, errCap, "%s", "no disc reader");
        return -1;
    }
    c.bk = bk;
    c.put = put;
    c.ud = putUd;
    c.st = st;
    c.err = err;
    c.errCap = errCap;
    if (!io->file(io->ud, bk->headFile, &head, &headLen)) {
        fail(err, errCap, "%s: the head file cannot be read", bk->name);
        goto out;
    }
    if (!io->file(io->ud, bk->bodyFile, &body, &bodyLen)) {
        fail(err, errCap, "%s: the body file cannot be read", bk->name);
        goto out;
    }
    if (!arcEntry(head, headLen, (unsigned)bk->vhIndex & 0xFFFF, &vh, &vhLen) || vhLen < 0x20 + 0x800) {
        fail(err, errCap, "%s: no VAB header in the head file", bk->name);
        goto out;
    }
    vab = (SndVab *)malloc(sizeof *vab);
    info = (SampleInfo *)calloc(256, sizeof *info);
    if (vab == NULL || info == NULL) {
        fail(err, errCap, "%s", "out of memory");
        goto out;
    }
    if (sndParseVab(vh, vhLen, vab) != 0) {
        fail(err, errCap, "%s: the VAB header does not parse", bk->name);
        goto out;
    }
    if (!arcEntry(body, bodyLen, (unsigned)bk->bodyIndex & 0xFFFF, &vb, &vbLen)) {
        fail(err, errCap, "%s: no VAB body in the body file", bk->name);
        goto out;
    }
    if (exportSamples(&c, vab, vb, vbLen, info) != 0 || bankJson(&c, vab, vh, info) != 0) {
        fail(err, errCap, "%s", "out of memory");
        goto out;
    }
    if (exportSequences(&c, head, headLen, &seq, &count) != 0) {
        fail(err, errCap, "%s", "the sequences could not be written");
        goto out;
    }
    {
        Sb js;
        memset(&js, 0, sizeof js);
        sbf(&js, "{\n  \"format\": \"sequences-v1\",\n  \"bank\": \"%s\",\n  \"bank_id\": %d,\n  \"sequences\": [", bk->name, bk->id);
        if (count > 0) sbf(&js, "\n%s\n  ", seq.p != NULL ? seq.p : "");
        sbf(&js, "],\n");
        sbNotes(&js, kSeqNotes, (int)(sizeof kSeqNotes / sizeof kSeqNotes[0]));
        sbf(&js, "}\n");
        if (js.oom || putFile(&c, "sequences.json", js.p, js.len) != 0) {
            free(js.p);
            fail(err, errCap, "%s", "sequences.json could not be written");
            goto out;
        }
        free(js.p);
    }
    rc = 0;
out:
    free(seq.p);
    free(info);
    free(vab);
    free(head);
    free(body);
    return rc;
}

int audioExportBuild(const AudioExportIo *io, const AudioExportBank *bank, AudioExportPut put, void *putUd, AudioExportStats *st, char *err,
                     size_t errCap) {
    AudioExportStats local;
    return buildBank(io, bank, put, putUd, st != NULL ? st : &local, err, errCap);
}

/* ---------------------------------------------------------------- the job and the data folder */

static char sDataDir[1024];
static AudioExportIo sIo;
static AudioExportBank *sAll; /* every bank the reader knows, filled by a request */
static int sAllCount;
static AudioExportStats sTotals;
static ExpManifest sMan;
static ExpJob sJob; /* the queue holds indexes into sAll */

static void manifestPath(char *out, size_t cap) { snprintf(out, cap, "%s/audio/manifest.json", sDataDir); }

static void manifestLoad(void) {
    char mp[1200];

    if (sMan.loaded) return;
    manifestPath(mp, sizeof mp);
    expManOpen(&sMan, "audio", mp, "audio");
}

static void manifestSave(void) { expManSave(&sMan, 1); }

/* the exporter version of a bank is kept in the manifest header under the bank's name */
static void versionKey(const char *name, char *rel, size_t cap) { snprintf(rel, cap, "%s", name); }

/* written by this version and its bank.json is there (an edited file still counts): nothing to build */
static int upToDate(const char *name) {
    char rel[96], path[1300];

    manifestLoad();
    versionKey(name, rel, sizeof rel);
    if (!expManIsCurrent(&sMan, rel, AUDIO_EXPORT_VERSION)) return 0;
    snprintf(path, sizeof path, "%s/audio/%s/bank.json", sDataDir, name);
    return expFileExists(path);
}

typedef struct PlaceCtx {
    const char *bank;
} PlaceCtx;

/* One file of a bank under the manifest rule of ADR-005 (exp_core.h). 0 to go on. */
static int place(void *ud, const char *file, const void *data, size_t len) {
    const PlaceCtx *pc = (const PlaceCtx *)ud;
    char rel[160], dir[1200];
    ExpSink sink;

    if (*expJobStopping(&sJob)) return 1;
    snprintf(rel, sizeof rel, "%s/%s", pc->bank, file);
    snprintf(dir, sizeof dir, "%s/audio", sDataDir);
    expSinkInPlace(&sink, "audio", dir, &sMan, NULL);
    return expPut(&sink, rel, data, len) ? 1 : 0;
}

/* 1: built, 0: already up to date, -1: failed */
static int exportOne(void *ud, int pos, int index, int force) {
    const AudioExportBank *bk = &sAll[index];
    char dir[1300], err[256], rel[96];
    AudioExportStats st;
    PlaceCtx pc;
    clock_t t0 = clock();

    (void)ud;
    (void)pos;
    manifestLoad();
    if (!force && upToDate(bk->name)) return 0;
    snprintf(dir, sizeof dir, "%s/audio/%s", sDataDir, bk->name);
    if (tdMakeDirs(dir)) {
        plogLine("audio", "cannot create %s\n", dir);
        return -1;
    }
    pc.bank = bk->name;
    if (buildBank(&sIo, bk, place, &pc, &st, err, sizeof err) != 0) {
        manifestSave();
        plogLine("audio", "%s not exported: %s\n", bk->name, err);
        return -1;
    }
    versionKey(bk->name, rel, sizeof rel);
    expManMark(&sMan, rel, AUDIO_EXPORT_VERSION);
    manifestSave();
    expJobLock(&sJob);
    sTotals.vags += st.vags;
    sTotals.programs += st.programs;
    sTotals.tones += st.tones;
    sTotals.packages += st.packages;
    sTotals.sequences += st.sequences;
    sTotals.loopingSequences += st.loopingSequences;
    sTotals.loopingVags += st.loopingVags;
    sTotals.bytes += st.bytes;
    expJobUnlock(&sJob);
    plogLine("audio", "%s: %d samples, %d programs, %d sequences, %ld bytes, %.1f s\n", bk->name, st.vags, st.programs, st.sequences, st.bytes,
             (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 1;
}

static void flushManifest(void *ud) {
    (void)ud;
    manifestSave();
}

static void logDone(void *ud, int done, int failed) {
    (void)ud;
    plogLine("audio", "Done: %d banks in %s/audio, %d failed\n", done, sDataDir, failed);
}

static const ExpJobKind kKind = {"audio", "dw3-audio-export", 1, 0, NULL, exportOne, NULL, flushManifest, logDone, NULL, NULL, NULL, NULL};

void audioExportSetup(const char *dataDir, const AudioExportIo *io) {
    expJobSetup(&sJob, &kKind);
    expManFree(&sMan);
    free(sAll);
    sAll = NULL;
    sAllCount = 0;
    snprintf(sDataDir, sizeof sDataDir, "%s", dataDir != NULL ? dataDir : "");
    if (io != NULL) sIo = *io;
    else memset(&sIo, 0, sizeof sIo);
}

static int nameEq(const char *p, size_t n, const char *name) {
    size_t i;
    if (n != strlen(name)) return 0;
    for (i = 0; i < n; i++) {
        if (toupper((unsigned char)p[i]) != toupper((unsigned char)name[i])) return 0;
    }
    return 1;
}

/* Asks the reader for every bank (the list is the game's table, read on the calling thread) */
static void loadBanks(void) {
    int i;

    free(sAll);
    sAll = (AudioExportBank *)calloc(256, sizeof *sAll);
    sAllCount = 0;
    if (sAll == NULL || sIo.bank == NULL) return;
    for (i = 0; i < 256 && sIo.bank(sIo.ud, i, &sAll[sAllCount]); i++) {
        sAll[sAllCount].name[sizeof sAll[sAllCount].name - 1] = '\0';
        if (sAll[sAllCount].name[0] != '\0') sAllCount++;
    }
}

int audioExportRequest(const char *spec) { return audioExportRequestEx(spec, 0); }

int audioExportRequestEx(const char *spec, int force) {
    int *q, n = 0, i;
    const char *p;

    if (expJobBegin(&sJob, force) < 0) return -1; /* one job at a time: a press while one runs changes nothing */
    memset(&sTotals, 0, sizeof sTotals);
    if (!sDataDir[0] || sIo.bank == NULL || sIo.file == NULL || spec == NULL) return -1;
    loadBanks();
    if (sAllCount == 0) return -1;
    q = (int *)malloc(sizeof(int) * (size_t)sAllCount);
    if (q == NULL) return -1;
    p = spec;
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, "all", 3) && (p[3] == '\0' || p[3] == ' ' || p[3] == ',' || p[3] == '\t')) {
        for (i = 0; i < sAllCount; i++) q[n++] = i;
    } else {
        while (*p) {
            const char *s;
            size_t len;
            int k, j;

            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            if (!*p) break;
            s = p;
            while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
            len = (size_t)(p - s);
            for (k = 0; k < sAllCount; k++) {
                if (nameEq(s, len, sAll[k].name)) break;
            }
            if (k == sAllCount) {
                plogLine("audio", "export_audio: \"%.*s\" is not a bank (use all or names like BGM001 separated by commas)\n", (int)len, s);
                continue;
            }
            for (j = 0; j < n; j++) {
                if (q[j] == k) break;
            }
            if (j == n) q[n++] = k;
        }
    }
    if (n == 0) {
        free(q);
        return -1;
    }
    return expJobQueue(&sJob, q, n);
}

int audioExportRun(const char *spec) {
    int n = audioExportRequest(spec);

    if (n > 0) expJobRun(&sJob);
    return n;
}

int audioExportStart(const char *spec, int force) {
    int n = audioExportRequestEx(spec, force);

    if (n <= 0 || expJobStart(&sJob) < 0) return -1;
    return n;
}

void audioExportShutdown(void) { expJobShutdown(&sJob); }
int audioExportActive(void) { return expJobActive(&sJob); }
void audioExportCounts(int *done, int *failed, int *total) { expJobCounts(&sJob, done, failed, total); }
int audioExportBuilt(void) { return expJobBuilt(&sJob); }
const char *audioExportStatus(void) { return expJobStatus(&sJob); }
int audioExportPump(unsigned budgetMs) { return expJobPump(&sJob, budgetMs); }

void audioExportTotals(AudioExportStats *st) {
    if (st == NULL) return;
    expJobLock(&sJob);
    *st = sTotals;
    expJobUnlock(&sJob);
}
