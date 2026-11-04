#ifndef PC_SHIM_HOST_ST_H
#define PC_SHIM_HOST_ST_H

/*
 * The port's own streaming source for .STR movie frames (T38): the `St*` half
 * of libcd that PsyCross declares and does not implement (ADR-002).
 *
 * The game's movie player (src/menus/title/movie.c) drives it exactly as it
 * drives the PlayStation's libcd:
 *
 *   StSetRing(ring, 32)            a 0x10000-byte ring buffer
 *   StSetStream(1, 1, -1, 0, 0)    stream mode, first/last frame, callbacks
 *   StGetNext(&addr, &header)      busy loop until it returns 0
 *   DecDCTvlc2(addr, vlcbuf, tbl)  the whole frame's MDEC payload
 *   StFreeRing(addr)               release that frame
 *
 * What StGetNext hands back is a WHOLE FRAME, not one disc sector: `addr`
 * points at the frame's MDEC payload stream (the first sector's 32-byte
 * StHEADER stripped, every following sector's header stripped, the payloads
 * concatenated) and `header` points at a copy of the first sector's 32-byte
 * StHEADER, which the game reads frameCount, width and height from. The disc
 * gives the reason: MOVIEOPN.STR's frame 1 spans 9 video sectors and its
 * count field (0x0C40 = 3136) matches the token count the whole frame's
 * Huffman stream needs, so the decoder is fed one concatenated buffer per
 * frame.
 *
 * Sector layout, measured on disks/eu/AAA/STR/MOVIEOPN.STR (2048-byte sectors,
 * little-endian):
 *
 *      0  u16 id         0x0160
 *      2  u16 type       0x8001
 *      4  u16 secCount   index of this sector in the frame, 0..nSectors-1
 *      6  u16 nSectors   sectors in this frame (9, or 8 in places)
 *      8  u32 frameCount 1-based
 *     12  u32 frameSize
 *     16  u16 width      320
 *     18  u16 height     416
 *     32  payload        2016 bytes (the MDEC stream starts here)
 *
 * Sectors whose id/type are not 0x0160/0x8001 (the XA audio sectors the file
 * interleaves, whose first bytes are e.g. 0x0C0C0C0C) are skipped and never
 * reach the decoder. A frame ends after nSectors video sectors (secCount runs
 * 0..nSectors-1) or when frameCount changes, and assembly only starts at
 * secCount == 0.
 *
 * Ring storage is the caller's buffer (ring, size*2048 bytes). Complete frames
 * sit in a FIFO inside it; StGetNext returns pointers into the ring and
 * StFreeRing releases the frame, so assembly never overwrites a frame the game
 * has not freed. With the game's 32-sector ring three 9-sector frames fit, and
 * a fourth frame waits for the game to free one (backpressure: the sector is
 * held back, not dropped).
 *
 * Two ways in, the same assembly code:
 *
 *   * the port: hostStSetSource installs the CD reader (host_cd.c reads the
 *     raw 2352-byte frames through the reader the XA path already uses and
 *     copies out each 2048-byte sector), hostStArm records the stream LBA
 *     from CdlSetloc, hostStStart/hostStStop follow CdRead2/CdlPause, and
 *     StGetNext pumps a bounded number of sectors per call while a frame is
 *     not ready yet -- the simplest correct option, because the game already
 *     calls StGetNext in a tight loop and no other per-frame tick owns the
 *     video side;
 *   * the harness: hostStFeedSector takes one 2048-byte sector directly, so
 *     pc/sttest.c can replay an extracted .STR with no disc image.
 */

/* One 2048-byte sector's bytes: the frame's user data, header included. */
#define HOST_ST_SECTOR      2048
#define HOST_ST_HEADER      32
#define HOST_ST_PAYLOAD     2016

/*
 * Reads one sector for the pump: 1 with sector2048 filled, 0 past the stream's
 * end (or unreadable). host_cd.c implements it over the raw disc image.
 */
typedef int (*HostStReadFn)(void *ctx, long lba, unsigned char *sector2048);

typedef struct HostStStats {
    long sectorsFed;      /* video sectors appended to a frame */
    long sectorsSkipped;  /* non-video sectors (XA, padding) */
    long sectorsBad;      /* video id/type but an unusable header */
    long sectorsDropped;  /* video sector out of frame order */
    long framesAssembled; /* frames completed */
    long framesDelivered; /* frames handed out by StGetNext */
} HostStStats;

/*
 * Feeds one 2048-byte sector through the assembly. Returns 1 when it was a
 * video sector and was consumed, 0 when it was not video (or no room for a new
 * frame: the caller may retry the same sector later).
 */
int hostStFeedSector(const unsigned char *sector2048);

/* Installs the pump's sector source (host_cd.c does this at CD init). */
void hostStSetSource(HostStReadFn read, void *ctx);

/* The CD control surface the movie player drives: CdlSetloc -> hostStArm,
   CdRead2(CdlModeStream) -> hostStStart, CdlPause -> hostStStop. */
void hostStArm(long lba);
void hostStStart(void);
void hostStStop(void);

/* Counters for the harness and for the ST_DEBUG log. */
void hostStGetStats(HostStStats *out);

/*
 * The width and height of the last frame StGetNext handed out, read from its
 * header (StHEADER +16/+18). Returns 1 when a frame has been delivered since
 * the ring was set, 0 when no size is known yet. The movie's decoder
 * (shim/host_press.c, T39) sizes its decode from these, because DecDCTin gets
 * the run-level stream and no header.
 */
int hostStLastFrameSize(int *w, int *h);

#endif /* PC_SHIM_HOST_ST_H */
