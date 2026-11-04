#ifndef PC_MIDI_WRITE_H
#define PC_MIDI_WRITE_H

/*
 * A Standard MIDI File, format 1, in memory (content-export E3b): a header chunk and one MTrk chunk per track. The caller adds events
 * with absolute ticks in non-decreasing order per track; the writer turns them into delta times (VLQ) and appends the end-of-track
 * meta event at the tick given to midiFinish. Status bytes are always written in full (no running status). Pure: no files.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct MidiFile MidiFile;

/* division: ticks per quarter note, 1..32767. NULL when out of memory or division is out of range */
MidiFile *midiCreate(int division);
void midiFree(MidiFile *m);
/* A new track; its index, or -1 */
int midiNewTrack(MidiFile *m);
/* A channel message: status is the whole byte (0x9n, 0xBn...); d2 < 0 for the statuses with one data byte (0xCn, 0xDn).
   0, or -1 when the tick goes back, the bytes are out of range or memory is out. */
int midiChannelEvent(MidiFile *m, int track, unsigned long tick, int status, int d1, int d2);
/* A meta event FF <type> <VLQ length> <data> */
int midiMeta(MidiFile *m, int track, unsigned long tick, int type, const void *data, size_t len);
/* FF 51 03 <usPerQuarter> (up to 0xFFFFFF) */
int midiTempo(MidiFile *m, int track, unsigned long tick, unsigned long usPerQuarter);
/* FF 58 04 <num> <den as a power of two> 24 8 */
int midiTimeSignature(MidiFile *m, int track, unsigned long tick, int num, int denPow2);
/* Ends every track at endTick (or at its last event, when that is later) and returns the file as malloc'ed bytes. 0, or -1. */
int midiFinish(MidiFile *m, unsigned long endTick, uint8_t **out, size_t *len);

#endif
