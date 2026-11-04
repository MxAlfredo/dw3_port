#ifndef PC_WIN_BSD_TYPES_H
#define PC_WIN_BSD_TYPES_H

/*
 * The BSD integer names glibc's <sys/types.h> gives the Linux build and MinGW's does not; the
 * Psy-Q headers rely on them. Force-included first in every TARGET=windows compile, in the game's
 * units (via win/compat.h) and in PsyCross's own CMake build (pc/psycross/CMakeLists.txt), so both
 * sides of the library agree on the width of u_long.
 *
 * u_long is 64 bits here (`unsigned long long`, which is what uintptr_t is on Win64): on Linux
 * glibc's u_long is `unsigned long`, 64 bits, and the port depends on it being pointer-sized
 * (the ordering tables hold full pointers, include/engine/gfx.h and pc/shim/gfx_engine.c). MinGW's
 * `unsigned long` is 32 bits (LLP64), which would truncate those pointers. Units that need the
 * PlayStation's 32-bit u_long still get it from pc/ps1_types.h, on both targets.
 * The _UCHAR_T family of guards is the one PsyCross's psx/types.h uses, so no name is typedef'd twice.
 */
#include <sys/types.h>

#ifndef _UCHAR_T
#define _UCHAR_T
typedef unsigned char u_char;
#endif
#ifndef _USHORT_T
#define _USHORT_T
typedef unsigned short u_short;
#endif
#ifndef _UINT_T
#define _UINT_T
typedef unsigned int u_int;
#endif
#ifndef _ULONG_T
#define _ULONG_T
typedef unsigned long long u_long;
#endif

#endif /* PC_WIN_BSD_TYPES_H */
