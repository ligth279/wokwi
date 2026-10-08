#!/usr/bin/env bash
# Prepare the project to be watched in the Wokwi UI (VS Code extension "Wokwi Simulator"):
#   tools/wokwi_ui.sh [build]      build = baseline | recovery (default) | protected | smoke ...
#   tools/wokwi_ui.sh --restore    put wokwi.toml back (git checkout)
# It builds the firmware and the custom chips and points wokwi.toml at that build. The command-line scripts pass
# --elf explicitly, so changing the default firmware does not affect them.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "${1:-}" = "--restore" ]; then git checkout wokwi.toml; echo "wokwi.toml restored"; exit 0; fi
B=${1:-recovery}
make -s BUILD="$B" > /dev/null
make -s chips > /dev/null
python3 - "$B" <<'PY'
import re, sys
b = sys.argv[1]
s = open("wokwi.toml").read()
s = re.sub(r'^firmware = .*$', f'firmware = "build/{b}/firmware.hex"', s, flags=re.M)
s = re.sub(r'^elf = .*$', f'elf = "build/{b}/firmware.elf"', s, flags=re.M)
open("wokwi.toml", "w").write(s)
PY
echo "wokwi.toml -> build/$B"
cat <<'TXT'

Open this folder in VS Code, press F1 and run "Wokwi: Start Simulator" (diagram.json opens with the board, the temperature sensor
chip and the I2C fault chip). Type into the Serial Monitor and press Enter:
  PING | STATUS | HELP
  FAULT MEM-01 | MEM-02 | CPU-01 | CPU-02 | TIM-01 | TIM-02 | DATA-01 | DATA-02 | PERIPH-01     (all builds)
  FAULT MEM-03 | MEM-04 | CPU-03 | TIM-03                                                       (protected / recovery builds)
Watch the [DETECT] / [RECOVERY] / [SAFE] lines in the serial output.  Do not set gdbServerPort in wokwi.toml (the simulation then waits for GDB).
TXT
