#!/usr/bin/env bash
# Step 3 acceptance run (fault-injection framework).
#   - host unit tests (parser + framework logic)
#   - 3 x scenario Tests/baseline/step3_framework.yaml (UART/timer/commands)
#   - 3 x GDB-assisted injection (Tests/tools/gdb_inject_run.sh)
#   - checker -> results/summaries/step3_acceptance.md + step3_injections.csv
# Uses BUILD=fwtest: the baseline application with the nine study faults
# registered but not implemented (FI_STUDY_FAULTS=0), so this suite keeps testing
# the framework itself; the real fault effects are Step 4 (Tests/run_step4.sh).
# STEP3_BUILD=protfw STEP3_TAG=_protfw runs the same suite on the protected code (Step 5.11).
# Wokwi budget: ~6 simulations (3 x ~20 s simulated + 3 x 4 s).
# From fish:  fish -c 'bash Tests/run_step3.sh'
set -uo pipefail
cd "$(dirname "$0")/.."

WOKWI=${WOKWI_CLI:-$(command -v wokwi-cli || echo "$HOME/.wokwi/bin/wokwi-cli")}
OUT=results/raw/step3/$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"

make -s unit > "$OUT/unit.txt" 2>&1; echo "unit tests exit=$?" | tee -a "$OUT/unit.txt"
python3 Tests/tools/gen_step3_scenario.py > /dev/null
BUILD_FW=${STEP3_BUILD:-fwtest}; TAG=${STEP3_TAG:-}
make -s BUILD=$BUILD_FW > /dev/null && make -s chips > /dev/null || { echo "build failed"; exit 1; }

for i in 1 2 3; do
    echo "== scenario run $i"
    "$WOKWI" --elf build/$BUILD_FW/firmware.elf --timeout 60000 \
        --scenario Tests/baseline/step3_framework.yaml \
        --serial-log-file "$OUT/scenario_run$i.log" . > "$OUT/scenario_run$i.console.txt" 2>&1
    echo "   wokwi exit=$?" | tee -a "$OUT/scenario_run$i.console.txt"
done

for i in 1 2 3; do
    echo "== gdb run $i"
    FW_ELF=build/$BUILD_FW/firmware.elf bash Tests/tools/gdb_inject_run.sh "$OUT" "gdb_run$i" 3333
    sleep 2
done

python3 Tests/tools/check_step3.py --dir "$OUT" \
    --report results/summaries/step3${TAG}_acceptance.md --csv results/summaries/step3${TAG}_injections.csv
