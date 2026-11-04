/*
 * The host's replacement for the game's overlay loader (T19).
 *
 * On the PlayStation the modes and the field's stages are MIPS overlays: the
 * executable keeps OVERLAY_LOADER (src/engine/system/overlay.c) and, when a
 * mode is entered, copies its .PRO from the disc into OVERLAY_VRAM and jumps
 * into the copy's entry point. The port cannot do either -- that code is MIPS
 * and its addresses are fixed (0x80082CB0, 0x800A5DE4) -- so, as T15 already
 * decided for the modes, every mode and every stage is *linked* into the host
 * binary instead, and the dispatch is the port's own (pc/boot.c's
 * createModeTask reads GAME.funcs.getMode() >> 8 and calls the linked entry
 * point; the stages are linked and renamed WSTAG###_* by pc/Makefile).
 *
 * What is left for the game's own C is OVERLAY_LOADER itself: the field
 * (field.c's startField) reads it to load the stage of the mode it is
 * entering (`OVERLAY_LOADER.loadSubOverlay(FIELDSTG_state.stageFile)`). The
 * game's overlay.c is not part of the host path -- its updateModeTask copies
 * .PRO files and its MODE_ENTRY_POINTS names all 22 overlays, which would drag
 * in every mode this build does not have -- so the table and its two loaders
 * live here. The two loaders copy nothing: the overlay is already linked; what stands in for the
 * copy is the state scopes (shim/state_scope.h), reset by pc/boot.c on mode entry and by
 * loadSubOverlay below on a sub-overlay's first load. The
 * stage one says which stage the game asked for, so a run shows which one it
 * entered, and says plainly that nothing was copied.
 *
 * This is the same shape as the other host shims (HEAP, CD_READER, the task
 * registry): the game's own source is not touched (ADR-003) and neither is the
 * PsyCross submodule; the host side stands in for what the PlayStation
 * provides.
 */
#include "engine/game.h"
#include "engine/overlay.h"
#include "state_scope.h"

#include <stdio.h>
#include "port_log.h"

/* Loads the current mode's overlay. The mode is already linked and its entry
   point was already called by pc/boot.c's createModeTask, so there is nothing
   to copy and nothing to say. */
void loadModeOverlay(void) {
}

/* Loads a stage overlay (FIELDSTG_state.stageFile). The stage is already
   linked (build/stages/), so nothing is copied; the line reports which stage
   the game's own code entered. */
void loadSubOverlay(s32 file) {
    /* overlay.c's loadSubOverlay copies only when the file changes: that is when its scope resets */
    stateScopeSubLoaded((int)file);
    PLOG("overlay", PLOG_TRACE, "overlay: the game loads stage overlay 0x%X; this build has it linked, nothing is copied\n",
         (unsigned)file);
}

/* The loader the game's own C reads (field.c's startField). mode and
   subOverlay are the fields overlay.c's loaders keep; nothing else in the
   host path reads them, so they start empty. */
OverlayLoader OVERLAY_LOADER = {0, 0, loadModeOverlay, loadSubOverlay};

/* CARDGAME_markedCounts is not defined by the card battle's C: on the
   PlayStation it is a linker symbol 2 bytes past CARDGAME_slotMoveCounts
   (config/<version>/undefined_syms_cardgame.txt). Nothing indexes the pair as
   one object, so on the host it is a variable of its own. */
#include "cardgame/card_battle.h"
/* in the card game's scope (pc/ovl_sections.ld): its bytes belong to the overlay, not to the executable */
u8 CARDGAME_markedCounts[2] __attribute__((section(".bss.ovl_cardgame")));
