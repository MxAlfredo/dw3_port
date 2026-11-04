/*
 * Host shim for libpress (T39): the MDEC the game's movie player drives.
 *
 * PsyCross carries the libpress.h header but implements none of it, so the
 * game's own movie player (src/menus/title/movie.c, the title overlay's
 * movies) has to be answered here (ADR-002). The decoder itself is
 * pc/shim/host_mdec.c (T37); this file is the libpress surface over it.
 *
 * The calls, and what each one does:
 *
 *   DecDCTvlcBuild(table)   builds the port's VLC tree. The game's table
 *                           buffer is libpress's 34816-entry layout, which is
 *                           NOT this decoder's node table: the buffer is
 *                           accepted and left untouched, the real table lives
 *                           in the decoder state below.
 *   DecDCTvlc2(bs, buf, t)  Huffman data -> the run-level stream, in the
 *                           game's 0x28000-byte buffer (libpress's layout:
 *                           command word, one token per coefficient).
 *   DecDCTin(buf, mode)     decodes the WHOLE frame once into a 15-bit shadow
 *                           buffer, sized from the last frame the St layer
 *                           delivered. Nothing is decoded when no frame or no
 *                           size is known yet.
 *   DecDCTout(buf, size)    copies `size` words of the shadow into the game's
 *                           staging buffer and then fires the slice callback,
 *                           which chains the next DecDCTout -- 20 slices per
 *                           320x416 frame. On the OUTERMOST return it
 *                           presents the frame.
 *   DecDCToutCallback(func) stores the callback and returns 0.
 *   DecDCTReset(mode)       rebuilds the decoder and invalidates the frame
 *                           (the game calls it before each movie).
 *
 * Why 15bpp (the display decision, T39): PsyCross ignores DISPENV.isrgb24 --
 * external/psycross/src/psx/LIBGPU.C is the only mention and it only clears
 * it -- so the game's 24-bit display path cannot put anything on this port's
 * window. The movie task calls setDisplayMode(320, 480, 1, 1), and the port's
 * own hostSetDisplayMode (shim/gfx_engine.c) shows a 320x480 15-bit area at
 * VRAM (0,0). The picture therefore has to be 15-bit in VRAM, and DecDCTin
 * passes HMDEC_DEPTH_15BPP even though the PS1 passes mode 3 (24bpp).
 *
 * Why the order is what makes the picture right: the game's own per-slice
 * LoadImages paint its 24-bit reconstruction into VRAM -- 20 strips of 24
 * halfwords at x = slice.x (+480 on the frames whose rectid is 1), y = 36.
 * The display is 320 halfwords wide, so a strip either lands outside the shown
 * columns or inside them as 24-bit garbage. The port's frame is presented by
 * LoadImage(0, 36, 320, 416) from the OUTERMOST DecDCTout return, i.e. after
 * the game's last slice LoadImage of that frame, so the last write to the
 * visible area is the whole 15-bit frame. Presenting earlier -- from
 * DecDCTin, or from the outermost call before its callback returns -- would
 * leave a 24-bit strip on top and the picture would come out in bands.
 *
 * How the frame reaches the window: PsyCross does not draw VRAM out (it draws
 * primitives and copies the backbuffer INTO VRAM), so the LoadImage above only
 * makes the frame the texture. The same present hands the rect to the port's
 * movie presenter (hostVideoSetSource, shim/host_video.c), and the port's frame
 * builder draws it as textured quads after the mode's own layers. The source
 * is cleared whenever the frame is invalidated (DecDCTReset, the next
 * DecDCTin), so no stale rect is ever drawn over a menu.
 *
 * Recursion: onSliceDecoded calls DecDCTout for the next slice, so the chain
 * is 20 calls deep (the one movie.c's TASK_RUN makes plus 19 from the
 * callback). That is bounded, but it is guarded anyway: a deeper chain reports
 * an error instead of smashing the stack.
 *
 * The staging copy itself is a formality: the game only forwards those bytes
 * to VRAM regions the port's own present overwrites or the display never
 * shows. The game asks for the frame in 24-bit slices (20 x 4992 words =
 * 320*416*3 bytes) while the shadow holds the same frame at 15bpp (66560
 * words), so the running offset simply advances by what was asked and stops
 * handing out words when the shadow is exhausted; it never reads past it.
 *
 * MDEC_DEBUG=1 prints one line per decoded frame and a summary when the movie
 * tears the callback down (DecDCToutCallback(NULL)); without the variable this
 * file is silent. Same switch style as host_st.c's ST_DEBUG.
 *
 * T40, the movie's pace: the opening movies are 15 fps and their CD-XA audio is
 * 37800 Hz, so video frame N (the StHEADER's 1-based frameCount) is due when
 * the audio stream has played N * 2520 sample frames. Before presenting a
 * frame, pressPaceFrame waits for sndXaPosition (shim/host_xa.h) to reach that
 * target, pumping the XA driver meanwhile (safe: it only decodes while the sink
 * has room). A stalled stream is capped at 500 ms per frame and counted, and a
 * clock that does not move at all turns pacing off instead of turning the
 * movie into a 500 ms-per-frame crawl. With no XA stream the movie plays at
 * decode speed, which is the pre-T40 behaviour. PACING_DEBUG=1 prints one line
 * per presented frame (frame, target, position, waited ms) and a summary at the
 * movie's teardown.
 *
 * E2b, a mod's replacement movie (shim/video_mod.h): the St layer then delivers one made-up record per AVI frame, and its payload
 * starts with a marker. DecDCTvlc2 recognises the marker, skips the VLC decode and leaves a short stub (the marker and the frame
 * number) in the game's buffer; DecDCTin finds the stub and fills the shadow with the AVI's picture (already in VRAM format)
 * instead of running the MDEC. Everything after that is the disc path. The pacer takes the frame's due time from the replacement
 * (the AVI's own frame rate) and holds the end record until the audio is over.
 *
 * StCdIntrFlag and StCdInterrupt live in host_st.c: the host's CD reader is
 * synchronous (pc/shim/host_cd.c) and raises no streaming interrupt, so the
 * flag stays 0.
 */
