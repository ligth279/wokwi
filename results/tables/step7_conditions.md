# Injection conditions: baseline vs protected campaign

Same fault, same target, same corruption and the same trigger rule in both campaigns: the command `FAULT <ID>` is sent over UART and the fault is injected at the start of the next control cycle (<= 100 ms later). The campaigns differ in WHEN in the run the command is sent: the scenarios chain several experiments (baseline `group`: FI-TEST, MEM-01, DATA-01, DATA-02, PERIPH-01; protected `g1`: MEM-01, DATA-01, DATA-02, MEM-03, MEM-04, MEM-02, TIM-02), so the fault is injected at a different simulated time and at a different phase of the sensor's 20 s temperature sweep, and in the protected chain after earlier recoveries. Effects that depend on the sensor value (MEM-01, DATA-01, DATA-02 output) are therefore compared as deviation from the nominal output for the same sensor reading, not as absolute values. This was not repeated with identical timing because of the Wokwi CI quota (it would need one simulation per fault and build).

| Fault | Injected at, baseline (ms after boot) | Injected at, protected (ms after boot) | Same target and corruption |
|---|---:|---:|---|
| MEM-01 | 4331 | 2675 | yes |
| MEM-02 | 2631 | 27462 | yes |
| CPU-01 | 2631 | 2675 | yes |
| CPU-02 | 2631 | 2675 | yes |
| TIM-01 | 2631 | 2675 | yes |
| TIM-02 | 2631 | 32547 | yes |
| DATA-01 | 6031 | 7575 | yes |
| DATA-02 | 7731 | 12475 | yes |
| PERIPH-01 | 9431 | 2675 | yes |
