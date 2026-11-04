#ifndef PC_AVI_READ_H
#define PC_AVI_READ_H

/*
 * An AVI 1.0 reader (content-export E2b): what a mod's replacement movie is read with. It reads what avi_write.c writes and what
 * a modder's ffmpeg writes with `-c:v mjpeg -c:a pcm_s16le`.
 *
 * It does not trust the index: the chunks of the 'movi' list are walked once at open (a 'rec ' list inside it is looked through,
 * JUNK and unknown chunks are skipped, a file cut short keeps the chunks before the cut), so an AVI without idx1 or with a wrong
 * one reads the same. The first video stream and the first audio stream are used; the chunk '<nn>dc' / '<nn>db' is a video frame
 * ('<nn>' is the stream number) and '<nn>wb' audio. A video chunk of zero bytes is a repeated frame (the caller shows the one
 * before). OpenDML extra RIFFs ('AVIX', files over 1 GB) are not read: the first RIFF is the whole movie.
 *
 * Accepted: video whose codec is MJPG / mjpg / AVI1 / avi1 / JPEG / jpeg; audio that is 16-bit PCM (format tag 1, or the
 * extensible tag with 16 bits), mono or stereo, any rate up to AVI_READ_MAX_RATE. Anything else is refused with the reason.
 * Several readers may open the same file (each has its own FILE and index), which is how video and audio are read from two threads.
 * Portable C; a file over 2 GB is refused (the offsets are `long`).
 */

#include <stddef.h>
#include <stdint.h>

#define AVI_READ_MAX_RATE 192000
#define AVI_READ_MAX_FRAMES 1000000L

typedef struct AviInfo {
    int width, height;     /* from the video stream header (the JPEG frames are the truth) */
    long rate, scale;      /* frames per second = rate / scale */
    long videoFrames;      /* video chunks */
    int hasAudio;
    int channels;          /* 1 or 2 */
    int sampleRate;        /* per channel */
    long audioFrames;      /* whole sample frames in all the audio chunks */
} AviInfo;

typedef struct AviReader AviReader;

/* Opens path and builds the index. NULL with the reason in err (may be NULL) when it cannot be read or is not an acceptable AVI. */
AviReader *aviReadOpen(const char *path, AviInfo *info, char *err, size_t errCap);
void aviReadClose(AviReader *r);
const AviInfo *aviReadInfo(const AviReader *r);

/* Bytes of video frame `index` (0-based), or -1 when out of range */
long aviReadVideoSize(const AviReader *r, long index);
/* Copies the frame to buf (cap bytes at most). Its length, or -1 when out of range, unreadable or larger than cap. */
long aviReadVideo(AviReader *r, long index, uint8_t *buf, size_t cap);
/* Sample frames [start, start + frames) as interleaved little-endian 16-bit values into out (frames * channels shorts). Returns the
   sample frames delivered: fewer than asked at the end of the audio (or on a read error), 0 past it. */
long aviReadAudio(AviReader *r, long start, long frames, int16_t *out);

#endif