#include "engine/game.h"

#include "host_mdec.h"
#include "host_st.h"
#include "host_xa.h"
#include "host_video.h"
#include "video_mod.h"

#include <libpress.h>

#include <stdio.h>
#include <time.h>

/* engine/game.h declares its own atoi/random against the PsyQ headers, so
   <stdlib.h> cannot come in here; getenv is all this file needs. */
extern char *getenv(const char *name);

/* The largest frame the shadow accepts (640x480 = the PS1's full screen). A
   stream asking for more is reported and not decoded. */
#define HOST_PRESS_MAX_W     640
#define HOST_PRESS_MAX_H     480

/* A 320x416 frame chains 20 DecDCTout calls; 32 leaves room without letting a
   runaway chain grow the stack. */
#define HOST_PRESS_MAX_DEPTH 32

/* The frame, 15-bit BGR555, as hostMdecDecodeFrame writes it: row-major,
   top-down, two bytes per pixel. Declared as words so LoadImage gets the
   4-byte alignment it needs. */
static u_int pressShadow[HOST_PRESS_MAX_W * HOST_PRESS_MAX_H / 2];

static HostMdec pressMdec;
static int pressMdecReady;

/* The slice callback the game registers (STDWTITL_onSliceDecoded). */
static void (*pressSliceCallback)(void);

/* The frame DecDCTin decoded, valid until the next reset or the next in. */
static int pressFrameValid;
static int pressFrameW;
static int pressFrameH;

/* Words of the shadow handed out by DecDCTout so far, in the game's 24-bit
   word count (see the staging copy note above). */
static long pressSliceOffset;

/* DecDCTout recursion depth: 0 while no call is on the stack, 1 in the
   outermost one. */
static int pressDepth;

static long pressFramesDecoded;
static long pressFramesPresented;
static long pressErrorTotal;

/* --- T40: the movie's clock (see the file comment) ----------------------- */

#define PRESS_SAMPLE_RATE     37800
#define PRESS_FRAME_RATE      15
#define PRESS_FRAME_SAMPLES   (PRESS_SAMPLE_RATE / PRESS_FRAME_RATE) /* 2520 */

