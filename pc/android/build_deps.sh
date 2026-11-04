#!/bin/sh
# Runs inside the pc/android/Dockerfile image (build.sh starts it): cross-builds SDL2 and OpenAL Soft for
# arm64-v8a with the NDK into <deps>/prefix/arm64-v8a, once (it skips what is already there).
#
#   build_deps.sh <deps-dir>
#
# minSdk 29 (Android 10): NDK r27 supports 21+ and SDL2 2.32 19+, but below API 29 Bionic's <android/api-level.h>
# declares atoi() inline in every translation unit, which collides with the game's own `int atoi(u8 *)`
# (include/engine/game.h, upstream); from 29 on it does not, so the upstream header stays untouched. The test
# phone (Android 14/15) is far above it. OpenAL Soft uses its OpenSL ES backend: the NDK ships libOpenSLES, so
# no other source tree is needed (the Oboe backend would need Oboe's sources).
set -e
deps="$1"
abi=arm64-v8a
api=29
prefix="$deps/prefix/$abi"
jobs=$(nproc)
toolchain="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
# 16 KB pages (Android 15+): NDK r27 needs this switch to link with -z max-page-size=16384. The stamp makes a prefix built
# without it (older checkouts) rebuild once.
if [ ! -f "$prefix/.page16k" ]; then rm -f "$prefix/lib/libSDL2.so" "$prefix/lib/libopenal.so"; fi
common="-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -G Ninja -DCMAKE_TOOLCHAIN_FILE=$toolchain -DANDROID_ABI=$abi -DANDROID_PLATFORM=android-$api \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$prefix -DCMAKE_INSTALL_LIBDIR=lib"

if [ ! -f "$prefix/lib/libSDL2.so" ]; then
    rm -rf "$deps/src/sdl2-$abi" && mkdir -p "$deps/src/sdl2-$abi"
    tar -xzf "$deps/dl/SDL2-2.32.10.tar.gz" -C "$deps/src/sdl2-$abi" --strip-components=1
    # shellcheck disable=SC2086
    cmake -S "$deps/src/sdl2-$abi" -B "$deps/src/sdl2-$abi/build" $common \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
    cmake --build "$deps/src/sdl2-$abi/build" -j "$jobs"
    cmake --install "$deps/src/sdl2-$abi/build"
fi

if [ ! -f "$prefix/lib/libopenal.so" ]; then
    rm -rf "$deps/src/openal-$abi" && mkdir -p "$deps/src/openal-$abi"
    tar -xjf "$deps/dl/openal-soft-1.24.3.tar.bz2" -C "$deps/src/openal-$abi" --strip-components=1
    # shellcheck disable=SC2086
    cmake -S "$deps/src/openal-$abi" -B "$deps/src/openal-$abi/build" $common \
        -DLIBTYPE=SHARED -DALSOFT_EXAMPLES=OFF -DALSOFT_UTILS=OFF -DALSOFT_TESTS=OFF \
        -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF \
        -DALSOFT_BACKEND_OBOE=OFF -DALSOFT_REQUIRE_OPENSL=ON
    cmake --build "$deps/src/openal-$abi/build" -j "$jobs"
    cmake --install "$deps/src/openal-$abi/build"
fi
touch "$prefix/.page16k"
ls -l "$prefix/lib"/libSDL2.so "$prefix/lib"/libopenal.so
