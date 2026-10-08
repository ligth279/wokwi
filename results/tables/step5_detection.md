# Step 5 - detection results (protected build)

Latency = detection cycle - injection cycle, DWT cycles at 72 MHz (1 ms = 72000 cycles). Source: `results/raw/step5/20261008_132103`.

| Fault | Expected mechanism | Detected by (3 runs) | Latency cycles (run1 / run2 / run3) | Latency ms (run1) | Follow-up |
|---|---|---|---|---|---|
| MEM-01 | CRC/REDUNDANT | CRC | 13682 / 13682 / 13682 | 0.19 |  |
| MEM-01 | CRC/REDUNDANT | REDUNDANT | 1302491 / 1302491 / 1302491 | 18.09 |  |
| MEM-02 | STACK_SEAL | STACK_SEAL | 1297512 / 1297512 / 1297512 | 18.02 |  |
| MEM-02 | STACK_SEAL | WWDG (incidental) | 17641224 / 17641224 / 17641224 | 245.02 | WWDG reset after the sensor task died, reboot |
| CPU-01 | none (no detector can see it in Wokwi) | WWDG (incidental) | 10441224 / 10441224 / 10441224 | 145.02 | WWDG reset, reboot |
| CPU-02 | none (no detector can see it in Wokwi) | WWDG (incidental) | 10441224 / 10441224 / 10441224 | 145.02 | WWDG reset, reboot |
| TIM-01 | WWDG | WWDG | 10441224 / 10441224 / 10441224 | 145.02 | WWDG reset, reboot (RESET breadcrumb=WWDG) |
| TIM-02 | HEARTBEAT | HEARTBEAT | 23482408 / 23482408 / 23482408 | 326.14 | none (detection only) |
| DATA-01 | CRC | CRC | 16763 / 16763 / 16763 | 0.23 |  |
| DATA-02 | CRC/REDUNDANT | CRC | 11916 / 11916 / 11916 | 0.17 |  |
| DATA-02 | CRC/REDUNDANT | REDUNDANT | 1632449 / 1632449 / 1632449 | 22.67 |  |
| PERIPH-01 | I2C_TIMEOUT | I2C_TIMEOUT | 23263326 / 23263326 / 23263326 | 323.10 |  |
| MEM-03 | STACK_CANARY | STACK_CANARY | 450938 / 450938 / 450938 | 6.26 |  |
| MEM-04 | STACK_PAINT | STACK_PAINT | 613279 / 613279 / 613279 | 8.52 |  |
| CPU-03 | FAULT_HANDLER | FAULT_HANDLER | 457498 / 457498 / 457498 | 6.35 |  |

WWDG latencies measure the simulator-workaround timeout (150 ms without monitor progress + 8 ms), not the silicon WWDG timeout (58 ms). MEM-03, MEM-04 and CPU-03 are detector-validation faults added in Step 5, not study faults; CPU-03 runs the fault-handler path with a synthetic frame.
