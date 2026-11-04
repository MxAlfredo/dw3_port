/*
 * Port-side wrapper of the game's file cache update (FILE_CACHE.update, src/engine/file/file_cache.c).
 *
 * The game's updateFileCache advances one queued file per call and a read file becomes loaded on the
 * call after (QUEUED -> READING -> LOADED), and the main loop calls it once a frame: with 20 files
 * queued the stage needs about 20 frames for the reads alone, though the host reader (host_cd.c)
 * completes each read before returning. This wrapper, installed by pcCdReaderOpen in place of the
 * table's function pointer, calls the original again for as long as the queue has work, so every
 * file queued during a frame is loaded at the end of that frame. The original function does all of
 * the work (state changes, lastUsed stamps, CD_READER.read, pending), so nothing of the game's
 * bookkeeping is duplicated; waitForFile and the other direct callers are untouched.
 *
 * DW3_FILE_CACHE_STEP=1 keeps the original one-file-per-call pace (to compare the two).
 *
 * Trace (the "stage" channel of the port log, DW3_STAGE_TIMING=1 or log trace; off costs one flag
 * test per call): the most slots of FILE_CACHE_SLOTS in use (a line each time the peak grows, and
 * one at exit) and, for every queue that took more than one call to empty, how many calls (frames)
 * the game waited and for how many files.
 */
#include "engine/file.h"
#include "host.h"

#include "stage_timing.h"

#include <stdlib.h>

#define BATCH_CALL_CAP (FILE_CACHE_SLOTS + 2)

static void (*origUpdate)(void);
static int stepMode;
static int peakSlots;
static int episodeCalls, episodeFiles;

/* Slots holding a file, and of them the ones still queued or being read */
static int countSlots(int *queued) {
    int i, used = 0;

    *queued = 0;
    for (i = 0; i < FILE_CACHE_SLOTS; i++) {
        const FileSlot *slot = &FILE_CACHE.slots[i];

        if (slot->file != 0) {
            used++;
            if (slot->state == FILE_QUEUED || slot->state == FILE_READING) {
                (*queued)++;
            }
        }
    }
    return used;
}

static void logPeakAtExit(void) {
    stageTimingLog("file cache: peak %d of %d slots in use", peakSlots, FILE_CACHE_SLOTS);
}

static void pcUpdateFileCache(void) {
    int trace = stageTimingOn();
    int queued = 0, used, calls;

    if (trace) {
        used = countSlots(&queued);
        if (used > peakSlots) {
            peakSlots = used;
            stageTimingLog("file cache: %d of %d slots in use (new peak)", used, FILE_CACHE_SLOTS);
        }
        if (queued > 0) {
            episodeCalls++;
            if (queued > episodeFiles) {
                episodeFiles = queued;
            }
        }
    }
    origUpdate();
    if (!stepMode) {
        for (calls = 1; calls < BATCH_CALL_CAP && FILE_CACHE.pending != 0; calls++) {
            origUpdate();
        }
    }
    if (trace && episodeCalls > 0) {
        (void)countSlots(&queued);
        if (queued == 0) {
            if (episodeCalls > 1) {
                stageTimingLog("file cache: queue of %d files took %d updates", episodeFiles, episodeCalls);
            }
            episodeCalls = episodeFiles = 0;
        }
    }
}

/* requestFile found every one of the FILE_CACHE_SLOTS taken (src/engine/file/file_cache.c): say so, whatever the trace setting */
static void logCacheFull(s32 file, s32 evicted) {
    if (evicted != 0) {
        plogLine("stage", "file cache full (%d slots): file %d evicted to make room for file %d\n", FILE_CACHE_SLOTS, (int)evicted, (int)file);
    } else {
        plogLine("stage", "file cache full (%d slots, none loaded to evict): file %d not queued, it will be asked for again\n", FILE_CACHE_SLOTS,
                 (int)file);
    }
}

/* Called once the disc is open (host_cd.c): idempotent */
void pcFileCacheInstall(void) {
    const char *env;

    if (origUpdate != NULL) {
        return;
    }
    env = getenv("DW3_FILE_CACHE_STEP");
    stepMode = env != NULL && env[0] != '\0' && env[0] != '0';
    origUpdate = FILE_CACHE.update;
    FILE_CACHE.update = pcUpdateFileCache;
    pcFileCacheFullHook = logCacheFull;
    if (stageTimingOn()) {
        atexit(logPeakAtExit);
    }
}

#ifdef FILE_CACHE_PC_TEST
/*
 * `make -C pc filecachetest`: the game's own file_cache.c over a fake file table and a fake CD reader
 * (no disc), checking that one FILE_CACHE.update() leaves every queued file loaded, in the order the
 * original would have read them, and that the original pace is one file per call.
 */
#include <stdio.h>

static int failures;
static int readCount;
static s32 readOrder[FILE_CACHE_SLOTS + 4];

#define CHECK(cond, ...)                         \
    do {                                         \
        if (!(cond)) {                           \
            failures++;                          \
            printf("FAIL line %d: ", __LINE__);  \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
        }                                        \
    } while (0)

static s32 fakeSectorCount(s32 file) {
    return 1 + file % 3;
}

