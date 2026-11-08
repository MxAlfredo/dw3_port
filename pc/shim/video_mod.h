#ifndef PC_VIDEO_MOD_H
#define PC_VIDEO_MOD_H

/*
 * A mod replaces a game movie (content-export E2b, ADR-005 / ADR-010): mods/<mod>/video/<NAME>.avi (NAME is the disc's movie,
 * MOVIED01 .. MOVIEOPN; the port writes it upper case, MOVIED01.avi / MOVIED01.AVI / movied01.avi are looked for) is played in
 * place of AAA/STR/<NAME>.STR and its XA audio. The last active mod that has the file wins. Without such a file nothing here is
 * touched and the movie plays exactly as before.
 *
 * What is accepted (anything else is refused with `[video] <mod>: <NAME>.avi rejected: <why>` and the next mod, then the disc, is
 * tried; the game never stops for a bad file):
 *   - an AVI with an MJPEG video stream (avi_read.h; the profile of the export, E2a, is the model: baseline JPEG, 16-bit PCM) and
 *     optionally 16-bit PCM audio, mono or stereo, at any rate;
 *   - the JPEG frames of jpeg_read.h (yuvj420p / 422p / 444p / grey, DHT or not, restart markers, JFIF or not);
 *   - any picture size: other than 320x416 it is scaled to 320x416 (bilinear, stretched: the game shows that area at 4:3);
 *   - 1 to 60 frames per second (the AVI's own rate sets the pace: frame k is due at (k - 1) / fps seconds of the audio clock);
 *   - audio at any other rate is resampled to the port's 37800 Hz (linear) and mono is made stereo; no audio is silence.
 * The first frame must decode, or the file is refused; a damaged frame later repeats the previous picture (one log line).
 *
 * How it plugs in without touching the game: the movie player (src/menus/title/movie.c) is unchanged. CdlSetloc names the movie by
 * its first sector; videoModArm recognises it and, when a mod replaces it, host_cd.c points the St layer's sector source
 * (videoModReadSector) and the XA driver's PCM source (videoModReadAudio) here instead of at the disc. The St layer then delivers
 * one made-up one-sector record per AVI frame (the usual StHEADER, 320x416, and a marker in the payload); host_press.c sees the
 * marker in DecDCTvlc2 / DecDCTin and puts the AVI's picture in VRAM format in its shadow buffer instead of running the MDEC.
 * The XA clock paces the movie as it always did (frames wait for the audio).
 *
 * The end: the game ends a movie when a delivered record's frameCount reaches the movie's end frame (movie.c). The records here
 * carry frameCount 0 (so a longer AVI than the original is not cut at the original's last frame), and after the last picture one
 * more record follows with frameCount 0xFFFFFFFF: that is the end the game honours, after the last picture was shown. That last
 * record also holds the player until the end of the audio (at most 450 ms past the last frame's slot, then the game goes on).
 * Skipping with Start works as for any movie.
 *
 * Plain C types only: host_press.c and host_cd.c include this next to the game's headers.
 */

#define VIDEO_MOD_W 320
#define VIDEO_MOD_H 416
#define VIDEO_MOD_RATE 37800      /* the XA clock the port paces the movies with */
#define VIDEO_MOD_MAX_HOLD_MS 450 /* how long the end waits for audio that outlasts the last frame */

typedef struct VideoModIo {
    /* the first sector of the disc's movie `name` ("MOVIED01"), 1 when it is on the disc */
    int (*movieLba)(void *ud, const char *name, long *lba);
    void *ud;
} VideoModIo;

/* Where the movies and the mods are. NULL turns replacement off. Safe to call again. */
void videoModSetup(const VideoModIo *io);

/* The movie whose first sector is `lba` (CdlSetloc): 1 when a mod replaces it, with the replacement open and ready (a previous one is
   closed first), 0 when the disc's movie plays. */
int videoModArm(long lba);
/* The replacement is over (CdlPause, or another movie arms): closes it. */
void videoModStop(void);
int videoModActive(void);
/* The mod and the name of the active replacement, "" when none */
const char *videoModCurrentMod(void);
const char *videoModCurrentName(void);
/* The open replacement's video frames, 0 when none */
long videoModFrames(void);

/* The St layer's sector source (host_st.h HostStReadFn): sector `lba` of the replacement, one record per frame, then the end
   record, then 0. */
int videoModReadSector(void *ctx, long lba, unsigned char *sector2048);
/* The XA driver's PCM source (host_xa.h SndXaPcmFn): up to `frames` stereo sample frames at VIDEO_MOD_RATE of the audio, silence
   where the AVI has none, then 0 at the end (a quarter second past the end of the movie, so the last picture never cuts the sound). */
int videoModReadAudio(void *ctx, short *pcm, int frames);

/* host_press.c's side. A record's payload starts with a marker: videoModRecord(payload) is 1 for a record made here, with the
   frame number (1-based; the end record has the last frame's number), the sample frame at which the player must present it
   (`due`), and `isEnd` set for the end record (its `due` is the end of the audio hold). */
int videoModRecord(const void *payload, long *frameNo, long *due, int *isEnd);
/* The picture of frame `frameNo` (1-based) as VRAM words (0bbbbbgggggrrrrr, VIDEO_MOD_W x VIDEO_MOD_H, rows top-down). 1, or 0 when
   no replacement is open or the number is out of range. A frame that does not decode repeats the previous picture. */
int videoModFrame15(long frameNo, unsigned short *out);

/* host_cd.c's side: the replacement's four entry points, installed with pcCdSetVideoMod (host_cd.c) */
typedef struct VideoModHooks {
    int (*arm)(long lba);
    void (*stop)(void);
    int (*readSector)(void *ctx, long lba, unsigned char *sector2048);
    int (*readAudio)(void *ctx, short *pcm, int frames);
} VideoModHooks;
const VideoModHooks *videoModHooks(void);

/* --validate-mod (U5): every video/<NAME>.avi of one mod through the check videoModArm makes (a name of a disc movie, a readable MJPEG AVI, a
   first frame that decodes). Refusals are logged as always. Returns how many files were looked at. */
int videoModValidate(const char *modDir, const char *modName);

#endif
