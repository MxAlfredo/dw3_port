/*
 * The port's streaming source for .STR movie frames (T38).
 *
 * PsyCross declares the whole `St*` streaming set and implements none of it;
 * the game's own movie player (src/menus/title/movie.c) names it, so the port
 * has to answer it (ADR-002). host_st.h has the contract and the sector layout
 * measured on the disc; this file is the implementation.
 *
 * Why whole frames: the game hands StGetNext's address straight to
 * DecDCTvlc2, and a frame's Huffman stream runs across all of its sectors.
 * MOVIEOPN.STR's frame 1 is 9 sectors and its count (0x0C40 = 3136 RLE words)
 * is the whole frame's, so the sector payloads are concatenated (headers
 * stripped) into one record per frame and the first sector's 32-byte header is
 * copied next to it for the game to read.
 *
 * Ring layout (all in the caller's buffer, size*2048 bytes):
 *
 *     [ header 32 ][ payload nSectors*2016 ][ header 32 ][ payload ... ]
 *      ^            ^
 *      |            +-- StGetNext's *addr
 *      +--------------- StGetNext's *header
 *
 * Records are laid out consecutively in a cyclic byte interval; when a record
 * does not fit before the end, a padding gap is inserted and the record starts
 * at offset 0. The padding is charged to that record's footprint, so freeing
 * the frames in order (as the game does: DecDCTvlc2 then StFreeRing) returns
 * exactly the space they occupied, and a new record is only started when
 * `live + padding + record <= ring bytes`. That is what keeps assembly off a
 * frame the game has not freed.
 *
 * Delivery: complete frames wait in a FIFO; StGetNext pops the oldest into a
 * single "in flight" slot and returns its pointers. While a frame is in flight
 * StGetNext does not hand out another one -- the game frees each frame right
 * after DecDCTvlc2 -- and StFreeRing(base) releases it, matching the address
 * the game was given. Extra complete frames stay queued behind it.
 *
 * Pumping: the game polls StGetNext in a tight loop (movie.c's 2000-try
 * loops), so StGetNext itself reads up to HST_PUMP_SECTORS sectors per call
 * while the stream is armed. If a new frame cannot start (the ring is full),
 * the sector just read is held in a one-sector pending slot and retried on the
 * next call: nothing is dropped on backpressure. That is the simplest correct
 * option here and keeps the video side independent from the XA driver, which
 * has its own handle and its own LBA cursor (host_cd.c).
 *
 * StCdIntrFlag/StCdInterrupt stay inert: the host reader is synchronous and
 * the ring needs no interrupt bookkeeping (host_st.h).
 */
#include "engine/game.h"

#include "host_st.h"

#include <stdio.h>

/* engine/game.h declares the game's own atoi/random against the PsyQ headers,
   so <stdlib.h> cannot come in here; getenv is all this file needs. */
extern char *getenv(const char *name);

#define HST_ID          0x0160u
#define HST_TYPE        0x8001u

/* Measured maximum on every .STR of the EU disc is 9; 32 leaves room for a
   larger frame without letting a bogus header reserve megabytes. */
#define HST_MAX_SECTORS 32
/* The ring can hold at most ring bytes / 2048 frames; 256 descriptors is more
   than the game's 32-sector ring can ever use. */
#define HST_MAX_FRAMES  256
/* Sectors read by StGetNext per call: a 9-sector frame completes at once. */
#define HST_PUMP_SECTORS 32

typedef struct HostStFrame {
    unsigned long offset;       /* record start (the header copy) */
    unsigned long payload;      /* record start + 32 */
    unsigned long payloadBytes;
    unsigned long footprint;    /* record size + any wrap padding */
    unsigned long frameCount;
    int nSectors;
} HostStFrame;

static unsigned char *stRing;
static unsigned long stRingBytes;

static unsigned long stHead;        /* next record's offset */
static unsigned long stLive;        /* bytes occupied by queued + assembling */

static HostStFrame stQueue[HST_MAX_FRAMES];
static int stQueueHead;
static int stQueueTail;
static int stQueueCount;

static HostStFrame stInFlight;
static int stInFlightActive;

