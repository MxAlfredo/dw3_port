# Host build of PsyCross (external/psycross, MIT, Copyright REDRIVER2 Project)
# as a static library, from our own build file.
#
# PsyCross's own CMakeLists.txt does not configure on Linux, and neither bug is
# about a missing dependency:
#
#   1. it calls `find_package(OPENAL)`. CMake package names are case-sensitive
#      and this system ships OpenAL / FindOpenAL.cmake, so the working spelling
#      is `find_package(OpenAL)`.
#   2. it globs with `file(GLOB_RECURSE ... "*.c")`, which on a case-sensitive
#      filesystem silently drops the eleven `*.C` files that hold the Psy-Q
#      implementations (LIBGPU.C, LIBCD.C, LIBSPU.C, ...). On the author's
#      Windows filesystem the glob matched them; on Linux the library would be
#      built without its implementations.
#
# Per ADR-003 the submodule is never patched: this file is ours, it lists the
# sources explicitly, and it compiles the submodule's sources untouched.
#
# Usage (out of tree, from the repository root):
#   cmake -S pc/psycross -B build/psycross
#   cmake --build build/psycross

cmake_minimum_required(VERSION 3.16)

project(psycross_host LANGUAGES C CXX)

set(PSYCROSS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../external/psycross")
if(NOT EXISTS "${PSYCROSS_ROOT}/include/PsyX/PsyX_public.h")
    message(FATAL_ERROR "PsyCross submodule not found at ${PSYCROSS_ROOT}")
endif()

# Source language and dialect. The .C units that we do compile as C need C11
# because psx/libgpu.h uses the file-scope `static_assert` macro that <assert.h>
# defines from C11 on; the rest of the C units go through the same C11 dialect.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Sources, listed explicitly. This list is the fix for the case-sensitive glob.
# ---------------------------------------------------------------------------

# PSYX_FORCE_GLES=ON (pc/Makefile GLES=1): the OpenGL ES 3 renderer the Android build uses, on a desktop Linux
# build (PsyX_render.h picks RENDERER_OGLES). No glad then; the game links GLESv2 + EGL (the GL loader is the driver's).
option(PSYX_FORCE_GLES "Build the OpenGL ES 3 renderer on a desktop build" OFF)

# Plain C (.c).
set(PSYCROSS_SOURCE_C
    "${PSYCROSS_ROOT}/src/util/timer.c"
)
# The Android build (ANDROID, pc/Makefile TARGET=android) is ES 3 by itself (PsyX_render.h), without glad either.
if(NOT PSYX_FORCE_GLES AND NOT ANDROID)
  list(APPEND PSYCROSS_SOURCE_C "${PSYCROSS_ROOT}/src/render/glad.c")
endif()

# C++ (.cpp). Every unit with this extension is C++ in PsyCross's own build.
set(PSYCROSS_SOURCE_CXX
    "${PSYCROSS_ROOT}/src/PsyX_main.cpp"
    "${PSYCROSS_ROOT}/src/audio/PsyX_SPUAL.cpp"
    "${PSYCROSS_ROOT}/src/gpu/PsyX_GPU.cpp"
    "${PSYCROSS_ROOT}/src/gte/half_float.cpp"
    "${PSYCROSS_ROOT}/src/gte/PsyX_GTE.cpp"
    "${PSYCROSS_ROOT}/src/pad/PsyX_pad.cpp"
    "${PSYCROSS_ROOT}/src/render/PsyX_render.cpp"
    "${PSYCROSS_ROOT}/src/util/crash_handler.cpp"
)

# The Psy-Q implementations: uppercase `.C`. CMake (like gcc) reads `.C` as C++
# by default; PsyCross's own build set every globbed `*.c`/`*.C` unit to C. We
# follow the library's intent, so these units are C except where the host C
# front end rejects the file (see PSYCROSS_SOURCE_PSX_CXX below).
set(PSYCROSS_SOURCE_PSX_C
    "${PSYCROSS_ROOT}/src/psx/ABS.C"
    "${PSYCROSS_ROOT}/src/psx/INLINE_C.C"
    "${PSYCROSS_ROOT}/src/psx/LIBCD.C"
    "${PSYCROSS_ROOT}/src/psx/LIBETC.C"
    "${PSYCROSS_ROOT}/src/psx/LIBMCRD.C"
    "${PSYCROSS_ROOT}/src/psx/LIBPAD.C"
    "${PSYCROSS_ROOT}/src/psx/LIBSN.C"
    "${PSYCROSS_ROOT}/src/psx/LIBSPU.C"
)

# The three `.C` units that must be C++ on this host, each for its own reason:
#   LIBGPU.C, LIBGTE.C — they reach psx/libgpu.h before <assert.h>, so the
#     file-scope `static_assert` is an unknown identifier to the C front end.
#     C++ has static_assert as a keyword, so they compile there.
#   LIBAPI.C — C rejects its `LoadTest`/`Load` definitions because the header
#     declares them with a `struct EXEC *` whose tag is scoped to the prototype;
#     C++ declares that tag at file scope, so the definition matches.
set(PSYCROSS_SOURCE_PSX_CXX
    "${PSYCROSS_ROOT}/src/psx/LIBAPI.C"
    "${PSYCROSS_ROOT}/src/psx/LIBGPU.C"
    "${PSYCROSS_ROOT}/src/psx/LIBGTE.C"
)

# LANGUAGE C tells CMake to drive these units with the C compiler, but the gcc
# driver still picks the front end from the extension, so `.C` would go to C++
# anyway; `-x c` forces the C front end like the library's own LANGUAGE C
# property intended. The three C++ units above need no flag: `.C` already means
# C++ to the driver.
set_source_files_properties(${PSYCROSS_SOURCE_PSX_C}
    PROPERTIES LANGUAGE C COMPILE_OPTIONS "-x;c")

add_library(psycross STATIC
    ${PSYCROSS_SOURCE_C}
    ${PSYCROSS_SOURCE_CXX}
    ${PSYCROSS_SOURCE_PSX_C}
    ${PSYCROSS_SOURCE_PSX_CXX}
)

set_target_properties(psycross PROPERTIES OUTPUT_NAME "psycross")

# ---------------------------------------------------------------------------
# Public interface: the two header directories a consumer needs.
# ---------------------------------------------------------------------------
#   include/     -> <PsyX/PsyX_public.h> and friends
#   include/psx/ -> <libgpu.h>, <libgte.h>, <libcd.h>, ...
target_include_directories(psycross PUBLIC "${PSYCROSS_ROOT}/include")

# include/psx has to be searchable but AFTER the system headers. PsyCross ships
# its own psx/strings.h (a Psy-Q name), which with a plain -I shadows glibc's
# <strings.h> and breaks PsyCross's own PsyX_main.cpp (strcasecmp). -idirafter
# keeps both working and is exported to consumers through PUBLIC.
target_compile_options(psycross PUBLIC "-idirafter" "${PSYCROSS_ROOT}/include/psx")

# ---------------------------------------------------------------------------
# Dependencies, spelled the way CMake can actually resolve them.
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)
find_package(OpenAL REQUIRED)
# FindOpenAL only defines OpenAL::OpenAL from CMake 3.25 on; older ones (the AppImage's
# Ubuntu 22.04 has 3.22) leave the variables, so the target is made from them.
if(NOT TARGET OpenAL::OpenAL)
  add_library(OpenAL::OpenAL UNKNOWN IMPORTED)
  set_target_properties(OpenAL::OpenAL PROPERTIES
    IMPORTED_LOCATION "${OPENAL_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}")
