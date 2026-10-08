# Step 4 probe runs (corruption-target selection)

Development probes, run once each on 2026-10-08 before the Step 4 campaign, to find corruption targets that
Wokwi survives. They used a temporary build of the Step 3 firmware with a HardFault register dump
(`-DDEBUG_FAULT`) and compile-time overrides of the corruption mask; the build variant and the overrides were
removed afterwards. Scenario: `Tests/baseline/step4_cpu01.yaml` / `step4_cpu02.yaml` / `step4_mem02.yaml`.
One run per probe, so these are observations, not repeated measurements.

| Probe | Fault | Corruption | Result |
|---|---|---|---|
| p1_pc15 | CPU-01 | PC ^ 2^15 (erased flash 0x0800A9FA) | `API Error ... code 1006`, no HardFault dump printed |
| p6_pc_sram | CPU-01 | PC = 0x20002001 (SRAM) | `code 1006`, no dump |
| p5_pc_even | CPU-01 | `bx` to the injector's own even address | simulation ran to the end of the scenario; serial output stopped after INJECTED |
| p2_sp12 | CPU-02 | SP ^ 2^12 (0x20001120 -> 0x20000120, inside SRAM) | `code 1006`, no dump |
| p3_sp28 | CPU-02 | SP ^ 2^28 (0x30001120, outside SRAM) | `code 1006`, no dump |
| p7_sp_flash | CPU-02 | SP = 0x00000100 (flash alias) | `code 1006`, no dump |
| p4_mem02_b15 | MEM-02 | saved LR ^ 2^15 (erased flash) | `code 1006`, no dump |
| p9_mem02_b0 | MEM-02 | saved LR ^ 2^0 (Thumb bit cleared) | no effect: the sensor task kept running (SENSOR seq 27, 28, ... printed) |

Conclusion used in the study: in Wokwi a corrupted PC or SP that leads to an invalid fetch or stack access ends the
simulation (code 1006) instead of raising a HardFault, so the Step 4 faults record that outcome and the fault
registers cannot be observed. See `docs/SIMULATOR_LIMITATIONS.md`, section 11.
