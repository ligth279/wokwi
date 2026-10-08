#!/usr/bin/env bash
# Run the Step 5 scenarios on build/protected: 7 scenarios x REPS runs.
#   Tests/run_step5_sims.sh <outdir> [reps] [scenario...]
set -uo pipefail
cd "$(dirname "$0")/.."
WOKWI=${WOKWI_CLI:-$(command -v wokwi-cli || echo "$HOME/.wokwi/bin/wokwi-cli")}
OUT=$1; REPS=${2:-3}; shift 2 || true
SCENS=${*:-${DEFAULT_SCENS:-fp group tim01 tim02 mem02 cpu01 cpu02}}
PAR=${PAR:-2}
mkdir -p "$OUT"
[ -f "$OUT/firmware.elf" ] || arm-none-eabi-strip --strip-debug -o "$OUT/firmware.elf" build/protected/firmware.elf
export WOKWI OUT RESUME SCEN_PREFIX TIMEOUT
run_one() { # scenario run
    local base="$OUT/$1_run$2" attempt rc
    [ -e "$OUT/QUOTA_EXHAUSTED" ] && { echo "   $1 run $2: skipped (Wokwi CI quota exhausted)"; return; }
    if [ "${RESUME:-0}" = 1 ] && [ -s "$base.log" ] && grep -aq "wokwi exit" "$base.console.txt" 2>/dev/null; then
        echo "   $1 run $2: kept ($(tail -1 "$base.console.txt"))"; return
    fi
    for attempt in 1 2 3 4 5 6; do
        "$WOKWI" --elf "$OUT/firmware.elf" --timeout "${TIMEOUT:-40000}" --scenario "${SCEN_PREFIX:-Tests/protected/step5_}$1.yaml" \
            --serial-log-file "$base.log" . > "$base.console.txt" 2>&1
        rc=$?
        # Infrastructure failures, not experiment results: HTTP 503, DNS/network errors, or a
        # connection that dies before the firmware printed anything (empty serial log).
        if grep -aq "used up your Free plan" "$base.console.txt"; then
            mv "$base.console.txt" "$base.quota.txt"; : > "$base.log"; touch "$OUT/QUOTA_EXHAUSTED"
            echo "   $1 run $2: Wokwi CI quota exhausted"; return
        fi
        if grep -aq "Service Unavailable\|WebSocket was closed before the connection\|EAI_AGAIN\|Error connecting" "$base.console.txt" || [ ! -s "$base.log" ]; then
            mv "$base.console.txt" "$base.attempt$attempt.txt"; sleep $((attempt * 5)); continue
        fi
        break
    done
    echo "   wokwi exit=$rc attempts=$attempt" >> "$base.console.txt"
    echo "   $1 run $2: $(tail -1 "$base.console.txt")"
}
export -f run_one
for s in $SCENS; do for i in $(seq 1 "$REPS"); do echo "$s $i"; done; done | xargs -P "$PAR" -L1 bash -c 'run_one "$0" "$1"'
