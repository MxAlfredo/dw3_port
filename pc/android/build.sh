#!/bin/sh
# Drives the Android build in Docker.
#
#   pc/android/build.sh deps     the image, then SDL2 + OpenAL Soft for arm64-v8a (once)
#   pc/android/build.sh apk      deps, then the debug APK: pc/android/app/build/outputs/apk/debug/app-debug.apk
#   pc/android/build.sh release [version]
#                                deps, then the signed release APK (app/build/outputs/apk/release/app-release.apk), checked with
#                                apksigner and zipalign -P 16; version defaults to 0.0.1-dev (pc/make_release.sh passes the real one)
#
# Release signing key: kept OUTSIDE the repository, in DW3_KEY_DIR (default ~/.config/dw3port): android-release.keystore plus
# android-release.pass (0600). Created once, by keytool inside the container, if missing. BACK IT UP: an APK signed with another key
# cannot update an installed one, so losing it means every player must uninstall (losing their saves) to take a new release.
#
# versionName = the version string; versionCode = MAJOR*1000000 + MINOR*10000 + PATCH*100 + stage, with stage 99 for a final
# release, 50+N for -rcN, 20+N for -betaN, and N for -alphaN / -devN (N from 0 to 29; -beta alone is -beta0). So 0.6.0-beta <
# 0.7.0-rc1 < 0.7.0 and any later release is a higher code (MINOR and PATCH below 100).
#
# ANDROID_OPT (DW3_ANDROID_OPT) stays empty by default: the owner measured a steady 60 FPS without -O2 on a Poco X7, and -O2 on this
# decompiled C could change behavior (undefined behavior it relied on under PS1 gcc) for no gain. Pass DW3_ANDROID_OPT=-O2 to try it.
#
# The APK carries the port only (libmain.so, SDL2, OpenAL Soft, SDL's Java glue, the es-419 overlay): never the disc, a memory card or settings.
#
# Downloads are pinned and sha256-checked into ~/.cache/dw3-android (DW3_ANDROID_CACHE). Docker
# Desktop only mounts paths under the home directory, which is why the cache lives there.
set -e
cd "$(dirname "$0")/../.."
cache="${DW3_ANDROID_CACHE:-$HOME/.cache/dw3-android}"
image=dw3port-android

mkdir -p "$cache/dl"
fetch() { # url sha file
    f="$cache/dl/$3"
    [ -f "$f" ] || curl -fsSL -o "$f" "$1"
    echo "$2  $f" | sha256sum -c --quiet - || { echo "build.sh: sha256 mismatch for $3" >&2; rm -f "$f"; exit 1; }
}
fetch https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz \
    5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 SDL2-2.32.10.tar.gz
fetch https://github.com/kcat/openal-soft/releases/download/1.24.3/openal-soft-1.24.3.tar.bz2 \
    cb5e6197a1c0da0edcf2a81024953cc8fa8545c3b9474e48c852af709d587892 openal-soft-1.24.3.tar.bz2

docker_run() { # args...: runs in the image as the caller, repo at /dw3, cache at /deps
    docker run --rm -v "$PWD:/dw3" -v "$cache:/deps" -w /dw3 -u "$(id -u):$(id -g)" -e HOME=/tmp "$image" "$@"
}

# SDL's Java glue and the Gradle wrapper jar come out of the pinned SDL2 tarball, not out of git
unpack_sdl_extras() {
    sdl_tmp="$cache/sdl-android-project"
    if [ ! -f "$sdl_tmp/.done-2.32.10" ]; then
        rm -rf "$sdl_tmp" && mkdir -p "$sdl_tmp"
        tar -xzf "$cache/dl/SDL2-2.32.10.tar.gz" -C "$sdl_tmp" --strip-components=1 \
            SDL2-2.32.10/android-project/app/src/main/java SDL2-2.32.10/android-project/gradle/wrapper/gradle-wrapper.jar SDL2-2.32.10/LICENSE.txt
        touch "$sdl_tmp/.done-2.32.10"
    fi
    rm -rf pc/android/app/build/sdl-java && mkdir -p pc/android/app/build/sdl-java
    cp -r "$sdl_tmp/android-project/app/src/main/java/." pc/android/app/build/sdl-java/
    # the wrapper jar of the template, sha256-pinned here (the Gradle distribution itself is pinned in gradle-wrapper.properties)
    echo "2c23278a62dc9f96ab11bee897d9f53e6479d6419135e4560ce822a6431df5e5  $sdl_tmp/android-project/gradle/wrapper/gradle-wrapper.jar" | sha256sum -c --quiet - \
        || { echo "build.sh: gradle-wrapper.jar sha256 mismatch" >&2; exit 1; }
    cp "$sdl_tmp/android-project/gradle/wrapper/gradle-wrapper.jar" pc/android/gradle/wrapper/
}

