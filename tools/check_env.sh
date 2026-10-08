#!/usr/bin/env bash
# Check that a fresh clone can build, simulate and report. Prints what is missing and how to fix it; exit 1 if anything required is missing.
cd "$(dirname "$0")/.."
bad=0
need() { # name command hint
    if command -v "$2" > /dev/null 2>&1 || [ -x "$2" ]; then printf '  ok       %-26s %s\n' "$1" "$(command -v "$2" 2>/dev/null || echo "$2")"
    else printf '  MISSING  %-26s %s\n' "$1" "$3"; bad=1; fi
}
echo "Toolchain and tools:"
need "arm-none-eabi-gcc" arm-none-eabi-gcc "install the Arm GNU toolchain (Arch: pacman -S arm-none-eabi-gcc arm-none-eabi-newlib)"
need "arm-none-eabi-gdb" arm-none-eabi-gdb "needed for the GDB-assisted injection runs (Arch: arm-none-eabi-gdb)"
need "wokwi-cli" "${WOKWI_CLI:-$HOME/.wokwi/bin/wokwi-cli}" "curl -L https://wokwi.com/ci/install.sh | sh"
need "python3" python3 "python 3.10+"
python3 -c "import matplotlib" 2> /dev/null && echo "  ok       python matplotlib" || { echo "  MISSING  python matplotlib         pip install matplotlib (needed for make report)"; bad=1; }
echo "Project:"
[ -d Drivers/FreeRTOS-Kernel ] && echo "  ok       vendor sources (Drivers/)" || { echo "  MISSING  vendor sources            run tools/fetch_deps.sh (HAL, CMSIS, FreeRTOS at pinned revisions)"; bad=1; }
[ -n "${WOKWI_CLI_TOKEN:-}" ] && echo "  ok       WOKWI_CLI_TOKEN is set (length ${#WOKWI_CLI_TOKEN})" || echo "  note     WOKWI_CLI_TOKEN not set: needed only for simulation runs (https://wokwi.com/dashboard/ci); the free plan has a small monthly CI-minute quota"
[ -f chips/temp-sensor.chip.wasm ] && echo "  ok       custom chips compiled" || echo "  note     custom chips not compiled yet: make chips (first run downloads WASI-SDK via wokwi-cli)"
echo "Quick checks:"
make -s BUILD=baseline > /dev/null 2>&1 && echo "  ok       make BUILD=baseline" || { echo "  FAILED   make BUILD=baseline"; bad=1; }
exit $bad
