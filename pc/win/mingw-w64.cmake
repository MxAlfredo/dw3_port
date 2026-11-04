# CMake toolchain file: cross-build PsyCross (pc/psycross) for 64-bit Windows with MinGW-w64.
# Usage (the Makefile does this for TARGET=windows):
#   pc/win/fetch_deps.sh
#   cmake -S pc/psycross -B build/psycross-win -DCMAKE_TOOLCHAIN_FILE=pc/win/mingw-w64.cmake
# The SDL2 and OpenAL development packages come from ~/.cache/dw3-win (or $DW3_WIN_CACHE).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_triple x86_64-w64-mingw32)
# The -posix variants: winpthreads and C++ threads work only with them.
set(CMAKE_C_COMPILER ${_triple}-gcc-posix)
set(CMAKE_CXX_COMPILER ${_triple}-g++-posix)
set(CMAKE_RC_COMPILER ${_triple}-windres)
set(CMAKE_AR ${_triple}-gcc-ar-posix CACHE FILEPATH "")
set(CMAKE_RANLIB ${_triple}-gcc-ranlib-posix CACHE FILEPATH "")

if(DEFINED ENV{DW3_WIN_CACHE})
    set(DW3_WIN_CACHE "$ENV{DW3_WIN_CACHE}")
else()
    set(DW3_WIN_CACHE "$ENV{HOME}/.cache/dw3-win")
endif()

set(CMAKE_FIND_ROOT_PATH /usr/${_triple} ${DW3_WIN_CACHE}/sdl2 ${DW3_WIN_CACHE}/openal)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Pre-seed what FindOpenAL and the SDL2 config package would otherwise search for.
set(SDL2_DIR ${DW3_WIN_CACHE}/sdl2/lib/cmake/SDL2 CACHE PATH "")
set(OPENAL_INCLUDE_DIR ${DW3_WIN_CACHE}/openal/include CACHE PATH "")
set(OPENAL_LIBRARY ${DW3_WIN_CACHE}/openal/lib/libOpenAL32.dll.a CACHE FILEPATH "")
