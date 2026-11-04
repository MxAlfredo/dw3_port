/*
 * The host's copy of the field's file loader (T22): one guard the upstream
 * unit does not have, and the rest copied verbatim from
 * src/field/field_mode/file_loader.c.
 *
 * Why the guard is here and not upstream (ADR-003 keeps the game's own src/
 * untouched):
 * FIELDSTG_requestSlotFiles reads FIELDSTG_state.slots and dereferences it
 * (the StageSlot->type at offset 0x08 is the read at address 8). A stage
 * without slots never assigns that pointer -- WSTAG780's setupStage does not,
 * and FIELDSTG_pickStage leaves FIELDSTG_state zeroed -- so slots == NULL is
 * the normal state for such a stage, and the field itself knows it: field.c
 * guards `if (FIELDSTG_state.slots != NULL)` before creating its triggers.
 * The loader does not, and on the PlayStation that read hits mapped low RAM
 * (there is no MMU) so the loop ends; on x86-64 page 0 is unmapped and the
 * read faults (measured: SIGSEGV in FIELDSTG_requestSlotFiles with rax == 0,
 * presentFrames 468, before this guard existed). That is NOT the frame T19
 * stopped at: T19's harness stopped itself on purpose at frame 463
 * (pcBootStopInMode), a graceful exit that printed its own summary and wrote
 * its capture, and 463 was later misread as the fault frame because a
 * signal-killed process never flushes a block-buffered stdout -- a pipe or gdb
 * shows the tail some frames short. The harness and pc/check_field.py run the
 * demo under stdbuf -oL for exactly that reason. The exact value the
 * PlayStation reads at address 8 is NOT measured: it is an assumption that the
 * loop terminates there, not a fact. This guard is therefore a host-side
 * accommodation, not a fix to the game: it makes the loader behave the way the
 * field's own code already does -- a stage without slots requests no slot
 * files.
 *
 * Keep this file in sync with the upstream unit: apart from the guard (and
 * this comment), its body is copied verbatim from
 * src/field/field_mode/file_loader.c. pc/Makefile drops that upstream unit
 * from the field-mode glob and compiles this one in its place.
 */

#include "field/field_mode.h"

/* The task (FIELDSTG_loadFieldFiles's) goes unused */
void FIELDSTG_requestInnNames(Task *task) {
    s32 mode = GAME.funcs.getMode();
    s32 found = 0;
    s32 i;

    for (i = 0; FIELDSTG_file5DModes[i] != 0; i++) {
        if (FIELDSTG_file5DModes[i] == (s16)mode) {
            found = 1;
            break;
        }
    }
    if (found) {
        FILE_CACHE.request(TEXT_FILE(TEXT_INN_NAMES));
    }
}

/* Requests the files of the actions the map's slots offer: climbs, drops
   and the gauge game. The task (FIELDSTG_loadFieldFiles's) goes unused. */
void FIELDSTG_requestSlotFiles(Task *task) {
    StageSlot *entry = FIELDSTG_state.slots;
    s32 climbs = 0;
    s32 drops = 0;
    s32 gauges = 0;

    /* T22 (host-only guard, see the file comment): a stage without slots
       leaves FIELDSTG_state.slots NULL -- the normal state, and the one the
       field itself guards before creating its triggers (field.c). The
       PlayStation tolerates the dereference below; the host faults. */
    if (FIELDSTG_state.slots == NULL) {
        return;
    }

    for (; entry->type != 0; entry++) {
        switch (entry->type) {
        case SLOT_CLIMB_UP:
        case SLOT_CLIMB_DOWN:
            climbs = 1;
            break;
        case SLOT_DROP:
            drops = 1;
            break;
        case SLOT_GAUGE:
            gauges = 1;
            break;
        }
    }
    if (climbs) {
        FILE_CACHE.request(FIELD_CLIMB_FILE);
    }
    if (drops) {
        FILE_CACHE.request(FIELD_DROP_FILE);
    }
    if (gauges) {
        FILE_CACHE.request(FIELD_GAUGE_FILE);
    }
}

