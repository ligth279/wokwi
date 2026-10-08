# Resource overhead

Flash and RAM from `arm-none-eabi-size -B` of the ELF actually simulated in each campaign. RAM includes the FreeRTOS heap array (8 KB baseline, 12 KB protected: larger task stacks, one more task) but not the 2 KB main stack reserved by the linker.

| Resource | Baseline | Detection only (Step 5) | Protected (detection + recovery) | Difference (protected - baseline) | Overhead % |
|---|---:|---:|---:|---:|---:|
| Flash (text+data), bytes | 23000 | 29604 | 37280 | +14280 | 62.1 % |
| RAM (data+bss), bytes | 10168 | 14832 | 15232 | +5064 | 49.8 % |
| CPU busy cycles in the first 10.04 s (fault-free) | 147 199 377 (20.37 %) | - | 254 162 557 (35.06 %) | +106 963 180 | 72.7 % |

CPU measurement: `make BUILD=<baseline|recovery> CPU_STATS=1` adds the same idle-cycle counters to both images (traceTASK_SWITCHED_IN/OUT hooks, [CPUSTAT] line once per second). busy = total elapsed DWT cycles - cycles in the idle task, i.e. application tasks + ISRs + scheduler + detection/recovery; the 10th one-second sample of 3 fault-free runs per image is used. Individual samples (total, idle, busy cycles): baseline [(722699599, 575500222, 147199377, 203), (722699599, 575500222, 147199377, 203), (722699599, 575500222, 147199377, 203)], recovery [(724932494, 470769937, 254162557, 350), (724932494, 470769937, 254162557, 350), (724932494, 470769937, 254162557, 350)].