/* One wait is capped so a stream that stops producing cannot hang the port. */
#define PRESS_PACE_MAX_MS     500

/* The StHEADER the St layer delivered with the frame being decoded: its
   frameCount at +8, 1-based (host_st.h). Zero until the first frame. */
static unsigned long pressFrameIndex;

/* E2b: the sample frame at which the frame above is due; the disc's movies are 15 fps, frame N at N * 2520 */
static long pressFrameDue;

/* E2b: the stub DecDCTvlc2 leaves for DecDCTin in the game's VLC buffer in place of a run-level stream (the first word is no
   command word the MDEC could produce, whose top nibble is 3) */
#define PRESS_MOD_STUB 0x56335744u /* "DW3V" */

/* Pacing outcome, also reported by the PACING_DEBUG summary. */
static int pressPaceStalled;     /* the clock stopped: pacing switched off  */
static long pressPaceCapped;     /* waits that hit PRESS_PACE_MAX_MS        */
static long pressPaceFrames;     /* frames presented while a clock existed  */
static long pressPaceTotalWait;  /* total ms spent waiting                 */
static long pressPaceMaxWait;    /* longest single wait, ms                */
static unsigned long pressPaceFirstFrame;
static long pressPaceFirstMs;
static unsigned long pressPaceLastFrame;
static long pressPaceLastMs;

/* --- logging (MDEC_DEBUG / PACING_DEBUG only; inert by default) ---------- */

static int pressLogState = -1;

static int pressLogOn(void) {
    if (pressLogState < 0) {
        pressLogState = (getenv("MDEC_DEBUG") != NULL);
    }
    return pressLogState;
}

static int pressPaceLogState = -1;

static int pressPaceLogOn(void) {
    if (pressPaceLogState < 0) {
        pressPaceLogState = (getenv("PACING_DEBUG") != NULL);
    }
    return pressPaceLogState;
}

