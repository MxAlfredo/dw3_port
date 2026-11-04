# The AppImage's build image: Ubuntu 22.04 (glibc 2.35), so the binary runs on that and anything newer.
# SDL2 and OpenAL Soft are not taken from here (22.04's are old): build_in_container.sh builds the
# Windows build's versions from source into the cache. Their backends (X11, Wayland, PulseAudio,
# PipeWire, ALSA) are loaded at run time, so only the headers are needed.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config python3 file patchelf ca-certificates \
        libgl-dev libegl-dev libgles-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev \
        libxxf86vm-dev libxss-dev libxfixes-dev libxkbcommon-dev \
        libwayland-dev wayland-protocols \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev \
    && rm -rf /var/lib/apt/lists/*
