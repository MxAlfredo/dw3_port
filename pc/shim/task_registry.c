/*
 * Host shim for the game's task registry (src/engine/task/registry.c).
 *
 * The registry is a table of the tasks created with an id, plus the methods
 * the game calls through TASK_REGISTRY.funcs. Its own file cannot be built on
 * the host: every task update switches the stack to the PlayStation's
 * scratchpad with inline MIPS assembly (SetSpadStack, 0x1F8003FC), which no
 * host assembler accepts. The scratchpad is a PS1 speed trick -- one task
 * update runs from 1F800000 instead of RAM -- so what is not portable is the
 * *stack*, not the behaviour.
 *
 * This file therefore provides that behaviour, as engine/task.h and task.c
 * document it: a task's update runs once a frame, TASK_KILL destroys the task
 * after its last update (task.c's destroyTask), a paused task is skipped, and
 * the children of a live task run right after it. It is the port's own code,
 * not a copy: it exists so that main's loop
 *
 *     ROOT_TASK = TASK_REGISTRY.funcs.run(ROOT_TASK);
 *
 * runs the game's own mode and its children (country_select.c's screen, menu,
 * panels and background are all tasks of this tree).
 *
 * Milestones 0-5 never registered a task (createTask passes id 0, and only
 * createTaskWithId with a nonzero id touches these methods); they keep
 * working with the table below, which is now a real registry rather than the
 * empty one those milestones needed.
 *
 * TODO(battle-entry T1): temporary traces for the balloon crash. They follow
 * the life and the lookups of the tasks the case goes through:
 *
 *  - the actors: the registry kind FIELD_TASK_ACTOR is 5 (field/field_map.h:26).
 *    Their registration print must not trust the task's own keys: the game
 *    assigns an actor's key1/key2 AFTER the registration (the balloon set
 *    printed the always-0 values), so the actor's keys are printed only at
 *    its removal (its death) and find.
 *  - the balloons: script command ids 0x323-0x326 (balloon.c's
 *    FIELDSTG_balloonCommand ids 803-806). A balloon can outlive the actor
 *    (or the player) it points at, which the crash theory needs timed.
 *
 * The find traces keep to actor looks: the field reads the registry far more
 * often than the balloon commands do, so tracing every kind floods the log.
 * Remove once T1 is diagnosed.
 */
#include "engine/task.h"
#include "field/stage.h"
#include "battle/battle_mode.h"
#include "engine/file.h"
#include <stdio.h>
#include "port_log.h"

/* Whether a task's life the balloon's crash traces follow (T1, see above) */
static int hostTracedTask(Task *task) {
    if (task->id == 5) { /* FIELD_TASK_ACTOR: its kind, in field/field_map.h:26 */
        return 1;
    }
    return task->id >= 0x323 && task->id <= 0x326; /* the balloons under 0x323 */
}

/* The boot's shared stand-in for address 0 (hostRunTaskOnce's comment): a
   zeroed page every childless task's update receives as its second arg, and
   where WFIGHTTS's debug test keeps its per-play pointers */
static char hostNullChildrenPage[0x80];

/* WSTAG460's seventh wanderer asks for the map object of animation 4, which
   the stage's table does not hold (the animations are 1-3 and 5-9), so its
   tile pointer stays NULL and every frame writes the object's fields at
   address 0 -- harmless RAM on the PS1. The stand-in is a zeroed object of
   its own that nothing draws. Weak: the tools that link this file without
   the stage overlays (rlen_tool) leave it NULL */
extern void WSTAG460_updateWanderer(StageWanderer *task) __attribute__((weak));
static StageTile hostNullWandererTile;

/* Forgets every registered task */
static void hostClearTasks(void) {
    s32 i;

    for (i = TASK_REGISTRY_SIZE - 1; i >= 0; i--) {
        TASK_REGISTRY.tasks[i] = NULL;
    }
}

/* Adds a task to the first free entry (ignored when the registry is full) */
static void hostRegisterTask(Task *task) {
    s32 i;

    for (i = 0; i < TASK_REGISTRY_SIZE; i++) {
        if (TASK_REGISTRY.tasks[i] == NULL) {
            TASK_REGISTRY.tasks[i] = task;
            /* TODO(battle-entry T1): the register line cannot print key1/key2:
               the registration runs before the caller assigns them (an actor
               always showed 0x0/0x0 here), so only the registry place goes. */
            if (hostTracedTask(task)) {
                PLOG("tasks", PLOG_TRACE, "register id=%d (0x%x) slot=%d\n",
                     (int)task->id, (unsigned)task->id, (int)i);
            }
            return;
        }
    }
    if (task->id == 5) { /* FIELD_TASK_ACTOR */
        plogLine("tasks", "registry FULL, actor DROPPED\n");
    }
}

