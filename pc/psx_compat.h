#ifndef PC_PSX_COMPAT_H
#define PC_PSX_COMPAT_H

/*
 * The few names the game's own C uses that PsyCross's headers spell
 * differently -- force-included (-include) ahead of the game's own headers
 * for the units that draw against PsyCross's libgpu/libgte.
 *
 * This file is ours (pc/), not the game's. The game's headers stay untouched
 * (the src/ and include/ trees) and the PsyCross submodule is never patched
 * (ADR-003); this is the host-side bridge between the two, the same role the
 * rest of pc/shim/ plays for HEAP, CD_READER and the task registry.
 *
 * PsyCross's libgpu.h uses the C11 file-scope `static_assert` macro (its own
 * PR adds the matching <assert.h>; the pinned submodule does not have it yet,
 * so the include is forced here). The unit is therefore compiled with C11.
 */

#include <assert.h>

/* libgte.h before libgpu.h: the packet names refer to the GTE types by name. */
#include <libgte.h>
#include <libgpu.h>

/*
 * PsyQ's libgte.h defines ONE (1.0 in the GTE's 12-bit fixed point);
 * PsyCross's libgte.h does not. Same value the retail header uses.
 */
#ifndef ONE
#define ONE 4096
#endif

/*
 * PsyQ's libgpu.h defines RECT (four shorts); PsyCross renamed it RECT16.
 * Same layout -- offset point (x, y) and (w, h).
 */
typedef RECT16 RECT;

/*
 * PsyQ declares both RotMatrixYXZ and RotMatrixYXZ_gte; PsyCross keeps only
 * the first. The _gte variant is the same Y-X-Z rotation through the GTE
 * registers, and PsyCross has no register variant, so the plain call is what
 * the name means on this host.
 */
#ifndef RotMatrixYXZ_gte
#define RotMatrixYXZ_gte RotMatrixYXZ
#endif

#endif /* PC_PSX_COMPAT_H */
