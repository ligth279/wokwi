#!/usr/bin/env bash
# One GDB-assisted injection run (Step 3.6).
#   gdb_inject_run.sh <outdir> <name> [port]
# Exit 0 only if: the port was free before, GDB attached, hit the anchor,
# injected, disconnected; the simulation ran to the end of its scenario on
# its own to its --timeout (exit 42); and the port was free again afterwards.
set -uo pipefail
cd "$(dirname "$0")/../.."
OUT=$1 NAME=$2 PORT=${3:-3333}
WOKWI=${WOKWI_CLI:-$(command -v wokwi-cli || echo "$HOME/.wokwi/bin/wokwi-cli")}
ELF=${FW_ELF:-build/fwtest/firmware.elf}
mkdir -p "$OUT"
log() { echo "[gdbrun] $*" | tee -a "$OUT/$NAME.harness.txt"; }
port_busy() { ss -ltn "sport = :$PORT" | grep -q LISTEN; }

: > "$OUT/$NAME.harness.txt"
if port_busy; then log "port_before=BUSY"; exit 2; fi
log "port_before=free port=$PORT"

# No --scenario: with a scenario Wokwi does not hold the CPU at reset for
# GDB, which would make the injection point depend on wall-clock timing.
# The run ends at --timeout (simulated ms), exit code 42.
"$WOKWI" --elf "$ELF" -g "$PORT" --timeout 4000 \
    --serial-log-file "$OUT/$NAME.log" . > "$OUT/$NAME.console.txt" 2>&1 &
SIM=$!

for _ in $(seq 1 60); do port_busy && break; sleep 0.5; done
if ! port_busy; then log "gdb_port_never_opened"; kill "$SIM" 2>/dev/null; exit 3; fi
# The Wokwi stub emits an unsolicited stop reply (S02) while the simulation
# is still being set up; connecting at that moment makes GDB fail with
# "Bogus trace status reply" / "Unknown remote qXfer reply". Let it settle.
sleep "${GDB_SETTLE_S:-5}"

# Same command order as the first working manual session: modes first, then
# connect, then the injection script.
cat > "$OUT/$NAME.cmds.gdb" <<GDBEOF
set pagination off
set confirm off
set non-stop off
target remote localhost:$PORT
source Tests/gdb/fi_inject.gdb
GDBEOF

timeout 120 arm-none-eabi-gdb -q -batch -nx -x "$OUT/$NAME.cmds.gdb" "$ELF" > "$OUT/$NAME.gdb.txt" 2>&1
GDB_RC=$?
log "gdb_exit=$GDB_RC"

SIM_RC=timeout
for _ in $(seq 1 240); do
    if ! kill -0 "$SIM" 2>/dev/null; then wait "$SIM"; SIM_RC=$?; break; fi
    sleep 0.5
done
if [ "$SIM_RC" = timeout ]; then
    log "sim_did_not_finish -> killed"
    kill "$SIM" 2>/dev/null; sleep 1; kill -9 "$SIM" 2>/dev/null
fi
log "sim_exit=$SIM_RC"
sleep 1
if port_busy; then log "port_after=BUSY"; else log "port_after=free"; fi

grep -q GDB_INJECTED "$OUT/$NAME.gdb.txt" && grep -q GDB_DISCONNECTED "$OUT/$NAME.gdb.txt" \
    && [ "$GDB_RC" = 0 ] && [ "$SIM_RC" = 42 ] && ! port_busy
RC=$?
log "result=$([ $RC = 0 ] && echo PASS || echo FAIL)"
exit $RC
