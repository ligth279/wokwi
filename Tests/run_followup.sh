#!/usr/bin/env bash
# Follow-up campaigns that close the gaps found in the repository audit (resumable, stops cleanly when the Wokwi quota runs out):
#   1. timer-triggered injection of study faults (baseline build)          3 scenarios x 3 runs  =  9 simulations
#   2. GDB-assisted injection of MEM-01, DATA-02, CPU-01, CPU-02 (gdbtest)    4 faults x 3 runs   = 12 simulations (sequential)
#   3. equal-timing single-fault runs for the baseline vs protected comparison:
#        baseline : mem01 data01 data02 periph  x 3  = 12;  recovery : mem01 data01 data02 mem02 tim02 x 3 = 15
#   Tests/run_followup.sh [root]   root = results/raw/followup (default); each stage keeps its own subdirectory
# Then: python3 Tests/tools/check_followup.py --root <root>  and  python3 Tests/tools/eval_step7.py ... --equiv <root>
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=${1:-results/raw/followup}
mkdir -p "$ROOT"
export RESUME=1 PAR=${PAR:-2}
python3 Tests/tools/gen_followup_scenarios.py > /dev/null
quota() { for f in "$ROOT"/*/QUOTA_EXHAUSTED; do [ -e "$f" ] && { echo "QUOTA EXHAUSTED ($f): give a new token and rerun the same command"; exit 3; }; done; }

# 1. timer
make -s BUILD=baseline > /dev/null && make -s chips > /dev/null || exit 1
mkdir -p "$ROOT/timer"; [ -f "$ROOT/timer/firmware.elf" ] || arm-none-eabi-strip --strip-debug -o "$ROOT/timer/firmware.elf" build/baseline/firmware.elf
SCEN_PREFIX=Tests/timer/ TIMEOUT=40000 bash Tests/run_step5_sims.sh "$ROOT/timer" 3 t_a t_b t_c
quota
# 2. GDB
for d in "$ROOT"/gdb; do mkdir -p "$d"; done
bash Tests/run_gdb_study.sh "$ROOT/gdb" || { quota; exit 1; }
# 3. equal timing
mkdir -p "$ROOT/equiv_baseline" "$ROOT/equiv_recovery"
[ -f "$ROOT/equiv_baseline/firmware.elf" ] || arm-none-eabi-strip --strip-debug -o "$ROOT/equiv_baseline/firmware.elf" build/baseline/firmware.elf
SCEN_PREFIX=Tests/equiv/ TIMEOUT=40000 bash Tests/run_step5_sims.sh "$ROOT/equiv_baseline" 3 e_mem01_base e_data01_base e_data02_base e_periph_base
quota
make -s BUILD=recovery > /dev/null || exit 1
[ -f "$ROOT/equiv_recovery/firmware.elf" ] || arm-none-eabi-strip --strip-debug -o "$ROOT/equiv_recovery/firmware.elf" build/recovery/firmware.elf
SCEN_PREFIX=Tests/equiv/ TIMEOUT=60000 bash Tests/run_step5_sims.sh "$ROOT/equiv_recovery" 3 e_mem01_rec e_data01_rec e_data02_rec e_mem02_rec e_tim02_rec
quota
echo "follow-up campaigns complete: $ROOT"
