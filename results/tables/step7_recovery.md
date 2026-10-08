# Recovery success rate and recovery time

Success rate = successful recoveries / recovery attempts x 100. An attempt is successful only if its verification of normal operation passed (COMPLETE success=1). Attempts are counted from every RECOVERY START line of the 27 recovery-campaign runs (including the escalation scenarios); escalations count as attempts of their own level.

| Level | Faults tested | Attempts | Successful | Failed | Success rate | Avg recovery time (cycles) | Min | Max |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1 Task restart / bus recovery | MEM-02, MEM-03, MEM-04, PERIPH-01, TIM-02, TIM-03 | 24 | 15 | 9 | 62.5 % | 23 531 383 | 18 522 792 | 25 112 052 |
| 2 Checkpoint restore | DATA-01, DATA-02, MEM-01 | 9 | 9 | 0 | 100.0 % | 12 119 387 | 11 777 526 | 12 798 020 |
| 3 System reset | CPU-01, CPU-02, CPU-03, PERIPH-01, TIM-01, TIM-03 | 27 | 21 | 6 | 77.8 % | 32 619 225 | 32 555 765 | 32 985 620 |
| 4 Safe state | TIM-01, TIM-03 | 6 | 6 | 0 | 100.0 % | 151 296 390 | 151 133 725 | 151 458 849 |
| **Overall** | | 66 | 51 | 15 | **77.3 %** | | | |

## Attempts on the nine study faults (includes the repeated-TIM-01 scenario that ends in the safe state; excludes the validation faults MEM-03, MEM-04, CPU-03, TIM-03)

| Level | Attempts | Successful | Failed | Success rate |
|---|---:|---:|---:|---:|
| 1 Task restart / bus recovery | 12 | 9 | 3 | 75.0 % |
| 2 Checkpoint restore | 9 | 9 | 0 | 100.0 % |
| 3 System reset | 21 | 18 | 3 | 85.7 % |
| 4 Safe state | 3 | 3 | 0 | 100.0 % |
| **Overall (nine study faults)** | 45 | 39 | 6 | **86.7 %** |

## Per fault

| Fault | Attempts (3 runs) | Successful | Rate | Levels used |
|---|---:|---:|---:|---|
| MEM-01 | 3 | 3 | 100.0 % | L2 config_restore |
| MEM-02 | 3 | 3 | 100.0 % | L1 task_restart_sensor |
| CPU-01 | 3 | 3 | 100.0 % | L3 wwdg_reset |
| CPU-02 | 3 | 3 | 100.0 % | L3 wwdg_reset |
| TIM-01 | 3 | 3 | 100.0 % | L3 wwdg_reset |
| TIM-02 | 3 | 3 | 100.0 % | L1 task_restart_sensor |
| DATA-01 | 3 | 3 | 100.0 % | L2 sample_restore |
| DATA-02 | 3 | 3 | 100.0 % | L2 config_restore |
| PERIPH-01 | 9 | 3 | 33.3 % | L1 i2c_bus_recovery, L3 software_reset |
| MEM-03 | 3 | 3 | 100.0 % | L1 task_restart_sensor |
| MEM-04 | 3 | 3 | 100.0 % | L1 task_restart_control |
| CPU-03 | 3 | 3 | 100.0 % | L3 software_reset |
| TIM-03 | 12 | 3 | 25.0 % | L1 task_restart_sensor, L3 software_reset, L4 safe_state |

## Recovery time per attempt (DWT cycles, start of the action to verified normal operation)

| Fault | Level | Action | Time cycles (run1 / run2 / run3) | ms (run 1) |
|---|---:|---|---|---:|
| MEM-01 | 2 | config_restore | 11 777 526 / 11 777 526 / 11 777 526 | 163.58 |
| MEM-02 | 1 | task_restart_sensor | 25 112 052 / 25 112 052 / 25 112 052 | 348.78 |
| CPU-01 | 3 | wwdg_reset | 32 555 770 / 32 555 770 / 32 555 765 | 452.16 |
| CPU-02 | 3 | wwdg_reset | 32 555 765 / 32 555 770 / 32 555 770 | 452.16 |
| TIM-01 | 3 | wwdg_reset | 32 555 765 / 32 555 765 / 32 555 770 | 452.16 |
| TIM-02 | 1 | task_restart_sensor | 24 833 338 / 24 833 338 / 24 833 338 | 344.91 |
| DATA-01 | 2 | sample_restore | 12 798 020 / 12 798 020 / 12 798 020 | 177.75 |
| DATA-02 | 2 | config_restore | 11 782 616 / 11 782 616 / 11 782 616 | 163.65 |
| PERIPH-01 | 1 | i2c_bus_recovery | FAILED (no recovery time) | - |
| PERIPH-01 | 1 | i2c_bus_recovery | 24 689 003 / 24 689 003 / 24 689 003 | 342.90 |
| PERIPH-01 | 3 | software_reset | FAILED (no recovery time) | - |
| MEM-03 | 1 | task_restart_sensor | 24 499 728 / 24 499 728 / 24 499 728 | 340.27 |
| MEM-04 | 1 | task_restart_control | 18 522 792 / 18 522 792 / 18 522 792 | 257.26 |
| CPU-03 | 3 | software_reset | 32 985 620 / 32 985 615 / 32 985 615 | 458.13 |
| TIM-03 | 1 | task_restart_sensor | FAILED (no recovery time) | - |
| TIM-03 | 3 | software_reset | FAILED (no recovery time) | - |
| TIM-03 | 4 | safe_state | 151 458 831 / 151 458 849 / 151 458 849 | 2103.59 |

Level 3 times include the reboot and the verification after it (they are valid in Wokwi, where DWT CYCCNT keeps counting through a reset; on silicon CYCCNT restarts and the persisted start cycle could not be used). Failed attempts show no time on purpose. The WWDG reset path uses the simulator workaround of Step 5.