endif()

target_link_libraries(psycross PRIVATE SDL2::SDL2 OpenAL::OpenAL)
if(PSYX_FORCE_GLES)
  target_compile_definitions(psycross PUBLIC PSYX_FORCE_GLES)
endif()

# PsyX_GPU.cpp initialises the OT terminator as `{ -1, 0 }` with a uintptr_t P_TAG
# field; that is a narrowing conversion, which C++11 makes ill-formed and gcc
# rejects by default. PsyCross is written for compilers that only warn (MSVC,
# clang), so this is a host-compiler strictness switch, not a source change.
target_compile_options(psycross PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-narrowing>)

# ---------------------------------------------------------------------------
# VERTTYPE: the coordinates the primitives carry.
#
# The game's own drawing code is C and writes the PlayStation's integer
# coordinates straight into the primitives (`poly->x0 = pos.vx`, an s16). With
# PGXP on, PsyCross makes VERTTYPE a 16-bit float (half) for C++ consumers, so
# the parser reads those raw s16 bits as half-floats: any C game that does not
# wrap every coordinate in `_HF(x)` (pgxp_defs.h says so: "use `_HF` to
# convert to half whenever you in C mode") renders degenerate geometry. This
# port compiles the game's C unmodified, so the library is built with PGXP off:
# then VERTTYPE is `short` in both C and C++ (pgxp_defs.h's #else branch) and
# the parser reads exactly what the game writes.
#
# It does not change the packet layout: the extended prim pointers stay on, so
# P_TAG is still 16, OT_TAG 12 and P_LEN 3 (pc/psycross/check_packets.c), and
# the coordinate fields stay 2 bytes in every prim. What is dropped is the
# PGXP precision enhancement, which needs the game side to write _HF().
# ---------------------------------------------------------------------------
target_compile_definitions(psycross PUBLIC USE_PGXP=0)

# ---------------------------------------------------------------------------
# Acceptance check: a small C11 consumer that pins the host packet layout and
# links a symbol out of the C-compiled psx units.
# ---------------------------------------------------------------------------
if(NOT ANDROID)  # an executable cannot run on the Android build host anyway
add_executable(check_packets check_packets.c)
target_link_libraries(check_packets PRIVATE psycross)
set_target_properties(check_packets PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# ---------------------------------------------------------------------------
# MinGW-w64 (pc/win/mingw-w64.cmake): the submodule's Windows paths were written for MSVC, which
# pulls <windows.h> (PsyX_main.cpp's console colours, CRITICAL_SECTION) and <cmath> (sinf in
# PsyX_render.cpp) in transitively. Force-including them is the switch that keeps the submodule
# untouched; it applies to this library's own C++ units only.
# ---------------------------------------------------------------------------
if(WIN32)
    # u_long must be the 64-bit type the game's side uses (win/bsd_types.h explains why).
    target_compile_options(psycross PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/../win/bsd_types.h")
    # WIN32_LEAN_AND_MEAN keeps <winsock.h> (and its 32-bit u_long) out of <windows.h>.
    target_compile_definitions(psycross PRIVATE WIN32_LEAN_AND_MEAN)
    # Per file, not library-wide: <windows.h> would turn the Psy-Q names OpenEvent/CloseEvent
    # (libapi.h) into OpenEventA/..., breaking LIBAPI.C.
    set_source_files_properties("${PSYCROSS_ROOT}/src/PsyX_main.cpp"
        PROPERTIES COMPILE_OPTIONS "-include;windows.h;-include;cmath")
    set_source_files_properties("${PSYCROSS_ROOT}/src/render/PsyX_render.cpp"
        PROPERTIES COMPILE_OPTIONS "-include;cmath")
endif()
