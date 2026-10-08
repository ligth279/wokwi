#!/usr/bin/env bash
# Step 5 acceptance run: fault detection on the protected build.
#   - host unit tests
#   - 7 scenarios x 3 runs = 21 Wokwi simulations of build/protected (Tests/protected/step5_*.yaml)
#   - regression (set SKIP_REGRESSION=1 to reuse the last summaries instead):
#       Step 2 suite (includes Steps 0 and 1), Step 3 suite on fwtest and on protfw, Step 4 suite
#   - checker -> results/summaries/step5_acceptance.md, results/tables/step5_detection.md
# Wokwi budget: 21 + 6 + 6 + 6 + 18 = 57 simulations (~25 s wall each, PAR=2).
# From fish:  fish -c 'bash Tests/run_step5.sh'
set -uo pipefail
cd "$(dirname "$0")/.."

OUT=results/raw/step5/$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"
make -s unit > "$OUT/unit.txt" 2>&1; UNIT=$?; echo "unit tests exit=$UNIT"
python3 Tests/tools/gen_step5_scenarios.py > /dev/null
make -s BUILD=protected > /dev/null && make -s chips > /dev/null || { echo "build failed"; exit 1; }
cp build/protected/firmware.elf "$OUT/firmware.elf"

bash Tests/run_step5_sims.sh "$OUT" 3

S2=skipped S3=skipped S3P=skipped S4=skipped
if [ "${SKIP_REGRESSION:-0}" != 1 ]; then
    bash Tests/run_step2.sh > "$OUT/step2_suite.txt" 2>&1; S2=$?; echo "step 2 suite exit=$S2"
    bash Tests/run_step3.sh > "$OUT/step3_suite.txt" 2>&1; S3=$?; echo "step 3 suite exit=$S3"
    STEP3_BUILD=protfw STEP3_TAG=_protfw bash Tests/run_step3.sh > "$OUT/step3_protfw_suite.txt" 2>&1; S3P=$?; echo "step 3 (protfw) suite exit=$S3P"
    SKIP_REGRESSION=1 PAR=2 bash Tests/run_step4.sh > "$OUT/step4_suite.txt" 2>&1; S4=$?; echo "step 4 suite exit=$S4"
    make -s BUILD=protected > /dev/null
fi
echo "unit_exit=$UNIT step2_exit=$S2 step3_exit=$S3 step3p_exit=$S3P step4_exit=$S4 step2_summary=results/summaries/step2_acceptance.md step3_summary=results/summaries/step3_acceptance.md step3p_summary=results/summaries/step3_protfw_acceptance.md step4_summary=results/summaries/step4_acceptance.md" > "$OUT/regression.txt"

python3 Tests/tools/check_step5.py --dir "$OUT"