/* Monotonic milliseconds, for the pacing measurements only. */
static long pressNowMs(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

static void pressSleepMs(long ms) {
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* --- decoder state ------------------------------------------------------- */

/*
 * T39: VRAM's 15-bit format is `0bbbbbgggggrrrrr` (red in bits 0-4), and that
 * is what PsyCross decodes to a colour. hostMdecDecodeFrame's 15bpp output
 * packs blue in the low field instead (`0rrrrrgggggbbbbb`, a PC-style BGR555),
 * so the frame is converted once per decode, in place: without it the presented
 * movie comes out with red and blue exchanged (measured: the red armoured
 * Digimon renders purple, the yellow one cyan, against the decoder's own RGB24
 * PNG). Bit 15 (the mask/STP bit) is preserved. host_mdec.c is the offline
 * decoder's and is left as it is; this is the movie shim's own presentation
 * contract.
 */
static void pressToVram15(u_short *pixels, long count) {
    long i;

    for (i = 0; i < count; i++) {
        u_short v = pixels[i];

        pixels[i] = (u_short)((v & 0x83E0u) | ((v & 0x1Fu) << 10)
                              | ((v >> 10) & 0x1Fu));
    }
}

static void pressInit(void) {
    if (!pressMdecReady) {
        hostMdecInit(&pressMdec);
        pressMdecReady = 1;
    }
}

static void pressInvalidate(void) {
    pressFrameValid = 0;
    pressFrameW = 0;
    pressFrameH = 0;
    pressSliceOffset = 0;
    pressDepth = 0;
    /* T39: no frame means nothing for the presenter to draw (a menu after the
       movie must not get the last movie frame over it). */
    hostVideoDisable();
}

/* --- present ------------------------------------------------------------- */

/*
 * T40: the frame number the pacer anchors on. The St layer (host_st.c) hands
 * DecDCTvlc2 a pointer into its ring record, and the record is
 * [32-byte StHEADER][payload], so the header is 8 words before that pointer;
 * frameCount is its third word (u32 at +8, little-endian like the disc). The
 * id/type are checked before the word is trusted: a buffer that is not an St
 * record (a harness calling libpress directly) falls back to the St layer's
 * own delivered-frame count, which equals frameCount while the stream starts at
 * frame 1, as the game's StSetStream(1, 1, ...) does.
 */
static void pressPaceTo(long target);

static void pressNoteStreamFrame(const u_long *payload) {
    const u_long *header;
    unsigned int id;
    unsigned int type;
    unsigned long count;
    HostStStats stats;

    pressFrameIndex = 0;
    pressFrameDue = 0;
    if (payload != NULL) {
        long modNo;
        long modDue;
        int modEnd;

        if (videoModRecord(payload, &modNo, &modDue, &modEnd)) {
            /* E2b: a replacement's record. Its end record is not presented: the player holds here until the audio is over, and
               the game ends the movie when it reads the record's frameCount. */
            pressFrameIndex = (unsigned long)modNo + (modEnd ? 1u : 0u);
            pressFrameDue = modDue;
            if (modEnd) {
                pressPaceTo(modDue);
            }
            return;
        }
        header = payload - 8;
        id = (unsigned int)(header[0] & 0xFFFFu);
        type = (unsigned int)(header[0] >> 16);
        count = (unsigned long)header[2];
        if (id == 0x0160u && type == 0x8001u && count != 0) {
            pressFrameIndex = count;
            pressFrameDue = (long)count * PRESS_FRAME_SAMPLES;
            return;
        }
    }
    hostStGetStats(&stats);
    if (stats.framesDelivered > 0) {
        pressFrameIndex = (unsigned long)stats.framesDelivered;
        pressFrameDue = (long)pressFrameIndex * PRESS_FRAME_SAMPLES;
    }
    if (pressPaceLogOn()) {
        printf("pacing: no St header at the payload, using the delivered "
               "count (%lu)\n", pressFrameIndex);
    }
}

/*
 * T40: hold this frame until the XA clock reaches its due time, N * 2520 sample
 * frames. The pump runs during the wait (demand-driven, so it only decodes
 * with room in the sink), one millisecond per iteration. A wait is capped at
 * PRESS_PACE_MAX_MS and counted; if the clock makes no progress at all, pacing
 * is switched off for the rest of the movie rather than capping every frame.
 * Never drops a frame: the frame is presented either way.
 */
static void pressPaceTo(long target) {
    long pos;
    long startPos;
    long startMs;
    long waited;
    int logOn = pressPaceLogOn();

    if (pressFrameIndex == 0 || pressPaceStalled) {
        return;
    }
    if (!sndXaActive()) {
        if (logOn) {
            printf("pacing: frame %lu, no xa stream: decode speed\n",
                   pressFrameIndex);
        }
        return;
    }
    pos = sndXaPosition();
    startPos = pos;
    startMs = pressNowMs();
    while (pos < target) {
        waited = pressNowMs() - startMs;
        if (waited >= PRESS_PACE_MAX_MS) {
            pressPaceCapped++;
            if (pos <= startPos) {
                pressPaceStalled = 1;
                if (logOn) {
                    printf("pacing: xa clock stuck at %ld (target %ld), "
                           "pacing off\n", pos, target);
                }
            }
            break;
        }
        sndXaTick();
        pressSleepMs(1);
        pos = sndXaPosition();
    }
    waited = pressNowMs() - startMs;
    if (waited > 0) {
        pressPaceTotalWait += waited;
        if (waited > pressPaceMaxWait) {
            pressPaceMaxWait = waited;
        }
    }
    pressPaceFrames++;
    if (pressPaceFrames == 1) {
        pressPaceFirstFrame = pressFrameIndex;
        pressPaceFirstMs = startMs;
    }
    pressPaceLastFrame = pressFrameIndex;
    pressPaceLastMs = startMs + waited;
    if (logOn) {
        printf("pacing: frame %lu target %ld pos %ld waited %ld ms t=%ld\n",
               pressFrameIndex, target, pos, waited,
               (pressPaceLastMs - pressPaceFirstMs));
    }
}

static void pressPaceFrame(void) {
    pressPaceTo(pressFrameDue);
}

/*
 * The port's own presentation: the whole 15-bit frame at (0, 36), the y the
 * game's slices use. Called from the outermost DecDCTout return, after the
 * game's last slice LoadImage (see the file comment).
 */
static void pressPresentFrame(void) {
    RECT rect;

    if (!pressFrameValid) {
        return;
    }
    /* T40: the frame is decoded; hold it until the audio says it is due. */
    pressPaceFrame();
    rect.x = 0;
    rect.y = 36;
    rect.w = (short)pressFrameW;
    rect.h = (short)pressFrameH;
    DrawSync(0);
    LoadImage(&rect, (u_long *)(void *)pressShadow);
    /* T39: the VRAM rect is now the frame; the port's presenter draws it as
       texture, because PsyCross never draws VRAM to the window (host_video.c). */
    hostVideoSetSource(rect.x, rect.y, pressFrameW, pressFrameH);
    pressFramesPresented++;
}

/* --- the slice staging copy ---------------------------------------------- */

static void pressCopyWords(u_long *dst, long dstWords, const u_int *src,
                           long srcWords) {
    long n = (srcWords < dstWords) ? srcWords : dstWords;
    long i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/* --- libpress ------------------------------------------------------------ */

/*
 * DecDCTReset(mode): the PS1 resets the MDEC here; the port rebuilds the
 * decoder state and drops the frame. `mode` is not used.
 */
void DecDCTReset(int mode) {
    (void)mode;
    hostMdecInit(&pressMdec);
    pressMdecReady = 1;
    pressInvalidate();
    pressFramesDecoded = 0;
    pressFramesPresented = 0;
    pressErrorTotal = 0;
    pressFrameIndex = 0;
    pressPaceStalled = 0;
    pressPaceCapped = 0;
    pressPaceFrames = 0;
    pressPaceTotalWait = 0;
    pressPaceMaxWait = 0;
    pressPaceFirstFrame = 0;
    pressPaceLastFrame = 0;
    pressPaceFirstMs = 0;
    pressPaceLastMs = 0;
    if (pressLogOn()) {
        printf("mdec: reset\n");
    }
}

/*
 * The VLC-table side. The game's buffer is libpress's table layout, not the
 * port's node table, so it is accepted and never written.
 */
void DecDCTvlcBuild(u_short *table) {
    (void)table;
    pressInit();
}

int DecDCTvlc2(u_long *bs, u_long *buf, u_short *table) {
    int result;

    (void)table;
    pressInit();
    /* T40: the frame's own number, for the video pacer. */
    pressNoteStreamFrame(bs);
    if (bs != NULL && buf != NULL) {
        long modNo;
        int modEnd;

        if (videoModRecord(bs, &modNo, NULL, &modEnd)) {
            /* E2b: nothing to decode; leave DecDCTin the frame's number */
            buf[0] = PRESS_MOD_STUB;
            buf[1] = (u_long)modNo;
            return 0;
        }
    }
    result = hostMdecVlc2(bs, buf, &pressMdec.vlc);
    if (result != 0 && pressLogOn()) {
        printf("mdec: vlc2 stopped at bit %d after %d tokens\n",
               pressMdec.vlc.lastErrorBit, pressMdec.vlc.lastTokens);
    }
    return result;
}

/*
 * DecDCTin(buf, mode): `buf` is the run-level stream DecDCTvlc2 produced.
 * The PS1 passes mode 3 (24bpp); the port decodes 15bpp on purpose, because
 * that is what the port's display can show (see the file comment).
 */
void DecDCTin(u_long *buf, int mode) {
    HostMdecStats stats;
    int width;
    int height;

    (void)mode;
    pressInit();
    pressInvalidate();
    if (buf == NULL) {
        return;
    }
    if (buf[0] == PRESS_MOD_STUB) {
        /* E2b: the replacement's picture, in VRAM format, instead of the MDEC's */
        if (videoModFrame15((long)buf[1], (u_short *)(void *)pressShadow)) {
            pressFrameW = VIDEO_MOD_W;
            pressFrameH = VIDEO_MOD_H;
            pressFrameValid = 1;
            pressFramesDecoded++;
        }
        return;
    }
    if (!hostStLastFrameSize(&width, &height)) {
        if (pressLogOn()) {
            printf("mdec: no frame size from the stream, nothing decoded\n");
        }
        return;
    }
    if (width <= 0 || height <= 0 || (width & 15) != 0 || (height & 15) != 0
            || width > HOST_PRESS_MAX_W || height > HOST_PRESS_MAX_H) {
        if (pressLogOn()) {
            printf("mdec: unusable frame size %dx%d, nothing decoded\n",
                   width, height);
        }
        return;
    }
    if (hostMdecDecodeFrame(&pressMdec, buf, width, height,
                            HMDEC_DEPTH_15BPP, pressShadow, &stats) != 0) {
        if (pressLogOn()) {
            printf("mdec: frame decode failed (%dx%d)\n", width, height);
        }
        return;
    }
    pressFrameW = width;
    pressFrameH = height;
    pressToVram15((u_short *)(void *)pressShadow, (long)width * (long)height);
    pressFrameValid = 1;
    pressFramesDecoded++;
    pressErrorTotal += stats.errors;
    if (pressLogOn()) {
        printf("mdec: frame %ld %dx%d macroblocks %d blocks %d errors %d "
               "tokens %d\n",
               pressFramesDecoded, width, height, stats.macroblocks,
               stats.blocks, stats.errors, stats.tokens);
    }
}

/*
 * DecDCTout(buf, size): hand the game `size` words of the frame, fire the
 * slice callback (which chains the next call), and on the outermost return
 * present the frame. `size` is the game's slice size in words (24-bit), so it
 * is larger than the 15-bit slice would be; the offset advances by it anyway
 * and the copy stops at the end of the shadow.
 */
void DecDCTout(u_long *buf, int size) {
    long total;
    long left;
    long given;

    if (pressDepth >= HOST_PRESS_MAX_DEPTH) {
        fprintf(stderr, "press: slice chain deeper than %d, frame dropped\n",
                HOST_PRESS_MAX_DEPTH);
        return;
    }

    pressDepth++;
    given = (long)size;
    if (buf != NULL && given > 0) {
        total = (long)pressFrameW * (long)pressFrameH / 2;
        left = (pressFrameValid && pressSliceOffset < total)
                   ? total - pressSliceOffset
                   : 0;
        pressCopyWords(buf, given,
                       pressShadow + (pressSliceOffset < total
                                          ? pressSliceOffset
                                          : total),
                       left);
        pressSliceOffset += given;
    }
    if (pressSliceCallback != NULL) {
        pressSliceCallback();
    }
    pressDepth--;
    if (pressDepth == 0) {
        pressPresentFrame();
    }
}

/* Registers the slice callback and returns 0 (accepted). A NULL callback is
   the game's teardown at the end of a movie; with MDEC_DEBUG=1 it reports the
   movie's totals there, and with PACING_DEBUG=1 the pace the movie actually
   kept (the owner's ear is the final judge of the sync). */
int DecDCToutCallback(void (*func)(void)) {
    if (func == NULL && pressLogOn()) {
        printf("mdec: %ld frames decoded, %ld presented, %ld errors over the "
               "movie\n",
               pressFramesDecoded, pressFramesPresented, pressErrorTotal);
    }
    if (func == NULL && pressPaceLogOn()) {
        if (pressPaceFrames > 1) {
            long span = pressPaceLastMs - pressPaceFirstMs;
            double fps = (span > 0)
                             ? (double)(pressPaceLastFrame
                                        - pressPaceFirstFrame) * 1000.0
                                   / (double)span
                             : 0.0;

            printf("pacing: %ld frames %lu..%lu in %ld ms = %.2f fps, "
                   "waited %ld ms (max %ld), capped %ld, stalled %d\n",
                   pressPaceFrames, pressPaceFirstFrame, pressPaceLastFrame,
                   span, fps, pressPaceTotalWait, pressPaceMaxWait,
                   pressPaceCapped, pressPaceStalled);
        } else {
            printf("pacing: %ld frames paced, capped %ld, stalled %d\n",
                   pressPaceFrames, pressPaceCapped, pressPaceStalled);
        }
    }
    pressSliceCallback = func;
    return 0;
}
