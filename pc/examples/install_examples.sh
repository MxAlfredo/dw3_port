#!/bin/sh
# Installs the example mods of pc/examples/mods/ into a user folder and turns every installed
# example OFF in settings.ini, so nothing changes until you enable one (Options > Mods).
#
#   pc/examples/install_examples.sh [--only a,b] [--list] [USER_DIR]
#
# USER_DIR defaults to $DW3_USER_DIR, else $HOME/.local/share/dw3port/dw3 (the folder with data/,
# mods/, saves/ and settings.ini). The script writes only <USER_DIR>/mods/ and
# <USER_DIR>/settings.ini (a copy of the old one goes to settings.ini.bak). It never touches saves/.
#
# --only takes example names, separated by commas (default: all):
#   training-field  (two new field stages with their own art, an NPC, a sprite, tables and texts)
#   sample-model    (a new enemy Digimon with a 3D model made from a glTF)
#   betamon-pack   (a new partner with three forms, a fourth starter team and an unlock talk)
# All are complete and ship their own (original) files: nothing is generated from your disc.
# The Mods tab of the options menu scrolls through every installed mod.
set -e

here=$(cd "$(dirname "$0")" && pwd)
src="$here/mods"

ALL="training-field sample-model betamon-pack"

only=""
list=0
user=""
while [ $# -gt 0 ]; do
    case "$1" in
    --only) only="$2"; shift 2 ;;
    --only=*) only="${1#--only=}"; shift ;;
    --list) list=1; shift ;;
    -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "install_examples: unknown option $1" >&2; exit 2 ;;
    *) user="$1"; shift ;;
    esac
done
user="${user:-${DW3_USER_DIR:-$HOME/.local/share/dw3port/dw3}}"

# the names to install
names=""
if [ -z "$only" ]; then
    names="$ALL"
else
    for n in $(echo "$only" | tr ',' ' '); do
        case "$n" in
        *) names="$names $n" ;;
        esac
    done
fi
if [ "$list" = 1 ]; then
    for n in $names; do echo "$n"; done
    exit 0
fi
for n in $names; do
    if [ ! -f "$src/$n/mod.json" ]; then
        echo "install_examples: no example named $n in $src" >&2
        exit 2
    fi
done

if [ ! -d "$user" ]; then
    echo "install_examples: $user does not exist (start the game once, or pass the user folder)" >&2
    exit 2
fi
mods="$user/mods"
mkdir -p "$mods"

for n in $names; do
    rm -rf "$mods/$n"
    cp -R "$src/$n" "$mods/"
done

# every installed example goes into [mods] disabled
list=$(echo $names | tr ' ' ',' | sed 's/,/, /g')
ini="$user/settings.ini"
if [ -f "$ini" ]; then
    cp -p "$ini" "$ini.bak"
    old_ini="$ini.bak"
else
    old_ini=/dev/null
fi
awk -v add="$list" '
function merge(old,   n, a, i, seen, out, m, b, j) {
    out = ""
    n = split(old, a, /[ \t]*,[ \t]*/)
    for (i = 1; i <= n; i++) {
        gsub(/^[ \t]+|[ \t]+$/, "", a[i])
        if (a[i] != "" && !(a[i] in seen)) { seen[a[i]] = 1; out = out (out == "" ? "" : ", ") a[i] }
    }
    m = split(add, b, /, /)
    for (j = 1; j <= m; j++) {
        if (b[j] != "" && !(b[j] in seen)) { seen[b[j]] = 1; out = out (out == "" ? "" : ", ") b[j] }
    }
    return out
}
/^[ \t]*\[/ {
    if (inmods && !done) { print "disabled = " merge(""); done = 1 }
    inmods = ($0 ~ /^[ \t]*\[mods\][ \t]*$/)
    if (inmods) hadmods = 1
    print; next
}
inmods && /^[ \t]*disabled[ \t]*=/ {
    v = $0; sub(/^[^=]*=/, "", v)
    print "disabled = " merge(v); done = 1; next
}
{ print }
END {
    if (inmods && !done) { print "disabled = " merge("") }
    else if (!hadmods) { print ""; print "[mods]"; print "disabled = " merge("") }
}' "$old_ini" > "$ini.new"
mv "$ini.new" "$ini"

len=$(grep '^disabled *=' "$ini" | awk '{ print length($0) }' | sort -n | tail -1)
echo "install_examples: installed:$(for n in $names; do printf ' %s' "$n"; done)"
echo "install_examples: every installed example is OFF ([mods] disabled in $ini; old file: $ini.bak)."
echo "Enable one in the game (Options > Mods) or delete its name from that line, then restart."
n=$(echo $names | wc -w)
[ "${len:-0}" -gt 500 ] && echo "warning: the disabled line is $len characters; the port reads at most 511."
exit 0