static void fakeRead(s32 file, s32 offset, s32 size, void *buffer, s32 *done) {
    (void)offset;
    (void)size;
    ((u8 *)buffer)[0] = (u8)file; /* the buffer must be the slot's own */
    if (readCount < (int)(sizeof(readOrder) / sizeof(readOrder[0]))) {
        readOrder[readCount] = file;
    }
    readCount++;
    if (done != NULL) {
        *done = 1;
    }
}

static int fullCalls;
static s32 fullEvicted;

static void fullHook(s32 file, s32 evicted) {
    (void)file;
    fullCalls++;
    fullEvicted = evicted;
}

static int loadedCount(void) {
    int i, n = 0;

    for (i = 0; i < FILE_CACHE_SLOTS; i++) {
        if (FILE_CACHE.slots[i].file != 0 && FILE_CACHE.slots[i].state == FILE_LOADED) {
            n++;
        }
    }
    return n;
}

static void request(int first, int count) {
    int i;

    for (i = 0; i < count; i++) {
        requestFile(first + i);
    }
}

static void resetCache(void) {
    freeAllFiles();
    FILE_CACHE.pending = 0;
    readCount = 0;
}

int main(void) {
    int i;

    pcGfxInit();
    FILE_TABLE.getSectorCount = fakeSectorCount;
    CD_READER.read = fakeRead;

    /* the game's own pace, called directly: one read a call, a read file loads on the call after */
    request(100, 5);
    updateFileCache();
    CHECK(readCount == 1 && loadedCount() == 0, "original call 1: reads=%d loaded=%d", readCount, loadedCount());
    updateFileCache();
    CHECK(readCount == 2 && loadedCount() == 1, "original call 2: reads=%d loaded=%d", readCount, loadedCount());
    resetCache();

    /* the wrapper in front of it: everything queued is loaded by the one call, in queue order */
    pcFileCacheInstall();
    request(100, 5);
    FILE_CACHE.update();
    CHECK(readCount == 5 && loadedCount() == 5 && FILE_CACHE.pending == 0, "wrapped, 5 queued: reads=%d loaded=%d pending=%d",
          readCount, loadedCount(), (int)FILE_CACHE.pending);
    for (i = 0; i < 5; i++) {
        CHECK(readOrder[i] == 100 + i, "read %d was file %d, expected %d", i, (int)readOrder[i], 100 + i);
        CHECK(findFileSlot(100 + i) != NULL && ((u8 *)findFileSlot(100 + i)->data)[0] == (u8)(100 + i),
              "file %d has its own bytes", 100 + i);
    }
    /* with nothing queued the call changes nothing */
    FILE_CACHE.update();
    CHECK(readCount == 5 && loadedCount() == 5, "idle call: reads=%d loaded=%d", readCount, loadedCount());
    /* a file queued later is loaded by the next call, the ones loaded stay */
    request(200, 1);
    FILE_CACHE.update();
    CHECK(readCount == 6 && loadedCount() == 6 && FILE_CACHE.pending == 0, "late file: reads=%d loaded=%d", readCount, loadedCount());
    resetCache();

    /* a full cache (all FILE_CACHE_SLOTS queued) is loaded by one call too */
    request(1000, FILE_CACHE_SLOTS);
    FILE_CACHE.update();
    CHECK(readCount == FILE_CACHE_SLOTS && loadedCount() == FILE_CACHE_SLOTS && FILE_CACHE.pending == 0,
          "full cache: reads=%d loaded=%d pending=%d", readCount, loadedCount(), (int)FILE_CACHE.pending);
    resetCache();

    /* modding-ux U8c: a 65th file with every slot taken must not crash (the original dereferences findFreeFileSlot() unchecked).
       All 64 queued (none loaded, so nothing to evict): the request is dropped and reported; all 64 loaded: the oldest is evicted. */
    pcFileCacheFullHook = fullHook;
    request(3000, FILE_CACHE_SLOTS);
    requestFile(4000);
    CHECK(fullCalls == 1 && fullEvicted == 0 && findFileSlot(4000) == NULL, "full of queued files: hook calls=%d evicted=%d", fullCalls,
          (int)fullEvicted);
    FILE_CACHE.update();
    CHECK(loadedCount() == FILE_CACHE_SLOTS, "queued files still load after a dropped request (%d)", loadedCount());
    {
        s32 oldest = findOldestFile()->file; /* whichever the game's own rule picks (the fake clock may tie them all) */

        requestFile(4001);
        CHECK(fullCalls == 2 && fullEvicted == oldest, "full of loaded files: hook calls=%d evicted=%d expected %d", fullCalls,
              (int)fullEvicted, (int)oldest);
        CHECK(findFileSlot(oldest) == NULL && findFileSlot(4001) != NULL, "the oldest file made room for the new one");
    }
    /* waitForFile on a full cache of loaded files still ends with the file loaded */
    waitForFile(4002);
    CHECK(findFileSlot(4002) != NULL && findFileSlot(4002)->state == FILE_LOADED, "waitForFile on a full cache loads the file");
    pcFileCacheFullHook = NULL;
    resetCache();

    printf(failures == 0 ? "filecachetest: OK\n" : "filecachetest: %d FAILED\n", failures);
    return failures != 0;
}
#endif