/* Loads the field's files, a step at a time: 1 once done */
s32 FIELDSTG_loadFieldFiles(Task *task) {
    TimLoader sprites;
    TimLoader loader;

    switch (task->step) {
    case 0:
        switch (task->counter) {
        case 0:
        default:
            FILE_CACHE.request(FIELD_SPRITES_FILE);
            task->tickCounter(task);
            /* fallthrough */
        case 1:
            if (FILE_CACHE.isLoading(FIELD_SPRITES_FILE) != 0) {
                return 0;
            }
            initTimLoader(&sprites);
            sprites.setImagePos(FIELD_SPRITES_X, FIELD_SPRITES_Y);
            sprites.loadArchive(FILE_CACHE.getEntry((FIELD_SPRITES_FILE << 16) | 2));
            sprites.setImagePos(FIELD_SPRITES2_X, FIELD_SPRITES_Y);
            sprites.loadArchive(FILE_CACHE.getEntry((FIELD_SPRITES_FILE << 16) | 3));
            task->nextStep(task);
            break;
        }
        break;
    case 1:
        switch (task->counter) {
        case 0:
        default:
            if (FIELDSTG_state.imageEntry == 0 && FIELDSTG_state.imageFile == 0) {
                task->nextStep(task);
                break;
            }
            if (FIELDSTG_state.imageEntry != 0) {
                FILE_CACHE.request(FIELDSTG_state.imageEntry >> 16);
            }
            if (FIELDSTG_state.sheetEntry != 0) {
                FILE_CACHE.request(FIELDSTG_state.sheetEntry >> 16);
            }
            if (FIELDSTG_state.imageFile != 0) {
                FILE_CACHE.request(FIELDSTG_state.imageFile);
            }
            task->tickCounter(task);
            /* fallthrough */
        case 1:
            if (FIELDSTG_state.imageEntry != 0) {
                if (FILE_CACHE.isLoading(FIELDSTG_state.imageEntry >> 16) != 0) {
                    return 0;
                }
                initTimLoader(&loader);
                loader.setClutPos(0, FIELD_OBJECTS_CLUT_Y);
                loader.setImagePos(FIELD_OBJECTS_X, FIELD_OBJECTS_Y);
                loader.loadArchive(FILE_CACHE.getEntry(FIELDSTG_state.imageEntry));
            }
            task->tickCounter(task);
            /* fallthrough */
        case 2:
            if (FIELDSTG_state.imageFile != 0) {
                if (FILE_CACHE.isLoading(FIELDSTG_state.imageFile) != 0) {
                    return 0;
                }
                initTimLoader(&loader);
                loader.setClutPos(0, FIELD_OBJECTS_CLUT_Y);
                loader.setImagePos(FIELD_OBJECTS_X, FIELD_OBJECTS_Y);
                loader.loadArchive(FILE_CACHE.load(FIELDSTG_state.imageFile));
            }
            task->tickCounter(task);
            /* fallthrough */
        case 3:
            if (FIELDSTG_state.sheetEntry != 0 && FILE_CACHE.isLoading(FIELDSTG_state.sheetEntry >> 16) != 0) {
                return 0;
            }
            task->nextStep(task);
            break;
        }
        break;
    case 2:
        if (FIELDSTG_state.imageFile != 0) {
            FILE_CACHE.free(FIELDSTG_state.imageFile);
        }
        task->nextStep(task);
        /* fallthrough */
    case 3:
        FILE_CACHE.request(FILE_MENU_SPRITES);
        FILE_CACHE.request(FIELDSTG_state.textFile);
        FILE_CACHE.request(TEXT_FILE(TEXT_STATUS));
        FIELDSTG_requestSlotFiles(task);
        FIELDSTG_requestInnNames(task);
        task->nextStep(task);
        return 1;
    default:
        return 1;
    }
    return 0;
}

/* The file loader's update: loads the field's files from its first step on
   (FIELDSTG_loadFieldFiles) */
void FIELDSTG_runFileLoader(Task *task) {
    switch (task->state) {
    case TASK_INIT:
    default:
        task->nextState(task);
        task->substate = task->key2;
        /* fallthrough */
    case TASK_RUN:
        if (FIELDSTG_loadFieldFiles(task) != 0) {
            task->setState(task, TASK_KILL);
        }
        break;
    case TASK_DONE:
    case TASK_KILL:
        break;
    }
}

/* Creates the file loader, which starts at a step */
Task *FIELDSTG_createFileLoader(s32 step) {
    Task *task = createTask(FIELDSTG_runFileLoader, sizeof(Task) + 4, 0); /* 4 bytes more than it uses */

    task->key2 = step;
    return task;
}
