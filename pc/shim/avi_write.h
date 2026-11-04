#ifndef PC_AVI_WRITE_H
#define PC_AVI_WRITE_H

/*
 * A streaming AVI 1.0 writer (content-export E2a): one MJPEG video stream and, optionally, one 16-bit PCM audio stream,
 * interleaved in the order the caller adds them (the video export adds each frame's audio first, then the frame).
 *
 * Layout: RIFF 'AVI ' { LIST hdrl { avih, LIST strl (vids, 'MJPG', optional OpenDML vprp), LIST strl (auds, PCM) }, LIST movi { 00dc / 01wb chunks },
 * idx1 }. Every chunk is word aligned; idx1 offsets count from the 'movi' fourcc, as players expect; every entry is a key
 * frame (JPEG frames are independent and PCM has no frames). The header is written first with placeholders and rewritten
 * at the end, so the file is written once, front to back, with no big buffer. The bytes depend only on what was added
 * (no clock), so the same input always gives the same file.
 *
 * The vprp chunk (VideoPropHeader, one progressive field) only declares how a player should shape the picture
 * (dwFrameAspectRatio = aspectW << 16 | aspectH); the stored pixels and every other chunk are unchanged.
 *
 * AVI 1.0 holds up to 4 GB; a file that would pass AVI_MAX_BYTES makes the writer fail (OpenDML would be the answer, and the
 * discs' longest movie is nowhere near it). Portable C; the file offsets are `long`.
 */

#include <stddef.h>
#include <stdint.h>

#define AVI_MAX_BYTES (2000L * 1000L * 1000L)

typedef struct AviParams {
    int width, height;
    int fps;            /* video frames per second (integer) */
    int channels;       /* 0: no audio stream; else 1 or 2 */
    int sampleRate;     /* audio samples per second, per channel */
    int aspectW, aspectH; /* the display aspect of the whole frame (e.g. 20:13); 0: undeclared, no vprp chunk */
} AviParams;

typedef struct AviWriter AviWriter;

/* Creates path and writes the header. NULL on failure (bad parameters or the file cannot be created). */
AviWriter *aviOpen(const char *path, const AviParams *p);
/* One JPEG frame ('00dc'). 0, or -1 on a write error or the size limit (the writer is then failed: aviClose reports it). */
int aviAddVideo(AviWriter *w, const uint8_t *jpeg, size_t len);
/* `frames` sample frames (frames * channels interleaved shorts, little-endian on disk) as one '01wb' chunk. */
int aviAddAudio(AviWriter *w, const int16_t *pcm, size_t frames);
/* Writes idx1, patches the sizes and counts and closes the file. 0, or -1 when anything failed since aviOpen. Frees w. */
int aviClose(AviWriter *w);
/* Closes and deletes the file (an export that failed halfway). Frees w. */
void aviAbort(AviWriter *w);
/* Counts so far */
long aviVideoFrames(const AviWriter *w);
long aviAudioFrames(const AviWriter *w);
/* The bytes written so far */
long aviBytes(const AviWriter *w);

#endif
