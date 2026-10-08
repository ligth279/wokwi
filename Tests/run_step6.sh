#!/usr/bin/env bash
# Steps 6 + 7 campaign on the `recovery` build (resumable):
#   Tests/run_step6.sh [outdir]        new directory results/raw/step6/<ts> unless one is given
#   - 9 scenarios x 3 runs of build/recovery (Tests/recovery/step6_*.yaml)
#   - CPU load: build/baseline_cpu and build/recovery_cpu, 3 fault-free runs each (Tests/cpu/step7_fp.yaml)
# Stops cleanly when the Wokwi CI quota is exhausted (marker file QUOTA_EXHAUSTED); rerun with the same outdir and a
# new token (RESUME=1 keeps the finished runs). Evaluation: Tests/tools/check_step6.py, Tests/tools/eval_step7.py.
# From fish:  fish -c 'bash Tests/run_step6.sh'
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/raw/step6/$(date +%Y%m%d_%H%M%S)}
mkdir -p "$OUT/cpu"
rm -f "$OUT/QUOTA_EXHAUSTED" "$OUT/cpu/QUOTA_EXHAUSTED"
export RESUME=1 PAR=${PAR:-2}

make -s BUILD=recovery > /dev/null && make -s chips > /dev/null || { echo "build failed"; exit 1; }
python3 Tests/tools/gen_step6_scenarios.py > /dev/null
[ -f "$OUT/firmware.elf" ] || arm-none-eabi-strip --strip-debug -o "$OUT/firmware.elf" build/recovery/firmware.elf

SCEN_PREFIX=Tests/recovery/step6_ TIMEOUT=75000 bash Tests/run_step5_sims.sh "$OUT" 3 fp g1 tim01 cpu01 cpu02 cpu03 periph safe_rep safe_fail

# CPU load: same stimulus, two firmware images
make -s BUILD=baseline CPU_STATS=1 > /dev/null && make -s BUILD=recovery CPU_STATS=1 > /dev/null || { echo "cpu build failed"; exit 1; }
for v in baseline recovery; do
    mkdir -p "$OUT/cpu/$v"
    [ -f "$OUT/cpu/$v/firmware.elf" ] || arm-none-eabi-strip --strip-debug -o "$OUT/cpu/$v/firmware.elf" build/${v}_cpu/firmware.elf
    SCEN_PREFIX=Tests/cpu/step7_ TIMEOUT=30000 bash Tests/run_step5_sims.sh "$OUT/cpu/$v" 3 fp
done
for f in "$OUT/QUOTA_EXHAUSTED" "$OUT"/cpu/*/QUOTA_EXHAUSTED; do [ -e "$f" ] && { echo "QUOTA EXHAUSTED: $f"; exit 3; }; done
echo "campaign complete: $OUT"

# refresh the figures and tables of results/report/ from everything that has been run so far
python3 Tests/tools/make_report.py > /dev/null 2>&1 || echo "(make_report failed; run: python3 Tests/tools/make_report.py)"
