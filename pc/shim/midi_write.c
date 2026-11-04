/* A Standard MIDI File, format 1, in memory (content-export E3b); see midi_write.h */
#include "midi_write.h"

#include <stdlib.h>
#include <string.h>

typedef struct MidiTrack {
    uint8_t *p;
    size_t len, cap;
    unsigned long last; /* tick of the last event */
} MidiTrack;

struct MidiFile {
    int division;
    MidiTrack *tr;
    int count, cap;
};

MidiFile *midiCreate(int division) {
    MidiFile *m;

    if (division < 1 || division > 32767) return NULL;
    m = (MidiFile *)calloc(1, sizeof *m);
    if (m != NULL) m->division = division;
    return m;
}

void midiFree(MidiFile *m) {
    int i;

    if (m == NULL) return;
    for (i = 0; i < m->count; i++) free(m->tr[i].p);
    free(m->tr);
    free(m);
}

int midiNewTrack(MidiFile *m) {
    if (m == NULL) return -1;
    if (m->count == m->cap) {
        int cap = m->cap ? m->cap * 2 : 8;
        MidiTrack *t = (MidiTrack *)realloc(m->tr, (size_t)cap * sizeof *t);
        if (t == NULL) return -1;
        m->tr = t;
        m->cap = cap;
    }
    memset(&m->tr[m->count], 0, sizeof(MidiTrack));
    return m->count++;
}

static int reserve(MidiTrack *t, size_t more) {
    if (t->len + more > t->cap) {
        size_t cap = t->cap ? t->cap : 256;
        uint8_t *p;
        while (cap < t->len + more) cap *= 2;
        p = (uint8_t *)realloc(t->p, cap);
        if (p == NULL) return -1;
        t->p = p;
        t->cap = cap;
    }
    return 0;
}

/* A VLQ of up to 4 bytes (28 bits) */
static size_t putVlq(uint8_t *p, unsigned long v) {
    uint8_t tmp[4];
    size_t n = 0, i;

    tmp[n++] = (uint8_t)(v & 0x7F);
    while ((v >>= 7) != 0 && n < 4) tmp[n++] = (uint8_t)((v & 0x7F) | 0x80);
    for (i = 0; i < n; i++) p[i] = tmp[n - 1 - i];
    return n;
}

static MidiTrack *prepare(MidiFile *m, int track, unsigned long tick, size_t bytes) {
    MidiTrack *t;

    if (m == NULL || track < 0 || track >= m->count) return NULL;
    t = &m->tr[track];
    if (tick < t->last || tick - t->last > 0x0FFFFFFFul || reserve(t, bytes + 4) != 0) return NULL;
    t->len += putVlq(t->p + t->len, tick - t->last);
    t->last = tick;
    return t;
}

int midiChannelEvent(MidiFile *m, int track, unsigned long tick, int status, int d1, int d2) {
    MidiTrack *t;
    int one = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0;

    if (status < 0x80 || status >= 0xF0 || d1 < 0 || d1 > 127 || (!one && (d2 < 0 || d2 > 127))) return -1;
    t = prepare(m, track, tick, 3);
    if (t == NULL) return -1;
    t->p[t->len++] = (uint8_t)status;
    t->p[t->len++] = (uint8_t)d1;
    if (!one) t->p[t->len++] = (uint8_t)d2;
    return 0;
}

int midiMeta(MidiFile *m, int track, unsigned long tick, int type, const void *data, size_t len) {
    MidiTrack *t;

    if (type < 0 || type > 127 || len > 0x0FFFFFFFu || (len > 0 && data == NULL)) return -1;
    t = prepare(m, track, tick, 2 + 4 + len);
    if (t == NULL) return -1;
    t->p[t->len++] = 0xFF;
    t->p[t->len++] = (uint8_t)type;
    t->len += putVlq(t->p + t->len, (unsigned long)len);
    if (len > 0) memcpy(t->p + t->len, data, len);
    t->len += len;
    return 0;
}

int midiTempo(MidiFile *m, int track, unsigned long tick, unsigned long usPerQuarter) {
    uint8_t b[3];

    if (usPerQuarter == 0 || usPerQuarter > 0xFFFFFFul) return -1;
    b[0] = (uint8_t)(usPerQuarter >> 16);
    b[1] = (uint8_t)(usPerQuarter >> 8);
    b[2] = (uint8_t)usPerQuarter;
    return midiMeta(m, track, tick, 0x51, b, 3);
}

int midiTimeSignature(MidiFile *m, int track, unsigned long tick, int num, int denPow2) {
    uint8_t b[4];

    if (num < 1 || num > 255 || denPow2 < 0 || denPow2 > 7) return -1;
    b[0] = (uint8_t)num;
    b[1] = (uint8_t)denPow2;
    b[2] = 24;
    b[3] = 8;
    return midiMeta(m, track, tick, 0x58, b, 4);
}

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

int midiFinish(MidiFile *m, unsigned long endTick, uint8_t **out, size_t *len) {
    size_t total = 14;
    uint8_t *buf, *p;
    int i;

    if (m == NULL || out == NULL || len == NULL || m->count < 1 || m->count > 65535) return -1;
    for (i = 0; i < m->count; i++) {
        unsigned long at = endTick > m->tr[i].last ? endTick : m->tr[i].last;
        if (midiMeta(m, i, at, 0x2F, NULL, 0) != 0) return -1;
        total += 8 + m->tr[i].len;
    }
    buf = (uint8_t *)malloc(total);
    if (buf == NULL) return -1;
    memcpy(buf, "MThd", 4);
    be32(buf + 4, 6);
    buf[8] = 0;
    buf[9] = 1; /* format 1 */
    buf[10] = (uint8_t)(m->count >> 8);
    buf[11] = (uint8_t)m->count;
    buf[12] = (uint8_t)(m->division >> 8);
    buf[13] = (uint8_t)m->division;
    p = buf + 14;
    for (i = 0; i < m->count; i++) {
        memcpy(p, "MTrk", 4);
        be32(p + 4, (uint32_t)m->tr[i].len);
        memcpy(p + 8, m->tr[i].p, m->tr[i].len);
        p += 8 + m->tr[i].len;
    }
    *out = buf;
    *len = total;
    return 0;
}
