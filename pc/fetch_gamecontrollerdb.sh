#!/bin/sh
# Downloads SDL_GameControllerDB (zlib license) next to the boot binary, where
# shim/input_cfg.c looks for it (SDL_GetBasePath, then the pref path). The file
# is not committed. Usage: pc/fetch_gamecontrollerdb.sh
set -eu
out="$(dirname "$0")/build/gamecontrollerdb.txt"
mkdir -p "$(dirname "$out")"
curl -fsSL -o "$out.tmp" \
    https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/master/gamecontrollerdb.txt
mv "$out.tmp" "$out"
echo "wrote $out ($(wc -l < "$out") lines)"
