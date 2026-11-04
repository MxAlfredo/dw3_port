#ifndef GTE_H
#define GTE_H

/*
 * Host stand-in for the game's own engine/gte.h (the battle build's include
 * path shadows it, Makefile's BATTLE_INC; ADR-003: the game's source stays).
 *
 * The PS1 header spells the GTE's primitive instructions as MIPS assembler
 * inlined in the macro bodies (the real encodings DMPSX patches in); PsyCross
 * spells exactly the same primitive names for its host-emulated GTE
 * (external/psycross's inline_c.h, whose doCOP2 runs the processor work the
 * PS1's cop2 does). The include shape stays the original's: the primitives
 * first, then gtemac.h, which builds the composite macros on them in both
 * worlds. The battle's three GTE units (battle_mode/draw.c, interp.c and
 * model_mesh.c) are the ones that name these primitives; the port's other
 * linked units never include the original header -- the field's stages were
 * measured to name no gte_* call at all (pc/README.md, T19).
 */

#include <inline_c.h>

/* battle-entry T3: PsyCross spells several of the GTE primitives as "load
   the word the argument points at", but the game's own header
   (include/engine/gte.h) spells them as `mtc2 %0, $N`: the argument IS the
   value. The battle calls four of them with values --
     ldp  (interp.c's t is 0-0x1000, model_mesh.c's blends; ??:?, 0x88 crash
           in FIGHTSTG_lerpVector),
     ldsxy3 (drawMeshPolygons passes the projected sxy words; crash in
           drawMeshPolygons deref'ing a packed sxy as a pointer),
     ldsz3/ldsz4 (drawMeshPolygons passes the otz/depth words, the
           gte_AverageZ composites' own contract)
   -- and the SDK's composites pass values too (gtemac.h's r2 of
   gte_DpqColor). Keeping PsyCross's forms turns those keeps into reads of
   small garbage addresses, so the shadow restores the original's value
   semantics here, before gtemac.h builds on them.

   battle-entry T5: PsyCross's gte_ldlv0 has its own transcription slip. The
   retail instruction is `mtc2 $12, $0` -- general register 12 into DATA
   register 0 (include/engine/gte.h:139-150) -- and the host spelling has no
   general register (the packed low halves go straight in), so it kept the
   12 as the DATA register number and writes V0's vy/vx pair into $12, which
   the composites never read (SXY0), while gte_rt() composes with whatever
   stale data register $0 holds. gte_CompMatrix's translation step
   (gtemac.h:351: ldlv0((char*)r2+20) then rt()) thus lands every bone and
   stage part at parent.t + rot*(leftover V0) instead of + rot*its OWN
   translation: model parts scattered around their parents with their
   textures intact. The shadow writes into $0 and takes vz as the full word
   the retail lwc2 copies (t[2] is a 32-bit int), before gtemac.h builds on
   this one too. */
#undef gte_lddp
#define gte_lddp(r0)                                                            \
	{	MTC2((unsigned int)(r0), 8); }
#undef gte_ldsxy3
#define gte_ldsxy3(r0, r1, r2)                                                  \
	{	MTC2((unsigned int)(r0), 12);                                           \
		MTC2((unsigned int)(r2), 14);                                           \
		MTC2((unsigned int)(r1), 13); }
#undef gte_ldsz3
#define gte_ldsz3(r0, r1, r2)                                                   \
	{	MTC2((unsigned int)(r0), 17);                                           \
		MTC2((unsigned int)(r1), 18);                                           \
		MTC2((unsigned int)(r2), 19); }
#undef gte_ldsz4
#define gte_ldsz4(r0, r1, r2, r3)                                               \
	{	MTC2((unsigned int)(r0), 16);                                           \
		MTC2((unsigned int)(r1), 17);                                           \
		MTC2((unsigned int)(r2), 18);                                           \
		MTC2((unsigned int)(r3), 19); }
#undef gte_ldlv0
#define gte_ldlv0(r0)                                                           \
	{	MTC2((unsigned int)((*(unsigned short*)((char*)(r0)+4) << 16)           \
	                            | *(unsigned short*)((char*)(r0))),            \
	            0);                                                            \
		MTC2(*(unsigned int*)((char*)(r0)+8), 1); }

#include <gtemac.h>

/* The host's byte loads are unaligned-safe, so the game's two-load lwc2
   variant of gte_ldv0 (engine/gte.h's gte_ldv0_unaligned, which the original
   keeps for a sprite position that need not be halfword aligned; draw.c's
   FIGHTSTG_projectPoint names it) is PsyCross's plain gte_ldv0: both load
   V0's two words out of the caller's layout. */
#define gte_ldv0_unaligned(r0) gte_ldv0(r0)

#endif /* GTE_H */
