#!/bin/sh
# Plays the game as a player would: the retail disc, from the title, with no
# harness shortcut, no scripted keys and no debug log. The closest thing to the
# final port until the standalone executable exists (Linux/Windows builds).
# For bug reports use pc/review.sh inicio instead: same game, with a full log.
#
#   pc/play.sh [disc.bin]
#
# The disc defaults to DW3_DISC, else the owner's retail image.

disc="${1:-${DW3_DISC:-$HOME/Juegos/DW3/Digimon World 2003 (Europe) (En,Fr,De,Es,It).bin}}"

if [ ! -f "$disc" ]; then
    echo "no such disc image: $disc (pass it as the first argument or set DW3_DISC)" >&2
    exit 2
fi

make -C pc >/dev/null || exit 1
# A frame budget no session reaches: the run ends when the window is closed
exec ./pc/build/boot "$disc" 2000000000 >/dev/null
