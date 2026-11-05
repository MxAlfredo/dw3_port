/*
 * The host's copy of the field's state methods (digilab hang): one change to
 * FIELDSTG_pickStage, the rest copied verbatim from
 * src/field/field_mode/state.c.
 *
 * Why (ADR-003 keeps the game's own src/ untouched): pickStage clears the
 * first 100 bytes of FIELDSTG_state, which on the PlayStation are every field
 * up to defaultStart (offset 0x64): the stage's pointers and the flags menuOpen,
 * frozen, busy, battleStarting and acting. FieldState holds seven pointers, so
 * on x86-64 those flags sit at byte 108..128 (battleStarting at 124, measured
 * with offsetof) and the 100 bytes miss them. A flag set by one field then
 * survives into the next: FIELDSTG_startEncounter's battleStarting = 1 stayed
 * set after a battle, so the next field refused to move the player and its
 * LEAVE (WSTAG200's event 52 to the Digimon Lab) ran the battle transition
 * instead of the field's close. The host clears the same fields, up to
 * defaultStart, by name instead of by a PlayStation byte count.
 *
 * Keep this file in sync with the upstream unit. pc/Makefile drops that
 * upstream unit from the field-mode glob and compiles this one in its place.
 */

/* FieldState's methods: the file and width of a character, the stage of
   the mode and the battles of a place */

#include "field/field_mode.h"
#include <stddef.h>

/* The file entry index of FIELDSTG_fileEntries */
s32 FIELDSTG_getFileEntry(s32 index) {
    return FIELDSTG_fileEntries[index];
}

/* The width of character index */
s32 FIELDSTG_getActorWidth(s32 index) {
    return FIELDSTG_actorWidths[index];
}

/* Picks the stage overlay of the mode (FIELDSTG_stages, in the European
   version's extra chapter FIELDSTG_extraStages and then those): its file and
   its setup; hangs if there is none */
void FIELDSTG_pickStage(void) {
    StageEntry *entry;
    s32 mode;

#if VERSION_US
    entry = FIELDSTG_stages;
#elif VERSION_EU
    if (GAME.progress != FIELD_PROGRESS_EXTRA) {
        entry = FIELDSTG_stages;
    } else {
        entry = FIELDSTG_extraStages;
    }
#endif
    mode = GAME.funcs.getMode();
    HEAP.zero(&FIELDSTG_state, offsetof(FieldState, defaultStart)); /* host: was 100 */
    /* host, same hook as src/field/field_mode/state.c (new-stages T4): a mode of the stages mods add (0x300-0x3FF) has no row below; the unit this file
       replaces got the hook, this copy did not, so a mode of that range ran into the endless loop at the end (the "mode:3FF" harness hang) */
    if (pcModStagePick(mode, &FIELDSTG_state) != 0) {
        return;
    }
    while (1) {
        if (entry->mode == mode) {
            FIELDSTG_state.stageFile = entry->file;
            FIELDSTG_state.stageInit = entry->init;
            break;
        }
        if ((++entry)->mode == 0) {
            break;
        }
    }
    if (entry->mode == 0) {
        while (1) {
        }
    }
}

/* The battles of the place id in a stage's list, or NULL */
FieldBattles *FIELDSTG_findBattles(FieldBattles *list, s32 id) {
    s32 i;

    for (i = 0; i < 30; i++) {
        if (list->id == id) {
            return list;
        }
        list++;
    }
    return NULL;
}
