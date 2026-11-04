#!/bin/sh
# Fetches what the port is built from, each at the commit deps.lock pins, and applies our patches:
#   deps/dw3_decomp      the decompilation (ReGame-Labs/dw3_decomp) + patches/dw3_decomp.patch;
#                        src and include at the repository root point into it
#   external/psycross    PsyCross (OpenDriver2/PsyCross) + patches/psycross.patch
#   external/psyq_headers the PsyQ headers the decompilation builds against
#   tools/extract_disc.py a link to the decompilation's disc extractor (the modding guide uses it)
#
#   scripts/fetch_deps.sh
#
# Safe to run again: each checkout is reset to its pinned commit before the patch goes on.
set -e
cd "$(dirname "$0")/.."
. ./deps.lock

checkout() { # dir url commit
    if [ ! -d "$1/.git" ]; then
        mkdir -p "$1"
        git -C "$1" init -q
        git -C "$1" remote add origin "$2"
    fi
    git -C "$1" fetch -q --depth 1 origin "$3"
    git -C "$1" checkout -q --force FETCH_HEAD
    git -C "$1" clean -qfdx
}

checkout deps/dw3_decomp "$DECOMP_URL" "$DECOMP_COMMIT"
git -C deps/dw3_decomp apply --whitespace=nowarn "$PWD/patches/dw3_decomp.patch"
ln -sfn deps/dw3_decomp/src src
ln -sfn deps/dw3_decomp/include include
ln -sfn ../deps/dw3_decomp/tools/extract_disc.py tools/extract_disc.py

checkout external/psycross "$PSYCROSS_URL" "$PSYCROSS_COMMIT"
git -C external/psycross apply --whitespace=nowarn "$PWD/patches/psycross.patch"

checkout external/psyq_headers "$PSYQ_HEADERS_URL" "$PSYQ_HEADERS_COMMIT"

echo "fetch_deps: dw3_decomp ${DECOMP_COMMIT%"${DECOMP_COMMIT#???????}"}, psycross ${PSYCROSS_COMMIT%"${PSYCROSS_COMMIT#???????}"}, psyq_headers ${PSYQ_HEADERS_COMMIT%"${PSYQ_HEADERS_COMMIT#???????}"} (patched)"
