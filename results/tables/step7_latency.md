# Detection latency (DWT cycles)

Latency = detection cycle - injection cycle (DWT CYCCNT, 72 MHz; 1 ms = 72 000 cycles). Every individual measurement is kept in `results/summaries/step7_latencies.csv`; min/avg/max below are computed from them. Within one run a fault can be detected by several mechanisms; each is listed.

| Fault | Mechanism | Runs | Min (cycles) | Avg (cycles) | Max (cycles) | Avg (ms @72 MHz) |
|---|---|---:|---:|---:|---:|---:|
| MEM-01 | CRC | 3 | 12 894 | 12 894 | 12 894 | 0.18 |
| MEM-01 | REDUNDANT | 3 | 1 337 170 | 1 337 170 | 1 337 170 | 18.57 |
| MEM-02 | STACK_SEAL | 3 | 1 312 148 | 1 312 148 | 1 312 148 | 18.22 |
| CPU-01 | WWDG | 3 | 10 440 792 | 10 440 792 | 10 440 792 | 145.01 |
| CPU-02 | WWDG | 3 | 10 440 792 | 10 440 792 | 10 440 792 | 145.01 |
| TIM-01 | WWDG | 3 | 10 440 792 | 10 440 792 | 10 440 792 | 145.01 |
| TIM-02 | HEARTBEAT | 3 | 24 057 965 | 24 057 965 | 24 057 965 | 334.14 |
| DATA-01 | CRC | 3 | 16 921 | 16 921 | 16 921 | 0.24 |
| DATA-02 | CRC | 3 | 13 703 | 13 703 | 13 703 | 0.19 |
| DATA-02 | REDUNDANT | 3 | 1 356 201 | 1 356 201 | 1 356 201 | 18.84 |
| PERIPH-01 | I2C_TIMEOUT | 6 | 23 263 728 | 95 618 802 | 167 973 879 | 1328.04 |
| MEM-03 | STACK_CANARY | 3 | 451 123 | 451 123 | 451 123 | 6.27 |
| MEM-04 | STACK_PAINT | 3 | 421 165 | 421 165 | 421 165 | 5.85 |
| CPU-03 | FAULT_HANDLER | 3 | 456 558 | 456 558 | 456 558 | 6.34 |
| TIM-03 | HEARTBEAT | 6 | 23 526 922 | 97 268 663 | 171 010 405 | 1350.95 |

WWDG rows measure the simulator-workaround timeout (150 ms without monitor progress + 8 ms), not the silicon WWDG timeout. The simulation is deterministic: the 3 runs of a fault give identical values.
