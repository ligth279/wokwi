# Detection coverage (protected build `recovery`)

Coverage = detected injections / injected faults x 100. One injection = one fault in one run. Detected = a DETECT line carrying the EXP of the injected experiment.

| Fault class | Injected | Detected | Not detected | Coverage |
|---|---:|---:|---:|---:|
| Memory | 6 | 6 | 0 | 100.0 % |
| Cpu | 6 | 6 | 0 | 100.0 % |
| Timing | 6 | 6 | 0 | 100.0 % |
| Data | 6 | 6 | 0 | 100.0 % |
| Peripheral | 3 | 3 | 0 | 100.0 % |
| **Overall (9 study faults x 3 runs)** | 27 | 27 | 0 | **100.0 %** |

Detector-validation faults (MEM-03, MEM-04, CPU-03, TIM-03; not part of the nine): 12/12 detected (100.0 %) - reported separately, not mixed into the table above.

CPU-01 and CPU-02 count as detected because the WWDG detects the resulting hang; no fault-exception detector can act in Wokwi (docs/SIMULATOR_LIMITATIONS.md section 13). Coverage of 100 % holds only for these nine faults at these injection points, not for faults in general.

Per fault (3 runs): MEM-01: 3/3; MEM-02: 3/3; CPU-01: 3/3; CPU-02: 3/3; TIM-01: 3/3; TIM-02: 3/3; DATA-01: 3/3; DATA-02: 3/3; PERIPH-01: 3/3
