#!/usr/bin/env bash
# GDB-assisted injection of study faults (build `gdbtest`): MEM-01, DATA-02 (variable corruption by the debugger) and
# CPU-01, CPU-02 (real PC / SP written by the debugger), 3 runs each = 12 simulations, one after the other (GDB port 3333).
#   Tests/run_gdb_study.sh [outdir] [faults...]
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/raw/gdb_study/$(date +%Y%m%d_%H%M%S)}; shift || true
GDB_BUILD=${GDB_BUILD:-gdbtest}   # gdbtest = baseline + GDB injection, gdbprot = recovery build + GDB injection
FAULTS=${*:-MEM-01 DATA-02 CPU-01 CPU-02}
mkdir -p "$OUT"
make -s BUILD="$GDB_BUILD" > /dev/null && make -s chips > /dev/null || { echo "build failed"; exit 1; }
cp "build/$GDB_BUILD/firmware.elf" "$OUT/firmware.elf"   # with debug info: GDB needs the symbols
export FW_ELF="$OUT/firmware.elf"
for f in $FAULTS; do
    for i in 1 2 3; do
        [ -s "$OUT/${f}_run$i.log" ] && grep -q "result=PASS" "$OUT/${f}_run$i.harness.txt" 2>/dev/null && { echo "== $f run $i: kept"; continue; }
        echo "== $f run $i"
        case $f in CPU-*) export EXPECT_SIM_RC="1 42" REQUIRE_GDB_RC0=0 ;; *) export EXPECT_SIM_RC="42" REQUIRE_GDB_RC0=1 ;; esac
        [ "$GDB_BUILD" = gdbprot ] && export SIM_TIMEOUT_MS=7000
        # Wokwi's GDB stub sometimes refuses the first connection ("Unknown remote qXfer reply", see docs/SIMULATOR_LIMITATIONS.md
        # section 8): that is an attach failure, not an experiment result, so retry it (the failed attempt is kept as *.attemptN.*).
        for attempt in 1 2 3; do
            GDB_SETTLE_S=$((4 + 2 * attempt)) GDB_SCRIPT="Tests/gdb/study_$f.gdb" bash Tests/tools/gdb_inject_run.sh "$OUT" "${f}_run$i" 3333 | tail -1
            grep -q "result=PASS" "$OUT/${f}_run$i.harness.txt" && break
            for x in gdb.txt harness.txt console.txt log; do [ -e "$OUT/${f}_run$i.$x" ] && mv "$OUT/${f}_run$i.$x" "$OUT/${f}_run$i.attempt$attempt.$x"; done
            grep -aq "used up your Free plan" "$OUT/${f}_run$i.attempt$attempt.console.txt" && break
            sleep 3
        done
        grep -aq "used up your Free plan" "$OUT/${f}_run$i.console.txt" && { touch "$OUT/QUOTA_EXHAUSTED"; echo "Wokwi CI quota exhausted"; exit 3; }
        sleep 2
    done
done
arm-none-eabi-strip --strip-debug "$OUT/firmware.elf"   # keep the repository small; the symbols are in build/<build> after `make`
python3 Tests/tools/make_index.py > /dev/null 2>&1 || true
echo "done: $OUT"
