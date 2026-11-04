#!/bin/sh
# Runs inside the pc/linux/Dockerfile image (make_appimage.sh starts it): builds SDL2 and OpenAL
# Soft once into $DEPS/prefix, then the port with TARGET=appimage against them.
#
#   build_in_container.sh <deps-dir>     (the repository is the working directory)
set -e
deps="$1"
prefix="$deps/prefix"
jobs=$(nproc)

if [ ! -f "$prefix/lib/libSDL2-2.0.so.0" ]; then
    rm -rf "$deps/src/sdl2" && mkdir -p "$deps/src/sdl2"
    tar -xzf "$deps/dl/SDL2-2.32.10.tar.gz" -C "$deps/src/sdl2" --strip-components=1
    cmake -S "$deps/src/sdl2" -B "$deps/src/sdl2/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib -DSDL_STATIC=OFF -DSDL_TEST=OFF
    cmake --build "$deps/src/sdl2/build" -j "$jobs"
    cmake --install "$deps/src/sdl2/build"
fi

if [ ! -f "$prefix/lib/libopenal.so.1" ]; then
    rm -rf "$deps/src/openal" && mkdir -p "$deps/src/openal"
    tar -xjf "$deps/dl/openal-soft-1.24.3.tar.bz2" -C "$deps/src/openal" --strip-components=1
    cmake -S "$deps/src/openal" -B "$deps/src/openal/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
        -DALSOFT_EXAMPLES=OFF -DALSOFT_UTILS=OFF -DALSOFT_TESTS=OFF
    cmake --build "$deps/src/openal/build" -j "$jobs"
    cmake --install "$deps/src/openal/build"
fi

# gcc and CMake find the prefix's headers and libraries ahead of the image's
export CMAKE_PREFIX_PATH="$prefix"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig"
export CPATH="$prefix/include:$prefix/include/SDL2"
export LIBRARY_PATH="$prefix/lib"
make -C pc TARGET=appimage -j"$jobs"
