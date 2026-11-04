#!/bin/sh
# Builds every beta download into pc/build-release/<version>/:
#   DW3Port-<version>-Setup.exe            Windows installer (pc/win/make_installer.sh)
#   DW3Port-<version>-windows-portable.zip Windows portable folder (portable.txt, a game/ folder for the disc)
#   DW3Port-<version>-x86_64.AppImage      Linux (pc/linux/make_appimage.sh)
#   DW3Port-<version>-android.apk          Android, signed with the release key (pc/android/build.sh release; key kept outside the repo)
#   SHA256SUMS, README.txt, LEEME.txt (the players' guide, also inside every download)
#
#   pc/make_release.sh <version>        e.g. pc/make_release.sh 0.1.0-beta
#
# Needs what the scripts need (MinGW-w64, makensis or MAKENSIS/NSISDIR, Docker). The unstripped libmain.so of the APK (crash
# symbolization) stays in pc/build-android-symbols/<version>/, with its build id, and is not part of any download. The downloads carry
# the port only: the script fails if a disc image, a memory card or a settings file got into one.
set -e
cd "$(dirname "$0")/.."
version="$1"
[ -n "$version" ] || { echo "usage: pc/make_release.sh <version>" >&2; exit 2; }
out="pc/build-release/$version"
stage=pc/build-release/portable-stage
name="DW3Port-$version"

rm -rf "$out" "$stage"
mkdir -p "$out"

# Windows: the player build, the console build, then the installer from both
make -C pc TARGET=windows -j"$(nproc)" >/dev/null
make -C pc TARGET=windows CONSOLE=1 -j"$(nproc)" >/dev/null
pc/win/make_installer.sh "$version"
cp pc/build-win/DW3Port-Setup.exe "$out/$name-Setup.exe"

# Windows portable: the installer's files plus portable.txt and an empty game/ for the disc
mkdir -p "$stage/$name"
cp -r pc/build-win/installer-stage/. "$stage/$name/"
rm -rf "$stage/$name/art"
cat > "$stage/$name/portable.txt" <<'EOF'
This file turns on portable mode: settings, saves and logs stay in this folder.
Este archivo activa el modo portátil: ajustes, partidas y registros se guardan en esta carpeta.
EOF
mkdir -p "$stage/$name/game"
cat > "$stage/$name/game/PUT-YOUR-DISC-HERE.txt" <<'EOF'
Put your own Digimon World 3 (Europe) disc image here (.cue + .bin, or .bin): the game finds it by itself.
Pon aquí la imagen de tu propio disco de Digimon World 3 (Europa) (.cue + .bin, o .bin): el juego la encuentra solo.
EOF
(cd "$stage" && zip -qr9 "../$version/$name-windows-portable.zip" "$name")

# Linux
pc/linux/make_appimage.sh
cp pc/build-linux/DW3Port-x86_64.AppImage "$out/$name-x86_64.AppImage"

# Android: the signed release APK (build.sh checks the signature and the 16 KB alignment)
pc/android/build.sh release "$version"
cp pc/android/app/build/outputs/apk/release/app-release.apk "$out/$name-android.apk"

# nothing retail: no disc image, memory card or settings in any download
retail='\.(bin|cue|iso|img|mcd|mcr)$|settings\.ini$'
if unzip -Z1 "$out/$name-windows-portable.zip" | grep -Ei "$retail"; then
    echo "make_release: the portable zip carries a disc, card or settings file" >&2; exit 1
fi
if unzip -Z1 "$out/$name-android.apk" | grep -Ei "$retail"; then
    echo "make_release: the APK carries a disc, card or settings file" >&2; exit 1
fi
if find pc/build-win/installer-stage pc/build-linux/AppDir -type f | grep -Ei "$retail"; then
    echo "make_release: the installer or the AppImage carries a disc, card or settings file" >&2; exit 1
fi

(cd "$out" && sha256sum "$name"-* > SHA256SUMS)
cp pc/release/README.txt pc/release/LEEME.txt "$out/"
rm -rf "$stage"
ls -l "$out"
