# Final comparison: baseline vs protected firmware

Baseline = build `baseline` (no protection), raw logs `results/raw/step4/20261008_135006`. Protected = build `recovery` (detection + recovery), raw logs `results/raw/step6/20261008_144835`. 3 runs per fault and build; results of the 3 runs are identical (deterministic simulation). Latency and time in DWT cycles (72 MHz, 72 000 cycles = 1 ms).

| Fault | Baseline Result | Detected? | Detection Mechanism | Recovery | Recovery Success | Detection Latency (cycles; min / avg / max) | Recovery Time (cycles; avg) |
|---|---|---|---|---|---|---:|---:|
| MEM-01 SRAM bit flip | wrong output | yes | CRC+REDUNDANT | L2 config_restore | 3/3 runs: final state: normal operation | 12 894 / 12 894 / 12 894 | 11 777 526 |
| MEM-02 Stack corruption | crash | yes | STACK_SEAL | L1 task_restart_sensor | 3/3 runs: final state: normal operation | 1 312 148 / 1 312 148 / 1 312 148 | 25 112 052 |
| CPU-01 PC corruption | crash | yes | WWDG | L3 wwdg_reset | 3/3 runs: final state: normal operation | 10 440 792 / 10 440 792 / 10 440 792 | 32 555 770 |
| CPU-02 SP corruption | crash | yes | WWDG | L3 wwdg_reset | 3/3 runs: final state: normal operation | 10 440 792 / 10 440 792 / 10 440 792 | 32 555 765 |
| TIM-01 Infinite loop | hang | yes | WWDG | L3 wwdg_reset | 3/3 runs: final state: normal operation | 10 440 792 / 10 440 792 / 10 440 792 | 32 555 765 |
| TIM-02 Blocked task | task stall | yes | HEARTBEAT | L1 task_restart_sensor | 3/3 runs: final state: normal operation | 24 057 965 / 24 057 965 / 24 057 965 | 24 833 338 |
| DATA-01 Sensor corruption | wrong output | yes | CRC | L2 sample_restore | 3/3 runs: final state: normal operation | 16 921 / 16 921 / 16 921 | 12 798 020 |
| DATA-02 Config corruption | wrong output | yes | CRC+REDUNDANT | L2 config_restore | 3/3 runs: final state: normal operation | 13 703 / 13 703 / 13 703 | 11 782 616 |
| PERIPH-01 I2C stuck-low | sensor reads fail | yes | I2C_TIMEOUT | L1 i2c_bus_recovery (failed) -> L3 software_reset (failed) -> L1 i2c_bus_recovery | 3/3 runs: final state: normal operation | 23 263 728 / 23 263 728 / 23 263 728 | 24 689 003 |

Notes: PERIPH-01 needs two recovery levels (bus recovery fails while the fault is held, a reset releases the trigger, bus recovery then succeeds), so its recovery time is for the last, successful attempt. CPU-01/CPU-02/TIM-01 are detected as hangs by the WWDG shim (simulator workaround) and recovered by the WWDG reset; their time includes the reboot. In the baseline CPU-01, CPU-02 and MEM-02 end the Wokwi simulation itself (code 1006), so no further behaviour of the baseline can be observed after them.
