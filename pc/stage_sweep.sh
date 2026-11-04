#!/bin/bash
# Stage sweep: warps into every stage of pc/shim/warp_stages.inc for a fixed
# number of frames and writes one TSV line per stage (stage, exit code, last
# boot mode line, warp fired, crash/unknown-warp line, elapsed s, CPU %, max RSS KB).
#
#   pc/stage_sweep.sh <out-dir> [disc.bin] [frames]    (run from the repo root)
#
# Env: JOBS (parallel runs, default 1: CPU% is only comparable at 1-2),
#      STAGES (space separated list; default every stage of the table).
# Logs and captures land in <out-dir>; the TSV is <out-dir>/sweep.tsv.
set -u
OUT=${1:?usage: pc/stage_sweep.sh <out-dir> [disc.bin] [frames]}
DISC=${2:-build/t9-e2e/DW3_EU_ES419_TEXT_TEST.bin}
FRAMES=${3:-600}
JOBS=${JOBS:-1}
mkdir -p "$OUT"
# No window and no speaker output by default: SDL's offscreen driver still
# renders and captures, and OpenAL's null device still runs the whole sound
# shim (it is not DW3_SND_MUTE). Set either variable to override.
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER-offscreen}
export ALSOFT_DRIVERS=${ALSOFT_DRIVERS-null}
# Ignore real pads too, so the owner's controller never drives a batch run
export SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=${SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT-0x1/0x1}

one() {
    local n=$1 rc log tm
    log="$OUT/s$n.log"
    tm="$OUT/s$n.time"
    /usr/bin/time -o "$tm" -f "%e %P %M" timeout 60 ./pc/build/boot "$DISC" "$FRAMES" \
        "$OUT/s$n.png" 0 "stay,warp:$n" >"$log" 2>&1
    rc=$?
    local mode fired bad
    mode=$(rg -a "^boot: mode" "$log" | tail -1); mode=${mode#boot: }
    fired=no; rg -aq "warp fired" "$log" && fired=yes
    bad=$(rg -a "SIG|Backtrace|unknown warp" "$log" | head -1)
    local el cpu rss
    read -r el cpu rss < <(tail -1 "$tm" | tr -d '%')
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$n" "$rc" "$mode" "$fired" "$bad" "$el" "$cpu" "$rss" \
        >"$OUT/s$n.tsv"
}
export -f one
export OUT DISC FRAMES

STAGES=${STAGES:-$(rg -o '^\s+\{ (\d+),' -r '$1' pc/shim/warp_stages.inc | tr '\n' ' ')}
printf '%s\n' $STAGES | xargs -P "$JOBS" -I{} bash -c 'one {}'
{
    printf 'stage\texit\tmode\twarp_fired\tcrash\telapsed_s\tcpu_pct\tmax_rss_kb\n'
    for n in $STAGES; do cat "$OUT/s$n.tsv"; done
} >"$OUT/sweep.tsv"
echo "sweep: $(echo $STAGES | wc -w) stages -> $OUT/sweep.tsv"