/* Removes a task from the registry */
static void hostUnregisterTask(Task *task) {
    s32 i;
    s32 j;

    for (i = 0; i < TASK_REGISTRY_SIZE; i++) {
        if (TASK_REGISTRY.tasks[i] == task) {
            /* TODO(battle-entry T1): the death line, with the task's own
               (once-assigned) keys: an actor's last key1, and whether it died
               before the balloon that still points it. */
            if (hostTracedTask(task)) {
                PLOG("tasks", PLOG_TRACE, "unregister id=%d (0x%x) key1=%d key2=%d state=%d slot=%d\n",
                     (int)task->id, (unsigned)task->id, (int)task->key1, (int)task->key2,
                     (int)task->state, (int)i);
            }
            /* TODO(battle-entry T1): the dangling-actor window: anything dies
               while a balloon (id 0x323-0x326) is still registered — the
               balloon's task->actor keeps the dead task's pointer. This does
               not flag same-id kills (a balloon killed beside its actor in a
               teardown is fine, its own life line above shows the state). */
            for (j = 0; j < TASK_REGISTRY_SIZE; j++) {
                Task *balloon = TASK_REGISTRY.tasks[j];
                if (balloon != NULL && balloon != task &&
                    balloon->id >= 0x323 && balloon->id <= 0x326) {
                    plogLine("tasks",
                            "unregister id=%d (0x%x) state=%d slot=%d "
                            "with balloon id=%d (0x%x) ALIVE slot=%d\n",
                            (int)task->id, (unsigned)task->id, (int)task->state, (int)i,
                            (int)balloon->id, (unsigned)balloon->id, (int)j);
                    break;
                }
            }
            TASK_REGISTRY.tasks[i] = NULL;
            return;
        }
    }
}

/* The next registered task that matches (-1 matches anything), or NULL */
static void *hostFindNextTask(void) {
    s32 i;

    for (i = TASK_REGISTRY.findNext; i < TASK_REGISTRY_SIZE; i++) {
        Task *task = TASK_REGISTRY.tasks[i];

        if (task != NULL && (TASK_REGISTRY.findId == -1 || task->id == TASK_REGISTRY.findId) &&
            (TASK_REGISTRY.findKey1 == -1 || task->key1 == TASK_REGISTRY.findKey1) &&
            (TASK_REGISTRY.findKey2 == -1 || task->key2 == TASK_REGISTRY.findKey2)) {
            TASK_REGISTRY.findNext = i + 1;
            return task;
        }
    }
    return NULL;
}

/* The first registered task that matches (-1 matches anything), or NULL */
static void *hostFindTask(s32 id, s32 key1, s32 key2) {
    void *ret;

    TASK_REGISTRY.findId = id;
    TASK_REGISTRY.findKey1 = key1;
    TASK_REGISTRY.findKey2 = key2;
    TASK_REGISTRY.findNext = 0;
    ret = hostFindNextTask();
    /* TODO(battle-entry T1): the actor finds only (kind id 5), the kind every
       balloon command reads: the requested (key1, key2) beside the matched
       task's own (id, key1, key2) on a hit, or a MISS when it is NULL. The
       wildcard keyed finds (the player's kind, key1 -1 key2 0, every frame)
       print nothing, or the log would flood. */
    if (id == 5) {
        Task *hit = (Task *)ret;

        if (key1 == -1 && key2 == 0) {
            return ret;
        }
        if (hit != NULL) {
            PLOG("tasks", PLOG_TRACE, "actor find key1=%d key2=%d -> hit id=%d key1=%d key2=%d\n",
                 (int)key1, (int)key2, (int)hit->id, (int)hit->key1, (int)hit->key2);
        } else {
            PLOG("tasks", PLOG_TRACE, "actor find key1=%d key2=%d -> MISS\n", (int)key1, (int)key2);
        }
    }
    return ret;
}

/*
 * One frame of a task: its update (unless it is paused), then its children,
 * or, when it was already dying, its destructor. Returns the task, or NULL
 * once it is gone (its destructor frees it).
 */
