/*
 * Host shim for the game's heap (src/engine/system/heap.c), which the game
 * reaches through HEAP: alloc / allocZeroed / free everywhere, and, from
 * main's mode loop, init and freeByTag.
 *
 * The game's heap is a doubly linked list of blocks between HEAP_START and
 * HEAP_END (0x801FF000, the top of the PlayStation's 2 MB of RAM); both are
 * fixed addresses, so the game's allocator is not portable as it stands. This
 * shim keeps the interface and backs it with the host's malloc, and it keeps
 * the tags, because main's loop frees a mode's memory by tag:
 *
 *   HEAP.freeByTag(MEM_MODE);
 *
 * Every block carries a 16-byte header (the tag and a link into the shim's
 * list of live blocks) so freeByTag can find the blocks that carry a tag.
 * That is what makes it possible to run the game's own main(): its loop
 * frees the whole mode by tag whenever the mode task dies.
 *
 * The header is 16 bytes, the alignment malloc already gives, so the pointer
 * handed to the game keeps malloc's alignment.
 *
 * What this is NOT: the game's allocator (no splitting, no high allocation
 * from the top of a fixed range, no real fragmentation behaviour). It is the
 * same trade the rest of pc/ makes: the game's own source is not edited, the
 * host side adjusts.
 */
#include "engine/heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The header in front of every block this shim hands out */
typedef struct HostBlock {
    struct HostBlock *next; /* the shim's list of live blocks */
    s32 tag;                /* the heap tag the block was allocated with */
    s32 pad;                /* keeps the block's data 16-byte aligned */
} HostBlock;

static HostBlock *hostBlocks; /* NULL when nothing is allocated */

static HostBlock *hostSplit(void *ptr) {
    return ((HostBlock *)ptr) - 1;
}

static void hostUnlink(HostBlock *block) {
    HostBlock **link = &hostBlocks;

    while (*link != NULL) {
        if (*link == block) {
            *link = block->next;
            return;
        }
        link = &(*link)->next;
    }
}

static void *hostAllocBlock(size_t size, s32 tag) {
    HostBlock *block = malloc(size + sizeof(HostBlock));

    if (block == NULL) {
        return NULL;
    }
    block->next = hostBlocks;
    block->tag = tag;
    block->pad = 0;
    hostBlocks = block;
    return (void *)(block + 1);
}

/* Whether a block is still in the live list */
static int hostIsLive(HostBlock *block) {
    HostBlock *it;

    for (it = hostBlocks; it != NULL; it = it->next) {
        if (it == block) {
            return 1;
        }
    }
    return 0;
}

/*
 * Frees a live block. A block that is not live is left alone: the game keeps
 * pointers into a mode's memory across freeByTag and frees them again later
 * (STGMCARD_loadFiles frees STGMCARD_funcs.dataBuf/infoBuf from the previous
 * memory card screen, src/menus/memory_card/screen.c:125; the second save of
 * a run faulted in free() as a double free). The game's own allocator only
 * marks a block free, which a stale pointer does no harm to; malloc's free
 * aborts instead, so the host ignores it, once logged.
 */
static void hostFree(void *ptr) {
    static int warned;
    HostBlock *block;

    if (ptr == NULL) {
        return;
    }
    block = hostSplit(ptr);
    if (!hostIsLive(block)) {
        if (!warned) {
            warned = 1;
            printf("heap: free of a block that is no longer live (%p), ignored\n",
                   ptr);
        }
        return;
    }
    hostUnlink(block);
    free(block);
}

static void *hostAlloc(s32 size, s32 tag) {
    if (size < 0) {
        size = 0;
    }
    if (size == 0) {
        /* battle-entry T3: WFIGHTTS_start (src/debug/battle_test/battle_test.c:393)
           and STAGSLCT_createStageSelect create their task with size 0. On the
           PlayStation that block still sits in RAM and the task's header (and
           fields, past a 0-size block) are written and read back every frame;
           here a 0-byte malloc cannot hold any of it. Give the block room for
           the zero-size task pattern to write (BattleTest is 0x6C bytes;
           TaskHeader ~0x50). The extra room is free. */
        size = 0x80;
    }
    return hostAllocBlock((size_t)size, tag);
}

