#!/bin/bash
# Battle sweep: fights every encounter of the game's FIELDSTG_encounters with
# the starter party and the battle autopilot, and writes one TSV line per run
# (id, exit code, end line, result, crash line, elapsed s, CPU %, max RSS KB).
#
#   pc/battle_sweep.sh <out-dir> [disc.bin] [frames]    (run from the repo root)
#
# Env: JOBS (parallel runs, default 1; keep it at 2 or less),
#      IDS (space separated `battleenc` specs `n[:stage[:music]]`; default every
#      encounter, the count taken from the FIELDSTG_encounters symbol size).
# Logs and captures land in <out-dir>; the TSV is <out-dir>/sweep.tsv.
# The result column: win (results scene back to the field), loss (game over to
# the title), fled (the autopilot ran away to the field), or other.
set -u
OUT=${1:?usage: pc/battle_sweep.sh <out-dir> [disc.bin] [frames]}
DISC=${2:-build/t9-e2e/DW3_EU_ES419_TEXT_TEST.bin}
FRAMES=${3:-6000}
JOBS=${JOBS:-1}
mkdir -p "$OUT"
# No window and no speaker output by default (see pc/stage_sweep.sh).
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER-offscreen}
export ALSOFT_DRIVERS=${ALSOFT_DRIVERS-null}
# Ignore real pads too, so the owner's controller never drives a batch run
export SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=${SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT-0x1/0x1}

one() {
    local id=$1 rc log tm tag
    tag=${id//:/_}
    log="$OUT/b$tag.log"
    tm="$OUT/b$tag.time"
    /usr/bin/time -o "$tm" -f "%e %P %M" timeout 150 ./pc/build/boot "$DISC" "$FRAMES" \
        "$OUT/b$tag.png" 0 "battleenc:$id,battleauto" >"$log" 2>&1
    rc=$?
    local endl res bad
    endl=$(rg -a "^end:" "$log" | tail -1); endl=${endl#end: }
    res="other"
    case "$endl" in
        "left mode 0x1400 for 0x207"*) res=win ;;
        "left mode 0x600 for 0xE00"*) res=loss ;;
        "left mode 0x600 for 0x207"*) res=fled ;;
        "") res=none ;;
    esac
    bad=$(rg -a "SIG|Backtrace|names no encounter" "$log" | head -1)
    local el cpu rss
    read -r el cpu rss < <(tail -1 "$tm" | tr -d '%')
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$id" "$rc" "$endl" "$res" "$bad" "$el" "$cpu" "$rss" \
        >"$OUT/b$tag.tsv"
}
export -f one
export OUT DISC FRAMES

if [ -z "${IDS:-}" ]; then
    size=$(nm -S pc/build/boot | rg ' FIELDSTG_encounters$' | cut -d' ' -f2)
    size=$((16#$size))
    # sizeof(Encounter) on the host: three 8-byte pointers + 14 bytes, padded to 8
    IDS=$(seq 0 $((size / 40 - 1)) | tr '\n' ' ')
fi
printf '%s\n' $IDS | xargs -P "$JOBS" -I{} bash -c 'one {}'
{
    printf 'id\texit\tend\tresult\tcrash\telapsed_s\tcpu_pct\tmax_rss_kb\n'
    for n in $IDS; do cat "$OUT/b${n//:/_}.tsv"; done
} >"$OUT/sweep.tsv"
echo "sweep: $(echo $IDS | wc -w) fights -> $OUT/sweep.tsv"
