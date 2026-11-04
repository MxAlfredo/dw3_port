/*
 * Host-side proof for T11: a plain C11 consumer of PsyCross's headers, built
 * and linked through our pc/psycross/CMakeLists.txt.
 *
 * It pins the two things the wiring has to get right:
 *
 *   - the packet sizes come from PsyCross's host-safe layout (P_TAG carries a
 *     uintptr_t and P_LEN is 3), not from the retail Psy-Q 4.7 layout (P_TAG 8
 *     bytes, POLY_F3 24). If the build reached the wrong headers, the
 *     file-scope static_asserts below fail at compile time;
 *   - the static library really links: fst_abs() lives in the C-compiled
 *     src/psx/ABS.C and is called here.
 *
 * <assert.h> must precede <libgpu.h>: the latter uses the C11 static_assert
 * macro at file scope. <libgte.h> must precede <libgpu.h>: libgpu.h refers to
 * SVECTOR and the rest of the GTE types by name.
 */

#include <assert.h>
#include <stdio.h>

#include <abs.h>
#include <libgte.h>
#include <libgpu.h>

static_assert(sizeof(P_TAG) == 16, "P_TAG must be 16 bytes on this host");
static_assert(sizeof(OT_TAG) == 12, "OT_TAG must be 12 bytes on this host");
static_assert(sizeof(POLY_F3) == 28, "POLY_F3 must be 28 bytes on this host");

int main(void)
{
	printf("sizeof(P_TAG)=%zu sizeof(OT_TAG)=%zu sizeof(POLY_F3)=%zu P_LEN=%d\n",
	       sizeof(P_TAG), sizeof(OT_TAG), sizeof(POLY_F3), (int)P_LEN);

	/* Linked out of the static library (src/psx/ABS.C, compiled as C). */
	if (fst_abs(-5) != 5) {
		fprintf(stderr, "fst_abs(-5) != 5\n");
		return 1;
	}

	/* The retail 4.7 layout would give P_TAG 8 and POLY_F3 24. */
	if (sizeof(P_TAG) != 16 || sizeof(OT_TAG) != 12 || sizeof(POLY_F3) != 28) {
		fprintf(stderr, "packet layout is not PsyCross's host layout\n");
		return 1;
	}

	printf("ok: PsyCross host packet layout\n");
	return 0;
}