/*
 * Zeroed allocations get twice the room. That is not padding for its own sake:
 * it is what the game's own task arithmetic addresses.
 *
 * createTask's child count used to be `childrenSize / 4` on this host too, and
 * with 8-byte pointers that walked twice the slots of the array. It now counts
 * `childrenSize / sizeof(void *)` (DW3_PC, src/engine/task/task.c) and every
 * caller sizes the array as n * sizeof(Task *), so the doubling is no longer
 * needed for the task children. It stays as a cheap margin for guest code that
 * sizes a zeroed block assuming 4-byte pointers (the same trade
 * pc/ps1_types.h makes for `u_long`); the extra room is zeroed.
 *
 * The overhead is a factor of two on the zeroed allocations only (tasks, their
 * child arrays, a few buffers); the plain allocations are exact.
 */
static void *hostAllocZeroed(s32 size, s32 tag) {
    void *ptr;

    if (size < 0) {
        size = 0;
    }
    if (size == 0) {
        /* Same zero-size stand-in as hostAlloc: the battle test's and the
           stage select's task are built with size 0 and never fit a 0-byte
           block. See hostAlloc for the why. */
        size = 0x80;
    }
    ptr = hostAllocBlock((size_t)size * 2, tag);
    if (ptr != NULL) {
        memset(ptr, 0, (size_t)size * 2);
    }
    return ptr;
}

static void *hostAllocHigh(s32 size, s32 tag) {
    /* No dedicated high end on the host; plain allocation is a valid Heap. */
    return hostAlloc(size, tag);
}

/* The text window's setTextBuffer and the text helpers' initTextTools zero
   their buffers through HEAP.zero; the file layer and the plain measure path
   never did, so this is the milestone 3 addition. */
static void hostZero(void *ptr, s32 size) {
    memset(ptr, 0, (size_t)size);
}

static void hostFill(void *ptr, s32 value, s32 size) {
    memset(ptr, value, (size_t)size);
}

/*
 * The game's own initHeap builds the block list between HEAP_START and
 * HEAP_END; the host allocator has nothing to set up. main calls it at boot.
 */
static void hostInit(void) {
}

/*
 * Frees every live block that carries `tag`. main's loop calls it with
 * MEM_MODE whenever the mode task dies, which is how a mode's own memory
 * goes away before the next mode starts.
 */
static void hostFreeByTag(s32 tag) {
    HostBlock *block = hostBlocks;
    HostBlock *next;

    while (block != NULL) {
        next = block->next;
        if (block->tag == tag) {
            hostUnlink(block);
            free(block);
        }
        block = next;
    }
}

/*
 * The game's lockMem keeps a block by marking it: with the tag freed by mode
 * it would go away with the mode. Here freeByTag only frees what its own tag
 * says, and MEM_LOCKED is kept apart from MEM_MODE, so locking is a no-op --
 * but it must exist: the sound loader calls HEAP.lock.
 */
static void hostLock(void *ptr, s32 lock) {
    (void)ptr;
    (void)lock;
}

static void hostNop(void) {
}

/*
 * The shim's live-block list, for the host's own probes: the game's heap is
 * a linked list the host does not build (see shim header note), and probes
 * like the pad's dialog walk need the same surface the game's code Would
 * have read. Blocks keep the same tag numbers as the game's (engine/heap.h).
 */
void *pcHeapFirstBlock(void) {
    return hostBlocks;
}

void *pcHeapNextBlock(void *block) {
    return ((HostBlock *)block)->next;
}

s32 pcHeapBlockTag(void *block) {
    return ((HostBlock *)block)->tag;
}

void *pcHeapBlockData(void *block) {
    return (void *)(((HostBlock *)block) + 1);
}

Heap HEAP = {
    .size = 0,
    .first = NULL,
    .end = NULL,
    .init = hostInit,
    .free = hostFree,
    .freeByTag = hostFreeByTag,
    .alloc = hostAlloc,
    .allocHigh = hostAllocHigh,
    .allocZeroed = hostAllocZeroed,
    .zero = hostZero,
    .fill = hostFill,
    .lock = hostLock,
    .nop = hostNop,
};