# the es-419 overlay as APK assets: a copy staged under the (ignored) Gradle build folder plus the index the app reads, because SDL
# cannot list assets. The stamp is the hash of every file, so the app refreshes its copy whenever the content changes.
stage_l10n() {
    out=pc/android/app/build/l10n-assets/localization
    rm -rf pc/android/app/build/l10n-assets && mkdir -p "$out/es-419"
    cp localization/layout.json "$out/"
    cp localization/es-419/*.json "$out/es-419/"
    (cd "$out" && {
        printf 'stamp %s\n' "$(find . -name '*.json' | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -c1-16)"
        find . -name '*.json' | LC_ALL=C sort | sed 's|^\./||' | while read -r f; do printf '%s %s\n' "$(wc -c < "$f")" "$f"; done
    } > index.txt)
}

# The unstripped libmain.so and libopenal.so of this build stay outside the APK (which carries the stripped ones), next to a BUILD-ID.txt with
# their build ids: the fatal-signal report prints "module <lib> build-id <hex>" and `llvm-addr2line -f -e <dir>/<lib> 0x<offset>` names the
# line (pc/README.md, "Reportes de crash"). Debug builds go to pc/build-android-symbols/debug, releases to .../<version>.
keep_symbols() { # dir
    sym="$1"
    rm -rf "$sym" && mkdir -p "$sym"
    unstripped=$(find pc/android/app/build/intermediates/cxx -path '*obj/arm64-v8a/libmain.so' -printf '%T@ %p\n' | sort -rn | head -n 1 | cut -d' ' -f2-)
    [ -n "$unstripped" ] || { echo "build.sh: no unstripped libmain.so found" >&2; exit 1; }
    cp "$unstripped" "$sym/libmain.so"
    cp "$cache/prefix/arm64-v8a/lib/libopenal.so" "$sym/libopenal.so"
}
symbol_ids() { # dir apk: the build ids of the kept libraries and of the ones inside the APK (they must match)
    docker_run sh -c '
        set -e
        rb=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin
        rm -rf /tmp/apkx && mkdir /tmp/apkx && unzip -q "$2" "lib/*" -d /tmp/apkx
        for lib in libmain.so libopenal.so; do
            a=$($rb/llvm-readelf -n /tmp/apkx/lib/arm64-v8a/$lib | awk "/Build ID/ { print \$3 }")
            b=$($rb/llvm-readelf -n "$1/$lib" | awk "/Build ID/ { print \$3 }")
            echo "$lib (APK) build id: $a"
            echo "$lib (symbols) build id: $b"
            [ -n "$a" ] && [ "$a" = "$b" ] || { echo "build.sh: $lib build ids differ or are missing" >&2; exit 1; }
        done
    ' sh "$1" "$2"
}

docker build -q -t "$image" pc/android >/dev/null

# The CMake cache lists the game's objects at configure time; when pc/Makefile's list (or the flags) change, a stale cache fails
# with undefined symbols. So hash what the Makefile prints and drop the cache when the hash differs from the last build's.
refresh_cxx() {
    h=$(docker_run sh -c 'make -s --no-print-directory -C pc TARGET=android ANDROID_OPT="$DW3_ANDROID_OPT" ANDROID_API=29 ANDROID_PREFIX=/deps/prefix/arm64-v8a print-android-objects print-android-ldflags print-android-psycross' \
        | sha256sum | cut -c1-16)
    [ -n "$h" ] || { echo "build.sh: cannot list the objects" >&2; exit 1; }
    stamp=pc/android/app/.cxx/.objlist-hash
    if [ "$(cat "$stamp" 2>/dev/null)" != "$h" ]; then
        echo "build.sh: object list changed ($h), clearing the stale CMake cache"
        rm -rf pc/android/app/.cxx pc/android/app/build/intermediates/cxx
        mkdir -p pc/android/app/.cxx && echo "$h" > "$stamp"
    fi
}

# the release key: created once outside the repository
ensure_key() {
    keydir="${DW3_KEY_DIR:-$HOME/.config/dw3port}"
    ks="$keydir/android-release.keystore"; pw="$keydir/android-release.pass"
    if [ ! -f "$ks" ] || [ ! -f "$pw" ]; then
        [ ! -f "$ks" ] || [ -f "$pw" ] || { echo "build.sh: $ks exists but $pw is missing" >&2; exit 1; }
        mkdir -p "$keydir" && chmod 700 "$keydir"
        ( umask 077; head -c 24 /dev/urandom | base64 | tr -d '\n=+/' > "$pw" )
        chmod 600 "$pw"
        docker run --rm -v "$keydir:/keys" -u "$(id -u):$(id -g)" -e HOME=/tmp -e KS_PASS="$(cat "$pw")" "$image" \
            keytool -genkeypair -keystore /keys/android-release.keystore -storetype PKCS12 -storepass:env KS_PASS -keypass:env KS_PASS \
            -alias dw3port -keyalg RSA -keysize 4096 -validity 36500 -dname "CN=DW3 Port, O=DW3 Port"
        chmod 600 "$ks"
        echo "################################################################" >&2
        echo "# NEW RELEASE KEY created: $ks" >&2
        echo "# BACK IT UP NOW together with $pw (keep both off this machine)." >&2
        echo "# Losing it means players cannot update in place: they would have" >&2
        echo "# to uninstall (and lose their saves) to install a build signed with another key." >&2
        echo "################################################################" >&2
    fi
    [ "$(stat -c %a "$pw")" = 600 ] || chmod 600 "$pw"
}

# versionCode from the version string (formula in the header)
version_code() {
    echo "$1" | awk '{
        if (!match($0, /^[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc|dev)[0-9]*)?$/)) exit 1
        split($0, a, "-"); split(a[1], v, "."); stage = 99
        if (a[2] != "") { n = a[2]; sub(/^[a-z]+/, "", n); n += 0; k = a[2]; sub(/[0-9]+$/, "", k)
            base = (k == "rc") ? 50 : (k == "beta") ? 20 : 0
            if (n > 29) exit 1
            stage = base + n }
        if (v[2] > 99 || v[3] > 99) exit 1
        print v[1] * 1000000 + v[2] * 10000 + v[3] * 100 + stage }'
}

case "${1:-deps}" in
deps) docker_run sh pc/android/build_deps.sh /deps ;;
apk)
    docker_run sh pc/android/build_deps.sh /deps
    refresh_cxx
    unpack_sdl_extras
    stage_l10n
    mkdir -p "$cache/gradle" "$cache/android-home" # android-home keeps the debug signing key: updates install over each other
    docker_run env GRADLE_USER_HOME=/deps/gradle ANDROID_USER_HOME=/deps/android-home DW3_ANDROID_OPT="${DW3_ANDROID_OPT:-}" sh -c 'cd pc/android && ./gradlew --no-daemon assembleDebug'
    apk=pc/android/app/build/outputs/apk/debug/app-debug.apk
    ls -l "$apk"
    keep_symbols pc/build-android-symbols/debug
    symbol_ids pc/build-android-symbols/debug "$apk" | tee pc/build-android-symbols/debug/BUILD-ID.txt
    echo "unstripped libmain.so and libopenal.so kept in pc/build-android-symbols/debug/"
    ;;
release)
    version="${2:-0.0.1-dev}"
    code=$(version_code "$version") || { echo "build.sh: bad version '$version' (X.Y.Z or X.Y.Z-{alpha,beta,rc,dev}[N], N<=29, minor and patch <=99)" >&2; exit 2; }
    ensure_key
    docker_run sh pc/android/build_deps.sh /deps
    refresh_cxx
    unpack_sdl_extras
    stage_l10n
    mkdir -p "$cache/gradle" "$cache/android-home"
    rm -f pc/android/app/build/outputs/apk/release/app-release.apk
    docker run --rm -v "$PWD:/dw3" -v "$cache:/deps" -v "$keydir:/keys:ro" -w /dw3 -u "$(id -u):$(id -g)" -e HOME=/tmp \
        -e GRADLE_USER_HOME=/deps/gradle -e ANDROID_USER_HOME=/deps/android-home -e DW3_ANDROID_OPT="${DW3_ANDROID_OPT:-}" \
        -e DW3_KEYSTORE=/keys/android-release.keystore -e DW3_KEYSTORE_PASS="$(cat "$keydir/android-release.pass")" "$image" \
        sh -c "cd pc/android && ./gradlew --no-daemon -Pdw3.versionName=$version -Pdw3.versionCode=$code assembleRelease"
    apk=pc/android/app/build/outputs/apk/release/app-release.apk
    # the unstripped libmain.so and libopenal.so (crash symbolization) stay out of the APK, which carries the stripped ones
    sym="pc/build-android-symbols/$version"
    keep_symbols "$sym"
    docker_run sh -c '
        set -e
        bt=/opt/android-sdk/build-tools/35.0.0
        rb=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin
        apk='"$apk"'
        $bt/apksigner verify --verbose --print-certs "$apk" | grep -E "Verifies|Number of signers|Signer #1 certificate (DN|SHA-256)|Verified using"
        $bt/zipalign -c -P 16 -v 4 "$apk" | tail -n 3
        rm -rf /tmp/apkx && mkdir /tmp/apkx && unzip -q "$apk" "lib/*" -d /tmp/apkx
        for so in /tmp/apkx/lib/arm64-v8a/*.so; do
            a=$($rb/llvm-readelf -lW "$so" | awk "\$1 == \"LOAD\" { print \$NF }" | sort -u | tr "\n" " ")
            echo "$(basename "$so"): LOAD align $a"
            [ "$a" = "0x4000 " ] || { echo "build.sh: $so is not 16 KB aligned" >&2; exit 1; }
        done
    ' | tee "$sym/BUILD-ID.txt"
    symbol_ids "$sym" "$apk" | tee -a "$sym/BUILD-ID.txt"
    ls -l "$apk"
    echo "versionName $version, versionCode $code; unstripped libmain.so and libopenal.so kept in $sym/"
    ;;
*) echo "usage: pc/android/build.sh deps|apk|release [version]" >&2; exit 2 ;;
esac