static int stAssembling;
static unsigned long stFrameOffset;
static unsigned long stFrameFootprint;
static unsigned long stWrite;
static unsigned long stFrameCount;
static unsigned long stPrevHead;
static unsigned long stPrevLive;
static int stNSec;
static int stVideoSeen;

static unsigned char stPending[HOST_ST_SECTOR];
static int stHavePending;

static HostStReadFn stRead;
static void *stReadCtx;
static long stNextLba;

static int stActive;
static int stEnded;

static unsigned long stMode;
static unsigned long stStartFrame;
static unsigned long stEndFrame;

static HostStStats stStats;

/* The last delivered frame's size, for the MDEC side (hostStLastFrameSize). */
static int stLastValid;
static int stLastWidth;
static int stLastHeight;

/* --- logging (ST_DEBUG only; inert by default) --------------------------- */

static int stLogState = -1;

static int stLogOn(void) {
    if (stLogState < 0) {
        stLogState = (getenv("ST_DEBUG") != NULL);
    }
    return stLogState;
}

/* --- little-endian header fields ---------------------------------------- */

static unsigned int stRead16(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned long stRead32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* engine/game.h declares its own int-sized memcpy, so no <string.h> here. */
static void stCopy(unsigned char *dst, const unsigned char *src, int n) {
    int i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/* --- ring bookkeeping ---------------------------------------------------- */

static void stDropStream(void) {
    if (stAssembling) {
        /* The newest record is being assembled, so its reservation is the
           tail of the used interval: roll head and live back to the saved
           values and nothing else moves. */
        stHead = stPrevHead;
        stLive = stPrevLive;
        stAssembling = 0;
        stStats.sectorsDropped += stVideoSeen;
    }
    stHavePending = 0;
}

static void stClearQueue(void) {
    stQueueHead = 0;
    stQueueTail = 0;
    stQueueCount = 0;
    stInFlightActive = 0;
    stAssembling = 0;
    stHead = 0;
    stLive = 0;
    stHavePending = 0;
}

static int stBegin(const unsigned char *sector, int nsec, unsigned long frameCount) {
    unsigned long size = (unsigned long)HOST_ST_HEADER +
                         (unsigned long)nsec * HOST_ST_PAYLOAD;
    unsigned long pad = 0;
    unsigned long offset;

    if (stRing == NULL || size > stRingBytes) {
        return 0;
    }
    if (stInFlightActive || stQueueCount >= HST_MAX_FRAMES) {
        /* The game frees each frame before asking for the next: one in-flight
           frame is the contract, and a full descriptor queue means no room. */
        return 0;
    }
    if (stHead + size > stRingBytes) {
        pad = stRingBytes - stHead;
        offset = 0;
    } else {
        offset = stHead;
    }
    if (stLive + pad + size > stRingBytes) {
        return 0; /* no room: the caller keeps the sector and retries */
    }

    stPrevHead = stHead;
    stPrevLive = stLive;
    stLive += pad + size;
    stHead = (offset + size) % stRingBytes;

    stFrameOffset = offset;
    stFrameFootprint = pad + size;
    stWrite = offset + HOST_ST_HEADER;
    stFrameCount = frameCount;
    stNSec = nsec;
    stVideoSeen = 0;
    stAssembling = 1;

    stCopy(stRing + offset, sector, HOST_ST_HEADER);
    return 1;
}

static void stComplete(void) {
    HostStFrame *frame = &stQueue[stQueueHead];

    frame->offset = stFrameOffset;
    frame->payload = stFrameOffset + HOST_ST_HEADER;
    frame->payloadBytes = (unsigned long)stNSec * HOST_ST_PAYLOAD;
    frame->footprint = stFrameFootprint;
    frame->frameCount = stFrameCount;
    frame->nSectors = stNSec;

    stQueueHead = (stQueueHead + 1) % HST_MAX_FRAMES;
    stQueueCount++;
    stAssembling = 0;
    stStats.framesAssembled++;

    if (stLogOn()) {
        printf("st: assembled frame %lu %dx%d %d sectors count %u\n",
               frame->frameCount,
               (unsigned int)stRead16(stRing + frame->offset + 16),
               (unsigned int)stRead16(stRing + frame->offset + 18),
               frame->nSectors,
               (unsigned int)stRead16(stRing + frame->payload));
    }
}

/*
 * One sector through the assembly:
 *   1  a video sector, consumed (started/appended/dropped as out of order);
 *   0  not a video sector, skipped;
 *  -1  a video sector that starts a frame with no room: not consumed.
 */
static int stAccept(const unsigned char *sector) {
    int sec;
    int nsec;
    unsigned long frameCount;

    if (stRing == NULL) {
        return -1;
    }
    if (stRead16(sector + 0) != HST_ID || stRead16(sector + 2) != HST_TYPE) {
        stStats.sectorsSkipped++;
        return 0;
    }

    sec = (int)stRead16(sector + 4);
    nsec = (int)stRead16(sector + 6);
    frameCount = stRead32(sector + 8);

    if (nsec <= 0 || nsec > HST_MAX_SECTORS || sec < 0 || sec >= nsec) {
        stStats.sectorsBad++;
        return 1;
    }

    if (!stAssembling) {
        if (sec != 0) {
            /* Assembly must start at secCount 0: wait for one. */
            stStats.sectorsDropped++;
            return 1;
        }
        if (!stBegin(sector, nsec, frameCount)) {
            return -1;
        }
    } else if (sec == 0 || frameCount != stFrameCount) {
        /* A new frame begins: either the count changed (the usual end-of-
           frame rule when nSectors is short) or the same frame restarted. */
        if (sec != 0) {
            stStats.sectorsDropped++;
            return 1;
        }
        stDropStream();
        if (!stBegin(sector, nsec, frameCount)) {
            return -1;
        }
    } else if (sec != stVideoSeen) {
        /* Gap or duplicate inside a frame. */
        stStats.sectorsDropped++;
        return 1;
    }

    stCopy(stRing + stWrite, sector + HOST_ST_HEADER, HOST_ST_PAYLOAD);
    stWrite += HOST_ST_PAYLOAD;
    stVideoSeen++;
    stStats.sectorsFed++;

    if (stVideoSeen >= stNSec) {
        stComplete();
    }
    return 1;
}

/* --- the public host-side entry points ----------------------------------- */

int hostStFeedSector(const unsigned char *sector2048) {
    if (sector2048 == NULL) {
        return 0;
    }
    return (stAccept(sector2048) == 1) ? 1 : 0;
}

void hostStSetSource(HostStReadFn read, void *ctx) {
    stRead = read;
    stReadCtx = ctx;
}

void hostStArm(long lba) {
    stNextLba = lba;
    stActive = 0;
    stEnded = 0;
    stDropStream();
    if (stLogOn()) {
        printf("st: arm lba=%ld\n", lba);
    }
}

void hostStStart(void) {
    stActive = 1;
    stEnded = 0;
    if (stLogOn()) {
        printf("st: start lba=%ld\n", stNextLba);
    }
}

void hostStStop(void) {
    stActive = 0;
    if (stLogOn()) {
        printf("st: stop fed=%ld skipped=%ld frames=%ld delivered=%ld\n",
               stStats.sectorsFed, stStats.sectorsSkipped,
               stStats.framesAssembled, stStats.framesDelivered);
    }
}

void hostStGetStats(HostStStats *out) {
    if (out != NULL) {
        *out = stStats;
    }
}

int hostStLastFrameSize(int *w, int *h) {
    if (!stLastValid) {
        return 0;
    }
    if (w != NULL) {
        *w = stLastWidth;
    }
    if (h != NULL) {
        *h = stLastHeight;
    }
    return 1;
}

/* --- libcd's St* surface ------------------------------------------------- */

void StSetRing(u_long *ring_addr, u_long ring_size) {
    stRing = (unsigned char *)(void *)ring_addr;
    stRingBytes = (unsigned long)ring_size * HOST_ST_SECTOR;
    stClearQueue();
    stLastValid = 0;
    stStats.sectorsFed = 0;
    stStats.sectorsSkipped = 0;
    stStats.sectorsBad = 0;
    stStats.sectorsDropped = 0;
    stStats.framesAssembled = 0;
    stStats.framesDelivered = 0;
    if (stLogOn()) {
        printf("st: ring %lu sectors (%lu bytes)\n", (unsigned long)ring_size,
               stRingBytes);
    }
}

void StSetStream(u_long mode, u_long start_frame, u_long end_frame,
                 void (*func1)(), void (*func2)()) {
    stMode = mode;
    stStartFrame = start_frame;
    stEndFrame = end_frame;
    (void)func1;
    (void)func2;
    stDropStream();
    stEnded = 0;
    if (stLogOn()) {
        printf("st: stream mode=%lu start=%lu end=%ld\n", mode, start_frame,
               (long)(int)end_frame);
    }
}

void StClearRing(void) {
    stClearQueue();
}

void StUnSetRing(void) {
    stActive = 0;
    stEnded = 1;
    stClearQueue();
    stRing = NULL;
    stRingBytes = 0;
    stLastValid = 0;
}

u_long StFreeRing(u_long *base) {
    if (stRing == NULL || !stInFlightActive) {
        return 0;
    }
    if ((unsigned char *)(void *)base != stRing + stInFlight.payload) {
        /* The game frees what StGetNext handed it, in delivery order. */
        if (stLogOn()) {
            printf("st: free of an address that is not in flight\n");
        }
        return 0;
    }
    stLive -= stInFlight.footprint;
    stInFlightActive = 0;
    if (stLogOn()) {
        printf("st: free frame %lu (%lu bytes, live %lu)\n",
               stInFlight.frameCount, stInFlight.footprint, stLive);
    }
    return 1;
}

/*
 * Pumps at most HST_PUMP_SECTORS sectors and hands out a complete frame. The
 * return value follows libcd: 0 with *addr and *header set, non-zero when no
 * frame is ready (or the stream ended), which the game's busy loops tolerate.
 */
u_long StGetNext(u_long **addr, u_long **header) {
    int budget = HST_PUMP_SECTORS;

    if (stRing == NULL) {
        return (u_long)-1;
    }
    if (stInFlightActive) {
        return (u_long)-1;
    }
    if (stQueueCount == 0 && stActive && !stEnded) {
        while (budget-- > 0) {
            int accepted;

            if (!stHavePending) {
                if (stRead == NULL ||
                    !stRead(stReadCtx, stNextLba, stPending)) {
                    stEnded = 1;
                    if (stLogOn()) {
                        printf("st: stream end at lba %ld (fed=%ld frames=%ld)\n",
                               stNextLba, stStats.sectorsFed,
                               stStats.framesAssembled);
                    }
                    break;
                }
                stNextLba++;
                stHavePending = 1;
            }
            accepted = stAccept(stPending);
            if (accepted < 0) {
                break; /* no room: keep the pending sector for the next call */
            }
            stHavePending = 0;
            if (stQueueCount > 0) {
                break;
            }
        }
    }
    if (stQueueCount == 0) {
        return (u_long)-1;
    }

    stInFlight = stQueue[stQueueTail];
    stQueueTail = (stQueueTail + 1) % HST_MAX_FRAMES;
    stQueueCount--;
    stInFlightActive = 1;
    stStats.framesDelivered++;

    stLastWidth = (int)stRead16(stRing + stInFlight.offset + 16);
    stLastHeight = (int)stRead16(stRing + stInFlight.offset + 18);
    stLastValid = 1;

    if (addr != NULL) {
        *addr = (u_long *)(void *)(stRing + stInFlight.payload);
    }
    if (header != NULL) {
        *header = (u_long *)(void *)(stRing + stInFlight.offset);
    }
    if (stLogOn()) {
        printf("st: frame %lu %dx%d %d sectors count %u\n",
               stInFlight.frameCount,
               (unsigned int)stRead16(stRing + stInFlight.offset + 16),
               (unsigned int)stRead16(stRing + stInFlight.offset + 18),
               stInFlight.nSectors,
               (unsigned int)stRead16(stRing + stInFlight.payload));
    }
    return 0;
}

/* The streaming interrupt libcd would raise as sectors arrive; the host
   reader is synchronous, so none arrives and the flag stays clear. */
void StCdInterrupt(void) {
}

u_char StCdIntrFlag = 0;
