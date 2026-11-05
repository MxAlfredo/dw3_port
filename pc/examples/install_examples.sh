#!/bin/sh
# Installs the example mods of pc/examples/mods/ into a user folder, makes the files that cannot
# live in the repository (ADR-001: no game data) from YOUR exported data/, and turns every
# installed example OFF in settings.ini, so nothing changes until you enable one (Options > Mods).
#
#   pc/examples/install_examples.sh [--only a,b,tables] [--list] [USER_DIR]
#
# USER_DIR defaults to $DW3_USER_DIR, else $HOME/.local/share/dw3port/dw3 (the folder with data/,
# mods/, saves/ and settings.ini). The script writes only <USER_DIR>/mods/ and
# <USER_DIR>/settings.ini (a copy of the old one goes to settings.ini.bak). It never touches saves/.
#
# --only takes example names or groups, separated by commas:
#   tables  (10 mods: data-table patches)   content (6: new items, techs, Digimon, cards, a model)
#   media   (10: text, images, sound, song, movie, stage art, stage characters, new stages)
# The Mods tab of the options menu scrolls through every installed mod. Run it again after "Prepare modding files" to add the ones it skipped.
# Needs python3 (and Pillow for the movie) for the generated examples; the others need nothing.
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
src="$here/mods"

G_TABLES="cheap-shops double-rewards generous-loot shop-extra-item field-encounters fast-growth gym-inn-prices card-rival-deck strong-weapons"
G_CONTENT="sample-card sample-digimon sample-enemy sample-item sample-model sample-tech"
G_MEDIA="menu-frames-gold player-recolor text-upper-items sfx-beep song-title-chime video-bars stage-tiles stage-objects stage-npc training-field"

only=""
list=0
user=""
while [ $# -gt 0 ]; do
    case "$1" in
    --only) only="$2"; shift 2 ;;
    --only=*) only="${1#--only=}"; shift ;;
    --list) list=1; shift ;;
    -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "install_examples: unknown option $1" >&2; exit 2 ;;
    *) user="$1"; shift ;;
    esac
done
user="${user:-${DW3_USER_DIR:-$HOME/.local/share/dw3port/dw3}}"

# the names to install
names=""
if [ -z "$only" ]; then
    names="$G_TABLES $G_CONTENT $G_MEDIA"
else
    for n in $(echo "$only" | tr ',' ' '); do
        case "$n" in
        tables) names="$names $G_TABLES" ;;
        content) names="$names $G_CONTENT" ;;
        media) names="$names $G_MEDIA" ;;
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

has() { case " $names " in *" $1 "*) return 0 ;; esac; return 1; }
py=$(command -v python3 || true)
installed=""
skipped=""

for n in $names; do
    cp -R "$src/$n" "$mods/"
done

# gen <mod> <file that proves it was made> <command...>: runs a generator; a mod whose file is
# missing afterwards is removed again, so a half-made example is never installed.
gen() {
    mod="$1"; proof="$2"; shift 2
    has "$mod" || return 0
    if [ -z "$py" ]; then
        echo "install_examples: $mod skipped: python3 not found"
    else
        "$@" || true
    fi
    if [ -e "$mods/$mod/$proof" ]; then
        return 0
    fi
    rm -rf "$mods/$mod"
    skipped="$skipped $mod"
    names=$(echo " $names " | sed "s/ $mod / /")
}

gen menu-frames-gold images/screen/stshoptm/00_00.png \
    python3 "$root/tools/image_mod_palette.py" --data "$user/data" --mod "$mods/menu-frames-gold"
gen player-recolor images/field/sprt/p001play \
    python3 "$root/tools/image_mod_palette.py" --data "$user/data" --mod "$mods/player-recolor" \
    --sprite p001play --sprite p002play --sprite p002plyd --sprite p002plyf --sprite p002plyk --sprite p002plyl --to 0
gen text-upper-items text python3 "$here/tools/gen_text.py" "$user"
gen stage-tiles z_stage/s330pack python3 "$here/tools/gen_art.py" "$user" stage-tiles
gen stage-objects images/field/stage/s233mask python3 "$here/tools/gen_art.py" "$user" stage-objects
gen stage-npc sprites/rival/sprite.json python3 "$here/tools/gen_art.py" "$user" stage-npc
gen sfx-beep audio/COMMON/vag_050.wav python3 "$here/tools/gen_media.py" "$user" sfx-beep
gen song-title-chime audio/TTLBGM/seq_0_00.wav python3 "$here/tools/gen_media.py" "$user" song-title-chime
gen video-bars video/MOVIEOPN.avi python3 "$here/tools/gen_media.py" "$user" video-bars

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
[ -n "$skipped" ] && echo "install_examples: skipped:$skipped (see the messages above; run again after exporting the data)"
echo "install_examples: every installed example is OFF ([mods] disabled in $ini; old file: $ini.bak)."
echo "Enable one in the game (Options > Mods) or delete its name from that line, then restart."
n=$(echo $names | wc -w)
[ "${len:-0}" -gt 500 ] && echo "warning: the disabled line is $len characters; the port reads at most 511."
exit 0
