#!/usr/bin/env bash
# Re-create Drivers/ at the exact revisions this project was built and tested
# with. The vendor repositories are not committed to this repo.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p Drivers
fetch() { # dir url commit
    if [ ! -d "Drivers/$1/.git" ]; then git clone -q "$2" "Drivers/$1"; fi
    git -C "Drivers/$1" fetch -q origin "$3" 2>/dev/null || git -C "Drivers/$1" fetch -q --unshallow || true
    git -C "Drivers/$1" checkout -q "$3"
    echo "Drivers/$1 @ $(git -C "Drivers/$1" rev-parse --short HEAD)"
}
fetch cmsis-core           https://github.com/STMicroelectronics/cmsis-core.git           01d18147527e30ed69ace1cb03b818de78bc7062
fetch cmsis-device-f1      https://github.com/STMicroelectronics/cmsis-device-f1.git      c8e9a4a4f16b6d2cb2a2083cbe5161025280fb22
fetch stm32f1xx-hal-driver https://github.com/STMicroelectronics/stm32f1xx-hal-driver.git bd368aa335c529641d2e567574885e02deacc64a
fetch FreeRTOS-Kernel      https://github.com/FreeRTOS/FreeRTOS-Kernel.git                dbf70559b27d39c1fdb68dfb9a32140b6a6777a0  # V11.1.0
