#!/bin/sh
# Builds the Windows installer, pc/build-win/DW3Port-Setup.exe, from the current
# Windows build (make -C pc TARGET=windows, plus CONSOLE=1 for DW3-debug.exe).
#
#   pc/win/make_installer.sh [version]
#
# Needs makensis (NSIS 3). Without a system install, an extracted copy works:
#   apt-get download nsis nsis-common; dpkg -x <each .deb> <dir>
#   MAKENSIS=<dir>/usr/bin/makensis NSISDIR=<dir>/usr/share/nsis pc/win/make_installer.sh
# The installer carries the port only: never the disc, a memory card or settings.
set -e
cd "$(dirname "$0")/../.."
version="${1:-beta}"
makensis="${MAKENSIS:-makensis}"
build=pc/build-win
stage="$build/installer-stage"
cache="${DW3_WIN_CACHE:-$HOME/.cache/dw3-win}"

# the DLLs come from the dependency cache pc/win/fetch_deps.sh fills (the build links against them there)
for f in "$build/boot.exe" "$build/boot-console.exe" "$cache/sdl2/bin/SDL2.dll" "$cache/openal/bin/OpenAL32.dll"; do
    [ -f "$f" ] || { echo "missing $f: run make -C pc TARGET=windows (and CONSOLE=1)" >&2; exit 1; }
done

rm -rf "$stage"
mkdir -p "$stage/licenses" "$stage/localization"
cp "$build/boot.exe" "$stage/DW3.exe"
cp "$build/boot-console.exe" "$stage/DW3-debug.exe"
cp "$cache/sdl2/bin/SDL2.dll" "$cache/openal/bin/OpenAL32.dll" "$stage/"
cp LICENSE "$stage/licenses/DW3port-MIT.txt"
decomp_license=deps/dw3_decomp/LICENSE  # the public repo's dependency checkout; here, the root LICENSE is it
[ -f "$decomp_license" ] || decomp_license=LICENSE
cp "$decomp_license" "$stage/licenses/dw3_decomp-MIT.txt"
cp external/psycross/LICENSE "$stage/licenses/PsyCross-MIT.txt"
cp pc/win/licenses/SDL2-zlib.txt "$stage/licenses/"
cp "$cache/openal/COPYING" "$stage/licenses/OpenAL-Soft-LGPL.txt"
cp third_party/tinyfiledialogs/LICENSE.txt "$stage/licenses/tinyfiledialogs-zlib.txt"
cp pc/release/README.txt pc/release/LEEME.txt pc/release/THIRD-PARTY.txt "$stage/"
cp localization/layout.json "$stage/localization/"
mkdir -p "$stage/localization/es-419"
cp localization/es-419/*.json "$stage/localization/es-419/"
mkdir -p "$stage/art"
python3 pc/win/installer_art.py pc/win/installer.png "$stage/art"

"$makensis" -V2 -DSTAGE="$PWD/$stage" -DOUTFILE="$PWD/$build/DW3Port-Setup.exe" \
    -DVERSION="$version" pc/win/installer.nsi
echo "installer: $build/DW3Port-Setup.exe"
