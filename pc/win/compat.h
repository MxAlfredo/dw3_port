#ifndef PC_WIN_COMPAT_H
#define PC_WIN_COMPAT_H

/*
 * MinGW-w64 stand-ins for what glibc's <sys/types.h> and <GL/gl.h> give the Linux build.
 * Force-included first (-include) in every TARGET=windows compile of pc/, so neither the
 * game's headers (src/, include/) nor the Psy-Q ones need an edit.
 *
 * - u_char/u_short/u_int/u_long: win/bsd_types.h.
 * - APIENTRY/WINGDIAPI: MinGW's <GL/gl.h> includes <windows.h> unless they are defined, and
 *   windows.h's RECT clashes with libgpu's. The fixed values are what windows.h would provide.
 */
#include "bsd_types.h"

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef WINGDIAPI
#define WINGDIAPI __declspec(dllimport)
#endif
#ifndef CALLBACK
#define CALLBACK __stdcall
#endif

#endif /* PC_WIN_COMPAT_H */
