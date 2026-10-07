#!/usr/bin/env bash
# Step 2 acceptance run: rebuilds everything, runs Step 0 (smoke), Step 1
# (i2ctest), the UART RX probe and 3 baseline application runs in Wokwi,
# archives the raw serial logs and evaluates the acceptance criteria.
#
# Requires WOKWI_CLI_TOKEN in the environment. From fish:
#   fish -c 'bash Tests/run_step2.sh'
set -euo pipefail
cd "$(dirname "$0")/.."

WOKWI=${WOKWI_CLI:-$(command -v wokwi-cli || echo "$HOME/.wokwi/bin/wokwi-cli")}
OUT=results/raw/step2/$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT"

run() { # name elf timeout_ms extra-args...
    local name=$1 elf=$2 to=$3; shift 3
    echo "== $name"
    "$WOKWI" --elf "$elf" --timeout "$to" --serial-log-file "$OUT/$name.log" "$@" . \
        > "$OUT/$name.console.txt" 2>&1 || echo "   (wokwi-cli exit $?; see $OUT/$name.console.txt)"
}

for b in smoke i2ctest uartrx baseline; do make -s BUILD=$b >/dev/null; done
make -s chips >/dev/null

run smoke    build/smoke/firmware.elf   8000  --expect-text SMOKE_DONE
run i2ctest  build/i2ctest/firmware.elf 4000  --expect-text I2CTEST_DONE
run uartrx   build/uartrx/firmware.elf  6000  --scenario Tests/uart/uart_rx.yaml
for i in 1 2 3; do
    run "baseline_run$i" build/baseline/firmware.elf 30000 --scenario Tests/baseline/step2_app.yaml
done

python3 Tests/tools/check_step2.py \
    --runs "$OUT"/baseline_run{1,2,3}.log --elf build/baseline/firmware.elf \
    --smoke "$OUT/smoke.log" --i2c "$OUT/i2ctest.log" \
    --report results/summaries/step2_acceptance.md
