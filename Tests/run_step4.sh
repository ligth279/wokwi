#!/usr/bin/env bash
# Step 4 acceptance run: actual fault effects on the baseline build.
#   - host unit tests
#   - 6 scenarios x 3 runs = 18 Wokwi simulations (Tests/baseline/step4_*.yaml)
#   - regression: the Step 2 suite (includes Step 0 and Step 1) and the Step 3 suite
#     (set SKIP_REGRESSION=1 to reuse the last summaries instead)
#   - checker -> results/summaries/step4_acceptance.md, results/tables/step4_fault_effects.md
# Wokwi budget: 18 + 12 simulations (~25 s wall each; PAR=3 runs them 3 at a time).
# From fish:  fish -c 'bash Tests/run_step4.sh'
set -uo pipefail
cd "$(dirname "$0")/.."

WOKWI=${WOKWI_CLI:-$(command -v wokwi-cli || echo "$HOME/.wokwi/bin/wokwi-cli")}
PAR=${PAR:-3}
OUT=results/raw/step4/$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"

make -s unit > "$OUT/unit.txt" 2>&1; UNIT=$?; echo "unit tests exit=$UNIT"
python3 Tests/tools/gen_step4_scenarios.py > /dev/null
make -s BUILD=baseline > /dev/null && make -s chips > /dev/null || { echo "build failed"; exit 1; }
arm-none-eabi-strip --strip-debug -o "$OUT/firmware.elf" build/baseline/firmware.elf

export WOKWI OUT
run_one() { # scenario run
    local base="$OUT/$1_run$2" attempt
    for attempt in 1 2 3 4; do
        "$WOKWI" --elf "$OUT/firmware.elf" --timeout 40000 --scenario "Tests/baseline/step4_$1.yaml" \
            --serial-log-file "$base.log" . > "$base.console.txt" 2>&1
        local rc=$?
        # The Wokwi service sometimes refuses a connection (HTTP 503) before any simulation
        # starts. That is an infrastructure failure, not an experiment result: retry it.
        if grep -aq "Service Unavailable\|WebSocket was closed before the connection" "$base.console.txt"; then
            mv "$base.console.txt" "$base.attempt$attempt.txt"
            sleep $((attempt * 5))
            continue
        fi
        break
    done
    echo "   wokwi exit=$rc attempts=$attempt" >> "$base.console.txt"
    echo "   $1 run $2: $(tail -1 "$base.console.txt")"
}
export -f run_one
for scen in group tim01 tim02 mem02 cpu01 cpu02; do
    for i in 1 2 3; do echo "$scen $i"; done
done | xargs -P "$PAR" -L1 bash -c 'run_one "$0" "$1"'

S2=skipped S3=skipped
if [ "${SKIP_REGRESSION:-0}" != 1 ]; then
    bash Tests/run_step2.sh > "$OUT/step2_suite.txt" 2>&1; S2=$?; echo "step 2 suite exit=$S2"
    bash Tests/run_step3.sh > "$OUT/step3_suite.txt" 2>&1; S3=$?; echo "step 3 suite exit=$S3"
    make -s BUILD=baseline > /dev/null
fi
I2C_LOG=$(ls -d results/raw/step2/*/ | tail -1)i2ctest.log
echo "unit_exit=$UNIT step2_exit=$S2 step3_exit=$S3 step2_summary=results/summaries/step2_acceptance.md step3_summary=results/summaries/step3_acceptance.md i2c_log=$I2C_LOG" > "$OUT/regression.txt"

python3 Tests/tools/check_step4.py --dir "$OUT" --elf "$OUT/firmware.elf"

# refresh the figures and tables of results/report/ from everything that has been run so far
python3 Tests/tools/make_report.py > /dev/null 2>&1 || echo "(make_report failed; run: python3 Tests/tools/make_report.py)"
