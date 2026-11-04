# The Android build image: JDK 17, the Android SDK command-line tools with
# platform 34, build-tools 34.0.0 (AGP's) plus 35.0.0 (its zipalign knows -P 16, the 16 KB page check; apksigner) and CMake 3.22.1, and the NDK r27c (LTS), every download sha256-pinned.
# Nothing here is the game's or the disc's. build.sh builds and runs it:
#   docker build -t dw3port-android -f pc/android/Dockerfile pc/android
#
# The pins (re-check them when bumping a version):
#   JDK       Temurin 17.0.13+11   sha256 from the release's .sha256.txt (adoptium/temurin17-binaries)
#   cmdline   11076708             sha256 as published on developer.android.com/studio (and the same on dl.google.com)
#   NDK       r27c (27.2.12479018) sha256 computed here; its sha1 090e8083a715fdb1a3e402d0763c388abb03fb4e is Google's
FROM ubuntu:24.04 AS fetch
ADD --checksum=sha256:8682892fc02965930b9022c066fa164dd6f458ef4a5dc262016aa28333b30f49 \
    https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.13%2B11/OpenJDK17U-jdk_x64_linux_hotspot_17.0.13_11.tar.gz /dl/jdk.tar.gz
ADD --checksum=sha256:2d2d50857e4eb553af5a6dc3ad507a17adf43d115264b1afc116f95c92e5e258 \
    https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip /dl/cmdline-tools.zip
ADD --checksum=sha256:59c2f6dc96743b5daf5d1626684640b20a6bd2b1d85b13156b90333741bad5cc \
    https://dl.google.com/android/repository/android-ndk-r27c-linux.zip /dl/ndk.zip

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
# make: the port's Makefile builds the game's objects; python3: pc/check_scopes.py; unzip: the APK listing, bzip2: OpenAL Soft tarball
RUN apt-get update && apt-get install -y --no-install-recommends \
        make python3 unzip zip bzip2 file git curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV JAVA_HOME=/opt/jdk17 ANDROID_HOME=/opt/android-sdk ANDROID_SDK_ROOT=/opt/android-sdk
RUN --mount=from=fetch,source=/dl,target=/dl \
    mkdir -p /opt/jdk17 "$ANDROID_HOME/cmdline-tools" \
    && tar -xzf /dl/jdk.tar.gz -C /opt/jdk17 --strip-components=1 \
    && unzip -q /dl/cmdline-tools.zip -d "$ANDROID_HOME/cmdline-tools" \
    && mv "$ANDROID_HOME/cmdline-tools/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest" \
    && mkdir -p "$ANDROID_HOME/ndk" && unzip -q /dl/ndk.zip -d "$ANDROID_HOME/ndk" \
    && mv "$ANDROID_HOME/ndk/android-ndk-r27c" "$ANDROID_HOME/ndk/27.2.12479018"
ENV PATH=/opt/jdk17/bin:/opt/android-sdk/cmdline-tools/latest/bin:/opt/android-sdk/cmake/3.22.1/bin:$PATH

# the SDK packages are version-pinned; sdkmanager checks their checksums against the repository index
RUN yes | sdkmanager --licenses >/dev/null \
    && sdkmanager "platforms;android-34" "build-tools;34.0.0" "build-tools;35.0.0" "cmake;3.22.1" \
    && test -f "$ANDROID_HOME/ndk/27.2.12479018/source.properties" \
    && chmod -R a+rX /opt/android-sdk /opt/jdk17

ENV ANDROID_NDK_HOME=/opt/android-sdk/ndk/27.2.12479018