static Task *hostRunTaskOnce(Task *task) {
    s32 dying = task->state == TASK_KILL;

    if (task->state == TASK_RUN && task->paused != 0) {
        /* A positive pause counts down one frame; -1 is paused until resumed */
        if (task->paused > 0) {
            task->paused = -1;
        }
    } else {
        if (WSTAG460_updateWanderer != NULL &&
            (void *)task->update == (void *)WSTAG460_updateWanderer && task->state != TASK_INIT &&
            ((StageWanderer *)task)->tile.tile == NULL) {
            ((StageWanderer *)task)->tile.tile = &hostNullWandererTile;
        }
        /* battle-entry T3: the runner hands `task->children` to the update.
           A task created with 0 child bytes has children NULL — on the PS1
           address 0 is RAM (KUSEG), so WFIGHTTS's debug test writes its
           per-play pointers there and reads them back next frame. Here NULL
           would fault on the first touch (x86 does not map address 0). The
           stand-in is this boot's shared zeroed page at address 0: every
           childless task sees the same buffer, writes persist, and it starts
           cleared like the PS1's RAM does after boot. */
        Task **children = task->children;

        task->update(task, (void *)(children != NULL ? children : hostNullChildrenPage));
    }
    if (dying) {
        task->destroy(task);
        return NULL;
    }
    if (task->state != TASK_RUN || task->paused == 0) {
        TASK_REGISTRY.funcs.runChildren(task);
    }
    return task;
}

/* Runs a frame of each child of a task, dropping the ones that end */
static void hostRunChildTasks(Task *task) {
    s32 count = task->childCount;
    Task **children = task->children;
    s32 i;

    for (i = 0; i < count; i++) {
        if (children[i] != NULL) {
            children[i] = hostRunTaskOnce(children[i]);
        }
    }
}

/*
 * FIGHTSTG_fighterCache remembers the last fighter getInfo found as raw pointers into the
 * fighters' file in the file cache and never forgets them (src/battle/battle_mode/fighter_info.c).
 * On the PlayStation the file is reloaded to the same address; the host heap gives it another one
 * (or none while it is freed), so the next battle's getInfo(sameId) returned a stale pointer: a
 * garbage model file and an endless loadFile(0) (or a crash). Drop the memo whenever the file
 * is not there or the pointer lies outside it.
 */
/* weak: this file is also linked into the rlen tool, which has no battle or file cache */
extern FighterCache FIGHTSTG_fighterCache __attribute__((weak));
extern FileSlot *findFileSlot(s32 file) __attribute__((weak));
extern FileTableFuncs FILE_TABLE __attribute__((weak));

static void hostDropStaleFighterCache(void) {
    FightersFile *file;
    FileSlot *slot;
    u8 *at;
    size_t size;
    static u8 *knownBase; /* where the file sat the last time the memo was seen */

    if (&FIGHTSTG_fighterCache == NULL || findFileSlot == NULL || &FILE_TABLE == NULL) {
        return;
    }
    if (FIGHTSTG_fighterCache.id == 0) {
        knownBase = NULL;
        return;
    }
    slot = findFileSlot(FILE_FIGHTERS);
    at = (u8 *)FIGHTSTG_fighterCache.enemyInfo;
    file = slot != NULL ? (FightersFile *)slot->data : NULL;
    size = slot != NULL ? (size_t)FILE_TABLE.getSectorCount(FILE_FIGHTERS) * CD_SECTOR_SIZE : 0;
    if (file == NULL || at < (u8 *)file || at >= (u8 *)file + size ||
        (knownBase != NULL && knownBase != (u8 *)file)) {
        knownBase = NULL;
        FIGHTSTG_fighterCache.id = 0;
        FIGHTSTG_fighterCache.partnerInfo = NULL;
        FIGHTSTG_fighterCache.enemyInfo = NULL;
        return;
    }
    knownBase = (u8 *)file;
}

/* Runs a frame of a task and its children; returns it, or NULL once it is gone */
static Task *hostRunTask(Task *task) {
    if (task == NULL) {
        return NULL;
    }
    hostDropStaleFighterCache();
    return hostRunTaskOnce(task);
}

/* Ends a task now: its last update and its destructor run at once */
static void hostKillTask(Task *task) {
    if (task != NULL) {
        task->setState(task, TASK_KILL);
        hostRunTaskOnce(task);
    }
}

TaskRegistry TASK_REGISTRY = {
    .funcs = {
        hostClearTasks,
        hostRegisterTask,
        hostUnregisterTask,
        hostFindTask,
        hostFindNextTask,
        hostRunChildTasks,
        hostRunTask,
        hostKillTask,
    },
};
