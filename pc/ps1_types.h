#ifndef PC_PS1_TYPES_H
#define PC_PS1_TYPES_H

/*
 * The one type width the host does not have by default: `u_long`.
 *
 * On the PlayStation `u_long` is 32 bits; on this host it is 64, because both
 * <sys/types.h> and PsyCross's psx/types.h spell it `unsigned long`. The game's
 * TIM loader (src/engine/gfx/tim_loader.c) walks a TIM's words through a
 * `u_long *` (the TimCursor union in include/engine/gfx.h): with 64-bit longs
 * the cursor lands 8 bytes ahead of where the PS1 code means it to, the image
 * block it reads is not the TIM's, and the font never reaches VRAM (T14, first
 * attempt: the two VRAM regions came back as 32768 zero bytes each).
 *
 * This header is force-included ahead of that unit (before psx_compat.h, which
 * is what pulls psx/types.h), so `u_long` means there what it means on the PS1.
 * It is the same trick as the rest of pc/: the game's own source is not edited
 * (ADR-003) and neither is the PsyCross submodule; the host side adjusts.
 *
 * <sys/types.h> goes first so that its `typedef __u_long u_long;` has already
 * happened; the macro then makes every later use -- and the type PsyCross's
 * psx/types.h would redefine -- resolve to the 32-bit `u_int`.
 */
#include <sys/types.h>

#ifndef _ULONG_T
#define _ULONG_T 1 /* keeps psx/types.h from typedef'ing u_long again */
#endif

#define u_long u_int

#endif /* PC_PS1_TYPES_H */
