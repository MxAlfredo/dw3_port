#ifndef PC_STGMCARD_WRAP_H
#define PC_STGMCARD_WRAP_H

/*
 * Host wrapper of include/menus/memory_card.h (menu-modes M2), found first by
 * the memory card units' own -I (pc/Makefile's MCARD_*). The game reaches the
 * card functions through MEMCARD_SYSTEM, a cast of MEMCARD's address to a
 * struct that puts MEMCARD_FUNCS right after the 0x328-byte MemCard -- true of
 * the PS1 link order, false here: MemCard is bigger with 64-bit pointers and
 * the linker places the globals where it likes, so the cast read a NULL
 * function pointer (the first run's crash in STGMCARD_acceptCard). Everything
 * the screen does through it is `.funcs`, so the view below copies
 * MEMCARD_FUNCS in place of the pun.
 */
#include "../../../../include/menus/memory_card.h"

#undef MEMCARD_SYSTEM
static inline MemCardSystem *pcMemCardSystem(void) {
    static MemCardSystem view;

    view.funcs = MEMCARD_FUNCS;
    return &view;
}
#define MEMCARD_SYSTEM (*pcMemCardSystem())

#endif
