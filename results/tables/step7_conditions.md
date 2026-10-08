# Injection conditions: baseline vs protected campaign

Same fault, same target, same corruption and the same trigger rule, but NOT the same time in the run: the equal-timing single-fault runs of Tests/run_followup.sh are missing or incomplete, so the chained scenarios are used (baseline `group`, protected `g1`), which inject at different times and at a different phase of the sensor's 20 s temperature sweep. Sensor-dependent effects (MEM-01, DATA-01, DATA-02 output) must then be compared as deviation from the nominal output for the same reading.

| Fault | Injected at, baseline (ms after boot) | Injected at, protected (ms after boot) | Same target and corruption | Same time of run |
|---|---:|---:|---|---|
| MEM-01 | 4331 | 2675 | yes | no (chained scenario) |
| MEM-02 | 2631 | 27462 | yes | no (chained scenario) |
| CPU-01 | 2631 | 2675 | yes | yes |
| CPU-02 | 2631 | 2675 | yes | yes |
| TIM-01 | 2631 | 2675 | yes | yes |
| TIM-02 | 2631 | 32547 | yes | no (chained scenario) |
| DATA-01 | 6031 | 7575 | yes | no (chained scenario) |
| DATA-02 | 7731 | 12475 | yes | no (chained scenario) |
| PERIPH-01 | 9431 | 2675 | yes | no (chained scenario) |
