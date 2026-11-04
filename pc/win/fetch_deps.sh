#!/bin/sh
# Fetch the Windows (MinGW-w64) development packages of SDL2 and OpenAL Soft into
# ~/.cache/dw3-win (override with DW3_WIN_CACHE). Pinned versions, sha256 checked, never committed.
# The hashes were recorded at first download of the official release assets (trust on first use).
# Prints nothing but errors; on success the layout is:
#   $CACHE/sdl2/{include,lib,bin}/...      (x86_64-w64-mingw32 part of the SDL2 devel tarball)
#   $CACHE/openal/{include,lib,bin}/...    (Win64 part of the OpenAL Soft bin zip)
set -eu
CACHE="${DW3_WIN_CACHE:-$HOME/.cache/dw3-win}"
SDL_VER=2.32.10
SDL_SHA=83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2
AL_VER=1.24.3
AL_SHA=03c8c0c0bcdba9d3fb54b0d3c2f9a565f81b3e2a19a538dd394f9b8cb6caaa22
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-devel-$SDL_VER-mingw.tar.gz"
AL_URL="https://github.com/kcat/openal-soft/releases/download/$AL_VER/openal-soft-$AL_VER-bin.zip"

mkdir -p "$CACHE/dl"
fetch() { # url sha file
    f="$CACHE/dl/$3"
    if [ ! -f "$f" ]; then curl -sSLf -o "$f.part" "$1" && mv "$f.part" "$f"; fi
    echo "$2  $f" | sha256sum -c --quiet - || { echo "fetch_deps: sha256 mismatch for $3" >&2; rm -f "$f"; exit 1; }
}
fetch "$SDL_URL" "$SDL_SHA" "SDL2-devel-$SDL_VER-mingw.tar.gz"
fetch "$AL_URL" "$AL_SHA" "openal-soft-$AL_VER-bin.zip"

if [ ! -f "$CACHE/sdl2/.ok-$SDL_VER" ]; then
    rm -rf "$CACHE/sdl2" "$CACHE/sdl2.tmp"; mkdir -p "$CACHE/sdl2.tmp"
    tar -xzf "$CACHE/dl/SDL2-devel-$SDL_VER-mingw.tar.gz" -C "$CACHE/sdl2.tmp"
    mv "$CACHE/sdl2.tmp/SDL2-$SDL_VER/x86_64-w64-mingw32" "$CACHE/sdl2"
    rm -rf "$CACHE/sdl2.tmp"; touch "$CACHE/sdl2/.ok-$SDL_VER"
fi
if [ ! -f "$CACHE/openal/.ok-$AL_VER" ]; then
    rm -rf "$CACHE/openal" "$CACHE/openal.tmp"; mkdir -p "$CACHE/openal.tmp"
    unzip -q "$CACHE/dl/openal-soft-$AL_VER-bin.zip" -d "$CACHE/openal.tmp"
    d="$CACHE/openal.tmp/openal-soft-$AL_VER-bin"
    mkdir -p "$CACHE/openal/bin" "$CACHE/openal/lib"
    cp -r "$d/include" "$CACHE/openal/include"
    cp "$d/bin/Win64/soft_oal.dll" "$CACHE/openal/bin/OpenAL32.dll"
    cp "$d/libs/Win64/libOpenAL32.dll.a" "$CACHE/openal/lib/libOpenAL32.dll.a"
    cp "$d/COPYING" "$CACHE/openal/" 2>/dev/null || true
    rm -rf "$CACHE/openal.tmp"; touch "$CACHE/openal/.ok-$AL_VER"
fi
echo "$CACHE"
