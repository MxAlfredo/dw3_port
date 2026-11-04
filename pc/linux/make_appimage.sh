#!/bin/sh
# Builds the Linux AppImage, pc/build-linux/DW3Port-x86_64.AppImage: one file that runs on any
# x86_64 desktop with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Mint 21, Fedora 36, ...).
#
#   pc/linux/make_appimage.sh
#
# The port is built inside pc/linux/Dockerfile (Ubuntu 22.04) into its own trees (TARGET=appimage:
# pc/build-linux, build/psycross-linux), against SDL2 and OpenAL Soft built from source (the
# Windows build's versions), which the AppImage carries; OpenGL and the C/C++ runtimes are the
# system's. Downloads are pinned and sha256-checked into ~/.cache/dw3-linux (DW3_LINUX_CACHE).
# The AppImage carries the port only: never the disc, a memory card or settings.
set -e
cd "$(dirname "$0")/../.."
cache="${DW3_LINUX_CACHE:-$HOME/.cache/dw3-linux}"
out=pc/build-linux
appdir="$out/AppDir"
image=dw3port-appimage

mkdir -p "$cache/dl" "$out"
fetch() { # url sha file
    f="$cache/dl/$3"
    [ -f "$f" ] || curl -fsSL -o "$f" "$1"
    echo "$2  $f" | sha256sum -c --quiet - || { echo "make_appimage: sha256 mismatch for $3" >&2; rm -f "$f"; exit 1; }
}
fetch https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz \
    5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 SDL2-2.32.10.tar.gz
fetch https://github.com/kcat/openal-soft/releases/download/1.24.3/openal-soft-1.24.3.tar.bz2 \
    cb5e6197a1c0da0edcf2a81024953cc8fa8545c3b9474e48c852af709d587892 openal-soft-1.24.3.tar.bz2
fetch https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
    ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 appimagetool-x86_64.AppImage
# the AppImage's own runtime, pinned too (appimagetool would otherwise fetch the moving "continuous" one)
fetch https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
    2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d runtime-x86_64-20251108

docker build -q -t "$image" pc/linux >/dev/null
docker run --rm -v "$PWD:/dw3" -v "$cache:/deps" -w /dw3 -u "$(id -u):$(id -g)" -e HOME=/tmp \
    "$image" sh pc/linux/build_in_container.sh /deps

rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/lib" "$appdir/usr/share/dw3port/localization/es-419" \
    "$appdir/usr/share/doc/dw3port" "$appdir/usr/share/applications" \
    "$appdir/usr/share/icons/hicolor/256x256/apps"
cp "$out/boot" "$appdir/usr/bin/dw3port"
cp -L "$cache/prefix/lib/libSDL2-2.0.so.0" "$cache/prefix/lib/libopenal.so.1" "$appdir/usr/lib/"
docker run --rm -v "$PWD:/dw3" -w /dw3 -u "$(id -u):$(id -g)" "$image" \
    patchelf --set-rpath '$ORIGIN/../lib' "$appdir/usr/bin/dw3port"

cp localization/layout.json "$appdir/usr/share/dw3port/localization/"
cp localization/es-419/*.json "$appdir/usr/share/dw3port/localization/es-419/"
cp pc/release/README.txt pc/release/LEEME.txt pc/release/THIRD-PARTY.txt "$appdir/usr/share/doc/dw3port/"
cp LICENSE "$appdir/usr/share/doc/dw3port/DW3port-MIT.txt"
decomp_license=deps/dw3_decomp/LICENSE  # the public repo's dependency checkout; here, the root LICENSE is it
[ -f "$decomp_license" ] || decomp_license=LICENSE
cp "$decomp_license" "$appdir/usr/share/doc/dw3port/dw3_decomp-MIT.txt"
cp external/psycross/LICENSE "$appdir/usr/share/doc/dw3port/PsyCross-MIT.txt"
cp pc/win/licenses/SDL2-zlib.txt "$appdir/usr/share/doc/dw3port/"
tar -xjf "$cache/dl/openal-soft-1.24.3.tar.bz2" -O openal-soft-1.24.3/COPYING \
    > "$appdir/usr/share/doc/dw3port/OpenAL-Soft-LGPL.txt"
cp third_party/tinyfiledialogs/LICENSE.txt "$appdir/usr/share/doc/dw3port/tinyfiledialogs-zlib.txt"

python3 -c "from PIL import Image; Image.open('pc/win/installer.png').convert('RGB').resize((256, 256), Image.LANCZOS).save('$appdir/usr/share/icons/hicolor/256x256/apps/dw3port.png')"
cp "$appdir/usr/share/icons/hicolor/256x256/apps/dw3port.png" "$appdir/dw3port.png"
cat > "$appdir/usr/share/applications/dw3port.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=DW3 Port
Comment=Digimon World 3 PC port (needs your own disc)
Exec=dw3port
Icon=dw3port
Categories=Game;
Terminal=false
EOF
cp "$appdir/usr/share/applications/dw3port.desktop" "$appdir/"
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
# The game finds its es-419 overlay inside the AppImage unless the player points elsewhere
here="$(dirname "$(readlink -f "$0")")"
export DW3_L10N_DIR="${DW3_L10N_DIR:-$here/usr/share/dw3port/localization/es-419}"
exec "$here/usr/bin/dw3port" "$@"
EOF
chmod +x "$appdir/AppRun" "$cache/dl/appimagetool-x86_64.AppImage"

ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$cache/dl/appimagetool-x86_64.AppImage" -n \
    --runtime-file "$cache/dl/runtime-x86_64-20251108" "$appdir" \
    "$out/DW3Port-x86_64.AppImage"
echo "appimage: $out/DW3Port-x86_64.AppImage"
