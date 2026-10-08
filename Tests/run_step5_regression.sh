#!/usr/bin/env bash
# Regression part of the Step 5 acceptance run, into an existing Step 5 raw directory:
#   Tests/run_step5_regression.sh <outdir> [suites...]   suites: step3p step2 step3 step4 (default: all, in this order)
# Writes <outdir>/regression.txt (merged with earlier partial runs) and re-runs the checker.
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=$1; shift
SUITES=${*:-step3p step2 step3 step4}
touch "$OUT/regression.txt"
declare -A V
while read -r line; do for kv in $line; do V[${kv%%=*}]=${kv#*=}; done; done < "$OUT/regression.txt"
for s in $SUITES; do
    case $s in
    step3p) STEP3_BUILD=protfw STEP3_TAG=_protfw bash Tests/run_step3.sh > "$OUT/step3_protfw_suite.txt" 2>&1; V[step3p_exit]=$? ;;
    step2)  bash Tests/run_step2.sh > "$OUT/step2_suite.txt" 2>&1; V[step2_exit]=$? ;;
    step3)  bash Tests/run_step3.sh > "$OUT/step3_suite.txt" 2>&1; V[step3_exit]=$? ;;
    step4)  SKIP_REGRESSION=1 PAR=2 bash Tests/run_step4.sh > "$OUT/step4_suite.txt" 2>&1; V[step4_exit]=$? ;;
    esac
    echo "$s exit=${V[${s}_exit]}"
    V[unit_exit]=${V[unit_exit]:-0}
    V[step2_summary]=results/summaries/step2_acceptance.md; V[step3_summary]=results/summaries/step3_acceptance.md
    V[step3p_summary]=results/summaries/step3_protfw_acceptance.md; V[step4_summary]=results/summaries/step4_acceptance.md
    : > "$OUT/regression.txt"; for k in "${!V[@]}"; do printf '%s=%s ' "$k" "${V[$k]}" >> "$OUT/regression.txt"; done; echo >> "$OUT/regression.txt"
done
make -s BUILD=protected > /dev/null
python3 Tests/tools/check_step5.py --dir "$OUT" | tail -5
